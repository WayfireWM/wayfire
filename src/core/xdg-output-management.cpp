// XDG Output Management Wayland protocol
// Inspired by the wlroots implementation
#include "xdg-output-management.hpp"

#include <optional>
#include <algorithm>
#include <wayfire/output-layout.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util.hpp>
#include <wayfire/geometry.hpp>
#include <wlr/types/wlr_output.h>
#include <wayfire/signal-provider.hpp>
#include "view/view-impl.hpp"
#include "wayfire/debug.hpp"
#include "xdg-output-unstable-v1-protocol.h"

namespace wf
{
#define OUTPUT_MANAGER_VERSION 3
#define OUTPUT_DONE_DEPRECATED_SINCE_VERSION 3
#define OUTPUT_DESCRIPTION_MUTABLE_SINCE_VERSION 3

static void output_handle_destroy(wl_client* /* client */, wl_resource *resource)
{
    wl_resource_destroy(resource);
}

constexpr static const struct zxdg_output_v1_interface output_implementation = {
    .destroy = output_handle_destroy,
};

class xdg_output_v1_resource
{
    std::optional<wf::geometry_t> last_sent_geometry;
    wf::wl_listener_wrapper on_description_changed;

  public:
    wl_resource *xdg_output;

    xdg_output_v1_resource(wl_resource *resource, wlr_output *output)
    {
        uint32_t proto_version = wl_resource_get_version(resource);
        this->xdg_output = resource;

        if (proto_version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION)
        {
            zxdg_output_v1_send_name(xdg_output, output->name);
        }

        on_description_changed.set_callback([=] (void*)
        {
            if ((proto_version >= ZXDG_OUTPUT_V1_DESCRIPTION_SINCE_VERSION) && (output->description != NULL))
            {
                zxdg_output_v1_send_description(xdg_output, output->description);
            }
        });

        on_description_changed.emit(NULL);
        if (proto_version >= OUTPUT_DESCRIPTION_MUTABLE_SINCE_VERSION)
        {
            on_description_changed.connect(&output->events.description);
        }
    }

    bool is_xwayland() const
    {
        return wl_resource_get_client(xdg_output) == wf::xwayland_get_client();
    }

    bool resend_details(wf::geometry_t geometry)
    {
        if (last_sent_geometry == geometry)
        {
            return false;
        }

        last_sent_geometry = geometry;
        zxdg_output_v1_send_logical_position(xdg_output, geometry.x, geometry.y);
        zxdg_output_v1_send_logical_size(xdg_output, geometry.width, geometry.height);
        if (wl_resource_get_version(xdg_output) < OUTPUT_DONE_DEPRECATED_SINCE_VERSION)
        {
            zxdg_output_v1_send_done(xdg_output);
        }

        return true;
    }
};

constexpr const struct zxdg_output_manager_v1_interface xdg_output_manager_v1::output_manager_implementation =
{
    .destroy = xdg_output_manager_v1::generic_handle_destroy,
    .get_xdg_output = xdg_output_manager_v1::handle_get_xdg_output,
};

xdg_output_manager_v1::xdg_output_manager_v1(wl_display *display, wf::output_layout_t *layout)
{
    this->global = wl_global_create(display, &zxdg_output_manager_v1_interface,
        OUTPUT_MANAGER_VERSION, this, xdg_output_manager_v1::output_manager_bind);

    this->layout_change.set_callback([this] (wf::output_layout_configuration_changed_signal* /* signal */)
    {
        this->update_outputs();
    });
    layout->connect(&this->layout_change);
}

xdg_output_manager_v1::~xdg_output_manager_v1() = default;

void xdg_output_manager_v1::update_outputs()
{
    auto& ol    = wf::get_core().output_layout;
    auto config = ol->get_current_configuration();

    auto output_visible_for_clients = [&] (wlr_output *output)
    {
        // NULL output is not part of the configuration state, so we need a separate check
        if ((std::string(nonull(output->name)) == "NOOP-1") && output->enabled)
        {
            return true;
        }

        return config.count(output) && (config[output].source & OUTPUT_IMAGE_SOURCE_SELF);
    };

    const auto& update_output = [&] (wlr_output *output, wf::geometry_t geometry,
                                     wf::geometry_t xwayland_geometry)
    {
        bool changed = false;
        for (auto& resource : output_resources[output])
        {
            wf::geometry_t to_send = resource->is_xwayland() ? xwayland_geometry : geometry;
            changed |= resource->resend_details(to_send);
        }

        if (changed)
        {
            wlr_output_schedule_done(output);
        }
    };

    static wf::option_wrapper_t<bool> force_xwayland_scaling{"workarounds/force_xwayland_scaling"};

    int xwayland_location_x = 0;
    auto it = output_resources.begin();
    while (it != output_resources.end())
    {
        if (!output_visible_for_clients(it->first))
        {
            it = output_resources.erase(it);
        } else
        {
            auto wo = ol->find_output(it->first);
            auto geometry = wo->get_layout_geometry();
            if (force_xwayland_scaling)
            {
                int width, height;
                wlr_output_transformed_resolution(it->first, &width, &height);
                wf::geometry_t xwayland_geometry = {xwayland_location_x, 0, width, height};
                update_output(it->first, geometry, xwayland_geometry);
                xwayland_location_x += width;
                wo->get_data_safe<wf::xdg_output_xwayland_geometry>()->geometry = xwayland_geometry;
            } else
            {
                update_output(it->first, geometry, geometry);
                wo->get_data_safe<wf::xdg_output_xwayland_geometry>()->geometry = geometry;
            }

            ++it;
        }
    }
}

void xdg_output_manager_v1::output_manager_bind(wl_client *wl_client, void *data,
    uint32_t version, uint32_t id)
{
    xdg_output_manager_v1 *self = static_cast<xdg_output_manager_v1*>(data);
    wl_resource *resource = wl_resource_create(wl_client, &zxdg_output_manager_v1_interface, version, id);
    if (resource == NULL)
    {
        wl_client_post_no_memory(wl_client);
        return;
    }

    wl_resource_set_implementation(resource, &xdg_output_manager_v1::output_manager_implementation, self,
        NULL);
}

void xdg_output_manager_v1::generic_handle_destroy(wl_client* /* client */, wl_resource *resource)
{
    wl_resource_destroy(resource);
}

void xdg_output_manager_v1::handle_output_resource_destroy(wl_resource *resource)
{
    auto self = static_cast<xdg_output_manager_v1*>(wl_resource_get_user_data(resource));
    for (auto& [output, resources] : self->output_resources)
    {
        resources.erase(std::remove_if(resources.begin(), resources.end(),
            [resource] (const std::unique_ptr<xdg_output_v1_resource>& res)
        {
            return res->xdg_output == resource;
        }), resources.end());
    }
}

void xdg_output_manager_v1::handle_get_xdg_output(wl_client *client, wl_resource *resource,
    uint32_t id, wl_resource *output_resource)
{
    auto self = static_cast<xdg_output_manager_v1*>(wl_resource_get_user_data(resource));
    wlr_output *output     = wlr_output_from_resource(output_resource);
    uint32_t proto_version = wl_resource_get_version(resource);
    if (!output)
    {
        auto inert = wl_resource_create(client, &zxdg_output_v1_interface, proto_version, id);
        wl_resource_set_implementation(inert, &output_implementation, NULL, NULL);
        return;
    }

    wl_resource *xdg_output_resource = wl_resource_create(client,
        &zxdg_output_v1_interface, proto_version, id);

    if (!xdg_output_resource)
    {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(xdg_output_resource, &output_implementation,
        self, xdg_output_manager_v1::handle_output_resource_destroy);

    auto xdg_output = std::make_unique<xdg_output_v1_resource>(xdg_output_resource, output);
    self->output_resources[output].push_back(std::move(xdg_output));
    self->update_outputs();
}
}

#pragma once
#include "wayfire/plugins/ipc/ipc-helpers.hpp"
#include <wayfire/output.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>
#include "config.h"
#include "wayfire/plugins/common/util.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/unstable/wlr-surface-node.hpp>
#include <wayfire/view-helpers.hpp>

static inline wf::json_t output_to_json(wf::output_t *o)
{
    if (!o)
    {
        return wf::json_t::null();
    }

    wf::json_t response;
    response["id"]   = o->get_id();
    response["name"] = o->to_string();
    response["geometry"]   = wf::ipc::geometry_to_json(o->get_layout_geometry());
    response["workarea"]   = wf::ipc::geometry_to_json(o->workarea->get_workarea());
    response["wset-index"] = o->wset()->get_index();
    response["workspace"]["x"] = o->wset()->get_current_workspace().x;
    response["workspace"]["y"] = o->wset()->get_current_workspace().y;
    response["workspace"]["grid_width"]  = o->wset()->get_workspace_grid_size().width;
    response["workspace"]["grid_height"] = o->wset()->get_workspace_grid_size().height;
    return response;
}

static inline pid_t get_view_pid(wayfire_view view)
{
    pid_t pid = -1;
    if (!view)
    {
        return pid;
    }

#if WF_HAS_XWAYLAND
    wlr_surface *wlr_surface = view->get_wlr_surface();
    if (wlr_surface && wlr_xwayland_surface_try_from_wlr_surface(wlr_surface))
    {
        pid = wlr_xwayland_surface_try_from_wlr_surface(wlr_surface)->pid;
    } else
#endif
    if (view && view->get_client())
    {
        wl_client_get_credentials(view->get_client(), &pid, 0, 0);
    }

    return pid; // NOLINT
}

static inline wf::geometry_t get_view_base_geometry(wayfire_view view)
{
    auto sroot = view->get_surface_root_node();
    for (auto& ch : sroot->get_children())
    {
        if (auto wlr_surf = dynamic_cast<wf::scene::wlr_surface_node_t*>(ch.get()))
        {
            auto bbox = wlr_surf->get_bounding_box();
            wf::pointf_t origin = sroot->to_global({0, 0});
            bbox.x = origin.x;
            bbox.y = origin.y;
            return bbox;
        }
    }

    return sroot->get_bounding_box();
}

static inline std::string role_to_string(enum wf::view_role_t role)
{
    switch (role)
    {
      case wf::VIEW_ROLE_TOPLEVEL:
        return "toplevel";

      case wf::VIEW_ROLE_UNMANAGED:
        return "unmanaged";

      case wf::VIEW_ROLE_DESKTOP_ENVIRONMENT:
        return "desktop-environment";

      default:
        return "unknown";
    }
}

static inline std::string layer_to_string(std::optional<wf::scene::layer> layer)
{
    if (!layer.has_value())
    {
        return "none";
    }

    switch (layer.value())
    {
      case wf::scene::layer::BACKGROUND:
        return "background";

      case wf::scene::layer::BOTTOM:
        return "bottom";

      case wf::scene::layer::WORKSPACE:
        return "workspace";

      case wf::scene::layer::TOP:
        return "top";

      case wf::scene::layer::UNMANAGED:
        return "unmanaged";

      case wf::scene::layer::OVERLAY:
        return "overlay";

      case wf::scene::layer::LOCK:
        return "lock";

      case wf::scene::layer::DWIDGET:
        return "dew";

      default:
        break;
    }

    wf::dassert(false, "invalid layer!");
    assert(false); // prevent compiler warning
}

static inline std::string get_view_type(wayfire_view view)
{
    if (view->role == wf::VIEW_ROLE_TOPLEVEL)
    {
        return "toplevel";
    }

    if (view->role == wf::VIEW_ROLE_UNMANAGED)
    {
#if WF_HAS_XWAYLAND
        auto surf = view->get_wlr_surface();
        if (surf && wlr_xwayland_surface_try_from_wlr_surface(surf))
        {
            return "x-or";
        }

#endif

        return "unmanaged";
    }

    auto layer = wf::get_view_layer(view);
    if ((layer == wf::scene::layer::BACKGROUND) || (layer == wf::scene::layer::BOTTOM))
    {
        return "background";
    } else if (layer == wf::scene::layer::TOP)
    {
        return "panel";
    } else if (layer == wf::scene::layer::OVERLAY)
    {
        return "overlay";
    }

    return "unknown";
}

static inline wf::json_t view_to_json(wayfire_view view)
{
    if (!view)
    {
        return wf::json_t::null();
    }

    auto output = view->get_output();
    wf::json_t description;
    description["id"]     = view->get_id();
    description["pid"]    = get_view_pid(view);
    description["title"]  = view->get_title();
    description["app-id"] = view->get_app_id();
    description["base-geometry"] = wf::ipc::geometry_to_json(get_view_base_geometry(view));
    auto toplevel = wf::toplevel_cast(view);
    description["parent"]   = toplevel && toplevel->parent ? (int)toplevel->parent->get_id() : -1;
    description["geometry"] =
        wf::ipc::geometry_to_json(toplevel ? toplevel->get_pending_geometry() : view->get_bounding_box());
    description["bbox"] = wf::ipc::geometry_to_json(view->get_bounding_box());
    description["output-id"]   = view->get_output() ? view->get_output()->get_id() : -1;
    description["output-name"] = output ? output->to_string() : "null";
    description["last-focus-timestamp"] = wf::get_focus_timestamp(view);
    description["role"]   = role_to_string(view->role);
    description["mapped"] = view->is_mapped();
    description["layer"]  = layer_to_string(get_view_layer(view));
    description["tiled-edges"] = toplevel ? toplevel->pending_tiled_edges() : 0;
    description["fullscreen"]  = toplevel ? toplevel->pending_fullscreen() : false;
    description["minimized"]   = toplevel ? toplevel->minimized : false;
    description["activated"]   = toplevel ? toplevel->activated : false;
    description["sticky"]     = toplevel ? toplevel->sticky : false;
    description["wset-index"] = toplevel && toplevel->get_wset() ?
        static_cast<int64_t>(toplevel->get_wset()->get_index()) :
        -1;
    description["min-size"] = wf::ipc::dimensions_to_json(
        toplevel ? toplevel->toplevel()->get_min_size() : wf::dimensions_t{0, 0});
    description["max-size"] = wf::ipc::dimensions_to_json(
        toplevel ? toplevel->toplevel()->get_max_size() : wf::dimensions_t{0, 0});
    description["focusable"] = view->is_focusable();
    description["type"] = get_view_type(view);

    return description;
}

static inline wf::json_t wset_to_json(wf::workspace_set_t *wset)
{
    if (!wset)
    {
        return wf::json_t::null();
    }

    wf::json_t response;
    response["index"] = wset->get_index();
    response["name"]  = wset->to_string();

    auto output = wset->get_attached_output();
    response["output-id"]   = output ? (int)output->get_id() : -1;
    response["output-name"] = output ? output->to_string() : "";
    response["workspace"]["x"] = wset->get_current_workspace().x;
    response["workspace"]["y"] = wset->get_current_workspace().y;
    response["workspace"]["grid_width"]  = wset->get_workspace_grid_size().width;
    response["workspace"]["grid_height"] = wset->get_workspace_grid_size().height;
    return response;
}

static inline wf::json_t get_keyboard_state(wlr_keyboard *keyboard)
{
    const auto& get_layout_name = [&] (xkb_layout_index_t layout)
    {
        auto layout_name = xkb_keymap_layout_get_name(keyboard->keymap, layout);
        return layout_name ? layout_name : "unknown";
    };

    wf::json_t state;
    state["possible-layouts"] = wf::json_t::array();
    if (keyboard)
    {
        auto layout = xkb_state_serialize_layout(keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
        state["layout"] = get_layout_name(layout);
        state["layout-index"] = layout;

        auto n_layouts = xkb_keymap_num_layouts(keyboard->keymap);
        for (size_t i = 0; i < n_layouts; i++)
        {
            state["possible-layouts"].append(get_layout_name(i));
        }
    } else
    {
        state["layout"] = "unknown";
    }

    return state;
}

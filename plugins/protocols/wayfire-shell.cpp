/**
 * Implementation of the wayfire-shell-unstable-v2 protocol
 */
#include <wayland-client.h>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/util/log.hpp>

#include <memory>
#include <wayfire/plugin.hpp>

#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire-shell-unstable-v2-protocol.h"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/plugins/ipc/ipc-activator.hpp"
#include "wayfire/util.hpp"

#define ZWF_SHELL_MANAGER_V2_CURRENT_VERSION 3

/* ----------------------------- wfs_hotspot -------------------------------- */
static void handle_hotspot_destroy(wl_resource *resource);

/**
 * Represents a zwf_shell_hotspot_v2.
 * Lifetime is managed by the resource.
 */
class wfs_hotspot
{
  private:
    wl_resource *shell_resource;
    wf::geometry_t trigger;
    wf::geometry_t hotspot_geometry;

    uint32_t threshold;
    bool hotspot_triggered = false;
    wf::wl_idle_call idle_check_input;
    wf::wl_timer<false> timer;

    uint32_t timeout_ms;
    wl_resource *hotspot_resource;

    wf::signal::connection_t<wf::post_input_event_signal<wlr_tablet_tool_axis_event>> on_tablet_axis =
        [=] (wf::post_input_event_signal<wlr_tablet_tool_axis_event> *ev)
    {
        idle_check_input.run_once([=] ()
        {
            auto gcf = wf::get_core().get_cursor_position();
            wf::point_t gc{(int)gcf.x, (int)gcf.y};
            process_input_motion(gc);
        });
    };

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> on_motion_event =
        [=] (auto)
    {
        idle_check_input.run_once([=] ()
        {
            auto gcf = wf::get_core().get_cursor_position();
            wf::point_t gc{(int)gcf.x, (int)gcf.y};
            process_input_motion(gc);
        });
    };

    wf::signal::connection_t<wf::post_input_event_signal<wlr_touch_motion_event>> on_touch_motion = [=] (auto)
    {
        idle_check_input.run_once([=] ()
        {
            auto gcf = wf::get_core().get_touch_position(0);
            wf::point_t gc{(int)gcf.x, (int)gcf.y};
            process_input_motion(gc);
        });
    };

    wf::signal::connection_t<wf::output_removed_signal> on_output_removed;

    void process_input_motion(wf::point_t gc)
    {
        if (is_cursor_in_hotspot(gc))
        {
            if (hotspot_triggered)
            {
                send_proximity_if_needed(gc);
            } else if (!timer.is_connected())
            {
                timer.set_timeout(timeout_ms, [=] ()
                {
                    hotspot_triggered = true;
                    zwf_hotspot_v2_send_enter(hotspot_resource);
                    send_proximity_if_needed(gc);
                });
            }
        } else
        {
            if (hotspot_triggered)
            {
                zwf_hotspot_v2_send_leave(hotspot_resource);
                hotspot_triggered = false;
            }

            timer.disconnect();
        }
    }

    wf::geometry_t calculate_hotspot_geometry(wf::output_t *output, const wf::geometry_t& trigger_rect,
        uint32_t threshold) const
    {
        // Get output geometry in global coordinates
        wf::geometry_t output_geom = output->get_layout_geometry();

        // Expand core rectangle by threshold in all directions
        wf::geometry_t expanded = {
            trigger_rect.x - (int)threshold,
            trigger_rect.y - (int)threshold,
            trigger_rect.width + (int)threshold * 2,
            trigger_rect.height + (int)threshold * 2
        };

        // Clamp to output boundaries
        wf::geometry_t result = wf::geometry_intersection(expanded, output_geom);

        return result;
    }

    uint32_t calculate_proximity(const wf::point_t& cursor) const
    {
        const auto& rect = trigger;
        int dx = 0, dy = 0;

        if (cursor.x < rect.x)
        {
            dx = rect.x - cursor.x;
        } else if ((rect.width > 0) && (cursor.x > rect.x + rect.width))
        {
            dx = cursor.x - (rect.x + rect.width);
        }

        if (cursor.y < rect.y)
        {
            dy = rect.y - cursor.y;
        } else if ((rect.height > 0) && (cursor.y > rect.y + rect.height))
        {
            dy = cursor.y - (rect.y + rect.height);
        }

        // Inside the core rectangle
        if ((dx == 0) && (dy == 0))
        {
            return 0;
        }

        // Euclidean distance to corner
        if ((dx > 0) && (dy > 0))
        {
            return std::min((uint32_t)std::sqrt(dx * dx + dy * dy), threshold);
        } else
        {
            return std::min((uint32_t)(dx + dy), threshold);
        }
    }

    bool is_cursor_in_hotspot(const wf::point_t& cursor) const
    {
        // Quick rejection using bounding box
        if (!(hotspot_geometry & cursor))
        {
            return false;
        }

        // Exact distance check
        return calculate_proximity(cursor) <= threshold;
    }

    void send_proximity_if_needed(const wf::point_t& cursor)
    {
        if (wl_resource_get_version(hotspot_resource) < ZWF_HOTSPOT_V2_PROXIMITY_CHANGED_SINCE_VERSION)
        {
            return;
        }

        if (hotspot_triggered)
        {
            uint32_t proximity = calculate_proximity(cursor);
            zwf_hotspot_v2_send_proximity_changed(hotspot_resource, proximity);
        }
    }

    wfs_hotspot(const wfs_hotspot&) = delete;
    wfs_hotspot(wfs_hotspot&&) = delete;
    wfs_hotspot& operator =(const wfs_hotspot&) = delete;
    wfs_hotspot& operator =(wfs_hotspot&&) = delete;

  public:

    /**
     * Create a new hotspot.
     * It is guaranteedd that edge_mask contains at most 2 non-opposing edges.
     */
    wfs_hotspot(wf::output_t *output, wl_resource *shell_resource, wf::geometry_t trigger, uint32_t threshold,
        uint32_t timeout,
        wl_client *client, uint32_t id) : trigger(trigger), threshold(threshold), timeout_ms(timeout)
    {
        // Calculate expanded bounding box
        this->hotspot_geometry = calculate_hotspot_geometry(output, trigger, threshold);

        // For version checking
        this->shell_resource = shell_resource;

        // Create resource and setup signals (same as before)
        hotspot_resource =
            wl_resource_create(client, &zwf_hotspot_v2_interface,
                std::min(wl_resource_get_version(shell_resource), ZWF_SHELL_MANAGER_V2_CURRENT_VERSION), id);
        wl_resource_set_implementation(hotspot_resource, NULL, this, handle_hotspot_destroy);

        // output destroy handler
        on_output_removed.set_callback([this, output] (wf::output_removed_signal *ev)
        {
            if (ev->output == output)
            {
                this->hotspot_geometry = {0, 0, 0, 0};
                this->trigger = {0, 0, 0, 0};
                process_input_motion({-1, -1});
            }
        });

        wf::get_core().connect(&on_motion_event);
        wf::get_core().connect(&on_touch_motion);
        wf::get_core().connect(&on_tablet_axis);
        wf::get_core().output_layout->connect(&on_output_removed);
    }

    ~wfs_hotspot() = default;
};

static void handle_hotspot_destroy(wl_resource *resource)
{
    auto *hotspot = (wfs_hotspot*)wl_resource_get_user_data(resource);

    delete hotspot;

    wl_resource_set_user_data(resource, nullptr);
}

/* ------------------------------ wfs_output -------------------------------- */
static void handle_output_destroy(wl_resource *resource);
static void handle_zwf_output_inhibit_output(wl_client*, wl_resource *resource);
static void handle_zwf_output_inhibit_output_done(wl_client*, wl_resource *resource);
static void handle_zwf_output_create_hotspot(wl_client*, wl_resource *resource, uint32_t hotspot,
    uint32_t threshold, uint32_t timeout, uint32_t id);
static void handle_zwf_output_create_custom_hotspot(wl_client*, wl_resource *resource, uint32_t x, uint32_t y,
    uint32_t w, uint32_t h, uint32_t threshold,
    uint32_t timeout, uint32_t id);

static struct zwf_output_v2_interface zwf_output_impl = {
    .inhibit_output = handle_zwf_output_inhibit_output,
    .inhibit_output_done = handle_zwf_output_inhibit_output_done,
    .create_hotspot = handle_zwf_output_create_hotspot,
    .create_custom_hotspot = handle_zwf_output_create_custom_hotspot,
};

/**
 * A signal emitted on the wayfire output where the menu should be toggled.
 */
struct wayfire_shell_toggle_menu_signal
{};

/**
 * Represents a zwf_output_v2.
 * Lifetime is managed by the wl_resource
 */
class wfs_output
{
    wl_resource *shell_resource;
    wl_resource *resource;
    wf::output_t *output;

    void disconnect_from_output()
    {
        wf::get_core().output_layout->disconnect(&on_output_removed);
        on_fullscreen_layer_focused.disconnect();
    }

    wf::signal::connection_t<wf::output_removed_signal> on_output_removed =
        [=] (wf::output_removed_signal *ev)
    {
        if (ev->output == this->output)
        {
            disconnect_from_output();
            this->output = nullptr;
        }
    };

    wf::signal::connection_t<wf::fullscreen_layer_focused_signal> on_fullscreen_layer_focused =
        [=] (wf::fullscreen_layer_focused_signal *ev)
    {
        if (ev->has_promoted)
        {
            zwf_output_v2_send_enter_fullscreen(resource);
        } else
        {
            zwf_output_v2_send_leave_fullscreen(resource);
        }
    };

    wf::signal::connection_t<wayfire_shell_toggle_menu_signal> on_toggle_menu = [=] (auto)
    {
        if (wl_resource_get_version(shell_resource) < ZWF_OUTPUT_V2_TOGGLE_MENU_SINCE_VERSION)
        {
            return;
        }

        zwf_output_v2_send_toggle_menu(resource);
    };

  public:
    wfs_output(wf::output_t *output, wl_resource *shell_resource, wl_client *client, int id)
    {
        this->output = output;
        this->shell_resource = shell_resource;

        resource =
            wl_resource_create(client, &zwf_output_v2_interface,
                std::min(wl_resource_get_version(shell_resource), ZWF_SHELL_MANAGER_V2_CURRENT_VERSION), id);
        wl_resource_set_implementation(resource, &zwf_output_impl, this, handle_output_destroy);
        output->connect(&on_fullscreen_layer_focused);
        output->connect(&on_toggle_menu);
        wf::get_core().output_layout->connect(&on_output_removed);
    }

    ~wfs_output()
    {
        if (!this->output)
        {
            /* The wayfire output was destroyed. Gracefully do nothing */
            return;
        }

        disconnect_from_output();
    }

    wfs_output(const wfs_output&) = delete;
    wfs_output(wfs_output&&) = delete;
    wfs_output& operator =(const wfs_output&) = delete;
    wfs_output& operator =(wfs_output&&) = delete;

    void inhibit_output()
    {
        if (this->output)
        {
            LOGW(
                "Ignoring inhibit call, left in for historical reasons.\
                Wayfire now manages all inhibits internally");
        }
    }

    void inhibit_output_done()
    {
        if (this->output)
        {
            LOGW(
                "Ignoring inhibit done call, left in for historical reasons.\
                Wayfire now manages all inhibits internally");
        }
    }

    wf::geometry_t calculate_trigger_geometry(uint32_t edge_mask)
    {
        wf::geometry_t output_geom = this->output->get_layout_geometry();

        // Count how many edge flags are set in the mask
        int edge_count = __builtin_popcount(edge_mask);

        if (edge_count == 1)
        {
            // Single edge: rectangle with zero thickness
            if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_TOP)
            {
                return {output_geom.x, output_geom.y, output_geom.width, 0};
            } else if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_BOTTOM)
            {
                return {output_geom.x, output_geom.y + output_geom.height - 1, output_geom.width, 0};
            } else if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_LEFT)
            {
                return {output_geom.x, output_geom.y, 0, output_geom.height};
            } else // RIGHT
            {
                return {output_geom.x + output_geom.width - 1, output_geom.y, 0, output_geom.height};
            }
        } else if (edge_count == 2)
        {
            // Corner: zero-area point at the corner
            int x = output_geom.x;
            int y = output_geom.y;

            if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_RIGHT)
            {
                x = output_geom.x + output_geom.width - 1;
            }

            if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_BOTTOM)
            {
                y = output_geom.y + output_geom.height - 1;
            }

            return {x, y, 0, 0};
        }

        // Invalid (e.g., 0 edges, 3 edges, or all 4 edges set)
        return {0, 0, 0, 0};
    }

    wf::geometry_t calculate_trigger_geometry(wf::geometry_t rect)
    {
        wf::geometry_t output_geom = this->output->get_layout_geometry();

        wf::geometry_t trigger = {
            output_geom.x + rect.x,
            output_geom.y + rect.y,
            rect.width,
            rect.height
        };

        return trigger;
    }

    void create_hotspot(const wf::geometry_t& trigger_rect, uint32_t threshold, uint32_t timeout, uint32_t id)
    {
        if (!this->output)
        {
            auto resource = wl_resource_create(wl_resource_get_client(
                this->resource), &zwf_hotspot_v2_interface,
                std::min(wl_resource_get_version(shell_resource), ZWF_SHELL_MANAGER_V2_CURRENT_VERSION), id);
            wl_resource_set_implementation(resource, NULL, NULL, NULL);
            return;
        }

        new wfs_hotspot(this->output, this->shell_resource, trigger_rect, threshold, timeout, wl_resource_get_client(
            this->resource), id);
    }
};

static void handle_zwf_output_inhibit_output(wl_client*, wl_resource *resource)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);

    output->inhibit_output();
}

static void handle_zwf_output_inhibit_output_done(wl_client*, wl_resource *resource)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);

    output->inhibit_output_done();
}

static void handle_zwf_output_create_hotspot(wl_client*, wl_resource *resource,
    uint32_t edge_mask, uint32_t threshold, uint32_t timeout, uint32_t id)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    wf::geometry_t trigger = output->calculate_trigger_geometry(edge_mask);

    output->create_hotspot(trigger, threshold, timeout, id);
}

static void handle_zwf_output_create_custom_hotspot(wl_client*, wl_resource *resource,
    uint32_t x, uint32_t y, uint32_t width, uint32_t height,
    uint32_t threshold, uint32_t timeout, uint32_t id)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    wf::geometry_t trigger = output->calculate_trigger_geometry({(int)x, (int)y, (int)width, (int)height});

    output->create_hotspot(trigger, threshold, timeout, id);
}

static void handle_output_destroy(wl_resource *resource)
{
    auto *output = (wfs_output*)wl_resource_get_user_data(resource);

    delete output;

    wl_resource_set_user_data(resource, nullptr);
}

/* ------------------------------ wfs_surface ------------------------------- */
static void handle_surface_destroy(wl_resource *resource);
static void handle_zwf_surface_interactive_move(wl_client*, wl_resource *resource);

static struct zwf_surface_v2_interface zwf_surface_impl = {
    .interactive_move = handle_zwf_surface_interactive_move,
};

/**
 * Represents a zwf_surface_v2.
 * Lifetime is managed by the wl_resource
 */
class wfs_surface
{
    wl_resource *resource;
    wayfire_view view;

    wf::signal::connection_t<wf::view_unmapped_signal> on_unmap = [=] (auto)
    {
        view = nullptr;
    };

  public:
    wfs_surface(wayfire_view view, wl_client *client, int id)
    {
        this->view = view;
        resource   = wl_resource_create(client, &zwf_surface_v2_interface,
            ZWF_SHELL_MANAGER_V2_CURRENT_VERSION, id);
        wl_resource_set_implementation(resource, &zwf_surface_impl, this, handle_surface_destroy);
        view->connect(&on_unmap);
    }

    ~wfs_surface() = default;
    void interactive_move()
    {
        LOGE("Interactive move no longer supported!");
    }
};

static void handle_zwf_surface_interactive_move(wl_client*, wl_resource *resource)
{
    auto surface = (wfs_surface*)wl_resource_get_user_data(resource);

    surface->interactive_move();
}

static void handle_surface_destroy(wl_resource *resource)
{
    auto surface = (wfs_surface*)wl_resource_get_user_data(resource);

    delete surface;
    wl_resource_set_user_data(resource, nullptr);
}

static void zwf_shell_manager_get_wf_output(wl_client *client, wl_resource *resource, wl_resource *output,
    uint32_t id)
{
    auto wlr_out = (wlr_output*)wl_resource_get_user_data(output);
    auto wo = wf::get_core().output_layout->find_output(wlr_out);

    if (wo)
    {
        // will be deleted when the resource is destroyed
        new wfs_output(wo, resource, client, id);
    }
}

static void zwf_shell_manager_get_wf_surface(wl_client *client, wl_resource *resource, wl_resource *surface,
    uint32_t id)
{
    auto view = wf::wl_surface_to_wayfire_view(surface);

    if (view)
    {
        /* Will be freed when the resource is destroyed */
        new wfs_surface(view, client, id);
    }
}

const struct zwf_shell_manager_v2_interface zwf_shell_manager_v2_impl =
{
    zwf_shell_manager_get_wf_output,
    zwf_shell_manager_get_wf_surface,
};

void bind_zwf_shell_manager(wl_client *client, void *data,
    uint32_t version, uint32_t id)
{
    auto resource = wl_resource_create(client, &zwf_shell_manager_v2_interface, version, id);

    wl_resource_set_implementation(resource, &zwf_shell_manager_v2_impl, NULL, NULL);
}

struct wayfire_shell
{
    wl_global *shell_manager;
};

wayfire_shell *wayfire_shell_create(wl_display *display)
{
    wayfire_shell *ws = new wayfire_shell;

    ws->shell_manager = wl_global_create(display, &zwf_shell_manager_v2_interface,
        ZWF_SHELL_MANAGER_V2_CURRENT_VERSION, NULL,
        bind_zwf_shell_manager);

    if (ws->shell_manager == NULL)
    {
        LOGE("Failed to create wayfire_shell interface");
        delete ws;

        return NULL;
    }

    return ws;
}

class wayfire_shell_protocol_impl : public wf::plugin_interface_t
{
    wf::ipc_activator_t toggle_menu{"wayfire-shell/toggle_menu"};
    wf::ipc_activator_t::handler_t toggle_menu_cb = [=] (wf::output_t *toggle_menu_output, wayfire_view)
    {
        wayfire_shell_toggle_menu_signal toggle_menu;
        toggle_menu_output->emit(&toggle_menu);
        return true;
    };

  public:
    void init() override
    {
        wf_shell = wayfire_shell_create(wf::get_core().display);
        toggle_menu.set_handler(toggle_menu_cb);
    }

    void fini() override
    {
        wl_global_destroy(wf_shell->shell_manager);
        delete wf_shell;
    }

  private:
    wayfire_shell *wf_shell;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_shell_protocol_impl);

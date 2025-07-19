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
#include "wayfire/render-manager.hpp"
#include "wayfire-shell-unstable-v2-protocol.h"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/plugins/ipc/ipc-activator.hpp"
#include "wayfire/util.hpp"

/* ----------------------------- wfs_hotspot -------------------------------- */
static void handle_hotspot_destroy(wl_resource *resource);

/**
 * Represents a zwf_shell_hotspot_v2.
 * Lifetime is managed by the resource.
 */
class wfs_hotspot
{
  private:
    wf::geometry_t hotspot_geometry;

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
        if (!(hotspot_geometry & gc))
        {
            if (hotspot_triggered)
            {
                zwf_hotspot_v2_send_leave(hotspot_resource);
            }

            /* Cursor outside of the hotspot */
            hotspot_triggered = false;
            timer.disconnect();

            return;
        }

        if (hotspot_triggered)
        {
            /* Hotspot was already triggered, wait for the next time the cursor
             * enters the hotspot area to trigger again */
            return;
        }

        if (!timer.is_connected())
        {
            timer.set_timeout(timeout_ms, [=] ()
            {
                hotspot_triggered = true;
                zwf_hotspot_v2_send_enter(hotspot_resource);
            });
        }
    }

    wf::geometry_t calculate_hotspot_geometry(wf::output_t *output,
        uint32_t edge_mask, uint32_t distance) const
    {
        wf::geometry_t slot = output->get_layout_geometry();
        if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_TOP)
        {
            slot.height = distance;
        } else if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_BOTTOM)
        {
            slot.y += slot.height - distance;
            slot.height = distance;
        }

        if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_LEFT)
        {
            slot.width = distance;
        } else if (edge_mask & ZWF_OUTPUT_V2_HOTSPOT_EDGE_RIGHT)
        {
            slot.x    += slot.width - distance;
            slot.width = distance;
        }

        return slot;
    }

    wfs_hotspot(const wfs_hotspot &) = delete;
    wfs_hotspot(wfs_hotspot &&) = delete;
    wfs_hotspot& operator =(const wfs_hotspot&) = delete;
    wfs_hotspot& operator =(wfs_hotspot&&) = delete;

  public:
    /**
     * Create a new hotspot.
     * It is guaranteedd that edge_mask contains at most 2 non-opposing edges.
     */
    wfs_hotspot(wf::output_t *output, uint32_t edge_mask,
        uint32_t distance, uint32_t timeout, wl_client *client, uint32_t id)
    {
        this->timeout_ms = timeout;
        this->hotspot_geometry =
            calculate_hotspot_geometry(output, edge_mask, distance);

        hotspot_resource =
            wl_resource_create(client, &zwf_hotspot_v2_interface, 1, id);
        wl_resource_set_implementation(hotspot_resource, NULL, this,
            handle_hotspot_destroy);

        // setup output destroy listener
        on_output_removed.set_callback([this, output] (wf::output_removed_signal *ev)
        {
            if (ev->output == output)
            {
                /* Make hotspot inactive by setting the region to empty */
                hotspot_geometry = {0, 0, 0, 0};
                process_input_motion({0, 0});
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
static void handle_zwf_output_inhibit_output_done(wl_client*,
    wl_resource *resource);
static void handle_zwf_output_create_hotspot(wl_client*, wl_resource *resource,
    uint32_t hotspot, uint32_t threshold, uint32_t timeout, uint32_t id);

static struct zwf_output_v2_interface zwf_output_impl = {
    .inhibit_output = handle_zwf_output_inhibit_output,
    .inhibit_output_done = handle_zwf_output_inhibit_output_done,
    .create_hotspot = handle_zwf_output_create_hotspot,
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
    uint32_t num_inhibits = 0;
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
                std::min(wl_resource_get_version(shell_resource), 2), id);
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
        /* Remove any remaining inhibits, otherwise the compositor will never
         * be "unlocked" */
        while (num_inhibits > 0)
        {
            this->output->render->add_inhibit(false);
            --num_inhibits;
        }
    }

    wfs_output(const wfs_output &) = delete;
    wfs_output(wfs_output &&) = delete;
    wfs_output& operator =(const wfs_output&) = delete;
    wfs_output& operator =(wfs_output&&) = delete;

    void inhibit_output()
    {
        ++this->num_inhibits;
        if (this->output)
        {
            this->output->render->add_inhibit(true);
        }
    }

    void inhibit_output_done()
    {
        if (this->num_inhibits == 0)
        {
            wl_resource_post_no_memory(resource);

            return;
        }

        --this->num_inhibits;
        if (this->output)
        {
            this->output->render->add_inhibit(false);
        }
    }

    void create_hotspot(uint32_t hotspot, uint32_t threshold, uint32_t timeout,
        uint32_t id)
    {
        if (!this->output)
        {
            // It can happen that the client requests a hotspot immediately after an output is destroyed -
            // this is an inherent race condition because the compositor and client are not in sync.
            //
            // In this case, we create a dummy hotspot resource to avoid Wayland protocol errors.
            auto resource = wl_resource_create(
                wl_resource_get_client(this->resource), &zwf_hotspot_v2_interface, 1, id);
            wl_resource_set_implementation(resource, NULL, NULL, NULL);
            return;
        }

        // will be auto-deleted when the resource is destroyed by the client
        new wfs_hotspot(this->output, hotspot, threshold, timeout,
            wl_resource_get_client(this->resource), id);
    }
};

static void handle_zwf_output_inhibit_output(wl_client*, wl_resource *resource)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    output->inhibit_output();
}

static void handle_zwf_output_inhibit_output_done(
    wl_client*, wl_resource *resource)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    output->inhibit_output_done();
}

static void handle_zwf_output_create_hotspot(wl_client*, wl_resource *resource,
    uint32_t hotspot, uint32_t threshold, uint32_t timeout, uint32_t id)
{
    auto output = (wfs_output*)wl_resource_get_user_data(resource);
    output->create_hotspot(hotspot, threshold, timeout, id);
}

static void handle_output_destroy(wl_resource *resource)
{
    auto *output = (wfs_output*)wl_resource_get_user_data(resource);
    delete output;

    wl_resource_set_user_data(resource, nullptr);
}

/* ------------------------------ wfs_surface ------------------------------- */
static void handle_surface_destroy(wl_resource *resource);
static void handle_zwf_surface_interactive_move(wl_client*,
    wl_resource *resource);

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
        resource   = wl_resource_create(client, &zwf_surface_v2_interface, 1, id);
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

static void handle_keyboard_lang_manager_set_layout(wl_client *client, wl_resource *resource, uint32_t layout_idx)
{
    auto seat     = wf::get_core().get_current_seat();
    auto keyboard = wlr_seat_get_keyboard(seat);

    if (!keyboard)
    {
        return;
    }

    if (layout_idx >= xkb_keymap_num_layouts(keyboard->keymap))
    {
        return;
    }

    wlr_keyboard_notify_modifiers(keyboard, keyboard->modifiers.depressed,
        keyboard->modifiers.latched, keyboard->modifiers.locked, layout_idx);
}

static void handle_keyboard_lang_manager_destroy(wl_resource *resource);

static struct zwf_keyboard_lang_manager_v2_interface zwf_keyboard_lang_manager_impl = {
    .set_layout = handle_keyboard_lang_manager_set_layout
};

class wfs_keyboard_lang_manager
{
    wl_resource *resource;
    xkb_layout_index_t current_layout;

    wf::signal::connection_t<wf::input_event_signal<mwlr_keyboard_modifiers_event>> on_keyboard_modifiers =
        [=] (wf::input_event_signal<mwlr_keyboard_modifiers_event> *ev)
    {
        auto keyboard = wlr_keyboard_from_input_device(ev->device);
        if (current_layout == keyboard->modifiers.group) {
            return;
        }

        current_layout = keyboard->modifiers.group;
        wl_resource_post_event(resource, ZWF_KEYBOARD_LANG_MANAGER_V2_CURRENT_LAYOUT, current_layout);
        return;
    };

  public:
    wfs_keyboard_lang_manager(wl_client *client, uint32_t id)
    {
        resource = wl_resource_create(client, &zwf_keyboard_lang_manager_v2_interface, 1, id);
        wl_resource_set_implementation(resource, &zwf_keyboard_lang_manager_impl, this, handle_keyboard_lang_manager_destroy);

        wf::get_core().connect(&on_keyboard_modifiers);

        auto seat     = wf::get_core().get_current_seat();
        auto keyboard = wlr_seat_get_keyboard(seat);
        if (!keyboard)
        {
            return;
        }

        const auto& get_layout_name = [&] (xkb_layout_index_t layout)
        {
            auto layout_name = xkb_keymap_layout_get_name(keyboard->keymap, layout);
            return layout_name ? layout_name : "unknown";
        };

        wl_array available;
        wl_array_init(&available);

        if (keyboard)
        {
            auto layout = xkb_state_serialize_layout(keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
            current_layout = layout;

            auto n_layouts = xkb_keymap_num_layouts(keyboard->keymap);
            for (size_t i = 0; i < n_layouts; i++)
            {
                auto layout_name = get_layout_name(i);
                char *elem = (char *)wl_array_add(&available, strlen(layout_name) + 1);
                if (elem == NULL) {
                    wl_array_release(&available);
                    return;
                }
                strcpy(elem, layout_name);
            }
        }

        wl_resource_post_event(resource, ZWF_KEYBOARD_LANG_MANAGER_V2_AVAILABLE_LAYOUTS, &available);
        wl_resource_post_event(resource, ZWF_KEYBOARD_LANG_MANAGER_V2_CURRENT_LAYOUT, current_layout);
        wl_array_release(&available);
    }

    ~wfs_keyboard_lang_manager()
    {
        on_keyboard_modifiers.disconnect();
    }
};

static void handle_keyboard_lang_manager_destroy(wl_resource *resource)
{
    auto manager = (wfs_keyboard_lang_manager*)wl_resource_get_user_data(resource);
    delete manager;
    wl_resource_set_user_data(resource, nullptr);
}

static void zwf_shell_manager_get_wf_output(wl_client *client,
    wl_resource *resource, wl_resource *output, uint32_t id)
{
    auto wlr_out = (wlr_output*)wl_resource_get_user_data(output);
    auto wo = wf::get_core().output_layout->find_output(wlr_out);

    if (wo)
    {
        // will be deleted when the resource is destroyed
        new wfs_output(wo, resource, client, id);
    }
}

static void zwf_shell_manager_get_wf_surface(wl_client *client,
    wl_resource *resource, wl_resource *surface, uint32_t id)
{
    auto view = wf::wl_surface_to_wayfire_view(surface);
    if (view)
    {
        /* Will be freed when the resource is destroyed */
        new wfs_surface(view, client, id);
    }
}

static void zwf_shell_manager_get_wf_keyboard_lang_manager(wl_client *client, wl_resource *resource, uint32_t id) {
    new wfs_keyboard_lang_manager(client, id);
}

const struct zwf_shell_manager_v2_interface zwf_shell_manager_v2_impl =
{
    zwf_shell_manager_get_wf_output,
    zwf_shell_manager_get_wf_surface,
    zwf_shell_manager_get_wf_keyboard_lang_manager,
};

void bind_zwf_shell_manager(wl_client *client, void *data,
    uint32_t version, uint32_t id)
{
    auto resource =
        wl_resource_create(client, &zwf_shell_manager_v2_interface, version, id);
    wl_resource_set_implementation(resource,
        &zwf_shell_manager_v2_impl, NULL, NULL);
}

struct wayfire_shell
{
    wl_global *shell_manager;
};

wayfire_shell *wayfire_shell_create(wl_display *display)
{
    wayfire_shell *ws = new wayfire_shell;

    ws->shell_manager = wl_global_create(display,
        &zwf_shell_manager_v2_interface, 2, NULL, bind_zwf_shell_manager);

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

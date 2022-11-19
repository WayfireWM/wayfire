/* Needed for pipe2 */
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <float.h>

#include <wayfire/img.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "seat/keyboard.hpp"
#include "opengl-priv.hpp"
#include "seat/input-manager.hpp"
#include "seat/input-method-relay.hpp"
#include "seat/touch.hpp"
#include "seat/pointer.hpp"
#include "seat/cursor.hpp"
#include "../view/view-impl.hpp"
#include "../output/wayfire-shell.hpp"
#include "../output/output-impl.hpp"
#include "../output/gtk-shell.hpp"

#include "core-impl.hpp"

/* decorations impl */
struct wf_server_decoration_t
{
    wlr_server_decoration *decor;
    wf::wl_listener_wrapper on_mode_set, on_destroy;

    std::function<void(void*)> mode_set = [&] (void*)
    {
        bool use_csd = decor->mode == WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
        wf::get_core_impl().uses_csd[decor->surface] = use_csd;

        auto wf_surface = dynamic_cast<wf::wlr_view_t*>(
            wf::wf_surface_from_void(decor->surface->data));

        if (wf_surface)
        {
            wf_surface->has_client_decoration = use_csd;
        }
    };

    wf_server_decoration_t(wlr_server_decoration *_decor) :
        decor(_decor)
    {
        on_mode_set.set_callback(mode_set);
        on_destroy.set_callback([&] (void*)
        {
            wf::get_core_impl().uses_csd.erase(decor->surface);
            delete this;
        });

        on_mode_set.connect(&decor->events.mode);
        on_destroy.connect(&decor->events.destroy);
        /* Read initial decoration settings */
        mode_set(NULL);
    }
};

struct wf_xdg_decoration_t
{
    wlr_xdg_toplevel_decoration_v1 *decor;
    wf::wl_listener_wrapper on_mode_request, on_commit, on_destroy;

    std::function<void(void*)> mode_request = [&] (void*)
    {
        wf::option_wrapper_t<std::string>
        deco_mode{"core/preferred_decoration_mode"};
        wlr_xdg_toplevel_decoration_v1_mode default_mode =
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        if ((std::string)deco_mode == "server")
        {
            default_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        }

        auto mode = decor->pending.mode;
        if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE)
        {
            mode = default_mode;
        }

        wlr_xdg_toplevel_decoration_v1_set_mode(decor, mode);
    };

    std::function<void(void*)> commit = [&] (void*)
    {
        bool use_csd =
            decor->current.mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        wf::get_core_impl().uses_csd[decor->surface->surface] = use_csd;

        auto wf_surface = dynamic_cast<wf::wlr_view_t*>(
            wf::wf_surface_from_void(decor->surface->data));
        if (wf_surface)
        {
            wf_surface->set_decoration_mode(use_csd);
        }
    };

    wf_xdg_decoration_t(wlr_xdg_toplevel_decoration_v1 *_decor) :
        decor(_decor)
    {
        on_mode_request.set_callback(mode_request);
        on_commit.set_callback(commit);
        on_destroy.set_callback([&] (void*)
        {
            wf::get_core_impl().uses_csd.erase(decor->surface->surface);
            delete this;
        });

        on_mode_request.connect(&decor->events.request_mode);
        on_commit.connect(&decor->surface->surface->events.commit);
        on_destroy.connect(&decor->events.destroy);
        /* Read initial decoration settings */
        mode_request(NULL);
    }
};

struct wf_pointer_constraint
{
    wf::wl_listener_wrapper on_destroy;

    wf_pointer_constraint(wlr_pointer_constraint_v1 *constraint)
    {
        on_destroy.set_callback([=] (void*)
        {
            // reset constraint
            auto& lpointer = wf::get_core_impl().seat->lpointer;
            if (lpointer->get_active_pointer_constraint() == constraint)
            {
                lpointer->set_pointer_constraint(nullptr, true);
            }

            on_destroy.disconnect();
            delete this;
        });

        on_destroy.connect(&constraint->events.destroy);

        // set correct constraint
        auto& lpointer = wf::get_core_impl().seat->lpointer;
        auto focus     = lpointer->get_focus();
        if (focus && (focus->priv->wsurface == constraint->surface))
        {
            lpointer->set_pointer_constraint(constraint);
        }
    }
};

struct wlr_idle_inhibitor_t : public wf::idle_inhibitor_t
{
    wf::wl_listener_wrapper on_destroy;
    wlr_idle_inhibitor_t(wlr_idle_inhibitor_v1 *wlri)
    {
        on_destroy.set_callback([&] (void*)
        {
            delete this;
        });

        on_destroy.connect(&wlri->events.destroy);
    }
};

void wf::compositor_core_impl_t::init()
{
    wlr_renderer_init_wl_display(renderer, display);

    /* Order here is important:
     * 1. init_desktop_apis() must come after wlr_compositor_create(),
     *    since Xwayland initialization depends on the compositor
     * 2. input depends on output-layout
     * 3. weston toy clients expect xdg-shell before wl_seat, i.e
     * init_desktop_apis() should come before input.
     * 4. GTK expects primary selection early. */
    compositor = wlr_compositor_create(display, renderer);
    /* Needed for subsurfaces */
    wlr_subcompositor_create(display);

    protocols.data_device = wlr_data_device_manager_create(display);
    protocols.primary_selection_v1 =
        wlr_primary_selection_v1_device_manager_create(display);
    protocols.data_control = wlr_data_control_manager_v1_create(display);

    output_layout = std::make_unique<wf::output_layout_t>(backend);
    init_desktop_apis();

    /* Somehow GTK requires the tablet_v2 to be advertised pretty early */
    protocols.tablet_v2 = wlr_tablet_v2_create(display);
    input = std::make_unique<wf::input_manager_t>();
    seat  = std::make_unique<wf::seat_t>();

    protocols.screencopy = wlr_screencopy_manager_v1_create(display);
    protocols.gamma_v1   = wlr_gamma_control_manager_v1_create(display);
    protocols.export_dmabuf  = wlr_export_dmabuf_manager_v1_create(display);
    protocols.output_manager = wlr_xdg_output_manager_v1_create(display,
        output_layout->get_handle());

    /* input-inhibit setup */
    protocols.input_inhibit = wlr_input_inhibit_manager_create(display);
    input_inhibit_activated.set_callback([&] (void*)
    {
        input->set_exclusive_focus(protocols.input_inhibit->active_client);
    });
    input_inhibit_activated.connect(&protocols.input_inhibit->events.activate);

    input_inhibit_deactivated.set_callback([&] (void*)
    {
        input->set_exclusive_focus(nullptr);
    });
    input_inhibit_deactivated.connect(&protocols.input_inhibit->events.deactivate);

    /* decoration_manager setup */
    protocols.decorator_manager = wlr_server_decoration_manager_create(display);
    wf::option_wrapper_t<std::string>
    deco_mode{"core/preferred_decoration_mode"};
    uint32_t default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
    if ((std::string)deco_mode == "server")
    {
        default_mode = WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
    }

    wlr_server_decoration_manager_set_default_mode(protocols.decorator_manager,
        default_mode);

    decoration_created.set_callback([&] (void *data)
    {
        /* will be freed by the destroy request */
        new wf_server_decoration_t((wlr_server_decoration*)(data));
    });
    decoration_created.connect(&protocols.decorator_manager->events.new_decoration);

    protocols.xdg_decorator = wlr_xdg_decoration_manager_v1_create(display);

    xdg_decoration_created.set_callback([&] (void *data)
    {
        /* will be freed by the destroy request */
        new wf_xdg_decoration_t((wlr_xdg_toplevel_decoration_v1*)(data));
    });
    xdg_decoration_created.connect(
        &protocols.xdg_decorator->events.new_toplevel_decoration);

    protocols.vkbd_manager = wlr_virtual_keyboard_manager_v1_create(display);
    vkbd_created.set_callback([&] (void *data)
    {
        auto kbd = (wlr_virtual_keyboard_v1*)data;
        input->handle_new_input(&kbd->keyboard.base);
    });
    vkbd_created.connect(&protocols.vkbd_manager->events.new_virtual_keyboard);

    protocols.vptr_manager = wlr_virtual_pointer_manager_v1_create(display);
    vptr_created.set_callback([&] (void *data)
    {
        auto event = (wlr_virtual_pointer_v1_new_pointer_event*)data;
        auto ptr   = event->new_pointer;
        input->handle_new_input(&ptr->pointer.base);
    });
    vptr_created.connect(&protocols.vptr_manager->events.new_virtual_pointer);

    protocols.idle_inhibit = wlr_idle_inhibit_v1_create(display);
    idle_inhibitor_created.set_callback([&] (void *data)
    {
        auto wlri = static_cast<wlr_idle_inhibitor_v1*>(data);
        /* will be freed by the destroy request */
        new wlr_idle_inhibitor_t(wlri);
    });
    idle_inhibitor_created.connect(
        &protocols.idle_inhibit->events.new_inhibitor);

    protocols.idle = wlr_idle_create(display);
    protocols.toplevel_manager = wlr_foreign_toplevel_manager_v1_create(display);
    protocols.pointer_gestures = wlr_pointer_gestures_v1_create(display);
    protocols.relative_pointer = wlr_relative_pointer_manager_v1_create(display);

    protocols.pointer_constraints = wlr_pointer_constraints_v1_create(display);
    pointer_constraint_added.set_callback([&] (void *data)
    {
        // will delete itself when the constraint is destroyed
        new wf_pointer_constraint((wlr_pointer_constraint_v1*)data);
    });
    pointer_constraint_added.connect(
        &protocols.pointer_constraints->events.new_constraint);

    protocols.input_method = wlr_input_method_manager_v2_create(display);
    protocols.text_input   = wlr_text_input_manager_v3_create(display);
    im_relay = std::make_unique<input_method_relay>();

    protocols.presentation = wlr_presentation_create(display, backend);
    protocols.viewporter   = wlr_viewporter_create(display);

    protocols.foreign_registry = wlr_xdg_foreign_registry_create(display);
    protocols.foreign_v1 = wlr_xdg_foreign_v1_create(display,
        protocols.foreign_registry);
    protocols.foreign_v2 = wlr_xdg_foreign_v2_create(display,
        protocols.foreign_registry);

    wf_shell  = wayfire_shell_create(display);
    gtk_shell = wf_gtk_shell_create(display);

    image_io::init();
    OpenGL::init();

    init_last_view_tracking();
    this->state = compositor_state_t::START_BACKEND;
}

void wf::compositor_core_impl_t::init_last_view_tracking()
{
    on_new_output.set_callback([&] (wf::signal_data_t *data)
    {
        auto wo = get_signaled_output(data);
        wo->connect_signal("view-unmapped", &on_view_unmap);
    });
    output_layout->connect_signal("output-added", &on_new_output);

    on_view_unmap.set_callback([&] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        if (view == last_active_toplevel)
        {
            last_active_toplevel = nullptr;
        }

        if (view == last_active_view)
        {
            last_active_view = nullptr;
        }
    });
}

void wf::compositor_core_impl_t::post_init()
{
    this->emit_signal("_backend_started", nullptr);
    this->state = compositor_state_t::RUNNING;

    // Move pointer to the middle of the leftmost, topmost output
    wf::pointf_t p;
    wf::output_t *wo =
        wf::get_core().output_layout->get_output_coords_at({FLT_MIN, FLT_MIN}, p);
    // Output might be noop but guaranteed to not be null
    wo->ensure_pointer(true);
    focus_output(wo);

    // Refresh device mappings when we have all outputs and devices
    input->refresh_device_mappings();

    // Start processing cursor events
    seat->cursor->setup_listeners();

    this->emit_signal("startup-finished", nullptr);
}

void wf::compositor_core_impl_t::shutdown()
{
    this->state = compositor_state_t::SHUTDOWN;
    wf::get_core().emit_signal("shutdown", nullptr);
    wl_display_terminate(wf::get_core().display);
}

wf::compositor_state_t wf::compositor_core_impl_t::get_current_state()
{
    return this->state;
}

wlr_seat*wf::compositor_core_impl_t::get_current_seat()
{
    return seat->seat;
}

uint32_t wf::compositor_core_impl_t::get_keyboard_modifiers()
{
    return seat->get_modifiers();
}

void wf::compositor_core_impl_t::set_cursor(std::string name)
{
    seat->cursor->set_cursor(name);
}

void wf::compositor_core_impl_t::unhide_cursor()
{
    seat->cursor->unhide_cursor();
}

void wf::compositor_core_impl_t::hide_cursor()
{
    seat->cursor->hide_cursor();
}

void wf::compositor_core_impl_t::warp_cursor(wf::pointf_t pos)
{
    seat->cursor->warp_cursor(pos);
}

wf::pointf_t wf::compositor_core_impl_t::get_cursor_position()
{
    if (seat->cursor)
    {
        return seat->cursor->get_cursor_position();
    } else
    {
        return {invalid_coordinate, invalid_coordinate};
    }
}

wf::pointf_t wf::compositor_core_impl_t::get_touch_position(int id)
{
    const auto& state = seat->touch->get_state();
    auto it = state.fingers.find(id);
    if (it != state.fingers.end())
    {
        return {it->second.current.x, it->second.current.y};
    }

    return {invalid_coordinate, invalid_coordinate};
}

const wf::touch::gesture_state_t& wf::compositor_core_impl_t::get_touch_state()
{
    return seat->touch->get_state();
}

wf::surface_interface_t*wf::compositor_core_impl_t::get_cursor_focus()
{
    return seat->lpointer->get_focus();
}

wayfire_view wf::compositor_core_t::get_cursor_focus_view()
{
    auto focus = get_cursor_focus();
    auto view  = dynamic_cast<wf::view_interface_t*>(
        focus ? focus->get_main_surface() : nullptr);

    return view ? view->self() : nullptr;
}

wf::surface_interface_t*wf::compositor_core_impl_t::get_surface_at(
    wf::pointf_t point)
{
    wf::pointf_t local = {0.0, 0.0};

    return input->input_surface_at(point, local);
}

wayfire_view wf::compositor_core_t::get_view_at(wf::pointf_t point)
{
    auto surface = get_surface_at(point);
    if (!surface)
    {
        return nullptr;
    }

    auto view = dynamic_cast<wf::view_interface_t*>(surface->get_main_surface());

    return view ? view->self() : nullptr;
}

wf::surface_interface_t*wf::compositor_core_impl_t::get_touch_focus()
{
    return seat->touch->get_focus();
}

wayfire_view wf::compositor_core_t::get_touch_focus_view()
{
    auto focus = get_touch_focus();
    auto view  = dynamic_cast<wf::view_interface_t*>(
        focus ? focus->get_main_surface() : nullptr);

    return view ? view->self() : nullptr;
}

void wf::compositor_core_impl_t::add_touch_gesture(
    nonstd::observer_ptr<wf::touch::gesture_t> gesture)
{
    seat->touch->add_touch_gesture(gesture);
}

void wf::compositor_core_impl_t::rem_touch_gesture(
    nonstd::observer_ptr<wf::touch::gesture_t> gesture)
{
    seat->touch->rem_touch_gesture(gesture);
}

std::vector<nonstd::observer_ptr<wf::input_device_t>> wf::compositor_core_impl_t::
get_input_devices()
{
    std::vector<nonstd::observer_ptr<wf::input_device_t>> list;
    for (auto& dev : input->input_devices)
    {
        list.push_back(nonstd::make_observer(dev.get()));
    }

    return list;
}

wlr_cursor*wf::compositor_core_impl_t::get_wlr_cursor()
{
    return seat->cursor->cursor;
}

void wf::compositor_core_impl_t::focus_output(wf::output_t *wo)
{
    if (active_output == wo)
    {
        return;
    }

    if (wo)
    {
        LOGD("focus output: ", wo->handle->name);
        /* Move to the middle of the output if this is the first output */
        wo->ensure_pointer((active_output == nullptr));
    }

    wf::plugin_grab_interface_t *old_grab = nullptr;
    if (active_output)
    {
        auto output_impl = dynamic_cast<wf::output_impl_t*>(active_output);
        old_grab = output_impl->get_input_grab_interface();
        active_output->focus_view(nullptr);
    }

    active_output = wo;

    /* invariant: input is grabbed only if the current output
     * has an input grab */
    if (input->input_grabbed())
    {
        assert(old_grab);
        input->ungrab_input();
    }

    /* On shutdown */
    if (!active_output)
    {
        return;
    }

    auto output_impl = dynamic_cast<wf::output_impl_t*>(wo);
    wf::plugin_grab_interface_t *iface = output_impl->get_input_grab_interface();
    if (!iface)
    {
        wo->refocus();
    } else
    {
        input->grab_input(iface);
    }

    wlr_output_schedule_frame(active_output->handle);

    wf::output_gain_focus_signal data;
    data.output = active_output;
    active_output->emit_signal("gain-focus", &data);
    this->emit_signal("output-gain-focus", &data);
}

wf::output_t*wf::compositor_core_impl_t::get_active_output()
{
    return active_output;
}

int wf::compositor_core_impl_t::focus_layer(uint32_t layer, int32_t request_uid_hint)
{
    static int32_t last_request_uid = -1;
    if (request_uid_hint >= 0)
    {
        /* Remove the old request, and insert the new one */
        uint32_t old_layer = -1;
        for (auto& req : layer_focus_requests)
        {
            if (req.second == request_uid_hint)
            {
                old_layer = req.first;
            }
        }

        /* Request UID isn't valid */
        if (old_layer == (uint32_t)-1)
        {
            return -1;
        }

        layer_focus_requests.erase({old_layer, request_uid_hint});
    }

    auto request_uid = request_uid_hint < 0 ?
        ++last_request_uid : request_uid_hint;
    layer_focus_requests.insert({layer, request_uid});
    LOGD("focusing layer ", get_focused_layer());

    if (active_output)
    {
        active_output->refocus();
    }

    return request_uid;
}

uint32_t wf::compositor_core_impl_t::get_focused_layer()
{
    if (layer_focus_requests.empty())
    {
        return 0;
    }

    return (--layer_focus_requests.end())->first;
}

void wf::compositor_core_impl_t::unfocus_layer(int request)
{
    for (auto& freq : layer_focus_requests)
    {
        if (freq.second == request)
        {
            layer_focus_requests.erase(freq);
            LOGD("focusing layer ", get_focused_layer());

            active_output->refocus(nullptr);

            return;
        }
    }
}

void wf::compositor_core_impl_t::add_view(
    std::unique_ptr<wf::view_interface_t> view)
{
    auto v = view->self(); /* non-owning copy */
    views.push_back(std::move(view));

    assert(active_output);
    if (!v->get_output())
    {
        v->set_output(active_output);
    }

    v->initialize();
}

std::vector<wayfire_view> wf::compositor_core_impl_t::get_all_views()
{
    std::vector<wayfire_view> result;
    for (auto& view : this->views)
    {
        result.push_back({view});
    }

    return result;
}

/* sets the "active" view and gives it keyboard focus
 *
 * It maintains two different classes of "active views"
 * 1. active_view -> the view which has the current keyboard focus
 * 2. last_active_toplevel -> the toplevel view which last held the keyboard focus
 *
 * Because we don't want to deactivate views when for ex. a panel gets focus,
 * we don't deactivate the current view when this is the case. However, when
 * the focus goes back to the toplevel layer, we need to ensure the proper view
 * is activated.
 */
void wf::compositor_core_impl_t::set_active_view(wayfire_view new_focus)
{
    static wf::option_wrapper_t<bool>
    all_dialogs_modal{"workarounds/all_dialogs_modal"};

    if (new_focus && !new_focus->is_mapped())
    {
        new_focus = nullptr;
    }

    if (all_dialogs_modal && new_focus)
    {
        // Choose the frontmost view which has focus enabled.
        auto all_views = new_focus->enumerate_views();
        for (auto& view : all_views)
        {
            if (view->get_keyboard_focus_surface())
            {
                new_focus = view;
                break;
            }
        }
    }

    bool refocus = (last_active_view == new_focus);
    /* don't deactivate view if the next focus is not a toplevel */
    if ((new_focus == nullptr) || (new_focus->role == VIEW_ROLE_TOPLEVEL))
    {
        if (last_active_view && last_active_view->is_mapped() && !refocus)
        {
            last_active_view->set_activated(false);
        }

        /* make sure to deactivate the last activated toplevel */
        if (last_active_toplevel && (new_focus != last_active_toplevel))
        {
            last_active_toplevel->set_activated(false);
        }
    }

    if (new_focus)
    {
        seat->set_keyboard_focus(new_focus);
        new_focus->set_activated(true);
    } else
    {
        seat->set_keyboard_focus(nullptr);
    }

    last_active_view = new_focus;
    if (!new_focus || (new_focus->role == VIEW_ROLE_TOPLEVEL))
    {
        last_active_toplevel = new_focus;
    }
}

void wf::compositor_core_impl_t::focus_view(wayfire_view v)
{
    if (!v)
    {
        return;
    }

    if (v->get_output() != active_output)
    {
        focus_output(v->get_output());
    }

    active_output->focus_view(v, true);
}

void wf::compositor_core_impl_t::erase_view(wayfire_view v)
{
    if (!v)
    {
        return;
    }

    if (v->get_output())
    {
        v->set_output(nullptr);
    }

    auto it = std::find_if(views.begin(), views.end(),
        [&v] (const auto& view) { return view.get() == v.get(); });

    v->deinitialize();
    views.erase(it);
}

pid_t wf::compositor_core_impl_t::run(std::string command)
{
    static constexpr size_t READ_END  = 0;
    static constexpr size_t WRITE_END = 1;
    pid_t pid;
    int pipe_fd[2];
    pipe2(pipe_fd, O_CLOEXEC);

    /* The following is a "hack" for disowning the child processes,
     * otherwise they will simply stay as zombie processes */
    pid = fork();
    if (!pid)
    {
        pid = fork();
        if (!pid)
        {
            close(pipe_fd[READ_END]);
            close(pipe_fd[WRITE_END]);

            setenv("_JAVA_AWT_WM_NONREPARENTING", "1", 1);
            setenv("WAYLAND_DISPLAY", wayland_display.c_str(), 1);
#if WF_HAS_XWAYLAND
            if (!xwayland_get_display().empty())
            {
                setenv("DISPLAY", xwayland_get_display().c_str(), 1);
            }

#endif
            int dev_null = open("/dev/null", O_WRONLY);
            dup2(dev_null, 1);
            dup2(dev_null, 2);
            close(dev_null);

            _exit(execl("/bin/sh", "/bin/sh", "-c", command.c_str(), NULL));
        } else
        {
            close(pipe_fd[READ_END]);
            write(pipe_fd[WRITE_END], (void*)(&pid), sizeof(pid));
            close(pipe_fd[WRITE_END]);
            _exit(0);
        }
    } else
    {
        close(pipe_fd[WRITE_END]);

        int status;
        waitpid(pid, &status, 0);

        pid_t child_pid;
        read(pipe_fd[READ_END], &child_pid, sizeof(child_pid));

        close(pipe_fd[READ_END]);

        return child_pid;
    }
}

std::string wf::compositor_core_impl_t::get_xwayland_display()
{
    return xwayland_get_display();
}

void wf::compositor_core_impl_t::move_view_to_output(wayfire_view v,
    wf::output_t *new_output, bool reconfigure)
{
    auto old_output = v->get_output();
    wf::view_pre_moved_to_output_signal data;
    data.view = v;
    data.old_output = old_output;
    data.new_output = new_output;
    this->emit_signal("view-pre-moved-to-output", &data);

    uint32_t edges;
    bool fullscreen;
    wf::geometry_t view_g;
    wf::geometry_t old_output_g;
    wf::geometry_t new_output_g;

    if (reconfigure)
    {
        edges = v->tiled_edges;
        fullscreen = v->fullscreen;
        view_g     = v->get_wm_geometry();
        old_output_g = old_output->get_relative_geometry();
        new_output_g = new_output->get_relative_geometry();
        auto ratio_x = (double)new_output_g.width / old_output_g.width;
        auto ratio_y = (double)new_output_g.height / old_output_g.height;
        view_g.x     *= ratio_x;
        view_g.y     *= ratio_y;
        view_g.width *= ratio_x;
        view_g.height *= ratio_y;
    }

    assert(new_output);
    v->set_output(new_output);
    new_output->workspace->add_view(v,
        v->minimized ? wf::LAYER_MINIMIZED : wf::LAYER_WORKSPACE);
    new_output->focus_view(v);

    if (reconfigure)
    {
        if (fullscreen)
        {
            v->fullscreen_request(new_output, true);
        } else if (edges)
        {
            v->tile_request(edges);
        } else
        {
            auto new_g = wf::clamp(view_g, new_output->workspace->get_workarea());
            v->set_geometry(new_g);
        }
    }

    this->emit_signal("view-moved-to-output", &data);
}

wf::compositor_core_t::compositor_core_t()
{}
wf::compositor_core_t::~compositor_core_t()
{}

wf::compositor_core_impl_t::compositor_core_impl_t()
{}
wf::compositor_core_impl_t::~compositor_core_impl_t()
{
    /* Unloading order is important. First we want to free any remaining views,
     * then we destroy the input manager, and finally the rest is auto-freed */
    views.clear();
    input.reset();
    output_layout.reset();
}

wf::compositor_core_impl_t& wf::compositor_core_impl_t::get()
{
    static compositor_core_impl_t instance;

    return instance;
}

wf::compositor_core_t& wf::compositor_core_t::get()
{
    return wf::compositor_core_impl_t::get();
}

wf::compositor_core_t& wf::get_core()
{
    return wf::compositor_core_t::get();
}

wf::compositor_core_impl_t& wf::get_core_impl()
{
    return wf::compositor_core_impl_t::get();
}

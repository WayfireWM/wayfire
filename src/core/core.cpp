/* Needed for pipe2 */
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
    #include "wayfire/core.hpp"
#endif

#include <wayfire/nonstd/tracking-allocator.hpp>
#include "wayfire/scene.hpp"
#include <wayfire/workarea.hpp>
#include "wayfire/scene-operations.hpp"
#include "wayfire/txn/transaction-manager.hpp"
#include "wayfire/bindings-repository.hpp"
#include "wayfire/util.hpp"
#include <memory>
#include "wayfire/config-backend.hpp" // IWYU pragma: keep

#include "plugin-loader.hpp"
#include "seat/tablet.hpp"
#include "wayfire/touch/touch.hpp"
#include "wayfire/view.hpp"
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <float.h>

#include <wayfire/img.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "wayfire/unstable/wlr-surface-controller.hpp"
#include "wayfire/scene-input.hpp"
#include "opengl-priv.hpp"
#include "seat/input-manager.hpp"
#include "seat/input-method-relay.hpp"
#include "seat/touch.hpp"
#include "seat/pointer.hpp"
#include "seat/cursor.hpp"
#include "../view/view-impl.hpp"
#include "main.hpp"
#include <wayfire/window-manager.hpp>

#include "core-impl.hpp"

struct wf_pointer_constraint
{
    wf::wl_listener_wrapper on_destroy;

    wf_pointer_constraint(wlr_pointer_constraint_v1 *constraint)
    {
        // set correct constraint
        auto& lpointer = wf::get_core_impl().seat->priv->lpointer;
        auto focus     = lpointer->get_focus();
        if (focus)
        {
            wf::node_recheck_constraints_signal data;
            focus->emit(&data);
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
    this->scene_root = std::make_shared<scene::root_node_t>();
    this->tx_manager = std::make_unique<txn::transaction_manager_t>();
    this->default_wm = std::make_unique<wf::window_manager_t>();

    wlr_renderer_init_wl_display(renderer, display);

    /* Order here is important:
     * 1. init_desktop_apis() must come after wlr_compositor_create(),
     *    since Xwayland initialization depends on the compositor
     * 2. input depends on output-layout
     * 3. weston toy clients expect xdg-shell before wl_seat, i.e
     * init_desktop_apis() should come before input.
     * 4. GTK expects primary selection early. */
    compositor = wlr_compositor_create(display, 6, renderer);
    /* Needed for subsurfaces */
    wlr_subcompositor_create(display);

    /* Legacy DRM */
    if (runtime_config.legacy_wl_drm &&
        wlr_renderer_get_texture_formats(renderer, WLR_BUFFER_CAP_DMABUF))
    {
        wlr_drm_create(display, renderer);
    }

    protocols.data_device = wlr_data_device_manager_create(display);
    protocols.primary_selection_v1 =
        wlr_primary_selection_v1_device_manager_create(display);
    protocols.data_control = wlr_data_control_manager_v1_create(display);

    output_layout = std::make_unique<wf::output_layout_t>(backend);
    init_desktop_apis();

    /* Somehow GTK requires the tablet_v2 to be advertised pretty early */
    protocols.tablet_v2 = wlr_tablet_v2_create(display);
    input = std::make_unique<wf::input_manager_t>();
    seat  = std::make_unique<wf::seat_t>(display, "default");

    protocols.screencopy = wlr_screencopy_manager_v1_create(display);
    protocols.gamma_v1   = wlr_gamma_control_manager_v1_create(display);
    protocols.export_dmabuf  = wlr_export_dmabuf_manager_v1_create(display);
    protocols.output_manager = wlr_xdg_output_manager_v1_create(display,
        output_layout->get_handle());
    protocols.drm_v1 = wlr_drm_lease_v1_manager_create(display, backend);
    drm_lease_request.set_callback([&] (void *data)
    {
        auto req = static_cast<wlr_drm_lease_request_v1*>(data);
        struct wlr_drm_lease_v1 *lease = wlr_drm_lease_request_v1_grant(req);
        if (!lease)
        {
            wlr_drm_lease_request_v1_reject(req);
        }
    });
    if (protocols.drm_v1)
    {
        drm_lease_request.connect(&protocols.drm_v1->events.request);
    } else
    {
        LOGE("Failed to create wlr_drm_lease_device_v1; VR will not be available!");
    }

    /* idle-inhibit setup */
    protocols.idle_notifier = wlr_idle_notifier_v1_create(display);
    protocols.idle_inhibit  = wlr_idle_inhibit_v1_create(display);

    idle_inhibitor_created.set_callback([&] (void *data)
    {
        auto wlri = static_cast<wlr_idle_inhibitor_v1*>(data);
        /* will be freed by the destroy request */
        new wlr_idle_inhibitor_t(wlri);
    });
    idle_inhibitor_created.connect(&protocols.idle_inhibit->events.new_inhibitor);

    /* decoration_manager setup */
    protocols.decorator_manager = wlr_server_decoration_manager_create(display);
    protocols.xdg_decorator     = wlr_xdg_decoration_manager_v1_create(display);
    init_xdg_decoration_handlers();

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

    wf::option_wrapper_t<bool> enable_input_method_v2{"workarounds/enable_input_method_v2"};
    if (enable_input_method_v2)
    {
        protocols.input_method = wlr_input_method_manager_v2_create(display);
        protocols.text_input   = wlr_text_input_manager_v3_create(display);
    }

    im_relay = std::make_unique<input_method_relay>();

    protocols.presentation = wlr_presentation_create(display, backend);
    protocols.viewporter   = wlr_viewporter_create(display);

    protocols.foreign_registry = wlr_xdg_foreign_registry_create(display);
    protocols.foreign_v1 = wlr_xdg_foreign_v1_create(display,
        protocols.foreign_registry);
    protocols.foreign_v2 = wlr_xdg_foreign_v2_create(display,
        protocols.foreign_registry);

    wlr_fractional_scale_manager_v1_create(display, 1);
    wlr_single_pixel_buffer_manager_v1_create(display);

    this->bindings = std::make_unique<bindings_repository_t>();
    image_io::init();
    OpenGL::init();
    this->state = compositor_state_t::START_BACKEND;
}

void wf::compositor_core_impl_t::post_init()
{
    discard_command_output.load_option("workarounds/discard_command_output");

    core_backend_started_signal backend_started_ev;
    this->emit(&backend_started_ev);
    this->state = compositor_state_t::RUNNING;
    plugin_mgr  = std::make_unique<wf::plugin_manager_t>();
    this->bindings->reparse_extensions();

    // Move pointer to the middle of the leftmost, topmost output
    wf::pointf_t p;
    wf::output_t *wo = wf::get_core().output_layout->get_output_coords_at({FLT_MIN, FLT_MIN}, p);
    // Output might be noop but guaranteed to not be null
    wo->ensure_pointer(true);
    seat->focus_output(wo);

    // Refresh device mappings when we have all outputs and devices
    input->configure_input_devices();

    // Start processing cursor events
    seat->priv->cursor->setup_listeners();
    core_startup_finished_signal startup_ev;
    this->emit(&startup_ev);
}

void wf::compositor_core_impl_t::shutdown()
{
    // We might get multiple signals in some scenarios. Shutdown only on the first instance.
    if (this->state != compositor_state_t::SHUTDOWN)
    {
        wl_display_terminate(wf::get_core().display);
    }
}

void wf::compositor_core_impl_t::disconnect_signals()
{
    fini_desktop_apis();
    fini_xdg_decoration_handlers();
    drm_lease_request.disconnect();
    input_inhibit_activated.disconnect();
    input_inhibit_deactivated.disconnect();
    idle_inhibitor_created.disconnect();
    vkbd_created.disconnect();
    vptr_created.disconnect();
    pointer_constraint_added.disconnect();
}

void wf::compositor_core_impl_t::fini()
{
    this->state = compositor_state_t::SHUTDOWN;
    core_shutdown_signal ev;
    this->emit(&ev);

    LOGI("Unloading plugins...");
    plugin_mgr.reset();
    // Shut down xwayland first, otherwise, wlroots will attempt to restart it when we kill it via
    // wl_display_destroy_clients().
    wf::fini_xwayland();
    LOGI("Stopping clients...");
    wl_display_destroy_clients(static_core->display);
    LOGI("Freeing resources...");
    default_wm.reset();
    bindings.reset();
    scene_root.reset();

    // General core stuff
    im_relay.reset();
    seat.reset();
    input.reset();
    priv_output_layout_fini(output_layout.get());
    output_layout.reset();
    tx_manager.reset();
    OpenGL::fini();
    disconnect_signals();
    wl_display_destroy(static_core->display);
}

wf::compositor_state_t wf::compositor_core_impl_t::get_current_state()
{
    return this->state;
}

wlr_seat*wf::compositor_core_impl_t::get_current_seat()
{
    return seat->seat;
}

void wf::compositor_core_impl_t::set_cursor(std::string name)
{
    seat->priv->cursor->set_cursor(name);
}

void wf::compositor_core_impl_t::unhide_cursor()
{
    seat->priv->cursor->unhide_cursor();
}

void wf::compositor_core_impl_t::hide_cursor()
{
    seat->priv->cursor->hide_cursor();
}

void wf::compositor_core_impl_t::warp_cursor(wf::pointf_t pos)
{
    seat->priv->cursor->warp_cursor(pos);
    seat->priv->lpointer->update_cursor_position(get_current_time());
}

void wf::compositor_core_impl_t::transfer_grab(wf::scene::node_ptr node)
{
    seat->priv->transfer_grab(node);
    seat->priv->lpointer->transfer_grab(node);
    seat->priv->touch->transfer_grab(node);

    for (auto dev : this->get_input_devices())
    {
        if (auto tablet = dynamic_cast<wf::tablet_t*>(dev.get()))
        {
            for (auto& tool : tablet->tools_list)
            {
                tool->reset_grab();
            }
        }
    }
}

wf::pointf_t wf::compositor_core_impl_t::get_cursor_position()
{
    if (seat->priv->cursor)
    {
        return seat->priv->cursor->get_cursor_position();
    } else
    {
        return {invalid_coordinate, invalid_coordinate};
    }
}

wf::pointf_t wf::compositor_core_impl_t::get_touch_position(int id)
{
    const auto& state = seat->priv->touch->get_state();
    auto it = state.fingers.find(id);
    if (it != state.fingers.end())
    {
        return {it->second.current.x, it->second.current.y};
    }

    return {invalid_coordinate, invalid_coordinate};
}

const wf::touch::gesture_state_t& wf::compositor_core_impl_t::get_touch_state()
{
    return seat->priv->touch->get_state();
}

wf::scene::node_ptr wf::compositor_core_impl_t::get_cursor_focus()
{
    return seat->priv->lpointer->get_focus();
}

wayfire_view wf::compositor_core_t::get_cursor_focus_view()
{
    return node_to_view(get_cursor_focus());
}

wayfire_view wf::compositor_core_t::get_view_at(wf::pointf_t point)
{
    auto isec = scene()->find_node_at(point);
    return isec ? node_to_view(isec->node->shared_from_this()) : nullptr;
}

wf::scene::node_ptr wf::compositor_core_impl_t::get_touch_focus(int finger_id)
{
    return seat->priv->touch->get_focus(finger_id);
}

wayfire_view wf::compositor_core_t::get_touch_focus_view()
{
    return node_to_view(get_touch_focus());
}

void wf::compositor_core_impl_t::add_touch_gesture(
    nonstd::observer_ptr<wf::touch::gesture_t> gesture)
{
    seat->priv->touch->add_touch_gesture(gesture);
}

void wf::compositor_core_impl_t::rem_touch_gesture(
    nonstd::observer_ptr<wf::touch::gesture_t> gesture)
{
    seat->priv->touch->rem_touch_gesture(gesture);
}

std::vector<nonstd::observer_ptr<wf::input_device_t>> wf::compositor_core_impl_t::get_input_devices()
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
    return seat->priv->cursor->cursor;
}

std::vector<wayfire_view> wf::compositor_core_t::get_all_views()
{
    return wf::tracking_allocator_t<view_interface_t>::get().get_all();
}

/**
 * Upon successful execution, returns the PID of the child process.
 * Returns 0 in case of failure.
 */
pid_t wf::compositor_core_impl_t::run(std::string command)
{
    static constexpr size_t READ_END  = 0;
    static constexpr size_t WRITE_END = 1;

    int pipe_fd[2];
    int ret = pipe2(pipe_fd, O_CLOEXEC);
    if (ret == -1)
    {
        LOGE("wf::compositor_core_impl_t::run: failed to create pipe2: ", strerror(errno));
        return 0;
    }

    /* The following is a "hack" for disowning the child processes,
     * otherwise they will simply stay as zombie processes */
    pid_t pid = fork();
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
            if (discard_command_output)
            {
                int dev_null = open("/dev/null", O_WRONLY);
                dup2(dev_null, 1);
                dup2(dev_null, 2);
                close(dev_null);
            }

            _exit(execl("/bin/sh", "/bin/sh", "-c", command.c_str(), NULL));
        } else
        {
            close(pipe_fd[READ_END]);
            int ret = write(pipe_fd[WRITE_END], (void*)(&pid), sizeof(pid));
            close(pipe_fd[WRITE_END]);
            _exit(ret != sizeof(pid) ? 1 : 0);
        }
    } else
    {
        close(pipe_fd[WRITE_END]);

        int status;
        waitpid(pid, &status, 0);

        // Return 0 if the child process didn't run or didn't exit normally, or returns a non-zero return
        // value.
        pid_t child_pid{};
        if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
        {
            int ret = read(pipe_fd[READ_END], &child_pid, sizeof(child_pid));
            if (ret != sizeof(child_pid))
            {
                // This is consider to be an error (even though theoretically a partial read would require an
                // attempt to continue).
                child_pid = 0;
                if (ret == -1)
                {
                    LOGE("wf::compositor_core_impl_t::run(\"", command,
                        "\"): failed to read PID from pipe end: ", strerror(errno));
                } else
                {
                    LOGE("wf::compositor_core_impl_t::run(\"", command,
                        "\"): short read of PID from pipe end, got ", std::to_string(ret), " bytes");
                }
            }
        }

        close(pipe_fd[READ_END]);

        return child_pid;
    }
}

std::string wf::compositor_core_impl_t::get_xwayland_display()
{
    return xwayland_get_display();
}

void wf::start_move_view_to_wset(wayfire_toplevel_view v, std::shared_ptr<wf::workspace_set_t> new_wset)
{
    emit_view_pre_moved_to_wset_pre(v, v->get_wset(), new_wset);
    if (v->get_wset())
    {
        v->get_wset()->remove_view(v);
        wf::scene::remove_child(v->get_root_node());
    }

    wf::scene::add_front(new_wset->get_node(), v->get_root_node());
    new_wset->add_view(v);
}

void wf::move_view_to_output(wayfire_toplevel_view v, wf::output_t *new_output, bool reconfigure)
{
    wf::dassert(!v->parent, "Cannot move a dialog to a different output than its parent!");

    auto old_output = v->get_output();
    auto old_wset   = v->get_wset();

    uint32_t edges;
    bool fullscreen;
    wf::geometry_t view_g;
    wf::geometry_t old_output_g;
    wf::geometry_t new_output_g;

    int delta_x = 0;
    int delta_y = 0;

    if (reconfigure)
    {
        edges = v->pending_tiled_edges();
        fullscreen = v->pending_fullscreen();
        view_g     = v->get_pending_geometry();
        old_output_g = old_output->get_relative_geometry();
        new_output_g = new_output->get_relative_geometry();
        auto ratio_x = (double)new_output_g.width / old_output_g.width;
        auto ratio_y = (double)new_output_g.height / old_output_g.height;
        view_g.x *= ratio_x;
        view_g.y *= ratio_y;

        delta_x = view_g.x - v->get_pending_geometry().x;
        delta_y = view_g.y - v->get_pending_geometry().y;
    }

    assert(new_output);

    start_move_view_to_wset(v, new_output->wset());
    if (new_output == wf::get_core().seat->get_active_output())
    {
        wf::get_core().seat->focus_view(v);
    }

    if (reconfigure)
    {
        if (fullscreen)
        {
            wf::get_core().default_wm->fullscreen_request(v, new_output, true);
        } else if (edges)
        {
            wf::get_core().default_wm->tile_request(v, edges);
        } else
        {
            auto new_g = wf::clamp(view_g, new_output->workarea->get_workarea());
            v->set_geometry(new_g);
        }

        for (auto& dialog : v->enumerate_views())
        {
            if ((dialog != v) && (delta_x || delta_y))
            {
                dialog->move(dialog->get_pending_geometry().x + delta_x,
                    dialog->get_pending_geometry().y + delta_y);
            }
        }
    }

    emit_view_moved_to_wset(v, old_wset, new_output->wset());
}

const std::shared_ptr<wf::scene::root_node_t>& wf::compositor_core_impl_t::scene()
{
    return scene_root;
}

wf::compositor_core_t::compositor_core_t()
{
    this->config = std::make_unique<wf::config::config_manager_t>();
}

wf::compositor_core_t::~compositor_core_t()
{}

wf::compositor_core_impl_t::compositor_core_impl_t()
{}
wf::compositor_core_impl_t::~compositor_core_impl_t()
{
    input.reset();
    output_layout.reset();
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

wf::compositor_core_impl_t& wf::compositor_core_impl_t::allocate_core()
{
    wf::dassert(!static_core, "Core already allocated");
    static_core = std::make_unique<compositor_core_impl_t>();
    return *static_core;
}

void wf::compositor_core_impl_t::deallocate_core()
{
    static_core->fini();
    static_core.reset();
}

wf::compositor_core_impl_t& wf::compositor_core_impl_t::get()
{
    return *static_core;
}

std::unique_ptr<wf::compositor_core_impl_t> wf::compositor_core_impl_t::static_core;

// TODO: move this to a better location
wf_runtime_config runtime_config;

std::shared_ptr<wf::config::option_base_t> wf::detail::load_raw_option(const std::string& name)
{
    return wf::get_core().config->get_option(name);
}

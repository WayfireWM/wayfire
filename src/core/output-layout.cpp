#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/view.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/util.hpp"
#include "wayfire/config-backend.hpp"

#include "../output/output-impl.hpp"
#include <xf86drmMode.h>
#include <cstring>
#include <climits>
#include <unordered_set>
#include <drm_fourcc.h>
#include <wayfire/seat.hpp>

#include <wayfire/debug.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

static void*const WF_NOOP_OUTPUT_MAGIC = (void*)0x1234;

// wlroots wrappers
namespace wf
{
class wlr_output_state_setter_t
{
  public:
    wlr_output_state_setter_t()
    {
        wlr_output_state_init(&pending);
    }

    ~wlr_output_state_setter_t()
    {
        wlr_output_state_finish(&pending);
    }

    wlr_output_state_setter_t(const wlr_output_state_setter_t&) = delete;
    wlr_output_state_setter_t(wlr_output_state_setter_t&&) = delete;
    wlr_output_state_setter_t& operator =(const wlr_output_state_setter_t&) = delete;
    wlr_output_state_setter_t& operator =(wlr_output_state_setter_t&&) = delete;

    void reset()
    {
        wlr_output_state_finish(&pending);
        wlr_output_state_init(&pending);
    }

    // Commit all pending changes on the output.
    // Returns true if the output was successfully committed.
    //
    // After this operation, the pending output state (@pending) is reset.
    bool commit(wlr_output *output)
    {
        bool r = wlr_output_commit_state(output, &pending);
        reset();
        return r;
    }

    // Test whether the pending state can be applied on the output.
    bool test(wlr_output *output)
    {
        return wlr_output_test_state(output, &pending);
    }

    // Test whether the pending state can be applied on the output.
    // If so, commit the state.
    //
    // In both cases, reset @pending.
    bool test_and_commit(wlr_output *output)
    {
        if (test(output))
        {
            commit(output);
            return true;
        } else
        {
            reset();
            return false;
        }
    }

    wlr_output_state pending;
};
}

static wl_output_transform get_transform_from_string(std::string transform)
{
    if (transform == "normal")
    {
        return WL_OUTPUT_TRANSFORM_NORMAL;
    } else if (transform == "90")
    {
        return WL_OUTPUT_TRANSFORM_90;
    } else if (transform == "180")
    {
        return WL_OUTPUT_TRANSFORM_180;
    } else if (transform == "270")
    {
        return WL_OUTPUT_TRANSFORM_270;
    } else if (transform == "flipped")
    {
        return WL_OUTPUT_TRANSFORM_FLIPPED;
    } else if (transform == "180_flipped")
    {
        return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    } else if (transform == "90_flipped")
    {
        return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    } else if (transform == "270_flipped")
    {
        return WL_OUTPUT_TRANSFORM_FLIPPED_270;
    }

    LOGE("Bad output transform in config: ", transform);

    return WL_OUTPUT_TRANSFORM_NORMAL;
}

wlr_output_mode *find_matching_mode(wlr_output *output,
    const wlr_output_mode& reference, bool exact_match = false)
{
    wlr_output_mode *mode;
    wlr_output_mode *best = NULL;

    // Pick highest refresh rate by default
    int target_refresh = reference.refresh;
    if (target_refresh == 0)
    {
        target_refresh = INT_MAX;
    }

    wl_list_for_each(mode, &output->modes, link)
    {
        if ((mode->width == reference.width) && (mode->height == reference.height))
        {
            if (mode->refresh == reference.refresh)
            {
                return mode;
            }

            if ((reference.refresh == 0) && mode->preferred)
            {
                // If there is a preferred mode and there is no refresh configured, pick preferred mode.
                return mode;
            }

            if (exact_match)
            {
                continue;
            }

            const int best_so_far = best ? std::abs(best->refresh - target_refresh) : INT_MAX;
            const int current     = std::abs(mode->refresh - target_refresh);
            if (!best || (best_so_far > current))
            {
                best = mode;
            }
        }
    }

    return best;
}

// from rootston
static bool parse_modeline(const char *modeline, drmModeModeInfo & mode)
{
    char hsync[16];
    char vsync[16];
    char interlace[16];
    interlace[0] = '\0';
    float fclock;

    std::memset(&mode, 0, sizeof(mode));
    mode.type = DRM_MODE_TYPE_USERDEF;

    if (sscanf(modeline, "%f %hd %hd %hd %hd %hd %hd %hd %hd %15s %15s %15s",
        &fclock, &mode.hdisplay, &mode.hsync_start, &mode.hsync_end,
        &mode.htotal, &mode.vdisplay, &mode.vsync_start, &mode.vsync_end,
        &mode.vtotal, hsync, vsync, interlace) < 11)
    {
        return false;
    }

    mode.clock    = fclock * 1000;
    mode.vrefresh = mode.clock * 1000.0 * 1000.0 / mode.htotal / mode.vtotal;
    if (strcasecmp(hsync, "+hsync") == 0)
    {
        mode.flags |= DRM_MODE_FLAG_PHSYNC;
    } else if (strcasecmp(hsync, "-hsync") == 0)
    {
        mode.flags |= DRM_MODE_FLAG_NHSYNC;
    } else
    {
        return false;
    }

    if (strcasecmp(vsync, "+vsync") == 0)
    {
        mode.flags |= DRM_MODE_FLAG_PVSYNC;
    } else if (strcasecmp(vsync, "-vsync") == 0)
    {
        mode.flags |= DRM_MODE_FLAG_NVSYNC;
    } else
    {
        return false;
    }

    if (strcasecmp(interlace, "interlace") == 0)
    {
        mode.flags |= DRM_MODE_FLAG_INTERLACE;
    }

    snprintf(mode.name, sizeof(mode.name), "%dx%d@%d",
        mode.hdisplay, mode.vdisplay, mode.vrefresh / 1000);

    return true;
}

namespace wf
{
void transfer_views(wf::output_t *from, wf::output_t *to)
{
    assert(from);

    LOGI("transfer views from ", from->handle->name, " -> ", to ? to->handle->name : "null");

    // Step 1: move views from the current workspace set to the other output
    if (to)
    {
        /* If we aren't moving to another output, then there is no need to
         * enumerate views either */
        auto views = from->wset()->get_views(WSET_SORT_STACKING);
        for (auto& view : views)
        {
            move_view_to_output(view, to, true);
        }
    }

    // Step 2: Ensure none of the remaining views have an invalid output.
    // Note that all views in workspace sets will have their output reassigned automatically by the
    // workspace-set impl.
    std::vector<std::shared_ptr<wf::view_interface_t>> non_ws_views;
    for (auto& view : wf::get_core().get_all_views())
    {
        if ((view->get_output() == from) && (!toplevel_cast(view) || !toplevel_cast(view)->get_wset()))
        {
            // Take a ref, so that the view doesn't get destroyed while we're doing operations on the views
            non_ws_views.push_back(view->shared_from_this());
        }
    }

    for (auto& view : non_ws_views)
    {
        if (view->role == VIEW_ROLE_DESKTOP_ENVIRONMENT)
        {
            // typically: layer-shell views that should be closed, since they are tied to a single output
            view->close();
            view->set_output(nullptr);
        } else
        {
            // typically: xwayland OR views
            view->set_output(to);
        }
    }
}

bool output_state_t::operator ==(const output_state_t& other) const
{
    if (source == OUTPUT_IMAGE_SOURCE_NONE)
    {
        return other.source == OUTPUT_IMAGE_SOURCE_NONE;
    }

    if (source == OUTPUT_IMAGE_SOURCE_MIRROR)
    {
        return other.source == OUTPUT_IMAGE_SOURCE_MIRROR &&
               mirror_from == other.mirror_from;
    }

    bool eq = true;

    eq &= source == other.source;
    eq &= position == other.position;
    eq &= (mode.width == other.mode.width);
    eq &= (mode.height == other.mode.height);
    eq &= (mode.refresh == other.mode.refresh);
    eq &= (transform == other.transform);
    eq &= (scale == other.scale);
    eq &= (vrr == other.vrr);
    eq &= (depth == other.depth);

    return eq;
}

inline bool is_shutting_down()
{
    return wf::get_core().get_current_state() == compositor_state_t::SHUTDOWN;
}

static const char *get_format_name(uint32_t format)
{
    switch (format)
    {
      case DRM_FORMAT_XRGB2101010:
        return "DRM_FORMAT_XRGB2101010";

      case DRM_FORMAT_XBGR2101010:
        return "DRM_FORMAT_XBGR2101010";

      case DRM_FORMAT_XRGB8888:
        return "DRM_FORMAT_XRGB8888";

      case DRM_FORMAT_INVALID:
      default:
        return "DRM_FORMAT_INVALID";
    }
}

/** Represents a single output in the output layout */
struct output_layout_output_t
{
    wlr_output *handle;
    wlr_output_state_setter_t pending_state;
    output_state_t current_state{};
    bool is_externally_managed = false;
    bool is_nested_compositor  = false;
    bool inhibited = false;
    std::map<int, std::vector<uint32_t>> formats_for_depth;
    int current_bit_depth = RENDER_BIT_DEPTH_DEFAULT;

    std::unique_ptr<wf::output_impl_t> output;
    wl_listener_wrapper on_destroy, on_commit;

    std::shared_ptr<wf::config::section_t> config_section;
    wf::option_wrapper_t<wf::output_config::mode_t> mode_opt;
    wf::option_wrapper_t<wf::output_config::position_t> position_opt;
    wf::option_wrapper_t<double> scale_opt;
    wf::option_wrapper_t<std::string> transform_opt;
    wf::option_wrapper_t<bool> vrr_opt;
    wf::option_wrapper_t<int> depth_opt;

    wf::option_wrapper_t<bool> use_ext_config{
        "workarounds/use_external_output_configuration"};

    void initialize_config_options()
    {
        config_section = wf::get_core().config_backend->get_output_section(handle);
        auto name = config_section->get_name();
        mode_opt.load_option(name + "/mode");
        position_opt.load_option(name + "/position");
        scale_opt.load_option(name + "/scale");
        transform_opt.load_option(name + "/transform");
        vrr_opt.load_option(name + "/vrr");
        depth_opt.load_option(name + "/depth");
    }

    output_layout_output_t(wlr_output *handle)
    {
        this->handle = handle;
        on_destroy.connect(&handle->events.destroy);
        initialize_config_options();

        is_nested_compositor = wlr_output_is_wl(handle);

#if WLR_HAS_X11_BACKEND
        is_nested_compositor |= wlr_output_is_x11(handle);
#endif
        if (is_nested_compositor)
        {
            /* Nested backends can be resized by the user. We need to handle
             * these cases */
            on_commit.set_callback([=] (void *data)
            {
                wlr_output_event_commit *ev = static_cast<wlr_output_event_commit*>(data);
                if (ev->state->committed & WLR_OUTPUT_STATE_MODE)
                {
                    handle_mode_changed();
                }
            });
            on_commit.connect(&handle->events.commit);
        }

        formats_for_depth[8]  = {DRM_FORMAT_XRGB8888};
        formats_for_depth[10] = {
            DRM_FORMAT_XRGB2101010,
            DRM_FORMAT_XBGR2101010,
            DRM_FORMAT_XRGB8888,
        };
    }

    /**
     * Update the current configuration based on the mode set by the
     * backend.
     */
    void handle_mode_changed()
    {
        auto& lmanager = wf::get_core().output_layout;
        auto config    = lmanager->get_current_configuration();
        if (config.count(handle) &&
            (config[handle].source == OUTPUT_IMAGE_SOURCE_SELF))
        {
            if (output && (output->get_screen_size() != get_effective_size()))
            {
                /* mode changed. Apply new configuration. */
                current_state.mode.width   = handle->width;
                current_state.mode.height  = handle->height;
                current_state.mode.refresh = handle->refresh;
                this->output->set_effective_size(get_effective_size());
                this->output->render->damage_whole();
                emit_configuration_changed(wf::OUTPUT_MODE_CHANGE);

                // Emit the output-layout-configuration-changed signal as well, which is usually emitted for
                // all changed outputs together in output-layout::apply_state(). However, resizing nested
                // backends does not use apply_state(), hence we have to emit the signal manually.
                output_layout_configuration_changed_signal ev;
                wf::get_core().output_layout->emit(&ev);
            }
        }
    }

    wlr_output_mode select_default_mode()
    {
        wlr_output_mode *mode;
        wl_list_for_each(mode, &handle->modes, link)
        {
            if (mode->preferred)
            {
                return *mode;
            }
        }

        /* Couldn't find a preferred mode. Just return the last, which is
         * usually also the "largest" */
        wl_list_for_each_reverse(mode, &handle->modes, link)

        return *mode;

        /* Finally, if there isn't any mode (for ex. wayland backend),
         * try the wlr_output resolution, falling back to 1200x720
         * if width or height is <= 0 */
        wlr_output_mode default_mode;
        auto width   = handle->width > 0 ? handle->width : 1200;
        auto height  = handle->height > 0 ? handle->height : 720;
        auto refresh = handle->refresh > 0 ? handle->refresh : 0;

        default_mode.width   = width;
        default_mode.height  = height;
        default_mode.refresh = refresh;

        return default_mode;
    }

    /* Returns true if mode setting for the given output can succeed */
    bool is_mode_supported(const wlr_output_mode& query)
    {
        /* DRM doesn't support setting a custom mode, so any supported mode
         * must be found in the mode list */
        if (wlr_output_is_drm(handle))
        {
            wlr_output_mode *mode;
            wl_list_for_each(mode, &handle->modes, link)
            {
                if ((mode->width == query.width) && (mode->height == query.height))
                {
                    return true;
                }
            }

            return false;
        }

        /* X11 and Wayland backends support setting custom modes */
        return true;
    }

    /**
     * Determine whether the state in the config file should be ignored.
     */
    bool should_ignore_config_state()
    {
        if (is_externally_managed && use_ext_config)
        {
            wf::output_config::mode_t mode = mode_opt;
            if (mode.get_type() == output_config::MODE_MIRROR)
            {
                // Special case: output mirroring
                // It is not supported directly supported by wlr-output-management
                // Thus, if the config file says to mirror an output, we do use that
                // information.
                return false;
            }

            return true;
        }

        return false;
    }

    /**
     * Load the state the output is configured with.
     * This is typically the config file, but in case of daemons like kanshi this
     * might be the external configuration.
     */
    output_state_t load_configured_state()
    {
        // Ensure custom modes from the config are enabled
        // Also make sure to refresh them even if the output is externally
        // managed.
        refresh_custom_modes();

        if (should_ignore_config_state())
        {
            // Current state is what was requested by the client.
            return this->current_state;
        }

        output_state_t state;
        state.position = position_opt;

        wf::output_config::mode_t mode = mode_opt;
        wlr_output_mode tmp;

        LOGI("loaded mode ",
            ((wf::option_sptr_t<wf::output_config::mode_t>)mode_opt)->get_value_str());

        switch (mode.get_type())
        {
          case output_config::MODE_AUTO:
            state.mode   = select_default_mode();
            state.source = OUTPUT_IMAGE_SOURCE_SELF;
            break;

          // fallthrough
          case output_config::MODE_RESOLUTION:
            tmp.width    = mode.get_width();
            tmp.height   = mode.get_height();
            tmp.refresh  = mode.get_refresh();
            state.mode   = (is_mode_supported(tmp) ? tmp : select_default_mode());
            state.source = OUTPUT_IMAGE_SOURCE_SELF;
            break;

          case output_config::MODE_OFF:
            state.source = OUTPUT_IMAGE_SOURCE_NONE;
            return state;

          case output_config::MODE_MIRROR:
            state.source = OUTPUT_IMAGE_SOURCE_MIRROR;
            state.mode   = select_default_mode();
            state.mirror_from = mode.get_mirror_from();
            break;
        }

        state.scale     = scale_opt;
        state.transform = get_transform_from_string(transform_opt);
        state.vrr   = vrr_opt;
        state.depth = depth_opt;
        return state;
    }

    void ensure_wayfire_output(const wf::dimensions_t& effective_size)
    {
        if (this->output)
        {
            this->output->set_effective_size(effective_size);

            return;
        }

        this->output =
            std::make_unique<wf::output_impl_t>(handle, effective_size);
        auto wo = output.get();
        priv_render_manager_start_rendering(wo->render.get());

        /* Focus the first output, but do not change the focus on subsequently
         * added outputs. We also change the focus if the noop output was
         * focused */
        wlr_output *focused = get_core().seat->get_active_output() ?
            get_core().seat->get_active_output()->handle : nullptr;
        if (!focused || (focused->data == WF_NOOP_OUTPUT_MAGIC))
        {
            get_core().seat->focus_output(wo);
        }

        output_added_signal data;
        data.output = wo;
        get_core().output_layout->emit(&data);
    }

    void destroy_wayfire_output()
    {
        if (!this->output)
        {
            return;
        }

        LOGE("disabling output: ", output->handle->name);

        auto wo = output.get();

        output_pre_remove_signal data;
        data.output = wo;
        wo->emit(&data);
        get_core().output_layout->emit(&data);
        wo->cancel_active_plugins();

        bool shutdown = is_shutting_down();
        if ((get_core().seat->get_active_output() == wo) && !shutdown)
        {
            get_core().seat->focus_output(
                get_core().output_layout->get_next_output(wo));
        } else if (shutdown)
        {
            get_core().seat->focus_output(nullptr);
        }

        /* It doesn't make sense to transfer to another output if we're
         * going to shut down the compositor */
        transfer_views(wo, shutdown ? nullptr : get_core().seat->get_active_output());

        wf::output_removed_signal data2;
        data2.output = wo;
        get_core().output_layout->emit(&data2);
        this->output = nullptr;
    }

    std::unordered_set<std::string> added_custom_modes;
    void add_custom_mode(std::string modeline)
    {
        if (added_custom_modes.count(modeline))
        {
            return;
        }

        added_custom_modes.insert(modeline);
        drmModeModeInfo *mode = new drmModeModeInfo;
        if (!parse_modeline(modeline.c_str(), *mode))
        {
            LOGE("invalid modeline ", modeline, " in config file");

            return;
        }

        LOGD("output ", handle->name, ": adding custom mode ", mode->name);
        if (wlr_output_is_drm(handle))
        {
            wlr_drm_connector_add_mode(handle, mode);
        }
    }

    void refresh_custom_modes()
    {
        auto section =
            wf::get_core().config_backend->get_output_section(handle);
        static const std::string custom_mode_prefix = "custom_mode";
        for (auto& opt : section->get_registered_options())
        {
            if (custom_mode_prefix ==
                opt->get_name().substr(0, custom_mode_prefix.length()))
            {
                add_custom_mode(opt->get_value_str());
            }
        }
    }

    /** Check whether the given state can be applied */
    bool test_state(const output_state_t& state)
    {
        return true;
    }

    /** Change the output mode */
    void apply_mode(const wlr_output_mode& mode, bool custom_mode)
    {
        if (handle->current_mode)
        {
            /* Do not modeset if nothing changed */
            if ((handle->current_mode->width == mode.width) &&
                (handle->current_mode->height == mode.height) &&
                (handle->current_mode->refresh == mode.refresh) &&
                ((handle->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED) == current_state.vrr) &&
                (current_bit_depth == current_state.depth))
            {
                /* Commit the enabling of the output */
                pending_state.commit(handle);
                return;
            }
        }

        refresh_custom_modes();
        auto built_in = find_matching_mode(handle, mode, custom_mode);
        if (built_in)
        {
            wlr_output_state_set_mode(&pending_state.pending, built_in);
        } else
        {
            LOGI("Couldn't find matching mode ",
                mode.width, "x", mode.height, "@", mode.refresh / 1000.0,
                " for output ", handle->name, ". Trying to use custom mode",
                "(might not work)");

            wlr_output_state_set_custom_mode(&pending_state.pending, mode.width, mode.height, mode.refresh);
        }

        pending_state.commit(handle);

        const bool adaptive_sync_enabled = (handle->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED);

        if (adaptive_sync_enabled != current_state.vrr)
        {
            wlr_output_state_set_adaptive_sync_enabled(&pending_state.pending, current_state.vrr);
            if (pending_state.test_and_commit(handle))
            {
                LOGD("Changed adaptive sync on output: ", handle->name, " to ", current_state.vrr);
            } else
            {
                LOGE("Failed to change adaptive sync on output: ", handle->name);
            }
        }

        if (current_state.depth != current_bit_depth)
        {
            for (auto fmt : formats_for_depth[current_state.depth])
            {
                wlr_output_state_set_render_format(&pending_state.pending, fmt);
                if (pending_state.test_and_commit(handle))
                {
                    current_bit_depth = current_state.depth;
                    LOGD("Set output format to ", get_format_name(fmt), " on output ", handle->name);
                    break;
                }

                LOGD("Failed to set output format ", get_format_name(fmt), " on output ", handle->name);
            }
        }
    }

    /* Mirroring implementation */
    wl_listener_wrapper on_mirrored_frame;
    wl_listener_wrapper on_frame;
    wlr_output *locked_cursors_on = NULL;

    /** Render the output using texture as source */
    void render_output(wlr_texture *texture)
    {
        int buffer_age;
        struct wlr_render_pass *pass = wlr_output_begin_render_pass(handle, &pending_state.pending,
            &buffer_age, NULL);
        if (pass == NULL)
        {
            return;
        }

        wf::texture_t tex{texture};
        OpenGL::render_transformed_texture(tex, {-1, -1, 2, 2});

        wlr_render_pass_submit(pass);
        pending_state.commit(handle);
    }

    /* Load output contents and render them */
    wlr_buffer *source_back_buffer = NULL;

    void handle_frame()
    {
        auto wo = get_core().output_layout->find_output(
            current_state.mirror_from);
        if (!wo)
        {
            LOGE("Cannot find mirrored output ", current_state.mirror_from,
                " for output ", handle->name);

            return;
        }

        if (source_back_buffer == NULL)
        {
            LOGE("Got empty buffer on ", wo->handle->name);
            return;
        }

        auto texture = wlr_texture_from_buffer(get_core().renderer, source_back_buffer);
        if (!texture)
        {
            LOGE("Failed to export texture to dmabuf!");
            return;
        }

        render_output(texture);
        wlr_texture_destroy(texture);
    }

    void set_enabled(bool enabled)
    {
        wlr_output_state_set_enabled(&pending_state.pending, enabled);
        if (!enabled)
        {
            pending_state.commit(handle);
        }
    }

    void setup_mirror()
    {
        /* Check if we can mirror */
        auto wo = get_core().output_layout->find_output(
            current_state.mirror_from);

        bool mirror_active = (wo != nullptr);
        if (wo)
        {
            auto config =
                get_core().output_layout->get_current_configuration();
            auto& wo_state = config[wo->handle];

            if (wo_state.source & OUTPUT_IMAGE_SOURCE_NONE)
            {
                mirror_active = false;
            }
        }

        if (!mirror_active)
        {
            /* If we mirror from a DPMS or an OFF output, we should turn
             * off this output as well */
            set_enabled(false);
            LOGI(handle->name, ": Cannot mirror from output ",
                current_state.mirror_from, ". Disabling output.");

            return;
        }

        /* Force software cursors on the mirrored from output.
         * This ensures that they will be copied when reading pixels
         * from the main plane */
        wlr_output_lock_software_cursors(wo->handle, true);
        locked_cursors_on = wo->handle;

        wlr_output_schedule_frame(handle);
        on_mirrored_frame.set_callback([=] (void *data)
        {
            auto ev = (wlr_output_event_commit*)data;
            if (!ev || !ev->state || !ev->state->buffer)
            {
                return;
            }

            if (ev->state->buffer)
            {
                if (source_back_buffer)
                {
                    wlr_buffer_unlock(source_back_buffer);
                }

                source_back_buffer = ev->state->buffer;
                wlr_buffer_lock(ev->state->buffer);
            }

            /* The mirrored output was repainted, schedule repaint
             * for us as well */
            wlr_output_schedule_frame(handle);
        });
        on_mirrored_frame.connect(&wo->handle->events.commit);

        on_frame.set_callback([=] (void*) { handle_frame(); });
        on_frame.connect(&handle->events.frame);
    }

    void teardown_mirror()
    {
        if (locked_cursors_on)
        {
            wlr_output_lock_software_cursors(locked_cursors_on, false);
            locked_cursors_on = NULL;
        }

        if (source_back_buffer)
        {
            wlr_buffer_unlock(source_back_buffer);
            source_back_buffer = NULL;
        }

        on_mirrored_frame.disconnect();
        on_frame.disconnect();
    }

    wf::dimensions_t get_effective_size()
    {
        wf::dimensions_t effective_size;
        wlr_output_effective_resolution(handle,
            &effective_size.width, &effective_size.height);

        return effective_size;
    }

    /**
     * Send the output-configuration-changed signal.
     */
    void emit_configuration_changed(uint32_t changed_fields)
    {
        if ((handle->data != WF_NOOP_OUTPUT_MAGIC) && changed_fields)
        {
            wf::output_configuration_changed_signal data{current_state};
            data.output = output.get();
            data.changed_fields = changed_fields;
            output->emit(&data);
        }
    }

    /** Apply the given state to the output, ignoring position.
     *
     * This won't have any effect if the output state can't be applied,
     * i.e if test_state(state) == false */
    void apply_state(const output_state_t& state)
    {
        if (!test_state(state))
        {
            return;
        }

        uint32_t changed_fields = 0;
        if (this->current_state.source != state.source)
        {
            changed_fields |= wf::OUTPUT_SOURCE_CHANGE;
        }

        if ((this->current_state.mode.width != state.mode.width) ||
            (this->current_state.mode.height != state.mode.height) ||
            (this->current_state.mode.refresh != state.mode.refresh))
        {
            changed_fields |= wf::OUTPUT_MODE_CHANGE;
        }

        if (this->current_state.scale != state.scale)
        {
            changed_fields |= wf::OUTPUT_SCALE_CHANGE;
        }

        if (this->current_state.transform != state.transform)
        {
            changed_fields |= wf::OUTPUT_TRANSFORM_CHANGE;
        }

        if (!(this->current_state.position == state.position))
        {
            changed_fields |= wf::OUTPUT_POSITION_CHANGE;
        }

        this->current_state = state;

        /* Even if output will remain mirrored, we can tear it down and set
         * up again, in case the output to mirror from changed */
        teardown_mirror();

        if (state.source == OUTPUT_IMAGE_SOURCE_NONE)
        {
            /* output is OFF */
            destroy_wayfire_output();
            set_enabled(false);

            return;
        }

        set_enabled(!(state.source & OUTPUT_IMAGE_SOURCE_NONE));
        apply_mode(state.mode, state.uses_custom_mode);

        if (state.source & OUTPUT_IMAGE_SOURCE_SELF)
        {
            if (handle->transform != state.transform)
            {
                wlr_output_state_set_transform(&pending_state.pending, state.transform);
            }

            if (handle->scale != state.scale)
            {
                wlr_output_state_set_scale(&pending_state.pending, state.scale);
            }

            pending_state.commit(handle);

            ensure_wayfire_output(get_effective_size());
            output->render->damage_whole();
            emit_configuration_changed(changed_fields);
        } else /* state.source == OUTPUT_IMAGE_SOURCE_MIRROR */
        {
            destroy_wayfire_output();
            setup_mirror();
        }
    }
};

class output_layout_t::impl
{
    std::map<wlr_output*, std::unique_ptr<output_layout_output_t>> outputs;

    wlr_output_layout *output_layout;
    wlr_output_manager_v1 *output_manager;
    wlr_output_power_manager_v1 *output_pw_manager;

    wl_listener_wrapper on_new_output;
    wl_listener_wrapper on_output_manager_test;
    wl_listener_wrapper on_output_manager_apply;
    wl_listener_wrapper on_output_power_mode_set;

    wl_listener_wrapper on_backend_destroy;

    wl_idle_call idle_update_configuration;
    wl_timer<false> timer_remove_noop;

    wlr_backend *noop_backend;
    /* Wayfire generally assumes that an enabled output is always available.
     * However, when switching connectors or something it might happen that
     * temporarily no output is available. For those cases, we create a
     * virtual output with the noop backend. */
    std::unique_ptr<output_layout_output_t> noop_output;

    wf::signal::connection_t<wf::reload_config_signal> on_config_reload = [=] (wf::reload_config_signal *ev)
    {
        reconfigure_from_config();
    };

    wf::signal::connection_t<core_backend_started_signal> on_backend_started =
        [=] (core_backend_started_signal *ev)
    {
        // We need to ensure that at any given time we have at least one
        // output while core is running.
        //
        // Thus we need to make sure the noop output is available if nothing
        // else is at startup.
        if (get_outputs().empty())
        {
            ensure_noop_output();
        }
    };

    void deinit_noop()
    {
        /* Disconnect timer, since otherwise it will be destroyed
         * after the wayland display is. */
        this->timer_remove_noop.disconnect();
        if (noop_output)
        {
            noop_output->destroy_wayfire_output();
            noop_output.reset();
        }
    }

  public:
    impl(wlr_backend *backend)
    {
        on_new_output.set_callback([=] (void *data)
        {
            add_output((wlr_output*)data);
        });
        on_new_output.connect(&backend->events.new_output);

        // We destroy the noop output when the renderer is destroyed.
        // This is needed because the noop output uses the same wlr_egl
        // as the real outputs.
        on_backend_destroy.set_callback([=] (auto) { deinit_noop(); });
        on_backend_destroy.connect(&wf::get_core().renderer->events.destroy);

        output_layout = wlr_output_layout_create(get_core().display);
        get_core().connect(&on_config_reload);

        noop_backend = wlr_headless_backend_create(get_core().ev_loop);
        wlr_backend_start(noop_backend);

        get_core().connect(&on_backend_started);

        output_manager = wlr_output_manager_v1_create(get_core().display);
        on_output_manager_test.set_callback([=] (void *data)
        {
            apply_wlr_configuration((wlr_output_configuration_v1*)data, true);
        });
        on_output_manager_apply.set_callback([=] (void *data)
        {
            apply_wlr_configuration((wlr_output_configuration_v1*)data, false);
        });

        on_output_manager_test.connect(&output_manager->events.test);
        on_output_manager_apply.connect(&output_manager->events.apply);

        output_pw_manager = wlr_output_power_manager_v1_create(get_core().display);
        on_output_power_mode_set.set_callback([=] (void *data)
        {
            set_power_mode((wlr_output_power_v1_set_mode_event*)data);
        });
        on_output_power_mode_set.connect(&output_pw_manager->events.set_mode);
    }

    void fini()
    {
        // Destroy outputs first
        this->outputs.clear();
        noop_output.reset();

        // Disconnect all signals
        on_new_output.disconnect();
        on_output_manager_test.disconnect();
        on_output_manager_apply.disconnect();
        on_output_power_mode_set.disconnect();
        on_backend_destroy.disconnect();

        wlr_backend_destroy(noop_backend);
        wlr_output_layout_destroy(output_layout);
    }

    impl(const impl &) = delete;
    impl(impl &&) = delete;
    impl& operator =(const impl&) = delete;
    impl& operator =(impl&&) = delete;

    output_configuration_t output_configuration_from_wlr_configuration(
        wlr_output_configuration_v1 *configuration)
    {
        output_configuration_t result;
        wlr_output_configuration_head_v1 *head;
        wl_list_for_each(head, &configuration->heads, link)
        {
            if (!this->outputs.count(head->state.output))
            {
                LOGE("Output configuration request contains unknown",
                    " output, probably a compositor bug!");
                continue;
            }

            auto& handle = head->state.output;
            auto& state  = result[handle];

            if (!head->state.enabled)
            {
                state.source = OUTPUT_IMAGE_SOURCE_NONE;
                continue;
            }

            state.source = OUTPUT_IMAGE_SOURCE_SELF;

            if (head->state.mode)
            {
                state.mode = *head->state.mode;
            } else
            {
                state.mode.width   = head->state.custom_mode.width;
                state.mode.height  = head->state.custom_mode.height;
                state.mode.refresh = head->state.custom_mode.refresh;
                state.uses_custom_mode = true;
            }

            state.position  = {head->state.x, head->state.y};
            state.scale     = head->state.scale;
            state.transform = head->state.transform;
            state.vrr = head->state.adaptive_sync_enabled;
            if ((handle->render_format == DRM_FORMAT_XRGB2101010) ||
                (handle->render_format == DRM_FORMAT_XBGR2101010))
            {
                state.depth = 10;
            } else
            {
                state.depth = 8;
            }
        }

        return result;
    }

    void apply_wlr_configuration(
        wlr_output_configuration_v1 *wlr_configuration, bool test_only)
    {
        auto configuration =
            output_configuration_from_wlr_configuration(wlr_configuration);

        if (apply_configuration(configuration, test_only))
        {
            // Notify outputs that they have external configuration
            for (auto& [wo, _] : configuration)
            {
                this->outputs[wo]->is_externally_managed = true;
            }

            wlr_output_configuration_v1_send_succeeded(wlr_configuration);
        } else
        {
            wlr_output_configuration_v1_send_failed(wlr_configuration);
        }
    }

    void ensure_noop_output()
    {
        LOGI("new output: NOOP-1");

        if (!noop_output)
        {
            auto handle = wlr_headless_add_output(noop_backend, 1280, 720);
            handle->data = WF_NOOP_OUTPUT_MAGIC;
            strcpy(handle->name, "NOOP-1");

            if (!wlr_output_init_render(handle,
                get_core().allocator, get_core().renderer))
            {
                LOGE("failed to init wlr render for noop output!");
                // XXX: can we even recover from this??
                std::exit(0);
            }

            noop_output = std::make_unique<output_layout_output_t>(handle);
        }

        /* Make sure that the noop output is up and running even before the
         * next reconfiguration. This is needed because if we are removing
         * an output, we might get into a situation where the last physical
         * output has already been removed but we are yet to add the noop one */
        noop_output->apply_state(noop_output->load_configured_state());
        wlr_output_layout_add_auto(output_layout, noop_output->handle);
        timer_remove_noop.disconnect();
    }

    void remove_noop_output()
    {
        if (!noop_output)
        {
            return;
        }

        if (noop_output->current_state.source == OUTPUT_IMAGE_SOURCE_NONE)
        {
            return;
        }

        LOGI("remove output: NOOP-1");

        output_state_t state;
        state.source = OUTPUT_IMAGE_SOURCE_NONE;
        noop_output->apply_state(state);
        wlr_output_layout_remove(output_layout, noop_output->handle);
        // Trigger repositioning of all outputs
        apply_configuration(get_current_configuration());
    }

    void add_output(wlr_output *output)
    {
        LOGI("new output: ", output->name,
            " (\"", output->make, " ", output->model, " ", output->serial, "\")");

        if (output->non_desktop)
        {
            LOGD("Non-desktop output ", output->name, " found");
            if (get_core().protocols.drm_v1)
            {
                LOGD("Drm lease offered to ", output->name);
                wlr_drm_lease_v1_manager_offer_output(get_core().protocols.drm_v1, output);
            }

            return;
        }

        if (!wlr_output_init_render(output,
            get_core().allocator, get_core().renderer))
        {
            LOGE("failed to init wlr render for output ", output->name);
            return;
        }

        auto lo = new output_layout_output_t(output);
        outputs[output] = std::unique_ptr<output_layout_output_t>(lo);
        lo->on_destroy.set_callback([output, this] (void*)
        {
            remove_output(output);
        });

        reconfigure_from_config();
    }

    void remove_output(wlr_output *to_remove)
    {
        auto active_outputs = get_outputs();
        LOGI("remove output: ", to_remove->name);

        /* Unset mode, plus destroy the wayfire output */
        auto configuration = get_current_configuration();
        configuration[to_remove].source = OUTPUT_IMAGE_SOURCE_NONE;
        apply_configuration(configuration);

        outputs.erase(to_remove);

        /* If no physical outputs, then at least the noop output */
        assert(get_outputs().size() || is_shutting_down());
    }

    /* Get the current configuration of all outputs */
    output_configuration_t get_current_configuration()
    {
        output_configuration_t configuration;
        for (auto& entry : this->outputs)
        {
            configuration[entry.first] = entry.second->current_state;
        }

        return configuration;
    }

    /** Load config from file, test and apply */
    void reconfigure_from_config()
    {
        // Load desired configuration from config file
        output_configuration_t configuration;
        for (auto& [output, layout_output] : this->outputs)
        {
            configuration[output] = layout_output->load_configured_state();
        }

        if (configuration != get_current_configuration())
        {
            if (test_configuration(configuration))
            {
                apply_configuration(configuration);
            }
        }
    }

    /**
     * Calculate the output layout geometry for the state.
     * The state represents a non-automatically positioned enabled output.
     */
    wf::geometry_t calculate_geometry_from_state(
        const output_state_t& state) const
    {
        wf::geometry_t geometry = {
            state.position.get_x(),
            state.position.get_y(),
            (int32_t)(state.mode.width / state.scale),
            (int32_t)(state.mode.height / state.scale),
        };

        if (state.transform & 1)
        {
            std::swap(geometry.width, geometry.height);
        }

        return geometry;
    }

    /** @return A list of geometries of fixed position outputs. */
    std::vector<wf::geometry_t> calculate_fixed_geometries(
        const output_configuration_t& config)
    {
        std::vector<wf::geometry_t> geometries;
        for (auto& entry : config)
        {
            if (!(entry.second.source & OUTPUT_IMAGE_SOURCE_SELF) ||
                entry.second.position.is_automatic_position())
            {
                continue;
            }

            geometries.push_back(calculate_geometry_from_state(entry.second));
        }

        return geometries;
    }

    /** @return true if there are overlapping outputs */
    bool test_overlapping_outputs(const output_configuration_t& config)
    {
        auto geometries = calculate_fixed_geometries(config);
        for (size_t i = 0; i < geometries.size(); i++)
        {
            for (size_t j = i + 1; j < geometries.size(); j++)
            {
                if (geometries[i] & geometries[j])
                {
                    return true;
                }
            }
        }

        return false;
    }

    /** @return true if all outputs are disabled. */
    bool test_all_disabled_outputs(const output_configuration_t& config)
    {
        int count_enabled = 0;
        for (auto& entry : config)
        {
            if (entry.second.source & OUTPUT_IMAGE_SOURCE_SELF)
            {
                ++count_enabled;
            }
        }

        return count_enabled == 0;
    }

    /* @return true if rectangles have a common interior or border point. */
    bool rectangles_touching(const wf::geometry_t& a, const wf::geometry_t& b)
    {
        return !(a.x + a.width < b.x || a.y + a.height < b.y ||
            b.x + b.width < a.x || b.y + b.height < a.y);
    }

    /** @return true if fixed position outputs do not form a continuous space */
    bool test_disjoint_outputs(const output_configuration_t& config)
    {
        auto geometries = calculate_fixed_geometries(config);
        if (geometries.empty())
        {
            /* Not disjoint */
            return false;
        }

        /* Create graph with a vertex for each rectangle.
         * Configuration is disjoint iff the graph has more than one component */
        std::vector<std::vector<int>> graph(geometries.size());
        for (size_t i = 0; i < geometries.size(); i++)
        {
            for (size_t j = i + 1; j < geometries.size(); j++)
            {
                if (rectangles_touching(geometries[i], geometries[j]))
                {
                    graph[i].push_back(j);
                    graph[j].push_back(i);
                }
            }
        }

        /* Do a depth-first-search */
        std::vector<int> visited(geometries.size(), 0);
        std::function<void(int)> dfs;
        dfs = [&] (int u)
        {
            if (visited[u] == 1)
            {
                return;
            }

            visited[u] = 1;
            for (int v : graph[u])
            {
                dfs(v);
            }
        };

        dfs(0);

        // If we have a zero somewhere it means the vertex was not reached
        return *std::min_element(visited.begin(), visited.end()) == 0;
    }

    /** Check whether the given configuration can be applied */
    bool test_configuration(const output_configuration_t& config)
    {
        if (config.size() != this->outputs.size())
        {
            return false;
        }

        bool ok = true;
        for (auto& entry : config)
        {
            if (this->outputs.count(entry.first) == 0)
            {
                return false;
            }

            ok &= this->outputs[entry.first]->test_state(entry.second);
        }

        /* Check overlapping outputs */
        if (test_overlapping_outputs(config))
        {
            LOGE("Overlapping outputs in the output configuration, ",
                "unexpected behavior might occur");
        }

        if (test_all_disabled_outputs(config))
        {
            LOGW("All wayfire outputs have been disabled!");
        }

        if (test_disjoint_outputs(config))
        {
            LOGW("Wayfire outputs have been configured with gaps between them, ",
                "pointer will not be movable between them. Note this might ",
                "be ok before all outputs are connected.");
        }

        return ok;
    }

    /** Apply the given configuration. Config MUST be a valid configuration */
    void apply_configuration(const output_configuration_t& config)
    {
        /* The order in which we enable and disable outputs is important.
         * Firstly, on some systems where there aren't enough CRTCs, we can
         * only enable a subset of all outputs at once. This means we should
         * first try to disable as many outputs as possible, and only then
         * start enabling new ones.
         *
         * Secondly, we need to check when we need to enable noop output -
         * which is exactly when all currently enabled outputs are going to
         * be disabled */

        /* Number of outputs that were enabled and continue to be enabled */
        int count_remaining_enabled = 0;
        auto active_outputs = get_outputs();
        for (auto& wo : active_outputs)
        {
            auto it = config.find(wo->handle);
            if ((it != config.end()) &&
                (it->second.source & OUTPUT_IMAGE_SOURCE_SELF))
            {
                ++count_remaining_enabled;
            }
        }

        bool turning_off_all_active =
            !active_outputs.empty() && count_remaining_enabled == 0;

        if (turning_off_all_active && !is_shutting_down())
        {
            /* If we aren't shutting down, and we will turn off all the
             * currently enabled outputs, we'll need the noop output, as a
             * temporary output to store views in, until a real output is
             * enabled again */
            ensure_noop_output();
        }

        /* First: disable all outputs that need disabling */
        for (auto& entry : config)
        {
            auto& handle = entry.first;
            auto& state  = entry.second;
            auto& lo     = this->outputs[handle];

            if (!(state.source & OUTPUT_IMAGE_SOURCE_SELF))
            {
                /* First shut down the output, move its views, etc. while it
                 * is still in the output layout and its global is active.
                 *
                 * This is needed so that clients can receive
                 * wl_surface.leave events for the to be destroyed output */
                lo->apply_state(state);
                wlr_output_layout_remove(output_layout, handle);
            }
        }

        /* Second: enable outputs with fixed positions. */
        int count_enabled = 0;
        for (auto& entry : config)
        {
            auto& handle = entry.first;
            auto& state  = entry.second;
            auto& lo     = this->outputs[handle];

            if (state.source & OUTPUT_IMAGE_SOURCE_SELF &&
                !entry.second.position.is_automatic_position())
            {
                ++count_enabled;
                wlr_output_layout_add(output_layout, handle,
                    state.position.get_x(), state.position.get_y());
                lo->apply_state(state);
            }
        }

        /*
         * Third: enable dynamically positioned outputs.
         * Since outputs with fixed positions were already added, we know
         * that the outputs here will not be moved after they are added to
         * the output_layout.
         */
        for (auto& entry : config)
        {
            auto& handle = entry.first;
            auto& lo     = this->outputs[handle];
            auto state   = entry.second;
            if (state.source & OUTPUT_IMAGE_SOURCE_SELF &&
                entry.second.position.is_automatic_position())
            {
                ++count_enabled;
                wlr_output_layout_add_auto(output_layout, handle);
                lo->apply_state(state);
            }
        }

        /* Fourth: enable mirrored outputs */
        for (auto& entry : config)
        {
            auto& handle = entry.first;
            auto& state  = entry.second;
            auto& lo     = this->outputs[handle];

            if (state.source == OUTPUT_IMAGE_SOURCE_MIRROR)
            {
                lo->apply_state(state);
                wlr_output_layout_remove(output_layout, handle);
            }
        }

        /* Fifth: emit configuration-changed again for dynamically-positioned outputs, because their position
         * might have changed. */
        for (auto& entry : config)
        {
            auto& handle = entry.first;
            auto& state  = entry.second;
            auto& lo     = this->outputs[handle];

            if (state.source & OUTPUT_IMAGE_SOURCE_SELF &&
                entry.second.position.is_automatic_position())
            {
                lo->emit_configuration_changed(wf::OUTPUT_POSITION_CHANGE);
            }
        }

        wf::output_layout_configuration_changed_signal ev;
        get_core().output_layout->emit(&ev);

        if (count_enabled > 0)
        {
            /* Make sure to remove the noop output if it is no longer needed.
             * NB: Libwayland has a bug when a global is created and
             * immediately destroyed, as clients don't have enough time
             * to bind it. That's why we don't destroy noop immediately,
             * but only after a timeout */
            timer_remove_noop.set_timeout(1000, [=] ()
            {
                remove_noop_output();
            });
        }

        idle_update_configuration.run_once([=] ()
        {
            send_wlr_configuration();
        });
    }

    void send_wlr_configuration()
    {
        auto wlr_configuration = wlr_output_configuration_v1_create();
        for (auto& output : outputs)
        {
            auto head = wlr_output_configuration_head_v1_create(
                wlr_configuration, output.first);

            wlr_box box;
            wlr_output_layout_get_box(output_layout, output.first, &box);
            if (wlr_box_empty(&box))
            {
                head->state.x = 0;
                head->state.y = 0;
            } else
            {
                head->state.x = box.x;
                head->state.y = box.y;
            }
        }

        wlr_output_manager_v1_set_configuration(output_manager,
            wlr_configuration);
    }

    void set_power_mode(wlr_output_power_v1_set_mode_event *ev)
    {
        LOGD("output: ", ev->output->name, " power mode: ", ev->mode);
        auto config = get_current_configuration();
        if (!config.count(ev->output))
        {
            return;
        }

        const bool wants_dpms = (ev->mode == ZWLR_OUTPUT_POWER_V1_MODE_OFF);
        config[ev->output].source = (wants_dpms ? OUTPUT_IMAGE_SOURCE_DPMS : OUTPUT_IMAGE_SOURCE_SELF);
        apply_configuration(config);

        auto& wo = outputs[ev->output];
        if (wo->inhibited != wants_dpms)
        {
            wo->inhibited = wants_dpms;
            wo->output->render->add_inhibit(wants_dpms);
        }

        wo->output->render->damage_whole();
    }

    /* Public API functions */
    wlr_output_layout *get_handle()
    {
        return output_layout;
    }

    size_t get_num_outputs()
    {
        return get_outputs().size();
    }

    wf::output_t *find_output(wlr_output *output)
    {
        if (outputs.count(output))
        {
            return outputs[output]->output.get();
        }

        if (noop_output && (noop_output->handle == output))
        {
            return noop_output->output.get();
        }

        return nullptr;
    }

    wf::output_t *find_output(std::string name)
    {
        for (auto& entry : outputs)
        {
            if (entry.first->name == name)
            {
                return entry.second->output.get();
            }
        }

        if (noop_output && (noop_output->handle->name == name))
        {
            return noop_output->output.get();
        }

        return nullptr;
    }

    std::vector<wf::output_t*> get_outputs()
    {
        std::vector<wf::output_t*> result;
        for (auto& entry : outputs)
        {
            if (entry.second->current_state.source & OUTPUT_IMAGE_SOURCE_SELF)
            {
                result.push_back(entry.second->output.get());
            }
        }

        if (noop_output && noop_output->output)
        {
            result.push_back(noop_output->output.get());
        }

        return result;
    }

    wf::output_t *get_next_output(wf::output_t *output)
    {
        auto os = get_outputs();

        auto it = std::find(os.begin(), os.end(), output);
        if ((it == os.end()) || (std::next(it) == os.end()))
        {
            return os[0];
        } else
        {
            return *(++it);
        }
    }

    wf::output_t *get_output_coords_at(const wf::pointf_t& origin,
        wf::pointf_t& closest)
    {
        wlr_output_layout_closest_point(output_layout, NULL,
            origin.x, origin.y, &closest.x, &closest.y);

        auto handle =
            wlr_output_layout_output_at(output_layout, closest.x, closest.y);
        assert(handle || is_shutting_down());
        if (!handle)
        {
            return nullptr;
        }

        if (noop_output && (handle == noop_output->handle))
        {
            return noop_output->output.get();
        } else
        {
            return outputs[handle]->output.get();
        }
    }

    wf::output_t *get_output_at(int x, int y)
    {
        wf::pointf_t dummy;

        return get_output_coords_at({1.0 * x, 1.0 * y}, dummy);
    }

    bool apply_configuration(const output_configuration_t& configuration,
        bool test_only)
    {
        bool ok = test_configuration(configuration);
        if (ok && !test_only)
        {
            apply_configuration(configuration);
        }

        return ok;
    }
};

/* Just pass to the PIMPL */
output_layout_t::output_layout_t(wlr_backend *b) : pimpl(new impl(b))
{}
output_layout_t::~output_layout_t() = default;

wlr_output_layout*output_layout_t::get_handle()
{
    return pimpl->get_handle();
}

wf::output_t*output_layout_t::get_output_at(int x, int y)
{
    return pimpl->get_output_at(x, y);
}

wf::output_t*output_layout_t::get_output_coords_at(wf::pointf_t origin,
    wf::pointf_t& closest)
{
    return pimpl->get_output_coords_at(origin, closest);
}

size_t output_layout_t::get_num_outputs()
{
    return pimpl->get_num_outputs();
}

std::vector<wf::output_t*> output_layout_t::get_outputs()
{
    return pimpl->get_outputs();
}

wf::output_t*output_layout_t::get_next_output(wf::output_t *output)
{
    return pimpl->get_next_output(output);
}

wf::output_t*output_layout_t::find_output(wlr_output *output)
{
    return pimpl->find_output(output);
}

wf::output_t*output_layout_t::find_output(std::string name)
{
    return pimpl->find_output(name);
}

output_configuration_t output_layout_t::get_current_configuration()
{
    return pimpl->get_current_configuration();
}

bool output_layout_t::apply_configuration(
    const output_configuration_t& configuration, bool test_only)
{
    return pimpl->apply_configuration(configuration, test_only);
}

void priv_output_layout_fini(wf::output_layout_t *layout)
{
    layout->pimpl->fini();
}
}

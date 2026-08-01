#include "wayfire/render-manager.hpp"
#include "pixman.h"
#include "wayfire/config-backend.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/view.hpp"
#include "wayfire/output.hpp"
#include "wayfire/util.hpp"
#include "../main.hpp"
#include "wayfire/workspace-set.hpp" // IWYU pragma: keep
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <deque>
#include <optional>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/nonstd/safe-list.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wayfire/output-layout.hpp>
#include "adaptive-repaint-scheduler.hpp"
#include <ctime>

namespace wf
{
/**
 * swapchain_damage_manager_t is responsible for tracking the damage and managing the swapchain on the
 * given output.
 */
struct swapchain_damage_manager_t
{
    wf::option_wrapper_t<bool> force_frame_sync{"workarounds/force_frame_sync"};
    wf::wl_listener_wrapper on_needs_frame;
    wf::wl_listener_wrapper on_damage;
    wf::wl_listener_wrapper on_gamma_changed;

    wf::region_t frame_damage;
    wlr_output *output;
    wlr_damage_ring damage_ring;
    output_t *wo;

    bool pending_gamma_lut = false;

    std::unique_ptr<wf::scene::render_instance_manager_t> instance_manager;
    void start_rendering()
    {
        scene::damage_callback push_damage = [=] (wf::regionf_t region)
        {
            // Damage is pushed up to the root in root coordinate system,
            // we need it in output-buffer-local coordinate system.
            region += -wf::origin(wo->get_layout_geometry());
            auto framebuffer_damage =
                wo->render->get_target_framebuffer().framebuffer_region_from_geometry_region(region);
            this->damage_buffer(framebuffer_damage, true);
        };

        std::vector<scene::node_ptr> nodes;
        nodes.push_back(wf::get_core().scene());
        instance_manager = std::make_unique<wf::scene::render_instance_manager_t>(nodes, push_damage, wo);
        instance_manager->set_visibility_region(wf::regionf_t{wo->get_layout_geometry()});
    }

    swapchain_damage_manager_t(output_t *output)
    {
        this->output = output->handle;
        this->wo     = output;

        output->connect(&output_mode_changed);

        wlr_damage_ring_init(&damage_ring);
        on_needs_frame.set_callback([=] (void*)
        {
            schedule_repaint();
        });
        on_damage.set_callback([=] (void *data)
        {
            auto ev = static_cast<wlr_output_event_damage*>(data);

            wf::region_t rotated{ev->damage};
            int width, height;
            wlr_output_transformed_resolution(this->output, &width, &height);
            wlr_region_transform(rotated.to_pixman(), rotated.to_pixman(),
                wlr_output_transform_invert(this->output->transform), width, height);
            damage_buffer(rotated, true);
        });

        on_gamma_changed.set_callback([=] (void *data)
        {
            auto event = (const wlr_gamma_control_manager_v1_set_gamma_event*)data;
            if (event->output == this->output)
            {
                pending_gamma_lut = true;
                schedule_repaint();
            }
        });

        on_needs_frame.connect(&output->handle->events.needs_frame);
        on_damage.connect(&output->handle->events.damage);
        on_gamma_changed.connect(&wf::get_core().protocols.gamma_v1->events.set_gamma);
    }

    ~swapchain_damage_manager_t()
    {
        wlr_damage_ring_finish(&damage_ring);
    }

    wf::signal::connection_t<wf::output_configuration_changed_signal>
    output_mode_changed = [=] (wf::output_configuration_changed_signal *ev)
    {
        if (!ev || !ev->changed_fields)
        {
            return;
        }

        schedule_repaint();
        instance_manager->set_visibility_region(wf::regionf_t{wo->get_layout_geometry()});
    };

    /**
     * Damage the given region
     */
    void damage_buffer(const wf::region_t& region, bool repaint)
    {
        if (region.empty())
        {
            return;
        }

        frame_damage |= region;
        wlr_damage_ring_add(&damage_ring, region.to_pixman());
        pending_frame_request = true;
        if (repaint)
        {
            schedule_repaint();
        }
    }

    void damage_buffer(const wlr_box& box, bool repaint)
    {
        if ((box.width <= 0) || (box.height <= 0))
        {
            return;
        }

        /* Wlroots expects damage after scaling */
        frame_damage |= box;
        wlr_damage_ring_add_box(&damage_ring, &box);
        pending_frame_request = true;
        if (repaint)
        {
            schedule_repaint();
        }
    }

    int constant_redraw_counter = 0;
    void set_redraw_always(bool always)
    {
        constant_redraw_counter += (always ? 1 : -1);
        if (constant_redraw_counter > 1) /* no change, exit */
        {
            return;
        }

        if (constant_redraw_counter < 0)
        {
            LOGE("constant_redraw_counter got below 0!");
            constant_redraw_counter = 0;

            return;
        }

        schedule_repaint();
    }

    // A struct which contains the necessary structures for painting one frame
    struct frame_object_t
    {
        wlr_output_state state;
        wlr_buffer *buffer = NULL;
        int buffer_age;

        frame_object_t()
        {
            wlr_output_state_init(&state);
        }

        ~frame_object_t()
        {
            wlr_output_state_finish(&state);
        }

        frame_object_t(const frame_object_t&) = delete;
        frame_object_t(frame_object_t&&) = delete;
        frame_object_t& operator =(const frame_object_t&) = delete;
        frame_object_t& operator =(frame_object_t&&) = delete;
    };

    bool acquire_next_swapchain_buffer(frame_object_t& frame)
    {
        if (!wlr_output_configure_primary_swapchain(output, &frame.state, &output->swapchain))
        {
            LOGE("Failed to configure primary output swapchain for output ", nonull(output->name));
            return false;
        }

        frame.buffer = wlr_swapchain_acquire(output->swapchain);
        if (!frame.buffer)
        {
            LOGE("Failed to acquire buffer from the output swapchain!");
            return false;
        }

        return true;
    }

    bool try_apply_gamma(frame_object_t& next_frame)
    {
        if (!pending_gamma_lut)
        {
            return true;
        }

        pending_gamma_lut = false;
        auto gamma_control =
            wlr_gamma_control_manager_v1_get_control(wf::get_core().protocols.gamma_v1, output);

        if (!wlr_gamma_control_v1_apply(gamma_control, &next_frame.state))
        {
            LOGE("Failed to apply gamma to output state!");
            return false;
        }

        if (!wlr_output_test_state(output, &next_frame.state))
        {
            wlr_gamma_control_v1_send_failed_and_destroy(gamma_control);
        }

        return true;
    }

    bool force_next_frame = false;
    uint64_t request_generation = 0;

    // Tracks whether a new frame is needed at all.
    // Set when new content was damaged, cleared when a frame (composited or direct scanout) is presented.
    // We cannot use the empty damage ring for this check, because the ring is only cleared for 'regular'
    // composited frames. Direct scanout does not clear the ring: the compositor's own render buffers are
    // not updated during scanout, so the accumulated damage must be kept to prevent corrupted frames
    // when transitioning from scanout to compositing.
    bool pending_frame_request = false;

    /**
     * Check whether a new frame should be produced for the output.
     */
    bool should_repaint() const
    {
        return force_next_frame || output->needs_frame || pending_frame_request ||
               (constant_redraw_counter > 0);
    }

    uint64_t get_request_generation() const
    {
        return request_generation;
    }

    /** Consume only requests which existed when this frame began. */
    void frame_committed(uint64_t frame_generation)
    {
        if (frame_generation == request_generation)
        {
            force_next_frame = false;
            pending_frame_request = false;
        } else
        {
            // A request raised during painting may have had its idle callback
            // superseded by this commit. Re-arm it behind the pending page flip.
            schedule_repaint();
        }
    }

    /**
     * Start rendering a new frame.
     * The caller is responsible for checking that a new frame is needed (should_repaint()).
     * If the operation could not be started, the function returns null.
     * If the operation succeeds, a frame object is returned, and the output (E)GL context is bound.
     */
    std::unique_ptr<frame_object_t> start_frame()
    {
        auto buffer_extents = this->get_buffer_extents();
        pixman_region32_intersect_rect(&damage_ring.current, &damage_ring.current,
            buffer_extents.x, buffer_extents.y, buffer_extents.width, buffer_extents.height);

        auto next_frame = std::make_unique<frame_object_t>();
        next_frame->state.committed |= WLR_OUTPUT_STATE_DAMAGE;

        if (!try_apply_gamma(*next_frame))
        {
            return {};
        }

        if (!acquire_next_swapchain_buffer(*next_frame))
        {
            return {};
        }

        // Accumulate damage now, when we are sure we will render the frame.
        // Doing this earlier may mean that the damage from the previous frames
        // creeps into the current frame damage, if we had skipped a frame.
        accumulate_damage(next_frame.get());

        return next_frame;
    }

    bool swap_buffers(std::unique_ptr<frame_object_t> next_frame, const wf::region_t& swap_damage)
    {
        /* If force frame sync option is set, call glFinish to block until
         * the GPU finishes rendering. This can work around some driver
         * bugs, but may cause more resource usage. */
        if (force_frame_sync)
        {
            wf::gles::run_in_context_if_gles([&]
            {
                GL_CALL(glFinish());
            });
        }

        wlr_output_state_set_buffer(&next_frame->state, next_frame->buffer);
        wlr_output_state_set_damage(&next_frame->state, swap_damage.to_pixman());
        auto release_sync = wo->render->next_explicit_sync_release_point();
        if (release_sync)
        {
            wlr_output_state_set_signal_timeline(
                &next_frame->state, release_sync.timeline, release_sync.point);
        }

        wlr_buffer_unlock(next_frame->buffer);

        if (!wlr_output_test_state(output, &next_frame->state))
        {
            LOGE("Output test failed!");
            return false;
        }

        if (!wlr_output_commit_state(output, &next_frame->state))
        {
            LOGE("Output commit failed!");
            return false;
        }

        frame_damage.clear();
        return true;
    }

    /**
     * Accumulate damage from last frame.
     * Needs to be called after make_current()
     */
    void accumulate_damage(frame_object_t *next_frame)
    {
        wf::region_t ring_damage;
        wlr_damage_ring_rotate_buffer(&damage_ring, next_frame->buffer, ring_damage.to_pixman());

        frame_damage |= ring_damage;
        if (runtime_config.no_damage_track)
        {
            frame_damage |= get_buffer_extents();
        }
    }

    /**
     * Return the damage that has been scheduled for the next frame up to now,
     * or, if in a repaint, the damage for the current frame
     */
    wf::regionf_t get_scheduled_damage(const wf::render_target_t& target)
    {
        return target.geometry_region_from_framebuffer_region(frame_damage) & target.geometry;
    }

    /**
     * Schedule a frame for the output
     */
    void schedule_repaint()
    {
        ++request_generation;
        wlr_output_schedule_frame(output);
        force_next_frame = true;
    }

    void schedule_vrr_keepalive()
    {
        if (output->frame_pending || output->needs_frame)
        {
            return;
        }

        wlr_output_schedule_frame(output);
    }

    /**
     * Get the full size of the buffer for damage tracking in output-buffer-local coordinate system
     */
    wlr_box get_buffer_extents() const
    {
        return {0, 0, output->width, output->height};
    }

    /**
     * Same as render_manager::damage_whole()
     */
    void damage_whole()
    {
        damage_buffer(get_buffer_extents(), true);
    }

    wf::wl_idle_call idle_damage;
    /**
     * Same as render_manager::damage_whole_idle()
     */
    void damage_whole_idle()
    {
        damage_whole();
        if (!idle_damage.is_connected())
        {
            idle_damage.run_once([&] () { damage_whole(); });
        }
    }
};

/**
 * Very simple class to manage effect hooks
 */
struct effect_hook_manager_t
{
    using effect_container_t = wf::safe_list_t<effect_hook_t*>;
    effect_container_t effects[OUTPUT_EFFECT_TOTAL];

    void add_effect(effect_hook_t *hook, output_effect_type_t type)
    {
        effects[type].push_back(hook);
    }

    bool can_scanout() const
    {
        return effects[OUTPUT_EFFECT_OVERLAY].size() == 0 &&
               effects[OUTPUT_EFFECT_POST].size() == 0;
    }

    void rem_effect(effect_hook_t *hook)
    {
        for (int i = 0; i < OUTPUT_EFFECT_TOTAL; i++)
        {
            effects[i].remove_all(hook);
        }
    }

    void run_effects(output_effect_type_t type)
    {
        effects[type].for_each([] (auto effect)
        { (*effect)(); });
    }
};

/**
 * A class to manage and run postprocessing effects
 */
struct postprocessing_manager_t
{
    using post_container_t = wf::safe_list_t<post_hook_t*>;
    post_container_t post_effects;
    wf::auxilliary_buffer_t post_buffers[2];
    /* Buffer to which other operations render to */
    static constexpr uint32_t default_out_buffer = 0;

    output_t *output;
    uint32_t output_width, output_height;
    postprocessing_manager_t(output_t *output)
    {
        this->output = output;
    }

    wf::render_buffer_t final_target;
    void set_current_buffer(wlr_buffer *buffer)
    {
        final_target = wf::render_buffer_t{
            buffer,
            wf::dimensions_t{output->handle->width, output->handle->height}
        };
    }

    void allocate(int width, int height)
    {
        if (post_effects.size() == 0)
        {
            return;
        }

        output_width  = width;
        output_height = height;
        for (auto& buffer : post_buffers)
        {
            buffer.allocate({width, height});
        }
    }

    void add_post(post_hook_t *hook)
    {
        post_effects.push_back(hook);
        output->render->damage_whole_idle();
    }

    void rem_post(post_hook_t *hook)
    {
        post_effects.remove_all(hook);
        output->render->damage_whole_idle();
    }

    /* Run all postprocessing effects, rendering to alternating buffers and
     * finally to the screen.
     *
     * NB: 2 buffers just aren't enough. We render to the zero buffer, and then
     * we alternately render to the second and the third. The reason: We track
     * damage. So, we need to keep the whole buffer each frame. */
    void run_post_effects()
    {
        int cur_idx = 0;
        post_effects.for_each([&] (auto post) -> void
        {
            int next_idx = 1 - cur_idx;
            wf::render_buffer_t dst_buffer = (post == post_effects.back() ?
                final_target : post_buffers[next_idx].get_renderbuffer());
            (*post)(post_buffers[cur_idx], dst_buffer);
            cur_idx = next_idx;
        });
    }

    wf::render_target_t get_target_framebuffer() const
    {
        wf::render_target_t fb{
            post_effects.size() > 0 ? post_buffers[default_out_buffer].get_renderbuffer() : final_target
        };

        fb.geometry     = output->get_relative_geometry();
        fb.wl_transform = output->handle->transform;
        fb.scale = output->handle->scale;

        return fb;
    }

    bool can_scanout() const
    {
        return post_effects.size() == 0;
    }
};

/**
 * Responsible for attaching depth buffers to framebuffers.
 * It keeps at most 3 depth buffers at any given time to conserve
 * resources.
 */
class depth_buffer_manager_t
{
  public:
    void ensure_depth_buffer(int fb, int width, int height)
    {
        /* If the backend doesn't have its own framebuffer, then the
         * framebuffer is created with a depth buffer. */
        if (required_counter <= 0)
        {
            return;
        }

        attach_buffer(fb, width, height);
    }

    void frame_done()
    {
        if (currently_attached_fb == INVALID_FB)
        {
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            // Detach depth buffer
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, currently_attached_fb));
            GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D, 0, 0));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        });

        currently_attached_fb = INVALID_FB;
    }

    void set_required(bool require)
    {
        required_counter += require ? 1 : -1;
        if (required_counter <= 0)
        {
            free_buffer();
        }
    }

    depth_buffer_manager_t() = default;

    ~depth_buffer_manager_t()
    {
        free_buffer();
    }

    depth_buffer_manager_t(const depth_buffer_manager_t &) = delete;
    depth_buffer_manager_t(depth_buffer_manager_t &&) = delete;
    depth_buffer_manager_t& operator =(const depth_buffer_manager_t&) = delete;
    depth_buffer_manager_t& operator =(depth_buffer_manager_t&&) = delete;

  private:
    int required_counter = 0;
    static constexpr int INVALID_FB  = 0;
    static constexpr int INVALID_TEX = 0;
    int currently_attached_fb = INVALID_FB;

    struct depth_buffer_t
    {
        GLuint tex = INVALID_TEX;
        int width  = 0;
        int height = 0;
    } buffer;

    void free_buffer()
    {
        currently_attached_fb = INVALID_FB;
        if (buffer.tex != INVALID_TEX)
        {
            wf::gles::run_in_context([&]
            {
                GL_CALL(glDeleteTextures(1, &buffer.tex));
                buffer.tex = INVALID_TEX;
            });
        }
    }

    void attach_buffer(int fb, int width, int height)
    {
        if ((buffer.width != width) || (buffer.height != height))
        {
            free_buffer();
            wf::gles::run_in_context_if_gles([&]
            {
                GL_CALL(glGenTextures(1, &buffer.tex));
                GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer.tex));
                GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                    width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL));
                GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            });

            buffer.width  = width;
            buffer.height = height;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer.tex));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fb));
            GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D, buffer.tex, 0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

            currently_attached_fb = fb;
        });
    }
};

static int64_t get_monotonic_time_ns()
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1'000'000'000ll + now.tv_nsec;
}

static int64_t timespec_to_ns(const timespec& timestamp)
{
    return timestamp.tv_sec * 1'000'000'000ll + timestamp.tv_nsec;
}

class wf::render_manager::impl
{
  public:
    struct render_timer_deleter_t
    {
        void operator ()(wlr_render_timer *timer) const
        {
            if (timer)
            {
                wlr_render_timer_destroy(timer);
            }
        }
    };

    using render_timer_ptr = std::unique_ptr<wlr_render_timer, render_timer_deleter_t>;

    struct pending_render_timer_t
    {
        uint32_t commit_seq;
        int64_t paint_started_ns;
        int64_t committed_ns;
        int64_t timer_started_ns;
        render_timer_ptr timer;
    };

    enum class render_timer_support_t
    {
        UNKNOWN,
        SUPPORTED,
        UNSUPPORTED,
    };

    struct color_transform_deleter_t
    {
        void operator ()(wlr_color_transform *transform) const
        {
            if (transform)
            {
                wlr_color_transform_unref(transform);
            }
        }
    };

    using color_transform_ptr =
        std::unique_ptr<wlr_color_transform, color_transform_deleter_t>;

    struct output_inverse_eotf_cache_t
    {
        color_transform_ptr transform;
        wlr_color_transfer_function tf;
        wlr_color_named_primaries primaries;
    };

    wf::wl_listener_wrapper on_frame, on_present, on_output_commit, on_renderer_destroy;
    wf::wl_high_resolution_timer repaint_timer;
    wf::wl_timer<false> vrr_idle_timer;

    output_t *output;
    wf::region_t swap_damage;
    std::unique_ptr<swapchain_damage_manager_t> damage_manager;
    std::unique_ptr<effect_hook_manager_t> effects;
    std::unique_ptr<postprocessing_manager_t> postprocessing;
    std::unique_ptr<depth_buffer_manager_t> depth_buffer_manager;
    adaptive_repaint_scheduler_t repaint_scheduler;
    std::deque<pending_render_timer_t> pending_render_timers;
    render_timer_support_t render_timer_support = render_timer_support_t::UNKNOWN;
    repaint_schedule_t last_schedule;

    wf::option_wrapper_t<int> min_render_budget;
    wf::option_wrapper_t<int> legacy_render_budget{"core/max_render_time"};
    wf::option_wrapper_t<bool> dynamic_repaint_delay{"workarounds/dynamic_repaint_delay"};
    wf::option_wrapper_t<int> vrr_idle_refresh_rate;
    bool legacy_render_budget_fallback_warned = false;
    bool legacy_render_budget_conflict_warned = false;

    wf::option_wrapper_t<wf::color_t> background_color_opt;
    std::unique_ptr<wf::render_pass_t> current_pass;
    wf::option_wrapper_t<std::string> icc_profile;
    wf::option_wrapper_t<bool> hdr;

    /**
     * The output color transform that matches the output's currently-committed image description.
     * For non-sRGB output primaries, this is a pipeline of [sRGB→output-primaries matrix,
     * inverse-EOTF]; otherwise it is just the inverse-EOTF. The wlroots Vulkan two-pass renderer
     * composites every add_texture into an sRGB-primaries FP16 blend image, so without the
     * primaries-conversion stage Rec.2020/PQ outputs would have sRGB-primaries values encoded
     * via PQ — which the display interprets as Rec.2020 primaries, producing oversaturated
     * colors. Cached so that it is not recreated each frame.
     */
    std::optional<output_inverse_eotf_cache_t> output_inverse_eotf_cache;
    color_transform_ptr icc_color_transform;

    /**
     * The transfer function the output expects in its committed image description, or sRGB if no
     * image description has been set.
     */
    wlr_color_transfer_function get_output_transfer_function()
    {
        if (output->handle->image_description)
        {
            return output->handle->image_description->transfer_function;
        }

        return WLR_COLOR_TRANSFER_FUNCTION_SRGB;
    }

    /**
     * The primaries the output expects in its committed image description, or sRGB if no
     * image description has been set.
     */
    wlr_color_named_primaries get_output_primaries()
    {
        if (output->handle->image_description && output->handle->image_description->primaries)
        {
            return output->handle->image_description->primaries;
        }

        return WLR_COLOR_NAMED_PRIMARIES_SRGB;
    }

    wlr_color_transform *get_output_inverse_eotf()
    {
        wlr_color_transfer_function tf = get_output_transfer_function();
        wlr_color_named_primaries prim = get_output_primaries();
        if (output_inverse_eotf_cache && (output_inverse_eotf_cache->tf == tf) &&
            (output_inverse_eotf_cache->primaries == prim))
        {
            return output_inverse_eotf_cache->transform.get();
        }

        output_inverse_eotf_cache.reset();

        wlr_color_transform *eotf = wlr_color_transform_init_linear_to_inverse_eotf(tf);
        if (!eotf)
        {
            LOGE("Failed to create inverse-EOTF transform for output ", output->to_string(),
                " (transfer function ", (int)tf, ")");
            return nullptr;
        }

        if (prim == WLR_COLOR_NAMED_PRIMARIES_SRGB)
        {
            output_inverse_eotf_cache = output_inverse_eotf_cache_t{color_transform_ptr{eotf}, tf, prim};
            return output_inverse_eotf_cache->transform.get();
        }

        wlr_color_primaries srgb_primaries{};
        wlr_color_primaries dst_primaries{};
        wlr_color_primaries_from_named(&srgb_primaries, WLR_COLOR_NAMED_PRIMARIES_SRGB);
        wlr_color_primaries_from_named(&dst_primaries, prim);
        float matrix[9];
        wlr_color_primaries_transform_absolute_colorimetric(&srgb_primaries, &dst_primaries, matrix);
        wlr_color_transform *mat = wlr_color_transform_init_matrix(matrix);
        if (!mat)
        {
            LOGE("Failed to create primaries-conversion matrix transform for output ",
                output->to_string());
            output_inverse_eotf_cache = output_inverse_eotf_cache_t{color_transform_ptr{eotf}, tf, prim};
            return output_inverse_eotf_cache->transform.get();
        }

        wlr_color_transform *stages[2] = {mat, eotf};
        wlr_color_transform *pipeline  = wlr_color_transform_init_pipeline(stages, 2);
        // init_pipeline references the stages; drop our own refs.
        wlr_color_transform_unref(mat);
        wlr_color_transform_unref(eotf);
        if (!pipeline)
        {
            LOGE("Failed to create color-transform pipeline for output ", output->to_string());
            return nullptr;
        }

        output_inverse_eotf_cache = output_inverse_eotf_cache_t{color_transform_ptr{pipeline}, tf, prim};
        return output_inverse_eotf_cache->transform.get();
    }

    wlr_color_transform *get_color_transform()
    {
        if (icc_color_transform)
        {
            return icc_color_transform.get();
        }

        return get_output_inverse_eotf();
    }

    wlr_drm_syncobj_timeline *render_timeline  = nullptr;
    wlr_drm_syncobj_timeline *release_timeline = nullptr;
    uint64_t render_point  = 0;
    uint64_t release_point = 0;

    impl(output_t *o) : output(o), env_allow_scanout(check_scanout_enabled())
    {
        damage_manager = std::make_unique<swapchain_damage_manager_t>(o);
        effects = std::make_unique<effect_hook_manager_t>();
        postprocessing = std::make_unique<postprocessing_manager_t>(o);
        depth_buffer_manager = std::make_unique<depth_buffer_manager_t>();
        if (output->handle->renderer)
        {
            on_renderer_destroy.set_callback([&] (void*)
            {
                pending_render_timers.clear();
                render_timer_support = render_timer_support_t::UNSUPPORTED;
                on_renderer_destroy.disconnect();
            });
            on_renderer_destroy.connect(&output->handle->renderer->events.destroy);
        }

        auto section = wf::get_core().config_backend->get_output_section(output->handle);
        min_render_budget.load_option(section, "min_render_budget");
        vrr_idle_refresh_rate.load_option(section, "vrr_idle_refresh_rate");
        vrr_idle_refresh_rate.set_callback([&] () { arm_vrr_idle_timer(); });

        on_present.set_callback([&] (void *data)
        {
            auto event = static_cast<wlr_output_event_present*>(data);
            uint32_t commit_seq = event->commit_seq;
#if WLR_HAS_X11_BACKEND
            // wlroots 0.20's X11 backend submits the sequence before the
            // generic output code increments it after a successful commit.
            if (wlr_output_is_x11(output->handle))
            {
                ++commit_seq;
            }

#endif
            consume_render_timer(commit_seq,
                event->flags & WLR_OUTPUT_PRESENT_HW_COMPLETION);
            repaint_scheduler.handle_presentation({
                    commit_seq,
                    event->presented,
                    timespec_to_ns(event->when),
                    event->refresh,
                    (event->flags & (WLR_OUTPUT_PRESENT_HW_CLOCK | WLR_OUTPUT_PRESENT_HW_COMPLETION)) ==
                    (WLR_OUTPUT_PRESENT_HW_CLOCK | WLR_OUTPUT_PRESENT_HW_COMPLETION),
                });
            if (event->presented)
            {
                arm_vrr_idle_timer();
            }
        });
        on_present.connect(&output->handle->events.present);

        on_output_commit.set_callback([&] (void *data)
        {
            auto event = static_cast<wlr_output_event_commit*>(data);
            constexpr uint32_t timing_changes = WLR_OUTPUT_STATE_MODE |
                WLR_OUTPUT_STATE_ENABLED | WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED |
                WLR_OUTPUT_STATE_RENDER_FORMAT;
            if (event->state->committed & timing_changes)
            {
                reset_repaint_timing(true);
                vrr_idle_timer.disconnect();
            }
        });
        on_output_commit.connect(&output->handle->events.commit);

        int drm_fd = wlr_backend_get_drm_fd(output->handle->backend);
        if ((drm_fd >= 0) && output->handle->backend->features.timeline &&
            output->handle->renderer && output->handle->renderer->features.timeline)
        {
            render_timeline  = wlr_drm_syncobj_timeline_create(drm_fd);
            release_timeline = wlr_drm_syncobj_timeline_create(drm_fd);
            if (!render_timeline || !release_timeline)
            {
                LOGE("Failed to create explicit synchronization timelines for ", output->to_string());
                wlr_drm_syncobj_timeline_unref(render_timeline);
                wlr_drm_syncobj_timeline_unref(release_timeline);
                render_timeline = release_timeline = nullptr;
            }
        }

        on_frame.set_callback([&] (void*)
        {
            consume_available_render_timers();
            const int64_t frame_arrived_ns = get_monotonic_time_ns();

            /* If the session is not active, don't paint.
             * This is the case when e.g. switching to another tty */
            if (wf::get_core().session && !wf::get_core().session->active)
            {
                reset_repaint_timing(false);
                vrr_idle_timer.disconnect();
                return;
            }

            const bool vrr_enabled = output->handle->adaptive_sync_status ==
                WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED;
            const int min_render_budget_ms = get_min_render_budget();
            auto schedule = repaint_scheduler.schedule_frame(frame_arrived_ns,
                min_render_budget_ms, dynamic_repaint_delay, vrr_enabled);

            // Leave a bit of time for clients to render, see
            // https://github.com/swaywm/sway/pull/4588
            if (schedule.repaint_deadline_ns <= 0)
            {
                output->handle->frame_pending = false;
                paint(schedule);
            } else
            {
                output->handle->frame_pending = true;
                if (!repaint_timer.set_deadline(schedule.repaint_deadline_ns, [=] ()
                {
                    output->handle->frame_pending = false;
                    paint(schedule);
                }))
                {
                    output->handle->frame_pending = false;
                    paint(schedule);
                }
            }

            frame_done_signal ev;
            output->emit(&ev);
            arm_vrr_idle_timer();
        });

        on_frame.connect(&output->handle->events.frame);

        background_color_opt.load_option("core/background_color");
        background_color_opt.set_callback([=] ()
        {
            damage_manager->damage_whole_idle();
        });

        damage_manager->schedule_repaint();

        icc_profile.load_option(section, "icc_profile");
        icc_profile.set_callback([=] ()
        {
            reload_icc_profile();
            damage_manager->damage_whole_idle();
        });
        hdr.load_option(section, "hdr");
        hdr.set_callback([=] ()
        {
            // Drop the cached output color transform: by the time the next frame is rendered,
            // the output's image_description will have been re-committed by output-layout, and
            // get_output_inverse_eotf() will lazily regenerate the transform to match.
            output_inverse_eotf_cache.reset();

            damage_manager->damage_whole_idle();
        });

        reload_icc_profile();
    }

    int get_min_render_budget()
    {
        const int configured_budget = min_render_budget;
        const int fallback_budget   = legacy_render_budget;
        if (configured_budget >= 0)
        {
            if ((fallback_budget >= 0) && !legacy_render_budget_conflict_warned)
            {
                legacy_render_budget_conflict_warned = true;
                LOGW("Both min_render_budget and deprecated core/max_render_time are set for output ",
                    output->to_string(), "; using min_render_budget.");
            }

            return configured_budget;
        }

        if ((fallback_budget != -1) && !legacy_render_budget_fallback_warned)
        {
            legacy_render_budget_fallback_warned = true;
            LOGW("Using deprecated core/max_render_time for output ", output->to_string(),
                " because min_render_budget is -1.");
        }

        return fallback_budget;
    }

    static render_debug_path_t convert_path(repaint_path_t path)
    {
        return path == repaint_path_t::DIRECT_SCANOUT ?
               render_debug_path_t::DIRECT_SCANOUT : render_debug_path_t::COMPOSED;
    }

    render_debug_info_t get_debug_info()
    {
        const int effective_budget = get_min_render_budget();
        const auto scheduler_info  = repaint_scheduler.get_debug_info(effective_budget);

        render_timer_debug_support_t timer_support = render_timer_debug_support_t::UNKNOWN;
        switch (render_timer_support)
        {
          case render_timer_support_t::SUPPORTED:
            timer_support = render_timer_debug_support_t::SUPPORTED;
            break;

          case render_timer_support_t::UNSUPPORTED:
            timer_support = render_timer_debug_support_t::UNSUPPORTED;
            break;

          case render_timer_support_t::UNKNOWN:
            break;
        }

        return {
            .min_render_budget_ms  = effective_budget,
            .dynamic_repaint_delay = dynamic_repaint_delay,
            .vrr_enabled = output->handle->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED,
            .vrr_idle_refresh_rate   = vrr_idle_refresh_rate,
            .last_scheduled_delay_ns = last_schedule.delay_ns,
            .has_last_target_presentation = last_schedule.target_presentation_ns.has_value(),
            .last_target_presentation_ns  = last_schedule.target_presentation_ns.value_or(0),
            .predicted_path = convert_path(last_schedule.predicted_path),
            .has_last_presentation = scheduler_info.has_last_presentation,
            .last_presentation_ns  = scheduler_info.last_presentation_ns,
            .refresh_ns = scheduler_info.refresh_ns,
            .pending_scheduler_frames = scheduler_info.pending_frames,
            .consecutive_scanouts     = scheduler_info.consecutive_scanouts,
            .composed = scheduler_info.composed,
            .direct_scanout = scheduler_info.direct_scanout,
            .render_timer_support  = timer_support,
            .pending_render_timers = static_cast<uint32_t>(pending_render_timers.size()),
            .output_frame_pending  = output->handle->frame_pending,
            .output_needs_frame    = output->handle->needs_frame,
            .repaint_pending = damage_manager->should_repaint(),
        };
    }

    void reset_repaint_timing(bool reset_estimates)
    {
        pending_render_timers.clear();
        repaint_scheduler.reset(reset_estimates);
    }

    render_timer_ptr create_render_timer()
    {
        if (!dynamic_repaint_delay || (get_min_render_budget() < 0) ||
            (render_timer_support == render_timer_support_t::UNSUPPORTED) ||
            !output->handle->renderer || !wf::get_core().is_gles2())
        {
            return {};
        }

        render_timer_ptr timer{wlr_render_timer_create(output->handle->renderer)};
        if (!timer)
        {
            render_timer_support = render_timer_support_t::UNSUPPORTED;
            return {};
        }

        render_timer_support = render_timer_support_t::SUPPORTED;
        return timer;
    }

    void track_render_timer(uint32_t commit_seq, int64_t paint_started_ns,
        int64_t committed_ns, int64_t timer_started_ns, render_timer_ptr timer)
    {
        if (!timer)
        {
            return;
        }

        pending_render_timers.push_back({commit_seq, paint_started_ns,
            committed_ns, timer_started_ns, std::move(timer)});
        constexpr size_t MAX_PENDING_RENDER_TIMERS = 16;
        if (pending_render_timers.size() > MAX_PENDING_RENDER_TIMERS)
        {
            pending_render_timers.pop_front();
        }
    }

    void consume_render_timer_at(std::deque<pending_render_timer_t>::iterator pending)
    {
        const int timer_duration_ns = wlr_render_timer_get_duration_ns(pending->timer.get());
        if (timer_duration_ns >= 0)
        {
            const int64_t cpu_duration_ns = std::max<int64_t>(0,
                pending->committed_ns - pending->paint_started_ns);
            const int64_t render_completion_ns = std::max<int64_t>(0,
                pending->timer_started_ns - pending->paint_started_ns) + timer_duration_ns;
            repaint_scheduler.observe_render_completion(pending->commit_seq,
                repaint_path_t::COMPOSED,
                std::max(cpu_duration_ns, render_completion_ns));
        }

        pending_render_timers.erase(pending);
    }

    void consume_render_timer(uint32_t commit_seq, bool completion_known)
    {
        auto pending = std::find_if(pending_render_timers.begin(),
            pending_render_timers.end(), [=] (const auto& candidate)
        {
            return candidate.commit_seq == commit_seq;
        });
        if ((pending == pending_render_timers.end()) || !completion_known)
        {
            return;
        }

        consume_render_timer_at(pending);
    }

    void consume_available_render_timers()
    {
        // A synthetic present event does not prove GPU completion. Defer its
        // one nonblocking query until a later backend frame. Since wlroots uses
        // -1 for both unavailable and invalid timers, never query one twice.
        while (!pending_render_timers.empty())
        {
            if ((get_monotonic_time_ns() - pending_render_timers.front().committed_ns) < 1'000'000)
            {
                break;
            }

            consume_render_timer_at(pending_render_timers.begin());
        }
    }

    void arm_vrr_idle_timer()
    {
        vrr_idle_timer.disconnect();
        const int refresh_rate = vrr_idle_refresh_rate;
        if ((refresh_rate <= 0) || !output->handle->enabled ||
            (output->handle->adaptive_sync_status != WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED))
        {
            return;
        }

        const uint32_t timeout_ms = std::max(1, (1000 + refresh_rate - 1) / refresh_rate);
        vrr_idle_timer.set_timeout(timeout_ms, [=] () { handle_vrr_idle_timeout(); });
    }

    void handle_vrr_idle_timeout()
    {
        if (!output->handle->enabled ||
            (output->handle->adaptive_sync_status != WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED))
        {
            return;
        }

        damage_manager->schedule_vrr_keepalive();
    }

    wlr_buffer_pass_options pass_opts{};

    void reload_icc_profile()
    {
        if (icc_profile.value().empty())
        {
            set_icc_transform(nullptr);
            return;
        }

        if (!wf::get_core().is_vulkan())
        {
            LOGW("ICC profiles in core are only supported with the vulkan renderer. "
                 "For GLES2, make sure to enable the vk-color-management plugin.");
        }

        auto path = std::filesystem::path{icc_profile.value()};
        if (std::filesystem::is_regular_file(path))
        {
            // Read binary file into vector<char> buffer
            std::ifstream file(icc_profile.value(), std::ios::binary);
            std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());

            auto transform = wlr_color_transform_init_linear_to_icc(buffer.data(), buffer.size());
            if (!transform)
            {
                LOGE("Failed to load ICC transform from ", icc_profile.value());
                set_icc_transform(nullptr);
                return;
            } else
            {
                LOGI("Loaded ICC transform from ", icc_profile.value(), " for output ", output->to_string());
            }

            set_icc_transform(transform);
        }
    }

    void set_icc_transform(wlr_color_transform *transform)
    {
        icc_color_transform.reset(transform);
    }

    ~impl()
    {
        pending_render_timers.clear();
        set_icc_transform(nullptr);
        output_inverse_eotf_cache.reset();
        if (render_timeline)
        {
            wlr_drm_syncobj_timeline_signal(render_timeline, UINT64_MAX);
            wlr_drm_syncobj_timeline_unref(render_timeline);
        }

        if (release_timeline)
        {
            wlr_drm_syncobj_timeline_signal(release_timeline, UINT64_MAX);
            wlr_drm_syncobj_timeline_unref(release_timeline);
        }
    }

    wf::explicit_sync_point_t next_release_point()
    {
        if (!release_timeline)
        {
            return {};
        }

        return {release_timeline, ++release_point};
    }

    wf::explicit_sync_point_t next_render_completion_point()
    {
        if (!render_timeline)
        {
            return {};
        }

        return {render_timeline, ++render_point};
    }

    const bool env_allow_scanout;
    static bool check_scanout_enabled()
    {
        const char *env_scanout = getenv("WAYFIRE_DISABLE_DIRECT_SCANOUT");
        bool env_allow_scanout  = (env_scanout == nullptr) || (!strcmp(env_scanout, "0"));
        if (!env_allow_scanout)
        {
            LOGC(SCANOUT, "Scanout disabled by environment variable.");
        }

        return env_allow_scanout;
    }

    int output_inhibit_counter = 0;
    void add_inhibit(bool add)
    {
        output_inhibit_counter += add ? 1 : -1;
        if (output_inhibit_counter == 0)
        {
            damage_manager->damage_whole_idle();

            wf::output_start_rendering_signal data;
            data.output = output;
            output->emit(&data);
        }
    }

    /* Actual rendering functions */

    /**
     * Try to directly scanout a view on the output, thereby skipping rendering
     * entirely.
     *
     * @return The committed output sequence if scanout succeeded.
     */
    std::optional<uint32_t> do_direct_scanout()
    {
        const bool can_scanout = !output_inhibit_counter && effects->can_scanout() &&
            postprocessing->can_scanout() && wlr_output_is_direct_scanout_allowed(output->handle) &&
            (icc_color_transform == nullptr);

        if (!can_scanout || !env_allow_scanout)
        {
            return {};
        }

        auto result = scene::try_scanout_from_list(
            damage_manager->instance_manager->get_instances(), output);
        if (result == scene::direct_scanout::SUCCESS)
        {
            return output->handle->commit_seq;
        }

        return {};
    }

    /**
     * Return the swap damage if called from overlay or postprocessing
     * effect callbacks or empty region otherwise.
     */
    wf::region_t get_swap_damage()
    {
        return swap_damage;
    }

    /**
     * Render an output. Either calls the built-in renderer, or the render hook
     * of a plugin
     *
     * @return The swap damage in buffer-local coordinates.
     */
    wf::region_t start_output_pass(
        std::unique_ptr<swapchain_damage_manager_t::frame_object_t>& next_frame)
    {
        render_pass_params_t params;
        params.instances = &damage_manager->instance_manager->get_instances();

        params.target = postprocessing->get_target_framebuffer().translated(
            wf::origin(output->get_layout_geometry()));
        params.target.set_color_transform(get_color_transform(), get_output_transfer_function());
        pass_opts.color_transform = get_color_transform();

        params.damage = damage_manager->get_scheduled_damage(params.target);

        params.background_color = background_color_opt;
        params.reference_output = this->output;
        params.renderer = output->handle->renderer;
        params.flags    = RPASS_CLEAR_BACKGROUND | RPASS_EMIT_SIGNALS;

        pass_opts.timer    = nullptr;
        params.pass_opts   = std::move(pass_opts);
        this->current_pass = std::make_unique<render_pass_t>(params);

        auto total_damage = current_pass->run_partial();
        if (runtime_config.damage_debug)
        {
            /* Clear the screen to yellow, so that the repainted parts are visible */
            wf::regionf_t yellow = params.target.geometry;
            yellow ^= total_damage;

            total_damage |= params.target.geometry;
            current_pass->clear(yellow, {1, 1, 0, 1});
        }

        // Transform to buffer-local damage
        auto framebuffer_damage = params.target.framebuffer_region_from_geometry_region(total_damage);
        framebuffer_damage &= damage_manager->get_buffer_extents();
        return framebuffer_damage;
    }

    void update_bound_output(wlr_buffer *buffer)
    {
        /* Make sure the default buffer has enough size */
        postprocessing->allocate(output->handle->width, output->handle->height);
        postprocessing->set_current_buffer(buffer);

        if (wf::get_core().is_gles2())
        {
            const auto& default_fb = postprocessing->get_target_framebuffer();
            GLuint default_fb_id   = gles::ensure_render_buffer_fb_id(default_fb);
            depth_buffer_manager->ensure_depth_buffer(default_fb_id,
                default_fb.get_size().width, default_fb.get_size().height);
        }
    }

    void unset_bound_output()
    {
        depth_buffer_manager->frame_done();
        postprocessing->set_current_buffer(nullptr);
    }

    /**
     * Repaints the whole output, includes all effects and hooks
     */
    void paint(const repaint_schedule_t& schedule)
    {
        const int64_t paint_started_ns = get_monotonic_time_ns();
        /* Part 1: frame setup: query damage, etc. */
        effects->run_effects(OUTPUT_EFFECT_PRE);
        effects->run_effects(OUTPUT_EFFECT_DAMAGE);

        // Optimization: the output doesn't need a new frame, so we can
        // just skip the whole repaint
        if (!damage_manager->should_repaint())
        {
            return;
        }

        const uint64_t frame_generation = damage_manager->get_request_generation();
        if (auto commit_seq = do_direct_scanout())
        {
            const int64_t submitted_ns = get_monotonic_time_ns();
            damage_manager->frame_committed(frame_generation);
            last_schedule = schedule;
            repaint_scheduler.submit_frame(schedule, repaint_path_t::DIRECT_SCANOUT,
                paint_started_ns, submitted_ns, *commit_seq,
                output->handle->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED);
            // Yet another optimization: if we can directly scanout, we should
            // stop the rest of the repaint cycle.
            return;
        }

        auto next_frame = damage_manager->start_frame();
        if (!next_frame)
        {
            damage_manager->damage_whole();
            return;
        }

        /* Part 2: call the renderer, which sets swap_damage and draws the scenegraph */
        update_bound_output(next_frame->buffer);
        render_timer_ptr render_timer;
        int64_t timer_started_ns = 0;
        this->swap_damage = start_output_pass(next_frame);

        /* Part 3: overlay effects */
        effects->run_effects(OUTPUT_EFFECT_OVERLAY);
        if (output_inhibit_counter)
        {
            current_pass->clear(current_pass->get_target().geometry, {0, 0, 0, 1});
        }

        /* Part 4: we are done with the main scene. Submit the main render pass. */
        const bool pass_status = current_pass->submit();
        current_pass.reset();
        if (!pass_status)
        {
            LOGE("Failed to submit render pass!");
            wlr_buffer_unlock(next_frame->buffer);
            unset_bound_output();
            swap_damage.clear();
            damage_manager->damage_whole();
            return;
        }

        effects->run_effects(OUTPUT_EFFECT_PASS_DONE);

        /* Part 5: finalize the scene: postprocessing effects */
        if (postprocessing->post_effects.size())
        {
            swap_damage |= damage_manager->get_buffer_extents();
        }

        postprocessing->run_post_effects();

        // GLES render timers include earlier work queued in the context, so a
        // final marker pass measures completion of scene, postprocessing and cursors.
        render_timer = create_render_timer();
        if (render_timer)
        {
            timer_started_ns = get_monotonic_time_ns();
        }

        /* Part 6: render sw cursors We render software cursors after everything else
         * for consistency with hardware cursor planes */
        if (!render_sw_cursors(next_frame.get(), render_timer.get()))
        {
            wlr_buffer_unlock(next_frame->buffer);
            unset_bound_output();
            swap_damage.clear();
            damage_manager->damage_whole();
            return;
        }

        /* Part 7: finalize frame: swap buffers, send frame_done, etc */
        const bool committed = damage_manager->swap_buffers(std::move(next_frame), swap_damage);
        const int64_t committed_ns = get_monotonic_time_ns();

        unset_bound_output();
        swap_damage.clear();
        if (!committed)
        {
            damage_manager->damage_whole();
            return;
        }

        damage_manager->frame_committed(frame_generation);
        last_schedule = schedule;
        repaint_scheduler.submit_frame(schedule, repaint_path_t::COMPOSED,
            paint_started_ns, committed_ns, output->handle->commit_seq,
            output->handle->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED);
        track_render_timer(output->handle->commit_seq, paint_started_ns,
            committed_ns, timer_started_ns, std::move(render_timer));
        post_paint();
    }

    bool render_sw_cursors(swapchain_damage_manager_t::frame_object_t *next_frame,
        wlr_render_timer *timer)
    {
        if (swap_damage.empty() && !render_timeline && !timer)
        {
            return true;
        }

        wlr_buffer_pass_options pass_options{};
        pass_options.timer = timer;
        pass_options.color_transform = get_color_transform();
        if (render_timeline)
        {
            pass_options.signal_timeline = render_timeline;
            pass_options.signal_point    = ++render_point;
        }

        auto *sw_cursor_pass = wlr_renderer_begin_buffer_pass(
            output->handle->renderer, next_frame->buffer, &pass_options);
        if (!sw_cursor_pass)
        {
            LOGE("Failed to render software cursors!");
            return false;
        }

        const auto output_tf = get_output_transfer_function();
        const float luminance_multiplier = wf::compute_luminance_multiplier(
            WLR_COLOR_TRANSFER_FUNCTION_GAMMA22, output_tf);

        wlr_color_primaries srgb_primaries{};
        wlr_color_primaries_from_named(&srgb_primaries, WLR_COLOR_NAMED_PRIMARIES_SRGB);

        int transformed_width, transformed_height;
        wlr_output_transformed_resolution(output->handle, &transformed_width, &transformed_height);

        wlr_output_cursor *cursor;
        wl_list_for_each(cursor, &output->handle->cursors, link)
        {
            if (!cursor->enabled || !cursor->visible ||
                (output->handle->hardware_cursor == cursor) || !cursor->texture)
            {
                continue;
            }

            // wlr_output_cursor stores x/y/width/height/hotspot in scaled
            // buffer-pixel units, pre-output-transform (see wlr_output_cursor_move
            // and wlr_output_cursor_set_buffer in wlroots). Mirror the wlroots
            // helper: build the integer fb-coord box, then apply the inverse
            // output transform so the final dst_box is in framebuffer pixels.
            wlr_box box{
                static_cast<int>(cursor->x - cursor->hotspot_x),
                static_cast<int>(cursor->y - cursor->hotspot_y),
                static_cast<int>(cursor->width),
                static_cast<int>(cursor->height),
            };
            wlr_box_transform(&box, &box,
                wlr_output_transform_invert(output->handle->transform),
                transformed_width, transformed_height);

            wf::region_t cursor_damage{box};
            cursor_damage &= swap_damage;
            if (cursor_damage.empty())
            {
                continue;
            }

            // Tag the cursor as sRGB-primaries / gamma2.2 — same treatment
            // wlr_surface_node gives regular surfaces — so the wlroots renderer
            // applies the primaries conversion and SDR→PQ luminance multiplier
            // needed for correct compositing on HDR (PQ) outputs.
            wlr_render_texture_options opts{};
            opts.texture = cursor->texture;
            opts.src_box = cursor->src_box;
            opts.dst_box = box;
            opts.clip    = cursor_damage.to_pixman();
            opts.transform     = output->handle->transform;
            opts.wait_timeline = cursor->wait_timeline;
            opts.wait_point    = cursor->wait_point;
            opts.transfer_function = WLR_COLOR_TRANSFER_FUNCTION_GAMMA22;
            opts.primaries = &srgb_primaries;
            if (luminance_multiplier != 1.0f)
            {
                opts.luminance_multiplier = &luminance_multiplier;
            }

            wlr_render_pass_add_texture(sw_cursor_pass, &opts);
        }

        if (!wlr_render_pass_submit(sw_cursor_pass))
        {
            LOGE("Failed to submit software cursor render pass!");
            return false;
        }

        if (render_timeline)
        {
            wlr_output_state_set_wait_timeline(&next_frame->state, render_timeline, render_point);
        }

        return true;
    }

    /**
     * Execute post-paint actions.
     */
    void post_paint()
    {
        effects->run_effects(OUTPUT_EFFECT_POST);
        if (damage_manager->constant_redraw_counter)
        {
            damage_manager->schedule_repaint();
        }
    }
};

scene::direct_scanout scene::try_scanout_from_list(
    const std::vector<scene::render_instance_uptr>& instances,
    wf::output_t *scanout)
{
    for (auto& ch : instances)
    {
        auto res = ch->try_scanout(scanout);
        if (res != direct_scanout::SKIP)
        {
            return res;
        }
    }

    return direct_scanout::SKIP;
}

void scene::compute_visibility_from_list(const std::vector<render_instance_uptr>& instances,
    wf::output_t *output, wf::regionf_t& region, const wf::pointf_t& offset)
{
    region -= offset;
    for (auto& ch : instances)
    {
        ch->compute_visibility(output, region);
    }

    region += offset;
}

render_manager::render_manager(output_t *o) :
    pimpl(new impl(o))
{}
render_manager::~render_manager() = default;

void render_manager::set_redraw_always(bool always)
{
    pimpl->damage_manager->set_redraw_always(always);
}

wf::region_t render_manager::get_swap_damage()
{
    return pimpl->get_swap_damage();
}

void render_manager::schedule_redraw()
{
    pimpl->damage_manager->schedule_repaint();
}

void render_manager::add_inhibit(bool add)
{
    pimpl->add_inhibit(add);
}

void render_manager::add_effect(effect_hook_t *hook, output_effect_type_t type)
{
    pimpl->effects->add_effect(hook, type);
}

void render_manager::rem_effect(effect_hook_t *hook)
{
    pimpl->effects->rem_effect(hook);
}

void render_manager::add_post(post_hook_t *hook)
{
    pimpl->postprocessing->add_post(hook);
}

void render_manager::rem_post(post_hook_t *hook)
{
    pimpl->postprocessing->rem_post(hook);
}

wf::regionf_t render_manager::get_scheduled_damage()
{
    return pimpl->damage_manager->get_scheduled_damage(get_target_framebuffer());
}

void render_manager::damage_whole()
{
    pimpl->damage_manager->damage_whole();
}

void render_manager::damage_whole_idle()
{
    pimpl->damage_manager->damage_whole_idle();
}

void render_manager::damage(const wf::geometry_t& box, bool repaint)
{
    auto fb = pimpl->postprocessing->get_target_framebuffer();
    pimpl->damage_manager->damage_buffer(fb.framebuffer_box_from_geometry_box(box), repaint);
}

void render_manager::damage(const wf::regionf_t& region, bool repaint)
{
    auto fb = pimpl->postprocessing->get_target_framebuffer();
    pimpl->damage_manager->damage_buffer(fb.framebuffer_region_from_geometry_region(region), repaint);
}

wlr_color_transform*render_manager::get_color_transform()
{
    return pimpl->get_color_transform();
}

wf::render_target_t render_manager::get_target_framebuffer() const
{
    return pimpl->postprocessing->get_target_framebuffer();
}

void render_manager::set_require_depth_buffer(bool require)
{
    return pimpl->depth_buffer_manager->set_required(require);
}

wf::explicit_sync_point_t render_manager::next_explicit_sync_release_point()
{
    return pimpl->next_release_point();
}

wf::explicit_sync_point_t render_manager::next_explicit_sync_render_point()
{
    return pimpl->next_render_completion_point();
}

render_debug_info_t render_manager::get_debug_info() const
{
    return pimpl->get_debug_info();
}

wf::render_pass_t*render_manager::get_current_pass()
{
    return pimpl->current_pass.get();
}

void priv_render_manager_clear_instances(wf::render_manager *manager)
{
    manager->pimpl->damage_manager->instance_manager.reset();
}

void priv_render_manager_start_rendering(wf::render_manager *manager)
{
    manager->pimpl->damage_manager->start_rendering();
}
} // namespace wf

/* End render_manager */

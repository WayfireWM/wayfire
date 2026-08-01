#pragma once

#include <wayfire/render.hpp>
#include <wayfire/output.hpp>
#include <wayfire/object.hpp>
#include <wayfire/region.hpp>
#include <cstdint>

namespace wf
{
/* Effect hooks provide the plugins with a way to execute custom code
 * at certain parts of the repaint cycle */
using effect_hook_t = std::function<void ()>;

enum output_effect_type_t
{
    /* Pre hooks are called before starting to repaint the output */
    OUTPUT_EFFECT_PRE       = 0,
    /**
     * Damage hooks are called before attaching the renderer to the output.
     * They are useful if the output damage needs to be modified, whereas
     * plugins that simply need to update their animation should use PRE hooks.
     */
    OUTPUT_EFFECT_DAMAGE    = 1,
    /* Overlay hooks are called right after repainting the output, but
     * before post hooks and before swapping buffers */
    OUTPUT_EFFECT_OVERLAY   = 2,
    /**
     * Pass done hooks are called after overlay hooks are all finished.
     * At this point, the main render pass for the output is over.
     */
    OUTPUT_EFFECT_PASS_DONE = 3,
    /* Post hooks are called after the buffers have been swapped */
    OUTPUT_EFFECT_POST      = 4,
    /* Invalid type for a hook, used internally */
    OUTPUT_EFFECT_TOTAL     = 5,
};

enum class render_debug_path_t
{
    COMPOSED,
    DIRECT_SCANOUT,
};

enum class render_timer_debug_support_t
{
    UNKNOWN,
    SUPPORTED,
    UNSUPPORTED,
};

struct render_path_debug_info_t
{
    /** Learned render duration including the scheduler sample margin, in nanoseconds. */
    int64_t paint_budget_ns = 0;
    /** Adaptive missed-deadline safety margin, in nanoseconds. */
    int64_t miss_guard_ns = 0;
    /** Effective render budget after applying the configured floor, in nanoseconds. */
    int64_t total_budget_ns = 0;
    /** Consecutive on-time correlated presentations since the last miss. */
    uint32_t successful_presentations = 0;
};

/** Snapshot returned by render_manager::get_debug_info(). Durations are nanoseconds unless noted. */
struct render_debug_info_t
{
    /** Effective per-output min_render_budget in milliseconds, including legacy fallback resolution. */
    int min_render_budget_ms = -1;
    /** Current workarounds/dynamic_repaint_delay value. */
    bool dynamic_repaint_delay = false;
    /** Whether adaptive sync is currently enabled on the output. */
    bool vrr_enabled = false;
    /** Configured VRR idle keepalive rate in Hz; zero disables it. */
    int vrr_idle_refresh_rate = 0;

    /** Delay used by the most recent successfully committed repaint. */
    int64_t last_scheduled_delay_ns = 0;
    /** Whether last_target_presentation_ns is valid. */
    bool has_last_target_presentation = false;
    /** Predicted timestamp for the most recent successfully committed repaint. */
    int64_t last_target_presentation_ns = 0;
    /** Render path predicted for the most recent successfully committed repaint. */
    render_debug_path_t predicted_path = render_debug_path_t::COMPOSED;

    /** Whether last_presentation_ns contains a usable presentation anchor. */
    bool has_last_presentation = false;
    /** Most recent usable presentation timestamp. */
    int64_t last_presentation_ns = 0;
    /** Nominal output refresh period. */
    int64_t refresh_ns = 0;
    /** Submitted frames waiting for presentation correlation. */
    uint32_t pending_scheduler_frames = 0;
    /** Number of consecutive direct-scanout presentations. */
    uint32_t consecutive_scanouts = 0;

    render_path_debug_info_t composed;
    render_path_debug_info_t direct_scanout;

    /** Availability of renderer GPU timestamp queries. */
    render_timer_debug_support_t render_timer_support = render_timer_debug_support_t::UNKNOWN;
    /** GPU timer samples waiting to be consumed. */
    uint32_t pending_render_timers = 0;

    /** wlroots reports an outstanding output frame. */
    bool output_frame_pending = false;
    /** wlroots reports that the output requires a commit. */
    bool output_needs_frame = false;
    /** Wayfire damage tracking currently requires repainting. */
    bool repaint_pending = false;
};

/** Post hooks are called just before swapping buffers. In contrast to
 * render hooks, post hooks operate on the whole output image, i.e they
 * are suitable for different postprocessing effects.
 *
 * When using post hooks, the output first gets rendered to a framebuffer,
 * which can then pass through multiple post hooks. The last hook then will
 * draw to the output's framebuffer.
 *
 * @param source Indicates the source buffer of the hook, which contains
 *        the output image up to this moment.
 *
 * @param destination Indicates where the processed image should be stored.
 */
using post_hook_t = std::function<void (wf::auxilliary_buffer_t& source,
    const wf::render_buffer_t& destination)>;

/**
 * The frame-done signal is emitted on an output when the frame has been completed (regardless of whether new
 * content was painted or not).
 */
struct frame_done_signal
{};

/** Render manager
 *
 * Each output has a render manager, which is responsible for all rendering
 * operations that happen on it, and also for damage tracking. */
class render_manager
{
  public:
    /** Create a render manager for the given output. Plugins do not need
     * to manually create render managers, as one is created for each output
     * automatically */
    render_manager(output_t *o);
    ~render_manager();

    /**
     * Rendering an output is done on demand, that is, when the output is
     * damaged. Some plugins however need to redraw the output as often as
     * possible, for ex. when displaying some kind of animation.
     *
     * auto_redraw() provides the plugins to temporarily request redrawing
     * of the output regardless of damage.
     *
     * @param always - Whether to always redraw, regardless of damage. Call
     *        set_redraw_always(false) once for each set_redraw_always(true).
     */
    void set_redraw_always(bool always = true);

    /**
     * Schedule a frame for the output. Note that if there is no damage for
     * the next frame, nothing will be redrawn
     */
    void schedule_redraw();

    /**
     * Inhibit rendering to the output. An inhibited output will show a
     * fully black image. Used mainly for compositor fade in/out on startup.
     */
    void add_inhibit(bool add);

    /**
     * Add a new effect hook.
     * @param hook The hook callback
     * @param type The type of the effect hook
     */
    void add_effect(effect_hook_t *hook, output_effect_type_t type);
    /**
     * Remove an added effect hook. No-op if the hook wasn't really added.
     * @param hook The hook callback to be removed
     */
    void rem_effect(effect_hook_t *hook);

    /**
     * Add a new post hook.
     *
     * @param hook The hook callback
     */
    void add_post(post_hook_t *hook);

    /**
     * Remove a post hook. No-op if hook isn't active.
     *
     * @param hook The hook to be removed.
     */
    void rem_post(post_hook_t *hook);

    /**
     * @return The damaged region on the current output for the current
     * frame that is used when swapping buffers. This function should
     * only be called from overlay or postprocessing effect callbacks.
     * Otherwise it will return an empty region.
     */
    wf::region_t get_swap_damage();

    /**
     * @return The current render pass, NULL if no rendering operations are currently active on the output.
     */
    wf::render_pass_t *get_current_pass();

    /**
     * @return The damaged region on the current output for the current
     * frame. Note that a larger region might actually be repainted due to
     * double buffering.
     */
    wf::regionf_t get_scheduled_damage();

    /**
     * @return The current wlr_color_transform from the icc_profile option, or NULL if none is set.
     */
    wlr_color_transform *get_color_transform();

    /**
     * Damage all workspaces of the output. Should not be used inside render
     * hooks, view transformers, etc.
     */
    void damage_whole();

    /**
     * Same as damage_whole() but the output will actually be damaged on the
     * next time the event loop goes idle. This is safe to use inside render
     * hooks, transformers, etc.
     */
    void damage_whole_idle();

    /**
     * Same as damage_whole(), but damages only a part of the output.
     *
     * @param box The output box to be damaged, in output-local coordinates.
     * @param repaint Whether to automatically schedule an output repaint.
     */
    void damage(const wf::geometry_t& box, bool repaint = true);

    /**
     * Same as damage_whole(), but damages only a part of the output.
     *
     * @param region The output region to be damaged, in output-local coordinates.
     * @param repaint Whether to automatically schedule an output repaint.
     */
    void damage(const wf::regionf_t& region, bool repaint = true);

    /**
     * @return The framebuffer on which all rendering operations except post
     * effects happen.
     */
    wf::render_target_t get_target_framebuffer() const;

    /**
     * Inform Wayfire whether a depth buffer is required for rendering on the default framebuffer for each
     * output.
     */
    void set_require_depth_buffer(bool require);

    /** Reserve the next point on the output backend-release timeline. */
    wf::explicit_sync_point_t next_explicit_sync_release_point();

    /** Reserve the next point on the output render-completion timeline. */
    wf::explicit_sync_point_t next_explicit_sync_render_point();

    /** Snapshot repaint scheduling state for debugging. */
    render_debug_info_t get_debug_info() const;

  public:
    class impl;
    std::unique_ptr<impl> pimpl;
};
}

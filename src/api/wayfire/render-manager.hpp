#pragma once

#include "wayfire/opengl.hpp"
#include <wayfire/output.hpp>
#include <wayfire/object.hpp>
#include <wayfire/region.hpp>

namespace wf
{
/* Effect hooks provide the plugins with a way to execute custom code
 * at certain parts of the repaint cycle */
using effect_hook_t = std::function<void ()>;

enum output_effect_type_t
{
    /* Pre hooks are called before starting to repaint the output */
    OUTPUT_EFFECT_PRE     = 0,
    /**
     * Damage hooks are called before attaching the renderer to the output.
     * They are useful if the output damage needs to be modified, whereas
     * plugins that simply need to update their animation should use PRE hooks.
     */
    OUTPUT_EFFECT_DAMAGE  = 1,
    /* Overlay hooks are called right after repainting the output, but
     * before post hooks and before swapping buffers */
    OUTPUT_EFFECT_OVERLAY = 2,
    /* Post hooks are called after the buffers have been swapped */
    OUTPUT_EFFECT_POST    = 3,
    /* Invalid type for a hook, used internally */
    OUTPUT_EFFECT_TOTAL   = 4,
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
using post_hook_t = std::function<void (const wf::framebuffer_t& source,
    const wf::framebuffer_t& destination)>;

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
     * @return The damaged region on the current output for the current
     * frame. Note that a larger region might actually be repainted due to
     * double buffering.
     */
    wf::region_t get_scheduled_damage();

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
    void damage(const wlr_box& box, bool repaint = true);

    /**
     * Same as damage_whole(), but damages only a part of the output.
     *
     * @param region The output region to be damaged, in output-local coordinates.
     * @param repaint Whether to automatically schedule an output repaint.
     */
    void damage(const wf::region_t& region, bool repaint = true);

    /**
     * @return A box in output-local coordinates containing the given
     * workspace of the output (returned value depends on current workspace).
     */
    wlr_box get_ws_box(wf::point_t ws) const;

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

  public:
    class impl;
    std::unique_ptr<impl> pimpl;
};
}

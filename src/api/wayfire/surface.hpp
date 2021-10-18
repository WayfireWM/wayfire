#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/nonstd/observer_ptr.h>
#include <wayfire/geometry.hpp>
#include <wayfire/object.hpp>

namespace wf
{
class output_t;
class surface_interface_t;
struct framebuffer_t;
struct region_t;

/**
 * A surface and its position on the screen.
 */
struct surface_iterator_t
{
    /** The surface */
    surface_interface_t *surface;
    /**
     * The position of the surface relative to the topmost surface in the
     * surface tree
     */
    wf::point_t position;
};

/**
 * The input side of a surface.
 * It is responsible for taking the raw events from core and forwarding them
 * to the client or processing them.
 */
class input_surface_t
{
  protected:
    input_surface_t(const input_surface_t&) = delete;
    input_surface_t(input_surface_t&&) = delete;
    input_surface_t& operator =(const input_surface_t&) = delete;
    input_surface_t& operator =(input_surface_t&&) = delete;
    input_surface_t() = default;

  public:
    virtual ~input_surface_t() = default;

    /**
     * Test whether the surface accepts touch or pointer input at the given
     * surface-local position.
     *
     * @param at The point to test for.
     * @return True if the point lies inside the input region of the surface,
     *   false otherwise.
     */
    virtual bool accepts_input(wf::pointf_t at) = 0;

    /**
     * The pointer entered the surface at coordinates @at.
     *
     * When entering a surface, the pointer can be confined to a particular
     * region of the surface. This means that no matter what the user input is,
     * the cursor will remain there, until the surface itself or a plugin
     * breaks the constraint.
     *
     * @param at The pointer coordinates relative to the surface.
     * @param refocus True if the pointer focus was already on the surface.
     * @return The region the input should be constrained to, in surface-local
     *   coordinates. No value means that no constraint should be activated.
     */
    virtual std::optional<wf::region_t> handle_pointer_enter(wf::pointf_t at,
        bool refocus) = 0;

    /**
     * The pointer left the surface.
     */
    virtual void handle_pointer_leave() = 0;

    /**
     * The user pressed or released a pointer button while in the surface.
     *
     * @param time_ms The time reported by the device when the event happened.
     * @param buttom The button which this event is about.
     * @param state The new state of the button.
     */
    virtual void handle_pointer_button(uint32_t time_ms, uint32_t button,
        wlr_button_state state) = 0;

    /**
     * The user moved the pointer.
     *
     * @param time_ms The time reported by the device when the event happened.
     * @param at The new position of the pointer relative to the surface.
     */
    virtual void handle_pointer_motion(uint32_t time_ms, wf::pointf_t at) = 0;

    /**
     * The user scrolled.
     *
     * @param time_ms The time reported by the device when the event happened.
     * @param delta The amount scrolled.
     */
    virtual void handle_pointer_axis(uint32_t time_ms,
        wlr_axis_orientation orientation, double delta,
        int32_t delta_discrete, wlr_axis_source source) = 0;

    /**
     * The user touched the screen.
     *
     * @param time_ms The time reported by the device when the event happened.
     * @param id The number of the finger pressed down (first is 0,
     *   then 1, 2, ...)
     * @param at The coordinates of the finger relative to the surface.
     */
    virtual void handle_touch_down(uint32_t time_ms, int32_t id,
        wf::pointf_t at) = 0;

    /**
     * The finger is no longer on the surface.
     *
     * @param time_ms The time reported by the device when the event happened.
     * @param id The number of the released finger
     *   (same as in handle_touch_down).
     * @param finger_lifted Whether the finger was lifted off the screen, or it
     *   has moved to a different surface.
     */
    virtual void handle_touch_up(uint32_t time_ms, int32_t id,
        bool finger_lifted) = 0;

    /**
     * The user moved their finger across the screen.
     *
     * @param time_ms The time reported by the device when the event happened.
     * @param id The number of the finger which was moved
     *   (same as in handle_touch_down).
     * @param at The new coordinates of the finger relative to the surface.
     */
    virtual void handle_touch_motion(uint32_t time_ms, int32_t id,
        wf::pointf_t at) = 0;
};

/**
 * The output side of a surface.
 * It is responsible for providing the content of the surface, redrawing,
 * scaling, etc.
 */
class output_surface_t
{
  protected:
    output_surface_t(const output_surface_t&) = delete;
    output_surface_t(output_surface_t&&) = delete;
    output_surface_t& operator =(const output_surface_t&) = delete;
    output_surface_t& operator =(output_surface_t&&) = delete;
    output_surface_t() = default;

  public:
    virtual ~output_surface_t() = default;

    /**
     * Get the position of the surface relative to its parent surface.
     */
    virtual wf::point_t get_offset() = 0;

    /**
     * Get the surface size.
     */
    virtual wf::dimensions_t get_size() const = 0;

    /**
     * The surface can start redrawing for the next frame.
     * Implementations backed by a wlr_surface typically send a
     * wl_surface.frame event to the client, most other implementations
     * likely do not need to do anything here.
     *
     * @param frame_end The time when the last frame finished rendering.
     */
    virtual void schedule_redraw(const timespec& frame_end) = 0;

    /**
     * Notify the surface that it is visible or no longer visible on a
     * specific output. This is hint to the surface, so that it can use
     * a rendering scale fitting the outputs it is visible on.
     *
     * Note that a surface may receive visibility on an output multiple times,
     * for example if multiple views display it on the same output. In this
     * case the number of visibility events is counted, and the surface stops
     * being visible when the counter reaches 0.
     *
     * Another consideration is that the outputs that a surface is visible on
     * are not automatically inherited, so implementations should initially
     * populate the list of visible outputs manually, if they care about this.
     *
     * @param output The output on which the surface is now visible or no
     *   longer visible.
     * @param is_visible Whether the view is visible on the output or not.
     */
    virtual void set_visible_on_output(wf::output_t *output, bool is_visible) = 0;

    /**
     * Get the opaque region of the surface, in surface-local coordinates.
     *
     * It can be used by renderers for optimization purposes, providing an
     * empty opaque region is a safe default.
     */
    virtual wf::region_t get_opaque_region() = 0;

    /**
     * Render the surface contents on the provided framebuffer.
     *
     * @param fb The target framebuffer.
     * @param pos The positon of the surface in the same coordinate system as
     *   the framebuffer's geometry.
     * @param damage The damaged region of the surface, in the same coordinate
     *   system as the framebuffer's geometry. Nothing should be drawn outside
     *   of the damaged region.
     */
    virtual void simple_render(const wf::framebuffer_t& fb, wf::point_t pos,
        const wf::region_t& damage) = 0;
};

/**
 * surface_interface_t is the base class for everything that can be displayed
 * on the screen. It is the closest thing there is in Wayfire to a Window in X11.
 */
class surface_interface_t : public wf::object_base_t
{
  public:
    virtual ~surface_interface_t();

    surface_interface_t(const surface_interface_t &) = delete;
    surface_interface_t(surface_interface_t &&) = delete;
    surface_interface_t& operator =(const surface_interface_t&) = delete;
    surface_interface_t& operator =(surface_interface_t&&) = delete;

    /**
     * Get the immediate parent of this surface.
     *
     * @return The parent or null, if this is the topmost surface in the hierarchy.
     */
    surface_interface_t *get_parent();

    /**
     * Add a new subsurface to the surface.
     * This may potentially change the subsurface's output.
     *
     * @param subsurface The subsurface to add.
     * @param is_below_parent If true, then the subsurface will be placed below
     *  the view, otherwise, it will be on top of it.
     */
    void add_subsurface(std::unique_ptr<surface_interface_t> subsurface,
        bool is_below_parent);

    /**
     * Remove the given subsurface from the surface tree.
     *
     * No-op if the subsurface does not exist.
     *
     * @param subsurface The subsurface to remove.
     * @return The subsurface removed or nullptr.
     */
    std::unique_ptr<surface_interface_t> remove_subsurface(
        nonstd::observer_ptr<surface_interface_t> subsurface);

    /**
     * @param surface_origin The coordinates of the top-left corner of the
     * surface.
     *
     * @param mapped_only Whether to include only mapped surfaces in the result.
     *
     * @return a list of each mapped surface in the surface tree, including the
     * surface itself.
     *
     * The surfaces should be ordered from the topmost to the bottom-most one.
     */
    virtual std::vector<surface_iterator_t> enumerate_surfaces(
        bool mapped_only = false);

    /**
     * Get the associated wlr_surface.
     * By default, a surface does not have an associated wlr_surface.
     *
     * @return the wlr_surface this surface was created for, or nullptr, if
     *   there is no such wlr_surface.
     */
    virtual wlr_surface *get_wlr_surface();

    /**
     * Check whether the surface is mapped. Mapped surfaces are "alive", i.e
     * they are rendered, can receive input, etc.
     *
     * Note an unmapped surface may be temporarily rendered, for ex. when
     * using a close animation.
     *
     * @return true if the surface is mapped and false otherwise.
     */
    virtual bool is_mapped() const = 0;

    /**
     * Get the input interface of this surface.
     */
    virtual input_surface_t& input() = 0;

    /**
     * Get the output interface of this surface.
     */
    virtual output_surface_t& output() = 0;

    /**
     * Private data for surface_interface_t, used for the implementation of
     * provided functions
     */
    class impl;
    std::unique_ptr<impl> priv;

    /**
     * Request that the opaque region is shrunk by a certain amount of pixels
     * from the edge. Surface implementations that implement subtract_opaque
     * typically also need to implement this function.
     *
     * @param constraint_name The unique name of the component that makes this
     *        request. The request with the biggest shrink_by will be used.
     * @param shrink_by The amount of pixels to shrink by.
     */
    static void set_opaque_shrink_constraint(
        std::string constraint_name, int value);

  protected:
    /** Construct a new surface. */
    surface_interface_t();

    /** @return the active shrink constraint */
    static int get_active_shrink_constraint();

    /** Remove all subsurfaces that we have. Should to be called after unmapping! */
    virtual void clear_subsurfaces();

    /* Allow wlr surface implementation to access surface internals */
    friend class wlr_surface_base_t;

    /**
     * Notify views that a portion of the surface changed.
     * Internally, this will emit the damage signal on the topmost surface in
     * the surface tree, see the damage signal.
     *
     * @param damage The region of the surface which changed.
     */
    void emit_damage(wf::region_t damage);
};

/**
 * Emit a map state change event for the provided surface.
 * A surface should generally emit this signal every time that the map state
 * changes.
 */
void emit_map_state_change(wf::surface_interface_t *surface);

/**
 * name: damage
 * on: surface
 * when: When the surface's contents have changed, it should emit this signal
 *   on the topmost surface in its tree. View implementations are required to
 *   listen for this signal on their main surface and propagate the changes to
 *   the output they are on.
 */
struct surface_damage_signal : public wf::signal_data_t
{
    surface_damage_signal(const wf::region_t& damage);
    const wf::region_t& damage;
};
}

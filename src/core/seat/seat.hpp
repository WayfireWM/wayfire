#ifndef SEAT_HPP
#define SEAT_HPP

#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "../../view/surface-impl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/input-device.hpp"
#include "wayfire/scene-input.hpp"

namespace wf
{
struct cursor_t;
class keyboard_t;

class input_device_impl_t : public wf::input_device_t
{
  public:
    input_device_impl_t(wlr_input_device *dev);
    virtual ~input_device_impl_t() = default;

    wf::wl_listener_wrapper on_destroy;
    virtual void update_options()
    {}
};

class pointer_t;
class touch_interface_t;
class drag_icon_t;

/**
 * A seat is a collection of input devices which work together, and have a
 * keyboard focus, etc.
 *
 * The seat is the place where a bit of the shared state of separate input devices
 * resides, and also contains:
 *
 * 1. Keyboards
 * 2. Logical pointer
 * 3. Touch interface
 * 4. Tablets
 *
 * In addition, each seat has its own clipboard, primary selection and DnD state.
 * Currently, Wayfire supports just a single seat.
 */
class seat_t
{
  public:
    seat_t();

    uint32_t get_modifiers();

    void break_mod_bindings();

    void set_keyboard_focus(wf::scene::node_ptr keyboard_focus);
    wf::scene::node_ptr keyboard_focus;

    /**
     * Set the currently active keyboard on the seat.
     */
    void set_keyboard(wf::keyboard_t *kbd);

    wlr_seat *seat = nullptr;
    std::unique_ptr<cursor_t> cursor;
    std::unique_ptr<pointer_t> lpointer;
    std::unique_ptr<touch_interface_t> touch;

    // Current drag icon
    std::unique_ptr<wf::drag_icon_t> drag_icon;
    // Is dragging active. Note we can have a drag without a drag icon.
    bool drag_active = false;

    /** Update the position of the drag icon, if it exists */
    void update_drag_icon();

    /**
     * Make sure that the surface can receive input focus.
     * If it is a xwayland surface, it will be restacked to the top.
     */
    void ensure_input_surface(wf::surface_interface_t *surface);

  private:
    wf::wl_listener_wrapper request_start_drag, start_drag, end_drag,
        request_set_selection, request_set_primary_selection;

    wf::signal_connection_t on_new_device, on_remove_device;

    /** The currently active keyboard device on the seat */
    wf::keyboard_t *current_keyboard = nullptr;

    /** A list of all keyboards in this seat */
    std::vector<std::unique_ptr<wf::keyboard_t>> keyboards;

    /**
     * Check if the drag request is valid, and if yes, start the drag operation.
     */
    void validate_drag_request(wlr_seat_request_start_drag_event *ev);

    /** Send updated capabilities to clients */
    void update_capabilities();

    // The surface which has last received input focus
    wlr_surface *last_focus_surface = NULL;
};
}

/** Convert the given point to a node-local point */
wf::pointf_t get_node_local_coords(wf::scene::node_t *node,
    const wf::pointf_t& point);

/** Check whether a node with an implicit grab should still retain the grab. */
bool is_grabbed_node_alive(wf::scene::node_ptr node);

#endif /* end of include guard: SEAT_HPP */

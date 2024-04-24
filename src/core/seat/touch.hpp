#ifndef TOUCH_HPP
#define TOUCH_HPP

#include <map>
#include <wayfire/touch/touch.hpp>
#include "wayfire/scene-input.hpp"
#include "wayfire/util.hpp"
#include <wayfire/signal-definitions.hpp>

// TODO: tests
namespace wf
{
using input_surface_selector_t =
    std::function<wf::scene::node_ptr(const wf::pointf_t&)>;

/**
 * Responsible for managing touch gestures and forwarding events to clients.
 */
class touch_interface_t
{
  public:
    touch_interface_t(wlr_cursor *cursor, wlr_seat *seat,
        input_surface_selector_t surface_at);
    ~touch_interface_t();

    touch_interface_t(const touch_interface_t &) = delete;
    touch_interface_t(touch_interface_t &&) = delete;
    touch_interface_t& operator =(const touch_interface_t&) = delete;
    touch_interface_t& operator =(touch_interface_t&&) = delete;

    /** Get the positions of the fingers */
    const touch::gesture_state_t& get_state() const;

    /** Get the focused surface */
    wf::scene::node_ptr get_focus(int finger_id = 0) const;

    /**
     * Register a new touchscreen gesture.
     */
    void add_touch_gesture(nonstd::observer_ptr<touch::gesture_t> gesture);

    /**
     * Unregister a touchscreen gesture.
     */
    void rem_touch_gesture(nonstd::observer_ptr<touch::gesture_t> gesture);

    /**
     * Transfer input focus to the given grab.
     */
    void transfer_grab(scene::node_ptr grab_node);

  private:
    wlr_seat *seat;
    wlr_cursor *cursor;
    input_surface_selector_t surface_at;

    wf::wl_listener_wrapper on_down, on_up, on_motion, on_cancel, on_frame;
    void handle_touch_down(int32_t id, uint32_t time, wf::pointf_t current,
        input_event_processing_mode_t mode);
    void handle_touch_motion(int32_t id, uint32_t time, wf::pointf_t current,
        bool real_event, input_event_processing_mode_t mode);
    void handle_touch_up(int32_t id, uint32_t time,
        input_event_processing_mode_t mode);

    void set_touch_focus(wf::scene::node_ptr node,
        int32_t id, int64_t time, wf::pointf_t current);

    touch::gesture_state_t finger_state;

    /** Pressed a finger on a surface and dragging outside of it now */
    std::map<int, wf::scene::node_ptr> focus;

    void update_gestures(const wf::touch::gesture_event_t& event);
    std::vector<nonstd::observer_ptr<touch::gesture_t>> gestures;

    wf::signal::connection_t<wf::scene::root_node_update_signal>
    on_root_node_updated;

    std::unique_ptr<touch::gesture_t> multiswipe, edgeswipe, multipinch;
    void add_default_gestures();

    /** Enable/disable cursor depending on how many touch points are there */
    void update_cursor_state();
};
}

#endif /* end of include guard: TOUCH_HPP */

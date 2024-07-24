#pragma once

#include <any>
#include "wayfire/util.hpp"
#include <wayfire/config/types.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <any>

namespace  wf
{
/**
 * Represents a binding with a plugin-provided callback and activation option.
 */
template<class Option, class Callback>
struct binding_t
{
    wf::option_sptr_t<Option> activated_by;
    Callback *callback;
    std::vector<std::any> tags;
};

template<class Option, class Callback> using binding_container_t =
    std::vector<std::unique_ptr<binding_t<Option, Callback>>>;

/**
 * Represents an instance of a hotspot.
 */
class hotspot_instance_t
{
  public:
    ~hotspot_instance_t() = default;
    hotspot_instance_t(uint32_t edges, uint32_t along, uint32_t away, int32_t timeout,
        std::function<void(uint32_t)> callback);

  private:
    /** The possible hotspot rectangles */
    wf::geometry_t hotspot_geometry[2];
    wf::output_t *last_output = nullptr;

    /** Requested dimensions */
    int32_t along, away;

    /** Timer for hotspot activation */
    wf::wl_timer<false> timer;

    /**
     * Only one event should be triggered once the cursor enters the hotspot area.
     * This prevents another event being fired until the cursor has left the area.
     */
    bool armed = true;

    /** Timeout to activate hotspot */
    uint32_t timeout_ms;

    /** Edges of the hotspot */
    uint32_t edges;

    /** Callback to execute */
    std::function<void(uint32_t)> callback;

    wf::signal::connection_t<wf::post_input_event_signal<wlr_tablet_tool_axis_event>> on_tablet_axis;
    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> on_motion_event;
    wf::signal::connection_t<wf::post_input_event_signal<wlr_touch_motion_event>> on_touch_motion;

    /** Update state based on input motion */
    void process_input_motion(wf::pointf_t gc);

    /** Calculate a rectangle with size @dim inside @og at the correct edges. */
    wf::geometry_t pin(wf::dimensions_t dim) noexcept;

    /** Recalculate the hotspot geometries. */
    void recalc_geometry() noexcept;
};

/**
 * Manages hotspot bindings on the given output.
 * A part of the bindings_repository_t.
 */
class hotspot_manager_t
{
  public:
    hotspot_manager_t()
    {}

    using container_t = binding_container_t<activatorbinding_t, activator_callback>;
    void update_hotspots(const container_t& activators);

  private:
    std::vector<std::unique_ptr<hotspot_instance_t>> hotspots;
};
}

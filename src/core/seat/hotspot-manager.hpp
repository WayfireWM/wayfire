#pragma once

#include <map>
#include "wayfire/util.hpp"
#include <wayfire/config/types.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/log.hpp>

namespace  wf
{
/**
 * Opaque binding handle for plugins.
 */
struct binding_t
{};

/**
 * Represents a binding with a plugin-provided callback and activation option.
 */
template<class Option, class Callback>
struct output_binding_t : public binding_t
{
    wf::option_sptr_t<Option> activated_by;
    Callback *callback;
};

template<class Option, class Callback> using binding_container_t =
    std::vector<std::unique_ptr<output_binding_t<Option, Callback>>>;

/**
 * Represents an instance of a hotspot.
 */
class hotspot_instance_t
{
  public:
    ~hotspot_instance_t() = default;
    hotspot_instance_t(wf::output_t *output, uint32_t edges, uint32_t along,
        uint32_t away, int32_t timeout, std::function<void(uint32_t)> callback);

    hotspot_instance_t(const hotspot_instance_t &) = delete;
    hotspot_instance_t(hotspot_instance_t &&) = delete;
    hotspot_instance_t& operator =(const hotspot_instance_t&) = delete;
    hotspot_instance_t& operator =(hotspot_instance_t&&) = delete;

  private:
    /** The output this hotspot is on */
    wf::output_t *output;

    /** The possible hotspot rectangles */
    wf::geometry_t hotspot_geometry[2];

    /** Requested dimensions */
    int32_t along, away;

    /** Timer for hotspot activation */
    wf::wl_timer timer;

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

    wf::signal_connection_t on_motion_event;
    wf::signal_connection_t on_touch_motion_event;
    wf::signal_connection_t on_output_config_changed;

    /** Update state based on input motion */
    void process_input_motion(wf::point_t gc);

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
    hotspot_manager_t(wf::output_t *output)
    {
        this->output = output;
    }

    using container_t =
        binding_container_t<activatorbinding_t, activator_callback>;

    void update_hotspots(const container_t& activators);

  private:
    wf::output_t *output;
    std::vector<std::unique_ptr<hotspot_instance_t>> hotspots;
};
}

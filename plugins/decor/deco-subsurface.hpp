#ifndef DECO_SUBSURFACE_HPP
#define DECO_SUBSURFACE_HPP

#include "wayfire/object.hpp"
#include "wayfire/toplevel.hpp"
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>

class simple_decoration_node_t;
namespace wf
{
/**
 * A decorator object attached as custom data to a toplevel object.
 */
class simple_decorator_t : public wf::custom_data_t
{
    wayfire_toplevel_view view;
    std::shared_ptr<simple_decoration_node_t> deco;

    wf::signal::connection_t<wf::view_activated_state_signal> on_view_activated;
    wf::signal::connection_t<wf::view_geometry_changed_signal> on_view_geometry_changed;
    wf::signal::connection_t<wf::view_fullscreen_signal> on_view_fullscreen;

  public:
    simple_decorator_t(wayfire_toplevel_view view);
    ~simple_decorator_t();
    wf::decoration_margins_t get_margins(const wf::toplevel_state_t& state);
};
}

#endif /* end of include guard: DECO_SUBSURFACE_HPP */

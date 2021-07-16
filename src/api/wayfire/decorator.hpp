#ifndef DECORATOR_HPP
#define DECORATOR_HPP

#include <wayfire/geometry.hpp>

namespace wf
{
class decorator_frame_t_t
{
  public:
    virtual wf::geometry_t expand_wm_geometry(
        wf::geometry_t contained_wm_geometry) = 0;

    virtual void calculate_resize_size(
        int& target_width, int& target_height) = 0;

    virtual void notify_view_activated(bool active)
    {}
    virtual void notify_view_resized(wf::geometry_t view_geometry)
    {}
    virtual void notify_view_tiled()
    {}
    virtual void notify_view_fullscreen()
    {}

    decorator_frame_t_t() = default;
    virtual ~decorator_frame_t_t() = default;
    decorator_frame_t_t(const decorator_frame_t_t &) = default;
    decorator_frame_t_t(decorator_frame_t_t &&) = default;
    decorator_frame_t_t& operator =(const decorator_frame_t_t&) = default;
    decorator_frame_t_t& operator =(decorator_frame_t_t&&) = default;
};
}

#endif /* end of include guard: DECORATOR_HPP */

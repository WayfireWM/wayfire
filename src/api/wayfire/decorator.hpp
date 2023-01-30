#ifndef DECORATOR_HPP
#define DECORATOR_HPP

#include <wayfire/geometry.hpp>

namespace wf
{
/**
 * Describes the size of the decoration frame around a toplevel.
 */
struct decoration_margins_t
{
    int left;
    int right;
    int bottom;
    int top;
};

class decorator_frame_t_t
{
  public:
    virtual decoration_margins_t get_margins() = 0;

    wf::geometry_t expand_wm_geometry(wf::geometry_t contained_wm_geometry)
    {
        auto margins = get_margins();
        contained_wm_geometry.x     -= margins.left;
        contained_wm_geometry.y     -= margins.top;
        contained_wm_geometry.width += margins.left + margins.right;
        contained_wm_geometry.height += margins.top + margins.bottom;
        return contained_wm_geometry;
    }

    void calculate_resize_size(int& target_width, int& target_height)
    {
        auto margins = get_margins();
        target_width  -= margins.left + margins.right;
        target_height -= margins.top + margins.bottom;
        target_width   = std::max(target_width, 1);
        target_height  = std::max(target_height, 1);
    }

    virtual ~decorator_frame_t_t() = default;
};
}

#endif /* end of include guard: DECORATOR_HPP */

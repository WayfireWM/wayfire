#ifndef COMPOSITOR_VIEW_HPP
#define COMPOSITOR_VIEW_HPP

#include "wayfire/geometry.hpp"
#include "wayfire/view.hpp"
#include <wayfire/config/types.hpp>

namespace wf
{
/**
 * color_rect_view_t represents another common type of compositor view - a
 * view which is simply a colored rectangle with a border.
 */
class color_rect_view_t : public wf::view_interface_t
{
  protected:
    wf::color_t _color;
    wf::color_t _border_color;
    int border;

    wf::geometry_t geometry;
    bool _is_mapped;
    class color_rect_node_t;

  public:
    /**
     * Create a colored rect view. The map signal is not fired by default.
     * The creator of the colored view should also add it to the desired layer.
     */
    color_rect_view_t();

    virtual void initialize() override;

    /**
     * Emit the unmap signal and then drop the internal reference.
     */
    virtual void close() override;

    /** Set the view color. Color's alpha is not premultiplied */
    virtual void set_color(wf::color_t color);
    /** Set the view border color. Color's alpha is not premultiplied */
    virtual void set_border_color(wf::color_t border);
    /** Set the border width. */
    virtual void set_border(int width);

    /** Set the view geometry. */
    virtual void set_geometry(wf::geometry_t geometry);
    virtual wf::geometry_t get_geometry();

    /* required for view_interface_t */
    virtual bool is_mapped() const override;

    virtual wlr_surface *get_keyboard_focus_surface() override;
    virtual bool is_focusable() const override;
};
}

#endif /* end of include guard: COMPOSITOR_VIEW_HPP */

#ifndef COMPOSITOR_VIEW_HPP
#define COMPOSITOR_VIEW_HPP

#include "wayfire/compositor-surface.hpp"
#include "wayfire/view.hpp"
#include <wayfire/config/types.hpp>

namespace wf
{
/**
 * Base class for compositor views that need to interact with the keyboard
 */
class compositor_interactive_view_t
{
  public:
    void handle_keyboard_enter()
    {}
    void handle_keyboard_leave()
    {}
    void handle_key(uint32_t key, uint32_t state)
    {}
};

compositor_interactive_view_t *interactive_view_from_view(
    wf::view_interface_t *view);

/**
 * mirror_view_t is a specialized type of views.
 *
 * They have the same contents as another view. A mirror view's size is
 * determined by the bounding box of the mirrored view. However, the mirror view
 * itself can be on another position, output, etc. and can have additional
 * transforms.
 *
 * A mirror view is mapped as long as its base view is mapped. Afterwards, the
 * view becomes unmapped, until it is destroyed.
 */
class mirror_view_t : public wf::view_interface_t, wf::compositor_surface_t
{
  protected:
    signal_connection_t base_view_unmapped, base_view_damaged;
    wayfire_view base_view;
    int x;
    int y;

  public:
    /**
     * Create a new mirror view for the given base view. When using mirror
     * views, the view should be manually added to the appropriate layer in
     * workspace-manager.
     *
     * Note: a map event is not emitted for mirror views by default.
     */
    mirror_view_t(wayfire_view base_view);
    virtual ~mirror_view_t();

    /**
     * Close the view. This means unsetting the base view and setting the state
     * of the view to unmapped.
     *
     * This will also fire the unmap event on the mirror view.
     */
    virtual void close() override;

    /* surface_interface_t implementation */
    virtual bool is_mapped() const override;
    virtual wf::dimensions_t get_size() const override;
    virtual void simple_render(const wf::render_target_t& fb, int x, int y,
        const wf::region_t& damage) override;

    /* view_interface_t implementation */
    virtual void move(int x, int y) override;
    virtual wf::geometry_t get_output_geometry() override;

    virtual wlr_surface *get_keyboard_focus_surface() override;
    virtual bool is_focuseable() const override;
    virtual bool should_be_decorated() override;
};

/**
 * color_rect_view_t represents another common type of compositor view - a
 * view which is simply a colored rectangle with a border.
 */
class color_rect_view_t : public wf::view_interface_t,
    public wf::compositor_surface_t
{
  protected:
    wf::color_t _color;
    wf::color_t _border_color;
    int border;

    wf::geometry_t geometry;
    bool _is_mapped;

  public:
    /**
     * Create a colored rect view. The map signal is not fired by default.
     * The creator of the colored view should also add it to the desired layer.
     */
    color_rect_view_t();

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

    /* required for surface_interface_t */
    virtual bool is_mapped() const override;
    virtual wf::dimensions_t get_size() const override;
    virtual void simple_render(const wf::render_target_t& fb, int x, int y,
        const wf::region_t& damage) override;

    /* required for view_interface_t */
    virtual void move(int x, int y) override;
    virtual void resize(int w, int h) override;
    virtual wf::geometry_t get_output_geometry() override;

    virtual wlr_surface *get_keyboard_focus_surface() override;
    virtual bool is_focuseable() const override;
    virtual bool should_be_decorated() override;
};
}

#endif /* end of include guard: COMPOSITOR_VIEW_HPP */

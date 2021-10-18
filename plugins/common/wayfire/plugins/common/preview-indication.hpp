#include <wayfire/geometry.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/compositor-view.hpp>

#include "geometry-animation.hpp"
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util/duration.hpp>

namespace wf
{
using namespace wf::animation;
class preview_indication_animation_t : public geometry_animation_t
{
  public:
    using geometry_animation_t::geometry_animation_t;
    timed_transition_t alpha{*this};
};

/**
 * A view which can be used to show previews for different actions on the
 * screen, for ex. when snapping a view
 */
class preview_indication_view_t : public wf::color_rect_view_t
{
    wf::effect_hook_t pre_paint;
    wf::output_t *output;

    /* Default colors */
    const wf::color_t base_color  = {0.5, 0.5, 1, 0.5};
    const wf::color_t base_border = {0.25, 0.25, 0.5, 0.8};
    const int base_border_w = 3;

    preview_indication_animation_t animation;
    bool should_close = false;

  public:
    preview_indication_view_t(const preview_indication_view_t&) = delete;
    preview_indication_view_t(preview_indication_view_t&&) = delete;
    preview_indication_view_t& operator =(const preview_indication_view_t&) = delete;
    preview_indication_view_t& operator =(preview_indication_view_t&&) = delete;

    /**
     * Create a new indication preview on the indicated output.
     *
     * @param start_geometry The geometry the preview should have, relative to
     *                       the output
     */
    preview_indication_view_t(wf::output_t *output, wf::geometry_t start_geometry) :
        wf::color_rect_view_t(), animation(wf::create_option<int>(200))
    {
        this->output = output;
        set_output(output);
        animation.set_start(start_geometry);
        animation.set_end(start_geometry);
        animation.alpha.set(0, 1);

        pre_paint = [=] () { update_animation(); };
        get_output()->render->add_effect(&pre_paint, wf::OUTPUT_EFFECT_PRE);

        get_color_surface()->set_color(base_color);
        get_color_surface()->set_border_color(base_border);
        get_color_surface()->set_border(base_border_w);

        this->role = VIEW_ROLE_DESKTOP_ENVIRONMENT;
    }

    void initialize() override
    {
        get_output()->workspace->add_view(self(), wf::LAYER_TOP);
    }

    /** A convenience wrapper around the full version */
    preview_indication_view_t(wf::output_t *output, wf::point_t start) :
        preview_indication_view_t(output, wf::geometry_t{start.x, start.y, 1, 1})
    {}

    /**
     * Animate the preview to the given target geometry and alpha.
     *
     * @param close Whether the view should be closed when the target is
     *              reached.
     */
    void set_target_geometry(wf::geometry_t target, float alpha, bool close = false)
    {
        animation.x.restart_with_end(target.x);
        animation.y.restart_with_end(target.y);
        animation.width.restart_with_end(target.width);
        animation.height.restart_with_end(target.height);
        animation.alpha.restart_with_end(alpha);
        animation.start();
        this->should_close = close;
    }

    /**
     * A wrapper around set_target_geometry(wf::geometry_t, double, bool)
     */
    void set_target_geometry(wf::point_t point, double alpha,
        bool should_close = false)
    {
        return set_target_geometry({point.x, point.y, 1, 1},
            alpha, should_close);
    }

    virtual ~preview_indication_view_t()
    {
        this->output->render->rem_effect(&pre_paint);
    }

  protected:
    /** Update the current state */
    void update_animation()
    {
        wf::geometry_t current = animation;
        if (current != get_wm_geometry())
        {
            set_geometry(current);
        }

        double alpha = animation.alpha;
        if (base_color.a * alpha != get_color_surface()->_color.a)
        {
            auto new_color  = get_color_surface()->_color;
            auto new_border = get_color_surface()->_border_color;
            new_color.a  = alpha * base_color.a;
            new_border.a = alpha * base_border.a;

            get_color_surface()->set_color(new_color);
            get_color_surface()->set_border_color(new_border);
        }

        /* The end of unmap animation, just exit */
        if (!animation.running() && should_close)
        {
            close();
        }
    }
};
}

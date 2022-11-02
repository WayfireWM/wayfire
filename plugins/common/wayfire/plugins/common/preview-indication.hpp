#include <wayfire/geometry.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/compositor-view.hpp>

#include "geometry-animation.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/view.hpp"
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
    const wf::option_wrapper_t<wf::color_t> base_color;
    const wf::option_wrapper_t<wf::color_t> base_border;
    const wf::option_wrapper_t<int> base_border_w;

    preview_indication_animation_t animation;
    bool should_close = false;

  public:

    /**
     * Create a new indication preview on the indicated output.
     *
     * @param start_geometry The geometry the preview should have, relative to
     *                       the output
     */
    preview_indication_view_t(wf::geometry_t start_geometry, const std::string& prefix) :
        wf::color_rect_view_t(), animation(wf::create_option<int>(200)),
        base_color(prefix + "/preview_base_color"),
        base_border(prefix + "/preview_base_border"),
        base_border_w(prefix + "/preview_border_width")
    {
        animation.set_start(start_geometry);
        animation.set_end(start_geometry);
        animation.alpha.set(0, 1);

        this->role = VIEW_ROLE_DESKTOP_ENVIRONMENT;
    }

    void set_output(wf::output_t *output) override
    {
        if (get_output())
        {
            get_output()->render->rem_effect(&pre_paint);
        }

        this->output = output;
        wf::color_rect_view_t::set_output(output);

        if (output)
        {
            pre_paint = [=] () { update_animation(); };
            output->render->add_effect(&pre_paint, wf::OUTPUT_EFFECT_PRE);
            wf::scene::readd_front(output->node_for_layer(wf::scene::layer::TOP), get_root_node());
        }
    }

    void initialize() override
    {
        color_rect_view_t::initialize();

        set_color(base_color);
        set_border_color(base_border);
        set_border(base_border_w);
    }

    /** A convenience wrapper around the full version */
    preview_indication_view_t(wf::point_t start, const std::string & prefix) :
        preview_indication_view_t(wf::geometry_t{start.x, start.y, 1, 1}, prefix)
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
        wf::scene::remove_child(get_root_node());
        if (this->output)
        {
            this->output->render->rem_effect(&pre_paint);
        }
    }

  protected:
    /** Update the current state */
    void update_animation()
    {
        wf::geometry_t current = animation;
        if (current != geometry)
        {
            set_geometry(current);
        }

        double alpha = animation.alpha;
        if (base_color.value().a * alpha != _color.a)
        {
            _color.a = alpha * base_color.value().a;
            _border_color.a = alpha * base_border.value().a;

            set_color(_color);
            set_border_color(_border_color);
        }

        /* The end of unmap animation, just exit */
        if (!animation.running() && should_close)
        {
            close();
        }
    }
};
}

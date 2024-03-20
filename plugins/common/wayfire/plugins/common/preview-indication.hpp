#pragma once

#include <wayfire/geometry.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/compositor-view.hpp>

#include "geometry-animation.hpp"
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
class preview_indication_t : public std::enable_shared_from_this<preview_indication_t>
{
    wf::effect_hook_t pre_paint;
    wf::output_t *output;

    preview_indication_animation_t animation;
    bool should_close = false;

    /* Default colors */
    const wf::option_wrapper_t<wf::color_t> base_color;
    const wf::option_wrapper_t<wf::color_t> base_border;
    const wf::option_wrapper_t<int> base_border_w;
    std::shared_ptr<preview_indication_t> _self_reference;

  public:
    std::shared_ptr<color_rect_view_t> view;

    /**
     * Create a new indication preview on the indicated output.
     *
     * @param start_geometry The geometry the preview should have, relative to
     *                       the output
     */
    preview_indication_t(wf::geometry_t start_geometry, wf::output_t *output, const std::string& prefix) :
        animation(wf::create_option<int>(200)),
        base_color(prefix + "/preview_base_color"),
        base_border(prefix + "/preview_base_border"),
        base_border_w(prefix + "/preview_border_width")
    {
        animation.set_start(start_geometry);
        animation.set_end(start_geometry);
        animation.alpha.set(0, 1);

        this->output = output;
        pre_paint    = [=] () { update_animation(); };
        output->render->add_effect(&pre_paint, wf::OUTPUT_EFFECT_PRE);
        view = color_rect_view_t::create(VIEW_ROLE_DESKTOP_ENVIRONMENT, output, wf::scene::layer::TOP);

        view->set_color(base_color);
        view->set_border_color(base_border);
        view->set_border(base_border_w);
    }

    /** A convenience wrapper around the full version */
    preview_indication_t(wf::point_t start, wf::output_t *output, const std::string & prefix) :
        preview_indication_t(wf::geometry_t{start.x, start.y, 1, 1}, output, prefix)
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

        if (should_close)
        {
            // Take a reference until we finally close the view
            _self_reference = shared_from_this();
        }
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

    wf::geometry_t get_target_geometry() const
    {
        return wf::geometry_t{
            .x     = int(animation.x.end),
            .y     = int(animation.y.end),
            .width = int(animation.width.end),
            .height = int(animation.height.end),
        };
    }

    wf::output_t *get_output() const
    {
        return output;
    }

    virtual ~preview_indication_t()
    {
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
        if (current != view->get_geometry())
        {
            view->set_geometry(current);
        }

        double alpha   = animation.alpha;
        auto cur_color = view->get_color();
        auto cur_border_color = view->get_border_color();

        if (base_color.value().a * alpha != cur_color.a)
        {
            cur_color.a = alpha * base_color.value().a;
            cur_border_color.a = alpha * base_border.value().a;

            view->set_color(cur_color);
            view->set_border_color(cur_border_color);
        }

        /* The end of unmap animation, just exit */
        if (!animation.running() && should_close)
        {
            view->close();
            view->damage();
            _self_reference.reset();
        }
    }
};
}

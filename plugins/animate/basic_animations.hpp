#include "animate.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/output.hpp>

class fade_animation : public animation_base
{
    wayfire_view view;

    float start = 0, end = 1;
    wf::animation::simple_animation_t progression;
    std::string name;

  public:

    void init(wayfire_view view, int dur, wf_animation_type type) override
    {
        this->view = view;
        this->progression =
            wf::animation::simple_animation_t(wf::create_option<int>(dur));

        this->progression.animate(start, end);

        if (type & HIDING_ANIMATION)
        {
            this->progression.flip();
        }

        name = "animation-fade-" + std::to_string(type);
        view->add_transformer(
            std::make_unique<wf::view_2D>(view, wf::TRANSFORMER_HIGHLEVEL), name);
    }

    bool step() override
    {
        auto transform =
            dynamic_cast<wf::view_2D*>(view->get_transformer(name).get());
        transform->alpha = this->progression;

        return progression.running();
    }

    void reverse() override
    {
        this->progression.reverse();
    }

    int get_direction() override
    {
        return this->progression.get_direction();
    }

    ~fade_animation()
    {
        view->pop_transformer(name);
    }
};

using namespace wf::animation;

class zoom_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t alpha{*this};
    timed_transition_t zoom{*this};
    timed_transition_t offset_x{*this};
    timed_transition_t offset_y{*this};
};

class zoom_animation : public animation_base
{
    wayfire_view view;
    wf::view_2D *our_transform = nullptr;
    zoom_animation_t progression;
    std::string name;

  public:

    void init(wayfire_view view, int dur, wf_animation_type type) override
    {
        this->view = view;
        this->progression = zoom_animation_t(wf::create_option<int>(dur));
        this->progression.alpha = wf::animation::timed_transition_t(
            this->progression, 0, 1);
        this->progression.zoom = wf::animation::timed_transition_t(
            this->progression, 1. / 3, 1);
        this->progression.offset_x = wf::animation::timed_transition_t(
            this->progression, 0, 0);
        this->progression.offset_y = wf::animation::timed_transition_t(
            this->progression, 0, 0);
        this->progression.start();

        if (type & MINIMIZE_STATE_ANIMATION)
        {
            auto hint = view->get_minimize_hint();
            if ((hint.width > 0) && (hint.height > 0))
            {
                int hint_cx = hint.x + hint.width / 2;
                int hint_cy = hint.y + hint.height / 2;

                auto bbox   = view->get_wm_geometry();
                int view_cx = bbox.x + bbox.width / 2;
                int view_cy = bbox.y + bbox.height / 2;

                progression.offset_x.set(1.0 * hint_cx - view_cx, 0);
                progression.offset_y.set(1.0 * hint_cy - view_cy, 0);

                if ((bbox.width > 0) && (bbox.height > 0))
                {
                    double scale_x = 1.0 * hint.width / bbox.width;
                    double scale_y = 1.0 * hint.height / bbox.height;
                    progression.zoom.set(std::min(scale_x, scale_y), 1);
                }
            }
        }

        if (type & HIDING_ANIMATION)
        {
            progression.alpha.flip();
            progression.zoom.flip();
            progression.offset_x.flip();
            progression.offset_y.flip();
        }

        name = "animation-zoom-" + std::to_string(type);
        our_transform = new wf::view_2D(view, wf::TRANSFORMER_HIGHLEVEL);
        view->add_transformer(std::unique_ptr<wf::view_2D>(our_transform), name);
    }

    bool step() override
    {
        float c = this->progression.zoom;

        our_transform->alpha   = this->progression.alpha;
        our_transform->scale_x = c;
        our_transform->scale_y = c;

        our_transform->translation_x = this->progression.offset_x;
        our_transform->translation_y = this->progression.offset_y;

        return this->progression.running();
    }

    void reverse() override
    {
        this->progression.reverse();
    }

    ~zoom_animation()
    {
        view->pop_transformer(name);
    }
};

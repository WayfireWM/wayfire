#include "animate.hpp"
#include "wayfire/toplevel-view.hpp"
#include <memory>
#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/output.hpp>

static const std::string zap_transformer_name = "zap-transformer";
namespace wf
{
namespace zap
{
using namespace wf::animation;
class zap_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
};
class zap_animation : public animation_base
{
    wayfire_view view;
    wf_animation_type type;
    wf::zap::zap_animation_t progression;

  public:

    void init(wayfire_view view, wf::animation_description_t dur, wf_animation_type type) override
    {
        this->view = view;
        this->type = type;
        this->progression =
            wf::zap::zap_animation_t(wf::create_option<wf::animation_description_t>(dur));

        if (type & HIDING_ANIMATION)
        {
            this->progression.reverse();
        }

        this->progression.start();

        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
        view->get_transformed_node()->add_transformer(
            tr, wf::TRANSFORMER_HIGHLEVEL, zap_transformer_name);
    }

    bool step() override
    {
        auto transform = view->get_transformed_node()
            ->get_transformer<wf::scene::view_2d_transformer_t>(zap_transformer_name);
        auto progress = this->progression.progress();
        auto progress_pt_one   = std::clamp(progress, 0.0, 1.0 / 3.0) * 3.0;
        auto progress_pt_two   = (std::clamp(progress, 1.0 / 3.0, (1.0 / 3.0) * 2.0) - 1.0 / 3.0) * 3.0;
        auto progress_pt_three = (std::clamp(progress, (1.0 / 3.0) * 2.0, 1.0) - (1.0 / 3.0) * 2.0) * 3.0;
        transform->alpha   = progress_pt_one;
        transform->scale_x = 0.01 + progress_pt_two * 0.99;
        transform->scale_y = 0.01 + progress_pt_three * 0.99;

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

    ~zap_animation()
    {
        view->get_transformed_node()->rem_transformer(zap_transformer_name);
    }
};
}
}

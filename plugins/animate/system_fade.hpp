#ifndef SYSTEM_FADE_HPP
#define SYSTEM_FADE_HPP

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>

/* animates wake from suspend/startup by fading in the whole output */
class wf_system_fade
{
    wf::animation::simple_animation_t progression;

    wf::output_t *output;

    wf::effect_hook_t damage_hook, render_hook;

  public:
    wf_system_fade(wf::output_t *out, wf::animation_description_t dur) :
        progression(wf::create_option<wf::animation_description_t>(dur)), output(out)
    {
        damage_hook = [=] ()
        { output->render->damage_whole(); };

        render_hook = [=] ()
        { render(); };

        output->render->add_effect(&damage_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->add_effect(&render_hook, wf::OUTPUT_EFFECT_OVERLAY);
        output->render->set_redraw_always();
        this->progression.animate(1, 0);
    }

    void render()
    {
        wf::color_t color{0, 0, 0, this->progression};
        auto fb = output->render->get_target_framebuffer();
        auto geometry = output->get_relative_geometry();

        OpenGL::render_begin(fb);
        OpenGL::render_rectangle(geometry, color,
            fb.get_orthographic_projection());
        OpenGL::render_end();

        if (!progression.running())
        {
            finish();
        }
    }

    void finish()
    {
        output->render->rem_effect(&damage_hook);
        output->render->rem_effect(&render_hook);
        output->render->set_redraw_always(false);

        delete this;
    }
};

#endif

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>

class wayfire_zoom_screen : public wf::per_output_plugin_instance_t
{
    enum class interpolation_method_t
    {
        LINEAR  = 0,
        NEAREST = 1,
    };

    wf::option_wrapper_t<wf::keybinding_t> modifier{"zoom/modifier"};
    wf::option_wrapper_t<double> speed{"zoom/speed"};
    wf::option_wrapper_t<wf::animation_description_t> smoothing_duration{"zoom/smoothing_duration"};
    wf::option_wrapper_t<int> interpolation_method{"zoom/interpolation_method"};
    wf::animation::simple_animation_t progression{smoothing_duration};
    bool hook_set = false;

    wf::plugin_activation_data_t grab_interface = {
        .name = "zoom",
        .capabilities = 0,
    };

  public:
    void init() override
    {
        progression.set(1, 1);
        output->add_axis(modifier, &axis);
    }

    void update_zoom_target(float delta)
    {
        float target = progression.end;
        target -= target * delta * speed;
        target  = wf::clamp(target, 1.0f, 50.0f);

        if (target != progression.end)
        {
            progression.animate(target);

            if (!hook_set)
            {
                hook_set = true;
                output->render->add_post(&render_hook);
                output->render->set_redraw_always();
            }
        }
    }

    wf::axis_callback axis = [=] (wlr_pointer_axis_event *ev)
    {
        if (!output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        if (ev->orientation != WLR_AXIS_ORIENTATION_VERTICAL)
        {
            return false;
        }

        update_zoom_target(ev->delta);

        return true;
    };

    wf::post_hook_t render_hook = [=] (const wf::framebuffer_t& source,
                                       const wf::framebuffer_t& destination)
    {
        auto w = destination.viewport_width;
        auto h = destination.viewport_height;
        auto oc = output->get_cursor_position();
        double x, y;
        wlr_box b = output->get_relative_geometry();
        wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

        /* get rotation & scale */
        wlr_box box = {int(x), int(y), 1, 1};
        box = output->render->get_target_framebuffer().
            framebuffer_box_from_geometry_box(box);

        x = box.x;
        y = h - box.y;

        const float scale = (progression - 1) / progression;

        // The target width and height are truncated here so that `x1+tw` and
        // `x1` round to GLint in tandem for glBlitFramebuffer(). This keeps the
        // aspect ratio constant while panning around.
        const GLint tw = w / progression, th = h / progression;

        const float x1 = x * scale;
        const float y1 = y * scale;

        const GLenum interpolation =
            (interpolation_method ==
                (int)interpolation_method_t::NEAREST) ? GL_NEAREST : GL_LINEAR;

        OpenGL::render_begin(source);
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fb));
        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.fb));
        GL_CALL(glBlitFramebuffer(x1, y1, x1 + tw, y1 + th, 0, 0, w, h,
            GL_COLOR_BUFFER_BIT, interpolation));
        OpenGL::render_end();

        if (!progression.running() && (progression - 1 <= 0.01))
        {
            unset_hook();
        }
    };

    void unset_hook()
    {
        output->render->set_redraw_always(false);
        output->render->rem_post(&render_hook);
        hook_set = false;
    }

    void fini() override
    {
        if (hook_set)
        {
            output->render->rem_post(&render_hook);
        }

        output->rem_binding(&axis);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_zoom_screen>);

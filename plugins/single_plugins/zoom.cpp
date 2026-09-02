#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render.hpp>
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

    double zoom_center_x = 0;
    double zoom_center_y = 0;
    bool zoom_center_set = false;

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
        target = wf::clamp(target, 1.0f, 50.0f);

        if (!zoom_center_set && target > 1.0f)
        {
            auto oc = output->get_cursor_position();

            double x, y;
            wlr_box b = wf::to_integer_box(output->get_relative_geometry());
            wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

            wf::geometry_t box = {x, y, 1, 1};
            box = output->render->get_target_framebuffer()
                .framebuffer_geometry_from_geometry_box(box);

            zoom_center_x = box.x;
            zoom_center_y = box.y;
            zoom_center_set = true;
        }

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

        if (ev->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            return false;
        }

        update_zoom_target(ev->delta);

        return true;
    };

    wf::post_hook_t render_hook = [=] (wf::auxilliary_buffer_t& source,
                                       const wf::render_buffer_t& destination)
    {
        auto w = destination.get_size().width;
        auto h = destination.get_size().height;

        if ((w <= 0) || (h <= 0))
        {
            LOGE("Invalid output size in zoom plugin!");
            return;
        }

        const float factor = (float)progression;
        const float scale  = (factor - 1) / factor;

        const float x = zoom_center_x;
        const float y = zoom_center_y;

        const float x1 = std::clamp(
            x * scale,
            0.0f,
            w - 1.0f);

        const float y1 = std::clamp(
            y * scale,
            0.0f,
            h - 1.0f);

        const float tw = std::clamp(
            w / factor,
            0.0f,
            w - x1);

        const float th = std::clamp(
            h / factor,
            0.0f,
            h - y1);

        auto filter_mode =
            (interpolation_method == (int)interpolation_method_t::NEAREST) ?
            WLR_SCALE_FILTER_NEAREST :
            WLR_SCALE_FILTER_BILINEAR;

        destination.blit(
            source,
            {x1, y1, tw, th},
            {0.0, 0.0, (double)w, (double)h},
            filter_mode);

        if (!progression.running() && (progression - 1 <= 0.01))
        {
            unset_hook();
            zoom_center_set = false;
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

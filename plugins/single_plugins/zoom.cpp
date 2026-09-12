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
    wf::option_wrapper_t<wf::activatorbinding_t> lock{"zoom/lock"};
    wf::option_wrapper_t<double> speed{"zoom/speed"};
    wf::option_wrapper_t<wf::animation_description_t> smoothing_duration{"zoom/smoothing_duration"};
    wf::option_wrapper_t<bool> edge{"zoom/edge"};
    wf::option_wrapper_t<int> interpolation_method{"zoom/interpolation_method"};

    wf::animation::simple_animation_t progression{smoothing_duration};

    bool hook_set = false;
    bool locked   = false;
    wf::pointf_t lock_point;

    wf::plugin_activation_data_t grab_interface = {
        .name = "zoom",
        .capabilities = 0,
    };

  public:
    void init() override
    {
        progression.set(1, 1);

        output->add_axis(modifier, &axis);
        output->add_activator(lock, &lock_binding);
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

        if (ev->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            return false;
        }

        update_zoom_target(ev->delta);

        return true;
    };

    wf::activator_callback lock_binding = [&] (const wf::activator_data_t&)
    {
        locked = !locked;

        if (locked)
        {
            lock_point = output->get_cursor_position();
        }

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

        auto cur_pos = output->get_cursor_position();
        auto oc = locked ? lock_point : cur_pos;
        double x, y;
        wlr_box b = wf::to_integer_box(output->get_relative_geometry());
        wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

        /* get rotation & scale */
        wf::geometry_t box = {x, y, 1, 1};
        box = output->render->get_target_framebuffer().framebuffer_geometry_from_geometry_box(box);
        x   = box.x;
        y   = box.y;

        // Store progression once to avoid its value changing in subsequent calls, could be very tricky due to
        // timing. And if we use slightly different progressions, we can get an invalid rect.
        const float factor = (float)progression;
        const float scale  = (factor - 1) / factor;
        float x1 = float(x * scale);
        float y1 = float(y * scale);
        float tw = w / factor;
        float th = h / factor;

        if (locked && edge)
        {
            wlr_box_closest_point(&b, cur_pos.x, cur_pos.y, &cur_pos.x, &cur_pos.y);
            wf::geometry_t box = {cur_pos.x, cur_pos.y, 1, 1};
            box = output->render->get_target_framebuffer().framebuffer_geometry_from_geometry_box(box);
            cur_pos.x = box.x;
            cur_pos.y = box.y;

            if (cur_pos.x < x1)
            {
                lock_point.x -= x1 - cur_pos.x;
                x1 = cur_pos.x;
            } else if (cur_pos.x > x1 + tw)
            {
                lock_point.x += cur_pos.x - (x1 + tw);
                x1 = cur_pos.x - tw;
            }

            if (cur_pos.y < y1)
            {
                lock_point.y -= y1 - cur_pos.y;
                y1 = cur_pos.y;
            } else if (cur_pos.y > y1 + th)
            {
                lock_point.y += cur_pos.y - (y1 + th);
                y1 = cur_pos.y - th;
            }
        }

        x1 = std::clamp(x1, 0.0f, w - 1.0f);
        y1 = std::clamp(y1, 0.0f, h - 1.0f);
        tw = std::clamp(tw, 0.0f, w - x1);
        th = std::clamp(th, 0.0f, h - y1);

        auto filter_mode = (interpolation_method == (int)interpolation_method_t::NEAREST) ?
            WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR;
        destination.blit(source, {x1, y1, tw, th}, {0.0, 0.0, (double)w, (double)h}, filter_mode);
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
        locked   = false;
    }

    void fini() override
    {
        if (hook_set)
        {
            output->render->rem_post(&render_hook);
        }

        output->rem_binding(&axis);
        output->rem_binding(&lock_binding);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_zoom_screen>);

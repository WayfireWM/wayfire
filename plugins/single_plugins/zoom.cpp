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
    bool locked = false;
    bool has_locked_region = false;
    wf::geometry_t locked_region;

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
        has_locked_region = false;
        return true;
    };

    wf::post_hook_t render_hook = [&] (
        wf::auxilliary_buffer_t& source,
        const wf::render_buffer_t& destination)
    {
        auto w = destination.get_size().width;
        auto h = destination.get_size().height;
        if ((w <= 0) || (h <= 0))
        {
            LOGE("Invalid output size in zoom plugin!");
            return;
        }

        auto oc = output->get_cursor_position();
        double x, y;
        wlr_box b = wf::to_integer_box(output->get_relative_geometry());
        wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

        /* get rotation & scale */
        wf::geometry_t box = {x, y, 1, 1};
        box = output->render->get_target_framebuffer().
            framebuffer_geometry_from_geometry_box(box);
        x = box.x;
        y = box.y;

        // Store progression once to avoid its value changing in subsequent
        // calls, which could result in an invalid rect.
        const float factor = (float)progression;
        const float scale  = (factor - 1) / factor;
        const float x1     = std::clamp(float(x * scale), 0.0f, w - 1.0f);
        const float y1     = std::clamp(float(y * scale), 0.0f, h - 1.0f);
        const float tw     = std::clamp(w / factor, 0.0f, w - x1);
        const float th     = std::clamp(h / factor, 0.0f, h - y1);

        wf::geometry_t source_box{x1, y1, tw, th};

        if (locked)
        {
            if (!has_locked_region)
            {
                locked_region = source_box;
                has_locked_region = true;
            } else
            {
                const double center_x =
                    locked_region.x + locked_region.width / 2.0;
                const double center_y =
                    locked_region.y + locked_region.height / 2.0;

                locked_region.width =
                    std::clamp((double)w / factor, 0.0, (double)w);
                locked_region.height =
                    std::clamp((double)h / factor, 0.0, (double)h);

                locked_region.x = std::clamp(
                    center_x - locked_region.width / 2.0,
                    0.0,
                    w - locked_region.width);

                locked_region.y = std::clamp(
                    center_y - locked_region.height / 2.0,
                    0.0,
                    h - locked_region.height);

                if (edge && !progression.running())
                {
                    if (x < locked_region.x)
                    {
                        locked_region.x = x;
                    } else if (x > locked_region.x + locked_region.width)
                    {
                        locked_region.x = x - locked_region.width;
                    }

                    if (y < locked_region.y)
                    {
                        locked_region.y = y;
                    } else if (y > locked_region.y + locked_region.height)
                    {
                        locked_region.y = y - locked_region.height;
                    }

                    locked_region.x = std::clamp(
                        locked_region.x,
                        0.0,
                        w - locked_region.width);

                    locked_region.y = std::clamp(
                        locked_region.y,
                        0.0,
                        h - locked_region.height);
                }
            }

            source_box = locked_region;
        }

        auto filter_mode =
            (interpolation_method ==
             (int)interpolation_method_t::NEAREST) ?
            WLR_SCALE_FILTER_NEAREST :
            WLR_SCALE_FILTER_BILINEAR;

        destination.blit(
            source,
            source_box,
            {0.0, 0.0, (double)w, (double)h},
            filter_mode);

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
        locked = false;
        has_locked_region = false;
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

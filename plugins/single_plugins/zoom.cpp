#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

class wayfire_zoom_screen : public wf::per_output_plugin_instance_t
{
    enum class interpolation_method_t
    {
        LINEAR  = 0,
        NEAREST = 1,
    };

    wf::option_wrapper_t<wf::keybinding_t> modifier{"zoom/modifier"};
    wf::option_wrapper_t<wf::activatorbinding_t> lock{"zoom/lock"};
    wf::option_wrapper_t<bool> centered{"zoom/centered"};
    wf::option_wrapper_t<double> speed{"zoom/speed"};
    wf::option_wrapper_t<wf::animation_description_t> smoothing_duration{"zoom/smoothing_duration"};
    wf::option_wrapper_t<bool> edge{"zoom/edge"};
    wf::option_wrapper_t<int> interpolation_method{"zoom/interpolation_method"};

    wf::animation::simple_animation_t progression{smoothing_duration};
    wf::animation::simple_animation_t lock_transition{wf::create_option<int>(500)};
    wf::pointf_t oc;

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
        lock_transition.set(1, 1);

        output->add_axis(modifier, &axis);
    }

    wf::pointf_t get_centered_zoom_point(wf::pointf_t cursor, double factor)
    {
        auto geometry = output->get_relative_geometry();
        const double scale  = (factor - 1.0) / factor;
        const double width  = geometry.width / factor;
        const double height = geometry.height / factor;

        return {
            std::clamp(cursor.x - width / 2.0, 0.0, geometry.width - width) / scale,
            std::clamp(cursor.y - height / 2.0, 0.0, geometry.height - height) / scale,
        };
    }

    wf::pointf_t to_framebuffer(wf::pointf_t point)
    {
        wf::geometry_t box = {point.x, point.y, 1, 1};
        box = output->render->get_target_framebuffer().framebuffer_geometry_from_geometry_box(box);
        return {box.x, box.y};
    }

    static wf::pointf_t untransform_delta(wf::pointf_t delta, wl_output_transform transform)
    {
        switch (transform)
        {
          case WL_OUTPUT_TRANSFORM_NORMAL:
            return {delta.x, delta.y};

          case WL_OUTPUT_TRANSFORM_90:
            return {-delta.y, delta.x};

          case WL_OUTPUT_TRANSFORM_180:
            return {-delta.x, -delta.y};

          case WL_OUTPUT_TRANSFORM_270:
            return {delta.y, -delta.x};

          case WL_OUTPUT_TRANSFORM_FLIPPED:
            return {-delta.x, delta.y};

          case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            return {delta.y, delta.x};

          case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            return {delta.x, -delta.y};

          case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            return {-delta.y, -delta.x};
        }

        return delta;
    }

    void update_zoom_target(float delta)
    {
        double target = progression.end;
        target -= target * delta * speed;
        target  = wf::clamp(target, 1.0f, 50.0f);

        if (target != progression.end)
        {
            progression.animate(target);

            if (!hook_set)
            {
                hook_set = true;
                output->add_activator(lock, &lock_binding);
                output->render->add_post(&render_hook);
                wf::get_core().connect(&on_motion_event);
                oc = output->get_cursor_position();
            }
        }

        output->render->damage_whole();
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
        if (progression <= 1.0)
        {
            return false;
        }

        locked = !locked;

        if (locked)
        {
            if (!lock_transition.running())
            {
                oc = output->get_cursor_position();
            }

            lock_transition.set(1, 1);

            if (centered)
            {
                lock_point = get_centered_zoom_point(oc, progression);
            } else
            {
                lock_point = oc;
            }
        } else
        {
            lock_transition.set(0, 0);
            lock_transition.animate(1.0);

            output->render->damage_whole();
        }

        return true;
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_motion_event =
        [=] (wf::input_event_signal<wlr_pointer_motion_event>*)
    {
        output->render->damage_whole();
    };

    void damage_cursors()
    {
        wlr_output_cursor *cursor;
        int transformed_width, transformed_height;
        wlr_output_transformed_resolution(output->handle, &transformed_width, &transformed_height);
        wl_list_for_each(cursor, &output->handle->cursors, link)
        {
            if (!cursor->enabled || !cursor->visible ||
                (output->handle->hardware_cursor == cursor) || !cursor->texture)
            {
                continue;
            }

            wlr_box box{
                static_cast<int>(cursor->x - cursor->hotspot_x),
                static_cast<int>(cursor->y - cursor->hotspot_y),
                static_cast<int>(cursor->width),
                static_cast<int>(cursor->height),
            };
            wlr_box_transform(&box, &box,
                wlr_output_transform_invert(output->handle->transform),
                transformed_width, transformed_height);

            wf::geometry_t damage{double(box.x), double(box.y), double(box.width), double(box.height)};
            output->render->damage(damage, false);
        }
    }

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

        damage_cursors();

        auto cur_pos = output->get_cursor_position();

        const double factor = progression;
        const double scale  = (factor - 1) / factor;
        const bool lock_transition_running = lock_transition.running();
        wlr_box output_box = wf::to_integer_box(output->get_relative_geometry());

        if (locked)
        {
            oc = lock_point;
        } else if (lock_transition_running)
        {
            wf::pointf_t target = cur_pos;
            if (centered)
            {
                target = get_centered_zoom_point(cur_pos, factor);
            }

            oc.x = lock_point.x + (target.x - lock_point.x) * lock_transition;
            oc.y = lock_point.y + (target.y - lock_point.y) * lock_transition;
        } else
        {
            oc = cur_pos;
        }

        double x, y;
        wlr_box_closest_point(&output_box, oc.x, oc.y, &x, &y);

        /* get rotation & scale */
        auto framebuffer_point = to_framebuffer({x, y});
        x = framebuffer_point.x;
        y = framebuffer_point.y;

        // Store progression once to avoid its value changing in subsequent calls, could be very tricky due to
        // timing. And if we use slightly different progressions, we can get an invalid rect.
        double x1 = x * scale;
        double y1 = y * scale;
        double tw = w / factor;
        double th = h / factor;

        if (locked && edge)
        {
            wlr_box_closest_point(&output_box, cur_pos.x, cur_pos.y, &cur_pos.x, &cur_pos.y);
            cur_pos = to_framebuffer(cur_pos);

            wf::pointf_t clamped{
                std::clamp(cur_pos.x, x1, x1 + tw),
                std::clamp(cur_pos.y, y1, y1 + th),
            };
            const auto& target = output->render->get_target_framebuffer();
            auto delta = untransform_delta(cur_pos - clamped, target.wl_transform);
            delta.x    /= target.scale;
            delta.y    /= target.scale;
            lock_point += delta;
        }

        if (!locked && centered && !lock_transition_running)
        {
            x1 = std::clamp(x - tw / 2.0, 0.0, w - tw);
            y1 = std::clamp(y - th / 2.0, 0.0, h - th);
        }

        x1 = std::clamp(x1, 0.0, w - 1.0);
        y1 = std::clamp(y1, 0.0, h - 1.0);
        tw = std::clamp(tw, 1.0, w - x1);
        th = std::clamp(th, 1.0, h - y1);

        auto filter_mode = (interpolation_method == (int)interpolation_method_t::NEAREST) ?
            WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR;
        destination.blit(source, {x1, y1, tw, th}, {0.0, 0.0, (double)w, (double)h}, filter_mode);

        if (progression.running() || lock_transition_running)
        {
            output->render->damage_whole_idle();
        } else if (progression - 1.0 <= 0.0)
        {
            unset_hook();
        }
    };

    void unset_hook()
    {
        on_motion_event.disconnect();
        output->render->rem_post(&render_hook);
        output->rem_binding(&lock_binding);
        hook_set = false;
        locked   = false;
    }

    void fini() override
    {
        if (hook_set)
        {
            unset_hook();
        }

        output->rem_binding(&axis);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_zoom_screen>);

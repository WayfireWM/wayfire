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
    double target;
    wf::pointf_t oc;

    bool hook_set = false;
    bool locked = false;
    wf::pointf_t lock_point, target_point;

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
        output->add_activator(lock, &lock_binding);
    }

    bool output_transform_portrait()
    {
        auto transform = output->handle->transform;
        return
            transform == WL_OUTPUT_TRANSFORM_90 ||
            transform == WL_OUTPUT_TRANSFORM_270 ||
            transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
            transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
    }

    void update_zoom_target(float delta)
    {
        target  = progression.end;
        target -= target * delta * speed;
        target  = wf::clamp(target, 1.0f, 50.0f);

        if (target != progression.end)
        {
            progression.animate(target);

            if (!hook_set)
            {
                hook_set = true;
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
                auto og = output->get_relative_geometry();
                const double factor = progression;
                const double scale  = (factor - 1) / factor;
                double tw = og.width / factor;
                double th = og.height / factor;
                double x1 = std::max(std::min(double(oc.x), og.width - tw / 2) - tw / 2, 0.0);
                double y1 = std::max(std::min(double(oc.y), og.height - th / 2) - th / 2, 0.0);
                double x, y;
                x = x1 / scale;
                y = y1 / scale;
                lock_point = {x, y};
            } else
            {
                lock_point = oc;
            }
        } else
        {
            lock_transition.set(0, 0);
            lock_transition.animate(1.0);
            target_point = output->get_cursor_position();

            output->render->damage_whole();
        }

        return true;
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_motion_event =
        [=] (wf::input_event_signal<wlr_pointer_motion_event> *ev)
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
        wlr_box b = wf::to_integer_box(output->get_relative_geometry());

        wf::pointf_t from, to;
        if (locked)
        {
            from = target_point;
            to   = lock_point;
        } else if (lock_transition_running)
        {
            if (centered)
            {
                double tw = b.width / factor;
                double th = b.height / factor;
                double x1 = std::min(cur_pos.x, b.width - tw / 2) - tw / 2;
                double y1 = std::min(cur_pos.y, b.height - th / 2) - th / 2;
                double x, y;
                x = x1 / scale;
                y = y1 / scale;

                from = lock_point;
                to   = {x, y};
            } else
            {
                from = lock_point;
                to   = cur_pos;
            }
        } else
        {
            from = cur_pos;
            to   = cur_pos;
        }

        oc.x = from.x + (to.x - from.x) * lock_transition;
        oc.y = from.y + (to.y - from.y) * lock_transition;

        double x, y;
        wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

        /* get rotation & scale */
        wf::geometry_t box = {x, y, 1, 1};
        box = output->render->get_target_framebuffer().framebuffer_geometry_from_geometry_box(box);
        x   = box.x;
        y   = box.y;

        // Store progression once to avoid its value changing in subsequent calls, could be very tricky due to
        // timing. And if we use slightly different progressions, we can get an invalid rect.
        double x1 = double(x * scale);
        double y1 = double(y * scale);
        double tw = w / factor;
        double th = h / factor;

        if (locked && edge)
        {
            auto transform = output->handle->transform;
            wlr_box_closest_point(&b, cur_pos.x, cur_pos.y, &cur_pos.x, &cur_pos.y);
            wf::geometry_t box = {cur_pos.x, cur_pos.y, 1, 1};
            box = output->render->get_target_framebuffer().framebuffer_geometry_from_geometry_box(box);
            cur_pos.x = box.x;
            cur_pos.y = box.y;

            if (cur_pos.x < x1)
            {
                if (output_transform_portrait())
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_270) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED_270))
                    {
                        lock_point.y += x1 - cur_pos.x;
                    } else
                    {
                        lock_point.y -= x1 - cur_pos.x;
                    }
                } else
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_180) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED))
                    {
                        lock_point.x += x1 - cur_pos.x;
                    } else
                    {
                        lock_point.x -= x1 - cur_pos.x;
                    }
                }
            } else if (cur_pos.x > x1 + tw)
            {
                if (output_transform_portrait())
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_270) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED_270))
                    {
                        lock_point.y -= cur_pos.x - (x1 + tw);
                    } else
                    {
                        lock_point.y += cur_pos.x - (x1 + tw);
                    }
                } else
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_180) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED))
                    {
                        lock_point.x -= cur_pos.x - (x1 + tw);
                    } else
                    {
                        lock_point.x += cur_pos.x - (x1 + tw);
                    }
                }
            }

            if (cur_pos.y < y1)
            {
                if (output_transform_portrait())
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_270) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED_90))
                    {
                        lock_point.x -= y1 - cur_pos.y;
                    } else
                    {
                        lock_point.x += y1 - cur_pos.y;
                    }
                } else
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_180) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED_180))
                    {
                        lock_point.y += y1 - cur_pos.y;
                    } else
                    {
                        lock_point.y -= y1 - cur_pos.y;
                    }
                }
            } else if (cur_pos.y > y1 + th)
            {
                if (output_transform_portrait())
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_270) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED_90))
                    {
                        lock_point.x += cur_pos.y - (y1 + th);
                    } else
                    {
                        lock_point.x -= cur_pos.y - (y1 + th);
                    }
                } else
                {
                    if ((transform == WL_OUTPUT_TRANSFORM_180) ||
                        (transform == WL_OUTPUT_TRANSFORM_FLIPPED_180))
                    {
                        lock_point.y -= cur_pos.y - (y1 + th);
                    } else
                    {
                        lock_point.y += cur_pos.y - (y1 + th);
                    }
                }
            }
        }

        if (!locked && centered && !lock_transition_running)
        {
            x1 = std::min(x, w - tw / 2) - tw / 2;
            y1 = std::min(y, h - th / 2) - th / 2;
        }

        x1 = std::clamp(x1, double(0.0f), double(w - 1.0f));
        y1 = std::clamp(y1, double(0.0f), double(h - 1.0f));
        tw = std::clamp(tw, double(1.0f), double(w - x1));
        th = std::clamp(th, double(1.0f), double(h - y1));

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
        output->rem_binding(&lock_binding);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_zoom_screen>);

#include "wayfire/per-output-plugin.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/seat.hpp"
#include "../cube/cube-control-signal.hpp"

#include <cmath>
#include <optional>
#include <wayfire/util/duration.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#define CUBE_ZOOM_BASE 1.0

enum cube_screensaver_state
{
    CUBE_SCREENSAVER_DISABLED,
    CUBE_SCREENSAVER_RUNNING,
    CUBE_SCREENSAVER_STOPPING,
};

using namespace wf::animation;
class screensaver_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t rot{*this};
    timed_transition_t zoom{*this};
    timed_transition_t ease{*this};
};

class wayfire_idle
{
    wf::option_wrapper_t<int> dpms_timeout{"idle/dpms_timeout"};
    bool is_idle = false;

  public:
    wf::signal::connection_t<wf::seat_activity_signal> on_seat_activity;
    std::optional<wf::idle_inhibitor_t> hotkey_inhibitor;
    wf::wl_timer<false> timeout_dpms;

    wayfire_idle()
    {
        dpms_timeout.set_callback([=] ()
        {
            create_dpms_timeout();
        });

        on_seat_activity = [=] (void*)
        {
            create_dpms_timeout();
        };
        create_dpms_timeout();
        wf::get_core().connect(&on_seat_activity);
    }

    void create_dpms_timeout()
    {
        if (dpms_timeout <= 0)
        {
            timeout_dpms.disconnect();
            return;
        }

        if (!timeout_dpms.is_connected() && is_idle)
        {
            is_idle = false;
            set_state(wf::OUTPUT_IMAGE_SOURCE_DPMS, wf::OUTPUT_IMAGE_SOURCE_SELF);

            return;
        }

        timeout_dpms.disconnect();
        timeout_dpms.set_timeout(1000 * dpms_timeout, [=] ()
        {
            is_idle = true;
            set_state(wf::OUTPUT_IMAGE_SOURCE_SELF, wf::OUTPUT_IMAGE_SOURCE_DPMS);
        });
    }

    ~wayfire_idle()
    {
        timeout_dpms.disconnect();
        wf::get_core().disconnect(&on_seat_activity);
    }

    /* Change all outputs with state from to state to */
    void set_state(wf::output_image_source_t from, wf::output_image_source_t to)
    {
        auto config = wf::get_core().output_layout->get_current_configuration();

        for (auto& entry : config)
        {
            if (entry.second.source == from)
            {
                entry.second.source = to;
            }
        }

        wf::get_core().output_layout->apply_configuration(config);
    }
};

class wayfire_idle_plugin : public wf::per_output_plugin_instance_t
{
    double rotation = 0.0;

    wf::option_wrapper_t<int> zoom_speed{"idle/cube_zoom_speed"};
    screensaver_animation_t screensaver_animation{zoom_speed};
    wf::option_wrapper_t<int> screensaver_timeout{"idle/screensaver_timeout"};
    wf::option_wrapper_t<double> cube_rotate_speed{"idle/cube_rotate_speed"};
    wf::option_wrapper_t<double> cube_max_zoom{"idle/cube_max_zoom"};
    wf::option_wrapper_t<bool> disable_on_fullscreen{"idle/disable_on_fullscreen"};
    wf::option_wrapper_t<bool> disable_initially{"idle/disable_initially"};

    std::optional<wf::idle_inhibitor_t> fullscreen_inhibitor;
    bool has_fullscreen = false;

    cube_screensaver_state state = CUBE_SCREENSAVER_DISABLED;
    bool hook_set = false;
    bool output_inhibited = false;
    uint32_t last_time;
    wf::wl_timer<false> timeout_screensaver;
    wf::signal::connection_t<wf::seat_activity_signal> on_seat_activity;
    wf::shared_data::ref_ptr_t<wayfire_idle> global_idle;

    wf::activator_callback toggle = [=] (auto)
    {
        if (global_idle->hotkey_inhibitor.has_value())
        {
            global_idle->hotkey_inhibitor.reset();
        } else
        {
            global_idle->hotkey_inhibitor.emplace();
        }

        return true;
    };

    wf::signal::connection_t<wf::fullscreen_layer_focused_signal> fullscreen_state_changed =
        [=] (wf::fullscreen_layer_focused_signal *ev)
    {
        this->has_fullscreen = ev->has_promoted;
        update_fullscreen();
    };

    wf::signal::connection_t<wf::idle_inhibit_changed_signal> inhibit_changed =
        [=] (wf::idle_inhibit_changed_signal *ev)
    {
        if (!ev)
        {
            return;
        }

        if (ev->inhibit)
        {
            wf::get_core().disconnect(&global_idle->on_seat_activity);
            wf::get_core().disconnect(&on_seat_activity);
            global_idle->timeout_dpms.disconnect();
            timeout_screensaver.disconnect();
        } else
        {
            wf::get_core().connect(&global_idle->on_seat_activity);
            wf::get_core().connect(&on_seat_activity);
            global_idle->create_dpms_timeout();
            create_screensaver_timeout();
        }
    };

    wf::config::option_base_t::updated_callback_t disable_on_fullscreen_changed =
        [=] ()
    {
        update_fullscreen();
    };

    void update_fullscreen()
    {
        bool want = disable_on_fullscreen && has_fullscreen;
        if (want && !fullscreen_inhibitor.has_value())
        {
            fullscreen_inhibitor.emplace();
        }

        if (!want && fullscreen_inhibitor.has_value())
        {
            fullscreen_inhibitor.reset();
        }
    }

    wf::plugin_activation_data_t grab_interface = {
        .name = "idle",
        .capabilities = 0,
    };

  public:
    void init() override
    {
        if (disable_initially)
        {
            global_idle->hotkey_inhibitor.emplace();
        }

        output->add_activator(wf::option_wrapper_t<wf::activatorbinding_t>{"idle/toggle"}, &toggle);
        output->connect(&fullscreen_state_changed);
        disable_on_fullscreen.set_callback(disable_on_fullscreen_changed);

        if (auto toplevel = toplevel_cast(wf::get_active_view_for_output(output)))
        {
            /* Currently, the fullscreen count would always be 0 or 1,
             * since fullscreen-layer-focused is only emitted on changes between 0
             * and 1
             **/
            has_fullscreen = toplevel->pending_fullscreen();
        }

        update_fullscreen();

        screensaver_timeout.set_callback([=] ()
        {
            create_screensaver_timeout();
        });
        create_screensaver_timeout();

        on_seat_activity = [=] (void*)
        {
            create_screensaver_timeout();
        };
        wf::get_core().connect(&on_seat_activity);
        wf::get_core().connect(&inhibit_changed);
    }

    void create_screensaver_timeout()
    {
        if (screensaver_timeout <= 0)
        {
            timeout_screensaver.disconnect();
            return;
        }

        if (!timeout_screensaver.is_connected() && output_inhibited)
        {
            uninhibit_output();
            return;
        }

        if (!timeout_screensaver.is_connected() && (state == CUBE_SCREENSAVER_RUNNING))
        {
            stop_screensaver();
            return;
        }

        timeout_screensaver.disconnect();
        timeout_screensaver.set_timeout(1000 * screensaver_timeout, [=] ()
        {
            start_screensaver();
        });
    }

    void inhibit_output()
    {
        if (output_inhibited)
        {
            return;
        }

        if (hook_set)
        {
            output->render->rem_effect(&screensaver_frame);
            hook_set = false;
        }

        output->render->add_inhibit(true);
        output->render->damage_whole();
        state = CUBE_SCREENSAVER_DISABLED;
        output_inhibited = true;
    }

    void uninhibit_output()
    {
        if (!output_inhibited)
        {
            return;
        }

        output->render->add_inhibit(false);
        output->render->damage_whole();
        output_inhibited = false;
    }

    void screensaver_terminate()
    {
        cube_control_signal data;
        data.angle = 0.0;
        data.zoom  = CUBE_ZOOM_BASE;
        data.ease  = 0.0;
        data.last_frame  = true;
        data.carried_out = false;

        output->emit(&data);
        if (hook_set)
        {
            output->render->rem_effect(&screensaver_frame);
            hook_set = false;
        }

        if (state == CUBE_SCREENSAVER_DISABLED)
        {
            uninhibit_output();
        }

        state = CUBE_SCREENSAVER_DISABLED;
    }

    wf::effect_hook_t screensaver_frame = [=] ()
    {
        cube_control_signal data;
        uint32_t current = wf::get_current_time();
        uint32_t elapsed = current - last_time;

        last_time = current;

        if ((state == CUBE_SCREENSAVER_STOPPING) && !screensaver_animation.running())
        {
            screensaver_terminate();

            return;
        }

        if (state == CUBE_SCREENSAVER_STOPPING)
        {
            rotation = screensaver_animation.rot;
        } else
        {
            rotation += (cube_rotate_speed / 5000.0) * elapsed;
        }

        if (rotation > M_PI * 2)
        {
            rotation -= M_PI * 2;
        }

        data.angle = rotation;
        data.zoom  = screensaver_animation.zoom;
        data.ease  = screensaver_animation.ease;
        data.last_frame  = false;
        data.carried_out = false;

        output->emit(&data);
        if (!data.carried_out)
        {
            screensaver_terminate();

            return;
        }

        if (state == CUBE_SCREENSAVER_STOPPING)
        {
            wf::get_core().seat->notify_activity();
        }
    };

    void start_screensaver()
    {
        cube_control_signal data;
        data.angle = 0.0;
        data.zoom  = CUBE_ZOOM_BASE;
        data.ease  = 0.0;
        data.last_frame  = false;
        data.carried_out = false;

        output->emit(&data);
        if (data.carried_out)
        {
            if (!hook_set)
            {
                output->render->add_effect(
                    &screensaver_frame, wf::OUTPUT_EFFECT_PRE);
                hook_set = true;
            }
        } else if (state == CUBE_SCREENSAVER_DISABLED)
        {
            inhibit_output();

            return;
        }

        state = CUBE_SCREENSAVER_RUNNING;

        rotation = 0.0;
        screensaver_animation.zoom.set(CUBE_ZOOM_BASE, cube_max_zoom);
        screensaver_animation.ease.set(0.0, 1.0);
        screensaver_animation.start();
        last_time = wf::get_current_time();
    }

    void stop_screensaver()
    {
        if (state == CUBE_SCREENSAVER_DISABLED)
        {
            uninhibit_output();

            return;
        }

        state = CUBE_SCREENSAVER_STOPPING;

        double end = rotation > M_PI ? M_PI * 2 : 0.0;
        screensaver_animation.rot.set(rotation, end);
        screensaver_animation.zoom.restart_with_end(CUBE_ZOOM_BASE);
        screensaver_animation.ease.restart_with_end(0.0);
        screensaver_animation.start();
    }

    void fini() override
    {
        wf::get_core().disconnect(&on_seat_activity);
        wf::get_core().disconnect(&inhibit_changed);
        timeout_screensaver.disconnect();
        output->rem_binding(&toggle);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_idle_plugin>);

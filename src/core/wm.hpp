#ifndef WM_H
#define WM_H

#include "wayfire/plugin.hpp"
#include "wayfire/per-output-plugin.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view.hpp"
#include "wayfire/touch/touch.hpp"
#include "wayfire/option-wrapper.hpp"

class wayfire_close : public wf::per_output_plugin_instance_t
{
    wf::activator_callback callback;

    wf::plugin_activation_data_t grab_interface = {
        .name = "builtin-close-view",
        .capabilities = wf::CAPABILITY_GRAB_INPUT,
    };

  public:
    void init() override;
    void fini() override;
};

class wayfire_focus : public wf::plugin_interface_t
{
    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_pointer_button;
    std::unique_ptr<wf::touch::gesture_t> tap_gesture;

    // @return True if the focus has changed
    bool check_focus_surface(wayfire_view view);

    wf::option_wrapper_t<bool> focus_modifiers{"core/focus_button_with_modifiers"};
    wf::option_wrapper_t<bool> pass_btns{"core/focus_buttons_passthrough"};
    wf::option_wrapper_t<wf::activatorbinding_t> focus_btns{"core/focus_buttons"};

    wf::plugin_activation_data_t grab_interface = {
        .name = "_wf_focus",
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP,
    };

  public:
    void init() override;
    void fini() override;
};

class wayfire_exit : public wf::per_output_plugin_instance_t
{
    wf::key_callback key;

  public:
    void init() override;
    void fini() override;
};

#endif

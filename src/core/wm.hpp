#ifndef WM_H
#define WM_H

#include "wayfire/plugin.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/view.hpp"
#include "wayfire/touch/touch.hpp"
#include "wayfire/option-wrapper.hpp"

struct wm_focus_request : public wf::signal_data_t
{
    wf::scene::node_ptr node;
};

class wayfire_close : public wf::plugin_interface_t
{
    wf::activator_callback callback;

  public:
    void init() override;
    void fini() override;
};

class wayfire_focus : public wf::plugin_interface_t
{
    wf::signal_connection_t on_button;
    wf::signal_connection_t on_wm_focus_request;

    std::unique_ptr<wf::touch::gesture_t> tap_gesture;

    // @return True if the focus has changed
    bool check_focus_surface(wayfire_view view);

    wf::option_wrapper_t<bool> focus_modifiers{"core/focus_button_with_modifiers"};
    wf::option_wrapper_t<bool> pass_btns{"core/focus_buttons_passthrough"};
    wf::option_wrapper_t<wf::activatorbinding_t> focus_btns{"core/focus_buttons"};

  public:
    void init() override;
    void fini() override;
};

class wayfire_exit : public wf::plugin_interface_t
{
    wf::key_callback key;

  public:
    void init() override;
    void fini() override;
};
#endif

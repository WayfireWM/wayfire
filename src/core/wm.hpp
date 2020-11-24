#ifndef WM_H
#define WM_H

#include "wayfire/plugin.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/view.hpp"
#include "wayfire/touch/touch.hpp"
#include "wayfire/option-wrapper.hpp"

struct wm_focus_request : public wf::signal_data_t
{
    wf::surface_interface_t *surface;
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
    wf::signal_callback_t on_wm_focus_request;

    std::unique_ptr<wf::touch::gesture_t> tap_gesture;
    void check_focus_surface(wf::surface_interface_t *surface);

    wf::option_wrapper_t<bool> focus_modifiers{"core/focus_btn_mod"};
    wf::option_wrapper_t<bool> focus_btn_middle{"core/focus_btn_middle"};
    wf::option_wrapper_t<bool> focus_btn_right{"core/focus_btn_right"};

  public:
    void init() override;
    void fini() override;
};

class wayfire_exit : public wf::plugin_interface_t
{
    wf::key_callback key;
    wf::signal_connection_t on_output_removed;

  public:
    void init() override;
    void fini() override;
};
#endif

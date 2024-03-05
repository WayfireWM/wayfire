#pragma once
#include "wayfire/util.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/view.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/unstable/wlr-text-input-v3-popup.hpp>

#include <vector>
#include <memory>

namespace wf
{
struct text_input;

class input_method_relay : public text_input_v3_im_relay_interface_t
{
  private:

    wf::wl_listener_wrapper on_text_input_new,
        on_input_method_new, on_input_method_commit, on_input_method_destroy,
        on_grab_keyboard, on_grab_keyboard_destroy, on_new_popup_surface;
    wlr_input_method_keyboard_grab_v2 *keyboard_grab = nullptr;

    std::optional<uint32_t> last_done_serial;
    uint32_t next_done_serial = 0;
    void send_im_done();

    text_input *find_focusable_text_input();
    void set_focus(wlr_surface*);

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> keyboard_focus_changed =
        [=] (wf::keyboard_focus_changed_signal *ev)
    {
        if (auto view = wf::node_to_view(ev->new_focus))
        {
            set_focus(view->get_wlr_surface());
        } else
        {
            set_focus(nullptr);
        }
    };

    bool should_grab(wlr_keyboard*);

  public:

    wlr_input_method_v2 *input_method = nullptr;
    std::vector<std::unique_ptr<text_input>> text_inputs;
    std::vector<std::shared_ptr<text_input_v3_popup>> popup_surfaces;

    input_method_relay();
    void send_im_state(wlr_text_input_v3*);
    text_input *find_focused_text_input();
    wlr_text_input_v3 *find_focused_text_input_v3() override;
    void disable_text_input(wlr_text_input_v3*);
    void remove_text_input(wlr_text_input_v3*);
    void remove_popup_surface(text_input_v3_popup*);
    bool handle_key(struct wlr_keyboard*, uint32_t time, uint32_t key, uint32_t state);
    bool handle_modifier(struct wlr_keyboard*);
    bool is_im_sent(struct wlr_keyboard*);
    ~input_method_relay();
};

struct text_input
{
    input_method_relay *relay = nullptr;
    wlr_text_input_v3 *input  = nullptr;
    /* A place to keep the focused surface when no input method exists
     * (when the IM returns, it would get that surface instantly) */
    wlr_surface *pending_focused_surface = nullptr;
    wf::wl_listener_wrapper on_pending_focused_surface_destroy,
        on_text_input_enable, on_text_input_commit,
        on_text_input_disable, on_text_input_destroy;

    text_input(input_method_relay*, wlr_text_input_v3*);
    void set_pending_focused_surface(wlr_surface*);
    ~text_input();
};
}

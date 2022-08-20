#pragma once
#include "../src/core/core-impl.hpp"

class mock_core_t : public wf::compositor_core_impl_t
{
  public:
    std::unordered_map<std::string, wayfire_view> fake_views;

    void init() override;
    void post_init() override;

    void erase_view(wayfire_view view) override;
    wayfire_view find_view(const std::string& id) override;

    static compositor_core_impl_t& get();

    wlr_seat *get_current_seat() override;
    uint32_t get_keyboard_modifiers() override;
    void warp_cursor(wf::pointf_t pos) override;
    void set_cursor(std::string name) override;
    void unhide_cursor() override;
    void hide_cursor() override;

    wf::pointf_t get_cursor_position() override;
    wf::pointf_t get_touch_position(int id) override;
    const wf::touch::gesture_state_t& get_touch_state() override;

    wf::scene::node_ptr get_cursor_focus() override;
    wf::scene::node_ptr get_touch_focus() override;
    wf::surface_interface_t *get_surface_at(wf::pointf_t point) override;

    void add_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) override;
    void rem_touch_gesture(
        nonstd::observer_ptr<wf::touch::gesture_t> gesture) override;

    std::vector<nonstd::observer_ptr<wf::input_device_t>> get_input_devices()
    override;
    virtual wlr_cursor *get_wlr_cursor() override;

    void add_view(std::unique_ptr<wf::view_interface_t> view) override;
    std::vector<wayfire_view> get_all_views() override;
    void set_active_view(wayfire_view v) override;
    void focus_view(wayfire_view win) override;
    void move_view_to_output(wayfire_view v, wf::output_t *new_output,
        bool reconfigure) override;

    void focus_output(wf::output_t *o) override;
    wf::output_t *get_active_output() override;
    int focus_layer(uint32_t layer, int request) override;
    void unfocus_layer(int request) override;
    uint32_t get_focused_layer() override;
    std::string get_xwayland_display() override;
    pid_t run(std::string command) override;
    void shutdown() override;
    wf::compositor_state_t get_current_state() override;

    mock_core_t();
    virtual ~mock_core_t();
};

mock_core_t& mock_core();

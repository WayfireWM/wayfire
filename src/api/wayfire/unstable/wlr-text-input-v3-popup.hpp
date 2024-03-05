#pragma once

#include <wayfire/unstable/wlr-surface-node.hpp>
#include <wayfire/unstable/translation-node.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/view.hpp>

namespace wf
{
class text_input_v3_im_relay_interface_t
{
  public:
    virtual wlr_text_input_v3 *find_focused_text_input_v3() = 0;
};

/**
 * A view implementation which presents an input-method popup-like surface relative to a text-input-v3 cursor.
 */
class text_input_v3_popup : public wf::view_interface_t
{
  public:
    text_input_v3_im_relay_interface_t *relay = nullptr;
    wlr_surface *surface = nullptr;

    text_input_v3_popup(text_input_v3_im_relay_interface_t *relay, wlr_surface *surface);
    static std::shared_ptr<text_input_v3_popup> create(text_input_v3_im_relay_interface_t*, wlr_surface*);

    bool is_mapped() const override;
    std::string get_app_id() override;
    std::string get_title() override;
    wf::geometry_t get_geometry();
    void map();
    void unmap();
    void update_geometry();
    ~text_input_v3_popup();

  private:
    wf::geometry_t geometry{0, 0, 0, 0};
    std::shared_ptr<wf::scene::wlr_surface_node_t> main_surface;
    std::shared_ptr<wf::scene::translation_node_t> surface_root_node;
    bool _is_mapped = false;

    virtual wlr_surface *get_keyboard_focus_surface() override
    {
        return nullptr;
    }

    wf::wl_listener_wrapper on_map;
    wf::wl_listener_wrapper on_unmap;
    wf::wl_listener_wrapper on_commit;
    wf::wl_listener_wrapper on_surface_destroy;

  public:
    // This is only a convenience wrapper for the users of this class.
    wf::wl_listener_wrapper on_destroy;
};
}

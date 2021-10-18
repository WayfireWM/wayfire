#pragma once

#include <map>
#include <wayfire/opengl.hpp>
#include <wayfire/surface.hpp>
#include <wayfire/util.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

namespace wf
{
class surface_interface_t::impl
{
  public:
    surface_interface_t *parent_surface;
    std::vector<std::unique_ptr<surface_interface_t>> surface_children_above;
    std::vector<std::unique_ptr<surface_interface_t>> surface_children_below;
    size_t last_cnt_surfaces = 0;

    /**
     * Remove all subsurfaces and emit signals for them.
     */
    void clear_subsurfaces(surface_interface_t *self);

    wf::output_t *output = nullptr;
    static int active_shrink_constraint;
};

/**
 * An implementation of surface_interface_t for wlr_surface-based surfaces.
 */
class wlr_surface_base_t : public wf::surface_interface_t,
        input_surface_t, public output_surface_t
{
  protected:
    wf::wl_listener_wrapper::callback_t handle_new_subsurface;
    wf::wl_listener_wrapper on_commit, on_destroy, on_new_subsurface;

    void apply_surface_damage();

    bool mapped = false;
    std::map<wf::output_t*, int> visibility;

  public:
    wlr_surface *surface;
    wlr_surface_base_t(wlr_surface *surface);

    /** Set the surface state to mapped. */
    void map();

    /** Set the surface state to unmapped. */
    void unmap();

    void handle_commit();

    virtual ~wlr_surface_base_t();

    wlr_surface_base_t(const wlr_surface_base_t &) = delete;
    wlr_surface_base_t(wlr_surface_base_t &&) = delete;
    wlr_surface_base_t& operator =(const wlr_surface_base_t&) = delete;
    wlr_surface_base_t& operator =(wlr_surface_base_t&&) = delete;

    /** Implementation of input_surface_t */
    wlr_pointer_constraint_v1 *current_constraint = NULL;
    wf::wl_listener_wrapper on_current_constraint_destroy;

    bool accepts_input(wf::pointf_t at) final;
    std::optional<wf::region_t> handle_pointer_enter(wf::pointf_t at,
        bool reenter) final;
    void handle_pointer_leave() final;
    void handle_pointer_button(uint32_t time_ms, uint32_t button,
        wlr_button_state state) final;
    void handle_pointer_motion(uint32_t time_ms, wf::pointf_t at) final;
    void handle_pointer_axis(uint32_t time_ms,
        wlr_axis_orientation orientation, double delta,
        int32_t delta_discrete, wlr_axis_source source) final;

    void handle_touch_down(uint32_t time_ms, int32_t id, wf::pointf_t at) final;
    void handle_touch_up(uint32_t time_ms, int32_t id, bool finger_lifted) final;
    void handle_touch_motion(uint32_t time_ms, int32_t id, wf::pointf_t at) final;

    /** Implementation of output_surface_t */
    wf::point_t get_offset() override;
    wf::dimensions_t get_size() const final;
    void schedule_redraw(const timespec& frame_end) final;
    void set_visible_on_output(wf::output_t *output, bool is_visible) override;
    wf::region_t get_opaque_region() final;
    void simple_render(const wf::framebuffer_t& fb, wf::point_t pos,
        const wf::region_t& damage) final;

    /** Implementation of surface_interface_t */
    bool is_mapped() const final;
    input_surface_t& input() final;
    output_surface_t& output() final;
    wlr_surface *get_wlr_surface() final;
};
}

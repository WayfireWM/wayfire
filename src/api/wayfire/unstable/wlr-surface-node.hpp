#pragma once

#include "wayfire/geometry.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view-transform.hpp"
#include <wayfire/scene.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/output-layout.hpp>

namespace wf
{
namespace scene
{
struct surface_state_t
{
    // The surface state struct keeps a lock on the wlr_buffer, so that it is always valid as long as the
    // state is current.
    wlr_buffer *current_buffer = nullptr;
    wlr_texture *texture; // The texture of the wlr_client_buffer

    wf::region_t accumulated_damage;
    wf::dimensions_t size = {0, 0};
    std::optional<wlr_fbox> src_viewport;
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;

    // Read the current surface state, get a lock on the current surface buffer (releasing any old locks),
    // and accumulate damage.
    void merge_state(wlr_surface *surface);

    surface_state_t() = default;

    // Releases the lock on the current_buffer, if one is held.
    ~surface_state_t();

    surface_state_t(const surface_state_t& other) = delete;
    surface_state_t& operator =(const surface_state_t& other) = delete;
    surface_state_t(surface_state_t&& other);
    surface_state_t& operator =(surface_state_t&& other);
};

/**
 * An implementation of node_t for wlr_surfaces.
 *
 * The node typically does not have children and displays a single surface. It is assumed that the surface is
 * positioned at (0, 0), which means this node usually should be put with a parent node which manages the
 * position in the scenegraph.
 */
class wlr_surface_node_t : public node_t, public zero_copy_texturable_node_t
{
  public:
    /**
     * @param autocommit Whether the surface should automatically apply new surface state on surface commit,
     *   or it should wait until it is manually applied.
     */
    wlr_surface_node_t(wlr_surface *surface, bool autocommit);

    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override;

    std::string stringify() const override;
    pointer_interaction_t& pointer_interaction() override;
    touch_interaction_t& touch_interaction() override;

    void gen_render_instances(std::vector<render_instance_uptr>& instances, damage_callback damage,
        wf::output_t *output) override;
    wf::geometry_t get_bounding_box() override;
    std::optional<wf::texture_t> to_texture() const override;

    wlr_surface *get_surface() const;
    void apply_state(surface_state_t&& state);
    void send_frame_done(bool delay_until_vblank);

  private:
    std::unique_ptr<pointer_interaction_t> ptr_interaction;
    std::unique_ptr<touch_interaction_t> tch_interaction;
    wlr_surface *surface;

    std::map<wf::output_t*, int> visibility;
    std::map<wf::output_t*, int> pending_visibility_delta;
    wf::signal::connection_t<wf::output_removed_signal> on_output_remove;

    class wlr_surface_render_instance_t;
    void handle_enter(wf::output_t *output);
    void handle_leave(wf::output_t *output);
    void update_pending_outputs();
    wf::wl_idle_call idle_update_outputs;

    wf::wl_listener_wrapper on_surface_destroyed;
    wf::wl_listener_wrapper on_surface_commit;

    const bool autocommit;
    surface_state_t current_state;
    void apply_current_surface_state();
};
}
}

#include "layer-shell-node.hpp"
#include "../view-keyboard-interaction.hpp"
#include "wayfire/scene-input.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/seat-impl.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/unstable/translation-node.hpp"
#include <wayfire/workspace-set.hpp>

wf::layer_shell_node_t::layer_shell_node_t(wayfire_view view) : view_node_tag_t(view)
{
    this->kb_interaction = std::make_unique<view_keyboard_interaction_t>(view);
    on_view_destroy = [=] (view_destruct_signal *ev)
    {
        this->view = nullptr;
        this->kb_interaction = std::make_unique<keyboard_interaction_t>();
    };

    view->connect(&on_view_destroy);
    this->view = view;
}

std::string wf::layer_shell_node_t::stringify() const
{
    std::ostringstream out;
    out << this->view;
    return out.str() + " " + stringify_flags();
}

wf::keyboard_interaction_t& wf::layer_shell_node_t::keyboard_interaction()
{
    return *kb_interaction;
}

wf::keyboard_focus_node_t wf::layer_shell_node_t::keyboard_refocus(wf::output_t *output)
{
    if (!view || !view->get_keyboard_focus_surface())
    {
        return wf::keyboard_focus_node_t{};
    }

    // Layer-shell views are treated differently.
    // Usually, they should not be focused at all. The only case we want to
    // focus them is when they were already focused, and should continue to
    // have focus, or when they have an active grab.
    if (auto surf = view->get_wlr_surface())
    {
        if (wlr_surface_is_layer_surface(surf))
        {
            auto lsurf = wlr_layer_surface_v1_from_wlr_surface(surf);
            if (lsurf->current.keyboard_interactive ==
                ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
            {
                // Active grab
                return wf::keyboard_focus_node_t{
                    .node = this,
                    .importance = focus_importance::HIGH,
                    .allow_focus_below = false
                };
            }
        }
    }

    if (output != view->get_output())
    {
        return wf::keyboard_focus_node_t{};
    }

    const uint64_t output_last_ts = view->get_output()->get_last_focus_timestamp();
    const uint64_t our_ts = keyboard_interaction().last_focus_timestamp;

    auto cur_focus = wf::get_core_impl().seat->priv->keyboard_focus.get();
    bool has_focus = (cur_focus == this) || (our_ts == output_last_ts);
    if (has_focus)
    {
        return wf::keyboard_focus_node_t{this, focus_importance::REGULAR};
    }

    return wf::keyboard_focus_node_t{};
}

wf::region_t wf::layer_shell_node_t::get_opaque_region() const
{
    if (view && view->is_mapped() && view->get_wlr_surface())
    {
        auto surf = view->get_wlr_surface();

        wf::region_t region{&surf->opaque_region};
        region += this->get_offset();
        return region;
    }

    return {};
}

std::optional<wf::texture_t> wf::layer_shell_node_t::to_texture() const
{
    if (!view || !view->is_mapped() || (get_children().size() != 1))
    {
        return {};
    }

    if (auto texturable = dynamic_cast<zero_copy_texturable_node_t*>(get_children().front().get()))
    {
        return texturable->to_texture();
    }

    return {};
}

class layer_shell_render_instance_t : public wf::scene::translation_node_instance_t
{
    wf::layer_shell_node_t *sself;

  public:
    layer_shell_render_instance_t(wf::layer_shell_node_t *self,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) :
        translation_node_instance_t(self, push_damage_on_all_workspaces(push_damage), shown_on)
    {
        sself = self;
    }

    wf::scene::damage_callback push_damage_on_all_workspaces(wf::scene::damage_callback push_damage)
    {
        return [=] (const wf::region_t& region)
        {
            if (!sself->get_view())
            {
                return;
            }

            auto view   = sself->get_view();
            auto output = view->get_output();
            if (!output)
            {
                push_damage(region);
                return;
            }

            auto wsize = output->wset()->get_workspace_grid_size();
            auto cws   = output->wset()->get_current_workspace();

            /* Damage only the visible region of the shell view.
             * This prevents hidden panels from spilling damage onto other workspaces */
            wlr_box ws_box = output->get_relative_geometry();
            wf::region_t full_damage;

            for (int i = 0; i < wsize.width; i++)
            {
                for (int j = 0; j < wsize.height; j++)
                {
                    const int dx = (i - cws.x) * ws_box.width;
                    const int dy = (j - cws.y) * ws_box.height;
                    full_damage |= region + wf::point_t{dx, dy};
                }
            }

            push_damage(full_damage);
        };
    }

    void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (!sself->get_view())
        {
            return;
        }

        wf::render_target_t our_target = target;
        wf::point_t offset = {0, 0};

        if (sself->get_view()->get_output())
        {
            // Adjust geometry of damage/target so that it is visible on all workspaces
            auto output_size = sself->get_view()->get_output()->get_screen_size();
            offset = {
                (target.geometry.x % output_size.width) - target.geometry.x,
                (target.geometry.y % output_size.height) - target.geometry.y,
            };

            our_target = target.translated(offset);
        }

        damage += offset;
        translation_node_instance_t::schedule_instructions(instructions, our_target, damage);
        damage += -offset;
    }
};

void wf::layer_shell_node_t::gen_render_instances(std::vector<scene::render_instance_uptr> & instances,
    scene::damage_callback push_damage, wf::output_t *shown_on)
{
    if (!view)
    {
        return;
    }

    // Special case: layer-shell views live only inside their outputs and should not be shown on other outputs
    if (shown_on && (this->view->get_output() != shown_on))
    {
        return;
    }

    instances.push_back(std::make_unique<layer_shell_render_instance_t>(this, push_damage, shown_on));
}

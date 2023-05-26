#include "view-keyboard-interaction.hpp"
#include <memory>
#include <wayfire/debug.hpp>
#include <wayfire/output.hpp>
#include "../core/core-impl.hpp"
#include "../core/seat/input-manager.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/view.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "view-impl.hpp"

wf::scene::view_node_t::view_node_t(wayfire_view _view) :
    floating_inner_node_t(false), view(_view)
{
    this->kb_interaction = std::make_unique<view_keyboard_interaction_t>(view);
    on_view_destroy = [=] (view_destruct_signal *ev)
    {
        this->view = nullptr;
        this->kb_interaction = std::make_unique<keyboard_interaction_t>();
    };

    view->connect(&on_view_destroy);
}

wf::scene::view_node_t::view_node_t() : floating_inner_node_t(false)
{}

std::string wf::scene::view_node_t::stringify() const
{
    std::ostringstream out;
    out << this->view;
    return out.str() + " " + stringify_flags();
}

wf::keyboard_interaction_t& wf::scene::view_node_t::keyboard_interaction()
{
    return *kb_interaction;
}

wf::pointf_t wf::scene::view_node_t::to_local(const wf::pointf_t& point)
{
    if (!view)
    {
        return point;
    }

    return point - wf::pointf_t(wf::origin(view->get_output_geometry()));
}

wf::pointf_t wf::scene::view_node_t::to_global(const wf::pointf_t& point)
{
    if (!view)
    {
        return point;
    }

    return point + wf::pointf_t(wf::origin(view->get_output_geometry()));
}

std::optional<wf::scene::input_node_t> wf::scene::view_node_t::find_node_at(
    const wf::pointf_t& at)
{
    if (!view)
    {
        return {};
    }

    auto& input_manager = wf::get_core_impl().input;
    if (input_manager->exclusive_client && (view->get_client() != input_manager->exclusive_client))
    {
        /* We have exclusive focus surface, for ex. a lockscreen.
         * The only kind of things we can focus are OSKs and similar */
        if (view)
        {
            auto layer = get_view_layer(view);
            if (layer.has_value() && (layer.value() == wf::scene::layer::DWIDGET))
            {
                return floating_inner_node_t::find_node_at(at);
            }
        }

        return {};
    }

    if (view->minimized)
    {
        return {};
    }

    return floating_inner_node_t::find_node_at(at);
}

/**
 * Minimal percentage of the view which needs to be visible on a workspace
 * for it to count to be on that workspace.
 */
static constexpr double MIN_VISIBILITY_PC = 0.1;

wf::keyboard_focus_node_t wf::scene::view_node_t::keyboard_refocus(
    wf::output_t *output)
{
    if (!view)
    {
        return wf::keyboard_focus_node_t{};
    }

    if (!this->view->is_mapped() ||
        !this->view->get_keyboard_focus_surface() ||
        this->view->minimized ||
        !this->view->get_output())
    {
        return wf::keyboard_focus_node_t{};
    }

    static wf::option_wrapper_t<bool> remove_output_limits{
        "workarounds/remove_output_limits"};
    bool foreign_output = !remove_output_limits && (output != view->get_output());

    const uint64_t output_last_ts = view->get_output()->get_last_focus_timestamp();
    const uint64_t our_ts = keyboard_interaction().last_focus_timestamp;

    auto cur_focus = wf::get_core_impl().seat->priv->keyboard_focus.get();
    bool has_focus = (cur_focus == this) || (our_ts == output_last_ts);

    const auto cur_layer = get_view_layer(view).value_or(wf::scene::layer::UNMANAGED);
    if (cur_layer != wf::scene::layer::WORKSPACE)
    {
        // Non-workspace views are treated differently.
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

        if (has_focus && !foreign_output)
        {
            return wf::keyboard_focus_node_t{this, focus_importance::REGULAR};
        }

        return wf::keyboard_focus_node_t{};
    }

    if (foreign_output)
    {
        return wf::keyboard_focus_node_t{};
    }

    // When refocusing, we consider each view visible on the output.
    // However, we want to filter out views which are 'barely visible', that is,
    // views where only a small area is visible, because the user typically does
    // not want to focus these views (they might be visible by mistake, or have
    // just a single pixel visible, etc).
    //
    // These views request a LOW focus_importance.
    auto output_box = output->get_layout_geometry();
    auto view_box   = view->get_wm_geometry() +
        wf::origin(view->get_output()->get_layout_geometry());

    auto intersection = wf::geometry_intersection(output_box, view_box);
    double area = 1.0 * intersection.width * intersection.height;
    area /= 1.0 * view_box.width * view_box.height;

    if (area >= MIN_VISIBILITY_PC)
    {
        return wf::keyboard_focus_node_t{this, focus_importance::REGULAR};
    } else if (area > 0)
    {
        return wf::keyboard_focus_node_t{this, focus_importance::LOW};
    } else
    {
        return wf::keyboard_focus_node_t{};
    }
}

namespace wf
{
namespace scene
{
class view_render_instance_t : public render_instance_t
{
    std::vector<render_instance_uptr> children;
    wayfire_view view;
    damage_callback push_damage;
    wf::output_t *visible_on;

  public:
    view_render_instance_t(wayfire_view view, damage_callback push_damage, wf::output_t *visible_on)
    {
        this->view = view;
        this->push_damage = push_damage;
        this->visible_on  = visible_on;
        view->get_surface_root_node()->connect(&on_view_damage);

        auto push_damage_child = [=] (wf::region_t child_damage)
        {
            if (!view)
            {
                return;
            }

            child_damage += wf::origin(view->get_output_geometry());
            push_damage(child_damage);
        };

        for (auto& ch : view->get_surface_root_node()->get_children())
        {
            if (ch->is_enabled())
            {
                ch->gen_render_instances(children, push_damage_child, visible_on);
            }
        }
    }

    // FIXME: once transformers are proper nodes, this should be
    // done in the surfaces and then bubbled up.
    wf::signal::connection_t<node_damage_signal> on_view_damage =
        [=] (node_damage_signal *data)
    {
        push_damage(data->region);
    };

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (!view)
        {
            return;
        }

        wf::render_target_t our_target = target;
        wf::point_t offset = {0, 0};

        if (view->sticky && view->get_output())
        {
            // Adjust geometry of damage/target so that view is always visible if
            // sticky
            auto output_size = view->get_output()->get_screen_size();
            offset = {
                (target.geometry.x % output_size.width) - target.geometry.x,
                (target.geometry.y % output_size.height) - target.geometry.y,
            };

            our_target = target.translated(offset);
        }

        damage += offset;

        auto bbox = view->get_surface_root_node()->get_bounding_box();
        wf::region_t our_damage = damage & bbox;
        if (!our_damage.empty())
        {
            if (!view->is_mapped())
            {
                instructions.push_back(render_instruction_t{
                            .instance = this,
                            .target   = our_target,
                            .damage   = std::move(our_damage),
                        });
            } else
            {
                auto surface_offset = wf::origin(view->get_output_geometry());
                damage += -surface_offset;

                our_target = our_target.translated(-surface_offset);
                for (auto& ch : this->children)
                {
                    ch->schedule_instructions(instructions, our_target, damage);
                }

                damage += surface_offset;
            }
        }

        damage += -offset;
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(
                view->priv->offscreen_buffer.tex,
                view->priv->offscreen_buffer.geometry,
                target.get_orthographic_projection());
        }

        OpenGL::render_end();
    }

    void presentation_feedback(wf::output_t *output) override
    {
        for (auto& ch : this->children)
        {
            ch->presentation_feedback(output);
        }
    }

    direct_scanout try_scanout(wf::output_t *output) override
    {
        if (!view)
        {
            return direct_scanout::SKIP;
        }

        auto og = output->get_relative_geometry();
        if (!(this->view->get_bounding_box() & og))
        {
            return direct_scanout::SKIP;
        }

        auto result = try_scanout_from_list(children, output);
        if (result == direct_scanout::SUCCESS)
        {
            LOGC(SCANOUT, "Scanned out ", view, " on output ", output->to_string());
            return direct_scanout::SUCCESS;
        } else
        {
            LOGC(SCANOUT, "Failed to scan out ", view, " on output ", output->to_string());
            return direct_scanout::OCCLUSION;
        }

        return result;
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        if (!view || !view->is_mapped())
        {
            return;
        }

        compute_visibility_from_list(children, output, visible, wf::origin(view->get_output_geometry()));
    }
};

void view_node_t::gen_render_instances(std::vector<render_instance_uptr> & instances,
    damage_callback push_damage, wf::output_t *shown_on)
{
    if (!view)
    {
        return;
    }

    if ((this->view->role == VIEW_ROLE_DESKTOP_ENVIRONMENT) &&
        this->view->sticky)
    {
        // FIXME: this code should be layer-shell-node-specific
        // Special case: layer-shell views live only inside their outputs and
        // have no benefit from being rendered on other outputs.
        if (shown_on && (this->view->get_output() != shown_on))
        {
            return;
        }
    }

    instances.push_back(std::make_unique<wf::scene::view_render_instance_t>(
        this->view, push_damage, shown_on));
}

std::optional<wf::texture_t> view_node_t::to_texture() const
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
}
}

wf::geometry_t wf::scene::view_node_t::get_bounding_box()
{
    if (!view)
    {
        return {0, 0, 0, 0};
    }

    if (!view->is_mapped())
    {
        return view->priv->offscreen_buffer.geometry;
    }

    return get_children_bounding_box() + wf::origin(view->get_output_geometry());
}

wf::region_t wf::scene::view_node_t::get_opaque_region() const
{
    if (view && view->is_mapped() && view->get_wlr_surface())
    {
        auto surf = view->get_wlr_surface();

        wf::region_t region{&surf->opaque_region};
        region += wf::origin(view->get_output_geometry());
        return region;
    }

    return {};
}

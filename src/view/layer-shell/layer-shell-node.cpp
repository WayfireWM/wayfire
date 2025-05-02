#include "layer-shell-node.hpp"
#include "wayfire/unstable/wlr-view-keyboard-interaction.hpp"
#include "wayfire/scene-input.hpp"
#include "../core/core-impl.hpp"
#include "../core/seat/seat-impl.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/unstable/translation-node.hpp"
#include <wayfire/workspace-set.hpp>

wf::layer_shell_node_t::layer_shell_node_t(wayfire_view view) : view_node_tag_t(view)
{
    this->kb_interaction = std::make_unique<wlr_view_keyboard_interaction_t>(view);
    this->_view = view->weak_from_this();
}

std::string wf::layer_shell_node_t::stringify() const
{
    auto view = _view.lock();
    if (view)
    {
        std::ostringstream out;
        out << view->self();
        return out.str() + " " + stringify_flags();
    } else
    {
        return "inert layer-shell";
    }
}

wf::keyboard_interaction_t& wf::layer_shell_node_t::keyboard_interaction()
{
    return *kb_interaction;
}

wf::keyboard_focus_node_t wf::layer_shell_node_t::keyboard_refocus(wf::output_t *output)
{
    auto view = _view.lock();
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
        if (wlr_layer_surface_v1 *layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(surf))
        {
            if (layer_surface->current.keyboard_interactive ==
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

    const uint64_t last_ts = wf::get_core().seat->get_last_focus_timestamp();
    const uint64_t our_ts  = keyboard_interaction().last_focus_timestamp;

    auto cur_focus = wf::get_core_impl().seat->priv->keyboard_focus.get();
    bool has_focus = (cur_focus == this) || (our_ts == last_ts);
    if (has_focus)
    {
        return wf::keyboard_focus_node_t{this, focus_importance::REGULAR};
    }

    return wf::keyboard_focus_node_t{};
}

wf::region_t wf::layer_shell_node_t::get_opaque_region() const
{
    auto view = _view.lock();
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
    auto view = _view.lock();
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

void wf::layer_shell_node_t::gen_render_instances(std::vector<scene::render_instance_uptr> & instances,
    scene::damage_callback push_damage, wf::output_t *shown_on)
{
    auto view = _view.lock();
    if (!view)
    {
        return;
    }

    // Special case: layer-shell views live only inside their outputs and should not be shown on other outputs
    if (shown_on && (view->get_output() != shown_on))
    {
        return;
    }

    instances.push_back(std::make_unique<scene::translation_node_instance_t>(this, push_damage, shown_on));
}

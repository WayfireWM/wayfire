#include "toplevel-node.hpp"
#include "wayfire/view.hpp"
#include <wayfire/output.hpp>
#include <wayfire/unstable/wlr-view-keyboard-interaction.hpp>

wf::toplevel_view_node_t::toplevel_view_node_t(wayfire_toplevel_view view) : view_node_tag_t(view)
{
    this->kb_interaction = std::make_unique<wlr_view_keyboard_interaction_t>(view);
    this->_view = view->weak_from_this();
}

/**
 * Minimal percentage of the view which needs to be visible on a workspace
 * for it to count to be on that workspace.
 */
static constexpr double MIN_VISIBILITY_PC = 0.1;

wf::keyboard_focus_node_t wf::toplevel_view_node_t::keyboard_refocus(wf::output_t *output)
{
    auto view = _view.lock();
    if (!view)
    {
        return wf::keyboard_focus_node_t{};
    }

    if (!view->is_mapped() || !view->get_keyboard_focus_surface() || view->minimized || !view->get_output())
    {
        return wf::keyboard_focus_node_t{};
    }

    static wf::option_wrapper_t<bool> remove_output_limits{"workarounds/remove_output_limits"};
    bool foreign_output = !remove_output_limits && (output != view->get_output());
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
    //
    // NB: we refocus based on the pending geometry, because the new geometry might not have been applied
    // immediately after switching workspaces.
    auto output_box = output->get_layout_geometry();
    auto view_box   = view->get_pending_geometry() + wf::origin(view->get_output()->get_layout_geometry());

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

wf::keyboard_interaction_t& wf::toplevel_view_node_t::keyboard_interaction()
{
    return *kb_interaction;
}

std::string wf::toplevel_view_node_t::stringify() const
{
    if (auto view = _view.lock())
    {
        std::ostringstream out;
        out << view->self();
        return out.str() + " " + stringify_flags();
    } else
    {
        return "inert toplevel " + stringify_flags();
    }
}

class toplevel_view_render_instance_t : public wf::scene::translation_node_instance_t
{
  public:
    using translation_node_instance_t::translation_node_instance_t;

    wf::scene::direct_scanout try_scanout(wf::output_t *output) override
    {
        wf::toplevel_view_node_t *tnode = dynamic_cast<wf::toplevel_view_node_t*>(self.get());
        auto view = tnode->get_view();

        if (!view)
        {
            return wf::scene::direct_scanout::SKIP;
        }

        auto og = output->get_relative_geometry();
        if (!(view->get_bounding_box() & og))
        {
            return wf::scene::direct_scanout::SKIP;
        }

        auto result = try_scanout_from_list(children, output);
        if (result == wf::scene::direct_scanout::SUCCESS)
        {
            LOGC(SCANOUT, "Scanned out ", view, " on output ", output->to_string());
            return wf::scene::direct_scanout::SUCCESS;
        } else
        {
            LOGC(SCANOUT, "Failed to scan out ", view, " on output ", output->to_string());
            return wf::scene::direct_scanout::OCCLUSION;
        }

        return result;
    }
};

void wf::toplevel_view_node_t::gen_render_instances(
    std::vector<scene::render_instance_uptr>& instances,
    scene::damage_callback push_damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<toplevel_view_render_instance_t>(this, push_damage, output));
}

std::optional<wf::texture_t> wf::toplevel_view_node_t::to_texture() const
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

wf::region_t wf::toplevel_view_node_t::get_opaque_region() const
{
    auto view = _view.lock();
    if (view && view->is_mapped() && view->get_wlr_surface())
    {
        auto surf = view->get_wlr_surface();

        wf::region_t region{&surf->opaque_region};
        region += get_offset();
        return region;
    }

    return {};
}

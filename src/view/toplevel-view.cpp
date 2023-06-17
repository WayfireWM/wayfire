#include <wayfire/toplevel-view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include "view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/view.hpp"

static void reposition_relative_to_parent(wayfire_toplevel_view view)
{
    if (!view->parent)
    {
        return;
    }

    auto parent_geometry = view->parent->get_wm_geometry();
    auto wm_geometry     = view->get_wm_geometry();
    auto scr_size = view->get_output()->get_screen_size();
    // Guess which workspace the parent is on
    wf::point_t center = {
        parent_geometry.x + parent_geometry.width / 2,
        parent_geometry.y + parent_geometry.height / 2,
    };
    wf::point_t parent_ws = {
        (int)std::floor(1.0 * center.x / scr_size.width),
        (int)std::floor(1.0 * center.y / scr_size.height),
    };

    auto workarea = view->get_output()->render->get_ws_box(
        view->get_output()->wset()->get_current_workspace() + parent_ws);
    if (view->parent->is_mapped())
    {
        auto parent_g = view->parent->get_wm_geometry();
        wm_geometry.x = parent_g.x + (parent_g.width - wm_geometry.width) / 2;
        wm_geometry.y = parent_g.y + (parent_g.height - wm_geometry.height) / 2;
    } else
    {
        /* if we have a parent which still isn't mapped, we cannot determine
         * the view's position, so we center it on the screen */
        wm_geometry.x = workarea.width / 2 - wm_geometry.width / 2;
        wm_geometry.y = workarea.height / 2 - wm_geometry.height / 2;
    }

    /* make sure view is visible afterwards */
    wm_geometry = wf::clamp(wm_geometry, workarea);
    view->move(wm_geometry.x, wm_geometry.y);
    if ((wm_geometry.width != view->get_wm_geometry().width) ||
        (wm_geometry.height != view->get_wm_geometry().height))
    {
        view->resize(wm_geometry.width, wm_geometry.height);
    }
}

static void unset_toplevel_parent(wayfire_toplevel_view view)
{
    if (view->parent)
    {
        auto& container = view->parent->children;
        auto it = std::remove(container.begin(), container.end(), view);
        container.erase(it, container.end());
        wf::scene::remove_child(view->get_root_node());
    }
}

static wayfire_toplevel_view find_toplevel_parent(wayfire_toplevel_view view)
{
    while (view->parent)
    {
        view = view->parent;
    }

    return view;
}

/**
 * Check whether the toplevel parent needs refocus.
 * This may be needed because when focusing a view, its topmost child is given
 * keyboard focus. When the parent-child relations change, it may happen that
 * the parent needs to be focused again, this time with a different keyboard
 * focus surface.
 */
static void check_refocus_parent(wayfire_toplevel_view view)
{
    view = find_toplevel_parent(view);
    if (view->get_output() && (view->get_output()->get_active_view() == view))
    {
        view->get_output()->focus_view(view, false);
    }
}

void wf::toplevel_view_interface_t::set_toplevel_parent(wayfire_toplevel_view new_parent)
{
    auto old_parent = parent;
    if (parent != new_parent)
    {
        /* Erase from the old parent */
        unset_toplevel_parent({this});

        /* Add in the list of the new parent */
        if (new_parent)
        {
            new_parent->children.insert(new_parent->children.begin(), {this});
        }

        parent = new_parent;
        view_parent_changed_signal ev;
        this->emit(&ev);
    }

    if (parent)
    {
        /* Make sure the view is available only as a child */
        if (this->get_output())
        {
            this->get_output()->wset()->remove_view({this});
        }

        this->set_output(parent->get_output());
        /* if the view isn't mapped, then it will be positioned properly in map() */
        if (is_mapped())
        {
            reposition_relative_to_parent({this});
        }

        wf::scene::readd_front(parent->get_root_node(), this->get_root_node());
        check_refocus_parent(parent);
    } else if (old_parent)
    {
        /* At this point, we are a regular view. */
        if (this->get_output())
        {
            wf::scene::readd_front(get_output()->wset()->get_node(), get_root_node());
            get_output()->wset()->add_view({this});
            check_refocus_parent(old_parent);
        }
    }
}

std::vector<wayfire_toplevel_view> wf::toplevel_view_interface_t::enumerate_views(bool mapped_only)
{
    if (!this->is_mapped() && mapped_only)
    {
        return {};
    }

    std::vector<wayfire_toplevel_view> result;
    result.reserve(priv->last_view_cnt);
    for (auto& v : this->children)
    {
        auto cdr = v->enumerate_views(mapped_only);
        result.insert(result.end(), cdr.begin(), cdr.end());
    }

    result.push_back({this});
    priv->last_view_cnt = result.size();
    return result;
}

void wf::toplevel_view_interface_t::set_output(wf::output_t *new_output)
{
    wf::view_interface_t::set_output(new_output);
    for (auto& view : this->children)
    {
        view->set_output(new_output);
    }
}

void wf::toplevel_view_interface_t::move(int x, int y)
{
    toplevel()->pending().geometry.x = x;
    toplevel()->pending().geometry.y = y;
    wf::get_core().tx_manager->schedule_object(toplevel());
}

void wf::toplevel_view_interface_t::resize(int w, int h)
{
    toplevel()->pending().geometry.width  = w;
    toplevel()->pending().geometry.height = h;
    wf::get_core().tx_manager->schedule_object(toplevel());
}

void wf::toplevel_view_interface_t::set_geometry(wf::geometry_t geometry)
{
    toplevel()->pending().geometry = geometry;
    wf::get_core().tx_manager->schedule_object(toplevel());
}

void wf::toplevel_view_interface_t::request_native_size()
{
    /* no-op */
}

wf::geometry_t wf::toplevel_view_interface_t::get_wm_geometry()
{
    return toplevel()->current().geometry;
}

void wf::toplevel_view_interface_t::set_minimized(bool minim)
{
    this->minimized = minim;
    view_minimized_signal data;
    data.view = {this};
    this->emit(&data);
    get_output()->emit(&data);

    if (pending_minimized == minim)
    {
        return;
    }

    this->pending_minimized = minim;
    minimized = minim;
    if (minimized)
    {
        view_disappeared_signal data;
        data.view = self();
        get_output()->emit(&data);
        wf::scene::set_node_enabled(get_root_node(), false);
    } else
    {
        wf::scene::set_node_enabled(get_root_node(), true);
        get_output()->focus_view(self(), true);
    }
}

void wf::toplevel_view_interface_t::set_sticky(bool sticky)
{
    if (this->sticky == sticky)
    {
        return;
    }

    damage();
    this->sticky = sticky;
    damage();

    wf::view_set_sticky_signal data;
    data.view = {this};

    this->emit(&data);
    if (this->get_output())
    {
        this->get_output()->emit(&data);
    }
}

void wf::toplevel_view_interface_t::set_activated(bool active)
{
    activated = active;
    view_activated_state_signal ev;
    this->emit(&ev);
}

wlr_box wf::toplevel_view_interface_t::get_minimize_hint()
{
    return this->priv->minimize_hint;
}

void wf::toplevel_view_interface_t::set_minimize_hint(wlr_box hint)
{
    this->priv->minimize_hint = hint;
}

bool wf::toplevel_view_interface_t::should_be_decorated()
{
    return false;
}

void wf::toplevel_view_interface_t::set_decoration(
    std::unique_ptr<wf::decorator_frame_t_t> frame)
{
    if (!frame)
    {
        damage();

        // Take wm geometry as it was with the decoration.
        const auto wm = get_wm_geometry();

        // Drop the owned frame.
        priv->frame = nullptr;

        // Grow the tiled view to fill its old expanded geometry that included
        // the decoration.
        if (!pending_fullscreen() && this->pending_tiled_edges() && (wm != get_wm_geometry()))
        {
            set_geometry(wm);
        }

        view_decoration_changed_signal data;
        data.view = self();
        emit(&data);
        return;
    }

    // Take wm geometry as it was before adding the frame */
    auto wm = get_wm_geometry();

    damage();
    // Drop the old frame if any and assign the new one.
    priv->frame = std::move(frame);

    /* Calculate the wm geometry of the view after adding the decoration.
     *
     * If the view is neither maximized nor fullscreen, then we want to expand
     * the view geometry so that the actual view contents retain their size.
     *
     * For fullscreen and maximized views we want to "shrink" the view contents
     * so that the total wm geometry remains the same as before. */
    wf::geometry_t target_wm_geometry;
    if (!pending_fullscreen() && !this->pending_tiled_edges())
    {
        target_wm_geometry = priv->frame->expand_wm_geometry(wm);
        // make sure that the view doesn't go outside of the screen or such
        auto wa = get_output()->workarea->get_workarea();
        auto visible = wf::geometry_intersection(target_wm_geometry, wa);
        if (visible != target_wm_geometry)
        {
            target_wm_geometry.x = wm.x;
            target_wm_geometry.y = wm.y;
        }
    } else if (pending_fullscreen())
    {
        target_wm_geometry = get_output()->get_relative_geometry();
    } else if (this->pending_tiled_edges())
    {
        target_wm_geometry = get_output()->workarea->get_workarea();
    }

    set_geometry(target_wm_geometry);
    damage();

    view_decoration_changed_signal ev;
    ev.view = self();
    emit(&ev);
}

void wf::toplevel_view_interface_t::deinitialize()
{
    auto children = this->children;
    for (auto ch : children)
    {
        ch->set_toplevel_parent(nullptr);
    }

    set_decoration(nullptr);
    view_interface_t::deinitialize();
}

wf::toplevel_view_interface_t::~toplevel_view_interface_t()
{
    /* Note: at this point, it is invalid to call most functions */
    unset_toplevel_parent({this});
}

void wf::toplevel_view_interface_t::set_allowed_actions(uint32_t actions) const
{
    priv->allowed_actions = actions;
}

uint32_t wf::toplevel_view_interface_t::get_allowed_actions() const
{
    return priv->allowed_actions;
}

std::shared_ptr<wf::workspace_set_t> wf::toplevel_view_interface_t::get_wset()
{
    return priv->current_wset.lock();
}

const std::shared_ptr<wf::toplevel_t>& wf::toplevel_view_interface_t::toplevel() const
{
    return priv->toplevel;
}

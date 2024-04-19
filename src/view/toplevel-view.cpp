#include <wayfire/toplevel-view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/seat.hpp>
#include "view-impl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"

static void reposition_relative_to_parent(wayfire_toplevel_view view)
{
    if (!view->parent)
    {
        return;
    }

    auto parent_geometry = view->parent->get_pending_geometry();
    auto wm_geometry     = view->get_pending_geometry();
    // Guess which workspace the parent is on
    wf::point_t center = {
        parent_geometry.x + parent_geometry.width / 2,
        parent_geometry.y + parent_geometry.height / 2,
    };

    if (view->parent->is_mapped())
    {
        auto parent_g = view->parent->get_pending_geometry();
        wm_geometry.x = parent_g.x + (parent_g.width - wm_geometry.width) / 2;
        wm_geometry.y = parent_g.y + (parent_g.height - wm_geometry.height) / 2;
    }

    if (view->get_output())
    {
        auto scr_size = view->get_output()->get_screen_size();
        wf::point_t parent_ws = {
            (int)std::floor(1.0 * center.x / scr_size.width),
            (int)std::floor(1.0 * center.y / scr_size.height),
        };

        auto workarea = view->get_output()->render->get_ws_box(
            view->get_output()->wset()->get_current_workspace() + parent_ws);
        if (!view->parent->is_mapped())
        {
            /* if we have a parent which still isn't mapped, we cannot determine
             * the view's position, so we center it on the screen */
            wm_geometry.x = workarea.width / 2 - wm_geometry.width / 2;
            wm_geometry.y = workarea.height / 2 - wm_geometry.height / 2;
        }

        wm_geometry = wf::clamp(wm_geometry, workarea);
    }

    /* make sure view is visible afterwards */
    view->move(wm_geometry.x, wm_geometry.y);
    if ((wm_geometry.width != view->get_pending_geometry().width) ||
        (wm_geometry.height != view->get_pending_geometry().height))
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
    if (wf::get_core().seat->get_active_view() == view)
    {
        wf::get_core().seat->focus_view(view);
    }
}

void wf::toplevel_view_interface_t::set_toplevel_parent(wayfire_toplevel_view new_parent)
{
    auto old_parent = parent;
    if (parent != new_parent)
    {
        LOGC(VIEWS, "Setting toplevel parent of ", self(), " to ", new_parent);

        /* Erase from the old parent */
        unset_toplevel_parent({this});

        /* Add in the list of the new parent */
        if (new_parent)
        {
            new_parent->children.insert(new_parent->children.begin(), wayfire_toplevel_view{this});
        }

        parent = new_parent;
        view_parent_changed_signal ev;
        ev.view = {this};
        this->emit(&ev);
    }

    if (parent)
    {
        /* Make sure the view is available only as a child */
        if (this->get_wset())
        {
            wf::view_pre_moved_to_wset_signal pre_move_to_wset;
            pre_move_to_wset.view     = {this};
            pre_move_to_wset.old_wset = get_wset();
            pre_move_to_wset.new_wset = nullptr;
            wf::get_core().emit(&pre_move_to_wset);

            get_wset()->remove_view({this});

            wf::view_moved_to_wset_signal move_to_wset;
            move_to_wset.view     = {this};
            move_to_wset.old_wset = pre_move_to_wset.old_wset;
            move_to_wset.new_wset = nullptr;
            wf::get_core().emit(&move_to_wset);
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
    wf::dassert(!this->parent || this->parent->get_output() == new_output,
        "Cannot set different output for a view with a parent!");
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

void wf::toplevel_view_interface_t::set_minimized(bool minim)
{
    if (minim == minimized)
    {
        return;
    }

    if (this->parent && minim)
    {
        LOGE("Ignoring a request to minimize a view with a parent, minimize the parent instead!");
        return;
    }

    this->minimized = minim;
    wf::scene::set_node_enabled(get_root_node(), !minimized);

    view_minimized_signal data;
    data.view = {this};
    this->emit(&data);
    if (get_output())
    {
        get_output()->emit(&data);
        if (minim)
        {
            view_disappeared_signal data;
            data.view = self();
            get_output()->emit(&data);
            wf::scene::update(get_root_node(), scene::update_flag::REFOCUS);
        }
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
    ev.view = {this};
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

void wf::toplevel_view_interface_t::set_toplevel(
    std::shared_ptr<wf::toplevel_t> toplevel)
{
    priv->toplevel = toplevel;
}

wayfire_toplevel_view wf::find_view_for_toplevel(
    std::shared_ptr<wf::toplevel_t> toplevel)
{
    // FIXME: this could be a lot more efficient if we simply store a custom data on the toplevel.
    for (auto& view : wf::get_core().get_all_views())
    {
        if (auto tview = toplevel_cast(view))
        {
            if (tview->toplevel() == toplevel)
            {
                return tview;
            }
        }
    }

    return nullptr;
}

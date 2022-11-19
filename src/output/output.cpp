#include "output-impl.hpp"
#include "wayfire/view.hpp"
#include "../core/core-impl.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/compositor-view.hpp"
#include "wayfire-shell.hpp"
#include "../core/seat/input-manager.hpp"
#include "../view/xdg-shell.hpp"
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include <algorithm>
#include <assert.h>

wf::output_t::output_t() = default;

wf::output_impl_t::output_impl_t(wlr_output *handle,
    const wf::dimensions_t& effective_size)
{
    this->bindings = std::make_unique<bindings_repository_t>(this);
    this->set_effective_size(effective_size);
    this->handle = handle;
    workspace    = std::make_unique<workspace_manager>(this);
    render = std::make_unique<render_manager>(this);

    view_disappeared_cb = [=] (wf::signal_data_t *data)
    {
        output_t::refocus(get_signaled_view(data));
    };

    connect_signal("view-disappeared", &view_disappeared_cb);
}

void wf::output_impl_t::start_plugins()
{
    plugin = std::make_unique<plugin_manager>(this);
}

std::string wf::output_t::to_string() const
{
    return handle->name;
}

/**
 * Minimal percentage of the view which needs to be visible on a workspace
 * for it to count to be on that workspace.
 */
static constexpr double MIN_VISIBILITY_PC = 0.1;

void wf::output_impl_t::refocus(wayfire_view skip_view, uint32_t layers)
{
    wf::point_t cur_ws = workspace->get_current_workspace();
    const auto& view_on_current_ws = [&] (wayfire_view view)
    {
        // Make sure the view is at least 10% visible on the
        // current workspace, to focus it
        auto ws_geometry = render->get_ws_box(cur_ws);
        auto bbox = view->transform_region(view->get_wm_geometry());
        auto intersection = wf::geometry_intersection(bbox, ws_geometry);
        double area = 1.0 * intersection.width * intersection.height;
        area /= 1.0 * bbox.width * bbox.height;

        return area >= MIN_VISIBILITY_PC;
    };

    const auto& suitable_for_focus = [&] (wayfire_view view)
    {
        return (view != skip_view) && view->is_mapped() &&
               view->get_keyboard_focus_surface() && !view->minimized;
    };

    auto views = workspace->get_views_on_workspace(cur_ws, layers);

    // All views which might be focused
    std::vector<wayfire_view> candidates;
    for (auto toplevel : views)
    {
        auto vs = toplevel->enumerate_views();
        std::copy_if(vs.begin(), vs.end(), std::back_inserter(candidates),
            suitable_for_focus);
    }

    // Choose the best view.
    // All views which are mostly visible on the current workspace are preferred.
    // In case of ties, views with the latest focus timestamp are preferred.
    auto it = std::max_element(candidates.begin(), candidates.end(),
        [&] (wayfire_view view1, wayfire_view view2)
    {
        bool visible_1 = view_on_current_ws(view1);
        bool visible_2 = view_on_current_ws(view2);

        return std::make_tuple(visible_1, view1->last_focus_timestamp) <
        std::make_tuple(visible_2, view2->last_focus_timestamp);
    });

    if (it == candidates.end())
    {
        focus_view(nullptr, 0u);
    } else
    {
        focus_view(*it, (uint32_t)FOCUS_VIEW_NOBUMP);
    }
}

void wf::output_t::refocus(wayfire_view skip_view)
{
    uint32_t focused_layer = wf::get_core().get_focused_layer();
    uint32_t layers = focused_layer <=
        LAYER_WORKSPACE ? MIDDLE_LAYERS : focused_layer;

    auto views = workspace->get_views_on_workspace(
        workspace->get_current_workspace(), layers);

    if (views.empty())
    {
        if (wf::get_core().get_active_output() == this)
        {
            LOGD("warning: no focused views in the focused layer, probably a bug");
        }

        /* Usually, we focus a layer so that a particular view has focus, i.e
         * we expect that there is a view in the focused layer. However we
         * should try to find reasonable focus in any focuseable layers if
         * that is not the case, for ex. if there is a focused layer by a
         * layer surface on another output */
        layers = all_layers_not_below(focused_layer);
    }

    refocus(skip_view, layers);
}

wf::output_t::~output_t()
{}

wf::output_impl_t::~output_impl_t()
{
    // Release plugins before bindings
    this->plugin.reset();
    this->bindings.reset();
}

void wf::output_impl_t::set_effective_size(const wf::dimensions_t& size)
{
    this->effective_size = size;
}

wf::dimensions_t wf::output_impl_t::get_screen_size() const
{
    return this->effective_size;
}

wf::geometry_t wf::output_t::get_relative_geometry() const
{
    auto size = get_screen_size();

    return {
        0, 0, size.width, size.height
    };
}

wf::geometry_t wf::output_t::get_layout_geometry() const
{
    wlr_box box;
    wlr_output_layout_get_box(
        wf::get_core().output_layout->get_handle(), handle, &box);
    if (wlr_box_empty(&box))
    {
        LOGE("Get layout geometry for an invalid output!");

        return {0, 0, 1, 1};
    } else
    {
        return box;
    }
}

void wf::output_t::ensure_pointer(bool center) const
{
    auto ptr = wf::get_core().get_cursor_position();
    if (!center &&
        (get_layout_geometry() & wf::point_t{(int)ptr.x, (int)ptr.y}))
    {
        return;
    }

    auto lg = get_layout_geometry();
    wf::pointf_t target = {
        lg.x + lg.width / 2.0,
        lg.y + lg.height / 2.0,
    };
    wf::get_core().warp_cursor(target);
    wf::get_core().set_cursor("default");
}

wf::pointf_t wf::output_t::get_cursor_position() const
{
    auto og = get_layout_geometry();
    auto gc = wf::get_core().get_cursor_position();

    return {gc.x - og.x, gc.y - og.y};
}

bool wf::output_t::ensure_visible(wayfire_view v)
{
    auto bbox = v->get_bounding_box();
    auto g    = this->get_relative_geometry();

    /* Compute the percentage of the view which is visible */
    auto intersection = wf::geometry_intersection(bbox, g);
    double area = 1.0 * intersection.width * intersection.height;
    area /= 1.0 * bbox.width * bbox.height;

    if (area >= 0.1) /* View is somewhat visible, no need for anything special */
    {
        return false;
    }

    /* Otherwise, switch the workspace so the view gets maximum exposure */
    int dx = bbox.x + bbox.width / 2;
    int dy = bbox.y + bbox.height / 2;

    int dvx  = std::floor(1.0 * dx / g.width);
    int dvy  = std::floor(1.0 * dy / g.height);
    auto cws = workspace->get_current_workspace();
    workspace->request_workspace(cws + wf::point_t{dvx, dvy});

    return true;
}

void wf::output_impl_t::close_popups()
{
    for (auto& v : workspace->get_views_in_layer(wf::ALL_LAYERS))
    {
        auto popup = dynamic_cast<wayfire_xdg_popup*>(v.get());
        if (!popup || (popup->popup_parent == active_view.get()))
        {
            continue;
        }

        /* Ignore popups which have a popup as their parent. In those cases, we'll
         * close the topmost popup and this will recursively destroy the others.
         *
         * Otherwise we get a race condition with wlroots. */
        if (dynamic_cast<wayfire_xdg_popup*>(popup->popup_parent))
        {
            continue;
        }

        popup->close();
    }
}

void wf::output_impl_t::update_active_view(wayfire_view v, uint32_t flags)
{
    this->active_view = v;
    if (this == wf::get_core().get_active_output())
    {
        wf::get_core().set_active_view(v);
    }

    if (flags & FOCUS_VIEW_CLOSE_POPUPS)
    {
        close_popups();
    }
}

void wf::update_focus_timestamp(wayfire_view view)
{
    if (view)
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        view->last_focus_timestamp = ts.tv_sec * 1'000'000'000ll + ts.tv_nsec;
    }
}

void wf::output_impl_t::focus_view(wayfire_view v, uint32_t flags)
{
    static wf::option_wrapper_t<bool>
    all_dialogs_modal{"workarounds/all_dialogs_modal"};

    const auto& make_view_visible = [this, flags] (wayfire_view view)
    {
        if (view->minimized)
        {
            view->minimize_request(false);
        }

        if (flags & FOCUS_VIEW_RAISE)
        {
            while (view->parent)
            {
                view = view->parent;
            }

            workspace->bring_to_front(view);
        }
    };

    if (v && (workspace->get_view_layer(v) < wf::get_core().get_focused_layer()))
    {
        auto active_view = get_active_view();
        if (active_view && (active_view->get_app_id().find("$unfocus") == 0))
        {
            /* This is the case where for ex. a panel has grabbed input focus,
             * but user has clicked on another view so we want to dismiss the
             * grab. We can't do that straight away because the client still
             * holds the focus layer request.
             *
             * Instead, we want to deactivate the $unfocus view, so that it can
             * release the grab. At the same time, we bring the to-be-focused
             * view on top, so that it gets the focus next. */
            update_active_view(nullptr, flags);
            make_view_visible(v);
            update_focus_timestamp(v);
        } else
        {
            LOGD("Denying focus request for a view from a lower layer than the"
                 " focused layer");
        }

        return;
    }

    focus_view_signal data;

    if (!v || !v->is_mapped())
    {
        update_active_view(nullptr, flags);
        data.view = nullptr;
        emit_signal("focus-view", &data);
        return;
    }

    while (all_dialogs_modal && v->parent && v->parent->is_mapped())
    {
        v = v->parent;
    }

    /* If no keyboard focus surface is set, then we don't want to focus the view */
    if (v->get_keyboard_focus_surface() || interactive_view_from_view(v.get()))
    {
        make_view_visible(v);
        if (!(flags & FOCUS_VIEW_NOBUMP))
        {
            update_focus_timestamp(v);
        }

        update_active_view(v, flags);
        data.view = v;
        emit_signal("view-focused", &data);
    }
}

void wf::output_impl_t::focus_view(wayfire_view v, bool raise)
{
    uint32_t flags = FOCUS_VIEW_CLOSE_POPUPS;
    if (raise)
    {
        flags |= FOCUS_VIEW_RAISE;
    }

    focus_view(v, flags);
}

wayfire_view wf::output_t::get_top_view() const
{
    auto views = workspace->get_views_on_workspace(
        workspace->get_current_workspace(),
        LAYER_WORKSPACE);

    return views.empty() ? nullptr : views[0];
}

wayfire_view wf::output_impl_t::get_active_view() const
{
    return active_view;
}

bool wf::output_impl_t::can_activate_plugin(uint32_t caps,
    uint32_t flags)
{
    if (this->inhibited && !(flags & wf::PLUGIN_ACTIVATION_IGNORE_INHIBIT))
    {
        return false;
    }

    for (auto act_owner : active_plugins)
    {
        bool compatible = ((act_owner->capabilities & caps) == 0);
        if (!compatible)
        {
            return false;
        }
    }

    return true;
}

bool wf::output_impl_t::can_activate_plugin(const plugin_grab_interface_uptr& owner,
    uint32_t flags)
{
    if (!owner)
    {
        return false;
    }

    if (active_plugins.find(owner.get()) != active_plugins.end())
    {
        return flags & wf::PLUGIN_ACTIVATE_ALLOW_MULTIPLE;
    }

    return can_activate_plugin(owner->capabilities, flags);
}

bool wf::output_impl_t::activate_plugin(const plugin_grab_interface_uptr& owner,
    uint32_t flags)
{
    if (!can_activate_plugin(owner, flags))
    {
        return false;
    }

    if (active_plugins.find(owner.get()) != active_plugins.end())
    {
        LOGD("output ", handle->name,
            ": activate plugin ", owner->name, " again");
    } else
    {
        LOGD("output ", handle->name, ": activate plugin ", owner->name);
    }

    active_plugins.insert(owner.get());

    return true;
}

bool wf::output_impl_t::deactivate_plugin(
    const plugin_grab_interface_uptr& owner)
{
    auto it = active_plugins.find(owner.get());
    if (it == active_plugins.end())
    {
        return true;
    }

    active_plugins.erase(it);
    LOGD("output ", handle->name, ": deactivate plugin ", owner->name);

    if (active_plugins.count(owner.get()) == 0)
    {
        owner->ungrab();
        active_plugins.erase(owner.get());

        return true;
    }

    return false;
}

void wf::output_impl_t::cancel_active_plugins()
{
    std::vector<wf::plugin_grab_interface_t*> ifaces;
    for (auto p : active_plugins)
    {
        if (p->callbacks.cancel)
        {
            ifaces.push_back(p);
        }
    }

    for (auto p : ifaces)
    {
        p->callbacks.cancel();
    }
}

bool wf::output_impl_t::is_plugin_active(std::string name) const
{
    for (auto act : active_plugins)
    {
        if (act && (act->name == name))
        {
            return true;
        }
    }

    return false;
}

wf::plugin_grab_interface_t*wf::output_impl_t::get_input_grab_interface()
{
    for (auto p : active_plugins)
    {
        if (p && p->is_grabbed())
        {
            return p;
        }
    }

    return nullptr;
}

void wf::output_impl_t::inhibit_plugins()
{
    this->inhibited = true;
    cancel_active_plugins();
}

void wf::output_impl_t::uninhibit_plugins()
{
    this->inhibited = false;
}

bool wf::output_impl_t::is_inhibited() const
{
    return this->inhibited;
}

namespace wf
{
template<class Option, class Callback>
static wf::binding_t *push_binding(
    binding_container_t<Option, Callback>& bindings,
    option_sptr_t<Option> opt,
    Callback *callback)
{
    auto bnd = std::make_unique<output_binding_t<Option, Callback>>();
    bnd->activated_by = opt;
    bnd->callback     = callback;
    bindings.emplace_back(std::move(bnd));

    return bindings.back().get();
}

binding_t*output_impl_t::add_key(option_sptr_t<keybinding_t> key,
    wf::key_callback *callback)
{
    return push_binding(this->bindings->keys, key, callback);
}

binding_t*output_impl_t::add_axis(option_sptr_t<keybinding_t> axis,
    wf::axis_callback *callback)
{
    return push_binding(this->bindings->axes, axis, callback);
}

binding_t*output_impl_t::add_button(option_sptr_t<buttonbinding_t> button,
    wf::button_callback *callback)
{
    return push_binding(this->bindings->buttons, button, callback);
}

binding_t*output_impl_t::add_activator(
    option_sptr_t<activatorbinding_t> activator, wf::activator_callback *callback)
{
    auto result = push_binding(this->bindings->activators, activator, callback);
    this->bindings->recreate_hotspots();
    return result;
}

void wf::output_impl_t::rem_binding(wf::binding_t *binding)
{
    return this->bindings->rem_binding(binding);
}

void wf::output_impl_t::rem_binding(void *callback)
{
    return this->bindings->rem_binding(callback);
}

bindings_repository_t& output_impl_t::get_bindings()
{
    return *bindings;
}

bool output_impl_t::call_plugin(
    const std::string& activator, const wf::activator_data_t& data) const
{
    return this->bindings->handle_activator(activator, data);
}

uint32_t all_layers_not_below(uint32_t layer)
{
    uint32_t mask = 0;
    for (int i = 0; i < wf::TOTAL_LAYERS; i++)
    {
        if ((1u << i) >= layer)
        {
            mask |= (1 << i);
        }
    }

    return mask;
}
} // namespace wf

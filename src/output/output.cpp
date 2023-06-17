#include "output-impl.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/core.hpp"
#include "wayfire/bindings-repository.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/output.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/view.hpp"
#include "../core/core-impl.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/compositor-view.hpp"
#include "../core/seat/input-manager.hpp"
#include "../view/xdg-shell.hpp"
#include <memory>
#include <wayfire/config/types.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>

#include <algorithm>
#include <assert.h>

wf::output_t::output_t() = default;

void wf::output_impl_t::handle_view_removed(wayfire_view view)
{
    if (this->active_view == view)
    {
        this->active_view = nullptr;
    }

    if (this->last_active_toplevel == view)
    {
        last_active_toplevel->set_activated(false);
        this->last_active_toplevel = nullptr;
    }

    refocus();
}

void wf::output_impl_t::update_node_limits()
{
    static wf::option_wrapper_t<bool> remove_output_limits{"workarounds/remove_output_limits"};
    for (int i = 0; i < (int)wf::scene::layer::ALL_LAYERS; i++)
    {
        if (remove_output_limits)
        {
            node_for_layer((wf::scene::layer)i)->limit_region.reset();
        } else
        {
            node_for_layer((wf::scene::layer)i)->limit_region = get_layout_geometry();
        }
    }

    wf::scene::update(wf::get_core().scene(), scene::update_flag::INPUT_STATE);
}

wf::output_impl_t::output_impl_t(wlr_output *handle,
    const wf::dimensions_t& effective_size)
{
    this->set_effective_size(effective_size);
    this->handle = handle;

    auto& root = wf::get_core().scene();
    for (size_t layer = 0; layer < (size_t)scene::layer::ALL_LAYERS; layer++)
    {
        nodes[layer] = std::make_shared<scene::output_node_t>(this);
        scene::add_back(root->layers[layer], nodes[layer]);
    }

    update_node_limits();

    workarea = std::make_unique<output_workarea_manager_t>(this);
    this->set_workspace_set(std::make_shared<workspace_set_t>());

    render = std::make_unique<render_manager>(this);
    promotion_manager = std::make_unique<promotion_manager_t>(this);

    on_view_disappeared.set_callback([=] (view_disappeared_signal *ev) { handle_view_removed(ev->view); });
    on_configuration_changed = [=] (wf::output_configuration_changed_signal *ev)
    {
        update_node_limits();
    };

    connect(&on_view_disappeared);
    connect(&on_configuration_changed);
}

std::shared_ptr<wf::scene::output_node_t> wf::output_impl_t::node_for_layer(
    wf::scene::layer layer) const
{
    return nodes[(int)layer];
}

std::shared_ptr<wf::workspace_set_t> wf::output_impl_t::wset()
{
    return current_wset;
}

void wf::output_impl_t::set_workspace_set(std::shared_ptr<workspace_set_t> wset)
{
    if (current_wset == wset)
    {
        return;
    }

    wf::dassert(wset != nullptr, "Workspace set should not be null!");

    if (this->current_wset)
    {
        this->current_wset->set_visible(false);
    }

    wset->attach_to_output(this);
    wset->set_visible(true);
    this->current_wset = wset;

    workspace_set_changed_signal data;
    data.new_wset = wset;
    data.output   = this;
    this->emit(&data);

    refocus();
}

std::string wf::output_t::to_string() const
{
    return handle->name;
}

void wf::output_impl_t::do_update_focus(wf::scene::node_t *new_focus)
{
    auto focus =
        new_focus ? new_focus->shared_from_this() : nullptr;
    if (this == wf::get_core().get_active_output())
    {
        wf::get_core().seat->set_active_node(focus);
    }
}

void wf::output_impl_t::refocus()
{
    auto new_focus = wf::get_core().scene()->keyboard_refocus(this);
    LOGC(KBD, "Output ", this->to_string(), " refocusing: choosing node ", new_focus.node);
    if (auto view = node_to_view(new_focus.node))
    {
        update_active_view(view);
    } else if (new_focus.node == nullptr)
    {
        update_active_view(nullptr);
    }

    do_update_focus(new_focus.node);
}

wf::output_t::~output_t()
{}

wf::output_impl_t::~output_impl_t()
{
    auto& bindings = wf::get_core().bindings;
    for (auto& [_, act] : this->key_map)
    {
        bindings->rem_binding(&act);
    }

    for (auto& [_, act] : this->button_map)
    {
        bindings->rem_binding(&act);
    }

    for (auto& [_, act] : this->axis_map)
    {
        bindings->rem_binding(&act);
    }

    for (auto& [_, act] : this->activator_map)
    {
        bindings->rem_binding(&act);
    }

    for (auto& layer_root : nodes)
    {
        layer_root->set_children_list({});
        scene::remove_child(layer_root);
    }
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
        // Can happen when initializing the output
        return {0, 0, handle->width, handle->height};
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
    auto cws = wset()->get_current_workspace();
    wset()->request_workspace(cws + wf::point_t{dvx, dvy});

    return true;
}

static wayfire_view pick_topmost_focusable(wayfire_view view)
{
    if (!wf::toplevel_cast(view))
    {
        if (view->get_keyboard_focus_surface())
        {
            return view;
        }

        return nullptr;
    }

    auto all_views = toplevel_cast(view)->enumerate_views();
    auto it = std::find_if(all_views.begin(), all_views.end(),
        [] (wayfire_view v) { return v->get_keyboard_focus_surface() != NULL; });

    if (it != all_views.end())
    {
        return *it;
    }

    return nullptr;
}

#include <wayfire/debug.hpp>

uint64_t wf::output_impl_t::get_last_focus_timestamp() const
{
    return this->last_timestamp;
}

void wf::output_impl_t::focus_node(wf::scene::node_ptr new_focus)
{
    // When we get a focus request, we have to consider whether there is
    // any node requesting a keyboard grab or something like that.
    if (new_focus)
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        this->last_timestamp = ts.tv_sec * 1'000'000'000ll + ts.tv_nsec;
        new_focus->keyboard_interaction().last_focus_timestamp =
            this->last_timestamp;

        auto focus = wf::get_core().scene()->keyboard_refocus(this);
        do_update_focus(focus.node);
    } else
    {
        do_update_focus(nullptr);
    }
}

void wf::output_impl_t::update_active_view(wayfire_view v)
{
    LOGC(KBD, "Output ", this->to_string(), ": active view becomes ", v);
    if ((v == nullptr) || toplevel_cast(v))
    {
        if (last_active_toplevel != v)
        {
            if (last_active_toplevel)
            {
                last_active_toplevel->set_activated(false);
            }

            last_active_toplevel = nullptr;
            if (auto toplevel = toplevel_cast(v))
            {
                toplevel->set_activated(true);
                last_active_toplevel = toplevel;
            }
        }
    }

    this->active_view = v;
}

wayfire_view find_topmost_parent(wayfire_view v)
{
    if (auto toplevel = wf::toplevel_cast(v))
    {
        while (toplevel->parent)
        {
            toplevel = toplevel->parent;
        }

        return toplevel;
    }

    return v;
}

void wf::output_impl_t::focus_view(wayfire_view v, uint32_t flags)
{
    static wf::option_wrapper_t<bool>
    all_dialogs_modal{"workarounds/all_dialogs_modal"};

    const auto& make_view_visible = [flags] (wayfire_view view)
    {
        view = find_topmost_parent(view);
        if (auto toplevel = toplevel_cast(view))
        {
            if (toplevel->minimized)
            {
                wf::get_core().default_wm->minimize_request(toplevel, false);
            }
        }

        if (flags & FOCUS_VIEW_RAISE)
        {
            view_bring_to_front(view);
        }
    };

    const auto& select_focus_view = [] (wayfire_view v) -> wayfire_view
    {
        if (v && v->is_mapped())
        {
            if (all_dialogs_modal)
            {
                return pick_topmost_focusable(v);
            }

            return v;
        } else
        {
            return nullptr;
        }
    };

    const auto& give_input_focus = [this] (wayfire_view view)
    {
        focus_node(view ? view->get_surface_root_node() : nullptr);
    };

    focus_view_signal data;
    if (!v || !v->is_mapped())
    {
        give_input_focus(nullptr);
        update_active_view(nullptr);

        data.view = nullptr;
        emit(&data);
        return;
    }

    if (all_dialogs_modal)
    {
        v = find_topmost_parent(v);
    }

    pre_focus_view_signal pre_focus;
    pre_focus.view = v;
    emit(&pre_focus);
    if (!pre_focus.can_focus)
    {
        return;
    }

    /* If no keyboard focus surface is set, then we don't want to focus the view */
    if (v->get_keyboard_focus_surface())
    {
        make_view_visible(v);
        update_active_view(v);
        give_input_focus(select_focus_view(v));
        data.view = v;
        emit(&data);
    }
}

void wf::output_impl_t::focus_view(wayfire_view v, bool raise)
{
    uint32_t flags = 0;
    if (raise)
    {
        flags |= FOCUS_VIEW_RAISE;
    }

    focus_view(v, flags);
}

wayfire_view wf::output_impl_t::get_active_view() const
{
    return this->active_view;
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

bool wf::output_impl_t::can_activate_plugin(wf::plugin_activation_data_t *owner, uint32_t flags)
{
    if (!owner)
    {
        return false;
    }

    if (active_plugins.find(owner) != active_plugins.end())
    {
        return flags & wf::PLUGIN_ACTIVATE_ALLOW_MULTIPLE;
    }

    return can_activate_plugin(owner->capabilities, flags);
}

bool wf::output_impl_t::activate_plugin(wf::plugin_activation_data_t *owner, uint32_t flags)
{
    if (!can_activate_plugin(owner, flags))
    {
        return false;
    }

    if (active_plugins.find(owner) != active_plugins.end())
    {
        LOGD("output ", handle->name, ": activate plugin ", owner->name, " again");
    } else
    {
        LOGD("output ", handle->name, ": activate plugin ", owner->name);
    }

    active_plugins.insert(owner);

    return true;
}

bool wf::output_impl_t::deactivate_plugin(wf::plugin_activation_data_t *owner)
{
    auto it = active_plugins.find(owner);
    if (it == active_plugins.end())
    {
        return true;
    }

    active_plugins.erase(it);
    LOGD("output ", handle->name, ": deactivate plugin ", owner->name);

    if (active_plugins.count(owner) == 0)
    {
        active_plugins.erase(owner);
        return true;
    }

    return false;
}

void wf::output_impl_t::cancel_active_plugins()
{
    std::vector<wf::plugin_activation_data_t*> ifaces;
    for (auto p : active_plugins)
    {
        if (p->cancel)
        {
            ifaces.push_back(p);
        }
    }

    for (auto p : ifaces)
    {
        p->cancel();
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
void output_impl_t::add_key(option_sptr_t<keybinding_t> key, wf::key_callback *callback)
{
    this->key_map[callback] = [=] (const wf::keybinding_t& kb)
    {
        if (this != wf::get_core().get_active_output())
        {
            return false;
        }

        return (*callback)(kb);
    };

    wf::get_core().bindings->add_key(key, &key_map[callback]);
}

void output_impl_t::add_axis(option_sptr_t<keybinding_t> axis,
    wf::axis_callback *callback)
{
    this->axis_map[callback] = [=] (wlr_pointer_axis_event *ax)
    {
        if (this != wf::get_core().get_active_output())
        {
            return false;
        }

        return (*callback)(ax);
    };

    wf::get_core().bindings->add_axis(axis, &axis_map[callback]);
}

void output_impl_t::add_button(option_sptr_t<buttonbinding_t> button,
    wf::button_callback *callback)
{
    this->button_map[callback] = [=] (const wf::buttonbinding_t& kb)
    {
        if (this != wf::get_core().get_active_output())
        {
            return false;
        }

        return (*callback)(kb);
    };

    wf::get_core().bindings->add_button(button, &button_map[callback]);
}

void output_impl_t::add_activator(
    option_sptr_t<activatorbinding_t> activator, wf::activator_callback *callback)
{
    this->activator_map[callback] = [=] (const wf::activator_data_t& act)
    {
        if (act.source == activator_source_t::HOTSPOT)
        {
            auto pos = wf::get_core().get_cursor_position();
            auto wo  = wf::get_core().output_layout->get_output_at(pos.x, pos.y);
            if (this != wo)
            {
                return false;
            }
        } else if (this != wf::get_core().get_active_output())
        {
            return false;
        }

        return (*callback)(act);
    };

    wf::get_core().bindings->add_activator(activator, &activator_map[callback]);
}

template<class Type>
void remove_binding(std::map<Type*, Type>& map, Type *cb)
{
    if (map.count(cb))
    {
        wf::get_core().bindings->rem_binding(&map[cb]);
        map.erase(cb);
    }
}

void wf::output_impl_t::rem_binding(void *callback)
{
    remove_binding(key_map, (key_callback*)callback);
    remove_binding(button_map, (button_callback*)callback);
    remove_binding(axis_map, (axis_callback*)callback);
    remove_binding(activator_map, (activator_callback*)callback);
}
} // namespace wf

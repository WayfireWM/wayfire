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
    this->set_workspace_set(workspace_set_t::create());

    render = std::make_unique<render_manager>(this);
    promotion_manager = std::make_unique<promotion_manager_t>(this);

    on_configuration_changed = [=] (wf::output_configuration_changed_signal *ev)
    {
        update_node_limits();
    };
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

    {
        // Delay freeing old_wset until we can safely report the new value
        auto old_wset = std::move(this->current_wset);
        this->current_wset = wset;
    }

    workspace_set_changed_signal data;
    data.new_wset = wset;
    data.output   = this;
    this->emit(&data);

    wf::get_core().seat->refocus();
}

std::string wf::output_t::to_string() const
{
    return handle->name;
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

    priv_render_manager_clear_instances(render.get());
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

    wf::get_core().set_cursor("default");
    wf::get_core().warp_cursor(target);
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

        wf::output_plugin_activated_changed_signal data;
        data.output = this;
        data.plugin_name = owner->name;
        data.activated   = true;
        this->emit(&data);
        wf::get_core().emit(&data);
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
        wf::output_plugin_activated_changed_signal data;
        data.output = this;
        data.plugin_name = owner->name;
        data.activated   = false;
        this->emit(&data);
        wf::get_core().emit(&data);
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

void wf::output_t::set_inhibited(bool inhibited)
{
    this->inhibited = inhibited;
    if (inhibited)
    {
        cancel_active_plugins();
    }
}

bool wf::output_t::is_inhibited() const
{
    return this->inhibited;
}

namespace wf
{
void output_impl_t::add_key(option_sptr_t<keybinding_t> key, wf::key_callback *callback)
{
    this->key_map[callback] = [=] (const wf::keybinding_t& kb)
    {
        if (this != wf::get_core().seat->get_active_output())
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
        if (this != wf::get_core().seat->get_active_output())
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
        if (this != wf::get_core().seat->get_active_output())
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
        } else if (this != wf::get_core().seat->get_active_output())
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

wayfire_view get_active_view_for_output(wf::output_t *output)
{
    if (output == wf::get_core().seat->get_active_output())
    {
        return wf::get_core().seat->get_active_view();
    }

    return nullptr;
}

void collect_output_nodes_recursive(wf::scene::node_ptr root, wf::output_t *output,
    std::vector<std::shared_ptr<scene::output_node_t>>& result)
{
    if (!root->is_enabled())
    {
        return;
    }

    if (auto output_node = std::dynamic_pointer_cast<scene::output_node_t>(root))
    {
        if (output_node->get_output() == output)
        {
            result.push_back(output_node);
        }

        return;
    }

    for (auto& ch : root->get_children())
    {
        collect_output_nodes_recursive(ch, output, result);
    }
}

std::vector<std::shared_ptr<scene::output_node_t>> collect_output_nodes(
    wf::scene::node_ptr root, wf::output_t *output)
{
    std::vector<std::shared_ptr<scene::output_node_t>> res;
    collect_output_nodes_recursive(root, output, res);
    return res;
}
} // namespace wf

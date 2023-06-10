#include <memory>
#include <wayfire/util/log.hpp>
#include <wayfire/workarea.hpp>
#include "../core/core-impl.hpp"
#include "view-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/workspace-set.hpp"
#include "wayfire/render-manager.hpp"
#include "xdg-shell.hpp"
#include "../core/seat/input-manager.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include "wayfire/signal-definitions.hpp"
#include <wayfire/scene-operations.hpp>

void wf::view_interface_t::set_role(view_role_t new_role)
{
    role = new_role;
    damage();
}

std::string wf::view_interface_t::to_string() const
{
    return "view-" + wf::object_base_t::to_string();
}

wayfire_view wf::view_interface_t::self()
{
    return wayfire_view(this);
}

/** Set the view's output. */
void wf::view_interface_t::set_output(wf::output_t *new_output)
{
    /* Make sure the view doesn't stay on the old output */
    if (get_output() && (get_output() != new_output))
    {
        view_disappeared_signal data_disappeared;
        data_disappeared.view = self();
        get_output()->emit(&data_disappeared);
    }

    view_set_output_signal data;
    data.view   = self();
    data.output = get_output();

    this->priv->output = new_output;

    this->emit(&data);
    if (new_output)
    {
        new_output->emit(&data);
    }

    wf::get_core().emit(&data);
}

wf::output_t*wf::view_interface_t::get_output()
{
    return priv->output;
}

void wf::view_interface_t::ping()
{
    // Do nothing, specialized in the various shells
}

void wf::view_interface_t::close()
{
    /* no-op */
}

wlr_box wf::view_interface_t::get_bounding_box()
{
    return get_transformed_node()->get_bounding_box();
}

bool wf::view_interface_t::is_focusable() const
{
    return priv->keyboard_focus_enabled;
}

void wf::view_interface_t::damage()
{
    wf::scene::damage_node(get_surface_root_node(), get_surface_root_node()->get_bounding_box());
}

bool wf::view_interface_t::has_transformer()
{
    auto ch = get_transformed_node()->get_children();
    return !ch.empty() && ch.front() != get_surface_root_node();
}

void wf::view_interface_t::take_snapshot(wf::render_target_t& target)
{
    auto root_node = get_surface_root_node();
    const wf::geometry_t bbox = root_node->get_bounding_box();
    float scale = get_output()->handle->scale;

    OpenGL::render_begin();
    target.allocate(bbox.width * scale, bbox.height * scale);
    OpenGL::render_end();

    target.geometry = root_node->get_bounding_box();
    target.scale    = scale;

    std::vector<scene::render_instance_uptr> instances;
    root_node->gen_render_instances(instances, [] (auto) {}, get_output());

    scene::render_pass_params_t params;
    params.background_color = {0, 0, 0, 0};
    params.damage    = bbox;
    params.target    = target;
    params.instances = &instances;
    scene::run_render_pass(params, scene::RPASS_CLEAR_BACKGROUND);
}

wf::view_interface_t::view_interface_t()
{
    this->priv = std::make_unique<wf::view_interface_t::view_priv_impl>();
    take_ref();
}

void wf::view_interface_t::set_surface_root_node(scene::floating_inner_ptr surface_root_node)
{
    this->priv->surface_root_node = surface_root_node;
}

void wf::view_interface_t::take_ref()
{
    ++priv->ref_cnt;
}

void wf::view_interface_t::unref()
{
    --priv->ref_cnt;
    if (priv->ref_cnt <= 0)
    {
        destruct();
    }
}

class view_root_node_t : public wf::scene::floating_inner_node_t, public wf::view_node_tag_t
{
  public:
    view_root_node_t(wf::view_interface_t *_view) : floating_inner_node_t(false),
        view_node_tag_t(_view), view(_view)
    {
        view->connect(&on_destruct);
    }

    std::string stringify() const override
    {
        if (view)
        {
            std::ostringstream out;
            out << this->view->self();
            return "view-root-node of " + out.str() + " " + stringify_flags();
        } else
        {
            return "inert view-root-node " + stringify_flags();
        }
    }

  private:
    wf::view_interface_t *view;
    wf::signal::connection_t<wf::view_destruct_signal> on_destruct = [=] (wf::view_destruct_signal *ev)
    {
        view = nullptr;
    };
};

void wf::view_interface_t::initialize()
{
    wf::dassert(priv->surface_root_node != nullptr,
        "View implementations should set the surface root node immediately after creating the view!");

    priv->root_node = std::make_shared<view_root_node_t>(this);
    priv->transformed_node = std::make_shared<scene::transform_manager_node_t>();

    // Set up view content to scene.
    priv->transformed_node->set_children_list({priv->surface_root_node});
    priv->root_node->set_children_list({priv->transformed_node});
    priv->root_node->set_enabled(false);
}

void wf::view_interface_t::deinitialize()
{
    this->_clear_data();
}

wf::view_interface_t::~view_interface_t() = default;

void wf::view_interface_t::destruct()
{
    view_destruct_signal ev;
    ev.view = self();
    this->emit(&ev);
    wf::get_core_impl().erase_view(self());
}

const wf::scene::floating_inner_ptr& wf::view_interface_t::get_root_node() const
{
    return priv->root_node;
}

std::shared_ptr<wf::scene::transform_manager_node_t> wf::view_interface_t::get_transformed_node() const
{
    return priv->transformed_node;
}

const wf::scene::floating_inner_ptr& wf::view_interface_t::get_surface_root_node() const
{
    return priv->surface_root_node;
}

wayfire_view wf::node_to_view(wf::scene::node_t *node)
{
    while (node)
    {
        if (auto vnode = dynamic_cast<view_node_tag_t*>(node))
        {
            return vnode->get_view();
        }

        node = node->parent();
    }

    return nullptr;
}

wayfire_view wf::node_to_view(wf::scene::node_ptr node)
{
    return node_to_view(node.get());
}

wl_client*wf::view_interface_t::get_client()
{
    if (priv->wsurface)
    {
        return wl_resource_get_client(priv->wsurface->resource);
    }

    return nullptr;
}

wlr_surface*wf::view_interface_t::get_wlr_surface()
{
    return priv->wsurface;
}

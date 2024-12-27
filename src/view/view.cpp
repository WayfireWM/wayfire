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
    view_set_output_signal data;
    data.view   = self();
    data.output = get_output(); // the old output

    this->priv->output = new_output;

    this->emit(&data);
    if (new_output)
    {
        new_output->emit(&data);
    }

    wf::get_core().emit(&data);

    if (data.output && (data.output != new_output))
    {
        view_disappeared_signal data_disappeared;
        data_disappeared.view = self();
        data.output->emit(&data_disappeared);
        wf::scene::update(get_root_node(), scene::update_flag::REFOCUS);
    }
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
    return true;
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
}

class sentinel_node_t : public wf::scene::node_t
{
  public:
    sentinel_node_t() : node_t(false)
    {}

    std::string stringify() const
    {
        return "sentinel node (unmapped contents)";
    }
};

void wf::view_interface_t::set_surface_root_node(scene::floating_inner_ptr surface_root_node)
{
    priv->dummy_node = std::make_shared<sentinel_node_t>();
    this->priv->surface_root_node = surface_root_node;

    // Plugins may want to add subsurfaces even before the view is mapped for the first time.
    // This is why we add a dummy node to track the position of the main surface in the order of surfaces
    // even before mapping the view for the first time.
    wf::scene::add_front(surface_root_node, priv->dummy_node);

    // Set up view content to scene.
    priv->transformed_node->set_children_list({surface_root_node});
}

class view_root_node_t : public wf::scene::floating_inner_node_t, public wf::view_node_tag_t
{
  public:
    view_root_node_t(wf::view_interface_t *_view) : floating_inner_node_t(false),
        view_node_tag_t(_view), view(_view->weak_from_this())
    {}

    std::string stringify() const override
    {
        if (auto ptr = view.lock())
        {
            std::ostringstream out;
            out << ptr->self();
            return "view-root-node of " + out.str() + " " + stringify_flags();
        } else
        {
            return "inert view-root-node " + stringify_flags();
        }
    }

  private:
    std::weak_ptr<wf::view_interface_t> view;
};

void wf::view_interface_t::base_initialization()
{
    priv->root_node = std::make_shared<view_root_node_t>(this);
    priv->transformed_node = std::make_shared<scene::transform_manager_node_t>();
    priv->root_node->set_children_list({priv->transformed_node});
    priv->root_node->set_enabled(false);

    priv->pre_free = [=] (auto)
    {
        this->_clear_data();
        if (auto self = dynamic_cast<toplevel_view_interface_t*>(this))
        {
            auto children = self->children;
            for (auto ch : children)
            {
                ch->set_toplevel_parent(nullptr);
            }
        }
    };

    this->connect(&priv->pre_free);
}

wf::view_interface_t::~view_interface_t()
{
    wf::scene::remove_child(get_root_node());
}

const wf::scene::floating_inner_ptr& wf::view_interface_t::get_root_node() const
{
    return priv->root_node;
}

const std::shared_ptr<wf::scene::transform_manager_node_t>& wf::view_interface_t::get_transformed_node() const
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
    if (get_wlr_surface())
    {
        return wl_resource_get_client(get_wlr_surface()->resource);
    }

    return nullptr;
}

wlr_surface*wf::view_interface_t::get_wlr_surface()
{
    return priv->wsurface;
}

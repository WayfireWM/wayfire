#include <limits>
#include <memory>
#include <wayfire/scene.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <set>
#include <algorithm>

#include "scene-priv.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include <wayfire/core.hpp>

namespace wf
{
namespace scene
{
// ---------------------------------- node_t -----------------------------------
node_t::~node_t()
{}

node_t::node_t(bool is_structure)
{
    this->_is_structure = is_structure;
}

void node_t::set_enabled(bool is_active)
{
    enabled_counter += (is_active ? 1 : -1);
}

std::string node_t::stringify_flags() const
{
    std::string fl = "";
    if (flags() & ((int)node_flags::DISABLED))
    {
        fl += "d";
    }

    return "(" + fl + ")";
}

std::optional<input_node_t> node_t::find_node_at(const wf::pointf_t& at)
{
    if (!test_point_in_limit(at))
    {
        return {};
    }

    for (auto& node : get_children())
    {
        auto child_node = node->find_node_at(
            node->to_local(at));
        if (child_node.has_value())
        {
            return child_node;
        }
    }

    return {};
}

static std::vector<node_t*> extract_structure_nodes(
    const std::vector<node_ptr>& list)
{
    std::vector<node_t*> structure;
    for (auto& node : list)
    {
        if (node->is_structure_node())
        {
            structure.push_back(node.get());
        }
    }

    return structure;
}

bool floating_inner_node_t::set_children_list(std::vector<node_ptr> new_list)
{
    // Structure nodes should be sorted in both sequences and be the same.
    // For simplicity, we just extract the nodes in new vectors and check that
    // they are the same.
    //
    // FIXME: this could also be done with a merge-sort-like algorithm in place,
    // but is it worth it here? The scenegraph is supposed to stay static for
    // most of the time.
    if (extract_structure_nodes(children) != extract_structure_nodes(new_list))
    {
        return false;
    }

    set_children_unchecked(std::move(new_list));
    return true;
}

void node_t::set_children_unchecked(std::vector<node_ptr> new_list)
{
    node_damage_signal data;
    data.region |= get_bounding_box();

    for (auto& node : this->children)
    {
        node->_parent = nullptr;
    }

    for (auto& node : new_list)
    {
        node->_parent = this;
    }

    this->children = std::move(new_list);

    data.region |= get_bounding_box();
    this->emit(&data);
}

static int get_layer_index(const wf::scene::node_t *node)
{
    using namespace wf::scene;
    if (auto root = dynamic_cast<root_node_t*>(node->parent()))
    {
        for (int layer = 0; layer < (int)layer::ALL_LAYERS; layer++)
        {
            if (root->layers[layer].get() == node)
            {
                return layer;
            }
        }
    }

    return -1;
}

static bool is_dynamic_output(const wf::scene::node_t *node)
{
    if (auto output = dynamic_cast<wf::scene::output_node_t*>(node->parent()))
    {
        return node == output->dynamic.get();
    }

    return false;
}

static bool is_static_output(const wf::scene::node_t *node)
{
    if (auto output = dynamic_cast<wf::scene::output_node_t*>(node->parent()))
    {
        return node == output->_static.get();
    }

    return false;
}

std::string node_t::stringify() const
{
    std::string description = "node ";
    int layer_idx = get_layer_index(this);

    if (layer_idx >= 0)
    {
        static constexpr const char *layer_names[] = {
            "background",
            "bottom",
            "workspace",
            "top",
            "unmanaged",
            "overlay",
            "dwidget"
        };

        static_assert((sizeof(layer_names) / sizeof(layer_names[0])) ==
            (size_t)layer::ALL_LAYERS);
        description  = "layer_";
        description += layer_names[layer_idx];
    } else if (is_static_output(this))
    {
        description = "static";
    } else if (is_dynamic_output(this))
    {
        description = "dynamic";
    }

    return description + " " + stringify_flags();
}

wf::pointf_t node_t::to_local(const wf::pointf_t& point)
{
    return point;
}

wf::pointf_t node_t::to_global(const wf::pointf_t& point)
{
    return point;
}

class default_render_instance_t : public render_instance_t
{
  protected:
    std::vector<render_instance_uptr> children;
    wf::region_t cached_damage;
    damage_callback push_damage;

    wf::signal::connection_t<node_damage_signal> on_main_node_damaged =
        [=] (node_damage_signal *data)
    {
        push_damage(data->region);
    };

  public:
    default_render_instance_t(node_t *self, damage_callback callback)
    {
        children.reserve(self->get_children().size());
        for (auto& child : self->get_children())
        {
            if (!child->is_disabled())
            {
                children.push_back(child->get_render_instance(callback));
            }
        }

        self->connect(&on_main_node_damaged);
        this->push_damage = callback;
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        for (auto& ch : children)
        {
            ch->schedule_instructions(instructions, target, damage);
        }
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region, wf::output_t *output) override
    {
        wf::dassert(false, "Rendering an inner node?");
    }
};

std::unique_ptr<render_instance_t> node_t::get_render_instance(
    damage_callback damage)
{
    return std::make_unique<default_render_instance_t>(this, damage);
}

wf::geometry_t node_t::get_bounding_box()
{
    if (children.empty())
    {
        return {0, 0, 0, 0};
    }

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    for (auto& ch : children)
    {
        auto bbox = ch->get_bounding_box();

        min_x = std::min(min_x, bbox.x);
        min_y = std::min(min_y, bbox.y);
        max_x = std::max(max_x, bbox.x + bbox.width);
        max_y = std::max(max_y, bbox.y + bbox.height);
    }

    return {min_x, min_y, max_x - min_x, max_y - min_y};
}

// ------------------------------ output_node_t --------------------------------

// FIXME: output nodes are actually structure nodes, but we need to add and
// remove them dynamically ...
output_node_t::output_node_t(wf::output_t *output) : floating_inner_node_t(false)
{
    this->output  = output;
    this->_static = std::make_shared<floating_inner_node_t>(true);
    this->dynamic = std::make_shared<floating_inner_node_t>(true);
    set_children_unchecked({dynamic, _static});
}

std::string output_node_t::stringify() const
{
    return "output " + this->output->to_string() + " " + stringify_flags();
}

wf::pointf_t output_node_t::to_local(const wf::pointf_t& point)
{
    auto offset = wf::origin(output->get_layout_geometry());
    return {point.x - offset.x, point.y - offset.y};
}

wf::pointf_t output_node_t::to_global(const wf::pointf_t& point)
{
    auto offset = wf::origin(output->get_layout_geometry());
    return {point.x + offset.x, point.y + offset.y};
}

class output_render_instance_t : public default_render_instance_t
{
    wf::output_t *output;

  public:
    output_render_instance_t(node_t *self, damage_callback callback,
        wf::output_t *output) :
        default_render_instance_t(self, transform_damage(callback))
    {
        this->output = output;
    }

    damage_callback transform_damage(damage_callback child_damage)
    {
        return [=] (const wf::region_t& damage)
        {
            child_damage(damage + wf::origin(output->get_layout_geometry()));
        };
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        // In principle, we just have to schedule the children.
        // However, we need to adjust the target's geometry and the damage to
        // fit with the coordinate system of the output.
        wf::render_target_t new_target = target;
        auto offset = wf::origin(output->get_layout_geometry());
        new_target.geometry.x -= offset.x;
        new_target.geometry.y -= offset.y;

        damage += -offset;
        for (auto& ch : children)
        {
            ch->schedule_instructions(instructions, new_target, damage);
        }

        damage += offset;
    }
};

std::unique_ptr<render_instance_t> output_node_t::get_render_instance(
    damage_callback push_damage)
{
    return std::make_unique<output_render_instance_t>(this, push_damage, output);
}

wf::geometry_t output_node_t::get_bounding_box()
{
    const auto bbox = node_t::get_bounding_box();
    return bbox + wf::origin(output->get_layout_geometry());
}

// ------------------------------ root_node_t ----------------------------------
root_node_t::root_node_t() : node_t(true)
{
    std::vector<node_ptr> children;

    this->priv = std::make_unique<root_node_t::priv_t>();
    for (int i = (int)layer::ALL_LAYERS - 1; i >= 0; i--)
    {
        layers[i] = std::make_shared<floating_inner_node_t>(true);
        children.push_back(layers[i]);
    }

    set_children_unchecked(children);
}

root_node_t::~root_node_t()
{}

std::string root_node_t::stringify() const
{
    return "root " + stringify_flags();
}

// ---------------------- generic scenegraph functions -------------------------
void set_node_enabled(wf::scene::node_ptr node, bool enabled)
{
    bool was_disabled = node->is_disabled();
    node->set_enabled(enabled);
    if (was_disabled != node->is_disabled())
    {
        update(node, update_flag::ENABLED);
    }
}

void update(node_ptr changed_node, uint32_t flags)
{
    if ((flags & update_flag::CHILDREN_LIST) ||
        (flags & update_flag::ENABLED))
    {
        flags |= update_flag::INPUT_STATE;
    }

    if (changed_node == wf::get_core().scene())
    {
        root_node_update_signal data;
        data.flags = flags;
        wf::get_core().scene()->emit(&data);
        return;
    }

    if (changed_node->parent())
    {
        update(changed_node->parent()->shared_from_this(), flags);
    }
}
} // namespace scene
}

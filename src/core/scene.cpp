#include <wayfire/scene.hpp>
#include <wayfire/view.hpp>
#include <set>
#include <algorithm>

#include "scene-priv.hpp"

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

std::string node_t::stringify() const
{
    return "generic-node " + stringify_flags();
}

// ------------------------------- inner_node_t --------------------------------

// ------------------------------- inner_node_t --------------------------------
inner_node_t::inner_node_t(bool _is_structure) : node_t(_is_structure)
{}

std::optional<input_node_t> inner_node_t::find_node_at(const wf::pointf_t& at)
{
    for (auto& node : get_children())
    {
        auto child_node = node->find_node_at(at);
        if (child_node.has_value())
        {
            return child_node;
        }
    }

    return {};
}

iteration inner_node_t::visit(visitor_t *visitor)
{
    auto proceed = visitor->inner_node(this);
    switch (proceed)
    {
      case iteration::STOP:
        // Tell parent to stop
        return iteration::STOP;

      case iteration::ALL:
        // Go through all children and see what they want
        for (auto& ch : get_children())
        {
            if (ch->flags() & (int)node_flags::DISABLED)
            {
                continue;
            }

            if (ch->visit(visitor) == iteration::STOP)
            {
                return iteration::STOP;
            }
        }

      // fallthrough
      case iteration::SKIP_CHILDREN:
        return iteration::ALL;
    }

    assert(false);
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

void inner_node_t::set_children_unchecked(std::vector<node_ptr> new_list)
{
    for (auto& node : new_list)
    {
        node->_parent = this;
    }

    this->children = std::move(new_list);
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

std::string inner_node_t::stringify() const
{
    std::string description = "inner";
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

// ------------------------------ output_node_t --------------------------------

// FIXME: output nodes are actually structure nodes, but we need to add and
// remove them dynamically ...
output_node_t::output_node_t() : inner_node_t(false)
{
    this->_static = std::make_shared<floating_inner_node_t>(true);
    this->dynamic = std::make_shared<floating_inner_node_t>(true);
    set_children_unchecked({dynamic, _static});
}

std::string output_node_t::stringify() const
{
    // FIXME: would be better to dump the name of the output here...
    return "output " + stringify_flags();
}

// ------------------------------ root_node_t ----------------------------------
root_node_t::root_node_t() : inner_node_t(true)
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

void root_node_t::update()
{}

std::string root_node_t::stringify() const
{
    return "root " + stringify_flags();
}
} // namespace scene
}

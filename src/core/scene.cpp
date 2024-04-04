#include <limits>
#include <memory>
#include <wayfire/scene.hpp>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <algorithm>

#include "scene-priv.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/signal-provider.hpp"
#include <wayfire/core.hpp>

namespace wf
{
bool keyboard_focus_node_t::operator <(const keyboard_focus_node_t& other) const
{
    if (this->importance == other.importance)
    {
        if (this->node && other.node)
        {
            auto ts_self  = node->keyboard_interaction().last_focus_timestamp;
            auto ts_other = other.node->keyboard_interaction().last_focus_timestamp;
            return ts_self < ts_other;
        }

        if (!this->node && !other.node)
        {
            return false;
        }

        // Prefer to focus with node
        return !this->node;
    }

    return this->importance < other.importance;
}

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

    if (flags() & ((int)node_flags::RAW_INPUT))
    {
        fl += "R";
    }

    return "(" + fl + ")";
}

std::optional<input_node_t> node_t::find_node_at(const wf::pointf_t& at)
{
    auto local = this->to_local(at);
    for (auto& node : get_children())
    {
        if (!node->is_enabled())
        {
            continue;
        }

        auto child_node = node->find_node_at(local);
        if (child_node.has_value())
        {
            return child_node;
        }
    }

    return {};
}

wf::keyboard_focus_node_t node_t::keyboard_refocus(wf::output_t *output)
{
    wf::keyboard_focus_node_t result;

    for (auto& ch : this->get_children())
    {
        if (!ch->is_enabled())
        {
            continue;
        }

        auto ch_focus = ch->keyboard_refocus(output);
        result = std::max(result, ch_focus);

        if (!ch_focus.allow_focus_below)
        {
            result.allow_focus_below = false;
            break;
        }
    }

    return result;
}

bool floating_inner_node_t::set_children_list(std::vector<node_ptr> new_list)
{
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
        wf::dassert(node->parent() == nullptr, "Adding a child node twice!");
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
            "lock",
            "dwidget"
        };

        static_assert((sizeof(layer_names) / sizeof(layer_names[0])) ==
            (size_t)layer::ALL_LAYERS);
        description  = "layer_";
        description += layer_names[layer_idx];
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

// Just listen for damage from the node and push it upwards
class default_render_instance_t : public render_instance_t
{
  protected:
    damage_callback push_damage;

    wf::signal::connection_t<node_damage_signal> on_main_node_damaged =
        [=] (node_damage_signal *data)
    {
        push_damage(data->region);
    };

  public:
    default_render_instance_t(node_t *self, damage_callback callback)
    {
        this->push_damage = callback;
        self->connect(&on_main_node_damaged);
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        // nothing to render here
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        wf::dassert(false, "Rendering an inner node?");
    }

    direct_scanout try_scanout(wf::output_t *output) override
    {
        // Nodes without actual visual content do not prevent further nodes
        // from being scanned out.
        return direct_scanout::SKIP;
    }
};

void node_t::gen_render_instances(std::vector<render_instance_uptr> & instances,
    damage_callback push_damage, wf::output_t *output)
{
    // Add self for damage tracking
    instances.push_back(
        std::make_unique<default_render_instance_t>(this, push_damage));

    // Add children as a flat list to avoid multiple indirections
    for (auto& ch : this->children)
    {
        if (ch->is_enabled())
        {
            ch->gen_render_instances(instances, push_damage, output);
        }
    }
}

wf::geometry_t node_t::get_children_bounding_box()
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

wf::geometry_t node_t::get_bounding_box()
{
    return get_children_bounding_box();
}

uint32_t node_t::optimize_update(uint32_t flags)
{
    if (!this->is_enabled())
    {
        return flags | update_flag::MASKED;
    }

    return flags;
}

// ------------------------------ output_node_t --------------------------------
output_node_t::output_node_t(wf::output_t *output) : floating_inner_node_t(true)
{
    this->output = output;
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

std::optional<input_node_t> output_node_t::find_node_at(const wf::pointf_t& at)
{
    if (limit_region && !(*limit_region & at))
    {
        return {};
    }

    return node_t::find_node_at(at);
}

class output_render_instance_t : public default_render_instance_t
{
    wf::output_t *output;
    output_node_t *self;
    std::vector<render_instance_uptr> children;

  public:
    output_render_instance_t(output_node_t *self, damage_callback callback,
        wf::output_t *output, wf::output_t *shown_on) :
        default_render_instance_t(self, transform_damage(callback))
    {
        this->self   = self;
        this->output = output;

        // Children are stored as a sublist, because we need to translate every
        // time between global and output-local geometry.
        for (auto& child : self->get_children())
        {
            if (child->is_enabled())
            {
                child->gen_render_instances(children,
                    transform_damage(callback), shown_on);
            }
        }
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
        if (self->limit_region)
        {
            wf::region_t our_damage = damage & *self->limit_region;
            our_damage &= target.geometry;
            if (!our_damage.empty())
            {
                _schedule_instructions(instructions, target, our_damage);
                // Inside limit region, damage is defined as the children
                // decided.
                damage ^= *self->limit_region;
                damage |= our_damage & *self->limit_region;
            }
        } else
        {
            _schedule_instructions(instructions, target, damage);
        }
    }

    void _schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage)
    {
        // In principle, we just have to schedule the children.
        // However, we need to adjust the target's geometry and the damage to
        // fit with the coordinate system of the output.
        auto offset = wf::origin(output->get_layout_geometry());
        wf::render_target_t new_target = target.translated(-offset);

        damage += -offset;
        for (auto& ch : children)
        {
            ch->schedule_instructions(instructions, new_target, damage);
        }

        damage += offset;
    }

    direct_scanout try_scanout(wf::output_t *scanout) override
    {
        if ((scanout != this->output) && this->self->limit_region)
        {
            // Can't scanout on a different output because it is outside
            // of the limit region
            return direct_scanout::SKIP;
        }

        for (auto& ch : children)
        {
            auto res = ch->try_scanout(scanout);
            if (res != direct_scanout::SKIP)
            {
                return res;
            }
        }

        return direct_scanout::SKIP;
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        auto offset = wf::origin(output->get_layout_geometry());
        compute_visibility_from_list(children, output, visible, offset);
    }
};

void output_node_t::gen_render_instances(
    std::vector<render_instance_uptr> & instances, damage_callback push_damage,
    wf::output_t *shown_on)
{
    if (this->limit_region && shown_on && (shown_on != this->output))
    {
        // If the limit region is set and we are limiting the generation of
        // instances to a particular region (typically an output), make sure we
        // generate instances only if the output will be visible.
        return;
    }

    instances.push_back(
        std::make_unique<output_render_instance_t>(this, push_damage, output,
            shown_on));
}

wf::geometry_t output_node_t::get_bounding_box()
{
    const auto bbox = node_t::get_bounding_box();
    return bbox + wf::origin(output->get_layout_geometry());
}

// ------------------------------ root_node_t ----------------------------------
root_node_t::root_node_t() : floating_inner_node_t(true)
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
    bool was_enabled = node->is_enabled();
    node->set_enabled(enabled);
    if (was_enabled != node->is_enabled())
    {
        if (node->parent())
        {
            node_damage_signal ev;
            ev.region = node->get_bounding_box();
            node->parent()->emit(&ev);
        }

        update(node, update_flag::ENABLED);
    }
}

void update(node_ptr changed_node, uint32_t flags)
{
    if ((flags & update_flag::CHILDREN_LIST) ||
        (flags & update_flag::ENABLED) ||
        (flags & update_flag::GEOMETRY))
    {
        flags |= update_flag::INPUT_STATE;
    }

    if (!changed_node->is_enabled() &&
        !(flags & update_flag::ENABLED))
    {
        flags |= update_flag::MASKED;
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
        flags = changed_node->parent()->optimize_update(flags);
        if (!changed_node->parent()->is_enabled())
        {
            flags |= update_flag::MASKED;
        }

        update(changed_node->parent()->shared_from_this(), flags);
    }
}

floating_inner_node_t::~floating_inner_node_t()
{
    for (auto& node : this->children)
    {
        node->_parent = nullptr;
    }
}

uint32_t optimize_nested_render_instances(wf::scene::node_ptr node, uint32_t flags)
{
    if (flags & (update_flag::CHILDREN_LIST | update_flag::ENABLED))
    {
        // If we update the list of children, there is no need to notify the whole scenegraph.
        // Instead, we can do a local update, and only update visibility.
        flags &= ~update_flag::CHILDREN_LIST;
        flags &= ~update_flag::ENABLED;
        flags |= update_flag::GEOMETRY | update_flag::INPUT_STATE;
        node_regen_instances_signal data;
        node->emit(&data);
    }

    return flags;
}
} // namespace scene
}

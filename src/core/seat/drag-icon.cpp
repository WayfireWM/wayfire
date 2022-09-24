#include "drag-icon.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/debug.hpp"
#include "../core-impl.hpp"
#include "seat.hpp"
#include "wayfire/signal-provider.hpp"
#include <memory>
#include <type_traits>

namespace wf
{
namespace scene
{
class dnd_icon_root_render_instance_t : public render_instance_t
{
    std::vector<render_instance_uptr> children;
    wf::drag_icon_t *icon;
    damage_callback push_damage;
    wf::signal::connection_t<node_damage_signal> on_damage =
        [=] (node_damage_signal *data)
    {
        push_damage(data->region);
    };

  public:
    dnd_icon_root_render_instance_t(node_t *self,
        wf::drag_icon_t *icon, damage_callback push_damage)
    {
        this->icon = icon;
        this->push_damage = push_damage;
        self->connect(&on_damage);

        auto transformed_push_damage = [icon, push_damage] (wf::region_t region)
        {
            region += icon->get_position();
            push_damage(region);
        };

        for (auto& ch : self->get_children())
        {
            if (ch->is_enabled())
            {
                ch->gen_render_instances(children, transformed_push_damage);
            }
        }
    }

    void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        wf::render_target_t our_target = target;
        our_target.geometry = our_target.geometry + -icon->get_position();

        damage += -icon->get_position();
        for (auto& ch : this->children)
        {
            ch->schedule_instructions(instructions, our_target, damage);
        }

        damage += icon->get_position();
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region, wf::output_t *output) override
    {
        wf::dassert(false, "Rendering a drag icon root node?");
    }
};

class dnd_root_icon_root_node_t : public floating_inner_node_t
{
    wf::drag_icon_t *icon;

  public:
    dnd_root_icon_root_node_t(drag_icon_t *icon) : floating_inner_node_t(false)
    {
        this->icon = icon;
    }

    /**
     * Views currently gather damage, etc. manually from the surfaces,
     * and sometimes render them, sometimes not ...
     */
    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage) override
    {
        instances.push_back(std::make_unique<dnd_icon_root_render_instance_t>(
            this, icon, push_damage));
    }

    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        // Don't allow focus going to the DnD surface itself
        return {};
    }

    wf::geometry_t get_bounding_box() override
    {
        return icon->last_box;
    }

    std::string stringify() const override
    {
        return "dnd-icon " + stringify_flags();
    }
};
}
}

/* ------------------------ Drag icon impl ---------------------------------- */
wf::drag_icon_t::drag_icon_t(wlr_drag_icon *ic) :
    wf::wlr_child_surface_base_t(this), icon(ic)
{
    on_map.set_callback([&] (void*) { this->map(icon->surface); });
    on_unmap.set_callback([&] (void*) { this->unmap(); });
    on_destroy.set_callback([&] (void*)
    {
        /* we don't dec_keep_count() because the surface memory is
         * managed by the unique_ptr */
        wf::get_core_impl().seat->drag_icon = nullptr;
    });

    on_map.connect(&icon->events.map);
    on_unmap.connect(&icon->events.unmap);
    on_destroy.connect(&icon->events.destroy);

    // Change the root node with a dnd node
    auto children = this->priv->root_node->get_children();
    this->priv->root_node->set_children_list({});
    this->priv->root_node = std::make_unique<scene::dnd_root_icon_root_node_t>(this);
    this->priv->root_node->set_children_list(children);

    wf::scene::add_front(wf::get_core().scene(), this->priv->root_node);
}

wf::drag_icon_t::~drag_icon_t()
{
    wf::scene::remove_child(this->priv->root_node);
}

wf::point_t wf::drag_icon_t::get_position()
{
    auto pos = icon->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH ?
        wf::get_core().get_touch_position(icon->drag->touch_id) :
        wf::get_core().get_cursor_position();

    if (is_mapped())
    {
        pos.x += icon->surface->sx;
        pos.y += icon->surface->sy;
    }

    return {(int)pos.x, (int)pos.y};
}

void wf::drag_icon_t::update_position()
{
    // damage previous position
    scene::node_damage_signal data;
    data.region |= last_box;
    last_box     = wf::construct_box(get_position(), get_size());
    data.region |= last_box;
    this->priv->root_node->emit(&data);
}

void wf::drag_icon_t::damage_surface_box(const wlr_box& box)
{
    if (!is_mapped())
    {
        return;
    }

    scene::node_damage_signal data;
    data.region |= box + get_position();
    this->priv->root_node->emit(&data);
}

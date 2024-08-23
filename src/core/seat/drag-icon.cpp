#include "drag-icon.hpp"
#include "wayfire/unstable/wlr-surface-controller.hpp"
#include "wayfire/unstable/wlr-surface-node.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "../core-impl.hpp"
#include "wayfire/signal-provider.hpp"
#include <memory>
#include "seat-impl.hpp"

namespace wf
{
namespace scene
{
class dnd_root_icon_root_node_t : public floating_inner_node_t
{
    class dnd_icon_root_render_instance_t : public render_instance_t
    {
        std::vector<render_instance_uptr> children;
        damage_callback push_damage;
        wf::signal::connection_t<node_damage_signal> on_damage =
            [=] (node_damage_signal *data)
        {
            push_damage(data->region);
        };

        std::weak_ptr<dnd_root_icon_root_node_t> _self;

      public:
        dnd_icon_root_render_instance_t(dnd_root_icon_root_node_t *self, damage_callback push_damage)
        {
            this->_self = std::dynamic_pointer_cast<dnd_root_icon_root_node_t>(self->shared_from_this());
            this->push_damage = push_damage;
            self->connect(&on_damage);

            auto transformed_push_damage = [this] (wf::region_t region)
            {
                if (auto self = _self.lock())
                {
                    region += self->get_position();
                    this->push_damage(region);
                }
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
            auto self = _self.lock();
            if (!self)
            {
                return;
            }

            wf::render_target_t our_target = target.translated(-self->get_position());

            damage += -self->get_position();
            for (auto& ch : this->children)
            {
                ch->schedule_instructions(instructions, our_target, damage);
            }

            damage += self->get_position();
        }

        void render(const wf::render_target_t& target,
            const wf::region_t& region) override
        {
            wf::dassert(false, "Rendering a drag icon root node?");
        }

        void compute_visibility(wf::output_t *output, wf::region_t& visible) override
        {
            if (auto self = _self.lock())
            {
                compute_visibility_from_list(children, output, visible, self->get_position());
            }
        }
    };

  public:
    wf::drag_icon_t *icon;
    dnd_root_icon_root_node_t(drag_icon_t *icon) : floating_inner_node_t(false)
    {
        this->icon = icon;
    }

    /**
     * Views currently gather damage, etc. manually from the surfaces,
     * and sometimes render them, sometimes not ...
     */
    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<dnd_icon_root_render_instance_t>(this, push_damage));
    }

    std::optional<input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        // Don't allow focus going to the DnD surface itself
        return {};
    }

    wf::geometry_t get_bounding_box() override
    {
        if (icon)
        {
            return icon->last_box;
        } else
        {
            return {0, 0, 0, 0};
        }
    }

    std::string stringify() const override
    {
        return "dnd-icon " + stringify_flags();
    }

    wf::point_t get_position()
    {
        if (icon)
        {
            return icon->get_position();
        } else
        {
            return {0, 0};
        }
    }
};
}
}

/* ------------------------ Drag icon impl ---------------------------------- */
wf::drag_icon_t::drag_icon_t(wlr_drag_icon *ic) : icon(ic)
{
    this->root_node = std::make_shared<scene::dnd_root_icon_root_node_t>(this);

    // Sometimes, the drag surface is reused between two or more drags.
    // In this case, when the drag starts, the icon is already mapped.
    if (!icon->surface->mapped)
    {
        root_node->set_enabled(false);
    }

    on_map.set_callback([=] (void*)
    {
        wf::scene::set_node_enabled(root_node, true);
    });
    on_unmap.set_callback([&] (void*)
    {
        wf::scene::set_node_enabled(root_node, false);
    });
    on_destroy.set_callback([&] (void*)
    {
        /* we don't dec_keep_count() because the surface memory is
         * managed by the unique_ptr */
        wf::get_core().seat->priv->drag_icon = nullptr;
    });

    on_map.connect(&icon->surface->events.map);
    on_unmap.connect(&icon->surface->events.unmap);
    on_destroy.connect(&icon->events.destroy);


    auto main_node = std::make_shared<scene::wlr_surface_node_t>(icon->surface, true);
    root_node->set_children_list({main_node});

    // Memory is auto-freed when the wlr_surface is destroyed
    wlr_surface_controller_t::create_controller(icon->surface, root_node);

    // Connect to the scenegraph
    wf::scene::readd_front(wf::get_core().scene(), root_node);
}

wf::drag_icon_t::~drag_icon_t()
{
    root_node->icon = nullptr;
    wf::scene::remove_child(root_node);
}

wf::point_t wf::drag_icon_t::get_position()
{
    auto pos = icon->drag->grab_type == WLR_DRAG_GRAB_KEYBOARD_TOUCH ?
        wf::get_core().get_touch_position(icon->drag->touch_id) :
        wf::get_core().get_cursor_position();

    if (root_node->is_enabled())
    {
        pos.x += icon->surface->current.dx;
        pos.y += icon->surface->current.dy;
    }

    return {(int)pos.x, (int)pos.y};
}

void wf::drag_icon_t::update_position()
{
    // damage previous position
    wf::region_t dmg_region;
    dmg_region |= last_box;
    last_box    =
        wf::construct_box(get_position(), {icon->surface->current.width, icon->surface->current.height});
    dmg_region |= last_box;
    scene::damage_node(root_node, dmg_region);
}

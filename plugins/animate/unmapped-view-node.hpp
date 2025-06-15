#pragma once

#include "wayfire/geometry.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include <wayfire/view.hpp>

namespace wf
{
class unmapped_view_snapshot_node : public wf::scene::node_t
{
    wf::auxilliary_buffer_t snapshot;
    wf::dimensions_t snapshot_logical_size;
    std::weak_ptr<wf::view_interface_t> _view;

  public:
    unmapped_view_snapshot_node(wayfire_view view) : node_t(false)
    {
        view->take_snapshot(snapshot);
        snapshot_logical_size = wf::dimensions(view->get_surface_root_node()->get_bounding_box());
        _view = view->weak_from_this();
    }

    wf::geometry_t get_bounding_box() override
    {
        if (auto view = _view.lock())
        {
            auto current_bbox = view->get_surface_root_node()->get_bounding_box();
            return wf::construct_box(wf::origin(current_bbox), snapshot_logical_size);
        }

        return {0, 0, 0, 0};
    }

    void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
        scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<rinstance_t>(this, push_damage, shown_on));
    }

    std::string stringify() const override
    {
        return "unmapped-view-snapshot-node " + this->stringify_flags();
    }

  private:
    class rinstance_t : public wf::scene::simple_render_instance_t<unmapped_view_snapshot_node>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;
        void render(const wf::scene::render_instruction_t& data)
        {
            wf::texture_t texture = wf::texture_t{self->snapshot.get_texture()};
            data.pass->add_texture(texture, data.target, self->get_bounding_box(), data.damage);
        }
    };
};
}

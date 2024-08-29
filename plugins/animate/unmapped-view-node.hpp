#pragma once

#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include <wayfire/view.hpp>

namespace wf
{
class unmapped_view_snapshot_node : public wf::scene::node_t
{
    wf::render_target_t snapshot;
    wf::geometry_t bbox;

  public:
    unmapped_view_snapshot_node(wayfire_view view) : node_t(false)
    {
        view->take_snapshot(snapshot);
        bbox = view->get_surface_root_node()->get_bounding_box();
    }

    ~unmapped_view_snapshot_node()
    {
        OpenGL::render_begin();
        snapshot.release();
        OpenGL::render_end();
    }

    wf::geometry_t get_bounding_box() override
    {
        return bbox;
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
        void render(const wf::render_target_t& target, const wf::region_t& region)
        {
            OpenGL::render_begin(target);
            for (auto& box : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));
                OpenGL::render_texture(self->snapshot.tex, target, self->get_bounding_box());
            }

            OpenGL::render_end();
        }
    };
};
}

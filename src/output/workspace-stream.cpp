#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"
#include <wayfire/config/types.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>

namespace wf
{
class workspace_stream_node_t::workspace_stream_instance_t : public scene::
    render_instance_t
{
    workspace_stream_node_t *self;

    std::vector<scene::render_instance_uptr> instances;
    // True for each instance generated from a desktop environment view.
    std::vector<bool> is_desktop_environment;

    wf::point_t get_offset()
    {
        auto g   = self->output->get_relative_geometry();
        auto cws = self->output->wset()->get_current_workspace();
        return wf::point_t{
            (self->ws.x - cws.x) * g.width,
            (self->ws.y - cws.y) * g.height,
        };
    }

  public:
    workspace_stream_instance_t(workspace_stream_node_t *self,
        scene::damage_callback push_damage)
    {
        this->self = self;
        auto translate_and_push_damage = [this, push_damage] (wf::region_t damage)
        {
            damage += -get_offset();
            push_damage(damage);
        };

        for (auto& output_node : wf::collect_output_nodes(wf::get_core().scene(), self->output))
        {
            for (auto& ch : output_node->get_children())
            {
                if (ch->is_enabled())
                {
                    auto view = node_to_view(ch);
                    const bool is_de     = (view && (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT));
                    size_t num_generated = this->instances.size();

                    // We push the damage as-is for desktop environment views, because they are visible on
                    // every workspace.
                    ch->gen_render_instances(this->instances,
                        is_de ? push_damage : translate_and_push_damage, self->output);

                    // Mark whether the instances were generated from a desktop environment view
                    num_generated = this->instances.size() - num_generated;
                    for (size_t i = 0; i < num_generated; i++)
                    {
                        is_desktop_environment.push_back(is_de);
                    }
                }
            }

            wf::dassert(instances.size() == is_desktop_environment.size(), "Setting de flag is wrong!");
        }
    }

    void schedule_instructions(
        std::vector<scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        auto bbox = self->get_bounding_box();
        auto our_damage = damage & bbox;
        if (!our_damage.empty())
        {
            auto offset = get_offset();
            wf::render_target_t subtarget = target.translated(offset);

            our_damage += offset;
            for (size_t i = 0; i < instances.size(); i++)
            {
                if (is_desktop_environment[i])
                {
                    // Special handling: move everything to 'current workspace' so that panels and backgrounds
                    // render at the correct position.
                    our_damage -= offset;
                    instances[i]->schedule_instructions(instructions, target, our_damage);
                    our_damage += offset;
                } else
                {
                    instances[i]->schedule_instructions(instructions, subtarget, our_damage);
                }
            }

            our_damage += -offset;

            damage ^= bbox; // Subtract the workspace because it will be filled
                            // with the background color, so nothing below it
                            // should be repainted anyway.
            instructions.push_back(scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = our_damage,
                });
        }
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        static wf::option_wrapper_t<wf::color_t> background_color_opt{
            "core/background_color"
        };

        auto color = self->background.value_or(background_color_opt);

        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::clear(color);
        }

        OpenGL::render_end();
    }

    void presentation_feedback(wf::output_t *output) override
    {
        for (auto& ch : this->instances)
        {
            ch->presentation_feedback(output);
        }
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        scene::compute_visibility_from_list(instances, output, visible, -get_offset());
    }
};

workspace_stream_node_t::workspace_stream_node_t(
    wf::output_t *output, wf::point_t workspace) :
    scene::node_t(false), output(output), ws(workspace)
{}

wf::geometry_t workspace_stream_node_t::get_bounding_box()
{
    return output->get_relative_geometry();
}

void workspace_stream_node_t::gen_render_instances(
    std::vector<scene::render_instance_uptr>& instances,
    scene::damage_callback push_damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<workspace_stream_instance_t>(this,
        push_damage));
}

std::string workspace_stream_node_t::stringify() const
{
    return "workspace-stream of output " + output->to_string() +
           " workspace " + std::to_string(ws.x) + "," + std::to_string(ws.y);
}
} // namespace wf

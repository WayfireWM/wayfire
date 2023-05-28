#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
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
        auto acc_damage = [this, push_damage] (wf::region_t damage)
        {
            damage += -get_offset();
            push_damage(damage);
        };

        for (int layer = (int)scene::layer::ALL_LAYERS - 1; layer >= 0; layer--)
        {
            auto layer_root = self->output->node_for_layer((scene::layer)layer);
            for (auto& ch : layer_root->get_children())
            {
                if (ch->is_enabled())
                {
                    ch->gen_render_instances(this->instances, acc_damage,
                        self->output);
                }
            }
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
            for (auto& ch : instances)
            {
                ch->schedule_instructions(instructions, subtarget, our_damage);
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

// ------------------------------ old workspace stream impl --------------------
void workspace_stream_t::update_instances()
{
    if (!this->current_output)
    {
        return;
    }

    auto acc_damage = [this] (const wf::region_t& damage)
    {
        this->accumulated_damage |= damage;
    };

    this->instances.clear();
    for (int layer = (int)scene::layer::ALL_LAYERS - 1; layer >= 0; layer--)
    {
        auto layer_root = current_output->node_for_layer((scene::layer)layer);
        for (auto& ch : layer_root->get_children())
        {
            if (ch->is_enabled())
            {
                ch->gen_render_instances(this->instances, acc_damage);
            }
        }
    }
}

void workspace_stream_t::start_for_workspace(wf::output_t *output,
    wf::point_t workspace)
{
    wf::dassert(this->current_output == nullptr,
        "Starting an active workspace stream!");

    this->ws = workspace;
    this->current_output = output;

    /* damage the whole workspace region, so that we get a full repaint
     * when updating the workspace */
    this->accumulated_damage |= output->render->get_ws_box(workspace);

    this->regen_instances = [=] (scene::root_node_update_signal *data)
    {
        if ((data->flags & scene::update_flag::ENABLED) ||
            (data->flags & scene::update_flag::CHILDREN_LIST))
        {
            update_instances();
        }
    };

    wf::get_core().scene()->connect(&regen_instances);
    this->update_instances();
}

void workspace_stream_t::render_frame()
{
    wf::dassert(current_output != nullptr,
        "Inactive workspace stream being rendered?");

    this->accumulated_damage &= current_output->render->get_ws_box(ws);
    if (this->accumulated_damage.empty())
    {
        return;
    }

    OpenGL::render_begin();
    buffer.allocate(current_output->handle->width, current_output->handle->height);
    OpenGL::render_end();

    scene::render_pass_params_t params;

    auto g   = current_output->get_relative_geometry();
    auto cws = current_output->wset()->get_current_workspace();
    params.target = current_output->render->get_target_framebuffer()
        .translated({(ws.x - cws.x) * g.width, (ws.y - cws.y) * g.height});

    /* Use the workspace buffers */
    params.target.fb  = this->buffer.fb;
    params.target.tex = this->buffer.tex;

    wf::option_wrapper_t<wf::color_t> background_color_opt{"core/background_color"};
    params.background_color =
        (this->background.a < 0 ? background_color_opt : this->background);

    params.instances = &this->instances;
    params.damage    = accumulated_damage;
    params.reference_output = current_output;

    scene::run_render_pass(params,
        scene::RPASS_EMIT_SIGNALS | scene::RPASS_CLEAR_BACKGROUND);
}

void workspace_stream_t::stop()
{
    this->current_output = nullptr;
    this->accumulated_damage.clear();
    this->instances.clear();
    regen_instances.disconnect();
}
} // namespace wf

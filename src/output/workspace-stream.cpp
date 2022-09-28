#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include <wayfire/config/types.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>

namespace wf
{
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

    params.target = current_output->render->get_target_framebuffer();

    /* Use the workspace buffers */
    params.target.fb  = this->buffer.fb;
    params.target.tex = this->buffer.tex;

    auto g   = current_output->get_relative_geometry();
    auto cws = current_output->workspace->get_current_workspace();
    params.target.geometry.x = (ws.x - cws.x) * g.width,
    params.target.geometry.y = (ws.y - cws.y) * g.height;

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
}

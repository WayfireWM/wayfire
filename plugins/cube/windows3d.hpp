#pragma once
/* 3D windows for the cube -- like Compiz's
 * "3D Windows": every window of a face is drawn as its own layer, lifted
 * off the face; the face itself shows the desktop without these windows. */

#include <set>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/render.hpp>
#include <wayfire/option-wrapper.hpp>

namespace cube3d
{
/* The toplevel windows of workspace @ws which are in the WORKSPACE layer
 * (not minimized, mapped), topmost first. Returned as root nodes: the
 * node of a window includes its dialogs. */
inline std::vector<wf::scene::node_ptr> workspace_windows(wf::output_t *output, wf::point_t ws)
{
    std::vector<wf::scene::node_ptr> result;
    auto ws_layer = output->node_for_layer(wf::scene::layer::WORKSPACE).get();
    auto views    = output->wset()->get_views(wf::WSET_MAPPED_ONLY |
        wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_SORT_STACKING, ws);
    for (auto& view : views)
    {
        auto root = view->get_root_node();
        /* only windows directly in the workspace set of the WORKSPACE layer
         * (child windows are drawn with their parent) */
        auto parent = root->parent();
        if (!parent || (parent->parent() != ws_layer) || !root->is_enabled())
        {
            continue;
        }

        result.push_back(root);
    }

    return result;
}

/* A copy of wf::workspace_stream_node_t (src/output/workspace-stream.cpp)
 * which leaves out the given window nodes. */
class base_stream_node_t : public wf::scene::node_t
{
  public:
    base_stream_node_t(wf::output_t *output, wf::point_t ws,
        std::set<wf::scene::node_t*> skip) :
        wf::scene::node_t(false), output(output), ws(ws), skip(std::move(skip))
    {}

    wf::output_t *output;
    const wf::point_t ws;
    /* compared only, never dereferenced */
    const std::set<wf::scene::node_t*> skip;

    wf::geometry_t get_bounding_box() override
    {
        return output->get_relative_geometry();
    }

    std::string stringify() const override
    {
        return "cube base stream " + std::to_string(ws.x) + "," + std::to_string(ws.y);
    }

    class instance_t : public wf::scene::render_instance_t
    {
        base_stream_node_t *self;
        std::vector<wf::scene::render_instance_uptr> instances;
        std::vector<bool> is_desktop_environment;

        wf::pointf_t get_offset()
        {
            auto g   = self->output->get_relative_geometry();
            auto cws = self->output->wset()->get_current_workspace();
            return wf::pointf_t{
                (double)(self->ws.x - cws.x) * g.width,
                (double)(self->ws.y - cws.y) * g.height,
            };
        }

      public:
        instance_t(base_stream_node_t *self, wf::scene::damage_callback push_damage)
        {
            this->self = self;
            auto translate_and_push_damage = [this, push_damage] (wf::regionf_t damage)
            {
                damage += -get_offset();
                damage &= this->self->get_bounding_box();
                if (!damage.empty())
                {
                    push_damage(damage);
                }
            };

            auto gen = [&] (const wf::scene::node_ptr& node)
            {
                auto view     = wf::node_to_view(node);
                const bool de = (view && (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT));
                node->gen_render_instances(instances,
                    de ? push_damage : translate_and_push_damage, self->output);
                is_desktop_environment.resize(instances.size(), de);
            };

            for (auto& output_node : wf::collect_output_nodes(wf::get_core().scene(), self->output))
            {
                for (auto& ch : output_node->get_children())
                {
                    if (!ch->is_enabled())
                    {
                        continue;
                    }

                    /* the workspace set node holds the windows: descend
                     * into it if one of its children is to be left out */
                    bool filter = false;
                    for (auto& gc : ch->get_children())
                    {
                        filter |= self->skip.count(gc.get()) > 0;
                    }

                    if (!filter)
                    {
                        gen(ch);
                        continue;
                    }

                    for (auto& gc : ch->get_children())
                    {
                        if (gc->is_enabled() && !self->skip.count(gc.get()))
                        {
                            gen(gc);
                        }
                    }
                }
            }
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto bbox = self->get_bounding_box();
            auto our_damage = damage & bbox;
            if (our_damage.empty())
            {
                return;
            }

            auto offset = get_offset();
            wf::render_target_t subtarget = target.translated(offset);
            our_damage += offset;
            for (size_t i = 0; i < instances.size(); i++)
            {
                if (is_desktop_environment[i])
                {
                    our_damage -= offset;
                    instances[i]->schedule_instructions(instructions, target, our_damage);
                    our_damage += offset;
                } else
                {
                    instances[i]->schedule_instructions(instructions, subtarget, our_damage);
                }
            }

            our_damage += -offset;
            damage     ^= bbox;
            instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = our_damage,
                });
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            static wf::option_wrapper_t<wf::color_t> background_color_opt{"core/background_color"};
            data.pass->clear(data.damage, background_color_opt);
        }

        void presentation_feedback(wf::output_t *output) override
        {
            for (auto& ch : instances)
            {
                ch->presentation_feedback(output);
            }
        }

        void compute_visibility(wf::output_t *output, wf::regionf_t& visible) override
        {
            wf::scene::compute_visibility_from_list(instances, output, visible, -get_offset());
        }
    };

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<instance_t>(this, push_damage));
    }
};

/* One window of a face, rendered into its own buffer */
struct window_layer_t
{
    std::weak_ptr<wf::scene::node_t> node;
    std::vector<wf::scene::render_instance_uptr> instances;
    wf::auxilliary_buffer_t buffer;
    /* the part of the window on this face, relative to the face (pixels);
     * empty = not drawn this frame */
    wf::geometry_t rect = {0, 0, 0, 0};
    /* the visible window (without shadow) on this face, relative to the
     * face (pixels); used for the thickness edges */
    wf::geometry_t slab = {0, 0, 0, 0};
    /* 1 = topmost window of the face, 1/n = lowest */
    float level = 1.0f;
};

using face_windows_t = std::vector<std::vector<std::unique_ptr<window_layer_t>>>;
}

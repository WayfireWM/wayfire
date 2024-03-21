#include "wayfire/bindings.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include "wayfire/seat.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/util.hpp"
#include "wayfire/view.hpp"
#include "plugins/ipc/ipc-helpers.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/plugins/common/shared-core-data.hpp"
#include <wayfire/signal-definitions.hpp>
#include <wayfire/config/option-types.hpp>
#include <wayfire/output.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

class wset_output_overlay_t : public wf::scene::node_t
{
    class render_instance_t : public wf::scene::simple_render_instance_t<wset_output_overlay_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::render_target_t& target, const wf::region_t& region)
        {
            OpenGL::render_begin(target);

            auto g = self->get_bounding_box();
            for (auto box : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));
                OpenGL::render_texture(self->cr_text.tex.tex, target, g, glm::vec4(1.0f),
                    OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
            }

            OpenGL::render_end();
        }
    };

    wf::cairo_text_t cr_text;

  public:
    wset_output_overlay_t() : node_t(false)
    {}

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        return wf::construct_box({10, 10}, cr_text.get_size());
    }

    void set_text(std::string text)
    {
        wf::cairo_text_t::params params;
        params.text_color   = wf::color_t{0.9, 0.9, 0.9, 1};
        params.bg_color     = wf::color_t{0.1, 0.1, 0.1, 0.9};
        params.font_size    = 32;
        params.rounded_rect = true;
        params.bg_rect  = true;
        params.max_size = wf::dimensions(get_bounding_box());

        cr_text.render_text(text, params);
        wf::scene::damage_node(this->shared_from_this(), get_bounding_box());
    }
};



class wayfire_wsets_plugin_t : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        method_repository->register_method("wsets/set-output-wset", set_output_wset);
        setup_bindings();
        wf::get_core().output_layout->connect(&on_new_output);
        for (auto& wo : wf::get_core().output_layout->get_outputs())
        {
            available_sets[wo->wset()->get_index()] = wo->wset();
        }
    }

    void fini() override
    {
        method_repository->unregister_method("wsets/set-output-wset");
        for (auto& binding : select_callback)
        {
            wf::get_core().bindings->rem_binding(&binding);
        }

        for (auto& binding : send_callback)
        {
            wf::get_core().bindings->rem_binding(&binding);
        }
    }

  private:
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> method_repository;
    wf::option_wrapper_t<wf::config::compound_list_t<wf::activatorbinding_t>>
    workspace_bindings{"wsets/wsets_bindings"};
    wf::option_wrapper_t<wf::config::compound_list_t<wf::activatorbinding_t>>
    send_to_bindings{"wsets/send_window_bindings"};
    wf::option_wrapper_t<wf::animation_description_t> label_duration{"wsets/label_duration"};

    std::list<wf::activator_callback> select_callback;
    std::list<wf::activator_callback> send_callback;
    std::map<int, std::shared_ptr<wf::workspace_set_t>> available_sets;

    wf::ipc::method_callback set_output_wset = [=] (nlohmann::json data)
    {
        WFJSON_EXPECT_FIELD(data, "output-id", number_integer);
        WFJSON_EXPECT_FIELD(data, "wset-index", number_integer);
        auto o = wf::ipc::find_output_by_id(data["output-id"]);
        if (!o)
        {
            return wf::ipc::json_error("output not found");
        }

        select_workspace(data["wset-index"], o);
        return wf::ipc::json_ok();
    };

    void setup_bindings()
    {
        for (const auto& [workspace, binding] : workspace_bindings.value())
        {
            int index = wf::option_type::from_string<int>(workspace.c_str()).value_or(-1);
            if (index < 0)
            {
                LOGE("[WSETS] Invalid workspace set ", index, " in configuration!");
                continue;
            }

            select_callback.push_back([=] (auto)
            {
                auto wo = wf::get_core().seat->get_active_output();
                if (!wo->can_activate_plugin(wf::CAPABILITY_MANAGE_COMPOSITOR))
                {
                    return false;
                }

                select_workspace(index);
                return true;
            });

            wf::get_core().bindings->add_activator(wf::create_option(binding), &select_callback.back());
        }

        for (const auto& [workspace, binding] : send_to_bindings.value())
        {
            int index = wf::option_type::from_string<int>(workspace.c_str()).value_or(-1);
            if (index < 0)
            {
                LOGE("[WSETS] Invalid workspace set ", index, " in configuration!");
                continue;
            }

            send_callback.push_back([=] (auto)
            {
                auto wo = wf::get_core().seat->get_active_output();
                if (!wo->can_activate_plugin(wf::CAPABILITY_MANAGE_COMPOSITOR))
                {
                    return false;
                }

                send_window_to(index);
                return true;
            });

            wf::get_core().bindings->add_activator(wf::create_option(binding), &send_callback.back());
        }
    }

    struct output_overlay_data_t : public wf::custom_data_t
    {
        std::shared_ptr<wset_output_overlay_t> node;
        wf::wl_timer<false> timer;
        ~output_overlay_data_t()
        {
            wf::scene::damage_node(node, node->get_bounding_box());
            wf::scene::remove_child(node);
            timer.disconnect();
        }
    };

    void cleanup_wsets()
    {
        auto it = available_sets.begin();
        while (it != available_sets.end())
        {
            auto wset = it->second;
            if (wset->get_views().empty() &&
                (!wset->get_attached_output() || (wset->get_attached_output()->wset() != wset)))
            {
                it = available_sets.erase(it);
            } else
            {
                ++it;
            }
        }
    }

    void show_workspace_set_overlay(wf::output_t *wo)
    {
        auto overlay = wo->get_data_safe<output_overlay_data_t>();
        if (!overlay->node)
        {
            overlay->node = std::make_shared<wset_output_overlay_t>();
        }

        overlay->node->set_text("Workspace set " + std::to_string(wo->wset()->get_index()));
        wf::scene::readd_front(wo->node_for_layer(wf::scene::layer::DWIDGET), overlay->node);
        wf::scene::damage_node(overlay->node, overlay->node->get_bounding_box());

        overlay->timer.set_timeout(label_duration.value().length_ms, [wo] ()
        {
            wo->erase_data<output_overlay_data_t>();
        });
    }

    /**
     * Find the workspace set with the given index, or create a new one if it does not exist already.
     * In addition, take a reference to it.
     */
    void locate_or_create_wset(uint64_t index)
    {
        if (available_sets.count(index))
        {
            return;
        }

        auto all_wsets = wf::workspace_set_t::get_all();
        auto it = std::find_if(all_wsets.begin(), all_wsets.end(),
            [&] (auto wset) { return wset->get_index() == index; });

        if (it == all_wsets.end())
        {
            available_sets[index] = wf::workspace_set_t::create(index);
        } else
        {
            available_sets[index] = (*it)->shared_from_this();
        }
    }

    void select_workspace(int index, wf::output_t *wo = wf::get_core().seat->get_active_output())
    {
        if (!wo)
        {
            return;
        }

        locate_or_create_wset(index);
        if (wo->wset() != available_sets[index])
        {
            LOGC(WSET, "Output ", wo->to_string(), " selecting workspace set id=", index);

            if (auto old_output = available_sets[index]->get_attached_output())
            {
                if (old_output->wset() == available_sets[index])
                {
                    // Create new empty wset for the output
                    old_output->set_workspace_set(wf::workspace_set_t::create());
                    available_sets[old_output->wset()->get_index()] = old_output->wset();
                    show_workspace_set_overlay(old_output);
                }
            }

            wo->set_workspace_set(available_sets[index]);
        }

        // We want to show the overlay even if we remain on the same workspace set
        show_workspace_set_overlay(wo);
        cleanup_wsets();
    }

    void send_window_to(int index)
    {
        auto wo = wf::get_core().seat->get_active_output();
        if (!wo)
        {
            return;
        }

        auto view = toplevel_cast(wf::get_active_view_for_output(wo));
        if (!view)
        {
            return;
        }

        locate_or_create_wset(index);

        auto target_wset     = available_sets[index];
        const auto& old_wset = view->get_wset();

        old_wset->remove_view(view);
        wf::scene::remove_child(view->get_root_node());
        wf::emit_view_pre_moved_to_wset_pre(view, old_wset, target_wset);

        if (view->get_output() != target_wset->get_attached_output())
        {
            view->set_output(target_wset->get_attached_output());
        }

        wf::scene::readd_front(target_wset->get_node(), view->get_root_node());
        target_wset->add_view(view);
        wf::emit_view_moved_to_wset(view, old_wset, target_wset);

        wf::get_core().seat->refocus();
    }

    wf::signal::connection_t<wf::output_added_signal> on_new_output = [=] (wf::output_added_signal *ev)
    {
        available_sets[ev->output->wset()->get_index()] = ev->output->wset();
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_wsets_plugin_t);

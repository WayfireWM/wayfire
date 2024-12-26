#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/core.hpp>
#include "animate.hpp"
#include "plugins/common/wayfire/plugins/common/shared-core-data.hpp"
#include "system_fade.hpp"
#include "basic_animations.hpp"
#include "squeezimize.hpp"
#include "zap.hpp"
#include "spin.hpp"
#include "fire/fire.hpp"
#include "unmapped-view-node.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view.hpp"
#include <wayfire/matcher.hpp>

/* Represents an animation running for a specific view
 * animation_t is which animation to use (i.e fire, zoom, etc). */
class animation_hook : public wf::custom_data_t
{
  public:
    std::shared_ptr<wf::view_interface_t> view;
    wf::animate::animation_type type;
    std::string name;
    wf::output_t *current_output = nullptr;
    std::unique_ptr<wf::animate::animation_base_t> animation;
    std::shared_ptr<wf::unmapped_view_snapshot_node> unmapped_contents;

    void damage_whole_view()
    {
        view->damage();
        if (unmapped_contents)
        {
            wf::scene::damage_node(unmapped_contents, unmapped_contents->get_bounding_box());
        }
    }

    /* Update animation right before each frame */
    wf::effect_hook_t update_animation_hook = [=] ()
    {
        damage_whole_view();
        bool result = animation->step();
        damage_whole_view();

        if (!result)
        {
            stop_hook(false);
        }
    };

    /**
     * Switch the output the view is being animated on, and update the lastly
     * animated output in the global list.
     */
    void set_output(wf::output_t *new_output)
    {
        if (current_output)
        {
            current_output->render->rem_effect(&update_animation_hook);
        }

        if (new_output)
        {
            new_output->render->add_effect(&update_animation_hook,
                wf::OUTPUT_EFFECT_PRE);
        }

        current_output = new_output;
    }

    wf::signal::connection_t<wf::view_set_output_signal> on_set_output = [=] (auto)
    {
        set_output(view->get_output());
    };

    animation_hook(wayfire_view view,
        std::unique_ptr<wf::animate::animation_base_t> _animation,
        wf::animation_description_t duration,
        wf::animate::animation_type type,
        std::string name)
    {
        this->type = type;
        this->view = view->shared_from_this();
        this->name = name;

        this->animation = std::move(_animation);
        this->animation->init(view, duration, type);

        set_output(view->get_output());
        /* Animation is driven by the output render cycle the view is on.
         * Thus, we need to keep in sync with the current output. */
        view->connect(&on_set_output);
        wf::scene::set_node_enabled(view->get_root_node(), true);

        if (type == wf::animate::ANIMATION_TYPE_UNMAP)
        {
            set_unmapped_contents();
        }
    }

    void stop_hook(bool detached)
    {
        view->erase_data(name);
    }

    // When showing the final unmap animation, we show a ``fake'' node instead of the actual view contents,
    // because the underlying (sub)surfaces might be destroyed.
    //
    // The unmapped contents have to be visible iff the view is in an unmap animation.
    void set_unmapped_contents()
    {
        if (!unmapped_contents)
        {
            unmapped_contents = std::make_shared<wf::unmapped_view_snapshot_node>(view);
            auto parent = dynamic_cast<wf::scene::floating_inner_node_t*>(
                view->get_surface_root_node()->parent());

            if (parent)
            {
                wf::scene::add_front(
                    std::dynamic_pointer_cast<wf::scene::floating_inner_node_t>(parent->shared_from_this()),
                    unmapped_contents);
            }
        }
    }

    void unset_unmapped_contents()
    {
        if (unmapped_contents)
        {
            wf::scene::remove_child(unmapped_contents);
            unmapped_contents.reset();
        }
    }

    void set_animation_type(wf::animate::animation_type new_type)
    {
        if (new_type == wf::animate::ANIMATION_TYPE_UNMAP)
        {
            set_unmapped_contents();
        } else
        {
            unset_unmapped_contents();
        }

        const bool direction_change =
            (type & WF_ANIMATE_HIDING_ANIMATION) != (new_type & WF_ANIMATE_HIDING_ANIMATION);
        this->type = new_type;
        if (animation && direction_change)
        {
            animation->reverse();
        }
    }

    ~animation_hook()
    {
        /**
         * Order here is very important.
         * After doing unref() the view will be potentially destroyed.
         * Hence, we want to deinitialize everything else before that.
         */
        set_output(nullptr);
        on_set_output.disconnect();
        this->animation.reset();

        unset_unmapped_contents();
        wf::scene::set_node_enabled(view->get_root_node(), false);
    }

    animation_hook(const animation_hook &) = delete;
    animation_hook(animation_hook &&) = delete;
    animation_hook& operator =(const animation_hook&) = delete;
    animation_hook& operator =(animation_hook&&) = delete;
};

class wayfire_animation : public wf::plugin_interface_t, private wf::per_output_tracker_mixin_t<>
{
    wf::option_wrapper_t<std::string> open_animation{"animate/open_animation"};
    wf::option_wrapper_t<std::string> close_animation{"animate/close_animation"};
    wf::option_wrapper_t<std::string> minimize_animation{"animate/minimize_animation"};

    wf::option_wrapper_t<wf::animation_description_t> default_duration{"animate/duration"};
    wf::option_wrapper_t<wf::animation_description_t> fade_duration{"animate/fade_duration"};
    wf::option_wrapper_t<wf::animation_description_t> zoom_duration{"animate/zoom_duration"};
    wf::option_wrapper_t<wf::animation_description_t> fire_duration{"animate/fire_duration"};
    wf::option_wrapper_t<wf::animation_description_t> squeezimize_duration{"animate/squeezimize_duration"};
    wf::option_wrapper_t<wf::animation_description_t> zap_duration{"animate/zap_duration"};
    wf::option_wrapper_t<wf::animation_description_t> spin_duration{"animate/spin_duration"};

    wf::option_wrapper_t<wf::animation_description_t> startup_duration{"animate/startup_duration"};

    wf::view_matcher_t animation_enabled_for{"animate/enabled_for"};
    wf::view_matcher_t fade_enabled_for{"animate/fade_enabled_for"};
    wf::view_matcher_t zoom_enabled_for{"animate/zoom_enabled_for"};
    wf::view_matcher_t fire_enabled_for{"animate/fire_enabled_for"};

    wf::shared_data::ref_ptr_t<wf::animate::animate_effects_registry_t> effects_registry;

    template<class animation_t>
    void register_effect(std::string name, wf::option_sptr_t<wf::animation_description_t> option)
    {
        effects_registry->register_effect(name, wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<animation_t>(); },
            .default_duration = [option] { return option->get_value(); },
        });
    }

  public:
    void init() override
    {
        init_output_tracking();

        register_effect<fade_animation>("fade", default_duration);
        register_effect<zoom_animation>("zoom", default_duration);
        register_effect<FireAnimation>("fire", default_duration);
        register_effect<wf::zap::zap_animation>("zap", zap_duration);
        register_effect<wf::spin::spin_animation>("spin", spin_duration);
        register_effect<wf::squeezimize::squeezimize_animation>("squeezimize", squeezimize_duration);
    }

    void handle_new_output(wf::output_t *output) override
    {
        output->connect(&on_view_mapped);
        output->connect(&on_view_pre_unmap);
        output->connect(&on_render_start);
        output->connect(&on_minimize_request);
    }

    void handle_output_removed(wf::output_t *output) override
    {
        cleanup_views_on_output(output);
    }

    void fini() override
    {
        cleanup_views_on_output(nullptr);

        effects_registry->unregister_effect("fade");
        effects_registry->unregister_effect("zoom");
        effects_registry->unregister_effect("fire");
        effects_registry->unregister_effect("zap");
        effects_registry->unregister_effect("spin");
        effects_registry->unregister_effect("squeezimize");
    }

    void cleanup_views_on_output(wf::output_t *output)
    {
        std::vector<std::shared_ptr<wf::view_interface_t>> all_views;
        for (auto& view : wf::get_core().get_all_views())
        {
            all_views.push_back(view->shared_from_this());
        }

        for (auto& view : all_views)
        {
            auto wo = view->get_output();
            if ((wo != output) && output)
            {
                continue;
            }

            for (auto& anim : effects_registry->effects)
            {
                auto map_name = get_map_animation_cdata_name(anim.first);
                if (view->has_data(map_name))
                {
                    view->get_data<animation_hook>(map_name)->stop_hook(false);
                }

                auto minimize_name = get_minimize_animation_cdata_name(anim.first);
                if (view->has_data(minimize_name))
                {
                    view->get_data<animation_hook>(minimize_name)->stop_hook(false);
                }
            }
        }
    }

    struct view_animation_t
    {
        std::string animation_name;
        wf::animation_description_t duration;
    };

    view_animation_t get_animation_for_view(
        wf::option_wrapper_t<std::string>& anim_type, wayfire_view view)
    {
        /* Determine the animation for the given view.
         * Note that the matcher plugin might not have been loaded, so
         * we need to have a fallback algorithm */
        if (fade_enabled_for.matches(view))
        {
            return {"fade", fade_duration};
        }

        if (zoom_enabled_for.matches(view))
        {
            return {"zoom", zoom_duration};
        }

        if (fire_enabled_for.matches(view))
        {
            return {"fire", fire_duration};
        }

        if (animation_enabled_for.matches(view))
        {
            if (!effects_registry->effects.count(anim_type))
            {
                if ((std::string)anim_type != "none")
                {
                    LOGE("Unknown animation type: \"", (std::string)anim_type, "\"");
                }

                return {"none", wf::animation_description_t{0, {}, ""}};
            }

            return {
                .animation_name = anim_type,
                .duration = effects_registry->effects[anim_type].default_duration().value_or(default_duration)
            };
        }

        return {"none", wf::animation_description_t{0, {}, ""}};
    }

    std::string get_map_animation_cdata_name(std::string animation_name)
    {
        return "animation-hook-" + animation_name;
    }

    std::string get_minimize_animation_cdata_name(std::string animation_name)
    {
        return "animation-hook-" + animation_name + "-minimize";
    }

    void set_animation(wayfire_view view, std::string animation_name,
        wf::animate::animation_type type, wf::animation_description_t duration)
    {
        if (animation_name == "none")
        {
            return;
        }

        auto cdata_name = (type & WF_ANIMATE_MINIMIZE_STATE_ANIMATION) ?
            get_minimize_animation_cdata_name(animation_name) :
            get_map_animation_cdata_name(animation_name);

        auto& effect = effects_registry->effects[animation_name];
        if (view->has_data(cdata_name))
        {
            view->get_data<animation_hook>(cdata_name)->set_animation_type(type);
            return;
        }

        auto hook = std::make_unique<animation_hook>(view, effect.generator(),
            duration, type, cdata_name);
        view->store_data(std::move(hook), cdata_name);
    }

    /* TODO: enhance - add more animations */
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        auto animation = get_animation_for_view(open_animation, ev->view);
        set_animation(ev->view, animation.animation_name,
            wf::animate::ANIMATION_TYPE_MAP, animation.duration);
    };

    wf::signal::connection_t<wf::view_pre_unmap_signal> on_view_pre_unmap =
        [=] (wf::view_pre_unmap_signal *ev)
    {
        auto animation = get_animation_for_view(close_animation, ev->view);
        set_animation(ev->view, animation.animation_name,
            wf::animate::ANIMATION_TYPE_UNMAP, animation.duration);
    };

    wf::signal::connection_t<wf::view_minimize_request_signal> on_minimize_request =
        [=] (wf::view_minimize_request_signal *ev)
    {
        auto animation = get_animation_for_view(minimize_animation, ev->view);
        set_animation(ev->view, minimize_animation,
            ev->state ? wf::animate::ANIMATION_TYPE_MINIMIZE : wf::animate::ANIMATION_TYPE_RESTORE,
            animation.duration);
        // ev->carried_out should remain false, so that core also does the automatic minimize/restore and
        // refocus
    };

    wf::signal::connection_t<wf::output_start_rendering_signal> on_render_start =
        [=] (wf::output_start_rendering_signal *ev)
    {
        new wf_system_fade(ev->output, startup_duration);
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_animation);

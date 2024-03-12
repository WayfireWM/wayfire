#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <type_traits>
#include <wayfire/core.hpp>
#include "animate.hpp"
#include "system_fade.hpp"
#include "basic_animations.hpp"
#include "fire/fire.hpp"
#include "unmapped-view-node.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/view.hpp"
#include <wayfire/matcher.hpp>

void animation_base::init(wayfire_view, wf::animation_description_t, wf_animation_type)
{}
bool animation_base::step()
{
    return false;
}

void animation_base::reverse()
{}

int animation_base::get_direction()
{
    return 1;
}

animation_base::~animation_base()
{}

static constexpr const char *animate_custom_data_fire     = "animation-hook-fire";
static constexpr const char *animate_custom_data_zoom     = "animation-hook-zoom";
static constexpr const char *animate_custom_data_fade     = "animation-hook-fade";
static constexpr const char *animate_custom_data_minimize =
    "animation-hook-minimize";

static constexpr int HIDDEN = 0;
static constexpr int SHOWN  = 1;

/* Represents an animation running for a specific view
 * animation_t is which animation to use (i.e fire, zoom, etc). */
struct animation_hook_base : public wf::custom_data_t
{
    virtual void stop_hook(bool) = 0;
    virtual void reverse(wf_animation_type) = 0;
    virtual int get_direction() = 0;

    animation_hook_base() = default;
    virtual ~animation_hook_base() = default;
    animation_hook_base(const animation_hook_base &) = default;
    animation_hook_base(animation_hook_base &&) = default;
    animation_hook_base& operator =(const animation_hook_base&) = default;
    animation_hook_base& operator =(animation_hook_base&&) = default;
};

template<class animation_t>
struct animation_hook : public animation_hook_base
{
    static_assert(std::is_base_of<animation_base, animation_t>::value,
        "animation_type must be derived from animation_base!");

    std::shared_ptr<wf::view_interface_t> view;
    wf_animation_type type;
    std::string name;
    wf::output_t *current_output = nullptr;
    std::unique_ptr<animation_base> animation;
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

    animation_hook(wayfire_view view, wf::animation_description_t duration, wf_animation_type type,
        std::string name)
    {
        this->type = type;
        this->view = view->shared_from_this();
        this->name = name;

        animation = std::make_unique<animation_t>();
        animation->init(view, duration, type);

        set_output(view->get_output());
        /* Animation is driven by the output render cycle the view is on.
         * Thus, we need to keep in sync with the current output. */
        view->connect(&on_set_output);
        wf::scene::set_node_enabled(view->get_root_node(), true);

        if (type == ANIMATION_TYPE_UNMAP)
        {
            set_unmapped_contents();
        }
    }

    void stop_hook(bool detached) override
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

    void reverse(wf_animation_type type) override
    {
        if (type == ANIMATION_TYPE_UNMAP)
        {
            set_unmapped_contents();
        } else
        {
            unset_unmapped_contents();
        }

        this->type = type;
        if (animation)
        {
            animation->reverse();
        }
    }

    int get_direction() override
    {
        return animation->get_direction();
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

static void cleanup_views_on_output(wf::output_t *output)
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

        if (view->has_data(animate_custom_data_fire))
        {
            view->get_data<animation_hook_base>(
                animate_custom_data_fire)->stop_hook(true);
        }

        if (view->has_data(animate_custom_data_zoom))
        {
            view->get_data<animation_hook_base>(
                animate_custom_data_zoom)->stop_hook(true);
        }

        if (view->has_data(animate_custom_data_fade))
        {
            view->get_data<animation_hook_base>(
                animate_custom_data_fade)->stop_hook(true);
        }

        if (view->has_data(animate_custom_data_minimize))
        {
            view->get_data<animation_hook_base>(
                animate_custom_data_minimize)->stop_hook(true);
        }
    }
}

class wayfire_animation : public wf::plugin_interface_t, private wf::per_output_tracker_mixin_t<>
{
    wf::option_wrapper_t<std::string> open_animation{"animate/open_animation"};
    wf::option_wrapper_t<std::string> close_animation{"animate/close_animation"};

    wf::option_wrapper_t<wf::animation_description_t> default_duration{"animate/duration"};
    wf::option_wrapper_t<wf::animation_description_t> fade_duration{"animate/fade_duration"};
    wf::option_wrapper_t<wf::animation_description_t> zoom_duration{"animate/zoom_duration"};
    wf::option_wrapper_t<wf::animation_description_t> fire_duration{"animate/fire_duration"};

    wf::option_wrapper_t<wf::animation_description_t> startup_duration{"animate/startup_duration"};

    wf::view_matcher_t animation_enabled_for{"animate/enabled_for"};
    wf::view_matcher_t fade_enabled_for{"animate/fade_enabled_for"};
    wf::view_matcher_t zoom_enabled_for{"animate/zoom_enabled_for"};
    wf::view_matcher_t fire_enabled_for{"animate/fire_enabled_for"};

  public:
    void init() override
    {
        init_output_tracking();
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
            return {anim_type, default_duration};
        }

        return {"none", wf::animation_description_t{0, {}, ""}};
    }

    bool try_reverse(wayfire_view view, wf_animation_type type, std::string name,
        int visibility)
    {
        visibility = !visibility;
        if (view->has_data(name))
        {
            auto data = view->get_data<animation_hook_base>(name);
            if (data->get_direction() == visibility)
            {
                data->reverse(type);
                return true;
            }
        }

        return false;
    }

    template<class animation_t>
    void set_animation(wayfire_view view,
        wf_animation_type type, wf::animation_description_t duration, std::string name)
    {
        name = "animation-hook-" + name;

        if (type == ANIMATION_TYPE_MAP)
        {
            if (try_reverse(view, type, name, SHOWN))
            {
                return;
            }

            auto animation = get_animation_for_view(open_animation, view);
            view->store_data(
                std::make_unique<animation_hook<animation_t>>(view, duration, type,
                    name), name);
        } else if (type == ANIMATION_TYPE_UNMAP)
        {
            if (try_reverse(view, type, name, HIDDEN))
            {
                return;
            }

            auto animation = get_animation_for_view(close_animation, view);
            view->store_data(
                std::make_unique<animation_hook<animation_t>>(view, duration, type,
                    name), name);
        } else if (type & MINIMIZE_STATE_ANIMATION)
        {
            if (view->has_data(animate_custom_data_minimize))
            {
                view->get_data<animation_hook_base>(
                    animate_custom_data_minimize)->reverse(type);
                return;
            }

            view->store_data(
                std::make_unique<animation_hook<animation_t>>(view, duration, type,
                    animate_custom_data_minimize),
                animate_custom_data_minimize);
        }
    }

    /* TODO: enhance - add more animations */
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        auto animation = get_animation_for_view(open_animation, ev->view);

        if (animation.animation_name == "fade")
        {
            set_animation<fade_animation>(ev->view, ANIMATION_TYPE_MAP,
                animation.duration, animation.animation_name);
        } else if (animation.animation_name == "zoom")
        {
            set_animation<zoom_animation>(ev->view, ANIMATION_TYPE_MAP,
                animation.duration, animation.animation_name);
        } else if (animation.animation_name == "fire")
        {
            set_animation<FireAnimation>(ev->view, ANIMATION_TYPE_MAP,
                animation.duration, animation.animation_name);
        }
    };

    wf::signal::connection_t<wf::view_pre_unmap_signal> on_view_pre_unmap =
        [=] (wf::view_pre_unmap_signal *ev)
    {
        auto animation = get_animation_for_view(close_animation, ev->view);

        if (animation.animation_name == "fade")
        {
            set_animation<fade_animation>(ev->view, ANIMATION_TYPE_UNMAP,
                animation.duration, animation.animation_name);
        } else if (animation.animation_name == "zoom")
        {
            set_animation<zoom_animation>(ev->view, ANIMATION_TYPE_UNMAP,
                animation.duration, animation.animation_name);
        } else if (animation.animation_name == "fire")
        {
            set_animation<FireAnimation>(ev->view, ANIMATION_TYPE_UNMAP,
                animation.duration, animation.animation_name);
        }
    };

    wf::signal::connection_t<wf::view_minimize_request_signal> on_minimize_request =
        [=] (wf::view_minimize_request_signal *ev)
    {
        if (ev->state)
        {
            set_animation<zoom_animation>(ev->view, ANIMATION_TYPE_MINIMIZE, default_duration, "minimize");
        } else
        {
            set_animation<zoom_animation>(ev->view, ANIMATION_TYPE_RESTORE, default_duration, "minimize");
        }

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

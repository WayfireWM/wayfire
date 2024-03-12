#include "wayfire/object.hpp"
#include "wayfire/plugins/common/input-grab.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view-helpers.hpp"
#include <wayfire/window-manager.hpp>
#include <memory>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/core.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/seat.hpp>

#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>

#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>

#include <wayfire/util/duration.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <exception>
#include <set>

constexpr const char *switcher_transformer = "switcher-3d";
constexpr const char *switcher_transformer_background = "switcher-3d";
constexpr float background_dim_factor = 0.6;

using namespace wf::animation;
class SwitcherPaintAttribs
{
  public:
    SwitcherPaintAttribs(const duration_t& duration) :
        scale_x(duration, 1, 1), scale_y(duration, 1, 1),
        off_x(duration, 0, 0), off_y(duration, 0, 0), off_z(duration, 0, 0),
        rotation(duration, 0, 0), alpha(duration, 1, 1)
    {}

    timed_transition_t scale_x, scale_y;
    timed_transition_t off_x, off_y, off_z;
    timed_transition_t rotation, alpha;
};

enum SwitcherViewPosition
{
    SWITCHER_POSITION_LEFT   = 0,
    SWITCHER_POSITION_CENTER = 1,
    SWITCHER_POSITION_RIGHT  = 2,
};

static constexpr bool view_expired(int view_position)
{
    return view_position < SWITCHER_POSITION_LEFT ||
           view_position > SWITCHER_POSITION_RIGHT;
}

struct SwitcherView
{
    wayfire_toplevel_view view;
    SwitcherPaintAttribs attribs;

    int position;
    SwitcherView(duration_t& duration) : attribs(duration)
    {}

    /* Make animation start values the current progress of duration */
    void refresh_start()
    {
        for_each([] (timed_transition_t& t) { t.restart_same_end(); });
    }

    void to_end()
    {
        for_each([] (timed_transition_t& t) { t.set(t.end, t.end); });
    }

  private:
    void for_each(std::function<void(timed_transition_t& t)> call)
    {
        call(attribs.off_x);
        call(attribs.off_y);
        call(attribs.off_z);

        call(attribs.scale_x);
        call(attribs.scale_y);

        call(attribs.alpha);
        call(attribs.rotation);
    }
};

class WayfireSwitcher : public wf::per_output_plugin_instance_t, public wf::keyboard_interaction_t
{
    wf::option_wrapper_t<double> view_thumbnail_scale{
        "switcher/view_thumbnail_scale"};
    wf::option_wrapper_t<wf::animation_description_t> speed{"switcher/speed"};
    wf::option_wrapper_t<int> view_thumbnail_rotation{
        "switcher/view_thumbnail_rotation"};

    duration_t duration{speed};
    duration_t background_dim_duration{speed};
    timed_transition_t background_dim{background_dim_duration};

    std::unique_ptr<wf::input_grab_t> input_grab;

    /* If a view comes before another in this list, it is on top of it */
    std::vector<SwitcherView> views;

    // the modifiers which were used to activate switcher
    uint32_t activating_modifiers = 0;
    bool active = false;

    class switcher_render_node_t : public wf::scene::node_t
    {
        class switcher_render_instance_t : public wf::scene::render_instance_t
        {
            switcher_render_node_t *self;
            wf::scene::damage_callback push_damage;
            wf::signal::connection_t<wf::scene::node_damage_signal> on_switcher_damage =
                [=] (wf::scene::node_damage_signal *ev)
            {
                push_damage(ev->region);
            };

          public:
            switcher_render_instance_t(switcher_render_node_t *self, wf::scene::damage_callback push_damage)
            {
                this->self = self;
                this->push_damage = push_damage;
                self->connect(&on_switcher_damage);
            }

            void schedule_instructions(
                std::vector<wf::scene::render_instruction_t>& instructions,
                const wf::render_target_t& target, wf::region_t& damage) override
            {
                instructions.push_back(wf::scene::render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });

                // Don't render anything below
                auto bbox = self->get_bounding_box();
                damage ^= bbox;
            }

            void render(const wf::render_target_t& target,
                const wf::region_t& region, const std::any& tag) override
            {
                self->switcher->render(target.translated(-wf::origin(self->get_bounding_box())));
            }
        };

      public:
        switcher_render_node_t(WayfireSwitcher *switcher) : node_t(false)
        {
            this->switcher = switcher;
        }

        virtual void gen_render_instances(
            std::vector<wf::scene::render_instance_uptr>& instances,
            wf::scene::damage_callback push_damage, wf::output_t *shown_on)
        {
            if (shown_on != this->switcher->output)
            {
                return;
            }

            instances.push_back(std::make_unique<switcher_render_instance_t>(this, push_damage));
        }

        wf::geometry_t get_bounding_box()
        {
            return switcher->output->get_layout_geometry();
        }

      private:
        WayfireSwitcher *switcher;
    };

    std::shared_ptr<switcher_render_node_t> render_node;
    wf::plugin_activation_data_t grab_interface = {
        .name = "switcher",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        output->add_key(
            wf::option_wrapper_t<wf::keybinding_t>{"switcher/next_view"},
            &next_view_binding);
        output->add_key(
            wf::option_wrapper_t<wf::keybinding_t>{"switcher/prev_view"},
            &prev_view_binding);
        output->connect(&view_disappeared);

        input_grab = std::make_unique<wf::input_grab_t>("switcher", output, this, nullptr, nullptr);
        grab_interface.cancel = [=] () {deinit_switcher();};
    }

    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event event) override
    {
        auto mod = wf::get_core().seat->modifier_from_keycode(event.keycode);
        if ((event.state == WLR_KEY_RELEASED) && (mod & activating_modifiers))
        {
            handle_done();
        }
    }

    wf::key_callback next_view_binding = [=] (auto)
    {
        handle_switch_request(-1);
        return false;
    };

    wf::key_callback prev_view_binding = [=] (auto)
    {
        handle_switch_request(1);
        return false;
    };

    wf::effect_hook_t pre_hook = [=] ()
    {
        dim_background(background_dim);
        wf::scene::damage_node(render_node, render_node->get_bounding_box());

        if (!duration.running())
        {
            cleanup_expired();
            if (!active)
            {
                deinit_switcher();
            }
        }
    };

    wf::signal::connection_t<wf::view_disappeared_signal> view_disappeared =
        [=] (wf::view_disappeared_signal *ev)
    {
        if (auto toplevel = toplevel_cast(ev->view))
        {
            handle_view_removed(toplevel);
        }
    };

    void handle_view_removed(wayfire_toplevel_view view)
    {
        // not running at all, don't care
        if (!output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        bool need_action = false;
        for (auto& sv : views)
        {
            need_action |= (sv.view == view);
        }

        // don't do anything if we're not using this view
        if (!need_action)
        {
            return;
        }

        if (active)
        {
            arrange();
        } else
        {
            cleanup_views([=] (SwitcherView& sv)
            { return sv.view == view; });
        }
    }

    bool handle_switch_request(int dir)
    {
        if (get_workspace_views().empty())
        {
            return false;
        }

        /* If we haven't grabbed, then we haven't setup anything */
        if (!output->is_plugin_active(grab_interface.name))
        {
            if (!init_switcher())
            {
                return false;
            }
        }

        /* Maybe we're still animating the exit animation from a previous
         * switcher activation? */
        if (!active)
        {
            active = true;
            input_grab->grab_input(wf::scene::layer::OVERLAY);

            focus_next(dir);
            arrange();
            activating_modifiers = wf::get_core().seat->get_keyboard_modifiers();
        } else
        {
            next_view(dir);
        }

        return true;
    }

    /* When switcher is done and starts animating towards end */
    void handle_done()
    {
        cleanup_expired();
        dearrange();
        input_grab->ungrab_input();
    }

    /* Sets up basic hooks needed while switcher works and/or displays animations.
     * Also lower any fullscreen views that are active */
    bool init_switcher()
    {
        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);

        render_node = std::make_shared<switcher_render_node_t>(this);
        wf::scene::add_front(wf::get_core().scene(), render_node);
        return true;
    }

    /* The reverse of init_switcher */
    void deinit_switcher()
    {
        output->deactivate_plugin(&grab_interface);

        output->render->rem_effect(&pre_hook);
        wf::scene::remove_child(render_node);
        render_node = nullptr;

        for (auto& view : output->wset()->get_views())
        {
            if (view->has_data("switcher-minimized-showed"))
            {
                view->erase_data("switcher-minimized-showed");
                wf::scene::set_node_enabled(view->get_root_node(), false);
            }

            view->get_transformed_node()->rem_transformer(switcher_transformer);
            view->get_transformed_node()->rem_transformer(
                switcher_transformer_background);
        }

        views.clear();

        wf::scene::update(wf::get_core().scene(),
            wf::scene::update_flag::INPUT_STATE);
    }

    /* offset from the left or from the right */
    float get_center_offset()
    {
        return output->get_relative_geometry().width / 3.0;
    }

    /* get the scale for non-focused views */
    float get_back_scale()
    {
        return 0.66;
    }

    /* offset in Z-direction for non-focused views */
    float get_z_offset()
    {
        return -1.0;
    }

    /* Move view animation target to the left
     * @param dir -1 for left, 1 for right */
    void move(SwitcherView& sv, int dir)
    {
        sv.attribs.off_x.restart_with_end(
            sv.attribs.off_x.end + get_center_offset() * dir);
        sv.attribs.off_y.restart_same_end();

        float z_sign = 0;
        if (sv.position == SWITCHER_POSITION_CENTER)
        {
            // Move from center to either left or right, so backwards
            z_sign = 1;
        } else if (view_expired(sv.position + dir))
        {
            // Expires, don't move
            z_sign = 0;
        } else
        {
            // Not from center, doesn't expire -> comes to the center
            z_sign = -1;
        }

        sv.attribs.off_z.restart_with_end(
            sv.attribs.off_z.end + get_z_offset() * z_sign);

        /* scale views that aren't in the center */
        sv.attribs.scale_x.restart_with_end(
            sv.attribs.scale_x.end * std::pow(get_back_scale(), z_sign));

        sv.attribs.scale_y.restart_with_end(
            sv.attribs.scale_y.end * std::pow(get_back_scale(), z_sign));

        float radians_rotation = -((float)view_thumbnail_rotation * (M_PI / 180.0));
        sv.attribs.rotation.restart_with_end(
            sv.attribs.rotation.end + radians_rotation * dir);

        sv.position += dir;
        sv.attribs.alpha.restart_with_end(
            view_expired(sv.position) ? 0.3 : 1.0);
    }

    /* Calculate how much a view should be scaled to fit into the slots */
    float calculate_scaling_factor(const wf::geometry_t& bbox) const
    {
        /* Each view should not be more than this percentage of the
         * width/height of the output */
        constexpr float screen_percentage = 0.45;

        auto og = output->get_relative_geometry();

        float max_width  = og.width * screen_percentage;
        float max_height = og.height * screen_percentage;

        float needed_exact = std::min(max_width / bbox.width,
            max_height / bbox.height);

        // don't scale down if the view is already small enough
        return std::min(needed_exact, 1.0f) * view_thumbnail_scale;
    }

    /* Calculate alpha for the view when switcher is inactive. */
    float get_view_normal_alpha(wayfire_toplevel_view view)
    {
        /* Usually views are visible, but if they were minimized,
         * and we aren't restoring the view, it has target alpha 0.0 */
        if (view->minimized && (views.empty() || (view != views[0].view)))
        {
            return 0.0;
        }

        return 1.0;
    }

    /* Move untransformed view to the center */
    void arrange_center_view(SwitcherView& sv)
    {
        auto og   = output->get_relative_geometry();
        auto bbox = wf::view_bounding_box_up_to(sv.view, switcher_transformer);

        float dx = (og.width / 2.0 - bbox.width / 2.0) - bbox.x;
        float dy = bbox.y - (og.height / 2.0 - bbox.height / 2.0);

        sv.attribs.off_x.set(0, dx);
        sv.attribs.off_y.set(0, dy);

        float scale = calculate_scaling_factor(bbox);
        sv.attribs.scale_x.set(1, scale);
        sv.attribs.scale_y.set(1, scale);
        sv.attribs.alpha.set(get_view_normal_alpha(sv.view), 1.0);
    }

    /* Position the view, starting from untransformed position */
    void arrange_view(SwitcherView& sv, int position)
    {
        arrange_center_view(sv);

        if (position == SWITCHER_POSITION_CENTER)
        {
            /* view already centered */
        } else
        {
            move(sv, position - SWITCHER_POSITION_CENTER);
        }
    }

    // returns a list of mapped views
    std::vector<wayfire_toplevel_view> get_workspace_views() const
    {
        return output->wset()->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_CURRENT_WORKSPACE);
    }

    /* Change the current focus to the next or the previous view */
    void focus_next(int dir)
    {
        auto ws_views = get_workspace_views();
        /* Change the focused view and rearrange views so that focused is on top */
        int size = ws_views.size();

        // calculate focus index & focus it
        int focused_view_index = (size + dir) % size;
        auto focused_view = ws_views[focused_view_index];
        wf::view_bring_to_front(focused_view);
    }

    /* Create the initial arrangement on the screen
     * Also changes the focus to the next or the last view, depending on dir */
    void arrange()
    {
        // clear views in case that deinit() hasn't been run
        views.clear();

        duration.start();
        background_dim.set(1, background_dim_factor);
        background_dim_duration.start();

        auto ws_views = get_workspace_views();
        for (auto v : ws_views)
        {
            views.push_back(create_switcher_view(v));
        }

        std::sort(views.begin(), views.end(), [] (SwitcherView& a, SwitcherView& b)
        {
            return wf::get_focus_timestamp(a.view) > wf::get_focus_timestamp(b.view);
        });

        /* Add a copy of the unfocused view if we have just 2 */
        if (ws_views.size() == 2)
        {
            views.push_back(create_switcher_view(ws_views.back()));
        }

        arrange_view(views[0], SWITCHER_POSITION_CENTER);

        /* If we have just 1 view, don't do anything else */
        if (ws_views.size() > 1)
        {
            arrange_view(views.back(), SWITCHER_POSITION_LEFT);
        }

        for (int i = 1; i < (int)views.size() - 1; i++)
        {
            arrange_view(views[i], SWITCHER_POSITION_RIGHT);
        }

        // We want the next view to be focused right off the bat
        // But we want it to be animated.
        handle_switch_request(-1);
    }

    void dearrange()
    {
        /* When we have just 2 views on the workspace, we have 2 copies
         * of the unfocused view. When dearranging those copies, they overlap.
         * If the view is translucent, this means that the view gets darker than
         * it really is.  * To circumvent this, we just fade out one of the copies */
        wayfire_toplevel_view fading_view = nullptr;
        if (count_different_active_views() == 2)
        {
            fading_view = get_unfocused_view();
        }

        for (auto& sv : views)
        {
            sv.attribs.off_x.restart_with_end(0);
            sv.attribs.off_y.restart_with_end(0);
            sv.attribs.off_z.restart_with_end(0);

            sv.attribs.scale_x.restart_with_end(1.0);
            sv.attribs.scale_y.restart_with_end(1.0);

            sv.attribs.rotation.restart_with_end(0);
            sv.attribs.alpha.restart_with_end(get_view_normal_alpha(sv.view));

            if (sv.view == fading_view)
            {
                sv.attribs.alpha.end = 0.0;
                // make sure we don't fade out the other unfocused view instance as
                // well
                fading_view = nullptr;
            }
        }

        background_dim.restart_with_end(1);
        background_dim_duration.start();
        duration.start();
        active = false;

        /* Potentially restore view[0] if it was maximized */
        if (views.size())
        {
            wf::get_core().default_wm->focus_raise_view(views[0].view);
        }
    }

    std::vector<wayfire_view> get_background_views() const
    {
        return wf::collect_views_from_output(output,
            {wf::scene::layer::BACKGROUND, wf::scene::layer::BOTTOM});
    }

    std::vector<wayfire_view> get_overlay_views() const
    {
        return wf::collect_views_from_output(output,
            {wf::scene::layer::TOP, wf::scene::layer::OVERLAY, wf::scene::layer::DWIDGET});
    }

    void dim_background(float dim)
    {
        for (auto view : get_background_views())
        {
            if (dim == 1.0)
            {
                view->get_transformed_node()->rem_transformer(
                    switcher_transformer_background);
            } else
            {
                auto tr =
                    wf::ensure_named_transformer<wf::scene::view_3d_transformer_t>(
                        view, wf::TRANSFORMER_3D, switcher_transformer_background,
                        view);
                tr->color[0] = tr->color[1] = tr->color[2] = dim;
            }
        }
    }

    SwitcherView create_switcher_view(wayfire_toplevel_view view)
    {
        /* we add a view transform if there isn't any.
         *
         * Note that a view might be visible on more than 1 place, so damage
         * tracking doesn't work reliably. To circumvent this, we simply damage
         * the whole output */
        if (!view->get_transformed_node()->get_transformer(switcher_transformer))
        {
            if (view->minimized)
            {
                wf::scene::set_node_enabled(view->get_root_node(), true);
                view->store_data(std::make_unique<wf::custom_data_t>(),
                    "switcher-minimized-showed");
            }

            view->get_transformed_node()->add_transformer(
                std::make_shared<wf::scene::view_3d_transformer_t>(view),
                wf::TRANSFORMER_3D, switcher_transformer);
        }

        SwitcherView sw{duration};
        sw.view     = view;
        sw.position = SWITCHER_POSITION_CENTER;

        return sw;
    }

    void render_view_scene(wayfire_view view, const wf::render_target_t& buffer)
    {
        std::vector<wf::scene::render_instance_uptr> instances;
        view->get_transformed_node()->gen_render_instances(instances, [] (auto) {});

        wf::scene::render_pass_params_t params;
        params.instances = &instances;
        params.damage    = view->get_transformed_node()->get_bounding_box();
        params.reference_output = this->output;
        params.target = buffer;
        wf::scene::run_render_pass(params, 0);
    }

    void render_view(const SwitcherView& sv, const wf::render_target_t& buffer)
    {
        auto transform = sv.view->get_transformed_node()
            ->get_transformer<wf::scene::view_3d_transformer_t>(switcher_transformer);
        assert(transform);

        transform->translation = glm::translate(glm::mat4(1.0),
        {(double)sv.attribs.off_x, (double)sv.attribs.off_y,
            (double)sv.attribs.off_z});

        transform->scaling = glm::scale(glm::mat4(1.0),
            {(double)sv.attribs.scale_x, (double)sv.attribs.scale_y, 1.0});

        transform->rotation = glm::rotate(glm::mat4(1.0),
            (float)sv.attribs.rotation, {0.0, 1.0, 0.0});

        transform->color[3] = sv.attribs.alpha;
        render_view_scene(sv.view, buffer);
    }

    void render(const wf::render_target_t& fb)
    {
        OpenGL::render_begin(fb);
        OpenGL::clear({0, 0, 0, 1});
        OpenGL::render_end();

        for (auto view : get_background_views())
        {
            render_view_scene(view, fb);
        }

        /* Render in the reverse order because we don't use depth testing */
        for (auto& view : wf::reverse(views))
        {
            render_view(view, fb);
        }

        for (auto view : get_overlay_views())
        {
            render_view_scene(view, fb);
        }
    }

    /* delete all views matching the given criteria, skipping the first "start" views
     * */
    void cleanup_views(std::function<bool(SwitcherView&)> criteria)
    {
        auto it = views.begin();
        while (it != views.end())
        {
            if (criteria(*it))
            {
                it = views.erase(it);
            } else
            {
                ++it;
            }
        }
    }

    /* Removes all expired views from the list */
    void cleanup_expired()
    {
        cleanup_views([=] (SwitcherView& sv)
        { return view_expired(sv.position); });
    }

    /* sort views according to their Z-order */
    void rebuild_view_list()
    {
        std::stable_sort(views.begin(), views.end(),
            [] (const SwitcherView& a, const SwitcherView& b)
        {
            enum category
            {
                FOCUSED   = 0,
                UNFOCUSED = 1,
                EXPIRED   = 2,
            };

            auto view_category = [] (const SwitcherView& sv)
            {
                if (sv.position == SWITCHER_POSITION_CENTER)
                {
                    return FOCUSED;
                }

                if (view_expired(sv.position))
                {
                    return EXPIRED;
                }

                return UNFOCUSED;
            };

            category aCat = view_category(a), bCat = view_category(b);
            if (aCat == bCat)
            {
                return a.position < b.position;
            } else
            {
                return aCat < bCat;
            }
        });
    }

    void next_view(int dir)
    {
        cleanup_expired();

        if (count_different_active_views() <= 1)
        {
            return;
        }

        /* Count of views in the left/right slots */
        int count_right = 0;
        int count_left  = 0;

        /* Move the topmost view from the center and the left/right group,
         * depending on the direction*/
        int to_move = (1 << SWITCHER_POSITION_CENTER) | (1 << (1 - dir));
        for (auto& sv : views)
        {
            if (!view_expired(sv.position) && ((1 << sv.position) & to_move))
            {
                to_move ^= (1 << sv.position); // only the topmost one
                move(sv, dir);
            } else if (!view_expired(sv.position))
            {
                /* Make sure animations start from where we are now */
                sv.refresh_start();
            }

            count_left  += (sv.position == SWITCHER_POSITION_LEFT);
            count_right += (sv.position == SWITCHER_POSITION_RIGHT);
        }

        /* Create a new view on the missing slot, but if both are missing,
         * show just the centered view */
        if (bool(count_left) ^ bool(count_right))
        {
            const int empty_slot = 1 - dir;
            fill_empty_slot(empty_slot);
        }

        rebuild_view_list();
        wf::view_bring_to_front(views.front().view);
        duration.start();
    }

    int count_different_active_views()
    {
        std::set<wayfire_toplevel_view> active_views;
        for (auto& sv : views)
        {
            active_views.insert(sv.view);
        }

        return active_views.size();
    }

/* Move the last view in the given slot so that it becomes invalid */
    wayfire_toplevel_view invalidate_last_in_slot(int slot)
    {
        for (int i = views.size() - 1; i >= 0; i--)
        {
            if (views[i].position == slot)
            {
                move(views[i], slot - 1);

                return views[i].view;
            }
        }

        return nullptr;
    }

/* Returns the non-focused view in the case where there is only 1 view */
    wayfire_toplevel_view get_unfocused_view()
    {
        for (auto& sv : views)
        {
            if (!view_expired(sv.position) &&
                (sv.position != SWITCHER_POSITION_CENTER))
            {
                return sv.view;
            }
        }

        return nullptr;
    }

    void fill_empty_slot(const int empty_slot)
    {
        const int full_slot = 2 - empty_slot;

        /* We have an empty slot. We invalidate the bottom-most view in the
         * opposite slot, and create a new view with the same content to
         * fill in the empty slot */
        auto view_to_create = invalidate_last_in_slot(full_slot);

        /* special case: we have just 2 views
         * in this case, the "new" view should not be the same as the
         * invalidated view(because this view is focused now), but the
         * one which isn't focused */
        if (count_different_active_views() == 2)
        {
            view_to_create = get_unfocused_view();
        }

        assert(view_to_create);

        auto sv = create_switcher_view(view_to_create);
        arrange_view(sv, empty_slot);

        /* directly show it on the target position */
        sv.to_end();
        sv.attribs.alpha.set(0, 1);
        views.push_back(std::move(sv));
    }

    void fini() override
    {
        if (output->is_plugin_active(grab_interface.name))
        {
            deinit_switcher();
        }

        output->rem_binding(&next_view_binding);
        output->rem_binding(&prev_view_binding);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<WayfireSwitcher>);

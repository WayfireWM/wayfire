#include <wayfire/plugin.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/core.hpp>

#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>

#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>

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
    wayfire_view view;
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

class WayfireSwitcher : public wf::plugin_interface_t
{
    wf::option_wrapper_t<double> view_thumbnail_scale{
        "switcher/view_thumbnail_scale"};
    wf::option_wrapper_t<int> speed{"switcher/speed"};

    duration_t duration{speed};
    duration_t background_dim_duration{speed};
    timed_transition_t background_dim{background_dim_duration};

    /* If a view comes before another in this list, it is on top of it */
    std::vector<SwitcherView> views;

    // the modifiers which were used to activate switcher
    uint32_t activating_modifiers = 0;
    bool active = false;

  public:

    void init() override
    {
        grab_interface->name = "switcher";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        output->add_key(
            wf::option_wrapper_t<wf::keybinding_t>{"switcher/next_view"},
            &next_view_binding);
        output->add_key(
            wf::option_wrapper_t<wf::keybinding_t>{"switcher/prev_view"},
            &prev_view_binding);
        output->connect_signal("view-detached", &view_removed);

        grab_interface->callbacks.keyboard.mod = [=] (uint32_t mod, uint32_t state)
        {
            if ((state == WLR_KEY_RELEASED) && (mod & activating_modifiers))
            {
                handle_done();
            }
        };

        grab_interface->callbacks.cancel = [=] () {deinit_switcher();};
    }

    wf::key_callback next_view_binding = [=] (auto)
    {
        return handle_switch_request(-1);
    };

    wf::key_callback prev_view_binding = [=] (auto)
    {
        return handle_switch_request(1);
    };

    wf::effect_hook_t damage = [=] ()
    {
        output->render->damage_whole();
    };

    wf::signal_callback_t view_removed = [=] (wf::signal_data_t *data)
    {
        handle_view_removed(get_signaled_view(data));
    };

    void handle_view_removed(wayfire_view view)
    {
        // not running at all, don't care
        if (!output->is_plugin_active(grab_interface->name))
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
        if (!output->is_plugin_active(grab_interface->name))
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

            // grabs shouldn't fail if we could successfully activate plugin
            auto grab = grab_interface->grab();
            assert(grab);

            focus_next(dir);
            arrange();
            activating_modifiers = wf::get_core().get_keyboard_modifiers();
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
        grab_interface->ungrab();
    }

    /* Sets up basic hooks needed while switcher works and/or displays animations.
     * Also lower any fullscreen views that are active */
    bool init_switcher()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        output->render->add_effect(&damage, wf::OUTPUT_EFFECT_PRE);
        output->render->set_renderer(switcher_renderer);
        output->render->set_redraw_always();

        return true;
    }

    /* The reverse of init_switcher */
    void deinit_switcher()
    {
        output->deactivate_plugin(grab_interface);

        output->render->rem_effect(&damage);
        output->render->set_renderer(nullptr);
        output->render->set_redraw_always(false);

        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            view->pop_transformer(switcher_transformer);
            view->pop_transformer(switcher_transformer_background);
        }

        views.clear();

        wf::stack_order_changed_signal data;
        data.output = output;
        wf::get_core().emit_signal("output-stack-order-changed", &data);
    }

    /* offset from the left or from the right */
    float get_center_offset()
    {
        return output->get_relative_geometry().width / 3;
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

    /* amount of rotation */
    float get_rotation()
    {
        return -M_PI / 6.0;
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

        sv.attribs.rotation.restart_with_end(
            sv.attribs.rotation.end + get_rotation() * dir);

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
    float get_view_normal_alpha(wayfire_view view)
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
        auto bbox = sv.view->get_bounding_box(switcher_transformer);

        float dx = (og.width / 2 - bbox.width / 2) - bbox.x;
        float dy = bbox.y - (og.height / 2 - bbox.height / 2);

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
    std::vector<wayfire_view> get_workspace_views() const
    {
        auto all_views = output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(),
            wf::WM_LAYERS | wf::LAYER_MINIMIZED);

        decltype(all_views) mapped_views;
        for (auto view : all_views)
        {
            if (view->is_mapped())
            {
                mapped_views.push_back(view);
            }
        }

        return mapped_views;
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

        output->workspace->bring_to_front(focused_view);
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
    }

    void dearrange()
    {
        /* When we have just 2 views on the workspace, we have 2 copies
         * of the unfocused view. When dearranging those copies, they overlap.
         * If the view is translucent, this means that the view gets darker than
         * it really is.  * To circumvent this, we just fade out one of the copies */
        wayfire_view fading_view = nullptr;
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
            output->focus_view(views[0].view, true);
        }
    }

    std::vector<wayfire_view> get_background_views() const
    {
        return output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(), wf::BELOW_LAYERS);
    }

    std::vector<wayfire_view> get_overlay_views() const
    {
        return output->workspace->get_views_on_workspace(
            output->workspace->get_current_workspace(), wf::ABOVE_LAYERS);
    }

    void dim_background(float dim)
    {
        for (auto view : get_background_views())
        {
            if (dim == 1.0)
            {
                view->pop_transformer(switcher_transformer_background);
            } else
            {
                if (!view->get_transformer(switcher_transformer_background))
                {
                    view->add_transformer(std::make_unique<wf::view_3D>(view),
                        switcher_transformer_background);
                }

                auto tr = dynamic_cast<wf::view_3D*>(
                    view->get_transformer(switcher_transformer_background).get());
                tr->color[0] = tr->color[1] = tr->color[2] = dim;
            }
        }
    }

    SwitcherView create_switcher_view(wayfire_view view)
    {
        /* we add a view transform if there isn't any.
         *
         * Note that a view might be visible on more than 1 place, so damage
         * tracking doesn't work reliably. To circumvent this, we simply damage
         * the whole output */
        if (!view->get_transformer(switcher_transformer))
        {
            view->add_transformer(std::make_unique<wf::view_3D>(view),
                switcher_transformer);
        }

        SwitcherView sw{duration};
        sw.view     = view;
        sw.position = SWITCHER_POSITION_CENTER;

        return sw;
    }

    void render_view(const SwitcherView& sv, const wf::framebuffer_t& buffer)
    {
        auto transform = dynamic_cast<wf::view_3D*>(
            sv.view->get_transformer(switcher_transformer).get());
        assert(transform);

        transform->translation = glm::translate(glm::mat4(1.0),
        {(double)sv.attribs.off_x, (double)sv.attribs.off_y,
            (double)sv.attribs.off_z});

        transform->scaling = glm::scale(glm::mat4(1.0),
            {(double)sv.attribs.scale_x, (double)sv.attribs.scale_y, 1.0});

        transform->rotation = glm::rotate(glm::mat4(1.0),
            (float)sv.attribs.rotation, {0.0, 1.0, 0.0});

        transform->color[3] = sv.attribs.alpha;
        sv.view->render_transformed(buffer, buffer.geometry);
    }

    wf::render_hook_t switcher_renderer = [=] (const wf::framebuffer_t& fb)
    {
        OpenGL::render_begin(fb);
        OpenGL::clear({0, 0, 0, 1});
        OpenGL::render_end();

        dim_background(background_dim);
        for (auto view : get_background_views())
        {
            view->render_transformed(fb, fb.geometry);
        }

        /* Render in the reverse order because we don't use depth testing */
        for (auto& view : wf::reverse(views))
        {
            render_view(view, fb);
        }

        for (auto view : get_overlay_views())
        {
            view->render_transformed(fb, fb.geometry);
        }

        if (!duration.running())
        {
            cleanup_expired();

            if (!active)
            {
                deinit_switcher();
            }
        }
    };

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
            fill_emtpy_slot(empty_slot);
        }

        rebuild_view_list();
        output->workspace->bring_to_front(views.front().view);
        duration.start();
    }

    int count_different_active_views()
    {
        std::set<wayfire_view> active_views;
        for (auto& sv : views)
        {
            active_views.insert(sv.view);
        }

        return active_views.size();
    }

/* Move the last view in the given slot so that it becomes invalid */
    wayfire_view invalidate_last_in_slot(int slot)
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
    wayfire_view get_unfocused_view()
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

    void fill_emtpy_slot(const int empty_slot)
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
        if (output->is_plugin_active(grab_interface->name))
        {
            deinit_switcher();
        }

        output->rem_binding(&next_view_binding);
        output->rem_binding(&prev_view_binding);
        output->disconnect_signal("view-detached", &view_removed);
    }
};

DECLARE_WAYFIRE_PLUGIN(WayfireSwitcher);

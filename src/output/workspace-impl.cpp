#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/opengl.hpp>
#include <list>
#include <algorithm>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/util/log.hpp>

#include <wayfire/scene-operations.hpp>

#include "../view/view-impl.hpp"
#include "output-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"

namespace wf
{
static void update_view_scene_node(wayfire_view view)
{
    using wf::scene::update_flag::update_flag;
    wf::scene::update(view->get_root_node(),
        update_flag::INPUT_STATE | update_flag::CHILDREN_LIST);
}

/** Damage the entire view tree including the view itself. */
void damage_views(wayfire_view view)
{
    for (auto view : view->enumerate_views(false))
    {
        view->damage();
    }
}

/**
 * output_layer_manager_t is a part of the workspace_manager module. It provides
 * the functionality related to layers and sublayers.
 */
class output_layer_manager_t
{
    // A hierarchical representation of the view stack order
    wf::output_t *output;

  public:
    output_layer_manager_t(wf::output_t *output)
    {
        this->output = output;
    }

    constexpr int layer_index_from_mask(uint32_t layer_mask) const
    {
        return __builtin_ctz(layer_mask);
    }

    void remove_view(wayfire_view view)
    {
        damage_views(view);
        scene::remove_child(view->get_root_node());
    }

    /** Add or move the view to the given layer */
    void add_view_to_layer(wayfire_view view, layer_t layer)
    {
        damage_views(view);
        auto idx = (wf::scene::layer)layer_index_from_mask(layer);
        scene::remove_child(view->get_root_node());

        if (layer == LAYER_WORKSPACE)
        {
            scene::add_front(output->get_wset(), view->get_root_node());
        } else
        {
            scene::add_front(output->node_for_layer(idx), view->get_root_node());
        }

        damage_views(view);
    }

    /** Precondition: view is in some sublayer */
    void bring_to_front(wayfire_view view)
    {
        wf::scene::node_t *node = view->get_root_node().get();
        wf::scene::node_t *damage_from = nullptr;
        while (node->parent())
        {
            if (!node->is_structure_node() &&
                dynamic_cast<scene::floating_inner_node_t*>(node->parent()))
            {
                damage_from = node->parent();
                wf::scene::raise_to_front(node->shared_from_this());
            }

            node = node->parent();
        }

        std::vector<wayfire_view> all_views;
        push_views_from_scenegraph(damage_from->shared_from_this(), all_views,
            false);

        for (auto& view : all_views)
        {
            view->damage();
        }
    }

    wayfire_view get_front_view(wf::layer_t layer)
    {
        auto views = get_views_in_layer(layer, false);
        if (views.size() == 0)
        {
            return nullptr;
        }

        return views.front();
    }

    void push_views_from_scenegraph(wf::scene::node_ptr root,
        std::vector<wayfire_view>& result, bool target_minimized)
    {
        if (auto view = node_to_view(root))
        {
            if (view->minimized == target_minimized)
            {
                result.push_back(view);
            }
        } else
        {
            for (auto& ch : root->get_children())
            {
                push_views_from_scenegraph(ch, result, target_minimized);
            }
        }
    }

    std::vector<wayfire_view> get_views_in_layer(uint32_t layers_mask,
        bool include_minimized)
    {
        std::vector<wayfire_view> views;
        auto try_push = [&] (scene::layer layer)
        {
            if (!((1u << (int)layer) & layers_mask))
            {
                return;
            }

            push_views_from_scenegraph(output->node_for_layer(layer), views, false);
        };

        /* Above fullscreen views */
        for (int layer = 0; layer < (int)scene::layer::ALL_LAYERS; layer++)
        {
            try_push((scene::layer)layer);
        }

        if (include_minimized)
        {
            push_views_from_scenegraph(
                output->node_for_layer(wf::scene::layer::WORKSPACE),
                views, true);
        }

        return views;
    }
};

struct default_workspace_implementation_t : public workspace_implementation_t
{
    bool view_movable(wayfire_view view)
    {
        return true;
    }

    bool view_resizable(wayfire_view view)
    {
        return true;
    }

    default_workspace_implementation_t() = default;
    virtual ~default_workspace_implementation_t() = default;
    default_workspace_implementation_t(const default_workspace_implementation_t &) =
    default;
    default_workspace_implementation_t(default_workspace_implementation_t &&) =
    default;
    default_workspace_implementation_t& operator =(
        const default_workspace_implementation_t&) = default;
    default_workspace_implementation_t& operator =(
        default_workspace_implementation_t&&) = default;
};

/**
 * The output_viewport_manager_t provides viewport-related functionality in
 * workspace_manager
 */
class output_viewport_manager_t
{
  private:
    wf::option_wrapper_t<int> vwidth_opt{"core/vwidth"};
    wf::option_wrapper_t<int> vheight_opt{"core/vheight"};

    int current_vx = 0;
    int current_vy = 0;

    output_t *output;

    // Grid size was set by a plugin?
    bool has_custom_grid_size = false;

    // Current dimensions of the grid
    wf::dimensions_t grid = {0, 0};

    std::function<void()> update_cfg_grid_size = [=] ()
    {
        if (has_custom_grid_size)
        {
            return;
        }

        auto old_grid = grid;
        grid = {vwidth_opt, vheight_opt};
        handle_grid_changed(old_grid);
    };

    wf::point_t closest_valid_ws(wf::point_t workspace)
    {
        workspace.x = wf::clamp(workspace.x, 0, grid.width - 1);
        workspace.y = wf::clamp(workspace.y, 0, grid.height - 1);
        return workspace;
    }

    /**
     * Handle a change in the workspace grid size.
     *
     * When it happens, we need to ensure that each view is at least partly
     * visible on the remaining workspaces.
     */
    void handle_grid_changed(wf::dimensions_t old_size)
    {
        if (!is_workspace_valid({current_vx, current_vy}))
        {
            set_workspace(closest_valid_ws({current_vx, current_vy}), {});
        }

        for (auto view : output->workspace->get_views_in_layer(
            wf::WM_LAYERS, true))
        {
            // XXX: we use the magic value 0.333, maybe something else would be
            // better?
            auto workspaces = get_view_workspaces(view, 0.333);

            bool is_visible = std::any_of(workspaces.begin(), workspaces.end(),
                [=] (auto ws) { return is_workspace_valid(ws); });

            if (!is_visible)
            {
                move_to_workspace(view, get_view_main_workspace(view));
            }
        }

        wf::workspace_grid_changed_signal data;
        data.old_grid_size = old_size;
        data.new_grid_size = grid;
        output->emit(&data);
    }

  public:
    output_viewport_manager_t(output_t *output)
    {
        this->output = output;

        vwidth_opt.set_callback(update_cfg_grid_size);
        vheight_opt.set_callback(update_cfg_grid_size);
        this->grid = {vwidth_opt, vheight_opt};
    }

    /**
     * @param threshold Threshold of the view to be counted
     *        on that workspace. 1.0 for 100% visible, 0.1 for 10%
     *
     * @return a vector of all the workspaces
     */
    std::vector<wf::point_t> get_view_workspaces(wayfire_view view, double threshold)
    {
        assert(view->get_output() == this->output);
        std::vector<wf::point_t> view_workspaces;
        wf::geometry_t workspace_relative_geometry;
        wlr_box view_bbox = view->get_bounding_box();

        for (int horizontal = 0; horizontal < grid.width; horizontal++)
        {
            for (int vertical = 0; vertical < grid.height; vertical++)
            {
                wf::point_t ws = {horizontal, vertical};
                if (output->workspace->view_visible_on(view, ws))
                {
                    workspace_relative_geometry = output->render->get_ws_box(ws);
                    auto intersection = wf::geometry_intersection(
                        view_bbox, workspace_relative_geometry);
                    double area = 1.0 * intersection.width * intersection.height;
                    area /= 1.0 * view_bbox.width * view_bbox.height;

                    if (area < threshold)
                    {
                        continue;
                    }

                    view_workspaces.push_back(ws);
                }
            }
        }

        return view_workspaces;
    }

    wf::point_t get_view_main_workspace(wayfire_view view)
    {
        auto og = output->get_screen_size();

        auto wm = view->get_wm_geometry();
        wf::point_t workspace = {
            current_vx + (int)std::floor((wm.x + wm.width / 2.0) / og.width),
            current_vy + (int)std::floor((wm.y + wm.height / 2.0) / og.height)
        };

        return closest_valid_ws(workspace);
    }

    /**
     * @param use_bbox When considering view visibility, whether to use the
     *        bounding box or the wm geometry.
     *
     * @return true if the view is visible on the workspace vp
     */
    bool view_visible_on(wayfire_view view, wf::point_t vp)
    {
        auto g = output->get_relative_geometry();
        if (!view->sticky)
        {
            g.x += (vp.x - current_vx) * g.width;
            g.y += (vp.y - current_vy) * g.height;
        }

        return g & view->get_wm_geometry();
    }

    /**
     * Moves view geometry so that it is visible on the given workspace
     */
    void move_to_workspace(wayfire_view view, wf::point_t ws)
    {
        if (view->get_output() != output)
        {
            LOGE("Cannot ensure view visibility for a view from a different output!");
            return;
        }

        // Sticky views are visible on all workspaces, so we just have to make
        // it visible on the current workspace
        if (view->sticky)
        {
            ws = {current_vx, current_vy};
        }

        auto box     = view->get_wm_geometry();
        auto visible = output->get_relative_geometry();
        visible.x += (ws.x - current_vx) * visible.width;
        visible.y += (ws.y - current_vy) * visible.height;

        if (!(box & visible))
        {
            /* center of the view */
            int cx = box.x + box.width / 2;
            int cy = box.y + box.height / 2;

            int width = visible.width, height = visible.height;
            /* compute center coordinates when moved to the current workspace */
            int local_cx = (cx % width + width) % width;
            int local_cy = (cy % height + height) % height;

            /* finally, calculate center coordinates in the target workspace */
            int target_cx = local_cx + visible.x;
            int target_cy = local_cy + visible.y;

            view->move(box.x + target_cx - cx, box.y + target_cy - cy);
        }
    }

    std::vector<wayfire_view> get_views_on_workspace(wf::point_t vp,
        uint32_t layers_mask, bool include_minimized)
    {
        /* get all views in the given layers */
        std::vector<wayfire_view> views =
            output->workspace->get_views_in_layer(layers_mask, include_minimized);

        /* remove those which aren't visible on the workspace */
        auto it = std::remove_if(views.begin(), views.end(), [&] (wayfire_view view)
        {
            return !view_visible_on(view, vp);
        });

        views.erase(it, views.end());

        return views;
    }

    wf::point_t get_current_workspace()
    {
        return {current_vx, current_vy};
    }

    wf::dimensions_t get_workspace_grid_size()
    {
        return grid;
    }

    void set_workspace_grid_size(wf::dimensions_t new_grid)
    {
        auto old = this->grid;
        this->grid = new_grid;
        this->has_custom_grid_size = true;
        handle_grid_changed(old);
    }

    bool is_workspace_valid(wf::point_t ws)
    {
        if ((ws.x >= grid.width) || (ws.y >= grid.height) || (ws.x < 0) ||
            (ws.y < 0))
        {
            return false;
        } else
        {
            return true;
        }
    }

    void set_workspace(wf::point_t nws,
        const std::vector<wayfire_view>& fixed_views)
    {
        if (!is_workspace_valid(nws))
        {
            LOGE("Attempt to set invalid workspace: ", nws,
                " workspace grid size is ", grid.width, "x", grid.height);

            return;
        }

        wf::workspace_changed_signal data;
        data.old_viewport = {current_vx, current_vy};
        data.new_viewport = {nws.x, nws.y};
        data.output = output;

        /* The part below is tricky, because with the current architecture
         * we cannot make the viewport change look atomic, i.e the workspace
         * is changed first, and then all views are moved.
         *
         * We first change the viewport, and then adjust the position of the
         * views. */
        current_vx = nws.x;
        current_vy = nws.y;

        auto screen = output->get_screen_size();
        auto dx     = (data.old_viewport.x - nws.x) * screen.width;
        auto dy     = (data.old_viewport.y - nws.y) * screen.height;

        std::vector<std::pair<wayfire_view, wf::point_t>>
        old_fixed_view_workspaces;
        old_fixed_view_workspaces.reserve(fixed_views.size());

        for (auto& view : output->workspace->get_views_in_layer(
            MIDDLE_LAYERS, true))
        {
            const auto is_fixed = std::find(fixed_views.cbegin(),
                fixed_views.cend(), view) != fixed_views.end();

            if (is_fixed)
            {
                old_fixed_view_workspaces.push_back({view,
                    get_view_main_workspace(view)});
            } else if (!view->sticky)
            {
                for (auto v : view->enumerate_views())
                {
                    v->move(v->get_wm_geometry().x + dx,
                        v->get_wm_geometry().y + dy);
                }
            }
        }

        for (auto& [v, old_workspace] : old_fixed_view_workspaces)
        {
            wf::view_change_workspace_signal vdata;
            vdata.view = v;
            vdata.from = old_workspace;
            vdata.to   = get_view_main_workspace(v);
            output->emit(&vdata);
            output->focus_view(v, true);
        }

        // Finally, do a refocus to update the keyboard focus
        output->refocus();
        output->emit(&data);

        // Don't forget to update the geometry of the wset, as the geometry of it has changed now.
        wf::scene::update(output->get_wset(), wf::scene::update_flag::GEOMETRY);
    }
};

/**
 * This class encapsulates functionality related to handling fullscreen views on a given workspace set.
 * When a fullscreen view is at the top of the stack, it should be 'promoted' above the top layer, where
 * panels reside. This is done by temporarily disabling the top layer, and then re-enabling it when the
 * fullscreen view is no longer fullscreen or no longer on top of all other views.
 *
 * Note that only views from the workspace layer are promoted, and views in the layers above do not affect
 * the view promotion algorithm.
 */
class promotion_manager_t
{
  public:
    promotion_manager_t(wf::output_t *output)
    {
        this->output = output;
        wf::get_core().scene()->connect(&on_root_node_updated);
        output->connect(&on_view_fullscreen);
        output->connect(&on_view_unmap);
    }

  private:
    wf::output_t *output;

    wf::signal::connection_t<wf::scene::root_node_update_signal> on_root_node_updated = [=] (auto)
    {
        update_promotion_state();
    };

    signal::connection_t<view_unmapped_signal> on_view_unmap = [=] (view_unmapped_signal *ev)
    {
        update_promotion_state();
    };

    wf::signal::connection_t<wf::view_fullscreen_signal> on_view_fullscreen = [=] (auto)
    {
        update_promotion_state();
    };

    wayfire_view find_top_visible_view(wf::scene::node_ptr root)
    {
        if (auto view = wf::node_to_view(root))
        {
            if (view->is_mapped() &&
                output->workspace->view_visible_on(view, output->workspace->get_current_workspace()))
            {
                return view;
            }
        }

        for (auto& ch : root->get_children())
        {
            if (ch->is_enabled())
            {
                if (auto result = find_top_visible_view(ch))
                {
                    return result;
                }
            }
        }

        return nullptr;
    }

    void update_promotion_state()
    {
        wayfire_view candidate = find_top_visible_view(output->get_wset());
        if (candidate && candidate->fullscreen)
        {
            start_promotion();
        } else
        {
            stop_promotion();
        }
    }

    bool promotion_active = false;

    // When a fullscreen view is on top of the stack, it should be displayed above
    // nodes in the TOP layer. To achieve this effect, we hide the TOP layer.
    void start_promotion()
    {
        if (promotion_active)
        {
            return;
        }

        promotion_active = true;
        scene::set_node_enabled(output->node_for_layer(scene::layer::TOP), false);

        wf::fullscreen_layer_focused_signal ev;
        ev.has_promoted = true;
        output->emit(&ev);
        LOGD("autohide panels");
    }

    void stop_promotion()
    {
        if (!promotion_active)
        {
            return;
        }

        promotion_active = false;
        scene::set_node_enabled(output->node_for_layer(scene::layer::TOP), true);

        wf::fullscreen_layer_focused_signal ev;
        ev.has_promoted = false;
        output->emit(&ev);
        LOGD("restore panels");
    }
};

class workspace_manager::impl
{
    wf::output_t *output;
    wf::geometry_t output_geometry;

    wf::signal::connection_t<output_configuration_changed_signal> output_geometry_changed =
        [=] (output_configuration_changed_signal *ev)
    {
        auto old_w = output_geometry.width, old_h = output_geometry.height;
        auto new_size = output->get_screen_size();
        if ((old_w == new_size.width) && (old_h == new_size.height))
        {
            // No actual change, stop here
            return;
        }

        for (auto& view : layer_manager.get_views_in_layer(MIDDLE_LAYERS, false))
        {
            if (!view->is_mapped())
            {
                continue;
            }

            auto wm  = view->get_wm_geometry();
            float px = 1. * wm.x / old_w;
            float py = 1. * wm.y / old_h;
            float pw = 1. * wm.width / old_w;
            float ph = 1. * wm.height / old_h;

            view->set_geometry({
                    int(px * new_size.width), int(py * new_size.height),
                    int(pw * new_size.width), int(ph * new_size.height)
                });
        }

        output_geometry = output->get_relative_geometry();
    };

    std::unique_ptr<workspace_implementation_t> workspace_impl;

  public:
    output_layer_manager_t layer_manager;
    output_viewport_manager_t viewport_manager;
    promotion_manager_t promotion_manager;

    impl(output_t *o) : layer_manager(o), viewport_manager(o), promotion_manager(o)
    {
        output = o;
        output_geometry = output->get_relative_geometry();
        o->connect(&output_geometry_changed);
    }

    workspace_implementation_t *get_implementation()
    {
        static default_workspace_implementation_t default_impl;

        return workspace_impl ? workspace_impl.get() : &default_impl;
    }

    bool set_implementation(std::unique_ptr<workspace_implementation_t> impl,
        bool overwrite)
    {
        bool replace = overwrite || !workspace_impl;

        if (replace)
        {
            workspace_impl = std::move(impl);
        }

        return replace;
    }

    void set_workspace(wf::point_t ws, const std::vector<wayfire_view>& fixed)
    {
        viewport_manager.set_workspace(ws, fixed);
    }

    void request_workspace(wf::point_t ws,
        const std::vector<wayfire_view>& fixed_views)
    {
        wf::workspace_change_request_signal data;
        data.carried_out  = false;
        data.old_viewport = viewport_manager.get_current_workspace();
        data.new_viewport = ws;
        data.output = output;
        data.fixed_views = fixed_views;
        output->emit(&data);

        if (!data.carried_out)
        {
            set_workspace(ws, fixed_views);
        }
    }

    void add_view_to_layer(wayfire_view view, layer_t layer)
    {
        assert(view->get_output() == output);
        layer_manager.add_view_to_layer(view, layer);
    }

    void bring_to_front(wayfire_view view)
    {
        layer_manager.bring_to_front(view);
    }

    void remove_view(wayfire_view view)
    {
        layer_manager.remove_view(view);
    }
};

workspace_manager::workspace_manager(output_t *wo) : pimpl(new impl(wo))
{}
workspace_manager::~workspace_manager() = default;

/* Just pass to the appropriate function from above */
std::vector<wf::point_t> workspace_manager::get_view_workspaces(wayfire_view view,
    double threshold)
{
    return pimpl->viewport_manager.get_view_workspaces(view, threshold);
}

wf::point_t workspace_manager::get_view_main_workspace(wayfire_view view)
{
    return pimpl->viewport_manager.get_view_main_workspace(view);
}

bool workspace_manager::view_visible_on(wayfire_view view, wf::point_t ws)
{
    return pimpl->viewport_manager.view_visible_on(view, ws);
}

std::vector<wayfire_view> workspace_manager::get_views_on_workspace(wf::point_t ws,
    uint32_t layer_mask, bool include_minimized)
{
    return pimpl->viewport_manager.get_views_on_workspace(
        ws, layer_mask, include_minimized);
}

void workspace_manager::move_to_workspace(wayfire_view view, wf::point_t ws)
{
    return pimpl->viewport_manager.move_to_workspace(view, ws);
}

void workspace_manager::add_view(wayfire_view view, layer_t layer)
{
    pimpl->add_view_to_layer(view, layer);
}

void workspace_manager::bring_to_front(wayfire_view view)
{
    pimpl->bring_to_front(view);
    update_view_scene_node(view);
}

void workspace_manager::remove_view(wayfire_view view)
{
    pimpl->remove_view(view);
    update_view_scene_node(view);
}

std::vector<wayfire_view> workspace_manager::get_views_in_layer(
    uint32_t layers_mask, bool include_minimized)
{
    return pimpl->layer_manager.get_views_in_layer(layers_mask, include_minimized);
}

workspace_implementation_t*workspace_manager::get_workspace_implementation()
{
    return pimpl->get_implementation();
}

bool workspace_manager::set_workspace_implementation(
    std::unique_ptr<workspace_implementation_t> impl, bool overwrite)
{
    return pimpl->set_implementation(std::move(impl), overwrite);
}

void workspace_manager::set_workspace(wf::point_t ws,
    const std::vector<wayfire_view>& fixed_views)
{
    return pimpl->set_workspace(ws, fixed_views);
}

void workspace_manager::request_workspace(wf::point_t ws,
    const std::vector<wayfire_view>& views)
{
    return pimpl->request_workspace(ws, views);
}

wf::point_t workspace_manager::get_current_workspace()
{
    return pimpl->viewport_manager.get_current_workspace();
}

wf::dimensions_t workspace_manager::get_workspace_grid_size()
{
    return pimpl->viewport_manager.get_workspace_grid_size();
}

void workspace_manager::set_workspace_grid_size(wf::dimensions_t dim)
{
    return pimpl->viewport_manager.set_workspace_grid_size(dim);
}

bool workspace_manager::is_workspace_valid(wf::point_t ws)
{
    return pimpl->viewport_manager.is_workspace_valid(ws);
}
} // namespace wf

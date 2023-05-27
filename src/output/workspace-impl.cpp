#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/opengl.hpp>
#include <set>
#include <algorithm>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/util/log.hpp>

#include <wayfire/scene-operations.hpp>

#include "../view/view-impl.hpp"
#include "output-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"

namespace wf
{
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

/**
 * This class encapsulates functionality related to the management of the workspace grid size.
 */
struct grid_size_manager_t
{
    wf::option_wrapper_t<int> vwidth_opt{"core/vwidth"};
    wf::option_wrapper_t<int> vheight_opt{"core/vheight"};
    wf::output_t *output;

    grid_size_manager_t(wf::output_t *o) : output(o)
    {
        vwidth_opt.set_callback(update_cfg_grid_size);
        vheight_opt.set_callback(update_cfg_grid_size);
        this->grid = {vwidth_opt, vheight_opt};
    }

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
        wf::workspace_grid_changed_signal data;
        data.old_grid_size = old_size;
        data.new_grid_size = grid;
        output->emit(&data);
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
};

static wf::scene::node_t *find_lca(wf::scene::node_t *a, wf::scene::node_t *b)
{
    wf::scene::node_t *iter = a;
    std::set<wf::scene::node_t*> a_ancestors;
    while (iter)
    {
        a_ancestors.insert(iter);
        iter = iter->parent();
    }

    iter = b;
    while (iter)
    {
        if (a_ancestors.count(iter))
        {
            return iter;
        }

        iter = iter->parent();
    }

    return nullptr;
}

static bool is_attached_to_scenegraph(wf::scene::node_t *a)
{
    auto root = wf::get_core().scene().get();
    while (a)
    {
        if (a == root)
        {
            return true;
        }

        a = a->parent();
    }

    return false;
}

static size_t find_index_in_parent(wf::scene::node_t *x, wf::scene::node_t *parent)
{
    while (x->parent() != parent)
    {
        x = x->parent();
    }

    auto& children = parent->get_children();
    auto it = std::find_if(children.begin(), children.end(), [&] (auto child) { return child.get() == x; });
    return it - children.begin();
}

class workspace_manager::impl
{
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

        for (auto& view : get_views(WSET_MAPPED_ONLY))
        {
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

    wf::signal::connection_t<workspace_grid_changed_signal> on_grid_changed =
        [=] (workspace_grid_changed_signal *ev)
    {
        if (!grid.is_workspace_valid({current_vx, current_vy}))
        {
            set_workspace(grid.closest_valid_ws({current_vx, current_vy}), {});
        }

        auto size = output->get_relative_geometry();
        wf::geometry_t full_grid = {
            -current_vx * size.width, -current_vy * size.height,
            grid.grid.width * size.width, grid.grid.height * size.height
        };

        for (auto view : get_views(WSET_MAPPED_ONLY))
        {
            if (!(view->get_wm_geometry() & full_grid))
            {
                move_to_workspace(view, get_view_main_workspace(view));
            }
        }
    };

    wf::signal::connection_t<view_destruct_signal> on_view_destruct = [=] (view_destruct_signal *ev)
    {
        remove_view(ev->view);
    };

    std::unique_ptr<workspace_implementation_t> workspace_impl;

  public:
    wf::output_t *output;
    promotion_manager_t promotion_manager;
    grid_size_manager_t grid;

    impl(output_t *o) : promotion_manager(o), grid(o)
    {
        output = o;
        output_geometry = output->get_relative_geometry();
        o->connect(&output_geometry_changed);
        o->connect(&on_grid_changed);
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

    void add_view(wayfire_view view)
    {
        if (std::find(wset_views.begin(), wset_views.end(), view) != wset_views.end())
        {
            return;
        }

        LOGC(WSET, "Adding view ", view, " to wset ", this);
        wset_views.push_back(view);
        view->connect(&on_view_destruct);
    }

    void remove_view(wayfire_view view)
    {
        auto it = std::find(wset_views.begin(), wset_views.end(), view);
        if (it == wset_views.end())
        {
            LOGW("Removing view ", view, " from wset ", this, " but the view is not there!");
            return;
        }

        LOGC(WSET, "Removing view ", view, " to from ", this);
        wset_views.erase(it);
        view->disconnect(&on_view_destruct);
    }

    std::vector<wayfire_view> get_views(uint32_t flags = 0, std::optional<wf::point_t> workspace = {})
    {
        if (!flags && !workspace)
        {
            return wset_views;
        }

        if (flags & WSET_CURRENT_WORKSPACE)
        {
            workspace = get_current_workspace();
        }

        auto views = wset_views;
        auto it    = std::remove_if(views.begin(), views.end(), [&] (wayfire_view view)
        {
            if ((flags & WSET_MAPPED_ONLY) && !view->is_mapped())
            {
                return true;
            }

            if ((flags & WSET_EXCLUDE_MINIMIZED) && view->minimized)
            {
                return true;
            }

            if ((flags & WSET_SORT_STACKING) && !is_attached_to_scenegraph(view->get_root_node().get()))
            {
                return true;
            }

            if (workspace && !view_visible_on(view, *workspace))
            {
                return true;
            }

            return false;
        });
        views.erase(it, views.end());

        if (flags & WSET_SORT_STACKING)
        {
            std::sort(views.begin(), views.end(), [] (wayfire_view a, wayfire_view b)
            {
                wf::scene::node_t *x   = a->get_root_node().get();
                wf::scene::node_t *y   = b->get_root_node().get();
                wf::scene::node_t *lca = find_lca(x, y);
                wf::dassert(lca != nullptr,
                    "LCA should always exist when the two nodes are in the scenegraph!");
                wf::dassert((lca != x) && (lca != y), "LCA should not be equal to one of the nodes, this"
                                                      "means nested views/dialogs have been added to the wset!");

                const size_t idx_x = find_index_in_parent(x, lca);
                const size_t idx_y = find_index_in_parent(y, lca);
                return idx_x < idx_y;
            });
        }

        return views;
    }

  private:
    std::vector<wayfire_view> wset_views;

    int current_vx = 0;
    int current_vy = 0;

  public:
    wf::point_t get_view_main_workspace(wayfire_view view)
    {
        auto og = output->get_screen_size();

        auto wm = view->get_wm_geometry();
        wf::point_t workspace = {
            current_vx + (int)std::floor((wm.x + wm.width / 2.0) / og.width),
            current_vy + (int)std::floor((wm.y + wm.height / 2.0) / og.height)
        };

        return grid.closest_valid_ws(workspace);
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

    wf::point_t get_current_workspace()
    {
        return {current_vx, current_vy};
    }

    void set_workspace(wf::point_t nws,
        const std::vector<wayfire_view>& fixed_views)
    {
        if (!grid.is_workspace_valid(nws))
        {
            LOGE("Attempt to set invalid workspace: ", nws,
                " workspace grid size is ", grid.grid.width, "x", grid.grid.height);
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

        for (auto& view : wset_views)
        {
            const auto is_fixed = std::find(fixed_views.cbegin(),
                fixed_views.cend(), view) != fixed_views.end();

            if (is_fixed)
            {
                old_fixed_view_workspaces.push_back({view, get_view_main_workspace(view)});
            } else if (!view->sticky)
            {
                for (auto v : view->enumerate_views())
                {
                    v->move(v->get_wm_geometry().x + dx, v->get_wm_geometry().y + dy);
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

workspace_manager::workspace_manager(output_t *wo) : pimpl(new impl(wo))
{}
workspace_manager::~workspace_manager() = default;

/* Just pass to the appropriate function from above */
wf::point_t workspace_manager::get_view_main_workspace(wayfire_view view)
{
    return pimpl->get_view_main_workspace(view);
}

bool workspace_manager::view_visible_on(wayfire_view view, wf::point_t ws)
{
    return pimpl->view_visible_on(view, ws);
}

void workspace_manager::move_to_workspace(wayfire_view view, wf::point_t ws)
{
    return pimpl->move_to_workspace(view, ws);
}

void workspace_manager::add_view(wayfire_view view)
{
    pimpl->add_view(view);
}

std::vector<wayfire_view> workspace_manager::get_views(uint32_t flags, std::optional<wf::point_t> ws)
{
    return pimpl->get_views(flags, ws);
}

void workspace_manager::remove_view(wayfire_view view)
{
    pimpl->remove_view(view);
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

void workspace_manager::request_workspace(wf::point_t ws, const std::vector<wayfire_view>& views)
{
    wf::workspace_change_request_signal data;
    data.carried_out  = false;
    data.old_viewport = pimpl->get_current_workspace();
    data.new_viewport = ws;
    data.output = pimpl->output;
    data.fixed_views = views;
    pimpl->output->emit(&data);

    if (!data.carried_out)
    {
        pimpl->set_workspace(ws, views);
    }
}

wf::point_t workspace_manager::get_current_workspace()
{
    return pimpl->get_current_workspace();
}

wf::dimensions_t workspace_manager::get_workspace_grid_size()
{
    return pimpl->grid.get_workspace_grid_size();
}

void workspace_manager::set_workspace_grid_size(wf::dimensions_t dim)
{
    return pimpl->grid.set_workspace_grid_size(dim);
}

bool workspace_manager::is_workspace_valid(wf::point_t ws)
{
    return pimpl->grid.is_workspace_valid(ws);
}
} // namespace wf

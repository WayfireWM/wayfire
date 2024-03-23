#include <string>
#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/opengl.hpp>
#include <set>
#include <algorithm>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/seat.hpp>

#include <wayfire/scene-operations.hpp>

#include "../view/view-impl.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/nonstd/tracking-allocator.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"

namespace wf
{
/**
 * This class encapsulates functionality related to the management of the workspace grid size.
 */
struct grid_size_manager_t
{
    wf::option_wrapper_t<int> vwidth_opt{"core/vwidth"};
    wf::option_wrapper_t<int> vheight_opt{"core/vheight"};
    workspace_set_t *set;

    grid_size_manager_t(workspace_set_t *wset)
    {
        this->set = wset;
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
        set->emit(&data);
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

static bool is_attached_to(wf::scene::node_t *a, wf::scene::node_t *root)
{
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

static bool is_attached_to_scenegraph(wf::scene::node_t *a)
{
    return is_attached_to(a, wf::get_core().scene().get());
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

class workspace_set_root_node_t : public wf::scene::floating_inner_node_t
{
    uint64_t index;

  public:
    workspace_set_root_node_t(uint64_t index) : floating_inner_node_t(true)
    {
        this->index = index;
    }

    std::string stringify() const override
    {
        return "workspace-set id=" + std::to_string(index) + " " + stringify_flags();
    }
};

std::vector<nonstd::observer_ptr<workspace_set_t>> workspace_set_t::get_all()
{
    return tracking_allocator_t<workspace_set_t>::get().get_all();
}

struct workspace_set_t::impl
{
    uint64_t index;

    /**
     * The geometry of the last output the workspace output was active on.
     */
    std::optional<wf::geometry_t> workspace_geometry;

    wf::signal::connection_t<output_configuration_changed_signal> output_geometry_changed =
        [=] (output_configuration_changed_signal *ev)
    {
        change_output_geometry(output->get_relative_geometry());
    };

    wf::signal::connection_t<output_removed_signal> on_output_removed = [=] (output_removed_signal *ev)
    {
        if (ev->output == this->output)
        {
            attach_to_output(nullptr, OLD_OUTPUT_DESTROY);
        }
    };

    void change_output_geometry(wf::geometry_t new_geometry)
    {
        if (!workspace_geometry)
        {
            workspace_geometry = new_geometry;
            return;
        }

        auto old_w = workspace_geometry->width, old_h = workspace_geometry->height;
        if (wf::dimensions(*workspace_geometry) == wf::dimensions(new_geometry))
        {
            // No actual change, stop here
            return;
        }

        for (auto& view : get_views(WSET_MAPPED_ONLY))
        {
            auto wm  = view->get_geometry();
            float px = 1. * wm.x / old_w;
            float py = 1. * wm.y / old_h;

            if (view->get_output() && view->toplevel()->current().fullscreen)
            {
                view->set_geometry(new_geometry);
            } else if (view->toplevel()->current().tiled_edges)
            {
                // Do nothing. This is taken care of, by the grid plugin.
                // If the user does not have grid enabled, we ignore it anyways.
            } else
            {
                view->set_geometry({
                        int(px * new_geometry.width), int(py * new_geometry.height),
                        wm.width, wm.height
                    });
            }
        }

        workspace_geometry = new_geometry;
    }

    wf::signal::connection_t<workspace_grid_changed_signal> on_grid_changed =
        [=] (workspace_grid_changed_signal *ev)
    {
        if (!workspace_geometry)
        {
            return;
        }

        if (!grid.is_workspace_valid({current_vx, current_vy}))
        {
            set_workspace(grid.closest_valid_ws({current_vx, current_vy}), {});
        }

        wf::geometry_t full_grid = {
            -current_vx * workspace_geometry->width, -current_vy * workspace_geometry->height,
            grid.grid.width * workspace_geometry->width, grid.grid.height * workspace_geometry->height
        };

        for (auto view : get_views(WSET_MAPPED_ONLY))
        {
            if (!(view->get_geometry() & full_grid))
            {
                move_to_workspace(view, get_view_main_workspace(view));
            }
        }
    };

    wf::signal::connection_t<wf::destruct_signal<view_interface_t>> on_view_destruct =
        [=] (wf::destruct_signal<view_interface_t> *ev)
    {
        remove_view(toplevel_cast(ev->object));
    };

    bool visible = false;

  public:
    wf::output_t *output = nullptr;
    workspace_set_t *self;
    grid_size_manager_t grid;
    scene::floating_inner_ptr wnode;

    impl(workspace_set_t *self, int64_t index) : grid(self)
    {
        this->self  = self;
        this->index = index;

        LOGC(WSET, "Creating new workspace set with id=", index);
        wnode = std::make_shared<workspace_set_root_node_t>(index);
        wnode->set_enabled(false);
        self->connect(&on_grid_changed);
        wf::get_core().output_layout->connect(&on_output_removed);
    }

    ~impl()
    {
        LOGC(WSET, "Destroying workspace set with id=", index);
        attach_to_output(nullptr, SELF_DESTROY);
        for (auto view : wset_views)
        {
            view->priv->current_wset.reset();
        }
    }

    enum attach_flags
    {
        // The current output is being destroyed
        OLD_OUTPUT_DESTROY = (1 << 0),
        // `this` is being freed.
        SELF_DESTROY       = (1 << 1),
    };

    void attach_to_output(wf::output_t *new_output, uint32_t flags)
    {
        if (new_output == output)
        {
            return;
        }

        LOGC(WSET, "Attaching workspace set id=", index, " to output ",
            (new_output ? new_output->to_string() : "null"));

        if (output)
        {
            output->disconnect(&output_geometry_changed);
            wf::scene::remove_child(wnode);
        }

        workspace_set_attached_signal data;
        data.old_output = output;
        data.set = self;
        output   = new_output;

        if (new_output)
        {
            change_output_geometry(new_output->get_relative_geometry());
            new_output->connect(&output_geometry_changed);
            scene::add_front(new_output->node_for_layer(scene::layer::WORKSPACE), wnode);
        }

        for (auto view : this->wset_views)
        {
            view->set_output(new_output);
        }

        if (!(flags & SELF_DESTROY))
        {
            self->emit<workspace_set_attached_signal>(&data);
        }
    }

    void set_visible(bool visible)
    {
        if (visible == this->visible)
        {
            return;
        }

        LOGC(WSET, "Changing visibility of workspace set id=", index, " visible=", visible);
        this->visible = visible;
        wf::scene::set_node_enabled(wnode, visible);
        for (auto& view : wset_views)
        {
            if (is_attached_to(view->get_root_node().get(), wnode.get()))
            {
                // Attached/detached state same as wnode
                continue;
            }

            wf::scene::set_node_enabled(view->get_root_node(), visible);
        }
    }

    void add_view(wayfire_toplevel_view view)
    {
        if (std::find(wset_views.begin(), wset_views.end(), view) != wset_views.end())
        {
            return;
        }

        LOGC(WSET, "Adding view ", view, " to wset ", index);
        wset_views.push_back(view);
        view->connect(&on_view_destruct);
        view->priv->current_wset = self->weak_from_this();
        view->set_output(this->output);
    }

    void remove_view(wayfire_toplevel_view view)
    {
        auto it = std::find(wset_views.begin(), wset_views.end(), view);
        if (it == wset_views.end())
        {
            LOGW("Removing view ", view, " from wset id=", index, " but the view is not there!");
            return;
        }

        LOGC(WSET, "Removing view ", view, " from id=", index);
        wset_views.erase(it);
        view->disconnect(&on_view_destruct);
        view->priv->current_wset.reset();
    }

    std::vector<wayfire_toplevel_view> get_views(uint32_t flags = 0,
        std::optional<wf::point_t> workspace = {})
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
        auto it    = std::remove_if(views.begin(), views.end(), [&] (wayfire_toplevel_view view)
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
            std::sort(views.begin(), views.end(), [] (wayfire_toplevel_view a, wayfire_view b)
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
    std::vector<wayfire_toplevel_view> wset_views;

    int current_vx = 0;
    int current_vy = 0;

  public:
    wf::point_t get_view_main_workspace(wayfire_toplevel_view view)
    {
        if (!workspace_geometry)
        {
            LOGW("Workspace-set id=", index, " does not have any output/geometry yet!");
            return {0, 0};
        }

        auto wm = view->get_geometry();
        wf::point_t workspace = {
            current_vx + (int)std::floor((wm.x + wm.width / 2.0) / workspace_geometry->width),
            current_vy + (int)std::floor((wm.y + wm.height / 2.0) / workspace_geometry->height)
        };

        return grid.closest_valid_ws(workspace);
    }

    /**
     * @param use_bbox When considering view visibility, whether to use the
     *        bounding box or the wm geometry.
     *
     * @return true if the view is visible on the workspace vp
     */
    bool view_visible_on(wayfire_toplevel_view view, wf::point_t vp)
    {
        if (!workspace_geometry)
        {
            LOGW("Workspace-set id=", index, " does not have any output/geometry yet!");
            return false;
        }

        auto g = *workspace_geometry;
        if (!view->sticky)
        {
            g.x += (vp.x - current_vx) * g.width;
            g.y += (vp.y - current_vy) * g.height;
        }

        return g & view->get_geometry();
    }

    /**
     * Moves view geometry so that it is visible on the given workspace
     */
    void move_to_workspace(wayfire_toplevel_view view, wf::point_t ws)
    {
        if (!workspace_geometry)
        {
            LOGW("Workspace-set id=", index, " does not have any output/geometry yet!");
            return;
        }

        // Sticky views are visible on all workspaces, so we just have to make
        // it visible on the current workspace
        if (view->sticky)
        {
            ws = {current_vx, current_vy};
        }

        auto box = view->get_pending_geometry();
        wf::geometry_t visible = *workspace_geometry;
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
        const std::vector<wayfire_toplevel_view>& fixed_views)
    {
        if (!grid.is_workspace_valid(nws))
        {
            LOGE("Attempt to set invalid workspace: ", nws,
                " workspace grid size is ", grid.grid.width, "x", grid.grid.height);
            return;
        }

        if (!workspace_geometry)
        {
            LOGW("Workspace-set id=", index, " does not have any output/geometry yet!");
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

        auto screen = wf::dimensions(*workspace_geometry);
        auto dx     = (data.old_viewport.x - nws.x) * screen.width;
        auto dy     = (data.old_viewport.y - nws.y) * screen.height;

        std::vector<std::pair<wayfire_toplevel_view, wf::point_t>>
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
                    v->move(v->get_pending_geometry().x + dx, v->get_pending_geometry().y + dy);
                }
            }
        }

        for (auto& [v, old_workspace] : old_fixed_view_workspaces)
        {
            wf::view_change_workspace_signal vdata;
            vdata.view = v;
            vdata.from = old_workspace;
            vdata.to   = get_view_main_workspace(v);

            self->emit(&vdata);

            if (output)
            {
                output->emit(&vdata);
                wf::get_core().default_wm->focus_raise_view(v, false);
            }
        }

        self->emit(&data);
        if (output)
        {
            // Finally, do a refocus to update the keyboard focus
            wf::get_core().seat->refocus();
            output->emit(&data);
        }

        // Don't forget to update the geometry of the wset, as the geometry of it has changed now.
        // FIXME: in theory this isn't enough, as there may be views outside, but in practice, nobody cares ..
        wf::scene::update(wnode, wf::scene::update_flag::GEOMETRY);
    }
};

workspace_set_t::workspace_set_t(int64_t index) : pimpl(new impl(this, index))
{}
workspace_set_t::~workspace_set_t() = default;

void workspace_set_t::attach_to_output(wf::output_t *output)
{
    pimpl->attach_to_output(output, 0);
}

wf::output_t*workspace_set_t::get_attached_output()
{
    return pimpl->output;
}

void workspace_set_t::set_visible(bool visible)
{
    pimpl->set_visible(visible);
}

/* Just pass to the appropriate function from above */
wf::point_t workspace_set_t::get_view_main_workspace(wayfire_toplevel_view view)
{
    return pimpl->get_view_main_workspace(view);
}

bool workspace_set_t::view_visible_on(wayfire_toplevel_view view, wf::point_t ws)
{
    return pimpl->view_visible_on(view, ws);
}

void workspace_set_t::move_to_workspace(wayfire_toplevel_view view, wf::point_t ws)
{
    return pimpl->move_to_workspace(view, ws);
}

void workspace_set_t::add_view(wayfire_toplevel_view view)
{
    pimpl->add_view(view);
}

std::vector<wayfire_toplevel_view> workspace_set_t::get_views(uint32_t flags, std::optional<wf::point_t> ws)
{
    return pimpl->get_views(flags, ws);
}

void workspace_set_t::remove_view(wayfire_toplevel_view view)
{
    pimpl->remove_view(view);
}

void workspace_set_t::set_workspace(wf::point_t ws,
    const std::vector<wayfire_toplevel_view>& fixed_views)
{
    return pimpl->set_workspace(ws, fixed_views);
}

void workspace_set_t::request_workspace(wf::point_t ws, const std::vector<wayfire_toplevel_view>& views)
{
    if (!pimpl->output)
    {
        pimpl->set_workspace(ws, views);
        return;
    }

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

wf::point_t workspace_set_t::get_current_workspace()
{
    return pimpl->get_current_workspace();
}

wf::dimensions_t workspace_set_t::get_workspace_grid_size()
{
    return pimpl->grid.get_workspace_grid_size();
}

void workspace_set_t::set_workspace_grid_size(wf::dimensions_t dim)
{
    return pimpl->grid.set_workspace_grid_size(dim);
}

bool workspace_set_t::is_workspace_valid(wf::point_t ws)
{
    return pimpl->grid.is_workspace_valid(ws);
}

scene::floating_inner_ptr workspace_set_t::get_node() const
{
    return pimpl->wnode;
}

uint64_t workspace_set_t::get_index() const
{
    return pimpl->index;
}

std::optional<wf::geometry_t> workspace_set_t::get_last_output_geometry()
{
    if (pimpl->output)
    {
        return pimpl->output->get_relative_geometry();
    }

    return pimpl->workspace_geometry;
}

void emit_view_pre_moved_to_wset_pre(wayfire_toplevel_view view,
    std::shared_ptr<workspace_set_t> old_wset, std::shared_ptr<workspace_set_t> new_wset)
{
    view_pre_moved_to_wset_signal data;
    data.view     = view;
    data.old_wset = old_wset;
    data.new_wset = new_wset;
    wf::get_core().emit(&data);
}

void emit_view_moved_to_wset(wayfire_toplevel_view view,
    std::shared_ptr<workspace_set_t> old_wset, std::shared_ptr<workspace_set_t> new_wset)
{
    view_moved_to_wset_signal data;
    data.view     = view;
    data.old_wset = old_wset;
    data.new_wset = new_wset;
    wf::get_core().emit(&data);
}

static int64_t choose_lowest_free_wset_id(int64_t hint_index)
{
    auto all_wsets = workspace_set_t::get_all();
    std::set<int64_t> used_indices;
    for (auto& wset : all_wsets)
    {
        used_indices.insert(wset->get_index());
    }

    int64_t index = 1;

    if ((hint_index <= 0) || used_indices.count(hint_index))
    {
        // Select lowest unused ID.
        for (index = 1; used_indices.count(index); index++)
        {}
    } else
    {
        index = hint_index;
    }

    return index;
}

std::shared_ptr<workspace_set_t> workspace_set_t::create(int64_t index)
{
    index = choose_lowest_free_wset_id(index);
    auto wset = wf::tracking_allocator_t<workspace_set_t>::get().allocate<workspace_set_t>(index);
    return wset;
}
} // namespace wf

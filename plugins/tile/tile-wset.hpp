#pragma once

#include "tree.hpp"
#include "tree-controller.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/txn/transaction-manager.hpp"
#include "wayfire/scene-operations.hpp"
#include <wayfire/workarea.hpp>
#include <wayfire/window-manager.hpp>

struct autocommit_transaction_t
{
  public:
    wf::txn::transaction_uptr tx;
    autocommit_transaction_t()
    {
        tx = wf::txn::transaction_t::create();
    }

    ~autocommit_transaction_t()
    {
        if (!tx->get_objects().empty())
        {
            wf::get_core().tx_manager->schedule_transaction(std::move(tx));
        }
    }
};

namespace wf
{
/**
 * When a view is moved from one output to the other, we want to keep its tiled
 * status. To achieve this, we do the following:
 *
 * 1. In view-pre-moved-to-output handler, we set view_auto_tile_t custom data.
 * 2. In detach handler, we just remove the view as usual.
 * 3. We now know we will receive attach as next event.
 *    Check for view_auto_tile_t, and tile the view again.
 */
class view_auto_tile_t : public wf::custom_data_t
{};

class tile_workspace_set_data_t : public wf::custom_data_t
{
  public:
    std::vector<std::vector<std::unique_ptr<wf::tile::tree_node_t>>> roots;
    std::vector<std::vector<wf::scene::floating_inner_ptr>> tiled_sublayer;
    static constexpr wf::tile::split_direction_t default_split = wf::tile::SPLIT_VERTICAL;

    wf::option_wrapper_t<int> inner_gaps{"simple-tile/inner_gap_size"};
    wf::option_wrapper_t<int> outer_horiz_gaps{"simple-tile/outer_horiz_gap_size"};
    wf::option_wrapper_t<int> outer_vert_gaps{"simple-tile/outer_vert_gap_size"};

    tile_workspace_set_data_t(std::shared_ptr<wf::workspace_set_t> wset)
    {
        this->wset = wset;
        wset->connect(&on_wset_attached);
        wset->connect(&on_workspace_grid_changed);
        resize_roots(wset->get_workspace_grid_size());
        if (wset->get_attached_output())
        {
            wset->get_attached_output()->connect(&on_workarea_changed);
        }

        inner_gaps.set_callback(update_gaps);
        outer_horiz_gaps.set_callback(update_gaps);
        outer_vert_gaps.set_callback(update_gaps);
    }

    wf::signal::connection_t<workarea_changed_signal> on_workarea_changed = [=] (auto)
    {
        update_root_size();
    };

    wf::signal::connection_t<workspace_set_attached_signal> on_wset_attached = [=] (auto)
    {
        on_workarea_changed.disconnect();
        if (wset.lock()->get_attached_output())
        {
            wset.lock()->get_attached_output()->connect(&on_workarea_changed);
            update_root_size();
        }
    };

    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_workspace_grid_changed = [=] (auto)
    {
        wf::dassert(!wset.expired(), "wset should not expire, ever!");
        resize_roots(wset.lock()->get_workspace_grid_size());
    };

    void resize_roots(wf::dimensions_t wsize)
    {
        for (size_t i = 0; i < tiled_sublayer.size(); i++)
        {
            for (size_t j = 0; j < tiled_sublayer[i].size(); j++)
            {
                if (wset.lock()->is_workspace_valid({(int)i, (int)j}))
                {
                    destroy_sublayer(tiled_sublayer[i][j]);
                }
            }
        }

        roots.resize(wsize.width);
        tiled_sublayer.resize(wsize.width);
        for (int i = 0; i < wsize.width; i++)
        {
            roots[i].resize(wsize.height);
            tiled_sublayer[i].resize(wsize.height);
            for (int j = 0; j < wsize.height; j++)
            {
                roots[i][j] = std::make_unique<wf::tile::split_node_t>(default_split);
                tiled_sublayer[i][j] = std::make_shared<wf::scene::floating_inner_node_t>(false);
                wf::scene::add_front(wset.lock()->get_node(), tiled_sublayer[i][j]);
            }
        }

        update_root_size();
        update_gaps();
    }

    void update_root_size()
    {
        auto wo = wset.lock()->get_attached_output();
        wf::geometry_t workarea = wo ? wo->workarea->get_workarea() : tile::default_output_resolution;

        wf::geometry_t output_geometry =
            wset.lock()->get_last_output_geometry().value_or(tile::default_output_resolution);

        auto wsize = wset.lock()->get_workspace_grid_size();
        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                /* Set size */
                auto vp_geometry = workarea;
                vp_geometry.x += i * output_geometry.width;
                vp_geometry.y += j * output_geometry.height;

                autocommit_transaction_t tx;
                roots[i][j]->set_geometry(vp_geometry, tx.tx);
            }
        }
    }

    void destroy_sublayer(wf::scene::floating_inner_ptr sublayer)
    {
        // Transfer views to the top
        auto root     = wset.lock()->get_node();
        auto children = root->get_children();
        auto sublayer_children = sublayer->get_children();
        sublayer->set_children_list({});
        children.insert(children.end(), sublayer_children.begin(), sublayer_children.end());
        root->set_children_list(children);
        wf::scene::update(root, wf::scene::update_flag::CHILDREN_LIST);
        wf::scene::remove_child(sublayer);
    }

    tile::gap_size_t get_gaps() const
    {
        return {
            .left   = outer_horiz_gaps,
            .right  = outer_horiz_gaps,
            .top    = outer_vert_gaps,
            .bottom = outer_vert_gaps,
            .internal = inner_gaps,
        };
    }

    void update_gaps_with_tx(wf::txn::transaction_uptr& tx)
    {
        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                root->set_gaps(get_gaps());
                root->set_geometry(root->geometry, tx);
            }
        }
    }

    void refresh(wf::txn::transaction_uptr& tx)
    {
        flatten_roots();
        update_gaps_with_tx(tx);
    }

    std::function<void()> update_gaps = [=] ()
    {
        autocommit_transaction_t tx;
        update_gaps_with_tx(tx.tx);
    };

    void flatten_roots()
    {
        for (auto& col : roots)
        {
            for (auto& root : col)
            {
                tile::flatten_tree(root);
            }
        }
    }

    static tile_workspace_set_data_t& get(std::shared_ptr<workspace_set_t> set)
    {
        if (!set->has_data<tile_workspace_set_data_t>())
        {
            set->store_data(std::make_unique<tile_workspace_set_data_t>(set));
        }

        return *set->get_data<tile_workspace_set_data_t>();
    }

    static tile_workspace_set_data_t& get(wf::output_t *output)
    {
        return get(output->wset());
    }

    static std::unique_ptr<tile::tree_node_t>& get_current_root(wf::output_t *output)
    {
        auto set   = output->wset();
        auto vp    = set->get_current_workspace();
        auto& data = get(output);
        return data.roots[vp.x][vp.y];
    }

    static scene::floating_inner_ptr get_current_sublayer(wf::output_t *output)
    {
        auto set   = output->wset();
        auto vp    = set->get_current_workspace();
        auto& data = get(output);
        return data.tiled_sublayer[vp.x][vp.y];
    }

    std::weak_ptr<workspace_set_t> wset;

    std::unique_ptr<wf::tile::view_node_t> setup_view_tiling(wayfire_toplevel_view view, wf::point_t vp)
    {
        view->set_allowed_actions(VIEW_ALLOW_WS_CHANGE);
        auto node = view->get_root_node();
        wf::scene::readd_front(tiled_sublayer[vp.x][vp.y], node);
        view_bring_to_front(view);
        return std::make_unique<wf::tile::view_node_t>(view);
    }

    void attach_view(wayfire_toplevel_view view, std::optional<wf::point_t> _vp = {})
    {
        auto vp = _vp.value_or(wset.lock()->get_current_workspace());
        auto view_node = setup_view_tiling(view, vp);
        {
            autocommit_transaction_t tx;
            roots[vp.x][vp.y]->as_split_node()->add_child(std::move(view_node), tx.tx);
        }

        consider_exit_fullscreen(view);
    }

    /** Remove the given view from its tiling container */
    void detach_views(std::vector<nonstd::observer_ptr<tile::view_node_t>> views,
        bool reinsert = true)
    {
        {
            autocommit_transaction_t tx;
            for (auto& v : views)
            {
                auto view = v->view;
                view->set_allowed_actions(VIEW_ALLOW_ALL);
                // After this, `v` is freed.
                v->parent->remove_child(v, tx.tx);

                if (view->pending_fullscreen() && view->is_mapped())
                {
                    wf::get_core().default_wm->fullscreen_request(view, nullptr, false);
                }

                if (reinsert && view->get_output())
                {
                    wf::scene::readd_front(view->get_output()->wset()->get_node(), view->get_root_node());
                }
            }
        }

        /* View node is invalid now */
        flatten_roots();
        update_root_size();
    }

    /**
     * Consider unfullscreening all fullscreen views because a new view has been focused or attached to the
     * tiling tree.
     */
    void consider_exit_fullscreen(wayfire_toplevel_view view)
    {
        if (tile::view_node_t::get_node(view) && !view->pending_fullscreen())
        {
            auto vp = this->wset.lock()->get_current_workspace();
            for_each_view(roots[vp.x][vp.y], [&] (wayfire_toplevel_view view)
            {
                if (view->pending_fullscreen())
                {
                    set_view_fullscreen(view, false);
                }
            });
        }
    }

    void set_view_fullscreen(wayfire_toplevel_view view, bool fullscreen)
    {
        /* Set fullscreen, and trigger resizing of the views (which will commit the view) */
        view->toplevel()->pending().fullscreen = fullscreen;
        update_root_size();
    }
};
}

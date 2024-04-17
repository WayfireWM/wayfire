#include "tree-controller.hpp"
#include <set>

#include <wayfire/nonstd/tracking-allocator.hpp>
#include <algorithm>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/util.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/plugins/common/preview-indication.hpp>
#include <wayfire/txn/transaction-manager.hpp>

namespace wf
{
namespace tile
{
void for_each_view(nonstd::observer_ptr<tree_node_t> root,
    std::function<void(wayfire_toplevel_view)> callback)
{
    if (root->as_view_node())
    {
        callback(root->as_view_node()->view);

        return;
    }

    for (auto& child : root->children)
    {
        for_each_view(child, callback);
    }
}

nonstd::observer_ptr<view_node_t> find_view_at(
    nonstd::observer_ptr<tree_node_t> root, wf::point_t input)
{
    if (root->as_view_node())
    {
        return root->as_view_node();
    }

    for (auto& child : root->children)
    {
        if (child->geometry & input)
        {
            return find_view_at({child}, input);
        }
    }

    /* Children probably empty? */
    return nullptr;
}

nonstd::observer_ptr<view_node_t> find_first_view_in_direction(
    nonstd::observer_ptr<tree_node_t> from, split_insertion_t direction)
{
    auto window = from->geometry;

    /* Since nodes are arranged tightly into a grid, we can just find the
     * proper edge and find the view there */
    wf::point_t point;
    switch (direction)
    {
      case INSERT_ABOVE:
        point = {
            window.x + window.width / 2,
            window.y - 1,
        };
        break;

      case INSERT_BELOW:
        point = {
            window.x + window.width / 2,
            window.y + window.height,
        };
        break;

      case INSERT_LEFT:
        point = {
            window.x - 1,
            window.y + window.height / 2,
        };
        break;

      case INSERT_RIGHT:
        point = {
            window.x + window.width,
            window.y + window.height / 2,
        };
        break;

      default:
        assert(false);
    }

    auto root = from;
    while (root->parent)
    {
        root = root->parent;
    }

    return find_view_at(root, point);
}

/* ------------------------ move_view_controller_t -------------------------- */
move_view_controller_t::move_view_controller_t(wf::workspace_set_t *set, wayfire_toplevel_view grabbed_view)
{
    if (this->drag_helper->view)
    {
        // Race conditions are possible: spontaneous move requests from xwayland could trigger move without
        // a pressed button, and then if the user tries to use simple-tile, it leads to a crash.
        return;
    }

    this->drag_helper->set_pending_drag(wf::get_core().get_cursor_position());

    move_drag::drag_options_t drag_options;
    drag_options.initial_scale   = 1.0;
    drag_options.enable_snap_off = true;
    drag_options.snap_off_threshold = 20;
    drag_options.join_views = false;
    this->drag_helper->start_drag(grabbed_view, drag_options);
}

move_view_controller_t::~move_view_controller_t()
{}

void move_view_controller_t::input_motion()
{
    drag_helper->handle_motion(wf::get_core().get_cursor_position().round_down());
}

void move_view_controller_t::input_released(bool force_stop)
{
    drag_helper->handle_input_released();
}

wf::geometry_t eval(nonstd::observer_ptr<tree_node_t> node)
{
    return node ? node->geometry : wf::geometry_t{0, 0, 0, 0};
}

/* ----------------------- resize tile controller --------------------------- */
resize_view_controller_t::resize_view_controller_t(wf::workspace_set_t *wset, wayfire_toplevel_view)
{
    this->last_point   = get_global_input_coordinates(wset->get_attached_output());
    this->grabbed_view = find_view_at(get_root(wset, wset->get_current_workspace()), last_point);
    this->output = wset->get_attached_output();

    if (this->grabbed_view)
    {
        this->resizing_edges = calculate_resizing_edges(last_point);
        horizontal_pair = this->find_resizing_pair(true);
        vertical_pair   = this->find_resizing_pair(false);
    }
}

resize_view_controller_t::~resize_view_controller_t()
{}

uint32_t resize_view_controller_t::calculate_resizing_edges(wf::point_t grab)
{
    uint32_t result_edges = 0;
    auto window = this->grabbed_view->geometry;
    assert(window & grab);

    if (grab.x < window.x + window.width / 2)
    {
        result_edges |= WLR_EDGE_LEFT;
    } else
    {
        result_edges |= WLR_EDGE_RIGHT;
    }

    if (grab.y < window.y + window.height / 2)
    {
        result_edges |= WLR_EDGE_TOP;
    } else
    {
        result_edges |= WLR_EDGE_BOTTOM;
    }

    return result_edges;
}

resize_view_controller_t::resizing_pair_t resize_view_controller_t::find_resizing_pair(bool horiz)
{
    split_insertion_t direction;

    /* Calculate the direction in which we are looking for the resizing pair */
    if (horiz)
    {
        if (this->resizing_edges & WLR_EDGE_TOP)
        {
            direction = INSERT_ABOVE;
        } else
        {
            direction = INSERT_BELOW;
        }
    } else
    {
        if (this->resizing_edges & WLR_EDGE_LEFT)
        {
            direction = INSERT_LEFT;
        } else
        {
            direction = INSERT_RIGHT;
        }
    }

    /* Find a view in the resizing direction, then look for the least common
     * ancestor(LCA) of the grabbed view and the found view.
     *
     * Then the resizing pair is a pair of children of the LCA */
    auto pair_view =
        find_first_view_in_direction(this->grabbed_view, direction);

    if (!pair_view) // no pair
    {
        return {nullptr, grabbed_view};
    }

    /* Calculate all ancestors of the grabbed view */
    std::set<nonstd::observer_ptr<tree_node_t>> grabbed_view_ancestors;

    nonstd::observer_ptr<tree_node_t> ancestor = grabbed_view;
    while (ancestor)
    {
        grabbed_view_ancestors.insert(ancestor);
        ancestor = ancestor->parent;
    }

    /* Find the LCA: this is the first ancestor of the pair_view which is also
     * an ancestor of the grabbed view */
    nonstd::observer_ptr<tree_node_t> lca = pair_view;
    /* The child of lca we came from the second time */
    nonstd::observer_ptr<tree_node_t> lca_successor = nullptr;
    while (lca && !grabbed_view_ancestors.count({lca}))
    {
        lca_successor = lca;
        lca = lca->parent;
    }

    /* In the "worst" case, the root of the tree is an LCA.
     * Also, an LCA is a split because it is an ancestor of two different
     * view nodes */
    assert(lca && lca->children.size());

    resizing_pair_t result_pair;
    for (auto& child : lca->children)
    {
        if (grabbed_view_ancestors.count({child}))
        {
            result_pair.first = {child};
            break;
        }
    }

    result_pair.second = lca_successor;

    /* Make sure the first node in the resizing pair is always to the
     * left or above of the second one */
    if ((direction == INSERT_LEFT) || (direction == INSERT_ABOVE))
    {
        std::swap(result_pair.first, result_pair.second);
    }

    return result_pair;
}

void resize_view_controller_t::adjust_geometry(int32_t& x1, int32_t& len1,
    int32_t& x2, int32_t& len2, int32_t delta)
{
    /*
     * On the line:
     *
     * x1        (x1+len1)=x2         x2+len2-1
     * ._______________.___________________.
     */
    constexpr int MIN_SIZE = 50;

    int maxPositive = std::max(0, len2 - MIN_SIZE);
    int maxNegative = std::max(0, len1 - MIN_SIZE);

    /* Make sure we don't shrink one dimension too much */
    delta = clamp(delta, -maxNegative, maxPositive);

    /* Adjust sizes */
    len1 += delta;
    x2   += delta;
    len2 -= delta;
}

void resize_view_controller_t::input_motion()
{
    auto input = get_global_input_coordinates(output);
    if (!this->grabbed_view)
    {
        return;
    }

    auto tx = wf::txn::transaction_t::create();
    if (horizontal_pair.first && horizontal_pair.second)
    {
        int dy = input.y - last_point.y;

        auto g1 = horizontal_pair.first->geometry;
        auto g2 = horizontal_pair.second->geometry;

        adjust_geometry(g1.y, g1.height, g2.y, g2.height, dy);
        horizontal_pair.first->set_geometry(g1, tx);
        horizontal_pair.second->set_geometry(g2, tx);
    }

    if (vertical_pair.first && vertical_pair.second)
    {
        int dx = input.x - last_point.x;

        auto g1 = vertical_pair.first->geometry;
        auto g2 = vertical_pair.second->geometry;

        adjust_geometry(g1.x, g1.width, g2.x, g2.width, dx);
        vertical_pair.first->set_geometry(g1, tx);
        vertical_pair.second->set_geometry(g2, tx);
    }

    wf::get_core().tx_manager->schedule_transaction(std::move(tx));
    this->last_point = input;
}

wf::point_t get_global_input_coordinates(wf::output_t *output)
{
    wf::pointf_t local = output->get_cursor_position();

    auto vp   = output->wset()->get_current_workspace();
    auto size = output->get_screen_size();
    local.x += size.width * vp.x;
    local.y += size.height * vp.y;

    return {(int)local.x, (int)local.y};
}
} // namespace tile
}

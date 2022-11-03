#include "tree-controller.hpp"

#include <set>
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

/**
 * Calculate which view node is at the given position
 *
 * Returns null if no view nodes are present.
 */
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

/**
 * Calculate the position of the split that needs to be created if a view is
 * dropped at @input over @node
 *
 * @param sensitivity What percentage of the view is "active", i.e the threshold
 *                    for INSERT_NONE
 */
static split_insertion_t calculate_insert_type(
    nonstd::observer_ptr<tree_node_t> node, wf::point_t input, double sensitivity)
{
    auto window = node->geometry;

    if (!(window & input))
    {
        return INSERT_NONE;
    }

    /*
     * Calculate how much to the left, right, top and bottom of the window
     * our input is, then filter through the sensitivity.
     *
     * In the end, take the edge which is closest to input.
     */
    std::vector<std::pair<double, split_insertion_t>> edges;

    double px = 1.0 * (input.x - window.x) / window.width;
    double py = 1.0 * (input.y - window.y) / window.height;

    edges.push_back({px, INSERT_LEFT});
    edges.push_back({py, INSERT_ABOVE});
    edges.push_back({1.0 - px, INSERT_RIGHT});
    edges.push_back({1.0 - py, INSERT_BELOW});

    /* Remove edges that are too far away */
    auto it = std::remove_if(edges.begin(), edges.end(),
        [sensitivity] (auto pair)
    {
        return pair.first > sensitivity;
    });
    edges.erase(it, edges.end());

    if (edges.empty())
    {
        return INSERT_SWAP;
    }

    /* Return the closest edge */
    return std::min_element(edges.begin(), edges.end())->second;
}

/* By default, 1/3rd of the view can be dropped into */
static constexpr double SPLIT_PREVIEW_PERCENTAGE = 1.0 / 3.0;

/**
 * Calculate the position of the split that needs to be created if a view is
 * dropped at @input over @node
 */
split_insertion_t calculate_insert_type(
    nonstd::observer_ptr<tree_node_t> node, wf::point_t input)
{
    return calculate_insert_type(node, input, SPLIT_PREVIEW_PERCENTAGE);
}

/**
 * Calculate the bounds of the split preview
 */
wf::geometry_t calculate_split_preview(nonstd::observer_ptr<tree_node_t> over,
    split_insertion_t split_type)
{
    auto preview = over->geometry;
    switch (split_type)
    {
      case INSERT_RIGHT:
        preview.x += preview.width * (1 - SPLIT_PREVIEW_PERCENTAGE);

      // fallthrough
      case INSERT_LEFT:
        preview.width = preview.width * SPLIT_PREVIEW_PERCENTAGE;
        break;

      case INSERT_BELOW:
        preview.y += preview.height * (1 - SPLIT_PREVIEW_PERCENTAGE);

      // fallthrough
      case INSERT_ABOVE:
        preview.height = preview.height * SPLIT_PREVIEW_PERCENTAGE;
        break;

      default:
        break; // nothing to do
    }

    return preview;
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
move_view_controller_t::move_view_controller_t(
    std::unique_ptr<tree_node_t>& uroot, wf::point_t grab) :
    root(uroot)
{
    this->grabbed_view = find_view_at(root, grab);
    if (this->grabbed_view)
    {
        this->output = this->grabbed_view->view->get_output();
        this->current_input = grab;
    }
}

move_view_controller_t::~move_view_controller_t()
{
    if (this->preview)
    {
        this->preview->set_target_geometry(
            get_wset_local_coordinates(output->wset(), current_input), 0.0, true);
    }
}

nonstd::observer_ptr<view_node_t> move_view_controller_t::check_drop_destination(
    wf::point_t input)
{
    auto dropped_at = find_view_at(this->root, this->current_input);
    if (!dropped_at || (dropped_at == this->grabbed_view))
    {
        return nullptr;
    }

    return dropped_at;
}

void move_view_controller_t::ensure_preview(wf::point_t start)
{
    if (this->preview)
    {
        return;
    }

    auto view =
        std::make_unique<wf::preview_indication_view_t>(start, "simple-tile");
    this->preview = {view};
    wf::get_core().add_view(std::move(view));
    this->preview->set_output(output);
}

void move_view_controller_t::input_motion(wf::point_t input)
{
    if (!this->grabbed_view)
    {
        return;
    }

    this->current_input = input;
    auto view = check_drop_destination(input);
    if (!view)
    {
        /* No view, no preview */
        if (this->preview)
        {
            preview->set_target_geometry(get_wset_local_coordinates(output->wset(), input), 0.0);
        }

        return;
    }

    auto split = calculate_insert_type(view, input);
    ensure_preview(get_wset_local_coordinates(output->wset(), input));

    auto preview_geometry = calculate_split_preview(view, split);
    preview_geometry = get_wset_local_coordinates(output->wset(), preview_geometry);
    this->preview->set_target_geometry(preview_geometry, 1.0);
}

/**
 * Find the index of the view in its parent list
 */
static int find_idx(nonstd::observer_ptr<tree_node_t> view)
{
    auto& children = view->parent->children;
    auto it = std::find_if(children.begin(), children.end(),
        [=] (auto& node) { return node.get() == view.get(); });

    return it - children.begin();
}

void move_view_controller_t::input_released()
{
    auto dropped_at = check_drop_destination(this->current_input);
    if (!this->grabbed_view || !dropped_at)
    {
        return;
    }

    auto split = calculate_insert_type(dropped_at, current_input);
    if (split == INSERT_NONE)
    {
        return;
    }

    auto tx = wf::txn::transaction_t::create();

    if (split == INSERT_SWAP)
    {
        std::swap(grabbed_view->geometry, dropped_at->geometry);

        auto p1 = grabbed_view->parent;
        auto p2 = dropped_at->parent;
        grabbed_view->parent = p2;
        dropped_at->parent   = p1;

        auto it1 = std::find_if(p1->children.begin(), p1->children.end(),
            [&] (const auto& ptr) { return ptr.get() == grabbed_view.get(); });
        auto it2 = std::find_if(p2->children.begin(), p2->children.end(),
            [&] (const auto& ptr) { return ptr.get() == dropped_at.get(); });

        std::swap(*it1, *it2);

        p1->set_geometry(p1->geometry, tx);
        p2->set_geometry(p2->geometry, tx);
        return;
    }

    auto split_type = (split == INSERT_LEFT || split == INSERT_RIGHT) ?
        SPLIT_VERTICAL : SPLIT_HORIZONTAL;

    if (dropped_at->parent->get_split_direction() == split_type)
    {
        /* We can simply add the dragged view as a sibling of the target view */
        auto view = grabbed_view->parent->remove_child(grabbed_view, tx);

        int idx = find_idx(dropped_at);
        if ((split == INSERT_RIGHT) || (split == INSERT_BELOW))
        {
            ++idx;
        }

        dropped_at->parent->add_child(std::move(view), tx, idx);
    } else
    {
        /* Case 2: we need a new split just for the dropped on and the dragged
         * views */
        auto new_split = std::make_unique<split_node_t>(split_type);
        /* The size will be autodetermined by the tree structure, but we set
         * some valid size here to avoid UB */
        new_split->set_geometry(dropped_at->geometry, tx);

        /* Find the position of the dropped view and its parent */
        int idx = find_idx(dropped_at);
        auto dropped_parent = dropped_at->parent;

        /* Remove both views */
        auto dropped_view = dropped_at->parent->remove_child(dropped_at, tx);
        auto dragged_view = grabbed_view->parent->remove_child(grabbed_view, tx);

        if ((split == INSERT_ABOVE) || (split == INSERT_LEFT))
        {
            new_split->add_child(std::move(dragged_view), tx);
            new_split->add_child(std::move(dropped_view), tx);
        } else
        {
            new_split->add_child(std::move(dropped_view), tx);
            new_split->add_child(std::move(dragged_view), tx);
        }

        /* Put them in place */
        dropped_parent->add_child(std::move(new_split), tx, idx);
    }

    /* Clean up tree structure */
    flatten_tree(this->root, tx);
    wf::get_core().tx_manager->schedule_transaction(std::move(tx));
}

wf::geometry_t eval(nonstd::observer_ptr<tree_node_t> node)
{
    return node ? node->geometry : wf::geometry_t{0, 0, 0, 0};
}

/* ----------------------- resize tile controller --------------------------- */
resize_view_controller_t::resize_view_controller_t(
    std::unique_ptr<tree_node_t>& uroot, wf::point_t grab) :
    root(uroot)
{
    this->grabbed_view = find_view_at(root, grab);
    this->last_point   = grab;

    if (this->grabbed_view)
    {
        this->resizing_edges = calculate_resizing_edges(grab);
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

void resize_view_controller_t::input_motion(wf::point_t input)
{
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
}
}

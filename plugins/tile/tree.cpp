#include "tree.hpp"
#include "wayfire/core.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/toplevel-view.hpp"
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>

#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/plugins/crossfade.hpp>
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/window-manager.hpp>

namespace wf
{
namespace tile
{
void tree_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr&)
{
    this->geometry = geometry;
}

nonstd::observer_ptr<split_node_t> tree_node_t::as_split_node()
{
    return nonstd::make_observer(dynamic_cast<split_node_t*>(this));
}

nonstd::observer_ptr<view_node_t> tree_node_t::as_view_node()
{
    return nonstd::make_observer(dynamic_cast<view_node_t*>(this));
}

wf::point_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::point_t p)
{
    auto vp   = wset->get_current_workspace();
    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);
    p.x -= vp.x * size.width;
    p.y -= vp.y * size.height;
    return p;
}

wf::geometry_t get_wset_local_coordinates(std::shared_ptr<wf::workspace_set_t> wset, wf::geometry_t g)
{
    auto new_tl = get_wset_local_coordinates(wset, wf::point_t{g.x, g.y});
    g.x = new_tl.x;
    g.y = new_tl.y;

    return g;
}

/* ---------------------- split_node_t implementation ----------------------- */
wf::geometry_t split_node_t::get_child_geometry(
    int32_t child_pos, int32_t child_size)
{
    wf::geometry_t child_geometry = this->geometry;
    switch (get_split_direction())
    {
      case SPLIT_HORIZONTAL:
        child_geometry.y += child_pos;
        child_geometry.height = child_size;
        break;

      case SPLIT_VERTICAL:
        child_geometry.x    += child_pos;
        child_geometry.width = child_size;
        break;
    }

    return child_geometry;
}

int32_t split_node_t::calculate_splittable(wf::geometry_t available) const
{
    switch (get_split_direction())
    {
      case SPLIT_HORIZONTAL:
        return available.height;

      case SPLIT_VERTICAL:
        return available.width;
    }

    return -1;
}

int32_t split_node_t::calculate_splittable() const
{
    return calculate_splittable(this->geometry);
}

void split_node_t::recalculate_children(wf::geometry_t available, wf::txn::transaction_uptr& tx)
{
    if (this->children.empty())
    {
        return;
    }

    double old_child_sum = 0.0;
    for (auto& child : this->children)
    {
        old_child_sum += calculate_splittable(child->geometry);
    }

    int32_t total_splittable = calculate_splittable(available);

    /* Sum of children sizes up to now */
    double up_to_now = 0.0;

    auto progress = [=] (double current)
    {
        return (current / old_child_sum) * total_splittable;
    };

    set_gaps(this->gaps, tx);

    /* For each child, assign its percentage of the whole. */
    for (auto& child : this->children)
    {
        /* Calculate child_start/end every time using the percentage from the
         * beginning. This way we avoid rounding errors causing empty spaces */
        int32_t child_start = progress(up_to_now);
        up_to_now += calculate_splittable(child->geometry);
        int32_t child_end = progress(up_to_now);

        /* Set new size */
        int32_t child_size = child_end - child_start;
        child->set_geometry(get_child_geometry(child_start, child_size), tx);
    }
}

void split_node_t::add_child(std::unique_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx, int index)
{
    /*
     * Strategy:
     * Calculate the size of the new child relative to the old children, so
     * that proportions are right. After that, rescale all nodes.
     */
    int num_children = this->children.size();

    /* Calculate where the new child should be, in current proportions */
    int size_new_child;
    if (num_children > 0)
    {
        size_new_child =
            (calculate_splittable() + num_children - 1) / num_children;
    } else
    {
        size_new_child = calculate_splittable();
    }

    if ((index == -1) || (index > num_children))
    {
        index = num_children;
    }

    /* Add child to the list */
    child->parent = {this};

    // Set size of the child to make sure it gets properly recalculated later
    child->geometry = get_child_geometry(0, size_new_child);

    this->children.emplace(this->children.begin() + index, std::move(child));

    set_gaps(this->gaps, tx);

    /* Recalculate geometry */
    recalculate_children(geometry, tx);
}

std::unique_ptr<tree_node_t> split_node_t::remove_child(
    nonstd::observer_ptr<tree_node_t> child, wf::txn::transaction_uptr& tx)
{
    /* Remove child */
    std::unique_ptr<tree_node_t> result;
    auto it = this->children.begin();

    while (it != this->children.end())
    {
        if (it->get() == child.get())
        {
            result = std::move(*it);
            it     = this->children.erase(it);
        } else
        {
            ++it;
        }
    }

    /* Remaining children have the full geometry */
    recalculate_children(this->geometry, tx);
    result->parent = nullptr;

    return result;
}

void split_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx)
{
    tree_node_t::set_geometry(geometry, tx);
    recalculate_children(geometry, tx);
}

void split_node_t::set_gaps(const gap_size_t& gaps, wf::txn::transaction_uptr& tx)
{
    this->gaps = gaps;
    for (const auto& child : this->children)
    {
        gap_size_t child_gaps = gaps;

        /* See which edges are modified by this split */
        int32_t *first_edge, *second_edge;
        switch (this->split_direction)
        {
          case SPLIT_HORIZONTAL:
            first_edge  = &child_gaps.top;
            second_edge = &child_gaps.bottom;
            break;

          case SPLIT_VERTICAL:
            first_edge  = &child_gaps.left;
            second_edge = &child_gaps.right;
            break;

          default:
            assert(false);
        }

        /* Override internal edges */
        if (child != this->children.front())
        {
            *first_edge = gaps.internal;
        }

        if (child != this->children.back())
        {
            *second_edge = gaps.internal;
        }

        child->set_gaps(child_gaps, tx);
    }
}

split_direction_t split_node_t::get_split_direction() const
{
    return this->split_direction;
}

split_node_t::split_node_t(split_direction_t dir)
{
    this->split_direction = dir;
    this->geometry = {0, 0, 0, 0};
}

/* -------------------- view_node_t implementation -------------------------- */
struct view_node_custom_data_t : public custom_data_t
{
    nonstd::observer_ptr<view_node_t> ptr;
    view_node_custom_data_t(view_node_t *node)
    {
        ptr = nonstd::make_observer(node);
    }
};

/**
 * A simple transformer to scale and translate the view in such a way that
 * its displayed wm geometry region is a specified box on the screen
 */
static const std::string scale_transformer_name =
    "simple-tile-scale-transformer";
struct view_node_t::scale_transformer_t : public wf::scene::view_2d_transformer_t
{
    wf::geometry_t box;

    scale_transformer_t(wayfire_toplevel_view view, wf::geometry_t box) :
        wf::scene::view_2d_transformer_t(view)
    {
        set_box(box);
    }

    void set_box(wf::geometry_t box)
    {
        assert(box.width > 0 && box.height > 0);

        this->view->damage();

        auto current = toplevel_cast(this->view)->get_geometry();
        if ((current.width <= 0) || (current.height <= 0))
        {
            /* view possibly unmapped?? */
            return;
        }

        double scale_horiz = 1.0 * box.width / current.width;
        double scale_vert  = 1.0 * box.height / current.height;

        /* Position of top-left corner after scaling */
        double scaled_x = current.x + (current.width / 2.0 * (1 - scale_horiz));
        double scaled_y = current.y + (current.height / 2.0 * (1 - scale_vert));

        this->scale_x = scale_horiz;
        this->scale_y = scale_vert;
        this->translation_x = box.x - scaled_x;
        this->translation_y = box.y - scaled_y;
    }
};

/**
 * A class for animating the view, emits a signal when the animation is over.
 */
class tile_view_animation_t : public wf::grid::grid_animation_t
{
  public:
    using wf::grid::grid_animation_t::grid_animation_t;

    ~tile_view_animation_t()
    {
        // The grid animation does this too, however, we want to remove the
        // transformer so that we can enforce the correct geometry from the
        // start.
        view->get_transformed_node()->rem_transformer<grid::crossfade_node_t>();

        tile_adjust_transformer_signal ev;
        view->emit(&ev);
    }

    tile_view_animation_t(const tile_view_animation_t &) = delete;
    tile_view_animation_t(tile_view_animation_t &&) = delete;
    tile_view_animation_t& operator =(const tile_view_animation_t&) = delete;
    tile_view_animation_t& operator =(tile_view_animation_t&&) = delete;
};

view_node_t::view_node_t(wayfire_toplevel_view view)
{
    this->view = view;
    LOGI("We store data??");
    view->store_data(std::make_unique<view_node_custom_data_t>(this));

    this->on_geometry_changed.set_callback([=] (auto)
    {
        update_transformer();
    });
    on_adjust_transformer.set_callback([=] (auto)
    {
        update_transformer();
    });

    view->connect(&on_geometry_changed);
    view->connect(&on_adjust_transformer);
}

view_node_t::~view_node_t()
{
    view->get_transformed_node()->rem_transformer(scale_transformer_name);
    view->erase_data<view_node_custom_data_t>();
}

void view_node_t::set_gaps(const gap_size_t& size, wf::txn::transaction_uptr& tx)
{
    if ((this->gaps.top != size.top) ||
        (this->gaps.bottom != size.bottom) ||
        (this->gaps.left != size.left) ||
        (this->gaps.right != size.right))
    {
        this->gaps = size;
    }
}

wf::geometry_t view_node_t::calculate_target_geometry()
{
    /* Calculate view geometry in coordinates local to the active workspace,
     * because tree coordinates are kept in workspace-agnostic coordinates. */
    auto wset = view->get_wset();
    auto local_geometry = get_wset_local_coordinates(wset, geometry);

    local_geometry.x     += gaps.left;
    local_geometry.y     += gaps.top;
    local_geometry.width -= gaps.left + gaps.right;
    local_geometry.height -= gaps.top + gaps.bottom;

    auto size = wset->get_last_output_geometry().value_or(default_output_resolution);
    /* If view is maximized, we want to use the full available geometry */
    if (view->pending_fullscreen())
    {
        auto vp = wset->get_current_workspace();
        int view_vp_x = std::floor(1.0 * geometry.x / size.width);
        int view_vp_y = std::floor(1.0 * geometry.y / size.height);

        local_geometry = {
            (view_vp_x - vp.x) * size.width,
            (view_vp_y - vp.y) * size.height,
            size.width,
            size.height,
        };
    }

    if (view->sticky)
    {
        local_geometry.x = (local_geometry.x % size.width + size.width) % size.width;
        local_geometry.y = (local_geometry.y % size.height + size.height) % size.height;
    }

    return local_geometry;
}

bool view_node_t::needs_crossfade()
{
    if (animation_duration.value().length_ms == 0)
    {
        return false;
    }

    if (view->has_data<wf::grid::grid_animation_t>())
    {
        return true;
    }

    if (view->get_output()->is_plugin_active("simple-tile"))
    {
        // Disable animations while controllers are active
        return false;
    }

    return true;
}

static nonstd::observer_ptr<wf::grid::grid_animation_t> ensure_animation(
    wayfire_toplevel_view view, wf::option_sptr_t<wf::animation_description_t> duration)
{
    if (!view->has_data<wf::grid::grid_animation_t>())
    {
        const auto type = wf::grid::grid_animation_t::CROSSFADE;
        view->store_data<wf::grid::grid_animation_t>(
            std::make_unique<tile_view_animation_t>(view, type, duration));
    }

    return view->get_data<wf::grid::grid_animation_t>();
}

void view_node_t::set_geometry(wf::geometry_t geometry, wf::txn::transaction_uptr& tx)
{
    tree_node_t::set_geometry(geometry, tx);

    if (!view->is_mapped())
    {
        return;
    }

    wf::get_core().default_wm->update_last_windowed_geometry(view);
    view->toplevel()->pending().tiled_edges = TILED_EDGES_ALL;
    tx->add_object(view->toplevel());

    auto target = calculate_target_geometry();
    if (this->needs_crossfade() && (target != view->get_geometry()))
    {
        view->get_transformed_node()->rem_transformer(scale_transformer_name);
        ensure_animation(view, animation_duration)
        ->adjust_target_geometry(target, -1, tx);
    } else
    {
        view->toplevel()->pending().geometry = target;
        tx->add_object(view->toplevel());
    }
}

void view_node_t::update_transformer()
{
    auto target_geometry = calculate_target_geometry();
    if ((target_geometry.width <= 0) || (target_geometry.height <= 0))
    {
        return;
    }

    if (view->has_data<wf::grid::grid_animation_t>())
    {
        // Still animating
        return;
    }

    auto wm = view->get_geometry();
    if (wm != target_geometry)
    {
        auto tr = ensure_named_transformer<scale_transformer_t>(view,
            wf::TRANSFORMER_2D, scale_transformer_name, view, target_geometry);
        tr->set_box(target_geometry);
    } else
    {
        view->get_transformed_node()->rem_transformer(scale_transformer_name);
    }
}

nonstd::observer_ptr<view_node_t> view_node_t::get_node(wayfire_view view)
{
    if (!view->has_data<view_node_custom_data_t>())
    {
        return nullptr;
    }

    return view->get_data<view_node_custom_data_t>()->ptr;
}

/* ----------------- Generic tree operations implementation ----------------- */
void flatten_tree(std::unique_ptr<tree_node_t>& root, txn::transaction_uptr& tx)
{
    /* Cannot flatten a view node */
    if (root->as_view_node())
    {
        return;
    }

    /* No flattening required on this level */
    if (root->children.size() >= 2)
    {
        for (auto& child : root->children)
        {
            flatten_tree(child, tx);
        }

        return;
    }

    /* Only the real root of the tree can have no children */
    assert(!root->parent || root->children.size());

    if (root->children.empty())
    {
        return;
    }

    nonstd::observer_ptr<tree_node_t> child_ptr = {root->children.front()};

    /* A single view child => cannot make it root */
    if (child_ptr->as_view_node())
    {
        if (!root->parent)
        {
            return;
        }
    }

    /* Rewire the tree, skipping the current root */
    auto child = root->as_split_node()->remove_child(child_ptr, tx);
    child->parent = root->parent;
    root = std::move(child); // overwrite root with the child
}

nonstd::observer_ptr<split_node_t> get_root(
    nonstd::observer_ptr<tree_node_t> node)
{
    if (!node->parent)
    {
        return {dynamic_cast<split_node_t*>(node.get())};
    }

    return get_root(node->parent);
}
}
}

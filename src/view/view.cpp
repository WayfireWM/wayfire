#include <memory>
#include <wayfire/util/log.hpp>
#include "../core/core-impl.hpp"
#include "view-impl.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/render-manager.hpp"
#include "xdg-shell.hpp"
#include "../core/seat/input-manager.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include "wayfire/signal-definitions.hpp"
#include <wayfire/scene-operations.hpp>

static void reposition_relative_to_parent(wayfire_view view)
{
    if (!view->parent)
    {
        return;
    }

    auto parent_geometry = view->parent->get_wm_geometry();
    auto wm_geometry     = view->get_wm_geometry();
    auto scr_size = view->get_output()->get_screen_size();
    // Guess which workspace the parent is on
    wf::point_t center = {
        parent_geometry.x + parent_geometry.width / 2,
        parent_geometry.y + parent_geometry.height / 2,
    };
    wf::point_t parent_ws = {
        (int)std::floor(1.0 * center.x / scr_size.width),
        (int)std::floor(1.0 * center.y / scr_size.height),
    };

    auto workarea = view->get_output()->render->get_ws_box(
        view->get_output()->workspace->get_current_workspace() + parent_ws);
    if (view->parent->is_mapped())
    {
        auto parent_g = view->parent->get_wm_geometry();
        wm_geometry.x = parent_g.x + (parent_g.width - wm_geometry.width) / 2;
        wm_geometry.y = parent_g.y + (parent_g.height - wm_geometry.height) / 2;
    } else
    {
        /* if we have a parent which still isn't mapped, we cannot determine
         * the view's position, so we center it on the screen */
        wm_geometry.x = workarea.width / 2 - wm_geometry.width / 2;
        wm_geometry.y = workarea.height / 2 - wm_geometry.height / 2;
    }

    /* make sure view is visible afterwards */
    wm_geometry = wf::clamp(wm_geometry, workarea);
    view->move(wm_geometry.x, wm_geometry.y);
    if ((wm_geometry.width != view->get_wm_geometry().width) ||
        (wm_geometry.height != view->get_wm_geometry().height))
    {
        view->resize(wm_geometry.width, wm_geometry.height);
    }
}

static void unset_toplevel_parent(wayfire_view view)
{
    if (view->parent)
    {
        auto& container = view->parent->children;
        auto it = std::remove(container.begin(), container.end(), view);
        container.erase(it, container.end());
        wf::scene::remove_child(view->get_root_node());
    }
}

static wayfire_view find_toplevel_parent(wayfire_view view)
{
    while (view->parent)
    {
        view = view->parent;
    }

    return view;
}

/**
 * Check whether the toplevel parent needs refocus.
 * This may be needed because when focusing a view, its topmost child is given
 * keyboard focus. When the parent-child relations change, it may happen that
 * the parent needs to be focused again, this time with a different keyboard
 * focus surface.
 */
static void check_refocus_parent(wayfire_view view)
{
    view = find_toplevel_parent(view);
    if (view->get_output() && (view->get_output()->get_active_view() == view))
    {
        view->get_output()->focus_view(view, false);
    }
}

void wf::view_interface_t::set_toplevel_parent(wayfire_view new_parent)
{
    auto old_parent = parent;
    if (parent != new_parent)
    {
        /* Erase from the old parent */
        unset_toplevel_parent(self());

        /* Add in the list of the new parent */
        if (new_parent)
        {
            new_parent->children.insert(new_parent->children.begin(), self());
        }

        parent = new_parent;
        view_parent_changed_signal ev;
        this->emit(&ev);
    }

    if (parent)
    {
        /* Make sure the view is available only as a child */
        if (this->get_output())
        {
            this->get_output()->workspace->remove_view(self());
        }

        this->set_output(parent->get_output());
        /* if the view isn't mapped, then it will be positioned properly in map() */
        if (is_mapped())
        {
            reposition_relative_to_parent(self());
        }

        wf::scene::readd_front(parent->get_root_node(), this->get_root_node());
        check_refocus_parent(parent);
    } else if (old_parent)
    {
        /* At this point, we are a regular view. */
        if (this->get_output())
        {
            wf::scene::readd_front(get_output()->get_wset(), get_root_node());
            get_output()->workspace->add_view(self());
            check_refocus_parent(old_parent);
        }
    }
}

std::vector<wayfire_view> wf::view_interface_t::enumerate_views(
    bool mapped_only)
{
    if (!this->is_mapped() && mapped_only)
    {
        return {};
    }

    std::vector<wayfire_view> result;
    result.reserve(priv->last_view_cnt);
    for (auto& v : this->children)
    {
        auto cdr = v->enumerate_views(mapped_only);
        result.insert(result.end(), cdr.begin(), cdr.end());
    }

    result.push_back(self());
    priv->last_view_cnt = result.size();

    return result;
}

void wf::view_interface_t::set_role(view_role_t new_role)
{
    role = new_role;
    damage();
}

std::string wf::view_interface_t::to_string() const
{
    return "view-" + wf::object_base_t::to_string();
}

wayfire_view wf::view_interface_t::self()
{
    return wayfire_view(this);
}

/** Set the view's output. */
void wf::view_interface_t::set_output(wf::output_t *new_output)
{
    /* Make sure the view doesn't stay on the old output */
    if (get_output() && (get_output() != new_output))
    {
        view_disappeared_signal data_disappeared;
        data_disappeared.view = self();
        get_output()->emit(&data_disappeared);
    }

    view_set_output_signal data;
    data.view   = self();
    data.output = get_output();

    this->priv->output = new_output;

    this->emit(&data);
    if (new_output)
    {
        new_output->emit(&data);
    }

    wf::get_core().emit(&data);

    for (auto& view : this->children)
    {
        view->set_output(new_output);
    }
}

wf::output_t*wf::view_interface_t::get_output()
{
    return priv->output;
}

void wf::view_interface_t::resize(int w, int h)
{
    /* no-op */
}

void wf::view_interface_t::set_geometry(wf::geometry_t g)
{
    move(g.x, g.y);
    resize(g.width, g.height);
}

void wf::view_interface_t::set_resizing(bool resizing, uint32_t edges)
{
    priv->update_windowed_geometry(self(), get_wm_geometry());
    /* edges are reset on the next commit */
    if (resizing)
    {
        this->priv->edges = edges;
    }

    auto& in_resize = this->priv->in_continuous_resize;
    in_resize += resizing ? 1 : -1;

    if (in_resize < 0)
    {
        LOGE("in_continuous_resize counter dropped below 0!");
    }
}

void wf::view_interface_t::set_moving(bool moving)
{
    priv->update_windowed_geometry(self(), get_wm_geometry());
    auto& in_move = this->priv->in_continuous_move;

    in_move += moving ? 1 : -1;
    if (in_move < 0)
    {
        LOGE("in_continuous_move counter dropped below 0!");
    }
}

void wf::view_interface_t::request_native_size()
{
    /* no-op */
}

void wf::view_interface_t::ping()
{
    // Do nothing, specialized in the various shells
}

void wf::view_interface_t::close()
{
    /* no-op */
}

wf::geometry_t wf::view_interface_t::get_wm_geometry()
{
    return get_output_geometry();
}

wlr_box wf::view_interface_t::get_bounding_box()
{
    return get_transformed_node()->get_bounding_box();
}

bool wf::view_interface_t::is_focusable() const
{
    return priv->keyboard_focus_enabled;
}

void wf::view_interface_t::set_minimized(bool minim)
{
    this->minimized = minim;
    view_minimized_signal data;
    data.view = self();
    this->emit(&data);
    get_output()->emit(&data);

    if (pending_minimized == minim)
    {
        return;
    }

    this->pending_minimized = minim;
    minimized = minim;
    if (minimized)
    {
        view_disappeared_signal data;
        data.view = self();
        get_output()->emit(&data);
        wf::scene::set_node_enabled(get_root_node(), false);
    } else
    {
        wf::scene::set_node_enabled(get_root_node(), true);
        get_output()->focus_view(self(), true);
    }
}

void wf::view_interface_t::set_sticky(bool sticky)
{
    if (this->sticky == sticky)
    {
        return;
    }

    damage();
    this->sticky = sticky;
    damage();

    wf::view_set_sticky_signal data;
    data.view = self();

    this->emit(&data);
    if (this->get_output())
    {
        this->get_output()->emit(&data);
    }
}

void wf::view_interface_t::set_tiled(uint32_t edges)
{
    if (edges)
    {
        priv->update_windowed_geometry(self(), get_wm_geometry());
    }

    wf::view_tiled_signal data;
    data.view = self();
    data.old_edges = this->tiled_edges;
    data.new_edges = edges;

    this->tiled_edges = edges;
    this->emit(&data);
    if (this->get_output())
    {
        get_output()->emit(&data);
    }
}

void wf::view_interface_t::set_fullscreen(bool full)
{
    /* When fullscreening a view, we want to store the last geometry it had
     * before getting fullscreen so that we can restore to it */
    if (full && !fullscreen)
    {
        priv->update_windowed_geometry(self(), get_wm_geometry());
    }

    fullscreen = full;

    view_fullscreen_signal data;
    data.view  = self();
    data.state = full;
    if (get_output())
    {
        get_output()->emit(&data);
    }

    this->emit(&data);
}

void wf::view_interface_t::set_activated(bool active)
{
    activated = active;
    view_activated_state_signal ev;
    this->emit(&ev);
}

void wf::view_interface_t::move_request()
{
    view_move_request_signal data;
    data.view = self();
    get_output()->emit(&data);
}

void wf::view_interface_t::focus_request()
{
    if (get_output())
    {
        view_focus_request_signal data;
        data.view = self();
        data.self_request = false;

        emit(&data);
        wf::get_core().emit(&data);
        if (!data.carried_out)
        {
            wf::get_core().focus_output(get_output());
            get_output()->ensure_visible(self());
            get_output()->focus_view(self(), true);
        }
    }
}

void wf::view_interface_t::resize_request(uint32_t edges)
{
    view_resize_request_signal data;
    data.view  = self();
    data.edges = edges;
    get_output()->emit(&data);
}

void wf::view_interface_t::tile_request(uint32_t edges)
{
    if (get_output())
    {
        tile_request(edges, get_output()->workspace->get_current_workspace());
    }
}

/**
 * Put a view on the given workspace.
 */
static void move_to_workspace(wf::view_interface_t *view, wf::point_t workspace)
{
    auto output = view->get_output();
    auto wm_geometry = view->get_wm_geometry();
    auto delta    = workspace - output->workspace->get_current_workspace();
    auto scr_size = output->get_screen_size();

    wm_geometry.x += scr_size.width * delta.x;
    wm_geometry.y += scr_size.height * delta.y;
    view->move(wm_geometry.x, wm_geometry.y);
}

void wf::view_interface_t::view_priv_impl::update_windowed_geometry(
    wayfire_view self, wf::geometry_t geometry)
{
    if (!self->is_mapped() || self->tiled_edges || this->in_continuous_move ||
        this->in_continuous_resize)
    {
        return;
    }

    this->last_windowed_geometry = geometry;
    if (self->get_output())
    {
        this->windowed_geometry_workarea =
            self->get_output()->workarea->get_workarea();
    } else
    {
        this->windowed_geometry_workarea = {0, 0, -1, -1};
    }
}

wf::geometry_t wf::view_interface_t::view_priv_impl::calculate_windowed_geometry(
    wf::output_t *output)
{
    if (!output || (windowed_geometry_workarea.width <= 0))
    {
        return last_windowed_geometry;
    }

    const auto& geom     = last_windowed_geometry;
    const auto& old_area = windowed_geometry_workarea;
    const auto& new_area = output->workarea->get_workarea();
    return {
        .x = new_area.x + (geom.x - old_area.x) * new_area.width /
            old_area.width,
        .y = new_area.y + (geom.y - old_area.y) * new_area.height /
            old_area.height,
        .width  = geom.width * new_area.width / old_area.width,
        .height = geom.height * new_area.height / old_area.height
    };
}

void wf::view_interface_t::tile_request(uint32_t edges, wf::point_t workspace)
{
    if (fullscreen || !get_output())
    {
        return;
    }

    view_tile_request_signal data;
    data.view  = self();
    data.edges = edges;
    data.workspace    = workspace;
    data.desired_size = edges ? get_output()->workarea->get_workarea() :
        priv->calculate_windowed_geometry(get_output());

    set_tiled(edges);
    if (is_mapped())
    {
        get_output()->emit(&data);
    }

    if (!data.carried_out)
    {
        if (data.desired_size.width > 0)
        {
            set_geometry(data.desired_size);
        } else
        {
            request_native_size();
        }

        move_to_workspace(this, workspace);
    }
}

void wf::view_interface_t::minimize_request(bool state)
{
    if ((state == minimized) || !is_mapped())
    {
        return;
    }

    view_minimize_request_signal data;
    data.view  = self();
    data.state = state;

    if (is_mapped())
    {
        get_output()->emit(&data);
        /* Some plugin (e.g animate) will take care of the request, so we need
         * to just send proper state to foreign-toplevel clients */
        if (data.carried_out)
        {
            minimized = state;
            get_output()->refocus();
        } else
        {
            /* Do the default minimization */
            set_minimized(state);
        }
    }
}

void wf::view_interface_t::fullscreen_request(wf::output_t *out, bool state)
{
    auto wo = (out ?: (get_output() ?: wf::get_core().get_active_output()));
    if (wo)
    {
        fullscreen_request(wo, state,
            wo->workspace->get_current_workspace());
    }
}

void wf::view_interface_t::fullscreen_request(wf::output_t *out, bool state,
    wf::point_t workspace)
{
    auto wo = (out ?: (get_output() ?: wf::get_core().get_active_output()));
    assert(wo);

    /* TODO: what happens if the view is moved to the other output, but not
     * fullscreened? We should make sure that it stays visible there */
    if (get_output() != wo)
    {
        wf::get_core().move_view_to_output(self(), wo, false);
    }

    view_fullscreen_request_signal data;
    data.view  = self();
    data.state = state;
    data.workspace    = workspace;
    data.desired_size = get_output()->get_relative_geometry();

    if (!state)
    {
        data.desired_size = this->tiled_edges ?
            this->get_output()->workarea->get_workarea() :
            this->priv->calculate_windowed_geometry(get_output());
    }

    set_fullscreen(state);
    if (is_mapped())
    {
        wo->emit(&data);
    }

    if (!data.carried_out)
    {
        if (data.desired_size.width > 0)
        {
            set_geometry(data.desired_size);
        } else
        {
            request_native_size();
        }

        move_to_workspace(this, workspace);
    }
}

void wf::view_interface_t::damage()
{
    auto bbox = get_untransformed_bounding_box();
    view_damage_raw(self(), bbox);
}

wlr_box wf::view_interface_t::get_minimize_hint()
{
    return this->priv->minimize_hint;
}

void wf::view_interface_t::set_minimize_hint(wlr_box hint)
{
    this->priv->minimize_hint = hint;
}

bool wf::view_interface_t::should_be_decorated()
{
    return false;
}

nonstd::observer_ptr<wf::decorator_frame_t_t> wf::view_interface_t::get_decoration()
{
    return this->priv->frame.get();
}

void wf::view_interface_t::set_decoration(
    std::unique_ptr<wf::decorator_frame_t_t> frame)
{
    if (!frame)
    {
        damage();

        // Take wm geometry as it was with the decoration.
        const auto wm = get_wm_geometry();

        // Drop the owned frame.
        priv->frame = nullptr;

        // Grow the tiled view to fill its old expanded geometry that included
        // the decoration.
        if (!fullscreen && this->tiled_edges && (wm != get_wm_geometry()))
        {
            set_geometry(wm);
        }

        view_decoration_changed_signal data;
        data.view = self();
        emit(&data);
        return;
    }

    // Take wm geometry as it was before adding the frame */
    auto wm = get_wm_geometry();

    damage();
    // Drop the old frame if any and assign the new one.
    priv->frame = std::move(frame);

    /* Calculate the wm geometry of the view after adding the decoration.
     *
     * If the view is neither maximized nor fullscreen, then we want to expand
     * the view geometry so that the actual view contents retain their size.
     *
     * For fullscreen and maximized views we want to "shrink" the view contents
     * so that the total wm geometry remains the same as before. */
    wf::geometry_t target_wm_geometry;
    if (!fullscreen && !this->tiled_edges)
    {
        target_wm_geometry = priv->frame->expand_wm_geometry(wm);
        // make sure that the view doesn't go outside of the screen or such
        auto wa = get_output()->workarea->get_workarea();
        auto visible = wf::geometry_intersection(target_wm_geometry, wa);
        if (visible != target_wm_geometry)
        {
            target_wm_geometry.x = wm.x;
            target_wm_geometry.y = wm.y;
        }
    } else if (fullscreen)
    {
        target_wm_geometry = get_output()->get_relative_geometry();
    } else if (this->tiled_edges)
    {
        target_wm_geometry = get_output()->workarea->get_workarea();
    }

    set_geometry(target_wm_geometry);
    damage();

    view_decoration_changed_signal ev;
    ev.view = self();
    emit(&ev);
}

bool wf::view_interface_t::has_transformer()
{
    auto ch = get_transformed_node()->get_children();
    return !ch.empty() && ch.front() != get_surface_root_node();
}

wf::geometry_t wf::view_interface_t::get_untransformed_bounding_box()
{
    return get_surface_root_node()->get_bounding_box();
}

const wf::render_target_t& wf::view_interface_t::take_snapshot()
{
    if (!is_mapped())
    {
        return priv->offscreen_buffer;
    }

    wf::render_target_t& offscreen_buffer = priv->offscreen_buffer;
    auto root_node = get_surface_root_node();
    const wf::geometry_t bbox = root_node->get_bounding_box();
    float scale = get_output()->handle->scale;

    OpenGL::render_begin();
    offscreen_buffer.allocate(bbox.width * scale, bbox.height * scale);
    OpenGL::render_end();

    offscreen_buffer.geometry = root_node->get_bounding_box();
    offscreen_buffer.scale    = scale;

    std::vector<scene::render_instance_uptr> instances;
    root_node->gen_render_instances(instances, [] (auto) {}, get_output());

    scene::render_pass_params_t params;
    params.background_color = {0, 0, 0, 0};
    params.damage    = bbox;
    params.target    = offscreen_buffer;
    params.instances = &instances;

    scene::run_render_pass(params, scene::RPASS_CLEAR_BACKGROUND);
    return offscreen_buffer;
}

wf::view_interface_t::view_interface_t()
{
    this->priv = std::make_unique<wf::view_interface_t::view_priv_impl>();
    take_ref();
}

void wf::view_interface_t::set_surface_root_node(scene::floating_inner_ptr surface_root_node)
{
    this->priv->surface_root_node = surface_root_node;
}

void wf::view_interface_t::take_ref()
{
    ++priv->ref_cnt;
}

void wf::view_interface_t::unref()
{
    --priv->ref_cnt;
    if (priv->ref_cnt <= 0)
    {
        destruct();
    }
}

class view_root_node_t : public wf::scene::floating_inner_node_t, public wf::view_node_tag_t
{
  public:
    view_root_node_t(wf::view_interface_t *_view) : floating_inner_node_t(false), view(_view)
    {
        view->connect(&on_destruct);
    }

    wayfire_view get_view() const override
    {
        return view;
    }

    std::string stringify() const override
    {
        if (view)
        {
            std::ostringstream out;
            out << this->view->self();
            return "view-root-node of " + out.str() + " " + stringify_flags();
        } else
        {
            return "inert view-root-node " + stringify_flags();
        }
    }

  private:
    wf::view_interface_t *view;
    wf::signal::connection_t<wf::view_destruct_signal> on_destruct = [=] (wf::view_destruct_signal *ev)
    {
        view = nullptr;
    };
};

void wf::view_interface_t::initialize()
{
    priv->root_node = std::make_shared<view_root_node_t>(this);
    priv->transformed_node = std::make_shared<scene::transform_manager_node_t>();

    if (!priv->surface_root_node)
    {
        priv->surface_root_node = std::make_shared<scene::view_node_t>(this);
    }

    // Set up view content to scene.
    priv->transformed_node->set_children_list({priv->surface_root_node});
    priv->root_node->set_children_list({priv->transformed_node});
    priv->root_node->set_enabled(false);
}

void wf::view_interface_t::deinitialize()
{
    auto children = this->children;
    for (auto ch : children)
    {
        ch->set_toplevel_parent(nullptr);
    }

    set_decoration(nullptr);
    this->_clear_data();

    OpenGL::render_begin();
    this->priv->offscreen_buffer.release();
    OpenGL::render_end();
}

wf::view_interface_t::~view_interface_t()
{
    /* Note: at this point, it is invalid to call most functions */
    unset_toplevel_parent(self());
}

void wf::view_damage_raw(wayfire_view view, const wlr_box& box)
{
    auto output = view->get_output();
    if (!output)
    {
        return;
    }

    wf::scene::node_damage_signal data;

    /* Sticky views are visible on all workspaces. */
    if (view->sticky)
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        auto cws   = output->workspace->get_current_workspace();

        /* Damage only the visible region of the shell view.
         * This prevents hidden panels from spilling damage onto other workspaces */
        wlr_box ws_box = output->get_relative_geometry();
        wlr_box visible_damage = geometry_intersection(box, ws_box);
        for (int i = 0; i < wsize.width; i++)
        {
            for (int j = 0; j < wsize.height; j++)
            {
                const int dx = (i - cws.x) * ws_box.width;
                const int dy = (j - cws.y) * ws_box.height;
                data.region |= visible_damage + wf::point_t{dx, dy};
            }
        }
    } else
    {
        data.region |= box;
    }

    view->get_transformed_node()->emit(&data);
    // FIXME: node should be pushed from surfaces up, but we can't do that yet,
    // because transformers are not in the scenegraph yet
    view->get_surface_root_node()->emit(&data);
}

void wf::view_interface_t::destruct()
{
    view_destruct_signal ev;
    ev.view = self();
    this->emit(&ev);
    wf::get_core_impl().erase_view(self());
}

const wf::scene::floating_inner_ptr& wf::view_interface_t::get_root_node() const
{
    return priv->root_node;
}

std::shared_ptr<wf::scene::transform_manager_node_t> wf::view_interface_t::get_transformed_node() const
{
    return priv->transformed_node;
}

const wf::scene::floating_inner_ptr& wf::view_interface_t::get_surface_root_node() const
{
    return priv->surface_root_node;
}

wayfire_view wf::node_to_view(wf::scene::node_t *node)
{
    while (node)
    {
        if (auto vnode = dynamic_cast<view_node_tag_t*>(node))
        {
            return vnode->get_view();
        }

        node = node->parent();
    }

    return nullptr;
}

wayfire_view wf::node_to_view(wf::scene::node_ptr node)
{
    return node_to_view(node.get());
}

// FIXME: Consider splitting to header + source file
#include "view-node.cpp"

wl_client*wf::view_interface_t::get_client()
{
    if (priv->wsurface)
    {
        return wl_resource_get_client(priv->wsurface->resource);
    }

    return nullptr;
}

wlr_surface*wf::view_interface_t::get_wlr_surface()
{
    return priv->wsurface;
}

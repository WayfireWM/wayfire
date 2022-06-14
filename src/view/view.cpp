#include <wayfire/util/log.hpp>
#include "../core/core-impl.hpp"
#include "view-impl.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/render-manager.hpp"
#include "xdg-shell.hpp"
#include "../output/gtk-shell.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include "wayfire/signal-definitions.hpp"

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
        desktop_state_updated();
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

        check_refocus_parent(parent);
    } else if (old_parent)
    {
        /* At this point, we are a regular view. */
        if (this->get_output())
        {
            this->get_output()->workspace->add_view(
                self(), wf::LAYER_WORKSPACE);

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
    result.reserve(view_impl->last_view_cnt);
    for (auto& v : this->children)
    {
        auto cdr = v->enumerate_views(mapped_only);
        result.insert(result.end(), cdr.begin(), cdr.end());
    }

    result.push_back(self());
    view_impl->last_view_cnt = result.size();

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
        /* Emit view-layer-detached first */
        get_output()->workspace->remove_view(self());

        view_detached_signal data;
        data.view = self();
        get_output()->emit_signal("view-disappeared", &data);
        get_output()->emit_signal("view-detached", &data);
    }

    _output_signal data;
    data.output = get_output();

    surface_interface_t::set_output(new_output);
    if ((new_output != data.output) && new_output)
    {
        view_attached_signal data;
        data.view = self();
        get_output()->emit_signal("view-attached", &data);
    }

    emit_signal("set-output", &data);

    for (auto& view : this->children)
    {
        view->set_output(new_output);
    }
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
    view_impl->update_windowed_geometry(self(), get_wm_geometry());
    /* edges are reset on the next commit */
    if (resizing)
    {
        this->view_impl->edges = edges;
    }

    auto& in_resize = this->view_impl->in_continuous_resize;
    in_resize += resizing ? 1 : -1;

    if (in_resize < 0)
    {
        LOGE("in_continuous_resize counter dropped below 0!");
    }
}

void wf::view_interface_t::set_moving(bool moving)
{
    view_impl->update_windowed_geometry(self(), get_wm_geometry());
    auto& in_move = this->view_impl->in_continuous_move;

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
    return transform_region(get_untransformed_bounding_box());
}

#define INVALID_COORDS(p) (std::isnan(p.x) || std::isnan(p.y))

wf::pointf_t wf::view_interface_t::global_to_local_point(const wf::pointf_t& arg,
    wf::surface_interface_t *surface)
{
    if (!is_mapped())
    {
        return arg;
    }

    /* First, untransform the coordinates to make them relative to the view's
     * internal coordinate system */
    wf::pointf_t result = arg;
    if (view_impl->transforms.size())
    {
        std::vector<wf::geometry_t> bb;
        bb.reserve(view_impl->transforms.size());
        auto box = get_untransformed_bounding_box();
        bb.push_back(box);
        view_impl->transforms.for_each([&] (auto& tr)
        {
            if (tr == view_impl->transforms.back())
            {
                return;
            }

            auto& transform = tr->transform;
            box = transform->get_bounding_box(box, box);
            bb.push_back(box);
        });

        view_impl->transforms.for_each_reverse([&] (auto& tr)
        {
            if (INVALID_COORDS(result))
            {
                return;
            }

            auto& transform = tr->transform;
            box = bb.back();
            bb.pop_back();
            result = transform->untransform_point(box, result);
        });

        if (INVALID_COORDS(result))
        {
            return result;
        }
    }

    /* Make cooordinates relative to the view */
    auto og = get_output_geometry();
    result.x -= og.x;
    result.y -= og.y;

    /* Go up from the surface, finding offsets */
    while (surface && surface != this)
    {
        auto offset = surface->get_offset();
        result.x -= offset.x;
        result.y -= offset.y;

        surface = surface->priv->parent_surface;
    }

    return result;
}

wf::surface_interface_t*wf::view_interface_t::map_input_coordinates(
    wf::pointf_t cursor, wf::pointf_t& local)
{
    if (!is_mapped())
    {
        return nullptr;
    }

    auto view_relative_coordinates =
        global_to_local_point(cursor, nullptr);

    for (auto& child : enumerate_surfaces({0, 0}))
    {
        local.x = view_relative_coordinates.x - child.position.x;
        local.y = view_relative_coordinates.y - child.position.y;

        if (child.surface->accepts_input(
            std::floor(local.x), std::floor(local.y)))
        {
            return child.surface;
        }
    }

    return nullptr;
}

bool wf::view_interface_t::is_focuseable() const
{
    return view_impl->keyboard_focus_enabled;
}

void wf::view_interface_t::set_minimized(bool minim)
{
    minimized = minim;
    if (minimized)
    {
        view_disappeared_signal data;
        data.view = self();
        get_output()->emit_signal("view-disappeared", &data);
        get_output()->workspace->add_view(self(), wf::LAYER_MINIMIZED);
    } else
    {
        get_output()->workspace->add_view(self(), wf::LAYER_WORKSPACE);
        get_output()->focus_view(self(), true);
    }

    view_minimized_signal data;
    data.view  = self();
    data.state = minimized;
    this->emit_signal("minimized", &data);
    get_output()->emit_signal("view-minimized", &data);

    desktop_state_updated();
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

    this->emit_signal("set-sticky", &data);
    if (this->get_output())
    {
        this->get_output()->emit_signal("view-set-sticky", &data);
    }
}

void wf::view_interface_t::set_tiled(uint32_t edges)
{
    if (edges)
    {
        view_impl->update_windowed_geometry(self(), get_wm_geometry());
    }

    wf::view_tiled_signal data;
    data.view = self();
    data.old_edges = this->tiled_edges;
    data.new_edges = edges;

    this->tiled_edges = edges;
    if (view_impl->frame)
    {
        view_impl->frame->notify_view_tiled();
    }

    this->emit_signal("tiled", &data);
    if (this->get_output())
    {
        get_output()->emit_signal("view-tiled", &data);
    }

    desktop_state_updated();
}

void wf::view_interface_t::set_fullscreen(bool full)
{
    /* When fullscreening a view, we want to store the last geometry it had
     * before getting fullscreen so that we can restore to it */
    if (full && !fullscreen)
    {
        view_impl->update_windowed_geometry(self(), get_wm_geometry());
    }

    fullscreen = full;
    if (view_impl->frame)
    {
        view_impl->frame->notify_view_fullscreen();
    }

    view_fullscreen_signal data;
    data.view  = self();
    data.state = full;
    data.desired_size = {0, 0, 0, 0};

    if (get_output())
    {
        get_output()->emit_signal("view-fullscreen", &data);
    }

    this->emit_signal("fullscreen", &data);
    desktop_state_updated();
}

void wf::view_interface_t::set_activated(bool active)
{
    if (view_impl->frame)
    {
        view_impl->frame->notify_view_activated(active);
    }

    activated = active;
    desktop_state_updated();
}

void wf::view_interface_t::desktop_state_updated()
{
    /* no-op */
}

void wf::view_interface_t::move_request()
{
    view_move_request_signal data;
    data.view = self();
    get_output()->emit_signal("view-move-request", &data);
}

void wf::view_interface_t::focus_request()
{
    if (get_output())
    {
        view_focus_request_signal data;
        data.view = self();
        data.self_request = false;

        emit_signal("view-focus-request", &data);
        wf::get_core().emit_signal("view-focus-request", &data);
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
    get_output()->emit_signal("view-resize-request", &data);
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
            self->get_output()->workspace->get_workarea();
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
    const auto& new_area = output->workspace->get_workarea();
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
    data.desired_size = edges ? get_output()->workspace->get_workarea() :
        view_impl->calculate_windowed_geometry(get_output());

    set_tiled(edges);
    if (is_mapped())
    {
        get_output()->emit_signal("view-tile-request", &data);
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
        get_output()->emit_signal("view-minimize-request", &data);
        /* Some plugin (e.g animate) will take care of the request, so we need
         * to just send proper state to foreign-toplevel clients */
        if (data.carried_out)
        {
            minimized = state;
            desktop_state_updated();
            get_output()->refocus(self());
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

    view_fullscreen_signal data;
    data.view  = self();
    data.state = state;
    data.workspace    = workspace;
    data.desired_size = get_output()->get_relative_geometry();

    if (!state)
    {
        data.desired_size = this->tiled_edges ?
            this->get_output()->workspace->get_workarea() :
            this->view_impl->calculate_windowed_geometry(get_output());
    }

    set_fullscreen(state);
    if (is_mapped())
    {
        wo->emit_signal("view-fullscreen-request", &data);
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

bool wf::view_interface_t::is_visible()
{
    if (view_impl->visibility_counter <= 0)
    {
        return false;
    }

    if (is_mapped())
    {
        return true;
    }

    /* If we have an unmapped view, then there are two cases:
     *
     * 1. View has been "destroyed". In this case, the view is visible as long
     * as it has at least one reference (for ex. a plugin which shows unmap
     * animation)
     *
     * 2. View hasn't been "destroyed", just unmapped. Here we need to have at
     * least 2 references, which would mean that the view is in unmap animation.
     */
    if (view_impl->is_alive)
    {
        return view_impl->ref_cnt >= 2;
    } else
    {
        return view_impl->ref_cnt >= 1;
    }
}

void wf::view_interface_t::set_visible(bool visible)
{
    this->view_impl->visibility_counter += (visible ? 1 : -1);
    if (this->view_impl->visibility_counter > 1)
    {
        LOGE("set_visible(true) called more often than set_visible(false)!");
    }

    this->damage();
}

void wf::view_interface_t::damage()
{
    auto bbox = get_untransformed_bounding_box();
    view_impl->offscreen_buffer.cached_damage |= bbox;
    view_damage_raw(self(), transform_region(bbox));
}

wlr_box wf::view_interface_t::get_minimize_hint()
{
    return this->view_impl->minimize_hint;
}

void wf::view_interface_t::set_minimize_hint(wlr_box hint)
{
    this->view_impl->minimize_hint = hint;
}

bool wf::view_interface_t::should_be_decorated()
{
    return false;
}

nonstd::observer_ptr<wf::decorator_frame_t_t> wf::view_interface_t::get_decoration()
{
    return this->view_impl->frame.get();
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
        view_impl->frame = nullptr;

        // Grow the tiled view to fill its old expanded geometry that included
        // the decoration.
        if (!fullscreen && this->tiled_edges && (wm != get_wm_geometry()))
        {
            set_geometry(wm);
        }

        emit_signal("decoration-changed", nullptr);
        return;
    }

    // Take wm geometry as it was before adding the frame */
    auto wm = get_wm_geometry();

    damage();
    // Drop the old frame if any and assign the new one.
    view_impl->frame = std::move(frame);

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
        target_wm_geometry = view_impl->frame->expand_wm_geometry(wm);
        // make sure that the view doesn't go outside of the screen or such
        auto wa = get_output()->workspace->get_workarea();
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
        target_wm_geometry = get_output()->workspace->get_workarea();
    }

    // notify the frame of the current size
    view_impl->frame->notify_view_resized(get_wm_geometry());
    // but request the target size, it will be sent to the frame on the
    // next commit
    set_geometry(target_wm_geometry);
    damage();

    emit_signal("decoration-changed", nullptr);
}

void wf::view_interface_t::add_transformer(
    std::unique_ptr<wf::view_transformer_t> transformer)
{
    add_transformer(std::move(transformer), "");
}

void wf::view_interface_t::add_transformer(
    std::unique_ptr<wf::view_transformer_t> transformer, std::string name)
{
    damage();

    auto tr = std::make_shared<wf::view_transform_block_t>();
    tr->transform   = std::move(transformer);
    tr->plugin_name = name;

    view_impl->transforms.emplace_at(std::move(tr), [&] (auto& other)
    {
        if (other->transform->get_z_order() >= tr->transform->get_z_order())
        {
            return view_impl->transforms.INSERT_BEFORE;
        }

        return view_impl->transforms.INSERT_NONE;
    });

    damage();
}

nonstd::observer_ptr<wf::view_transformer_t> wf::view_interface_t::get_transformer(
    std::string name)
{
    nonstd::observer_ptr<wf::view_transformer_t> result{nullptr};
    view_impl->transforms.for_each([&] (auto& tr)
    {
        if (tr->plugin_name == name)
        {
            result = nonstd::make_observer(tr->transform.get());
        }
    });

    return result;
}

void wf::view_interface_t::pop_transformer(
    nonstd::observer_ptr<wf::view_transformer_t> transformer)
{
    view_impl->transforms.remove_if([&] (auto& tr)
    {
        return tr->transform.get() == transformer.get();
    });

    /* Since we can remove transformers while rendering the output, damaging it
     * won't help at this stage (damage is already calculated).
     *
     * Instead, we directly damage the whole output for the next frame */
    if (get_output())
    {
        get_output()->render->damage_whole_idle();
    }
}

void wf::view_interface_t::pop_transformer(std::string name)
{
    pop_transformer(get_transformer(name));
}

bool wf::view_interface_t::has_transformer()
{
    return view_impl->transforms.size();
}

wf::geometry_t wf::view_interface_t::get_untransformed_bounding_box()
{
    if (!is_mapped())
    {
        return view_impl->offscreen_buffer.geometry;
    }

    auto bbox = get_output_geometry();
    wf::region_t bounding_region = bbox;

    for (auto& child : enumerate_surfaces({bbox.x, bbox.y}))
    {
        auto dim = child.surface->get_size();
        bounding_region |= {child.position.x, child.position.y,
            dim.width, dim.height};
    }

    return wlr_box_from_pixman_box(bounding_region.get_extents());
}

wlr_box wf::view_interface_t::get_bounding_box(std::string transformer)
{
    return get_bounding_box(get_transformer(transformer));
}

wlr_box wf::view_interface_t::get_bounding_box(
    nonstd::observer_ptr<wf::view_transformer_t> transformer)
{
    return transform_region(get_untransformed_bounding_box(), transformer);
}

wlr_box wf::view_interface_t::transform_region(const wlr_box& region,
    nonstd::observer_ptr<wf::view_transformer_t> upto)
{
    auto box  = region;
    auto view = get_untransformed_bounding_box();

    bool computed_region = false;
    view_impl->transforms.for_each([&] (auto& tr)
    {
        if (computed_region || (tr->transform.get() == upto.get()))
        {
            computed_region = true;

            return;
        }

        box  = tr->transform->get_bounding_box(view, box);
        view = tr->transform->get_bounding_box(view, view);
    });

    return box;
}

wlr_box wf::view_interface_t::transform_region(const wlr_box& region,
    std::string transformer)
{
    return transform_region(region, get_transformer(transformer));
}

wlr_box wf::view_interface_t::transform_region(const wlr_box& region)
{
    return transform_region(region,
        nonstd::observer_ptr<wf::view_transformer_t>(nullptr));
}

wf::pointf_t wf::view_interface_t::transform_point(const wf::pointf_t& point)
{
    auto result = point;
    auto view   = get_untransformed_bounding_box();

    view_impl->transforms.for_each([&] (auto& tr)
    {
        result = tr->transform->transform_point(view, result);
        view   = tr->transform->get_bounding_box(view, view);
    });

    return result;
}

bool wf::view_interface_t::intersects_region(const wlr_box& region)
{
    /* fallback to the whole transformed boundingbox, if it exists */
    if (!is_mapped())
    {
        return region & get_bounding_box();
    }

    auto origin = get_output_geometry();
    for (auto& child : enumerate_surfaces({origin.x, origin.y}))
    {
        wlr_box box = {child.position.x, child.position.y,
            child.surface->get_size().width, child.surface->get_size().height};
        box = transform_region(box);

        if (region & box)
        {
            return true;
        }
    }

    return false;
}

wf::region_t wf::view_interface_t::get_transformed_opaque_region()
{
    if (!is_mapped())
    {
        return {};
    }

    auto obox = get_untransformed_bounding_box();
    auto og   = get_output_geometry();

    wf::region_t opaque;
    for (auto& surf : enumerate_surfaces({og.x, og.y}))
    {
        opaque |= surf.surface->get_opaque_region(surf.position);
    }

    auto bbox = obox;
    this->view_impl->transforms.for_each(
        [&] (const std::shared_ptr<view_transform_block_t> tr)
    {
        opaque = tr->transform->transform_opaque_region(bbox, opaque);
        bbox   = tr->transform->get_bounding_box(bbox, bbox);
    });

    return opaque;
}

bool wf::view_interface_t::render_transformed(const wf::framebuffer_t& framebuffer,
    const wf::region_t& damage)
{
    if (!is_mapped() && !view_impl->offscreen_buffer.valid())
    {
        return false;
    }

    wf::geometry_t obox = get_untransformed_bounding_box();
    wf::texture_t previous_texture;
    float texture_scale;

    if (is_mapped() && (enumerate_surfaces().size() == 1) && get_wlr_surface())
    {
        /* Optimized case: there is a single mapped surface.
         * We can directly start with its texture */
        previous_texture = wf::texture_t{this->get_wlr_surface()};
        texture_scale    = this->get_wlr_surface()->current.scale;
    } else
    {
        take_snapshot();
        previous_texture = wf::texture_t{view_impl->offscreen_buffer.tex};
        texture_scale    = view_impl->offscreen_buffer.scale;
    }

    /* We keep a shared_ptr to the previous transform which we executed, so that
     * even if it gets removed, its texture remains valid.
     *
     * NB: we do not call previous_transform's transformer functions after the
     * cycle is complete, because the memory might have already been freed.
     * We only know that the texture is still alive. */
    std::shared_ptr<view_transform_block_t> previous_transform = nullptr;

    /* final_transform is the one that should render to the screen */
    std::shared_ptr<view_transform_block_t> final_transform = nullptr;

    /* Render the view passing its snapshot through the transformers.
     * For each transformer except the last we render on offscreen buffers,
     * and the last one is rendered to the real fb. */
    auto& transforms = view_impl->transforms;
    transforms.for_each([&] (auto& transform) -> void
    {
        /* Last transform is handled separately */
        if (transform == transforms.back())
        {
            final_transform = transform;

            return;
        }

        /* Calculate size after this transform */
        auto transformed_box =
            transform->transform->get_bounding_box(obox, obox);
        int scaled_width  = transformed_box.width * texture_scale;
        int scaled_height = transformed_box.height * texture_scale;

        /* Prepare buffer to store result after the transform */
        OpenGL::render_begin();
        transform->fb.allocate(scaled_width, scaled_height);
        transform->fb.scale    = texture_scale;
        transform->fb.geometry = transformed_box;
        transform->fb.bind(); // bind buffer to clear it
        OpenGL::clear({0, 0, 0, 0});
        OpenGL::render_end();

        /* Actually render the transform to the next framebuffer */
        transform->transform->render_with_damage(previous_texture, obox,
            wf::region_t{transformed_box}, transform->fb);

        previous_transform = transform;
        previous_texture   = previous_transform->fb.tex;
        obox = transformed_box;
    });

    /* This can happen in two ways:
     * 1. The view is unmapped, and no snapshot
     * 2. The last transform was deleted while iterating, so now the last
     *    transform is invalid in the list
     *
     * In both cases, we simply render whatever contents we have to the
     * framebuffer. */
    if (final_transform == nullptr)
    {
        OpenGL::render_begin(framebuffer);
        auto matrix = framebuffer.get_orthographic_projection();
        gl_geometry src_geometry = {
            1.0f * obox.x, 1.0f * obox.y,
            1.0f * obox.x + 1.0f * obox.width,
            1.0f * obox.y + 1.0f * obox.height,
        };

        for (const auto& rect : damage)
        {
            framebuffer.logic_scissor(wlr_box_from_pixman_box(rect));
            OpenGL::render_transformed_texture(previous_texture, src_geometry,
                {}, matrix);
        }

        OpenGL::render_end();
    } else
    {
        /* Regular case, just call the last transformer, but render directly
         * to the target framebuffer */
        final_transform->transform->render_with_damage(previous_texture, obox,
            damage & framebuffer.geometry, framebuffer);
    }

    return true;
}

wf::view_transform_block_t::view_transform_block_t()
{}
wf::view_transform_block_t::~view_transform_block_t()
{
    OpenGL::render_begin();
    this->fb.release();
    OpenGL::render_end();
}

const wf::framebuffer_t& wf::view_interface_t::take_snapshot()
{
    if (!is_mapped())
    {
        return view_impl->offscreen_buffer;
    }

    auto& offscreen_buffer = view_impl->offscreen_buffer;

    auto buffer_geometry = get_untransformed_bounding_box();
    offscreen_buffer.geometry = buffer_geometry;

    float scale = get_output()->handle->scale;

    offscreen_buffer.cached_damage &= buffer_geometry;
    /* Nothing has changed, the last buffer is still valid */
    if (offscreen_buffer.cached_damage.empty())
    {
        return view_impl->offscreen_buffer;
    }

    int scaled_width  = buffer_geometry.width * scale;
    int scaled_height = buffer_geometry.height * scale;
    if ((scaled_width != offscreen_buffer.viewport_width) ||
        (scaled_height != offscreen_buffer.viewport_height))
    {
        offscreen_buffer.cached_damage |= buffer_geometry;
    }

    OpenGL::render_begin();
    offscreen_buffer.allocate(scaled_width, scaled_height);
    offscreen_buffer.scale = scale;
    offscreen_buffer.bind();
    for (auto& box : offscreen_buffer.cached_damage)
    {
        offscreen_buffer.logic_scissor(wlr_box_from_pixman_box(box));
        OpenGL::clear({0, 0, 0, 0});
    }

    OpenGL::render_end();

    auto output_geometry = get_output_geometry();
    auto children = enumerate_surfaces({output_geometry.x, output_geometry.y});
    for (auto& child : wf::reverse(children))
    {
        wlr_box child_box{
            child.position.x,
            child.position.y,
            child.surface->get_size().width,
            child.surface->get_size().height
        };

        child.surface->simple_render(offscreen_buffer,
            child.position.x, child.position.y,
            offscreen_buffer.cached_damage & child_box);
    }

    offscreen_buffer.cached_damage.clear();

    return view_impl->offscreen_buffer;
}

wf::view_interface_t::view_interface_t()
{
    this->view_impl = std::make_unique<wf::view_interface_t::view_priv_impl>();
    take_ref();
}

void wf::view_interface_t::take_ref()
{
    ++view_impl->ref_cnt;
}

void wf::view_interface_t::unref()
{
    --view_impl->ref_cnt;
    if (view_impl->ref_cnt <= 0)
    {
        destruct();
    }
}

void wf::view_interface_t::initialize()
{}

void wf::view_interface_t::deinitialize()
{
    auto children = this->children;
    for (auto ch : children)
    {
        ch->set_toplevel_parent(nullptr);
    }

    set_decoration(nullptr);

    this->clear_subsurfaces();
    this->view_impl->transforms.clear();
    this->_clear_data();

    OpenGL::render_begin();
    this->view_impl->offscreen_buffer.release();
    OpenGL::render_end();
}

wf::view_interface_t::~view_interface_t()
{
    /* Note: at this point, it is invalid to call most functions */
    unset_toplevel_parent(self());
}

void wf::view_interface_t::damage_surface_box(const wlr_box& box)
{
    auto obox = get_output_geometry();

    auto damaged = box;
    damaged.x += obox.x;
    damaged.y += obox.y;
    view_impl->offscreen_buffer.cached_damage |= damaged;
    view_damage_raw(self(), transform_region(damaged));
}

void wf::view_damage_raw(wayfire_view view, const wlr_box& box)
{
    auto output = view->get_output();
    if (!output)
    {
        return;
    }

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
                output->render->damage(visible_damage + wf::point_t{dx, dy});
            }
        }
    } else
    {
        output->render->damage(box);
    }

    view->emit_signal("region-damaged", nullptr);
}

void wf::view_interface_t::destruct()
{
    view_impl->is_alive = false;
    wf::get_core_impl().erase_view(self());
}

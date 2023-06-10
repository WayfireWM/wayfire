#ifndef VIEW_HPP
#define VIEW_HPP

#include <memory>
#include <vector>
#include <wayfire/nonstd/observer_ptr.h>

#include "wayfire/object.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/decorator.hpp"
#include "wayfire/view-transform.hpp"
#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/region.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/scene.hpp>

namespace wf
{
class view_interface_t;
class workspace_set_t;
class decorator_frame_t_t;
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

namespace wf
{
class output_t;
namespace scene
{
class view_node_t;
}

// A signal emitted when the view is destroyed and its memory will be freed.
struct view_destruct_signal
{
    wayfire_view view;
};

/* abstraction for desktop-apis, no real need for plugins
 * This is a base class to all "drawables" - desktop views, subsurfaces, popups */
enum view_role_t
{
    /** Regular views which can be moved around. */
    VIEW_ROLE_TOPLEVEL,
    /** Views which position is fixed externally, for ex. Xwayland OR views */
    VIEW_ROLE_UNMANAGED,
    /**
     * Views which are part of the desktop environment, for example panels,
     * background views, etc.
     */
    VIEW_ROLE_DESKTOP_ENVIRONMENT,
};

/**
 * view_interface_t is the base class for all "toplevel windows", i.e surfaces
 * which have no parent.
 */
class view_interface_t : public wf::signal::provider_t, public wf::object_base_t
{
  public:
    /**
     * Get the root of the view tree. This is the node which contains the view
     * and all of its child views.
     *
     * Usually, the tree root node has at least the transformed_node as its child,
     * and the tree root nodes of child views.
     */
    const scene::floating_inner_ptr& get_root_node() const;

    /**
     * Get the root of the view itself, including its main surface, subsurfaces
     * and transformers, but not dialogs.
     */
    std::shared_ptr<scene::transform_manager_node_t> get_transformed_node() const;

    /**
     * Get the node which contains the main view (+subsurfaces) only.
     */
    const scene::floating_inner_ptr& get_surface_root_node() const;

    /** The current view role.  */
    view_role_t role = VIEW_ROLE_TOPLEVEL;

    /** Set the view role */
    virtual void set_role(view_role_t new_role);

    /** Get a textual identifier for this view.  */
    std::string to_string() const;

    /** Wrap the view into a nonstd::observer_ptr<> */
    wayfire_view self();

    /**
     * Set the view's output.
     *
     * If the new output is different from the previous, the view will be
     * removed from the layer it was on the old output.
     */
    virtual void set_output(wf::output_t *new_output);

    /**
     * Get the view's main output.
     */
    virtual wf::output_t *get_output();

    /** Request that the view closes. */
    virtual void close();

    /**
     * Ping the view's client.
     * If the ping request times out, `ping-timeout` event will be emitted.
     */
    virtual void ping();

    /**
     * @return The bounding box of the view, which includes all (sub)surfaces,
     * menus, etc. after applying the view transformations.
     */
    virtual wlr_box get_bounding_box();

    /**
     * @return the wlr_surface which should receive focus when focusing this
     * view. Views which aren't backed by a wlr_surface should implement the
     * compositor_view interface.
     *
     * In case no focus surface is available, or the view should not be focused,
     * nullptr should be returned.
     */
    virtual wlr_surface *get_keyboard_focus_surface() = 0;

    /**
     * Check whether the surface is focusable. Note the actual ability to give
     * keyboard focus while the surface is mapped is determined by the keyboard
     * focus surface or the compositor_view implementation.
     *
     * This is meant for plugins like matcher, which need to check whether the
     * view is focusable at any point of the view life-cycle.
     */
    virtual bool is_focusable() const;

    /** Damage the whole view and add the damage to its output */
    virtual void damage();

    /** @return the app-id of the view */
    virtual std::string get_app_id()
    {
        return "";
    }

    /** @return the title of the view */
    virtual std::string get_title()
    {
        return "";
    }

    /** @return true if the view has active transformers */
    bool has_transformer();

    /**
     * A snapshot of the view is a copy of the view's contents into a framebuffer.
     */
    virtual void take_snapshot(wf::render_target_t& target);

    /**
     * View lifetime is managed by reference counting. To take a reference,
     * use take_ref(). Note that one reference is automatically made when the
     * view is created.
     */
    void take_ref();

    /**
     * Drop a reference to the surface. When the reference count reaches 0, the
     * destruct() method is called.
     */
    void unref();

    /**
     * @return the wl_client associated with this surface, or null if the
     *   surface doesn't have a backing wlr_surface.
     */
    wl_client *get_client();

    /**
     * @return the wlr_surface associated with this surface, or null if no
     *   the surface doesn't have a backing wlr_surface. */
    wlr_surface *get_wlr_surface();

    virtual bool is_mapped() const
    {
        return false;
    }

    virtual ~view_interface_t();

    class view_priv_impl;
    std::unique_ptr<view_priv_impl> priv;

  protected:
    view_interface_t();
    void set_surface_root_node(scene::floating_inner_ptr surface_root_node);

    friend class compositor_core_impl_t;
    /**
     * View initialization happens in three stages:
     *
     * First, memory for the view is allocated and default members are set.
     * Second, the view is added to core, which assigns the view to an output.
     * Third, core calls @initialize() on the view. This is where the real view
     *   initialization should happen.
     *
     * Note that generally most of the operations in 3. can be also done in 1.,
     * except when they require an output.
     */
    virtual void initialize();

    /**
     * When a view is being destroyed, all associated objects like subsurfaces,
     * transformers and custom data are destroyed.
     *
     * In general, we want to make sure that these associated objects are freed
     * before the actual view object destruction starts. Thus, deinitialize()
     * is called from core just before destroying the view.
     */
    virtual void deinitialize();

    /**
     * Called when the reference count reaches 0.
     * It destructs the object and deletes it, so "this" may not be
     * accessed after destruct() is called.
     */
    virtual void destruct();

    /**
     * Emit the view map signal. It indicates that a view has been mapped, i.e.
     * plugins can now "work" with it. Note that not all views will emit the map
     * event.
     */
    virtual void emit_view_map();

    /**
     * Emit the view unmap signal. It indicates that the view is in the process of
     * being destroyed. Most plugins should stop any actions they have on the view.
     */
    virtual void emit_view_unmap();

    /**
     * Emit the view pre-unmap signal. It is emitted right before the view
     * destruction start. At this moment a plugin can still take a snapshot of the
     * view. Note that not all views emit the pre-unmap signal, however the unmap
     * signal is mandatory for all views.
     */
    virtual void emit_view_pre_unmap();
};

wayfire_view wl_surface_to_wayfire_view(wl_resource *surface);

/**
 * Find a view this node belongs to.
 * May return NULL if @node is NULL or it is not a child of a view node.
 */
wayfire_view node_to_view(wf::scene::node_ptr node);
wayfire_view node_to_view(wf::scene::node_t *node);

/**
 * A base class for nodes which are to be identified as views.
 * Used by @node_to_view in order to figure out whether a given node is a view or not.
 */
class view_node_tag_t
{
  public:
    view_node_tag_t(wayfire_view view)
    {
        this->view = view;
    }

    virtual ~view_node_tag_t() = default;

    wayfire_view get_view() const
    {
        return view;
    }

  private:
    wayfire_view view;
};
}

#endif

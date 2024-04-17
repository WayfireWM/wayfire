#include "wayfire/unstable/xwl-toplevel-base.hpp"
#include "wayfire/view-helpers.hpp"
#include "../view-impl.hpp"

#if WF_HAS_XWAYLAND

wf::xwayland_view_base_t::xwayland_view_base_t(wlr_xwayland_surface *xww)
{
    this->xw = xww;

    on_destroy.set_callback([=] (void*) { destroy(); });
    on_set_title.set_callback([=] (void*)
    {
        handle_title_changed(nonull(xw->title));
    });
    on_set_app_id.set_callback([=] (void*)
    {
        handle_app_id_changed(nonull(xw->class_t));
    });
    on_ping_timeout.set_callback([=] (void*)
    {
        wf::view_implementation::emit_ping_timeout_signal(dynamic_cast<wf::view_interface_t*>(this));
    });

    this->title  = nonull(xw->title);
    this->app_id = nonull(xw->class_t);

    on_destroy.connect(&xw->events.destroy);
    on_set_title.connect(&xw->events.set_title);
    on_set_app_id.connect(&xw->events.set_class);
    on_ping_timeout.connect(&xw->events.ping_timeout);

    xw->data = dynamic_cast<wf::view_interface_t*>(this);
}

wf::xwayland_view_base_t::~xwayland_view_base_t()
{
    if (xw && (xw->data == dynamic_cast<view_interface_t*>(this)))
    {
        xw->data = nullptr;
    }
}

void wf::xwayland_view_base_t::do_map(wlr_surface *surface, bool autocommit, bool emit_map)
{
    if (!this->main_surface)
    {
        this->main_surface = std::make_shared<wf::scene::wlr_surface_node_t>(xw->surface, autocommit);
        priv->set_mapped_surface_contents(main_surface);
    }

    priv->set_mapped(xw->surface);
    priv->set_enabled(true);
    damage();

    if (emit_map)
    {
        emit_view_map();
    }
}

void wf::xwayland_view_base_t::do_unmap()
{
    damage();
    main_surface = nullptr;
    priv->unset_mapped_surface_contents();
    priv->set_mapped(nullptr);

    emit_view_unmap();
    priv->set_enabled(false);
    wf::scene::update(get_surface_root_node(), wf::scene::update_flag::INPUT_STATE);
}

void wf::xwayland_view_base_t::destroy()
{
    this->xw = nullptr;
    on_destroy.disconnect();
    on_set_title.disconnect();
    on_set_app_id.disconnect();
    on_ping_timeout.disconnect();
}

void wf::xwayland_view_base_t::handle_app_id_changed(std::string new_app_id)
{
    this->app_id = new_app_id;
    wf::view_implementation::emit_app_id_changed_signal(
        dynamic_cast<wf::view_interface_t*>(this));
}

void wf::xwayland_view_base_t::handle_title_changed(std::string new_title)
{
    this->title = new_title;
    wf::view_implementation::emit_title_changed_signal(
        dynamic_cast<wf::view_interface_t*>(this));
}

std::string wf::xwayland_view_base_t::get_app_id()
{
    return this->app_id;
}

std::string wf::xwayland_view_base_t::get_title()
{
    return this->title;
}

void wf::xwayland_view_base_t::ping()
{
    if (xw)
    {
        wlr_xwayland_surface_ping(xw);
    }
}

void wf::xwayland_view_base_t::close()
{
    if (xw)
    {
        wlr_xwayland_surface_close(xw);
    }
}

bool wf::xwayland_view_base_t::is_mapped() const
{
    return priv->is_mapped;
}

wlr_surface*wf::xwayland_view_base_t::get_keyboard_focus_surface()
{
    if (is_mapped() && kb_focus_enabled)
    {
        return priv->wsurface;
    }

    return NULL;
}

bool wf::xwayland_view_base_t::is_focusable() const
{
    return kb_focus_enabled;
}

#endif

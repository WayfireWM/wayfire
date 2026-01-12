#include "xwayland-helpers.hpp"
#include "core/xdg-output-management.hpp"

#if WF_HAS_XWAYLAND

std::optional<xcb_atom_t> wf::xw::load_atom(xcb_connection_t *connection, const std::string& name)
{
    std::optional<xcb_atom_t> result;
    auto cookie = xcb_intern_atom(connection, 0, name.length(), name.c_str());
    xcb_generic_error_t *error = NULL;
    xcb_intern_atom_reply_t *reply;
    reply = xcb_intern_atom_reply(connection, cookie, &error);

    if (!error && reply)
    {
        result = reply->atom;
    }

    free(reply);
    free(error);
    return result;
}

bool wf::xw::load_basic_atoms(const char *server_name)
{
    auto connection = xcb_connect(server_name, NULL);
    if (!connection || xcb_connection_has_error(connection))
    {
        return false;
    }

    _NET_WM_WINDOW_TYPE_NORMAL  = load_atom(connection, "_NET_WM_WINDOW_TYPE_NORMAL").value_or(-1);
    _NET_WM_WINDOW_TYPE_DIALOG  = load_atom(connection, "_NET_WM_WINDOW_TYPE_DIALOG").value_or(-1);
    _NET_WM_WINDOW_TYPE_SPLASH  = load_atom(connection, "_NET_WM_WINDOW_TYPE_SPLASH").value_or(-1);
    _NET_WM_WINDOW_TYPE_UTILITY = load_atom(connection, "_NET_WM_WINDOW_TYPE_UTILITY").value_or(-1);
    _NET_WM_WINDOW_TYPE_DND     = load_atom(connection, "_NET_WM_WINDOW_TYPE_DND").value_or(-1);
    xcb_disconnect(connection);
    return true;
}

bool wf::xw::has_type(wlr_xwayland_surface *xw, xcb_atom_t type)
{
    for (size_t i = 0; i < xw->window_type_len; i++)
    {
        if (xw->window_type[i] == type)
        {
            return true;
        }
    }

    return false;
}

wf::output_t*wf::xw::find_xwayland_surface_output(wlr_xwayland_surface *xw)
{
    auto& ol  = wf::get_core().output_layout;
    double cx = xw->x + xw->width / 2.0;
    double cy = xw->y + xw->height / 2.0;
    wf::output_t *closest = ol->get_outputs().empty() ? nullptr : ol->get_outputs().front();
    double closest_dist   = std::numeric_limits<double>::max();
    for (auto & wo : ol->get_outputs())
    {
        auto data = wo->get_data_safe<wf::xdg_output_xwayland_geometry>();
        if (!data->geometry.has_value())
        {
            continue;
        }

        double dx;
        double dy;
        wlr_box_closest_point(&data->geometry.value(), cx, cy, &dx, &dy);
        const double dist = (dx - cx) * (dx - cx) + (dy - cy) * (dy - cy);
        if (dist < closest_dist)
        {
            closest_dist = dist;
            closest = wo;
        }
    }

    return closest;
}

wf::geometry_t wf::xw::calculate_wayfire_geometry(wf::output_t *ref_output, wf::geometry_t geometry)
{
    if (!ref_output)
    {
        return geometry;
    }

    auto data = ref_output->get_data_safe<wf::xdg_output_xwayland_geometry>();
    if (!data->geometry.has_value())
    {
        LOGW("Xwayland geometry not set for output ", ref_output->to_string(),
            ", returning original geometry");
        return geometry;
    }

    geometry = geometry - wf::origin(data->geometry.value());
    static wf::option_wrapper_t<bool> force_xwayland_scaling{"workarounds/force_xwayland_scaling"};
    if (force_xwayland_scaling)
    {
        geometry = geometry * (1.0 / ref_output->get_scale());
    }

    return geometry;
}

#endif

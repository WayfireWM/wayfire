#include "xwayland-helpers.hpp"

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

#endif

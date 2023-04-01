#include "config.h"
#include <optional>
#include <string>

#if WF_HAS_XWAYLAND

    #include <xcb/xcb.h>

namespace wf
{
namespace xw
{
enum class view_type
{
    NORMAL,
    UNMANAGED,
    DND,
};

extern xcb_atom_t _NET_WM_WINDOW_TYPE_NORMAL;
extern xcb_atom_t _NET_WM_WINDOW_TYPE_DIALOG;
extern xcb_atom_t _NET_WM_WINDOW_TYPE_SPLASH;
extern xcb_atom_t _NET_WM_WINDOW_TYPE_DND;

std::optional<xcb_atom_t> load_atom(xcb_connection_t *connection, const std::string& name);
bool load_basic_atoms(const char *server_name);
}
}

#endif

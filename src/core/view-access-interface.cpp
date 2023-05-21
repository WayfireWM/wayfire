#include "wayfire/condition/access_interface.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/view.hpp"
#include "wayfire/view-access-interface.hpp"
#include "wayfire/workspace-manager.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <algorithm>
#include <iostream>
#include <string>

namespace wf
{
view_access_interface_t::view_access_interface_t()
{}

view_access_interface_t::view_access_interface_t(wayfire_view view) : _view(view)
{}

view_access_interface_t::~view_access_interface_t()
{}

variant_t view_access_interface_t::get(const std::string & identifier, bool & error)
{
    variant_t out = std::string(""); // Default to empty string as output.
    error = false; // Assume things will go well.

    // Cannot operate if no view is set.
    if (_view == nullptr)
    {
        error = true;

        return out;
    }

    if (identifier == "app_id")
    {
        out = _view->get_app_id();
    } else if (identifier == "title")
    {
        out = _view->get_title();
    } else if (identifier == "role")
    {
        switch (_view->role)
        {
          case VIEW_ROLE_TOPLEVEL:
            out = std::string("TOPLEVEL");
            break;

          case VIEW_ROLE_UNMANAGED:
            out = std::string("UNMANAGED");
            break;

          case VIEW_ROLE_DESKTOP_ENVIRONMENT:
            out = std::string("DESKTOP_ENVIRONMENT");
            break;

          default:
            std::cerr <<
                "View access interface: View has unsupported value for role: " <<
                static_cast<int>(_view->role) << std::endl;
            error = true;
            break;
        }
    } else if (identifier == "fullscreen")
    {
        out = _view->fullscreen;
    } else if (identifier == "activated")
    {
        out = _view->activated;
    } else if (identifier == "minimized")
    {
        out = _view->minimized;
    } else if (identifier == "focusable")
    {
        out = _view->is_focusable();
    } else if (identifier == "mapped")
    {
        out = _view->is_mapped();
    } else if (identifier == "tiled-left")
    {
        out = (_view->tiled_edges & WLR_EDGE_LEFT) > 0;
    } else if (identifier == "tiled-right")
    {
        out = (_view->tiled_edges & WLR_EDGE_RIGHT) > 0;
    } else if (identifier == "tiled-top")
    {
        out = (_view->tiled_edges & WLR_EDGE_TOP) > 0;
    } else if (identifier == "tiled-bottom")
    {
        out = (_view->tiled_edges & WLR_EDGE_BOTTOM) > 0;
    } else if (identifier == "maximized")
    {
        out = _view->tiled_edges == TILED_EDGES_ALL;
    } else if (identifier == "floating")
    {
        out = _view->tiled_edges == 0;
    } else if (identifier == "type")
    {
        do {
            if (_view->role == VIEW_ROLE_TOPLEVEL)
            {
                out = std::string("toplevel");
                break;
            }

            if (_view->role == VIEW_ROLE_UNMANAGED)
            {
#if WF_HAS_XWAYLAND
                auto surf = _view->get_wlr_surface();
                if (surf && wlr_surface_is_xwayland_surface(surf))
                {
                    out = std::string("x-or");
                    break;
                }

#endif
                out = std::string("unmanaged");
                break;
            }

            if (!_view->get_output())
            {
                out = std::string("unknown");
                break;
            }

            auto layer = get_view_layer(_view);
            if ((layer == wf::scene::layer::BACKGROUND) || (layer == wf::scene::layer::BOTTOM))
            {
                out = std::string("background");
            } else if (layer == wf::scene::layer::TOP)
            {
                out = std::string("panel");
            } else if (layer == wf::scene::layer::OVERLAY)
            {
                out = std::string("overlay");
            }

            break;

            out = std::string("unknown");
        } while (false);
    } else
    {
        std::cerr << "View access interface: Get operation triggered to" <<
            " unsupported view property " << identifier << std::endl;
    }

    return out;
}

void view_access_interface_t::set_view(wayfire_view view)
{
    _view = view;
}
} // End namespace wf.

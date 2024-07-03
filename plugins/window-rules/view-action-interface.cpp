#include "view-action-interface.hpp"

#include "wayfire/core.hpp"
#include "wayfire/output.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view.hpp"
#include "wayfire/plugins/grid.hpp"
#include "wayfire/util/log.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/output-layout.hpp"
#include "../wm-actions/wm-actions-signals.hpp"
#include <wayfire/plugins/common/util.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workspace-set.hpp>

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace wf
{
view_action_interface_t::~view_action_interface_t()
{}

bool view_action_interface_t::execute(const std::string & name,
    const std::vector<variant_t> & args)
{
    const auto& execute_set_alpha = [&]
    {
        auto alpha = _validate_alpha(args);
        if (std::get<0>(alpha))
        {
            _set_alpha(std::get<1>(alpha));
        }
    };

    if (!_view)
    {
        // We have a non-toplevel view. We can only adjust alpha for it ...

        if ((name != "set") || (args.size() != 2) || (wf::get_string(args[0]) != "alpha"))
        {
            LOGW("The only allowed action for non-toplevel views is",
                " set alpha <double>, matched ", _nontoplevel);
            return true;
        }

        execute_set_alpha();
        return false;
    }

    if (name == "set")
    {
        auto id = wf::get_string(args.at(0));

        if (id == "sticky")
        {
            _make_sticky();
            return false;
        } else if (id == "always_on_top")
        {
            _always_on_top();
            return false;
        }

        if ((args.size() < 2) || (wf::is_string(args.at(0)) == false))
        {
            LOGE(
                "View action interface: Set execution requires at least 2 arguments, the first of which should be an identifier.");

            return true;
        }

        if (id == "alpha")
        {
            execute_set_alpha();
        } else if (id == "geometry")
        {
            auto geometry = _validate_geometry(args);
            if (std::get<0>(geometry))
            {
                _set_geometry(std::get<1>(geometry), std::get<2>(geometry),
                    std::get<3>(geometry), std::get<4>(geometry));
            }
        } else if (id == "geometry_ppt")
        {
            auto geometry = _validate_geometry(args);
            if (std::get<0>(geometry))
            {
                _set_geometry_ppt(std::get<1>(geometry), std::get<2>(geometry),
                    std::get<3>(geometry), std::get<4>(geometry));
            }
        } else
        {
            LOGE("View action interface: Unsupported set operation to identifier ",
                id);

            return true;
        }

        return false;
    } else if (name == "maximize")
    {
        _maximize();

        return false;
    } else if (name == "unmaximize")
    {
        _unmaximize();

        return false;
    } else if (name == "minimize")
    {
        _minimize();

        return false;
    } else if (name == "unminimize")
    {
        _unminimize();

        return false;
    } else if (name == "snap")
    {
        if ((args.size() < 1) || (wf::is_string(args.at(0)) == false))
        {
            LOGE(
                "View action interface: Snap execution requires 1 string as argument.");

            return true;
        }

        auto output = _view->get_output();
        if (output == nullptr)
        {
            LOGE("View action interface: Output associated with view was null.");

            return true;
        }

        auto location = wf::get_string(args.at(0));

        grid::slot_t slot;
        if (location == "top")
        {
            slot = grid::SLOT_TOP;
        } else if (location == "top_right")
        {
            slot = grid::SLOT_TR;
        } else if (location == "right")
        {
            slot = grid::SLOT_RIGHT;
        } else if (location == "bottom_right")
        {
            slot = grid::SLOT_BR;
        } else if (location == "bottom")
        {
            slot = grid::SLOT_BOTTOM;
        } else if (location == "bottom_left")
        {
            slot = grid::SLOT_BL;
        } else if (location == "left")
        {
            slot = grid::SLOT_LEFT;
        } else if (location == "top_left")
        {
            slot = grid::SLOT_TL;
        } else if (location == "center")
        {
            slot = grid::SLOT_CENTER;
        } else
        {
            LOGE(
                "View action interface: Incorrect string literal for snap location: ", location,
                ".");

            return true;
        }

        LOGI("View action interface: Snap to ", location, ".");
        wf::get_core().default_wm->tile_request(_view, grid::get_tiled_edges_for_slot(slot));
        return false;
    } else if (name == "start_on_output")
    {
        if ((args.size() < 1) || (wf::is_string(args.at(0)) == false))
        {
            LOGE(
                "View action interface: Start on output execution requires 1 string as argument.");

            return true;
        }

        auto name = wf::get_string(args.at(0));
        _start_on_output(name);

        return false;
    } else if (name == "move")
    {
        auto position = _validate_position(args);
        if (std::get<0>(position))
        {
            _move(std::get<1>(position), std::get<2>(position));
            return false;
        }

        LOGE("View action interface: invalid arguments for move");
        return true;
    } else if (name == "resize")
    {
        auto size = _validate_size(args);
        if (std::get<0>(size))
        {
            _resize(std::get<1>(size), std::get<2>(size));
            return false;
        }

        LOGE("View action interface: invalid arguments for resize");
        return true;
    } else if (name == "assign_workspace")
    {
        auto [ok, ws] = _validate_ws(args);
        if (ok)
        {
            _assign_ws(ws);
            return false;
        }

        return true;
    }

    LOGE("View action interface: Unsupported action execution requested. Name: ",
        name, ".");

    return true;
}

void view_action_interface_t::set_view(wayfire_view view)
{
    _nontoplevel = view;
    _view = toplevel_cast(view);
}

void view_action_interface_t::_maximize()
{
    wf::get_core().default_wm->tile_request(_view, wf::TILED_EDGES_ALL);
}

void view_action_interface_t::_unmaximize()
{
    wf::get_core().default_wm->tile_request(_view, 0);
}

void view_action_interface_t::_minimize()
{
    wf::get_core().default_wm->minimize_request(_view, true);
}

void view_action_interface_t::_unminimize()
{
    wf::get_core().default_wm->minimize_request(_view, false);
}

void view_action_interface_t::_make_sticky()
{
    _view->set_sticky(1);
}

void view_action_interface_t::_always_on_top()
{
    wf::wm_actions_set_above_state_signal data;

    auto output = _view->get_output();
    if (!output)
    {
        return;
    }

    data.view  = _view;
    data.above = true;
    output->emit(&data);
}

std::tuple<bool, float> view_action_interface_t::_expect_float(
    const std::vector<variant_t> & args, std::size_t position)
{
    if ((args.size() > position) && (wf::is_float(args.at(position))))
    {
        return {true, wf::get_float(args.at(position))};
    }

    return {false, 0.0f};
}

std::tuple<bool, double> view_action_interface_t::_expect_double(
    const std::vector<variant_t> & args, std::size_t position)
{
    if ((args.size() > position) && (wf::is_double(args.at(position))))
    {
        return {true, wf::get_double(args.at(position))};
    }

    return {false, 0.0};
}

std::tuple<bool, int> view_action_interface_t::_expect_int(
    const std::vector<variant_t> & args, std::size_t position)
{
    if ((args.size() > position) && (wf::is_int(args.at(position))))
    {
        return {true, wf::get_int(args.at(position))};
    }

    return {false, 0};
}

std::tuple<bool, float> view_action_interface_t::_validate_alpha(
    const std::vector<variant_t> & args)
{
    auto arg_float = _expect_float(args, 1);
    if (std::get<0>(arg_float))
    {
        return arg_float;
    } else
    {
        auto arg_double = _expect_double(args, 1);
        if (std::get<0>(arg_double))
        {
            return {true, static_cast<float>(std::get<1>(arg_double))};
        }
    }

    LOGE(
        "View action interface: Invalid arguments. Expected 'set alpha [float|double].");

    return {false, 1.0f};
}

std::tuple<bool, int, int, int, int> view_action_interface_t::_validate_geometry(
    const std::vector<variant_t> & args)
{
    auto arg_x = _expect_int(args, 1);
    auto arg_y = _expect_int(args, 2);
    auto arg_w = _expect_int(args, 3);
    auto arg_h = _expect_int(args, 4);

    if (std::get<0>(arg_x) && std::get<0>(arg_y) && std::get<0>(arg_w) &&
        std::get<0>(arg_h))
    {
        return {true, std::get<1>(arg_x), std::get<1>(arg_y), std::get<1>(arg_w),
            std::get<1>(arg_h)};
    }

    LOGE(
        "View action interface: Invalid arguments. Expected 'set geometry int int int int.");

    return {false, 0, 0, 0, 0};
}

std::tuple<bool, int, int> view_action_interface_t::_validate_position(
    const std::vector<variant_t> & args)
{
    auto arg_x = _expect_int(args, 0);
    auto arg_y = _expect_int(args, 1);

    if (std::get<0>(arg_x) && std::get<0>(arg_y))
    {
        return {true, std::get<1>(arg_x), std::get<1>(arg_y)};
    }

    LOGE("View action interface: Invalid arguments. Expected 'move int int.");

    return {false, 0, 0};
}

std::tuple<bool, int, int> view_action_interface_t::_validate_size(
    const std::vector<variant_t> & args)
{
    auto arg_w = _expect_int(args, 0);
    auto arg_h = _expect_int(args, 1);

    if (std::get<0>(arg_w) && std::get<0>(arg_h))
    {
        return {true, std::get<1>(arg_w), std::get<1>(arg_h)};
    }

    LOGE("View action interface: Invalid arguments. Expected 'resize int int.");

    return {false, 0, 0};
}

void view_action_interface_t::_set_alpha(float alpha)
{
    alpha = std::clamp(alpha, 0.1f, 1.0f);

    // Apply view transformer if needed and set alpha.
    auto tr = wf::ensure_named_transformer<wf::scene::view_2d_transformer_t>(
        _nontoplevel, wf::TRANSFORMER_2D, "alpha", _nontoplevel);
    if (fabs(tr->alpha - alpha) > FLT_EPSILON)
    {
        tr->alpha = alpha;
        _nontoplevel->damage();

        LOGI("View action interface: Alpha set to ", alpha, ".");
    }
}

void view_action_interface_t::_set_geometry(int x, int y, int w, int h)
{
    _resize(w, h);
    _move(x, y);
}

void view_action_interface_t::_set_geometry_ppt(int x, int y, int w, int h)
{
    auto output = _view->get_output();
    if (!output)
    {
        return;
    }

    auto og = output->get_relative_geometry();

    x = std::clamp(x, 0, 100);
    y = std::clamp(y, 0, 100);
    w = std::clamp(w, 0, 100);
    h = std::clamp(h, 0, 100);

    x = og.width * x / 100;
    y = og.height * y / 100;
    w = og.width * w / 100;
    h = og.height * h / 100;

    _resize(w, h);
    _move(x, y);
}

void view_action_interface_t::_start_on_output(std::string name)
{
    auto output = wf::get_core().output_layout->find_output(name);
    if (!output)
    {
        return;
    }

    if (_view->get_output() == output)
    {
        return;
    }

    move_view_to_output(_view, output, true);
}

std::tuple<bool, wf::point_t> view_action_interface_t::_validate_ws(
    const std::vector<variant_t>& args)
{
    if (!this->_view->get_output())
    {
        return {false, {}};
    }

    if (args.size() != 2)
    {
        LOGE("Invalid workspace identifier, expected <x> <y>");
    }

    auto [ok1, x] = _expect_int(args, 0);
    auto [ok2, y] = _expect_int(args, 1);
    if (!ok1 || !ok2)
    {
        LOGE("Workspace coordinates should be integers!");
        return {false, {}};
    }

    auto wsize = _view->get_output()->wset()->get_workspace_grid_size();
    if (((0 <= x) && (x < wsize.width)) && ((0 <= y) && (y < wsize.height)))
    {
        return {true, {x, y}};
    }

    LOGE("Workspace coordinates out of bounds!");
    return {false, {}};
}

wf::geometry_t view_action_interface_t::_get_workspace_grid_geometry(
    wf::output_t *output) const
{
    auto vsize = output->wset()->get_workspace_grid_size();
    auto vp    = output->wset()->get_current_workspace();
    auto res   = output->get_screen_size();

    return wf::geometry_t{
        -vp.x * res.width,
        -vp.y * res.height,
        vsize.width * res.width,
        vsize.height * res.height,
    };
}

void view_action_interface_t::_move(int x, int y)
{
    // Clamp x and y to sane values. Do not allow to move outside of workspace grid.
    auto output = _view->get_output();
    if (output != nullptr)
    {
        auto grid = this->_get_workspace_grid_geometry(output);
        auto view_geometry = _view->get_pending_geometry();
        view_geometry.x = x;
        view_geometry.y = y;

        view_geometry = wf::clamp(view_geometry, grid);
        _view->move(view_geometry.x, view_geometry.y);
    }
}

void view_action_interface_t::_resize(int w, int h)
{
    // Clamp w and h to sane values. Do not allow to get bigger then output. Do not
    // allow to get smaller then 40x30.
    auto output = _view->get_output();
    if (output != nullptr)
    {
        auto dimensions = output->get_screen_size();

        w = std::clamp(w, 40, dimensions.width);
        h = std::clamp(h, 30, dimensions.height);

        _view->resize(w, h);
    }
}

void view_action_interface_t::_assign_ws(wf::point_t point)
{
    auto output = _view->get_output();

    auto delta = point - output->wset()->get_current_workspace();
    auto size  = output->get_screen_size();

    auto wm = _view->get_pending_geometry();
    _view->move(wm.x + delta.x * size.width, wm.y + delta.y * size.height);
}
} // End namespace wf.

#ifndef VIEW_ACTION_INTERFACE_HPP
#define VIEW_ACTION_INTERFACE_HPP

#include "wayfire/action/action_interface.hpp"
#include "wayfire/toplevel-view.hpp"
#include <string>
#include <tuple>
#include <vector>

namespace wf
{
class view_action_interface_t : public action_interface_t
{
  public:
    virtual ~view_action_interface_t() override;

    virtual bool execute(const std::string & name,
        const std::vector<variant_t> & args) override;

    void set_view(wayfire_view view);

  private:
    void _maximize();
    void _unmaximize();
    void _minimize();
    void _unminimize();
    void _make_sticky();
    void _always_on_top();

    std::tuple<bool, float> _expect_float(const std::vector<variant_t> & args,
        std::size_t position);
    std::tuple<bool, double> _expect_double(const std::vector<variant_t> & args,
        std::size_t position);
    std::tuple<bool, int> _expect_int(const std::vector<variant_t> & args,
        std::size_t position);

    std::tuple<bool, float> _validate_alpha(const std::vector<variant_t> & args);
    std::tuple<bool, int, int, int, int> _validate_geometry(
        const std::vector<variant_t> & args);
    std::tuple<bool, int, int> _validate_position(
        const std::vector<variant_t> & args);
    std::tuple<bool, int, int> _validate_size(const std::vector<variant_t> & args);

    std::tuple<bool, wf::point_t> _validate_ws(const std::vector<variant_t>& args);

    void _set_alpha(float alpha);
    void _set_geometry(int x, int y, int w, int h);
    void _set_geometry_ppt(int x, int y, int w, int h);
    void _start_on_output(std::string output);
    void _move(int x, int y);
    void _resize(int w, int h);

    void _assign_ws(wf::point_t point);

    wf::geometry_t _get_workspace_grid_geometry(wf::output_t *output) const;

    wayfire_toplevel_view _view;
    wayfire_view _nontoplevel;
};
} // End namespace wf.

#endif // VIEW_ACTION_INTERFACE_HPP

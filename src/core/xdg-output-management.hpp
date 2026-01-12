#pragma once

#include <memory>
#include <map>
#include <vector>
#include "wayfire/object.hpp"
#include "xdg-output-unstable-v1-protocol.h"
#include <wayfire/output-layout.hpp>

namespace wf
{
class xdg_output_v1_resource;
class xdg_output_xwayland_geometry : public wf::custom_data_t
{
  public:
    std::optional<wf::geometry_t> geometry;
};

class xdg_output_manager_v1
{
  public:
    xdg_output_manager_v1(wl_display *display, wf::output_layout_t *layout);
    ~xdg_output_manager_v1();

  private:
    wl_global *global;
    std::map<wlr_output*, std::vector<std::unique_ptr<xdg_output_v1_resource>>> output_resources;
    wf::signal::connection_t<wf::output_layout_configuration_changed_signal> layout_change;

    void update_outputs();

    static void output_manager_bind(wl_client *wl_client, void *data, uint32_t version, uint32_t id);
    static void generic_handle_destroy(wl_client *client, wl_resource *resource);
    static void handle_output_resource_destroy(wl_resource *resource);
    static void handle_get_xdg_output(wl_client *client, wl_resource *resource, uint32_t id,
        wl_resource *output_resource);
    static const struct zxdg_output_manager_v1_interface output_manager_implementation;
};
} // namespace wf

#pragma once

#include <wayfire/view.hpp>

/**
 * Custom data to store on views the DBus address and object path set
 * by clients using the kde-appmenu protocol.
 */
class kde_appmenu_properties_t : public wf::custom_data_t
{
  public:
    std::optional<std::string> service_name;
    std::optional<std::string> object_path;
};

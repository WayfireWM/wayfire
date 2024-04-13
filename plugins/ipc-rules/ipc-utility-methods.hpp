#pragma once
#include "config.h"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/debug.hpp"
#include <wayfire/plugin.hpp>

namespace wf
{
class ipc_rules_utility_methods_t
{
  public:
    void init_utility_methods(ipc::method_repository_t *method_repository)
    {
        method_repository->register_method("wayfire/configuration", get_wayfire_configuration_info);
    }

    void fini_utility_methods(ipc::method_repository_t *method_repository)
    {
        method_repository->unregister_method("wayfire/configuration");
    }

    wf::ipc::method_callback get_wayfire_configuration_info = [=] (nlohmann::json)
    {
        nlohmann::json response;

        response["api-version"]    = WAYFIRE_API_ABI_VERSION;
        response["plugin-path"]    = PLUGIN_PATH;
        response["plugin-xml-dir"] = PLUGIN_XML_DIR;
        response["xwayland-support"] = WF_HAS_XWAYLAND;

        response["build-commit"] = wf::version::git_commit;
        response["build-branch"] = wf::version::git_branch;
        return response;
    };
};
}

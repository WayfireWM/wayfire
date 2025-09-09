#include "wayfire/core.hpp"
#include "wayfire/option-wrapper.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/nonstd/wlroots-full.hpp"
#include "wayfire/util/log.hpp"
#include <set>

class wayfire_security_context_v1 : public wf::plugin_interface_t
{
  public:
    void init() override
    {
        auto& core = wf::get_core();

        manager = wlr_security_context_manager_v1_create(core.display);
        if (!manager)
        {
            LOGE("Failed to create security context manager");
            return;
        }

        filter = core.create_global_filter();
        filter->set_filter([this] (const wl_client *client, const wl_global *global)
        {
            if (!is_privileged_protocol(global))
            {
                return true;
            }

            // Only allow clients without a sandbox engine to see globals, same as Sway's policy
            auto ctx = wlr_security_context_manager_v1_lookup_client(manager, client);
            if (ctx && ctx->sandbox_engine)
            {
                return false;
            }

            // Allow everything else
            return true;
        });

        update_privileged();
        privileged.set_callback([this] () { update_privileged(); });
    }

    bool is_privileged_protocol(const wl_global *global)
    {
        auto name = wl_global_get_interface(global)->name;
        return parsed_privileged.count(std::string(name));
    }

    void update_privileged()
    {
        parsed_privileged.clear();

        std::istringstream iss(privileged);
        std::string token;
        while (std::getline(iss, token, ','))
        {
            LOGD("Marking protocol \"", token, "\" as privileged");
            parsed_privileged.insert(token);
        }
    }

    void fini() override
    {
        // everything will be auto-destroyed.
    }

    bool is_unloadable() override
    {
        return false;
    }

  private:
    wlr_security_context_manager_v1 *manager = nullptr;
    std::unique_ptr<wf::wayland_global_filter_t> filter;

    wf::option_wrapper_t<std::string> privileged{"security-context-v1/privileged_protocols"};
    std::set<std::string> parsed_privileged;
};

DECLARE_WAYFIRE_PLUGIN(wayfire_security_context_v1);

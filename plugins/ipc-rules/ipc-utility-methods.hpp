#pragma once
#include "config.h"
#include "ipc-rules-common.hpp"
#include "plugins/ipc/ipc-method-repository.hpp"
#include "wayfire/debug.hpp"
#include <set>
#include <wayfire/plugin.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/config/compound-option.hpp>
#include <wayfire/config/config-manager.hpp>

extern "C" {
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
}

namespace wf
{
class ipc_rules_utility_methods_t
{
  private:
    wlr_backend *headless_backend = NULL;
    std::set<uint64_t> our_outputs;

  public:
    void init_utility_methods(ipc::method_repository_t *method_repository)
    {
        method_repository->register_method("wayfire/configuration", get_wayfire_configuration_info);
        method_repository->register_method("wayfire/create-headless-output", create_headless_output);
        method_repository->register_method("wayfire/destroy-headless-output", destroy_headless_output);
        method_repository->register_method("wayfire/get-config-option", get_config_option);
        method_repository->register_method("wayfire/set-config-options", set_config_options);
    }

    void fini_utility_methods(ipc::method_repository_t *method_repository)
    {
        method_repository->unregister_method("wayfire/configuration");
        method_repository->unregister_method("wayfire/create-headless-output");
        method_repository->unregister_method("wayfire/destroy-headless-output");
        method_repository->unregister_method("wayfire/get-config-option");
        method_repository->unregister_method("wayfire/set-config-option");
    }

    wf::ipc::method_callback get_wayfire_configuration_info = [=] (wf::json_t)
    {
        wf::json_t response;

        response["api-version"]    = WAYFIRE_API_ABI_VERSION;
        response["plugin-path"]    = PLUGIN_PATH;
        response["plugin-xml-dir"] = PLUGIN_XML_DIR;
        response["xwayland-support"] = WF_HAS_XWAYLAND;

        response["build-commit"] = wf::version::git_commit;
        response["build-branch"] = wf::version::git_branch;
        return response;
    };

    wf::ipc::method_callback create_headless_output = [=] (const wf::json_t& data)
    {
        auto width  = wf::ipc::json_get_uint64(data, "width");
        auto height = wf::ipc::json_get_uint64(data, "height");

        if (!headless_backend)
        {
            auto& core = wf::get_core();
            headless_backend = wlr_headless_backend_create(core.ev_loop);
            wlr_multi_backend_add(core.backend, headless_backend);
            wlr_backend_start(headless_backend);
        }

        auto handle = wlr_headless_add_output(headless_backend, width, height);
        auto wo     = wf::get_core().output_layout->find_output(handle);
        our_outputs.insert(wo->get_id());

        auto response = wf::ipc::json_ok();
        response["output"] = output_to_json(wo);
        return response;
    };

    wf::ipc::method_callback destroy_headless_output = [=] (const wf::json_t& data)
    {
        auto output    = wf::ipc::json_get_optional_string(data, "output");
        auto output_id = wf::ipc::json_get_optional_uint64(data, "output-id");

        if (!output.has_value() && !output_id.has_value())
        {
            return wf::ipc::json_error("Missing `output` or `output-id`!");
        }

        wf::output_t *wo = NULL;
        if (output.has_value())
        {
            wo = wf::get_core().output_layout->find_output(output.value());
        } else if (output_id.has_value())
        {
            wo = wf::ipc::find_output_by_id(output_id.value());
        }

        if (!wo)
        {
            return wf::ipc::json_error("Output not found!");
        }

        if (!our_outputs.count(wo->get_id()))
        {
            return wf::ipc::json_error("Output is not a headless output created from an IPC command!");
        }

        our_outputs.erase(wo->get_id());
        wlr_output_destroy(wo->handle);
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback get_config_option = [=] (const wf::json_t& data)
    {
        auto option_name = wf::ipc::json_get_string(data, "option");
        auto option = wf::get_core().config->get_option(option_name);
        if (!option)
        {
            return wf::ipc::json_error("Option not found!");
        }

        auto response = wf::ipc::json_ok();
        response["value"]   = option->get_value_str();
        response["default"] = option->get_default_value_str();
        return response;
    };

    std::string json_to_string(const wf::json_t& data)
    {
        if (data.is_string())
        {
            return data;
        }

        std::string buffer;
        data.map_serialized([&] (const char *src, size_t size)
        {
            buffer = std::string{src, size};
        });

        return buffer;
    }

    std::optional<std::string> add_compound_entry(const wf::json_t& entry,
        const std::string& entry_name,
        const wf::config::compound_option_t::entries_t& tuple_entries,
        std::vector<std::vector<std::string>>& values)
    {
        values.emplace_back();
        values.back().push_back(entry_name);

        if (!entry.is_object() && (tuple_entries.size() == 1))
        {
            auto str_value = json_to_string(entry);
            if (!tuple_entries[0]->is_parsable(str_value))
            {
                return "Failed to parse entry " + str_value;
            }

            values.back().push_back(str_value);
        } else if (entry.is_array())
        {
            // A simple tuple => copy one to one
            if (entry.size() != tuple_entries.size())
            {
                return "Number of entries does not match option type!";
            }

            for (size_t i = 0; i < entry.size(); i++)
            {
                auto str_value = json_to_string(entry[i]);
                if (!tuple_entries[i]->is_parsable(str_value))
                {
                    return "Failed to parse entry " + str_value;
                }

                values.back().push_back(str_value);
            }
        } else if (entry.is_object())
        {
            for (size_t i = 0; i < tuple_entries.size(); i++)
            {
                if (entry.has_member(tuple_entries[i]->get_name()))
                {
                    auto str_value = json_to_string(entry[tuple_entries[i]->get_name()]);
                    if (!tuple_entries[i]->is_parsable(str_value))
                    {
                        return "Failed to parse entry " + str_value;
                    }

                    values.back().push_back(str_value);
                } else if (tuple_entries[i]->get_default_value().has_value())
                {
                    values.back().push_back(tuple_entries[i]->get_default_value().value());
                } else
                {
                    return "Missing entry without default value " + tuple_entries[i]->get_name();
                }
            }
        } else
        {
            return "Compound entry must be an array or object";
        }

        return {};
    }

    std::optional<std::string> parse_compound_json(const wf::json_t& data,
        std::shared_ptr<config::compound_option_t> option)
    {
        std::vector<std::vector<std::string>> values;
        const auto& tuple_entries = option->get_entries();
        int counter = 0;

        if (data.is_array())
        {
            for (size_t i = 0; i < data.size(); i++)
            {
                std::string entry_name = "autogenerated" + std::to_string(counter++);
                if (auto err = add_compound_entry(data[i], entry_name, tuple_entries, values))
                {
                    return err;
                }
            }
        } else if (data.is_object())
        {
            for (auto& key : data.get_member_names())
            {
                if (auto err = add_compound_entry(data[key], key, tuple_entries, values))
                {
                    return err;
                }
            }
        } else
        {
            return "Compound value must be an array or object!";
        }

        option->set_value_untyped(values);
        return {};
    }

    wf::ipc::method_callback set_config_options = [=] (const wf::json_t& data)
        -> json_t
    {
        if (!data.is_object())
        {
            return wf::ipc::json_error("Options must be an object!");
        }

        for (auto& option : data.get_member_names())
        {
            auto opt = wf::get_core().config->get_option(option);
            if (!opt)
            {
                return wf::ipc::json_error(option + ": Option not found!");
            }

            if (auto compound = std::dynamic_pointer_cast<wf::config::compound_option_t>(opt))
            {
                auto error = parse_compound_json(data[option], compound);
                if (error.has_value())
                {
                    return wf::ipc::json_error(option + ": " + error.value());
                }
            } else
            {
                if (!opt->set_value_str(json_to_string(data[option])))
                {
                    return wf::ipc::json_error(option + ": Invalid value for option " +
                        std::string(json_to_string(data[option])) + "!");
                }
            }

            opt->set_locked(true);
        }

        reload_config_signal event;
        wf::get_core().emit(&event);
        return wf::ipc::json_ok();
    };
};
}

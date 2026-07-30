#include <wayfire/per-output-plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

extern "C"
{
#include <wlr/types/wlr_ext_workspace_v1.h>
}

namespace wf
{
struct workspace_handle_data
{
    point_t grid_pos;
};

class ext_workspaces_intergration;

class wlr_ext_workspaces_manager : public custom_data_t
{
  public:
    wlr_ext_workspace_manager_v1 *manager;
    wf::wl_listener_wrapper on_commit;

    wlr_ext_workspaces_manager()
    {
        manager = wlr_ext_workspace_manager_v1_create(wf::get_core().display, 1);
        on_commit.set_callback([&] (void *data)
        {
            handle_commit(static_cast<wlr_ext_workspace_v1_commit_event*>(data));
        });
        on_commit.connect(&manager->events.commit);
    }

    void handle_commit(wlr_ext_workspace_v1_commit_event *event);
};

class ext_workspaces_intergration : public wf::per_output_plugin_instance_t
{
    wf::shared_data::ref_ptr_t<wlr_ext_workspaces_manager> manager;
    wlr_ext_workspace_group_handle_v1 *group = nullptr;
    std::vector<std::vector<wlr_ext_workspace_handle_v1*>> workspaces;

    wf::signal::connection_t<wf::workspace_changed_signal> on_workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        if (ev->old_viewport.y < (int)workspaces.size() &&
            ev->old_viewport.x < (int)workspaces[ev->old_viewport.y].size())
        {
            wlr_ext_workspace_handle_v1_set_active(
                workspaces[ev->old_viewport.y][ev->old_viewport.x], false);
        }
        if (ev->new_viewport.y < (int)workspaces.size() &&
            ev->new_viewport.x < (int)workspaces[ev->new_viewport.y].size())
        {
            wlr_ext_workspace_handle_v1_set_active(
                workspaces[ev->new_viewport.y][ev->new_viewport.x], true);
        }
    };

    wf::signal::connection_t<wf::workspace_set_changed_signal> on_wset_changed =
        [=] (wf::workspace_set_changed_signal *ev)
    {
        on_grid_changed.disconnect();
        rebuild_workspaces();
        if (output->wset())
        {
            output->wset()->connect(&on_grid_changed);
        }
    };

    wf::signal::connection_t<wf::workspace_grid_changed_signal> on_grid_changed =
        [=] (wf::workspace_grid_changed_signal *ev)
    {
        rebuild_workspaces();
    };

    void rebuild_workspaces()
    {
        teardown_workspaces();
        setup_workspaces();
    }

    void setup_workspaces()
    {
        auto wset = output->wset();
        if (!wset) return;

        dimensions_t ws_dim = wset->get_workspace_grid_size();
        workspaces.resize(ws_dim.height,
            std::vector<wlr_ext_workspace_handle_v1*>(ws_dim.width, nullptr));

        for (int i = 0; i < ws_dim.height; i++)
        {
            for (int j = 0; j < ws_dim.width; j++)
            {
                std::string id =
                    output->to_string() + ":workspace-" +
                    std::to_string(j) + "-" + std::to_string(i);

                workspaces[i][j] = wlr_ext_workspace_handle_v1_create(manager->manager,
                    id.c_str(),
                    EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE |
                    EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN);
                wlr_ext_workspace_handle_v1_set_group(workspaces[i][j], group);
                wlr_ext_workspace_handle_v1_set_name(workspaces[i][j], id.c_str());

                uint32_t coords[2] = {(uint32_t)j, (uint32_t)i};
                wlr_ext_workspace_handle_v1_set_coordinates(
                    workspaces[i][j], coords, 2);

                auto *ws_data = new workspace_handle_data();
                ws_data->grid_pos = {j, i};
                workspaces[i][j]->data = ws_data;
            }
        }

        auto current = wset->get_current_workspace();
        if (current.y < ws_dim.height && current.x < ws_dim.width)
        {
            wlr_ext_workspace_handle_v1_set_active(
                workspaces[current.y][current.x], true);
        }
    }

    void teardown_workspaces()
    {
        for (auto& row : workspaces)
        {
            for (auto *ws : row)
            {
                delete static_cast<workspace_handle_data*>(ws->data);
                wlr_ext_workspace_handle_v1_destroy(ws);
            }
        }
        workspaces.clear();
    }

  public:
    void handle_activate(point_t grid_pos)
    {
        output->wset()->request_workspace(grid_pos);
    }

    void init() override
    {
        group = wlr_ext_workspace_group_handle_v1_create(manager->manager, 0);
        group->data = this;

        wlr_ext_workspace_group_handle_v1_output_enter(group, output->handle);

        setup_workspaces();

        output->connect(&on_workspace_changed);
        output->connect(&on_wset_changed);
        if (output->wset())
        {
            output->wset()->connect(&on_grid_changed);
        }
    }

    void fini() override
    {
        if (group)
        {
            group->data = nullptr;
        }

        teardown_workspaces();

        if (group)
        {
            wlr_ext_workspace_group_handle_v1_destroy(group);
            group = nullptr;
        }
    }
};

void wlr_ext_workspaces_manager::handle_commit(wlr_ext_workspace_v1_commit_event *event)
{
    wlr_ext_workspace_v1_request *req, *tmp;
    wl_list_for_each_safe(req, tmp, event->requests, link)
    {
        switch (req->type)
        {
          case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE:
          {
              auto *ws = req->activate.workspace;
              if (!ws || !ws->group) break;
              auto *ws_data = static_cast<workspace_handle_data*>(ws->data);
              auto *integration = static_cast<ext_workspaces_intergration*>(ws->group->data);
              if (ws_data && integration)
              {
                  integration->handle_activate(ws_data->grid_pos);
              }
              break;
          }
          case WLR_EXT_WORKSPACE_V1_REQUEST_CREATE_WORKSPACE:
            LOGD("Client requested workspace creation");
            break;
          case WLR_EXT_WORKSPACE_V1_REQUEST_REMOVE:
            LOGD("Client requested workspace removal");
            break;
          default:
            break;
        }
    }
}
class ext_workspaces_plugin_t : public wf::per_output_plugin_t<wf::ext_workspaces_intergration>
{
    bool is_unloadable() override
    {
        return false;
    }
};
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::ext_workspaces_plugin_t);

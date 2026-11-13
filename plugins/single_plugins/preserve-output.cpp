#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/plugin.hpp"
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/output.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

namespace wf
{
namespace preserve_output
{
static std::string make_output_identifier(wf::output_t *output)
{
    std::string identifier = "";
    identifier += nonull(output->handle->make);
    identifier += "|";
    identifier += nonull(output->handle->model);
    identifier += "|";
    identifier += nonull(output->handle->serial);
    return identifier;
}

/**
 * A view which was moved to a different output because its original output was disconnected. The view
 * remembers the geometry it had on its original output, so that it can be restored to its exact position when
 * the original output is reconnected.
 */
struct moved_view_t
{
    std::weak_ptr<wf::toplevel_view_interface_t> view;
    wf::geometry_t geometry;
};

struct per_output_state_t
{
    std::shared_ptr<wf::workspace_set_t> workspace_set;
    std::chrono::time_point<std::chrono::steady_clock> destroy_timestamp;
    bool was_focused = false;
    // Views which were evicted to another output when this output was disconnected.
    std::vector<moved_view_t> moved_views;
};

class preserve_output_t : public wf::plugin_interface_t
{
    wf::option_wrapper_t<int> last_output_focus_timeout{"preserve-output/last_output_focus_timeout"};
    std::map<std::string, per_output_state_t> saved_outputs;

    bool focused_output_expired(const per_output_state_t& state) const
    {
        using namespace std::chrono;
        const auto now = steady_clock::now();
        const auto elapsed_since_focus = duration_cast<milliseconds>(now - state.destroy_timestamp).count();
        return elapsed_since_focus > last_output_focus_timeout;
    }

    /**
     * Find an output to which views of a disconnected output can be moved. The active output is preferred, if
     * it is not the disconnected one.
     */
    wf::output_t *find_connected_output(wf::output_t *exclude)
    {
        wf::output_t *fallback = nullptr;
        for (auto output : wf::get_core().output_layout->get_outputs())
        {
            if ((output == exclude) || wlr_output_is_headless(output->handle))
            {
                continue;
            }

            if (output == wf::get_core().seat->get_active_output())
            {
                return output;
            }

            if (!fallback)
            {
                fallback = output;
            }
        }

        return fallback;
    }

    /**
     * Move all views of the saved workspace set to the given output, so that they remain accessible while
     * their original output is disconnected.
     */
    void move_views_to_output(per_output_state_t& data, wf::output_t *target)
    {
        auto source_wset = data.workspace_set;
        auto target_wset = target->wset();
        if (source_wset == target_wset)
        {
            return;
        }

        for (auto view : source_wset->get_views())
        {
            // Views which are not part of the workspace set's scenegraph node (for example,
            // pinned views) or which have a toplevel parent are left where they are.
            if ((view->get_root_node().get()->parent() != source_wset->get_node().get()) || view->parent)
            {
                continue;
            }

            // The view should end up on the same workspace coordinates on the target output.
            // If the target output has a smaller grid, the closest valid workspace is used.
            auto ws   = source_wset->get_view_main_workspace(view);
            auto grid = target_wset->get_workspace_grid_size();
            ws.x = std::max(0, std::min(ws.x, grid.width - 1));
            ws.y = std::max(0, std::min(ws.y, grid.height - 1));

            data.moved_views.push_back({view->weak_from_this(), view->get_geometry()});

            auto old_wset = view->get_wset();
            old_wset->remove_view(view);
            wf::scene::remove_child(view->get_root_node());
            wf::emit_view_pre_moved_to_wset_pre(view, old_wset, target_wset);
            if (view->get_output() != target)
            {
                view->set_output(target);
            }

            wf::scene::readd_front(target_wset->get_node(), view->get_root_node());
            target_wset->add_view(view);
            wf::emit_view_moved_to_wset(view, old_wset, target_wset);

            // Keep the view on the same workspace it was on its original output.
            target_wset->move_to_workspace(view, ws);
        }
    }

    /**
     * Move the views which were evicted from this output back to it, restoring their original position on the
     * output.
     */
    void move_views_back(per_output_state_t& data, wf::output_t *output)
    {
        for (auto& moved : data.moved_views)
        {
            auto ptr = moved.view.lock();
            if (!ptr)
            {
                // The view was destroyed while the output was disconnected.
                continue;
            }

            wayfire_toplevel_view view = ptr.get();
            auto old_wset = view->get_wset();
            if (old_wset == data.workspace_set)
            {
                continue;
            }

            if (old_wset)
            {
                old_wset->remove_view(view);
            }

            wf::scene::remove_child(view->get_root_node());
            wf::emit_view_pre_moved_to_wset_pre(view, old_wset, data.workspace_set);
            view->set_output(output);
            wf::scene::readd_front(data.workspace_set->get_node(), view->get_root_node());
            data.workspace_set->add_view(view);
            wf::emit_view_moved_to_wset(view, old_wset, data.workspace_set);

            if (!view->toplevel()->current().fullscreen)
            {
                view->set_geometry(moved.geometry);
            }
        }

        data.moved_views.clear();
    }

    void save_output(wf::output_t *output)
    {
        auto ident = make_output_identifier(output);
        auto& data = saved_outputs[ident];

        data.was_focused = (output == wf::get_core().seat->get_active_output());
        data.destroy_timestamp = std::chrono::steady_clock::now();
        data.workspace_set     = output->wset();
        data.moved_views.clear();

        LOGD("Saving workspace set ", data.workspace_set->get_index(), " from output ", output->to_string(),
            " with identifier ", ident);

        // Set a dummy workspace set with no views at all.
        output->set_workspace_set(wf::workspace_set_t::create());

        // Detach workspace set from its old output
        data.workspace_set->attach_to_output(nullptr);

        // Move the views to a connected output, so that they can still be accessed while
        // their original output is disconnected. They are moved back when the output is restored.
        if (auto target = find_connected_output(output))
        {
            LOGD("Moving views of disconnected output ", output->to_string(), " to ", target->to_string());
            move_views_to_output(data, target);
        }
    }

    void try_restore_output(wf::output_t *output)
    {
        std::string ident = make_output_identifier(output);
        if (!saved_outputs.count(ident))
        {
            LOGD("No saved identifier for ", output->to_string());
            return;
        }

        auto& data = saved_outputs[ident];

        auto new_output = data.workspace_set->get_attached_output();
        if (new_output && (new_output->wset() == data.workspace_set))
        {
            // The wset was moved to a different output => We should leave it where it is
            LOGD("Saved workspace for ", output->to_string(), " has been remapped to another output.");
            return;
        }

        LOGD("Restoring workspace set ", data.workspace_set->get_index(), " to output ", output->to_string());
        output->set_workspace_set(data.workspace_set);
        // Move back the views which were evicted to another output while this output was disconnected.
        move_views_back(data, output);
        if (data.was_focused && !focused_output_expired(data))
        {
            wf::get_core().seat->focus_output(output);
        }

        saved_outputs.erase(ident);
    }

    wf::signal::connection_t<output_pre_remove_signal> output_pre_remove = [=] (output_pre_remove_signal *ev)
    {
        if (wlr_output_is_headless(ev->output->handle))
        {
            // For example, NOOP-1
            return;
        }

        if (wf::get_core().get_current_state() == compositor_state_t::RUNNING)
        {
            LOGD("Received pre-remove event: ", ev->output->to_string());
            save_output(ev->output);
        }
    };

    wf::signal::connection_t<output_added_signal> on_new_output = [=] (output_added_signal *ev)
    {
        if (wlr_output_is_headless(ev->output->handle))
        {
            // For example, NOOP-1
            return;
        }

        try_restore_output(ev->output);
    };

  public:
    void init() override
    {
        wf::get_core().output_layout->connect(&on_new_output);
        wf::get_core().output_layout->connect(&output_pre_remove);
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::preserve_output::preserve_output_t);

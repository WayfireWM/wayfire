#pragma once

#include "wayfire/toplevel-view.hpp"
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workspace-set.hpp>

namespace wf
{
/**
 * This class encapsulates functionality related to handling fullscreen views on a given workspace set.
 * When a fullscreen view is at the top of the stack, it should be 'promoted' above the top layer, where
 * panels reside. This is done by temporarily disabling the top layer, and then re-enabling it when the
 * fullscreen view is no longer fullscreen or no longer on top of all other views.
 *
 * Note that only views from the workspace layer are promoted, and views in the layers above do not affect
 * the view promotion algorithm.
 */
class promotion_manager_t
{
  public:
    promotion_manager_t(wf::output_t *output)
    {
        this->output = output;
        wf::get_core().scene()->connect(&on_root_node_updated);
        output->connect(&on_view_fullscreen);
        output->connect(&on_view_unmap);
    }

  private:
    wf::output_t *output;

    wf::signal::connection_t<wf::scene::root_node_update_signal> on_root_node_updated = [=] (auto)
    {
        update_promotion_state();
    };

    signal::connection_t<view_unmapped_signal> on_view_unmap = [=] (view_unmapped_signal *ev)
    {
        update_promotion_state();
    };

    wf::signal::connection_t<wf::view_fullscreen_signal> on_view_fullscreen = [=] (auto)
    {
        update_promotion_state();
    };

    wayfire_toplevel_view find_top_visible_view(wf::scene::node_ptr root)
    {
        if (auto view = wf::node_to_view(root))
        {
            if (!view->is_mapped() || !toplevel_cast(view))
            {
                return nullptr;
            }

            if (output->wset()->view_visible_on(toplevel_cast(view), output->wset()->get_current_workspace()))
            {
                return toplevel_cast(view);
            }
        }

        for (auto& ch : root->get_children())
        {
            if (ch->is_enabled())
            {
                if (auto result = find_top_visible_view(ch))
                {
                    return result;
                }
            }
        }

        return nullptr;
    }

    void update_promotion_state()
    {
        wayfire_toplevel_view candidate = find_top_visible_view(output->wset()->get_node());
        if (candidate && candidate->toplevel()->current().fullscreen)
        {
            start_promotion();
        } else
        {
            stop_promotion();
        }
    }

    bool promotion_active = false;

    // When a fullscreen view is on top of the stack, it should be displayed above
    // nodes in the TOP layer. To achieve this effect, we hide the TOP layer.
    void start_promotion()
    {
        if (promotion_active)
        {
            return;
        }

        promotion_active = true;
        scene::set_node_enabled(output->node_for_layer(scene::layer::TOP), false);

        wf::fullscreen_layer_focused_signal ev;
        ev.has_promoted = true;
        output->emit(&ev);
        LOGD("autohide panels");
    }

    void stop_promotion()
    {
        if (!promotion_active)
        {
            return;
        }

        promotion_active = false;
        scene::set_node_enabled(output->node_for_layer(scene::layer::TOP), true);

        wf::fullscreen_layer_focused_signal ev;
        ev.has_promoted = false;
        output->emit(&ev);
        LOGD("restore panels");
    }
};
}

/* definition of filters for scale plugin and activator */

#ifndef SCALE_SIGNALS_H
#define SCALE_SIGNALS_H

#include <wayfire/object.hpp>
#include <wayfire/view.hpp>
#include <vector>
#include <algorithm>

/**
 * name: scale-filter
 * on: output
 * when: This signal is sent from the scale plugin whenever it is updating the
 *   list of views to display, with the list of views to be displayed in
 *   views_shown. Plugins can move views to views_hidden to request them not to
 *   be displayed by scale.
 *
 * Note: it is an error to remove a view from views_shown without adding it to
 *   views_hidden; this will result in views rendered in wrong locations.
 *
 * If multiple plugins are connected to this signal, they are called in the
 * order defined by the logic in signal_provider_t; plugins should not depend
 * on being called in a predictable order. Specifically, plugins should not
 * expect views_hidden to be empty (and should not call clear() on it). It is OK
 * for a plugin to move a view from views_hidden to views_shown, but this will
 * likely not have predictable results.
 */
struct scale_filter_signal
{
    std::vector<wayfire_toplevel_view>& views_shown;
    std::vector<wayfire_toplevel_view>& views_hidden;
    scale_filter_signal(std::vector<wayfire_toplevel_view>& shown,
        std::vector<wayfire_toplevel_view>& hidden) : views_shown(shown), views_hidden(hidden)
    {}
};

/* Convenience function for processing a list of views if the plugin wants to
 * filter based on a simple predicate. The predicate should return true for
 * views to be hidden. */
template<class pred>
void scale_filter_views(scale_filter_signal *signal, pred&& p)
{
    auto it = std::remove_if(signal->views_shown.begin(), signal->views_shown.end(),
        [signal, &p] (wayfire_toplevel_view v)
    {
        bool r = p(v);
        if (r)
        {
            signal->views_hidden.push_back(v);
        }

        return r;
    });
    signal->views_shown.erase(it, signal->views_shown.end());
}

/**
 * name: scale-end
 * on: output
 * when: When scale ended / is deactivated. A plugin performing filtering can
 *   connect to this signal to reset itself if filtering is not supposed to
 *   happen at the next activation of scale.
 * argument: unused
 */
struct scale_end_signal
{};

/**
 * name: scale-update
 * on: output
 * when: A plugin can emit this signal to request scale to be updated. This is
 *   intended for plugins that filter the scaled views to request an update when
 *   the filter is changed. It is a no-op if scale is not currently running.
 * argument: unused
 */
struct scale_update_signal
{};

/**
 * name: scale-transformer-added
 * on: output
 * when: This signal is emitted when scale adds a transformer to a view, so
 *   plugins extending its functionality can add their overlays to it.
 * argument: pointer to the newly added transformer
 */
struct scale_transformer_added_signal
{
    wayfire_toplevel_view view;
};

struct scale_transformer_removed_signal
{
    wayfire_toplevel_view view;
};

#endif

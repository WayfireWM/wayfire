#pragma once

#include <optional>
#include <wayfire/nonstd/wlroots.hpp>
#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include <wayfire/txn/transaction-object.hpp>
#include "wayfire/nonstd/wlroots.hpp" // IWYU pragma: keep

namespace wf
{
/**
 * Describes the size of the decoration frame around a toplevel.
 */
struct decoration_margins_t
{
    int left;
    int right;
    int bottom;
    int top;
};

struct toplevel_state_t
{
    /**
     * Mapped toplevel objects are ready to be presented to the user and can interact with input.
     * Unmapped toplevels usually are not displayed and do not interact with any plugins until they are mapped
     * at a later point in time.
     */
    bool mapped = false;

    /**
     * The geometry of the toplevel, as seen by the 'window manager'. This includes for example decorations,
     * but excludes shadows or subsurfaces sticking out of the main surface.
     */
    wf::geometry_t geometry = {100, 100, 0, 0};

    /**
     * A bitmask of WLR_EDGE_* values. Indicates the edge or corner of the toplevel which should stay immobile
     * if the client resizes in a way not indicated by the compositor.
     *
     * The default gravity is the top-left corner, which stays immobile if the client for example resizes
     * itself or does not obey a resize request sent by the compositor.
     */
    uint32_t gravity = WLR_EDGE_LEFT | WLR_EDGE_TOP;

    /**
     * The tiled edges of the toplevel.
     * Tiled edges are edges of the toplevel that are aligned to other objects (output edge, other toplevels,
     * etc.). Clients usually draw no shadows, rounded corners and similar decorations on tiled edges.
     *
     * Usually, when all edges are tiled, the toplevel is considered maximized.
     */
    uint32_t tiled_edges = 0;

    /**
     * The fullscreen state of the view. Fullscreen clients are typically shown above panels and take up the
     * full size of their primary output.
     */
    bool fullscreen = false;

    /**
     * The size of the server-side decorations around the view.
     *
     * Note that the margin values should be updated by decoration plugins before the toplevel state is
     * committed, for example during the new_transaction_signal. As a result, the pending margins are not
     * always meaningful for plugins, and they should avoid reading these values as they likely will not be
     * finalized before the view is actually committed.
     */
    decoration_margins_t margins = {0, 0, 0, 0};
};

/**
 * Toplevels are a kind of views which can be moved, resized and whose state can change (fullscreen, tiled,
 * etc). Most of the toplevel's attributes are double-buffered and are changed via transactions.
 */
class toplevel_t : public wf::txn::transaction_object_t, public wf::object_base_t
{
  public:
    /**
     * The current state of the toplevel, as was last committed by the client. The main surface's buffers
     * contents correspond to the current state.
     */
    const toplevel_state_t& current()
    {
        return _current;
    }

    /**
     * The committed state of the toplevel, that is, the state which the compositor has requested from the
     * client. This state may be different than the current state in case the client has not committed in
     * response to the compositor's request.
     */
    const toplevel_state_t& committed()
    {
        return _committed;
    }

    /**
     * The pending state of a toplevel. It may be changed by plugins. The pending state, however, will not be
     * applied until the toplevel is committed as a part of a transaction.
     */
    toplevel_state_t& pending()
    {
        return _pending;
    }

    /**
     * The minimum desirable size of the toplevel if set by the client.
     * If the client has not indicated a minimum size in either dimension, that dimension will be set to 0.
     */
    virtual wf::dimensions_t get_min_size()
    {
        return {0, 0};
    }

    /**
     * The maximum desirable size of the toplevel if set by the client.
     * If the client has not indicated a maximum size in either dimension, that dimension will be set to 0.
     */
    virtual wf::dimensions_t get_max_size()
    {
        return {0, 0};
    }

  protected:
    toplevel_state_t _current;
    toplevel_state_t _pending;
    toplevel_state_t _committed;

    std::optional<wf::geometry_t> last_windowed_geometry;
};

// Helper functions when working with toplevel state
inline wf::dimensions_t expand_dimensions_by_margins(wf::dimensions_t dim,
    const decoration_margins_t& margins)
{
    dim.width  += margins.left + margins.right;
    dim.height += margins.top + margins.bottom;
    return dim;
}

inline wf::dimensions_t shrink_dimensions_by_margins(wf::dimensions_t dim,
    const decoration_margins_t& margins)
{
    dim.width  -= margins.left + margins.right;
    dim.height -= margins.top + margins.bottom;
    return dim;
}

inline wf::geometry_t expand_geometry_by_margins(wf::geometry_t geometry, const decoration_margins_t& margins)
{
    geometry.x     -= margins.left;
    geometry.y     -= margins.top;
    geometry.width += margins.left + margins.right;
    geometry.height += margins.top + margins.bottom;
    return geometry;
}

inline wf::geometry_t shrink_geometry_by_margins(wf::geometry_t geometry, const decoration_margins_t& margins)
{
    geometry.x     += margins.left;
    geometry.y     += margins.top;
    geometry.width -= margins.left + margins.right;
    geometry.height -= margins.top + margins.bottom;
    return geometry;
}
}

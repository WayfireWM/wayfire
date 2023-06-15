#pragma once

#include <optional>
#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include <wayfire/txn/transaction-object.hpp>

namespace wf
{
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

  protected:
    toplevel_state_t _current;
    toplevel_state_t _pending;
    toplevel_state_t _committed;

    std::optional<wf::geometry_t> last_windowed_geometry;
};
}

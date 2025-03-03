#pragma once

#include <wlr/util/edges.h>
#include <cstdint>

class wayfire_resize;
class wayfire_decoration;
namespace wf
{
namespace grid
{
class grid_animation_t;
}

namespace tile
{
class view_node_t;
}

/**
 * A bitmask consisting of the top and bottom edges.
 * This corresponds to a vertically maximized state.
 */
constexpr uint32_t TILED_EDGES_VERTICAL =
    WLR_EDGE_TOP | WLR_EDGE_BOTTOM;

/**
 * A bitmask consisting of the left and right edges.
 * This corresponds to a horizontally maximized state.
 */
constexpr uint32_t TILED_EDGES_HORIZONTAL =
    WLR_EDGE_LEFT | WLR_EDGE_RIGHT;

/**
 * A bitmask consisting of all tiled edges.
 * This corresponds to a maximized state.
 */
constexpr uint32_t TILED_EDGES_ALL =
    TILED_EDGES_VERTICAL | TILED_EDGES_HORIZONTAL;

/**
 * Represents the maximization state (like in pending), not toggle.
 *
 * We can no longer use a boolean for "maximized" or not.
 * This class wraps two bits to represent being maximized vertically, horizontally or both.
 *
 * To test for vertical maximization (including full maximization), use:
 *
 *   maximization >= maximization_t::vertical
 *
 * To test if the maximization state is purely vertical and not horizontal, use:
 *
 *   maximization == maximization_t::vertical
 *
 * Likewise for horizontal.
 */
class maximization_t
{
  public:
    struct state_t
    {
        using mask_t = unsigned char;
        mask_t mask_;

        constexpr state_t operator |(state_t state) const
        {
            return {static_cast<mask_t>(mask_ | state.mask_)};
        }

        constexpr state_t operator &(state_t state) const
        {
            return {static_cast<mask_t>(mask_ & state.mask_)};
        }

        bool operator ==(state_t state) const
        {
            return mask_ == state.mask_;
        }

        bool operator !=(state_t state) const
        {
            return mask_ != state.mask_;
        }
    };

    static constexpr state_t none{0};
    static constexpr state_t vertical{1};
    static constexpr state_t horizontal{2};
    static constexpr state_t full{vertical.mask_ | horizontal.mask_};

  private:
    state_t state_;

    /**
     * Convert a tiled_edges bit mask to a maximization_t.
     */
    friend struct toplevel_state_t;
    maximization_t(uint32_t tiled_edges) : state_{
            ((tiled_edges & TILED_EDGES_VERTICAL) == TILED_EDGES_VERTICAL ? vertical : none) |
            ((tiled_edges & TILED_EDGES_HORIZONTAL) == TILED_EDGES_HORIZONTAL ? horizontal : none)}
    {}

  public:
    /**
     * Default constructor is a non-maximized state.
     */
    maximization_t() : state_{none}
    {}

    /**
     * Allow users to use maximization_t::full etc. where a maximization_t is required.
     */
    maximization_t(state_t state) : state_(state)
    {}

    /**
     * Add two states together.
     *
     * This is akin to a bit-wise OR.
     */
    maximization_t& operator +=(maximization_t maximization)
    {
        state_.mask_ |= maximization.state_.mask_;
        return *this;
    }

    /**
     * Remove maximization state.
     *
     * Clear the bits that are set in `maximization`.
     */
    maximization_t& operator -=(maximization_t maximization)
    {
        state_.mask_ &= ~maximization.state_.mask_;
        return *this;
    }

    /**
     * Toggle maximization state.
     */
    maximization_t& operator ^=(maximization_t maximization)
    {
        state_.mask_ ^= maximization.state_.mask_;
        return *this;
    }

    /**
     * Invert maximization state.
     */
    maximization_t operator ~() const
    {
        maximization_t inverted{*this};
        inverted ^= full;
        return inverted;
    }

    /**
     * Test contains maximization state.
     *
     * Returns true if the bits set in `maximization` are also set in this object.
     */
    bool operator >=(maximization_t maximization) const
    {
        return (state_ & maximization.state_) == maximization.state_;
    }

    /**
     * Test does not contain maximization state.
     *
     * Returns true if the bits set in `maximization` are not set in this object.
     */
    bool operator <(maximization_t maximization) const
    {
        return (state_ & maximization.state_) == none;
    }

    /**
     * Test equal.
     *
     * Returns true if the maximization states are the same.
     */
    bool operator ==(maximization_t maximization) const
    {
        return state_ == maximization.state_;
    }

    /**
     * Test unequal.
     *
     * Returns true if the maximization states are not the same.
     */
    bool operator !=(maximization_t maximization) const
    {
        return state_ != maximization.state_;
    }

    /**
     * Convert maximization_t to tiled_edges.
     */
    uint32_t as_tiled_edges() const
    {
        return ((state_ & vertical) !=
            none ? TILED_EDGES_VERTICAL : 0) | ((state_ & horizontal) != none ? TILED_EDGES_HORIZONTAL : 0);
    }
};
} // namespace wf

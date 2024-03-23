#ifndef ANIMATE_H_
#define ANIMATE_H_

#include <wayfire/view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/option-wrapper.hpp>

#define HIDING_ANIMATION (1 << 0)
#define SHOWING_ANIMATION (1 << 1)
#define MAP_STATE_ANIMATION (1 << 2)
#define MINIMIZE_STATE_ANIMATION (1 << 3)

enum wf_animation_type
{
    ANIMATION_TYPE_MAP      = SHOWING_ANIMATION | MAP_STATE_ANIMATION,
    ANIMATION_TYPE_UNMAP    = HIDING_ANIMATION | MAP_STATE_ANIMATION,
    ANIMATION_TYPE_MINIMIZE = HIDING_ANIMATION | MINIMIZE_STATE_ANIMATION,
    ANIMATION_TYPE_RESTORE  = SHOWING_ANIMATION | MINIMIZE_STATE_ANIMATION,
};

class animation_base
{
  public:
    virtual void init(wayfire_view view, wf::animation_description_t duration, wf_animation_type type);
    virtual bool step(); /* return true if continue, false otherwise */
    virtual void reverse(); /* reverse the animation */
    virtual int get_direction();

    animation_base() = default;
    virtual ~animation_base();
    animation_base(const animation_base &) = default;
    animation_base(animation_base &&) = default;
    animation_base& operator =(const animation_base&) = default;
    animation_base& operator =(animation_base&&) = default;
};

#endif

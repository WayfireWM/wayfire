#pragma once
#include <wayfire/option-wrapper.hpp>
#include <wayfire/util/duration.hpp>
#include <cmath>

namespace wf
{
using namespace wf::animation;
class geometry_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;

    timed_transition_t x{*this};
    timed_transition_t y{*this};
    timed_transition_t width{*this};
    timed_transition_t height{*this};

    void set_start(wf::geometry_t geometry)
    {
        copy_fields(geometry, &timed_transition_t::start);
    }

    void set_end(wf::geometry_t geometry)
    {
        copy_fields(geometry, &timed_transition_t::end);
    }

    operator wf::geometry_t() const
    {
        return {(double)x, (double)y, (double)width, (double)height};
    }

  protected:
    void copy_fields(wf::geometry_t geometry, double timed_transition_t::*member)
    {
        this->x.*member     = geometry.x;
        this->y.*member     = geometry.y;
        this->width.*member = geometry.width;
        this->height.*member = geometry.height;
    }
};

/** Interpolate the geometry between a and b with alpha (in [0..1]), i.e a *
 * (1-alpha) + b * alpha */
static inline wf::geometry_t interpolate(wf::geometry_t a, wf::geometry_t b,
    double alpha)
{
    const auto& interp = [=] (double wf::geometry_t::*member) -> double
    {
        return (1 - alpha) * a.*member + alpha * b.*member;
    };

    return {
        interp(&wf::geometry_t::x),
        interp(&wf::geometry_t::y),
        interp(&wf::geometry_t::width),
        interp(&wf::geometry_t::height)
    };
}
}

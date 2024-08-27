#ifndef WF_GEOMETRY_HPP
#define WF_GEOMETRY_HPP

#include <iosfwd>
#include <algorithm>

extern "C" {
#include <wlr/util/box.h>
}

namespace wf
{
struct point_t
{
    int x, y;
};

struct pointf_t
{
    double x, y;

    pointf_t() : x(0), y(0)
    {}
    pointf_t(double _x, double _y) : x(_x), y(_y)
    {}
    explicit pointf_t(const point_t& pt) : x(pt.x), y(pt.y)
    {}

    pointf_t operator +(const pointf_t& other) const
    {
        return pointf_t{x + other.x, y + other.y};
    }

    pointf_t operator -(const pointf_t& other) const
    {
        return pointf_t{x - other.x, y - other.y};
    }

    pointf_t& operator +=(const pointf_t& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    pointf_t& operator -=(const pointf_t& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    pointf_t operator -() const
    {
        return pointf_t{-x, -y};
    }

    point_t round_down() const
    {
        return point_t{static_cast<int>(x), static_cast<int>(y)};
    }
};

struct dimensions_t
{
    int32_t width;
    int32_t height;
};

using geometry_t = wlr_box;

point_t origin(const geometry_t& geometry);
dimensions_t dimensions(const geometry_t& geometry);
geometry_t construct_box(
    const wf::point_t& origin, const wf::dimensions_t& dimensions);

/* Returns the intersection of the two boxes, if the boxes don't intersect,
 * the resulting geometry has undefined (x,y) and width == height == 0 */
geometry_t geometry_intersection(const geometry_t& r1,
    const geometry_t& r2);

std::ostream& operator <<(std::ostream& stream, const wf::point_t& point);
std::ostream& operator <<(std::ostream& stream, const wf::pointf_t& pointf);
std::ostream& operator <<(std::ostream& stream, const wf::dimensions_t& dims);

bool operator ==(const wf::dimensions_t& a, const wf::dimensions_t& b);
bool operator !=(const wf::dimensions_t& a, const wf::dimensions_t& b);

bool operator ==(const wf::point_t& a, const wf::point_t& b);
bool operator !=(const wf::point_t& a, const wf::point_t& b);

wf::point_t operator +(const wf::point_t& a, const wf::point_t& b);
wf::point_t operator -(const wf::point_t& a, const wf::point_t& b);

wf::point_t operator -(const wf::point_t& a);

/** Return the closest valume to @value which is in [@min, @max] */
template<class T>
T clamp(T value, T min, T max)
{
    return std::min(std::max(value, min), max);
}

/**
 * Return the closest geometry to window which is completely inside the output.
 * The returned geometry might be smaller, but never bigger than window.
 */
geometry_t clamp(geometry_t window, geometry_t output);

// Transform a subbox from coordinate space A to coordinate space B.
// The returned subbox will occupy the same relative part of @B as
// @box occupies in @A.
wf::geometry_t scale_box(wf::geometry_t A, wf::geometry_t B, wf::geometry_t box);
}

bool operator ==(const wf::geometry_t& a, const wf::geometry_t& b);
bool operator !=(const wf::geometry_t& a, const wf::geometry_t& b);

wf::point_t operator +(const wf::point_t& a, const wf::geometry_t& b);
wf::geometry_t operator +(const wf::geometry_t & a, const wf::point_t& b);
wf::geometry_t operator -(const wf::geometry_t & a, const wf::point_t& b);

/** Scale the box */
wf::geometry_t operator *(const wf::geometry_t& box, double scale);

/* @return The length of the given vector */
double abs(const wf::point_t & p);

/* Returns true if point is inside rect */
bool operator &(const wf::geometry_t& rect, const wf::point_t& point);
/* Returns true if point is inside rect */
bool operator &(const wf::geometry_t& rect, const wf::pointf_t& point);
/* Returns true if the two geometries have a common point */
bool operator &(const wf::geometry_t& r1, const wf::geometry_t& r2);

/* Make geometry and point printable */
std::ostream& operator <<(std::ostream& stream, const wf::geometry_t& geometry);

#endif /* end of include guard: WF_GEOMETRY_HPP */

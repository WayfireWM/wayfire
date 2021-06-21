#ifndef WF_GEOMETRY_HPP
#define WF_GEOMETRY_HPP

#include <sstream>
#include <wayfire/nonstd/wlroots.hpp>

namespace wf
{
struct point_t
{
    int x, y;
};

struct pointf_t
{
    double x, y;
};

struct dimensions_t
{
    int32_t width;
    int32_t height;
};

using geometry_t = wlr_box;

point_t origin(const geometry_t& geometry);
dimensions_t dimensions(const geometry_t& geometry);

/* Returns the intersection of the two boxes, if the boxes don't intersect,
 * the resulting geometry has undefined (x,y) and width == height == 0 */
geometry_t geometry_intersection(const geometry_t& r1,
    const geometry_t& r2);

std::ostream& operator <<(std::ostream& stream, const wf::point_t& point);
std::ostream& operator <<(std::ostream& stream, const wf::pointf_t& pointf);

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
}

bool operator ==(const wf::geometry_t& a, const wf::geometry_t& b);
bool operator !=(const wf::geometry_t& a, const wf::geometry_t& b);

wf::point_t operator +(const wf::point_t& a, const wf::geometry_t& b);
wf::geometry_t operator +(const wf::geometry_t & a, const wf::point_t& b);

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

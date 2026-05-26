#ifndef WF_GEOMETRY_HPP
#define WF_GEOMETRY_HPP

#include <iosfwd>
#include <algorithm>

extern "C" {
#include <wlr/util/box.h>
}

namespace wf
{
struct geometryf_t
{
    double x, y;
    double width, height;
};

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
        return point_t{(int)std::floor(x), (int)std::floor(y)};
    }
};

struct dimensions_t
{
    int32_t width;
    int32_t height;
};

struct dimensionsf_t
{
    double width;
    double height;

    dimensionsf_t() : width(0), height(0)
    {}
    dimensionsf_t(double width, double height) : width(width), height(height)
    {}
    explicit dimensionsf_t(const dimensions_t& dims) : width(dims.width), height(dims.height)
    {}
};

using geometry_t = geometryf_t;
using framebuffer_box_t = wlr_box;

pointf_t origin(const geometry_t& geometry);
dimensions_t dimensions(const geometry_t& geometry);
dimensionsf_t fdimensions(const geometry_t& geometry);
dimensions_t containing_size(const dimensionsf_t& dimensions);
geometry_t construct_box(
    const wf::pointf_t& origin, const wf::dimensions_t& dimensions);
geometry_t construct_box(
    const wf::pointf_t& origin, const wf::dimensionsf_t& dimensions);

/* Returns the intersection of the two boxes, if the boxes don't intersect,
 * the resulting geometry has undefined (x,y) and width == height == 0 */
geometry_t geometry_intersection(const geometry_t& r1,
    const geometry_t& r2);

std::ostream& operator <<(std::ostream& stream, const wf::point_t& point);
std::ostream& operator <<(std::ostream& stream, const wf::pointf_t& pointf);
std::ostream& operator <<(std::ostream& stream, const wf::dimensions_t& dims);
std::ostream& operator <<(std::ostream& stream, const wf::dimensionsf_t& dims);

bool operator ==(const wf::dimensions_t& a, const wf::dimensions_t& b);
bool operator !=(const wf::dimensions_t& a, const wf::dimensions_t& b);

bool operator ==(const wf::dimensionsf_t& a, const wf::dimensionsf_t& b);
bool operator !=(const wf::dimensionsf_t& a, const wf::dimensionsf_t& b);

bool operator ==(const wf::point_t& a, const wf::point_t& b);
bool operator !=(const wf::point_t& a, const wf::point_t& b);

bool operator ==(const wf::pointf_t& a, const wf::pointf_t& b);
bool operator !=(const wf::pointf_t& a, const wf::pointf_t& b);

wf::point_t operator +(const wf::point_t& a, const wf::point_t& b);
wf::point_t operator -(const wf::point_t& a, const wf::point_t& b);

wf::point_t operator -(const wf::point_t& a);

/** Return the closest valume to @value which is in [@min, @max] */
template<class T>
T clamp(T value, T min, T max)
{
    return std::min(std::max(value, min), max);
}

template<class T, class U, class V>
auto clamp(T value, U min, V max)
{
    using R = std::common_type_t<T, U, V>;
    return wf::clamp<R>((R)value, (R)min, (R)max);
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

framebuffer_box_t to_framebuffer_box(const geometry_t& box);
geometry_t from_framebuffer_box(const framebuffer_box_t& box);
framebuffer_box_t containing_box(const geometry_t& box);
framebuffer_box_t containing_box(const wlr_fbox& box);

// Transform a subbox from coordinate space A to coordinate space B.
// The returned subbox will occupy the same relative part of @B as
// @box occupies in @A.
wlr_fbox scale_fbox(wlr_fbox A, wlr_fbox B, wlr_fbox box);

// Helper function to convert wf::geometry_t to wlr_fbox
wlr_fbox geometry_to_fbox(const geometry_t& geometry);

// Helper function to convert wlr_fbox to wf::geometry_t
geometry_t fbox_to_geometry(const wlr_fbox& fbox);
}

namespace wf
{
bool operator ==(const geometry_t& a, const geometry_t& b);
bool operator !=(const geometry_t& a, const geometry_t& b);

bool operator ==(const wlr_fbox& a, const wlr_fbox& b);
bool operator !=(const wlr_fbox& a, const wlr_fbox& b);

pointf_t operator +(const pointf_t& a, const geometry_t& b);
geometry_t operator +(const geometry_t& a, const pointf_t& b);
geometry_t operator -(const geometry_t& a, const pointf_t& b);
geometry_t operator +(const geometry_t& a, const framebuffer_box_t& b);
geometry_t operator -(const geometry_t& a, const framebuffer_box_t& b);

/** Scale the box */
geometry_t operator *(const geometry_t& box, double scale);
wlr_fbox operator *(const wlr_fbox& box, double scale);

/* @return The length of the given vector */
double abs(const pointf_t& p);

/* Returns true if point is inside rect */
bool operator &(const geometry_t& rect, const point_t& point);
/* Returns true if point is inside rect */
bool operator &(const geometry_t& rect, const pointf_t& point);
/* Returns true if the two geometries have a common point */
bool operator &(const geometry_t& r1, const geometry_t& r2);

/* Make geometry and point printable */
std::ostream& operator <<(std::ostream& stream, const geometry_t& geometry);
std::ostream& operator <<(std::ostream& stream, const wlr_fbox& geometry);
}

#endif /* end of include guard: WF_GEOMETRY_HPP */

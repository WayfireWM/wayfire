#include <wayfire/geometry.hpp>
#include <cmath>
#include <iomanip>

/* Geometry helpers */
std::ostream & wf::operator <<(std::ostream& stream, const wf::geometry_t& geometry)
{
    stream << '(' << geometry.x << ',' << geometry.y <<
        ' ' << geometry.width << 'x' << geometry.height << ')';

    return stream;
}

std::ostream& operator <<(std::ostream& stream, const wlr_box& geometry)
{
    stream << '(' << geometry.x << ',' << geometry.y <<
        ' ' << geometry.width << 'x' << geometry.height << ')';

    return stream;
}

std::ostream& wf::operator <<(std::ostream& stream, const wf::point_t& point)
{
    stream << '(' << point.x << ',' << point.y << ')';

    return stream;
}

std::ostream& wf::operator <<(std::ostream& stream, const wf::dimensions_t& dims)
{
    stream << dims.width << "x" << dims.height;
    return stream;
}

std::ostream& wf::operator <<(std::ostream& stream, const wf::dimensionsf_t& dims)
{
    stream << std::fixed << std::setprecision(4) << dims.width << 'x' << dims.height;
    return stream;
}

std::ostream& wf::operator <<(std::ostream& stream, const wf::pointf_t& pointf)
{
    stream << std::fixed << std::setprecision(4) <<
        '(' << pointf.x << ',' << pointf.y << ')';

    return stream;
}

wf::pointf_t wf::origin(const geometry_t& geometry)
{
    return {geometry.x, geometry.y};
}

wf::dimensions_t wf::dimensions(const geometry_t& geometry)
{
    return {(int)std::ceil(geometry.width), (int)std::ceil(geometry.height)};
}

wf::dimensions_t wf::dimensions(const wlr_box& geometry)
{
    return {geometry.width, geometry.height};
}

wf::dimensionsf_t wf::fdimensions(const geometry_t& geometry)
{
    return {geometry.width, geometry.height};
}

wf::dimensions_t wf::containing_size(const dimensionsf_t& dimensions)
{
    return {
        (int32_t)std::ceil(dimensions.width),
        (int32_t)std::ceil(dimensions.height),
    };
}

bool wf::operator ==(const wf::dimensions_t& a, const wf::dimensions_t& b)
{
    return a.width == b.width && a.height == b.height;
}

bool wf::operator !=(const wf::dimensions_t& a, const wf::dimensions_t& b)
{
    return !(a == b);
}

bool wf::operator ==(const wf::dimensionsf_t& a, const wf::dimensionsf_t& b)
{
    return a.width == b.width && a.height == b.height;
}

bool wf::operator !=(const wf::dimensionsf_t& a, const wf::dimensionsf_t& b)
{
    return !(a == b);
}

bool wf::operator ==(const wf::point_t& a, const wf::point_t& b)
{
    return a.x == b.x && a.y == b.y;
}

bool wf::operator !=(const wf::point_t& a, const wf::point_t& b)
{
    return !(a == b);
}

bool wf::operator ==(const wf::pointf_t& a, const wf::pointf_t& b)
{
    return a.x == b.x && a.y == b.y;
}

bool wf::operator !=(const wf::pointf_t& a, const wf::pointf_t& b)
{
    return !(a == b);
}

bool wf::operator ==(const wf::geometry_t& a, const wf::geometry_t& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool wf::operator !=(const wf::geometry_t& a, const wf::geometry_t& b)
{
    return !(a == b);
}

wf::point_t wf::operator +(const wf::point_t& a, const wf::point_t& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf::point_t wf::operator -(const wf::point_t& a, const wf::point_t& b)
{
    return {a.x - b.x, a.y - b.y};
}

wf::geometry_t wf::operator +(const wf::geometry_t & a, const wf::pointf_t& b)
{
    return {
        a.x + b.x,
        a.y + b.y,
        a.width,
        a.height
    };
}

wf::geometry_t wf::operator -(const wf::geometry_t & a, const wf::pointf_t& b)
{
    return a + wf::pointf_t{-b.x, -b.y};
}

wf::point_t wf::operator -(const wf::point_t& a)
{
    return {-a.x, -a.y};
}

wf::geometry_t wf::operator *(const wf::geometry_t& box, double scale)
{
    return {
        box.x * scale,
        box.y * scale,
        box.width * scale,
        box.height * scale,
    };
}

double wf::abs(const wf::pointf_t& p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}

bool wf::operator &(const wf::geometry_t& rect, const wf::point_t& point)
{
    return (rect.x <= point.x) && (point.x < rect.x + rect.width) &&
           (rect.y <= point.y) && (point.y < rect.y + rect.height);
}

bool wf::operator &(const wf::geometry_t& rect, const wf::pointf_t& point)
{
    return (rect.x <= point.x) && (point.x < rect.x + rect.width) &&
           (rect.y <= point.y) && (point.y < rect.y + rect.height);
}

bool wf::operator &(const wf::geometry_t& r1, const wf::geometry_t& r2)
{
    if ((r1.x + r1.width <= r2.x) || (r2.x + r2.width <= r1.x) ||
        (r1.y + r1.height <= r2.y) || (r2.y + r2.height <= r1.y))
    {
        return false;
    }

    return true;
}

wf::geometry_t wf::geometry_intersection(const wf::geometry_t& r1,
    const wf::geometry_t& r2)
{
    const double x1 = std::max(r1.x, r2.x);
    const double y1 = std::max(r1.y, r2.y);
    const double x2 = std::min(r1.x + r1.width, r2.x + r2.width);
    const double y2 = std::min(r1.y + r1.height, r2.y + r2.height);
    if ((x1 < x2) && (y1 < y2))
    {
        return {x1, y1, x2 - x1, y2 - y1};
    }

    return {0.0, 0.0, 0.0, 0.0};
}

wf::geometry_t wf::clamp(wf::geometry_t window, wf::geometry_t output)
{
    window.width  = wf::clamp(window.width, 0, output.width);
    window.height = wf::clamp(window.height, 0, output.height);
    window.x = wf::clamp(window.x, output.x, output.x + output.width - window.width);
    window.y = wf::clamp(window.y, output.y, output.y + output.height - window.height);

    return window;
}

wlr_box wf::clamp(wlr_box window, wlr_box output)
{
    window.width  = wf::clamp(window.width, 0, output.width);
    window.height = wf::clamp(window.height, 0, output.height);
    window.x = wf::clamp(window.x, output.x, output.x + output.width - window.width);
    window.y = wf::clamp(window.y, output.y, output.y + output.height - window.height);

    return window;
}

wf::geometry_t wf::construct_box(
    const wf::pointf_t& origin, const wf::dimensions_t& dimensions)
{
    return {
        origin.x, origin.y, (double)dimensions.width, (double)dimensions.height
    };
}

wf::geometry_t wf::construct_box(
    const wf::pointf_t& origin, const wf::dimensionsf_t& dimensions)
{
    return {
        origin.x, origin.y, dimensions.width, dimensions.height
    };
}

wf::framebuffer_box_t wf::to_integer_box(const geometry_t& box)
{
    return containing_box(box);
}

wf::geometry_t wf::from_integer_box(const framebuffer_box_t& box)
{
    return {(double)box.x, (double)box.y, (double)box.width, (double)box.height};
}

wf::framebuffer_box_t wf::containing_box(const geometry_t& box)
{
    int x  = (int)std::floor(box.x);
    int y  = (int)std::floor(box.y);
    int x2 = (int)std::ceil(box.x + box.width);
    int y2 = (int)std::ceil(box.y + box.height);

    return {
        .x     = x,
        .y     = y,
        .width = x2 - x,
        .height = y2 - y,
    };
}

wf::framebuffer_box_t wf::containing_box(const wlr_fbox& box)
{
    int x  = (int)std::floor(box.x);
    int y  = (int)std::floor(box.y);
    int x2 = (int)std::ceil(box.x + box.width);
    int y2 = (int)std::ceil(box.y + box.height);

    return {
        .x     = x,
        .y     = y,
        .width = x2 - x,
        .height = y2 - y,
    };
}

wf::geometry_t wf::scale_box(
    wf::geometry_t A, wf::geometry_t B, wf::geometry_t box)
{
    double scale_x = B.width / A.width;
    double scale_y = B.height / A.height;

    double x     = B.x + scale_x * (box.x - A.x);
    double y     = B.y + scale_y * (box.y - A.y);
    double width = scale_x * box.width;
    double height = scale_y * box.height;

    return geometry_t{
        .x     = x,
        .y     = y,
        .width = width,
        .height = height,
    };
}

#include <wayfire/geometry.hpp>
#include <cmath>
#include <iomanip>

/* Geometry helpers */
std::ostream& operator <<(std::ostream& stream, const wf::geometry_t& geometry)
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

std::ostream& wf::operator <<(std::ostream& stream, const wf::pointf_t& pointf)
{
    stream << std::fixed << std::setprecision(4) <<
        '(' << pointf.x << ',' << pointf.y << ')';

    return stream;
}

wf::point_t wf::origin(const geometry_t& geometry)
{
    return {geometry.x, geometry.y};
}

wf::dimensions_t wf::dimensions(const geometry_t& geometry)
{
    return {geometry.width, geometry.height};
}

bool wf::operator ==(const wf::dimensions_t& a, const wf::dimensions_t& b)
{
    return a.width == b.width && a.height == b.height;
}

bool wf::operator !=(const wf::dimensions_t& a, const wf::dimensions_t& b)
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

bool operator ==(const wf::geometry_t& a, const wf::geometry_t& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator !=(const wf::geometry_t& a, const wf::geometry_t& b)
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

wf::point_t operator +(const wf::point_t& a, const wf::geometry_t& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf::geometry_t operator +(const wf::geometry_t & a, const wf::point_t& b)
{
    return {
        a.x + b.x,
        a.y + b.y,
        a.width,
        a.height
    };
}

wf::geometry_t operator -(const wf::geometry_t & a, const wf::point_t& b)
{
    return a + -b;
}

wf::point_t wf::operator -(const wf::point_t& a)
{
    return {-a.x, -a.y};
}

wf::geometry_t operator *(const wf::geometry_t& box, double scale)
{
    wlr_box scaled;
    scaled.x = std::floor(box.x * scale);
    scaled.y = std::floor(box.y * scale);
    /* Scale it the same way that regions are scaled, otherwise
     * we get numerical issues. */
    scaled.width  = std::ceil((box.x + box.width) * scale) - scaled.x;
    scaled.height = std::ceil((box.y + box.height) * scale) - scaled.y;

    return scaled;
}

double abs(const wf::point_t& p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}

bool operator &(const wf::geometry_t& rect, const wf::point_t& point)
{
    return wlr_box_contains_point(&rect, point.x, point.y);
}

bool operator &(const wf::geometry_t& rect, const wf::pointf_t& point)
{
    return wlr_box_contains_point(&rect, point.x, point.y);
}

bool operator &(const wf::geometry_t& r1, const wf::geometry_t& r2)
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
    wlr_box result;
    if (wlr_box_intersection(&result, &r1, &r2))
    {
        return result;
    }

    return {0, 0, 0, 0};
}

wf::geometry_t wf::clamp(wf::geometry_t window, wf::geometry_t output)
{
    window.width  = wf::clamp(window.width, 0, output.width);
    window.height = wf::clamp(window.height, 0, output.height);

    window.x = wf::clamp(window.x,
        output.x, output.x + output.width - window.width);
    window.y = wf::clamp(window.y,
        output.y, output.y + output.height - window.height);

    return window;
}

wf::geometry_t wf::construct_box(
    const wf::point_t& origin, const wf::dimensions_t& dimensions)
{
    return {
        origin.x, origin.y, dimensions.width, dimensions.height
    };
}

wf::geometry_t wf::scale_box(
    wf::geometry_t A, wf::geometry_t B, wf::geometry_t box)
{
    // Figure out damage relative to the viewport
    double px  = 1.0 * (box.x - A.x) / A.width;
    double py  = 1.0 * (box.y - A.y) / A.height;
    double px2 = 1.0 * (box.x + box.width - A.x) / A.width;
    double py2 = 1.0 * (box.y + box.height - A.y) / A.height;

    int x = int(std::floor(B.x + B.width * px));
    int y = int(std::floor(B.y + B.height * py));

    int x2 = int(std::ceil(B.x + B.width * px2));
    int y2 = int(std::floor(B.y + B.height * py2));

    return wf::geometry_t{
        .x     = x,
        .y     = y,
        .width = x2 - x,
        .height = y2 - y,
    };
}

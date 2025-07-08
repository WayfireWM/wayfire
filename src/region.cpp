#include <wayfire/region.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

/* Pixman helpers */
wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box)
{
    return {
        box.x1, box.y1,
        box.x2 - box.x1,
        box.y2 - box.y1
    };
}

pixman_box32_t pixman_box_from_wlr_box(const wlr_box& box)
{
    return {
        box.x, box.y,
        box.x + box.width,
        box.y + box.height
    };
}

wf::region_t::region_t()
{
    pixman_region32_init(&_region);
}

wf::region_t::region_t(const pixman_region32_t *region) : wf::region_t()
{
    pixman_region32_copy(this->to_pixman(), region);
}

wf::region_t::region_t(const wlr_box& box)
{
    pixman_region32_init_rect(&_region, box.x, box.y, box.width, box.height);
}

wf::region_t::~region_t()
{
    pixman_region32_fini(&_region);
}

wf::region_t::region_t(const wf::region_t& other) : wf::region_t()
{
    pixman_region32_copy(this->to_pixman(), other.unconst());
}

wf::region_t::region_t(wf::region_t&& other) : wf::region_t()
{
    std::swap(this->_region, other._region);
}

wf::region_t& wf::region_t::operator =(const wf::region_t& other)
{
    if (&other == this)
    {
        return *this;
    }

    pixman_region32_copy(&_region, other.unconst());

    return *this;
}

wf::region_t& wf::region_t::operator =(wf::region_t&& other)
{
    if (&other == this)
    {
        return *this;
    }

    std::swap(_region, other._region);

    return *this;
}

bool wf::region_t::empty() const
{
    return !pixman_region32_not_empty(this->unconst());
}

void wf::region_t::clear()
{
    pixman_region32_clear(&_region);
}

void wf::region_t::expand_edges(int amount)
{
    /* FIXME: make sure we don't throw pixman errors when amount is bigger
     * than a rectangle size */
    pixman_region32_t *region = this->to_pixman();

    if (amount == 0)
    {
        return;
    }

    int nrects;
    const pixman_box32_t *src_rects = pixman_region32_rectangles(region, &nrects);

    pixman_box32_t *dst_rects = (pixman_box32_t*)malloc(nrects * sizeof(pixman_box32_t));
    if (dst_rects == NULL)
    {
        return;
    }

    for (int i = 0; i < nrects; ++i)
    {
        dst_rects[i].x1 = src_rects[i].x1 - amount;
        dst_rects[i].x2 = src_rects[i].x2 + amount;
        dst_rects[i].y1 = src_rects[i].y1 - amount;
        dst_rects[i].y2 = src_rects[i].y2 + amount;
        /* If x1 > x2 or y1 > y2, this is an invalid rect.
         * Set the rect members to 0 so it is skipped. */
        if ((dst_rects[i].x1 > dst_rects[i].x2) || (dst_rects[i].y1 > dst_rects[i].y2))
        {
            dst_rects[i].x1 = dst_rects[i].x2 = dst_rects[i].y1 = dst_rects[i].y2 = 0;
        }
    }

    pixman_region32_fini(region);
    pixman_region32_init_rects(region, dst_rects, nrects);
    free(dst_rects);
}

pixman_box32_t wf::region_t::get_extents() const
{
    return *pixman_region32_extents(this->unconst());
}

bool wf::region_t::contains_point(const wf::point_t& point) const
{
    return pixman_region32_contains_point(this->unconst(),
        point.x, point.y, NULL);
}

bool wf::region_t::contains_pointf(const wf::pointf_t& point) const
{
    for (auto& box : *this)
    {
        if ((box.x1 <= point.x) && (point.x < box.x2))
        {
            if ((box.y1 <= point.y) && (point.y < box.y2))
            {
                return true;
            }
        }
    }

    return false;
}

/* Translate the region */
wf::region_t wf::region_t::operator +(const wf::point_t& vector) const
{
    wf::region_t result{*this};
    pixman_region32_translate(&result._region, vector.x, vector.y);
    return result;
}

wf::region_t& wf::region_t::operator +=(const wf::point_t& vector)
{
    pixman_region32_translate(&_region, vector.x, vector.y);
    return *this;
}

wf::region_t wf::region_t::operator -(const wf::point_t& vector) const
{
    wf::region_t result{*this};
    pixman_region32_translate(&result._region, -vector.x, -vector.y);
    return result;
}

wf::region_t& wf::region_t::operator -=(const wf::point_t& vector)
{
    pixman_region32_translate(&_region, -vector.x, -vector.y);
    return *this;
}

wf::region_t wf::region_t::operator *(float scale) const
{
    wf::region_t result;
    wlr_region_scale(result.to_pixman(), this->unconst(), scale);

    return result;
}

wf::region_t& wf::region_t::operator *=(float scale)
{
    wlr_region_scale(this->to_pixman(), this->to_pixman(), scale);

    return *this;
}

/* Region intersection */
wf::region_t wf::region_t::operator &(const wlr_box& box) const
{
    wf::region_t result;
    pixman_region32_intersect_rect(result.to_pixman(), this->unconst(),
        box.x, box.y, box.width, box.height);

    return result;
}

wf::region_t wf::region_t::operator &(const wf::region_t& other) const
{
    wf::region_t result;
    pixman_region32_intersect(result.to_pixman(),
        this->unconst(), other.unconst());

    return result;
}

wf::region_t& wf::region_t::operator &=(const wlr_box& box)
{
    pixman_region32_intersect_rect(this->to_pixman(), this->to_pixman(),
        box.x, box.y, box.width, box.height);

    return *this;
}

wf::region_t& wf::region_t::operator &=(const wf::region_t& other)
{
    pixman_region32_intersect(this->to_pixman(),
        this->to_pixman(), other.unconst());

    return *this;
}

/* Region union */
wf::region_t wf::region_t::operator |(const wlr_box& other) const
{
    wf::region_t result;
    pixman_region32_union_rect(result.to_pixman(), this->unconst(),
        other.x, other.y, other.width, other.height);

    return result;
}

wf::region_t wf::region_t::operator |(const wf::region_t& other) const
{
    wf::region_t result;
    pixman_region32_union(result.to_pixman(), this->unconst(), other.unconst());

    return result;
}

wf::region_t& wf::region_t::operator |=(const wlr_box& other)
{
    pixman_region32_union_rect(this->to_pixman(), this->to_pixman(),
        other.x, other.y, other.width, other.height);

    return *this;
}

wf::region_t& wf::region_t::operator |=(const wf::region_t& other)
{
    pixman_region32_union(this->to_pixman(), this->to_pixman(), other.unconst());

    return *this;
}

/* Subtract the box/region from the current region */
wf::region_t wf::region_t::operator ^(const wlr_box& box) const
{
    wf::region_t result;
    wf::region_t sub{box};
    pixman_region32_subtract(result.to_pixman(), this->unconst(), sub.to_pixman());

    return result;
}

wf::region_t wf::region_t::operator ^(const wf::region_t& other) const
{
    wf::region_t result;
    pixman_region32_subtract(result.to_pixman(),
        this->unconst(), other.unconst());

    return result;
}

wf::region_t& wf::region_t::operator ^=(const wlr_box& box)
{
    wf::region_t sub{box};
    pixman_region32_subtract(this->to_pixman(),
        this->to_pixman(), sub.to_pixman());

    return *this;
}

wf::region_t& wf::region_t::operator ^=(const wf::region_t& other)
{
    pixman_region32_subtract(this->to_pixman(),
        this->to_pixman(), other.unconst());

    return *this;
}

pixman_region32_t*wf::region_t::to_pixman()
{
    return &_region;
}

const pixman_region32_t*wf::region_t::to_pixman() const
{
    return &_region;
}

pixman_region32_t*wf::region_t::unconst() const
{
    return const_cast<pixman_region32_t*>(&_region);
}

const pixman_box32_t*wf::region_t::begin() const
{
    int n;

    return pixman_region32_rectangles(unconst(), &n);
}

const pixman_box32_t*wf::region_t::end() const
{
    int n;
    auto data = pixman_region32_rectangles(unconst(), &n);

    return data + n;
}

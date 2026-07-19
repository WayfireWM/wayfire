#include <wayfire/region.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include <cstdlib>

/* Pixman helpers */
wf::geometry_t geometry_from_pixman_box(const pixman_box64f_t& box)
{
    return {
        box.x1, box.y1,
        box.x2 - box.x1,
        box.y2 - box.y1
    };
}

wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box)
{
    return {
        box.x1, box.y1,
        box.x2 - box.x1,
        box.y2 - box.y1
    };
}

wf::regionf_t::regionf_t()
{
    pixman_region64f_init(&_region);
}

wf::regionf_t::regionf_t(const pixman_region64f_t *region) : wf::regionf_t()
{
    pixman_region64f_copy(&_region, region);
}

wf::regionf_t::regionf_t(const wf::geometry_t& box)
{
    pixman_region64f_init_rectf(&_region, box.x, box.y, box.width, box.height);
}

wf::regionf_t::regionf_t(const wf::region_t& other) : regionf_t(other.to_pixman())
{}

wf::regionf_t::regionf_t(const pixman_region32_t *other)
{
    pixman_region64f_init(&_region);

    int nrects;
    const pixman_box32_t *src_rects = pixman_region32_rectangles(other, &nrects);
    for (int i = 0; i < nrects; ++i)
    {
        pixman_region64f_union_rectf(&_region, &_region,
            src_rects[i].x1,
            src_rects[i].y1,
            src_rects[i].x2 - src_rects[i].x1,
            src_rects[i].y2 - src_rects[i].y1);
    }
}

wf::regionf_t::~regionf_t()
{
    pixman_region64f_fini(&_region);
}

wf::regionf_t::regionf_t(const wf::regionf_t& other) : wf::regionf_t()
{
    pixman_region64f_copy(&_region, &other._region);
    pending_translations = other.pending_translations;
}

wf::regionf_t::regionf_t(wf::regionf_t&& other) : wf::regionf_t()
{
    std::swap(this->_region, other._region);
    std::swap(pending_translations, other.pending_translations);
}

wf::regionf_t& wf::regionf_t::operator =(const wf::regionf_t& other)
{
    if (&other == this)
    {
        return *this;
    }

    pixman_region64f_copy(&_region, &other._region);
    pending_translations = other.pending_translations;
    return *this;
}

wf::regionf_t& wf::regionf_t::operator =(wf::regionf_t&& other)
{
    if (&other == this)
    {
        return *this;
    }

    std::swap(_region, other._region);
    std::swap(pending_translations, other.pending_translations);
    return *this;
}

bool wf::regionf_t::empty() const
{
    return !pixman_region64f_not_empty(&_region);
}

void wf::regionf_t::clear()
{
    pixman_region64f_clear(&_region);
    pending_translations.clear();
}

void wf::regionf_t::expand_edges(double amount)
{
    pixman_region64f_t *region = &_region;
    if (amount == 0.0)
    {
        return;
    }

    int nrects;
    const pixman_box64f_t *src_rects = pixman_region64f_rectangles(region, &nrects);
    auto *dst_rects = (pixman_box64f_t*)malloc(nrects * sizeof(pixman_box64f_t));
    if (!dst_rects)
    {
        return;
    }

    for (int i = 0; i < nrects; ++i)
    {
        dst_rects[i].x1 = src_rects[i].x1 - amount;
        dst_rects[i].x2 = src_rects[i].x2 + amount;
        dst_rects[i].y1 = src_rects[i].y1 - amount;
        dst_rects[i].y2 = src_rects[i].y2 + amount;
        if ((dst_rects[i].x1 > dst_rects[i].x2) || (dst_rects[i].y1 > dst_rects[i].y2))
        {
            dst_rects[i].x1 = dst_rects[i].x2 = dst_rects[i].y1 = dst_rects[i].y2 = 0.0;
        }
    }

    pixman_region64f_clear(region);
    for (int i = 0; i < nrects; ++i)
    {
        pixman_region64f_union_rectf(region, region,
            dst_rects[i].x1,
            dst_rects[i].y1,
            dst_rects[i].x2 - dst_rects[i].x1,
            dst_rects[i].y2 - dst_rects[i].y1);
    }

    free(dst_rects);
}

pixman_box64f_t wf::regionf_t::get_extents() const
{
    auto extents = *pixman_region64f_extents(&_region);
    if (empty())
    {
        return extents;
    }

    auto translation = get_translation();
    extents.x1 += translation.x;
    extents.x2 += translation.x;
    extents.y1 += translation.y;
    extents.y2 += translation.y;
    return extents;
}

bool wf::regionf_t::contains_point(const wf::point_t& point) const
{
    auto translation = get_translation();
    return pixman_region64f_contains_pointf(&_region,
        point.x - translation.x, point.y - translation.y, NULL);
}

bool wf::regionf_t::contains_pointf(const wf::pointf_t& point) const
{
    auto translation = get_translation();
    return pixman_region64f_contains_pointf(&_region,
        point.x - translation.x, point.y - translation.y, NULL);
}

wf::regionf_t wf::regionf_t::operator +(const wf::pointf_t& vector) const
{
    wf::regionf_t result{*this};
    result.add_translation(vector);
    return result;
}

wf::regionf_t& wf::regionf_t::operator +=(const wf::pointf_t& vector)
{
    add_translation(vector);
    return *this;
}

wf::regionf_t wf::regionf_t::operator -(const wf::pointf_t& vector) const
{
    wf::regionf_t result{*this};
    result.add_translation({-vector.x, -vector.y});
    return result;
}

wf::regionf_t& wf::regionf_t::operator -=(const wf::pointf_t& vector)
{
    add_translation({-vector.x, -vector.y});
    return *this;
}

wf::regionf_t wf::regionf_t::operator *(double scale) const
{
    wf::regionf_t result;
    int count;
    auto rectangles = pixman_region64f_rectangles(&_region, &count);
    for (int i = 0; i < count; ++i)
    {
        pixman_region64f_union_rectf(&result._region, &result._region,
            rectangles[i].x1 * scale,
            rectangles[i].y1 * scale,
            (rectangles[i].x2 - rectangles[i].x1) * scale,
            (rectangles[i].y2 - rectangles[i].y1) * scale);
    }

    for (const auto& translation : pending_translations)
    {
        result.add_translation({translation.x * scale, translation.y * scale});
    }

    return result;
}

wf::regionf_t& wf::regionf_t::operator *=(double scale)
{
    *this = *this * scale;
    return *this;
}

wf::regionf_t wf::regionf_t::operator &(const wf::geometry_t& box) const
{
    wf::regionf_t result{*this};
    result &= box;
    return result;
}

wf::regionf_t wf::regionf_t::operator &(const wf::regionf_t& other) const
{
    wf::regionf_t result{*this};
    result &= other;
    return result;
}

wf::regionf_t& wf::regionf_t::operator &=(const wf::geometry_t& box)
{
    auto translation = get_translation();
    pixman_region64f_intersect_rectf(&_region, &_region,
        box.x - translation.x, box.y - translation.y, box.width, box.height);
    return *this;
}

wf::regionf_t& wf::regionf_t::operator &=(const wf::regionf_t& other)
{
    wf::regionf_t aligned{other};
    auto translation = get_storage_translation(other);
    pixman_region64f_translatef(&aligned._region, translation.x, translation.y);
    pixman_region64f_intersect(&_region, &_region, &aligned._region);
    return *this;
}

wf::regionf_t wf::regionf_t::operator |(const wf::geometry_t& other) const
{
    wf::regionf_t result{*this};
    result |= other;
    return result;
}

wf::regionf_t wf::regionf_t::operator |(const wf::regionf_t& other) const
{
    wf::regionf_t result{*this};
    result |= other;
    return result;
}

wf::regionf_t& wf::regionf_t::operator |=(const wf::geometry_t& other)
{
    auto translation = get_translation();
    pixman_region64f_union_rectf(&_region, &_region,
        other.x - translation.x, other.y - translation.y, other.width, other.height);
    return *this;
}

wf::regionf_t& wf::regionf_t::operator |=(const wf::regionf_t& other)
{
    wf::regionf_t aligned{other};
    auto translation = get_storage_translation(other);
    pixman_region64f_translatef(&aligned._region, translation.x, translation.y);
    pixman_region64f_union(&_region, &_region, &aligned._region);
    return *this;
}

wf::regionf_t wf::regionf_t::operator ^(const wf::geometry_t& box) const
{
    wf::regionf_t result{*this};
    result ^= box;
    return result;
}

wf::regionf_t wf::regionf_t::operator ^(const wf::regionf_t& other) const
{
    wf::regionf_t result{*this};
    result ^= other;
    return result;
}

wf::regionf_t& wf::regionf_t::operator ^=(const wf::geometry_t& box)
{
    auto translation = get_translation();
    wf::regionf_t sub{{box.x - translation.x, box.y - translation.y,
        box.width, box.height}};
    pixman_region64f_subtract(&_region, &_region, &sub._region);
    return *this;
}

wf::regionf_t& wf::regionf_t::operator ^=(const wf::regionf_t& other)
{
    wf::regionf_t aligned{other};
    auto translation = get_storage_translation(other);
    pixman_region64f_translatef(&aligned._region, translation.x, translation.y);
    pixman_region64f_subtract(&_region, &_region, &aligned._region);
    return *this;
}

void wf::regionf_t::add_translation(const wf::pointf_t& vector)
{
    if ((vector.x == 0.0) && (vector.y == 0.0))
    {
        return;
    }

    if (!pending_translations.empty() &&
        (pending_translations.back().x == -vector.x) &&
        (pending_translations.back().y == -vector.y))
    {
        pending_translations.pop_back();
    } else
    {
        pending_translations.push_back(vector);
    }
}

wf::pointf_t wf::regionf_t::get_translation() const
{
    long double x = 0.0;
    long double y = 0.0;
    for (const auto& translation : pending_translations)
    {
        x += translation.x;
        y += translation.y;
    }

    return {(double)x, (double)y};
}

wf::pointf_t wf::regionf_t::get_storage_translation(const wf::regionf_t& other) const
{
    long double x = 0.0;
    long double y = 0.0;
    for (const auto& translation : other.pending_translations)
    {
        x += translation.x;
        y += translation.y;
    }

    for (const auto& translation : pending_translations)
    {
        x -= translation.x;
        y -= translation.y;
    }

    return {(double)x, (double)y};
}

void wf::regionf_t::materialize() const
{
    if (pending_translations.empty())
    {
        return;
    }

    auto translation = get_translation();
    pixman_region64f_translatef(&_region, translation.x, translation.y);
    pending_translations.clear();
}

pixman_region64f_t*wf::regionf_t::to_pixman()
{
    materialize();
    return &_region;
}

const pixman_region64f_t*wf::regionf_t::to_pixman() const
{
    materialize();
    return &_region;
}

pixman_region64f_t*wf::regionf_t::unconst() const
{
    materialize();
    return &_region;
}

const pixman_box64f_t*wf::regionf_t::begin() const
{
    int n;
    return pixman_region64f_rectangles(unconst(), &n);
}

const pixman_box64f_t*wf::regionf_t::end() const
{
    int n;
    auto begin = pixman_region64f_rectangles(unconst(), &n);
    return begin + n;
}

wf::region_t::region_t()
{
    pixman_region32_init(&_region);
}

wf::region_t::region_t(const pixman_region32_t *region) : wf::region_t()
{
    pixman_region32_copy(this->to_pixman(), region);
}

wf::region_t::region_t(const wf::geometry_t& box) : wf::region_t()
{
    *this |= box;
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

wf::region_t& wf::region_t::operator =(const wf::geometry_t& other)
{
    clear();
    *this |= other;
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

    pixman_region32_clear(region);
    for (int i = 0; i < nrects; ++i)
    {
        pixman_region32_union_rect(region, region,
            dst_rects[i].x1,
            dst_rects[i].y1,
            dst_rects[i].x2 - dst_rects[i].x1,
            dst_rects[i].y2 - dst_rects[i].y1);
    }

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

wf::region_t wf::region_t::operator &(const wf::geometry_t& box) const
{
    return *this & wf::containing_box(box);
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

wf::region_t& wf::region_t::operator &=(const wf::geometry_t& box)
{
    return *this &= wf::containing_box(box);
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

wf::region_t wf::region_t::operator |(const wf::geometry_t& other) const
{
    return *this | wf::containing_box(other);
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

wf::region_t& wf::region_t::operator |=(const wf::geometry_t& other)
{
    return *this |= wf::containing_box(other);
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

wf::region_t wf::region_t::operator ^(const wf::geometry_t& box) const
{
    return *this ^ wf::containing_box(box);
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

wf::region_t& wf::region_t::operator ^=(const wf::geometry_t& box)
{
    return *this ^= wf::containing_box(box);
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

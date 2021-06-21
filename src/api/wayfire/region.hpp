#pragma once

#include <pixman.h>
#include "wayfire/geometry.hpp"

/* ---------------------- pixman utility functions -------------------------- */
namespace wf
{
struct region_t
{
    region_t();
    /* Makes a copy of the given region */
    region_t(pixman_region32_t *damage);
    region_t(const wlr_box& box);
    ~region_t();

    region_t(const region_t& other);
    region_t(region_t&& other);

    region_t& operator =(const region_t& other);
    region_t& operator =(region_t&& other);

    bool empty() const;
    void clear();

    void expand_edges(int amount);
    pixman_box32_t get_extents() const;
    bool contains_point(const point_t& point) const;
    bool contains_pointf(const pointf_t& point) const;

    /* Translate the region */
    region_t operator +(const point_t& vector) const;
    region_t& operator +=(const point_t& vector);

    region_t operator *(float scale) const;
    region_t& operator *=(float scale);

    /* Region intersection */
    region_t operator &(const wlr_box& box) const;
    region_t operator &(const region_t& other) const;
    region_t& operator &=(const wlr_box& box);
    region_t& operator &=(const region_t& other);

    /* Region union */
    region_t operator |(const wlr_box& other) const;
    region_t operator |(const region_t& other) const;
    region_t& operator |=(const wlr_box& other);
    region_t& operator |=(const region_t& other);

    /* Subtract the box/region from the current region */
    region_t operator ^(const wlr_box& box) const;
    region_t operator ^(const region_t& other) const;
    region_t& operator ^=(const wlr_box& box);
    region_t& operator ^=(const region_t& other);

    pixman_region32_t *to_pixman();

    const pixman_box32_t *begin() const;
    const pixman_box32_t *end() const;

  private:
    pixman_region32_t _region;
    /* Returns a const-casted pixman_region32_t*, useful in const operators
     * where we use this->_region as only source for calculations, but pixman
     * won't let us pass a const pixman_region32_t* */
    pixman_region32_t *unconst() const;
};
}

wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box);
pixman_box32_t pixman_box_from_wlr_box(const wlr_box& box);

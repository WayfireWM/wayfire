#pragma once

#include <pixman.h>
#include <vector>
#include "wayfire/geometry.hpp"

/* ---------------------- pixman utility functions -------------------------- */
namespace wf
{
struct region_t;
struct regionf_t
{
    regionf_t();
    regionf_t(const pixman_region64f_t *damage);
    regionf_t(const geometry_t& box);

    explicit regionf_t(const wf::region_t& other);
    explicit regionf_t(const pixman_region32_t *other);
    ~regionf_t();

    regionf_t(const regionf_t& other);
    regionf_t(regionf_t&& other);

    regionf_t& operator =(const regionf_t& other);
    regionf_t& operator =(regionf_t&& other);

    bool empty() const;
    void clear();

    void expand_edges(double amount);
    pixman_box64f_t get_extents() const;
    bool contains_point(const point_t& point) const;
    bool contains_pointf(const pointf_t& point) const;

    regionf_t operator +(const pointf_t& vector) const;
    regionf_t& operator +=(const pointf_t& vector);

    regionf_t operator -(const pointf_t& vector) const;
    regionf_t& operator -=(const pointf_t& vector);

    regionf_t operator *(double scale) const;
    regionf_t& operator *=(double scale);

    regionf_t operator &(const geometry_t& box) const;
    regionf_t operator &(const regionf_t& other) const;
    regionf_t& operator &=(const geometry_t& box);
    regionf_t& operator &=(const regionf_t& other);

    regionf_t operator |(const geometry_t& other) const;
    regionf_t operator |(const regionf_t& other) const;
    regionf_t& operator |=(const geometry_t& other);
    regionf_t& operator |=(const regionf_t& other);

    regionf_t operator ^(const geometry_t& box) const;
    regionf_t operator ^(const regionf_t& other) const;
    regionf_t& operator ^=(const geometry_t& box);
    regionf_t& operator ^=(const regionf_t& other);

    pixman_region64f_t *to_pixman();
    const pixman_region64f_t *to_pixman() const;

    const pixman_box64f_t *begin() const;
    const pixman_box64f_t *end() const;

  private:
    mutable pixman_region64f_t _region;
    mutable std::vector<pointf_t> pending_translations;

    void add_translation(const pointf_t& vector);
    pointf_t get_translation() const;
    pointf_t get_storage_translation(const regionf_t& other) const;
    void materialize() const;
    pixman_region64f_t *unconst() const;
};

struct region_t
{
    region_t();
    /* Makes a copy of the given region */
    region_t(const pixman_region32_t *damage);
    region_t(const geometry_t& box);
    region_t(const wlr_box& box);
    ~region_t();

    region_t(const region_t& other);
    region_t(region_t&& other);

    region_t& operator =(const geometry_t& other);
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

    region_t operator -(const point_t& vector) const;
    region_t& operator -=(const point_t& vector);

    region_t operator *(float scale) const;
    region_t& operator *=(float scale);

    /* Region intersection */
    region_t operator &(const wlr_box& box) const;
    region_t operator &(const geometry_t& box) const;
    region_t operator &(const region_t& other) const;
    region_t& operator &=(const wlr_box& box);
    region_t& operator &=(const geometry_t& box);
    region_t& operator &=(const region_t& other);

    /* Region union */
    region_t operator |(const wlr_box& other) const;
    region_t operator |(const geometry_t& other) const;
    region_t operator |(const region_t& other) const;
    region_t& operator |=(const wlr_box& other);
    region_t& operator |=(const geometry_t& other);
    region_t& operator |=(const region_t& other);

    /* Subtract the box/region from the current region */
    region_t operator ^(const wlr_box& box) const;
    region_t operator ^(const geometry_t& box) const;
    region_t operator ^(const region_t& other) const;
    region_t& operator ^=(const wlr_box& box);
    region_t& operator ^=(const geometry_t& box);
    region_t& operator ^=(const region_t& other);

    pixman_region32_t *to_pixman();
    const pixman_region32_t *to_pixman() const;

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

wf::geometry_t geometry_from_pixman_box(const pixman_box64f_t& box);
wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box);

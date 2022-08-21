#ifndef VIEW_TRANSFORM_HPP
#define VIEW_TRANSFORM_HPP

#include <wayfire/view.hpp>
#include <wayfire/opengl.hpp>

namespace wf
{
enum transformer_z_order_t
{
    /* Simple 2D transforms */
    TRANSFORMER_2D        = 1,
    /* 3D transforms */
    TRANSFORMER_3D        = 2,
    /* Highlevels transforms and above do special effects, for ex. wobbly or fire */
    TRANSFORMER_HIGHLEVEL = 500,
    /* Do not use Z order blur or more,
     * except if you are willing to break it */
    TRANSFORMER_BLUR      = 999,
};

class view_transformer_t
{
  public:
    /**
     * Get the Z ordering of the transformer, e.g the order in which it should
     * be applied relative to the other transformers on the same view.
     * Higher numbers indicate that the transform should be applied later.
     *
     * @return The Z order of the transformer.
     */
    virtual uint32_t get_z_order() = 0;

    /**
     * Transform the opaque region of the view.
     *
     * It must be guaranteed that the pixels part of the returned region are
     * opaque. The default implementation simply returns an empty region.
     *
     * @param box The bounding box of the view up to this transformer.
     * @param region The opaque region to transform.
     *
     * @return The transformed opaque region.
     */
    virtual wf::region_t transform_opaque_region(
        wf::geometry_t box, wf::region_t region);

    /**
     * Transform a single point.
     *
     * @param view The bounding box of the view, in output-local
     *   coordinates.
     * @param point The point to transform, in output-local coordinates.
     *
     * @return The point after transforming it, in output-local coordinates.
     */
    virtual wf::pointf_t transform_point(
        wf::geometry_t view, wf::pointf_t point) = 0;

    /**
     * Reverse the transformation of the point.
     *
     * @param view The bounding box of the view, in output-local
     *   coordinates.
     * @param point The point to untransform, in output-local coordinates.
     *
     * @return The point before after transforming it, in output-local
     *   coordinates. If a reversal of the transformation is not possible,
     *   return NaN.
     */
    virtual wf::pointf_t untransform_point(
        wf::geometry_t view, wf::pointf_t point) = 0;

    /**
     * Compute the bounding box of the given region after transforming it.
     *
     * @param view The bounding box of the view, in output-local
     *   coordinates.
     * @param region The region whose bounding box should be computed, in
     *   output-local coordinates.
     *
     * @return The bounding box of region after transforming it, in
     *   output-local coordinates.
     */
    virtual wlr_box get_bounding_box(wf::geometry_t view, wlr_box region);

    /**
     * Render the indicated parts of the view.
     *
     * @param src_tex The texture of the view.
     * @param src_box The bounding box of the view in output-local coordinates.
     * @param damage The region to repaint, clipped to the view's and
     *   the framebuffer's bounds. It is in output-local coordinates.
     * @param target_fb The framebuffer to draw the view to. It's geometry
     *   is in output-local coordinates.
     *
     * The default implementation of render_with_damage() will simply
     * iterate over all rectangles in the damage region, apply framebuffer
     * transform to it and then call render_box(). Plugins can override
     * either of the functions.
     */
    virtual void render_with_damage(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::render_target_t& target_fb);

    /** Same as render_with_damage(), but for a single rectangle of damage */
    virtual void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::render_target_t& target_fb)
    {}

    view_transformer_t() = default;
    virtual ~view_transformer_t() = default;
    view_transformer_t(const view_transformer_t &) = default;
    view_transformer_t(view_transformer_t &&) = default;
    view_transformer_t& operator =(const view_transformer_t&) = default;
    view_transformer_t& operator =(view_transformer_t&&) = default;
};

/* 2D transforms operate with a coordinate system centered at the
 * center of the main surface(the wayfire_view_t) */
class view_2D : public view_transformer_t
{
  protected:
    wayfire_view view;
    const uint32_t z_order;

  public:
    float angle = 0.0f;
    float scale_x = 1.0f, scale_y = 1.0f;
    float translation_x = 0.0f, translation_y = 0.0f;
    float alpha = 1.0f;

  public:
    view_2D(wayfire_view view, uint32_t z_order_ = TRANSFORMER_2D);

    virtual uint32_t get_z_order() override
    {
        return z_order;
    }

    wf::pointf_t transform_point(
        wf::geometry_t view, wf::pointf_t point) override;
    wf::pointf_t untransform_point(
        wf::geometry_t view, wf::pointf_t point) override;
    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::render_target_t& target_fb) override;
};

/* Those are centered relative to the view's bounding box */
class view_3D : public view_transformer_t
{
  protected:
    wayfire_view view;
    const uint32_t z_order;

  public:
    glm::mat4 view_proj{1.0}, translation{1.0}, rotation{1.0}, scaling{1.0};
    glm::vec4 color{1, 1, 1, 1};

    glm::mat4 calculate_total_transform();

  public:
    view_3D(wayfire_view view, uint32_t z_order_ = TRANSFORMER_3D);

    virtual uint32_t get_z_order() override
    {
        return z_order;
    }

    wf::pointf_t transform_point(
        wf::geometry_t view, wf::pointf_t point) override;
    wf::pointf_t untransform_point(
        wf::geometry_t view, wf::pointf_t point) override;
    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::render_target_t& target_fb) override;

    static const float fov; // PI / 8
    static glm::mat4 default_view_matrix();
    static glm::mat4 default_proj_matrix();
};

/* create a matrix which corresponds to the inverse of the given transform */
glm::mat4 get_output_matrix_from_transform(wl_output_transform transform);

/* a matrix which can be used to render wf::geometry_t directly */
glm::mat4 output_get_projection(wf::output_t *output);
}

#endif /* end of include guard: VIEW_TRANSFORM_HPP */

#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/core.hpp"
#include "wayfire/output.hpp"
#include <glm/ext/matrix_transform.hpp>
#include <string>
#include <tuple>
#include <wayfire/view.hpp>
#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

static const double PI = std::acos(-1);

namespace wf
{
wf::geometry_t get_bbox_for_node(scene::node_t *node, wf::geometry_t box)
{
    const auto p1 = node->to_global(wf::pointf_t(box.x, box.y));
    const auto p2 = node->to_global(wf::pointf_t(box.x + box.width, box.y));
    const auto p3 = node->to_global(wf::pointf_t(box.x, box.y + box.height));
    const auto p4 = node->to_global(
        wf::pointf_t(box.x + box.width, box.y + box.height));

    const int x1 = std::floor(std::min({p1.x, p2.x, p3.x, p4.x}));
    const int x2 = std::ceil(std::max({p1.x, p2.x, p3.x, p4.x}));
    const int y1 = std::floor(std::min({p1.y, p2.y, p3.y, p4.y}));
    const int y2 = std::ceil(std::max({p1.y, p2.y, p3.y, p4.y}));
    return wlr_box{x1, y1, x2 - x1, y2 - y1};
}

wf::geometry_t get_bbox_for_node(scene::node_ptr node, wf::geometry_t box)
{
    return get_bbox_for_node(node.get(), box);
}

namespace scene
{
void transform_manager_node_t::_add_transformer(
    wf::scene::floating_inner_ptr transformer, int z_order, std::string name)
{
    wf::scene::damage_node(shared_from_this(), get_bounding_box());
    size_t pos = 0;
    while (pos < transformers.size() && transformers[pos].z_order < z_order)
    {
        ++pos;
    }

    auto _parent = (pos == transformers.size() ?
        this->shared_from_this() : transformers[pos].node);
    auto parent = std::dynamic_pointer_cast<floating_inner_node_t>(_parent);
    transformers.insert(transformers.begin() + pos, added_transformer_t{
                .node    = transformer,
                .z_order = z_order,
                .name    = name,
            });

    auto children = parent->get_children();
    parent->set_children_list({transformer});
    transformer->set_children_list(children);
    wf::scene::update(transformer, update_flag::CHILDREN_LIST);
    wf::scene::damage_node(shared_from_this(), get_bounding_box());
}

void transform_manager_node_t::_rem_transformer(
    wf::scene::floating_inner_ptr node)
{
    if (!node)
    {
        return;
    }

    wf::scene::damage_node(shared_from_this(), get_bounding_box());
    auto children = node->get_children();
    auto parent   = dynamic_cast<floating_inner_node_t*>(node->parent());

    wf::dassert(parent != nullptr, "transformer is missing a parent?");
    node->set_children_list({});
    parent->set_children_list(children);

    const auto& find_node = [&] (auto transformer)
    {
        return transformer.node == node;
    };
    auto it = std::remove_if(transformers.begin(), transformers.end(), find_node);
    this->transformers.erase(it, transformers.end());
    wf::scene::update(parent->shared_from_this(), update_flag::CHILDREN_LIST);
    wf::scene::damage_node(shared_from_this(), get_bounding_box());
}

view_2d_transformer_t::view_2d_transformer_t(wayfire_view view) :
    transformer_base_node_t(false)
{
    this->view = view;
}

static wf::pointf_t get_center(wf::geometry_t view)
{
    return {
        view.x + view.width / 2.0,
        view.y + view.height / 2.0,
    };
}

static wf::pointf_t get_center(wayfire_view view)
{
    if (auto toplevel = toplevel_cast(view))
    {
        return get_center(toplevel->get_geometry());
    } else
    {
        return get_center(view->get_surface_root_node()->get_bounding_box());
    }
}

static void rotate_xy(double& x, double& y, double angle)
{
    const auto cs = std::cos(angle);
    const auto sn = std::sin(angle);
    std::tie(x, y) = std::make_tuple(cs * x - sn * y, sn * x + cs * y);
}

wf::pointf_t view_2d_transformer_t::to_local(const wf::pointf_t& point)
{
    auto midpoint = get_center(view);
    auto result   = point - midpoint;
    result.x -= translation_x;
    result.y -= translation_y;
    rotate_xy(result.x, result.y, angle);
    result.x /= scale_x;
    result.y /= scale_y;
    result   += midpoint;
    return result;
}

wf::pointf_t view_2d_transformer_t::to_global(const wf::pointf_t& point)
{
    auto midpoint = get_center(view);
    auto result   = point - midpoint;

    result.x *= scale_x;
    result.y *= scale_y;
    rotate_xy(result.x, result.y, -angle);
    result.x += translation_x;
    result.y += translation_y;

    return result + midpoint;
}

std::string view_2d_transformer_t::stringify() const
{
    return "view-2d for " + view->to_string();
}

wf::geometry_t view_2d_transformer_t::get_bounding_box()
{
    return get_bbox_for_node(this, get_children_bounding_box());
}

static void transform_linear_damage(node_t *self, wf::region_t& damage)
{
    auto copy = damage;
    damage.clear();
    for (auto& box : copy)
    {
        damage |=
            get_bbox_for_node(self, wlr_box_from_pixman_box(box));
    }
}

class view_2d_render_instance_t :
    public transformer_render_instance_t<view_2d_transformer_t>
{
  public:
    using transformer_render_instance_t::transformer_render_instance_t;

    void transform_damage_region(wf::region_t& damage) override
    {
        transform_linear_damage(self, damage);
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region) override
    {
        // Untransformed bounding box
        auto bbox = self->get_children_bounding_box();
        auto tex  = this->get_texture(target.scale);

        auto midpoint  = get_center(self->view);
        auto center_at = glm::translate(glm::mat4(1.0),
            {-midpoint.x, -midpoint.y, 0.0});
        auto scale = glm::scale(glm::mat4(1.0),
            glm::vec3{self->scale_x, self->scale_y, 1.0});
        auto rotate = glm::rotate<float>(glm::mat4(1.0), -self->angle,
            glm::vec3{0.0, 0.0, 1.0});
        auto translate = glm::translate(glm::mat4(1.0),
            glm::vec3{self->translation_x + midpoint.x,
                self->translation_y + midpoint.y, 0.0});
        auto ortho = target.get_orthographic_projection();
        auto full_matrix = ortho * translate * rotate * scale * center_at;

        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            // OpenGL::clear({1, 0, 0, 1});
            OpenGL::render_transformed_texture(tex, bbox, full_matrix,
                glm::vec4{1.0, 1.0, 1.0, self->alpha});
        }

        OpenGL::render_end();
    }
};

void view_2d_transformer_t::gen_render_instances(
    std::vector<render_instance_uptr>& instances, damage_callback push_damage,
    wf::output_t *shown_on)
{
    auto uptr =
        std::make_unique<view_2d_render_instance_t>(this, push_damage, shown_on);
    if (uptr->has_instances())
    {
        instances.push_back(std::move(uptr));
    }
}

/* -------------------------------- 3d view --------------------------------- */
const float view_3d_transformer_t::fov = PI / 4;
glm::mat4 view_3d_transformer_t::default_view_matrix()
{
    return glm::lookAt(
        glm::vec3(0., 0., 1.0 / std::tan(fov / 2)),
        glm::vec3(0., 0., 0.),
        glm::vec3(0., 1., 0.));
}

glm::mat4 view_3d_transformer_t::default_proj_matrix()
{
    return glm::perspective(fov, 1.0f, .1f, 100.f);
}

view_3d_transformer_t::view_3d_transformer_t(wayfire_view view) :
    scene::transformer_base_node_t(false)
{
    this->view = view;
    view_proj  = default_proj_matrix() * default_view_matrix();
}

static wf::pointf_t get_center_relative_coords(wf::geometry_t view,
    wf::pointf_t point)
{
    return {
        (point.x - view.x) - view.width / 2.0,
        view.height / 2.0 - (point.y - view.y)
    };
}

static wf::pointf_t get_absolute_coords_from_relative(wf::geometry_t view,
    wf::pointf_t point)
{
    return {
        point.x + view.x + view.width / 2.0,
        (view.height / 2.0 - point.y) + view.y
    };
}

/* TODO: cache total_transform, because it is often unnecessarily recomputed */
glm::mat4 view_3d_transformer_t::calculate_total_transform()
{
    auto bbox   = get_children_bounding_box();
    float scale = std::max(bbox.width, bbox.height);
    glm::mat4 depth_scale = glm::scale(glm::mat4(1.0), {1, 1, 2.0 / scale});
    return translation * view_proj * depth_scale * rotation * scaling;
}

wf::pointf_t view_3d_transformer_t::to_local(const wf::pointf_t& point)
{
    auto wm_geom = get_children_bounding_box();
    auto p  = get_center_relative_coords(wm_geom, point);
    auto tr = calculate_total_transform();

    /* Since we know that our original z coordinates were zero, we can write a
     * system of linear equations for the original (x,y) coordinates by writing
     * out the (x,y,w) components of the transformed coordinate.
     *
     * This results in the following matrix equation:
     * A x = b, where A and b are defined below and x is the vector
     * of untransformed coordinates that we want to compute. */
    glm::dmat2 A{p.x * tr[0][3] - tr[0][0], p.y * tr[0][3] - tr[0][1],
        p.x * tr[1][3] - tr[1][0], p.y * tr[1][3] - tr[1][1]};

    if (std::abs(glm::determinant(A)) < 1e-6)
    {
        /* This will happen if the transformed view is in rotated in a plane
         * perpendicular to the screen (i.e. it is displayed as a thin line).
         * We might want to add special casing for this so that the view can
         * still be "selected" in this case. */
        return {wf::compositor_core_t::invalid_coordinate,
            wf::compositor_core_t::invalid_coordinate};
    }

    glm::dvec2 b{tr[3][0] - p.x * tr[3][3], tr[3][1] - p.y * tr[3][3]};
    /* TODO: use a better solution formula instead of explicitly calculating the
     * inverse to have better numerical stability. For a 2x2 matrix, the
     * difference will be small though. */
    glm::dvec2 res = glm::inverse(A) * b;

    return get_absolute_coords_from_relative(wm_geom, {res.x, res.y});
}

wf::pointf_t view_3d_transformer_t::to_global(const wf::pointf_t& point)
{
    auto wm_geom = get_children_bounding_box();
    auto p = get_center_relative_coords(wm_geom, point);
    glm::vec4 v(1.0f * p.x, 1.0f * p.y, 0, 1);
    v = calculate_total_transform() * v;

    if (std::abs(v.w) < 1e-6)
    {
        /* This should never happen as long as we use well-behaving matrices.
         * However if we set transform to the zero matrix we might get
         * this case where v.w is zero. In this case we assume the view is
         * just a single point at 0,0 */
        v.x = v.y = 0;
    } else
    {
        v.x /= v.w;
        v.y /= v.w;
    }

    return get_absolute_coords_from_relative(wm_geom, {v.x, v.y});
}

std::string view_3d_transformer_t::stringify() const
{
    return "view-2d for " + view->to_string();
}

wf::geometry_t view_3d_transformer_t::get_bounding_box()
{
    return get_bbox_for_node(this, get_children_bounding_box());
}

struct transformable_quad
{
    gl_geometry geometry;
    float off_x, off_y;
};

static transformable_quad center_geometry(wf::geometry_t output_geometry,
    wf::geometry_t geometry,
    wf::pointf_t target_center)
{
    transformable_quad quad;

    geometry.x -= output_geometry.x;
    geometry.y -= output_geometry.y;

    target_center.x -= output_geometry.x;
    target_center.y -= output_geometry.y;

    quad.geometry.x1 = -(target_center.x - geometry.x);
    quad.geometry.y1 = (target_center.y - geometry.y);

    quad.geometry.x2 = quad.geometry.x1 + geometry.width;
    quad.geometry.y2 = quad.geometry.y1 - geometry.height;

    quad.off_x = (geometry.x - output_geometry.width / 2.0) - quad.geometry.x1;
    quad.off_y = (output_geometry.height / 2.0 - geometry.y) - quad.geometry.y1;

    return quad;
}

class view_3d_render_instance_t :
    public transformer_render_instance_t<view_3d_transformer_t>
{
  public:
    using transformer_render_instance_t::transformer_render_instance_t;


    void transform_damage_region(wf::region_t& damage) override
    {
        transform_linear_damage(self, damage);
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& damage) override
    {
        auto bbox = self->get_children_bounding_box();
        auto quad = center_geometry(target.geometry, bbox, scene::get_center(bbox));

        auto transform = self->calculate_total_transform();
        auto translate = glm::translate(glm::mat4(1.0), {quad.off_x, quad.off_y, 0});
        auto scale     = glm::scale(glm::mat4(1.0), {
                    2.0 / target.geometry.width,
                    2.0 / target.geometry.height,
                    1.0
                });

        transform = target.gl_to_framebuffer() * scale * translate * transform;
        auto tex = get_texture(target.scale);

        OpenGL::render_begin(target);
        for (auto& box : damage)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(tex, quad.geometry, {},
                transform, self->color);
        }

        OpenGL::render_end();
    }
};

void view_3d_transformer_t::gen_render_instances(
    std::vector<render_instance_uptr>& instances, damage_callback push_damage,
    wf::output_t *shown_on)
{
    auto uptr =
        std::make_unique<view_3d_render_instance_t>(this, push_damage, shown_on);
    if (uptr->has_instances())
    {
        instances.push_back(std::move(uptr));
    }
}

void transform_manager_node_t::begin_transform_update()
{
    wf::scene::damage_node(this, get_bounding_box());
}

void transform_manager_node_t::end_transform_update()
{
    wf::scene::damage_node(this, get_bounding_box());
    wf::scene::update(shared_from_this(), wf::scene::update_flag::GEOMETRY);
}
} // namespace scene
}

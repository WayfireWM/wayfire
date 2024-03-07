#ifndef VIEW_TRANSFORM_HPP
#define VIEW_TRANSFORM_HPP

#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/region.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include <memory>
#include <wayfire/opengl.hpp>

namespace wf
{
namespace scene
{
class zero_copy_texturable_node_t
{
  public:
    virtual ~zero_copy_texturable_node_t() = default;

    /**
     * Get a texture from the node without copying.
     * Note that this operation might fail for non-trivial transformers.
     */
    virtual std::optional<wf::texture_t> to_texture() const
    {
        return {};
    }
};

class opaque_region_node_t
{
  public:
    virtual ~opaque_region_node_t() = default;

    /**
     * Get the opaque region of the node in its parent's coordinate system (same as get_bounding_box()).
     */
    virtual wf::region_t get_opaque_region() const
    {
        return {};
    }
};

/**
 * A base class for all transformer nodes.
 * It facilitates the reuse of auxilliary buffers between render instances.
 */
class transformer_base_node_t : public scene::floating_inner_node_t
{
  public:
    using floating_inner_node_t::floating_inner_node_t;

    // A temporary buffer to render children to.
    wf::render_target_t inner_content;

    // Damage from the children, which is the region of @inner_content that
    // should be repainted on the next frame to have a valid copy of the
    // children's current content.
    wf::region_t cached_damage;

    wf::texture_t get_updated_contents(const wf::geometry_t& bbox, float scale,
        std::vector<scene::render_instance_uptr>& children)
    {
        int target_width  = scale * bbox.width;
        int target_height = scale * bbox.height;

        OpenGL::render_begin();
        inner_content.scale = scale;
        if (inner_content.allocate(target_width, target_height))
        {
            cached_damage |= bbox;
        }

        inner_content.geometry = bbox;
        OpenGL::render_end();

        render_pass_params_t params;
        params.instances = &children;
        params.target    = inner_content;
        params.damage    = cached_damage;
        params.background_color = {0.0f, 0.0f, 0.0f, 0.0f};
        scene::run_render_pass(params, RPASS_CLEAR_BACKGROUND);

        cached_damage.clear();
        return wf::texture_t{inner_content.tex};
    }

    void release_buffers()
    {
        if (inner_content.fb != (uint) - 1)
        {
            // Release the inner_content buffer, because we are on
            // the zero-copy path and we do not need an auxiliary
            // buffer to render to.
            OpenGL::render_begin();
            inner_content.release();
            OpenGL::render_end();
        }
    }

    ~transformer_base_node_t()
    {
        release_buffers();
    }
};

/**
 * A helper class for implementing transformer nodes.
 * Transformer nodes usually operate on views and implement special effects, like
 * for example rotating a view, blurring the background, etc.
 *
 * To allow arbitrary combinations of transformers, the different transformers are
 * arranged so that they build a chain where each transformer is the child of the
 * previous transformer, and the child of the last transformer is the view's
 * surface root node. For the actual composition of effects, every transformer
 * first renders its children (with the transformation which comes from the next
 * transformers in the chain) to a temporary buffer and then renders the temporary
 * buffer with the node's own transform applied.
 *
 * @param NodeType the concrete type of the node this instance belongs to, must be
 *   a subclass of transformer_base_node_t.
 */
template<class NodeType>
class transformer_render_instance_t : public render_instance_t
{
  protected:
    std::optional<wf::texture_t> zero_copy_texture()
    {
        if (self->get_children().size() == 1)
        {
            auto child = self->get_children().front().get();
            if (auto zcopy = dynamic_cast<zero_copy_texturable_node_t*>(child))
            {
                return zcopy->to_texture();
            }
        }

        return {};
    }

    // A pointer to the transformer node this render instance belongs to.
    NodeType *self;

    // A list of render instances of the next transformer or the view itself.
    std::vector<render_instance_uptr> children;

    /**
     * Get a texture which contains the contents of the children nodes.
     * If the node has a single child which supports zero-copy texture generation
     * via @to_texture, that method is preferred to avoid unnecessary copies.
     *
     * Otherwise, the children are rendered to an auxiliary buffer (@inner_content),
     * whose texture is returned.
     *
     * @param scale The scale to use when generating the texture. The scale
     *   indicates how much bigger the temporary buffer should be than its logical
     *   size.
     */
    wf::texture_t get_texture(float scale)
    {
        // Optimization: if we have a single child (usually the surface root node)
        // and we can directly convert it to texture, we don't need a full render
        // pass.
        if (auto tex = zero_copy_texture())
        {
            self->release_buffers();
            return *tex;
        }

        return self->get_updated_contents(self->get_children_bounding_box(), scale, children);
    }

    void presentation_feedback(wf::output_t *output) override
    {
        for (auto& ch : children)
        {
            ch->presentation_feedback(output);
        }
    }

    virtual void transform_damage_region(wf::region_t& damage)
    {}

  public:
    transformer_render_instance_t(NodeType *self, damage_callback push_damage,
        wf::output_t *shown_on)
    {
        static_assert(std::is_base_of_v<node_t, NodeType>,
            "transformer_render_instance_t should be instantiated with a "
            "subclass of node_t!");

        this->self = self;
        auto push_damage_child = [=] (wf::region_t region)
        {
            self->cached_damage |= region;
            transform_damage_region(region);
            push_damage(region);
        };

        self->cached_damage |= self->get_children_bounding_box();
        for (auto& ch : self->get_children())
        {
            ch->gen_render_instances(children, push_damage_child, shown_on);
        }
    }

    ~transformer_render_instance_t()
    {}

    void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (!damage.empty())
        {
            auto our_damage = damage & self->get_bounding_box();
            instructions.push_back(wf::scene::render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = std::move(our_damage),
                    });
        }
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& damage) override
    {
        wf::dassert(false, "Rendering not implemented for view transformer?");
    }

    direct_scanout try_scanout(wf::output_t *output) override
    {
        // By default, disable direct scanout
        return direct_scanout::OCCLUSION;
    }

    bool has_instances()
    {
        return !children.empty();
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        if (!(visible & self->get_bounding_box()).empty())
        {
            // By default, we are not sure how the visibility region is affected, so we take a simple 0-or-1
            // approach: if anything of the bounding box is visible, we assume the whole view is visible, and
            // we do not subtract anything from the visibility region of the nodes below.
            wf::region_t copy = self->get_children_bounding_box();
            for (auto& ch : this->children)
            {
                ch->compute_visibility(output, copy);
            }
        }
    }
};

/**
 * A floating inner node which contains a chain of view transformers and a view
 * surface root node at the bottom of the chain. Its interface can be used to
 * add and sort view transformers.
 */
class transform_manager_node_t : public wf::scene::floating_inner_node_t
{
  public:
    transform_manager_node_t() : floating_inner_node_t(false)
    {}

    /**
     * Marks a section of the code which updates one or more transformers added to this transform manager.
     * Doing so will ensure that the proper damage is propagated upwards in the scenegraph.
     */
    void begin_transform_update();

    /**
     * Marks the end of a section of the code which updates one or more transformers added to this transform
     * manager. Doing so will ensure that the proper damage is propagated upwards in the scenegraph.
     */
    void end_transform_update();

    /**
     * Add a new transformer in the transformer chain.
     *
     * @param transformer The transformer to be added.
     * @param z_order The order of this transformer relative to other transformers.
     *   Smaller values indicate that the transformer should be applied before
     *   others, see @transformer_z_order_t.
     * @param name A string which can be used as an ID for easily getting a
     *   transformer node or removing it.
     */
    template<class ConcreteTransformer>
    void add_transformer(std::shared_ptr<ConcreteTransformer> transformer,
        int z_order, std::string name = typeid(ConcreteTransformer).name())
    {
        _add_transformer(transformer, z_order, name);
    }

    template<class ConcreteTransformer>
    void rem_transformer(std::shared_ptr<ConcreteTransformer> transformer)
    {
        _rem_transformer(transformer);
    }

    template<class ConcreteTransformer = wf::scene::floating_inner_node_t>
    void rem_transformer(std::string name = typeid(ConcreteTransformer).name())
    {
        _rem_transformer(get_transformer<ConcreteTransformer>(name));
    }

    /**
     * Find a transformer with the given name and type.
     */
    template<class ConcreteTransformer = wf::scene::floating_inner_node_t>
    std::shared_ptr<ConcreteTransformer> get_transformer(
        std::string name = typeid(ConcreteTransformer).name())
    {
        for (auto& tr : transformers)
        {
            if (tr.name == name)
            {
                return std::dynamic_pointer_cast<ConcreteTransformer>(tr.node);
            }
        }

        return nullptr;
    }

    std::string stringify() const override
    {
        return "view-transform-root";
    }

  private:
    struct added_transformer_t
    {
        wf::scene::floating_inner_ptr node;
        int z_order;
        std::string name;
    };

    std::vector<added_transformer_t> transformers;
    void _add_transformer(wf::scene::floating_inner_ptr transformer,
        int z_order, std::string name);
    void _rem_transformer(wf::scene::floating_inner_ptr transformer);
};

/**
 * A simple transformer which supports 2D transformations on a view.
 */
class view_2d_transformer_t : public transformer_base_node_t
{
  public:
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float translation_x = 0.0f;
    float translation_y = 0.0f;
    // An angle in radians indicating how much the view should be rotated
    // around its center counter-clockwise.
    float angle = 0.0f;
    // A multiplier for the view's opacity.
    // Note that if the view was not opaque to begin with, setting alpha=1.0
    // does not make it opaque.
    float alpha = 1.0f;

    view_2d_transformer_t(wayfire_view view);
    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;
    std::string stringify() const override;
    wf::geometry_t get_bounding_box() override;
    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override;

    wayfire_view view;
};

/**
 * A simple transformer which supports 3D transformations on a view.
 */
class view_3d_transformer_t : public transformer_base_node_t
{
  protected:
    wayfire_view view;

  public:
    glm::mat4 view_proj{1.0}, translation{1.0}, rotation{1.0}, scaling{1.0};
    glm::vec4 color{1, 1, 1, 1};

    glm::mat4 calculate_total_transform();

  public:
    view_3d_transformer_t(wayfire_view view);
    wf::pointf_t to_local(const wf::pointf_t& point) override;
    wf::pointf_t to_global(const wf::pointf_t& point) override;
    std::string stringify() const override;
    wf::geometry_t get_bounding_box() override;
    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override;

    static const float fov; // PI / 8
    static glm::mat4 default_view_matrix();
    static glm::mat4 default_proj_matrix();
};
}

/**
 * When adding multiple transformers to a view, the relative order of these
 * transform nodes to each other matters. The transformer_z_order_t enum contains
 * a few common values used by transformers from core. Note that plugins may use
 * any integer as a Z order for a transformer.
 */
enum transformer_z_order_t
{
    // Simple 2D transforms applied to the base surface. Used for things like
    // scaling, simple 2D rotation.
    TRANSFORMER_2D        = 1,
    TRANSFORMER_3D        = 2,
    // Highlevel transformation which is usually at the top of the stack.
    // Used for things like wobbly and fire animation.
    TRANSFORMER_HIGHLEVEL = 500,
    // The highest level of view transforms, used by blur.
    TRANSFORMER_BLUR      = 1000,
};

// Calculate a bounding box after applying the node transformation to @box,
// assuming an affine transformation applied by the node.
wf::geometry_t get_bbox_for_node(scene::node_ptr node, wf::geometry_t box);
}

#endif /* end of include guard: VIEW_TRANSFORM_HPP */

#pragma once

#include <memory>
#include <vector>
#include <any>
#include <wayfire/config/types.hpp>
#include <wayfire/region.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/signal-provider.hpp>

namespace wf
{
class output_t;
namespace scene
{
class node_t;
using node_ptr = std::shared_ptr<node_t>;

class render_instance_t;

/**
 * Describes the result of trying to do direct scanout of a render instance on
 * an output.
 */
enum class direct_scanout
{
    /**
     * The node cannot be directly scanned out on the output, but does not occlude
     * any node below it which may be scanned out directly.
     */
    SKIP,
    /**
     * The node cannot be directly scanned out on the output, but covers a part
     * of the output, thus makes direct scanout impossible.
     */
    OCCLUSION,
    /**
     * The node was successfully scanned out.
     */
    SUCCESS,
};

/**
 * A single rendering call in a render pass.
 */
struct render_instruction_t
{
    render_instance_t *instance = NULL;
    wf::render_target_t target;
    wf::region_t damage;
    std::any data = {};
};

/**
 * When (parts) of the scenegraph have to be rendered, they have to be
 * 'instantiated' first. The instantiation of a (sub)tree of the scenegraph
 * is a tree of render instances, called a render tree. The purpose of the
 * render trees is to enable damage tracking (each render instance has its own
 * damage), while allowing arbitrary transformations in the scenegraph (e.g. a
 * render instance does not need to export information about how it transforms
 * its children). Due to this design, render trees have to be regenerated every
 * time the relevant portion of the scenegraph changes.
 *
 * Actually painting a render tree (called render pass) is a process involving
 * three steps:
 *
 * 1. Calculate the damage accumulated from the render tree.
 * 2. A front-to-back iteration through the render tree, so that every node
 *   calculates the parts of the destination buffer it should actually repaint.
 * 3. A final back-to-front iteration where the actual rendering happens.
 */
class render_instance_t
{
  public:
    virtual ~render_instance_t() = default;

    /**
     * Handle the front-to-back iteration (2.) from a render pass.
     * Each instance should add the render instructions (calls to
     * render_instance_t::render()) for itself and its children.
     *
     * @param instructions A list of render instructions to be executed.
     *   Instructions are evaluated in the reverse order they are pushed
     *   (e.g. from instructions.rbegin() to instructions.rend()).
     * @param damage The damaged region of the node, in node-local coordinates.
     *   Nodes may subtract from the damage, to prevent rendering below opaque
     *   regions, or expand it for certain special effects like blur.
     * @param fb The target framebuffer to render the node and its children.
     *   Note that some nodes may cause their children to be rendered to
     *   auxiliary buffers.
     */
    virtual void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) = 0;

    /**
     * Render the node on the given render target and the given damage region.
     * The node should not paint outside of @region.
     * All coordinates are to be given in the node's parent coordinate system.
     *
     * Note: render() should not be called outside of a render pass.
     *
     * @param target The render target to render the node to, as calculated in
     *   @schedule_instructions.
     * @param region The region to repaint, as calculated in
     *   @schedule_instructions.
     */
    virtual void render(const wf::render_target_t& target,
        const wf::region_t& region)
    {}

    /**
     * Render instances may also pass custom data to their render callbacks.
     * However, since few of them do this, it is enough to override the version
     * without custom data.
     */
    virtual void render(const wf::render_target_t& target,
        const wf::region_t& region, const std::any& custom_data)
    {
        render(target, region);
    }

    /**
     * Notify the render instance that it has been presented on an output.
     * Note that a render instance may get multiple presentation_feedback calls
     * for the same rendered frame.
     */
    virtual void presentation_feedback(wf::output_t *output)
    {}

    /**
     * Attempt direct scanout on the given output.
     *
     * Direct scanout is an optimization where a buffer from a node is directly
     * attached as the front buffer of an output. This is possible in a single
     * case, namely when the topmost node with visible contents on an output
     * covers it perfectly.
     *
     * @return The result of the attempt, see @direct_scanout.
     */
    virtual direct_scanout try_scanout(wf::output_t *output)
    {
        // By default, we report an occlusion, e.g. scanout is not possible,
        // neither for this node, nor for nodes below.
        return direct_scanout::OCCLUSION;
    }

    /**
     * Compute the render instance's visible region on the given output.
     *
     * The visible region can be used for things like determining when to send frame done events to
     * wlr_surfaces and to ignore damage to invisible parts of a render instance.
     */
    virtual void compute_visibility(wf::output_t *output, wf::region_t& visible)
    {}
};

using render_instance_uptr = std::unique_ptr<render_instance_t>;

using damage_callback = std::function<void (const wf::region_t&)>;

/**
 * A signal emitted when a part of the node is damaged.
 * on: the node itself.
 */
struct node_damage_signal
{
    wf::region_t region;
};

/**
 * A helper function to emit the damage signal on a node.
 */
template<class NodePtr>
inline void damage_node(NodePtr node, wf::region_t damage)
{
    node_damage_signal data;
    data.region = damage;
    node->emit(&data);
}

/**
 * Signal that a render pass starts.
 * emitted on: core.
 */
struct render_pass_begin_signal
{
    render_pass_begin_signal(wf::region_t& damage, wf::render_target_t target) :
        damage(damage), target(target)
    {}

    /**
     * The initial damage for this render pass.
     * Plugins may expand it further.
     */
    wf::region_t& damage;

    /**
     * The target buffer for rendering.
     */
    wf::render_target_t target;
};

/**
 * Signal that is emitted once a render pass ends.
 * emitted on: core.
 */
struct render_pass_end_signal
{
    wf::render_target_t target;
};

enum render_pass_flags
{
    /**
     * Do not emit render-pass-{begin, end} signals.
     */
    RPASS_EMIT_SIGNALS     = (1 << 0),
    /**
     * Do not clear the background areas.
     */
    RPASS_CLEAR_BACKGROUND = (1 << 1),
};

/**
 * A struct containing the information necessary to execute a render pass.
 */
struct render_pass_params_t
{
    /** The instances which are to be rendered in this render pass. */
    std::vector<render_instance_uptr> *instances;

    /** The rendering target. */
    render_target_t target;

    /** The total damage accumulated from the instances since the last repaint. */
    region_t damage;

    /**
     * The background color visible below all instances, if
     * RPASS_CLEAR_BACKGROUND is specified.
     */
    color_t background_color;

    /**
     * The output the instances were rendered, used for sending presentation
     * feedback.
     */
    output_t *reference_output = nullptr;
};

/**
 * A helper function to execute a render pass.
 *
 * The render pass goes as described below:
 *
 * 1. Emit render-pass-begin.
 * 2. Render instructions are generated from the given instances.
 * 3. Any remaining background areas are painted in @background_color.
 * 4. Render instructions are executed back-to-forth.
 * 5. Emit render-pass-end.
 *
 * By specifying @flags, steps 1, 3, and 5 can be disabled.
 *
 * @return The full damage which was rendered on the screen. It may be more (or
 *  less) than @accumulated_damage because plugins are allowed to modify the
 *  damage in render-pass-begin.
 */
wf::region_t run_render_pass(
    const render_pass_params_t& params, uint32_t flags);

/**
 * A helper function for direct scanout implementations.
 * It tries to forward the direct scanout request to the first render instance
 * in the given list, and returns the first non-SKIP result, or SKIP, if no
 * instance interacts with direct scanout.
 */
direct_scanout try_scanout_from_list(
    const std::vector<render_instance_uptr>& instances,
    wf::output_t *scanout);

/**
 * A helper function for compute_visibility implementations. It applies an offset to the damage and reverts it
 * afterwards. It also calls compute_visibility for the children instances.
 */
void compute_visibility_from_list(const std::vector<render_instance_uptr>& instances, wf::output_t *output,
    wf::region_t& region, const wf::point_t& offset);

/**
 * A helper class for easier implementation of render instances.
 * It automatically schedules instruction for the current node and tracks damage from the main node.
 */
template<class Node>
class simple_render_instance_t : public render_instance_t
{
  public:
    simple_render_instance_t(Node *self, damage_callback push_damage, wf::output_t *output)
    {
        this->self = std::dynamic_pointer_cast<Node>(self->shared_from_this());
        this->push_damage = push_damage;
        this->output = output;
        self->connect(&on_self_damage);
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        instructions.push_back(render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });
    }

  protected:
    std::shared_ptr<Node> self;
    wf::signal::connection_t<scene::node_damage_signal> on_self_damage = [=] (scene::node_damage_signal *ev)
    {
        push_damage(ev->region);
    };

    damage_callback push_damage;
    wf::output_t *output;
};

/**
 * Emitted on: node
 * The signal is used by some nodes to avoid unnecessary scenegraph recomputations.
 * For example it is used by nodes whose render instances keep a list of children, so that when the children
 * are updated, these nodes update only their internal list of children and not the entire scenegraph.
 */
struct node_regen_instances_signal
{};

uint32_t optimize_nested_render_instances(wf::scene::node_ptr node, uint32_t flags);
}
}

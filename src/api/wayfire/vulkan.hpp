#pragma once

#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif

#if WF_HAS_VULKANFX
    #include <memory>
    #include <map>
    #include <string>
    #include <string_view>
    #include <variant>
    #include <vector>
    #include <wayfire/nonstd/wlroots-full.hpp>
    #include <wayfire/render.hpp>
    #include <vulkan/vulkan.h>
    #include <wayfire/object.hpp>
    #include <wayfire/util.hpp>
    #include <glm/glm.hpp>

namespace wf
{
namespace vk
{
class context_t;
class gpu_buffer_t;
class graphics_pipeline_t;
class image_descriptor_set_pool_t;
/**
 * A class which encapsulates the information for specialization of a graphics pipeline for a specific render
 * pass. Specializations are used in order to reuse the same shader in different configurations, for example
 * to sample from textures with different color spaces (linear, sRGB, etc. @get_specialization_fo_texture),
 * or a custom user-specified specialization constant.
 *
 * On the GLSL side, this works by definiting the specialization constants and then using them to turn
 * parts of the code on or off.
 *
 * The pipeline specialization also involves using immutable samplers (bilinear, nearest or ycbcr) for each
 * texture sampling descriptor set indicated in the pipeline layout parameters.
 */
class pipeline_specialization_t
{
  public:
    template<class T>
    void add_specialization(uint32_t constant_id, uint32_t offset, const T& data)
    {
        _add_specialization(constant_id, offset, &data, sizeof(T));
    }

    /**
     * Adds the default specialization constants for sampling from a texture with different color encodings.
     * Interacts with @pipeline_params_t::descriptor_set_layouts to ensure that the correct descriptor set
     * layout (with the correct samplers) are used for the texture sampling.
     *
     * The added specializations are:
     * - A uint32_t matching the value of wf::texture_t::get_color_transform()::transfer_function
     * - A uint32_t matching the value of wf::texture_t::get_transform()
     *
     * @param texture The texture for which to add the specialization.
     * @param constant_id The ID (default: 0) of the specialization constant to use for the color transform.
     *                    The next ID (default: 1) is used for the texture rotation. These IDs should
     *                    match the one used in the shader.
     * @param offset The offset in bytes into the specialization data where the constant value will be
     * written. This allows packing multiple specializations into the same specialization data buffer.
     */
    void add_specialization_for_texture(const std::shared_ptr<wf::texture_t>& texture,
        const uint32_t constant_id = 0, const uint32_t offset = 0);

    /**
     * Get the specialization map entries.
     */
    const std::vector<VkSpecializationMapEntry>& get_entries() const
    {
        return entries;
    }

    /**
     * Get the raw specialization data.
     */
    const std::vector<std::byte>& get_data() const
    {
        return specialization_data;
    }

  private:
    void _add_specialization(uint32_t constant_id, uint32_t offset, const void *data, size_t size);
    std::vector<VkSpecializationMapEntry> entries;
    std::vector<std::byte> specialization_data;
    std::vector<std::shared_ptr<wf::texture_t>> specialized_textures;
    friend class graphics_pipeline_t;
};

/**
 * A wrapper around a wlr_render_pass from the vulkan backend.
 */
class command_buffer_t : public wf::signal::provider_t
{
  private:
    std::shared_ptr<context_t> context;
    std::vector<std::shared_ptr<graphics_pipeline_t>> bound_pipelines;
    std::vector<std::shared_ptr<wf::texture_t>> bound_textures;

    friend class image_descriptor_set_pool_t;
    friend class gpu_buffer_t;
    friend class graphics_pipeline_t;
    std::vector<std::shared_ptr<gpu_buffer_t>> bound_buffers;

    VkCommandBuffer cmd;
    VkRenderPass current_pass = VK_NULL_HANDLE;
    wlr_render_pass *wlr_pass = nullptr;
    command_buffer_t(std::shared_ptr<context_t> context, wf::render_pass_t& pass);

  public:
    /**
     * Create a wrapper around the command buffer used currently in the given pass.
     * Note that the memory for the command buffer is managed by core.
     */
    static command_buffer_t& buffer_for_pass(wf::render_pass_t& pass);

    /**
     * A wrapper around the command buffer used currently in the given pass.
     */
    ~command_buffer_t();

    /**
     * Emitted when the command buffer is reset, i.e. when it can be safely reused again.
     */
    struct reset_signal
    {
        command_buffer_t *self;
    };

    /**
     * Bind the pipeline to the command buffer.
     * The pipeline will be kept alive until the command buffer is finished executing.
     *
     * @return The vulkan pipeline layout and pipeline used for the current render pass and specialization.
     */
    std::pair<VkPipelineLayout, VkPipeline> bind_pipeline(std::shared_ptr<graphics_pipeline_t> pipeline,
        const wf::render_target_t& target,
        const pipeline_specialization_t& specialization = pipeline_specialization_t{});

    /**
     * Set the viewport to cover the entire render target.
     */
    void set_full_viewport(const wf::render_target_t& target);

    /**
     * Iterate over all rectangles needed to cover the given damage region, set each of them as the scissor
     * and call the callback for each of them.
     *
     * Typical usage:
     * for_each_scissor_rect(target, damage, [&] { vkCmdDraw(...); });
     */
    void for_each_scissor_rect(const wf::render_target_t& target, const wf::region_t& damage,
        const std::function<void()> & callback);

    /**
     * Bind a gpu_buffer_t to this command buffer, keeping it alive until the command buffer is reset.
     * This is useful for vertex buffers, uniform buffers, etc. that need to stay allocated while the GPU
     * is executing the commands.
     */
    void bind_buffer(std::shared_ptr<gpu_buffer_t> buffer);

    /**
     * Bind a texture to this command buffer, keeping it alive until the command buffer is reset.
     */
    void bind_texture(const std::shared_ptr<wf::texture_t>& texture);

    operator VkCommandBuffer() const
    {
        return cmd;
    }

    command_buffer_t(const command_buffer_t&) = delete;
    command_buffer_t(command_buffer_t&&) = delete;
    command_buffer_t& operator =(const command_buffer_t&) = delete;
    command_buffer_t& operator =(command_buffer_t&&) = delete;
};

/**
 * Manages a pool of descriptor sets for image sampling.
 */
/**
 * A GPU buffer allocated via a context_t.
 * The buffer is host-visible and host-coherent, so it can be mapped for CPU access.
 * Suitable for uniform buffers, vertex buffers, or any other buffer usage.
 *
 * Use context_t::create_buffer() to allocate, and command_buffer_t::bind_buffer() to keep it alive
 * for the duration of a command buffer's execution.
 */
class gpu_buffer_t : public std::enable_shared_from_this<gpu_buffer_t>
{
  public:
    ~gpu_buffer_t();

    gpu_buffer_t(const gpu_buffer_t&) = delete;
    gpu_buffer_t(gpu_buffer_t&&) = delete;
    gpu_buffer_t& operator =(const gpu_buffer_t&) = delete;
    gpu_buffer_t& operator =(gpu_buffer_t&&) = delete;

    /**
     * Get the underlying VkBuffer handle.
     */
    const VkBuffer& get_buffer() const
    {
        return buffer;
    }

    /**
     * Get the size of the buffer in bytes.
     */
    const VkDeviceSize& get_size() const
    {
        return size;
    }

    /**
     * Map the buffer memory and copy data into it.
     * @param data Pointer to the source data.
     * @param data_size Size of the data to copy in bytes. Must be <= get_size().
     * @param offset Offset into the buffer to start writing at.
     * @return true if the data was successfully written.
     */
    bool write(const void *data, VkDeviceSize data_size, VkDeviceSize offset = 0);

    /**
     * Map the buffer memory for direct access.
     * The caller must call unmap() when done.
     * @return Pointer to the mapped memory, or nullptr on failure.
     */
    void *map(VkDeviceSize offset = 0, VkDeviceSize map_size = VK_WHOLE_SIZE);

    /**
     * Unmap previously mapped buffer memory.
     */
    void unmap();

    operator VkBuffer() const
    {
        return buffer;
    }

  private:
    friend class context_t;
    gpu_buffer_t(std::shared_ptr<context_t> ctx, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize size);

    std::shared_ptr<context_t> context;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size     = 0;
};

/**
 * The texture sampling parameters indicate how to sample from a texture.
 *
 * We may need to apply a transformation (rotation) of the buffer and/or sample from a particular subregion
 * of the image.
 */
struct texture_sampling_params_t
{
    /**
     * Initialize the sampling parameters for the given texture.
     */
    texture_sampling_params_t(const std::shared_ptr<wf::texture_t>& texture);

    /**
     * Get the UV scale to apply when sampling the texture.
     * This is used to sample from a subregion of the texture, starting from uv_offset to
     * uv_offset + uv_scale.
     */
    const glm::vec2& get_uv_scale() const
    {
        return uv_scale;
    }

    /**
     * Get the UV offset to apply when sampling the texture.
     * This is used to sample from a subregion of the texture, starting from uv_offset to
     * uv_offset + uv_scale.
     */
    const glm::vec2& get_uv_offset() const
    {
        return uv_offset;
    }

  private:
    glm::vec2 uv_scale{1, 1};
    glm::vec2 uv_offset{0, 0};
};

class image_descriptor_set_pool_t : public std::enable_shared_from_this<image_descriptor_set_pool_t>
{
    std::shared_ptr<context_t> context;

  public:
    image_descriptor_set_pool_t(std::shared_ptr<context_t> ctx);
    ~image_descriptor_set_pool_t();

    /**
     * Get or allocate descriptor set for the given command buffer and texture.
     * The first binding in the set is used as a combined image sampler pointing to the texture.
     *
     * The descriptor set will match the layout returned by context_t::get_image_descriptor_set_layout().
     */
    VkDescriptorSet get_descriptor_set(
        command_buffer_t& cmd_buf, const std::shared_ptr<wf::texture_t>& texture);
};

class context_t : public std::enable_shared_from_this<context_t>
{
  public:
    /**
     * Create a new vulkan context from the given wlr_renderer.
     * The renderer needs to be a vulkan renderer.
     */
    context_t(wlr_renderer *renderer);
    ~context_t();
    context_t(const context_t&) = delete;
    context_t(context_t&&) = delete;
    context_t& operator =(const context_t&) = delete;
    context_t& operator =(context_t&&) = delete;

    VkShaderModule load_shader_module(std::string_view path);
    VkShaderModule load_shader_module(const uint32_t *data, size_t size);

    /**
     * Create a GPU buffer with the given size and usage flags.
     * The buffer is allocated with host-visible and host-coherent memory, making it suitable for
     * CPU writes. Common usage flags include VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT and
     * VK_BUFFER_USAGE_VERTEX_BUFFER_BIT.
     *
     * @param size Size of the buffer in bytes.
     * @param usage Vulkan buffer usage flags.
     * @return A shared pointer to the created buffer, or nullptr on failure.
     */
    std::shared_ptr<gpu_buffer_t> create_buffer(VkDeviceSize size, VkBufferUsageFlags usage);

    VkDevice get_device() const
    {
        return device;
    }

    VkQueue get_queue() const
    {
        return queue;
    }

    VkPhysicalDevice get_physical_device() const
    {
        return physical_device;
    }

    /**
     * Get the wlr_renderer associated with this context.
     */
    wlr_renderer *get_renderer() const
    {
        return renderer;
    }

  private:
    // Vulkan core objects
    wlr_renderer *renderer = nullptr;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkQueue queue;
};

struct pipeline_shader_t
{
    VkShaderStageFlagBits stage;
    // Can be either file names (std::string) or preloaded VkShaderModules.
    // Note that the pipeline will take ownership of any VkShaderModule passed here.
    std::variant<std::string, VkShaderModule> shader;
};

struct blending_params_t
{
    // If not set, blending is disabled.
    std::optional<VkBlendOp> blend_op = VK_BLEND_OP_ADD;
    VkBlendFactor src_factor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    // If not set, same as color blending is used.
    std::optional<VkBlendOp> alpha_blend_op = {};
    std::optional<VkBlendFactor> alpha_src_factor = {};
    std::optional<VkBlendFactor> alpha_dst_factor = {};
};

struct pipeline_params_t
{
    struct texture_descriptor_set_t {};

    // List of shaders for the pipeline.
    std::vector<pipeline_shader_t> shaders;

    // Descriptor set layouts used in the pipeline.
    // Each texture_descriptor_set_t will be replaced with an appropriate descriptor set when specializing
    // the pipeline with pipeline_specialization_t::add_specialization_for_texture():
    // the i-th texture_descriptor_set_t in the descriptor layout list will be replaced with a descriptor set
    // layout for the i-th texture specialization.
    std::vector<std::variant<VkDescriptorSetLayout, texture_descriptor_set_t>> descriptor_set_layouts;

    // Vertex input description
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    std::vector<VkVertexInputAttributeDescription> vertex_attribute_description;
    std::vector<VkVertexInputBindingDescription> vertex_input_description;

    // Push constants for the shader
    std::vector<VkPushConstantRange> push_constants;

    // Blending parameters
    blending_params_t blending{};
};

/**
 * A helper class to create and manage a graphics pipeline.
 * This involves the creation of render passes as well as pipelines as needed for various output buffer
 * formats.
 */
class graphics_pipeline_t
{
  public:
    graphics_pipeline_t(std::shared_ptr<context_t> ctx, const pipeline_params_t& params);
    ~graphics_pipeline_t();

    graphics_pipeline_t(const graphics_pipeline_t&) = delete;
    graphics_pipeline_t(graphics_pipeline_t&&) = delete;
    graphics_pipeline_t& operator =(const graphics_pipeline_t&) = delete;
    graphics_pipeline_t& operator =(graphics_pipeline_t&&) = delete;

    /**
     * Get or create a pipeline for the given render pass and specialization.
     * If no specialization is provided, a pipeline with no specialization constants is used.
     * Pipelines are cached by (render pass, specialization data) pairs.
     */
    std::pair<VkPipelineLayout, VkPipeline> pipeline_for(const command_buffer_t& pass,
        const pipeline_specialization_t& specialization = pipeline_specialization_t{});

  private:
    std::shared_ptr<context_t> context;
    pipeline_params_t params;
    std::vector<VkShaderModule> loaded_shader_modules;

    // Cache key: (render pass, specialization data bytes, specialization texture ds)
    using pipeline_key_t = std::tuple<VkRenderPass, std::vector<std::byte>,
        std::vector<VkDescriptorSetLayout>>;
    std::map<pipeline_key_t, std::pair<VkPipelineLayout, VkPipeline>> pipelines;
};

/**
 * Helper method to calculate a 4x4 matrix which maps the unit square [0,1]x[0,1] to the given box.
 */
glm::mat4 geometry_mat(const wf::geometry_t& box);

/**
 * Helper method to calculate a transformation from logical render target coordinates to final NDC space.
 */
glm::mat4 render_target_transform(const wf::render_target_t& target);
}

/**
 * Global vulkan state managed in core.
 */
class vulkan_render_state_t : public wf::object_base_t
{
  public:
    /**
     * Gets the vulkan state allocated and managed by core.
     * Note that plugins are free to create and manage their own vulkan contexts, but this is provided as a
     * convenience for plugins so that we can reuse command buffers between multiple plugins.
     */
    static vulkan_render_state_t& get();

    std::shared_ptr<wf::vk::context_t> get_context() const
    {
        return context;
    }

    std::shared_ptr<wf::vk::image_descriptor_set_pool_t> get_descriptor_pool() const
    {
        return descriptor_pool;
    }

    vulkan_render_state_t(wlr_renderer *renderer);

  private:
    std::shared_ptr<wf::vk::context_t> context;
    std::shared_ptr<wf::vk::image_descriptor_set_pool_t> descriptor_pool;
};
}

#endif

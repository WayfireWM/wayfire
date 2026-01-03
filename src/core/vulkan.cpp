#include "core/core-impl.hpp"
#include "wayfire/opengl.hpp"
#include <wayfire/vulkan.hpp>
#include <fstream>
#include <wayfire/util/log.hpp>
#include <drm_fourcc.h>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

extern "C"
{
#include <wlr/interfaces/wlr_buffer.h>
}

namespace wf
{
namespace vk
{
static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++)
    {
        if ((type_filter & (1 << i)) &&
            ((mem_properties.memoryTypes[i].propertyFlags & properties) == properties))
        {
            return i;
        }
    }

    return UINT32_MAX;
}

// --- gpu_buffer_t implementation ---
gpu_buffer_t::gpu_buffer_t(std::shared_ptr<context_t> ctx, VkBuffer buffer, VkDeviceMemory memory,
    VkDeviceSize size) :
    context(std::move(ctx)), buffer(buffer), memory(memory), size(size)
{}

gpu_buffer_t::~gpu_buffer_t()
{
    if (buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(context->get_device(), buffer, nullptr);
    }

    if (memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(context->get_device(), memory, nullptr);
    }
}

bool gpu_buffer_t::write(const void *data, VkDeviceSize data_size, VkDeviceSize offset)
{
    if ((offset + data_size) > size)
    {
        LOGE("gpu_buffer_t::write: data_size + offset exceeds buffer size");
        return false;
    }

    void *mapped = map(offset, data_size);
    if (!mapped)
    {
        return false;
    }

    memcpy(mapped, data, data_size);
    unmap();
    return true;
}

void*gpu_buffer_t::map(VkDeviceSize offset, VkDeviceSize map_size)
{
    void *mapped = nullptr;
    if (vkMapMemory(context->get_device(), memory, offset, map_size, 0, &mapped) != VK_SUCCESS)
    {
        LOGE("Failed to map gpu_buffer_t memory");
        return nullptr;
    }

    return mapped;
}

void gpu_buffer_t::unmap()
{
    vkUnmapMemory(context->get_device(), memory);
}

texture_sampling_params_t::texture_sampling_params_t(const std::shared_ptr<wf::texture_t>& texture)
{
    if (texture->get_source_box())
    {
        uv_offset.x = texture->get_source_box()->x / static_cast<float>(texture->get_width());
        uv_offset.y = texture->get_source_box()->y / static_cast<float>(texture->get_height());
        uv_scale.x  = texture->get_source_box()->width / static_cast<float>(texture->get_width());
        uv_scale.y  = texture->get_source_box()->height / static_cast<float>(texture->get_height());
    }
}

image_descriptor_set_pool_t::image_descriptor_set_pool_t(std::shared_ptr<context_t> ctx)
{
    this->context = std::move(ctx);
}

image_descriptor_set_pool_t::~image_descriptor_set_pool_t() = default;

VkDescriptorSet image_descriptor_set_pool_t::get_descriptor_set(
    command_buffer_t& cmd_buf, const std::shared_ptr<wf::texture_t>& texture)
{
    return wlr_vk_render_pass_get_texture_ds(cmd_buf.wlr_pass, texture->get_wlr_texture(),
        texture->get_filter_mode().value_or(WLR_SCALE_FILTER_BILINEAR),
        texture->get_color_transform().transfer_function);
}

command_buffer_t::command_buffer_t(std::shared_ptr<context_t> ctx, wf::render_pass_t& pass) :
    context(ctx)
{
    this->cmd = wlr_vk_render_pass_get_command_buffer(pass.get_wlr_pass());
    this->current_pass = wlr_vk_render_pass_get_render_pass(pass.get_wlr_pass());
    this->wlr_pass     = pass.get_wlr_pass();
}

command_buffer_t::~command_buffer_t()
{
    bound_pipelines.clear();
    bound_buffers.clear();
    reset_signal data;
    emit(&data);
}

static void handle_release_resources(void *user_data)
{
    auto *cmd_buf = static_cast<command_buffer_t*>(user_data);
    // LOGI("Render pass is releasing resources, deleting command buffer");
    delete cmd_buf;
}

command_buffer_t& command_buffer_t::buffer_for_pass(wf::render_pass_t& pass)
{
    auto ctx = vulkan_render_state_t::get().get_context();

    // Will be freed by handle_release_resources callback when the render pass is really completed.
    auto *cmd_buf = new command_buffer_t(ctx, pass);
    wlr_vk_render_pass_set_resources_callback(pass.get_wlr_pass(), handle_release_resources, cmd_buf);
    return *cmd_buf;
}

void command_buffer_t::bind_buffer(std::shared_ptr<gpu_buffer_t> buffer)
{
    bound_buffers.push_back(std::move(buffer));
}

void command_buffer_t::bind_texture(const std::shared_ptr<wf::texture_t>& texture)
{
    bound_textures.push_back(texture);
}

std::pair<VkPipelineLayout, VkPipeline> command_buffer_t::bind_pipeline(
    std::shared_ptr<graphics_pipeline_t> pipeline,
    const wf::render_target_t& target, const pipeline_specialization_t& specialization)
{
    bound_pipelines.push_back(pipeline);

    auto [pipeline_layout, vk_pipeline] = pipeline->pipeline_for(*this, specialization);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);
    return {pipeline_layout, vk_pipeline};
}

void command_buffer_t::set_full_viewport(const wf::render_target_t& target)
{
    VkViewport viewport{
        .x     = 0.0f,
        .y     = 0.0f,
        .width = static_cast<float>(target.get_size().width),
        .height   = static_cast<float>(target.get_size().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    vkCmdSetViewport(*this, 0, 1, &viewport);
}

void command_buffer_t::for_each_scissor_rect(const wf::render_target_t& target, const wf::region_t& damage,
    const std::function<void()> & callback)
{
    auto buffer_damage = target.framebuffer_region_from_geometry_region(damage);
    for (const auto& box : buffer_damage)
    {
        VkRect2D scissor{
            .offset = {box.x1, box.y1},
            .extent = {static_cast<uint32_t>(box.x2 - box.x1), static_cast<uint32_t>(box.y2 - box.y1)},
        };

        vkCmdSetScissor(*this, 0, 1, &scissor);
        callback();
    }

    wlr_vk_render_pass_mark_updated(wlr_pass, buffer_damage.to_pixman());
}

context_t::context_t(wlr_renderer *renderer) : renderer(renderer)
{
    this->device = wlr_vk_renderer_get_device(renderer);
    this->physical_device = wlr_vk_renderer_get_physical_device(renderer);
    uint32_t queue_family = wlr_vk_renderer_get_queue_family(renderer);

    // Get the graphics queue
    vkGetDeviceQueue(device, queue_family, 0, &queue);
}

context_t::~context_t()
{}

VkShaderModule context_t::load_shader_module(std::string_view path)
{
    std::ifstream file(std::string(path), std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        LOGE("Failed to open shader file: ", path);
        return VK_NULL_HANDLE;
    }

    size_t file_size = (size_t)file.tellg();
    std::vector<uint8_t> buffer(file_size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    file.close();

    while (buffer.size() % 4 != 0)
    {
        buffer.push_back(0);
    }

    return load_shader_module(reinterpret_cast<const uint32_t*>(buffer.data()), buffer.size() / 4);
}

VkShaderModule context_t::load_shader_module(const uint32_t *data, size_t size)
{
    if (!data || (size == 0))
    {
        LOGE("Invalid shader data or size");
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo create_info{};
    create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode    = data;

    VkShaderModule shader_module;
    if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) != VK_SUCCESS)
    {
        LOGE("Failed to create shader module");
        return VK_NULL_HANDLE;
    }

    return shader_module;
}

std::shared_ptr<gpu_buffer_t> context_t::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size  = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS)
    {
        LOGE("Failed to create GPU buffer");
        return nullptr;
    }

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

    uint32_t memory_type = find_memory_type(physical_device,
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX)
    {
        LOGE("Failed to find suitable memory type for GPU buffer");
        vkDestroyBuffer(device, buffer, nullptr);
        return nullptr;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_requirements.size;
    alloc_info.memoryTypeIndex = memory_type;

    VkDeviceMemory memory;
    if (vkAllocateMemory(device, &alloc_info, nullptr, &memory) != VK_SUCCESS)
    {
        LOGE("Failed to allocate memory for GPU buffer");
        vkDestroyBuffer(device, buffer, nullptr);
        return nullptr;
    }

    vkBindBufferMemory(device, buffer, memory, 0);

    // Use the private constructor via shared_ptr with a custom make (can't use make_shared with private ctor)
    return std::shared_ptr<gpu_buffer_t>(
        new gpu_buffer_t(shared_from_this(), buffer, memory, size));
}

void pipeline_specialization_t::add_specialization_for_texture(const std::shared_ptr<wf::texture_t>& texture,
    const uint32_t constant_id, const uint32_t offset)
{
    add_specialization(constant_id, offset, (uint32_t)texture->get_color_transform().transfer_function);
    // We need to flip 180 degrees because for Vulkan, 0,0 is top left
    auto total_transform =
        wlr_output_transform_compose(texture->get_transform(), WL_OUTPUT_TRANSFORM_FLIPPED_180);
    add_specialization(constant_id + 1, offset + sizeof(uint32_t), total_transform);
    specialized_textures.push_back(texture);
}

void pipeline_specialization_t::_add_specialization(
    uint32_t constant_id, uint32_t offset, const void *data, size_t size)
{
    // Ensure the data buffer is large enough to hold the new entry
    if (specialization_data.size() < offset + size)
    {
        specialization_data.resize(offset + size);
    }

    // Copy the data into the buffer at the given offset
    std::memcpy(specialization_data.data() + offset, data, size);

    // Record the specialization map entry
    VkSpecializationMapEntry entry{};
    entry.constantID = constant_id;
    entry.offset     = offset;
    entry.size = size;
    entries.push_back(entry);
}

graphics_pipeline_t::graphics_pipeline_t(std::shared_ptr<context_t> ctx, const pipeline_params_t& params) :
    context(ctx)
{
    // Store pipeline params for later use
    this->params = params;

    // Load all shader modules in advance and store them
    for (const auto& shader : params.shaders)
    {
        if (std::holds_alternative<std::string>(shader.shader))
        {
            VkShaderModule module = context->load_shader_module(std::get<std::string>(shader.shader));
            loaded_shader_modules.push_back(module);
        } else
        {
            loaded_shader_modules.push_back(std::get<VkShaderModule>(shader.shader));
        }
    }
}

graphics_pipeline_t::~graphics_pipeline_t()
{
    // Destroy all pipelines and render passes
    for (auto& [pass, pf] : pipelines)
    {
        vkDestroyPipelineLayout(context->get_device(), pf.first, nullptr);
        vkDestroyPipeline(context->get_device(), pf.second, nullptr);
    }

    // Destroy loaded shader modules
    for (auto module : loaded_shader_modules)
    {
        if (module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(context->get_device(), module, nullptr);
        }
    }
}

std::pair<VkPipelineLayout, VkPipeline> graphics_pipeline_t::pipeline_for(const command_buffer_t& cmd_buf,
    const pipeline_specialization_t& specialization)
{
    std::vector<VkDescriptorSetLayout> specialization_filters;
    for (const auto& texture : specialization.specialized_textures)
    {
        auto dsl = wlr_vk_render_pass_get_texture_ds_layout(cmd_buf.wlr_pass, texture->get_wlr_texture(),
            texture->get_filter_mode().value_or(WLR_SCALE_FILTER_BILINEAR),
            texture->get_color_transform().transfer_function);
        specialization_filters.push_back(dsl);
    }

    pipeline_key_t key{cmd_buf.current_pass, specialization.get_data(), specialization_filters};
    auto it = pipelines.find(key);
    if (it != pipelines.end())
    {
        return it->second;
    }

    // Create pipeline layout from descriptor set layouts and push constants
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> specialized_layouts;
    size_t specialized_idx = 0;
    for (const auto& layout_variant : params.descriptor_set_layouts)
    {
        if (std::holds_alternative<VkDescriptorSetLayout>(layout_variant))
        {
            specialized_layouts.push_back(std::get<VkDescriptorSetLayout>(layout_variant));
        } else if (specialized_idx < specialization_filters.size())
        {
            specialized_layouts.push_back(specialization_filters[specialized_idx++]);
        } else
        {
            LOGE("Not enough specialization filters provided for pipeline specialization");
            return {VK_NULL_HANDLE, VK_NULL_HANDLE};
        }
    }

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = specialized_layouts.size();
    layout_info.pSetLayouts    = specialized_layouts.data();
    layout_info.pushConstantRangeCount = params.push_constants.size();
    layout_info.pPushConstantRanges    = params.push_constants.data();
    if (vkCreatePipelineLayout(context->get_device(), &layout_info, nullptr, &layout) != VK_SUCCESS)
    {
        LOGE("Failed to create pipeline layout");
        return {VK_NULL_HANDLE, VK_NULL_HANDLE};
    }

    // Build VkSpecializationInfo if we have specialization constants
    VkSpecializationInfo spec_info{};
    const bool has_specialization = !specialization.get_entries().empty();
    if (has_specialization)
    {
        spec_info.mapEntryCount = specialization.get_entries().size();
        spec_info.pMapEntries   = specialization.get_entries().data();
        spec_info.dataSize = specialization.get_data().size();
        spec_info.pData    = specialization.get_data().data();
    }

    // --- Pipeline creation ---
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    for (size_t i = 0; i < params.shaders.size(); ++i)
    {
        const auto& shader = params.shaders[i];
        VkPipelineShaderStageCreateInfo stage_info{};
        stage_info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage_info.stage  = shader.stage;
        stage_info.module = loaded_shader_modules[i];
        stage_info.pName  = "main";
        stage_info.pSpecializationInfo = has_specialization ? &spec_info : nullptr;
        shader_stages.push_back(stage_info);
    }

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = params.vertex_input_description.size();
    vertex_input_info.pVertexBindingDescriptions    = params.vertex_input_description.data();
    vertex_input_info.vertexAttributeDescriptionCount = params.vertex_attribute_description.size();
    vertex_input_info.pVertexAttributeDescriptions    = params.vertex_attribute_description.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = params.topology;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x     = 0.0f;
    viewport.y     = 0.0f;
    viewport.width = 1.0f;
    viewport.height   = 1.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {1, 1};

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports    = nullptr; // Dynamic
    viewport_state.scissorCount  = 1;
    viewport_state.pScissors     = nullptr; // Dynamic

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = params.blending.blend_op.has_value() ? VK_TRUE : VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = params.blending.src_factor;
    color_blend_attachment.dstColorBlendFactor = params.blending.dst_factor;
    color_blend_attachment.colorBlendOp = params.blending.blend_op.value_or(VK_BLEND_OP_ADD);
    color_blend_attachment.srcAlphaBlendFactor =
        params.blending.alpha_src_factor.value_or(params.blending.src_factor);
    color_blend_attachment.dstAlphaBlendFactor =
        params.blending.alpha_dst_factor.value_or(params.blending.dst_factor);
    color_blend_attachment.alphaBlendOp = params.blending.alpha_blend_op.value_or(
        params.blending.blend_op.value_or(VK_BLEND_OP_ADD));

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable   = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments    = &color_blend_attachment;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state_info{};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = 2;
    dynamic_state_info.pDynamicStates    = dynamic_states;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = shader_stages.size();
    pipeline_info.pStages    = shader_stages.data();
    pipeline_info.pVertexInputState   = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState   = &multisampling;
    pipeline_info.pColorBlendState    = &color_blending;
    pipeline_info.layout     = layout;
    pipeline_info.renderPass = cmd_buf.current_pass;
    pipeline_info.subpass    = 0;
    pipeline_info.pDynamicState = &dynamic_state_info;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(
        context->get_device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS)
    {
        LOGE("Failed to create graphics pipeline for pass: ", cmd_buf.current_pass);
        vkDestroyPipelineLayout(context->get_device(), layout, nullptr);
        return {VK_NULL_HANDLE, VK_NULL_HANDLE};
    }

    // Store the pipeline keyed by (render pass, specialization data)
    pipelines[key] = {layout, pipeline};
    return {layout, pipeline};
}

glm::mat4 render_target_transform(const wf::render_target_t& target)

{
    auto ortho = glm::ortho(
        1.0f * target.geometry.x,
        1.0f * target.geometry.x + 1.0f * target.geometry.width,
        1.0f * target.geometry.y,
        1.0f * target.geometry.y + 1.0f * target.geometry.height);

    ortho[1][1] *= -1; // Invert Y axis to match Vulkan's coordinate system
    return ortho * gles::render_target_gl_to_framebuffer(target);
}
} // namespace vk

vulkan_render_state_t::vulkan_render_state_t(wlr_renderer *renderer)
{
    this->context = std::make_shared<wf::vk::context_t>(renderer);
    this->descriptor_pool = std::make_shared<wf::vk::image_descriptor_set_pool_t>(context);
}

vulkan_render_state_t& vulkan_render_state_t::get()
{
    auto& core_impl = wf::get_core_impl();
    return *core_impl.vulkan_state;
}
} // namespace wf

#include "core/core-impl.hpp"
#include "wayfire/dassert.hpp"
#include "wayfire/opengl.hpp"
#include <wayfire/vulkan.hpp>
#include <fstream>
#include <wayfire/util/log.hpp>
#include <drm_fourcc.h>
#include <array>
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
image_descriptor_set_pool_t::image_descriptor_set_pool_t(std::shared_ptr<context_t> ctx, size_t max_size) :
    max_size(max_size)
{
    wf::dassert(max_size > 0, "Descriptor set pool must have a positive max size");
    this->context = std::move(ctx);

    // Create a descriptor pool for sampled images
    VkDescriptorPoolSize pool_size_image{};
    pool_size_image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size_image.descriptorCount = max_size;

    VkDescriptorPoolSize pool_size_matrix{};
    pool_size_matrix.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size_matrix.descriptorCount = max_size;

    std::array<VkDescriptorPoolSize, 2> pool_sizes = {pool_size_image, pool_size_matrix};

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = pool_sizes.size();
    pool_info.pPoolSizes    = pool_sizes.data();
    pool_info.maxSets = max_size;

    if (vkCreateDescriptorPool(context->get_device(), &pool_info, nullptr, &pool) != VK_SUCCESS)
    {
        LOGE("Failed to create Vulkan descriptor pool");
        pool = VK_NULL_HANDLE;
    }
}

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

image_descriptor_set_pool_t::~image_descriptor_set_pool_t()
{
    if (pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(context->get_device(), pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
}

VkImageView image_descriptor_set_pool_t::update_descriptor_set_from_texture(allocated_set_t& entry,
    std::shared_ptr<wf::texture_t> texture)
{
    wlr_vk_image_attribs attribs;
    wlr_vk_texture_get_image_attribs(texture->get_wlr_texture(), &attribs);

    VkImageViewCreateInfo view_info{};
    view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image    = attribs.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = attribs.format;
    view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    VkImageView image_view;
    if (vkCreateImageView(context->get_device(), &view_info, nullptr, &image_view) != VK_SUCCESS)
    {
        LOGE("Failed to create image view");
        return VK_NULL_HANDLE;
    }

    glm::mat4 uv_transform(1.0f);

    // We need to flip 180 degrees because for Vulkan, 0,0 is top left
    auto total_transform =
        wlr_output_transform_compose(texture->get_transform(), WL_OUTPUT_TRANSFORM_FLIPPED_180);

    // We translate by -0.5, -0.5, so that the texture is centered at the origin,
    // then apply the transform and translate back
    uv_transform = glm::translate(glm::mat4(1.0f), {0.5f, 0.5f, 0.0f}) *
        get_output_matrix_from_transform(total_transform) *
        glm::translate(glm::mat4(1.0f), {-0.5f, -0.5f, 0.0f});

    if (texture->get_source_box())
    {
        float subx = texture->get_source_box()->x / static_cast<float>(texture->get_width());
        float suby = texture->get_source_box()->y / static_cast<float>(texture->get_height());
        float subw = texture->get_source_box()->width / static_cast<float>(texture->get_width());
        float subh = texture->get_source_box()->height / static_cast<float>(texture->get_height());

        uv_transform =
            glm::translate(glm::mat4(1.0f), {subx, suby, 0.0f}) *
            glm::scale(glm::mat4(1.0f), {subw, subh, 1.0f}) *
            uv_transform;
    }

    // Write identity matrix to the uniform buffer (std140 layout: mat3 = 3 x vec4)
    float identity[12] = {
        uv_transform[0][0], uv_transform[0][1], uv_transform[0][3], 0.0f,
        uv_transform[1][0], uv_transform[1][1], uv_transform[1][3], 0.0f,
        uv_transform[3][0], uv_transform[3][1], uv_transform[3][3], 0.0f,
    };

    if (!entry.matrix_buffer->write(identity, sizeof(identity)))
    {
        LOGE("Failed to write uv transform matrix to uniform buffer");
    }

    // Update both descriptor bindings
    VkDescriptorImageInfo image_info{};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView   = image_view;
    image_info.sampler     = context->get_linear_sampler();

    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = entry.matrix_buffer->get_buffer();
    buffer_info.offset = 0;
    buffer_info.range  = sizeof(identity);

    std::array<VkWriteDescriptorSet, 2> writes{};

    // Binding 0: combined image sampler
    writes[0].sType  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = entry.set;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &image_info;

    // Binding 1: uniform buffer for 3x3 matrix
    writes[1].sType  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = entry.set;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo     = &buffer_info;

    vkUpdateDescriptorSets(context->get_device(), writes.size(), writes.data(), 0, nullptr);
    return image_view;
}

VkDescriptorSet image_descriptor_set_pool_t::get_descriptor_set(
    command_buffer_t& cmd_buf, const std::shared_ptr<wf::texture_t>& texture)
{
    const auto& use_set = [&] (size_t index) -> VkDescriptorSet
    {
        auto& entry = *allocated_sets[index];

        // Cleanup potential old connections
        entry.reset_listener.disconnect();

        lookup_table[texture->get_wlr_texture()] = index;
        entry.last_texture = texture;
        entry.owner = &cmd_buf;

        entry.reset_listener = [this, &entry, index] (command_buffer_t::reset_signal *data)
        {
            if (entry.image_view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(context->get_device(), entry.image_view, nullptr);
                entry.image_view = VK_NULL_HANDLE;
            }

            if (auto it = lookup_table.find(entry.last_texture->get_wlr_texture());
                (it != lookup_table.end()) && (it->second == index))
            {
                lookup_table.erase(it);
            }

            entry.reset_listener.disconnect();
            entry.last_texture.reset();
            entry.owner = nullptr;
        };
        cmd_buf.connect(&entry.reset_listener);

        // We need to keep the descriptor pool alive until the command buffer is reset.
        cmd_buf.bound_descriptor_pools.push_back(this->shared_from_this());

        // Update descriptor set for new texture
        entry.image_view = update_descriptor_set_from_texture(entry, texture);
        return entry.set;
    };

    if (auto it = lookup_table.find(texture->get_wlr_texture());it != lookup_table.end())
    {
        auto& allocated = allocated_sets[it->second];
        if ((texture->get_source_box() == allocated->last_texture->get_source_box()) &&
            (texture->get_transform() == allocated->last_texture->get_transform()) &&
            (allocated->owner == &cmd_buf))
        {
            // Fast path: we already have a descriptor set for this texture, just reuse it.
            return allocated->set;
        }
    }

    for (size_t i = 0; i < allocated_sets.size(); ++i)
    {
        size_t idx = (next_set + i) % allocated_sets.size();
        if (!allocated_sets[idx]->owner)
        {
            next_set = (idx + 1) % allocated_sets.size();
            return use_set(idx);
        }
    }

    if (allocated_sets.size() < max_size)
    {
        // Allocate a new descriptor set
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool     = pool;
        alloc_info.descriptorSetCount = 1;
        VkDescriptorSetLayout layout = context->get_image_descriptor_set_layout();
        alloc_info.pSetLayouts = &layout;

        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(context->get_device(), &alloc_info, &set) != VK_SUCCESS)
        {
            LOGE("Failed to allocate Vulkan descriptor set");
            return VK_NULL_HANDLE;
        }

        auto entry = std::make_unique<allocated_set_t>();
        entry->set = set;
        // 3x3 matrix in std140 layout = 3 vec4s = 48 bytes
        entry->matrix_buffer = context->create_buffer(48, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        if (!entry->matrix_buffer)
        {
            LOGE("Failed to allocate matrix buffer for descriptor set");
            return VK_NULL_HANDLE;
        }

        allocated_sets.push_back(std::move(entry));
        next_set = 0;
        return use_set(allocated_sets.size() - 1);
    }

    LOGW("Descriptor set pool exhausted!");

    size_t use_idx = next_set;
    next_set = (next_set + 1) % allocated_sets.size();
    return use_set(use_idx);
}

command_buffer_t::command_buffer_t(std::shared_ptr<context_t> ctx, wf::render_pass_t& pass) :
    context(ctx)
{
    this->cmd = wlr_vk_render_pass_get_command_buffer(pass.get_wlr_pass());
    this->current_pass = wlr_vk_render_pass_get_render_pass(pass.get_wlr_pass());
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

void command_buffer_t::bind_pipeline(std::shared_ptr<graphics_pipeline_t> pipeline,
    const wf::render_target_t& target, const pipeline_specialization_t& specialization)
{
    bound_pipelines.push_back(pipeline);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->pipeline_for(current_pass, specialization));
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
    for (const auto& box : damage)
    {
        VkRect2D scissor{
            .offset = {box.x1, box.y1},
            .extent = {static_cast<uint32_t>(box.x2 - box.x1), static_cast<uint32_t>(box.y2 - box.y1)},
        };

        vkCmdSetScissor(*this, 0, 1, &scissor);
        callback();
    }
}

context_t::context_t(wlr_renderer *renderer) : renderer(renderer)
{
    this->device = wlr_vk_renderer_get_device(renderer);
    this->physical_device = wlr_vk_renderer_get_physical_device(renderer);
    uint32_t queue_family = wlr_vk_renderer_get_queue_family(renderer);

    // Get the graphics queue
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // Create a command pool
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = queue_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) != VK_SUCCESS)
    {
        LOGE("Failed to create command pool");
        command_pool = VK_NULL_HANDLE;
    }

    // Create descriptor set layout for image sampling (sampler + 3x3 matrix)
    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 1;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    sampler_binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding matrix_binding{};
    matrix_binding.binding = 1;
    matrix_binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrix_binding.descriptorCount = 1;
    matrix_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    matrix_binding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {sampler_binding, matrix_binding};
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = bindings.size();
    layout_info.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
        &image_descriptor_set_layout) != VK_SUCCESS)
    {
        LOGE("Failed to create descriptor set layout for image sampling");
        image_descriptor_set_layout = VK_NULL_HANDLE;
    }

    // Create linear and nearest samplers, which can be reused for pretty much every texture we use.
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy    = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp     = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias    = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;

    if (vkCreateSampler(get_device(), &sampler_info, nullptr, &linear_sampler) != VK_SUCCESS)
    {
        LOGE("Failed to create Vulkan sampler");
    }

    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    if (vkCreateSampler(get_device(), &sampler_info, nullptr, &nearest_sampler) != VK_SUCCESS)
    {
        LOGE("Failed to create Vulkan sampler");
    }
}

context_t::~context_t()
{
    if (image_descriptor_set_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, image_descriptor_set_layout, nullptr);
    }

    if (command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, command_pool, nullptr);
    }

    if (linear_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, linear_sampler, nullptr);
    }

    if (nearest_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, nearest_sampler, nullptr);
    }
}

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

    // Create pipeline layout from descriptor set layouts and push constants
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = params.descriptorSetLayouts.size();
    layout_info.pSetLayouts    = params.descriptorSetLayouts.data();
    layout_info.pushConstantRangeCount = params.pushConstants.size();
    layout_info.pPushConstantRanges    = params.pushConstants.data();
    if (vkCreatePipelineLayout(ctx->get_device(), &layout_info, nullptr, &_layout) != VK_SUCCESS)
    {
        LOGE("Failed to create pipeline layout");
        _layout = VK_NULL_HANDLE;
    }
}

graphics_pipeline_t::~graphics_pipeline_t()
{
    // Destroy all pipelines and render passes
    for (auto& [pass, pf] : pipelines)
    {
        vkDestroyPipeline(context->get_device(), pf, nullptr);
    }

    if (_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(context->get_device(), _layout, nullptr);
        _layout = VK_NULL_HANDLE;
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

VkPipeline graphics_pipeline_t::pipeline_for(VkRenderPass pass,
    const pipeline_specialization_t& specialization)
{
    pipeline_key_t key{pass, specialization.get_data()};
    auto it = pipelines.find(key);
    if (it != pipelines.end())
    {
        return it->second;
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
    vertex_input_info.vertexBindingDescriptionCount = params.vertexInputDescription.size();
    vertex_input_info.pVertexBindingDescriptions    = params.vertexInputDescription.data();
    vertex_input_info.vertexAttributeDescriptionCount = params.vertexAttributeDescription.size();
    vertex_input_info.pVertexAttributeDescriptions    = params.vertexAttributeDescription.data();

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
    pipeline_info.layout     = _layout;
    pipeline_info.renderPass = pass;
    pipeline_info.subpass    = 0;
    pipeline_info.pDynamicState = &dynamic_state_info;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(
        context->get_device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS)
    {
        LOGE("Failed to create graphics pipeline for pass: ", pass);
        return VK_NULL_HANDLE;
    }

    // Store the pipeline keyed by (render pass, specialization data)
    pipelines[key] = pipeline;
    return pipeline;
}

VkPipelineLayout graphics_pipeline_t::layout()
{
    return _layout;
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
    this->descriptor_pool = std::make_shared<wf::vk::image_descriptor_set_pool_t>(context, 16384);
}

vulkan_render_state_t& vulkan_render_state_t::get()
{
    auto& core_impl = wf::get_core_impl();
    return *core_impl.vulkan_state;
}
} // namespace wf

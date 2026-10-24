#include "particle.hpp"
#include "shaders.hpp"
#include <wayfire/core.hpp>

void Particle::update(float time)
{
    if (life <= 0) // ignore
    {
        return;
    }

    const float slowdown = 0.8;

    pos   += speed * 0.2f * slowdown;
    speed += g * 0.3f * slowdown;

    if (life != 0)
    {
        color.a /= life;
    }

    life    -= fade * 0.3 * slowdown;
    radius   = base_radius * std::pow(life, 0.5);
    color.a *= life;

    if (start_pos.x < pos.x)
    {
        g.x = -1;
    } else
    {
        g.x = 1;
    }

    if (life <= 0)
    {
        /* move outside */
        pos = {-10000, -10000};
    }
}

ParticleSystem::ParticleSystem(int particles)
{
    resize(particles);
    last_update_msec = wf::get_current_time();

    particles_alive.store(0);
}

void ParticleSystem::set_initer(ParticleIniter init)
{
    this->pinit_func = init;
}

ParticleSystem::~ParticleSystem()
{
    if (gl_program_created)
    {
        wf::gles::run_in_context([&]
        {
            program.free_resources();
        });
    }
}

int ParticleSystem::spawn(int num)
{
    std::atomic<int> spawned(0);

#   pragma omp parallel for
    for (size_t i = 0; i < ps.size(); i++)
    {
        if ((ps[i].life <= 0) && (spawned < num))
        {
            pinit_func(ps[i]);
            ++spawned;
            ++particles_alive;
        }
    }

    return spawned;
}

void ParticleSystem::resize(int num)
{
    if (num == (int)ps.size())
    {
        return;
    }

#   pragma omp parallel for
    for (size_t i = num; i < ps.size(); i++)
    {
        if (ps[i].life >= 0)
        {
            --particles_alive;
        }
    }

    ps.resize(num);

    color.resize(color_per_particle * num);
    dark_color.resize(color_per_particle * num);
    radius.resize(radius_per_particle * num);
    center.resize(center_per_particle * num);
}

int ParticleSystem::size()
{
    return ps.size();
}

void ParticleSystem::update_worker(float time, int i)
{
    if (ps[i].life <= 0)
    {
        return;
    }

    ps[i].update(time);

    if (ps[i].life <= 0)
    {
        --particles_alive;
    }

    for (int j = 0; j < 4; j++) // maybe use memcpy?
    {
        color[4 * i + j] = ps[i].color[j];
        dark_color[4 * i + j] = ps[i].color[j] * 0.5;
    }

    center[2 * i]     = ps[i].pos[0];
    center[2 * i + 1] = ps[i].pos[1];

    radius[i] = ps[i].radius;
}

void ParticleSystem::update()
{
    // FIXME: don't hardcode 60FPS
    float time = (wf::get_current_time() - last_update_msec) / 16.0;
    last_update_msec = wf::get_current_time();

#   pragma omp parallel for
    for (size_t i = 0; i < ps.size(); i++)
    {
        update_worker(time, i);
    }
}

int ParticleSystem::statistic()
{
    return particles_alive;
}

void ParticleSystem::create_program()
{
    wf::gles::run_in_context([&]
    {
        program.set_simple(OpenGL::compile_program(particle_vert_source,
            particle_frag_source));
    });
    gl_program_created = true;
}

void ParticleSystem::render(glm::mat4 matrix)
{
    if (!gl_program_created)
    {
        create_program();
    }

    program.use(wf::TEXTURE_TYPE_RGBA);
    static float vertex_data[] = {
        -1, -1,
        1, -1,
        1, 1,
        -1, 1
    };

    program.attrib_pointer("position", 2, 0, vertex_data);
    program.attrib_divisor("position", 0);

    program.attrib_pointer("radius", 1, 0, radius.data());
    program.attrib_divisor("radius", 1);

    program.attrib_pointer("center", 2, 0, center.data());
    program.attrib_divisor("center", 1);

    // matrix
    program.uniformMatrix4f("matrix", matrix);

    /* Darken the background */
    program.attrib_pointer("color", 4, 0, dark_color.data());
    program.attrib_divisor("color", 1);

    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA));
    program.uniform1f("smoothing", 0.7);

    // TODO: optimize shaders for this case
    GL_CALL(glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, ps.size()));

    // particle color
    program.attrib_pointer("color", 4, 0, color.data());
    GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE));
    program.uniform1f("smoothing", 0.5);
    GL_CALL(glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, ps.size()));

    GL_CALL(glDisable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

    program.deactivate();
}

#if WF_HAS_VULKANFX
ParticleSystem::fire_vk_state_t& ParticleSystem::ensure_vk_state(
    wf::vulkan_render_state_t& state)
{
    if (auto data = state.get_data<fire_vk_state_t>())
    {
        return *data;
    }

    auto ctx = state.get_context();
    auto vs  = ctx->load_shader_module(fire_vert_data, sizeof(fire_vert_data));
    auto fs  = ctx->load_shader_module(fire_frag_data, sizeof(fire_frag_data));

    /* Per-instance attributes: center (2), radius (1), color (4).
     * The quad corner position is a per-vertex attribute. */
    wf::vk::pipeline_params_t params{};
    params.shaders = {
        {.stage = VK_SHADER_STAGE_VERTEX_BIT, .shader = vs},
        {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .shader = fs},
    };

    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    params.vertex_input_description = {
        {
            .binding   = 0,
            .stride    = sizeof(float) * 2,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        {
            .binding   = 1,
            .stride    = sizeof(float) * 7,
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        },
    };
    params.vertex_attribute_description = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = 0,
        },
        {
            .location = 1,
            .binding  = 1,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = 0,
        },
        {
            .location = 2,
            .binding  = 1,
            .format   = VK_FORMAT_R32_SFLOAT,
            .offset   = sizeof(float) * 2,
        },
        {
            .location = 3,
            .binding  = 1,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset   = sizeof(float) * 3,
        },
    };

    params.push_constants = {
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = sizeof(vulkan_push_constants_t),
        },
    };

    auto data = std::make_unique<fire_vk_state_t>();

    /* Pass 1: darken the background behind the particle.
     * GL: (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA) => out = dst * (1 - src.a) */
    params.blending.blend_op = VK_BLEND_OP_ADD;
    params.blending.src_factor = VK_BLEND_FACTOR_ZERO;
    params.blending.dst_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    params.blending.alpha_blend_op = {};
    params.blending.alpha_src_factor = {};
    params.blending.alpha_dst_factor = {};
    data->darken_pipeline = std::make_shared<wf::vk::graphics_pipeline_t>(ctx, params);

    /* Pass 2: additive particle color.
     * GL: (GL_SRC_ALPHA, GL_ONE) => out = src * src.a + dst */
    params.blending.src_factor = VK_BLEND_FACTOR_SRC_ALPHA;
    params.blending.dst_factor = VK_BLEND_FACTOR_ONE;
    data->additive_pipeline = std::make_shared<wf::vk::graphics_pipeline_t>(ctx, params);

    auto ptr = data.get();
    state.store_data<fire_vk_state_t>(std::move(data));
    return *ptr;
}

void ParticleSystem::upload_instance_data(wf::vulkan_render_state_t& state, int count)
{
    /* Interleaved instance data: [center.x, center.y, radius, r, g, b, a]. */
    std::vector<float> instance_data;
    instance_data.reserve(count * 7);

    for (int i = 0; i < count; i++)
    {
        instance_data.push_back(center[2 * i]);
        instance_data.push_back(center[2 * i + 1]);
        instance_data.push_back(radius[i]);
        instance_data.push_back(color[4 * i]);
        instance_data.push_back(color[4 * i + 1]);
        instance_data.push_back(color[4 * i + 2]);
        instance_data.push_back(color[4 * i + 3]);
    }

    const VkDeviceSize total_size = instance_data.size() * sizeof(float);

    /* Reuse the buffer between frames when possible, to avoid reallocations. */
    if (!instance_buffer || (instance_buffer->get_size() < total_size) ||
        (instance_buffer.use_count() != 1))
    {
        instance_buffer = state.get_context()->create_buffer(
            total_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    instance_buffer->write(instance_data.data(), total_size);
}

void ParticleSystem::render_vk(wf::vulkan_render_state_t& state,
    wf::vk::command_buffer_t& cmd_buf, const wf::render_target_t& target,
    const wf::regionf_t& damage, glm::mat4 matrix)
{
    const int count = ps.size();
    if (count <= 0)
    {
        return;
    }

    upload_instance_data(state, count);

    auto& vk_state = ensure_vk_state(state);

    static const float vertex_data[] = {
        -1, -1,
        1, -1,
        1, 1,
        -1, 1,
    };

    auto vertex_buffer = state.get_context()->create_buffer(
        sizeof(vertex_data), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vertex_buffer->write(vertex_data, sizeof(vertex_data));

    vulkan_push_constants_t push_constants{};
    push_constants.matrix     = matrix;
    push_constants.smoothing  = 0.5;

    for (int pass = 0; pass < 2; pass++)
    {
        auto pipeline = (pass == 0) ? vk_state.darken_pipeline : vk_state.additive_pipeline;
        push_constants.smoothing = (pass == 0) ? 0.7 : 0.5;

        auto [layout, _] = cmd_buf.bind_pipeline(pipeline, target);
        cmd_buf.set_full_viewport(target);

        vkCmdPushConstants(cmd_buf, layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(vulkan_push_constants_t), &push_constants);

        cmd_buf.bind_buffer(vertex_buffer);
        cmd_buf.bind_buffer(instance_buffer);

        VkDeviceSize vertex_offset = 0;
        VkBuffer vertex_buffers[]  = {vertex_buffer->get_buffer(), instance_buffer->get_buffer()};
        VkDeviceSize offsets[] = {0, 0};
        vkCmdBindVertexBuffers(cmd_buf, 0, 2, vertex_buffers, offsets);

        cmd_buf.for_each_scissor_rect(target, damage, [&]
        {
            vkCmdDraw(cmd_buf, 4, count, 0, 0);
        });
    }
}
#endif

#ifndef ANIMATION_FIRE_PARTICLE_HPP
#define ANIMATION_FIRE_PARTICLE_HPP

#include <wayfire/opengl.hpp>
#ifdef WF_USE_CONFIG_H
    #include <config.h>
#else
    #include <wayfire/config.h>
#endif
#include <functional>
#include <atomic>
#include <vector>

#if WF_HAS_VULKANFX
    #include <wayfire/vulkan.hpp>
    #include "shaders/fire.vert.h"
    #include "shaders/fire.frag.h"
#endif

struct Particle
{
    float life = -1;
    float fade;

    float radius, base_radius;

    glm::vec2 pos{0.0, 0.0}, speed{0.0, 0.0}, g{0.0, 0.0};
    glm::vec2 start_pos;

    glm::vec4 color{1.0, 1.0, 1.0, 1.0};

    /* update the given particle
     * time is the percentage of the frame which has elapsed
     * must be thread-safe */
    void update(float time);
};

/* a function to initialize a particle */
using ParticleIniter = std::function<void (Particle&)>;

class ParticleSystem
{
  public:
    /* the user of this class has to set up a proper GL context
     * before creating the ParticleSystem */
    ParticleSystem(int num_part);
    ~ParticleSystem();
    void set_initer(ParticleIniter init);

    ParticleSystem(const ParticleSystem &) = delete;
    ParticleSystem(ParticleSystem &&) = delete;
    ParticleSystem& operator =(const ParticleSystem&) = delete;
    ParticleSystem& operator =(ParticleSystem&&) = delete;

    /* spawn at most num new particles.
     * returns the number of actually spawned particles */
    int spawn(int num);

    /* change the maximal number of particles
     * Warning: This might kill a lot of particles */
    void resize(int num);

    // return the maximal number of particles
    int size();

    /* update all particles */
    void update();

    // number of particles alive
    int statistic();

    /* render particles, each will be multiplied by matrix
     * The user of this class has to set up the same GL context that was
     * used during the creation of the particle system */
    void render(glm::mat4 matrix);

  private:
    ParticleSystem() = delete;

    ParticleIniter pinit_func = [] (auto) {};
    uint32_t last_update_msec;

    std::atomic<int> particles_alive;
    std::vector<Particle> ps;

    static constexpr int color_per_particle = 4;
    std::vector<float> color, dark_color;

    static constexpr int radius_per_particle = 1;
    std::vector<float> radius;

    static constexpr int center_per_particle = 2;
    std::vector<float> center;

    OpenGL::program_t program;
    bool gl_program_created = false;
    void update_worker(float time, int i);
    void create_program();

#if WF_HAS_VULKANFX
  public:
    /* Render the particles with the given vulkan render state.
     * Performs two draws: one to darken the background and one additive
     * pass for the particle colors, mirroring the GLES implementation. */
    void render_vk(wf::vulkan_render_state_t& state, wf::vk::command_buffer_t& cmd_buf,
        const wf::render_target_t& target, const wf::regionf_t& damage, glm::mat4 matrix);

  private:
    struct vulkan_push_constants_t
    {
        glm::mat4 matrix;
        float smoothing;
    };

    class fire_vk_state_t : public wf::custom_data_t
    {
      public:
        std::shared_ptr<wf::vk::graphics_pipeline_t> darken_pipeline;
        std::shared_ptr<wf::vk::graphics_pipeline_t> additive_pipeline;
    };

    std::shared_ptr<wf::vk::gpu_buffer_t> instance_buffer;
    fire_vk_state_t& ensure_vk_state(wf::vulkan_render_state_t& state);
    void upload_instance_data(wf::vulkan_render_state_t& state, int count);
#endif
};


#endif /* end of include guard: ANIMATION_FIRE_PARTICLE_HPP */

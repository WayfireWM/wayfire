#ifndef ANIMATION_FIRE_PARTICLE_HPP
#define ANIMATION_FIRE_PARTICLE_HPP

#include <wayfire/opengl.hpp>
#include <functional>
#include <atomic>
#include <vector>

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
    void update_worker(float time, int i);
    void create_program();
};


#endif /* end of include guard: ANIMATION_FIRE_PARTICLE_HPP */

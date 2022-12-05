#include "particle.hpp"
#include "shaders.hpp"
#include <wayfire/core.hpp>
#include <thread>

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
    create_program();

    particles_alive.store(0);
}

void ParticleSystem::set_initer(ParticleIniter init)
{
    this->pinit_func = init;
}

ParticleSystem::~ParticleSystem()
{
    OpenGL::render_begin();
    program.free_resources();
    OpenGL::render_end();
}

int ParticleSystem::spawn(int num)
{
    // TODO: multithread this
    int spawned = 0;
    for (size_t i = 0; i < ps.size() && spawned < num; i++)
    {
        if (ps[i].life <= 0)
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

    // TODO: multithread this
    for (int i = num; i < (int)ps.size(); i++)
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

void ParticleSystem::update_worker(float time, int start, int end)
{
    end = std::min(end, (int)ps.size());
    for (int i = start; i < end; ++i)
    {
        if (ps[i].life <= 0)
        {
            continue;
        }

        // printf("%d\n", i);
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

        // printf("center %d gets %f", 2 * i, ps[i].pos[0]);
        center[2 * i]     = ps[i].pos[0];
        center[2 * i + 1] = ps[i].pos[1];

        radius[i] = ps[i].radius;
    }
}

void ParticleSystem::exec_worker_threads(std::function<void(int, int)> spawn_worker)
{
// return spawn_worker(0, ps.size());

    const int num_threads = std::thread::hardware_concurrency();
    const int worker_load = (ps.size() + num_threads - 1) / num_threads;

    std::thread workers[num_threads];
    for (int i = 0; i < num_threads; i++)
    {
        int thread_start = i * worker_load;
        int thread_end   = (i + 1) * worker_load;
        thread_end = std::min(thread_end, (int)ps.size());

        workers[i] = std::thread([=] () { spawn_worker(thread_start, thread_end); });
    }

    for (auto& w : workers)
    {
        w.join();
    }
}

void ParticleSystem::update()
{
    // FIXME: don't hardcode 60FPS
    float time = (wf::get_current_time() - last_update_msec) / 16.0;
    last_update_msec = wf::get_current_time();

    exec_worker_threads([=] (int start, int end)
    {
        update_worker(time, start, end);
    });
}

int ParticleSystem::statistic()
{
    return particles_alive;
}

void ParticleSystem::create_program()
{
    /* Just load the proper context, viewport doesn't matter */
    OpenGL::render_begin();
    program.set_simple(OpenGL::compile_program(particle_vert_source,
        particle_frag_source));
    OpenGL::render_end();
}

void ParticleSystem::render(glm::mat4 matrix)
{
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

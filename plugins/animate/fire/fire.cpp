#include "fire.hpp"
#include "particle.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/scene-render.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/view-transform.hpp"

#include <memory>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <glm/gtc/matrix_transform.hpp>

static wf::option_wrapper_t<int> fire_particles{"animate/fire_particles"};
static wf::option_wrapper_t<double> fire_particle_size{"animate/fire_particle_size"};
static wf::option_wrapper_t<bool> random_fire_color{"animate/random_fire_color"};
static wf::option_wrapper_t<wf::color_t> fire_color{"animate/fire_color"};

// generate a random float between s and e
static float random(float s, float e)
{
    double r = 1.0 * (std::rand() % RAND_MAX) / (RAND_MAX - 1);

    return (s * r + (1 - r) * e);
}

static int particle_count_for_width(int width)
{
    int particles = fire_particles;

    return particles * std::min(width / 400.0, 3.5);
}

class fire_node_t : public wf::scene::floating_inner_node_t
{
  public:
    std::unique_ptr<ParticleSystem> ps;
    fire_node_t() : floating_inner_node_t(false)
    {
        ps = std::make_unique<ParticleSystem>(1);
        ps->set_initer(
            [=] (Particle& p)
        {
            init_particle_with_node(p, get_children_bounding_box(), progress_line);
        });
    }

    static void init_particle_with_node(Particle& p,
        wf::geometry_t bounding_box, double progress)
    {
        p.life = 1;
        p.fade = random(0.1, 0.6);

        wf::color_t color_setting = fire_color;

        float r;
        float g;
        float b;

        if (!random_fire_color)
        {
            // The calculation here makes the variation lower at darker values
            float randomize_amount_r = (color_setting.r * 0.857) / 2;
            float randomize_amount_g = (color_setting.g * 0.857) / 2;
            float randomize_amount_b = (color_setting.b * 0.857) / 2;

            r = random(color_setting.r - randomize_amount_r,
                std::min(color_setting.r + randomize_amount_r,
                    1.0));
            g = random(color_setting.g - randomize_amount_g,
                std::min(color_setting.g + randomize_amount_g,
                    1.0));
            b = random(color_setting.b - randomize_amount_b,
                std::min(color_setting.b + randomize_amount_b,
                    1.0));
        } else
        {
            r = random(0, 1);
            g = random(0, 1);
            b = random(0, 1);

            r = 2 * pow(r, 16);
            g = 2 * pow(g, 16);
            b = 2 * pow(b, 16);
        }

        p.color = {r, g, b, 1};

        const double cur_pos = bounding_box.height * progress;
        p.pos = {random(0, bounding_box.width), random(cur_pos - 10, cur_pos + 10)};
        p.start_pos = p.pos;
        p.speed     = {random(-10, 10), random(-25, 5)};
        p.g = {-1, -3};

        double size = fire_particle_size;
        p.base_radius = p.radius = random(size * 0.8, size * 1.2);
    }

    std::string stringify() const override
    {
        return "fire";
    }

    void gen_render_instances(
        std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage,
        wf::output_t *output = nullptr) override;

    wf::geometry_t get_bounding_box() override
    {
        static constexpr int left_border   = 200;
        static constexpr int right_border  = 200;
        static constexpr int top_border    = 200;
        static constexpr int bottom_border = 200;

        auto view = get_children_bounding_box();
        view.x     -= left_border;
        view.y     -= top_border;
        view.width += left_border + right_border;
        view.height += top_border + bottom_border;
        return view;
    }

    float progress_line;
    void set_progress_line(float line)
    {
        progress_line = line;
    }
};

class fire_render_instance_t : public wf::scene::render_instance_t
{
    fire_node_t *self;

  public:
    fire_render_instance_t(fire_node_t *self,
        wf::scene::damage_callback push_damage,
        wf::output_t *output)
    {
        this->self = self;

        auto child_damage = [=] (const wf::region_t& damage)
        {
            push_damage(damage | self->get_bounding_box());
        };

        for (auto& ch : self->get_children())
        {
            if (ch->is_enabled())
            {
                ch->gen_render_instances(children, child_damage, output);
            }
        }
    }

    void schedule_instructions(
        std::vector<wf::scene::render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (children.empty())
        {
            return;
        }

        // Step 2: we render ourselves
        auto bbox = self->get_bounding_box();
        instructions.push_back(wf::scene::render_instruction_t{
            .instance = this,
            .target   = target,
            .damage   = damage & bbox,
        });

        // Step 1: render the view below normally, however, make sure it doesn't
        // render above the progress line
        bbox = self->get_children_bounding_box();
        bbox.height *= self->progress_line;
        auto child_damage = damage & bbox;
        for (auto& ch : children)
        {
            ch->schedule_instructions(instructions, target, child_damage);
        }
    }

    void render(const wf::render_target_t& target_fb,
        const wf::region_t& region) override
    {
        OpenGL::render_begin(target_fb);
        auto bbox = self->get_children_bounding_box();
        auto translate =
            glm::translate(glm::mat4(1.0), {bbox.x, bbox.y, 0});

        for (auto& box : region)
        {
            target_fb.logic_scissor(wlr_box_from_pixman_box(box));
            self->ps->render(target_fb.get_orthographic_projection() * translate);
        }

        OpenGL::render_end();
    }

    void presentation_feedback(wf::output_t *output) override
    {
        for (auto& ch : children)
        {
            ch->presentation_feedback(output);
        }
    }

    void compute_visibility(wf::output_t *output, wf::region_t& visible) override
    {
        for (auto& ch : this->children)
        {
            ch->compute_visibility(output, visible);
        }
    }

  private:
    std::vector<wf::scene::render_instance_uptr> children;
};

void fire_node_t::gen_render_instances(
    std::vector<wf::scene::render_instance_uptr>& instances,
    wf::scene::damage_callback push_damage,
    wf::output_t *output)
{
    instances.push_back(std::make_unique<fire_render_instance_t>(
        this, push_damage, output));
}

static float fire_duration_mod_for_height(int height)
{
    return std::min(height / 400.0, 3.0);
}

void FireAnimation::init(wayfire_view view, wf::animation_description_t dur, wf_animation_type type)
{
    this->view = view;

    auto bbox = view->get_transformed_node()->get_bounding_box();
    dur.length_ms    *= fire_duration_mod_for_height(bbox.height);
    this->progression = wf::animation::simple_animation_t(
        wf::create_option<wf::animation_description_t>(dur));
    this->progression.animate(0, 1);

    if (type & HIDING_ANIMATION)
    {
        this->progression.flip();
    }

    name = "animation-fire-" + std::to_string(type);

    auto tr = std::make_shared<fire_node_t>();
    view->get_transformed_node()->add_transformer(
        tr, wf::TRANSFORMER_HIGHLEVEL + 1, name);
}

bool FireAnimation::step()
{
    auto transformer = view->get_transformed_node()
        ->get_transformer<fire_node_t>(name);

    transformer->set_progress_line(this->progression);
    if (this->progression.running())
    {
        transformer->ps->spawn(transformer->ps->size() / 10);
    }

    transformer->ps->update();
    transformer->ps->resize(particle_count_for_width(
        transformer->get_children_bounding_box().width));
    return this->progression.running() || transformer->ps->statistic();
}

void FireAnimation::reverse()
{
    this->progression.reverse();
}

FireAnimation::~FireAnimation()
{
    view->get_transformed_node()->rem_transformer(name);
}

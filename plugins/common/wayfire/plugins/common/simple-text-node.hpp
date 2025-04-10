#include "wayfire/opengl.hpp"
#include "wayfire/output.hpp"
#include "wayfire/scene.hpp"
#include <wayfire/scene-render.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

class simple_text_node_t : public wf::scene::node_t
{
    class render_instance_t : public wf::scene::simple_render_instance_t<simple_text_node_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::scene::render_instruction_t& data)
        {
            auto g = self->get_bounding_box();
            data.pass->custom_gles_subpass(data.target, [&]
            {
                for (auto box : data.damage)
                {
                    wf::gles::render_target_logic_scissor(data.target, wlr_box_from_pixman_box(box));
                    OpenGL::render_texture(self->cr_text.tex.tex, data.target, g, glm::vec4(1.0f),
                        OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
                }
            });
        }
    };

    wf::cairo_text_t cr_text;

  public:
    simple_text_node_t() : node_t(false)
    {}

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        return wf::construct_box(position, size.value_or(cr_text.get_size()));
    }

    void set_position(wf::point_t position)
    {
        this->position = position;
    }

    void set_size(wf::dimensions_t size)
    {
        this->size = size;
    }

    void set_text_params(wf::cairo_text_t::params params)
    {
        this->params = params;
    }

    void set_text(std::string text)
    {
        wf::scene::damage_node(this->shared_from_this(), get_bounding_box());
        cr_text.render_text(text, params);
        wf::scene::damage_node(this->shared_from_this(), get_bounding_box());
    }

  private:
    wf::cairo_text_t::params params;
    std::optional<wf::dimensions_t> size;
    wf::point_t position;
};

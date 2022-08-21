#ifndef WF_CUBE_CUBEMAP_HPP
#define WF_CUBE_CUBEMAP_HPP

#include "cube-background.hpp"

class wf_cube_background_cubemap : public wf_cube_background_base
{
  public:
    wf_cube_background_cubemap();
    virtual void render_frame(const wf::render_target_t& fb,
        wf_cube_animation_attribs& attribs) override;

    ~wf_cube_background_cubemap();

  private:
    void reload_texture();
    void create_program();

    OpenGL::program_t program;
    GLuint tex = -1;
    GLuint vbo_cube_vertices;
    GLuint ibo_cube_indices;

    std::string last_background_image;
    wf::option_wrapper_t<std::string> background_image{"cube/cubemap_image"};
};

#endif /* end of include guard: WF_CUBE_CUBEMAP_HPP */

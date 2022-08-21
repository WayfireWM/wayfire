#ifndef WF_CUBE_BACKGROUND_SKYDOME
#define WF_CUBE_BACKGROUND_SKYDOME

#include "cube-background.hpp"
#include "wayfire/output.hpp"
#include <vector>

class wf_cube_background_skydome : public wf_cube_background_base
{
  public:
    wf_cube_background_skydome(wf::output_t *output);
    virtual void render_frame(const wf::render_target_t& fb,
        wf_cube_animation_attribs& attribs) override;

    virtual ~wf_cube_background_skydome();

  private:
    wf::output_t *output;

    void load_program();
    void fill_vertices();
    void reload_texture();

    OpenGL::program_t program;
    GLuint tex = -1;

    std::vector<GLfloat> vertices;
    std::vector<GLfloat> coords;
    std::vector<GLuint> indices;

    std::string last_background_image;
    int last_mirror = -1;
    wf::option_wrapper_t<std::string> background_image{"cube/skydome_texture"};
    wf::option_wrapper_t<bool> mirror_opt{"cube/skydome_mirror"};
};

#endif /* end of include guard: WF_CUBE_BACKGROUND_SKYDOME */

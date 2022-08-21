#include <wayfire/core.hpp>
#include "simple-background.hpp"

wf_cube_simple_background::wf_cube_simple_background()
{}

void wf_cube_simple_background::render_frame(const wf::render_target_t& fb,
    wf_cube_animation_attribs&)
{
    OpenGL::render_begin(fb);
    OpenGL::clear(background_color, GL_COLOR_BUFFER_BIT);
    OpenGL::render_end();
}

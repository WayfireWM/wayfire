#include "skydome.hpp"
#include <wayfire/core.hpp>
#include <wayfire/img.hpp>

#include <wayfire/output.hpp>
#include <wayfire/workspace-set.hpp>


#include <glm/gtc/matrix_transform.hpp>
#include "shaders.tpp"

#define SKYDOME_GRID_WIDTH 128
#define SKYDOME_GRID_HEIGHT 128

wf_cube_background_skydome::wf_cube_background_skydome(wf::output_t *output)
{
    this->output = output;
    load_program();
    reload_texture();
}

wf_cube_background_skydome::~wf_cube_background_skydome()
{
    OpenGL::render_begin();
    program.free_resources();
    if (tex != (GLuint) - 1)
    {
        GL_CALL(glDeleteTextures(1, &tex));
    }

    OpenGL::render_end();
}

void wf_cube_background_skydome::load_program()
{
    OpenGL::render_begin();
    program.set_simple(OpenGL::compile_program(cube_vertex_2_0, cube_fragment_2_0));
    OpenGL::render_end();
}

void wf_cube_background_skydome::reload_texture()
{
    if (!last_background_image.compare(background_image))
    {
        return;
    }

    last_background_image = background_image;
    OpenGL::render_begin();

    if (tex == (uint32_t)-1)
    {
        GL_CALL(glGenTextures(1, &tex));
    }

    GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));

    if (image_io::load_from_file(last_background_image, GL_TEXTURE_2D))
    {
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    } else
    {
        LOGE("Failed to load skydome image from \"%s\".",
            last_background_image.c_str());
        GL_CALL(glDeleteTextures(1, &tex));
        tex = -1;
    }

    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));

    OpenGL::render_end();
}

void wf_cube_background_skydome::fill_vertices()
{
    if (mirror_opt == last_mirror)
    {
        return;
    }

    last_mirror = mirror_opt;

    float scale = 75.0;
    int gw = SKYDOME_GRID_WIDTH + 1;
    int gh = SKYDOME_GRID_HEIGHT;

    vertices.clear();
    indices.clear();
    coords.clear();

    for (int i = 1; i < gh; i++)
    {
        for (int j = 0; j < gw; j++)
        {
            float theta = ((2 * M_PI) / (gw - 1)) * j;
            float phi   = (M_PI / gh) * i;

            vertices.push_back(cos(theta) * sin(phi) * scale);
            vertices.push_back(cos(phi) * scale);
            vertices.push_back(sin(theta) * sin(phi) * scale);

            if (last_mirror == 0)
            {
                coords.push_back((float)j / (gw - 1));
                coords.push_back((float)(i - 1) / (gh - 2));
            } else
            {
                float u = ((float)j / (gw - 1)) * 2.0;
                coords.push_back(u - ((u > 1.0) ? (2.0 * (u - 1.0)) : 0));
                coords.push_back((float)(i - 1) / (gh - 2));
            }
        }
    }

    for (int i = 1; i < gh - 1; i++)
    {
        for (int j = 0; j < gw - 1; j++)
        {
            indices.push_back((i - 1) * gw + j);
            indices.push_back((i - 1) * gw + j + gw);
            indices.push_back((i - 1) * gw + j + 1);
            indices.push_back((i - 1) * gw + j + 1);
            indices.push_back((i - 1) * gw + j + gw);
            indices.push_back((i - 1) * gw + j + gw + 1);
        }
    }
}

void wf_cube_background_skydome::render_frame(const wf::render_target_t& fb,
    wf_cube_animation_attribs& attribs)
{
    fill_vertices();
    reload_texture();

    if (tex == (uint32_t)-1)
    {
        GL_CALL(glClearColor(TEX_ERROR_FLAG_COLOR));
        GL_CALL(glClear(GL_COLOR_BUFFER_BIT));

        return;
    }

    OpenGL::render_begin(fb);
    program.use(wf::TEXTURE_TYPE_RGBA);

    auto rotation = glm::rotate(glm::mat4(1.0),
        (float)(attribs.cube_animation.offset_y * 0.5),
        glm::vec3(1., 0., 0.));

    auto view = glm::lookAt(glm::vec3(0., 0., 0.),
        glm::vec3(0., 0., -attribs.cube_animation.offset_z),
        glm::vec3(0., 1., 0.));

    auto vp = fb.transform * attribs.projection * view * rotation;
    program.uniformMatrix4f("VP", vp);

    program.attrib_pointer("position", 3, 0, vertices.data());
    program.attrib_pointer("uvPosition", 2, 0, coords.data());

    auto cws   = output->wset()->get_current_workspace();
    auto model = glm::rotate(glm::mat4(1.0),
        float(attribs.cube_animation.rotation) - cws.x * attribs.side_angle,
        glm::vec3(0, 1, 0));

    program.uniformMatrix4f("model", model);

    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));

    GL_CALL(glDrawElements(GL_TRIANGLES,
        6 * SKYDOME_GRID_WIDTH * (SKYDOME_GRID_HEIGHT - 2),
        GL_UNSIGNED_INT, indices.data()));

    program.deactivate();
    OpenGL::render_end();
}

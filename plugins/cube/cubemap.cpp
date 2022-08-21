#include "cubemap.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <config.h>
#include <wayfire/core.hpp>
#include <wayfire/img.hpp>

#include "cubemap-shaders.tpp"

wf_cube_background_cubemap::wf_cube_background_cubemap()
{
    create_program();
    reload_texture();
}

wf_cube_background_cubemap::~wf_cube_background_cubemap()
{
    OpenGL::render_begin();
    program.free_resources();
    GL_CALL(glDeleteTextures(1, &tex));
    GL_CALL(glDeleteBuffers(1, &vbo_cube_vertices));
    GL_CALL(glDeleteBuffers(1, &ibo_cube_indices));
    OpenGL::render_end();
}

void wf_cube_background_cubemap::create_program()
{
    OpenGL::render_begin();
    program.set_simple(
        OpenGL::compile_program(cubemap_vertex, cubemap_fragment));
    OpenGL::render_end();
}

void wf_cube_background_cubemap::reload_texture()
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
        GL_CALL(glGenBuffers(1, &vbo_cube_vertices));
        GL_CALL(glGenBuffers(1, &ibo_cube_indices));
    }

    GL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, tex));
    if (!image_io::load_from_file(last_background_image, GL_TEXTURE_CUBE_MAP))
    {
        LOGE("Failed to load cubemap background image from \"%s\".",
            last_background_image.c_str());

        GL_CALL(glDeleteTextures(1, &tex));
        GL_CALL(glDeleteBuffers(1, &vbo_cube_vertices));
        GL_CALL(glDeleteBuffers(1, &ibo_cube_indices));
        tex = -1;
    }

    if (tex != (uint32_t)-1)
    {
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
            GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
            GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R,
            GL_CLAMP_TO_EDGE));
    }

    GL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
    OpenGL::render_end();
}

void wf_cube_background_cubemap::render_frame(const wf::render_target_t& fb,
    wf_cube_animation_attribs& attribs)
{
    reload_texture();

    OpenGL::render_begin(fb);
    if (tex == (uint32_t)-1)
    {
        GL_CALL(glClearColor(TEX_ERROR_FLAG_COLOR));
        GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
        OpenGL::render_end();

        return;
    }

    program.use(wf::TEXTURE_TYPE_RGBA);
    GL_CALL(glDepthMask(GL_FALSE));

    GL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, tex));

    GLfloat cube_vertices[] = {
        -1.0, 1.0, 1.0,
        -1.0, -1.0, 1.0,
        1.0, -1.0, 1.0,
        1.0, 1.0, 1.0,
        -1.0, 1.0, -1.0,
        -1.0, -1.0, -1.0,
        1.0, -1.0, -1.0,
        1.0, 1.0, -1.0,
    };

    GLushort cube_indices[] = {
        3, 7, 6, // right
        3, 6, 2, // right
        4, 0, 1, // left
        4, 1, 5, // left
        4, 7, 3, // top
        4, 3, 0, // top
        1, 2, 6, // bottom
        1, 6, 5, // bottom
        0, 3, 2, // front
        0, 2, 1, // front
        7, 4, 5, // back
        7, 5, 6, // back
    };

    glBindBuffer(GL_ARRAY_BUFFER, vbo_cube_vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_vertices), cube_vertices,
        GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_cube_indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_indices), cube_indices,
        GL_STATIC_DRAW);

    GLint vertex = glGetAttribLocation(program.get_program_id(
        wf::TEXTURE_TYPE_RGBA), "position");
    glEnableVertexAttribArray(vertex);
    glVertexAttribPointer(vertex, 3, GL_FLOAT, GL_FALSE, 0, 0);

    auto model = glm::rotate(glm::mat4(1.0),
        float(attribs.cube_animation.rotation),
        glm::vec3(0, 1, 0));

    glm::vec3 look_at{0.,
        (double)-attribs.cube_animation.offset_y,
        (double)attribs.cube_animation.offset_z};

    auto view = glm::lookAt(glm::vec3(0., 0., 0.), look_at, glm::vec3(0., 1., 0.));
    auto vp   = fb.transform * attribs.projection * view;

    model = vp * model;
    program.uniformMatrix4f("cubeMapMatrix", model);

    glDrawElements(GL_TRIANGLES, 12 * 3, GL_UNSIGNED_SHORT, 0);

    program.deactivate();
    GL_CALL(glDepthMask(GL_TRUE));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    OpenGL::render_end();
}

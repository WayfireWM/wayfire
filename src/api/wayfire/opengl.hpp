#ifndef WF_OPENGL_HPP
#define WF_OPENGL_HPP

#include "wayfire/render.hpp"
#include <GLES3/gl3.h>

#include <wayfire/config/types.hpp>
#include <wayfire/util.hpp>
#include <wayfire/nonstd/wlroots.hpp>

#include <wayfire/geometry.hpp>

#define GLM_FORCE_RADIANS
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

void gl_call(const char*, uint32_t, const char*);

#ifndef __STRING
    #  define __STRING(x) #x
#endif

/*
 * recommended to use this to make OpenGL calls, since it offers easier debugging
 * This macro is taken from WLC source code
 */
#define GL_CALL(x) x;gl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

struct gl_geometry
{
    float x1, y1, x2, y2;
};

namespace wf
{
// Extra functions for plugins dealing with render targets with OpenGL ES rendering.
namespace gles
{
GLuint ensure_render_buffer_fb_id(const render_buffer_t& buffer);
void bind_render_buffer(const render_buffer_t& buffer);
/* Set the GL scissor to the given box, after inverting it to match GL coordinate
 * space */
void scissor_render_buffer(const render_buffer_t& buffer, wlr_box box);

/* Returns a matrix which contains an orthographic projection from "geometry"
 * coordinates to the framebuffer coordinates. */
glm::mat4 render_target_orthographic_projection(const render_target_t& target);

/* Returns a matrix which contains an orthographic projection from OpenGL [-1, 1]
 * coordinates coordinates to the framebuffer coordinates (includes rotation,
 * subbuffer, etc). */
glm::mat4 render_target_gl_to_framebuffer(const render_target_t& target);

glm::mat4 output_transform(const render_target_t& target);

/**
 * Set the scissor region to the given box.
 *
 * In contrast to framebuffer_t::scissor(), this method takes its argument
 * as a box with "logical" coordinates, not raw framebuffer coordinates.
 *
 * @param box The scissor box, in the same coordinate system as the
 *   framebuffer's geometry.
 */
void render_target_logic_scissor(const render_target_t& target, wlr_box box);

/**
 * Ensure that the default EGL/GLES context is current.
 */
bool ensure_context(bool fail_on_error = false);

/**
 * Run code in the default EGL/GLES context, if we are running with GLES rendering.
 */
template<class F>
bool maybe_run_in_context(F&& code, bool fail_on_error = false)
{
    if (ensure_context(fail_on_error))
    {
        code();
        return true;
    }

    return false;
}

/**
 * Run code in the default EGL/GLES context, print an error and exit otherwise.
 */
template<class F>
bool run_in_context(F&& code)
{
    return maybe_run_in_context(code, true);
}
}

/** Represents the different types(formats) of textures in Wayfire. */
enum texture_type_t
{
    /* Regular OpenGL texture with 4 channels */
    TEXTURE_TYPE_RGBA     = 0,
    /* Regular OpenGL texture with 4 channels, but alpha channel should be
     * discarded. */
    TEXTURE_TYPE_RGBX     = 1,
    /** An EGLImage, it has been shared via dmabuf */
    TEXTURE_TYPE_EXTERNAL = 2,
    /* Invalid */
    TEXTURE_TYPE_ALL      = 3,
};

struct gles_texture_t
{
    /* Texture type */
    texture_type_t type = TEXTURE_TYPE_RGBA;
    /* Texture target */
    GLenum target = GL_TEXTURE_2D;
    /* Actual texture ID */
    GLuint tex_id;

    /** Invert Y? */
    bool invert_y = false;
    /** Has viewport? */
    bool has_viewport = false;

    /**
     * Part of the texture which is used for rendering.
     * Valid only if has_viewport is true.
     */
    gl_geometry viewport_box;

    /* tex_id will be initialized later */
    gles_texture_t();
    /** Initialize a non-inverted RGBA texture with the given texture id */
    gles_texture_t(GLuint tex);
    /** Initialize a texture with the attributes of the wlr texture */
    explicit gles_texture_t(wlr_texture*, std::optional<wlr_fbox> viewport = {});

    static gles_texture_t from_aux(auxilliary_buffer_t& buffer, std::optional<wlr_fbox> viewport = {});
};
}

namespace OpenGL
{
/* Clear the currently bound framebuffer with the given color */
void clear(wf::color_t color, uint32_t mask = GL_COLOR_BUFFER_BIT);


enum rendering_flags_t
{
    /* Invert the texture's X axis when sampling */
    TEXTURE_TRANSFORM_INVERT_X = (1 << 0),
    /* Invert the texture's Y axis when sampling */
    TEXTURE_TRANSFORM_INVERT_Y = (1 << 1),
    /* Use a subrectangle of the texture to render */
    TEXTURE_USE_TEX_GEOMETRY   = (1 << 2),
    /*
     * Enable an optimized, "cached" mode.
     *
     * The user first calls a render_texture variant with this bit set.
     * The default GL program will be called, uniforms uploaded, etc.
     * After that, draw_cached() may be used for each damaged rectangle.
     * In the end, clear_cache() is called.
     *
     * This allows re-use of uniform values for different damage rectangles.
     */
    RENDER_FLAG_CACHED         = (1 << 3),
};

/**
 * Render a textured quad using the built-in shaders.
 *
 * @param texture   The texture to render.
 * @param g         The initial coordinates of the quad.
 * @param texg      A rectangle containing the subtexture of @texture to render.
 *                    To enable rendering a subtexture, use
 *                    TEXTURE_USE_TEX_GEOMETRY. Texture coordinates are in the
 *                    usual coordinate system [0,1]x[0,1]. x1/y1 describe the
 *                    lower-left corner, and x2/y2 - the upper-right corner.
 * @param transform The matrix transformation to apply to the quad.
 * @param color     A color multiplier for each channel of the texture.
 * @param bits      A bitwise OR of texture_rendering_flags_t.
 */
void render_transformed_texture(wf::gles_texture_t texture,
    const gl_geometry& g,
    const gl_geometry& texg,
    glm::mat4 transform = glm::mat4(1.0),
    glm::vec4 color     = glm::vec4(1.f),
    uint32_t bits = 0);

/**
 * Render a textured quad using the built-in shaders.
 *
 * @param texture   The texture to render.
 * @param geometry  The initial coordinates of the quad.
 * @param transform The matrix transformation to apply to the quad.
 * @param color     A color multiplier for each channel of the texture.
 * @param bits      A bitwise OR of texture_rendering_flags_t. In this variant,
 *                    TEX_GEOMETRY flag is ignored.
 */
void render_transformed_texture(wf::gles_texture_t texture,
    const wf::geometry_t& geometry,
    glm::mat4 transform = glm::mat4(1.0),
    glm::vec4 color     = glm::vec4(1.f),
    uint32_t bits = 0);

/**
 * Render a textured quad on the given framebuffer.
 *
 * @param texture   The texture to render.
 * @param fb        The framebuffer to render onto.
 *                  It should have been already bound.
 * @param geometry  The geometry of the quad to render, in the same coordinate
 *                    system as the framebuffer geometry.
 * @param color     A color multiplier for each channel of the texture.
 * @param bits      A bitwise OR of texture_rendering_flags_t. In this variant,
 *                    TEX_GEOMETRY flag is ignored.
 */
void render_texture(wf::gles_texture_t texture,
    const wf::render_target_t& framebuffer,
    const wf::geometry_t& geometry,
    glm::vec4 color = glm::vec4(1.f),
    uint32_t bits   = 0);

/**
 * Render the textured rectangle again.
 *
 * See RENDER_FLAG_CACHED for detailed explanation.
 */
void draw_cached();

/**
 * Clear the cached state.
 *
 * See RENDER_FLAG_CACHED for detailed explanation.
 */
void clear_cached();

/* Compiles the given shader source */
GLuint compile_shader(std::string source, GLuint type);

/**
 * Create an OpenGL program from the given shader sources.
 *
 * @param vertex_source The source code of the vertex shader.
 * @param frag_source The source code of the fragment shader.
 */
GLuint compile_program(std::string vertex_source, std::string frag_source);

/**
 * Render a colored rectangle using OpenGL.
 *
 * @param box The rectangle geometry.
 * @param color The color of the rectangle.
 * @param matrix The matrix to transform the rectangle with.
 */
void render_rectangle(wf::geometry_t box, wf::color_t color, glm::mat4 matrix);

/**
 * An OpenGL program for rendering texture_t.
 * It contains multiple programs for the different texture types.
 *
 * All of the program_t's functions should only be used inside a rendering
 * block (see render_pass_t::custom_gles_subpass or wf::gles::(maybe_)run_in_context)
 */
class program_t
{
  public:
    program_t();

    /* Does nothing */
    ~program_t();

    program_t(const program_t &) = delete;
    program_t(program_t &&) = default;
    program_t& operator =(const program_t&) = delete;
    program_t& operator =(program_t&&) = default;

    /**
     * Compile the program consisting of @vertex_source and @fragment_source.
     *
     * Fragment source should contain two special symbols`@builtin@` and
     * `@builtin_ext@`.They will be replaced by the definitions needed for each
     * texture type, and will also provide a function `get_pixel(vec2)` to get
     * the texture pixel at the given position. `@builtin_ext@` has to be put
     * directly after the OpenGL version declaration, but there are no
     * restrictions about where to place `@builtin@`.
     *
     * The following identifiers should not be defined in the user source:
     *   _wayfire_texture, _wayfire_uv_scale, _wayfire_y_base, get_pixel
     */
    void compile(const std::string& vertex_source,
        const std::string& fragment_source);

    /**
     * Create a simple program
     * It will support only the given type.
     */
    void set_simple(GLuint program_id,
        wf::texture_type_t type = wf::TEXTURE_TYPE_RGBA);

    /** Deletes the underlying OpenGL programs */
    void free_resources();

    /**
     * Call glUseProgram with the appropriate program for the given texture type.
     * Raises a runtime exception if the type is not supported by the
     * view_program_t .
     */
    void use(wf::texture_type_t type);

    /** @return The program ID for the given texture type, or 0 on failure */
    int get_program_id(wf::texture_type_t type);

    /** Set the given uniform for the currently used program. */
    void uniform1i(const std::string& name, int value);
    /** Set the given uniform for the currently used program. */
    void uniform1f(const std::string& name, float value);
    /** Set the given uniform for the currently used program. */
    void uniform2f(const std::string& name, float x, float y);
    /** Set the given uniform for the currently used program. */
    void uniform3f(const std::string& name, float x, float y, float z);
    /** Set the given uniform for the currently used program. */
    void uniform4f(const std::string& name, const glm::vec4& value);
    /** Set the given uniform for the currently used program. */
    void uniformMatrix4f(const std::string& name, const glm::mat4& value);

    /*
     * Set the attribute pointer and active the attribute.
     *
     * @param attrib The name of the attrib array.
     * @param size, stride, ptr, type The same as the corresponding arguments of
     *   glVertexAttribPointer()
     */
    void attrib_pointer(const std::string& attrib,
        int size, int stride, const void *ptr, GLenum type = GL_FLOAT);

    /*
     * Set the attrib divisor. Analogous to glVertexAttribDivisor().
     *
     * @param attrib The name of the attribute.
     * @param divisor The divisor value.
     */
    void attrib_divisor(const std::string& attrib, int divisor);

    /**
     * Set the active texture, and modify the builtin Y-inversion uniforms.
     * Will not work with custom programs.
     */
    void set_active_texture(const wf::gles_texture_t& texture);

    /**
     * Deactivate the vertex attributes activated by attrib_pointer and
     * attrib_divisor, and reset the active OpenGL program.
     */
    void deactivate();

  private:
    class impl;
    std::unique_ptr<impl> priv;
};
}

/* utils */
glm::mat4 get_output_matrix_from_transform(wl_output_transform transform);

#endif // WF_OPENGL_HPP

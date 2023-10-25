#ifndef WF_OPENGL_HPP
#define WF_OPENGL_HPP

#include "wayfire/region.hpp"
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
/**
 * A simple class for managing framebuffers.
 * It can handle allocation and deallocation (not done automatically) of
 * framebuffers and their backing textures.
 */
struct framebuffer_t
{
    GLuint tex = -1, fb = -1;
    int32_t viewport_width = 0, viewport_height = 0;

    /* The functions below assume they are called between
     * OpenGL::render_begin() and OpenGL::render_end() */

    /* will invalidate texture contents if width or height changes.
     * If tex and/or fb haven't been set, it creates them
     * Return true if texture was created/invalidated */
    bool allocate(int width, int height);

    /* Make the framebuffer current, and adjust viewport to its size */
    void bind() const;

    /* Set the GL scissor to the given box, after inverting it to match GL
     * coordinate space */
    void scissor(wlr_box box) const;

    /* Will destroy the texture and framebuffer
     * Warning: will destroy tex/fb even if they have been allocated outside of
     * allocate() */
    void release();

    /* Reset the framebuffer, WITHOUT freeing resources.
     * There is no need to call reset() after release() */
    void reset();
};

/**
 * A render target contains a framebuffer and information on how to map
 * coordinates from the logical coordinate space (output-local coordinates, etc.)
 * to framebuffer coordinates.
 *
 * A render target may or not cover the full framebuffer.
 */
struct render_target_t : public framebuffer_t
{
    // Describes the logical coordinates of the render area, in whatever
    // coordinate system the render target needs.
    wf::geometry_t geometry = {0, 0, 0, 0};

    wl_output_transform wl_transform = WL_OUTPUT_TRANSFORM_NORMAL;
    // The scale of a framebuffer is a hint at how bigger the actual framebuffer
    // is compared to the logical geometry. It is useful for plugins utilizing
    // auxiliary buffers in logical coordinates, so that they know they should
    // render with higher resolution and still get a crisp image on the screen.
    float scale = 1.0;

    // If set, the subbuffer indicates a subrectangle of the framebuffer which
    // is used instead of the full buffer. In that case, the logical @geometry
    // is mapped only to that subrectangle and not to the full framebuffer.
    // Note: (0,0) is top-left for subbuffer.
    std::optional<wf::geometry_t> subbuffer;

    /* Transform contains output rotation, and possibly
     * other framebuffer transformations, if has_nonstandard_transform is set */
    glm::mat4 transform = glm::mat4(1.0);

    /**
     * Get a render target which is the same as this, but whose geometry is
     * translated by @offset.
     */
    render_target_t translated(wf::point_t offset) const;

    /**
     * Get the geometry of the given box after projecting it onto the framebuffer.
     * In the values returned, (0,0) is top-left.
     *
     * The resulting geometry is affected by the framebuffer geometry, scale and
     * transform.
     */
    wlr_box framebuffer_box_from_geometry_box(wlr_box box) const;

    /**
     * Get the geometry of the given region after projecting it onto the framebuffer. This is the same as
     * iterating over the rects in the region and transforming them with framebuffer_box_from_geometry_box.
     */
    wf::region_t framebuffer_region_from_geometry_region(const wf::region_t& region) const;

    /* Returns a matrix which contains an orthographic projection from "geometry"
     * coordinates to the framebuffer coordinates. */
    glm::mat4 get_orthographic_projection() const;

    /* Returns a matrix which contains an orthographic projection from OpenGL [-1, 1] coordinates
     * coordinates to the framebuffer coordinates (includes rotation, subbuffer, etc). */
    glm::mat4 gl_to_framebuffer() const;

    /**
     * Set the scissor region to the given box.
     *
     * In contrast to framebuffer_t::scissor(), this method takes its argument
     * as a box with "logical" coordinates, not raw framebuffer coordinates.
     *
     * @param box The scissor box, in the same coordinate system as the
     *   framebuffer's geometry.
     */
    void logic_scissor(wlr_box box) const;
};
}

namespace wf
{
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

struct texture_t
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
    texture_t();
    /** Initialize a non-inverted RGBA texture with the given texture id */
    texture_t(GLuint tex);
    /** Initialize a texture with the attributes of the wlr texture */
    explicit texture_t(wlr_texture*, std::optional<wlr_fbox> viewport = {});
};
}

namespace OpenGL
{
/* "Begin" rendering to the given framebuffer and the given viewport.
 * All rendering operations should happen between render_begin and render_end,
 * because
 * that's the only time we're guaranteed we have a valid GLES context
 *
 * The other functions below assume they are called between render_begin()
 * and render_end() */
void render_begin(); // use if you just want to bind GL context but won't draw
void render_begin(const wf::framebuffer_t& fb);
void render_begin(int32_t viewport_width, int32_t viewport_height, uint32_t fb);

/* Call this to indicate an end of the rendering.
 * Resets bound framebuffer and scissor box.
 * render_end() must be called for each render_begin() */
void render_end();

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
void render_transformed_texture(wf::texture_t texture,
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
void render_transformed_texture(wf::texture_t texture,
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
void render_texture(wf::texture_t texture,
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
 * block guarded by render_begin/end()
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
    void set_active_texture(const wf::texture_t& texture);

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

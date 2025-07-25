#include <wayfire/util/log.hpp>
#include <map>
#include "opengl-priv.hpp"
#include "wayfire/dassert.hpp"
#include "wayfire/geometry.hpp"
#include "core-impl.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <set>
#include <glm/gtc/matrix_transform.hpp>
#include "shaders.tpp"

const char *gl_error_string(const GLenum err)
{
    switch (err)
    {
      case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";

      case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";

      case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";

      case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";

      case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
    }

    return "UNKNOWN GL ERROR";
}

static bool disable_gl_call = false;
void gl_call(const char *func, uint32_t line, const char *glfunc)
{
    GLenum err;
    if (disable_gl_call || ((err = glGetError()) == GL_NO_ERROR))
    {
        return;
    }

    LOGE("gles2: function ", glfunc, " in ", func, " line ", line, ": ",
        gl_error_string(err));

    if (OpenGL::exit_on_gles_error)
    {
        wf::print_trace(false);
        std::_Exit(-1);
    }
}

namespace OpenGL
{
/*
 * Different Context is kept for each output
 * Each of the following functions uses the currently bound context
 */
program_t program, color_program;
GLuint compile_shader(std::string source, GLuint type)
{
    GLuint shader = GL_CALL(glCreateShader(type));

    const char *c_src = source.c_str();
    GL_CALL(glShaderSource(shader, 1, &c_src, NULL));

    int s;
#define LENGTH 1024 * 128
    char b1[LENGTH];
    GL_CALL(glCompileShader(shader));
    GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &s));
    GL_CALL(glGetShaderInfoLog(shader, LENGTH, NULL, b1));

    if (s == GL_FALSE)
    {
        LOGE("Failed to load shader:\n", source,
            "\nCompiler output:\n", b1);

        return -1;
    }

    return shader;
}

/* Create a very simple gl program from the given shader sources */
GLuint compile_program(std::string vertex_source, std::string frag_source)
{
    auto vertex_shader   = compile_shader(vertex_source, GL_VERTEX_SHADER);
    auto fragment_shader = compile_shader(frag_source, GL_FRAGMENT_SHADER);
    auto result_program  = GL_CALL(glCreateProgram());
    GL_CALL(glAttachShader(result_program, vertex_shader));
    GL_CALL(glAttachShader(result_program, fragment_shader));
    GL_CALL(glLinkProgram(result_program));

    int s = GL_FALSE;
#define LENGTH 1024 * 128
    char log[LENGTH];
    GL_CALL(glGetProgramiv(result_program, GL_LINK_STATUS, &s));
    GL_CALL(glGetProgramInfoLog(result_program, LENGTH, NULL, log));

    if (s == GL_FALSE)
    {
        LOGE("Failed to link vertex shader:\n", vertex_source,
            "\nFragment shader:\n", frag_source,
            "\nLinker output:\n", log);

        GL_CALL(glDeleteProgram(result_program));
    }

    /* won't be really deleted until program is deleted as well */
    GL_CALL(glDeleteShader(vertex_shader));
    GL_CALL(glDeleteShader(fragment_shader));
    return (s == GL_FALSE) ? 0 : result_program;
}

void init()
{
    wf::gles::run_in_context_if_gles([&]
    {
        // enable_gl_synchronous_debug()
        program.compile(default_vertex_shader_source,
            default_fragment_shader_source);
        color_program.set_simple(compile_program(default_vertex_shader_source,
            color_rect_fragment_source));
    });
}

void fini()
{
    wf::gles::run_in_context_if_gles([&]
    {
        program.free_resources();
        color_program.free_resources();
    });
}

namespace
{
uint32_t current_output_fb = 0;
}

void bind_output(uint32_t fb)
{
    current_output_fb = fb;
}

void unbind_output()
{
    current_output_fb = 0;
}

bool exit_on_gles_error = false;

std::vector<GLfloat> vertexData;
std::vector<GLfloat> coordData;

void render_transformed_texture(wf::gles_texture_t tex,
    const gl_geometry& g, const gl_geometry& texg,
    glm::mat4 model, glm::vec4 color, uint32_t bits)
{
    // We don't expect any errors from us!
    disable_gl_call = true;

    program.use(tex.type);

    vertexData = {
        g.x1, g.y2,
        g.x2, g.y2,
        g.x2, g.y1,
        g.x1, g.y1,
    };

    gl_geometry final_texg = (bits & TEXTURE_USE_TEX_GEOMETRY) ?
        texg : gl_geometry{0.0f, 0.0f, 1.0f, 1.0f};

    if (bits & TEXTURE_TRANSFORM_INVERT_Y)
    {
        final_texg.y1 = 1.0 - final_texg.y1;
        final_texg.y2 = 1.0 - final_texg.y2;
    }

    if (bits & TEXTURE_TRANSFORM_INVERT_X)
    {
        final_texg.x1 = 1.0 - final_texg.x1;
        final_texg.x2 = 1.0 - final_texg.x2;
    }

    coordData = {
        final_texg.x1, final_texg.y1,
        final_texg.x2, final_texg.y1,
        final_texg.x2, final_texg.y2,
        final_texg.x1, final_texg.y2,
    };

    program.set_active_texture(tex);
    program.attrib_pointer("position", 2, 0, vertexData.data());
    program.attrib_pointer("uvPosition", 2, 0, coordData.data());
    program.uniformMatrix4f("MVP", model);
    program.uniform4f("color", color);

    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

    if (bits & RENDER_FLAG_CACHED)
    {
        return;
    }

    draw_cached();
    clear_cached();
}

void draw_cached()
{
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
}

void clear_cached()
{
    disable_gl_call = false;
    program.deactivate();
}

void render_transformed_texture(wf::gles_texture_t texture,
    const wf::geometry_t& geometry, glm::mat4 transform,
    glm::vec4 color, uint32_t bits)
{
    bits &= ~TEXTURE_USE_TEX_GEOMETRY;

    gl_geometry gg;
    gg.x1 = geometry.x;
    gg.y1 = geometry.y;
    gg.x2 = gg.x1 + geometry.width;
    gg.y2 = gg.y1 + geometry.height;
    render_transformed_texture(texture, gg, {}, transform, color, bits);
}

void render_texture(wf::gles_texture_t texture,
    const wf::render_target_t& framebuffer,
    const wf::geometry_t& geometry, glm::vec4 color, uint32_t bits)
{
    render_transformed_texture(texture, geometry,
        wf::gles::render_target_orthographic_projection(framebuffer), color, bits);
}

void render_rectangle(wf::geometry_t geometry, wf::color_t color,
    glm::mat4 matrix)
{
    color_program.use(wf::TEXTURE_TYPE_RGBA);
    float x = geometry.x, y = geometry.y,
        w = geometry.width, h = geometry.height;

    GLfloat vertexData[] = {
        x, y + h,
        x + w, y + h,
        x + w, y,
        x, y,
    };

    color_program.attrib_pointer("position", 2, 0, vertexData);
    color_program.uniformMatrix4f("MVP", matrix);
    color_program.uniform4f("color", {color.r, color.g, color.b, color.a});

    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

    color_program.deactivate();
}

void clear(wf::color_t col, uint32_t mask)
{
    GL_CALL(glClearColor(col.r, col.g, col.b, col.a));
    GL_CALL(glClear(mask));
}
}

static bool egl_make_current(struct wlr_egl *egl)
{
    if (!eglMakeCurrent(wlr_egl_get_display(egl), EGL_NO_SURFACE, EGL_NO_SURFACE,
        wlr_egl_get_context(egl)))
    {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    return true;
}

static bool egl_is_current(struct wlr_egl *egl)
{
    return eglGetCurrentContext() == wlr_egl_get_context(egl);
}

bool wf::gles::ensure_context(bool fail_on_error)
{
    bool is_gles2 = wf::get_core().is_gles2();
    if (fail_on_error && !is_gles2)
    {
        wf::dassert(false,
            "Wayfire not running with GLES renderer, no GL calls allowed!");
    }

    if (!is_gles2)
    {
        return false;
    }

    if (!egl_is_current(wf::get_core_impl().egl))
    {
        egl_make_current(wf::get_core_impl().egl);
    }

    return true;
}

[[maybe_unused]]
static std::string framebuffer_status_to_str(GLuint status)
{
    switch (status)
    {
      case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        return "incomplete attachment";

      case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        return "missing attachment";

      case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
        return "incomplete dimensions";

      case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
        return "incomplete multisample";

      default:
        return "unknown";
    }
}

GLuint wf::gles::ensure_render_buffer_fb_id(const render_buffer_t& buffer)
{
    return wlr_gles2_renderer_get_buffer_fbo(wf::get_core().renderer, buffer.get_buffer());
}

void wf::gles::bind_render_buffer(const wf::render_buffer_t& buffer)
{
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ensure_render_buffer_fb_id(buffer)));
    GL_CALL(glViewport(0, 0, buffer.get_size().width, buffer.get_size().height));
}

void wf::gles::scissor_render_buffer(const wf::render_buffer_t& buffer, wlr_box box)
{
    GL_CALL(glEnable(GL_SCISSOR_TEST));
    GL_CALL(glScissor(box.x, box.y, box.width, box.height));
}

glm::mat4 wf::gles::render_target_orthographic_projection(const wf::render_target_t& target)
{
    auto ortho = glm::ortho(1.0f * target.geometry.x,
        1.0f * target.geometry.x + 1.0f * target.geometry.width,
        1.0f * target.geometry.y + 1.0f * target.geometry.height,
        1.0f * target.geometry.y);

    return gles::render_target_gl_to_framebuffer(target) * ortho;
}

glm::mat4 wf::gles::render_target_gl_to_framebuffer(const wf::render_target_t& target)
{
    if (target.subbuffer)
    {
        auto sub = target.subbuffer.value();

        float scale_x = 1.0 * sub.width / target.get_size().width;
        float scale_y = 1.0 * sub.height / target.get_size().height;

        // Translation is calculated between the midpoint of the whole buffer
        // and the midpoint of the subbuffer, then scaled to NDC.
        float half_w = target.get_size().width / 2.0;
        float half_h = target.get_size().height / 2.0;

        float translate_x = ((sub.x + sub.width / 2.0) - half_w) / half_w;
        float translate_y = ((sub.y + sub.height / 2.0) - half_h) / half_h;

        glm::mat4 scale = glm::scale(glm::mat4(1.0),
            glm::vec3(scale_x, scale_y, 1.0));
        glm::mat4 translate = glm::translate(glm::mat4(1.0),
            glm::vec3(translate_x, translate_y, 0.0));

        return translate * scale * gles::output_transform(target);
    }

    return gles::output_transform(target);
}

glm::mat4 wf::gles::output_transform(const render_target_t& target)
{
    return get_output_matrix_from_transform(
        wlr_output_transform_compose(target.wl_transform, WL_OUTPUT_TRANSFORM_FLIPPED_180));
}

void wf::gles::render_target_logic_scissor(const wf::render_target_t& target, wlr_box box)
{
    wf::gles::scissor_render_buffer(target, target.framebuffer_box_from_geometry_box(box));
}

/* look up the actual values of wl_output_transform enum
 * All _flipped transforms have values (regular_transform + 4) */
glm::mat4 get_output_matrix_from_transform(wl_output_transform transform)
{
    glm::mat4 scale = glm::mat4(1.0);

    if (transform >= 4)
    {
        scale = glm::scale(scale, {-1, 1, 1});
    }

    /* remove the third bit if it's set */
    uint32_t rotation = transform & (~4);
    glm::mat4 rotation_matrix(1.0);

    if (rotation == WL_OUTPUT_TRANSFORM_90)
    {
        rotation_matrix =
            glm::rotate(rotation_matrix, glm::radians(90.0f), {0, 0, 1});
    }

    if (rotation == WL_OUTPUT_TRANSFORM_180)
    {
        rotation_matrix = glm::rotate(rotation_matrix, glm::radians(
            180.0f), {0, 0, 1});
    }

    if (rotation == WL_OUTPUT_TRANSFORM_270)
    {
        rotation_matrix = glm::rotate(rotation_matrix, glm::radians(
            270.0f), {0, 0, 1});
    }

    return rotation_matrix * scale;
}

namespace wf
{
wf::gles_texture_t::gles_texture_t()
{}
wf::gles_texture_t::gles_texture_t(GLuint tex)
{
    this->tex_id = tex;
}

wf::gles_texture_t::gles_texture_t(wf::texture_t tex) : wf::gles_texture_t(tex.texture, tex.source_box)
{}

wf::gles_texture_t::gles_texture_t(wlr_texture *texture, std::optional<wlr_fbox> viewport)
{
    wf::dassert(wlr_texture_is_gles2(texture));
    wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(texture, &attribs);

    /* Wayfire works in inverted Y while wlroots doesn't, so we do invert here */
    this->invert_y = true;
    this->target   = attribs.target;
    this->tex_id   = attribs.tex;

    if (this->target == GL_TEXTURE_2D)
    {
        this->type = attribs.has_alpha ?
            wf::TEXTURE_TYPE_RGBA : wf::TEXTURE_TYPE_RGBX;
    } else
    {
        this->type = wf::TEXTURE_TYPE_EXTERNAL;
    }

    if (viewport)
    {
        this->has_viewport = true;

        auto width  = texture->width;
        auto height = texture->height;
        viewport_box.x1 = viewport->x / width;
        viewport_box.x2 = (viewport->x + viewport->width) / width;
        viewport_box.y1 = 1.0 - (viewport->y + viewport->height) / height;
        viewport_box.y2 = 1.0 - (viewport->y) / height;
    }
}

gles_texture_t gles_texture_t::from_aux(auxilliary_buffer_t& buffer, std::optional<wlr_fbox> viewport)
{
    wf::gles_texture_t tex{buffer.get_texture(), viewport};
    gles::run_in_context([&]
    {
        GL_CALL(glBindTexture(tex.target, tex.tex_id));
        GL_CALL(glTexParameteri(tex.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(tex.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glBindTexture(tex.target, 0));
    });

    return tex;
}
} // namespace wf

namespace OpenGL
{
class program_t::impl
{
  public:
    std::set<int> active_attrs;
    std::set<int> active_attrs_divisors;

    int active_program_idx = 0;

    int id[wf::TEXTURE_TYPE_ALL];
    std::unordered_map<std::string, int> uniforms[wf::TEXTURE_TYPE_ALL];

    /** Find the uniform location for the currently bound program */
    int find_uniform_loc(const std::string& name)
    {
        auto it = uniforms[active_program_idx].find(name);
        if (it != uniforms[active_program_idx].end())
        {
            return it->second;
        }

        uniforms[active_program_idx][name] =
            GL_CALL(glGetUniformLocation(id[active_program_idx], name.c_str()));

        if (uniforms[active_program_idx][name] == -1)
        {
            LOGE("Uniform ", name, " not found in program");
        }

        return uniforms[active_program_idx][name];
    }

    std::map<std::string, int> attribs[wf::TEXTURE_TYPE_ALL];
    /** Find the attrib location for the currently bound program */
    int find_attrib_loc(const std::string& name)
    {
        auto it = attribs[active_program_idx].find(name);
        if (it != attribs[active_program_idx].end())
        {
            return it->second;
        }

        attribs[active_program_idx][name] =
            GL_CALL(glGetAttribLocation(id[active_program_idx], name.c_str()));

        return attribs[active_program_idx][name];
    }
};

program_t::program_t()
{
    this->priv = std::make_unique<impl>();
    for (int i = 0; i < wf::TEXTURE_TYPE_ALL; i++)
    {
        this->priv->id[i] = 0;
    }
}

void program_t::set_simple(GLuint program_id, wf::texture_type_t type)
{
    free_resources();
    wf::dassert(type < wf::TEXTURE_TYPE_ALL);
    this->priv->id[type] = program_id;
}

program_t::~program_t()
{}

static std::string replace_builtin_with(const std::string& source,
    const std::string& builtin, const std::string& with)
{
    size_t pos = source.find(builtin);
    if (pos == std::string::npos)
    {
        return source;
    }

    return source.substr(0, pos) + with + source.substr(pos + builtin.length());
}

static const std::string builtin     = "@builtin@";
static const std::string builtin_ext = "@builtin_ext@";
struct texture_type_builtins
{
    std::string builtin;
    std::string builtin_ext;
};

std::map<wf::texture_type_t, texture_type_builtins> builtins = {
    {wf::TEXTURE_TYPE_RGBA, {builtin_rgba_source, ""}},
    {wf::TEXTURE_TYPE_RGBX, {builtin_rgbx_source, ""}},
    {wf::TEXTURE_TYPE_EXTERNAL, {builtin_external_source,
            builtin_ext_external_source}},
};

void program_t::compile(const std::string& vertex_source,
    const std::string& fragment_source)
{
    free_resources();

    for (const auto& program_type : builtins)
    {
        auto fragment = replace_builtin_with(fragment_source,
            builtin, program_type.second.builtin);
        fragment = replace_builtin_with(fragment,
            builtin_ext, program_type.second.builtin_ext);
        this->priv->id[program_type.first] =
            compile_program(vertex_source, fragment);
    }
}

void program_t::free_resources()
{
    for (int i = 0; i < wf::TEXTURE_TYPE_ALL; i++)
    {
        if (this->priv->id[i])
        {
            GL_CALL(glDeleteProgram(priv->id[i]));
            this->priv->id[i] = 0;
        }

        priv->uniforms[i].clear();
        priv->attribs[i].clear();
    }
}

void program_t::use(wf::texture_type_t type)
{
    if (priv->id[type] == 0)
    {
        throw std::runtime_error("program_t has no program for type " +
            std::to_string(type));
    }

    GL_CALL(glUseProgram(priv->id[type]));
    priv->active_program_idx = type;
}

int program_t::get_program_id(wf::texture_type_t type)
{
    return priv->id[type];
}

void program_t::uniform1i(const std::string& name, int value)
{
    int loc = priv->find_uniform_loc(name);
    GL_CALL(glUniform1i(loc, value));
}

void program_t::uniform1f(const std::string& name, float value)
{
    int loc = priv->find_uniform_loc(name);
    GL_CALL(glUniform1f(loc, value));
}

void program_t::uniform2f(const std::string& name, float x, float y)
{
    int loc = priv->find_uniform_loc(name);
    GL_CALL(glUniform2f(loc, x, y));
}

void program_t::uniform3f(const std::string& name, float x, float y, float z)
{
    int loc = priv->find_uniform_loc(name);
    GL_CALL(glUniform3f(loc, x, y, z));
}

void program_t::uniform4f(const std::string& name, const glm::vec4& value)
{
    int loc = priv->find_uniform_loc(name);
    GL_CALL(glUniform4f(loc, value.r, value.g, value.b, value.a));
}

void program_t::uniformMatrix4f(const std::string& name, const glm::mat4& value)
{
    int loc = priv->find_uniform_loc(name);
    GL_CALL(glUniformMatrix4fv(loc, 1, GL_FALSE, &value[0][0]));
}

void program_t::attrib_pointer(const std::string& attrib,
    int size, int stride, const void *ptr, GLenum type)
{
    int loc = priv->find_attrib_loc(attrib);
    priv->active_attrs.insert(loc);

    GL_CALL(glEnableVertexAttribArray(loc));
    GL_CALL(glVertexAttribPointer(loc, size, type, GL_FALSE, stride, ptr));
}

void program_t::attrib_divisor(const std::string& attrib, int divisor)
{
    int loc = priv->find_attrib_loc(attrib);
    priv->active_attrs_divisors.insert(loc);
    GL_CALL(glVertexAttribDivisor(loc, divisor));
}

void program_t::set_active_texture(const wf::gles_texture_t& texture)
{
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(texture.target, texture.tex_id));
    GL_CALL(glTexParameteri(texture.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

    glm::vec2 base{0.0f, 0.0f};
    glm::vec2 scale{1.0f, 1.0f};

    if (texture.has_viewport)
    {
        scale.x = texture.viewport_box.x2 - texture.viewport_box.x1;
        scale.y = texture.viewport_box.y2 - texture.viewport_box.y1;
        base.x  = texture.viewport_box.x1;
        base.y  = texture.viewport_box.y1;
    }

    if (texture.invert_y)
    {
        scale.y *= -1;
        base.y   = 1.0 - base.y;
    }

    uniform2f("_wayfire_uv_base", base.x, base.y);
    uniform2f("_wayfire_uv_scale", scale.x, scale.y);
}

void program_t::deactivate()
{
    for (int loc : priv->active_attrs_divisors)
    {
        GL_CALL(glVertexAttribDivisor(loc, 0));
    }

    for (int loc : priv->active_attrs)
    {
        GL_CALL(glDisableVertexAttribArray(loc));
    }

    priv->active_attrs_divisors.clear();
    priv->active_attrs.clear();
    GL_CALL(glUseProgram(0));
}
}

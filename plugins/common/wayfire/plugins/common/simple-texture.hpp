#pragma once
#include <wayfire/opengl.hpp>

namespace wf
{
struct simple_texture_t
{
    GLuint tex = -1;
    int width  = 0;
    int height = 0;

    /**
     * Destroy the GL texture.
     * This will call OpenGL::render_begin()/end() internally.
     */
    void release()
    {
        if (this->tex == (GLuint) - 1)
        {
            return;
        }

        OpenGL::render_begin();
        GL_CALL(glDeleteTextures(1, &tex));
        OpenGL::render_end();
        this->tex = -1;
    }

    simple_texture_t() = default;

    /** Auto-release the texture when the object is destroyed */
    ~simple_texture_t()
    {
        release();
    }

    simple_texture_t(const simple_texture_t &) = delete;
    simple_texture_t& operator =(const simple_texture_t&) = delete;

    simple_texture_t(simple_texture_t && o) noexcept : tex(o.tex), width(o.width),
        height(o.height)
    {
        o.tex = (GLuint) - 1;
    }

    simple_texture_t& operator =(simple_texture_t&& o) noexcept
    {
        if (&o == this)
        {
            return *this;
        }

        release();

        tex    = o.tex;
        width  = o.width;
        height = o.height;
        o.tex  = (GLuint) - 1;

        return *this;
    }
};
}

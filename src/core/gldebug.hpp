#include <GLES3/gl32.h>
#include <wayfire/debug.hpp>

const char *getStrSrc(GLenum src)
{
    if (src == GL_DEBUG_SOURCE_API)
    {
        return "API";
    }

    if (src == GL_DEBUG_SOURCE_WINDOW_SYSTEM)
    {
        return "WINDOW_SYSTEM";
    }

    if (src == GL_DEBUG_SOURCE_SHADER_COMPILER)
    {
        return "SHADER_COMPILER";
    }

    if (src == GL_DEBUG_SOURCE_THIRD_PARTY)
    {
        return "THIRD_PARTYB";
    }

    if (src == GL_DEBUG_SOURCE_APPLICATION)
    {
        return "APPLICATIONB";
    }

    if (src == GL_DEBUG_SOURCE_OTHER)
    {
        return "OTHER";
    } else
    {
        return "UNKNOWN";
    }
}

const char *getStrType(GLenum type)
{
    if (type == GL_DEBUG_TYPE_ERROR)
    {
        return "ERROR";
    }

    if (type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR)
    {
        return "DEPRECATED_BEHAVIOR";
    }

    if (type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)
    {
        return "UNDEFINED_BEHAVIOR";
    }

    if (type == GL_DEBUG_TYPE_PORTABILITY)
    {
        return "PORTABILITY";
    }

    if (type == GL_DEBUG_TYPE_PERFORMANCE)
    {
        return "PERFORMANCE";
    }

    if (type == GL_DEBUG_TYPE_OTHER)
    {
        return "OTHER";
    }

    return "UNKNOWN";
}

const char *getStrSeverity(GLenum severity)
{
    if (severity == GL_DEBUG_SEVERITY_HIGH)
    {
        return "HIGH";
    }

    if (severity == GL_DEBUG_SEVERITY_MEDIUM)
    {
        return "MEDIUM";
    }

    if (severity == GL_DEBUG_SEVERITY_LOW)
    {
        return "LOW";
    }

    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return "NOTIFICATION";
    }

    return "UNKNOWN";
}

void errorHandler(GLenum src, GLenum type, GLuint id, GLenum severity,
    GLsizei len, const GLchar *msg, const void *dummy)
{
    // ignore notifications
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return;
    }

    LOGI(
        "_______________________________________________\n",
        "Source: ", getStrSrc(src), "\n",
        "Type: ", getStrType(type), "\n",
        "Severity: ", getStrSeverity(severity), "\n",
        "Msg: ", msg, "\n",
        "_______________________________________________\n");
}

void enable_gl_synchronous_debug()
{
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(errorHandler, 0);
}

#include "drm_fourcc.h"
#include "png.h"
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/opengl.hpp>
#include <random>
#include <vector>
#include <wayfire/core.hpp> // For wf::get_core()
#include <wayfire/util/log.hpp>
#include <chrono> // For time measurement
#include <glm/gtc/matrix_transform.hpp> // For glm::ortho
#include <glm/gtc/type_ptr.hpp> // For glm::value_ptr

#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

void texture_to_png(const char *name, uint8_t *pixels, int w, int h, bool invert)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
        nullptr, nullptr);
    if (!png)
    {
        return;
    }

    png_infop infot = png_create_info_struct(png);
    if (!infot)
    {
        png_destroy_write_struct(&png, &infot);

        return;
    }

    FILE *fp = fopen(name, "wb");
    if (!fp)
    {
        png_destroy_write_struct(&png, &infot);

        return;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, infot, w, h, 8 /* depth */, PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_colorp palette =
        (png_colorp)png_malloc(png, PNG_MAX_PALETTE_LENGTH * sizeof(png_color));
    if (!palette)
    {
        fclose(fp);
        png_destroy_write_struct(&png, &infot);

        return;
    }

    png_set_PLTE(png, infot, palette, PNG_MAX_PALETTE_LENGTH);
    png_write_info(png, infot);
    png_set_packing(png);

    png_bytepp rows = (png_bytepp)png_malloc(png, h * sizeof(png_bytep));
    for (int i = 0; i < h; ++i)
    {
        if (invert)
        {
            rows[i] = (png_bytep)(pixels + (h - i - 1) * w * 4);
        } else
        {
            rows[i] = (png_bytep)(pixels + i * w * 4);
        }
    }

    png_write_image(png, rows);
    png_write_end(png, infot);
    png_free(png, palette);
    png_destroy_write_struct(&png, &infot);

    fclose(fp);
    png_free(png, rows);
}

class wayfire_test_plugin : public wf::per_output_plugin_instance_t
{
    static constexpr int buffer_size = 512;

    // Custom EGL/GL context members
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    int drm_render_fd = -1;
    struct gbm_device *gbm_device = nullptr;

    // EGL extension function pointers
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = NULL;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = NULL;
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES = NULL;

    // OpenGL program and locations
    GLuint program_id  = 0;
    GLint position_loc = -1;
    // Helper function to compile a shader
    GLuint compile_shader(GLenum type, const char *source)
    {
        GLuint shader = GL_CALL(glCreateShader(type));
        GL_CALL(glShaderSource(shader, 1, &source, NULL));
        GL_CALL(glCompileShader(shader));

        GLint success;
        GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &success));
        if (!success)
        {
            GLchar info_log[512];
            GL_CALL(glGetShaderInfoLog(shader, 512, NULL, info_log));
            LOGE("Shader compilation failed: %s", info_log);
            GL_CALL(glDeleteShader(shader));
            return 0;
        }

        return shader;
    }

    // Helper function to link a program
    GLuint link_program(GLuint vertex_shader, GLuint fragment_shader)
    {
        GLuint program = GL_CALL(glCreateProgram());
        GL_CALL(glAttachShader(program, vertex_shader));
        GL_CALL(glAttachShader(program, fragment_shader));
        GL_CALL(glLinkProgram(program));

        GLint success;
        GL_CALL(glGetProgramiv(program, GL_LINK_STATUS, &success));
        if (!success)
        {
            GLchar info_log[512];
            GL_CALL(glGetProgramInfoLog(program, 512, NULL, info_log));
            LOGE("Shader program linking failed: %s", info_log);
            GL_CALL(glDeleteProgram(program));
            return 0;
        }

        return program;
    }

    // Helper function to create an OpenGL program
    GLuint create_opengl_program()
    {
        const char *vertex_shader_source =
            R"(
            #version 100
            attribute vec2 position;
            varying vec2 v_texcoord;
            void main()
            {
                gl_Position = vec4(position, 0.0, 1.0);
                v_texcoord = (position + 1.0) / 2.0;
            }
        )";

        const char *fragment_shader_source =
            R"(
            #version 100
            precision mediump float;
            varying vec2 v_texcoord;
            uniform sampler2D tex;
            void main()
            {
                gl_FragColor = texture2D(tex, v_texcoord);
            }
        )";

        GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
        if (vertex_shader == 0)
        {
            return 0;
        }

        GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
        if (fragment_shader == 0)
        {
            GL_CALL(glDeleteShader(vertex_shader));
            return 0;
        }

        GLuint program = link_program(vertex_shader, fragment_shader);

        GL_CALL(glDeleteShader(vertex_shader));
        GL_CALL(glDeleteShader(fragment_shader));

        return program;
    }

  public:
    void init() override
    {
        // Open DRM render node
        drm_render_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (drm_render_fd < 0)
        {
            LOGE("Failed to open DRM render node /dev/dri/renderD128: %s", strerror(errno));
            // Handle error, maybe shutdown or disable the plugin
            return;
        }

        // Create GBM device
        gbm_device = gbm_create_device(drm_render_fd);
        if (!gbm_device)
        {
            LOGE("Failed to create GBM device: %s", strerror(errno));
            close(drm_render_fd);
            drm_render_fd = -1;
            // Handle error
            return;
        }

        // Initialize EGL
        egl_display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm_device, NULL);
        if (egl_display == EGL_NO_DISPLAY)
        {
            LOGE("Failed to get EGL display: %d", eglGetError());
            gbm_device_destroy(gbm_device);
            gbm_device = nullptr;
            close(drm_render_fd);
            drm_render_fd = -1;
            // Handle error
            return;
        }

        EGLint major, minor;
        if (!eglInitialize(egl_display, &major, &minor))
        {
            LOGE("Failed to initialize EGL: %d", eglGetError());
            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
            gbm_device_destroy(gbm_device);
            gbm_device = nullptr;
            close(drm_render_fd);
            drm_render_fd = -1;
            // Handle error
            return;
        }

        // Bind the API
        eglBindAPI(EGL_OPENGL_ES_API);

        // Choose EGL config
        EGLint config_attribs[] = {
            EGL_SURFACE_TYPE, EGL_DONT_CARE,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };

        EGLConfig config;
        EGLint num_configs;
        if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_configs) || (num_configs == 0))
        {
            LOGE("Failed to choose EGL config: %d", eglGetError());
            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
            gbm_device_destroy(gbm_device);
            gbm_device = nullptr;
            close(drm_render_fd);
            drm_render_fd = -1;
            // Handle error
            return;
        }

        // Create EGL context
        EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };

        egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
        if (egl_context == EGL_NO_CONTEXT)
        {
            LOGE("Failed to create EGL context: %d", eglGetError());
            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
            gbm_device_destroy(gbm_device);
            gbm_device = nullptr;
            close(drm_render_fd);
            drm_render_fd = -1;
            // Handle error
            return;
        }

        // Make the context current
        if (!eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context))
        {
            LOGE("Failed to make EGL context current: %d", eglGetError());
            eglDestroyContext(egl_display, egl_context);
            egl_context = EGL_NO_CONTEXT;
            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
            gbm_device_destroy(gbm_device);
            gbm_device = nullptr;
            close(drm_render_fd);
            drm_render_fd = -1;
            // Handle error
            return;
        }

        program_id = create_opengl_program();
        if (program_id == 0)
        {
            LOGE("Failed to create OpenGL program!");
        }

        // Get uniform and attribute locations
        position_loc = GL_CALL(glGetAttribLocation(program_id, "position"));

        for (int i = 0; i < 2; i++)
        {
            benchmark_render_to_buffer();
            benchmark_render_to_buffer_opengl();
        }

        wf::get_core().shutdown();
    }

    void fini() override
    {}

    GLuint generate_random_texture(int width, int height)
    {
        std::vector<uint32_t> pixels(width * height);

        std::mt19937 rng(42);
        std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

        for (int i = 0; i < width * height; ++i)
        {
            pixels[i] = dist(rng);
        }

        GLuint texture_id;
        GL_CALL(glGenTextures(1, &texture_id));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture_id));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
            pixels.data()));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        return texture_id;
    }

    void run_benchmark(GLuint fb_id, std::string name)
    {
        LOGI("Starting ", name, " render buffer benchmark...");

        GLuint random_texture = generate_random_texture(buffer_size, buffer_size);
        if (!random_texture)
        {
            LOGE("Failed to generate random texture!");
            return;
        }

        auto tex = wf::gles_texture_t{random_texture};
        auto start_time = std::chrono::high_resolution_clock::now();

        // Vertex data for a fullscreen quad in NDC
        float positions[] = {
            -1.0f, -1.0f, // bottom-left
            1.0f, -1.0f, // bottom-right
            -1.0f, 1.0f, // top-left

            1.0f, -1.0f, // bottom-right
            1.0f, 1.0f, // top-right
            -1.0f, 1.0f // top-left
        };

        for (int i = 0; i < 100000; ++i)
        {
            GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb_id));
            GL_CALL(glViewport(0, 0, buffer_size, buffer_size));

            // Use the custom OpenGL program
            GL_CALL(glUseProgram(program_id));

            // Set uniform values
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, tex.tex_id));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

            // Set attribute pointers
            GL_CALL(glEnableVertexAttribArray(position_loc));
            GL_CALL(glVertexAttribPointer(position_loc, 2, GL_FLOAT, GL_FALSE, 0, positions));

            // Draw the quad
            GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));

            // Unbind
            GL_CALL(glDisableVertexAttribArray(position_loc));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glUseProgram(0));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0)); // Unbind the FBO
        }

        GL_CALL(glFinish());

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        std::vector<char> buffer(buffer_size * buffer_size * 4);
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_id));
        GL_CALL(glReadPixels(0, 0, buffer_size, buffer_size, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data()));
        std::string fname = "/tmp/" + name + ".png";
        texture_to_png(fname.c_str(), (uint8_t*)buffer.data(), buffer_size, buffer_size, false);

        LOGI("Render ", name, " render buffer benchmark finished in ", elapsed.count(), " seconds.");
    }

    void benchmark_render_to_buffer()
    {
        uint32_t format = DRM_FORMAT_ABGR8888;
        unsigned long mods[] = {
            I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC,
        };

        struct gbm_bo *bo = gbm_bo_create_with_modifiers2(gbm_device, buffer_size, buffer_size,
            format, mods, sizeof(mods) / sizeof(mods[0]), GBM_BO_USE_RENDERING);
        if (bo == NULL)
        {
            LOGE("Failed to create GBM BO with modifiers!");
            gbm_device_destroy(gbm_device);
            return;
        }

        // Export GBM BO to dmabuf attributes
        int width  = gbm_bo_get_width(bo);
        int height = gbm_bo_get_height(bo);
        uint64_t modifier = gbm_bo_get_modifier(bo);
        int n_planes = gbm_bo_get_plane_count(bo);
        int fd[4];
        int offset[4];
        int stride[4];

        for (int i = 0; i < n_planes; ++i)
        {
            fd[i] = gbm_bo_get_fd_for_plane(bo, i);
            if (fd[i] < 0)
            {
                LOGE("gbm_bo_get_fd_for_plane failed");
                return;
            }

            offset[i] = gbm_bo_get_offset(bo, i);
            stride[i] = gbm_bo_get_stride_for_plane(bo, i);
        }

        // Create EGLImage from dmabuf attributes
        std::vector<EGLint> attribs_list;
        attribs_list.push_back(EGL_WIDTH);
        attribs_list.push_back(width);
        attribs_list.push_back(EGL_HEIGHT);
        attribs_list.push_back(height);
        attribs_list.push_back(EGL_LINUX_DRM_FOURCC_EXT);
        attribs_list.push_back(format);

        EGLint plane_fd_attribs[] = {
            EGL_DMA_BUF_PLANE0_FD_EXT,
            EGL_DMA_BUF_PLANE1_FD_EXT,
            EGL_DMA_BUF_PLANE2_FD_EXT,
            EGL_DMA_BUF_PLANE3_FD_EXT,
        };
        EGLint plane_offset_attribs[] = {
            EGL_DMA_BUF_PLANE0_OFFSET_EXT,
            EGL_DMA_BUF_PLANE1_OFFSET_EXT,
            EGL_DMA_BUF_PLANE2_OFFSET_EXT,
            EGL_DMA_BUF_PLANE3_OFFSET_EXT,
        };
        EGLint plane_pitch_attribs[] = {
            EGL_DMA_BUF_PLANE0_PITCH_EXT,
            EGL_DMA_BUF_PLANE1_PITCH_EXT,
            EGL_DMA_BUF_PLANE2_PITCH_EXT,
            EGL_DMA_BUF_PLANE3_PITCH_EXT,
        };
        EGLint plane_modifier_lo_attribs[] = {
            EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
            EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT,
        };
        EGLint plane_modifier_hi_attribs[] = {
            EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
            EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
            EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
            EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT,
        };

        for (int j = 0; j < n_planes; ++j)
        {
            attribs_list.push_back(plane_fd_attribs[j]);
            attribs_list.push_back(fd[j]);
            attribs_list.push_back(plane_offset_attribs[j]);
            attribs_list.push_back(offset[j]);
            attribs_list.push_back(plane_pitch_attribs[j]);
            attribs_list.push_back(stride[j]);
            if (modifier != DRM_FORMAT_MOD_INVALID)
            {
                attribs_list.push_back(plane_modifier_lo_attribs[j]);
                attribs_list.push_back(modifier & 0xFFFFFFFF);
                attribs_list.push_back(plane_modifier_hi_attribs[j]);
                attribs_list.push_back(modifier >> 32);
            }
        }

        attribs_list.push_back(EGL_NONE);

        // Load EGL extension functions if not already loaded
        if (!eglCreateImageKHR)
        {
            eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
            glEGLImageTargetTexture2DOES =
                (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        }

        if (!eglCreateImageKHR || !glEGLImageTargetTexture2DOES)
        {
            LOGE("Failed to load EGL extension functions!");
            return;
        }

        EGLImage image = eglCreateImageKHR(egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL,
            (const EGLint*)attribs_list.data());
        LOGI("GOt image ", image);
        if (image == EGL_NO_IMAGE_KHR)
        {
            LOGE("Failed to create EGLImage from dmabuf: %d", eglGetError());
            return;
        }

        // Create OpenGL render buffer from EGLImage
        GLuint renderbuffer = 0;
        // Load EGL extension function if not already loaded
        if (!glEGLImageTargetRenderbufferStorageOES)
        {
            glEGLImageTargetRenderbufferStorageOES =
                (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)eglGetProcAddress(
                    "glEGLImageTargetRenderbufferStorageOES");
        }

        if (!glEGLImageTargetRenderbufferStorageOES)
        {
            LOGE("Failed to load glEGLImageTargetRenderbufferStorageOES extension function!");
            return;
        }

        GL_CALL(glGenRenderbuffers(1, &renderbuffer));
        GL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer));
        GL_CALL(glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, image));
        GL_CALL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

        // Create OpenGL FBO and attach render buffer
        GLuint fb_id = 0;
        GL_CALL(glGenFramebuffers(1, &fb_id));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fb_id));
        GL_CALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
            renderbuffer));
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LOGE("OpenGL framebuffer is not complete!");
            return;
        }

        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        run_benchmark(fb_id, "aux");
    }

    void benchmark_render_to_buffer_opengl()
    {
        GLuint fbo     = 0;
        GLuint texture = 0;

        GL_CALL(glGenTextures(1, &texture));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer_size, buffer_size, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, NULL));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glGenFramebuffers(1, &fbo));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0));
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LOGE("OpenGL framebuffer is not complete!");
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
            GL_CALL(glDeleteFramebuffers(1, &fbo));
            GL_CALL(glDeleteTextures(1, &texture));
            return;
        }

        run_benchmark(fbo, "opengl");
    }
};

// Declare the plugin
DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_test_plugin>);

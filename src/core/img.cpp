#include <GLES2/gl2.h>
#include <wayfire/util/log.hpp>
#include "wayfire/img.hpp"
#include "wayfire/opengl.hpp"

#include <config.h>

#ifdef BUILD_WITH_IMAGEIO
    #include <png.h>
    #include <jpeglib.h>
    #include <jerror.h>
#endif

#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <cstdio>
#include <unordered_map>
#include <functional>

#define TEXTURE_LOAD_ERROR 0

namespace image_io
{
using Loader = std::function<bool (const char*, GLuint)>;
using Writer = std::function<void (const char*name, uint8_t*pixels, unsigned long,
    unsigned long, bool)>;
namespace
{
std::unordered_map<std::string, Loader> loaders;
std::unordered_map<std::string, Writer> writers;
}

bool load_data_as_cubemap(unsigned char *data, int width, int height, int channels)
{
    width  /= 4;
    height /= 3;
    int x, y, t;

    if (width != height)
    {
        LOGE("cubemap width / 4(", width, ") != height / 3(", height, ")");
        return false;
    }

    /*
     *  CUBEMAP IMAGE FORMAT
     *
     *    0    1    2    3
     *    _____________________
     *  0 | X  | T  | X  | X  |
     *    |____|____|____|____|
     *  1 | R  | F  | L  | BA |
     *    |____|____|____|____|
     *  2 | X  | BO | X  | X  |
     *    |____|____|____|____|
     *
     *  WIDTH / 4 == HEIGHT / 3
     *
     *  X : UNUSED
     *  T:  TOP
     *  R:  RIGHT
     *  F:  FRONT
     *  L:  LEFT
     *  BA: BACK
     *  BO: BOTTOM
     *
     */

    for (t = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
         t < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6;
         t++)
    {
        switch (t)
        {
          case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            x = 2, y = 1;
            break;

          case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            x = 0, y = 1;
            break;

          case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            x = 1, y = 0;
            break;

          case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            x = 1, y = 2;
            break;

          case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            x = 1, y = 1;
            break;

          case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            x = 3, y = 1;
            break;

          default:
            return false;
            break;
        }

        auto format = (channels == 4 ? GL_RGBA : GL_RGB);
        GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, width * 4));
        GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS, y * height));
        GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS, x * width));

        GL_CALL(glTexImage2D(t, 0, format, width, height, 0,
            format, GL_UNSIGNED_BYTE, data));
    }

    GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS, 0));
    GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0));

    return true;
}

#ifdef BUILD_WITH_IMAGEIO
/* All backend functions are taken from the internet.
 * If you want to be credited, contact me */
bool texture_from_png(const char *filename, GLuint target)
{
    FILE *fp = fopen(filename, "rb");
    int width, height;
    png_byte color_type;
    png_byte bit_depth;
    png_bytep *row_pointers;

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png)
    {
        fclose(fp);
        return false;
    }

    png_infop infos = png_create_info_struct(png);
    if (!infos)
    {
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png)))
    {
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, infos);

    width  = png_get_image_width(png, infos);
    height = png_get_image_height(png, infos);
    color_type = png_get_color_type(png, infos);
    bit_depth  = png_get_bit_depth(png, infos);

    if (bit_depth == 16)
    {
        png_set_strip_16(png);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(png);
    }

    // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
    if ((color_type == PNG_COLOR_TYPE_GRAY) && (bit_depth < 8))
    {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    if (png_get_valid(png, infos, PNG_INFO_tRNS))
    {
        png_set_tRNS_to_alpha(png);
    }

    // These color_type don't have an alpha channel then fill it with 0xff.
    if ((color_type == PNG_COLOR_TYPE_RGB) ||
        (color_type == PNG_COLOR_TYPE_GRAY) ||
        (color_type == PNG_COLOR_TYPE_PALETTE))
    {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    if ((color_type == PNG_COLOR_TYPE_GRAY) ||
        (color_type == PNG_COLOR_TYPE_GRAY_ALPHA))
    {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, infos);

    row_pointers = new png_bytep[height];
    png_byte *data = new png_byte[height * png_get_rowbytes(png, infos)];

    for (int i = 0; i < height; i++)
    {
        row_pointers[i] = data + i * png_get_rowbytes(png, infos);
    }

    png_read_image(png, row_pointers);

    if (target == GL_TEXTURE_CUBE_MAP)
    {
        if (!load_data_as_cubemap(data, width, height,
            png_get_channels(png, infos)))
        {
            png_destroy_read_struct(&png, &infos, NULL);
            delete[] row_pointers;
            delete[] data;
            fclose(fp);
            return false;
        }
    } else if (target == GL_TEXTURE_2D)
    {
        GL_CALL(glTexImage2D(target, 0, GL_RGBA, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)data));
    }

    png_destroy_read_struct(&png, &infos, NULL);
    delete[] row_pointers;
    delete[] data;

    fclose(fp);

    return true;
}

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

bool texture_from_jpeg(const char *FileName, GLuint target)
{
    unsigned long data_size;
    unsigned char *rowptr[1];
    unsigned char *jdata;
    struct jpeg_decompress_struct infot;
    struct jpeg_error_mgr err;

    std::FILE *file = fopen(FileName, "rb");
    infot.err = jpeg_std_error(&err);
    jpeg_create_decompress(&infot);

    if (!file)
    {
        LOGE("failed to read JPEG file ", FileName);

        return false;
    }

    jpeg_stdio_src(&infot, file);
    jpeg_read_header(&infot, TRUE);
    jpeg_start_decompress(&infot);

    data_size = infot.output_width * infot.output_height * 3;

    jdata = new unsigned char [data_size];
    while (infot.output_scanline < infot.output_height)
    {
        rowptr[0] = (unsigned char*)jdata + 3 * infot.output_width *
            infot.output_scanline;
        jpeg_read_scanlines(&infot, rowptr, 1);
    }

    jpeg_finish_decompress(&infot);

    GLint width  = infot.output_width;
    GLint height = infot.output_height;

    if (target == GL_TEXTURE_CUBE_MAP)
    {
        if (!load_data_as_cubemap(jdata, width, height, 3))
        {
            fclose(file);
            delete[] jdata;
            return false;
        }
    } else if (target == GL_TEXTURE_2D)
    {
        GL_CALL(glTexImage2D(target, 0, GL_RGB, width, height, 0,
            GL_RGB, GL_UNSIGNED_BYTE, jdata));
    }

    fclose(file);
    delete[] jdata;

    return true;
}

#endif

bool load_from_file(std::string name, GLuint target)
{
    if (access(name.c_str(), F_OK) == -1)
    {
        if (!name.empty())
        {
            LOGE(__func__, "() cannot access ", name);
        }

        return false;
    }

    int len = name.length();
    if ((len < 4) || (name[len - 4] != '.'))
    {
        LOGE(
            "load_from_file() called with file without extension or with invalid extension!");

        return false;
    }

    auto ext = name.substr(len - 3, 3);
    for (int i = 0; i < 3; i++)
    {
        ext[i] = std::tolower(ext[i]);
    }

    auto it = loaders.find(ext);
    if (it == loaders.end())
    {
        LOGE("load_from_file() called with unsupported extension ", ext);

        return false;
    } else
    {
        return it->second(name.c_str(), target);
    }
}

void write_to_file(std::string name, uint8_t *pixels, int w, int h, std::string type,
    bool invert)
{
    auto it = writers.find(type);

    if (it == writers.end())
    {
        LOGE("unsupported image_writer backend");
    } else
    {
        it->second(name.c_str(), pixels, w, h, invert);
    }
}

void write_to_file(std::string name, wf::framebuffer_t fb)
{
    std::vector<char> buffer(fb.viewport_width * fb.viewport_height * 4);
    OpenGL::render_begin();
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb.fb));
    GL_CALL(glReadPixels(0, 0, fb.viewport_width, fb.viewport_height,
        GL_RGBA, GL_UNSIGNED_BYTE, buffer.data()));
    OpenGL::render_end();
    write_to_file(name, (uint8_t*)buffer.data(),
        fb.viewport_width, fb.viewport_height, "png", false);
}

void init()
{
    LOGD("init ImageIO");
#ifdef BUILD_WITH_IMAGEIO
    loaders["png"] = Loader(texture_from_png);
    loaders["jpg"] = Loader(texture_from_jpeg);
    writers["png"] = Writer(texture_to_png);
#endif
}
}

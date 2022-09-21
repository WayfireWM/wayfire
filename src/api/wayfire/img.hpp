#ifndef IMG_HPP_
#define IMG_HPP_

#include <wayfire/opengl.hpp>
#include <string>

namespace image_io
{
/* Load the image from the given file, binding it to the given GL texture target
 * Bind the texture before you call this function
 * Guaranteed: doesn't change any GL state except pixel packing */
bool load_from_file(std::string name, GLuint target);

/* Function that saves the given pixels(in rgba format) to a (currently) png file */
void write_to_file(std::string name, uint8_t *pixels, int w, int h,
    std::string type, bool invert = false);

void write_to_file(std::string name, wf::framebuffer_t buffer);

/* Initializes all backends, called at startup */
void init();
}

#endif /* end of include guard: IMG_HPP_ */

#version 450
#extension GL_ARB_shading_language_include : require

layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;

#include "color-transform.frag"

void main() {
    vec4 r = texture(tex, uv);
    out_color = vec4(transform_color(r.rgb), r.a);
}

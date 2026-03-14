#version 450
#extension GL_ARB_shading_language_include : require

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 0, binding = 1, column_major) uniform UVT {
    mat3 uv_transform;
} uvt;

layout(location = 0) in vec2 uv;

#include "color-transform.frag"


void main() {
    vec4 r = texture(tex, (uvt.uv_transform * vec3(uv, 1.0)).xy);
    out_color = vec4(transform_color(r.rgb), r.a);
}

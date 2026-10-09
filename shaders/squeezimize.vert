#version 450
#extension GL_ARB_shading_language_include : require

layout(push_constant, column_major) uniform UBO {
	mat4 matrix;
    vec4 src_box;
    vec4 target_box;
    float progress;
    int upward;
} data;

#include "texture-transform.vert"

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv_in;

layout(location = 0) out vec2 uv;

void main() {
    uv = uv_in;
    gl_Position = data.matrix * vec4(position, 0.0, 1.0);
}

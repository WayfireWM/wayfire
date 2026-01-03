#version 450
#extension GL_ARB_shading_language_include : require
#include "texture-transform.vert"

layout(push_constant, column_major) uniform UBO {
	mat4 mvp;
    vec2 tex_scale;
    vec2 tex_offset;
} data;

layout(location = 0) out vec2 uv;

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv_in;

void main() {
	gl_Position = data.mvp * vec4(pos, 0.0, 1.0);
	uv = transform_texture_uv(uv_in, data.tex_scale, data.tex_offset);
}

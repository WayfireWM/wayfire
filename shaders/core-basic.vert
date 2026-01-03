#version 450
#extension GL_ARB_shading_language_include : require

layout(push_constant, column_major) uniform UBO {
	mat4 mvp;
    vec2 uv_scale;
    vec2 uv_offset;
} data;

#include "texture-transform.vert"

layout(location = 0) out vec2 uv;

vec2 positions[4] = vec2[](
    vec2(0.0, 0.0), // top left
    vec2(0.0, 1.0), // top right
    vec2(1.0, 1.0), // bottom right
    vec2(1.0, 0.0)  // bottom left
);

void main() {
    vec2 pos = positions[gl_VertexIndex];
	uv = transform_texture_uv(pos, data.uv_scale, data.uv_offset);
	gl_Position = data.mvp * vec4(pos, 0.0, 1.0);
}

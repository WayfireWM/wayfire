#version 450

layout(push_constant, column_major) uniform UBO {
	mat4 mvp;
} data;

layout(location = 0) out vec2 uv;

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv_in;

void main() {
	gl_Position = data.mvp * vec4(pos, 0.0, 1.0);
	uv = uv_in;
}

#version 450

layout(push_constant, column_major) uniform UBO {
	mat4 mvp;
} data;

layout(location = 0) out vec2 uv;

vec2 positions[4] = vec2[](
    vec2(0.0, 0.0), // top left
    vec2(0.0, 1.0), // top right
    vec2(1.0, 1.0), // bottom right
    vec2(1.0, 0.0)  // bottom left
);

void main() {
    vec2 pos = positions[gl_VertexIndex];
	uv = pos;
	gl_Position = data.mvp * vec4(pos, 0.0, 1.0);
}

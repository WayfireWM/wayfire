#version 450

layout(push_constant, column_major) uniform UBO {
    mat4 matrix;
    float smoothing;
} data;

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 center;
layout(location = 2) in float radius;
layout(location = 3) in vec4 color;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) out float R;

void main() {
    uv = position * radius;
    gl_Position = data.matrix * vec4(center.x + uv.x * 0.75, center.y + uv.y, 0.0, 1.0);

    R = radius;
    out_color = color;
}

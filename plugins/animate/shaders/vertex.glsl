#version 310 es

layout(location = 0) in highp vec2 position;
layout(location = 1) in highp vec2 center;
layout(location = 2) in highp vec4 color;
layout(location = 3) uniform highp vec2 global_offset;

out highp vec4 out_color;
out highp vec2 pos;

void main() {
    gl_Position = vec4 (position + center + global_offset, 0.0, 1.0);
    out_color = color;
    pos = position;
}

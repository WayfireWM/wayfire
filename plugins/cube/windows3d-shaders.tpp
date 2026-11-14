#pragma once
/* 3D windows: the window texture with its alpha
 * (premultiplied, blended with GL_ONE, 1 - src alpha), shaded like the
 * face it stands on (colorFactor, see the light option). */
static const char* cube_window_fragment =
R"(#version 100
varying highp vec2 uvpos;
uniform sampler2D smp;
uniform highp float colorFactor;

void main() {
    highp vec4 c = texture2D(smp, uvpos);
    gl_FragColor = vec4(c.rgb * colorFactor, c.a);
})";

/* Window thickness edges: a flat colour (premultiplied), shaded like the
 * face the window stands on. */
static const char* cube_edge_vertex =
R"(#version 100
attribute highp vec3 position;
uniform mat4 VP;
uniform mat4 model;

void main() {
    gl_Position = VP * model * vec4(position, 1.0);
})";

static const char* cube_edge_fragment =
R"(#version 100
uniform highp vec4 color;
uniform highp float colorFactor;

void main() {
    gl_FragColor = vec4(color.rgb * color.a * colorFactor, color.a);
})";

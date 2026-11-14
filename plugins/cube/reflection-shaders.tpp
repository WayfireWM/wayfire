#pragma once

/* Reflection: like cube_vertex_2_0, plus the model-space
 * height for the fade; premultiplied output for GL_ONE, 1 - src alpha. */
static const char* cube_reflection_vertex =
R"(#version 100
attribute highp vec3 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;
varying highp float modelY;

uniform mat4 VP;
uniform mat4 model;

void main() {
    vec4 p = model * vec4(position, 1.0);
    gl_Position = VP * p;
    uvpos = uvPosition;
    modelY = p.y;
})";

static const char* cube_reflection_fragment =
R"(#version 100
varying highp vec2 uvpos;
varying highp float modelY;
uniform sampler2D smp;
uniform highp float intensity;
uniform highp float floorY;
uniform highp float fade;

void main() {
    highp float a = intensity * clamp(1.0 - (modelY - floorY) / fade, 0.0, 1.0);
    gl_FragColor = vec4(texture2D(smp, uvpos).rgb * a, a);
})";

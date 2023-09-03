#pragma once

static const char* cube_vertex_2_0 =
R"(#version 100
attribute mediump vec3 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;

uniform mat4 VP;
uniform mat4 model;

void main() {
    gl_Position = VP * model * vec4(position, 1.0);
    uvpos = uvPosition;
})";

static const char* cube_fragment_2_0 =
R"(#version 100
varying highp vec2 uvpos;
uniform sampler2D smp;

void main() {
    gl_FragColor = vec4(texture2D(smp, uvpos).xyz, 1);
})";

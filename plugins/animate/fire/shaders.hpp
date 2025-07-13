#ifndef PARTICLE_ANIMATION_SHADER
#define PARTICLE_ANIMATION_SHADER

static const char *particle_vert_source =
    R"(
#version 100

attribute highp float radius;
attribute highp vec2 position;
attribute highp vec2 center;
attribute highp vec4 color;

uniform mat4 matrix;

varying highp vec2 uv;
varying highp vec4 out_color;
varying highp float R;

void main() {
    uv = position * radius;
    gl_Position = matrix * vec4(center.x + uv.x * 0.75, center.y + uv.y, 0.0, 1.0);

    R = radius;
    out_color = color;
}
)";

static const char *particle_frag_source =
    R"(
#version 100

varying highp vec2 uv;
varying highp vec4 out_color;
varying highp float R;

uniform highp float smoothing;

void main()
{
    highp float len = length(uv);
    if (len >= R)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
    else {
        highp float factor = 1.0 - len / R;
        factor = pow(factor, smoothing);
        gl_FragColor = factor * out_color;
    }
}
)";

#endif /* end of include guard: PARTICLE_ANIMATION_SHADER */

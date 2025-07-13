#version 310 es

in highp vec4 out_color;
in highp vec2 pos;
out highp vec4 fragColor;

layout(location = 4) uniform highp float radii;

void main()
{
    highp float dist_center = sqrt(pos.x * pos.x + pos.y * pos.y);
    highp float factor = (radii - dist_center) / radii;
    if (factor < 0.0) factor = 0.0;
    highp float factor2 = factor * factor;

    fragColor = vec4(out_color.xyz, out_color.w * factor2);
}

#version 450

layout(location = 0) out vec4 frag_color;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 out_color;
layout(location = 2) in float R;

layout(push_constant, column_major) uniform UBO {
    mat4 matrix;
    float smoothing;
} data;

void main()
{
    float len = length(uv);
    if (len >= R)
    {
        frag_color = vec4(0.0, 0.0, 0.0, 0.0);
    }
    else
    {
        float factor = 1.0 - len / R;
        factor = pow(factor, data.smoothing);
        frag_color = factor * out_color;
    }
}

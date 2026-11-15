#version 450

layout(location = 0) out vec4 frag_color;
layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant, column_major) uniform UBO {
    mat4 matrix;
    vec4 src_box;
    vec4 target_box;
    float progress;
    int upward;
} data;

layout(location = 0) in vec2 uv;

void main()
{
    float y, sigmoid;
    vec2 uv_squeeze;
    float inv_w = 1.0 / (data.src_box.z - data.src_box.x);
    float inv_h = 1.0 / (data.src_box.w - data.src_box.y);
    float progress_pt_one = pow(clamp(data.progress, 0.0, 0.25) * 4.0, 2.0);
    float progress_pt_two = pow(data.progress, 2.0);

    uv_squeeze.x = inv_w * (uv.x - data.src_box.x);
    uv_squeeze.y = inv_h * (1.0 - uv.y - data.src_box.y);

    if (data.upward == 1)
    {
        y = 1.0 - uv.x;
        uv_squeeze.x += progress_pt_two * (inv_w - data.target_box.z);
        sigmoid = 1.0 / (1.0 + pow(2.718, -(y * (1.0 / (data.src_box.z - data.target_box.z)) * 15.0 - 10.0)));
    } else
    {
        y = uv.x;
        uv_squeeze.x -= progress_pt_two * (inv_w - data.target_box.x + data.target_box.z);
        sigmoid = 1.0 / (1.0 + pow(2.718, -(y * (1.0 / (data.target_box.z - data.src_box.x)) * 15.0 - 10.0)));
    }

    float t = sigmoid * progress_pt_one;
    float sy0 = mix(data.src_box.y, 1.0 - data.target_box.w, t);
    float sy1 = mix(data.src_box.w, 1.0 - data.target_box.y, t);
    uv_squeeze.y = (uv.y - sy1) / (sy0 - sy1);
    uv_squeeze.y = 1.0 - uv_squeeze.y;

    if (uv_squeeze.x < 0.0 || uv_squeeze.y < 0.0 ||
        uv_squeeze.x > 1.0 || uv_squeeze.y > 1.0)
    {
        discard;
    }

    frag_color = texture(tex, uv_squeeze);
}

static const char *default_vertex_shader_source =
R"(#version 100

attribute highp vec2 position;
attribute highp vec2 uvPosition;
varying highp vec2 uvpos;

uniform mat4 MVP;

void main() {
    gl_Position = MVP * vec4(position.xy, 0.0, 1.0);
    uvpos = uvPosition;
})";

static const char *default_fragment_shader_source =
R"(#version 100
@builtin_ext@
@builtin@

varying highp vec2 uvpos;
uniform highp vec4 color;

void main()
{
    highp vec4 tex_color = get_pixel(uvpos);
    tex_color.rgb = tex_color.rgb * color.a;
    gl_FragColor = tex_color * color;
})";

static const char *color_rect_fragment_source =
R"(#version 100
varying highp vec2 uvpos;
uniform highp vec4 color;

void main()
{
    gl_FragColor = color;
})";



static const char *builtin_rgba_source =
R"(
uniform sampler2D _wayfire_texture;
uniform highp vec2 _wayfire_uv_base;
uniform highp vec2 _wayfire_uv_scale;

highp vec4 get_pixel(highp vec2 uv) {
    uv = _wayfire_uv_base + _wayfire_uv_scale * uv;
    return texture2D(_wayfire_texture, uv);
}
)";

static const char *builtin_rgbx_source =
R"(
uniform sampler2D _wayfire_texture;
uniform highp vec2 _wayfire_uv_base;
uniform highp vec2 _wayfire_uv_scale;

highp vec4 get_pixel(highp vec2 uv) {
    uv = _wayfire_uv_base + _wayfire_uv_scale * uv;
    return vec4(texture2D(_wayfire_texture, uv).rgb, 1.0);
}
)";

static const char *builtin_external_source =
R"(
uniform samplerExternalOES _wayfire_texture;
uniform highp vec2 _wayfire_uv_base;
uniform highp vec2 _wayfire_uv_scale;

highp vec4 get_pixel(highp vec2 uv) {
    uv = _wayfire_uv_base + _wayfire_uv_scale * uv;
    return texture2D(_wayfire_texture, uv);
}
)";

static const char *builtin_ext_external_source =
R"(#extension GL_OES_EGL_image_external : require

)";

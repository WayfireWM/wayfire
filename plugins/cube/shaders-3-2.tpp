static const char *cube_vertex_3_2 =
R"(#version 320 es
in vec3 position;
in vec2 uvPosition;

out vec2 uvpos;
out vec3 vPos;

void main() {
    vPos = position;
    uvpos = uvPosition;
})";

static const char *cube_tcs_3_2 =
R"(#version 320 es
layout(vertices = 3) out;

in vec2 uvpos[];
in vec3 vPos[];

out vec3 tcPosition[];
out vec2 uv[];

#define ID gl_InvocationID

uniform int deform;
uniform int light;

void main() {
    tcPosition[ID] = vPos[ID];
    uv[ID] = uvpos[ID];

    if(ID == 0){
        /* deformation requires tessellation
           and lighting even higher degree to
           make lighting smoother */

        float tessLevel = 1.0f;
        if(deform > 0)
            tessLevel = 30.0f;
        if(light > 0)
            tessLevel = 50.0f;

        gl_TessLevelInner[0] = tessLevel;
        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
    }
})";

static const char *cube_tes_3_2 =
R"(#version 320 es
layout(triangles) in;

in vec3 tcPosition[];
in vec2 uv[];

out vec2 tesuv;
out vec3 tePosition;

uniform mat4 model;
uniform mat4 VP;
uniform int  deform;
uniform float ease;

vec2 interpolate2D(vec2 v0, vec2 v1, vec2 v2) {
    return vec2(gl_TessCoord.x) * v0
         + vec2(gl_TessCoord.y) * v1
         + vec2(gl_TessCoord.z) * v2;
}

vec3 interpolate3D(vec3 v0, vec3 v1, vec3 v2) {
    return vec3(gl_TessCoord.x) * v0
         + vec3(gl_TessCoord.y) * v1
         + vec3(gl_TessCoord.z) * v2;
}


vec3 tp;
void main() {
    tesuv = interpolate2D(uv[0], uv[1], uv[2]);

    tp = interpolate3D(tcPosition[0], tcPosition[1], tcPosition[2]);
    tp = (model * vec4(tp, 1.0)).xyz;

    if(deform > 0) {
        float r = 0.5;
        float d = distance(tp.xz, vec2(0, 0));
        float scale = 1.0;
        if(deform == 1)
            scale = r / d;
        else
            scale = d / r;

        scale = pow(scale, ease);
        tp = vec3(tp[0] * scale, tp[1], tp[2] * scale);
    }

    tePosition = tp;
    gl_Position = VP * vec4 (tp, 1.0);
})";

static const char* cube_geometry_3_2 =
R"(#version 320 es
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec2 tesuv[3];
in vec3 tePosition[3];

uniform int  light;

out vec2 guv;
out vec3 colorFactor;

#define AL 0.3    // ambient lighting
#define DL (1.0-AL) // diffuse lighting

void main() {

    const vec3 lightSource = vec3(0, 0, 2);
    const vec3 lightNormal = normalize(lightSource);

    if(light == 1) {
        vec3 A = tePosition[2] - tePosition[0];
        vec3 B = tePosition[1] - tePosition[0];
        vec3 N = normalize(cross(A, B));

        vec3 center = (tePosition[0] + tePosition[1] + tePosition[2]) / 3.0;

        float d = distance(center, lightSource);
        float ambient_coeff = pow(clamp(2.0 / d, 0.0, 1.0), 10.0);

        float value = clamp(pow(abs(dot(N, lightNormal)), 1.5), 0.0, 1.0);

        float df = AL * ambient_coeff + DL * value;
        colorFactor = vec3(df, df, df);
    }
    else
        colorFactor = vec3(1.0, 1.0, 1.0);

    gl_Position = gl_in[0].gl_Position;
    guv = tesuv[0];
    EmitVertex();

    gl_Position = gl_in[1].gl_Position;
    guv = tesuv[1];
    EmitVertex();

    gl_Position = gl_in[2].gl_Position;
    guv = tesuv[2];
    EmitVertex();
})";

static const char *cube_fragment_3_2 =
R"(#version 320 es

in highp vec2 guv;
in highp vec3 colorFactor;
layout(location = 0) out mediump vec4 outColor;

uniform sampler2D smp;

void main() {
    outColor = vec4(texture(smp, guv).xyz * colorFactor, 1.0);
})";

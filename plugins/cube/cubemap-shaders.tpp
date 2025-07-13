static const char* cubemap_vertex =
R"(#version 100

attribute highp vec3 position;
varying highp vec3 direction;

uniform mat4 cubeMapMatrix;

void main()
{
    gl_Position = cubeMapMatrix * vec4(position, 1.0);
    direction = position;
})";

static const char* cubemap_fragment =
R"(#version 100
varying highp vec3 direction;
uniform samplerCube smp;

void main()
{
    gl_FragColor = vec4(textureCube(smp, direction).xyz, 1);
})";

#version 330 core
layout (location = 0) in vec3 vPos;
layout (location = 1) in vec2 vUV;
uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;
out vec2 uv;
out float viewDepth;
void main()
{
    vec4 v = view * model * vec4(vPos, 1.0);
    viewDepth = -v.z;
    uv = vUV;
    gl_Position = proj * v;
}

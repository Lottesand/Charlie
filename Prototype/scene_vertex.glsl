#version 330 core
layout (location = 0) in vec3 vPos;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;
uniform mat4 lightVP;
uniform mat3 normalMat;

out vec3 wPos;
out vec3 wNormal;
out vec3 vNormalView;
out vec4 lsPos;
out float viewDepth;

void main()
{
    vec4 w = model * vec4(vPos, 1.0);
    wPos = w.xyz;
    wNormal = normalize(normalMat * vNormal);
    vNormalView = mat3(view) * wNormal;
    lsPos = lightVP * w;
    vec4 v = view * w;
    viewDepth = -v.z;
    gl_Position = proj * v;
}

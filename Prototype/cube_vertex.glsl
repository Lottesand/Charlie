#version 330 core
layout (location = 0) in vec3 vPos;
layout (location = 1) in vec3 vNormal;
uniform mat4 mvp;
uniform mat3 normalMat;
out vec3 vN;
void main()
{
    vN = normalMat * vNormal;
    gl_Position = mvp * vec4(vPos, 1.0);
}

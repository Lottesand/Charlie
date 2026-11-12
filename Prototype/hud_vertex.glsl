#version 330 core
layout (location = 0) in vec2 vPos;
layout (location = 1) in vec4 vColor;
uniform mat3 xform;
out vec4 outColor;
void main()
{
    vec3 p = xform * vec3(vPos, 1.0);
    gl_Position = vec4(p.xy, 0.0, 1.0);
    outColor = vColor;
}

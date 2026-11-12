#version 330 core
in vec2 uv;
in float viewDepth;
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;
uniform sampler2D tex;
uniform float farPlane;
void main()
{
    // 캡처한 화면은 이미 감마/톤매핑이 된 값이라 선형으로 되돌려서 넣는다
    vec3 c = pow(texture(tex, uv).rgb, vec3(2.2)) * 0.85;
    outColor = vec4(c, 1.0);
    outNormal = vec4(0.5, 0.5, 1.0, clamp(viewDepth / farPlane, 0.0, 0.998));
}

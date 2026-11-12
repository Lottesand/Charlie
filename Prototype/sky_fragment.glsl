#version 330 core
in vec2 uv;
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;

uniform mat4 invViewProj;
uniform vec3 camPos;
uniform vec3 sunDir;
uniform vec3 zenith;
uniform vec3 horizon;
uniform vec3 sunColor;

void main()
{
    vec4 p = invViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - camPos);
    float h = dir.y;

    vec3 col = mix(horizon, zenith, smoothstep(-0.02, 0.6, h));
    if (h < 0.0) col = mix(horizon, horizon * 0.55, 1.0 - smoothstep(-0.3, 0.0, h));

    float s = max(dot(dir, normalize(sunDir)), 0.0);
    col += sunColor * (pow(s, 900.0) * 30.0 + pow(s, 12.0) * 0.35);

    outColor = vec4(col, 1.0);
    outNormal = vec4(0.5, 0.5, 1.0, 1.0);
}

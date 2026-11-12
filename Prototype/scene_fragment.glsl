#version 330 core
in vec3 wPos;
in vec3 wNormal;
in vec3 vNormalView;
in vec4 lsPos;
in float viewDepth;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outNormal;   // 뷰 공간 법선 + 선형 깊이 (외곽선 검출용)

uniform vec3 objColor;
uniform float Ks;
uniform float shininess;
uniform float emissive;
uniform int pattern;                // 0 단색, 1 바닥 타일, 2 콘크리트 노이즈

uniform vec3 sunDir;                // 표면 → 태양 방향
uniform vec3 sunColor;
uniform vec3 skyAmb;
uniform vec3 groundAmb;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float farPlane;
uniform sampler2D shadowMap;

uniform vec3 pointPos;
uniform vec3 pointColor;
uniform float pointPower;
uniform float alpha;      // 1 불투명, 그 미만은 카메라를 가리는 반투명 오브젝트

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

// 텍스처 이미지 없이 월드 좌표로 표면 무늬 생성
vec3 surface(vec3 base, vec3 N)
{
    if (pattern == 1) {
        // 방의 어느 면이든 바닥이 될 수 있으므로 법선에 맞는 평면으로 타일을 깐다
        vec2 q = abs(N.y) > 0.5 ? wPos.xz : (abs(N.x) > 0.5 ? wPos.zy : wPos.xy);
        vec2 g = q / 2.0;
        vec2 f = abs(fract(g) - 0.5);
        float line = smoothstep(0.475, 0.495, max(f.x, f.y));
        float n = noise(q * 1.7) * 0.08 + noise(q * 7.0) * 0.04;
        float tile = hash(floor(g)) * 0.06;
        return base * (1.0 - line * 0.3 - n - tile);
    }
    if (pattern == 2) {
        vec2 p = abs(N.y) > 0.5 ? wPos.xz : (abs(N.x) > 0.5 ? wPos.zy : wPos.xy);
        float n = noise(p * 2.3) * 0.07 + noise(p * 9.0) * 0.035;
        return base * (1.0 - n);
    }
    return base;
}

// PCF 3x3 그림자
float shadowFactor(vec3 N, vec3 L)
{
    vec3 p = lsPos.xyz / lsPos.w * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = max(0.002 * (1.0 - dot(N, L)), 0.0005);
    vec2 ts = 1.0 / vec2(textureSize(shadowMap, 0));
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(shadowMap, p.xy + vec2(x, y) * ts).r;
            s += (p.z - bias > d) ? 0.0 : 1.0;
        }
    return s / 9.0;
}

void main()
{
    vec3 N = normalize(wNormal);
    vec3 L = normalize(sunDir);
    vec3 V = normalize(viewPos - wPos);
    vec3 base = surface(objColor, N);

    float sh = shadowFactor(N, L);
    float NdotL = max(dot(N, L), 0.0);
    vec3 amb = mix(groundAmb, skyAmb, N.y * 0.5 + 0.5);     // 반구 주변광
    vec3 R = reflect(-L, N);
    float spec = NdotL > 0.0 ? pow(max(dot(V, R), 0.0), shininess) * Ks : 0.0;

    vec3 col = base * (amb + sunColor * NdotL * sh) + sunColor * spec * sh;
    float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0) * Ks * 0.4;
    col += skyAmb * fres;

    if (pointPower > 0.0) {
        vec3 lv = pointPos - wPos;
        float d = length(lv);
        float a = pointPower / (1.0 + 0.5 * d + 0.3 * d * d);
        col += base * pointColor * max(dot(N, lv / d), 0.0) * a;
    }

    col += objColor * emissive;

    float fog = 1.0 - exp(-fogDensity * length(viewPos - wPos));
    col = mix(col, fogColor, fog);

    outColor = vec4(col, alpha);
    outNormal = vec4(normalize(vNormalView) * 0.5 + 0.5, clamp(viewDepth / farPlane, 0.0, 0.998));
}

#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform sampler2D sceneTex;
uniform sampler2D normalTex;
uniform sampler2D bloomTex;
uniform vec2 texel;
uniform float film;      // 0~1 무성영화 필름 느낌 (세피아 + 깜빡임 + 스크래치)
uniform float flash;     // 0~1 톱니 획득 등 화면 번쩍임
uniform float time;

vec4 S(vec2 o) { return texture(normalTex, uv + o * texel); }

vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main()
{
    vec3 col = texture(sceneTex, uv).rgb;

    // 외곽선: 법선은 Sobel, 깊이는 라플라시안(평면에서는 0) → 잉크 선
    vec4 c = S(vec2(0.0));
    vec4 gx = -S(vec2(-1, -1)) - 2.0 * S(vec2(-1, 0)) - S(vec2(-1, 1)) + S(vec2(1, -1)) + 2.0 * S(vec2(1, 0)) + S(vec2(1, 1));
    vec4 gy = -S(vec2(-1, -1)) - 2.0 * S(vec2(0, -1)) - S(vec2(1, -1)) + S(vec2(-1, 1)) + 2.0 * S(vec2(0, 1)) + S(vec2(1, 1));
    float lap = S(vec2(1, 0)).a + S(vec2(-1, 0)).a + S(vec2(0, 1)).a + S(vec2(0, -1)).a - 4.0 * c.a;
    float eN = length(vec2(length(gx.xyz), length(gy.xyz)));
    float eD = abs(lap) / max(c.a, 0.002);
    float edge = max(smoothstep(0.6, 1.2, eN), smoothstep(0.03, 0.08, eD));
    edge *= 1.0 - smoothstep(0.3, 0.7, c.a);
    col = mix(col, vec3(0.05, 0.04, 0.03), edge * 0.85);

    col += texture(bloomTex, uv).rgb * 0.9;
    col = aces(col * 1.1);

    // 필름: 채도 낮춤 + 세피아, 프레임 단위 밝기 깜빡임, 세로 스크래치, 먼지
    float frame = floor(time * 18.0);
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    vec3 sepia = vec3(lum) * vec3(1.08, 0.96, 0.78);
    col = mix(col, mix(col, sepia, 0.5), film);
    col = mix(col, smoothstep(0.0, 1.0, col), 0.35 * film);     // 대비를 살짝 올려 흑백 영화 같은 톤
    col *= 1.0 + (hash(vec2(frame, 3.7)) - 0.5) * 0.07 * film;
    float sx = hash(vec2(frame, 1.3));
    if (hash(vec2(frame, 9.1)) > 0.55) {
        float d = abs(uv.x - sx);
        col = mix(col, vec3(0.92, 0.88, 0.78), film * 0.35 * (1.0 - smoothstep(0.0, 0.0012, d)));
    }
    float dust = hash(floor(uv * vec2(320.0, 180.0)) + frame);
    if (dust > 0.9993) col = mix(col, vec3(0.05), film * 0.8);

    vec2 dir = uv - 0.5;
    float v = smoothstep(0.9, 0.25, length(dir * vec2(1.0, 1.15)));
    col *= mix(1.0, v, 0.35 + film * 0.3);
    col += vec3(1.0, 0.85, 0.4) * flash * 0.35;

    col += (hash(uv * 900.0 + time) - 0.5) * (0.025 + 0.04 * film);
    FragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}

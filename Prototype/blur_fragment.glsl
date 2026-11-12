#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D src;
uniform int horizontal;
uniform vec2 texel;

const float W[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec2 step = horizontal != 0 ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec3 c = texture(src, uv).rgb * W[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(src, uv + step * float(i) * 1.5).rgb * W[i];
        c += texture(src, uv - step * float(i) * 1.5).rgb * W[i];
    }
    FragColor = vec4(c, 1.0);
}

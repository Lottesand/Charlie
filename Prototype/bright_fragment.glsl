#version 330 core
in vec2 uv;
out vec4 FragColor;
uniform sampler2D src;
void main()
{
    vec3 c = texture(src, uv).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    FragColor = vec4(c * smoothstep(0.9, 1.8, l), 1.0);
}

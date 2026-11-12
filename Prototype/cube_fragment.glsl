#version 330 core
in vec3 vN;
out vec4 FragColor;
uniform vec3 color;
uniform float emissive;
uniform float alpha;
void main()
{
    vec3 L = normalize(vec3(0.4, 0.7, 0.6));
    float d = max(dot(normalize(vN), L), 0.0);
    FragColor = vec4(color * (0.38 + 0.72 * d + emissive), alpha);
}

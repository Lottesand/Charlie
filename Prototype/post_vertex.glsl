#version 330 core
// 정점 버퍼 없이 화면 전체를 덮는 삼각형 하나
out vec2 uv;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

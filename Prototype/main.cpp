// Prototype v2 - 씬 조립 플랫포머 (모던 타임즈 공장)
//
// 용어
//  블럭     : 1x1x1 칸 하나. 독립된 컨셉을 가진 작은 맵
//  단블럭   : 블럭 4개 이하가 붙은 프리셋
//  다단블럭 : 씬을 클리어해서 얻는 정육면체 블럭 (그 씬 전체가 통째로 들어간다)
//  씬 조립  : 빈 정육면체 그리드를 가진 블럭으로 채우기
//
// 규칙
//  - 씬 안쪽을 향하는 면은 벽이 없다(내면). 씬 바깥 테두리만 벽(외면)
//  - 다단블럭 껍데기가 씬 안쪽을 향하면 꼭짓점 나사 4개로 잠긴 패널이 된다 → 그 씬에서 얻은 스패너로 풀면 열린다
//    닫힌 패널은 발판/벽으로 쓸 수 있다
//  - 시작 칸(아래층 7번)과 끝 칸(위층 2번)은 고정. 나머지는 어둠이고, 어둠에 들어가면 사망 → 떨어지기 직전 발판으로 복귀
//  - 떠 있는 톱니를 모두 모으고 씬을 전부 채운 뒤 끝 칸의 퇴근 기적을 울리면 클리어 → 이 씬이 다단블럭이 된다
//  - 편집 모드는 언제든 (서 있는 블럭도 편집 가능). 돌아오면 씬 안에서 블럭들이 실제로 빠지고/움직이고/날아와 끼워진다
//
// 렌더링 파이프라인 (GT_Demo 에서 가져옴): 그림자 → HDR 씬(MRT) → 블룸 → 외곽선 + 톤매핑 + 필름 합성 → HUD

#include <GL/glew.h>
#include <GL/glfw3.h>
#include <gl/glm/glm.hpp>
#include <gl/glm/ext.hpp>
#include <gl/glm/gtc/matrix_transform.hpp>
#include <gl/glm/gtc/type_ptr.hpp>
#include <gl/glm/gtc/quaternion.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <functional>

const float PI = 3.1415926535f;
const float FAR_PLANE = 260.0f;
const int SHADOW_SIZE = 2048;

const float CS = 21.0f;             // 블럭 하나의 월드 크기 (캐릭터 키 1.75 의 12배)
const float HALF = CS * 0.5f;
const float SLAB = 0.4f;            // 블럭 바닥 두께 (높이 h 는 블럭 밑면에서 잰다)

const float PLAYER_R = 0.35f;
const float PLAYER_H = 1.75f;
const float STEP_H = 0.6f;
const float GRAVITY = 22.0f;
const float JUMP_V = 10.0f, AIR_JUMP_V = 9.5f;      // 땅 점프 (높이 약 2.3) + 공중 2단 점프 (약 2.0 더)
const float PILLAR_H = 3.0f, PILLAR_W = 1.3f;        // 스패너로 옮기는 기둥

const glm::vec3 SUN_DIR = glm::normalize(glm::vec3(0.35f, 0.88f, 0.3f));
const glm::vec3 SUN_COLOR(1.35f, 1.2f, 1.0f);
const glm::vec3 SKY_ZENITH(0.03f, 0.035f, 0.05f);
const glm::vec3 SKY_HORIZON(0.12f, 0.1f, 0.09f);

// ===================================================================
// 공통 유틸
// ===================================================================
std::mt19937 rng(777);
float frand(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(rng); }
int irand(int a, int b) { std::uniform_int_distribution<int> d(a, b); return d(rng); }

std::string filetobuf(const char* file) {
    std::ifstream shaderFile(file);
    if (!shaderFile.is_open()) {
        std::string altPath = std::string("Prototype/") + file;
        shaderFile.open(altPath);
        if (!shaderFile.is_open()) return "";
    }
    return std::string((std::istreambuf_iterator<char>(shaderFile)), std::istreambuf_iterator<char>());
}

GLuint compileShader(GLenum type, const std::string& src, const char* name) {
    const char* s = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &s, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, 1024, NULL, log);
        std::cerr << "[셰이더 컴파일 오류] " << name << "\n" << log << std::endl;
    }
    return sh;
}

GLuint makeProgram(const char* vsFile, const char* fsFile) {
    std::string vs = filetobuf(vsFile), fs = filetobuf(fsFile);
    if (vs.empty() || fs.empty()) {
        std::cerr << "셰이더 파일을 찾을 수 없습니다: " << vsFile << ", " << fsFile << std::endl;
        return 0;
    }
    GLuint v = compileShader(GL_VERTEX_SHADER, vs, vsFile);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs, fsFile);
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, 1024, NULL, log);
        std::cerr << "[셰이더 링크 오류] " << vsFile << " + " << fsFile << "\n" << log << std::endl;
        return 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return program;
}

GLint U(GLuint p, const char* name) { return glGetUniformLocation(p, name); }

glm::mat4 TRS(glm::vec3 t, glm::vec3 s) { return glm::scale(glm::translate(glm::mat4(1.0f), t), s); }
glm::mat4 at(glm::vec3 p) { return glm::translate(glm::mat4(1.0f), p); }
glm::mat4 at(float x, float y, float z) { return at(glm::vec3(x, y, z)); }
glm::mat4 rotX(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(1, 0, 0)); }
glm::mat4 rotZ(float a) { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 0, 1)); }

// 지역 Y 축을 축 정렬 방향 d 로 돌리는 행렬
glm::mat4 alignY(glm::vec3 d) {
    if (d.y > 0.5f) return glm::mat4(1.0f);
    if (d.y < -0.5f) return rotX(PI);
    if (d.x > 0.5f) return rotZ(-PI * 0.5f);
    if (d.x < -0.5f) return rotZ(PI * 0.5f);
    if (d.z > 0.5f) return rotX(PI * 0.5f);
    return rotX(-PI * 0.5f);
}

// a 에서 b 로 뻗는 막대: 지역 Y 축이 a->b 방향, 원점이 가운데 (길이는 scale 로)
glm::mat4 beamMat(glm::vec3 a, glm::vec3 b) {
    glm::vec3 y = b - a;
    float len = glm::length(y);
    y = len > 1e-5f ? y / len : glm::vec3(0, 1, 0);
    glm::vec3 ref = fabsf(y.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 x = glm::normalize(glm::cross(ref, y)), z = glm::cross(x, y);
    return glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4((a + b) * 0.5f, 1));
}

// ===================================================================
// 메쉬
// ===================================================================
struct Mesh { GLuint vao = 0, vbo = 0; GLsizei count = 0; };
Mesh cubeMesh, cylMesh;

void pushVert(std::vector<float>& v, glm::vec3 p, glm::vec3 n) { v.insert(v.end(), { p.x, p.y, p.z, n.x, n.y, n.z, 1.0f, 1.0f, 1.0f }); }
void pushTri(std::vector<float>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
    pushVert(v, a, n); pushVert(v, b, n); pushVert(v, c, n);
}
void pushQuad(std::vector<float>& v, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) { pushTri(v, p0, p1, p2); pushTri(v, p0, p2, p3); }

Mesh uploadMesh(const std::vector<float>& data) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STATIC_DRAW);
    for (int i = 0; i < 3; ++i) {
        glVertexAttribPointer(i, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(i * 3 * sizeof(float)));
        glEnableVertexAttribArray(i);
    }
    glBindVertexArray(0);
    m.count = (GLsizei)(data.size() / 9);
    return m;
}

Mesh buildCube() {
    float s = 0.5f;
    glm::vec3 v000(-s, -s, -s), v100(s, -s, -s), v110(s, s, -s), v010(-s, s, -s);
    glm::vec3 v001(-s, -s, s), v101(s, -s, s), v111(s, s, s), v011(-s, s, s);
    std::vector<float> d;
    pushQuad(d, v001, v101, v111, v011);
    pushQuad(d, v101, v100, v110, v111);
    pushQuad(d, v100, v000, v010, v110);
    pushQuad(d, v000, v001, v011, v010);
    pushQuad(d, v011, v111, v110, v010);
    pushQuad(d, v000, v100, v101, v001);
    return uploadMesh(d);
}

Mesh buildCylinder(int seg) {
    std::vector<float> d;
    for (int i = 0; i < seg; ++i) {
        float a0 = 2.0f * PI * i / seg, a1 = 2.0f * PI * (i + 1) / seg;
        glm::vec3 n0(cosf(a0), 0, sinf(a0)), n1(cosf(a1), 0, sinf(a1));
        glm::vec3 b0 = n0 * 0.5f + glm::vec3(0, -0.5f, 0), b1 = n1 * 0.5f + glm::vec3(0, -0.5f, 0);
        glm::vec3 t0 = n0 * 0.5f + glm::vec3(0, 0.5f, 0), t1 = n1 * 0.5f + glm::vec3(0, 0.5f, 0);
        pushVert(d, b0, n0); pushVert(d, t1, n1); pushVert(d, b1, n1);
        pushVert(d, b0, n0); pushVert(d, t0, n0); pushVert(d, t1, n1);
        pushTri(d, glm::vec3(0, 0.5f, 0), t1, t0);
        pushTri(d, glm::vec3(0, -0.5f, 0), b0, b1);
    }
    return uploadMesh(d);
}

// ===================================================================
// 90도 회전 (정수 행렬)
// ===================================================================
glm::mat3 quarterTurn(int axis, int turns) {
    glm::mat3 q(1.0f);
    if (axis == 0) q = glm::mat3(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0));
    else if (axis == 1) q = glm::mat3(glm::vec3(0, 0, -1), glm::vec3(0, 1, 0), glm::vec3(1, 0, 0));
    else q = glm::mat3(glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1));
    glm::mat3 r(1.0f);
    for (int i = 0; i < ((turns % 4) + 4) % 4; ++i) r = q * r;
    return r;
}

std::vector<glm::mat3> ROTS;
void initRotations() {
    ROTS = { glm::mat3(1.0f) };
    for (size_t i = 0; i < ROTS.size(); ++i)
        for (int a = 0; a < 3; ++a) {
            glm::mat3 m = quarterTurn(a, 1) * ROTS[i];
            if (std::find(ROTS.begin(), ROTS.end(), m) == ROTS.end()) ROTS.push_back(m);
        }
}

bool isUpright(const glm::mat3& R) { return (R * glm::vec3(0, 1, 0)).y > 0.5f; }

// ===================================================================
// 블럭 컨셉 + 발판 구성
//   블럭 바닥은 10x10 타일 3x3 장. fl[tz][tx] 가 'X' 면 바닥이 있고 '.' 이면 구멍 (아래 블럭으로 떨어지거나, 아래에서 올라오는 통로)
//   블럭마다 바닥에서 천장(높이 30)까지 이어지는 오르막(발판 경로/사다리/승강기/튕김 발판)이 있다
//   오르막이 끝나는 타일 바로 위 블럭의 그 자리에 구멍이 있어야 위층으로 올라간다
// ===================================================================
struct Theme {
    const char* name;
    glm::vec3 color, floor, accent;
    const char* fl[3];
};

enum { TH_GEAR, TH_CONVEYOR, TH_PRESS, TH_BOILER, TH_BENCH, TH_SUPERVISOR, TH_FEEDING, TH_CLOCK, TH_START, TH_END };
const int THEME_COUNT = 8;
const Theme THEMES[10] = {
    { "GEAR WORKS",       { 0.78f, 0.58f, 0.26f }, { 0.44f, 0.38f, 0.31f }, { 0.95f, 0.75f, 0.3f },  { "XXX", "XX.", "XXX" } },
    { "CONVEYOR LINE",    { 0.34f, 0.56f, 0.64f }, { 0.36f, 0.39f, 0.41f }, { 0.95f, 0.82f, 0.2f },  { "XXX", "XXX", "XX." } },
    { "PRESS SHOP",       { 0.78f, 0.3f, 0.22f },  { 0.42f, 0.35f, 0.32f }, { 1.0f, 0.78f, 0.1f },  { "XXX", "X.X", "XXX" } },
    { "BOILER ROOM",      { 0.58f, 0.34f, 0.2f },  { 0.32f, 0.27f, 0.25f }, { 1.0f, 0.45f, 0.15f }, { "XXX", "X.X", "XXX" } },
    { "WORKBENCH",        { 0.42f, 0.54f, 0.3f },  { 0.47f, 0.41f, 0.33f }, { 0.92f, 0.9f, 0.72f },  { "XXX", "XXX", ".XX" } },
    { "SUPERVISOR",       { 0.32f, 0.38f, 0.6f },  { 0.38f, 0.31f, 0.28f }, { 0.55f, 0.92f, 1.0f },  { "...", "XXX", "XXX" } },
    { "FEEDING MACHINE",  { 0.68f, 0.64f, 0.56f }, { 0.42f, 0.4f, 0.37f },  { 0.98f, 0.92f, 0.5f },  { "XXX", "XXX", "XX." } },
    { "TIME CLOCK",       { 0.56f, 0.42f, 0.64f }, { 0.4f, 0.35f, 0.4f },   { 1.0f, 0.95f, 0.8f },   { ".XX", "XXX", "XXX" } },
    { "START POINT",    { 0.35f, 0.72f, 0.42f }, { 0.4f, 0.42f, 0.36f },  { 0.6f, 1.0f, 0.6f },    { "XXX", "XXX", "XXX" } },
    { "GOAL POINT", { 0.95f, 0.85f, 0.6f },  { 0.45f, 0.42f, 0.36f }, { 1.0f, 0.9f, 0.5f },    { "XXX", "X.X", "XXX" } },
};

const float TILE = CS / 3.0f;
const float EXIT_H = CS - 1.8f;     // 오르막 꼭대기 발판 높이: 한 번 점프로 위 블럭 바닥(CS + 0.4)에 올라선다

// 발판 경로: 블럭 가운데를 도는 사각 둘레를 따라 일정 간격으로 놓고 높이를 조금씩 올린다
struct RStep { float x, z, h; };
struct RouteSpec { float r, u0; int dir; float s, rise, rise2, h0; int skip; };
std::vector<RStep> squareRoute(const RouteSpec& sp) {
    std::vector<RStep> v;
    float per = 8.0f * sp.r, h = sp.h0;
    for (int i = 0;; ++i) {
        float u = fmodf(sp.u0 + sp.dir * (i + sp.skip) * sp.s, per);
        if (u < 0.0f) u += per;
        int side = (int)(u / (2.0f * sp.r));
        float t = u - side * 2.0f * sp.r, x, z;
        if (side == 0) { x = -sp.r + t; z = -sp.r; }
        else if (side == 1) { x = sp.r; z = -sp.r + t; }
        else if (side == 2) { x = sp.r - t; z = sp.r; }
        else { x = -sp.r; z = sp.r - t; }
        bool last = h >= EXIT_H - 0.01f;
        if (last) h = EXIT_H;
        v.push_back({ x, z, h });
        if (last) break;
        h = std::min(EXIT_H, h + ((sp.rise2 > 0.0f && i % 2 == 1) ? sp.rise2 : sp.rise));
    }
    return v;
}

std::vector<RStep> ROUTE[10];           // 컨셉별 발판 경로
int EXIT_T[10][2];                      // 오르막이 끝나는 타일 (-1 이면 없음)
std::vector<glm::vec3> ANCHOR[10];      // 톱니를 띄울 자리 (x, h, z)
const glm::vec2 LADDER_XZ(CS / 3.0f, -CS * 0.5f + 1.4f);
const glm::vec2 PAD0_XZ(-5.6f, -5.6f);

int tileOf(float v) { return glm::clamp((int)lroundf((v + TILE) / TILE), 0, 2); }

void initThemes() {
    // r, u0, dir, 간격, 오름, 교대 오름(2단 점프), 시작 높이, 건너뛰기
    RouteSpec spec[10] = {};
    spec[TH_GEAR] = { 7.0f, 0.0f, 1, 3.8f, 2.0f, 0.0f, 2.4f, 0 };
    spec[TH_CONVEYOR] = { 7.0f, 14.0f, 1, 3.6f, 2.0f, 0.0f, 2.4f, 0 };
    spec[TH_PRESS] = { 6.3f, 25.2f, -1, 4.0f, 2.0f, 3.0f, 2.4f, 0 };
    spec[TH_SUPERVISOR] = { 7.7f, 46.2f, 1, 3.4f, 2.0f, 0.0f, 2.4f, 0 };
    spec[TH_FEEDING] = { 5.6f, 0.0f, 1, 5.0f, 5.5f, 0.0f, 6.4f, 1 };
    spec[TH_CLOCK] = { 7.7f, 15.4f, -1, 3.4f, 2.0f, 0.0f, 2.4f, 0 };
    spec[TH_START] = { 7.0f, 14.0f, 1, 3.6f, 2.0f, 0.0f, 2.4f, 0 };
    for (int t = 0; t < 10; ++t) {
        ROUTE[t].clear();
        ANCHOR[t].clear();
        EXIT_T[t][0] = EXIT_T[t][1] = -1;
        if (spec[t].r > 0.0f) {
            ROUTE[t] = squareRoute(spec[t]);
            const RStep& e = ROUTE[t].back();
            EXIT_T[t][0] = tileOf(e.x);
            EXIT_T[t][1] = tileOf(e.z);
            if (t == TH_START) continue;
            size_t n = ROUTE[t].size();
            for (size_t k : { n / 3, (2 * n) / 3 }) {
                const RStep& r = ROUTE[t][k];
                float off = t == TH_FEEDING ? -0.7f : 0.0f;
                ANCHOR[t].push_back({ r.x + off, r.h + 1.4f, r.z + off });
            }
        }
    }
    // 보일러: 가운데 증기 승강기 (+ 높이 10, 20 의 둘레 발판)
    EXIT_T[TH_BOILER][0] = EXIT_T[TH_BOILER][1] = 1;
    ANCHOR[TH_BOILER] = { { 3.6f, CS / 3.0f + 1.8f, 0.0f }, { 0.0f, CS * 2.0f / 3.0f + 1.8f, -3.6f } };
    // 작업대: 한쪽 가장자리 사다리 (+ 쉼터 발판)
    EXIT_T[TH_BENCH][0] = tileOf(LADDER_XZ.x);
    EXIT_T[TH_BENCH][1] = tileOf(LADDER_XZ.y);
    ANCHOR[TH_BENCH] = { { LADDER_XZ.x + 2.0f, CS / 3.0f + 1.8f, LADDER_XZ.y + 1.4f }, { LADDER_XZ.x + 2.0f, CS * 2.0f / 3.0f + 1.8f, LADDER_XZ.y + 1.4f } };
}

// ===================================================================
// 블럭 / 단블럭 / 다단블럭
// ===================================================================
struct Cell {
    glm::ivec3 off;
    int theme;
    glm::mat3 orient;       // 칸 내용물 → 조각 공간 회전 (다단블럭 안의 칸은 저마다 다를 수 있다)
    unsigned seed;
    int role = 0;           // 다단블럭: 1 옛 시작 칸, 2 옛 도착 칸 (바깥 면이 열린 입구/출구)
};

struct Socket { glm::vec3 local; bool filled; };    // 기둥 꽂는 자리 (조각 고유 좌표, 칸 바닥 위)

struct Piece {
    std::string name;
    std::vector<Cell> cells;
    bool custom = false;            // 다단블럭
    int customSize = 0;
    glm::mat3 rot = glm::mat3(1.0f);
    bool placed = false;
    glm::ivec3 pos = glm::ivec3(0);
    glm::ivec3 solPos = glm::ivec3(0);
    bool inSolution = true;         // 생성기의 정답에 쓰인 조각인지 (아니면 선택 사항)
    std::vector<Socket> sockets;    // 다단블럭은 기둥이 꽂힌 채로, 단블럭은 빈 소켓으로 시작
    int fromStage = -1;             // 다단블럭: 어느 촬영(스테이지)에서 나왔는지
};

std::vector<glm::ivec3> shapeCells(const Piece& p, const glm::mat3& rot) {
    std::vector<glm::ivec3> v;
    glm::ivec3 mn(1 << 20);
    for (const auto& c : p.cells) {
        glm::ivec3 r = glm::ivec3(glm::round(rot * glm::vec3(c.off)));
        v.push_back(r);
        mn = glm::min(mn, r);
    }
    for (auto& r : v) r -= mn;
    return v;
}

// 칸 내용물의 최종 회전 (seed 로 세로축 방향만 다양화)
glm::mat3 contentRot(const glm::mat3& pieceRot, const Cell& c) { return pieceRot * c.orient * quarterTurn(1, (int)(c.seed % 4)); }

// ===================================================================
// 씬 상태
// ===================================================================
int N = 2;
int stage = 0;
std::vector<Piece> pieces;
std::vector<Piece> library;         // 다단블럭 보관함
std::vector<int> occ;               // -1 어둠, -3 시작, -4 끝, 그 외 조각 번호
glm::ivec3 startCell(0), endCell(1);
const int OCC_EMPTY = -1, OCC_START = -3, OCC_END = -4;

struct Pickup { glm::vec3 pos; int type; bool taken; };    // type 0 톱니, 1 멜빵(스패너)
std::vector<Pickup> pickups;
bool hasSpanner = false;
bool carrying = false;              // 기둥을 들고 있는지
bool stageClear = false;

int idxOf(glm::ivec3 c) { return c.x + c.y * N + c.z * N * N; }
glm::ivec3 cellOf(int i) { return glm::ivec3(i % N, (i / N) % N, i / (N * N)); }
bool inGrid(glm::ivec3 c) { return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < N && c.y < N && c.z < N; }
bool isFixed(glm::ivec3 c) { return c == startCell || c == endCell; }
const glm::ivec3 NB[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

glm::vec3 cellWorldCenter(glm::ivec3 c) {
    return glm::vec3((c.x - (N - 1) * 0.5f) * CS, c.y * CS + HALF, (c.z - (N - 1) * 0.5f) * CS);
}

void rebuildOcc() {
    occ.assign(N * N * N, OCC_EMPTY);
    occ[idxOf(startCell)] = OCC_START;
    occ[idxOf(endCell)] = OCC_END;
    for (int p = 0; p < (int)pieces.size(); ++p) {
        if (!pieces[p].placed) continue;
        for (auto c : shapeCells(pieces[p], pieces[p].rot)) occ[idxOf(pieces[p].pos + c)] = p;
    }
}

int filledCount() { int n = 0; for (int v : occ) if (v != OCC_EMPTY) ++n; return n; }
bool gridComplete() { return filledCount() == N * N * N; }
int cogsTotal() { int n = 0; for (auto& p : pickups) if (p.type == 0) ++n; return n; }
int cogsTaken() { int n = 0; for (auto& p : pickups) if (p.type == 0 && p.taken) ++n; return n; }

bool canPlace(const Piece& p, const glm::mat3& rot, glm::ivec3 pos, int self) {
    for (auto c : shapeCells(p, rot)) {
        glm::ivec3 g = pos + c;
        if (!inGrid(g)) return false;
        int o = occ[idxOf(g)];
        if (o != OCC_EMPTY && o != self) return false;
    }
    return true;
}

glm::vec3 pieceCenter(const Piece& p) {
    glm::vec3 c(0.0f);
    auto sc = shapeCells(p, p.rot);
    for (auto s : sc) c += cellWorldCenter(p.pos + s);
    return c / (float)sc.size();
}
// 조각의 월드 변환: 조각 안 모든 지오메트리 = 중심 + rot * (조각 고유 좌표)
glm::mat4 pieceXform(const Piece& p) { return glm::translate(glm::mat4(1.0f), pieceCenter(p)) * glm::mat4(p.rot); }

// ===================================================================
// 생성기: 정답 배치를 먼저 만들고, 추상 모델로 시작 → 톱니 → 끝 까지 갈 수 있는지 확인
// ===================================================================
struct Abs { int theme = -1; glm::mat3 R = glm::mat3(1.0f); bool walk = false; int grp = -1; bool door = false; };
// 두 칸 사이를 지나갈 수 있는지: 다단블럭 껍데기는 문 칸(옛 시작/도착)만 열려 있다
bool absOpen(const Abs& a, const Abs& b) {
    if (a.grp == b.grp) return true;
    if (a.grp >= 0 && !a.door) return false;
    if (b.grp >= 0 && !b.door) return false;
    return true;
}

bool absSolid(const Abs& a, int wtx, int wtz) {
    glm::vec3 l = glm::transpose(a.R) * glm::vec3(-TILE + TILE * wtx, 0.0f, -TILE + TILE * wtz);
    return THEMES[a.theme].fl[tileOf(l.z)][tileOf(l.x)] == 'X';
}

bool absExit(const Abs& a, int& wtx, int& wtz) {
    if (EXIT_T[a.theme][0] < 0) return false;
    glm::vec3 w = a.R * glm::vec3(-TILE + TILE * EXIT_T[a.theme][0], 0.0f, -TILE + TILE * EXIT_T[a.theme][1]);
    wtx = tileOf(w.x);
    wtz = tileOf(w.z);
    return true;
}

std::vector<char> absReach(const std::vector<Abs>& A) {
    std::vector<char> vis(N * N * N, 0);
    std::vector<int> q{ idxOf(startCell) };
    vis[q[0]] = 1;
    for (size_t qi = 0; qi < q.size(); ++qi) {
        glm::ivec3 c = cellOf(q[qi]);
        const Abs& a = A[q[qi]];
        if (!a.walk) continue;
        auto push = [&](glm::ivec3 n) { if (inGrid(n) && !vis[idxOf(n)] && A[idxOf(n)].walk && absOpen(a, A[idxOf(n)])) { vis[idxOf(n)] = 1; q.push_back(idxOf(n)); } };
        // 옆 블럭: 맞닿은 가장자리 타일이 양쪽 다 바닥이면 걸어서 건넌다
        for (int d = 0; d < 6; ++d) {
            if (d == 2 || d == 3) continue;
            glm::ivec3 n = c + NB[d];
            if (!inGrid(n) || !A[idxOf(n)].walk) continue;
            const Abs& b = A[idxOf(n)];
            bool ok = false;
            for (int k = 0; k < 3 && !ok; ++k) {
                if (d == 0) ok = absSolid(a, 2, k) && absSolid(b, 0, k);
                if (d == 1) ok = absSolid(a, 0, k) && absSolid(b, 2, k);
                if (d == 4) ok = absSolid(a, k, 2) && absSolid(b, k, 0);
                if (d == 5) ok = absSolid(a, k, 0) && absSolid(b, k, 2);
            }
            if (ok) push(n);
        }
        // 위: 오르막 끝 타일 바로 위에 구멍
        int ex, ez;
        glm::ivec3 up = c + glm::ivec3(0, 1, 0);
        if (inGrid(up) && A[idxOf(up)].walk && absExit(a, ex, ez) && !absSolid(A[idxOf(up)], ex, ez)) push(up);
        // 아래: 바닥에 구멍이 하나라도 있으면 떨어져 내려간다
        glm::ivec3 dn = c - glm::ivec3(0, 1, 0);
        bool hole = false;
        for (int i = 0; i < 9; ++i) if (!absSolid(a, i % 3, i / 3)) hole = true;
        if (hole) push(dn);
    }
    return vis;
}

void partitionInto(std::vector<char>& used, std::vector<Piece>& out) {
    int total = N * N * N, lastTheme = -1;
    while (true) {
        std::vector<int> fr;
        for (int i = 0; i < total; ++i) if (!used[i]) fr.push_back(i);
        if (fr.empty()) break;
        std::shuffle(fr.begin(), fr.end(), rng);
        int start = fr[0], bestN = 99;
        for (int i : fr) {
            int n = 0;
            for (auto d : NB) { glm::ivec3 c = cellOf(i) + d; if (inGrid(c) && !used[idxOf(c)]) ++n; }
            if (n < bestN) { bestN = n; start = i; }
        }
        int roll = irand(0, 99);
        int target = N == 2 ? (roll < 15 ? 1 : roll < 60 ? 2 : 3) : (roll < 8 ? 1 : roll < 30 ? 2 : roll < 62 ? 3 : 4);
        std::vector<int> pc{ start };
        used[start] = 1;
        while ((int)pc.size() < target) {
            std::vector<int> cand;
            for (int gi : pc)
                for (auto d : NB) {
                    glm::ivec3 n = cellOf(gi) + d;
                    if (inGrid(n) && !used[idxOf(n)]) cand.push_back(idxOf(n));
                }
            if (cand.empty()) break;
            int pick = cand[irand(0, (int)cand.size() - 1)];
            used[pick] = 1;
            pc.push_back(pick);
        }
        Piece p;
        glm::ivec3 mn(1 << 20);
        for (int gi : pc) mn = glm::min(mn, cellOf(gi));
        int theme;
        do { theme = irand(0, THEME_COUNT - 1); } while (theme == lastTheme);
        lastTheme = theme;
        for (int gi : pc) p.cells.push_back({ cellOf(gi) - mn, theme, glm::mat3(1.0f), (unsigned)irand(0, 1 << 20) });
        p.solPos = mn;
        out.push_back(p);
    }
}

// 칸 바닥 모서리 타일 위 한 지점 (조각 고유 좌표). 내용물이 똑바로 선 칸만
bool floorSpot(const Piece& p, int k, glm::vec3& out) {
    const Cell& c = p.cells[k];
    glm::mat3 R = c.orient * quarterTurn(1, (int)(c.seed % 4));
    if (!isUpright(R) || c.theme >= TH_START) return false;
    glm::vec3 avg(0.0f);
    for (const auto& q : p.cells) avg += glm::vec3(q.off);
    avg /= (float)p.cells.size();
    const int corners[4][2] = { { 0, 0 }, { 2, 0 }, { 0, 2 }, { 2, 2 } };
    int o = irand(0, 3);
    for (int i = 0; i < 4; ++i) {
        int tx = corners[(i + o) % 4][0], tz = corners[(i + o) % 4][1];
        if (THEMES[c.theme].fl[tz][tx] != 'X') continue;
        out = (glm::vec3(c.off) - avg) * CS + R * glm::vec3(-TILE + TILE * tx, -HALF + SLAB, -TILE + TILE * tz);
        return true;
    }
    return false;
}

// 정답에 쓰지 않는 여분 단블럭 (넉넉하게 주기)
Piece randomExtraPiece(int sz) {
    std::vector<glm::ivec3> cs{ glm::ivec3(irand(0, N - 1), irand(0, N - 1), irand(0, N - 1)) };
    for (int guard = 0; (int)cs.size() < sz && guard < 80; ++guard) {
        glm::ivec3 b = cs[irand(0, (int)cs.size() - 1)] + NB[irand(0, 5)];
        if (!inGrid(b) || std::find(cs.begin(), cs.end(), b) != cs.end()) continue;
        cs.push_back(b);
    }
    glm::ivec3 mn(1 << 20);
    for (auto& x : cs) mn = glm::min(mn, x);
    Piece p;
    int theme = irand(0, THEME_COUNT - 1);
    for (auto& x : cs) p.cells.push_back({ x - mn, theme, glm::mat3(1.0f), (unsigned)irand(0, 1 << 20) });
    p.inSolution = false;
    return p;
}

std::string gridLabel(int n) { return std::to_string(n) + "X" + std::to_string(n) + "X" + std::to_string(n); }

void generateStage() {
    const int sizes[] = { 2, 3, 4 };
    N = sizes[std::min(stage, 2)];
    // 넘버패드 기준: 아래층 7번(먼 쪽 왼쪽) 시작, 위층 2번(가까운 쪽 가운데) 끝
    startCell = glm::ivec3(0, 0, 0);
    endCell = glm::ivec3(N == 2 ? 1 : N / 2, N - 1, N - 1);
    int total = N * N * N;
    int itemsWanted = stage == 0 ? 1 : stage == 1 ? 2 : 3;

    int best = -1;
    for (int i = 0; i < (int)library.size(); ++i)
        if (library[i].customSize < N && (best < 0 || library[i].customSize > library[best].customSize)) best = i;
    bool multiRequired = best >= 0;                     // SCENE 2부터 다단블럭을 꼭 써야 한다
    int extra = best >= 0 ? 1 : 0;                     // 다단블럭이 있으면 그 씬에 멜빵(스패너)도 놓는다

    std::vector<Piece> chosen;
    std::vector<int> cand;
    std::vector<Abs> chosenA;
    for (int attempt = 0; attempt < 800; ++attempt) {
        std::vector<Piece> ps;
        std::vector<char> used(total, 0);
        used[idxOf(startCell)] = used[idxOf(endCell)] = 1;
        if (multiRequired) {
            int S = library[best].customSize;
            std::vector<glm::ivec3> spots;
            for (int z = 0; z + S <= N; ++z)
                for (int y = 0; y + S <= N; ++y)
                    for (int x = 0; x + S <= N; ++x) {
                        bool ok = true;
                        for (glm::ivec3 f : { startCell, endCell })
                            if (f.x >= x && f.x < x + S && f.y >= y && f.y < y + S && f.z >= z && f.z < z + S) ok = false;
                        if (ok) spots.push_back({ x, y, z });
                    }
            if (spots.empty()) { multiRequired = false; }
            else {
                Piece m = library[best];
                m.solPos = spots[irand(0, (int)spots.size() - 1)];
                for (auto& c : m.cells) used[idxOf(m.solPos + c.off)] = 1;
                ps.push_back(m);
            }
        }
        partitionInto(used, ps);

        std::vector<Abs> A(total);
        A[idxOf(startCell)] = { TH_START, glm::mat3(1.0f), true };
        A[idxOf(endCell)] = { TH_END, glm::mat3(1.0f), true };
        for (int pi = 0; pi < (int)ps.size(); ++pi)
            for (const auto& c : ps[pi].cells) {
                glm::mat3 R = contentRot(glm::mat3(1.0f), c);
                A[idxOf(ps[pi].solPos + c.off)] = { c.theme, R, isUpright(R), ps[pi].custom ? pi : -1, c.role != 0 };
            }
        auto reach = absReach(A);
        if (!reach[idxOf(endCell)]) continue;
        // 다단블럭을 지나가지 않고도 도착할 수 있으면 다시 (오래 못 찾으면 봐준다)
        if (multiRequired && attempt < 600) {
            std::vector<Abs> B = A;
            for (auto& x : B) if (x.grp >= 0) x.walk = false;
            if (absReach(B)[idxOf(endCell)]) continue;
        }
        std::vector<int> cc;
        for (int i = 0; i < total; ++i)
            if (reach[i] && !isFixed(cellOf(i)) && A[i].walk && !ANCHOR[A[i].theme].empty()) cc.push_back(i);
        if ((int)cc.size() < itemsWanted + extra) continue;
        std::cout << "[gen] N " << N << " attempt " << attempt << (multiRequired && attempt >= 600 ? " (multi bypass allowed)" : "") << std::endl;
        chosen = ps;
        cand = cc;
        chosenA = A;
        break;
    }
    if (chosen.empty()) std::cout << "[경고] 생성기가 경로를 찾지 못했습니다\n";

    pickups.clear();
    std::shuffle(cand.begin(), cand.end(), rng);
    for (int k = 0; k < itemsWanted + extra && k < (int)cand.size(); ++k) {
        const Abs& a = chosenA[cand[k]];
        const auto& an = ANCHOR[a.theme];
        glm::vec3 l = an[irand(0, (int)an.size() - 1)];
        glm::vec3 w = cellWorldCenter(cellOf(cand[k])) + a.R * glm::vec3(l.x, -HALF + l.y, l.z);
        pickups.push_back({ w, k < itemsWanted ? 0 : 1, false });
    }

    pieces.clear();
    for (auto& p : chosen) { p.inSolution = true; pieces.push_back(p); }
    if (best >= 0 && !multiRequired) {
        Piece m = library[best];
        m.inSolution = false;
        pieces.push_back(m);
    }
    // 넉넉하게: 정답 단블럭 칸 수의 약 1.4배. 다단블럭이 필수면 단블럭만으로는 다 못 채우게 한다
    {
        int need = 0;
        for (auto& p : pieces) if (!p.custom) need += (int)p.cells.size();
        int extraCells = (int)ceilf(need * 0.4f);
        if (multiRequired) extraCells = std::min(extraCells, (total - 2 - 1) - need);
        while (extraCells > 0) {
            int sz = extraCells == 1 ? 1 : std::min(extraCells, irand(2, 4));
            Piece e = randomExtraPiece(sz);
            extraCells -= (int)e.cells.size();
            pieces.push_back(e);
        }
    }
    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.custom > b.custom; });
    // 빈 기둥 소켓: 다단블럭 기둥 수 + 1 개를 단블럭 바닥에 하나씩
    {
        int pillars = 0;
        for (auto& p : pieces) if (p.custom) pillars += (int)p.sockets.size();
        std::vector<int> cand;
        for (int i = 0; i < (int)pieces.size(); ++i) if (!pieces[i].custom) { pieces[i].sockets.clear(); cand.push_back(i); }
        std::shuffle(cand.begin(), cand.end(), rng);
        int want = pillars > 0 ? pillars + 1 : 0;
        for (int i : cand) {
            if (want <= 0) break;
            Piece& p = pieces[i];
            int k = irand(0, (int)p.cells.size() - 1);
            glm::vec3 l;
            if (floorSpot(p, k, l)) { p.sockets.push_back({ l, false }); --want; }
        }
    }
    int letter = 0;
    for (auto& p : pieces) {
        p.rot = ROTS[irand(0, 23)];
        p.placed = false;
        if (!p.custom) p.name = std::string(1, (char)('A' + letter++ % 26));
    }
    hasSpanner = false;
    carrying = false;
    stageClear = false;
    rebuildOcc();
}

// 클리어한 씬 → 다단블럭 (시작/끝 블럭도 그대로 들어간다)
void saveCustomBlock() {
    Piece c;
    c.custom = true;
    c.customSize = N;
    c.name = "MY " + gridLabel(N);
    for (int i = 0; i < N * N * N; ++i) {
        glm::ivec3 g = cellOf(i);
        if (occ[i] == OCC_START) { c.cells.push_back({ g, TH_START, glm::mat3(1.0f), 0u, 1 }); continue; }
        if (occ[i] == OCC_END) { c.cells.push_back({ g, TH_END, glm::mat3(1.0f), 0u, 2 }); continue; }
        const Piece& p = pieces[occ[i]];
        auto sc = shapeCells(p, p.rot);
        for (size_t k = 0; k < sc.size(); ++k)
            if (p.pos + sc[k] == g) c.cells.push_back({ g, p.cells[k].theme, p.rot * p.cells[k].orient, p.cells[k].seed });
    }
    // 안에 기둥을 꽂아 둔다 (스패너로 뽑아 갈 수 있다)
    {
        std::vector<int> ks;
        for (int k = 0; k < (int)c.cells.size(); ++k) ks.push_back(k);
        std::shuffle(ks.begin(), ks.end(), rng);
        int want = N == 2 ? 2 : 3;
        for (int k : ks) {
            if (want <= 0) break;
            glm::vec3 l;
            if (floorSpot(c, k, l)) { c.sockets.push_back({ l, true }); --want; }
        }
    }
    c.fromStage = stage;
    library.erase(std::remove_if(library.begin(), library.end(), [](const Piece& q) { return q.fromStage == stage; }), library.end());
    library.push_back(c);
    std::sort(library.begin(), library.end(), [](const Piece& a, const Piece& b) { return a.fromStage < b.fromStage; });
}

// ===================================================================
// 월드 데이터
// ===================================================================
struct Material { glm::vec3 color; float ks, shininess, emissive; int pattern; };
Material mat(glm::vec3 c, float ks = 0.2f, float sh = 16.0f, float em = 0.0f, int pat = 0) { return { c, ks, sh, em, pat }; }

struct Box {
    glm::vec3 center, size;
    Material mat;
    bool solid, visible;
    glm::vec3 belt;         // 컨베이어: 위에 서면 이 속도로 실려 간다
    float bounce;           // 튕김 발판: 밟으면 이 속도로 튀어 오른다
    int chunk;              // 소속 조각 (-1 고정 지형). 재배치 연출 때 조각 단위로 움직인다
    glm::vec3 bmin() const { return center - size * 0.5f; }
    glm::vec3 bmax() const { return center + size * 0.5f; }
};

enum DecoKind { D_STATIC, D_SPIN, D_GEAR, D_PISTON, D_SLIDE, D_HAND };
struct Deco {
    glm::mat4 base;
    int kind, mesh, teeth;
    glm::vec3 size;
    Material mat;
    float speed, amp, phase;
    int chunk;
};

struct Shaft { glm::vec3 mn, mx, dir; };       // 증기 승강기
struct Ladder { glm::vec3 mn, mx; };
struct Vent { glm::vec3 pos, dir; float acc; int chunk; };

std::vector<Box> boxes;
std::vector<Deco> decos;
std::vector<Shaft> shafts;
std::vector<Ladder> ladders;
std::vector<Vent> vents;
std::vector<int> voidCells;
glm::vec3 goalPos(0.0f);
int curChunk = -1;

void addBox(glm::vec3 c, glm::vec3 s, const Material& m, bool solid = true, glm::vec3 belt = glm::vec3(0.0f), bool visible = true, float bounce = 0.0f) {
    boxes.push_back({ c, s, m, solid, visible, belt, bounce, curChunk });
}

// 블럭 하나를 지역 좌표(-6~6, 바닥은 -Y)로 짓고 회전 R 을 적용해 월드에 넣는 도우미
struct CellBuilder {
    glm::mat3 R;
    glm::vec3 C;
    static glm::vec3 L(float x, float h, float z) { return glm::vec3(x, -HALF + h, z); }
    void box(glm::vec3 lc, glm::vec3 ls, const Material& m, bool solid = true, glm::vec3 belt = glm::vec3(0.0f), bool visible = true, float bounce = 0.0f) {
        addBox(C + R * lc, glm::abs(R * ls), m, solid, R * belt, visible, bounce);
    }
    void pillar(float x, float z, float sx, float sz, float top, const Material& m, bool solid = true) {
        box(L(x, (SLAB + top) * 0.5f, z), glm::vec3(sx, top - SLAB, sz), m, solid);
    }
    void plat(float x, float z, float sx, float sz, float top, const Material& m, float thick = 0.5f) {
        box(L(x, top - thick * 0.5f, z), glm::vec3(sx, thick, sz), m);
    }
    glm::mat4 toWorld(const glm::mat4& local) { return glm::translate(glm::mat4(1.0f), C) * glm::mat4(R) * local; }
    void deco(int kind, const glm::mat4& local, glm::vec3 size, int mesh, const Material& m, float speed = 0, float amp = 0, float phase = 0, int teeth = 0) {
        decos.push_back({ toWorld(local), kind, mesh, teeth, size, m, speed, amp, phase, curChunk });
    }
    void ladder(float x, float z, float h0, float h1) {
        glm::vec3 a = C + R * L(x - 0.7f, h0, z - 0.6f), b = C + R * L(x + 0.7f, h1, z + 0.6f);
        if (isUpright(R)) ladders.push_back({ glm::min(a, b), glm::max(a, b) });
        Material rail = mat(glm::vec3(0.35f, 0.3f, 0.26f), 0.6f, 32.0f);
        for (int s = -1; s <= 1; s += 2) deco(D_STATIC, at(L(x + 0.6f * s, (h0 + h1) * 0.5f, z)), glm::vec3(0.12f, h1 - h0, 0.12f), 0, rail);
        for (float h = h0 + 0.4f; h < h1; h += 0.6f) deco(D_STATIC, at(L(x, h, z)), glm::vec3(1.2f, 0.08f, 0.08f), 0, rail);
    }
    void lift(float h0, float h1) {
        glm::vec3 c = C + R * L(0, (h0 + h1) * 0.5f, 0), s = glm::abs(R * glm::vec3(3.2f, h1 - h0, 3.2f));
        shafts.push_back({ c - s * 0.5f, c + s * 0.5f, R * glm::vec3(0, 1, 0) });
        vents.push_back({ C + R * L(0, 0.2f, 0), R * glm::vec3(0, 1, 0), 0.0f, curChunk });
    }
    void pad(float x, float z, float size, float top, float strength) {
        Material m = mat(glm::vec3(0.95f, 0.35f, 0.25f), 0.6f, 32.0f, 0.25f), spring = mat(glm::vec3(0.6f, 0.6f, 0.62f), 1.0f, 64.0f);
        box(L(x, top - 0.15f, z), glm::vec3(size, 0.3f, size), m, true, glm::vec3(0.0f), true, strength);
        for (int i = 0; i < 3; ++i) deco(D_STATIC, at(L(x, top - 0.38f - i * 0.18f, z)), glm::vec3(size * 0.7f, 0.08f, size * 0.7f), 1, spring);
    }
};

// ---------------- 컨셉별 맵 (블럭 21 x 21 x 21, 발판/기믹 크기는 그대로) ----------------
// 위치 값은 30 기준으로 적고 P() 로 블럭 크기에 맞춘다 (크기 값은 그대로)
inline float P(float v) { return v * CS / 30.0f; }

// 경로 발판: 떠 있는 발판 + 바닥까지 내려가는 지지대
void routePlat(CellBuilder& b, const RStep& r, float size, const Material& top, const Material& pole, float thick = 0.5f) {
    b.plat(r.x, r.z, size, size, r.h, top, thick);
    b.deco(D_STATIC, at(CellBuilder::L(r.x, (SLAB + r.h - thick) * 0.5f, r.z)), glm::vec3(0.22f, r.h - thick - SLAB, 0.22f), 1, pole);
}

// 바닥 장애물: 상자 더미 / 드럼통 / 눕힌 파이프
// 가운데 승강기 자리, 구멍 타일, 경로 첫 발판 근처는 피한다
void addClutter(CellBuilder& b, int theme, std::mt19937& r, int count) {
    auto rnd = [&](float a, float c) { std::uniform_real_distribution<float> d(a, c); return d(r); };
    const Theme& t = THEMES[theme];
    Material crate = mat(glm::vec3(0.62f, 0.45f, 0.28f), 0.1f, 8.0f, 0.0f, 2), drum = mat(t.color * 0.75f, 0.6f, 32.0f), pipe = mat(glm::vec3(0.5f, 0.42f, 0.32f), 0.8f, 48.0f);
    float lim = HALF - 2.2f;
    int placed = 0;
    for (int tries = 0; tries < count * 10 && placed < count; ++tries) {
        float x = rnd(-lim, lim), z = rnd(-lim, lim);
        if (fabsf(x) < 3.5f && fabsf(z) < 3.5f) continue;
        if (t.fl[tileOf(z)][tileOf(x)] != 'X') continue;
        if (!ROUTE[theme].empty() && glm::length(glm::vec2(x - ROUTE[theme][0].x, z - ROUTE[theme][0].z)) < 3.5f) continue;
        if (theme == TH_BENCH && glm::length(glm::vec2(x - LADDER_XZ.x, z - LADDER_XZ.y)) < 3.0f) continue;
        if (theme == TH_END && glm::length(glm::vec2(x - P(-10.0f), z - P(-10.0f))) < 3.5f) continue;
        if (theme == TH_FEEDING && glm::length(glm::vec2(x - PAD0_XZ.x, z - PAD0_XZ.y)) < 3.0f) continue;
        int kind = (int)rnd(0.0f, 2.999f);
        if (kind == 0) {
            float s = rnd(1.2f, 2.0f);
            b.pillar(x, z, s, s, SLAB + s, crate);
            if (rnd(0.0f, 1.0f) < 0.5f) b.pillar(x + s * 0.6f, z, s * 0.7f, s * 0.7f, SLAB + s * 0.7f, crate);
        } else if (kind == 1) {
            for (int i = 0; i < 3; ++i) {
                float dx = (i - 1) * 0.85f, hh = i == 1 ? 1.6f : 1.2f;
                b.box(CellBuilder::L(x + dx, SLAB + hh * 0.5f, z), glm::vec3(0.75f, hh, 0.75f), drum, true, glm::vec3(0.0f), false);
                b.deco(D_STATIC, at(CellBuilder::L(x + dx, SLAB + hh * 0.5f, z)), glm::vec3(0.8f, hh, 0.8f), 1, drum);
            }
        } else {
            float len = rnd(2.5f, 4.5f);
            bool alongX = rnd(0.0f, 1.0f) < 0.5f;
            b.box(CellBuilder::L(x, SLAB + 0.4f, z), alongX ? glm::vec3(len, 0.8f, 0.8f) : glm::vec3(0.8f, 0.8f, len), pipe, true, glm::vec3(0.0f), false);
            b.deco(D_STATIC, at(CellBuilder::L(x, SLAB + 0.4f, z)) * (alongX ? rotZ(PI * 0.5f) : rotX(PI * 0.5f)), glm::vec3(0.8f, len, 0.8f), 1, pipe);
        }
        ++placed;
    }
}

void buildGear(CellBuilder& b, const Theme& t) {
    Material brass = mat(t.accent, 0.9f, 48.0f), iron = mat(glm::vec3(0.22f, 0.2f, 0.19f), 0.5f, 32.0f);
    const auto& R = ROUTE[TH_GEAR];
    for (size_t i = 0; i < R.size(); ++i) {
        b.box(CellBuilder::L(R[i].x, R[i].h - 0.25f, R[i].z), glm::vec3(2.6f, 0.5f, 2.6f), brass, true, glm::vec3(0.0f), false);
        b.deco(D_GEAR, at(CellBuilder::L(R[i].x, R[i].h - 0.25f, R[i].z)), glm::vec3(1.45f, 0.5f, 0), 1, brass, i % 2 ? 0.6f : -0.6f, 0, 0, 10);
        b.deco(D_STATIC, at(CellBuilder::L(R[i].x, (SLAB + R[i].h - 0.5f) * 0.5f, R[i].z)), glm::vec3(0.3f, R[i].h - 0.5f - SLAB, 0.3f), 1, iron);
    }
    float wg[4][4] = { { -5.0f, 8.0f, 3.0f, 0.3f }, { -9.6f, 4.0f, 1.6f, -0.56f }, { 3.0f, 15.0f, 2.4f, -0.37f }, { 7.0f, 22.0f, 1.6f, 0.56f } };
    for (auto& g : wg) b.deco(D_GEAR, at(CellBuilder::L(-HALF + 0.6f, P(g[1]), P(g[0]))) * rotZ(PI * 0.5f), glm::vec3(g[2], 0.4f, 0), 1, brass, g[3], 0, 0, (int)(g[2] * 5.0f));
}

void buildConveyor(CellBuilder& b, const Theme& t) {
    Material belt = mat(glm::vec3(0.12f, 0.12f, 0.13f), 0.3f, 24.0f), frame = mat(t.color, 0.4f, 24.0f), crate = mat(glm::vec3(0.62f, 0.45f, 0.28f), 0.1f, 8.0f, 0.0f, 2);
    for (const auto& r : ROUTE[TH_CONVEYOR]) routePlat(b, r, 2.4f, crate, frame, 0.8f);
    float L1 = CS - 4.0f, L2 = P(10.0f);
    b.box(CellBuilder::L(0, SLAB + 0.25f, 0), glm::vec3(L1, 0.5f, 2.2f), belt, true, glm::vec3(2.4f, 0, 0));
    b.box(CellBuilder::L(P(-6.0f), SLAB + 0.25f, P(7.5f)), glm::vec3(2.2f, 0.5f, L2), belt, true, glm::vec3(0, 0, -2.4f));
    for (int s = -1; s <= 1; s += 2) b.box(CellBuilder::L(0, SLAB + 0.2f, 1.18f * s), glm::vec3(L1, 0.4f, 0.16f), frame, false);
    for (int i = 0; i < 7; ++i) b.deco(D_SLIDE, at(CellBuilder::L(0, SLAB + 0.85f, 0)), glm::vec3(0.7f, 0.6f, 0.7f), 0, crate, 2.4f, L1 * 0.48f, i * 2.5f);
    for (int i = 0; i < 3; ++i) b.deco(D_SLIDE, at(CellBuilder::L(P(-6.0f), SLAB + 0.85f, P(7.5f))) * glm::rotate(glm::mat4(1.0f), PI * 0.5f, glm::vec3(0, 1, 0)), glm::vec3(0.7f, 0.6f, 0.7f), 0, crate, 2.4f, L2 * 0.48f, i * 2.4f);
    for (int i = 0; i < 2; ++i) {
        float x = i ? P(12.0f) : P(-12.0f);
        b.pillar(x, P(-12.0f), 0.25f, 0.25f, 3.2f, frame, false);
        b.deco(D_SPIN, at(CellBuilder::L(x, 3.4f, P(-12.0f))), glm::vec3(0.5f, 0.35f, 0.5f), 1, mat(t.accent, 0, 1, 3.0f), 3.0f);
    }
}

void buildPress(CellBuilder& b, const Theme& t) {
    Material body = mat(t.color, 0.5f, 32.0f), dark = mat(glm::vec3(0.16f, 0.15f, 0.15f), 0.5f, 32.0f), stripe = mat(t.accent, 0.1f, 8.0f, 0.4f);
    const auto& R = ROUTE[TH_PRESS];
    for (size_t i = 0; i < R.size(); ++i) {
        float x = R[i].x, z = R[i].z, h = R[i].h;
        b.pillar(x, z, 2.6f, 2.6f, h, i + 1 == R.size() ? body : dark);
        b.box(CellBuilder::L(x, h - 0.3f, z), glm::vec3(2.7f, 0.2f, 2.7f), stripe, false);
        if (i % 3 == 0 && i + 1 < R.size()) {
            for (int s = -1; s <= 1; s += 2) b.deco(D_STATIC, at(CellBuilder::L(x + 1.4f * s, h + 1.7f, z)), glm::vec3(0.3f, 3.4f, 0.5f), 0, body);
            b.deco(D_STATIC, at(CellBuilder::L(x, h + 3.5f, z)), glm::vec3(3.1f, 0.5f, 0.7f), 0, body);
            b.deco(D_PISTON, at(CellBuilder::L(x, h + 2.9f, z)), glm::vec3(1.9f, 0.5f, 1.9f), 0, dark, 0.4f, 2.2f, i * 0.3f);
        }
    }
    for (int i = 0; i < 2; ++i) {
        float x = i ? P(-10.0f) : 0.0f, z = i ? P(10.0f) : P(11.0f);
        b.pillar(x, z, 2.4f, 2.4f, 1.0f, dark);
        b.deco(D_PISTON, at(CellBuilder::L(x, 3.6f, z)), glm::vec3(1.8f, 0.5f, 1.8f), 0, body, 0.5f, 2.3f, i * 0.5f);
    }
}

void buildBoiler(CellBuilder& b, const Theme& t) {
    Material shell = mat(t.color, 0.7f, 40.0f), pipe = mat(glm::vec3(0.5f, 0.42f, 0.32f), 0.8f, 48.0f), fire = mat(t.accent, 0.0f, 1.0f, 4.0f);
    Material grate = mat(glm::vec3(0.2f, 0.18f, 0.16f), 0.6f, 32.0f), ring = mat(t.accent, 0.4f, 24.0f, 0.5f);
    b.lift(0.0f, CS);
    for (int i = -1; i <= 1; ++i) b.box(CellBuilder::L(i * 1.0f, 0.05f, 0), glm::vec3(0.12f, 0.1f, 3.2f), grate, false);
    for (int s = -1; s <= 1; s += 2) {
        b.box(CellBuilder::L(0, 0.15f, 2.1f * s), glm::vec3(4.4f, 0.1f, 0.2f), ring, false);
        b.box(CellBuilder::L(2.1f * s, 0.15f, 0), glm::vec3(0.2f, 0.1f, 4.0f), ring, false);
    }
    for (float h : { CS / 3.0f + 0.4f, CS * 2.0f / 3.0f + 0.4f })
        for (int k = 0; k < 4; ++k) {
            glm::vec2 d = k == 0 ? glm::vec2(3.6f, 0) : k == 1 ? glm::vec2(-3.6f, 0) : k == 2 ? glm::vec2(0, 3.6f) : glm::vec2(0, -3.6f);
            b.plat(d.x, d.y, 3.0f, 3.0f, h, pipe, 0.4f);
        }
    for (int k = 0; k < 4; ++k) {
        float x = k & 1 ? P(10.0f) : P(-10.0f), z = k & 2 ? P(10.0f) : P(-10.0f);
        b.box(CellBuilder::L(x, 2.9f, z), glm::vec3(2.8f, 5.0f, 2.8f), shell, true, glm::vec3(0.0f), false);
        b.deco(D_STATIC, at(CellBuilder::L(x, 2.9f, z)), glm::vec3(3.3f, 5.0f, 3.3f), 1, shell);
        b.deco(D_STATIC, at(CellBuilder::L(x, 5.6f, z)), glm::vec3(2.4f, 0.4f, 2.4f), 1, pipe);
        b.box(CellBuilder::L(x, 1.2f, z + (z < 0 ? 1.68f : -1.68f)), glm::vec3(1.0f, 0.8f, 0.1f), fire, false);
        b.deco(D_STATIC, at(CellBuilder::L(x, (5.8f + CS - 3.0f) * 0.5f, z)), glm::vec3(0.5f, CS - 8.8f, 0.5f), 1, pipe);
        vents.push_back({ b.C + b.R * CellBuilder::L(x, CS - 3.0f, z), b.R * glm::vec3(0, 1, 0), 0.0f, curChunk });
    }
}

void buildBench(CellBuilder& b, const Theme& t) {
    Material wood = mat(glm::vec3(0.55f, 0.38f, 0.22f), 0.15f, 12.0f, 0.0f, 2), metal = mat(glm::vec3(0.62f, 0.63f, 0.66f), 1.0f, 64.0f);
    Material lamp = mat(glm::vec3(1.0f, 0.88f, 0.6f), 0.0f, 1.0f, 3.5f), shade = mat(t.color, 0.4f, 24.0f);
    b.ladder(LADDER_XZ.x, LADDER_XZ.y, SLAB, CS + 1.4f);
    for (float h : { CS / 3.0f + 0.4f, CS * 2.0f / 3.0f + 0.4f }) b.plat(LADDER_XZ.x + 2.0f, LADDER_XZ.y + 1.4f, 2.4f, 2.4f, h, wood, 0.4f);
    float benches[3][4] = { { 0.0f, P(-10.0f), 5.0f, 1.6f }, { P(-10.0f), P(4.0f), 1.6f, 6.0f }, { P(5.0f), P(8.0f), 5.0f, 1.6f } };
    for (int k = 0; k < 3; ++k) {
        auto& bn = benches[k];
        b.pillar(bn[0], bn[1], bn[2], bn[3], 1.4f, wood);
        bool alongX = bn[2] > bn[3];
        for (int i = 0; i < 5; ++i) {
            float o = -2.0f + i * 1.0f;
            b.deco(D_SPIN, at(CellBuilder::L(bn[0] + (alongX ? o : 0.0f), 1.52f, bn[1] + (alongX ? 0.0f : o))), glm::vec3(0.28f, 0.22f, 0.28f), 0, metal, 6.0f + i, 0, (float)i);
        }
    }
    b.plat(P(-10.0f), P(-10.0f), 2.8f, 2.8f, 4.4f, wood, 0.4f);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) b.deco(D_STATIC, at(CellBuilder::L(P(-10.0f) + 1.2f * sx, 2.2f, P(-10.0f) + 1.2f * sz)), glm::vec3(0.15f, 3.6f, 0.15f), 0, wood);
    b.deco(D_HAND, at(CellBuilder::L(-HALF + 0.5f, P(9.0f), P(6.0f))) * rotZ(-PI * 0.5f), glm::vec3(2.4f, 0.15f, 0.45f), 0, metal, 0.6f, 0.35f, 1.0f);
    for (int i = 0; i < 3; ++i) {
        float x = P(-6.0f) + i * P(5.0f);
        b.deco(D_STATIC, at(CellBuilder::L(x, P(12.0f), P(4.0f))), glm::vec3(0.05f, P(6.0f), 0.05f), 0, metal);
        b.deco(D_STATIC, at(CellBuilder::L(x, P(9.0f) - 0.1f, P(4.0f))), glm::vec3(0.9f, 0.4f, 0.9f), 1, shade);
        b.deco(D_STATIC, at(CellBuilder::L(x, P(9.0f) - 0.35f, P(4.0f))), glm::vec3(0.5f, 0.12f, 0.5f), 1, lamp);
    }
}

void buildSupervisor(CellBuilder& b, const Theme& t) {
    Material frame = mat(glm::vec3(0.12f, 0.12f, 0.14f), 0.4f, 32.0f), screen = mat(t.accent, 0.0f, 1.0f, 1.6f), ink = mat(glm::vec3(0.05f));
    Material desk = mat(glm::vec3(0.4f, 0.24f, 0.14f), 0.3f, 24.0f, 0.0f, 2), cab = mat(t.color, 0.5f, 32.0f);
    for (const auto& r : ROUTE[TH_SUPERVISOR]) routePlat(b, r, 2.2f, cab, frame, 0.6f);
    float wx = -HALF + 0.25f;
    for (int k = -1; k <= 1; ++k) {
        float z = k * P(8.0f), y = P(14.0f) + (k == 0 ? P(4.0f) : 0.0f);
        b.box(CellBuilder::L(wx, y, z), glm::vec3(0.3f, 4.4f, 5.0f), frame, false);
        b.box(CellBuilder::L(wx + 0.17f, y, z), glm::vec3(0.06f, 3.8f, 4.4f), screen, false);
        b.box(CellBuilder::L(wx + 0.22f, y + 0.6f, z - 1.0f), glm::vec3(0.04f, 0.35f, 0.6f), ink, false);
        b.box(CellBuilder::L(wx + 0.22f, y + 0.6f, z + 1.0f), glm::vec3(0.04f, 0.35f, 0.6f), ink, false);
        b.box(CellBuilder::L(wx + 0.22f, y - 0.9f, z), glm::vec3(0.04f, 0.25f, 1.8f), ink, false);
    }
    for (int i = 0; i < 3; ++i) b.pillar(P(-6.0f) + i * P(6.0f), P(6.0f), 2.6f, 1.5f, 1.3f, desk);
}

void buildFeeding(CellBuilder& b, const Theme& t) {
    Material steel = mat(t.color, 0.8f, 48.0f), corn = mat(glm::vec3(0.98f, 0.82f, 0.25f), 0.3f, 16.0f, 0.2f), plate = mat(glm::vec3(0.92f, 0.9f, 0.86f), 0.6f, 32.0f);
    Material table = mat(glm::vec3(0.5f, 0.32f, 0.2f), 0.2f, 12.0f, 0.0f, 2);
    const float PAD_V = 17.3f;     // 다음 발판(+5.5)보다 1.6 정도 높이 튀어 오른다
    b.pad(PAD0_XZ.x, PAD0_XZ.y, 2.2f, 0.9f, PAD_V);
    const auto& R = ROUTE[TH_FEEDING];
    for (size_t i = 0; i < R.size(); ++i) {
        routePlat(b, R[i], 3.0f, steel, steel);
        if (i + 1 < R.size()) b.pad(R[i].x + 0.6f, R[i].z + 0.6f, 1.4f, R[i].h + 0.3f, PAD_V);
    }
    b.pillar(P(-11.0f), P(11.0f), 2.2f, 2.2f, 3.4f, steel);
    b.box(CellBuilder::L(P(-11.0f) + 1.4f, 3.0f, P(11.0f)), glm::vec3(1.0f, 0.3f, 0.3f), steel, false);
    b.deco(D_SPIN, at(CellBuilder::L(P(-11.0f) + 2.2f, 3.0f, P(11.0f))) * rotZ(PI * 0.5f), glm::vec3(0.35f, 1.2f, 0.35f), 1, corn, 9.0f);
    for (int i = 0; i < 2; ++i) {
        float z = P(-2.0f + i * 5.0f);
        b.pillar(P(-11.0f), z, 2.4f, 3.0f, 1.2f, table);
        b.deco(D_SPIN, at(CellBuilder::L(P(-11.0f), 1.25f, z)), glm::vec3(1.2f, 0.08f, 1.2f), 1, plate, 1.4f);
    }
}

void buildClock(CellBuilder& b, const Theme& t) {
    Material face = mat(glm::vec3(0.93f, 0.9f, 0.8f), 0.2f, 16.0f, 0.25f), ink = mat(glm::vec3(0.06f), 0.3f, 16.0f), rim = mat(t.color, 0.8f, 48.0f);
    Material locker = mat(t.color * 0.8f, 0.5f, 32.0f);
    for (const auto& r : ROUTE[TH_CLOCK]) b.pillar(r.x, r.z, 1.9f, 1.9f, r.h, locker);
    for (int k = 0; k < 2; ++k) {
        glm::mat4 wall = at(CellBuilder::L(k ? P(6.0f) : P(-5.0f), k ? P(20.0f) : P(9.0f), -HALF + 0.25f)) * rotX(PI * 0.5f);
        b.deco(D_STATIC, wall, glm::vec3(5.2f, 0.3f, 5.2f), 1, rim);
        b.deco(D_STATIC, wall * at(0, 0.1f, 0), glm::vec3(4.6f, 0.2f, 4.6f), 1, face);
        for (int i = 0; i < 12; ++i)
            b.deco(D_STATIC, wall * glm::rotate(glm::mat4(1.0f), i * PI / 6.0f, glm::vec3(0, 1, 0)) * at(1.95f, 0.22f, 0), glm::vec3(i % 3 == 0 ? 0.5f : 0.28f, 0.06f, 0.12f), 0, ink);
        b.deco(D_HAND, wall * at(0, 0.26f, 0), glm::vec3(1.9f, 0.05f, 0.14f), 0, ink, 1.2f + k, 0, 0);
        b.deco(D_HAND, wall * at(0, 0.3f, 0), glm::vec3(1.3f, 0.05f, 0.22f), 0, ink, 0.1f, 0, 1.0f);
    }
    for (int i = 0; i < 3; ++i) b.pillar(P(-10.0f) + i * 2.0f, P(10.0f), 1.2f, 0.8f, 1.9f, rim);
}

void buildStart(CellBuilder& b, const Theme& t) {
    Material green = mat(t.color, 0.4f, 24.0f), lamp = mat(t.accent, 0, 1, 2.5f), iron = mat(glm::vec3(0.2f), 0.5f, 32.0f);
    for (const auto& r : ROUTE[TH_START]) routePlat(b, r, 2.2f, green, iron, 0.6f);
    b.pillar(P(-9.0f), P(-2.0f), 0.4f, 0.4f, 4.6f, iron, false);
    b.pillar(P(-3.0f), P(-2.0f), 0.4f, 0.4f, 4.6f, iron, false);
    b.box(CellBuilder::L(P(-6.0f), 4.7f, P(-2.0f)), glm::vec3(P(6.0f) + 0.4f, 0.4f, 0.4f), green, false);
    b.deco(D_SPIN, at(CellBuilder::L(P(-6.0f), 5.2f, P(-2.0f))), glm::vec3(0.5f, 0.4f, 0.5f), 1, lamp, 2.0f);
}

void buildEnd(CellBuilder& b, const Theme& t, bool isGoal) {
    Material brass = mat(t.accent, 1.0f, 64.0f, 0.2f), iron = mat(glm::vec3(0.2f), 0.5f, 32.0f), glow = mat(glm::vec3(1.0f, 0.9f, 0.5f), 0, 1, 3.0f);
    float x = P(-10.0f), z = P(-10.0f);
    b.pillar(x, z, 1.8f, 1.8f, 1.2f, iron);
    b.deco(D_STATIC, at(CellBuilder::L(x, 4.0f, z)), glm::vec3(0.5f, 5.6f, 0.5f), 1, brass);
    b.deco(D_STATIC, at(CellBuilder::L(x, 7.2f, z)), glm::vec3(1.0f, 1.0f, 1.0f), 1, brass);
    b.deco(D_STATIC, at(CellBuilder::L(x, 7.9f, z)), glm::vec3(1.4f, 0.3f, 1.4f), 1, brass);
    b.deco(D_SPIN, at(CellBuilder::L(x, 2.6f, z)) * rotZ(PI * 0.5f), glm::vec3(0.3f, 1.4f, 0.3f), 1, glow, isGoal ? 1.0f : 0.0f);
    if (isGoal) goalPos = b.C + b.R * CellBuilder::L(x, 2.2f, z);
}

// 블럭 공통: 타일 바닥(구멍 포함) + 가장자리 띠 + 컨셉 맵 + 바닥 장애물
void buildCell(int theme, const glm::mat3& R, glm::vec3 C, bool isGoal, unsigned seed) {
    CellBuilder b{ R, C };
    const Theme& t = THEMES[theme];
    Material floorM = mat(t.floor, 0.25f, 24.0f, 0.0f, 1), trim = mat(t.color, 0.3f, 24.0f, 0.2f);
    float e = HALF - 0.15f;
    for (int tz = 0; tz < 3; ++tz)
        for (int tx = 0; tx < 3; ++tx) {
            if (t.fl[tz][tx] != 'X') continue;
            float x = -TILE + TILE * tx, z = -TILE + TILE * tz;
            b.box(CellBuilder::L(x, SLAB * 0.5f, z), glm::vec3(TILE, SLAB, TILE), floorM);
            if (tx == 0) b.box(CellBuilder::L(-e, SLAB + 0.03f, z), glm::vec3(0.3f, 0.06f, TILE), trim, false);
            if (tx == 2) b.box(CellBuilder::L(e, SLAB + 0.03f, z), glm::vec3(0.3f, 0.06f, TILE), trim, false);
            if (tz == 0) b.box(CellBuilder::L(x, SLAB + 0.03f, -e), glm::vec3(TILE, 0.06f, 0.3f), trim, false);
            if (tz == 2) b.box(CellBuilder::L(x, SLAB + 0.03f, e), glm::vec3(TILE, 0.06f, 0.3f), trim, false);
        }
    switch (theme) {
    case TH_GEAR: buildGear(b, t); break;
    case TH_CONVEYOR: buildConveyor(b, t); break;
    case TH_PRESS: buildPress(b, t); break;
    case TH_BOILER: buildBoiler(b, t); break;
    case TH_BENCH: buildBench(b, t); break;
    case TH_SUPERVISOR: buildSupervisor(b, t); break;
    case TH_FEEDING: buildFeeding(b, t); break;
    case TH_CLOCK: buildClock(b, t); break;
    case TH_START: buildStart(b, t); break;
    case TH_END: buildEnd(b, t, isGoal); break;
    }
    std::mt19937 r(seed * 2654435761u + (unsigned)theme);
    addClutter(b, theme, r, theme >= TH_START ? 2 : 4);
}

// 다단블럭 껍데기: 옛 시작/도착 칸의 바깥 면은 열려 있고(위/아래 포함), 나머지 바깥 면은 벽
void buildShell(int pi) {
    const Piece& p = pieces[pi];
    auto sc = shapeCells(p, p.rot);
    Material plate = mat(glm::vec3(0.36f, 0.34f, 0.32f), 0.7f, 40.0f, 0.0f, 2), trim = mat(glm::vec3(0.75f, 0.6f, 0.3f), 1.0f, 64.0f, 0.15f);
    Material door = mat(glm::vec3(0.4f, 1.0f, 0.5f), 0, 1, 2.0f);
    auto inPiece = [&](glm::ivec3 g) { for (auto s2 : sc) if (p.pos + s2 == g) return true; return false; };
    for (size_t k = 0; k < sc.size(); ++k) {
        glm::ivec3 g = p.pos + sc[k];
        bool isDoor = p.cells[k].role != 0;
        glm::vec3 cw = cellWorldCenter(g);
        for (int d = 0; d < 6; ++d) {
            glm::ivec3 n = g + NB[d];
            if (!inGrid(n) || inPiece(n)) continue;
            glm::vec3 dv(NB[d]), ad = glm::abs(dv), in = glm::vec3(1.0f) - ad;
            if (isDoor) {
                // 열린 문: 가장자리에 초록 띠만 둘러서 입구/출구임을 알린다
                for (int e = 0; e < 3; ++e) {
                    if (ad[e] > 0.5f) continue;
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        glm::vec3 off(0.0f), sz = ad * 0.3f + in * CS;
                        off[e] = sgn * (HALF - 0.2f);
                        sz[e] = 0.3f;
                        addBox(cw + dv * (HALF - 0.15f) + off, sz, door, false);
                    }
                }
                continue;
            }
            addBox(cw + dv * (HALF - 0.2f), ad * 0.4f + in * CS, plate);
            for (int e = 0; e < 3; ++e) {
                if (ad[e] > 0.5f) continue;
                glm::vec3 sz = ad * 0.2f + in * 0.5f;
                sz[e] = CS;
                addBox(cw + dv * (HALF + 0.05f), sz, trim, false);
            }
        }
    }
}

// 기둥 소켓: 꽂혀 있으면 기둥(밟을 수 있음), 비어 있으면 노란 받침
void buildSockets(int pi) {
    const Piece& p = pieces[pi];
    Material steel = mat(glm::vec3(0.3f, 0.42f, 0.62f), 0.8f, 48.0f, 0.05f, 2), cap = mat(glm::vec3(1.0f, 0.8f, 0.3f), 1.0f, 64.0f, 0.4f);
    Material ring = mat(glm::vec3(1.0f, 0.78f, 0.15f), 0.3f, 16.0f, 1.2f);
    glm::vec3 c = pieceCenter(p), up = p.rot * glm::vec3(0, 1, 0);
    for (const auto& so : p.sockets) {
        glm::vec3 base = c + p.rot * so.local;
        if (so.filled) {
            addBox(base + up * (PILLAR_H * 0.5f), glm::abs(p.rot * glm::vec3(PILLAR_W, PILLAR_H, PILLAR_W)), steel);
            addBox(base + up * (PILLAR_H + 0.06f), glm::abs(p.rot * glm::vec3(PILLAR_W + 0.3f, 0.12f, PILLAR_W + 0.3f)), cap, false);
        } else {
            addBox(base + up * 0.06f, glm::abs(p.rot * glm::vec3(PILLAR_W + 0.6f, 0.12f, PILLAR_W + 0.6f)), ring, false);
        }
    }
}

void buildPiece(int pi) {
    const Piece& p = pieces[pi];
    curChunk = pi;
    auto sc = shapeCells(p, p.rot);
    for (size_t k = 0; k < sc.size(); ++k)
        buildCell(p.cells[k].theme, contentRot(p.rot, p.cells[k]), cellWorldCenter(p.pos + sc[k]), false, p.cells[k].seed);
    if (p.custom) buildShell(pi);
    buildSockets(pi);
    curChunk = -1;
}

void computeVoidCells(const std::vector<int>* alsoBusy = nullptr) {
    voidCells.clear();
    for (int i = 0; i < N * N * N; ++i)
        if (occ[i] == OCC_EMPTY && !(alsoBusy && (*alsoBusy)[i] != OCC_EMPTY)) voidCells.push_back(i);
}

void buildWorld() {
    boxes.clear(); decos.clear(); shafts.clear(); ladders.clear(); vents.clear();
    curChunk = -1;
    float W = N * CS, H = N * CS + 2.0f;
    Material ground = mat(glm::vec3(0.3f, 0.28f, 0.26f), 0.15f, 16.0f, 0.0f, 1), brick = mat(glm::vec3(0.46f, 0.28f, 0.22f), 0.1f, 12.0f, 0.0f, 2);
    addBox({ 0, -0.5f, 0 }, { W + 120.0f, 1.0f, W + 120.0f }, ground);
    // 씬 외면 = 공장 외벽 (지붕은 없음)
    addBox({ 0, H * 0.5f, W * 0.5f + 0.5f }, { W + 2.0f, H, 1.0f }, brick);
    addBox({ 0, H * 0.5f, -W * 0.5f - 0.5f }, { W + 2.0f, H, 1.0f }, brick);
    addBox({ W * 0.5f + 0.5f, H * 0.5f, 0 }, { 1.0f, H, W }, brick);
    addBox({ -W * 0.5f - 0.5f, H * 0.5f, 0 }, { 1.0f, H, W }, brick);
    {
        Material wood = mat(glm::vec3(0.45f, 0.33f, 0.2f), 0.1f, 8.0f, 0.0f, 2), iron = mat(glm::vec3(0.15f), 0.6f, 32.0f), lampM = mat(glm::vec3(1.0f, 0.95f, 0.8f), 0, 1, 4.0f);
        auto beam = [&](glm::vec3 a, glm::vec3 b, float t, const Material& m) { decos.push_back({ beamMat(a, b), D_STATIC, 0, 0, glm::vec3(t, glm::length(b - a), t), m, 0, 0, 0, -1 }); };
        for (int side = 0; side < 4; ++side) {
            glm::vec3 n = side == 0 ? glm::vec3(1, 0, 0) : side == 1 ? glm::vec3(-1, 0, 0) : side == 2 ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
            glm::vec3 tg(n.z, 0, -n.x);
            for (float s = -W * 0.5f + CS * 0.5f; s < W * 0.5f; s += CS * 0.5f)
                beam(n * (W * 0.5f + 1.0f + H * 0.4f) + tg * s, n * (W * 0.5f + 1.0f) + tg * s + glm::vec3(0, H * 0.7f, 0), 0.35f, wood);
        }
        for (int k = 0; k < 4; ++k) {
            float sx = k & 1 ? 1.0f : -1.0f, sz = k & 2 ? 1.0f : -1.0f;
            glm::vec3 base(sx * (W * 0.5f + 10.0f), 0, sz * (W * 0.5f + 10.0f)), top = base + glm::vec3(0, H + 6.0f, 0);
            beam(base, top, 0.4f, iron);
            for (int l = 0; l < 3; ++l) { float a = l * 2.094f; beam(base + glm::vec3(cosf(a) * 2.0f, 0, sinf(a) * 2.0f), base + glm::vec3(0, 3.0f, 0), 0.15f, iron); }
            glm::vec3 toC = glm::normalize(glm::vec3(0, N * CS * 0.4f, 0) - top);
            decos.push_back({ beamMat(top, top + toC * 1.6f), D_STATIC, 0, 0, glm::vec3(1.6f, 1.6f, 1.6f), iron, 0, 0, 0, -1 });
            decos.push_back({ beamMat(top + toC * 0.9f, top + toC * 1.0f), D_STATIC, 0, 0, glm::vec3(1.3f, 0.1f, 1.3f), lampM, 0, 0, 0, -1 });
        }
        for (int k = -1; k <= 1; k += 2) {
            glm::vec3 base(k * W * 0.25f, 0, -W * 0.5f - 9.0f), head = base + glm::vec3(0, 3.2f, 0);
            for (int l = 0; l < 3; ++l) { float a = l * 2.094f + 0.5f; beam(base + glm::vec3(cosf(a) * 1.2f, 0, sinf(a) * 1.2f), head, 0.1f, wood); }
            decos.push_back({ glm::translate(glm::mat4(1.0f), head + glm::vec3(0, 0.6f, 0)), D_STATIC, 0, 0, glm::vec3(1.0f, 1.0f, 1.6f), iron, 0, 0, 0, -1 });
            decos.push_back({ glm::translate(glm::mat4(1.0f), head + glm::vec3(0, 0.6f, 1.0f)) * rotX(PI * 0.5f), D_STATIC, 1, 0, glm::vec3(0.6f, 0.6f, 0.6f), iron, 0, 0, 0, -1 });
            for (int r = -1; r <= 1; r += 2) decos.push_back({ glm::translate(glm::mat4(1.0f), head + glm::vec3(0, 1.5f, r * 0.45f)) * rotZ(PI * 0.5f), D_STATIC, 1, 0, glm::vec3(0.8f, 0.15f, 0.8f), iron, 0, 0, 0, -1 });
        }
        glm::vec3 cb = glm::vec3(W * 0.5f + 7.0f, 0.0f, -W * 0.5f - 7.0f);
        (void)cb;
    }
    int P = (int)pieces.size();
    curChunk = P;
    buildCell(TH_START, glm::mat3(1.0f), cellWorldCenter(startCell), false, 11u);
    curChunk = P + 1;
    buildCell(TH_END, glm::mat3(1.0f), cellWorldCenter(endCell), true, 23u);
    curChunk = -1;
    for (int p = 0; p < P; ++p)
        if (pieces[p].placed) buildPiece(p);
    computeVoidCells();
}

// ===================================================================
// 충돌 (축 정렬 상자 + 원기둥 캐릭터)
// ===================================================================
bool segAABB(glm::vec3 p0, glm::vec3 p1, glm::vec3 bmin, glm::vec3 bmax, float& tHit) {
    glm::vec3 d = p1 - p0;
    float tmin = 0.0f, tmax = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-8f) {
            if (p0[i] < bmin[i] || p0[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (bmin[i] - p0[i]) * inv, t2 = (bmax[i] - p0[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tHit = tmin;
    return true;
}

bool segmentBlocked(glm::vec3 p0, glm::vec3 p1, float* tOut = nullptr) {
    float best = 2.0f;
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        float t;
        if (segAABB(p0, p1, b.bmin(), b.bmax(), t) && t < best) best = t;
    }
    if (tOut) *tOut = best;
    return best <= 1.0f;
}

void pushOutCircle(glm::vec3& p, float r, const Box& b) {
    glm::vec3 mn = b.bmin(), mx = b.bmax();
    float cx = glm::clamp(p.x, mn.x, mx.x), cz = glm::clamp(p.z, mn.z, mx.z);
    float dx = p.x - cx, dz = p.z - cz;
    float d2 = dx * dx + dz * dz;
    if (d2 > r * r) return;
    if (d2 > 1e-8f) {
        float d = sqrtf(d2);
        p.x += dx / d * (r - d);
        p.z += dz / d * (r - d);
    } else {
        float l = p.x - mn.x, rr = mx.x - p.x, bk = p.z - mn.z, f = mx.z - p.z;
        float m = std::min({ l, rr, bk, f });
        if (m == l) p.x = mn.x - r;
        else if (m == rr) p.x = mx.x + r;
        else if (m == bk) p.z = mn.z - r;
        else p.z = mx.z + r;
    }
}

bool circleOverlapsBoxXZ(glm::vec3 p, float r, const Box& b) {
    glm::vec3 mn = b.bmin(), mx = b.bmax();
    float cx = glm::clamp(p.x, mn.x, mx.x), cz = glm::clamp(p.z, mn.z, mx.z);
    return (p.x - cx) * (p.x - cx) + (p.z - cz) * (p.z - cz) < r * r;
}

float groundHeightAt(glm::vec3 p, float r, int* boxIdx = nullptr) {
    float ground = -1000.0f;
    int bi = -1;
    for (int i = 0; i < (int)boxes.size(); ++i) {
        const Box& b = boxes[i];
        if (!b.solid) continue;
        float top = b.bmax().y;
        if (top <= p.y + STEP_H && top > ground && circleOverlapsBoxXZ(p, r, b)) { ground = top; bi = i; }
    }
    if (boxIdx) *boxIdx = bi;
    return ground;
}

bool inAABB(glm::vec3 p, glm::vec3 mn, glm::vec3 mx) { return p.x >= mn.x && p.y >= mn.y && p.z >= mn.z && p.x <= mx.x && p.y <= mx.y && p.z <= mx.z; }

glm::ivec3 worldToCell(glm::vec3 p) {
    return glm::ivec3((int)floorf(p.x / CS + N * 0.5f), (int)floorf(p.y / CS), (int)floorf(p.z / CS + N * 0.5f));
}

// ===================================================================
// 플레이어 (TPS, 공중 2단 점프)
// ===================================================================
struct Player {
    glm::vec3 pos, vel;
    float bodyYaw, walkT, landDip, landTimer;
    bool onGround, inLift, onLadder, prevSpace;
    int groundBox, jumpChain;
} pl;

// 떨어지기 직전 발판: 조각 기준 좌표로 기억해서, 그 조각이 옮겨져도 같은 자리로 돌아간다
struct SafeSpot { int chunk = -2; glm::vec3 local = glm::vec3(0.0f); } lastSafe;

float camYaw = 0.0f, camPitch = -0.3f, camDist = 6.0f;
float globalTime = 0.0f, fps = 0.0f, flashT = 0.0f, deathT = 0.0f, helpT = 12.0f, clearT = 0.0f, toastT = 0.0f;
int deaths = 0;
std::string toast;
bool filmOn = false;          // 필름 효과는 쓰지 않는다
bool inHome = false;            // 찰리의 방 (1인칭 홈)
GLFWwindow* gWindow = nullptr;

void showToast(const std::string& s) { toast = s; toastT = 3.0f; }

struct FakeKeys { bool on = false, w = false, space = false, ctrl = false; } fake;
bool keyDown(int k) {
    if (fake.on) {
        if (k == GLFW_KEY_W) return fake.w;
        if (k == GLFW_KEY_SPACE) return fake.space;
        if (k == GLFW_KEY_LEFT_CONTROL) return fake.ctrl;
        return false;
    }
    return glfwGetKey(gWindow, k) == GLFW_PRESS;
}

glm::mat4 chunkXform(int ch) {
    if (ch >= 0 && ch < (int)pieces.size()) return pieceXform(pieces[ch]);
    return glm::mat4(1.0f);
}

void spawnAtStart() {
    glm::vec3 c = cellWorldCenter(startCell);
    pl.pos = c + glm::vec3(-4.2f, -HALF + SLAB, -4.2f);
    pl.vel = glm::vec3(0.0f);
    pl.onGround = false;
    camYaw = PI * 0.75f;
    camPitch = -0.25f;
}

void resetPlayer() {
    pl = Player{};
    spawnAtStart();
    lastSafe = SafeSpot{};
}

struct Spark { glm::vec3 pos, vel, color; float life; };
std::vector<Spark> sparks;
void spawnSparks(glm::vec3 p, glm::vec3 color, int n, float speed);

void respawn() {
    bool ok = false;
    if (lastSafe.chunk != -2) {
        int ch = lastSafe.chunk;
        bool alive = ch < 0 || ch >= (int)pieces.size() || pieces[ch].placed;
        if (alive) {
            pl.pos = glm::vec3(chunkXform(ch) * glm::vec4(lastSafe.local, 1.0f));
            glm::ivec3 g = worldToCell(pl.pos + glm::vec3(0, 0.9f, 0));
            ok = !inGrid(g) || occ[idxOf(g)] != OCC_EMPTY;
        }
    }
    if (!ok) spawnAtStart();
    pl.vel = glm::vec3(0.0f);
    pl.onGround = false;
    pl.jumpChain = 0;
}

void die() {
    ++deaths;
    deathT = 1.0f;
    respawn();
}

void updatePlayer(float dt) {
    pl.landDip = std::max(0.0f, pl.landDip - dt * 1.2f);
    pl.landTimer += dt;
    glm::vec3 f(sinf(camYaw), 0, -cosf(camYaw)), r(cosf(camYaw), 0, sinf(camYaw));
    glm::vec3 wish(0.0f);
    bool fwdKey = keyDown(GLFW_KEY_W), backKey = keyDown(GLFW_KEY_S);
    if (fwdKey) wish += f;
    if (backKey) wish -= f;
    if (keyDown(GLFW_KEY_D)) wish += r;
    if (keyDown(GLFW_KEY_A)) wish -= r;
    bool space = keyDown(GLFW_KEY_SPACE), crouch = keyDown(GLFW_KEY_LEFT_CONTROL) || keyDown(GLFW_KEY_C);
    bool spacePressed = space && !pl.prevSpace;
    pl.prevSpace = space;
    float speed = keyDown(GLFW_KEY_LEFT_SHIFT) ? 9.0f : 6.0f;
    if (glm::length(wish) > 0.01f) {
        wish = glm::normalize(wish);
        float target = atan2f(wish.x, wish.z), diff = target - pl.bodyYaw;
        while (diff > PI) diff -= 2 * PI;
        while (diff < -PI) diff += 2 * PI;
        pl.bodyYaw += diff * std::min(1.0f, dt * 14.0f);
        wish *= speed;
    }
    glm::vec3 mid = pl.pos + glm::vec3(0, PLAYER_H * 0.5f, 0);

    // 사다리
    bool wasLadder = pl.onLadder;
    pl.onLadder = false;
    for (const auto& l : ladders) if (inAABB(mid, l.mn, l.mx)) pl.onLadder = true;
    if (wasLadder && !pl.onLadder && pl.vel.y > 0.0f) pl.vel.y = 5.0f;     // 사다리 꼭대기에서 올라서기

    float accel = pl.onGround ? 16.0f : 6.0f;
    glm::vec2 hv(pl.vel.x, pl.vel.z), target(wish.x, wish.z);
    if (pl.onLadder) target *= 0.5f;
    hv += (target - hv) * std::min(1.0f, accel * dt);
    pl.vel.x = hv.x;
    pl.vel.z = hv.y;
    if (pl.onGround && glm::length(hv) > 0.5f) pl.walkT += dt * glm::length(hv) * 0.9f;

    // 증기 승강기
    pl.inLift = false;
    for (const auto& s : shafts) {
        if (!inAABB(mid, s.mn, s.mx)) continue;
        if (s.dir.y > 0.5f) pl.inLift = true;
        else if (glm::dot(pl.vel, s.dir) < 7.0f) pl.vel += s.dir * 26.0f * dt;
    }

    if (pl.onLadder) {
        pl.vel.y = (fwdKey || space) ? 5.0f : backKey ? -5.0f : 0.0f;
    } else if (pl.inLift && !crouch) {
        float tv = space ? 8.0f : 1.4f;
        pl.vel.y += (tv - pl.vel.y) * std::min(1.0f, dt * 5.0f);
    } else {
        if (spacePressed && (pl.onGround || pl.jumpChain < 2)) {
            if (pl.onGround) { pl.jumpChain = 1; pl.vel.y = JUMP_V; }
            else { pl.jumpChain = 2; pl.vel.y = AIR_JUMP_V; spawnSparks(pl.pos, glm::vec3(0.9f, 0.9f, 0.85f), 6, 2.0f); }
            pl.onGround = false;
        }
        pl.vel.y = std::max(pl.vel.y - GRAVITY * dt, -32.0f);
    }

    // 세로 이동 + 천장
    float oldY = pl.pos.y;
    pl.pos.y += pl.vel.y * dt;
    if (pl.vel.y > 0.0f)
        for (const auto& b : boxes) {
            if (!b.solid) continue;
            float by = b.bmin().y;
            if (by >= oldY + STEP_H - 0.001f && by < pl.pos.y + PLAYER_H && circleOverlapsBoxXZ(pl.pos, PLAYER_R * 0.9f, b)) {
                pl.pos.y = by - PLAYER_H;
                pl.vel.y = 0.0f;
            }
        }
    // 가로 이동 + 벽
    pl.pos.x += pl.vel.x * dt;
    pl.pos.z += pl.vel.z * dt;
    for (const auto& b : boxes) {
        if (!b.solid) continue;
        if (b.bmax().y > pl.pos.y + STEP_H && b.bmin().y < pl.pos.y + PLAYER_H - 0.02f) pushOutCircle(pl.pos, PLAYER_R, b);
    }
    // 바닥
    int gb = -1;
    float ground = groundHeightAt(pl.pos, PLAYER_R * 0.7f, &gb);
    bool was = pl.onGround;
    if (!pl.onLadder && (pl.pos.y <= ground + 0.001f || (was && pl.vel.y <= 0.0f && pl.pos.y - ground < 0.25f))) {
        if (!was) { if (pl.vel.y < -9.0f) pl.landDip = 0.2f; pl.landTimer = 0.0f; }
        pl.jumpChain = 0;
        pl.pos.y = ground;
        pl.vel.y = 0.0f;
        pl.onGround = true;
        pl.groundBox = gb;
    } else if (pl.onLadder && pl.pos.y < ground) {
        pl.pos.y = ground;
        pl.onGround = false;
        pl.groundBox = -1;
    } else {
        pl.onGround = false;
        pl.groundBox = -1;
    }
    if (pl.onGround && gb >= 0) {
        const Box& g = boxes[gb];
        if (g.bounce > 0.0f) {
            pl.vel.y = g.bounce;
            pl.onGround = false;
            pl.jumpChain = 0;
            spawnSparks(pl.pos, glm::vec3(1.0f, 0.6f, 0.4f), 8, 3.0f);
        } else {
            pl.pos += g.belt * dt;
            // 떨어지기 직전 위치로 쓸 발판 기억
            glm::ivec3 cell = worldToCell(pl.pos + glm::vec3(0, 0.1f, 0));
            if (glm::length(g.belt) < 0.01f && (!inGrid(cell) || occ[idxOf(cell)] != OCC_EMPTY)) {
                lastSafe.chunk = g.chunk;
                lastSafe.local = glm::vec3(glm::inverse(chunkXform(g.chunk)) * glm::vec4(pl.pos, 1.0f));
            }
        }
    }

    if (inHome) return;
    // 어둠(빈 칸)에 들어가면 사망
    mid = pl.pos + glm::vec3(0, PLAYER_H * 0.5f, 0);
    glm::ivec3 mc = worldToCell(mid);
    if ((inGrid(mc) && occ[idxOf(mc)] == OCC_EMPTY) || pl.pos.y < -10.0f) { die(); return; }

    // 줍기
    for (auto& p : pickups) {
        if (p.taken || glm::length(p.pos - mid) > 1.5f) continue;
        p.taken = true;
        flashT = 1.0f;
        if (p.type == 0) {
            spawnSparks(p.pos, glm::vec3(1.0f, 0.8f, 0.3f), 30, 4.0f);
            showToast("COG " + std::to_string(cogsTaken()) + "/" + std::to_string(cogsTotal()));
        } else {
            hasSpanner = true;
            spawnSparks(p.pos, glm::vec3(0.5f, 0.8f, 1.0f), 30, 4.0f);
            showToast("GOT SUSPENDERS - SPANNER READY (E TO PULL PILLARS)");
        }
    }
    // 퇴근 기적
    if (!stageClear && glm::length(goalPos - mid) < 2.4f) {
        if (cogsTaken() < cogsTotal()) { if (toastT <= 0.0f) showToast("COLLECT EVERY COG FIRST"); }
        else if (!gridComplete()) { if (toastT <= 0.0f) showToast("FILL EVERY CELL OF THE SCENE FIRST"); }
        else { stageClear = true; clearT = 0.0f; flashT = 1.0f; spawnSparks(goalPos, glm::vec3(1.0f, 0.9f, 0.5f), 60, 6.0f); }
    }
}

// 가까운 소켓 찾기 (filled: 기둥이 꽂힌 것 / 빈 것)
bool nearestSocket(bool filled, int& outPiece, int& outSock) {
    glm::vec3 mid = pl.pos + glm::vec3(0, PLAYER_H * 0.5f, 0);
    float bd = 2.8f;
    bool found = false;
    for (int pi = 0; pi < (int)pieces.size(); ++pi) {
        const Piece& p = pieces[pi];
        if (!p.placed || p.sockets.empty()) continue;
        glm::vec3 c = pieceCenter(p), up = p.rot * glm::vec3(0, 1, 0);
        for (int si = 0; si < (int)p.sockets.size(); ++si) {
            if (p.sockets[si].filled != filled) continue;
            glm::vec3 base = c + p.rot * p.sockets[si].local;
            glm::vec3 a = base + up * 0.3f, b2 = base + up * (filled ? PILLAR_H : 1.0f);
            glm::vec3 ab = b2 - a;
            float t = glm::clamp(glm::dot(mid - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            float d = glm::length(a + ab * t - mid);
            if (d < bd) { bd = d; outPiece = pi; outSock = si; found = true; }
        }
    }
    return found;
}

void buildWorld();

void tryPillar() {
    int pi, si;
    if (carrying) {
        if (!nearestSocket(false, pi, si)) { showToast("SET THE PILLAR ON A YELLOW SOCKET"); return; }
        pieces[pi].sockets[si].filled = true;
        carrying = false;
        showToast("PILLAR SET");
        buildWorld();
        return;
    }
    if (!nearestSocket(true, pi, si)) return;
    if (!hasSpanner) { showToast("BOLTED PILLAR - FIND THE SUSPENDERS IN THIS SCENE"); return; }
    const Piece& p = pieces[pi];
    spawnSparks(pieceCenter(p) + p.rot * p.sockets[si].local + glm::vec3(0, 1.0f, 0), glm::vec3(1.0f, 0.75f, 0.35f), 14, 3.0f);
    pieces[pi].sockets[si].filled = false;
    carrying = true;
    showToast("PILLAR PULLED - SET IT ON A YELLOW SOCKET");
    buildWorld();
}

// ===================================================================
// 효과
// ===================================================================
struct Puff { glm::vec3 pos, vel; float life, size; };
std::vector<Puff> puffs;

void spawnSparks(glm::vec3 p, glm::vec3 color, int n, float speed) {
    for (int i = 0; i < n; ++i)
        sparks.push_back({ p, glm::vec3(frand(-1, 1), frand(0, 1.5f), frand(-1, 1)) * speed, color, frand(0.4f, 0.9f) });
}

void updateEffects(float dt) {
    flashT = std::max(0.0f, flashT - dt * 2.5f);
    deathT = std::max(0.0f, deathT - dt * 1.5f);
    for (auto& v : vents) {
        if (glm::length(v.pos - pl.pos) > 30.0f) continue;
        v.acc += dt * 9.0f;
        while (v.acc > 1.0f) {
            v.acc -= 1.0f;
            glm::vec3 j(frand(-1.2f, 1.2f), frand(-1.2f, 1.2f), frand(-1.2f, 1.2f));
            j -= v.dir * glm::dot(j, v.dir);
            puffs.push_back({ v.pos + j, v.dir * frand(3.0f, 6.0f) + j * 0.3f, frand(1.2f, 2.0f), frand(0.25f, 0.5f) });
        }
    }
    for (auto& p : puffs) { p.life -= dt; p.pos += p.vel * dt; p.vel *= 1.0f - dt * 0.6f; p.size += dt * 0.5f; }
    puffs.erase(std::remove_if(puffs.begin(), puffs.end(), [](const Puff& p) { return p.life <= 0.0f; }), puffs.end());
    if (puffs.size() > 600) puffs.erase(puffs.begin(), puffs.begin() + (puffs.size() - 600));
    for (auto& s : sparks) { s.life -= dt; s.vel.y -= 9.8f * dt; s.pos += s.vel * dt; }
    sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& s) { return s.life <= 0.0f; }), sparks.end());
}

// ===================================================================
// 재배치 연출: 편집에서 돌아오면 씬 안에서 조각들이 실제로 빠지고, 움직이고, 날아와 끼워진다
//   새 배치로 지은 지오메트리 G 는 world = Xnew * G 이므로, 그리는 행렬을 A(t) * inverse(Xnew) 로 바꾸면
//   A(0) = Xold 일 때 옛 자리, A(1) = Xnew 일 때 새 자리에 보인다
// ===================================================================
struct Snap { bool placed; glm::ivec3 pos; glm::mat3 rot; };
std::vector<Snap> editSnap;

enum { EV_REMOVE, EV_MOVE, EV_ADD };
struct TrEvent {
    int piece, type;
    float t0, dur;
    glm::vec3 cOld, cNew, dirOut, axis;
    glm::quat qOld, qNew;
    glm::mat4 invNew, invOld;
};
std::vector<TrEvent> trEvents;
bool transitioning = false;
float trT = 0.0f, trTotal = 0.0f, camDistBoost = 0.0f;
std::vector<glm::mat4> chunkM;          // 조각별 그리기 행렬 (평소엔 단위 행렬)
std::vector<Box> ghostBoxes;            // 빠지는 조각의 옛 지오메트리
std::vector<Deco> ghostDecos;
std::vector<glm::mat4> ghostM;
std::vector<int> oldOcc;
int riderPiece = -1;
bool riderOnGhost = false;
glm::vec3 riderLocal(0.0f);

glm::quat toQuat(const glm::mat3& m) { return glm::normalize(glm::quat_cast(m)); }

void snapshotForEdit() {
    editSnap.clear();
    for (auto& p : pieces) editSnap.push_back({ p.placed, p.pos, p.rot });
    oldOcc = occ;
}

void startTransition() {
    int P = (int)pieces.size();
    trEvents.clear();
    ghostBoxes.clear();
    ghostDecos.clear();
    riderPiece = -1;
    riderOnGhost = false;
    glm::vec3 sceneC(0, N * CS * 0.5f, 0);

    // 플레이어가 서 있던 조각 (옛 배치 기준)
    glm::ivec3 pc = worldToCell(pl.pos + glm::vec3(0, 0.5f, 0));
    int standPiece = inGrid(pc) ? oldOcc[idxOf(pc)] : -1;

    // 빠지는 조각은 옛 배치로 지어서 따로 보관
    boxes.clear(); decos.clear();
    for (int p = 0; p < P; ++p) {
        const Snap& s = editSnap[p];
        if (!s.placed || pieces[p].placed) continue;
        Piece cur = pieces[p];
        pieces[p].placed = true; pieces[p].pos = s.pos; pieces[p].rot = s.rot;
        buildPiece(p);
        pieces[p] = cur;
    }
    ghostBoxes = boxes;
    ghostDecos = decos;

    float tMove = 0.0f, tAdd;
    bool anyRemove = false;
    for (int p = 0; p < P; ++p) {
        const Snap& s = editSnap[p];
        Piece& cur = pieces[p];
        Piece old = cur;
        old.placed = s.placed; old.pos = s.pos; old.rot = s.rot;
        TrEvent e{};
        e.piece = p;
        e.axis = glm::normalize(glm::vec3(frand(-1, 1), frand(-1, 1), frand(-1, 1)) + glm::vec3(0.01f));
        if (s.placed) { e.cOld = pieceCenter(old); e.qOld = toQuat(old.rot); e.invOld = glm::inverse(pieceXform(old)); }
        if (cur.placed) { e.cNew = pieceCenter(cur); e.qNew = toQuat(cur.rot); e.invNew = glm::inverse(pieceXform(cur)); }
        glm::vec3 c = cur.placed ? e.cNew : e.cOld;
        e.dirOut = glm::length(c - sceneC) > 0.1f ? glm::normalize(c - sceneC) : glm::vec3(0, -1, 0);
        if (s.placed && !cur.placed) { e.type = EV_REMOVE; e.t0 = 0.0f; e.dur = 0.8f; anyRemove = true; }
        else if (!s.placed && cur.placed) { e.type = EV_ADD; }
        else if (s.placed && cur.placed && (s.pos != cur.pos || s.rot != cur.rot)) { e.type = EV_MOVE; }
        else continue;
        trEvents.push_back(e);
        if (p == standPiece) {
            riderPiece = (int)trEvents.size() - 1;
            riderLocal = glm::vec3((s.placed ? glm::inverse(pieceXform(old)) : glm::mat4(1.0f)) * glm::vec4(pl.pos, 1.0f));
            riderOnGhost = e.type == EV_REMOVE;
        }
    }
    if (trEvents.empty()) return;
    // 순서: 빠지기 → 움직이기(하나씩) → 끼워지기(하나씩)
    tMove = anyRemove ? 0.55f : 0.0f;
    for (auto& e : trEvents) if (e.type == EV_MOVE) { e.t0 = tMove; e.dur = 1.0f; tMove += 0.55f; }
    tAdd = tMove + 0.1f;
    for (auto& e : trEvents) if (e.type == EV_ADD) { e.t0 = tAdd; e.dur = 0.85f; tAdd += 0.4f; }
    trTotal = 0.0f;
    for (auto& e : trEvents) trTotal = std::max(trTotal, e.t0 + e.dur);
    trTotal += 0.25f;
    // 연출이 너무 길면 전체 속도를 올린다
    float scale = trTotal > 2.0f ? 2.0f / trTotal : 1.0f;
    for (auto& e : trEvents) { e.t0 *= scale; e.dur *= scale; }
    trTotal *= scale;

    buildWorld();
    std::vector<int> busy = oldOcc;
    computeVoidCells(&busy);
    chunkM.assign(P + 2, glm::mat4(1.0f));
    ghostM.assign(P, glm::mat4(0.0f));
    transitioning = true;
    trT = 0.0f;
}

void updateTransition(float dt) {
    trT += dt;
    camDistBoost += (5.0f - camDistBoost) * std::min(1.0f, dt * 3.0f);
    // 카메라가 씬 가운데를 향하도록 천천히 돌아서, 플레이어 주변에서 맵이 재조립되는 장면을 보여 준다
    glm::vec3 toC = glm::vec3(0, N * CS * 0.5f, 0) - (pl.pos + glm::vec3(0, 1.5f, 0));
    if (glm::length(glm::vec2(toC.x, toC.z)) > 0.5f) {
        float wantYaw = atan2f(toC.x, -toC.z), diff = wantYaw - camYaw;
        while (diff > PI) diff -= 2 * PI;
        while (diff < -PI) diff += 2 * PI;
        camYaw += diff * std::min(1.0f, dt * 2.5f);
    }
    camPitch += (glm::clamp(atan2f(toC.y, glm::length(glm::vec2(toC.x, toC.z))), -0.6f, 0.5f) - camPitch) * std::min(1.0f, dt * 2.5f);
    for (const auto& e : trEvents) {
        float s = glm::clamp((trT - e.t0) / e.dur, 0.0f, 1.0f);
        float k = s * s * (3.0f - 2.0f * s);
        glm::mat4 A;
        if (e.type == EV_MOVE) {
            glm::vec3 c = glm::mix(e.cOld, e.cNew, k) + glm::vec3(0, sinf(k * PI) * CS * 0.35f, 0);
            A = glm::translate(glm::mat4(1.0f), c) * glm::mat4_cast(glm::slerp(e.qOld, e.qNew, k));
            chunkM[e.piece] = A * e.invNew;
        } else if (e.type == EV_ADD) {
            if (s <= 0.0f) { chunkM[e.piece] = glm::mat4(0.0f); continue; }
            glm::vec3 c = e.cNew + e.dirOut * (1.0f - k) * CS * 2.2f;
            glm::quat q = e.qNew * glm::angleAxis((1.0f - k) * PI * 0.8f, e.axis);
            A = glm::translate(glm::mat4(1.0f), c) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), glm::vec3(0.4f + 0.6f * k));
            chunkM[e.piece] = A * e.invNew;
        } else {
            if (s >= 1.0f) { ghostM[e.piece] = glm::mat4(0.0f); continue; }
            glm::vec3 c = e.cOld + e.dirOut * k * CS * 2.2f - glm::vec3(0, k * k * CS, 0);
            glm::quat q = e.qOld * glm::angleAxis(k * PI * 0.7f, e.axis);
            A = glm::translate(glm::mat4(1.0f), c) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f - 0.6f * k));
            ghostM[e.piece] = A * e.invOld;
        }
    }
    // 서 있던 블럭이 움직이면 플레이어도 같이 실려 간다
    if (riderPiece >= 0) {
        const TrEvent& e = trEvents[riderPiece];
        float s = glm::clamp((trT - e.t0) / e.dur, 0.0f, 1.0f);
        if (riderOnGhost) { if (s < 1.0f) pl.pos = glm::vec3(ghostM[e.piece] * glm::inverse(e.invOld) * glm::vec4(riderLocal, 1.0f)); }
        else pl.pos = glm::vec3(chunkM[e.piece] * glm::inverse(e.invNew) * glm::vec4(riderLocal, 1.0f));
        pl.vel = glm::vec3(0.0f);
    }
    if (trT >= trTotal) {
        transitioning = false;
        for (auto& m : chunkM) m = glm::mat4(1.0f);
        ghostBoxes.clear();
        ghostDecos.clear();
        computeVoidCells();
        if (riderPiece >= 0 && !riderOnGhost) pl.pos = glm::vec3(glm::inverse(trEvents[riderPiece].invNew) * glm::vec4(riderLocal, 1.0f));
        pl.onGround = false;
        glm::ivec3 mc = worldToCell(pl.pos + glm::vec3(0, PLAYER_H * 0.5f, 0));
        if (riderOnGhost || (inGrid(mc) && occ[idxOf(mc)] == OCC_EMPTY)) die();
    }
}

// ===================================================================
// 렌더링 공통
// ===================================================================
GLuint sceneProg, shadowProg, skyProg, brightProg, blurProg, compositeProg, hudProg, cubeProg;
GLuint emptyVao, hudVao, hudVbo;
struct SceneLocs {
    GLint model, view, proj, lightVP, normalMat, objColor, Ks, shininess, emissive, pattern;
    GLint sunDir, sunColor, skyAmb, groundAmb, viewPos, fogColor, fogDensity, farPlane, shadowMap;
    GLint pointPos, pointColor, pointPower, alpha;
} sl;
GLint shadowModelLoc, shadowVPLoc;
GLuint shadowFbo, shadowTex;
struct Targets {
    int w = 0, h = 0;
    GLuint hdrFbo = 0, hdrColor = 0, hdrNormal = 0, hdrDepth = 0;
    GLuint bloomFbo[2] = { 0, 0 }, bloomTex[2] = { 0, 0 };
} rt;
bool shadowPass = false;
float drawAlpha = 1.0f;     // 카메라를 가리는 오브젝트를 반투명으로 그릴 때

GLuint makeTex(int w, int h, GLenum internalFmt, GLenum fmt, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void createShadowMap() {
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1, 1, 1, 1 };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGenFramebuffers(1, &shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void createTargets(int w, int h) {
    if (rt.hdrFbo) {
        glDeleteFramebuffers(1, &rt.hdrFbo);
        glDeleteTextures(1, &rt.hdrColor);
        glDeleteTextures(1, &rt.hdrNormal);
        glDeleteRenderbuffers(1, &rt.hdrDepth);
        glDeleteFramebuffers(2, rt.bloomFbo);
        glDeleteTextures(2, rt.bloomTex);
    }
    rt.w = w;
    rt.h = h;
    rt.hdrColor = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    rt.hdrNormal = makeTex(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT);
    glGenRenderbuffers(1, &rt.hdrDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, rt.hdrDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &rt.hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.hdrColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, rt.hdrNormal, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.hdrDepth);
    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);
    glGenFramebuffers(2, rt.bloomFbo);
    for (int i = 0; i < 2; ++i) {
        rt.bloomTex[i] = makeTex(std::max(1, w / 2), std::max(1, h / 2), GL_RGBA16F, GL_RGBA, GL_FLOAT);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.bloomTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void drawMesh(const Mesh& mesh, const glm::mat4& m, const Material& mt) {
    if (shadowPass) {
        glUniformMatrix4fv(shadowModelLoc, 1, GL_FALSE, glm::value_ptr(m));
    } else {
        glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
        glUniformMatrix4fv(sl.model, 1, GL_FALSE, glm::value_ptr(m));
        glUniformMatrix3fv(sl.normalMat, 1, GL_FALSE, glm::value_ptr(nm));
        glUniform3fv(sl.objColor, 1, glm::value_ptr(mt.color));
        glUniform1f(sl.Ks, mt.ks);
        glUniform1f(sl.shininess, mt.shininess);
        glUniform1f(sl.emissive, mt.emissive);
        glUniform1i(sl.pattern, mt.pattern);
        glUniform1f(sl.alpha, drawAlpha);
    }
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh.count);
}

void drawFullscreen() {
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void drawGear(const glm::mat4& m, float r, float thick, int teeth, const Material& mt) {
    drawMesh(cylMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(r * 1.64f, thick, r * 1.64f)), mt);
    Material hub = mt;
    hub.color *= 0.55f;
    drawMesh(cylMesh, m * glm::scale(glm::mat4(1.0f), glm::vec3(r * 0.5f, thick * 1.5f, r * 0.5f)), hub);
    for (int i = 0; i < teeth; ++i)
        drawMesh(cubeMesh, m * glm::rotate(glm::mat4(1.0f), i * 2.0f * PI / teeth, glm::vec3(0, 1, 0)) * TRS(glm::vec3(r * 0.86f, 0, 0), glm::vec3(r * 0.3f, thick, std::min(0.45f, r * 0.24f))), mt);
}

void drawDeco(const Deco& d, const glm::mat4& pre) {
    const Mesh& mesh = d.mesh ? cylMesh : cubeMesh;
    float t = globalTime * d.speed + d.phase;
    glm::mat4 m = pre * d.base;
    switch (d.kind) {
    case D_SPIN: m = m * glm::rotate(glm::mat4(1.0f), t, glm::vec3(0, 1, 0)); break;
    case D_PISTON: {
        float s = t - floorf(t);
        float pulse = s < 0.1f ? s / 0.1f : s < 0.22f ? 1.0f : std::max(0.0f, 1.0f - (s - 0.22f) / 0.5f);
        m = m * glm::translate(glm::mat4(1.0f), glm::vec3(0, -d.amp * pulse, 0));
        break;
    }
    case D_SLIDE: m = m * glm::translate(glm::mat4(1.0f), glm::vec3(fmodf(globalTime * d.speed + d.phase, 2.0f * d.amp) - d.amp, 0, 0)); break;
    case D_HAND: {
        float a = d.amp > 0.0f ? sinf(t) * d.amp : -t;
        m = m * glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 1, 0)) * glm::translate(glm::mat4(1.0f), glm::vec3(d.size.x * 0.5f - 0.15f, 0, 0));
        break;
    }
    case D_GEAR:
        drawGear(m * glm::rotate(glm::mat4(1.0f), t, glm::vec3(0, 1, 0)), d.size.x, d.size.y, d.teeth, d.mat);
        return;
    }
    drawMesh(mesh, m * glm::scale(glm::mat4(1.0f), d.size), d.mat);
}

// ===================================================================
// 연출 공용 상태: 카메라 덮어쓰기, 찰리 자세 덮어쓰기, 크레인, 필름통, 포스터
// ===================================================================
bool camOv = false;                     // 연출 카메라 사용 중 (홈 1인칭, 인트로, 크레인, 엿보기, THE END)
glm::vec3 camOvPos(0.0f), camOvFwd(0, 0, -1), camOvFocus(0.0f);
float fadeAmt = 0.0f;                   // 화면 암전 (0~1)
struct WorkerPose { bool on = false; glm::vec3 pos = glm::vec3(0.0f); float yaw = 0.0f; bool seated = false; } wOverride;
bool craneActive = false;
glm::vec3 craneTip(0.0f);
const glm::vec3 CAN_DESK(0.8f, 0.98f, -3.25f);
glm::vec3 canPos = CAN_DESK;
GLuint posterProg = 0, posterVao = 0, posterVbo = 0;
GLuint posterTex[3] = { 0, 0, 0 };

glm::vec3 craneBasePos() { float W = N * CS; return glm::vec3(W * 0.5f + 7.0f, 0.0f, -W * 0.5f - 7.0f); }
glm::vec3 craneRestTip() { float W = N * CS, H = N * CS + 2.0f; return glm::vec3(W * 0.2f, H + 7.0f, -W * 0.2f); }

void drawBeam(glm::vec3 a, glm::vec3 b, float t, const Material& m) {
    float len = glm::length(b - a);
    if (len < 0.01f) return;
    drawMesh(cubeMesh, beamMat(a, b) * glm::scale(glm::mat4(1.0f), glm::vec3(t, len, t)), m);
}

// 촬영 크레인: 세트 밖 기둥 → 팔 → 끝에 매달린 감독 의자
void drawCrane() {
    Material iron = mat(glm::vec3(0.18f, 0.18f, 0.2f), 0.6f, 32.0f), red = mat(glm::vec3(0.7f, 0.15f, 0.12f), 0.4f, 24.0f), canvas = mat(glm::vec3(0.75f, 0.68f, 0.5f), 0.1f, 8.0f);
    glm::vec3 base = craneBasePos(), top = base + glm::vec3(0, N * CS + 12.0f, 0);
    glm::vec3 tip = craneActive ? craneTip : craneRestTip();
    drawMesh(cubeMesh, TRS(base + glm::vec3(0, 0.6f, 0), glm::vec3(4.0f, 1.2f, 4.0f)), iron);
    drawBeam(base, top, 1.0f, red);
    drawBeam(top, tip, 0.7f, red);
    drawBeam(top, top - glm::normalize(tip - top) * 6.0f, 0.9f, iron);      // 균형추 쪽
    drawMesh(cubeMesh, TRS(top - glm::normalize(tip - top) * 6.0f, glm::vec3(2.2f)), iron);
    glm::vec3 seat = tip - glm::vec3(0, 2.2f, 0);
    drawBeam(tip, seat + glm::vec3(0, 0.6f, 0), 0.06f, iron);
    drawMesh(cubeMesh, TRS(seat, glm::vec3(0.9f, 0.1f, 0.9f)), canvas);
    drawMesh(cubeMesh, TRS(seat + glm::vec3(0, 0.45f, -0.42f), glm::vec3(0.9f, 0.8f, 0.06f)), canvas);
    for (int s = -1; s <= 1; s += 2) drawMesh(cubeMesh, TRS(seat + glm::vec3(0.45f * s, 0.25f, 0), glm::vec3(0.06f, 0.5f, 0.9f)), iron);
}

void drawWorker() {
    bool seated = wOverride.on && wOverride.seated;
    glm::mat4 root = glm::translate(glm::mat4(1.0f), wOverride.on ? wOverride.pos : pl.pos + glm::vec3(0, -pl.landDip * 0.5f, 0));
    root = glm::rotate(root, wOverride.on ? wOverride.yaw : pl.bodyYaw, glm::vec3(0, 1, 0));
    // 2단 점프는 공중제비 (몸 가운데를 축으로)
    if (!wOverride.on && !pl.onGround && pl.jumpChain == 2) root = glm::translate(root, glm::vec3(0, 0.9f, 0)) * glm::rotate(glm::mat4(1.0f), fmodf(globalTime * 9.0f, 2 * PI), glm::vec3(1, 0, 0)) * glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.9f, 0));
    float swing = pl.onGround ? sinf(pl.walkT * 7.0f) * 0.6f : 0.25f;
    Material pants = mat(glm::vec3(0.12f, 0.12f, 0.14f), 0.1f, 8.0f), overall = mat(glm::vec3(0.25f, 0.33f, 0.5f), 0.15f, 12.0f, 0.0f, 2);
    Material shirt = mat(glm::vec3(0.88f, 0.86f, 0.8f), 0.1f, 8.0f), skin = mat(glm::vec3(0.92f, 0.76f, 0.62f), 0.2f, 16.0f);
    Material black = mat(glm::vec3(0.05f), 0.5f, 32.0f), metal = mat(glm::vec3(0.7f, 0.7f, 0.74f), 1.0f, 64.0f), strap = mat(glm::vec3(0.75f, 0.2f, 0.18f), 0.2f, 12.0f);
    for (int i = 0; i < 2; ++i) {
        float s = i == 0 ? -1.0f : 1.0f;
        glm::mat4 leg = glm::rotate(glm::translate(root, glm::vec3(0.13f * s, 0.82f, 0)), seated ? -1.45f : swing * s, glm::vec3(1, 0, 0));
        drawMesh(cubeMesh, leg * TRS({ 0, -0.4f, 0 }, { 0.2f, 0.82f, 0.22f }), pants);
        drawMesh(cubeMesh, leg * TRS({ 0, -0.8f, 0.07f }, { 0.22f, 0.12f, 0.34f }), black);
    }
    if (carrying && !wOverride.on) {
        drawMesh(cubeMesh, root * TRS({ 0, 2.15f, 0 }, { 0.55f, 0.55f, 2.2f }), mat(glm::vec3(0.3f, 0.42f, 0.62f), 0.8f, 48.0f, 0.05f, 2));
        drawMesh(cubeMesh, root * TRS({ 0, 2.15f, 1.12f }, { 0.7f, 0.7f, 0.1f }), mat(glm::vec3(1.0f, 0.8f, 0.3f), 1.0f, 64.0f, 0.4f));
    }
    drawMesh(cubeMesh, root * TRS({ 0, 1.05f, 0 }, { 0.5f, 0.5f, 0.3f }), overall);
    drawMesh(cubeMesh, root * TRS({ 0, 1.42f, 0 }, { 0.52f, 0.3f, 0.3f }), shirt);
    // 멜빵을 얻으면 빨간 멜빵이 보인다
    if (hasSpanner) for (int s = -1; s <= 1; s += 2) drawMesh(cubeMesh, root * TRS({ 0.14f * s, 1.42f, 0 }, { 0.07f, 0.32f, 0.32f }), strap);
    drawMesh(cubeMesh, root * TRS({ 0, 1.74f, 0 }, { 0.3f, 0.32f, 0.3f }), skin);
    drawMesh(cylMesh, root * TRS({ 0, 1.92f, 0 }, { 0.46f, 0.04f, 0.46f }), black);
    drawMesh(cylMesh, root * TRS({ 0, 2.02f, 0 }, { 0.3f, 0.2f, 0.3f }), black);
    bool up = !wOverride.on && (pl.inLift || pl.onLadder) && !pl.onGround;
    for (int i = 0; i < 2; ++i) {
        float s = i == 0 ? -1.0f : 1.0f;
        float a = up ? -2.7f + sinf(globalTime * 8.0f + i * PI) * 0.3f : -swing * s * 0.8f;
        glm::mat4 arm = glm::rotate(glm::translate(root, glm::vec3(0.34f * s, 1.5f, 0)), a, glm::vec3(1, 0, 0));
        drawMesh(cubeMesh, arm * TRS({ 0, -0.3f, 0 }, { 0.14f, 0.6f, 0.14f }), shirt);
        drawMesh(cubeMesh, arm * TRS({ 0, -0.63f, 0 }, { 0.12f, 0.1f, 0.12f }), skin);
        if (hasSpanner && i == 1) {
            drawMesh(cubeMesh, arm * TRS({ 0, -0.68f, 0.18f }, { 0.05f, 0.05f, 0.42f }), metal);
            drawMesh(cubeMesh, arm * TRS({ 0, -0.68f, 0.42f }, { 0.14f, 0.05f, 0.1f }), metal);
        }
    }
}

void drawPickupModel(const Pickup& p, bool withBeam) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), p.pos + glm::vec3(0, sinf(globalTime * 2.0f + p.pos.x) * 0.15f, 0));
    m = m * glm::rotate(glm::mat4(1.0f), globalTime * 1.5f, glm::vec3(0, 1, 0)) * rotX(PI * 0.5f);
    if (p.type == 0) drawGear(m, 0.45f, 0.16f, 8, mat(glm::vec3(1.0f, 0.78f, 0.25f), 1.0f, 64.0f, 1.4f));
    else {
        Material red = mat(glm::vec3(0.9f, 0.2f, 0.18f), 0.3f, 16.0f, 0.8f), steel = mat(glm::vec3(0.7f, 0.8f, 1.0f), 1.0f, 64.0f, 0.8f);
        drawMesh(cubeMesh, m * TRS({ -0.2f, 0, 0 }, { 0.1f, 0.1f, 0.9f }), red);
        drawMesh(cubeMesh, m * TRS({ 0.2f, 0, 0 }, { 0.1f, 0.1f, 0.9f }), red);
        drawMesh(cubeMesh, m * TRS({ 0, 0.15f, 0 }, { 0.12f, 0.08f, 0.8f }), steel);
    }
    if (withBeam) drawMesh(cubeMesh, TRS(p.pos + glm::vec3(0, 3.0f, 0), glm::vec3(0.05f, 5.0f, 0.05f)), mat(p.type == 0 ? glm::vec3(1.0f, 0.8f, 0.3f) : glm::vec3(0.5f, 0.8f, 1.0f), 0, 1, 1.0f));
}

std::vector<float> boxFade, decoFade;
float frameDt = 0.016f;

// 카메라 → 캐릭터 머리 선분에 걸리는 오브젝트는 투명도를 낮추고, 벗어나면 다시 서서히 불투명하게
void updateFades(glm::vec3 target, glm::vec3 cam) {
    if (boxFade.size() != boxes.size()) boxFade.assign(boxes.size(), 1.0f);
    if (decoFade.size() != decos.size()) decoFade.assign(decos.size(), 1.0f);
    glm::vec3 seg = target - cam;
    float len = glm::length(seg);
    glm::vec3 end = len > 0.5f ? cam + seg * ((len - 0.35f) / len) : cam;
    float k = std::min(1.0f, frameDt * 10.0f);
    auto distToSeg = [&](glm::vec3 p) {
        glm::vec3 ab = end - cam;
        float t = glm::clamp(glm::dot(p - cam, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        return glm::length(cam + ab * t - p);
    };
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& b = boxes[i];
        bool hit = false;
        if (b.visible && b.center.y > 0.0f) {     // 지면은 제외
            if (transitioning && b.chunk >= 0 && b.chunk < (int)chunkM.size()) {
                glm::vec3 c = glm::vec3(chunkM[b.chunk] * glm::vec4(b.center, 1.0f));
                hit = distToSeg(c) < glm::length(b.size) * 0.5f + 0.3f;
            } else {
                float t;
                hit = segAABB(cam, end, b.bmin() - glm::vec3(0.3f), b.bmax() + glm::vec3(0.3f), t);
            }
        }
        boxFade[i] += ((hit ? 0.12f : 1.0f) - boxFade[i]) * k;
    }
    for (size_t i = 0; i < decos.size(); ++i) {
        const Deco& d = decos[i];
        glm::mat4 m = transitioning && d.chunk >= 0 && d.chunk < (int)chunkM.size() ? chunkM[d.chunk] * d.base : d.base;
        float r = d.kind == D_GEAR ? d.size.x * 1.1f : std::max(d.size.x, std::max(d.size.y, d.size.z)) * 0.6f;
        bool hit = distToSeg(glm::vec3(m[3])) < r + 0.3f;
        decoFade[i] += ((hit ? 0.12f : 1.0f) - decoFade[i]) * k;
    }
}

void drawWorld(bool withEffects) {
    auto chunkMat = [](int c) { return c >= 0 && c < (int)chunkM.size() ? chunkM[c] : glm::mat4(1.0f); };
    bool anim = transitioning;
    bool fadeOk = !shadowPass && boxFade.size() == boxes.size() && decoFade.size() == decos.size();
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& b = boxes[i];
        if (!b.visible) continue;
        if (shadowPass && b.mat.emissive > 0.3f) continue;
        if (fadeOk && boxFade[i] < 0.98f) continue;     // 반투명 오브젝트는 맨 마지막에
        drawMesh(cubeMesh, anim ? chunkMat(b.chunk) * TRS(b.center, b.size) : TRS(b.center, b.size), b.mat);
    }
    for (size_t i = 0; i < decos.size(); ++i) {
        const Deco& d = decos[i];
        if (shadowPass && (d.mat.emissive > 0.3f || d.size.x < 0.3f)) continue;
        if (fadeOk && decoFade[i] < 0.98f) continue;
        drawDeco(d, anim ? chunkMat(d.chunk) : glm::mat4(1.0f));
    }
    if (anim) {
        for (const auto& b : ghostBoxes) if (b.visible) drawMesh(cubeMesh, ghostM[b.chunk] * TRS(b.center, b.size), b.mat);
        for (const auto& d : ghostDecos) drawDeco(d, ghostM[d.chunk]);
    }
    if (!inHome) for (const auto& p : pickups) if (!p.taken) drawPickupModel(p, withEffects);
    if (!inHome || wOverride.on) drawWorker();
    if (!inHome) drawCrane();
    else {
        drawMesh(cylMesh, TRS(canPos, glm::vec3(0.5f, 0.12f, 0.5f)), mat(glm::vec3(0.7f, 0.72f, 0.75f), 1.0f, 64.0f));
        drawMesh(cylMesh, TRS(canPos + glm::vec3(0, 0.065f, 0), glm::vec3(0.2f, 0.02f, 0.2f)), mat(glm::vec3(0.7f, 0.2f, 0.15f), 0, 1, 0.4f));
    }
    if (!withEffects) return;
    // 빈 칸은 비워 둔다: 어둠 너머 다른 블럭 안이 그대로 보인다 (들어가면 사망)
    Material steam = mat(glm::vec3(0.86f, 0.84f, 0.8f), 0.0f, 1.0f, 0.15f);
    for (const auto& p : puffs) drawMesh(cubeMesh, TRS(p.pos, glm::vec3(p.size * std::min(1.0f, p.life))), steam);
    for (const auto& s : sparks) drawMesh(cubeMesh, TRS(s.pos, glm::vec3(0.07f * std::min(1.0f, s.life * 2.0f))), mat(s.color, 0, 1, 4.0f));
    // 카메라와 캐릭터 사이를 가리는 오브젝트: 깊이는 쓰지 않고 색만 섞는다 (법선 버퍼는 건드리지 않아 외곽선도 안 생김)
    if (fadeOk) {
        glEnablei(GL_BLEND, 0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        for (size_t i = 0; i < boxes.size(); ++i) {
            const Box& b = boxes[i];
            if (!b.visible || boxFade[i] >= 0.98f) continue;
            drawAlpha = boxFade[i];
            drawMesh(cubeMesh, anim ? chunkMat(b.chunk) * TRS(b.center, b.size) : TRS(b.center, b.size), b.mat);
        }
        for (size_t i = 0; i < decos.size(); ++i) {
            if (decoFade[i] >= 0.98f) continue;
            drawAlpha = decoFade[i];
            drawDeco(decos[i], anim ? chunkMat(decos[i].chunk) : glm::mat4(1.0f));
        }
        drawAlpha = 1.0f;
        glDepthMask(GL_TRUE);
        glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisablei(GL_BLEND, 0);
    }
    if (inHome) return;
    // 어둠 너머로도 보이는 표식: 톱니, 멜빵, 퇴근 기적
    glDisable(GL_DEPTH_TEST);
    for (const auto& p : pickups) {
        if (p.taken) continue;
        glm::vec3 col = p.type == 0 ? glm::vec3(1.0f, 0.8f, 0.3f) : glm::vec3(0.5f, 0.8f, 1.0f);
        drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), p.pos) * glm::rotate(glm::mat4(1.0f), globalTime, glm::vec3(0, 1, 0)) * rotZ(PI * 0.25f) * glm::scale(glm::mat4(1.0f), glm::vec3(0.35f)), mat(col, 0, 1, 2.5f));
    }
    bool open = cogsTaken() >= cogsTotal() && gridComplete();
    drawMesh(cubeMesh, glm::translate(glm::mat4(1.0f), goalPos + glm::vec3(0, 6.0f, 0)) * glm::rotate(glm::mat4(1.0f), globalTime * 0.7f, glm::vec3(0, 1, 0)) * rotZ(PI * 0.25f) * glm::scale(glm::mat4(1.0f), glm::vec3(0.6f)),
             mat(open ? glm::vec3(0.5f, 1.0f, 0.5f) : glm::vec3(1.0f, 0.95f, 0.85f), 0, 1, open ? 3.0f : 1.2f));
    glEnable(GL_DEPTH_TEST);
}

void clearFades() { boxFade.clear(); decoFade.clear(); }

// 3인칭 카메라 (연기 모드 기본)
void tpsCamera(glm::vec3& eye, glm::vec3& fwd, glm::vec3& focus) {
    focus = pl.pos + glm::vec3(0, 1.55f - pl.landDip, 0);
    fwd = glm::vec3(sinf(camYaw) * cosf(camPitch), sinf(camPitch), -cosf(camYaw) * cosf(camPitch));
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    eye = focus - fwd * (camDist + camDistBoost) + right * 0.5f;
    eye.y = std::max(eye.y, 0.4f);
}

// 홈 벽에 걸린 포스터 (클리어 순간을 찍은 화면)
void drawPosters(const glm::mat4& view, const glm::mat4& proj) {
    glUseProgram(posterProg);
    glUniformMatrix4fv(U(posterProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(U(posterProg, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
    glUniform1f(U(posterProg, "farPlane"), FAR_PLANE);
    glUniform1i(U(posterProg, "tex"), 1);
    glActiveTexture(GL_TEXTURE1);
    glBindVertexArray(posterVao);
    for (int i = 0; i < 3; ++i) {
        if (!posterTex[i]) continue;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(-2.6f + i * 2.6f, 1.85f, 3.955f)) * glm::rotate(glm::mat4(1.0f), PI, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(1.85f, 1.04f, 1.0f));
        glUniformMatrix4fv(U(posterProg, "model"), 1, GL_FALSE, glm::value_ptr(m));
        glBindTexture(GL_TEXTURE_2D, posterTex[i]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(sceneProg);
}

void renderWorld(int w, int h) {
    if (w != rt.w || h != rt.h) createTargets(w, h);
    // 카메라는 밀려 들어오지 않는다. 대신 사이를 가리는 오브젝트를 반투명하게
    glm::vec3 desired, fwd, focus;
    if (camOv) { desired = camOvPos; fwd = camOvFwd; focus = camOvFocus; }
    else tpsCamera(desired, fwd, focus);
    if (inHome) clearFades();
    else updateFades(focus, desired);
    glm::mat4 view = glm::lookAt(desired, desired + fwd, glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(70.0f), (float)w / (float)h, 0.05f, FAR_PLANE);
    glm::vec3 center = inHome ? glm::vec3(0, 1.7f, 0) : glm::vec3(0, N * CS * 0.5f, 0);
    float ext = inHome ? 8.0f : N * CS * 0.85f + 14.0f;
    glm::mat4 lightVP = glm::ortho(-ext, ext, -ext, ext, 60.0f, 340.0f) * glm::lookAt(center + SUN_DIR * 200.0f, center, glm::vec3(0, 1, 0));

    shadowPass = true;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glUseProgram(shadowProg);
    glUniformMatrix4fv(shadowVPLoc, 1, GL_FALSE, glm::value_ptr(lightVP));
    drawWorld(false);
    glDisable(GL_POLYGON_OFFSET_FILL);
    shadowPass = false;

    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo);
    glViewport(0, 0, w, h);
    float clearC[4] = { 0, 0, 0, 1 }, clearN[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    glClearBufferfv(GL_COLOR, 0, clearC);
    glClearBufferfv(GL_COLOR, 1, clearN);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(skyProg);
    glm::mat4 invVP = glm::inverse(proj * view);
    glUniformMatrix4fv(U(skyProg, "invViewProj"), 1, GL_FALSE, glm::value_ptr(invVP));
    glUniform3fv(U(skyProg, "camPos"), 1, glm::value_ptr(desired));
    glUniform3fv(U(skyProg, "sunDir"), 1, glm::value_ptr(SUN_DIR));
    glUniform3fv(U(skyProg, "zenith"), 1, glm::value_ptr(SKY_ZENITH));
    glUniform3fv(U(skyProg, "horizon"), 1, glm::value_ptr(SKY_HORIZON));
    glm::vec3 moon = SUN_COLOR * 0.3f;
    glUniform3fv(U(skyProg, "sunColor"), 1, glm::value_ptr(moon));
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);

    glUseProgram(sceneProg);
    glUniformMatrix4fv(sl.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(sl.proj, 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(sl.lightVP, 1, GL_FALSE, glm::value_ptr(lightVP));
    glUniform3fv(sl.sunDir, 1, glm::value_ptr(SUN_DIR));
    glUniform3fv(sl.sunColor, 1, glm::value_ptr(SUN_COLOR));
    glUniform3f(sl.skyAmb, 0.42f, 0.43f, 0.5f);
    glUniform3f(sl.groundAmb, 0.32f, 0.27f, 0.23f);
    glUniform3fv(sl.viewPos, 1, glm::value_ptr(desired));
    glUniform3fv(sl.fogColor, 1, glm::value_ptr(SKY_HORIZON));
    glUniform1f(sl.fogDensity, inHome ? 0.0f : 0.0035f);
    glUniform1f(sl.farPlane, FAR_PLANE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glUniform1i(sl.shadowMap, 0);
    glm::vec3 lantern = inHome ? glm::vec3(0, 2.9f, 0.3f) : pl.pos + glm::vec3(0, 2.4f, 0);
    glUniform3fv(sl.pointPos, 1, glm::value_ptr(lantern));
    glUniform3f(sl.pointColor, 1.0f, 0.82f, 0.55f);
    glUniform1f(sl.pointPower, inHome ? 7.0f : 3.0f);
    drawWorld(true);
    if (inHome) drawPosters(view, proj);

    glDisable(GL_DEPTH_TEST);
    int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    glViewport(0, 0, bw, bh);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[0]);
    glUseProgram(brightProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt.hdrColor);
    glUniform1i(U(brightProg, "src"), 0);
    drawFullscreen();
    glUseProgram(blurProg);
    glUniform1i(U(blurProg, "src"), 0);
    glUniform2f(U(blurProg, "texel"), 1.0f / bw, 1.0f / bh);
    for (int i = 0; i < 8; ++i) {
        int horiz = i % 2 == 0 ? 1 : 0;
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[horiz]);
        glBindTexture(GL_TEXTURE_2D, rt.bloomTex[1 - horiz]);
        glUniform1i(U(blurProg, "horizontal"), horiz);
        drawFullscreen();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glUseProgram(compositeProg);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt.hdrColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rt.hdrNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, rt.bloomTex[0]);
    glUniform1i(U(compositeProg, "sceneTex"), 0);
    glUniform1i(U(compositeProg, "normalTex"), 1);
    glUniform1i(U(compositeProg, "bloomTex"), 2);
    glUniform2f(U(compositeProg, "texel"), 1.0f / w, 1.0f / h);
    glUniform1f(U(compositeProg, "film"), filmOn ? 1.0f : 0.0f);
    glUniform1f(U(compositeProg, "flash"), std::max(flashT, deathT));
    glUniform1f(U(compositeProg, "fade"), fadeAmt);
    glUniform1f(U(compositeProg, "time"), globalTime);
    drawFullscreen();
    glActiveTexture(GL_TEXTURE0);
}

// ===================================================================
// 편집 모드 (씬 조립)
// ===================================================================
enum { MODE_SCENE, MODE_EDIT, MODE_HOME };     // 연기 모드, 감독 모드, 찰리의 방
int mode = MODE_SCENE;
int selPiece = 0;
float asmYaw = 0.35f, asmPitch = 0.55f, asmZoom = 1.0f;
glm::ivec3 hoverTarget(0);
bool hoverValid = false;
int hoverPiece = -1;
glm::ivec3 ghostPos(0);
bool ghostOk = false, ghostShow = false;
bool rDown = false, rMoved = false;
double rStartX = 0, rStartY = 0, lastMX = 0, lastMY = 0;
glm::mat4 cubeVP(1.0f);
struct Slot { float x, y, s; int piece; };
std::vector<Slot> slots;
float panelTop = 0.0f;

glm::vec3 asmEyeDir() { return glm::vec3(cosf(asmPitch) * sinf(asmYaw), sinf(asmPitch), cosf(asmPitch) * cosf(asmYaw)); }
glm::mat4 asmVP(int w, int h) {
    float panel = glm::clamp(panelTop / (float)std::max(h, 1), 0.0f, 0.45f);
    float dist = (N * 2.1f + 2.6f) * asmZoom;
    glm::mat4 shift = glm::translate(glm::mat4(1.0f), glm::vec3(0, panel, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f - panel * 0.6f, 1.0f - panel * 0.6f, 1.0f));
    return shift * glm::perspective(glm::radians(36.0f), (float)w / (float)std::max(h, 1), 0.1f, 200.0f) * glm::lookAt(asmEyeDir() * dist, glm::vec3(0.0f), glm::vec3(0, 1, 0));
}
glm::vec3 asmCellCenter(glm::ivec3 c) { return glm::vec3(c) - (N - 1) * 0.5f; }
glm::vec3 worldToAsm(glm::vec3 p) { return glm::vec3(p.x / CS, p.y / CS - N * 0.5f, p.z / CS); }

glm::vec2 mouseFb() {
    double mx, my;
    glfwGetCursorPos(gWindow, &mx, &my);
    int ww, wh, fw, fh;
    glfwGetWindowSize(gWindow, &ww, &wh);
    glfwGetFramebufferSize(gWindow, &fw, &fh);
    if (ww <= 0 || wh <= 0) return glm::vec2(-1);
    return glm::vec2((float)mx * fw / ww, (float)(wh - my) * fh / wh);
}

void layoutSlots(int w, int h) {
    slots.clear();
    float ui = h / 720.0f, gap = 8.0f * ui, s = 84.0f * ui;
    int count = (int)pieces.size(), perRow = 1, rows = 1;
    for (;;) {
        perRow = std::max(1, (int)((w - 40.0f * ui) / (s + gap)));
        rows = (count + perRow - 1) / perRow;
        if (rows <= 2 || s < 44.0f * ui) break;
        s -= 4.0f * ui;
    }
    for (int i = 0; i < count; ++i) {
        int r = i / perRow, c = i % perRow;
        int inRow = std::min(perRow, count - r * perRow);
        float rowW = inRow * s + (inRow - 1) * gap;
        slots.push_back({ (w - rowW) * 0.5f + c * (s + gap), 14.0f * ui + (rows - 1 - r) * (s + gap), s, i });
    }
    panelTop = 14.0f * ui + rows * (s + gap) + 30.0f * ui;
}

int slotAt(glm::vec2 m) {
    for (const auto& s : slots)
        if (m.x >= s.x && m.x <= s.x + s.s && m.y >= s.y && m.y <= s.y + s.s) return s.piece;
    return -1;
}

void selectNextUnplaced(int from, int dir = 1) {
    int n = (int)pieces.size();
    for (int k = 1; k <= n; ++k) {
        int i = ((from + dir * k) % n + n) % n;
        if (!pieces[i].placed) { selPiece = i; return; }
    }
    selPiece = -1;
}

void updateHover(int w, int h) {
    hoverValid = false;
    hoverPiece = -1;
    ghostShow = false;
    glm::vec2 m = mouseFb();
    if (m.y < panelTop) return;
    glm::mat4 inv = glm::inverse(asmVP(w, h));
    float nx = 2.0f * m.x / w - 1.0f, ny = 2.0f * m.y / h - 1.0f;
    glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
    glm::vec3 o = glm::vec3(a) / a.w, d = glm::normalize(glm::vec3(b) / b.w - o);
    float half = N * 0.5f, t0 = -1e9f, t1 = 1e9f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-7f) { if (fabsf(o[i]) > half) return; continue; }
        float ta = (-half - o[i]) / d[i], tb = (half - o[i]) / d[i];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    if (t0 > t1 || t1 < 0.0f) return;
    bool haveEmpty = false;
    glm::ivec3 lastEmpty(0);
    for (float t = std::max(t0, 0.0f) + 0.001f; t < t1; t += 0.02f) {
        glm::ivec3 c = glm::clamp(glm::ivec3(glm::floor(o + d * t + half)), glm::ivec3(0), glm::ivec3(N - 1));
        int v = occ[idxOf(c)];
        if (v == OCC_EMPTY) { lastEmpty = c; haveEmpty = true; }
        else { if (v >= 0) hoverPiece = v; break; }
    }
    if (!haveEmpty) return;
    hoverValid = true;
    hoverTarget = lastEmpty;
    if (selPiece >= 0 && !pieces[selPiece].placed) {
        const Piece& p = pieces[selPiece];
        auto sc = shapeCells(p, p.rot);
        ghostShow = true;
        ghostOk = false;
        ghostPos = hoverTarget - sc[0];
        for (auto c : sc)
            if (canPlace(p, p.rot, hoverTarget - c, -1)) { ghostPos = hoverTarget - c; ghostOk = true; break; }
    }
}

void placeSelected() {
    if (selPiece < 0 || !ghostOk || !ghostShow) return;
    pieces[selPiece].placed = true;
    pieces[selPiece].pos = ghostPos;
    rebuildOcc();
    selectNextUnplaced(selPiece);
}

void removePiece(int p) {
    if (p < 0) return;
    pieces[p].placed = false;
    rebuildOcc();
    selPiece = p;
}

// 화면 기준 회전: Q/E 세로축, W/S 화면 오른쪽 축, A/D 화면 앞쪽 축 (24방향 전부)
void rotateSel(int key) {
    if (selPiece < 0 || pieces[selPiece].placed) return;
    glm::vec3 fwd = -asmEyeDir();
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 fh = glm::normalize(glm::vec3(fwd.x, 0, fwd.z));
    auto snap = [](glm::vec3 v, int& axis) { axis = fabsf(v.x) > fabsf(v.z) ? 0 : 2; return v[axis] > 0.0f ? 1 : -1; };
    int axis = 1, dir = 1;
    if (key == GLFW_KEY_Q) dir = 1;
    else if (key == GLFW_KEY_E) dir = -1;
    else if (key == GLFW_KEY_W) dir = -snap(right, axis);
    else if (key == GLFW_KEY_S) dir = snap(right, axis);
    else if (key == GLFW_KEY_A) dir = snap(fh, axis);
    else if (key == GLFW_KEY_D) dir = -snap(fh, axis);
    pieces[selPiece].rot = quarterTurn(axis, dir) * pieces[selPiece].rot;
}

void autoSolve() {
    for (auto& p : pieces) {
        p.placed = p.inSolution;
        if (p.inSolution) { p.rot = glm::mat3(1.0f); p.pos = p.solPos; }
    }
    rebuildOcc();
    selectNextUnplaced(-1);
}

void drawCubePart(const glm::mat4& m, glm::vec3 color, float emissive, float alpha = 1.0f) {
    glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    glm::mat4 mvp = cubeVP * m;
    glUniformMatrix4fv(U(cubeProg, "mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix3fv(U(cubeProg, "normalMat"), 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3fv(U(cubeProg, "color"), 1, glm::value_ptr(color));
    glUniform1f(U(cubeProg, "emissive"), emissive);
    glUniform1f(U(cubeProg, "alpha"), alpha);
    glBindVertexArray(cubeMesh.vao);
    glDrawArrays(GL_TRIANGLES, 0, cubeMesh.count);
}

void drawBoxEdges(const glm::mat4& m, glm::vec3 c, glm::vec3 half, float thick, glm::vec3 color, float em = 1.0f) {
    for (int a = 0; a < 3; ++a) {
        int b = (a + 1) % 3, d = (a + 2) % 3;
        for (int sb = -1; sb <= 1; sb += 2)
            for (int sd = -1; sd <= 1; sd += 2) {
                glm::vec3 p = c, s(thick);
                p[b] += half[b] * sb;
                p[d] += half[d] * sd;
                s[a] = half[a] * 2.0f + thick;
                drawCubePart(m * TRS(p, s), color, em);
            }
    }
}

// 블럭 하나: 컨셉 색 + 어두운 판(= 그 블럭의 바닥). 발판 구성은 일부러 보여 주지 않는다
void drawCellVis(glm::vec3 center, const glm::mat3& R, int theme, float alpha, glm::vec3 tint, float tintAmt, float glow) {
    const Theme& t = THEMES[theme];
    drawCubePart(TRS(center, glm::vec3(0.84f)), glm::mix(t.color, tint, tintAmt), glow, alpha);
    glm::vec3 fd = R * glm::vec3(0, -1, 0);
    drawCubePart(TRS(center + fd * 0.43f, glm::abs(R * glm::vec3(0.8f, 0.06f, 0.8f))), glm::vec3(0.1f, 0.09f, 0.08f), 0.0f, alpha);
}

void drawPieceVis(const Piece& p, glm::vec3 base, float alpha, glm::vec3 tint, float tintAmt, float glow) {
    auto sc = shapeCells(p, p.rot);
    for (size_t k = 0; k < sc.size(); ++k) drawCellVis(base + glm::vec3(sc[k]), contentRot(p.rot, p.cells[k]), p.cells[k].theme, alpha, tint, tintAmt, glow);
    for (size_t a = 0; a < sc.size(); ++a)
        for (size_t b = a + 1; b < sc.size(); ++b) {
            glm::ivec3 d = glm::abs(sc[a] - sc[b]);
            if (d.x + d.y + d.z != 1) continue;
            int axis = d.x ? 0 : d.y ? 1 : 2;
            glm::vec3 s(0.6f);
            s[axis] = 0.2f;
            drawCubePart(TRS(base + (glm::vec3(sc[a]) + glm::vec3(sc[b])) * 0.5f, s), glm::mix(THEMES[p.cells[a].theme].color, tint, tintAmt) * 0.8f, glow, alpha);
        }
    if (p.custom) {
        glm::ivec3 mx(0);
        for (auto c : sc) mx = glm::max(mx, c);
        glm::vec3 half = (glm::vec3(mx) + 1.0f) * 0.5f;
        cubeVP = cubeVP;
        drawBoxEdges(glm::mat4(1.0f), base + half - 0.5f, half + 0.02f, 0.04f, glm::vec3(0.6f, 0.9f, 1.0f), 0.6f);
    }
}

glm::ivec3 shapeExtent(const Piece& p) {
    glm::ivec3 mx(0);
    for (auto c : shapeCells(p, p.rot)) mx = glm::max(mx, c);
    return mx;
}

// 감독 모드 화면용 작은 찰리 (세트 안의 대역 / 세트 밖 감독 의자의 찰리)
void drawMiniCharlie(glm::vec3 base, float s, float yaw, bool seated, float glow) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), base) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(s));
    glm::vec3 pants(0.12f, 0.12f, 0.14f), overall(0.25f, 0.33f, 0.5f), shirt(0.88f, 0.86f, 0.8f), skin(0.92f, 0.76f, 0.62f), black(0.05f);
    if (seated) {
        glm::vec3 wood(0.45f, 0.3f, 0.18f), canvas(0.75f, 0.68f, 0.5f);
        drawCubePart(m * TRS({ 0, 0.45f, 0 }, { 0.8f, 0.08f, 0.8f }), canvas, 0.0f);
        drawCubePart(m * TRS({ 0, 0.95f, -0.38f }, { 0.8f, 0.7f, 0.06f }), canvas, 0.0f);
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) drawCubePart(m * TRS({ 0.36f * sx, 0.22f, 0.34f * sz }, { 0.06f, 0.45f, 0.06f }), wood, 0.0f);
        for (int sx = -1; sx <= 1; sx += 2) {
            drawCubePart(m * TRS({ 0.13f * sx, 0.58f, 0.2f }, { 0.2f, 0.2f, 0.55f }), pants, 0.0f);
            drawCubePart(m * TRS({ 0.13f * sx, 0.3f, 0.47f }, { 0.2f, 0.55f, 0.2f }), pants, 0.0f);
        }
        drawCubePart(m * TRS({ 0, 0.9f, -0.05f }, { 0.5f, 0.5f, 0.3f }), overall, glow);
        drawCubePart(m * TRS({ 0, 1.25f, -0.05f }, { 0.52f, 0.3f, 0.3f }), shirt, glow);
        drawCubePart(m * TRS({ 0, 1.55f, -0.05f }, { 0.3f, 0.32f, 0.3f }), skin, glow);
        drawCubePart(m * TRS({ 0, 1.73f, -0.05f }, { 0.46f, 0.04f, 0.46f }), black, 0.0f);
        drawCubePart(m * TRS({ 0, 1.83f, -0.05f }, { 0.3f, 0.2f, 0.3f }), black, 0.0f);
        // 메가폰
        drawCubePart(m * TRS({ 0.3f, 1.3f, 0.3f }, { 0.12f, 0.12f, 0.55f }), glm::vec3(0.95f, 0.85f, 0.35f), 0.3f);
        drawCubePart(m * TRS({ 0.3f, 1.3f, 0.6f }, { 0.26f, 0.26f, 0.08f }), glm::vec3(0.95f, 0.85f, 0.35f), 0.3f);
    } else {
        for (int sx = -1; sx <= 1; sx += 2) drawCubePart(m * TRS({ 0.13f * sx, 0.4f, 0 }, { 0.2f, 0.8f, 0.22f }), pants, glow);
        drawCubePart(m * TRS({ 0, 1.05f, 0 }, { 0.5f, 0.5f, 0.3f }), overall, glow);
        drawCubePart(m * TRS({ 0, 1.42f, 0 }, { 0.52f, 0.3f, 0.3f }), shirt, glow);
        drawCubePart(m * TRS({ 0, 1.74f, 0 }, { 0.3f, 0.32f, 0.3f }), skin, glow);
        drawCubePart(m * TRS({ 0, 1.92f, 0 }, { 0.46f, 0.04f, 0.46f }), black, 0.0f);
        drawCubePart(m * TRS({ 0, 2.02f, 0 }, { 0.3f, 0.2f, 0.3f }), black, 0.0f);
    }
}

void renderEdit(int w, int h) {
    layoutSlots(w, h);
    updateHover(w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.06f, 0.055f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(cubeProg);
    cubeVP = asmVP(w, h);
    float half = N * 0.5f;
    drawBoxEdges(glm::mat4(1.0f), glm::vec3(0.0f), glm::vec3(half), 0.03f, glm::vec3(0.55f, 0.48f, 0.38f), 0.3f);
    for (int i = 0; i <= N; ++i) {
        drawCubePart(TRS(glm::vec3(0, -half, i - half), glm::vec3((float)N, 0.015f, 0.015f)), glm::vec3(0.5f, 0.44f, 0.36f), 0.2f);
        drawCubePart(TRS(glm::vec3(i - half, -half, 0), glm::vec3(0.015f, 0.015f, (float)N)), glm::vec3(0.5f, 0.44f, 0.36f), 0.2f);
    }
    drawCubePart(TRS(glm::vec3(0, -half - 0.55f, 0), glm::vec3(0.05f, 0.7f, 0.05f)), glm::vec3(1.0f), 0.4f);
    drawCubePart(TRS(glm::vec3(0, -half - 0.95f, 0), glm::vec3(0.2f, 0.12f, 0.2f)), glm::vec3(1.0f), 0.4f);

    float pulse = 0.25f + 0.2f * sinf(globalTime * 4.0f);
    drawCellVis(asmCellCenter(startCell), glm::mat3(1.0f), TH_START, 1.0f, glm::vec3(0), 0, 0.1f);
    drawCellVis(asmCellCenter(endCell), glm::mat3(1.0f), TH_END, 1.0f, glm::vec3(0), 0, pulse);
    for (int p = 0; p < (int)pieces.size(); ++p) {
        if (!pieces[p].placed) continue;
        float glow = (p == hoverPiece && !(ghostShow && ghostOk)) ? 0.25f + 0.1f * sinf(globalTime * 6.0f) : 0.0f;
        drawPieceVis(pieces[p], asmCellCenter(pieces[p].pos), 1.0f, glm::vec3(0), 0.0f, glow);
    }
    // 톱니/멜빵/플레이어 위치 (조립 공간으로 축소해서 표시)
    glDisable(GL_DEPTH_TEST);
    for (const auto& pk : pickups)
        if (!pk.taken) drawCubePart(TRS(worldToAsm(pk.pos), glm::vec3(0.12f)), pk.type == 0 ? glm::vec3(1.0f, 0.8f, 0.25f) : glm::vec3(0.5f, 0.8f, 1.0f), 0.8f);
    {
        // 내 위치: 지금 있는 칸 테두리 + 정확한 위치 점
        glm::ivec3 pc = worldToCell(pl.pos + glm::vec3(0, 0.1f, 0));
        if (inGrid(pc)) drawBoxEdges(glm::mat4(1.0f), asmCellCenter(pc), glm::vec3(0.5f), 0.03f, glm::vec3(1.0f, 1.0f, 1.0f), 0.8f + pulse);
        glm::vec3 me = worldToAsm(pl.pos + glm::vec3(0, PLAYER_H * 0.5f, 0));
        drawCubePart(TRS(me, glm::vec3(0.09f)), glm::vec3(1.0f), 1.5f + pulse * 2.0f);
        drawCubePart(TRS(me + glm::vec3(0, 0.3f, 0), glm::vec3(0.02f, 0.5f, 0.02f)), glm::vec3(1.0f), 1.0f);
    }
    glEnable(GL_DEPTH_TEST);
    if (hoverValid && !ghostShow) drawBoxEdges(glm::mat4(1.0f), asmCellCenter(hoverTarget), glm::vec3(0.5f), 0.02f, glm::vec3(0.9f, 0.85f, 0.6f), 0.6f);
    if (ghostShow) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        drawPieceVis(pieces[selPiece], asmCellCenter(ghostPos), 0.55f, ghostOk ? glm::vec3(0.4f, 1.0f, 0.45f) : glm::vec3(1.0f, 0.25f, 0.2f), 0.45f, 0.25f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
}

void renderSlotPreviews(int w, int h) {
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(cubeProg);
    for (const auto& s : slots) {
        const Piece& p = pieces[s.piece];
        int x = (int)s.x + 2, y = (int)s.y + 2, sz = (int)s.s - 4;
        glViewport(x, y, sz, sz);
        glScissor(x, y, sz, sz);
        glClear(GL_DEPTH_BUFFER_BIT);
        glm::vec3 ext = glm::vec3(shapeExtent(p)) + 1.0f;
        float e = std::max(ext.x, std::max(ext.y, ext.z));
        cubeVP = glm::perspective(glm::radians(30.0f), 1.0f, 0.1f, 100.0f) * glm::lookAt(asmEyeDir() * (e * 2.1f + 1.4f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
        drawPieceVis(p, -(ext - 1.0f) * 0.5f, 1.0f, glm::vec3(0.15f), p.placed ? 0.75f : 0.0f, 0.0f);
    }
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, w, h);
    glBindVertexArray(0);
}

// ===================================================================
// HUD (비트맵 글꼴)
// ===================================================================
struct Batch2D {
    std::vector<float> tris;
    void rect(float x, float y, float w, float h, const glm::vec4& c) {
        float v[6][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y }, { x + w, y + h }, { x, y + h } };
        for (auto& p : v) tris.insert(tris.end(), { p[0], p[1], c.r, c.g, c.b, c.a });
    }
    void frame(float x, float y, float w, float h, float t, const glm::vec4& c) {
        rect(x, y, w, t, c); rect(x, y + h - t, w, t, c); rect(x, y, t, h, c); rect(x + w - t, y, t, h, c);
    }
};

struct Glyph { char c; unsigned char rows[7]; };
const Glyph FONT[] = {
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3',{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}},
    {'6',{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}},
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}}, {'D',{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x11,0x19,0x15,0x13,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x11,0x0A,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {':',{0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}}, {'/',{0x00,0x01,0x02,0x04,0x08,0x10,0x00}},
    {'-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}}, {'.',{0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}},
    {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'!',{0x04,0x04,0x04,0x04,0x04,0x00,0x04}}, {'+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
    {'=',{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}}, {'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
    {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}}, {',',{0x00,0x00,0x00,0x00,0x0C,0x04,0x08}},
    {'?',{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}}, {'\'',{0x04,0x04,0x08,0x00,0x00,0x00,0x00}},
};

const unsigned char* glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (const auto& g : FONT) if (g.c == ch) return g.rows;
    return nullptr;
}
float textWidth(const std::string& s, float scale) { return s.empty() ? 0.0f : (s.size() * 6.0f - 1.0f) * scale; }
void drawText(Batch2D& b, float x, float y, float s, const std::string& str, const glm::vec4& col) {
    for (int pass = 0; pass < 2; ++pass) {
        glm::vec4 c = pass == 0 ? glm::vec4(0, 0, 0, col.a * 0.55f) : col;
        float off = pass == 0 ? s * 0.5f : 0.0f, cx = x;
        for (char ch : str) {
            const unsigned char* g = glyphFor(ch);
            if (g)
                for (int r = 0; r < 7; ++r)
                    for (int k = 0; k < 5; ++k)
                        if (g[r] & (0x10 >> k)) b.rect(cx + k * s + off, y + (6 - r) * s - off, s, s, c);
            cx += 6.0f * s;
        }
    }
}
void drawTextCentered(Batch2D& b, float cx, float y, float s, const std::string& str, const glm::vec4& col) { drawText(b, cx - textWidth(str, s) * 0.5f, y, s, str, col); }

void flushHUD(Batch2D& b, int w, int h) {
    float m[9] = { 2.0f / w, 0, -1, 0, 2.0f / h, -1, 0, 0, 1 };
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(hudProg);
    glUniformMatrix3fv(U(hudProg, "xform"), 1, GL_TRUE, m);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glBufferData(GL_ARRAY_BUFFER, b.tris.size() * sizeof(float), b.tris.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(b.tris.size() / 6));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

const glm::vec4 C_WHITE(1, 1, 1, 0.92f), C_DIM(1, 1, 1, 0.6f), C_GOLD(1.0f, 0.8f, 0.3f, 1.0f), C_RED(1.0f, 0.4f, 0.3f, 1.0f), C_GREEN(0.5f, 1.0f, 0.55f, 1.0f), C_BLUE(0.55f, 0.85f, 1.0f, 1.0f);

void drawStatus(Batch2D& b, int w, int h, float ui) {
    int ct = cogsTotal(), cg = cogsTaken();
    for (int i = 0; i < ct; ++i) {
        float x = 20 * ui + i * 22 * ui, y = h - 74 * ui;
        b.rect(x, y, 16 * ui, 16 * ui, i < cg ? C_GOLD : glm::vec4(0, 0, 0, 0.5f));
        b.rect(x + 5 * ui, y + 5 * ui, 6 * ui, 6 * ui, i < cg ? glm::vec4(0.4f, 0.28f, 0.05f, 1) : glm::vec4(1, 1, 1, 0.15f));
    }
    int f = filledCount(), tot = N * N * N;
    drawText(b, 20 * ui, h - 98 * ui, 1.6f * ui, "COGS " + std::to_string(cg) + "/" + std::to_string(ct) + "   FILLED " + std::to_string(f) + "/" + std::to_string(tot) + "   TAKE " + std::to_string(deaths + 1), C_WHITE);
    bool hasMulti = false;
    for (auto& p : pickups) if (p.type == 1) hasMulti = true;
    if (hasMulti) drawText(b, 20 * ui, h - 118 * ui, 1.5f * ui, std::string(hasSpanner ? "SPANNER: READY" : "SPANNER: FIND THE SUSPENDERS") + (carrying ? "   CARRYING A PILLAR" : ""), hasSpanner ? C_BLUE : C_DIM);
}

void drawCardHUD(int w, int h);
const char* STAGE_TITLES_EARLY[3] = { "THE FIRST SHIFT", "OVERTIME", "THE BIG MACHINE" };
void renderEditHUD(int w, int h) {
    float ui = h / 720.0f;
    Batch2D b;
    b.rect(0, 0, (float)w, panelTop - 8 * ui, glm::vec4(0.05f, 0.04f, 0.035f, 0.85f));
    for (const auto& s : slots) {
        b.rect(s.x, s.y, s.s, s.s, pieces[s.piece].placed ? glm::vec4(0.1f, 0.1f, 0.1f, 0.9f) : glm::vec4(0.2f, 0.17f, 0.14f, 0.95f));
        if (s.piece == selPiece) b.frame(s.x - 3 * ui, s.y - 3 * ui, s.s + 6 * ui, s.s + 6 * ui, 3 * ui, C_GOLD);
        else if (pieces[s.piece].custom) b.frame(s.x - 1, s.y - 1, s.s + 2, s.s + 2, 2 * ui, glm::vec4(0.6f, 0.9f, 1.0f, 0.8f));
    }
    flushHUD(b, w, h);
    renderSlotPreviews(w, h);

    Batch2D t;
    for (const auto& s : slots) {
        const Piece& p = pieces[s.piece];
        std::string lab = p.custom ? p.name : p.name + std::to_string(p.cells.size());
        if (s.piece < 9) lab = std::to_string(s.piece + 1) + " " + lab;
        float sc = 1.2f * ui;
        while (textWidth(lab, sc) > s.s - 6 * ui && sc > 0.6f * ui) sc -= 0.1f * ui;
        drawText(t, s.x + 4 * ui, s.y + s.s - 11 * ui, sc, lab, p.placed ? C_DIM : C_WHITE);
        if (p.placed) drawText(t, s.x + s.s - textWidth("IN", 1.4f * ui) - 4 * ui, s.y + 4 * ui, 1.4f * ui, "IN", C_GREEN);
    }
    drawText(t, 20 * ui, h - 40 * ui, 2.6f * ui, "DIRECTOR MODE  -  " + std::string(STAGE_TITLES_EARLY[std::min(stage, 2)]), C_WHITE);
    drawStatus(t, w, h, ui);
    float y = h - 142 * ui;
    if (selPiece >= 0) {
        const Piece& p = pieces[selPiece];
        drawText(t, 20 * ui, y, 1.6f * ui, "PIECE " + p.name + (p.custom ? "  (MULTI BLOCK)" : "  (" + std::to_string(p.cells.size()) + " BLOCKS)"), C_GOLD);
        drawText(t, 20 * ui, y - 20 * ui, 1.5f * ui, p.custom ? "OLD START/GOAL CELLS ARE OPEN DOORS" : THEMES[p.cells[0].theme].name, p.custom ? C_BLUE : C_WHITE);
    }
    if (hoverPiece >= 0 && !(ghostShow && ghostOk)) drawText(t, 20 * ui, y - 44 * ui, 1.4f * ui, "HOVER " + pieces[hoverPiece].name + " - RMB CLICK TO TAKE BACK", C_DIM);
    float rx = w - 20 * ui;
    auto right = [&](float yy, const std::string& s, const glm::vec4& c) { drawText(t, rx - textWidth(s, 1.4f * ui), yy, 1.4f * ui, s, c); };
    right(h - 34 * ui, "LMB PLACE    RMB CLICK TAKE BACK", C_DIM);
    right(h - 52 * ui, "RMB DRAG ORBIT    WHEEL ZOOM", C_DIM);
    right(h - 70 * ui, "Q/E  W/S  A/D  ROTATE (ALL 24 WAYS)", C_GOLD);
    right(h - 88 * ui, "Z/X OR 1-9 SELECT    BACKSPACE CLEAR", C_DIM);
    right(h - 106 * ui, "TAB / ENTER  ACTION! (BACK TO ACTING)", C_GREEN);
    right(h - 124 * ui, "F5 AUTO SOLVE (TEST)", C_DIM);
    right(h - 150 * ui, "DARK PLATE = FLOOR OF THAT BLOCK", C_WHITE);
    right(h - 168 * ui, "GREEN = START POINT  BRIGHT = GOAL POINT", C_WHITE);
    right(h - 186 * ui, "GOLD DOT = COG   BLUE DOT = SUSPENDERS   WHITE = YOU", C_WHITE);
    if (toastT > 0.0f) drawTextCentered(t, w * 0.5f, panelTop + 16 * ui, 2.0f * ui, toast, glm::vec4(1, 0.85f, 0.4f, std::min(1.0f, toastT)));
    flushHUD(t, w, h);
    drawCardHUD(w, h);
}

const char* currentRoomName() {
    glm::ivec3 g = worldToCell(pl.pos + glm::vec3(0, 0.9f, 0));
    if (!inGrid(g)) return "";
    int o = occ[idxOf(g)];
    if (o == OCC_START) return THEMES[TH_START].name;
    if (o == OCC_END) return THEMES[TH_END].name;
    if (o < 0) return "DARKNESS";
    const Piece& p = pieces[o];
    auto sc = shapeCells(p, p.rot);
    for (size_t k = 0; k < sc.size(); ++k) if (p.pos + sc[k] == g) return THEMES[p.cells[k].theme].name;
    return "";
}

// ===================================================================
// 진행 상태 / 연출 시퀀스
// ===================================================================
enum { SEQ_NONE, SEQ_PACK, SEQ_INTRO, SEQ_DIRECTOR, SEQ_THE_END, SEQ_MIRROR, SEQ_SHELF };
int seq = SEQ_NONE;
float seqT = 0.0f;
bool seqLong = false;                   // 감독 모드 전환: 스테이지 첫 번째만 크레인 연출
bool craneShown = false;
bool peeking = false, tabHeld = false;
double tabDownTime = 0.0;
float peekBlend = 0.0f;
int progress = 0;                       // 클리어한 스테이지 수 (다음 촬영 = progress)
int pendingStage = 0, shelfIdx = 0;
bool posterCaptured = false, capturePosterNow = false;
std::string cardText;
float cardT = 0.0f;
const int STAGE_COUNT = 3;
const char* STAGE_TITLES[STAGE_COUNT] = { "THE FIRST SHIFT", "OVERTIME", "THE BIG MACHINE" };
double lastMouseX = 0.0, lastMouseY = 0.0;
bool firstMouse = true;

void setCard(const std::string& s, float d) { cardText = s; cardT = d; }
float smooth01(float x) { x = glm::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
void setCamLook(glm::vec3 pos, glm::vec3 target) { camOv = true; camOvPos = pos; camOvFwd = glm::normalize(target - pos); camOvFocus = target; }
glm::vec3 sceneCenter() { return glm::vec3(0, N * CS * 0.5f, 0); }
glm::vec3 overheadPos() { float W = N * CS, H = N * CS + 2.0f; return glm::vec3(0, H + W * 0.35f + 6.0f, W * 0.8f); }
std::string stageLabel(int s) { const int sz[3] = { 2, 3, 4 }; return std::string("SCENE ") + std::to_string(s + 1) + ": " + STAGE_TITLES[s] + "  (" + gridLabel(sz[s]) + " SET)"; }

// ===================================================================
// 찰리의 방 (홈)
// ===================================================================
enum { IK_SCHEDULE, IK_MIRROR, IK_SHELF };
struct Interact { glm::vec3 pos; float r; int kind, idx; std::string label; };
std::vector<Interact> interacts;
int hoverInteract = -1;

void buildHome() {
    boxes.clear(); decos.clear(); shafts.clear(); ladders.clear(); vents.clear(); voidCells.clear(); puffs.clear(); sparks.clear();
    interacts.clear();
    curChunk = -1;
    Material floorM = mat(glm::vec3(0.46f, 0.32f, 0.2f), 0.2f, 16.0f, 0.0f, 1), wall = mat(glm::vec3(0.66f, 0.58f, 0.46f), 0.05f, 8.0f, 0.0f, 2);
    Material ceil = mat(glm::vec3(0.55f, 0.5f, 0.42f), 0.05f, 8.0f), wood = mat(glm::vec3(0.4f, 0.26f, 0.15f), 0.2f, 16.0f, 0.0f, 2), dark = mat(glm::vec3(0.12f, 0.1f, 0.09f), 0.3f, 16.0f);
    Material cork = mat(glm::vec3(0.62f, 0.45f, 0.28f), 0.05f, 8.0f, 0.0f, 2), sheet = mat(glm::vec3(0.85f, 0.82f, 0.74f), 0.1f, 8.0f), red = mat(glm::vec3(0.55f, 0.16f, 0.14f), 0.1f, 8.0f, 0.0f, 2);
    addBox({ 0, -0.1f, 0 }, { 9.4f, 0.2f, 8.4f }, floorM);
    addBox({ 0, 3.5f, 0 }, { 9.4f, 0.2f, 8.4f }, ceil);
    addBox({ 4.6f, 1.7f, 0 }, { 0.2f, 3.4f, 8.4f }, wall);
    addBox({ -4.6f, 1.7f, 0 }, { 0.2f, 3.4f, 8.4f }, wall);
    addBox({ 0, 1.7f, 4.1f }, { 9.4f, 3.4f, 0.2f }, wall);
    addBox({ 0, 1.7f, -4.1f }, { 9.4f, 3.4f, 0.2f }, wall);
    addBox({ 0, 0.01f, 0.4f }, { 3.4f, 0.02f, 2.4f }, red, false);
    // 침대, 의자
    addBox({ -3.4f, 0.3f, 2.6f }, { 1.8f, 0.6f, 2.8f }, wood);
    addBox({ -3.4f, 0.65f, 2.75f }, { 1.7f, 0.12f, 2.4f }, mat(glm::vec3(0.75f, 0.72f, 0.65f), 0.05f, 8.0f), false);
    addBox({ -3.4f, 0.78f, 3.6f }, { 1.2f, 0.2f, 0.5f }, sheet, false);
    addBox({ 0.0f, 0.24f, -2.4f }, { 0.6f, 0.48f, 0.6f }, wood);
    // 촬영 스케줄 보드 (-Z 벽) + 아래 책상 (필름통)
    addBox({ 0, 0.45f, -3.45f }, { 2.6f, 0.9f, 1.0f }, wood);
    addBox({ 0, 1.95f, -3.97f }, { 3.8f, 1.8f, 0.06f }, cork, false);
    addBox({ 0, 2.92f, -3.95f }, { 3.9f, 0.12f, 0.1f }, dark, false);
    addBox({ 0, 0.98f, -3.95f }, { 3.9f, 0.12f, 0.1f }, dark, false);
    for (int i = 0; i < STAGE_COUNT; ++i) {
        glm::vec3 p(-1.2f + i * 1.2f, 1.95f, -3.92f);
        bool done = i < progress, next = i == progress, locked = i > progress;
        glm::vec3 col = done ? glm::vec3(0.75f, 0.86f, 0.7f) : next ? glm::vec3(0.98f, 0.95f, 0.85f) : glm::vec3(0.35f, 0.34f, 0.32f);
        addBox(p, { 0.95f, 1.35f, 0.03f }, mat(col, 0.1f, 8.0f, next ? 0.5f + 0.0f : 0.0f), false);
        addBox(p + glm::vec3(0, 0.5f, 0.02f), { 0.75f, 0.12f, 0.02f }, dark, false);                       // 제목 줄
        for (int k = 0; k < i + 2; ++k) addBox(p + glm::vec3(-0.3f + k * 0.2f, 0.1f, 0.02f), { 0.15f, 0.15f, 0.02f }, dark, false);   // 세트 크기 표시
        if (done) addBox(p + glm::vec3(0, -0.4f, 0.025f), { 0.5f, 0.12f, 0.02f }, mat(glm::vec3(0.2f, 0.7f, 0.25f), 0, 1, 0.6f), false);
        if (locked) addBox(p + glm::vec3(0, -0.4f, 0.025f), { 0.5f, 0.12f, 0.02f }, mat(glm::vec3(0.5f, 0.12f, 0.1f)), false);
        std::string st = done ? "  [WRAPPED - RESHOOT]" : next ? "  [NEXT SHOOT]" : "  [NOT SCHEDULED]";
        interacts.push_back({ p, 0.6f, IK_SCHEDULE, i, stageLabel(i) + st });
    }
    // 거울 (+X 벽)
    addBox({ 4.47f, 1.6f, 0 }, { 0.06f, 2.1f, 1.2f }, dark, false);
    addBox({ 4.43f, 1.6f, 0 }, { 0.02f, 1.9f, 1.0f }, mat(glm::vec3(0.75f, 0.8f, 0.85f), 1.0f, 96.0f, 0.35f), false);
    interacts.push_back({ { 4.4f, 1.6f, 0 }, 0.9f, IK_MIRROR, 0, "MIRROR - CHARLIE AND HIS ABILITIES" });
    // 필름 선반 (-X 벽): 이미 찍은 영화 = 다단블럭
    for (float y : { 1.2f, 1.9f }) addBox({ -4.25f, y, -0.6f }, { 0.5f, 0.06f, 2.6f }, wood);
    Material can = mat(glm::vec3(0.7f, 0.72f, 0.75f), 1.0f, 64.0f);
    for (int i = 0; i < (int)library.size(); ++i) {
        glm::vec3 p(-4.25f, 1.23f + 0.07f + (i / 4) * 0.7f, -1.5f + (i % 4) * 0.6f);
        decos.push_back({ glm::translate(glm::mat4(1.0f), p), D_STATIC, 1, 0, glm::vec3(0.5f, 0.14f, 0.5f), can, 0, 0, 0, -1 });
        decos.push_back({ glm::translate(glm::mat4(1.0f), p + glm::vec3(0, 0.08f, 0)), D_STATIC, 1, 0, glm::vec3(0.2f, 0.03f, 0.2f), mat(glm::vec3(0.7f, 0.2f, 0.15f), 0, 1, 0.4f), 0, 0, 0, -1 });
        std::string t = library[i].fromStage >= 0 ? STAGE_TITLES[library[i].fromStage] : "FILM";
        interacts.push_back({ p, 0.4f, IK_SHELF, i, std::string("FILM: ") + t + " (" + library[i].name + ")" });
    }
    if (library.empty()) interacts.push_back({ { -4.25f, 1.5f, -0.6f }, 0.9f, IK_SHELF, -1, "FILM SHELF - NOTHING SHOT YET" });
    // 포스터 액자 (+Z 벽)
    for (int i = 0; i < STAGE_COUNT; ++i) {
        addBox({ -2.6f + i * 2.6f, 1.85f, 3.98f }, { 2.05f, 1.25f, 0.04f }, posterTex[i] ? dark : mat(glm::vec3(0.4f, 0.36f, 0.3f)), false);
    }
    // 천장 등
    decos.push_back({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 3.25f, 0.3f)), D_STATIC, 1, 0, glm::vec3(0.6f, 0.25f, 0.6f), mat(glm::vec3(1.0f, 0.9f, 0.7f), 0, 1, 3.0f), 0, 0, 0, -1 });
}

void updateHomeHover() {
    hoverInteract = -1;
    glm::vec3 eye = pl.pos + glm::vec3(0, 1.62f, 0);
    glm::vec3 f(sinf(camYaw) * cosf(camPitch), sinf(camPitch), -cosf(camYaw) * cosf(camPitch));
    float best = 3.5f;
    for (int i = 0; i < (int)interacts.size(); ++i) {
        glm::vec3 v = interacts[i].pos - eye;
        float t = glm::dot(v, f);
        if (t < 0.0f || t > best) continue;
        if (glm::length(v - f * t) < interacts[i].r) { best = t; hoverInteract = i; }
    }
}

void enterHome() {
    inHome = true;
    mode = MODE_HOME;
    seq = SEQ_NONE;
    transitioning = false;
    peeking = tabHeld = false;
    peekBlend = 0.0f;
    stageClear = false;
    pickups.clear();
    hasSpanner = false;
    buildHome();
    pl = Player{};
    pl.pos = glm::vec3(0, 0, 1.8f);
    camYaw = 0.0f;
    camPitch = -0.05f;
    lastSafe = SafeSpot{};
    clearFades();
    firstMouse = true;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// ===================================================================
// 촬영 (스테이지)
// ===================================================================
void startStage(int idx) {
    stage = idx;
    inHome = false;
    generateStage();
    selPiece = 0;
    buildWorld();
    chunkM.assign(pieces.size() + 2, glm::mat4(1.0f));
    resetPlayer();
    mode = MODE_SCENE;
    craneShown = false;
    posterCaptured = false;
    stageClear = false;
    peeking = tabHeld = false;
    peekBlend = 0.0f;
    deaths = 0;
    clearFades();
    firstMouse = true;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    std::cout << "촬영 시작: " << stageLabel(idx) << ", 블럭 조각 " << pieces.size() << "개\n";
}

void startIntro() { seq = SEQ_INTRO; seqT = 0.0f; helpT = 0.0f; }

void openEdit() {
    if (transitioning || stageClear) return;
    snapshotForEdit();
    mode = MODE_EDIT;
    rDown = false;
    if (selPiece < 0 || selPiece >= (int)pieces.size() || pieces[selPiece].placed) selectNextUnplaced(-1);
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void closeEdit() {
    mode = MODE_SCENE;
    firstMouse = true;
    glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    setCard("ACTION!", 0.8f);
    startTransition();
    if (!transitioning) buildWorld();
}

// 감독 모드로: 스테이지 첫 번째는 크레인을 타고 지붕 위로, 이후는 "컷!" 한 번에 빠르게
void beginDirector() {
    if (transitioning || stageClear || seq != SEQ_NONE) return;
    seq = SEQ_DIRECTOR;
    seqT = 0.0f;
    seqLong = !craneShown;
    setCard("CUT!", seqLong ? 0.9f : 0.5f);
}

void returnHome() {
    saveCustomBlock();
    progress = std::max(progress, std::min(stage + 1, STAGE_COUNT));
    std::string t = STAGE_TITLES[stage];
    enterHome();
    showToast("FILM WRAPPED: " + t);
}

void activateHover() {
    if (hoverInteract < 0) return;
    const Interact& it = interacts[hoverInteract];
    if (it.kind == IK_SCHEDULE) {
        if (it.idx > progress) { showToast("NOT ON THE SCHEDULE YET"); return; }
        pendingStage = it.idx;
        seq = SEQ_PACK;
        seqT = 0.0f;
    } else if (it.kind == IK_MIRROR) {
        seq = SEQ_MIRROR;
    } else {
        if (library.empty()) { showToast("NO FILMS ON THE SHELF YET"); return; }
        shelfIdx = std::max(0, it.idx);
        seq = SEQ_SHELF;
    }
}

void updateSequences(float dt) {
    camOv = false;
    wOverride.on = false;
    craneActive = false;
    fadeAmt = 0.0f;
    cardT = std::max(0.0f, cardT - dt);
    if (mode == MODE_HOME) {
        glm::vec3 eye = pl.pos + glm::vec3(0, 1.62f, 0);
        glm::vec3 f(sinf(camYaw) * cosf(camPitch), sinf(camPitch), -cosf(camYaw) * cosf(camPitch));
        canPos = CAN_DESK;
        if (seq == SEQ_PACK) {
            // 필름통을 챙겨 들고 → 암전 → 촬영장
            seqT += dt;
            float a = smooth01(seqT / 0.6f);
            glm::vec3 pos = glm::mix(eye, glm::vec3(0.5f, 1.65f, -1.9f), a), tgt = glm::mix(eye + f, CAN_DESK, a);
            setCamLook(pos, tgt);
            canPos = glm::mix(CAN_DESK, pos + glm::normalize(tgt - pos) * 0.6f - glm::vec3(0, 0.15f, 0), smooth01((seqT - 0.5f) / 0.7f));
            fadeAmt = smooth01((seqT - 0.9f) / 0.5f);
            if (seqT >= 1.45f) { startStage(pendingStage); startIntro(); fadeAmt = 1.0f; }
        } else if (seq == SEQ_MIRROR) {
            setCamLook(glm::vec3(4.3f, 1.4f, 0.0f), glm::vec3(2.3f, 1.05f, 0.0f));
            wOverride.on = true;
            wOverride.pos = glm::vec3(2.3f, 0.0f, 0.0f);
            wOverride.yaw = PI * 0.5f;
            wOverride.seated = false;
        } else {
            camOv = true;
            camOvPos = eye;
            camOvFwd = f;
            camOvFocus = eye + f;
        }
        return;
    }
    if (mode != MODE_SCENE) return;
    glm::vec3 tpsPos, tpsFwd, tpsFocus;
    tpsCamera(tpsPos, tpsFwd, tpsFocus);
    glm::vec3 c = sceneCenter();
    float W = N * CS, H = N * CS + 2.0f;

    // 엿보기: Tab 을 누르고 있는 동안 지붕 위에서 내려다본다
    peekBlend = glm::clamp(peekBlend + (peeking && seq == SEQ_NONE ? dt : -dt) * 4.0f, 0.0f, 1.0f);

    if (seq == SEQ_INTRO) {
        // 세트 전체 → 목적지(퇴근 기적) → 찰리 뒤
        seqT += dt;
        fadeAmt = 1.0f - smooth01(seqT / 0.4f);
        glm::vec3 p0 = c + glm::vec3(-W * 0.85f, H * 0.75f, -W * 0.85f);
        glm::vec3 gd = goalPos - c;
        gd.y = 0.0f;
        gd = glm::length(gd) > 0.1f ? glm::normalize(gd) : glm::vec3(0, 0, 1);
        glm::vec3 p1 = goalPos + gd * 9.0f + glm::vec3(0, 5.0f, 0);
        glm::vec3 pos, tgt;
        if (seqT < 1.6f) { float a = smooth01(seqT / 1.6f); pos = glm::mix(p0, p1, a) + glm::vec3(0, sinf(a * PI) * W * 0.25f, 0); tgt = glm::mix(c, goalPos, a); }
        else if (seqT < 2.5f) { float a = (seqT - 1.6f) / 0.9f; pos = glm::mix(p1, goalPos + gd * 6.0f + glm::vec3(0, 3.0f, 0), a * 0.5f); tgt = goalPos; }
        else { float a = smooth01((seqT - 2.5f) / 1.3f); pos = glm::mix(goalPos + gd * 7.5f + glm::vec3(0, 4.0f, 0), tpsPos, a); tgt = glm::mix(goalPos, tpsFocus, a); }
        setCamLook(pos, tgt);
        if (seqT >= 3.8f) { seq = SEQ_NONE; setCard("ACTION!", 0.8f); helpT = 10.0f; camOv = false; }
    } else if (seq == SEQ_DIRECTOR) {
        seqT += dt;
        glm::vec3 over = overheadPos();
        if (!seqLong) {
            float a = smooth01(seqT / 0.4f);
            camOv = true;
            camOvPos = glm::mix(tpsPos, over, a);
            camOvFwd = glm::normalize(glm::mix(tpsFwd, glm::normalize(c - over), a));
            camOvFocus = glm::mix(tpsFocus, c, a);
            if (seqT >= 0.4f) { seq = SEQ_NONE; openEdit(); }
        } else {
            // 크레인이 내려와 찰리를 태우고 지붕 위로
            craneActive = true;
            glm::vec3 seat = pl.pos + glm::vec3(0, 0.55f, 0), above(pl.pos.x, H + 9.0f, pl.pos.z);
            glm::vec3 tipAtSeat = seat + glm::vec3(0, 2.2f, 0), rest = craneRestTip();
            glm::vec3 horiz = glm::normalize(glm::vec3(tpsFwd.x, 0.0f, tpsFwd.z) + glm::vec3(0.0001f));
            if (seqT < 0.5f) craneTip = glm::mix(rest, above, smooth01(seqT / 0.5f));
            else if (seqT < 1.0f) craneTip = glm::mix(above, tipAtSeat, smooth01((seqT - 0.5f) / 0.5f));
            else if (seqT < 1.25f) craneTip = tipAtSeat;
            else craneTip = glm::mix(tipAtSeat, above + glm::vec3(0, 4.0f, 0), smooth01((seqT - 1.25f) / 1.15f));
            if (seqT >= 1.0f) {
                wOverride.on = true;
                wOverride.pos = craneTip - glm::vec3(0, 2.2f, 0) - glm::vec3(0, 0.45f, 0);
                wOverride.yaw = pl.bodyYaw;
                wOverride.seated = true;
            }
            glm::vec3 seatNow = craneTip - glm::vec3(0, 2.2f, 0);
            glm::vec3 toC = glm::vec3(c.x - pl.pos.x, 0.0f, c.z - pl.pos.z);
            toC = glm::length(toC) > 0.5f ? glm::normalize(toC) : -horiz;
            // 크레인이 내려오는 동안은 찰리를, 태운 뒤에는 올라가는 의자를 본다
            glm::vec3 follow = seqT < 1.25f ? seat : seatNow;
            glm::vec3 watch = follow + toC * 7.0f + glm::vec3(0, 1.8f, 0);
            if (seqT < 2.4f) {
                float a = smooth01(seqT / 0.5f);
                camOv = true;
                camOvPos = glm::mix(tpsPos, watch, a);
                camOvFocus = glm::mix(tpsFocus, follow + glm::vec3(0, 0.8f, 0), a);
                camOvFwd = glm::normalize(camOvFocus - camOvPos);
            } else {
                float a = smooth01((seqT - 2.4f) / 0.4f);
                glm::vec3 from = watch;
                camOv = true;
                camOvPos = glm::mix(from, over, a);
                camOvFwd = glm::normalize(glm::mix(glm::normalize(seatNow - from), glm::normalize(c - over), a));
                camOvFocus = glm::mix(seatNow, c, a);
            }
            if (seqT >= 2.8f) { seq = SEQ_NONE; craneShown = true; openEdit(); }
        }
    } else if (seq == SEQ_THE_END) {
        seqT += dt;
        float ang = seqT * 0.22f + 0.7f;
        setCamLook(c + glm::vec3(sinf(ang) * W * 0.9f, H * 0.55f + W * 0.2f, cosf(ang) * W * 0.9f), c);
        if (!posterCaptured && seqT > 0.7f) capturePosterNow = true;
    } else if (peekBlend > 0.001f) {
        float a = smooth01(peekBlend);
        glm::vec3 over = overheadPos();
        camOv = true;
        camOvPos = glm::mix(tpsPos, over, a);
        camOvFwd = glm::normalize(glm::mix(tpsFwd, glm::normalize(c - over), a));
        camOvFocus = glm::mix(tpsFocus, c, a);
    }
}

void capturePoster(int w, int h) {
    if (stage < 0 || stage >= STAGE_COUNT) return;
    if (!posterTex[stage]) glGenTextures(1, &posterTex[stage]);
    glBindTexture(GL_TEXTURE_2D, posterTex[stage]);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, w, h, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    posterCaptured = true;
    capturePosterNow = false;
    std::cout << "포스터 촬영: " << STAGE_TITLES[stage] << "\n";
}

// ===================================================================
// 홈 화면 / 필름 선반 보기 / 자막 카드 / 씬 HUD
// ===================================================================
void renderShelfView(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.07f, 0.06f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(cubeProg);
    const Piece& p = library[shelfIdx];
    glm::vec3 ext = glm::vec3(shapeExtent(p)) + 1.0f;
    float e = std::max(ext.x, std::max(ext.y, ext.z));
    cubeVP = glm::perspective(glm::radians(32.0f), (float)w / h, 0.1f, 100.0f) * glm::lookAt(glm::vec3(0.6f, 0.55f, 1.0f) * (e * 1.9f + 1.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0)) *
             glm::rotate(glm::mat4(1.0f), globalTime * 0.4f, glm::vec3(0, 1, 0));
    drawPieceVis(p, -(ext - 1.0f) * 0.5f, 1.0f, glm::vec3(0), 0.0f, 0.0f);
    glBindVertexArray(0);
}

void drawCardHUD(int w, int h) {
    if (cardT <= 0.0f) return;
    float ui = h / 720.0f, a = std::min(1.0f, cardT * 5.0f);
    Batch2D b;
    float bw = 420 * ui, bh = 170 * ui, x = (w - bw) * 0.5f, y = (h - bh) * 0.5f;
    b.rect(x, y, bw, bh, glm::vec4(0.02f, 0.02f, 0.02f, 0.92f * a));
    b.frame(x + 8 * ui, y + 8 * ui, bw - 16 * ui, bh - 16 * ui, 2 * ui, glm::vec4(0.95f, 0.92f, 0.82f, a));
    b.frame(x + 14 * ui, y + 14 * ui, bw - 28 * ui, bh - 28 * ui, 1 * ui, glm::vec4(0.95f, 0.92f, 0.82f, 0.6f * a));
    drawTextCentered(b, w * 0.5f, h * 0.5f - 18 * ui, 6.0f * ui, cardText, glm::vec4(0.97f, 0.94f, 0.85f, a));
    flushHUD(b, w, h);
}

void renderHomeHUD(int w, int h) {
    float ui = h / 720.0f, cx = w * 0.5f;
    Batch2D b;
    if (seq == SEQ_SHELF) {
        const Piece& p = library[shelfIdx];
        std::string t = p.fromStage >= 0 ? STAGE_TITLES[p.fromStage] : "FILM";
        drawTextCentered(b, cx, h - 50 * ui, 2.6f * ui, "FILM " + std::to_string(shelfIdx + 1) + "/" + std::to_string(library.size()) + ": " + t, C_GOLD);
        drawTextCentered(b, cx, h - 76 * ui, 1.6f * ui, "YOUR CUT, SAVED AS A " + p.name.substr(3) + " MULTI BLOCK", C_WHITE);
        drawTextCentered(b, cx, 30 * ui, 1.5f * ui, "A / D  BROWSE      ESC / CLICK  BACK", C_DIM);
    } else if (seq == SEQ_MIRROR) {
        float x = 40 * ui, y = h - 80 * ui;
        b.rect(x - 16 * ui, y - 250 * ui, 560 * ui, 290 * ui, glm::vec4(0, 0, 0, 0.55f));
        drawText(b, x, y, 2.6f * ui, "CHARLIE", C_GOLD);
        drawText(b, x, y - 26 * ui, 1.6f * ui, "FACTORY WORKER. ACTOR. DIRECTOR.", C_WHITE);
        drawText(b, x, y - 64 * ui, 1.8f * ui, "ABILITIES", C_GOLD);
        drawText(b, x, y - 90 * ui, 1.5f * ui, "JUMP - PRESS AGAIN IN THE AIR TO DOUBLE JUMP", C_WHITE);
        drawText(b, x, y - 112 * ui, 1.5f * ui, "SUSPENDERS - FOUND ON SET. PULL PILLARS OUT OF", C_WHITE);
        drawText(b, x, y - 130 * ui, 1.5f * ui, "             MULTI BLOCKS, SET THEM ON SOCKETS", C_WHITE);
        drawText(b, x, y - 158 * ui, 1.5f * ui, "MORE ABILITIES COMING SOON", C_DIM);
        drawText(b, x, y - 192 * ui, 1.5f * ui, "FILMS WRAPPED: " + std::to_string(progress) + "/" + std::to_string(STAGE_COUNT), C_BLUE);
        drawText(b, x, y - 216 * ui, 1.4f * ui, "ESC / CLICK  BACK", C_DIM);
    } else if (seq == SEQ_NONE) {
        b.rect(cx - 2 * ui, h * 0.5f - 2 * ui, 4 * ui, 4 * ui, glm::vec4(1, 1, 1, 0.8f));
        drawText(b, 20 * ui, h - 40 * ui, 2.6f * ui, "CHARLIE'S ROOM", C_WHITE);
        drawText(b, 20 * ui, h - 64 * ui, 1.5f * ui, "FILMS WRAPPED " + std::to_string(progress) + "/" + std::to_string(STAGE_COUNT), C_GOLD);
        if (hoverInteract >= 0) drawTextCentered(b, cx, h * 0.5f - 46 * ui, 1.8f * ui, "CLICK - " + interacts[hoverInteract].label, C_GOLD);
        else if (progress < STAGE_COUNT) drawTextCentered(b, cx, h * 0.5f - 46 * ui, 1.5f * ui, "THE SHOOTING SCHEDULE IS ON THE BOARD", C_DIM);
        else drawTextCentered(b, cx, h * 0.5f - 46 * ui, 1.8f * ui, "ALL FILMS WRAPPED - THAT'S A WRAP!", C_GREEN);
        std::string tip = "WASD MOVE   MOUSE LOOK   CLICK / E USE   ESC QUIT   F1 FILM";
        drawText(b, w - textWidth(tip, 1.4f * ui) - 20 * ui, 14 * ui, 1.4f * ui, tip, C_DIM);
    }
    if (toastT > 0.0f) drawTextCentered(b, cx, h * 0.72f, 2.0f * ui, toast, glm::vec4(1, 0.85f, 0.4f, std::min(1.0f, toastT)));
    flushHUD(b, w, h);
}

void renderSceneHUD(int w, int h) {
    float ui = h / 720.0f, cx = w * 0.5f;
    Batch2D b;
    bool cinematic = seq == SEQ_INTRO || seq == SEQ_THE_END || (seq == SEQ_DIRECTOR && seqLong);
    if (cinematic) {
        // 영화 화면비 띠
        b.rect(0, 0, (float)w, h * 0.1f, glm::vec4(0, 0, 0, 1));
        b.rect(0, h * 0.9f, (float)w, h * 0.1f, glm::vec4(0, 0, 0, 1));
    }
    if (seq == SEQ_INTRO) {
        drawTextCentered(b, cx, h * 0.93f, 2.4f * ui, stageLabel(stage), glm::vec4(0.97f, 0.94f, 0.85f, 1));
        std::string sub = seqT < 1.6f ? "THE SET" : seqT < 2.6f ? "DESTINATION: THE GOAL POINT" : "AND OUR STAR...";
        drawTextCentered(b, cx, h * 0.035f, 1.8f * ui, sub, glm::vec4(0.97f, 0.94f, 0.85f, 0.9f));
        drawText(b, w - textWidth("ANY KEY SKIP", 1.3f * ui) - 16 * ui, h * 0.035f, 1.3f * ui, "ANY KEY SKIP", C_DIM);
    } else if (seq == SEQ_THE_END) {
        float a = std::min(1.0f, seqT);
        drawTextCentered(b, cx, h * 0.62f, 7.0f * ui, "THE END", glm::vec4(0.97f, 0.94f, 0.85f, a));
        drawTextCentered(b, cx, h * 0.52f, 2.2f * ui, std::string(STAGE_TITLES[stage]) + " - A FILM BY CHARLIE", glm::vec4(0.97f, 0.94f, 0.85f, a));
        drawTextCentered(b, cx, h * 0.46f, 1.6f * ui, "THIS " + gridLabel(N) + " SET IS SAVED AS YOUR MULTI BLOCK", glm::vec4(1, 1, 1, 0.8f * a));
        if (seqT > 1.2f) drawTextCentered(b, cx, h * 0.035f, 1.8f * ui, "ENTER / CLICK - BACK HOME", glm::vec4(0.5f, 1.0f, 0.55f, 0.7f + 0.3f * sinf(globalTime * 5.0f)));
    } else if (!(seq == SEQ_DIRECTOR)) {
        drawText(b, 20 * ui, h - 40 * ui, 2.2f * ui, stageLabel(stage), C_WHITE);
        drawStatus(b, w, h, ui);
        std::string room = currentRoomName();
        if (!room.empty() && peekBlend < 0.5f) drawTextCentered(b, cx, h - 40 * ui, 2.0f * ui, room, glm::vec4(1, 0.92f, 0.75f, 0.85f));
        if (transitioning) drawTextCentered(b, cx, h * 0.82f, 2.2f * ui, "RESETTING THE SET...  (SPACE SKIP)", glm::vec4(1.0f, 0.85f, 0.35f, 0.8f + 0.2f * sinf(globalTime * 8.0f)));
        else if (peekBlend > 0.5f) drawTextCentered(b, cx, h * 0.1f, 1.8f * ui, "PEEKING - RELEASE TAB TO RETURN", C_BLUE);
        else {
            if (pl.inLift) drawTextCentered(b, cx, h * 0.3f, 1.8f * ui, "STEAM LIFT - SPACE UP / CTRL DOWN", C_BLUE);
            if (pl.onLadder) drawTextCentered(b, cx, h * 0.3f, 1.8f * ui, "LADDER - W UP / S DOWN", C_BLUE);
            int spi, ssi;
            if (carrying) { if (nearestSocket(false, spi, ssi)) drawTextCentered(b, cx, h * 0.34f, 2.0f * ui, "E  SET PILLAR", C_GOLD); }
            else if (nearestSocket(true, spi, ssi)) drawTextCentered(b, cx, h * 0.34f, 2.0f * ui, hasSpanner ? "E  PULL PILLAR" : "BOLTED PILLAR - NEED THE SUSPENDERS", hasSpanner ? C_BLUE : C_DIM);
        }
        drawText(b, 16 * ui, 14 * ui, 1.3f * ui, std::to_string((int)fps) + " FPS", glm::vec4(1, 1, 1, 0.45f));
        std::string tip = "TAB DIRECTOR MODE   E PILLAR   R START POINT   ESC HOME";
        drawText(b, w - textWidth(tip, 1.4f * ui) - 20 * ui, 14 * ui, 1.4f * ui, tip, C_DIM);
        if (helpT > 0.0f) {
            glm::vec4 hc(1, 1, 1, std::min(1.0f, helpT) * 0.9f);
            drawTextCentered(b, cx, h * 0.2f, 1.7f * ui, "WASD MOVE   MOUSE LOOK   SPACE JUMP (AGAIN IN THE AIR = DOUBLE JUMP)   SHIFT RUN", hc);
            drawTextCentered(b, cx, h * 0.2f - 20 * ui, 1.7f * ui, "THE DARK IS AN EMPTY SET - TAB INTO DIRECTOR MODE AND BUILD THE SET WITH BLOCKS", hc);
            drawTextCentered(b, cx, h * 0.2f - 40 * ui, 1.7f * ui, "GET EVERY COG, FILL THE WHOLE SET, THEN REACH THE GOAL POINT AT THE TOP", hc);
        }
    }
    if (toastT > 0.0f && seq != SEQ_THE_END) drawTextCentered(b, cx, h * 0.72f, 2.0f * ui, toast, glm::vec4(1, 0.85f, 0.4f, std::min(1.0f, toastT)));
    flushHUD(b, w, h);
    drawCardHUD(w, h);
}

// ===================================================================
// 입력
// ===================================================================
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (mode == MODE_SCENE && key == GLFW_KEY_TAB) {
        if (action == GLFW_PRESS && seq == SEQ_NONE && !transitioning && !stageClear) beginDirector();
        return;
    }
    if (action != GLFW_PRESS) return;
    if (mode == MODE_HOME) {
        if (seq == SEQ_MIRROR || seq == SEQ_SHELF) {
            if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_E || key == GLFW_KEY_ENTER) seq = SEQ_NONE;
            if (seq == SEQ_SHELF && !library.empty()) {
                int n = (int)library.size();
                if (key == GLFW_KEY_A || key == GLFW_KEY_LEFT) shelfIdx = (shelfIdx + n - 1) % n;
                if (key == GLFW_KEY_D || key == GLFW_KEY_RIGHT) shelfIdx = (shelfIdx + 1) % n;
            }
            return;
        }
        if (seq != SEQ_NONE) return;
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, true);
        else if (key == GLFW_KEY_E || key == GLFW_KEY_ENTER) activateHover();
        return;
    }
    if (mode == MODE_EDIT) {
        switch (key) {
        case GLFW_KEY_ESCAPE: case GLFW_KEY_TAB: case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: closeEdit(); break;
        case GLFW_KEY_Q: case GLFW_KEY_E: case GLFW_KEY_W: case GLFW_KEY_S: case GLFW_KEY_A: case GLFW_KEY_D: rotateSel(key); break;
        case GLFW_KEY_Z: selectNextUnplaced(selPiece < 0 ? 0 : selPiece, -1); break;
        case GLFW_KEY_X: selectNextUnplaced(selPiece < 0 ? -1 : selPiece, 1); break;
        case GLFW_KEY_BACKSPACE: for (auto& p : pieces) p.placed = false; rebuildOcc(); selPiece = 0; break;
        case GLFW_KEY_F5: autoSolve(); showToast("AUTO SOLVED (TEST)"); break;
        default:
            if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && key - GLFW_KEY_1 < (int)pieces.size()) selPiece = key - GLFW_KEY_1;
        }
        return;
    }
    // 연기 모드
    if (seq == SEQ_INTRO) { seqT = std::max(seqT, 3.8f); return; }
    if (seq == SEQ_THE_END) { if (seqT > 1.2f && (key == GLFW_KEY_ENTER || key == GLFW_KEY_SPACE || key == GLFW_KEY_KP_ENTER)) returnHome(); return; }
    if (seq != SEQ_NONE) return;
    if (transitioning) { if (key == GLFW_KEY_SPACE || key == GLFW_KEY_ENTER || key == GLFW_KEY_E) trT = trTotal; return; }
    if (key == GLFW_KEY_ESCAPE) enterHome();
    else if (key == GLFW_KEY_R) { spawnAtStart(); lastSafe = SafeSpot{}; }
    else if (key == GLFW_KEY_E) tryPillar();
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (mode == MODE_HOME) {
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
        if (seq == SEQ_MIRROR || seq == SEQ_SHELF) seq = SEQ_NONE;
        else if (seq == SEQ_NONE) activateHover();
        return;
    }
    if (mode == MODE_SCENE) {
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;
        if (seq == SEQ_INTRO) seqT = std::max(seqT, 3.8f);
        else if (seq == SEQ_THE_END && seqT > 1.2f) returnHome();
        else if (transitioning) trT = trTotal;
        return;
    }
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        int s = slotAt(mouseFb());
        if (s >= 0) { if (!pieces[s].placed) selPiece = s; else removePiece(s); return; }
        placeSelected();
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) { rDown = true; rMoved = false; rStartX = lastMX = mx; rStartY = lastMY = my; }
        else if (action == GLFW_RELEASE) { if (!rMoved && hoverPiece >= 0) removePiece(hoverPiece); rDown = false; }
    }
}

void cursor_callback(GLFWwindow* window, double x, double y) {
    if (mode == MODE_EDIT) {
        if (rDown) {
            if (fabs(x - rStartX) + fabs(y - rStartY) > 5.0) rMoved = true;
            asmYaw -= (float)(x - lastMX) * 0.008f;
            asmPitch = glm::clamp(asmPitch + (float)(y - lastMY) * 0.008f, -0.3f, 1.45f);
        }
        lastMX = x;
        lastMY = y;
        return;
    }
    if (firstMouse) { lastMouseX = x; lastMouseY = y; firstMouse = false; }
    double dx = x - lastMouseX, dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;
    if (fabs(dx) > 300.0 || fabs(dy) > 300.0) return;
    bool free = (mode == MODE_HOME && seq == SEQ_NONE) || (mode == MODE_SCENE && (seq == SEQ_NONE || transitioning));
    if (!free) return;
    camYaw += (float)dx * 0.0025f;
    camPitch = glm::clamp(camPitch - (float)dy * 0.0025f, mode == MODE_HOME ? -1.4f : -1.3f, mode == MODE_HOME ? 1.4f : 1.0f);
}

void scroll_callback(GLFWwindow* window, double xo, double yo) {
    if (mode == MODE_EDIT) asmZoom = glm::clamp(asmZoom * (1.0f - (float)yo * 0.08f), 0.45f, 2.2f);
    else if (mode == MODE_SCENE) camDist = glm::clamp(camDist - (float)yo * 0.5f, 2.5f, 12.0f);
}

// ===================================================================
// 셀프 테스트 (--shot)
// ===================================================================
void saveScreenshot(int w, int h, const std::string& path) {
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) out.write((const char*)&px[(size_t)y * w * 3], (std::streamsize)w * 3);
}

void collectAll() {
    for (auto& p : pickups) {
        if (p.taken) continue;
        pl.pos = p.pos - glm::vec3(0, PLAYER_H * 0.5f, 0);
        pl.vel = glm::vec3(0.0f);
        updatePlayer(0.0001f);
    }
}

void finishStageForTest() {
    collectAll();
    pl.pos = goalPos - glm::vec3(0, PLAYER_H * 0.5f, 0);
    updatePlayer(0.0001f);
    std::cout << "[test] cogs " << cogsTaken() << "/" << cogsTotal() << " full " << gridComplete() << " clear " << stageClear << "\n";
}

void lookAtHome(glm::vec3 p) {
    glm::vec3 d = glm::normalize(p - (pl.pos + glm::vec3(0, 1.62f, 0)));
    camYaw = atan2f(d.x, -d.z);
    camPitch = asinf(d.y);
}

struct Step { int delay; std::function<bool()> fn; };
std::vector<Step> script;
size_t scriptIdx = 0;
int scriptWait = 0, shotW = 0, shotH = 0;
void shot(const std::string& n) { saveScreenshot(shotW, shotH, "shot_" + n + ".ppm"); std::cout << "[shot] " << n << "\n"; }

void stats() {
    int cells = 0, bad = 0, opt = 0;
    int optCells = 0, multi = 0, doors = 0;
    for (auto& p : pieces) { if (p.inSolution) cells += (int)p.cells.size(); else { ++opt; optCells += (int)p.cells.size(); } if (!p.custom && p.cells.size() > 4) ++bad; if (p.custom) { ++multi; for (auto& c : p.cells) doors += c.role != 0; } }
    std::cout << "[test] spare cells " << optCells << " multi " << multi << " doors " << doors << "\n";
    std::cout << "[test] stage " << stage + 1 << " N " << N << " pieces " << pieces.size() << " (optional " << opt << ") cells " << cells + 2 << "/" << N * N * N
              << " pieces>4: " << bad << " cogs " << cogsTotal() << " suspenders " << (int)pickups.size() - cogsTotal() << "\n";
}

void buildScript() {
    auto S = [](int d, std::function<bool()> f) { script.push_back({ d, f }); };
    auto waitTr = []() { return !transitioning; };
    // 홈
    S(5, [] { std::cout << "[test] home interacts " << interacts.size() << "\n"; shot("h1_home"); return true; });
    S(1, [] { pl.pos = glm::vec3(-0.6f, 0.0f, -1.6f); lookAtHome(interacts[0].pos); return true; });
    S(2, [] { updateHomeHover(); std::cout << "[test] hover " << (hoverInteract >= 0 ? interacts[hoverInteract].label : "none") << "\n"; shot("h2_board_hover"); return true; });
    S(1, [] { seq = SEQ_MIRROR; return true; });
    S(3, [] { shot("h3_mirror"); seq = SEQ_NONE; return true; });
    S(1, [] { lookAtHome(interacts[0].pos); updateHomeHover(); activateHover(); return true; });
    S(40, [] { shot("h4_pack"); return true; });
    S(0, [] { return mode == MODE_SCENE && seq == SEQ_INTRO; });
    S(50, [] { stats(); shot("s1_intro_set"); return true; });
    S(70, [] { shot("s1_intro_goal"); return true; });
    S(0, [] { return seq == SEQ_NONE; });
    S(8, [] { shot("s1_action"); beginDirector(); return true; });
    S(45, [] { shot("s1_crane_pickup"); return true; });
    S(50, [] { shot("s1_crane_up"); return true; });
    S(0, [] { return mode == MODE_EDIT; });
    S(3, [] { shot("s1_director_mode"); autoSolve(); closeEdit(); return true; });
    S(30, [] { shot("s1_reassemble"); return true; });
    S(0, waitTr);
    S(30, [] { finishStageForTest(); return true; });
    S(0, [] { return seq == SEQ_THE_END; });
    S(80, [] { std::cout << "[test] poster " << (posterTex[0] != 0) << "\n"; shot("s1_the_end"); returnHome(); return true; });
    S(10, [] { std::cout << "[test] progress " << progress << " library " << library.size() << "\n"; camYaw = PI; camPitch = 0.0f; return true; });
    S(5, [] { shot("h5_poster_wall"); camYaw = -PI * 0.5f; camPitch = -0.1f; return true; });
    S(5, [] { shot("h6_shelf"); seq = SEQ_SHELF; shelfIdx = 0; return true; });
    S(5, [] { shot("h7_shelf_view"); seq = SEQ_NONE; return true; });
    // 3x3x3: 다단블럭 필수
    S(1, [] { startStage(1); startIntro(); seqT = 3.8f; stats(); return true; });
    S(0, [] { return seq == SEQ_NONE; });
    S(5, [] { beginDirector(); return true; });
    S(0, [] { return mode == MODE_EDIT; });
    S(3, [] { shot("s2_director_mode"); autoSolve(); closeEdit(); return true; });
    S(0, waitTr);
    S(20, [] { shot("s2_scene"); return true; });
    // 기둥: 다단블럭에서 뽑아 빈 소켓에 세운다
    S(1, [] {
        int pi, si;
        hasSpanner = true;
        if (!nearestSocket(true, pi, si)) {
            // 꽂힌 기둥 옆으로 순간이동
            for (int p = 0; p < (int)pieces.size(); ++p)
                for (auto& so : pieces[p].sockets)
                    if (so.filled && pieces[p].placed) { pl.pos = pieceCenter(pieces[p]) + pieces[p].rot * so.local + glm::vec3(1.6f, 0.2f, 0); pl.vel = glm::vec3(0); goto found; }
        found:;
        }
        int filled0 = 0;
        for (auto& p : pieces) for (auto& so : p.sockets) filled0 += so.filled;
        tryPillar();
        std::cout << "[test] pillars before " << filled0 << " carrying " << carrying << "\n";
        return true;
    });
    S(10, [] { shot("s2_carry");
        for (int p = 0; p < (int)pieces.size(); ++p)
            for (auto& so : pieces[p].sockets)
                if (!so.filled && pieces[p].placed && !pieces[p].custom) { pl.pos = pieceCenter(pieces[p]) + pieces[p].rot * so.local + glm::vec3(1.6f, 0.2f, 0); pl.vel = glm::vec3(0); goto found2; }
        found2:
        tryPillar();
        int filled1 = 0, sockets = 0;
        for (auto& p : pieces) for (auto& so : p.sockets) { filled1 += so.filled; ++sockets; }
        std::cout << "[test] pillars after " << filled1 << " sockets " << sockets << " carrying " << carrying << "\n";
        return true; });
    S(10, [] { shot("s2_pillar_set"); beginDirector(); return true; });
    S(0, [] { return mode == MODE_EDIT; });
    S(3, [] { shot("s2_director_marker"); closeEdit(); return true; });
    S(0, waitTr);
    S(5, [] { finishStageForTest(); return true; });
    S(0, [] { return seq == SEQ_THE_END; });
    S(60, [] { returnHome(); return true; });
    // 4x4x4: 다단블럭 선택
    S(1, [] { startStage(2); startIntro(); seqT = 3.8f; stats(); return true; });
    S(0, [] { return seq == SEQ_NONE; });
    S(5, [] { beginDirector(); return true; });
    S(0, [] { return mode == MODE_EDIT; });
    S(3, [] { shot("s3_director_mode"); autoSolve(); closeEdit(); return true; });
    S(0, waitTr);
    S(20, [] { shot("s3_scene"); beginDirector(); std::cout << "[test] second director entry long=" << seqLong << "\n"; return true; });
    S(12, [] { shot("s3_quick_cut"); return true; });
    S(0, [] { return mode == MODE_EDIT; });
    S(2, [] { closeEdit(); return true; });
    S(0, waitTr);
    S(5, [] { finishStageForTest(); return true; });
    S(0, [] { return seq == SEQ_THE_END; });
    S(60, [] { returnHome(); return true; });
    S(5, [] { camYaw = PI; camPitch = 0.0f; return true; });
    S(5, [] { shot("h8_all_posters"); std::cout << "[test] progress " << progress << " library " << library.size() << "\n"; glfwSetWindowShouldClose(gWindow, true); return true; });
}

void runScript(int w, int h) {
    shotW = w; shotH = h;
    if (scriptIdx >= script.size()) return;
    if (scriptWait < script[scriptIdx].delay) { ++scriptWait; return; }
    if (script[scriptIdx].fn()) { ++scriptIdx; scriptWait = 0; }
}

// ===================================================================
// main
// ===================================================================
int main(int argc, char** argv) {
    bool shotMode = argc > 1 && std::string(argv[1]) == "--shot";
    rng.seed(shotMode ? 4242u : std::random_device{}());
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Prototype - Director's Cut", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    gWindow = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { glfwDestroyWindow(window); glfwTerminate(); return -1; }
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    sceneProg = makeProgram("scene_vertex.glsl", "scene_fragment.glsl");
    shadowProg = makeProgram("shadow_vertex.glsl", "shadow_fragment.glsl");
    skyProg = makeProgram("post_vertex.glsl", "sky_fragment.glsl");
    brightProg = makeProgram("post_vertex.glsl", "bright_fragment.glsl");
    blurProg = makeProgram("post_vertex.glsl", "blur_fragment.glsl");
    compositeProg = makeProgram("post_vertex.glsl", "composite_fragment.glsl");
    hudProg = makeProgram("hud_vertex.glsl", "hud_fragment.glsl");
    cubeProg = makeProgram("cube_vertex.glsl", "cube_fragment.glsl");
    posterProg = makeProgram("poster_vertex.glsl", "poster_fragment.glsl");
    if (!sceneProg || !shadowProg || !skyProg || !brightProg || !blurProg || !compositeProg || !hudProg || !cubeProg || !posterProg) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    sl.model = U(sceneProg, "model"); sl.view = U(sceneProg, "view"); sl.proj = U(sceneProg, "proj");
    sl.lightVP = U(sceneProg, "lightVP"); sl.normalMat = U(sceneProg, "normalMat"); sl.objColor = U(sceneProg, "objColor");
    sl.Ks = U(sceneProg, "Ks"); sl.shininess = U(sceneProg, "shininess"); sl.emissive = U(sceneProg, "emissive");
    sl.pattern = U(sceneProg, "pattern"); sl.sunDir = U(sceneProg, "sunDir"); sl.sunColor = U(sceneProg, "sunColor");
    sl.skyAmb = U(sceneProg, "skyAmb"); sl.groundAmb = U(sceneProg, "groundAmb"); sl.viewPos = U(sceneProg, "viewPos");
    sl.fogColor = U(sceneProg, "fogColor"); sl.fogDensity = U(sceneProg, "fogDensity"); sl.farPlane = U(sceneProg, "farPlane");
    sl.shadowMap = U(sceneProg, "shadowMap"); sl.pointPos = U(sceneProg, "pointPos"); sl.pointColor = U(sceneProg, "pointColor");
    sl.pointPower = U(sceneProg, "pointPower");
    sl.alpha = U(sceneProg, "alpha");
    shadowModelLoc = U(shadowProg, "model");
    shadowVPLoc = U(shadowProg, "lightVP");

    cubeMesh = buildCube();
    cylMesh = buildCylinder(20);
    glGenVertexArrays(1, &emptyVao);
    glGenVertexArrays(1, &hudVao);
    glGenBuffers(1, &hudVbo);
    glBindVertexArray(hudVao);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // 포스터용 사각형 (위치 + UV)
    float quad[] = { -0.5f, -0.5f, 0, 0, 0,  0.5f, -0.5f, 0, 1, 0,  0.5f, 0.5f, 0, 1, 1,  -0.5f, -0.5f, 0, 0, 0,  0.5f, 0.5f, 0, 1, 1,  -0.5f, 0.5f, 0, 0, 1 };
    glGenVertexArrays(1, &posterVao);
    glGenBuffers(1, &posterVbo);
    glBindVertexArray(posterVao);
    glBindBuffer(GL_ARRAY_BUFFER, posterVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    createShadowMap();

    initRotations();
    initThemes();
    N = 2;
    rebuildOcc();
    enterHome();
    if (shotMode) {
        for (int t = 0; t < 10; ++t) {
            const auto& R = ROUTE[t];
            if (R.empty()) continue;
            float maxRise = 0.0f, maxGap = 0.0f;
            for (size_t i = 1; i < R.size(); ++i) {
                maxRise = std::max(maxRise, R[i].h - R[i - 1].h);
                maxGap = std::max(maxGap, glm::length(glm::vec2(R[i].x - R[i - 1].x, R[i].z - R[i - 1].z)) - 2.2f);
            }
            std::cout << "[route] " << THEMES[t].name << " steps " << R.size() << " top " << R.back().h << " exit tile " << EXIT_T[t][0] << "," << EXIT_T[t][1]
                      << " first tile solid " << (THEMES[t].fl[tileOf(R[0].z)][tileOf(R[0].x)] == 'X') << " max rise " << maxRise << " max gap " << maxGap << std::endl;
        }
        buildScript();
    }

    std::cout << "=== Prototype 3 : Director's Cut ===\n"
              << "[찰리의 방] WASD 이동, 마우스 시점, 클릭/E 사용 (스케줄 보드, 거울, 필름 선반)\n"
              << "[연기 모드] WASD 이동, Space 점프 (달리며 연속 3단), Tab 짧게 = 감독 모드, Tab 누르고 있기 = 엿보기, Esc 홈\n"
              << "[감독 모드] 좌클릭 배치, 우클릭 회수, 우드래그 시점, 휠 확대, Q/E W/S A/D 회전, Tab/Enter = 액션!\n"
              << "F1 필름 효과\n";

    double lastTime = glfwGetTime();
    float fpsAcc = 0.0f;
    int fpsFrames = 0;
    while (!glfwWindowShouldClose(window)) {
        double curTime = glfwGetTime();
        float dt = (float)(curTime - lastTime);
        lastTime = curTime;
        fpsAcc += dt;
        ++fpsFrames;
        if (fpsAcc >= 0.5f) { fps = fpsFrames / fpsAcc; fpsAcc = 0.0f; fpsFrames = 0; }
        if (shotMode) dt = 1.0f / 60.0f;
        if (dt > 0.05f) dt = 0.05f;
        frameDt = dt;
        globalTime += dt;
        toastT -= dt;

        if (mode == MODE_HOME) {
            if (seq == SEQ_NONE) { updatePlayer(dt); updateHomeHover(); }
        } else if (mode == MODE_SCENE) {
            helpT -= dt;
            if (stageClear) clearT += dt;
            if (stageClear && seq != SEQ_THE_END) { seq = SEQ_THE_END; seqT = 0.0f; peeking = false; }
            if (transitioning) updateTransition(dt);
            else if (seq == SEQ_NONE && !peeking && peekBlend < 0.3f) {
                camDistBoost = std::max(0.0f, camDistBoost - dt * 4.0f);
                if (!stageClear) updatePlayer(dt);
            }
            updateEffects(dt);
        }
        updateSequences(dt);

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            if (mode == MODE_EDIT) { renderEdit(w, h); renderEditHUD(w, h); }
            else if (mode == MODE_HOME && seq == SEQ_SHELF) { renderShelfView(w, h); renderHomeHUD(w, h); }
            else {
                renderWorld(w, h);
                if (capturePosterNow && mode == MODE_SCENE) capturePoster(w, h);
                if (mode == MODE_HOME) renderHomeHUD(w, h);
                else renderSceneHUD(w, h);
            }
            if (shotMode) runScript(w, h);
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

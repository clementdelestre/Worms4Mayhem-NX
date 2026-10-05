#include "frontbg.h"
#include "fx.h"
#include "lit.h"
#include "models.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

#ifdef __SWITCH__
#define DIR "sdmc:/switch/worms4nx/assets/models/frontend/"
#else
#define DIR "./assets/models/frontend/"
#endif

namespace {
const float S = 0.25f;  // metres per W4M title unit (the diorama is ~116 units wide)
// Camera per menu page (metres): position, target
const Vector3 PAGES[][2] = {
    {{2, 5.5f, 34}, {-1, 4.5f, 0}},     // 0 main: island, wreck and burning tree
    {{-10, 3, 18}, {-60, 6, -30}},      // 1 local game: open sea
    {{12, 9, 26}, {6, 7, -10}},         // 2 network
    {{-6, 4, 24}, {-4, 3, 6}},          // 3 options
};
const int NPAGES = sizeof PAGES / sizeof PAGES[0];
// Emitter locators of WX.Mesh.Title (Tree_Smoke, Crash_Fire, Crash_Sparks, Barrel_Poison), title units
struct Emitter { Vector3 p; float rate, life, s0, s1; Color c; bool fire; float acc; };
Emitter emitters[] = {
    {{-9, 7, 50}, 2, 6.0f, 0.8f, 4.0f, {150, 150, 150, 110}, false, 0},   // tree smoke
    {{-9, 13, 12}, 2.5f, 6.0f, 0.8f, 4.5f, {120, 120, 120, 120}, false, 0},  // wreck smoke
    {{-9, 13, 12}, 8, 0.5f, 0.5f, 0.2f, {255, 170, 70, 255}, true, 0},    // wreck fire
    {{-9, 7.5f, 50}, 6, 0.5f, 0.6f, 0.2f, {255, 160, 60, 255}, true, 0},  // tree fire
    {{7, 9, 26}, 2, 2.0f, 0.15f, 0.5f, {120, 220, 80, 140}, false, 0},    // poison barrel bubbles
};
// Seagulls: their Location clip already flies a 14 s loop, so each one is just a placement and a time offset
struct Gull { Vector3 p; float yaw, t0; };
const Gull GULLS[] = {{{0, 30, 0}, 0.4f, 0}, {{-30, 40, -20}, 2.1f, 4.7f}, {{25, 34, -30}, 4.0f, 9.3f}};

Model title{}, sky{};
Shader sh{};
int locLit = -1;
bool ok = false, tried = false;
int cur = 0;
Camera3D cam = {PAGES[0][0], PAGES[0][1], {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
float elapsed = 0;
double lastDraw = -10;

const char *VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matNormal;
varying vec2 uv;
varying vec3 n;
varying vec4 col;
void main() { uv = vertexTexCoord; col = vertexColor; n = (matNormal * vec4(vertexNormal, 0.0)).xyz; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
// lit = 1: alpha-tested, two-sided lambert (island); 0: unlit alpha-blended (clouds)
const char *FS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float lit;
varying vec2 uv;
varying vec3 n;
varying vec4 col;
void main() {
    vec4 c = texture2D(texture0, uv) * colDiffuse * col;
    if (lit > 0.5) {
        if (c.a < 0.5) discard;
        vec3 nn = normalize(gl_FrontFacing ? n : -n);
        c.rgb *= 0.65 + 0.5 * max(dot(nn, normalize(vec3(0.35, 0.8, 0.5))), 0.0);
        c.a = 1.0;
    }
    gl_FragColor = c;
}
)";

void setShader(Model &m) {
    for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = sh;
}

void drawModel(const Model &m, float lit) {
    SetShaderValue(sh, locLit, &lit, SHADER_UNIFORM_FLOAT);
    for (int i = 0; i < m.meshCount; i++) DrawMesh(m.meshes[i], m.materials[m.meshMaterial[i]], m.transform);
}
}  // namespace

void FrontBg::load() {
    tried = true;
    if (ok || !FileExists(DIR "title.glb") || !FileExists(DIR "sky.glb")) return;
    double t0 = GetTime();
    sh = Lit::shader(VS, FS);
    locLit = GetShaderLocation(sh, "lit");
    title = Models::take(DIR "title.glb");
    sky = Models::take(DIR "sky.glb");
    ok = title.meshCount && sky.meshCount && sh.id != rlGetShaderIdDefault();
    if (!ok) return unload();
    setShader(title), setShader(sky);
    title.transform = MatrixScale(S, S, S);
    TraceLog(LOG_INFO, "FRONTBG: loaded in %.0f ms", (GetTime() - t0) * 1000);
}

void FrontBg::unload() {
    if (title.meshCount) UnloadModel(title);
    if (sky.meshCount) UnloadModel(sky);
    if (sh.id && sh.id != rlGetShaderIdDefault()) UnloadShader(sh);
    title = sky = {}, sh = {}, ok = false, tried = false;
}

void FrontBg::page(int id) { cur = id >= 0 && id < NPAGES ? id : 0; }

void FrontBg::draw(float dt) {
    if (!tried) load();
    if (!ok) return;
    if (GetTime() - lastDraw > 1) {  // back from a match
        static const float FE_WATER[14] = {0.5f, 60, 1, 0.2f, 1, 5, 15, 6, 6, 1, 0.4f, 0.4f, 1, 0.1f};  // TWEAK.XOM FE.Water
        Fx::theme("frontend", {150, 200, 240, 255}, "day", false), Fx::clear();  // FE.DAYWater set
        std::copy(FE_WATER, FE_WATER + 14, Lit::sun.water);
    }
    lastDraw = GetTime();
    elapsed += dt;
    // glide to the page's view (critically damped-ish), plus a slow drift so the scene never freezes
    float k = 1 - expf(-dt * 1.6f);
    Vector3 drift = {sinf(elapsed * 0.13f) * 0.8f, sinf(elapsed * 0.21f) * 0.3f, cosf(elapsed * 0.09f) * 0.6f};
    cam.position = Vector3Lerp(cam.position, Vector3Add(PAGES[cur][0], drift), k);
    cam.target = Vector3Lerp(cam.target, PAGES[cur][1], k);

    for (Emitter &e : emitters) {
        for (e.acc += e.rate * dt; e.acc >= 1; e.acc--) {
            Vector3 p = Vector3Scale(e.p, S), v = {0.25f + GetRandomValue(-10, 10) * 0.02f, e.fire ? 0.8f : 0.5f, GetRandomValue(-10, 10) * 0.02f};
            Fx::puff(p, v, e.life, e.s0, e.s1, e.c, e.fire);
        }
    }
    Fx::update(dt);

    ClearBackground(Fx::fog());
    Lit::frame(cam.position);
    BeginMode3D(cam);
    Fx::drawSky(cam, {}, S);
    // cloud dome (~25000 units) follows the camera inside the far plane, drawn behind everything
    rlDrawRenderBatchActive();
    rlDisableDepthTest(), rlDisableDepthMask(), rlDisableBackfaceCulling();
    sky.transform = MatrixMultiply(MatrixScale(0.03f, 0.03f, 0.03f), MatrixTranslate(cam.position.x, cam.position.y, cam.position.z));
    BeginBlendMode(BLEND_ALPHA);
    drawModel(sky, 0);
    EndBlendMode();
    rlEnableDepthTest(), rlEnableDepthMask();
    drawModel(title, 1);
    rlEnableBackfaceCulling();
    rlPushMatrix();
    rlScalef(S, S, S);
    for (const Gull &g : GULLS) Models::draw("seagull", g.p, g.yaw, 0, WHITE, "WXM_SGull_WingFlap", elapsed + g.t0);
    rlPopMatrix();
    Fx::drawWater(cam, -100 * S, elapsed, 12000 * S);  // MenuBackgroundLandscapeEntity 0x47fdd0: Water.Level -100 units
    Fx::draw(cam);
    EndMode3D();
}

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
const float S = 0.05f;  // metres per FE unit
// FE camera = the app-init HUD camera [disasm 0x4d8bd6..0x4d8cc9, docs/w4m/frontend.md §FE camera]: z 900 looking down -z, fovy 2 atan(0.48 / 1.8); Title Mesh item of WXFE.MainMenu [data]
const Vector3 FE_EYE = {0, 0, 900}, TITLE_POS = {-25, -131, 500}, TITLE_ROT = {-0.15f, 0.15f, -0.01f};
const float FE_FOV = 0.5216f, TITLE_SCALE = 7.3f;
const float WATER_DROP = 735;  // FE units under the eye [observed: fits the real menu's horizon; the camera placing Water.Level not found]
const float ANIM_SPEED = 0.9f;  // mesh clips play at FE.AnimSpeed (0x778c91) [disasm + data]
// Intro_Title (11.664 s) moves Land from its rest (-5.71, 15.78125, 1.5039) to (y 17.921875, z -23.78125); Loop_Title keys only the trees,
// so Land holds that last key [data; the hold observed on the real game's settled menu]
const float INTRO_LEN = 11.6640625f;
const Vector3 LAND_HOLD = {0, 17.921875f - 15.78125f, -23.78125f - 1.5039063f};
Matrix titleBase;  // title units -> world
float introT = 0;
// WXFE.MainMenu Title Mesh EffectNames / LocatorNames, SpawnDelay {0, 0}, SpawnLoopTime {6000, 10000} ms: the first two restart on that
// period, the others run from the item's creation (WXFE_MeshObjectParticleEntity 0x76d27c, 0x76d1a1) [data + disasm]
struct TitleFx { const char *fx, *loc; float loop; int h; float next; };
TitleFx titleFx[] = {{"WXP_FE_CrashBang", "Crash_FireSpark", 6, 0, 0}, {"WXP_FE_BarrelSpark", "Barrel_Spark", 10, 0, 0},
                     {"WXP_FE_TreeFire", "Tree_Fire", 0, 0, 0},         {"WXP_FE_BarrelPoison", "Barrel_Poison", 0, 0, 0},
                     {"WXP_FE_CrashFire", "Crash_Fire", 0, 0, 0},       {"WXP_FE_CrashSparks", "Crash_Sparks", 0, 0, 0},
                     {"WXP_FE_TreeSmoke", "Tree_Smoke", 0, 0, 0},       {"WXP_FE_Flies", "Flies", 0, 0, 0}};
const float FX_UNIT = S;  // the effects run in FE units, not the mesh's Scale 7.3 [observed: puff sizes and the column height on the real game]
bool fxOn = false;

Model sky{};
Shader sh{};
bool ok = false, tried = false;
int cur = 0;
Camera3D cam = {{FE_EYE.x * S, FE_EYE.y * S, FE_EYE.z * S}, {0, 0, 0}, {0, 1, 0}, FE_FOV * RAD2DEG, CAMERA_PERSPECTIVE};
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
// unlit alpha-blended (clouds)
const char *FS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
varying vec2 uv;
varying vec3 n;
varying vec4 col;
void main() { gl_FragColor = texture2D(texture0, uv) * colDiffuse * col; }
)";

void setShader(Model &m) {
    for (int i = 0; i < m.materialCount; i++) m.materials[i].shader = sh;
}

void drawModel(const Model &m) {
    for (int i = 0; i < m.meshCount; i++) DrawMesh(m.meshes[i], m.materials[m.meshMaterial[i]], m.transform);
}

// Title Mesh clip at intro time t: Intro_Title once, then Loop_Title looped with Land held at Intro's last key
const char *titleClip(float t, float *ct, Matrix *m) {
    float k = t * ANIM_SPEED;
    bool intro = k < INTRO_LEN;
    *ct = intro ? k : fmodf(k - INTRO_LEN, INTRO_LEN);
    *m = intro ? titleBase : MatrixMultiply(MatrixTranslate(LAND_HOLD.x, LAND_HOLD.y, LAND_HOLD.z), titleBase);
    return intro ? "Intro_Title" : "Loop_Title";
}

void titleEffects(bool on, float t) {
    if (!on) {
        for (TitleFx &f : titleFx) Fx::stop(f.h), f.h = 0;  // the item's removal kills its effects (0x76d090)
        fxOn = false;
        return;
    }
    float ct;
    Matrix m;
    const char *clip = titleClip(t, &ct, &m);
    for (TitleFx &f : titleFx) {
        Matrix j;
        if (!Models::joint("title", f.loc, clip, ct, true, &j)) continue;
        Vector3 at = Vector3Transform({j.m12, j.m13, j.m14}, m);
        if (!fxOn || (f.loop > 0 && t >= f.next)) Fx::stop(f.h), f.h = Fx::start(f.fx, at, FX_UNIT), f.next = t + f.loop;
        else Fx::place(f.h, at);
    }
    fxOn = true;
}
}  // namespace

void FrontBg::load() {
    tried = true;
    if (ok || !FileExists(DIR "sky.glb")) return;
    double t0 = GetTime();
    sh = Lit::shader(VS, FS);
    sky = Models::take(DIR "sky.glb");
    ok = sky.meshCount && sh.id != rlGetShaderIdDefault();
    if (!ok) return unload();
    setShader(sky);
    titleBase = MatrixMultiply(MatrixMultiply(MatrixScale(TITLE_SCALE * S, TITLE_SCALE * S, TITLE_SCALE * S), MatrixRotateZYX({TITLE_ROT.x, TITLE_ROT.y, TITLE_ROT.z})),
                               MatrixTranslate(TITLE_POS.x * S, TITLE_POS.y * S, TITLE_POS.z * S));
    TraceLog(LOG_INFO, "FRONTBG: loaded in %.0f ms", (GetTime() - t0) * 1000);
}

void FrontBg::unload() {
    if (sky.meshCount) UnloadModel(sky);
    if (sh.id && sh.id != rlGetShaderIdDefault()) UnloadShader(sh);
    sky = {}, sh = {}, ok = false, tried = false, fxOn = false;
}

void FrontBg::page(int id) {
    if (!id && cur) introT = 0;
    cur = id;
}

void FrontBg::draw(float dt, bool particles) {
    if (!tried) load();
    if (!ok) return;
    if (GetTime() - lastDraw > 1) {  // back from a match
        introT = 0, fxOn = false;
        static const float FE_WATER[14] = {0.5f, 60, 1, 0.2f, 1, 5, 15, 6, 6, 1, 0.4f, 0.4f, 1, 0.1f};  // TWEAK.XOM FE.Water
        Fx::theme("frontend", {150, 200, 240, 255}, "day", false), Fx::clear();  // FE.DAYWater set
        std::copy(FE_WATER, FE_WATER + 14, Lit::sun.water);
    }
    lastDraw = GetTime();
    elapsed += dt;
    if (!cur) introT += dt;
    if (!cur && introT <= dt) titleEffects(false, 0);  // the menu reopened: the item and its effects are new
    titleEffects(!cur, introT);  // the island and its effects exist on the main menu only (Outro_Title 0.04 s)
    if (particles) Fx::update(dt);

    ClearBackground(Fx::fog());
    Lit::frame(cam.position);
    BeginMode3D(cam);
    Fx::drawSky(cam, {}, S);
    // cloud dome (~25000 units) follows the camera inside the far plane, drawn behind everything
    rlDrawRenderBatchActive();
    rlDisableDepthTest(), rlDisableDepthMask(), rlDisableBackfaceCulling();
    sky.transform = MatrixMultiply(MatrixScale(0.03f, 0.03f, 0.03f), MatrixTranslate(cam.position.x, cam.position.y, cam.position.z));
    BeginBlendMode(BLEND_ALPHA);
    drawModel(sky);
    EndBlendMode();
    rlEnableDepthTest(), rlEnableDepthMask();
    if (!cur) {
        float ct;
        Matrix m;
        const char *clip = titleClip(introT, &ct, &m);
        Models::draw("title", m, WHITE, clip, ct, true);  // Lighting 0: no FrontendMeshLight, the worm light (0x77887d) [disasm]
    }
    rlEnableBackfaceCulling();
    Fx::drawWater(cam, -WATER_DROP * S, elapsed, 12000 * S);
    if (particles) Fx::draw(cam);
    EndMode3D();
}

#include "fx.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <vector>

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#else
#define DATA_DIR "./"
#endif

namespace Fx {
float shake = 0;

namespace {
enum Tex { GLOW, PUFF, FIRE, DROP, SPARK, TRAIL_R, TRAIL_B, STAR, TRAIL_W, RING, JET, TOON, CROSS, QUESTION, BUBBLE, TEX_COUNT };  // also the draw order within a blend pass
const char *TEX_FILES[] = {"wxp_sprite_001", "wxp_sprite_004", "wxp_sprite_030", "wxp_sprite_005", "wxp_sprite_026", "wxp_trailsprite_r", "wxp_trailsprite_b", "wxp_sprite_007", "wxp_trailsprite_w"};
const int MAX = 1024;

struct Particle {
    Vector3 p, v;
    float age, life, size0, size1, rot, spin, grav, drag;  // age < 0: not born yet
    Color c;
    unsigned char tex;
    bool add;  // additive (fire, sparks) vs alpha (smoke, dust, debris, water)
    float stretch = 0;  // > 0: ribbon along -velocity (s of travel), size = width
    Color tail = {};  // alpha > 0: leaves a trail of additive puffs of this colour (W4M anchor particles)
};
struct Streak { Vector3 p, dir; float len, width; unsigned char tex; };

Texture2D tex[TEX_COUNT];
std::vector<Particle> ps;
std::vector<Streak> streaks;
struct Seen { Vector3 p; int weapon; bool child; };
std::vector<Seen> seen, seenPrev;  // live shots of this / the previous frame: which weapon blew up where
float show = 0, tickAcc = 0, sinceLast = 0;  // victory fireworks: s left of W4M GameOverLogicEntity's 4 s wait + 5 s show
Vector3 stageC{};  // Land.Center, Land.Radius, Land.MaxHeight of the match that ended
float stageR = 0, stageTop = 0;
Vector3 camAt{};
Texture2D skyTex{}, waterTex[3]{};
Shader skySh{}, waterSh{};
Mesh dome{}, plane{};
Material skyMat{}, waterMat{};
Color fogCol = {120, 170, 230, 255};
uint32_t seed = 12345;

float rnd(float a = 0, float b = 1) {
    seed = seed * 1664525u + 1013904223u;
    return a + (b - a) * ((seed >> 8) / 16777216.0f);
}
Vector3 rndDir() {
    float z = rnd(-1, 1), a = rnd(0, 2 * PI), r = sqrtf(1 - z * z);
    return {r * cosf(a), z, r * sinf(a)};
}
void add(Particle p) {
    if ((int)ps.size() >= MAX) return;
    p.rot = rnd(0, 2 * PI);
    ps.push_back(p);
}

Texture2D loadTex(const char *dir, const char *name, bool mips) {
    const char *f = TextFormat(DATA_DIR "assets/ui/%s/%s.png", dir, name);
    if (!FileExists(f)) return Texture2D{};
    Texture2D t = LoadTexture(f);
    if (mips) GenTextureMipmaps(&t);
    SetTextureFilter(t, mips ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
    return t;
}

Shader shader(const char *vs, const char *fs) {
    bool es = rlGetVersion() == RL_OPENGL_ES_20 || rlGetVersion() == RL_OPENGL_ES_30;
    std::string v = es ? "#version 100\n" : "#version 330\n#define attribute in\n#define varying out\n";
    std::string f = es ? "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n" : "#version 330\n#define varying in\n#define texture2D texture\n#define gl_FragColor fragColor\nout vec4 fragColor;\n";
    return LoadShaderFromMemory((v + vs).c_str(), (f + fs).c_str());
}

// Sky: W4M's 256x1 ramp on a camera-centred sphere, zenith u = 0 to horizon u = 0.75 (the cream end washes everything out).
const char *SKY_VS = R"(
attribute vec3 vertexPosition;
uniform mat4 mvp;
varying vec3 vPos;
void main() { vPos = vertexPosition; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char *SKY_FS = R"(
uniform sampler2D texture0;
varying vec3 vPos;
void main() {
    float h = max(normalize(vPos).y, 0.0);
    gl_FragColor = vec4(texture2D(texture0, vec2(0.75 * pow(1.0 - h, 3.0), 0.5)).rgb, 1.0);
}
)";
// Water after W4M's water.cg: two panned normal maps distort the diffuse and a sphere-mapped environment,
// plus sun specular, fresnel alpha and distance fog.
const char *WATER_VS = R"(
attribute vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
varying vec3 wPos;
void main() { wPos = (matModel * vec4(vertexPosition, 1.0)).xyz; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char *WATER_FS = R"(
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform float time;
uniform vec3 camPos;
uniform vec3 fogColor;
uniform vec2 fogRange;
varying vec3 wPos;
void main() {
    vec2 uv = wPos.xz / 14.0;
    vec3 a = texture2D(texture1, uv + time * vec2(-0.015, 0.03)).rgb * 2.0 - 1.0;
    vec3 b = texture2D(texture1, uv * -0.6 + time * vec2(0.01, -0.02)).rgb * 2.0 - 1.0;
    vec3 n = normalize(vec3(a.x + b.x, 2.5, a.y + b.y));
    vec3 e = normalize(wPos - camPos);
    vec3 r = reflect(e, n);
    vec3 col = texture2D(texture0, uv * 0.5 + n.xz * 0.08).rgb;
    vec2 sp = vec2(0.5) + normalize(r.xz + vec2(0.0001)) * (1.0 - r.y) * 0.25;
    float f = pow(1.0 - max(-e.y, 0.0), 4.0);
    col = mix(col, texture2D(texture2, sp).rgb, 0.35 + 0.4 * f);
    col += vec3(pow(max(dot(r, normalize(vec3(0.4, 1.0, 0.3))), 0.0), 80.0));
    float fog = clamp((length(wPos - camPos) - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    gl_FragColor = vec4(mix(col, fogColor, fog), mix(0.7 + 0.25 * f, 1.0, fog));
}
)";

void quad(Vector3 p, Vector3 r, Vector3 u, Color c) {
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlTexCoord2f(0, 0); rlVertex3f(p.x - r.x + u.x, p.y - r.y + u.y, p.z - r.z + u.z);
    rlTexCoord2f(0, 1); rlVertex3f(p.x - r.x - u.x, p.y - r.y - u.y, p.z - r.z - u.z);
    rlTexCoord2f(1, 1); rlVertex3f(p.x + r.x - u.x, p.y + r.y - u.y, p.z + r.z - u.z);
    rlTexCoord2f(1, 0); rlVertex3f(p.x + r.x + u.x, p.y + r.y + u.y, p.z + r.z + u.z);
}
}  // namespace

void load() {
    for (int i = 0; i < RING; i++) tex[i] = loadTex("fe", TEX_FILES[i], true);
    Image ring = GenImageColor(64, 64, BLANK);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float d = Vector2Length({x - 31.5f, y - 31.5f}) / 32, a = fmaxf(0, 1 - fabsf(d - 0.85f) / 0.15f);
            ImageDrawPixel(&ring, x, y, {255, 255, 255, (unsigned char)(255 * a * a)});
        }
    tex[RING] = LoadTextureFromImage(ring);
    tex[JET] = loadTex("hud", "jetfire", true), tex[TOON] = loadTex("hud", "toonfire", true), tex[CROSS] = loadTex("hud", "wxp_sprite_006", true);
    tex[QUESTION] = loadTex("hud", "wxp_sprite_022", true), tex[BUBBLE] = loadTex("hud", "wxp_sprite_023", true);  // W4M Particle.WXSprite22/23
    UnloadImage(ring);
    skySh = shader(SKY_VS, SKY_FS);
    waterSh = shader(WATER_VS, WATER_FS);
    dome = GenMeshSphere(1, 12, 16);
    plane = GenMeshPlane(1, 1, 1, 1);
    skyMat = LoadMaterialDefault(), waterMat = LoadMaterialDefault();
    skyMat.shader = skySh, waterMat.shader = waterSh;
    ps.reserve(MAX);
}

void theme(const std::string &theme, Color sky, const std::string &time) {
    static const char *NAMES[] = {"jurassic", "camelot", "arabian", "wildwest", "construction", "arctic", "england", "horror", "lunar", "pirate", "war"};
    static const char LETTERS[] = "pcawbrehlto";
    char l = 'c';  // procedural island and unknown themes
    for (int i = 0; i < 11; i++)
        if (theme == NAMES[i]) l = LETTERS[i];
    char suffix = time == "night" ? '3' : time == "evening" ? '2' : '1';  // W4M ramp naming: 01 day, 02 evening, 03 night
    if (skyTex.id) UnloadTexture(skyTex);
    for (Texture2D &t : waterTex)
        if (t.id) UnloadTexture(t), t = {};
    skyTex = {};
    fogCol = sky;
    const char *f = TextFormat(DATA_DIR "assets/ui/sky/%c_sky0%c.png", l, suffix);
    if (FileExists(f)) {
        Image im = LoadImage(f);
        fogCol = GetImageColor(im, im.width * 3 / 4, 0);  // sky colour at the horizon
        skyTex = LoadTextureFromImage(im);
        SetTextureFilter(skyTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(skyTex, TEXTURE_WRAP_CLAMP);
        UnloadImage(im);
    }
    for (int i = 0; i < 3; i++) waterTex[i] = loadTex("sky", TextFormat("%c_water0%c%c", l, suffix, 'a' + i), true);
    if (waterTex[2].id) SetTextureWrap(waterTex[2], TEXTURE_WRAP_CLAMP);
    skyMat.maps[MATERIAL_MAP_DIFFUSE].texture = skyTex;
    waterMat.maps[MATERIAL_MAP_DIFFUSE].texture = waterTex[0];
    waterMat.maps[MATERIAL_MAP_SPECULAR].texture = waterTex[1];
    waterMat.maps[MATERIAL_MAP_NORMAL].texture = waterTex[2];
}

void unload() {
    for (Texture2D &t : tex) UnloadTexture(t);
    if (skyTex.id) UnloadTexture(skyTex);
    for (Texture2D &t : waterTex)
        if (t.id) UnloadTexture(t);
    UnloadMesh(dome), UnloadMesh(plane);
    UnloadShader(skySh), UnloadShader(waterSh);
    MemFree(skyMat.maps), MemFree(waterMat.maps);
}

void fireworks(Vector3 centre, float radius, float top) { stageC = centre, stageR = radius, stageTop = top, tickAcc = 0; }
void clear() { ps.clear(), streaks.clear(), seen.clear(), seenPrev.clear(), shake = show = 0; }

Color fog() { return fogCol; }
int count() { return (int)ps.size(); }

namespace {
const Seen *shotAt(Vector3 p) {  // the shot drawn last frame closest to where something just blew up
    const Seen *best = nullptr;
    float bd = 2.5f;
    for (const Seen &s : seenPrev)
        if (float d = Vector3Distance(s.p, p); d < bd) bd = d, best = &s;
    return best;
}
bool is(const Seen *s, const char *name) { return s && s->weapon >= 0 && WEAPONS[s->weapon].name == name; }

// W4M anchors (WXP_ExplosionX_TailAnchors / AnchorsHigh, HolyHG_Anchors): heads flung up and out, each trailing WXP_ExplosionX_Tail fire
void anchors(Vector3 c, int n, float up, float size, Color col, unsigned char t) {
    for (int i = 0; i < n; i++)
        add({c, {rnd(-6, 6), up * rnd(0.7f, 1.2f), rnd(-6, 6)}, -rnd(0, 0.05f), rnd(1.2f, 2.0f), size, size * 0.7f, 0, rnd(-6, 6), 14, 0.3f, col, t, true, 0, {255, 140, 20, 150}});
}
// W4M firework (WXP_StarburstExplosion, WXPF_Firework*): glow bang, trail-sprite spray, delayed glints
void burst(Vector3 c, Color glow, unsigned char trailTex, Color trailCol) {
    add({c, {}, 0, 0.8f, 1.5f, 3, 0, 0, 0, 0, glow, GLOW, true});
    add({c, {}, -0.005f, 1.8f, 4, 7, 0, 0, 0, 0, {glow.r, glow.g, glow.b, 110}, GLOW, true});
    for (int i = 0; i < 32; i++)
        add({c, Vector3Scale(rndDir(), rnd(6, 10)), 0, rnd(1.2f, 1.75f), 0.5f, 0.2f, 0, 0, 4, 1.2f, trailCol, trailTex, true, 0.18f});
    for (int i = 0; i < 20; i++)  // WXP_StarB_ManyGlows: crackle 0.4 s later, within 5 m
        add({Vector3Add(c, Vector3Scale(rndDir(), rnd(1, 5))), {}, -rnd(0.4f, 0.9f), 0.8f, 1.3f, 0.2f, 0, 0, 0, 0, {glow.r, glow.g, glow.b, 220}, GLOW, true});
}
// W4M PARTTWK WXPF_Firework1-5 (m = units / 20). Glows (WXSprite1) additive, stars (WXSprite7) alpha. Trails additive: their
// Bundl10 shader is alpha but the TrailSprite images have no alpha channel (black ground), so they are drawn as light.
// Trails fly at (0, 15, 0) +-20 m/s under Mass 4.6 x Acceleration (23 m/s²). Stars and Starburst trails use W4M's alternate
// curve r(t) = v0 (N - 1/(S t + 1/N)), approximated by drag with the same final radius and the same half-way time 1 / (S N).
void firework(Vector3 c, int kind) {
    auto glow = [&](int n, float size, float rand, float life, float delay, Color col) {
        for (int i = 0; i < n; i++) { float s = size + rnd(-rand, rand); add({c, {}, -delay, life, s, s, 0, 0, 0, 0, col, GLOW, true}); }
    };
    auto whiteout = [&](float delay) { add({c, {}, -delay, 0.08f, 20, 20, 0, 0, 0, 0, {255, 255, 255, 26}, GLOW, true}); };  // 400 units, alpha 0.1
    auto trails = [&](int n, unsigned char t, float size, float life) {  // WXPF_*Trails*: kTrail ribbons, size +-0.5 units, life +-400 ms
        for (int i = 0; i < n; i++)
            add({c, {rnd(-20, 20), 15 + rnd(-20, 20), rnd(-20, 20)}, 0, life + rnd(-0.4f, 0.4f), size + rnd(-0.025f, 0.025f), 0, 0, 0, 23, 0, WHITE, t, true, 0.48f});
    };
    auto stars = [&](int n, float radius, float life, float delay, Color col) {  // WXPF_Exploder*: N 6500, S 2e-6
        for (int i = 0; i < n; i++) add({c, Vector3Scale(rndDir(), radius * 9), -delay, life, 0.15f, 0.15f, 0, 0, 0, 9, col, STAR, false});
    };
    auto starTrails = [&](float reach, float life) {  // WXP_StarburstTrailsA/B: 16, per-axis velocity, N 5000, S 3e-6, white
        for (int i = 0; i < 16; i++)
            add({c, Vector3Scale({rnd(-reach, reach), rnd(-reach, reach), rnd(-reach, reach)}, 15), 0, life + rnd(-0.4f, 0.4f), 0.05f, 0, 0, 0, 0, 15, WHITE, TRAIL_B, true, 0.48f});
    };
    const Color FW_CYAN = {77, 255, 255, 255}, FW_ORANGE = {255, 128, 0, 255}, FW_GREEN = {0, 255, 64, 255}, STAR_COL = {255, 230, 128, 255};  // stars: white to (1, 0.8, 0)
    whiteout(0);
    if (kind == 0) glow(2, 1.5f, 0, 0.8f, 0, FW_CYAN), glow(1, 6, 0, 1.8f, 0.005f, FW_CYAN), starTrails(12.5f, 1.2f), starTrails(17.5f, 1.75f), trails(12, TRAIL_B, 0.1f, 1.6f);
    else if (kind == 3) glow(2, 1.5f, 0, 0.8f, 0, FW_GREEN), glow(1, 6, 0, 1.8f, 0.005f, FW_GREEN), stars(50, 26, 1.5f, 0, {255, 255, 128, 255});
    else {
        glow(2, 1.5f, 0, 0.8f, 0, FW_ORANGE), glow(1, 6, 0, 1.8f, 0.005f, FW_ORANGE);
        if (kind == 1) trails(24, TRAIL_R, 0.2f, 1.6f), stars(30, 13, 0.8f, 0, STAR_COL);
        if (kind == 2) stars(30, 13, 0.8f, 0, STAR_COL), stars(50, 19.5f, 1.5f, 0.25f, STAR_COL), glow(2, 10, 2.5f, 1.8f, 0.25f, FW_ORANGE), whiteout(0.25f);
        if (kind == 4) trails(72, TRAIL_R, 0.1f, 3);
    }
}
}  // namespace

void event(const GameEvent &e, Color dirt) {
    bool big = e.kind == GameEvent::BigBoom;
    if (e.kind == GameEvent::Boom || big) {
        float r = big ? 5.5f : 2.5f;
        const Seen *s = shotAt(e.pos);
        bool holy = is(s, "Holy Hand Grenade");  // WXP_Holy_HG_Explosion = WXP_ExplosionX_Large with a gold ring, white cloud and crosses
        shake = fmaxf(shake, big ? 0.6f : 0.18f);
        add({e.pos, {}, 0, 0.3f, r * 1.5f, r * 3, 0, 0, 0, 0, {255, 220, 150, 255}, GLOW, true});  // flash
        add({e.pos, {}, 0, 0.45f, r * 0.4f, r * 2.6f, 0, 0, 0, 0, {255, 230, 180, 200}, RING, true});
        for (int i = 0; i < (big ? 18 : 10); i++) {
            Vector3 d = rndDir();
            Color c = holy ? Color{255, (unsigned char)rnd(225, 250), (unsigned char)rnd(150, 210), 255} : Color{255, (unsigned char)rnd(120, 220), (unsigned char)rnd(30, 90), 255};
            add({Vector3Add(e.pos, Vector3Scale(d, r * 0.2f)), Vector3Scale(d, rnd(1, 3) * r * 0.5f), -rnd(0, 0.08f), rnd(0.4f, 0.7f), r * 0.5f, r * 1.2f, 0, rnd(-2, 2), -1.5f, 4, c, FIRE, false});  // fireball
        }
        if (is(s, "Starburst")) burst(e.pos, {77, 255, 255, 255}, TRAIL_B, WHITE);
        if (holy) {
            anchors(e.pos, 8, 14, 1.4f, WHITE, CROSS);
            for (int i = 0; i < 24; i++) {  // WXP_Explosion_Holy_HG_Ring: gold halo spreading 3 m up
                float a = i * 2 * PI / 24 + rnd(0, 0.2f), v = rnd(3, 5);
                add({Vector3Add(e.pos, {0, 3, 0}), {cosf(a) * v, rnd(0, 0.5f), sinf(a) * v}, -rnd(0, 0.15f), rnd(2.5f, 3.5f), 1.5f, 3.5f, 0, rnd(-1, 1), -0.3f, 1.2f, {255, 170, 60, 45}, PUFF, true});
            }
        } else if (!s || !s->child) anchors(e.pos, big ? 8 : 4, big ? 10 : 8, 0.35f, {255, 190, 0, 255}, PUFF);
        if (big) anchors(e.pos, 8, 16, 0.3f, {255, 190, 0, 255}, PUFF);
        for (int i = 0; i < (big ? 24 : 8); i++)  // WXP_BangTrails(Large): yellow streaks
            add({e.pos, Vector3Scale(rndDir(), rnd(6, 12)), 0, rnd(0.6f, 0.9f), 0.22f, 0.08f, 0, 0, 18, 0.5f, {255, 230, 60, 255}, TRAIL_W, true, 0.06f});
        for (int i = 0; i < (big ? 14 : 7); i++) {
            Vector3 d = rndDir();
            d.y = fabsf(d.y) * 0.6f + 0.3f;
            unsigned char g = (unsigned char)rnd(80, 120);
            add({Vector3Add(e.pos, Vector3Scale(d, r * 0.3f)), Vector3Scale(d, rnd(0.5f, 2) * r * 0.4f), -rnd(0.1f, 0.3f), rnd(1.8f, 2.8f), r * 0.7f, r * 1.8f, 0, rnd(-0.6f, 0.6f), -0.6f, 1.5f,
                 {g, g, g, 150}, PUFF, false});  // smoke rises (negative gravity)
        }
        for (int i = 0; i < (big ? 24 : 12); i++) {
            Vector3 d = rndDir();
            d.y = fabsf(d.y) + 0.4f;
            add({e.pos, Vector3Scale(Vector3Normalize(d), rnd(5, 12) * (big ? 1.4f : 1)), 0, rnd(0.8f, 1.4f), rnd(0.12f, 0.3f), 0.1f, 0, rnd(-8, 8), 18, 0.3f, dirt, PUFF, false});  // debris
            if (i % 2) add({e.pos, Vector3Scale(Vector3Normalize(d), rnd(8, 16)), 0, rnd(0.3f, 0.6f), 0.25f, 0.05f, 0, 0, 10, 1, {255, 200, 90, 255}, SPARK, true});
        }
        Color dust = {(unsigned char)((dirt.r + 255) / 2), (unsigned char)((dirt.g + 230) / 2), (unsigned char)((dirt.b + 200) / 2), 120};
        for (int i = 0; i < (big ? 12 : 6); i++) {
            float a = i * 2 * PI / (big ? 12 : 6) + rnd(0, 0.5f);
            Vector3 d = {cosf(a), 0.15f, sinf(a)};
            add({Vector3Add(e.pos, {0, -r * 0.4f, 0}), Vector3Scale(d, r * rnd(1.2f, 1.8f)), 0, rnd(1.0f, 1.6f), r * 0.4f, r * 1.2f, 0, rnd(-1, 1), 0, 2.5f, dust, PUFF, false});
        }
    } else if (e.kind == GameEvent::Collect) {  // PARTTWK WXP_PickupFX (the only pickup effect the exe spawns): poof + sparklies + glow
        Vector3 c = Vector3Add(e.pos, {0, 0.3f, 0});
        add({c, {}, 0, 0.3f, 0.8f, 2.0f, 0, 0, 0, 0, {255, 215, 70, 255}, GLOW, true});
        for (int i = 0; i < 6; i++)  // WXP_PickupGlow: additive (SrcAlpha, One) at alpha 0.3, 30 units, 350 +-100 ms
            add({Vector3Add(c, Vector3Scale(rndDir(), 0.25f)), {}, 0, rnd(0.25f, 0.45f), rnd(1.25f, 1.75f), 0.5f, 0, 0, 0, 0, {128, 230, 255, 77}, GLOW, true});
        for (int i = 0; i < 8; i++) {
            Vector3 d = rndDir();
            d.y = fabsf(d.y) * 0.5f + 0.1f;
            float t = rnd(0, 0.6f);  // white -> (0.3, 0.85, 1) ramp, sampled per cloud
            add({Vector3Add(c, Vector3Scale(d, 0.2f)), Vector3Scale(d, rnd(2, 3.5f)), -rnd(0.04f, 0.1f), rnd(0.8f, 1.1f), 0.3f, 1.4f, 0, rnd(-1, 1), -0.5f, 3,
                 {(unsigned char)(255 - 178 * t), (unsigned char)(255 - 38 * t), 255, 190}, PUFF, false});
        }
        for (int i = 0; i < 24; i++) {
            Vector3 d = rndDir();
            d.y = fabsf(d.y) * 0.8f + 0.2f;
            bool ray = i % 2;
            Color col = ray ? Color{180, 245, 255, 255} : rnd() < 0.5f ? Color{51, 255, 255, 255} : Color{220, 255, 255, 255};
            add({c, Vector3Scale(Vector3Normalize(d), ray ? rnd(9, 14) : rnd(4, 8)), 0, ray ? rnd(0.3f, 0.45f) : rnd(0.6f, 0.9f), ray ? 0.3f : 0.32f, 0.08f, 0,
                 rnd(-6, 6), ray ? 0.0f : 3.0f, ray ? 4.0f : 2.5f, col, (unsigned char)(ray ? TRAIL_W : STAR), true, ray ? 0.07f : 0});
        }
    } else if (e.kind == GameEvent::Splash) {
        for (int i = 0; i < 24; i++) {
            Vector3 d = {rnd(-1, 1), 0, rnd(-1, 1)};
            add({e.pos, {d.x * 2.5f, rnd(6, 11), d.z * 2.5f}, -rnd(0, 0.15f), rnd(0.8f, 1.2f), rnd(0.4f, 0.7f), 0.2f, 0, 0, 15, 0.2f, {220, 235, 255, 230}, DROP, false});
        }
        add({e.pos, {}, 0, 0.9f, 1, 5, 0, 0, 0, 0, {255, 255, 255, 170}, RING, false});
        for (int i = 0; i < 8; i++)
            add({e.pos, {rnd(-1, 1), rnd(1, 4), rnd(-1, 1)}, 0, rnd(0.8f, 1.3f), 0.8f, 2.2f, 0, rnd(-1, 1), 2, 1.5f, {235, 245, 255, 170}, PUFF, false});  // spray
    } else if (e.kind == GameEvent::GameOver) show = 9;  // W4M GameOverLogicEntity 0x4ff8d0: WXPF_Firework1-5
}

void trail(const Projectile &s, float dt) {
    const WeaponDef &d = WEAPONS[s.weapon];
    seen.push_back({s.pos, s.weapon, s.child});
    if (d.name == "Holy Hand Grenade" && rnd() < dt * 16)  // WXP_HolyHG_Trails: crosses left floating behind
        add({s.pos, {rnd(-0.2f, 0.2f), rnd(0.1f, 0.4f), rnd(-0.2f, 0.2f)}, 0, rnd(1.8f, 2.2f), 0.35f, 0.05f, 0, rnd(-1, 1), -0.1f, 1, {255, 240, 200, 230}, CROSS, false});
    if (d.name == "Starburst") {  // rocket: WXP_Wep_StarburstRocket flames + orange glow; stars: blue trail + cyan glow
        float v = Vector3Length(s.vel);
        add({s.pos, {}, 0, 0.06f, s.child ? 0.9f : 0.75f, 0.6f, 0, 0, 0, 0, s.child ? Color{120, 230, 255, 255} : Color{255, 77, 0, 255}, GLOW, true});
        if (s.child && v > 0.5f) streaks.push_back({s.pos, Vector3Scale(s.vel, -1 / v), fminf(v * 0.15f, 2.5f), 0.3f, TRAIL_B});
        if (rnd() < dt * (s.child ? 20 : 60))
            add({s.pos, {rnd(-0.3f, 0.3f), rnd(-0.3f, 0.3f), rnd(-0.3f, 0.3f)}, 0, rnd(0.3f, 0.7f), s.child ? 0.2f : 0.4f, 0.05f, 0, rnd(-3, 3), s.child ? 3.0f : 0.0f, 1,
                 s.child ? Color{180, 245, 255, 255} : Color{255, 200, 60, 255}, (unsigned char)(s.child ? STAR : PUFF), true});
        return;
    }
    bool rocket = (d.kind == Kind::Shell && d.fuse <= 0 && d.name != "Poison Arrow") || d.kind == Kind::Homing || (d.kind == Kind::Airstrike && s.child) || d.kind == Kind::SuperSheep;
    float v = Vector3Length(s.vel);
    if (!rocket || v < 0.5f) return;
    Vector3 back = Vector3Scale(s.vel, -1 / v);
    streaks.push_back({s.pos, back, fminf(v * 0.1f, 3.5f), 0.35f, (unsigned char)(d.kind == Kind::Homing ? TRAIL_B : TRAIL_R)});
    if (rnd() < dt * 40) {  // ~40 puffs/s, independent of frame rate
        unsigned char g = (unsigned char)rnd(170, 220);
        Vector3 p = Vector3Add(s.pos, Vector3Scale(back, 0.4f));
        add({p, {rnd(-0.2f, 0.2f), rnd(0.2f, 0.6f), rnd(-0.2f, 0.2f)}, 0, rnd(0.8f, 1.3f), 0.35f, 1.3f, 0, rnd(-1, 1), -0.3f, 1, {g, g, g, 150}, PUFF, false});
    }
}

void puff(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, bool fire) {
    add({p, v, 0, life, size0, size1, 0, rnd(-1, 1), fire ? -1.0f : -0.3f, 0.6f, c, (unsigned char)(fire ? FIRE : PUFF), fire});
}

void dud(Vector3 p) {
    auto lilac = [](float t) { return ColorLerp({242, 230, 255, 220}, {128, 110, 145, 220}, t); };  // ParticleColor ramps of both emitters
    for (int i = 0; i < 20; i++) {  // WXP_MineDudPoof: 20 puffs blown out flat (normalised xz velocity), 1.2 +-0.2 s, shrinking
        float a = rnd(0, 2 * PI), v = rnd(1.5f, 3);
        add({p, {cosf(a) * v, rnd(0, 0.3f), sinf(a) * v}, 0, rnd(1.0f, 1.4f), 0.6f, 0.05f, 0, rnd(-8, 8), -0.5f, 3, lilac(rnd()), PUFF, false});
    }
    for (int i = 0; i < 10; i++)  // WXP_MineDud: one wisp per 100 ms for 1 s, rising, 2 +-0.6 s
        add({Vector3Add(p, {rnd(-0.08f, 0.08f), rnd(0, 0.1f), rnd(-0.08f, 0.08f)}), {rnd(-0.1f, 0.1f), rnd(0.8f, 1.2f), rnd(-0.1f, 0.1f)}, -0.1f * i, rnd(1.4f, 2.6f), 0.5f, 0.2f, 0,
             rnd(-4, 4), -0.1f, 0.5f, lilac(rnd(0, 0.6f)), PUFF, false});
}

void sprite(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, float grav, bool bubble) {
    if ((int)ps.size() < MAX) ps.push_back({p, v, 0, life, size0, size1, 0, 0, grav, 0, c, (unsigned char)(bubble ? BUBBLE : QUESTION), false});
}

void flame(Vector3 p, Vector3 v, float life, float size0, float size1, bool jet) {
    add({Vector3Add(p, Vector3Scale(rndDir(), size0 * 0.25f)), Vector3Add(v, Vector3Scale(rndDir(), 0.5f)), 0, life, size0, size1, 0, rnd(-2, 2), 0, 2, jet ? Color{255, 200, 140, 255} : Color{255, 110, 30, 255}, (unsigned char)(jet ? JET : TOON), true});
}

void update(float dt) {
    shake *= expf(-dt * 6);
    // W4M 0x4ffa56, per 20 ms tick after the 4 s wait: at most one per 100 ms, chance 1/40, WXPF_Firework<1 + rand % 5>,
    // at Land.Center +- Radius / 2 in x and z, Land.MaxHeight + rand x 30 units
    for (tickAcc += show > 0 && show < 5 ? dt : 0, sinceLast += dt; tickAcc >= 0.02f; tickAcc -= 0.02f)
        if (sinceLast >= 0.1f && rnd() * 40 < 1)
            sinceLast = 0, firework({stageC.x + (rnd() - 0.5f) * stageR, stageTop + rnd() * 1.5f, stageC.z + (rnd() - 0.5f) * stageR}, (int)rnd(0, 4.99f));
    show -= dt;
    for (size_t i = 0; i < ps.size();) {
        Particle &p = ps[i];
        p.age += dt;
        if (p.age < 0) { i++; continue; }
        if (p.age >= p.life) { p = ps.back(); ps.pop_back(); continue; }
        p.v = Vector3Scale(p.v, expf(-p.drag * dt));
        p.v.y -= p.grav * dt;
        p.p = Vector3Add(p.p, Vector3Scale(p.v, dt));
        p.rot += p.spin * dt;
        if (p.tail.a && rnd() < dt * 60)  // add() never reallocates (reserved MAX), so p stays valid
            add({p.p, {rnd(-0.3f, 0.3f), rnd(0, 0.3f), rnd(-0.3f, 0.3f)}, 0, rnd(0.25f, 0.41f), 0.4f, 0.05f, 0, rnd(-3, 3), -0.5f, 1, p.tail, PUFF, true});
        i++;
    }
}

void drawSky(const Camera3D &cam) {
    if (!skyTex.id || skySh.id == rlGetShaderIdDefault()) return;
    rlDrawRenderBatchActive();
    rlDisableDepthTest(), rlDisableDepthMask(), rlDisableBackfaceCulling();
    DrawMesh(dome, skyMat, MatrixMultiply(MatrixScale(100, 100, 100), MatrixTranslate(cam.position.x, cam.position.y, cam.position.z)));
    rlEnableDepthTest(), rlEnableDepthMask(), rlEnableBackfaceCulling();
}

void drawWater(const Camera3D &cam, float level, float time) {
    if (!waterTex[0].id || !waterTex[1].id || waterSh.id == rlGetShaderIdDefault()) {
        DrawPlane({40, level, 40}, {400, 400}, {30, 80, 160, 180});
        return;
    }
    Vector3 fc = {fogCol.r / 255.f, fogCol.g / 255.f, fogCol.b / 255.f};
    Vector2 fr = {FOG_NEAR, FOG_FAR};
    time = fmodf(time, 200);  // every pan speed below loops at 200 s: keeps uv offsets small
    SetShaderValue(waterSh, GetShaderLocation(waterSh, "time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(waterSh, GetShaderLocation(waterSh, "camPos"), &cam.position, SHADER_UNIFORM_VEC3);
    SetShaderValue(waterSh, GetShaderLocation(waterSh, "fogColor"), &fc, SHADER_UNIFORM_VEC3);
    SetShaderValue(waterSh, GetShaderLocation(waterSh, "fogRange"), &fr, SHADER_UNIFORM_VEC2);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    DrawMesh(plane, waterMat, MatrixMultiply(MatrixScale(400, 1, 400), MatrixTranslate(40, level, 40)));
    EndBlendMode();
}

void draw(const Camera3D &cam) {
    Matrix v = GetCameraMatrix(cam);
    Vector3 right = {v.m0, v.m4, v.m8}, up = {v.m1, v.m5, v.m9}, fwd = {-v.m2, -v.m6, -v.m10};
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    // alpha pass first (smoke behind fire reads better), one texture per batch
    for (int add : {0, 1}) {
        BeginBlendMode(add ? BLEND_ADDITIVE : BLEND_ALPHA);
        for (int t = 0; t < TEX_COUNT; t++) {
            if (!tex[t].id) continue;
            bool any = false;
            for (const Particle &p : ps) {
                if (p.tex != t || p.add != (bool)add || p.age < 0) continue;
                if (!any) rlSetTexture(tex[t].id), rlBegin(RL_QUADS), any = true;
                float k = p.age / p.life, s = Lerp(p.size0, p.size1, k) * 0.5f, c = cosf(p.rot) * s, sn = sinf(p.rot) * s;
                Vector3 r = Vector3Add(Vector3Scale(right, c), Vector3Scale(up, sn)), u = Vector3Subtract(Vector3Scale(up, c), Vector3Scale(right, sn));
                Color col = p.c;
                col.a = (unsigned char)(col.a * (1 - k) * fminf(1, k * 12 + 0.3f));
                if (p.stretch > 0) {  // trail texture: u = 0 head, 1 tail
                    Vector3 tail = Vector3Subtract(p.p, Vector3Scale(p.v, p.stretch)), w = Vector3Scale(Vector3Normalize(Vector3CrossProduct(Vector3Subtract(tail, p.p), fwd)), s);
                    rlColor4ub(col.r, col.g, col.b, col.a);
                    rlTexCoord2f(0, 0); rlVertex3f(p.p.x + w.x, p.p.y + w.y, p.p.z + w.z);
                    rlTexCoord2f(0, 1); rlVertex3f(p.p.x - w.x, p.p.y - w.y, p.p.z - w.z);
                    rlTexCoord2f(1, 1); rlVertex3f(tail.x - w.x, tail.y - w.y, tail.z - w.z);
                    rlTexCoord2f(1, 0); rlVertex3f(tail.x + w.x, tail.y + w.y, tail.z + w.z);
                } else quad(p.p, r, u, col);
            }
            if (add && (t == TRAIL_R || t == TRAIL_B))
                for (const Streak &st : streaks) {
                    if (st.tex != t) continue;
                    if (!any) rlSetTexture(tex[t].id), rlBegin(RL_QUADS), any = true;
                    // ribbon along -velocity, widened across the view direction; u runs head (bright) to tail
                    Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(st.dir, fwd)), st.width), tail = Vector3Add(st.p, Vector3Scale(st.dir, st.len));
                    rlColor4ub(255, 255, 255, 255);
                    rlTexCoord2f(0, 0); rlVertex3f(st.p.x + side.x, st.p.y + side.y, st.p.z + side.z);
                    rlTexCoord2f(0, 1); rlVertex3f(st.p.x - side.x, st.p.y - side.y, st.p.z - side.z);
                    rlTexCoord2f(1, 1); rlVertex3f(tail.x - side.x, tail.y - side.y, tail.z - side.z);
                    rlTexCoord2f(1, 0); rlVertex3f(tail.x + side.x, tail.y + side.y, tail.z + side.z);
                }
            if (any) rlEnd(), rlSetTexture(0);
        }
        EndBlendMode();
    }
    streaks.clear();
    seenPrev.swap(seen), seen.clear(), camAt = cam.target;
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
}  // namespace Fx

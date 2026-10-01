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
enum Tex { GLOW, PUFF, FIRE, DROP, SPARK, TRAIL_R, TRAIL_B, RING, TEX_COUNT };  // also the draw order within a blend pass
const char *TEX_FILES[] = {"wxp_sprite_001", "wxp_sprite_004", "wxp_sprite_030", "wxp_sprite_005", "wxp_sprite_026", "wxp_trailsprite_r", "wxp_trailsprite_b"};
const int MAX = 512;

struct Particle {
    Vector3 p, v;
    float age, life, size0, size1, rot, spin, grav, drag;  // age < 0: not born yet
    Color c;
    unsigned char tex;
    bool add;  // additive (fire, sparks) vs alpha (smoke, dust, debris, water)
};
struct Streak { Vector3 p, dir; float len, width; unsigned char tex; };

Texture2D tex[TEX_COUNT];
std::vector<Particle> ps;
std::vector<Streak> streaks;
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
    UnloadImage(ring);
    skySh = shader(SKY_VS, SKY_FS);
    waterSh = shader(WATER_VS, WATER_FS);
    dome = GenMeshSphere(1, 12, 16);
    plane = GenMeshPlane(1, 1, 1, 1);
    skyMat = LoadMaterialDefault(), waterMat = LoadMaterialDefault();
    skyMat.shader = skySh, waterMat.shader = waterSh;
    ps.reserve(MAX);
}

void theme(const std::string &theme, Color sky) {
    static const char *NAMES[] = {"jurassic", "camelot", "arabian", "wildwest", "construction", "arctic", "england", "horror", "lunar", "pirate", "war"};
    static const char LETTERS[] = "pcawbrehlto";
    char l = 'c';  // procedural island and unknown themes
    for (int i = 0; i < 11; i++)
        if (theme == NAMES[i]) l = LETTERS[i];
    if (skyTex.id) UnloadTexture(skyTex);
    for (Texture2D &t : waterTex)
        if (t.id) UnloadTexture(t), t = {};
    skyTex = {};
    fogCol = sky;
    const char *f = TextFormat(DATA_DIR "assets/ui/sky/%c_sky01.png", l);
    if (FileExists(f)) {
        Image im = LoadImage(f);
        fogCol = GetImageColor(im, im.width * 3 / 4, 0);  // sky colour at the horizon
        skyTex = LoadTextureFromImage(im);
        SetTextureFilter(skyTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(skyTex, TEXTURE_WRAP_CLAMP);
        UnloadImage(im);
    }
    for (int i = 0; i < 3; i++) waterTex[i] = loadTex("sky", TextFormat("%c_water01%c", l, 'a' + i), true);
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

void clear() { ps.clear(), streaks.clear(), shake = 0; }

Color fog() { return fogCol; }
int count() { return (int)ps.size(); }

void event(const GameEvent &e, Color dirt) {
    bool big = e.kind == GameEvent::BigBoom;
    if (e.kind == GameEvent::Boom || big) {
        float r = big ? 5.5f : 2.5f;
        shake = fmaxf(shake, big ? 0.6f : 0.18f);
        add({e.pos, {}, 0, 0.3f, r * 1.5f, r * 3, 0, 0, 0, 0, {255, 220, 150, 255}, GLOW, true});  // flash
        add({e.pos, {}, 0, 0.45f, r * 0.4f, r * 2.6f, 0, 0, 0, 0, {255, 230, 180, 200}, RING, true});
        for (int i = 0; i < (big ? 18 : 10); i++) {
            Vector3 d = rndDir();
            add({Vector3Add(e.pos, Vector3Scale(d, r * 0.2f)), Vector3Scale(d, rnd(1, 3) * r * 0.5f), -rnd(0, 0.08f), rnd(0.4f, 0.7f), r * 0.5f, r * 1.2f, 0, rnd(-2, 2), -1.5f, 4,
                 {255, (unsigned char)rnd(120, 220), (unsigned char)rnd(30, 90), 255}, FIRE, false});  // fireball
        }
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
    } else if (e.kind == GameEvent::Splash) {
        for (int i = 0; i < 24; i++) {
            Vector3 d = {rnd(-1, 1), 0, rnd(-1, 1)};
            add({e.pos, {d.x * 2.5f, rnd(6, 11), d.z * 2.5f}, -rnd(0, 0.15f), rnd(0.8f, 1.2f), rnd(0.4f, 0.7f), 0.2f, 0, 0, 15, 0.2f, {220, 235, 255, 230}, DROP, false});
        }
        add({e.pos, {}, 0, 0.9f, 1, 5, 0, 0, 0, 0, {255, 255, 255, 170}, RING, false});
        for (int i = 0; i < 8; i++)
            add({e.pos, {rnd(-1, 1), rnd(1, 4), rnd(-1, 1)}, 0, rnd(0.8f, 1.3f), 0.8f, 2.2f, 0, rnd(-1, 1), 2, 1.5f, {235, 245, 255, 170}, PUFF, false});  // spray
    }
}

void trail(const Projectile &s, float dt) {
    const WeaponDef &d = WEAPONS[s.weapon];
    bool rocket = (d.kind == Kind::Shell && d.fuse <= 0 && d.name != "Poison Arrow") || d.kind == Kind::Homing || (d.kind == Kind::Airstrike && s.child) ||
                  (d.kind == Kind::SuperSheep && d.name != "Starburst");
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

void update(float dt) {
    shake *= expf(-dt * 6);
    for (size_t i = 0; i < ps.size();) {
        Particle &p = ps[i];
        p.age += dt;
        if (p.age < 0) { i++; continue; }
        if (p.age >= p.life) { p = ps.back(); ps.pop_back(); continue; }
        p.v = Vector3Scale(p.v, expf(-p.drag * dt));
        p.v.y -= p.grav * dt;
        p.p = Vector3Add(p.p, Vector3Scale(p.v, dt));
        p.rot += p.spin * dt;
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
                quad(p.p, r, u, col);
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
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
}  // namespace Fx

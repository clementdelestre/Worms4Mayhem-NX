#include "fx.h"
#include "lit.h"
#include "audio.h"
#include "models.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <deque>
#include <map>
#include "json.h"
#include <cmath>
#include <functional>
#include <vector>

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#else
#define DATA_DIR "./"
#endif

extern "C" void glClear(unsigned int mask);

namespace Fx {
float shake = 0;

namespace {
enum Tex { GLOW, PUFF, FIRE, DROP, SPARK, TRAIL_R, TRAIL_B, STAR, TRAIL_W, RING, JET, TOON, CROSS, QUESTION, BUBBLE, SOAP, MIST, WHITEOUT, TEX_COUNT };  // also the draw order within a blend pass
const char *TEX_FILES[] = {"wxp_sprite_001", "wxp_sprite_004", "wxp_sprite_030", "wxp_sprite_005", "wxp_sprite_026", "wxp_trailsprite_r", "wxp_trailsprite_b", "wxp_sprite_007", "wxp_trailsprite_w"};
const int MAX = 1024;

struct Emit;
struct Particle {
    Vector3 p, v;
    float age, life, size0, size1, rot, spin, grav, drag;  // age < 0: not born yet
    Color c;
    unsigned char tex;
    bool add;  // additive (fire, sparks) vs alpha (smoke, dust, debris, water)
    float stretch = 0;  // > 0: ribbon along -velocity (s of travel), size = width
    Color tail = {};  // alpha > 0: leaves a trail of additive puffs of this colour (W4M anchor particles)
    float altN = 0, altS = 0;  // > 0: W4M IsAlternateAcceleration (0x5b7450), p = p0 + v (N - 1/(S t + 1/N)) + v.y t on y, t in ms
    Vector3 p0{};
    uint16_t ramp = 0;  // > 0: PARTTWK ParticleColor / ColorBand ramp (ramps) by age, ParticleAlpha, ParticleSize as below
    float fadeIn = 0, delay = 0;  // ParticleSizeFadeIn (end of the fade, s) / ParticleSizeVelocityDelay, s
    float alpha = 1;  // ParticleAlpha of a ramp particle
    float fadeA = -1;  // >= 0: ParticleAlphaVelocity < 0, linear fade to 0 at end of life from aIn + this (AlphaVelocityDelay), s
    Vector3 acc{};  // m/s², on top of grav (ParticleMass x (ParticleAcceleration + wind))
    float aIn = 0, aspect = 1;  // ParticleAlphaFadeIn s; ParticleSize y / x
    float spR = 0, spW = 0;  // ParticleIsSpiral: radius m, rad/s
    bool head = false;  // kTrail sprite set "A,B": B (WXSprite1) drawn at the head (0x5bc6e5)
    uint32_t id = 0;  // > 0: carries an EmitterParticleFX emitter
    const Emit *expire = nullptr;  // EmitterParticleExpireFX
    // ramp particles, 0x5b6f40: S half extents (m; 0 = size0, size0 x aspect), F FinalSizeScale (< 0: SizeVelocity V, m/s), d0 SizeFadeInDelay s
    Vector2 S{}, F{-1, -1}, V{-1, -1};
    float d0 = 0, alphaV = 0, aDelay = 0, spS = 0, windK = 0;  // AlphaVelocity >= 0 (/s) from aIn + aDelay; spiral shrink m/s; wind x Mass, m/s² per unit
    bool immortal = false;  // ParticleLife 65535 (0x5b73d0)
    int8_t mesh = -1;  // MeshSet particle (meshes), drawn as that model
    Vector3 ori{}, oriV{};  // mesh particles: ParticleOrientation, rad and rad/s
    const std::vector<const Emit *> *expireL = nullptr;  // data EmitterParticleExpireFX (an effect or one emitter)
    float unit = 0.05f;  // m per W4M unit of the emitter, for the emitters it carries or leaves
    int level = -1;  // map emitter that spawned it: its deletion takes immortal particles (0x5bbfa9)
};
// ParticleColorBand 0x5b77c0: edge[i] = end of segment c[i] -> c[i+1]; the last segment runs to 1
struct Ramp { int n; Color c[5]; float edge[5]; };
const Ramp RAMPS[] = {
    {},
    {5, {{255, 255, 255, 255}, {255, 204, 0, 255}, {255, 153, 77, 255}, {204, 128, 0, 255}, {26, 0, 0, 255}}, {0, 0.1f, 0.15f, 0.3f, 1}},  // WXP_StarBurstRocketFlames
    {5, {{255, 255, 255, 255}, {255, 204, 0, 255}, {230, 153, 77, 255}, {128, 77, 0, 255}, {26, 0, 0, 255}}, {0, 0.1f, 0.15f, 0.3f, 1}},  // WXP_BazookaTrail_Main
    {2, {{230, 242, 255, 255}, {179, 204, 255, 255}}, {0, 1}},  // WXP_BazookaTrail_Puffs, WXP_JetpackStartRing / StartBase
    {4, {{255, 255, 255, 255}, {191, 230, 255, 255}, {179, 191, 255, 255}, {255, 255, 255, 255}}, {0, 0.1f, 0.2f, 1}},  // WXP_HomingMissileSmoke
    {1, {{0, 128, 255, 255}}, {0, 1}},  // WXP_HomingMissileGlow
    {1, {{255, 255, 255, 255}}, {0, 1}},  // WeaponBazookaPuff
    {2, {{255, 255, 255, 255}, {140, 140, 140, 255}}, {0, 1}},  // WXP_DonkeySpritePuffLG / Horiz
    {2, {{242, 255, 255, 255}, {153, 204, 255, 255}}, {0, 1}},  // WXP_CrateSpawnLGRings
    {1, {{77, 255, 255, 255}}, {0, 1}},  // WXP_StarburstGlowBangs / EXPGlowBang1
    {1, {{128, 230, 255, 255}}, {0, 1}},  // WXP_StarB_ManyGlows
    {1, {{255, 128, 0, 255}}, {0, 1}},  // WXPF_RedGlow / RedBigGlow(Delayed)
    {1, {{0, 255, 64, 255}}, {0, 1}},  // WXPF_GreenGlowBang / BigBang
    {2, {{255, 255, 255, 255}, {255, 204, 0, 255}}, {0, 1}},  // WXPF_Exploder_1/2, RedExpiryFlash, Twinkle*
    {1, {{255, 255, 128, 255}}, {0, 1}},  // WXPF_ExploderGreen, GreenTrail1
    {2, {{255, 255, 64, 255}, {0, 204, 0, 255}}, {0, 1}},  // WXPF_EndGlowGreen
};
enum RampId { R_NONE, R_STARBURST, R_BAZ_MAIN, R_BAZ_PUFF, R_HOM_SMOKE, R_HOM_GLOW, R_MIST, R_DONKEY_PUFF, R_DONKEY_RING,
              R_SB_CYAN, R_SB_GLOW, R_FW_RED, R_FW_GREEN, R_FW_STAR, R_FW_PALE, R_FW_ENDGREEN, R_WHITE = R_MIST };
struct Streak { Vector3 p, dir; float len, width; unsigned char tex; };

std::vector<Texture2D> tex(TEX_COUNT);  // past TEX_COUNT: the data sprite sets (sprites.txt)
std::vector<Ramp> ramps(std::begin(RAMPS), std::end(RAMPS));
std::vector<Particle> ps;
std::vector<Streak> streaks;
struct Seen { Vector3 p; int weapon; bool child; float age; };  // age: s since the rocket's emitters started
std::vector<Seen> seen, seenPrev;  // live shots of this / the previous frame: which weapon blew up where
float show = 0, tickAcc = 0;  // victory fireworks: s left of W4M GameOverLogicEntity's 4 s wait + 5 s show
Vector3 stageC{};  // Land.Center, Land.Radius, Land.MaxHeight of the match that ended
float stageR = 0, stageTop = 0;
Texture2D skyTex{}, waterTex[3]{};
Shader skySh{}, waterSh{};
int waterLoc[6];
Mesh dome{}, plane{};
Material skyMat{}, waterMat{};
Model skyModel{};  // W4M SkyBoxEntity scene "<THEME>.<TIME>Sky" (tools/w4m-models), W4M units
// per sky mesh: XBlendModeGL source / dest factor (-1 opaque), clip change of rotate Y (rad) and texture offset U, V, last key s
struct SkyPart { int src = -1, dst = -1; float rot = 0, u = 0, v = 0, t1 = 0; };
std::vector<SkyPart> skyParts;
float skyClip = 0, skyT = 0;  // SkyBoxEntity's one clip, looped (0x486019)
constexpr float SKY_K = 0.04f;  // the ~20000-unit sky scene shrunk inside the far plane, centred on the camera
Shader skyMeshSh{};
int uvOffLoc = -1;
bool hasSun = false;  // the scene's Sun locator: LensFlareGraphicEntity "LF.<sky>"
Vector3 sunAt{};
int flareSet = 0;
float flareFade = 0, flareOwed = 0;
Texture2D flareTex{};
// Particle.WXPMesh7 emitters (one particle each): ParticleSize as (xz, y) like the sprites' aspect [assumed]
Model domeModel{};
struct Dome { Vector3 p; float sxz, sy, age; };
std::vector<Dome> domes;
// WXM_DefSource scales 0 -> 1 then holds (no LOOP:); its material alpha key reaches no CG program (materialDiffuseCol is a constant in
// FixedFunction.cg), so the dome stays for ParticleLife 5000 ms
constexpr float DOME_LIFE = 5;
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
    p.rot = rnd(0, 2 * PI), p.p0 = p.p;
    ps.push_back(p);
}

Vector3 windNow{};  // (cos, 0, sin) x Wind.Speed / Wind.MaxSpeed (setWind)
// CParticle position 0x5b7450: p0 + v t + (acc + wind - grav) t² / 2 (wind read now: the closed form follows its changes), or the alternate
// curve (t in ms, no gravity); plus the spiral circle in x, z while its radius (shrinking by SpiralRadiusSizeVelocity) is > 0
Vector3 posAt(const Particle &p, float t) {
    Vector3 q;
    if (p.altN > 0) q = Vector3Add(p.p0, Vector3Add(Vector3Scale(p.v, (p.altN - 1 / (p.altS * t * 1000 + 1 / p.altN)) / 1000), {0, p.v.y * t, 0}));
    else q = Vector3Add(p.p0, Vector3Add(Vector3Scale(p.v, t), Vector3Scale({p.acc.x + windNow.x * p.windK, p.acc.y - p.grav, p.acc.z + windNow.z * p.windK}, t * t / 2)));
    if (float r = p.spR - p.spS * t; r > 0) q.x += sinf(p.spW * t) * r, q.z += cosf(p.spW * t) * r;
    return q;
}

// ParticleSize 0x5b6f40 (half extents, written to the XSpriteSet Size, 0x5b7ecb): 0 before SizeFadeInDelay, linear up to S at SizeFadeIn,
// S until SizeVelocityDelay, then linear to S x FinalSizeScale at end of life, or (no FinalSizeScale) S + SizeVelocity t, or a shrink to 0 when
// that velocity is negative (or shrink: a mesh particle with AlphaVelocity < 0, 0x5bd69e)
Vector2 sizeAt(const Particle &p, float t, bool shrink = false) {
    Vector2 S = p.S.x > 0 || p.S.y > 0 ? p.S : Vector2{p.size0, p.size0 * p.aspect};
    if (t < p.d0) return {0, 0};
    if (t < p.fadeIn) return Vector2Scale(S, (t - p.d0) / (p.fadeIn - p.d0));
    if (t < p.delay) return S;
    float u = (t - p.delay) / (p.life - p.delay);
    if (p.F.x >= 0) return {S.x * (p.F.x * u + 1 - u), S.y * (p.F.y * u + 1 - u)};
    return {p.V.x >= 0 && !shrink ? S.x + p.V.x * t : (1 - u) * S.x, p.V.y >= 0 && !shrink ? S.y + p.V.y * t : (1 - u) * S.y};
}

// ParticleAlpha 0x5b7660: linear fade-in over AlphaFadeIn, then past AlphaVelocityDelay a linear fade to 0 at end of life (AlphaVelocity < 0)
// or a drop of AlphaVelocity per ms; clamped to 0..1
float alphaAt(const Particle &p, float t) {
    float a = p.alpha, from = p.aIn + (p.fadeA >= 0 ? p.fadeA : p.aDelay);
    if (t < p.aIn) a *= t / p.aIn;
    else if (p.fadeA >= 0 && t > from) a *= 1 - (t - from) / (p.life - from);
    else if (p.alphaV > 0 && t > from) a -= (t - from) * p.alphaV;
    return Clamp(a, 0, 1);
}

// PARTTWK ParticleEmitterContainer fields, in W4M units (20 per m) and ms
struct Emit {
    unsigned char tex; bool add = true, head = false, trail = false;  // trail: EmitterType kTrail
    int num = 1, max = 1; float lifeTime = 0, freq = 0, freqR = 0, delay = 0;
    float life = 0, lifeR = 0, size = 0, sizeR = 0, aspect = 1, shrink = -1, shrinkR = 0, sizeIn = 0;  // shrink: SizeVelocityDelay of a SizeVelocity < 0; sizeIn: SizeFadeIn
    float alpha = 1, alphaIn = 0, fade = -1;  // fade: AlphaVelocityDelay of an AlphaVelocity < 0
    Vector3 v{}, vR{}; bool norm = false; float mass = 0, altN = 0, altS = 0;  // mass: x ParticleAcceleration (0, -1, 0)
    Vector3 off{}, offR{}; float spiral = 0, spiralW = 0, rot = 0, rotR = 0, spinR = 0;
    uint16_t col = R_WHITE; int sfx = -1; const Emit *fx = nullptr, *expire = nullptr;  // EmitterSoundFX, EmitterParticleFX, EmitterParticleExpireFX
    // data emitters (parttwk.json), W4M units and ms: the fields the hand-written ones above leave at their defaults
    bool w4m = false, wind = false, infinite = false, immortal = false, sfxLoop = false, sfxHold = false;  // EmitterLifeTime / ParticleLife 65535
    int kind = 0, numR = 0; float lifeTimeR = 0;  // EmitterType (2 kRain), EmitterNumSpawnRadnomise, EmitterLifeTimeRandomise
    Vector2 sizeV{}, sizeVR{}, fin{-1, -1}; float finR = 0, sizeInR = 0, sizeInDelay = 0, sizeInDelayR = 0, alphaV = 0, aDelay = 0;  // alphaV: AlphaVelocity >= 0, per ms
    float spiralR = 0, spiralWR = 0, spiralS = 0; Vector3 acc{0, -1, 0}, accR{}, ori{}, oriR{}, oriV{}, oriVR{};
    int8_t mesh = -1; bool spiralOn = false, altOn = false;
    const std::vector<const Emit *> *fxL = nullptr, *expireL = nullptr;  // data EmitterParticleFX / ExpireFX
    int pool() const;  // particle slots, 0x5b9f24
};
// Running emitter (ParticleEmitterEffectEntity): age, spawn timer, its randoms (0x5ba3bc..0x5ba446), particle slot ends, batch progress
struct Live {
    const Emit *e; Vector3 at; uint32_t follow = 0; float clock = 0, next = 0, timer = 0, jitter = 0; bool started = false; std::vector<float> ends;
    int key = 0, level = -1; float unit = 0.05f, lifeR = 0; int extra = 0, spawned = 0, batch = 0; bool init = false;  // level: map emitter index
    int handle = 0;  // its effect's (0x5c1410)
};
std::vector<Live> lives, born;  // running emitters; those created while they tick
uint32_t lastId = 0;
int lastKey = 0, curLevel = -1;  // curLevel: the map emitter spawn() is spawning for
const float TRAIL_DT = 0.02f;
const int TRAIL_SEGS = 24;  // TrailGraphicEntity: 25 points (0x5bc899), shifted once per 20 ms update (0x5c3aa0 -> 0x5c2ae0)

int Emit::pool() const {  // min(MaxParticles, (Life + LifeRand) / (SpawnFreq - SpawnFreqRand) x (NumSpawn + NumSpawnRand, 0 -> 1) + 1), integers
    int f = (int)freq - (int)freqR, n = num + numR;
    return life > 0 && f > 0 ? std::min(max, (int)(life + lifeR) / f * (n ? n : 1) + 1) : max;
}

// CParticle setup 0x5b98e0: V + (2r - 1) Vrand per axis x 0.01 units/ms (IsNormalised: the unit vector x (V.x + Vrand.x)); returns the life, s.
// u: metres per W4M unit (map emitters: the import scale k / 20)
float spawn(const Emit &e, Vector3 at, float u = 0.05f) {
    auto r = [](float x) { return rnd(-x, x); };
    Vector3 v = {e.v.x + r(e.vR.x), e.v.y + r(e.vR.y), e.v.z + r(e.vR.z)}, o = {e.off.x + r(e.offR.x), e.off.y + r(e.offR.y), e.off.z + r(e.offR.z)};
    if (e.norm) v = Vector3Scale(Vector3Normalize(v), e.v.x + e.vR.x);
    Particle q = {Vector3Add(at, Vector3Scale(o, u)), Vector3Scale(v, 10 * u), 0, (e.life + r(e.lifeR)) / 1000, (e.size + r(e.sizeR)) * u, 0, (e.rot + r(e.rotR)) * DEG2RAD,
                  r(e.spinR) * DEG2RAD * 10, e.altN > 0 ? 0 : 100 * u * e.mass, 0, WHITE, e.tex, e.add, e.trail ? TRAIL_SEGS * TRAIL_DT : 0};
    q.altN = e.altN, q.altS = e.altS, q.p0 = q.p, q.ramp = e.col, q.fadeIn = e.sizeIn / 1000, q.delay = e.shrink < 0 ? 1e9f : (e.shrink + r(e.shrinkR)) / 1000;
    q.alpha = e.alpha, q.aIn = e.alphaIn / 1000, q.fadeA = e.fade < 0 ? -1 : e.fade / 1000, q.aspect = e.aspect;
    q.spR = e.spiral * u, q.spW = e.spiralW * 10, q.head = e.head, q.expire = e.expire;
    if (e.w4m) {
        float k = rnd(-1, 1), kv = rnd(-1, 1);  // 0x5b6a30 / 0x5b6920: one random for both axes (SizeVelocityRandomise y < 0 reuses x's)
        q.S = {(e.size + k * e.sizeR) * u, (e.size * e.aspect + k * e.sizeR) * u};
        q.V = Vector2Scale({e.sizeV.x + kv * e.sizeVR.x, e.sizeV.y + (e.sizeVR.y < 0 ? kv * e.sizeVR.x : r(e.sizeVR.y))}, 10 * u);
        q.F = e.fin.x < 0 ? Vector2{-1, -1} : Vector2AddValue(e.fin, r(e.finR));  // FinalSizeScale (0, 0) means none (0x5b9a83)
        q.d0 = (e.sizeInDelay + r(e.sizeInDelayR)) / 1000, q.fadeIn = (e.sizeIn + r(e.sizeInR)) / 1000;
        q.delay = (e.shrink + r(e.shrinkR)) / 1000;
        q.alphaV = e.alphaV * 1000, q.aDelay = e.aDelay / 1000;
        Vector3 a = {e.acc.x + r(e.accR.x), e.acc.y + r(e.accR.y), e.acc.z + r(e.accR.z)};
        q.grav = 0, q.acc = Vector3Scale(a, 100 * u * e.mass), q.windK = e.wind ? 85 * u * e.mass : 0;
        q.altN = e.altOn ? e.altN : 0;
        q.spR = e.spiralOn ? (e.spiral + r(e.spiralR)) * u : 0, q.spW = (e.spiralW + r(e.spiralWR)) * 10, q.spS = e.spiralS * 10 * u;
        q.ori = Vector3Scale({e.ori.x + r(e.oriR.x), e.ori.y + r(e.oriR.y), e.ori.z + r(e.oriR.z)}, DEG2RAD);
        q.oriV = Vector3Scale({e.oriV.x + r(e.oriVR.x), e.oriV.y + r(e.oriVR.y), e.oriV.z + r(e.oriVR.z)}, DEG2RAD * 10);
        q.rot = q.ori.z, q.spin = q.oriV.z, q.immortal = e.immortal, q.mesh = e.mesh;
    }
    if ((int)ps.size() >= MAX) return q.life;
    q.unit = u, q.expireL = e.expireL, q.level = curLevel;
    if (e.fx || e.fxL) q.id = ++lastId;
    if (e.fx) born.push_back({e.fx, q.p, q.id});
    if (e.fxL)
        for (const Emit *x : *e.fxL) { Live l{x, q.p, q.id}; l.unit = u, l.level = curLevel; born.push_back(std::move(l)); }
    ps.push_back(q);
    return q.immortal ? 1e30f : q.life;
}

// ParticleEmitterEffectEntity 0x5bb690 / 0x5bab20 per 20 ms (docs/w4m/render.md §3): the spawn timer starts at SpawnFreq (so the first update
// past StartDelay spawns), each batch of NumSpawn (+ rand) fills free slots of the pool; past EmitterLifeTime (+ rand, 65535 = forever) with the
// pool spawned once, it stops (0x5bc022). An attached emitter (EmitterParticleFX) follows its particle and stops with it [assumed]
void tickEmitters(float dt) {
    auto rr = [](float x) { return truncf(rnd(-x, x)); };
    for (size_t i = 0; i < lives.size();) {
        Live &L = lives[i];
        const Emit &e = *L.e;
        bool done = false;
        if (L.follow) {
            auto it = std::find_if(ps.begin(), ps.end(), [&](const Particle &p) { return p.id == L.follow; });
            if (it == ps.end()) done = true;
            else L.at = it->p;
        }
        int pool = e.pool();
        curLevel = L.level;
        for (L.clock += dt; !done && L.clock >= L.next; L.next += 0.02f) {
            float t = L.next * 1000;
            if (!L.init) L.init = true, L.timer = e.freq, L.jitter = rr(e.freqR), L.extra = (int)rr((float)e.numR), L.lifeR = rr(e.lifeTimeR);
            else L.timer += 20;
            if (t >= e.delay) {
                if (!L.started) {
                    L.started = true;
                    if (e.sfx >= 0 && !e.sfxLoop && !e.sfxHold) Audio::play((Audio::Sfx)e.sfx, L.at);
                }
                L.ends.erase(std::remove_if(L.ends.begin(), L.ends.end(), [&](float end) { return end <= L.next; }), L.ends.end());  // next only grows
                for (bool go = L.timer >= e.freq + L.jitter; go && (int)L.ends.size() < pool;) {
                    L.ends.push_back(L.next + spawn(e, L.at, L.unit)), L.spawned++;
                    if (++L.batch >= e.num + L.extra) L.batch = 0, L.timer -= e.freq + L.jitter, L.jitter = rr(e.freqR), L.extra = (int)rr((float)e.numR), go = false;
                }
            }
            if (!e.infinite && t + 20 > fmaxf(e.lifeTime, 1) + L.lifeR && L.spawned >= pool) done = true;
        }
        if (!done && L.started && e.sfx >= 0 && e.sfxLoop) Audio::emitter(L.key, (Audio::Sfx)e.sfx, L.at);
        if (!done && L.started && e.sfx >= 0 && e.sfxHold) Audio::hold((Audio::Sfx)e.sfx, true, &L.at);
        if (done) lives[i] = std::move(lives.back()), lives.pop_back();
        else i++;
    }
    curLevel = -1;
    for (Live &l : born) l.key = ++lastKey, lives.push_back(std::move(l));
    born.clear();
}
void rainStart(const Emit *e, Vector3 origin, bool weather, int level, int handle = 0);
int lastHandle = 0;
// ParticleHandlerService 0x5c1410: an EffectDetailsContainer's emitters at one point, kSnow / kRain as SnowParticleEmitterEntity (0x5c0c44);
// returns the handle kill() takes (0x5bfde0)
int effect(const std::vector<const Emit *> &list, Vector3 at, float unit = 0.05f, int level = -1) {
    int h = ++lastHandle;
    for (const Emit *e : list) {
        if (e->kind == 1 || e->kind == 2) { rainStart(e, at, false, level, h); continue; }
        Live l{e, at};
        l.key = ++lastKey, l.unit = unit, l.level = level, l.handle = h;
        lives.push_back(std::move(l));
    }
    return h;
}
// WAE_Jetpack takeoff [data PARTTWK]: WXSprite4 (alpha), (.9, .95, 1) to (.7, .8, 1), orientation 30 +- 20, spin +- 30, thrown flat (normalised)
const Emit JET_RING = {.tex = PUFF, .add = false, .num = 30, .max = 30, .lifeTime = 1, .life = 1000, .lifeR = 300, .size = 9.5f, .sizeR = 1.5f, .shrink = 0,
                       .sizeIn = 20, .vR = {1.2f, 0, 1.2f}, .norm = true, .altN = 6500, .altS = 1e-6f, .rot = 30, .rotR = 20, .spinR = 30, .col = R_BAZ_PUFF};
const Emit JET_BASE = {.tex = PUFF, .add = false, .max = 30, .lifeTime = 2000, .life = 800, .lifeR = 300, .size = 7, .sizeR = 1.5f, .shrink = 250,
                       .sizeIn = 200, .vR = {0.8f, 0, 0.8f}, .norm = true, .altN = 6500, .altS = 1e-6f, .rot = 30, .rotR = 20, .spinR = 30, .col = R_BAZ_PUFF};

const int FW_SFX = (int)Audio::Sfx::Fireworks, BANG_SFX = (int)Audio::Sfx::Explosion;  // global/FireWorksExplosion, global/ExplosionRegular
// PARTTWK [data]: glows (WXSprite1), whiteouts, kTrail ribbons, stars (WXSprite7) and their sparkles (WXSprite26)
const Emit SB_GLOWS = {.tex = GLOW, .num = 2, .max = 2, .lifeTime = 10, .life = 800, .size = 30, .fade = 0, .off = {0, 4, 0}, .col = R_SB_CYAN};
const Emit SB_BIG_GLOW = {.tex = GLOW, .lifeTime = 10, .delay = 5, .life = 1800, .size = 120, .fade = 0, .off = {0, 4, 0}, .col = R_SB_CYAN};
const Emit SB_MANY_GLOWS = {.tex = GLOW, .num = 2, .max = 20, .lifeTime = 10, .delay = 400, .life = 800, .size = 20, .fade = 0, .offR = {100, 100, 100}, .col = R_SB_GLOW};
const Emit SB_TRAILS_A = {.tex = TRAIL_B, .trail = true, .num = 16, .max = 16, .lifeTime = 100, .freq = 1, .life = 1200, .lifeR = 400, .size = 1, .vR = {5, 5, 5},
                          .altN = 5000, .altS = 3e-6f, .spiral = 1, .spiralW = 4};
const Emit SB_TRAILS_B = {.tex = TRAIL_B, .trail = true, .num = 16, .max = 16, .lifeTime = 100, .freq = 1, .life = 1750, .lifeR = 400, .size = 1, .vR = {7, 7, 7},
                          .altN = 5000, .altS = 3e-6f, .spiral = 1, .spiralW = 4, .sfx = FW_SFX};
const Emit WHITEOUT_FW = {.tex = WHITEOUT, .life = 80, .size = 400, .aspect = 0.75f, .alpha = 0.1f, .alphaIn = 50, .fade = 1000, .sfx = FW_SFX};
const Emit WHITEOUT_DELAYED = {.tex = WHITEOUT, .delay = 250, .life = 80, .size = 400, .aspect = 0.75f, .alpha = 0.1f, .alphaIn = 50, .fade = 1000};
const Emit WHITEOUT_LARGE = {.tex = WHITEOUT, .life = 80, .size = 400, .aspect = 0.75f, .alpha = 0.6f, .alphaIn = 50, .fade = 1000};  // WXP_WhiteoutflashLarge
const Emit RED_GLOW = {.tex = GLOW, .num = 2, .max = 2, .lifeTime = 10, .life = 800, .size = 30, .fade = 0, .off = {0, 4, 0}, .col = R_FW_RED, .sfx = FW_SFX};
const Emit RED_BIG_GLOW = {.tex = GLOW, .lifeTime = 10, .delay = 5, .life = 1800, .size = 120, .fade = 0, .off = {0, 4, 0}, .col = R_FW_RED, .sfx = FW_SFX};
const Emit RED_BIG_GLOW_DELAYED = {.tex = GLOW, .num = 2, .max = 2, .lifeTime = 10, .delay = 250, .life = 1800, .size = 200, .sizeR = 50, .fade = 0, .off = {0, 4, 0},
                                   .rotR = 360, .col = R_FW_RED};
const Emit GREEN_GLOW = {.tex = GLOW, .num = 2, .max = 2, .lifeTime = 10, .life = 800, .size = 30, .fade = 0, .off = {0, 4, 0}, .col = R_FW_GREEN};
const Emit GREEN_BIG_GLOW = {.tex = GLOW, .lifeTime = 10, .delay = 5, .life = 1800, .size = 120, .fade = 0, .off = {0, 4, 0}, .col = R_FW_GREEN};
// kTrail ribbons with a glow head: V (0, 30, 0) +- 40, Mass 4.6 (23 m/s²), shrinking from 800 ms
const Emit BLUE_TRAILS = {.tex = TRAIL_B, .head = true, .trail = true, .num = 12, .max = 12, .lifeTime = 1, .life = 1600, .lifeR = 400, .size = 2, .sizeR = 0.5f,
                          .shrink = 800, .v = {0, 30, 0}, .vR = {40, 40, 40}, .mass = 4.6f, .rotR = 5};
const Emit RED_TRAILS = {.tex = TRAIL_R, .head = true, .trail = true, .num = 12, .max = 12, .lifeTime = 1, .life = 1600, .lifeR = 400, .size = 4, .sizeR = 0.5f,
                         .shrink = 800, .v = {0, 30, 0}, .vR = {40, 40, 40}, .mass = 4.6f, .rotR = 5};
const Emit RED_TRAILS_LONG = {.tex = TRAIL_R, .head = true, .trail = true, .num = 18, .max = 18, .lifeTime = 1, .life = 3000, .lifeR = 400, .size = 2, .sizeR = 0.5f,
                              .shrink = 800, .v = {0, 30, 0}, .vR = {40, 40, 40}, .mass = 4.6f, .rotR = 5};
// sparkles: still (normalised V of speed V.x + Vrand.x = 0), shrinking to 0; the anchors fall (Mass 2: 10 m/s²)
const Emit RED_EXPIRY = {.tex = SPARK, .add = false, .max = 3, .lifeTime = 25, .freq = 50, .freqR = 25, .life = 250, .size = 15, .shrink = 0, .v = {0, 0.02f, 0},
                         .norm = true, .altN = 6500, .altS = 2e-6f, .offR = {10, 10, 10}, .rotR = 360, .spinR = 30, .col = R_FW_STAR};
const Emit TWINKLE_TRAILS = {.tex = SPARK, .add = false, .max = 5, .lifeTime = 1500, .freq = 150, .freqR = 25, .life = 600, .size = 15, .shrink = 0, .v = {0, 0.02f, 0},
                             .norm = true, .altN = 6500, .altS = 2e-6f, .offR = {10, 10, 10}, .rotR = 360, .spinR = 30, .col = R_FW_STAR};
const Emit TWINKLE_ANCHOR = {.tex = SPARK, .add = false, .lifeTime = 25, .freq = 50, .freqR = 25, .life = 2000, .size = 15, .shrink = 0, .v = {0, 0.02f, 0}, .norm = true,
                             .mass = 2, .offR = {10, 10, 10}, .rotR = 360, .spinR = 30, .col = R_FW_STAR, .fx = &TWINKLE_TRAILS};
const Emit GREEN_TRAIL = {.tex = SPARK, .add = false, .max = 3, .lifeTime = 1500, .freq = 50, .freqR = 30, .life = 200, .size = 15, .shrink = 0, .v = {0, 0.02f, 0},
                          .norm = true, .altN = 6500, .altS = 2e-6f, .offR = {10, 10, 10}, .rotR = 360, .spinR = 30, .col = R_FW_PALE};
const Emit END_GLOW_GREEN = {.tex = GLOW, .max = 2, .lifeTime = 25, .life = 500, .size = 50, .sizeR = 25, .shrink = 0, .v = {0, 0.02f, 0}, .norm = true, .mass = 2,
                             .rotR = 360, .spinR = 30, .col = R_FW_ENDGREEN};
// stars: thrown out on the alternate curve (radius 4 / 6 / 8 x 0.01 x 6500 units = 13 / 19.5 / 26 m), constant size
const Emit EXPLODER_1 = {.tex = STAR, .add = false, .num = 30, .max = 30, .lifeTime = 1, .freq = 80, .life = 800, .size = 3, .shrink = 2200, .shrinkR = 400, .vR = {4, 4, 4},
                         .norm = true, .altN = 6500, .altS = 2e-6f, .spinR = 8, .col = R_FW_STAR, .sfx = BANG_SFX, .expire = &RED_EXPIRY};
const Emit EXPLODER_2 = {.tex = STAR, .add = false, .num = 50, .max = 50, .lifeTime = 1, .freq = 80, .delay = 250, .life = 1500, .size = 3, .shrink = 2200, .shrinkR = 400,
                         .vR = {6, 6, 6}, .norm = true, .altN = 6500, .altS = 2e-6f, .spinR = 8, .col = R_FW_STAR, .sfx = BANG_SFX, .expire = &TWINKLE_ANCHOR};
const Emit EXPLODER_GREEN = {.tex = STAR, .add = false, .num = 50, .max = 50, .lifeTime = 1, .freq = 80, .life = 1500, .lifeR = 100, .size = 3, .shrink = 2200, .shrinkR = 400,
                             .vR = {8, 8, 8}, .norm = true, .altN = 6500, .altS = 2e-6f, .spinR = 8, .col = R_FW_PALE, .sfx = BANG_SFX, .fx = &GREEN_TRAIL, .expire = &END_GLOW_GREEN};
const std::vector<const Emit *> FIREWORKS[] = {  // WXPF_Firework1-5
    {&SB_GLOWS, &SB_TRAILS_A, &SB_BIG_GLOW, &WHITEOUT_FW, &SB_TRAILS_B, &BLUE_TRAILS},
    {&RED_GLOW, &RED_BIG_GLOW, &WHITEOUT_FW, &RED_TRAILS, &EXPLODER_1, &RED_TRAILS},
    {&RED_GLOW, &RED_BIG_GLOW, &WHITEOUT_FW, &EXPLODER_1, &EXPLODER_2, &RED_BIG_GLOW_DELAYED, &WHITEOUT_DELAYED},
    {&GREEN_GLOW, &GREEN_BIG_GLOW, &WHITEOUT_FW, &EXPLODER_GREEN},
    {&RED_GLOW, &RED_BIG_GLOW, &WHITEOUT_FW, &RED_TRAILS_LONG, &RED_TRAILS_LONG, &RED_TRAILS_LONG, &RED_TRAILS_LONG},
};
// WXP_StarburstExplosion without its WXP_ExplosionX_Med (drawn by the common blast)
const std::vector<const Emit *> STARBURST = {&SB_GLOWS, &SB_TRAILS_A, &SB_TRAILS_A, &SB_BIG_GLOW, &WHITEOUT_LARGE, &SB_MANY_GLOWS, &SB_TRAILS_B};

// ---- data effects: assets/fx/parttwk.json (tools/w4m-re/parttwk.py), started by name like ParticleHandlerService 0x5c09d0 ----
// PARTTWK MeshSet: models/fx/<name>.glb, MeshAnimNodeName clips; its .blend (tools/w4m-models): first part's XBlendModeGL and the clip's
// texture offset change (u, v over t1 s, 2-key linear)
struct MeshSet { std::string model, clip, clip2; int src = -1, dst = -1; float u = 0, v = 0, t1 = 0; };
std::vector<MeshSet> meshes;
std::deque<Emit> dataEmits;
int rainSplashTex = -1;
bool rainSplashAdd = false;
std::map<std::string, std::vector<const Emit *>> dataFx;  // lowercase name (the resource trie folds case, 0x6bdff0) -> its emitters
bool dataLoaded = false;

std::string lower(std::string n) { for (char &c : n) c = (char)tolower((unsigned char)c); return n; }
// unknown names fall back to XXX_PlaceholderPP (0x5c0aae)
const std::vector<const Emit *> *fxNamed(const std::string &name) {
    auto it = dataFx.find(lower(name));
    if (it == dataFx.end()) it = dataFx.find("xxx_placeholderpp");
    return it == dataFx.end() ? nullptr : &it->second;
}

void loadData() {
    if (dataLoaded) return;
    dataLoaded = true;
    struct Spr { int tex; bool add; };
    std::map<std::string, Spr> sprites;
    if (char *t = LoadFileText(DATA_DIR "assets/fx/sprites.txt")) {
        for (char *l = strtok(t, "\n"); l; l = strtok(nullptr, "\n")) {
            char name[96];
            int src, dst;
            if (sscanf(l, "%95s %d %d", name, &src, &dst) != 3) continue;
            const char *f = TextFormat(DATA_DIR "assets/fx/%s.png", lower(name).c_str());
            if (!FileExists(f)) continue;
            Texture2D x = LoadTexture(f);
            GenTextureMipmaps(&x), SetTextureFilter(x, TEXTURE_FILTER_TRILINEAR);
            sprites[lower(name)] = {(int)tex.size(), dst == 1};  // XBlendModeGL: SrcAlpha / One additive, else SrcAlpha / OneMinusSrcAlpha
            if (lower(name) == "particle.rainsplash") rainSplashTex = (int)tex.size(), rainSplashAdd = dst == 1;
            tex.push_back(x);
        }
        UnloadFileText(t);
    }
    char *txt = LoadFileText(DATA_DIR "assets/fx/parttwk.json");
    if (!txt) return;
    Json j;
    bool ok = Json::parse(txt, j);
    UnloadFileText(txt);
    if (!ok) return;
    auto v3 = [](const Json &a) { return Vector3{a[0].f(), a[1].f(), a[2].f()}; };
    std::vector<std::pair<Emit *, const Json *>> made;
    for (const auto &[name, c] : j["emitters"].obj) {
        Emit &e = dataEmits.emplace_back();
        e.w4m = true, e.kind = (int)c["EmitterType"].f(), e.trail = e.kind == 3;
        std::string set = c["SpriteSet"].s(), a = set.substr(0, set.find(','));
        auto sp = sprites.find(lower(a));
        e.tex = sp == sprites.end() ? 255 : (unsigned char)sp->second.tex, e.add = sp != sprites.end() && sp->second.add;
        e.head = set.find(',') != std::string::npos;  // "A,B": B drawn at the particles (0x5bc735)
        if (c["MeshSet"].size() && !c["MeshSet"][0].s().empty()) {
            std::string clips = c["MeshAnimNodeName"].s(), c1, c2;
            for (size_t k = 0, n = 0; k <= clips.size(); k++)
                if (k == clips.size() || clips[k] == '+') {
                    std::string part = clips.substr(n, k - n);
                    part = part.substr(part.find(':') + 1);
                    (c1.empty() ? c1 : c2) = part, n = k + 1;
                }
            MeshSet ms{lower(c["MeshSet"][0].s()), c1, c2};
            if (char *b = LoadFileText(TextFormat(DATA_DIR "assets/models/fx/%s.blend", ms.model.c_str()))) {
                float rot;
                sscanf(b, "%d %d %f %f %f %f", &ms.src, &ms.dst, &rot, &ms.u, &ms.v, &ms.t1);
                UnloadFileText(b);
            }
            meshes.push_back(ms);
            e.mesh = (int8_t)(meshes.size() - 1), e.tex = 255;
        }
        int lt = (int)c["EmitterLifeTime"].f(), pl = (int)c["ParticleLife"].f();
        e.infinite = lt == 65535, e.immortal = pl == 65535;  // 0x5b9f0d, 0x5b73dc
        e.lifeTime = (float)lt, e.lifeTimeR = c["EmitterLifeTimeRandomise"].f(), e.max = (int)c["EmitterMaxParticles"].f();
        e.num = (int)c["EmitterNumSpawn"].f(), e.numR = (int)c["EmitterNumSpawnRadnomise"].f();
        e.off = v3(c["EmitterOriginOffset"]), e.offR = v3(c["EmitterOriginRandomise"]);
        e.freq = c["EmitterSpawnFreq"].f(), e.freqR = c["EmitterSpawnFreqRansomise"].f(), e.delay = c["EmitterStartDelay"].f();
        e.acc = v3(c["ParticleAcceleration"]), e.accR = v3(c["ParticleAccelerationRandomise"]), e.mass = c["ParticleMass"].f();
        e.alpha = c["ParticleAlpha"].f(1), e.alphaIn = c["ParticleAlphaFadeIn"].f();
        float av = c["ParticleAlphaVelocity"].f(), ad = c["ParticleAlphaVelocityDelay"].f();
        if (av < 0) e.fade = ad; else e.alphaV = av, e.aDelay = ad;
        e.altOn = c["ParticleIsAlternateAcceleration"].f() != 0, e.altN = c["ParticleAlternateAccelerationN"].f(), e.altS = c["ParticleAlternateAccelerationS"].f();
        e.wind = c["ParticleIsEffectedByWind"].f() != 0, e.spiralOn = c["ParticleIsSpiral"].f() != 0;
        e.life = (float)pl, e.lifeR = c["ParticleLifeRandomise"].f();
        e.ori = v3(c["ParticleOrientation"]), e.oriR = v3(c["ParticleOrientationRandomise"]);
        e.oriV = v3(c["ParticleOrientationVelocity"]), e.oriVR = v3(c["ParticleOrientationVelocityRandomise"]);
        const Json &sz = c["ParticleSize"];
        e.size = sz[0].f(), e.aspect = sz[0].f() != 0 ? sz[1].f() / sz[0].f() : 1, e.sizeR = c["ParticleSizeRandomise"].f();
        e.sizeV = {c["ParticleSizeVelocity"][0].f(), c["ParticleSizeVelocity"][1].f()};
        e.sizeVR = {c["ParticleSizeVelocityRandomise"][0].f(), c["ParticleSizeVelocityRandomise"][1].f()};
        e.shrink = c["ParticleSizeVelocityDelay"].f(), e.shrinkR = c["ParticleSizeVelocityDelayRandomise"].f();
        Vector2 fin = {c["ParticleFinalSizeScale"][0].f(), c["ParticleFinalSizeScale"][1].f()};
        e.fin = fin.x == 0 && fin.y == 0 ? Vector2{-1, -1} : fin, e.finR = c["ParticleFinalSizeScaleRandomise"].f();
        e.sizeIn = c["ParticleSizeFadeIn"].f(), e.sizeInR = c["ParticleSizeFadeInRandomize"].f();
        e.sizeInDelay = c["ParticleSizeFadeInDelay"].f(), e.sizeInDelayR = c["ParticleSizeFadeInDelayRandomize"].f();
        e.spiral = c["ParticleSpiralRadius"].f(), e.spiralR = c["ParticleSpiralRadiusRandomise"].f();
        e.spiralW = c["ParticleSpiralRadiusVelocity"].f(), e.spiralWR = c["ParticleSpiralRadiusVelocityRandomise"].f(), e.spiralS = c["ParticleSpiralRadiusSizeVelocity"].f();
        e.v = v3(c["ParticleVelocity"]), e.vR = v3(c["ParticleVelocityRandomise"]), e.norm = c["ParticleVelocityIsNormalised"].f() != 0;
        // ParticleColor / ColorBand 0x5b77c0: 0 colours = white; band[i] ends segment i, the last segment runs to 1
        Ramp rp{1, {WHITE}, {0, 1}};
        int n = std::min((int)c["ParticleNumColors"].f(), std::min((int)c["ParticleColor"].size(), 5));
        if (n > 0) {
            rp.n = n;
            for (int k = 0; k < n; k++) {
                const Json &col = c["ParticleColor"][k];
                rp.c[k] = {(unsigned char)(255 * col[0].f()), (unsigned char)(255 * col[1].f()), (unsigned char)(255 * col[2].f()), 255};
            }
            for (int k = 1; k < n - 1; k++) rp.edge[k] = c["ParticleColorBand"][k - 1].f();
            rp.edge[0] = 0, rp.edge[n - 1] = 1;
        }
        e.col = (uint16_t)ramps.size(), ramps.push_back(rp);
        // EmitterSoundFX (FEV event names fold case, fmod_event 0x10005110); the "silent" events have no sound definition
        static const struct { const char *ev; Audio::Sfx id; int mode; } SND[] = {
            {"weapons/fireloop", Audio::Sfx::FireLoop, 1}, {"weapons/steamloop", Audio::Sfx::SteamLoop, 1}, {"weapons/fliesloop", Audio::Sfx::FliesLoop, 1},
            {"weapons/elecarc", Audio::Sfx::ElecArc, 1}, {"weapons/electricarching", Audio::Sfx::ElectricArching, 1},
            {"weapons/hoseintowater", Audio::Sfx::HoseIntoWater, 1}, {"weapons/stormcloud", Audio::Sfx::StormCloud, 0},
            {"weapons/thud", Audio::Sfx::Land, 0}, {"weapons/bubblemachineloop", Audio::Sfx::BubbleLoop, 2},
            {"global/explosionregular", Audio::Sfx::Explosion, 0}, {"weapons/explosionlarge", Audio::Sfx::BigExplosion, 0},
            {"weapons/floodrainloop", Audio::Sfx::FloodRain, 1}};
        for (const auto &x : SND)
            if (lower(c["EmitterSoundFX"].s()) == x.ev) e.sfx = (int)x.id, e.sfxLoop = x.mode == 1, e.sfxHold = x.mode == 2;
        dataFx[lower(name)] = {&e};
        made.push_back({&e, &c});
    }
    // an effect's names are emitters or effects (0x5c0c06 recurses), each an unknown one the placeholder
    std::function<void(const std::string &, std::vector<const Emit *> &, int)> expand = [&](const std::string &n, std::vector<const Emit *> &out, int depth) {
        const Json &eff = j["effects"][n.c_str()];
        if (eff.type != Json::Arr) {
            auto it = dataFx.find(lower(n));
            if (it != dataFx.end()) out.insert(out.end(), it->second.begin(), it->second.end());
            else if (depth < 8 && lower(n) != "xxx_placeholderpp") expand("XXX_PlaceholderPP", out, depth + 1);
            return;
        }
        for (const Json &x : eff.arr) if (depth < 8) expand(x.s(), out, depth + 1);
    };
    for (const auto &[name, list] : j["effects"].obj) {
        std::vector<const Emit *> v;
        expand(name, v, 0);
        dataFx[lower(name)] = v;
    }
    for (auto &[e, c] : made) {
        if (std::string f = (*c)["EmitterParticleFX"].s(); !f.empty()) e->fxL = fxNamed(f);
        if (std::string f = (*c)["EmitterParticleExpireFX"].s(); !f.empty()) e->expireL = fxNamed(f);
    }
}

// ---- kRain / kSnow emitters: SnowParticleEmitterEntity (init 0x487140, update 0x486b70) and RainGraphicEntity splashes (0x482910, 0x482530) ----
struct Drop { Vector3 p; float t; Vector2 S; };  // spawn point (W4M position, not wrapped), s since spawn, half extents m
struct Splash { Vector3 p; float size, alpha, acc; };  // half extent units, alpha byte, ms toward the next 7 ms step
struct Rain { const Emit *e; Vector3 origin; std::vector<Drop> d; std::vector<Splash> sp; bool dying = false, weather = false; float fadeB = 255; int level = -1, handle = 0; };
std::vector<Rain> rains;
constexpr float RU = 0.05f;  // rain is camera-relative: W4M units at 20 per m, not the map scale
Vector3 camPos{}, camLook{0, 0, 1};
std::function<bool(Vector3, float, Vector3 *, Vector3 *)> ground;  // ray straight down from a point, length: land hit and its normal
float waterY = 0;

void rainSpawn(Rain &r, Drop &d) {  // base particle setup at the emitter origin + offset + OriginRandomise, then 0x5b6790
    const Emit &e = *r.e;
    auto q = [](float x) { return rnd(-x, x); };
    float k = rnd(-1, 1);
    d = {Vector3Add(r.origin, Vector3Scale({e.off.x + q(e.offR.x), e.off.y + q(e.offR.y), e.off.z + q(e.offR.z)}, RU)), 0,
         {(e.size + k * e.sizeR) * RU, (e.size * e.aspect + k * e.sizeR) * RU}};
}
// 0x4863e0: wrap into the +-200 unit box around the camera (+0x194..0x19c); a y wrap tells the caller to respawn
bool rainWrap(Vector3 &p) {
    const float h = 200 * RU, w = 2 * h;
    auto wrap = [&](float &v, float c) { bool out = v < c - h || v > c + h; v = c - h + fmodf(fmodf(v - (c - h), w) + w, w); return out; };
    wrap(p.x, camPos.x), wrap(p.z, camPos.z);
    return wrap(p.y, camPos.y);
}
void rainStart(const Emit *e, Vector3 origin, bool weather, int level, int handle) {
    Rain r{e, origin};
    r.weather = weather, r.level = level, r.handle = handle;
    r.d.resize(e->pool());  // the whole pool at once, each wrapped into the box (0x487140)
    for (Drop &d : r.d) rainSpawn(r, d), rainWrap(d.p);
    rains.push_back(std::move(r));
}
void rainUpdate(float dt) {
    for (size_t i = 0; i < rains.size();) {
        Rain &r = rains[i];
        Vector3 v = Vector3Scale(r.e->v, 10 * RU);
        for (Drop &d : r.d) {
            d.t += dt;
            Vector3 p = Vector3Add(d.p, Vector3Scale(v, d.t));
            if (rainWrap(p)) rainSpawn(r, d), rainWrap(d.p);
        }
        if (r.dying) r.fadeB = fmaxf(0, r.fadeB - dt * 600);  // 0.6 per ms (0x487053)
        // splashes, RainGraphicEntity 0x482070: up to 3 a frame below 50, r = rand % 500 units ahead of the view, down from 250 units up
        if (!r.dying && r.e->kind == 2)
            for (int k = 0; k < 3 && r.sp.size() < 50; k++) {
                float spread = fabsf(camLook.y) > 0.5f ? fabsf(camLook.y) * PI : 35 * DEG2RAD;
                float a = atan2f(camLook.x, camLook.z) + rnd(-spread, spread), dist = (float)(GetRandomValue(0, 499)) * RU;
                Vector3 from = {camPos.x + sinf(a) * dist, camPos.y + 250 * RU, camPos.z + cosf(a) * dist}, hit, n;
                Vector3 at = {from.x, waterY, from.z};
                if (ground && ground(from, from.y - waterY, &hit, &n)) {
                    if (!(fabsf(n.y) > fabsf(n.x) && fabsf(n.y) > fabsf(n.z))) continue;
                    at = hit;
                }
                r.sp.push_back({{at.x, at.y + 1.05f * RU, at.z}, 0.1f, 200, 0});
            }
        for (Splash &q : r.sp)  // every 7 ms: size + 0.4, alpha - 16 (0x482350)
            for (q.acc += dt * 1000; q.acc >= 7; q.acc -= 7) q.size += 0.4f, q.alpha -= 16;
        r.sp.erase(std::remove_if(r.sp.begin(), r.sp.end(), [](const Splash &q) { return q.alpha <= 10; }), r.sp.end());
        if (r.dying && r.fadeB <= 0 && r.sp.empty()) rains.erase(rains.begin() + i);
        else i++;
    }
    bool loop = std::any_of(rains.begin(), rains.end(), [](const Rain &r) { return r.e->kind == 2; });
    Audio::loop(Audio::Sfx::Flood, loop);  // RainGraphicEntity's weapons/RainLoop (0x4829cc); kRain never plays its EmitterSoundFX
}
void kill(int h) {  // 0x5bfde0 fading delete: the emitters stop, their particles live on; a rain fades
    if (!h) return;
    lives.erase(std::remove_if(lives.begin(), lives.end(), [&](const Live &l) { return l.handle == h; }), lives.end());
    for (Rain &r : rains) if (r.handle == h) r.dying = true;
}
// FloodLogicEntity: StartRain once the weapon's LaunchDelay is over (0x555cff), StopRain (0x555420) at floodStop
const float FLOOD_START = 1;  // kWeaponFlood LaunchDelay 1000 ms
float floodT = -1, floodStop = 0;
Vector3 floodAt{};
int floodRain = 0;

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
// Sky scene parts: raylib's default shading plus the clip's texture offset (0x6dffd0: uv x Repeat + Offset; sky Repeat 1, Rotate 0)
const char *SKYMESH_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec4 vertexColor;
uniform mat4 mvp;
uniform vec2 uvOff;
varying vec2 uv;
varying vec4 col;
void main() { uv = vertexTexCoord + uvOff; col = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char *SKYMESH_FS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
varying vec2 uv;
varying vec4 col;
void main() { gl_FragColor = texture2D(texture0, uv) * colDiffuse * col; }
)";
// W4M CG/water.cg WaterFragmentMain: colour from the three panned normal maps alone (no eye vector), constant alpha.
// p0 = (ReflectionContrast, ReflectionStrength, SpecularPower), p3 = (SpecularContrast, NearOpacity, SubtractColourScale).
const char *WATER_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
uniform mat4 mvp;
uniform float textureScale;
varying vec2 uv;
void main() { uv = vertexTexCoord * textureScale; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char *WATER_FS = R"(
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform float time;
uniform vec3 p0;
uniform vec3 ni;
uniform vec3 ni1;
uniform vec3 p3;
varying vec2 uv;
vec3 cube(vec3 c) {
    c = normalize(c);
    float m = max(max(c.x, c.y), c.z);
    vec2 f = m == c.x ? c.yz : m == c.y ? c.xz : c.xy;
    return texture2D(texture2, f * 0.5 + 0.5).rgb;
}
vec3 nmap(vec2 t) { return normalize(texture2D(texture1, t).rgb * 2.0 - 1.0); }
void main() {
    float t = time * 0.5;
    vec3 n = normalize(nmap(uv * 5.0 + t * vec2(-0.3, 0.6)) * vec3(0.2, 0.2, 0.0) + nmap(uv * -0.2 + t * vec2(0.0, 0.2))
                       + nmap(uv * -0.75 + t * vec2(-0.1, -0.2)));
    vec3 d = texture2D(texture0, n.gg * 0.25).rgb - texture2D(texture0, n.bb * 2.0).rrr * p3.z;
    vec3 r = pow(cube(n * ni1), vec3(p0.x)) * p0.y;
    float s = clamp(pow(cube(n * ni).r, p0.z) * p3.x, 0.0, 1.0);
    gl_FragColor = vec4(d + r + s, p3.y);
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
    tex[SOAP] = loadTex("hud", "wxp_sprite_011", true);  // Particle.WXSprite11, the soap bubbles
    tex[MIST] = loadTex("hud", "mist_puff", true);  // Particle.MistPuff (Mist_Puff.tga)
    tex[WHITEOUT] = loadTex("fe", "whiteout", false);  // Particle.Whiteout: flat white, additive
    UnloadImage(ring);
    skySh = shader(SKY_VS, SKY_FS);
    waterSh = shader(WATER_VS, WATER_FS);
    static const char *WATER_U[6] = {"time", "textureScale", "p0", "ni", "ni1", "p3"};
    for (int i = 0; i < 6; i++) waterLoc[i] = GetShaderLocation(waterSh, WATER_U[i]);
    skyMeshSh = shader(SKYMESH_VS, SKYMESH_FS);
    uvOffLoc = GetShaderLocation(skyMeshSh, "uvOff");
    flareTex = loadTex("hud", "lensflares", false);  // W4M Lens.Flares (LensFlares.tga)
    if (FileExists(DATA_DIR "assets/models/wxpmesh7.glb")) domeModel = LoadModel(DATA_DIR "assets/models/wxpmesh7.glb");  // raw units, rest scale 1
    for (int i = 0; i < domeModel.materialCount && skyMeshSh.id != rlGetShaderIdDefault(); i++) domeModel.materials[i].shader = skyMeshSh;
    dome = GenMeshSphere(1, 12, 16);
    plane = GenMeshPlane(1, 1, 1, 1);
    skyMat = LoadMaterialDefault(), waterMat = LoadMaterialDefault();
    skyMat.shader = skySh, waterMat.shader = waterSh;
    ps.reserve(MAX);
}

static void skyKey(const std::string &theme, const std::string &time, char &l, char &suffix) {
    static const char *NAMES[] = {"jurassic", "camelot", "arabian", "wildwest", "construction", "arctic", "england", "horror", "lunar", "pirate", "war", "frontend"};
    static const char LETTERS[] = "pcawbrehltof";
    l = 'c';  // procedural island and unknown themes
    for (int i = 0; i < 12; i++)
        if (theme == NAMES[i]) l = LETTERS[i];
    suffix = time == "night" ? '3' : time == "evening" ? '2' : '1';  // W4M ramp naming: 01 day, 02 evening, 03 night
}

std::string gradientFile(const std::string &theme, const std::string &time, bool side) {
    char l, suffix;
    skyKey(theme, time, l, suffix);
    return TextFormat(DATA_DIR "assets/ui/sky/%c_%ssky0%c.png", l, side ? "side" : "", suffix);
}

std::string skyFile(const std::string &theme, const std::string &time) {
    char l, suffix;
    skyKey(theme, time, l, suffix);
    return TextFormat(DATA_DIR "assets/models/sky/%c_sky0%c.glb", l, suffix);
}

void theme(const std::string &theme, Color sky, const std::string &time, bool levelSky) {
    char l, suffix;
    skyKey(theme, time, l, suffix);
    std::string glb = skyFile(theme, time);
    if (skyModel.meshCount) UnloadModel(skyModel), skyModel = {};
    skyParts.clear(), skyClip = skyT = 0, hasSun = false, flareFade = flareOwed = 0;
    if (levelSky && FileExists(glb.c_str())) {
        skyModel = Models::take(glb.c_str());
        if (skyMeshSh.id != rlGetShaderIdDefault())
            for (int i = 0; i < skyModel.materialCount; i++) skyModel.materials[i].shader = skyMeshSh;
        char *b = LoadFileText((glb.substr(0, glb.size() - 4) + ".blend").c_str());
        for (char *p = b; p && *p;) {
            SkyPart k;
            if (sscanf(p, "clip %f", &skyClip) == 1 || sscanf(p, "sun %f %f %f", &sunAt.x, &sunAt.y, &sunAt.z) == 3) hasSun |= *p == 's';
            else sscanf(p, "%d %d %f %f %f %f", &k.src, &k.dst, &k.rot, &k.u, &k.v, &k.t1), skyParts.push_back(k);
            while (*p && *p++ != '\n') {}
        }
        if (b) UnloadFileText(b);
    }
    // TWEAK.XOM LF.<THEME>.<TIME>Sky -> Sky.Flare<n+1>: day 0, night 1, evening 2 (Wild West evening 1)
    flareSet = time == "night" || (time == "evening" && theme == "wildwest") ? 1 : time == "evening" ? 2 : 0;
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
    // WaterPlaneTweaks Diffuse / Normal / EnvironmentTexture: <L>.<TIME>Water, FE.DAYWaterNormal for every theme, <L>.<TIME>WaterEnv
    for (int i = 0; i < 3; i++) waterTex[i] = loadTex("sky", i == 1 ? "f_water01b" : TextFormat("%c_water0%c%c", l, suffix, 'a' + i), true);
    if (waterTex[2].id) SetTextureWrap(waterTex[2], TEXTURE_WRAP_CLAMP);
    skyMat.maps[MATERIAL_MAP_DIFFUSE].texture = skyTex;
    waterMat.maps[MATERIAL_MAP_DIFFUSE].texture = waterTex[0];
    waterMat.maps[MATERIAL_MAP_SPECULAR].texture = waterTex[1];
    waterMat.maps[MATERIAL_MAP_NORMAL].texture = waterTex[2];
}

void unload() {
    for (Texture2D &t : tex) UnloadTexture(t);
    if (skyModel.meshCount) UnloadModel(skyModel), skyModel = {};
    if (skyTex.id) UnloadTexture(skyTex);
    for (Texture2D &t : waterTex)
        if (t.id) UnloadTexture(t);
    UnloadMesh(dome), UnloadMesh(plane);
    UnloadShader(skySh), UnloadShader(waterSh), UnloadShader(skyMeshSh), UnloadTexture(flareTex);
    if (domeModel.meshCount) UnloadModel(domeModel), domeModel = {};
    MemFree(skyMat.maps), MemFree(waterMat.maps);
}

void fireworks(Vector3 centre, float radius, float top) { stageC = centre, stageR = radius, stageTop = top, tickAcc = 0, show = 9; }
namespace {
int rolled = -1;  // last weather roll index (60 s periods)
float rainProb = 0;
Vector3 weatherAt{};
const std::vector<Terrain::Emitter> *levelEm = nullptr;
float levelUnit = 0.05f;
std::map<int, int> scriptFx;  // mission script emitter handle -> effect handle; its particles carry level SCRIPT_LEVEL + effect handle
const int SCRIPT_LEVEL = 1 << 20;
float shakeLen = 0, shakeT = 0, shakeMag = 0;  // the last Camera.ShakeStart
// CameraShakeManager 0x523e60: the summed jitter x 0.02 is clamped to Camera.Shake.Max 0.01, then x 1000 units: 0.5 m; our jolt is 1.58 x shake
const float SHAKE_MAX = 0.5f / 1.58f;
void startLevel() {  // DetailEntity 0x5cd5eb -> 0x5c1410: each live EMITTER_ detail's effect at its position
    for (int i = 0; levelEm && i < (int)levelEm->size(); i++) {
        const Terrain::Emitter &m = (*levelEm)[i];
        if (const std::vector<const Emit *> *fx = m.alive ? fxNamed(m.fx) : nullptr) effect(*fx, m.pos, levelUnit, i);
    }
}
}  // namespace
void clear() {
    domes.clear(), ps.clear(), streaks.clear(), seen.clear(), seenPrev.clear(), lives.clear(), rains.clear(), shake = show = 0, floodT = -1;
    scriptFx.clear(), shakeLen = 0;
    startLevel();  // the map's emitters are part of the level, not of the moment cleared
    rolled = -1;
}

void setWind(Vector3 w) { windNow = w; }


void level(const std::vector<Terrain::Emitter> &em, float unit, Vector3 origin, const std::string &theme, const std::string &time, float water,
           std::function<bool(Vector3, float, Vector3 *, Vector3 *)> down, float rainOverride) {
    loadData();
    ground = std::move(down), waterY = water, weatherAt = origin, rolled = -1;
    // LOCAL.XOM Prob.Rain.<THEME>.<TIME>, read by FlowControlService 0x4e7fcd (absent keys: 0)
    static const struct { const char *theme, *time; float p; } PROB[] = {{"england", "evening", 40}, {"england", "night", 20}, {"horror", "day", 40},
        {"horror", "evening", 30}, {"horror", "night", 20}, {"war", "day", 50}, {"war", "night", 40}};
    rainProb = 0;
    for (const auto &r : PROB) if (theme == r.theme && time == r.time) rainProb = r.p;
    if (rainOverride >= 0) rainProb = fminf(rainOverride, 100);
    levelEm = &em, levelUnit = unit;
    clear();
}

void levelSync(const std::vector<Terrain::Emitter> &em) {
    auto gone = [&](int l) { return l >= 0 && l < (int)em.size() && !em[l].alive; };
    lives.erase(std::remove_if(lives.begin(), lives.end(), [&](const Live &l) { return gone(l.level); }), lives.end());
    ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const Particle &p) { return p.immortal && gone(p.level); }), ps.end());
    for (Rain &r : rains) if (gone(r.level)) r.dying = true;
}

// ParticleHandlerService 0x5c1270, at GameLoadComplete then every Particle.Weather.RefreshFreq 60000 ms of logic time: r = rand % 100 starts
// WXP_RainFallBG below Particle.Rain.Prob, deletes it above (W4M draws from the logic RNG; ours from the match seed, render only)
void weather(uint32_t seed, int ticks) {
    int k = ticks / 3600;
    if (k == rolled || rainProb <= 0) return;
    rolled = k;
    uint32_t x = seed ^ (0x9e3779b9u * (uint32_t)(k + 1));
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    int r = (int)(x % 100);
    auto it = std::find_if(rains.begin(), rains.end(), [](const Rain &q) { return q.weather && !q.dying; });
    const std::vector<const Emit *> *fx = fxNamed("WXP_RainFallBG");
    if (it == rains.end() && r < rainProb && fx && !fx->empty() && ((*fx)[0]->kind == 2 || (*fx)[0]->kind == 1)) rainStart((*fx)[0], weatherAt, true, -1);
    else if (it != rains.end() && r > rainProb) it->dying = true;
}

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
}  // namespace

// W4M PARTTWK (20 units per metre; tall sprites as glow columns): WXP_Poof_VLarge, WXP_Abductee_Teleport, WXP_AbdDamageInd
static void abductee(const GameEvent &e) {
    auto glow = [&](Vector3 p, Vector3 v, float delay, float life, float s0, float s1, Color c, unsigned char t = GLOW) { add({p, v, -delay, life, s0, s1, 0, rnd(-1, 1), 0, 2, c, t, true}); };
    auto column = [&](float y0, float h, float w, float life, Color c) { for (float y = y0; y < y0 + h; y += w) glow(Vector3Add(e.pos, {0, y, 0}), {}, 0, life, w, w * 0.5f, c); };
    if (e.kind == GameEvent::Poof)  // 20 white to grey puffs, 0.7 +- 0.3 s
        for (int i = 0; i < 20; i++) {
            Vector3 d = rndDir();
            add({Vector3Add(e.pos, {0, 0.15f, 0}), {d.x * 3, rnd(0, 1), d.z * 3}, 0, rnd(0.4f, 1), 0.3f, 0.05f, 0, rnd(-8, 8), -0.5f, 3, {(unsigned char)rnd(80, 255), 0, 0, 230}, PUFF, false});
            ps.back().c.g = ps.back().c.b = ps.back().c.r;
        }
    if (e.kind == GameEvent::Zap) {  // AbdTelep_Central (3 x 45 units), Stars / Stars2 (WXSprite26), Glow (3 x 90 units, alpha 0.1), GloTall (40 x 250 units)
        for (int i = 0; i < 3; i++) glow(e.pos, {}, 0, rnd(0.6f, 1.8f), rnd(1.75f, 2.75f), 0, {153, 255, 153, 255});
        for (int i = 0; i < 13; i++) glow(e.pos, Vector3Scale(rndDir(), rnd(1, 4)), i < 8 ? 0.02f : 0, rnd(0.5f, 1.1f), rnd(0.08f, 0.15f), 0.02f, {128, 255, 128, 255}, SPARK);
        for (int i = 0; i < 3; i++) glow(Vector3Add(e.pos, Vector3Scale(rndDir(), 0.25f)), {}, 0, 0.8f, 4.5f, 0, {51, 255, 0, 26});
        column(0, 12.5f, 2, 0.35f, {102, 255, 102, 77});
    }
    if (e.kind == GameEvent::AbdDamage) {  // Abd_DamageRand: 2 beams 10 x 600 units centred 300 units up, randomised 500; AbdDamageSputt: 30 +- 60 unit flickers for 0.5 s
        for (int i = 0; i < 2; i++) column(rnd(-12.5f, 12.5f), 30, 0.6f, 0.6f, {26, 255, 26, 77});
        for (int i = 0; i < 5; i++) glow(e.pos, {}, i * 0.1f, 0.1f, rnd(1.5f, 4.5f), 0, {51, 230, 51, 255});
    }
}

// W4M WXP_BubbleMachineExpire at the machine (0x54e6a0). Poof / ExpBubb: 10 thrown ~8 units out (AlternateAcceleration
// N 6500, S 2e-6: a drag); Plume / BubbPlum: 7 and 14 rising 0.25 m/s, accelerating up (Mass < 0), 2 +- 0.6 s.
static void bubblePop(Vector3 p) {
    for (int i = 0; i < 20; i++) {
        bool soap = i >= 10;
        Vector3 d = soap ? rndDir() : Vector3Normalize({rnd(-1, 1), 0.15f, rnd(-1, 1)});
        Color c = soap ? WHITE : ColorLerp({102, 102, 102, 255}, {255, 153, 128, 255}, rnd());
        add({p, Vector3Scale(d, soap ? 6.3f : 5.2f), 0, rnd(1.0f, 1.4f), 0.15f, 0, 0, 0, 0, 13, c, (unsigned char)(soap ? SOAP : PUFF), false});
    }
    for (int i = 0; i < 21; i++) {
        bool soap = i >= 7;
        Vector3 o = {rnd(-0.075f, 0.075f), 0.05f + rnd(-0.05f, 0.05f), rnd(-0.075f, 0.075f)};
        Color c = soap ? WHITE : ColorLerp({230, 204, 153, 255}, {255, 102, 153, 255}, rnd());
        add({Vector3Add(p, o), {0, 0.25f, 0}, soap ? -0.2f : 0, rnd(1.4f, 2.6f), rnd(0.05f, 0.15f), 0, 0, 0, -0.5f, 0, c, (unsigned char)(soap ? SOAP : PUFF), false});
    }
}

// W4M WXP_Bubbles_Small (machine node "bubble", one per 320 +- 100 ms): 4 +- 2 units, 1.8 +- 0.3 s, rising 0.25 m/s, up 0.625 m/s2
void soap(Vector3 p) {
    add({Vector3Add(p, {rnd(-0.3f, 0.3f), rnd(0, 0.1f), rnd(-0.3f, 0.3f)}), {rnd(-0.25f, 0.25f), 0.25f, rnd(-0.25f, 0.25f)}, 0, rnd(1.5f, 2.1f), rnd(0.1f, 0.3f), 0.15f, 0, 0,
         -0.625f, 0, WHITE, SOAP, false});
}

// W4M PARTTWK, one burst each (EmitterLifeTime 1 ms): n sprites, life 2 +- 0.3 s, 1.75 m shrinking to 0, spin +- 8 deg x 10/s, IsAlternateAcceleration N / S;
// ParticleVelocityIsNormalised: unit(V + (2r-1) Vrand) x (V.x + Vrand.x) x 0.01 units/ms = x 0.5 m/s
static void alt(Vector3 p, Vector3 dir, float speed, float life, float size, float spin, float rot, RampId r, float n, Color c = WHITE) {
    Particle q = {p, Vector3Scale(Vector3Normalize(dir), speed * 0.5f), 0, life, size / 20, 0, rot, spin, 0, 0, c, PUFF, false};
    q.ramp = r, q.altN = n, q.altS = 2e-6f, q.p0 = p;
    add(q);
}

// WXP_Wep_Donkey = WXP_DonkeyStrikeBounce (mesh WXPMesh7) + WXP_DonkeySpritePuffLG (40, V (0, 2, 0) +- (1.3, 4, 1.3)) + WXP_DonkeySpritePuffHoriz (20, V (0, .2, 0) +- (2.2, 0, 2.2)), 20 units above the point
static void donkeyDust(Vector3 pos) {
    Vector3 p = Vector3Add(pos, {0, 1, 0});
    for (int i = 0; i < 40; i++) alt(p, {rnd(-1, 1) * 1.3f, 2 + rnd(-1, 1) * 4, rnd(-1, 1) * 1.3f}, 1.3f, 2 + rnd(-0.3f, 0.3f), 35, rnd(-8, 8) * DEG2RAD * 10, 0, R_DONKEY_PUFF, 6500);
    for (int i = 0; i < 20; i++) alt(p, {rnd(-1, 1) * 2.2f, 0.2f, rnd(-1, 1) * 2.2f}, 2.2f, 2 + rnd(-0.3f, 0.3f), 35, rnd(-8, 8) * DEG2RAD * 10, 0, R_DONKEY_PUFF, 6500);
}

// W4M WXP_ShotgunBlast = WXP_ShotgunBlastHit (10 puffs, 3 units, 1.2 +- 0.2 s, speed 0.2) + WXP_ShotgunBlastHitTrails (10, 1 unit, 0.4 +- 0.1 s, speed 0.3);
// V (0, .02, 0) +- (.2 | .3, .05 | .1, .2 | .3), grey-lilac (.95, .9, 1) to (.5, .45, .55), spin +- 8 deg x 10/s, drag N 6500
static void gunBlast(Vector3 p) {
    for (int i = 0; i < 20; i++) {
        float k = i < 10 ? 1 : 1.5f;
        Vector3 d = {rnd(-1, 1) * 0.2f * k, 0.02f + rnd(-1, 1) * 0.05f * k, rnd(-1, 1) * 0.2f * k};
        float life = (i < 10 ? 1.2f : 0.4f) + rnd(-0.2f, 0.2f) * (i < 10 ? 1 : 0.5f), spin = rnd(-8, 8) * DEG2RAD * 10;
        alt(p, d, 0.2f * k, life, i < 10 ? 3 : 1, spin, 0, R_NONE, 6500, ColorLerp({242, 230, 255, 255}, {128, 115, 140, 255}, rnd()));
    }
}

void donkeyAriel(Vector3 at) {  // WXP_CrateSpawnLGRings: 30 sprites, 22 units, life 1.2 +- 0.5 s, V +- (1.5, 0, 1.5), random orientation, spin +- 15 deg x 10/s
    for (int i = 0; i < 30; i++) alt(at, {rnd(-1, 1) * 1.5f, 0, rnd(-1, 1) * 1.5f}, 1.5f, 1.2f + rnd(-0.5f, 0.5f), 22, rnd(-15, 15) * DEG2RAD * 10, rnd(0, 360) * DEG2RAD, R_DONKEY_RING, 6000);
}

void event(const GameEvent &e, Color dirt) {
    abductee(e);
    if (e.kind == GameEvent::BubblePop) bubblePop(e.pos);
    bool big = e.kind == GameEvent::BigBoom;
    bool donkey = (e.kind == GameEvent::Boom || big) && e.weapon >= 0 && WEAPONS[e.weapon].kind == Kind::Donkey && WEAPONS[e.weapon].clusters == 0;
    if (donkey && domeModel.meshCount) domes.push_back({Vector3Add(e.pos, {0, 0.5f, 0}), 1.2f, 1.3f, 0});  // WXP_DonkeyStrikeBounce: +10 units, (1.2, 1.3)
    if ((e.kind == GameEvent::Boom || big) && e.fx) {
        shake = fmaxf(shake, big ? 0.6f : 0.18f);  // its ExplosionMessage shakes the camera like any blast
        if (const std::vector<const Emit *> *fx = fxNamed(e.fx)) effect(*fx, e.pos);
    } else if (donkey) {
        shake = fmaxf(shake, 0.6f);  // the Explode ExplosionMessage shakes the camera as any blast; its only effect is DetonationFx
        donkeyDust(e.pos);
    } else if (e.kind == GameEvent::Boom && e.weapon >= 0 && WEAPONS[e.weapon].kind == Kind::Shotgun) {
        shake = fmaxf(shake, 0.18f);  // the gun's ExplosionMessage shakes the camera like any blast; its only visual is WormCollisionFX / LandCollisionFX
        gunBlast(e.pos);
    } else if (e.kind == GameEvent::Boom || big) {
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
        if (is(s, "Starburst")) effect(STARBURST, e.pos);
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
    }
}

void trail(const Projectile &s, float dt, Vector3 wind) {
    const WeaponDef &d = WEAPONS[s.weapon];
    float age = 0, best = 9;  // emitter clock: carried from the same shot of the previous frame
    for (const Seen &q : seenPrev)
        if (q.weapon == s.weapon && q.child == s.child && Vector3DistanceSqr(q.p, s.pos) < best) best = Vector3DistanceSqr(q.p, s.pos), age = q.age + dt;
    seen.push_back({s.pos, s.weapon, s.child, age});
    if (d.name == "Holy Hand Grenade" && rnd() < dt * 16)  // WXP_HolyHG_Trails: crosses left floating behind
        add({s.pos, {rnd(-0.2f, 0.2f), rnd(0.1f, 0.4f), rnd(-0.2f, 0.2f)}, 0, rnd(1.8f, 2.2f), 0.35f, 0.05f, 0, rnd(-1, 1), -0.1f, 1, {255, 240, 200, 230}, CROSS, false});
    if (d.name == "Starburst") {  // rocket: WXP_Wep_StarburstRocket flames + orange glow; stars: blue trail + cyan glow
        float v = Vector3Length(s.vel);
        if (!s.child && d.fuse - s.fuse < 2) return;  // the rocket's effect starts on Starburst.FuseLit, 2000 ms in (0x588cba)
        add({s.pos, {}, 0, 0.06f, s.child ? 0.9f : 0.75f, 0.6f, 0, 0, 0, 0, s.child ? Color{120, 230, 255, 255} : Color{255, 77, 0, 255}, GLOW, true});
        if (s.child && v > 0.5f) streaks.push_back({s.pos, Vector3Scale(s.vel, -1 / v), fminf(v * 0.15f, 2.5f), 0.3f, TRAIL_B});
        int alive = 0;
        for (const Particle &q : ps) alive += q.ramp == R_STARBURST;
        if (dt > 0)  // WXP_StarBurstRocketFlames: SpawnFreq 1 ms < frame, so one batch of NumSpawn 2 per update; pool MaxParticles 200
            for (int i = 0; i < 2 && alive++ < 200; i++) {
                float sz = rnd(2.5f, 5.5f) / 20;
                Particle q = {s.pos, {rnd(-0.2f, 0.2f), rnd(-0.2f, 0.2f), rnd(-0.2f, 0.2f)}, 0, rnd(0.3f, 0.7f), sz, 0, 0, rnd(-1, 1), 0, 0, WHITE, PUFF, false};
                q.ramp = R_STARBURST;
                add(q);
            }
        return;
    }
    if (d.kind == Kind::Airstrike && s.child && d.fuse <= 0) return;  // WEAPTWK: no TrailBitmap; its ArielFx is a one-off (ariel())
    // ArielFx emitters: Bazooka WXP_BazookaTrailPack (Main + Puffs), Homing Missile WXP_HomingMissileTrail (Smoke + Glow), Factory bazooka WeaponBazooka (MistPuff)
    bool homing = d.kind == Kind::Homing;
    if (!homing && !(d.kind == Kind::Shell && d.fuse <= 0 && d.name != "Poison Arrow")) return;
    if (homing && customWeapon(s.weapon)) return;  // Factory homing WeaponHomingTail: sprite set Particle.Additive4 exists in no bundle
    // emitters sit at the payload model's FxLocator (WEAPTWK: bazookarocket / homingmissile), posed as drawShot poses the model
    Vector3 fx{};
    Models::fxLocator(homing ? "homing" : "bazooka", &fx);
    float hv = sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z);
    Vector3 tail = Vector3Add(s.pos, Vector3Transform(fx, MatrixMultiply(MatrixRotateX(-atan2f(s.vel.y, hv)), MatrixRotateY(atan2f(s.vel.x, s.vel.z)))));
    // batches due this frame: one per SpawnFreq (a 20 ms logic tick at most, 0x5bab20) while age < EmitterLifeTime
    auto batches = [&](float freq, float life) {
        auto n = [&](float t) { return t < 0 ? 0 : (int)(fminf(t, life) / freq) + (t < life); };
        return n(age) - n(age - dt);
    };
    auto count = [&](RampId r) { int n = 0; for (const Particle &q : ps) n += q.ramp == r; return n; };
    // orientation: ParticleOrientation +- Randomise, spin: OrientationVelocityRandomise, both degrees (0x5b6c90 / 0x5b6d50), the spin x 0.01 per ms
    auto emit = [&](RampId r, int cap, int num, float life0, float lifeR, float size, float sizeR, float vel, float velR, float fadeIn, float delay, bool additive, Tex t,
                    float rot = 30, float rotR = 20, float spinR = 30) {
        int alive = count(r);
        for (int i = 0; i < num && alive++ < cap; i++) {
            float sz = (size + rnd(-sizeR, sizeR)) / 20, life = life0 + rnd(-lifeR, lifeR);
            Vector3 v = {(vel + rnd(-velR, velR)) * 0.5f, (vel + rnd(-velR, velR)) * 0.5f, (vel + rnd(-velR, velR)) * 0.5f};  // V x 0.01 units/ms
            Particle q = {tail, v, 0, life, sz, 0, (rot + rnd(-rotR, rotR)) * DEG2RAD, rnd(-spinR, spinR) * DEG2RAD * 10, 0, 0, WHITE, (unsigned char)t, additive};
            q.ramp = r, q.fadeIn = fadeIn, q.delay = delay;
            add(q);
        }
    };
    if (dt <= 0) return;
    if (homing) {
        for (int i = batches(0.02f, 20); i > 0; i--) emit(R_HOM_GLOW, 10, 1, 0.08f, 0, 12, 5, 0, 0, 0, 0, true, GLOW, 0, 360, 0);
        for (int i = batches(0.02f, 50); i > 0; i--) emit(R_HOM_SMOKE, 50, 2, 0.4f, 0.03f, 2.5f, 1.5f, 0.2f, 0.5f, 0.04f, 0, false, PUFF);
        return;
    }
    if (customWeapon(s.weapon)) {  // WeaponBazookaPuff: one MistPuff per update (SpawnFreq 0), 200 ms, rising with Mass 2 x (0.2 + wind)
        for (int i = batches(0.02f, 65.535f), alive = count(R_MIST); i > 0 && alive++ < 20; i--) {
            Particle q = {tail, {}, 0, 0.2f, 7.0f / 20, 0, rnd(-2 * PI, 2 * PI), rnd(-30, 30) * DEG2RAD * 10, -2.0f, 0, WHITE, MIST, false};
            q.ramp = R_MIST, q.delay = 0.3f, q.alpha = 0.5f, q.fadeA = 0;
            q.acc = {wind.x * 8.5f, 0, wind.z * 8.5f};  // Wind.MaxSpeed 8.5e-5 units/ms² = 4.25 m/s², x Mass 2
            add(q);
        }
        return;
    }
    for (int i = batches(0.02f, 12); i > 0; i--) emit(R_BAZ_MAIN, 150, 1, 2, 0.2f, 4, 1.5f, 0.2f, 0.2f, 0.1f, 0.04f, false, PUFF);
    for (int i = batches(0.12f, 2.5f); i > 0; i--) emit(R_BAZ_PUFF, 100, 4, 1, 0.2f, 9.5f, 1.5f, 0.2f, 0.2f, 0.1f, 0.3f, false, PUFF);
}

void puff(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, bool fire) {
    add({p, v, 0, life, size0, size1, 0, rnd(-1, 1), fire ? -1.0f : -0.3f, 0.6f, c, (unsigned char)(fire ? FIRE : PUFF), fire});
}

// WXP_PlaneWingTrails. W4M scales ParticleVelocity and SizeVelocity by 0.01 per ms (0x5b97d0): drift 0..0.2 m/s,
// 4 +-0.75 units shrinking 0.5 m/s, so gone in ~0.4 s, well before ParticleLife 2 s.
void wingTrail(Vector3 p) {
    float s = rnd(0.1625f, 0.2375f);
    add({p, {rnd(0, 0.2f), rnd(-0.1f, 0.1f), rnd(0, 0.2f)}, 0, 0.04f + s / 0.5f, s, 0, 0, rnd(-1, 1), 0, 0, {200, 205, 190, 255}, PUFF, false});
}

// WXP_AirstrikeArielA: EmitterLifeTime 1 ms, so one burst of its MaxParticles 10 (0x5bbfa0); 0.08 m/s, shrinking 0.5 m/s.
void ariel(Vector3 p) {
    for (int i = 0; i < 10; i++)
        add({p, Vector3Scale(rndDir(), 0.08f), 0, 0.4f, 0.2f, 0, 0, rnd(-8, 8) * 0.06f, 0, 0, ColorLerp({51, 51, 77, 220}, {128, 115, 140, 220}, rnd()), PUFF, false});
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

void jetStart(Vector3 at) { effect({&JET_RING, &JET_BASE}, at); }
void start(const char *name, Vector3 at) {
    if (const std::vector<const Emit *> *fx = fxNamed(name)) effect(*fx, at);
}
void scripted(int handle, const char *name, Vector3 at) {
    const std::vector<const Emit *> *fx = name ? fxNamed(name) : nullptr;
    if (!fx) return (void)TraceLog(LOG_WARNING, "fx: no effect '%s'", name ? name : "");
    scriptedOff(handle, false);
    scriptFx[handle] = effect(*fx, at, levelUnit, SCRIPT_LEVEL + lastHandle + 1);  // effect() takes lastHandle + 1
}
void scriptedOff(int handle, bool now) {
    auto it = scriptFx.find(handle);
    if (it == scriptFx.end()) return;
    int level = SCRIPT_LEVEL + it->second;
    kill(it->second), scriptFx.erase(it);
    ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const Particle &p) { return p.level == level && (now || p.immortal); }), ps.end());
}
void shakeFor(float mag, float secs) { shakeMag = mag, shakeLen = secs, shakeT = 0; }
void flood(Vector3 at, float stop) { floodAt = at, floodStop = stop, floodT = 0; }
void jetStop() { lives.erase(std::remove_if(lives.begin(), lives.end(), [](const Live &l) { return l.e == &JET_BASE; }), lives.end()); }

void flame(Vector3 p, Vector3 v, float life, float size0, float size1, bool jet) {
    add({Vector3Add(p, Vector3Scale(rndDir(), size0 * 0.25f)), Vector3Add(v, Vector3Scale(rndDir(), 0.5f)), 0, life, size0, size1, 0, rnd(-2, 2), 0, 2, jet ? Color{255, 200, 140, 255} : Color{255, 110, 30, 255}, (unsigned char)(jet ? JET : TOON), true});
}

// W4M AlienAbductionGraphicEntity (PARTTWK; g = s since the warp gate opened, < 0 closed; 20 units per metre, all cyan (0.5, 0.9, 1) WXSprite1 glows): WXP_AlienWarpGate at the gate, WXP_BeamStartParts
// at the saucer's nozzle from 4.975 s, WXP_AlienBeamup at the beam from 7.791 s, WXP_Alien_ABD_Grnd_Effect (0.4, 0.7, 0.9) under it while it lifts.
void ufo(Vector3 at, Vector3 nozzle, Vector3 gate, Vector3 ground, float e, float g, int stage, float dt) {
    static float acc[9];
    auto every = [&](int k, float period, bool on) {
        int n = 0;
        if (!on) return acc[k] = 0, 0;
        for (acc[k] += dt; acc[k] >= period; acc[k] -= period) n++;
        return n;
    };
    const Color C = {128, 230, 255, 255}, G = {102, 179, 230, 255};
    auto glow = [&](Vector3 p, Vector3 v, float life, float size, Color c, unsigned char t = GLOW) { add({p, v, 0, life, size, size, 0, rnd(-1, 1), 0, 0, c, t, true}); };
    auto near = [&](Vector3 c, float rx, float ry, float rz) { return Vector3Add(c, {rnd(-rx, rx), rnd(-ry, ry), rnd(-rz, rz)}); };
    if (g >= 0 && g < 0.05f) glow(gate, {}, 4, 15, C), glow(gate, {}, 3, 12, C);  // WXP_WarpGateGlow_A (300 units); the 2 s of rays follow
    for (int n = every(0, 0.04f, g >= 0 && g < 2); n > 0; n--) glow(gate, Vector3Scale(rndDir(), 2.25f), 1.6f, 4.5f, C);  // Warpgate_Rayouts: 25 over 2 s
    for (int n = every(1, 0.4f, g >= 0 && g < 2); n > 0; n--) glow(gate, {}, 3.5f, 9, {C.r, C.g, C.b, 140});  // Raystars: tall rays (40 x 600 units), approximated
    if (g >= 3.7f && g < 3.7f + dt + 0.001f) glow(gate, {}, 0.6f, 40, {C.r, C.g, C.b, 150});  // WarpGateEndFlash: 1300 units shrinking
    bool lead = e >= 4.975f && e < 8.475f;
    for (int n = every(2, 0.05f, lead); n > 0; n--) glow(nozzle, {}, 0.15f, rnd(1.5f, 2.25f), C);  // BeamStartGlo
    for (int n = every(3, 0.2f, lead); n > 0; n--) glow(nozzle, {}, 0.8f, 2.5f, C, STAR);  // BeamStartStars (3 -> 50 units)
    for (int n = every(4, 0.35f, lead); n > 0; n--) glow(nozzle, {}, 4, rnd(1.75f, 2.25f), {C.r, C.g, C.b, 120});  // BeamBuildup
    bool beam = e >= 7.791f && stage < Game::ABD_HOLDING;  // AbductCloseBeam stops it (0x5461e0)
    for (int n = every(5, 1 / 60.f, beam); n > 0; n--) glow(at, {}, 0.05f, 1.5f, C), glow(at, {}, 0.05f, rnd(2.5f, 4), C);  // RootGlow, Mainglow
    for (int n = every(6, 0.5f, beam); n > 0; n--) glow(near(Vector3Add(at, {0, -3.75f, 0}), 0, 2.5f, 0), {}, 2, rnd(2.5f, 4), {C.r, C.g, C.b, 128});  // CentralBeam
    for (int n = every(7, 0.06f, beam); n > 0; n--) glow(near(Vector3Add(at, {0, -7.5f, 0}), 0.5f, 2.5f, 0.5f), {0, 0.75f, 0}, 1, 0.3f, C, SPARK);  // RisingStars
    for (int n = every(8, 0.25f, stage == Game::ABD_LIFTING); n > 0; n--) {  // Grnd_Effect: glows, thin glows and rising specks
        glow(near(Vector3Add(ground, {0, 0.25f, 0}), 1.5f, 0.2f, 1.5f), {}, 2, 1.5f, {G.r, G.g, G.b, 64});
        glow(near(Vector3Add(ground, {0, 1, 0}), 0.5f, 2, 0.5f), {}, 2, 2, {G.r, G.g, G.b, 64});
        glow(near(Vector3Add(ground, {0, 0.6f, 0}), 1.5f, 0.2f, 1.5f), {0, 0.5f, 0}, 2, 0.1f, G, SPARK);
    }
}

void update(float dt) {
    skyT += dt;
    for (Dome &d : domes) d.age += dt;
    domes.erase(std::remove_if(domes.begin(), domes.end(), [](const Dome &d) { return d.age >= DOME_LIFE; }), domes.end());
    shake *= expf(-dt * 6);
    if (shakeT < shakeLen) shakeT += dt, shake = fmaxf(shake, fminf(shakeMag * fmaxf(0, 1 - shakeT / shakeLen), SHAKE_MAX));
    // W4M 0x4ffa56, every 20 ms of the 5 s show: rand() % 40 == 0 fires WXPF_Firework<1 + rand() % 5> at Land.Center +- Radius / 2 in x and z,
    // Land.MaxHeight + rand x 30 units (its 100 ms gap test computes last - now, unsigned, so it never holds)
    for (tickAcc += show > 0 && show < 5 ? dt : 0; tickAcc >= 0.02f; tickAcc -= 0.02f)
        if (rnd() * 40 < 1) effect(FIREWORKS[(int)(rnd() * 5)], {stageC.x + (rnd() - 0.5f) * stageR, stageTop + rnd() * 1.5f, stageC.z + (rnd() - 0.5f) * stageR});
    show -= dt;
    if (floodT >= 0) {  // 0x555720: WXP_RainFall (kept to stop it) and WXP_StormCloud (left to its own life)
        float was = floodT;
        floodT += dt;
        if (was < FLOOD_START && floodT >= FLOOD_START) {
            if (auto *f = fxNamed("WXP_RainFall")) floodRain = effect(*f, floodAt);
            start("WXP_StormCloud", floodAt);
        }
        if (floodT >= floodStop) kill(floodRain), floodT = -1;
    }
    tickEmitters(dt);
    rainUpdate(dt);
    for (size_t i = 0; i < ps.size();) {
        Particle &p = ps[i];
        p.age += dt;
        if (p.age < 0) { i++; continue; }
        if (p.age >= p.life && !p.immortal) {
            if (p.expire) lives.push_back({p.expire, p.p});
            if (p.expireL) effect(*p.expireL, p.p, p.unit);
            p = ps.back(), ps.pop_back();
            continue;
        }
        if (p.altN > 0 || p.ramp) p.p = posAt(p, p.age);
        else {
            p.v = Vector3Scale(Vector3Add(p.v, Vector3Scale(p.acc, dt)), expf(-p.drag * dt));
            p.v.y -= p.grav * dt;
            p.p = Vector3Add(p.p, Vector3Scale(p.v, dt));
        }
        p.rot += p.spin * dt;
        if (p.tail.a && rnd() < dt * 60)  // add() never reallocates (reserved MAX), so p stays valid
            add({p.p, {rnd(-0.3f, 0.3f), rnd(0, 0.3f), rnd(-0.3f, 0.3f)}, 0, rnd(0.25f, 0.41f), 0.4f, 0.05f, 0, rnd(-3, 3), -0.5f, 1, p.tail, PUFF, true});
        i++;
    }
}

static Vector3 skyOrigin;
static float skyUnit = 0.05f;
// Skybox1-3 camera (0x4d927b): the view with its x / z translation zeroed puts the scene origin on the view's up axis, this many units below the eye
static float skyDrop(const Camera3D &cam, Vector3 up) { return Vector3DotProduct(Vector3Subtract(cam.position, skyOrigin), up) / skyUnit; }

void drawSky(const Camera3D &cam, Vector3 origin, float unit) {
    skyOrigin = origin, skyUnit = unit;
    rlDrawRenderBatchActive();
    if (skyModel.meshCount) {
        // the ~20000-unit scene shrunk inside the far plane around the camera: opaque parts, then the blended ones in scene
        // order with their XBlendModeGL; depth only sorts the sky's own parts and is cleared after
        static const int GL_FACTOR[11] = {0, 1, 0x306, 0x307, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x308};  // W4M BlendFactor -> GL, table 0x8b4b5c
        Matrix v = GetCameraMatrix(cam);
        Vector3 up = {v.m1, v.m5, v.m9}, at = Vector3Add(cam.position, Vector3Scale(up, -skyDrop(cam, up) * SKY_K));
        Matrix m = MatrixMultiply(MatrixScale(SKY_K, SKY_K, SKY_K), MatrixTranslate(at.x, at.y, at.z));
        float t = skyClip > 0 ? fmodf(skyT, skyClip) : 0;
        rlDisableBackfaceCulling();
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < skyModel.meshCount; i++) {
                SkyPart k = i < (int)skyParts.size() ? skyParts[i] : SkyPart{};
                auto [src, dst] = std::pair<int, int>{k.src, k.dst};
                if ((src >= 0) != (pass == 1)) continue;
                float f = k.t1 > 0 ? fminf(t, k.t1) / k.t1 : 0;  // 2-key linear channels, held to the clip end
                Vector2 off = {k.u * f, k.v * f};
                if (skyMeshSh.id != rlGetShaderIdDefault()) SetShaderValue(skyMeshSh, uvOffLoc, &off, SHADER_UNIFORM_VEC2);
                if (pass) {
                    if (src < 11 && dst < 11) rlSetBlendFactors(GL_FACTOR[src], GL_FACTOR[dst], 0x8006);  // else glBlendFunc fails in W4M: state kept
                    rlSetBlendMode(RL_BLEND_CUSTOM), rlDisableDepthMask();
                }
                DrawMesh(skyModel.meshes[i], skyModel.materials[skyModel.meshMaterial[i]], MatrixMultiply(MatrixRotateY(k.rot * f), m));
            }
        rlSetBlendMode(RL_BLEND_ALPHA), rlEnableDepthMask(), rlEnableBackfaceCulling();
        glClear(0x100);  // GL_DEPTH_BUFFER_BIT
        return;
    }
    if (!skyTex.id || skySh.id == rlGetShaderIdDefault()) return;
    rlDisableDepthTest(), rlDisableDepthMask(), rlDisableBackfaceCulling();
    DrawMesh(dome, skyMat, MatrixMultiply(MatrixScale(100, 100, 100), MatrixTranslate(cam.position.x, cam.position.y, cam.position.z)));
    rlEnableDepthTest(), rlEnableDepthMask(), rlEnableBackfaceCulling();
}

void drawWater(const Camera3D &cam, float level, float time, float HALF, Vector2 centre) {
    // W4M WaterCgGraphicEntity: a 0..1 uv quad scaled to +-12000 units (0x48cada), alpha blended, depth written, not culled
    if (!waterTex[0].id || !waterTex[1].id || waterSh.id == rlGetShaderIdDefault()) {
        DrawPlane({centre.x, level, centre.y}, {2 * HALF, 2 * HALF}, {30, 80, 160, 180});
        return;
    }
    const float *w = Lit::sun.water;
    Vector3 p0 = {w[4], w[3], w[6]}, ni = {w[7], w[8], w[9]}, ni1 = {w[10], w[11], w[12]}, p3 = {w[5], w[0], w[13]};
    time = fmodf(time, 200);  // every pan speed loops at 200 s: keeps uv offsets small
    SetShaderValue(waterSh, waterLoc[0], &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(waterSh, waterLoc[1], &w[1], SHADER_UNIFORM_FLOAT);
    SetShaderValue(waterSh, waterLoc[2], &p0, SHADER_UNIFORM_VEC3);
    SetShaderValue(waterSh, waterLoc[3], &ni, SHADER_UNIFORM_VEC3);
    SetShaderValue(waterSh, waterLoc[4], &ni1, SHADER_UNIFORM_VEC3);
    SetShaderValue(waterSh, waterLoc[5], &p3, SHADER_UNIFORM_VEC3);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableBackfaceCulling();
    DrawMesh(plane, waterMat, MatrixMultiply(MatrixScale(2 * HALF, 1, 2 * HALF), MatrixTranslate(centre.x, level, centre.y)));
    rlEnableBackfaceCulling();
    EndBlendMode();
}

void draw(const Camera3D &cam) {
    camPos = cam.position, camLook = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    if (!domes.empty()) {  // unlit, SrcAlpha / OneMinusSrcAlpha (lambert2 XBlendModeGL 6 7), no z write, two-sided [assumed]
        static const float SCALE[3][6] = {{0.1344f, 0.9907f, 0.1344f, 0.9907f, 0, 0}, {0.8091f, 0.5869f, 0.8091f, 0.5869f, 0.16663f, 0.85498f},
                                          {0.9995f, 0.0215f, 0.9995f, 0.0215f, 0.83301f, 1}};  // WXM_DefSource scale keys
        Vector2 off = {0, 0};
        SetShaderValue(skyMeshSh, uvOffLoc, &off, SHADER_UNIFORM_VEC2);
        rlDrawRenderBatchActive();
        rlDisableDepthMask(), rlDisableBackfaceCulling();
        BeginBlendMode(BLEND_ALPHA);
        for (const Dome &d : domes) {
            float k = Models::curve(SCALE, 3, d.age) / 20;
            Matrix m = MatrixMultiply(MatrixScale(d.sxz * k, d.sy * k, d.sxz * k), MatrixTranslate(d.p.x, d.p.y, d.p.z));
            for (int i = 0; i < domeModel.meshCount; i++) DrawMesh(domeModel.meshes[i], domeModel.materials[domeModel.meshMaterial[i]], m);
        }
        EndBlendMode();
        rlEnableDepthMask(), rlEnableBackfaceCulling();
    }
    // MeshSet particles (0x5bd665): the mesh at the particle, scale (Size.x, Size.y, Size.x), ParticleOrientation (+ velocity) in XYZ order;
    // no alpha reaches a mesh, so AlphaVelocity < 0 shrinks it to 0 at end of life instead (0x5bd69e)
    for (const Particle &p : ps) {
        if (p.mesh < 0 || p.age < 0 || p.mesh >= (int)meshes.size()) continue;
        const MeshSet &m = meshes[p.mesh];
        Vector2 sz = sizeAt(p, p.age, p.fadeA >= 0);
        Vector3 o = Vector3Add(p.ori, Vector3Scale(p.oriV, p.age));
        Matrix r = MatrixMultiply(MatrixMultiply(MatrixRotateX(o.x), MatrixRotateY(o.y)), MatrixRotateZ(o.z));
        Matrix w = MatrixMultiply(MatrixMultiply(MatrixScale(sz.x, sz.y, sz.x), r), MatrixTranslate(p.p.x, p.p.y, p.p.z));
        float f = m.t1 > 0 ? fmodf(p.age, m.t1) / m.t1 : 0;
        Vector2 off = {m.u * f, m.v * f};
        SetShaderValue(skyMeshSh, uvOffLoc, &off, SHADER_UNIFORM_VEC2);
        static const int GL_FACTOR[11] = {0, 1, 0x306, 0x307, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x308};
        rlDrawRenderBatchActive();
        if (m.src >= 0 && m.src < 11 && m.dst >= 0 && m.dst < 11) rlSetBlendFactors(GL_FACTOR[m.src], GL_FACTOR[m.dst], 0x8006), rlSetBlendMode(RL_BLEND_CUSTOM), rlDisableDepthMask();
        rlDisableBackfaceCulling();
        Models::shade(skyMeshSh), Models::draw(m.model.c_str(), w, WHITE, m.clip.empty() ? nullptr : m.clip.c_str(), p.age), Models::shade({});
        rlDrawRenderBatchActive();
        rlSetBlendMode(RL_BLEND_ALPHA), rlEnableDepthMask(), rlEnableBackfaceCulling();
    }
    Matrix v = GetCameraMatrix(cam);
    Vector3 right = {v.m0, v.m4, v.m8}, up = {v.m1, v.m5, v.m9}, fwd = {-v.m2, -v.m6, -v.m10};
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    // alpha pass first (smoke behind fire reads better), one texture per batch
    for (int add : {0, 1}) {
        BeginBlendMode(add ? BLEND_ADDITIVE : BLEND_ALPHA);
        for (int t = 0; t < (int)tex.size(); t++) {
            if (!tex[t].id) continue;
            bool any = false;
            for (const Particle &p : ps) {
                bool body = p.tex == t && p.add == (bool)add && p.mesh < 0, head = p.head && t == GLOW && add;
                if ((!body && !head) || p.age < 0) continue;
                if (!any) rlSetTexture(tex[t].id), rlBegin(RL_QUADS), any = true;
                float k = p.immortal ? 0 : p.age / p.life;
                Vector2 sz = p.ramp ? sizeAt(p, p.age) : Vector2{Lerp(p.size0, p.size1, k) * 0.5f, Lerp(p.size0, p.size1, k) * 0.5f * p.aspect};
                float c = cosf(p.rot), sn = sinf(p.rot);
                Vector3 r = Vector3Scale(Vector3Add(Vector3Scale(right, c), Vector3Scale(up, sn)), sz.x), u = Vector3Scale(Vector3Subtract(Vector3Scale(up, c), Vector3Scale(right, sn)), sz.y);
                Color col = p.c;
                if (p.ramp) {
                    const Ramp &R = ramps[p.ramp];
                    int i = 0;
                    while (i < R.n - 2 && k > R.edge[i + 1]) i++;
                    if (R.n > 1) col = ColorLerp(R.c[i], R.c[i + 1], (k - R.edge[i]) / (R.edge[i + 1] - R.edge[i]));
                    else col = R.c[0];
                    col.a = (unsigned char)(255 * alphaAt(p, p.age));
                } else col.a = (unsigned char)(col.a * (1 - k) * fminf(1, k * 12 + 0.3f));
                if (head) quad(p.p, r, u, col);
                if (!body) continue;
                if (p.stretch > 0) {
                    // TrailGraphicEntity 0x5c28f0 / 0x5c2ae0: a point per 20 ms update, edges +- n x Size.x (n across the view), u = 0 at the particle,
                    // +n edge v = 1; the first point has no width; no colour set: the texture alone, additive (0x5c2d30)
                    rlColor4ub(255, 255, 255, 255);
                    Vector3 a = p.p;
                    float sa = sizeAt(p, p.age).x;
                    for (int j = 1; j <= TRAIL_SEGS && p.age > (j - 1) * TRAIL_DT; j++) {
                        float tb = p.age - j * TRAIL_DT, sb = tb <= 0 ? 0 : sizeAt(p, tb).x, u0 = (j - 1) / (float)TRAIL_SEGS, u1 = j / (float)TRAIL_SEGS;
                        Vector3 b = posAt(p, fmaxf(tb, 0)), n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(a, cam.position), Vector3Subtract(a, b)));
                        Vector3 wa = Vector3Scale(n, sa), wb = Vector3Scale(n, sb);
                        rlTexCoord2f(u0, 1); rlVertex3f(a.x + wa.x, a.y + wa.y, a.z + wa.z);
                        rlTexCoord2f(u0, 0); rlVertex3f(a.x - wa.x, a.y - wa.y, a.z - wa.z);
                        rlTexCoord2f(u1, 0); rlVertex3f(b.x - wb.x, b.y - wb.y, b.z - wb.z);
                        rlTexCoord2f(u1, 1); rlVertex3f(b.x + wb.x, b.y + wb.y, b.z + wb.z);
                        a = b, sa = sb;
                    }
                } else quad(p.p, r, u, col);
            }
            // kRain drops: vertical streaks facing the view (right = (look.z, 0, -look.x), 0x486900), vertex alpha 128 or the delete fade
            for (const Rain &rn : rains) {
                if (rn.e->tex != t || rn.e->add != (bool)add) continue;
                if (!any) rlSetTexture(tex[t].id), rlBegin(RL_QUADS), any = true;
                Vector3 rs = Vector3Normalize({camLook.z, 0, -camLook.x}), us = {0, 1, 0};
                Vector3 v = Vector3Scale(rn.e->v, 10 * RU);
                Color col = {255, 255, 255, (unsigned char)(rn.dying ? rn.fadeB : 128)};
                for (const Drop &d : rn.d) {
                    Vector3 q = Vector3Add(d.p, Vector3Scale(v, d.t));
                    rainWrap(q);
                    quad(q, Vector3Scale(rs, d.S.x), Vector3Scale(us, d.S.y), col);
                }
            }
            if (t == rainSplashTex && rainSplashAdd == (bool)add)  // splashes: turn about the vertical only
                for (const Rain &rn : rains)
                    for (const Splash &q : rn.sp) {
                        if (!any) rlSetTexture(tex[t].id), rlBegin(RL_QUADS), any = true;
                        Vector3 rs = Vector3Normalize({camLook.z, 0, -camLook.x});
                        quad(q.p, Vector3Scale(rs, q.size * RU), {0, q.size * RU, 0}, {255, 255, 255, (unsigned char)fmaxf(0, q.alpha)});
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
    seenPrev.swap(seen), seen.clear();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}
// W4M LensFlareGraphicEntity (0x479770): additive billboards on the line from the sun through the screen centre
void drawFlare(const Camera3D &cam, float dt, const std::function<int(Vector3, Vector3)> &hit) {
    struct El { float scale, size, fadeIn; unsigned char r, g, b; int type; };
    // LVLSETUP Sky.Flare1-3 (LensScale 2, FocalOffset 1); type 0 SunGlow, 1 Circle, 2 FadedRing, 4 FadedHex, 5 Hex
    static const El F1[] = {{0, 4000, 1, 255, 255, 255, 0}, {.8f, 600, 1, 128, 0, 128, 1}, {.2f, 100, 1, 64, 64, 255, 2}, {.3f, 200, .6f, 128, 64, 128, 1},
                            {.7f, 300, .5f, 64, 64, 128, 4}, {.75f, 100, .8f, 42, 32, 128, 2}, {.15f, 200, .9f, 64, 32, 32, 5}, {.4f, 100, 1, 63, 64, 128, 0},
                            {0, 500, 2, 255, 255, 255, 1}};
    static const El F2[] = {{0, 7000, .2f, 150, 140, 82, 0}};
    static const El F3[] = {{.1f, 4000, 1, 100, 100, 100, 0}, {.8f, 800, .5f, 40, 40, 40, 1}, {.3f, 200, .6f, 128, 64, 128, 1}, {.6f, 200, .5f, 40, 40, 40, 4},
                            {.4f, 100, .5f, 40, 40, 40, 1}, {0, 200, .5f, 60, 60, 60, 0}};
    static const struct { const El *e; int n; } SETS[3] = {{F1, 9}, {F2, 1}, {F3, 6}};
    // atlas 0x81cea8 per type: u0, v0, w, h, v counted from the image bottom
    static const float ATLAS[7][4] = {{0, .5f, .5f, .5f}, {0, 0, .5f, .5f}, {.5f, .25f, .25f, .25f}, {.5f, .5f, .5f, .5f}, {.75f, .25f, .25f, .25f}, {.5f, 0, .25f, .25f}, {.75f, 0, .25f, .25f}};
    const float LENS = 2, FOCAL = 1;
    if (!hasSun || !flareTex.id) return;
    Matrix v = GetCameraMatrix(cam);
    Vector3 right = {v.m0, v.m4, v.m8}, up = {v.m1, v.m5, v.m9}, fwd = {-v.m2, -v.m6, -v.m10};
    float x = Vector3DotProduct(sunAt, right), y = Vector3DotProduct(sunAt, up) - skyDrop(cam, up), z = Vector3DotProduct(sunAt, fwd);
    float r = sqrtf(x * x + y * y) / fabsf(z);  // affine transforms only (0x455d30): no projection scale
    if (r >= 1) { flareFade = 1; return; }  // as the exe: full again when it comes back on screen
    int h = hit(cam.position, Vector3Normalize(Vector3Subtract(sunAt, Vector3Scale(Vector3Subtract(cam.position, skyOrigin), 1 / skyUnit))));  // 0x4799e7: sun - eye
    if (h == 2) flareOwed += dt;  // an object: the flare freezes and its clock stops
    else flareFade = Clamp(flareFade + (h ? -5.0f : 5.0f) * (dt + flareOwed), 0, 1), flareOwed = 0;  // 0.005 per ms
    if (flareFade <= 0) return;
    rlDrawRenderBatchActive();
    rlDisableDepthTest(), rlDisableDepthMask(), rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(flareTex.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i < SETS[flareSet].n; i++) {
        const El &e = SETS[flareSet].e[i];
        float k = e.type == 0 ? 1 - r / 2 : LENS, a = (e.fadeIn > 1 ? 1 : (1 - r) * e.fadeIn) * flareFade, hs = e.size * k * 0.5f * SKY_K;
        Vector3 p = Vector3Add(cam.position, Vector3Scale(Vector3Add(Vector3Add(Vector3Scale(right, x * (1 - 2 * e.scale)), Vector3Scale(up, y * (1 - 2 * e.scale))), Vector3Scale(fwd, FOCAL * z)), SKY_K));
        Vector3 rr = Vector3Scale(right, hs), uu = Vector3Scale(up, hs);
        const float *t = ATLAS[e.type];
        float u0 = t[0], u1 = t[0] + t[2], vt = 1 - t[1] - t[3], vb = 1 - t[1];
        rlColor4ub((unsigned char)(e.r * a), (unsigned char)(e.g * a), (unsigned char)(e.b * a), 255);
        rlTexCoord2f(u0, vt); rlVertex3f(p.x - rr.x + uu.x, p.y - rr.y + uu.y, p.z - rr.z + uu.z);
        rlTexCoord2f(u0, vb); rlVertex3f(p.x - rr.x - uu.x, p.y - rr.y - uu.y, p.z - rr.z - uu.z);
        rlTexCoord2f(u1, vb); rlVertex3f(p.x + rr.x - uu.x, p.y + rr.y - uu.y, p.z + rr.z - uu.z);
        rlTexCoord2f(u1, vt); rlVertex3f(p.x + rr.x + uu.x, p.y + rr.y + uu.y, p.z + rr.z + uu.z);
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
    rlEnableDepthTest(), rlEnableDepthMask(), rlEnableBackfaceCulling();
}

}  // namespace Fx

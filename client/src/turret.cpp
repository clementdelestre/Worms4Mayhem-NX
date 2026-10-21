#include "turret.h"
#include "audio.h"
#include "fx.h"
#include "models.h"
#include "raymath.h"
#include <algorithm>
#include <map>

namespace {
constexpr float U = 1 / 20.f;  // m per W4M unit
constexpr int TRACERS = 30, DIVS = 6;  // TrailGraphicEntity(TrailSprite_W, 6 divisions, 30 trails) (0x56b5c6)
const char *const AIM_LR = "AimSentrygunLR", *const AIM_UD = "AimSentrygunUD", *const SHOOT = "Shoot";

float rand01() { return GetRandomValue(0, 65535) / 65536.f; }  // W4M 0x68c07b: the graphical LCG (0x96d040), never the logic one, 16 bits / 65536
float wrap2pi(float a) { while (a >= 2 * PI) a -= 2 * PI; while (a < 0) a += 2 * PI; return a; }
float wrapPi(float a) { return a > PI ? a - 2 * PI : a < -PI ? a + 2 * PI : a; }
// 0x47a1a0: (target + k cur) / (k + 1), at most step
float ease(float cur, float target, float k, float step) {
    float v = (target + k * cur) / (k + 1);
    return v - cur > step ? cur + step : cur - v > step ? cur - step : v;
}
// 0x569f20 with k = 2: (k cur + dt target) / (dt + k), at most dt x rate
float turn(float cur, float target, float rate, float dt) {
    float v = (2 * cur + dt * target) / (dt + 2), step = dt * rate;
    return v - cur > step ? cur + step : cur - v > step ? cur - step : v;
}

struct Tracer { Vector3 dir; float end; int age; };
struct Graphic {
    bool init = false;
    Vector3 hand{}, base{}, up{0, 1, 0}, upTo{0, 1, 0}, fwd{};
    float lerp = 0, place = 0, yaw = PI, pitch = PI / 2, spin = 0, slope = 0, slopeK = 0;  // +0xc4, +0xc0, +0xe8, +0xe4, +0xb4, +0xb8, +0xbc
    int mode = 0;  // +0x150: 0 none, 1 scan, 2 rest, 3 fire
    bool fast = false, aimed = false, firing = false;  // +0xe0; the Aim clips set (a mode ran); the trail and FireSound (+0x164, +0x160)
    float idle = 0, shootT = 0, acc = 0, fxAge = -1;  // +0x154 frames; +0x15c s; 20 ms accumulator; WXP_Wep_Sentry_Fire's age, s
    int fx = -1, smoke[2] = {-1, -1};  // WXP_Wep_Sentry_Fire, the damage smoke at Smoke1 / Smoke2 (+0xec, +0xf0); -2: started at the next draw
    float health = 0;  // +0x14c, from command 7 (WeaponHealth)
    bool live = false;  // its sentry was seen this frame
    struct Shot { const char *clip; float t; } shots[4] = {};  // one-shot clips (0x6a03d0), one instance each
    Tracer tr[TRACERS] = {};
    int trN = 0, trI = 0;
    int8_t state = Object::Turret::Done;
    bool moving = false, wake = false;
    int timer = 0;
};
std::map<int, Graphic> gfx;  // by Object::id
Vector3 lastHand{};

Vector3 meshPos(const Graphic &s) { return Vector3Lerp(s.hand, s.base, s.lerp); }
// 0x56a120: X = up x fwd, Y = up, Z = X x up
void frame(const Graphic &s, Vector3 *x, Vector3 *y, Vector3 *z) {
    *y = s.up, *x = Vector3Normalize(Vector3CrossProduct(s.up, s.fwd)), *z = Vector3Normalize(Vector3CrossProduct(*x, s.up));
}
void play(Graphic &s, const char *clip) {  // a clip playing restarts; ponytail: past 4 at once the oldest stops
    Graphic::Shot *slot = &s.shots[0];
    for (auto &k : s.shots) if (k.clip == clip || (slot->clip != clip && (!k.clip || (slot->clip && k.t > slot->t)))) slot = &k;
    *slot = {clip, 0};
}
// 0x56b180: the Shoot clip off, the trail and FireSound gone (the sound's end plays)
void stopFire(Graphic &s, Vector3 at) {
    s.shootT = 0;
    if (!s.firing) return;
    s.firing = false, s.trN = 0;
    Audio::play(Audio::Sfx::SentryEnd, at);
}
// 0x56b0b0: the damage smoke starts as the health crosses 50, then 25
void damage(Graphic &s, float dmg) {
    float was = s.health;
    s.health -= dmg;
    if (was > 50 && s.health <= 50) s.smoke[0] = -2;
    if (was > 25 && s.health <= 25) s.smoke[1] = -2;
}
// the command queue 0x56b7b0; v: its float (4: the damage)
void command(Graphic &s, int c, float v = 0) {
    stopFire(s, s.base);
    switch (c) {
    case 1: play(s, "DeploySentrygun"), s.fast = false; break;
    case 3: s.mode = 1, s.idle = rand01() * 300, s.fast = false; break;
    case 4: play(s, "Hit"), s.mode = 2, s.spin = v * 40 * DEG2RAD * 0.02f, damage(s, v), s.fast = false; break;
    case 5: s.spin = 0, s.mode = 2, s.fast = false; break;
    case 6: play(s, "Hit"); break;
    case 8: s.fast = true, Fx::start("WXP_Wep_Sentry_Warmup", Vector3Add(s.base, s.up)); break;  // its EmitterSoundFX: SentryGunActivation
    case 9:
        s.mode = 3, s.firing = true, s.trN = s.trI = 0, s.shootT = 0;
        s.fx = -2, s.fxAge = 0;  // WXP_Wep_Sentry_Fire at the "effect" node, started at the next draw
        break;
    case 10: play(s, "ShootEnd"); break;
    case 11: play(s, "ShootStart"); break;
    }
}
// 0x56ab60: the up vector eases to the ground normal, (target + 3 up) / 4, 0.2 at most
void settle(Graphic &s) {
    Vector3 v = Vector3Scale(Vector3Add(s.upTo, Vector3Scale(s.up, 3)), 0.25f), d = Vector3Subtract(v, s.up);
    float l = Vector3Length(d);
    s.up = Vector3Normalize(l <= 0.2f ? v : Vector3Add(s.up, Vector3Scale(d, 0.2f / l)));
    if (s.up.x == 0 && s.up.z == 0) s.slope = s.slopeK = 0;
    else s.slope = wrap2pi(atan2f(s.up.x, s.up.z) + PI / 2 + s.place), s.slopeK = (1 - s.up.y) * 0.05f;
}
// 0x56abe0: yaw / pitch toward the aim point, 15 units under it in the turret's frame
void aim(Graphic &s, Vector3 look, float dt) {
    Vector3 x, y, z, d = Vector3Subtract(look, meshPos(s));
    frame(s, &x, &y, &z);
    Vector3 l = Vector3Normalize({Vector3DotProduct(d, x), Vector3DotProduct(d, y) - 15 * U, Vector3DotProduct(d, z)});
    float pitch = l.y >= 1 ? PI / 2 : l.y <= -1 ? -PI / 2 : asinf(l.y) + PI / 2, rate = s.fast ? 0.28f : 0.08f;
    float yaw = atan2f(l.x, l.z) + 2 * PI;
    if (yaw > 2 * PI) yaw -= 2 * PI;
    s.yaw = wrap2pi(turn(s.yaw, s.yaw + wrapPi(yaw - s.yaw), rate, dt));
    s.pitch = turn(s.pitch, pitch, rate, dt);
}
// 0x56a940: at rest the head swings downhill and the barrel droops to 45 degrees
void rest(Graphic &s, float dt) {
    s.spin -= dt * sinf(wrapPi(s.slope - s.yaw)) * s.slopeK;
    s.spin *= 0.98f - 0.05f / (fabsf(s.spin) * 10 + 1);
    s.yaw = wrap2pi(s.yaw + s.spin * dt);
    float k = 1 - 1 / ((fabsf(s.spin) - 0.05f <= 0 ? 0 : dt) * 15 + 1);
    s.pitch = ease(s.pitch, k * k * PI / 4 + PI / 4, 1, 0.08f);
}
// 0x56a430: a new tracer each update in a 0.4 r^2 cone about the aim, 50 units further per update until the land
void tracers(Graphic &s, const Game &g, Vector3 look, float dt) {
    Vector3 o = Vector3Add(meshPos(s), Vector3Scale(s.up, 20 * U)), v = Vector3Normalize(Vector3Subtract(look, o));
    Vector3 a = Vector3Normalize(Vector3CrossProduct(v, fabsf(v.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0})), b = Vector3CrossProduct(v, a);
    float ang = rand01() * 2 * PI, r = rand01(), m = r * r * 0.4f;
    Vector3 dir = Vector3Normalize(Vector3Add(v, Vector3Add(Vector3Scale(a, cosf(ang) * m), Vector3Scale(b, sinf(ang) * m)))), hit;
    s.tr[s.trI] = {dir, g.terrain.raycast({o, dir}, 3000 * U, &hit) ? Vector3Distance(o, hit) : 3000 * U, 0};
    s.trI = (s.trI + 1) % TRACERS, s.trN = std::min(s.trN + 1, TRACERS);
    for (int i = 0; i < s.trN; i++) s.tr[i].age++;
    s.shootT += dt * 0.02f;
    for (float len = Models::clipLength("sentry", SHOOT); len > 0 && s.shootT > len;) s.shootT -= len;
}
// one 20 ms update (0x56bb40), dt in 20 ms frames
void step(Graphic &s, const Game &g, const Object &o, float dt) {
    s.lerp = ease(s.lerp, 1, 1, 0.1f);
    switch (s.mode) {
    case 1:
        if (dt < s.idle) s.idle -= dt;
        else s.idle = rand01() * 300, play(s, "Idle");
        settle(s), aim(s, o.turret.look, dt), s.aimed = true;
        break;
    case 2: rest(s, dt), settle(s), s.aimed = true; break;
    case 3: tracers(s, g, o.turret.look, dt), settle(s), aim(s, o.turret.look, dt), s.aimed = true; break;
    }
    for (auto &k : s.shots) if (k.clip && (k.t += dt * 0.02f) > Models::clipLength("sentry", k.clip)) k.clip = nullptr;
    if (s.fxAge >= 0 && (s.fxAge += dt * 0.02f) > 2) s.fxAge = -1, s.fx = -1;  // the emitter's 2000 ms life
}
// the sim's turret moves as the logic entity's commands (0x56cbd0)
void follow(Graphic &s, const Game &g, const Object &o) {
    const Object::Turret &t = o.turret;
    bool moving = Vector3LengthSqr(o.vel) >= 0.01f;  // Game::stepSentry's falling test
    s.base = sentryBase(o);
    if (moving) { s.moving = true, s.wake = false; return; }
    if (s.moving) {  // landed (0x56d7f0): the land normal, the resumed state's commands, then the deploy clip
        s.moving = false, s.upTo = sentryUp(g.terrain, o);
        if (t.state == Object::Turret::Scan) command(s, 3), s.state = t.state, s.wake = false;
        command(s, 1);
    }
    if (s.wake || (t.state == Object::Turret::Scan && s.state != Object::Turret::Scan)) {  // Turn.Started 0x56dc30
        if (s.state == Object::Turret::Done) command(s, 6);
        command(s, 3);
    } else if (t.state == Object::Turret::Track && s.state == Object::Turret::Scan) command(s, 6), command(s, 8);  // 0x56d9e0
    else if (t.state == Object::Turret::Fire && s.state != Object::Turret::Fire) command(s, 9);  // 0x56d680
    else if (t.state == Object::Turret::Done && (s.state == Object::Turret::Track || s.state == Object::Turret::Fire)) command(s, 10), command(s, 5);
    if (t.state == Object::Turret::Track && s.state == Object::Turret::Track && s.timer * 1000 / 60 > 260 && t.timer * 1000 / 60 <= 260) command(s, 11);  // 250..269 ms left
    s.wake = false, s.state = t.state, s.timer = t.timer;
}
Graphic &graphic(const Object &o) {
    Graphic &s = gfx[o.id];
    if (!s.init) {  // 0x56d150 -> 0x56b2d0: at the WeaponLocator, up (0, 1, 0), +z = (cos p, 0, sin p), p = -(worm angle + pi/2)
        s.init = true, s.hand = lastHand, s.base = sentryBase(o), s.state = o.turret.state, s.moving = Vector3LengthSqr(o.vel) >= 0.01f;
        s.health = WEAPONS[o.weapon].health;  // command 7 (0x56d0ef)
        s.place = -(atan2f(o.turret.face.x, o.turret.face.z) + PI / 2), s.fwd = {cosf(s.place), 0, sinf(s.place)};
        command(s, 5);  // 0x56d279: queued at placement, the aim clips at yaw pi turn the barrel away from the worm
    }
    return s;
}
Matrix world(const Graphic &s) {
    Vector3 x, y, z, p = meshPos(s);
    frame(s, &x, &y, &z);
    Matrix r = {x.x, y.x, z.x, p.x, x.y, y.y, z.y, p.y, x.z, y.z, z.z, p.z, 0, 0, 0, 1};
    return MatrixMultiply(MatrixRotateX((1 - s.lerp) * PI * 0.4f), r);  // tilted in the hand, level on the ground (Euler x, 0x56bc1f)
}
}  // namespace

namespace Turret {
void clear() { gfx.clear(); }

void event(const Game &g, const GameEvent &e) {
    if (e.kind == GameEvent::TurnStart) for (auto &[id, s] : gfx) s.wake = true;
    if (e.kind != GameEvent::SentryHit) return;
    for (const Object &o : g.objects)  // 0x56d4bc: command 4 alone, whatever state the hit left
        if (o.type == Object::Sentry && o.id == e.count) {
            Graphic &s = graphic(o);
            command(s, 4, e.amount), s.state = o.turret.state, s.timer = o.turret.timer;
        }
}

void update(const Game &g, float dt, Vector3 hand) {
    lastHand = hand;
    const Vector3 *firing = nullptr;
    for (auto &[id, s] : gfx) s.live = false;
    for (const Object &o : g.objects) {
        if (o.type != Object::Sentry) continue;
        Graphic &s = graphic(o);
        s.live = true;
        follow(s, g, o);
        for (s.acc += dt; s.acc >= 0.02f; s.acc -= 0.02f) step(s, g, o, 1);
        if (s.firing) {
            firing = &s.base;
            Vector3 o0 = Vector3Add(meshPos(s), Vector3Scale(s.up, 20 * U));
            for (int k = 0; k < s.trN; k++) {  // the ribbon's 6 points: the last pushed ones, held at the last short of the land
                const Tracer &t = s.tr[k];
                int head = std::min(t.age - 1, (int)ceilf(t.end / (50 * U)) - 1), tail = std::max(0, t.age - DIVS);
                if (tail < head) Fx::streak(Vector3Add(o0, Vector3Scale(t.dir, head * 50 * U)), Vector3Add(o0, Vector3Scale(t.dir, tail * 50 * U)), 1 * U, tail ? 1 * U : 0);
            }
        }
    }
    for (auto it = gfx.begin(); it != gfx.end();) {  // its sentry blew up: 0x56b020 fades the smoke out
        if (it->second.live) { ++it; continue; }
        for (int h : it->second.smoke) if (h >= 0) Fx::stop(h);
        it = gfx.erase(it);
    }
    Audio::loop(Audio::Sfx::SentryFire, firing, firing);  // ponytail: one FireSound for every firing turret, at the first one
}

bool draw(const Game &, const Object &o) {
    Graphic &s = graphic(o);
    Models::Layers ly;
    int n = 0;
    if (s.aimed) ly.add[n] = AIM_LR, ly.addT[n++] = s.yaw / PI;
    if (s.firing) ly.add[n] = SHOOT, ly.addT[n++] = s.shootT;
    for (const auto &k : s.shots) if (k.clip) ly.add[n] = k.clip, ly.addT[n++] = k.t;
    const Matrix m = world(s);
    auto attach = [&](int &h, const char *node, const char *fx) {  // an effect on a mesh node (0x5c1470)
        Matrix j;
        if (!Models::joint("sentry", node, "Base", 0, true, &j, s.aimed ? AIM_UD : nullptr, s.pitch / (PI / 2), &ly)) return;
        Vector3 at = Vector3Transform({0, 0, 0}, MatrixMultiply(j, m));
        if (h == -2) h = Fx::start(fx, at);
        else Fx::place(h, at);
    };
    if (s.fx != -1 && s.fxAge >= 0) attach(s.fx, "barrel|effect", "WXP_Wep_Sentry_Fire");
    if (s.smoke[0] != -1) attach(s.smoke[0], "gun|Smoke1", "WXP_Smoke_ThinBase_Light");
    if (s.smoke[1] != -1) attach(s.smoke[1], "gun|Smoke2", "WXP_Smoke_ThinBase_Light");
    return Models::draw("sentry", m, "Base", 0, true, s.aimed ? AIM_UD : nullptr, s.pitch / (PI / 2), ly);
}
}  // namespace Turret

#include "ai.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

static constexpr float DT = Game::DT, R = Game::R;
static constexpr float FALL_SAFE = 15, FALL_SCALE = 2;  // copy of sim.cpp's W4M fall damage

static int8_t q(float v) { return (int8_t)Clamp(roundf(v * 127), -127, 127); }

// W4M AITWK.XOM AIParams.CPU1..CPU5 (docs/w4m/ai.md §18); distances at 20 W4M units per game unit.
struct Level {
    float shotErr, directErr, strikeErr, exchange, secondary, threat, nearby, randomise, humans, fireDelay, nonFirstMove, collect, lowHp,
        moveFar, moveTime, memory;
    bool jump;
    float cluster, gas, homing;
    bool flip;             // MovementJumpBackflipAllowed
    float jumpErr, sweet;  // MovementJumpError; ProjectileSweetSpotDistance in m
};
static const Level LEVELS[5] = {
    {0.3f, 0.05f, 1, 0.05f, 0, 0, 1.5f, 0.2f, 0.8f, 0.5f, 2, 1000, 25, 5, 40, 0.2f, false, 1, 1, 0.9f, false, 0, 0},
    {0.2f, 0.02f, 0.5f, 0.1f, 0.5f, 1, 1, 0.2f, 1, 0.5f, 0, 5000, 25, 10, 30, 1, true, 1, 1, 0.9f, false, 0.2f, 0.25f},
    {0.1f, 0.01f, 0.25f, 0.1f, 0.5f, 1, 0.5f, 0.1f, 1, 0.5f, 0, 20000, 25, 5, 30, 1, true, 1, 1, 0.9f, false, 0.1f, 0.25f},
    {0.05f, 0.005f, 0.25f, 0.5f, 0.8f, 2, 0.1f, 0.2f, 1.2f, 0.5f, 0, 30000, 25, 5, 20, 1.5f, true, 0.8f, 0.5f, 0.8f, true, 0.05f, 0.5f},
    {0, 0, 0, 0.5f, 1, 4, 0, 0, 1.8f, 0.2f, 0, 60000, 50, 5, 20, 1, true, 0.3f, 0.5f, 0.7f, true, 0, 0.25f},
};

static const Level &levelOf(const Game &g, int team) {  // a human team played by the AI is CPU5 (W4M /ALLAIPLAYERS)
    int l = team < (int)g.cfg.teamSetup.size() ? g.cfg.teamSetup[team].cpu : 0;
    return LEVELS[l ? std::min(l, 5) - 1 : 4];
}

static int owned(const Game &g, int team, Kind k) {
    for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == k && g.usable(team, (int)i)) return (int)i;
    return -1;
}

static uint64_t touches(const Game &g, Vector3 p) {  // Game::stepShots' contact bits
    uint64_t m = 0;
    for (size_t k = 0; k < g.worms.size() && k < 63; k++) if (g.worms[k].alive && Vector3Distance(p, g.worms[k].pos) < R + 0.3f) m |= 1ull << k;
    return m;
}

static bool outside(const Game &g, Vector3 p) {
    const float W = Terrain::NX * Terrain::VOX;
    return p.y < g.water - 2 || p.x < -20 || p.z < -20 || p.x > W + 20 || p.z > W + 20;
}

// Copy of Game::stepWorm without hp; false once drowned.
struct Body { Vector3 pos, vel; bool grounded; float fall = 0; Motion motion{}; };
static bool stepBody(const Game &g, Body &b, float &yaw) {
    float speed = wormBody(g.terrain, b.pos, b.vel, b.grounded, b.motion, yaw, g.gravity(), g.cfg.wormpot);
    if (speed > FALL_SAFE && g.cfg.scheme.fallDamage) b.fall += (int)((speed - FALL_SAFE) * FALL_SCALE) + 1;
    return b.pos.y >= g.water;
}

// W4M plan scoring: 0x49f190 for a blast, 0x49ed30 for each worm it hurts. Every worm counts, self included.
struct Outcome {
    const Game &g;
    const Level &L;
    const std::vector<float> &rating;  // W4M target threat rating (+0x1c)
    int team, self;
    float s = 0;
    bool started = false;

    Outcome(const Game &g, const Level &L, int me, const std::vector<float> &rating) : g(g), L(L), rating(rating), team(g.worms[me].team), self(me) {}

    // W4M 0x4a9260 target value: allies (self included) are -v·K, enemies v/K, with K = (enemies / allies)^WormExchange
    float value(int i) const {
        const Worm &w = g.worms[i];
        int left = 0, foes = 0, friends = 0;
        for (const Worm &x : g.worms) if (x.alive) left += x.team == w.team, (x.team == team ? friends : foes)++;
        float K = powf((float)std::max(foes, 1) / std::max(friends, 1), L.exchange);
        float v = (1 + 0.04f * w.hp + fmaxf(0.1f, 1 - 0.08f * w.poison)) * (left == 1 ? 2 : 1);
        if (w.team == team) return -v * K;
        bool human = w.team >= (int)g.cfg.teamSetup.size() || !g.cfg.teamSetup[w.team].cpu;
        return v / K * powf(10 / fmaxf(Vector3Distance(w.pos, g.worms[self].pos), 10), L.nearby) * (human ? L.humans : 1);
    }
    // W4M 0x49ed30: d hp on worm i; armour unless melee; a lethal hit is max(hp, KillTarget 200), else the knock weight `a`
    // pulls d toward hp by the threat rating t and scales WeightingAttack 2 by 1 + a·t
    void hurt(int i, float d, float a, bool melee = false) {
        const Worm &w = g.worms[i];
        if (!w.alive) return;
        if (w.armour && !melee) d = (float)((int)d * Game::ARMOUR / 100);
        float attack = 2;
        if (w.hp <= d) d = fmaxf((float)w.hp, 200);
        else if (a != 0) {
            const float t = rating[i];
            attack *= 1 + t * a;
            d = fminf((float)w.hp, d + a * t * (w.hp - d));
        }
        s += d * attack * value(i);
    }
    // W4M 0x49f190: (1 - dist / WormDamageRadius) · WeightingExplosiveSecondaryDamage · WormDamageMagnitude (x2 doubled) per worm
    void blast(Vector3 p, const Blast &b, float a, bool skipSelf = false) {
        started = true;
        if (b.reach <= 0) return;
        for (size_t i = 0; i < g.worms.size(); i++) {
            const float d = Vector3Distance(g.worms[i].pos, p), k = d < b.reach ? (1 - d / b.reach) * L.secondary : 0;
            if (k > 0 && !(skipSelf && (int)i == self)) hurt((int)i, k * b.damage * (g.doubled() ? 2 : 1), a);
        }
    }
};

// Point copy of Game::stepShots for a ballistic or homing (aim != null) projectile; false when lost.
static bool fly(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 v, float wind, bool child, Vector3 &out, const Vector3 *aim = nullptr) {
    bool impact = child || wd.fuse == 0;
    float fuse = aim ? 0 : g.fuseOf(wd);  // the team's current fuse: the AI never changes it
    uint64_t touching = ~0ull;
    for (int i = 0; i < 600; i++) {
        uint64_t now = 0;
        if (aim && (fuse += DT) > Game::HOMING_LOCK && fuse < Game::HOMING_LOCK + Game::HOMING_TIME) v = Game::homingStep(v, p, *aim);
        else v.y -= g.gravity() * (child && wd.kind != Kind::Airstrike ? 1 : wd.grav) * DT;
        if (g.windy(int(&wd - WEAPONS.data()))) v.x += wind * Game::WIND_ACCEL * DT, v.z += g.windZ * Game::WIND_ACCEL * DT;
        for (int k = 0, n = substeps(v); k < n; k++) {  // Game::stepShots' sub-steps
            Vector3 np = p + v * (DT / n);
            out = np;
            if (g.terrain.solid(np)) {
                if (impact) return true;
                v = Vector3Reflect(v, g.terrain.normal(np)) * wd.bounce;
                break;
            }
            p = np;
            uint64_t m = touches(g, np);
            now |= m;
            if (impact && wd.kind != Kind::Donkey && (m & ~touching)) return true;
        }
        touching = now;
        if (!impact && (!wd.restFuse || fuse < wd.fuse || Vector3Length(v) < 1) && (fuse -= DT) < DT / 2) return true;
        if (outside(g, p)) return false;
    }
    return false;
}

// Copy of the sheep/old woman walk: closest approach to `e`, and where its fuse ends.
static Vector3 sheepWalk(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 f, Vector3 e, Vector3 &end) {
    Vector3 v = f * wd.speed, best = p;
    for (int i = 0; i * DT < wd.fuse && p.y > g.water - 2; i++) {
        walkerStep(g.terrain, p, v, g.gravity());
        if (Vector3Distance(p, e) < Vector3Distance(best, e)) best = p;
    }
    end = p;
    return best;
}

// Super sheep autopilot, shared by planning and play: climb over walls, cruise above the target, then dive.
static void steer(const Game &g, Vector3 p, Vector3 v, Vector3 e, Input &in) {
    Vector3 to = e - p, dir = Vector3Normalize(v);
    float h = sqrtf(to.x * to.x + to.z * to.z);
    Vector3 d = h > 6 ? to + Vector3{0, fminf(h * 0.4f, 8), 0} : to;
    float yaw = atan2f(v.x, v.z), pitch = asinf(Clamp(v.y / fmaxf(Vector3Length(v), 0.01f), -1, 1));
    float dy = wrapPi(atan2f(d.x, d.z) - yaw), dp = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) - pitch;
    if (h > 3 && (g.terrain.solid(p + dir * 3) || g.terrain.solid(p + dir * 1.5f))) dp = 1;
    in.turn = q(dy / (2 * DT));
    in.aim = q(dp / (1.5f * DT));
}

// Planned super sheep flight under the autopilot; false if it is lost.
static bool superFly(const Game &g, const WeaponDef &wd, Vector3 pos, float yaw, float pitch, Vector3 e, Vector3 &out) {
    Vector3 d = dirOf(yaw, wd.walks ? Game::SHEEP_TAKEOFF : pitch), p = muzzle(g.terrain, pos, launchPoint(wd, pos, yaw)), v = d * wd.speed;
    uint64_t touching = ~0ull;
    for (float t = wd.fuse; t > 0; t -= DT) {  // walks: takes off at once (think() presses FIRE)
        Input in;
        steer(g, p, v, e, in);
        bool det = Vector3Distance(p, e) < 1.5f;
        float yw = atan2f(v.x, v.z) + in.turn / 127.0f * 2 * DT;
        float pt = Clamp(asinf(Clamp(v.y / fmaxf(Vector3Length(v), 0.01f), -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
        v = dirOf(yw, pt) * wd.speed;
        if (det) { out = p; return true; }
        uint64_t now = 0;
        for (int k = 0, n = substeps(v); k < n; k++) {  // Game::stepShots' sub-steps
            out = p = p + v * (DT / n);
            uint64_t m = touches(g, out);
            now |= m;
            if (g.terrain.solid(out) || (m & ~touching)) return true;
        }
        touching = now;
        if (outside(g, p)) return false;
    }
    return true;
}

static bool seen(const Game &g, Vector3 a, Vector3 b) {
    Vector3 to = b - a, hit;
    float d = Vector3Length(to);
    return d < 0.7f || !g.terrain.raycast({a, to / d}, d - 0.6f, &hit);
}

// Firing position worth for `team`: sight of an enemy at a useful range, high ground, dry, away from mines and sentries.
static float spot(const Game &g, int team, Vector3 p) {
    if (p.y < g.water + 2 || g.terrain.solid(p)) return -1e9f;
    float s = -30;
    for (const Object &o : g.objects) {
        if (o.type == Object::Mine && Vector3Distance(o.pos, p) < 2.5f) return -1e9f;
        if (o.type == Object::Sentry && o.team != team && Vector3Distance(o.pos, p) < WEAPONS[o.weapon].radius && seen(g, o.pos + Vector3{0, 0.4f, 0}, p)) s -= 10;
    }
    float best = -30;
    for (const Worm &e : g.worms) {
        if (!e.alive || e.team == team) continue;
        float d = Vector3Distance(p, e.pos), v = d < 4 ? -10 : 0;
        if (d < 40 && seen(g, p + Vector3{0, 0.3f, 0}, e.pos)) v += 20 - fabsf(d - 15) * 0.4f + Clamp((p.y - e.pos.y) * 0.5f, 0, 5);
        best = fmaxf(best, v);
    }
    return s + 30 + best;
}

static bool ground(const Game &g, Vector3 p, Vector3 &hit) {
    return g.terrain.raycast({p + Vector3{0, 3, 0}, {0, -1, 0}}, 14, &hit);
}

// --- rope race: exact copy of Game::step for the active worm, driven by a parametric swing policy ---

struct Mover { Body b; float yaw, pitch; bool roped; Vector3 anchor; float len; uint8_t prev; int jump = 0; uint8_t kind = 0; Vault vault{}; };

static bool stepRope(const Game &g, Mover &m) {
    Body &b = m.b;
    b.vel.y -= g.gravity() * DT;
    auto blocked = [&](Vector3 p) { return g.terrain.solid({p.x, p.y - R, p.z}) || g.terrain.solid({p.x, p.y + R, p.z}); };
    for (int k = 0, n = substeps(b.vel); k < n; k++) {
        Vector3 np = Vector3Add(b.pos, Vector3Scale(b.vel, DT / n)), d = Vector3Subtract(np, m.anchor);
        float l = Vector3Length(d);
        if (l > m.len) {
            Vector3 u = Vector3Scale(d, 1 / l);
            np = Vector3Add(m.anchor, Vector3Scale(u, m.len));
            float out = Vector3DotProduct(b.vel, u);
            if (out > 0) b.vel = Vector3Subtract(b.vel, Vector3Scale(u, out));
        }
        if (blocked(np)) { np.y = b.pos.y; b.vel.y = 0; }
        if (blocked(np)) { b.vel = Vector3Scale(b.vel, -0.3f); break; }
        b.pos = np;
    }
    return b.pos.y >= g.water;
}

static bool move(const Game &g, Mover &m, const Input &in, float ropeMax) {
    uint8_t pressed = in.buttons & ~m.prev;
    m.prev = in.buttons;
    Body &b = m.b;
    bool tool = m.roped;
    if (!m.vault.t) m.yaw += in.turn / 127.0f * 2.5f * DT;
    const float ws = Game::WALK_SPEED * (g.cfg.wormpot & WP_QUICK_WALK ? 2 : 1);
    if (m.vault.t) vaultStep(b.pos, m.vault, flat(m.yaw) * (float)in.walk);  // Game::step's vault
    else if (b.grounded && !b.motion.slide && in.walk && !m.jump) {  // Game::step's walk
        Vector3 walkV = flat(m.yaw) * (in.walk / 127.0f * Game::INPUT_IMPULSE);
        if (walkStep(g.terrain, b.pos, m.yaw, in.walk / 127.0f * ws * DT, &m.vault)) b.vel = walkV, b.motion.air = true;
        else if (!m.vault.t) slideIfSteep(g.terrain, b.pos, b.vel, b.motion, walkV, g.cfg.wormpot);
    }
    b.motion.input = flat(m.yaw) * (in.walk / 127.0f);
    Vector3 jv;  // Game::step's jump
    if ((pressed & Input::JUMP) && !m.jump && !m.vault.t && b.grounded && !b.motion.slide && !tool && !(g.cfg.wormpot & WP_NO_JUMPING)) m.jump = Game::JUMP_WINDOW, m.kind = 2;
    else if (m.jump && Game::jumpTick(m.jump, m.kind, in.buttons, pressed, in.walk, m.yaw, jv) && b.grounded) b.vel = jv, b.grounded = false, b.motion.air = true;
    Vector3 push = Vector3Scale({sinf(m.yaw), 0, cosf(m.yaw)}, in.walk / 127.0f * DT);
    if (m.roped) {
        if (pressed & Input::JUMP) m.roped = false;
        m.len = Game::reel(m.len, in.aim, ropeMax);
        b.vel = Vector3Add(b.vel, Vector3Scale(push, 6));
    } else {
        m.pitch = Clamp(m.pitch + in.aim / 127.0f * 1.5f * DT, -1.2f, 1.45f);
        Vector3 hit;
        if ((pressed & Input::FIRE) && g.terrain.raycast({b.pos, dirOf(m.yaw, m.pitch)}, ropeMax, &hit)) {
            m.roped = true; m.anchor = hit; m.len = Vector3Distance(b.pos, hit); b.grounded = false;
        }
    }
    if (m.vault.t && (m.roped || Vector3LengthSqr(b.vel) > 0)) b.pos = m.vault.to, m.vault.t = 0;
    if (m.roped) b.motion.slide = false;
    return m.roped ? stepRope(g, m) : m.vault.t ? true : stepBody(g, b, m.yaw);
}

// Aim, fire the rope, swing pushing forward for `release` ticks, let go and fly until landing.
static Input ropePolicy(const Mover &m, Vector3 finish, const Ai::RopePlan &p, Ai::RopeRun &r) {
    Input in;
    r.t++;
    if (p.release < -1) { r.done = r.t >= 60; return in; }  // stay
    if (p.release < 0) {
        in.turn = q(wrapPi(yawTo(m.b.pos, finish) - m.yaw) / (2.5f * DT));
        in.walk = 127;
        if (r.t % 30 == 0) in.buttons = Input::JUMP;
        r.done = r.t >= 60;
        return in;
    }
    if (m.roped) {
        in.walk = 127;
        in.aim = p.reel ? 127 : 0;
        in.turn = q(wrapPi(p.yaw - m.yaw) / (2.5f * DT));
        if (++r.held >= p.release && !(m.prev & Input::JUMP)) { in.buttons = Input::JUMP; r.after = 0; }
        return in;
    }
    if (r.fired) {
        r.done = ++r.after > 20 || (r.after > 1 && m.b.grounded);
        return in;
    }
    float dy = wrapPi(p.yaw - m.yaw), dp = p.pitch - m.pitch;
    in.turn = q(dy / (2.5f * DT));
    in.aim = q(dp / (1.5f * DT));
    if (fabsf(dy) < 0.02f && fabsf(dp) < 0.02f && !m.prev) { in.buttons = Input::FIRE; r.fired = true; }
    r.done = r.t > 120;  // could not aim in time
    return in;
}

Input Ai::race(const Game &g) {
    const Worm &w = g.worms[g.current];
    const float ropeMax = WEAPONS[g.weapon].speed;
    Vector3 fin = g.raceFinish;
    Mover now{{w.pos, w.vel, w.grounded, 0, w.motion}, w.yaw, w.pitch, g.roped, g.anchor, g.ropeLen, g.prevButtons, g.jumpDelay, g.jumpKind, g.vault};
    if (run.done) {
        // spread over frames: standing it waits; in the air it plans from where it will be once the choice is made
        const bool rest = w.grounded && !g.roped && Vector3LengthSqr(w.vel) < 1e-4f;
        if (raceAt < 0) raceAt = 0, raceBest = -1e30f, raceWait = rest ? 0 : 12, raceTimer = g.timer - raceWait;
        Mover from = now;
        for (int k = 0; k < raceWait; k++) move(g, from, Input{}, ropeMax);
        const float fy = yawTo(from.b.pos, fin);
        const bool shots = g.ropeShots < Game::ROPE_SHOTS;  // W4M Ninja.NumShots: with none left, walk or stay
        std::vector<RopePlan> cands = {{0, 0, -1}};
        if (!shots) cands.push_back({0, 0, -2});
        for (float dy : {-0.2f, 0.0f, 0.2f})
            for (float pitch : {0.7f, 0.85f, 1.0f, 1.15f, 1.3f})
                for (int rel : {8, 20, 40, 65, 95, 125}) if (shots) cands.push_back({fy + dy, pitch, rel});
        for (Vector3 o : {Vector3{0, 0, 0}, {1.5f, 1, 0}, {-1.5f, 1, 0}, {0, 1, 1.5f}, {0, 1, -1.5f}, {0, 3, 0}}) {  // climb: hook by the finish, reel in
            Vector3 to = fin + o - from.b.pos;
            for (int rel : {40, 90}) if (shots) cands.push_back({atan2f(to.x, to.z), fminf(atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z)), 1.45f), rel, true});
        }
        const unsigned long s0 = Terrain::samples;
        for (; raceAt < (int)cands.size() && ((!rest && !raceWait) || (long)(Terrain::samples - s0) < budget); raceAt++) {
            const RopePlan &c = cands[raceAt];
            Mover m = from;
            RopeRun r{};
            r.done = false;
            float s = 0;
            int i = 0;
            bool hooked = false;
            for (; i < 400 && i < raceTimer - 1 && !r.done && s == 0; i++, hooked |= m.roped) {
                if (!move(g, m, ropePolicy(m, fin, c, r), ropeMax)) s = -1e20f;
                else if (Vector3Distance(m.b.pos, fin) < 2) s = 1e6f - i;
            }
            // then fall freely: the turn ended (rope dropped) or a new swing may still save a bad landing
            bool over = i >= raceTimer - 1, rescue = !over && !m.b.grounded && g.ropeShots + hooked < Game::ROPE_SHOTS;  // a rescue needs a shot left
            float d = Vector3Distance(m.b.pos, fin), hy = m.b.pos.y;
            if (over) m.roped = false;
            for (int k = 0; k < 300 && s == 0 && !m.b.grounded; k++) {
                if (!move(g, m, Input{}, ropeMax)) s = rescue && hy > g.water + 5 ? -d - 40 : -1e20f;
                else if (Vector3Distance(m.b.pos, fin) < 2) s = 1e6f - i - k;
            }
            if (s == 0) s = -Vector3Distance(m.b.pos, fin) - m.b.fall * 0.3f;
            if (c.release >= 0 && !hooked) s = fminf(s, -1e19f);  // a miss only spends a rope shot
            if (s > raceBest) { raceBest = s; rope = c; }
        }
        if (raceWait > 0) { raceWait--; return Input{}; }
        if (raceAt < (int)cands.size()) return Input{};
        raceAt = -1;
        run = RopeRun{};
        run.done = false;
    }
    return ropePolicy(now, fin, rope, run);
}

// --- paths: W4M AIPathManager A* (0x492d80) over a node grid; each edge is played with the sim's own walk and jump code ---

static constexpr int MAX_ITER = 200;  // W4M m_nMaxNumIterations, every pathfind (0x4b0ba6)
static int octile(int dx, int dz) { dx = std::abs(dx), dz = std::abs(dz); return 10 * std::max(dx, dz) + 4 * std::min(dx, dz); }  // W4M 0x4923a9

// W4M AISceneGraphService node grid: spacing sqrt(Σ NodeGrid box areas / 16000) (PopulatePathingNodes 0x4b2a68), one lattice
// anchored at the boxes' min corner with nodes at origin + i·spacing, i = trunc(offset / spacing + 0.5) (0x4aeb70); a node exists
// only inside a box (0x4ae9d0). Boxes: Terrain::blocks (W4M AddLandBlock).
struct Grid {
    float s = 0.5f, ox = 0, oz = 0;
    std::vector<Vector4> boxes;
    struct Entry { bool ok = false; float hi = -1e4f, lo = 1e4f; };  // m, relative to the start: arc heights met at that offset
    int n[2] = {0, 0};
    std::vector<Entry> reach[2];  // W4M m_fJumpNodes per jump type (forward, backflip), n x n by |dx|, |dz|
    int ci(float x) const { return (int)floorf((x - ox) / s + 0.5f); }
    int cj(float z) const { return (int)floorf((z - oz) / s + 0.5f); }
    Vector2 at(int i, int j) const { return {ox + i * s, oz + j * s}; }
    bool has(int i, int j) const {
        Vector2 p = at(i, j);
        for (const Vector4 &b : boxes) if (p.x >= b.x && p.x < b.z && p.y >= b.y && p.y < b.w) return true;
        return boxes.empty();
    }
    int64_t key(Vector3 p) const { return ((int64_t)ci(p.x) * 100003 + cj(p.z)) * 1009 + (int)floorf(p.y) + 100; }  // + ours: 1 m layer
};

// W4M 0x4af3f0, the jump reach table, in W4M units then m (20 units per m): each jump type's tweak velocity (|x| and y of
// Worm.Jump.Forward 0.07, 0.15 / Backflip 0.04, 0.2) flies in 1 ms steps under Gravity -0.00025 until it falls at 0.3 units/ms,
// twice: horizontal speed + 0.8 AftertouchDelta (0.015) each ms capped at AftertouchStrength 0.1, and - 0.8 AftertouchDelta
// (uncapped). A cell whose distance either arc crosses gets the arc's height there, clamped to [lowest height, 2 x distance];
// it is reachable when an arc crosses it going down. Cells past the back-steered arc's end get the lowest height as minimum.
static void buildReach(Grid &gr) {
    const double G = -0.00025, S = 0.1, D = 0.8 * 0.015, V[2][2] = {{0.07, 0.15}, {0.04, 0.2}}, s = gr.s * 20;
    for (int t = 0; t < 2; t++) {
        const double h = V[t][0], vy0 = V[t][1], T = (vy0 + 0.3) / -G, H = T * (vy0 + 0.5 * G * T);
        const int N = gr.n[t] = (int)(T * (S + h) / s);
        auto &e = gr.reach[t];
        e.assign((size_t)N * N, {});
        double y = 0, vy = vy0, x[2] = {0, 0}, px[2], sp[2] = {h, h}, at[2] = {0, 0}, sign[2] = {1, -1};
        for (;;) {
            const double yp = y;
            y += vy, vy += G;
            if (!(vy > -0.3)) break;
            for (int i = 0; i < 2; i++) {
                px[i] = x[i], x[i] += sp[i];
                const double was = at[i];
                at[i] += sign[i] * D, sp[i] += at[i] - was;
                if (sp[i] > S) sp[i] = S;
            }
            if (!(x[0] > x[1])) continue;
            for (int i = 0; i < 2; i++) {
                if (x[i] == px[i]) continue;
                for (int b = 0; b < N; b++)
                    for (int a = 0; a < N; a++) {
                        const double r = sqrt((double)(a * a + b * b)) * s, f = (r - px[i]) / (x[i] - px[i]);
                        if (!(f > 0 && f <= 1)) continue;
                        Grid::Entry &c = e[(size_t)N * b + a];
                        if (vy <= 0) c.ok = true;
                        const double yc = fmax(fmin((1 - f) * yp + f * y, 2 * r), H) / 20;
                        c.hi = fmaxf(c.hi, (float)yc), c.lo = fminf(c.lo, (float)yc);
                    }
            }
        }
        for (int b = 0; b < N; b++)
            for (int a = 0; a < N; a++) if (!(x[1] > sqrt((double)(a * a + b * b)) * s)) e[(size_t)N * b + a].lo = (float)(H / 20);
    }
}

static Grid makeGrid(const Terrain &t) {
    Grid gr;
    gr.boxes = t.blocks;
    float area = 0;  // m², the 1/20 unit scale cancels out
    gr.ox = gr.oz = 1e9f;
    for (const Vector4 &b : gr.boxes) area += (b.z - b.x) * (b.w - b.y), gr.ox = fminf(gr.ox, b.x), gr.oz = fminf(gr.oz, b.y);
    if (gr.boxes.empty()) gr.ox = gr.oz = 0, area = Terrain::NX * Terrain::NZ * Terrain::VOX * Terrain::VOX;
    gr.s = sqrtf(area / 16000);
    buildReach(gr);
    return gr;
}

// W4M 0x4ade10: a node's heights from 3 x 3 rays down at ±spacing/3 around it from `from`: lo / hi = lowest / highest hit; false
// (water, blocked) when a ray finds no land or lands in the water.
static bool probe(const Game &g, const Grid &gr, int i, int j, float from, float &lo, float &hi) {
    const Vector2 c = gr.at(i, j);
    lo = 1e9f, hi = -1e9f;
    for (int a = -1; a <= 1; a++)
        for (int b = -1; b <= 1; b++) {
            float y = from;
            const float x = c.x + a * gr.s / 3, z = c.y + b * gr.s / 3;
            while (y > g.water && !g.terrain.solid({x, y, z})) y -= Terrain::VOX / 2;
            if (y <= g.water) return false;
            lo = fminf(lo, y), hi = fmaxf(hi, y);
        }
    return true;
}

static Mover moverOf(const Game &g) {
    const Worm &w = g.worms[g.current];
    return {{w.pos, w.vel, w.grounded, 0, w.motion}, w.yaw, w.pitch, false, {}, 0, g.prevButtons, g.jumpDelay, g.jumpKind, g.vault};
}

// A player's inputs for one path step; r.done once the worm stands still after it.
static Input stepInput(const Game &g, const Mover &m, const Ai::Step &s, Ai::StepRun &r) {
    Input in;
    r.t++;
    const bool still = m.b.grounded && Vector3LengthSqr(m.b.vel) < 1e-4f && !m.jump && !m.vault.t;
    if (r.air) { r.done = still || r.t > 400; return in; }
    if (m.vault.t) { in.walk = 127; return in; }  // releasing the stick would drop it back (W4M Vaulting)
    if (s.move == 0) {
        Vector3 d = s.to - m.b.pos;
        float h = sqrtf(d.x * d.x + d.z * d.z), dy = wrapPi(atan2f(d.x, d.z) - m.yaw), ws = Game::WALK_SPEED * (g.cfg.wormpot & WP_QUICK_WALK ? 2 : 1);
        if (h < 0.05f || !m.b.grounded || r.t > 120) { r.stuck = h >= 0.05f && m.b.grounded; r.air = true; r.done = still; return in; }
        in.turn = q(dy / (2.5f * DT));
        if (fabsf(dy) < 0.3f) in.walk = (int8_t)Clamp(roundf(127 * h / (ws * DT)), 1, 127);
        return in;
    }
    if (m.jump || !m.b.grounded) {  // forward jump pending, or flying
        if (s.move == 2 && m.jump && !(m.prev & Input::JUMP)) in.buttons = Input::JUMP;  // second press: backflip
        r.air = !m.b.grounded;
        return in;
    }
    float dy = wrapPi(s.yaw + (s.move == 2 ? PI : 0) - m.yaw);  // a backflip leaves backwards
    in.turn = q(dy / (2.5f * DT));
    if (fabsf(dy) < 2e-3f && still && !(m.prev & Input::JUMP)) in.buttons = Input::JUMP;
    r.done = r.t > 200;
    return in;
}

// W4M Fits 0x59edf0: three vertical rods at (±4, -3) and (0, 5) units, feet to head; ours scaled to BODY_R, from above the steps
static bool fits(const Terrain &t, Vector3 p) {
    const float k = Game::BODY_R / 4;
    for (Vector2 o : {Vector2{4 * k, -3 * k}, Vector2{-4 * k, -3 * k}, Vector2{0, 5 * k}})
        for (float y = p.y - R + Game::STEP + 0.05f; y <= p.y + R; y += Terrain::VOX / 2)
            if (t.solid({p.x + o.x, y, p.z + o.y})) return false;
    return true;
}

// Plays one step from m as the follower will; false if the worm drowns, gets hurt, never settles or ends where it doesn't fit.
static bool runStep(const Game &g, Mover &m, const Ai::Step &s, int &ticks) {
    Ai::StepRun r;
    bool rest = false;  // the last tick left a still worm unchanged: turning on the spot can skip stepBody
    for (ticks = 0; ticks < 600 && !r.done; ticks++) {
        Input in = stepInput(g, m, s, r);
        if (rest && !in.walk && !in.buttons && !m.jump && !m.vault.t) { m.yaw += in.turn / 127.0f * 2.5f * DT; m.prev = in.buttons; continue; }
        Body before = m.b;
        if (!move(g, m, in, 0)) return false;
        rest = m.b.grounded && before.grounded && before.pos.x == m.b.pos.x && before.pos.y == m.b.pos.y && before.pos.z == m.b.pos.z &&
               Vector3LengthSqr(before.vel) == 0 && Vector3LengthSqr(m.b.vel) == 0;
    }
    return r.done && m.b.fall == 0 && !r.stuck && fits(g.terrain, m.b.pos);
}

// Where to stand after the shot: away from enemies (and out of their sight), the blast, mines and sentries.
static float haven(const Game &g, int team, Vector3 p, Vector3 boom) {
    float near = 20, s = 0;
    for (const Worm &e : g.worms) {
        if (!e.alive || e.team == team) continue;
        near = fminf(near, Vector3Distance(e.pos, p));
        if (Vector3Distance(e.pos, p) < 40 && seen(g, p + Vector3{0, 0.3f, 0}, e.pos)) s -= 4;
    }
    if (p.y < g.water + 1.5f) return -1e9f;
    s += near + 0.5f * fminf(Vector3Distance(p, boom), 10);
    for (const Object &o : g.objects) {
        if (o.type == Object::Mine && Vector3Distance(o.pos, p) < 3) s -= 20;
        if (o.type == Object::Sentry && o.team != team && Vector3Distance(o.pos, p) < WEAPONS[o.weapon].radius) s -= 5;
    }
    return s;
}

// Destination scoring (W4M ScoreAllMoveNodes, 0x4ab490) then A* to the best ones, one unit of work per call.
struct Search {
    enum { Crate, Closer, Retreat };
    std::shared_ptr<const Grid> grid;
    std::vector<int64_t> blocked;  // W4M path-failed blockages (0x490551): nodes no edge may end on
    int purpose = Crate, team = 0, limit = 0;  // limit: most ticks the path may take
    Vector3 boom{}, target{};                  // Retreat: the blast to flee; Closer: the worm to approach
    std::vector<std::pair<float, Vector3>> cands;
    size_t scored = 0, tries = 0;
    float here = -1e9f;  // the start's own score
    Mover root{};
    struct Node { Mover m; Ai::Step step; int parent, g, h, ticks; bool open; };
    std::vector<Node> nodes;
    std::unordered_map<int64_t, int> index;
    std::unordered_map<int64_t, float> tops;  // layer-0 node heights met by jump arcs
    int iter = 0, best = 0, cur = -1, mv = 0;  // cur: node being expanded, mv: its next edge (move type x 8 + direction)
    uint8_t walked = 0;                        // directions cur walked to: a diagonal needs both of its sides
    bool started = false;
    int h(Vector3 p) const {  // octile cells to the nearest goal
        int b = 1 << 20;
        for (const auto &c : cands) b = std::min(b, octile(grid->ci(c.second.x) - grid->ci(p.x), grid->cj(c.second.z) - grid->cj(p.z)));
        return b;
    }
    bool goal(Vector3 p) const {
        for (const auto &c : cands) if (grid->ci(c.second.x) == grid->ci(p.x) && grid->cj(c.second.z) == grid->cj(p.z) && fabsf(p.y - c.second.y) < 1.5f) return true;
        return false;
    }
    float score(const Game &g, Vector3 p) const {  // Closer: W4M CAIPlanMoveCloserToTarget, nearer is better on any safe spot
        if (purpose == Retreat) return haven(g, team, p, boom);
        float s = spot(g, team, p);
        return s < -1e8f ? s : -Vector3Distance(p, target);
    }
    float top(const Game &g, int i, int j) {  // W4M layer 0 (0x4aed70, from the box top): its highest node height, water if none
        auto [it, fresh] = tops.try_emplace(((int64_t)i << 32) ^ (uint32_t)j, g.water);
        float lo, hi;
        if (fresh && grid->has(i, j) && probe(g, *grid, i, j, (Terrain::NY - 1) * Terrain::VOX, lo, hi)) it->second = hi;
        return it->second;
    }
    // W4M 0x4928e1: a jump edge leaves a top-layer node for a reach-table cell whose node heights (lo, hi) give landing deltas
    // d1 = hi' - lo, d2 = lo' - hi inside the arc's heights there; stepping back from the landing to the start (0x492c0d), each
    // cell crossed keeps its top under the arc, interpolated by where the landing sits in that range.
    bool jumpOk(const Game &g, int t, Vector3 from, Vector3 to) {
        const Grid &gr = *grid;
        const int i0 = gr.ci(from.x), j0 = gr.cj(from.z), i1 = gr.ci(to.x), j1 = gr.cj(to.z), dx = i1 - i0, dz = j1 - j0, N = gr.n[t];
        if (std::abs(dx) >= N || std::abs(dz) >= N) return false;
        const Grid::Entry &E = gr.reach[t][(size_t)N * std::abs(dz) + std::abs(dx)];
        float lo0, hi0, lo1, hi1;
        if (!E.ok || !probe(g, gr, i0, j0, from.y - R + 1, lo0, hi0) || !probe(g, gr, i1, j1, to.y - R + 1, lo1, hi1)) return false;
        if (top(g, i0, j0) > hi0 + Terrain::VOX) return false;  // jumps only from layer 0 (0x4928d6)
        const float d1 = hi1 - lo0, d2 = lo1 - hi0;
        if (!(E.hi > d1 && E.lo < d1 && E.hi > d2 && E.lo < d2)) return false;
        const float frac = E.hi > E.lo ? (E.hi - (d1 + d2) / 2) / (E.hi - E.lo) : 0, slope = dz ? (float)std::abs(dx) / std::abs(dz) : 0;
        for (int i = i1, j = j1; i != i0 || j != j0;) {
            if (i != i1 || j != j1) {
                const Grid::Entry &c = gr.reach[t][(size_t)N * std::abs(j - j0) + std::abs(i - i0)];
                if (!((1 - frac) * c.hi + frac * c.lo > top(g, i, j) - lo0)) return false;
            }
            if (dz && j != j0) {
                if (slope < (float)std::abs(i - i0) / std::abs(j - j0)) i += dx <= 0 ? 1 : -1;
                else j += dz <= 0 ? 1 : -1;
            } else if (dx) i += dx <= 0 ? 1 : -1;
            else break;
        }
        return true;
    }
};

void Ai::startSearch(const Game &g, int purpose, Vector3 to) {
    auto s = std::make_shared<Search>();
    const Worm &w = g.worms[g.current];
    s->purpose = purpose, s->team = w.team, s->boom = lastBoom, s->target = to, s->root = moverOf(g), s->grid = grid, s->blocked = blocked;
    if (purpose == Search::Retreat) {  // the weapon's retreat time (W4M timer from PostLaunchDelay's end), after the 1 s pause
        const WeaponDef &wd = WEAPONS[plan.weapon];
        s->limit = g.retreatTicks(wd) + msTicks(wd.postLaunch) - 60;
    } else s->limit = (int)((thinkTimer * DT - 10) / DT);  // ForbidMoveIfWouldLeaveTimeLessThan 10 s
    if (purpose == Search::Crate) s->cands = {{0, to}}, s->scored = 1;
    else {  // W4M scores the 21 x 21 nodes around the worm's (0x4ab5bc); a retreat node comes from the same window (0x4a8c60)
        const int i0 = grid->ci(w.pos.x), j0 = grid->cj(w.pos.z);
        for (int i = -10; i <= 10; i++)
            for (int j = -10; j <= 10; j++)
                if ((i || j) && grid->has(i0 + i, j0 + j)) s->cands.push_back({0, {grid->at(i0 + i, j0 + j).x, w.pos.y + 5, grid->at(i0 + i, j0 + j).y}});
        s->here = s->score(g, w.pos) + 1;  // ours: a gain of 1 m or one haven point
    }
    search = s;
    if (purpose != Search::Retreat) mode = Mode::Search;
}

void Ai::searchStep(const Game &g) {
    static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    Search &s = *search;
    const Grid &gr = *s.grid;
    auto done = [&](int end) {  // end: the path's last node, -1 if none
        std::vector<Step> steps;
        for (int i = end; i > 0; i = s.nodes[i].parent) steps.push_back(s.nodes[i].step);
        std::reverse(steps.begin(), steps.end());
        int purpose = s.purpose;
        if (end > 0) lastPurpose = purpose, lastGoals = s.cands, lastTarget = s.target;  // for a repath
        search.reset();
        if (end > 0) takePath(g, std::move(steps));
        if (purpose == Search::Retreat) return;
        if (end > 0) { wait = walks++ ? (int)(levelOf(g, g.worms[g.current].team).nonFirstMove / DT) : 0; mode = Mode::Walk; return; }  // DelayBeforeNonFirstMove
        mode = Mode::Eval;
        decide(g);
    };
    // W4M 0x494a42: a path, whole or partial, ending under 50 units (2.5 m) from the start is "too short to bother with"
    auto far = [&](int end) { return end > 0 && Vector3Distance(s.nodes[end].m.b.pos, s.root.b.pos) >= 2.5f; };
    if (s.scored < s.cands.size()) {
        auto &c = s.cands[s.scored++];
        Vector3 hit;
        c.first = ground(g, c.second, hit) ? s.score(g, c.second = hit + Vector3{0, R, 0}) : -1e9f;
        if (s.scored == s.cands.size()) {
            std::stable_sort(s.cands.begin(), s.cands.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            while (!s.cands.empty() && s.cands.back().first <= s.here) s.cands.pop_back();
            if (s.cands.size() > 3) s.cands.resize(3);  // ponytail: one A* toward the best three [ours]
        }
        return;
    }
    if (s.cands.empty() || s.tries) return done(-1);
    if (!s.started) {
        s.started = true, s.cur = -1, s.iter = 0, s.best = 0;
        Vector3 p = s.root.b.pos;
        s.nodes = {{s.root, {}, -1, 0, s.h(p), 0, true}};
        s.index = {{gr.key(p), 0}};
        return;
    }
    if (s.cur < 0) {  // pop the open node with the lowest F = G + H (W4M 0x492d80)
        int bi = -1;
        for (size_t i = 0; i < s.nodes.size(); i++)
            if (s.nodes[i].open && (bi < 0 || s.nodes[i].g + s.nodes[i].h < s.nodes[bi].g + s.nodes[bi].h)) bi = (int)i;
        const Vector3 p = bi < 0 ? Vector3{} : s.nodes[bi].m.b.pos;
        if (bi >= 0 && s.goal(p)) {
            if (far(bi)) return done(bi);
            s.tries++, s.started = false;
            return;
        }
        if (bi >= 0 && s.iter++ < MAX_ITER) {
            s.nodes[bi].open = false, s.cur = bi, s.mv = 0, s.walked = 0;
            if (s.nodes[bi].h < s.nodes[s.best].h) s.best = bi;  // W4M 0x490ed0: the partial path ends at the closed node of least h
        }
    }
    if (s.cur < 0) {  // W4M takes the partial path unless it is too short
        if (far(s.best)) return done(s.best);
        s.tries++, s.started = false;
        return;
    }
    const int bi = s.cur;
    const Search::Node n = s.nodes[bi];
    const Vector3 p = n.m.b.pos;
    const int cx = gr.ci(p.x), cz = gr.cj(p.z);
    const Level &L = levelOf(g, s.team);
    // one edge per unit (W4M expands a node in one go; ours runs each edge in the sim): WALK, JUMP_FORWARD, JUMP_BACKFLIP x 8 directions
    while (s.mv < 24 && ((s.mv / 8 == 1 && !L.jump) || (s.mv / 8 == 2 && !L.flip))) s.mv++;  // MovementJumpForward/BackflipAllowed
    if (s.mv >= 24) { s.cur = -1; return; }
    static const int ORDER[8] = {0, 2, 4, 6, 1, 3, 5, 7};  // the sides of a diagonal first
    const uint8_t mv = (uint8_t)(s.mv / 8);
    const int d = mv ? s.mv % 8 : ORDER[s.mv % 8];
    s.mv++;
    if (n.ticks + 10 > s.limit) return;
    // W4M 0x4926f1: a diagonal walk only once walk edges were added to both of its sides from this node
    if (!mv && (d & 1) && !((s.walked >> ((d + 7) % 8) & 1) && (s.walked >> ((d + 1) % 8) & 1))) return;
    const Vector2 to = gr.at(cx + DX[d], cz + DZ[d]);
    Step st{{to.x, p.y, to.y}, atan2f((float)DX[d], (float)DZ[d]), mv};
    Mover m = n.m;
    int ticks;
    if (!runStep(g, m, st, ticks)) return;
    if (n.ticks + ticks + (int)(s.h(m.b.pos) / 10 * gr.s / Game::WALK_SPEED / DT) > s.limit) return;  // can't reach a goal in time
    Vector3 e = m.b.pos;
    int ex = gr.ci(e.x), ez = gr.cj(e.z);
    if (ex == cx && ez == cz) return;
    float lo, hi;  // W4M 0x4aea60: no edge to a node outside the grid, in the water, or blocked (heights over 20 units apart, 0x4aef54)
    if (!gr.has(ex, ez) || !probe(g, gr, ex, ez, e.y - R + 1, lo, hi) || hi - lo > 1) return;
    if (mv && !s.jumpOk(g, mv - 1, p, e)) return;  // the jump reach table (0x4924d7)
    if (std::find(s.blocked.begin(), s.blocked.end(), gr.key(e)) != s.blocked.end()) return;
    if (!mv) s.walked |= 1 << d;
    if (mv) st.to = e;  // a jump's landing, for MovementJumpError
    int cost = n.g + octile(ex - cx, ez - cz) + (mv == 1 ? 40 : mv == 2 ? 60 : 0);  // W4M 0x491fd8; jumps +40, backflips +60 (0x492008, 0x492003)
    Search::Node nn{m, st, bi, cost, s.h(e), n.ticks + ticks, true};
    auto it = s.index.find(gr.key(e));
    if (it != s.index.end()) {
        Search::Node &o = s.nodes[it->second];
        if (o.open && cost < o.g) o = nn;
        return;
    }
    s.index[gr.key(e)] = (int)s.nodes.size();
    s.nodes.push_back(nn);
}

static float noise(uint32_t &r) { r = r * 1664525u + 1013904223u; return ((r >> 8) / 16777216.0f) * 2 - 1; }

// W4M 0x496f24: MovementJumpError scales each jump's displacement by 1 ± e per component; only its heading is the AI's to choose.
void Ai::takePath(const Game &g, std::vector<Step> &&p) {
    const Worm &w = g.worms[g.current];
    const float e = levelOf(g, w.team).jumpErr;
    uint32_t r = salt * 2246822519u + (uint32_t)walks;
    Vector3 from = w.pos;
    for (Step &s : p) {
        if (s.move && e > 0) s.yaw = atan2f((s.to.x - from.x) * (1 + e * noise(r)), (s.to.z - from.z) * (1 + e * noise(r)));
        from = s.to;
    }
    path = std::move(p);
    pathAt = 0;
    stepRun = {};
}

bool Ai::follow(const Game &g, Input &in) {  // false once the path is over, or failed (pathFailed)
    if (pathAt >= path.size()) return false;
    in = stepInput(g, moverOf(g), path[pathAt], stepRun);
    if (!stepRun.done) return true;
    const Step &st = path[pathAt];
    bool off = stepRun.stuck;  // a walk held off its node (a jump lands off it on purpose: MovementJumpError)
    pathAt++, stepRun = {};
    if (!off) return true;
    pathFailed = true, blocked.push_back(grid->key(st.to)), path.clear();  // W4M "Adding path-failed blockage at"
    return false;
}

// W4M AIPathAction 0x490551: on a failed step, block its node and pathfind again to the same goal; past 2 repaths, no more movement
bool Ai::repath(const Game &g) {
    pathFailed = false;
    if (++repaths > 2 || lastPurpose < 0) { walks = 3; return false; }  // "Has already done too many repaths, forbidding further movement"
    startSearch(g, lastPurpose, lastTarget);
    search->cands = lastGoals, search->scored = lastGoals.size();
    return true;
}

// W4M 0x49b210: think while under the frame's budget; an overrun carries to the next frames (at most one frame's worth).
void Ai::slice(const Game &g) {
    debt = std::max(0L, debt - budget);
    while (debt < budget && (search || (mode == Mode::Eval && g.phase == Phase::Aim && stage >= 0))) {  // stage < 0: think() starts the next eval
        unsigned long s0 = Terrain::samples;
        unit(g);
        debt += (long)(Terrain::samples - s0) + 100;
    }
    debt = std::min(debt, 2 * budget);
}

// W4M 0x4a9e90: threat rating of a worm, probed in 8 directions at 50..200 units (0x90ebf8), weight 2/(k+2) by range: water or no
// land 0.2, a drop over 30 units 0.05, and within 100 units with a linear falloff mines 0.1, oil drums 0.05 (GameLogicService
// m_OilDrumIds, 0x4f56a0) and other worms 0.05. Returns the total capped at 1; `away` is opposite the worst direction (zero if none).
static float threatOf(const Game &g, const Worm &e, Vector3 &away) {
    static const float RANGE[4] = {2.5f, 5, 7.5f, 10};
    float worst = 0, total = 0;
    int dir = -1;
    for (int d = 0; d < 8; d++) {
        Vector3 f = flat(d * PI / 4);
        float t = 0;
        for (int k = 0; k < 4; k++) {
            const float wgt = 2.0f / (k + 2);
            Vector3 p = e.pos + f * RANGE[k], hit;
            if (!g.terrain.raycast({{p.x, e.pos.y + 3, p.z}, {0, -1, 0}}, 30, &hit) || hit.y < g.water) t += 0.2f * wgt;
            else if (e.pos.y - R - hit.y > 1.5f) t += 0.05f * wgt;
            for (const Object &o : g.objects)
                if (o.type == Object::Mine || o.type == Object::Barrel) t += (o.type == Object::Mine ? 0.1f : 0.05f) * wgt * fmaxf(0, 1 - Vector3Distance(o.pos, p) / 5);
            for (const Worm &x : g.worms) if (x.alive && &x != &e) t += 0.05f * wgt * fmaxf(0, 1 - Vector3Distance(x.pos, p) / 5);
        }
        total += t;
        if (t > worst) worst = t, dir = d;
    }
    away = dir < 0 ? Vector3{} : flat(dir * PI / 4) * -1.0f;
    return fminf(total, 1);
}

void Ai::unit(const Game &g) {
    if (search) return searchStep(g);
    if (!threats.empty()) {  // threat rating; ProjectileSweetSpotDistance (0x4a9be0): also aim that far on a threatened enemy's safe side
        const int i = threats.back();
        const Worm &e = g.worms[i];
        Vector3 away;
        rating[i] = threatOf(g, e, away);
        const Level &L = levelOf(g, g.worms[g.current].team);
        if (e.team != g.worms[g.current].team && L.sweet > 0 && Vector3LengthSqr(away) > 0) tpos.push_back(e.pos + away * L.sweet), tworm.push_back(i);
        threats.pop_back();
        return;
    }
    const int T = (int)tpos.size(), N = (int)WEAPONS.size() * T, team = g.worms[g.current].team;
    while (stage < N && !g.usable(team, stage / T)) stage++, sub = 0;  // unusable (or scheme-delayed) weapons are skipped for free
    if (stage < N) {
        int n = evalWeapon(g, stage / T, stage % T, sub);
        if (++sub >= n) sub = 0, stage++;
    }
    if (stage >= N) { stage = -1; decide(g); }
}

// --- shot planning ---

// W4M has a worm-select mode (0x4a4f2a) but only when the data key ChooseWorm.Enabled is set, which is 0 in LOCAL.XOM and
// written nowhere: the CPU plans for its current worm only and never uses Change Worm.
void Ai::startEval(const Game &g) {
    const Worm &w = g.worms[g.current];
    const Level &L = levelOf(g, w.team);
    plan = Plan{};
    tpos.clear();
    tworm.clear();
    threats.clear();
    rating.assign(g.worms.size(), 0);
    int nearest = -1;
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &e = g.worms[i];
        if (e.alive && (L.threat != 0 || (L.sweet > 0 && e.team != w.team))) threats.push_back((int)i);  // rated by unit()
        if (!e.alive || e.team == w.team) continue;
        if (nearest < 0 || Vector3Distance(e.pos, w.pos) < Vector3Distance(g.worms[nearest].pos, w.pos)) nearest = (int)i;
        Vector3 to = e.pos - w.pos;  // also the ground just short of it: a shell there still splashes
        tpos.insert(tpos.end(), {e.pos, e.pos - Vector3Normalize({to.x, 0, to.z}) * 1.5f});
        tworm.insert(tworm.end(), {(int)i, (int)i});
    }
    if (nearest < 0) return;
    plan.target = nearest;
    stage = sub = 0;
    thinkTimer = g.timer;
    regress(g);
}

int Ai::evalWeapon(const Game &g, int wi, int only, int sub) {
    const int me = g.current;
    const Worm &w = g.worms[me];
    const WeaponDef &wd = WEAPONS[wi];
    const int team = w.team;
    const Level &L = levelOf(g, team);
    const float wind = g.wind;  // W4M solves with the exact wind at every level
    if (!g.usable(team, wi) && !(g.shotsLeft && wi == g.weapon)) return 0;  // W4M SchemeData Delay: not before its turn
    if (w.nailed && !nailUsable(wd.kind)) return 0;  // Tail Nail: the sim refuses it
    // W4M 0x498a0a, 0x49c060: a positive score ×(1 ± PlanScoreRandomise) ×Pref(weapon) ×(1 − PreferVariety if last turn's weapon)
    uint32_t h = (salt ^ (uint32_t)wi * 2654435761u) * 2246822519u;
    const std::string &n = wd.name;
    const float pref = n == "Prod" ? 0.3f : n == "Cluster Grenade" ? L.cluster : n == "Gas Canister" ? L.gas : wd.kind == Kind::Homing ? L.homing : 1;
    const float taste = (1 + L.randomise * ((h >> 8) / 16777216.0f * 2 - 1)) * pref * (wi == g.picked[team] ? 0.6f : 1);
    // W4M 0x49bd60: AddScoreMove 10, + AddScoreMoveIfNotMoved 20 before the turn's first move, for a plan with a move (ours: the
    // eval after a Closer walk) or always for a close-range explosive (0x4a2c70 forces it)
    auto bonus = [&](bool closeRange) { return closeRange || afterCloser ? 10.0f + (walks == (afterCloser ? 1 : 0) ? 20 : 0) : 0; };
    auto consider = [&](float score, float yaw, float pitch, int charge, int target) {
        for (const Fail &f : failed)  // W4M 0x4a6590: this worm at a target that survived it, x(1 - effect / 2), another weapon 0.2 of that
            if (f.worm == me && f.target == target) score *= 1 - 0.5f * f.effect * (f.weapon == wi ? 1 : 0.2f);
        float rank = score > 0 ? score * taste : score;
        if (rank > plan.rank) plan = {wi, charge, target, yaw, pitch, score, rank};
    };
    int cands = 0;
    auto pick = [&] { return sub < 0 || cands++ == sub; };  // sub >= 0: only that candidate, the rest are counted
    auto shell = [&](Vector3 at) {  // W4M CAIPlanAttackProjectile 0x4a0540, close-range explosive 0x4a2c70 for a dropped shell
        Outcome o(g, L, me, rating);
        o.blast(at, blastOf(wd, false), L.threat, dropped(wd));  // ours: a dropped shell spares the thrower, retreat() walks clear
        return o.s + bonus(dropped(wd));
    };
    for (size_t k = only < 0 ? 0 : only; k < (only < 0 ? tpos.size() : only + 1); k++) {
        const Vector3 e = tpos[k];
        const int ti = tworm[k];
        const bool isWorm = Vector3Equals(e, g.worms[ti].pos);
        const Vector3 to = e - w.pos;
        const float yawE = atan2f(to.x, to.z), horiz = sqrtf(to.x * to.x + to.z * to.z);
        switch (wd.kind) {
        case Kind::Shell:
            if (dropped(wd) && pick()) {  // also set it down at the feet, facing the target
                Vector3 d = dirOf(yawE, 0), out;
                if (fly(g, wd, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), d * wd.speed, wind, false, out)) consider(shell(out), yawE, 0, 1, ti);
            }
            // constant acceleration A: hit T at time t with V = (T - P - A t(t+DT)/2) / t (semi-implicit Euler)
            for (float t = 0.2f; t < 4.5f; t += 0.43f) {  // 11 arcs, as W4M samples about 11 speeds (0x4ace50)
                const bool wf = g.windy(int(&wd - WEAPONS.data()));
                Vector3 A = {wf ? wind * Game::WIND_ACCEL : 0, -g.gravity() * wd.grav, wf ? g.windZ * Game::WIND_ACCEL : 0}, P = launchPoint(wd, w.pos, yawE);
                Vector3 V = (e - P - A * (0.5f * t * (t + DT))) / t;
                float sp = Vector3Length(V), pitch = asinf(V.y / sp), yaw = atan2f(V.x, V.z);
                const float lo = launchSpeed(wd, 0);
                if (sp > wd.speed || sp < lo || pitch < -1.2f || pitch > 1.45f || !pick()) continue;
                int n = std::max(1, (int)roundf((wd.base >= 0 ? (sp - lo) / (wd.speed - lo) : sp / wd.speed) * 90));  // ticks of charge
                float pw = 0;
                for (int j = 0; j < n; j++) pw = fminf(1, pw + DT / 1.5f);
                Vector3 d = dirOf(yaw, pitch), out;
                if (fly(g, wd, muzzle(g.terrain, w.pos, P), d * launchSpeed(wd, pw), wind, false, out)) consider(shell(out), yaw, pitch, n, ti);
            }
            break;
        case Kind::Homing: {  // locked on the target's feet from the Blimp (act())
            if (!isWorm) break;
            float pitch = atan2f(to.y - R - 0.2f, horiz);
            Vector3 tgt = e - Vector3{0, R, 0}, d = dirOf(yawE, pitch), out;
            if (pitch < -1.2f) break;
            for (int n : {30, 60, 90}) {
                if (!pick()) continue;
                float pw = 0;
                for (int j = 0; j < n; j++) pw = fminf(1, pw + DT / 1.5f);
                if (fly(g, wd, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), d * launchSpeed(wd, pw), wind, false, out, &tgt)) consider(shell(out), yawE, pitch, n, ti);
            }
            break;
        }
        case Kind::Shotgun: {  // W4M CAIPlanAttackDirect 0x4a0a20: one shot (bCanMoveBetweenShots) as a WormDamageRadius blast, no knock
            if (!pick()) break;
            float pitch = atan2f(to.y - (isWorm ? 0 : 0.3f), horiz);
            Vector3 d = dirOf(yawE, pitch), o = muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), hit;
            float dist = g.terrain.raycast({o, d}, 60, &hit) ? Vector3Distance(o, hit) : 60;
            for (const Worm &x : g.worms) {
                float t = Vector3DotProduct(x.pos - o, d);
                if (x.alive && &x != &w && t > 0 && t < dist && Vector3Distance(x.pos, o + d * t) < R + 0.1f) dist = t;
            }
            if (dist < 60 && pitch > -1.2f && pitch < 1.45f) {
                Outcome oc(g, L, me, rating);
                Blast b = blastOf(wd, false);
                b.damage = wd.damage;
                oc.blast(o + d * dist, b, 0);
                consider(oc.s + bonus(false), yawE, pitch, 0, ti);
            }
            break;
        }
        case Kind::Melee:  // W4M CAIPlanAttackMeleeActionable 0x4a2b10: the plan's target takes WormDamageMagnitude
            if (!isWorm || Vector3Distance(e, w.pos) > 3.5f) break;
            for (float dy : {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f})
                for (float pitch : {0.0f, 0.5f, 1.0f}) {
                    if (!pick()) continue;
                    Worm at = w;
                    at.yaw = yawE + dy;
                    if (!meleeHits(at, e, wd)) continue;
                    Outcome oc(g, L, me, rating);
                    oc.hurt(ti, wd.damage * (g.doubled() ? 2 : 1), L.threat, !wd.pins);  // armour spares ids 10..12 only (bat, prod, fire punch)
                    consider(oc.s + bonus(false), at.yaw, pitch, 0, ti);
                }
            break;
        case Kind::Sheep:  // W4M CAIPlanAttackAnimal 0x4a4210: the payload's blast where the walker reaches the target
        case Kind::OldWoman:
        case Kind::Scouser: {
            if (!isWorm || !pick()) break;
            Vector3 f = flat(yawE), end, p = sheepWalk(g, wd, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), f, e, end);
            if (Vector3Distance(p, e) < 2) consider(shell(p), yawE, w.pitch, 0, ti);
            break;
        }
        case Kind::SuperSheep:  // and the Starburst: W4M CAIPlanAttackStarburst 0x4a43a0 adds its rider's death (0x49ed30, all its hp, knock 1)
            if (!isWorm) break;
            for (float pitch : {0.3f, 0.9f}) {
                if (!pick()) continue;
                Vector3 out;
                if (!superFly(g, wd, w.pos, yawE, pitch, e, out) || Vector3Distance(out, e) >= 2) continue;
                Outcome o(g, L, me, rating);
                o.blast(out, blastOf(wd, false), L.threat);
                if (wd.name == "Starburst") o.hurt(me, (float)w.hp, 1);  // Worm.Vapourize
                consider(o.s + bonus(false), yawE, pitch, 0, ti);
            }
            break;
        case Kind::Mine: {  // W4M CAIPlanAttackLandmine: CloseRangeExplosive 0x4a2c70, the blast scored where it is laid
            Vector3 hit;
            if (!isWorm || !pick() || !ground(g, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), hit)) break;
            Outcome o(g, L, me, rating);
            o.blast(hit, Game::MINE_BLAST, L.threat, true);  // as a dropped shell: retreat() walks clear
            consider(o.s + bonus(true), yawE, 0, 0, ti);
            break;
        }
        case Kind::Flood:  // W4M CAIPlanAttackFlood 0x4a3640: every target under Water.Level + Flood.Delta scores a kill (damage 1000, no knock)
            if (k != 0 || !pick()) break;  // one plan, whatever the target
            {
                Outcome o(g, L, me, rating);
                float level = fminf(g.water + wd.speed, Terrain::WATER + 15);
                for (size_t i = 0; i < g.worms.size(); i++)
                    if (g.worms[i].alive && g.worms[i].pos.y < level) o.hurt((int)i, 1000, 0);
                consider(o.s + bonus(false), w.yaw, w.pitch, 0, ti);
            }
            break;
        case Kind::Airstrike:  // W4M CAIPlanAttackStrike 0x4a11d0: one blast at the target (set from the Blimp, act()), radius
        case Kind::Donkey:     // BlitzDuration 2 s x GroundSpeed + 2 WormDamageRadius, the payload's WormDamageMagnitude
            if (isWorm && pick()) {
                Blast b = blastOf(wd, wd.kind == Kind::Airstrike);
                b.reach = 2 * Game::BOMBER_SPEED + 2 * b.reach;
                Outcome oc(g, L, me, rating);
                oc.blast(e, b, L.threat);
                consider(oc.s + bonus(false), w.yaw, w.pitch, 0, ti);  // W4M strike plans queue no worm orientation (flags 0x170)
            }
            break;
        default: break;  // sentry, abduction, bubble, girder, teleport and utilities: W4M has no AI plan for them (no CAIPlanAttack*)
        }
    }
    return cands;
}

// W4M think start 0x49af70: CheckPlanResult 0x4a6ab0 (the last attack failed if its target kept its hp and place), then
// RegressFailedMemory 0x4a5b10 (x0.99, 0 once the target changed) and RegressImproveAccuracyMemory 0x4a6080 (x0.95); < 0.1 is dropped
void Ai::regress(const Game &g) {
    auto same = [&](int t, Vector3 at, int hp) { const Worm &x = g.worms[t]; return x.hp == hp && x.pos.x == at.x && x.pos.y == at.y && x.pos.z == at.z; };
    if (recent.weapon >= 0 && same(recent.target, recent.at, recent.hp)) failed.push_back(recent);
    recent.weapon = -1;
    for (Fail &f : failed) f.effect = same(f.target, f.at, f.hp) ? f.effect * 0.99f : 0;
    failed.erase(std::remove_if(failed.begin(), failed.end(), [](const Fail &f) { return f.effect < 0.1f; }), failed.end());
    for (Shot &s : memory) s.effect *= 0.95f;
    memory.erase(std::remove_if(memory.begin(), memory.end(), [](const Shot &s) { return s.effect < 0.1f; }), memory.end());
}

// Shot is ready: W4M 0x4a4580 scales each launch-velocity component by 1 ± ShotError (game-derived seed: same state, same shot),
// divided by 1 + MemoryImproveAccuracyEffect · the match with earlier shots (0x4a5d00).
void Ai::finish(const Game &g) {
    const Worm &w = g.worms[g.current];
    const Level &L = levelOf(g, w.team);
    const Kind k = WEAPONS[plan.weapon].kind;
    uint32_t r = (salt ^ (uint32_t)g.shotsLeft * 2654435761u) + (uint32_t)walks * 40503u;  // turn-start state: not the think's length
    Vector3 at = plan.target >= 0 ? g.worms[plan.target].pos : w.pos;
    float match = 0;  // W4M 0x4a5640: effect x (1 - d_from / R)(1 - d_at / R) within MatchRadius R 200 units, any worm's records
    for (const Shot &s : memory) {
        float a = Vector3Distance(s.from, w.pos), b = Vector3Distance(s.at, at);
        if (a < 10 && b < 10) match += s.effect * (1 - a / 10) * (1 - b / 10);
    }
    memory.push_back({w.pos, at, 1});
    if (plan.target >= 0) recent = {plan.weapon, g.current, plan.target, at, g.worms[plan.target].hp, 1};  // StorePlanAttackMemory 0x4a6360
    const float recall = 1 + L.memory * match;
    strikeOff = {};
    if (k == Kind::Airstrike || k == Kind::Donkey) {  // W4M 0x4a1b00: target + ShotErrorStrike·(2r-1) per component, no accuracy memory
        const float ex = noise(r) * L.strikeErr;
        noise(r);  // the y offset: bombs fall straight down
        strikeOff = {ex, 0, noise(r) * L.strikeErr};
    }
    else if (k != Kind::Melee && k != Kind::Mine && k != Kind::Flood && !dropped(WEAPONS[plan.weapon])) {  // direct weapons aim statically: ShotErrorDirectNonStrafe
        float e = (k == Kind::Shotgun ? L.directErr : L.shotErr) / recall;
        Vector3 v = dirOf(plan.yaw, plan.pitch) * (powered(k) ? plan.charge / 90.0f : 1);
        v = {v.x * (1 + e * noise(r)), v.y * (1 + e * noise(r)), v.z * (1 + e * noise(r))};
        float sp = Vector3Length(v);
        plan.yaw = atan2f(v.x, v.z);
        plan.pitch = Clamp(asinf(Clamp(v.y / sp, -1, 1)), -1.2f, 1.45f);
        if (powered(k)) plan.charge = (int)Clamp(roundf(sp * 90), 1, 90);
    }
    mode = Mode::Act;
    charged = aimed = 0;
    if (!g.shotsLeft && !w.nailed) {  // W4M plans the retreat with the attack ("move to ..., retreat to ..."), from the firing pose
        lastBoom = dropped(WEAPONS[plan.weapon]) || k == Kind::Mine || plan.target < 0 ? w.pos : g.worms[plan.target].pos;
        path.clear();
        startSearch(g, Search::Retreat, {});
        search->root.yaw = plan.yaw;
    }
}

// W4M CAIPlanMove: a crate beats the shot when it scores more; else walk closer, else the best shot. W4M creates path moves only
// from A* edges (walk, jumps: 0x4b0c3c), the super sheep (0x4a3405) and melee (0x4a2adf): never JETPACK, PARACHUTE or NINJA_ROPE.
void Ai::decide(const Game &g) {
    const Worm &w = g.worms[g.current];
    const int team = w.team;
    const Level &L = levelOf(g, team);
    const float left = thinkTimer * DT;  // the think's start: the plan must not depend on how it was sliced
    const bool canMove = walks < 3 && !w.nailed && left > 10;  // ForbidMoveIfWouldLeaveTimeLessThan 10 s
    if (canMove && !crateTried) {  // W4M CAIPlanCollectCrate
        crateTried = true;
        const Object *crate = nullptr;
        float best = 0;
        for (const Object &o : g.objects) {
            Vector3 d = o.pos - w.pos;
            if (o.type != Object::Crate || o.falling || fabsf(d.y) >= 3 || sqrtf(d.x * d.x + d.z * d.z) >= 18) continue;
            // AddScoreCollectSomething, health ×3 when poisoned or low; ×R/d past ReduceMoveScoreFurtherThan, ×t/T under its time
            float s = L.collect * (o.weapon < 0 && (w.poison > 0 || w.hp < L.lowHp) ? 3 : 1), dist = Vector3Length(d);
            if (dist > L.moveFar) s *= L.moveFar / dist;
            if (left < L.moveTime) s *= left / L.moveTime;
            if (s > best) best = s, crate = &o;
        }
        if (crate && best > plan.rank) return startSearch(g, Search::Crate, crate->pos);
    }
    if (plan.score >= 5) return finish(g);
    if (canMove && walks < 2 && plan.target >= 0 && !closerTried) { closerTried = true; return startSearch(g, Search::Closer, g.worms[plan.target].pos); }  // CAIPlanMoveCloserToTarget
    if (plan.score > 0) return finish(g);  // W4M 0x49e6d0: a negative plan is forbidden, the turn is skipped
    int skip = owned(g, team, Kind::SkipGo);
    if (skip >= 0) { plan = Plan{skip, 0, -1}; plan.yaw = w.yaw; plan.pitch = w.pitch; mode = Mode::Act; }
}

static bool select(const Game &g, int wi, Input &in) {  // W4M: the CPU picks its weapon directly
    if (g.weapon == wi || g.shotsLeft) return true;
    in = Input::pick(wi);
    return false;
}

Input Ai::act(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    if (plan.weapon < 0) return in;
    if (g.shotsLeft && g.shotsLeft != shotsSeen) {  // shotgun: re-aim the next shot at where things are now
        shotsSeen = g.shotsLeft;
        Plan keep = plan;
        plan = Plan{};
        plan.target = keep.target;
        evalWeapon(g, g.weapon);
        if (plan.weapon < 0) plan = keep;
        else { finish(g); }
        return in;
    }
    if (!select(g, plan.weapon, in)) return in;
    Kind k = WEAPONS[g.weapon].kind;
    float dy = wrapPi(plan.yaw - w.yaw), dp = plan.pitch - w.pitch;
    in.turn = q(dy / (2.5f * DT));
    in.aim = q(dp / (1.5f * DT));
    if (fabsf(dy) > 2e-3f || fabsf(dp) > 2e-3f) return in;
    if (aimed++ < levelOf(g, w.team).fireDelay / DT) return in;  // W4M DelayBeforeFire
    if (blimped(k) && !g.locked) return blimp(g);
    if (powered(k)) { if (k == Kind::Homing && !charged && g.prevButtons) return in; if (charged++ < plan.charge) in.buttons = Input::FIRE; }  // release fires
    else if (!g.prevButtons) in.buttons = Input::FIRE;
    return in;
}

// W4M SetStrikeTarget 0x4b4c70 sets the strike or homing target with the Blimp camera on (0x4b4c99); ours drives the Blimp
// cursor until its camera ray meets the target's feet, then FIRE confirms (a strike fires, a homing missile locks).
Input Ai::blimp(const Game &g) const {
    Input in;
    in.buttons = Input::TARGET;
    if (!g.cursorOn || plan.target < 0) return in;  // the first TARGET tick opens the view
    const Vector3 t = g.worms[plan.target].pos + strikeOff - Vector3{0, R, 0};
    const float y = g.cursorYaw, back = (g.cursor.y - t.y) / tanf(g.cursorPitch), dx = t.x - sinf(y) * back - g.cursor.x,
                dz = t.z - cosf(y) * back - g.cursor.z, step = Game::CURSOR_SPEED * DT;
    in.walk = q((sinf(y) * dx + cosf(y) * dz) / step);  // Game::step's cursor axes
    in.aim = q((sinf(y) * dz - cosf(y) * dx) / step);
    if (dx * dx + dz * dz < 0.05f * 0.05f && !(g.prevButtons & Input::FIRE)) in.buttons |= Input::FIRE;
    return in;
}

// Retreat: the path planned with the shot, after the 1 s pause (0x49e6d0), while the shot flies too (W4M).
Input Ai::retreat(const Game &g) {
    Input in;
    if (search) { slice(g); return in; }  // a repath in progress
    if (++afterFire >= 60 && g.retreating() && !follow(g, in) && pathFailed) repath(g);
    return in;
}

Input Ai::think(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) lastBoom = e.pos;
    if (g.phase == Phase::Flying || (g.phase == Phase::Settle && !g.shots.empty())) {  // sheep: detonate next to an enemy; super sheep: autopilot
        if (g.phase == Phase::Flying && w.alive) in = retreat(g);
        for (const Projectile &s : g.shots) {
            Kind k = WEAPONS[s.weapon].kind;
            if (k == Kind::Airstrike && WEAPONS[s.weapon].fuse > 0 && !s.child) {  // bomber: head for the target, drop with the lead
                if (plan.target < 0 || !g.worms[plan.target].alive || (!s.prey && s.stage > 0)) continue;  // held for STRIKE_LEAD
                Vector3 e = g.worms[plan.target].pos + strikeOff, land = s.pos + s.vel * (0.3f * (s.pos.y - e.y) / Game::COW_CHUTE);
                in.turn = q(wrapPi(yawTo(s.pos, e) - atan2f(s.vel.x, s.vel.z)) / (0.8f * DT));
                if (Vector2Distance({land.x, land.z}, {e.x, e.z}) < 1.5f && !g.prevButtons) in.buttons = Input::FIRE;
                continue;
            }
            // W4M queues DetonateWhenGoingAwayFrom for the Sheep only (flag 0x800, 0x49d848): no AI press for the Old Woman or Scouser
            if (s.child || (k != Kind::Sheep && k != Kind::SuperSheep) || plan.target < 0) continue;
            const Vector3 e = g.worms[plan.target].pos, n = s.pos + s.vel * DT;
            bool near = k == Kind::SuperSheep && WEAPONS[s.weapon].walks && !s.stage;  // take off at once
            if (k == Kind::SuperSheep)
                for (const Worm &x : g.worms) near |= x.alive && x.team != w.team && Vector3Distance(s.pos, x.pos) < 1.5f;
            else near = Vector2Distance({n.x, n.z}, {e.x, e.z}) > Vector2Distance({s.pos.x, s.pos.z}, {e.x, e.z});  // 0x57e130: the next step goes away
            if (k == Kind::SuperSheep && g.worms[plan.target].alive) steer(g, s.pos, s.vel, e, in);
            if (near && !g.prevButtons) in.buttons = Input::FIRE;
        }
        return in;
    }
    if (g.phase == Phase::Settle) mode = Mode::Eval;  // the turn is over: striking() must not show last turn's plan
    if (!w.alive) return in;
    if (g.phase == Phase::Retreat) return retreat(g);
    if (g.phase != Phase::Aim) return in;
    if (g.timer > lastTimer || g.current != worm) {
        worm = g.current;
        walks = charged = aimed = wait = afterFire = shotsSeen = 0;
        if (g.clock < lastClock) memory.clear(), failed.clear(), recent.weapon = -1, grid.reset();  // new match
        lastClock = g.clock;
        if (!grid) grid = std::make_shared<const Grid>(makeGrid(g.terrain));
        crateTried = closerTried = pathFailed = afterCloser = false, repaths = 0, lastPurpose = -1, blocked.clear();
        mode = Mode::Eval;
        stage = -1;
        search.reset();
        path.clear();
        debt = 0;
        run = RopeRun{};
        raceAt = -1;
        lastBoom = w.pos;
        uint32_t hp = 0;  // not g.clock: how long earlier thinks took must not change this turn's taste
        for (const Worm &x : g.worms) hp = hp * 31 + (uint32_t)x.hp;
        salt = (g.rng ^ hp * 2654435761u) + (uint32_t)g.current;
    }
    lastTimer = g.timer;
    if (wait > 0) { wait--; return in; }  // W4M DelayAtStart is 0: no pause before thinking
    if (g.cfg.rules & RULE_ROPE_RACE) return race(g);
    if (g.jetting) return in;
    if (g.roped) { in.buttons = g.prevButtons ? 0 : Input::JUMP; return in; }
    switch (mode) {
    case Mode::Eval:
    case Mode::Search:
        if (!w.grounded || Vector3LengthSqr(w.vel) > 0.01f) return in;  // wait to stand still
        if (mode == Mode::Eval && stage < 0) startEval(g);
        if (mode == Mode::Eval && tpos.empty()) return in;
        slice(g);
        return in;
    case Mode::Walk:
        if (follow(g, in)) return in;
        if (pathFailed && repath(g)) return in;
        wait = 30, mode = Mode::Eval, stage = -1, crateTried = closerTried = false;  // W4M: 0.5 s after a path, then think again
        afterCloser = lastPurpose == Search::Closer;
        return in;
    case Mode::Act:
        if (search) { slice(g); return in; }  // the retreat path, before firing
        return act(g);
    }
    return in;
}

#include "ai.h"
#include "navgrid.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

static constexpr float DT = Game::DT, R = Game::R, UNIT = 20;  // UNIT: W4M units per m

static int8_t q(float v) { return (int8_t)Clamp(roundf(v * 127), -127, 127); }

// W4M AITWK.XOM AIParams.CPU1..CPU5 (docs/w4m/ai.md §18); distances at 20 W4M units per game unit.
struct Level {
    float shotErr, directErr, strikeErr, exchange, secondary, threat, nearby, randomise, humans, fireDelay, nonFirstMove, collect, lowHp,
        moveFar, moveTime, memory;
    bool jump;
    float cluster, gas, homing;
    bool flip;             // MovementJumpBackflipAllowed
    float jumpErr, sweet;  // MovementJumpError; ProjectileSweetSpotDistance in m
    bool thrust;           // ConsidersStrikeThrustDirection
    float others;          // WeightStrikeSecondaryTarget
    float vital;           // WeightingWormVital
};
static const Level LEVELS[5] = {
    {0.3f, 0.05f, 1, 0.05f, 0, 0, 1.5f, 0.2f, 0.8f, 0.5f, 2, 1000, 25, 5, 40, 0.2f, false, 1, 1, 0.9f, false, 0, 0, false, 0, 1},
    {0.2f, 0.02f, 0.5f, 0.1f, 0.5f, 1, 1, 0.2f, 1, 0.5f, 0, 5000, 25, 10, 30, 1, true, 1, 1, 0.9f, false, 0.2f, 0.25f, false, 0.3f, 1.5f},
    {0.1f, 0.01f, 0.25f, 0.1f, 0.5f, 1, 0.5f, 0.1f, 1, 0.5f, 0, 20000, 25, 5, 30, 1, true, 1, 1, 0.9f, false, 0.1f, 0.25f, true, 5, 3},
    {0.05f, 0.005f, 0.25f, 0.5f, 0.8f, 2, 0.1f, 0.2f, 1.2f, 0.5f, 0, 30000, 25, 5, 20, 1.5f, true, 0.8f, 0.5f, 0.8f, true, 0.05f, 0.5f, true, 1, 5},
    {0, 0, 0, 0.5f, 1, 4, 0, 0, 1.8f, 0.2f, 0, 60000, 50, 5, 20, 1, true, 0.3f, 0.5f, 0.7f, true, 0, 0.25f, true, 1, 1000},
};
// Shared by all five levels (AITWK): AddScoreMove, AddScoreMoveIfNotMoved, RandomSmallMoveRange 100 units, StrikeSweetSpotDistance 2 units
static constexpr float MOVE_BONUS = 10, FIRST_MOVE_BONUS = 20, SMALL_MOVE = 5, STRIKE_SWEET = 0.1f;
// WeightingWormNearbyWorms and WeightingWormMilesAway: the same in all five (AITWK), read by 0x4a9c50
static constexpr float NEARBY_WORMS = 0.02f, MILES_AWAY = 0.1f;

// The thinking worm's AIParams.WormNN (W4M per worm), else its team's level; a human team played by the AI is CPU5, copied over
// its worms' params as /ALLAIPLAYERS does (0x4b39b4)
static const Level &levelOf(const Game &g) {
    int team = g.worms[g.current].team, t = team < (int)g.cfg.teamSetup.size() ? g.cfg.teamSetup[team].cpu : 0;
    int l = !t ? 5 : g.current < (int)g.wormCpu.size() && g.wormCpu[g.current] ? g.wormCpu[g.current] : t;
    return LEVELS[std::min(l, 5) - 1];
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
struct Body { Vector3 pos, vel; bool grounded; Motion motion{}; };
static const Colliders &wormColliders(const Game &g) {  // Game::stepWorm's: the active worm's sphere against the others
    static thread_local Colliders cs;
    g.colliders(cs, Game::WORM_MASK, g.current);
    return cs;
}

static bool stepBody(const Game &g, Body &b, float &yaw) {
    float speed = wormBody(g.terrain, b.pos, b.vel, b.grounded, b.motion, yaw, g.gravity(), g.pot, 0.3f, g.wormWind(), &wormColliders(g));
    g.landDamage(b.motion, speed);  // spends a 0x40 flag on the landing, as the sim does
    return b.pos.y >= g.water;
}

// W4M 0x4a46f0: a target is an active worm of a team that has not surrendered
static bool targeted(const Game &g, const Worm &x) { return x.alive && !g.surrendered[x.team]; }

// W4M c_plstTargets after the worms (0x4ab730, 0x4a5070): the thinking worm's TargetDetailObject markers, value 1, hp 1 (any hit is lethal),
// no threat rating
static const std::vector<Vector3> &scripted(const Game &g) {
    static const std::vector<Vector3> none;
    return g.current < (int)g.wormTarget.size() ? g.wormTarget[g.current] : none;
}
static size_t targetCount(const Game &g) { return g.worms.size() + scripted(g).size(); }
static Vector3 targetPos(const Game &g, size_t i) { return i < g.worms.size() ? g.worms[i].pos : scripted(g)[i - g.worms.size()]; }
static int targetHp(const Game &g, size_t i) { return i < g.worms.size() ? g.worms[i].hp : 1; }
static bool targetAlive(const Game &g, size_t i) { return i >= g.worms.size() || g.worms[i].alive; }

static const Game::AiTweak &tweakOf(const Game &g, int i) {
    static const Game::AiTweak none;
    return i < (int)g.wormTweak.size() ? g.wormTweak[i] : none;
}

// W4M 0x4ab730 + 0x4a9260: a target of the thinker (me) is dropped beyond its MaximumDistanceTargetConsidered, and with
// ForbidShotsWhenMightAffectAITrigger 1 inside an AffectsAI trigger (3D, d <= radius)
static bool considered(const Game &g, int me, int i) {
    const Game::AiTweak &t = tweakOf(g, me);
    const Vector3 p = g.worms[i].pos;
    if (Vector3DistanceSqr(p, g.worms[me].pos) > t.reach * t.reach) return false;
    if (t.forbid) for (const Game::Trigger &x : g.triggers) if (x.affectsAI && Vector3DistanceSqr(p, x.pos) <= x.radius * x.radius) return false;
    return true;
}

// W4M 0x4a9260 target value, once per think (0x4ab730) from where the worm stands: v = Vital · (1 + 0.04·hp) + max(0.1, 1 − 0.08·poison); allies (self
// included; the same AlliedGroup, 0x4a4790) are −v·K, enemies v/K, K = (enemies / allies)^WormExchange, and both side counts are 1 without a worm on
// each side (0x4a9b20); then ×LastInTeam when its side has 1 left, ×nearby, ×the target's ThisWormValue, ×PreferAttackHumans when v > 0 and human
static float targetValue(const Game &g, const Level &L, int me, int i) {
    const Worm &w = g.worms[i];
    const int team = g.worms[me].team;
    int foes = 0, friends = 0;
    for (const Worm &x : g.worms) if (targeted(g, x)) (g.alliance(x.team) == g.alliance(team) ? friends : foes)++;
    float K = 1;
    if (foes && friends) K = powf((float)foes / friends, L.exchange);
    else foes = friends = 1;
    const bool ally = g.alliance(w.team) == g.alliance(team);
    float v = (g.vital(i) ? L.vital : 1) * (1 + 0.04f * w.hp) + fmaxf(0.1f, 1 - 0.08f * w.poison);
    v = ally ? -v * K : v / K;
    if ((ally ? friends : foes) == 1) v *= 2;
    v *= powf(10 / fmaxf(Vector3Distance(w.pos, g.worms[me].pos), 10), L.nearby) * tweakOf(g, i).value;
    const bool human = w.team >= (int)g.cfg.teamSetup.size() || !g.cfg.teamSetup[w.team].cpu;
    return v > 0 && human ? v * L.humans : v;
}

// W4M 0x4a9c50 DoValueSecondPass, on the first-pass values: a target moves by −(1 − d/100 units)·v·WormNearbyWorms for each target of the other side
// (value of the other sign) within 100 units, and by WormMilesAway·v when none is within 1000 units
static void secondPass(const Game &g, const std::vector<bool> &listed, std::vector<float> &vals) {
    std::vector<float> adj(vals.size(), 0);
    for (size_t i = 0; i < vals.size(); i++) {
        if (!listed[i]) continue;
        float nearest = INFINITY;
        for (size_t j = 0; j < vals.size(); j++) {
            if (j == i || !listed[j] || (vals[i] > 0) == (vals[j] > 0)) continue;
            const float d = Vector3Distance(targetPos(g, j), targetPos(g, i)) * 20;
            nearest = fminf(nearest, d);
            if (d < 100) adj[i] -= (1 - d / 100) * vals[j] * NEARBY_WORMS;
        }
        if (nearest >= 1000) adj[i] += MILES_AWAY * vals[i];
    }
    for (size_t i = 0; i < vals.size(); i++) vals[i] += adj[i];
}

// W4M plan scoring: 0x49f190 for a blast, 0x49ed30 for each target it hurts. Every target counts, self included, where it stands.
struct Outcome {
    const Game &g;
    const Level &L;
    const std::vector<float> &rating, &vals;  // W4M target threat rating (+0x1c) and value (+8)
    int self;
    float s = 0;
    bool started = false;

    Outcome(const Game &g, const Level &L, int me, const std::vector<float> &rating, const std::vector<float> &vals) : g(g), L(L), rating(rating), vals(vals), self(me) {}

    // W4M 0x49ed30: d hp on worm i; armour unless melee; a lethal hit is max(hp, KillTarget 200), else the knock weight `a`
    // pulls d toward hp by the threat rating t and scales WeightingAttack 2 by 1 + a·t
    void hurt(int i, float d, float a, bool melee = false) {
        if (!targetAlive(g, i)) return;
        const int hp = targetHp(g, i);
        if (i < (int)g.worms.size() && g.worms[i].armour && !melee) d = (float)((int)d * Game::ARMOUR / 100);
        float attack = 2;
        if (hp <= d) d = fmaxf((float)hp, 200);
        else if (a != 0) {
            const float t = rating[i];
            attack *= 1 + t * a;
            d = fminf((float)hp, d + a * t * (hp - d));
        }
        s += d * attack * vals[i];
    }
    // W4M 0x49f190: (1 - dist / WormDamageRadius) · WeightingExplosiveSecondaryDamage · WormDamageMagnitude (x2 doubled) per worm
    void blast(Vector3 p, const Blast &b, float a, bool skipSelf = false) {
        started = true;
        if (b.reach <= 0) return;
        for (size_t i = 0; i < targetCount(g); i++) {
            Vector3 t = targetPos(g, i);
            if (i < g.worms.size()) t.y -= Game::R;  // W4M target position = the worm's feet (+0x38)
            const float d = Vector3Distance(t, p), k = d < b.reach ? (1 - d / b.reach) * L.secondary : 0;
            if (k > 0 && !(skipSelf && (int)i == self)) hurt((int)i, k * b.damage * (g.doubled() ? 2 : 1), a);
        }
    }
};

// Point copy of Game::stepShots for a ballistic or homing (aim != null) projectile; false when lost.
// first: stop at the first contact (land, worm, fuse or water) and give its tick, as W4M FindFirstEvent (0x576580)
static bool fly(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 v, float wind, bool child, Vector3 &out, const Vector3 *aim = nullptr, int *first = nullptr) {
    bool impact = child || wd.fuse == 0;
    float fuse = aim ? 0 : g.fuseOf(wd);  // the team's current fuse: the AI never changes it
    uint64_t touching = ~0ull;
    for (int i = 0; i < 600; i++) {
        uint64_t now = 0;
        const Vector3 v0 = v;
        auto at = [&](bool r) { if (first) *first = i + 1; return r; };
        if (aim && (fuse += DT) > Game::HOMING_LOCK && fuse < Game::HOMING_LOCK + Game::HOMING_TIME) v = Game::homingStep(v, p, *aim);
        else v.y -= g.gravity() * (child && wd.kind != Kind::Airstrike ? 1 : wd.grav) * DT;
        if (g.windy(int(&wd - WEAPONS.data()))) v.x += wind * Game::WIND_ACCEL * DT, v.z += g.windZ * Game::WIND_ACCEL * DT;
        const Vector3 d = arcMove(v0, v, arcLag(wd));
        for (int k = 0, n = substeps(v); k < n; k++) {  // Game::stepShots' sub-steps
            Vector3 np = p + d * (1.0f / n);
            out = np;
            if (g.terrain.solid(np)) {
                if (impact || first) return at(true);
                v = payBounce(v, g.terrain.normal(np), child ? wd.cdamp : wd.damp, true);
                break;
            }
            p = np;
            uint64_t m = touches(g, np);
            now |= m;
            if ((impact || first) && wd.kind != Kind::Donkey && (m & ~touching)) return at(true);
        }
        touching = now;
        if (!impact && (!wd.restFuse || fuse < wd.fuse || Vector3Length(v) < 1) && (fuse -= DT) < DT / 2) return at(true);
        if (outside(g, p)) return at(false);
    }
    return false;
}

int Ai::firstContact(const Game &g, const Projectile &s) {
    int n = -1;
    Vector3 out;
    fly(g, WEAPONS[s.weapon], s.pos, s.vel, g.wind, s.child, out, nullptr, &n);
    return n;
}

// Copy of the sheep/old woman walk up to step `to`: its closest approach to `e` so far.
static void sheepWalk(const Game &g, const WeaponDef &wd, Ai::Walk &k, Vector3 e, int to) {
    for (; k.i < to && k.i * DT < wd.fuse && k.p.y > g.water - 2; k.i++) {
        walkerStep(g.terrain, k.p, k.v, g.gravity());
        if (Vector3Distance(k.p, e) < Vector3Distance(k.best, e)) k.best = k.p;
    }
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
    Vector3 d = dirOf(yaw, wd.walks ? Game::SHEEP_TAKEOFF : pitch), p = muzzle(g.terrain, pos, launchPoint(wd, pos, yaw));
    const bool star = !wd.walks;  // Starburst: still through its fuse, then accelerates (Game::starSpeed)
    float sp = star ? 0 : wd.speed;
    uint64_t touching = ~0ull;
    for (float t = wd.fuse; t > 0; t -= DT) {  // walks: takes off at once (think() presses FIRE)
        Input in;
        steer(g, p, d, e, in);
        bool lit = star && wd.fuse - t < Game::STAR_FUSE - DT / 2, det = !lit && Vector3Distance(p, e) < 1.5f;
        float yw = atan2f(d.x, d.z) + in.turn / 127.0f * 2 * DT;
        float pt = Clamp(asinf(Clamp(d.y, -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
        d = dirOf(yw, pt);
        Vector3 v = d * sp;
        if (det) { out = p; return true; }
        uint64_t now = 0;
        for (int k = 0, n = substeps(v); k < n; k++) {  // Game::stepShots' sub-steps
            out = p = p + v * (DT / n);
            uint64_t m = touches(g, out);
            now |= m;
            if (g.terrain.solid(out) || (m & ~touching)) return true;
        }
        touching = now;
        if (star && wd.fuse - (t - DT) >= Game::STAR_FUSE - DT / 2) sp = Game::starSpeed(sp, wd.speed, p.y < g.water);
        if (outside(g, p)) return false;
    }
    return true;
}

static bool ground(const Game &g, Vector3 p, Vector3 &hit) {
    return g.terrain.raycast({p + Vector3{0, 3, 0}, {0, -1, 0}}, 14, &hit);
}

// --- the walking mover: an exact copy of Game::step for the active worm ---

struct Mover { Body b; float yaw, pitch; bool roped; Rope rope; uint8_t prev; int jump = 0; uint8_t kind = 0; Vault vault{}; Hook hook{}; int walkTick = 0; };

static Vector3 feetOf(const Body &b) { return {b.pos.x, b.pos.y - R, b.pos.z}; }

static bool stepRope(const Game &g, Mover &m, const Input &in) {  // Game::step's ropeTick on the feet
    Vector3 feet = feetOf(m.b);
    g.ropeTick(m.rope, feet, m.b.vel, Game::ropeSwing(in, m.yaw), in.aim, g.current);
    m.b.pos = {feet.x, feet.y + R, feet.z};
    return m.b.pos.y >= g.water;
}

static bool move(const Game &g, Mover &m, const Input &in, float ropeMax) {
    uint8_t pressed = in.buttons & ~m.prev;
    m.prev = in.buttons;
    Body &b = m.b;
    bool tool = m.roped;
    if (!m.vault.t && !m.jump && !m.roped) m.yaw += in.turn / 127.0f * 2.5f * DT;
    const float ws = Game::WALK_SPEED * g.walkScale();
    const int wt = m.walkTick;
    m.walkTick = 0;
    if (m.vault.t) vaultStep(b.pos, m.vault, flat(m.yaw) * (float)in.walk);  // Game::step's vault
    else if (b.grounded && !b.motion.slide && in.walk && !m.jump) {  // Game::step's walk
        Vector3 walkV = flat(m.yaw) * (in.walk / 127.0f * Game::INPUT_IMPULSE), gn;
        if (!Game::walkFrame(m.walkTick = wt + 1)) {
        } else if (walkStep(g.terrain, b.pos, m.yaw, in.walk / 127.0f * ws * Game::WALK_FRAME, &m.vault, &gn, &wormColliders(g), &b.motion)) b.vel = walkV, b.motion.air = true;
        else if (!m.vault.t) slideIfSteep(gn, b.vel, b.motion, walkV, g.pot);
    }
    b.motion.input = flat(m.yaw) * (in.walk / 127.0f);
    Vector3 jv;  // Game::step's jump
    if ((pressed & Input::JUMP) && !m.jump && !m.vault.t && b.grounded && !b.motion.slide && !tool && !g.wp(WP_NO_JUMPING)) m.jump = Game::JUMP_WINDOW, m.kind = 2;
    else if (m.jump && Game::jumpTick(m.jump, m.kind, in, pressed, m.yaw, jv) && b.grounded) b.vel = jv, b.grounded = false, b.motion.air = true;
    if (m.roped) {
        if (pressed & Input::JUMP) m.roped = false, b.vel = g.ropeRelease(m.rope, feetOf(b), Game::ropeSwing(in, m.yaw));
    } else {
        m.pitch = Clamp(m.pitch + in.aim / 127.0f * 1.5f * DT, -1.2f, 1.45f);
        if (pressed & Input::FIRE) m.hook = m.hook.on ? Hook{} : Game::grappleFire(feetOf(b), m.yaw, m.pitch, b.vel, m.rope.swung);
    }
    if (m.vault.t && (m.roped || Vector3LengthSqr(b.vel) > 0)) b.pos = m.vault.to, m.vault.t = 0;
    if (m.roped) b.motion.slide = false;
    bool ok = m.roped ? stepRope(g, m, in) : m.vault.t ? true : stepBody(g, b, m.yaw);
    if (m.hook.on) {  // Game::step's grapple, objects aside
        Vector3 hit;
        int r = g.grappleStep(m.hook, feetOf(b), ropeMax, false, &hit, nullptr);
        m.hook.on = r == 1;
        if (r == 2) m.roped = true, b.grounded = false, b.motion.slide = false, g.ropeHang(m.rope, hit, feetOf(b), b.vel, m.yaw);
    }
    if (!m.roped && !m.hook.on && (b.grounded || m.vault.t)) m.rope.swung = false;
    return ok;
}

// --- paths: W4M AIPathManager A* (0x492d80) over a node grid; each edge is played with the sim's own walk and jump code ---

static constexpr int MAX_ITER = 200;  // W4M m_nMaxNumIterations, every pathfind (0x4b0ba6)
static int octile(int dx, int dz) { dx = std::abs(dx), dz = std::abs(dz); return 10 * std::max(dx, dz) + 4 * std::min(dx, dz); }  // W4M 0x4923a9

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
        std::vector<std::pair<double, int>> cells;  // (distance, cell) by distance: a step only visits the cells its arc crosses
        for (int b = 0; b < N; b++)
            for (int a = 0; a < N; a++) cells.push_back({sqrt((double)(a * a + b * b)) * s, N * b + a});
        std::sort(cells.begin(), cells.end());
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
                const double lo = fmin(px[i], x[i]) - 1e-6, hi = fmax(px[i], x[i]) + 1e-6;
                for (auto k = std::lower_bound(cells.begin(), cells.end(), std::make_pair(lo, -1)); k != cells.end() && k->first <= hi; ++k) {
                    const double r = k->first, f = (r - px[i]) / (x[i] - px[i]);
                    if (!(f > 0 && f <= 1)) continue;
                    Grid::Entry &c = e[k->second];
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

static Grid aiGrid(const Terrain &t) {
    Grid gr = makeGrid(t);
    buildReach(gr);
    return gr;
}

// W4M 0x4ade10: a node's heights from 3 x 3 rays down at ±spacing/3 around it from `from`: lo / hi = lowest / highest hit; false
// (water, blocked) when a ray finds no land or lands in the water.
static bool probe(const Game &g, const Grid &gr, NodeCache &c, int i, int j, float from, float &lo, float &hi) {
    const Vector2 p = gr.at(i, j);
    lo = 1e9f, hi = -1e9f;
    for (int a = -1; a <= 1; a++)
        for (int b = -1; b <= 1; b++) {
            const float y = rayDown(g.terrain, g.water, c, p.x + a * gr.s / 3, p.y + b * gr.s / 3, from, false);
            if (y <= g.water) return false;
            lo = fminf(lo, y), hi = fmaxf(hi, y);
        }
    return true;
}

static Mover moverOf(const Game &g) {
    const Worm &w = g.worms[g.current];
    return {{w.pos, w.vel, w.grounded, w.motion}, w.yaw, w.pitch, false, Rope{}, g.prevButtons, g.jumpDelay, g.jumpKind, g.vault, Hook{}, g.walkTick};
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
        float h = sqrtf(d.x * d.x + d.z * d.z), dy = wrapPi(atan2f(d.x, d.z) - m.yaw), ws = Game::WALK_SPEED * g.walkScale();
        if (h < 0.05f || !m.b.grounded || r.t > 120) { r.stuck = h >= 0.05f && m.b.grounded; r.air = true; r.done = still; return in; }
        in.turn = q(dy / (2.5f * DT));
        if (fabsf(dy) < 0.3f) in.walk = (int8_t)Clamp(roundf(127 * h / (ws * Game::WALK_FRAME)), 1, 127);
        return in;
    }
    if (m.jump || !m.b.grounded) {  // forward jump pending, or flying
        if (s.move == 2 && m.jump) in.walk = -127, in.buttons = m.prev & Input::JUMP ? 0 : Input::JUMP;  // second press, stick back: backflip
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
        for (float y = p.y - R + Game::STEP + 0.05f; y <= p.y + R; y += Terrain::SUB)
            if (t.solid({p.x + o.x, y, p.z + o.y})) return false;
    return true;
}

// --- movement plans: W4M position scores 0x4aa6a0 over the ScoreAllMoveNodes window 0x4ab490, move -> retreat pairs 0x4a8c60 ---


// W4M ScoreAllMoveNodes 0x4ab490: the 21 x 21 window at every other node around the worm's (±20 nodes), both layers. Per entry: def
// (0x4aa6a0 with arg5 1, the retreat spot score), open (arg6 1, the move spot score) and the retreat 0x4a8c60 paired it with.
struct Moves : NodeCache {
    struct Entry { float def = -1e6f, open = -1e6f; int r = -1; };
    int i0 = 0, j0 = 0, l0 = 0, at = 0, warm = 0;  // the worm's node; next entry to score; layer-0 rows probed ahead
    Entry e[2 * 21 * 21];                // [layer][row][col]: node (i0 + 2 (row - 10), j0 + 2 (col - 10), layer)
    struct Jump { bool ok; Vector3 land; unsigned long work; };
    std::unordered_map<int64_t, Jump> jumps;  // jumpLand per (node, jump, heading); work: its Terrain::samples
    unsigned edits = 0;                       // Terrain::edits the jumps were found with
    int i(int k) const { return i0 + 2 * (k % 441 / 21 - 10); }
    int j(int k) const { return j0 + 2 * (k % 21 - 10); }
};

static NodeH nodeH(const Game &g, const Grid &gr, Moves &mv, int i, int j, int layer) { return nodeH(g.terrain, g.water, gr, mv, i, j, layer); }

// W4M node position (0x4aeb00): mid-height, here the worm's centre over it
static Vector3 nodePos(const Grid &gr, const NodeH &h, int i, int j) { const Vector2 c = gr.at(i, j); return {c.x, (h.lo + h.hi) / 2 + R, c.y}; }
static Vector3 entryPos(const Game &g, const Grid &gr, Moves &mv, int k) { return nodePos(gr, nodeH(g, gr, mv, mv.i(k), mv.j(k), k / 441), mv.i(k), mv.j(k)); }

// W4M 0x4aa6a0 in its units (20 per m): def scores a retreat spot (arg5 1), open a move spot (arg6 1); terms in docs/w4m/ai.md §18
static void posScore(const Game &g, const Grid &gr, Moves &mv, const std::vector<float> &vals, float norm, int me, int i, int j, int layer,
                     float &def, float &open) {
    const NodeH n = nodeH(g, gr, mv, i, j, layer);
    const Vector2 c = gr.at(i, j);
    const Vector3 p = {c.x, (n.lo + n.hi) / 2, c.y};
    float mult = layer ? 5.0f : 1.0f, allies = 0, mines = 0, drums = 0;
    for (size_t k = 0; k < g.worms.size(); k++) {
        if (!g.worms[k].alive || vals[k] >= 0 || (int)k == me) continue;  // 0x4aa78a: only negative (ally) values
        const float d = Vector3Distance(g.worms[k].pos, p) * 20;
        if (d < 100) allies -= (100 - d) * (vals[k] / norm) * 0.01f;
    }
    mult *= expf(logf(0.1f) * allies);
    for (const Object &o : g.objects) {
        const float d = Vector3Distance(o.pos, p) * 20;
        if (d < 100 && o.type == Object::Mine) mines += (100 - d) * 0.01f;
        if (d < 100 && o.type == Object::Barrel) drums += (100 - d) * 0.01f;
    }
    mult *= expf(logf(0.02f) * mines);
    mult *= expf(logf(0.05f) * drums);
    mult *= logf(1.05f * expf((n.hi - g.water) * 20 * 0.01f));  // as W4M writes it, not 1.05^h
    if (g.worms[me].alive && vals[me] < 0) mult *= -vals[me] / norm;
    float a = 0, b = 0;
    for (int di = -12; di <= 12; di++)
        for (int dj = -12; dj <= 12; dj++) {
            const float inv = 1 / sqrtf((float)(di * di + dj * dj));  // the node itself: infinite
            const NodeH q = nodeH(g, gr, mv, i + di, j + dj, 0);
            if (q.flag == 1) a += inv;
            const float diff = (n.hi - q.lo) * 20;
            if (diff > 30) a += 0.2f * diff * inv / 30;
            if (diff < 0) {
                const float k = fminf(1, -0.01f * diff);
                a -= k * inv, b += k * inv;
            }
        }
    a = fmaxf(a, -2.5f);
    def = fminf(mult * 5 / (a + 5), 10), open = fminf(5 / (b + 5), 10);
}

// W4M 0x4a8c60: each scored entry as a move node takes the best def within ±2 entries (both layers) as its retreat; the pair with the
// most retreat def + 0.001 move open wins if it beats entry 0 of the worm's layer (W4M reads the window's corner, not the worm's node).
static bool pairOf(Moves &mv, int &m, int &r, float &score) {
    const Moves::Entry &c = mv.e[mv.l0 * 441];
    float best = c.def + 0.001f * c.open;
    const float first = best;
    score = c.def;
    for (int row = 0; row < 21; row++)
        for (int col = 0; col < 21; col++)
            for (int l = 0; l < 2; l++) {
                Moves::Entry &E = mv.e[l * 441 + row * 21 + col];
                if (E.open == -1e6f) continue;
                float def = -1e6f;
                for (int dr = -2; dr <= 2; dr++)
                    for (int dc = -2; dc <= 2; dc++)
                        for (int l2 = 0; l2 < 2; l2++) {
                            const int rr = row + dr, cc = col + dc, k = l2 * 441 + rr * 21 + cc;
                            if (rr >= 0 && rr <= 20 && cc >= 0 && cc <= 20 && mv.e[k].def > def) def = mv.e[k].def, E.r = k;
                        }
                if (def + 0.001f * E.open > best) best = def + 0.001f * E.open, m = l * 441 + row * 21 + col, r = E.r, score = def;
            }
    return best > first;
}

// W4M 0x4a9030: entries within ±4 of one (its layer) can no longer pair
static void blockAround(Moves &mv, int k) {
    const int l = k / 441, row = k % 441 / 21, col = k % 21;
    for (int rr = std::max(0, row - 4); rr <= std::min(20, row + 4); rr++)
        for (int cc = std::max(0, col - 4); cc <= std::min(20, col + 4); cc++) mv.e[l * 441 + rr * 21 + cc] = {};
}

static float noise(uint32_t &r) { r = r * 1664525u + 1013904223u; return ((r >> 8) / 16777216.0f) * 2 - 1; }

// The node a position stands on: its cell, and of the two layers the one nearer its feet (W4M 0x4aeb70)
static int layerOf(const Game &g, const Grid &gr, Moves &mv, Vector3 p) {
    const int i = gr.ci(p.x), j = gr.cj(p.z);
    const NodeH a = nodeH(g, gr, mv, i, j, 0), b = nodeH(g, gr, mv, i, j, 1);
    return b.flag != 1 && fabsf(p.y - R - b.hi) < fabsf(p.y - R - a.hi);
}

// Where our jump from rest at p lands: the worm's centre under the sim's gravity, its feet, head and front against the land;
// false if it drowns, hits something rising or never comes down (ours: W4M aftertouches to any reach-table cell, 0x4928e1)
static bool jumpLand(const Game &g, Vector3 p, Vector3 v, Vector3 &land) {
    const Vector3 f = Vector3Normalize({v.x, 0, v.z});
    for (int t = 0; t < 400; t++) {
        const Vector3 v0 = v;
        v.y -= g.gravity() * DT;
        const Vector3 d = arcMove(v0, v);  // wormBody's flight: W4M Integrate
        for (int k = 0, n = substeps(v); k < n; k++) {
            const Vector3 np = p + d * (1.0f / n);
            if (g.terrain.solid({np.x, np.y - R, np.z}) || g.terrain.solid({np.x, np.y + R, np.z}) || g.terrain.solid(np + f * R)) {
                land = p;
                return v.y < 0;
            }
            p = np;
        }
        if (p.y < g.water) return false;
    }
    return false;
}

// jumpLand once per think and edge: a repeat charges the same Terrain::samples, so the budget slices exactly as before
static bool jumpFrom(const Game &g, Moves &mv, int64_t key, Vector3 p, Vector3 v, Vector3 &land) {
    if (mv.edits != g.terrain.edits) mv.jumps.clear(), mv.edits = g.terrain.edits;
    if (auto it = mv.jumps.find(key); it != mv.jumps.end()) return Terrain::samples += it->second.work, land = it->second.land, it->second.ok;
    const unsigned long s0 = Terrain::samples;
    land = {};
    const bool ok = jumpLand(g, p, v, land);
    mv.jumps[key] = {ok, land, Terrain::samples - s0};
    return ok;
}

// W4M AIPathManager A* (0x492d80) over the node graph toward one goal node, one node expanded per call: walk edges 0x492510,
// jump edges 0x4928e1 (ours: our one landing per heading), G 10 / 14, jumps + octile + 40 / 60
struct Search {
    std::shared_ptr<const Grid> grid;
    std::shared_ptr<Moves> mv;
    std::vector<int64_t> blocked;  // W4M path-failed blockages (0x490551): nodes no edge may end on
    Ai::Route route;
    Vector3 start{};
    struct Node { int i, j, l; Ai::Step step; int parent, g, h; bool open; };
    std::vector<Node> nodes;
    std::unordered_map<int64_t, int> index;
    int iter = 0, best = 0, gi = 0, gj = 0, gl = 0;  // gi, gj, gl: the goal node
    bool started = false;
    int h(int i, int j) const { return octile(gi - i, gj - j); }  // W4M 0x4923a9
    float top(const Game &g, int i, int j) { return nodeH(g, *grid, *mv, i, j, 0).hi; }  // W4M layer 0: its highest height, water if none
    // W4M 0x4928e1: a jump edge leaves a top-layer node for a reach-table cell whose node heights (lo, hi) give landing deltas
    // d1 = hi' - lo, d2 = lo' - hi inside the arc's heights there; stepping back from the landing to the start (0x492c0d), each
    // cell crossed keeps its top under the arc, interpolated by where the landing sits in that range.
    bool jumpOk(const Game &g, int t, Vector3 from, Vector3 to) {
        const Grid &gr = *grid;
        const int i0 = gr.ci(from.x), j0 = gr.cj(from.z), i1 = gr.ci(to.x), j1 = gr.cj(to.z), dx = i1 - i0, dz = j1 - j0, N = gr.n[t];
        if (std::abs(dx) >= N || std::abs(dz) >= N) return false;
        const Grid::Entry &E = gr.reach[t][(size_t)N * std::abs(dz) + std::abs(dx)];
        float lo0, hi0, lo1, hi1;
        if (!E.ok || !probe(g, gr, *mv, i0, j0, from.y - R + 1, lo0, hi0) || !probe(g, gr, *mv, i1, j1, to.y - R + 1, lo1, hi1)) return false;
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
    void add(const Game &g, int i, int j, int l, Ai::Step st, Vector3 stand, int parent, int cost) {
        if (std::find(blocked.begin(), blocked.end(), grid->key(st.to)) != blocked.end()) return;
        if (!fits(g.terrain, stand)) return;  // ours: W4M Fits 0x59edf0 where the worm will stand
        auto it = index.find(nodeKey(i, j, l));
        if (it != index.end()) {
            Node &o = nodes[it->second];
            if (o.open && cost < o.g) o.step = st, o.parent = parent, o.g = cost;
            return;
        }
        index[nodeKey(i, j, l)] = (int)nodes.size();
        nodes.push_back({i, j, l, st, parent, cost, h(i, j), true});
    }
    void expand(const Game &g, int bi) {
        static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const Grid &gr = *grid;
        const Node n = nodes[bi];
        const NodeH here = nodeH(g, gr, *mv, n.i, n.j, n.l);
        const float drop = 0.045f / (g.gravity() * 20 / 1e6f) / 20;  // m: 9 at the standard gravity
        bool side[8] = {};
        for (int d : {4, 0, 6, 2, 5, 7, 3, 1})  // -x, +x, -z, +z, then each diagonal whose two sides were added
            if (!(d & 1) || (side[(d + 7) % 8] && side[(d + 1) % 8]))
                for (int l = 0; l < 2; l++) {
                    const int i = n.i + DX[d], j = n.j + DZ[d];
                    const NodeH q = nodeH(g, gr, *mv, i, j, l);
                    if (q.flag != 0 || q.hi - here.lo > 1 || here.hi - q.lo > drop) continue;  // 0x490830: up 20 units, down 0.045 / |Gravity|
                    side[d] = true;
                    const Vector3 at = nodePos(gr, q, i, j);
                    add(g, i, j, l, {at, atan2f((float)DX[d], (float)DZ[d]), 0}, {at.x, q.hi + R, at.z}, bi, n.g + (d & 1 ? 14 : 10));
                    break;
                }
        const Level &L = levelOf(g);
        if (n.l != 0) return;
        for (int t = 0; t < 2; t++) {  // JUMP_FORWARD, JUMP_BACKFLIP when MovementJumpForward/BackflipAllowed (0x4924d7)
            if (!(t ? L.flip : L.jump)) continue;
            for (int d = 0; d < 8; d++) {
                const float yaw = atan2f((float)DX[d], (float)DZ[d]);
                const Vector3 from = nodePos(gr, here, n.i, n.j), f = flat(yaw);
                Vector3 land;  // Game::jumpTick: tapped forward, or a backflip facing away
                if (!jumpFrom(g, *mv, nodeKey(n.i, n.j, 0) * 16 + t * 8 + d, from, t ? f * -Game::FLIP_BACK + Vector3{0, Game::FLIP_UP, 0} : f * Game::JUMP_FWD + Vector3{0, Game::JUMP_UP, 0}, land)) continue;
                const int i = gr.ci(land.x), j = gr.cj(land.z), l = layerOf(g, gr, *mv, land);
                if ((i == n.i && j == n.j) || nodeH(g, gr, *mv, i, j, l).flag != 0 || !jumpOk(g, t, from, land)) continue;
                add(g, i, j, l, {land, yaw, (uint8_t)(t + 1)}, land, bi, n.g + octile(i - n.i, j - n.j) + (t ? 60 : 40));
            }
        }
    }
};

static std::shared_ptr<Search> newSearch(std::shared_ptr<const Grid> grid, std::shared_ptr<Moves> mv, const std::vector<int64_t> &blocked,
                                         const Ai::Route &r, Vector3 from) {
    auto s = std::make_shared<Search>();
    s->grid = std::move(grid), s->mv = std::move(mv), s->blocked = blocked, s->route = r, s->start = from;
    return s;
}

// The turn time a path may take: W4M refuses one that would leave under ForbidMoveIfWouldLeaveTimeLessThan 10 s (0x494614)
static int pathLimit(int timer) { return std::max(0, (int)((timer * DT - 10) / DT)); }

void Ai::startSearch(const Game &g, const Route &r) {
    search = newSearch(grid, moves, blocked, r, g.worms[g.current].pos);
    if (r.purpose != Route::Retreat && r.purpose != Route::Close && r.purpose != Route::Before && r.purpose != Route::After) mode = Mode::Search;
}

void Ai::searchStep(const Game &g) {
    Search &s = *search;
    const Grid &gr = *s.grid;
    auto done = [&](int end) {  // end: the path's last node, 0 the start is the goal, -1 none
        std::vector<Step> steps;
        for (int i = end; i > 0; i = s.nodes[i].parent) steps.push_back(s.nodes[i].step);
        std::reverse(steps.begin(), steps.end());
        const Route r = s.route;
        // W4M 0x4942f0: an empty path fails (0x959a58 0xb, the start is the goal: only the retreat leg takes it), and so does one past
        // the time left (estimate 0x4ae0a0: G x 0.0029 x node spacing in units, s); 0x494a42: a partial one ending under 50 units out
        const bool far = end > 0 && Vector3Distance(s.nodes[end].step.to, s.start) >= 2.5f;
        const int ticks = end > 0 ? (int)(s.nodes[end].g * 0.0029f * gr.s * 20 / DT) : 0;
        const bool ok = r.partial ? far : end > 0 ? r.limit < 0 || ticks <= r.limit : end == 0 && r.purpose == Route::After;
        const Vector3 at = end > 0 ? s.nodes[end].step.to : s.start;
        const float yaw = steps.empty() ? g.worms[g.current].yaw : steps.back().yaw;
        search.reset();
        switch (r.purpose) {
        case Route::Before: {  // the leg to the move node, then the retreat leg from that node
            pairOk = ok, pairBefore = std::move(steps), pairAt = at, pairYaw = yaw;
            search = newSearch(grid, moves, blocked, {Route::After, 50, pathLimit(thinkTimer), false, entryPos(g, gr, *moves, pairR)}, r.goal);
            return;
        }
        case Route::After:  // W4M 0x49faf0: both legs found: attacks from the move node; a failed leg blocks its node (0x4a9030)
            if (pairOk && ok) {
                Origin o;
                o.pos = pairAt, o.yaw = pairYaw, o.before = std::move(pairBefore), o.after = std::move(steps), o.path = true;
                o.weight = logf(1.1f * expf(pairScore));  // m_fRetreatScoreWeight (0x49fdd6), as W4M writes it
                o.to = {Route::Rewalk, 200, pathLimit(thinkTimer), false, entryPos(g, gr, *moves, pairM)};
                o.back = r, o.back.purpose = Route::Retreat;
                origins.push_back(std::move(o));
                blockAround(*moves, pairM), blockAround(*moves, pairR), pairs++;
            } else {
                if (!pairOk) blockAround(*moves, pairM);
                if (!ok) blockAround(*moves, pairR);
            }
            return;
        case Route::Close: if (ok) afterPath = std::move(steps), afterRoute = r, afterRoute.purpose = Route::Retreat; return;
        case Route::Retreat: if (ok) takePath(g, std::move(steps)); return;
        case Route::Rewalk:
            if (ok) { takePath(g, std::move(steps)); mode = Mode::Walk; return; }
            mode = Mode::Eval, stage = -1, fireAfterWalk = false;  // W4M: the move is forbidden, think again
            return;
        default:  // a move plan (W4M CAIPlanMove: CollectSomething, MoveCloserToTarget, DoRandomSmallMove): walk then think again
            if (ok) {
                walking = r, walking.purpose = Route::Rewalk, repaths = 0;
                takePath(g, std::move(steps));
                wait = moved ? (int)(levelOf(g).nonFirstMove / DT) : 0;  // 0x4983a0: DelayBeforeNonFirstMove once a path ran since the last shot
                walks++;
                moved = true, fireAfterWalk = false, mode = Mode::Walk;
                return;
            }
            mode = Mode::Eval;
            decide(g);  // the next plan in rank
        }
    };
    if (!s.started) {
        s.started = true;
        const int i = gr.ci(s.start.x), j = gr.cj(s.start.z);
        s.gi = gr.ci(s.route.goal.x), s.gj = gr.cj(s.route.goal.z), s.gl = layerOf(g, gr, *s.mv, s.route.goal);
        s.nodes = {{i, j, layerOf(g, gr, *s.mv, s.start), {s.start, 0, 0}, -1, 0, s.h(i, j), true}};
        s.index = {{nodeKey(i, j, s.nodes[0].l), 0}};
        // W4M 0x492210: a whole-path search to a goal that is no node fails at once (0x959a58 6..9)
        if (!s.route.partial && nodeH(g, gr, *s.mv, s.gi, s.gj, s.gl).flag != 0) return done(-1);
        return;
    }
    int bi = -1;  // pop the open node with the lowest F = G + H (W4M 0x492d80)
    for (size_t i = 0; i < s.nodes.size(); i++)
        if (s.nodes[i].open && (bi < 0 || s.nodes[i].g + s.nodes[i].h < s.nodes[bi].g + s.nodes[bi].h)) bi = (int)i;
    if (bi >= 0 && s.nodes[bi].i == s.gi && s.nodes[bi].j == s.gj && s.nodes[bi].l == s.gl) return done(bi);
    if (bi < 0 || s.iter++ >= std::min(s.route.iters, MAX_ITER)) return done(s.route.partial ? s.best : -1);  // 0x4b0ba6 caps; 0x490ed0
    s.nodes[bi].open = false;
    if (s.nodes[bi].h < s.nodes[s.best].h) s.best = bi;
    s.expand(g, bi);
}

// W4M 0x496f24: MovementJumpError scales each jump's displacement by 1 ± e per component; only its heading is the AI's to choose.
void Ai::takePath(const Game &g, std::vector<Step> &&p) {
    const Worm &w = g.worms[g.current];
    const float e = levelOf(g).jumpErr;
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
    if (++repaths > 2 || walking.purpose < 0 || !moves) { noMove = true; return false; }  // "Has already done too many repaths, forbidding further movement"
    startSearch(g, walking);
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

// W4M 0x4a9e90: threat rating of target i at `at`, probed in 8 directions at 50..200 units (0x90ebf8), weight 2/(k+2) by range, from the ground height
// of the probe: water or no land 0.2, a drop over 30 units 0.05, and within 100 units with a linear falloff mines 0.1, oil drums 0.05 (GameLogicService
// m_OilDrumIds, 0x4f56a0) and every other listed target of positive value 0.05 · value / norm (norm: the previous think's mean, 0x959868, still
// unset at the first). Returns the total capped at 1; `away` is opposite the worst direction (zero if none).
static float threatOf(const Game &g, size_t i, const std::vector<float> &vals, const std::vector<bool> &listed, float norm, Vector3 &away) {
    static const float RANGE[4] = {2.5f, 5, 7.5f, 10};
    const Vector3 at = targetPos(g, i);
    float worst = 0, total = 0;
    int dir = -1;
    for (int d = 0; d < 8; d++) {
        Vector3 f = flat(d * PI / 4);
        float t = 0;
        for (int k = 0; k < 4; k++) {
            const float wgt = 2.0f / (k + 2);
            Vector3 p = at + f * RANGE[k], hit;
            if (!g.terrain.raycast({{p.x, at.y + 3, p.z}, {0, -1, 0}}, 30, &hit) || hit.y < g.water) t += 0.2f * wgt;
            else {
                p.y = hit.y;
                if (at.y - R - hit.y > 1.5f) t += 0.05f * wgt;
            }
            for (const Object &o : g.objects)
                if (o.type == Object::Mine || o.type == Object::Barrel) t += (o.type == Object::Mine ? 0.1f : 0.05f) * wgt * fmaxf(0, 1 - Vector3Distance(o.pos, p) / 5);
            for (size_t j = 0; j < vals.size(); j++)
                if (const float dist = Vector3Distance(targetPos(g, j), p); j != i && listed[j] && vals[j] > 0 && dist < 5) t += vals[j] / norm * (1 - dist / 5) * 0.05f * wgt;
        }
        total += t;
        if (t > worst) worst = t, dir = d;
    }
    away = dir < 0 ? Vector3{} : flat(dir * PI / 4) * -1.0f;
    return fminf(total, 1);
}

void Ai::unit(const Game &g) {
    if (search) return searchStep(g);
    if (!threats.empty()) {  // threat rating; ProjectileSweetSpotDistance (0x4a9be0) aims that far on a threatened enemy's safe side
        const size_t i = threats.back();
        rating[i] = threatOf(g, i, vals, listed, threatNorm, away[i]);
        threats.pop_back();
        return;
    }
    if (moves && moves->at < 2 * 441) {  // W4M ScoreAllMoveNodes: one window entry; water, blocked or outside: -1e6 (0x4ab687)
        Moves &mv = *moves;
        if (mv.warm < 65) {  // the scores read layer 0 of every node within 32: one row of them per unit
            const long reads = mv.reads;
            for (int j = -32; j <= 32; j++) nodeH(g, *grid, mv, mv.i0 - 32 + mv.warm, mv.j0 + j, 0);
            debt += (mv.reads - reads) / 8, mv.warm++;
            return;
        }
        const int k = mv.at++, l = k / 441;
        Moves::Entry &E = mv.e[k];
        const long reads = mv.reads;
        if (nodeH(g, *grid, mv, mv.i(k), mv.j(k), l).flag == 0) posScore(g, *grid, mv, vals, norm, g.current, mv.i(k), mv.j(k), l, E.def, E.open), debt += 1000;  // ~35 us: 625 cached nodes
        debt += (mv.reads - reads) / 8;
        return;
    }
    if (pairing) return nextPair(g);
    if (deciding) return decide(g);
    if (origin < (int)origins.size()) {  // the shots, origin by origin
        if (built != origin) targetsFor(g, origins[origin]), built = origin;
        const int T = (int)tpos.size(), N = (int)WEAPONS.size() * T, team = g.worms[g.current].team;
        while (stage < N && !g.usable(team, stage / T)) stage++, sub = 0;  // unusable (or scheme-delayed) weapons are skipped for free
        if (stage < N) {
            int n = evalWeapon(g, stage / T, stage % T, sub, origins[origin], origin);
            if (++sub >= n) sub = 0, stage++;
        }
        if (stage >= N) origin++, stage = sub = 0;
        return;
    }
    deciding = true;
    decide(g);
}

// W4M CAIPlanAttackWithMoveAndRetreat::Spawn 0x49f8a0, up to 3 times: a move -> retreat pair (0x4a8c60), the path to the move node
// (200 iterations) and from it to the retreat (50); with no pair left, plans from where the worm stands, x ln 1.1 (no retreat score).
void Ai::nextPair(const Game &g) {
    if (pairs < 3) {
        if (moves && pairOf(*moves, pairM, pairR, pairScore)) {
            startSearch(g, {Route::Before, 200, pathLimit(thinkTimer), false, entryPos(g, *grid, *moves, pairM)});
            return;
        }
        const Worm &w = g.worms[g.current];
        Origin o;  // "No move->retreat pair found. Just spawn plans involving no movement"
        o.pos = w.pos, o.yaw = w.yaw, o.weight = logf(1.1f * expf(0));
        origins.push_back(std::move(o));
    }
    pairing = false;
}

// W4M think start (0x49af70): target values (0x4ab730), the attack roots, the move window. Its worm-select mode (0x4a4f2a) needs
// ChooseWorm.Enabled, 0 in LOCAL.XOM and written nowhere: the CPU plans for its current worm only and never uses Change Worm.
void Ai::startEval(const Game &g) {
    const int me = g.current;
    const Worm &w = g.worms[me];
    const Level &L = levelOf(g);
    plan = Plan{};
    threats.clear(), origins.clear(), choices.clear(), pending.clear(), choiceAt = 0, deciding = false, moves.reset();
    rating.assign(targetCount(g), 0), away.assign(targetCount(g), {}), vals.assign(targetCount(g), 0), listed.assign(targetCount(g), false);
    bool enemy = !scripted(g).empty();
    float sum = 0;
    int live = 0;
    for (size_t i = 0; i < g.worms.size(); i++) {
        if (!targeted(g, g.worms[i])) continue;
        enemy |= g.alliance(g.worms[i].team) != g.alliance(w.team);  // before the filter (0x4ab730 sets bVar1 first)
        if (!(listed[i] = considered(g, me, (int)i))) continue;
        threats.push_back((int)i);  // rated by unit()
        vals[i] = targetValue(g, L, me, (int)i);
    }
    for (size_t i = g.worms.size(); i < targetCount(g); i++) vals[i] = 1, listed[i] = true, threats.push_back((int)i);
    secondPass(g, listed, vals);
    for (size_t i = 0; i < vals.size(); i++) if (listed[i]) sum += fabsf(vals[i]), live++;
    threatNorm = norm;  // 0x4ab070 rates the threats before 0x959868 is renewed
    norm = live && sum != 0 ? sum / live : 1;
    if (!enemy) return;
    stage = sub = origin = pairs = 0, built = -1;
    thinkTimer = g.timer;
    regress(g);
    Origin close;  // the root plans: close range and Special (Flood), scored x1 where the worm stands
    close.pos = w.pos, close.yaw = w.yaw, close.close = true;
    origins.push_back(std::move(close));
    pairing = !w.nailed && !g.artillery() && !noMove;  // W4M GetThinkingWormCanMove: 0x90ea74 (not Tug O Worms artillery), 0x9560f8
    if (!pairing) return nextPair(g);
    moves = std::make_shared<Moves>();
    Moves &mv = *moves;
    mv.i0 = grid->ci(w.pos.x), mv.j0 = grid->cj(w.pos.z);
    mv.l0 = layerOf(g, *grid, mv, w.pos);
}

// The plans' targets (W4M 0x49f8a0 spawns per c_plstTargets entry): each enemy; each weapon family sets its own point from it.
void Ai::targetsFor(const Game &g, const Origin &) {
    tpos.clear(), tworm.clear();
    for (size_t i = 0; i < targetCount(g); i++) {
        if (i < g.worms.size() && (!listed[i] || g.alliance(g.worms[i].team) == g.alliance(g.worms[g.current].team))) continue;
        tpos.push_back(targetPos(g, i)), tworm.push_back((int)i);
    }
}

// W4M target position (+0xc): a worm's feet, a scripted target's marker
static Vector3 feetOf(const Game &g, int i) { return targetPos(g, i) - Vector3{0, i < (int)g.worms.size() ? R : 0, 0}; }

// W4M 0x49f690 (projectile, direct, strike, animal scores): WeightingPunchThroughLand 10 x max(0.5, LandDamageRadius / d), d = the
// plan point's distance to its target (0: 0.5); no LandDamageRadius: 0
static float punch(const Game &g, float ldr, Vector3 at, int target) {
    if (ldr == 0) return 0;
    const float d = Vector3Distance(at, feetOf(g, target));
    return 10 * (d > 0 ? fmaxf(0.5f, ldr / d) : 0.5f);
}

// W4M projectile families (0x49ffa0) by solve: 0x4ace50 arc, 0x4ad250 timed, 0x4ad770 bounce to rest; Super Sheep 0x49c400; -1 no plan class
enum Solver { ARC, TIMED, BOUNCE, SHEEP };
static int solverOf(const WeaponDef &wd) {
    static const std::pair<const char *, int> PLANS[] = {{"Bazooka", ARC}, {"Poison Arrow", ARC}, {"Homing Missile", ARC}, {"Grenade", TIMED},
        {"Cluster Grenade", TIMED}, {"Banana Bomb", TIMED}, {"Gas Canister", BOUNCE}, {"Holy Hand Grenade", BOUNCE}, {"Super Sheep", SHEEP}};
    for (const auto &[n, k] : PLANS) if (wd.name == n) return k;
    return -1;
}

// The plan point (+0x14, 0x49ffa0 / 0x4a2fe0): Gas Canister and Super Sheep at the target, Cluster Grenade ClusterDistanceAboveTarget
// (10 units) over it, the others at its sweet spot (0x4a9be0) when its threat rating is positive
static Vector3 projPoint(const Game &g, const WeaponDef &wd, int ti, float rating, Vector3 away, float sweet) {
    const Vector3 f = feetOf(g, ti);
    if (wd.name == "Gas Canister" || wd.name == "Super Sheep") return f;
    if (wd.name == "Cluster Grenade") return f + Vector3{0, 10 / UNIT, 0};
    return rating > 0 ? f + away * sweet : f;
}

// W4M IsBomberWeapon (0x49e1e0): the bomb aimed at T, dropped from Land.MaxHeight + Bomber.ExtraHeight along D at GroundSpeed
// (BomberLogicEntity 0x54d460), must land within 10 units of it. Ours falls under the sim's gravity, land only; *hit: where it met land.
static bool bombLands(const Game &g, const WeaponDef &wd, Vector3 T, Vector3 D, float top, std::optional<Vector3> &hit) {
    const float grav = g.gravity() * wd.grav, h = top + Game::STRIKE_EXTRA, lead = Game::BOMBER_SPEED * sqrtf(2 * fmaxf(h - T.y, 0) / grav);
    Vector3 p = {T.x - D.x * lead, h, T.z - D.z * lead}, v = D * Game::BOMBER_SPEED;
    for (int i = 0; i < 600 && p.y > g.water; i++) {
        const Vector3 v0 = v;
        v.y -= grav * DT;
        const Vector3 d = arcMove(v0, v);
        for (int k = 0, n = substeps(v); k < n; k++)
            if (p = p + d * (1.0f / n); g.terrain.solid(p)) return hit = p, Vector3Distance(p, T) < 0.5f;
    }
    return false;
}

// --- W4M AIPlanUtilities projectile solves (docs/w4m/ai.md "Projectile IsPossible"), in W4M units (20 per m, origin Terrain::origin), ms ---
static Vector3 toUnits(const Terrain &t, Vector3 m) { return (m - t.origin) * UNIT; }
static Vector3 toMetres(const Terrain &t, Vector3 u) { return u * (1 / UNIT) + t.origin; }

// W4M land ray 0x466ae0 along S + V k + A k²/2 for k = 0..steps ms: the first land, refined 5 times to its last empty point; step: its
// truncation (0 from inside land, steps + 1 none). Ours samples it 2.5 units apart (Terrain::SUB).
struct Sweep { int step; Vector3 at, n; };
static Sweep sweep(const Terrain &t, Vector3 S, Vector3 V, Vector3 A, int steps) {
    auto at = [&](float k) { return S + V * k + A * (0.5f * k * k); };
    auto land = [&](float k) { return t.solid(toMetres(t, at(k))); };
    const float floor = -t.origin.y * UNIT;  // the grid's bottom: no land under it
    if (land(0)) return {0, S, {0, 1, 0}};
    for (float k = 0; k < steps;) {
        const float k2 = fminf((float)steps, k + fmaxf(Terrain::SUB * UNIT / fmaxf(Vector3Length(V + A * k), 1e-3f), 1e-2f));
        if (land(k2)) {
            float air = k, in = k2;
            for (int i = 0; i < 5; i++) { const float m = (air + in) / 2; (land(m) ? in : air) = m; }
            return {(int)air, at(air), t.normal(toMetres(t, at(air)))};
        }
        if (at(k2).y < floor && V.y + A.y * k2 <= 0) break;
        k = k2;
    }
    return {steps + 1, {}, {0, 1, 0}};
}

// W4M TargetParabola 0x519a40: the arcs through T at speed s (v98 the lower, t2; v10 the higher, t1); none: false, v10 = 0. Without
// acceleration both are (s, 0, 0) and the times are kept.
static bool arcs(Ai::Para &p, float s) {
    p.v10 = {};
    const Vector3 D = p.T - p.S;
    const float a2 = Vector3LengthSqr(p.A), dd = Vector3LengthSqr(D) * a2;
    if (dd == 0) return p.v98 = p.v10 = {s, 0, 0}, true;
    const float f = s * s + Vector3DotProduct(D, p.A), disc = f * f - dd;
    if (disc < 0) return false;
    const float r = sqrtf(disc), big = 2 * (f + r) / a2;
    float first = 2 * (f - r) / a2, second = first;
    if (big >= 0) first = big, second = second < 0 ? big : second;
    p.t1 = sqrtf(first), p.t2 = sqrtf(second);
    p.v10 = D / p.t1 - p.A * (p.t1 / 2), p.v98 = D / p.t2 - p.A * (p.t2 / 2);
    return true;
}

// W4M 0x4aca20: clear when the sweep meets no land before min(ms, 9989) - 10; its hit is the new collision point (none: unknown, W4M
// keeps the last land ray's)
static bool clearArc(const Terrain &t, Ai::Para &p, Vector3 v, float ms) {
    const float last = fminf(ms, 9989) - 10;
    const int steps = std::min(10000, std::max(0, (int)floorf(last) + 1));  // a later hit cannot refuse it
    const Sweep s = sweep(t, p.S, v, p.A, steps);
    p.known = s.step <= steps, p.hit = s.at;
    return last < (float)s.step;
}

// W4M 0x4ace50: speeds lo + (hi - lo) (f0 .. f1) in 10 steps, each speed's lower then higher arc; the first clear one is the launch v.
// None: v and hit are the arc whose collision came nearest T (an unsolvable speed repeats the previous pair; none yet: the origin)
static bool arcSolve(const Terrain &t, Ai::Para &p, float f0, float f1, Vector3 &v) {
    float s = (p.hi - p.lo) * f0 + p.lo, end = (p.hi - p.lo) * f1 + p.lo, step = (end - s) / 10;
    if (step == 0) step = 1, s -= 1;
    float best = 1e8f;
    Vector3 c1{}, c2{}, v1{}, v2{}, bh{}, bv{};
    for (s += step, end += 1e-7f; s <= end; s += step) {
        if (arcs(p, s)) {
            if (clearArc(t, p, p.v98, p.t2)) return v = p.v98, true;
            c1 = p.hit, v1 = p.v98;
            if (clearArc(t, p, p.v10, p.t1)) return v = p.v10, true;
            c2 = p.hit, v2 = p.v10;
        }
        if (const float e = Vector3DistanceSqr(p.T, c1); e < best) best = e, bh = c1, bv = v1;
        if (const float e = Vector3DistanceSqr(p.T, c2); e < best) best = e, bh = c2, bv = v2;
    }
    p.hit = bh, p.known = true, v = bv;
    return false;
}

// W4M 0x4ad250 (Grenade, Cluster, Banana): the arc through T in exactly 1 then 2 s, capped at hi; possible when within hi and clear, the
// fuse that time. None: v and hit of the nearer collision; a capped arc that is clear leaves W4M the last land ray's hit [ours: not counted]
static bool timedSolve(const Terrain &t, Ai::Para &p, Vector3 &v, int &fuse) {
    float best = 1e8f;
    Vector3 bh{}, bv{};
    bool known = false;
    for (int k = 1; k < 3; k++) {
        const float ms = k * 1000.0f;
        p.t1 = p.t2 = ms, p.v98 = p.v10 = (p.T - p.S - p.A * (0.5f * ms * ms)) / ms;  // 0x5198d0
        const bool slow = Vector3LengthSqr(p.v10) <= p.hi * p.hi;
        if (!slow) p.v10 = Vector3Normalize(p.v10) * p.hi;
        if (clearArc(t, p, p.v10, ms) && slow) return v = p.v10, fuse = k, true;
        if (!p.known) continue;
        if (const float e = Vector3DistanceSqr(p.T, p.hit); e < best) best = e, bh = p.hit, bv = p.v10, known = true;
    }
    p.hit = bh, p.known = known, v = bv;
    return false;
}

// W4M 0x4ad580: the rest point of a throw at v from S, up to 10 bounces (0x57ef80 with the payload's damping) until |v|² <= Bounce.MinSpeed
// (0.03, compared squared); each bounce's velocity is the parabola's own v10 + A k (0x5193e0), not the throw's. No land in 10 s: the
// v10 arc where it meets Water.Level - 100 units (0x519a00).
static Vector3 restOf(const Terrain &t, const Ai::Para &p, const WeaponDef &wd, Vector3 v, float floor) {
    constexpr float MIN_SPEED = 0.03f;
    Vector3 pos = p.S, rest = p.hit;
    if (Vector3LengthSqr(v) <= MIN_SPEED) return rest;
    for (int i = 0; i < 10; i++) {
        const Sweep s = sweep(t, pos, v, p.A, 10000);
        if (s.step > 9999) {  // 0x5194c0: the first time past 0 the v10 arc is at that height, else the later one
            const float c = p.S.y - floor, b = p.v10.y, a = p.A.y;
            float k = -1;
            if (a == 0) k = -c / b;
            else if (const float d = b * b - 2 * a * c; d >= 0) {
                const float r1 = (sqrtf(d) - b) / a, r2 = (-b - sqrtf(d)) / a, lo = fminf(r1, r2), hi = fmaxf(r1, r2);
                k = lo > 0 ? lo : hi;
            }
            return k >= 0 ? p.S + p.v10 * k + p.A * (0.5f * k * k) : rest;
        }
        rest = pos = s.at;
        v = payBounce((p.v10 + p.A * (float)s.step) * 50.0f, s.n, wd.damp, true) * (1 / 50.0f);  // units/ms <-> m/s
        if (Vector3LengthSqr(v) <= MIN_SPEED) break;
    }
    return rest;
}

// W4M BounceToRest 0x4ad770 (Gas Canister, Holy Hand Grenade): from 0x4ace50's launch, 10 tries scaling the best throw by
// (|T - shooter|² / |rest - shooter|²)^step (step 0.5, x2 after a better rest, then x0.5); possible when a rest comes within 10 units of T
// and |v| <= 10 units/ms. hit: the best rest.
static bool bounceSolve(const Terrain &t, Ai::Para &p, const WeaponDef &wd, Vector3 shooter, float water, Vector3 &v) {
    arcSolve(t, p, 0, 1, v);
    if (fabsf(v.x) <= 1.192e-7f && fabsf(v.y) <= 1.192e-7f && fabsf(v.z) <= 1.192e-7f) return false;  // 0x445310
    const float floor = water - 100, target = Vector3DistanceSqr(p.T, shooter);
    float best = 1e8f, step = 0.5f;
    Vector3 keep = v, rest = p.hit;
    bool ok = false;
    for (int i = 0; i < 10 && !ok; i++) {
        const Vector3 r = restOf(t, p, wd, v, floor);
        if (const float e = Vector3DistanceSqr(p.T, r); e < best) step *= 2, keep = v, rest = r, best = e, ok = e <= 100;
        if (ok) break;
        v = keep * expf(logf(target / Vector3DistanceSqr(rest, shooter)) * step), step *= 0.5f;
    }
    v = keep, p.hit = rest, p.known = true;
    return ok && Vector3Length(v) <= 10;
}

// W4M CAIPlanAttackStrike 0x4a13a0: the bomb line heading with the most value hit (docs/w4m/ai.md §18 "Strike heading"); false when
// the last heading checked by 0x49e1e0 was refused (impossible), bomb: where that bomb met land (none: W4M's last land ray). view: the Blimp yaw
// that sets it. A best-miss clone (+0x3c) skips the check.
static bool strikeHeading(const Game &g, const Level &L, const WeaponDef &wd, int target, const std::vector<float> &vals,
                          const std::vector<float> &rating, const std::vector<Vector3> &away, float top, float &view, bool miss,
                          std::optional<Vector3> &bomb) {
    const Vector3 T = targetPos(g, target);
    const float rb = blastOf(wd, wd.kind == Kind::Airstrike).reach, reach = 2 * Game::BOMBER_SPEED + 2 * rb, gap = 2 * Game::BOMBER_SPEED / 6;
    const bool bomber = !miss && (wd.kind == Kind::Airstrike || wd.name == "Fatkins Strike");  // WEAPTWK IsBomberWeapon
    float best = -1e6f;
    bool ok = false;
    for (int a = 0; a < 360; a += 10) {
        const float r = a * PI / 180;
        const Vector3 D = {cosf(r), 0, -sinf(r)};
        float total = 0;
        for (size_t k = 0; k < targetCount(g); k++) {
            const Vector3 xp = targetPos(g, k);
            if (!targetAlive(g, k) || Vector3Distance(xp, T) >= reach) continue;
            const Vector3 aim = rating[k] > 0 ? xp + away[k] * STRIKE_SWEET : xp;
            float sum = 0;
            for (int i = 0; i < 6; i++) {
                const Vector3 b = T + D * (gap * (i - 2.5f));
                const float d = Vector3Distance(b, aim), f = (rb - d) / rb, t = f * vals[k];
                if (d >= rb) continue;
                sum += t;
                if (f > 0 && f < 0.8f && L.thrust && i < 5) sum += Vector3DotProduct(D, Vector3Normalize(xp - b)) * t * 0.5f;
            }
            if (sum > 0) total += (int)k == target ? sum : sum * L.others;
        }
        if (total > best) {
            bomb.reset();
            ok = !bomber || bombLands(g, wd, T - Vector3{0, R, 0}, D, top, bomb);
            if (ok) best = total, view = atan2f(D.x, D.z);  // W4M Airstrike.Direction is the Blimp view's forward
            else a += 30;
        }
    }
    return ok;
}

// The ranking every attack plan goes through: x the origin's weight (+0x58), the failed-plan memory (0x4a6590), then x taste for a positive
// score (W4M 0x498a0a, 0x49c060: (1 ± PlanScoreRandomise) x Pref(weapon) x (1 − PreferVariety if last turn's weapon))
float Ai::rankOf(const Game &g, int wi, int target, int origin, float &score) const {
    const Level &L = levelOf(g);
    const int me = g.current, team = g.worms[me].team;
    score *= origins[origin].weight;
    for (const Fail &f : failed)  // this worm at a target that survived it, x(1 - effect / 2), another weapon 0.2 of that
        if (f.worm == me && f.target == target) score *= 1 - 0.5f * f.effect * (f.weapon == wi ? 1 : 0.2f);
    const uint32_t h = (salt ^ (uint32_t)wi * 2654435761u) * 2246822519u;
    const WeaponDef &wd = WEAPONS[wi];
    const std::string &n = wd.name;
    const float pref = n == "Prod" ? 0.3f : n == "Cluster Grenade" ? L.cluster : n == "Gas Canister" ? L.gas : wd.kind == Kind::Homing ? L.homing : 1;
    const float taste = (1 + L.randomise * ((h >> 8) / 16777216.0f * 2 - 1)) * pref * (wi == g.picked[team] ? 0.6f : 1);
    return score > 0 ? score * taste : score;
}

// W4M projectile Score 0x4a0540 (animal 0x4a4210 for the Super Sheep) at the plan point, before the origin weight: AddScoreMove when the plan
// has a path (0x49bd60), the blast (WormDamageRadius, WormDamageMagnitude, knock NearbyThreat; none for a best-miss, 0x49efb5), the punch term
float Ai::projScore(const Game &g, const Plan &p) const {
    const Level &L = levelOf(g);
    const WeaponDef &wd = WEAPONS[p.weapon];
    Outcome o(g, L, g.current, rating, vals);
    o.blast(p.at, blastOf(wd, false), p.miss ? 0 : L.threat);
    return o.s + (origins[p.origin].path ? MOVE_BONUS + (moved ? 0 : FIRST_MOVE_BONUS) : 0) + punch(g, wd.radius, p.at, p.target);
}

int Ai::evalWeapon(const Game &g, int wi, int only, int sub, const Origin &O, int oi) {
    const int me = g.current;
    Worm w = g.worms[me];
    w.pos = O.pos, w.yaw = O.yaw;  // W4M sets the worm position to the plan's (0x49b479) when it has a path
    const WeaponDef &wd = WEAPONS[wi];
    const int team = w.team;
    const Level &L = levelOf(g);
    if (g.shotsLeft && wi != g.weapon) return 0;  // locked to the weapon with shots left (0x493500)
    if (!g.usable(team, wi) && !(g.shotsLeft && wi == g.weapon)) return 0;  // W4M SchemeData Delay: not before its turn
    if (w.nailed && !nailUsable(wd.kind)) return 0;  // Tail Nail: the sim refuses it
    static const char *BLIMPED[] = {"Homing Missile", "Airstrike", "Super Airstrike", "Concrete Donkey", "Fatkins Strike"};
    if (g.wp(WP_NO_BLIMP) && std::count_if(std::begin(BLIMPED), std::end(BLIMPED), [&](const char *b) { return wd.name == b; })) return 0;  // 0x90ea75 (0x4a03ab, 0x4a0f18)
    // W4M root plans (CloseRange: melee, dropped explosives, landmine; Special: Flood) stand where the worm is; Projectile, Direct,
    // Strike and Animal plans come from each move -> retreat pair, for targets over 100 units (5 m) from it (0x49fcf6)
    const bool close = wd.kind == Kind::Melee || wd.kind == Kind::Mine || wd.kind == Kind::Flood || (wd.kind == Kind::Shell && dropped(wd));
    if (close != O.close) return 0;
    // W4M 0x49bd60: AddScoreMove, + AddScoreMoveIfNotMoved while no path ran since the last shot, for a plan with a path before or
    // always for a close-range explosive (0x4a2c70 forces it)
    auto bonus = [&](bool force) { return force || O.path ? MOVE_BONUS + (moved ? 0 : FIRST_MOVE_BONUS) : 0; };
    auto consider = [&](float score, float yaw, float pitch, int charge, int target, float view = 0) {
        const float rank = rankOf(g, wi, target, oi, score);
        if (rank > plan.rank) plan = {wi, charge, target, yaw, pitch, score, rank, oi, view, feetOf(g, target)};
    };
    // a projectile plan (W4M 0x49ffa0, Super Sheep 0x4a2fe0): scored now, solved when the list pops it
    auto project = [&](int target) {
        Plan p{wi, 0, target};
        p.origin = oi, p.at = projPoint(g, wd, target, rating[target], away[target], L.sweet);
        p.score = projScore(g, p), p.rank = rankOf(g, wi, target, oi, p.score);
        pending.push_back({p.rank, {}, p, true});
    };
    int cands = 0;
    auto pick = [&] { return sub < 0 || cands++ == sub; };  // sub >= 0: only that candidate, the rest are counted
    auto shell = [&](Vector3 at) {  // W4M CAIPlanAttackProjectile 0x4a0540, close-range explosive 0x4a2c70 for a dropped shell
        Outcome o(g, L, me, rating, vals);
        o.blast(at, blastOf(wd, false), L.threat, dropped(wd));  // ours: a dropped shell spares the thrower, it retreats
        return o.s + bonus(dropped(wd));
    };
    // W4M close-range plans (0x4a22d0) set the blast next to their target; ours sets it down at the feet, so the target must be in it
    auto inBlast = [&](Vector3 at, int ti, const Blast &b) { return Vector3Distance(targetPos(g, ti), at) < b.reach; };
    for (size_t k = only < 0 ? 0 : only; k < (only < 0 ? tpos.size() : only + 1); k++) {
        const Vector3 e = tpos[k];
        const int ti = tworm[k];
        if (!close && Vector3Distance(targetPos(g, ti), O.pos) <= 5) continue;
        const bool isWorm = Vector3Equals(e, targetPos(g, ti));
        const Vector3 to = e - w.pos;
        const float yawE = atan2f(to.x, to.z);
        switch (wd.kind) {
        case Kind::Shell:
            if (dropped(wd) && pick()) {  // also set it down at the feet, facing the target
                Vector3 d = dirOf(yawE, 0), out;
                if (fly(g, wd, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), d * wd.speed, g.wind, false, out) && inBlast(out, ti, blastOf(wd, false))) consider(shell(out), yawE, 0, 1, ti);
            }
            if (!dropped(wd) && solverOf(wd) >= 0 && pick()) project(ti);  // a Weapon Factory weapon has no plan class
            break;
        case Kind::Homing:  // locked on the plan point from the Blimp (act())
            if (pick()) project(ti);
            break;
        case Kind::Shotgun: {  // W4M CAIPlanAttackDirect 0x4a0a20: one shot (bCanMoveBetweenShots) as a WormDamageRadius blast, no knock
            if (!pick()) break;
            // the plan's target point +0x14: the target's position (feet) + Worm.EyeLevelOffset / 2 (Spawn 0x4a0840); IsPossible, the score and the aim all use it
            const Vector3 P = e + Vector3{0, 0.5f * Game::EYE - (isWorm ? Game::R : 0), 0};
            const Vector3 m = muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), seg = P - m;
            // the aim vector +0x20 = target point - muzzle (0x49e000 / 0x4ac880); 0x49e6d0 queues SetAimAngle(asin(y / |v|)) and the worm orientation from it
            const float yawS = atan2f(seg.x, seg.z), pitch = asinf(Clamp(seg.y / fmaxf(Vector3Length(seg), 1e-6f), -1, 1));
            Vector3 land;  // IsPossible 0x4ac880: the muzzle -> target land sweep (1000 steps) must pass step 989
            if (g.terrain.raycast({m, Vector3Normalize(seg)}, Vector3Length(seg) * 0.989f, &land)) break;
            const Game::GunHit hit = g.gunRay({m, dirOf(yawS, pitch)}, g.worms[me], wd.size);
            if (hit.dist < Game::GUN_RANGE && pitch > -1.2f && pitch < 1.45f) {  // ours: the sim's ray must strike within range
                Outcome oc(g, L, me, rating, vals);
                oc.blast(P, g.gunBlast(wi), 0);  // W4M 0x4a0a20: 0x49f190 scores the blast at the plan point, whoever the ray strikes
                consider(oc.s + bonus(false), yawS, pitch, 0, ti);
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
                    Outcome oc(g, L, me, rating, vals);
                    oc.hurt(ti, wd.damage * (g.doubled() ? 2 : 1), L.threat, !wd.pins);  // armour spares ids 10..12 only (bat, prod, fire punch)
                    consider(oc.s + bonus(false), at.yaw, pitch, 0, ti);
                }
            break;
        case Kind::Sheep:  // W4M CAIPlanAttackAnimal 0x4a4210: the payload's blast where the walker reaches the target
        case Kind::OldWoman:
        case Kind::Scouser: {
            if (!isWorm) break;
            for (int c = 0; c * Walk::STEPS * DT < wd.fuse; c++) {  // one candidate per STEPS: a 30 s Old Woman walk is ~8 frame budgets
                if (!pick()) continue;
                if (c == 0) walk.p = walk.best = muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), walk.v = flat(yawE) * wd.speed, walk.i = 0;
                sheepWalk(g, wd, walk, e, (c + 1) * Walk::STEPS);
                if ((c + 1) * Walk::STEPS * DT >= wd.fuse && Vector3Distance(walk.best, e) < 2) consider(shell(walk.best) + punch(g, wd.radius, feetOf(g, ti), ti), yawE, w.pitch, 0, ti);
            }
            break;
        }
        case Kind::SuperSheep:  // and the Starburst: W4M CAIPlanAttackStarburst 0x4a43a0 adds its rider's death (0x49ed30, all its hp, knock 1)
            if (wd.name == "Super Sheep") {
                if (pick()) project(ti);
                break;
            }
            for (float pitch : {0.3f, 0.9f}) {
                if (!pick()) continue;
                Vector3 out;
                if (!superFly(g, wd, w.pos, yawE, pitch, e, out) || Vector3Distance(out, e) >= 2) continue;
                Outcome o(g, L, me, rating, vals);
                o.blast(out, blastOf(wd, false), L.threat);
                o.hurt(me, (float)w.hp, 1);  // Worm.Vapourize
                consider(o.s + bonus(false) + punch(g, wd.radius, feetOf(g, ti), ti), yawE, pitch, 0, ti);
            }
            break;
        case Kind::Mine: {  // W4M CAIPlanAttackLandmine: CloseRangeExplosive 0x4a2c70, the blast scored where it is laid
            Vector3 hit;
            if (!isWorm || !pick() || !ground(g, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), hit) || !inBlast(hit, ti, Game::MINE_BLAST)) break;
            Outcome o(g, L, me, rating, vals);
            o.blast(hit, Game::MINE_BLAST, L.threat, true);  // as a dropped shell: retreat() walks clear
            consider(o.s + bonus(true), yawE, 0, 0, ti);
            break;
        }
        case Kind::Flood:  // W4M CAIPlanAttackFlood 0x4a3640: every target under Water.Level + Flood.Delta scores a kill (damage 1000, no knock)
            if (k != 0 || !pick()) break;  // one plan, whatever the target
            {
                Outcome o(g, L, me, rating, vals);
                float level = g.water + wd.speed;
                for (size_t i = 0; i < g.worms.size(); i++)
                    if (g.worms[i].alive && g.worms[i].pos.y < level) o.hurt((int)i, 1000, 0);
                consider(o.s + bonus(false), w.yaw, w.pitch, 0, ti);
            }
            break;
        case Kind::Airstrike:  // W4M CAIPlanAttackStrike 0x4a11d0: one blast at the target (set from the Blimp, act()), radius
        case Kind::Donkey:     // BlitzDuration 2 s x GroundSpeed + 2 WormDamageRadius, the payload's WormDamageMagnitude
            if (pick()) {
                auto score = [&](bool miss) {  // 0x4a11d0 at the target, before the heading search (IsPossible); a best-miss knocks nothing
                    Blast b = blastOf(wd, wd.kind == Kind::Airstrike);
                    b.reach = 2 * Game::BOMBER_SPEED + 2 * b.reach;
                    Outcome oc(g, L, me, rating, vals);
                    oc.blast(feetOf(g, ti), b, miss ? 0 : L.threat);
                    return oc.s + bonus(false) + punch(g, wd.radius, feetOf(g, ti), ti);
                };
                float view = 0;
                std::optional<Vector3> bomb;
                if (strikeHeading(g, L, wd, ti, vals, rating, away, g.landTop(), view, false, bomb)) {
                    consider(score(false), w.yaw, w.pitch, 0, ti, view);  // W4M strike plans queue no worm orientation (flags 0x170)
                    break;
                }
                // 0x49be00: the refused bomb met land 50+ units from the shooter on destructible land: a clone at the same target (flag 0x10)
                // whose heading search skips the check (+0x3c)
                if (!bomb || Vector3DistanceSqr(*bomb, w.pos - Vector3{0, R, 0}) < 2500 / (UNIT * UNIT) || g.landLocked()) break;
                float s0 = score(false), s1 = score(true);
                const float r0 = rankOf(g, wi, ti, oi, s0);
                strikeHeading(g, L, wd, ti, vals, rating, away, g.landTop(), view, true, bomb);
                Plan c{wi, 0, ti, w.yaw, w.pitch, s1, 0, oi, view, feetOf(g, ti)};
                c.miss = true, c.rank = rankOf(g, wi, ti, oi, c.score);
                Choice spawn{r0, {}, c};
                spawn.spawn = true;
                pending.push_back(spawn);
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
    auto same = [&](int t, Vector3 at, int hp) { if (t >= (int)targetCount(g)) return false; const Vector3 p = targetPos(g, t); return targetHp(g, t) == hp && p.x == at.x && p.y == at.y && p.z == at.z; };
    if (recent.weapon >= 0 && same(recent.target, recent.at, recent.hp)) failed.push_back(recent);
    recent.weapon = -1;
    for (Fail &f : failed) f.effect = same(f.target, f.at, f.hp) ? f.effect * 0.99f : 0;
    failed.erase(std::remove_if(failed.begin(), failed.end(), [](const Fail &f) { return f.effect < 0.1f; }), failed.end());
    for (Shot &s : memory) s.effect *= 0.95f;
    memory.erase(std::remove_if(memory.begin(), memory.end(), [](const Shot &s) { return s.effect < 0.1f; }), memory.end());
}

// Shot is ready: W4M 0x4a4580 scales each launch-velocity component by 1 ± ShotError / (1 + MemoryImproveAccuracyEffect · match)
// (0x4a5d00, game-derived seed); then 0x49e6d0 queues the path before, the weapon and, under plan flag 0x100, the path after.
void Ai::finish(const Game &g) {
    const Level &L = levelOf(g);
    const Kind k = WEAPONS[plan.weapon].kind;
    const Origin &O = origins[plan.origin];
    uint32_t r = (salt ^ (uint32_t)g.shotsLeft * 2654435761u) + (uint32_t)walks * 40503u;  // turn-start state: not the think's length
    Vector3 at = plan.target >= 0 ? targetPos(g, plan.target) : O.pos;
    float match = 0;  // W4M 0x4a5640: effect x (1 - d_from / R)(1 - d_at / R) within MatchRadius R 200 units, any worm's records
    for (const Shot &s : memory) {
        float a = Vector3Distance(s.from, O.pos), b = Vector3Distance(s.at, at);
        if (a < 10 && b < 10) match += s.effect * (1 - a / 10) * (1 - b / 10);
    }
    memory.push_back({O.pos, at, 1});
    if (plan.target >= 0) recent = {plan.weapon, g.current, plan.target, at, targetHp(g, plan.target), 1};  // StorePlanAttackMemory 0x4a6360
    const float recall = 1 + L.memory * match;
    strikeOff = {};
    if (k == Kind::Airstrike || k == Kind::Donkey) {  // W4M 0x4a1b00: target + ShotErrorStrike·(2r-1) per component, no accuracy memory
        const float ex = noise(r) * L.strikeErr;
        noise(r);  // the y offset: bombs fall straight down
        strikeOff = {ex, 0, noise(r) * L.strikeErr};
    }
    else if (k != Kind::Melee && k != Kind::Mine && k != Kind::Flood && !dropped(WEAPONS[plan.weapon])) {  // direct weapons aim statically: ShotErrorDirectNonStrafe
        float e = (k == Kind::Shotgun ? L.directErr : L.shotErr) / recall;
        Vector3 v = dirOf(plan.yaw, plan.pitch) * (powered(WEAPONS[plan.weapon]) ? plan.charge / 90.0f : 1);
        v = {v.x * (1 + e * noise(r)), v.y * (1 + e * noise(r)), v.z * (1 + e * noise(r))};
        float sp = Vector3Length(v);
        plan.yaw = atan2f(v.x, v.z);
        plan.pitch = Clamp(asinf(Clamp(v.y / sp, -1, 1)), -1.2f, 1.45f);
        if (powered(WEAPONS[plan.weapon])) plan.charge = (int)Clamp(roundf(sp * 90), 1, 90);
    }
    mode = Mode::Act;
    charged = aimed = 0;
    afterPath.clear(), afterRoute = {}, retreatTaken = false;
    // W4M flag 0x100 (plan constructors 0x49cb00..0x49dbb0): projectile, homing, direct, strike, close-range explosive, Old Woman and
    // Scouser retreat; melee, Sheep, Super Sheep, Starburst and Flood do not
    // a gun that may move between shots keeps its retreat path for the last shot only (0x4a0cb4)
    const bool retreats = k != Kind::Melee && k != Kind::Sheep && k != Kind::SuperSheep && k != Kind::Flood && !(WEAPONS[plan.weapon].shots > 1 && g.shotsLeft != 1);
    if (retreats && O.path) afterPath = O.after, afterRoute = O.back;
    else if (retreats && O.close && moves) {
        // ours: W4M walks next to the target and back along its approach (0x4a22d0); ours sets it down where it stands, so it takes the
        // retreat 0x4a8c60 would pair with the worm's own node: the best def within ±2 entries, 50 iterations
        const Moves &mv = *moves;
        int best = mv.l0 * 441 + 220;
        for (int dr = -2; dr <= 2; dr++)
            for (int dc = -2; dc <= 2; dc++)
                for (int l = 0; l < 2; l++)
                    if (const int e = l * 441 + (10 + dr) * 21 + 10 + dc; mv.e[e].def > mv.e[best].def) best = e;
        if (best != mv.l0 * 441 + 220) {
            startSearch(g, {Route::Close, 50, pathLimit(thinkTimer), false, entryPos(g, *grid, *moves, best)});
        }
    }
    if (O.path) {  // W4M: Delay(DelayBeforeFirstMove, NonFirstMove after a rethink), the path, Delay 0.5 s, then the weapon
        walking = O.to, repaths = 0;
        takePath(g, std::vector<Step>(O.before));
        wait = moved ? (int)(L.nonFirstMove / DT) : 0;  // 0x4983a0: NonFirstMove while a path ran since the last shot (0x9560ba)
        walks++;
        moved = true, fireAfterWalk = true, mode = Mode::Walk;
    }
}

// A plan joining the list (W4M 0x4993a0, then the stable bubble sort 0x493cf0): popped before the plans it ties with
void Ai::insert(const Choice &c) {
    size_t i = choiceAt;
    while (i < choices.size() && choices[i].score > c.score) i++;
    choices.insert(choices.begin() + (long)i, c);
}

// W4M projectile IsPossible (0x49c340 .. 0x49c400): the plan's solve from its shooter (0x9560fc: the plan's +0x4c with a path, else the
// worm's feet); possible: the launch velocity, as yaw, pitch and the charge ticks reaching its speed. Impossible: 0x49be00 spawns the
// best-miss clone (+0x3c) at the solve's collision point when it is not one already, the point is 50+ units from the shooter and
// Land.Indestructable is 0; the Homing Missile (flag 0x10) keeps its point.
bool Ai::solve(const Game &g, Plan &p) {
    const WeaponDef &wd = WEAPONS[p.weapon];
    const Terrain &t = g.terrain;
    const Vector3 shooter = toUnits(t, origins[p.origin].pos - Vector3{0, R, 0});
    const int k = solverOf(wd);
    para.T = toUnits(t, p.at);
    Vector3 v{};
    bool ok;
    if (k == SHEEP) {  // 0x49c400: from the feet, Gravity only, speeds 0 .. 0.5 units/ms (0x4ac7b0), launched level toward the point
        const float dx = para.T.x - shooter.x, dz = para.T.z - shooter.z;
        if (sqrtf(dx * dx + dz * dz) < 200) return false;  // within 200 units: impossible, no best miss
        para.S = shooter, para.A = {0, -g.gravity() * UNIT / 1e6f, 0}, para.lo = 0, para.hi = 0.5f;
        if ((ok = arcSolve(t, para, 0.5f, 1, v))) p.yaw = atan2f(dx, dz), p.pitch = 0;
    } else {  // 0x4acbc0: from the eye (0x4ac670), the payload's gravity and whole wind, BasePower .. BasePower + MaxPower
        const bool wf = g.windy(p.weapon);
        para.S = shooter + Vector3{0, Game::EYE * UNIT, 0};
        para.A = Vector3{wf ? g.wind * Game::WIND_ACCEL : 0, -g.gravity() * wd.grav, wf ? g.windZ * Game::WIND_ACCEL : 0} * (UNIT / 1e6f);
        para.lo = launchSpeed(wd, 0) * UNIT / 1000, para.hi = wd.speed * UNIT / 1000;
        if (wd.name == "Banana Bomb") para.T.y += 10;  // 0x49c360: ClusterDistanceAboveTarget over its point
        ok = k == ARC ? arcSolve(t, para, 0, 1, v) : k == TIMED ? timedSolve(t, para, v, p.fuse) : bounceSolve(t, para, wd, shooter, toUnits(t, {0, g.water, 0}).y, v);
        if (ok) {
            const Vector3 m = v * (1000 / UNIT);  // m/s
            const float sp = Vector3Length(m), lo = launchSpeed(wd, 0);
            p.yaw = atan2f(m.x, m.z), p.pitch = asinf(Clamp(m.y / sp, -1, 1));
            p.charge = std::clamp((int)roundf((wd.base >= 0 ? (sp - lo) / (wd.speed - lo) : sp / wd.speed) * 90), 1, 90);  // ticks of charge
        }
    }
    if (ok || p.miss || !para.known || Vector3DistanceSqr(para.hit, shooter) < 2500 || g.landLocked()) return ok;
    Plan c = p;
    c.miss = true, c.fuse = 0;
    if (wd.kind != Kind::Homing) c.at = toMetres(t, para.hit);
    c.score = projScore(g, c), c.rank = rankOf(g, c.weapon, c.target, c.origin, c.score);
    insert({c.rank, {}, c, true});
    return false;
}

// W4M AIPlan best first (0x49a7d0): the best scored plan that is possible wins, a negative score never; a move plan is possible
// when its path is found. Path moves come only from A* edges (0x4b0c3c): never JETPACK, PARACHUTE or NINJA_ROPE.
void Ai::decide(const Game &g) {
    const Worm &w = g.worms[g.current];
    const int team = w.team;
    const Level &L = levelOf(g);
    if (choiceAt == 0 && choices.empty()) {
        const float left = thinkTimer * DT;  // the think's start: the plan must not depend on how it was sliced
        const float slow = left < L.moveTime ? left / L.moveTime : 1;  // ReduceMoveScoreIfTimeLeftLessThan (0x4a75c0)
        uint32_t r = salt * 2891336453u + (uint32_t)walks;
        auto taste = [&] { return 1 + L.randomise * noise(r); };
        if (plan.weapon >= 0) choices.push_back({plan.rank, {}, plan});
        choices.insert(choices.end(), pending.begin(), pending.end());
        if (moves) {
            const int limit = pathLimit(thinkTimer);
            for (const Object &o : g.objects) {  // W4M CAIPlanCollectCrate
                Vector3 d = o.pos - w.pos;
                if (o.type != Object::Crate || o.falling || fabsf(d.y) >= 3 || sqrtf(d.x * d.x + d.z * d.z) >= 18) continue;
                // AddScoreCollectSomething, health ×3 when poisoned or low; ×R/d past ReduceMoveScoreFurtherThan
                float s = L.collect * (o.weapon < 0 && o.mystery < 0 && (w.poison > 0 || w.hp < L.lowHp) ? 3 : 1), dist = Vector3Length(d);
                if (dist > L.moveFar) s *= L.moveFar / dist;
                choices.push_back({s * slow * taste(), {Route::Crate, 200, limit, false, o.pos}});
            }
            if (!moved) {
                for (size_t i = 0; i < targetCount(g); i++)  // W4M 0x4a88a0, 0x4a8ac0: 10 x the target's value, 500 iterations, a partial path
                    if (targetAlive(g, i) && vals[i] > 0) choices.push_back({10 * vals[i] * slow * taste(), {Route::Closer, 500, -1, true, targetPos(g, i)}});
                for (int k = 0; k < 5; k++) {  // W4M 0x4a7330: 5 nodes within RandomSmallMoveRange, score 0, 100 iterations
                    const float dx = noise(r) * SMALL_MOVE, dz = noise(r) * SMALL_MOVE;
                    const int i = grid->ci(w.pos.x + dx), j = grid->cj(w.pos.z + dz);
                    const NodeH a = nodeH(g, *grid, *moves, i, j, 0), b = nodeH(g, *grid, *moves, i, j, 1);
                    const NodeH &n = b.flag == 0 && fabsf(w.pos.y - R - b.hi) < fabsf(w.pos.y - R - a.hi) ? b : a;  // the node nearest (0x4af3a0)
                    if (n.flag == 0) choices.push_back({0, {Route::Random, 100, limit, false, nodePos(*grid, n, i, j)}});
                }
            }
        }
        std::stable_sort(choices.begin(), choices.end(), [](const Choice &a, const Choice &b) { return a.score > b.score; });
    }
    while (choiceAt < choices.size()) {
        Choice c = choices[choiceAt++];
        if (c.score < 0) break;  // "Considered impossible as forbidding any plan with a -ve score"
        if (c.route.purpose >= 0) return startSearch(g, c.route);  // the walk if found, else back here for the next plan
        if (c.spawn) {  // its IsPossible failed: the best-miss clone joins the list (0x4993a0), scored
            insert({c.plan.rank, {}, c.plan});
            continue;
        }
        if (c.lazy && !solve(g, c.plan)) return;  // one IsPossible per unit
        plan = c.plan, deciding = false, stage = -1;
        return finish(g);
    }
    deciding = false, stage = -1;
    if (g.shotsLeft) {  // 0x499ad0: locked with shots left and no plan: aim up (SetAimAngle 0x3fc90fdb), Delay 1000, fire again
        plan = Plan{g.weapon, 0, -1};
        plan.yaw = w.yaw, plan.pitch = Clamp(PI / 2, -1.2f, 1.45f);
        mode = Mode::Act, charged = 0, aimed = 1 << 20, wait = msTicks(1000);
        return;
    }
    int skip = owned(g, team, Kind::SkipGo);
    if (skip >= 0) { plan = Plan{skip, 0, -1}; plan.yaw = w.yaw; plan.pitch = w.pitch; mode = Mode::Act; }
    else wait = 1 << 20;  // "Want to skip turn but can't use the skip turn utility, so just delaying for a long time"
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
    if (g.shotsLeft && g.shotsLeft != shotsSeen) {  // after the shot the queue holds Delay 1000, Delay 100, Rethink (0x49e6d0, 0x499830)
        shotsSeen = g.shotsLeft;
        wait = msTicks(1100), stage = -1, mode = Mode::Eval;
        return in;
    }
    if (!select(g, plan.weapon, in)) return in;
    if (const int f = g.fuses[w.team]; plan.fuse > 0 && weaponDef(g.weapon).userFuse && f != plan.fuse) {  // W4M SetWeaponFuse (0x4b5510)
        if (!(g.prevButtons & (Input::FUSE_UP | Input::FUSE_DOWN))) in.buttons = plan.fuse > f ? Input::FUSE_UP : Input::FUSE_DOWN;
        return in;
    }
    Kind k = weaponDef(g.weapon).kind;
    float dy = wrapPi(plan.yaw - w.yaw), dp = plan.pitch - w.pitch;
    in.turn = q(dy / (2.5f * DT));
    in.aim = q(dp / (1.5f * DT));
    if (fabsf(dy) > 2e-3f || fabsf(dp) > 2e-3f) return in;
    if (aimed++ < levelOf(g).fireDelay / DT) return in;  // W4M DelayBeforeFire
    if (blimped(k) && !g.locked) return blimp(g);
    moved = false;  // W4M FireWeapon 0x496a42
    if (powered(weaponDef(g.weapon))) { if (k == Kind::Homing && !charged && g.prevButtons) return in; if (charged++ < plan.charge) in.buttons = Input::FIRE; }  // release fires
    else if (!g.prevButtons) in.buttons = Input::FIRE;
    return in;
}

// W4M SetStrikeTarget 0x4b4c70 / SetStrikeDirection 0x4b4bc0 set the target and heading with the Blimp camera on; ours turns the
// view to the heading and drives its cursor onto the target's feet, then FIRE confirms (a strike fires, a homing missile locks).
Input Ai::blimp(const Game &g) const {
    Input in;
    in.buttons = Input::TARGET;
    if (!g.cursorOn || plan.target < 0) return in;  // the first TARGET tick opens the view
    const Kind k = WEAPONS[plan.weapon].kind;
    const float dv = k == Kind::Airstrike || k == Kind::Donkey ? wrapPi(plan.view - g.cursorYaw) : 0;
    in.turn = q(dv / (Game::BLIMP_TURN * DT));
    const Vector3 t = plan.at + strikeOff;  // W4M SetWeaponTarget(+0x14)
    const float y = g.cursorYaw, back = (g.cursor.y - t.y) / tanf(g.cursorPitch), dx = t.x - sinf(y) * back - g.cursor.x,
                dz = t.z - cosf(y) * back - g.cursor.z, step = Game::CURSOR_SPEED * DT;
    in.walk = q((sinf(y) * dx + cosf(y) * dz) / step);  // Game::step's cursor axes
    in.aim = q((sinf(y) * dz - cosf(y) * dx) / step);
    if (dx * dx + dz * dz < 0.05f * 0.05f && fabsf(dv) < 2e-3f && !(g.prevButtons & Input::FIRE)) in.buttons |= Input::FIRE;
    return in;
}

// Retreat: the plan's path after the weapon, after the 1 s pause (0x49e6d0), while the shot flies too (W4M).
Input Ai::retreat(const Game &g) {
    Input in;
    if (search) { slice(g); return in; }  // a repath in progress
    if (++afterFire < 60 || !g.retreating()) return in;
    if (!retreatTaken) retreatTaken = true, walking = afterRoute, repaths = 0, takePath(g, std::move(afterPath));
    if (!follow(g, in) && pathFailed) repath(g);
    return in;
}

Input Ai::think(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    if (g.phase == Phase::Flying || (g.phase == Phase::Settle && !g.shots.empty())) {  // sheep: detonate next to an enemy; super sheep: autopilot
        if (g.phase == Phase::Flying && w.alive) in = retreat(g);
        for (const Projectile &s : g.shots) {
            Kind k = WEAPONS[s.weapon].kind;
            if (k == Kind::Airstrike && WEAPONS[s.weapon].fuse > 0 && !s.child) {  // bomber: head for the target, drop with the lead
                if (plan.target < 0 || !targetAlive(g, plan.target) || (!s.prey && s.stage > 0)) continue;  // held for STRIKE_LEAD
                Vector3 e = targetPos(g, plan.target) + strikeOff, land = s.pos + s.vel * (0.3f * (s.pos.y - e.y) / Game::COW_CHUTE);
                in.turn = q(wrapPi(yawTo(s.pos, e) - atan2f(s.vel.x, s.vel.z)) / (0.8f * DT));
                if (Vector2Distance({land.x, land.z}, {e.x, e.z}) < 1.5f && !g.prevButtons) in.buttons = Input::FIRE;
                continue;
            }
            // W4M queues DetonateWhenGoingAwayFrom for the Sheep only (flag 0x800, 0x49d848): no AI press for the Old Woman or Scouser
            if (s.child || (k != Kind::Sheep && k != Kind::SuperSheep) || plan.target < 0) continue;
            const Vector3 e = targetPos(g, plan.target), n = s.pos + s.vel * DT;
            bool near = k == Kind::SuperSheep && WEAPONS[s.weapon].walks && !s.stage;  // take off at once
            if (k == Kind::SuperSheep)
                for (const Worm &x : g.worms) near |= x.alive && g.alliance(x.team) != g.alliance(w.team) && Vector3Distance(s.pos, x.pos) < 1.5f;
            else near = Vector2Distance({n.x, n.z}, {e.x, e.z}) > Vector2Distance({s.pos.x, s.pos.z}, {e.x, e.z});  // 0x57e130: the next step goes away
            if (k == Kind::SuperSheep && targetAlive(g, plan.target)) steer(g, s.pos, WEAPONS[s.weapon].walks ? s.vel : s.aim, e, in);  // Starburst: its heading
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
        if (g.clock < lastClock) memory.clear(), failed.clear(), recent.weapon = -1, grid.reset(), para = Para{};  // new match
        lastClock = g.clock;
        if (!grid) grid = std::make_shared<const Grid>(aiGrid(g.terrain));
        pathFailed = moved = noMove = fireAfterWalk = retreatTaken = false, repaths = 0, walking = {}, blocked.clear();
        mode = Mode::Eval;
        stage = -1;
        search.reset(), moves.reset();
        origins.clear(), choices.clear(), pending.clear(), choiceAt = 0, deciding = false;
        path.clear(), afterPath.clear();
        debt = 0;
        uint32_t hp = 0;  // not g.clock: how long earlier thinks took must not change this turn's taste
        for (const Worm &x : g.worms) hp = hp * 31 + (uint32_t)x.hp;
        salt = (g.rng ^ hp * 2654435761u) + (uint32_t)g.current;
    }
    lastTimer = g.timer;
    if (wait > 0) { wait--; return in; }  // W4M DelayAtStart is 0: no pause before thinking
    if (g.jetting) return in;
    if (g.roped) { in.buttons = g.prevButtons ? 0 : Input::JUMP; return in; }
    switch (mode) {
    case Mode::Eval:
    case Mode::Search:
        if (!w.grounded || Vector3LengthSqr(w.vel) > 0.01f) return in;  // wait to stand still
        if (mode == Mode::Eval && stage < 0 && g.shotsLeft && g.tokens() > 1) return in;  // AIService 0x4b3e1d: ObjectCount.Active <= 2 with the gun's token
        if (mode == Mode::Eval && stage < 0) startEval(g);
        if (mode == Mode::Eval && stage < 0) return in;  // no enemy
        slice(g);
        return in;
    case Mode::Walk: {
        if (follow(g, in)) return in;
        const bool failed = pathFailed;
        if (failed && repath(g)) return in;
        wait = 30, stage = -1;  // W4M: Delay 0.5 s after a path, then the weapon, or think again after a move plan
        mode = fireAfterWalk && !failed ? Mode::Act : Mode::Eval, fireAfterWalk = false;
        return in;
    }
    case Mode::Act:
        if (search) { slice(g); return in; }  // the retreat path, before firing
        return act(g);
    }
    return in;
}

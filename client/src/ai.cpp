#include "ai.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

static constexpr float DT = Game::DT, R = Game::R;

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
};
static const Level LEVELS[5] = {
    {0.3f, 0.05f, 1, 0.05f, 0, 0, 1.5f, 0.2f, 0.8f, 0.5f, 2, 1000, 25, 5, 40, 0.2f, false, 1, 1, 0.9f, false, 0, 0, false, 0},
    {0.2f, 0.02f, 0.5f, 0.1f, 0.5f, 1, 1, 0.2f, 1, 0.5f, 0, 5000, 25, 10, 30, 1, true, 1, 1, 0.9f, false, 0.2f, 0.25f, false, 0.3f},
    {0.1f, 0.01f, 0.25f, 0.1f, 0.5f, 1, 0.5f, 0.1f, 1, 0.5f, 0, 20000, 25, 5, 30, 1, true, 1, 1, 0.9f, false, 0.1f, 0.25f, true, 5},
    {0.05f, 0.005f, 0.25f, 0.5f, 0.8f, 2, 0.1f, 0.2f, 1.2f, 0.5f, 0, 30000, 25, 5, 20, 1.5f, true, 0.8f, 0.5f, 0.8f, true, 0.05f, 0.5f, true, 1},
    {0, 0, 0, 0.5f, 1, 4, 0, 0, 1.8f, 0.2f, 0, 60000, 50, 5, 20, 1, true, 0.3f, 0.5f, 0.7f, true, 0, 0.25f, true, 1},
};
// Shared by all five levels (AITWK): AddScoreMove, AddScoreMoveIfNotMoved, RandomSmallMoveRange 100 units, StrikeSweetSpotDistance 2 units
static constexpr float MOVE_BONUS = 10, FIRST_MOVE_BONUS = 20, SMALL_MOVE = 5, STRIKE_SWEET = 0.1f;

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
    float speed = wormBody(g.terrain, b.pos, b.vel, b.grounded, b.motion, yaw, g.gravity(), g.pot, 0.3f, g.wormWind());
    b.fall += g.fallDamage(speed);
    return b.pos.y >= g.water;
}

// W4M 0x4a9260 target value, once per think (0x4ab730) from where the worm stands: allies (self included) are -v·K, enemies v/K,
// with K = (enemies / allies)^WormExchange
static float targetValue(const Game &g, const Level &L, int me, int i) {
    const Worm &w = g.worms[i];
    const int team = g.worms[me].team;
    int left = 0, foes = 0, friends = 0;
    for (const Worm &x : g.worms) if (x.alive) left += x.team == w.team, (x.team == team ? friends : foes)++;
    float K = powf((float)std::max(foes, 1) / std::max(friends, 1), L.exchange);
    float v = (1 + 0.04f * w.hp + fmaxf(0.1f, 1 - 0.08f * w.poison)) * (left == 1 ? 2 : 1);
    if (w.team == team) return -v * K;
    bool human = w.team >= (int)g.cfg.teamSetup.size() || !g.cfg.teamSetup[w.team].cpu;
    return v / K * powf(10 / fmaxf(Vector3Distance(w.pos, g.worms[me].pos), 10), L.nearby) * (human ? L.humans : 1);
}

// W4M plan scoring: 0x49f190 for a blast, 0x49ed30 for each worm it hurts. Every worm counts, self included, where it stands.
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
        s += d * attack * vals[i];
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

// --- rope race: exact copy of Game::step for the active worm, driven by a parametric swing policy ---

struct Mover { Body b; float yaw, pitch; bool roped; Rope rope; uint8_t prev; int jump = 0; uint8_t kind = 0; Vault vault{}; Hook hook{}; };

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
    if (!m.vault.t && !m.roped) m.yaw += in.turn / 127.0f * 2.5f * DT;
    const float ws = Game::WALK_SPEED * g.walkScale();
    if (m.vault.t) vaultStep(b.pos, m.vault, flat(m.yaw) * (float)in.walk);  // Game::step's vault
    else if (b.grounded && !b.motion.slide && in.walk && !m.jump) {  // Game::step's walk
        Vector3 walkV = flat(m.yaw) * (in.walk / 127.0f * Game::INPUT_IMPULSE);
        if (walkStep(g.terrain, b.pos, m.yaw, in.walk / 127.0f * ws * DT, &m.vault)) b.vel = walkV, b.motion.air = true;
        else if (!m.vault.t) slideIfSteep(g.terrain, b.pos, b.vel, b.motion, walkV, g.pot);
    }
    b.motion.input = flat(m.yaw) * (in.walk / 127.0f);
    Vector3 jv;  // Game::step's jump
    if ((pressed & Input::JUMP) && !m.jump && !m.vault.t && b.grounded && !b.motion.slide && !tool && !g.wp(WP_NO_JUMPING)) m.jump = Game::JUMP_WINDOW, m.kind = 2;
    else if (m.jump && Game::jumpTick(m.jump, m.kind, in.buttons, pressed, in.walk, m.yaw, jv) && b.grounded) b.vel = jv, b.grounded = false, b.motion.air = true;
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
        if (!m.hook.on) r.done = ++r.after > 20 || (r.after > 1 && m.b.grounded);
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
    Mover now{{w.pos, w.vel, w.grounded, 0, w.motion}, w.yaw, w.pitch, g.roped, g.rope, g.prevButtons, g.jumpDelay, g.jumpKind, g.vault, g.grapple};
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
            if (c.release >= 0 && !hooked) s = fminf(s, -1e19f);  // a miss gains nothing
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
    return {{w.pos, w.vel, w.grounded, 0, w.motion}, w.yaw, w.pitch, false, Rope{}, g.prevButtons, g.jumpDelay, g.jumpKind, g.vault};
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

// --- movement plans: W4M position scores 0x4aa6a0 over the ScoreAllMoveNodes window 0x4ab490, move -> retreat pairs 0x4a8c60 ---

// A node's heights (W4M 0x4aed70): flag 0 a node, 1 water or outside the grid (heights: the water level), 3 blocked (over 20 units apart)
struct NodeH { uint8_t flag = 1; float lo = 0, hi = 0; };

// W4M ScoreAllMoveNodes 0x4ab490: the 21 x 21 window at every other node around the worm's (±20 nodes), both layers. Per entry: def
// (0x4aa6a0 with arg5 1, the retreat spot score), open (arg6 1, the move spot score) and the retreat 0x4a8c60 paired it with.
struct Moves {
    struct Entry { float def = -1e6f, open = -1e6f; int r = -1; };
    int i0 = 0, j0 = 0, l0 = 0, at = 0, warm = 0;  // the worm's node; next entry to score; layer-0 rows probed ahead
    Entry e[2 * 21 * 21];                // [layer][row][col]: node (i0 + 2 (row - 10), j0 + 2 (col - 10), layer)
    std::unordered_map<int64_t, NodeH> heights;
    std::vector<int16_t> tops;  // per voxel column: its highest solid voxel, -2 not read yet
    long reads = 0;             // raw voxels read for them: work the samples don't count
    int i(int k) const { return i0 + 2 * (k % 441 / 21 - 10); }
    int j(int k) const { return j0 + 2 * (k % 21 - 10); }
};

static int colTop(const Terrain &t, Moves &mv, int x, int z) {
    if (x < 0 || z < 0 || x >= Terrain::NX || z >= Terrain::NZ) return -1;
    int16_t &c = mv.tops[(size_t)z * Terrain::NX + x];
    if (c == -2) {
        c = -1;
        for (int y = Terrain::NY - 1; y >= 0 && c < 0; y--, mv.reads++) if (t.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] > 0) c = (int16_t)y;
    }
    return c;
}

// One of W4M 0x4ade10's rays: from y down in half voxels to the first solid sample, <= the water if none. Layer 0 starts at the
// first step under every solid voxel `sample` can read there (same samples, fewer of them); layer 1 first leaves the land it starts in.
static float rayDown(const Game &g, Moves &mv, float x, float z, float y, bool under) {
    const float step = Terrain::VOX / 2;
    if (under) while (y > g.water && g.terrain.solid({x, y, z})) y -= step;
    else {
        const int ix = (int)floorf(x / Terrain::VOX), iz = (int)floorf(z / Terrain::VOX);
        const int t = std::max(std::max(colTop(g.terrain, mv, ix, iz), colTop(g.terrain, mv, ix + 1, iz)),
                               std::max(colTop(g.terrain, mv, ix, iz + 1), colTop(g.terrain, mv, ix + 1, iz + 1)));
        const float s = (t + 1) * Terrain::VOX;
        if (y >= s) y -= (floorf((y - s) / step) + 1) * step;
    }
    while (y > g.water && !g.terrain.solid({x, y, z})) y -= step;
    return y;
}

// W4M 0x4aed70: layer 0 probed from the land's top, layer 1 from 20 units under layer 0's lowest hit (water if layer 0 is);
// 3 x 3 rays at ±spacing/3 (0x4ade10), lo / hi = lowest / highest hit, any miss or water hit: water
static NodeH nodeH(const Game &g, const Grid &gr, Moves &mv, int i, int j, int layer) {
    const int64_t key = (((int64_t)i + (1 << 20)) * (1 << 21) + (j + (1 << 20))) * 2 + layer;
    if (auto it = mv.heights.find(key); it != mv.heights.end()) return it->second;
    NodeH h{1, g.water, g.water};
    float from = (Terrain::NY - 1) * Terrain::VOX;
    bool ok = gr.has(i, j);
    if (ok && layer) {
        const NodeH up = nodeH(g, gr, mv, i, j, 0);
        from = up.lo - 1, ok = up.flag != 1;
    }
    if (ok) {
        const Vector2 c = gr.at(i, j);
        float lo = 1e9f, hi = -1e9f;
        for (int a = -1; a <= 1 && ok; a++)
            for (int b = -1; b <= 1 && ok; b++) {
                const float y = rayDown(g, mv, c.x + a * gr.s / 3, c.y + b * gr.s / 3, from, layer > 0);
                if (y <= g.water) ok = false;
                lo = fminf(lo, y), hi = fmaxf(hi, y);
            }
        if (ok) h = {(uint8_t)(hi - lo > 1 ? 3 : 0), lo, hi};
    }
    return mv.heights[key] = h;
}

// W4M node position (0x4aeb00): mid-height, here the worm's centre over it
static Vector3 nodePos(const Grid &gr, const NodeH &h, int i, int j) { const Vector2 c = gr.at(i, j); return {c.x, (h.lo + h.hi) / 2 + R, c.y}; }

// W4M 0x4aa6a0 in its units (20 per m): def scores a retreat spot (arg5 1), open a move spot (arg6 1); terms in docs/w4m/ai.md §18
static void posScore(const Game &g, const Grid &gr, Moves &mv, const std::vector<float> &vals, float norm, int me, int i, int j, int layer,
                     float &def, float &open) {
    const NodeH n = nodeH(g, gr, mv, i, j, layer);
    const Vector2 c = gr.at(i, j);
    const Vector3 p = {c.x, (n.lo + n.hi) / 2, c.y};
    float mult = layer ? 5.0f : 1.0f, allies = 0, mines = 0, drums = 0;
    for (size_t k = 0; k < g.worms.size(); k++) {
        if (!g.worms[k].alive || vals[k] > 0 || (int)k == me) continue;
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
    if (g.worms[me].alive && vals[me] <= 0) mult *= -vals[me] / norm;
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
        v.y -= g.gravity() * DT;
        for (int k = 0, n = substeps(v); k < n; k++) {
            const Vector3 np = p + v * (DT / n);
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
    static int64_t key(int i, int j, int l) { return (((int64_t)i + (1 << 20)) * (1 << 21) + (j + (1 << 20))) * 2 + l; }
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
    void add(const Game &g, int i, int j, int l, Ai::Step st, Vector3 stand, int parent, int cost) {
        if (std::find(blocked.begin(), blocked.end(), grid->key(st.to)) != blocked.end()) return;
        if (!fits(g.terrain, stand)) return;  // ours: W4M Fits 0x59edf0 where the worm will stand
        auto it = index.find(key(i, j, l));
        if (it != index.end()) {
            Node &o = nodes[it->second];
            if (o.open && cost < o.g) o.step = st, o.parent = parent, o.g = cost;
            return;
        }
        index[key(i, j, l)] = (int)nodes.size();
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
        const Level &L = levelOf(g, g.worms[g.current].team);
        if (n.l != 0) return;
        for (int t = 0; t < 2; t++) {  // JUMP_FORWARD, JUMP_BACKFLIP when MovementJumpForward/BackflipAllowed (0x4924d7)
            if (!(t ? L.flip : L.jump)) continue;
            for (int d = 0; d < 8; d++) {
                const float yaw = atan2f((float)DX[d], (float)DZ[d]);
                const Vector3 from = nodePos(gr, here, n.i, n.j), f = {sinf(yaw), 0, cosf(yaw)};
                Vector3 land;  // Game::jumpTick: tapped forward, or a backflip facing away
                if (!jumpLand(g, from, t ? f * -Game::FLIP_BACK + Vector3{0, Game::FLIP_UP, 0} : f * Game::JUMP_FWD + Vector3{0, Game::JUMP_UP, 0}, land)) continue;
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
            const int k = pairR;
            const NodeH h = nodeH(g, gr, *moves, moves->i(k), moves->j(k), k / 441);
            search = newSearch(grid, moves, blocked, {Route::After, 50, pathLimit(thinkTimer), false, nodePos(gr, h, moves->i(k), moves->j(k))}, r.goal);
            return;
        }
        case Route::After:  // W4M 0x49faf0: both legs found: attacks from the move node; a failed leg blocks its node (0x4a9030)
            if (pairOk && ok) {
                Origin o;
                o.pos = pairAt, o.yaw = pairYaw, o.before = std::move(pairBefore), o.after = std::move(steps), o.path = true;
                o.weight = logf(1.1f * expf(pairScore));  // m_fRetreatScoreWeight (0x49fdd6), as W4M writes it
                const int m = pairM;
                o.to = {Route::Rewalk, 200, pathLimit(thinkTimer), false, nodePos(gr, nodeH(g, gr, *moves, moves->i(m), moves->j(m), m / 441), moves->i(m), moves->j(m))};
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
                wait = walks++ ? (int)(levelOf(g, g.worms[g.current].team).nonFirstMove / DT) : 0;  // DelayBeforeNonFirstMove
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
        s.index = {{Search::key(i, j, s.nodes[0].l), 0}};
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
    if (!threats.empty()) {  // threat rating; ProjectileSweetSpotDistance (0x4a9be0) aims that far on a threatened enemy's safe side
        const int i = threats.back();
        rating[i] = threatOf(g, g.worms[i], away[i]);
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
    stage = -1;
    decide(g);
}

// W4M CAIPlanAttackWithMoveAndRetreat::Spawn 0x49f8a0, up to 3 times: a move -> retreat pair (0x4a8c60), the path to the move node
// (200 iterations) and from it to the retreat (50); with no pair left, plans from where the worm stands, x ln 1.1 (no retreat score).
void Ai::nextPair(const Game &g) {
    if (pairs < 3) {
        if (moves && pairOf(*moves, pairM, pairR, pairScore)) {
            const int m = pairM;
            const NodeH h = nodeH(g, *grid, *moves, moves->i(m), moves->j(m), m / 441);
            startSearch(g, {Route::Before, 200, pathLimit(thinkTimer), false, nodePos(*grid, h, moves->i(m), moves->j(m))});
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
    const Level &L = levelOf(g, w.team);
    plan = Plan{};
    threats.clear(), origins.clear(), choices.clear(), choiceAt = 0, moves.reset(), landTop = -1;
    rating.assign(g.worms.size(), 0), away.assign(g.worms.size(), {}), vals.assign(g.worms.size(), 0);
    bool enemy = false;
    float sum = 0;
    int live = 0;
    for (size_t i = 0; i < g.worms.size(); i++) {
        if (!g.worms[i].alive) continue;
        threats.push_back((int)i);  // rated by unit()
        vals[i] = targetValue(g, L, me, (int)i), sum += fabsf(vals[i]), live++;
        enemy |= g.worms[i].team != w.team;
    }
    if (!enemy) return;
    norm = live && sum != 0 ? sum / live : 1;
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
    mv.tops.assign((size_t)Terrain::NX * Terrain::NZ, -2);
    mv.i0 = grid->ci(w.pos.x), mv.j0 = grid->cj(w.pos.z);
    const NodeH a = nodeH(g, *grid, mv, mv.i0, mv.j0, 0), b = nodeH(g, *grid, mv, mv.i0, mv.j0, 1);
    const float feet = w.pos.y - R;
    mv.l0 = b.flag != 1 && fabsf(feet - b.hi) < fabsf(feet - a.hi);  // the worm's node layer (0x4aeb70): the nearer height
}

// Targets from one origin: each enemy, the ground 1.5 m short of it, its sweet spot.
void Ai::targetsFor(const Game &g, const Origin &o) {
    const Level &L = levelOf(g, g.worms[g.current].team);
    tpos.clear(), tworm.clear();
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &e = g.worms[i];
        if (!e.alive || e.team == g.worms[g.current].team) continue;
        Vector3 to = e.pos - o.pos;  // also the ground just short of it: a shell there still splashes
        tpos.insert(tpos.end(), {e.pos, e.pos - Vector3Normalize({to.x, 0, to.z}) * 1.5f});
        tworm.insert(tworm.end(), {(int)i, (int)i});
        if (L.sweet > 0 && Vector3LengthSqr(away[i]) > 0) tpos.push_back(e.pos + away[i] * L.sweet), tworm.push_back((int)i);
    }
}

// W4M IsBomberWeapon (0x49e1e0): the bomb aimed at T, dropped from Land.MaxHeight + Bomber.ExtraHeight along D at GroundSpeed
// (BomberLogicEntity 0x54d460), must land within 10 units of it. Ours falls under the sim's gravity, land only.
static bool bombLands(const Game &g, const WeaponDef &wd, Vector3 T, Vector3 D, float top) {
    const float grav = g.gravity() * wd.grav, h = top + Game::STRIKE_EXTRA, lead = Game::BOMBER_SPEED * sqrtf(2 * fmaxf(h - T.y, 0) / grav);
    Vector3 p = {T.x - D.x * lead, h, T.z - D.z * lead}, v = D * Game::BOMBER_SPEED;
    for (int i = 0; i < 600 && p.y > g.water; i++) {
        v.y -= grav * DT;
        for (int k = 0, n = substeps(v); k < n; k++)
            if (p = p + v * (DT / n); g.terrain.solid(p)) return Vector3Distance(p, T) < 0.5f;
    }
    return false;
}

// W4M CAIPlanAttackStrike 0x4a13a0: the bomb line heading with the most value hit (docs/w4m/ai.md §18 "Strike heading"); false when
// the last heading checked by 0x49e1e0 was refused (impossible). view: the Blimp yaw that sets it.
static bool strikeHeading(const Game &g, const Level &L, const WeaponDef &wd, int target, const std::vector<float> &vals,
                          const std::vector<float> &rating, const std::vector<Vector3> &away, float top, float &view) {
    const Vector3 T = g.worms[target].pos;
    const float rb = blastOf(wd, wd.kind == Kind::Airstrike).reach, reach = 2 * Game::BOMBER_SPEED + 2 * rb, gap = 2 * Game::BOMBER_SPEED / 6;
    const bool bomber = wd.kind == Kind::Airstrike || wd.name == "Fatkins Strike";  // WEAPTWK IsBomberWeapon
    float best = -1e6f;
    bool ok = false;
    for (int a = 0; a < 360; a += 10) {
        const float r = a * PI / 180;
        const Vector3 D = {cosf(r), 0, -sinf(r)};
        float total = 0;
        for (size_t k = 0; k < g.worms.size(); k++) {
            const Worm &x = g.worms[k];
            if (!x.alive || Vector3Distance(x.pos, T) >= reach) continue;
            const Vector3 aim = rating[k] > 0 ? x.pos + away[k] * STRIKE_SWEET : x.pos;
            float sum = 0;
            for (int i = 0; i < 6; i++) {
                const Vector3 b = T + D * (gap * (i - 2.5f));
                const float d = Vector3Distance(b, aim), f = (rb - d) / rb, t = f * vals[k];
                if (d >= rb) continue;
                sum += t;
                if (f > 0 && f < 0.8f && L.thrust && i < 5) sum += Vector3DotProduct(D, Vector3Normalize(x.pos - b)) * t * 0.5f;
            }
            if (sum > 0) total += (int)k == target ? sum : sum * L.others;
        }
        if (total > best) {
            ok = !bomber || bombLands(g, wd, T - Vector3{0, R, 0}, D, top);
            if (ok) best = total, view = atan2f(D.x, D.z);  // W4M Airstrike.Direction is the Blimp view's forward
            else a += 30;
        }
    }
    return ok;
}

int Ai::evalWeapon(const Game &g, int wi, int only, int sub, const Origin &O, int oi) {
    const int me = g.current;
    Worm w = g.worms[me];
    w.pos = O.pos, w.yaw = O.yaw;  // W4M sets the worm position to the plan's (0x49b479) when it has a path
    const WeaponDef &wd = WEAPONS[wi];
    const int team = w.team;
    const Level &L = levelOf(g, team);
    const float wind = g.wind;  // W4M solves with the exact wind at every level
    if (!g.usable(team, wi) && !(g.shotsLeft && wi == g.weapon)) return 0;  // W4M SchemeData Delay: not before its turn
    if (w.nailed && !nailUsable(wd.kind)) return 0;  // Tail Nail: the sim refuses it
    static const char *BLIMPED[] = {"Homing Missile", "Airstrike", "Super Airstrike", "Concrete Donkey", "Fatkins Strike"};
    if (g.wp(WP_NO_BLIMP) && std::count_if(std::begin(BLIMPED), std::end(BLIMPED), [&](const char *b) { return wd.name == b; })) return 0;  // 0x90ea75 (0x4a03ab, 0x4a0f18)
    // W4M root plans (CloseRange: melee, dropped explosives, landmine; Special: Flood) stand where the worm is; Projectile, Direct,
    // Strike and Animal plans come from each move -> retreat pair, for targets over 100 units (5 m) from it (0x49fcf6)
    const bool close = wd.kind == Kind::Melee || wd.kind == Kind::Mine || wd.kind == Kind::Flood || (wd.kind == Kind::Shell && dropped(wd));
    if (close != O.close && !O.near) return 0;
    // W4M 0x498a0a, 0x49c060: a positive score ×(1 ± PlanScoreRandomise) ×Pref(weapon) ×(1 − PreferVariety if last turn's weapon)
    uint32_t h = (salt ^ (uint32_t)wi * 2654435761u) * 2246822519u;
    const std::string &n = wd.name;
    const float pref = n == "Prod" ? 0.3f : n == "Cluster Grenade" ? L.cluster : n == "Gas Canister" ? L.gas : wd.kind == Kind::Homing ? L.homing : 1;
    const float taste = (1 + L.randomise * ((h >> 8) / 16777216.0f * 2 - 1)) * pref * (wi == g.picked[team] ? 0.6f : 1);
    // W4M 0x49bd60: AddScoreMove, + AddScoreMoveIfNotMoved while no path ran since the last shot, for a plan with a path before or
    // always for a close-range explosive (0x4a2c70 forces it)
    auto bonus = [&](bool force) { return force || O.path ? MOVE_BONUS + (moved ? 0 : FIRST_MOVE_BONUS) : 0; };
    auto consider = [&](float score, float yaw, float pitch, int charge, int target, float view = 0) {
        score *= O.weight;  // W4M +0x58
        for (const Fail &f : failed)  // W4M 0x4a6590: this worm at a target that survived it, x(1 - effect / 2), another weapon 0.2 of that
            if (f.worm == me && f.target == target) score *= 1 - 0.5f * f.effect * (f.weapon == wi ? 1 : 0.2f);
        float rank = score > 0 ? score * taste : score;
        if (rank > plan.rank) plan = {wi, charge, target, yaw, pitch, score, rank, oi, view};
    };
    int cands = 0;
    auto pick = [&] { return sub < 0 || cands++ == sub; };  // sub >= 0: only that candidate, the rest are counted
    auto shell = [&](Vector3 at) {  // W4M CAIPlanAttackProjectile 0x4a0540, close-range explosive 0x4a2c70 for a dropped shell
        Outcome o(g, L, me, rating, vals);
        o.blast(at, blastOf(wd, false), L.threat, dropped(wd));  // ours: a dropped shell spares the thrower, it retreats
        return o.s + bonus(dropped(wd));
    };
    for (size_t k = only < 0 ? 0 : only; k < (only < 0 ? tpos.size() : only + 1); k++) {
        const Vector3 e = tpos[k];
        const int ti = tworm[k];
        if (!close && !O.near && Vector3Distance(g.worms[ti].pos, O.pos) <= 5) continue;
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
            const Game::GunHit hit = g.gunRay({muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), dirOf(yawE, pitch)}, g.worms[me]);
            if (hit.dist < 60 && pitch > -1.2f && pitch < 1.45f) {  // the sim's ray and hit (0x55e5da): on the struck worm, else the ray's end
                Outcome oc(g, L, me, rating, vals);
                oc.blast(hit.at, g.gunBlast(wi), 0);
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
                    Outcome oc(g, L, me, rating, vals);
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
                Outcome o(g, L, me, rating, vals);
                o.blast(out, blastOf(wd, false), L.threat);
                if (wd.name == "Starburst") o.hurt(me, (float)w.hp, 1);  // Worm.Vapourize
                consider(o.s + bonus(false), yawE, pitch, 0, ti);
            }
            break;
        case Kind::Mine: {  // W4M CAIPlanAttackLandmine: CloseRangeExplosive 0x4a2c70, the blast scored where it is laid
            Vector3 hit;
            if (!isWorm || !pick() || !ground(g, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), hit)) break;
            Outcome o(g, L, me, rating, vals);
            o.blast(hit, Game::MINE_BLAST, L.threat, true);  // as a dropped shell: retreat() walks clear
            consider(o.s + bonus(true), yawE, 0, 0, ti);
            break;
        }
        case Kind::Flood:  // W4M CAIPlanAttackFlood 0x4a3640: every target under Water.Level + Flood.Delta scores a kill (damage 1000, no knock)
            if (k != 0 || !pick()) break;  // one plan, whatever the target
            {
                Outcome o(g, L, me, rating, vals);
                float level = fminf(g.water + wd.speed, Terrain::WATER + 15);
                for (size_t i = 0; i < g.worms.size(); i++)
                    if (g.worms[i].alive && g.worms[i].pos.y < level) o.hurt((int)i, 1000, 0);
                consider(o.s + bonus(false), w.yaw, w.pitch, 0, ti);
            }
            break;
        case Kind::Airstrike:  // W4M CAIPlanAttackStrike 0x4a11d0: one blast at the target (set from the Blimp, act()), radius
        case Kind::Donkey:     // BlitzDuration 2 s x GroundSpeed + 2 WormDamageRadius, the payload's WormDamageMagnitude
            if (isWorm && pick()) {
                float view = 0;
                if (landTop < 0 && (wd.kind == Kind::Airstrike || wd.name == "Fatkins Strike")) landTop = g.landTop();
                if (!strikeHeading(g, L, wd, ti, vals, rating, away, landTop, view)) break;
                Blast b = blastOf(wd, wd.kind == Kind::Airstrike);
                b.reach = 2 * Game::BOMBER_SPEED + 2 * b.reach;
                Outcome oc(g, L, me, rating, vals);
                oc.blast(e, b, L.threat);
                consider(oc.s + bonus(false), w.yaw, w.pitch, 0, ti, view);  // W4M strike plans queue no worm orientation (flags 0x170)
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

// Shot is ready: W4M 0x4a4580 scales each launch-velocity component by 1 ± ShotError / (1 + MemoryImproveAccuracyEffect · match)
// (0x4a5d00, game-derived seed); then 0x49e6d0 queues the path before, the weapon and, under plan flag 0x100, the path after.
void Ai::finish(const Game &g, bool again) {
    const Worm &w = g.worms[g.current];
    const Level &L = levelOf(g, w.team);
    const Kind k = WEAPONS[plan.weapon].kind;
    const Origin &O = plan.origin >= 0 ? origins[plan.origin] : reaim;
    uint32_t r = (salt ^ (uint32_t)g.shotsLeft * 2654435761u) + (uint32_t)walks * 40503u;  // turn-start state: not the think's length
    Vector3 at = plan.target >= 0 ? g.worms[plan.target].pos : O.pos;
    float match = 0;  // W4M 0x4a5640: effect x (1 - d_from / R)(1 - d_at / R) within MatchRadius R 200 units, any worm's records
    for (const Shot &s : memory) {
        float a = Vector3Distance(s.from, O.pos), b = Vector3Distance(s.at, at);
        if (a < 10 && b < 10) match += s.effect * (1 - a / 10) * (1 - b / 10);
    }
    memory.push_back({O.pos, at, 1});
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
    if (again) return;  // the shotgun's next shot: same retreat
    afterPath.clear(), afterRoute = {}, retreatTaken = false;
    // W4M flag 0x100 (plan constructors 0x49cb00..0x49dbb0): projectile, homing, direct, strike, close-range explosive, Old Woman and
    // Scouser retreat; melee, Sheep, Super Sheep, Starburst and Flood do not
    const bool retreats = k != Kind::Melee && k != Kind::Sheep && k != Kind::SuperSheep && k != Kind::Flood;
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
            const NodeH h = nodeH(g, *grid, *moves, mv.i(best), mv.j(best), best / 441);
            startSearch(g, {Route::Close, 50, pathLimit(thinkTimer), false, nodePos(*grid, h, mv.i(best), mv.j(best))});
        }
    }
    if (O.path) {  // W4M: Delay(DelayBeforeFirstMove, NonFirstMove after a rethink), the path, Delay 0.5 s, then the weapon
        walking = O.to, repaths = 0;
        takePath(g, std::vector<Step>(O.before));
        wait = walks++ ? (int)(L.nonFirstMove / DT) : 0;
        moved = true, fireAfterWalk = true, mode = Mode::Walk;
    }
}

// W4M AIPlan best first (0x49a7d0): the best scored plan that is possible wins, a negative score never; a move plan is possible
// when its path is found. Path moves come only from A* edges (0x4b0c3c): never JETPACK, PARACHUTE or NINJA_ROPE.
void Ai::decide(const Game &g) {
    const Worm &w = g.worms[g.current];
    const int team = w.team;
    const Level &L = levelOf(g, team);
    if (choiceAt == 0 && choices.empty()) {
        const float left = thinkTimer * DT;  // the think's start: the plan must not depend on how it was sliced
        const float slow = left < L.moveTime ? left / L.moveTime : 1;  // ReduceMoveScoreIfTimeLeftLessThan (0x4a75c0)
        uint32_t r = salt * 2891336453u + (uint32_t)walks;
        auto taste = [&] { return 1 + L.randomise * noise(r); };
        if (plan.weapon >= 0) choices.push_back({plan.rank, {}});
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
                for (size_t i = 0; i < g.worms.size(); i++)  // W4M 0x4a88a0, 0x4a8ac0: 10 x the target's value, 500 iterations, a partial path
                    if (g.worms[i].alive && vals[i] > 0) choices.push_back({10 * vals[i] * slow * taste(), {Route::Closer, 500, -1, true, g.worms[i].pos}});
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
        const Choice &c = choices[choiceAt++];
        if (c.score < 0) break;  // "Considered impossible as forbidding any plan with a -ve score"
        if (c.route.purpose < 0) return finish(g);
        return startSearch(g, c.route);  // the walk if found, else back here for the next plan
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
    if (g.shotsLeft && g.shotsLeft != shotsSeen) {  // shotgun: re-aim the next shot at where things are now
        shotsSeen = g.shotsLeft;
        Plan keep = plan;
        plan = Plan{};
        plan.target = keep.target;
        reaim = Origin{}, reaim.pos = w.pos, reaim.yaw = w.yaw, reaim.near = true;
        targetsFor(g, reaim), built = -1;
        evalWeapon(g, g.weapon, -1, -1, reaim, -1);
        if (plan.weapon < 0) plan = keep;
        else finish(g, true);
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
    moved = false;  // W4M FireWeapon 0x496a42
    if (powered(k)) { if (k == Kind::Homing && !charged && g.prevButtons) return in; if (charged++ < plan.charge) in.buttons = Input::FIRE; }  // release fires
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
    const Vector3 t = g.worms[plan.target].pos + strikeOff - Vector3{0, R, 0};
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
            if (k == Kind::SuperSheep && g.worms[plan.target].alive) steer(g, s.pos, WEAPONS[s.weapon].walks ? s.vel : s.aim, e, in);  // Starburst: its heading
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
        pathFailed = moved = noMove = fireAfterWalk = retreatTaken = false, repaths = 0, walking = {}, blocked.clear();
        mode = Mode::Eval;
        stage = -1;
        search.reset(), moves.reset();
        origins.clear(), choices.clear(), choiceAt = 0;
        path.clear(), afterPath.clear();
        debt = 0;
        run = RopeRun{};
        raceAt = -1;
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

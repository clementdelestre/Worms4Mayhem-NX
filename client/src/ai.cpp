#include "ai.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

static constexpr float DT = Game::DT, R = Game::R;
static constexpr float FALL_SAFE = 15, FALL_SCALE = 2;  // copy of sim.cpp's W4M fall damage
static constexpr float WALK_OFF = 1;  // copy of sim.cpp: W4M Fall keeps the walk speed

static float grav(const Game &g) { return g.gravity(); }
static Vector3 dirOf(float yaw, float pitch) { return {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}; }
static Vector3 flat(float yaw) { return {sinf(yaw), 0, cosf(yaw)}; }
static float yawTo(Vector3 a, Vector3 b) { return atan2f(b.x - a.x, b.z - a.z); }
static float angle(float a) { return remainderf(a, 2 * PI); }
static int8_t q(float v) { return (int8_t)Clamp(roundf(v * 127), -127, 127); }

// W4M AITWK.XOM AIParams.CPU1..CPU5 (docs/w4m-map.md §18); distances at 20 W4M units per game unit.
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
    float speed = wormBody(g.terrain, b.pos, b.vel, b.grounded, b.motion, yaw, grav(g), g.cfg.wormpot);
    if (speed > FALL_SAFE && g.cfg.scheme.fallDamage) b.fall += (int)((speed - FALL_SAFE) * FALL_SCALE) + 1;
    return b.pos.y >= g.water;
}

// Where a knocked worm comes to rest: false if it drowns; `fall` gets the fall damage.
static bool fling(const Game &g, Vector3 p, Vector3 v, float &fall) {
    Body b{p, v, false};
    float yaw = 0;
    for (int i = 0; i < 300; i++) {
        if (!stepBody(g, b, yaw)) return false;
        if (b.grounded && Vector3LengthSqr(b.vel) < 0.04f) break;
    }
    fall = b.fall;
    return true;
}

// Predicted result of one action of the active worm: damage, knockback (falls, drowning), chained barrels/mines/crates,
// poison, karma and vampire, scored like W4M 0x49ed30 (see total()).
struct Outcome {
    const Game &g;
    const Level &L;
    int team, self;
    std::vector<float> dmg;
    std::vector<Vector3> kick;
    std::vector<float> pois;
    std::vector<char> hit, gone;
    float extra = 0;
    bool started = false;

    Outcome(const Game &g, const Level &L, int me) : g(g), L(L), team(g.worms[me].team), self(me), dmg(g.worms.size()),
        kick(g.worms.size()), pois(g.worms.size()), hit(g.worms.size()), gone(g.objects.size()) {}

    // share: chained blasts count WeightingExplosiveSecondaryDamage
    void blast(Vector3 p, const Blast &b, float poison = 0, int depth = 0, float share = 1) {  // Game::explode()
        float nearest = 100;
        for (size_t i = 0; i < g.worms.size(); i++) {
            const Worm &w = g.worms[i];
            float d = Vector3Distance(w.pos, p);
            if (w.alive && w.team != team) nearest = fminf(nearest, d);
            int hp = Game::blastDamage(b, p, w.pos);
            Vector3 k = Game::blastKick(b, p, w.pos);
            bool gassed = poison > 0 && d < b.reach + R;
            if (!w.alive || (!hp && !gassed && Vector3LengthSqr(k) == 0)) continue;
            dmg[i] += share * (w.armour ? hp * Game::ARMOUR / 100 : hp);
            kick[i] = kick[i] + k * (share * (w.armour ? 0.5f : 1));
            hit[i] = 1;
            if (gassed) pois[i] = fmaxf(pois[i], poison);
        }
        if (!started) { started = true; extra -= 0.05f * nearest; }  // tie-breaker: land near an enemy
        if (L.secondary <= 0 || depth > 2) return;
        for (size_t k = 0; k < g.objects.size(); k++) {
            const Object &o = g.objects[k];
            if (gone[k] || o.type == Object::Mine || Vector3Distance(o.pos, p) >= b.reach) continue;  // a mine is only pushed
            gone[k] = 1;
            if (o.type == Object::Barrel) blast(o.pos, Game::BARREL_BLAST, 0, depth + 1, share * L.secondary);
            else if (o.type == Object::Crate && o.weapon >= 0) blast(o.pos, Game::CRATE_BLAST, 0, depth + 1, share * L.secondary);
            else if (o.type == Object::Sentry) extra += o.team == team ? -15 : 15;
            else extra -= 5;  // health crate lost
        }
    }
    void strike(int i, float d, Vector3 vel) {  // melee / abduction: velocity is replaced
        dmg[i] += d;
        kick[i] = vel - g.worms[i].vel;
        hit[i] = 1;
    }
    // W4M 0x4a9260 target value: allies (self included) are −v·K, enemies v/K, with K = (enemies / allies)^WormExchange
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
    // 2·value·d, d = hp lost or max(hp, 200) on a kill; a predicted drowning moves d toward the kill by NearbyThreat/4 (assumed blend)
    float total() const {
        const Worm &me = g.worms[self];
        float s = extra, karma = 0, leech = 0;
        for (size_t i = 0; i < g.worms.size(); i++)
            if ((int)i != self && hit[i]) { karma += dmg[i] * 0.5f; if (g.worms[i].team != team) leech += dmg[i] * 0.5f; }
        if (!(g.cfg.rules & RULE_KARMA)) karma = 0;
        for (size_t i = 0; i < g.worms.size(); i++) {
            float own = (int)i == self ? karma : 0;
            if (!hit[i] && !own) continue;
            const Worm &w = g.worms[i];
            Vector3 v = w.vel + kick[i];
            float fall = 0;
            bool sunk = L.threat > 0 && Vector3LengthSqr(v) > 1 && !fling(g, w.pos, v, fall);
            float lost = fminf(w.hp, dmg[i] + fall + own), kill = fmaxf(w.hp, 200);
            float d = lost >= w.hp ? kill : sunk ? lost + fminf(1, L.threat / 4) * (kill - lost) : lost + 2 * fmaxf(0, pois[i] - w.poison);
            s += 2 * value((int)i) * d;  // poison: worth ~2 turns on a survivor
        }
        if (g.cfg.rules & RULE_VAMPIRE) s -= 2 * value(self) * 0.5f * fminf(leech, fmaxf(0, 200 - me.hp));
        return s;
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
        else v.y -= grav(g) * (child && wd.kind != Kind::Airstrike ? 1 : wd.grav) * DT;
        if (wd.wind || (g.cfg.wormpot & WP_WIND_ALL)) v.x += wind * Game::WIND_ACCEL * DT;
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
        walkerStep(g.terrain, p, v, grav(g));
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
    float dy = angle(atan2f(d.x, d.z) - yaw), dp = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)) - pitch;
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

// Game::target() for a hypothetical aim.
static Vector3 reticle(const Game &g, Vector3 pos, float yaw, float pitch) {
    Vector3 hit, dir = dirOf(yaw, pitch);
    if (g.terrain.raycast({pos + dir, dir}, 60, &hit)) return hit;
    Vector3 far = {pos.x + sinf(yaw) * 30, (Terrain::NY - 1) * Terrain::VOX, pos.z + cosf(yaw) * 30};
    if (g.terrain.raycast({far, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit)) return hit;
    return {far.x, g.water, far.z};
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
    b.vel.y -= grav(g) * DT;
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
        Vector3 walkV = flat(m.yaw) * (in.walk / 127.0f * ws);
        if (walkStep(g.terrain, b.pos, m.yaw, in.walk / 127.0f * ws * DT, &m.vault)) b.vel = walkV * WALK_OFF, b.motion.air = true;
        else if (m.vault.t) m.vault.vel = walkV;
        else slideIfSteep(g.terrain, b.pos, b.vel, b.motion, walkV, g.cfg.wormpot);
    }
    b.motion.input = flat(m.yaw) * (in.walk / 127.0f);
    Vector3 jv;  // Game::step's jump
    if ((pressed & Input::JUMP) && !m.jump && !m.vault.t && b.grounded && !b.motion.slide && !tool && !(g.cfg.wormpot & WP_NO_JUMPING)) m.jump = Game::JUMP_WINDOW, m.kind = 2;
    else if (m.jump && Game::jumpTick(m.jump, m.kind, in.buttons, pressed, in.walk, m.yaw, jv) && b.grounded) b.vel = jv, b.grounded = false, b.motion.air = true;
    Vector3 push = Vector3Scale({sinf(m.yaw), 0, cosf(m.yaw)}, in.walk / 127.0f * DT);
    if (m.roped) {
        if (pressed & Input::JUMP) m.roped = false;
        m.len = Clamp(m.len - in.aim / 127.0f * 6 * DT, 1, ropeMax);
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
        in.turn = q(angle(yawTo(m.b.pos, finish) - m.yaw) / (2.5f * DT));
        in.walk = 127;
        if (r.t % 30 == 0) in.buttons = Input::JUMP;
        r.done = r.t >= 60;
        return in;
    }
    if (m.roped) {
        in.walk = 127;
        in.aim = p.reel ? 127 : 0;
        in.turn = q(angle(p.yaw - m.yaw) / (2.5f * DT));
        if (++r.held >= p.release && !(m.prev & Input::JUMP)) { in.buttons = Input::JUMP; r.after = 0; }
        return in;
    }
    if (r.fired) {
        r.done = ++r.after > 20 || (r.after > 1 && m.b.grounded);
        return in;
    }
    float dy = angle(p.yaw - m.yaw), dp = p.pitch - m.pitch;
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

// W4M PopulatePathingNodes 0x4b2800: node spacing sqrt(Σ NodeGrid box areas / 16000), the boxes merged where they overlap (0x4ae320);
// ours: one box per island of land above the sea (0.5 m columns, 8-connected), overlapping boxes merged
static float nodeSpacing(const Terrain &t) {
    const int N = Terrain::NX / 2, M = Terrain::NZ / 2, y0 = (int)ceilf(Terrain::WATER / Terrain::VOX);
    std::vector<char> land(N * M, 0);
    for (int z = 0; z < M; z++)
        for (int x = 0; x < N; x++)
            for (int y = Terrain::NY - 1; y >= y0 && !land[z * N + x]; y--) land[z * N + x] = t.d[((size_t)(2 * z) * Terrain::NY + y) * Terrain::NX + 2 * x] > 0;
    struct Box { int x0, z0, x1, z1; };
    std::vector<Box> boxes;
    std::vector<int> stack;
    for (int i = 0; i < N * M; i++) {
        if (land[i] != 1) continue;
        Box b{N, M, -1, -1};
        stack = {i}, land[i] = 2;
        while (!stack.empty()) {
            int c = stack.back(), cx = c % N, cz = c / N;
            stack.pop_back();
            b = {std::min(b.x0, cx), std::min(b.z0, cz), std::max(b.x1, cx), std::max(b.z1, cz)};
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = cx + dx, nz = cz + dz;
                    if (nx >= 0 && nz >= 0 && nx < N && nz < M && land[nz * N + nx] == 1) land[nz * N + nx] = 2, stack.push_back(nz * N + nx);
                }
        }
        boxes.push_back(b);
    }
    for (bool merged = true; merged;) {  // W4M's merge passes
        merged = false;
        for (size_t a = 0; a < boxes.size() && !merged; a++)
            for (size_t c = a + 1; c < boxes.size() && !merged; c++) {
                Box &p = boxes[a], &q = boxes[c];
                if (p.x0 > q.x1 || q.x0 > p.x1 || p.z0 > q.z1 || q.z0 > p.z1) continue;
                p = {std::min(p.x0, q.x0), std::min(p.z0, q.z0), std::max(p.x1, q.x1), std::max(p.z1, q.z1)};
                boxes.erase(boxes.begin() + c), merged = true;
            }
    }
    float area = 0;  // m², the 1/20 unit scale cancels out
    for (const Box &b : boxes) area += (b.x1 - b.x0 + 1) * (b.z1 - b.z0 + 1) * 0.25f;
    return fmaxf(Terrain::VOX, sqrtf(area / 16000));
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
        float h = sqrtf(d.x * d.x + d.z * d.z), dy = angle(atan2f(d.x, d.z) - m.yaw), ws = Game::WALK_SPEED * (g.cfg.wormpot & WP_QUICK_WALK ? 2 : 1);
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
    float dy = angle(s.yaw + (s.move == 2 ? PI : 0) - m.yaw);  // a backflip leaves backwards
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
        float h0 = Vector2Length({before.vel.x, before.vel.z}), h1 = Vector2Length({m.b.vel.x, m.b.vel.z});
        if (s.move && !before.grounded && !m.b.grounded && h1 < h0 * 0.5f) return false;  // the arc hit a wall: not in W4M's jump reach table [assumed]
        rest = m.b.grounded && before.grounded && before.pos.x == m.b.pos.x && before.pos.y == m.b.pos.y && before.pos.z == m.b.pos.z &&
               Vector3LengthSqr(before.vel) == 0 && Vector3LengthSqr(m.b.vel) == 0;
    }
    return r.done && m.b.fall == 0 && !r.stuck && fits(g.terrain, m.b.pos);  // W4M 0x492510: edges only to walkable nodes
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
static int64_t cellKey(Vector3 p, float node) {  // cell and 1 m layer
    return ((int64_t)floorf(p.x / node) * 100003 + (int64_t)floorf(p.z / node)) * 1009 + (int)floorf(p.y) + 100;
}

struct Search {
    enum { Crate, Closer, Retreat };
    float node = 0.5f;  // W4M node spacing (nodeSpacing)
    std::vector<int64_t> blocked;  // W4M path-failed blockages (0x490551): nodes no edge may end on
    int cellOf(float v) const { return (int)floorf(v / node); }
    int64_t keyOf(Vector3 p) const { return cellKey(p, node); }
    int purpose = Crate, team = 0, limit = 0;  // limit: most ticks the path may take
    Vector3 boom{}, target{};                  // Retreat: the blast to flee; Closer: the worm to approach
    std::vector<std::pair<float, Vector3>> cands;
    size_t scored = 0, tries = 0;
    float here = -1e9f;  // the start's own score
    Mover root{};
    struct Node { Mover m; Ai::Step step; int parent, g, h, ticks; bool open; };
    std::vector<Node> nodes;
    std::unordered_map<int64_t, int> index;
    int iter = 0, best = 0, cur = -1, mv = 0;  // cur: node being expanded, mv: its next edge (move type x 8 + direction)
    uint8_t walked = 0;                        // directions cur could walk to: a diagonal needs both of its sides
    bool started = false;
    int h(Vector3 p) const {  // octile cells to the nearest goal
        int b = 1 << 20;
        for (const auto &c : cands) b = std::min(b, octile(cellOf(c.second.x) - cellOf(p.x), cellOf(c.second.z) - cellOf(p.z)));
        return b;
    }
    bool goal(Vector3 p) const {
        for (const auto &c : cands) if (cellOf(c.second.x) == cellOf(p.x) && cellOf(c.second.z) == cellOf(p.z) && fabsf(p.y - c.second.y) < 1.5f) return true;
        return false;
    }
    float score(const Game &g, Vector3 p) const {  // Closer: W4M CAIPlanMoveCloserToTarget, nearer is better on any safe spot
        if (purpose == Retreat) return haven(g, team, p, boom);
        float s = spot(g, team, p);
        return s < -1e8f ? s : -Vector3Distance(p, target);
    }
};

void Ai::startSearch(const Game &g, int purpose, Vector3 to) {
    auto s = std::make_shared<Search>();
    const Worm &w = g.worms[g.current];
    s->purpose = purpose, s->team = w.team, s->boom = lastBoom, s->target = to, s->root = moverOf(g), s->node = node, s->blocked = blocked;
    if (purpose == Search::Retreat) {  // the weapon's retreat time (W4M timer from PostLaunchDelay's end), after the 1 s pause
        const WeaponDef &wd = WEAPONS[plan.weapon];
        s->limit = g.retreatTicks(wd) + msTicks(wd.postLaunch) - 60;
    } else s->limit = (int)((thinkTimer * DT - 10) / DT);  // ForbidMoveIfWouldLeaveTimeLessThan 10 s
    if (purpose == Search::Crate) s->cands = {{0, to}}, s->scored = 1;
    else {  // W4M scores a 21 x 21 node window around the worm (0x4ab5bc), the retreat's too [assumed: same window]
        for (int i = -10; i <= 10; i++)
            for (int j = -10; j <= 10; j++) if (i || j) s->cands.push_back({0, {w.pos.x + i * node, w.pos.y + 5, w.pos.z + j * node}});
        s->here = s->score(g, w.pos) + 1;  // a gain of 1 m or one haven point [unverified threshold]
    }
    search = s;
    if (purpose != Search::Retreat) mode = Mode::Search;
}

void Ai::searchStep(const Game &g) {
    static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DZ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    Search &s = *search;
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
        s.index = {{s.keyOf(p), 0}};
        return;
    }
    if (s.cur < 0) {  // pop the open node with the lowest F = G + H (W4M 0x492d80)
        int bi = -1;
        for (size_t i = 0; i < s.nodes.size(); i++)
            if (s.nodes[i].open && (bi < 0 || s.nodes[i].g + s.nodes[i].h < s.nodes[bi].g + s.nodes[bi].h)) bi = (int)i;
        const Vector3 p = bi < 0 ? Vector3{} : s.nodes[bi].m.b.pos;
        if (bi >= 0 && s.goal(p)) return done(bi);
        if (bi >= 0 && s.iter++ < MAX_ITER) s.nodes[bi].open = false, s.cur = bi, s.mv = 0, s.walked = 0;
    }
    if (s.cur < 0) {  // W4M takes a partial path unless it is too short [minimum unverified]
        const Search::Node &b = s.nodes[s.best];
        Vector3 e = b.m.b.pos;
        if (s.purpose != Search::Crate && s.best > 0 && octile(s.cellOf(e.x) - s.cellOf(s.root.b.pos.x), s.cellOf(e.z) - s.cellOf(s.root.b.pos.z)) >= 20 &&
            s.score(g, e) > s.here)
            return done(s.best);
        s.tries++, s.started = false;
        return;
    }
    const int bi = s.cur;
    const Search::Node n = s.nodes[bi];
    const Vector3 p = n.m.b.pos;
    const int cx = s.cellOf(p.x), cz = s.cellOf(p.z);
    const Level &L = levelOf(g, s.team);
    // one edge per unit (W4M expands a node in one go; ours runs each edge in the sim): WALK, JUMP_FORWARD, JUMP_BACKFLIP x 8 directions
    while (s.mv < 24 && ((s.mv / 8 == 1 && !L.jump) || (s.mv / 8 == 2 && !L.flip))) s.mv++;  // MovementJumpForward/BackflipAllowed
    if (s.mv >= 24) { s.cur = -1; return; }
    static const int ORDER[8] = {0, 2, 4, 6, 1, 3, 5, 7};  // the sides of a diagonal first
    const uint8_t mv = (uint8_t)(s.mv / 8);
    const int d = mv ? s.mv % 8 : ORDER[s.mv % 8];
    s.mv++;
    if (n.ticks + 10 > s.limit) return;
    // W4M 0x492510: a diagonal walk only if both of its sides are walkable (ours: walked from this node [assumed equivalent])
    if (!mv && (d & 1) && !((s.walked >> ((d + 7) % 8) & 1) && (s.walked >> ((d + 1) % 8) & 1))) return;
    Step st{{(cx + DX[d] + 0.5f) * s.node, p.y, (cz + DZ[d] + 0.5f) * s.node}, atan2f((float)DX[d], (float)DZ[d]), mv};
    Mover m = n.m;
    int ticks;
    if (!runStep(g, m, st, ticks)) return;
    if (n.ticks + ticks + (int)(s.h(m.b.pos) / 10 * s.node / Game::WALK_SPEED / DT) > s.limit) return;  // can't reach a goal in time
    Vector3 e = m.b.pos;
    int ex = s.cellOf(e.x), ez = s.cellOf(e.z);
    if (ex == cx && ez == cz) return;
    if (!mv) s.walked |= 1 << d;
    if (std::find(s.blocked.begin(), s.blocked.end(), s.keyOf(e)) != s.blocked.end()) return;
    if (mv) st.to = e;  // a jump's landing, for MovementJumpError
    int cost = n.g + octile(ex - cx, ez - cz) + (mv == 1 ? 40 : mv == 2 ? 60 : 0);  // W4M 0x491fd8; jumps +40, backflips +60 (0x492008, 0x492003)
    Search::Node nn{m, st, bi, cost, s.h(e), n.ticks + ticks, true};
    auto it = s.index.find(s.keyOf(e));
    if (it != s.index.end()) {
        Search::Node &o = s.nodes[it->second];
        if (o.open && cost < o.g) o = nn;
        return;
    }
    s.index[s.keyOf(e)] = (int)s.nodes.size();
    s.nodes.push_back(nn);
    const Search::Node &b = s.nodes[s.best];
    if (nn.h < b.h || (nn.h == b.h && nn.g < b.g)) s.best = (int)s.nodes.size() - 1;
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
    pathFailed = true, blocked.push_back(cellKey(st.to, node)), path.clear();  // W4M "Adding path-failed blockage at"
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

// W4M 0x4a9e90: threat around a target, probed in 8 directions at 50..200 units (0x90ebf8), weight 2/(k+2) by range:
// water or no land 0.2, a drop over 30 units 0.05, mines 0.1 and other worms 0.05 within 100 units. Returns away from the worst.
// ponytail: the GameLogicService object term (0x4aa229, 0.05) is not ported [unverified: which objects]
static Vector3 awayFromThreat(const Game &g, const Worm &e) {
    static const float RANGE[4] = {2.5f, 5, 7.5f, 10};
    float worst = 0;
    int dir = -1;
    for (int d = 0; d < 8; d++) {
        Vector3 f = flat(d * PI / 4);
        float t = 0;
        for (int k = 0; k < 4; k++) {
            const float wgt = 2.0f / (k + 2);
            Vector3 p = e.pos + f * RANGE[k], hit;
            if (!g.terrain.raycast({{p.x, e.pos.y + 3, p.z}, {0, -1, 0}}, 30, &hit) || hit.y < g.water) t += 0.2f * wgt;
            else if (e.pos.y - R - hit.y > 1.5f) t += 0.05f * wgt;
            for (const Object &o : g.objects) if (o.type == Object::Mine) t += 0.1f * wgt * fmaxf(0, 1 - Vector3Distance(o.pos, p) / 5);
            for (const Worm &x : g.worms) if (x.alive && &x != &e) t += 0.05f * wgt * fmaxf(0, 1 - Vector3Distance(x.pos, p) / 5);
        }
        if (t > worst) worst = t, dir = d;
    }
    return dir < 0 ? Vector3{} : flat(dir * PI / 4) * -1.0f;
}

void Ai::unit(const Game &g) {
    if (search) return searchStep(g);
    if (!threats.empty()) {  // ProjectileSweetSpotDistance (0x4a9be0): also aim that far on a threatened worm's safe side
        const Worm &e = g.worms[threats.back()];
        Vector3 away = awayFromThreat(g, e);
        if (Vector3LengthSqr(away) > 0) tpos.push_back(e.pos + away * levelOf(g, g.worms[cur(g)].team).sweet), tworm.push_back(threats.back());
        threats.pop_back();
        return;
    }
    if (topX >= 0 && topX < Terrain::NX) {  // Game::landTop(), one row of columns per unit: the max does not depend on the order
        for (int z = 0; z < Terrain::NZ; z += 8)
            for (int y = Terrain::NY - 1; y * Terrain::VOX > top; y--)
                if (g.terrain.solid({topX * Terrain::VOX, y * Terrain::VOX, z * Terrain::VOX})) { top = y * Terrain::VOX; break; }
        topX += 8;
        return;
    }
    const int T = (int)tpos.size(), N = (int)WEAPONS.size() * T, team = g.worms[cur(g)].team;
    while (stage < N && !g.usable(team, stage / T)) stage++, sub = 0;  // unusable (or scheme-delayed) weapons are skipped for free
    if (stage < N) {
        int n = evalWeapon(g, stage / T, stage % T, sub);
        if (++sub >= n) sub = 0, stage++;
    }
    if (stage >= N) { stage = -1; selecting ? nextSelect(g) : decide(g); }
}

// W4M worm-select mode (0x4a4f2a, flag 0x9560c1): with a Worm Select in hand the CPU plans for every worm of its team and plays the best;
// a worm with no plan stores a skipped-turn memory (0x499cac -> 0x4a68c0), and scales that worm's plans by 1 / (1 + 5 Σ effect) (0x4a57c0)
float Ai::skipScale(int worm) const {
    float sum = 0;
    for (const Skip &k : skipped) if (k.worm == worm) sum += k.effect;
    return 1 / (1 + 5 * sum);
}

void Ai::nextSelect(const Game &g) {
    if (plan.weapon < 0 || plan.score <= 0) skipped.push_back({selWorms[selAt], 1});
    else if (selBest < 0 || plan.rank > selRank) selRank = plan.rank, selBest = selWorms[selAt];
    if (++selAt < selWorms.size()) return;  // stage -1: the next worm's eval
    selecting = false, selDone = true;
    if (selBest >= 0 && selBest != g.current) selTarget = selBest;  // pressed through Change Worm, then planned again as itself
}

// --- shot planning ---

// Game::strikeStart with Game::landTop() precomputed (slice by slice in unit()): the scan costs 300k samples.
static Vector3 strikeFrom(const Game &g, float top, const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) {
    float h = top + Game::STRIKE_EXTRA, lead = Game::BOMBER_SPEED * sqrtf(2 * fmaxf(h - tgt.y, 0) / (grav(g) * wd.grav));
    float back = lead + (wd.clusters - 1) / 2.0f * Game::STRIKE_GAP;
    vel = dir * Game::BOMBER_SPEED;
    return {tgt.x - dir.x * back, h, tgt.z - dir.z * back};
}

void Ai::startEval(const Game &g) {
    const Worm &cw = g.worms[g.current];
    int changer = owned(g, cw.team, Kind::ChangeWorm);
    if (!selDone && !selecting && changer >= 0 && !g.shotsLeft && !walks) {
        selWorms = {g.current};  // ours: the worms our Change Worm reaches with the ammo left, one use a step
        for (int k = 1, n = 0, ammo = g.ammo[cw.team][changer]; k < g.perTeam; k++) {
            int c = cw.team * g.perTeam + (g.current - cw.team * g.perTeam + k) % g.perTeam;
            if (g.worms[c].alive && (ammo < 0 || ++n <= ammo)) selWorms.push_back(c);
        }
        if (selWorms.size() > 1) selecting = true, selAt = 0, selBest = -1, selRank = -1e30f;
        else selDone = true;
    }
    me = selecting ? selWorms[selAt] : -1;
    const Worm &w = g.worms[cur(g)];
    const Level &L = levelOf(g, w.team);
    plan = Plan{};
    tpos.clear();
    tworm.clear();
    threats.clear();
    int nearest = -1;
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &e = g.worms[i];
        if (!e.alive || e.team == w.team) continue;
        if (nearest < 0 || Vector3Distance(e.pos, w.pos) < Vector3Distance(g.worms[nearest].pos, w.pos)) nearest = (int)i;
        Vector3 to = e.pos - w.pos;  // also the ground just short of it: a shell there still splashes
        tpos.insert(tpos.end(), {e.pos, e.pos - Vector3Normalize({to.x, 0, to.z}) * 1.5f});
        tworm.insert(tworm.end(), {(int)i, (int)i});
        if (L.sweet > 0) threats.push_back((int)i);  // its sweet spot is found by unit()
    }
    if (nearest < 0) return;
    plan.target = nearest;
    if (L.secondary > 0)  // barrels/crates whose blast reaches an enemy (a blast only pushes a mine, W4M)
        for (const Object &o : g.objects) {
            if (o.type == Object::Sentry || o.type == Object::Mine || (o.type == Object::Crate && o.weapon < 0)) continue;
            for (const Worm &e : g.worms)
                if (e.alive && e.team != w.team && Vector3Distance(e.pos, o.pos) < 7) { tpos.push_back(o.pos); tworm.push_back(int(&e - g.worms.data())); break; }
        }
    stage = sub = 0;
    thinkTimer = g.timer;
    if (!selecting || !selAt) regress(g);
    if (topX < 0) {  // once a turn: nothing carves the land before the shot
        top = 0, topX = Terrain::NX;
        for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == Kind::Airstrike && g.ammo[w.team][i]) topX = 0;
    }
}

int Ai::evalWeapon(const Game &g, int wi, int only, int sub) {
    const int me = cur(g);
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
    auto consider = [&](float score, float yaw, float pitch, int charge, int target) {
        for (const Fail &f : failed)  // W4M 0x4a6590: this worm at a target that survived it, x(1 - effect / 2), another weapon 0.2 of that
            if (f.worm == me && f.target == target) score *= 1 - 0.5f * f.effect * (f.weapon == wi ? 1 : 0.2f);
        if (selecting) score *= skipScale(me);
        float rank = score > 0 ? score * taste : score;
        if (rank > plan.rank) plan = {wi, charge, target, yaw, pitch, score, rank};
    };
    int cands = 0;
    auto pick = [&] { return sub < 0 || cands++ == sub; };  // sub >= 0: only that candidate, the rest are counted
    auto shell = [&](Vector3 at) {
        Outcome o(g, L, me);
        o.blast(at, blastOf(wd, false), wd.poison);
        if (wd.poison > 0 && wd.fuse > 0) o.blast(at, {0, Game::GAS_RADIUS - R, 0, 0, 0, 0}, wd.poison);  // gas cloud; ponytail: no wind drift
        if (wd.clusters) {  // expected bomblet share
            Blast c = blastOf(wd, true);
            c.reach *= 1.5f, c.damage *= wd.clusters * 0.4f;
            o.blast(at, c, 0, 3);
        }
        if (dropped(wd)) o.hit[o.self] = 0, o.dmg[o.self] = 0;  // retreat() walks clear while the fuse burns
        return o.total();
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
                Vector3 A = {wd.wind || (g.cfg.wormpot & WP_WIND_ALL) ? wind * Game::WIND_ACCEL : 0, -grav(g) * wd.grav, 0}, P = launchPoint(wd, w.pos, yawE);
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
        case Kind::Homing: {
            if (!isWorm) break;
            float pitch = atan2f(to.y - R - 0.2f, horiz);
            Vector3 tgt = reticle(g, w.pos, yawE, pitch), d = dirOf(yawE, pitch), out;
            if (Vector3Distance(tgt, e) > 2.5f || pitch < -1.2f) break;
            for (int n : {30, 60, 90}) {
                if (!pick()) continue;
                float pw = 0;
                for (int j = 0; j < n; j++) pw = fminf(1, pw + DT / 1.5f);
                if (fly(g, wd, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), d * launchSpeed(wd, pw), wind, false, out, &tgt)) consider(shell(out), yawE, pitch, n, ti);
            }
            break;
        }
        case Kind::Shotgun: {
            if (!pick()) break;
            float pitch = atan2f(to.y - (isWorm ? 0 : 0.3f), horiz);
            Vector3 d = dirOf(yawE, pitch), o = muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), hit;
            float dist = g.terrain.raycast({o, d}, 60, &hit) ? Vector3Distance(o, hit) : 60;
            int struck = -1;
            for (size_t i = 0; i < g.worms.size(); i++) {
                const Worm &x = g.worms[i];
                float t = Vector3DotProduct(x.pos - o, d);
                if (x.alive && &x != &w && t > 0 && t < dist && Vector3Distance(x.pos, o + d * t) < R + 0.1f) dist = t, struck = (int)i;
            }
            if (dist < 60 && pitch > -1.2f && pitch < 1.45f) {
                Outcome oc(g, L, me);
                for (int s = 0; s < wd.shots; s++) {
                    Blast b = blastOf(wd, false);
                    if (struck >= 0) b.damage = 0;
                    oc.blast(o + d * dist, b);
                    if (struck >= 0) oc.dmg[struck] += wd.damage, oc.hit[struck] = 1;
                }
                consider(oc.total(), yawE, pitch, 0, ti);
            }
            break;
        }
        case Kind::Melee:
            if (!isWorm || Vector3Distance(e, w.pos) > 3.5f) break;
            for (float dy : {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f})
                for (float pitch : {0.0f, 0.5f, 1.0f}) {
                    if (!pick()) continue;
                    Worm at = w;
                    at.yaw = yawE + dy;
                    const float yaw = at.yaw;
                    Vector3 v = dirOf(yaw, pitch) * wd.speed + Vector3{0, wd.bounce, 0};
                    Outcome oc(g, L, me);
                    for (size_t i = 0; i < g.worms.size(); i++) {
                        const Worm &x = g.worms[i];
                        if (x.alive && (int)i != me && meleeHits(at, x.pos, wd)) oc.strike((int)i, (int)wd.damage, v);
                    }
                    consider(oc.total(), yaw, pitch, 0, ti);
                }
            break;
        case Kind::Sheep:
        case Kind::OldWoman: {
            if (!isWorm || !pick()) break;
            Vector3 f = flat(yawE), end, p = sheepWalk(g, wd, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), f, e, end);
            if (Vector3Distance(p, e) < 2) consider(shell(p), yawE, w.pitch, 0, ti);
            break;
        }
        case Kind::SuperSheep:  // and the Starburst: W4M CAIPlanAttackStarburst 0x4a43a0 adds its rider's death (0x49ed30, all its hp)
            if (!isWorm) break;
            for (float pitch : {0.3f, 0.9f}) {
                if (!pick()) continue;
                Vector3 out;
                if (!superFly(g, wd, w.pos, yawE, pitch, e, out) || Vector3Distance(out, e) >= 2) continue;
                Outcome o(g, L, me);
                o.blast(out, blastOf(wd, false));
                if (wd.name == "Starburst") o.dmg[o.self] += w.hp, o.hit[o.self] = 1;  // Worm.Vapourize
                consider(o.total(), yawE, pitch, 0, ti);
            }
            break;
        case Kind::Mine: {  // W4M CAIPlanAttackLandmine: CloseRangeExplosive 0x4a2c70, the blast scored where it is laid
            Vector3 hit;
            if (!isWorm || !pick() || !ground(g, muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), hit)) break;
            Outcome o(g, L, me);
            o.blast(hit, Game::MINE_BLAST);
            o.hit[o.self] = 0, o.dmg[o.self] = 0;  // as a dropped shell: retreat() walks clear
            consider(o.total(), yawE, 0, 0, ti);
            break;
        }
        case Kind::Scouser: {  // W4M CAIPlanAttackScouser; ours follows our scouser: walks, swallows the first worm, drops it SCOUSER_FLOAT s on
            if (!isWorm || !pick()) break;
            Vector3 p = muzzle(g.terrain, w.pos, launchPoint(wd, w.pos, yawE)), v = flat(yawE) * wd.speed;
            int prey = -1;
            for (int i = 0; i * DT < wd.fuse && p.y > g.water - 2 && prey < 0; i++) {
                walkerStep(g.terrain, p, v, grav(g));
                for (size_t j = 0; j < g.worms.size() && prey < 0; j++)
                    if (g.worms[j].alive && (int)j != me && Vector3Distance(p, g.worms[j].pos) <= R + 0.4f) prey = (int)j;
            }
            if (prey < 0) break;
            float fall = 0, up = 1.2f * Game::SCOUSER_FLOAT;  // ponytail: the climb only, no wind drift
            bool lives = fling(g, g.worms[prey].pos + Vector3{0, up, 0}, {0, 0, 0}, fall);
            Outcome o(g, L, me);
            o.strike(prey, lives ? wd.damage + fall : 1000, g.worms[prey].vel);
            consider(o.total(), yawE, w.pitch, 0, ti);
            break;
        }
        case Kind::Flood:  // W4M CAIPlanAttackFlood 0x4a3640: every target under Water.Level + Flood.Delta scores a kill (damage 1000)
            if (k != 0 || !pick()) break;  // one plan, whatever the target
            {
                Outcome o(g, L, me);
                float level = fminf(g.water + wd.speed, Terrain::WATER + 15);
                for (size_t i = 0; i < g.worms.size(); i++)
                    if (g.worms[i].alive && g.worms[i].pos.y < level) o.strike((int)i, 1000, g.worms[i].vel);
                consider(o.total(), w.yaw, w.pitch, 0, ti);
            }
            break;
        case Kind::Airstrike:
        case Kind::Donkey:
            if (!isWorm) break;
            for (float dy : {0.0f, -0.25f, 0.25f})
                for (float pitch : {atan2f(to.y - R - 0.1f, horiz), 1.45f}) {
                    if (!pick()) continue;
                    float yaw = yawE + dy;
                    Vector3 tgt = reticle(g, w.pos, yaw, pitch), f = flat(yaw), out;
                    Outcome oc(g, L, me);
                    if (wd.kind == Kind::Airstrike && wd.fuse > 0) {  // steered bomber: think() drops the cows over the target
                        if (dy != 0 || pitch > 1.4f) continue;
                        for (int i = 0; i < std::min(wd.clusters, 2); i++) oc.blast(e - Vector3{0, R, 0}, blastOf(wd, true));
                    } else if (wd.kind == Kind::Donkey) {
                        // ponytail: scores its first impact twice, not the whole dig
                        Vector3 v = {0, -wd.speed, 0}, p = wd.name == "Fatkins Strike" ? g.fatkinsDrop(wd, tgt, f, v) : tgt + Vector3{0, 25, 0};  // sim use()
                        if (fly(g, wd, p, v, wind, false, out)) { oc.blast(out, blastOf(wd, false)); oc.blast(out, blastOf(wd, false)); }
                    } else {
                        Vector3 v, p = strikeFrom(g, top, wd, tgt, f, v);
                        for (int i = 0; i < wd.clusters; i++)  // sim: held STRIKE_LEAD at p, then bomb i leaves i STRIKE_TICKS on
                            if (fly(g, wd, p + v * (i * Game::STRIKE_TICKS * DT), v, wind, true, out)) oc.blast(out, blastOf(wd, true));
                    }
                    if (oc.started) consider(oc.total(), yaw, pitch, 0, ti);
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
    for (Skip &k : skipped) k.effect *= 0.9f;  // RegressSkippedTurnMemory 0x4a61b0
    skipped.erase(std::remove_if(skipped.begin(), skipped.end(), [](const Skip &k) { return k.effect < 0.1f; }), skipped.end());
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
    auto noise = [&] { r = r * 1664525u + 1013904223u; return ((r >> 8) / 16777216.0f) * 2 - 1; };
    Vector3 at = plan.target >= 0 ? g.worms[plan.target].pos : w.pos;
    float match = 0;  // W4M 0x4a5640: effect x (1 - d_from / R)(1 - d_at / R) within MatchRadius R 200 units, any worm's records
    for (const Shot &s : memory) {
        float a = Vector3Distance(s.from, w.pos), b = Vector3Distance(s.at, at);
        if (a < 10 && b < 10) match += s.effect * (1 - a / 10) * (1 - b / 10);
    }
    memory.push_back({w.pos, at, 1});
    if (plan.target >= 0) recent = {plan.weapon, g.current, plan.target, at, g.worms[plan.target].hp, 1};  // StorePlanAttackMemory 0x4a6360
    const float recall = 1 + L.memory * match;
    if (k == Kind::Airstrike || k == Kind::Donkey)  // ShotErrorStrike: target offset, here sideways only
        plan.yaw += atanf(noise() * L.strikeErr / recall / fmaxf(Vector3Distance(at, w.pos), 1));
    else if (k != Kind::Melee && k != Kind::Mine && k != Kind::Flood && !dropped(WEAPONS[plan.weapon])) {  // direct weapons aim statically: ShotErrorDirectNonStrafe
        float e = (k == Kind::Shotgun ? L.directErr : L.shotErr) / recall;
        Vector3 v = dirOf(plan.yaw, plan.pitch) * (powered(k) ? plan.charge / 90.0f : 1);
        v = {v.x * (1 + e * noise()), v.y * (1 + e * noise()), v.z * (1 + e * noise())};
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

// W4M CAIPlanMove: a crate beats the shot when it scores more; else walk closer or jetpack to a better spot, else the best shot.
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
    if (!moved && plan.target >= 0 && !w.nailed && left > 10) {  // no teleport: W4M paths never use it
        moved = true;
        float here = spot(g, team, w.pos) + 5, best = here;
        int jetpack = owned(g, team, Kind::Jetpack);
        if (jetpack >= 0) {
            for (int a = 0; a < 16; a++)
                for (float r : {6.0f, 12.0f, 18.0f}) {
                    Vector3 hit;
                    if (!g.terrain.raycast({w.pos + flat(a * PI / 8) * r + Vector3{0, 20, 0}, {0, -1, 0}}, 40, &hit)) continue;
                    float s = spot(g, team, hit + Vector3{0, R + 0.3f, 0});
                    if (s > best) { best = s; goal = hit + Vector3{0, R + 0.3f, 0}; }
                }
            if (best > here) { plan = Plan{jetpack, 0, plan.target}; plan.score = 0; walk = 600; mode = Mode::Act; return; }
        }
    }
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
    if (k == Kind::Jetpack) {
        if (!g.prevButtons) { in.buttons = Input::FIRE; mode = Mode::Jet; }
        return in;
    }
    float dy = angle(plan.yaw - w.yaw), dp = plan.pitch - w.pitch;
    in.turn = q(dy / (2.5f * DT));
    in.aim = q(dp / (1.5f * DT));
    if (fabsf(dy) > 2e-3f || fabsf(dp) > 2e-3f) return in;
    if (aimed++ < levelOf(g, w.team).fireDelay / DT) return in;  // W4M DelayBeforeFire
    if (powered(k)) { if (charged++ < plan.charge) in.buttons = Input::FIRE; }  // release fires
    else if (!g.prevButtons) in.buttons = Input::FIRE;
    return in;
}

// Jetpack flight to `goal`: thrust to clear the terrain, drift over, brake the descent, land and switch off.
Input Ai::jet(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    Vector3 d = goal - w.pos;
    float h = sqrtf(d.x * d.x + d.z * d.z), dy = angle(atan2f(d.x, d.z) - w.yaw);
    in.turn = q(dy / (2.5f * DT));
    if (fabsf(dy) < 0.5f && h > 0.8f) in.walk = (int8_t)(127 * fminf(1, h / 4));
    Vector3 hit;
    bool low = g.terrain.raycast({w.pos + flat(w.yaw) * 2, {0, -1, 0}}, 3, &hit) || g.terrain.solid(w.pos + flat(w.yaw) * 1.5f);
    if (g.fuel > Game::JET_DRY && ((h > 1.5f && (w.pos.y < goal.y + 3 || low)) || w.vel.y < -5)) in.buttons = Input::FIRE;
    if (h < 1.5f || --walk <= 0) in.buttons = 0, mode = Mode::Eval, stage = -1;  // W4M: no switch-off, it lands (or falls dry)
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
                Vector3 e = g.worms[plan.target].pos, land = s.pos + s.vel * (0.3f * (s.pos.y - e.y) / Game::COW_CHUTE);
                in.turn = q(angle(yawTo(s.pos, e) - atan2f(s.vel.x, s.vel.z)) / (0.8f * DT));
                if (Vector2Distance({land.x, land.z}, {e.x, e.z}) < 1.5f && !g.prevButtons) in.buttons = Input::FIRE;
                continue;
            }
            if (s.child || (k != Kind::Sheep && k != Kind::SuperSheep && k != Kind::OldWoman)) continue;
            bool near = k == Kind::SuperSheep && WEAPONS[s.weapon].walks && !s.stage;  // take off at once
            for (const Worm &e : g.worms) near = near || (e.alive && e.team != w.team && Vector3Distance(s.pos, e.pos) < (k == Kind::SuperSheep ? 1.5f : 1.2f));
            if (k == Kind::SuperSheep && plan.target >= 0 && g.worms[plan.target].alive) steer(g, s.pos, s.vel, g.worms[plan.target].pos, in);
            if (near && !g.prevButtons) in.buttons = Input::FIRE;
        }
        return in;
    }
    if (g.phase == Phase::Settle) mode = Mode::Eval;  // the turn is over: striking() must not show last turn's plan
    if (!w.alive) return in;
    if (g.phase == Phase::Retreat) return retreat(g);
    if (g.phase != Phase::Aim) return in;
    if (g.timer > lastTimer) selDone = selecting = false, selTarget = -1, me = -1;  // a new turn (a Change Worm keeps the selection)
    if (g.timer > lastTimer || g.current != worm) {
        worm = g.current;
        walk = walks = charged = aimed = wait = afterFire = shotsSeen = 0;
        if (g.clock < lastClock) memory.clear(), failed.clear(), skipped.clear(), recent.weapon = -1, node = 0;  // new match
        lastClock = g.clock;
        if (!node) node = nodeSpacing(g.terrain);
        moved = crateTried = closerTried = pathFailed = false, repaths = 0, lastPurpose = -1, blocked.clear();
        mode = Mode::Eval;
        stage = -1;
        search.reset();
        path.clear();
        debt = 0;
        run = RopeRun{};
        raceAt = topX = -1;
        lastBoom = w.pos;
        uint32_t hp = 0;  // not g.clock: how long earlier thinks took must not change this turn's taste
        for (const Worm &x : g.worms) hp = hp * 31 + (uint32_t)x.hp;
        salt = (g.rng ^ hp * 2654435761u) + (uint32_t)g.current;
    }
    lastTimer = g.timer;
    if (g.dropping()) return in;
    if (wait > 0) { wait--; return in; }  // W4M DelayAtStart is 0: no pause before thinking
    if (g.cfg.rules & RULE_ROPE_RACE) return race(g);
    if (selTarget >= 0) {  // W4M WormSelect action: ours presses Change Worm until the chosen worm is current
        int cw = owned(g, w.team, Kind::ChangeWorm);
        if (g.current == selTarget || cw < 0 || !g.worms[selTarget].alive) { selTarget = -1; return in; }
        if (select(g, cw, in) && !g.prevButtons) in.buttons = Input::FIRE;
        return in;
    }
    if (g.jetting) return mode == Mode::Jet ? jet(g) : Input{};  // drops back down; think again once landed
    if (g.roped) { in.buttons = g.prevButtons ? 0 : Input::JUMP; return in; }
    switch (mode) {
    case Mode::Eval:
    case Mode::Search:
        if (!w.grounded || Vector3LengthSqr(w.vel) > 0.01f) return in;  // wait to stand still
        if (mode == Mode::Eval && stage < 0 && !selDone)  // worm select plans for worms that are not the current one: all of them still
            for (const Worm &x : g.worms) if (x.alive && (!x.grounded || Vector3LengthSqr(x.vel) > 0.01f)) return in;
        if (mode == Mode::Eval && stage < 0) startEval(g);
        if (mode == Mode::Eval && tpos.empty()) return in;
        slice(g);
        return in;
    case Mode::Walk:
        if (follow(g, in)) return in;
        if (pathFailed && repath(g)) return in;
        wait = 30, mode = Mode::Eval, stage = -1, crateTried = closerTried = false;  // W4M: 0.5 s after a path, then think again
        return in;
    case Mode::Act:
        if (search) { slice(g); return in; }  // the retreat path, before firing
        return act(g);
    case Mode::Jet: if (g.jetUsed) mode = Mode::Eval, stage = -1; return in;  // waiting for the take-off; landed short: think again
    }
    return in;
}

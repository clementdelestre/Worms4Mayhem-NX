#include "ai.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

static constexpr float DT = Game::DT, R = Game::R;
static constexpr float FALL_SAFE = 10.5f, FALL_SCALE = 4;  // copies of sim.cpp's fall damage, jump and slide
static constexpr float JUMP_UP = 7.75f, JUMP_FWD = 3.1f, SLIDE_NY = 0.5f, SLIDE_FRICTION = 0.95f, WALK_OFF = 0.7f;

static float grav(const Game &g) { return 15 * ((g.cfg.rules & RULE_LOW_GRAVITY) ? 0.4f : 1.0f); }  // Game::gravity() is private
static Vector3 dirOf(float yaw, float pitch) { return {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}; }
static Vector3 flat(float yaw) { return {sinf(yaw), 0, cosf(yaw)}; }
static float yawTo(Vector3 a, Vector3 b) { return atan2f(b.x - a.x, b.z - a.z); }
static float angle(float a) { return remainderf(a, 2 * PI); }
static int8_t q(float v) { return (int8_t)Clamp(roundf(v * 127), -127, 127); }

static int levelOf(const Game &g, int team) {
    int l = team < (int)g.cfg.teamSetup.size() ? g.cfg.teamSetup[team].cpu : 0;
    return l ? std::min(l, 3) : 2;
}

static int owned(const Game &g, int team, Kind k) {
    for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == k && g.ammo[team][i]) return (int)i;
    return -1;
}

static bool touches(const Game &g, Vector3 p) {
    for (const Worm &w : g.worms) if (w.alive && Vector3Distance(p, w.pos) < R + 0.3f) return true;
    return false;
}

static bool outside(const Game &g, Vector3 p) {
    const float W = Terrain::NX * Terrain::VOX;
    return p.y < g.water - 2 || p.x < -20 || p.z < -20 || p.x > W + 20 || p.z > W + 20;
}

// Copy of Game::stepWorm without hp; false once drowned.
struct Body { Vector3 pos, vel; bool grounded; float fall = 0; };
static bool stepBody(const Game &g, Body &b) {
    auto feet = [&](float off) { return g.terrain.solid({b.pos.x, b.pos.y - R - off, b.pos.z}); };
    bool was = b.grounded;
    float fallSpeed = -b.vel.y;
    b.grounded = b.vel.y <= 0 && feet(0.05f);
    if (b.grounded) {
        if (!was && fallSpeed > FALL_SAFE && g.cfg.scheme.fallDamage) b.fall += (int)((fallSpeed - FALL_SAFE) * FALL_SCALE);
        Vector3 n = g.terrain.normal({b.pos.x, b.pos.y - R, b.pos.z});
        float keep = n.y < SLIDE_NY ? SLIDE_FRICTION : 0.85f;
        if (n.y < SLIDE_NY) b.vel.x += n.x * grav(g) * DT, b.vel.z += n.z * grav(g) * DT;
        b.vel = {b.vel.x * keep, 0, b.vel.z * keep};
    } else b.vel.y -= grav(g) * DT;
    Vector3 np = Vector3Add(b.pos, Vector3Scale(b.vel, DT));
    if (g.terrain.solid({np.x, b.pos.y, np.z})) { b.vel.x = b.vel.z = 0; np.x = b.pos.x; np.z = b.pos.z; }
    if (b.vel.y > 0 && g.terrain.solid({np.x, np.y + R, np.z})) { b.vel.y = 0; np.y = b.pos.y; }
    b.pos = np;
    for (int i = 0; i < 20 && feet(0); i++) {
        b.pos.y += 0.05f;
        if (b.vel.y < 0) b.vel.y = 0;
    }
    return b.pos.y >= g.water;
}

// Where a knocked worm comes to rest: false if it drowns; `fall` gets the fall damage.
static bool fling(const Game &g, Vector3 p, Vector3 v, float &fall) {
    Body b{p, v, false};
    for (int i = 0; i < 300; i++) {
        if (!stepBody(g, b)) return false;
        if (b.grounded && Vector3LengthSqr(b.vel) < 0.04f) break;
    }
    fall = b.fall;
    return true;
}

// Predicted result of one action of the active worm: damage, knockback (falls, drowning), chained barrels/mines/crates,
// poison, karma and vampire. Own team losses count double; kills get a bonus.
struct Outcome {
    const Game &g;
    int team, self;
    bool smart;
    std::vector<float> dmg;
    std::vector<Vector3> kick;
    std::vector<char> hit, gone;
    float extra = 0;
    bool started = false;

    Outcome(const Game &g, bool smart) : g(g), team(g.worms[g.current].team), self(g.current), smart(smart), dmg(g.worms.size()),
        kick(g.worms.size()), hit(g.worms.size()), gone(g.objects.size()) {}

    void blast(Vector3 p, float radius, float damage, float poison = 0, int depth = 0) {
        float reach = radius * 2, nearest = 100;
        for (size_t i = 0; i < g.worms.size(); i++) {
            const Worm &w = g.worms[i];
            float d = Vector3Distance(w.pos, p);
            if (w.alive && w.team != team) nearest = fminf(nearest, d);
            if (!w.alive || d >= reach) continue;
            float f = 1 - d / reach;
            dmg[i] += (int)(damage * f + 0.5f);
            kick[i] = kick[i] + Vector3Normalize(w.pos - p + Vector3{0, 1, 0}) * (14 * f);
            hit[i] = 1;
            if (poison > 0 && w.poison < poison) extra += (w.team == team ? -2 : 1) * poison * 2;
        }
        if (!started) { started = true; extra -= 0.05f * nearest; }  // tie-breaker: land near an enemy
        if (!smart || depth > 2) return;
        for (size_t k = 0; k < g.objects.size(); k++) {
            const Object &o = g.objects[k];
            if (gone[k] || Vector3Distance(o.pos, p) >= reach) continue;
            gone[k] = 1;
            if (o.type == Object::Barrel) blast(o.pos, 4, 50, 0, depth + 1);
            else if (o.type == Object::Mine) blast(o.pos, 3, 45, 0, depth + 1);
            else if (o.type == Object::Crate && o.weapon >= 0) blast(o.pos, 3, 35, 0, depth + 1);
            else if (o.type == Object::Sentry) extra += o.team == team ? -15 : 15;
            else extra -= 5;  // health crate lost
        }
    }
    void strike(int i, float d, Vector3 vel) {  // melee / abduction: velocity is replaced
        dmg[i] += d;
        kick[i] = vel - g.worms[i].vel;
        hit[i] = 1;
    }
    float total() const {
        const Worm &me = g.worms[self];
        float s = extra, karma = 0, leech = 0;
        for (size_t i = 0; i < g.worms.size(); i++) {
            if (!hit[i]) continue;
            const Worm &w = g.worms[i];
            Vector3 v = w.vel + kick[i];
            float fall = 0;
            bool sunk = smart && Vector3LengthSqr(v) > 1 && !fling(g, w.pos, v, fall);
            float lost = sunk ? w.hp : fminf(w.hp, dmg[i] + fall), val = lost + (lost >= w.hp ? 30 : 0);
            s += w.team == team ? -2 * val : val;
            if ((int)i != self) { karma += dmg[i] * 0.5f; if (w.team != team) leech += dmg[i] * 0.5f; }
        }
        if (g.cfg.rules & RULE_KARMA) s -= 2 * fminf(karma, me.hp) + (karma + dmg[self] >= me.hp ? 60 : 0);
        if (g.cfg.rules & RULE_VAMPIRE) s += 0.5f * fminf(leech, fmaxf(0, 200 - me.hp));
        return s;
    }
};

// Point copy of Game::stepShots for a ballistic or homing (aim != null) projectile; false when lost.
static bool fly(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 v, float wind, bool child, Vector3 &out, const Vector3 *aim = nullptr) {
    bool impact = child || wd.fuse == 0;
    float fuse = aim ? 0 : wd.fuse;
    for (int i = 0; i < 360; i++) {
        if (aim && (fuse += DT) > 0.4f) v = Vector3Lerp(v, Vector3Normalize(*aim - p) * wd.speed, 3 * DT);
        else v.y -= grav(g) * DT;
        if (wd.wind) v.x += wind * 6 * DT;
        Vector3 np = p + v * DT;
        out = np;
        if (g.terrain.solid(np)) {
            if (impact) return true;
            v = Vector3Reflect(v, g.terrain.normal(np)) * wd.bounce;
        } else p = np;
        if (impact && wd.kind != Kind::Donkey && touches(g, np)) return true;
        if (!impact && (fuse -= DT) <= 0) return true;
        if (outside(g, p)) return false;
    }
    return false;
}

// Copy of the sheep/old woman walk: closest approach to `e`, and where its fuse ends.
static Vector3 sheepWalk(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 f, Vector3 e, Vector3 &end) {
    Vector3 v = f * wd.speed, best = p;
    for (int i = 0; i * DT < wd.fuse && p.y > g.water - 2; i++) {
        bool ground = g.terrain.solid({p.x, p.y - 0.35f, p.z});
        v.y = ground && v.y <= 0 ? 0 : v.y - grav(g) * DT;
        Vector3 np = p + v * DT;
        float climb = 0;
        while (climb <= 0.6f && g.terrain.solid({np.x, np.y - 0.3f + climb, np.z})) climb += 0.05f;
        if (climb > 0.6f) { np.x = p.x; np.z = p.z; if (ground) v.y = 7; }
        else np.y += climb;
        if (g.terrain.solid({np.x, np.y + 0.3f, np.z})) { np = p; v.y = fminf(v.y, 0); }
        p = np;
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
    Vector3 d = dirOf(yaw, pitch), p = pos + d * 1.2f, v = d * wd.speed;
    for (float t = wd.fuse; t > 0; t -= DT) {
        Input in;
        steer(g, p, v, e, in);
        bool det = Vector3Distance(p, e) < 1.5f;
        float yw = atan2f(v.x, v.z) + in.turn / 127.0f * 2 * DT;
        float pt = Clamp(asinf(Clamp(v.y / fmaxf(Vector3Length(v), 0.01f), -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
        v = dirOf(yw, pt) * wd.speed;
        out = p + v * DT;
        if (det || g.terrain.solid(out) || touches(g, out)) return true;
        p = out;
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

struct Mover { Body b; float yaw, pitch; bool roped; Vector3 anchor; float len; uint8_t prev; int jump = 0; };

static bool stepRope(const Game &g, Mover &m) {
    Body &b = m.b;
    b.vel.y -= grav(g) * DT;
    Vector3 np = Vector3Add(b.pos, Vector3Scale(b.vel, DT)), d = Vector3Subtract(np, m.anchor);
    float l = Vector3Length(d);
    if (l > m.len) {
        Vector3 n = Vector3Scale(d, 1 / l);
        np = Vector3Add(m.anchor, Vector3Scale(n, m.len));
        float out = Vector3DotProduct(b.vel, n);
        if (out > 0) b.vel = Vector3Subtract(b.vel, Vector3Scale(n, out));
    }
    auto blocked = [&](Vector3 p) { return g.terrain.solid({p.x, p.y - R, p.z}) || g.terrain.solid({p.x, p.y + R, p.z}); };
    if (blocked(np)) { np.y = b.pos.y; b.vel.y = 0; }
    if (blocked(np)) b.vel = Vector3Scale(b.vel, -0.3f);
    else b.pos = np;
    return b.pos.y >= g.water;
}

static bool move(const Game &g, Mover &m, const Input &in, float ropeMax) {
    uint8_t pressed = in.buttons & ~m.prev;
    m.prev = in.buttons;
    Body &b = m.b;
    bool tool = m.roped;
    m.yaw += in.turn / 127.0f * 2.5f * DT;
    if (b.grounded && in.walk && !m.jump) {
        Vector3 np = {b.pos.x + sinf(m.yaw) * in.walk / 127.0f * 3 * DT, b.pos.y, b.pos.z + cosf(m.yaw) * in.walk / 127.0f * 3 * DT};
        float climb = 0;
        while (climb <= 0.6f && g.terrain.solid({np.x, np.y - R + climb, np.z})) climb += 0.05f;
        if (climb <= 0.6f && !g.terrain.solid({np.x, np.y + climb + R, np.z})) { np.y += climb; b.pos = np; }
        int i = 0;
        for (; i < 12 && !g.terrain.solid({b.pos.x, b.pos.y - R - 0.05f, b.pos.z}); i++) b.pos.y -= 0.05f;
        if (i == 12) b.vel = flat(m.yaw) * (in.walk / 127.0f * 3 * WALK_OFF);
    }
    // forward jump only: the policies never double-tap
    if ((pressed & Input::JUMP) && !m.jump && b.grounded && !tool) m.jump = Game::JUMP_WINDOW;
    else if (m.jump && --m.jump == 0 && b.grounded) { b.vel = {sinf(m.yaw) * JUMP_FWD, JUMP_UP, cosf(m.yaw) * JUMP_FWD}; b.grounded = false; }
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
    return m.roped ? stepRope(g, m) : stepBody(g, b);
}

// Aim, fire the rope, swing pushing forward for `release` ticks, let go and fly until landing.
static Input ropePolicy(const Mover &m, Vector3 finish, const Ai::RopePlan &p, Ai::RopeRun &r) {
    Input in;
    r.t++;
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
    Mover now{{w.pos, w.vel, w.grounded}, w.yaw, w.pitch, g.roped, g.anchor, g.ropeLen, g.prevButtons, g.jumpDelay};
    if (run.done) {
        float best = -1e30f, fy = yawTo(w.pos, fin);
        std::vector<RopePlan> cands = {{0, 0, -1}};
        for (float dy : {-0.2f, 0.0f, 0.2f})
            for (float pitch : {0.7f, 1.0f, 1.3f})
                for (int rel : {8, 20, 40, 65, 95, 125}) cands.push_back({fy + dy, pitch, rel});
        for (Vector3 o : {Vector3{0, 0, 0}, {1.5f, 1, 0}, {-1.5f, 1, 0}, {0, 1, 1.5f}, {0, 1, -1.5f}, {0, 3, 0}}) {  // climb: hook by the finish, reel in
            Vector3 to = fin + o - w.pos;
            for (int rel : {40, 90}) cands.push_back({atan2f(to.x, to.z), fminf(atan2f(to.y, sqrtf(to.x * to.x + to.z * to.z)), 1.45f), rel, true});
        }
        for (const RopePlan &c : cands) {
            Mover m = now;
            RopeRun r{};
            r.done = false;
            float s = 0;
            int i = 0;
            for (; i < 400 && i < g.timer - 1 && !r.done && s == 0; i++) {
                if (!move(g, m, ropePolicy(m, fin, c, r), ropeMax)) s = -1e20f;
                else if (Vector3Distance(m.b.pos, fin) < 2) s = 1e6f - i;
            }
            // then fall freely: the turn ended (rope dropped) or a new swing may still save a bad landing
            bool over = i >= g.timer - 1, rescue = !over && !m.b.grounded;
            float d = Vector3Distance(m.b.pos, fin), hy = m.b.pos.y;
            if (over) m.roped = false;
            for (int k = 0; k < 300 && s == 0 && !m.b.grounded; k++) {
                if (!move(g, m, Input{}, ropeMax)) s = rescue && hy > g.water + 5 ? -d - 40 : -1e20f;
                else if (Vector3Distance(m.b.pos, fin) < 2) s = 1e6f - i - k;
            }
            if (s == 0) s = -Vector3Distance(m.b.pos, fin) - m.b.fall * 0.3f;
            if (s > best) { best = s; rope = c; }
        }
        run = RopeRun{};
        run.done = false;
    }
    return ropePolicy(now, fin, rope, run);
}

// --- shot planning ---

void Ai::startEval(const Game &g) {
    const Worm &w = g.worms[g.current];
    const int level = levelOf(g, w.team);
    plan = Plan{};
    tpos.clear();
    tworm.clear();
    int nearest = -1;
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &e = g.worms[i];
        if (!e.alive || e.team == w.team) continue;
        if (nearest < 0 || Vector3Distance(e.pos, w.pos) < Vector3Distance(g.worms[nearest].pos, w.pos)) nearest = (int)i;
        Vector3 to = e.pos - w.pos;  // also the ground just short of it: a shell there still splashes
        tpos.insert(tpos.end(), {e.pos, e.pos - Vector3Normalize({to.x, 0, to.z}) * 1.5f});
        tworm.insert(tworm.end(), {(int)i, (int)i});
    }
    if (nearest < 0) return;
    plan.target = nearest;
    if (level == 3)  // barrels/mines/crates whose blast reaches an enemy
        for (const Object &o : g.objects) {
            if (o.type == Object::Sentry || (o.type == Object::Crate && o.weapon < 0)) continue;
            for (const Worm &e : g.worms)
                if (e.alive && e.team != w.team && Vector3Distance(e.pos, o.pos) < 7) { tpos.push_back(o.pos); tworm.push_back(int(&e - g.worms.data())); break; }
        }
    stage = 0;
}

void Ai::evalWeapon(const Game &g, int wi, int only) {
    const Worm &w = g.worms[g.current];
    const WeaponDef &wd = WEAPONS[wi];
    const int team = w.team, level = levelOf(g, team);
    const float wind = level > 1 ? g.wind : g.wind * 0.5f;  // level 1 half-guesses the wind
    const bool smart = level > 1;
    if (!g.ammo[team][wi] && !(g.shotsLeft && wi == g.weapon)) return;
    if (level == 1 && wd.count >= 0) return;
    auto consider = [&](float score, float yaw, float pitch, int charge, int target) {
        if (wd.count > 0) score -= 8;  // keep specials for good chances
        if (score > plan.score) plan = {wi, charge, target, yaw, pitch, score};
    };
    auto shell = [&](Vector3 at) {
        Outcome o(g, smart);
        o.blast(at, wd.radius, wd.damage, wd.poison);
        if (wd.clusters) o.blast(at, wd.cradius * 1.5f, wd.cdamage * wd.clusters * 0.4f, 0, 3);  // expected bomblet share
        return o.total();
    };
    if (wd.kind == Kind::Sentry) {
        if (only > 0) return;
        Vector3 f = flat(w.yaw), p = w.pos + f * 1.3f + Vector3{0, 0.3f, 0};
        if (g.terrain.solid(p)) return;
        float s = 0;
        for (const Worm &e : g.worms)
            if (e.alive && e.team != team && Vector3Distance(e.pos, p) < wd.radius && seen(g, p + Vector3{0, 0.4f, 0}, e.pos)) s += 15;
        if (s > 0) consider(s, w.yaw, w.pitch, 0, plan.target);
        return;
    }
    for (size_t k = only < 0 ? 0 : only; k < (only < 0 ? tpos.size() : only + 1); k++) {
        const Vector3 e = tpos[k];
        const int ti = tworm[k];
        const bool isWorm = Vector3Equals(e, g.worms[ti].pos);
        const Vector3 to = e - w.pos;
        const float yawE = atan2f(to.x, to.z), horiz = sqrtf(to.x * to.x + to.z * to.z);
        switch (wd.kind) {
        case Kind::Shell:
            // constant acceleration A: hit T at time t with V = (T - P - A t(t+DT)/2) / t (semi-implicit Euler)
            for (float t = 0.2f; t < 4.5f; t += 0.15f) {
                Vector3 A = {wd.wind ? wind * 6 : 0, -grav(g), 0}, P = w.pos, V{};
                for (int it = 0; it < 2; it++) {
                    V = (e - P - A * (0.5f * t * (t + DT))) / t;
                    P = w.pos + Vector3Normalize(V) * 1.2f;
                }
                float sp = Vector3Length(V), pitch = asinf(V.y / sp), yaw = atan2f(V.x, V.z);
                if (sp > wd.speed || sp < 0.15f * wd.speed || pitch < -1.2f || pitch > 1.45f) continue;
                int n = std::max(1, (int)roundf(sp / wd.speed * 90));
                float pw = 0;
                for (int j = 0; j < n; j++) pw = fminf(1, pw + DT / 1.5f);
                Vector3 d = dirOf(yaw, pitch), out;
                if (fly(g, wd, w.pos + d * 1.2f, d * (wd.speed * pw), wind, false, out)) consider(shell(out), yaw, pitch, n, ti);
            }
            break;
        case Kind::Homing: {
            if (!isWorm) break;
            float pitch = atan2f(to.y - R - 0.2f, horiz);
            Vector3 tgt = reticle(g, w.pos, yawE, pitch), d = dirOf(yawE, pitch), out;
            if (Vector3Distance(tgt, e) > 2.5f || pitch < -1.2f) break;
            for (int n : {30, 60, 90}) {
                float pw = 0;
                for (int j = 0; j < n; j++) pw = fminf(1, pw + DT / 1.5f);
                if (fly(g, wd, w.pos + d * 1.2f, d * (wd.speed * pw), wind, false, out, &tgt)) consider(shell(out), yawE, pitch, n, ti);
            }
            break;
        }
        case Kind::Shotgun: {
            float pitch = atan2f(to.y - (isWorm ? 0 : 0.3f), horiz);
            Vector3 d = dirOf(yawE, pitch), o = w.pos + d * 0.6f, hit;
            float dist = g.terrain.raycast({o, d}, 60, &hit) ? Vector3Distance(o, hit) : 60;
            for (const Worm &x : g.worms) {
                float t = Vector3DotProduct(x.pos - o, d);
                if (x.alive && &x != &w && t > 0 && t < dist && Vector3Distance(x.pos, o + d * t) < R + 0.1f) dist = t;
            }
            if (dist < 60 && pitch > -1.2f && pitch < 1.45f) {
                Outcome oc(g, smart);
                for (int s = 0; s < wd.shots; s++) oc.blast(o + d * dist, wd.radius, wd.damage);
                consider(oc.total(), yawE, pitch, 0, ti);
            }
            break;
        }
        case Kind::Melee:
            if (!isWorm || Vector3Distance(e, w.pos) > 1.7f) break;
            for (float dy : {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f})
                for (float pitch : {0.0f, 0.5f, 1.0f}) {
                    float yaw = yawE + dy;
                    Vector3 f = flat(yaw), v = dirOf(yaw, pitch) * wd.speed + Vector3{0, wd.bounce, 0};
                    Outcome oc(g, smart);
                    for (size_t i = 0; i < g.worms.size(); i++) {
                        const Worm &x = g.worms[i];
                        Vector3 dd = x.pos - w.pos;
                        float dist = Vector3Length(dd);
                        if (x.alive && (int)i != g.current && dist <= 1.8f && Vector3DotProduct(dd, f) >= 0.5f * dist) oc.strike((int)i, (int)wd.damage, v);
                    }
                    consider(oc.total(), yaw, pitch, 0, ti);
                }
            break;
        case Kind::Sheep:
        case Kind::OldWoman: {
            if (!isWorm) break;
            Vector3 f = flat(yawE), end, p = sheepWalk(g, wd, w.pos + f * 0.9f, f, e, end);
            if (wd.kind == Kind::OldWoman) p = end;
            if (Vector3Distance(p, e) < 2) consider(shell(p), yawE, w.pitch, 0, ti);
            break;
        }
        case Kind::SuperSheep:
            if (!isWorm) break;
            for (float pitch : {0.3f, 0.9f}) {
                Vector3 out;
                if (superFly(g, wd, w.pos, yawE, pitch, e, out) && Vector3Distance(out, e) < 2) consider(shell(out), yawE, pitch, 0, ti);
            }
            break;
        case Kind::Abduction: {
            if (!isWorm) break;
            float pitch = atan2f(to.y - R - 0.2f, horiz);
            Vector3 tgt = reticle(g, w.pos, yawE, pitch);
            if (Vector3Distance(tgt, e) > wd.radius || pitch < -1.2f) break;
            Outcome oc(g, smart);
            for (size_t i = 0; i < g.worms.size(); i++)
                if (g.worms[i].alive && Vector3Distance(g.worms[i].pos, tgt) < wd.radius) oc.strike((int)i, 0, {0, wd.speed, 0});
            consider(oc.total(), yawE, pitch, 0, ti);
            break;
        }
        case Kind::Airstrike:
        case Kind::Donkey:
            if (!isWorm) break;
            for (float dy : {0.0f, -0.25f, 0.25f})
                for (float pitch : {atan2f(to.y - R - 0.1f, horiz), 1.45f}) {
                    float yaw = yawE + dy;
                    Vector3 tgt = reticle(g, w.pos, yaw, pitch), f = flat(yaw), out;
                    Outcome oc(g, smart);
                    if (wd.kind == Kind::Donkey) {
                        // ponytail: scores its first impact twice, not the whole dig
                        if (fly(g, wd, tgt + Vector3{0, 25, 0}, {0, -wd.speed, 0}, wind, false, out)) { oc.blast(out, wd.radius, wd.damage); oc.blast(out, wd.radius, wd.damage); }
                    } else
                        for (int i = 0; i < wd.clusters; i++) {
                            float s = (i - (wd.clusters - 1) / 2.0f) * 2;
                            if (fly(g, wd, tgt + Vector3{f.x * s, 25, f.z * s}, {0, -wd.speed, 0}, wind, true, out)) oc.blast(out, wd.cradius, wd.cdamage);
                        }
                    if (oc.started) consider(oc.total(), yaw, pitch, 0, ti);
                }
            break;
        default: break;  // mine, scouser, flood and movement utilities: not used as attacks
        }
    }
}

// Shot is ready: add the level's aim error from a game-derived seed (same state, same shot).
void Ai::finish(const Game &g) {
    const int level = levelOf(g, g.worms[g.current].team);
    uint32_t r = g.rng ^ (uint32_t)(g.timer * 2654435761u);
    auto noise = [&] { r = r * 1664525u + 1013904223u; return ((r >> 8) / 16777216.0f) * 2 - 1; };
    float err = level == 1 ? 0.05f : level == 2 ? 0.025f : 0;
    plan.yaw += noise() * err;
    plan.pitch = Clamp(plan.pitch + noise() * err, -1.2f, 1.45f);
    plan.charge = (int)Clamp(plan.charge * (1 + noise() * err), 1, 90);
    mode = Mode::Act;
    charged = 0;
}

// No good shot from here: grab a crate, walk closer, teleport or jetpack to a better spot, else take the best shot.
void Ai::decide(const Game &g) {
    const Worm &w = g.worms[g.current];
    const int team = w.team, level = levelOf(g, team);
    if (plan.score >= 5) return finish(g);
    if (walks < (level > 1 ? 3 : 1)) {
        const Object *crate = nullptr;
        for (const Object &o : g.objects) {
            Vector3 d = o.pos - w.pos;
            if (level > 1 && o.type == Object::Crate && !o.falling && fabsf(d.y) < 3 && sqrtf(d.x * d.x + d.z * d.z) < 18 &&
                (!crate || Vector3Distance(o.pos, w.pos) < Vector3Distance(crate->pos, w.pos)))
                crate = &o;
        }
        if (crate || (walks < (level > 1 ? 2 : 1) && plan.target >= 0)) {
            goal = crate ? crate->pos : g.worms[plan.target].pos;
            stopAt = crate ? 0.3f : 1.5f;
            walk = crate ? 360 : 90;
            walks++;
            mode = Mode::Walk;
            chuteWanted = false;
            return;
        }
    }
    if (level == 3 && !moved && plan.target >= 0) {
        moved = true;
        float here = spot(g, team, w.pos) + 5, best = here;
        int tele = owned(g, team, Kind::Teleport), jetpack = owned(g, team, Kind::Jetpack);
        if (tele >= 0) {
            Plan p{tele, 0, plan.target};
            for (int a = 0; a < 16; a++)
                for (float pitch : {-0.4f, -0.1f, 0.2f}) {
                    float yaw = a * PI / 8;
                    float s = spot(g, team, reticle(g, w.pos, yaw, pitch) + Vector3{0, R + 0.3f, 0});
                    if (s > best) { best = s; p.yaw = yaw; p.pitch = pitch; }
                }
            if (best > here) { p.score = 0; plan = p; mode = Mode::Act; return; }
        }
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
    if (plan.score > -1e9f) return finish(g);
    int skip = owned(g, team, Kind::SkipGo);
    if (skip >= 0) { plan = Plan{skip, 0, -1}; plan.yaw = w.yaw; plan.pitch = w.pitch; mode = Mode::Act; }
}

static bool select(const Game &g, int wi, Input &in) {
    if (g.weapon == wi || g.shotsLeft) return true;
    in.buttons = g.prevButtons ? 0 : Input::NEXT_WEAPON;
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
    if (powered(k)) { if (charged++ < plan.charge) in.buttons = Input::FIRE; }  // release fires
    else if (!g.prevButtons) {
        in.buttons = Input::FIRE;
        if (k == Kind::Teleport) { mode = Mode::Eval; stage = -1; }
    }
    return in;
}

Input Ai::walkTo(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    int chute = owned(g, w.team, Kind::Parachute);
    if (chuteWanted && chute >= 0 && !g.chute) {  // drop ahead: open the parachute before walking off
        if (select(g, chute, in) && !g.prevButtons) in.buttons = Input::FIRE;
        return in;
    }
    float dy = angle(yawTo(w.pos, goal) - w.yaw);
    in.turn = q(dy / (2.5f * DT));
    Vector3 f = flat(w.yaw), hit;
    bool safe = true, drop = false;
    for (float a : {1.0f, 4.0f}) {
        bool h = g.terrain.raycast({w.pos + f * a + Vector3{0, 2, 0}, {0, -1, 0}}, 10, &hit);
        safe = safe && h && hit.y > g.water + 1;
        drop = drop || (h && hit.y < w.pos.y - 6) || !h;
    }
    if (drop && safe && chute >= 0 && !g.chute && !chuteWanted && w.grounded) { chuteWanted = true; return in; }
    if (fabsf(dy) < 0.3f && safe) {
        in.walk = 127;
        if (Vector3Distance(w.pos, lastPos) < 0.005f && w.grounded && !g.jumpDelay && !g.prevButtons) in.buttons = Input::JUMP;
    }
    lastPos = w.pos;
    Vector3 d = goal - w.pos;
    if (--walk <= 0 || !safe || sqrtf(d.x * d.x + d.z * d.z) < stopAt) { walk = 0; mode = Mode::Eval; stage = -1; }
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
    if (g.fuel > 0 && ((h > 1.5f && (w.pos.y < goal.y + 3 || low)) || w.vel.y < -5)) in.buttons = Input::FIRE;
    if ((w.grounded && h < 1.5f) || --walk <= 0) {
        in.buttons = (g.prevButtons & Input::JUMP) ? 0 : Input::JUMP;
        mode = Mode::Eval;
        stage = -1;
    }
    return in;
}

// Retreat: walk to dry ground away from enemies (and out of their sight), the blast and mines.
Input Ai::retreat(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    if (levelOf(g, w.team) == 1) return in;
    auto worth = [&](Vector3 p) {
        float near = 20, s = 0;
        for (const Worm &e : g.worms) {
            if (!e.alive || e.team == w.team) continue;
            near = fminf(near, Vector3Distance(e.pos, p));
            if (Vector3Distance(e.pos, p) < 40 && seen(g, p + Vector3{0, 0.3f, 0}, e.pos)) s -= 4;  // prefer cover
        }
        s += near + 0.5f * fminf(Vector3Distance(p, lastBoom), 10);
        for (const Object &o : g.objects) {
            if (o.type == Object::Mine && Vector3Distance(o.pos, p) < 3) s -= 20;
            if (o.type == Object::Sentry && o.team != w.team && Vector3Distance(o.pos, p) < WEAPONS[o.weapon].radius) s -= 5;
        }
        return s;
    };
    if (!retreatPicked) {
        retreatPicked = true;
        float best = worth(w.pos) + 1;
        for (int a = 0; a < 16; a++) {
            Vector3 f = flat(a * PI / 8), end = w.pos, hit;
            float y = w.pos.y;
            bool ok = true;
            for (int s = 1; s <= 7 && ok; s++) {
                ok = ground(g, w.pos + f * (float)s, hit) && hit.y > g.water + 1.5f && hit.y > y - 3;
                y = hit.y;
                end = hit;
            }
            float v = ok ? worth(end) : -1e9f;
            if (v > best) { best = v; retreatYaw = a * PI / 8; retreatOn = true; }
        }
    }
    if (!retreatOn) return in;
    float dy = angle(retreatYaw - w.yaw);
    in.turn = q(dy / (2.5f * DT));
    Vector3 f = flat(w.yaw), hit;
    bool safe = true;
    for (float a : {1.0f, 2.5f}) safe = safe && ground(g, w.pos + f * a, hit) && hit.y > g.water + 1;
    if (!safe) { retreatOn = false; return in; }
    if (fabsf(dy) < 0.3f) {
        in.walk = 127;
        if (Vector3Distance(w.pos, lastPos) < 0.005f && w.grounded && !g.jumpDelay && !g.prevButtons) in.buttons = Input::JUMP;
    }
    lastPos = w.pos;
    return in;
}

Input Ai::think(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) lastBoom = e.pos;
    if (g.phase == Phase::Flying) {  // sheep: detonate next to an enemy; super sheep: autopilot
        for (const Projectile &s : g.shots) {
            Kind k = WEAPONS[s.weapon].kind;
            if (s.child || (k != Kind::Sheep && k != Kind::SuperSheep)) continue;
            bool near = false;
            for (const Worm &e : g.worms) near = near || (e.alive && e.team != w.team && Vector3Distance(s.pos, e.pos) < (k == Kind::Sheep ? 1.2f : 1.5f));
            if (k == Kind::SuperSheep && plan.target >= 0 && g.worms[plan.target].alive) steer(g, s.pos, s.vel, g.worms[plan.target].pos, in);
            if (near && !g.prevButtons) in.buttons = Input::FIRE;
        }
        return in;
    }
    if (!w.alive) return in;
    if (g.phase == Phase::Retreat) return retreat(g);
    if (g.phase != Phase::Aim) return in;
    if (g.timer > lastTimer || g.current != worm) {
        worm = g.current;
        tick = walk = walks = charged = shotsSeen = 0;
        moved = retreatPicked = retreatOn = chuteWanted = false;
        mode = Mode::Eval;
        stage = -1;
        run = RopeRun{};
        lastBoom = w.pos;
    }
    lastTimer = g.timer;
    if (++tick < 20) return in;  // short pause so a watcher can follow
    if (g.cfg.rules & RULE_ROPE_RACE) return race(g);
    if (g.jetting) return mode == Mode::Jet ? jet(g) : Input{0, 0, 0, (uint8_t)(g.prevButtons ? 0 : Input::JUMP)};
    if (g.roped) { in.buttons = g.prevButtons ? 0 : Input::JUMP; return in; }
    switch (mode) {
    case Mode::Eval:
        if (!w.grounded || Vector3LengthSqr(w.vel) > 0.01f) return in;  // wait to stand still
        if (stage < 0) startEval(g);
        if (tpos.empty()) return in;
        {  // one (weapon, target) pair per tick; unusable weapons are skipped for free
            const int T = (int)tpos.size(), N = (int)WEAPONS.size() * T, team = w.team;
            while (stage < N && !g.ammo[team][stage / T]) stage++;
            if (stage < N) { evalWeapon(g, stage / T, stage % T); stage++; }
            if (stage >= N) { stage = -1; decide(g); }
        }
        return in;
    case Mode::Walk: return walkTo(g);
    case Mode::Act: return act(g);
    case Mode::Jet: return in;  // waiting for the jetpack to start
    }
    return in;
}

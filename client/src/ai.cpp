#include "ai.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

static constexpr float DT = Game::DT;

static float grav(const Game &g) { return 15 * ((g.cfg.rules & RULE_LOW_GRAVITY) ? 0.4f : 1.0f); }  // Game::gravity() is private
static Vector3 dirOf(float yaw, float pitch) { return {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}; }
static int8_t q(float v) { return (int8_t)Clamp(roundf(v * 127), -127, 127); }

static int levelOf(const Game &g, int team) {
    int l = team < (int)g.cfg.teamSetup.size() ? g.cfg.teamSetup[team].cpu : 0;
    return l ? std::min(l, 3) : 2;
}

// Explosion worth for `team`: enemy damage (+kill bonus) minus twice its own, nearest enemy distance as tie-breaker.
static float value(const Game &g, int team, Vector3 p, float radius, float damage) {
    float s = 0, reach = radius * 2, nearest = 1e9f;
    for (const Worm &w : g.worms) {
        if (!w.alive) continue;
        float d = Vector3Distance(w.pos, p);
        if (w.team != team) nearest = fminf(nearest, d);
        if (d >= reach) continue;
        float dmg = damage * (1 - d / reach), v = dmg + (dmg >= w.hp ? 30 : 0);
        s += w.team == team ? -2 * v : v;
    }
    return s - 0.05f * fminf(nearest, 100);
}

// Point copy of Game::stepShots for one non-sheep projectile; false when lost in water or off the map.
static bool fly(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 v, float wind, bool child, Vector3 &out) {
    const float W = Terrain::NX * Terrain::VOX;
    bool impact = child || wd.fuse == 0;
    float fuse = wd.fuse;
    for (int i = 0; i < 360; i++) {
        v.y -= grav(g) * DT;
        if (wd.wind) v.x += wind * 6 * DT;
        Vector3 np = p + v * DT;
        out = np;
        if (g.terrain.solid(np)) {
            if (impact) return true;
            v = Vector3Reflect(v, g.terrain.normal(np)) * wd.bounce;
        } else p = np;
        if (impact && wd.kind != Kind::Donkey)
            for (const Worm &w : g.worms)
                if (w.alive && Vector3Distance(np, w.pos) < Game::R + 0.3f) return true;
        if (!impact && (fuse -= DT) <= 0) return true;
        if (p.y < g.water - 2 || p.x < -20 || p.z < -20 || p.x > W + 20 || p.z > W + 20) return false;
    }
    return false;
}

// Copy of the sheep walk; returns the point of closest approach to `e`.
static Vector3 sheepWalk(const Game &g, const WeaponDef &wd, Vector3 p, Vector3 f, Vector3 e) {
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
    return best;
}

// Game::target() for a hypothetical aim.
static Vector3 reticle(const Game &g, Vector3 pos, float yaw, float pitch) {
    Vector3 hit, dir = dirOf(yaw, pitch);
    if (g.terrain.raycast({pos + dir, dir}, 60, &hit)) return hit;
    Vector3 far = {pos.x + sinf(yaw) * 30, (Terrain::NY - 1) * Terrain::VOX, pos.z + cosf(yaw) * 30};
    if (g.terrain.raycast({far, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit)) return hit;
    return {far.x, g.water, far.z};
}

void Ai::replan(const Game &g) {
    const Worm &w = g.worms[worm];
    const int team = w.team, level = levelOf(g, team);
    const float wind = level > 1 ? g.wind : 0;  // level 1 ignores the wind
    plan = Plan{};
    float nearest = 1e9f;
    std::vector<int> targets;
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &e = g.worms[i];
        if (!e.alive || e.team == team) continue;
        targets.push_back((int)i);
        float d = Vector3Distance(e.pos, w.pos);
        if (d < nearest) { nearest = d; plan.target = (int)i; }
    }
    if (targets.empty()) return;
    if (level == 1) targets = {plan.target};
    auto consider = [&](float score, int wi, float yaw, float pitch, int charge, int target) {
        if (WEAPONS[wi].count > 0) score -= 8;  // keep specials for good chances
        if (score > plan.score) plan = {wi, charge, target, yaw, pitch, score};
    };
    for (size_t wi = 0; wi < WEAPONS.size(); wi++) {
        const WeaponDef &wd = WEAPONS[wi];
        if (!g.ammo[team][wi] || (level == 1 && wd.count >= 0)) continue;
        for (int ti : targets) {
            Vector3 e = g.worms[ti].pos;
            Vector3 to = e - w.pos;
            float yawTo = atan2f(to.x, to.z), horiz = sqrtf(to.x * to.x + to.z * to.z);
            switch (wd.kind) {
            case Kind::Shell:
                // constant acceleration A: hit T at time t with V = (T - P - A t(t+DT)/2) / t (semi-implicit Euler)
                for (Vector3 T : {e, e - Vector3Normalize({to.x, 0, to.z}) * 1.5f})
                    for (float t = 0.2f; t < 4.5f; t += 0.1f) {
                        Vector3 A = {wd.wind ? wind * 6 : 0, -grav(g), 0}, P = w.pos, V{};
                        for (int it = 0; it < 2; it++) {
                            V = (T - P - A * (0.5f * t * (t + DT))) / t;
                            P = w.pos + Vector3Normalize(V) * 1.2f;
                        }
                        float sp = Vector3Length(V), pitch = asinf(V.y / sp), yaw = atan2f(V.x, V.z);
                        if (sp > wd.speed || sp < 0.15f * wd.speed || pitch < -1.2f || pitch > 1.45f) continue;
                        int n = std::max(1, (int)roundf(sp / wd.speed * 90));
                        float pw = 0;
                        for (int k = 0; k < n; k++) pw = fminf(1, pw + DT / 1.5f);
                        Vector3 d = dirOf(yaw, pitch), out;
                        if (fly(g, wd, w.pos + d * 1.2f, d * (wd.speed * pw), wind, false, out))
                            consider(value(g, team, out, wd.radius, wd.damage), (int)wi, yaw, pitch, n, ti);
                    }
                break;
            case Kind::Shotgun: {
                float pitch = atan2f(to.y, horiz);
                Vector3 d = dirOf(yawTo, pitch), o = w.pos + d * 0.6f, hit;
                float dist = g.terrain.raycast({o, d}, 60, &hit) ? Vector3Distance(o, hit) : 60;
                for (const Worm &x : g.worms) {
                    float t = Vector3DotProduct(x.pos - o, d);
                    if (x.alive && &x != &w && t > 0 && t < dist && Vector3Distance(x.pos, o + d * t) < Game::R + 0.1f) dist = t;
                }
                if (dist < 60 && pitch > -1.2f && pitch < 1.45f)
                    consider(wd.shots * value(g, team, o + d * dist, wd.radius, wd.damage), (int)wi, yawTo, pitch, 0, ti);
                break;
            }
            case Kind::Sheep: {
                Vector3 f = {sinf(yawTo), 0, cosf(yawTo)}, p = sheepWalk(g, wd, w.pos + f * 0.9f, f, e);
                if (Vector3Distance(p, e) < 1.5f) consider(value(g, team, p, wd.radius, wd.damage), (int)wi, yawTo, w.pitch, 0, ti);
                break;
            }
            case Kind::Airstrike:
            case Kind::Donkey:
                for (float pitch : {atan2f(to.y - Game::R - 0.1f, horiz), 1.45f}) {
                    Vector3 tgt = reticle(g, w.pos, yawTo, pitch), f = {sinf(yawTo), 0, cosf(yawTo)}, out;
                    float s = 0;
                    if (wd.kind == Kind::Donkey) {
                        // ponytail: scores its first impact twice, not the whole dig
                        if (fly(g, wd, tgt + Vector3{0, 25, 0}, {0, -wd.speed, 0}, wind, false, out)) s = 2 * value(g, team, out, wd.radius, wd.damage);
                    } else
                        for (int i = 0; i < wd.clusters; i++) {
                            float k = (i - (wd.clusters - 1) / 2.0f) * 2;
                            if (fly(g, wd, tgt + Vector3{f.x * k, 25, f.z * k}, {0, -wd.speed, 0}, wind, true, out)) s += value(g, team, out, wd.cradius, wd.cdamage);
                        }
                    consider(s, (int)wi, yawTo, pitch, 0, ti);
                }
                break;
            default: break;  // utilities: only walking is used for now
            }
        }
    }
    if (plan.score < 5 && walks < 2 && level > 1) { walk = 90; walks++; return; }
    // aim error from a game-derived seed: same state, same shot
    uint32_t r = g.rng ^ (uint32_t)(g.timer * 2654435761u);
    auto noise = [&] { r = r * 1664525u + 1013904223u; return ((r >> 8) / 16777216.0f) * 2 - 1; };
    float err = level == 1 ? 0.05f : level == 2 ? 0.025f : 0;
    plan.yaw += noise() * err;
    plan.pitch = Clamp(plan.pitch + noise() * err, -1.2f, 1.45f);
    plan.charge = (int)Clamp(plan.charge * (1 + noise() * err), 1, 90);
}

Input Ai::think(const Game &g) {
    Input in;
    const Worm &w = g.worms[g.current];
    if (g.phase == Phase::Flying) {  // sheep: detonate next to an enemy
        for (const Projectile &s : g.shots)
            for (const Worm &e : g.worms)
                if (WEAPONS[s.weapon].kind == Kind::Sheep && e.alive && e.team != w.team && Vector3Distance(s.pos, e.pos) < 1.2f && g.prevButtons == 0)
                    in.buttons = Input::FIRE;
        return in;
    }
    if (g.phase != Phase::Aim || !w.alive) return in;
    if (g.timer > lastTimer || g.current != worm) { worm = g.current; tick = walk = walks = charged = 0; }
    lastTimer = g.timer;
    if (g.roped || g.jetting) { in.buttons = g.prevButtons ? 0 : Input::JUMP; return in; }
    if (++tick < 20) return in;  // short pause so a watcher can follow
    if (tick == 20) replan(g);
    if (walk > 0) {
        const Worm &e = g.worms[plan.target];
        float dy = remainderf(atan2f(e.pos.x - w.pos.x, e.pos.z - w.pos.z) - w.yaw, 2 * PI);
        in.turn = q(dy / (2.5f * DT));
        Vector3 f = {sinf(w.yaw), 0, cosf(w.yaw)}, hit;
        bool safe = true;
        for (float a : {1.0f, 4.0f})
            safe = safe && g.terrain.raycast({w.pos + f * a + Vector3{0, 2, 0}, {0, -1, 0}}, 10, &hit) && hit.y > g.water + 1;
        if (fabsf(dy) < 0.3f && safe) {
            in.walk = 127;
            if (Vector3Distance(w.pos, lastPos) < 0.005f && tick % 2) in.buttons = Input::JUMP;
        }
        lastPos = w.pos;
        if (--walk == 0 || !safe || Vector3Distance(w.pos, e.pos) < 4) { walk = 0; replan(g); }
        return in;
    }
    if (plan.score <= -1e9f) return in;
    if (g.weapon != plan.weapon && !g.shotsLeft) { in.buttons = g.prevButtons ? 0 : Input::NEXT_WEAPON; return in; }
    float dy = remainderf(plan.yaw - w.yaw, 2 * PI), dp = plan.pitch - w.pitch;
    in.turn = q(dy / (2.5f * DT));
    in.aim = q(dp / (1.5f * DT));
    if (fabsf(dy) > 2e-3f || fabsf(dp) > 2e-3f) return in;
    if (WEAPONS[g.weapon].kind == Kind::Shell) { if (charged++ < plan.charge) in.buttons = Input::FIRE; }  // release fires
    else if (!g.prevButtons) in.buttons = Input::FIRE;
    return in;
}

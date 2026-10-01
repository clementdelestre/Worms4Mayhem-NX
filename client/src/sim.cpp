#include "sim.h"
#include "raymath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

std::vector<WeaponDef> WEAPONS = {
    {"Bazooka", Kind::Shell, 3, 45, 32, 0, 0, 0, 0, -1, 0, 1, true},
    {"Grenade", Kind::Shell, 3, 45, 22, 3, 0.45f, 0, 0, -1, 0, 1, false},
};

// Minimal JSON: one array of flat objects with string/number/bool values.
bool loadWeapons(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::string src;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) src.append(buf, n);
    fclose(f);
    static const char *KINDS[] = {"shell", "sheep", "airstrike", "donkey", "shotgun", "rope", "jetpack", "teleport", "supersheep", "oldwoman",
                                  "melee", "homing", "mine", "scouser", "sentry", "abduction", "flood", "parachute", "skipgo", "surrender", "changeworm"};
    const char *p = src.c_str();
    auto ws = [&] { while (isspace((unsigned char)*p)) p++; };
    auto eat = [&](char c) {
        ws();
        return *p == c ? (p++, true) : false;
    };
    auto str = [&](std::string &out) {
        if (!eat('"')) return false;
        const char *b = p;
        while (*p && *p != '"') p++;
        if (!*p) return false;
        out.assign(b, p++);
        return true;
    };
    std::vector<WeaponDef> list;
    if (!eat('[')) return false;
    while (eat('{')) {
        WeaponDef w = {"?", Kind::Shell, 2, 30, 20, 0, 0, 1.5f, 15, -1, 0, 1, false};
        const struct { const char *k; float *v; } floats[] = {{"radius", &w.radius}, {"damage", &w.damage}, {"speed", &w.speed}, {"fuse", &w.fuse},
            {"bounce", &w.bounce}, {"cluster_radius", &w.cradius}, {"cluster_damage", &w.cdamage}, {"poison", &w.poison}};
        const struct { const char *k; int *v; } ints[] = {{"count", &w.count}, {"clusters", &w.clusters}, {"shots", &w.shots}, {"crate_weight", &w.weight}};
        std::string key, sv;
        while (str(key) && eat(':')) {
            float num = 0;
            bool b = false;
            ws();
            if (*p == '"') { if (!str(sv)) return false; }
            else if (!strncmp(p, "true", 4)) { b = true; p += 4; }
            else if (!strncmp(p, "false", 5)) p += 5;
            else {
                char *e;
                num = strtof(p, &e);
                if (e == p) return false;
                p = e;
            }
            if (key == "name") w.name = sv;
            if (key == "wind") w.wind = b;
            for (int k = 0; k < (int)(sizeof KINDS / sizeof *KINDS); k++) if (key == "kind" && sv == KINDS[k]) w.kind = (Kind)k;
            for (auto &fl : floats) if (key == fl.k) *fl.v = num;
            for (auto &in : ints) if (key == in.k) *in.v = (int)num;
            if (!eat(',')) break;
        }
        if (!eat('}')) return false;
        list.push_back(w);
        if (!eat(',')) break;
    }
    if (!eat(']') || list.empty()) return false;
    WEAPONS = list;
    return true;
}

static constexpr float GRAVITY = 15;
static constexpr float FALL_SAFE = 9, FALL_SCALE = 4;  // fall damage: speed threshold, hp per m/s above it

float Game::rand01() {
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / 16777216.0f;
}

float Game::gravity() const { return GRAVITY * ((cfg.rules & RULE_LOW_GRAVITY) ? 0.4f : 1.0f); }

Vector3 Game::aimDir(const Worm &w) const {
    return {cosf(w.pitch) * sinf(w.yaw), sinf(w.pitch), cosf(w.pitch) * cosf(w.yaw)};
}

void Game::start(const GameConfig &c) {
    cfg = c;
    terrain.load(c.map, c.seed);
    rng = c.seed * 2654435761u + 1;
    teams = c.teams;
    perTeam = c.wormsPerTeam;
    const int per = perTeam;
    worms.clear();
    shots.clear();
    objects.clear();
    nextWorm.assign(teams, 0);
    winner = -1;
    weapon = 0;
    prevButtons = 0;
    water = Terrain::WATER;
    turnCount = 0;
    suddenDeath = false;
    lastHitTeam.assign(teams * perTeam, -1);

    ammo.assign(teams, std::vector<int>(WEAPONS.size(), 0));
    int ropeIdx = 0;
    for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == Kind::Rope) ropeIdx = (int)i;
    if (cfg.rules & RULE_ROPE_RACE) {
        for (auto &a : ammo) a[ropeIdx] = -1;  // infinite rope, nothing else
        weapon = ropeIdx;
    } else if (cfg.rules & RULE_HIGHLANDER) {
        for (auto &a : ammo)
            for (int i = 0; i < 3; i++) a[(int)(rand01() * WEAPONS.size())] += 2;  // small random starting set
    } else {
        for (auto &a : ammo)
            for (size_t i = 0; i < WEAPONS.size(); i++) a[i] = WEAPONS[i].count;
    }
    const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
    for (int t = 0; t < teams; t++)
        for (int k = 0; k < per; k++) {
            Worm w = {{cx, (Terrain::NY - 1) * Terrain::VOX, cz}, {0, 0, 0}, 0, 0.3f, 100, t, true, false};
            size_t slot = t * per + k;
            if (slot < terrain.spawns.size()) {
                w.pos = terrain.spawns[slot];
                w.yaw = atan2f(cx - w.pos.x, cz - w.pos.z);
            }
            for (int tries = 0; slot >= terrain.spawns.size() && tries < 200; tries++) {
                float a = rand01() * 2 * PI, r = 4 + rand01() * 20;
                Vector3 hit, top = {cx + cosf(a) * r, (Terrain::NY - 1) * Terrain::VOX, cz + sinf(a) * r};
                if (terrain.raycast({top, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit) && hit.y > water + 1.5f) {
                    w.pos = {hit.x, hit.y + R + 0.3f, hit.z};
                    w.yaw = atan2f(cx - hit.x, cz - hit.z);
                    break;
                }
            }
            worms.push_back(w);
        }
    if (!(cfg.rules & RULE_ROPE_RACE)) {
        for (int i = 0; i < MINES; i++) addObject(Object::Mine, 0);
        for (int i = 0; i < BARRELS; i++) addObject(Object::Barrel, 0);
    }

    if (cfg.rules & RULE_ROPE_RACE) {
        raceFinish = terrain.finish;
        if (!terrain.hasFinish) {
            // fallback: highest point on a coarse grid, biased away from spawns
            float best = -1e9f;
            for (int z = 4; z < Terrain::NZ - 4; z += 8)
                for (int x = 4; x < Terrain::NX - 4; x += 8) {
                    int y = Terrain::NY - 1;
                    while (y > 0 && terrain.at(x, y, z) <= 0) y--;
                    if (y <= 0) continue;
                    Vector3 p = {x * Terrain::VOX, (y + 1) * Terrain::VOX, z * Terrain::VOX};
                    float near = 1e9f;
                    for (const Worm &wo : worms) near = fminf(near, Vector3Distance(p, wo.pos));
                    float score = p.y + near * 0.5f;
                    if (score > best) { best = score; raceFinish = p; }
                }
        }
    }
    beginTurn(teams - 1);
}

void Game::beginTurn(int team) {
    for (Worm &x : worms)
        if (x.alive && x.poison && x.hp > 1) { x.hp = std::max(1, x.hp - x.poison); emit(GameEvent::Hurt, x.pos, int(&x - worms.data())); }
    std::vector<bool> has(teams, false);
    int alive = 0, last = -1;
    for (const Worm &w : worms)
        if (w.alive && !has[w.team]) { has[w.team] = true; alive++; last = w.team; }
    if (alive <= 1) {
        phase = Phase::GameOver;
        winner = alive ? last : -1;
        emit(GameEvent::GameOver, {0, 0, 0});
        return;
    }
    if (cfg.rules & RULE_SUDDEN_DEATH) {
        if (++turnCount == SD_TURNS * teams && !suddenDeath) {
            suddenDeath = true;
            for (Worm &x : worms) if (x.alive && x.hp > 1) x.hp = 1;
        }
        if (suddenDeath) water = fminf(water + 0.2f, Terrain::WATER + 15);
    }
    for (int i = 1; i <= teams; i++) {
        int t = (team + i) % teams;
        if (!has[t]) continue;
        for (int k = 0; k < perTeam; k++) {
            int slot = (nextWorm[t] + k) % perTeam, c = t * perTeam + slot;
            if (!worms[c].alive) continue;
            current = c;
            nextWorm[t] = (slot + 1) % perTeam;
            phase = Phase::Aim;
            timer = TURN_TICKS;
            power = 0;
            wind = rand01() * 2 - 1;
            roped = jetting = chute = false;
            shotsLeft = 0;
            if (!ammo[t][weapon]) nextWeapon(t);
            if (!(cfg.rules & RULE_ROPE_RACE) && rand01() < CRATE_CHANCE && addObject(Object::Crate, 15)) emit(GameEvent::CrateDrop, objects.back().pos);
            emit(GameEvent::TurnStart, worms[c].pos, c);
            return;
        }
    }
}

void Game::nextWeapon(int team) {
    for (int i = 1; i <= (int)WEAPONS.size(); i++) {
        int k = (weapon + i) % WEAPONS.size();
        if (ammo[team][k]) { weapon = k; return; }
    }
}

static float halfHeight(Object::Type t) { return t == Object::Mine ? 0.1f : t == Object::Barrel || t == Object::Sentry ? 0.5f : 0.45f; }

// Random dry ground point away from living worms and other objects.
bool Game::dropPoint(Vector3 &out) {
    const float W = Terrain::NX * Terrain::VOX;
    for (int tries = 0; tries < 100; tries++) {
        Vector3 hit, top = {8 + rand01() * (W - 16), (Terrain::NY - 1) * Terrain::VOX, 8 + rand01() * (W - 16)};
        if (!terrain.raycast({top, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit) || hit.y < water + 1.5f) continue;
        bool clear = true;
        for (const Worm &w : worms) clear = clear && (!w.alive || Vector3Distance(w.pos, hit) > 3.5f);
        for (const Object &o : objects) clear = clear && Vector3Distance(o.pos, hit) > 2;
        if (clear) { out = hit; return true; }
    }
    return false;
}

// lift > 0: dropped from that height under a parachute.
bool Game::addObject(Object::Type t, float lift) {
    Vector3 p;
    if (!dropPoint(p)) return false;
    Object o = {t, {p.x, fminf(p.y + halfHeight(t) + 0.05f + lift, (Terrain::NY - 1) * Terrain::VOX), p.z}, {0, 0, 0}, -1, -1, lift > 0, false};
    if (t == Object::Crate && rand01() >= HEALTH_CHANCE) {
        int total = 0;
        for (const WeaponDef &wd : WEAPONS) total += wd.weight;
        int r = (int)(rand01() * total);
        for (size_t k = 0; k < WEAPONS.size() && o.weapon < 0; k++) if ((r -= WEAPONS[k].weight) < 0) o.weapon = (int)k;
    }
    objects.push_back(o);
    return true;
}

void Game::stepObjects() {
    for (size_t i = 0; i < objects.size();) {
        Object &o = objects[i];
        float h = halfHeight(o.type);
        if (o.vel.y <= 0 && terrain.solid({o.pos.x, o.pos.y - h - 0.05f, o.pos.z})) {
            o.vel = {o.vel.x * 0.8f, 0, o.vel.z * 0.8f};
            o.falling = false;
        } else {
            o.vel.y -= gravity() * DT;
            if (o.falling) o.vel.y = fmaxf(o.vel.y, -2.5f);
        }
        Vector3 np = Vector3Add(o.pos, Vector3Scale(o.vel, DT));
        if (terrain.solid({np.x, o.pos.y, np.z})) { o.vel.x = o.vel.z = 0; np.x = o.pos.x; np.z = o.pos.z; }
        o.pos = np;
        for (int k = 0; k < 20 && terrain.solid({o.pos.x, o.pos.y - h, o.pos.z}); k++) o.pos.y += 0.05f;

        bool gone = o.pos.y < water, boom = o.dead && (o.type == Object::Barrel || (o.type == Object::Crate && o.weapon >= 0));
        if (o.type == Object::Mine) {
            for (const Worm &w : worms)
                if (o.fuse < 0 && w.alive && Vector3Distance(w.pos, o.pos) < 1.5f) { o.fuse = MINE_FUSE; emit(GameEvent::MineArm, o.pos); }
            if (o.fuse >= 0 && (o.fuse -= DT) <= 0) boom = true;
        }
        if (o.type == Object::Sentry) {
            if (o.fuse >= 0 && (o.fuse -= DT) < 0) o.fuse = -1;
            Worm &t = worms[current];
            const WeaponDef &sd = WEAPONS[o.weapon];
            Vector3 eye = Vector3Add(o.pos, {0, 0.4f, 0}), to = Vector3Subtract(t.pos, eye), hit;
            float dist = Vector3Length(to);
            bool seen = dist > 0.1f && !terrain.raycast({eye, Vector3Scale(to, 1 / dist)}, dist - 0.6f, &hit);
            if (o.fuse < 0 && phase != Phase::Settle && t.alive && t.team != o.team && dist < sd.radius && seen) {
                o.fuse = sd.fuse;
                emit(GameEvent::Fire, o.pos, -1, o.weapon);
                hurt(t, (int)sd.damage);
                t.vel = Vector3Add(t.vel, Vector3Add(Vector3Scale(to, 4 / dist), {0, 2, 0}));
            }
        }
        if (o.type == Object::Crate && !o.dead)
            for (Worm &w : worms) {
                if (!w.alive || Vector3Distance(w.pos, o.pos) >= R + 0.7f) continue;
                if (o.weapon < 0) w.hp += 25, w.poison = 0;
                else if (ammo[w.team][o.weapon] >= 0) ammo[w.team][o.weapon]++;
                emit(GameEvent::Collect, o.pos, int(&w - worms.data()), o.weapon);
                gone = true;
                break;
            }
        if (!gone && !boom && !o.dead) { i++; continue; }
        Object x = o;
        objects.erase(objects.begin() + i);  // before explode(), which only flags the others
        if (boom && !gone) {
            if (x.type == Object::Barrel) explode(x.pos, 4, 50);
            else if (x.type == Object::Mine) explode(x.pos, 3, 45);
            else explode(x.pos, 3, 35);
        }
    }
}

Vector3 Game::target() const {
    const Worm &w = worms[current];
    Vector3 hit, dir = aimDir(w);
    if (terrain.raycast({Vector3Add(w.pos, dir), dir}, 60, &hit)) return hit;
    Vector3 far = {w.pos.x + sinf(w.yaw) * 30, (Terrain::NY - 1) * Terrain::VOX, w.pos.z + cosf(w.yaw) * 30};
    if (terrain.raycast({far, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit)) return hit;
    return {far.x, water, far.z};
}

void Game::use(Worm &w) {
    const WeaponDef &wd = WEAPONS[weapon];
    int &n = ammo[w.team][weapon];
    if (n > 0 && !shotsLeft) n--;
    Vector3 dir = aimDir(w), f = {sinf(w.yaw), 0, cosf(w.yaw)}, tgt = target();
    emit(GameEvent::Fire, w.pos, current, weapon);
    switch (wd.kind) {
    case Kind::Shell:
        shots.push_back({Vector3Add(w.pos, Vector3Scale(dir, 1.2f)), Vector3Scale(dir, wd.speed * fmaxf(power, 0.15f)), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Sheep:
        shots.push_back({Vector3Add(w.pos, Vector3Scale(f, 0.9f)), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Airstrike:
        for (int i = 0; i < wd.clusters; i++)
            shots.push_back({Vector3Add(tgt, {f.x * (i - (wd.clusters - 1) / 2.0f) * 2, 25, f.z * (i - (wd.clusters - 1) / 2.0f) * 2}), {0, -wd.speed, 0}, weapon, 0, true, 1});
        phase = Phase::Flying;
        break;
    case Kind::Donkey:
        shots.push_back({Vector3Add(tgt, {0, 25, 0}), {0, -wd.speed, 0}, weapon, 0, false, wd.clusters});
        phase = Phase::Flying;
        break;
    case Kind::Shotgun: {
        if (!shotsLeft) shotsLeft = wd.shots;
        Ray r = {Vector3Add(w.pos, Vector3Scale(dir, 0.6f)), dir};
        Vector3 hit;
        float dist = terrain.raycast(r, 60, &hit) ? Vector3Distance(r.position, hit) : 60;
        for (const Worm &o : worms) {
            float t = Vector3DotProduct(Vector3Subtract(o.pos, r.position), dir);
            if (o.alive && &o != &w && t > 0 && t < dist && Vector3Distance(o.pos, Vector3Add(r.position, Vector3Scale(dir, t))) < R + 0.1f) dist = t;
        }
        if (dist < 60) explode(Vector3Add(r.position, Vector3Scale(dir, dist)), wd.radius, wd.damage);
        if (--shotsLeft == 0) phase = Phase::Flying;
        break;
    }
    case Kind::Rope: {
        Vector3 hit;
        if (terrain.raycast({w.pos, dir}, wd.speed, &hit)) { roped = true; anchor = hit; ropeLen = Vector3Distance(w.pos, hit); w.grounded = false; }
        break;
    }
    case Kind::Jetpack: jetting = true; fuel = wd.fuse; break;
    case Kind::Teleport: w.pos = Vector3Add(tgt, {0, R + 0.3f, 0}); w.vel = {0, 0, 0}; break;
    case Kind::SuperSheep:
    case Kind::Homing:
        shots.push_back({Vector3Add(w.pos, Vector3Scale(dir, 1.2f)), Vector3Scale(dir, wd.speed * (wd.kind == Kind::Homing ? fmaxf(power, 0.15f) : 1)), weapon, wd.fuse, false, 1, tgt});
        phase = Phase::Flying;
        break;
    case Kind::OldWoman:
        shots.push_back({Vector3Add(w.pos, Vector3Scale(f, 0.9f)), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Scouser:
        shots.push_back({Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 1.4f), {0, 0.6f, 0})), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Melee:
        for (Worm &o : worms) {
            Vector3 d = Vector3Subtract(o.pos, w.pos);
            float dist = Vector3Length(d);
            if (!o.alive || &o == &w || dist > 1.8f || Vector3DotProduct(d, f) < 0.5f * dist) continue;
            hurt(o, (int)wd.damage);
            o.vel = Vector3Add(Vector3Scale(dir, wd.speed), {0, wd.bounce, 0});
        }
        if (wd.fuse > 0) { w.vel.y = wd.fuse; w.grounded = false; }
        phase = Phase::Flying;
        break;
    case Kind::Mine:
        objects.push_back({Object::Mine, Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 1.7f), {0, 0.3f, 0})), {0, 0, 0}, -1, -1, false, false});
        phase = Phase::Flying;
        break;
    case Kind::Sentry:
        objects.push_back({Object::Sentry, Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 1.3f), {0, 0.3f, 0})), {0, 0, 0}, weapon, -1, false, false, w.team});
        phase = Phase::Flying;
        break;
    case Kind::Abduction:
        for (Worm &o : worms)
            if (o.alive && Vector3Distance(o.pos, tgt) < wd.radius) { o.vel = {0, wd.speed, 0}; o.grounded = false; }
        phase = Phase::Flying;
        break;
    case Kind::Flood: water = fminf(water + wd.speed, Terrain::WATER + 15); phase = Phase::Flying; break;
    case Kind::Parachute: chute = true; break;
    case Kind::Surrender:
        for (Worm &o : worms) if (o.team == w.team) o.hp = 0;
        [[fallthrough]];
    case Kind::SkipGo: phase = Phase::Settle; timer = 300; break;
    case Kind::ChangeWorm:
        for (int k = 1, base = w.team * perTeam; k < perTeam; k++) {
            int c = base + (current - base + k) % perTeam;
            if (worms[c].alive) { current = c; break; }
        }
        break;
    }
    power = 0;
}

void Game::stepWorm(Worm &w) {
    if (!w.alive) return;
    auto feet = [&](float off) { return terrain.solid({w.pos.x, w.pos.y - R - off, w.pos.z}); };
    bool wasGrounded = w.grounded;
    float fallSpeed = -w.vel.y;
    w.grounded = w.vel.y <= 0 && feet(0.05f);
    if (w.grounded) {
        if (!wasGrounded && fallSpeed > FALL_SAFE) {
            int dmg = (int)((fallSpeed - FALL_SAFE) * FALL_SCALE);
            if (dmg > 0) { w.hp -= dmg; emit(GameEvent::Hurt, w.pos, int(&w - worms.data())); }
        }
        w.vel = {w.vel.x * 0.85f, 0, w.vel.z * 0.85f};
    } else w.vel.y -= gravity() * DT;
    Vector3 np = Vector3Add(w.pos, Vector3Scale(w.vel, DT));
    if (terrain.solid({np.x, w.pos.y, np.z})) { w.vel.x = w.vel.z = 0; np.x = w.pos.x; np.z = w.pos.z; }
    if (w.vel.y > 0 && terrain.solid({np.x, np.y + R, np.z})) { w.vel.y = 0; np.y = w.pos.y; }
    w.pos = np;
    for (int i = 0; i < 20 && feet(0); i++) {
        w.pos.y += 0.05f;
        if (w.vel.y < 0) w.vel.y = 0;
    }
    if (w.pos.y < water) drown(w);
}

void Game::drown(Worm &w) {
    w.alive = false;
    w.hp = 0;
    emit(GameEvent::Splash, w.pos, int(&w - worms.data()));
    emit(GameEvent::Death, w.pos, int(&w - worms.data()));
}

// Pendulum: integrate freely, then project back onto the rope sphere and drop outward velocity.
void Game::stepRope(Worm &w) {
    w.vel.y -= gravity() * DT;
    Vector3 np = Vector3Add(w.pos, Vector3Scale(w.vel, DT)), d = Vector3Subtract(np, anchor);
    float l = Vector3Length(d);
    if (l > ropeLen) {
        Vector3 n = Vector3Scale(d, 1 / l);
        np = Vector3Add(anchor, Vector3Scale(n, ropeLen));
        float out = Vector3DotProduct(w.vel, n);
        if (out > 0) w.vel = Vector3Subtract(w.vel, Vector3Scale(n, out));
    }
    auto blocked = [&](Vector3 p) { return terrain.solid({p.x, p.y - R, p.z}) || terrain.solid({p.x, p.y + R, p.z}); };
    if (blocked(np)) { np.y = w.pos.y; w.vel.y = 0; }  // slide along the ground
    if (blocked(np)) w.vel = Vector3Scale(w.vel, -0.3f);
    else w.pos = np;
    if (w.pos.y < water) drown(w);
}

void Game::hurt(Worm &w, int dmg) {
    if (dmg <= 0) return;
    w.hp -= dmg;
    int wi = int(&w - worms.data());
    emit(GameEvent::Hurt, w.pos, wi);
    if (wi == current) return;
    Worm &h = worms[current];
    if (cfg.rules & RULE_KARMA) h.hp -= (int)(dmg * 0.5f + 0.5f);
    if ((cfg.rules & RULE_VAMPIRE) && w.team != h.team) h.hp = std::min(200, h.hp + (int)(dmg * 0.5f + 0.5f));
    if (cfg.rules & RULE_HIGHLANDER) lastHitTeam[wi] = h.team;
}

void Game::explode(Vector3 p, float radius, float damage, float poison) {
    terrain.carve(p, radius);
    emit(radius >= 5 ? GameEvent::BigBoom : GameEvent::Boom, p);
    float reach = radius * 2;
    for (Worm &w : worms) {
        float d = Vector3Distance(w.pos, p);
        if (!w.alive || d >= reach) continue;
        float f = 1 - d / reach;
        hurt(w, (int)(damage * f + 0.5f));
        if (poison > 0) w.poison = std::max(w.poison, (int)poison);
        Vector3 dir = Vector3Normalize(Vector3Add(Vector3Subtract(w.pos, p), {0, 1, 0}));
        w.vel = Vector3Add(w.vel, Vector3Scale(dir, 14 * f));
    }
    for (Object &o : objects) {
        float d = Vector3Distance(o.pos, p);
        if (d >= reach) continue;
        Vector3 dir = Vector3Normalize(Vector3Add(Vector3Subtract(o.pos, p), {0, 1, 0}));
        o.vel = Vector3Add(o.vel, Vector3Scale(dir, 10 * (1 - d / reach)));
        o.falling = false;
        if (o.type != Object::Mine) o.dead = true;
        else if (o.fuse < 0 || o.fuse > 1) o.fuse = 1;
    }
}

void Game::stepShots(const Input &in, bool detonate) {
    const float W = Terrain::NX * Terrain::VOX;
    std::vector<Projectile> spawned;
    auto touches = [&](Vector3 p) {
        for (const Worm &w : worms) if (w.alive && Vector3Distance(p, w.pos) < R + 0.3f) return true;
        return false;
    };
    for (size_t i = 0; i < shots.size();) {
        Projectile &s = shots[i];
        const WeaponDef &wd = WEAPONS[s.weapon];
        bool boom = false, timed = wd.fuse > 0 && !s.child;
        Vector3 np;
        if ((wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman) && !s.child) {
            // walks in its launch direction, climbs small steps, hops at walls
            bool ground = terrain.solid({s.pos.x, s.pos.y - 0.35f, s.pos.z});
            s.vel.y = ground && s.vel.y <= 0 ? 0 : s.vel.y - gravity() * DT;
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            float climb = 0;
            while (climb <= 0.6f && terrain.solid({np.x, np.y - 0.3f + climb, np.z})) climb += 0.05f;
            if (climb > 0.6f) { np.x = s.pos.x; np.z = s.pos.z; if (ground) s.vel.y = 7; }
            else np.y += climb;
            if (terrain.solid({np.x, np.y + 0.3f, np.z})) { np = s.pos; s.vel.y = fminf(s.vel.y, 0); }
            s.pos = np;
            boom = detonate && wd.kind == Kind::Sheep;
        } else if (wd.kind == Kind::SuperSheep && !s.child) {
            // steered by the stick at constant speed, no gravity
            float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 2 * DT;
            float pitch = Clamp(asinf(Clamp(s.vel.y / fmaxf(Vector3Length(s.vel), 0.01f), -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
            s.vel = Vector3Scale({cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}, wd.speed);
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            boom = detonate || terrain.solid(np) || touches(np);
            s.pos = np;
        } else if (wd.kind == Kind::Scouser) {
            // inflated: floats up to a slow climb and drifts with the wind
            s.vel.y += (1.2f - s.vel.y) * 2 * DT;
            s.vel.x += wind * 3 * DT;
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            if (terrain.solid(np)) s.vel = Vector3Scale(s.vel, -0.3f);
            else s.pos = np;
            boom = detonate || touches(np);
        } else {
            bool homing = wd.kind == Kind::Homing && (s.fuse += DT) > 0.4f;  // fuse counts flight time: ballistic, then locks on
            if (homing) s.vel = Vector3Lerp(s.vel, Vector3Scale(Vector3Normalize(Vector3Subtract(s.aim, s.pos)), wd.speed), 3 * DT);
            else s.vel.y -= gravity() * DT;
            if (wd.wind) s.vel.x += wind * 6 * DT;
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            bool impact = s.child || wd.fuse == 0;
            if (terrain.solid(np)) {
                if (!impact) {
                    s.vel = Vector3Scale(Vector3Reflect(s.vel, terrain.normal(np)), wd.bounce);
                    if (Vector3Length(s.vel) > 2) emit(GameEvent::Bounce, np, -1, s.weapon);
                }
                else boom = true;
            } else s.pos = np;
            if (impact && wd.kind != Kind::Donkey && touches(np)) boom = true;
        }
        if (timed && (s.fuse -= DT) <= 0) boom = true;
        bool gone = s.pos.y < water - 2 || s.pos.x < -20 || s.pos.z < -20 || s.pos.x > W + 20 || s.pos.z > W + 20;
        if (boom) {
            s.pos = np;
            explode(np, s.child ? wd.cradius : wd.radius, s.child ? wd.cdamage : wd.damage, s.child ? 0 : wd.poison);
            if (!s.child && wd.kind != Kind::Airstrike && wd.kind != Kind::Donkey)
                for (int k = 0; k < wd.clusters; k++)
                    spawned.push_back({Vector3Add(np, {0, 0.5f, 0}), {(rand01() - 0.5f) * 8, 6 + rand01() * 5, (rand01() - 0.5f) * 8}, s.weapon, 0, true, 1});
        }
        if ((boom && --s.hits <= 0) || gone) shots.erase(shots.begin() + i);
        else i++;
    }
    shots.insert(shots.end(), spawned.begin(), spawned.end());
}

void Game::step(const Input &in) {
    if (phase == Phase::GameOver) return;
    uint8_t pressed = in.buttons & ~prevButtons;
    prevButtons = in.buttons;
    events.clear();
    Worm &w = worms[current];
    bool detonate = phase == Phase::Flying && (pressed & Input::FIRE), tool = roped || jetting;

    if (w.alive && (phase == Phase::Aim || phase == Phase::Retreat)) {
        w.yaw += in.turn / 127.0f * 2.5f * DT;
        if (w.grounded && in.walk) {
            Vector3 np = {w.pos.x + sinf(w.yaw) * in.walk / 127.0f * 3 * DT, w.pos.y, w.pos.z + cosf(w.yaw) * in.walk / 127.0f * 3 * DT};
            float climb = 0;
            while (climb <= 0.6f && terrain.solid({np.x, np.y - R + climb, np.z})) climb += 0.05f;
            if (climb <= 0.6f && !terrain.solid({np.x, np.y + climb + R, np.z})) { np.y += climb; w.pos = np; }
            for (int i = 0; i < 12 && !terrain.solid({w.pos.x, w.pos.y - R - 0.05f, w.pos.z}); i++) w.pos.y -= 0.05f;
        }
        if ((pressed & Input::JUMP) && w.grounded && !tool) {
            w.vel = {sinf(w.yaw) * 4, 7, cosf(w.yaw) * 4};
            w.grounded = false;
            emit(GameEvent::Jump, w.pos, current);
        }
    }
    if (w.alive && phase == Phase::Aim) {
        Vector3 push = Vector3Scale({sinf(w.yaw), 0, cosf(w.yaw)}, in.walk / 127.0f * DT);
        if (roped) {
            if (pressed & Input::JUMP) roped = false;
            ropeLen = Clamp(ropeLen - in.aim / 127.0f * 6 * DT, 1, WEAPONS[weapon].speed);
            w.vel = Vector3Add(w.vel, Vector3Scale(push, 6));
        } else if (jetting) {
            if ((pressed & Input::JUMP) || (fuel <= 0 && w.grounded)) jetting = false;
            if ((in.buttons & Input::FIRE) && fuel > 0) { w.vel.y += WEAPONS[weapon].speed * DT; fuel -= DT; }
            w.vel = Vector3Add(w.vel, Vector3Scale(push, 8));
            w.vel.x *= 0.98f;
            w.vel.z *= 0.98f;
        } else {
            if (chute && !w.grounded) w.vel = Vector3Add(w.vel, Vector3Scale(push, 5));
            w.pitch = Clamp(w.pitch + in.aim / 127.0f * 1.5f * DT, -1.2f, 1.45f);
            if ((pressed & Input::NEXT_WEAPON) && !shotsLeft) nextWeapon(w.team);
            if (ammo[w.team][weapon] || shotsLeft) {
                if (!powered(WEAPONS[weapon].kind)) { if (pressed & Input::FIRE) use(w); }
                else {
                    if (in.buttons & Input::FIRE) power = fminf(1, power + DT / 1.5f);
                    if (power > 0 && (!(in.buttons & Input::FIRE) || power >= 1)) use(w);
                }
            }
        }
    }

    if (chute && !w.grounded && w.vel.y < -2.5f) w.vel.y = -2.5f;  // below FALL_SAFE: no fall damage
    for (Worm &x : worms)
        if (roped && &x == &w) stepRope(x);
        else stepWorm(x);
    stepShots(in, detonate);
    stepObjects();

    if ((cfg.rules & RULE_ROPE_RACE) && phase != Phase::GameOver)
        for (const Worm &x : worms)
            if (x.alive && Vector3Distance(x.pos, raceFinish) < 2.0f) {
                phase = Phase::GameOver;
                winner = x.team;
                emit(GameEvent::GameOver, raceFinish);
                break;
            }

    switch (phase) {
    case Phase::Aim:
        if (!w.alive || --timer <= 0) { phase = Phase::Settle; timer = 300; roped = jetting = chute = false; }
        break;
    case Phase::Flying:
        if (shots.empty()) { phase = Phase::Retreat; timer = 180; }
        break;
    case Phase::Retreat:
        if (!w.alive || --timer <= 0) { phase = Phase::Settle; timer = 300; }
        break;
    case Phase::Settle: {
        bool still = shots.empty();
        for (const Worm &x : worms) still = still && (!x.alive || x.grounded);
        for (const Object &o : objects) still = still && o.fuse < 0 && Vector3LengthSqr(o.vel) < 0.01f;
        if (--timer <= 0 || (still && timer < 270)) {
            for (Worm &x : worms) {
                if (!x.alive || x.hp > 0) continue;
                int xi = int(&x - worms.data());
                if ((cfg.rules & RULE_HIGHLANDER) && lastHitTeam[xi] >= 0 && lastHitTeam[xi] != x.team)
                    for (size_t wi = 0; wi < WEAPONS.size(); wi++)
                        if (ammo[x.team][wi] && ammo[lastHitTeam[xi]][wi] >= 0) ammo[lastHitTeam[xi]][wi]++;
                x.alive = false;
                terrain.carve(x.pos, 0.8f);
                emit(GameEvent::Boom, x.pos);
                emit(GameEvent::Death, x.pos, xi);
            }
            if (cfg.rules & RULE_KING)
                for (int t = 0; t < teams; t++) {
                    int king = t * perTeam;
                    if (king < (int)worms.size() && !worms[king].alive)
                        for (int k = 0; k < perTeam; k++) {
                            Worm &x = worms[t * perTeam + k];
                            if (x.alive) { x.alive = false; x.hp = 0; emit(GameEvent::Boom, x.pos); emit(GameEvent::Death, x.pos, t * perTeam + k); }
                        }
                }
            beginTurn(w.team);
        }
        break;
    }
    case Phase::GameOver: break;
    }
}

uint32_t Game::checksum() const {
    uint32_t h = 2166136261u;
    auto mix = [&](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
    };
    for (const Worm &w : worms) { mix(&w.pos, sizeof w.pos); mix(&w.vel, sizeof w.vel); mix(&w.hp, sizeof w.hp); mix(&w.yaw, sizeof w.yaw); mix(&w.pitch, sizeof w.pitch); mix(&w.alive, sizeof w.alive); mix(&w.poison, sizeof w.poison); }
    for (const Projectile &s : shots) { mix(&s.pos, sizeof s.pos); mix(&s.vel, sizeof s.vel); mix(&s.weapon, sizeof s.weapon); mix(&s.fuse, sizeof s.fuse); mix(&s.hits, sizeof s.hits); }
    mix(&chute, 1);
    mix(lastHitTeam.data(), lastHitTeam.size() * sizeof(int));
    mix(&rng, sizeof rng);
    mix(&current, sizeof current);
    mix(&weapon, sizeof weapon);
    mix(&water, sizeof water);
    mix(&turnCount, sizeof turnCount);
    mix(&suddenDeath, sizeof suddenDeath);
    mix(&raceFinish, sizeof raceFinish);
    for (const Object &o : objects) { mix(&o.type, 1); mix(&o.pos, sizeof o.pos); mix(&o.vel, sizeof o.vel); mix(&o.weapon, sizeof o.weapon); mix(&o.fuse, sizeof o.fuse); mix(&o.falling, 1); mix(&o.dead, 1); mix(&o.team, sizeof o.team); }
    for (const auto &a : ammo) mix(a.data(), a.size() * sizeof(int));
    // weapon table: every client must have loaded the same weapons.json
    for (const WeaponDef &wd : WEAPONS) {
        mix(wd.name.data(), wd.name.size());
        mix(&wd.kind, sizeof wd.kind);
        mix(&wd.radius, 7 * sizeof(float));
        mix(&wd.count, 3 * sizeof(int));
        mix(&wd.wind, sizeof wd.wind);
        mix(&wd.weight, sizeof wd.weight);
        mix(&wd.poison, sizeof wd.poison);
    }
    return h;
}

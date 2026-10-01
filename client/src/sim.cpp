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

static size_t baseWeapons = WEAPONS.size();  // loaded table; start() appends GameConfig::custom after it
static const char *KINDS[] = {"shell", "sheep", "airstrike", "donkey", "shotgun", "rope", "jetpack", "teleport", "supersheep", "oldwoman",
                              "melee", "homing", "mine", "scouser", "sentry", "abduction", "flood", "parachute", "skipgo", "surrender", "changeworm", "armour"};

// Minimal JSON: one array of flat objects with string/number/bool values.
static bool parseWeapons(const char *path, std::vector<WeaponDef> &list) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    std::string src;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) src.append(buf, n);
    fclose(f);
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
    list.clear();
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
            if (key == "model") w.model = sv;
            if (key == "icon") w.icon = sv;
            if (key == "wind") w.wind = b;
            if (key == "user_fuse") w.userFuse = b;
            if (key == "rest_fuse") w.restFuse = b;
            if (key == "walks") w.walks = b;
            if (key == "pins") w.pins = b;
            for (int k = 0; k < (int)(sizeof KINDS / sizeof *KINDS); k++) if (key == "kind" && sv == KINDS[k]) w.kind = (Kind)k;
            for (auto &fl : floats) if (key == fl.k) *fl.v = num;
            for (auto &in : ints) if (key == in.k) *in.v = (int)num;
            if (!eat(',')) break;
        }
        if (!eat('}')) return false;
        list.push_back(w);
        if (!eat(',')) break;
    }
    return eat(']');
}

bool loadWeapons(const char *path) {
    std::vector<WeaponDef> list;
    if (!parseWeapons(path, list) || list.empty()) return false;
    WEAPONS = list;
    baseWeapons = list.size();
    return true;
}

bool loadCustomWeapons(const char *path, std::vector<WeaponDef> &out) { return parseWeapons(path, out); }

bool saveCustomWeapons(const char *path, const std::vector<WeaponDef> &list) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    auto clean = [](std::string s) { s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '"' || c == '\\' || (unsigned char)c < 32; }), s.end()); return s; };
    fputs("[\n", f);
    for (size_t i = 0; i < list.size(); i++) {
        const WeaponDef &w = list[i];
        fprintf(f, "  {\"name\": \"%s\", \"kind\": \"%s\", \"radius\": %g, \"damage\": %g, \"speed\": %g, \"fuse\": %g, \"bounce\": %g, "
                   "\"clusters\": %d, \"cluster_radius\": %g, \"cluster_damage\": %g, \"count\": %d, \"shots\": %d, \"crate_weight\": %d, "
                   "\"poison\": %g, \"wind\": %s, \"model\": \"%s\", \"icon\": \"%s\"}%s\n",
                clean(w.name).c_str(), KINDS[(int)w.kind], w.radius, w.damage, w.speed, w.fuse, w.bounce, w.clusters, w.cradius, w.cdamage,
                w.count, w.shots, w.weight, w.poison, w.wind ? "true" : "false", clean(w.model).c_str(), clean(w.icon).c_str(), i + 1 < list.size() ? "," : "");
    }
    fputs("]\n", f);
    return fclose(f) == 0;
}

const WormpotMode WORMPOT_MODES[18] = {
    {"Double Damage", "Everything does twice the amount of damage."},
    {"Super Explosives", "All explosive weapons do increased damage and will throw worms further than usual."},
    {"Super Animals", "All animal weapons do increased damage and will throw worms further than usual."},
    {"Wind Affects All", "Wind affects all weapons in the game, even the grenade."},
    {"Worms Only Drown", "Worms can only be killed by knocking them into the water, no health is removed via a shot."},
    {"Quick Walk", "Worms will be able to walk a lot faster than normal."},
    {"Slippy Mode", "Worms will slide around the landscape more easily."},
    {"Sticky Mode", "Worms will stick to the landscape more and be harder to move around."},
    {"Low Gravity", "Gravity has less of an effect on worms making them jump higher."},
    {"No Jumping", "Worms lose their ability to jump and can only walk around the landscape."},
    {"Max Fall Damage", "Fall damage will hurt worms a lot."},
    {"Crate Shower", "A crate shower will arrive at the start of every turn."},
    {"Crate Drops Only", "Weapons can only be obtained through crate drops."},
    {"Max Health Drops", "Health crates will contain 100 health."},
    {"David And Goliath", "One of your worms is the mighty Goliath (lots of energy) and the rest are Davids (not much energy)."},
    {"One Shot One Kill", "All worms start with 1 health so one shot does indeed mean one kill."},
    {"Vampire", "Half the damage a worm inflicts on another worm is gifted back to him."},
    {"Vital Worm", "One worm on each team is vital, if he dies the team dies."},
};
const int WORMPOT_REELS[4] = {0, 5, 11, 18};  // weapons, worms, crates & energy


// Order: turn, retreat, hot seat, round (min), energy, crate %, weapon/health/utility shares, crate hp,
// mines, barrels, mine fuse, sudden death, fall damage, wind, weapon set. Mines, barrels, sets: ours.
const std::vector<SchemePreset> SCHEMES = {
    {"Standard", {45, 3, 5, 20, 100, 40, 30, 20, 30, 25, 5, 4, 3, 0, 1, 2, 0}},
    {"Beginner", {90, 0, 5, 20, 100, 50, 30, 20, 30, 50, 3, 4, 5, 0, 0, 1, 0}},
    {"Pro", {30, 5, 0, 20, 100, 30, 10, 50, 30, 25, 5, 4, Scheme::FUSE_RANDOM, 0, 1, 3, 0}},
    {"BnG", {30, 5, 5, 10, 150, 40, 40, 40, 10, 25, 5, 4, 5, 0, 1, 2, Scheme::SET_BNG}},
    {"Shopping", {60, 3, 5, 20, 100, 100, 60, 20, 20, 25, 5, 4, Scheme::FUSE_RANDOM, 0, 1, 1, Scheme::SET_CRATES}},
    {"All Action", {30, 3, 5, 10, 200, 40, 30, 20, 30, 25, 5, 6, 1, 0, 1, 1, 0}},
    {"Strategy", {30, 3, 0, 30, 100, 40, 10, 50, 10, 25, 8, 4, 5, 0, 0, 3, 0}},
    {"Family", {90, 0, 8, 20, 125, 50, 30, 20, 30, 50, 3, 4, 5, 0, 0, 1, 0}},
    {"Mega Power", {30, 3, 5, 10, 200, 50, 40, 20, 40, 100, 5, 8, Scheme::FUSE_RANDOM, 0, 1, 1, Scheme::SET_UNLIMITED}},
    {"Holy Grail", {45, 3, 5, 30, 100, 60, 30, 30, 30, 50, 5, 4, 1, 0, 0, 2, 0}},
    {"Darksider", {45, 3, 5, 30, 100, 50, 10, 50, 10, 25, 5, 4, 5, 0, 0, 1, 0}},
};

static constexpr float GRAVITY = 15;
static constexpr float FALL_SAFE = 10.5f, FALL_SCALE = 4;  // fall damage: speed threshold, hp per m/s above it
// W4M WXWorm.Jump*/Backflip* (world units, 25 = one worm height = 1 m here): forward 3.2 m long, 2 m high;
// backflip 1.6 m back, 3.2 m high. Speeds for g = 15: v_up = sqrt(2gh), v_side = d * g / (2 v_up).
static constexpr float JUMP_UP = 7.75f, JUMP_FWD = 3.1f, FLIP_UP = 9.8f, FLIP_BACK = -1.22f;
static constexpr float SLIDE_NY = 0.5f, SLIDE_FRICTION = 0.95f, WALK_OFF = 0.7f;  // W4M SlideAngle 60, WalkOffCliffVelMulti
static const float WIND_MAX[] = {0, 0.5f, 1, 1.5f};

// W4M melee box: in front of the attacker, a worm height up or down; the Fire Punch (a leap) also reaches above.
bool meleeHits(const Worm &a, Vector3 p, const WeaponDef &wd) {
    Vector3 d = Vector3Subtract(p, a.pos);
    float ahead = d.x * sinf(a.yaw) + d.z * cosf(a.yaw), side = fabsf(d.x * cosf(a.yaw) - d.z * sinf(a.yaw));
    return ahead > -0.2f && ahead < 2 && side < 1 && d.y > -1.2f && d.y < (wd.fuse > 0 ? 3 : 1.2f);
}

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
    WEAPONS.resize(std::min(WEAPONS.size(), baseWeapons));
    WEAPONS.insert(WEAPONS.end(), c.custom.begin(), c.custom.end());
    // Wormpot modes that are existing rules or scheme values: set them on our copy, which the checksum covers
    const uint32_t wp = c.wormpot;
    if (wp & WP_LOW_GRAVITY) cfg.rules |= RULE_LOW_GRAVITY;
    if (wp & WP_VAMPIRE) cfg.rules |= RULE_VAMPIRE;
    if (wp & WP_VITAL_WORM) cfg.rules |= RULE_KING;
    if (wp & WP_CRATE_DROPS) cfg.scheme.weapons = Scheme::SET_CRATES;
    if (wp & WP_MAX_HEALTH) cfg.scheme.crateHealth = 100;
    if (wp & WP_CRATE_SHOWER) cfg.scheme.crateChance = 100;
    if (wp & WP_ONE_SHOT) cfg.scheme.health = 1;
    terrain.load(c.map, c.seed);
    rng = c.seed * 2654435761u + 1;
    teams = c.teams;
    perTeam = c.wormsPerTeam;
    const int per = perTeam;
    worms.clear();
    shots.clear();
    objects.clear();
    gas.clear();
    nextWorm.assign(teams, 0);
    winner = -1;
    weapon = 0;
    prevButtons = 0;
    water = Terrain::WATER;
    clock = hotSeat = jumpDelay = ropeShots = 0;
    selfHurt = false;
    suddenDeath = false;
    lastHitTeam.assign(teams * perTeam, -1);
    fuses.assign(teams, 3);
    countGroup.clear(), countT = 0, landHold = 0;

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
        const int set = cfg.scheme.weapons;
        for (auto &a : ammo)
            for (size_t i = 0; i < WEAPONS.size(); i++) {
                Kind k = WEAPONS[i].kind;
                bool keep = set == Scheme::SET_DEFAULT || k == Kind::SkipGo || k == Kind::Surrender ||
                            (set == Scheme::SET_BNG && (k == Kind::Shell || k == Kind::Homing));
                a[i] = set == Scheme::SET_UNLIMITED ? -1 : keep ? WEAPONS[i].count : 0;
            }
    }
    const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
    for (int t = 0; t < teams; t++)
        for (int k = 0; k < per; k++) {
            Worm w = {{cx, (Terrain::NY - 1) * Terrain::VOX, cz}, {0, 0, 0}, 0, 0.3f, std::max(1, (int)cfg.scheme.health), t, true, false};
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
            if ((cfg.wormpot & WP_GOLIATH) && per > 1) w.hp = k ? std::max(1, w.hp / 2) : w.hp * 2;
            worms.push_back(w);
        }
    if (!(cfg.rules & RULE_ROPE_RACE)) {
        for (int i = 0; i < cfg.scheme.mines; i++) addObject(Object::Mine, 0);
        for (int i = 0; i < cfg.scheme.barrels; i++) addObject(Object::Barrel, 0);
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
    picked.assign(teams, weapon);
    idle.assign(teams, 0);
    run = MissionRun{};
    if (cfg.mission) missionStart(*this);
    beginTurn(teams - 1);
}

void Game::beginTurn(int team) {
    for (Worm &x : worms)
        if (x.alive && x.poison && x.hp > 1) { x.hp = std::max(1, x.hp - x.poison); emit(GameEvent::Hurt, x.pos, int(&x - worms.data())); }
    for (Worm &x : worms) x.counted = std::max(0, x.hp);  // poison: the hud counts it during the hot seat
    std::vector<bool> has(teams, false);
    int alive = 0, last = -1;
    for (const Worm &w : worms)
        if (w.alive && !has[w.team]) { has[w.team] = true; alive++; last = w.team; }
    if (alive <= 1 && !cfg.mission) {  // missions end through their objectives
        phase = Phase::GameOver;
        winner = alive ? last : -1;
        emit(GameEvent::GameOver, {0, 0, 0});
        return;
    }
    const Scheme &sc = cfg.scheme;
    if ((cfg.rules & RULE_SUDDEN_DEATH) && !suddenDeath && clock >= sc.roundTime * 3600) {
        suddenDeath = true;
        if (sc.sdType != Scheme::SD_WATER)
            for (Worm &x : worms) if (x.alive && x.hp > 1) x.hp = 1;
    }
    if (suddenDeath && sc.sdType != Scheme::SD_ONE_HP) water = fminf(water + 1.25f, Terrain::WATER + 15);  // W4M Water.RiseAmount 25
    for (int i = 1; i <= teams; i++) {
        int t = (team + i) % teams;
        if (!has[t] || idle[t]) continue;
        for (int k = 0; k < perTeam; k++) {
            int slot = (nextWorm[t] + k) % perTeam, c = t * perTeam + slot;
            if (!worms[c].alive) continue;
            current = c;
            nextWorm[t] = (slot + 1) % perTeam;
            phase = Phase::Aim;
            timer = std::max(1, (int)sc.turnTime) * 60;
            hotSeat = sc.hotSeat * 60;
            jumpDelay = 0;
            selfHurt = false;
            power = 0;
            wind = (rand01() * 2 - 1) * WIND_MAX[std::min<int>(sc.wind, 3)];
            roped = jetting = chute = false;
            shotsLeft = ropeShots = 0;
            weapon = picked[t];
            if (!ammo[t][weapon]) nextWeapon(t);
            for (int n = cfg.wormpot & WP_CRATE_SHOWER ? 3 : 1; n > 0; n--)
                if (!(cfg.rules & RULE_ROPE_RACE) && clock && rand01() * 100 < sc.crateChance && addObject(Object::Crate, 15))  // clock: none before the first turn
                    emit(GameEvent::CrateDrop, objects.back().pos);
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
    const Scheme &sc = cfg.scheme;
    float pick = t == Object::Crate ? rand01() * (sc.healthShare + sc.weaponShare + sc.utilityShare) : -1;
    if (pick >= sc.healthShare) {
        bool util = pick >= sc.healthShare + sc.weaponShare;
        int total = 0;
        for (const WeaponDef &wd : WEAPONS) total += utility(wd.kind) == util ? wd.weight : 0;
        int r = (int)(rand01() * total);
        for (size_t k = 0; k < WEAPONS.size() && o.weapon < 0; k++) if (utility(WEAPONS[k].kind) == util && (r -= WEAPONS[k].weight) < 0) o.weapon = (int)k;
    }
    objects.push_back(o);
    return true;
}

void Game::stepObjects() {
    for (size_t i = 0; i < objects.size();) {
        Object &o = objects[i];
        float h = halfHeight(o.type);
        if (o.tag >= 0 && (o.type == Object::Crate || o.type == Object::Target)) {  // pinned mission object
        } else if (o.vel.y <= 0 && terrain.solid({o.pos.x, o.pos.y - h - 0.05f, o.pos.z})) {
            if (o.falling && o.type == Object::Crate) emit(GameEvent::CrateLand, o.pos, -1, o.weapon), landHold = phase == Phase::Aim ? 45 : 0;
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
            if (o.courtesy > 0) o.courtesy--;
            for (const Worm &w : worms)  // every worm, flying or sliding too
                if (o.fuse < 0 && !o.dud && !o.courtesy && w.alive && Vector3Distance(w.pos, o.pos) < MINE_ARM) {
                    o.fuse = cfg.scheme.mineFuse == Scheme::FUSE_RANDOM ? 1 + rand01() * 4 : cfg.scheme.mineFuse;  // W4M Mine.Min/MaxFuse 1-5 s
                    emit(GameEvent::MineArm, o.pos);
                }
            if (o.fuse >= 0 && (o.fuse -= DT) <= 0) {
                o.dud = rand01() < MINE_DUD;
                if (o.dud) o.fuse = -1;
                else boom = true;
            }
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
        bool sheep = false;  // W4M challenges: a Super Sheep collects mission crates for its worm
        for (const Projectile &s : shots) sheep |= o.tag >= 0 && WEAPONS[s.weapon].kind == Kind::SuperSheep && Vector3Distance(s.pos, o.pos) < 1.2f;
        // touching the 0.9 m crate box, with some slack: beside it, on top or just under it
        auto touching = [&](const Worm &w) { return fabsf(w.pos.y - o.pos.y) < 1.3f && Vector2Distance({w.pos.x, w.pos.z}, {o.pos.x, o.pos.z}) < R + 0.75f; };
        if (o.type == Object::Crate && !o.dead)
            for (Worm &w : worms) {
                if (!(sheep && &w == &worms[current]) && (!w.alive || !touching(w))) continue;
                if (o.weapon < 0) w.hp += cfg.scheme.crateHealth, w.counted += cfg.scheme.crateHealth, w.poison = 0;  // label jumps on pickup (W4M)
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
            else if (x.type == Object::Mine) explode(x.pos, 3, 40);  // W4M kWeaponLandmine 40
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
    if (wd.kind == Kind::Rope && ropeShots >= ROPE_SHOTS) return;
    if (n > 0 && !shotsLeft && !(wd.kind == Kind::Rope && ropeShots)) n--;  // one rope = ROPE_SHOTS launches
    Vector3 dir = aimDir(w), f = {sinf(w.yaw), 0, cosf(w.yaw)}, tgt = target();
    emit(GameEvent::Fire, w.pos, current, weapon);
    switch (wd.kind) {
    case Kind::Shell:
        shots.push_back({Vector3Add(w.pos, Vector3Scale(dir, 1.2f)), Vector3Scale(dir, wd.speed * fmaxf(power, 0.15f)), weapon, fuseOf(wd), false, 1});
        if (dropped(wd)) phase = Phase::Retreat, timer = 1;  // W4M: walk away while the fuse burns, the blast ends the turn
        else phase = Phase::Flying;
        break;
    case Kind::Sheep:
        shots.push_back({Vector3Add(w.pos, Vector3Scale(f, 0.9f)), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Airstrike:
        if (wd.fuse > 0) shots.push_back({Vector3Add(tgt, {-f.x * BOMBER_LEAD, BOMBER_HEIGHT, -f.z * BOMBER_LEAD}), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1, {}, 0, 0});
        else {  // W4M Bomber: a run centred on the target along the worm's facing, one bomb every STRIKE_TICKS
            float back = -(wd.clusters - 1) / 2.0f * STRIKE_GAP;
            shots.push_back({Vector3Add(tgt, {f.x * back, 25, f.z * back}), Vector3Scale(f, STRIKE_GAP / (STRIKE_TICKS * DT)), weapon, 0, false, 1, {}, 0, 0});
        }
        phase = Phase::Flying;
        break;
    case Kind::Donkey:
        shots.push_back({Vector3Add(tgt, {0, 25, 0}), {0, -wd.speed, 0}, weapon, 0, false, wd.clusters > 0 ? wd.clusters : 1 << 30});  // 0: smashes down to the water
        phase = Phase::Flying;
        break;
    case Kind::Shotgun: {
        if (!shotsLeft) shotsLeft = wd.shots;
        Ray r = {Vector3Add(w.pos, Vector3Scale(dir, 0.6f)), dir};
        Vector3 hit;
        float dist = terrain.raycast(r, 60, &hit) ? Vector3Distance(r.position, hit) : 60;
        Worm *struck = nullptr;
        for (Worm &o : worms) {
            float t = Vector3DotProduct(Vector3Subtract(o.pos, r.position), dir);
            if (o.alive && &o != &w && t > 0 && t < dist && Vector3Distance(o.pos, Vector3Add(r.position, Vector3Scale(dir, t))) < R + 0.1f) dist = t, struck = &o;
        }
        for (const Object &o : objects) {
            float t = Vector3DotProduct(Vector3Subtract(o.pos, r.position), dir);
            if (o.type == Object::Target && t > 0 && t < dist && Vector3Distance(o.pos, Vector3Add(r.position, Vector3Scale(dir, t))) < 0.6f) dist = t, struck = nullptr;
        }
        // a worm hit takes the full damage (W4M gun), the blast only digs and pushes
        if (dist < 60) explode(Vector3Add(r.position, Vector3Scale(dir, dist)), wd.radius, struck ? 0 : wd.damage);
        if (struck) hurt(*struck, (int)wd.damage * (struck->armour ? ARMOUR : 100) / 100);
        if (--shotsLeft == 0) phase = Phase::Flying;
        break;
    }
    case Kind::Rope: {
        ropeShots++;
        Vector3 hit;
        float reach = terrain.raycast({w.pos, dir}, wd.speed, &hit) ? Vector3Distance(w.pos, hit) : wd.speed;
        Object *grab = nullptr;  // W4M: the hook catches crates, mines and drums and yanks them over
        for (Object &o : objects) {
            float t = Vector3DotProduct(Vector3Subtract(o.pos, w.pos), dir);
            bool loose = o.type == Object::Mine || o.type == Object::Barrel || (o.type == Object::Crate && o.tag < 0);
            if (loose && t > 0 && t < reach && Vector3Distance(o.pos, Vector3Add(w.pos, Vector3Scale(dir, t))) < 0.6f) reach = t, grab = &o;
        }
        if (grab) grab->vel = {(w.pos.x - grab->pos.x) * 1.2f, 5, (w.pos.z - grab->pos.z) * 1.2f}, grab->falling = false;  // lands ~4/5 of the way
        else if (reach < wd.speed) { roped = true; anchor = hit; ropeLen = reach; ropeMax = wd.speed; w.grounded = false; }
        break;
    }
    case Kind::Jetpack: jetting = true; fuel = wd.fuse; thrust = wd.speed; break;
    case Kind::Teleport: w.pos = Vector3Add(tgt, {0, R + 0.3f, 0}); w.vel = {0, 0, 0}; break;
    case Kind::SuperSheep:
        if (wd.walks) {
            shots.push_back({Vector3Add(w.pos, Vector3Scale(f, 0.9f)), Vector3Scale(f, SHEEP_STEP), weapon, SHEEP_WALK, false, 1});
            phase = Phase::Flying;
            break;
        }
        [[fallthrough]];
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
            if (!o.alive || &o == &w || !meleeHits(w, o.pos, wd)) continue;
            hurt(o, (int)wd.damage);
            if (wd.pins) o.nailed = true, o.vel = {0, 0, 0}, o.pos.y -= 0.35f;  // sunk to the waist
            else o.vel = Vector3Add(Vector3Scale(dir, wd.speed), {0, wd.bounce, 0});
        }
        if (wd.fuse > 0) { w.vel.y = wd.fuse; w.grounded = false; }
        phase = Phase::Flying;
        break;
    case Kind::Mine:
        objects.push_back({Object::Mine, Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 1.7f), {0, 0.3f, 0})), {0, 0, 0}, -1, -1, false, false, -1, -1, false, MINE_COURTESY});
        phase = Phase::Flying;
        break;
    case Kind::Sentry:
        objects.push_back({Object::Sentry, Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 1.3f), {0, 0.3f, 0})), {0, 0, 0}, weapon, -1, false, false, w.team});
        phase = Phase::Flying;
        break;
    case Kind::Abduction:
        for (Worm &o : worms)
            if (o.alive && Vector3Distance(o.pos, tgt) < wd.radius) { hurt(o, o.hp / 2); o.vel = {0, wd.speed, 0}; o.grounded = false; }  // W4M: half its health
        phase = Phase::Flying;
        break;
    case Kind::Flood: water = fminf(water + wd.speed, Terrain::WATER + 15); phase = Phase::Flying; break;
    case Kind::Parachute: chute = true; break;
    case Kind::Armour: w.armour = true; break;
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
    if (w.nailed) { w.vel = {0, 0, 0}, w.grounded = true; if (w.pos.y < water) drown(w); return; }
    auto feet = [&](float off) { return terrain.solid({w.pos.x, w.pos.y - R - off, w.pos.z}); };
    bool wasGrounded = w.grounded;
    float fallSpeed = -w.vel.y;
    w.grounded = w.vel.y <= 0 && feet(0.05f);
    if (w.grounded) {
        bool maxFall = cfg.wormpot & WP_MAX_FALL;
        float safe = maxFall ? FALL_SAFE * 0.7f : FALL_SAFE;
        if (!wasGrounded && fallSpeed > safe && (cfg.scheme.fallDamage || maxFall) && !(cfg.wormpot & WP_WORMS_DROWN)) {
            int dmg = (int)((fallSpeed - safe) * FALL_SCALE * (maxFall ? 3 : 1)), wi = int(&w - worms.data());
            if (dmg > 0) { w.hp -= dmg; selfHurt |= wi == current; emit(GameEvent::Hurt, w.pos, wi); }
        }
        Vector3 n = terrain.normal({w.pos.x, w.pos.y - R, w.pos.z});
        float flat = cfg.wormpot & WP_SLIPPY ? 0.97f : cfg.wormpot & WP_STICKY ? 0.5f : 0.85f;
        float keep = n.y < SLIDE_NY ? SLIDE_FRICTION : flat;  // too steep to stand: slide downhill
        if (n.y < SLIDE_NY) w.vel.x += n.x * gravity() * DT, w.vel.z += n.z * gravity() * DT;
        w.vel = {w.vel.x * keep, 0, w.vel.z * keep};
    } else w.vel.y -= gravity() * DT;
    Vector3 np = Vector3Add(w.pos, Vector3Scale(w.vel, DT));
    if (terrain.solid({np.x, w.pos.y, np.z})) { w.vel.x = w.vel.z = 0; np.x = w.pos.x; np.z = w.pos.z; }
    if (w.vel.y > 0 && terrain.solid({np.x, np.y + R, np.z})) { w.vel.y = 0; np.y = w.pos.y; }
    w.pos = np;
    for (int i = 0; i < 20 && feet(0); i++) {
        w.pos.y += 0.05f;
        if (w.vel.y < 0) w.vel.y = 0;
    }
    for (int k = 0; k < 4; k++) {  // body BODY_R clear of walls, sampled above the climbable steps; two walls in a corner
        Vector3 c = {w.pos.x, w.pos.y + (k & 1 ? 0.45f : 0.2f), w.pos.z}, n = terrain.normal(c);
        float in = terrain.sample(c) + BODY_R, l = sqrtf(n.x * n.x + n.z * n.z);
        if (in > 0 && l > 0.7f) w.pos.x += n.x / l * fminf(in, 0.1f), w.pos.z += n.z / l * fminf(in, 0.1f);
    }
    if (w.pos.y < water) drown(w);
}

void Game::drown(Worm &w) {
    w.alive = false;
    w.hp = 0;
    emit(GameEvent::Splash, w.pos, int(&w - worms.data()));  // Death once Settle has counted it to 0
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
    if (cfg.wormpot & WP_DOUBLE_DAMAGE) dmg *= 2;
    if (dmg <= 0 || (cfg.wormpot & WP_WORMS_DROWN)) return;
    w.hp -= dmg;
    int wi = int(&w - worms.data());
    emit(GameEvent::Hurt, w.pos, wi);
    if (wi == current) { selfHurt = true; return; }
    Worm &h = worms[current];
    if (cfg.rules & RULE_KARMA) h.hp -= (int)(dmg * 0.5f + 0.5f);
    if ((cfg.rules & RULE_VAMPIRE) && w.team != h.team) h.hp = std::max(h.hp, std::min(200, h.hp + (int)(dmg * 0.5f + 0.5f)));
    if (cfg.rules & RULE_HIGHLANDER) lastHitTeam[wi] = h.team;
}

// W4M Old Woman: each enemy she bumps loses 1-8 of a random weapon of its team to the thrower's team.
void Game::steal(const Worm &v) {
    int n = 0, thief = worms[current].team;
    for (int a : ammo[v.team]) n += a > 0;
    if (!n) return;
    int k = 0, pick = (int)(rand01() * n);
    while (ammo[v.team][k] <= 0 || pick--) k++;
    int take = std::min(ammo[v.team][k], 1 + (int)(rand01() * 8));
    ammo[v.team][k] -= take;
    if (ammo[thief][k] >= 0) ammo[thief][k] += take;
    emit(GameEvent::Collect, v.pos, current, k);
}

void Game::explode(Vector3 p, float radius, float damage, float poison, float push) {
    terrain.carve(p, radius);
    emit(radius >= 5 ? GameEvent::BigBoom : GameEvent::Boom, p);
    float reach = radius * 2;
    for (Worm &w : worms) {
        float d = Vector3Distance(w.pos, p);
        if (!w.alive || d >= reach) continue;
        float f = 1 - d / reach;
        if (w.nailed && d < radius + R) w.nailed = false;  // the ground around it is gone
        hurt(w, (int)(damage * f + 0.5f) * (w.armour ? ARMOUR : 100) / 100);
        if (poison > 0 && !(cfg.wormpot & WP_WORMS_DROWN)) w.poison = std::max(w.poison, (int)poison);
        if (w.nailed) continue;
        Vector3 dir = Vector3Normalize(Vector3Add(Vector3Subtract(w.pos, p), {0, 1, 0}));
        w.vel = Vector3Add(w.vel, Vector3Scale(dir, 14 * f * push * (cfg.wormpot & WP_STICKY ? 0.6f : 1) * (w.armour ? 0.4f : 1)));
    }
    for (Object &o : objects) {
        float d = Vector3Distance(o.pos, p);
        if (d >= reach || (o.tag >= 0 && o.type == Object::Crate)) continue;
        Vector3 dir = Vector3Normalize(Vector3Add(Vector3Subtract(o.pos, p), {0, 1, 0}));
        o.vel = Vector3Add(o.vel, Vector3Scale(dir, 10 * (1 - d / reach)));
        o.falling = false;
        if (o.type != Object::Mine) o.dead = true;
        else if (!o.dud && (o.fuse < 0 || o.fuse > 1)) o.fuse = 1;
    }
}

void Game::stepShots(const Input &in, bool detonate) {
    const float W = Terrain::NX * Terrain::VOX;
    std::vector<Projectile> spawned;
    auto touches = [&](Vector3 p) {
        for (const Worm &w : worms) if (w.alive && Vector3Distance(p, w.pos) < R + 0.3f) return true;
        for (const Object &o : objects) if (o.type == Object::Target && Vector3Distance(p, o.pos) < 0.7f) return true;
        return false;
    };
    for (size_t i = 0; i < shots.size();) {
        Projectile &s = shots[i];
        const WeaponDef &wd = WEAPONS[s.weapon];
        bool boom = false, timed = wd.fuse > 0 && !s.child;
        Vector3 np;
        bool bomber = wd.kind == Kind::Airstrike && !s.child;  // the plane; fuse > 0: the steered Bovine Blitz
        bool walker = !s.child && (wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman || (wd.kind == Kind::Scouser && !s.stage) ||
                                   (wd.kind == Kind::SuperSheep && wd.walks && !s.stage));
        if (walker) {
            // walks in its launch direction, climbs small steps, hops at walls; W4M: the old woman and the scouser are steered
            if (wd.kind == Kind::OldWoman || wd.kind == Kind::Scouser) {
                float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 2 * DT;
                s.vel.x = sinf(yaw) * wd.speed, s.vel.z = cosf(yaw) * wd.speed;
            }
            bool ground = terrain.solid({s.pos.x, s.pos.y - 0.35f, s.pos.z});
            s.vel.y = ground && s.vel.y <= 0 ? 0 : s.vel.y - gravity() * DT;
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            float climb = 0;
            while (climb <= 0.6f && terrain.solid({np.x, np.y - 0.3f + climb, np.z})) climb += 0.05f;
            if (climb > 0.6f) { np.x = s.pos.x; np.z = s.pos.z; if (ground) s.vel.y = 7; }
            else np.y += climb;
            if (terrain.solid({np.x, np.y + 0.3f, np.z})) { np = s.pos; s.vel.y = fminf(s.vel.y, 0); }
            s.pos = np;
            boom = detonate && (wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman);
            if (detonate && wd.kind == Kind::SuperSheep) {  // takes off: the flight gets its own lifetime
                s.stage = 1, s.fuse = wd.fuse;
                s.vel = Vector3Scale(Vector3Normalize({s.vel.x, Vector2Length({s.vel.x, s.vel.z}) * tanf(SHEEP_TAKEOFF), s.vel.z}), wd.speed);
                timer = std::max(timer, (int)(wd.fuse * 60) + 60);
            }
            for (Worm &v : worms) {
                int vi = int(&v - worms.data());
                if (!v.alive || vi == current || vi == s.prey || Vector3Distance(np, v.pos) > R + 0.4f) continue;
                if (wd.kind == Kind::Scouser) {  // swallows it and floats away
                    s.stage = 1, s.prey = vi, s.fuse = SCOUSER_FLOAT, v.nailed = false;
                    break;
                }
                if (wd.kind == Kind::OldWoman && v.team != worms[current].team) steal(v), s.prey = vi;
            }
        } else if (wd.kind == Kind::SuperSheep && !s.child) {
            // steered by the stick at constant speed, no gravity
            float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 2 * DT;
            float pitch = Clamp(asinf(Clamp(s.vel.y / fmaxf(Vector3Length(s.vel), 0.01f), -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
            s.vel = Vector3Scale({cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}, wd.speed);
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            boom = detonate || terrain.solid(np) || touches(np);
            s.pos = np;
        } else if (bomber && wd.fuse <= 0) {  // air strike plane: s.prey bombs dropped, s.stage ticks to the next
            if (s.stage > 0) s.stage--;
            else spawned.push_back({s.pos, {0, -wd.speed, 0}, s.weapon, 0, true, 1}), s.prey++, s.stage = STRIKE_TICKS - 1;
            np = s.pos = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            boom = s.prey >= wd.clusters;
        } else if (bomber) {  // banks on the stick; s.prey: payloads dropped, s.stage: ticks to the next drop
            float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 0.8f * DT;
            s.vel = {sinf(yaw) * wd.speed, 0, cosf(yaw) * wd.speed};
            np = s.pos = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            if (s.stage > 0) s.stage--;
            else if (detonate && s.prey < wd.clusters)
                spawned.push_back({Vector3Add(s.pos, {0, -1, 0}), Vector3Scale(s.vel, 0.3f), s.weapon, 0, true, 1}), s.prey++, s.stage = (int)(BOMBER_GAP * 60);
            boom = s.prey >= wd.clusters && !s.stage;
        } else if (wd.kind == Kind::Scouser) {
            // inflated: floats up to a slow climb and drifts with the wind
            s.vel.y += (1.2f - s.vel.y) * 2 * DT;
            s.vel.x += wind * 3 * DT;
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            if (terrain.solid(np)) s.vel = Vector3Scale(s.vel, -0.3f);
            else s.pos = np;
            if (s.prey >= 0 && worms[s.prey].alive) worms[s.prey].pos = Vector3Subtract(s.pos, {0, 0.6f, 0}), worms[s.prey].vel = s.vel, worms[s.prey].grounded = false;
        } else {
            bool homing = wd.kind == Kind::Homing && (s.fuse += DT) > HOMING_LOCK && s.fuse < HOMING_LOCK + HOMING_TIME;  // fuse: flight time
            if (homing) s.vel = Vector3Lerp(s.vel, Vector3Scale(Vector3Normalize(Vector3Subtract(s.aim, s.pos)), wd.speed), 3 * DT);
            else s.vel.y -= gravity() * DT;
            if (wd.kind == Kind::Airstrike && wd.fuse > 0) s.vel.y = fmaxf(s.vel.y, -COW_CHUTE);  // the bomber's cows come down under a chute
            if (wd.wind || (cfg.wormpot & WP_WIND_ALL)) s.vel.x += wind * 6 * DT;
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
        timed = timed && (!wd.restFuse || s.fuse < wd.fuse || Vector3Length(s.vel) < 1);
        if (timed && wd.restFuse && s.fuse == wd.fuse) emit(GameEvent::Hallelujah, s.pos, -1, s.weapon);  // at rest: the choir, then the blast
        if (timed && (s.fuse -= DT) < DT / 2) boom = true;  // n s = exactly 60 n ticks, whatever the float drift
        // off the map a shot flies on until it falls into the sea
        bool sank = s.pos.y < water - 2, gone = sank || s.pos.x < -100 || s.pos.z < -100 || s.pos.x > W + 100 || s.pos.z > W + 100;
        if (sank && !boom) emit(GameEvent::Splash, {s.pos.x, water, s.pos.z}, -1, s.weapon);
        if (boom && bomber) {  // flies off
        } else if (boom && wd.kind == Kind::Scouser) {  // W4M: pops and drops its catch, empty it bursts harmlessly
            emit(GameEvent::Boom, s.pos, -1, s.weapon);
            if (s.prey >= 0 && worms[s.prey].alive) hurt(worms[s.prey], (int)wd.damage);
        } else if (boom) {
            s.pos = np;
            bool animal = wd.kind == Kind::Sheep || wd.kind == Kind::SuperSheep || wd.kind == Kind::Donkey || wd.kind == Kind::OldWoman;
            float super = (cfg.wormpot & (animal ? WP_SUPER_ANIMALS : WP_SUPER_EXPLOSIVES)) ? 1.5f : 1;
            explode(np, s.child ? wd.cradius : wd.radius, (s.child ? wd.cdamage : wd.damage) * super, s.child ? 0 : wd.poison, super);
            if (!s.child && wd.poison > 0 && wd.fuse > 0) gas.push_back({np, GAS_LIFE, wd.poison});  // timed poison shell: the gas canister
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
    events.clear();  // also when over: GameOver must reach the listeners once
    if (phase == Phase::GameOver) return;
    uint8_t pressed = in.buttons & ~prevButtons;
    prevButtons = in.buttons;
    Worm &w = worms[current];
    bool detonate = phase == Phase::Flying && (pressed & Input::FIRE), tool = roped || jetting;
    bool drop = phase == Phase::Aim && dropping();  // W4M: the turn waits for the dropped crate to land
    if (landHold > 0) landHold--;
    Phase before = phase;

    if (phase == Phase::Aim && hotSeat > 0 && !drop) hotSeat = in.turn || in.walk || in.aim || in.buttons ? 0 : hotSeat - 1;
    if (w.alive && !drop && (phase == Phase::Aim || phase == Phase::Retreat)) {
        w.yaw += in.turn / 127.0f * 2.5f * DT;
        if (pressed & Input::ABOUT_FACE) w.yaw += PI;
        const float ws = cfg.wormpot & WP_QUICK_WALK ? 6 : 3;
        if (w.grounded && in.walk && !jumpDelay && !w.nailed) {
            Vector3 np = {w.pos.x + sinf(w.yaw) * in.walk / 127.0f * ws * DT, w.pos.y, w.pos.z + cosf(w.yaw) * in.walk / 127.0f * ws * DT};
            float climb = 0;
            while (climb <= 0.6f && terrain.solid({np.x, np.y - R + climb, np.z})) climb += 0.05f;
            if (climb <= 0.6f && !terrain.solid({np.x, np.y + climb + R, np.z})) { np.y += climb; w.pos = np; }
            int i = 0;
            for (; i < 12 && !terrain.solid({w.pos.x, w.pos.y - R - 0.05f, w.pos.z}); i++) w.pos.y -= 0.05f;
            if (i == 12) w.vel = Vector3Scale({sinf(w.yaw), 0, cosf(w.yaw)}, in.walk / 127.0f * ws * WALK_OFF);  // walked off a ledge
        }
        // W4M: JUMP waits JUMP_WINDOW ticks, a second press in that window makes it a backflip
        bool flip = jumpDelay && (pressed & Input::JUMP);
        if ((pressed & Input::JUMP) && !jumpDelay && w.grounded && !tool && !w.nailed && !(cfg.wormpot & WP_NO_JUMPING)) jumpDelay = JUMP_WINDOW;
        else if (jumpDelay && (flip || --jumpDelay == 0)) {
            jumpDelay = 0;
            if (w.grounded) {
                float side = flip ? FLIP_BACK : JUMP_FWD;
                w.vel = {sinf(w.yaw) * side, flip ? FLIP_UP : JUMP_UP, cosf(w.yaw) * side};
                w.grounded = false;
                emit(GameEvent::Jump, w.pos, current);
            }
        }
    } else jumpDelay = 0;
    // rope and jetpack outlast the attack: still steered while the shot flies and during the retreat
    if (w.alive && !drop && (phase == Phase::Aim || (tool && (phase == Phase::Flying || phase == Phase::Retreat)))) {
        Vector3 push = Vector3Scale({sinf(w.yaw), 0, cosf(w.yaw)}, in.walk / 127.0f * DT);
        bool armed = phase == Phase::Aim && !utility(WEAPONS[weapon].kind);  // a weapon in hand: FIRE and the aim axis are its own
        if (roped) {
            if (pressed & Input::JUMP) roped = false;
            if (!armed) ropeLen = Clamp(ropeLen - in.aim / 127.0f * 6 * DT, 1, ropeMax);
            w.vel = Vector3Add(w.vel, Vector3Scale(push, 6));
        } else if (jetting) {
            if ((pressed & Input::JUMP) || (fuel <= 0 && w.grounded)) jetting = false;
            if (!armed && (in.buttons & Input::FIRE) && fuel > 0) { w.vel.y += thrust * DT; fuel -= DT; }
            w.vel = Vector3Add(w.vel, Vector3Scale(push, 8));
            w.vel.x *= 0.98f;
            w.vel.z *= 0.98f;
        } else if (chute && !w.grounded) w.vel = Vector3Add(w.vel, Vector3Scale(push, 5));
        if (phase == Phase::Aim && !tool) armed = true;
        if (phase == Phase::Aim) {
            if (armed) w.pitch = Clamp(w.pitch + in.aim / 127.0f * 1.5f * DT, -1.2f, 1.45f);
            if ((pressed & Input::NEXT_WEAPON) && !shotsLeft) nextWeapon(w.team);
            if (WEAPONS[weapon].userFuse) fuses[w.team] = std::clamp(fuses[w.team] + !!(pressed & Input::FUSE_UP) - !!(pressed & Input::FUSE_DOWN), 1, 5);
            if (armed && (ammo[w.team][weapon] || shotsLeft) && !(w.nailed && !nailUsable(WEAPONS[weapon].kind))) {
                if (!powered(WEAPONS[weapon].kind)) { if (pressed & Input::FIRE) use(w); }
                else {
                    if (in.buttons & Input::FIRE) power = fminf(1, power + DT / 1.5f);
                    if (power > 0 && (!(in.buttons & Input::FIRE) || power >= 1)) use(w);
                }
            }
        }
    }

    // W4M: the parachute in hand opens by itself on a long fall (past a jump's landing speed)
    if (!chute && !tool && w.alive && !w.grounded && w.vel.y < -FALL_SAFE * 0.8f && (phase == Phase::Aim || phase == Phase::Retreat) &&
        WEAPONS[weapon].kind == Kind::Parachute && ammo[w.team][weapon])
        use(w);
    if (before != phase && phase == Phase::Flying) timer = 20 * 60;  // only a stuck shot outlasts this
    if (chute && !w.grounded && w.vel.y < -2.5f) w.vel.y = -2.5f;  // below FALL_SAFE: no fall damage
    if (chute && !w.grounded) w.vel.x += (wind * 2 - w.vel.x) * DT;  // drifts downwind, up to 2 m/s per wind unit
    for (Worm &x : worms)
        if (roped && &x == &w) stepRope(x);
        else stepWorm(x);
    stepShots(in, detonate);
    stepObjects();
    for (size_t i = 0; i < gas.size();) {
        Gas &c = gas[i];
        c.pos.x += wind * DT;
        for (Worm &x : worms)
            if (x.alive && Vector3Distance(x.pos, c.pos) < GAS_RADIUS && !(cfg.wormpot & WP_WORMS_DROWN)) x.poison = std::max(x.poison, (int)c.poison);
        if ((c.life -= DT) <= 0) gas.erase(gas.begin() + i);
        else i++;
    }

    if ((cfg.rules & RULE_ROPE_RACE) && phase != Phase::GameOver)
        for (const Worm &x : worms)
            if (x.alive && Vector3Distance(x.pos, raceFinish) < 2.0f) {
                phase = Phase::GameOver;
                winner = x.team;
                emit(GameEvent::GameOver, raceFinish);
                break;
            }

    if (!hotSeat) clock++;
    // W4M: the turn ends once at most one team still stands, whatever is left of it (shots, utility)
    int standing = -1, teamsLeft = 0;
    for (const Worm &x : worms)
        if (x.alive && x.hp > 0 && x.team != standing) standing = x.team, teamsLeft += teamsLeft < 2;
    bool over = !cfg.mission && teamsLeft <= 1;
    switch (phase) {
    case Phase::Aim:
        if (!w.alive || selfHurt || over || (!hotSeat && !drop && --timer <= 0)) { phase = Phase::Settle; timer = 300; roped = jetting = chute = false; }
        break;
    case Phase::Flying:
        if (shots.empty() || --timer <= 0) { shots.clear(); phase = Phase::Retreat; timer = cfg.scheme.retreatTime * 60; }
        break;
    case Phase::Retreat:
        if (shots.empty() && (!w.alive || selfHurt || over || --timer <= 0)) { phase = Phase::Settle; timer = 300; jumpDelay = 0; roped = jetting = chute = false; }
        break;
    case Phase::Settle: {
        bool still = shots.empty();
        for (const Worm &x : worms) still = still && (!x.alive || x.grounded);
        for (const Object &o : objects) still = still && o.fuse < 0 && Vector3LengthSqr(o.vel) < 0.01f;
        if (!countGroup.empty()) {  // W4M: the labels count down together, then the dead blow up one by one (its death queue)
            int boom = countBoom(), dead = 0;
            ++countT;
            for (int i : countGroup) {
                Worm &x = worms[i];
                if (x.hp > 0) continue;
                int at = blastAt(i);
                dead++;
                if (countT == at && !x.alive && x.counted > 0) {  // drowned: blows up at the surface, no grave
                    x.counted = 0;  // gone: the renderer stops drawing it afloat
                    emit(GameEvent::Boom, {x.pos.x, water, x.pos.z});
                    emit(GameEvent::Death, x.pos, i);
                }
                if (countT >= at && x.alive) {
                    if ((cfg.rules & RULE_HIGHLANDER) && lastHitTeam[i] >= 0 && lastHitTeam[i] != x.team)
                        for (size_t wi = 0; wi < WEAPONS.size(); wi++)
                            if (ammo[x.team][wi] && ammo[lastHitTeam[i]][wi] >= 0) ammo[lastHitTeam[i]][wi]++;
                    x.alive = false;
                    terrain.carve(x.pos, 0.8f);
                    emit(GameEvent::Boom, x.pos);
                    emit(GameEvent::Death, x.pos, i);
                }
            }
            if (countT >= (dead ? blastAt(-1) : boom)) {
                for (int i : countGroup) worms[i].counted = std::max(0, worms[i].hp);
                countGroup.clear();
            }
            break;
        }
        if (--timer <= 0 || (still && timer < 270)) {
            if (cfg.rules & RULE_KING)  // king gone: his team counts down to 0 and blows up like any dead worm
                for (int t = 0; t < teams; t++)
                    if (t * perTeam < (int)worms.size() && !worms[t * perTeam].alive)
                        for (int k = 1; k < perTeam; k++) worms[t * perTeam + k].hp = std::min(worms[t * perTeam + k].hp, 0);
            auto pending = [](const Worm &x) { return x.alive ? x.hp <= 0 || x.hp != x.counted : x.counted > 0; };
            for (const Worm &x : worms)  // first pending worm and those near it: one camera shot
                if (pending(x)) {
                    for (const Worm &y : worms)
                        if (pending(y) && Vector3Distance(x.pos, y.pos) < COUNT_SPAN) countGroup.push_back(int(&y - worms.data()));
                    countT = 0;
                    break;
                }
            if (!countGroup.empty()) break;
            picked[w.team] = weapon;
            beginTurn(w.team);
        }
        break;
    }
    case Phase::GameOver: break;
    }
    if (cfg.mission && phase != Phase::GameOver) missionStep(*this);
}

uint32_t Game::checksum() const {
    uint32_t h = 2166136261u;
    auto mix = [&](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
    };
    for (const Worm &w : worms) { mix(&w.pos, sizeof w.pos); mix(&w.vel, sizeof w.vel); mix(&w.hp, sizeof w.hp); mix(&w.yaw, sizeof w.yaw); mix(&w.pitch, sizeof w.pitch); mix(&w.alive, sizeof w.alive); mix(&w.poison, sizeof w.poison); mix(&w.counted, sizeof w.counted); mix(&w.nailed, 1); mix(&w.armour, 1); }
    for (const Projectile &s : shots) { mix(&s.pos, sizeof s.pos); mix(&s.vel, sizeof s.vel); mix(&s.weapon, sizeof s.weapon); mix(&s.fuse, sizeof s.fuse); mix(&s.hits, sizeof s.hits); mix(&s.stage, sizeof s.stage); mix(&s.prey, sizeof s.prey); }
    mix(&chute, 1);
    mix(&landHold, sizeof landHold);
    mix(&ropeShots, sizeof ropeShots);
    mix(lastHitTeam.data(), lastHitTeam.size() * sizeof(int));
    mix(&rng, sizeof rng);
    mix(&current, sizeof current);
    mix(&weapon, sizeof weapon);
    mix(picked.data(), picked.size() * sizeof(int));
    mix(fuses.data(), fuses.size() * sizeof(int));
    mix(&water, sizeof water);
    mix(&clock, sizeof clock);
    mix(&hotSeat, sizeof hotSeat);
    mix(&jumpDelay, sizeof jumpDelay);
    mix(&selfHurt, sizeof selfHurt);
    mix(&cfg.scheme, sizeof cfg.scheme);
    mix(&cfg.rules, sizeof cfg.rules);
    mix(&cfg.wormpot, sizeof cfg.wormpot);
    mix(&suddenDeath, sizeof suddenDeath);
    mix(&raceFinish, sizeof raceFinish);
    mix(countGroup.data(), countGroup.size() * sizeof(int)), mix(&countT, sizeof countT);
    for (const Gas &c : gas) mix(&c, sizeof c);
    for (const Object &o : objects) { mix(&o.type, 1); mix(&o.pos, sizeof o.pos); mix(&o.vel, sizeof o.vel); mix(&o.weapon, sizeof o.weapon); mix(&o.fuse, sizeof o.fuse); mix(&o.falling, 1); mix(&o.dead, 1); mix(&o.team, sizeof o.team); mix(&o.tag, sizeof o.tag); mix(&o.dud, 1); mix(&o.courtesy, sizeof o.courtesy); }
    if (cfg.mission) {
        mix(&run.result, 5 * sizeof(int));
        mix(run.state.data(), run.state.size()), mix(run.met.data(), run.met.size()), mix(idle.data(), idle.size());
    }
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
        mix(&wd.userFuse, sizeof wd.userFuse), mix(&wd.restFuse, sizeof wd.restFuse), mix(&wd.walks, 1), mix(&wd.pins, 1);
    }
    return h;
}

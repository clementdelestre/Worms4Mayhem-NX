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
                              "melee", "homing", "mine", "scouser", "sentry", "abduction", "flood", "parachute", "skipgo", "surrender", "changeworm", "armour",
                              "girder", "binoculars", "bubble", "icarus", "doubledamage", "cratespy"};

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
            {"bounce", &w.bounce}, {"cluster_radius", &w.cradius}, {"cluster_damage", &w.cdamage}, {"poison", &w.poison},
            {"reach", &w.blast[0]}, {"push", &w.blast[1]}, {"push_reach", &w.blast[2]}, {"push_depth", &w.blast[3]},
            {"cluster_reach", &w.cblast[0]}, {"cluster_push", &w.cblast[1]}, {"cluster_push_reach", &w.cblast[2]}, {"cluster_push_depth", &w.cblast[3]}, {"lift", &w.lift}, {"gravity", &w.grav}, {"min_speed", &w.base}, {"fuse_height", &w.fuseHeight}, {"fuse_size", &w.fuseSize}};
        const struct { const char *k; int *v; } ints[] = {{"count", &w.count}, {"clusters", &w.clusters}, {"shots", &w.shots}, {"crate_weight", &w.weight}, {"retreat", &w.retreat}, {"post_launch", &w.postLaunch}};
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
            if (key == "avoid_land") w.avoid = b;
            if (key == "user_fuse") w.userFuse = b;
            if (key == "rest_fuse") w.restFuse = b;
            if (key == "walks") w.walks = b;
            if (key == "fuse_shown") w.fuseShown = b;
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
bool customWeapon(int i) { return i >= (int)baseWeapons; }

bool saveCustomWeapons(const char *path, const std::vector<WeaponDef> &list) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    auto clean = [](std::string s) { s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '"' || c == '\\' || (unsigned char)c < 32; }), s.end()); return s; };
    fputs("[\n", f);
    for (size_t i = 0; i < list.size(); i++) {
        const WeaponDef &w = list[i];
        fprintf(f, "  {\"name\": \"%s\", \"kind\": \"%s\", \"radius\": %g, \"damage\": %g, \"speed\": %g, \"fuse\": %g, \"bounce\": %g, "
                   "\"clusters\": %d, \"cluster_radius\": %g, \"cluster_damage\": %g, \"count\": %d, \"shots\": %d, \"crate_weight\": %d, "
                   "\"poison\": %g, \"wind\": %s, \"avoid_land\": %s, \"model\": \"%s\", \"icon\": \"%s\"}%s\n",
                clean(w.name).c_str(), KINDS[(int)w.kind], w.radius, w.damage, w.speed, w.fuse, w.bounce, w.clusters, w.cradius, w.cdamage,
                w.count, w.shots, w.weight, w.poison, w.wind ? "true" : "false", w.avoid ? "true" : "false", clean(w.model).c_str(), clean(w.icon).c_str(), i + 1 < list.size() ? "," : "");
    }
    fputs("]\n", f);
    return fclose(f) == 0;
}

int wormpotReel(uint32_t wp, int r) { return int(wp >> (WORMPOT_COUNT + 4 * r) & 15) - 1; }
void wormpotPick(uint32_t &wp, int r, int m) {
    wp = (wp & ~(15u << (WORMPOT_COUNT + 4 * r)) & ~((1u << WORMPOT_COUNT) - 1)) | uint32_t(m + 1) << (WORMPOT_COUNT + 4 * r);
    for (int q = 0; q < 3; q++) if (int k = wormpotReel(wp, q); k >= 0) wp |= 1u << WORMPOT_REEL[q][k];
}
uint32_t wormpotSlots(uint32_t wp) {
    if (wp >> WORMPOT_COUNT) return wp;
    uint32_t out = 0;
    for (int b = 0; b < WORMPOT_COUNT; b++)
        for (int q = 0; q < 3 && (wp >> b & 1); q++) {
            auto it = std::find(WORMPOT_REEL[q].begin(), WORMPOT_REEL[q].end(), b);
            if (wormpotReel(out, q) < 0 && it != WORMPOT_REEL[q].end()) { wormpotPick(out, q, int(it - WORMPOT_REEL[q].begin())); break; }
        }
    return out;
}

const WormpotMode WORMPOT_MODES[20] = {
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
    {"Multiple Girders", "More than one girder can be placed during one turn."},  // FETXT.WPotHelp.MUltiGirder
    {"No Bombing", "This will stop the worm's ability to drop weapons whilst using movement utilities."},  // FETXT.WPotHelp.NoBombing
};
const std::vector<int> WORMPOT_REEL[3] = {{1, 2, 11, 0, 12, 10, 3, 13, 4, 16, 8, 18, 19, 17},
                                         {1, 2, 11, 0, 10, 15, 14, 7, 6, 3, 13, 8, 18, 19, 17},
                                         {1, 2, 11, 0, 9, 8, 5, 18, 19, 17}};


// Order: turn, retreat (LandTime), hot seat (HotSeat, 10 s in every W4M scheme), round (min), energy, crate %, weapon/health/utility shares, crate hp,
// mines, barrels, mine fuse, sudden death, fall damage, wind, weapon set. Mines, barrels, sets: ours.
// Shares: SchemeData WeaponChance / HealthChance / UtilityChance (CreateRandomCrate 0x4fa4b0; MysteryChance not modelled).
const std::vector<SchemePreset> SCHEMES = {
    {"Standard", {45, 5, 10, 20, 100, 40, 30, 30, 20, 25, 5, 4, 3, 0, 1, 1, 0}, "Airstrike 5|Banana Bomb 8|Holy Hand Grenade 3|Homing Missile 2|Super Sheep 5|Icarus Potion 2|Binoculars 2|* 3"},
    {"Beginner", {90, 5, 10, 20, 100, 50, 30, 30, 20, 50, 3, 4, 5, 0, 0, 0, 0}, "Homing Missile 2"},
    {"Pro", {30, 0, 10, 20, 100, 30, 10, 30, 50, 25, 5, 4, Scheme::FUSE_RANDOM, 0, 1, 3, 0}, "Icarus Potion 4|Sentry Gun 5|Sniper Rifle 3|Starburst 4|Binoculars 3"},
    {"BnG", {30, 5, 10, 10, 150, 40, 40, 10, 40, 25, 5, 4, 5, 0, 1, 1, Scheme::SET_BNG}, "Concrete Donkey 3|Icarus Potion 2|Bubble Trouble 3|* 8"},
    {"Shopping", {60, 5, 10, 20, 100, 100, 60, 20, 20, 25, 5, 4, Scheme::FUSE_RANDOM, 0, 1, 1, Scheme::SET_CRATES}, ""},
    {"All Action", {30, 5, 10, 10, 200, 40, 30, 30, 20, 25, 5, 6, 1, 0, 1, 1, 0}, "Airstrike 5|Banana Bomb 5|Homing Missile 2|Super Sheep 3|Super Airstrike 6"},
    {"Strategy", {30, 0, 10, 30, 100, 40, 10, 10, 50, 25, 8, 4, 5, 0, 0, 3, 0}, "Airstrike 6|Baseball Bat 2|Dynamite 4|Jetpack 4|Shotgun 3|Icarus Potion 4|Sniper Rifle 4|* 4"},
    {"Family", {90, 8, 10, 20, 125, 50, 30, 30, 20, 50, 3, 4, 5, 0, 0, 0, 0}, ""},
    {"Mega Power", {30, 5, 10, 10, 200, 50, 40, 40, 20, 100, 5, 8, Scheme::FUSE_RANDOM, 0, 1, 1, Scheme::SET_UNLIMITED}, "Concrete Donkey 8|Alien Abduction 8|Fatkins Strike 8|Sentry Gun 8|Super Airstrike 8"},
    {"Holy Grail", {45, 5, 10, 30, 100, 60, 30, 30, 30, 50, 5, 4, 1, 0, 0, 1, 0}, "Concrete Donkey 16|Super Sheep 4"},
    {"Darksider", {45, 5, 10, 30, 100, 50, 10, 10, 50, 25, 5, 4, 5, 0, 0, 1, 0}, "Airstrike 4|Cluster Grenade 2|Dynamite 2|Homing Missile 6|Landmine 2|Shotgun 2|Flood 2|Tail Nail 2|Poison Arrow 2|Sniper Rifle 2|Starburst 2|* 2"},
};

static constexpr float GRAVITY = 12.5f;  // W4M Gravity -0.00025 units/ms² (WEAPTWK), 20 units = 1 m
// W4M FallDamage 0x5ac3e0: none up to 0.3 units/ms (15 m/s), then trunc((v - 0.3) x FallDamageRatio 100) + 1 = 2 hp per m/s
static constexpr float FALL_SAFE = 15, FALL_SCALE = 2;
// W4M Sliding 0x5afbe0 (TWEAK WXWorm.*_Default / _Slippy): cos SlideAngle 60 / 10; Start/StopSlideVel 0.2 / 0.06 and 0.01 units/ms;
// SlideFriction 0.95 / 0.999 per 20 ms frame, here per 1/60 s tick. Fall 0x5b14c7 keeps the walk speed (WalkOffCliffVelMulti unread).
static constexpr float SLIDE_NY = 0.5f, SLIDE_FRICTION = 0.9582f, START_SLIDE = 10, STOP_SLIDE = 3, WALK_OFF = 1;
static constexpr float SLIPPY_NY = 0.9848f, SLIPPY_FRICTION = 0.99917f, SLIPPY_SLIDE = 0.5f;
static const float WIND_CAP[] = {0, 0.3f, 0.6f, 1};  // WindMaxStrength / 10: 0, 3, 10 in the W4M schemes; 6: ours

// W4M melee box: in front of the attacker, a worm height up or down; the Fire Punch (a leap) also reaches above.
bool meleeHits(const Worm &a, Vector3 p, const WeaponDef &wd) {
    Vector3 d = Vector3Subtract(p, a.pos);
    float ahead = d.x * sinf(a.yaw) + d.z * cosf(a.yaw), side = fabsf(d.x * cosf(a.yaw) - d.z * sinf(a.yaw));
    return ahead > -0.2f && ahead < 2 && side < 1 && d.y > -1.2f && d.y < (wd.fuse > 0 ? 3 : 1.2f);
}

// W4M land probe 0x91ffc8: the centre and the foot tripod (+-4, -3) (0, 5) units, world axes; any hit carries the worm.
// *n: the mean land normal of the hits, as W4M 0x59ef90 averages those level with the highest
static bool footing(const Terrain &t, Vector3 foot, Vector3 *n = nullptr) {
    const Vector2 O[4] = {{0, 0}, {0.2f, -0.15f}, {-0.2f, -0.15f}, {0, 0.25f}};
    int top[4], best = -1;  // land above the probe height, in units (5 at most)
    for (int i = 0; i < 4; i++) {
        top[i] = -1;
        while (top[i] < 5 && t.solid({foot.x + O[i].x, foot.y + (top[i] + 1) * 0.05f, foot.z + O[i].y})) top[i]++;
        if (!n && top[i] >= 0) return true;
        best = std::max(best, top[i]);
    }
    if (best < 0) return false;
    Vector3 sum{};
    for (int i = 0; i < 4; i++)
        if (top[i] >= best - 1) sum = Vector3Add(sum, t.normal({foot.x + O[i].x, foot.y + top[i] * 0.05f, foot.z + O[i].y}));
    *n = Vector3LengthSqr(sum) > 1e-8f ? Vector3Normalize(sum) : Vector3{0, 1, 0};
    return true;
}

bool walkStep(const Terrain &t, Vector3 &pos, float yaw, float dist, Vault *vault) {
    const float R = Game::R, U = 0.05f;  // U: one W4M unit
    Vector3 f = {sinf(yaw), 0, cosf(yaw)}, np = Vector3Add(pos, Vector3Scale(f, dist)), held = np;
    float climb = 0;
    while (climb <= Game::STEP_UP && t.solid({np.x, pos.y - R + climb, np.z})) climb += U;
    clearWalls(t, held);
    if (Vector3DotProduct(Vector3Subtract(held, pos), f) * dist < 0.5f * dist * dist) {  // a face holds the body off: W4M vault onto the highest ground the front foot finds
        Vector3 toe = Vector3Add(np, Vector3Scale(f, copysignf(Game::BODY_R + 0.1f, dist)));
        float up = Game::STEP_UP;
        while (up > 0 && !t.solid({toe.x, pos.y - R + up, toe.z})) up -= U;
        if (up > Game::STEP && up < Game::STEP_UP - U && !t.solid({toe.x, pos.y + up + U + R, toe.z})) np = toe, climb = up + U;
    }
    np.y += climb;
    Vector3 n = {0, 1, 0};
    bool near = false;  // ground within 5 units under the candidate (W4M -5 <= d)
    for (int k = 1; k <= 5 && !near; k++) near = footing(t, {np.x, np.y - R - k * U, np.z}, &n);
    if (climb > Game::STEP) {  // W4M 0x5b1209: walkable (0x4adda0) and Fits, else blocked
        if (climb <= Game::STEP_UP && n.y >= SLIDE_NY && fits(t, pos, np)) {
            if (vault) { *vault = {pos, np, Vector3Scale(f, copysignf(1, dist)), {}, msTicks(250)}; return false; }  // 0x5b1285: no move this frame
            pos = np;
        }
    } else {
        for (int d = 0; near && d < 5 && !footing(t, {np.x, np.y - R - U, np.z}); d++) np.y -= U;  // set on the highest hit
        if (Vector3DotProduct(n, Vector3Subtract(np, pos)) >= 0 || n.y >= SLIDE_NY) {  // 0x5b1920: into the ground only if walkable
            // W4M push-out: the step tries +0..+4 units (0x5b194c), a drop +1..+5 after its plain fall (0x5b14e1)
            int k = near ? 0 : fits(t, pos, np) ? -1 : 1, last = near ? 4 : 5;
            if (k < 0) pos = np;
            else {
                for (; k <= last && !fits(t, pos, {np.x, np.y + k * U, np.z}); k++) {}
                if (k <= last) { pos = {np.x, np.y + k * U, np.z}; if (k) return false; }
            }
        }
    }
    int i = 0;
    for (; i < 12 && !footing(t, {pos.x, pos.y - R - U, pos.z}); i++) pos.y -= U;
    return i == 12;
}

// W4M Vaulting 0x5aca80: input along the start input or back to the old pos; 4 units per 20 ms toward the target, no collision
// test, snapped there when the 250 ms run out (ChangeState 0x5aa847 snaps to the target on leaving the state)
void vaultStep(Vector3 &pos, Vault &v, Vector3 input) {
    if (Vector3DotProduct(input, v.dir) <= 0) pos = v.from, v.t = 0;
    else if (--v.t <= 0) pos = v.to;
    else pos = Vector3MoveTowards(pos, v.to, 10 * Game::DT);
}

// Deepest density in the upper body: the W4M Fits 0x59edf0 rods (+-4 / -3 and 0 / +5 units, ~0.2 m) above our 0.7 m of plain steps.
static float body(const Terrain &t, Vector3 p) {
    float worst = -1;
    for (float h : {0.2f, 0.45f})
        for (int k = 0; k < 7; k++) worst = fmaxf(worst, t.sample({p.x + (k ? 0.2f * cosf(k * PI / 3) : 0), p.y + h, p.z + (k ? 0.2f * sinf(k * PI / 3) : 0)}));
    return worst;
}

bool fits(const Terrain &t, Vector3 from, Vector3 to) {  // W4M tests Fits before each move; a body already in land may still move out
    float d = body(t, to);
    return d <= 0 || d <= body(t, from);
}

void clearWalls(const Terrain &t, Vector3 &pos) {
    for (int k = 0; k < 4; k++) {  // above the 0.7 m of plain steps; two passes per height for a corner's two walls
        const float e = Terrain::VOX / 2;
        Vector3 c = {pos.x, pos.y + (k & 1 ? 0.45f : 0.2f), pos.z};
        Vector3 g = {t.sample({c.x + e, c.y, c.z}) - t.sample({c.x - e, c.y, c.z}), t.sample({c.x, c.y + e, c.z}) - t.sample({c.x, c.y - e, c.z}),
                     t.sample({c.x, c.y, c.z + e}) - t.sample({c.x, c.y, c.z - e})};
        float gl = Vector3Length(g), l = sqrtf(g.x * g.x + g.z * g.z);
        if (gl < 1e-4f || l < 0.7f * gl) continue;
        float in = Game::BODY_R + t.sample(c) * 2 * e / gl;  // density / |gradient|: the .vox field flattens well before its 0.25 m clamp
        if (in > 0) pos.x -= g.x / l * fminf(in, 0.1f), pos.z -= g.z / l * fminf(in, 0.1f);
    }
}

int substeps(Vector3 vel) { return 1 + (int)(Vector3Length(vel) * Game::DT / (Terrain::VOX / 2)); }

Vector3 launchPoint(const WeaponDef &d, Vector3 pos, float yaw) {
    float z = dropped(d) ? 13 : d.kind == Kind::Mine ? 10 : d.kind == Kind::Sheep || d.kind == Kind::SuperSheep ? 5 : d.kind == Kind::OldWoman ? 7 : d.kind == Kind::Scouser ? 10 : 0;
    float y = dropped(d) || d.kind == Kind::Mine ? -10 : 0;
    return {pos.x + sinf(yaw) * z / 20, pos.y - Game::R + (15 + y) / 20, pos.z + cosf(yaw) * z / 20};  // 20 units = 1 m
}

Vector3 muzzle(const Terrain &t, Vector3 pos, Vector3 spawn) {
    const Vector3 eye = {pos.x, pos.y - Game::R + 0.75f, pos.z}, d = Vector3Subtract(spawn, eye);
    Vector3 p = eye;
    int n = 1 + (int)(Vector3Length(d) / (Terrain::VOX / 2));
    for (int k = 1; k <= n; k++) {
        Vector3 q = Vector3Add(eye, Vector3Scale(d, (float)k / n));
        if (t.solid(q)) return p;
        p = q;
    }
    return spawn;
}

Vector3 restOn(const Terrain &t, Vector3 p, float r) {
    Vector3 s = p;
    for (int k = 0; k < 8 && !t.solid({s.x, s.y - 0.02f, s.z}); k++) s.y -= 0.02f;
    return t.solid({s.x, s.y - 0.02f, s.z}) ? Vector3Add(s, Vector3Scale(t.normal(s), r)) : p;  // in the air: as is
}

float flyBody(const Terrain &t, Vector3 &pos, Vector3 &vel, float e, int *contact) {
    const float R = Game::R;
    float landing = 0;
    int hits = 0;  // 1: a move did not Fit (W4M Ballistic 0x5afa78 / 0x5afb17: undone, rebounds), 2: a wall stopped it
    for (int k = 0, n = substeps(vel); k < n; k++) {
        Vector3 np = Vector3Add(pos, Vector3Scale(vel, Game::DT / n));
        bool wall = t.solid({np.x, pos.y, np.z}), side = !wall && !fits(t, pos, {np.x, pos.y, np.z});
        if (wall || side) { vel.x *= -e, vel.z *= -e; np.x = pos.x; np.z = pos.z; }  // wall normal taken along v
        bool roof = vel.y > 0 && t.solid({np.x, np.y + R, np.z}), up = vel.y > 0 && !roof && !fits(t, {np.x, pos.y, np.z}, np);
        if (roof || up) { vel.y *= -e; np.y = pos.y; }
        Vector3 foot = {np.x, np.y - R, np.z};
        bool down = false;
        Vector3 fn;
        if (vel.y < 0 && footing(t, foot, &fn)) {  // W4M Ballistic land hit: Rebound 0x5acea0 below n.y 0.2, else the landing keeps only vt
            float vn = Vector3DotProduct(vel, fn);
            bool face = fn.y < 0.2f;
            landing = -vel.y;
            if (vn < 0) vel = Vector3Subtract(vel, Vector3Scale(fn, vn * (face ? 1 + e : 1)));
            else vel.y = 0;
            if (!face) vel.y = fminf(vel.y, 0);
            else hits |= 2;  // a wall: Rebound
        } else if (vel.y < 0 && !fits(t, {np.x, pos.y, np.z}, np)) down = true, vel.y *= -e, np.y = pos.y;
        hits |= (side || up || down) | (wall || roof) << 1;
        // W4M lands a foot where its ray met land this frame (0x5af522): raised while it Fits, never above where the substep began + 1 unit
        for (float top = fmaxf(pos.y, np.y) + 0.05f; np.y < top && footing(t, {np.x, np.y - R, np.z}) && fits(t, np, {np.x, np.y + 0.05f, np.z});) np.y += 0.05f;
        pos = np;
    }
    if (contact) *contact = hits;
    return landing;
}

static const Vector2 PROBE[4] = {{0, 0}, {0.2f, -0.15f}, {-0.2f, -0.15f}, {0, 0.25f}};  // W4M 0x91ffc8: centre and foot tripod, world axes

// W4M 0x59ec70 down the 4 foot rays from 20 units above: the highest land, in units over the feet; -99 when none down to -5
static int probe(const Terrain &t, Vector3 feet) {
    int best = -99;
    for (Vector2 o : {Vector2{0, 0}, Vector2{0.2f, -0.15f}, Vector2{-0.2f, -0.15f}, Vector2{0, 0.25f}})
        for (int h = 20; h >= -5 && h > best; h--)
            if (t.solid({feet.x + o.x, feet.y + h * 0.05f, feet.z + o.y})) { best = h; break; }
    return best;
}

// W4M Sliding 0x5afbe0, one tick (K W4M frames); velocities are m/s, W4M's units/ms are 50 times smaller
static void slideStep(const Terrain &t, Vector3 &pos, Vector3 &vel, Motion &m, float &yaw, float g, uint32_t wormpot, float e) {
    const float R = Game::R, DT = Game::DT, U = 0.05f, K = DT / 0.02f;
    const bool slip = wormpot & WP_SLIPPY;
    const float start = slip ? SLIPPY_SLIDE : START_SLIDE, stop = slip ? SLIPPY_SLIDE : STOP_SLIDE, walkable = slip ? SLIPPY_NY : SLIDE_NY;
    auto landed = [&] { vel = {0, 0, 0}, m.slide = false, m.stuck = 0; };  // kWE_Landed, Ambulatory; every Landed clears the count
    const Vector3 n = m.normal;  // SupportNormal, stored at the landing and on each ground follow
    m.spin += Clamp((m.spinTo - m.spin) / 4, -0.0349f, 0.0349f) * K, yaw += m.spin * K;  // 0x47a1a0(k 3, 2 deg), yaw += rate
    if (m.air && Vector3LengthSqr(m.input) > 0) {  // input along the motion and not uphill: x0.003, else x0.0005 units/ms per frame
        // 0x5afe30 / 0x5afe68: (g.n) n.xz . input > 0 (input uphill) gives 0.0005; else (g.n) n.xz . v > 0 (moving uphill) gives 0.003
        bool upIn = m.input.x * n.x + m.input.z * n.z < 0, upV = vel.x * n.x + vel.z * n.z < 0;
        vel = Vector3Add(vel, Vector3Scale(m.input, (!upIn && upV ? 0.003f : 0.0005f) * 50 * K));
    }
    const float f = wormpot & WP_STICKY ? 0.5f : slip ? SLIPPY_FRICTION : SLIDE_FRICTION;  // sticky: ours
    vel = {(vel.x + n.x * n.y * g * DT) * f, vel.y * f, (vel.z + n.z * n.y * g * DT) * f};  // gravity's slope part acts on x, z only
    Vector3 cand = Vector3Add(pos, Vector3Scale(vel, DT));
    int d = probe(t, {cand.x, cand.y - R, cand.z});
    if (d > 5) {  // wall or step: slow lands, else one frame's ray along v rebounds it
        float l = Vector3Length(vel), best = 1e9f;
        if (l < start) return landed();
        Vector3 sum{}, dir = Vector3Scale(vel, 1 / l);  // 0x5b00e0: the 8 probe points (feet and heads, 20 units up) along v for a frame
        float near[8];
        for (int i = 0; i < 8; i++) {
            Vector2 o = PROBE[i % 4];
            Vector3 hit;
            near[i] = t.raycast({{pos.x + o.x, pos.y - R + (i < 4 ? 0 : 1.0f), pos.z + o.y}, dir}, l * DT, &hit) ? Vector3Distance(hit, {pos.x + o.x, pos.y - R + (i < 4 ? 0 : 1.0f), pos.z + o.y}) : 1e9f;
            best = fminf(best, near[i]);
        }
        if (best > 1e8f) return landed();
        for (int i = 0; i < 8; i++)  // 0x59ef90: the normals of the hits within 1 unit of the nearest
            if (near[i] <= best + U) {
                Vector2 o = PROBE[i % 4];
                Vector3 a = {pos.x + o.x, pos.y - R + (i < 4 ? 0 : 1.0f), pos.z + o.y};
                sum = Vector3Add(sum, t.normal(Vector3Add(a, Vector3Scale(dir, near[i]))));
            }
        Vector3 nh = Vector3LengthSqr(sum) > 1e-8f ? Vector3Normalize(sum) : Vector3Negate(dir), v = Vector3Scale(vel, 1.0f / 50);
        m.spinTo = (m.spinTo + 3 * Vector3DotProduct(n, Vector3CrossProduct(nh, v))) / 2;
        float vn = Vector3DotProduct(vel, nh);
        if (vn < 0) vel = Vector3Subtract(vel, Vector3Scale(nh, vn * (1 + e)));  // Rebound 0x5acea0, air control off
        m.air = false, m.stuck += 2;
    } else if (d < -5) {  // a drop: the velocity off the ground, Fall() if the body Fits at the old height
        Vector3 c = {cand.x, pos.y, cand.z};
        if (!fits(t, pos, c)) return landed();
        pos = c, vel = Vector3Subtract(vel, Vector3Scale(n, Vector3DotProduct(vel, n))), m.slide = m.air = false;
    } else {  // follow the ground: the highest hit + 0.1 unit
        Vector3 c = {cand.x, cand.y + (d + 1) * U, cand.z}, nn = n;
        if (!fits(t, pos, c)) return landed();
        pos = c, m.stuck = std::max(m.stuck - 1, 0);
        footing(t, {c.x, c.y - R - U, c.z}, &nn);
        m.spinTo -= 2 * Vector3DotProduct(Vector3CrossProduct(nn, n), Vector3Scale(vel, 1.0f / 50));
        m.normal = nn;  // 0x5b04a1
        if (nn.y >= walkable && Vector3LengthSqr(vel) < stop * stop) return landed();
    }
    if (m.stuck >= 20) landed();
}

void slideIfSteep(const Terrain &t, Vector3 pos, Vector3 &vel, Motion &m, Vector3 walk, uint32_t wormpot) {
    Vector3 n;
    if (!m.slide && footing(t, {pos.x, pos.y - Game::R - 0.05f, pos.z}, &n) && n.y < (wormpot & WP_SLIPPY ? SLIPPY_NY : SLIDE_NY))
        vel = walk, m.slide = true, m.spin = m.spinTo = 0, m.normal = n;  // event 17 (SupportNormal 0x5b1998); ChangeState zeroes the spin (0x5aaa0c)
}

float wormBody(const Terrain &t, Vector3 &pos, Vector3 &vel, bool &grounded, Motion &m, float &yaw, float g, uint32_t wormpot, float e) {
    const float R = Game::R, DT = Game::DT;
    bool was = grounded, slip = wormpot & WP_SLIPPY;  // assumed: SlippyMode puts worms on the W4M Slippy surface
    if (m.slide) {
        slideStep(t, pos, vel, m, yaw, g, wormpot, e);
        grounded = m.slide || Vector3LengthSqr(vel) == 0;
        clearWalls(t, pos);
        return 0;
    }
    float fall = -vel.y, landing = 0;
    Vector3 n;
    grounded = vel.y <= 0 && footing(t, {pos.x, pos.y - R - 0.05f, pos.z}, &n);
    if (grounded && was && Vector3LengthSqr(vel) > 0 && Vector3DotProduct(vel, n) >= 0) grounded = false;  // ImpulseWorm: not into the ground, Ballistic
    if (grounded && (!was || Vector3LengthSqr(vel) > 0)) {  // a landing, or a push along the ground (ImpulseWorm 0x5ad010): walk or slide
        if (!was) landing = fall;
        float vn = Vector3DotProduct(vel, n), start = slip ? SLIPPY_SLIDE : START_SLIDE;
        vel = Vector3Subtract(vel, Vector3Scale(n, vn));  // vt
        float v2 = Vector3LengthSqr(vel);
        if (!was && v2 < vn * vn) v2 *= 0.5f;  // W4M Ballistic 0x5af6be: |vt|² halved when below vn²
        if (n.y >= (slip ? SLIPPY_NY : SLIDE_NY) && v2 < start * start) vel = {0, 0, 0};  // lands walking
        else m.slide = true, m.spin = m.spinTo = 0;  // event 17, Sliding with Velocity = vt
        m.normal = n;  // SupportNormal 0x5af5ba
    } else if (grounded) vel = {0, 0, 0};  // Ambulatory: only the push-up below keeps the highest foot out of land, as W4M stands on it
    else vel.y -= g * DT / 2;  // half before and half after the move: exact like W4M Integrate 0x5a6e90
    int contact = 0;
    float hit = flyBody(t, pos, vel, e, &contact);
    if (grounded) hit = 0;  // already landed: Sliding deals no fall damage
    else if (!hit) vel.y -= g * DT / 2;
    if (contact & 2) m.air = false;  // Rebound 0x5acea0
    if (!grounded) {
        bool blocked = contact & 1;
        m.stuck = blocked ? m.stuck + 2 : std::max(m.stuck - 1, 0);
        if (blocked && m.stuck >= 20) vel = {0, 0, 0}, grounded = true, hit = 0;  // W4M 0x5af821: landed where it is, the count kept
    }
    clearWalls(t, pos);
    return fmaxf(landing, hit);
}

void walkerStep(const Terrain &t, Vector3 &pos, Vector3 &vel, float gravity) {
    bool ground = t.solid({pos.x, pos.y - 0.35f, pos.z});
    vel.y = ground && vel.y <= 0 ? 0 : vel.y - gravity * Game::DT;
    Vector3 np = Vector3Add(pos, Vector3Scale(vel, Game::DT));
    for (int k = 1, n = substeps(vel); k < n && vel.y < 0; k++) {  // a fast fall stops on the first ground the feet cross
        Vector3 p = Vector3Lerp(pos, np, (float)k / n);
        if (t.solid({p.x, p.y - 0.3f, p.z})) { np = p; break; }
    }
    float climb = 0;
    while (climb <= 0.6f && t.solid({np.x, np.y - 0.3f + climb, np.z})) climb += 0.05f;
    if (climb > 0.6f) { np.x = pos.x; np.z = pos.z; if (ground) vel.y = 7; }
    else np.y += climb;
    bool roof = t.solid({np.x, np.y + 0.3f, np.z});
    for (float y = np.y - 0.2f; y < np.y + 0.3f && !roof; y += 0.1f) roof = t.solid({np.x, y, np.z});  // the whole body: a thin overhang can't fall between feet and head
    if (roof) { np = pos; vel.y = fminf(vel.y, 0); }
    pos = np;
}

float Game::rand01() {
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / 16777216.0f;
}

float Game::gravity() const { return GRAVITY * ((cfg.rules & RULE_LOW_GRAVITY) ? 0.5f : 1.0f); }  // W4M Low.Gravity.OnValue 0.5 (TWEAK)

Vector3 Game::aimDir(const Worm &w) const {
    return {cosf(w.pitch) * sinf(w.yaw), sinf(w.pitch), cosf(w.pitch) * cosf(w.yaw)};
}

void Game::start(const GameConfig &c) {
    cfg = c;
    WEAPONS.resize(std::min(WEAPONS.size(), baseWeapons));
    WEAPONS.insert(WEAPONS.end(), c.custom.begin(), c.custom.end());
    for (size_t i = baseWeapons; i < WEAPONS.size(); i++)  // WEAPTWK kWeaponFactoryWeapon PostLaunchDelay 500, kWeaponFactoryHoming 0; not on the wire, so set here
        WEAPONS[i].postLaunch = WEAPONS[i].kind == Kind::Homing ? 0 : 500, WEAPONS[i].retreat = -1;
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
    prevButtons = 0, cursorOn = locked = false;
    water = Terrain::WATER;
    clock = hotSeat = jumpDelay = ropeShots = 0;
    vault = {};
    selfHurt = false;
    suddenDeath = false;
    lastHitTeam.assign(teams * perTeam, -1);
    fuses.assign(teams, 3);
    delays.assign(teams, std::vector<int>(WEAPONS.size()));
    for (const SchemePreset &p : SCHEMES)  // a W4M preset brings its SchemeData delays; custom schemes have none (WXD.DefaultSchemeData)
        if (!(cfg.rules & RULE_NO_DELAYS) && !memcmp(&p.s, &cfg.scheme, sizeof(Scheme)))
            for (const char *q = p.delays; *q;) {
                const char *e = strchr(q, '|'), *end = e ? e : q + strlen(q), *sp = end;
                while (sp > q && sp[-1] != ' ') sp--;
                std::string n(q, sp - 1);
                for (size_t i = 0; i < WEAPONS.size(); i++)
                    if (n == "*" ? i >= baseWeapons : WEAPONS[i].name == n) for (auto &d : delays) d[i] = atoi(sp);
                q = e ? e + 1 : end;
            }
    countGroup.clear(), countT = countEnd = 0, landHold = camHold = 0, abdRolled = crated = false;
    girderOn = false, girderWait = girders = 0, bubbles.clear(), icarus = flapAt = 0, drift = {}, doubleDamage = false;
    roped = jetting = jetUsed = chute = false, fuel = boost = 0, secondary = -1;
    spy.assign(teams, 0), scout = Scout{};

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
    for (size_t i = 0; i < bubbles.size();) if (--bubbles[i].life <= 0) emit(GameEvent::BubblePop, bubbles[i].pos), bubbles.erase(bubbles.begin() + i); else i++;  // GameLogic.Turn.Ended
    girderOn = false, icarus = 0, drift = {}, doubleDamage = false, scout.t = -1;  // DoPostActivity: SetData("DoubleDamage", 0)
    for (Worm &x : worms)
        if (x.alive && x.poison && x.hp > 1) { x.hp = std::max(1, x.hp - x.poison); emit(GameEvent::Hurt, x.pos, int(&x - worms.data())); }
    abdRolled = crated = false;
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
            hotSeat = msTicks(sc.hotSeat * 1000);
            jumpDelay = 0;
            selfHurt = false;
            power = 0;
            {  // W4M stdlib SelectRandomWind: Speed = Cap/10 x r² x MaxSpeed, Direction = r2 x 2pi; ponytail: x component only
                float r = rand01();
                wind = WIND_CAP[std::min<int>(sc.wind, 3)] * r * r * cosf(rand01() * 2 * PI);
            }
            roped = jetting = jetUsed = chute = cursorOn = blimp = locked = false, fuel = boost = 0, secondary = -1;
            for (Object &o : objects) o.hooked = false;
            shotsLeft = ropeShots = 0;
            weapon = picked[t];
            if (!usable(t, weapon)) firstWeapon(t);  // W4M 0x565770 keeps it while 0x50d900 says it is usable
            emit(GameEvent::TurnStart, worms[c].pos, c);
            return;
        }
    }
}

bool Game::toolOut() const {
    bool hook = false;
    for (const Object &o : objects) hook |= o.hooked;
    return roped || hook || jetting || (chute && current < (int)worms.size() && !worms[current].grounded);
}

bool Game::selectable(int team, int wi) const {
    if (!toolOut()) return usable(team, wi);
    return wi == weapon || (usable(team, wi) && toolDrop(WEAPONS[wi]));  // the tool in hand stays, its ammo spent
}

void Game::firstWeapon(int team) {
    for (int pass = 0; pass < 2; pass++)  // none: ours falls back to Skip Go / Surrender (W4M kWeaponUndefined)
        for (size_t k = 0; k < WEAPONS.size(); k++)
            if (usable(team, (int)k) && (pass || (WEAPONS[k].kind != Kind::SkipGo && WEAPONS[k].kind != Kind::Surrender))) {
                weapon = (int)k, secondary = -1, jetUsed = false;
                return;
            }
}

// W4M WeaponSelected 0x565d30: with a tool out (+0x8d, kept by a landed jetpack) a toolDrop() becomes the secondary (0x566310);
// anything else, or a toolDrop() under No Bombing (0x5662f6), ends the tool (0x565650) and is wielded
void Game::pick(int team, int k) {
    bool tool = toolOut(), keep = tool || jetLanded();
    if (icarus == 3) return;  // W4M Redbull: Weapon.DisableWeaponChange until the drink is done
    if (keep && k == weapon) { secondary = -1; return; }
    if (k < 0 || k >= (int)WEAPONS.size() || k == held() || !usable(team, k)) return;
    if (keep && !(cfg.wormpot & WP_NO_BOMBING) && toolDrop(WEAPONS[k])) { secondary = k; return; }
    if (tool) roped = jetting = chute = false;
    for (Object &o : objects) o.hooked &= !tool;
    weapon = k, secondary = -1, jetUsed = false;  // a new jetpack on the ground (new W4M entity)
}

// Y / R: the next selectable weapon (ours); under a tool only the tool and its toolDrop()s, so a step never ends it
void Game::nextWeapon(int team) {
    for (int i = 1; i <= (int)WEAPONS.size(); i++) {
        int k = (held() + i) % WEAPONS.size();
        if (selectable(team, k)) return pick(team, k);
    }
}

// Mine: W4M kWeaponLandmine Radius 3 units, the mesh drawn that far over the land (0x5761f0); the others: our meshes' half heights
static float halfHeight(Object::Type t) { return t == Object::Mine ? 0.15f : t == Object::Barrel || t == Object::Sentry ? 0.5f : 0.45f; }

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
            if (o.falling && o.type == Object::Crate) emit(GameEvent::CrateLand, o.pos, -1, o.weapon), landHold = phase == Phase::Aim ? POST_ACTIVITY : 0;  // W4M NoActivity -> PostActivityTime
            Vector3 n = terrain.normal({o.pos.x, o.pos.y - h, o.pos.z});
            if (n.y < SLIDE_NY) o.vel = {(o.vel.x + n.x * n.y * gravity() * DT) * SLIDE_FRICTION, 0, (o.vel.z + n.z * n.y * gravity() * DT) * SLIDE_FRICTION};  // too steep: W4M Sliding, as wormBody
            else o.vel = {o.vel.x * 0.8f, 0, o.vel.z * 0.8f};
            o.falling = false;
        } else {
            o.vel.y -= gravity() * DT;
            if (o.falling) o.vel.y = fmaxf(o.vel.y, -2.5f);
        }
        Vector3 face = Vector3Scale(Vector3Normalize({o.vel.x, 0, o.vel.z}), h);  // the leading side meets a wall, not the centre
        for (int k = 0, n = substeps(o.vel); k < n; k++) {
            Vector3 np = Vector3Add(o.pos, Vector3Scale(o.vel, DT / n));
            if (terrain.solid({np.x + face.x, o.pos.y, np.z + face.z})) { o.vel.x = o.vel.z = 0; np.x = o.pos.x; np.z = o.pos.z; }
            if (o.vel.y > 0 && terrain.solid({np.x, np.y + h, np.z})) { o.vel.y = 0; np.y = o.pos.y; }
            o.pos = np;
            for (int j = 0; j < 20 && terrain.solid({o.pos.x, o.pos.y - h, o.pos.z}); j++) o.pos.y += 0.05f;
        }

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
                if (!shielded(t, o.pos)) {  // 0x56c140: its bullets stop on a bubble
                    hurt(t, (int)sd.damage);
                    t.vel = Vector3Add(t.vel, Vector3Add(Vector3Scale(to, 4 / dist), {0, 2, 0}));
                }
            }
        }
        bool sheep = false;  // W4M challenges: a Super Sheep collects mission crates for its worm
        for (const Projectile &s : shots) sheep |= o.tag >= 0 && WEAPONS[s.weapon].kind == Kind::SuperSheep && Vector3Distance(s.pos, o.pos) < 1.2f;
        // touching the 0.9 m crate box, with some slack: beside it, on top or just under it
        auto touching = [&](const Worm &w) { return fabsf(w.pos.y - o.pos.y) < 1.3f && Vector2Distance({w.pos.x, w.pos.z}, {o.pos.x, o.pos.z}) < R + 0.75f; };
        if (o.type == Object::Crate && !o.dead)
            for (Worm &w : worms) {
                if (!(sheep && &w == &worms[current]) && (!w.alive || !touching(w))) continue;
                if (o.weapon < 0) w.hp += cfg.scheme.crateHealth, w.counted += cfg.scheme.crateHealth, w.poison = 0, w.abducted = false;  // label jumps on pickup (W4M); Worm.Antidote also clears 0x400 (0x5adecd)
                else if (WEAPONS[o.weapon].kind == Kind::DoubleDamage) doubleDamage = true;
                else if (WEAPONS[o.weapon].kind == Kind::CrateSpy) spy[w.team] = 1;  // never reset (0x5c8b20)
                else if (WEAPONS[o.weapon].kind == Kind::Armour) w.armour = true;  // Armour.Collected: the worm that took it (unverified)
                else if (ammo[w.team][o.weapon] >= 0) ammo[w.team][o.weapon]++;
                emit(GameEvent::Collect, o.pos, int(&w - worms.data()), o.weapon);
                gone = true;
                break;
            }
        if (!gone && !boom && !o.dead) { i++; continue; }
        Object x = o;
        objects.erase(objects.begin() + i);  // before explode(), which only flags the others
        if (boom && !gone) {
            explode(x.pos, x.type == Object::Barrel ? BARREL_BLAST : x.type == Object::Mine ? MINE_BLAST : CRATE_BLAST);
        }
    }
}

Vector3 Game::blimpEye(Vector3 focus, float yaw, float pitch) const {
    return Vector3Add(focus, {-sinf(yaw) * cosf(pitch) * BLIMP_STICK, sinf(pitch) * BLIMP_STICK, -cosf(yaw) * cosf(pitch) * BLIMP_STICK});
}

bool Game::blimpHit(Vector3 *hit) const {
    Vector3 e = blimpEye(cursor, cursorYaw, cursorPitch), d = Vector3Normalize(Vector3Subtract(cursor, e));
    float reach = 200, wet = d.y < -1e-4f ? (e.y - water) / -d.y : 1e9f;  // ray: Land.Radius + max(Land.Radius, distance to centre)
    if (terrain.raycast({e, d}, fminf(wet, reach), hit)) return true;
    *hit = Vector3Add(e, Vector3Scale(d, fminf(wet, reach)));
    return wet <= reach;
}

Vector3 Game::fatkinsDrop(const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) const {
    float g = gravity(), t = (sqrtf(wd.speed * wd.speed + 50 * g) - wd.speed) / g;  // the 25 m fall at speed, under gravity
    vel = {dir.x * BOMBER_SPEED, -wd.speed, dir.z * BOMBER_SPEED};
    return {tgt.x - dir.x * BOMBER_SPEED * t, tgt.y + 25, tgt.z - dir.z * BOMBER_SPEED * t};
}

Vector3 Game::strikeStart(const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) const {
    float h = landTop() + STRIKE_EXTRA, lead = BOMBER_SPEED * sqrtf(2 * fmaxf(h - tgt.y, 0) / (gravity() * wd.grav));
    float back = lead + (wd.clusters - 1) / 2.0f * STRIKE_GAP;  // our run is centred on the lead point
    vel = Vector3Scale(dir, BOMBER_SPEED);
    return {tgt.x - dir.x * back, h, tgt.z - dir.z * back};
}

// W4M active objects (0x4d3af0 callers): worms falling or sliding (0x5aa996, 0x5aaa04), a crate till it rests (0x5c9bd0), an armed
// or moving mine (payload 0x57ee73, 0x5778f0), a drum about to blow (0x5d1f86), a falling sentry (0x56cdc3), the gas jet's 1 s emitter
// (PARTTWK WeaponGasCanJet EmitterIsOfInterest, 0x5be6c6), the FlyCam hold (0x528457)
bool Game::active() const {
    for (const Worm &x : worms) if (x.alive && (!x.grounded || Vector3LengthSqr(x.vel) >= 0.01f)) return true;
    for (const Object &o : objects) {
        bool moving = Vector3LengthSqr(o.vel) >= 0.01f;
        if ((o.type == Object::Mine && (o.fuse >= 0 || moving)) || (o.type == Object::Crate && o.falling) || o.dead || (o.type == Object::Sentry && moving))
            return true;
    }
    for (const Gas &c : gas) if (c.life > GAS_LIFE - 1) return true;
    return camHold > 0 || landHold > 0;
}

// W4M LandscapeLogicEntity 0x4720c0: Land.Center = the middle of the land's bounding box (ours: floor 0 to landTop)
Vector3 Game::landCenter() const { return {Terrain::NX * Terrain::VOX / 2, landTop() / 2, Terrain::NZ * Terrain::VOX / 2}; }

float Game::landTop() const {
    float top = 0;
    for (int x = 0; x < Terrain::NX; x += 8)
        for (int z = 0; z < Terrain::NZ; z += 8)
            for (int y = Terrain::NY - 1; y * Terrain::VOX > top; y--)
                if (terrain.solid({x * Terrain::VOX, y * Terrain::VOX, z * Terrain::VOX})) { top = y * Terrain::VOX; break; }
    return top;
}

Vector3 Game::blimpFocus(Vector3 ref, float yaw) const {
    float y = fmaxf(ref.y, landTop()) + BLIMP_LIFT, d = (y - ref.y) / tanf(BLIMP_PITCH);
    return {ref.x - sinf(yaw) * d, y, ref.z - cosf(yaw) * d};
}

Vector3 Game::target() const {
    const Worm &w = worms[current];
    Vector3 hit, dir = aimDir(w);
    if (cursorOn && targeted(WEAPONS[weapon].kind)) return blimpHit(&hit), hit;
    if (terrain.raycast({Vector3Add(w.pos, dir), dir}, 60, &hit)) return hit;
    Vector3 far = {w.pos.x + sinf(w.yaw) * 30, (Terrain::NY - 1) * Terrain::VOX, w.pos.z + cosf(w.yaw) * 30};
    if (terrain.raycast({far, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit)) return hit;
    return {far.x, water, far.z};
}

void Game::use(Worm &w) {
    const WeaponDef &wd = WEAPONS[weapon];
    int &n = ammo[w.team][weapon];
    if (wd.kind == Kind::Rope && ropeShots >= ROPE_SHOTS) return;
    if (wd.kind == Kind::Girder && (!girderOn || girderFits(girder) || girders >= GIRDER_MAX)) return;  // W4M 0x55a840: weapons/Gong
    if (wd.kind == Kind::Binoculars) {  // W4M: no DecrementInventory, no end of turn; FIRE needs a target in sight
        Vector3 d = aimDir(w), hit;
        float far = terrain.raycast({w.pos, d}, 200, &hit) ? Vector3Distance(w.pos, hit) : 200;
        for (const Worm &o : worms) {  // ray radius 10 units; the shooter's alliance is skipped
            float t = Vector3DotProduct(Vector3Subtract(o.pos, w.pos), d);
            if (o.alive && o.team != w.team && t > 0 && t < far && Vector3Distance(o.pos, Vector3Add(w.pos, Vector3Scale(d, t))) < R + 0.5f) far = t;
        }
        if (far >= 200) return;
        scout = {Vector3Add(w.pos, Vector3Scale(d, far)), 0, 0, 0, false};
        scout.ok = scoutSolve(scout.at, scout.power, scout.pitch);
        emit(GameEvent::Fire, w.pos, current, weapon);
        return;
    }
    if (wd.kind == Kind::Jetpack) {  // W4M FireUtilPressed 0x562270: takes off with fuel > 20 ms, the first take-off spends the ammo
        if (!jetUsed) jetUsed = true, fuel = wd.fuse, n -= n > 0;
        if (fuel > JET_DRY) jetting = true, thrust = wd.speed, boost = 0, emit(GameEvent::Fire, w.pos, current, weapon);
        return;
    }
    if (n > 0 && !shotsLeft && !(wd.kind == Kind::Rope && ropeShots)) n--;  // one rope = ROPE_SHOTS launches
    Vector3 dir = aimDir(w), f = {sinf(w.yaw), 0, cosf(w.yaw)}, tgt = target();
    // W4M 0x585a52 / 0x585bc5: off its feet and moving, a payload starts 30 units further along the worm's velocity and inherits it
    const bool carried = (!w.grounded && Vector3LengthSqr(w.vel) > 0) || (vault.t && Vector3LengthSqr(vault.vel) > 0);  // a vault is state 4, off its feet
    const Vector3 carry = carried ? (vault.t ? vault.vel : w.vel) : Vector3{},
                  from = muzzle(terrain, w.pos, Vector3Add(launchPoint(wd, w.pos, w.yaw), carried ? Vector3Scale(Vector3Normalize(carry), 1.5f) : Vector3{}));
    emit(GameEvent::Fire, w.pos, current, weapon);
    switch (wd.kind) {
    case Kind::Shell:
        shots.push_back({from, Vector3Add(carry, dropped(wd) ? Vector3Scale(f, wd.speed) : Vector3Scale(dir, launchSpeed(wd, power))),
                         weapon, fuseOf(wd), false, 1});  // dropped: W4M BasePower along the facing, not aimed
        phase = Phase::Flying;
        break;
    case Kind::Sheep:
        shots.push_back({from, Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Airstrike:
        if (cursorOn && targeted(wd.kind)) f = strikeDir();  // W4M 0x54d931: cross(Airstrike.Direction, UpVector), flat
        if (wd.fuse > 0) shots.push_back({Vector3Add(tgt, {-f.x * BOMBER_LEAD, BOMBER_HEIGHT, -f.z * BOMBER_LEAD}), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1, {}, STRIKE_LEAD, 0});
        else {  // W4M Bomber: a run along the worm's facing, one bomb every STRIKE_TICKS
            Vector3 v, p = strikeStart(wd, tgt, f, v);
            shots.push_back({p, v, weapon, 0, false, 1, {}, STRIKE_LEAD, 0});
        }
        phase = Phase::Flying;
        break;
    case Kind::Donkey: {
        Vector3 v = {0, -wd.speed, 0}, p = wd.name == "Fatkins Strike" ? fatkinsDrop(wd, tgt, cursorOn ? strikeDir() : f, v) : Vector3Add(tgt, {0, 25, 0});
        shots.push_back({p, v, weapon, 0, false, wd.clusters > 0 ? wd.clusters : 1 << 30, {0, p.y, 0}, wd.name == "Fatkins Strike" ? STRIKE_LEAD : 0});  // 0: smashes until LifeTime or the water
    }
        phase = Phase::Flying;
        break;
    case Kind::Shotgun: {
        if (!shotsLeft) shotsLeft = wd.shots;
        Ray r = {muzzle(terrain, w.pos, launchPoint(wd, w.pos, w.yaw)), dir};  // W4M 0x55e10f: the eye; the shooter is skipped by id (0x5b27e0 -> 0x519dd0)
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
        for (const Bubble &bb : bubbles) {  // unverified for hitscan guns: stopped by the shell like payloads
            Vector3 c = Vector3Subtract(Vector3Add(bb.pos, {0, BUBBLE_UP, 0}), r.position);
            float t = Vector3DotProduct(c, dir), h = Vector3LengthSqr(c) - t * t;
            if (Vector3Length(c) > BUBBLE_SHELL && h < BUBBLE_SHELL * BUBBLE_SHELL && t > 0) {
                float hit = t - sqrtf(BUBBLE_SHELL * BUBBLE_SHELL - h);
                if (hit < dist) dist = hit, struck = nullptr;
            }
        }
        // a worm hit takes the full damage (W4M gun), the blast only digs and pushes
        Blast b = blastOf(wd, false);
        if (struck) b.damage = 0;
        if (dist < 60) explode(Vector3Add(r.position, Vector3Scale(dir, dist)), b);
        if (struck) hurt(*struck, (int)wd.damage);  // W4M Damage.Impulse 0x5ae320 ignores armour
        if (--shotsLeft == 0) phase = Phase::Flying;
        break;
    }
    case Kind::Rope: {
        ropeShots++;
        Vector3 hit;
        float reach = terrain.raycast({w.pos, dir}, wd.speed, &hit) ? Vector3Distance(w.pos, hit) : wd.speed;
        Object *grab = nullptr;  // W4M: the hook catches crates, mines and drums, reeled in and out like the rope
        for (Object &o : objects) {
            float t = Vector3DotProduct(Vector3Subtract(o.pos, w.pos), dir);
            bool loose = o.type == Object::Mine || o.type == Object::Barrel || (o.type == Object::Crate && o.tag < 0);
            if (loose && t > 0 && t < reach && Vector3Distance(o.pos, Vector3Add(w.pos, Vector3Scale(dir, t))) < 0.6f) reach = t, grab = &o;
        }
        if (grab) grab->hooked = true, grab->falling = false, ropeLen = reach, ropeMax = wd.speed;
        else if (reach < wd.speed) { roped = true; anchor = hit; ropeLen = reach; ropeMax = wd.speed; w.grounded = false; }
        break;
    }
    case Kind::Jetpack: break;  // above
    case Kind::Teleport: w.pos = Vector3Add(tgt, {0, R + 0.3f, 0}); w.vel = {0, 0, 0}; break;
    case Kind::SuperSheep:
        if (wd.walks) {
            shots.push_back({from, Vector3Scale(f, SHEEP_STEP), weapon, SHEEP_WALK, false, 1});
            phase = Phase::Flying;
            break;
        }
        [[fallthrough]];
    case Kind::Homing:
        shots.push_back({from, Vector3Add(carry, Vector3Scale(dir, wd.kind == Kind::Homing ? launchSpeed(wd, power) : wd.speed)), weapon, wd.fuse, false, 1,
                         wd.kind == Kind::Homing && locked ? lockAt : tgt});
        phase = Phase::Flying;
        break;
    case Kind::OldWoman:
        shots.push_back({from, Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Scouser:
        shots.push_back({from, Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1});
        phase = Phase::Flying;
        break;
    case Kind::Melee:
        for (Worm &o : worms) {
            if (!o.alive || &o == &w || !meleeHits(w, o.pos, wd)) continue;
            hurt(o, (int)wd.damage);
            if (wd.pins) o.nailed = true, o.vel = {0, 0, 0}, o.pos.y -= 0.35f;  // sunk to the waist
            else o.vel = Vector3Scale(Vector3Add(Vector3Scale(dir, wd.speed), {0, wd.bounce, 0}), doubled() ? 2 : 1);  // DamageImpulseMessage 0x518cbf
        }
        if (wd.fuse > 0) { w.vel.y = wd.fuse; w.grounded = false; }
        phase = Phase::Flying;
        break;
    case Kind::Mine:
        objects.push_back({Object::Mine, from, carry, -1, -1, false, false, -1, -1, false, MINE_COURTESY});
        phase = Phase::Flying;
        break;
    case Kind::Sentry:
        objects.push_back({Object::Sentry, muzzle(terrain, w.pos, Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 1.3f), {0, 0.3f, 0}))), {0, 0, 0}, weapon, -1, false, false, w.team});
        phase = Phase::Flying;
        break;
    case Kind::Abduction: {  // W4M 0x548aa0: AbductStart, or AbductFail with nobody in reach; stepUfo runs the rest
        Projectile u = {{tgt.x, landTop() + ABD_RISE, tgt.z}, {0, 0, 0}, weapon, ABD_ARRIVE, false, 1, tgt};
        abductees.clear(), abdCam = -1;
        auto sees = [&](const Worm &o) {  // 0x466a20: saucer -> worm foot against the worm colliders (flag 1), any hit counts, the target's own included
            Vector3 d = Vector3Subtract({o.pos.x, o.pos.y - R, o.pos.z}, u.pos);
            float l2 = Vector3LengthSqr(d);
            for (const Worm &c : worms) {
                float t = l2 > 0 ? Clamp(Vector3DotProduct(Vector3Subtract(c.pos, u.pos), d) / l2, 0, 1) : 0;
                if (c.alive && Vector3Distance(c.pos, Vector3Add(u.pos, Vector3Scale(d, t))) <= R + 0.01f) return true;
            }
            return false;
        };
        for (int i = 0; i < (int)worms.size(); i++) {  // 0x5488e0: xz within AreaOfEffect, then the sight ray; nearest the saucer first
            const Worm &o = worms[i];
            if (!o.alive || Vector2Distance({o.pos.x, o.pos.z}, {u.pos.x, u.pos.z}) >= wd.radius || o.nailed || shielded(o, {1e4f, 1e4f, 1e4f}) || !sees(o)) continue;  // flags 0x20 nailed, 0x8 in a bubble
            auto at = abductees.begin();
            while (at != abductees.end() && Vector3Distance(worms[at->worm].pos, u.pos) <= Vector3Distance(o.pos, u.pos)) at++;
            abductees.insert(at, {i, 0, o.pos});
        }
        if (abductees.empty()) u.stage = ABD_FAILING, u.fuse = ABD_FAIL;
        shots.push_back(u);
        phase = Phase::Flying;
        break;
    }
    case Kind::Flood: water = fminf(water + wd.speed, Terrain::WATER + 15); phase = Phase::Flying; break;
    case Kind::Parachute: chute = true; break;
    case Kind::Armour: w.armour = true; break;
    case Kind::Girder:  // Land.SpawnPiece GirderSmall.xom: a 4 x 4 m deck 1 m thick on two 1 m legs, axis-aligned
        terrain.weld(Vector3Add(girder, {0, 0.5f, 0}), {2, 0.5f, 2});  // legs along x at the z ends: BitArray3D word 3 + x + 4 z (0x43dfd0)
        for (float sz : {-1.5f, 1.5f}) terrain.weld(Vector3Add(girder, {0, -0.5f, sz}), {2, 0.5f, 0.5f});
        girders++;
        if (!(cfg.wormpot & WP_MULTI_GIRDER)) phase = Phase::Flying;  // 0x55ac30: Timer.StartRetreatTimer unless GirdersDontEndTurn
        break;
    case Kind::Bubble: {  // 0x550190: spawned beside the worm, then falls freely
        Vector3 side = {cosf(w.yaw), 0, -sinf(w.yaw)};
        bubbles.push_back({Vector3Add(w.pos, Vector3Add(Vector3Add(Vector3Scale(f, 0.623f), Vector3Scale(side, -0.097f)), {0, 0.47f, 0})), {0, 0, 0}, 6, w.yaw});
        emit(GameEvent::BubbleNew, bubbles.back().pos);
        phase = Phase::Flying;  // unverified: assumed the normal fired-weapon retreat (PostLaunchDelay 0)
        break;
    }
    case Kind::Icarus:  // 0x587600: DisableMovementRef, DisableWeaponChange, EndFireWeapon -> Weapon.PostLaunchDelay (500 ms)
        icarus = 3, flapAt = clock + msTicks(WEAPONS[weapon].postLaunch);
        break;
    case Kind::Binoculars: case Kind::DoubleDamage: case Kind::CrateSpy: break;
    case Kind::Surrender:
        for (Worm &o : worms) if (o.team == w.team) o.hp = 0;
        phase = Phase::Settle, timer = SETTLE_WAIT;
        break;
    case Kind::SkipGo: phase = Phase::Flying; break;  // W4M 0x587ed0: EndTurn, PostLaunchDelay, then a retreat of 0 (0x588160)
    case Kind::ChangeWorm:
        for (int k = 1, base = w.team * perTeam; k < perTeam; k++) {
            int c = base + (current - base + k) % perTeam;
            if (worms[c].alive) { current = c; break; }
        }
        break;
    }
    power = 0;
}

// W4M BaseWeapon / PayloadWeapon CanFire (0x54a2d0, 0x583ca0): only an Ambulatory worm (state 0) fires, unless the weapon
// CanBeFiredWhenWormMoving (WEAPTWK: Dynamite, Fire Punch, Landmine, Sheep). Guns (0x55cd30) and utilities have no such test
bool Game::fireable(const Worm &w) const {
    const WeaponDef &d = WEAPONS[weapon];
    if (d.kind == Kind::Shotgun || utility(d.kind)) return true;
    bool moving = d.name == "Dynamite" || d.name == "Fire Punch" || d.kind == Kind::Mine || d.kind == Kind::Sheep;
    return moving || (w.grounded && !w.motion.slide && !vault.t && !jumpDelay);
}

void Game::stepWorm(Worm &w) {
    if (!w.alive || aboard(int(&w - worms.data()))) return;
    if (w.abducted) zapStep(w);
    if (w.nailed) { w.vel = {0, 0, 0}, w.grounded = true; if (w.pos.y < water) drown(w); return; }
    if (vault.t && &w == &worms[current]) return;  // W4M: no Ballistic while Vaulting
    auto land = [&](float speed) {  // fall damage
        bool maxFall = cfg.wormpot & WP_MAX_FALL;
        float safe = maxFall ? FALL_SAFE * 0.7f : FALL_SAFE;
        bool winged = (icarus == 2 || jetting) && &w == &worms[current];  // W4M flag 0x40 each frame of the flight: no fall damage
        if (speed > safe && (cfg.scheme.fallDamage || maxFall) && !(cfg.wormpot & WP_WORMS_DROWN) && !winged) {
            int dmg = (int)((speed - safe) * FALL_SCALE * (maxFall ? 3 : 1)) + 1, wi = int(&w - worms.data());
            if (dmg > 0) { w.hp -= dmg; selfHurt |= wi == current; emit(GameEvent::Hurt, w.pos, wi); }
        }
    };
    bool jet = jetting && &w == &worms[current];  // W4M jetpack contact 0x5633e9: walls and ceilings bounce at 0.8; it lands itself, no Ballistic
    w.motion.input = &w == &worms[current] ? steerIn : Vector3{};
    int stuck = w.motion.stuck;  // the jetpack's own flight is not Ballistic: it keeps no stuck count
    land(wormBody(terrain, w.pos, w.vel, w.grounded, w.motion, w.yaw, gravity(), cfg.wormpot, jet ? JET_BOUNCE : 0.3f));
    if (jet) w.motion.stuck = stuck;
    if (w.pos.y < water) drown(w);
}

void Game::drown(Worm &w) {
    w.alive = false;
    w.hp = 0;
    emit(GameEvent::Splash, w.pos, int(&w - worms.data()));  // Death once Settle has counted it to 0
}

bool Game::steered() const {
    for (const Projectile &s : shots) {
        const WeaponDef &d = WEAPONS[s.weapon];
        if (!s.child && (d.kind == Kind::OldWoman || d.kind == Kind::SuperSheep || (d.kind == Kind::Scouser && !s.stage) || (d.kind == Kind::Airstrike && d.fuse > 0))) return true;
    }
    return false;
}

// W4M: Worm.WeaponDisableMovement from the fire to PostLaunchDelay's end; FlyCam (homing, super sheep) disables WormMoving
bool Game::retreating() const {
    if ((phase != Phase::Flying && phase != Phase::Retreat) || timer > retreatTicks(WEAPONS[weapon])) return false;
    for (const Projectile &s : shots) if (!s.child && WEAPONS[s.weapon].kind == Kind::Homing) return false;
    return !steered();  // ours: the stick steers the shot, not the worm
}

Object *Game::hooked() {
    for (Object &o : objects) if (o.hooked) return &o;
    return nullptr;
}

// Pendulum: integrate freely, then project back onto the rope sphere and drop outward velocity; sub-stepped like a flight.
void Game::stepRope(Worm &w) {
    w.motion.slide = false;
    w.vel.y -= gravity() * DT;
    auto blocked = [&](Vector3 p) { return terrain.solid({p.x, p.y - R, p.z}) || terrain.solid({p.x, p.y + R, p.z}); };
    for (int k = 0, n = substeps(w.vel); k < n; k++) {
        Vector3 np = Vector3Add(w.pos, Vector3Scale(w.vel, DT / n)), d = Vector3Subtract(np, anchor);
        float l = Vector3Length(d);
        if (l > ropeLen) {
            Vector3 u = Vector3Scale(d, 1 / l);
            np = Vector3Add(anchor, Vector3Scale(u, ropeLen));
            float out = Vector3DotProduct(w.vel, u);
            if (out > 0) w.vel = Vector3Subtract(w.vel, Vector3Scale(u, out));
        }
        if (blocked(np)) { np.y = w.pos.y; w.vel.y = 0; }  // slide along the ground
        if (blocked(np)) { w.vel = Vector3Scale(w.vel, -0.3f); break; }
        w.pos = np;
    }
    if (w.pos.y < water) drown(w);
}

void Game::hurt(Worm &w, int dmg, bool blast) {
    if (doubled()) dmg *= 2;  // W4M 0x518da5 doubles the message first,
    if (blast && w.armour) dmg = dmg * ARMOUR / 100;  // then the worm's Shield.DamageScale (explosions only)
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

// W4M StealInventory 0x592d30: 5 single units, each from a random weapon the victim has, those the thief lacks first.
void Game::steal(const Worm &v) {
    int thief = worms[current].team, first = -1;
    std::vector<int> &from = ammo[v.team], &to = ammo[thief];
    for (int round = 0; round < 5; round++) {
        std::vector<int> a, b;
        for (int k = 0; k < (int)from.size(); k++) if (from[k]) (to[k] ? b : a).push_back(k);
        std::vector<int> &pool = a.empty() ? b : a;
        if (pool.empty()) break;
        int k = pool[(int)(rand01() * pool.size())];
        if (from[k] > 0) from[k]--;
        if (to[k] >= 0) to[k]++;
        if (first < 0) first = k;
    }
    if (first >= 0) emit(GameEvent::Collect, v.pos, current, first);
}

Blast blastOf(const WeaponDef &d, bool child) {
    const float *f = child ? d.cblast : d.blast, r = child ? d.cradius : d.radius;
    float pr = f[2] >= 0 ? f[2] : 1.8f * r;  // derived: W4M bazooka-like ratios
    return {r, f[0] >= 0 ? f[0] : 1.4f * r, child ? d.cdamage : d.damage, f[1] >= 0 ? f[1] : 14.5f, pr, f[3] >= 0 ? f[3] : 0.4f * pr};
}

// full damage out to 1/6 of the reach, then linear; distance from the worm's skin; at least 1 hp
int Game::blastDamage(const Blast &b, Vector3 p, Vector3 w) {
    float d = fmaxf(0, Vector3Distance(w, p) - R);
    return d < b.reach && b.damage > 0 ? std::max(1, (int)(b.damage * fminf(1, 1.2f * (b.reach - d) / b.reach))) : 0;
}

// away from a point pushDepth under the blast, so worms beside it fly up; 1.2x the magnitude at that point
Vector3 Game::blastKick(const Blast &b, Vector3 p, Vector3 w) {
    Vector3 to = Vector3Subtract(w, {p.x, p.y - b.pushDepth, p.z});
    float d = Vector3Length(to);
    return d < b.pushReach && d > 1e-4f ? Vector3Scale(to, b.push * 1.2f * (b.pushReach - d) / b.pushReach / d) : Vector3{0, 0, 0};
}

void Game::explode(Vector3 p, const Blast &b0, float poison) {
    Blast b = b0;  // W4M ExplosionMessage 0x518d80: DoubleDamage doubles the radii and the impulse too (hurt() doubles the damage)
    if (doubled()) b.crater *= 2, b.reach *= 2, b.push *= 2, b.pushReach *= 2;
    for (size_t i = 0; i < bubbles.size();) {  // 0x54eff0: only a blast inside pops it (Health 1)
        float d = Vector3Distance(p, bubbles[i].pos);
        bool in = Vector3Distance(p, Vector3Add(bubbles[i].pos, {0, BUBBLE_UP, 0})) < BUBBLE_R && d < b.pushReach + 0.5f && d < b.crater;
        if (in && b.damage * (b.crater - d) / b.crater >= 1) { emit(GameEvent::BubblePop, bubbles[i].pos), bubbles.erase(bubbles.begin() + i); continue; }
        if (Vector3Distance(p, Vector3Add(bubbles[i].pos, {0, BUBBLE_UP, 0})) < b.pushReach + BUBBLE_SHELL) bubbleHit(bubbles[i]);  // Damage.Impulse 0x54fc41
        i++;
    }
    if (b.crater > 0) terrain.carve(p, b.crater);
    emit(b.crater >= 5 ? GameEvent::BigBoom : GameEvent::Boom, p);
    for (Worm &w : worms) {
        // a worm queued to blow up stays put: the death queue's schedule hangs on it
        if (!w.alive || (w.hp <= 0 && !countGroup.empty()) || shielded(w, p)) continue;
        int dmg = blastDamage(b, p, w.pos);
        Vector3 kick = blastKick(b, p, w.pos);
        if (!dmg && poison <= 0 && Vector3LengthSqr(kick) == 0) continue;
        if (w.nailed && Vector3Distance(w.pos, p) < b.crater + R) w.nailed = false;  // the ground around it is gone
        if (dmg) hurt(w, dmg, true);
        if (poison > 0 && Vector3Distance(w.pos, p) < b.reach + R && !(cfg.wormpot & WP_WORMS_DROWN)) w.poison = std::max(w.poison, (int)poison), w.abducted = false;  // Worm.Poison clears 0x400 (0x5addd1)
        if (w.nailed) continue;
        w.vel = Vector3Add(w.vel, Vector3Scale(kick, (cfg.wormpot & WP_STICKY ? 0.6f : 1) * (w.armour ? 0.5f : 1)));  // W4M shield: half
        w.motion.air = w.motion.slide = false;  // ImpulseWorm 0x5ad010: air control off, then Ballistic or Sliding
    }
    for (Object &o : objects) {
        if (o.type == Object::Mine) {  // W4M payload 0x57f4d0: only pushed (no 1.2x), at least 0.3 of it upward; never set off
            Vector3 k = Vector3Scale(blastKick(b, p, o.pos), 1 / 1.2f);
            if (Vector3LengthSqr(k) == 0) continue;
            k.y = fmaxf(k.y, 0.3f * Vector3Length(k));
            o.vel = Vector3Add(o.vel, k), o.falling = false;
        } else if (Vector3Distance(o.pos, p) < b.reach && !(o.tag >= 0 && o.type == Object::Crate)) o.dead = true;
    }
}

// W4M AlienAbductionLogicEntity::Update 0x548710; s.fuse: s left in a timed state, s spent lifting
bool Game::stepUfo(Projectile &s) {
    const WeaponDef &wd = WEAPONS[s.weapon];
    switch (s.stage) {
    case ABD_ARRIVING:
        if ((s.fuse -= DT) >= DT / 2) break;
        abductees.erase(std::remove_if(abductees.begin(), abductees.end(), [&](const Abductee &a) { return !worms[a.worm].alive || worms[a.worm].hp <= 0; }), abductees.end());
        if (abductees.empty()) s.stage = ABD_LEAVING, s.fuse = ABD_LEAVE;  // 0x548500: nobody left, AbductEnd
        else s.stage = ABD_LIFTING, s.fuse = 0, abductees[0].st = 1, abdCam = abductees[0].worm;
        break;
    case ABD_LIFTING: {  // 0x547760: straight at the saucer's origin, no collision; 0x548b82: speed = NormalSpeed x height / AverageHeight, capped
        s.fuse += DT;
        float v = fminf(fmaxf((s.pos.y - Terrain::WATER) / ABD_AVG * wd.speed, wd.speed), ABD_MAX) * DT;
        int last = -1, wait = -1;
        bool done = true;
        for (int k = 0; k < (int)abductees.size(); k++) {
            Abductee &a = abductees[k];
            if (a.st == 0) { done = false, wait = wait < 0 ? k : wait; continue; }
            last = k;
            if (a.st == 2) continue;
            Worm &o = worms[a.worm];
            Vector3 d = Vector3Subtract(s.pos, o.pos);
            float l = Vector3Length(d);
            o.vel = {0, 0, 0}, o.grounded = false, done = false;
            if (l < ABD_IN) a.st = 2, o.pos = Vector3Add(s.pos, {0, a.worm * 0.05f, 0});  // aboard and hidden, id units above the origin
            else o.pos = Vector3Add(o.pos, Vector3Scale(d, v / l));
        }
        if (done) s.stage = ABD_HOLDING, s.fuse = ABD_HOLD;  // 0x547200
        else if (wait >= 0 && worms[abductees[wait].worm].pos.y + ABD_GAP < worms[abductees[last].worm].pos.y) abductees[wait].st = 1;  // 0x548440
        break;
    }
    case ABD_HOLDING:
        if ((s.fuse -= DT) < DT / 2) s.stage = ABD_SPITTING, s.fuse = 0;  // 0x5471e0: the first one goes the next tick
        break;
    case ABD_SPITTING: {  // 0x547ac0 SpitOutWorm, one per 2100 ms in lift order
        if ((s.fuse -= DT) >= DT / 2) break;
        if (abductees.empty()) { s.stage = ABD_LEAVING, s.fuse = ABD_LEAVE; break; }
        Abductee a = abductees.front();
        abductees.erase(abductees.begin());
        Worm &o = worms[a.worm];
        Vector3 d = Vector3Subtract(a.from, s.pos);  // towards where it was taken, +-40 units in x and z
        d.x += rand01() * 4 - 2, d.z += rand01() * 4 - 2;
        o.pos = s.pos, o.vel = Vector3Scale(Vector3Normalize(d), ABD_OUT), o.grounded = false;
        o.poison = 0, o.abducted = true, o.zap = ZAP_FIRST, o.calm = -1;  // 0x547360: flags 0x440, poison cleared, then half its health
        hurt(o, o.hp / 2);  // a DamageImpulseMessage: DoubleDamage doubles it, armour does not apply
        emit(GameEvent::Abducted, o.pos, a.worm);
        s.fuse = ABD_SPIT;
        break;
    }
    default:  // AbductEnd, AbductFail
        if ((s.fuse -= DT) < DT / 2) return false;
    }
    return true;
}

// W4M HomingPayloadLogicEntity 0x5611b0 (Factory HomingAvoidLand: Vertical / ForwardLandAvoidanceDistance 100 units, forces 0.009, MaxHomingSpeed 0.25)
// Within 0.25 m of the target it stops for good (+0x178); under 5 m from it nothing; else up / down / forward land probes (0x57dca0) steer it.
void Game::avoidLand(Projectile &s) {
    const float PROBE = 5, FORCE = 9, MAXV = 12.5f;  // 100 units; 0.009 units/ms^2 x 20 ms x 50 m/s per unit/ms
    float d = Vector3Distance(s.aim, s.pos);
    if (d < 0.25f) s.stage = 1;
    if (s.stage || d < PROBE) return;
    Vector3 hit;
    bool up = terrain.raycast({s.pos, {0, 1, 0}}, PROBE, &hit), low = terrain.raycast({s.pos, {0, -1, 0}}, PROBE, &hit) || s.pos.y < water + 0.25f;
    float l = Vector3Length(s.vel);
    if (l > 0 && terrain.raycast({s.pos, Vector3Scale(s.vel, 1 / l)}, PROBE, &hit)) {
        Vector3 w = Vector3Normalize(Vector3Subtract(hit, s.pos));  // nothing above: up; ceiling and room below: down; ceiling and floor: level
        s.vel = Vector3Add(s.vel, {-w.x * FORCE, (up ? (low ? 0 : -FORCE) : FORCE) - w.y * FORCE, -w.z * FORCE});
    }
    l = Vector3Length(s.vel);
    if (l > MAXV) s.vel = Vector3Scale(s.vel, MAXV / l);
}

// W4M UpdateAbductee 0x5a9c40: when a Zap is due and the abductee is moving, it jumps to a free spot nearby; the next comes 1-4 s on
void Game::zapStep(Worm &w) {
    if (--w.zap > 0) return;
    w.zap = ZAP_MIN + (int)(rand01() * ZAP_SPAN);
    if (Vector3LengthSqr(w.vel) == 0) return;
    for (int k = 0; k < 16; k++) {  // ponytail: W4M probes a spot per tick while it waits (then a physics step); ours 16 at once, dropped onto the land
        Vector3 p = Vector3Add(w.pos, {(rand01() - 0.5f) * 2 * ZAP_XZ, rand01() * ZAP_Y, (rand01() - 0.5f) * 2 * ZAP_XZ}), hit;
        if (body(terrain, p) > 0 || !terrain.raycast({p, {0, -1, 0}}, 2 * ZAP_Y, &hit) || hit.y < water) continue;
        int wi = int(&w - worms.data());
        emit(GameEvent::Poof, w.pos, wi);  // WXP_Poof_VLarge where it was, WXP_Abductee_Teleport where it lands
        w.pos = Vector3Add(hit, {0, R + 0.3f, 0}), w.vel = {0, 0, 0}, w.grounded = false;
        emit(GameEvent::Zap, w.pos, wi);
        return;
    }
}

void Game::stepShots(const Input &in, bool detonate) {
    const float W = Terrain::NX * Terrain::VOX;
    std::vector<Projectile> spawned;
    auto shell = [&](Vector3 a, Vector3 b, Vector3 *n) {  // a bubble's shell crossed inward: payload 0x581dc0 reports new contacts only
        for (Bubble &bb : bubbles) {
            Vector3 c = Vector3Add(bb.pos, {0, BUBBLE_UP, 0});
            if (Vector3Distance(a, c) >= BUBBLE_SHELL && Vector3Distance(b, c) < BUBBLE_SHELL)
                return bubbleHit(bb), *n = Vector3Normalize(Vector3Subtract(b, c)), true;
        }
        return false;
    };
    auto touches = [&](Vector3 p) {  // contact bits: worm index, 63 = a target
        uint64_t m = 0;
        for (size_t k = 0; k < worms.size() && k < 63; k++) if (worms[k].alive && Vector3Distance(p, worms[k].pos) < R + 0.3f) m |= 1ull << k;
        for (const Object &o : objects) if (o.type == Object::Target && Vector3Distance(p, o.pos) < 0.7f) m |= 1ull << 63;
        return m;
    };
    for (size_t i = 0; i < shots.size();) {
        Projectile &s = shots[i];
        const WeaponDef &wd = WEAPONS[s.weapon];
        bool boom = false, timed = wd.fuse > 0 && !s.child;
        Vector3 np;
        bool bomber = wd.kind == Kind::Airstrike && !s.child;  // the plane; fuse > 0: the steered Bovine Blitz
        if (wd.kind == Kind::Abduction) {
            if (stepUfo(s)) i++;
            else shots.erase(shots.begin() + i);
            continue;
        }
        if (wd.kind == Kind::Donkey && wd.clusters > 0 && s.stage > 0) { s.stage--, i++; continue; }  // Fatkins: in the bomber until its DropBomb
        bool walker = !s.child && (wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman || (wd.kind == Kind::Scouser && !s.stage) ||
                                   (wd.kind == Kind::SuperSheep && wd.walks && !s.stage));
        if (walker) {
            // walks in its launch direction, climbs small steps, hops at walls; W4M: the old woman and the scouser are steered
            if (wd.kind == Kind::OldWoman || wd.kind == Kind::Scouser) {
                float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 2 * DT;
                s.vel.x = sinf(yaw) * wd.speed, s.vel.z = cosf(yaw) * wd.speed;
            }
            Vector3 was = s.pos, sn;
            walkerStep(terrain, s.pos, s.vel, gravity());
            if (shell(was, s.pos, &sn)) s.pos = was, s.vel = {-s.vel.x, 0, -s.vel.z};  // 0x593476: walkers turn back
            np = s.pos;
            boom = detonate && (wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman);
            if (detonate && wd.kind == Kind::SuperSheep) {  // takes off: the flight gets its own lifetime
                s.stage = 1, s.fuse = wd.fuse;
                s.vel = Vector3Scale(Vector3Normalize({s.vel.x, Vector2Length({s.vel.x, s.vel.z}) * tanf(SHEEP_TAKEOFF), s.vel.z}), wd.speed);
                if (phase == Phase::Settle) timer = std::max(timer, SETTLE_WAIT + (int)(wd.fuse * 60) + 60);
            }
            for (Worm &v : worms) {
                int vi = int(&v - worms.data());
                if (!v.alive || vi == current || vi == s.prey || Vector3Distance(np, v.pos) > R + 0.4f) continue;
                if (wd.kind == Kind::Scouser) {  // swallows it and floats away
                    s.stage = 1, s.prey = vi, s.fuse = SCOUSER_FLOAT, v.nailed = false;
                    break;
                }
                if (wd.kind == Kind::OldWoman) steal(v), s.prey = vi;  // W4M robs any worm but the thrower
            }
        } else if (wd.kind == Kind::SuperSheep && !s.child) {
            // steered by the stick at constant speed, no gravity
            float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 2 * DT;
            float pitch = Clamp(asinf(Clamp(s.vel.y / fmaxf(Vector3Length(s.vel), 0.01f), -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
            s.vel = Vector3Scale({cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}, wd.speed);
            boom = detonate;
            uint64_t now = 0;
            for (int k = 0, n = substeps(s.vel); k < n && !boom; k++) {
                Vector3 sn, was = s.pos;
                np = s.pos = Vector3Add(s.pos, Vector3Scale(s.vel, DT / n));
                uint64_t m = touches(np);
                now |= m;
                boom = terrain.solid(np) || (m & ~s.touching) || shell(was, np, &sn);
            }
            s.touching = now;
            np = s.pos;
        } else if (bomber && wd.fuse <= 0) {  // air strike plane: s.prey bombs dropped, s.stage ticks to the next
            bool lead = !s.prey && s.stage > 0;  // held at the first drop point while bombrun_start plays
            if (s.stage > 0) s.stage--;
            else spawned.push_back({s.pos, s.vel, s.weapon, 0, true, 1}), s.prey++, s.stage = STRIKE_TICKS - 1, emit(GameEvent::Launch, s.pos, -1, s.weapon);
            np = s.pos = lead ? s.pos : Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            boom = s.prey >= wd.clusters;
        } else if (bomber) {  // banks on the stick; s.prey: payloads dropped, s.stage: ticks to the next drop
            bool lead = !s.prey && s.stage > 0;  // W4M SuperBomber: the run starts once bombrun_start has played (0x58b4e0)
            float yaw = atan2f(s.vel.x, s.vel.z) + (lead ? 0 : in.turn / 127.0f * 0.8f * DT);
            s.vel = {sinf(yaw) * wd.speed, 0, cosf(yaw) * wd.speed};
            np = s.pos = lead ? s.pos : Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            timed = timed && !lead;
            if (s.stage > 0) s.stage--;
            else if (detonate && s.prey < wd.clusters)
                spawned.push_back({Vector3Add(s.pos, {0, -1, 0}), Vector3Scale(s.vel, 0.3f), s.weapon, 0, true, 1}), s.prey++, s.stage = (int)(BOMBER_GAP * 60),
                emit(GameEvent::Launch, s.pos, -1, s.weapon);
            boom = s.prey >= wd.clusters && !s.stage;
        } else if (wd.kind == Kind::Scouser) {
            // inflated: floats up to a slow climb and drifts with the wind
            s.vel.y += (1.2f - s.vel.y) * 2 * DT;
            s.vel.x += wind * 3 * DT;
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            if (terrain.solid(np)) s.vel = Vector3Scale(s.vel, -0.3f);
            else s.pos = np;
            if (s.prey >= 0 && worms[s.prey].alive) worms[s.prey].pos = Vector3Subtract(s.pos, {0, 0.6f, 0}), worms[s.prey].vel = s.vel, worms[s.prey].grounded = false;
        } else if (wd.kind == Kind::Donkey && wd.clusters == 0) {  // W4M 0x553370: y = apex - 220 units/s^4 (t - tApex)^4, no gravity
            s.fuse += DT, np = s.pos;  // fuse: flight time; aim.x, aim.y: apex time and height; stage: ticks held after a smash
            if (s.stage > 0 && --s.stage == 0) s.aim = {s.fuse + DONKEY_HANG, s.pos.y + DONKEY_CURVE * powf(DONKEY_HANG, 4), 0};
            if (!s.stage) {
                float t = s.fuse - s.aim.x;
                s.vel = {0, (s.aim.y - DONKEY_CURVE * t * t * t * t - s.pos.y) / DT, 0};
                for (int k = 0, n = substeps(s.vel); k < n; k++) {
                    np = Vector3Add(s.pos, Vector3Scale(s.vel, DT / n));
                    if (terrain.solid(np)) { boom = true, s.stage = DONKEY_HOLD; break; }
                    s.pos = np;
                }
            }
            if (s.fuse >= DONKEY_LIFE - DT / 2) boom = true, s.hits = 1;  // LifeTime 8000, DetonatesOnExpiry
        } else {
            bool homing = wd.kind == Kind::Homing && (s.fuse += DT) > HOMING_LOCK && s.fuse < HOMING_LOCK + homingTime(wd);  // fuse: flight time
            if (homing) {  // 0x561730: the step, then the avoidance
                s.vel = homingStep(s.vel, s.pos, s.aim, wd.avoid ? 12.5f : HOMING_MAX);
                if (wd.avoid) avoidLand(s);
            }
            else s.vel.y -= gravity() * (s.child && wd.kind != Kind::Airstrike ? 1 : wd.grav) * DT;  // W4M bomblets: IsLowGravity 0
            if (wd.kind == Kind::Airstrike && wd.fuse > 0) s.vel.y = fmaxf(s.vel.y, -COW_CHUTE);  // the bomber's cows come down under a chute
            if (wd.wind || (cfg.wormpot & WP_WIND_ALL)) s.vel.x += wind * WIND_ACCEL * DT;
            bool impact = s.child || wd.fuse == 0;
            uint64_t now = 0;
            for (int k = 0, n = substeps(s.vel); k < n && !boom; k++) {  // W4M searches the whole path for the first land contact
                np = Vector3Add(s.pos, Vector3Scale(s.vel, DT / n));
                Vector3 sn;
                if (shell(s.pos, np, &sn)) {  // the land bounce response (vtable +0x80), Bubble.Hit
                    if (impact) { boom = true; break; }
                    s.vel = Vector3Scale(Vector3Reflect(s.vel, sn), wd.bounce);
                    break;
                }
                if (terrain.solid(np)) {
                    if (impact) { boom = true; break; }
                    s.vel = Vector3Scale(Vector3Reflect(s.vel, terrain.normal(np)), wd.bounce);
                    if (Vector3Length(s.vel) > 2) emit(GameEvent::Bounce, np, -1, s.weapon);
                    break;
                }
                s.pos = np;
                uint64_t m = touches(np);
                now |= m;
                if (impact && wd.kind != Kind::Donkey && (m & ~s.touching)) boom = true;
            }
            s.touching = now;
        }
        if (wd.kind == Kind::Homing && wd.avoid && s.fuse >= 30) boom = true;  // LifeTime 30000, DetonatesOnExpiry (0x598e0c)
        timed = timed && (!wd.restFuse || s.fuse < wd.fuse || Vector3Length(s.vel) < 1);
        if (timed && wd.restFuse && s.fuse == wd.fuse) emit(GameEvent::Hallelujah, s.pos, -1, s.weapon);  // at rest: the choir, then the blast
        if (timed && (s.fuse -= DT) < DT / 2) boom = true;  // n s = exactly 60 n ticks, whatever the float drift
        // off the map a shot flies on until it falls into the sea
        bool sank = s.pos.y < water - 2, gone = sank || (wd.kind == Kind::Homing && s.fuse >= HOMING_LIFE) || s.pos.x < -100 || s.pos.z < -100 || s.pos.x > W + 100 || s.pos.z > W + 100;
        if (sank && !boom) emit(GameEvent::Splash, {s.pos.x, water, s.pos.z}, -1, s.weapon);
        if (boom && bomber) {  // flies off
        } else if (boom && wd.kind == Kind::Scouser) {  // W4M: pops and drops its catch, empty it bursts harmlessly
            emit(GameEvent::Boom, s.pos, -1, s.weapon);
            if (s.prey >= 0 && worms[s.prey].alive) hurt(worms[s.prey], (int)wd.damage);
        } else if (boom) {
            s.pos = np;
            bool animal = wd.kind == Kind::Sheep || wd.kind == Kind::SuperSheep || wd.kind == Kind::Donkey || wd.kind == Kind::OldWoman;
            float super = (cfg.wormpot & (animal ? WP_SUPER_ANIMALS : WP_SUPER_EXPLOSIVES)) ? 1.5f : 1;
            Blast b = blastOf(wd, s.child);
            b.damage *= super, b.push *= super;
            explode(Vector3Add(np, {0, s.child ? 0 : wd.lift, 0}), b, s.child ? 0 : wd.poison);
            bool fly = !s.child && ((wd.kind == Kind::Homing && !wd.avoid) || (wd.kind == Kind::SuperSheep && (wd.name == "Starburst" ? wd.fuse - s.fuse >= 3.5f : !wd.walks || s.stage)));
            if (fly) camHold = msTicks(1000);  // the FlyCam's: CAMTWK PauseDuration 1000 (homing, super sheep, starburst)
            if (wd.name == "Starburst" && worms[current].alive) hurt(worms[current], worms[current].hp);  // W4M Worm.Vapourize 0x5885f0: its rider
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

void Game::step(const Input &raw) {
    events.clear();  // also when over: GameOver must reach the listeners once
    if (phase == Phase::GameOver) return;
    Input in = raw;
    int chosen = (in.buttons & Input::NEXT_WEAPON) && in.aim ? (uint8_t)in.aim - 1 : -1;
    if (chosen >= 0) in.aim = 0;
    uint8_t pressed = in.buttons & ~prevButtons;
    prevButtons = in.buttons;
    Worm &w = worms[current];
    bool detonate = phase != Phase::Aim && (pressed & Input::FIRE), tool = roped || jetting || hooked();
    bool drop = phase == Phase::Aim && dropping();  // W4M: the turn waits for the dropped crate to land
    if (landHold > 0) landHold--;
    if (camHold > 0) camHold--;
    Phase before = phase;

    if (phase == Phase::Aim && hotSeat > 0 && !drop) hotSeat = in.turn || in.walk || in.aim || (in.buttons & ~Input::TARGET) || in.flags ? 0 : hotSeat - 1;
    bool aimCursor = phase == Phase::Aim && (in.buttons & Input::TARGET) && !tool;  // walk and aim drive the cursor, whatever the weapon
    if (w.alive && !drop && icarus != 3 && (phase == Phase::Aim || retreating())) {  // drinking: W4M Worm.DisableMovementRef
        bool head = in.buttons & Input::HEADING;  // W4M 0x5b107c: walking sets Orientation to the input at once; the jetpack turns at 0x561e40's rate
        float rate = head ? remainderf(in.turn * PI / 128 - w.yaw, 2 * PI) / DT : in.turn / 127.0f * 2.5f, lim = jetting ? JET_TURN : head ? PI / DT : 2.5f;
        if (!aimCursor && !vault.t) w.yaw += Clamp(rate, -lim, lim) * DT;  // W4M Vaulting keeps the Orientation
        if (aimCursor && targeted(WEAPONS[weapon].kind)) {  // W4M IsometricCam 0x52a5e0
            if (!cursorOn) cursorYaw = w.yaw, cursorPitch = BLIMP_PITCH, cursor = blimpFocus(w.pos, w.yaw), cursorOn = true;
            cursorYaw += in.turn / 127.0f * BLIMP_TURN * DT;
            bool tilt = in.buttons & Input::PITCH;
            if (tilt) cursorPitch = Clamp(cursorPitch - in.aim / 127.0f * BLIMP_TILT * DT, 0, PI / 2);  // stick up: RotateUp
            float v = CURSOR_SPEED * DT / 127, y = cursorYaw, side = tilt ? 0 : in.aim;
            cursor.x += (sinf(y) * in.walk - cosf(y) * side) * v, cursor.z += (cosf(y) * in.walk + sinf(y) * side) * v;
            float dx = cursor.x - Terrain::NX * Terrain::VOX / 2, dz = cursor.z - Terrain::NZ * Terrain::VOX / 2;
            float dy = fmaxf(fabsf(cursor.y), fabsf(cursor.y - Terrain::NY * Terrain::VOX / 2));  // landCenter().y is in [0, NY VOX / 2]
            if (dx * dx + dy * dy + dz * dz > (BLIMP_RANGE - 1) * (BLIMP_RANGE - 1)) {  // landTop() costs ~300k samples: only near the edge
                Vector3 c = landCenter(), off = Vector3Subtract(cursor, c);
                if (Vector3Length(off) > BLIMP_RANGE) cursor = Vector3Add(c, Vector3Scale(Vector3Normalize(off), BLIMP_RANGE));
            }
        }
        blimp = aimCursor && targeted(WEAPONS[weapon].kind);
        if (phase == Phase::Aim && WEAPONS[weapon].kind == Kind::Girder) {
            if (!girderOn)  // W4M Update state 0: eye level (Worm.EyeLevelOffset 15 units), 40 units ahead
                girderOn = true, girderFrom = w.pos, cursorYaw = w.yaw, girderWait = 0,
                girder = Vector3Add(w.pos, {sinf(w.yaw) * 2, 0.75f, cosf(w.yaw) * 2});
            if (aimCursor) stepGirder(in, pressed);
        } else girderOn = false;
        const float ws = WALK_SPEED * (cfg.wormpot & WP_QUICK_WALK ? 2 : 1);  // quick walk x2: ours
        if (vault.t) {  // W4M 0x5ab3d0: the camera-relative stick, here the heading or the facing
            float y = head ? in.turn * PI / 128 : w.yaw;
            vaultStep(w.pos, vault, aimCursor ? Vector3{} : Vector3Scale({sinf(y), 0, cosf(y)}, in.walk));
        } else if (w.grounded && !w.motion.slide && in.walk && !aimCursor && !jumpDelay && !w.nailed) {  // W4M Sliding: no walking
            Vector3 walkV = Vector3Scale({sinf(w.yaw), 0, cosf(w.yaw)}, in.walk / 127.0f * ws);  // W4M Velocity = InputImpulse (0x546f10)
            if (walkStep(terrain, w.pos, w.yaw, in.walk / 127.0f * ws * DT, &vault)) w.vel = Vector3Scale(walkV, WALK_OFF), w.motion.air = true;  // Fall(), air control on
            else if (vault.t) vault.vel = walkV;
            else slideIfSteep(terrain, w.pos, w.vel, w.motion, walkV, cfg.wormpot);
        }
        steerIn = aimCursor ? Vector3{} : Vector3Scale(head ? Vector3{sinf(in.turn * PI / 128), 0, cosf(in.turn * PI / 128)} : Vector3{sinf(w.yaw), 0, cosf(w.yaw)}, in.walk / 127.0f);
        Vector3 jv;
        if ((pressed & Input::JUMP) && !jumpDelay && !vault.t && w.grounded && !w.motion.slide && !tool && !w.nailed && !(cfg.wormpot & WP_NO_JUMPING)) jumpDelay = JUMP_WINDOW, jumpKind = 2;
        else if (jumpDelay && jumpTick(jumpDelay, jumpKind, in.buttons, pressed, in.walk, w.yaw, jv) && w.grounded) {
            w.vel = jv, w.grounded = false, w.motion.air = true;  // DetectJump: Flags |= 1
            emit(GameEvent::Jump, w.pos, current);
        }
    } else {
        jumpDelay = 0, steerIn = {};
        if (vault.t) vaultStep(w.pos, vault, {});  // control gone: no input, back to the old pos
    }
    // rope and jetpack outlast the attack: still steered while the shot flies and during the retreat
    if (w.alive && !drop && (phase == Phase::Aim || (tool && (phase == Phase::Flying || phase == Phase::Retreat)))) {
        Vector3 push = Vector3Scale({sinf(w.yaw), 0, cosf(w.yaw)}, in.walk / 127.0f * DT);
        bool armed = phase == Phase::Aim && !utility(WEAPONS[weapon].kind);  // a weapon in hand: FIRE and the aim axis are its own
        if (roped) {
            if (pressed & Input::JUMP) roped = false;
            if (!armed) ropeLen = Clamp(ropeLen - in.aim / 127.0f * 6 * DT, 1, ropeMax);
            w.vel = Vector3Add(w.vel, Vector3Scale(push, 6));
        } else if (Object *o = hooked()) {
            if (pressed & Input::JUMP) o->hooked = false;
            if (!armed) ropeLen = Clamp(ropeLen - in.aim / 127.0f * 6 * DT, 1, ropeMax);
        } else if (jetting) {  // W4M 0x562810 every 20 ms, here per tick; FIRE held = FireUtil, whatever the hand holds
            bool burn = in.buttons & Input::FIRE;
            if ((w.grounded && !burn) || (burn && fuel <= JET_DRY)) jetting = false;  // landed (0x563252), or dry (0x562990): it falls
            else {
                const float n = DT / 0.02f, h = fmaxf(w.pos.y - water, 0);  // W4M steps per tick; height over Water.Level
                Vector3 f = {sinf(w.yaw), 0, cosf(w.yaw)}, a = {0, 0, 0};
                bool fwd = in.walk > 1, along = fwd && f.x * w.vel.x + f.z * w.vel.z > 0;  // stick > 0.01 along the facing; InputImpulse.Velocity > 0
                if (burn) {
                    float t = thrust * DT;
                    fuel -= DT;
                    a = fwd ? Vector3{f.x * sinf(JET_TILT) * t, cosf(JET_TILT) * t, f.z * sinf(JET_TILT) * t} : Vector3{0, t, 0};
                    if (h > JET_CEIL) a = {a.x * JET_OVER, w.vel.y > 0 ? 0 : a.y * JET_OVER, a.z * JET_OVER};
                    else {
                        a = Vector3Scale(a, sinf((1 - h / JET_CEIL) * PI / 2));
                        if (w.vel.y < -JET_FALL) boost = fminf(1, boost + BOOST_ACCEL * fminf(1, BOOST_MOD * (-JET_FALL - w.vel.y)) * n);  // 0x562180
                        else if ((boost *= powf(BOOST_DECAY, n)) < BOOST_OFF) boost = 0;
                        a.y *= 1 + boost;
                    }
                } else if ((boost *= powf(BOOST_DECAY, n)) < BOOST_OFF) boost = 0;
                float k = powf(along ? JET_RES : JET_RES_IDLE, n);
                w.vel = {w.vel.x * k + a.x, w.vel.y + a.y, w.vel.z * k + a.z};
            }
        } else if (chute && !w.grounded) w.vel = Vector3Add(w.vel, Vector3Scale(push, 5));
        if (phase == Phase::Aim && !tool) armed = true;
        if (phase == Phase::Aim) {
            if (armed && !aimCursor) w.pitch = Clamp(w.pitch + in.aim / 127.0f * 1.5f * DT, -1.2f, 1.45f);
            if (chosen >= 0 && !shotsLeft) pick(w.team, chosen);
            else if ((pressed & Input::NEXT_WEAPON) && !shotsLeft) nextWeapon(w.team);
            if (WEAPONS[weapon].userFuse) fuses[w.team] = std::clamp(fuses[w.team] + !!(pressed & Input::FUSE_UP) - !!(pressed & Input::FUSE_DOWN), 1, 5);
            bool spare = jetUsed && WEAPONS[weapon].kind == Kind::Jetpack;  // takes off again without ammo
            // Fire.Second: JUMP in flight, PITCH without TARGET on a landed jetpack (UtilityFire stays on: FIRE takes off, JUMP jumps)
            uint8_t second = jetting ? Input::JUMP : jetLanded() ? (in.buttons & Input::TARGET ? 0 : Input::PITCH) : Input::FIRE;
            bool drop2 = secondary >= 0 && (pressed & second) && usable(w.team, secondary) && !w.nailed;
            if (drop2) {  // W4M UtilityFire group 0x4e1d50: Fire.Second fires the secondary, the tool stays in hand
                int tool = weapon;
                weapon = secondary, secondary = -1, power = 0;
                use(w);
                weapon = tool;
            } else if (armed && ((ammo[w.team][weapon] && !delays[w.team][weapon]) || shotsLeft || spare) && !(w.nailed && !nailUsable(WEAPONS[weapon].kind))) {
                Vector3 h;
                // W4M CanFire is asked on Input.FirePressed only (0x586092); a charge started goes on to its release
                if (!powered(WEAPONS[weapon].kind)) { if ((pressed & Input::FIRE) && fireable(w) && !(aimCursor && cursorOn && !blimpHit(&h))) use(w); }  // W4M: no target, NotClearToFire
                else if (aimCursor && targeted(WEAPONS[weapon].kind)) {  // homing in the Blimp, W4M state 1: FIRE takes its target, no charge
                    if ((pressed & Input::FIRE) && fireable(w) && !locked && cursorOn && blimpHit(&h)) locked = true, lockAt = h;
                } else {
                    if ((in.buttons & Input::FIRE) && (power > 0 || ((pressed & Input::FIRE) && fireable(w)))) power = fminf(1, power + DT / 1.5f);
                    if (power > 0 && (!(in.buttons & Input::FIRE) || power >= 1)) use(w);
                }
            }
        }
    }

    if (secondary >= 0 && !toolOut() && !jetLanded()) weapon = secondary, secondary = -1, jetUsed = false;  // W4M 0x565920: rope, chute, jetpack dry
    if (icarus && (WEAPONS[weapon].kind != Kind::Icarus || !w.alive)) icarus = 0, drift = {};  // a weapon change deletes it (0x587540)
    if (icarus == 3 && clock >= flapAt) {  // 0x587750 at the PostLaunchDelay: Worm.Antidote, energy back to its start (unverified: InitialEnergy = the scheme's)
        int up = std::max(0, (int)cfg.scheme.health - w.hp);
        w.hp += up, w.counted += up, w.poison = 0, w.abducted = false;
        icarus = 1;
    }
    if (icarus == 1 && !w.grounded) icarus = 2, flapAt = clock + FLAP_WAIT, drift = {};  // took off: PackAccessory.Wield
    else if (icarus == 2 && w.grounded) icarus = 1;  // landed: the wings fold
    if (icarus == 2) {  // 0x587970: JUMP and back-jump flap alike; too early restarts the wait, a missed window waits a beat
        while (clock >= flapAt + FLAP_WAIT) flapAt += FLAP_BEAT;
        Vector3 was = drift, f = {sinf(w.yaw), 0, cosf(w.yaw)};
        drift = Vector3Add(drift, Vector3Scale(f, in.walk / 127.0f * AFTERTOUCH));  // WXWorm.AftertouchDelta 0.015 u/ms a frame
        if (Vector3Length(drift) > AFTERTOUCH_MAX) drift = Vector3Scale(Vector3Normalize(drift), AFTERTOUCH_MAX);  // AftertouchStrength 0.1
        w.vel = Vector3Add(w.vel, Vector3Subtract(drift, was));
        if (pressed & Input::JUMP) {
            if (clock < flapAt) flapAt = clock + FLAP_WAIT;
            else {
                bool sky = w.pos.y > (Terrain::NY - 4) * Terrain::VOX;  // unverified: W4M stops under its SkyBox height
                w.vel = {drift.x, sky ? 0 : FLAP, drift.z}, flapAt += FLAP_BEAT;
                emit(GameEvent::Jump, w.pos, current);
            }
        }
    }
    if (scout.t >= 0) scout.t = WEAPONS[weapon].kind == Kind::Binoculars && phase == Phase::Aim ? scout.t + 1 : -1;  // LeaveBinocularsVision
    // W4M: the parachute in hand opens by itself on a long fall (past a jump's landing speed)
    if (!chute && !tool && w.alive && !w.grounded && w.vel.y < -FALL_SAFE * 0.8f && (phase == Phase::Aim || retreating()) &&
        WEAPONS[weapon].kind == Kind::Parachute && ammo[w.team][weapon])
        use(w);
    // W4M 0x5833a0 / 0x54a0e0: PostLaunchDelay, then StartRetreatTimer (RetreatTimeOverride or DefaultRetreatTime), the shot still flying
    if (before != phase && phase == Phase::Flying) timer = msTicks(WEAPONS[weapon].postLaunch) + retreatTicks(WEAPONS[weapon]);
    if (chute && !w.grounded && w.vel.y < -2.5f) w.vel.y = -2.5f;  // below FALL_SAFE: no fall damage
    if (chute && !w.grounded) w.vel.x += (wind * 2 - w.vel.x) * DT;  // drifts downwind, up to 2 m/s per wind unit
    if (vault.t && Vector3Distance(w.pos, vault.to) > Vector3Distance(vault.from, vault.to) + 0.01f) vault.t = 0;  // moved by a weapon
    else if (vault.t && (roped || jetting || !w.grounded || Vector3LengthSqr(w.vel) > 0)) w.pos = vault.to, vault.t = 0;  // W4M ChangeState 0x5aa847: snaps to the target
    for (Worm &x : worms)
        if (roped && &x == &w) stepRope(x);
        else stepWorm(x);
    stepShots(in, detonate);
    if (Object *o = hooked(); o && w.alive && phase != Phase::Settle) {  // the worm outweighs it (Ninja.WormMass 100): the rope drags it
        Vector3 d = Vector3Subtract(o->pos, w.pos);
        float l = Vector3Length(d), out;
        if (l > ropeLen && l > 1e-4f) {
            Vector3 n = Vector3Scale(d, 1 / l);
            if ((out = Vector3DotProduct(o->vel, n)) > 0) o->vel = Vector3Subtract(o->vel, Vector3Scale(n, out));
            o->vel = Vector3Subtract(o->vel, Vector3Scale(n, (l - ropeLen) / DT));
        }
    }
    stepObjects();
    for (size_t i = 0; i < bubbles.size();) {  // 0x54f160: falls until it rests on land, gone under water
        Bubble &b = bubbles[i];
        b.age++;
        if (!terrain.solid({b.pos.x, b.pos.y - 0.05f, b.pos.z})) b.vel.y -= gravity() * DT, b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, DT));
        else b.vel = {0, 0, 0};
        for (int k = 0; k < 20 && terrain.solid(b.pos); k++) b.pos.y += 0.05f;
        if (b.pos.y < water) emit(GameEvent::BubblePop, b.pos), bubbles.erase(bubbles.begin() + i);
        else i++;
    }
    for (size_t i = 0; i < gas.size();) {
        Gas &c = gas[i];
        c.pos.x += wind * DT;
        for (Worm &x : worms)
            if (x.alive && Vector3Distance(x.pos, c.pos) < GAS_RADIUS && !(cfg.wormpot & WP_WORMS_DROWN)) x.poison = std::max(x.poison, (int)c.poison), x.abducted = false;
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
        if (!w.alive || selfHurt || over || (!hotSeat && !drop && --timer <= 0)) { phase = Phase::Settle; timer = SETTLE_WAIT; roped = jetting = chute = false; }
        break;
    case Phase::Flying:
        if (shots.empty()) phase = Phase::Retreat;
        [[fallthrough]];
    case Phase::Retreat:  // W4M Timer_RetreatTimedOut / Worm_Damaged_Current -> EndTurn, which waits for the shots (ObjectCount.Active)
        if (!w.alive || selfHurt || over || --timer <= 0) {
            phase = Phase::Settle, timer = SETTLE_WAIT + (shots.empty() ? 0 : SHOT_CAP), jumpDelay = 0, roped = jetting = chute = false;
        }
        break;
    case Phase::Settle: {
        if (!shots.empty()) {  // ours: a shot still flying SHOT_CAP into the settle is dropped
            if (--timer <= SETTLE_WAIT && !abducting()) shots.clear();  // W4M waits for the saucer, however many it carries
            break;
        }
        bool still = !active();
        if (!countGroup.empty()) {  // W4M: the labels count down together, then the dead blow up one by one (its death queue)
            int boom = countBoom(), dead = 0;
            ++countT;
            std::vector<int> recount;  // living members a death blast hurt: they count again in a later group
            auto deathBlast = [&](Vector3 p) {
                std::vector<int> was;
                for (int j : countGroup) was.push_back(worms[j].hp);
                explode(p, DEATH_BLAST);
                for (size_t k = 0; k < countGroup.size(); k++)
                    if (worms[countGroup[k]].hp != was[k]) worms[countGroup[k]].counted = was[k], recount.push_back(countGroup[k]);
            };
            for (int i : countGroup) {
                Worm &x = worms[i];
                if (x.hp > 0) continue;
                int at = blastAt(i);
                dead++;
                if (countT == at && !x.alive && x.counted > 0) {  // drowned: blows up at the surface, no grave
                    x.counted = 0;  // gone: the renderer stops drawing it afloat
                    deathBlast({x.pos.x, water, x.pos.z});
                    emit(GameEvent::Death, x.pos, i);
                }
                if (countT >= at && x.alive) {
                    if ((cfg.rules & RULE_HIGHLANDER) && lastHitTeam[i] >= 0 && lastHitTeam[i] != x.team)
                        for (size_t wi = 0; wi < WEAPONS.size(); wi++)
                            if (ammo[x.team][wi] && ammo[lastHitTeam[i]][wi] >= 0) ammo[lastHitTeam[i]][wi]++;
                    x.alive = false;
                    deathBlast(x.pos);
                    emit(GameEvent::Death, x.pos, i);
                }
            }
            for (int j : recount)
                if (auto it = std::find(countGroup.begin(), countGroup.end(), j); it != countGroup.end()) countGroup.erase(it);
            if (countT >= (dead ? blastAt(-1) : boom)) {
                for (int i : countGroup) worms[i].counted = std::max(0, worms[i].hp);
                countGroup.clear();
            }
            break;
        }
        if (timer < 0) {  // W4M PostActivityTime, once nothing moves and every count is over
            if (++timer < 0) break;
            if (!still) { timer = SETTLE_WAIT; break; }  // stdlib CheckActivity: wait for GameLogic_NoActivity, count, then PostActivityTime again
            if (!crated) {  // W4M DoPostActivity pass 1, DoOncePerTurnFunctions: DropRandomCrate (stdvs)
                crated = true;
                bool drop = false;
                for (int n = cfg.wormpot & WP_CRATE_SHOWER ? 6 : 1; n > 0 && !over; n--)  // W4M GameLogic.CrateShower 0x4fb850: 6 crates
                    if (!(cfg.rules & RULE_ROPE_RACE) && rand01() * 100 < cfg.scheme.crateChance && addObject(Object::Crate, 15))
                        emit(GameEvent::CrateDrop, objects.back().pos), drop = true;
                if (drop) { timer = SETTLE_WAIT; break; }  // the crate is active ("Crate Spawn") until it rests
            }
            {
                for (int &d : delays[w.team]) d = std::max(0, d - 1);  // W4M ActivateNextWorm 0x5b5a5f -> DecrementWeaponDelays 0x4f4df0: the team that just played
                picked[w.team] = weapon, beginTurn(w.team);
            }
            break;
        }
        if (still || --timer <= 0) {  // W4M WaitUntilNoActivity (stdlib.lub); ours gives up after SETTLE_WAIT
            if (!abdRolled) {  // DoPostActivity (stdlib.lub): ApplyPoison, then ApplyDamage; 0x5ac060 gives an unhurt abductee rand % 100 hp, 0 kills (no minimum)
                abdRolled = true;
                for (Worm &x : worms) {
                    if (!x.alive || !x.abducted) continue;
                    if (x.calm >= 0 && x.hp >= x.calm) {
                        int h = (int)(rand01() * 100), wi = int(&x - worms.data());
                        if (h != x.hp) emit(GameEvent::AbdDamage, x.pos, wi);
                        if (h < x.hp) emit(GameEvent::Hurt, x.pos, wi);
                        x.hp = h;
                    }
                    x.calm = x.hp;
                }
            }
            if (cfg.rules & RULE_KING)  // king gone: his team counts down to 0 and blows up like any dead worm
                for (int t = 0; t < teams; t++)
                    if (t * perTeam < (int)worms.size() && !worms[t * perTeam].alive)
                        for (int k = 1; k < perTeam; k++) worms[t * perTeam + k].hp = std::min(worms[t * perTeam + k].hp, 0);
            auto pending = [](const Worm &x) { return x.alive ? x.hp <= 0 || x.hp != x.counted : x.counted > 0; };
            for (const Worm &x : worms)  // first pending worm and those near it: one camera shot
                if (pending(x)) {
                    for (const Worm &y : worms)
                        if (pending(y) && Vector3Distance(x.pos, y.pos) < COUNT_SPAN) countGroup.push_back(int(&y - worms.data()));
                    countT = 0, countEnd = countSpan();
                    break;
                }
            if (countGroup.empty()) timer = -POST_ACTIVITY;
        }
        break;
    }
    case Phase::GameOver: break;
    }
    if (cfg.mission && phase != Phase::GameOver) missionStep(*this);
}

// W4M 0x558c30: one 6-unit step per cycle, camera-relative, Right > Left > Back > Forward, raise / lower besides
void Game::stepGirder(const Input &in, uint8_t pressed) {
    cursorYaw += in.turn / 127.0f * GIRDER_YAW * DT;  // GirderCamera YawSpeed 0.4
    if (girderWait > 0 && --girderWait) return;
    bool tilt = in.buttons & Input::PITCH;
    int side = tilt ? 0 : in.aim;
    Vector3 fwd = {sinf(cursorYaw), 0, cosf(cursorYaw)}, right = {-cosf(cursorYaw), 0, sinf(cursorYaw)}, d = {0, 0, 0};
    if (side > 40) d = right;
    else if (side < -40) d = Vector3Negate(right);
    else if (in.walk < -40) d = Vector3Negate(fwd);
    else if (in.walk > 40) d = fwd;
    if (tilt && abs(in.aim) > 40) d.y = in.aim > 0 ? 1 : -1;
    if (Vector3LengthSqr(d) == 0) return;
    Vector3 c = Vector3Add(girder, Vector3Scale(d, GIRDER_STEP));
    c = {Clamp(c.x, girderFrom.x - GIRDER_RANGE, girderFrom.x + GIRDER_RANGE), Clamp(c.y, girderFrom.y - GIRDER_RANGE, girderFrom.y + GIRDER_RANGE),
         Clamp(c.z, girderFrom.z - GIRDER_RANGE, girderFrom.z + GIRDER_RANGE)};  // 0x558b20: per axis
    bool sea = c.y + 1 <= water, sky = c.y > 37.5f && c.y > fminf(landTop() + 37.5f, (Terrain::NY - 6) * Terrain::VOX);  // Land.InitialMaxHeight + 750
    bool into = !(girderFits(girder) & 1) && (girderFits(c) & 1);  // a valid preview can't move into land
    if (!sea && !sky && !into) girder = c;
    girderWait = GIRDER_TICKS;
}

// 1: land on an edge of the probe box (half 30, 20, 30 units); 2: a worm or object in a probe sphere (radius 30 at +-15, +-15)
int Game::girderFits(Vector3 c) const {  // axis-aligned: W4M PC never sets the yaw / pitch bits 0x200-0x1000
    const Vector3 x = {1, 0, 0}, z = {0, 0, 1}, y = {0, 1, 0};
    const Vector3 half = {1.5f, 1, 1.5f};
    int r = 0;
    for (int e = 0; e < 12 && !r; e++) {  // 4 edges along each axis
        int ax = e / 4, s1 = e & 1 ? 1 : -1, s2 = e & 2 ? 1 : -1;
        for (float t = -1; t <= 1.001f && !r; t += 0.125f) {
            float v[3];
            v[ax] = t, v[(ax + 1) % 3] = s1, v[(ax + 2) % 3] = s2;
            Vector3 p = Vector3Add(c, Vector3Add(Vector3Scale(x, v[0] * half.x), Vector3Add(Vector3Scale(y, v[1] * half.y), Vector3Scale(z, v[2] * half.z))));
            if (terrain.solid(p)) r = 1;
        }
    }
    for (float sx : {-0.75f, 0.75f})
        for (float sz : {-0.75f, 0.75f}) {
            Vector3 p = Vector3Add(c, Vector3Add(Vector3Scale(x, sx), Vector3Scale(z, sz)));
            for (const Worm &w : worms) if (w.alive && Vector3Distance(p, w.pos) < 1.5f + R) r |= 2;
            for (const Object &o : objects) if (Vector3Distance(p, o.pos) < 1.5f + 0.45f) r |= 2;
        }
    return r;
}

bool Game::shielded(const Worm &w, Vector3 blast) const {  // 0x5a61e0: the worm's bubble ignores what comes from outside it
    for (const Bubble &b : bubbles) {
        Vector3 c = Vector3Add(b.pos, {0, BUBBLE_UP, 0});
        if (Vector3Distance(w.pos, c) < BUBBLE_IN && Vector3Distance(blast, c) > BUBBLE_R) return true;
    }
    return false;
}

// W4M 0x54b6c0: 0.3 + 0.7 (1 - sin(pi ms / 4000)) for 2.7 s, eased to 1 by 4 s; otherwise x 1.06 per 20 ms up to 1
float Game::scoutZoom(float z, float dt) const {
    float n = dt / 0.02f, ms = scout.t * DT * 1000;
    if (scout.t < 0 || ms >= 4000) return fminf(z * powf(1.06f, n), 1);
    if (ms < 2700) return 0.3f + 0.7f * (1 - sinf(PI * ms / 4000));
    return 1 - (1 - z) * powf(1 - (ms - 2700) / 1300, n);
}

// W4M 0x54bcc0 (TargetParabola 0x519a40): ten launch speeds from full power down, the lowest clear arc wins; ours: our bazooka
bool Game::scoutSolve(Vector3 at, float &pw, float &pitch) const {
    const WeaponDef *baz = nullptr;
    for (const WeaponDef &d : WEAPONS) if (!baz && d.kind == Kind::Shell && d.fuse <= 0 && d.radius > 0) baz = &d;
    if (!baz) return false;
    const Worm &w = worms[current];
    Vector3 to = Vector3Subtract(at, w.pos);
    float x = sqrtf(to.x * to.x + to.z * to.z), y = to.y, g = gravity(), yaw = atan2f(to.x, to.z), best = 1e9f;
    for (int k = 0; k <= 10; k++) {
        float p = fmaxf(1 - 0.1f * k, 0.15f), v = launchSpeed(*baz, p), q = v * v * v * v - g * (g * x * x + 2 * y * v * v);
        if (q < 0) continue;
        for (float sg : {-1.0f, 1.0f}) {
            float th = atanf((v * v + sg * sqrtf(q)) / (g * fmaxf(x, 0.01f)));
            if (th >= best) continue;
            Vector3 d = {cosf(th) * sinf(yaw), sinf(th), cosf(th) * cosf(yaw)}, pos = launchPoint(*baz, w.pos, yaw), vel = Vector3Scale(d, v);
            float T = x / fmaxf(v * cosf(th), 0.01f) - 0.1f;  // 0x54add0: clear until 100 ms before arrival
            bool clear = true;
            for (float t = 0; t < T && clear; t += DT) {
                vel.y -= g * DT, pos = Vector3Add(pos, Vector3Scale(vel, DT));
                clear = !terrain.solid(pos);
            }
            if (clear) best = th, pw = p, pitch = th;
        }
    }
    return best < 1e9f;
}

uint32_t Game::checksum() const {
    uint32_t h = 2166136261u;
    auto mix = [&](const void *p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
    };
    for (const Worm &w : worms) { mix(&w.pos, sizeof w.pos); mix(&w.vel, sizeof w.vel); mix(&w.hp, sizeof w.hp); mix(&w.yaw, sizeof w.yaw); mix(&w.pitch, sizeof w.pitch); mix(&w.alive, sizeof w.alive); mix(&w.poison, sizeof w.poison); mix(&w.counted, sizeof w.counted); mix(&w.nailed, 1); mix(&w.armour, 1); mix(&w.motion.stuck, sizeof w.motion.stuck), mix(&w.motion.air, 1), mix(&w.motion.slide, 1), mix(&w.motion.spin, sizeof w.motion.spin), mix(&w.motion.spinTo, sizeof w.motion.spinTo), mix(&w.motion.normal, sizeof w.motion.normal); if (w.abducted) mix(&w.zap, sizeof w.zap), mix(&w.calm, sizeof w.calm); }
    for (const Projectile &s : shots) { mix(&s.pos, sizeof s.pos); mix(&s.vel, sizeof s.vel); mix(&s.weapon, sizeof s.weapon); mix(&s.fuse, sizeof s.fuse); mix(&s.hits, sizeof s.hits); mix(&s.stage, sizeof s.stage); mix(&s.prey, sizeof s.prey); mix(&s.aim, sizeof s.aim); mix(&s.touching, sizeof s.touching); }
    mix(&chute, 1);
    mix(&landHold, sizeof landHold);
    mix(&ropeShots, sizeof ropeShots);
    mix(lastHitTeam.data(), lastHitTeam.size() * sizeof(int));
    mix(&rng, sizeof rng);
    mix(&current, sizeof current);
    mix(&weapon, sizeof weapon);
    mix(picked.data(), picked.size() * sizeof(int));
    mix(fuses.data(), fuses.size() * sizeof(int));
    for (const auto &d : delays) mix(d.data(), d.size() * sizeof(int));
    mix(&water, sizeof water);
    mix(&clock, sizeof clock);
    mix(&hotSeat, sizeof hotSeat);
    mix(&jumpDelay, sizeof jumpDelay), mix(&jumpKind, sizeof jumpKind);
    if (vault.t) mix(&vault, sizeof vault);
    mix(&selfHurt, sizeof selfHurt);
    mix(&cfg.scheme, sizeof cfg.scheme);
    mix(&cfg.rules, sizeof cfg.rules);
    mix(&cfg.wormpot, sizeof cfg.wormpot);
    mix(&suddenDeath, sizeof suddenDeath);
    mix(&raceFinish, sizeof raceFinish);
    if (cursorOn) mix(&cursor, sizeof cursor), mix(&cursorYaw, sizeof cursorYaw), mix(&cursorPitch, sizeof cursorPitch);  // never on in replays from before the cursor: their checksums hold
    if (locked) mix(&lockAt, sizeof lockAt);
    mix(countGroup.data(), countGroup.size() * sizeof(int)), mix(&countT, sizeof countT), mix(&countEnd, sizeof countEnd);
    for (const Gas &c : gas) mix(&c, sizeof c);
    if (abdRolled) mix(&abdRolled, 1);
    if (crated) mix(&crated, 1);
    if (camHold) mix(&camHold, sizeof camHold);
    for (const Abductee &a : abductees) mix(&a.worm, sizeof a.worm), mix(&a.st, 1), mix(&a.from, sizeof a.from);
    mix(&girderOn, 1), mix(&girders, sizeof girders), mix(&girderWait, sizeof girderWait);
    if (girderOn) mix(&girder, sizeof girder), mix(&girderFrom, sizeof girderFrom), mix(&cursorYaw, sizeof cursorYaw);
    if (secondary >= 0) mix(&secondary, sizeof secondary);
    if (jetting || jetUsed) mix(&jetting, 1), mix(&jetUsed, 1), mix(&fuel, sizeof fuel), mix(&boost, sizeof boost);  // no jetpack: old replays' sums hold
    for (const Bubble &b : bubbles) mix(&b, sizeof b);
    mix(&icarus, sizeof icarus), mix(&flapAt, sizeof flapAt), mix(&drift, sizeof drift), mix(&doubleDamage, 1), mix(spy.data(), spy.size());
    mix(&scout.t, sizeof scout.t), mix(&scout.power, sizeof scout.power), mix(&scout.pitch, sizeof scout.pitch);
    for (const Object &o : objects) { mix(&o.type, 1); mix(&o.pos, sizeof o.pos); mix(&o.vel, sizeof o.vel); mix(&o.weapon, sizeof o.weapon); mix(&o.fuse, sizeof o.fuse); mix(&o.falling, 1); mix(&o.dead, 1); mix(&o.team, sizeof o.team); mix(&o.tag, sizeof o.tag); mix(&o.dud, 1); mix(&o.courtesy, sizeof o.courtesy); mix(&o.hooked, 1); }
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
        mix(&wd.poison, sizeof wd.poison), mix(wd.blast, sizeof wd.blast), mix(wd.cblast, sizeof wd.cblast), mix(&wd.lift, sizeof wd.lift), mix(&wd.grav, sizeof wd.grav), mix(&wd.base, sizeof wd.base);
        if (wd.avoid) mix(&wd.avoid, 1);
        mix(&wd.userFuse, sizeof wd.userFuse), mix(&wd.restFuse, sizeof wd.restFuse), mix(&wd.walks, 1), mix(&wd.pins, 1);
    }
    return h;
}

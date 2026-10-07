#include "sim.h"
#include "script.h"
#include "navgrid.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

const WeaponDef NO_WEAPON = [] { WeaponDef d{}; d.kind = Kind::None; return d; }();
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
            {"cluster_reach", &w.cblast[0]}, {"cluster_push", &w.cblast[1]}, {"cluster_push_reach", &w.cblast[2]}, {"cluster_push_depth", &w.cblast[3]}, {"lift", &w.lift}, {"stick", &w.stick}, {"gravity", &w.grav}, {"min_speed", &w.base}, {"fuse_height", &w.fuseHeight}, {"fuse_size", &w.fuseSize},
            {"size", &w.size}, {"sink", &w.sink}, {"cluster_size", &w.csize}, {"cluster_sink", &w.csink}, {"skim_speed", &w.skim[0]}, {"skim_angle", &w.skim[1]},
            {"skim_xz", &w.skim[2]}, {"skim_y", &w.skim[3]}, {"cluster_cone", &w.ccone}, {"cluster_min_speed", &w.cspeed[0]}, {"cluster_max_speed", &w.cspeed[1]}, {"cluster_spread", &w.spread}};
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
            if (key == "splash") w.splash = sv;
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
                   "\"poison\": %g, \"cluster_spread\": %g, \"wind\": %s, \"avoid_land\": %s, \"model\": \"%s\", \"icon\": \"%s\"}%s\n",
                clean(w.name).c_str(), KINDS[(int)w.kind], w.radius, w.damage, w.speed, w.fuse, w.bounce, w.clusters, w.cradius, w.cdamage,
                w.count, w.shots, w.weight, w.poison, w.spread, w.wind ? "true" : "false", w.avoid ? "true" : "false", clean(w.model).c_str(), clean(w.icon).c_str(), i + 1 < list.size() ? "," : "");
    }
    fputs("]\n", f);
    return fclose(f) == 0;
}

const WormpotInfo WORMPOT_MODES[WP_MODES] = {  // English FETXT.WPotName / WPotHelp
    {"Empty", "- Empty Reel -", "No wormpot mode selected on this reel."},
    {"Empty", "- Empty Reel -", "No wormpot mode selected on this reel."},
    {"SuperExplos", "Super Explosives", "All explosive weapons do increased damage and will throw worms further than usual."},
    {"SuperCluster", "Super Clusters", "All cluster weapons do increased damage and will throw worms further than usual."},
    {"SuperAnimals", "Super Animals", "All animal weapons do increased damage and will throw worms further than usual."},
    {"SuperFirearms", "Super Firearms", "All firearm weapons do increased damage and will throw worms further than usual."},
    {"SuperMelee", "Super Hand To Hand", "All melee weapons do increased damage and will throw worms further than usual."},
    {"WormsDrown", "Worms Only Drown", "Worms can only be killed by knocking them into the water, no health is removed via a shot."},
    {"Goliath", "David And Goliath", "One of your worms is the mighty Goliath (lots of energy) and the rest are Davids (not much energy)."},
    {"MaxFall", "Max Fall Damage", "Fall damage will hurt worms a lot."},
    {"DoubleDamage", "Double Damage", "Everything does twice the amount of damage."},
    {"CrateShower", "Crates Everywhere", "A crate shower will arrive at the start of every turn."},
    {"Specialist", "Specialists", "Each worm on the team will have a special weapon set."},
    {"NoCowards", "No Cowards", "Worms cannot retreat or surrender."},
    {"Max Health", "Max Health Drops", "Health crates will contain 100 health."},
    {"WindAll", "Wind Affects All", "Wind affects all weapons in the game, even the grenade."},
    {"Energy", "Energy Or Enemy", "All worms are poisoned, cure your worms or kill the enemy."},
    {"CrateDrops", "Crate Drops Only", "Weapons can only be obtained through crate drops."},
    {"Sticky", "Sticky Mode", "Worms will stick to the landscape more and be harder to move around."},
    {"Slippy", "Slippy Mode", "Worms will slide around the landscape more easily."},
    {"Lowgravity", "Low Gravity", "Gravity has less of an effect on worms making them jump higher."},
    {"NoJumping", "No Jumping", "Worms lose their ability to jump and can only walk around the landscape."},
    {"TugOWorms", "Tug O Worms", "Worms can only move around using the Grappling Hook."},
    {"WindGuns", "Wind Affects Guns", "Gun wobble is affected by the wind speed."},
    {"QuickWalk", "Quick Walk", "Worms will be able to walk a lot faster than normal."},
    {"BlimpView", "No Blimp View", "The Blimp View camera will be disabled."},
    {"MineRespawn", "Mine Respawn", "The landscape is indestructible and any mine that explodes will respawn again and again."},
    {"MUltiGirder", "Multiple Girders", "More than one girder can be placed during one turn."},
    {"DimMak", "Dim-Mak", "Changes the normally harmless prod function into a health sapping death touch."},
    {"NoBombing", "No Bombing", "This will stop the worm's ability to drop weapons whilst using movement utilities."},
    {"Vampire", "Vampire", "One of your worms is a vampire. Half the damage he inflicts on another worm is gifted back to him."},
    {"VitalWorm", "Vital Worm", "One worm on each team is vital, if he dies the team dies."},
    {"SecretWeap", "Super Secret Weapons", "A random weapon in the inventory will do super damage."},
    {"DonorCard", "Donor Card", "Whenever a worm dies its complete inventory is left for anyone to collect."},
    {"GirdersOnly", "Girders Only", "There is no land, only girders, and that's the only way to get around."},
    {"WindWorms", "Wind Affects Worms", "Wind affects worms when jumping or knocked into the air."},
    {"JumpingOnly", "Jumping Only", "Worms can only jump to get around the landscape."},
    {"OneShot", "One Shot One Kill", "All worms start with 1 health so one shot does indeed mean one kill."},
};
const MysteryItem MYSTERY_ITEMS[15] = {
    {"MineLayer", "Bring On The Mines", 40}, {"MineTriplet", "Random Detonation", 30}, {"BarrelTriplet", "Barrel O' Laughs", 40},
    {"Flood", "Global Warming", 50}, {"Disarm", "Sabotage Inventory", 40}, {"Teleport", "Reposition Worm", 60}, {"QuickWalk", "Hyperactive!", 70},
    {"LowGravity", "Moon Physics", 70}, {"DoubleTurnTime", "Extended Time", 60}, {"Health", "Medical Insurance", 70},
    {"Damage", "Damage Incoming", 40}, {"SuperHealth", "Super Health", 40}, {"SpecialWeapon", "Big Guns!", 50},
    {"BadPoison", "Team Disease", 20}, {"GoodPoison", "Sick It To Them", 30},
};
const std::vector<int> WORMPOT_REEL[3] = {{2, 3, 4, 5, 6, 11, 10, 17, 9, 16, 15, 14, 13, 12, 7, 30, 20, 23, 25, 27, 28, 29, 31, 32, 35},
                                         {2, 3, 4, 5, 6, 11, 10, 9, 37, 8, 18, 19, 15, 14, 13, 20, 23, 25, 27, 29, 31, 32, 35},
                                         {2, 3, 4, 5, 6, 11, 10, 26, 36, 21, 20, 22, 23, 24, 25, 27, 29, 31, 32, 35}};


// Order: turn, retreat (LandTime), hot seat (10 s in every W4M scheme), round (min), energy, crate %, weapon/health/utility shares, crate hp, mines, barrels
// (Objects: 3 = 15 mines + 10 drums, 2 = drums only), mine fuse, sudden death, fall damage, wind, weapon set (ours), water speed, mystery share (*Chance, 0x4fa4b0).
const std::vector<SchemePreset> SCHEMES = {
    {"Standard", {45, 5, 10, 20, 100, 40, 30, 30, 20, 25, 15, 10, 3, 1, 1, 1, 0, 2}, "Airstrike 5|Banana Bomb 8|Holy Hand Grenade 3|Homing Missile 2|Super Sheep 5|Icarus Potion 2|Binoculars 2|* 3"},
    {"Beginner", {90, 5, 10, 20, 100, 50, 30, 30, 20, 50, 15, 10, 5, 0, 1, 0, 0, 1}, "Homing Missile 2"},
    {"Pro", {30, 0, 10, 20, 100, 30, 10, 30, 50, 25, 15, 10, Scheme::FUSE_RANDOM, 1, 1, 3, 0, 3}, "Icarus Potion 4|Sentry Gun 5|Sniper Rifle 3|Starburst 4|Binoculars 3"},
    {"BnG", {30, 5, 10, 10, 150, 40, 40, 10, 40, 25, 15, 10, 5, 1, 1, 1, Scheme::SET_BNG, 2, 0, 1}, "Concrete Donkey 3|Icarus Potion 2|Bubble Trouble 3|* 8"},
    {"Shopping", {60, 5, 10, 20, 100, 100, 60, 20, 20, 25, 0, 10, Scheme::FUSE_RANDOM, 1, 1, 1, Scheme::SET_CRATES, 1}, ""},
    {"All Action", {30, 5, 10, 10, 200, 40, 30, 30, 20, 25, 15, 10, 1, 1, 1, 1, 0, 1, 20, 1}, "Airstrike 5|Banana Bomb 5|Homing Missile 2|Super Sheep 3|Super Airstrike 6"},
    {"Strategy", {30, 0, 10, 30, 100, 40, 10, 10, 50, 25, 15, 10, 5, 0, 1, 3, 0, 3}, "Airstrike 6|Baseball Bat 2|Dynamite 4|Jetpack 4|Shotgun 3|Icarus Potion 4|Sniper Rifle 4|* 4"},
    {"Family", {90, 8, 10, 20, 125, 50, 30, 30, 20, 50, 15, 10, 8, 0, 1, 0, 0, 1}, ""},
    {"Mega Power", {30, 5, 10, 10, 200, 50, 40, 40, 20, 100, 15, 10, Scheme::FUSE_RANDOM, 1, 1, 1, Scheme::SET_UNLIMITED, 1, 30, 1}, "Concrete Donkey 8|Alien Abduction 8|Fatkins Strike 8|Sentry Gun 8|Super Airstrike 8"},
    {"Holy Grail", {45, 5, 10, 30, 100, 60, 30, 30, 30, 50, 15, 10, 1, 0, 1, 1, 0, 2}, "Concrete Donkey 16|Super Sheep 4"},
    {"Darksider", {45, 5, 10, 30, 100, 50, 10, 10, 50, 25, 15, 10, 5, 0, 1, 1, 0, 1}, "Airstrike 4|Cluster Grenade 2|Dynamite 2|Homing Missile 6|Landmine 2|Shotgun 2|Flood 2|Tail Nail 2|Poison Arrow 2|Sniper Rifle 2|Starburst 2|* 2"},
};

static constexpr float GRAVITY = 12.5f;  // W4M Gravity -0.00025 units/ms² (WEAPTWK), 20 units = 1 m
// WEAPTWK Payload.SinkSpeed.Min / Max 0.08 / 0.1 units/ms; Water.ExpiryDepth -200 units, absolute (Water.Level starts at 0)
static constexpr float SINK_MIN = 4, SINK_MAX = 5, WATER_EXPIRY = Terrain::WATER - 10;
// W4M FallDamage 0x5ac3e0: none up to 0.3 units/ms (15 m/s), then trunc((v - 0.3) x FallDamageRatio 100) + 1 = 2 hp per m/s
static constexpr float FALL_SAFE = 15, FALL_SCALE = 2;
// W4M Sliding 0x5afbe0 (TWEAK WXWorm.*_Default / _Slippy): cos SlideAngle 60 / 10; Start/StopSlideVel 0.2 / 0.06 and 0.01 units/ms;
// SlideFriction 0.95 / 0.999 per 20 ms frame, here per 1/60 s tick.
static constexpr float SLIDE_NY = 0.5f, SLIDE_FRICTION = 0.9582f, START_SLIDE = 10, STOP_SLIDE = 3;
// Wormpot Slippy (0x5d59c0): each WXWorm.*_Default moved halfway to *_Slippy: 35 deg, Start / StopSlideVel 0.105 / 0.035, friction 0.9745
static constexpr float SLIPPY_NY = 0.81915f, SLIPPY_FRICTION = 0.97870f, SLIPPY_START = 5.25f, SLIPPY_STOP = 1.75f;
static const float WIND_CAP[] = {0, 0.3f, 0.5f, 1};  // WindMaxStrength / 10: the schemes' 0, 3, 10 and the editor's Medium 5 (0x7539b0)

// W4M melee box: in front of the attacker, a worm height up or down; the Fire Punch (a leap) also reaches above.
bool meleeHits(const Worm &a, Vector3 p, const WeaponDef &wd) {
    Vector3 d = Vector3Subtract(p, a.pos);
    float ahead = d.x * sinf(a.yaw) + d.z * cosf(a.yaw), side = fabsf(d.x * cosf(a.yaw) - d.z * sinf(a.yaw));
    return ahead > -0.2f && ahead < 2 && side < 1 && d.y > -1.2f && d.y < (wd.fuse > 0 ? 3 : 1.2f);
}

static const Vector2 PROBE[4] = {{0, 0}, {0.2f, -0.15f}, {-0.2f, -0.15f}, {0, 0.25f}};  // W4M 0x91ffc8: centre and foot tripod, world axes
static constexpr float STANCE = 0.1f;  // a walking worm stands 0.1 to 1.1 units over its highest hit (`probe`): ground within 2 units carries it

// W4M land probe 0x91ffc8: the centre and the foot tripod (+-4, -3) (0, 5) units, world axes; any in land carries the worm.
// *n: the mean normal of the faces the feet's down rays (from 6 units up) enter, those level with the highest (W4M 0x59ef90)
static bool footing(const Terrain &t, Vector3 foot, Vector3 *n = nullptr) {
    float h[4], hb = -1e9f;
    Vector3 hn[4], sum{};
    for (int i = 0; i < 4; i++) {
        const Vector3 a = {foot.x + PROBE[i].x, foot.y + 0.3f, foot.z + PROBE[i].y};
        h[i] = -1e9f;
        if (!t.solid({a.x, foot.y, a.z})) continue;
        if (!n) return true;
        float th;
        if (t.cast(a, {0, -1, 0}, 0.3f, &th, &hn[i]) && th > 0) h[i] = a.y - th;
        else h[i] = a.y, hn[i] = {0, 1, 0};
        hb = fmaxf(hb, h[i]);
    }
    if (hb < -1e8f) return false;
    for (int i = 0; i < 4; i++)
        if (h[i] > -1e8f && hb - h[i] < 0.05f) sum = Vector3Add(sum, hn[i]);
    *n = Vector3LengthSqr(sum) > 1e-8f ? Vector3Normalize(sum) : Vector3{0, 1, 0};
    return true;
}

struct Feet { float d = -99; Vector3 n{0, 1, 0}; };
// W4M CastRays 0x59ec70 down the 4 foot rays from 20 units over the feet: d = 20 - the nearest hit in units (0x468490's float, its
// last empty sample; a ray starting in land: 20), under -5 none; n = 0x59ef90, the mean face of the hits within 1 unit of it
static Feet feet(const Terrain &t, Vector3 f) {
    const float U = 0.05f;
    Feet r;
    float h[4];
    Vector3 hn[4], sum{};
    for (int i = 0; i < 4; i++) {
        float th;
        h[i] = t.cast({f.x + PROBE[i].x, f.y + 20 * U, f.z + PROBE[i].y}, {0, -1, 0}, 26 * U, &th, &hn[i]) ? 20 - fmaxf(th - 1e-4f, 0) / U : -99;
        r.d = fmaxf(r.d, h[i]);
    }
    for (int i = 0; i < 4; i++)
        if (h[i] > -99 && r.d - h[i] < 1) sum = Vector3Add(sum, hn[i]);
    if (Vector3LengthSqr(sum) > 1e-8f) r.n = Vector3Normalize(sum);
    return r;
}

static bool rodsFit(const Terrain &t, Vector3 from, Vector3 to);

// W4M UpdateWalking 0x5b0da0, one step: the candidate is the nearest foot hit + 0.1 unit
bool walkStep(const Terrain &t, Vector3 &pos, float yaw, float dist, Vault *vault, Vector3 *ground) {
    const float R = Game::R, U = 0.05f;
    const Vector3 f = flat(yaw), np = Vector3Add(pos, Vector3Scale(f, dist));
    const Feet c = feet(t, {np.x, pos.y - R, np.z});
    Vector3 to = {np.x, pos.y + (c.d + 0.1f) * U, np.z};
    if (ground) *ground = {0, 1, 0};
    if (c.d > 5) {  // 0x5b1209: a ledge up to the 20-unit body vaults when walkable (0x4adda0) and Fits, else blocks
        if (c.n.y < SLIDE_NY || !rodsFit(t, pos, to)) return false;
        if (vault) { *vault = {pos, to, Vector3Scale(f, copysignf(1, dist)), {}, msTicks(250)}; return false; }  // 0x5b1285: no move this frame
        pos = to;
    } else if (c.d >= -5) {
        if (Vector3DotProduct(c.n, Vector3Subtract(to, pos)) < 0 && c.n.y < SLIDE_NY) return false;  // 0x5b1920: into the ground only if walkable
        for (int k = 0; k <= 4; k++)  // push-out +0..+4 units (0x5b194c)
            if (rodsFit(t, pos, {to.x, to.y + k * U, to.z})) {
                pos = {to.x, to.y + k * U, to.z};
                if (ground) *ground = c.n;
                break;
            }
    } else {  // 0x5b14c7: Fall from the old height when it Fits, else +1..+5 units (0x5b14e1)
        to.y = pos.y;
        if (rodsFit(t, pos, to)) { pos = to; return true; }
        for (int k = 1; k <= 5; k++)
            if (rodsFit(t, pos, {to.x, to.y + k * U, to.z})) { pos = {to.x, to.y + k * U, to.z}; break; }
    }
    return false;
}

// W4M Vaulting 0x5aca80: input along the start input or back to the old pos; pos = (4 pos + target) / 5 per 20 ms (0x5a59f0),
// no collision test, snapped there when the 250 ms run out (ChangeState 0x5aa847 snaps to the target on leaving the state)
void vaultStep(Vector3 &pos, Vault &v, Vector3 input) {
    if (Vector3DotProduct(input, v.dir) <= 0) pos = v.from, v.t = 0;
    else if (--v.t <= 0) pos = v.to;
    else pos = Vector3Lerp(pos, v.to, 1 - powf(0.8f, Game::DT / 0.02f));
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

int substeps(Vector3 vel) { return 1 + (int)(Vector3Length(vel) * Game::DT / Terrain::SUB); }

Vector3 launchPoint(const WeaponDef &d, Vector3 pos, float yaw) {
    float z = dropped(d) ? 13 : d.kind == Kind::Mine ? 10 : d.kind == Kind::Sheep || d.kind == Kind::SuperSheep ? 5 : d.kind == Kind::OldWoman ? 7 : d.kind == Kind::Scouser ? 10 : 0;
    float y = dropped(d) || d.kind == Kind::Mine ? -10 : 0;
    return {pos.x + sinf(yaw) * z / 20, pos.y - Game::R + Game::EYE + y / 20, pos.z + cosf(yaw) * z / 20};  // 20 units = 1 m
}

Vector3 muzzle(const Terrain &t, Vector3 pos, Vector3 spawn) {
    const Vector3 eye = {pos.x, pos.y - Game::R + Game::EYE, pos.z}, d = Vector3Subtract(spawn, eye);
    Vector3 p = eye;
    int n = 1 + (int)(Vector3Length(d) / Terrain::SUB);
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

static Vector3 probePoint(Vector3 pos, int i) { return {pos.x + PROBE[i % 4].x, pos.y - Game::R + (i < 4 ? 0 : 1.0f), pos.z + PROBE[i % 4].y}; }

// the 3 rods, feet to heads
static bool rodsClear(const Terrain &t, Vector3 p) {
    float th;
    for (int i = 1; i < 4; i++) if (t.cast(probePoint(p, i), {0, 1, 0}, 1.0f, &th, nullptr)) return false;
    return true;
}

// W4M Fits 0x59edf0 (walk, slide, flight): the 3 rods (PROBE 1..3, feet to heads) clear of land. [ours] rods already in land at
// `from` (land that appeared around the worm) fall back to the relative body test, so it can move out
static bool rodsFit(const Terrain &t, Vector3 from, Vector3 to) { return rodsClear(t, to) || (!rodsClear(t, from) && fits(t, from, to)); }

struct Sweep { int at = -1; float d = 0; Vector3 n{}; };

// W4M CastRays 0x59ec70: the 8 PROBE points (feet, heads 1 m up) along one tick's move m, each stopping on the air side of its land
// (0x468490's last empty sample); the earliest first, a foot on a tie; n = 0x59ef90, the mean face of the hits within 1 unit
static Sweep sweep(const Terrain &t, Vector3 pos, Vector3 m) {
    Sweep s;
    const float l = Vector3Length(m);
    if (l < 1e-6f) return s;
    const Vector3 dir = Vector3Scale(m, 1 / l);
    float d[8];
    Vector3 hn[8];
    for (int i = 0; i < 8; i++) {
        float th;
        d[i] = -1;
        if (!t.cast(probePoint(pos, i), dir, l, &th, &hn[i])) continue;  // 0x46a070: the face crossed
        if (!t.sharp.on && Vector3DotProduct(dir, hn[i]) >= 0) continue;  // [ours] a field's gradient, not a face the ray could enter
        d[i] = fmaxf(th - 1e-4f, 0);
        if (s.at < 0 || d[i] < s.d) s.at = i, s.d = d[i];
    }
    for (int i = 0; i < 8 && s.at >= 0; i++)
        if (d[i] >= 0 && d[i] - s.d < 0.05f) s.n = Vector3Add(s.n, hn[i]);
    if (s.at >= 0) s.n = Vector3LengthSqr(s.n) > 1e-8f ? Vector3Normalize(s.n) : Vector3Negate(dir);
    return s;
}

// W4M Rebound 0x5acea0 = Bounce 0x518f40(e, vt x1, 0.01 units/ms): v.n <= 0 keeps vt and turns vn into -e vn, else v = e |v| n; slower stops.
// Stopped on land facing up: true (W4M Sliding); facing down: v = (0, -0.01, 0) units/ms and one unchecked Integrate (0x5acfa5)
static bool rebound(Vector3 &pos, Vector3 &v, Vector3 n, float e, Vector3 acc) {
    const float vn = Vector3DotProduct(v, n), l = Vector3Length(v), MIN = 0.5f, DT = Game::DT;
    v = vn <= 0 ? Vector3Subtract(v, Vector3Scale(n, vn * (1 + e))) : Vector3Scale(n, l * e);
    if ((vn <= 0 ? Vector3Length(v) : l) < MIN) v = {0, 0, 0};
    if (v.x != 0 || v.z != 0 || v.y > 0) return false;
    if (n.y > 0) return true;
    v = {0, -MIN, 0};
    pos = Vector3Add(pos, Vector3Add(Vector3Scale(v, DT), Vector3Scale(acc, DT * DT / 2)));
    v = Vector3Add(v, Vector3Scale(acc, DT));
    return false;
}

enum { FLY_STUCK = 1, FLY_REBOUND = 2, FLY_LAND = 4, FLY_SLIDE = 8 };

// W4M Ballistic 0x5af430 on land, one tick: no hit Integrates; a hit moves to the contact and stores its normal (SupportNormal 0x5af5ba),
// then a foot on n.y >= 0.2 lands, a head or a wall Rebounds on n. Not Fitting there: stays put and Rebounds on normalize(pos - there)
static int flyBody(const Terrain &t, Vector3 &pos, Vector3 &vel, Vector3 acc, float e, Vector3 &support) {
    const float DT = Game::DT;
    const Vector3 m = Vector3Add(Vector3Scale(vel, DT), Vector3Scale(acc, DT * DT / 2));
    const Sweep s = sweep(t, pos, m);
    const Vector3 to = s.at < 0 ? Vector3Add(pos, m) : Vector3Add(pos, Vector3Scale(Vector3Normalize(m), s.d));
    if (s.at < 0) vel = Vector3Add(vel, Vector3Scale(acc, DT));  // 0x5af98f Integrate; a hit keeps the Velocity
    if (!rodsFit(t, pos, to)) {  // 0x5af81a / 0x5afb17; the contact at pos itself: v = -v (0x5af8cb)
        Vector3 back = Vector3Subtract(pos, to);
        if (Vector3LengthSqr(back) > 1e-12f) return FLY_STUCK | (rebound(pos, vel, Vector3Normalize(back), e, acc) ? FLY_SLIDE : FLY_REBOUND);
        if (s.at >= 0) vel = Vector3Negate(vel);
        return FLY_STUCK | FLY_REBOUND;
    }
    pos = to;
    if (s.at < 0) return 0;
    support = s.n;
    if (s.at >= 4 || s.n.y < 0.2f) return rebound(pos, vel, s.n, e, acc) ? FLY_SLIDE : FLY_REBOUND;  // 0x5af5e6 head, 0x5af5fb wall
    return FLY_LAND;
}

// W4M jetpack collider 0x59ec70 over one tick (the same sweep): a foot with the rods clear (Fits) lands the pack with v -= (v.n) n,
// whatever the normal; a head or blocked rods bounce v -= 1.8 (v.n) n (0x5630dc)
bool jetBody(const Terrain &t, Vector3 &pos, Vector3 &vel, float g) {
    const float DT = Game::DT;
    const bool still = Vector3LengthSqr(vel) == 0;  // [ours] at rest (our take-off tick, its thrust comes a tick later): neither swept nor moved
    vel.y -= g * DT / 2;
    const Sweep s = still ? Sweep{} : sweep(t, pos, Vector3Scale(vel, DT));
    bool landed = false;
    if (s.at >= 0) {
        const Vector3 from = pos;
        pos = Vector3Add(pos, Vector3Scale(Vector3Normalize(vel), s.d));
        landed = s.at < 4 && rodsFit(t, from, pos);
        vel = Vector3Subtract(vel, Vector3Scale(s.n, Vector3DotProduct(vel, s.n) * (landed ? 1 : 1 + Game::JET_BOUNCE)));
    } else if (!still) pos = Vector3Add(pos, Vector3Scale(vel, DT));
    vel.y -= g * DT / 2;
    return landed;
}

// W4M Sliding 0x5afbe0, one tick (K W4M frames); velocities are m/s, W4M's units/ms are 50 times smaller
static void slideStep(const Terrain &t, Vector3 &pos, Vector3 &vel, Motion &m, float &yaw, float g, uint64_t pot, float e) {
    const float R = Game::R, DT = Game::DT, U = 0.05f, K = DT / 0.02f;
    const bool slip = wpOn(pot, WP_SLIPPY);
    const float start = slip ? SLIPPY_START : START_SLIDE, stop = slip ? SLIPPY_STOP : STOP_SLIDE, walkable = slip ? SLIPPY_NY : SLIDE_NY;
    auto landed = [&] { vel = {0, 0, 0}, m.slide = false, m.stuck = 0; };  // kWE_Landed, Ambulatory; every Landed clears the count
    const Vector3 n = m.normal;  // SupportNormal, stored at the landing and on each ground follow
    m.spin += Clamp((m.spinTo - m.spin) / 4, -0.0349f, 0.0349f) * K, yaw += m.spin * K;  // 0x47a1a0(k 3, 2 deg), yaw += rate
    if (m.air && Vector3LengthSqr(m.input) > 0) {  // input along the motion and not uphill: x0.003, else x0.0005 units/ms per frame
        // 0x5afe30 / 0x5afe68: (g.n) n.xz . input > 0 (input uphill) gives 0.0005; else (g.n) n.xz . v > 0 (moving uphill) gives 0.003
        bool upIn = m.input.x * n.x + m.input.z * n.z <= 0, upV = vel.x * n.x + vel.z * n.z < 0;
        vel = Vector3Add(vel, Vector3Scale(m.input, (!upIn && upV ? 0.003f : 0.0005f) * 50 * K));
    }
    const float f = slip ? SLIPPY_FRICTION : SLIDE_FRICTION;  // Sticky changes no slide value (only ImpulseWorm, 0x5ad1ea)
    vel = {(vel.x + n.x * n.y * g * DT) * f, vel.y * f, (vel.z + n.z * n.y * g * DT) * f};  // gravity's slope part acts on x, z only
    Vector3 cand = Vector3Add(pos, Vector3Scale(vel, DT));
    const Feet c = feet(t, {cand.x, cand.y - R, cand.z});
    if (c.d > 5) {  // wall or step: slow lands, else one frame's ray along v rebounds it
        if (Vector3Length(vel) < start) return landed();
        const Sweep s = sweep(t, pos, Vector3Scale(vel, DT));  // 0x5b00e0: CastRays(pos, v, 20 steps, all 8 points)
        if (s.at < 0) return landed();
        m.spinTo = (m.spinTo + 3 * Vector3DotProduct(n, Vector3CrossProduct(s.n, Vector3Scale(vel, 1.0f / 50)))) / 2;
        rebound(pos, vel, s.n, e, {0, -g, 0});  // already Sliding: a stop on land facing up stays so
        m.air = false, m.stuck += STUCK_UP;
    } else if (c.d < -5) {  // a drop: the velocity off the ground, Fall() if the body Fits at the old height
        const Vector3 to = {cand.x, pos.y, cand.z};
        if (!rodsFit(t, pos, to)) return landed();
        pos = to, vel = Vector3Subtract(vel, Vector3Scale(n, Vector3DotProduct(vel, n))), m.slide = m.air = false;
    } else {  // follow the ground: the highest hit + 0.1 unit
        const Vector3 to = {cand.x, cand.y + (c.d + 0.1f) * U, cand.z}, nn = c.n;
        if (!rodsFit(t, pos, to)) return landed();
        pos = to, m.stuck = std::max(m.stuck - STUCK_DOWN, 0);
        m.spinTo -= 2 * Vector3DotProduct(Vector3CrossProduct(nn, n), Vector3Scale(vel, 1.0f / 50));
        m.normal = nn;  // 0x5b04a1
        if (nn.y >= walkable && Vector3LengthSqr(vel) < stop * stop) return landed();
    }
    if (m.stuck >= STUCK_MAX) landed();
}

void slideIfSteep(Vector3 n, Vector3 &vel, Motion &m, Vector3 walk, uint64_t pot) {
    if (!m.slide && n.y < (wpOn(pot, WP_SLIPPY) ? SLIPPY_NY : SLIDE_NY))
        vel = walk, m.slide = true, m.spin = m.spinTo = 0, m.normal = n;  // event 17 (SupportNormal 0x5b1998); ChangeState zeroes the spin (0x5aaa0c)
}

float wormBody(const Terrain &t, Vector3 &pos, Vector3 &vel, bool &grounded, Motion &m, float &yaw, float g, uint64_t pot, float e, Vector2 wind) {
    const float R = Game::R;
    bool was = grounded, slip = wpOn(pot, WP_SLIPPY);
    if (m.slide) {
        slideStep(t, pos, vel, m, yaw, g, pot, e);
        grounded = m.slide || Vector3LengthSqr(vel) == 0;
        return 0;
    }
    float landing = 0;
    // W4M Ballistic landing (0x5af601) on n, or ImpulseWorm 0x5ad010 into the ground: vt walks or slides, FallDamage takes vn
    auto land = [&](Vector3 n, bool fall) {
        float vn = Vector3DotProduct(vel, n), start = slip ? SLIPPY_START : START_SLIDE;
        if (fall) landing = -vn;
        vel = Vector3Subtract(vel, Vector3Scale(n, vn));  // vt
        float v2 = Vector3LengthSqr(vel);
        if (fall && v2 < vn * vn) v2 *= 0.5f;  // 0x5af6be: |vt|² halved when below vn²
        if (n.y >= (slip ? SLIPPY_NY : SLIDE_NY) && v2 < start * start) vel = {0, 0, 0};  // lands walking
        else m.slide = true, m.spin = m.spinTo = 0;  // event 17, Sliding with Velocity = vt
        m.normal = n;  // SupportNormal 0x5af5ba
    };
    Vector3 n{0, 1, 0};
    // a flight lands through its sweep (0x5af430); [ours] a worm put down at rest (spawn, teleport) stands where it is
    grounded = vel.y <= 0 && (was || Vector3LengthSqr(vel) == 0) && footing(t, {pos.x, pos.y - R - STANCE, pos.z}, &n);
    if (grounded && was && Vector3LengthSqr(vel) > 0 && Vector3DotProduct(vel, n) >= 0) grounded = false;  // ImpulseWorm: not into the ground, Ballistic
    // [ours] idle and no fall Fits: W4M would Rebound to a stop, Slide, land with support 0xFFFF and stay (0x5b1a3e); ours stays at once
    if (!grounded && was && Vector3LengthSqr(vel) == 0 && !rodsFit(t, pos, {pos.x, pos.y - 0.05f, pos.z})) grounded = true;
    if (grounded) {
        if (!was || Vector3LengthSqr(vel) > 0) land(n, !was);
        else vel = {0, 0, 0};
    } else {
        int c = flyBody(t, pos, vel, {wind.x, -g, wind.y}, e, m.normal);
        m.stuck = c & FLY_STUCK ? m.stuck + STUCK_UP : std::max(m.stuck - STUCK_DOWN, 0);
        if ((c & FLY_STUCK) && m.stuck >= STUCK_MAX) vel = {0, 0, 0}, grounded = true;  // W4M 0x5af821: landed where it is, the count kept
        else if (c & FLY_LAND) grounded = true, land(m.normal, true);
        else if (c & FLY_SLIDE) grounded = m.slide = true, m.spin = m.spinTo = 0, m.air = false;  // Rebound stopped on land: event 8, Sliding
        else if (c & FLY_REBOUND) m.air = false;
    }
    return landing;
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

// 0x4f64f0, Init 0x5d0170: onto the land below (1000 steps of 1 unit), Land maxY kept (+0x28)
void Game::factoryCreate(Vector3 p) {
    Vector3 hit;
    if (terrain.raycast({p, {0, -1, 0}}, 50, &hit)) p = hit;
    factory = {p, true};
    factory.wait = factoryData.inactive, factory.top = landTop();
    const float U = 1 / 20.0f;  // Land.SpawnPiece MineFacCollisionSmall / Big: boxes centred on pos + their frame's Position (Bundl09)
    weldLand(Vector3Add(p, {18.96f * U, 15.96f * U, 1.08f * U}), {10.5f * U, 16 * U, 8.4f * U});
    weldLand(Vector3Add(p, {-5.28f * U, 20.76f * U, 0.96f * U}), {12 * U, 20 * U, 12 * U});
}

void Game::factoryStart() {
    Factory &f = factory;
    if (!f.on || f.state || --f.wait > 0) return;  // below 0 it retries every call
    int mines = 0;  // every kWeaponLandmine payload in play (GLS +0x1d8)
    for (const Object &o : objects) mines += o.type == Object::Mine;
    if ((uint8_t)mines >= (uint8_t)factoryData.activation) return;
    f.state = 1, f.toSpawn = std::min(factoryData.activation - mines, 10), f.wait = factoryData.inactive;  // the "Mine Factory" active object
}

// 0x5d0b00: MineFactory.Start, after the MineFactoryStart clip (2 s) Fire, after FireStart (291 ms) FireEnd, after FireEnd (708 ms) one
// more update, then PlaceMines 0x5d05c0. An Explosion within its LandDamageRadius + 40 units of pos + (0, 40, 0) (0x5cfbc0), or water
// over pos + 10 units, blows it up 100 ms later (0x5cfc70)
void Game::stepFactory() {
    Factory &f = factory;
    if (!f.on || phase == Phase::GameOver) return;
    const float U = 1 / 20.0f;
    const FactoryData &d = factoryData;
    if (f.damaged ? clock > f.die : f.pos.y + 10 * U < water) {
        f.on = false;
        explode(f.pos, {d.crater * U, d.reach * U, d.damage, d.push * 1000 * U, d.pushReach * U, 5 * U}, 0, 0, -1, "WXP_Explosion_MineMachine");
        return;
    }
    if (f.state == 1) f.until = clock + msTicks(2000), f.state = 2;
    else if (f.state >= 2 && f.state <= 4 && clock > f.until) f.until = clock + msTicks(f.state == 2 ? 291 : 708), f.state++;
    else if (f.state == 5) {  // around a random worm, 128 units out, dropped at Land maxY + 10 units if the land under it is above the water, flat
        std::vector<Vector3> avoid;  // (normal y >= 0.9) and 127 units (ArmingRadius 45 + SafeRadiusPadding) from every worm in xz
        for (const Worm &w : worms) if (w.alive) avoid.push_back(w.pos);
        float r = (45 + d.padding) * U;
        for (int k = 0; k < 35 && f.toSpawn > 0 && !avoid.empty(); k++) {
            Vector3 b = avoid[std::min((int)(rand01() * avoid.size()), (int)avoid.size() - 1)], hit;
            float a = rand01() * 2 * PI;
            Vector3 p = {b.x + sinf(a) * (r + U), f.top + 10 * U, b.z + cosf(a) * (r + U)};
            if (!terrain.raycast({p, {0, -1, 0}}, p.y + 1, &hit) || hit.y <= water || terrain.normal(hit).y < 0.9f) continue;
            bool clear = true;
            for (const Vector3 &e : avoid) clear = clear && (hit.x - e.x) * (hit.x - e.x) + (hit.z - e.z) * (hit.z - e.z) >= r * r;
            if (!clear) continue;
            Object m = newMine(p);  // 0x4f9c40: at p, falling at 0.3 units/ms
            m.vel = {0, -0.3f * 1000 * U, 0};
            objects.push_back(m), f.toSpawn--;
        }
        f.toSpawn = 0, f.state = 0;
    }
}

// CreateMine 0x4f981c: Min + trunc((Max - Min) r) ms
float Game::mineFuse() { return (mineMin + (int)((mineMax - mineMin) * rand01())) / 1000.0f; }

// The dud roll (none with Mine Respawn, 0x4f97c9), then the fuse; StartsMidAir 0 only skips the ArielFx (0x581978)
Object Game::newMine(Vector3 p) {
    Object o = {Object::Mine, p, {0, 0, 0}, -1, -1, false, false};
    o.fizzle = !wp(WP_MINE_RESPAWN) && rand01() < mineDud;
    o.delay = mineFuse();
    return o;
}

// Detonate 0x581113: Mine.DetonationType 1..4 forced; -1 and 0 (kWeaponLandmine DetonateMultiEffect 0, kDT_Random): (rand & 3) + 1
int Game::mineType() { return mineDet >= DT_NORMAL && mineDet <= DT_BIGPUSH ? mineDet : (int)(rand01() * 4) + 1; }

// Fire: WXP_Napalm for DetonationFx (0x5813c4); Clusters: kWeaponLandmineCluster's payload (0x581236), not Wormpot-scaled, kind 3
// (0x57f35e), then its bomblets; BigPush: Explode 0x57f1c6 doubles the impulse and its radius, x0.3 damage, reach and crater
void Game::mineBlast(Vector3 p, int type) {
    if (type == DT_CLUSTERS) {
        explode(p, MINE_CLUSTER_BLAST, 0, 3, -1, "WXP_ExplosionX_Med");
        for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == Kind::Mine && !customWeapon((int)i)) { bomblets(p, (int)i, shots); break; }
        return;
    }
    Blast b = superBlast(MINE_BLAST, W4M_LANDMINE);
    if (type == DT_BIGPUSH) b.push *= 2, b.pushReach *= 2, b.damage *= 0.3f, b.reach *= 0.3f, b.crater *= 0.3f;
    explode(p, b, 0, 0, -1, type == DT_FIRE ? "WXP_Napalm" : "WXP_Explosion_Mine");
}

// 0x5519d0, one per 20 ms update from the blast point: azimuth 2 pi r, tilt from up ConeAngle r, speed Min + (Max - Min) r; the
// cone axis stays up because an expiry Detonate passes a zero normal (0x57fb89); drawn here, W4M draws each at its spawn
void Game::bomblets(Vector3 at, int weapon, std::vector<Projectile> &out) {
    const WeaponDef &wd = weaponDef(weapon);
    for (int k = 0; k < wd.clusters; k++) {
        float az = rand01() * 2 * PI, tilt = rand01() * wd.ccone, v = wd.cspeed[0] + (wd.cspeed[1] - wd.cspeed[0]) * rand01();
        Projectile b = {at, {sinf(tilt) * sinf(az) * v, cosf(tilt) * v, sinf(tilt) * cosf(az) * v}, weapon, 0, true, 1};
        b.stage = msTicks(20 * k);  // held at the blast until then
        out.push_back(b);
    }
}

float Game::rand01() {
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / 16777216.0f;
}

float Game::gravity() const { return GRAVITY * ((cfg.rules & RULE_LOW_GRAVITY) || mysteryLow ? 0.5f : 1.0f); }  // W4M Low.Gravity.OnValue 0.5 (TWEAK)

Vector3 Game::aimDir(const Worm &w) const {  // a gun in hand: + GunWobblePitch / Yaw (Fire 0x55e1d8, HeadCam 0x528eb5)
    bool sway = &w == &worms[current] && wobble.weapon == weapon && wobble.weapon >= 0;
    return dirOf(w.yaw + (sway ? wobble.at.y : 0), w.pitch + (sway ? wobble.at.x : 0));
}

// GunWobble.MaxAmp 0.03, Period 5000 ms, Speed 0.004 (WEAPTWK); Wind Affects Guns / All: f = 1 + 2.5 Wind.Speed / Wind.MaxSpeed, sampled once
void Game::wobbleStep(float zoom) {
    if (weaponDef(weapon).kind != Kind::Shotgun || (phase != Phase::Aim && !shotsLeft)) { wobble.weapon = -1; return; }
    Wobble &o = wobble;
    if (o.weapon != weapon) {
        o.weapon = weapon, o.start = clock;
        o.f = wp(WP_WIND_GUNS) || wp(WP_WIND_ALL) ? 1 + 2.5f * sqrtf(wind * wind + windZ * windZ) : 1;
        for (int i = 0; i < 8; i++) o.w[i] = rand01(), o.phase[i] = rand01() * 3.14159265f, o.freq[i] = rand01() * 0.004f * o.f;
    }
    float t = (clock - o.start) * 1000.0f / 60, amp = 0.03f * cosf(t / 5000) * o.f, p = 0, y = 0;
    for (int i = 0; i < 4; i++) p += sinf(o.freq[i] * t + o.phase[i]) * o.w[i], y += sinf(o.freq[i + 4] * t + o.phase[i + 4]) * o.w[i + 4];
    float z = fminf(1.5f * zoom, 1);  // 0x55fbed: the current logical camera's zoom, sent in Input::zoom
    o.at = {amp * p * z, amp * y * z};
}

// Wormpot.lub SetSpecialistTeam: the ammo of each set, and the sets of each worm class
static const std::pair<const char *, int> SPEC_AMMO[4][7] = {
    {{"Shotgun", -1}, {"Airstrike", 1}, {"Landmine", 2}, {"Fire Punch", -1}, {"Prod", -1}},
    {{"Ninja Rope", 5}, {"Girder", 3}, {"Dynamite", 1}, {"Parachute", 2}, {"Baseball Bat", 1}, {"Sheep", 1}, {"Teleport", 2}},
    {{"Bazooka", -1}, {"Homing Missile", 1}}, {{"Grenade", -1}, {"Cluster Grenade", 3}}};
static const int SPEC_SETS[7] = {0, 4 | 8, 1 | 2, 1, 2, 4, 8};

bool Game::allowed(int team, int wi) const {
    if (current < 0 || current >= (int)worms.size() || worms[current].team != team) return true;
    const std::string &n = WEAPONS[wi].name;
    if (artillery() && WEAPONS[wi].kind == Kind::Jetpack) return false;  // AllowJetpack 0
    int c = current < (int)special.size() ? special[current] : 0;
    if (!c || n == "Skip Go" || n == "Surrender" || n == "Teleport" || n == "Binoculars") return true;  // DisallowAllWeapons leaves these
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 7 && (SPEC_SETS[c] >> k & 1) && SPEC_AMMO[k][j].first; j++) if (n == SPEC_AMMO[k][j].first) return true;
    return false;
}

// The load's Land.MaxHeight: the highest solid voxel on a column every 2 m (ours: W4M takes its land frames' box)
static float scanTop(const Terrain &t) {
    const int s = (int)(2 * Terrain::IVOX);
    for (int y = Terrain::NY - 1; y > 0; y--)
        for (int z = 0; z < Terrain::NZ; z += s)
            for (int x = 0; x < Terrain::NX; x += s)
                if (t.at(x, y, z) > 0) return y * Terrain::VOX;
    return 0;
}

void Game::start(const GameConfig &c) {
    cfg = c;
    WEAPONS.resize(std::min(WEAPONS.size(), baseWeapons));
    WEAPONS.insert(WEAPONS.end(), c.custom.begin(), c.custom.end());
    for (size_t i = baseWeapons; i < WEAPONS.size(); i++)  // WEAPTWK kWeaponFactoryWeapon PostLaunchDelay 500, kWeaponFactoryHoming 0; not on the wire, so set here
        WEAPONS[i].postLaunch = WEAPONS[i].kind == Kind::Homing ? 0 : 500, WEAPONS[i].retreat = -1,  // water: Radius 5, SinkDepth 5, FactoryCluster 4 / 5 units
        WEAPONS[i].size = WEAPONS[i].sink = WEAPONS[i].csink = 0.25f, WEAPONS[i].csize = 0.2f,
        WEAPONS[i].skim[0] = WEAPONS[i].kind == Kind::Homing ? -1 : 5, WEAPONS[i].skim[1] = -0.4f, WEAPONS[i].skim[2] = 0.45f, WEAPONS[i].skim[3] = -0.3f,
        // 0x5995dd: cone 0.28, speeds k/7 .. k/2 units/ms, k = 0.5 ClusterSpread + 0.3
        WEAPONS[i].ccone = 0.28f, WEAPONS[i].cspeed[0] = (0.5f * WEAPONS[i].spread + 0.3f) / 7 * 50, WEAPONS[i].cspeed[1] = (0.5f * WEAPONS[i].spread + 0.3f) / 2 * 50;
    // Wormpot modes that are existing rules or scheme values: set them on our copy, which the checksum covers
    pot = wormpotModes(c.wormpot);
    if (wp(WP_LOW_GRAVITY)) cfg.rules |= RULE_LOW_GRAVITY;
    if (wp(WP_VITAL_WORM)) cfg.rules |= RULE_KING;  // flag 0x100 on each team's first worm (0x5d6970)
    terrain.load(c.map, c.seed);
    landMax = scanTop(terrain);
    rng = c.seed * 2654435761u + 1;
    teams = c.teams;
    perTeam = c.wormsPerTeam;
    const int per = perTeam;
    worms.clear();
    shots.clear();
    objects.clear(), triggers.clear(), allied.clear(), noTurn = false;
    factory = {}, factoryData = {};
    gas.clear();
    nextWorm.assign(teams, 0);
    winner = -1;
    weapon = -1;
    prevButtons = 0, cursorOn = locked = false;
    water = Terrain::WATER;
    bool anyFuse = cfg.scheme.mineFuse == Scheme::FUSE_RANDOM;  // lib_SetupMinesAndOildrums: MineFuse -1 -> 0..5000 ms
    mineMin = anyFuse ? 0 : cfg.scheme.mineFuse * 1000, mineMax = anyFuse ? 5000 : cfg.scheme.mineFuse * 1000, mineDud = MINE_DUD, mineDet = 0;
    for (const WeaponDef &d : WEAPONS) if (d.kind == Kind::Jetpack) jetInit = d.fuse;
    endlessGun = false;
    clock = hotSeat = jumpDelay = ropeShots = 0, rope = {};
    vault = {}, walkVel = {};
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
    if (wp(WP_MAX_HEALTH)) cfg.scheme.crateHealth = 100;  // after the preset match: the scheme's delays stay
    if (wp(WP_ONE_SHOT)) cfg.scheme.health = 1;
    countGroup.clear(), deathQueue.clear(), countT = throes = camHold = 0, dyingWorm = -1, crated = false;
    respawns.clear(), wobble = {}, mysteryWalk = mysteryLow = false;
    girderOn = false, girderWait = girders = 0, bubbles.clear(), bubbleAt = -1, icarus = flapAt = 0, drift = {}, doubleDamage = changing = false;
    roped = jetting = jetUsed = chute = false, fuel = jetInit, boost = 0, secondary = launched = -1;
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
    superWeapon.assign(teams, 0);  // SetupModes runs before the Lua's inventory changes
    for (int t = 0; t < teams && wp(WP_SECRET_WEAPON); t++) {  // 0x5d65c0: rand() % 30 until the team holds it, if it holds any kWeapon
        auto holds = [&](int id) { for (size_t i = 0; i < WEAPONS.size(); i++) if (ammo[t][i] && containerOf((int)i, false) == id) return true; return false; };
        bool any = false;
        for (int id = 1; id < 32; id++) any |= holds(id);
        while (any && !holds(superWeapon[t] = (int)(rand01() * 30))) {}
    }
    if (wp(WP_CRATE_DROPS))  // GameLogic.ClearInventories 0x4fef78
        for (auto &a : ammo) for (size_t i = 0; i < WEAPONS.size(); i++) a[i] = WEAPONS[i].kind == Kind::SkipGo || WEAPONS[i].kind == Kind::Surrender ? -1 : 0;
    if (wp(WP_NO_COWARDS)) {  // Wormpot.lub NoRetreatTime: DefaultRetreatTime 0 (weapon overrides stay), no Surrender
        cfg.scheme.retreatTime = 0;
        for (auto &a : ammo) for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == Kind::Surrender) a[i] = 0;
    }
    const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
    const auto navT0 = std::chrono::steady_clock::now();
    const Grid grid = makeGrid(terrain);
    NodeCache nodes;
    for (int t = 0; t < teams; t++)
        for (int k = 0; k < per; k++) {
            Worm w = {{cx, (Terrain::NY - 1) * Terrain::VOX, cz}, {0, 0, 0}, 0, 0.3f, std::max(1, (int)cfg.scheme.health), t, true, false};
            Vector3 c = placeWorm(grid, nodes, w.yaw);
            w.pos = {c.x, c.y + R, c.z};
            if (wp(WP_GOLIATH)) w.hp = k ? 50 : 50 * (per + 1);  // SetDavidAndGolithHealth(100 n, n): Davids 100 n / 2n, the first the rest
            if (wp(WP_ENERGY)) w.poison = POISON_DEFAULT;  // Wormpot.lub EnergyOrEnemy: PoisonRate = Worm.Poison.Default, then Worm.Poison
            worms.push_back(w);
        }
    terrain.loadMs[2] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - navT0).count();
    if (!(cfg.rules & RULE_ROPE_RACE)) {
        for (int i = 0; i < cfg.scheme.mines; i++) addObject(Object::Mine, 0);
        for (int i = 0; i < cfg.scheme.barrels; i++) addObject(Object::Barrel, 0);
    }

    if (cfg.rules & RULE_ROPE_RACE) {
        raceFinish = terrain.finish;
        if (!terrain.hasFinish) {
            // fallback: highest point on a coarse grid, biased away from spawns
            float best = -1e9f;
            const int s = (int)(2 * Terrain::IVOX);  // 2 m
            for (int z = s / 2; z < Terrain::NZ - s / 2; z += s)
                for (int x = s / 2; x < Terrain::NX - s / 2; x += s) {
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
    surrendered.assign(teams, 0);
    special.assign(worms.size(), 0), wormCpu.assign(worms.size(), 0);
    if ((cfg.rules & RULE_SUDDEN_DEATH) && cfg.scheme.roundTime == 0) {  // stdvs Initialise: RoundTime 0 starts sudden death
        suddenDeath = true;
        if (cfg.scheme.sdType == Scheme::SD_ONE_HP) for (Worm &x : worms) x.hp = x.counted = 1;
        if (cfg.scheme.sdType == Scheme::SD_DRAW) { phase = Phase::GameOver, winner = -1; return; }
    }
    Vector3 fp;  // stdvs Initialise: MineFactoryOn -> CreateRandMineFactory (0x4fe18e), a 40-unit sphere 40 up clear on the ground
    if (cfg.scheme.mineFactory && !cfg.mission && !(cfg.rules & RULE_ROPE_RACE) && dropPoint(Object::Mine, fp, 40 / 20.0f)) factoryCreate(fp);
    if (wp(WP_SPECIALIST) && per > 1) {  // Wormpot.lub SetSpecialistTeam: a class per worm by team size and place, the team's ammo set
        static const uint8_t CLASS[7][6] = {{}, {}, {1, 2}, {1, 3, 4}, {5, 6, 3, 4}, {5, 6, 3, 4, 6}, {5, 6, 3, 4, 6, 3}};
        for (size_t i = 0; i < worms.size(); i++) {
            int c = special[i] = CLASS[std::min(per, 6)][i % per];
            for (int k = 0; k < 4; k++)
                for (int j = 0; j < 7 && (SPEC_SETS[c] >> k & 1) && SPEC_AMMO[k][j].first; j++)
                    for (size_t x = 0; x < WEAPONS.size(); x++) if (WEAPONS[x].name == SPEC_AMMO[k][j].first) ammo[worms[i].team][x] = SPEC_AMMO[k][j].second;
        }
        for (size_t x = 0; x < WEAPONS.size(); x++)  // Inventory.WeaponDelays.Default: HomingMissile 1, Airstrike 5, copied to every team
            for (auto &d : delays) if (WEAPONS[x].name == "Homing Missile" || WEAPONS[x].name == "Airstrike") d[x] = WEAPONS[x].name == "Airstrike" ? 5 : 1;
    }
    if (!(cfg.rules & RULE_ROPE_RACE)) {  // SetWormpotModes: CratesOnly and LotsOfCrates each send a CrateShower
        if (wp(WP_CRATE_DROPS)) dropCrates(6, false);
        if (wp(WP_CRATE_SHOWER)) dropCrates(6, false);
    }
    run = MissionRun{}, script.reset(), indestructible = false;
    if (cfg.mission) missionStart(*this);
    for (Worm &x : worms) x.counted = std::max(0, x.hp);
    if (!script) beginTurn(teams - 1);  // a script starts its first turn itself (stdlib StartFirstTurn)
}

void Game::beginTurn(int team) {
    ropeCleanup(), grapple.on = false;
    for (size_t i = 0; i < bubbles.size();) if (--bubbles[i].life <= 0) emit(GameEvent::BubblePop, bubbles[i].pos), bubbles.erase(bubbles.begin() + i); else i++;  // GameLogic.Turn.Ended
    girderOn = false, icarus = 0, bubbleAt = -1, drift = {}, doubleDamage = changing = false, scout.t = -1, walkVel = {};  // DoPostActivity: SetData("DoubleDamage", 0)
    crated = false;
    wobble.weapon = -1;  // the gun entity is made anew each turn
    mysteryWalk = mysteryLow = false;  // GameLogic.Turn.Ended: Worm.VelocityScale 1 (0x4f59f0), Low.Gravity.GameDefault (0x4f24f0)
    std::vector<bool> has(teams, false);
    int alive = 0, last = -1;
    for (const Worm &w : worms)
        if (w.alive && !has[w.team] && !surrendered[w.team]) { has[w.team] = true; alive++; last = w.team; }  // GetActiveAlliances
    if (alive <= 1 && !cfg.mission) {  // missions end through their objectives
        phase = Phase::GameOver;
        winner = alive ? last : -1;
        emit(GameEvent::GameOver, {0, 0, 0});
        return;
    }
    const Scheme &sc = cfg.scheme;
    for (int i = 1; i <= teams; i++) {
        int t = (team + i) % teams;
        if (!has[t]) continue;
        for (int k = 0; k < perTeam; k++) {
            int slot = (nextWorm[t] + k) % perTeam, c = t * perTeam + slot;
            if (!worms[c].alive || !worms[c].turns) continue;
            current = c;
            nextWorm[t] = (slot + 1) % perTeam;
            phase = Phase::Aim;
            timer = std::max(1, (int)sc.turnTime) * 60;
            hotSeat = msTicks(sc.hotSeat * 1000);
            jumpDelay = 0;
            selfHurt = false;
            power = 0;
            if (!script) {  // W4M stdlib SelectRandomWind (a script runs its own SetWind): Speed = Cap/10 x r² x MaxSpeed, Direction = r2 x 2 x 3.14, an xz vector (0x57eb25)
                float r = rand01(), sp = WIND_CAP[std::min<int>(sc.wind, 3)] * r * r, dir = rand01() * 2 * 3.14f;
                wind = sp * cosf(dir), windZ = sp * sinf(dir);
            }
            roped = jetting = jetUsed = chute = cursorOn = blimp = locked = false, fuel = jetInit, boost = 0, secondary = launched = -1;
            for (Object &o : objects) o.hooked = false;
            shotsLeft = ropeShots = 0, rope.swung = false;
            weapon = -1;  // W4M GameLogic.Turn.Started (0x566d57): kWeaponUndefined, the empty hand
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

// W4M WeaponSelected 0x565d30: with a tool out (+0x8d, kept by a landed jetpack) a toolDrop() becomes the secondary (0x566310);
// anything else, or a toolDrop() under No Bombing (0x5662f6), ends the tool (0x565650) and is wielded
void Game::pick(int team, int k) {
    bool tool = toolOut(), keep = tool || jetLanded();
    if (icarus == 3) return;  // W4M Redbull: Weapon.DisableWeaponChange until the drink is done
    if (keep && k == weapon) { secondary = -1; return; }
    if (k < 0 || k >= (int)WEAPONS.size() || k == held() || !usable(team, k)) return;
    if (keep && !wp(WP_NO_BOMBING) && toolDrop(WEAPONS[k])) { secondary = k; return; }
    if (tool) roped = jetting = chute = false;
    for (Object &o : objects) o.hooked &= !tool;
    weapon = k, secondary = -1, jetUsed = false, fuel = jetInit;  // a new jetpack on the ground (new W4M entity)
}

// Y / R: the next selectable weapon (ours); under a tool only the tool and its toolDrop()s, so a step never ends it
void Game::nextWeapon(int team) {
    for (int i = 1; i <= (int)WEAPONS.size(); i++) {
        int k = (held() + i) % WEAPONS.size();
        if (selectable(team, k)) return pick(team, k);
    }
}

// Mine: W4M kWeaponLandmine Radius 3 units, the mesh drawn that far over the land (0x5761f0); the others: our meshes' half heights
// the point a hooked object hangs by: a crate's centre is 10 units above it (0x5cbc86), a drum's collider 9 (0x5d2135)
static Vector3 ropePoint(const Object &o) { return {o.pos.x, o.pos.y - (o.type == Object::Crate ? 0.5f : o.type == Object::Barrel ? 0.45f : 0), o.pos.z}; }
static float halfHeight(Object::Type t) { return t == Object::Mine ? 0.15f : t == Object::Barrel || t == Object::Sentry ? 0.5f : 0.45f; }
// W4M effect height over Water.Level: it quantised to 1/16 unit with the low bit set (0x48b260), plus off units (drown, death blast 1; payload 2)
static float surfaceY(float water, float off) { return Terrain::WATER + (((int)((water - Terrain::WATER) * 20 * 16) | 1) / 16.0f + off) / 20; }

static bool crateLike(const Object &o) { return o.type == Object::Crate || o.type == Object::Target; }  // W4M CrateLogicEntity: a target is crate type 3
static bool sheepLike(Kind k) { return k == Kind::Sheep || k == Kind::SuperSheep || k == Kind::OldWoman || k == Kind::Scouser; }  // WEAPTWK ColliderFlags 128: collider 0x88

// W4M PlaceWormAtSpawnPoint (0x5b4180, Spawn "spawn"): a random yaw, then up to 1000 random walkable cells (0x4ae810), each
// at its node + (0, 10, ZOffset -5) turned by the yaw + 10 units up; a point whose sphere (10 units) is free of worms counts, the
// highest of 3 counted wins; none: Land.Center +-50 units at Land.MaxHeight + 10 units. Returns the worm's logical position (feet).
Vector3 Game::placeWorm(const Grid &gr, NodeCache &nc, float &yaw) {
    const float U = 0.05f;
    rand01();  // 0x5b41a8: the debug log's rand(), drawn whether it logs or not
    yaw = (int)(rand01() * 256) * (2 * PI / 256);  // rand() & 0xff
    const Vector3 off = {-sinf(yaw) * 5 * U, 20 * U, -cosf(yaw) * 5 * U};
    float area = 0;
    for (const Grid::Box &b : gr.boxes) area += (b.b.z - b.b.x) * (b.b.w - b.b.y);
    // AISceneGraphService 0x4ae810: 500 draws of a box by area, a node in it, a layer; the first walkable one
    auto cell = [&](int &i, int &j, int &l) {
        for (int k = 0; k < 500; k++) {
            float r = rand01() * area;
            const Grid::Box *b = &gr.boxes.back();
            for (const Grid::Box &x : gr.boxes)
                if ((r -= (x.b.z - x.b.x) * (x.b.w - x.b.y)) <= 0) { b = &x; break; }
            i = std::min(b->i0 + (int)(rand01() * (b->i1 - b->i0)), b->i1 - 1);
            j = std::min(b->j0 + (int)(rand01() * (b->j1 - b->j0)), b->j1 - 1);
            l = (int)(rand01() * 2);
            if (nodeH(terrain, water, gr, nc, i, j, l).flag == 0) return true;
        }
        return false;
    };
    Vector3 best = {0, -1e4f, 0};
    int found = 0, i, j, l;
    for (int tries = 0; tries < 1000 && found < 3 && cell(i, j, l); tries++) {
        const NodeH h = nodeH(terrain, water, gr, nc, i, j, l);
        const Vector2 p = gr.at(i, j);
        const Vector3 c = Vector3Add({p.x, (h.lo + h.hi) / 2, p.y}, off);  // node position 0x4aeb00: mid-height
        bool clear = true;
        for (const Worm &o : worms) clear = clear && Vector3Distance({o.pos.x, o.pos.y - R + 5 * U, o.pos.z}, c) >= 20 * U;
        if (!clear) continue;
        found++;
        if (c.y > best.y) best = c;
    }
    if (!found) {
        best = {Terrain::NX * Terrain::VOX / 2 + (rand01() - 0.5f) * 100 * U, landTop() + 10 * U, 0};
        best.z = Terrain::NZ * Terrain::VOX / 2 + (rand01() - 0.5f) * 100 * U;
    }
    return best;
}

// W4M random spot (crates 0x5c6560, mines and drums 0x4f26b0): x, z uniform over the land's box, a ray down from its top to land
// above Water.Level, 100 tries. A crate's column must miss every worm sphere (10 units, 5 above the feet); a mine or drum
// sphere (its radius + 5 units, on the ground) must overlap no worm or object. All tries failed: a crate spawns at the box top.
bool Game::dropPoint(Object::Type t, Vector3 &out, float radius) {
    const float U = 0.05f, top = landTop();
    auto rad = [](Object::Type k) { return k == Object::Mine ? 0.15f : k == Object::Barrel ? 0.45f : k == Object::Crate ? 0.5f : halfHeight(k); };  // Landmine Radius 3, drum 9, crate 10 units
    const float r = radius >= 0 ? radius : rad(t);
    for (int tries = 0; tries < 100; tries++) {
        Vector3 hit, from = {rand01() * Terrain::NX * Terrain::VOX, top, rand01() * Terrain::NZ * Terrain::VOX};
        out = from;
        if (!terrain.raycast({from, {0, -1, 0}}, top + 1, &hit) || hit.y < water) continue;
        bool clear = true;
        for (const Worm &w : worms) {
            Vector3 c = {w.pos.x, w.pos.y - R + 5 * U, w.pos.z};  // the worm collider (0x5a9ac0)
            if (!w.alive) continue;
            if (t == Object::Crate) clear = clear && Vector3Distance(c, {hit.x, Clamp(c.y, hit.y + 10 * U, top), hit.z}) >= 20 * U;
            else clear = clear && Vector3Distance(c, {hit.x, hit.y + r, hit.z}) >= 10 * U + r + 5 * U;
        }
        for (const Object &o : objects) clear = clear && (t == Object::Crate || Vector3Distance(o.pos, {hit.x, hit.y + r, hit.z}) >= rad(o.type) + r + 5 * U);
        if (clear) { out = hit; return true; }
    }
    return t == Object::Crate;
}

// lift > 0: a crate dropped from that height above the land (CreateRandomCrate: no parachute, "Crate Spawn" until it rests)
bool Game::addObject(Object::Type t, float lift) {
    Vector3 p;
    if (!dropPoint(t, p)) return false;
    Vector3 at = {p.x, lift > 0 ? p.y + lift : p.y + halfHeight(t) + 0.05f, p.z};  // CreateRandomCrate 0x5c67a8: the crate centre 300 units over the ground point
    Object o = t == Object::Mine ? newMine(at) : Object{t, at, {0, 0, 0}, -1, -1, false, false};
    o.spawning = t == Object::Crate && lift > 0;
    const Scheme &sc = cfg.scheme;
    float pick = t == Object::Crate ? rand01() * (sc.healthShare + sc.weaponShare + sc.utilityShare + sc.mysteryShare) : -1;
    if (pick >= sc.healthShare + sc.weaponShare + sc.utilityShare) {  // 0x4f4b90: the item at spawn, by its Mystery.Crate weight
        int total = 0;
        for (const MysteryItem &m : MYSTERY_ITEMS) total += m.weight;
        int r = (int)(rand01() * total);
        for (o.mystery = 0; (r -= MYSTERY_ITEMS[o.mystery].weight) >= 0; o.mystery++) {}
    } else if (pick >= sc.healthShare) {
        bool util = pick >= sc.healthShare + sc.weaponShare;
        int total = 0;
        for (const WeaponDef &wd : WEAPONS) total += utility(wd.kind) == util ? wd.weight : 0;
        int r = (int)(rand01() * total);
        for (size_t k = 0; k < WEAPONS.size() && o.weapon < 0; k++) if (utility(WEAPONS[k].kind) == util && (r -= WEAPONS[k].weight) < 0) o.weapon = (int)k;
    } else if (t == Object::Crate) o.count = sc.crateHealth;  // CreateRandomCrate 0x4fa71d: NumContents = Crate.HealthInCrates
    objects.push_back(o);
    return true;
}

void Game::stepObjects() {
    for (size_t i = 0; i < objects.size();) {
        Object &o = objects[i];
        float h = crateLike(o) ? fmaxf(0, 0.5f * o.scale - 0.05f) : halfHeight(o.type);  // crate: rests at 10 x Scale units (0x5c94d0), less the 0.05 m probe
        if (o.pinned) o.vel = {};  // Crate.Gravity 0: the fall 0x5c9420 never moves it
        Vector3 v0 = o.vel;
        if (o.hooked || o.pinned) {  // on the rope (step())
        } else if (o.vel.y <= 0 && terrain.solid({o.pos.x, o.pos.y - h - 0.05f, o.pos.z})) {
            Vector3 n = terrain.normal({o.pos.x, o.pos.y - h, o.pos.z});
            if (crateLike(o)) {  // CrateLogicEntity 0x5c8900: v = 0.2 (vx, -vy, vz), at rest under 0.02 units/ms; the chute closes (0x5c9503)
                o.vel = {o.vel.x * 0.2f, -o.vel.y * 0.2f, o.vel.z * 0.2f};
                if (Vector3Length(o.vel) < 1) {
                    if (o.spawning) emit(GameEvent::CrateLand, o.pos, -1, o.mystery >= 0 ? -2 : o.weapon);
                    o.vel = {0, 0, 0}, o.spawning = false;
                }
            } else if (n.y < SLIDE_NY) o.vel = {(o.vel.x + n.x * n.y * gravity() * DT) * SLIDE_FRICTION, 0, (o.vel.z + n.z * n.y * gravity() * DT) * SLIDE_FRICTION};  // too steep: W4M Sliding, as wormBody
            else o.vel = {o.vel.x * 0.8f, 0, o.vel.z * 0.8f};
            o.falling = false, v0 = o.vel;
        } else {
            o.vel.y -= gravity() * DT;  // crates: Gravity x Low.Gravity.Multiplier, no drag, no wind (0x5c9420)
            if (o.falling && o.vel.y < -2.75f) o.vel.y += (-2.75f - o.vel.y) * (1 - powf(0.75f, DT / 0.02f));  // under a chute: toward 0.055 units/ms
        }
        Vector3 face = Vector3Scale(Vector3Normalize({o.vel.x, 0, o.vel.z}), h);  // the leading side meets a wall, not the centre
        Vector3 d = arcMove(v0, o.vel, crateLike(o) ? EULER_X : 0);  // crates 0x5c961a: pos += v x 20, then v += a x 20
        for (int k = 0, n = o.hooked ? 0 : substeps(o.vel); k < n; k++) {
            Vector3 np = Vector3Add(o.pos, Vector3Scale(d, 1.0f / n));
            if (terrain.solid({np.x + face.x, o.pos.y, np.z + face.z})) { o.vel.x = o.vel.z = d.x = d.z = 0; np.x = o.pos.x; np.z = o.pos.z; }
            if (o.vel.y > 0 && terrain.solid({np.x, np.y + h, np.z})) { o.vel.y = d.y = 0; np.y = o.pos.y; }
            o.pos = np;
            for (int j = 0; !o.pinned && j < 20 && terrain.solid({o.pos.x, o.pos.y - h, o.pos.z}); j++) o.pos.y += 0.05f;
        }

        bool gone = o.pos.y < water, boom = o.dead && (o.type == Object::Barrel || crateLike(o));  // 0x5c5810: every crate type blows up
        if (gone && (crateLike(o) || o.type == Object::Mine))  // crate 0x5c5b70: WaterSmallSplash at y = 1 unit absolute; mine: a Parabolic payload, SplashFx (the drum starts none, 0x5d1e12)
            emit(GameEvent::Splash, crateLike(o) ? Vector3{o.pos.x, Terrain::WATER + 1.0f / 20, o.pos.z} : Vector3{o.pos.x, surfaceY(water, 2), o.pos.z}),
                events.back().fx = crateLike(o) ? "WXP_WaterSmallSplash" : "WXP_WaterSplash";
        int det = 0;
        if (o.type == Object::Mine) {
            if (o.courtesy > 0) o.courtesy--;
            for (const Worm &w : worms)  // every worm, flying or sliding too
                if (o.fuse < 0 && !o.dud && !o.courtesy && w.alive && Vector3Distance(w.pos, o.pos) < MINE_ARM) {
                    o.fuse = o.delay >= 0 ? o.delay : mineFuse();
                    emit(GameEvent::MineArm, o.pos);
                }
            if (o.fuse >= 0 && (o.fuse -= DT) <= 0) {
                det = mineType();  // drawn before the dud test (0x58118d, 0x581326)
                o.dud = o.fizzle && !wp(WP_MINE_RESPAWN);  // only CreateMine rolls a dud (0x4f9818), a laid mine never; Detonate 0x581179
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
        // 0x5cb7e0: the crate sphere (10 x Scale units) meets a worm's (10 units, 5 above its feet), any worm, any time, or a sheep-like
        // payload's for the active worm (0x5c9750); the collector's AlliedGroup must be TeamCollectable's (0x5c8370)
        bool sheep = false;
        for (const Projectile &s : shots) sheep |= sheepLike(WEAPONS[s.weapon].kind) && Vector3Distance(s.pos, o.pos) < 0.5f * o.scale + WEAPONS[s.weapon].size;
        auto touching = [&](const Worm &w) { return Vector3Distance(o.pos, {w.pos.x, w.pos.y - R + 0.25f, w.pos.z}) < 0.5f * o.scale + 0.5f; };
        int opened = -1, opener = -1;  // mystery: applied once the crate is gone (it may add objects)
        if (crateLike(o) && !o.dead)
            for (Worm &w : worms) {
                if (!(sheep && &w == &worms[current]) && (!w.alive || !touching(w))) continue;
                if (o.teamCollect >= 0 && alliance(w.team) != o.teamCollect) continue;
                int add = 0;
                if (o.mystery >= 0) opened = o.mystery, opener = int(&w - worms.data()), emit(GameEvent::Mystery, o.pos, opener, o.mystery);
                else if (o.type == Object::Target || o.weapon < -1) {  // a target or a script's custom crate: no contents (0x5cb61e)
                } else if (o.weapon < 0) {  // NumContents (0x5c86af); label jumps on pickup (W4M); Worm.Antidote also clears 0x400 (0x5adecd)
                    int hp = o.count == CRATE_STOCK ? cfg.scheme.crateHealth : o.count;
                    w.hp += hp, w.counted += hp, w.poison = 0, w.abducted = false;
                }
                else if (WEAPONS[o.weapon].kind == Kind::DoubleDamage) doubleDamage = true;
                else if (WEAPONS[o.weapon].kind == Kind::CrateSpy) spy[w.team] = 1;  // never reset (0x5c8b20)
                else if (WEAPONS[o.weapon].kind == Kind::Armour) w.armour = true;  // Armour.Collected: the collector (0x5c9928 -> 0x5ae1ea)
                else ammo[w.team][o.weapon] = crateAdd(ammo[w.team][o.weapon], o.count), add = o.count;
                if (o.mystery < 0) emit(GameEvent::Collect, o.pos, int(&w - worms.data()), o.weapon), events.back().count = add;  // mystery: no pickup sound, the reveal's
                gone = true;
                break;
            }
        if (!gone && !boom && !o.dead) { i++; continue; }
        Object x = o;
        objects.erase(objects.begin() + i);  // before explode(), which only flags the others
        if (x.type == Object::Mine) emit(GameEvent::Deleted, x.pos, -1, x.id);
        if (opened >= 0) openMystery(opened, worms[opener]);
        if (boom && !gone) {
            if (x.type == Object::Mine) mineBlast(x.pos, det);
            else explode(x.pos, x.type == Object::Barrel ? BARREL_BLAST : CRATE_BLAST);
            if (x.type == Object::Mine && wp(WP_MINE_RESPAWN)) respawns.push_back({x.pos, msTicks(500)});
        }
    }
    for (size_t i = 0; i < respawns.size();)  // 0x4ff56a: CreateMine at that spot if it is above the water
        if (--respawns[i].second > 0) i++;
        else {
            Vector3 p = respawns[i].first;
            respawns.erase(respawns.begin() + i);
            if (p.y <= water) continue;
            objects.push_back(newMine(p));
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

// W4M FallDamage 0x5ac3e0: none up to 0.3 units/ms; Wormpot Max Fall doubles FallDamageRatio (Wormpot.lub FallingScale 2), Worms Drown sets SetNoFallDamage
int Game::fallDamage(float speed) const {
    if (speed <= FALL_SAFE || !cfg.scheme.fallDamage || wp(WP_WORMS_DROWN)) return 0;
    return (int)((speed - FALL_SAFE) * FALL_SCALE * (wp(WP_MAX_FALL) ? 2 : 1)) + 1;
}

// W4M 0x55e10f: the shooter is skipped by id (0x5b27e0 -> 0x519dd0); gun mask 0x1c3f has the bubble shell's 0x1000, not the 0x2000 it takes with the shooter inside
Game::GunHit Game::gunRay(Ray r, const Worm &shooter, float bullet) const {
    Vector3 hit, dir = r.direction;
    GunHit h{{}, terrain.raycast(r, GUN_RANGE, &hit) ? Vector3Distance(r.position, hit) : GUN_RANGE, -1, true};
    auto across = [&](Vector3 c, float rad) {  // 0x519dd0: where the bullet sphere first touches the collider sphere
        Vector3 o = Vector3Subtract(c, r.position);
        float t = Vector3DotProduct(o, dir), d2 = Vector3LengthSqr(o) - t * t, R2 = (rad + bullet) * (rad + bullet);
        float e = d2 < R2 ? t - sqrtf(R2 - d2) : -1;
        return e > 0 && e < h.dist ? e : -1.0f;
    };
    for (size_t i = 0; i < worms.size(); i++)
        if (float t = worms[i].alive && &worms[i] != &shooter ? across(worms[i].pos, R) : -1; t > 0) h.dist = t, h.worm = (int)i, h.land = false;
    for (size_t i = 0; i < objects.size(); i++)  // crate colliders (gun mask 0x1c3f): the crate sphere, 10 x Scale units
        if (float t = crateLike(objects[i]) && !objects[i].dead ? across(objects[i].pos, 0.5f * objects[i].scale) : -1; t > 0) h.dist = t, h.worm = -1, h.land = false, h.obj = (int)i;
    for (const Object &o : objects)  // mines (payload flag 8, Radius 3) and drums (flag 0x10, 9 units): only the hit's Explosion reaches them
        if (float t = (o.type == Object::Mine || o.type == Object::Barrel) && !o.dead ? across(o.pos, o.type == Object::Mine ? 0.15f : 0.45f) : -1; t > 0)
            h.dist = t, h.worm = -1, h.land = false, h.obj = -1;
    for (const Bubble &bb : bubbles) {
        Vector3 c = Vector3Add(bb.pos, {0, BUBBLE_UP, 0});
        if (float t = Vector3Distance(c, r.position) > BUBBLE_SHELL ? across(c, BUBBLE_SHELL) : -1; t > 0) h.dist = t, h.worm = -1, h.land = false, h.obj = -1;
    }
    h.at = h.worm >= 0 ? worms[h.worm].pos : Vector3Add(r.position, Vector3Scale(dir, h.dist));
    return h;
}

Blast Game::gunBlast(int weapon) const { return superBlast(blastOf(weaponDef(weapon), false), containerOf(weapon, false)); }

Blast Game::superBlast(Blast b, int container) const {
    Vector2 k = superScale(container);
    b.damage *= k.x, b.crater *= k.x, b.push *= k.y;
    return b;
}

Vector3 Game::strikeStart(const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) const {
    float h = landTop() + STRIKE_EXTRA, lead = BOMBER_SPEED * sqrtf(2 * fmaxf(h - tgt.y, 0) / (gravity() * wd.grav));
    float back = lead + (wd.clusters > 1 ? STRIKE_BLITZ / 1000.0f * BOMBER_SPEED / 2 : 0);  // 0x54d9b4: BlitzDuration x GroundSpeed / 2 when NumBombs > 1
    vel = Vector3Scale(dir, BOMBER_SPEED);
    return {tgt.x - dir.x * back, h, tgt.z - dir.z * back};
}

// W4M active objects (0x4d3af0 callers): worms falling or sliding (0x5aa996, 0x5aaa04), a drowned worm afloat ("Worm Dying"
// 0x5a7190), a crate till it rests (0x5c9bd0), an armed or moving mine (payload 0x57ee73, 0x5778f0), a drum about to blow
// (0x5d1f86), a falling sentry (0x56cdc3), the FlyCam hold (0x528457). The only emitter of interest, WeaponGasCanJet, is never spawned.
bool Game::efmvActive() const { return ufo() || scriptMovieOn(*this); }

bool Game::active() const {
    for (const Worm &x : worms) if ((x.alive && (!x.grounded || Vector3LengthSqr(x.vel) >= 0.01f)) || (x.drowned && x.counted > 0)) return true;
    for (const Object &o : objects) {
        bool moving = Vector3LengthSqr(o.vel) >= 0.01f;
        if ((o.type == Object::Mine && (o.fuse >= 0 || moving)) || (o.type == Object::Crate && o.spawning) || o.dead || (o.type == Object::Sentry && moving))
            return true;
    }
    return camHold > 0 || (factory.on && factory.state);
}

// WormManager.GetActiveAlliances: a team stands until its last worm has died (Worm.Died at its blast) or it surrendered
bool Game::standing(int team) const {
    if (team < (int)surrendered.size() && surrendered[team]) return false;
    for (const Worm &x : worms) if (x.team == team && (x.alive || (x.drowned && x.counted > 0))) return true;
    return false;
}

// SurrenderTeam 0x5b4d00 (Team.Surrender from the utility, SurrenderTeamById from a vital worm): Surrendered, out of the turn order
void Game::surrender(int team) { if (team < (int)surrendered.size()) surrendered[team] = 1; }

// GameLogic.ApplyDamage: every hurt worm shows its damage at once (0x5abc50); its energy <= the damage queues it to die (0x5abfb4).
// Vampire Wormpot (0x5a9710): the active worm, if its team's vampire, gains half of each other worm's damage, in worm order.
void Game::applyDamage(const std::vector<int> *type6) {
    for (Worm &x : worms) std::fill(x.dealt, x.dealt + 5, 0);
    countGroup.clear(), countT = 0;
    const Worm *vamp = wp(WP_VAMPIRE) && current % perTeam == 0 ? &worms[current] : nullptr;
    for (Worm &x : worms) {
        int i = int(&x - worms.data());
        if (vamp && &x != vamp && x.alive) worms[current].hp += (x.counted - x.hp + (type6 ? (*type6)[i] : 0)) / 2;  // poison included
        if (!x.alive || (x.hp == x.counted && x.hp > 0)) continue;
        if (x.hp != x.counted) countGroup.push_back(i);
        if (x.hp <= 0) {
            deathQueue.push_back(i);  // GameLogic.AddMeToDeathQueue
            if ((cfg.rules & RULE_KING) && i % perTeam == 0) surrender(x.team);  // flag 0x100 (0x5abf26)
        }
    }
}

// Worm.ApplyPoison 0x5ac060: poison takes its rate, never the last hp; an abductee unhurt since its last roll gets rand % 100 hp,
// 0 kills. Both are damage type 6, which shows no display (0x5abe38): the label jumps unless other damage is pending.
std::vector<int> Game::applyPoison() {
    std::vector<int> dealt(worms.size());
    for (Worm &x : worms) {
        if (!x.alive) continue;
        int wi = int(&x - worms.data()), was = x.hp;
        if (x.poison && !wp(WP_WORMS_DROWN)) {
            if (x.hp > 1) x.hp -= std::min(x.poison, x.hp - 1), emit(GameEvent::Hurt, x.pos, wi, 6);
        } else if (x.abducted) {
            if (x.calm >= 0 && x.hp >= x.calm) {
                int h = (int)(rand01() * 100);
                if (h != x.hp) emit(GameEvent::AbdDamage, x.pos, wi);
                if (h < x.hp) emit(GameEvent::Hurt, x.pos, wi, 6);
                x.hp = h;
            }
            x.calm = x.hp;
        }
        if (x.counted == was) x.counted = std::max(0, x.hp);
        dealt[wi] = was - x.hp;
    }
    return dealt;
}

// stdlib CheckActivity: anything active (shots, worms, displays, the death queue) -> wait for GameLogic_NoActivity, else DoPostActivity
void Game::checkActivity() {
    if (!shots.empty() || active() || !countGroup.empty() || !deathQueue.empty()) timer = 1;
    else doPostActivity();
}

// stdlib DoPostActivity: pass 1 ApplyPoison, ApplyDamage, DoubleDamage 0, DoOncePerTurnFunctions (stdvs, two teams standing:
// sudden death, DropRandomCrate, Wormpot showers), then CheckActivity; pass 2 Turn.Ended, the victory check, StartTurn.
void Game::doPostActivity() {
    Worm &w = worms[current];
    if (!crated) {
        crated = true;
        std::vector<int> t6 = applyPoison();
        applyDamage(&t6);
        doubleDamage = false;
        int left = 0;
        for (int t = 0; t < teams; t++) left += standing(t);
        if (left > 1 || cfg.mission) {
            const Scheme &sc = cfg.scheme;
            if ((cfg.rules & RULE_SUDDEN_DEATH) && !suddenDeath && clock >= sc.roundTime * 3600) {  // CheckSuddenDeath: RoundTimeRemaining 0
                suddenDeath = true;
                if (sc.sdType == Scheme::SD_ONE_HP) for (Worm &x : worms) if (x.alive && x.hp > 1) x.hp = x.counted = 1;  // lib_SetAllWormsEnergy(1)
                if (sc.sdType == Scheme::SD_DRAW) { phase = Phase::GameOver, winner = -1, emit(GameEvent::GameOver, {0, 0, 0}); return; }  // FCS.GameOver
            }
            static const float RISE[] = {0, 0.2f, 0.4f, 0.8f};  // Water.RiseSpeed 0 / Slow 4 / Medium 8 / Fast 16 units
            if (suddenDeath) water += RISE[std::min<int>(sc.waterSpeed, 3)];
            if (!(cfg.rules & RULE_ROPE_RACE)) {
                dropCrates(1, true);  // GameLogic.DropRandomCrate
                factoryStart();
                if (wp(WP_CRATE_SHOWER) || wp(WP_CRATE_DROPS)) dropCrates(6, false);  // DoWormpotOncePerTurnFunctions: one CrateShower
            }
        }
        checkActivity();
        return;
    }
    for (int &d : delays[w.team]) d = std::max(0, d - 1);  // W4M ActivateNextWorm 0x5b5a5f -> DecrementWeaponDelays 0x4f4df0: the team that just played
    picked[w.team] = weapon, beginTurn(w.team);
}

// GameLogic.DropRandomCrate 0x4fab20 (roll: rand % 100 < RandomCrateChancePerTurn) or CrateShower 0x4fb850 (6, no roll):
// CreateRandomCrate 0x4fa4b0, 300 units over the land
void Game::dropCrates(int n, bool roll) {
    for (; n > 0; n--)
        if ((!roll || (int)(rand01() * 100) < cfg.scheme.crateChance) && addObject(Object::Crate, 15)) emit(GameEvent::CrateDrop, objects.back().pos);
}

// The damage display (COUNT_DAMAGE), then the death queue 0x4f9b30: the front worm's throes once nothing else is active, its blast
// 3 s later; a living worm its blast hurts keeps that damage pending for the next ApplyDamage.
void Game::stepCount() {
    ++countT;
    bool shown = countGroup.empty() || countT >= COUNT_DAMAGE;
    if (dyingWorm >= 0) {
        if (--throes > 0) return;
        Worm &x = worms[dyingWorm];
        int i = dyingWorm;
        dyingWorm = -1;
        if ((cfg.rules & RULE_HIGHLANDER) && lastHitTeam[i] >= 0 && lastHitTeam[i] != x.team)
            for (size_t wi = 0; wi < WEAPONS.size(); wi++)
                if (ammo[x.team][wi] && ammo[lastHitTeam[i]][wi] >= 0) ammo[lastHitTeam[i]][wi]++;
        x.alive = false, x.counted = 0;
        std::vector<int> was;
        for (int j : countGroup) was.push_back(worms[j].hp);
        deathBlast(x, i);
        for (size_t k = countGroup.size(); k-- > 0;)  // hurt now: shown at the next ApplyDamage
            if (worms[countGroup[k]].hp != was[k]) worms[countGroup[k]].counted = was[k], countGroup.erase(countGroup.begin() + k);
    } else if (shown && !deathQueue.empty() && shots.empty() && !active()) {
        dyingWorm = deathQueue.front(), throes = COUNT_THROES;  // Worm.TimeToDie
        deathQueue.erase(deathQueue.begin());
        if ((cfg.rules & RULE_KING) && dyingWorm % perTeam == 0) surrender(worms[dyingWorm].team);  // 0x5a7287
    }
    if (shown && deathQueue.empty() && dyingWorm < 0) {
        for (int i : countGroup) worms[i].counted = std::max(0, worms[i].hp);
        countGroup.clear(), timer = 1;  // back to WaitUntilNoActivity
    }
}

// W4M LandscapeLogicEntity 0x4720c0: Land.Center = the middle of the land's bounding box (ours: floor 0 to landTop)
Vector3 Game::landCenter() const { return {Terrain::NX * Terrain::VOX / 2, landTop() / 2, Terrain::NZ * Terrain::VOX / 2}; }

void Game::weldLand(Vector3 c, Vector3 half) {
    terrain.weld(c, half);
    landMax = fmaxf(landMax, c.y + half.y);
}

Vector3 Game::blimpFocus(Vector3 ref, float yaw) const {
    float y = fmaxf(ref.y, landTop()) + BLIMP_LIFT, d = (y - ref.y) / tanf(BLIMP_PITCH);
    return {ref.x - sinf(yaw) * d, y, ref.z - cosf(yaw) * d};
}

Vector3 Game::target() const {
    const Worm &w = worms[current];
    Vector3 hit, dir = aimDir(w);
    if (weaponDef(weapon).kind == Kind::Homing ? blimp : cursorOn && targeted(weaponDef(weapon).kind)) return blimpHit(&hit), hit;  // Homing: the cursor only while in the Blimp
    if (terrain.raycast({Vector3Add(w.pos, dir), dir}, 60, &hit)) return hit;
    Vector3 far = {w.pos.x + sinf(w.yaw) * 30, (Terrain::NY - 1) * Terrain::VOX, w.pos.z + cosf(w.yaw) * 30};
    if (terrain.raycast({far, {0, -1, 0}}, Terrain::NY * Terrain::VOX, &hit)) return hit;
    return {far.x, water, far.z};
}

void Game::use(Worm &w) {
    launched = weapon;
    const WeaponDef &wd = weaponDef(weapon);
    int &n = ammo[w.team][weapon];
    if (wd.kind == Kind::Parachute) {  // 0x578db0 on FireUtil: opens only in Ballistic (+0x60), spending the ammo; open, it closes
        if (chute) chute = false;
        else if (!w.grounded && !vault.t) n -= n > 0, chuteOpen(w), emit(GameEvent::Fire, w.pos, current, weapon);
        return;
    }
    if (wd.kind == Kind::Rope) {  // 0x573790: FIRE launches the hook (NumShots left) or retracts it in flight
        if (grapple.on) grapple.on = false;
        else if (ropeShots < ROPE_SHOTS) {
            grapple = grappleFire({w.pos.x, w.pos.y - R, w.pos.z}, w.yaw, w.pitch, w.vel, rope.swung), ropeMax = wd.speed;
            emit(GameEvent::Fire, w.pos, current, weapon);
        }
        return;
    }
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
        if (!jetUsed) jetUsed = true, n -= n > 0;  // the fuel is the entity's since its selection
        if (fuel > JET_DRY) {
            if (ambulatory(w)) emit(GameEvent::JetStart, w.pos, current, weapon);
            jetting = true, thrust = wd.speed, boost = 0, emit(GameEvent::Fire, w.pos, current, weapon);
        }
        return;
    }
    if (wd.kind == Kind::Bubble) {  // W4M 0x54ff40 (not a BaseWeapon: no Timer.EndTurn, the turn goes on): its delay set to 1 for this turn
        delays[w.team][weapon] = 1, bubbleAt = clock + msTicks(400);  // DecrementInventory at the callback; Bubble.LaunchDelay 400
        emit(GameEvent::Fire, w.pos, current, weapon);
        return;
    }
    if (n > 0 && !shotsLeft && !(wd.kind == Kind::ChangeWorm && changing)) n--;
    Vector3 dir = aimDir(w), f = flat(w.yaw), tgt = target();
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
        if (cursorOn && wd.fuse > 0) f = {f.z, 0, -f.x};      // SuperBomber 0x58aa76: that right vector turned pi/2 about up (0x69cc4d), the view's forward
        if (wd.fuse > 0) shots.push_back({Vector3Add(tgt, {-f.x * BOMBER_LEAD, BOMBER_HEIGHT, -f.z * BOMBER_LEAD}), Vector3Scale(f, wd.speed), weapon, wd.fuse, false, 1, {}, STRIKE_LEAD, 0});
        else {  // W4M Bomber: a run along the worm's facing, one bomb every strikeTicks()
            Vector3 v, p = strikeStart(wd, tgt, f, v);
            shots.push_back({p, v, weapon, 0, false, 1, {}, STRIKE_LEAD, 0});
        }
        phase = Phase::Flying;
        break;
    case Kind::Donkey: {
        bool fat = wd.clusters > 0;  // Fatkins: the Bomber's one bomb (0x54ddf0), dropped as an airstrike bomb
        Vector3 v = {0, -wd.speed, 0}, p = fat ? strikeStart(wd, tgt, cursorOn ? strikeDir() : f, v) : Vector3Add(tgt, {0, fmaxf(DONKEY_MIN_HEIGHT, landTop() - Terrain::WATER + DONKEY_EXTRA), 0});  // Land.MaxHeight in W4M y (ours - WATER), added to the target
        shots.push_back({p, v, weapon, 0, false, fat ? 1 : 1 << 30, {0, p.y, 0}, fat ? STRIKE_LEAD : 0, 0});  // donkey: smashes until LifeTime or the water
        if (wd.clusters == 0) emit(GameEvent::Launch, p, -1, weapon);  // ArielFx WXP_CrateSpawnLARGE where it appears
    }
        phase = Phase::Flying;
        break;
    case Kind::Shotgun: {
        if (!shotsLeft) shotsLeft = wd.shots;
        const GunHit h = gunRay({muzzle(terrain, w.pos, launchPoint(wd, w.pos, w.yaw)), dir}, w, wd.size);
        // W4M 0x55e5da / 0x55ea22: one ExplosionMessage per hit (LandDamageRadius 0: no crater), damage centred on the worm or the land hit, push centre 2 units
        // behind the worm (and 2 low) / 1 unit behind the land hit; the worm's own damage and impulse come from that message
        Blast b = gunBlast(weapon);
        b.pushOff = h.worm >= 0 ? Vector3{-dir.x * 0.1f, -0.1f, -dir.z * 0.1f} : Vector3Scale(dir, -0.05f);
        // ours: W4M clears the hit land voxel (0x55d8c0, Land.ClearVoxel, LandDamageMagnitude 25 >= 15); the importer keeps no cell grid, so a 0.5 m sphere 0.5 m deep stands in
        if (h.land && h.dist < GUN_RANGE) terrain.carve(Vector3Add(h.at, Vector3Scale(dir, 0.5f)), 0.5f);
        if (h.dist < GUN_RANGE) explode(h.at, b, 0, 0, weapon);
        if (h.obj >= 0) crateHit(objects[h.obj], b.damage * (doubled() ? 2 : 1));  // DamageImpulseMessage 0x518c20 -> 0x5c8a90, x2 under DoubleDamage
        if (--shotsLeft == 0 && endlessGun) shotsLeft = 1;  // Challenge.EndlessGun (0x55efbf): no last shot, the worm may move (0x55d54f)
        else if (shotsLeft == 0) phase = Phase::Flying;
        break;
    }
    case Kind::Rope: break;  // above
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
        if (wd.kind == Kind::SuperSheep)  // Starburst: aim holds its heading, Starburst.InitialSpeed 0 (0x5883c0 sets v = heading x speed)
            shots.push_back({from, {}, weapon, wd.fuse, false, 1, dir});
        else
            shots.push_back({from, Vector3Add(carry, Vector3Scale(dir, launchSpeed(wd, power))), weapon, wd.fuse, false, 1, locked ? lockAt : tgt});
        for (size_t k = 0; k < worms.size() && k < 63; k++)  // already inside a worm at launch (its rider): not a fresh contact
            if (worms[k].alive && Vector3Distance(from, worms[k].pos) < R + 0.3f) shots.back().touching |= 1ull << k;
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
            Vector2 k = superScale(containerOf(weapon, false));
            bool dimMak = wp(WP_DIM_MAK) && wd.name == "Prod";  // 0x5d6420: kWeaponProd InstantKill, WXP_Wep_DimMak; 0x567c03 deals the energy left, no impulse
            hurt(o, dimMak ? std::max(0, o.hp) : (int)(wd.damage * k.x), false, dimMak ? 5 : 0);
            if (dimMak) continue;
            if (wd.pins) o.nailed = true, o.vel = {0, 0, 0}, o.pos.y -= 0.35f;  // sunk to the waist
            else impulse(o, Vector3Scale(Vector3Add(Vector3Scale(dir, wd.speed), {0, wd.bounce, 0}), k.y));
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
    case Kind::Parachute: break;  // above
    case Kind::Armour: w.armour = true; break;
    case Kind::Girder:  // Land.SpawnPiece GirderSmall.xom: a 4 x 4 m deck 1 m thick on two 1 m legs, axis-aligned
        weldLand(Vector3Add(girder, {0, 0.5f, 0}), {2, 0.5f, 2});  // legs along x at the z ends: BitArray3D word 3 + x + 4 z (0x43dfd0)
        for (float sz : {-1.5f, 1.5f}) weldLand(Vector3Add(girder, {0, -0.5f, sz}), {2, 0.5f, 0.5f});
        girders++;
        if (!wp(WP_MULTI_GIRDER)) phase = Phase::Flying;  // 0x55ac30: Timer.StartRetreatTimer unless GirdersDontEndTurn
        break;
    case Kind::Icarus:  // 0x587600: DisableMovementRef, DisableWeaponChange, EndFireWeapon -> Weapon.PostLaunchDelay (500 ms)
        icarus = 3, flapAt = clock + msTicks(weaponDef(weapon).postLaunch);
        break;
    case Kind::Binoculars: case Kind::DoubleDamage: case Kind::CrateSpy: case Kind::Bubble: case Kind::None: break;
    case Kind::Surrender:  // SurrenderLogicEntity 0x58b910: Team.Surrender -> SurrenderTeam; its worms stay
        surrender(w.team);
        phase = Phase::Settle, timer = 1;
        break;
    case Kind::SkipGo: phase = Phase::Flying; break;  // W4M 0x587ed0: EndTurn, PostLaunchDelay, then a retreat of 0 (0x588160)
    case Kind::ChangeWorm:  // W4M 0x59a6c0: the first cycle spends one (DecrementInventory), later ones are free (SelectNextWorm)
        changing = true;
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
    const WeaponDef &d = weaponDef(weapon);
    if (d.kind == Kind::Shotgun || utility(d.kind)) return true;
    bool moving = d.name == "Dynamite" || d.name == "Fire Punch" || d.kind == Kind::Mine || d.kind == Kind::Sheep;
    return moving || ambulatory(w);
}

void Game::stepWorm(Worm &w) {
    if (w.drowned && w.counted > 0) return floatStep(w);
    if (!w.alive || aboard(int(&w - worms.data()))) return;
    if (w.abducted) zapStep(w);
    if (w.nailed) { w.vel = {0, 0, 0}, w.grounded = true; if (underwater(w)) drown(w); return; }
    if (vault.t && &w == &worms[current]) return;  // W4M: no Ballistic while Vaulting
    auto land = [&](float speed) {
        bool winged = icarus == 2 && &w == &worms[current];  // W4M Redbull sets flag 0x40 each flight frame (0x587446)
        int dmg = winged ? 0 : fallDamage(speed), wi = int(&w - worms.data());
        if (dmg > 0) { w.hp -= dmg; selfHurt |= wi == current; emit(GameEvent::Hurt, w.pos, wi), emit(GameEvent::Fall, w.pos, wi); }
    };
    bool jet = jetting && &w == &worms[current];  // W4M jetpack contact 0x5633e9: walls and ceilings bounce at 0.8; it lands itself, no Ballistic
    w.motion.input = &w == &worms[current] ? steerIn : Vector3{};
    int stuck = w.motion.stuck;  // the jetpack's own flight is not Ballistic: it keeps no stuck count
    float hit = 0;
    bool hung = chute && &w == &worms[current] && !w.grounded;  // the parachute moves it (0x5792a0), no Ballistic gravity
    auto settle = [&](float e) { return wormBody(terrain, w.pos, w.vel, w.grounded, w.motion, w.yaw, hung ? 0 : gravity(), pot, e, hung ? Vector2{} : wormWind()); };
    // hung: Position = canopy + 40 units (sin a right - cos a up); ours reaches it through the body's sweep (voxel land), Velocity kept
    const Vector3 hv = w.vel, f = flat(w.yaw);
    if (hung) w.vel = Vector3Scale(Vector3Subtract(Vector3Add(chuteAt, {2 * sinf(chuteAng) * f.z, -2 * cosf(chuteAng), -2 * sinf(chuteAng) * f.x}), w.pos), 1 / DT);
    if (!jet) hit = settle(0.3f);
    else if (w.grounded = false, jetBody(terrain, w.pos, w.vel, gravity())) jetting = false, settle(0.3f);  // landed: no fall speed left, only the shared settling
    if (hung) w.vel = hv, chute = !w.grounded;  // a blocked canopy move closes it (0x57967e)
    if (!jet) land(hit);  // W4M 0x562f72: a foot touching land lands the pack with v -= (v.n) n, so Ballistic sees no fall speed
    if (jet) w.motion.stuck = stuck;
    if (underwater(w)) drown(w);
}

// 0x578db0: the canopy 40 units over Position, angle and spin 0; the drift reads 70 ms of (Wind, Gravity x Low.Gravity.Multiplier) once
void Game::chuteOpen(const Worm &w) {
    chute = true, chuteAt = {w.pos.x, w.pos.y + 2, w.pos.z}, chuteAng = chuteSpin = chuteSink = chuteGain = 0;
    chuteDrift = Vector3Scale({wind * WIND_ACCEL, -gravity(), windZ * WIND_ACCEL}, 0.07f);
}

// 0x578a40 on InputImpulse (0.05 units/ms at full tilt): backward or level, toward its side (+ on 0); forward, past 0.01 units/ms aside
int Game::chuteSteer(Vector3 in, float yaw) {
    if (Vector3LengthSqr(in) == 0) return 0;
    float ahead = in.x * sinf(yaw) + in.z * cosf(yaw), side = in.x * cosf(yaw) - in.z * sinf(yaw);
    return ahead <= 0 ? (side < 0 ? -1 : 1) : side > 0.2f ? 1 : side < -0.2f ? -1 : 0;
}

// W4M Worm.Vapourize 0x5885f0 (Starburst rider): 0x5ac160 deals its energy (0x5ab7e0) and unspawns it: no throes, death blast, grave or FX
void Game::vapourize(Worm &w) {
    int wi = int(&w - worms.data());
    // unspawned in the blast's own dispatch: its graphic never shows the blast's hurt
    events.erase(std::remove_if(events.begin(), events.end(), [&](const GameEvent &e) { return e.worm == wi && e.kind == GameEvent::Hurt; }), events.end());
    if ((cfg.rules & RULE_KING) && wi % perTeam == 0) surrender(w.team);
    w.alive = false, w.hp = w.counted = 0, w.vel = {0, 0, 0};
    if (wi == current) selfHurt = true;
}

// W4M 0x5ad640, each frame: its Position (the feet) under Water.Level - Worm.Drown.HeightOffset (7 units) -> kWPS_DrownFloat with
// its Velocity; a vital worm's team surrenders (0x5ad6fa), the active vampire gains half its energy (0x5ad7c9)
bool Game::underwater(const Worm &w) const { return w.pos.y - R < water - 0.35f; }

void Game::drown(Worm &w) {
    int wi = int(&w - worms.data());
    if (wp(WP_VAMPIRE) && current % perTeam == 0 && wi != current) worms[current].hp += w.counted / 2;
    if ((cfg.rules & RULE_KING) && wi % perTeam == 0) surrender(w.team);
    if (w.grounded || w.motion.slide) w.vel = {w.vel.x / 2, -1.5f, w.vel.z / 2};  // from Ambulatory / Sliding (0x5ad91f): -0.03 units/ms
    w.alive = false, w.drowned = true, w.floatT = 0;
    w.hp = 0, w.counted = std::max(1, w.counted);  // counted > 0: afloat, drawn until its blast
    if (wi == current) selfHurt = true;  // 0x5ad75c: the active worm posts Worm.Damaged.Current (stdlib EndTurn)
    emit(GameEvent::Splash, {w.pos.x, surfaceY(water, 1), w.pos.z}, wi), events.back().fx = "WXP_WaterSplash";  // 0x5ad640
}

// W4M death blast 0x5a9400 at the worm's Position (its feet): WXP_Explosion_Small, lifted to 1 unit over the surface when at or under it;
// a drowned worm (kWPS_DrownFloat) first starts WXP_WormDrownPopSplash there
void Game::deathBlast(const Worm &w, int wi) {
    Vector3 feet = {w.pos.x, w.pos.y - R, w.pos.z}, at = feet;
    if (surfaceY(water, 0) >= feet.y) at.y = surfaceY(water, 1);
    if (w.drowned) emit(GameEvent::Pop, {w.pos.x, surfaceY(water, 1), w.pos.z}, wi), events.back().fx = "WXP_WormDrownPopSplash";
    explode(feet, DEATH_BLAST, 0, 0, -1, "WXP_Explosion_Small", &at);
    emit(GameEvent::Death, w.pos, wi);
}

// W4M DrownFloat 0x5aa130 per 20 ms, no gravity, toward its feet at Water.Level - 8 units: below and sinking v = (6 v + up) / 7,
// below and rising v moves 1/13 of the way to up (at most 0.001 units/ms), up = (0, 0.03, 0) units/ms (0x5a59f0, 0x569fa0);
// there and not sinking: 2000 ms of vy = (vy - 0.001 (y - target)) x 0.95, then the death blast (0x5a9400) and the unspawn
void Game::floatStep(Worm &w) {
    const float K = DT / 0.02f, target = water - 0.4f, UP = 1.5f;
    float feet = w.pos.y - R;
    if (!w.floatT) {
        if (feet < target && w.vel.y < 0) w.vel = Vector3Lerp({0, UP, 0}, w.vel, powf(6.0f / 7, K));
        else if (feet < target) {
            Vector3 d = Vector3Scale(Vector3Subtract({0, UP, 0}, w.vel), 1 - powf(12.0f / 13, K));
            float l = Vector3Length(d), cap = 0.05f * K;
            w.vel = Vector3Add(w.vel, l > cap ? Vector3Scale(d, cap / l) : d);
        } else if (w.vel.y >= 0) w.floatT = DROWN_FLOAT;
    } else w.vel.y = (w.vel.y - (feet - target) * K) * powf(0.95f, K);
    w.pos = Vector3Add(w.pos, Vector3Scale(w.vel, DT));
    if (w.floatT && --w.floatT <= 0) {
        w.floatT = 0, w.counted = 0;  // gone: nothing left to draw
        deathBlast(w, int(&w - worms.data()));
    }
}

bool Game::steered() const {
    for (const Projectile &s : shots) {
        const WeaponDef &d = WEAPONS[s.weapon];
        if (!s.child && (d.kind == Kind::OldWoman || d.kind == Kind::SuperSheep || (d.kind == Kind::Scouser && !s.stage) || (d.kind == Kind::Airstrike && d.fuse > 0))) return true;
    }
    return false;
}

// W4M: Worm.WeaponDisableMovement from the fire to PostLaunchDelay's end; FlyCam (homing, super sheep) disables WormMoving
bool Game::wielding() const {
    return phase == Phase::Aim || ((phase == Phase::Flying || phase == Phase::Retreat) && timer > retreatTicks(weaponDef(launched >= 0 ? launched : weapon)));
}

bool Game::retreating() const {
    if ((phase != Phase::Flying && phase != Phase::Retreat) || wielding()) return false;
    for (const Projectile &s : shots) if (!s.child && WEAPONS[s.weapon].kind == Kind::Homing) return false;
    return !steered();  // ours: the stick steers the shot, not the worm
}

Object *Game::hooked() {
    for (Object &o : objects) if (o.hooked) return &o;
    return nullptr;
}

// Ninja rope: W4M runs 0x574480 every 20 ms, ours every tick with each step scaled by ROPE_K. Lengths in m, the W4M
// formulas in units (20 per m); the body point is the feet (W4M Position).
static constexpr float UNITS = 20, ROPE_K = Game::DT / 0.02f;
static constexpr float ROPE_SWING = 6e-5f, ROPE_DAMP = 0.99f, ROPE_MIN = 0.5f, ROPE_BEND = 0.5f, ROPE_RATE = 10, ROPE_EYE = 0.75f;  // Ninja.SwingAmount,
// RotationDamping, MinLength / MinBendDistFromWorm 10 units, LengthenShortenRate 0.2 units/ms, Worm.EyeLevelOffset 15 units
static constexpr int ROPE_REFINE = 8;  // Ninja.NumRaycastRefinements

// 0x571020: land across the stretch from the body to a bend. Ours starts 0.1 m off the feet and stops 0.4 m short of the bend:
// voxel hits land half a voxel deep, so the feet on the ground and the bend's own land would always cut it
static bool ropeCut(const Terrain &t, Vector3 from, Vector3 to, Vector3 *hit) {
    Vector3 d = Vector3Subtract(to, from);
    float l = Vector3Length(d);
    if (l < 0.5f) return false;
    d = Vector3Scale(d, 1 / l);
    return t.raycast({Vector3Add(from, Vector3Scale(d, 0.1f)), d}, l - 0.5f, hit);
}

// 0x5729e0: the angle moves on, the stick pushes (less on a long rope), damping without it, gravity pulls; the body is
// placed one step on (angle + spin) at the last stretch's length, (0, -1, 0) at angle 0, behind the yaw for > 0
static Vector3 swingStep(Rope &r, Vector3 body, int swing, float gravity) {
    int last = r.n - 1;
    float lc = Vector3Distance(body, r.pt[last]) * UNITS, ls = r.len[last] * UNITS, g = -gravity * UNITS / 1e6f;
    r.angle = wrapPi(r.angle + r.spin * ROPE_K);
    if (swing && lc > 0) r.spin += -swing * ROPE_SWING * 20 * 5 * ROPE_MIN * UNITS / (lc + 0.001f * lc * lc) * ROPE_K;
    else r.spin *= powf(ROPE_DAMP, ROPE_K);
    if (ls > 0) r.spin += 400 * g * sinf(r.angle) / ls * ROPE_K;
    float th = r.angle + r.spin * ROPE_K;
    return Vector3Add(r.pt[last], Vector3Scale(Vector3Add(Vector3Scale(flat(r.yaw), -sinf(th)), {0, -cosf(th), 0}), r.len[last]));
}

int Game::ropeSwing(const Input &in, float yaw) {
    if (!in.walk) return 0;
    if (!(in.buttons & Input::HEADING)) return in.walk > 0 ? 1 : -1;
    float a = fabsf(wrapPi(PI * in.turn / 128 - yaw));  // camera-relative stick vs the facing: 1.41372 / 1.72788 rad (81 / 99 deg)
    return a < 1.41372f ? 1 : a > 1.72788f ? -1 : 0;
}

// W4M collider of an object (centre, radius, flags): crate 10 units at its centre, flags 4 health / 0x20 target / 2 others (0x5ca180);
// drum 9 units at its Position, 0x10 (0x5d18b2); mine: a Landmine payload, Radius 3 units, ColliderFlags 0 | 8 (0x58246d)
static uint32_t collider(const Object &o, Vector3 &c, float &r) {
    c = ropePoint(o), r = o.type == Object::Mine ? 0.15f : o.type == Object::Barrel ? 0.45f : 0.5f;
    if (o.type == Object::Crate || o.type == Object::Target) c = o.pos;
    return o.type == Object::Crate ? (o.weapon < 0 && o.mystery < 0 ? 4 : 2) : o.type == Object::Target ? 0x20 : o.type == Object::Barrel ? 0x10 : o.type == Object::Mine ? 8 : 0;
}

// 0x56fcb0: the rope's sphere at the body meets a collider of its mask, or Fits 0x59edf0's 1 m rods (ours from Terrain::SUB up) fail. A worm:
// 5 units, mask 0x19 (worms, payloads, drums, bubbles: no crate); a hooked object (0x571d90): 0x3f, a crate its own 10 units (0x5c5fd0)
bool Game::ropeBlocked(Vector3 f, int self, int body) const {
    static const Vector2 ROD[] = {{0.2f, -0.15f}, {-0.2f, -0.15f}, {0, 0.25f}};
    for (Vector2 r : ROD)
        for (float h = Terrain::SUB; h < 1; h += 2 * Terrain::SUB) if (terrain.solid({f.x + r.x, f.y + h, f.z + r.y})) return true;
    const float r = body >= 0 && objects[body].type == Object::Crate ? 0.5f : 0.25f;
    for (size_t i = 0; i < worms.size(); i++)
        if ((int)i != self && worms[i].alive && Vector3Distance(f, {worms[i].pos.x, worms[i].pos.y - R + 0.25f, worms[i].pos.z}) < r + 0.5f) return true;
    for (const Projectile &s : shots) if (Vector3Distance(f, s.pos) < r + WEAPONS[s.weapon].size) return true;
    for (const Bubble &b : bubbles) if (Vector3Distance(f, b.pos) < r + 0.45f) return true;
    for (size_t i = 0; i < objects.size(); i++) {
        Vector3 c;
        float cr;
        if ((int)i != body && (collider(objects[i], c, cr) & (body >= 0 ? 0x3f : 0x19)) && Vector3Distance(f, c) < r + cr) return true;
    }
    return false;
}

// 0x573d00 / 0x571d90: one stretch from the hook; the angle from the eye's drop below it, the spin from the velocity (0x571aa0)
void Game::ropeHang(Rope &r, Vector3 hook, Vector3 feet, Vector3 vel, float yaw) const {
    Vector3 d = Vector3Subtract({feet.x, feet.y + ROPE_EYE, feet.z}, hook), f = flat(yaw), hv = {vel.x, 0, vel.z};
    float l = Vector3Length(d);
    r.n = 1, r.pt[0] = hook, r.len[0] = Vector3Distance(hook, feet), r.side[0] = {}, r.yaw = yaw;
    r.angle = acosf(Clamp(l > 0 ? -d.y / l : 1, -1, 1));
    if (r.swung && Vector3DotProduct(f, hv) < 0) r.angle = -r.angle;  // 0x570650 on a second hook: moving backwards, in front
    r.spin = 0;  // 0x571aa0: the angle the body sweeps about the hook in 20 ms, negative moving forwards
    Vector3 a = Vector3Subtract(feet, hook), b = Vector3Add(a, Vector3Scale(vel, 0.02f));
    if (Vector3LengthSqr(vel) > 0 && Vector3LengthSqr(a) > 0 && Vector3LengthSqr(b) > 0 && !Vector3Equals(a, b)) {
        float sweep = acosf(Clamp(Vector3DotProduct(Vector3Normalize(a), Vector3Normalize(b)), -1, 1));
        r.spin = Vector3LengthSqr(hv) > 0 && Vector3DotProduct(f, hv) >= 0 ? -sweep : sweep;
    }
    r.swung = true;
}

// 0x574480: reel 0x5713c0, swing 0x5729e0, bounce or wrap 0x573060, unwrap 0x571600; vel as 0x56fd60 sets it ((new - old) x
// 0.02 x 0.05 units/ms: a 50th of the motion's speed)
void Game::ropeTick(Rope &r, Vector3 &feet, Vector3 &vel, int swing, int8_t aim, int self, int body) const {
    const Vector3 was = feet;
    Vector3 a = feet, hit;
    int last = r.n - 1;
    if (aim) {  // 0x5713c0: lengthening stops at MaxLength over the whole rope; shortening is refused below MinLength, MinBendDistFromWorm
        float step = ROPE_RATE * DT, l = r.len[last], total = 0, nl = l;
        for (int i = 0; i < r.n; i++) total += r.len[i];
        if (aim < 0) nl = l + fminf(step, ropeMax - total);
        else if (step <= l && total - step >= ROPE_MIN && l - step >= ROPE_BEND) nl = l - step;
        Vector3 d = Vector3Subtract(a, r.pt[last]), np;
        if (nl != l && Vector3LengthSqr(d) > 0 && nl > 0) {
            np = Vector3Add(r.pt[last], Vector3Scale(Vector3Normalize(d), nl));
            if (!ropeCut(terrain, np, r.pt[last], &hit) && !ropeBlocked(np, self, body)) r.spin *= l / nl, r.len[last] = nl, a = np;
        }
    }
    Vector3 np = swingStep(r, a, swing, gravity());
    // 0x573060: the body hits something: the swing turns back at 0.9; the stretch cut by land: a bend where it was cut
    if (ropeBlocked(np, self, body)) r.spin *= -0.9f;
    else if (ropeCut(terrain, np, r.pt[last], &hit)) {
        Vector3 p = np, h;
        bool clear = false;
        for (int k = 0; k < ROPE_REFINE && !clear; k++) {  // halves back toward the old position while the stretch stays cut
            p = Vector3Scale(Vector3Add(was, p), 0.5f);
            if (!(clear = !ropeCut(terrain, p, r.pt[last], &h))) hit = h;
        }
        if (!clear) p = was;
        Vector3 off = Vector3Subtract(p, hit);
        if (Vector3LengthSqr(off) > 1e-8f) hit = Vector3Add(hit, Vector3Scale(Vector3Normalize(off), 0.05f + Terrain::SUB));  // W4M 1 unit off the land; ours + the march's half voxel
        if (Vector3Distance(hit, p) <= ROPE_BEND || r.n >= Rope::MAX) r.spin *= -0.9f;
        else {
            Vector3 o = r.pt[last], u = Vector3Subtract(was, o);
            float nl = Vector3Distance(hit, np);
            r.spin *= r.len[last] / nl;  // WormMass x spin x old length / (WormMass x new length)
            r.len[last] = Vector3Distance(o, hit);
            if (Vector3LengthSqr(u) > 0) u = Vector3Normalize(u);
            Vector3 v = Vector3Subtract(hit, Vector3Add(o, Vector3Scale(u, r.len[last])));  // from the old stretch's line toward the bend
            if (Vector3LengthSqr(v) < 1e-8f) v = Vector3Subtract(np, was);
            r.side[last] = Vector3Negate(v);
            r.pt[r.n] = hit, r.len[r.n] = nl, r.side[r.n] = {}, r.n++;
        }
    }
    // 0x571600: the bend before sees the body again and the body is past its side: the last bend goes
    if (r.n >= 2) {
        int b = r.n - 2;
        if (!ropeCut(terrain, np, r.pt[b], &hit) && Vector3DotProduct(Vector3Subtract(np, r.pt[b]), r.side[b]) > 0) {
            float old = r.len[b + 1];
            r.n--, r.len[b] = Vector3Distance(np, r.pt[b]);
            if (r.len[b] > 0) r.spin *= old / r.len[b];
        }
    }
    vel = Vector3Scale(Vector3Subtract(np, was), 1 / ROPE_K);
    feet = np;
}

// 0x573530: one more swing from where the body is; (new - old) x 0.05 units/ms x Ninja.DetachVelocityMulti 1
Vector3 Game::ropeRelease(Rope r, Vector3 feet, int swing) const {
    return Vector3Scale(Vector3Subtract(swingStep(r, feet, swing, gravity()), feet), 1 / DT);
}

void Game::ropeOn(Vector3 hook) {
    Worm &w = worms[current];
    roped = true, w.grounded = false, w.motion.slide = false;
    ropeHang(rope, hook, {w.pos.x, w.pos.y - R, w.pos.z}, w.vel, w.yaw);
}

// 0x572800: from the eye, 1 unit/ms along RotY(yaw) RotX(-pitch); hooked since it last stood, the pitch is 0x570650's instead:
// pi/2 tilted pi/4 toward the horizontal motion at 0.2 units/ms (10 m/s) and over, less below
Hook Game::grappleFire(Vector3 feet, float yaw, float pitch, Vector3 vel, bool swung) {
    if (swung) {
        float h = sqrtf(vel.x * vel.x + vel.z * vel.z), s = fminf(h, 10);
        if (sinf(yaw) * vel.x + cosf(yaw) * vel.z > 0) s = -s;
        pitch = PI / 2 + s / 10 * PI / 4;
    }
    return {{feet.x, feet.y + ROPE_EYE, feet.z}, Vector3Scale(dirOf(yaw, pitch), HOOK_SPEED), true};
}

// 0x573d00 per 20 ms, ours per tick: a standing worm's hook takes what its 5 units touch (mask 0x1e: a crate but a target, a still
// mine, a drum; a bubble retracts it, 0x572058); else land within this step's flight; else it flies on, retracted past maxLen
int Game::grappleStep(Hook &h, Vector3 feet, float maxLen, bool standing, Vector3 *hit, int *obj) const {
    if (standing && obj) {  // 0x571d90 asserts kWPS_Ambulatory first
        for (size_t i = 0; i < objects.size(); i++) {
            const Object &o = objects[i];
            Vector3 c;
            float r;
            bool still = o.type != Object::Mine || Vector3LengthSqr(o.vel) == 0;  // a moving payload: "not allowed", it flies on
            if ((collider(o, c, r) & 0x1e) && still && o.tag < 0 && Vector3Distance(h.at, c) < 0.25f + r) return *obj = (int)i, 3;
        }
        for (const Bubble &b : bubbles) if (Vector3Distance(h.at, b.pos) < 0.25f + 0.45f) return 0;
    }
    Vector3 d = Vector3Scale(h.vel, DT);
    if (terrain.raycast({h.at, Vector3Normalize(d)}, Vector3Length(d), hit)) return 2;
    h.at = Vector3Add(h.at, d);
    return Vector3Distance(feet, h.at) > maxLen ? 0 : 1;
}

void Game::ropeCleanup() {  // 0x572620: a rope that hooked (+0x7b) decrements the inventory as it goes
    if (ropeUsed < 0) return;
    int &n = ammo[worms[current].team][ropeUsed];
    n -= n > 0, ropeUsed = -1;
}

// W4M DamageImpulseMessage 0x518cbf: a direct hit (gun, melee) sets the worm's velocity, doubled under Double Damage, and launches it
void Game::impulse(Worm &o, Vector3 v) {
    o.vel = Vector3Scale(v, doubled() ? 2 : 1), o.grounded = false;
}

void Game::hurt(Worm &w, int dmg, bool blast, int type) {
    if (doubled()) dmg *= 2;  // W4M 0x518da5 doubles the message first,
    if (blast && w.armour) dmg = dmg * ARMOUR / 100;  // then the worm's Shield.DamageScale (explosions only)
    if (type >= 2 && type <= 4) {  // 0x5ab7e0 cap per type and ApplyDamage (0x5ababb); 150 for type 4 if the hurt worm's team has the Factory as super weapon (0x5abaf0)
        int cap = type == 4 && wp(WP_SECRET_WEAPON) && superWeapon[w.team] == W4M_FACTORY ? 150 : 75;
        dmg = std::max(0, std::min(dmg, cap * (doubled() ? 2 : 1) - w.dealt[type])), w.dealt[type] += dmg;
    }
    if (dmg <= 0 || wp(WP_WORMS_DROWN)) return;
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

// Wormpot.lub SetWormpotModes: WindEffectMore sets IsAffectedByWind on these (Homing, Super Sheep, guns... stay as WEAPTWK has them)
bool Game::windy(int wi, bool child) const {
    static const int ALL[] = {4, 7, 31, 30, 3, 18, 16, 2, 6, 17, 15, 24, 23, 26, W4M_FACTORY, 5, 8, 29};  // Wormpot.lub SetWeaponWind containers
    const WeaponDef &wd = WEAPONS[wi];
    if (wd.wind || !wp(WP_WIND_ALL)) return wd.wind;
    return std::count(std::begin(ALL), std::end(ALL), containerOf(wi, child)) > 0;
}

int Game::containerOf(int wi, bool child) const {
    static const char *ID[] = {"", "Bazooka", "Grenade", "Cluster Grenade", "Airstrike", "Dynamite", "Holy Hand Grenade", "Banana Bomb", "Landmine",
                               "Shotgun", "Baseball Bat", "Prod", "Fire Punch", "Homing Missile", "Flood", "Sheep", "Gas Canister", "Old Woman",
                               "Concrete Donkey", "Super Sheep", "Starburst", "", "Alien Abduction", "Fatkins Strike", "Inflatable Scouser", "Tail Nail",
                               "Poison Arrow", "Sentry Gun", "Sniper Rifle", "Super Airstrike"};
    if (wi < 0) return 0;
    if (customWeapon(wi)) return child ? 0 : W4M_FACTORY;
    for (int id = 1; id < 30; id++)
        if (WEAPONS[wi].name == ID[id]) return !child ? id : id == 3 ? 30 : id == 7 ? 31 : id == W4M_LANDMINE ? 0 : id;  // bomblets: kWeaponClusterBomb, kWeaponBananette, kWeaponLandmineBomblet (no id)
    return 0;
}

// Wormpot.lub: each Super* mode scales its containers' WormDamageMagnitude and LandDamageRadius by SuperScale 2 (x) and ImpulseMagnitude by
// PowerScale 2 (y); Super Secret Weapons scales the active team's weapon's damage and crater by SuperScale for its turn (0x5d71b0 -> 0x5d6770)
Vector2 Game::superScale(int id) const {
    static const std::vector<int> SET[] = {{1, 5, 2, 6, 8, 13, 16, 23}, {3, 30, 4, 29, 7, 31}, {15, 19, 17, 18, 24}, {9, 28}, {10, 11, 12, 25}};
    static const int MODE[] = {WP_SUPER_EXPLOSIVES, WP_SUPER_CLUSTERS, WP_SUPER_ANIMALS, WP_SUPER_FIREARMS, WP_SUPER_MELEE};
    Vector2 k = {1, 1};
    for (int m = 0; m < 5; m++) if (wp(MODE[m]) && std::count(SET[m].begin(), SET[m].end(), id)) k = {2, 2};
    if (id && id == superWeapon[worms[current].team] && id != W4M_SENTRY) k.x *= 2;  // a Sentry Gun is refused (0x5d692e)
    return k;
}

int Game::inventoryId(int wi) const {
    static const char *UTIL[] = {"Girder", "Ninja Rope", "Parachute", "Jetpack", "Skip Go", "Surrender", "Change Worm", "Icarus Potion",
                                 "Bubble Trouble", "Binoculars", "Double Damage", "", "Crate Spy", "Armour"};  // kUtilityGirder 0x22 ..
    if (int id = containerOf(wi, false)) return id;
    for (int k = 0; k < 14; k++) if (WEAPONS[wi].name == UTIL[k]) return 0x22 + k;
    return 0;
}

void Game::openMystery(int item, Worm &w) {
    auto named = [](const char *n) { for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].name == n) return (int)i; return -1; };
    auto triplet = [&](Object::Type t) {  // 0x4f5760 / 0x4f5690: min(n, 3) draws of rand % n, a repeat is dropped
        std::vector<int> ids, pick;
        for (size_t i = 0; i < objects.size(); i++) if (objects[i].type == t) ids.push_back((int)i);
        for (size_t k = 0; k < std::min<size_t>(ids.size(), 3); k++)
            if (int j = ids[(int)(rand01() * ids.size())]; std::find(pick.begin(), pick.end(), j) == pick.end()) pick.push_back(j);
        return pick;
    };
    switch (item) {
    case MY_MINE_LAYER:  // GameLogic.CreateRandomMine x MysteryMineLayer.NumMines 5, the start-of-game mine (no Mine.MaxInPlay check)
        for (int k = 0; k < 5; k++) addObject(Object::Mine, 0);
        break;
    case MY_MINE_TRIPLET:  // Payload.Arm: each starts its own fuse
        for (int j : triplet(Object::Mine)) {
            Object &m = objects[j];
            if (m.fuse < 0 && !m.dud) m.fuse = m.delay >= 0 ? m.delay : mineFuse(), emit(GameEvent::MineArm, m.pos);
        }
        break;
    case MY_BARREL_TRIPLET:  // a 1-unit blast at each drum: its own explosion
        for (int j : triplet(Object::Barrel)) objects[j].dead = true;
        break;
    case MY_FLOOD:  // the Flood weapon's FloodLogicEntity
        if (int f = named("Flood"); f >= 0) water = fminf(water + WEAPONS[f].speed, Terrain::WATER + 15);
        break;
    case MY_DISARM: {  // the alliance inventory: from slot rand % 66, the first with ammo > 0 loses one (infinite never)
        int start = (int)(rand01() * 66);
        for (int k = 0; k < 66; k++) {
            int slot = (start + k) % 66, hit = -1;
            for (size_t i = 0; i < WEAPONS.size() && hit < 0; i++) if (ammo[w.team][i] > 0 && inventoryId((int)i) == slot) hit = (int)i;
            if (hit >= 0) { ammo[w.team][hit]--; break; }
        }
        break;
    }
    case MY_TELEPORT:  // 3 tries: the mine spot search (15-unit sphere 10 units up), land below, the worm fits 2 units above it
        for (int k = 0; k < 3; k++) {
            Vector3 p;
            if (!dropPoint(Object::Mine, p, 0.75f)) continue;
            Vector3 to = {p.x, p.y + 0.1f + R, p.z};
            if (terrain.solid(to)) continue;
            emit(GameEvent::Poof, w.pos, int(&w - worms.data()));
            if (&w == &worms[current]) roped = jetting = chute = false;  // NinjaRope / Jetpack / Parachute.Kill
            w.pos = to, w.vel = {0, 0, 0}, w.grounded = false;
            emit(GameEvent::Zap, w.pos, int(&w - worms.data()));
            break;
        }
        break;
    case MY_QUICK_WALK: mysteryWalk = true; break;
    case MY_LOW_GRAVITY: mysteryLow = true; break;
    case MY_DOUBLE_TIME:  // 0x50f020: the running turn timer doubled, at most 99 s
        if (phase == Phase::Aim) timer = std::min(timer * 2, 99 * 60);
        break;
    case MY_HEALTH: case MY_SUPER_HEALTH: {  // MysteryHealth / MysterySuperHealth.HealthValue, Worm.Antidote
        int hp = item == MY_HEALTH ? 25 : 100;
        w.hp += hp, w.counted += hp, w.poison = 0, w.abducted = false;
        break;
    }
    case MY_DAMAGE:  // MysteryDamage.DamageValue 25, type 0, ApplyDamage at once
        hurt(w, 25);
        w.counted = std::max(0, w.hp);
        break;
    case MY_SPECIAL_WEAPON: {  // rand % 3 of Concrete Donkey, Holy Hand Grenade, Super Airstrike: +1 unless infinite
        static const char *BIG[] = {"Concrete Donkey", "Holy Hand Grenade", "Super Airstrike"};
        int k = named(BIG[(int)(rand01() * 3)]);
        if (k >= 0 && ammo[w.team][k] >= 0) ammo[w.team][k]++;
        break;
    }
    case MY_BAD_POISON: case MY_GOOD_POISON:  // the collector's team (itself included) / every other team, by team not alliance
        for (Worm &x : worms) if (x.alive && (x.team == w.team) == (item == MY_BAD_POISON)) poisonWorm(x);
        break;
    }
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
    Vector3 to = Vector3Subtract(w, {p.x + b.pushOff.x, p.y - b.pushDepth + b.pushOff.y, p.z + b.pushOff.z});
    float d = Vector3Length(to);
    return d < b.pushReach && d > 1e-4f ? Vector3Scale(to, b.push * 1.2f * (b.pushReach - d) / b.pushReach / d) : Vector3{0, 0, 0};
}

void Game::explode(Vector3 p, const Blast &b0, float poison, int type, int weapon, const char *fx, const Vector3 *fxAt) {
    Blast b = b0;  // W4M ExplosionMessage 0x518d80: DoubleDamage doubles the radii and the impulse too (hurt() doubles the damage)
    if (doubled()) b.crater *= 2, b.reach *= 2, b.push *= 2, b.pushReach *= 2;
    for (Bubble &bb : bubbles) bb.rest = 0;  // 0x54effa: any Explosion lets it fall again
    for (size_t i = 0; i < bubbles.size();) {  // 0x54eff0: only a blast inside pops it (Health 1)
        float d = Vector3Distance(p, bubbles[i].pos);
        bool in = Vector3Distance(p, Vector3Add(bubbles[i].pos, {0, BUBBLE_UP, 0})) < BUBBLE_R && d < b.pushReach + 0.5f && d < b.crater;
        if (in && b.damage * (b.crater - d) / b.crater >= 1) { emit(GameEvent::BubblePop, bubbles[i].pos), bubbles.erase(bubbles.begin() + i); continue; }
        if (Vector3Distance(p, Vector3Add(bubbles[i].pos, {0, BUBBLE_UP, 0})) < b.pushReach + BUBBLE_SHELL) bubbleHit(bubbles[i]);  // Damage.Impulse 0x54fc41
        i++;
    }
    if (factory.on && !factory.damaged && Vector3Distance(p, Vector3Add(factory.pos, {0, 2, 0})) < b.crater + 2) factory.damaged = true, factory.die = clock + msTicks(100);
    if (b.crater > 0 && !wp(WP_MINE_RESPAWN) && !indestructible) blastLand(p, b.crater);  // Land.Indestructable: the Land Explosion handler returns (0x47356d)
    emit(b.crater >= 5 ? GameEvent::BigBoom : GameEvent::Boom, fxAt ? *fxAt : p, -1, weapon), events.back().fx = fx;
    for (Worm &w : worms) {
        if (!w.alive || int(&w - worms.data()) == dyingWorm || shielded(w, p)) continue;  // ImpulseWorm ignores kWPS_DeathThroes (0x5ad010)
        int dmg = blastDamage(b, p, w.pos);
        Vector3 kick = blastKick(b, p, w.pos);
        if (!dmg && poison <= 0 && Vector3LengthSqr(kick) == 0) continue;
        if (w.nailed && Vector3Distance(w.pos, p) < b.crater + R) w.nailed = false;  // the ground around it is gone
        if (dmg) hurt(w, dmg, true, type);
        if (poison > 0 && Vector3Distance(w.pos, p) < b.reach + R && !wp(WP_WORMS_DROWN)) w.poison = std::max(w.poison, (int)poison), w.abducted = false;  // Worm.Poison clears 0x400 (0x5addd1)
        if (w.nailed) continue;
        w.vel = Vector3Add(w.vel, Vector3Scale(kick, (wp(WP_STICKY) ? 0.5f : 1) * (w.armour ? 0.5f : 1)));  // W4M shield: half
        w.motion.air = w.motion.slide = false;  // ImpulseWorm 0x5ad010: air control off, then Ballistic or Sliding
    }
    for (Object &o : objects) {
        if (o.type == Object::Mine) {  // W4M payload 0x57f4d0: only pushed (no 1.2x), at least 0.3 of it upward; never set off
            Vector3 k = Vector3Scale(blastKick(b, p, o.pos), 1 / 1.2f);
            if (Vector3LengthSqr(k) == 0) continue;
            k.y = fmaxf(k.y, 0.3f * Vector3Length(k));
            o.vel = Vector3Add(o.vel, k), o.falling = false;
        } else if (crateLike(o)) {  // 0x5c9a10: WormDamageMagnitude over LandDamageRadius, then a Pushable crate's push
            float d = Vector3Distance(o.pos, p);
            if (d < b.crater) crateHit(o, b.damage * (doubled() ? 2 : 1) * (b.crater - d) / b.crater);
            if (o.pushable && !o.pinned) o.vel = Vector3Add(o.vel, Vector3Scale(blastKick(b, p, o.pos), 1 / 1.2f));
        } else if (Vector3Distance(o.pos, p) < b.reach) o.dead = true;
    }
    for (Trigger &t : triggers) {  // 0x5d4cb0: within WormDamageRadius + its radius, WormDamageMagnitude (1 - d²/R²), at least 1
        float r2 = (b.reach + t.radius) * (b.reach + t.radius), d2 = Vector3LengthSqr(Vector3Subtract(t.pos, p));
        if (t.gone || d2 >= r2 || (t.teamDestroy != -1 && t.teamDestroy != turnTeam())) continue;  // 0x5d47f0: CurrentTeamIndex
        float dmg = b.damage * (doubled() ? 2 : 1) * (r2 - d2) / r2;
        if ((t.hp -= (int)dmg < 1 ? (int)(dmg + 1) : (int)dmg) <= 0) t.gone = TRIG_DESTROYED;
    }
}

// 0x5c87a0 / 0x5c8a90: with TeamDestructible set, only during its AlliedGroup's turn; at 0 hp it blows up (0x5c5810)
void Game::crateHit(Object &o, float dmg) {
    if (o.teamDestroy >= 0 && (noTurn || alliance(worms[current].team) != o.teamDestroy)) return;  // ActiveWormIndex -1: none
    if ((o.hp -= (int)dmg) <= 0) o.dead = true;
}

// 0x5d5180, each update: the first worm (0x5d4d70: TeamIndex and slot) or payload in reach collects it, a sheep-like payload posting
// PayloadCollected and any other SheepCollected (0x5d4df0 / 0x5d4e40 swap them), for the active worm's team
void Game::stepTriggers() {
    for (Trigger &t : triggers) {
        for (size_t i = 0; i < worms.size() && !t.gone && (t.mask & 1); i++) {
            const Worm &w = worms[i];
            if (w.alive && Vector3Distance(t.pos, {w.pos.x, w.pos.y - R + 0.25f, w.pos.z}) < t.radius + 0.5f &&
                (t.teamCollect == -1 || w.team == t.teamCollect) && (t.wormCollect == -1 || t.wormCollect == (int)i))
                t.gone = TRIG_COLLECTED, t.collector = (int)i;
        }
        for (const Projectile &s : shots) {
            const WeaponDef &wd = WEAPONS[s.weapon];
            bool sheep = sheepLike(wd.kind) && !s.child, payload = s.child || wd.kind == Kind::Shell || wd.kind == Kind::Homing || wd.kind == Kind::Donkey;
            int team = turnTeam(), need = sheep ? t.sheepCollect : t.payloadCollect;
            if (t.gone || !(sheep || payload) || need >= 4 || !(t.mask & (sheep ? 0x80 : 8)) || (need != -1 && need != team)) continue;
            if (Vector3Distance(s.pos, t.pos) < t.radius + (s.child ? wd.csize : wd.size)) t.gone = sheep ? TRIG_PAYLOAD : TRIG_SHEEP, t.collector = current;
        }
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
        o.poison = 0, o.abducted = true, o.zap = -1, o.calm = -1;  // 0x547360: flags 0x440, poison cleared, then half its health
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
    const float PROBE = 5, FORCE = 450 * DT, MAXV = 12.5f;  // 100 units; 0.009 units/ms^2 = 450 m/s^2 (W4M adds 20 ms of it per 20 ms tick)
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

// W4M UpdateAbductee 0x5a9c40, each 20 ms in physics states 0, 2, 3 only (0x5b2045): one random spot tried per tick, the Zap
// takes the last good one once due, and only if the worm moves (else it waits MinTime..MaxTime again)
void Game::zapStep(Worm &w) {
    int wi = int(&w - worms.data());
    bool ctl = wi == current, held = w.nailed || (ctl && (roped || jetting || (chute && !w.grounded)));  // Worm.OverridePhysics (state 5)
    for (const Projectile &s : shots) held = held || (WEAPONS[s.weapon].kind == Kind::Scouser && s.stage && s.prey == wi);  // kWC_FloatAway
    if (held || (ctl && (vault.t || jumpDelay)) || (w.grounded && !w.motion.slide && !ctl)) return;  // Override, Vaulting, DetectJump, Passive
    if (w.zap < 0) { w.zap = ZAP_FIRST, w.zapFound = false; return; }
    if (--w.zap > 0 || !w.zapFound) {
        Vector3 p = Vector3Add(w.pos, {(rand01() - 0.5f) * 2 * ZAP_XZ, rand01() * ZAP_Y, (rand01() - 0.5f) * 2 * ZAP_XZ}), hit;
        // Fits 0x59edf0, then EstablishPhysicsState 0x5a6af0 drops it on the support within 1000 units; kept above Water.Level
        if (body(terrain, p) <= 0 && terrain.raycast({{p.x, p.y - R, p.z}, {0, -1, 0}}, 50, &hit) && hit.y > water) w.zapSpot = {hit.x, hit.y + R, hit.z}, w.zapFound = true;
        return;
    }
    if (Vector3LengthSqr(w.vel) > 0) {
        emit(GameEvent::Poof, w.pos, wi);  // WXP_Poof_VLarge where it was, WXP_Abductee_Teleport where it lands
        w.pos = w.zapSpot, w.vel = {0, 0, 0}, w.zapFound = false;
        emit(GameEvent::Zap, w.pos, wi);
    }
    w.zap = ZAP_MIN + (int)(rand01() * ZAP_SPAN);
    if (Vector3DistanceSqr(w.pos, w.zapSpot) > ZAP_XZ * ZAP_XZ) w.zapFound = false;
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
    auto touches = [&](Vector3 p, float r = R + 0.3f) {  // contact bits: worm index, 63 = a target
        uint64_t m = 0;
        for (size_t k = 0; k < worms.size() && k < 63; k++) if (worms[k].alive && Vector3Distance(p, worms[k].pos) < r) m |= 1ull << k;
        for (const Object &o : objects) if (o.type == Object::Target && Vector3Distance(p, o.pos) < 0.7f) m |= 1ull << 63;
        return m;
    };
    auto stick = [&](Projectile &s) {  // 0x586650: ArmOnImpact arms it, bounce damping 0 stops it; 0x575fb0 schedules Payload.Detonate PreDetonationTime later
        if (!s.stage) s.fuse = WEAPONS[s.weapon].stick;  // stopping again after a fall: the first Payload.Detonate still stands
        s.aim = Vector3Normalize(s.vel), s.vel = {}, s.stage = 1;
        emit(GameEvent::Arm, s.pos, -1, s.weapon);
    };
    // W4M 0x582050 on the tick's path: skim or splash at Water.Level + Radius (0x580830), disarmed and sinking under Water.Level -
    // SinkDepth (0x580aa0), removed without a blast crossing Water.ExpiryDepth (slot 21); a homing missile homing (+0x6e clear, 0x560bdc) only splashes.
    // True: the payload is removed
    auto wet = [&](Projectile &s, Vector3 was, bool sinks) {
        const WeaponDef &wd = WEAPONS[s.weapon];
        float size = s.child ? wd.csize : wd.size, sink = s.child ? wd.csink : wd.sink;
        auto crossed = [&](float y) { return was.y > y && s.pos.y <= y; };
        auto splash = [&](bool skim) {  // slot 19 0x580830: SplishFx on a skim, else SplashFx, 2 units over the surface (0x57eee0)
            emit(GameEvent::Splash, {s.pos.x, surfaceY(water, 2), s.pos.z}, -1, s.weapon);
            events.back().fx = skim ? "WXP_WaterSmallSplash" : wd.splash.c_str();
        };
        if (!sinks) {
            if (crossed(water)) splash(false);
        } else if (crossed(water + size)) {
            float v = Vector3Length(s.vel), pitch = v > 0 ? asinf(Clamp(s.vel.y / v, -1, 1)) : 0;
            bool skim = !s.child && wd.skim[0] >= 0 && v > wd.skim[0] && pitch > wd.skim[1];
            if (skim) s.vel = {s.vel.x * wd.skim[2], s.vel.y * wd.skim[3], s.vel.z * wd.skim[2]};
            s.pos.y = water + size;
            splash(skim);
        } else if (crossed(water - sink)) {  // Payload.SinkSpeed.Min / Max 0.08 / 0.1 units/ms
            float v = Vector3Length(s.vel), k = fminf(v, SINK_MAX);
            if (k > 0) s.vel = Vector3Scale(s.vel, k / v);
            k /= SINK_MAX;
            s.vel = {s.vel.x * k * k, -fmaxf(SINK_MIN, fabsf(s.vel.y) * k), s.vel.z * k * k};
            s.pos.y = water - sink, s.sunk = true;
        } else if (crossed(WATER_EXPIRY)) return true;
        return false;
    };
    for (size_t i = 0; i < shots.size();) {
        Projectile &s = shots[i];
        const WeaponDef &wd = WEAPONS[s.weapon];
        float fatBlast = 0;  // Fatkins: this contact's blast scale
        bool boom = false, lifeEnd = false, stuck = wd.stick > 0 && s.stage == 1 && !s.child, timed = (wd.fuse > 0 || (wd.stick > 0 && s.stage)) && !s.child;
        Vector3 np, was = s.pos;
        bool bomber = wd.kind == Kind::Airstrike && !s.child;  // the plane; fuse > 0: the steered Bovine Blitz
        if (wd.kind == Kind::Abduction) {
            if (stepUfo(s)) i++;
            else shots.erase(shots.begin() + i);
            continue;
        }
        if (s.sunk) {  // no acceleration; any contact (0x577e86) or Water.ExpiryDepth (0x582050) removes it, never a blast
            np = Vector3Add(s.pos, Vector3Scale(s.vel, DT));
            uint64_t m = touches(np);
            bool gone = terrain.solid(np) || (m & ~s.touching) || (s.pos.y > WATER_EXPIRY && np.y <= WATER_EXPIRY);
            s.pos = np, s.touching = m;
            if (gone) emit(GameEvent::Deleted, s.pos), shots.erase(shots.begin() + i);
            else i++;
            continue;
        }
        if (s.stage > 0 && (s.child || (wd.kind == Kind::Donkey && wd.clusters > 0))) { s.stage--, i++; continue; }  // a bomblet before its spawn; Fatkins in the bomber until its DropBomb
        bool walker = !s.child && (wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman || (wd.kind == Kind::Scouser && !s.stage) ||
                                   (wd.kind == Kind::SuperSheep && wd.walks && !s.stage));
        if (walker) {
            // walks in its launch direction, climbs small steps, hops at walls; W4M: the old woman and the scouser are steered
            if (wd.kind == Kind::OldWoman || wd.kind == Kind::Scouser) {
                float yaw = atan2f(s.vel.x, s.vel.z) + in.turn / 127.0f * 2 * DT;
                s.vel.x = sinf(yaw) * wd.speed, s.vel.z = cosf(yaw) * wd.speed;
            }
            Vector3 was = s.pos, sn;
            bool under = (wd.kind == Kind::OldWoman || wd.kind == Kind::Scouser) && s.pos.y + 0.8f < water;  // 0x594002: Water.Level > y + 2 Radius
            if (under && Vector3LengthSqr(s.vel) > 0) s.vel = {}, s.fuse = fminf(s.fuse, 2);  // Payload.Sink: stops, expires 2 s on (DetonatesOnExpiry)
            if (under) {
            } else if (wd.kind == Kind::OldWoman && s.stage > 0) s.stage--;  // 0x593370: state 3, still for 800 ms after a theft
            else walkerStep(terrain, s.pos, s.vel, gravity());
            if (shell(was, s.pos, &sn)) s.pos = was, s.vel = {-s.vel.x, 0, -s.vel.z};  // 0x593476: walkers turn back
            np = s.pos;
            boom = detonate && (wd.kind == Kind::Sheep || wd.kind == Kind::OldWoman);
            if (detonate && wd.kind == Kind::SuperSheep) {  // takes off: the flight gets its own lifetime
                s.stage = 1, s.fuse = wd.fuse;
                s.vel = Vector3Scale(Vector3Normalize({s.vel.x, Vector2Length({s.vel.x, s.vel.z}) * tanf(SHEEP_TAKEOFF), s.vel.z}), wd.speed);
            }
            for (Worm &v : worms) {
                int vi = int(&v - worms.data());
                if (!v.alive || vi == current || vi == s.prey || Vector3Distance(np, v.pos) > R + 0.4f) continue;
                if (wd.kind == Kind::Scouser) {  // 0x5931f7: swallows it, stops; it inflates the next update (0x591ac0)
                    s.stage = 1, s.prey = vi, s.fuse = SCOUSER_FLOAT + DT, s.vel = {}, v.nailed = false;
                    break;
                }
                if (wd.kind == Kind::OldWoman)  // W4M robs any worm but the thrower, stops 800 ms, then walks back (+0x160 negated)
                    steal(v), s.prey = vi, s.stage = msTicks(800), s.vel = {-s.vel.x, s.vel.y, -s.vel.z};
            }
        } else if (wd.kind == Kind::SuperSheep && !s.child) {
            // steered by the stick, no gravity; the Starburst steers its heading during the fuse too (Fly.* inputs, 0x558250)
            bool star = wd.name == "Starburst", lit = star && starLit(s);
            Vector3 hd = star ? s.aim : Vector3Normalize(s.vel);
            float yaw = atan2f(hd.x, hd.z) + in.turn / 127.0f * 2 * DT;
            float pitch = Clamp(asinf(Clamp(hd.y, -1, 1)) + in.aim / 127.0f * 1.5f * DT, -1.4f, 1.4f);
            hd = dirOf(yaw, pitch);
            if (star) s.aim = hd;
            s.vel = Vector3Scale(hd, star ? Vector3Length(s.vel) : wd.speed);
            boom = detonate && !lit;  // 0x589142: FIRE only once launched (+0x1c0 cleared)
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
            // slot 27 after the move (0x57fbe7): launched at fuse end, then the speed steps for the next frame
            if (star && wd.fuse - (s.fuse - DT) >= STAR_FUSE - DT / 2) s.vel = Vector3Scale(hd, starSpeed(Vector3Length(s.vel), wd.speed, s.pos.y < water));
        } else if (bomber && wd.fuse <= 0) {  // air strike plane: s.prey bombs dropped, s.stage ticks to the next
            bool lead = !s.prey && s.stage > 0;  // held at the first drop point while bombrun_start plays
            if (s.stage > 0) s.stage--;
            else spawned.push_back({s.pos, s.vel, s.weapon, 0, true, 1}), s.prey++, s.stage = strikeTicks(wd) - 1, emit(GameEvent::Launch, s.pos, -1, s.weapon);
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
        } else if (wd.kind == Kind::Scouser) {  // stage 1 caught (state 4), 2 rising through what it touches (6), 3 drifting (5): 0x5925c0
            if (s.stage == 1) {  // straight up; with land both 1.5 s up and Radius above it pops at once
                s.stage = 2, s.vel = {0, SCOUSER_RISE, 0};
                boom = terrain.solid(Vector3Add(s.pos, {0, SCOUSER_RISE * 1.5f, 0})) && terrain.solid(Vector3Add(s.pos, {0, wd.size, 0}));
            }
            // 0x592460: the payload's wind x 0.6 (0x81ae48), no gravity; a contact once clear pops it
            Vector3 a = s.stage == 3 ? Vector3{wind * WIND_ACCEL * 0.6f, 0, windZ * WIND_ACCEL * 0.6f} : Vector3{};
            bool hit = false;
            for (int k = 0, n = substeps(s.vel); k < n && !hit && !boom; k++) hit = terrain.solid(Vector3Add(s.pos, Vector3Scale(s.vel, DT * (k + 1) / n)));
            if (hit && s.stage == 3) boom = true;
            else if (!boom) {
                if (!hit) s.stage = 3;
                const Vector3 v0 = s.vel;
                s.vel = Vector3Add(s.vel, Vector3Scale(a, DT)), s.pos = Vector3Add(s.pos, arcMove(v0, s.vel, EULER_SI));
            }
            np = s.pos;
            if (s.prey >= 0 && worms[s.prey].alive) worms[s.prey].pos = Vector3Add(s.pos, {0, R, 0}), worms[s.prey].vel = s.vel, worms[s.prey].grounded = false;  // its feet at the payload (0x5927ff)
        } else if (wd.kind == Kind::Donkey && wd.clusters == 0) {  // W4M 0x553370: y = apex - 220 units/s^4 (t - tApex)^4, no gravity
            s.fuse += DT, np = s.pos;  // fuse: flight time; aim.x, aim.y: apex time and height; stage: ticks held after a smash
            if (s.stage > 0 && --s.stage == 0) s.aim = {s.fuse + DONKEY_HANG, s.pos.y + DONKEY_CURVE * powf(DONKEY_HANG, 4), 0};
            if (!s.stage) {
                float t = s.fuse - s.aim.x;
                s.vel = {0, (s.aim.y - DONKEY_CURVE * t * t * t * t - s.pos.y) / DT, 0};
                uint64_t now = 0;
                for (int k = 0, n = substeps(s.vel); k < n; k++) {  // the collider is a sphere of Radius (0x582200): any contact is a smash
                    np = Vector3Add(s.pos, Vector3Scale(s.vel, DT / n));
                    Vector3 sn;
                    if (shell(s.pos, np, &sn)) { s.pos = np, s.stage = DONKEY_HOLD; break; }  // a bubble (flags 0x3000): held, no blast (0x553be6)
                    // ours: point samples of the sphere's lower half in the voxel terrain, only while falling (a rise never hits what it left)
                    static const Vector3 DIR[] = {{0, -1, 0}, {0.7f, -0.7f, 0}, {-0.7f, -0.7f, 0}, {0, -0.7f, 0.7f}, {0, -0.7f, -0.7f}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
                    bool land = false;
                    for (const Vector3 &d : DIR) land = land || (s.vel.y < 0 && terrain.solid(Vector3Add(np, Vector3Scale(d, wd.lift))));
                    now |= touches(np, wd.lift + R);
                    if (land || (now & ~s.touching)) { boom = true, s.stage = DONKEY_HOLD; break; }
                    s.pos = np;
                }
                s.touching = now;
            }
            if (!boom && s.fuse >= DONKEY_LIFE - DT / 2) boom = lifeEnd = true, s.hits = 1;  // LifeTime 8000, DetonatesOnExpiry
        } else if (stuck) {  // in the land, waiting out PreDetonationTime
            np = s.pos;
            if (!terrain.solid(Vector3Add(s.pos, Vector3Scale(s.aim, Terrain::VOX / 2)))) s.stage = 2;  // Land.NewShape took its voxel: it falls again (0x5777f0)
        } else if (wd.kind == Kind::Donkey) {  // Fatkins 0x554e90: any contact (land, worm, object, bubble) bounces it, whatever DetonatesOn*
            const Vector3 v0 = s.vel;
            s.vel.y -= gravity() * DT;
            if (windy(s.weapon)) s.vel.x += wind * WIND_ACCEL * DT, s.vel.z += windZ * WIND_ACCEL * DT;
            const Vector3 d = arcMove(v0, s.vel);
            // its collider is a sphere of Radius (lift): ours samples the voxel land on the half facing the motion
            static const Vector3 DIR[] = {{0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {.577f, .577f, .577f}, {-.577f, .577f, .577f},
                                          {.577f, .577f, -.577f}, {-.577f, .577f, -.577f}, {.577f, -.577f, .577f}, {-.577f, -.577f, .577f}, {.577f, -.577f, -.577f}, {-.577f, -.577f, -.577f}};
            Vector3 n{};
            bool hit = false;
            uint64_t now = 0;
            np = s.pos;
            for (int k = 0, ns = substeps(s.vel); k < ns && !hit; k++) {
                Vector3 q = Vector3Add(np, Vector3Scale(d, 1.0f / ns)), sn;
                for (const Vector3 &d : DIR)
                    if (!hit && Vector3DotProduct(d, s.vel) > 0 && terrain.solid(Vector3Add(q, Vector3Scale(d, wd.lift)))) hit = true, n = terrain.normal(Vector3Add(q, Vector3Scale(d, wd.lift)));
                uint64_t m = touches(q, wd.lift + R), fresh = m & ~s.touching;
                now |= m;
                for (int j = 0; !hit && fresh && j < 64; j++)
                    if (fresh >> j & 1) hit = true, n = j < 63 ? Vector3Normalize(Vector3Subtract(q, worms[j].pos)) : Vector3Negate(Vector3Normalize(s.vel));
                if (!hit && shell(np, q, &sn)) hit = true, n = sn;
                if (!hit) np = q;
            }
            s.touching = now, s.pos = np;
            if (hit) {  // 0x580db0 -> 0x518f40: v = vt x TangentialBounceDamping 0.4 - vn x Parallel 0.6, 0 under Bounce.MinSpeed 0.03 units/ms
                float vn = Vector3DotProduct(s.vel, n);
                s.vel = vn < 0 ? Vector3Subtract(Vector3Scale(Vector3Subtract(s.vel, Vector3Scale(n, vn)), 0.4f), Vector3Scale(n, vn * 0.6f)) : Vector3Scale(n, Vector3Length(s.vel) * 0.6f);
                if (Vector3Length(s.vel) < FATKINS_STOP) s.vel = {};
                else emit(GameEvent::Bounce, np, -1, s.weapon);  // Payload.Bounce: BounceFx / BounceSfx
                s.prey++;
                boom = Vector3LengthSqr(s.vel) == 0 || s.prey > FATKINS_BOUNCES;  // Payload.Rest, DetonatesAtRest: Detonate (0x5758c0)
                fatBlast = 1 - 0.2f * (s.prey - 1);  // radii x 1, 0.8, 0.6, 0.4: blasted after the Detonate (0x555066)
            }
        } else {
            bool homing = wd.kind == Kind::Homing && (s.fuse += DT) > HOMING_LOCK && s.fuse < HOMING_LOCK + homingTime(wd);  // fuse: flight time
            const Vector3 v0 = s.vel;
            if (homing) {  // 0x561730: the step, then the avoidance
                s.vel = homingStep(s.vel, s.pos, s.aim, wd.avoid ? 12.5f : HOMING_MAX);
                if (wd.avoid) avoidLand(s);
            }
            else s.vel.y -= gravity() * (s.child && wd.kind != Kind::Airstrike ? 1 : wd.grav) * DT;  // W4M bomblets: IsLowGravity 0
            if (wd.kind == Kind::Airstrike && wd.fuse > 0) s.vel.y = fmaxf(s.vel.y, -COW_CHUTE);  // the bomber's cows come down under a chute
            if (windy(s.weapon, s.child)) s.vel.x += wind * WIND_ACCEL * DT, s.vel.z += windZ * WIND_ACCEL * DT;  // 0x57eb25: (cos, 0, sin) Wind.Direction
            bool impact = s.child || wd.fuse == 0;
            uint64_t now = 0;
            const Vector3 d = arcMove(v0, s.vel, arcLag(wd));
            for (int k = 0, n = substeps(s.vel); k < n && !boom; k++) {  // W4M searches the whole path for the first land contact
                np = Vector3Add(s.pos, Vector3Scale(d, 1.0f / n));
                Vector3 sn;
                if (shell(s.pos, np, &sn)) {  // the land bounce response (vtable +0x80), Bubble.Hit
                    if (impact && wd.stick > 0) { stick(s); break; }
                    if (impact) { boom = true; break; }
                    s.vel = Vector3Scale(Vector3Reflect(s.vel, sn), wd.bounce);
                    break;
                }
                if (terrain.solid(np)) {
                    if (impact && wd.stick > 0) { stick(s); break; }
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
        bool expired = false;
        if (!boom && !bomber && !stuck && wd.kind != Kind::Scouser && wd.kind != Kind::OldWoman) {  // the walking payloads sink their own way (0x594002)
            expired = wet(s, was, !(wd.kind == Kind::Homing && s.fuse > HOMING_LOCK && s.fuse < HOMING_LOCK + homingTime(wd)));
            timed = timed && !s.sunk && !expired;
        }
        timed = timed && (!wd.restFuse || s.fuse < wd.fuse || Vector3Length(s.vel) < 1);
        if (timed && wd.restFuse && s.fuse == wd.fuse) emit(GameEvent::Hallelujah, s.pos, -1, s.weapon);  // at rest: the choir, then the blast
        if (timed && (s.fuse -= DT) < DT / 2) boom = true;  // n s = exactly 60 n ticks, whatever the float drift
        // off the map a shot flies on until it falls into the sea
        bool gone = expired || (wd.kind == Kind::Homing && s.fuse >= HOMING_LIFE) || s.pos.x < -100 || s.pos.z < -100 || s.pos.x > W + 100 || s.pos.z > W + 100;
        if (boom && bomber) {  // flies off
        } else if (boom && wd.kind == Kind::Scouser) {  // W4M: pops and drops its catch, empty it bursts harmlessly
            emit(GameEvent::Boom, s.pos, -1, s.weapon);
            if (s.prey >= 0 && worms[s.prey].alive) hurt(worms[s.prey], (int)(wd.damage * superScale(containerOf(s.weapon, false)).x));
        } else if (boom) {
            s.pos = np;
            if (wd.stick > 0 && !stuck) emit(GameEvent::Arm, np, -1, s.weapon);  // a worm hit: armed by the impact, detonated in the same call (0x586520)
            Blast b = superBlast(blastOf(wd, s.child), containerOf(s.weapon, s.child));
            Vector3 at = np;
            bool smash = wd.kind == Kind::Donkey && wd.clusters == 0;
            if (smash && !lifeEnd && !wp(WP_MINE_RESPAWN) && !indestructible) blastLand(np, wd.lift * (doubled() ? 2 : 1));
            if (smash && !lifeEnd) at = Vector3Subtract(np, {0, wd.lift, 0});  // 0x553970: a land-only blast of Radius at the centre, then Explode Radius below it
            else if (smash) at = np;  // expiry: Detonate at the entity
            explode(at, b, s.child ? 0 : wd.poison, (size_t)s.weapon >= baseWeapons ? 4 : wd.name == "Cluster Grenade" ? 2 : 0, smash || wd.stick > 0 ? s.weapon : -1,
                    fatBlast > 0 ? "WXP_ExplosionX_Large" : nullptr);  // Fatkins: its DetonationFx; kind by container name prefix kWeaponCluster/Factory (0x57f32c)
            if (wd.name == "Starburst" && !s.child && worms[current].alive) vapourize(worms[current]);  // Detonate 0x588dd0: the blast, then Worm.Vapourize
            bool fly = !s.child && ((wd.kind == Kind::Homing && !wd.avoid) || (wd.kind == Kind::SuperSheep && (wd.name == "Starburst" ? !starLit(s) : !wd.walks || s.stage)));
            if (fly) camHold = msTicks(1000);  // the FlyCam's: CAMTWK PauseDuration 1000 (homing, super sheep, starburst)
            if (!s.child && wd.poison > 0 && (wd.fuse > 0 || wd.stick > 0)) gas.push_back({Vector3Add(np, {0, 0.5f, 0}), GAS_LIFE, wd.poison});  // timed poison shell: the gas canister; collider offset (0, 10, 0) units
            if (!s.child && wd.kind != Kind::Airstrike && wd.kind != Kind::Donkey) bomblets(np, s.weapon, spawned);
        }
        if (fatBlast > 0) {  // 0x555066: WormDamage / ImpulseMagnitude kept, the three radii scaled, the impulse at the centre
            Blast b = superBlast(blastOf(wd, false), containerOf(s.weapon, false));
            b.crater *= fatBlast, b.reach *= fatBlast, b.pushReach *= fatBlast, b.pushDepth = 0;
            explode(s.pos, b, 0, 0, s.weapon, wd.radius * fatBlast < 3 ? "WXP_Explosion_Small" : "WXP_ExplosionX_Med");  // LandDamageRadius x scale < 60 units
        }
        if (!boom && !s.child && wd.name == "Starburst" && !starLit(s) && worms[current].alive) {  // 0x5891e0: attached at launch, takes the rocket's position and velocity
            Worm &r = worms[current];
            r.pos = s.pos, r.vel = s.vel, r.grounded = false, r.yaw = atan2f(s.aim.x, s.aim.z);
        }
        if ((boom && --s.hits <= 0) || gone) {
            if (!bomber) emit(GameEvent::Deleted, s.pos);  // the bomber planes are no payloads (BomberLogicEntity)
            shots.erase(shots.begin() + i);
        } else i++;
    }
    shots.insert(shots.end(), spawned.begin(), spawned.end());
}

void Game::step(const Input &raw) {
    events.clear();  // also when over: GameOver must reach the listeners once
    if (phase == Phase::GameOver && !scriptMovieOn(*this)) return;  // the world runs on under the EFMV.GameOverMovie
    if (raw.flags & Input::SKIP_MOVIE) scriptSkipMovie(*this);
    if (raw.flags & Input::DRAW) { phase = Phase::GameOver, winner = -1, emit(GameEvent::GameOver, {0, 0, 0}); return; }  // the round to "Nobody" (0x4fd105)
    Input in = scriptMovieOn(*this) ? Input{} : raw;  // a movie leaves only its own input group (EFMVMovie, 0x5074a0): no worm control
    if (wp(WP_NO_BLIMP)) in.buttons &= ~Input::TARGET;  // Camera.Disable "Blimp" (0x5d6fea): targeting from the aim view only
    ropeIn = {};
    int chosen = (in.buttons & Input::NEXT_WEAPON) && in.aim ? (uint8_t)in.aim - 1 : -1;
    if (chosen >= 0) in.aim = 0;
    // user-requested (2026-10-03, retest vs W4M later): the press that cancels the hot seat (W4M 0x50fce0) is never a shot, jump or view change
    uint8_t keep = Input::TARGET | Input::NEXT_WEAPON;
    if (phase == Phase::Aim && hotSeat > 0 && (in.buttons & ~keep)) hotSeat = 0, in.buttons &= keep;
    uint8_t pressed = in.buttons & ~prevButtons;
    prevButtons = raw.buttons;
    Worm &w = worms[current];
    bool detonate = phase != Phase::Aim && (pressed & Input::FIRE), tool = roped || jetting || hooked();
    if (camHold > 0) camHold--;
    Phase before = phase;
    if (in.walk || in.turn || (pressed & Input::JUMP)) changing = false;  // W4M 0x59a6b9: a move or jump ends the worm select

    if (phase == Phase::Aim && hotSeat > 0) hotSeat = in.turn || in.walk || in.aim || (in.buttons & ~Input::TARGET) || in.flags ? 0 : hotSeat - 1;
    bool aimCursor = phase == Phase::Aim && (in.buttons & Input::TARGET) && !tool;  // walk and aim drive the cursor, whatever the weapon
    if (w.alive && icarus != 3 && (phase == Phase::Aim || retreating())) {  // drinking: W4M Worm.DisableMovementRef
        bool head = in.buttons & Input::HEADING;  // W4M 0x5b107c: walking sets Orientation to the input at once; the jetpack turns at 0x561e40's rate
        float rate = head ? wrapPi(in.turn * PI / 128 - w.yaw) / DT : in.turn / 127.0f * 2.5f, lim = jetting ? JET_TURN : head ? PI / DT : 2.5f;
        if (!aimCursor && !vault.t && !jumpDelay && !roped && !(chute && !w.grounded)) w.yaw += Clamp(rate, -lim, lim) * DT;  // W4M Vaulting keeps the Orientation; the rope's 0x5729e0 too
        if (aimCursor && blimped(weaponDef(weapon).kind)) {  // W4M IsometricCam 0x52a5e0
            if (!cursorOn) cursorYaw = w.yaw, cursorPitch = BLIMP_PITCH, cursor = blimpFocus(w.pos, w.yaw), cursorOn = true;
            cursorYaw += in.turn / 127.0f * BLIMP_TURN * DT;
            bool tilt = in.buttons & Input::PITCH;
            if (tilt) cursorPitch = Clamp(cursorPitch - in.aim / 127.0f * BLIMP_TILT * DT, 0, PI / 2);  // stick up: RotateUp
            float v = CURSOR_SPEED * DT / 127, y = cursorYaw, side = tilt ? 0 : in.aim;
            cursor.x += (sinf(y) * in.walk - cosf(y) * side) * v, cursor.z += (cosf(y) * in.walk + sinf(y) * side) * v;
            Vector3 c = landCenter(), off = Vector3Subtract(cursor, c);
            if (Vector3Length(off) > BLIMP_RANGE) cursor = Vector3Add(c, Vector3Scale(Vector3Normalize(off), BLIMP_RANGE));
        }
        blimp = aimCursor && blimped(weaponDef(weapon).kind);
        if (phase == Phase::Aim && weaponDef(weapon).kind == Kind::Girder) {
            if (!girderOn)  // W4M Update state 0: eye level (Worm.EyeLevelOffset 15 units), 40 units ahead
                girderOn = true, girderFrom = w.pos, cursorYaw = w.yaw, girderWait = 0,
                girder = Vector3Add(w.pos, {sinf(w.yaw) * 2, 0.75f, cosf(w.yaw) * 2});
            if (aimCursor) stepGirder(in, pressed);
        } else girderOn = false;
        const float ws = WALK_SPEED * walkScale();
        const int wt = walkTick;
        walkTick = 0;
        if (vault.t) {  // W4M 0x5ab3d0: the camera-relative stick, here the heading or the facing
            float y = head ? in.turn * PI / 128 : w.yaw;
            vaultStep(w.pos, vault, aimCursor ? Vector3{} : Vector3Scale(flat(y), in.walk));
        } else if (w.grounded && !w.motion.slide && in.walk && !aimCursor && !jumpDelay && !w.nailed && !artillery()) {  // W4M Sliding: no walking
            Vector3 walkV = Vector3Scale(flat(w.yaw), in.walk / 127.0f * INPUT_IMPULSE), was = w.pos, gn;  // W4M Velocity = InputImpulse (0x546f10)
            if (!walkFrame(walkTick = wt + 1)) {
            } else if (walkStep(terrain, w.pos, w.yaw, in.walk / 127.0f * ws * WALK_FRAME, &vault, &gn)) w.vel = walkV, w.motion.air = true;  // Fall(InputImpulse) 0x5b14c7, air control on
            else if (vault.t) vault.vel = walkVel;  // the vault start writes no Velocity: the last step's
            else {
                if (!Vector3Equals(was, w.pos)) walkVel = walkV;  // a blocked step leaves Velocity as it was
                slideIfSteep(gn, w.vel, w.motion, walkV, pot);
            }
        } else if (!vault.t) walkVel = {};  // idle branch 0x5b1c1b zeroes Velocity
        steerIn = aimCursor ? Vector3{} : Vector3Scale(head ? flat(in.turn * PI / 128) : flat(w.yaw), in.walk / 127.0f);
        Vector3 jv;
        if ((pressed & Input::JUMP) && !jumpDelay && !vault.t && w.grounded && !w.motion.slide && !tool && !w.nailed && !wp(WP_NO_JUMPING) && !artillery()) jumpDelay = JUMP_WINDOW, jumpKind = 2;
        else if (int ev = jumpDelay ? jumpTick(jumpDelay, jumpKind, in, pressed, w.yaw, jv) : 0; ev && w.grounded) {
            w.vel = jv, w.grounded = false, w.motion.air = true;  // DetectJump: Flags |= 1
            emit(GameEvent::Jump, w.pos, current, ev);  // weapon: the W4M kWE code (animation)
        }
    } else {
        jumpDelay = 0, steerIn = {}, walkTick = 0;
        if (vault.t) vaultStep(w.pos, vault, {});  // control gone: no input, back to the old pos
    }
    // rope and jetpack outlast the attack: still steered while the shot flies and during the retreat
    // observed in W4M (user, 2026-10-03): after the secondary drop a landed jetpack takes off again during the retreat while fuel lasts
    if (w.alive && (phase == Phase::Flying || phase == Phase::Retreat) && jetLanded() && (pressed & Input::FIRE)) {
        if (ambulatory(w)) emit(GameEvent::JetStart, w.pos, current, weapon);
        jetting = tool = true, thrust = weaponDef(weapon).speed, boost = 0;
        emit(GameEvent::Fire, w.pos, current, weapon);
    }
    if (w.alive && (phase == Phase::Aim || (tool && (phase == Phase::Flying || phase == Phase::Retreat)))) {
        bool armed = phase == Phase::Aim && !utility(weaponDef(weapon).kind);  // a weapon in hand: FIRE and the aim axis are its own
        ropeIn = {(int8_t)ropeSwing(in, w.yaw), armed ? (int8_t)0 : in.aim};
        if (roped) {  // 0x573530 on letting go; the swing itself runs with the worms (ropeTick)
            if (pressed & Input::JUMP) roped = false, w.vel = ropeRelease(rope, {w.pos.x, w.pos.y - R, w.pos.z}, ropeIn.swing);
        } else if (Object *o = hooked()) {
            if (pressed & Input::JUMP) o->hooked = false, o->vel = ropeRelease(rope, ropePoint(*o), ropeIn.swing);  // NinjaRope.EndSwing
        } else if (jetting) {  // W4M 0x562810 every 20 ms, here per tick; FIRE held = FireUtil, whatever the hand holds
            bool burn = in.buttons & Input::FIRE;
            if ((w.grounded && !burn) || (burn && fuel <= JET_DRY)) jetting = false;  // landed (0x562f72), or dry (0x562990): it falls
            else {
                const float n = DT / 0.02f, h = fmaxf(w.pos.y - water, 0);  // W4M steps per tick; height over Water.Level
                Vector3 f = flat(w.yaw), a = {0, 0, 0};
                float want = PI * in.turn / 128;  // W4M InputImpulse: the move stick's direction (HEADING); none from the D-pad alone
                bool fwd = in.walk > 1, along = fwd && (in.buttons & Input::HEADING) && sinf(want) * w.vel.x + cosf(want) * w.vel.z > 0;  // InputImpulse . Velocity > 0 (0x5628ef, 0x562b81)
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
        }
        if (phase == Phase::Aim && !tool) armed = true;
        if (phase == Phase::Aim) {
            if (armed && !aimCursor) w.pitch = Clamp(w.pitch + in.aim / 127.0f * 1.5f * DT, -1.2f, 1.45f);
            if (chosen >= 0 && !shotsLeft) pick(w.team, chosen);
            else if ((pressed & Input::NEXT_WEAPON) && !shotsLeft) nextWeapon(w.team);
            if (weaponDef(weapon).userFuse) fuses[w.team] = std::clamp(fuses[w.team] + !!(pressed & Input::FUSE_UP) - !!(pressed & Input::FUSE_DOWN), 1, 5);
            bool spare = jetUsed && weaponDef(weapon).kind == Kind::Jetpack;  // takes off again without ammo
            // Fire.Second: JUMP in flight, PITCH without TARGET on a landed jetpack (UtilityFire stays on: FIRE takes off, JUMP jumps)
            uint8_t second = jetting ? Input::JUMP : jetLanded() ? (in.buttons & Input::TARGET ? 0 : Input::PITCH) : Input::FIRE;
            bool drop2 = secondary >= 0 && (pressed & second) && usable(w.team, secondary) && !w.nailed;
            if (drop2) {  // W4M UtilityFire group 0x4e1d50: Fire.Second fires the secondary, the tool stays in hand
                int tool = weapon;
                weapon = secondary, secondary = -1, power = 0;
                use(w);
                weapon = tool;
            } else if (armed && weapon >= 0 && ((ammo[w.team][weapon] && !delays[w.team][weapon]) || shotsLeft || spare) && !(w.nailed && !nailUsable(weaponDef(weapon).kind))) {
                Vector3 h;
                // W4M CanFire is asked on Input.FirePressed only (0x586092); a charge started goes on to its release
                if (!powered(weaponDef(weapon).kind)) { if ((pressed & Input::FIRE) && fireable(w) && !(aimCursor && cursorOn && !blimpHit(&h))) use(w); }  // W4M: no target, NotClearToFire
                else if (weaponDef(weapon).kind == Kind::Homing && !locked) {  // W4M state 1: FIRE takes the aim ray's target, no charge
                    if ((pressed & Input::FIRE) && fireable(w)) {  // from the Blimp the cursor point, else the aim ray
                        if (!aimCursor) locked = true, lockAt = target();
                        else if (cursorOn && blimpHit(&h)) locked = true, lockAt = h;
                    }
                } else {
                    if ((in.buttons & Input::FIRE) && (power > 0 || ((pressed & Input::FIRE) && fireable(w)))) power = fminf(1, power + DT / 1.5f);
                    if (power > 0 && (!(in.buttons & Input::FIRE) || power >= 1)) use(w);
                }
            }
        }
    }

    if (secondary >= 0 && !toolOut() && !jetLanded()) weapon = secondary, secondary = -1, jetUsed = false, fuel = jetInit;
    if (ropeUsed >= 0 && weapon != ropeUsed) ropeCleanup();  // W4M 0x565920: rope, chute, jetpack dry
    if (icarus && (weaponDef(weapon).kind != Kind::Icarus || !w.alive)) icarus = 0, drift = {};  // a weapon change deletes it (0x587540)
    if (bubbleAt >= 0 && (weaponDef(weapon).kind != Kind::Bubble || !w.alive)) bubbleAt = -1;  // Weapon.Delete ends the utility first
    if (bubbleAt >= 0 && clock >= bubbleAt) {  // 0x550190: spawned beside the worm, then falls freely
        Vector3 f = flat(w.yaw), side = {cosf(w.yaw), 0, -sinf(w.yaw)};
        // velocity 0.02 units/ms along (forward - up), orientation yaw + 1.3439 rad (0x55021d, 0x55031c)
        bubbles.push_back({Vector3Add(w.pos, Vector3Add(Vector3Add(Vector3Scale(f, 0.623f), Vector3Scale(side, -0.097f)), {0, 0.47f, 0})), {f.x, -1, f.z}, 6, w.yaw + 1.3439f});
        emit(GameEvent::BubbleNew, bubbles.back().pos);
        int &n = ammo[w.team][weapon];
        n -= n > 0, bubbleAt = -1;
    }
    if (icarus == 3 && clock >= flapAt) {  // 0x587750 at the PostLaunchDelay: Worm.Antidote; its heal up to WormData InitialEnergy never
        w.poison = 0, w.abducted = false;     // fires: no code writes that field (only the serializer, data default 0), so hp >= it
        icarus = 1;
    }
    if (icarus == 1 && !w.grounded) icarus = 2, flapAt = clock + FLAP_WAIT, drift = {};  // took off: PackAccessory.Wield
    else if (icarus == 2 && w.grounded) icarus = 1;  // landed: the wings fold
    if (icarus == 2) {  // 0x587970: JUMP and back-jump flap alike; too early restarts the wait, a missed window waits a beat
        while (clock >= flapAt + FLAP_WAIT) flapAt += FLAP_BEAT;
        Vector3 was = drift, f = flat(w.yaw);
        drift = Vector3Add(drift, Vector3Scale(f, in.walk / 127.0f * AFTERTOUCH));  // WXWorm.AftertouchDelta 0.015 u/ms a frame
        if (Vector3Length(drift) > AFTERTOUCH_MAX) drift = Vector3Scale(Vector3Normalize(drift), AFTERTOUCH_MAX);  // AftertouchStrength 0.1
        w.vel = Vector3Add(w.vel, Vector3Subtract(drift, was));
        if (pressed & Input::JUMP) {
            if (clock < flapAt) flapAt = clock + FLAP_WAIT;
            else {
                bool sky = w.pos.y > (Terrain::NY - 4) * Terrain::VOX;  // W4M: at the skybox Sun locator's y (0x587acb); our maps lack it, so the map top
                w.vel = {drift.x, sky ? 0 : FLAP, drift.z}, flapAt += FLAP_BEAT;
                emit(GameEvent::Jump, w.pos, current);
            }
        }
    }
    if (scout.t >= 0) scout.t = weaponDef(weapon).kind == Kind::Binoculars && phase == Phase::Aim ? scout.t + 1 : -1;  // LeaveBinocularsVision
    // W4M 0x579720: the parachute in hand opens by itself under -0.3 x 0.75 units/ms
    if (!chute && !tool && w.alive && !w.grounded && w.vel.y < -FALL_SAFE * 0.75f && (phase == Phase::Aim || retreating()) &&
        weaponDef(weapon).kind == Kind::Parachute && ammo[w.team][weapon])
        use(w);
    // W4M 0x5833a0 / 0x54a0e0: PostLaunchDelay, then StartRetreatTimer (RetreatTimeOverride or DefaultRetreatTime), the shot still flying
    if (before != phase && phase == Phase::Flying) timer = msTicks(weaponDef(launched >= 0 ? launched : weapon).postLaunch) + retreatTicks(weaponDef(launched >= 0 ? launched : weapon));
    if (chute && !w.grounded) {  // W4M 0x579720 per 20 ms (ours per tick, K steps): sway 0x578a40, then the open update 0x5792a0
        const float K = DT / 0.02f, sn = sinf(chuteAng);
        int dir = chuteSteer(steerIn, w.yaw);
        w.yaw = wrapPi(w.yaw + (0.02f * dir - 0.01f * sn) * K), chuteSpin -= 0.001f * dir * K;  // +-0.02 rad a step, the swing's own -0.01 sin
        chuteSpin = (chuteSpin - 0.003f * sn * K) * powf(0.99f, K), chuteAng += chuteSpin * K;
        const float c = cosf(chuteAng), was = chuteSink;  // swung past acos 0.4 it sinks, else the sink decays into glide
        if (c < 0.4f) chuteSink += 2 * (c - 0.4f) * gravity() * 1e-3f * K;
        else chuteSink *= powf(2.0f / 3, K), chuteGain += chuteSink - was;
        float g = chuteGain * (1 - powf(6.0f / 7, K)), gcap = 0.05f * K;  // 0x47a1a0(c0, 0, 6, 0.001): 1/7 back to 0, 0.001 units/ms at most
        chuteGain -= Clamp(g, -gcap, gcap);
        Vector3 f = flat(w.yaw);  // half the gap to 0.06 units/ms + c0 along the facing + the drift, 0.05 at most
        float k = 1 - powf(0.5f, K);
        Vector3 d = Vector3Scale(Vector3Subtract(Vector3Add(Vector3Scale(f, CHUTE_GLIDE + chuteGain), chuteDrift), w.vel), k);
        float l = Vector3Length(d), cap = CHUTE_STEP * K;
        w.vel = Vector3Add(w.vel, l > cap ? Vector3Scale(d, cap / l) : d);
        w.vel.y += chuteSink * 2 * k;  // v.y += s5c a step after the halving: the same rest speed
        chuteAt = Vector3Add(chuteAt, Vector3Scale(w.vel, DT));
    }
    if (vault.t && Vector3Distance(w.pos, vault.to) > Vector3Distance(vault.from, vault.to) + 0.01f) vault.t = 0;  // moved by a weapon
    else if (vault.t && (roped || jetting || !w.grounded || Vector3LengthSqr(w.vel) > 0)) w.pos = vault.to, vault.t = 0;  // W4M ChangeState 0x5aa847: snaps to the target
    bool star = false;  // Worm.OverridePhysics bit 4 (0x588580): the Starburst holds its worm, standing through the fuse, on the rocket after
    for (const Projectile &q : shots) star |= !q.child && WEAPONS[q.weapon].name == "Starburst";
    for (Worm &x : worms)
        if (star && &x == &w) {
        } else if (roped && &x == &w) {
            Vector3 feet = {x.pos.x, x.pos.y - R, x.pos.z};
            x.motion.slide = false;
            ropeTick(rope, feet, x.vel, ropeIn.swing, ropeIn.aim, current);
            x.pos = {feet.x, feet.y + R, feet.z};
            if (underwater(x)) drown(x);
        } else stepWorm(x);
    if (grapple.on) {
        Vector3 hit;
        int obj = -1, r = w.alive && weaponDef(weapon).kind == Kind::Rope ? grappleStep(grapple, {w.pos.x, w.pos.y - R, w.pos.z}, ropeMax, ambulatory(w), &hit, &obj) : 0;
        grapple.on = r == 1;
        if (r == 2) ropeShots++, ropeUsed = weapon, ropeOn(hit);  // 0x573ea3: NumShots counts the hooks on land
        if (r == 3) {  // 0x571d90: the object swings about the worm's feet, its plane facing away from the worm, no spin
            Object &o = objects[obj];
            Vector3 feet = {w.pos.x, w.pos.y - R, w.pos.z}, d = Vector3Subtract(ropePoint(o), feet);
            float l = Vector3Length(d);
            o.hooked = true, o.falling = false, ropeUsed = weapon;
            rope.n = 1, rope.pt[0] = feet, rope.len[0] = l, rope.side[0] = {}, rope.yaw = atan2f(d.x, d.z) + PI, rope.spin = 0, rope.swung = true;
            rope.angle = acosf(Clamp(l > 0 ? -d.y / l : 1, -1, 1));
        }
    }
    if (!roped && !grapple.on && !hooked() && (w.grounded || vault.t || jetting || chute)) rope.swung = false;  // 0x574b96: idle off Ballistic
    stepShots(in, detonate);
    if (Object *o = hooked(); o && w.alive && phase != Phase::Settle) {  // 0x5cbc81 / 0x5d2110: the hooked object runs the rope's update
        Vector3 p = ropePoint(*o), off = Vector3Subtract(o->pos, p);
        ropeTick(rope, p, o->vel, ropeIn.swing, ropeIn.aim, -1, int(o - objects.data()));
        o->pos = Vector3Add(p, off);
    }
    stepObjects();
    stepTriggers();
    stepFactory();
    wobbleStep(raw.zoom / 255.0f);
    for (size_t i = 0; i < bubbles.size();) {  // 0x54f160: falls until it rests on land, gone under water
        Bubble &b = bubbles[i];
        b.age++;
        if (!b.rest) {  // stops where it is once the next step of its parabola meets land (0x466ae0, 20 steps); Explosion clears it
            Vector3 to = Vector3Add(b.pos, Vector3Add(Vector3Scale(b.vel, DT), {0, -0.5f * gravity() * DT * DT, 0})), d = Vector3Subtract(to, b.pos), hit;
            float l = Vector3Length(d);
            b.rest = l > 0 ? terrain.raycast({b.pos, Vector3Scale(d, 1 / l)}, l, &hit) : terrain.solid(b.pos);
            for (const Worm &x : worms)  // mask 1 (kCF_Worm): worm spheres, radius 10 units centred 5 above the feet (0x5a9bbb)
                if (x.alive && !b.rest) {
                    Vector3 c = {x.pos.x, x.pos.y - R + 0.25f, x.pos.z}, e = Vector3Subtract(c, b.pos);
                    float t = l > 0 ? Clamp(Vector3DotProduct(e, d) / (l * l), 0, 1) : 0;
                    b.rest = Vector3Distance(c, Vector3Add(b.pos, Vector3Scale(d, t))) < 0.5f;
                }
            if (!b.rest && b.pos.y < water) { emit(GameEvent::BubblePop, b.pos), bubbles.erase(bubbles.begin() + i); continue; }
            if (!b.rest) b.pos = Vector3Add(b.pos, arcMove(b.vel, {b.vel.x, b.vel.y - gravity() * DT, b.vel.z}, EULER_X)), b.vel.y -= gravity() * DT;
        }
        i++;
    }
    for (size_t i = 0; i < gas.size();) {
        Gas &c = gas[i];  // WXP_GasCloud's ParticleMass 0 cancels its wind (0x5b7450): the cloud stays put
        for (Worm &x : worms)
            if (x.alive && Vector3Distance(x.pos, c.pos) < GAS_RADIUS && !wp(WP_WORMS_DROWN)) x.poison = std::max(x.poison, (int)c.poison), x.abducted = false;
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

    clock++;  // TimerLogicEntity 0x50f17d: the round clock also runs in the hot seat
    // stdvs Worm_Died: the turn ends once fewer than two teams stand (worms die at their blast, not at 0 hp)
    int left = 0;
    for (int t = 0; t < teams; t++) left += standing(t);
    bool over = !cfg.mission && left <= 1;
    switch (phase) {
    case Phase::Aim:  // a script ends the turn through its Timer_* / Worm_Damaged_Current callbacks (stdlib EndTurn)
        if (script) { if (!hotSeat && !scriptMovieCamera(*this) && --timer <= 0) scriptEvent(*this, "Timer_TurnTimedOut"); }  // 0x50f383
        else if (!w.alive || selfHurt || over || (!hotSeat && --timer <= 0)) { phase = Phase::Settle; timer = 1; roped = jetting = chute = false; }
        break;
    case Phase::Flying:
        if (shots.empty()) phase = Phase::Retreat;
        [[fallthrough]];
    case Phase::Retreat:  // W4M Timer_RetreatTimedOut / Worm_Damaged_Current -> EndTurn, which waits for the shots (ObjectCount.Active)
        if (script) { if (--timer <= 0) scriptEvent(*this, "Timer_RetreatTimedOut"); }
        else if (!w.alive || selfHurt || over || --timer <= 0) phase = Phase::Settle, timer = 1, jumpDelay = 0, roped = jetting = chute = false;
        break;
    case Phase::Settle:  // stdlib.lub EndTurn: timer > 0 WaitUntilNoActivity, < 0 PostActivityTime; no timeout, as W4M
        if (in.flags & Input::SKIP_COUNT) countT = std::max(countT, COUNT_DAMAGE);  // observed in W4M by the user, 2026-10-03: X ends the display, deaths follow as usual
        if (!countGroup.empty() || !deathQueue.empty() || dyingWorm >= 0) stepCount();  // displays and dying worms are active objects
        else if (timer < 0) {
            if (++timer < 0) break;
            if (script) scriptEvent(*this, "Timer_PostActivityTimedOut");
            else applyDamage(), checkActivity();  // Timer_PostActivityTimedOut: AboutToApplyDamage, ApplyDamage, CheckActivity
        } else if (shots.empty() && !active()) {
            if (script) scriptNoActivity(*this);
            else timer = -POST_ACTIVITY;  // GameLogic_NoActivity -> Timer.StartPostActivity
        }
        break;
    case Phase::GameOver: break;
    }
    if (cfg.mission && (phase != Phase::GameOver || script)) missionStep(*this);
}

// W4M 0x558c30: one 6-unit step per cycle, camera-relative, Right > Left > Back > Forward, raise / lower besides
void Game::stepGirder(const Input &in, uint8_t pressed) {
    cursorYaw += in.turn / 127.0f * GIRDER_YAW * DT;  // GirderCamera YawSpeed 0.4
    if (girderWait > 0 && --girderWait) return;
    bool tilt = in.buttons & Input::PITCH;
    int side = tilt ? 0 : in.aim;
    Vector3 fwd = flat(cursorYaw), right = {-cosf(cursorYaw), 0, sinf(cursorYaw)}, d = {0, 0, 0};
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
                const Vector3 v0 = vel;
                vel.y -= g * DT, pos = Vector3Add(pos, arcMove(v0, vel));
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
    mix(&walkTick, sizeof walkTick);
    for (const Worm &w : worms) { mix(&w.pos, sizeof w.pos); mix(&w.vel, sizeof w.vel); mix(&w.hp, sizeof w.hp); mix(&w.yaw, sizeof w.yaw); mix(&w.pitch, sizeof w.pitch); mix(&w.alive, sizeof w.alive); mix(&w.poison, sizeof w.poison); mix(&w.counted, sizeof w.counted); mix(&w.nailed, 1); mix(&w.armour, 1); mix(&w.motion.stuck, sizeof w.motion.stuck), mix(&w.motion.air, 1), mix(&w.motion.slide, 1), mix(&w.motion.spin, sizeof w.motion.spin), mix(&w.motion.spinTo, sizeof w.motion.spinTo), mix(&w.motion.normal, sizeof w.motion.normal); if (w.drowned) mix(&w.floatT, sizeof w.floatT); if (w.abducted) mix(&w.zap, sizeof w.zap), mix(&w.calm, sizeof w.calm), mix(&w.zapFound, 1), mix(&w.zapSpot, sizeof w.zapSpot); }
    for (const Projectile &s : shots) { mix(&s.pos, sizeof s.pos); mix(&s.vel, sizeof s.vel); mix(&s.weapon, sizeof s.weapon); mix(&s.fuse, sizeof s.fuse); mix(&s.hits, sizeof s.hits); mix(&s.stage, sizeof s.stage); mix(&s.prey, sizeof s.prey); mix(&s.aim, sizeof s.aim); mix(&s.touching, sizeof s.touching); }
    mix(&chute, 1);
    if (chute) mix(&chuteDrift, sizeof chuteDrift), mix(&chuteAt, sizeof chuteAt), mix(&chuteAng, sizeof chuteAng), mix(&chuteSpin, sizeof chuteSpin),
               mix(&chuteSink, sizeof chuteSink), mix(&chuteGain, sizeof chuteGain);
    mix(&walkVel, sizeof walkVel);
    mix(&windZ, sizeof windZ);
    mix(surrendered.data(), surrendered.size());
    mix(&ropeShots, sizeof ropeShots), mix(&ropeUsed, sizeof ropeUsed), mix(&rope.swung, 1);
    if (grapple.on) mix(&grapple.at, sizeof grapple.at), mix(&grapple.vel, sizeof grapple.vel);
    bool hook = false;
    for (const Object &o : objects) hook |= o.hooked;
    if (roped || hook) mix(&roped, 1), mix(rope.pt, sizeof(Vector3) * rope.n), mix(rope.side, sizeof(Vector3) * rope.n), mix(rope.len, sizeof(float) * rope.n),
                      mix(&rope.angle, sizeof rope.angle), mix(&rope.spin, sizeof rope.spin), mix(&rope.yaw, sizeof rope.yaw);
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
    mix(countGroup.data(), countGroup.size() * sizeof(int)), mix(&countT, sizeof countT), mix(deathQueue.data(), deathQueue.size() * sizeof(int));
    mix(&dyingWorm, sizeof dyingWorm), mix(&throes, sizeof throes);
    for (const Gas &c : gas) mix(&c, sizeof c);
    if (crated) mix(&crated, 1);
    if (camHold) mix(&camHold, sizeof camHold);
    for (const Abductee &a : abductees) mix(&a.worm, sizeof a.worm), mix(&a.st, 1), mix(&a.from, sizeof a.from);
    mix(&girderOn, 1), mix(&girders, sizeof girders), mix(&girderWait, sizeof girderWait);
    if (girderOn) mix(&girder, sizeof girder), mix(&girderFrom, sizeof girderFrom), mix(&cursorYaw, sizeof cursorYaw);
    if (secondary >= 0) mix(&secondary, sizeof secondary);
    if (launched >= 0) mix(&launched, sizeof launched);
    if (jetting || jetUsed) mix(&jetting, 1), mix(&jetUsed, 1), mix(&fuel, sizeof fuel), mix(&boost, sizeof boost);  // no jetpack: old replays' sums hold
    for (const Bubble &b : bubbles) mix(&b, sizeof b);
    mix(&bubbleAt, sizeof bubbleAt);
    mix(&icarus, sizeof icarus), mix(&flapAt, sizeof flapAt), mix(&drift, sizeof drift), mix(&doubleDamage, 1), mix(&changing, 1), mix(spy.data(), spy.size());
    mix(&scout.t, sizeof scout.t), mix(&scout.power, sizeof scout.power), mix(&scout.pitch, sizeof scout.pitch);
    for (const Object &o : objects) { mix(&o.type, 1); mix(&o.pos, sizeof o.pos); mix(&o.vel, sizeof o.vel); mix(&o.weapon, sizeof o.weapon); mix(&o.fuse, sizeof o.fuse); mix(&o.falling, 1); mix(&o.dead, 1); mix(&o.team, sizeof o.team); mix(&o.tag, sizeof o.tag); mix(&o.dud, 1); mix(&o.courtesy, sizeof o.courtesy); mix(&o.hooked, 1); mix(&o.spawning, 1); mix(&o.delay, sizeof o.delay); mix(&o.fizzle, 1); if (o.mystery >= 0) mix(&o.mystery, sizeof o.mystery); if (o.id >= 0) mix(&o.id, sizeof o.id); if (crateLike(o)) mix(&o.hp, sizeof o.hp), mix(&o.count, sizeof o.count), mix(&o.teamCollect, 2), mix(&o.pinned, 1), mix(&o.pushable, 1), mix(&o.scale, sizeof o.scale); }
    if (factory.on) {
        const Factory &f = factory;
        mix(&f.pos, sizeof f.pos), mix(&f.damaged, 1), mix(&f.state, sizeof f.state), mix(&f.wait, sizeof f.wait), mix(&f.toSpawn, sizeof f.toSpawn), mix(&f.until, sizeof f.until), mix(&f.die, sizeof f.die);
    }
    mix(&noTurn, 1);
    for (const Trigger &t : triggers) mix(&t.pos, sizeof t.pos), mix(&t.radius, sizeof t.radius), mix(&t.index, 7 * sizeof(int)), mix(&t.mask, sizeof t.mask), mix(&t.gone, 1);
    if (mysteryWalk || mysteryLow) mix(&mysteryWalk, 1), mix(&mysteryLow, 1);
    for (const auto &r : respawns) mix(&r.first, sizeof r.first), mix(&r.second, sizeof r.second);
    if (wobble.weapon >= 0) mix(&wobble.at, sizeof wobble.at);
    if (mineDet) mix(&mineDet, sizeof mineDet);
    if (cfg.mission) {
        mix(&run.result, 2 * sizeof(int));
        if (script) { uint32_t s = scriptChecksum(*this); mix(&s, sizeof s), mix(&indestructible, 1); }
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
        mix(&wd.poison, sizeof wd.poison), mix(wd.blast, sizeof wd.blast), mix(wd.cblast, sizeof wd.cblast), mix(&wd.lift, sizeof wd.lift), mix(&wd.stick, sizeof wd.stick), mix(&wd.grav, sizeof wd.grav), mix(&wd.base, sizeof wd.base);
        if (wd.avoid) mix(&wd.avoid, 1);
        mix(&wd.size, sizeof wd.size), mix(&wd.sink, sizeof wd.sink), mix(&wd.csize, sizeof wd.csize), mix(&wd.csink, sizeof wd.csink), mix(wd.skim, sizeof wd.skim), mix(&wd.ccone, sizeof wd.ccone), mix(wd.cspeed, sizeof wd.cspeed);
        mix(&wd.userFuse, sizeof wd.userFuse), mix(&wd.restFuse, sizeof wd.restFuse), mix(&wd.walks, 1), mix(&wd.pins, 1);
    }
    return h;
}

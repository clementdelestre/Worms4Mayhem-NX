#include "script.h"
#include "json.h"
#include "mission.h"
#include "raymath.h"
#include "sim.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <strings.h>
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

// t: 'i' int, 'f' float, 'b' bool, 's' string, 'x' not given to Lua (vectors, colours, arrays)
struct Val { char t = 'x'; double n = 0; std::string s; };
struct Ctn { std::string type; std::vector<std::pair<std::string, Val>> f; };
struct Timer { std::string fn; int64_t due = 0; bool live = false; };
constexpr int SLOTS = 16, TIMERS = 10;  // W4M kMaxWorms (Rm.cpp 0x50c245); GameLogic.Timer0..9 (table 0x9213fc)

struct ScriptState {
    lua_State *L = nullptr;
    std::map<std::string, Val> keys, resets;  // resets: Crate.* / Trigger.* as loaded (GameLogic.Reset*)
    std::map<std::string, Ctn> ctns;
    std::map<std::string, Json> movies;
    std::vector<Timer> timers = std::vector<Timer>(TIMERS);
    std::vector<std::string> queue;  // callbacks run once the current one returns (EFMV_Terminated)
    std::vector<int> locks;          // EditContainer: registry ref of the table, -1 closed
    std::vector<std::string> lockName;
    int slotWorm[SLOTS], ai[SLOTS];  // W4M worm slot -> our worm (-1), its AIParams.CPUn level (0 none)
    bool touched[SLOTS] = {};        // Worm.DataNN set up by the script
    std::vector<int> wormSlot;
    bool built = false, started = false, hurtSent = false, endless = false, roundOn = false, timedOut = false;
    int64_t roundStart = 0, idleDue = -1;  // idleDue: a turn with no worm times out then (game ms)
    int64_t roundHeld = 0, holdFrom = -1;  // GameLogic.RoundTime.Pause: ms the round clock stood still, since when it stands
    int nextId = 0;  // Mine.Id of a placed mine, Payload.Deleted.Id of the other payloads: only ever compared with each other
    std::vector<int> poison;  // Worm.ApplyPoison's damage, for the next GameLogic.ApplyDamage
    std::vector<Object> tagged;  // last tick's crates with an Index
    std::map<int, std::string> graphic;  // custom crate Index -> CustomGraphic, render only
    std::vector<bool> died;
    std::vector<std::vector<int>> shown;  // per team: the ammo last given to the sim, from the inventories of
    std::vector<int> ammoSlot;            // this worm slot (the team's active worm)
    std::set<int> toWorm;                 // Crate.Index of the crates made with Crate.AddToWormInventory
    std::vector<GameEvent> ui;  // UI-only events (commentary, emitters, shake), into g.events at the end of the tick
    int emitters = 0;           // Particle.Handle of the last script emitter
    bool noDefault = false;     // Commentary.NoDefault: CommentService +0x138
    std::map<std::string, int> ignored, missing;
    int errors = 0;
    std::string lastError;
    std::set<std::string> wroteKeys, wroteCtns;  // the state beyond the loaded data, for the checksum
    bool dirty = true;
    uint32_t sum = 0;
    static constexpr int DEPTH = 4;
    std::string buf, tmp[DEPTH];  // checksum serialisation, owned here: no destructor runs inside the protected call
    std::vector<std::pair<size_t, size_t>> ent[DEPTH];
    ScriptState() { std::fill(slotWorm, slotWorm + SLOTS, -1), std::fill(ai, ai + SLOTS, 0); }
    ~ScriptState() { if (L) lua_close(L); }
};

static ScriptState *S;  // the running script: one Lua call chain at a time
static Game *G;
struct Using {
    ScriptState *s = S;
    Game *g = G;
    explicit Using(const Game &x) { S = x.script.get(), G = const_cast<Game *>(&x); }
    ~Using() { S = s, G = g; }
};

static int64_t roundMs();
static int64_t nowMs() { return (int64_t)G->clock * 1000 / 60; }  // W4M game time [0x96d030]+0x38 (docs/w4m/missions.md §23.1)
// ElapsedRoundTime: the 10 ms counts of TimerLogicEntity 0x50f100, skipped while GameLogic.RoundTime.Pause holds (+0x6f) or the
// logical camera is a Path / TimedPath one (type 14, 0x50f15f: movie cameras, which skipped movies never start)
static int64_t roundMs() { return nowMs() - S->roundStart - S->roundHeld - (S->holdFrom >= 0 ? nowMs() - std::max(S->holdFrom, S->roundStart) : 0); }

static void count(std::map<std::string, int> &m, const std::string &k) { m[k]++; }

// Engine -> Lua: the global function, if the scripts define it
static bool call(const char *fn) {
    lua_State *L = S->L;
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) return lua_pop(L, 1), false;
    S->dirty = true;
    if (lua_pcall(L, 0, 0, 0)) {
        S->errors++, S->lastError = std::string(fn) + ": " + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        TraceLog(LOG_WARNING, "script %s", S->lastError.c_str());
        lua_pop(L, 1);
    }
    return true;
}

static Val *key(const std::string &k) { auto it = S->keys.find(k); return it == S->keys.end() ? nullptr : &it->second; }
static double num(const char *k, double def = 0) { const Val *v = key(k); return v && v->t != 's' ? v->n : def; }
static void put(const char *k, double n) { if (Val *v = key(k)) v->n = v->t == 'f' ? (float)n : (double)(int64_t)n, S->wroteKeys.insert(k), S->dirty = true; }
static void putStr(const char *k, const char *s) { if (Val *v = key(k)) v->s = s, S->wroteKeys.insert(k), S->dirty = true; }
static Ctn *ctn(const std::string &n) { auto it = S->ctns.find(n); return it == S->ctns.end() ? nullptr : &it->second; }
static Val *field(Ctn *c, const char *f) {
    if (c) for (auto &kv : c->f) if (kv.first == f) return &kv.second;
    return nullptr;
}
static double fnum(Ctn *c, const char *f, double def = 0) { const Val *v = field(c, f); return v ? v->n : def; }

// kWeapon* / kUtility* crate contents and WeaponInventory / WeaponDelays field names -> our weapon
static int weaponOf(const char *w4m) {
    static const char *ALIAS[][2] = {{"Fatkins", "Fatkins Strike"}, {"Scouser", "Inflatable Scouser"}, {"NoMoreNails", "Tail Nail"}, {"Redbull", "Icarus Potion"}};
    const char *n = !strncmp(w4m, "kWeapon", 7) ? w4m + 7 : !strncmp(w4m, "kUtility", 8) ? w4m + 8 : w4m;
    for (auto &a : ALIAS) if (!strcmp(n, a[0])) n = a[1];
    for (size_t i = 0; i < WEAPONS.size(); i++) {
        const char *a = WEAPONS[i].name.c_str(), *b = n;
        for (; *a || *b; a++, b++) {
            while (*a == ' ') a++;
            while (*b == ' ') b++;
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) break;
        }
        if (!*a && !*b) return (int)i;
    }
    return -1;
}

static bool marker(const Game &g, const std::string &name, Vector3 &out) {
    for (const Terrain::Marker &k : g.terrain.markers) if (!strcasecmp(k.name.c_str(), name.c_str())) return out = k.pos, true;
    return false;
}

static float waterY(double units) { return G->terrain.origin.y + (float)units * G->terrain.scale / 20; }  // Water.Level, map units

static bool crate(const Object &o) { return o.type == Object::Crate || o.type == Object::Target; }  // a W4M CrateLogicEntity
static int slotOf(int worm) { return worm >= 0 && worm < (int)S->wormSlot.size() ? S->wormSlot[worm] : -1; }
static std::string slotName(const char *fmt, int i) { char b[40]; snprintf(b, sizeof b, fmt, i); return b; }

// Live game state into the store / containers before a script reads them, and back after it writes
struct ScriptHost {
    static int alliance(int team) { return (int)fnum(ctn(slotName("Team.Data%02d", team)), "AlliedGroup", team); }

    static void pullKey(const char *k) {
        Game &g = *G;
        if (!strcmp(k, "ObjectCount.Active")) put(k, !g.shots.empty() || g.active() || !g.countGroup.empty() || !g.deathQueue.empty() || g.dyingWorm >= 0);
        else if (!strcmp(k, "RoundTimeRemaining") && S->started) put(k, std::max<int64_t>(0, (int64_t)num("RoundTime") - (S->roundOn ? roundMs() : 0)));
        else if (!strcmp(k, "CurrentTeamIndex") && S->started) put(k, g.worms[g.current].team);
        else if (!strcmp(k, "ActiveWormIndex") && S->started) put(k, slotOf(g.current));
        else if (!strcmp(k, "FCS.GameOver")) put(k, g.phase == Phase::GameOver);
        else if (!strcmp(k, "DoubleDamage")) put(k, g.doubleDamage);
        else if (!strcmp(k, "Water.Level") && fabsf(waterY(num(k)) - g.water) > 1e-4f) put(k, (g.water - g.terrain.origin.y) * 20 / g.terrain.scale);
    }

    static void pushKey(const char *k) {
        Game &g = *G;
        if (!strcmp(k, "Wind.Speed") || !strcmp(k, "Wind.Direction")) {  // xz vector (0x57eb25), our wind is Speed / MaxSpeed
            float s = (float)(num("Wind.Speed") / std::max(1e-9, num("Wind.MaxSpeed", 1))), d = (float)num("Wind.Direction");
            g.wind = s * cosf(d), g.windZ = s * sinf(d);
        } else if (!strcmp(k, "DoubleDamage")) g.doubleDamage = num(k) != 0;
        else if (!strcmp(k, "Land.Indestructable")) g.indestructible = num(k) != 0;
        else if (!strcmp(k, "DefaultRetreatTime")) g.cfg.scheme.retreatTime = (uint8_t)Clamp(num(k) / 1000, 0, 255);
        else if (!strcmp(k, "Crate.HealthInCrates")) g.cfg.scheme.crateHealth = (uint8_t)Clamp(num(k), 0, 255);
        else if (!strcmp(k, "Jetpack.Fuel")) g.fuel = (float)(num(k) / 1000);
        else if (!strcmp(k, "Water.Level")) g.water = waterY(num(k));  // the drown and payload tests read it at once
        else if (!strcmp(k, "Mine.MinFuse")) g.mineMin = (int)num(k);
        else if (!strcmp(k, "Mine.MaxFuse")) g.mineMax = (int)num(k);
        else if (!strcmp(k, "Mine.DudProbability")) g.mineDud = (float)num(k);
        else if (!strcmp(k, "Mine.DetonationType")) g.mineDet = (int)num(k);
        else if (!strcmp(k, "Jetpack.InitFuel")) g.jetInit = (float)(num(k) / 1000);
        else if (!strcmp(k, "Challenge.EndlessGun")) g.endlessGun = num(k) != 0;
    }

    // A worm's ammo = Inventory.WormNN + TeamNN + AllianceNN, -1 if any is -1 (0x50d900). Ours is per team: the team's ammo worm's
    // (ammoSlot). Each tick the sim's changes are booked back as W4M makes them: a use takes from the alliance, then the team, then the
    // worm, none when one is -1 (0x4f4fd0); a crate adds to the alliance (0x5c8820)
    static const std::vector<std::pair<std::string, int>> &weaponFields() {  // WeaponInventory field -> our weapon
        static std::vector<std::pair<std::string, int>> f;
        if (f.empty()) for (auto &kv : ctn("Inventory.Worm.Default")->f) if (int k = weaponOf(kv.first.c_str()); k >= 0) f.push_back({kv.first, k});
        return f;
    }
    static std::string invName(int t, int part) {  // 0 alliance, 1 team, 2 the ammo worm's ("" without a worm)
        return part == 0 ? slotName("Inventory.Alliance%02d", alliance(t)) : part == 1 ? slotName("Inventory.Team%02d", t) : S->ammoSlot[t] < 0 ? "" : slotName("Inventory.Worm%02d", S->ammoSlot[t]);
    }
    static Ctn *teamInv(int t, int part) { return ctn(invName(t, part)); }
    static void pushInventory(int team) {
        Game &g = *G;
        if (team >= g.teams || team >= (int)S->shown.size()) return;
        for (auto &f : weaponFields()) {
            int x = (int)fnum(teamInv(team, 0), f.first.c_str()), y = (int)fnum(teamInv(team, 1), f.first.c_str()), z = (int)fnum(teamInv(team, 2), f.first.c_str());
            g.ammo[team][f.second] = S->shown[team][f.second] = x < 0 || y < 0 || z < 0 ? -1 : x + y + z;
        }
    }
    static void bookAmmo() {
        Game &g = *G;
        bool any = false;
        for (const GameEvent &e : g.events)  // an AddToWormInventory crate: its contents to the collector's Inventory.WormNN (0x5c8820)
            for (const Object &o : S->tagged)
                if (e.kind == GameEvent::Collect && e.worm >= 0 && e.weapon >= 0 && S->toWorm.count(o.tag) && Vector3Distance(e.pos, o.pos) < 0.01f) {
                    int t = g.worms[e.worm].team, &was = S->shown[t][e.weapon];
                    for (auto &f : weaponFields())
                        if (Val *v = f.second == e.weapon ? field(ctn(slotName("Inventory.Worm%02d", slotOf(e.worm))), f.first.c_str()) : nullptr; v && v->n >= 0)
                            v->n += o.count, S->wroteCtns.insert(slotName("Inventory.Worm%02d", slotOf(e.worm))), any = true;
                    if (was >= 0 && g.ammo[t][e.weapon] >= 0) was += o.count;  // not the alliance's
                }
        for (int t = 0; t < (int)S->shown.size() && t < g.teams; t++)
            for (auto &f : weaponFields()) {
                int n = g.ammo[t][f.second], &was = S->shown[t][f.second];
                if (n == was) continue;
                Val *v[3] = {field(teamInv(t, 0), f.first.c_str()), field(teamInv(t, 1), f.first.c_str()), field(teamInv(t, 2), f.first.c_str())};
                bool inf = false;
                for (Val *x : v) inf |= x && x->n < 0;
                for (int d = was - n; !inf && d > 0; d--)
                    for (Val *x : v) if (x && x->n > 0) { x->n--; break; }
                if (!inf && n > was && v[0]) v[0]->n += n - was;
                was = n, any = true;
                for (int part = 0; part < 3; part++) if (v[part]) S->wroteCtns.insert(invName(t, part));
            }
        if (!any) return;
        S->dirty = true;
        for (int t = 0; t < g.teams; t++) pushInventory(t);  // a shared alliance reaches its other teams
    }

    static void pull(const std::string &n) {
        Game &g = *G;
        int i = -1;
        if (!n.compare(0, 10, "Inventory.") && S->built) bookAmmo();
        else if (sscanf(n.c_str(), "Inventory%d.WeaponDelays", &i) == 1 && i < g.teams) {
            for (auto &kv : ctn(n)->f) if (int k = weaponOf(kv.first.c_str()); k >= 0) kv.second.n = g.delays[i][k];
        } else if (sscanf(n.c_str(), "Worm.Data%d", &i) == 1 && i < SLOTS && S->slotWorm[i] >= 0) {
            const Worm &w = g.worms[S->slotWorm[i]];
            if (Val *e = field(ctn(n), "Energy")) e->n = std::max(0, w.hp);
            if (Val *a = field(ctn(n), "Active")) a->n = w.alive;
        }
    }

    static void push(const std::string &n, const std::string *from) {
        Game &g = *G;
        S->wroteCtns.insert(n), S->dirty = true;
        int i = -1;
        if (sscanf(n.c_str(), "Worm.Data%d", &i) == 1 && i >= 0 && i < SLOTS) {
            S->touched[i] = true;
            if (S->slotWorm[i] >= 0) {
                Worm &w = g.worms[S->slotWorm[i]];
                if (w.alive) w.hp = w.counted = std::max(1, (int)fnum(ctn(n), "Energy"));
            }
        } else if (sscanf(n.c_str(), "AIParams.Worm%d", &i) == 1 && i >= 0 && i < SLOTS && from) {
            int lv = 0;
            if (sscanf(from->c_str(), "AIParams.CPU%d", &lv) == 1) S->ai[i] = lv;
            else if (sscanf(from->c_str(), "AIParams.Worm%d", &lv) == 1 && lv >= 0 && lv < SLOTS) S->ai[i] = S->ai[lv];
        } else if (!n.compare(0, 10, "Inventory.")) {
            for (int t = 0; t < g.teams && S->built; t++) pushInventory(t);
        } else if (sscanf(n.c_str(), "Inventory%d.WeaponDelays", &i) == 1) {
            if (S->built && i < g.teams) for (auto &kv : ctn(n)->f) if (int k = weaponOf(kv.first.c_str()); k >= 0) g.delays[i][k] = (int)kv.second.n;
        } else if (n == "kMineFactoryData") {
            Ctn *c = ctn(n);
            Game::FactoryData &f = g.factoryData;
            f.activation = (uint8_t)fnum(c, "NumMineActivation"), f.inactive = (uint8_t)fnum(c, "NumTurnsInactive"), f.padding = (float)fnum(c, "SafeRadiusPadding");
            f.damage = (float)fnum(c, "DamageMagnitude"), f.push = (float)fnum(c, "ImpulseMagnitude"), f.reach = (float)fnum(c, "WormDamageRadius");
            f.crater = (float)fnum(c, "LandDamageRadius"), f.pushReach = (float)fnum(c, "ImpulseRadius");
        } else if (n == "GM.SchemeData") {  // DropRandomCrate 0x4fab20 reads the live scheme
            Ctn *c = ctn(n);
            Scheme &s = g.cfg.scheme;
            s.crateChance = (uint8_t)fnum(c, "RandomCrateChancePerTurn"), s.weaponShare = (uint8_t)fnum(c, "WeaponChance");
            s.healthShare = (uint8_t)fnum(c, "HealthChance"), s.utilityShare = (uint8_t)fnum(c, "UtilityChance"), s.mysteryShare = (uint8_t)fnum(c, "MysteryChance");
        }
    }

    static Worm blank() { return Worm{{Terrain::NX * Terrain::VOX / 2, -50, Terrain::NZ * Terrain::VOX / 2}, {0, 0, 0}, 0, 0.3f, 0, 0, false, false}; }
    // A slot's worm from its Worm.DataNN: at its Spawn marker if Active (PlaceWormAtSpawnPoint 0x5b4180), dropped onto the ground
    // below, facing the map centre [ours, as the JSON path]
    static bool place(Game &g, Worm &w, Ctn *c, int s) {
        const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
        w.team = std::min(3, (int)fnum(c, "TeamIndex")), w.turns = fnum(c, "IsAllowedToTakeTurn", 1) != 0;
        const Val *sp = field(c, "Spawn");
        Vector3 p, hit;
        if (fnum(c, "Active") == 0 || !sp || !marker(g, sp->s, p)) {
            if (fnum(c, "Active") != 0) TraceLog(LOG_WARNING, "script: worm %d has no spawn marker '%s'", s, sp ? sp->s.c_str() : "");
            return false;
        }
        if (g.terrain.raycast({Vector3Add(p, {0, 0.8f, 0}), {0, -1, 0}}, 40, &hit)) p = {hit.x, hit.y + Game::R + 0.3f, hit.z};
        w.pos = p, w.yaw = atan2f(cx - p.x, cz - p.z), w.alive = true, w.hp = w.counted = std::max(1, (int)fnum(c, "Energy"));
        return true;
    }

    // Worm.Respawn 0x5b4f70: a worm that exists stays; else a fresh one at its Spawn (SpawnWorm 0x5b31b0), joining the turns if
    // IsAllowedToTakeTurn. A slot new since Reinitialise takes its team's first free column [ours: the team-major layout]
    static void respawn(int s) {
        Game &g = *G;
        if (s < 0 || s >= SLOTS || !S->built) return;
        int i = S->slotWorm[s], t = std::min(3, (int)fnum(ctn(slotName("Worm.Data%02d", s)), "TeamIndex"));
        if (i >= 0 && g.worms[i].alive) return (void)TraceLog(LOG_WARNING, "script: worm %d respawned while alive", s);
        if (i >= 0 && i / g.perTeam != t) S->wormSlot[i] = -1, i = -1;
        for (int k = 0; k < g.perTeam && i < 0 && t < g.teams; k++) if (S->wormSlot[t * g.perTeam + k] < 0) i = t * g.perTeam + k;
        for (int k = 0; k < g.perTeam && i < 0 && t < g.teams; k++)  // else a gone worm's column: its slot is remapped if it comes back
            if (!g.worms[t * g.perTeam + k].alive && !g.worms[t * g.perTeam + k].drowned) i = t * g.perTeam + k, S->slotWorm[S->wormSlot[i]] = -1;
        if (i < 0) return (void)TraceLog(LOG_WARNING, "script: no room for worm %d in team %d", s, t);
        S->slotWorm[s] = i, S->wormSlot[i] = s, S->touched[s] = true;
        Worm w = blank();
        if (!place(g, w, ctn(slotName("Worm.Data%02d", s)), s)) return;
        g.worms[i] = w, S->died[i] = false, g.lastHitTeam[i] = -1, g.special[i] = 0;
    }

    // Worm.DieQuietly and WXWormManager.UnspawnWorm (both 0x5b4af0): the worm entity goes, WormData.Active = 0; no blast, no grave,
    // no Worm.Died
    static void unspawn(int s) {
        Game &g = *G;
        int i = s >= 0 && s < SLOTS ? S->slotWorm[s] : -1;
        if (i < 0 || !g.worms[i].alive) return (void)TraceLog(LOG_DEBUG, "script: unspawn of worm %d, which does not exist", s);
        if (Val *a = field(ctn(slotName("Worm.Data%02d", s)), "Active")) a->n = 0, S->wroteCtns.insert(slotName("Worm.Data%02d", s));
        Worm w = blank();
        w.team = g.worms[i].team, w.turns = g.worms[i].turns;
        g.worms[i] = w, S->died[i] = true;
        auto drop = [&](std::vector<int> &v) { v.erase(std::remove(v.begin(), v.end(), i), v.end()); };
        drop(g.countGroup), drop(g.deathQueue);
        if (g.dyingWorm == i) g.dyingWorm = -1;
    }

    // WormManager.Reinitialise: the worms of the Worm.DataNN slots the script set up, by TeamIndex then slot (our worms are team-major)
    static void reinitialise() {
        Game &g = *G;
        if (S->built) return count(S->ignored, "WormManager.Reinitialise (again)");
        int teams = 1, per = 1, used = 0, n[4] = {};
        for (int t = 0; t < 4; t++) if (S->wroteCtns.count(slotName("Team.Data%02d", t))) teams = std::max(teams, t + 1);
        for (int s = 0; s < SLOTS; s++)
            if (S->touched[s]) {
                int t = std::min(3, (int)fnum(ctn(slotName("Worm.Data%02d", s)), "TeamIndex"));
                teams = std::max(teams, t + 1), per = std::max(per, ++n[t]), used++;
            }
        per += SLOTS - used;  // ours: columns for the slots a later Worm.Respawn sets up (W4M: 16 slots, any team)
        std::vector<Worm> worms(teams * per, blank());
        S->wormSlot.assign(worms.size(), -1);
        std::fill(n, n + 4, 0);
        for (int s = 0; s < SLOTS; s++) {
            if (!S->touched[s]) continue;
            Ctn *c = ctn(slotName("Worm.Data%02d", s));
            int t = std::min(3, (int)fnum(c, "TeamIndex")), i = t * per + n[t]++;
            S->slotWorm[s] = i, S->wormSlot[i] = s;
            place(g, worms[i], c, s);
        }
        g.worms = worms, g.teams = teams, g.perTeam = per, g.current = 0;
        g.allied.assign(teams, 0);
        for (int t = 0; t < teams; t++) g.allied[t] = (int8_t)alliance(t);
        g.nextWorm.assign(teams, 0), g.lastHitTeam.assign(worms.size(), -1), g.fuses.assign(teams, 3), g.picked.assign(teams, g.weapon);
        g.ammo.assign(teams, std::vector<int>(WEAPONS.size(), 0)), g.delays.assign(teams, std::vector<int>(WEAPONS.size(), 0));
        g.idle.assign(teams, 0), g.surrendered.assign(teams, 0), g.spy.assign(teams, 0), g.special.assign(worms.size(), 0), g.superWeapon.assign(teams, 0);
        S->died.assign(worms.size(), false);
        S->shown.assign(teams, std::vector<int>(WEAPONS.size(), 0)), S->ammoSlot.assign(teams, -1);
        S->built = true;
        g.cfg.teams = teams, g.cfg.wormsPerTeam = per;
        g.cfg.teamSetup.resize(teams);
        for (int t = 0; t < teams; t++) {
            Ctn *c = ctn(slotName("Team.Data%02d", t));
            GameConfig::Team &ts = g.cfg.teamSetup[t];
            if (const Val *nm = field(c, "Name")) ts.name = nm->s;
            int lv = 0;
            for (int k = 0; k < per && !lv; k++) if (S->wormSlot[t * per + k] >= 0) lv = S->ai[S->wormSlot[t * per + k]];
            ts.cpu = fnum(c, "IsAIControlled") != 0 ? (uint8_t)Clamp(lv ? lv : 2, 1, 5) : 0;  // none copied: AIService init's CPU2 (0x4b3390)
            S->ammoSlot[t] = -1;
            for (int k = per - 1; k >= 0; k--) if (S->wormSlot[t * per + k] >= 0) S->ammoSlot[t] = S->wormSlot[t * per + k];
            pushInventory(t);
            push(slotName("Inventory%d.WeaponDelays", t), nullptr);
        }
    }

    // GameLogic.ActivateNextWorm 0x5b59b0: the team that played spends a delay turn (0x5b5a5f), then the next worm; SameTeamNextTurn
    // (reset to 0 at once, 0x5b60e3) skips the delays and gives the turn to that team's next worm (0x5b5630); stdlib's clocks
    static void activateNext() {
        Game &g = *G;
        if (!S->built || g.phase == Phase::GameOver) return;
        bookAmmo();
        bool same = num("SameTeamNextTurn") != 0;
        if (same) put("SameTeamNextTurn", 0);
        if (S->started) {
            const Worm &w = g.worms[g.current];
            if (!same) for (int &d : g.delays[w.team]) d = std::max(0, d - 1);
            g.picked[w.team] = g.weapon, g.beginTurn(same ? (w.team + g.teams - 1) % g.teams : w.team);
        } else g.beginTurn(g.teams - 1);
        if (g.phase != Phase::GameOver && slotOf(g.current) >= 0) {
            int t = g.worms[g.current].team;
            S->ammoSlot[t] = slotOf(g.current), pushInventory(t);
        }
        S->started = true, S->hurtSent = false, g.noTurn = false;
        int turn = (int)num("TurnTime");
        S->endless = turn <= 0;  // Timer.StartTurn: no turn clock (0x50f980)
        g.timer = turn > 0 ? std::max(1, msTicks(turn)) : 99 * 60;
        g.hotSeat = msTicks((int)num("HotSeatTime"));
        g.cfg.scheme.turnTime = (uint8_t)Clamp(turn / 1000, 1, 255);
        // no worm to activate (0x5b5630: ActiveWormIndex -1): W4M's turn clocks still run [ours: the hot seat runs out, no input]
        S->idleDue = g.phase == Phase::Aim || turn <= 0 ? -1 : nowMs() + std::max(0, (int)num("HotSeatTime")) + turn;
    }

    static void endTurn() {  // GameLogic.EndTurn: the worm loses control; stdlib then waits for no activity
        Game &g = *G;
        if (g.phase == Phase::GameOver) return;
        g.phase = Phase::Settle, g.timer = 1, g.jumpDelay = 0, g.roped = g.jetting = g.chute = false;
    }

    static void explode(const std::string &at, float dmg, float imp, float reach, float crater, float impReach, float offset, const std::string &fx = "") {
        Game &g = *G;
        Vector3 p;
        if (!marker(g, at, p)) return (void)TraceLog(LOG_WARNING, "script: no explosion marker '%s'", at.c_str());
        // 0x4f9970: impulse centre ImpulseOffset units above the blast; ParticleEffect, empty: WXP_ExplosionX_Med (0x4f9a8c)
        g.explode(p, Blast{crater / 20, reach / 20, dmg, imp * 50, impReach / 20, -offset / 20}, 0, 0, -1, intern(fx.empty() ? "WXP_ExplosionX_Med" : fx));  // units, units/ms -> m, m/s
    }

    // UI-only events: queued here, never read back by the sim or the checksum
    static const char *intern(const std::string &s) { static std::set<std::string> pool; return pool.insert(s).first->c_str(); }
    static void ui(GameEvent::Kind k, Vector3 p, int b, const std::string &s = "") { S->ui.push_back({k, p, -1, b, s.empty() ? nullptr : intern(s)}); }
    // CommentaryPanel.TimedText (Delay ms, GetData default 1000) / ScriptText (0): CommentService 0x5e4ca0; pos.x 1: ScriptText
    static void comment(const std::string &id, int ms, bool script) { ui(GameEvent::Comment, {(float)script, 0, 0}, ms, id); }
    // Particle.NewEmitter (0x5c1530): the effect at the named detail, its handle into Particle.Handle; EFMV user ids are handles < 0
    static void emitter(const std::string &fx, const std::string &at, int handle) {
        Vector3 p;
        if (!marker(*G, at, p)) return (void)TraceLog(LOG_WARNING, "script: no emitter detail '%s'", at.c_str());
        if (handle > 0) put("Particle.Handle", handle);
        ui(GameEvent::Emitter, p, handle, fx);
    }

    static void movie() {
        const Val *nm = key("EFMV.MovieName");
        auto it = nm ? S->movies.find(nm->s) : S->movies.end();
        if (it == S->movies.end()) TraceLog(LOG_WARNING, "script: no movie '%s'", nm ? nm->s.c_str() : "");
        else
            for (const Json &e : it->second.arr) {  // the skip: Critical events only (acting.md §19)
                std::string t = e[0].s();
                if (t == "CreateExplosion") explode(e[1].s(), e[2].f(), e[3].f(), e[4].f(), e[5].f(), e[6].f(), e[8].f(), e[7].s());
                else if (t == "Comment") comment(e[1].s(), (int)e[2].f(), false);  // 0x525dc0: Comment + Delay = Duration, TimedText
                else if (t == "CreateEmitter") emitter(e[1].s(), e[2].s(), -1 - (int)e[4].f());  // NewUserIdEmitter
                else if (t == "DeleteEmitter") ui(GameEvent::EmitterOff, {}, -1 - (int)e[1].f());
                else if (t == "RaiseWater") put("Water.Level", num("Water.Level") + e[1].f()), pushKey("Water.Level");
                else if (t == "SpawnWorm") {  // 0x525e7b: CopyContainer(DataId, Worm.Data<WormId>), then Worm.Respawn
                    std::string to = slotName("Worm.Data%02d", (int)e[1].f()), from = e[2].s();
                    Ctn *a = ctn(from), *b = ctn(to);
                    if (a && b) *b = *a, push(to, &from), respawn((int)e[1].f());
                } else if (t == "UnspawnWorm") unspawn((int)e[1].f());  // 0x525fa0
                else if (t != "DeleteBorders") count(S->ignored, "EFMV " + t);  // borders: render only
            }
        S->queue.push_back("EFMV_Terminated");
    }

    static void createCrate() {
        Game &g = *G;
        const Val *ty = key("Crate.Type"), *ct = key("Crate.Contents"), *sp = key("Crate.Spawn");
        Vector3 p;
        if (!sp || !marker(g, sp->s, p)) return (void)TraceLog(LOG_WARNING, "script: no crate marker '%s'", sp ? sp->s.c_str() : "");
        auto type = [&](const char *t) { return ty && !strcasecmp(ty->s.c_str(), t); };
        int w = type("target") || type("health") ? -1 : type("custom") ? -2 : ct ? weaponOf(ct->s.c_str()) : -1;  // custom: no contents
        if (w == -1 && !type("target") && !type("health")) TraceLog(LOG_WARNING, "script: crate contents '%s'", ct ? ct->s.c_str() : "");
        // at the marker itself (0x5c8ed0); LifetimeSec / LifetimeTurns -1, UXB, DelayMillisec, RandomSpawnPos 0 in every W4M crate
        Object o = {type("target") ? Object::Target : Object::Crate, p, {0, -(float)num("Crate.FallSpeed") / 20, 0}, w, -1, false, false};
        float k = (float)num("Crate.HitpointsMultiplier");
        o.tag = (int)num("Crate.Index", -1), o.count = (int)num("Crate.NumContents", 1), o.scale = (float)num("Crate.Scale", 1);
        o.hp = k > 0 ? (int)(num("Crate.Hitpoints") * k) : (int)num("Crate.Hitpoints");  // 0x5c7f88
        o.teamCollect = (int8_t)num("Crate.TeamCollectable", -1), o.teamDestroy = (int8_t)num("Crate.TeamDestructible", -1);
        o.pinned = num("Crate.Gravity", 1) != 1, o.pushable = num("Crate.Pushable", 1) == 1, o.track = num("Crate.TrackCam", 1) == 1;
        o.falling = !o.pinned && num("Crate.Parachute") == 1;
        o.spawning = !o.pinned && num("Crate.WaitTillLanded", 1) == 1;  // the "Crate Spawn" active object (0x5c8049, 0x5c96d2)
        Vector3 hit;
        if (num("Crate.GroundSnap") == 1) {  // 0x5c80a0: onto the land below, landed, no chute, no active object; else at the water line
            o.falling = o.spawning = false;
            if (g.terrain.raycast({p, {0, -1, 0}}, 1000, &hit) && hit.y > g.water) o.pos.y = hit.y + 0.45f * o.scale;
            else o.pos.y = g.water;
        }
        g.objects.push_back(o);
        if (num("Crate.AddToWormInventory") != 0) S->toWorm.insert(o.tag), put("Crate.AddToWormInventory", 0);  // reset per crate (0x4f2245)
        const Val *gfx = key("Crate.CustomGraphic");
        if (type("custom") && gfx && !gfx->s.empty()) S->graphic[o.tag] = gfx->s;  // render only (CrateGraphicEntity 0x5c4ea1)
    }

    // GameLogic.CreateTrigger -> TriggerLogicEntity 0x5d5360: the Trigger.* keys at create time, at the marker itself (0x5d4f30)
    static void createTrigger() {
        Game &g = *G;
        const Val *sp = key("Trigger.Spawn");
        Vector3 p;
        if (!sp || !marker(g, sp->s, p)) return (void)TraceLog(LOG_WARNING, "script: no trigger marker '%s'", sp ? sp->s.c_str() : "");
        int tc = (int)num("Trigger.TeamCollect", -1), wc = (int)num("Trigger.WormCollect", -1), sc = (int)num("Trigger.SheepCollect", -1), pc = (int)num("Trigger.PayloadCollect", -1);
        auto in = [](int v, int n) { return v >= -1 && v < n; };  // 0x5d4b50
        uint16_t mask = (in(tc, 4) || in(wc, SLOTS) ? 1 : 0) | (in(sc, 4) ? 0x80 : 0) | (in(pc, 4) ? 8 : 0) | (num("Trigger.GirderCollect") != 0 ? 0x200 : 0);
        float r = (float)num("Trigger.Radius");
        if (!(r > 0)) r = 1;  // 'Error Invalid Trigger Radius'
        int worm = wc < 0 ? wc : wc < SLOTS && S->slotWorm[wc] >= 0 ? S->slotWorm[wc] : -2;  // ours: the slot's worm now
        g.triggers.push_back({p, r / 20, (int)num("Trigger.Index", -1), std::max(1, (int)num("Trigger.HitPoints")), tc, worm,
                              (int)num("Trigger.TeamDestroy", -1), sc, pc, mask, num("Trigger.Visibility") == 1});  // 0x5d567b: shown if 1
    }

    static void placeMine(const char *at) {  // 0x4fdf90: the detail object by name (0x4f9400), CreateMine there, Mine.Id = the mine
        Game &g = *G;
        Vector3 p;
        if (!at || !marker(g, at, p)) return (void)TraceLog(LOG_WARNING, "script: no mine marker '%s'", at ? at : "");
        Object o = g.newMine(p);
        o.id = ++S->nextId;
        g.objects.push_back(o);
        put("Mine.Id", o.id);
    }

    // Weapon.Create 0x565770: the active worm's WeaponIndex kept if usable (0x50d900), else the first usable id from 0 (Skip Go and
    // Surrender skipped), else kWeaponUndefined; written back to Worm.DataNN, then Weapon.PreSelected
    static int weaponById(int id) { for (size_t i = 0; i < WEAPONS.size() && id > 0; i++) if (G->inventoryId((int)i) == id) return (int)i; return -1; }
    static void createWeapon() {
        Game &g = *G;
        std::string n = slotName("Worm.Data%02d", slotOf(g.current));
        Val *wi = field(ctn(n), "WeaponIndex");
        if (!wi) return;
        int team = g.worms[g.current].team;
        auto usable = [&](int id) { int k = weaponById(id); return id != 0x43 && k >= 0 && g.usable(team, k); };
        if (!usable((int)wi->n)) {
            int id = 0;
            for (; id < 0x42 && (id == 0x26 || id == 0x27 || !usable(id)); id++) {}
            wi->n = id < 0x42 ? id : 0x43;
        }
        S->wroteCtns.insert(n), S->dirty = true;
        preSelected();
    }
    // Weapon.PreSelected 0x566c77: WeaponSelected 0x565d30 builds the WeaponIndex item, no ammo test; kWeaponUndefined: ours keeps the hand
    static void preSelected() {
        Game &g = *G;
        int k = weaponById((int)fnum(ctn(slotName("Worm.Data%02d", slotOf(g.current))), "WeaponIndex", 0x43));
        if (k >= 0 && !g.toolOut()) g.weapon = k, g.secondary = -1, g.jetUsed = false, g.fuel = g.jetInit;
    }

    static void message(const char *m, float a, const char *s) {
        Game &g = *G;
        auto is = [&](const char *x) { return !strcmp(m, x); };
        S->dirty = true;
        if (is("GameLogic.ActivateNextWorm")) activateNext();
        else if (is("GameLogic.EndTurn")) endTurn();
        else if (is("Timer.StartGame")) S->roundStart = nowMs(), S->roundOn = num("RoundTime") > 0, S->roundHeld = 0, S->holdFrom = S->holdFrom < 0 ? -1 : nowMs();
        else if (is("GameLogic.RoundTime.Pause")) { if (S->holdFrom < 0) S->holdFrom = nowMs(); }  // TimerLogicEntity +0x6f (0x510018)
        else if (is("GameLogic.RoundTime.Resume")) { if (S->holdFrom >= 0) S->roundHeld += nowMs() - S->holdFrom, S->holdFrom = -1; }
        else if (is("Timer.StartPostActivity")) {
            int t = scriptPostActivity(g);
            if (t > 0) g.timer = -t;
            else g.timer = 0, call("Timer_PostActivityTimedOut");
        } else if (is("Worm.ApplyPoison")) S->poison = g.applyPoison();
        else if (is("GameLogic.ApplyDamage")) {  // 0x5b22a0 ends with Turn.MaxDamage 0, Turn.Boring 1 if it was 0 (acting.cpp reads them first)
            if (num("Turn.MaxDamage") != 0) put("Turn.MaxDamage", 0);
            if (num("Turn.Boring") == 0) put("Turn.Boring", 1);
            std::vector<int> p;
            p.swap(S->poison);
            g.applyDamage(p.empty() ? nullptr : &p);
        } else if (is("RandomNumber.Get")) {  // 0x4ff0aa: Uint = a draw's low 16 bits, then Float = another's / 65536
            put("RandomNumber.Uint", (int)(g.rand01() * 65536)), put("RandomNumber.Float", (int)(g.rand01() * 65536) / 65536.0f);
        } else if (is("WormManager.GetActiveAlliances")) {
            std::set<int> al;
            for (int t = 0; t < g.teams; t++) if (g.standing(t)) al.insert(alliance(t));
            put("AllianceCount", (double)al.size());
        } else if (is("WormManager.GetSurvivingTeam")) {
            int t = 0;
            while (t < g.teams && !g.standing(t)) t++;
            put("SurvivingTeamIndex", t < g.teams ? t : -1);
        } else if (is("GameLogic.Mission.Success") || is("GameLogic.Challenge.Success")) missionEnd(g, true);
        else if (is("GameLogic.Mission.Failure") || is("GameLogic.Challenge.Failure") || is("GameLogic.Draw")) missionEnd(g, false);
        else if (is("GameLogic.Win")) missionEnd(g, (int)a == 0);
        else if (is("WormManager.Reinitialise")) reinitialise();
        else if (is("GameLogic.PlaceObjects")) placeMapObjects(g);
        else if (is("GameLogic.ResetCrateParameters") || is("GameLogic.ResetTriggerParams")) {
            const char *pre = is("GameLogic.ResetCrateParameters") ? "Crate." : "Trigger.";
            for (auto &kv : S->resets) if (!kv.first.compare(0, strlen(pre), pre)) S->keys[kv.first] = kv.second;
        } else if (is("GameLogic.CreateCrate")) createCrate();
        else if (is("GameLogic.PlaceMine")) placeMine(s);
        else if (is("GameLogic.StartMineFactory")) g.factoryStart();
        else if (is("Crate.Delete")) {  // 0x5c5cf0: the crates of that Index go; Crate.Destroyed reaches the script at once (0x6910e4)
            int n = 0;
            for (size_t i = 0; i < g.objects.size();)
                if (crate(g.objects[i]) && g.objects[i].tag == (int)a) n++, g.objects.erase(g.objects.begin() + i);
                else i++;
            S->tagged.erase(std::remove_if(S->tagged.begin(), S->tagged.end(), [&](const Object &o) { return o.tag == (int)a; }), S->tagged.end());
            for (int k = 0; k < n; k++) put("Crate.Index", (int)a), call("Crate_Destroyed");
        } else if (is("Crate.RadarHide") || is("Crate.RadarDisplay")) {  // 0x5cb4af / 0x5cb453: +0x86, the radar's
            for (Object &o : g.objects) if (crate(o) && o.tag == (int)a) o.radar = is("Crate.RadarDisplay");
        } else if (is("GameLogic.CreateTrigger")) createTrigger();
        else if (is("GameLogic.DestroyTrigger"))  // 0x5d5747: every trigger of that Index goes, no callback
            g.triggers.erase(std::remove_if(g.triggers.begin(), g.triggers.end(), [&](const Game::Trigger &t) { return t.index == (int)a; }), g.triggers.end());
        else if (is("Worm.Respawn")) respawn((int)a);
        else if (is("Worm.DieQuietly") || is("WXWormManager.UnspawnWorm")) unspawn((int)a);
        else if (is("GameLogic.DropRandomCrate")) g.dropCrates(1, true);
        else if (is("EFMV.Play")) movie();
        else if (is("Explosion.Construct")) {
            explode(key("Explosion.DetailObject") ? key("Explosion.DetailObject")->s : "", (float)num("Explosion.WormDamageMagnitude"), (float)num("Explosion.ImpulseMagnitude"),
                    (float)num("Explosion.WormDamageRadius"), (float)num("Explosion.LandDamageRadius"), (float)num("Explosion.ImpulseRadius"),
                    (float)num("Explosion.ImpulseOffset"), key("Explosion.ParticleEffect") ? key("Explosion.ParticleEffect")->s : "");
            triggers();  // the triggers it destroys post Trigger.Destroyed at once (0x5d48ca -> 0x6910e4)
        }
        else if (is("Weapon.Create")) createWeapon();
        else if (is("Weapon.PreSelected")) preSelected();
        else if (is("Jetpack.UpdateFuel")) put("Jetpack.Fuel", g.fuel * 1000);
        else if (is("CommentaryPanel.TimedText") || is("CommentaryPanel.ScriptText")) {
            const Val *c = key("CommentaryPanel.Comment");
            comment(c ? c->s : "", is("CommentaryPanel.ScriptText") ? 0 : (int)num("CommentaryPanel.Delay", 1000), is("CommentaryPanel.ScriptText"));
        } else if (is("Commentary.Clear")) ui(GameEvent::CommentClear, {}, 0);
        else if (is("Commentary.NoDefault") || is("Commentary.EnableDefault")) S->noDefault = is("Commentary.NoDefault");
        else if (is("Particle.NewEmitter")) {
            const Val *at = key("Particle.DetailObject"), *fx = key("Particle.Name");
            emitter(fx ? fx->s : "", at ? at->s : "", ++S->emitters);
        } else if (is("Particle.DelGraphicalEmitter") || is("Particle.DelGraphicalEmitterImm")) ui(GameEvent::EmitterOff, {(float)is("Particle.DelGraphicalEmitterImm"), 0, 0}, (int)a);
        else if (is("Camera.ShakeStart")) ui(GameEvent::Shake, {(float)num("Camera.Shake.Magnitude"), 0, 0}, (int)num("Camera.Shake.Length"));  // 0x522c29
        else if (is("Timer.StartHotSeatTimer") || is("Timer.StartTurn") || is("Timer.EndTurn") || is("Timer.EndRetreatTimer") || is("GameLogic.Turn.Started") ||
                 is("GameLogic.Turn.Ended") || is("GameLogic.AboutToApplyDamage") || is("AI.PerformDefaultAITurn") || is("AI.ExecuteActions") ||
                 is("Weapon.Delete") || is("Utility.Delete") || is("Weapon.DisableWeaponChange") || is("Net.DisableAllInput") || is("EFMV.End") ||
                 is("Weapon.Wield")) {
            // covered by the sim's own turn: ActivateNextWorm sets the clocks, EndTurn the tools, our AI polls; EFMV.End: borders;
            // Weapon.Wield: the holstered weapon's draw pose and EquipSfx only (WeaponAccessoryEntity 0x597739)
        } else {
            count(S->ignored, m);
            TraceLog(LOG_DEBUG, "script: message %s not modelled", m);
        }
        (void)s;
    }

    static void events() {
        Game &g = *G;
        std::vector<GameEvent> ev = g.events;
        bool current = false;
        for (const GameEvent &e : ev) {  // 0x5ab7e0, each hit: Worm.Damaged.Current (the active worm, any phase, not types 5 / 6), then Worm.Damaged
            if (e.kind != GameEvent::Hurt || e.worm < 0) continue;
            if (e.worm == g.current && e.weapon != 6 && S->started) current = true, call("Worm_Damaged_Current");
            put("DamagedWorm.Id", slotOf(e.worm)), call("Worm_Damaged");
        }
        if (g.selfHurt && !S->hurtSent && !current && S->started) call("Worm_Damaged_Current");  // drowned (0x5ad77d), vapourised: no Hurt event
        S->hurtSent = g.selfHurt;
        for (size_t i = 0; i < g.worms.size() && i < S->died.size(); i++) {  // Worm.Died at the worm's blast (0x5a6970)
            bool blast = std::any_of(ev.begin(), ev.end(), [&](const GameEvent &e) { return e.kind == GameEvent::Death && e.worm == (int)i; });
            const Worm &w = g.worms[i];
            bool gone = blast || (!w.alive && !w.drowned && S->wormSlot[i] >= 0 && g.dyingWorm != (int)i &&
                                  std::find(g.deathQueue.begin(), g.deathQueue.end(), (int)i) == g.deathQueue.end() && w.hp <= 0 && S->started);
            if (!gone || S->died[i]) continue;
            S->died[i] = true;
            put("DeadWorm.Id", slotOf((int)i)), call("Worm_Died");
        }
        for (const Object &o : S->tagged) {  // a crate with an Index left: collected, sunk or destroyed
            if (std::any_of(g.objects.begin(), g.objects.end(), [&](const Object &x) { return x.tag == o.tag && x.type == o.type; })) continue;
            bool got = std::any_of(ev.begin(), ev.end(), [&](const GameEvent &e) { return e.kind == GameEvent::Collect && Vector3Distance(e.pos, o.pos) < 1; });
            put("Crate.Index", o.tag);
            call(got ? "Crate_Collected" : o.pos.y < g.water ? "Crate_Sunk" : "Crate_Destroyed");  // 0x5cb748: any crate type
        }
        for (const GameEvent &e : ev)  // every payload's teardown posts it (0x580560)
            if (e.kind == GameEvent::Deleted) put("Payload.Deleted.Id", e.weapon >= 0 ? e.weapon : ++S->nextId), call("Payload_Deleted");
        triggers();
    }

    // 0x5d49b0 / 0x5d47f0: Trigger.Index (and Trigger.Collector, a slot), then its message; also before a queued callback, as
    // a skipped movie's blast posts Trigger.Destroyed before its EFMV.Terminated
    static void triggers() {
        Game &g = *G;
        std::vector<Game::Trigger> gone;
        for (size_t i = 0; i < g.triggers.size();) if (g.triggers[i].gone) gone.push_back(g.triggers[i]), g.triggers.erase(g.triggers.begin() + i); else i++;
        static const char *const FN[] = {"", "Trigger_Collected", "Trigger_SheepCollected", "Trigger_PayloadCollected", "Trigger_Destroyed"};
        for (const Game::Trigger &t : gone) {
            put("Trigger.Index", t.index);
            if (t.gone != Game::TRIG_DESTROYED) put("Trigger.Collector", slotOf(t.collector));
            call(FN[t.gone]);
        }
    }
};

// The 14 C functions (0x6958d2). No C++ object with a destructor is alive in them: a Lua error longjmps out.
static int lSend(lua_State *L) {
    const char *m = lua_tostring(L, 1);
    if (m) ScriptHost::message(m, lua_type(L, 2) == LUA_TNUMBER ? (float)lua_tonumber(L, 2) : 0, lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr);
    return 0;
}

static const Val *getData(const char *k) {
    ScriptHost::pullKey(k);
    const Val *v = key(k);
    if (!v) count(S->missing, k), TraceLog(LOG_WARNING, "script: GetData %s : Incorrect DataID", k);
    return v;
}
static int lGet(lua_State *L) {
    const char *k = lua_tostring(L, 1);
    const Val *v = k ? getData(k) : nullptr;
    if (!v || v->t == 'x') return 0;
    if (v->t == 's') lua_pushstring(L, v->s.c_str());
    else if (v->t == 'b') lua_pushboolean(L, v->n != 0);
    else lua_pushnumber(L, (lua_Number)v->n);
    return 1;
}

// Keys the sim reads or that only feed our handlers; RetreatTime: every weapon rewrites it on firing (turn.md §6.6). The parameters of
// a message counted itself (CreateTrigger, NewEmitter, ShakeStart, the commentary) are not counted twice.
static bool modelled(const char *k) {
    static const char *USED[] = {"Crate.", "EFMV.MovieName", "Explosion.", "TurnTime", "HotSeatTime", "RoundTime", "PostActivityTime", "DefaultRetreatTime",
                                 "RetreatTime", "Wind.", "DoubleDamage", "Land.Indestructable", "Jetpack.Fuel", "Particle.Rain.Prob",
                                 "Trigger.Spawn", "Trigger.Radius", "Trigger.Index", "Trigger.Team", "Trigger.HitPoints", "Trigger.SheepCollect",
                                 "Trigger.PayloadCollect", "Trigger.GirderCollect", "Trigger.WormCollect", "Trigger.Visibility", "Trigger.AffectsAI", "Particle.DetailObject", "Particle.Name",
                                 "Camera.Shake.", "CommentaryPanel.", "HUD.", "BriefingText.", "Water.Level", "Mine.",
                                 "Jetpack.InitFuel", "Challenge.EndlessGun", "SameTeamNextTurn", "Turn.Boring", "Turn.MaxDamage",
                                 "GameToFrontEndDelayTime"};  // the last: no reader in the exe, as in W4M
    for (const char *u : USED) if (!strncmp(k, u, strlen(u))) return true;  // prefixes
    return false;
}

static void setData(const char *k, int type, double n, const char *s) {
    Val *v = key(k);
    if (!modelled(k)) count(S->ignored, std::string("SetData ") + k);
    if (!v) return count(S->missing, k), (void)TraceLog(LOG_WARNING, "script: SetData %s : Incorrect DataID", k);
    if (v->t == 's') { if (s) putStr(k, s); }
    else if (type == LUA_TNUMBER || type == LUA_TBOOLEAN) put(k, n);  // ints truncate (0x6fe690)
    else TraceLog(LOG_WARNING, "script: SetData %s : Data is not a number", k);
    ScriptHost::pushKey(k);
}
static int lSet(lua_State *L) {
    const char *k = lua_tostring(L, 1);
    int t = lua_type(L, 2);
    double n = t == LUA_TBOOLEAN ? lua_toboolean(L, 2) : lua_tonumber(L, 2);  // before lua_tostring turns a number into a string
    if (k) setData(k, t, n, t == LUA_TSTRING || t == LUA_TNUMBER ? lua_tostring(L, 2) : nullptr);
    return 0;
}

static int startTimer(const char *fn, double ms) {
    for (int i = 0; i < TIMERS; i++)
        if (!S->timers[i].live) return S->timers[i] = {fn, nowMs() + (int64_t)ms, true}, S->dirty = true, i;
    TraceLog(LOG_WARNING, "script: Too many timers requested");
    return -1;
}
static int lStartTimer(lua_State *L) {  // 0x69810f: the global function `name` runs at the deadline; returns the slot
    const char *fn = lua_tostring(L, 1);
    int slot = fn && lua_gettop(L) == 2 ? startTimer(fn, lua_tonumber(L, 2)) : -1;
    if (slot < 0) return 0;
    lua_pushnumber(L, (lua_Number)slot);
    return 1;
}
static int lCancelTimer(lua_State *L) {
    int i = (int)lua_tonumber(L, 1);
    if (i >= 0 && i < TIMERS) S->timers[i].live = false, S->dirty = true;
    return 0;
}

// Enum fields the scripts touch (pe.py schema): WeaponNameEnum (0x90c920) and WXFE_UnlockableStateEnum
static const char *WEAPON_ENUM[] = {
    "kWeaponOneBeforeFirst", "kWeaponBazooka", "kWeaponGrenade", "kWeaponClusterGrenade", "kWeaponAirstrike", "kWeaponDynamite", "kWeaponHolyHandGrenade",
    "kWeaponBananaBomb", "kWeaponLandmine", "kWeaponShotgun", "kWeaponBaseballBat", "kWeaponProd", "kWeaponFirePunch", "kWeaponHomingMissile", "kWeaponFlood",
    "kWeaponSheep", "kWeaponGasCanister", "kWeaponOldWoman", "kWeaponConcreteDonkey", "kWeaponSuperSheep", "kWeaponStarburst", "kWeaponFactoryWeapon",
    "kWeaponAlienAbduction", "kWeaponFatkins", "kWeaponScouser", "kWeaponNoMoreNails", "kWeaponPoisonArrow", "kWeaponSentryGun", "kWeaponSniperRifle",
    "kWeaponSuperAirstrike", "kWeaponClusterBomb", "kWeaponBananette", "kWeaponOneAfterLast", "kUtilityOneBeforeFirst", "kUtilityGirder", "kUtilityNinjaRope",
    "kUtilityParachute", "kUtilityJetpack", "kUtilitySkipGo", "kUtilitySurrender", "kUtilityChangeWorm", "kUtilityRedbull", "kUtilityBubbleTrouble",
    "kUtilityBinoculars", "kUtilityDoubleDamage", "kUtilityCrateShower", "kUtilityCrateSpy", "kUtilityArmour", "kUtilityOneAfterLast", "kMysteryOneBeforeFirst",
    "kMysteryMineLayer", "kMysteryMineTriplet", "kMysteryBarrelTriplet", "kMysteryFlood", "kMysteryDisarm", "kMysteryTeleport", "kMysteryQuickWalk",
    "kMysteryLowGravity", "kMysteryDoubleTurnTime", "kMysteryHealth", "kMysteryDamage", "kMysterySuperHealth", "kMysterySpecialWeapon", "kMysteryBadPoison",
    "kMysteryGoodPoison", "kMysteryOneAfterLast", "kInventorySize", "kWeaponUndefined", "kWeaponTerminal"};
static const char *UNLOCK_STATE[] = {"kUS_Hidden", "kUS_Purchasable", "kUS_Unlocked"};
struct EnumField { const char *type, *field; const char *const *names; int n; };
static const EnumField ENUMS[] = {{"WormDataContainer", "WeaponIndex", WEAPON_ENUM, 69}, {"TeamDataContainer", "WormpotSuperWeapon", WEAPON_ENUM, 69},
                                  {"WXFE_UnlockableItem", "State", UNLOCK_STATE, 3}};
static const EnumField *enumOf(const Ctn &c, const std::string &f) {
    for (const EnumField &e : ENUMS) if (c.type == e.type && f == e.field) return &e;
    return nullptr;
}
static size_t enumPrefix(const EnumField &e) {  // 0x6c7078: the shortest common prefix of adjacent value names
    size_t p = SIZE_MAX;
    for (int i = 1; i < e.n; i++) {
        size_t k = 0;
        while (e.names[i - 1][k] && e.names[i - 1][k] == e.names[i][k]) k++;
        p = std::min(p, k);
    }
    return p;
}

static void pushCtn(lua_State *L, const Ctn &c) {
    lua_newtable(L);
    for (const auto &kv : c.f) {
        const Val &v = kv.second;
        if (v.t == 'x') continue;
        lua_pushstring(L, kv.first.c_str());
        const EnumField *e = enumOf(c, kv.first);
        if (e) {  // the value's name less the prefix: State "Unlocked"
            int i = (int)v.n;
            if (i >= 0 && i < e->n) lua_pushstring(L, e->names[i] + enumPrefix(*e));
            else lua_pushstring(L, TextFormat("Invalid (%d)", i));
        } else if (v.t == 's') lua_pushstring(L, v.s.c_str());
        else if (v.t == 'b') lua_pushboolean(L, v.n != 0);
        else lua_pushnumber(L, (lua_Number)v.n);
        lua_rawset(L, -3);
    }
}
static Ctn *openCtn(const char *n, const char *fn) {
    Ctn *c = n ? ctn(n) : nullptr;
    if (!c) count(S->missing, n ? n : "?"), TraceLog(LOG_WARNING, "script: %s %s : Incorrect DataID", fn, n ? n : "?");
    else ScriptHost::pull(n);
    return c;
}
static int lQuery(lua_State *L) {
    Ctn *c = openCtn(lua_tostring(L, 1), "QueryContainer");
    if (!c) return 0;
    pushCtn(L, *c);
    return 1;
}
static int lEdit(lua_State *L) {  // 0x6983fa: (lock, table); CloseContainer(lock) writes the table back
    const char *n = lua_tostring(L, 1);
    Ctn *c = openCtn(n, "EditContainer");
    if (!c) return 0;
    S->locks.push_back(-1), S->lockName.push_back(n);
    lua_pushlightuserdata(L, (void *)(intptr_t)S->locks.size());
    pushCtn(L, *c);
    lua_pushvalue(L, -1);
    S->locks.back() = luaL_ref(L, LUA_REGISTRYINDEX);
    return 2;
}
static void closeCtn(lua_State *L, int id) {
    Ctn *c = ctn(S->lockName[id]);
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->locks[id]);
    for (auto &kv : c->f) {
        Val &v = kv.second;
        if (v.t == 'x') continue;
        lua_pushstring(L, kv.first.c_str());
        lua_rawget(L, -2);
        int t = lua_type(L, -1);
        if (const EnumField *e = enumOf(*c, kv.first); e && t == LUA_TSTRING) {  // by name, full or as read
            for (int i = 0; i < e->n; i++) if (!strcmp(lua_tostring(L, -1), e->names[i]) || !strcmp(lua_tostring(L, -1), e->names[i] + enumPrefix(*e))) v.n = i;
        } else if (v.t == 's') { if (t == LUA_TSTRING) v.s = lua_tostring(L, -1); }
        else if (t == LUA_TBOOLEAN) v.n = lua_toboolean(L, -1);
        else if (t == LUA_TNUMBER) v.n = v.t == 'f' ? (double)(float)lua_tonumber(L, -1) : v.t == 'b' ? lua_tonumber(L, -1) != 0 : (double)(int64_t)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, S->locks[id]);
    S->locks[id] = -1;
    ScriptHost::push(S->lockName[id], nullptr);
}
static int lClose(lua_State *L) {
    int id = (int)(intptr_t)lua_touserdata(L, 1) - 1;
    if (id >= 0 && id < (int)S->locks.size() && S->locks[id] >= 0) closeCtn(L, id);
    return 0;
}
static void copyCtn(const char *from, const char *to) {
    Ctn *a = openCtn(from, "CopyContainer"), *b = ctn(to);
    if (!a) return;
    if (!b) return count(S->missing, to), (void)TraceLog(LOG_WARNING, "script: CopyContainer %s : Incorrect DataID", to);
    *b = *a;
    std::string f = from;
    ScriptHost::push(to, &f);
}
static int lCopy(lua_State *L) {
    const char *a = lua_tostring(L, 1), *b = lua_tostring(L, 2);
    if (a && b) copyCtn(a, b);
    return 0;
}
static int lLog(lua_State *L) {  // echo, log: debug strings
    for (int i = 1; i <= lua_gettop(L); i++) if (const char *s = lua_tostring(L, i)) TraceLog(LOG_DEBUG, "script: %s", s);
    return 0;
}

static bool loadJson(const std::string &path, bool level) {
    char *txt = LoadFileText(path.c_str());
    Json j;
    bool ok = txt && Json::parse(txt, j);
    UnloadFileText(txt);
    if (!ok) return TraceLog(LOG_WARNING, "script: %s unreadable", path.c_str()), false;
    auto val = [](const Json &x) {
        Val v;
        if (x.type == Json::Num) v.t = x.frac ? 'f' : 'i', v.n = x.num;
        else if (x.type == Json::Bool) v.t = 'b', v.n = x.b;
        else if (x.type == Json::Str) v.t = 's', v.s = x.str;
        return v;
    };
    for (const auto &kv : j["keys"].obj) S->keys[kv.first] = val(kv.second);
    for (const auto &kv : j["containers"].obj) {
        Ctn &c = S->ctns[kv.first];
        c = Ctn{kv.second["_type"].s(), {}};
        for (const auto &f : kv.second.obj) if (f.first != "_type") c.f.push_back({f.first, val(f.second)});
    }
    for (const auto &kv : j["movies"].obj) S->movies[kv.first] = kv.second;
    (void)level;
    return true;
}

static bool loadChunk(const std::string &dir, const std::string &name) {
    int n = 0;
    unsigned char *b = LoadFileData((dir + name + ".lub").c_str(), &n);
    if (!b) return TraceLog(LOG_WARNING, "script: no %s.lub", name.c_str()), false;
    int r = luaL_loadbuffer(S->L, (const char *)b, n, name.c_str());
    UnloadFileData(b);
    if (!r) r = lua_pcall(S->L, 0, 0, 0);
    if (r) S->errors++, S->lastError = lua_tostring(S->L, -1) ? lua_tostring(S->L, -1) : "?", TraceLog(LOG_WARNING, "script %s: %s", name.c_str(), S->lastError.c_str());
    return !r;
}

bool scriptStart(Game &g, const std::string &dir, const std::string &script, const std::string &bank) {
    g.script = std::make_shared<ScriptState>();
    Using u(g);
    if (!loadJson(dir + "data.json", false) || !loadJson(dir + bank + ".json", true)) return g.script.reset(), false;
    for (auto &kv : S->keys) if (!kv.first.compare(0, 6, "Crate.") || !kv.first.compare(0, 8, "Trigger.")) S->resets.insert(kv);
    // GameLogicService init 0x4f5bd0, before the scripts: the default inventories and delays emptied (LOCAL's SkipGo / Surrender -1
    // too), then copied into the 16 worm, 4 team and 4 alliance inventories and the 4 delay tables
    for (const char *d : {"Inventory.Worm.Default", "Inventory.Team.Default", "Inventory.Alliance.Default", "Inventory.WeaponDelays.Default"})
        if (Ctn *c = ctn(d)) for (auto &f : c->f) if (f.second.t != 's' && f.second.t != 'x') f.second.n = 0;
    auto copy = [&](const char *from, const char *fmt, int n) { for (int i = 0; i < n; i++) if (Ctn *c = ctn(slotName(fmt, i)); c && ctn(from)) *c = *ctn(from); };
    copy("Inventory.Worm.Default", "Inventory.Worm%02d", SLOTS), copy("Inventory.Team.Default", "Inventory.Team%02d", 4);
    copy("Inventory.Alliance.Default", "Inventory.Alliance%02d", 4), copy("Inventory.WeaponDelays.Default", "Inventory%d.WeaponDelays", 4);
    lua_State *L = S->L = lua_open();
    luaopen_base(L), luaopen_math(L), lua_settop(L, 0);  // XLuaBaseLibrary, XLuaMathLibrary (0x6958d2)
    static const luaL_reg FNS[] = {{"SendMessage", lSend}, {"SendFloatMessage", lSend}, {"SendIntMessage", lSend}, {"SendStringMessage", lSend},
                                   {"GetData", lGet}, {"SetData", lSet}, {"StartTimer", lStartTimer}, {"CancelTimer", lCancelTimer},
                                   {"EditContainer", lEdit}, {"CloseContainer", lClose}, {"QueryContainer", lQuery}, {"CopyContainer", lCopy},
                                   {"echo", lLog}, {"log", lLog}};
    for (const luaL_reg &f : FNS) lua_register(L, f.name, f.func);
    if (!loadChunk(dir, "stdlib") || !loadChunk(dir, "lib_help") || !loadChunk(dir, script)) return g.script.reset(), false;
    ScriptHost::push("GM.SchemeData", nullptr), ScriptHost::push("kMineFactoryData", nullptr);
    for (const char *k : {"Water.Level", "Mine.MinFuse", "Mine.MaxFuse", "Mine.DudProbability", "Mine.DetonationType", "Jetpack.InitFuel", "Challenge.EndlessGun"}) ScriptHost::pushKey(k);
    g.phase = Phase::Settle, g.timer = 0;  // no turn until the script's StartFirstTurn
    g.indestructible = false, g.noTurn = true;
    call("Initialise");
    if (!S->built) TraceLog(LOG_WARNING, "script %s: no WormManager.Reinitialise", script.c_str());
    return S->built;
}

void scriptEvent(Game &g, const char *fn) {
    Using u(g);
    call(fn);
}

void scriptNoActivity(Game &g) {
    Using u(g);
    lua_getglobal(S->L, "WaitUntilNoActivity");
    bool wait = lua_toboolean(S->L, -1);
    lua_pop(S->L, 1);
    if (wait) call("GameLogic_NoActivity");
}

int scriptPostActivity(const Game &g) {
    Using u(g);
    return msTicks((int)num("PostActivityTime"));
}

void scriptStep(Game &g) {
    Using u(g);
    if (S->built) ScriptHost::bookAmmo();
    ScriptHost::events();
    if (S->roundOn && !S->timedOut && roundMs() >= (int64_t)num("RoundTime")) S->timedOut = true, call("Timer_GameTimedOut");  // 0x50f17d
    if (S->idleDue >= 0 && nowMs() >= S->idleDue) S->idleDue = -1, call("Timer_TurnTimedOut");
    for (bool fired = true; fired;) {  // due timers in deadline order (each a delayed message)
        fired = false;
        int k = -1;
        for (int i = 0; i < TIMERS; i++) if (S->timers[i].live && S->timers[i].due <= nowMs() && (k < 0 || S->timers[i].due < S->timers[k].due)) k = i;
        if (k >= 0) {
            S->timers[k].live = false, fired = true;
            std::string fn = S->timers[k].fn;
            call(fn.c_str());
        }
    }
    while (!S->queue.empty()) {
        ScriptHost::triggers();
        std::string fn = S->queue.front();
        S->queue.erase(S->queue.begin());
        call(fn.c_str());
    }
    if (S->endless && g.phase == Phase::Aim) g.timer = std::max(g.timer, 99 * 60);
    S->tagged.clear();
    for (const Object &o : g.objects) if (o.tag >= 0 && crate(o)) S->tagged.push_back(o);
    g.events.insert(g.events.end(), S->ui.begin(), S->ui.end());  // also Initialise's, which ran before the first step cleared the events
    S->ui.clear();
}

// Canonical bytes of the Lua globals (functions left out), run protected: only state-owned buffers are touched
static void ser(lua_State *L, int idx, int depth) {
    std::string &b = S->buf;
    int t = lua_type(L, idx);
    b += (char)t;
    if (t == LUA_TNUMBER) { float f = (float)lua_tonumber(L, idx); b.append((const char *)&f, 4); }
    else if (t == LUA_TSTRING) b.append(lua_tostring(L, idx), lua_strlen(L, idx)), b += '\0';
    else if (t == LUA_TBOOLEAN) b += (char)lua_toboolean(L, idx);
    else if (t == LUA_TLIGHTUSERDATA) { intptr_t p = (intptr_t)lua_touserdata(L, idx); b.append((const char *)&p, sizeof p); }
    else if (t == LUA_TTABLE && depth < ScriptState::DEPTH) {  // entries sorted by their bytes: lua_next follows the hash layout
        auto &ent = S->ent[depth];
        ent.clear();
        size_t start = b.size();
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            bool skip = lua_type(L, -1) == LUA_TFUNCTION || (lua_type(L, -2) == LUA_TSTRING && (!strcmp(lua_tostring(L, -2), "_G") || !strcmp(lua_tostring(L, -2), "_LOADED")));
            if (!skip) {
                size_t at = b.size();
                int top = lua_gettop(L);
                ser(L, top - 1, depth + 1), ser(L, top, depth + 1);
                ent.push_back({at, b.size() - at});
            }
            lua_pop(L, 1);
        }
        std::sort(ent.begin(), ent.end(), [&](const std::pair<size_t, size_t> &x, const std::pair<size_t, size_t> &y) { return b.compare(x.first, x.second, b, y.first, y.second) < 0; });
        std::string &tmp = S->tmp[depth];
        tmp.clear();
        for (auto &e : ent) tmp.append(b, e.first, e.second);
        b.replace(start, std::string::npos, tmp);
    }
}
static int serGlobals(lua_State *L) {
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    ser(L, lua_gettop(L), 0);
    return 0;
}

uint32_t scriptChecksum(const Game &g) {
    Using u(g);
    if (!S->dirty) return S->sum;
    S->buf.clear();
    lua_cpcall(S->L, serGlobals, nullptr);
    std::string &b = S->buf;
    auto val = [&](const std::string &k, const Val &v) { b += k, b += v.t, b.append((const char *)&v.n, sizeof v.n), b += v.s, b += '\0'; };
    for (const std::string &k : S->wroteKeys) val(k, S->keys[k]);
    for (const std::string &k : S->wroteCtns) for (auto &f : S->ctns[k].f) val(k + "." + f.first, f.second);
    for (const Timer &t : S->timers) if (t.live) b += t.fn, b.append((const char *)&t.due, sizeof t.due);
    for (const std::string &q : S->queue) b += q;
    b += (char)(S->started | S->hurtSent << 1 | S->endless << 2 | S->roundOn << 3 | S->timedOut << 4);
    b.append((const char *)&S->roundStart, sizeof S->roundStart), b.append((const char *)&S->idleDue, sizeof S->idleDue);
    b.append((const char *)&S->roundHeld, sizeof S->roundHeld), b.append((const char *)&S->holdFrom, sizeof S->holdFrom);
    b.append((const char *)&S->nextId, sizeof S->nextId);
    for (int a : S->ammoSlot) b.append((const char *)&a, sizeof a);
    for (int c : S->toWorm) b.append((const char *)&c, sizeof c);
    uint32_t h = 2166136261u;
    for (unsigned char c : b) h = (h ^ c) * 16777619u;
    S->dirty = false;
    return S->sum = h;
}

bool scriptDefines(const Game &g, const char *fn) {
    if (!g.script) return false;
    lua_State *L = g.script->L;
    lua_getglobal(L, fn);
    bool f = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return f;
}

const char *scriptCrateGraphic(const Game &g, int index) {
    if (!g.script || index < 0) return nullptr;
    auto it = g.script->graphic.find(index);
    return it == g.script->graphic.end() ? nullptr : it->second.c_str();
}

double scriptNum(const Game &g, const char *k, double def) {
    if (!g.script) return def;
    Using u(g);
    return key(k) ? num(k, def) : def;
}

bool scriptOutro(const Game &g) {
    if (!g.script) return false;
    Using u(g);
    const Val *m = key("EFMV.GameOverMovie");
    return (m && !m->s.empty()) || num("EFMV.GameOverMovie.Off") != 0;
}

ScriptReport scriptReport(const Game &g) {
    ScriptReport r;
    if (!g.script) return r;
    r.ignored.assign(g.script->ignored.begin(), g.script->ignored.end());
    r.missingKeys.assign(g.script->missing.begin(), g.script->missing.end());
    r.errors = g.script->errors, r.lastError = g.script->lastError;
    return r;
}

ScriptHud scriptHud(const Game &g) {
    ScriptHud h;
    if (!g.script) return h;
    Using u(g);
    h.counter = num("HUD.Counter.Active") != 0, h.value = (int)num("HUD.Counter.Value"), h.percent = num("HUD.Counter.Percent") == 1;  // 0x5e57c0
    h.tenths = num("HUD.Clock.DisplayTenths") != 0, h.roundClock = num("HUD.Clock.DisplayRoundTime", 1) != 0, h.defaults = !S->noDefault, h.endless = S->endless;
    h.roundTime = (int)num("RoundTime");  // HudClockEntity 0x5f03f0: RoundTimeRemaining if RoundTime > 0, else ElapsedRoundTime (0 at Timer.StartGame)
    h.clockMs = h.roundTime > 0 ? std::max<int64_t>(0, h.roundTime - (S->roundOn ? roundMs() : 0)) : roundMs();
    return h;
}

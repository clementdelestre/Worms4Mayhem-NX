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
    int64_t roundStart = 0;
    std::vector<int> poison;  // Worm.ApplyPoison's damage, for the next GameLogic.ApplyDamage
    std::vector<Object> tagged;  // last tick's crates with an Index
    std::vector<bool> died;
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

static int64_t nowMs() { return (int64_t)G->clock * 1000 / 60; }  // W4M game time [0x96d030]+0x38 (docs/w4m/missions.md §23.1)

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

static int slotOf(int worm) { return worm >= 0 && worm < (int)S->wormSlot.size() ? S->wormSlot[worm] : -1; }
static std::string slotName(const char *fmt, int i) { char b[40]; snprintf(b, sizeof b, fmt, i); return b; }

// Live game state into the store / containers before a script reads them, and back after it writes
struct ScriptHost {
    static int alliance(int team) { return (int)fnum(ctn(slotName("Team.Data%02d", team)), "AlliedGroup", team); }

    static void pullKey(const char *k) {
        Game &g = *G;
        if (!strcmp(k, "ObjectCount.Active")) put(k, !g.shots.empty() || g.active() || !g.countGroup.empty() || !g.deathQueue.empty() || g.dyingWorm >= 0);
        else if (!strcmp(k, "RoundTimeRemaining") && S->started) put(k, std::max<int64_t>(0, (int64_t)num("RoundTime") - (S->roundOn ? nowMs() - S->roundStart : 0)));
        else if (!strcmp(k, "CurrentTeamIndex") && S->started) put(k, g.worms[g.current].team);
        else if (!strcmp(k, "ActiveWormIndex") && S->started) put(k, slotOf(g.current));
        else if (!strcmp(k, "FCS.GameOver")) put(k, g.phase == Phase::GameOver);
        else if (!strcmp(k, "DoubleDamage")) put(k, g.doubleDamage);
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
    }

    // Effective ammo = worm + team + alliance inventories, -1 if any is infinite (0x50d900); ours is per team: the worm part is
    // Inventory.Worm.Default, and what the game changes (shots, crates) is booked to the alliance
    static int inv(Ctn *c, const char *f) { return (int)fnum(c, f); }
    static void pushInventory(int team) {
        Game &g = *G;
        if (team >= g.teams) return;
        Ctn *w = ctn("Inventory.Worm.Default"), *t = ctn(slotName("Inventory.Team%02d", team)), *a = ctn(slotName("Inventory.Alliance%02d", alliance(team)));
        if (!t) return;
        std::fill(g.ammo[team].begin(), g.ammo[team].end(), 0);
        for (auto &kv : t->f) {
            int k = weaponOf(kv.first.c_str());
            if (k < 0) continue;
            int x = inv(w, kv.first.c_str()), y = inv(t, kv.first.c_str()), z = inv(a, kv.first.c_str());
            g.ammo[team][k] = x < 0 || y < 0 || z < 0 ? -1 : x + y + z;
        }
    }
    static void pullAlliance(int a) {
        Game &g = *G;
        Ctn *al = ctn(slotName("Inventory.Alliance%02d", a)), *w = ctn("Inventory.Worm.Default");
        for (int team = 0; team < g.teams && al; team++) {
            if (alliance(team) != a) continue;
            Ctn *t = ctn(slotName("Inventory.Team%02d", team));
            for (auto &kv : al->f) {
                int k = weaponOf(kv.first.c_str()), x = inv(w, kv.first.c_str()), y = inv(t, kv.first.c_str());
                if (k < 0) continue;
                int n = g.ammo[team][k];
                if (n >= 0) kv.second.n = std::max(0, n - std::max(0, x) - std::max(0, y));
                else if (x >= 0 && y >= 0) kv.second.n = -1;
            }
            return;
        }
    }

    static void pull(const std::string &n) {
        Game &g = *G;
        int i = -1;
        if (sscanf(n.c_str(), "Inventory.Alliance%d", &i) == 1) pullAlliance(i);
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
        } else if (sscanf(n.c_str(), "Inventory.Team%d", &i) == 1) {
            if (S->built) pushInventory(i);
        } else if (sscanf(n.c_str(), "Inventory.Alliance%d", &i) == 1) {
            for (int t = 0; t < g.teams && S->built; t++) if (alliance(t) == i) pushInventory(t);
        } else if (sscanf(n.c_str(), "Inventory%d.WeaponDelays", &i) == 1) {
            if (S->built && i < g.teams) for (auto &kv : ctn(n)->f) if (int k = weaponOf(kv.first.c_str()); k >= 0) g.delays[i][k] = (int)kv.second.n;
        } else if (n == "GM.SchemeData") {  // DropRandomCrate 0x4fab20 reads the live scheme
            Ctn *c = ctn(n);
            Scheme &s = g.cfg.scheme;
            s.crateChance = (uint8_t)fnum(c, "RandomCrateChancePerTurn"), s.weaponShare = (uint8_t)fnum(c, "WeaponChance");
            s.healthShare = (uint8_t)fnum(c, "HealthChance"), s.utilityShare = (uint8_t)fnum(c, "UtilityChance"), s.mysteryShare = (uint8_t)fnum(c, "MysteryChance");
        }
    }

    // WormManager.Reinitialise: the worms of the Worm.DataNN slots the script set up, by TeamIndex then slot (our worms are team-major)
    static void reinitialise() {
        Game &g = *G;
        if (S->built) return count(S->ignored, "WormManager.Reinitialise (again)");
        int teams = 1, per = 1, n[4] = {};
        for (int s = 0; s < SLOTS; s++)
            if (S->touched[s]) {
                int t = std::min(3, (int)fnum(ctn(slotName("Worm.Data%02d", s)), "TeamIndex"));
                teams = std::max(teams, t + 1), per = std::max(per, ++n[t]);
            }
        const float cx = Terrain::NX * Terrain::VOX / 2, cz = Terrain::NZ * Terrain::VOX / 2;
        std::vector<Worm> worms(teams * per, Worm{{cx, -50, cz}, {0, 0, 0}, 0, 0.3f, 0, 0, false, false});
        S->wormSlot.assign(worms.size(), -1);
        std::fill(n, n + 4, 0);
        for (int s = 0; s < SLOTS; s++) {
            if (!S->touched[s]) continue;
            Ctn *c = ctn(slotName("Worm.Data%02d", s));
            int t = std::min(3, (int)fnum(c, "TeamIndex")), i = t * per + n[t]++;
            Worm &w = worms[i];
            w.team = t, w.turns = fnum(c, "IsAllowedToTakeTurn", 1) != 0;
            S->slotWorm[s] = i, S->wormSlot[i] = s;
            const Val *sp = field(c, "Spawn");
            Vector3 p, hit;
            if (fnum(c, "Active") == 0 || !sp || !marker(g, sp->s, p)) {
                if (fnum(c, "Active") != 0) TraceLog(LOG_WARNING, "script: worm %d has no spawn marker '%s'", s, sp ? sp->s.c_str() : "");
                continue;
            }
            if (g.terrain.raycast({Vector3Add(p, {0, 0.8f, 0}), {0, -1, 0}}, 40, &hit)) p = {hit.x, hit.y + Game::R + 0.3f, hit.z};
            w.pos = p, w.yaw = atan2f(cx - p.x, cz - p.z), w.alive = true, w.hp = w.counted = std::max(1, (int)fnum(c, "Energy"));
        }
        g.worms = worms, g.teams = teams, g.perTeam = per, g.current = 0;
        g.nextWorm.assign(teams, 0), g.lastHitTeam.assign(worms.size(), -1), g.fuses.assign(teams, 3), g.picked.assign(teams, g.weapon);
        g.ammo.assign(teams, std::vector<int>(WEAPONS.size(), 0)), g.delays.assign(teams, std::vector<int>(WEAPONS.size(), 0));
        g.idle.assign(teams, 0), g.surrendered.assign(teams, 0), g.spy.assign(teams, 0), g.special.assign(worms.size(), 0), g.superWeapon.assign(teams, 0);
        S->died.assign(worms.size(), false);
        S->built = true;
        g.cfg.teams = teams, g.cfg.wormsPerTeam = per;
        g.cfg.teamSetup.resize(teams);
        for (int t = 0; t < teams; t++) {
            Ctn *c = ctn(slotName("Team.Data%02d", t));
            GameConfig::Team &ts = g.cfg.teamSetup[t];
            if (const Val *nm = field(c, "Name")) ts.name = nm->s;
            int lv = 0;
            for (int k = 0; k < per && !lv; k++) if (S->wormSlot[t * per + k] >= 0) lv = S->ai[S->wormSlot[t * per + k]];
            ts.cpu = fnum(c, "IsAIControlled") != 0 ? (uint8_t)Clamp(lv ? lv : 3, 1, 5) : 0;  // no AIParams.CPUn copied: ours, CPU3
            pushInventory(t);
            push(slotName("Inventory%d.WeaponDelays", t), nullptr);
        }
    }

    // GameLogic.ActivateNextWorm: the team that played spends a delay turn (0x5b5a5f), then the next worm; stdlib's clocks
    static void activateNext() {
        Game &g = *G;
        if (!S->built || g.phase == Phase::GameOver) return;
        if (S->started) {
            const Worm &w = g.worms[g.current];
            for (int &d : g.delays[w.team]) d = std::max(0, d - 1);
            g.picked[w.team] = g.weapon, g.beginTurn(w.team);
        } else g.beginTurn(g.teams - 1);
        S->started = true, S->hurtSent = false;
        int turn = (int)num("TurnTime");
        S->endless = turn <= 0;  // Timer.StartTurn: no turn clock (0x50f980)
        g.timer = turn > 0 ? std::max(1, msTicks(turn)) : 99 * 60;
        g.hotSeat = msTicks((int)num("HotSeatTime"));
        g.cfg.scheme.turnTime = (uint8_t)Clamp(turn / 1000, 1, 255);
    }

    static void endTurn() {  // GameLogic.EndTurn: the worm loses control; stdlib then waits for no activity
        Game &g = *G;
        if (g.phase == Phase::GameOver) return;
        g.phase = Phase::Settle, g.timer = 1, g.jumpDelay = 0, g.roped = g.jetting = g.chute = false;
    }

    static void explode(const std::string &at, float dmg, float imp, float reach, float crater, float impReach, float offset) {
        Game &g = *G;
        Vector3 p;
        if (!marker(g, at, p)) return (void)TraceLog(LOG_WARNING, "script: no explosion marker '%s'", at.c_str());
        g.explode(p, Blast{crater / 20, reach / 20, dmg, imp * 50, impReach / 20, offset / 20});  // units, units/ms -> m, m/s
    }

    static void movie() {
        const Val *nm = key("EFMV.MovieName");
        auto it = nm ? S->movies.find(nm->s) : S->movies.end();
        if (it == S->movies.end()) TraceLog(LOG_WARNING, "script: no movie '%s'", nm ? nm->s.c_str() : "");
        else
            for (const Json &e : it->second.arr) {  // the skip: Critical events only (acting.md §19)
                std::string t = e[0].s();
                if (t == "CreateExplosion") explode(e[1].s(), e[2].f(), e[3].f(), e[4].f(), e[5].f(), e[6].f(), e[8].f());
                else if (t != "DeleteBorders" && t != "DeleteEmitter") count(S->ignored, "EFMV " + t);  // borders, emitters: render only
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
        spawnObject(g, type("target") ? Object::Target : Object::Crate, p, w, false, (int)num("Crate.Index", -1));
    }

    static void message(const char *m, float a, const char *s) {
        Game &g = *G;
        auto is = [&](const char *x) { return !strcmp(m, x); };
        S->dirty = true;
        if (is("GameLogic.ActivateNextWorm")) activateNext();
        else if (is("GameLogic.EndTurn")) endTurn();
        else if (is("Timer.StartGame")) S->roundStart = nowMs(), S->roundOn = num("RoundTime") > 0;
        else if (is("Timer.StartPostActivity")) {
            int t = scriptPostActivity(g);
            if (t > 0) g.timer = -t;
            else g.timer = 0, call("Timer_PostActivityTimedOut");
        } else if (is("Worm.ApplyPoison")) S->poison = g.applyPoison();
        else if (is("GameLogic.ApplyDamage")) {
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
        else if (is("Crate.Delete")) {
            for (size_t i = 0; i < g.objects.size();) if (g.objects[i].tag == (int)a) g.objects.erase(g.objects.begin() + i); else i++;
        } else if (is("GameLogic.DropRandomCrate")) g.dropCrates(1, true);
        else if (is("EFMV.Play")) movie();
        else if (is("Explosion.Construct"))
            explode(key("Explosion.DetailObject") ? key("Explosion.DetailObject")->s : "", (float)num("Explosion.WormDamageMagnitude"), (float)num("Explosion.ImpulseMagnitude"),
                    (float)num("Explosion.WormDamageRadius"), (float)num("Explosion.LandDamageRadius"), (float)num("Explosion.ImpulseRadius"), 0);
        else if (is("Weapon.Create")) {  // 0x565770: the worm's WeaponIndex (kWeapon enum) if usable, else the first usable item
            const Worm &w = g.worms[g.current];
            int id = (int)fnum(ctn(slotName("Worm.Data%02d", slotOf(g.current))), "WeaponIndex"), k = -1;
            for (size_t i = 0; i < WEAPONS.size() && k < 0; i++) if (g.inventoryId((int)i) == id && g.usable(w.team, (int)i)) k = (int)i;
            if (k >= 0) g.weapon = k, g.secondary = -1;
            else g.firstWeapon(w.team);
        } else if (is("Jetpack.UpdateFuel")) put("Jetpack.Fuel", g.fuel * 1000);
        else if (is("Timer.StartHotSeatTimer") || is("Timer.StartTurn") || is("Timer.EndTurn") || is("Timer.EndRetreatTimer") || is("GameLogic.Turn.Started") ||
                 is("GameLogic.Turn.Ended") || is("GameLogic.AboutToApplyDamage") || is("AI.PerformDefaultAITurn") || is("AI.ExecuteActions") ||
                 is("Weapon.Delete") || is("Utility.Delete") || is("Weapon.DisableWeaponChange") || is("Net.DisableAllInput") || is("EFMV.End")) {
            // covered by the sim's own turn: ActivateNextWorm sets the clocks, EndTurn the tools, our AI polls; EFMV.End: borders
        } else {
            count(S->ignored, m);
            TraceLog(LOG_DEBUG, "script: message %s not modelled", m);
        }
        (void)s;
    }

    static void events() {
        Game &g = *G;
        bool turn = g.phase == Phase::Aim || g.phase == Phase::Flying || g.phase == Phase::Retreat;
        std::vector<GameEvent> ev = g.events;
        for (const GameEvent &e : ev) {  // 0x5ab7e0: Worm.Damaged.Current (the active worm, not poison), then Worm.Damaged with DamagedWorm.Id
            if (e.kind != GameEvent::Hurt || e.worm < 0) continue;
            if (e.worm == g.current && turn && !S->hurtSent) S->hurtSent = true, call("Worm_Damaged_Current");
            put("DamagedWorm.Id", slotOf(e.worm)), call("Worm_Damaged");
        }
        if (g.selfHurt && turn && !S->hurtSent) S->hurtSent = true, call("Worm_Damaged_Current");  // drowned, vapourised, fall
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
            bool got = std::any_of(ev.begin(), ev.end(), [&](const GameEvent &e) { return e.kind == GameEvent::Collect && Vector3Distance(e.pos, o.pos) < 0.01f; });
            put("Crate.Index", o.tag);
            call(o.type == Object::Crate && got ? "Crate_Collected" : o.pos.y < g.water ? "Crate_Sunk" : "Crate_Destroyed");
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
                                 "Trigger.PayloadCollect", "Trigger.GirderCollect", "Trigger.WormCollect", "Particle.DetailObject", "Particle.Name",
                                 "Camera.Shake.", "CommentaryPanel."};
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

static void pushCtn(lua_State *L, const Ctn &c) {
    lua_newtable(L);
    for (const auto &kv : c.f) {
        const Val &v = kv.second;
        if (v.t == 'x') continue;
        lua_pushstring(L, kv.first.c_str());
        if (v.t == 's') lua_pushstring(L, v.s.c_str());
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
        if (v.t == 's') { if (t == LUA_TSTRING) v.s = lua_tostring(L, -1); }
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
    lua_State *L = S->L = lua_open();
    luaopen_base(L), luaopen_math(L), lua_settop(L, 0);  // XLuaBaseLibrary, XLuaMathLibrary (0x6958d2)
    static const luaL_reg FNS[] = {{"SendMessage", lSend}, {"SendFloatMessage", lSend}, {"SendIntMessage", lSend}, {"SendStringMessage", lSend},
                                   {"GetData", lGet}, {"SetData", lSet}, {"StartTimer", lStartTimer}, {"CancelTimer", lCancelTimer},
                                   {"EditContainer", lEdit}, {"CloseContainer", lClose}, {"QueryContainer", lQuery}, {"CopyContainer", lCopy},
                                   {"echo", lLog}, {"log", lLog}};
    for (const luaL_reg &f : FNS) lua_register(L, f.name, f.func);
    if (!loadChunk(dir, "stdlib") || !loadChunk(dir, "lib_help") || !loadChunk(dir, script)) return g.script.reset(), false;
    ScriptHost::push("GM.SchemeData", nullptr);
    g.phase = Phase::Settle, g.timer = 0;  // no turn until the script's StartFirstTurn
    g.indestructible = false;
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
    ScriptHost::events();
    if (S->roundOn && !S->timedOut && nowMs() - S->roundStart >= (int64_t)num("RoundTime")) S->timedOut = true, call("Timer_GameTimedOut");  // 0x50f17d
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
        std::string fn = S->queue.front();
        S->queue.erase(S->queue.begin());
        call(fn.c_str());
    }
    if (S->endless && g.phase == Phase::Aim) g.timer = std::max(g.timer, 99 * 60);
    S->tagged.clear();
    for (const Object &o : g.objects) if (o.tag >= 0 && (o.type == Object::Crate || o.type == Object::Target)) S->tagged.push_back(o);
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
    b.append((const char *)&S->roundStart, sizeof S->roundStart);
    uint32_t h = 2166136261u;
    for (unsigned char c : b) h = (h ^ c) * 16777619u;
    S->dirty = false;
    return S->sum = h;
}

ScriptReport scriptReport(const Game &g) {
    ScriptReport r;
    if (!g.script) return r;
    r.ignored.assign(g.script->ignored.begin(), g.script->ignored.end());
    r.missingKeys.assign(g.script->missing.begin(), g.script->missing.end());
    r.errors = g.script->errors, r.lastError = g.script->lastError;
    return r;
}

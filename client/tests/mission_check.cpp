// Every W4M mission (assets/missions, a Lua script) starts, runs with the AI on every team to its end without a Lua error, and lists
// what its script asked that we do not model yet; a scripted mission replays bit-identically.
// Run from client/: make mission_check (W4NX_MISSION=<id> runs that one only, W4NX_MISSION=movies / crates those checks)
#include <map>
#include "../src/ai.h"
#include "../src/mission.h"
#include "../src/script.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

// Where the AI cannot reach a W4M goal (crates to collect, targets, triggers, enemies that never take a turn): the player's turn
// collects the crates and triggers it may one a tick, pops the targets, blasts one trigger it may destroy, then ends with the
// enemies at 0 hp (they die at the turn's ApplyDamage) and the worms that never play poisoned (hurt at the turn's ApplyPoison:
// the accuracy dummies)
static void assist(Game &g) {
    Worm &me = g.worms[g.current];
    if (g.phase != Phase::Aim || me.team != 0 || !me.alive || scriptMovieOn(g)) return;  // a movie leaves the player no control
    for (Object &o : g.objects) if (o.tag >= 0 && o.type == Object::Target) o.dead = true;
    for (const Object &o : g.objects)
        if (o.tag >= 0 && o.type == Object::Crate && (o.teamCollect < 0 || o.teamCollect == g.alliance(0))) { me.pos = o.pos, me.vel = {}; return; }
    for (const Game::Trigger &t : g.triggers)  // only the kind the script answers: a silent one would be lost
        if (scriptDefines(g, "Trigger_Collected") && (t.mask & 1) && !t.gone && t.teamCollect <= 0 && (t.wormCollect == -1 || t.wormCollect == g.current)) {
            me.pos = t.pos, me.vel = {};
            return;
        }
    for (Game::Trigger &t : g.triggers) if (scriptDefines(g, "Trigger_Destroyed") && !t.gone && t.teamDestroy <= 0) { t.gone = Game::TRIG_DESTROYED; break; }
    for (Worm &w : g.worms) if (w.team != 0 && w.alive) w.hp = 0;
    for (Worm &w : g.worms) if (!w.turns && w.alive) w.poison = 5;
    g.timer = 1;
}

// A W4M mission played by the AI on every team until its script ends it, or `cap` ticks; assisted after 3 minutes
struct Run { int ticks = 0, result = 0; uint32_t sum = 0; ScriptReport rep; int comments = 0, emitters = 0; ScriptEgg egg; };
static Run scripted(const MissionSpec &m, uint32_t seed, int cap) {
    Game g;
    g.start(missionConfig(m, seed));
    for (auto &t : g.cfg.teamSetup) t.cpu = std::max<uint8_t>(t.cpu, 3);
    Ai ai;
    Run r;
    for (; r.ticks < cap && (g.phase != Phase::GameOver || scriptMovieOn(g)); r.ticks++) {  // and its game-over movie
        if (r.ticks > 60 * 60 * 3) assist(g);
        g.step(ai.think(g));
        for (const GameEvent &e : g.events) r.comments += e.kind == GameEvent::Comment, r.emitters += e.kind == GameEvent::Emitter;
    }
    r.result = g.run.result, r.sum = g.checksum(), r.rep = scriptReport(g), r.egg = scriptEgg(g);
    return r;
}

// Lot 2 keys and messages on the real scripts: placed mines and Payload_Deleted, the mine factory, Water.Level, Jetpack.InitFuel,
// Challenge.EndlessGun, Weapon.PreSelected, the emptied default inventories, the CPU2 default
static void checkLot2(const std::vector<MissionSpec> &list) {
    auto run = [&](const char *id, Game &g, int ticks) {
        for (const MissionSpec &m : list) if (m.id == id) g.start(missionConfig(m, 3));
        Input skip;
        skip.flags = Input::SKIP_MOVIE;
        for (int t = 0; t < ticks; t++) g.step(skip);
        assert(g.script);
    };
    auto mines = [](const Game &g, bool placed) { int n = 0; for (const Object &o : g.objects) n += o.type == Object::Mine && (o.id >= 0) == placed; return n; };
    {
        Game g;
        run("MineAllMine", g, 2);
        assert(mines(g, true) == 4 && mines(g, false) == 0);  // PlaceMine Mine1/3/5/7; PlaceObjects skips the "MineN" details
        for (size_t i = 0; i < WEAPONS.size(); i++) if (WEAPONS[i].kind == Kind::Surrender) assert(g.ammo[0][i] == 0);  // LOCAL's -1 emptied
        for (Object &o : g.objects) if (o.id == 1) o.pos.y = g.water - 1;  // Mine1 sinks: Payload_Deleted places Mine2
        g.step(Input{}), g.step(Input{});
        assert(mines(g, true) == 4 && std::any_of(g.objects.begin(), g.objects.end(), [](const Object &o) { return o.id == 5; }));
    }
    {
        Game g;
        run("DeathMatch6", g, 2);
        assert(g.factory.on && g.factoryData.activation == 15);
        int before = (int)g.objects.size();
        for (int k = 0; k < 7; k++) g.factoryStart();
        assert(g.factory.state == 1 && g.active());
        for (int t = 0; t < 240; t++) g.step(Input{});
        printf("mine factory: %d mines dropped\n", (int)g.objects.size() - before);
        assert(g.factory.state == 0 && (int)g.objects.size() > before);
    }
    {
        Game g;
        run("DoomCanyon", g, 3);  // the skipped intro's EFMV_Terminated: Water.Level 20
        assert(fabsf(g.water - (g.terrain.origin.y + 20 * g.terrain.scale / 20)) < 1e-3f);
    }
    {
        Game g;
        run("FastFoodDino", g, 2);
        assert(g.jetInit == 25 && g.cfg.teamSetup.size() >= 1);
    }
    {
        Game g;
        run("ChallengeShotgun2", g, 3);  // TurnStarted: WeaponIndex kWeaponShotgun, Weapon.PreSelected
        assert(!g.endlessGun && WEAPONS[g.weapon].name == "Shotgun");
        Game h;
        run("ChallengeSniper", h, 2);
        assert(h.endlessGun);
    }
    {
        Game g;
        run("TurkishDelights", g, 1);  // PNTLGHT ... PL01 / PL04, created on: Initialise's EnablePointLight changes nothing
        Terrain &t = g.terrain;
        assert(t.lights.size() == 2 && t.lights[0].code == "PL01" && t.lights[0].on && t.lights[1].on);
        std::fill(t.dirty.begin(), t.dirty.end(), false);
        t.pointLight("PL01", false);
        assert(!t.lights[0].on && t.lights[1].on && std::count(t.dirty.begin(), t.dirty.end(), true) > 0);
    }
    {  // ChallengeShotgun2's shotgun reaches a Target crate (HighNoonHiJinx's "Target": 25 hit points) in the line of fire
        Game g;
        run("ChallengeShotgun2", g, 3);
        for (int t = 0; t < 60 * 60 && !(g.phase == Phase::Aim && g.worms[g.current].team == 0); t++) g.step(Input{});
        assert(g.phase == Phase::Aim && g.worms[g.current].team == 0);
        Worm &me = g.worms[g.current];
        Object o = {Object::Target, Vector3Add(me.pos, Vector3Scale(g.aimDir(me), 6)), {0, 0, 0}, -1, -1, false, false};
        o.tag = 99, o.hp = 25, o.pinned = true, o.teamCollect = 5;
        g.objects.push_back(o);
        for (size_t k = 0; k < WEAPONS.size(); k++) if (WEAPONS[k].name == "Shotgun") g.weapon = (int)k;
        Input fire;
        fire.buttons = Input::FIRE;
        g.hotSeat = 0;
        for (int k = 0; k < 240; k++) g.step(k % 20 < 2 ? fire : Input{});
        bool gone = std::none_of(g.objects.begin(), g.objects.end(), [](const Object &x) { return x.tag == 99; });
        printf("ChallengeShotgun2 target: %s by the shotgun\n", gone ? "destroyed" : "NOT destroyed");
        assert(gone);
    }
    for (const char *id : {"GibbonTake", "TraitorousWaters"}) {  // AI teams whose script copies no AIParams.CPUn: the AIService init's CPU2
        Game g;
        run(id, g, 1);
        for (const auto &t : g.cfg.teamSetup) assert(t.cpu == 0 || t.cpu == 2);
    }
}

// Level movies (docs/missions.md "Movies"): played in the sim at the W4M player's pace, its camera paths holding the end; skipped
// with Input::SKIP_MOVIE (Critical events only)
static void checkMovies(const std::vector<MissionSpec> &list) {
    auto play = [&](const char *id, int skipAt, int *startT, int *endT, Game &g) {
        for (const MissionSpec &m : list) if (m.id == id) g.start(missionConfig(m, 3));
        *startT = *endT = -1;
        for (int t = 0; t < 60 * 200 && *endT < 0; t++) {
            Input in;
            if (t == skipAt) in.flags = Input::SKIP_MOVIE;
            g.step(in);
            for (const GameEvent &e : g.events) {
                if (e.kind == GameEvent::MovieStart && *startT < 0) *startT = t;
                if (e.kind == GameEvent::MovieEnd) *endT = t;
            }
        }
    };
    int a, b;
    {  // TinCanWally Intro: its last camera, TargetWatch at 74380 ms, a look-at of 600 steps: 601 updates to the last knot, then 1
        Game g;
        play("TinCanWally", -1, &a, &b, g);
        int ms = (b - a) * 1000 / 60;
        printf("TinCanWally Intro: %d ms\n", ms);
        assert(a == 0 && abs(ms - 80390) <= 17 && !scriptMovieOn(g));
    }
    {  // skipped: over at once, its Critical DeleteBorders run, its comments and cameras not
        Game g;
        play("TinCanWally", 30, &a, &b, g);
        assert(b == 30 && !scriptMovie(g).on);
    }
    {  // DestructAndServe's EasterMovie: Land.ClearCoded JEFF empties the DeLorean's land frames
        Game g;
        for (const MissionSpec &m : list) if (m.id == "DestructAndServe") g.start(missionConfig(m, 3));
        assert(g.terrain.codes.count("JEFF"));
        Terrain::Coded jeff = g.terrain.codes["JEFF"];
        auto solid = [&] {
            int k = 0, n = 0;
            for (auto [at, len] : jeff.vox)
                for (int i = at; i < at + len; i++, n++)
                    k += g.terrain.solid({i % Terrain::NX * Terrain::VOX, i / Terrain::NX % Terrain::NY * Terrain::VOX, i / (Terrain::NX * Terrain::NY) * Terrain::VOX});
            return std::make_pair(k, n);
        };
        auto [was, n] = solid();
        assert(n > 0 && was > n * 9 / 10 && g.terrain.clearCoded("JEFF") && !g.terrain.codes.count("JEFF") && solid().first == 0);
        printf("DestructAndServe JEFF: %d of %d voxels solid, none once cleared\n", was, n);
    }
}

// Crates book into the W4M inventories as 0x5c8820: NumContents as a u8 on the alliance's count; -1 on a count of 0 gives an infinite
// weapon (SneakyBridgeThieves' bat)
static void checkCrates(const std::vector<MissionSpec> &list) {
    auto start = [&](const char *id, Game &g) {
        for (const MissionSpec &m : list) if (m.id == id) g.start(missionConfig(m, 3));
        Input skip;
        skip.flags = Input::SKIP_MOVIE;
        for (int t = 0; t < 60 * 120 && !(g.phase == Phase::Aim && g.worms[g.current].team == 0 && !scriptMovieOn(g)); t++) g.step(t % 30 ? Input{} : skip);
        assert(g.phase == Phase::Aim && g.worms[g.current].team == 0);
    };
    auto collect = [](Game &g, int tag) {  // the crate's weapon, its count 20 ticks after the pickup
        auto o = std::find_if(g.objects.begin(), g.objects.end(), [&](const Object &x) { return x.tag == tag && x.type == Object::Crate; });
        assert(o != g.objects.end() && o->weapon >= 0);
        int w = o->weapon, before = g.ammo[0][w];
        g.worms[g.current].pos = o->pos, g.worms[g.current].vel = {}, g.hotSeat = 0;
        for (int t = 0; t < 20; t++) g.step(Input{});
        printf("crate %d: %s %d -> %d\n", tag, WEAPONS[w].name.c_str(), before, g.ammo[0][w]);
        return std::make_pair(w, g.ammo[0][w]);
    };
    {
        Game g;
        start("SneakyBridgeThieves", g);
        auto [bat, n] = collect(g, 9);  // Crate_9: kWeaponBaseballBat, NumContents -1
        assert(WEAPONS[bat].name == "Baseball Bat" && n == -1);
        g.weapon = bat;
        Input fire;
        fire.buttons = Input::FIRE;
        g.step(fire), g.step(Input{});
        assert(g.phase != Phase::Aim && g.ammo[0][bat] == -1);
    }
    {
        Game g;
        start("CarpetCapers", g);
        auto [bazooka, n] = collect(g, 1);
        assert(n == 10 && collect(g, 3).second == 4 && collect(g, 9).second == 5);  // Bazooka x10, HomingMissile x4 then x1
        g.objects.push_back({Object::Crate, g.worms[g.current].pos, {0, 0, 0}, bazooka, 0, false, false});  // a random crate: 1 item
        g.objects.back().tag = 50;
        assert(collect(g, 50).second == 11);
    }
}

// Mission crates: every one that falls or snaps ends with its centre 10 x Scale units over the ground (0x5c94d0, GroundSnap 0x5c8137),
// within the 0.25 m voxel; a pinned one (Gravity 0) stays at its marker
static void checkCratePlacement(const std::vector<MissionSpec> &list) {
    const char *only = getenv("W4NX_CRATEPOS");  // one mission id; W4NX_SEED its seed
    for (const MissionSpec &m : list) {
        if (only && m.id != only) continue;
        Game g;
        g.start(missionConfig(m, getenv("W4NX_SEED") ? atoi(getenv("W4NX_SEED")) : 3));
        Input skip;
        skip.flags = Input::SKIP_MOVIE;
        int n = 0, pinned = 0, rest = 0, over = 0;
        float worst = 0;
        std::map<int, Vector3> at;  // a pinned crate's first position: the marker
        for (int t = 0; t < 60 * 90; t++) {
            g.step(t % 30 ? Input{} : skip);
            std::map<int, Vector3> now;  // a tag leaving (Crate.Delete) frees it for the script's next crate of that Index
            for (const Object &o : g.objects) if ((o.type == Object::Crate || o.type == Object::Target) && o.pinned && o.tag >= 0) now[o.tag] = o.pos;
            for (auto it = at.begin(); it != at.end();) it = now.count(it->first) ? ++it : at.erase(it);
            for (auto &[tag, pos] : now) {  // a re-created Index sits elsewhere; the same column must keep its height
                Vector3 &was = at.emplace(tag, pos).first->second;
                assert(was.x != pos.x || was.z != pos.z || was.y == pos.y);
                was = pos;
            }
        }
        for (size_t i = 0; i < g.objects.size(); i++) {
            const Object &o = g.objects[i];
            if (o.type != Object::Crate && o.type != Object::Target) continue;
            n++;
            if (o.pinned) { pinned++; continue; }
            Vector3 hit;
            if (o.vel.x != 0 || o.vel.y != 0 || o.vel.z != 0 || o.spawning || !g.terrain.raycast({o.pos, {0, -1, 0}}, 3, &hit)) { over++; continue; }
            float gap = o.pos.y - hit.y - 0.5f * o.scale;
            worst = fmaxf(worst, fabsf(gap)), rest++;
            if (fabsf(gap) > 0.3f) printf("    %s crate %d: centre %.2f m over the ground, W4M %.2f\n", m.id.c_str(), o.tag, o.pos.y - hit.y, 0.5f * o.scale);
        }
        if (n) printf("%-24s crates %2d: pinned %2d, rested %2d (worst %.2f m off 10 x Scale), moving or over water %2d\n", m.id.c_str(), n, pinned, rest, worst, over);
        assert(worst < 0.3f);
        if (m.id == "SneakyBridgeThieves") for (const Object &o : g.objects) if (o.tag == 5) assert(o.pos.y > 13.8f);  // Crate5 (marker y 13.72) rests on the bridge rail (top 13.49), not on the ground 2 m under
    }
}

// The player's last worm dies in its own turn (drowned, blown up, fallen off the map): the turn ends and the script's TurnEnded fails
// the mission (GetActiveAlliances 0); the enemies drowned in the player's turn reach Worm_Died (DeadWorm.Id) and win it
static void checkDeaths(const std::vector<MissionSpec> &list) {
    for (const char *id : {"BuildingSiteSaboteurs", "NoRoomForError", "TheCrateEscape", "MineAllMine"})
        for (int how = 0; how < 4; how++) {
            bool enemies = how == 3;
            if (enemies && strcmp(id, "NoRoomForError") && strcmp(id, "MineAllMine")) continue;  // 4 enemies dead at a turn's end wins
            Game g;
            for (const MissionSpec &m : list) if (m.id == id) g.start(missionConfig(m, 3));
            Input skip;
            skip.flags = Input::SKIP_MOVIE;
            for (int t = 0; t < 60 * 120 && !(g.phase == Phase::Aim && g.worms[g.current].team == 0 && !scriptMovieOn(g)); t++) g.step(t % 30 ? Input{} : skip);
            assert(g.phase == Phase::Aim && g.worms[g.current].team == 0);
            Worm &me = g.worms[g.current];
            for (Worm &w : g.worms) if (w.team == 0 && &w != &me) w.alive = false, w.hp = w.counted = 0;
            if (how == 0) me.pos.y = g.water - 2;
            else if (how == 1) me.hp = me.counted = 20, g.objects.push_back({Object::Mine, me.pos, {0, 0, 0}, -1, 0.05f, false, false});
            else if (how == 2) me.pos.x = -4, me.vel = {}, me.grounded = false;
            else {
                for (Worm &w : g.worms) if (w.team != 0 && w.alive) w.pos.y = g.water - 2;
                g.timer = 1;
            }
            int t = 0;
            for (; t < 60 * 60 && g.phase != Phase::GameOver; t++) g.step(Input{});
            static const char *const HOW[] = {"player drowned", "player blown up", "player off the map", "enemies drowned"};
            printf("%-22s %-18s: %s after %d s\n", id, HOW[how], g.phase != Phase::GameOver ? "NOT ENDED" : g.run.result < 0 ? "lost" : "won", t / 60);
            fflush(stdout);
            assert(g.phase == Phase::GameOver && (g.run.result > 0) == enemies);
        }
}

int main() {
    assert(loadWeapons("romfs/weapons.json"));
    std::vector<MissionSpec> list = listMissions("./romfs/", "./");
    Progress p;
    p.record("a", false, 0), p.record("a", true, 900), p.record("a", true, 1200);
    p.unlocks = {"Lock.EasterEgg.3"};
    p.save("progress_check.txt");
    Progress q;
    q.load("progress_check.txt");
    remove("progress_check.txt");
    assert(q.get("a").done && q.get("a").best == 900 && !q.get("b").done && q.unlocks == p.unlocks);
    if (list.empty()) return puts("mission_check: no W4M mission imported (assets/missions), nothing to run"), 0;
    const char *only = getenv("W4NX_MISSION");
    int imported = 0, ended = 0;
    for (const MissionSpec &m : list) {
        if (only && m.id != only) continue;
        imported++;
        assert((m.kind == "mission") == !m.objectives.empty());  // the pause menu's Briefing: Story levels only carry Objectives
        Run r = scripted(m, 5, 60 * 60 * 90);  // ChuteToVictory: its round clock (50 min) stands through ~8 s of movie camera a turn
        printf("%-24s %-9s %s after %5d s, Lua errors %d%s%s, comments %d, emitters %d\n", m.id.c_str(), m.kind.c_str(), r.result > 0 ? "won " : r.result < 0 ? "lost" : "NOT ENDED",
               r.ticks / 60, r.rep.errors, r.rep.errors ? ": " : "", r.rep.lastError.c_str(), r.comments, r.emitters);
        if (m.id == "SneakyBridgeThieves") assert(r.emitters >= 8);  // Initialise's emitters reach the events
        if (m.id == "EscapeFromTreeRex") assert(r.comments >= 1);    // its skipped intro's Critical Comment
        if (!r.egg.item.empty()) printf("    easter egg: %s (%s, %d coins)\n", r.egg.item.c_str(), r.egg.name.c_str(), r.egg.coins);
        assert(r.egg.item.empty() || (!r.egg.item.compare(0, 15, "Lock.EasterEgg.") && r.egg.coins == 1000));  // DEFSAVE Value
        for (auto &k : r.rep.ignored) printf("    not modelled: %s x%d\n", k.first.c_str(), k.second);
        for (auto &k : r.rep.missingKeys) printf("    missing data: %s x%d\n", k.first.c_str(), k.second);
        fflush(stdout);
        assert(r.rep.errors == 0 && r.rep.missingKeys.empty());
        ended += r.result != 0;
    }
    if (only && !strcmp(only, "movies")) return checkMovies(list), 0;
    if (only && !strcmp(only, "crates")) return checkCrates(list), 0;
    if (only && !strcmp(only, "cratepos")) return checkCratePlacement(list), 0;
    if (only && !strcmp(only, "deaths")) return checkDeaths(list), 0;
    if (only) return 0;
    checkLot2(list);
    checkCrates(list);
    checkCratePlacement(list);
    checkDeaths(list);
    checkMovies(list);
    for (const MissionSpec &x : list)  // a scripted mission plays the same twice (its Lua state is in the checksum)
        if (x.id == "DeathMatch1") {
            Run a = scripted(x, 11, 60 * 60 * 3), b = scripted(x, 11, 60 * 60 * 3);
            assert(a.sum == b.sum && a.ticks == b.ticks);
        }
    printf("W4M missions: %d of %d ended\n", ended, imported);
    assert(ended == imported);

    for (size_t i = 1; i < list.size(); i++)
        if (list[i].kind == "mission" && list[i - 1].kind == "mission" && list[i].campaign == list[i - 1].campaign) { assert(!Progress{}.unlocked(list, i)); break; }
    printf("mission_check ok: %d missions\n", imported);
}

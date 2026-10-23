// Every W4M mission (assets/missions, a Lua script) starts, runs with the AI on every team to its end without a Lua error, and lists
// what its script asked that we do not model yet; a scripted mission replays bit-identically.
// Run from client/: make mission_check (W4NX_MISSION=<id> runs that one only, W4NX_MISSION=movies / crates / attach those checks)
#define private public  // the hurt test calls hurt() directly
#include <map>
#include "../src/ai.h"
#include "../src/controls.h"
#include "../src/mission.h"
#include "../src/script.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

// Where the AI cannot reach a W4M goal (crates to collect, targets, triggers, enemies that never take a turn): the player's turn
// collects the crates and triggers it may one a tick, lures an enemy-team trigger's worm in, pops a target or blasts a trigger (one a
// turn when the script answers Trigger_Destroyed), then ends with the enemies not allied to the player at 0 hp (they die at the turn's
// ApplyDamage) and the never-playing worms poisoned (the accuracy dummies, on the player's team); `survive` spares the enemies; trigIndex, if >= 0, is the
// only trigger index blasted
static void assist(Game &g, bool survive, int trigIndex, bool &blasted) {
    Worm &me = g.worms[g.current];
    if (g.phase != Phase::Aim) blasted = false;
    if (g.phase != Phase::Aim || me.team != 0 || !me.alive || scriptMovieOn(g)) return;  // a movie leaves the player no control
    for (Object &o : g.objects) {  // a target's blast may destroy a trigger, which starts a movie: one of those a turn
        if (o.tag < 0 || o.type != Object::Target || o.dead) continue;
        bool hits = std::any_of(g.triggers.begin(), g.triggers.end(), [&](const Game::Trigger &t) { return !t.gone && Vector3Distance(t.pos, o.pos) < 4; });
        if (hits && blasted) continue;
        o.dead = true, blasted |= hits;
    }
    for (const Object &o : g.objects)
        if (o.tag >= 0 && o.type == Object::Crate && (o.teamCollect < 0 || o.teamCollect == g.alliance(0))) { me.pos = o.pos, me.vel = {}; return; }
    for (const Game::Trigger &t : g.triggers)  // only the kind the script answers: a silent one would be lost
        if (scriptDefines(g, "Trigger_Collected") && (t.mask & 1) && !t.gone && t.teamCollect <= 0 && (t.wormCollect == -1 || t.wormCollect == g.current)) {
            me.pos = {t.pos.x, t.pos.y + t.radius / 2, t.pos.z}, me.vel = {};  // inside its sphere, above water (ChuteToVictory's last one sits just under it)
            return;
        }
    for (const Game::Trigger &t : g.triggers)  // a trigger only an enemy team collects (TurkishDelights' lights): lure one of its worms in
        if (scriptDefines(g, "Trigger_Collected") && (t.mask & 1) && !t.gone && t.teamCollect > 0 && g.alliance(t.teamCollect) != g.alliance(0))
            for (Worm &w : g.worms) if (w.team == t.teamCollect && w.alive && w.turns) { w.pos = t.pos, w.vel = {}; return; }
    if (!blasted)  // one blast a turn, as a player: the movies one trigger starts must end before the next
        for (Game::Trigger &t : g.triggers)
            if (scriptDefines(g, "Trigger_Destroyed") && !t.gone && t.teamDestroy <= 0 && (trigIndex < 0 || t.index == trigIndex)) { t.gone = Game::TRIG_DESTROYED, blasted = true; break; }
    if (survive) return g.timer = 1, void();
    bool foes = std::any_of(g.worms.begin(), g.worms.end(), [&](const Worm &w) { return w.turns && w.alive && g.alliance(w.team) != g.alliance(0); });
    for (Worm &w : g.worms)  // allies and, while enemies play, the never-playing ones (CarpetCapers' Prof, TheLand's classmate) are spared
        if (w.alive && g.alliance(w.team) != g.alliance(0) && (w.turns || !foes)) w.hp = 0;
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
    // The AI player cannot play these: NiceToSiegeYou is survived (its enemies' death blasts and respawns dig the spawn markers under water),
    // DoomCanyon's rising water drowns a worm that does not climb, TraitorousWaters wants the gun about to fire destroyed (its Index 4 + g_nGunFiring)
    bool blasted = false, survive = m.id == "NiceToSiegeYou" || m.id == "TraitorousWaters";
    int from = m.id == "DoomCanyon" || m.id == "TraitorousWaters" ? 0 : 60 * 60 * 3;
    for (; r.ticks < cap && (g.phase != Phase::GameOver || scriptMovieOn(g)); r.ticks++) {  // and its game-over movie
        if (r.ticks > from) assist(g, survive, m.id == "TraitorousWaters" ? 4 + (int)scriptEval(g, "return g_nGunFiring") : -1, blasted);
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
        for (Object &o : g.objects) if (o.id == 1) o.pos = {-400, g.water + 0.1f, -400}, o.vel.y = -30;  // Mine1 falls through Level - SinkDepth, sinks to ExpiryDepth -200 u (0x582050): Payload_Deleted places Mine2
        for (int t = 0; t < 300; t++) g.step(Input{});
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
        assert(fabsf(g.water - (g.terrain.origin.y + 20.0f / 20)) < 1e-3f);
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
                for (int i = at; i < at + len; i++, n++) {
                    int x, y, z;
                    Terrain::xyz(i, x, y, z);
                    k += g.terrain.solid({x * Terrain::VOX, y * Terrain::VOX, z * Terrain::VOX});
                }
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
        for (int t = 0; t < 120 && !g.ambulatory(g.worms[g.current]); t++) g.step(Input{});  // the crate may still be falling: so is the worm put there
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

// W4M Worm_Damaged_Current (stdlib): the active worm hurt in its turn, a CPU's too, runs EndTurn at once
static void checkHurtEndsTurn(const std::vector<MissionSpec> &list) {
    Game g;
    for (const MissionSpec &m : list) if (m.id == "DeathMatch6") g.start(missionConfig(m, 1));
    assert(g.script);
    for (auto &t : g.cfg.teamSetup) t.cpu = std::max<uint8_t>(t.cpu, 3);
    Ai ai;
    Input skip;
    skip.flags = Input::SKIP_MOVIE;
    int cpuTurns = 0;
    for (int t = 0, since = 0; t < 60 * 60 * 5 && cpuTurns < 3; t++) {
        bool play = g.phase == Phase::Aim && !scriptMovieOn(g) && g.worms[g.current].alive;
        since = play ? since + 1 : 0;
        if (play && since > 60 * (cpuTurns + 1) && g.worms[g.current].team != 0) {
            g.hurt(g.worms[g.current], 5, true, 0);
            g.step(ai.think(g));
            assert(g.phase == Phase::Settle);
            cpuTurns++, since = 0;
        } else g.step(play ? ai.think(g) : skip);
    }
    assert(cpuTurns == 3);
}

// A crate a script makes with Crate.AddToWormInventory books to the collector's own Inventory.WormNN (0x5c8820), not the alliance's;
// NumContents -1 on a count of 0 gives it an infinite weapon (-1)
static void checkWormInventoryCrate(const std::vector<MissionSpec> &list) {
    Game g;
    for (const MissionSpec &m : list) if (m.id == "CarpetCapers") g.start(missionConfig(m, 3));
    Input skip;
    skip.flags = Input::SKIP_MOVIE;
    for (int t = 0; t < 60 * 120 && !(g.phase == Phase::Aim && g.worms[g.current].team == 0 && !scriptMovieOn(g)); t++) g.step(t % 30 ? Input{} : skip);
    assert(g.phase == Phase::Aim && g.worms[g.current].team == 0);
    auto inv = [&](const char *who, const char *w) {
        char b[160];
        snprintf(b, sizeof b, "return QueryContainer(\"Inventory.%s\").%s", who, w);
        return (int)scriptEval(g, b);
    };
    int slot = (int)scriptEval(g, "return GetData(\"ActiveWormIndex\")");
    char me[16];
    snprintf(me, sizeof me, "Worm%02d", slot);
    // the crate as a script makes it: the Crate.* keys, then GameLogic.CreateCrate (GameLogicService resets AddToWormInventory per crate)
    auto spawn = [&](int tag, const char *what, int n, int toWorm) {
        char b[400];
        snprintf(b, sizeof b, "SetData(\"Crate.Type\",\"weapon\") SetData(\"Crate.Contents\",\"%s\") SetData(\"Crate.NumContents\",%d) SetData(\"Crate.Index\",%d) "
                 "SetData(\"Crate.Spawn\",\"Crate1\") SetData(\"Crate.Gravity\",0) SetData(\"Crate.AddToWormInventory\",%d) SendMessage(\"GameLogic.CreateCrate\") return 0", what, n, tag, toWorm);
        g.worms[g.current].pos.y += 8, g.worms[g.current].vel = {};  // clear of the marker: the script sees the crate (an Index) a tick before it is collected
        scriptEval(g, b);
        auto o = std::find_if(g.objects.begin(), g.objects.end(), [&](const Object &x) { return x.tag == tag && x.type == Object::Crate; });
        assert(o != g.objects.end());
        g.step(Input{});
        o = std::find_if(g.objects.begin(), g.objects.end(), [&](const Object &x) { return x.tag == tag && x.type == Object::Crate; });
        g.worms[g.current].pos = o->pos, g.worms[g.current].vel = {}, g.hotSeat = 0;
        for (int t = 0; t < 20; t++) g.step(Input{});
    };
    assert(inv(me, "Dynamite") == 0);
    int alliance = inv("Alliance00", "Dynamite");
    spawn(70, "kWeaponDynamite", 3, 1);
    assert(inv(me, "Dynamite") == 3 && inv("Alliance00", "Dynamite") == alliance && scriptEval(g, "return GetData(\"Crate.AddToWormInventory\")") == 0);
    spawn(71, "kWeaponDynamite", 2, 0);  // the key was reset: this one goes to the alliance
    assert(inv(me, "Dynamite") == 3 && inv("Alliance00", "Dynamite") == alliance + 2);
    assert(inv(me, "Girder") == 0);
    spawn(72, "kUtilityGirder", -1, 1);
    printf("worm-inventory crate: %s Dynamite %d, Girder %d (alliance %d, team %d)\n", me, inv(me, "Dynamite"), inv(me, "Girder"), inv("Alliance00", "Girder"), inv("Team00", "Girder"));
    assert(inv(me, "Girder") == -1 && [&] { for (size_t w = 0; w < WEAPONS.size(); w++) if (WEAPONS[w].name == "Girder") return g.ammo[0][w] == -1; return false; }());
    assert(scriptReport(g).errors == 0);
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
            if (t == 600 && m.id == "BuildingSiteSaboteurs") for (auto &tr : g.triggers) tr.gone = Game::TRIG_DESTROYED;  // the 3 diggers down: Midtro spawns Crate1, 2, 5-8
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
            bool stacked = false;  // rests on another crate's collider (0x5cb7e0), e.g. NoRoomForError's script respawns Crate1 on its uncollected twin
            for (const Object &p : g.objects) stacked |= &p != &o && (p.type == Object::Crate || p.type == Object::Target) && p.pos.y < o.pos.y && Vector3Distance(p.pos, o.pos) <= 0.5f * (o.scale + p.scale);
            Vector3 hit;
            if (stacked) { over++; continue; }
            if (o.vel.x != 0 || o.vel.y != 0 || o.vel.z != 0 || o.spawning || !g.terrain.raycast({o.pos, {0, -1, 0}}, 3, &hit)) { over++; continue; }
            float gap = o.pos.y - hit.y - 0.5f * o.scale;
            worst = fmaxf(worst, fabsf(gap)), rest++;
            if (fabsf(gap) > 0.3f) printf("    %s crate %d: centre %.2f m over the ground, W4M %.2f\n", m.id.c_str(), o.tag, o.pos.y - hit.y, 0.5f * o.scale);
        }
        if (n) printf("%-24s crates %2d: pinned %2d, rested %2d (worst %.2f m off 10 x Scale), moving, stacked or over water %2d\n", m.id.c_str(), n, pinned, rest, worst, over);
        assert(worst < 0.3f);
        if (m.id == "BuildingSiteSaboteurs") {  // the six crates side by side on the heightmap ledge (.hmp vertices at 150 / 99 m): one height
            float lo = 99, hi = -99;
            int found = 0;
            for (const Object &o : g.objects) if (o.type == Object::Crate && (o.tag == 1 || o.tag == 2 || o.tag == 5 || o.tag == 6 || o.tag == 7 || o.tag == 8)) lo = fminf(lo, o.pos.y), hi = fmaxf(hi, o.pos.y), found++;
            assert(found == 6 && hi - lo < 0.1f);
        }
        if (m.id == "SneakyBridgeThieves") for (const Object &o : g.objects) if (o.tag == 5) assert(o.pos.y > 23.6f);  // Crate5 (marker y 23.68) rests on the bridge rail, not on the ground under it
    }
}

// PlaceWormAtSpawnPoint 0x5b4180: a worm named by a locator stands at its exact x, z (no random search) and, after the first Update
// (EstablishPhysicsState 0x5a6af0), on the ground under it: no fall, no fall damage; ground over 1000 units down: it falls
static void checkWormSpawn(const std::vector<MissionSpec> &list) {
    int placed = 0, settled = 0, dup = 0;
    for (const MissionSpec &m : list) {
        Game g;
        g.start(missionConfig(m, 3));
        Input skip;
        skip.flags = Input::SKIP_MOVIE;
        for (int t = 0; t < 2; t++) g.step(skip);
        std::vector<Worm> was = g.worms;
        for (int t = 0; t < 120; t++) g.step(skip);
        for (size_t i = 0; i < was.size() && i < g.worms.size(); i++) {
            const Worm &w = was[i];
            if (!w.alive) continue;
            const Terrain::Marker *k = nullptr;
            for (const Terrain::Marker &x : g.terrain.markers) if (fabsf(x.pos.x - w.pos.x) < 1e-3f && fabsf(x.pos.z - w.pos.z) < 1e-3f) k = &x;
            if (!k) continue;  // a worm of the random "spawn" search or one moved by the script
            placed++;
            bool facing = false;  // locators can share a spot (Player, STANDIN Worm0): the worm took one of them
            for (const Terrain::Marker &x : g.terrain.markers) {
                if (fabsf(x.pos.x - w.pos.x) > 1e-3f || fabsf(x.pos.z - w.pos.z) > 1e-3f) continue;
                // 0x5cc7c0: Orientation is written by every detail of the name, so the worm faces the name's last one
                const Terrain::Marker *o = &x;
                int same = 0;
                for (const Terrain::Marker &y : g.terrain.markers) if (!strcasecmp(y.name.c_str(), x.name.c_str())) o = &y, same++;
                if (same > 1 && m.id == "DeathMatch8" && !strcasecmp(x.name.c_str(), "Enemy1")) assert(o == &x);  // slot 4 of 2 details: Position of the (4+1)th match = the last
                if (same > 1) dup++, printf("  duplicate locator: %s worm %zu on '%s' (%d details)\n", m.id.c_str(), i, x.name.c_str(), same);
                // 0x69c118 identity, then 0x69c1a9 / 0x69c209 / 0x69c269 (each the transposed rotation) fill the matrix; 0x4453c0 + 0x519120 turn its row 2 into the yaw
                float M[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                for (int ax = 0; ax < 3; ax++) {
                    const float a = (&o->euler.x)[ax], c = cosf(a), sn = sinf(a);
                    const int i = ax == 0 ? 1 : 0, j = ax == 2 ? 1 : 2;
                    for (int r = 0; r < 3; r++) {
                        const float p = M[r][i], q = M[r][j];
                        if (ax == 1) M[r][i] = p * c + q * sn, M[r][j] = q * c - p * sn;
                        else M[r][i] = p * c - q * sn, M[r][j] = q * c + p * sn;
                    }
                }
                const float len = hypotf(M[2][0], M[2][2]), yaw = M[2][0] >= 0 ? acosf(M[2][2] / len) : -acosf(M[2][2] / len);
                facing |= fabsf(remainderf(w.yaw - yaw, 2 * PI)) < 1e-4f;
            }
            assert(facing);
            const float feet = w.pos.y - Game::R;
            assert(feet <= k->pos.y + 5 * 0.05f);  // settled onto the ground under the marker; a marker in the land lifts it by a walking step (<= 5 units)
            if (k->pos.y - feet < 1000 * 0.05f) settled++;
            if (g.worms[i].hp == w.hp && g.worms[i].alive) assert(fabsf(g.worms[i].pos.y - w.pos.y) < 0.3f);  // it stays: no fall
        }
    }
    printf("worm spawn: %d worms at their marker's x, z, %d settled on the ground under it, %d on a name shared by several details\n", placed, settled, dup);
    assert(placed > 20 && settled == placed);
}

// NoRoomForError creates Crate1 at its marker on top of the uncollected twin that already fell from it (0.85 m lower, spheres
// overlapping): 0x5cb7e0 finds the overlap (0x516350), v = 0 <= 0.02 and the twin below, so it rests where it was made, never pushed out
static void checkCrateStack(const std::vector<MissionSpec> &list) {
    for (const MissionSpec &m : list) {
        if (m.id != "NoRoomForError") continue;
        Game g;
        g.start(missionConfig(m, 3));
        Input skip;
        skip.flags = Input::SKIP_MOVIE;
        std::map<std::pair<int, int>, float> first;  // an overlapping resting pair's separation when first seen
        float closest = 99;
        for (int t = 0; t < 60 * 90; t++) {
            g.step(t % 30 ? Input{} : skip);
            for (size_t i = 0; i < g.objects.size(); i++)
                for (size_t j = i + 1; j < g.objects.size(); j++) {
                    const Object &a = g.objects[i], &b = g.objects[j];
                    if (a.type != Object::Crate || b.type != Object::Crate || a.pinned || b.pinned) continue;
                    if (Vector3LengthSqr(a.vel) != 0 || Vector3LengthSqr(b.vel) != 0 || a.spawning || b.spawning) continue;
                    const float d = Vector3Distance(a.pos, b.pos);
                    if (d >= 1.0f) continue;
                    closest = fminf(closest, d);
                    assert(fabsf(d - first.emplace(std::make_pair(a.uid, b.uid), d).first->second) < 1e-4f);
                }
        }
        printf("%-24s overlapping resting crates %zu, closest centres %.3f m (sum of radii 1.00)\n", m.id.c_str(), first.size(), closest);
        assert(!first.empty() && closest > 0.8f);
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

// TinCanWally's CreateTarget writes AIParams.Worm01 TargetDetailObject = AITargN at each CPU turn: the AI worm's scripted target is that marker
static void checkScriptedTarget(const std::vector<MissionSpec> &list) {
    Game g;
    for (const MissionSpec &m : list) if (m.id == "TinCanWally") g.start(missionConfig(m, 3));
    assert(!g.worms.empty());
    Input skip;
    skip.flags = Input::SKIP_MOVIE;
    for (int t = 0; t < 60 * 600 && !(g.phase == Phase::Aim && g.worms[g.current].team == 1 && !scriptMovieOn(g)); t++) {
        g.step(t % 30 ? Input{} : skip);
        if (g.phase == Phase::Aim && g.worms[g.current].team == 0 && !scriptMovieOn(g)) g.timer = 1;  // the player's turn ends
    }
    assert(g.phase == Phase::Aim && g.worms[g.current].team == 1);
    Vector3 at{};
    for (const Terrain::Marker &k : g.terrain.markers) if (k.name == "AITarg1") at = k.pos;
    printf("scripted target: AI worm %d, %zu marker(s)\n", g.current, g.wormTarget[g.current].size());
    assert(g.wormTarget[g.current].size() == 1 && Vector3Equals(g.wormTarget[g.current][0], at) && !Vector3Equals(at, Vector3{}));
}

// A human team that gets no input: no AI drives it, and Controls::read / tick give a zero Input, so nothing walks, turns or slides
static void idleOn(Game &g) {
    assert(!g.cfg.teamSetup[0].cpu);
    Input skip;
    skip.flags = Input::SKIP_MOVIE;
    for (int t = 0; t < 60 * 60 && (g.phase != Phase::Aim || scriptMovieOn(g)); t++) g.step(skip);
    assert(g.phase == Phase::Aim && g.worms[g.current].team == 0);
    for (int t = 0; t < 120; t++) g.step(Input{});  // settled
    Worm was = g.worms[g.current];
    for (int t = 0; t < 60 * 20; t++) {
        Input in = Controls::tick(Controls::read(g, 0, true, Game::DT));
        assert(!in.turn && !in.walk && !in.aim && !in.buttons && !in.flags);
        g.step(in);
    }
    const Worm &w = g.worms[g.current];
    assert(w.team == 0);
    assert(Vector3Distance(w.pos, was.pos) < 0.01f && w.yaw == was.yaw);
}

// Games chained in one process (mission, challenge, restart, local): the static Controls and script state carries nothing over
static void checkIdle(const std::vector<MissionSpec> &list) {
    for (const char *id : {"ChallengeNavigation2", "DeathMatch1", "SneakyBridgeThieves", "ChallengeNavigation2"})
        for (const MissionSpec &m : list) {
            if (m.id != id) continue;
            Game g;
            g.start(missionConfig(m, 5));
            idleOn(g);
        }
    Game g;
    g.start({1234, 2, 2, "", 0u, {{"Red Rockets"}, {"Blue Bombers"}}});
    idleOn(g);
}

// W4M WXWormGraphicEntity 0x5a49a0: a worm wears its Worm.DataNN ATT_*, else its Team.DataNN's
static void checkAttachments(const std::vector<MissionSpec> &list) {
    int worn = 0, teamWorn = 0;
    for (const MissionSpec &m : list) {
        if (m.id != "GibbonTake" && m.id != "DeathMatch4") continue;  // a worm-level and a team-level user
        Game g;
        g.start(missionConfig(m, 1));
        Input skip;
        skip.flags = Input::SKIP_MOVIE;
        for (int t = 0; t < 600; t++) g.step(skip);
        int w = (int)std::count_if(g.wormAtt.begin(), g.wormAtt.end(), [](const auto &a) { return !a[0].empty(); });
        int t = (int)std::count_if(g.cfg.teamSetup.begin(), g.cfg.teamSetup.end(), [](const auto &x) { return !x.hat.empty(); });
        worn += w, teamWorn += t;
    }
    assert(worn > 0 && teamWorn > 0);
}

// lib_SetupTeam / lib_SetupWorm copy the picked team (GM.GameInitData T1_*) into the human team and worms
static void checkPickedTeam(const std::vector<MissionSpec> &list) {
    GameConfig::Team me;
    me.name = "Zorglub", me.flag = "Flag.Spaceman", me.speech = "voalien", me.grave = 3, me.hat = "WXFE.A.Hat.Cowboy.Bk", me.gloves = "WXFE.A.Gloves.Ringed";
    me.glasses = "WXFE.A.Glasses.Monocle", me.tash = "WXFE.A.Mustache.Professor", me.wormNames = {"Aa", "Bb", "Cc", "Dd", "Ee", "Ff"};
    for (const MissionSpec &m : list) {
        if (m.id != "BuildingSiteSaboteurs") continue;
        Game g;
        g.start(missionConfig(m, 1, &me));
        const GameConfig::Team &t = g.cfg.teamSetup[0];
        assert(t.name == me.name && t.flag == me.flag && t.speech == me.speech && t.grave == me.grave);
        assert(t.hat == me.hat && t.gloves == me.gloves && t.glasses == me.glasses && t.tash == me.tash);
        assert(!t.wormNames.empty() && t.wormNames[0] == "Aa");
        assert(g.cfg.teamSetup[1].name != me.name && g.cfg.teamSetup[1].hat != me.hat);  // the CPU team keeps its own
    }
}

int main() {
    assert(loadWeapons("romfs/weapons.json"));
    std::vector<MissionSpec> list = listMissions("./romfs/", "./");
    Progress p;
    p.open("progress_check.txt", "progress_check_team.txt");
    p.record("a", false, 0), p.record("a", true, 900), p.record("a", true, 1200);
    p.unlocks = {"Lock.EasterEgg.3"}, p.storyIntro = true;
    p.save();
    Progress q;
    q.open("progress_check.txt", "progress_check_team.txt");
    remove("progress_check.txt"), remove("progress_check_team.txt");
    assert(q.get("a").done && q.get("a").best == 900 && !q.get("b").done && q.unlocks == p.unlocks && q.storyIntro);
    if (list.empty()) return puts("mission_check: no W4M mission imported (assets/missions), nothing to run"), 0;
    for (const char *stem : {"DinerMight", "DoomCanyon", "HighNoonHiJinx"}) {  // a stale import (older .vox format) leaves the voxels empty under the cells
        Terrain t;
        assert(t.load(stem, 1) && !t.mats.empty());
    }
    {  // an old-format .vox under a valid .cells is an invalid map: the procedural island, and a blast stays inside it
        char *js = LoadFileText("assets/maps/Accuracy.json");
        if (js && FileExists("assets/maps/Accuracy.cells")) {
            std::string txt = js, from = "Accuracy.vox";
            txt.replace(txt.find(from), from.size(), "zz_stale.vox");
            SaveFileText("assets/maps/zz_stale.json", txt.data());
            SaveFileData("assets/maps/zz_stale.vox", (void *)"W4V3 stale", 10);
            Terrain t;
            assert(!t.load("zz_stale", 1) && !t.sharp.on && t.theme.empty() && t.codes.empty() && t.landUV.empty());
            assert(t.carve({Terrain::NX * Terrain::VOX / 2, 8, Terrain::NZ * Terrain::VOX / 2}, 3));
            remove("assets/maps/zz_stale.json"), remove("assets/maps/zz_stale.vox");
        }
        UnloadFileText(js);
    }
    const char *only = getenv("W4NX_MISSION");
    int imported = 0, ended = 0;
    for (const MissionSpec &m : list) {
        if (only && m.id != only) continue;
        imported++;
        assert((m.kind == "mission") == !m.objectives.empty());  // the pause menu's Briefing: Story levels only carry Objectives
        Run r = scripted(m, 5, 60 * 60 * 90);  // ChuteToVictory: its round clock (50 min) stands through ~8 s of movie camera a turn
        printf("%-24s %-9s %s after %5d s, Lua errors %d%s%s, comments %d, emitters %d\n", m.id.c_str(), m.kind.c_str(), r.result > 0 ? "won " : r.result < 0 ? "lost" : "NOT ENDED",
               r.ticks / 60, r.rep.errors, r.rep.errors ? ": " : "", r.rep.lastError.c_str(), r.comments, r.emitters);
        if (m.id == "JoustAboutIt") assert(r.result > 0);  // the Wizard's DieQuietly reaches Worm_Died: 4 deaths start wave 2, 10 the hourglass
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
    if (only && !strcmp(only, "hurt")) return checkHurtEndsTurn(list), 0;
    if (only && !strcmp(only, "attach")) return checkAttachments(list), 0;
    if (only && !strcmp(only, "picked")) return checkPickedTeam(list), 0;
    if (only && !strcmp(only, "wormcrate")) return checkWormInventoryCrate(list), 0;
    if (only && !strcmp(only, "cratepos")) return checkCratePlacement(list), 0;
    if (only && !strcmp(only, "idle")) return checkIdle(list), 0;
    if (only && !strcmp(only, "target")) return checkScriptedTarget(list), 0;
    if (only && !strcmp(only, "deaths")) return checkDeaths(list), 0;
    if (only && !strcmp(only, "cratestack")) return checkCrateStack(list), 0;
    if (only && !strcmp(only, "wormspawn")) return checkWormSpawn(list), 0;
    if (only) return 0;
    checkLot2(list);
    checkCrates(list);
    checkWormInventoryCrate(list);
    checkHurtEndsTurn(list);
    checkCratePlacement(list);
    checkCrateStack(list);
    checkWormSpawn(list);
    checkDeaths(list);
    checkPickedTeam(list);
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

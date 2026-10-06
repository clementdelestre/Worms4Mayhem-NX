// Every JSON mission (romfs/missions) loads, places its worms and objects, wins when its objectives are forced and loses when the
// player team is wiped out; one AI-vs-AI mission replays bit-identically. Every W4M mission (assets/missions, a Lua script) starts,
// runs with the AI on every team to its end without a Lua error, and lists what its script asked that we do not model yet.
// Run from client/: make mission_check (W4NX_MISSION=<id> runs that one only)
#include "../src/ai.h"
#include "../src/mission.h"
#include "../src/script.h"
#include "raymath.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

using G = MissionSpec::Goal;

// Forces every objective once per tick (kills, moves the player, pops targets) until the mission ends.
static int forceWin(const MissionSpec &m) {
    Game g;
    g.start(missionConfig(m, 42));
    for (int t = 0; t < 10 * 3600 && g.phase != Phase::GameOver; t++) {
        Worm &me = g.worms[0];
        for (const G &o : m.objectives) {
            if (o.type == G::KillAll) for (Worm &w : g.worms) if (w.team) w.alive = false;
            if (o.type == G::Kill) g.worms[o.team * g.perTeam + o.worm].alive = false;
            if (o.type == G::PoisonAll) for (Worm &w : g.worms) if (w.team) w.poison = 5;
            if (o.type == G::Reach) me.pos = placeOf(g, o.at), me.vel = {};
            if (o.type == G::Survive && o.seconds) g.run.ticks = std::max(g.run.ticks, o.seconds * 60);
        }
        for (Object &o : g.objects) {
            if (o.tag >= 0 && o.type == Object::Target) o.dead = true;
            if (o.tag >= 0 && o.type == Object::Crate) { me.pos = o.pos, me.vel = {}; break; }
        }
        g.step(Input{});
    }
    return g.run.result;
}

static int forceLose(const MissionSpec &m) {
    Game g;
    g.start(missionConfig(m, 7));
    for (Worm &w : g.worms) if (w.team == 0) w.alive = false;
    g.step(Input{});
    return g.run.result;
}

// Where the AI cannot reach a W4M goal (crates to collect, targets, enemies that never take a turn): the player's turn collects
// the crates one a tick, pops the targets, then ends with the enemies at 0 hp (they die at the turn's ApplyDamage) and the worms
// that never play poisoned (hurt at the turn's ApplyPoison: the accuracy dummies)
static void assist(Game &g) {
    Worm &me = g.worms[g.current];
    if (g.phase != Phase::Aim || me.team != 0 || !me.alive) return;
    for (Object &o : g.objects) if (o.tag >= 0 && o.type == Object::Target) o.dead = true;
    for (const Object &o : g.objects) if (o.tag >= 0 && o.type == Object::Crate) { me.pos = o.pos, me.vel = {}; return; }
    for (Worm &w : g.worms) if (w.team != 0 && w.alive) w.hp = 0;
    for (Worm &w : g.worms) if (!w.turns && w.alive) w.poison = 5;
    g.timer = 1;
}

// A W4M mission played by the AI on every team until its script ends it, or `cap` ticks; assisted after 3 minutes
struct Run { int ticks = 0, result = 0; uint32_t sum = 0; ScriptReport rep; };
static Run scripted(const MissionSpec &m, uint32_t seed, int cap) {
    Game g;
    g.start(missionConfig(m, seed));
    for (auto &t : g.cfg.teamSetup) t.cpu = std::max<uint8_t>(t.cpu, 3);
    Ai ai;
    Run r;
    for (; r.ticks < cap && g.phase != Phase::GameOver; r.ticks++) {
        if (r.ticks > 60 * 60 * 3) assist(g);
        g.step(ai.think(g));
    }
    r.result = g.run.result, r.sum = g.checksum(), r.rep = scriptReport(g);
    return r;
}

int main() {
    assert(loadWeapons("romfs/weapons.json"));
    std::vector<MissionSpec> list = listMissions("./romfs/", "./");
    const char *only = getenv("W4NX_MISSION");
    int ours = 0, imported = 0, ended = 0;
    for (const MissionSpec &m : list) {
        if (m.script.empty() || (only && m.id != only)) continue;
        imported++;
        Run r = scripted(m, 5, 60 * 60 * 60);
        printf("%-24s %-9s %s after %5d s, Lua errors %d%s%s\n", m.id.c_str(), m.kind.c_str(), r.result > 0 ? "won " : r.result < 0 ? "lost" : "NOT ENDED",
               r.ticks / 60, r.rep.errors, r.rep.errors ? ": " : "", r.rep.lastError.c_str());
        for (auto &k : r.rep.ignored) printf("    not modelled: %s x%d\n", k.first.c_str(), k.second);
        for (auto &k : r.rep.missingKeys) printf("    missing data: %s x%d\n", k.first.c_str(), k.second);
        fflush(stdout);
        assert(r.rep.errors == 0 && r.rep.missingKeys.empty());
        bool blocked = m.id == "TraitorousWaters";  // ends only on Trigger_Destroyed (guns, houses) and has no round clock: lot 2 triggers
        ended += r.result != 0 || blocked;
    }
    if (only) return 0;
    for (const MissionSpec &m : list) {
        if (!m.script.empty()) continue;
        ours++;
        Game g;
        g.start(missionConfig(m, 1));
        for (size_t t = 0; t < m.teams.size(); t++) {
            int alive = 0;
            for (const Worm &w : g.worms) alive += w.team == (int)t && w.alive;
            if (alive != (int)m.teams[t].worms.size()) printf("%s: team %zu has %d worms, want %zu\n", m.id.c_str(), t, alive, m.teams[t].worms.size());
            assert(alive == (int)m.teams[t].worms.size());
        }
        for (const Worm &w : g.worms) if (w.alive && w.pos.y < g.water + 0.3f) printf("  warning %s: worm %d starts in the water\n", m.id.c_str(), int(&w - g.worms.data()));
        for (const MissionSpec::ObjectSpec &o : m.objects)
            if (!o.at.set && std::none_of(g.terrain.markers.begin(), g.terrain.markers.end(), [&](const Terrain::Marker &k) { return k.name == o.at.marker; }))
                printf("  warning %s: no marker '%s'\n", m.id.c_str(), o.at.marker.c_str());
        assert(g.phase != Phase::GameOver && g.run.result == 0);
        int win = forceWin(m), lose = forceLose(m);
        printf("%-24s %-9s %-15s %zu teams, %2zu objects: win %d lose %d  [%s]\n", m.id.c_str(), m.kind.c_str(), m.campaign.c_str(), m.teams.size(), m.objects.size(),
               win, lose, goalText(m, m.objectives[0], nullptr).c_str());
        fflush(stdout);
        assert(win == 1 && lose == -1);
    }
    assert(ours >= 3);
    for (const MissionSpec &x : list)  // a scripted mission plays the same twice (its Lua state is in the checksum)
        if (x.id == "DeathMatch1") {
            Run a = scripted(x, 11, 60 * 60 * 3), b = scripted(x, 11, 60 * 60 * 3);
            assert(a.sum == b.sum && a.ticks == b.ticks);
        }
    printf("W4M missions: %d of %d ended\n", ended, imported);
    assert(ended == imported);

    // AI plays both sides of the first mission: it ends, and replays identically
    MissionSpec m = *std::find_if(list.begin(), list.end(), [](const MissionSpec &x) { return x.script.empty(); });
    m.teams[0].cpu = 3;
    uint32_t sums[2];
    for (int k = 0; k < 2; k++) {
        Game g;
        g.start(missionConfig(m, 99));
        Ai ai;
        int t = 0;
        for (; t < 60 * 60 * 25 && g.phase != Phase::GameOver; t++) g.step(ai.think(g));
        sums[k] = g.checksum();
        if (!k) printf("AI run %s: result %d after %d s\n", m.id.c_str(), g.run.result, t / 60);
        assert(g.run.result != 0);
    }
    assert(sums[0] == sums[1]);

    // a shotgun blast pops a target placed in the line of fire (shots and blasts reach targets)
    for (const MissionSpec &t : list) {
        if (t.id != "10_target_practice") continue;
        Game g;
        g.start(missionConfig(t, 3));
        for (size_t k = 0; k < WEAPONS.size(); k++) if (WEAPONS[k].name == "Shotgun") g.weapon = (int)k;
        for (Object &o : g.objects) if (o.tag >= 0) o.pos = Vector3Add(g.worms[0].pos, Vector3Scale(g.aimDir(g.worms[0]), 6));
        Input fire;
        fire.buttons = Input::FIRE;
        for (int k = 0; k < 120; k++) g.step(k % 20 < 2 ? fire : Input{});
        printf("target practice: %d destroyed by the shotgun\n", g.run.destroyed);
        assert(g.run.destroyed >= 1);
    }

    Progress p;
    p.record("a", false, 0), p.record("a", true, 900), p.record("a", true, 1200);
    p.save("progress_check.txt");
    Progress q;
    q.load("progress_check.txt");
    remove("progress_check.txt");
    assert(q.get("a").done && q.get("a").best == 900 && !q.get("b").done);
    for (size_t i = 1; i < list.size(); i++)
        if (list[i].kind == "mission" && list[i - 1].kind == "mission" && list[i].campaign == list[i - 1].campaign) { assert(!Progress{}.unlocked(list, i)); break; }
    printf("mission_check ok: %d bundled, %d imported\n", ours, imported);
}

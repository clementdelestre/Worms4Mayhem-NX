// AI vs AI: matches deal damage and finish, replay bit-identically, CPU5 beats CPU1, rope race reaches the finish.
// Run from client/ (romfs maps). Prints weapon usage, damage per turn and planning cost.
#include "../src/ai.h"
#include "../src/controls.h"
#include "raymath.h"
#include <cassert>
#include <chrono>
#include <cstdio>

static std::vector<int> fires[6];  // [level][weapon], all matches
static double maxTick = 0, maxTurn = 0;

struct Result { uint32_t sum; int turns, fired, damage, winner; bool over, reached; };

static Result match(const char *map, uint32_t seed, uint8_t l0, uint8_t l1, uint32_t rules = 0, bool quiet = false, int only = -1, int maxTurns = 1000,
                    const Scheme &scheme = Scheme{}) {
    Game g;
    GameConfig c{seed, 2, 3, map, rules};
    c.scheme = scheme;
    c.teamSetup = {{"CPU", l0}, {"CPU", l1}};
    g.start(c);
    if (only >= 0)  // single weapon test: that weapon plus Skip Go
        for (auto &a : g.ammo)
            for (size_t k = 0; k < a.size(); k++) a[k] = (int)k == only ? 9 : WEAPONS[k].kind == Kind::SkipGo ? -1 : 0;
    Ai ai;
    Result r{};
    int team = 0, before = 0;
    double turn = 0;
    auto enemyHp = [&] {
        int s = 0;
        for (const Worm &w : g.worms) if (w.team != team) s += w.alive && w.hp > 0 ? w.hp : 0;
        return s;
    };
    Phase prev = Phase::Settle;
    for (int t = 0; t < 60 * 60 * 30 && g.phase != Phase::GameOver && r.turns <= maxTurns; t++) {
        auto t0 = std::chrono::steady_clock::now();
        Input in = ai.think(g);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        maxTick = std::max(maxTick, ms);
        turn += ms;
        g.step(in);
        bool fire = false;  // a 0 s retreat (strikes, old woman) goes from Aim to Settle in one tick: count the Fire event
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Fire && e.worm >= 0) fires[g.worms[e.worm].team ? l1 : l0][e.weapon]++, fire = true;
        if (prev == Phase::Settle && g.phase != Phase::Settle) {  // turn boundary
            if (r.turns) r.damage += before - enemyHp();
            maxTurn = std::max(maxTurn, turn);
            turn = 0;
            team = g.worms[g.current].team;
            before = enemyHp();
            r.turns++;
        }
        if (prev == Phase::Aim && g.phase != Phase::Aim && (g.phase != Phase::Settle || fire)) r.fired++;
        prev = g.phase;
    }
    r.sum = g.checksum();
    r.over = g.phase == Phase::GameOver;
    r.winner = g.winner;
    for (const Worm &w : g.worms) r.reached |= w.alive && Vector3Distance(w.pos, g.raceFinish) < 2;
    if (!quiet)
        printf("%-12s seed %3u lvl %d/%d: %3d turns, %3d shots, %5.1f dmg/turn, %s (winner %d)\n", *map ? map : "island", seed, l0, l1, r.turns,
               r.fired, r.turns ? (float)r.damage / r.turns : 0.f, r.over ? "over" : "timeout", g.winner);
    return r;
}

// The CPU's turn shows the Blimp view only while its plan fires a targeted weapon, never because the team's
// last weapon (reselected at turn start) is one, nor during an instant replay of it.
static void blimpView() {
    int strikes = 0, stale = 0;
    for (uint32_t seed = 1; seed <= 4; seed++) {
        Game g;
        GameConfig c{seed, 2, 2, "", 0};
        c.teamSetup = {{"CPU", 3}, {"CPU", 3}};
        g.start(c);
        for (auto &a : g.ammo) for (size_t k = 0; k < a.size(); k++) if (targeted(WEAPONS[k].kind) && WEAPONS[k].kind != Kind::Teleport) a[k] = 9;
        Ai ai;
        bool shown = false;
        for (int t = 0; t < 60 * 60 * 8 && g.phase != Phase::GameOver; t++) {
            g.step(ai.think(g));
            int sim = g.weapon;
            Controls::cpuTurn = ai.striking();
            bool v = Controls::targetView(g);
            stale += Controls::targetHeld(g) && !v;
            shown |= v;
            g.weapon = sim;
            for (const GameEvent &e : g.events)
                if (e.kind == GameEvent::Fire && e.worm >= 0) {
                    assert(!shown || targeted(WEAPONS[e.weapon].kind));
                    strikes += shown;
                }
            if (g.phase != Phase::Aim) shown = false;
        }
    }
    printf("blimp view: %d CPU strikes seen from it, %d ticks a targeted weapon was held without a strike planned\n", strikes, stale);
    assert(strikes > 0);
}

// Enemy right next to the active CPU worm: the weapon it fires first.
static int pointBlank(uint32_t seed, uint8_t level) {
    Game g;
    GameConfig c{seed, 2, 1, "", 0};
    c.teamSetup = {{"CPU", level}, {"CPU", level}};
    g.start(c);
    for (int t = 0; t < 120; t++) g.step(Input{1});  // land; any input skips the hot seat
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    v.pos = a.pos + Vector3{sinf(a.yaw), 0.1f, cosf(a.yaw)} * 1.0f;
    v.vel = {0, 0, 0};
    Ai ai;
    for (int t = 0; t < 60 * 40 && g.phase == Phase::Aim; t++) {
        g.step(ai.think(g));
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Fire && e.worm >= 0) return e.weapon;
    }
    return -1;
}

// The first shots of a match with a given planning budget: the plan must not depend on how the think is sliced.
struct Fired { int weapon; Vector3 pos; float yaw, pitch; };
static std::vector<Fired> shots(const char *map, uint32_t seed, uint8_t level, long budget) {
    Game g;
    GameConfig c{seed, 2, 3, map, 0};
    c.teamSetup = {{"CPU", level}, {"CPU", level}};
    g.start(c);
    Ai ai;
    ai.budget = budget;
    std::vector<Fired> out;
    for (int t = 0; t < 60 * 60 * 20 && out.size() < 6 && g.phase != Phase::GameOver; t++) {
        g.step(ai.think(g));
        for (const GameEvent &e : g.events)
            if (e.kind == GameEvent::Fire && e.worm >= 0) out.push_back({e.weapon, g.worms[e.worm].pos, g.worms[e.worm].yaw, g.worms[e.worm].pitch});
    }
    return out;
}

// A thin wall in front of the CPU, its enemy behind: no walk or jump against the wall (W4M Fits nodes, 0x59edf0)
// and no shot into it (launch from the eye, a negative plan skips the turn: 0x585a29, 0x49e6d0).
static void wallAhead(uint32_t seed, uint8_t level, float x0, bool bazooka) {
    Game g;
    GameConfig c{seed, 2, 1, "", 0};
    c.teamSetup = {{"CPU", level}, {"CPU", level}};
    g.start(c);
    for (int z = 16; z < 80; z++)  // floor at y 50, a one-voxel slab at x 22 m (face at 21.83 m)
        for (int y = 176; y < 248; y++)
            for (int x = 16; x < 300; x++)
                g.terrain.d[((size_t)z * Terrain::NY + y) * Terrain::NX + x] = x == 88 && y > 200 ? 127 : (signed char)Clamp((50 - y * Terrain::VOX) * Terrain::Q, -64, 64);
    for (auto &a : g.ammo)
        for (size_t k = 0; k < a.size(); k++) a[k] = (bazooka && WEAPONS[k].name == "Bazooka") || WEAPONS[k].name == "Shotgun" ? 9 : WEAPONS[k].kind == Kind::SkipGo ? -1 : 0;
    Worm &a = g.worms[g.current], &v = g.worms[1 - g.current];
    a.pos = {x0, 50.6f, 12}, v.pos = {30, 50.6f, 12}, a.vel = v.vel = {}, a.yaw = PI / 2, v.yaw = -PI / 2;
    g.hotSeat = 0, g.wind = 0;
    Ai ai;
    const int hp = a.hp;
    float far = 0, nearBoom = 1e9f;
    for (int t = 0; t < 60 * 45 && g.phase != Phase::Settle; t++) {
        g.step(ai.think(g));
        far = fmaxf(far, a.pos.x);
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom) nearBoom = fminf(nearBoom, Vector3Distance(e.pos, a.pos));
    }
    printf("wall %.2f m ahead, CPU%d: furthest x %.2f, nearest blast %.1f m, hp %d -> %d\n", 21.83f - x0, level, far, nearBoom, hp, a.hp);
    assert(far < fmaxf(x0 + 0.05f, 21.5f) && a.hp == hp && nearBoom > 2);  // 21.5: the face less 0.33 m
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    assert(loadWeapons("romfs/weapons.json"));
    for (uint8_t level : {1, 3, 5}) wallAhead(5, level, 21.4f, true), wallAhead(5, level, 19.4f, false);
    for (auto &f : fires) f.assign(WEAPONS.size(), 0);
    blimpView();
    const char *maps[] = {"", "arabian", "wildwest", "camelot", "jurassic", "construction"};
    int over = 0, n = 0;
    for (const char *map : maps)
        for (uint8_t level : {1, 2, 3, 4, 5}) {
            Result a = match(map, 7 + level, level, level);
            assert(a.damage > 0 && a.fired > 0);
            over += a.over;
            n++;
        }
    assert(over * 4 >= n * 3);  // most matches finish
    for (int l = 1; l <= 5; l++) {
        printf("level %d:", l);
        for (size_t i = 0; i < WEAPONS.size(); i++) if (fires[l][i]) printf(" %s:%d", WEAPONS[i].name.c_str(), fires[l][i]);
        printf("\n");
    }
    int changes = 0;  // W4M worm-select mode: the CPU plans every worm of its team and switches with Worm Select
    for (int l = 1; l <= 5; l++) for (size_t i = 0; i < WEAPONS.size(); i++) changes += WEAPONS[i].kind == Kind::ChangeWorm ? fires[l][i] : 0;
    assert(changes > 0);

    int close = 0;
    printf("point blank:");
    for (uint32_t seed = 1; seed <= 12; seed++) {
        int wi = pointBlank(seed, 1 + seed % 5);
        printf(" %s", wi < 0 ? "-" : WEAPONS[wi].name.c_str());
        close += wi >= 0 && (WEAPONS[wi].kind == Kind::Melee || WEAPONS[wi].kind == Kind::Shotgun || dropped(WEAPONS[wi]));
    }
    printf("\n");
    // adjacent enemy: melee, a gun or dynamite set down before the retreat, not a blast that hurts the shooter.
    // No melee quota: W4M scores damage linearly with no point-blank bonus, so 75 hp dynamite outranks a 30 hp bat.
    assert(close >= 10);

    int wins = 0, games = 0;
    for (const char *map : maps)
        for (uint32_t seed : {21u, 22u}) {
            Result a = match(map, seed, 5, 1, 0, true), b = match(map, seed, 1, 5, 0, true);
            wins += (a.winner == 0) + (b.winner == 1);
            games += 2;
        }
    printf("CPU5 vs CPU1: %d/%d wins\n", wins, games);
    assert(wins * 3 >= games * 2);

    printf("single weapon, 8 turns, dmg/turn:");
    for (size_t k = 0; k < WEAPONS.size(); k++) {
        Kind kd = WEAPONS[k].kind;
        if (kd == Kind::Rope || kd == Kind::Jetpack || kd == Kind::Teleport || kd == Kind::Parachute || kd > Kind::Flood) continue;  // Landmine, Scouser, Flood: W4M AI plans too
        int d = 0, turns = 0;
        for (const char *map : {"", "arabian"}) { Result x = match(map, 31, 5, 5, 0, true, (int)k, 8); d += x.damage; turns += x.turns; }
        printf(" %s %.1f", WEAPONS[k].name.c_str(), (float)d / turns);
    }
    printf("\n");

    for (const SchemePreset &p : SCHEMES) {  // every preset plays: short turns, sudden death, odd weapon sets
        Scheme sc = p.s;
        sc.roundTime = 5;
        Result x = match("arabian", 41, 2, 2, RULE_SUDDEN_DEATH, true, -1, 60, sc);
        printf("%-10s %3d turns, %3d shots, %5.1f dmg/turn, %s\n", p.name, x.turns, x.fired, x.turns ? (float)x.damage / x.turns : 0.f, x.over ? "over" : "timeout");
        assert(x.fired > 0 && (x.damage > 0 || sc.weapons == Scheme::SET_CRATES));
    }

    printf("planning cost: max %.2f ms per tick, %.2f ms per turn\n", maxTick, maxTurn);
    for (const char *map : {"arabian", "jurassic"}) {  // sliced vs all in one tick: same shots
        auto a = shots(map, 3, 5, Ai{}.budget), b = shots(map, 3, 5, 1L << 40);
        assert(a.size() == b.size());
        for (size_t i = 0; i < a.size(); i++)
            assert(a[i].weapon == b[i].weapon && Vector3Distance(a[i].pos, b[i].pos) < 1e-4f && fabsf(a[i].yaw - b[i].yaw) < 1e-4f && fabsf(a[i].pitch - b[i].pitch) < 1e-4f);
    }
    maxTick = maxTurn = 0;
    for (uint8_t level : {1, 5}) assert(match("ropetrack", 5, level, level, RULE_ROPE_RACE).reached);
    Result a = match("", 99, 2, 2, RULE_KARMA | RULE_VAMPIRE), b = match("", 99, 2, 2, RULE_KARMA | RULE_VAMPIRE);
    assert(a.sum == b.sum);
    printf("rope race cost: max %.2f ms per tick, %.2f ms per turn\n", maxTick, maxTurn);
    printf("ai_check OK (%d/%d matches over)\n", over, n);
}

// AI vs AI: matches deal damage and finish, replay bit-identically, level 3 beats level 1, rope race reaches the finish.
// Run from client/ (romfs maps). Prints weapon usage, damage per turn and planning cost.
#include "../src/ai.h"
#include "raymath.h"
#include <cassert>
#include <chrono>
#include <cstdio>

static std::vector<int> fires[4];  // [level][weapon], all matches
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
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Fire && e.worm >= 0) fires[g.worms[e.worm].team ? l1 : l0][e.weapon]++;
        if (prev == Phase::Settle && g.phase != Phase::Settle) {  // turn boundary
            if (r.turns) r.damage += before - enemyHp();
            maxTurn = std::max(maxTurn, turn);
            turn = 0;
            team = g.worms[g.current].team;
            before = enemyHp();
            r.turns++;
        }
        if (prev == Phase::Aim && g.phase != Phase::Aim && g.phase != Phase::Settle) r.fired++;
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

int main() {
    SetTraceLogLevel(LOG_WARNING);
    assert(loadWeapons("romfs/weapons.json"));
    for (auto &f : fires) f.assign(WEAPONS.size(), 0);
    const char *maps[] = {"", "arabian", "wildwest", "camelot", "jurassic", "construction"};
    int over = 0, n = 0;
    for (const char *map : maps)
        for (uint8_t level : {1, 2, 3}) {
            Result a = match(map, 7 + level, level, level);
            assert(a.damage > 0 && a.fired > 0);
            over += a.over;
            n++;
        }
    assert(over * 4 >= n * 3);  // most matches finish
    for (int l = 1; l <= 3; l++) {
        printf("level %d:", l);
        for (size_t i = 0; i < WEAPONS.size(); i++) if (fires[l][i]) printf(" %s:%d", WEAPONS[i].name.c_str(), fires[l][i]);
        printf("\n");
    }

    int melee = 0, close = 0;
    printf("point blank:");
    for (uint32_t seed = 1; seed <= 12; seed++) {
        int wi = pointBlank(seed, 1 + seed % 3);
        printf(" %s", wi < 0 ? "-" : WEAPONS[wi].name.c_str());
        melee += wi >= 0 && WEAPONS[wi].kind == Kind::Melee;
        close += wi >= 0 && (WEAPONS[wi].kind == Kind::Melee || WEAPONS[wi].kind == Kind::Shotgun);
    }
    printf("\n");
    assert(melee >= 3 && close >= 10);  // adjacent enemy: melee or a gun, not a blast that hurts the shooter

    int wins = 0, games = 0;
    for (const char *map : maps)
        for (uint32_t seed : {21u, 22u}) {
            Result a = match(map, seed, 3, 1, 0, true), b = match(map, seed, 1, 3, 0, true);
            wins += (a.winner == 0) + (b.winner == 1);
            games += 2;
        }
    printf("level 3 vs level 1: %d/%d wins\n", wins, games);
    assert(wins * 3 >= games * 2);

    printf("single weapon, 8 turns, dmg/turn:");
    for (size_t k = 0; k < WEAPONS.size(); k++) {
        Kind kd = WEAPONS[k].kind;
        if (kd == Kind::Rope || kd == Kind::Jetpack || kd == Kind::Teleport || kd == Kind::Parachute || kd >= Kind::Flood || kd == Kind::Mine || kd == Kind::Scouser) continue;
        int d = 0, turns = 0;
        for (const char *map : {"", "arabian"}) { Result x = match(map, 31, 3, 3, 0, true, (int)k, 8); d += x.damage; turns += x.turns; }
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
    maxTick = maxTurn = 0;
    for (uint8_t level : {1, 3}) assert(match("ropetrack", 5, level, level, RULE_ROPE_RACE).reached);
    Result a = match("", 99, 2, 2, RULE_KARMA | RULE_VAMPIRE), b = match("", 99, 2, 2, RULE_KARMA | RULE_VAMPIRE);
    assert(a.sum == b.sum);
    printf("rope race cost: max %.2f ms per tick, %.2f ms per turn\n", maxTick, maxTurn);
    printf("ai_check OK (%d/%d matches over)\n", over, n);
}

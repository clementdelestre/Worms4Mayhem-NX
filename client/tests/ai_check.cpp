// AI vs AI: matches must deal damage, usually end, and replay bit-identically. Run from client/ (romfs maps).
#include "../src/ai.h"
#include <cassert>
#include <cstdio>

static std::vector<int> fires;  // per weapon, all matches

struct Result { uint32_t sum; int turns, fired, hits, damage; bool over; };

static Result match(const char *map, uint32_t seed, uint8_t level) {
    Game g;
    GameConfig c{seed, 2, 3, map, 0};
    c.teamSetup.assign(2, {"CPU", level});
    g.start(c);
    Ai ai;
    Result r{};
    int team = 0, before = 0;
    auto enemyHp = [&] {
        int s = 0;
        for (const Worm &w : g.worms) if (w.team != team) s += w.hp > 0 ? w.hp : 0;
        return s;
    };
    Phase prev = Phase::Settle;
    for (int t = 0; t < 60 * 60 * 20 && g.phase != Phase::GameOver; t++) {
        g.step(ai.think(g));
        for (const GameEvent &e : g.events) if (e.kind == GameEvent::Fire) fires[e.weapon]++;
        if (prev == Phase::Settle && g.phase != Phase::Settle) {  // turn boundary
            if (r.turns) { int d = before - enemyHp(); r.damage += d; r.hits += d > 0; }
            team = g.worms[g.current].team;
            before = enemyHp();
            r.turns++;
        }
        if (prev == Phase::Aim && g.phase != Phase::Aim && g.phase != Phase::Settle) r.fired++;
        prev = g.phase;
    }
    r.sum = g.checksum();
    r.over = g.phase == Phase::GameOver;
    printf("%-10s seed %u lvl %d: %3d turns, %3d shots, %3d hits, %4d dmg, %s (winner %d)\n", *map ? map : "island", seed, level, r.turns,
           r.fired, r.hits, r.damage, r.over ? "over" : "timeout", g.winner);
    return r;
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    assert(loadWeapons("romfs/weapons.json"));
    fires.assign(WEAPONS.size(), 0);
    int over = 0, n = 0;
    for (const char *map : {"", "arabian", "wildwest"})
        for (uint8_t level : {1, 2, 3}) {
            Result a = match(map, 7 + level, level);
            assert(a.damage > 0 && a.fired > 0);
            over += a.over;
            n++;
        }
    Result a = match("", 99, 2), b = match("", 99, 2);
    assert(a.sum == b.sum);
    assert(over * 2 >= n);  // most matches finish
    for (size_t i = 0; i < WEAPONS.size(); i++) printf("%s:%d ", WEAPONS[i].name.c_str(), fires[i]);
    printf("\nai_check OK (%d/%d matches over)\n", over, n);
}

// Recorded AI match: save/load/re-sim gives the same checksum; instant-replay restore (snapshot or checkpoint) + re-sim,
// or a skip (forward), lands on the live state.
// Run from client/ (romfs maps).
#include "../src/ai.h"
#include "../src/replay.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>

static uint32_t voxels(const Game &g) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < g.terrain.d.size(); i++) h = (h ^ (uint8_t)g.terrain.d[i]) * 16777619u;
    uint32_t ex = 0;  // exact-land lists, order-free
    const SharpLand &sh = g.terrain.sharp;
    for (uint32_t k : sh.key) {
        if (!k || !sh.mixed(k - 1)) continue;
        uint32_t c = (k - 1) * 2654435761u;
        for (uint32_t w : sh.list(k - 1)) c = (c ^ w) * 16777619u;
        ex += c;
    }
    return h ^ ex * 31;
}
static double ms(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

int main() {
    loadWeapons("romfs/weapons.json");
    for (const char *map : {"", "arabian", "Accuracy", "BuildingSiteSaboteurs"}) {
        Recording rec;
        rec.cfg = GameConfig{77, 2, 3, map};
        rec.cfg.teamSetup = {{"CPU", 2}, {"CPU", 2}};
        if (*map) rec.cfg.wormpot = WP_DOUBLE_DAMAGE;
        if (!strcmp(map, "arabian")) {  // each team its own Weapon Factory weapon, through the W4R6 team records
            WeaponDef a = {"Lobber A", Kind::Shell, 4, 70, 25, 2, 0.5f, 1.5f, 20, 2, 3, 1, false, 4}, b = a;
            b.name = "Strike B", b.kind = Kind::Airstrike, b.clusters = 3;
            rec.cfg.teamSetup[0].secret = a, rec.cfg.teamSetup[1].secret = b;
        }
        Game g;
        g.start(rec.cfg);
        Ai ai;
        Snapshot snap;
        snap.take(g, 0);
        std::map<uint32_t, uint32_t> sums = {{0, g.checksum()}};  // checksum at the snapshot and each checkpoint
        uint32_t snapSum = g.checksum(), snapVox = voxels(g);
        int replays = 0;
        size_t maxLog = 0;
        double take = 0, restore = 0, resim = 0, resimCp = 0, step = 0;
        Phase prev = g.phase;
        for (uint32_t tick = 0; tick < 60 * 60 * 20 && g.phase != Phase::GameOver; tick++) {
            Input in = ai.think(g);
            rec.inputs.push_back(in);
            auto t0 = std::chrono::steady_clock::now();
            g.step(in);
            step += ms(t0);
            bool turnStart = false;
            for (const GameEvent &e : g.events) turnStart |= e.kind == GameEvent::TurnStart;
            if (turnStart) {  // as main.cpp: one snapshot per turn, a checkpoint every 30 ticks
                t0 = std::chrono::steady_clock::now();
                snap.take(g, tick + 1);
                take = std::max(take, ms(t0));
                snapSum = g.checksum(), snapVox = voxels(g);
                sums.clear(), sums[tick + 1] = snapSum;
            } else if ((tick + 1 - snap.tick) % 30 == 0) snap.mark(g, tick + 1), sums[tick + 1] = g.checksum();
            if (prev == Phase::Flying && g.phase != Phase::Flying) {  // instant replay of the shot
                uint32_t liveSum = g.checksum(), liveVox = voxels(g);
                maxLog = std::max(maxLog, snap.log.size());
                bool skip = replays % 2;  // odd replays: from a checkpoint 45 ticks back, skipped halfway (forward)
                t0 = std::chrono::steady_clock::now();
                uint32_t from = snap.restore(g, skip ? std::max(snap.tick, tick + 1 > 45 ? tick + 1 - 45 : 0) : snap.tick);
                restore = std::max(restore, ms(t0));
                assert(g.checksum() == sums[from] && (from != snap.tick || voxels(g) == snapVox));
                t0 = std::chrono::steady_clock::now();
                uint32_t to = skip ? (from + tick + 1) / 2 : tick + 1;
                for (uint32_t t = from; t < to; t++) g.step(rec.inputs[t]);
                double &worst = skip ? resimCp : resim;
                worst = std::max(worst, ms(t0));
                if (skip) assert(snap.forward(g));
                assert(g.checksum() == liveSum && voxels(g) == liveVox);
                replays++;
            }
            prev = g.phase;
        }
        rec.checksum = g.checksum();
        const char *path = "replay_check.w4r";
        bool saved = rec.save(path);
        Recording back;
        bool loaded = back.load(path);
        assert(saved && loaded);
        assert(back.inputs.size() == rec.inputs.size() && back.checksum == rec.checksum && back.cfg.map == rec.cfg.map && back.cfg.wormpot == rec.cfg.wormpot);
        for (size_t t = 0; t < rec.cfg.teamSetup.size(); t++)
            assert(back.cfg.teamSetup[t].secret.has_value() == rec.cfg.teamSetup[t].secret.has_value() &&
                   (!rec.cfg.teamSetup[t].secret || back.cfg.teamSetup[t].secret->name == rec.cfg.teamSetup[t].secret->name));
        Game h;
        h.start(back.cfg);
        auto t0 = std::chrono::steady_clock::now();
        for (const Input &in : back.inputs) h.step(in);
        double ff = ms(t0);
        assert(h.checksum() == rec.checksum && voxels(h) == voxels(g));
        printf("%-8s %zu ticks (%zu KB file) %s, %d instant replays ok; step avg %.3f ms, fast-forward %.0f ticks/s\n", *map ? map : "island", rec.inputs.size(),
               rec.inputs.size() * 4 / 1024, g.phase == Phase::GameOver ? "game over" : "timeout", replays, step / rec.inputs.size(), back.inputs.size() / ff * 1000);
        printf("         snapshot take max %.3f ms, restore max %.3f ms (undo log max %zu voxels = %zu KB), re-sim max %.1f ms (from the snapshot) %.1f ms (from a checkpoint, half)\n", take, restore,
               maxLog, maxLog * sizeof(snap.log[0]) / 1024, resim, resimCp);
        remove(path);
    }
    puts("replay_check OK");
}

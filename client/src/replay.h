#pragma once
#include "sim.h"
#include <cstdint>
#include <string>
#include <vector>

// Whole match: config + one Input per tick (index = tick). Re-simulating it reproduces the match exactly.
struct Recording {
    GameConfig cfg;
    std::vector<Input> inputs;
    uint32_t checksum = 0;  // Game::checksum() after the last input, 0 = unknown
    bool save(const std::string &path) const;
    bool load(const std::string &path);
};
std::vector<std::string> listReplays(const std::string &dir);  // *.w4r file names, newest first
std::string replayName(const GameConfig &cfg);                  // <date>_<map>.w4r

// Game state minus the voxels at some tick; voxels carved since are rolled back from `log` (terrain.undo).
// Checkpoints (mark) bound the re-simulation an instant replay needs; restore() sets the live state aside for forward().
struct Snapshot {
    Game g;
    std::vector<Terrain::Object> decor;
    std::vector<Terrain::Emitter> emit;
    std::vector<std::vector<char>> frameAlive;
    std::vector<std::pair<int, signed char>> log;
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> slog;  // exact-land lists changed (cell, list before)
    std::vector<std::pair<uint32_t, float>> tlog;  // heightmap tops lowered by blasts (column, top before)
    SharpLand::Sizes prims;
    uint32_t tick = 0;
    bool valid = false;
    struct Checkpoint { uint32_t tick; Game g; std::vector<Terrain::Object> decor; std::vector<Terrain::Emitter> emit; std::vector<std::vector<char>> frameAlive; size_t log, slog, tlog; SharpLand::Sizes prims; };
    std::vector<Checkpoint> cps;
    Game liveG;
    std::vector<Terrain::Object> liveDecor;
    std::vector<Terrain::Emitter> liveEmit;
    std::vector<std::vector<char>> liveFrameAlive;
    std::vector<std::pair<int, signed char>> redo;  // (voxel, live density); negative voxel: a girder's steel
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> sredo;  // (cell, live list)
    std::vector<std::pair<uint32_t, float>> tredo;  // (column, live top)
    SharpLand::Sizes pbase;
    SharpLand::Tail ptail;  // primitives made after the restored state, for forward()
    bool hasLive = false;
    void take(Game &live, uint32_t tick);
    void mark(Game &live, uint32_t tick);  // a checkpoint (the 20 newest are kept)
    // live becomes the newest state at or before upTo (returned: its tick); marks the touched chunks dirty unless the
    // meshes kept since take() cover them
    uint32_t restore(Game &live, uint32_t upTo = UINT32_MAX);
    bool forward(Game &live);  // back to the state restore() set aside; false if none
};

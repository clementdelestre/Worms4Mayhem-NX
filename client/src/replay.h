#pragma once
#include "sim.h"
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
struct Snapshot {
    Game g;
    std::vector<Terrain::Object> decor;
    std::vector<std::pair<int, signed char>> log;
    uint32_t tick = 0;
    bool valid = false;
    void take(Game &live, uint32_t tick);
    void restore(Game &live);  // live becomes the snapshot again; marks the touched chunks dirty
};

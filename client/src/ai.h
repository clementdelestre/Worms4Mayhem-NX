#pragma once
#include "sim.h"

// CPU player: reads the Game, plans one shot per turn and plays it as Inputs, so it is logged/relayed like a human.
// Level (cfg.teamSetup[team].cpu) 1..3: aim error, wind compensation, weapon choice and walking.
struct Ai {
    Input think(const Game &g);

private:
    struct Plan { int weapon = 0, charge = 0, target = -1; float yaw = 0, pitch = 0, score = -1e9f; };
    Plan plan;
    int worm = -1, lastTimer = -1, tick = 0, walk = 0, walks = 0, charged = 0;
    Vector3 lastPos{};
    void replan(const Game &g);
};

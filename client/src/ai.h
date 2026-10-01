#pragma once
#include "sim.h"

// CPU player: reads the Game and plays Inputs, so it is logged/relayed like a human.
// Level (cfg.teamSetup[team].cpu) 1..3: aim error, wind, weapon set, knockback/chain prediction and mobility.
// Planning is a state machine: one (weapon, target) pair per tick bounds the per-frame cost.
struct Ai {
    Input think(const Game &g);
    int picking = -1;  // weapon NEXT_WEAPON steps toward this tick: shown instead of the ones on the way

    struct Plan { int weapon = -1, charge = 0, target = -1; float yaw = 0, pitch = 0, score = -1e9f, rank = -1e9f; };  // rank: score + taste
    struct RopePlan { float yaw = 0, pitch = 0; int release = -1; bool reel = false; };  // release < 0: walk toward the finish
    struct RopeRun { int t = 0, held = 0, after = -1; bool fired = false, done = true; };

private:
    enum class Mode { Eval, Walk, Act, Jet };
    Plan plan;
    Mode mode = Mode::Eval;
    uint32_t salt = 0;  // per turn: seeds the weapon taste
    int worm = -1, lastTimer = -1, tick = 0, stage = 0, walk = 0, walks = 0, charged = 0, shotsSeen = 0;
    bool moved = false, retreatPicked = false, retreatOn = false, chuteWanted = false;
    float stopAt = 0, retreatYaw = 0;
    Vector3 goal{}, lastPos{}, lastBoom{};
    std::vector<Vector3> tpos;  // targets: enemy worms, then barrels/mines next to enemies
    std::vector<int> tworm;     // worm aimed at (nearest enemy for object targets)
    RopePlan rope;
    RopeRun run;

    void startEval(const Game &g);
    void evalWeapon(const Game &g, int wi, int only = -1);  // only: one target index
    void decide(const Game &g);
    void finish(const Game &g);
    Input act(const Game &g);
    Input walkTo(const Game &g);
    Input jet(const Game &g);
    Input retreat(const Game &g);
    Input race(const Game &g);
};

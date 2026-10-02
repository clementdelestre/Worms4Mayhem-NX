#pragma once
#include "sim.h"
#include <memory>

// CPU player: reads the Game and plays Inputs, so it is logged/relayed like a human.
// Level (cfg.teamSetup[team].cpu) 1..5: W4M AIParams.CPU1..CPU5 (aim error, scoring weights, crates, delays, jumps).
// Planning is a state machine sliced over frames by a work budget (W4M 80 cost units per frame); the plan never depends on the slicing.
struct Ai {
    Input think(const Game &g);
    int picking = -1;  // weapon NEXT_WEAPON steps toward this tick: shown instead of the ones on the way
    bool striking() const { return mode == Mode::Act && plan.weapon >= 0 && targeted(WEAPONS[plan.weapon].kind) && WEAPONS[plan.weapon].kind != Kind::Homing; }  // seen from the Blimp; its homing never locks

    struct Plan { int weapon = -1, charge = 0, target = -1; float yaw = 0, pitch = 0, score = -1e9f, rank = -1e9f; };  // rank: score + taste
    struct RopePlan { float yaw = 0, pitch = 0; int release = -1; bool reel = false; };  // release < 0: walk toward the finish
    struct RopeRun { int t = 0, held = 0, after = -1; bool fired = false, done = true; };
    struct Step { Vector3 to; float yaw = 0; uint8_t move = 0; };  // W4M path move: 0 WALK to `to`, 1 JUMP_FORWARD, 2 JUMP_BACKFLIP along yaw
    struct StepRun { int t = 0; bool air = false, done = false, stuck = false; };  // stuck: a walk held off its node
    long budget = 20000;  // Terrain::samples per frame: ~0.7 ms on desktop, plus at most one unit (< 2.5 ms)

private:
    enum class Mode { Eval, Search, Walk, Act, Jet };
    Plan plan;
    Mode mode = Mode::Eval;
    uint32_t salt = 0;  // per turn: seeds the weapon taste
    int worm = -1, lastTimer = -1, lastClock = 0, stage = 0, walk = 0, walks = 0, charged = 0, aimed = 0, wait = 0, afterFire = 0, shotsSeen = 0;
    int sub = 0, thinkTimer = 0;  // sub: candidate of the (weapon, target) pair; thinkTimer: g.timer when the think began
    long debt = 0;                // work done ahead of the per-frame budget
    float top = 0;                // Game::landTop(), scanned up to column topX
    int topX = -1;                // -1: not scanned this turn
    bool moved = false, crateTried = false, closerTried = false;
    Vector3 goal{}, lastBoom{};
    std::vector<Step> path;  // being followed
    size_t pathAt = 0;
    StepRun stepRun;
    std::shared_ptr<struct Search> search;  // destination scoring and A* in progress (ai.cpp)
    std::vector<Vector3> tpos;  // targets: enemy worms, then barrels/mines next to enemies
    std::vector<int> tworm;     // worm aimed at (nearest enemy for object targets)
    std::vector<int> threats;   // enemies whose sweet spot is still to be found
    struct Shot { int team; Vector3 from, at; };
    std::vector<Shot> memory;  // this match's shots: repeats get more accurate (W4M AIPlanMemory)
    RopePlan rope;
    RopeRun run;
    int raceAt = -1, raceTimer = 0, raceWait = 0;  // rope race: next candidate, turn time left when the run starts, ticks until then
    float raceBest = 0;

    void startEval(const Game &g);
    int evalWeapon(const Game &g, int wi, int only = -1, int sub = -1);  // only: one target, sub: one candidate; returns the candidates
    void slice(const Game &g);
    void unit(const Game &g);
    void startSearch(const Game &g, int purpose, Vector3 to);
    void searchStep(const Game &g);
    void takePath(const Game &g, std::vector<Step> &&p);
    bool follow(const Game &g, Input &in);
    void decide(const Game &g);
    void finish(const Game &g);
    Input act(const Game &g);
    Input jet(const Game &g);
    Input retreat(const Game &g);
    Input race(const Game &g);
};

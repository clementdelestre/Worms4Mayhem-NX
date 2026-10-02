#pragma once
#include "sim.h"
#include <memory>

// CPU player: reads the Game and plays Inputs, so it is logged/relayed like a human.
// Level (cfg.teamSetup[team].cpu) 1..5: W4M AIParams.CPU1..CPU5 (aim error, scoring weights, crates, delays, jumps).
// Planning is a state machine sliced over frames by a work budget (W4M 80 cost units per frame); the plan never depends on the slicing.
struct Ai {
    Input think(const Game &g);
    bool striking() const { return mode == Mode::Act && plan.weapon >= 0 && targeted(WEAPONS[plan.weapon].kind) && WEAPONS[plan.weapon].kind != Kind::Homing; }  // seen from the Blimp; its homing never locks

    struct Plan { int weapon = -1, charge = 0, target = -1; float yaw = 0, pitch = 0, score = -1e9f, rank = -1e9f; };  // rank: score + taste
    struct RopePlan { float yaw = 0, pitch = 0; int release = -1; bool reel = false; };  // release -1: walk toward the finish, -2: stay
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
    float node = 0;               // W4M path node spacing for this match (0: not computed)
    std::vector<int64_t> blocked;  // this turn's path-failed nodes
    int repaths = 0, lastPurpose = -1;
    bool pathFailed = false;
    std::vector<std::pair<float, Vector3>> lastGoals;  // the last search's goals, for a repath
    Vector3 lastTarget{};
    bool repath(const Game &g);
    struct Skip { int worm; float effect; };
    std::vector<Skip> skipped;  // W4M skipped-turn memory (worm-select mode)
    bool selecting = false, selDone = false;  // W4M worm-select mode: planning every worm of the team, done this turn
    std::vector<int> selWorms;
    size_t selAt = 0;
    int selBest = -1, selTarget = -1, me = -1;  // me: the worm being planned (-1: the current one)
    float selRank = 0;
    int cur(const Game &g) const { return me >= 0 ? me : g.current; }
    float skipScale(int worm) const;
    void nextSelect(const Game &g);
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
    struct Shot { Vector3 from, at; float effect; };
    std::vector<Shot> memory;  // W4M AIPlanMemory ImproveAccuracy records: repeats get more accurate
    struct Fail { int weapon, worm, target; Vector3 at; int hp; float effect; };
    std::vector<Fail> failed;  // W4M AIPlanMemory failed plans: the target neither moved nor lost hp
    Fail recent{-1};           // the last attack, checked at the next think (CheckPlanResult)
    void regress(const Game &g);
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

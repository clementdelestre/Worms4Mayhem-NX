#pragma once
#include "sim.h"
#include <memory>

// CPU player: reads the Game and plays Inputs, so it is logged/relayed like a human.
// Level (cfg.teamSetup[team].cpu) 1..5: W4M AIParams.CPU1..CPU5 (aim error, scoring weights, crates, delays, jumps).
// Planning is a state machine sliced over frames by a work budget (W4M 80 cost units per frame); the plan never depends on the slicing.
struct Ai {
    Input think(const Game &g);
    static int firstContact(const Game &g, const Projectile &s);  // ticks until a ballistic shot's first contact (land, worm, fuse, water), -1 none in 10 s
    bool striking() const { return mode == Mode::Act && plan.weapon >= 0 && targeted(WEAPONS[plan.weapon].kind); }  // seen from the Blimp

    // rank: score x taste; origin: index in origins (-1: re-aimed where the worm stands); view: the Blimp yaw a strike is called with
    struct Plan { int weapon = -1, charge = 0, target = -1; float yaw = 0, pitch = 0, score = -1e9f, rank = -1e9f; int origin = -1; float view = 0; };
    struct RopePlan { float yaw = 0, pitch = 0; int release = -1; bool reel = false; };  // release -1: walk toward the finish, -2: stay
    struct RopeRun { int t = 0, held = 0, after = -1; bool fired = false, done = true; };
    struct Step { Vector3 to; float yaw = 0; uint8_t move = 0; };  // W4M path move: 0 WALK to `to`, 1 JUMP_FORWARD, 2 JUMP_BACKFLIP along yaw
    struct StepRun { int t = 0; bool air = false, done = false, stuck = false; };  // stuck: a walk held off its node
    // A pathfind request: W4M 0x4942f0 (the whole path, at most `iters` A* iterations, refused past `limit` ticks; -1: no time check)
    // or 0x494950 (`partial`: the best partial path, no time check). Kept to repath a failed step.
    struct Route {
        enum { Crate, Closer, Random, Before, After, Close, Rewalk, Retreat };
        int purpose = -1, iters = 200, limit = -1;
        bool partial = false;
        Vector3 goal{};
    };
    // Where an attack is planned from (W4M CAIPlanAttack +0x4c): a move -> retreat pair's move node (path before and after), or the
    // worm's spot. weight: the plan's +0x58 multiplier; close: the root plan's close-range and Special family (scored x1 where it stands)
    struct Origin {
        Vector3 pos{};
        float yaw = 0, weight = 1;
        std::vector<Step> before, after;
        Route to, back;
        bool path = false, close = false, near = false;  // near: targets within 5 m allowed (a re-aim, not a W4M plan)
    };
    long budget = 20000;  // Terrain::samples per frame: ~0.7 ms on desktop, plus at most one unit (< 2.5 ms)
    struct Walk { static constexpr int STEPS = 300; Vector3 p{}, v{}, best{}; int i = 0; };  // a walker's planned walk, resumed per unit

private:
    enum class Mode { Eval, Search, Walk, Act };
    struct Choice { float score; Route route; };  // route.purpose < 0: the attack plan
    Plan plan;
    Mode mode = Mode::Eval;
    uint32_t salt = 0;  // per turn: seeds the weapon taste
    int worm = -1, lastTimer = -1, lastClock = 0, stage = 0, walks = 0, charged = 0, aimed = 0, wait = 0, afterFire = 0, shotsSeen = 0;
    Walk walk;
    int sub = 0, thinkTimer = 0;  // sub: candidate of the (weapon, target) pair; thinkTimer: g.timer when the think began
    long debt = 0;                // work done ahead of the per-frame budget
    std::shared_ptr<const struct Grid> grid;  // W4M path node grid and jump reach tables for this match
    std::shared_ptr<struct Moves> moves;      // W4M ScoreAllMoveNodes window and node heights for this think (ai.cpp)
    std::vector<int64_t> blocked;  // this turn's path-failed nodes
    int repaths = 0;
    Route walking;  // the route being followed, for a repath
    // moved: W4M 0x9560ba, a path ran since the last shot; noMove: 0x9560f8, too many repaths
    bool pathFailed = false, moved = false, noMove = false, fireAfterWalk = false, retreatTaken = false;
    std::vector<Origin> origins;  // this think's attack origins
    Origin reaim;                 // the shotgun's next shot, where the worm stands
    int origin = 0, built = -1, pairs = 0, pairM = -1, pairR = -1;  // origin under evaluation, the one tpos was built for
    bool pairing = false, pairOk = false;
    float pairScore = 0, pairYaw = 0;
    Vector3 pairAt{};
    std::vector<Step> pairBefore;
    std::vector<Choice> choices;  // ranked plans, tried in order (W4M IsPossible)
    size_t choiceAt = 0;
    Vector3 strikeOff{};          // ShotErrorStrike for the steered bomber
    std::vector<Step> path, afterPath;  // being followed; the retreat queued after the weapon
    Route afterRoute;
    size_t pathAt = 0;
    StepRun stepRun;
    std::shared_ptr<struct Search> search;  // A* in progress (ai.cpp)
    std::vector<Vector3> tpos;  // targets: enemy worms, the ground short of them, their sweet spots
    std::vector<int> tworm;     // worm aimed at
    std::vector<int> threats;   // worms whose threat rating is still to be found
    std::vector<float> rating;  // W4M target threat rating (+0x1c, 0..1) per worm
    std::vector<Vector3> away;  // per worm: away from its worst threat (+0x20), zero if none
    std::vector<float> vals;    // W4M target value (+8, 0x4a9260) per worm, at the think's start
    float norm = 1;             // their mean |value| (0x4ac5e6)
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
    void targetsFor(const Game &g, const Origin &o);
    int evalWeapon(const Game &g, int wi, int only, int sub, const Origin &o, int oi);  // only: one target, sub: one candidate; returns the candidates
    void slice(const Game &g);
    void unit(const Game &g);
    void nextPair(const Game &g);
    void startSearch(const Game &g, const Route &r);
    void searchStep(const Game &g);
    void takePath(const Game &g, std::vector<Step> &&p);
    bool follow(const Game &g, Input &in);
    bool repath(const Game &g);
    void decide(const Game &g);
    void finish(const Game &g, bool again = false);
    Input act(const Game &g);
    Input blimp(const Game &g) const;
    Input retreat(const Game &g);
    Input race(const Game &g);
};

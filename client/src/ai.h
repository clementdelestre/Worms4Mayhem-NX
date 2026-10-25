#pragma once
#include "sim.h"
#include <memory>

// CPU player: reads the Game and plays Inputs, so it is logged/relayed like a human.
// Level (cfg.teamSetup[team].cpu) 1..5: W4M AIParams.CPU1..CPU5 (aim error, scoring weights, crates, delays, jumps).
// Planning is a state machine sliced over frames by a work budget (W4M 80 cost units per frame); the plan never depends on the slicing.
struct Ai {
    Input think(const Game &g);
    static Vector3 firstEvent(const Game &g, const WeaponDef &wd, Vector3 pos, Vector3 vel);  // W4M FindFirstEvent 0x576580: first land, water or fuse-end point of a ballistic shot (no worm)
    static int firstContact(const Game &g, const Projectile &s);  // ticks until a ballistic shot's first contact (land, worm, fuse, water), -1 none in 10 s
    bool striking() const { return mode == Mode::Act && plan.weapon >= 0 && targeted(WEAPONS[plan.weapon].kind); }  // seen from the Blimp

    // rank: score x taste; origin: index in origins; view: the Blimp yaw a strike is called with; at: W4M +0x14, the plan's target point;
    // fuse: W4M SetWeaponFuse (s, 0 none); miss: W4M +0x3c, a best-miss clone
    struct Plan { int weapon = -1, charge = 0, target = -1; float yaw = 0, pitch = 0, score = -1e9f, rank = -1e9f; int origin = -1; float view = 0;
                  Vector3 at{}; int fuse = 0; bool miss = false; };
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
        bool path = false, close = false;
    };
    long budget = 20000;  // Terrain::samples per frame: ~0.7 ms on desktop, plus at most one unit (< 2.5 ms)
    struct Walk { static constexpr int STEPS = 300; Vector3 p{}, v{}, best{}; int i = 0; };  // a walker's planned walk, resumed per unit
    // W4M AIPlanUtilities' TargetParabola (0x959970) and its outputs, in W4M units (20 per m, origin Terrain::origin) and ms; kept across thinks
    struct Para { Vector3 S{}, A{}, T{}, v10{}, v98{}, hit{}; float t1 = 0, t2 = 0, lo = 0, hi = 0; bool known = false; };

private:
    enum class Mode { Eval, Search, Walk, Act };
    // W4M c_plstActivePlans entry: a move route, a possible attack (plan), a projectile plan whose solve (IsPossible) runs when popped
    // (lazy), or an impossible plan whose best-miss clone (plan) joins the list when popped (spawn)
    struct Choice { float score; Route route; Plan plan; bool lazy = false, spawn = false; };
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
    int origin = 0, built = -1, pairs = 0, pairM = -1, pairR = -1;  // origin under evaluation, the one tpos was built for
    bool pairing = false, pairOk = false;
    float pairScore = 0, pairYaw = 0;
    Vector3 pairAt{};
    std::vector<Step> pairBefore;
    std::vector<Choice> pending;  // this think's projectile plans and best-miss spawns, for the list
    std::vector<Choice> choices;  // ranked plans, tried in order (W4M IsPossible)
    size_t choiceAt = 0;
    bool deciding = false;        // popping choices, one IsPossible per unit
    Para para;
    Vector3 strikeOff{};          // ShotErrorStrike for the steered bomber
    std::vector<Step> path, afterPath;  // being followed; the retreat queued after the weapon
    Route afterRoute;
    size_t pathAt = 0;
    StepRun stepRun;
    std::shared_ptr<struct Search> search;  // A* in progress (ai.cpp)
    std::vector<Vector3> tpos;  // targets: enemy worms, the ground short of them, their sweet spots
    std::vector<int> tworm;     // worm aimed at
    std::vector<int> threats;   // targets whose threat rating is still to be found
    std::vector<float> rating;  // W4M target threat rating (+0x1c, 0..1) per worm
    std::vector<Vector3> away;  // per worm: away from its worst threat (+0x20), zero if none
    std::vector<bool> listed;   // W4M c_plstTargets membership (0x4ab730): a target of its side not too far, not inside an AI trigger
    std::vector<float> vals;    // W4M target value (+8, 0x4a9260) per worm, at the think's start
    float norm = 0, threatNorm = 0;  // their mean |value| (0x4ac5e6), and the previous think's, which the threat ratings use (0x959868)
    struct Shot { Vector3 from, at; float effect; };
    std::vector<Shot> memory;  // W4M AIPlanMemory ImproveAccuracy records: repeats get more accurate
    struct Fail { int weapon, worm, target; Vector3 at; int hp; float effect; };
    std::vector<Fail> failed;  // W4M AIPlanMemory failed plans: the target neither moved nor lost hp
    Fail recent{-1};           // the last attack, checked at the next think (CheckPlanResult)
    void regress(const Game &g);

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
    bool solve(const Game &g, Plan &p);  // W4M projectile IsPossible; false may queue its best-miss clone
    float projScore(const Game &g, const Plan &p) const;
    float rankOf(const Game &g, int wi, int target, int origin, float &score) const;  // score: x weight and memory on return
    void insert(const Choice &c);
    void finish(const Game &g);
    Input act(const Game &g);
    Input blimp(const Game &g) const;
    Input retreat(const Game &g);
};

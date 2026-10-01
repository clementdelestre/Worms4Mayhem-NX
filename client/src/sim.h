#pragma once
#include "terrain.h"
#include <cstdint>
#include <string>
#include <vector>

// Quantized per-tick input of the active player: the only thing sent over the network.
struct Input {
    int8_t turn = 0, walk = 0, aim = 0;
    uint8_t buttons = 0;
    enum : uint8_t { FIRE = 1, JUMP = 2, NEXT_WEAPON = 4, ABOUT_FACE = 8, FUSE_UP = 16, FUSE_DOWN = 32 };  // ABOUT_FACE: stick pulled behind the worm, it turns round at once
    // FUSE_UP/DOWN: W4M FuseUp, the timer of user-fuse weapons (WeaponDef::userFuse) in 1 s steps
};

// Shell covers bazooka/grenades/clusters; speed = rope length or jetpack thrust, fuse = sheep timeout or jetpack fuel.
// Melee: speed = knock along the aim, bounce = upward knock, fuse = attacker's leap. Sentry: radius = range, fuse = reload.
// Airstrike with fuse > 0: W4M controlled bomber (Bovine Blitz), flies fuse s at speed, FIRE drops one of `clusters` payloads.
enum class Kind : uint8_t { Shell, Sheep, Airstrike, Donkey, Shotgun, Rope, Jetpack, Teleport,
                            SuperSheep, OldWoman, Melee, Homing, Mine, Scouser, Sentry, Abduction, Flood,
                            Parachute, SkipGo, Surrender, ChangeWorm, Armour };
inline bool powered(Kind k) { return k == Kind::Shell || k == Kind::Homing; }  // hold FIRE to charge, release to fire
inline bool utility(Kind k) { return k == Kind::Rope || k == Kind::Jetpack || k == Kind::Teleport || k == Kind::Parachute || k == Kind::ChangeWorm || k == Kind::Armour; }  // utility-crate pool
// W4M CanBeUsedWhenTailNailed: no movement tools, animals or melee for a nailed worm
inline bool nailUsable(Kind k) { return !(utility(k) && k != Kind::ChangeWorm) && k != Kind::Sheep && k != Kind::SuperSheep && k != Kind::OldWoman && k != Kind::Scouser && k != Kind::Melee; }

struct WeaponDef {
    std::string name;
    Kind kind;
    float radius, damage, speed, fuse, bounce;  // fuse 0 = explodes on impact
    float cradius, cdamage;                     // cluster bomblets
    int count, clusters, shots;                 // count: ammo per team (-1 = infinite); clusters: bomblets/missiles/smashes
    bool wind;
    int weight = 0;  // crate_weight: relative odds in weapon crates (0 = never)
    float poison = 0;  // hp lost per turn by worms caught in the blast (health crate cures)
    std::string model, icon;  // Weapon Factory: projectile model, icon weapon name (cosmetic, "" = default)
    bool userFuse = false;    // "user_fuse": the thrower picks 1..5 s (W4M grenade, cluster, banana), fuse = default
    bool restFuse = false;    // "rest_fuse": the fuse only burns once the shell has stopped (W4M Holy Hand Grenade)
    bool pins = false;        // "pins": a melee that nails its victim in the ground (W4M Tail Nail)
    bool walks = false;       // "walks": a super sheep that walks first, FIRE takes off (W4M "launch, transform and detonate")
};
// Set down at the worm's feet (dynamite): the worm retreats while the fuse burns.
inline bool dropped(const WeaponDef &d) { return d.kind == Kind::Shell && d.fuse > 0 && d.speed < 5; }
extern std::vector<WeaponDef> WEAPONS;  // built-in fallback until loadWeapons() succeeds, then + GameConfig::custom
bool loadWeapons(const char *path);
bool loadCustomWeapons(const char *path, std::vector<WeaponDef> &out);  // same JSON as weapons.json
bool saveCustomWeapons(const char *path, const std::vector<WeaponDef> &list);

struct Worm {
    Vector3 pos, vel;
    float yaw, pitch;
    int hp, team;
    bool alive, grounded;
    int poison = 0;  // hp lost at each turn start, never below 1
    int counted = 0;  // hp its label shows: Settle counts it toward hp, nearby worms together (W4M)
    bool nailed = false;  // Tail Nail: can't walk, jump or use tools, animals, melee; blasting the ground frees it
    bool armour = false;  // W4M Armour: blasts and bullets hurt it ARMOUR %, knock it back 40 % (rest of the match)
};
bool meleeHits(const Worm &a, Vector3 p, const WeaponDef &wd);  // p inside a's melee hit box (shared with the AI)

struct Projectile {
    Vector3 pos, vel;
    int weapon;
    float fuse;
    bool child;  // cluster bomblet or airstrike missile: explodes on impact, never splits
    int hits;    // explosions left before it disappears (donkey)
    Vector3 aim{};  // homing target
    int stage = 0;   // walks-first super sheep: 1 once airborne; scouser: 1 once inflated
    int prey = -1;   // scouser: the worm it carries; old woman: the last worm she robbed
};

// Battlefield object. Crate: weapon = contents (-1 = health). Mine: fuse < 0 idle, else counting down.
// Target: mission bullseye, floats until an explosion or a shot reaches it.
struct Object {
    enum Type : uint8_t { Crate, Mine, Barrel, Sentry, Target } type;
    Vector3 pos, vel;
    int weapon;
    float fuse;
    bool falling;  // crate under parachute
    bool dead;     // hit by an explosion: detonates (barrel, weapon crate) or vanishes next step
    int team = -1;  // sentry owner (weapon = its WEAPONS index, fuse = reload left)
    int tag = -1;   // mission object index; tagged crates and targets are pinned in place, crates can't be blown up
    bool dud = false;  // mine whose fuse fizzled (W4M Mine.DudProbability): inert for good
    int courtesy = 0;  // mine: ticks before a worm can arm it (W4M ArmingCourtesyTime)
};

// Things that happened this tick, for audio/fx; not part of the checksum. worm/weapon = -1 when not applicable.
struct GameEvent {
    enum Kind : uint8_t { Boom, BigBoom, Fire, Bounce, Splash, Death, Hurt, Jump, TurnStart, GameOver, CrateDrop, Collect, MineArm, Hallelujah, CrateLand } kind;
    Vector3 pos;
    int worm, weapon;
};

// Match options, identical on every client (sent in the network Start message).
enum Rule : uint32_t {
    RULE_KING = 1,        // one king per team: king dies => team out
    RULE_HIGHLANDER = 2,  // random weapon each turn, killer inherits the victim's ammo
    RULE_VAMPIRE = 4,     // attacker heals a share of damage dealt
    RULE_KARMA = 8,       // attacker takes a share of damage dealt
    RULE_LOW_GRAVITY = 16,
    RULE_ROPE_RACE = 32,  // rope only, first to reach the map's finish wins
    RULE_SUDDEN_DEATH = 64,  // once the scheme's round time is up (see Scheme::sdType)
};
// W4M "Game Style". All bytes, no padding: sent and checksummed as raw bytes, so only ever append fields.
struct Scheme {
    uint8_t turnTime = 45, retreatTime = 3, hotSeat = 5;  // seconds; hot seat: pause before a turn, any input skips it
    uint8_t roundTime = 20;                                // minutes of play before sudden death
    uint8_t health = 100;                                  // worm start energy
    uint8_t crateChance = 40;                              // % per turn
    uint8_t weaponShare = 30, healthShare = 20, utilityShare = 30, crateHealth = 25;  // crate odds; hp in a health crate
    uint8_t mines = 5, barrels = 4, mineFuse = 3;          // fuse seconds 0..5, FUSE_RANDOM: 1..5 s per mine
    uint8_t sdType = 0;                                    // SD_*
    uint8_t fallDamage = 1, wind = 2;                      // wind 0..3: none, low, medium, high
    uint8_t weapons = 0;                                   // SET_*
    enum : uint8_t { FUSE_RANDOM = 6, SD_BOTH = 0, SD_WATER, SD_ONE_HP, SET_DEFAULT = 0, SET_BNG, SET_CRATES, SET_UNLIMITED };
};
static_assert(sizeof(Scheme) == 17, "Scheme must stay plain bytes");
// W4M Wormpot modes (FETXT.WPotName.*), one bit each; bits [first, last) of WORMPOT_REELS form one slot reel.
enum Wormpot : uint32_t {
    WP_DOUBLE_DAMAGE = 1, WP_SUPER_EXPLOSIVES = 2, WP_SUPER_ANIMALS = 4, WP_WIND_ALL = 8, WP_WORMS_DROWN = 16,
    WP_QUICK_WALK = 32, WP_SLIPPY = 64, WP_STICKY = 128, WP_LOW_GRAVITY = 256, WP_NO_JUMPING = 512, WP_MAX_FALL = 1024,
    WP_CRATE_SHOWER = 2048, WP_CRATE_DROPS = 4096, WP_MAX_HEALTH = 8192, WP_GOLIATH = 16384, WP_ONE_SHOT = 32768,
    WP_VAMPIRE = 65536, WP_VITAL_WORM = 131072,
};
struct WormpotMode { const char *name, *help; };
extern const WormpotMode WORMPOT_MODES[18];  // [bit index]
extern const int WORMPOT_REELS[4];          // reel r = bits WORMPOT_REELS[r] .. WORMPOT_REELS[r + 1] - 1
struct SchemePreset { const char *name; Scheme s; };
extern const std::vector<SchemePreset> SCHEMES;  // [0] = Standard; values from W4M Data/Tweak/LOCAL.XOM
struct GameConfig {
    uint32_t seed = 0;
    int teams = 2, wormsPerTeam = 3;
    std::string map;     // romfs maps/<map>.json; empty = procedural island
    uint32_t rules = 0;  // Rule flags
    struct Team { std::string name; uint8_t cpu = 0, voice = 0, hat = 0; };  // cpu: 0 = human, 1..3 = AI level
    std::vector<Team> teamSetup;  // per team; may be shorter than teams (defaults apply)
    Scheme scheme;
    uint32_t wormpot = 0;            // Wormpot flags
    std::vector<WeaponDef> custom;   // host's Weapon Factory weapons, appended to the loaded table at start()
    const struct MissionSpec *mission = nullptr;  // single-player mission (mission.h), owned by the caller
};

// Mission progress, driven by mission.cpp (checksummed). result: 0 running, 1 success, -1 failure.
struct MissionRun {
    int result = 0, ticks = 0, collected = 0, destroyed = 0, turns = 0;
    std::vector<uint8_t> state;  // per spec object: 0 waiting (sequence), 1 placed, 2 collected / destroyed / lost
    std::vector<uint8_t> met;    // per objective: latched once met
};

enum class Phase { Aim, Flying, Retreat, Settle, GameOver };

// Deterministic simulation: same seed + same inputs => same state on every client.
struct Game {
    static constexpr float DT = 1.0f / 60, R = 0.5f, BODY_R = 0.3f;  // BODY_R: the worm mesh's half width
    static constexpr float HOMING_LOCK = 1.25f, HOMING_TIME = 5;  // W4M: flies on, homes 5 s, then falls (WEAPTWK 1250/5000 ms)
    static constexpr int JUMP_WINDOW = 12;  // ticks a jump waits for a second press (backflip)
    static constexpr float MINE_DUD = 0.1f;  // W4M Mine.DudProbability
    static constexpr float MINE_ARM = 2.25f;  // W4M Landmine ArmingRadius 45: any worm this close arms it
    static constexpr int MINE_COURTESY = 150;  // W4M ArmingCourtesyTime 2500 ms: a laid mine ignores worms that long
    static constexpr int ARMOUR = 25;        // W4M Armour.ProtectionPercentage: share of the damage still taken
    static constexpr int ROPE_SHOTS = 5;     // W4M Ninja.NumShots: rope launches per turn
    static constexpr float SHEEP_WALK = 5, SHEEP_STEP = 4, SHEEP_TAKEOFF = 0.6f;  // walks-first super sheep: walk fuse s, m/s, take-off pitch
    static constexpr float SCOUSER_FLOAT = 5;  // s a swallowed worm is carried before the pop
    static constexpr float STRIKE_GAP = 2.5f;  // m between air strike bombs: BlitzDuration 2 s x GroundSpeed 7.5 m/s / NumBombs 6
    static constexpr int STRIKE_TICKS = 20;    // one bomb every BlitzDuration / NumBombs = 333 ms
    static constexpr float BOMBER_HEIGHT = 15, BOMBER_LEAD = 20, BOMBER_GAP = 0.8f, COW_CHUTE = 5;  // m, m, s (SuperBomber.DelayBetweenBombs), m/s
    static constexpr float GAS_LIFE = 8, GAS_RADIUS = 5;  // W4M WXP_GasCloud: one 8 s particle, collision radius 100 units

    Terrain terrain;
    std::vector<Worm> worms;
    std::vector<Projectile> shots;
    std::vector<Object> objects;
    struct Gas { Vector3 pos; float life, poison; };
    std::vector<Gas> gas;  // poison clouds: drift with the wind, poison every worm inside
    std::vector<GameEvent> events;  // cleared at the start of each step()
    std::vector<int> nextWorm;
    std::vector<std::vector<int>> ammo;  // [team][weapon]
    std::vector<int> lastHitTeam;        // per worm: team index of last attacker, -1 none (highlander)
    std::vector<int> picked;             // per team: weapon in hand when its last turn ended, reselected next turn (W4M)
    int teams = 2, perTeam = 1, current = 0, weapon = 0, winner = -1, timer = 0;
    int clock = 0;      // ticks played (hot seat excluded): sudden death after cfg.scheme.roundTime
    int hotSeat = 0;    // ticks left before the turn clock starts
    int jumpDelay = 0;  // forward jump pending; a second JUMP turns it into a backflip
    bool selfHurt = false;  // the active worm took damage: its turn ends (W4M)
    float power = 0, wind = 0;
    bool roped = false, jetting = false;  // active utility: keeps the turn going
    bool chute = false;                   // parachute open until the turn ends
    Vector3 anchor{};
    float ropeLen = 0, fuel = 0, ropeMax = 0, thrust = 0;  // max/thrust: of the tool in use, the hand may hold a weapon
    std::vector<int> fuses;  // per team: seconds set for userFuse weapons (W4M default 3)
    int shotsLeft = 0;
    int ropeShots = 0;  // rope launches this turn
    Phase phase = Phase::Aim;
    uint8_t prevButtons = 0;
    uint32_t rng = 1;
    float water = Terrain::WATER;    // rises in sudden death; terrain.* keeps using the constant
    bool suddenDeath = false;
    Vector3 raceFinish{};  // rope race: terrain.finish, or a deterministic fallback
    std::vector<uint8_t> idle;  // per team: never takes a turn (mission captives)
    MissionRun run;
    // Settle count, per group of nearby hurt worms: camera travel, 40 hp/s (at most 1.5 s), linger, then the dead blow up
    // THROES: the W4M Death5 scene (3 s) before a blast; DEATH: after it, banner LIFE (docs/death-sequence.md)
    static constexpr int COUNT_TRAVEL = 42, COUNT_LINGER = 30, COUNT_THROES = 180, COUNT_DEATH = 180, COUNT_FLOAT = 60;
    static constexpr float COUNT_SPAN = 18;  // m from the group's first worm
    std::vector<int> countGroup;  // worms counting in Settle; countT: ticks into the group's count
    int countT = 0;
    int countTicks(int i) const {  // drowned: no count, floats instead (W4M kWPS_DrownFloat)
        return !worms[i].alive ? COUNT_FLOAT : std::min(90, std::max(1, std::abs(std::max(0, worms[i].hp) - worms[i].counted) * 3 / 2));
    }
    int countBoom() const { int b = 0; for (int i : countGroup) b = std::max(b, COUNT_TRAVEL + countTicks(i) + COUNT_LINGER); return b; }  // the dead blow up
    int deathLead(int i) const { return worms[i].pos.y < water ? 45 : COUNT_THROES; }  // drowned: floated already
    int blastAt(int i) const {  // countT of dead worm i's blast in the queue; -1: when the queue is over
        int t = countBoom() - 45;
        for (int j : countGroup)
            if (worms[j].hp <= 0) { t += deathLead(j); if (j == i) return t; t += COUNT_DEATH; }
        return t;
    }
    int dying() const {  // the dead worm the camera shows, from its lead before its blast to COUNT_DEATH after
        for (int i : countGroup)
            if (worms[i].hp <= 0 && countT > blastAt(i) - deathLead(i) && countT <= blastAt(i) + COUNT_DEATH) return i;
        return -1;
    }

    GameConfig cfg;

    void start(const GameConfig &cfg);
    void step(const Input &in);
    uint32_t checksum() const;
    Vector3 aimDir(const Worm &w) const;
    Vector3 target() const;  // terrain point under the aim reticle
    int landHold = 0;  // ticks the turn still waits once a dropped crate has landed
    bool dropping() const { if (landHold > 0) return true; for (const Object &o : objects) if (o.type == Object::Crate && o.falling) return true; return false; }  // crate under its chute
    float fuseOf(const WeaponDef &wd) const { return wd.userFuse ? fuses[worms[current].team] : wd.fuse; }

private:
    float rand01();
    float gravity() const;
    void beginTurn(int team);
    void nextWeapon(int team);
    void use(Worm &w);
    void emit(GameEvent::Kind k, Vector3 p, int worm = -1, int weapon = -1) { events.push_back({k, p, worm, weapon}); }
    void stepWorm(Worm &w);
    void drown(Worm &w);
    void stepRope(Worm &w);
    void stepShots(const Input &in, bool detonate);
    void explode(Vector3 p, float radius, float damage, float poison = 0, float push = 1);
    void steal(const Worm &victim);  // old woman ammo theft
    void hurt(Worm &w, int dmg);  // applies the vampire/karma/highlander rules for the active worm
    bool dropPoint(Vector3 &out);
    bool addObject(Object::Type t, float lift);
    void stepObjects();
};
void missionStart(Game &g);  // mission.cpp: worms, ammo, objects from cfg.mission
void missionStep(Game &g);   // mission.cpp: sequences, objectives, result

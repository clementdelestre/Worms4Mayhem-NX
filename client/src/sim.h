#pragma once
#include "terrain.h"
#include <cstdint>
#include <string>
#include <vector>

// Quantized per-tick input of the active player: the only thing sent over the network.
struct Input {
    int8_t turn = 0, walk = 0, aim = 0;
    uint8_t buttons = 0;
    enum : uint8_t { FIRE = 1, JUMP = 2, NEXT_WEAPON = 4 };
};

// Shell covers bazooka/grenades/clusters; speed = rope length or jetpack thrust, fuse = sheep timeout or jetpack fuel.
// Melee: speed = knock along the aim, bounce = upward knock, fuse = attacker's leap. Sentry: radius = range, fuse = reload.
enum class Kind : uint8_t { Shell, Sheep, Airstrike, Donkey, Shotgun, Rope, Jetpack, Teleport,
                            SuperSheep, OldWoman, Melee, Homing, Mine, Scouser, Sentry, Abduction, Flood,
                            Parachute, SkipGo, Surrender, ChangeWorm };
inline bool powered(Kind k) { return k == Kind::Shell || k == Kind::Homing; }  // hold FIRE to charge, release to fire

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
};
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
};

struct Projectile {
    Vector3 pos, vel;
    int weapon;
    float fuse;
    bool child;  // cluster bomblet or airstrike missile: explodes on impact, never splits
    int hits;    // explosions left before it disappears (donkey)
    Vector3 aim{};  // homing target
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
};

// Things that happened this tick, for audio/fx; not part of the checksum. worm/weapon = -1 when not applicable.
struct GameEvent {
    enum Kind : uint8_t { Boom, BigBoom, Fire, Bounce, Splash, Death, Hurt, Jump, TurnStart, GameOver, CrateDrop, Collect, MineArm } kind;
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
    uint8_t mines = 5, barrels = 4, mineFuse = 3;          // fuse seconds 0..5, FUSE_RANDOM: 0..5 s per mine
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
    static constexpr float DT = 1.0f / 60, R = 0.5f;
    static constexpr int JUMP_WINDOW = 12;  // ticks a jump waits for a second press (backflip)

    Terrain terrain;
    std::vector<Worm> worms;
    std::vector<Projectile> shots;
    std::vector<Object> objects;
    std::vector<GameEvent> events;  // cleared at the start of each step()
    std::vector<int> nextWorm;
    std::vector<std::vector<int>> ammo;  // [team][weapon]
    std::vector<int> lastHitTeam;        // per worm: team index of last attacker, -1 none (highlander)
    int teams = 2, perTeam = 1, current = 0, weapon = 0, winner = -1, timer = 0;
    int clock = 0;      // ticks played (hot seat excluded): sudden death after cfg.scheme.roundTime
    int hotSeat = 0;    // ticks left before the turn clock starts
    int jumpDelay = 0;  // forward jump pending; a second JUMP turns it into a backflip
    bool selfHurt = false;  // the active worm took damage: its turn ends (W4M)
    float power = 0, wind = 0;
    bool roped = false, jetting = false;  // active utility: keeps the turn going
    bool chute = false;                   // parachute open until the turn ends
    Vector3 anchor{};
    float ropeLen = 0, fuel = 0;
    int shotsLeft = 0;
    Phase phase = Phase::Aim;
    uint8_t prevButtons = 0;
    uint32_t rng = 1;
    float water = Terrain::WATER;    // rises in sudden death; terrain.* keeps using the constant
    bool suddenDeath = false;
    Vector3 raceFinish{};  // rope race: terrain.finish, or a deterministic fallback
    std::vector<uint8_t> idle;  // per team: never takes a turn (mission captives)
    MissionRun run;

    GameConfig cfg;

    void start(const GameConfig &cfg);
    void step(const Input &in);
    uint32_t checksum() const;
    Vector3 aimDir(const Worm &w) const;
    Vector3 target() const;  // terrain point under the aim reticle

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
    void hurt(Worm &w, int dmg);  // applies the vampire/karma/highlander rules for the active worm
    bool dropPoint(Vector3 &out);
    bool addObject(Object::Type t, float lift);
    void stepObjects();
};
void missionStart(Game &g);  // mission.cpp: worms, ammo, objects from cfg.mission
void missionStep(Game &g);   // mission.cpp: sequences, objectives, result

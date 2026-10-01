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
enum class Kind : uint8_t { Shell, Sheep, Airstrike, Donkey, Shotgun, Rope, Jetpack, Teleport };

struct WeaponDef {
    std::string name;
    Kind kind;
    float radius, damage, speed, fuse, bounce;  // fuse 0 = explodes on impact
    float cradius, cdamage;                     // cluster bomblets
    int count, clusters, shots;                 // count: ammo per team (-1 = infinite); clusters: bomblets/missiles/smashes
    bool wind;
    int weight = 0;  // crate_weight: relative odds in weapon crates (0 = never)
};
extern std::vector<WeaponDef> WEAPONS;  // built-in fallback until loadWeapons() succeeds
bool loadWeapons(const char *path);

struct Worm {
    Vector3 pos, vel;
    float yaw, pitch;
    int hp, team;
    bool alive, grounded;
};

struct Projectile {
    Vector3 pos, vel;
    int weapon;
    float fuse;
    bool child;  // cluster bomblet or airstrike missile: explodes on impact, never splits
    int hits;    // explosions left before it disappears (donkey)
};

// Battlefield object. Crate: weapon = contents (-1 = health). Mine: fuse < 0 idle, else counting down.
struct Object {
    enum Type : uint8_t { Crate, Mine, Barrel } type;
    Vector3 pos, vel;
    int weapon;
    float fuse;
    bool falling;  // crate under parachute
    bool dead;     // hit by an explosion: detonates (barrel, weapon crate) or vanishes next step
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
    RULE_SUDDEN_DEATH = 64,  // after SD_TURNS turns: everyone drops to 1 hp, water rises each turn
};
struct GameConfig {
    uint32_t seed = 0;
    int teams = 2, wormsPerTeam = 3;
    std::string map;     // romfs maps/<map>.json; empty = procedural island
    uint32_t rules = 0;  // Rule flags
};

enum class Phase { Aim, Flying, Retreat, Settle, GameOver };

// Deterministic simulation: same seed + same inputs => same state on every client.
struct Game {
    static constexpr float DT = 1.0f / 60, R = 0.5f;
    static constexpr int TURN_TICKS = 45 * 60, SD_TURNS = 8;  // sudden death after SD_TURNS*teams individual turns
    static constexpr int MINES = 5, BARRELS = 4;
    static constexpr float CRATE_CHANCE = 0.5f, HEALTH_CHANCE = 0.3f, MINE_FUSE = 3;  // per turn; share of health crates

    Terrain terrain;
    std::vector<Worm> worms;
    std::vector<Projectile> shots;
    std::vector<Object> objects;
    std::vector<GameEvent> events;  // cleared at the start of each step()
    std::vector<int> nextWorm;
    std::vector<std::vector<int>> ammo;  // [team][weapon]
    std::vector<int> lastHitTeam;        // per worm: team index of last attacker, -1 none (highlander)
    int teams = 2, perTeam = 1, current = 0, weapon = 0, winner = -1, timer = 0, turnCount = 0;
    float power = 0, wind = 0;
    bool roped = false, jetting = false;  // active utility: keeps the turn going
    Vector3 anchor{};
    float ropeLen = 0, fuel = 0;
    int shotsLeft = 0;
    Phase phase = Phase::Aim;
    uint8_t prevButtons = 0;
    uint32_t rng = 1;
    float water = Terrain::WATER;    // rises in sudden death; terrain.* keeps using the constant
    bool suddenDeath = false;
    Vector3 raceFinish{};  // rope race: terrain.finish, or a deterministic fallback

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
    void stepShots(bool detonate);
    void explode(Vector3 p, float radius, float damage);
    bool dropPoint(Vector3 &out);
    bool addObject(Object::Type t, float lift);
    void stepObjects();
};

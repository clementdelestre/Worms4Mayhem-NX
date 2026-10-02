#pragma once
#include "terrain.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Quantized per-tick input of the active player: the only thing sent over the network.
struct Input {
    int8_t turn = 0, walk = 0, aim = 0;
    uint8_t buttons = 0;
    enum : uint8_t { FIRE = 1, JUMP = 2, NEXT_WEAPON = 4, HEADING = 8, FUSE_UP = 16, FUSE_DOWN = 32, TARGET = 64, PITCH = 128 };  // HEADING: turn is the wanted yaw, PI * turn / 128 (W4M walk)
    // FUSE_UP/DOWN: W4M FuseUp, the timer of user-fuse weapons (WeaponDef::userFuse) in 1 s steps
    // TARGET: W4M Blimp view; turn yaws the camera, walk / aim move its focus (Game::cursor) forward / right, the worm stays put.
    // PITCH (with TARGET): this tick's aim tilts the camera instead of moving it sideways (the client alternates the two)
};

// Shell covers bazooka/grenades/clusters; speed = rope length or jetpack thrust, fuse = sheep timeout or jetpack fuel.
// Melee: speed = knock along the aim, bounce = upward knock, fuse = attacker's leap. Sentry: radius = range, fuse = reload.
// Airstrike with fuse > 0: W4M controlled bomber (Bovine Blitz), flies fuse s at speed, FIRE drops one of `clusters` payloads.
enum class Kind : uint8_t { Shell, Sheep, Airstrike, Donkey, Shotgun, Rope, Jetpack, Teleport,
                            SuperSheep, OldWoman, Melee, Homing, Mine, Scouser, Sentry, Abduction, Flood,
                            Parachute, SkipGo, Surrender, ChangeWorm, Armour,
                            Girder, Binoculars, Bubble, Icarus, DoubleDamage, CrateSpy };  // W4M utilities 34, 43, 42, 41, 44, 46
inline bool collected(Kind k) { return k == Kind::DoubleDamage || k == Kind::CrateSpy || k == Kind::Armour; }  // W4M crate collect 0x5c9800 (44, 46, 47): applied at once, never in the inventory
inline bool targeted(Kind k) { return k == Kind::Airstrike || k == Kind::Donkey || k == Kind::Abduction || k == Kind::Teleport || k == Kind::Homing; }  // W4M IsTargetingWeapon: blimp view
inline bool powered(Kind k) { return k == Kind::Shell || k == Kind::Homing; }  // hold FIRE to charge, release to fire
inline bool utility(Kind k) { return k == Kind::Rope || k == Kind::Jetpack || k == Kind::Teleport || k == Kind::Parachute || k == Kind::ChangeWorm || k == Kind::Armour ||
                                    k >= Kind::Girder; }  // utility-crate pool
// W4M CanBeUsedWhenTailNailed: no movement tools, animals or melee for a nailed worm
inline bool nailUsable(Kind k) { return !(utility(k) && k != Kind::ChangeWorm && k != Kind::Girder && k != Kind::Binoculars && k != Kind::Bubble) && k != Kind::Sheep && k != Kind::SuperSheep && k != Kind::OldWoman && k != Kind::Scouser && k != Kind::Melee; }

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
    // W4M WEAPTWK WormDamageRadius, ImpulseMagnitude, ImpulseRadius, -ImpulseOffset in m, m/s; -1: derived from radius
    float blast[4] = {-1, -1, -1, -1}, cblast[4] = {-1, -1, -1, -1};  // "reach", "push", "push_reach", "push_depth"; cluster_*
    float lift = 0;  // "lift": W4M payload Radius, the blast centre above the point that hit the ground (donkey 3.6 m)
    float grav = 1;  // "gravity": share of Gravity; W4M IsLowGravity = Gravity.Slow 0.6, IsAffectedByGravity 0 = 0
    float base = -1;  // "min_speed": W4M BasePower in m/s, speed = BasePower + MaxPower; -1: 0.15 x speed
    int retreat = -1, postLaunch = 0;  // "retreat", "post_launch": W4M RetreatTimeOverride (-1: the scheme's LandTime), PostLaunchDelay, ms
};
// W4M ExplosionMessage: crater (LandDamageRadius), worm damage reach and max, knockback m/s, its reach and its epicentre depth.
struct Blast { float crater, reach, damage, push, pushReach, pushDepth; };
Blast blastOf(const WeaponDef &d, bool child);
// Set down at the worm's feet (dynamite): the worm retreats while the fuse burns.
// W4M IsPoweredWeapon: BasePower + ShotPower x MaxPower, ShotPower 0..1 over Tweaks.MaxPowerUpTime 1500 ms
inline float launchSpeed(const WeaponDef &d, float power) { return d.base >= 0 ? d.base + (d.speed - d.base) * power : d.speed * fmaxf(power, 0.15f); }
inline bool dropped(const WeaponDef &d) { return d.kind == Kind::Shell && d.fuse > 0 && d.speed < 5; }
// W4M WeaponSelected 0x565d30: with rope, jetpack or parachute out, only Dynamite, Landmine and Sheep (payload case 0) keep it
inline bool toolDrop(const WeaponDef &d) { return dropped(d) || d.kind == Kind::Mine || d.kind == Kind::Sheep; }
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
    bool armour = false;  // W4M Armour: blasts hurt it ARMOUR %, knock it back half as far (rest of its life); bullets ignore it
};
bool meleeHits(const Worm &a, Vector3 p, const WeaponDef &wd);  // p inside a's melee hit box (shared with the AI)
// Ground walk and wall clearance, shared with the AI's prediction. walkStep: true when it walked off a ledge.
bool walkStep(const Terrain &t, Vector3 &pos, float yaw, float dist);
void clearWalls(const Terrain &t, Vector3 &pos);
bool fits(const Terrain &t, Vector3 from, Vector3 to);  // the upper body at `to` is out of land, or no deeper than at `from`
// Free flight, shared with the AI: a tick's move cut into sub-steps of at most VOX/2, so nothing skips thin land.
int substeps(Vector3 vel);
// W4M 0x585a29 launches from the worm's eye (feet + Worm.EyeLevelOffset 15 units): spawn, pulled back to the last free point eye → spawn
Vector3 muzzle(const Terrain &t, Vector3 pos, Vector3 spawn);
// W4M LogicalLaunchZ/YOffset from the eye (WEAPTWK): dynamite 13/-10, landmine 10/-10, sheep 5, old woman 7, scouser 10 units;
// the others: ours, ahead along the aim (impact payloads would hit their own worm from the eye: W4M's exclusion not traced)
Vector3 launchPoint(const WeaponDef &d, Vector3 pos, float yaw, Vector3 dir);
// W4M payloads touch land by their centre point (0x574e90); at rest the mesh is drawn r along the land normal (0x5761f0)
Vector3 restOn(const Terrain &t, Vector3 p, float r);
float flyBody(const Terrain &t, Vector3 &pos, Vector3 &vel, float e = 0.3f);  // worm body, walls bounce at e (W4M Rebound 0x5acea0: 0.3); returns the landing speed
// One worm tick (ground slide, fall, flight), shared with the AI; returns the landing speed, 0 if none.
float wormBody(const Terrain &t, Vector3 &pos, Vector3 &vel, bool &grounded, float gravity, uint32_t wormpot, float e = 0.3f);
void walkerStep(const Terrain &t, Vector3 &pos, Vector3 &vel, float gravity);  // sheep, old woman, scouser on foot

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
    bool hooked = false;  // W4M kRopeModeAttachedToObject: on the active worm's rope, at most Game::ropeLen away
};

// Things that happened this tick, for audio/fx; not part of the checksum. worm/weapon = -1 when not applicable.
struct GameEvent {
    enum Kind : uint8_t { Boom, BigBoom, Fire, Bounce, Splash, Death, Hurt, Jump, TurnStart, GameOver, CrateDrop, Collect, MineArm, Hallelujah, CrateLand,
                          Launch } kind;  // Launch: a bomber dropped a payload (W4M LaunchSfx: BombWhistle, CowFall)
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
    RULE_NO_DELAYS = 128,    // test: the scheme's weapon delays (W4M SchemeData) are ignored
};
// W4M "Game Style". All bytes, no padding: sent and checksummed as raw bytes, so only ever append fields.
struct Scheme {
    uint8_t turnTime = 45, retreatTime = 5, hotSeat = 10;  // s (W4M ms / 1000; LandTime, HotSeat): any input skips the hot seat
    uint8_t roundTime = 20;                                // minutes of play before sudden death
    uint8_t health = 100;                                  // worm start energy
    uint8_t crateChance = 40;                              // % per turn
    uint8_t weaponShare = 30, healthShare = 30, utilityShare = 20, crateHealth = 25;  // crate odds (W4M Standard); hp in a health crate
    uint8_t mines = 5, barrels = 4, mineFuse = 3;          // fuse seconds 0..5, FUSE_RANDOM: 1..5 s per mine
    uint8_t sdType = 0;                                    // SD_*
    uint8_t fallDamage = 1, wind = 1;                      // wind 0..3: none, low, medium, high (W4M WindMaxStrength 0, 3, [6], 10)
    uint8_t weapons = 0;                                   // SET_*
    enum : uint8_t { FUSE_RANDOM = 6, SD_BOTH = 0, SD_WATER, SD_ONE_HP, SET_DEFAULT = 0, SET_BNG, SET_CRATES, SET_UNLIMITED };
};
static_assert(sizeof(Scheme) == 17, "Scheme must stay plain bytes");
// W4M Wormpot modes (FETXT.WPotName.*), one bit each; bits [first, last) of WORMPOT_REELS form one slot reel.
enum Wormpot : uint32_t {
    WP_DOUBLE_DAMAGE = 1, WP_SUPER_EXPLOSIVES = 2, WP_SUPER_ANIMALS = 4, WP_WIND_ALL = 8, WP_WORMS_DROWN = 16,
    WP_QUICK_WALK = 32, WP_SLIPPY = 64, WP_STICKY = 128, WP_LOW_GRAVITY = 256, WP_NO_JUMPING = 512, WP_MAX_FALL = 1024,
    WP_CRATE_SHOWER = 2048, WP_CRATE_DROPS = 4096, WP_MAX_HEALTH = 8192, WP_GOLIATH = 16384, WP_ONE_SHOT = 32768,
    WP_VAMPIRE = 65536, WP_VITAL_WORM = 131072, WP_MULTI_GIRDER = 262144,  // MULTI_GIRDER: W4M WormPot GirdersDontEndTurn
};
struct WormpotMode { const char *name, *help; };
extern const WormpotMode WORMPOT_MODES[19];  // [bit index]
extern const int WORMPOT_REELS[4];          // reel r = bits WORMPOT_REELS[r] .. WORMPOT_REELS[r + 1] - 1
struct SchemePreset { const char *name; Scheme s; const char *delays; };  // delays: "Weapon N|..." (W4M SchemeData Delay), "*" = Weapon Factory
extern const std::vector<SchemePreset> SCHEMES;  // [0] = Standard; values from W4M Data/Tweak/LOCAL.XOM
struct GameConfig {
    uint32_t seed = 0;
    int teams = 2, wormsPerTeam = 3;
    std::string map;     // romfs maps/<map>.json; empty = procedural island
    uint32_t rules = 0;  // Rule flags
    struct Team { std::string name; uint8_t cpu = 0, voice = 0, hat = 0; };  // cpu: 0 = human, 1..5 = AI level (W4M CPU1..CPU5)
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

constexpr int msTicks(int ms) { return ms * 60 / 1000; }  // W4M times are in ms, the sim runs at 60 Hz

// Deterministic simulation: same seed + same inputs => same state on every client.
struct Game {
    static constexpr float DT = 1.0f / 60, R = 0.5f, BODY_R = 0.3f;  // BODY_R: the worm mesh's half width
    static constexpr float STEP = 0.25f, STEP_UP = 1.0f;  // W4M UpdateWalking: steps up to 5 units, vaults ledges up to the 20-unit body
    // W4M HomingPayload 0x560aa0: Stage1 1250 ms straight, Stage2 4000 ms homing, Stage3 5000 ms straight, then it expires; no gravity (IsAffectedByGravity 0)
    static constexpr float HOMING_LOCK = 1.25f, HOMING_TIME = 4, HOMING_LIFE = 10.25f, HOMING_ACCEL = 97.5f, HOMING_MAX = 29.5f;
    // W4M 0x560eb0 (stage 2 only, 0x5617c3): v += dir x HomingAcceleration 0.00195 units/ms², |v| <= MaxHomingSpeed 0.59 units/ms
    static Vector3 homingStep(Vector3 v, Vector3 p, Vector3 aim) {
        Vector3 d = {aim.x - p.x, aim.y - p.y, aim.z - p.z};
        float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), k = l > 0 ? HOMING_ACCEL * DT / l : 0;
        v = {v.x + d.x * k, v.y + d.y * k, v.z + d.z * k};
        float s = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z), c = s > HOMING_MAX ? HOMING_MAX / s : 1;
        return {v.x * c, v.y * c, v.z * c};
    }
    static constexpr float WIND_ACCEL = 4.25f;  // W4M payload accel += Wind.Speed (0x57eb18, IsAffectedByWind); Wind.MaxSpeed 8.5e-5 units/ms²
    static constexpr int JUMP_WINDOW = 18;  // W4M StartJump 0x5acd40: a jump waits 300 ms for a second press (backflip)
    // W4M, 20 units = 1 m: launch 0x5a5d30 (vy 0.15811 / vx 0.063246, backflip 0.2 / 0.031623 units/ms); Walk.Speed / 0.004 x 0.05 units/ms
    static constexpr float JUMP_UP = 7.9057f, JUMP_FWD = 3.1623f, FLIP_UP = 10, FLIP_BACK = -1.5811f, WALK_SPEED = 3.0625f;
    static constexpr float FWDFLIP_FWD = 1.5811f, VJUMP_UP = 9.354f;  // W4M forward flip vx 0.031623 (0x95fb88), vertical vy 0.18708 (0x95fb7c)
    // W4M DetectJump 0x5aefa0: release turns held (2) into tapped (0), a second press makes it a flip (1); all launch when the window ends
    static bool jumpTick(int &delay, uint8_t &kind, uint8_t buttons, uint8_t pressed, int walk, float yaw, Vector3 &vel) {
        if (kind == 2 && !(buttons & Input::JUMP)) kind = 0;
        else if (kind == 0 && (pressed & Input::JUMP)) kind = 1;
        if (--delay > 0) return false;
        float side = JUMP_FWD, up = JUMP_UP;  // tapped, or held with the stick forward
        if (kind == 1) side = walk > 0 ? FWDFLIP_FWD : FLIP_BACK, up = FLIP_UP;
        else if (kind == 2 && walk <= 0) side = 0, up = VJUMP_UP;
        vel = {sinf(yaw) * side, up, cosf(yaw) * side};
        return true;
    }
    float gravity() const;
    static constexpr float MINE_DUD = 0.1f;  // W4M Mine.DudProbability
    static constexpr float MINE_ARM = 2.25f;  // W4M Landmine ArmingRadius 45: any worm this close arms it
    static constexpr int MINE_COURTESY = 150;  // W4M ArmingCourtesyTime 2500 ms: a laid mine ignores worms that long
    static constexpr int ARMOUR = 25;        // W4M Shield.DamageScale 0.25 (explosion handler 0x5ae71c): share of the damage still taken
    // W4M WXWormLogicEntity explosion handler 0x5ae4f0 (docs/weapons-audit.md "Blast formula"); shared with the AI
    static int blastDamage(const Blast &b, Vector3 p, Vector3 w);
    static Vector3 blastKick(const Blast &b, Vector3 p, Vector3 w);
    // W4M Worm.Death* (TWEAK.XOM): the dead worm's blast, also when it pops at the surface
    static constexpr Blast DEATH_BLAST = {1.75f, 3, 35, 30, 2.25f, 0.5f};
    // W4M kWeaponLandmine, OilDrum.* and Crate.* (TWEAK.XOM); impulse depth: drum 9 units (0x5d13ce), crate its radius 10 x Crate.Scale (0x5c5879)
    static constexpr Blast MINE_BLAST = {2.625f, 3.73f, 40, 12.5f, 5, 2.5f}, BARREL_BLAST = {2.25f, 3.75f, 55, 20, 3.75f, 0.45f},
                           CRATE_BLAST = {3, 3.5f, 60, 9, 2.5f, 0.5f};
    static constexpr int ROPE_SHOTS = 5;     // W4M Ninja.NumShots: rope launches per turn
    static constexpr float SHEEP_WALK = 5, SHEEP_STEP = 4, SHEEP_TAKEOFF = 0.6f;  // walks-first super sheep: walk fuse s, m/s, take-off pitch
    static constexpr float SCOUSER_FLOAT = 5;  // s a swallowed worm is carried before the pop
    static constexpr float STRIKE_GAP = 2.5f;  // m between air strike bombs: BlitzDuration 2 s x GroundSpeed 7.5 m/s / NumBombs 6
    static constexpr int STRIKE_TICKS = 20;    // one bomb every BlitzDuration / NumBombs = 333 ms
    static constexpr int STRIKE_LEAD = 240;    // W4M Bomber: the first DropBomb waits for the 4 s bombrun_start clip (0x54db75)
    static constexpr float STRIKE_EXTRA = 7;  // Bomber.ExtraHeight 140 units
    // W4M AlienAbductionLogicEntity clip lengths (0x546d00): AbductStart, AbductViolate + AbductOpenDoors, AbductEnd; ABD_RISE = Abduction.ExtraHeight 220 units
    static constexpr float ABD_ARRIVE = 8.333f, ABD_STAY = 6.917f, ABD_LEAVE = 3.25f, ABD_RISE = 11;
    static constexpr float BOMBER_HEIGHT = 15, BOMBER_LEAD = 20, BOMBER_GAP = 0.8f, COW_CHUTE = 5;  // m, m, s (SuperBomber.DelayBetweenBombs), m/s
    // W4M Concrete Donkey 0x553370: 220 units/s^4 fall curve, 0.75 s back to the apex, 85 ms held after a smash, LifeTime 8000 ms
    static constexpr float DONKEY_CURVE = 11, DONKEY_HANG = 0.75f, DONKEY_LIFE = 8;
    static constexpr int DONKEY_HOLD = 5;
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
    int jumpDelay = 0;  // jump pending: ticks left of the W4M window
    uint8_t jumpKind = 0;  // W4M DetectJump kind: 0 tapped, 1 pressed twice, 2 held
    bool selfHurt = false;  // the active worm took damage: its turn ends (W4M)
    float power = 0, wind = 0;
    bool roped = false, jetting = false;  // active utility: keeps the turn going; jetting = in flight
    bool jetUsed = false;  // the jetpack in hand has taken off (ammo spent, fuel live); W4M entity +0xf0
    float boost = 0;       // W4M Jetpack SuperThrust level 0..1 (+0xa0)
    // W4M JetpackUtilityLogicEntity 0x562810 (TWEAK Jetpack.*): ThrustScale 0.004 x 2 per 20 ms, FwdThrustRotation 0.3 rad,
    // MaxAltitude 2000 units over Water.Level, turn 2 x TurnRotationSpeed 0.0092 rad per 20 ms, fuel stops at 20 ms
    static constexpr float JET_TILT = 0.3f, JET_CEIL = 100, JET_TURN = 0.92f, JET_DRY = 0.02f, JET_BOUNCE = 0.8f;
    // OverCeilingThrustMod, XZWindResThrust / NoThrust per 20 ms; SuperThrust* under -0.15 units/ms (Mod 0.2 per units/ms = 0.004 per m/s)
    static constexpr float JET_OVER = 0.05f, JET_RES = 0.999f, JET_RES_IDLE = 0.95f, JET_FALL = 7.5f, BOOST_MOD = 0.004f, BOOST_ACCEL = 0.3f, BOOST_DECAY = 0.97f, BOOST_OFF = 0.01f;
    bool chute = false;                   // parachute open until the turn ends
    Vector3 anchor{};
    float ropeLen = 0, fuel = 0, ropeMax = 0, thrust = 0;  // max/thrust: of the tool in use, the hand may hold a weapon
    std::vector<int> fuses;  // per team: seconds set for userFuse weapons (W4M default 3)
    std::vector<std::vector<int>> delays;  // [team][weapon]: own turns left before it unlocks (W4M InventoryN.WeaponDelays)
    bool usable(int team, int wi) const { return ammo[team][wi] && !delays[team][wi]; }
    bool selectable(int team, int wi) const;  // usable, and with a movement tool out only the tool itself or a toolDrop()
    bool abducting() const { for (const Projectile &s : shots) if (WEAPONS[s.weapon].kind == Kind::Abduction) return true; return false; }  // W4M EFMV.Active (0x548d0b): labels off
    bool toolOut() const;  // rope (or hooked object), jetpack in flight, open parachute in the air: W4M utility mode +0x8d
    // W4M m_eSecondaryWeapon (+0x98, flag +0x8c): a toolDrop() held besides the tool, dropped by Fire.Second; -1 none
    int secondary = -1;
    int held() const { return secondary >= 0 ? secondary : weapon; }  // what NEXT_WEAPON and the panel step
    void firstWeapon(int team);  // W4M Weapon.Create 0x565770: the first usable item, Skip Go / Surrender skipped
    int shotsLeft = 0;
    int ropeShots = 0;  // rope launches this turn
    Phase phase = Phase::Aim;
    uint8_t prevButtons = 0;
    uint32_t rng = 1;
    float water = Terrain::WATER;    // rises in sudden death; terrain.* keeps using the constant
    bool suddenDeath = false;
    Vector3 raceFinish{};  // rope race: terrain.finish, or a deterministic fallback
    std::vector<uint8_t> idle;  // per team: never takes a turn (mission captives)
    // W4M GirderKitLogicEntity 0x55ada0: a preview 2 m ahead at eye level, 0.3 m steps camera-relative, each axis within
    // Weapon.Girder.MaxDistance 300 units of the worm; FIRE welds GirderSmall.xom (4 x 2 x 4 voxels of 1 m, profile 2,1,1,2)
    static constexpr float GIRDER_STEP = 0.3f, GIRDER_RANGE = 15, GIRDER_CAM = 16.25f, GIRDER_CAM_PITCH = 0.6f, GIRDER_YAW = 0.4f;
    static constexpr int GIRDER_TICKS = 5, GIRDER_MAX = 45;  // a step per 4 updates of 20 ms (unverified); Girder.TotalNumberPlaced < 45
    bool girderOn = false;  // preview up: GirderCam follows it, TARGET's walk / aim step it, PITCH's aim raises it
    Vector3 girder{}, girderFrom{};  // preview centre, the worm's position when it appeared
    int girderWait = 0, girders = 0;  // ticks to the next step, placed
    void stepGirder(const Input &in, uint8_t pressed);  // yaw in 45 degree steps (FUSE_UP / DOWN), ticks to the next step, placed
    int girderFits(Vector3 c) const;  // W4M 0x5590e0: probe box edges clear of land, 4 spheres clear of worms and objects
    // W4M BubbleTroubleLogicEntity 0x54f6e0: a falling shell around its spot; shots from outside bounce off, worms inside
    // ignore blasts centred outside Radius 42 units; Lifetime 6 turn ends; any blast inside pops it
    static constexpr float BUBBLE_R = 2.1f, BUBBLE_UP = 1.75f, BUBBLE_SHELL = 2.52f, BUBBLE_IN = 1.52f;  // Radius, OffsetY, Radius x 1.2, - 20
    struct Bubble { Vector3 pos, vel; int life; };  // pos: its base; centre BUBBLE_UP higher
    std::vector<Bubble> bubbles;
    bool shielded(const Worm &w, Vector3 blast) const;  // in a bubble the blast is outside of
    // W4M RedbullUtilityLogicEntity 0x587390: after a jump, JUMP flaps up at FlapVelocity 0.15 u/ms in a 250 ms window every
    // 500 ms (Weapon.Redbull.MinTime / TimeLength); a flap zeroes the horizontal speed, the stick steers (WXWorm.Aftertouch*)
    static constexpr float FLAP = 7.5f, AFTERTOUCH = 0.75f, AFTERTOUCH_MAX = 5;
    static constexpr int FLAP_WAIT = 15, FLAP_BEAT = 30;
    int icarus = 0;  // 0 off, 1 drunk (on the ground), 2 flying; flapAt: tick the next window opens
    int flapAt = 0;
    Vector3 drift{};  // aftertouch velocity, kept across flaps
    bool doubleDamage = false;  // W4M SetData("DoubleDamage", 1) from a crate: every blast and hit this turn x2
    bool doubled() const { return doubleDamage || (cfg.wormpot & WP_DOUBLE_DAMAGE); }  // the Wormpot mode sets it every turn
    std::vector<uint8_t> spy;   // per team: TeamDataContainer.IsCrateSpyActive, crate contents shown in its turns
    // W4M Binoculars 0x54bcc0: FIRE on a target seen from the eye solves the bazooka shot (no wind), shown after 4 s
    struct Scout { Vector3 at; float power, pitch; int t = -1; bool ok; };  // t: ticks since FIRE, -1 none
    Scout scout;
    static constexpr int SCOUT_TICKS = 240;
    bool scoutSolve(Vector3 at, float &power, float &pitch) const;
    MissionRun run;
    // Settle count, per group of nearby hurt worms: camera travel, the W4M damage display (hp counts 40 hp/s, at most 1.5 s),
    // then the death queue: 3 s of throes per dead worm, blast, next one. Then PostActivityTime. docs/death-sequence.md
    static constexpr int COUNT_TRAVEL = 42;              // camera travel to the group (ours)
    static constexpr int COUNT_DAMAGE = msTicks(2500);     // W4M Worm.DamageComplete posted 2500 ms after the damage (0x5abe94)
    static constexpr int COUNT_THROES = msTicks(3000);     // W4M kWPS_DeathThroes timer +0x128 (Worm.TimeToDie 0x5adbf0)
    static constexpr int COUNT_FLOAT = msTicks(2000);      // W4M kWPS_DrownFloat timer (0x5aa222)
    static constexpr int COUNT_DEATH = 1;                // W4M: the next death pops on the queue's next tick (0x4f9b30), no grave wait
    static constexpr int POST_ACTIVITY = msTicks(2400);    // W4M PostActivityTime (LOCAL.XOM) once nothing is active
    static constexpr int SETTLE_WAIT = 300, SHOT_CAP = 30 * 60;  // ours: give up on moving worms / objects after 5 s, on a shot after 30 s
    static constexpr float COUNT_SPAN = 18;  // m from the group's first worm
    std::vector<int> countGroup;  // worms counting in Settle; countT: ticks into the group's count
    int countT = 0, countEnd = 0;  // countEnd: countSpan() when the group formed; death blasts must not move the queue
    int countTicks(int i) const {  // drowned: no count, floats instead (W4M kWPS_DrownFloat)
        return !worms[i].alive ? COUNT_FLOAT : std::min(90, std::max(1, std::abs(std::max(0, worms[i].hp) - worms[i].counted) * 3 / 2));
    }
    int countSpan() const { int b = 0; for (int i : countGroup) b = std::max(b, COUNT_TRAVEL + (worms[i].alive ? COUNT_DAMAGE : COUNT_FLOAT)); return b; }
    int countBoom() const { return countEnd; }  // every damage display is over: the death queue starts
    bool drowned(int i) const { return worms[i].pos.y < water; }  // pops at the end of its float, outside the queue (W4M)
    int blastAt(int i) const {  // countT of dead worm i's blast; -1: when the queue is over
        if (i >= 0 && drowned(i)) return COUNT_TRAVEL + COUNT_FLOAT;
        int t = countBoom();
        for (int j : countGroup)
            if (worms[j].hp <= 0 && !drowned(j)) { t += COUNT_THROES; if (j == i) return t; t += COUNT_DEATH; }
        return t;
    }
    int dying() const {  // the dead worm the camera shows: its throes or its float, until its blast
        for (int i : countGroup)
            if (worms[i].hp <= 0 && countT > blastAt(i) - (drowned(i) ? COUNT_FLOAT : COUNT_THROES) && countT <= blastAt(i) + COUNT_DEATH) return i;
        return -1;
    }

    GameConfig cfg;

    void start(const GameConfig &cfg);
    void step(const Input &in);
    uint32_t checksum() const;
    Vector3 aimDir(const Worm &w) const;
    Vector3 target() const;  // terrain point under the aim reticle, or at the Blimp view's centre
    // W4M Blimp: DefaultPitch 1 rad, StickLength 500, HeightAboveLand 6 units; full stick: MoveSpeed 250 u/s x MaxZoom 2,
    // RotateSpeed 0.55 rad/s x (0.9 + 0.1 MaxZoom). The client scales by its zoom.
    // PitchSpeed 0.45 x 1.1; the focus stays within 4500 units of Land.Center (0x8556a0).
    static constexpr float BLIMP_PITCH = 1, BLIMP_STICK = 25, BLIMP_LIFT = 0.3f, CURSOR_SPEED = 25, BLIMP_TURN = 0.605f, BLIMP_TILT = 0.495f, BLIMP_RANGE = 225;
    static constexpr float BOMBER_SPEED = 7.5f;  // m/s: Bomber.GroundSpeed 0.15 units/ms
    bool cursorOn = false;  // Blimp state, from the first TARGET input of the turn
    bool blimp = false;     // the last tick had TARGET with a targeted weapon: the player is in the Blimp view
    Vector3 cursor{};       // the camera's focus: the reticle is the land behind it on the camera ray
    float cursorYaw = 0, cursorPitch = BLIMP_PITCH;
    bool locked = false;  // homing target picked: FIRE in the Blimp (W4M 0x583a10 Payload.Target), then aimed and powered as usual
    Vector3 lockAt{};
    Vector3 blimpFocus(Vector3 ref, float yaw) const;  // W4M entry pose: above all land, its centre ray on ref
    Vector3 blimpEye(Vector3 focus, float yaw, float pitch = BLIMP_PITCH) const;
    bool blimpHit(Vector3 *hit) const;  // W4M CMS 0x51c910: land, then water, on the camera ray; false: no target
    Vector3 fatkinsDrop(const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) const;  // W4M Bomber: the fat man leaves the plane to land on tgt
    // W4M Bomber 0x54d460: the plane flies ExtraHeight over all land and drops early, its bombs keep its speed; vel = the plane's
    Vector3 strikeStart(const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) const;
    float landTop() const;  // Land.MaxHeight: a column every 2 m
    Vector3 landCenter() const;
    Vector3 strikeDir() const { return {-cosf(cursorYaw), 0, sinf(cursorYaw)}; }  // the view's right: bombers cross the screen
    int landHold = 0;  // ticks the turn still waits once a dropped crate has landed
    int retreatTicks(const WeaponDef &d) const { return msTicks(d.retreat >= 0 ? d.retreat : cfg.scheme.retreatTime * 1000); }
    bool steered() const;     // a live shot takes the stick (old woman, scouser, super sheep, Bovine Blitz)
    bool retreating() const;  // Flying / Retreat and the worm may move: W4M timer started (0x549bb0), no FlyCam holding WormMoving
    bool dropping() const { if (landHold > 0) return true; for (const Object &o : objects) if (o.type == Object::Crate && o.falling) return true; return false; }  // crate under its chute
    float fuseOf(const WeaponDef &wd) const { return wd.userFuse ? fuses[worms[current].team] : wd.fuse; }

private:
    float rand01();
    void beginTurn(int team);
    void nextWeapon(int team);
    void use(Worm &w);
    void emit(GameEvent::Kind k, Vector3 p, int worm = -1, int weapon = -1) { events.push_back({k, p, worm, weapon}); }
    void stepWorm(Worm &w);
    void drown(Worm &w);
    void stepRope(Worm &w);
    void stepShots(const Input &in, bool detonate);
    void explode(Vector3 p, const Blast &b, float poison = 0);
    void steal(const Worm &victim);  // old woman ammo theft
    void hurt(Worm &w, int dmg, bool blast = false);  // vampire/karma/highlander for the active worm; blast: armour applies
    bool dropPoint(Vector3 &out);
    bool addObject(Object::Type t, float lift);
    void stepObjects();
    Object *hooked();  // the object on the rope, if any
};
void missionStart(Game &g);  // mission.cpp: worms, ammo, objects from cfg.mission
void missionStep(Game &g);   // mission.cpp: sequences, objectives, result

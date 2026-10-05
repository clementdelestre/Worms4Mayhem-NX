#pragma once
#include "terrain.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Quantized per-tick input of the active player: the only thing sent over the network.
struct Input {
    int8_t turn = 0, walk = 0, aim = 0;
    uint8_t buttons = 0, flags = 0;
    enum : uint8_t { CAMERA = 1, SKIP_COUNT = 2 };  // flags: a camera key this tick (W4M InGame group: its SomeInputFrom ends the hot seat); SKIP_COUNT: observed in W4M by the user 2026-10-03, ends the damage display
    enum : uint8_t { FIRE = 1, JUMP = 2, NEXT_WEAPON = 4, HEADING = 8, FUSE_UP = 16, FUSE_DOWN = 32, TARGET = 64, PITCH = 128 };  // HEADING: turn is the wanted yaw, PI * turn / 128 (W4M walk)
    // FUSE_UP/DOWN: W4M FuseUp, the timer of user-fuse weapons (WeaponDef::userFuse) in 1 s steps
    // TARGET: W4M Blimp view; turn yaws the camera, walk / aim move its focus (Game::cursor) forward / right, the worm stays put.
    // PITCH (with TARGET): this tick's aim tilts the camera instead of moving it sideways (the client alternates the two)
    // NEXT_WEAPON: aim 0 steps to the next weapon (on the press); aim != 0 picks weapon (uint8_t)aim - 1 (W4M direct select), aim unused
    static Input pick(int weapon) { Input in; in.buttons = NEXT_WEAPON, in.aim = (int8_t)(uint8_t)(weapon + 1); return in; }
};

// Heading helpers shared by the sim, the AI and the controls: yaw 0 faces +z
inline Vector3 dirOf(float yaw, float pitch) { return {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}; }
inline Vector3 flat(float yaw) { return {sinf(yaw), 0, cosf(yaw)}; }
inline float yawTo(Vector3 a, Vector3 b) { return atan2f(b.x - a.x, b.z - a.z); }
inline float wrapPi(float a) { return remainderf(a, 2 * PI); }

// Shell covers bazooka/grenades/clusters; speed = rope length or jetpack thrust, fuse = sheep timeout or jetpack fuel.
// Melee: speed = knock along the aim, bounce = upward knock, fuse = attacker's leap. Sentry: radius = range, fuse = reload.
// Abduction: radius = W4M Abduction.AreaOfEffect (xz), speed = NormalSpeed, m/s.
// Airstrike with fuse > 0: W4M controlled bomber (Bovine Blitz), flies fuse s at speed, FIRE drops one of `clusters` payloads.
enum class Kind : uint8_t { Shell, Sheep, Airstrike, Donkey, Shotgun, Rope, Jetpack, Teleport,
                            SuperSheep, OldWoman, Melee, Homing, Mine, Scouser, Sentry, Abduction, Flood,
                            Parachute, SkipGo, Surrender, ChangeWorm, Armour,
                            Girder, Binoculars, Bubble, Icarus, DoubleDamage, CrateSpy };  // W4M utilities 34, 43, 42, 41, 44, 46
inline bool collected(Kind k) { return k == Kind::DoubleDamage || k == Kind::CrateSpy || k == Kind::Armour; }  // W4M crate collect 0x5c9800 (44, 46, 47): applied at once, never in the inventory
inline bool targeted(Kind k) { return k == Kind::Airstrike || k == Kind::Donkey || k == Kind::Abduction || k == Kind::Teleport; }  // W4M IsTargetingWeapon seen from the Blimp (Homing: see blimped)
inline bool blimped(Kind k) { return targeted(k) || k == Kind::Homing; }  // seen from the Blimp: also Homing, which locks from there or from the aim view (0x583a10, user-requested A / ZR mapping)
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
    bool fuseShown = false;   // "fuse_shown": W4M IsFuseDisplayed, the last 5 s float over the shot (0x57b1e0)
    float fuseHeight = 0.5f, fuseSize = 0.25f;  // "fuse_height", "fuse_size": W4M FuseTimerGraphicOffset + Radius, FuseTimerScale (0: HUD.3DText.Scale 5), m
    bool walks = false;       // "walks": a super sheep that walks first, FIRE takes off (W4M "launch, transform and detonate")
    // W4M WEAPTWK WormDamageRadius, ImpulseMagnitude, ImpulseRadius, -ImpulseOffset in m, m/s; -1: derived from radius
    float blast[4] = {-1, -1, -1, -1}, cblast[4] = {-1, -1, -1, -1};  // "reach", "push", "push_reach", "push_depth"; cluster_*
    float stick = 0;  // "stick": W4M PreDetonationTime s: ArmOnImpact + all bounce damping 0, a land hit stops it and it detonates that long after (worm hit: at once)
    float lift = 0;  // "lift": W4M payload Radius = its collider sphere (donkey 3.6 m): the centre rests that far above the ground it hit
    float grav = 1;  // "gravity": share of Gravity; W4M IsLowGravity = Gravity.Slow 0.6, IsAffectedByGravity 0 = 0
    float base = -1;  // "min_speed": W4M BasePower in m/s, speed = BasePower + MaxPower; -1: 0.15 x speed
    bool avoid = false;  // "avoid_land": W4M Factory HomingAvoidLand (+0x69): the homing missile steers off land, 30 s life, 12.5 m/s, detonates on expiry, ChaseCamera
    int retreat = -1, postLaunch = 0;  // "retreat", "post_launch": W4M RetreatTimeOverride (-1: the scheme's LandTime), PostLaunchDelay, ms
    // W4M payload water (Game::wet): "size" Radius (the surface is met that high), "sink" SinkDepth, m; "skim_speed" MinSpeedForSkim
    // m/s (-1: SkimsOnWater 0), "skim_angle" MaxAngleForSkim rad, "skim_xz" / "skim_y" SkimDamping; cluster_*: the bomblets' container
    float size = 0, sink = 0, csize = 0, csink = 0, skim[4] = {-1, 0, 0, 0};
};
// W4M ExplosionMessage: crater (LandDamageRadius), worm damage reach and max, knockback m/s, its reach and its epicentre depth.
struct Blast { float crater, reach, damage, push, pushReach, pushDepth; Vector3 pushOff = {0, 0, 0}; };  // pushOff: the impulse centre's offset from the blast point (guns)
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
bool customWeapon(int i);  // a Weapon Factory weapon (appended after the loaded table at start())
bool saveCustomWeapons(const char *path, const std::vector<WeaponDef> &list);

// W4M worm physics state beside Velocity: stuck count (entity +0x12c), air control (Flags bit0), Sliding (state 3) and its
// spin rate / target (+0x120 / +0x124, rad per 20 ms), SupportNormal (pData +0x80). input: this tick's stick, not state
struct Motion { int stuck = 0; bool air = false, slide = false; float spin = 0, spinTo = 0; Vector3 normal{0, 1, 0}, input{}; };
struct Worm {
    Vector3 pos, vel;
    float yaw, pitch;
    int hp, team;
    bool alive, grounded;
    int dealt[5] = {};  // damage taken per type since the last ApplyDamage (cap 75, 0x5ababb)
    int poison = 0;  // hp lost at each turn start, never below 1
    int counted = 0;  // hp its label shows: Settle counts it toward hp, nearby worms together (W4M)
    bool nailed = false;  // Tail Nail: can't walk, jump or use tools, animals, melee; blasting the ground frees it
    bool armour = false;  // W4M Armour: blasts hurt it ARMOUR %, knock it back half as far (rest of its life); bullets ignore it
    // W4M WormData flag 0x400 (spat out by the UFO, cleared by poison or a health crate): zap = ticks to the next Zap (-1: not started), calm = hp at the last turn start (-1 unarmed)
    bool abducted = false, zapFound = false;  // zapFound: zapSpot holds a checked landing spot (UpdateAbductee +0x14c / +0x140)
    int zap = -1, calm = -1;
    Vector3 zapSpot{};
    bool drowned = false;  // W4M kWPS_DrownFloat (0x5ad640): afloat while counted > 0, then its blast
    int floatT = 0;        // DrownFloat timer, ticks: 0 until it reaches the surface, then DROWN_FLOAT down to the blast
    Motion motion;
};
bool meleeHits(const Worm &a, Vector3 p, const WeaponDef &wd);  // p inside a's melee hit box (shared with the AI)
// W4M Vaulting 0x5aca80: t ticks left (0 = walking), dir = the input when it began
struct Vault { Vector3 from{}, to{}, dir{}, vel{}; int t = 0; };  // vel: the walk velocity, W4M Velocity during the vault
// Ground walk and wall clearance, shared with the AI's prediction. walkStep: true when it walked off a ledge.
// A 5..20-unit ledge starts *vault (pos unchanged) when given, else it is climbed at once.
bool walkStep(const Terrain &t, Vector3 &pos, float yaw, float dist, Vault *vault = nullptr);
void vaultStep(Vector3 &pos, Vault &v, Vector3 input);  // one vault tick
void clearWalls(const Terrain &t, Vector3 &pos);
bool fits(const Terrain &t, Vector3 from, Vector3 to);  // the upper body at `to` is out of land, or no deeper than at `from`
// A tick's move cut into sub-steps of at most VOX/2 (shots, objects, walkers), so nothing skips thin land.
int substeps(Vector3 vel);
// W4M 0x585a29 launches from the worm's eye (feet + Worm.EyeLevelOffset 15 units): spawn, pulled back to the last free point eye → spawn
Vector3 muzzle(const Terrain &t, Vector3 pos, Vector3 spawn);
// W4M LogicalLaunchZ/YOffset from the eye (WEAPTWK): dynamite 13/-10, landmine 10/-10, (super) sheep, starburst 5, old woman 7, scouser 10 units, the rest 0
Vector3 launchPoint(const WeaponDef &d, Vector3 pos, float yaw);
// W4M payloads touch land by their centre point (0x574e90); at rest the mesh is drawn r along the land normal (0x5761f0)
Vector3 restOn(const Terrain &t, Vector3 p, float r);
bool jetBody(const Terrain &t, Vector3 &pos, Vector3 &vel, float g);  // W4M jetpack collider, one tick: true when a foot landed the pack
// One worm tick (ground slide, fall, flight), shared with the AI; returns the landing speed, 0 if none.
// wind: the Ballistic acceleration's xz part (W4M SetAcceleration 0x5a6d20, Wormpot WindAffectsWorms), m/s²
float wormBody(const Terrain &t, Vector3 &pos, Vector3 &vel, bool &grounded, Motion &m, float &yaw, float gravity, uint64_t pot, float e = 0.3f, Vector2 wind = {0, 0});
// W4M UpdateWalking 0x5b19d0: a step onto ground past SlideAngle starts Sliding with the walk velocity
void slideIfSteep(const Terrain &t, Vector3 pos, Vector3 &vel, Motion &m, Vector3 walk, uint64_t pot);
void walkerStep(const Terrain &t, Vector3 &pos, Vector3 &vel, float gravity);  // sheep, old woman, scouser on foot

// W4M NinjaRopeUtilityLogicEntity: the body (worm feet, or a hooked object) swings in its yaw's vertical plane about the last bend
struct Rope {
    static constexpr int MAX = 16;
    Vector3 pt[MAX]{}, side[MAX]{};  // m_NinjaRopeList (+0x20): bends from the hook on, each with its unwrap side (+0x10)
    float len[MAX]{};                // segment lengths, m
    int n = 0;
    float angle = 0, spin = 0, yaw = 0;  // +0x120: 0 hangs, > 0 behind the yaw; +0x10c: rad per 20 ms; the swing plane (+0x114)
    bool swung = false;                  // +0x79: hooked since the worm last stood (cleared idle off Ballistic, 0x574b96)
};

// W4M rope mode 2 (0x572800 / 0x573d00): the hook flies 1 unit/ms from the eye until it hits land, catches an object or passes MaxLength
struct Hook {
    Vector3 at{}, vel{};
    bool on = false;
};

struct Projectile {
    Vector3 pos, vel;
    int weapon;
    float fuse;
    bool child;  // cluster bomblet or airstrike missile: explodes on impact, never splits
    int hits;    // explosions left before it disappears (donkey)
    Vector3 aim{};  // homing target
    int stage = 0;   // walks-first super sheep: 1 once airborne; scouser: 1 once inflated
    int prey = -1;   // scouser: the worm it carries; old woman: the last worm she robbed
    uint64_t touching = ~0ull;  // W4M 0x582200 / 0x581dc0: last tick's contacts (worm bits, 63 = target) are not hits; all of them on the first tick
    bool sunk = false;  // W4M +0x6f: under the disarm plane, disarmed, sinking to Water.ExpiryDepth
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
    bool hooked = false;  // W4M kRopeModeAttachedToObject: swings on Game::rope about the worm's feet
    bool spawning = false;  // crate: W4M "Crate Spawn" active object (0x5c9bd0) until a bounce leaves it under 1 m/s (0x5c8900)
    float delay = -1;  // mine: fuse s drawn at creation (W4M CreateMine 0x4f97cf); -1: drawn when armed
    bool fizzle = false;  // mine: its dud roll, drawn with delay
    int mystery = -1;  // crate: W4M kMystery item - 0x32 (MYSTERY_ITEMS), drawn at spawn; weapon stays -1
};

// W4M mystery crate items (kMysteryMineLayer 0x32 .. kMysteryGoodPoison 0x40): Text.k<name> and the <name>Mystery.Crate weight of the
// schemes that drop them (All Action, Mega Power, Mystery, Kitchen Sink, custom WXD.DefaultSchemeData; LOCAL.XOM)
struct MysteryItem { const char *name, *text; int weight; };
extern const MysteryItem MYSTERY_ITEMS[15];
enum : int { MY_MINE_LAYER, MY_MINE_TRIPLET, MY_BARREL_TRIPLET, MY_FLOOD, MY_DISARM, MY_TELEPORT, MY_QUICK_WALK, MY_LOW_GRAVITY, MY_DOUBLE_TIME,
             MY_HEALTH, MY_DAMAGE, MY_SUPER_HEALTH, MY_SPECIAL_WEAPON, MY_BAD_POISON, MY_GOOD_POISON };

// Things that happened this tick, for audio/fx; not part of the checksum. worm/weapon = -1 when not applicable.
struct GameEvent {
    enum Kind : uint8_t { Boom, BigBoom, Fire, Bounce, Splash, Death, Hurt, Jump, TurnStart, GameOver, CrateDrop, Collect, MineArm, Hallelujah, CrateLand,
                          Launch, Zap, Poof, AbdDamage, Abducted, BubbleNew, BubbleHit, BubblePop, Fall, Arm, Mystery, Debris, JetStart } kind;  // Arm: a payload armed on impact (the arrow's ArmSfxLoop)  // Zap / Poof: an abductee's new / old spot; AbdDamage: its random hp  // Launch: a bomber dropped a payload (W4M LaunchSfx: BombWhistle, CowFall)  // JetStart: a jetpack takes off from Ambulatory (PackAccessory.Trigger 0x5623a7)
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
    uint8_t mines = 15, barrels = 10, mineFuse = 3;        // W4M Objects 3: 15 mines, 10 drums; MineFuse s (editor -1..5, 0x752f33; Family 8), FUSE_RANDOM (W4M -1): 0..5 s per mine
    uint8_t sdType = 1;                                    // W4M SchemeData SuddenDeath: SD_ONE_HP, SD_NOTHING (commentary only), SD_DRAW
    uint8_t fallDamage = 1, wind = 1;                      // wind 0..3: W4M WindMaxStrength 0, 3, 5 (editor Medium), 10
    uint8_t weapons = 0;                                   // SET_*
    uint8_t waterSpeed = 2;                                // W4M SchemeData WaterSpeed 0..3: Water.RiseSpeed 0 / 4 / 8 / 16 units per turn end
    uint8_t mysteryShare = 0;                              // W4M SchemeData MysteryChance (+0x130), rolled after the other three shares
    enum : uint8_t { FUSE_RANDOM = 6, SD_ONE_HP = 0, SD_NOTHING, SD_DRAW, SET_DEFAULT = 0, SET_BNG, SET_CRATES, SET_UNLIMITED };
};
static_assert(sizeof(Scheme) == 19, "Scheme must stay plain bytes");
// W4M Wormpot modes: the ids of SetupModes' jump table (0x5d6bc0, names FETXT.WPotName.* at 0x9205e0); 1 = empty reel.
// GameConfig::wormpot holds the three reels' ids, reel r in byte r (W4M FE.Wormpot.Reel1..3); 0 = empty too.
enum WormpotMode : uint8_t {
    WP_EMPTY = 1, WP_SUPER_EXPLOSIVES, WP_SUPER_CLUSTERS, WP_SUPER_ANIMALS, WP_SUPER_FIREARMS, WP_SUPER_MELEE, WP_WORMS_DROWN, WP_GOLIATH,
    WP_MAX_FALL, WP_DOUBLE_DAMAGE, WP_CRATE_SHOWER, WP_SPECIALIST, WP_NO_COWARDS, WP_MAX_HEALTH, WP_WIND_ALL, WP_ENERGY, WP_CRATE_DROPS,
    WP_STICKY, WP_SLIPPY, WP_LOW_GRAVITY, WP_NO_JUMPING, WP_TUG_O_WORMS, WP_WIND_GUNS, WP_QUICK_WALK, WP_NO_BLIMP, WP_MINE_RESPAWN,
    WP_MULTI_GIRDER, WP_DIM_MAK, WP_NO_BOMBING, WP_VAMPIRE, WP_VITAL_WORM, WP_SECRET_WEAPON, WP_DONOR_CARD, WP_GIRDERS_ONLY, WP_WIND_WORMS,
    WP_JUMPING_ONLY, WP_ONE_SHOT, WP_MODES
};
struct WormpotInfo { const char *key, *name, *help; };  // key: FETXT.WPotName.<key> / WPotHelp.<key>
extern const WormpotInfo WORMPOT_MODES[WP_MODES];   // [mode id]
extern const std::vector<int> WORMPOT_REEL[3];      // W4M reel lists 0x8ac960 / 0x8ac9c8 / 0x8aca28 (empty left out), mode ids
inline int wormpotReel(uint32_t wp, int r) { return int(wp >> 8 * r & 0xff); }  // reel r's mode id, < WP_SUPER_EXPLOSIVES: empty
inline uint32_t wormpotSet(uint32_t wp, int r, int mode) { return (wp & ~(0xffu << 8 * r)) | uint32_t(mode & 0xff) << 8 * r; }
inline uint64_t wormpotModes(uint32_t wp) {  // the picked modes, bit = mode id
    uint64_t m = 0;
    for (int r = 0; r < 3; r++) if (int k = wormpotReel(wp, r); k > WP_EMPTY && k < WP_MODES) m |= 1ull << k;
    return m;
}
inline bool wpOn(uint64_t modes, int mode) { return modes >> mode & 1; }
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
    uint32_t wormpot = 0;            // Wormpot reels: mode id per byte (wormpotReel)
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
    static float homingTime(const WeaponDef &d) { return d.avoid ? 30 : HOMING_TIME; }  // Factory HomingAvoidLand: Stage2Duration 30000 (0x598d97), LifeTime 30000
    static Vector3 homingStep(Vector3 v, Vector3 p, Vector3 aim, float max = HOMING_MAX) {
        Vector3 d = {aim.x - p.x, aim.y - p.y, aim.z - p.z};
        float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z), k = l > 0 ? HOMING_ACCEL * DT / l : 0;
        v = {v.x + d.x * k, v.y + d.y * k, v.z + d.z * k};
        float s = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z), c = s > max ? max / s : 1;
        return {v.x * c, v.y * c, v.z * c};
    }
    static constexpr float WIND_ACCEL = 4.25f;  // W4M payload accel += Wind.Speed (0x57eb18, IsAffectedByWind); Wind.MaxSpeed 8.5e-5 units/ms²
    static constexpr int JUMP_WINDOW = 18;  // W4M StartJump 0x5acd40: a jump waits 300 ms for a second press (backflip)
    // W4M, 20 units = 1 m: launch 0x5a5d30 (vy 0.15811 / vx 0.063246, backflip 0.2 / 0.031623 units/ms); Walk.Speed / 0.004 x 0.05 units/ms
    static constexpr float JUMP_UP = 7.9057f, JUMP_FWD = 3.1623f, FLIP_UP = 10, FLIP_BACK = -1.5811f, WALK_SPEED = 3.0625f;
    // W4M InputImpulse at full stick: 1/20 unit/ms whatever the walk speed (0x5ab5f3); the walk moves it x Walk.Speed / 0.004 (0x5b0fec)
    static constexpr float INPUT_IMPULSE = 2.5f;
    static constexpr float FWDFLIP_FWD = 1.5811f, VJUMP_UP = 9.354f;  // W4M forward flip vx 0.031623 (0x95fb88), vertical vy 0.18708 (0x95fb7c)
    // W4M DetectJump 0x5aefa0: release turns held (2) into tapped (0), a second press makes it a flip (1); all launch when the window ends.
    // Returns 0 while waiting, else the W4M event: 3 jump, 4 backflip, 5 forward flip, 6 vertical jump
    static int jumpTick(int &delay, uint8_t &kind, const Input &in, uint8_t pressed, float yaw, Vector3 &vel) {
        if (kind == 2 && !(in.buttons & Input::JUMP)) kind = 0;
        else if (kind == 0 && (pressed & Input::JUMP)) kind = 1;
        if (--delay > 0) return 0;
        // input . facing (0x5ab3d0); zero input flips forward: +0x159 is set on Activate, cleared only by Input.JumpBack (DEFSAVE Backflip option: Forwards)
        float along = in.buttons & Input::HEADING ? in.walk * cosf(PI * in.turn / 128 - yaw) : in.walk;
        float side = JUMP_FWD, up = JUMP_UP;  // tapped, or held with the stick forward
        int ev = 3;
        if (kind == 1) ev = along >= 0 ? 5 : 4, side = along >= 0 ? FWDFLIP_FWD : FLIP_BACK, up = FLIP_UP;
        else if (kind == 2 && along <= 0) ev = 6, side = 0, up = VJUMP_UP;
        vel = {sinf(yaw) * side, up, cosf(yaw) * side};
        return ev;
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
    static constexpr int ROPE_SHOTS = 5;     // W4M Ninja.NumShots: hooks on land per turn (0x573ea3; misses and objects are free)
    static constexpr float HOOK_SPEED = 50;  // 1 unit/ms (0x572800)
    // Ninja rope (W4M 0x574480 per 20 ms; shared with the AI's planner). hang: attach at `hook` (0x573d00); tick: reel by aim's
    // sign, swing by `swing` (-1, 0, 1), bounce or wrap, unwrap; release: the velocity on letting go (0x573530)
    static int ropeSwing(const Input &in, float yaw);  // 0x571820: the stick along (1) or against (-1) the facing
    void ropeHang(Rope &r, Vector3 hook, Vector3 feet, Vector3 vel, float yaw) const;
    void ropeTick(Rope &r, Vector3 &feet, Vector3 &vel, int swing, int8_t aim, int self, int body = -1) const;
    Vector3 ropeRelease(Rope r, Vector3 feet, int swing) const;
    bool ropeBlocked(Vector3 feet, int self, int body = -1) const;  // body: the hooked object's index
    void ropeOn(Vector3 hook);  // the active worm hangs from `hook`
    // grapple: fire from the eye along the aim, or (hooked since it last stood) along the motion (0x570650); step: 1 flying,
    // 2 on land at *hit, 3 on object *obj (only a standing worm, obj null: none), 0 retracted
    static Hook grappleFire(Vector3 feet, float yaw, float pitch, Vector3 vel, bool swung);
    int grappleStep(Hook &h, Vector3 feet, float maxLen, bool standing, Vector3 *hit, int *obj) const;
    static constexpr float SHEEP_WALK = 5, SHEEP_STEP = 4, SHEEP_TAKEOFF = 0.6f;  // walks-first super sheep: walk fuse s, m/s, take-off pitch
    // W4M Starburst: still for its 3500 ms fuse (0x588c97), then Starburst.Acceleration 0.01 units/ms a 20 ms frame (0x588ea0)
    static constexpr float STAR_FUSE = 3.5f, STAR_ACCEL = 25;
    static float starSpeed(float v, float max, bool wet) { return wet ? (v > 0 ? v - STAR_ACCEL * DT : v) : v < max ? v + STAR_ACCEL * DT : v; }
    static bool starLit(const Projectile &s) { return WEAPONS[s.weapon].fuse - s.fuse < STAR_FUSE - DT / 2; }  // fuse still burning
    // W4M WalkingPayload FloatAway (0x591ac0): expiry 6000 ms from the inflation, rising 0.08 units/ms (0x85d65c)
    static constexpr float SCOUSER_FLOAT = 6, SCOUSER_RISE = 4;
    static constexpr int STRIKE_BLITZ = 2000;  // W4M Bomber.BlitzDuration ms: N bombs, one every BlitzDuration / N (0x54dbf5, integer ms)
    static int strikeTicks(const WeaponDef &wd) { return (STRIKE_BLITZ / std::max(1, wd.clusters) * 60 + 500) / 1000; }
    static constexpr int FATKINS_BOUNCES = 3;      // W4M 0x554f88: the 4th contact rests it (DetonatesAtRest)
    static constexpr float FATKINS_STOP = 1.5f;   // Bounce.MinSpeed 0.03 units/ms
    static constexpr int STRIKE_LEAD = 240;    // W4M Bomber: the first DropBomb waits for the 4 s bombrun_start clip (0x54db75)
    static constexpr float STRIKE_EXTRA = 7;  // Bomber.ExtraHeight 140 units
    // W4M AlienAbductionLogicEntity states 1-6 (0x548710) as Projectile::stage; times from its clip table 0x546d00, HOLD = CloseBeam + Violate + OpenDoors - 250 ms
    enum : int { ABD_ARRIVING, ABD_LIFTING, ABD_HOLDING, ABD_SPITTING, ABD_LEAVING, ABD_FAILING };
    static constexpr float ABD_ARRIVE = 8.333f, ABD_HOLD = 8.956f, ABD_SPIT = 2.1f, ABD_LEAVE = 3.25f, ABD_FAIL = 9.833f, ABD_RISE = 11;  // RISE: ExtraHeight 220 units
    // WEAPTWK Abduction.*: AverageHeight 700 and MaxSpeed 3.4 units per 20 ms tick (speed = NormalSpeed), DistanceBetweenWorms 50; in 5 units it is aboard; spat at 0.28 units/ms
    static constexpr float ABD_AVG = 35, ABD_MAX = 8.5f, ABD_GAP = 2.5f, ABD_IN = 0.25f, ABD_OUT = 14;
    static constexpr int ZAP_FIRST = 1800, ZAP_MIN = 60, ZAP_SPAN = 180;  // W4M UpdateAbductee 0x5a9c40: 30 s, then Worm.Zap.MinTime 1000 .. MaxTime 4000 ms
    static constexpr float ZAP_XZ = 15, ZAP_Y = 15;                       // Worm.Zap.XZRange / YRange 300 units
    struct Abductee { int worm; uint8_t st; Vector3 from; };  // its waiting / moving lists in launch order: st 0 waiting, 1 rising, 2 aboard
    std::vector<Abductee> abductees;
    bool crated = false;     // stdlib done_once_per_turn_functions: DoPostActivity's first pass is done
    int camHold = 0;         // W4M FlyCam's "Hold the FlyCamera" active object: PauseDuration after its shot's blast
    bool active() const;     // W4M ObjectCount.Active > 0 (ActiveObjectRegistrationService 0x4d3af0 users), shots aside
    int abdCam = -1;  // its m_uCameraWorm (+0x74): the first worm lifted, the AlienAbductionCamera's
    bool aboard(int wi) const { for (const Abductee &a : abductees) if (a.worm == wi && a.st) return true; return false; }  // Worm.OverridePhysics
    const Projectile *ufo() const { for (const Projectile &s : shots) if (WEAPONS[s.weapon].kind == Kind::Abduction) return &s; return nullptr; }
    static constexpr float BOMBER_HEIGHT = 15, BOMBER_LEAD = 20, BOMBER_GAP = 0.8f, COW_CHUTE = 5;  // m, m, s (SuperBomber.DelayBetweenBombs), m/s
    // W4M Concrete Donkey 0x553370: 220 units/s^4 fall curve, 0.75 s back to the apex, 85 ms held after a smash, LifeTime 8000 ms
    static constexpr float DONKEY_CURVE = 11, DONKEY_HANG = 0.75f, DONKEY_LIFE = 8;
    static constexpr float DONKEY_MIN_HEIGHT = 75, DONKEY_EXTRA = 25;  // Donkey.MinHeight 1500, Donkey.ExtraHeight 500 units: it starts target + max(MinHeight, Land.MaxHeight + ExtraHeight) up (0x553700)
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
    int clock = 0;      // ticks played: sudden death after cfg.scheme.roundTime
    int hotSeat = 0;    // ticks left before the turn clock starts
    int jumpDelay = 0;  // jump pending: ticks left of the W4M window
    Vault vault;        // the active worm's ledge vault
    Vector3 walkVel{};  // the active worm's W4M Velocity while it walks: the last step's InputImpulse, 0 when idle
    Vector3 steerIn{};  // this tick's stick for the active worm's slide (transient)
    struct { int8_t swing = 0, aim = 0; } ropeIn;  // this tick's rope stick: ropeSwing(), the reel axis (transient)
    uint8_t jumpKind = 0;  // W4M DetectJump kind: 0 tapped, 1 pressed twice, 2 held
    bool selfHurt = false;  // the active worm took damage: its turn ends (W4M)
    float power = 0, wind = 0, windZ = 0;  // W4M Wind.Speed / Wind.MaxSpeed along x and z: (cos, sin) Wind.Direction (0x57eb25)
    bool roped = false, jetting = false;  // active utility: keeps the turn going; jetting = in flight
    bool jetUsed = false;  // the jetpack in hand has taken off (ammo spent, fuel live); W4M entity +0xf0
    float boost = 0;       // W4M Jetpack SuperThrust level 0..1 (+0xa0)
    // W4M JetpackUtilityLogicEntity 0x562810 (TWEAK Jetpack.*): ThrustScale 0.004 x 2 per 20 ms, FwdThrustRotation 0.3 rad,
    // MaxAltitude 2000 units over Water.Level, turn 2 x TurnRotationSpeed 0.0092 rad per 20 ms, fuel stops at 20 ms
    static constexpr float JET_TILT = 0.3f, JET_CEIL = 100, JET_TURN = 0.92f, JET_DRY = 0.02f, JET_BOUNCE = 0.8f;
    // OverCeilingThrustMod, XZWindResThrust / NoThrust per 20 ms; SuperThrust* under -0.15 units/ms (Mod 0.2 per units/ms = 0.004 per m/s)
    static constexpr float JET_OVER = 0.05f, JET_RES = 0.999f, JET_RES_IDLE = 0.95f, JET_FALL = 7.5f, BOOST_MOD = 0.004f, BOOST_ACCEL = 0.3f, BOOST_DECAY = 0.97f, BOOST_OFF = 0.01f;
    bool chute = false;                   // parachute open until the turn ends
    // W4M ParachuteLogicEntity: 70 ms of (Wind, Gravity x Low.Gravity.Multiplier) read at the opening (0x578db0, +0x88);
    // glide 0.06 units/ms along the facing (0x85bcc0), at most 0.05 units/ms of change a step (0x5792a0)
    Vector3 chuteDrift{};
    static constexpr float CHUTE_GLIDE = 3, CHUTE_STEP = 2.5f;
    // the canopy (+0x70, 2 m over the worm at the opening), its sway angle +0xac (rad) and spin +0xa8 (rad per 20 ms), the sink
    // +0x5c and the glide gain +0xc0 (m/s, 0 at Initialize 0x578587): the worm hangs 2 m under the canopy, swung sideways by the angle
    Vector3 chuteAt{};
    float chuteAng = 0, chuteSpin = 0, chuteSink = 0, chuteGain = 0;
    void chuteOpen(const Worm &w);
    // 0x578a40: the stick (InputImpulse, a fraction of full tilt) turns the canopy toward its side: 1 / -1, 0 none
    static int chuteSteer(Vector3 in, float yaw);
    Rope rope;  // the active worm's rope, or the hooked object's
    Hook grapple;  // the active worm's hook in flight
    int ropeUsed = -1;  // the rope that hooked this turn: its ammo goes at the cleanup (0x5727b0, +0x7b)
    float fuel = 0, ropeMax = 0, thrust = 0;  // max/thrust: of the tool in use, the hand may hold a weapon
    std::vector<int> fuses;  // per team: seconds set for userFuse weapons (W4M default 3)
    std::vector<std::vector<int>> delays;  // [team][weapon]: own turns left before it unlocks (W4M InventoryN.WeaponDelays)
    bool usable(int team, int wi) const { return ammo[team][wi] && !delays[team][wi] && allowed(team, wi); }
    bool allowed(int team, int wi) const;  // the team's active worm's WormData Allow* flags (0x50c800): Specialists, Tug O Worms
    std::vector<uint8_t> special;  // per worm: Wormpot.lub SetSpecialistTeam class 1..6, 0 none
    bool artillery() const { return wp(WP_TUG_O_WORMS); }  // WormData.ArtilleryMode (0x5d6a10): no walking, no jump (0x5ac390)
    bool jetLanded() const { return jetUsed && !jetting && WEAPONS[weapon].kind == Kind::Jetpack && fuel > JET_DRY; }  // 0x562f72: entity kept
    bool pickable(int team, int wi) const { return wi == weapon || usable(team, wi); }  // a direct pick (pick()) is legal
    bool selectable(int team, int wi) const;  // usable, and with a movement tool out only the tool itself or a toolDrop()
    bool abducting() const { return ufo(); }  // W4M EFMV.Active (0x548d0b): labels off
    bool toolOut() const;  // rope (or hooked object), jetpack in flight, open parachute in the air: W4M utility mode +0x8d
    // W4M m_eSecondaryWeapon (+0x98, flag +0x8c): a toolDrop() held besides the tool, dropped by Fire.Second; -1 none
    int secondary = -1;
    int launched = -1;  // the weapon use() last fired (a dropped secondary, not the tool kept in hand): its retreat times the turn
    int held() const { return secondary >= 0 ? secondary : weapon; }  // what NEXT_WEAPON and the panel step
    void firstWeapon(int team);  // W4M Weapon.Create 0x565770: the first usable item, Skip Go / Surrender skipped
    int shotsLeft = 0;
    int ropeShots = 0;  // rope hooks on land this turn
    Phase phase = Phase::Aim;
    uint8_t prevButtons = 0;
    uint32_t rng = 1;
    float water = Terrain::WATER;    // rises in sudden death; terrain.* keeps using the constant
    bool suddenDeath = false;
    Vector3 raceFinish{};  // rope race: terrain.finish, or a deterministic fallback
    std::vector<uint8_t> idle;  // per team: never takes a turn (mission captives)
    std::vector<uint8_t> surrendered;  // per team: W4M TeamData +0x72 (SurrenderTeam 0x5b4d00): no turn, no longer standing
    // W4M GirderKitLogicEntity 0x55ada0: a preview 2 m ahead at eye level, 0.3 m steps camera-relative, each axis within
    // Weapon.Girder.MaxDistance 300 units of the worm; FIRE welds GirderSmall.xom (4 x 2 x 4 voxels of 1 m, profile 2,1,1,2)
    static constexpr float GIRDER_STEP = 0.3f, GIRDER_RANGE = 15, GIRDER_CAM = 16.25f, GIRDER_CAM_PITCH = 0.6f, GIRDER_YAW = 0.4f;
    static constexpr int GIRDER_TICKS = 5, GIRDER_MAX = 45;  // 0x55ada0: a step per 4 updates of 20 ms (ret 0x14), 80 ms; Girder.TotalNumberPlaced < 45
    bool girderOn = false;  // preview up: GirderCam follows it, TARGET's walk / aim step it, PITCH's aim raises it
    Vector3 girder{}, girderFrom{};  // preview centre, the worm's position when it appeared
    int girderWait = 0, girders = 0;  // ticks to the next step, placed
    void stepGirder(const Input &in, uint8_t pressed);  // axis-aligned like W4M PC (no rotation); PITCH raises / lowers, ticks to the next step, placed
    int girderFits(Vector3 c) const;  // W4M 0x5590e0: probe box edges clear of land, 4 spheres clear of worms and objects
    // W4M BubbleTroubleLogicEntity 0x54f6e0: a falling shell around its spot; shots from outside bounce off, worms inside
    // ignore blasts centred outside Radius 42 units; Lifetime 6 turn ends; any blast inside pops it
    static constexpr float BUBBLE_R = 2.1f, BUBBLE_UP = 1.75f, BUBBLE_SHELL = 2.52f, BUBBLE_IN = 1.52f;  // Radius, OffsetY, Radius x 1.2, - 20
    struct Bubble { Vector3 pos, vel; int life; float yaw = 0; int age = 0, hit = -1, rest = 0; };  // pos: its base; centre BUBBLE_UP higher; age, hit: ticks (clips); rest: +0x79
    std::vector<Bubble> bubbles;
    int bubbleAt = -1;  // W4M Bubble.LaunchDelay 400 ms after FIRE: the tick Weapon.LaunchPayload.Callback (0x550190) spawns it
    void bubbleHit(Bubble &b) { if (b.age >= 90) b.hit = b.age, emit(GameEvent::BubbleHit, b.pos); }  // 0x54e480: not during WXM_Create (1.5 s)
    bool shielded(const Worm &w, Vector3 blast) const;  // in a bubble the blast is outside of
    // W4M RedbullUtilityLogicEntity 0x587390: after a jump, JUMP flaps up at FlapVelocity 0.15 u/ms in a 250 ms window every
    // 500 ms (Weapon.Redbull.MinTime / TimeLength); a flap zeroes the horizontal speed, the stick steers (WXWorm.Aftertouch*)
    static constexpr float FLAP = 7.5f, AFTERTOUCH = 0.75f, AFTERTOUCH_MAX = 5;
    static constexpr int FLAP_WAIT = 15, FLAP_BEAT = 30;
    int icarus = 0;  // 0 off, 1 drunk (on the ground), 2 flying, 3 drinking until flapAt; flapAt: tick the next window opens
    int flapAt = 0;
    Vector3 drift{};  // aftertouch velocity, kept across flaps
    bool doubleDamage = false;  // W4M SetData("DoubleDamage", 1) from a crate: every blast and hit this turn x2
    bool changing = false;  // W4M WormSelectLogicEntity +0x44: this Change Worm already spent its ammo; a move or jump ends it
    bool doubled() const { return doubleDamage || wp(WP_DOUBLE_DAMAGE); }  // the Wormpot mode sets it every turn
    std::vector<uint8_t> spy;   // per team: TeamDataContainer.IsCrateSpyActive, crate contents shown in its turns
    // W4M Binoculars 0x54bcc0: FIRE on a target seen from the eye solves the bazooka shot (no wind), shown after 4 s
    struct Scout { Vector3 at; float power, pitch; int t = -1; bool ok; };  // t: ticks since FIRE, -1 none
    Scout scout;
    static constexpr int SCOUT_TICKS = 240;
    bool scoutSolve(Vector3 at, float &power, float &pitch) const;
    float scoutZoom(float z, float dt) const;  // W4M HeadCam zoom 0x91f31c after dt s, from z (camera only)
    MissionRun run;
    // Settle (W4M stdlib.lub): GameLogic.ApplyDamage starts every hurt worm's damage display at once, then the death queue
    // blows the dead up one by one, each once nothing else is active (0x4f9b30). docs/death-sequence.md
    static constexpr int COUNT_DAMAGE = msTicks(2500);     // W4M Worm.DamageComplete posted 2500 ms after the damage (0x5abe94)
    static constexpr int COUNT_THROES = msTicks(3000);     // W4M kWPS_DeathThroes timer +0x128 (Worm.TimeToDie 0x5adbf0)
    static constexpr int DROWN_FLOAT = msTicks(2000);      // W4M kWPS_DrownFloat timer once at the surface (0x5aa222)
    static constexpr int POST_ACTIVITY = msTicks(2400);    // W4M PostActivityTime (LOCAL.XOM)
    std::vector<int> countGroup;  // worms whose damage display runs; countT: ticks since that ApplyDamage
    std::vector<int> deathQueue;  // W4M GameLogicService+0x1fc: the dead, in ApplyDamage order
    int countT = 0, dyingWorm = -1, throes = 0;  // the worm in kWPS_DeathThroes, ticks left
    int countTicks(int i) const { return std::min(90, std::max(1, std::abs(std::max(0, worms[i].hp) - worms[i].counted) * 3 / 2)); }
    bool drowned(int i) const { return worms[i].drowned; }
    int dying() const {  // the dying worm the camera shows: in its throes, else afloat (W4M "Worm Dying" 0x5a7190)
        if (dyingWorm >= 0) return dyingWorm;
        for (size_t i = 0; i < worms.size(); i++) if (worms[i].drowned && worms[i].counted > 0) return (int)i;
        return -1;
    }

    GameConfig cfg;
    uint64_t pot = 0;  // wormpotModes(cfg.wormpot)
    bool wp(int mode) const { return wpOn(pot, mode); }
    static constexpr int POISON_DEFAULT = 10;  // TWEAK Worm.Poison.Default
    bool mysteryWalk = false, mysteryLow = false;  // mystery Quick Walk / Low Gravity: Worm.VelocityScale 2, Low.Gravity.Multiplier 0.5 until the turn ends
    // W4M GunWobbleObject (GunWeaponLogicEntity +0x90, ctor 0x55f5b0, update 0x55f9e0): the gun's aim sways from its creation
    struct Wobble { float w[8], phase[8], freq[8], f = 1; int start = 0, weapon = -1; Vector2 at{}; };  // at: (pitch, yaw) rad
    Wobble wobble;
    void wobbleStep();
    std::vector<std::pair<Vector3, int>> respawns;  // Mine Respawn: GameLogic.RespawnMine at the mine's last position, ticks left (0x5812af, 500 ms)
    Vector2 wormWind() const { return wp(WP_WIND_WORMS) ? Vector2{wind * WIND_ACCEL * 0.5f, windZ * WIND_ACCEL * 0.5f} : Vector2{0, 0}; }  // x WormPot.WindScale 0.5
    float walkScale() const { return wp(WP_QUICK_WALK) || (mysteryWalk && !wp(WP_JUMPING_ONLY)) ? 2 : wp(WP_JUMPING_ONLY) ? 0 : 1; }  // Worm.VelocityScale, set by SetupModes 0x5d6bc0

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
    bool locked = false;  // homing target picked: FIRE on the aim ray (W4M 0x583a10 Payload.Target, any view but Default), then powered as usual
    Vector3 lockAt{};
    Vector3 blimpFocus(Vector3 ref, float yaw) const;  // W4M entry pose: above all land, its centre ray on ref
    Vector3 blimpEye(Vector3 focus, float yaw, float pitch = BLIMP_PITCH) const;
    bool blimpHit(Vector3 *hit) const;  // W4M CMS 0x51c910: land, then water, on the camera ray; false: no target
    // W4M Bomber 0x54d460: the plane flies ExtraHeight over all land and drops early, its bombs keep its speed; vel = the plane's
    Vector3 strikeStart(const WeaponDef &wd, Vector3 tgt, Vector3 dir, Vector3 &vel) const;
    float landTop() const;  // Land.MaxHeight: a column every 2 m
    Vector3 landCenter() const;
    Vector3 strikeDir() const { return {-cosf(cursorYaw), 0, sinf(cursorYaw)}; }  // the view's right: bombers cross the screen
    int retreatTicks(const WeaponDef &d) const { return msTicks(d.retreat >= 0 ? d.retreat : cfg.scheme.retreatTime * 1000); }
    int fallDamage(float speed) const;  // W4M FallDamage 0x5ac3e0: hp lost landing at `speed` m/s, scheme and Wormpot included
    struct GunHit { Vector3 at; float dist; int worm; bool land; };  // at: the struck worm's centre, else the ray's end; worm -1: none
    GunHit gunRay(Ray r, const Worm &shooter) const;  // W4M gun ray 0x55e10f: land, worms, targets, bubbles (dist 60: a miss)
    Blast gunBlast(int weapon) const;                  // one hit's explosion, Wormpot super firearms included
    bool steered() const;    // a live shot takes the stick (old woman, scouser, super sheep, Bovine Blitz)
    bool fireable(const Worm &w) const;  // the weapon in hand may fire now (W4M CanFire)
    bool ambulatory(const Worm &w) const { return w.grounded && !w.motion.slide && !vault.t && !jumpDelay; }  // W4M kWPS_Ambulatory (state 0)
    bool retreating() const;  // Flying / Retreat and the worm may move: W4M timer started (0x549bb0), no FlyCam holding WormMoving
    bool windy(int weapon) const;  // W4M IsAffectedByWind, Wormpot WindEffectMore included
    // W4M weapon enum id (0x90c920) of a shot's WEAPTWK container, 0 none
    static constexpr int W4M_LANDMINE = 8, W4M_FACTORY = 21, W4M_SENTRY = 27;
    int containerOf(int weapon, bool child) const;
    int inventoryId(int weapon) const;  // W4M inventory slot (enum 0x90c920, utilities 0x22..0x2f), 0 none
    Vector2 superScale(int container) const;  // x: WormDamageMagnitude and LandDamageRadius, y: ImpulseMagnitude
    Blast superBlast(Blast b, int container) const;  // b under its container's superScale
    std::vector<int> superWeapon;  // per team: TeamData.WormpotSuperWeapon (Super Secret Weapons), a container id, 0 none
    float fuseOf(const WeaponDef &wd) const { return wd.userFuse ? fuses[worms[current].team] : wd.fuse; }

private:
    float rand01();
    float mineFuse();  // a new mine's fuse, s (Scheme mineFuse, random: one draw)
    void beginTurn(int team);
    void ropeCleanup();  // the rope entity goes: a hooked rope spends its ammo
    // stdlib.lub turn end: ApplyDamage, CheckActivity, DoPostActivity's two passes; ApplyPoison 0x5ac060; SurrenderTeam 0x5b4d00
    void applyDamage(const std::vector<int> *type6 = nullptr);  // type6: ApplyPoison's damage, no display but the vampire's
    std::vector<int> applyPoison();
    void checkActivity();
    void doPostActivity();
    void stepCount();
    void floatStep(Worm &w);
    void surrender(int team);
    void dropCrates(int n, bool roll);
    bool standing(int team) const;
    void nextWeapon(int team);
    void pick(int team, int k);  // W4M WeaponSelected 0x565d30
    void use(Worm &w);
    void emit(GameEvent::Kind k, Vector3 p, int worm = -1, int weapon = -1) { events.push_back({k, p, worm, weapon}); }
    void blastLand(Vector3 p, float r) { if (terrain.carve(p, r)) emit(GameEvent::Debris, p); }  // W4M Land Explosion handler 0x473530: weapons/Debris once Land.Changed
    void stepWorm(Worm &w);
    void avoidLand(Projectile &s);  // HomingAvoidLand steering (W4M 0x5611b0)
    bool stepUfo(Projectile &s);  // false once it has left
    void zapStep(Worm &w);
    void drown(Worm &w);
    void vapourize(Worm &w);
    bool underwater(const Worm &w) const;
    void stepShots(const Input &in, bool detonate);
    void explode(Vector3 p, const Blast &b, float poison = 0, int type = 0, int weapon = -1);  // weapon: carried by the Boom event (the donkey's blast has its own effects)
    void steal(const Worm &victim);  // old woman ammo theft
    void impulse(Worm &o, Vector3 v);  // direct-hit knock (gun, melee): sets the velocity, x2 under Double Damage
    void hurt(Worm &w, int dmg, bool blast = false, int type = 0);  // vampire/karma/highlander for the active worm; blast: armour applies
    Vector3 placeWorm(const struct Grid &gr, struct NodeCache &nc, float &yaw);  // a worm's feet
    bool dropPoint(Object::Type t, Vector3 &out, float radius = -1);  // radius: the sphere 0x4f26b0 checks, -1 the object's
    bool addObject(Object::Type t, float lift);
    void stepObjects();
    void openMystery(int item, Worm &w);  // CrateLogicEntity 0x5ca1f0
    void poisonWorm(Worm &w) { if (!w.poison) w.poison = POISON_DEFAULT, w.abducted = false; }  // Worm.Poison 0x5add14: only an unpoisoned worm
    Object *hooked();  // the object on the rope, if any
};
void missionStart(Game &g);  // mission.cpp: worms, ammo, objects from cfg.mission
void missionStep(Game &g);   // mission.cpp: sequences, objectives, result

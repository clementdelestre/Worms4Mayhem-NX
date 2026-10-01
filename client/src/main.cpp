#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "ai.h"
#include "audio.h"
#include "controls.h"
#include "fx.h"
#include "lanhost.h"
#include "mission.h"
#include "models.h"
#include "net.h"
#include "replay.h"
#include "sim.h"
#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#define ROMFS_DIR "romfs:/"
#else
#define DATA_DIR "./"
#define ROMFS_DIR "./romfs/"
#endif

extern "C" void glFinish(void);  // perf overlay only; rlgl does not wrap it

static const Color TEAM_COLORS[] = {{220, 50, 50, 255}, {50, 110, 230, 255}, {60, 190, 70, 255}, {240, 200, 40, 255}};

static bool pressedAny(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys) { return Ui::pressed(pad, buttons, keys); }

// Scripted input for shot mode: select weapon, aim up, charge, release.
static Input scriptInput(int frame, int weapon) {
    Input in;
    if (frame < 2 * weapon && frame % 2) in.buttons = Input::NEXT_WEAPON;
    if (frame > 10 && frame < 30) in.aim = 127;
    if (frame > 40 && frame < 100) in.buttons = Input::FIRE;
    return in;
}

static void drawTextCentered(const char *t, int x, int y, int size, Color c) {
    Ui::text(t, x, y, size, c, 1);
}

static void onEvent(const Game &g, const GameEvent &e) {
    using Audio::Sfx;
    using Audio::Voice;
    int team = e.worm >= 0 ? g.worms[e.worm].team : 0;
    Fx::event(e, g.terrain.side);
    switch (e.kind) {
    case GameEvent::Boom: Audio::play(Sfx::Explosion); break;
    case GameEvent::BigBoom: Audio::play(Sfx::Holy); Audio::play(Sfx::BigExplosion); break;
    case GameEvent::Fire: {
        const WeaponDef &d = WEAPONS[e.weapon];
        Kind k = d.kind;
        static const Sfx FIRE_SFX[] = {Sfx::Fire, Sfx::Sheep, Sfx::Airstrike, Sfx::Donkey, Sfx::Shotgun, Sfx::Rope, Sfx::Fire, Sfx::Teleport,
                                       Sfx::SuperSheepFire, Sfx::OldWomanFire, Sfx::Bounce, Sfx::Homing, Sfx::Tick, Sfx::ScouserFire, Sfx::SentryFire,
                                       Sfx::Abduction, Sfx::Flood, Sfx::Parachute, Sfx::TurnStart, Sfx::TurnStart, Sfx::TurnStart};  // by Kind
        Sfx s = FIRE_SFX[(int)k];
        // a few Kinds cover several named weapons with distinct W4M sounds; pick by name like heldModel() does
        if (k == Kind::Shell) s = d.name == "Poison Arrow" ? Sfx::Bow : d.name == "Dynamite" ? Sfx::Dynamite : d.name == "Gas Canister" ? Sfx::Gas : s;
        else if (k == Kind::Melee) s = d.name == "Baseball Bat" ? Sfx::BatSwing : d.name == "Prod" ? Sfx::Prod : Sfx::FirePunch;
        else if (k == Kind::Shotgun && d.name == "Sniper Rifle") s = Sfx::Sniper;
        else if (k == Kind::Sentry && e.worm >= 0) s = Sfx::SentryPlace;  // placing vs. the turret's own shots (worm -1)
        Audio::play(s);
        bool utility = k == Kind::Rope || k == Kind::Jetpack || k == Kind::Teleport || k == Kind::Parachute || k == Kind::SkipGo || k == Kind::ChangeWorm;
        if (e.worm >= 0 && !utility) Audio::voice(team, Voice::Fire);  // worm -1: sentry gun shot
        break;
    }
    case GameEvent::Bounce: Audio::play(Sfx::Bounce, 0.6f); break;
    case GameEvent::Splash: Audio::play(Sfx::Splash); break;
    case GameEvent::Death: Audio::voice(team, Voice::Death); break;
    case GameEvent::Hurt: Audio::voice(team, Voice::Hurt); break;
    case GameEvent::Jump: Audio::play(Sfx::Jump); Audio::voice(team, Voice::Jump); break;
    case GameEvent::TurnStart: Audio::play(Sfx::TurnStart); Audio::voice(team, Voice::Idle); break;
    case GameEvent::CrateDrop: Audio::play(Sfx::CrateLand, 0.5f); break;
    case GameEvent::Collect: Audio::play(Sfx::Pickup, 0.7f); break;
    case GameEvent::MineArm: Audio::play(Sfx::MineBeep); break;
    case GameEvent::GameOver:
        Audio::music(true, "victory");
        if (g.winner >= 0) Audio::voice(g.winner, Voice::Victory);
        break;
    }
}

// Mesh held at the worm's WeaponLocator while aiming, and the Aim/Hold clip that shows the hands.
static const char *heldModel(const WeaponDef &d, const char **clip) {
    const std::string &n = d.name;
    switch (d.kind) {
    case Kind::Shell:
        if (n == "Poison Arrow") { *clip = "AimBow"; return "hold_bow"; }
        if (n == "Dynamite") { *clip = "HoldDynamite"; return "hold_dynamite"; }
        *clip = d.fuse > 0 ? "AimGrenade" : "AimBazooka";
        return d.fuse <= 0 ? "hold_bazooka" : n == "Cluster Grenade" ? "hold_cluster" : n == "Banana Bomb" ? "hold_banana"
             : n == "Holy Hand Grenade" ? "hold_holy" : n == "Gas Canister" ? "hold_gas" : "hold_grenade";
    case Kind::Shotgun: *clip = n == "Sniper Rifle" ? "AimSniper" : "AimShotgun"; return n == "Sniper Rifle" ? "hold_sniper" : "hold_shotgun";
    case Kind::Homing: *clip = "AimHomingMissile"; return "hold_homing";
    case Kind::Sheep: *clip = "HoldBazooka"; return "hold_sheep";  // HoldSheep tilts the whole worm with our clip layering
    case Kind::SuperSheep: *clip = n == "Starburst" ? "HoldStarburst" : "HoldBazooka"; return n == "Starburst" ? "hold_starburst" : "hold_supersheep";
    case Kind::OldWoman: *clip = "HoldOldWoman"; return "hold_oldwoman";
    case Kind::Scouser: *clip = "HoldScouser"; return "hold_scouser";
    case Kind::Melee: *clip = n == "Baseball Bat" ? "HoldBat" : n == "Prod" ? "HoldProd" : "HoldFirepunch"; return n == "Baseball Bat" ? "hold_bat" : "";  // "": hands only
    case Kind::Mine: *clip = "HoldLandmine"; return "hold_landmine";
    case Kind::Sentry: *clip = "HoldSentrygun"; return "hold_sentry";
    case Kind::Surrender: *clip = "HoldSurrender"; return "hold_flag";
    case Kind::SkipGo: *clip = "HoldSkipGo"; return "";
    case Kind::Airstrike: case Kind::Donkey: case Kind::Abduction: case Kind::Flood: *clip = "HoldAirstrike"; return "hold_radio";
    case Kind::Rope: *clip = "HoldNinjarope"; return "hold_rope";
    default: return nullptr;
    }
}

// Render-only gait state: the sim walks worms by moving pos (vel stays 0), so the cycle follows position deltas.
struct WormAnim { Vector3 pos{}; float yaw = 0, walk = 0, still = 1, air = 0, fallV = 0, land = 9; bool init = false, moving = false, flip = false; };
static std::vector<WormAnim> wormAnims;

// Once per frame, for every worm: walk phase advances with distance walked, footsteps on the cycle beat, landing thud.
static void animateWorms(const Game &g, float dt) {
    wormAnims.resize(g.worms.size());
    const float L = fmaxf(Models::clipLength("worm", "Walk"), 0.1f);
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &w = g.worms[i];
        WormAnim &a = wormAnims[i];
        Vector3 d = Vector3Subtract(w.pos, a.pos);
        float dist = sqrtf(d.x * d.x + d.z * d.z), turn = fabsf(remainderf(w.yaw - a.yaw, 2 * PI));
        if (!a.init || dist > 2) a = WormAnim{}, a.init = true, dist = turn = 0;  // spawn, teleport, replay rewind
        a.pos = w.pos, a.yaw = w.yaw;
        if (!w.grounded && fabsf(w.vel.y) > 1) {  // ignore slope-contact flicker
            if (a.air == 0) a.flip = w.vel.x * sinf(w.yaw) + w.vel.z * cosf(w.yaw) < -0.5f;  // backflip leaves backwards
            a.air += dt, a.fallV = fminf(a.fallV, w.vel.y), a.walk = 0, a.moving = false;
            continue;
        }
        if (a.air > 0.2f && a.fallV < -4 && w.alive) a.land = 0, Audio::play(Audio::Sfx::Land, fminf(-a.fallV / 20, 0.5f));
        a.air = a.fallV = 0, a.land += dt;
        float step = dist + turn * 0.6f;  // turning in place shuffles at half the walk pace
        a.still = step > 1e-4f ? 0 : a.still + dt;
        a.moving = w.alive && a.still < 0.1f;  // bridges render frames that ran no sim tick
        float before = a.walk, B = 0.4f * L;   // one body surge per cycle; B: where it lands
        if (a.moving) a.walk += step / 3;      // in-place clip, 1x at the 3 u/s walk speed
        else if (a.walk > 0) a.walk = a.walk < L / 2 ? fmaxf(a.walk - dt, 0) : a.walk + dt >= L ? 0 : a.walk + dt;  // ease to the upright pose
        bool beat = floorf((a.walk - B) / L) > floorf((before - B) / L);
        a.walk = fmodf(a.walk, L);
        bool self = (int)i == g.current && (g.phase == Phase::Aim || g.phase == Phase::Retreat) && Vector3Length(w.vel) < 0.3f;  // not sliding
        if (a.moving && beat && self) Audio::play(Audio::Sfx::Step, 0.3f);
    }
}

// W4M worm: team-tinted, animation picked from the sim state (aim clips map pitch to their timeline).
static bool drawWorm(const Game &g, const Worm &w, float clock) {
    int i = int(&w - g.worms.data());
    WormAnim a = i < (int)wormAnims.size() ? wormAnims[i] : WormAnim{};
    float speed = sqrtf(w.vel.x * w.vel.x + w.vel.z * w.vel.z), t = clock;  // shared timeline: idle worms reuse one skinned pose
    float fidget = fmodf(clock + i * 7.3f, 25);  // desynchronised per worm
    const char *clip = "Base", *held = nullptr;
    bool loop = true;
    bool tool = i == g.current && (g.roped || g.jetting);
    if (a.air > 0 && speed > 4 && !tool) clip = "Blastflight2";  // knocked flying
    else if (a.air > 0) {
        clip = a.flip ? "Backflip" : w.vel.y > 0 ? "Jump" : "Fall";
        if (w.vel.y > 0 || a.flip) t = a.air, loop = false;
    } else if (a.moving || a.walk > 0) clip = "Walk", t = a.walk;
    else if (a.land < Models::clipLength("worm", "Land") && w.hp > 0) clip = "Land", t = a.land, loop = false;
    else if (w.hp <= 0) clip = "Wave";  // bye-bye until Settle blows it up
    else if (g.phase == Phase::GameOver && w.team == g.winner) clip = "Victorious_Grin";
    else if (i == g.current && g.phase == Phase::Aim && !g.roped && !g.jetting && (held = heldModel(WEAPONS[g.weapon], &clip))) {
        if (clip[0] == 'A') t = (w.pitch + 1.2f) / 2.65f * Models::clipLength("worm", clip), loop = false;  // sim pitch range [-1.2, 1.45]
    } else if (w.hp < 25) clip = "Wounded";
    else if (fidget < Models::clipLength("worm", i % 2 ? "Yawn" : "ScratchHead")) clip = i % 2 ? "Yawn" : "ScratchHead", t = fidget, loop = false;
    Color tint = ColorLerp(WHITE, TEAM_COLORS[w.team], 0.5f);
    Vector3 p = {w.pos.x, w.pos.y - Game::R, w.pos.z};
    if (!Models::draw("worm", p, w.yaw, 0, tint, clip, t, loop)) return false;
    Matrix m;
    if (held && Models::joint("worm", "WeaponLocator", clip, t, loop, &m))
        Models::draw(held, MatrixMultiply(m, MatrixMultiply(MatrixRotateY(w.yaw), MatrixTranslate(p.x, p.y, p.z))));
    int hat = w.team < (int)g.cfg.teamSetup.size() ? g.cfg.teamSetup[w.team].hat : 0;  // cosmetic only: index resolved against this client's own sorted hat list
    if (hat && Models::joint("worm", "HatLocator", clip, t, loop, &m))
        Models::draw(Models::hatName(hat - 1), MatrixMultiply(m, MatrixMultiply(MatrixRotateY(w.yaw), MatrixTranslate(p.x, p.y, p.z))));
    return true;
}

// Dead worms leave their team's W4M gravestone, dropped onto whatever terrain is left below (render only).
static void drawGrave(const Game &g, const Worm &w) {
    if (w.pos.y < g.water) return;  // drowned
    Vector3 p = {w.pos.x, w.pos.y - Game::R, w.pos.z};
    for (int k = 0; k < 100 && p.y > g.water && !g.terrain.solid(p); k++) p.y -= 0.1f;
    Models::draw(TextFormat("grave%d", w.team % 4), p, w.yaw);
}

// Shells point along their velocity; fused throwables (grenades) tumble instead.
static bool drawShot(const Projectile &s, float clock) {
    const WeaponDef &d = WEAPONS[s.weapon];
    const std::string &n = d.name;
    if (s.child && d.kind == Kind::SuperSheep) return false;  // starburst stars: placeholder spheres
    const char *m = n == "Fatkins Strike" ? "fatkins" : n == "Starburst" ? "starburst" : n == "Poison Arrow" ? "arrow" : n == "Dynamite" ? "dynamite"
                  : n == "Gas Canister" ? "gas" : d.kind == Kind::SuperSheep ? "supersheep" : d.kind == Kind::OldWoman ? "oldwoman"
                  : d.kind == Kind::Homing ? "homing" : d.kind == Kind::Scouser ? "scouser"
                  : d.kind == Kind::Sheep ? "sheep" : d.kind == Kind::Donkey ? "donkey" : d.kind == Kind::Airstrike ? "airstrike"
                  : n == "Cluster Grenade" ? (s.child ? "clusterlet" : "cluster") : n == "Banana Bomb" ? (s.child ? "bananette" : "banana")
                  : n == "Holy Hand Grenade" ? "holy" : d.fuse > 0 ? "grenade" : "bazooka";
    if (!d.model.empty()) m = d.model.c_str();  // Weapon Factory
    float h = sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z), yaw = atan2f(s.vel.x, s.vel.z);
    if (d.kind == Kind::Sheep || d.kind == Kind::Donkey) return Models::draw(m, s.pos, yaw, 0, WHITE, "Run", clock);
    if (d.kind == Kind::OldWoman) return Models::draw(m, {s.pos.x, s.pos.y - 0.3f, s.pos.z}, yaw, 0, WHITE, "Walk", clock);
    if (d.kind == Kind::SuperSheep && n != "Starburst") return Models::draw(m, s.pos, yaw, atan2f(s.vel.y, h), WHITE, "Fly", clock);
    if (d.kind == Kind::Scouser) return Models::draw(m, s.pos, clock * 0.7f);
    if (d.fuse > 0 && d.kind == Kind::Shell) return Models::draw(m, s.pos, clock * 6, clock * 4);
    return Models::draw(m, s.pos, yaw, atan2f(s.vel.y, h));
}

enum class Screen { Menu, Lobby, Play, Replays, Missions };

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--netbot")) SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1280, 720, "Worms4NX");
    SetExitKey(KEY_NULL);  // Esc is back / pause; quit from the title screen
    SetTargetFPS(60);
    rlSetClipPlanes(0.5, 500);  // default 0.01 near plane z-fights the water on GLES depth buffers
    Audio::init();
    Audio::music(true);
    Models::load();
    Ui::load();
    Controls::load(DATA_DIR "controls.txt");
    Fx::load();

    // Shot mode (flag file or --shot): scripted turn, screenshot, quit. Lets us check rendering in the emulator.
    // --cpu [map] [level]: every team is played by the AI (until the team setup menu lands)
    // --ui title|main|setup|options|hud|panel [map]: capture that screen to ui.png and quit
    const char *uiShot = argc > 2 && !strcmp(argv[1], "--ui") ? argv[2] : nullptr;
    // --bench <map> [frames] [nosync]: CPU-vs-CPU match, uncapped, one sim tick per frame, prints per-section ms and exits
    bool bench = argc > 2 && !strcmp(argv[1], "--bench");
    int benchFrames = bench && argc > 3 ? atoi(argv[3]) : 1200;
    // --netbot host port name create|join [turns] [map] [rules] [scheme] [roundMin]: own teams played by the AI online,
    // checksums on stdout, exit 1 on desync. The token is kept in ./netbot.token so a restarted bot resumes its match.
    bool netbot = argc > 5 && !strcmp(argv[1], "--netbot"), botCreate = netbot && !strcmp(argv[5], "create"), desynced = false;
    int botTurns = netbot && argc > 6 ? atoi(argv[6]) : 6, turns = 0;
    bool cpuAll = argc > 1 && (!strcmp(argv[1], "--cpu") || bench), shot = !cpuAll && !uiShot && !netbot && (argc > 1 || FileExists(DATA_DIR "shot"));
    int shotWeapon = argc > 2 ? atoi(argv[2]) : 0;  // --shot N: use weapon N
    if (!loadWeapons(ROMFS_DIR "weapons.json")) TraceLog(LOG_WARNING, "weapons.json missing or invalid, using built-in weapons");

    // server.txt on the SD card: "<host> [port] [name]", rewritten by the Options screen
    std::string host = "127.0.0.1", name = "Worm";
    int port = 7777;
    if (char *txt = LoadFileText(DATA_DIR "server.txt")) {
        char h[64] = "", n[32] = "";
        int k = sscanf(txt, "%63s %d %31s", h, &port, n);
        if (k >= 1) host = h;
        if (k >= 3) name = n;
        UnloadFileText(txt);
    }

    Game game;
    Net net;
    bool online = netbot;
    Screen screen = shot ? Screen::Play : netbot ? Screen::Lobby : Screen::Menu;
    int roomSel = 0, lastSec = -1;
    GameConfig opt;
    std::vector<std::string> maps = {""};  // "" = procedural island
    for (const char *dir : {ROMFS_DIR "maps", DATA_DIR "assets/maps"}) {  // assets/ = maps imported from the user's W4M install
        if (!DirectoryExists(dir)) continue;
        FilePathList files = LoadDirectoryFilesEx(dir, ".json", false);
        for (unsigned i = 0; i < files.count; i++) {
            std::string m = GetFileNameWithoutExt(files.paths[i]);
            if (std::find(maps.begin(), maps.end(), m) == maps.end()) maps.push_back(m);
        }
        UnloadDirectoryFiles(files);
    }
    uint32_t tick = 0;
    std::string status;
    std::map<uint32_t, float> offlineSince;  // player id -> clock when seen offline
    // --shot [weapon] [map] [rules]
    // the shot flag file may name the map (Switch has no args)
    std::string shotMap = argc > 3 ? argv[3] : "";
    // --view <map> x y z tx ty tz: shot mode from a fixed camera (overview captures)
    bool fixedView = argc > 8 && !strcmp(argv[1], "--view");
    Camera3D viewCam = {{0, 0, 0}, {0, 0, 0}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    if (fixedView) {
        shotMap = argv[2];
        viewCam.position = {(float)atof(argv[3]), (float)atof(argv[4]), (float)atof(argv[5])};
        viewCam.target = {(float)atof(argv[6]), (float)atof(argv[7]), (float)atof(argv[8])};
    }
    if (char *t = argc <= 3 && FileExists(DATA_DIR "shot") ? LoadFileText(DATA_DIR "shot") : nullptr) {
        char m[64] = "";
        if (sscanf(t, "%63s", m) == 1) shotMap = m;
        UnloadFileText(t);
    }
    if (shot) { game.start({1234, 2, 2, shotMap, argc > 4 && !fixedView ? (uint32_t)atoi(argv[4]) : 0u}); game.terrain.remesh(); Fx::theme(game.terrain.theme, game.terrain.sky, game.terrain.time); }

    Camera3D cam = {{40, 30, 0}, {40, 8, 40}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    float acc = 0, clock = 0, reconnectAt = 0;
    // perf overlay (L+R / F3 cycles off, CPU, GPU-synced): ms per section, smoothed. CPU mode only times command
    // submission (GPU work lands in "present"); synced mode glFinish()es after each section to charge the GPU cost to it.
    enum { T_SIM, T_REMESH, T_SKY, T_TERRAIN, T_DECOR, T_MODELS, T_FX, T_UI, T_PRESENT, T_COUNT };
    static const char *NAMES[T_COUNT] = {"sim", "remesh", "sky+water", "terrain", "decor", "models", "fx", "ui", "present+wait"};
    double perf[T_COUNT] = {}, cost[T_COUNT] = {}, cpuCost[T_COUNT] = {}, mark = 0;
    int perfOn = bench ? (argc > 4 ? 1 : 2) : 0;  // --bench map frames nosync: real fps, no glFinish
    auto lap = [&](int k) {
        if (perfOn == 2) rlDrawRenderBatchActive();
        double c = GetTime();  // before glFinish: CPU submission only
        if (perfOn == 2) glFinish();
        double t = GetTime();
        cpuCost[k] += c - mark, cost[k] += t - mark, mark = t;
    };
    double benchSum[T_COUNT] = {}, benchCpu[T_COUNT] = {}, benchMax[T_COUNT] = {}, benchStart = 0, frameMax = 0, frameStart = 0;

    // Match recording (saved to replays/ at game over or quit), Replays playback, instant replay of big shots (local only).
    Recording rec, play;
    Snapshot snap;  // turn start, for the instant replay
    bool recSaved = true, playing = false, paused = false, freeCam = false, instant = true, shotDone = false;
    int speed = 1, irEnd = -1, replaySel = 0;  // irEnd: live tick the instant replay catches up to, -1 = live
    uint32_t irTick = 0, irLive = 0, fireTick = 0;
    float irAcc = 0, fcYaw = 0, fcPitch = 0;
    Vector3 fcPos{};
    std::vector<std::string> replayFiles;
    if (char *t = FileExists(DATA_DIR "replay.txt") ? LoadFileText(DATA_DIR "replay.txt") : nullptr) instant = t[0] != '0', UnloadFileText(t);
    auto saveRec = [&] {
        if (recSaved || playing || rec.inputs.empty() || shot || bench || uiShot || netbot || rec.cfg.mission) return;  // replays don't carry missions
        recSaved = true;
        rec.checksum = irEnd < 0 && rec.inputs.size() == tick ? game.checksum() : 0;
        MakeDirectory(DATA_DIR "replays");
        std::string path = DATA_DIR "replays/" + replayName(rec.cfg);
        if (!rec.save(path)) TraceLog(LOG_WARNING, "cannot save %s", path.c_str());
    };
    auto irFinish = [&] {  // the replayed ticks end on the live state (determinism)
        if (irEnd < 0) return;
        for (; irTick < (uint32_t)irEnd; irTick++) game.step(rec.inputs[irTick]);
        if (game.checksum() != irLive) TraceLog(LOG_ERROR, "instant replay diverged from the live state");  // local only: keep playing
        irEnd = -1;
        Fx::clear();
    };
    int livePad = -1;  // pad of the human playing this turn here, -1 none
    auto feel = [&](const GameEvent &e) {  // HD rumble, local players' own pads only
        if (playing || shot || bench || netbot || uiShot) return;
        auto padOf = [&](int team) {
            if (team < (int)game.cfg.teamSetup.size() && game.cfg.teamSetup[team].cpu) return -1;
            if (online) return team < (int)net.owners.size() && net.owners[team] == net.id ? 0 : -1;
            return IsGamepadAvailable(team) ? team : 0;
        };
        int view = online ? 0 : livePad >= 0 ? livePad : 0;
        if (e.kind == GameEvent::Fire && e.worm >= 0) Controls::rumble(livePad, 0.3f, 0.08f);
        if (e.kind == GameEvent::Boom || e.kind == GameEvent::BigBoom)
            Controls::rumble(view, (e.kind == GameEvent::BigBoom ? 1 : 0.8f) * Clamp(1 - Vector3Distance(e.pos, cam.target) / 30, 0, 1), 0.3f);
        if (e.kind == GameEvent::Hurt && e.worm >= 0) Controls::rumble(padOf(game.worms[e.worm].team), 0.9f, 0.2f);
    };
    auto stepOnce = [&](const Input &in) {
        Phase was = game.phase;
        game.step(in);
        tick++;
        if (!playing) rec.inputs.push_back(in);
        shotDone |= was == Phase::Flying && game.phase != Phase::Flying;
        for (const GameEvent &e : game.events) {
            onEvent(game, e);
            feel(e);
            if (e.kind == GameEvent::Fire && e.worm >= 0) fireTick = tick;
            if (e.kind == GameEvent::TurnStart && !online && !playing) snap.take(game, tick);
            if (e.kind == GameEvent::GameOver) saveRec();
            if (e.kind != GameEvent::TurnStart && e.kind != GameEvent::GameOver) continue;
            if (online) net.turnEnd(tick, game.checksum());
            if (netbot) printf("[%s] turn %d tick %u checksum %08x%s\n", name.c_str(), ++turns, tick, game.checksum(), e.kind == GameEvent::GameOver ? " gameover" : "");
        }
    };
    auto startMatch = [&](const GameConfig &c) {
        game.terrain.undo = nullptr;
        snap.valid = false;
        rec = {c, {}, 0};
        recSaved = shotDone = false;
        irEnd = -1;
        game.start(c);
        Audio::music(true, game.terrain.theme.empty() ? "theme" : game.terrain.theme.c_str());
        game.terrain.remesh();
        Fx::theme(game.terrain.theme, game.terrain.sky, game.terrain.time);
        Fx::clear();
        Audio::preloadVoices(game.teams);
        tick = 0;
        acc = 0;
        screen = Screen::Play;
    };
    bool botStarted = false;
    float botAt = 0, botDone = 1e9f;
    int proxyTurn = -1, botSpeed = getenv("W4NX_SPEED") ? atoi(getenv("W4NX_SPEED")) : 8;  // ticks per frame
    // LAN: this console hosts (embedded relay, its own Net joins over loopback) or joins a game heard on the LAN
    LanHost lanHost;
    LanScan lanScan;
    bool lan = false;
    int lanSel = 0;
    std::string connHost = host;  // where `net` connects / reconnects (host:port stays the Options server)
    int connPort = port;
    auto leaveLan = [&] { net.close(), lanHost.close(), net.roomId = 0, net.players.clear(), net.profiles.clear(); };
    if (netbot) {  // host "lan": create = host the LAN game on `port`, join = find it by its beacon
        host = argv[2], port = atoi(argv[3]), name = argv[4];
        lan = host == "lan";
        connHost = lan ? "127.0.0.1" : host, connPort = port;
        if (FILE *f = fopen("netbot.token", "r")) botStarted = fscanf(f, "%llx", (unsigned long long *)&net.token) == 1, fclose(f);
        opt.map = argc > 7 ? argv[7] : "";
        opt.rules = argc > 8 ? (uint32_t)atoi(argv[8]) : 0;
        if (argc > 9) opt.scheme = SCHEMES[atoi(argv[9]) % SCHEMES.size()].s;
        if (argc > 10) opt.scheme.roundTime = atoi(argv[10]);
        opt.teamSetup = {{"Bot " + name}};
        if (getenv("W4NX_CPU")) opt.teamSetup.push_back({"CPU", (uint8_t)atoi(getenv("W4NX_CPU"))});  // host's CPU team level
        opt.teams = (int)opt.teamSetup.size();
        if (lan && botCreate && !lanHost.open(port, "netbot")) return printf("[%s] cannot host on port %d\n", name.c_str(), port), 1;
        if (lan && !botCreate) lanScan.open();
        else if (!net.connect(connHost.c_str(), connPort, name.c_str())) return printf("[%s] cannot reach %s\n", name.c_str(), host.c_str()), 1;
    }
    if (cpuAll) {
        if (bench) SetTargetFPS(0), opt.seed = 1234;
        opt.map = argc > 2 ? argv[2] : "";
        opt.teamSetup.assign(4, {"CPU", (uint8_t)(!bench && argc > 3 ? atoi(argv[3]) : 2)});
        startMatch(opt);
    }
    Ai ai;
    Ui::Frontend front;
    Ui::Hud hud;
    Ui::Pause pause;
    // Single player: missions list (loaded on first use), progress.txt, the end-of-mission screen's choice
    std::vector<MissionSpec> missions;
    Progress progress;
    Ui::MissionMenu missionMenu;
    int missionIdx = -1, missionAct = 0;
    bool missionSaved = false;
    auto openMissions = [&] {
        if (missions.empty()) missions = listMissions(ROMFS_DIR, DATA_DIR), progress.load(DATA_DIR "progress.txt");
        screen = Screen::Missions;
    };
    auto startMission = [&](int i) {
        missionIdx = i, missionSaved = false, online = false;
        startMatch(missionConfig(missions[i], (uint32_t)(GetTime() * 1000)));
    };
    // help | helpmenu: the hold - controls overlay over a match / the main menu
    Ui::forceHelp = uiShot && (!strcmp(uiShot, "help") || !strcmp(uiShot, "helpmenu"));
    if (uiShot && (!strcmp(uiShot, "hud") || !strcmp(uiShot, "panel") || !strcmp(uiShot, "pause") || !strcmp(uiShot, "help"))) {
        startMatch({1234, 2, 2, argc > 3 ? argv[3] : "", 0u, {{"Red Rockets"}, {"Blue Bombers"}}});
        hud.open = !strcmp(uiShot, "panel");
        pause.open = !strcmp(uiShot, "pause");
    } else if (uiShot) {
        front.screen = !strcmp(uiShot, "main") || Ui::forceHelp ? Ui::Frontend::Main : !strcmp(uiShot, "setup") ? Ui::Frontend::Setup
                     : !strcmp(uiShot, "options") ? Ui::Frontend::Options : !strcmp(uiShot, "controls") ? Ui::Frontend::Controls : Ui::Frontend::Title;
        if (!strcmp(uiShot, "wormpot")) front.screen = Ui::Frontend::Wormpot, opt.wormpot = WP_DOUBLE_DAMAGE | WP_QUICK_WALK | WP_CRATE_SHOWER;
        if (!strcmp(uiShot, "factory") || !strcmp(uiShot, "weapon")) front.screen = !strcmp(uiShot, "weapon") ? Ui::Frontend::FactoryEdit : Ui::Frontend::Factory;
        if (!strcmp(uiShot, "replays") || !strcmp(uiShot, "playback")) replayFiles = listReplays(DATA_DIR "replays"), screen = Screen::Replays;
        if (!strcmp(uiShot, "playback") && !replayFiles.empty() && play.load(DATA_DIR "replays/" + replayFiles[0])) playing = true, startMatch(play.cfg);
        // missions | briefing | missionhud | missionend [mission id]
        if (!strncmp(uiShot, "mission", 7) || !strcmp(uiShot, "briefing")) openMissions(), missionMenu.brief = !strcmp(uiShot, "briefing");
        for (size_t i = 0; i < missions.size() && (!strcmp(uiShot, "missionhud") || !strcmp(uiShot, "missionend")); i++)
            if (argc > 3 ? missions[i].id == argv[3] : i == 0) {
                startMission((int)i);
                if (!strcmp(uiShot, "missionend")) game.run.result = 1, game.run.ticks = 5000, game.phase = Phase::GameOver;
                break;
            }
    }
    auto cpu = [&](int team) { return team < (int)game.cfg.teamSetup.size() && game.cfg.teamSetup[team].cpu > 0; };
    auto pressed = [](std::initializer_list<int> buttons, std::initializer_list<int> keys) { return pressedAny(-1, buttons, keys); };

    for (int frame = 0; !WindowShouldClose(); frame++) {
        float dt = bench || shot || uiShot ? Game::DT : fminf(GetFrameTime(), 0.25f);  // fixed: reproducible captures
        clock += dt;
        Ui::pollStick();
        Audio::update();
        Controls::update(dt);

        if (online) {
            lanHost.poll();
            net.poll();
            Net::Event e;
            while (net.next(e)) {
                switch (e.type) {
                case Net::Welcome:
                    if (lanHost.running()) net.createRoom((name + "'s game").c_str(), 4);
                    else net.listRooms();
                    status = "Connected to " + connHost;
                    break;
                case Net::RoomList: if (lan && !net.roomId && !net.rooms.empty()) net.joinRoom(net.rooms[0].id); break;
                case Net::RoomState: if (net.roomId && !opt.teamSetup.empty()) net.sendProfile(opt.teamSetup[0], opt.teams - 1); break;
                case Net::Error: status = "Server: " + e.text; break;
                case Net::Start: startMatch(net.cfg); status.clear(); break;
                case Net::Desync: status = TextFormat("DESYNC at tick %u", e.a); desynced = true; break;
                case Net::Chat: status = e.text; break;
                case Net::Disconnected: status = "Disconnected: " + e.text; reconnectAt = clock + 3; break;
                default: break;
                }
                if (!netbot || e.type == Net::RoomList || e.type == Net::RoomState || e.type == Net::Pong) continue;
                printf("[%s] %s\n", name.c_str(), e.type == Net::Welcome ? "welcome" : e.type == Net::Start ? "start" : status.c_str());
                fflush(stdout);
                if (e.type == Net::Welcome)
                    if (FILE *f = fopen("netbot.token", "w")) fprintf(f, "%llx\n", (unsigned long long)net.token), fclose(f);
            }
            // the token kept in `net` resumes the match: server resends Start + the whole input log
            if (!net.online() && screen == Screen::Play && clock > reconnectAt) {
                net.connect(connHost.c_str(), connPort, name.c_str());
                reconnectAt = clock + 3;
            }
        }

        if (screen == Screen::Menu) {
            if (uiShot && frame == 10) front.capture = "ui.png";
            if (uiShot && frame > 10) break;
            Ui::Frontend::Action a = front.frame(opt, maps, host, port, name);
            if (a == Ui::Frontend::Quit) break;
            if (a == Ui::Frontend::Replays) replayFiles = listReplays(DATA_DIR "replays"), replaySel = 0, screen = Screen::Replays;
            if (a == Ui::Frontend::SinglePlayer) openMissions();
            if (a == Ui::Frontend::QuickMatch) {  // you vs one level-2 CPU team on a random map, Standard scheme; opt untouched
                GameConfig q = opt;
                q.teams = 2, q.wormsPerTeam = 4, q.rules = 0, q.wormpot = 0, q.mission = nullptr, q.scheme = SCHEMES[0].s;
                q.custom.clear();
                q.teamSetup.resize(2);
                q.teamSetup[0].cpu = 0;
                q.teamSetup[1] = {"CPU", 2, 1, 0};
                q.map = maps[GetRandomValue(0, (int)maps.size() - 1)];
                q.seed = (uint32_t)(clock * 1000) + frame;
                online = lan = false;
                startMatch(q);
            }
            if (a == Ui::Frontend::StartLocal) {
                online = false;
                opt.seed = (uint32_t)(clock * 1000) + frame;
                startMatch(opt);
            } else if (a == Ui::Frontend::StartOnline) {
                online = true, lan = false, connHost = host, connPort = port;
                status = net.connect(host.c_str(), port, name.c_str()) ? "Connecting to " + host + "..." : "Cannot reach " + host;
                screen = Screen::Lobby;
            } else if (a == Ui::Frontend::StartLan) {
                online = lan = true;
                net.close();
                status = lanScan.open() ? "" : "Network unavailable";
                screen = Screen::Lobby;
            }
            continue;
        }

        if (screen == Screen::Missions) {
            BeginDrawing();
            Ui::background();
            int pick = Ui::missionMenu(missionMenu, missions, progress);
            if (Ui::helpHeld()) Ui::controls(false);
            if (uiShot && frame == 10) {
                rlDrawRenderBatchActive();
                Image img = LoadImageFromScreen();
                ExportImage(img, "ui.png");
                UnloadImage(img);
            }
            EndDrawing();
            if (uiShot && frame >= 10) break;
            if (pick == -2) screen = Screen::Menu;
            else if (pick >= 0) missionMenu.brief = false, startMission(pick);
            continue;
        }

        if (screen == Screen::Replays) {
            bool was = instant;
            BeginDrawing();
            Ui::background();
            int pick = Ui::replayList(replayFiles, replaySel, instant);
            if (Ui::helpHeld()) Ui::controls(false);
            if (uiShot && frame == 10) {
                rlDrawRenderBatchActive();
                Image img = LoadImageFromScreen();
                ExportImage(img, "ui.png");
                UnloadImage(img);
            }
            EndDrawing();
            if (uiShot && frame >= 10) break;
            if (instant != was) SaveFileText(DATA_DIR "replay.txt", (char *)(instant ? "1\n" : "0\n"));
            if (pick == -2) screen = Screen::Menu;
            else if (pick >= 0 && play.load(DATA_DIR "replays/" + replayFiles[pick])) {
                online = false, playing = true, paused = freeCam = false, speed = 1;
                startMatch(play.cfg);
            } else if (pick >= 0) TraceLog(LOG_WARNING, "unreadable replay %s", replayFiles[pick].c_str());
            continue;
        }

        if (screen == Screen::Lobby) {
            bool inRoom = net.roomId != 0, isHost = inRoom && net.hostId == net.id;
            if (netbot && botStarted && clock > 15) {
                printf("[%s] no match to resume\n", name.c_str());
                desynced = true;
                break;
            }
            if (netbot && !botStarted && net.id && clock > botAt) {  // botStarted: resuming, the server replays the match
                botAt = clock + 0.5f;
                if (!inRoom && botCreate) net.createRoom("netbot", 2);
                else if (!inRoom) net.rooms.empty() ? net.listRooms() : net.joinRoom(net.rooms[0].id);
            }
            if (lan && !net.online()) {  // LAN list: games heard by their beacon, or host one here
                if (!lanHost.running()) lanScan.open();
                lanScan.poll(clock);
                const std::vector<LanGame> &games = lanScan.games;
                int n = (int)games.size();
                lanSel = n ? (lanSel + pressed({GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {KEY_DOWN}) - pressed({GAMEPAD_BUTTON_LEFT_FACE_UP}, {KEY_UP}) + n) % n : 0;
                bool join = n && (netbot ? !botCreate : pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}));
                if (!netbot && pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) {
                    if (lanHost.open(port, (name + "'s game").c_str())) connHost = "127.0.0.1", connPort = port;
                    else status = TextFormat("Cannot host: port %d busy", port);
                }
                if (join) connHost = games[lanSel].ip, connPort = games[lanSel].port;
                if (join && netbot) printf("[%s] lan game at %s:%d\n", name.c_str(), connHost.c_str(), connPort);
                if (join || lanHost.running()) {
                    lanScan.close();
                    status = net.connect(connHost.c_str(), connPort, name.c_str()) ? "Connecting to " + connHost + "..." : "Cannot reach " + connHost;
                }
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) { lanScan.close(); online = lan = false; screen = Screen::Menu; }
                BeginDrawing();
                Ui::background();
                Ui::lanGames(games, lanSel, status);
                if (Ui::helpHeld()) Ui::controls(false);
                EndDrawing();
                continue;
            }
            if (!inRoom) {
                int n = (int)net.rooms.size();
                if (n) roomSel = (roomSel + pressed({GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {KEY_DOWN}) - pressed({GAMEPAD_BUTTON_LEFT_FACE_UP}, {KEY_UP}) + n) % n;
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}) && n) net.joinRoom(net.rooms[roomSel].id);
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) net.createRoom((name + "'s room").c_str(), 4);
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_R})) net.listRooms();
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) {
                    if (lan) leaveLan();
                    else net.close(), online = false, screen = Screen::Menu;
                }
            } else {
                if (isHost && net.players.size() >= 2 && (netbot ? !botStarted : pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}))) {
                    botStarted = true;
                    GameConfig c = opt;
                    c.seed = (uint32_t)(clock * 1000) + frame;
                    net.startMatch(c);  // one team per console + the host's CPU teams
                }
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) {
                    if (lan) leaveLan();
                    else net.leave(), net.listRooms();
                }
            }
            BeginDrawing();
            Ui::background();
            if (inRoom) Ui::room(net, opt, lan, status);
            else {
                drawTextCentered(lan ? "JOINING..." : "ONLINE LOBBY", 640, 60, 60, {255, 220, 120, 255});
                for (size_t i = 0; i < net.rooms.size(); i++) {
                    const NetRoom &r = net.rooms[i];
                    drawTextCentered(TextFormat("%s %s  (%d/%d)%s", (int)i == roomSel ? ">" : " ", r.name.c_str(), r.players, r.maxPlayers, r.started ? " playing" : ""),
                                     640, 160 + (int)i * 40, 30, (int)i == roomSel ? YELLOW : WHITE);
                }
                if (net.rooms.empty()) drawTextCentered("No rooms yet", 640, 200, 30, LIGHTGRAY);
                Ui::hints({{"A", "Enter", "Join"}, {"X", "C", "Create room"}, {"Y", "R", "Refresh"}, {"B", "Esc", "Back"}});
                drawTextCentered(status.c_str(), 640, 660, 22, ORANGE);
            }
            if (Ui::helpHeld()) Ui::controls(false);
            EndDrawing();
            continue;
        }

        mark = GetTime();
        const Worm &cur = game.worms[game.current];
        int pad = !online && IsGamepadAvailable(cur.team) ? cur.team : 0;
        bool quit;
        if (playing) {  // match playback controls
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) paused = !paused;
            if (pressed({GAMEPAD_BUTTON_RIGHT_TRIGGER_1}, {KEY_TAB})) speed = speed == 4 ? 1 : speed * 2;
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) {
                Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                freeCam = !freeCam, fcPos = cam.position, fcYaw = atan2f(f.x, f.z), fcPitch = asinf(f.y);
            }
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_N})) {  // fast-forward to the next turn, unrendered
                for (bool next = false; !next && tick < play.inputs.size();) {
                    game.step(play.inputs[tick++]);
                    for (const GameEvent &e : game.events) next |= e.kind == GameEvent::TurnStart || e.kind == GameEvent::GameOver;
                }
                Fx::clear();
                game.terrain.remesh();
            }
            quit = pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_MIDDLE_RIGHT}, {KEY_ESCAPE, KEY_BACKSPACE});
        } else quit = !shot && pause.update() == Ui::Pause::Quit;
        if (quit) {
            irFinish();
            saveRec();
            playing = false;
            if (lan) leaveLan();  // a host leaving ends the LAN game: it is the server
            else if (online) net.leave(), net.listRooms();
            screen = online ? Screen::Lobby : game.cfg.mission ? Screen::Missions : Screen::Menu;
            Audio::music(true, "theme");
            continue;
        }
        for (uint32_t o : net.owners) {  // an owner who left the room counts as offline too
            auto p = std::find_if(net.players.begin(), net.players.end(), [&](const NetPlayer &pl) { return pl.id == o; });
            if (p != net.players.end() && p->online) offlineSince.erase(o);
            else offlineSince.emplace(o, clock);
        }
        // the host plays idle turns for owners gone > 30 s so a dropout can't stall the match
        // ponytail: if the owner reconnects mid-turn both may send the same tick; server keeps the first
        auto proxied = [&](int team) {
            auto it = team < (int)net.owners.size() ? offlineSince.find(net.owners[team]) : offlineSince.end();
            return net.hostId == net.id && it != offlineSince.end() && clock - it->second > 30;
        };
        // the host also plays the CPU teams
        auto owns = [&](int team) { return (cpu(team) && net.hostId == net.id) || (team < (int)net.owners.size() && (net.owners[team] == net.id || proxied(team))); };
        bool remoteTurn = online && game.phase != Phase::GameOver && !owns(cur.team);
        bool padTurn = !shot && !remoteTurn && !pause.open && !playing && irEnd < 0;
        Input pin = Controls::read(game, pad, padTurn && !cpu(cur.team), dt);
        Input in = shot ? scriptInput(frame, shotWeapon) : pause.open || playing || irEnd >= 0 ? Input{} : pin;
        hud.input(game, in, padTurn, pad, tick);
        bool feedPad = padTurn && !hud.open;
        auto local = [&] { return feedPad ? Controls::tick(in) : in; };  // stick rates spread over ticks
        livePad = padTurn && !cpu(cur.team) ? pad : -1;
        if (playing) {
            for (acc += paused ? 0 : dt * speed; acc >= Game::DT; acc -= Game::DT) {
                if (tick >= play.inputs.size()) { acc = 0; break; }
                stepOnce(play.inputs[tick]);
            }
        } else if (irEnd >= 0) {  // instant replay: slow motion, A skips
            if (!pause.open && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) irFinish();
            for (irAcc += pause.open ? 0 : dt * 0.5f; irAcc >= Game::DT && irEnd >= 0; irAcc -= Game::DT) {
                game.step(rec.inputs[irTick++]);
                for (const GameEvent &e : game.events) onEvent(game, e);
                if (irTick == (uint32_t)irEnd) irFinish();
            }
        } else if (!online) {
            for (acc += pause.open ? 0 : dt; acc >= Game::DT; acc -= Game::DT) stepOnce(!shot && cpu(game.worms[game.current].team) ? ai.think(game) : local());
        } else {
            // remote/replayed inputs first, then ours when we own the active team; otherwise wait
            acc = netbot ? Game::DT * botSpeed : fminf(acc + dt, Game::DT * 4);
            for (int budget = 240; budget > 0 && !(netbot && turns >= botTurns); budget--) {
                Input r;
                if (net.remoteInput(tick, r)) { stepOnce(r); continue; }
                bool mine = game.phase != Phase::GameOver && tick >= net.replay && owns(game.worms[game.current].team);
                if (!mine || acc < Game::DT) break;
                int team = game.worms[game.current].team;
                bool bot = netbot && team < (int)net.owners.size() && net.owners[team] == net.id;
                Input mineIn = cpu(team) || bot ? ai.think(game) : proxied(team) ? Input{} : local();
                if (netbot && proxied(team) && proxyTurn != turns) printf("[%s] playing turn %d for offline team %d\n", name.c_str(), turns + 1, team), proxyTurn = turns;
                net.sendInput(tick, mineIn);
                stepOnce(mineIn);
                acc -= Game::DT;
            }
        }
        if (shotDone && instant && snap.valid && irEnd < 0 && !online && !playing && !shot && !bench) {
            int dmg = 0;
            bool kill = false;
            for (size_t i = 0; i < game.worms.size() && i < snap.g.worms.size(); i++) {
                const Worm &a = snap.g.worms[i], &b = game.worms[i];
                if (a.alive) dmg += std::max(0, a.hp - std::max(0, b.hp)), kill |= !b.alive || b.hp <= 0;
            }
            if (kill || dmg >= 30) {  // replay from just before the shot, at most the last 8 s
                irLive = game.checksum(), irEnd = (int)tick;
                snap.restore(game);
                snap.valid = false;
                irTick = std::max({snap.tick, fireTick > 45 ? fireTick - 45 : 0, tick > 480 ? tick - 480 : 0});
                for (uint32_t t = snap.tick; t < irTick; t++) game.step(rec.inputs[t]);
                game.terrain.remesh();
                Fx::clear();
                irAcc = 0;
            }
        }
        shotDone = false;
        lap(T_SIM);
        game.terrain.remesh(0.003);  // a big blast's rebuild spreads over a few frames, hidden by the fireball
        lap(T_REMESH);
        int sec = game.phase == Phase::Aim && game.timer <= 300 ? game.timer / 60 : -1;
        if (sec >= 0 && sec != lastSec) Audio::play(Audio::Sfx::Tick), Controls::rumble(livePad, 0.12f, 0.05f);
        lastSec = sec;
        if (netbot && botDone > 1e8f && (game.phase == Phase::GameOver || turns >= botTurns)) botDone = clock + 2;  // let the last inputs and sums out
        if (netbot && clock > botDone) break;
        if (game.phase == Phase::GameOver && !pause.open && !playing && irEnd < 0 && !game.cfg.mission && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) {
            screen = online ? Screen::Lobby : Screen::Menu;
            Audio::music(true, "theme");
        }

        // camera (controls.cpp): free orbit, over the shoulder in aim mode, chasing the projectile, sniper scope
        const WeaponDef &wd = WEAPONS[game.weapon];
        bool chase = game.phase == Phase::Flying && !game.shots.empty();
        bool scope = !chase && game.phase == Phase::Aim && cur.alive && !game.roped && !game.jetting && wd.name == "Sniper Rifle";
        if ((IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) &&
             (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) || IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))) || IsKeyPressed(KEY_F3))
            perfOn = (perfOn + 1) % 3;
        Controls::camera(cam, game, chase, scope, !pause.open && !hud.open && !(playing && freeCam), dt);
        Camera3D view = cam;  // shaken copy: the smoothed camera itself never drifts
        if (fixedView) view = viewCam;
        if (playing && freeCam) {  // LS / arrows move, RS / A D W S look, ZL ZR / Z X down up
            auto ax = [&](int a) { float v = GetGamepadAxisMovement(pad, a); return fabsf(v) < 0.2f ? 0.0f : v; };
#ifdef __SWITCH__
            const float up = 1;
#else
            const float up = -1;
#endif
            fcYaw -= (ax(GAMEPAD_AXIS_RIGHT_X) + IsKeyDown(KEY_D) - IsKeyDown(KEY_A)) * dt * 2;
            fcPitch = Clamp(fcPitch + (up * ax(GAMEPAD_AXIS_RIGHT_Y) + IsKeyDown(KEY_W) - IsKeyDown(KEY_S)) * dt * 1.5f, -1.4f, 1.4f);
            Vector3 f = {sinf(fcYaw) * cosf(fcPitch), sinf(fcPitch), cosf(fcYaw) * cosf(fcPitch)}, right = {-cosf(fcYaw), 0, sinf(fcYaw)};
            float mv = up * ax(GAMEPAD_AXIS_LEFT_Y) + IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN), st = ax(GAMEPAD_AXIS_LEFT_X) + IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT);
            float lift = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) + IsKeyDown(KEY_X) - IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_2) - IsKeyDown(KEY_Z);
            fcPos = Vector3Add(fcPos, Vector3Scale(Vector3Add(Vector3Add(Vector3Scale(f, mv), Vector3Scale(right, st)), {0, lift, 0}), dt * 15));
            view.position = fcPos, view.target = Vector3Add(fcPos, f), view.fovy = 50;
        }
        Vector3 jolt = Vector3Scale({sinf(clock * 53), sinf(clock * 61 + 1) * 0.7f, sinf(clock * 47 + 2)}, Fx::shake);
        view.position = Vector3Add(view.position, jolt), view.target = Vector3Add(view.target, jolt);
        mark = GetTime();

        BeginDrawing();
        ClearBackground(Fx::fog());
        BeginMode3D(view);
        Fx::drawSky(view);
        lap(T_SKY);
        game.terrain.setFog(view.position, Fx::fog(), Fx::FOG_NEAR, Fx::FOG_FAR);
        game.terrain.draw();
        lap(T_TERRAIN);
        game.terrain.drawObjects(view.position);
        lap(T_DECOR);
        animateWorms(game, dt);
        for (const Worm &w : game.worms) {
            if (!w.alive) { drawGrave(game, w); continue; }
            if ((scope && &w == &cur) || !Models::visible(w.pos, 2)) continue;  // scope: the camera is inside it
            if (!drawWorm(game, w, clock)) {
                Vector3 f = {sinf(w.yaw), 0, cosf(w.yaw)}, side = {f.z, 0, -f.x};
                DrawCapsule({w.pos.x, w.pos.y - 0.2f, w.pos.z}, {w.pos.x, w.pos.y + 0.3f, w.pos.z}, 0.35f, 8, 6, TEAM_COLORS[w.team]);
                for (float s : {-0.13f, 0.13f})
                    DrawSphere(Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 0.3f), Vector3Add(Vector3Scale(side, s), {0, 0.4f, 0}))), 0.1f, WHITE);
            }
            if ((game.cfg.rules & RULE_KING) && int(&w - game.worms.data()) % game.perTeam == 0)
                DrawCylinderEx({w.pos.x, w.pos.y + 0.55f, w.pos.z}, {w.pos.x, w.pos.y + 0.8f, w.pos.z}, 0.28f, 0.08f, 6, GOLD);
        }
        if (game.roped) DrawLine3D(game.anchor, cur.pos, BROWN);
        if (game.jetting) DrawCube(Vector3Add(cur.pos, {-sinf(cur.yaw) * 0.4f, 0.1f, -cosf(cur.yaw) * 0.4f}), 0.35f, 0.5f, 0.35f, GRAY);
        if (game.phase == Phase::Aim && cur.alive && !game.roped && !game.jetting && !scope) {
            if (wd.kind == Kind::Airstrike || wd.kind == Kind::Donkey || wd.kind == Kind::Teleport || wd.kind == Kind::Homing || wd.kind == Kind::Abduction) {
                Vector3 t = game.target();
                DrawCircle3D(Vector3Add(t, {0, 0.1f, 0}), 1.2f, {1, 0, 0}, 90, RED);
                DrawCircle3D(Vector3Add(t, {0, 0.1f, 0}), 0.4f, {1, 0, 0}, 90, RED);
                DrawLine3D(t, Vector3Add(t, {0, 6, 0}), RED);
            }
            Vector3 tip = Vector3Add(cur.pos, Vector3Scale(game.aimDir(cur), 3));
            DrawLine3D(cur.pos, tip, RED);
            DrawSphere(tip, 0.12f, RED);
            DrawCube(Vector3Add(cur.pos, {0, 1.5f + sinf(clock * 5) * 0.15f, 0}), 0.25f, 0.25f, 0.25f, YELLOW);
        }
        for (const Projectile &s : game.shots) {
            const WeaponDef &d = WEAPONS[s.weapon];
            if (drawShot(s, clock)) continue;
            Vector3 f = Vector3Normalize({s.vel.x, 0, s.vel.z});
            switch (d.kind) {
            case Kind::Sheep:
                DrawCube(s.pos, 0.5f, 0.4f, 0.5f, WHITE);
                DrawCube(Vector3Add(s.pos, {f.x * 0.3f, 0.15f, f.z * 0.3f}), 0.22f, 0.22f, 0.22f, BLACK);
                break;
            case Kind::Donkey: DrawCube(s.pos, 1.0f, 1.4f, 0.7f, GRAY); break;
            case Kind::Airstrike: DrawCylinderEx(s.pos, Vector3Add(s.pos, Vector3Scale(Vector3Normalize(s.vel), -1)), 0.15f, 0.15f, 6, MAROON); break;
            default:
                DrawSphere(s.pos, s.child ? 0.15f : 0.2f, s.child ? ORANGE : d.radius >= 5 ? GOLD : d.clusters ? YELLOW : d.fuse > 0 ? DARKGREEN : DARKGRAY);
            }
        }
        for (const Object &o : game.objects) {
            const char *m = o.type == Object::Mine ? "mine" : o.type == Object::Barrel ? "barrel" : o.type == Object::Sentry ? "sentry"
                          : o.type == Object::Target ? "target" : o.weapon < 0 ? "crate_health" : "crate_weapon";
            if (!Models::visible(Vector3Add(o.pos, {0, 1, 0}), 2.5f)) continue;  // incl. the parachute
            if (!Models::draw(m, o.pos, (&o - game.objects.data()) * 1.3f)) {
                if (o.type == Object::Mine) DrawCylinder({o.pos.x, o.pos.y - 0.1f, o.pos.z}, 0.15f, 0.2f, 0.2f, 8, DARKGRAY);
                else if (o.type == Object::Barrel) DrawCylinder({o.pos.x, o.pos.y - 0.5f, o.pos.z}, 0.35f, 0.35f, 1, 10, MAROON);
                else if (o.type == Object::Target) {  // bullseye facing the active worm
                    Vector3 f = Vector3Normalize({cur.pos.x - o.pos.x, 0, cur.pos.z - o.pos.z});
                    for (int k = 0; k < 3; k++)
                        DrawCylinderEx(Vector3Add(o.pos, Vector3Scale(f, 0.02f * k)), Vector3Add(o.pos, Vector3Scale(f, 0.02f * k + 0.06f)), 0.5f - 0.15f * k, 0.5f - 0.15f * k, 16, k % 2 ? WHITE : RED);
                    DrawCylinderEx(Vector3Add(o.pos, {0, -0.5f, 0}), Vector3Add(o.pos, {0, -1.2f, 0}), 0.05f, 0.05f, 6, BROWN);
                }
                else DrawCube(o.pos, 0.8f, 0.8f, 0.8f, o.weapon < 0 ? RAYWHITE : BROWN);
            }
            if (o.type == Object::Mine && o.fuse >= 0 && fmodf(clock, 0.3f) < 0.15f) DrawSphere(Vector3Add(o.pos, {0, 0.15f, 0}), 0.08f, RED);
            if (o.falling) {
                Vector3 top = Vector3Add(o.pos, {0, 2.2f, 0});
                DrawCylinderEx(top, Vector3Add(top, {0, 0.5f, 0}), 1.1f, 0.3f, 10, o.weapon < 0 ? RED : ORANGE);
                for (float a : {0.8f, 2.4f, 3.9f, 5.5f}) DrawLine3D(Vector3Add(o.pos, {0, 0.4f, 0}), Vector3Add(top, {cosf(a) * 1.1f, 0, sinf(a) * 1.1f}), LIGHTGRAY);
            }
        }
        if (game.cfg.mission)  // reach objectives: a gold beacon
            for (const MissionSpec::Goal &g : game.cfg.mission->objectives) {
                if (g.type != MissionSpec::Goal::Reach) continue;
                Vector3 p = placeOf(game, g.at);
                DrawCylinderEx(p, Vector3Add(p, {0, 10, 0}), 0.15f, 0.15f, 8, Fade(GOLD, 0.5f));
                DrawCircle3D(Vector3Add(p, {0, 0.05f, 0}), g.radius, {1, 0, 0}, 90, GOLD);
            }
        if (game.cfg.rules & RULE_ROPE_RACE) {
            DrawCylinderEx(game.raceFinish, Vector3Add(game.raceFinish, {0, 10, 0}), 0.15f, 0.15f, 8, Fade(GOLD, 0.5f));
            DrawCube(Vector3Add(game.raceFinish, {0, 10.3f, 0}), 1.2f, 0.6f, 0.08f, RED);
        }
        lap(T_MODELS);
        Fx::drawWater(view, game.water, clock);
        lap(T_SKY);
        float fxDt = pause.open ? 0 : dt;
        for (const Projectile &s : game.shots) Fx::trail(s, fxDt);
        Fx::update(fxDt);
        Fx::draw(view);
        lap(T_FX);
        EndMode3D();

        if (scope) {
            Vector2 c = GetWorldToScreen(Vector3Add(cur.pos, Vector3Scale(game.aimDir(cur), 30)), view);
            DrawRing(c, 26, 29, 0, 360, 32, Fade(BLACK, 0.7f));
            DrawRectangle(c.x - 40, c.y - 1, 80, 2, Fade(BLACK, 0.7f)), DrawRectangle(c.x - 1, c.y - 40, 2, 80, Fade(BLACK, 0.7f));
        }
        hud.quiet = pause.open || playing || irEnd >= 0;  // those draw their own hints
        hud.draw(game, view, tick);
        if (const MissionSpec *ms = game.cfg.mission; ms && game.phase != Phase::GameOver) Ui::missionHud(game, *ms);
        else if (ms && !pause.open && missionIdx >= 0) {
            if (!missionSaved && !uiShot) progress.record(ms->id, game.run.result > 0, game.run.ticks), progress.save(DATA_DIR "progress.txt");
            missionSaved = true;
            int next = missionIdx + 1;
            bool more = next < (int)missions.size() && missions[next].kind == ms->kind && missions[next].campaign == ms->campaign && progress.unlocked(missions, next);
            missionAct = Ui::missionEnd(game, *ms, progress.get(ms->id), more);
        }
        pause.draw(online);
        if (playing) {
            bool end = tick >= play.inputs.size();
            Ui::playbackBar(paused, speed, freeCam, tick * Game::DT, play.inputs.size() * Game::DT,
                            !end ? "" : play.checksum && game.checksum() != play.checksum ? "Replay out of sync (other game version?)" : "End of replay");
        } else if (irEnd >= 0) Ui::replayBadge();
        if (remoteTurn) drawTextCentered("Remote player's turn", 640, 90, 24, WHITE);
        if (online && !status.empty()) drawTextCentered(status.c_str(), 640, 120, 24, ORANGE);
        if (Ui::helpHeld()) Ui::controls(true);
        if (perfOn && !bench) {  // bench: keep the overlay out of the measured ui cost
            int tris = 0;
            for (const auto &ps : game.terrain.parts)
                for (const Terrain::Part &p : ps) tris += p.mesh.triangleCount;
            DrawRectangle(6, 34, 270, 40 + T_COUNT * 20, Fade(BLACK, 0.6f));
            double total = 0;
            for (double c : perf) total += c;
            Ui::text(TextFormat("%d fps  %.2f ms %s", GetFPS(), total * 1000, perfOn == 2 ? "gpu sync" : "cpu"), 14, 38, 20, YELLOW);
            for (int k = 0; k < T_COUNT; k++) {
                Ui::text(NAMES[k], 14, 60 + k * 20, 18, WHITE);
                Ui::text(TextFormat("%.2f", perf[k] * 1000), 200, 60 + k * 20, 18, WHITE, 2);
            }
            Ui::text(TextFormat("terrain %dk tris  fx %d", tris / 1000, Fx::count()), 14, 62 + T_COUNT * 20, 18, LIGHTGRAY);
        } else DrawFPS(10, 10);
        lap(T_UI);
        if (shot && (frame == 35 || frame == 150)) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, frame == 35 ? DATA_DIR "shot_aim.png" : DATA_DIR "shot.png");
            UnloadImage(img);
        }
        if (uiShot && frame == 40) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, "ui.png");
            UnloadImage(img);
        }
        EndDrawing();
        lap(T_PRESENT);
        if (missionAct) {  // end-of-mission choice, outside the frame: next, retry, back to the list
            int act = missionAct;
            missionAct = 0;
            if (act == 3) screen = Screen::Missions, Audio::music(true, "theme");
            else startMission(act == 1 ? missionIdx + 1 : missionIdx);
            continue;
        }
        const int warm = 10;  // skip load / first-use frames
        if (bench && frame == warm) benchStart = GetTime();
        if (bench && frame > warm) {
            frameMax = fmax(frameMax, GetTime() - frameStart);
            for (int k = 0; k < T_COUNT; k++) benchSum[k] += cost[k], benchCpu[k] += cpuCost[k], benchMax[k] = fmax(benchMax[k], cost[k]);
        }
        frameStart = GetTime();
        for (int k = 0; k < T_COUNT; k++) perf[k] = perf[k] * 0.95 + cost[k] * 0.05, cost[k] = 0, cpuCost[k] = 0;
        if (bench && frame == warm + benchFrames) {
            double n = benchFrames, wall = GetTime() - benchStart, cpu = 0, gpu = 0;
            printf("BENCH map=%s frames=%d  %.1f fps (%s)  frame avg %.2f ms max %.2f ms\n", opt.map.empty() ? "(procedural)" : opt.map.c_str(), benchFrames, n / wall, perfOn == 2 ? "gpu-synced" : "no sync",
                   wall / n * 1000, frameMax * 1000);
            printf("%-13s %8s %8s %8s %8s\n", "section", "cpu", "sync", "gpu", "syncmax");
            for (int k = 0; k < T_COUNT; k++) {
                double c = benchCpu[k] / n * 1000, s = benchSum[k] / n * 1000;
                cpu += c, gpu += s - c;
                printf("%-13s %8.3f %8.3f %8.3f %8.2f\n", NAMES[k], c, s, s - c, benchMax[k] * 1000);
            }
            gpu -= (benchSum[T_PRESENT] - benchCpu[T_PRESENT]) / n * 1000;  // vsync wait, not GPU work
            // ponytail: Switch ~5x slower CPU, Tegra X1 docked ~4x below a desktop iGPU; glFinish waits overstate GPU time
            double sw = fmax(5 * cpu, 4 * gpu);
            printf("total cpu %.2f ms gpu %.2f ms -> Switch ~%.1f ms = max(5x cpu, 4x gpu docked) = %.0f fps\n", cpu, gpu, sw, 1000 / sw);
            int tris = 0, parts = 0;
            for (const auto &ps : game.terrain.parts)
                for (const Terrain::Part &p : ps) tris += p.mesh.triangleCount, parts++;
            printf("terrain %d tris %d meshes, %d decor, phase %d turn tick %u\n", tris, parts, (int)game.terrain.objects.size(), (int)game.phase, tick);
            fflush(stdout);
            break;
        }
        if ((shot && frame == 150) || (uiShot && frame == 40)) break;
    }
    if (screen == Screen::Play) irFinish(), saveRec();
    net.close();
    game.terrain.unload();
    Models::unload();
    Ui::unload();
    Fx::unload();
    Audio::shutdown();
    CloseWindow();
    if (netbot) printf("[%s] done: %d turns, %s\n", name.c_str(), turns, desynced ? "FAILED" : "in sync");
    return desynced;
}

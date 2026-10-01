#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "ai.h"
#include "audio.h"
#include "fx.h"
#include "models.h"
#include "net.h"
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

static Input readInput(int pad) {
    auto ax = [&](int a) {
        float v = GetGamepadAxisMovement(pad, a);
        return fabsf(v) < 0.2f ? 0.0f : v;
    };
    auto q = [](float v) { return (int8_t)(Clamp(v, -1, 1) * 127); };
    // yaw grows toward +x, which is the camera's left
    Input in;
    in.turn = q(-ax(GAMEPAD_AXIS_LEFT_X) + IsKeyDown(KEY_LEFT) - IsKeyDown(KEY_RIGHT));
#ifdef __SWITCH__
    const float up = 1;  // libnx HID sticks report +y for up, GLFW reports -y
#else
    const float up = -1;
#endif
    in.walk = q(up * ax(GAMEPAD_AXIS_LEFT_Y) + IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN));
    in.aim = q(up * ax(GAMEPAD_AXIS_RIGHT_Y) + IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) && !in.aim) in.aim = in.walk, in.walk = 0;  // L + stick aims (single Joy-Con)
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsKeyDown(KEY_SPACE)) in.buttons |= Input::FIRE;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsKeyDown(KEY_ENTER)) in.buttons |= Input::JUMP;
    bool r = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) && !IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1);  // L+R: perf overlay
    if (r || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || IsKeyDown(KEY_TAB))
        in.buttons |= Input::NEXT_WEAPON;
    return in;
}

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

// W4M worm: team-tinted, animation picked from the sim state (aim clips map pitch to their timeline).
static bool drawWorm(const Game &g, const Worm &w, float clock) {
    int i = int(&w - g.worms.data());
    float speed = sqrtf(w.vel.x * w.vel.x + w.vel.z * w.vel.z), t = clock;  // shared timeline: idle worms reuse one skinned pose
    float fidget = fmodf(clock + i * 7.3f, 25);  // desynchronised per worm
    const char *clip = "Base", *held = nullptr;
    bool loop = true;
    if (!w.grounded && fabsf(w.vel.y) > 1) clip = w.vel.y > 0 ? "Jump" : "Fall";  // ignore slope-contact flicker
    else if (speed > 0.3f) clip = "Walk";
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
    float h = sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z), yaw = atan2f(s.vel.x, s.vel.z);
    if (d.kind == Kind::Sheep || d.kind == Kind::Donkey) return Models::draw(m, s.pos, yaw, 0, WHITE, "Run", clock);
    if (d.kind == Kind::OldWoman) return Models::draw(m, {s.pos.x, s.pos.y - 0.3f, s.pos.z}, yaw, 0, WHITE, "Walk", clock);
    if (d.kind == Kind::SuperSheep && n != "Starburst") return Models::draw(m, s.pos, yaw, atan2f(s.vel.y, h), WHITE, "Fly", clock);
    if (d.kind == Kind::Scouser) return Models::draw(m, s.pos, clock * 0.7f);
    if (d.fuse > 0 && d.kind == Kind::Shell) return Models::draw(m, s.pos, clock * 6, clock * 4);
    return Models::draw(m, s.pos, yaw, atan2f(s.vel.y, h));
}

enum class Screen { Menu, Lobby, Play };

int main(int argc, char **argv) {
    InitWindow(1280, 720, "Worms4NX");
    SetExitKey(KEY_NULL);  // Esc is back / pause; quit from the title screen
    SetTargetFPS(60);
    rlSetClipPlanes(0.5, 500);  // default 0.01 near plane z-fights the water on GLES depth buffers
    Audio::init();
    Audio::music(true);
    Models::load();
    Ui::load();
    Fx::load();

    // Shot mode (flag file or --shot): scripted turn, screenshot, quit. Lets us check rendering in the emulator.
    // --cpu [map] [level]: every team is played by the AI (until the team setup menu lands)
    // --ui title|main|setup|options|hud|panel [map]: capture that screen to ui.png and quit
    const char *uiShot = argc > 2 && !strcmp(argv[1], "--ui") ? argv[2] : nullptr;
    bool cpuAll = argc > 1 && !strcmp(argv[1], "--cpu"), shot = !cpuAll && !uiShot && (argc > 1 || FileExists(DATA_DIR "shot"));
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
    bool online = false;
    Screen screen = shot ? Screen::Play : Screen::Menu;
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
    if (char *t = argc <= 3 && FileExists(DATA_DIR "shot") ? LoadFileText(DATA_DIR "shot") : nullptr) {
        char m[64] = "";
        if (sscanf(t, "%63s", m) == 1) shotMap = m;
        UnloadFileText(t);
    }
    if (shot) { game.start({1234, 2, 2, shotMap, argc > 4 ? (uint32_t)atoi(argv[4]) : 0u}); game.terrain.remesh(); Fx::theme(game.terrain.theme, game.terrain.sky); }

    Camera3D cam = {{40, 30, 0}, {40, 8, 40}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    float camYaw = 0, orbit = 0, zoom = 1, acc = 0, clock = 0, reconnectAt = 0;
    // perf overlay (L+R / F3 cycles off, CPU, GPU-synced): ms per section, smoothed. CPU mode only times command
    // submission (GPU work lands in "present"); synced mode glFinish()es after each section to charge the GPU cost to it.
    enum { T_SIM, T_REMESH, T_SKY, T_TERRAIN, T_MODELS, T_FX, T_UI, T_PRESENT, T_COUNT };
    double perf[T_COUNT] = {}, cost[T_COUNT] = {}, mark = 0;
    int perfOn = 0;
    auto lap = [&](int k) {
        if (perfOn == 2) rlDrawRenderBatchActive(), glFinish();
        double t = GetTime();
        cost[k] += t - mark, mark = t;
    };

    auto stepOnce = [&](const Input &in) {
        game.step(in);
        tick++;
        for (const GameEvent &e : game.events) {
            onEvent(game, e);
            if (online && e.kind == GameEvent::TurnStart) net.turnEnd(tick, game.checksum());
        }
    };
    auto startMatch = [&](const GameConfig &c) {
        game.start(c);
        Audio::music(true, game.terrain.theme.empty() ? "theme" : game.terrain.theme.c_str());
        game.terrain.remesh();
        Fx::theme(game.terrain.theme, game.terrain.sky);
        Fx::clear();
        tick = 0;
        acc = 0;
        screen = Screen::Play;
    };
    if (cpuAll) {
        opt.map = argc > 2 ? argv[2] : "";
        opt.teamSetup.assign(4, {"CPU", (uint8_t)(argc > 3 ? atoi(argv[3]) : 2)});
        startMatch(opt);
    }
    Ai ai;
    Ui::Frontend front;
    Ui::Hud hud;
    Ui::Pause pause;
    if (uiShot && (!strcmp(uiShot, "hud") || !strcmp(uiShot, "panel") || !strcmp(uiShot, "pause"))) {
        startMatch({1234, 2, 2, argc > 3 ? argv[3] : "", 0u, {{"Red Rockets"}, {"Blue Bombers"}}});
        hud.open = !strcmp(uiShot, "panel");
        pause.open = !strcmp(uiShot, "pause");
    } else if (uiShot) {
        front.screen = !strcmp(uiShot, "main") ? Ui::Frontend::Main : !strcmp(uiShot, "setup") ? Ui::Frontend::Setup
                     : !strcmp(uiShot, "options") ? Ui::Frontend::Options : !strcmp(uiShot, "controls") ? Ui::Frontend::Controls : Ui::Frontend::Title;
    }
    auto cpu = [&](int team) { return team < (int)game.cfg.teamSetup.size() && game.cfg.teamSetup[team].cpu > 0; };
    auto pressed = [](std::initializer_list<int> buttons, std::initializer_list<int> keys) { return pressedAny(-1, buttons, keys); };

    for (int frame = 0; !WindowShouldClose(); frame++) {
        float dt = fminf(GetFrameTime(), 0.25f);
        clock += dt;
        Ui::pollStick();
        Audio::update();

        if (online) {
            net.poll();
            Net::Event e;
            while (net.next(e)) switch (e.type) {
                case Net::Welcome: net.listRooms(); status = "Connected to " + host; break;
                case Net::Error: status = "Server: " + e.text; break;
                case Net::Start: startMatch(net.cfg); status.clear(); break;
                case Net::Desync: status = TextFormat("DESYNC at tick %u", e.a); break;
                case Net::Chat: status = e.text; break;
                case Net::Disconnected: status = "Disconnected: " + e.text; reconnectAt = clock + 3; break;
                default: break;
            }
            // the token kept in `net` resumes the match: server resends Start + the whole input log
            if (!net.online() && screen == Screen::Play && clock > reconnectAt) {
                net.connect(host.c_str(), port, name.c_str());
                reconnectAt = clock + 3;
            }
        }

        if (screen == Screen::Menu) {
            if (uiShot && frame == 10) front.capture = "ui.png";
            if (uiShot && frame > 10) break;
            Ui::Frontend::Action a = front.frame(opt, maps, host, port, name);
            if (a == Ui::Frontend::Quit) break;
            if (a == Ui::Frontend::StartLocal) {
                online = false;
                opt.seed = (uint32_t)(clock * 1000) + frame;
                startMatch(opt);
            } else if (a == Ui::Frontend::StartOnline) {
                online = true;
                status = net.connect(host.c_str(), port, name.c_str()) ? "Connecting to " + host + "..." : "Cannot reach " + host;
                screen = Screen::Lobby;
            }
            continue;
        }

        if (screen == Screen::Lobby) {
            bool inRoom = net.roomId != 0, isHost = inRoom && net.hostId == net.id;
            if (!inRoom) {
                int n = (int)net.rooms.size();
                if (n) roomSel = (roomSel + pressed({GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {KEY_DOWN}) - pressed({GAMEPAD_BUTTON_LEFT_FACE_UP}, {KEY_UP}) + n) % n;
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER}) && n) net.joinRoom(net.rooms[roomSel].id);
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_UP}, {KEY_C})) net.createRoom((name + "'s room").c_str(), 4);
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_R})) net.listRooms();
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) { net.close(); online = false; screen = Screen::Menu; }
            } else {
                if (isHost && net.players.size() >= 2 && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER})) {
                    std::vector<uint32_t> owners;
                    for (size_t i = 0; i < net.players.size() && i < 4; i++) owners.push_back(net.players[i].id);
                    GameConfig c = opt;
                    c.seed = (uint32_t)(clock * 1000) + frame;
                    c.teams = (int)owners.size();
                    net.start(c, owners);
                }
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE, KEY_ESCAPE})) { net.leave(); net.listRooms(); }
            }
            BeginDrawing();
            Ui::background();
            drawTextCentered(inRoom ? "ROOM" : "ONLINE LOBBY", 640, 60, 60, {255, 220, 120, 255});
            if (!inRoom) {
                for (size_t i = 0; i < net.rooms.size(); i++) {
                    const NetRoom &r = net.rooms[i];
                    drawTextCentered(TextFormat("%s %s  (%d/%d)%s", (int)i == roomSel ? ">" : " ", r.name.c_str(), r.players, r.maxPlayers, r.started ? " playing" : ""),
                                     640, 160 + (int)i * 40, 30, (int)i == roomSel ? YELLOW : WHITE);
                }
                if (net.rooms.empty()) drawTextCentered("No rooms yet", 640, 200, 30, LIGHTGRAY);
                Ui::hints({{"A", "Enter", "Join"}, {"X", "C", "Create room"}, {"Y", "R", "Refresh"}, {"B", "Esc", "Back"}});
            } else {
                for (size_t i = 0; i < net.players.size(); i++) {
                    const NetPlayer &pl = net.players[i];
                    drawTextCentered(TextFormat("%s%s%s", pl.name.c_str(), pl.id == net.hostId ? " (host)" : "", pl.online ? "" : " - offline"), 640, 160 + (int)i * 40, 30,
                                     i < 4 ? TEAM_COLORS[i] : GRAY);
                }
                if (isHost) Ui::hints({{"A", "Enter", "Start (2+ players)"}, {"B", "Esc", "Leave room"}});
                else Ui::hints({{"B", "Esc", "Leave room"}});
                if (!isHost) drawTextCentered("Waiting for the host to start...", 640, 600, 24, LIGHTGRAY);
            }
            drawTextCentered(status.c_str(), 640, 660, 22, ORANGE);
            EndDrawing();
            continue;
        }

        mark = GetTime();
        const Worm &cur = game.worms[game.current];
        int pad = !online && IsGamepadAvailable(cur.team) ? cur.team : 0;
        if (!shot && pause.update() == Ui::Pause::Quit) {
            if (online) net.leave(), net.listRooms();
            screen = online ? Screen::Lobby : Screen::Menu;
            Audio::music(true, "theme");
            continue;
        }
        Input in = shot ? scriptInput(frame, shotWeapon) : pause.open ? Input{} : readInput(pad);
        for (const NetPlayer &pl : net.players)
            if (pl.online) offlineSince.erase(pl.id);
            else offlineSince.emplace(pl.id, clock);
        // the host plays idle turns for owners gone > 30 s so a dropout can't stall the match
        // ponytail: if the owner reconnects mid-turn both may send the same tick; server keeps the first
        auto proxied = [&](int team) {
            auto it = team < (int)net.owners.size() ? offlineSince.find(net.owners[team]) : offlineSince.end();
            return net.hostId == net.id && it != offlineSince.end() && clock - it->second > 30;
        };
        // the host also plays the CPU teams
        auto owns = [&](int team) { return (cpu(team) && net.hostId == net.id) || (team < (int)net.owners.size() && (net.owners[team] == net.id || proxied(team))); };
        bool remoteTurn = online && game.phase != Phase::GameOver && !owns(cur.team);
        hud.input(game, in, !shot && !remoteTurn && !pause.open, pad, tick);
        if (!online) {
            for (acc += pause.open ? 0 : dt; acc >= Game::DT; acc -= Game::DT) stepOnce(!shot && cpu(game.worms[game.current].team) ? ai.think(game) : in);
        } else {
            // remote/replayed inputs first, then ours when we own the active team; otherwise wait
            acc = fminf(acc + dt, Game::DT * 4);
            for (int budget = 240; budget > 0; budget--) {
                Input r;
                if (net.remoteInput(tick, r)) { stepOnce(r); continue; }
                bool mine = game.phase != Phase::GameOver && owns(game.worms[game.current].team);
                if (!mine || acc < Game::DT) break;
                int team = game.worms[game.current].team;
                Input mineIn = cpu(team) ? ai.think(game) : proxied(team) ? Input{} : in;
                net.sendInput(tick, mineIn);
                stepOnce(mineIn);
                acc -= Game::DT;
            }
        }
        lap(T_SIM);
        game.terrain.remesh();
        lap(T_REMESH);
        int sec = game.phase == Phase::Aim && game.timer <= 300 ? game.timer / 60 : -1;
        if (sec >= 0 && sec != lastSec) Audio::play(Audio::Sfx::Tick);
        lastSec = sec;
        if (game.phase == Phase::GameOver && !pause.open && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) {
            screen = online ? Screen::Lobby : Screen::Menu;
            Audio::music(true, "theme");
        }

        // camera: behind the active worm (right stick X / A D orbits, ZL ZR / Z X / wheel zoom), chasing the projectile,
        // or through the sniper's eyes while aiming it
        const WeaponDef &wd = WEAPONS[game.weapon];
        bool chase = game.phase == Phase::Flying && !game.shots.empty();
        bool scope = !chase && game.phase == Phase::Aim && cur.alive && !game.roped && !game.jetting && wd.name == "Sniper Rifle";
        bool camIn = !pause.open && !hud.open;
        float ox = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_RIGHT_X);
        ox = camIn * ((fabsf(ox) < 0.2f ? 0 : ox) + IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
        orbit = ox ? Clamp(orbit - ox * dt * 2.5f, -PI, PI) : orbit * expf(-dt * 0.7f);  // springs back behind the worm
        float zin = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) + IsKeyDown(KEY_X) - IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_2) - IsKeyDown(KEY_Z);
        zoom = Clamp(zoom * expf(camIn * (-zin * dt * 1.5f - GetMouseWheelMove() * 0.1f)), 0.45f, 2.5f);
        if ((IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) &&
             (IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) || IsGamepadButtonPressed(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))) || IsKeyPressed(KEY_F3))
            perfOn = (perfOn + 1) % 3;
        Vector3 focus = cur.pos;
        if (chase) focus = Vector3Add(game.shots[0].pos, Vector3Scale(game.shots[0].vel, 0.1f));  // lead the shot a little
        float dy = cur.yaw + orbit - camYaw;
        camYaw += atan2f(sinf(dy), cosf(dy)) * (1 - expf(-dt * 4));
        float back = (chase ? 16 : 9) * zoom;
        Vector3 want = Vector3Add(focus, {-sinf(camYaw) * back, (chase ? 7.0f : 4.0f) * zoom, -cosf(camYaw) * back});
        Vector3 hit, to = Vector3Subtract(want, focus);
        if (chase) want.y = fmaxf(want.y, cur.pos.y + 4);  // donkey/airstrike dig below the surface: stay above ground
        else if (game.terrain.raycast({focus, Vector3Normalize(to)}, Vector3Length(to), &hit)) want = Vector3Lerp(focus, hit, 0.85f);  // orbiting into a hill
        float kt = 1 - expf(-dt * 6), kp = 1 - expf(-dt * (chase ? 2.5f : 3));
        if (scope) {
            want = Vector3Add(cur.pos, {0, 0.35f, 0});
            focus = Vector3Add(want, Vector3Scale(game.aimDir(cur), 30));
            kt = kp = 1 - expf(-dt * 12);
        }
        cam.target = Vector3Lerp(cam.target, focus, kt);
        cam.position = Vector3Lerp(cam.position, want, kp);
        cam.fovy = Lerp(cam.fovy, scope ? 25.0f : 50.0f, 1 - expf(-dt * 8));
        Camera3D view = cam;  // shaken copy: the smoothed camera itself never drifts
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
        for (const Worm &w : game.worms) {
            if (!w.alive) { drawGrave(game, w); continue; }
            if (scope && &w == &cur) continue;  // the camera is inside it
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
                          : o.weapon < 0 ? "crate_health" : "crate_weapon";
            if (!Models::draw(m, o.pos, (&o - game.objects.data()) * 1.3f)) {
                if (o.type == Object::Mine) DrawCylinder({o.pos.x, o.pos.y - 0.1f, o.pos.z}, 0.15f, 0.2f, 0.2f, 8, DARKGRAY);
                else if (o.type == Object::Barrel) DrawCylinder({o.pos.x, o.pos.y - 0.5f, o.pos.z}, 0.35f, 0.35f, 1, 10, MAROON);
                else DrawCube(o.pos, 0.8f, 0.8f, 0.8f, o.weapon < 0 ? RAYWHITE : BROWN);
            }
            if (o.type == Object::Mine && o.fuse >= 0 && fmodf(clock, 0.3f) < 0.15f) DrawSphere(Vector3Add(o.pos, {0, 0.15f, 0}), 0.08f, RED);
            if (o.falling) {
                Vector3 top = Vector3Add(o.pos, {0, 2.2f, 0});
                DrawCylinderEx(top, Vector3Add(top, {0, 0.5f, 0}), 1.1f, 0.3f, 10, o.weapon < 0 ? RED : ORANGE);
                for (float a : {0.8f, 2.4f, 3.9f, 5.5f}) DrawLine3D(Vector3Add(o.pos, {0, 0.4f, 0}), Vector3Add(top, {cosf(a) * 1.1f, 0, sinf(a) * 1.1f}), LIGHTGRAY);
            }
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
        hud.draw(game, view, tick);
        pause.draw(online);
        if (remoteTurn) drawTextCentered("Remote player's turn", 640, 90, 24, WHITE);
        if (online && !status.empty()) drawTextCentered(status.c_str(), 640, 120, 24, ORANGE);
        if (perfOn) {
            static const char *NAMES[T_COUNT] = {"sim", "remesh", "sky+water", "terrain", "models", "fx", "ui", "present+wait"};
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
        for (int k = 0; k < T_COUNT; k++) perf[k] = perf[k] * 0.95 + cost[k] * 0.05, cost[k] = 0;
        if ((shot && frame == 150) || (uiShot && frame == 40)) break;
    }
    net.close();
    game.terrain.unload();
    Models::unload();
    Ui::unload();
    Fx::unload();
    Audio::shutdown();
    CloseWindow();
}

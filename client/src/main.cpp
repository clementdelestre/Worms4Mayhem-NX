#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "audio.h"
#include "models.h"
#include "net.h"
#include "sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static const Color TEAM_COLORS[] = {{220, 50, 50, 255}, {50, 110, 230, 255}, {60, 190, 70, 255}, {240, 200, 40, 255}};

static bool pressedAny(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys) {
    for (int b : buttons) if (IsGamepadButtonPressed(pad, b)) return true;
    for (int k : keys) if (IsKeyPressed(k)) return true;
    return false;
}

static Input readInput(int pad) {
    auto ax = [&](int a) {
        float v = GetGamepadAxisMovement(pad, a);
        return fabsf(v) < 0.2f ? 0.0f : v;
    };
    auto q = [](float v) { return (int8_t)(Clamp(v, -1, 1) * 127); };
    // yaw grows toward +x, which is the camera's left
    Input in;
    in.turn = q(-ax(GAMEPAD_AXIS_LEFT_X) + IsKeyDown(KEY_LEFT) - IsKeyDown(KEY_RIGHT));
    in.walk = q(-ax(GAMEPAD_AXIS_LEFT_Y) + IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN));
    in.aim = q(-ax(GAMEPAD_AXIS_RIGHT_Y) + IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsKeyDown(KEY_SPACE)) in.buttons |= Input::FIRE;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsKeyDown(KEY_ENTER)) in.buttons |= Input::JUMP;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || IsKeyDown(KEY_TAB))
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
    int w = MeasureText(t, size);
    DrawText(t, x - w / 2 + 2, y + 2, size, {0, 0, 0, 160});
    DrawText(t, x - w / 2, y, size, c);
}

struct Fx { Vector3 p; float t, r; };

static void onEvent(const Game &g, const GameEvent &e, std::vector<Fx> &fx) {
    using Audio::Sfx;
    using Audio::Voice;
    int team = e.worm >= 0 ? g.worms[e.worm].team : 0;
    switch (e.kind) {
    case GameEvent::Boom: fx.push_back({e.pos, 0, 3}); Audio::play(Sfx::Explosion); break;
    case GameEvent::BigBoom: fx.push_back({e.pos, 0, 7}); Audio::play(Sfx::Holy); Audio::play(Sfx::BigExplosion); break;
    case GameEvent::Fire: {
        Kind k = WEAPONS[e.weapon].kind;
        static const Sfx FIRE_SFX[] = {Sfx::Fire, Sfx::Sheep, Sfx::Airstrike, Sfx::Donkey, Sfx::Shotgun, Sfx::Rope, Sfx::Fire, Sfx::Teleport};  // by Kind
        Audio::play(FIRE_SFX[(int)k]);
        if (k != Kind::Rope && k != Kind::Jetpack && k != Kind::Teleport) Audio::voice(team, Voice::Fire);
        break;
    }
    case GameEvent::Bounce: Audio::play(Sfx::Bounce, 0.6f); break;
    case GameEvent::Splash: Audio::play(Sfx::Splash); break;
    case GameEvent::Death: Audio::voice(team, Voice::Death); break;
    case GameEvent::Hurt: Audio::voice(team, Voice::Hurt); break;
    case GameEvent::Jump: Audio::play(Sfx::Jump); Audio::voice(team, Voice::Jump); break;
    case GameEvent::TurnStart: Audio::play(Sfx::TurnStart); Audio::voice(team, Voice::Idle); break;
    case GameEvent::CrateDrop: Audio::play(Sfx::Airstrike, 0.5f); break;
    case GameEvent::Collect: Audio::play(Sfx::Teleport, 0.7f); break;
    case GameEvent::MineArm: Audio::play(Sfx::Tick); break;
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
        *clip = d.fuse > 0 ? "AimGrenade" : "AimBazooka";
        return d.fuse <= 0 ? "hold_bazooka" : n == "Cluster Grenade" ? "hold_cluster" : n == "Banana Bomb" ? "hold_banana"
             : n == "Holy Hand Grenade" ? "hold_holy" : "hold_grenade";
    case Kind::Shotgun: *clip = "AimShotgun"; return "hold_shotgun";
    case Kind::Sheep: *clip = "HoldBazooka"; return "hold_sheep";  // HoldSheep tilts the whole worm with our clip layering
    case Kind::Airstrike: case Kind::Donkey: *clip = "HoldAirstrike"; return "hold_radio";
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
    const char *m = d.kind == Kind::Sheep ? "sheep" : d.kind == Kind::Donkey ? "donkey" : d.kind == Kind::Airstrike ? "airstrike"
                  : n == "Cluster Grenade" ? (s.child ? "clusterlet" : "cluster") : n == "Banana Bomb" ? (s.child ? "bananette" : "banana")
                  : n == "Holy Hand Grenade" ? "holy" : d.fuse > 0 ? "grenade" : "bazooka";
    float h = sqrtf(s.vel.x * s.vel.x + s.vel.z * s.vel.z), yaw = atan2f(s.vel.x, s.vel.z);
    if (d.kind == Kind::Sheep || d.kind == Kind::Donkey) return Models::draw(m, s.pos, yaw, 0, WHITE, "Run", clock);
    if (d.fuse > 0 && d.kind == Kind::Shell) return Models::draw(m, s.pos, clock * 6, clock * 4);
    return Models::draw(m, s.pos, yaw, atan2f(s.vel.y, h));
}

enum class Screen { Menu, Lobby, Play };

int main(int argc, char **argv) {
    InitWindow(1280, 720, "Worms4NX");
    SetTargetFPS(60);
    rlSetClipPlanes(0.5, 500);  // default 0.01 near plane z-fights the water on GLES depth buffers
    Audio::init();
    Audio::music(true);
    Models::load();

    // Shot mode (flag file or --shot): scripted turn, screenshot, quit. Lets us check rendering in the emulator.
    bool shot = argc > 1 || FileExists(DATA_DIR "shot");
    int shotWeapon = argc > 2 ? atoi(argv[2]) : 0;  // --shot N: use weapon N
    if (!loadWeapons(ROMFS_DIR "weapons.json")) TraceLog(LOG_WARNING, "weapons.json missing or invalid, using built-in weapons");

    // server.txt on the SD card: "<host> [port] [name]" — saves us an on-screen keyboard
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
    bool online = false, optOnline = false;
    Screen screen = shot ? Screen::Play : Screen::Menu;
    int menuRow = 0, optMap = 0, roomSel = 0, lastSec = -1;
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
    static const char *RULE_LABELS[] = {"King", "Highlander", "Vampire", "Karma", "Low gravity", "Rope race", "Sudden death"};
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
    if (shot) { game.start({1234, 2, 2, shotMap, argc > 4 ? (uint32_t)atoi(argv[4]) : 0u}); game.terrain.remesh(); }

    std::vector<Fx> fx;
    Camera3D cam = {{40, 30, 0}, {40, 8, 40}, {0, 1, 0}, 50, CAMERA_PERSPECTIVE};
    float camYaw = 0, acc = 0, clock = 0, reconnectAt = 0;

    auto stepOnce = [&](const Input &in) {
        game.step(in);
        tick++;
        for (const GameEvent &e : game.events) {
            onEvent(game, e, fx);
            if (online && e.kind == GameEvent::TurnStart) net.turnEnd(tick, game.checksum());
        }
    };
    auto startMatch = [&](const GameConfig &c) {
        game.start(c);
        Audio::music(true, game.terrain.theme.empty() ? "theme" : game.terrain.theme.c_str());
        game.terrain.remesh();
        fx.clear();
        tick = 0;
        acc = 0;
        screen = Screen::Play;
    };
    auto pressed = [](std::initializer_list<int> buttons, std::initializer_list<int> keys) { return pressedAny(0, buttons, keys); };

    for (int frame = 0; !WindowShouldClose(); frame++) {
        float dt = fminf(GetFrameTime(), 0.25f);
        clock += dt;
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
            const int ROWS = 4 + 7;  // teams, worms, map, mode, then one toggle per rule
            if (pressed({GAMEPAD_BUTTON_LEFT_FACE_UP}, {KEY_UP})) menuRow = (menuRow + ROWS - 1) % ROWS;
            if (pressed({GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {KEY_DOWN})) menuRow = (menuRow + 1) % ROWS;
            int d = pressed({GAMEPAD_BUTTON_LEFT_FACE_RIGHT}, {KEY_RIGHT}) - pressed({GAMEPAD_BUTTON_LEFT_FACE_LEFT}, {KEY_LEFT});
            if (menuRow == 0) opt.teams = Clamp(opt.teams + d, 2, 4);
            if (menuRow == 1) opt.wormsPerTeam = Clamp(opt.wormsPerTeam + d, 1, 4);
            if (menuRow == 2) optMap = (optMap + d + (int)maps.size()) % (int)maps.size();
            if (menuRow == 3 && d) optOnline = !optOnline;
            if (menuRow >= 4 && d) opt.rules ^= 1u << (menuRow - 4);
            opt.map = maps[optMap];
            if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER})) {
                if (!optOnline) {
                    online = false;
                    opt.seed = (uint32_t)(clock * 1000) + frame;
                    startMatch(opt);
                } else {
                    online = true;
                    status = net.connect(host.c_str(), port, name.c_str()) ? "Connecting to " + host + "..." : "Cannot reach " + host;
                    screen = Screen::Lobby;
                }
            }
            BeginDrawing();
            ClearBackground({40, 70, 120, 255});
            drawTextCentered("WORMS4NX", 640, 40, 70, {255, 220, 120, 255});
            for (int r = 0; r < ROWS; r++) {
                const char *t = r == 0 ? TextFormat("Teams: %d", opt.teams)
                              : r == 1 ? TextFormat("Worms per team: %d", opt.wormsPerTeam)
                              : r == 2 ? TextFormat("Map: %s", opt.map.empty() ? "Random island" : opt.map.c_str())
                              : r == 3 ? (optOnline ? "Mode: Online" : "Mode: Local")
                                       : TextFormat("%s: %s", RULE_LABELS[r - 4], opt.rules & (1u << (r - 4)) ? "ON" : "off");
                drawTextCentered(TextFormat("%s %s", menuRow == r ? ">" : " ", t), 640, 140 + r * 44, 32, menuRow == r ? YELLOW : WHITE);
            }
            drawTextCentered(optOnline ? TextFormat("Server %s:%d as %s (edit server.txt)", host.c_str(), port, name.c_str())
                                       : "Local: one controller per team, or share one", 640, 600, 24, LIGHTGRAY);
            drawTextCentered("Left/Right: change   A / Space: start", 640, 640, 24, LIGHTGRAY);
            EndDrawing();
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
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE})) { net.close(); online = false; screen = Screen::Menu; }
            } else {
                if (isHost && net.players.size() >= 2 && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE, KEY_ENTER})) {
                    std::vector<uint32_t> owners;
                    for (size_t i = 0; i < net.players.size() && i < 4; i++) owners.push_back(net.players[i].id);
                    GameConfig c = opt;
                    c.seed = (uint32_t)(clock * 1000) + frame;
                    c.teams = (int)owners.size();
                    net.start(c, owners);
                }
                if (pressed({GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_BACKSPACE})) { net.leave(); net.listRooms(); }
            }
            BeginDrawing();
            ClearBackground({40, 70, 120, 255});
            drawTextCentered(inRoom ? "ROOM" : "ONLINE LOBBY", 640, 60, 60, {255, 220, 120, 255});
            if (!inRoom) {
                for (size_t i = 0; i < net.rooms.size(); i++) {
                    const NetRoom &r = net.rooms[i];
                    drawTextCentered(TextFormat("%s %s  (%d/%d)%s", (int)i == roomSel ? ">" : " ", r.name.c_str(), r.players, r.maxPlayers, r.started ? " playing" : ""),
                                     640, 160 + (int)i * 40, 30, (int)i == roomSel ? YELLOW : WHITE);
                }
                if (net.rooms.empty()) drawTextCentered("No rooms yet", 640, 200, 30, LIGHTGRAY);
                drawTextCentered("A: join   X: create   Y: refresh   B: back", 640, 600, 24, LIGHTGRAY);
            } else {
                for (size_t i = 0; i < net.players.size(); i++) {
                    const NetPlayer &pl = net.players[i];
                    drawTextCentered(TextFormat("%s%s%s", pl.name.c_str(), pl.id == net.hostId ? " (host)" : "", pl.online ? "" : " - offline"), 640, 160 + (int)i * 40, 30,
                                     i < 4 ? TEAM_COLORS[i] : GRAY);
                }
                drawTextCentered(isHost ? "A: start (2+ players)   B: leave" : "Waiting for host...   B: leave", 640, 600, 24, LIGHTGRAY);
            }
            drawTextCentered(status.c_str(), 640, 660, 22, ORANGE);
            EndDrawing();
            continue;
        }

        const Worm &cur = game.worms[game.current];
        int pad = !online && IsGamepadAvailable(cur.team) ? cur.team : 0;
        Input in = shot ? scriptInput(frame, shotWeapon) : readInput(pad);
        for (const NetPlayer &pl : net.players)
            if (pl.online) offlineSince.erase(pl.id);
            else offlineSince.emplace(pl.id, clock);
        // the host plays idle turns for owners gone > 30 s so a dropout can't stall the match
        // ponytail: if the owner reconnects mid-turn both may send the same tick; server keeps the first
        auto proxied = [&](int team) {
            auto it = team < (int)net.owners.size() ? offlineSince.find(net.owners[team]) : offlineSince.end();
            return net.hostId == net.id && it != offlineSince.end() && clock - it->second > 30;
        };
        auto owns = [&](int team) { return team < (int)net.owners.size() && (net.owners[team] == net.id || proxied(team)); };
        bool remoteTurn = online && game.phase != Phase::GameOver && !owns(cur.team);
        if (!online) {
            for (acc += dt; acc >= Game::DT; acc -= Game::DT) stepOnce(in);
        } else {
            // remote/replayed inputs first, then ours when we own the active team; otherwise wait
            acc = fminf(acc + dt, Game::DT * 4);
            for (int budget = 240; budget > 0; budget--) {
                Input r;
                if (net.remoteInput(tick, r)) { stepOnce(r); continue; }
                bool mine = game.phase != Phase::GameOver && owns(game.worms[game.current].team);
                if (!mine || acc < Game::DT) break;
                Input mineIn = proxied(game.worms[game.current].team) ? Input{} : in;
                net.sendInput(tick, mineIn);
                stepOnce(mineIn);
                acc -= Game::DT;
            }
        }
        game.terrain.remesh();
        int sec = game.phase == Phase::Aim && game.timer <= 300 ? game.timer / 60 : -1;
        if (sec >= 0 && sec != lastSec) Audio::play(Audio::Sfx::Tick);
        lastSec = sec;
        if (game.phase == Phase::GameOver && pressed({GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {KEY_SPACE})) {
            screen = online ? Screen::Lobby : Screen::Menu;
            Audio::music(true, "theme");
        }

        // camera: behind the active worm, or chasing the projectile
        bool chase = game.phase == Phase::Flying && !game.shots.empty();
        Vector3 focus = chase ? game.shots[0].pos : cur.pos;
        float dy = cur.yaw - camYaw;
        camYaw += atan2f(sinf(dy), cosf(dy)) * fminf(1, dt * 4);
        float back = chase ? 16 : 9;
        Vector3 want = Vector3Add(focus, {-sinf(camYaw) * back, chase ? 7.0f : 4.0f, -cosf(camYaw) * back});
        want.y = fmaxf(want.y, cur.pos.y + 4);  // donkey/airstrike dig below the surface: stay above ground
        cam.target = Vector3Lerp(cam.target, focus, fminf(1, dt * 6));
        cam.position = Vector3Lerp(cam.position, want, fminf(1, dt * 3));

        BeginDrawing();
        ClearBackground(game.terrain.sky);
        BeginMode3D(cam);
        game.terrain.draw();
        game.terrain.drawObjects(cam.position);
        for (const Worm &w : game.worms) {
            if (!w.alive) { drawGrave(game, w); continue; }
            if (!drawWorm(game, w, clock)) {
                Vector3 f = {sinf(w.yaw), 0, cosf(w.yaw)}, side = {f.z, 0, -f.x};
                DrawCapsule({w.pos.x, w.pos.y - 0.2f, w.pos.z}, {w.pos.x, w.pos.y + 0.3f, w.pos.z}, 0.35f, 8, 6, TEAM_COLORS[w.team]);
                for (float s : {-0.13f, 0.13f})
                    DrawSphere(Vector3Add(w.pos, Vector3Add(Vector3Scale(f, 0.3f), Vector3Add(Vector3Scale(side, s), {0, 0.4f, 0}))), 0.1f, WHITE);
            }
            if ((game.cfg.rules & RULE_KING) && int(&w - game.worms.data()) % game.perTeam == 0)
                DrawCylinderEx({w.pos.x, w.pos.y + 0.55f, w.pos.z}, {w.pos.x, w.pos.y + 0.8f, w.pos.z}, 0.28f, 0.08f, 6, GOLD);
        }
        const WeaponDef &wd = WEAPONS[game.weapon];
        if (game.roped) DrawLine3D(game.anchor, cur.pos, BROWN);
        if (game.jetting) DrawCube(Vector3Add(cur.pos, {-sinf(cur.yaw) * 0.4f, 0.1f, -cosf(cur.yaw) * 0.4f}), 0.35f, 0.5f, 0.35f, GRAY);
        if (game.phase == Phase::Aim && cur.alive && !game.roped && !game.jetting) {
            if (wd.kind == Kind::Airstrike || wd.kind == Kind::Donkey || wd.kind == Kind::Teleport) {
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
            const char *m = o.type == Object::Mine ? "mine" : o.type == Object::Barrel ? "barrel" : o.weapon < 0 ? "crate_health" : "crate_weapon";
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
        DrawPlane({40, game.water, 40}, {400, 400}, {30, 80, 160, 180});
        for (size_t i = 0; i < fx.size();) {
            fx[i].t += dt;
            if (fx[i].t > 0.4f) { fx.erase(fx.begin() + i); continue; }
            float k = fx[i].t / 0.4f;
            DrawSphere(fx[i].p, fx[i].r * (0.5f + k), Fade(ORANGE, 1 - k));
            i++;
        }
        EndMode3D();

        for (const Worm &w : game.worms) {
            if (!w.alive) continue;
            Vector2 sp = GetWorldToScreen(Vector3Add(w.pos, {0, 1.1f, 0}), cam);
            drawTextCentered(TextFormat("%d", w.hp > 0 ? w.hp : 0), sp.x, sp.y, 20, TEAM_COLORS[w.team]);
        }
        if (game.phase == Phase::GameOver) {
            drawTextCentered(game.winner >= 0 ? TextFormat("TEAM %d WINS!", game.winner + 1) : "DRAW!", 640, 280, 70, game.winner >= 0 ? TEAM_COLORS[game.winner] : WHITE);
            drawTextCentered("A / Space: menu", 640, 380, 30, WHITE);
        } else {
            drawTextCentered(TextFormat("Team %d  -  %d", cur.team + 1, game.phase == Phase::Aim ? game.timer / 60 : 0), 640, 16, 30, TEAM_COLORS[cur.team]);
            if (game.cfg.rules & RULE_ROPE_RACE) DrawText(TextFormat("Race %ds", tick / 60), 1080, 16, 26, GOLD);
            DrawRectangle(540, 56, 200, 12, {0, 0, 0, 120});
            DrawRectangle(game.wind > 0 ? 640 : 640 + (int)(game.wind * 100), 56, (int)fabsf(game.wind * 100), 12, game.wind > 0 ? SKYBLUE : PINK);
            int ammo = game.ammo[cur.team][game.weapon];
            DrawText(TextFormat("%s  %s", wd.name.c_str(), ammo < 0 ? "inf" : TextFormat("x%d", ammo)), 20, 680, 28, ammo ? WHITE : GRAY);
            if (game.shotsLeft) DrawText(TextFormat("%d shot(s) left", game.shotsLeft), 20, 620, 22, WHITE);
            if (game.roped) DrawText("Rope: stick swings, aim = length, jump releases", 20, 620, 22, WHITE);
            if (game.jetting) {
                DrawText("Jetpack: hold fire to thrust, jump to stop", 20, 620, 22, WHITE);
                DrawRectangle(20, 650, (int)(300 * game.fuel / fmaxf(wd.fuse, 0.01f)), 18, SKYBLUE);
            }
            if (game.power > 0) {
                DrawRectangle(20, 650, 300, 18, {0, 0, 0, 120});
                DrawRectangle(20, 650, (int)(300 * game.power), 18, ColorLerp(YELLOW, RED, game.power));
            }
            for (int t = 0; t < game.teams; t++) {
                int hp = 0;
                for (const Worm &w : game.worms) if (w.team == t && w.alive) hp += w.hp > 0 ? w.hp : 0;
                DrawRectangle(1260 - hp / 2, 680 - t * 22, hp / 2, 16, TEAM_COLORS[t]);
            }
        }
        if (remoteTurn) drawTextCentered("Remote player's turn", 640, 90, 24, WHITE);
        if (online && !status.empty()) drawTextCentered(status.c_str(), 640, 120, 24, ORANGE);
        DrawFPS(10, 10);
        if (shot && (frame == 35 || frame == 150)) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, frame == 35 ? DATA_DIR "shot_aim.png" : DATA_DIR "shot.png");
            UnloadImage(img);
        }
        EndDrawing();
        if (shot && frame == 150) break;
    }
    net.close();
    game.terrain.unload();
    Models::unload();
    Audio::shutdown();
    CloseWindow();
}

#pragma once
#include "raylib.h"
#include "mission.h"
#include "net.h"
#include "sim.h"
#include <string>
#include <vector>

// W4M-styled frontend and HUD. Art: assets/ui/*.png from tools/w4m-ui; missing files fall back to plain shapes.
namespace Ui {
extern const Color TEAM_COLORS[4];
void load();
bool preload(double until);  // boot: uploads the decoded frontend art until GetTime() reaches until; true while some is pending
void unload();
bool warmWeaponIcons(int &i);  // loads weapon icon i++ into the cache (match prep, not on the first panel open); true while more remain
void text(const char *t, float x, float y, float size, Color c, int align = 0);  // align: 0 left, 1 centre, 2 right
float textWidth(const char *t, float size);
const Font &textFont();  // the font text() draws, for rotated or outline-free text
Texture2D art(const char *name);  // assets/ui/<name>.png, cached; id 0 when missing
void logo(float cx, float y, float w, float deg = 0);  // W4M logo, top centre at (cx, y), tilted deg
// Menu language: 0 English, 1 French (lang.txt, else the system language). tr(): W4M's string for key
// (assets/lang/<en|fr>.txt from tools/w4m-ui), else the built-in en / fr text (key may be null).
extern int language;
const char *tr(const char *key, const char *en, const char *fr = nullptr);
void background();
std::string teamName(const GameConfig &c, int team);
const char *wormName(int team, int i);
// pad -1 = any pad. D-pad buttons also fire on left-stick flicks and auto-repeat while held.
bool pressed(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys);
void pollStick();  // once per frame, before any pressed()
// Bottom button hints. pad: "A" "B" "X" "Y" "+" "-" "L" "R" "ZL"...; key: shown on desktop without a controller.
struct Hint { const char *pad, *key, *label; };
void hints(std::initializer_list<Hint> h);
void hints(const std::vector<Hint> &h);
void controls(bool game);  // full-screen controller diagram: match or menu controls
bool helpHeld();            // - (desktop F1) held past 0.35 s: show controls()
extern bool forceHelp;      // --ui help captures

struct MenuItem;  // ui.cpp: one W4M menu entry and its place on screen
// Title, main menu, match setup, options. frame() updates and draws (inside Begin/EndDrawing).
struct Frontend {
    enum Screen { Title, Main, Setup, Options, Controls, SchemeEdit, Wormpot, Factory, FactoryEdit, Local, Network, MyWorms, HelpOpts, Confirm } screen = Title;
    enum Action { None, StartLocal, StartOnline, Quit, Replays, StartLan, SinglePlayer, QuickMatch };
    const char *capture = nullptr;  // screenshot path for the next frame (--ui)
    int missionTab = 0;             // SinglePlayer: 0 story missions, 1 challenges
    void go(Screen s);              // W4M menu change: current items fly out, then s slides in
    Action frame(GameConfig &cfg, const std::vector<std::string> &maps, std::string &host, int &port, std::string &name);

private:
    int row = 0, mainRow = 0, mapSel = 0, hats = 0, schemeRow = 0, subRow[5] = {};  // subRow: Local, Network, MyWorms, HelpOpts, Confirm
    Screen shown = (Screen)-1, from = Title;  // W4M menus: item slide-in since `entered` (from: previous screen), smoothed highlight per item
    float entered = -100, glow[8] = {};
    Screen next = Title;  // W4M menu change: the current items fly out for LEAVE s since `leaving`, then `next` slides in
    float leaving = -1;
    float subIn(float t) const;  // submenu panel progress: 0 hidden .. 1 shown
    void menu(const MenuItem *items, int n, int &sel, int dy, float t, bool live = true);  // live: animate + take input
    bool online = false, lan = false, loaded = false, music = true, layout = false;  // layout: Controls shows the pad diagram  // online: network setup (LAN or server)
    std::string *editing = nullptr;  // desktop text entry target
    void loadSetup(GameConfig &cfg, const std::vector<std::string> &maps);
    void saveSetup(const GameConfig &cfg) const;
    bool edit(std::string &s, const char *hint);  // true when s changed (Switch: swkbd, desktop: starts inline entry)
    // Wormpot slot machine (3 reels) and Weapon Factory (custom_weapons.json, sent to the room in Start)
    int reel = 0, spinTo[3] = {}, facSel = 0, facRow = 0;
    float spinEnd[3] = {};
    std::vector<WeaponDef> customs;
    void wormpot(GameConfig &cfg, int dx, int dy, bool ok, bool back, float t);
    void factory(int dx, int dy, bool ok, bool back);
    void factoryEdit(int dx, int dy, bool ok, bool back, bool typing, float t);
};

// In-game HUD and W4M weapon panel (X / Q). A pick goes out as Input::pick() until game.held() shows it (a frame may run no tick).
// First-person aim reticle at screen point c, per weapon like W4M; scope: sniper vignette + cross.
void reticle(const WeaponDef &wd, Vector2 c, bool scope);
// W4M Blimp-view reticle at the screen centre: Bomber cursor (arrows = the run, left to right) or Targeting cursor.
void targetCursor(const WeaponDef &wd, int state, const Vector2 *lock = nullptr, const Vector2 *at = nullptr);  // state: 0 valid, 1 water, 2 no target, -1 none; lock: homing marker; at: centre, default screen centre
Vector2 windPointer(bool live, float wx, float wz, Vector2 fwd);  // wind meter needle on screen (y down), length foreshortened
// W4M PiP (HUDTWK PiP.*, WXFE_Border_Bubble): the event camera's picture in its tilted inset; show / full as Controls::inset
void pipInset(const RenderTexture2D &scene, float show, float full);
struct Hud {
    bool skipHp = false;  // X pressed during the count (user-requested)
    int counting = -1;  // worm whose label is counting, camera on it; wait: camera travel / linger seconds
    bool reopen = false;  // X skipped the Settle count: the next local Aim opens the panel
    bool forceX = false;  // tests: X pressed
    bool readyScreen(const Game &g, bool cinematic) const;
    bool open = false, mine = false;  // mine: a human here plays the current turn
    bool quiet = false;               // no bottom hints this frame
    bool fp = false;                  // first-person aim, or just left it: no label on the current worm
    float pipShow = 0, pipFull = 0;   // Controls::inset: labels stay off the PiP, ActWormInfo moves to its PosPiP
    // local: a human here plays the current turn; tick: ticks simulated so far
    void input(const Game &g, Input &in, bool local, int pad, uint32_t tick);
    void draw(const Game &g, const Camera3D &cam, uint32_t tick);
    void select(int weapon) { pick = weapon, open = false, swallow = true; }

private:
    int cursor = 0, pick = -1;  // pick: sent until it lands
    bool swallow = true;  // a held A (weapon pick, menu START) must not fire
    int introWorm = -1;       // turn-start name banner (CPU/remote): current worm and the tick it became current
    uint32_t introStart = 0;
    // W4M hp count: labels/bars show `shown`, which ticks toward the sim's counted hp (Settle: timed by the sim)
    struct HpTrack { int seen = 0; float shown = 0, from = 0; bool poison = false; };
    struct Popup { int worm, amount; float age, punch; bool poison, live; };  // big damage counter; age runs once its count ends
    std::vector<HpTrack> hpt;
    std::vector<Popup> popups;
    std::vector<int> order;  // worms with a count to play, in first-hit order
    float wait = 0, tickGap = 0;
    uint32_t hpTick = 0;
    int hpClock = 0;
    bool trackHp(const Game &g, bool turnStart, uint32_t tick);  // true while the camera is on a worm or a crate
};
void hudEvent(const Game &g, const GameEvent &e);  // per sim event: W4M commentary banners (deaths, crates)
// + menu in a match. Local play stops stepping the sim while open; online it is only an overlay.
struct Pause {
    enum Action { None, Quit };
    bool open = false, help = false;
    int row = 0;
    Action update();
    void draw(bool online) const;
};
// Replays list (inside Begin/EndDrawing): picked file index, -1 none yet, -2 back. Y toggles `instant` (instant replay).
int replayList(const std::vector<std::string> &files, int &sel, bool &instant);
void playbackBar(bool paused, int speed, bool freeCam, float sec, float total, const char *note);  // match playback overlay
void replayBadge();  // instant replay overlay
// Network lobby (inside Begin/EndDrawing): LAN games heard, or the room's consoles (one team each) and the host's CPU teams.
void lanGames(const std::vector<LanGame> &games, int sel, const std::string &status);
void room(const Net &net, const GameConfig &opt, bool lan, const std::string &status);
// Single player (inside Begin/EndDrawing): Missions / Challenges list, then the briefing. Mission index to start, -1 none yet, -2 back.
struct MissionMenu { int tab = 0, sel[2] = {}; bool brief = false; };
int missionMenu(MissionMenu &st, const std::vector<MissionSpec> &list, const Progress &p);
void missionHud(const Game &g, const MissionSpec &m);  // objectives and clock during a mission
// Mission over (drawn over the match): 0 nothing yet, 1 next mission, 2 retry, 3 back to the list
int missionEnd(const Game &g, const MissionSpec &m, const Progress::Entry &best, bool hasNext);
}  // namespace Ui

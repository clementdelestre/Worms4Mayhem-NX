#pragma once
#include "raylib.h"
#include "sim.h"
#include <string>
#include <vector>

// W4M-styled frontend and HUD. Art: assets/ui/*.png from tools/w4m-ui; missing files fall back to plain shapes.
namespace Ui {
extern const Color TEAM_COLORS[4];
void load();
void unload();
void text(const char *t, float x, float y, float size, Color c, int align = 0);  // align: 0 left, 1 centre, 2 right
void background();
std::string teamName(const GameConfig &c, int team);
const char *wormName(int team, int i);
// pad -1 = any pad. D-pad buttons also fire on left-stick flicks and auto-repeat while held.
bool pressed(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys);
void pollStick();  // once per frame, before any pressed()
// Bottom button-hint bar. pad: "A" "B" "X" "Y" "+" "-" "L" "R" "ZL"...; key: shown on desktop without a controller.
struct Hint { const char *pad, *key, *label; };
void hints(std::initializer_list<Hint> h);
void controls();  // full-screen controls help page

// Title, main menu, match setup, options. frame() updates and draws (inside Begin/EndDrawing).
struct Frontend {
    enum Screen { Title, Main, Setup, Options, Controls, SchemeEdit } screen = Title;
    enum Action { None, StartLocal, StartOnline, Quit };
    const char *capture = nullptr;  // screenshot path for the next frame (--ui)
    Action frame(GameConfig &cfg, const std::vector<std::string> &maps, std::string &host, int &port, std::string &name);

private:
    int row = 0, mainRow = 0, mapSel = 0, hats = 0, schemeRow = 0;
    bool online = false, loaded = false, music = true;
    std::string *editing = nullptr;  // desktop text entry target
    void loadSetup(GameConfig &cfg, const std::vector<std::string> &maps);
    void saveSetup(const GameConfig &cfg) const;
    bool edit(std::string &s, const char *hint);  // true when s changed (Switch: swkbd, desktop: starts inline entry)
};

// In-game HUD and W4M weapon panel (X / Q). The panel selects by holding NEXT_WEAPON until the weapon changes, then
// releasing for at least one tick, until game.weapon matches: the sim and the network only ever see plain inputs.
struct Hud {
    bool open = false, mine = false;  // mine: a human here plays the current turn
    // local: a human here plays the current turn; tick: ticks simulated so far
    void input(const Game &g, Input &in, bool local, int pad, uint32_t tick);
    void draw(const Game &g, const Camera3D &cam, uint32_t tick);
    void select(int weapon) { target = weapon, tries = 0, pressedOn = -1, releasedAt = 0, open = false, swallow = true; }

private:
    int cursor = 0, target = -1, tries = 0;
    bool swallow = false;  // the A that picked a weapon must not fire
    int pressedOn = -1;       // weapon when the current press began, -1 = releasing
    uint32_t releasedAt = 0;  // tick when the release began
};
// + menu in a match. Local play stops stepping the sim while open; online it is only an overlay.
struct Pause {
    enum Action { None, Quit };
    bool open = false, help = false;
    int row = 0;
    Action update();
    void draw(bool online) const;
};
}  // namespace Ui

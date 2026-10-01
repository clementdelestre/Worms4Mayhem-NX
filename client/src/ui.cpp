#ifdef __SWITCH__
#include <switch.h>
#endif
#include "ui.h"
#include "audio.h"
#include "models.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#else
#define DATA_DIR "./"
#endif

namespace Ui {

const Color TEAM_COLORS[4] = {{220, 50, 50, 255}, {50, 110, 230, 255}, {60, 190, 70, 255}, {240, 200, 40, 255}};

namespace {
const int A = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, B = GAMEPAD_BUTTON_RIGHT_FACE_DOWN, X = GAMEPAD_BUTTON_RIGHT_FACE_UP,
          MINUS = GAMEPAD_BUTTON_MIDDLE_LEFT, UP = GAMEPAD_BUTTON_LEFT_FACE_UP, DOWN = GAMEPAD_BUTTON_LEFT_FACE_DOWN, LEFT = GAMEPAD_BUTTON_LEFT_FACE_LEFT,
          RIGHT = GAMEPAD_BUTTON_LEFT_FACE_RIGHT, PLUS = GAMEPAD_BUTTON_MIDDLE_RIGHT;
const int PANEL_COLS = 8;
#ifdef __SWITCH__
const char *APPLET = "-";  // controller applet glyph, Switch only
#else
const char *APPLET = nullptr;
#endif
const Color GOLDEN = {255, 210, 60, 255}, PANEL = {16, 52, 84, 230};
const char *RULE_LABELS[] = {"King", "Highlander", "Vampire", "Karma", "Low gravity", "Rope race", "Sudden death"};
const char *DEFAULT_TEAMS[] = {"Red Rockets", "Blue Bombers", "Green Grubs", "Gold Diggers"};
const char *WORM_NAMES[] = {"Boggy", "Spadge", "Nobby", "Clagnut", "Thumper", "Wiggles", "Squirm", "Noodle",
                            "Gizmo", "Pickles", "Biscuit", "Rumble", "Sprout", "Dumpling", "Chompy", "Fidget"};
const int HEALTH_ROW[4] = {2, 1, 4, 3};  // team colour -> bar of fe/team_health.png (grey, blue, red, orange, green)

Font font;
bool fontLoaded = false;
#ifdef __SWITCH__
bool plOk = false;
#endif
std::map<std::string, Texture2D> cache;

// assets/ui/<name>.png, loaded once; id 0 when missing
Texture2D tex(const std::string &name) {
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    Texture2D t{};
    const char *p = TextFormat(DATA_DIR "assets/ui/%s.png", name.c_str());
    if (FileExists(p)) {
        t = LoadTexture(p);
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    }
    return cache[name] = t;
}

// 9-slice: source margin m px drawn at m * s
bool nine(const char *name, Rectangle d, float m, float s, Color tint = WHITE) {
    Texture2D t = tex(name);
    if (!t.id) return false;
    float W = t.width, H = t.height, k = fminf(m * s, fminf(d.width, d.height) / 2);
    float sx[4] = {0, m, W - m, W}, sy[4] = {0, m, H - m, H};
    float dx[4] = {d.x, d.x + k, d.x + d.width - k, d.x + d.width}, dy[4] = {d.y, d.y + k, d.y + d.height - k, d.y + d.height};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            DrawTexturePro(t, {sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]}, {dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]}, {}, 0, tint);
    return true;
}

void panel(Rectangle d, bool hi) {
    float s = fminf(d.width, d.height) * 0.006f;
    if (nine(hi ? "fe/paperpopup01" : "fe/paperpopup02", d, 48, s * 1.4f, hi ? WHITE : Color{255, 255, 255, 220})) {
        if (hi) nine("fe/buttonbig_highlight", {d.x - 6, d.y - 6, d.width + 12, d.height + 12}, 110, s);
        return;
    }
    DrawRectangleRounded(d, 0.25f, 6, PANEL);
    DrawRectangleRoundedLinesEx(d, 0.25f, 6, hi ? 4 : 2, hi ? GOLDEN : WHITE);
}

void popup(Rectangle d) {
    if (nine("fe/paperpopup01", d, 48, 0.8f)) return;
    DrawRectangleRounded(d, 0.08f, 6, PANEL);
    DrawRectangleRoundedLinesEx(d, 0.08f, 6, 3, BLACK);
}

bool image(const std::string &name, Rectangle d, Color tint = WHITE) {
    Texture2D t = tex(name);
    if (t.id) DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, d, {}, 0, tint);
    return t.id;
}

std::string lower(std::string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// W4M level preview for a map file name (imported maps keep W4M names, bundled ones map to a theme picture)
std::string preview(const std::string &m) {
    static const std::map<std::string, std::string> ALIAS = {{"", "random_camelot"}, {"jurassic", "level_prehistoric"}, {"construction", "level_building"}};
    std::string l = lower(m);
    auto a = ALIAS.find(l);
    if (a != ALIAS.end()) l = a->second;
    for (const char *pre : {"", "multi_", "level_", "story_"})
        if (tex(std::string("levels/") + pre + l).id) return std::string("levels/") + pre + l;
    return "levels/nolevel";
}

// "EscapeFromTreeRex" -> "Escape From Tree Rex", "Alien-w3d" -> "Alien (W3D)"
std::string mapTitle(const std::string &m) {
    if (m.empty()) return "Random island";
    std::string s, b = m;
    bool w3d = b.size() > 4 && lower(b.substr(b.size() - 4)) == "-w3d";
    if (w3d) b.resize(b.size() - 4);
    for (size_t i = 0; i < b.size(); i++) {
        if (i && isupper((unsigned char)b[i]) && islower((unsigned char)b[i - 1])) s += ' ';
        s += b[i] == '_' ? ' ' : b[i];
    }
    s[0] = (char)toupper((unsigned char)s[0]);
    return w3d ? s + " (W3D)" : s;
}

// HUD/Weapons icon for a weapon name ("Holy Hand Grenade" -> weapons/hollyhandgrenade)
std::string weaponIcon(const std::string &n) {
    std::string k;
    for (char c : n) if (isalnum((unsigned char)c)) k += (char)tolower((unsigned char)c);
    static const std::map<std::string, std::string> ALIAS = {
        {"holyhandgrenade", "hollyhandgrenade"}, {"fatkinsstrike", "fatkinstrike"}, {"flood", "raindance"}, {"changeworm", "wormselect"}};
    auto a = ALIAS.find(k);
    if (a != ALIAS.end()) k = a->second;
    return "weapons/" + k;
}

int clampWrap(int v, int n) { return n ? ((v % n) + n) % n : 0; }

// single press of any button/key on any pad
bool P(std::initializer_list<int> b, std::initializer_list<int> k) { return pressed(-1, b, k); }
}  // namespace

static bool dirFire[4][4];  // [pad][up, right, down, left] (raylib LEFT_FACE_* order): press or auto-repeat

void pollStick() {
#ifdef __SWITCH__
    const float up = 1;  // libnx HID sticks report +y for up, GLFW reports -y
#else
    const float up = -1;
#endif
    static bool was[4][4];
    static float wait[4][4];
    const int KEYS[4] = {KEY_UP, KEY_RIGHT, KEY_DOWN, KEY_LEFT};
    float dt = GetFrameTime();
    for (int p = 0; p < 4; p++) {
        float x = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X), y = up * GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
        bool stick[4] = {y > 0.5f, x > 0.5f, y < -0.5f, x < -0.5f};
        for (int d = 0; d < 4; d++) {
            bool h = stick[d] || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP + d) || (p == 0 && IsKeyDown(KEYS[d]));
            float &w = wait[p][d];
            bool fire = h && (!was[p][d] || (w -= dt) <= 0);
            if (fire) w = was[p][d] ? w + 0.1f : 0.35f;  // first repeat after 0.35 s, then every 0.1 s
            was[p][d] = h, dirFire[p][d] = fire;
        }
    }
}

bool pressed(int pad, std::initializer_list<int> buttons, std::initializer_list<int> keys) {
    if (pad < 0) {
        for (int p = 0; p < 4; p++) if (pressed(p, buttons, {})) return true;
    } else for (int b : buttons) {
        bool dir = b >= UP && b <= LEFT;
        if (dir ? pad < 4 && dirFire[pad][b - UP] : IsGamepadButtonPressed(pad, b)) return true;
    }
    for (int k : keys) if (IsKeyPressed(k)) return true;
    return false;
}

void load() {
    std::vector<int> cps;
    for (int c = 32; c < 256; c++) if (c < 127 || c > 160) cps.push_back(c);
#ifdef __SWITCH__
    // system shared font: nicer than raylib's bitmap font and nothing to bundle
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);  // single Joy-Cons are held sideways (one per player)
    PlFontData fd;
    plOk = R_SUCCEEDED(plInitialize(PlServiceType_User));
    if (plOk && R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) {
        font = LoadFontFromMemory(".ttf", (const unsigned char *)fd.address, (int)fd.size, 48, cps.data(), (int)cps.size());
        fontLoaded = font.texture.id != 0;
    }
#else
    for (const char *p : {DATA_DIR "assets/ui/font.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"}) {
        if (!FileExists(p)) continue;
        font = LoadFontEx(p, 48, cps.data(), (int)cps.size());
        fontLoaded = font.texture.id != 0;
        if (fontLoaded) break;
    }
#endif
    if (fontLoaded) SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    else font = GetFontDefault();
}

void unload() {
    for (auto &kv : cache) if (kv.second.id) UnloadTexture(kv.second);
    cache.clear();
    if (fontLoaded) UnloadFont(font);
    fontLoaded = false;
#ifdef __SWITCH__
    if (plOk) plExit();
#endif
}

void text(const char *t, float x, float y, float size, Color c, int align) {
    float sp = fontLoaded ? 0 : size / 10;
    Vector2 m = MeasureTextEx(font, t, size, sp);
    Vector2 p = {roundf(x - m.x * align / 2), roundf(y)};
    float o = fmaxf(1.5f, size / 14);  // W4M text: black outline
    Color k = {0, 0, 0, c.a};
    for (Vector2 d : {Vector2{-o, -o}, {o, -o}, {-o, o}, {o, o}, {0, o * 1.5f}}) DrawTextEx(font, t, Vector2Add(p, d), size, sp, k);
    DrawTextEx(font, t, p, size, sp, c);
}

static float textWidth(const char *t, float size) { return MeasureTextEx(font, t, size, fontLoaded ? 0 : size / 10).x; }

// "Y/R" -> two glyphs. Pad: dark pills (round for one letter), key: light keycaps. Returns the width.
static float glyphs(const char *g, float x, float y, float h, bool key, bool draw = true) {
    float x0 = x, fs = h * 0.62f, sp = fontLoaded ? 0 : fs / 10;
    for (const char *t = g; *t;) {
        const char *e = strchr(t, '/');
        std::string s(t, e ? e - t : strlen(t));
        float w = fmaxf(h, textWidth(s.c_str(), fs) + h * 0.55f);
        if (draw) {
            Rectangle r = {x, y, w, h};
            DrawRectangleRounded(r, key ? 0.3f : 1, 8, key ? Color{235, 235, 235, 255} : Color{45, 45, 45, 255});
            DrawRectangleRoundedLinesEx(r, key ? 0.3f : 1, 8, 2, key ? DARKGRAY : WHITE);
            Vector2 m = MeasureTextEx(font, s.c_str(), fs, sp);
            DrawTextEx(font, s.c_str(), {roundf(x + (w - m.x) / 2), roundf(y + (h - m.y) / 2)}, fs, sp, key ? BLACK : WHITE);
        }
        x += w;
        if (!e) break;
        if (draw) text("/", x + 3, y, h * 0.8f, WHITE);
        x += h * 0.5f, t = e + 1;
    }
    return x - x0;
}

static bool keyGlyphs() {
#ifdef __SWITCH__
    return false;
#else
    return !IsGamepadAvailable(0);
#endif
}

void hints(std::initializer_list<Hint> h) {
    const float G = 24, S = 21;
    bool keys = keyGlyphs();
    auto pick = [&](const Hint &x) { return keys ? x.key : x.pad ? x.pad : x.key; };
    float total = 0;
    for (const Hint &x : h) if (pick(x)) total += glyphs(pick(x), 0, 0, G, pick(x) == x.key, false) + 8 + textWidth(x.label, S) + 28;
    DrawRectangle(0, 686, 1280, 34, {0, 0, 0, 225});
    float x = 640 - (total - 28) / 2;
    for (const Hint &x0 : h) {
        const char *g = pick(x0);
        if (!g) continue;
        x += glyphs(g, x, 691, G, g == x0.key) + 8;
        text(x0.label, x, 691, S, WHITE);
        x += textWidth(x0.label, S) + 28;
    }
}

void controls() {
    struct Row { const char *what, *pad, *key; };
    static const Row GAME[] = {{"Walk / turn", "LS", "Arrows"}, {"Aim", "RS", "W/S"}, {"Aim (single Joy-Con)", "L", nullptr},
                               {"Fire (hold = power)", "A", "Space"}, {"Jump / let go of rope", "B", "Enter"}, {"Weapon panel", "X", "Q"},
                               {"Next weapon", "Y/R", "Tab"}, {"Rotate camera", "RS", "A/D"}, {"Zoom", "ZL/ZR", "Z/X"},
                               {"Performance overlay", "L+R", "F3"}, {"Pause", "+", "Esc"}};
    static const Row MENU[] = {{"Move", "D-pad/LS", "Arrows"}, {"Confirm", "A", "Enter"}, {"Back", "B", "Esc"},
                               {"Start match (setup)", "+", nullptr}, {"Controllers (setup)", "-", nullptr}, {"Quit (title screen)", "+", "Esc"}};
    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 120});
    popup({90, 24, 1100, 652});
    text("CONTROLS", 640, 40, 46, GOLDEN, 1);
    auto table = [](const char *title, const Row *r, int n, float y) {
        text(title, 140, y, 28, GOLDEN);
        for (int i = 0; i < n; i++, r++) {
            float ry = y + 34 + i * 29;
            text(r->what, 160, ry, 22, WHITE);
            glyphs(r->pad, 560, ry, 24, false);
            if (r->key) glyphs(r->key, 800, ry, 24, true);
        }
    };
    table("In a match", GAME, 11, 96);
    table("Menus", MENU, 6, 96 + 34 + 11 * 29 + 8);
    text("hold L + stick up/down", 640, 96 + 34 + 2 * 29 + 2, 18, LIGHTGRAY);
}

void background() {
    Texture2D t = tex("back/loadbackgeneric");
    if (t.id) {
        DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {0, 0, 1280, 720}, {}, 0, WHITE);
        return;
    }
    DrawRectangleGradientV(0, 0, 1280, 720, {40, 80, 150, 255}, {120, 170, 220, 255});
}

static void logo(float cx, float y, float w) {
    Texture2D t = tex("fe/tournament_vsus");
    if (t.id) {
        Rectangle src = {130, 50, 780, 400};
        float h = w * src.height / src.width;
        DrawTexturePro(t, src, {cx - w / 2, y, w, h}, {}, 0, WHITE);
        text("NX", cx + w * 0.41f, y + h * 0.80f, w * 0.09f, GOLDEN, 1);
    } else {
        text("WORMS4NX", cx, y + w * 0.1f, w * 0.15f, GOLDEN, 1);
    }
}

std::string teamName(const GameConfig &c, int team) {
    if (team < (int)c.teamSetup.size() && !c.teamSetup[team].name.empty()) return c.teamSetup[team].name;
    return TextFormat("Team %d", team + 1);
}

const char *wormName(int team, int i) { return WORM_NAMES[(team * 4 + i) % 16]; }

// ---------------------------------------------------------------- frontend

#ifdef __SWITCH__
static bool swkbd(std::string &s, const char *hint) {
    SwkbdConfig kbd;
    char out[32] = "";
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetInitialText(&kbd, s.c_str());
    swkbdConfigSetGuideText(&kbd, hint);
    swkbdConfigSetStringLenMax(&kbd, 20);
    Result rc = swkbdShow(&kbd, out, sizeof out);
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !out[0] || s == out) return false;
    s = out;
    return true;
}

// system controller applet: lets players pair, split or turn single Joy-Cons sideways
static void controllerApplet() {
    HidLaControllerSupportArg arg;
    HidLaControllerSupportResultInfo info;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_max = 4;
    hidLaShowControllerSupport(&info, &arg);
}
#endif

bool Frontend::edit(std::string &s, const char *hint) {
#ifdef __SWITCH__
    return swkbd(s, hint);
#else
    (void)hint;
    editing = &s;
    while (GetCharPressed()) {}
    return false;
#endif
}

// setup.txt: "teams N", "worms N", "map NAME|-", "rules N", "hats N"... then "team <i> <cpu> <hat> <voice bank|-> <name>"
void Frontend::loadSetup(GameConfig &cfg, const std::vector<std::string> &maps) {
    loaded = true;
    hats = Models::hatCount();
    cfg.teamSetup.resize(4);
    for (int t = 0; t < 4; t++) cfg.teamSetup[t] = {DEFAULT_TEAMS[t], 0, (uint8_t)(Audio::voiceBanks() ? t % Audio::voiceBanks() : 0), 0};
    char *txt = LoadFileText(DATA_DIR "setup.txt");
    if (!txt) return;
    for (char *line = strtok(txt, "\n"); line; line = strtok(nullptr, "\n")) {
        char s[64] = "", nm[64] = "";
        int a = 0, b = 0, c = 0, n = 0;
        if (sscanf(line, "teams %d", &a) == 1) cfg.teams = Clamp(a, 2, 4);
        else if (sscanf(line, "worms %d", &a) == 1) cfg.wormsPerTeam = Clamp(a, 1, 4);
        else if (sscanf(line, "rules %d", &a) == 1) cfg.rules = (uint32_t)a;
        else if (sscanf(line, "map %63s", s) == 1) {
            auto it = std::find(maps.begin(), maps.end(), std::string(s) == "-" ? "" : s);
            if (it != maps.end()) mapSel = int(it - maps.begin());
        } else if (sscanf(line, "team %d %d %d %63s %n", &a, &b, &c, s, &n) == 4 && a >= 0 && a < 4) {
            GameConfig::Team &t = cfg.teamSetup[a];
            t.cpu = (uint8_t)Clamp(b, 0, 3);
            t.hat = (uint8_t)Clamp(c, 0, hats);
            for (int v = 0; v < Audio::voiceBanks(); v++) if (!strcmp(Audio::voiceBankName(v), s)) t.voice = (uint8_t)v;
            snprintf(nm, sizeof nm, "%s", line + n);
            if (nm[0]) t.name = nm;
        }
    }
    UnloadFileText(txt);
}

void Frontend::saveSetup(const GameConfig &cfg) const {
    std::string s = TextFormat("teams %d\nworms %d\nrules %u\nmap %s\n", cfg.teams, cfg.wormsPerTeam, cfg.rules, cfg.map.empty() ? "-" : cfg.map.c_str());
    for (int t = 0; t < (int)cfg.teamSetup.size(); t++) {
        const GameConfig::Team &m = cfg.teamSetup[t];
        const char *v = Audio::voiceBankName(m.voice);
        s += TextFormat("team %d %d %d %s %s\n", t, m.cpu, m.hat, v[0] ? v : "-", m.name.c_str());
    }
    SaveFileText(DATA_DIR "setup.txt", s.data());
}

Frontend::Action Frontend::frame(GameConfig &cfg, const std::vector<std::string> &maps, std::string &host, int &port, std::string &name) {
    if (!loaded) loadSetup(cfg, maps);
    if (cfg.teamSetup.size() < 4) cfg.teamSetup.resize(4);
    Action act = None;
    float t = (float)GetTime();
    bool typing = editing != nullptr;
    if (editing) {
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c >= 32 && c < 256 && editing->size() < 20) editing->push_back((char)c);
        if (IsKeyPressed(KEY_BACKSPACE) && !editing->empty()) editing->pop_back();
        if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, A)) editing = nullptr;
    }
    int dy = typing ? 0 : P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP});
    int dx = typing ? 0 : P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT});
    bool ok = !typing && P({A}, {KEY_ENTER, KEY_SPACE}), back = !typing && P({B}, {KEY_BACKSPACE, KEY_ESCAPE});

    BeginDrawing();
    background();
    switch (screen) {
    case Title: {
        logo(640, 90, 760);
        if (fmodf(t, 1.2f) < 0.8f) text(keyGlyphs() ? "Press Enter to start" : "Press A to start", 640, 560, 40, WHITE, 1);
        text("Worms4NX - fan-made homebrew", 640, 648, 20, {230, 230, 230, 200}, 1);
        hints({{"A", "Enter", "Start"}, {"+", "Esc", "Quit"}});
        if (ok) screen = Main;
        else if (P({PLUS}, {KEY_ESCAPE})) act = Quit;
        break;
    }
    case Main: {
        static const char *ITEMS[] = {"Local game", "Online game", "Options"};
        mainRow = clampWrap(mainRow + dy, 3);
        logo(640, 30, 460);
        for (int i = 0; i < 3; i++) {
            Rectangle r = {440, 330 + i * 100.0f, 400, 80};
            panel(r, i == mainRow);
            text(ITEMS[i], 640, r.y + 20, 40, i == mainRow ? GOLDEN : WHITE, 1);
        }
        hints({{"A", "Enter", "Select"}, {"B", "Esc", "Back"}});
        if (back) screen = Title;
        if (ok) {
            if (mainRow == 2) screen = Options, row = 0;
            else screen = Setup, online = mainRow == 1, row = 0;
        }
        break;
    }
    case Options: {
        row = clampWrap(row + dy, 4);
        text("OPTIONS", 640, 60, 60, GOLDEN, 1);
        std::string portS = TextFormat("%d", port);
        const char *labels[] = {"Player name", "Server", "Music", "Controls"};
        const std::string vals[] = {name, host + ":" + portS, music ? "On" : "Off", ">"};
        for (int i = 0; i < 4; i++) {
            Rectangle r = {290, 180 + i * 90.0f, 700, 70};
            panel(r, i == row);
            text(labels[i], r.x + 30, r.y + 18, 32, i == row ? GOLDEN : WHITE);
            bool ed = editing && ((i == 0 && editing == &name) || (i == 1 && editing == &host));
            text(TextFormat("%s%s", vals[i].c_str(), ed && fmodf(t, 1) < 0.5f ? "_" : ""), r.x + r.width - 30, r.y + 18, 32, WHITE, 2);
        }
        if (row == 1 && dx) port = Clamp(port + dx, 1, 65535);
        if (row == 2 && (dx || ok)) music = !music, Audio::music(music);
        if (ok && row == 0) edit(name, "Player name");
        if (ok && row == 1) edit(host, "Server address");
        if (ok && row == 3) screen = Controls;
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else if (row == 1) hints({{"A", "Enter", "Edit address"}, {"D-pad", "Left/Right", "Port"}, {"B", "Esc", "Save & back"}});
        else hints({{"A", "Enter", row == 2 ? "Toggle" : row == 3 ? "Open" : "Edit"}, {"B", "Esc", "Save & back"}});
        if (back) {
            for (char &c : name) if (c == ' ') c = '_';
            SaveFileText(DATA_DIR "server.txt", (char *)TextFormat("%s %d %s\n", host.c_str(), port, name.c_str()));
            screen = Main;
        }
        break;
    }
    case Controls:
        controls();
        hints({{"B", "Esc", "Back"}});
        if (back || ok) screen = Options;
        break;
    case Setup: {
        // focus order: teams count, 4 fields per visible team, worms, map, rules, start
        std::vector<int> ids = {0};
        for (int k = 0; k < cfg.teams; k++) for (int f = 0; f < 4; f++) ids.push_back(100 + k * 4 + f);
        ids.push_back(200), ids.push_back(201);
        for (int r = 0; r < 7; r++) ids.push_back(300 + r);
        ids.push_back(400);
        row = clampWrap(row + dy, (int)ids.size());
        int id = ids[row];
        int nb = Audio::voiceBanks();
        if (id == 0) cfg.teams = Clamp(cfg.teams + dx, 2, 4);
        if (id >= 100 && id < 200) {
            GameConfig::Team &tm = cfg.teamSetup[(id - 100) / 4];
            int k = (id - 100) / 4;
            switch ((id - 100) % 4) {
            case 0: if (ok) edit(tm.name, "Team name"); break;
            case 1: if (dx || ok) tm.cpu = (uint8_t)clampWrap(tm.cpu + (dx ? dx : 1), 4); break;
            case 2:
                if ((dx || ok) && nb) {
                    tm.voice = (uint8_t)clampWrap(tm.voice + (dx ? dx : 0), nb);
                    Audio::setTeamVoice(k, tm.voice);
                    Audio::voice(k, Audio::Voice::Idle);
                }
                break;
            case 3: if (dx && hats) tm.hat = (uint8_t)clampWrap(tm.hat + dx, hats + 1); break;
            }
        }
        if (id == 200) cfg.wormsPerTeam = Clamp(cfg.wormsPerTeam + dx, 1, 4);
        if (id == 201) mapSel = clampWrap(mapSel + dx, (int)maps.size());
        if (id >= 300 && id < 400 && (dx || ok)) cfg.rules ^= 1u << (id - 300);
        cfg.map = maps.empty() ? "" : maps[std::min(mapSel, (int)maps.size() - 1)];

        text(online ? "ONLINE MATCH" : "LOCAL MATCH", 640, 18, 48, GOLDEN, 1);
        auto value = [&](int vid, Rectangle r, const char *label, const std::string &v, float size) {
            bool hi = ids[row] == vid;
            if (hi) DrawRectangleRounded(r, 0.4f, 6, {255, 210, 60, 70});
            text(label, r.x + 10, r.y + (r.height - size) / 2, size, hi ? GOLDEN : WHITE);
            text(TextFormat(hi ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + (r.height - size) / 2, size, WHITE, 2);
        };
        value(0, {40, 86, 600, 40}, "Teams", TextFormat("%d", cfg.teams), 28);
        static const char *CTRL[] = {"Human", "CPU 1", "CPU 2", "CPU 3"};
        for (int k = 0; k < cfg.teams; k++) {
            GameConfig::Team &tm = cfg.teamSetup[k];
            Rectangle card = {40, 134 + k * 134.0f, 600, 126};
            panel(card, ids[row] >= 100 + k * 4 && ids[row] < 104 + k * 4);
            DrawRectangleRounded({card.x + 16, card.y + 16, 12, card.height - 32}, 1, 4, TEAM_COLORS[k]);
            bool hiName = ids[row] == 100 + k * 4, ed = editing == &tm.name;
            if (hiName) DrawRectangleRounded({card.x + 36, card.y + 10, 540, 38}, 0.4f, 6, {255, 210, 60, 70});
            text(TextFormat("%s%s", tm.name.c_str(), ed && fmodf(t, 1) < 0.5f ? "_" : ""), card.x + 44, card.y + 12, 32, TEAM_COLORS[k]);
            if (!online && !tm.cpu) text(!k || IsGamepadAvailable(k) ? TextFormat("Controller %d", k + 1) : "Controller 1 (shared)", card.x + card.width - 24, card.y + 18, 20, LIGHTGRAY, 2);
            const char *hat = !hats ? "-" : tm.hat ? Models::hatName(tm.hat - 1) : "None";
            value(101 + k * 4, {card.x + 36, card.y + 50, 540, 24}, "Player", CTRL[tm.cpu], 22);
            value(102 + k * 4, {card.x + 36, card.y + 74, 540, 24}, "Voice", nb ? Audio::voiceBankName(tm.voice) : "-", 22);
            value(103 + k * 4, {card.x + 36, card.y + 98, 540, 24}, "Hat", hat, 22);
        }
        Rectangle pv = {680, 86, 230, 230};
        panel({pv.x - 8, pv.y - 8, pv.width + 16, pv.height + 16}, ids[row] == 201);
        if (!image(preview(cfg.map), pv)) DrawRectangleRec(pv, {30, 60, 40, 255});
        value(201, {920, 96, 330, 40}, "Map", "", 28);
        text(mapTitle(cfg.map).c_str(), 930, 140, 28, WHITE);
        text(TextFormat("%d / %d", mapSel + 1, (int)maps.size()), 930, 176, 20, LIGHTGRAY);
        value(200, {920, 230, 330, 40}, "Worms", TextFormat("%d", cfg.wormsPerTeam), 28);
        text(online ? "Teams = players in the room" : "One controller per team, or share one", 930, 280, 18, LIGHTGRAY);
        popup({672, 330, 584, 262});
        for (int r = 0; r < 7; r++)
            value(300 + r, {690, 344 + r * 34.0f, 548, 32}, RULE_LABELS[r], cfg.rules & (1u << r) ? "ON" : "off", 24);
        Rectangle go = {860, 608, 390, 70};
        panel(go, ids[row] == 400);
        text(online ? "GO ONLINE" : "START", go.x + go.width / 2, go.y + 16, 40, ids[row] == 400 ? GOLDEN : WHITE, 1);
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else if (online) hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", "Edit/toggle"}, {"+", nullptr, "Go online"}, {"B", "Esc", "Back"}});
        else hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", "Edit/toggle"}, {"+", nullptr, "Start"}, {APPLET, nullptr, "Controllers"}, {"B", "Esc", "Back"}});
#ifdef __SWITCH__
        if (!online && P({MINUS}, {})) controllerApplet();
#endif
        if (back) saveSetup(cfg), screen = Main;
        if ((ok && id == 400) || (!typing && P({PLUS}, {}))) {
            saveSetup(cfg);
            act = online ? StartOnline : StartLocal;
        }
        break;
    }
    }
    if (capture) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, capture);
        UnloadImage(img);
        capture = nullptr;
    }
    EndDrawing();
    if (act != None) {
        for (int k = 0; k < 4; k++) Audio::setTeamVoice(k, cfg.teamSetup[k].voice);
        cfg.teamSetup.resize(cfg.teams);
        loaded = false;  // reload (and re-pad to 4 teams) next time the menu shows
    }
    return act;
}

// ---------------------------------------------------------------- HUD

void Hud::input(const Game &g, Input &in, bool local, int pad, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    if (cur.team < (int)g.cfg.teamSetup.size() && g.cfg.teamSetup[cur.team].cpu) local = false;
    mine = local;
    // swallow: a button still held from a menu or another turn must not fire or jump
    if (!local || g.phase != Phase::Aim) { open = false, target = -1, swallow = true; return; }
    int n = (int)WEAPONS.size(), cols = PANEL_COLS;
    if (pressed(pad, {X}, {KEY_Q})) open = !open, cursor = g.weapon;
    if (open) {
        int dx = pressed(pad, {RIGHT}, {KEY_RIGHT}) - pressed(pad, {LEFT}, {KEY_LEFT});
        int dy = pressed(pad, {DOWN}, {KEY_DOWN}) - pressed(pad, {UP}, {KEY_UP});
        cursor = clampWrap(cursor + dx + dy * cols, n);
        if (pressed(pad, {A}, {KEY_SPACE, KEY_ENTER}) && g.ammo[cur.team][cursor]) select(cursor);
        if (pressed(pad, {B}, {KEY_BACKSPACE})) open = false, swallow = true;
        in = Input{};
    }
    if (swallow) {
        if (!(in.buttons & (Input::FIRE | Input::JUMP))) swallow = false;
        in.buttons &= ~(Input::FIRE | Input::JUMP);
    }
    if (target >= 0) {
        if (pressedOn >= 0 && g.weapon != pressedOn) pressedOn = -1, releasedAt = tick;
        if (g.weapon == target || ++tries > 8 * n) target = -1;
        else if (pressedOn < 0 && tick > releasedAt) pressedOn = g.weapon;
        in.buttons &= ~Input::NEXT_WEAPON;
        if (target >= 0 && pressedOn >= 0) in.buttons |= Input::NEXT_WEAPON;  // edge-triggered in the sim
    }
}

static void healthBar(int team, float x, float y, float w, float h, float frac) {
    Texture2D t = tex("fe/team_health");
    DrawRectangleRounded({x - 2, y - 2, w + 4, h + 4}, 0.5f, 4, {0, 0, 0, 160});
    if (t.id) {
        float row = 4 + HEALTH_ROW[team % 4] * 102.0f, sw = 489 * frac;
        DrawTexturePro(t, {11, row, sw, 97}, {x, y, w * frac, h}, {}, 0, WHITE);
    } else {
        DrawRectangleRounded({x, y, w * frac, h}, 0.5f, 4, TEAM_COLORS[team % 4]);
    }
}

void Hud::draw(const Game &g, const Camera3D &cam, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    // names + health above worms
    for (const Worm &w : g.worms) {
        if (!w.alive) continue;
        Vector3 top = Vector3Add(w.pos, {0, 1.1f, 0});
        Vector3 rel = Vector3Subtract(top, cam.position);
        float dist = Vector3DotProduct(rel, fwd);
        if (dist < 0.5f) continue;
        Vector2 sp = GetWorldToScreen(top, cam);
        float s = Clamp(160 / dist, 11, 22);
        int i = int(&w - g.worms.data()), k = i % std::max(1, g.perTeam);
        Color c = TEAM_COLORS[w.team % 4];
        const char *hp = TextFormat("%d", w.hp > 0 ? w.hp : 0);
        float bw = textWidth(hp, s) + s;
        Rectangle box = {sp.x - bw / 2, sp.y - s * 1.3f, bw, s * 1.2f};
        DrawRectangleRounded(box, 0.4f, 4, {0, 0, 0, 170});
        DrawRectangleRoundedLinesEx(box, 0.4f, 4, 2, c);
        text(hp, sp.x, box.y + s * 0.1f, s, WHITE, 1);
        text(wormName(w.team, k), sp.x, box.y - s * 1.1f, s, c, 1);
        if (&w == &cur) text(teamName(g.cfg, w.team).c_str(), sp.x, box.y - s * 2.0f, s * 0.75f, c, 1);
    }
    if (g.phase == Phase::GameOver) {
        if (g.winner >= 0) text(TextFormat("%s WINS!", teamName(g.cfg, g.winner).c_str()), 640, 260, 70, TEAM_COLORS[g.winner % 4], 1);
        else text("DRAW!", 640, 260, 70, WHITE, 1);
        hints({{"A", "Space", "Continue"}, {"+", "Esc", "Pause"}});
        return;
    }
    const WeaponDef &wd = WEAPONS[g.weapon];
    Color tc = TEAM_COLORS[cur.team % 4];
    // turn banner
    text(TextFormat("%s - %s", teamName(g.cfg, cur.team).c_str(), wormName(cur.team, g.current % std::max(1, g.perTeam))), 640, 14, 28, tc, 1);
    if (g.cfg.rules & RULE_ROPE_RACE) text(TextFormat("Race %ds", tick / 60), 1260, 14, 26, GOLDEN, 2);
    rlPushMatrix();
    rlTranslatef(0, -30, 0);  // bottom HUD sits above the hint bar
    // turn timer (bottom left)
    int secs = g.phase == Phase::Aim || g.phase == Phase::Retreat ? g.timer / 60 : 0;
    Rectangle clockR = {20, 600, 100, 100};
    if (!image(secs <= 5 && g.phase == Phase::Aim ? "fe/buttonsmall_highlight" : "fe/buttonsmall_normal", clockR)) DrawCircle(70, 650, 48, PANEL);
    text(TextFormat("%d", secs), 70, 626, 46, WHITE, 1);
    // team health (bottom centre)
    int maxHp = 100 * std::max(1, g.perTeam);
    for (int t = 0; t < g.teams; t++) {
        int hp = 0;
        for (const Worm &w : g.worms) if (w.team == t && w.alive) hp += w.hp > 0 ? w.hp : 0;
        float y = 690 - (g.teams - 1 - t) * 26.0f;
        text(teamName(g.cfg, t).c_str(), 600, y - 2, 20, TEAM_COLORS[t % 4], 2);
        healthBar(t, 612, y, 260, 18, Clamp((float)hp / maxHp, 0, 1));
    }
    // wind (bottom right)
    Rectangle wr = {1030, 664, 230, 36};
    DrawRectangleRounded(wr, 0.5f, 6, {0, 0, 0, 150});
    DrawRectangleRoundedLinesEx(wr, 0.5f, 6, 2, WHITE);
    float wind = Clamp(g.wind, -1, 1);
    DrawRectangleRounded({wind > 0 ? 1145.0f : 1145 + wind * 105, 672, fabsf(wind) * 105, 20}, 0.5f, 4, wind > 0 ? SKYBLUE : PINK);
    Texture2D arrow = tex("fe/mouse");
    for (int a = 0; a < (int)roundf(fabsf(wind) * 4); a++) {
        float x = wind > 0 ? 1150 + a * 26.0f : 1140 - a * 26.0f - 26;
        if (arrow.id) DrawTexturePro(arrow, {0, 0, wind > 0 ? -(float)arrow.width : (float)arrow.width, (float)arrow.height}, {x, 668, 26, 26}, {}, 0, WHITE);
        else text(wind > 0 ? ">" : "<", x + 13, 668, 24, WHITE, 1);
    }
    DrawLine(1145, 668, 1145, 696, WHITE);
    text("WIND", 1145, 640, 20, WHITE, 1);
    // current weapon (above wind)
    int ammo = g.ammo[cur.team][g.weapon];
    Rectangle ib = {1030, 560, 64, 64};
    if (!image(weaponIcon(wd.name), ib)) panel(ib, false);
    text(wd.name.c_str(), 1102, 568, 24, ammo ? WHITE : GRAY);
    text(ammo < 0 ? "Infinite" : TextFormat("x%d", ammo), 1102, 596, 20, LIGHTGRAY);
    // state hints and power
    if (g.shotsLeft) text(TextFormat("%d shot(s) left", g.shotsLeft), 140, 610, 22, WHITE);
    if (g.roped) text("Rope: stick swings, aim = length, jump releases", 140, 610, 22, WHITE);
    if (g.jetting) {
        text("Jetpack: hold fire to thrust, jump to stop", 140, 610, 22, WHITE);
        healthBar(1, 140, 650, 300, 18, Clamp(g.fuel / fmaxf(wd.fuse, 0.01f), 0, 1));
    }
    if (g.power > 0) {
        DrawRectangleRounded({138, 648, 304, 22}, 0.5f, 4, {0, 0, 0, 160});
        DrawRectangleRounded({140, 650, 300 * g.power, 18}, 0.5f, 4, ColorLerp(YELLOW, RED, g.power));
    }
    rlPopMatrix();
    if (open) hints({{"D-pad", "Up/Down/Left/Right", "Move"}, {"A", "Enter", "Select"}, {"B/X", "Backspace/Q", "Close"}});
    else if (mine && g.phase == Phase::Aim)
        hints({{"A", "Space", "Fire (hold)"}, {"B", "Enter", "Jump"}, {"X", "Q", "Weapons"}, {"Y/R", "Tab", "Next"}, {"+", "Esc", "Pause"}});
    else hints({{"+", "Esc", "Pause"}});
    if (!open) return;

    // weapon panel
    int n = (int)WEAPONS.size(), cols = PANEL_COLS, rows = (n + cols - 1) / cols;
    float cell = 92, pw = cols * cell + 60, ph = rows * cell + 150;
    Rectangle pr = {640 - pw / 2, 360 - ph / 2, pw, ph};
    popup(pr);
    text("WEAPONS", 640, pr.y + 24, 40, GOLDEN, 1);
    for (int i = 0; i < n; i++) {
        Rectangle c = {pr.x + 30 + (i % cols) * cell, pr.y + 80 + (i / cols) * cell, cell - 8, cell - 8};
        int a = g.ammo[cur.team][i];
        if (i == cursor && !nine("fe/buttonbig_highlight", c, 64, 0.25f)) DrawRectangleRoundedLinesEx(c, 0.2f, 4, 4, GOLDEN);
        Rectangle ic = {c.x + 8, c.y + 8, c.width - 16, c.height - 16};
        if (!image(weaponIcon(WEAPONS[i].name), ic, a ? WHITE : Fade(GRAY, 0.5f))) {
            DrawRectangleRounded(ic, 0.2f, 4, a ? PANEL : Fade(PANEL, 0.4f));
            text(WEAPONS[i].name.substr(0, 4).c_str(), ic.x + ic.width / 2, ic.y + ic.height / 2 - 10, 20, a ? WHITE : GRAY, 1);
        }
        if (a > 0) text(TextFormat("%d", a), c.x + c.width - 6, c.y + c.height - 26, 22, WHITE, 2);
    }
    const WeaponDef &sel = WEAPONS[cursor];
    int sa = g.ammo[cur.team][cursor];
    text(TextFormat("%s  %s", sel.name.c_str(), sa < 0 ? "(infinite)" : TextFormat("x%d", sa)), 640, pr.y + ph - 58, 30, sa ? WHITE : GRAY, 1);
}

// ---------------------------------------------------------------- pause

Pause::Action Pause::update() {
    bool plus = P({PLUS}, {KEY_ESCAPE, KEY_P});
    if (!open) {
        if (plus) open = true, help = false, row = 0;
        return None;
    }
    bool ok = P({A}, {KEY_ENTER, KEY_SPACE}), back = plus || P({B}, {KEY_BACKSPACE});
    if (help) {
        if (ok || back) help = false;
        return None;
    }
    row = clampWrap(row + P({DOWN}, {}) - P({UP}, {}), 3);
    if (back || (ok && row == 0)) open = false;
    if (ok && row == 1) help = true;
    if (ok && row == 2) { open = false; return Quit; }
    return None;
}

void Pause::draw(bool online) const {
    if (!open) return;
    if (help) {
        controls();
        hints({{"B", "Esc", "Back"}});
        return;
    }
    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 140});
    popup({440, 170, 400, 380});
    text("PAUSED", 640, 190, 50, GOLDEN, 1);
    const char *items[] = {"Resume", "Controls", online ? "Leave match" : "Quit to menu"};
    for (int i = 0; i < 3; i++) {
        Rectangle r = {490, 270 + i * 86.0f, 300, 68};
        panel(r, i == row);
        text(items[i], 640, r.y + 16, 34, i == row ? GOLDEN : WHITE, 1);
    }
    if (online) text("The match keeps running", 640, 520, 20, LIGHTGRAY, 1);
    hints({{"A", "Enter", "Select"}, {"B/+", "Esc", "Resume"}});
}

}  // namespace Ui

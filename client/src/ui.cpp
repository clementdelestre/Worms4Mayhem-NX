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
const int MAX_CUSTOM = 8;  // Weapon Factory slots
#ifdef __SWITCH__
const char *APPLET = "-";  // controller applet glyph, Switch only
#else
const char *APPLET = nullptr;
#endif
const Color GOLDEN = {255, 210, 60, 255}, PANEL = {16, 52, 84, 230};
const char *RULE_LABELS[] = {"King", "Highlander", "Vampire", "Karma", "Low gravity", "Rope race", "Sudden death"};
// Scheme edit page: one row per Scheme byte, in struct order. names: enum labels (min = 0).
struct SchemeField { const char *label, *fmt; int min, max, step; const char *names[7]; };
const SchemeField SCHEME_FIELDS[] = {
    {"Turn time", "%d s", 5, 90, 5, {}}, {"Retreat time", "%d s", 0, 10, 1, {}}, {"Hot seat time", "%d s", 0, 10, 1, {}},
    {"Round time", "%d min", 5, 60, 5, {}}, {"Worm energy", "%d", 25, 250, 25, {}}, {"Crate drops", "%d%%", 0, 100, 10, {}},
    {"Weapon crates", "%d", 0, 100, 10, {}}, {"Health crates", "%d", 0, 100, 10, {}}, {"Utility crates", "%d", 0, 100, 10, {}},
    {"Health crate", "%d hp", 5, 100, 5, {}}, {"Mines", "%d", 0, 12, 1, {}}, {"Oil drums", "%d", 0, 12, 1, {}},
    {"Mine fuse", nullptr, 0, 6, 1, {"0 s", "1 s", "2 s", "3 s", "4 s", "5 s", "Random"}},
    {"Sudden death", nullptr, 0, 2, 1, {"1 HP + water", "Water rise", "1 HP"}}, {"Fall damage", nullptr, 0, 1, 1, {"Off", "On"}},
    {"Wind", nullptr, 0, 3, 1, {"None", "Low", "Medium", "High"}}, {"Weapons", nullptr, 0, 3, 1, {"Default", "BnG", "Crates only", "Unlimited"}},
};
static_assert(sizeof SCHEME_FIELDS / sizeof *SCHEME_FIELDS == sizeof(Scheme), "one row per Scheme byte");

int presetOf(const Scheme &s) {
    for (size_t i = 0; i < SCHEMES.size(); i++) if (!memcmp(&SCHEMES[i].s, &s, sizeof s)) return (int)i;
    return -1;
}
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
static float minusFor = 0;     // seconds - (desktop F1) has been held
static bool minusTap = false;  // - released this frame before HOLD
const float HOLD = 0.35f;
bool forceHelp = false;
bool helpHeld() { return forceHelp || minusFor >= HOLD; }

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
    bool m = IsKeyDown(KEY_F1);
    for (int p = 0; p < 4; p++) m |= IsGamepadButtonDown(p, MINUS);
    minusTap = !m && minusFor > 0 && minusFor < HOLD;
    minusFor = m ? minusFor + dt : 0;
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
    // DrawTextEx() x6, but its linear glyph lookup and per-glyph batch checks run once per string
    struct G { float ox, oy; int g; };
    static std::vector<G> gs;
    gs.clear();
    float s = size / font.baseSize, pad = (float)font.glyphPadding, ox = 0, oy = 0;
    for (int i = 0, n = 1; t[i]; i += n) {
        int cp = GetCodepointNext(t + i, &n), g = GetGlyphIndex(font, cp);
        if (cp == '\n') { oy += size + 2, ox = 0; continue; }  // raylib's default line spacing
        if (cp != ' ' && cp != '\t') gs.push_back({ox, oy, g});
        ox += (font.glyphs[g].advanceX == 0 ? font.recs[g].width * s : font.glyphs[g].advanceX * s) + sp;
    }
    float W = (float)font.texture.width, H = (float)font.texture.height;
    rlSetTexture(font.texture.id);
    rlBegin(RL_QUADS);
    rlNormal3f(0, 0, 1);
    for (Vector2 d : {Vector2{-o, -o}, {o, -o}, {-o, o}, {o, o}, {0, o * 1.5f}, {0, 0}}) {
        Vector2 q = Vector2Add(p, d);
        Color col = d.x == 0 && d.y == 0 ? c : k;
        rlColor4ub(col.r, col.g, col.b, col.a);
        for (const G &e : gs) {
            const Rectangle &r = font.recs[e.g];
            float x = q.x + e.ox + font.glyphs[e.g].offsetX * s - pad * s, y = q.y + e.oy + font.glyphs[e.g].offsetY * s - pad * s;
            float w = (r.width + 2 * pad) * s, h = (r.height + 2 * pad) * s, u0 = (r.x - pad) / W, v0 = (r.y - pad) / H;
            float u1 = (r.x - pad + (r.width + 2 * pad)) / W, v1 = (r.y - pad + (r.height + 2 * pad)) / H;
            rlTexCoord2f(u0, v0), rlVertex2f(x, y);
            rlTexCoord2f(u0, v1), rlVertex2f(x, y + h);
            rlTexCoord2f(u1, v1), rlVertex2f(x + w, y + h);
            rlTexCoord2f(u1, v0), rlVertex2f(x + w, y);
        }
    }
    rlEnd();
    rlSetTexture(0);
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
    const float G = 22, S = 19;
    bool keys = keyGlyphs();
    auto pick = [&](const Hint &x) { return keys ? x.key : x.pad ? x.pad : x.key; };
    float total = 0;
    for (const Hint &x : h) if (pick(x)) total += glyphs(pick(x), 0, 0, G, pick(x) == x.key, false) + 8 + textWidth(x.label, S) + 28;
    float x = 640 - (total - 28) / 2;
    for (const Hint &x0 : h) {
        const char *g = pick(x0);
        if (!g) continue;
        DrawRectangleRounded({x + 2, 694, glyphs(g, 0, 0, G, g == x0.key, false), G}, 1, 8, {0, 0, 0, 110});  // drop shadow
        x += glyphs(g, x, 692, G, g == x0.key) + 8;
        text(x0.label, x, 693, S, WHITE);
        x += textWidth(x0.label, S) + 28;
    }
}

static void logo(float cx, float y, float w);

// both windings: rlgl culls back faces
static void tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col), DrawTriangle(a, c, b, col); }

// Generic twin-stick controller drawn with shapes, each control's callout arrow pointing at it.
void controls(bool game) {
    enum { ZL, L, MIN, LS, DPAD, ZR, R, PLS, BX, BY, BA, BB, RS };
    static const Vector3 PART[] = {{-185, -137, 6}, {-160, -112, 6}, {-45, -70, 14}, {-150, -35, 40}, {-75, 35, 40}, {185, -137, 6},
                                   {160, -112, 6}, {45, -70, 14}, {150, -69, 20}, {116, -35, 20}, {184, -35, 20}, {150, -1, 20}, {75, 35, 40}};  // x, y, radius
    struct Call { int part; float ly; const char *label, *key; };
    static const Call GAME[] = {
        {ZL, 232, "Zoom out", "Z"}, {L, 276, "L + stick: aim\n(single Joy-Con)", nullptr}, {MIN, 356, "Hold: controls", "F1"},
        {LS, 414, "Move / turn", "Arrows"}, {DPAD, 488, "Weapon panel cursor", "Arrows"}, {ZR, 232, "Zoom in", "X"},
        {R, 270, "Next weapon", "Tab"}, {PLS, 306, "Pause", "Esc"}, {BX, 342, "Weapon panel", "Q"}, {BY, 380, "Next weapon", "Tab"},
        {BA, 440, "Fire (hold = power)", "Space"}, {BB, 482, "Jump (twice = backflip)", "Enter"},
        {RS, 530, "Aim (up / down)\nCamera orbit (left / right)", "W/S/A/D"}};
    static const Call MENU[] = {
        {MIN, 330, "Tap: controllers (setup)\nHold: controls", "F1"}, {LS, 414, "Move", "Arrows"}, {DPAD, 488, "Move / change value", "Arrows"},
        {PLS, 300, "Start match (setup)\nQuit (title screen)", "Esc"}, {BA, 430, "Confirm", "Enter"}, {BB, 482, "Back", "Esc"}};
    const float cx = 640, cy = 450;
    const Color BODY = {58, 62, 72, 255}, EDGE = {225, 228, 236, 255}, DARK = {30, 31, 36, 255};
    auto at = [&](int p) { return Vector2{cx + PART[p].x, cy + PART[p].y}; };
    bool keys = keyGlyphs();

    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 225});
    logo(640, 4, 300);
    text(game ? "MATCH CONTROLS" : "MENU CONTROLS", 640, 164, 40, GOLDEN, 1);
    // shoulders, then the body silhouette (outline pass under the fill)
    for (float s : {-1.0f, 1.0f}) {
        Rectangle zl = {cx + s * 180 - 65, cy - 152, 130, 40}, l = {cx + s * 160 - 80, cy - 126, 160, 36};
        DrawRectangleRounded(zl, 0.8f, 8, DARK), DrawRectangleRoundedLinesEx(zl, 0.8f, 8, 2, EDGE);
        DrawRectangleRounded(l, 0.8f, 8, {44, 47, 55, 255}), DrawRectangleRoundedLinesEx(l, 0.8f, 8, 2, EDGE);
        text(s < 0 ? "ZL" : "ZR", zl.x + zl.width / 2, zl.y + 3, 16, LIGHTGRAY, 1);
        text(s < 0 ? "L" : "R", l.x + l.width / 2, l.y + 3, 16, LIGHTGRAY, 1);
    }
    for (int pass = 0; pass < 2; pass++) {
        float g = pass ? 0 : 3;
        Color c = pass ? BODY : EDGE;
        DrawRectangleRounded({cx - 250 - g, cy - 100 - g, 500 + 2 * g, 170 + 2 * g}, 0.55f, 12, c);
        for (float s : {-1.0f, 1.0f}) DrawCircleV({cx + s * 190, cy + 70}, 80 + g, c);
    }
    DrawRectangleRounded({cx - 236, cy - 92, 472, 44}, 0.9f, 8, {255, 255, 255, 14});
    // callout lines run under the buttons, arrow heads stop at their edge
    const Call *calls = game ? GAME : MENU;
    int n = game ? sizeof GAME / sizeof *GAME : sizeof MENU / sizeof *MENU;
    for (int i = 0; i < n; i++) {
        Vector2 a = at(calls[i].part), e = {a.x < cx ? 368.0f : 912.0f, calls[i].ly + 11}, via = e;
        if (calls[i].part == BY) via = {cx + 218, cy - 69}, DrawLineEx(via, e, 2, GOLDEN);  // out between X and A
        Vector2 d = Vector2Normalize(Vector2Subtract(via, a)), tip = Vector2Add(a, Vector2Scale(d, PART[calls[i].part].z + 2)), q = {-d.y, d.x};
        DrawLineEx(tip, via, 2, GOLDEN);
        tri(tip, Vector2Add(Vector2Add(tip, Vector2Scale(d, 12)), Vector2Scale(q, 6)), Vector2Add(Vector2Add(tip, Vector2Scale(d, 12)), Vector2Scale(q, -6)), GOLDEN);
        DrawCircleV(e, 3, GOLDEN);
    }
    for (int p : {LS, RS}) {
        Vector2 c = at(p);
        DrawCircleV(c, 38, DARK), DrawCircleV(c, 28, {78, 82, 94, 255}), DrawCircleLinesV(c, 28, EDGE), DrawCircleV(c, 17, {64, 68, 78, 255});
    }
    Vector2 dp = at(DPAD);
    DrawRectangleRounded({dp.x - 13, dp.y - 38, 26, 76}, 0.3f, 4, DARK), DrawRectangleRounded({dp.x - 38, dp.y - 13, 76, 26}, 0.3f, 4, DARK);
    for (int k = 0; k < 4; k++) {
        Vector2 u = {cosf(k * PI / 2), sinf(k * PI / 2)}, v = {-u.y, u.x}, t = Vector2Add(dp, Vector2Scale(u, 33));
        tri(t, Vector2Add(Vector2Add(dp, Vector2Scale(u, 22)), Vector2Scale(v, 7)), Vector2Add(Vector2Add(dp, Vector2Scale(u, 22)), Vector2Scale(v, -7)), EDGE);
    }
    const char *FACE[] = {"X", "Y", "A", "B"};
    for (int k = 0; k < 4; k++) {
        Vector2 c = at(BX + k);
        DrawCircleV(c, 19, DARK), DrawCircleLinesV(c, 19, EDGE);
        text(FACE[k], c.x, c.y - 11, 22, WHITE, 1);
    }
    Vector2 mi = at(MIN), pl = at(PLS);
    DrawCircleV(mi, 12, DARK), DrawCircleV(pl, 12, DARK);
    DrawRectangle(mi.x - 6, mi.y - 1.5f, 12, 3, EDGE), DrawRectangle(pl.x - 6, pl.y - 1.5f, 12, 3, EDGE), DrawRectangle(pl.x - 1.5f, pl.y - 6, 3, 12, EDGE);
    // labels: right-aligned on the left of the pad, left-aligned on the right; keycaps on desktop
    for (int i = 0; i < n; i++) {
        bool left = at(calls[i].part).x < cx;
        std::string lines = calls[i].label;
        float y = calls[i].ly, x = left ? 355.0f : 925.0f;
        for (size_t s = 0, e; s <= lines.size(); s = e + 1, y += 25) {
            e = lines.find('\n', s);
            if (e == std::string::npos) e = lines.size();
            std::string ln = lines.substr(s, e - s);
            text(ln.c_str(), x, y, 22, WHITE, left ? 2 : 0);
            if (keys && calls[i].key && y == calls[i].ly) {
                float w = textWidth(ln.c_str(), 22) + 10;
                glyphs(calls[i].key, left ? x - w - glyphs(calls[i].key, 0, 0, 24, true, false) : x + w, y - 1, 24, true);
            }
        }
    }
    if (game) text(keys ? "L + R  (F3): performance overlay" : "L + R: performance overlay", 640, 640, 22, LIGHTGRAY, 1);
    else text("Each screen lists its other buttons at the bottom", 640, 640, 22, LIGHTGRAY, 1);
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

// system controller applet: lets players pair, split or turn single Joy-Cons sideways.
// players: controllers the match needs; team k always lands on pad k afterwards (padInitialize No1..No4 order).
static void controllerApplet(int players) {
    HidLaControllerSupportArg arg;
    HidLaControllerSupportResultInfo info;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = arg.hdr.player_count_max = (s8)Clamp(players, 1, 4);
    arg.hdr.enable_single_mode = players == 1;  // lets 1 player stay in handheld mode
    arg.hdr.enable_identification_color = 1;
    for (int i = 0; i < 4; i++) arg.identification_color[i] = {TEAM_COLORS[i].r, TEAM_COLORS[i].g, TEAM_COLORS[i].b, TEAM_COLORS[i].a};
    hidLaShowControllerSupport(&info, &arg);
}

// human (non-CPU) teams: the applet's player count, each later mapped to the same-index pad
static int humanTeams(const GameConfig &cfg) {
    int n = 0;
    for (int k = 0; k < cfg.teams; k++) if (!cfg.teamSetup[k].cpu) n++;
    return n;
}
static int connectedPads() {
    int n = 0;
    for (int i = 0; i < 4; i++) if (IsGamepadAvailable(i)) n++;
    return n;
}
// short device label for the setup card ("Pro Controller", "Joy-Con L"...)
static const char *padStyle(int pad) {
    const char *n = GetGamepadName(pad);
    if (strstr(n, "Pro")) return "Pro Controller";
    if (strstr(n, "Handheld")) return "Handheld";
    if (strstr(n, "Dual")) return "Dual Joy-Con";
    if (strstr(n, "left")) return "Joy-Con L";
    if (strstr(n, "right")) return "Joy-Con R";
    return "Controller";
}
#endif

// Network setup: card 0 is this console's player, the other cards are the host's CPU teams.
static void netTeams(GameConfig &cfg, int dx) {
    cfg.teamSetup[0].cpu = 0;
    for (size_t k = 1; k < cfg.teamSetup.size(); k++)
        if (!cfg.teamSetup[k].cpu) cfg.teamSetup[k].cpu = dx < 0 ? 3 : 1;
}

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

// setup.txt: "teams N", "worms N", "map NAME|-", "rules N", "scheme <Scheme bytes>"... then "team <i> <cpu> <hat> <voice bank|-> <name>"
void Frontend::loadSetup(GameConfig &cfg, const std::vector<std::string> &maps) {
    loaded = true;
    hats = Models::hatCount();
    cfg.teamSetup.resize(4);
    for (int t = 0; t < 4; t++) cfg.teamSetup[t] = {DEFAULT_TEAMS[t], 0, (uint8_t)(Audio::voiceBanks() ? t % Audio::voiceBanks() : 0), 0};
    if (!loadCustomWeapons(DATA_DIR "custom_weapons.json", customs)) customs.clear();
    if (customs.size() > (size_t)MAX_CUSTOM) customs.resize(MAX_CUSTOM);
    bool netFile = online && FileExists(DATA_DIR "setup_net.txt");
    if (online && !netFile) cfg.teams = 1;
    char *txt = LoadFileText(netFile ? DATA_DIR "setup_net.txt" : DATA_DIR "setup.txt");
    if (!txt) return;
    for (char *line = strtok(txt, "\n"); line; line = strtok(nullptr, "\n")) {
        char s[64] = "", nm[64] = "";
        int a = 0, b = 0, c = 0, n = 0;
        if (sscanf(line, "teams %d", &a) == 1) cfg.teams = Clamp(a, online ? 1 : 2, 4);
        else if (sscanf(line, "worms %d", &a) == 1) cfg.wormsPerTeam = Clamp(a, 1, 4);
        else if (sscanf(line, "rules %d", &a) == 1) cfg.rules = (uint32_t)a;
        else if (sscanf(line, "wormpot %d", &a) == 1) cfg.wormpot = (uint32_t)a & ((1u << WORMPOT_REELS[3]) - 1);
        else if (!strncmp(line, "scheme ", 7)) {
            char *p = line + 7;
            for (size_t i = 0; i < sizeof(Scheme); i++) {
                char *e;
                long v = strtol(p, &e, 10);
                if (e == p) break;
                const SchemeField &f = SCHEME_FIELDS[i];
                ((uint8_t *)&cfg.scheme)[i] = (uint8_t)Clamp(v, f.min, f.max);
                p = e;
            }
        }
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
    std::string s = TextFormat("teams %d\nworms %d\nrules %u\nwormpot %u\nmap %s\nscheme", cfg.teams, cfg.wormsPerTeam, cfg.rules, cfg.wormpot,
                               cfg.map.empty() ? "-" : cfg.map.c_str());
    for (size_t i = 0; i < sizeof(Scheme); i++) s += TextFormat(" %d", ((const uint8_t *)&cfg.scheme)[i]);
    s += "\n";
    for (int t = 0; t < (int)cfg.teamSetup.size(); t++) {
        const GameConfig::Team &m = cfg.teamSetup[t];
        const char *v = Audio::voiceBankName(m.voice);
        s += TextFormat("team %d %d %d %s %s\n", t, m.cpu, m.hat, v[0] ? v : "-", m.name.c_str());
    }
    SaveFileText(online ? DATA_DIR "setup_net.txt" : DATA_DIR "setup.txt", s.data());
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
        static const char *ITEMS[] = {"Single player", "Local (same console)", "LAN", "Online (server)", "Replays", "Options"};
        const int n = sizeof ITEMS / sizeof *ITEMS;
        mainRow = clampWrap(mainRow + dy, n);
        logo(640, 24, 420);
        for (int i = 0; i < n; i++) {
            Rectangle r = {420, 250 + i * 70.0f, 440, 60};
            panel(r, i == mainRow);
            text(ITEMS[i], 640, r.y + 13, 34, i == mainRow ? GOLDEN : WHITE, 1);
        }
        hints({{"A", "Enter", "Select"}, {"B", "Esc", "Back"}});
        if (back) screen = Title;
        if (ok) {
            if (mainRow == 0) act = SinglePlayer;
            else if (mainRow == 5) screen = Options, row = 0;
            else if (mainRow == 4) act = Replays;
            else screen = Setup, online = mainRow > 1, lan = mainRow == 2, row = 0, loaded = false;  // reload: net setup has its own file
        }
        break;
    }
    case Options: {
        row = clampWrap(row + dy, 5);
        text("OPTIONS", 640, 60, 60, GOLDEN, 1);
        std::string portS = TextFormat("%d", port);
        const char *labels[] = {"Player name", "Server", "Music", "Controls", "Weapon Factory"};
        const std::string vals[] = {name, host + ":" + portS, music ? "On" : "Off", ">", TextFormat("%d / %d  >", (int)customs.size(), MAX_CUSTOM)};
        for (int i = 0; i < 5; i++) {
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
        if (ok && row == 4) screen = Factory, facSel = 0;
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else if (row == 1) hints({{"A", "Enter", "Edit address"}, {"D-pad", "Left/Right", "Port"}, {"B", "Esc", "Save & back"}});
        else hints({{"A", "Enter", row == 2 ? "Toggle" : row >= 3 ? "Open" : "Edit"}, {"B", "Esc", "Save & back"}});
        if (back) {
            for (char &c : name) if (c == ' ') c = '_';
            SaveFileText(DATA_DIR "server.txt", (char *)TextFormat("%s %d %s\n", host.c_str(), port, name.c_str()));
            screen = Main;
        }
        break;
    }
    case Controls:
        controls(true);
        hints({{"B", "Esc", "Back"}});
        if (back || ok) screen = Options;
        break;
    case Wormpot: wormpot(cfg, dx, dy, ok, back, t); break;
    case Factory: factory(dx, dy, ok, back); break;
    case FactoryEdit: factoryEdit(dx, dy, ok, back, typing, t); break;
    case SchemeEdit: {
        const int n = (int)sizeof(Scheme);
        schemeRow = clampWrap(schemeRow + dy, n + 1);
        int preset = presetOf(cfg.scheme), np = (int)SCHEMES.size();
        if (schemeRow == 0 && dx) cfg.scheme = SCHEMES[clampWrap(preset < 0 ? (dx > 0 ? 0 : np - 1) : preset + dx, np)].s;
        uint8_t *bytes = (uint8_t *)&cfg.scheme;
        if (schemeRow > 0 && dx) {
            const SchemeField &f = SCHEME_FIELDS[schemeRow - 1];
            int v = bytes[schemeRow - 1] + dx * f.step;
            bytes[schemeRow - 1] = (uint8_t)(f.names[0] ? clampWrap(v, f.max + 1) : Clamp(v, f.min, f.max));
        }
        text("GAME SCHEME", 640, 18, 48, GOLDEN, 1);
        popup({300, 80, 680, 590});
        for (int i = 0; i <= n; i++) {
            Rectangle r = {320, 92 + i * 31.0f, 640, 29};
            bool hi = i == schemeRow;
            std::string v = !i ? (preset < 0 ? "Custom" : SCHEMES[preset].name) : "";
            if (i) {
                const SchemeField &f = SCHEME_FIELDS[i - 1];
                v = f.names[0] ? f.names[std::min<int>(bytes[i - 1], f.max)] : TextFormat(f.fmt, bytes[i - 1]);
            }
            if (hi) DrawRectangleRounded(r, 0.4f, 6, {255, 210, 60, 70});
            text(i ? SCHEME_FIELDS[i - 1].label : "Preset", r.x + 10, r.y + 3, 22, hi ? GOLDEN : i ? WHITE : SKYBLUE);
            text(TextFormat(hi ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + 3, 22, WHITE, 2);
        }
        hints({{"D-pad", "Up/Down", "Move"}, {"D-pad", "Left/Right", "Change"}, {"B", "Esc", "Back"}});
        if (back || ok) screen = Setup;
        break;
    }
    case Setup: {
        // focus order: teams count, 4 fields per visible team, worms, map, rules, start
        std::vector<int> ids = {0};
        for (int k = 0; k < cfg.teams; k++) for (int f = 0; f < 4; f++) ids.push_back(100 + k * 4 + f);
        ids.push_back(200), ids.push_back(201), ids.push_back(299);
        for (int r = 0; r < 7; r++) ids.push_back(300 + r);
        ids.push_back(350);
        ids.push_back(400);
        row = clampWrap(row + dy, (int)ids.size());
        int id = ids[row];
        int nb = Audio::voiceBanks();
        if (id == 0) cfg.teams = Clamp(cfg.teams + dx, online ? 1 : 2, 4);
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
        int preset = presetOf(cfg.scheme), np = (int)SCHEMES.size();
        if (id == 299 && dx) cfg.scheme = SCHEMES[clampWrap(preset < 0 ? (dx > 0 ? 0 : np - 1) : preset + dx, np)].s;
        if (id == 299 && ok) screen = SchemeEdit, schemeRow = 0;
        if (id == 350 && ok) screen = Wormpot, reel = 0;
        cfg.map = maps.empty() ? "" : maps[std::min(mapSel, (int)maps.size() - 1)];
        if (online) netTeams(cfg, dx);

        text(online ? lan ? "LAN MATCH" : "ONLINE MATCH" : "LOCAL MATCH", 640, 18, 48, GOLDEN, 1);
        auto value = [&](int vid, Rectangle r, const char *label, const std::string &v, float size) {
            bool hi = ids[row] == vid;
            if (hi) DrawRectangleRounded(r, 0.4f, 6, {255, 210, 60, 70});
            text(label, r.x + 10, r.y + (r.height - size) / 2, size, hi ? GOLDEN : WHITE);
            text(TextFormat(hi ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + (r.height - size) / 2, size, WHITE, 2);
        };
        value(0, {40, 86, 600, 40}, "Teams", online ? TextFormat("You + %d CPU", cfg.teams - 1) : TextFormat("%d", cfg.teams), 28);
        static const char *CTRL[] = {"Human", "CPU 1", "CPU 2", "CPU 3"};
        for (int k = 0; k < cfg.teams; k++) {
            GameConfig::Team &tm = cfg.teamSetup[k];
            Rectangle card = {40, 134 + k * 134.0f, 600, 126};
            panel(card, ids[row] >= 100 + k * 4 && ids[row] < 104 + k * 4);
            DrawRectangleRounded({card.x + 16, card.y + 16, 12, card.height - 32}, 1, 4, TEAM_COLORS[k]);
            bool hiName = ids[row] == 100 + k * 4, ed = editing == &tm.name;
            if (hiName) DrawRectangleRounded({card.x + 36, card.y + 10, 540, 38}, 0.4f, 6, {255, 210, 60, 70});
            text(TextFormat("%s%s", tm.name.c_str(), ed && fmodf(t, 1) < 0.5f ? "_" : ""), card.x + 44, card.y + 12, 32, TEAM_COLORS[k]);
            if (!online && !tm.cpu) {
#ifdef __SWITCH__
                const char *lbl = (!k || IsGamepadAvailable(k)) ? TextFormat("P%d %s", k + 1, padStyle(k)) : "Controller 1 (shared)";
#else
                const char *lbl = (!k || IsGamepadAvailable(k)) ? TextFormat("Controller %d", k + 1) : "Controller 1 (shared)";
#endif
                text(lbl, card.x + card.width - 24, card.y + 18, 20, LIGHTGRAY, 2);
            }
            const char *hat = !hats ? "-" : tm.hat ? Models::hatName(tm.hat - 1) : "None";
            value(101 + k * 4, {card.x + 36, card.y + 50, 540, 24}, "Player", online && !k ? "You (this console)" : CTRL[tm.cpu], 22);
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
        text(online ? "+1 team per console that joins" : "One controller per team, or share one", 930, 280, 18, LIGHTGRAY);
        popup({672, 330, 584, 262});
        value(299, {690, 342, 548, 28}, "Scheme", preset < 0 ? "Custom" : SCHEMES[preset].name, 22);
        for (int r = 0; r < 7; r++)
            value(300 + r, {690, 372 + r * 27.0f, 548, 26}, RULE_LABELS[r], cfg.rules & (1u << r) ? "ON" : "off", 22);
        int pots = 0, pot = 0;
        for (int b = 0; b < WORMPOT_REELS[3]; b++) if (cfg.wormpot & (1u << b)) pots++, pot = b;
        value(350, {690, 372 + 7 * 27.0f, 548, 26}, "Wormpot", !pots ? "None" : pots == 1 ? WORMPOT_MODES[pot].name : TextFormat("%d modes", pots), 22);
        Rectangle go = {860, 608, 390, 70};
        panel(go, ids[row] == 400);
        text(online ? lan ? "FIND GAMES" : "GO ONLINE" : "START", go.x + go.width / 2, go.y + 16, 40, ids[row] == 400 ? GOLDEN : WHITE, 1);
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else if (id == 299) hints({{"D-pad", "Left/Right", "Preset"}, {"A", "Enter", "Edit scheme"}, {"B", "Esc", "Back"}});
        else if (id == 350) hints({{"A", "Enter", "Open Wormpot"}, {"B", "Esc", "Back"}});
        else if (online) hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", "Edit/toggle"}, {"+", nullptr, "Go online"}, {"B", "Esc", "Back"}});
        else hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", "Edit/toggle"}, {"+", nullptr, "Start"}, {APPLET, nullptr, "Tap: controllers"}, {"B", "Esc", "Back"}});
#ifdef __SWITCH__
        if (!online && minusTap) controllerApplet(humanTeams(cfg));
#endif
        if (back) saveSetup(cfg), screen = Main;
        if ((ok && id == 400) || (!typing && P({PLUS}, {}))) {
            saveSetup(cfg);
#ifdef __SWITCH__
            // 2+ human teams, or not enough pads for them yet: let players pair up before the match starts
            if (!online) { int hu = humanTeams(cfg); if (hu >= 2 || connectedPads() < hu) controllerApplet(hu); }
#endif
            act = online ? lan ? StartLan : StartOnline : StartLocal;
        }
        break;
    }
    }
    if (helpHeld()) controls(false);
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
        cfg.custom = customs;
        loaded = false;  // reload (and re-pad to 4 teams) next time the menu shows
    }
    return act;
}

// ---------------------------------------------------------------- wormpot & weapon factory

namespace {
int reelSize(int r) { return WORMPOT_REELS[r + 1] - WORMPOT_REELS[r]; }
int reelMode(uint32_t wp, int r) {  // -1 = empty reel
    for (int b = WORMPOT_REELS[r]; b < WORMPOT_REELS[r + 1]; b++) if (wp & (1u << b)) return b - WORMPOT_REELS[r];
    return -1;
}
void setReel(uint32_t &wp, int r, int m) {
    for (int b = WORMPOT_REELS[r]; b < WORMPOT_REELS[r + 1]; b++) wp &= ~(1u << b);
    if (m >= 0) wp |= 1u << (WORMPOT_REELS[r] + m);
}
const char *modeName(int r, int m) { return m < 0 ? "- Empty Reel -" : WORMPOT_MODES[WORMPOT_REELS[r] + m].name; }

// centred, word-wrapped to width w
void wrapped(const char *s, float cx, float y, float w, float size, Color c) {
    std::string line, word;
    for (const char *p = s;; p++) {
        if (*p && *p != ' ') { word += *p; continue; }
        std::string next = line.empty() ? word : line + " " + word;
        if (textWidth(next.c_str(), size) > w && !line.empty()) text(line.c_str(), cx, y, size, c, 1), y += size + 4, next = word;
        line = next, word.clear();
        if (!*p) break;
    }
    if (!line.empty()) text(line.c_str(), cx, y, size, c, 1);
}

const char *LAUNCH[] = {"Bazooka", "Grenade", "Airstrike", "Homing"};
const char *SHOT_MODELS[] = {"bazooka", "grenade", "cluster", "banana", "holy", "homing", "airstrike", "dynamite", "gas", "arrow", "sheep", "mine", "barrel", "starburst"};
const char *ICONS[] = {"Secret Weapon", "Bazooka", "Grenade", "Cluster Grenade", "Banana Bomb", "Holy Hand Grenade", "Homing Missile", "Airstrike",
                       "Super Airstrike", "Dynamite", "Gas Canister", "Sheep", "Pipe Gun", "Tail Nail", "Mad Cow Strike", "Bubble Trubble"};
int launchOf(const WeaponDef &w) { return w.kind == Kind::Airstrike ? 2 : w.kind == Kind::Homing ? 3 : w.fuse > 0 ? 1 : 0; }
template <size_t N> int indexOf(const char *(&list)[N], const std::string &v) {
    for (size_t i = 0; i < N; i++) if (v == list[i]) return (int)i;
    return 0;
}
}  // namespace

std::string iconOf(const WeaponDef &w) { return weaponIcon(w.icon.empty() ? w.name : w.icon); }

void Frontend::wormpot(GameConfig &cfg, int dx, int dy, bool ok, bool back, float t) {
    bool spinning = false;
    for (int r = 0; r < 3; r++) {
        if (spinEnd[r] > 0 && t >= spinEnd[r]) spinEnd[r] = 0, setReel(cfg.wormpot, r, spinTo[r]), Audio::play(Audio::Sfx::Tick);
        spinning |= spinEnd[r] > 0;
    }
    reel = clampWrap(reel + dx, 3);
    if (dy && !spinning) setReel(cfg.wormpot, reel, clampWrap(reelMode(cfg.wormpot, reel) + 1 + dy, reelSize(reel) + 1) - 1);
    if (!spinning && P({X}, {KEY_S}))
        for (int r = 0; r < 3; r++) spinEnd[r] = t + 1 + 0.6f * r, spinTo[r] = GetRandomValue(-1, reelSize(r) - 1);
    if (!spinning && P({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_R})) cfg.wormpot = 0;

    text("WORMPOT", 640, 18, 48, GOLDEN, 1);
    popup({120, 84, 1040, 500});
    image("fe/icon_wxpot", {140, 96, 96, 96});
    text("Wormpot modifies the game rules to create new ways to play.", 660, 120, 24, WHITE, 1);
    static const char *REELS[] = {"Weapons", "Worms", "Crates & Energy"};
    for (int r = 0; r < 3; r++) {
        Rectangle box = {190 + r * 310.0f, 210, 280, 270};
        text(REELS[r], box.x + box.width / 2, box.y - 36, 28, r == reel ? GOLDEN : WHITE, 1);
        DrawRectangleRounded(box, 0.12f, 6, {20, 24, 36, 255});
        int n = reelSize(r) + 1, cur = reelMode(cfg.wormpot, r) + 1;
        float scroll = 0;
        if (spinEnd[r] > 0) {
            float k = (t + r * 0.37f) * 14;
            cur = (int)k % n, scroll = k - floorf(k);
        }
        BeginScissorMode((int)box.x, (int)box.y + 4, (int)box.width, (int)box.height - 8);
        for (int k = -2; k <= 2; k++) {
            float y = box.y + box.height / 2 - 14 + (k - scroll) * 90;
            text(modeName(r, clampWrap(cur + k, n) - 1), box.x + box.width / 2, y, 24, k ? Fade(LIGHTGRAY, 0.6f) : WHITE, 1);
        }
        EndScissorMode();
        DrawRectangleLinesEx({box.x, box.y + box.height / 2 - 40, box.width, 80}, 3, GOLDEN);
        DrawRectangleRoundedLinesEx(box, 0.12f, 6, r == reel ? 5 : 2, r == reel ? GOLDEN : GRAY);
    }
    int m = reelMode(cfg.wormpot, reel);
    wrapped(m < 0 ? "No wormpot mode selected on this reel." : WORMPOT_MODES[WORMPOT_REELS[reel] + m].help, 640, 500, 900, 24, WHITE);
    text(spinning ? "Spinning..." : "Spin those reels!", 640, 600, 30, GOLDEN, 1);
    hints({{"D-pad", "Left/Right", "Reel"}, {"D-pad", "Up/Down", "Nudge"}, {"X", "S", "Spin"}, {"Y", "R", "Reset"}, {"B", "Esc", "Back"}});
    if ((back || ok) && !spinning) screen = Setup;
}

void Frontend::factory(int dx, int dy, bool ok, bool back) {
    (void)dx;
    int n = (int)customs.size(), slots = std::min(n + 1, MAX_CUSTOM);
    facSel = clampWrap(facSel + dy, slots);
    text("WEAPON FACTORY", 640, 18, 48, GOLDEN, 1);
    popup({290, 84, 700, 590});
    for (int i = 0; i < slots; i++) {
        Rectangle r = {310, 100 + i * 70.0f, 660, 62};
        bool hi = i == facSel;
        if (hi) DrawRectangleRounded(r, 0.3f, 6, {255, 210, 60, 70});
        if (i == n) { text("+ Create a weapon", r.x + r.width / 2, r.y + 16, 28, hi ? GOLDEN : WHITE, 1); continue; }
        const WeaponDef &w = customs[i];
        if (!image(iconOf(w), {r.x + 8, r.y + 3, 56, 56})) DrawRectangleRounded({r.x + 8, r.y + 3, 56, 56}, 0.3f, 4, GRAY);
        text(w.name.c_str(), r.x + 80, r.y + 6, 28, hi ? GOLDEN : WHITE);
        text(TextFormat("%s  -  %.0f dmg, radius %.1f%s", LAUNCH[launchOf(w)], w.kind == Kind::Airstrike ? w.cdamage : w.damage,
                        w.kind == Kind::Airstrike ? w.cradius : w.radius, w.clusters ? TextFormat(", %d x %.0f", w.clusters, w.cdamage) : ""),
             r.x + 80, r.y + 36, 18, LIGHTGRAY);
    }
    hints({{"A", "Enter", facSel == n ? "Create" : "Edit"}, {"Y", "Delete", "Delete"}, {"B", "Esc", "Save & back"}});
    if (ok && facSel == n) {
        WeaponDef w = {TextFormat("Custom %d", n + 1), Kind::Shell, 3, 45, 30, 0, 0, 1.5f, 15, 1, 0, 1, true, 2};
        w.model = "bazooka", w.icon = "Secret Weapon";
        customs.push_back(w);
    }
    if (ok) screen = FactoryEdit, facRow = 0;
    if (facSel < n && P({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_DELETE})) customs.erase(customs.begin() + facSel);
    if (back) {
        if (!saveCustomWeapons(DATA_DIR "custom_weapons.json", customs)) TraceLog(LOG_WARNING, "cannot save custom_weapons.json");
        screen = Options;
    }
}

void Frontend::factoryEdit(int dx, int dy, bool ok, bool back, bool typing, float t) {
    if (customs.empty()) { screen = Factory; return; }
    WeaponDef &w = customs[std::min<int>(facSel, (int)customs.size() - 1)];
    int launch = launchOf(w);
    struct Num { const char *label; float *f; int *i; float lo, hi, step; const char *fmt; };
    const Num nums[] = {
        {"Blast radius", &w.radius, nullptr, 0.5f, 8, 0.5f, "%.1f m"}, {"Damage", &w.damage, nullptr, 0, 100, 5, "%.0f"},
        {"Launch speed", &w.speed, nullptr, 5, 50, 1, "%.0f"}, {"Fuse", &w.fuse, nullptr, 1, 5, 0.5f, "%.1f s"},
        {"Bounce", &w.bounce, nullptr, 0, 1, 0.05f, "%.2f"}, {launch == 2 ? "Missiles" : "Clusters", nullptr, &w.clusters, 0, 10, 1, "%d"},
        {launch == 2 ? "Missile radius" : "Cluster radius", &w.cradius, nullptr, 0.5f, 5, 0.5f, "%.1f m"},
        {launch == 2 ? "Missile damage" : "Cluster damage", &w.cdamage, nullptr, 0, 60, 5, "%.0f"},
        {"Crate weight", nullptr, &w.weight, 0, 10, 1, "%d"}, {"Ammo", nullptr, &w.count, -1, 9, 1, "%d"},
    };
    const int N = 5 + (int)(sizeof nums / sizeof *nums);  // name, launch, model, icon, wind + numbers
    facRow = clampWrap(facRow + dy, N);
    if (dx) switch (facRow) {
    case 1:
        launch = clampWrap(launch + dx, 4);
        w.kind = launch == 2 ? Kind::Airstrike : launch == 3 ? Kind::Homing : Kind::Shell;
        w.fuse = launch == 1 ? fmaxf(w.fuse, 3) : 0;
        if (launch == 2 && !w.clusters) w.clusters = 5;
        break;
    case 2: w.model = SHOT_MODELS[clampWrap(indexOf(SHOT_MODELS, w.model) + dx, (int)(sizeof SHOT_MODELS / sizeof *SHOT_MODELS))]; break;
    case 3: w.icon = ICONS[clampWrap(indexOf(ICONS, w.icon) + dx, (int)(sizeof ICONS / sizeof *ICONS))]; break;
    case 4: w.wind = !w.wind; break;
    default: {
        const Num &k = nums[facRow - 5];
        if (k.f == &w.fuse && launch != 1) break;  // only grenades have a fuse
        if (k.f) *k.f = Clamp(roundf((*k.f + dx * k.step) / k.step) * k.step, k.lo, k.hi);
        else *k.i = (int)Clamp(*k.i + dx, k.lo, k.hi);
        if (k.i == &w.clusters && launch == 2) w.clusters = std::max(w.clusters, 1);
    }
    }
    if (ok && facRow == 0) edit(w.name, "Weapon name");
    if (ok && facRow == 4) w.wind = !w.wind;

    text("WEAPON FACTORY", 640, 18, 48, GOLDEN, 1);
    popup({200, 80, 640, 600});
    Rectangle pv = {870, 120, 300, 300};
    panel(pv, false);
    if (!image(iconOf(w), {pv.x + 50, pv.y + 30, 200, 200})) text("?", pv.x + 150, pv.y + 80, 90, WHITE, 1);
    text(w.name.c_str(), pv.x + 150, pv.y + 244, 28, GOLDEN, 1);
    wrapped("In every match's weapon panel. Online, the host's weapons are used.", 1020, 444, 300, 20, LIGHTGRAY);
    for (int i = 0; i < N; i++) {
        Rectangle r = {216, 92 + i * 38.0f, 608, 34};
        bool hi = i == facRow;
        std::string v;
        const char *label = i == 0 ? "Name" : i == 1 ? "Launch" : i == 2 ? "Model" : i == 3 ? "Icon" : i == 4 ? "Wind" : nums[i - 5].label;
        if (i == 0) v = w.name + (typing && fmodf(t, 1) < 0.5f ? "_" : "");
        else if (i == 1) v = LAUNCH[launch];
        else if (i == 2) v = w.model;
        else if (i == 3) v = w.icon;
        else if (i == 4) v = w.wind ? "Affected" : "Not affected";
        else {
            const Num &k = nums[i - 5];
            v = k.f == &w.fuse && launch != 1 ? "Impact" : k.i == &w.count && w.count < 0 ? "Infinite" : k.f ? TextFormat(k.fmt, *k.f) : TextFormat(k.fmt, *k.i);
        }
        if (hi) DrawRectangleRounded(r, 0.4f, 6, {255, 210, 60, 70});
        text(label, r.x + 10, r.y + 5, 24, hi ? GOLDEN : WHITE);
        text(TextFormat(hi && i ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + 5, 24, WHITE, 2);
    }
    if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
    else if (facRow == 0) hints({{"A", "Enter", "Rename"}, {"D-pad", "Up/Down", "Move"}, {"B", "Esc", "Back"}});
    else hints({{"D-pad", "Up/Down", "Move"}, {"D-pad", "Left/Right", "Change"}, {"B", "Esc", "Back"}});
    if (back) screen = Factory;
}

// ---------------------------------------------------------------- HUD

void Hud::input(const Game &g, Input &in, bool local, int pad, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    const uint8_t held = in.buttons;  // before the panel blanks `in`: the swallow must wait for a real release
    if (cur.team < (int)g.cfg.teamSetup.size() && g.cfg.teamSetup[cur.team].cpu) local = false;
    mine = local;
    // swallow: a button still held from a menu (START at tick 0) or another turn must not fire or jump
    if (tick == 0) swallow = true;
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
        if (!(held & (Input::FIRE | Input::JUMP))) swallow = false;
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

// assets/ui/hud/<name>.png (or a src cell of it) scaled by s, rotated deg about pivot (src px) placed at pos
static bool sprite(const char *name, Vector2 pos, float s, Vector2 pivot, float deg = 0, Color tint = WHITE, Rectangle src = {}) {
    Texture2D t = tex(std::string("hud/") + name);
    if (!t.id) return false;
    if (!src.width) src = {0, 0, (float)t.width, (float)t.height};
    DrawTexturePro(t, src, {pos.x, pos.y, src.width * s, src.height * s}, {pivot.x * s, pivot.y * s}, deg, tint);
    return true;
}

// W4M HUD digits: "0-9 . m", '~' = infinity, ':' = two dots; h = cell height. Falls back to text().
static void digits(const char *s, float x, float y, float h, int align, bool grey = false) {
    static const char *ROW[2] = {"012345.", "6789~m"};
    static const short COL[2][7][2] = {{{6, 78}, {87, 131}, {142, 211}, {218, 290}, {300, 376}, {384, 455}, {463, 500}},
                                       {{6, 78}, {86, 149}, {158, 233}, {238, 312}, {319, 418}, {428, 508}}};
    Texture2D t = tex(grey ? "hud/hud_font_grey" : "hud/hud_font");
    if (!t.id) return text(s, x, y + h * 0.1f, h * 0.75f, grey ? WHITE : GOLDEN, align);
    float k = h / 128, w = 0;
    for (int pass = 0; pass < 2; pass++) {
        float cx = pass ? x - w * align / 2 : 0;
        for (const char *p = s; *p; p++)
            for (int r = 0; r < 2; r++) {
                const char *f = strchr(ROW[r], *p == ':' ? '.' : *p);
                if (!f) continue;
                float x0 = COL[r][f - ROW[r]][0], gw = COL[r][f - ROW[r]][1] - x0;
                Rectangle src = {x0, r * 128.0f, gw, 128};
                if (pass) DrawTexturePro(t, src, {cx, y, gw * k, h}, {}, 0, WHITE);
                if (pass && *p == ':') DrawTexturePro(t, src, {cx, y - h * 0.36f, gw * k, h}, {}, 0, WHITE);
                cx += gw * k * 0.9f;
            }
        w = cx;
    }
}

static void healthBar(int team, float x, float y, float w, float h, float frac) {
    Texture2D t = tex("fe/team_health");
    DrawRectangleRounded({x - 3, y - 3, w + 6, h + 6}, 0.3f, 4, {20, 20, 20, 230});
    DrawRectangleRounded({x, y, w, h}, 0.3f, 4, {170, 150, 110, 255});
    if (t.id) {
        float row = 4 + HEALTH_ROW[team % 4] * 102.0f, sw = 489 * frac;
        DrawTexturePro(t, {11, row, sw, 97}, {x, y, w * frac, h}, {}, 0, WHITE);
    } else {
        DrawRectangleRounded({x, y, w * frac, h}, 0.3f, 4, TEAM_COLORS[team % 4]);
    }
}

// Top-left compass centred on the active worm, up = camera forward: worm dots, crates, mines, aim target.
static void radar(const Game &g, Vector2 c, Vector3 fwd, bool aiming) {
    const float R = 54, RANGE = 40;  // px, metres
    const Color TEAL = {0, 104, 138, 210};
    Vector2 f = Vector2Normalize({fwd.x, fwd.z});
    Vector3 o = g.worms[g.current].pos;
    auto dir = [&](float dx, float dz) { return Vector2{-dx * f.y + dz * f.x, -(dx * f.x + dz * f.y)}; };
    auto at = [&](Vector3 p) {
        Vector2 v = Vector2Scale(dir(p.x - o.x, p.z - o.z), R / RANGE);
        float l = Vector2Length(v);
        return Vector2Add(c, l > R - 6 ? Vector2Scale(v, (R - 6) / l) : v);
    };
    if (!sprite("radar_back", c, 2 * R / 174, {128, 128})) DrawCircleV(c, R, TEAL), DrawRing(c, R, R + 3, 0, 360, 32, BLACK);
    for (int i = 0; i < 16; i++) {
        float a = i * PI / 8;
        Vector2 p = {c.x + cosf(a) * (R + 9), c.y + sinf(a) * (R + 9)};
        if (!sprite("radar_marks", p, 0.42f, {16, 32}, 0, WHITE, {i % 4 ? 32.0f : 0, 0, 32, 64})) DrawCircleV(p, i % 4 ? 2 : 3.5f, WHITE);
    }
    const char *NSEW = "NSWE";
    const Vector2 D[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};  // world (x, z): N = -z
    for (int i = 0; i < 4; i++) {
        Vector2 p = Vector2Add(c, Vector2Scale(dir(D[i].x, D[i].y), R + 22));
        if (!sprite("radar_nsew", p, 0.42f, {32, 32}, 0, WHITE, {(i & 1) * 64.0f, (i / 2) * 64.0f, 64, 64}))
            text(TextFormat("%c", NSEW[i]), p.x, p.y - 11, 22, GOLDEN, 1);
    }
    for (const Object &ob : g.objects) {
        if (ob.dead || ob.type == Object::Barrel) continue;
        Kind k = ob.weapon >= 0 && ob.weapon < (int)WEAPONS.size() ? WEAPONS[ob.weapon].kind : Kind::Shell;
        bool util = k == Kind::Rope || k == Kind::Jetpack || k == Kind::Teleport || k == Kind::Parachute || k == Kind::ChangeWorm;
        int cell = ob.type == Object::Mine ? 5 : ob.type == Object::Sentry ? 4 : ob.weapon < 0 ? 1 : util ? 2 : 0;
        Vector2 p = at(ob.pos);
        if (!sprite("radar_objects", p, 0.45f, {16, 16}, 0, WHITE, {(cell % 4) * 32.0f, (cell / 4) * 32.0f, 32, 32}))
            DrawRectangleV({p.x - 3, p.y - 3}, {6, 6}, cell == 5 ? RED : BROWN);
    }
    for (const Worm &w : g.worms) {
        if (!w.alive) continue;
        Vector2 p = at(w.pos);
        float r = &w == &g.worms[g.current] ? 6 : 4.5f;
        DrawCircleV(p, r + 1.5f, &w == &g.worms[g.current] ? WHITE : BLACK);
        DrawCircleV(p, r, TEAM_COLORS[w.team % 4]);
    }
    if (aiming && !sprite("radar_objects", at(g.target()), 0.5f, {16, 16}, 0, WHITE, {96, 0, 32, 32})) DrawCircleLinesV(at(g.target()), 5, WHITE);
}

void Hud::draw(const Game &g, const Camera3D &cam, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    // W4M worm labels: name over hp, team colour, black outline
    for (const Worm &w : g.worms) {
        if (!w.alive) continue;
        Vector3 top = Vector3Add(w.pos, {0, 1.1f, 0});
        float dist = Vector3DotProduct(Vector3Subtract(top, cam.position), fwd);
        if (dist < 0.5f) continue;
        Vector2 sp = GetWorldToScreen(top, cam);
        float s = Clamp(170 / dist, 12, 24);
        int i = int(&w - g.worms.data()), k = i % std::max(1, g.perTeam);
        Color c = TEAM_COLORS[w.team % 4];
        text(TextFormat("%d", w.hp > 0 ? w.hp : 0), sp.x, sp.y - s, s, c, 1);
        text(wormName(w.team, k), sp.x, sp.y - s * 2, s, c, 1);
        if (&w == &cur && g.phase == Phase::Aim) {  // bobbing "this one" arrow
            float b = sinf(tick * 0.12f) * 4;
            if (!sprite("wormlocarrow", {sp.x, sp.y - s * 2 - 18 + b}, 0.22f, {64, 120}, 0, c)) DrawTriangle({sp.x - 8, sp.y - s * 2 - 30 + b}, {sp.x, sp.y - s * 2 - 18 + b}, {sp.x + 8, sp.y - s * 2 - 30 + b}, c);
        }
    }
    if (g.phase == Phase::GameOver) {
        if (g.winner >= 0) text(TextFormat("%s WINS!", teamName(g.cfg, g.winner).c_str()), 640, 260, 70, TEAM_COLORS[g.winner % 4], 1);
        else text("DRAW!", 640, 260, 70, WHITE, 1);
        if (!g.cfg.mission && !quiet) hints({{"A", "Space", "Continue"}});  // missionEnd() has its own
        return;
    }
    const WeaponDef &wd = WEAPONS[g.weapon];
    Color tc = TEAM_COLORS[cur.team % 4];
    bool aiming = g.phase == Phase::Aim;
    // turn start banner + W4M "- Press Fire -" while the hot seat waits
    if (g.hotSeat || (aiming && g.timer > std::max(1, (int)g.cfg.scheme.turnTime) * 60 - 120))
        text(TextFormat("%s - %s", teamName(g.cfg, cur.team).c_str(), wormName(cur.team, g.current % std::max(1, g.perTeam))), 640, 14, 28, tc, 1);
    if (g.hotSeat && mine) text("- Press Fire -", 640, 350, 36, WHITE, 1);
    if (g.cfg.rules & RULE_ROPE_RACE) text(TextFormat("Race %ds", tick / 60), 640, 80, 26, GOLDEN, 1);
    bool crate = false;
    for (const Object &o : g.objects) crate = crate || (o.type == Object::Crate && o.falling);
    if (crate || g.suddenDeath) text(crate ? "Crate drop!" : "SUDDEN DEATH!", 640, 46, 26, GOLDEN, 1);

    radar(g, {124, 112}, fwd, aiming && mine);
    // wind: arrow along the wind on screen, length by strength; distance to the aim target
    Vector2 wc = {58, 236};
    float wind = g.wind, ws = fabsf(wind);
    if (!sprite(ws < 0.01f ? "wind_backdisabled" : "wind_back", wc, 0.72f, {64, 64})) DrawCircleV(wc, 30, {0, 104, 138, 220});
    if (ws >= 0.01f) {
        Vector2 f = Vector2Normalize({fwd.x, fwd.z}), v = {-wind * f.y, -wind * f.x};
        float deg = atan2f(v.y, v.x) * RAD2DEG, l = 0.12f + 0.12f * Clamp(ws / 1.5f, 0, 1);
        if (!sprite("wormlocarrow", wc, l, {64, 64}, deg - 90, {255, 170, 30, 255})) DrawLineEx(wc, Vector2Add(wc, Vector2Scale(Vector2Normalize(v), 24)), 5, ORANGE);
    }
    if (aiming) digits(TextFormat("%02dm", (int)roundf(Vector3Distance(cur.pos, g.target()))), 96, 216, 40, 0);
    // current weapon (top right) + ammo
    int ammo = g.ammo[cur.team][g.weapon];
    Vector2 wp = {1176, 92};
    if (!sprite("secondback", wp, 0.5f, {128, 128})) DrawCircleV(wp, 44, {0, 119, 155, 230});
    if (!image(iconOf(wd), {wp.x - 34, wp.y - 34, 68, 68}, ammo ? WHITE : GRAY)) text(wd.name.substr(0, 4).c_str(), wp.x, wp.y - 12, 22, WHITE, 1);
    digits(ammo < 0 ? "~" : TextFormat("%d", ammo), wp.x, wp.y + 44, 40, 1);
    text(wd.name.c_str(), wp.x - 54, wp.y - 10, 22, ammo ? WHITE : GRAY, 2);
    // turn timer (bottom right): turn seconds, round clock below
    int left = aiming && g.hotSeat ? g.hotSeat : aiming || g.phase == Phase::Retreat ? g.timer : 0, secs = (left + 59) / 60;
    Vector2 tp = {1180, 612};
    bool urgent = (secs <= 5 && aiming && !g.hotSeat) || g.phase == Phase::Retreat;
    if (!sprite("timer_back", tp, 0.62f, {128, 128}, 0, urgent && tick / 15 % 2 ? Color{255, 120, 120, 255} : WHITE)) DrawCircleV(tp, 54, {0, 119, 155, 230});
    digits(TextFormat("%d", secs), tp.x, tp.y - 34, 56, 1, true);
    int round = std::max(0, g.cfg.scheme.roundTime * 3600 - g.clock) / 60;
    digits(TextFormat("%02d:%02d", round / 60, round % 60), tp.x, tp.y + 18, 26, 1, true);
    if (g.phase == Phase::Retreat || g.hotSeat) text(g.hotSeat ? "READY" : "RETREAT", tp.x, tp.y - 82, 22, GOLDEN, 1);
    // team health (bottom centre, above the hints)
    static const char *FLAGS[4] = {"flags/custom_cool", "flags/custom_police", "flags/custom_genie", "flags/custom_crown"};
    int maxHp = std::max(1, (int)g.cfg.scheme.health) * std::max(1, g.perTeam);
    for (int t = 0; t < g.teams; t++) {
        int hp = 0;
        for (const Worm &w : g.worms) if (w.team == t && w.alive) hp += w.hp > 0 ? w.hp : 0;
        float y = 646 - (g.teams - 1 - t) * 38.0f;
        text(teamName(g.cfg, t).c_str(), 574, y - 1, 24, TEAM_COLORS[t % 4], 2);
        if (!image(FLAGS[t % 4], {584, y - 4, 32, 32})) DrawRectangleRounded({584, y - 4, 32, 32}, 0.2f, 4, TEAM_COLORS[t % 4]);
        healthBar(t, 628, y, 240, 24, Clamp((float)hp / maxHp, 0, 1));
    }
    // power (stacked blocks, fill from the bottom) and pitch arc (bottom left)
    Vector2 pb = {22, 520};
    float ps = 0.62f, pf = Clamp(g.power, 0, 1);
    if (sprite("powerbar_off", pb, ps, {80, 30})) {
        float top = 215 - (215 - 41) * pf;
        if (pf > 0) sprite("powerbar_on", {pb.x, pb.y + (top - 30) * ps}, ps, {80, 0}, 0, WHITE, {0, top, 256, 256 - top});
    } else {
        DrawRectangleRounded({pb.x, pb.y, 40, 120}, 0.3f, 4, {0, 0, 0, 150});
        DrawRectangleRounded({pb.x + 4, pb.y + 4 + 112 * (1 - pf), 32, 112 * pf}, 0.3f, 4, ColorLerp(YELLOW, RED, pf));
    }
    Vector2 ap = {102, 590};  // arc pivot: flat edge centre
    float deg = -cur.pitch * RAD2DEG;
    if (sprite("angle_back", ap, 0.6f, {70, 130})) sprite("angle_head", ap, 0.55f, {16, 32}, deg);
    else {
        DrawCircleSector(ap, 58, -90, 90, 16, {0, 104, 138, 210});
        DrawLineEx(ap, {ap.x + cosf(-cur.pitch) * 56, ap.y + sinf(-cur.pitch) * 56}, 4, GOLDEN);
        DrawCircleV(ap, 7, MAROON);
    }
    // state hints
    if (g.shotsLeft) text(TextFormat("%d shot(s) left", g.shotsLeft), 190, 600, 22, WHITE);
    if (g.roped) text("Rope: stick swings, aim = length, jump releases", 190, 600, 22, WHITE);
    if (g.jetting) {
        text("Jetpack: hold fire to thrust, jump to stop", 190, 600, 22, WHITE);
        healthBar(1, 190, 630, 240, 16, Clamp(g.fuel / fmaxf(wd.fuse, 0.01f), 0, 1));
    }
    if (open) hints({{"D-pad", "Up/Down/Left/Right", "Move"}, {"A", "Enter", "Select"}, {"B/X", "Backspace/Q", "Close"}});
    else if (tick < 300 && !quiet) hints({{"-", "F1", "Hold: controls"}});
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
        if (!image(iconOf(WEAPONS[i]), ic, a ? WHITE : Fade(GRAY, 0.5f))) {
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
        controls(true);
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

// ---------------------------------------------------------------- replays

int replayList(const std::vector<std::string> &files, int &sel, bool &instant) {
    int n = (int)files.size(), first = std::max(0, std::min(sel - 4, n - 9));
    sel = clampWrap(sel + P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP}), n);
    if (P({X}, {KEY_Y})) instant = !instant;
    text("REPLAYS", 640, 50, 60, GOLDEN, 1);
    for (int i = first; i < n && i < first + 9; i++) {
        Rectangle r = {290, 130 + (i - first) * 52.0f, 700, 46};
        panel(r, i == sel);
        text(files[i].substr(0, files[i].size() - 4).c_str(), r.x + 20, r.y + 9, 28, i == sel ? GOLDEN : WHITE);
    }
    if (!n) text("No replays yet: finished matches are saved here", 640, 300, 28, LIGHTGRAY, 1);
    text(TextFormat("Instant replay of big shots: %s", instant ? "On" : "Off"), 640, 610, 26, WHITE, 1);
    hints({{"A", "Enter", "Watch"}, {"X", "Y", "Instant replay"}, {"B", "Esc", "Back"}});
    if (P({B}, {KEY_BACKSPACE, KEY_ESCAPE})) return -2;
    return n && P({A}, {KEY_ENTER, KEY_SPACE}) ? sel : -1;
}

void playbackBar(bool paused, int speed, bool freeCam, float sec, float total, const char *note) {
    DrawRectangle(340, 58, 600, 60, Fade(BLACK, 0.55f));
    text(TextFormat("%s  %dx   %d:%02d / %d:%02d%s", paused ? "PAUSED" : "REPLAY", speed, (int)sec / 60, (int)sec % 60, (int)total / 60, (int)total % 60,
                    freeCam ? "   free camera" : ""), 640, 62, 26, GOLDEN, 1);
    DrawRectangle(360, 100, 560, 8, Fade(WHITE, 0.3f));
    DrawRectangle(360, 100, (int)(560 * (total > 0 ? sec / total : 1)), 8, GOLDEN);
    if (note && *note) text(note, 640, 126, 26, ORANGE, 1);
    if (freeCam) hints({{"A", "Space", "Pause"}, {"R", "Tab", "Speed"}, {"Y", "N", "Next turn"}, {"X", "C", "Chase cam"}, {"LS/RS", "Arrows/WASD", "Move/look"}, {"B", "Esc", "Quit"}});
    else hints({{"A", "Space", "Pause"}, {"R", "Tab", "Speed"}, {"Y", "N", "Next turn"}, {"X", "C", "Free cam"}, {"B", "Esc", "Quit"}});
}

void replayBadge() {
    if (fmodf((float)GetTime(), 1) < 0.7f) text("REPLAY", 640, 70, 44, GOLDEN, 1);
    hints({{"A", "Space", "Skip replay"}});
}

void lanGames(const std::vector<LanGame> &games, int sel, const std::string &status) {
    text("LAN GAMES", 640, 40, 60, GOLDEN, 1);
    for (size_t i = 0; i < games.size() && i < 6; i++) {
        const LanGame &g = games[i];
        Rectangle r = {290, 150 + i * 70.0f, 700, 60};
        panel(r, (int)i == sel);
        text(g.name.c_str(), r.x + 24, r.y + 14, 30, (int)i == sel ? GOLDEN : WHITE);
        text(TextFormat("%s  %d/%d%s", g.ip.c_str(), g.players, g.maxPlayers, g.started ? "  playing" : ""), r.x + r.width - 24, r.y + 18, 24, LIGHTGRAY, 2);
    }
    if (games.empty()) text("Looking for games on this network...", 640, 220, 30, LIGHTGRAY, 1);
    text(status.c_str(), 640, 620, 22, ORANGE, 1);
    hints({{"A", "Enter", "Join"}, {"X", "C", "Host game"}, {"B", "Esc", "Back"}});
}

void room(const Net &net, const GameConfig &opt, bool lan, const std::string &status) {
    bool isHost = net.hostId == net.id;
    text(lan ? "LAN GAME" : "ROOM", 640, 40, 60, GOLDEN, 1);
    int row = 0, humans = std::min<int>((int)net.players.size(), 4);
    auto line = [&](const std::string &team, const std::string &who, bool cpu) {
        Rectangle r = {290, 140 + row * 66.0f, 700, 58};
        panel(r, false);
        Color c = row < 4 ? TEAM_COLORS[row] : GRAY;
        DrawRectangleRounded({r.x + 14, r.y + 12, 10, r.height - 24}, 1, 4, c);
        text(team.c_str(), r.x + 40, r.y + 12, 30, c);
        text(who.c_str(), r.x + r.width - 24, r.y + 18, 22, cpu ? SKYBLUE : WHITE, 2);
        row++;
    };
    for (const NetPlayer &p : net.players) {
        auto it = net.profiles.find(p.id);
        std::string team = p.id == net.id && !opt.teamSetup.empty() ? opt.teamSetup[0].name : it != net.profiles.end() ? it->second.team.name : p.name;
        line(team, p.name + (p.id == net.hostId ? " (host)" : "") + (p.id == net.id ? " - you" : "") + (p.online ? "" : " - offline"), false);
    }
    auto hp = net.profiles.find(net.hostId);
    int cpus = isHost ? opt.teams - 1 : hp != net.profiles.end() ? hp->second.cpus : 0;
    for (int k = 0; k < cpus && humans + k < 4; k++)
        line(isHost && k + 1 < (int)opt.teamSetup.size() ? opt.teamSetup[k + 1].name : TextFormat("CPU team %d", k + 1),
             TextFormat("CPU %d", isHost && k + 1 < (int)opt.teamSetup.size() ? std::max<int>(opt.teamSetup[k + 1].cpu, 1) : 2), true);
    if (humans + cpus > 4) text("4 teams max: extra CPU teams are left out", 640, 140 + row * 66.0f, 20, LIGHTGRAY, 1);
    text(status.c_str(), 640, 620, 22, ORANGE, 1);
    if (isHost) {
        text(net.players.size() >= 2 ? "Ready" : "Waiting for a second console to join...", 640, 580, 24, LIGHTGRAY, 1);
        hints({{"A", "Enter", "Start (2+ consoles)"}, {"B", "Esc", "Leave"}});
    } else {
        text("Waiting for the host to start...", 640, 580, 24, LIGHTGRAY, 1);
        hints({{"B", "Esc", "Leave"}});
    }
}

// ---------------------------------------------------------------- single player

static std::string clockText(int ticks) { return TextFormat("%d:%02d.%d", ticks / 3600, ticks / 60 % 60, ticks % 60 / 6); }

// Word-wrapped text, at most maxLines lines; returns the height used.
static float paragraph(const std::string &s, float x, float y, float w, float size, Color c, int maxLines = 99) {
    std::string line, word;
    int lines = 0;
    auto flush = [&] { if (lines < maxLines) text(line.c_str(), x, y + lines * (size + 4), size, c); lines++, line.clear(); };
    for (size_t i = 0; i <= s.size(); i++) {
        char ch = i < s.size() ? s[i] : ' ';
        if (ch != ' ' && ch != '\n') { word += ch; continue; }
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(t.c_str(), size) > w) flush(), t = word;
        line = t, word.clear();
        if (ch == '\n') flush();
    }
    if (!line.empty()) flush();
    return std::min(lines, maxLines) * (size + 4);
}

int missionMenu(MissionMenu &st, const std::vector<MissionSpec> &list, const Progress &p) {
    static const char *TABS[2] = {"Missions", "Challenges"};
    std::vector<int> rows;
    for (size_t i = 0; i < list.size(); i++) if ((list[i].kind == "mission") == (st.tab == 0)) rows.push_back((int)i);
    int &sel = st.sel[st.tab], n = (int)rows.size();
    sel = n ? clampWrap(sel, n) : 0;
    bool ok = P({A}, {KEY_ENTER, KEY_SPACE}), back = P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
    int pick = n ? rows[sel] : -1;
    bool open = pick >= 0 && p.unlocked(list, pick);
    if (st.brief && pick >= 0) {
        const MissionSpec &m = list[pick];
        popup({140, 40, 1000, 630});
        text(m.name.c_str(), 640, 60, 48, GOLDEN, 1);
        text(m.campaign.c_str(), 640, 112, 22, SKYBLUE, 1);
        if (!image(m.preview.empty() ? preview(m.map) : m.preview, {180, 150, 240, 240})) image(preview(m.map), {180, 150, 240, 240});
        float y = 150 + paragraph(m.brief, 450, 150, 650, 24, WHITE, 9) + 16;
        text("Objectives", 450, y, 28, GOLDEN), y += 36;
        for (const MissionSpec::Goal &g : m.objectives) text(("- " + goalText(m, g, nullptr)).c_str(), 470, y, 24, WHITE), y += 30;
        for (const MissionSpec::Goal &g : m.fail) text(("- " + goalText(m, g, nullptr)).c_str(), 470, y, 24, ORANGE), y += 30;
        y = std::max(y + 10, 420.0f);
        for (size_t t = 0; t < m.teams.size(); t++) {
            const MissionSpec::TeamSpec &ts = m.teams[t];
            text(TextFormat("%s: %d worm%s%s", ts.name.c_str(), (int)ts.worms.size(), ts.worms.size() > 1 ? "s" : "", t == 0 ? " (you)" : ts.idle ? "" : TextFormat(" (CPU %d)", ts.cpu)),
                 180, y, 22, TEAM_COLORS[t % 4]);
            y += 28;
        }
        Progress::Entry e = p.get(m.id);
        if (e.done) text(TextFormat("Best time %s", clockText(e.best).c_str()), 1100, 630, 24, GOLDEN, 2);
        if (m.par) text(TextFormat("Par %d:%02d", m.par / 60, m.par % 60), 180, 630, 24, LIGHTGRAY);
        hints({{"A", "Enter", "Start"}, {"B", "Esc", "Back"}});
        if (back) st.brief = false;
        return ok ? pick : -1;
    }
    st.tab = clampWrap(st.tab + P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT}), 2);
    if (n) sel = clampWrap(sel + P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP}), n);
    text("SINGLE PLAYER", 640, 16, 52, GOLDEN, 1);
    for (int t = 0; t < 2; t++) {
        Rectangle r = {40 + t * 300.0f, 80, 280, 50};
        panel(r, t == st.tab);
        text(TABS[t], r.x + r.width / 2, r.y + 10, 30, t == st.tab ? GOLDEN : WHITE, 1);
    }
    int first = std::max(0, std::min(sel - 4, n - 9));
    for (int k = first; k < n && k < first + 9; k++) {
        const MissionSpec &m = list[rows[k]];
        Rectangle r = {40, 146 + (k - first) * 58.0f, 700, 52};
        Progress::Entry e = p.get(m.id);
        bool lock = !p.unlocked(list, rows[k]);
        panel(r, k == sel);
        text(m.name.c_str(), r.x + 20, r.y + 11, 28, lock ? GRAY : k == sel ? GOLDEN : WHITE);
        text(lock ? "Locked" : e.done ? TextFormat("Done  %s", clockText(e.best).c_str()) : "New", r.x + r.width - 20, r.y + 14, 22,
             lock ? GRAY : e.done ? GOLDEN : SKYBLUE, 2);
    }
    if (!n) text(st.tab ? "No challenges found" : "No missions found", 390, 300, 28, LIGHTGRAY, 1);
    if (pick >= 0) {
        const MissionSpec &m = list[pick];
        Rectangle pv = {800, 146, 420, 236};
        panel({pv.x - 8, pv.y - 8, pv.width + 16, pv.height + 16}, false);
        std::string img = m.preview.empty() ? preview(m.map) : m.preview;
        if (!image(img, pv, open ? WHITE : GRAY) && !image(preview(m.map), pv, open ? WHITE : GRAY)) DrawRectangleRec(pv, {30, 60, 40, 255});
        text(m.campaign.c_str(), 800, 400, 22, SKYBLUE);
        paragraph(open ? goalText(m, m.objectives[0], nullptr) : "Complete the previous mission to unlock", 800, 430, 420, 26, open ? WHITE : GRAY, 2);
        paragraph(m.brief, 800, 500, 420, 20, LIGHTGRAY, 7);
    }
    hints({{"D-pad", "Left/Right", TABS[1 - st.tab]}, {"A", "Enter", "Briefing"}, {"B", "Esc", "Back"}});
    if (back) return -2;
    if (ok && open) st.brief = true;
    return -1;
}

void missionHud(const Game &g, const MissionSpec &m) {
    int lines = (int)(m.objectives.size() + m.fail.size());
    DrawRectangleRounded({390, 78, 500, 34.0f + lines * 26}, 0.2f, 6, Fade(BLACK, 0.45f));
    text(TextFormat("%s  %s", m.name.c_str(), clockText(g.run.ticks).c_str()), 640, 82, 24, GOLDEN, 1);
    float y = 110;
    for (size_t i = 0; i < m.objectives.size(); i++, y += 26)
        text(((i < g.run.met.size() && g.run.met[i] ? "[x] " : "[ ] ") + goalText(m, m.objectives[i], &g)).c_str(), 410, y, 20, WHITE);
    for (const MissionSpec::Goal &f : m.fail) {
        std::string s = goalText(m, f, &g);
        if (f.type == MissionSpec::Goal::Time) s = "Time left " + clockText(std::max(0, f.seconds * 60 - g.run.ticks));
        text(("(!) " + s).c_str(), 410, y, 20, ORANGE), y += 26;
    }
}

int missionEnd(const Game &g, const MissionSpec &m, const Progress::Entry &best, bool hasNext) {
    bool won = g.run.result > 0;
    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 110});
    popup({340, 170, 600, 380});
    text(m.kind == "mission" ? (won ? "MISSION COMPLETE!" : "MISSION FAILED") : (won ? "CHALLENGE COMPLETE!" : "CHALLENGE FAILED"), 640, 196, 44, won ? GOLDEN : ORANGE, 1);
    text(m.name.c_str(), 640, 252, 28, WHITE, 1);
    paragraph(won ? m.success : m.failure, 380, 300, 520, 22, LIGHTGRAY, 3);
    text(TextFormat("Time  %s", clockText(g.run.ticks).c_str()), 640, 400, 32, WHITE, 1);
    if (best.done) text(won && best.best == g.run.ticks ? "New best time!" : TextFormat("Best  %s", clockText(best.best).c_str()), 640, 444, 26, GOLDEN, 1);
    if (won && hasNext) hints({{"A", "Enter", "Next"}, {"X", "R", "Retry"}, {"B", "Backspace", "Back to list"}});
    else hints({{"A", "Enter", won ? "Continue" : "Retry"}, {"B", "Backspace", "Back to list"}});
    if (P({A}, {KEY_ENTER, KEY_SPACE})) return won ? (hasNext ? 1 : 3) : 2;
    if (P({X}, {KEY_R})) return 2;
    return P({B}, {KEY_BACKSPACE, KEY_ESCAPE}) ? 3 : 0;
}

}  // namespace Ui

#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#endif
#include "ui.h"
#include "controls.h"
#include "audio.h"
#include "models.h"
#include "frontbg.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <map>
#include <thread>

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#else
#define DATA_DIR "./"
#endif
#ifdef __SWITCH__
#define ROMFS_DIR "romfs:/"
#else
#define ROMFS_DIR "./romfs/"
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
const Color GOLDEN = {255, 210, 60, 255}, PANEL = {24, 74, 92, 230};  // W4M teal paper
const char *RULE_LABELS[] = {"King", "Highlander", "Vampire", "Karma", "Low gravity", "Rope race", "Sudden death", nullptr};  // [7]: tr()'d
const int RULES = 8;
// Scheme edit page: one row per Scheme byte, in struct order. names: enum labels (min = 0).
struct SchemeField { const char *label, *fmt; int min, max, step; const char *names[9]; };
const SchemeField SCHEME_FIELDS[] = {
    {"Turn time", "%d s", 5, 90, 5, {}}, {"Retreat time", "%d s", 0, 10, 1, {}}, {"Hot seat time", "%d s", 0, 10, 1, {}},
    {"Round time", "%d min", 5, 60, 5, {}}, {"Worm energy", "%d", 25, 250, 25, {}}, {"Crate drops", "%d%%", 0, 100, 10, {}},
    {"Weapon crates", "%d", 0, 100, 10, {}}, {"Health crates", "%d", 0, 100, 10, {}}, {"Utility crates", "%d", 0, 100, 10, {}},
    {"Health crate", "%d hp", 5, 100, 5, {}}, {"Mines", "%d", 0, 15, 15, {}}, {"Oil drums", "%d", 0, 10, 10, {}},  // W4M Objects: 15 mines, 10 drums
    {"Mine fuse", nullptr, 0, 6, 1, {"0 s", "1 s", "2 s", "3 s", "4 s", "5 s", "Random", "7 s", "8 s"}},  // editor -1..5 (0x752f33), Family 8
    {"Sudden death", nullptr, 0, 2, 1, {"1 HP", "Water only", "Draw"}}, {"Fall damage", nullptr, 0, 1, 1, {"Off", "On"}},
    {"Wind", nullptr, 0, 3, 1, {"None", "Low", "Medium", "High"}}, {"Weapons", nullptr, 0, 3, 1, {"Default", "BnG", "Crates only", "Unlimited"}},
    {"Water rise", nullptr, 0, 3, 1, {"None", "Slow", "Medium", "Fast"}},  // FETXT.WaterNoRise / Slow / Medium / FastRise
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
double t0;  // raylib-nx's GetTime() counts from console boot: as a float it barely moves
float now() { return (float)(GetTime() - t0); }
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

// Frontend art, decoded off the main thread at startup: SD read + PNG decode of these took a frame on menu changes
const char *const PRELOAD[] = {"fe/bluedivide", "fe2/art_local", "fe2/art_local_static", "fe2/nav_normal", "fe/title_underline", "fe2/art_network",
                               "fe2/art_help", "fe2/art_myworms", "fe/paperpopup01", "fe/paperpopup02", "fe/buttonbig_highlight", "fe/text_border_charcoal",
                               "fe/icon_wxpot", "fe2/paper_strip", "fe/icon_splat", "fe2/loading_worm", "fe/hintpanel", "hud/trailparticle",
                               "back/loadbackgeneric", "fe/tournament_vsus"};
const int NPRE = sizeof PRELOAD / sizeof *PRELOAD;
Image preImg[NPRE];
std::atomic<int> preDecoded{0};
int preUploaded = 0;
std::thread preThread;

void decodeArt() {
#ifdef __SWITCH__
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3F);  // only runs while the main thread waits (vsync, GPU)
#endif
    for (int i = 0; i < NPRE; i++) {
        char p[160];
        snprintf(p, sizeof p, DATA_DIR "assets/ui/%s.png", PRELOAD[i]);
        int n = 0;
        if (unsigned char *d = FileExists(p) ? LoadFileData(p, &n) : nullptr) preImg[i] = LoadImageFromMemory(".png", d, n), UnloadFileData(d);
        ImageMipmaps(&preImg[i]);  // here: no GPU mipmap blits on the main thread
        preDecoded.store(i + 1);
    }
}

void uploadArt() {
    if (preUploaded >= preDecoded.load()) return;
    Image &img = preImg[preUploaded];
    if (img.data && !cache.count(PRELOAD[preUploaded])) {
        Texture2D t = LoadTextureFromImage(img);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        cache[PRELOAD[preUploaded]] = t;
    }
    UnloadImage(img), img = {};
    preUploaded++;
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

// Level picture: the map's "preview" (W4M Frontend_Image, set by w4m-maps), else its theme's picture, else nolevel
std::string preview(const std::string &m) {
    static std::map<std::string, std::string> cache;
    auto c = cache.find(m);
    if (c != cache.end()) return c->second;
    static const std::map<std::string, std::string> THEME = {
        {"arabian", "level_arabian"}, {"camelot", "level_camelot"}, {"construction", "level_building"}, {"jurassic", "level_prehistoric"}, {"wildwest", "level_wildwest"}};
    std::string pv = m.empty() ? "random_camelot" : "";
    for (const char *dir : {DATA_DIR "assets/maps/", ROMFS_DIR "maps/"}) {
        if (!pv.empty() || m.empty()) break;
        FILE *f = fopen((dir + m + ".json").c_str(), "rb");
        if (!f) continue;
        char head[400] = {};
        size_t n = fread(head, 1, sizeof head - 1, f);
        fclose(f);
        head[n] = 0;
        auto field = [&](const char *key) {
            const char *k = strstr(head, key);
            const char *e = k ? strchr(k + strlen(key), '"') : nullptr;
            return e ? std::string(k + strlen(key), e) : std::string();
        };
        pv = field("\"preview\": \"");
        if (pv.empty() || !tex("levels/" + pv).id) pv = THEME.count(field("\"theme\": \"")) ? THEME.at(field("\"theme\": \"")) : "";
    }
    return cache[m] = tex("levels/" + pv).id ? "levels/" + pv : "levels/nolevel";
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
        {"holyhandgrenade", "hollyhandgrenade"}, {"fatkinsstrike", "fatkinstrike"}, {"flood", "raindance"}, {"changeworm", "wormselect"}, {"bubbletrouble", "bubbletrubble"}};
    auto a = ALIAS.find(k);
    if (a != ALIAS.end()) k = a->second;
    if (k == "armour") return "hud/hud_shield";  // W4M has no HUD/Weapons icon for kUtilityArmour; HUD.Armour = "HUD Shield.tga"
    return "weapons/" + k;
}

// W4M WEAPTWK DisplayName: the language's Text.kWeapon* / Text.kUtility* text ("Super Airstrike" -> Bovine Blitz), else our name
const char *weaponName(const WeaponDef &w) {
    static const std::map<std::string, std::string> ALIAS = {{"TailNail", "NoMoreNails"}, {"FatkinsStrike", "Fatkins"}, {"InflatableScouser", "Scouser"}};
    std::string k;
    for (char c : w.name) if (c != ' ') k += c;
    if (auto a = ALIAS.find(k); a != ALIAS.end()) k = a->second;
    const char *s = tr(("Text.kWeapon" + k).c_str(), nullptr);
    return s ? s : tr(("Text.kUtility" + k).c_str(), w.name.c_str());
}

int clampWrap(int v, int n) { return n ? ((v % n) + n) % n : 0; }

// single press of any button/key on any pad, with W4M's menu sound for it (menus only: the HUD calls pressed())
bool P(std::initializer_list<int> b, std::initializer_list<int> k) {
    if (!pressed(-1, b, k)) return false;
    using S = Audio::Sfx;
    int f = b.size() ? *b.begin() : -1;
    if (f == UP || f == DOWN || f == LEFT || f == RIGHT || f == A || f == B)
        Audio::play(f == UP || f == DOWN ? S::FeHighlight : f == A ? S::FeClick : f == B ? S::FeCancel : S::FeChange);
    return true;
}
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

int language = 0;
static std::map<std::string, std::string> strings;
static int stringsFor = -1;

const char *tr(const char *key, const char *en, const char *fr) {
    const char *path = TextFormat(DATA_DIR "assets/lang/%s.txt", language ? "fr" : "en");
    if (stringsFor != language && (stringsFor = language, strings.clear(), FileExists(path))) {
        char *txt = LoadFileText(path);
        for (char *l = strtok(txt, "\n"); l; l = strtok(nullptr, "\n")) {
            char *tab = strchr(l, '\t');
            if (!tab) continue;
            std::string v;
            for (char *c = tab + 1; *c; c++) v += c[0] == '\\' && c[1] == 'n' ? (c++, '\n') : *c;
            strings[std::string(l, tab)] = v;
        }
        UnloadFileText(txt);
    }
    auto it = key ? strings.find(key) : strings.end();
    return it != strings.end() ? it->second.c_str() : language && fr ? fr : en;
}

// lang.txt ("en" / "fr"), else the console / desktop locale
static int systemLanguage() {
    if (FileExists(DATA_DIR "lang.txt")) {
        char *l = LoadFileText(DATA_DIR "lang.txt");
        int fr = !strncmp(l, "fr", 2);
        UnloadFileText(l);
        return fr;
    }
#ifdef __SWITCH__
    u64 code = 0;
    SetLanguage sl = SetLanguage_ENUS;
    if (R_SUCCEEDED(setInitialize())) {
        if (R_SUCCEEDED(setGetSystemLanguage(&code))) setMakeLanguage(code, &sl);
        setExit();
    }
    return sl == SetLanguage_FR || sl == SetLanguage_FRCA;
#else
    for (const char *v : {"LANGUAGE", "LC_ALL", "LANG"})
        if (const char *e = getenv(v); e && *e) return !strncmp(e, "fr", 2);
    return 0;
#endif
}

void load() {
    t0 = GetTime();
    if (!preThread.joinable()) preThread = std::thread(decodeArt);
    language = systemLanguage();
    std::vector<int> cps;
    for (int c = 32; c < 256; c++) if (c < 127 || c > 160) cps.push_back(c);
#ifdef __SWITCH__
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);  // single Joy-Cons are held sideways (one per player)
#endif
    // W4M's FE.Font (tools/w4m-ui, user's install), else the Switch shared font / a desktop TTF
#ifdef __SWITCH__
    chdir(DATA_DIR);  // raylib turns the BMFont page dir of "sdmc:/..." into "./sdmc:/...": load it relative
#endif
    if (FileExists("assets/ui/font/w4m.fnt")) {
        font = LoadFont("assets/ui/font/w4m.fnt");
        fontLoaded = font.texture.id != GetFontDefault().texture.id;  // LoadFont falls back to the default font
        if (fontLoaded) {
            GenTextureMipmaps(&font.texture);
            SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
            return;
        }
    }
#ifdef __SWITCH__
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
    if (preThread.joinable()) preThread.join();
    for (Image &i : preImg) if (i.data) UnloadImage(i), i = {};
    preUploaded = NPRE;
    for (auto &kv : cache) if (kv.second.id) UnloadTexture(kv.second);
    cache.clear();
    if (fontLoaded) UnloadFont(font);
    fontLoaded = false;
#ifdef __SWITCH__
    if (plOk) plExit();
#endif
}

// c2: bottom colour of a vertical gradient over the line height (W4M menu items)
static void textG(const char *t, float x, float y, float size, Color c, Color c2, int align) {
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
        bool main = d.x == 0 && d.y == 0;
        Color col = main ? c : k;
        auto at = [&](float vy) {
            Color v = main ? ColorLerp(c, c2, Clamp((vy - q.y - size * 0.2f) / (size * 0.7f), 0, 1)) : col;
            rlColor4ub(v.r, v.g, v.b, v.a);
        };
        rlColor4ub(col.r, col.g, col.b, col.a);
        for (const G &e : gs) {
            const Rectangle &r = font.recs[e.g];
            float x = q.x + e.ox + font.glyphs[e.g].offsetX * s - pad * s, y = q.y + e.oy + font.glyphs[e.g].offsetY * s - pad * s;
            float w = (r.width + 2 * pad) * s, h = (r.height + 2 * pad) * s, u0 = (r.x - pad) / W, v0 = (r.y - pad) / H;
            float u1 = (r.x - pad + (r.width + 2 * pad)) / W, v1 = (r.y - pad + (r.height + 2 * pad)) / H;
            at(y), rlTexCoord2f(u0, v0), rlVertex2f(x, y);
            at(y + h), rlTexCoord2f(u0, v1), rlVertex2f(x, y + h);
            rlTexCoord2f(u1, v1), rlVertex2f(x + w, y + h);
            at(y), rlTexCoord2f(u1, v0), rlVertex2f(x + w, y);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

void text(const char *t, float x, float y, float size, Color c, int align) { textG(t, x, y, size, c, c, align); }

float textWidth(const char *t, float size) { return MeasureTextEx(font, t, size, fontLoaded ? 0 : size / 10).x; }
const Font &textFont() { return font; }
Texture2D art(const char *name) { return tex(name); }

// W4M menu look: cream text, golden titles over a brush underline, charcoal stroke + orange arrow on the selection
static const Color CREAM = {238, 226, 186, 255}, BRIGHT = {255, 250, 232, 255}, TITLE = {255, 224, 120, 255};
static Color ink(bool hi) { return hi ? BRIGHT : CREAM; }
static void tri(Vector2 a, Vector2 b, Vector2 c, Color col);

static void brush(Rectangle r) {
    if (!image("fe/text_border_charcoal", {r.x - r.height * 0.3f, r.y - r.height * 0.15f, r.width + r.height * 0.6f, r.height * 1.3f}))
        DrawRectangleRounded(r, 0.5f, 6, {10, 10, 10, 200});
}

// selectable row: stroke behind the whole row
static void mark(Rectangle r, bool hi) {
    if (hi) brush(r);
}

// centred menu entry: the stroke hugs the label
static void item(const char *label, float cx, float y, float size, bool hi) {
    float w = textWidth(label, size) + size * 0.9f;
    mark({cx - w / 2, y - size * 0.1f, w, size * 1.15f}, hi);
    text(label, cx, y, size, ink(hi), 1);
}

static void heading(const char *t, float cx, float y, float size) {
    float w = fmaxf(textWidth(t, size) * 1.25f, size * 4);
    text(t, cx, y, size, TITLE, 1);
    if (!image("fe/title_underline", {cx - w / 2, y + size * 0.98f, w, w / 16}, CREAM)) DrawRectangle(cx - w / 2, y + size, w, 3, CREAM);
}

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

static bool menuPage = false;  // menu pages show no button bar (W4M); the in-game HUD keeps its hints
void hints(std::initializer_list<Hint> h) {
    if (menuPage) return;
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


// both windings: rlgl culls back faces
static void tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col), DrawTriangle(a, c, b, col); }

// Generic twin-stick controller drawn with shapes, each control's callout arrow pointing at it.
void controls(bool game) {
    enum { ZL, L, MIN, LS, DPAD, ZR, R, PLS, BX, BY, BA, BB, RS };
    static const Vector3 PART[] = {{-185, -137, 6}, {-160, -112, 6}, {-45, -70, 14}, {-150, -35, 40}, {-75, 35, 40}, {185, -137, 6},
                                   {160, -112, 6}, {45, -70, 14}, {150, -69, 20}, {116, -35, 20}, {184, -35, 20}, {150, -1, 20}, {75, 35, 40}};  // x, y, radius
    struct Call { int part; float ly; const char *label, *key; };
    static const Call GAME[] = {
        {ZL, 222, "Hold: precise aim\nJetpack: drop", "RMB"}, {L, 276, "L + stick: aim (single Joy-Con)\nHold: sky view (strikes)", nullptr}, {MIN, 356, "Hold: controls", "F1"},
        {LS, 414, "Move (camera-relative)\nAim mode: walk / turn", "Arrows"}, {DPAD, 488, "Zoom in / out\nWeapon panel cursor", "X/Z"},
        {ZR, 232, "Fire", "Space"}, {R, 270, "Next weapon", "Tab"}, {PLS, 306, "Pause", "Esc"}, {BX, 342, "Weapon panel", "Q"},
        {BY, 380, "Next weapon", "Tab"}, {BA, 440, "Fire (hold = power)\nStrikes: sky view, then fire", "Space/E"}, {BB, 482, "Jump (twice = backflip)\nSky view: leave", "Enter"},
        {RS, 530, "Camera orbit\nAim mode: aim (+ gyro)", "A/D/W/S"}};
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
    if (game) text(keys ? "Hold right mouse button: aim with the mouse  -  F3: performance overlay"
                        : "Aim mode: hold ZL, or while charging (A)  -  L + R: performance overlay", 640, 640, 22, LIGHTGRAY, 1);
    if (game) text(keys ? "Jetpack: Space thrusts, arrows steer, Backspace drops dynamite / mine / sheep"  // W4M UtilityFire group
                        : "Jetpack: A / ZR thrust, left stick steers, ZL drops dynamite / mine / sheep", 640, 666, 22, LIGHTGRAY, 1);
    else text("Each screen lists its other buttons at the bottom", 640, 640, 22, LIGHTGRAY, 1);
}

bool preload(double until) {
    while (preUploaded < NPRE && preUploaded < preDecoded.load() && GetTime() < until) uploadArt();
    if (preUploaded < NPRE) return true;
    if (preThread.joinable()) preThread.join();  // all decoded: join here so no exit path leaves it running
    return false;
}

void background() {
    uploadArt();  // one GPU upload per frame
    Texture2D t = tex("back/loadbackgeneric");
    if (t.id) DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {0, 0, 1280, 720}, {}, 0, WHITE);
    else DrawRectangleGradientV(0, 0, 1280, 720, {40, 80, 150, 255}, {120, 170, 220, 255});
    FrontBg::draw(GetFrameTime());
}

// tilted deg about its centre
void logo(float cx, float y, float w, float deg) {
    Texture2D t = tex("fe/tournament_vsus");
    Rectangle src = {130, 50, 780, 400};
    float h = w * src.height / src.width;
    rlPushMatrix();
    rlTranslatef(cx, y + h / 2, 0);
    rlRotatef(deg, 0, 0, 1);
    if (t.id) {
        DrawTexturePro(t, src, {-w / 2, -h / 2, w, h}, {}, 0, WHITE);
        text("NX", w * 0.41f, h * 0.30f, w * 0.09f, GOLDEN, 1);
    } else {
        text("WORMS4NX", 0, -w * 0.08f, w * 0.15f, GOLDEN, 1);
    }
    rlPopMatrix();
}

// ---------------------------------------------------------------- W4M menus

#ifndef W4NX_VERSION
#define W4NX_VERSION "0.1.0"
#endif
// x, y: label centre; deg: tilt
struct MenuItem { const char *key, *en, *fr; float x, y, size, deg; };

static const Color GOLD_TOP = {255, 240, 130, 255}, GOLD_BOT = {245, 140, 25, 255}, INK = {22, 36, 58, 255}, BLUE_PANEL = {2, 79, 119, 255};
static float easeOut(float k) { k = Clamp(k, 0, 1); return 1 - (1 - k) * (1 - k) * (1 - k); }

// W4M menu entry: gold gradient label tilted about its centre; glow 0..1 = highlight (white on a black brush stroke, pulsing)
static void menuEntry(const char *label, float cx, float cy, float size, float deg, float glow, float appear, float t) {
    if (appear <= 0) return;
    float w = textWidth(label, size), s = 1 + glow * (0.04f - 0.04f * cosf(t * 4 * PI));
    cx = fminf(cx, 1250 - w * 0.54f);  // long translations stay on screen
    rlPushMatrix();
    rlTranslatef(cx + (1 - appear) * 260, cy, 0);
    rlRotatef(deg, 0, 0, 1);
    rlScalef(s, s, 1);
    if (glow > 0.01f) {
        Rectangle b = {-w / 2 - size * 0.55f, -size * 0.8f, w + size * 1.1f, size * 1.55f};
        if (!image("fe/icon_splat", b, Fade(WHITE, glow * appear))) DrawRectangleRounded(b, 0.6f, 6, Fade(BLACK, 0.85f * glow * appear));
    }
    Color top = Fade(ColorLerp(GOLD_TOP, WHITE, glow), appear), bot = Fade(ColorLerp(GOLD_BOT, WHITE, glow), appear);
    textG(label, 0, -size * 0.52f, size, top, bot, 1);
    rlPopMatrix();
}

// Bottom torn paper strip: scrolling ticker, version; back: bobbing back arrow (submenus)
static void paperStrip(float t, bool back) {
    const float y = 652;  // paper band y + 16 .. y + 80, flush with the bottom edge
    Texture2D p = tex("fe2/paper_strip");
    if (p.id) for (float x = 0; x < 1280; x += 255) DrawTexturePro(p, {0, 0, 256, 128}, {x, y, 256, 128}, {}, 0, WHITE);
    else DrawRectangle(0, y + 16, 1280, 64, {246, 243, 232, 255}), DrawRectangle(0, y + 12, 1280, 4, BLACK);
    const char *tick = tr("WXFE.TickerTapeDefault", "Worms4NX - fan-made homebrew                    ");
    float w = textWidth(tick, 38) + 160;
    for (float x = -fmodf(t * 90, w); x < 1280; x += w) text(tick, roundf(x), y + 28, 38, INK);  // whole pixels: no shimmer while it scrolls
    if (!back) return;
    Rectangle d = {16, 572 + 5 * sinf(t * 3), 96, 96};
    Texture2D a = tex("fe2/nav_normal");
    if (a.id) DrawTexturePro(a, {0, a.height / 2.0f, a.width / 2.0f, a.height / 2.0f}, d, {}, 0, WHITE);
    else tri({d.x + 14, d.y + 52}, {d.x + 60, d.y + 22}, {d.x + 60, d.y + 82}, ORANGE);
}

static float backOut(float k) { k = Clamp(k, 0, 1) - 1; return 1 + 2.7f * k * k * k + 1.7f * k * k; }

// Submenu page: curved blue panel (slides in from the left) with the title, its vertical watermark and an illustration
// that pops in after it; p: 0 hidden .. 1 shown (the panel takes the first 0.7)
static void subPanel(const char *title, const char *art, float t, float p) {
    float x = (1 - easeOut(p / 0.7f)) * -820, pop = backOut((p - 0.3f) / 0.7f);
    if (!image("fe/bluedivide", {x - 60, -40, 800, 800})) DrawCircleV({x - 260, 360}, 760, BLUE_PANEL);
    rlPushMatrix();
    rlTranslatef(x + 40, 700, 0);
    rlRotatef(-90, 0, 0, 1);
    text(title, 0, 0, 150, {255, 255, 255, 22});
    rlPopMatrix();
    float tw = textWidth(title, 34);
    text(title, x + 44, 26, 34, GOLD_TOP);
    if (!image("fe/title_underline", {x + 36, 62, tw + 24, 18})) DrawRectangle(x + 40, 66, tw + 10, 3, WHITE);
    Rectangle r = {x + 70, 140 + 8 * sinf(t * 1.6f), 400, 400};
    rlPushMatrix();
    rlTranslatef(r.x + r.width / 2, r.y + r.height / 2, 0);
    rlRotatef(3 * sinf(t * 1.1f), 0, 0, 1);
    rlScalef(pop, pop, 1);
    if (!strcmp(art, "fe2/art_local") && tex(art).id) {  // the TV robot shows noise
        float o = (float)((int)(t * 12) * 37 % 97);
        DrawTexturePro(tex("fe2/art_local_static"), {o, o * 0.7f, 128, 128}, {-0.06f * r.width, -0.14f * r.height, 0.34f * r.width, 0.36f * r.height}, {}, 0, WHITE);
    }
    image(art, {-r.width / 2, -r.height / 2, r.width, r.height});
    rlPopMatrix();
}

#ifdef __SWITCH__
static const int MAIN_ITEMS = 5;  // console games leave through HOME, no Quit entry
#else
static const int MAIN_ITEMS = 6;
#endif
// W4M layouts: staggered, tilted, one size per entry
static const MenuItem MAIN_MENU[] = {
    {"FETXT.LocalGame", "Local Game", "Partie locale", 905, 150, 62, -3},
    {"FETXT.HTPHeader3", "Network Game", "Partie en réseau", 975, 248, 48, 2},
    {"FETXT.MyWorms", "My Worms", "Mes Worms", 880, 334, 56, -2},
    {nullptr, "Replays", "Replays", 990, 416, 46, 3},
    {"FETXT.Help&Options", "Help & Options", "Aide et options", 900, 494, 52, -2},
    {"Lang.Quit", "Quit", "Quitter", 1010, 570, 44, 2},
};
static const MenuItem LOCAL_MENU[] = {
    {"FETXT.QuickGame", "Quick Game", "Partie rapide", 870, 160, 60, -3},
    {nullptr, "Custom match", "Partie personnalisée", 1075, 262, 50, 3},  // W4M calls it Versus: too vague
    {"FETXT.Story", "Story", "Histoire", 860, 362, 68, -2},
    {"FETXT.Challenges", "Challenges", "Défis", 1060, 470, 52, 2},
};
static const MenuItem NET_MENU[] = {
    {"FETXT.LocalNetwork", "Local Network", "Réseau local", 900, 220, 60, -3},
    {"FETXT.Online", "Online", "En ligne", 1040, 360, 58, 2},
};
static const MenuItem HELP_MENU[] = {
    {"FETXT.Options", "Options", "Options", 890, 190, 60, -3},
    {"FETXT.Controls", "Controls", "Contrôles", 1050, 300, 52, 2},
    {nullptr, "Weapon Factory", "Usine d'armes", 900, 420, 56, -2},
};

static const float LEAVE = 0.18f;
static bool feBack = false;  // last A/B in a menu was B: screen changes play W4M's prev in/out sounds
// W4M's intro sound for screen s (kAUDIO_In_*), entered from `from`
static Audio::Sfx enterSfx(Frontend::Screen s, Frontend::Screen from) {
    using F = Frontend;
    using S = Audio::Sfx;
    if (s == F::Confirm || s == F::SchemeEdit) return S::FePopupIn;
    if (from == F::Confirm || from == F::SchemeEdit) return S::FePopupOut;
    if (feBack) return S::FePrevIn;
    switch (s) {
    case F::Main: return S::FeBounce;
    case F::Local: case F::HelpOpts: return S::FeSlide;
    case F::Network: return S::FeNet;
    case F::MyWorms: return S::FeCustom;
    case F::Options: return S::FeSoundVid;
    case F::Controls: return S::FeController;
    case F::Factory: case F::FactoryEdit: return S::FeFactory;
    case F::Wormpot: return S::FeWormpot;
    default: return S::FeNextIn;
    }
}
static int bgPage(Frontend::Screen s, bool online) {
    using F = Frontend;
    return s <= F::Main || s == F::Confirm ? 0 : s == F::Local || (s == F::Setup && !online) ? 1 : s == F::Network || s == F::Setup ? 2 : 3;
}

float Frontend::subIn(float t) const { return leaving >= 0 ? 1 - (t - leaving) / LEAVE : (t - entered) / 0.4f; }
void Frontend::go(Screen s) {
    next = s, leaving = now(), FrontBg::page(bgPage(s, online));
    Audio::play(feBack ? Audio::Sfx::FePrevOut : Audio::Sfx::FeNextOut);
}

void Frontend::menu(const MenuItem *items, int n, int &sel, int dy, float t, bool live) {
    sel = clampWrap(sel + dy, n);
    float k = fminf(1, GetFrameTime() * 14);
    for (int i = 0; i < n; i++) {
        const MenuItem &m = items[i];
        float &g = glow[i], a = !live ? 1 : leaving >= 0 ? 1 - easeOut((t - leaving - 0.012f * i) / (LEAVE - 0.06f))
                                                           : easeOut((t - entered - 0.05f * i) / 0.35f);
        g = live ? g + ((i == sel) - g) * k : i == sel;
        menuEntry(tr(m.key, m.en, m.fr), m.x, m.y, m.size, m.deg, g, a, t);
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
        if (!cfg.teamSetup[k].cpu) cfg.teamSetup[k].cpu = dx < 0 ? 5 : 1;
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
        else if (sscanf(line, "wormpot %d", &a) == 1) cfg.wormpot = wormpotSlots((uint32_t)a);
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
            t.cpu = (uint8_t)Clamp(b, 0, 5);
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
    menuPage = true;
    if (!loaded) loadSetup(cfg, maps);
    if (cfg.teamSetup.size() < 4) cfg.teamSetup.resize(4);
    Action act = None;
    float t = now();
    if (leaving >= 0 && t - leaving >= LEAVE) screen = next, leaving = -1;
    bool typing = editing != nullptr, busy = typing || leaving >= 0;  // no input while the menu flies out
    if (editing) {
        for (int c = GetCharPressed(); c; c = GetCharPressed())
            if (c >= 32 && c < 256 && editing->size() < 20) editing->push_back((char)c), Audio::play(Audio::Sfx::FeType);
        if (IsKeyPressed(KEY_BACKSPACE) && !editing->empty()) editing->pop_back(), Audio::play(Audio::Sfx::FeType);
        if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, A)) editing = nullptr, Audio::play(Audio::Sfx::FeClick);
    }
    int dy = busy ? 0 : P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP});
    int dx = busy ? 0 : P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT});
    bool ok = !busy && P({A}, {KEY_ENTER, KEY_SPACE}), back = !busy && P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
    if (ok || back) feBack = back;
    if (screen != shown) {  // W4M menus: slide in (not on the first frame: --ui captures), highlight the current entry
        entered = shown == (Screen)-1 ? -100 : t, from = shown, shown = screen;
        int sel = screen == Main ? mainRow : screen >= Local ? subRow[screen - Local] : 0;
        for (int i = 0; i < 8; i++) glow[i] = i == sel;
        FrontBg::page(bgPage(screen, online));
        if (from != (Screen)-1) Audio::play(enterSfx(screen, from));
    }

    BeginDrawing();
    background();
    switch (screen) {
    case Title: {
        logo(640, 90, 760);
        if (fmodf(t, 1.2f) < 0.8f)
            text(keyGlyphs() ? tr(nullptr, "Press Enter to start", "Appuyez sur Entrée") : tr(nullptr, "Press A to start", "Appuyez sur A"), 640, 560, 44, BRIGHT, 1);
        text(tr(nullptr, "Worms4NX - fan-made homebrew", "Worms4NX - homebrew de fan"), 640, 648, 20, CREAM, 1);
#ifdef __SWITCH__
        if (ok) screen = Main;
#else
        if (ok) screen = Main;
        else if (P({PLUS}, {KEY_ESCAPE})) act = Quit;
#endif
        break;
    }
    case Main:
    case Confirm: {
        bool confirm = screen == Confirm;
        float a = from == Title ? easeOut((t - entered) / 0.4f) : 1;  // the title's logo glides to its menu spot
        float up = leaving >= 0 ? easeOut((t - leaving) / LEAVE) : from == Title ? 0 : 1 - easeOut((t - entered) / 0.35f);  // to / from a submenu
        logo(Lerp(640, 330, a), Lerp(90, 40, a) - up * 300, Lerp(760, 560, a), -6 * a);
        menu(MAIN_MENU, MAIN_ITEMS, mainRow, confirm ? 0 : dy, t, !confirm);
        rlPushMatrix();
        rlTranslatef(0, (1 - a) * 110, 0);
        paperStrip(t, false);
        rlPopMatrix();
        if (confirm) {
            int &r = subRow[4];
            r = clampWrap(r + dy + dx, 2);
            DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 120});
            popup({330, 200, 620, 320});
            const char *q = tr("FETXT.ConfirmQuit", "Exit Game? Are You Sure?", "Quitter le jeu ? Vraiment ?");
            float qw = fmaxf(textWidth(q, 36) + 40, 300);
            text(q, 640, 236, 36, GOLD_TOP, 1);
            if (!image("fe/title_underline", {640 - qw / 2, 280, qw, 22})) DrawRectangle(640 - qw / 2, 286, qw, 3, WHITE);
            for (int i = 0; i < 2; i++) {
                bool hi = i == r;
                float sz = hi ? 50 : 42, s = hi ? 1.04f - 0.04f * cosf(t * 4 * PI) : 1;
                rlPushMatrix();
                rlTranslatef(640, 360 + i * 78.0f, 0);
                rlScalef(s, s, 1);
                text(i ? tr("FETXT.Yes", "Yes", "Oui") : tr("FETXT.No", "No", "Non"), 0, -sz / 2, sz, hi ? WHITE : Color{150, 205, 238, 255}, 1);
                rlPopMatrix();
            }
            if (back || (ok && !r)) screen = Main;
            else if (ok) act = Quit;
            break;
        }
        if (back) screen = Title;
        if (ok) {
            Screen to[] = {Local, Network, MyWorms, Main, HelpOpts, Confirm};
            if (mainRow == 3) act = Replays;
            else if (mainRow == 5) screen = Confirm, subRow[4] = 0;
            else go(to[mainRow]);
            if (mainRow == 2) online = false, loaded = false;  // the local setup.txt teams
        }
        break;
    }
    case Local: case Network: case HelpOpts: {
        int k = screen == Local ? 0 : screen == Network ? 1 : 3, n = k == 0 ? 4 : k == 1 ? 2 : 3;
        const MenuItem *items = k == 0 ? LOCAL_MENU : k == 1 ? NET_MENU : HELP_MENU;
        const char *title = k == 0 ? tr("FETXTH.LOCALGAME", "LOCAL GAME", "PARTIE LOCALE") : k == 1 ? tr("FETXTH.NetworkPlay", "NETWORK PLAY", "JEU EN RÉSEAU")
                                   : tr("FETXTH.HELP&OPTIONS", "HELP & OPTIONS", "AIDE ET OPTIONS");
        subPanel(title, k == 0 ? "fe2/art_local" : k == 1 ? "fe2/art_network" : "fe2/art_help", t, subIn(t));
        int &sel = subRow[k];
        menu(items, n, sel, dy, t);
        paperStrip(t, true);
        hints({{"A", "Enter", tr(nullptr, "Select", "Sélectionner")}, {"B", "Esc", tr(nullptr, "Back", "Retour")}});
        if (back) go(Main);
        if (ok && k == 0) {
            if (sel == 0) act = QuickMatch;
            else if (sel == 1) screen = Setup, online = lan = false, row = 0, loaded = false;
            else act = SinglePlayer, missionTab = sel - 2;
        }
        if (ok && k == 1) screen = Setup, online = true, lan = sel == 0, row = 0, loaded = false;  // reload: net setup has its own file
        if (ok && k == 3) screen = sel == 0 ? Options : sel == 1 ? Controls : Factory, row = 0, facSel = 0;
        break;
    }
    case MyWorms: {
        // the 4 teams' name, voice and hat (setup.txt, also edited in the match setup)
        int nb = Audio::voiceBanks(), &r = subRow[2];
        r = clampWrap(r + dy, 12);
        int k = r / 3, f = r % 3;
        GameConfig::Team &tm = cfg.teamSetup[k];
        if (f == 0 && ok) edit(tm.name, "Team name");
        if (f == 1 && dx && nb) tm.voice = (uint8_t)clampWrap(tm.voice + dx, nb), Audio::setTeamVoice(k, tm.voice);
        if (f == 2 && dx && hats) tm.hat = (uint8_t)clampWrap(tm.hat + dx, hats + 1);
        subPanel(tr("FETXTH.MYWORMS", "MY WORMS", "MES WORMS"), "fe2/art_myworms", t, subIn(t));
        for (int i = 0; i < 4; i++) {
            const GameConfig::Team &m = cfg.teamSetup[i];
            float a = leaving >= 0 ? 1 - easeOut((t - leaving - 0.012f * i) / (LEAVE - 0.06f)) : easeOut((t - entered - 0.06f * i) / 0.35f);
            Rectangle c = {560 + (i % 2) * 355.0f + (1 - a) * 400, 104 + (i / 2) * 250.0f, 335, 220};
            popup(c);
            if (k == i && !nine("fe/buttonbig_highlight", {c.x - 6, c.y - 6, c.width + 12, c.height + 12}, 110, 0.5f)) DrawRectangleRoundedLinesEx(c, 0.1f, 6, 4, GOLDEN);
            DrawRectangleRounded({c.x + 16, c.y + 18, 10, c.height - 36}, 1, 4, TEAM_COLORS[i]);
            const char *vals[3] = {TextFormat("%s%s", m.name.c_str(), editing == &cfg.teamSetup[i].name && fmodf(t, 1) < 0.5f ? "_" : ""),
                                   nb ? Audio::voiceBankName(m.voice) : "-", !hats ? "-" : m.hat ? Models::hatName(m.hat - 1) : tr(nullptr, "None", "Aucun")};
            const char *labels[3] = {nullptr, tr("FETXTSH.Voice", "Voice", "Voix"), tr(nullptr, "Hat", "Chapeau")};
            for (int j = 0; j < 3; j++) {
                bool hi = k == i && f == j;
                Rectangle l = {c.x + 36, c.y + 20 + j * 62.0f, c.width - 56, 50};
                if (hi) brush(l);
                if (!j) { text(vals[0], l.x + 8, l.y + 8, 32, TEAM_COLORS[i]); continue; }
                text(labels[j], l.x + 8, l.y + 12, 24, ink(hi));
                text(TextFormat(hi ? "< %s >" : "%s", vals[j]), l.x + l.width - 8, l.y + 12, 24, ink(hi), 2);
            }
        }
        paperStrip(t, true);
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else hints({{"A", "Enter", f ? tr(nullptr, "Listen", "Écouter") : tr(nullptr, "Rename", "Renommer")}, {"D-pad", "Left/Right", tr(nullptr, "Change", "Changer")},
                    {"B", "Esc", tr(nullptr, "Save & back", "Enregistrer")}});
        if (back) saveSetup(cfg), go(Main);
        break;
    }
    case Options: {
        const int n = 4;
        row = clampWrap(row + dy, n);
        heading(tr("FETXT.Options", "Options", "Options"), 640, 40, 60);
        popup({230, 150, 820, 420});
        std::string portS = TextFormat("%d", port);
        const char *labels[n] = {tr(nullptr, "Player name", "Nom du joueur"), tr(nullptr, "Server", "Serveur"), tr(nullptr, "Music", "Musique"), tr("FETXT.Language", "Language", "Langue")};
        const std::string vals[n] = {name, host + ":" + portS, music ? tr("FETXT.On", "On", "Oui") : tr("FETXT.Off", "Off", "Non"), language ? "Français" : "English"};
        for (int i = 0; i < n; i++) {
            Rectangle r = {290, 190 + i * 86.0f, 700, 60};
            mark(r, i == row);
            text(labels[i], r.x + 24, r.y + 12, 34, ink(i == row));
            bool ed = editing && ((i == 0 && editing == &name) || (i == 1 && editing == &host));
            text(TextFormat("%s%s", vals[i].c_str(), ed && fmodf(t, 1) < 0.5f ? "_" : ""), r.x + r.width - 24, r.y + 12, 34, ink(i == row), 2);
        }
        if (row == 1 && dx) port = Clamp(port + dx, 1, 65535);
        if (row == 2 && (dx || ok)) music = !music, Audio::music(music);
        if (row == 3 && (dx || ok)) language = !language, SaveFileText(DATA_DIR "lang.txt", (char *)(language ? "fr\n" : "en\n"));
        if (ok && row == 0) edit(name, "Player name");
        if (ok && row == 1) edit(host, "Server address");
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else if (row == 1) hints({{"A", "Enter", "Edit address"}, {"D-pad", "Left/Right", "Port"}, {"B", "Esc", "Save & back"}});
        else hints({{"A", "Enter", row >= 2 ? tr(nullptr, "Toggle", "Changer") : tr(nullptr, "Edit", "Modifier")}, {"B", "Esc", tr(nullptr, "Save & back", "Enregistrer")}});
        if (back) {
            for (char &c : name) if (c == ' ') c = '_';
            SaveFileText(DATA_DIR "server.txt", (char *)TextFormat("%s %d %s\n", host.c_str(), port, name.c_str()));
            screen = HelpOpts;
        }
        break;
    }
    case Controls: {
        if (layout) {
            controls(true);
            hints({{"B", "Esc", "Back"}});
            if (back || ok) layout = false;
            break;
        }
        ::Controls::Settings &s = ::Controls::settings;
        const int n = 8;
        row = clampWrap(row + dy, n);
        heading("Controls", 640, 20, 56);
        popup({230, 108, 820, 570});
        static const char *labels[n] = {"Aim sensitivity", "Camera sensitivity", "Invert aim Y", "Invert camera Y", "Gyro aiming (aim mode)", "Gyro sensitivity", "Rumble", "Button layout"};
        float *slider[n] = {&s.aim, &s.cam, nullptr, nullptr, nullptr, &s.gyro};
        bool *toggle[n] = {nullptr, nullptr, &s.invertAim, &s.invertCam, &s.gyroOn, nullptr, &s.rumbleOn};
        for (int i = 0; i < n; i++) {
            Rectangle r = {290, 130 + i * 66.0f, 700, 54};
            mark(r, i == row);
            text(labels[i], r.x + 24, r.y + 11, 32, ink(i == row));
            if (slider[i]) {
                Rectangle bar = {r.x + 380, r.y + 24, 180, 10};
                DrawRectangleRec(bar, {0, 0, 0, 120});
                DrawRectangleRec({bar.x, bar.y, bar.width * (*slider[i] - 0.2f) / 2.8f, bar.height}, GOLDEN);
            }
            const char *v = slider[i] ? TextFormat("x%.1f", *slider[i]) : toggle[i] ? (*toggle[i] ? "On" : "Off") : ">";
            text(v, r.x + r.width - 24, r.y + 11, 32, ink(i == row), 2);
        }
        if (slider[row] && dx) *slider[row] = Clamp(roundf(*slider[row] * 10 + dx) / 10, 0.2f, 3.0f);
        if (toggle[row] && (dx || ok)) *toggle[row] = !*toggle[row];
        if (row == 6 && (dx || ok)) ::Controls::rumble(0, 0.6f, 0.15f);  // feel it (no-op when off)
        if (row == 7 && ok) layout = true;
        hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", row == 7 ? "Open" : "Toggle"}, {"B", "Esc", "Save & back"}});
        if (back) ::Controls::save(DATA_DIR "controls.txt"), screen = HelpOpts;
        break;
    }
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
        heading("Game scheme", 640, 14, 48);
        popup({300, 80, 680, 590});
        for (int i = 0; i <= n; i++) {
            Rectangle r = {320, 92 + i * 31.0f, 640, 29};
            bool hi = i == schemeRow;
            std::string v = !i ? (preset < 0 ? "Custom" : SCHEMES[preset].name) : "";
            if (i) {
                const SchemeField &f = SCHEME_FIELDS[i - 1];
                v = f.names[0] ? f.names[bytes[i - 1] < 9 && f.names[bytes[i - 1]] ? bytes[i - 1] : f.max] : TextFormat(f.fmt, bytes[i - 1]);
            }
            if (hi) brush(r);
            text(i ? SCHEME_FIELDS[i - 1].label : "Preset", r.x + 10, r.y + 3, 22, hi ? BRIGHT : i ? CREAM : SKYBLUE);
            text(TextFormat(hi ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + 3, 22, WHITE, 2);
        }
        hints({{"D-pad", "Up/Down", "Move"}, {"D-pad", "Left/Right", "Change"}, {"B", "Esc", "Back"}});
        if (back || ok) screen = Setup;
        break;
    }
    case Setup: {
        // focus order: teams count, 4 fields per visible team, map, worms, rules, start
        std::vector<int> ids = {0};
        for (int k = 0; k < cfg.teams; k++) for (int f = 0; f < 4; f++) ids.push_back(100 + k * 4 + f);
        ids.push_back(201), ids.push_back(200), ids.push_back(299);  // screen order: map above worms
        for (int r = 0; r < RULES; r++) ids.push_back(300 + r);
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
            case 1: if (dx || ok) tm.cpu = (uint8_t)clampWrap(tm.cpu + (dx ? dx : 1), 6); break;
            case 2:
                if ((dx || ok) && nb) {
                    tm.voice = (uint8_t)clampWrap(tm.voice + (dx ? dx : 0), nb);
                    Audio::setTeamVoice(k, tm.voice);
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

        heading(online ? lan ? "LAN match" : "Online match" : "Local match", 640, 14, 48);
        auto value = [&](int vid, Rectangle r, const char *label, const std::string &v, float size) {
            bool hi = ids[row] == vid;
            if (hi) brush(r);
            text(label, r.x + 10, r.y + (r.height - size) / 2, size, ink(hi));
            text(TextFormat(hi ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + (r.height - size) / 2, size, ink(hi), 2);
        };
        value(0, {40, 86, 600, 40}, "Teams", online ? TextFormat("You + %d CPU", cfg.teams - 1) : TextFormat("%d", cfg.teams), 28);
        const char *CTRL[] = {tr("FETXT.HumanPlayer", "Human Player", "Joueur humain"), tr("FETXT.CPU1", "CPU Level 1", "I.A. Niveau 1"),
                              tr("FETXT.CPU2", "CPU Level 2", "I.A. Niveau 2"), tr("FETXT.CPU3", "CPU Level 3", "I.A. Niveau 3"),
                              tr("FETXT.CPU4", "CPU Level 4", "I.A. Niveau 4"), tr("FETXT.CPU5", "CPU Level 5", "I.A. Niveau 5")};
        for (int k = 0; k < cfg.teams; k++) {
            GameConfig::Team &tm = cfg.teamSetup[k];
            Rectangle card = {40, 134 + k * 134.0f, 600, 126};
            panel(card, ids[row] >= 100 + k * 4 && ids[row] < 104 + k * 4);
            DrawRectangleRounded({card.x + 16, card.y + 16, 12, card.height - 32}, 1, 4, TEAM_COLORS[k]);
            bool hiName = ids[row] == 100 + k * 4, ed = editing == &tm.name;
            if (hiName) brush({card.x + 36, card.y + 10, 540, 38});
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
        text(online ? "+1 team per console that joins" : "One controller per team,\nor share one", 930, 280, 18, LIGHTGRAY);
        popup({672, 330, 584, 270});
        value(299, {690, 342, 548, 28}, "Scheme", preset < 0 ? "Custom" : SCHEMES[preset].name, 22);
        for (int r = 0; r < RULES; r++)
            value(300 + r, {690, 372 + r * 24.0f, 548, 23}, RULE_LABELS[r] ? RULE_LABELS[r] : tr("RULE.NoDelays", "No weapon delays (test)", "Sans délai d'armes (test)"),
                  cfg.rules & (1u << r) ? "ON" : "off", 22);
        int pots = 0, pot = 0;
        for (int b = 0; b < WORMPOT_COUNT; b++) if (cfg.wormpot & (1u << b)) pots++, pot = b;
        value(350, {690, 372 + RULES * 24.0f, 548, 23}, "Wormpot", !pots ? "None" : pots == 1 ? WORMPOT_MODES[pot].name : TextFormat("%d modes", pots), 22);
        Rectangle go = {860, 608, 390, 70};
        panel(go, ids[row] == 400);
        text(online ? lan ? "FIND GAMES" : "GO ONLINE" : "START", go.x + go.width / 2, go.y + 16, 40, ink(ids[row] == 400), 1);
        if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
        else if (id == 299) hints({{"D-pad", "Left/Right", "Preset"}, {"A", "Enter", "Edit scheme"}, {"B", "Esc", "Back"}});
        else if (id == 350) hints({{"A", "Enter", "Open Wormpot"}, {"B", "Esc", "Back"}});
        else if (online) hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", "Edit/toggle"}, {"+", nullptr, "Go online"}, {"B", "Esc", "Back"}});
        else hints({{"D-pad", "Left/Right", "Change"}, {"A", "Enter", "Edit/toggle"}, {"+", nullptr, "Start"}, {APPLET, nullptr, "Tap: controllers"}, {"B", "Esc", "Back"}});
#ifdef __SWITCH__
        if (!online && minusTap) controllerApplet(humanTeams(cfg));
#endif
        if (back) saveSetup(cfg), screen = online ? Network : Local;
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
        if (act != Quit && act != Replays && act != SinglePlayer) Audio::play(Audio::Sfx::FeGrenade);  // match launch
        for (int k = 0; k < 4; k++) Audio::setTeamVoice(k, cfg.teamSetup[k].voice);
        cfg.teamSetup.resize(cfg.teams);
        cfg.custom = customs;
        loaded = false;  // reload (and re-pad to 4 teams) next time the menu shows
    }
    return act;
}

// ---------------------------------------------------------------- wormpot & weapon factory

namespace {
int reelSize(int r) { return (int)WORMPOT_REEL[r].size(); }
int reelMode(uint32_t wp, int r) { return wormpotReel(wp, r); }
void setReel(uint32_t &wp, int r, int m) { wormpotPick(wp, r, m); }
const char *modeName(int r, int m) { return m < 0 ? "- Empty Reel -" : WORMPOT_MODES[WORMPOT_REEL[r][m]].name; }

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

bool warmWeaponIcons(int &i) {
    if (i >= (int)WEAPONS.size()) return false;
    tex(iconOf(WEAPONS[i++]));
    return i < (int)WEAPONS.size();
}

void Frontend::wormpot(GameConfig &cfg, int dx, int dy, bool ok, bool back, float t) {
    bool spinning = false;
    for (int r = 0; r < 3; r++) {
        if (spinEnd[r] > 0 && t >= spinEnd[r]) spinEnd[r] = 0, setReel(cfg.wormpot, r, spinTo[r]), Audio::play(Audio::Sfx::WormpotStop);
        spinning |= spinEnd[r] > 0;
    }
    reel = clampWrap(reel + dx, 3);
    if (dy && !spinning) setReel(cfg.wormpot, reel, clampWrap(reelMode(cfg.wormpot, reel) + 1 + dy, reelSize(reel) + 1) - 1);
    if (!spinning && P({X}, {KEY_S})) {
        for (int r = 0; r < 3; r++) spinEnd[r] = t + 1 + 0.6f * r, spinTo[r] = GetRandomValue(-1, reelSize(r) - 1);
        spinning = true;
    }
    Audio::loop(Audio::Sfx::WormpotSpin, spinning);  // W4M WormPotLoop loops until the last reel stops
    if (!spinning && P({GAMEPAD_BUTTON_RIGHT_FACE_LEFT}, {KEY_R})) cfg.wormpot = 0;

    heading("Wormpot", 640, 14, 48);
    popup({120, 84, 1040, 500});
    image("fe/icon_wxpot", {140, 96, 96, 96});
    text("Wormpot modifies the game rules to create new ways to play.", 660, 120, 24, WHITE, 1);
    static const char *REELS[] = {"Weapons", "Worms", "Crates & Energy"};
    for (int r = 0; r < 3; r++) {
        Rectangle box = {190 + r * 310.0f, 210, 280, 270};
        text(REELS[r], box.x + box.width / 2, box.y - 36, 28, ink(r == reel), 1);
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
    wrapped(m < 0 ? "No wormpot mode selected on this reel." : WORMPOT_MODES[WORMPOT_REEL[reel][m]].help, 640, 500, 900, 24, WHITE);
    text(spinning ? "Spinning..." : "Spin those reels!", 640, 600, 30, GOLDEN, 1);
    hints({{"D-pad", "Left/Right", "Reel"}, {"D-pad", "Up/Down", "Nudge"}, {"X", "S", "Spin"}, {"Y", "R", "Reset"}, {"B", "Esc", "Back"}});
    if ((back || ok) && !spinning) screen = Setup;
}

void Frontend::factory(int dx, int dy, bool ok, bool back) {
    (void)dx;
    int n = (int)customs.size(), slots = std::min(n + 1, MAX_CUSTOM);
    facSel = clampWrap(facSel + dy, slots);
    heading("Weapon Factory", 640, 14, 48);
    popup({290, 84, 700, 590});
    for (int i = 0; i < slots; i++) {
        Rectangle r = {310, 100 + i * 70.0f, 660, 62};
        bool hi = i == facSel;
        if (hi) brush(r);
        if (i == n) { text("+ Create a weapon", r.x + r.width / 2, r.y + 16, 28, ink(hi), 1); continue; }
        const WeaponDef &w = customs[i];
        if (!image(iconOf(w), {r.x + 8, r.y + 3, 56, 56})) DrawRectangleRounded({r.x + 8, r.y + 3, 56, 56}, 0.3f, 4, GRAY);
        text(w.name.c_str(), r.x + 80, r.y + 6, 28, ink(hi));
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
        screen = HelpOpts;
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
    case 4: (launch == 3 ? w.avoid : w.wind) ^= 1; break;  // Homing: HomingAvoidLand instead of wind (a W4M Factory homing missile ignores wind)
    default: {
        const Num &k = nums[facRow - 5];
        if (k.f == &w.fuse && launch != 1) break;  // only grenades have a fuse
        if (k.f) *k.f = Clamp(roundf((*k.f + dx * k.step) / k.step) * k.step, k.lo, k.hi);
        else *k.i = (int)Clamp(*k.i + dx, k.lo, k.hi);
        if (k.i == &w.clusters && launch == 2) w.clusters = std::max(w.clusters, 1);
    }
    }
    if (ok && facRow == 0) edit(w.name, "Weapon name");
    if (ok && facRow == 4) (launch == 3 ? w.avoid : w.wind) ^= 1;

    heading("Weapon Factory", 640, 14, 48);
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
        const char *label = i == 0 ? "Name" : i == 1 ? "Launch" : i == 2 ? "Model" : i == 3 ? "Icon" : i == 4 ? (launch == 3 ? "Avoid land" : "Wind") : nums[i - 5].label;
        if (i == 0) v = w.name + (typing && fmodf(t, 1) < 0.5f ? "_" : "");
        else if (i == 1) v = LAUNCH[launch];
        else if (i == 2) v = w.model;
        else if (i == 3) v = w.icon;
        else if (i == 4) v = launch == 3 ? (w.avoid ? "Yes" : "No") : w.wind ? "Affected" : "Not affected";
        else {
            const Num &k = nums[i - 5];
            v = k.f == &w.fuse && launch != 1 ? "Impact" : k.i == &w.count && w.count < 0 ? "Infinite" : k.f ? TextFormat(k.fmt, *k.f) : TextFormat(k.fmt, *k.i);
        }
        if (hi) brush(r);
        text(label, r.x + 10, r.y + 5, 24, ink(hi));
        text(TextFormat(hi && i ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + 5, 24, WHITE, 2);
    }
    if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
    else if (facRow == 0) hints({{"A", "Enter", "Rename"}, {"D-pad", "Up/Down", "Move"}, {"B", "Esc", "Back"}});
    else hints({{"D-pad", "Up/Down", "Move"}, {"D-pad", "Left/Right", "Change"}, {"B", "Esc", "Back"}});
    if (back) screen = Factory;
}

// ---------------------------------------------------------------- HUD

// weapon panel slots: W4M crate-only effects (Double Damage, Crate Spy, Armour) are never in the inventory
static std::vector<int> panelSlots() {
    std::vector<int> v;
    for (size_t i = 0; i < WEAPONS.size(); i++) if (!collected(WEAPONS[i].kind)) v.push_back((int)i);
    return v;
}

void Hud::input(const Game &g, Input &in, bool local, int pad, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    const uint8_t held = in.buttons;  // before the panel blanks `in`: the swallow must wait for a real release
    if (cur.team < (int)g.cfg.teamSetup.size() && g.cfg.teamSetup[cur.team].cpu) local = false;
    mine = local;
    // swallow: a button still held from a menu (START at tick 0) or another turn must not fire or jump
    if (tick == 0) swallow = true;
    bool x = local && (forceX || pressed(pad, {X}, {KEY_Q}));
    if (x && g.phase == Phase::Settle && !g.countGroup.empty()) in.flags |= Input::SKIP_COUNT, reopen = true;  // observed in W4M by the user, 2026-10-03: the menu key stops the count
    if (x && counting >= 0) skipHp = true;
    if (!local || g.phase != Phase::Aim) {
        open = false, pick = -1, swallow = true;
        if (g.phase != Phase::Settle) reopen = false;
        return;
    }
    if (reopen) reopen = false, x = !open;
    int cols = PANEL_COLS;
    using S = Audio::Sfx;
    if (x) open = !open, cursor = g.held(), Audio::play(open ? S::FePopupIn : S::FePopupOut);
    if (open) {
        int dx = pressed(pad, {RIGHT}, {KEY_RIGHT}) - pressed(pad, {LEFT}, {KEY_LEFT});
        bool fwd = g.jetting && ((pad >= 0 && IsGamepadButtonDown(pad, UP)) || IsKeyDown(KEY_W));  // Jetpack.Forward, InGame group
        int dy = pressed(pad, {DOWN}, {KEY_DOWN}) - (pressed(pad, {UP}, {KEY_UP}) && !(fwd && pad >= 0 && IsGamepadButtonDown(pad, UP)));
        int was = cursor;
        std::vector<int> slots = panelSlots();
        int at = int(std::find(slots.begin(), slots.end(), cursor) - slots.begin()) % std::max<int>(1, (int)slots.size());
        cursor = slots.empty() ? 0 : slots[clampWrap(at + dx + dy * cols, (int)slots.size())];
        if (cursor != was) Audio::play(S::FeHighlight);
        if (pressed(pad, {A}, {KEY_SPACE, KEY_ENTER})) {
            if (g.pickable(cur.team, cursor)) select(cursor), Audio::play(S::FeClick);  // a tool out: a payload is its secondary, else it ends
            else Audio::play(S::FeError);
        }
        if (pressed(pad, {B}, {KEY_BACKSPACE})) open = false, swallow = true, Audio::play(S::FeCancel);
        // W4M 0x602ea0 disables only WormMoving (stick, jump; 0x506fa0 posts their releases): UtilityFire and InGame stay live.
        // W4M navigates with the stick (Menu group): the D-pad up stays Jetpack.Forward. Landed: A would also be FIRE, so no takeoff
        in = Input{}, in.buttons = g.jetting ? held & (Input::FIRE | Input::JUMP) : g.jetLanded() ? held & Input::PITCH : 0, in.walk = fwd ? 127 : 0;
    }
    if (swallow) {
        if (!(held & (Input::FIRE | Input::JUMP))) swallow = false;
        if (!g.jetting) in.buttons &= ~(Input::FIRE | Input::JUMP);  // in flight FIRE is the thrust and JUMP is ZL (Fire.Second), never a leftover press
    }
    if (pick >= 0 && (g.held() == pick || g.shotsLeft || !g.pickable(cur.team, pick))) pick = -1;
    if (pick >= 0) in.buttons = (in.buttons & ~(Input::FIRE | Input::JUMP)) | Input::NEXT_WEAPON, in.aim = Input::pick(pick).aim;  // pending pick: Controls still sees the old weapon, so a bounce press would fire it unaimed
}

// assets/ui/hud/<name>.png (or a src cell of it) scaled by s, rotated deg about pivot (src px) placed at pos
static bool sprite(const char *name, Vector2 pos, float s, Vector2 pivot, float deg = 0, Color tint = WHITE, Rectangle src = {}) {
    Texture2D t = tex(std::string("hud/") + name);
    if (!t.id) return false;
    if (!src.width) src = {0, 0, (float)t.width, (float)t.height};
    DrawTexturePro(t, src, {pos.x, pos.y, src.width * s, src.height * s}, {pivot.x * s, pivot.y * s}, deg, tint);
    return true;
}

// W4M Text3DEntity draws a "Name Backing.tga" frame behind the text (docs/w4m/render.md); (x, y) is the text centre.
static void text3d(const char *t, float x, float y, float size, Color c) {
    // user-requested (2026-10-03): no W4M Name Backing frame, a drop shadow instead
    float d = fmaxf(1, size / 14);
    text(t, x + d, y - size / 2 + d, size, {0, 0, 0, (unsigned char)(c.a * 0.6f)}, 1);
    text(t, x, y - size / 2, size, c, 1);
}

// PiP centre, half extents (px) and tilt (rad): HUDTWK PiP.Off/OnScreenPosition, OnScreenScale, OnScreenRotation z. PiPService
// (0x635e10, 0x6360d6) multiplies position and scale by 0x4d4cc0's (0.75 aspect, clamped to 4/3..16/9; 1): x 4/3 at 16:9.
// The scale as half extents is unverified: HUD.PiP is an XBitmapDescriptor quad built by the XOM renderer (table 0x91df5c).
static void pipPlace(float show, float full, Vector2 &c, Vector2 &h, float &rot) {
    const float u = 720 / 480.0f, k = Clamp(0.75f * 1280 / 720, 4 / 3.0f, 16 / 9.0f);  // HUD units: centre origin, y up, 480 high
    Vector2 on = Vector2Lerp({400, 155}, {190, 135}, show);
    c = Vector2Lerp({640 + on.x * k * u, 360 - on.y * u}, {640, 360}, full), h = Vector2Lerp({120 * show * k * u, 90 * show * u}, {640, 360}, full);
    rot = 0.1f * show * (1 - full);
}

void pipInset(const RenderTexture2D &scene, float show, float full) {
    Vector2 c, h;
    float rot;
    pipPlace(show, full, c, h, rot);
    if (h.x < 1) return;
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlRotatef(-rot * RAD2DEG, 0, 0, 1);
    DrawTexturePro(scene.texture, {0, 0, (float)scene.texture.width, -(float)scene.texture.height}, {-h.x, -h.y, 2 * h.x, 2 * h.y}, {}, 0, WHITE);
    Texture2D b = tex("fe/speech_popup");  // WXFE.Speech.Border.Edge, the bubble border (kMT_BubbleBorderNoPointer)
    float t = 14 * (1 - full) * show + 1, k = t * 1.6f, x0 = -h.x - t / 2, y0 = -h.y - t / 2, x1 = h.x + t / 2, y1 = h.y + t / 2;
    if (b.id) {
        DrawTexturePro(b, {150, 5, 240, 30}, {x0 + k, y0, x1 - x0 - 2 * k, t}, {}, 0, WHITE);
        DrawTexturePro(b, {150, 5, 240, -30}, {x0 + k, y1 - t, x1 - x0 - 2 * k, t}, {}, 0, WHITE);
        DrawTexturePro(b, {9, 10, 22, 230}, {x0, y0 + k, t, y1 - y0 - 2 * k}, {}, 0, WHITE);
        DrawTexturePro(b, {9, 10, -22, 230}, {x1 - t, y0 + k, t, y1 - y0 - 2 * k}, {}, 0, WHITE);
        DrawTexturePro(b, {127, 80, 40, 40}, {x0, y0, k, k}, {}, 0, WHITE);
        DrawTexturePro(b, {247, 80, 40, 40}, {x1 - k, y0, k, k}, {}, 0, WHITE);
        DrawTexturePro(b, {127, 208, 40, 40}, {x0, y1 - k, k, k}, {}, 0, WHITE);
        DrawTexturePro(b, {247, 208, 40, 40}, {x1 - k, y1 - k, k, k}, {}, 0, WHITE);
    } else DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, t, BLACK);
    rlPopMatrix();
}

void reticle(const WeaponDef &wd, Vector2 c, bool scope) {
    if (scope) {  // sniper sight: navy all around a round view, dark tapered cross with three ovals per arm
        const Color NAVY = {6, 18, 36, 255};
        const float r = 300;
        DrawRing(c, r, 1600, 0, 360, 72, NAVY);
        for (int i = 0; i < 14; i++) DrawRing(c, r - 42 + i * 3, r - 39 + i * 3, 0, 360, 72, Fade(NAVY, (i + 1) / 15.0f));
        for (Vector2 d : {Vector2{1, 0}, Vector2{-1, 0}, Vector2{0, 1}, Vector2{0, -1}}) {
            Vector2 n = {-d.y, d.x};
            DrawLineEx(Vector2Add(c, Vector2Scale(d, r)), Vector2Add(c, Vector2Scale(d, r * 0.55f)), 26, NAVY);
            for (int k = 0; k < 3; k++) {
                Vector2 o = Vector2Add(c, Vector2Scale(d, r * (0.64f + 0.1f * k)));
                float along = 9 + 2.0f * k, across = 20 + 7.0f * k;
                DrawEllipse((int)o.x, (int)o.y, fabsf(d.x) * along + fabsf(n.x) * across, fabsf(d.y) * along + fabsf(n.y) * across, NAVY);
            }
            DrawLineEx(Vector2Add(c, Vector2Scale(d, r * 0.55f)), Vector2Add(c, Vector2Scale(d, 16)), 4, NAVY);
        }
        DrawCircleV(c, 5, NAVY);
        return;
    }
    const Color CREAM = {255, 244, 228, 240};
    auto put = [&](const char *name, float w, float h, float top) {  // top: fraction of h above c
        Texture2D t = tex(std::string("hud/") + name);
        if (t.id) DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {c.x - w / 2, c.y - h * top, w, h}, {}, 0, CREAM);
        return t.id != 0;
    };
    bool ok;
    if (wd.kind == Kind::Homing) {
        Texture2D t[4] = {tex("hud/homing_tl"), tex("hud/homing_tr"), tex("hud/homing_bl"), tex("hud/homing_br")};
        ok = t[0].id && t[3].id;
        for (int k = 0; k < 4 && ok; k++)
            DrawTexturePro(t[k], {0, 0, (float)t[k].width, (float)t[k].height}, {c.x + (k % 2 ? 46 : -94), c.y + (k / 2 ? 46 : -94), 48, 48}, {}, 0, CREAM);
        put("bazookatargetinner", 185, 370, 0.5f);
    } else if (wd.kind == Kind::Shell && wd.fuse <= 0) ok = put("bazookatargetouter", 185, 185, 0.5f) && put("bazookatargetinner", 185, 370, 0.5f);
    else if (wd.kind == Kind::Shell) ok = put("target", 200, 200, 0.5f);  // thrown
    else ok = put("aimer_outer", 215, 215, 0.5f) && put("aimer_inner", 110, 110, 0.5f);
    if (!ok) {
        DrawRing(c, 9, 11, 0, 360, 24, CREAM);
        for (Vector2 d : {Vector2{1, 0}, Vector2{-1, 0}, Vector2{0, 1}, Vector2{0, -1}})
            DrawLineEx(Vector2Add(c, Vector2Scale(d, 14)), Vector2Add(c, Vector2Scale(d, 22)), 2, CREAM);
    }
}

// W4M HUD digits: "0-9 . m", '~' = infinity, ':' = two dots; h = cell height. Falls back to text().
static float digits(const char *s, float x, float y, float h, int align, bool grey = false, Color tint = WHITE) {  // returns the width
    static const char *ROW[2] = {"012345.", "6789~m"};
    static const short COL[2][7][2] = {{{6, 78}, {87, 131}, {142, 211}, {218, 290}, {300, 376}, {384, 455}, {463, 500}},
                                       {{6, 78}, {86, 149}, {158, 233}, {238, 312}, {319, 418}, {428, 508}}};
    Texture2D t = tex(grey ? "hud/hud_font_grey" : "hud/hud_font");
    if (!t.id) return text(s, x, y + h * 0.1f, h * 0.75f, grey ? tint : GOLDEN, align), textWidth(s, h * 0.75f);
    float k = h / 128, w = 0;
    for (int pass = 0; pass < 2; pass++) {
        float cx = pass ? x - w * align / 2 : 0;
        for (const char *p = s; *p; p++)
            for (int r = 0; r < 2; r++) {
                const char *f = strchr(ROW[r], *p == ':' ? '.' : *p);
                if (!f) continue;
                float x0 = COL[r][f - ROW[r]][0], gw = COL[r][f - ROW[r]][1] - x0;
                Rectangle src = {x0, r * 128.0f, gw, 128};
                if (pass) DrawTexturePro(t, src, {cx, y, gw * k, h}, {}, 0, tint);
                if (pass && *p == ':') DrawTexturePro(t, src, {cx, y - h * 0.36f, gw * k, h}, {}, 0, tint);
                cx += gw * k * 0.9f;
            }
        if (!pass) w = cx;
    }
    return w;
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

// W4M Bomber / Targeting cursor (Bundl09 meshes and clips) at the screen centre, in HUD units: screen height / 480
// (HUDTWK places the HUD in that space, e.g. AngleMeter.ScreenY -165); the cursor honours the HUD's hide flag.
void targetCursor(const WeaponDef &wd, int state, const Vector2 *lock, const Vector2 *at) {
    static double last = -1, start = 0, lockLast = -1, lockStart = 0;
    double now = GetTime();
    if (state >= 0 && now - last > 0.2) start = now;  // Show: the intro from t = 0
    if (lock && now - lockLast > 0.2) lockStart = now;  // HUD.Target.Selected: Lock_Outer from t = 0
    if (state >= 0) last = now;
    if (lock) lockLast = now;
    float t = float(now - start), u = GetScreenHeight() / 480.0f;
    Vector2 c = at ? *at : Vector2{GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    Color tint = state == 1 ? Color{98, 168, 255, 255} : state == 2 ? Color{215, 32, 0, 255} : WHITE;  // 0x552340: valid, water, none
    auto keys = [&](std::initializer_list<Vector2> k) {  // (time, value) keys, linear, held past both ends
        Vector2 a = *k.begin();
        for (Vector2 b : k) { if (t <= b.x) return b.x > a.x ? Lerp(a.y, b.y, (t - a.x) / (b.x - a.x)) : b.y; a = b; }
        return a.y;
    };
    auto quad = [&](const char *name, Vector2 at, float sx, float sy, float rad, Color col) {  // rad: mesh z rotation, counter-clockwise
        Texture2D x = tex(std::string("hud/") + name);
        if (x.id) DrawTexturePro(x, {0, 0, (float)x.width, (float)x.height}, {at.x, at.y, sx * u, sy * u}, {sx * u / 2, sy * u / 2}, -rad * RAD2DEG, col);
    };
    if (wd.kind == Kind::Homing) {  // Homing.Cursor.Mesh (4 brush ticks) inside Homing.Cursor.SquareMesh (4 corners, HomingLockOnGraphicEntity)
        Texture2D in = tex("fe2/homing_inner");
        float p = keys({{0, 84.5f}, {1.25f, 65}});  // Intro_Inner 1.25 s, then Loop_Inner / Loop_Outer 2.08 s
        if (t >= 1.25f) t = 1.25f + fmodf(t - 1.25f, 2.082f);
        float s = t < 1.25f ? keys({{0, 6.043f}, {1.166f, 0.8f}}) : keys({{1.25f, 0.8f}, {2.5f, 0.757f}, {3.332f, 0.8f}});
        for (int i = 0; state >= 0 && in.id && i < 4; i++) {  // Inner_01 top, 04 bottom (18 x 54 units), 02 left, 03 right (54 x 18); one texture row each
            const Vector2 D[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
            const int ROW[4] = {3, 2, 1, 0};
            Rectangle src = {0, ROW[i] * in.height / 4.0f, (float)in.width, in.height / 4.0f};
            Vector2 at = {c.x + D[i].x * p * s * u, c.y + D[i].y * p * s * u};
            DrawTexturePro(in, src, {at.x, at.y, 54 * s * u, 18 * s * u}, {27 * s * u, 9 * s * u}, i < 2 ? -90 : 0, WHITE);  // the Inner mesh is never tinted
        }
        auto corners = [&](Vector2 o, float x, float y, float k, Color col) {  // locators 1-4 at (-+x, +-y); bitmaps 128 units x k
            const char *N[4] = {"homing_tl", "homing_tr", "homing_bl", "homing_br"};
            for (int i = 0; i < 4; i++) quad(N[i], {o.x + (i % 2 ? x : -x) * u, o.y + (i / 2 ? y : -y) * u}, 128 * k, 128 * k, 0, col);
        };
        float o = t < 1.25f ? keys({{0, 7.258f}, {0.375f, 0.752f}, {1.25f, 0.8f}}) : keys({{1.25f, 0.8f}, {2.125f, 0.747f}, {3.332f, 0.8f}});  // Intro / Loop_Outer
        if (state >= 0 && !lock) corners(c, 50 * o, 50 * o, 0.8f * o, tint);
        if (lock) {  // Lock_Outer 0.625 s (OnTargetSelected 0x560481, added over Loop_Outer): it keys the locators only, so Outer keeps Loop's scale
            t = float(now - lockStart);
            float x = keys({{0, 50}, {0.1666f, 9.55f}, {0.2083f, 16.45f}, {0.25f, 12.33f}, {0.2915f, 15.6f}, {0.3333f, 14.78f}});
            float y = keys({{0, 50}, {0.1666f, 9.18f}, {0.2083f, 18.27f}, {0.25f, 11.48f}, {0.2915f, 14.66f}, {0.3333f, 14.5f}});
            corners(*lock, o * x, o * y, o * keys({{0, 0.8f}, {0.625f, 0.6f}}), WHITE);
        }
    } else if (state < 0) {
    } else if (wd.kind == Kind::Airstrike || wd.name == "Fatkins Strike") {  // Airstrike.Cursor.Mesh: 58-unit aimer, 5 x 16-unit arrows along the run
        float sy = keys({{0, 0}, {0.25f, 0.917f}, {0.333f, 0.789f}, {0.54f, 0.777f}, {0.667f, 0.8f}}), sx = keys({{0.4165f, 0.789f}, {0.54f, 0.777f}, {0.667f, 0.8f}});
        if (t > 0.667f) sx = sy = 0.7885f + 0.0115f * cosf(2 * PI * (t - 0.667f) / 0.833f);  // Cursor_Loop
        quad("airstrike_outer", c, 58 * sx, 58 * sy, keys({{0, 1.57f}, {0.29f, -0.186f}, {0.5f, 0.057f}, {0.667f, 0}}), tint);
        float run = t < 0.667f ? t / 0.667f : fmodf((t - 0.667f) / 0.4165f, 1);  // Airstrike_Intro, then Dots_Loop: one 30-unit step
        for (int i = 0; i < 5; i++) {
            float s = t < 0.667f ? 0.8f * Clamp((t - 0.042f - 0.083f * i) / 0.125f, 0, 1) * (i == 4 ? Clamp((0.667f - t) / 0.167f, 0, 1) : 1)
                                 : i == 0 ? 0.8f * run : i == 4 ? 0.8f * (1 - run) : 0.8f;
            quad("dot", {c.x + (-76 + 30.4f * (i + run)) * u, c.y}, 16 * s, 16 * s, 0, tint);
        }
    } else {  // Targeting.Cursor.Mesh: 100 units, scale 0.75, spins in, then a turn per 3.33 s; Targeting.Cursor.Shadow at (2, -2)
        float s = keys({{0, 0}, {0.4165f, 0.75f}}), rot = t < 0.4165f ? keys({{0, -4.363f}, {0.4165f, 0}}) : 2 * PI * fmodf((t - 0.4165f) / 3.332f, 1);
        quad("target", {c.x + 2 * u, c.y + 2 * u}, 100 * s, 100 * s, rot, {2, 43, 94, 100});  // tint 0x645e2b02
        quad("target", c, 100 * s, 100 * s, rot, tint);
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
        bool util = utility(k);
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

// W4M commentary banner (com_panel), queued; crateFocus: seconds the camera follows a dropped crate
static std::vector<std::string> banners;
static float bannerAge = 0, crateFocus = 0;
static uint32_t bannerTick = 0;
static std::vector<uint8_t> announced;  // per worm death, per team wipe (replays re-emit events)

// Random W4M line "Comment.<cat>.<n>" with %s = name
static void comment(const char *cat, const char *name) {
    std::vector<const char *> lines;
    for (int i = 1; i <= 60; i++)
        if (const char *l = tr(TextFormat("Comment.%s.%d", cat, i), nullptr); l && *l) lines.push_back(l);
    if (lines.empty()) return;
    std::string s = lines[GetRandomValue(0, (int)lines.size() - 1)];
    if (size_t at = s.find("%s"); at != std::string::npos) s.replace(at, 2, name);
    banners.push_back(s);
}

void hudEvent(const Game &g, const GameEvent &e) {
    announced.resize(g.worms.size() + g.teams);
    if (e.kind == GameEvent::Death && e.worm >= 0 && !announced[e.worm]) {
        const Worm &w = g.worms[e.worm];
        announced[e.worm] = 1;
        comment(g.drowned(e.worm) ? "WaterDeath" : "LandDeath", wormName(w.team, e.worm % std::max(1, g.perTeam)));
        bool left = false;
        for (const Worm &x : g.worms) left |= x.team == w.team && x.alive;
        if (!left && !announced[g.worms.size() + w.team]) announced[g.worms.size() + w.team] = 1, comment("TeamDeath", teamName(g.cfg, w.team).c_str());
    } else if (e.kind == GameEvent::CrateDrop && !g.objects.empty()) {
        int wi = g.objects.back().weapon;
        Kind k = wi >= 0 && wi < (int)WEAPONS.size() ? WEAPONS[wi].kind : Kind::Shell;
        bool util = utility(k);
        comment(wi < 0 ? "Health" : util ? "Utility" : "Crate", "");
        crateFocus = 30;  // until it lands: the sim holds the turn meanwhile
    } else if (e.kind == GameEvent::Collect && e.worm >= 0) {
        const Worm &w = g.worms[e.worm];
        const char *who = wormName(w.team, e.worm % std::max(1, g.perTeam));
        if (e.weapon < 0) banners.push_back(TextFormat("%s : +%d", who, (int)g.cfg.scheme.crateHealth));
        else if (e.weapon < (int)WEAPONS.size()) {
            banners.push_back(TextFormat("%s : %s", who, weaponName(WEAPONS[e.weapon])));
        }
    }
}

static void drawBanner(float dt) {
    if (banners.empty()) return;
    const float LIFE = banners.size() > 1 ? 2 : 3, SIZE = 26, MAXW = 680;  // queued: keeps pace with the death queue (2 s a worm)
    if ((bannerAge += dt) > LIFE) { banners.erase(banners.begin()), bannerAge = 0; return; }
    std::vector<std::string> lines(1);  // word wrap
    for (size_t i = 0, j; i < banners[0].size(); i = j + 1) {
        j = std::min(banners[0].find(' ', i), banners[0].size());
        std::string word = banners[0].substr(i, j - i), line = lines.back().empty() ? word : lines.back() + " " + word;
        if (textWidth(line.c_str(), SIZE) > MAXW && !lines.back().empty()) lines.push_back(word);
        else lines.back() = line;
    }
    float w = 0;
    for (const std::string &l : lines) w = fmaxf(w, textWidth(l.c_str(), SIZE));
    w += 80;
    float h = 44 + 30 * lines.size(), y = 66 - (h + 60) * (fmaxf(0, 1 - bannerAge / 0.2f) + fmaxf(0, (bannerAge - LIFE + 0.2f) / 0.2f));
    Rectangle r = {640 - w / 2, y, w, h};
    Texture2D p = tex("fe/com_panel");
    if (p.id) {  // the sheet holds a left-capped (top) and a right-capped (bottom) half: W4M joins them
        float half = floorf(r.width / 2);  // seam at sheet column 120, where both halves' edges line up
        DrawTextureNPatch(p, {{2, 2, 118, 124}, 26, 26, 0, 26, NPATCH_NINE_PATCH}, {r.x, r.y, half, h}, {}, 0, WHITE);
        DrawTextureNPatch(p, {{120, 130, 131, 124}, 0, 26, 26, 26, NPATCH_NINE_PATCH}, {r.x + half, r.y, r.width - half, h}, {}, 0, WHITE);
    } else DrawRectangleRounded(r, 0.3f, 6, {0, 104, 138, 230});
    for (size_t i = 0; i < lines.size(); i++) text(lines[i].c_str(), 640, y + 20 + 30 * i, SIZE, WHITE, 1);
}

bool Hud::trackHp(const Game &g, bool turnStart, uint32_t tick) {
    float dt = fminf((tick - hpTick) * Game::DT, 0.1f);  // sim time: the count stops with the pause menu
    if (hpt.size() != g.worms.size() || tick < hpTick) announced.assign(g.worms.size() + g.teams, 0), banners.clear(), crateFocus = 0, bannerTick = tick;  // new match
    if (hpt.size() != g.worms.size() || tick < hpTick || g.clock < hpClock) {  // new match, replay seek or instant replay
        hpt.assign(g.worms.size(), {});
        for (size_t i = 0; i < hpt.size(); i++) hpt[i].seen = g.worms[i].counted, hpt[i].shown = hpt[i].seen;
        popups.clear(), order.clear(), counting = -1;
    }
    hpTick = tick, hpClock = g.clock;
    for (size_t i = 0; i < hpt.size(); i++) {  // labels follow the sim's counted hp; out of Settle only poison moves it
        HpTrack &t = hpt[i];
        const Worm &w = g.worms[i];
        if (w.counted == t.seen) continue;
        if (w.counted > t.seen && g.phase != Phase::Settle) {  // health crate: +N on the spot, no count
            popups.push_back({(int)i, w.counted - t.seen, 0, 0, false, false});
            t.seen = w.counted, t.shown = t.from = (float)w.counted;
            continue;
        }
        if (!w.alive) { t.shown = t.seen = w.counted; continue; }  // drowned: no count (W4M)
        t.poison = turnStart && w.poison && w.counted < t.seen, t.seen = w.counted;
        if (std::find(order.begin(), order.end(), (int)i) == order.end()) order.push_back((int)i);
    }
    // big W4M damage counters: follow their counting label, then pop and fade
    for (Popup &p : popups) p.punch += dt, p.age += p.live ? 0 : dt;
    popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup &p) { return p.age > 1.0f; }), popups.end());
    auto bump = [&](int i) {
        int a = (int)lroundf(hpt[i].shown) - (int)lroundf(hpt[i].from);
        auto p = std::find_if(popups.begin(), popups.end(), [&](const Popup &q) { return q.worm == i && q.live; });
        if (p == popups.end() && a) popups.push_back({i, a, 0, 0, hpt[i].poison, true});
        else if (p != popups.end() && p->amount != a) p->amount = a, p->punch = 0;
    };
    const std::vector<int> &grp = g.phase == Phase::Settle ? g.countGroup : std::vector<int>{};
    for (Popup &p : popups) p.live &= p.worm == counting || std::count(grp.begin(), grp.end(), p.worm);
    tickGap -= dt;
    if (!grp.empty()) {  // the sim times this count: same on every client
        bool ticked = false;
        for (int i : grp) {
            const Worm &w = g.worms[i];
            HpTrack &t = hpt[i];
            long before = lroundf(t.shown);
            float k = Clamp(g.countT / (float)g.countTicks(i), 0, 1);
            if (w.alive) t.from = w.counted, t.shown = w.counted + (std::max(0, w.hp) - w.counted) * k, t.poison = false, bump(i);
            ticked |= lroundf(t.shown) != before;
        }
        if (ticked && tickGap <= 0) Audio::play(Audio::Sfx::HpTick), tickGap = 0.06f;
        const Worm &fw = g.worms[g.countFocus()];
        Vector3 c = {fw.pos.x, fmaxf(fw.pos.y, g.water) + 1.2f, fw.pos.z};
        float r = 0;
        if (int d = g.dying(); d >= 0) c = {g.worms[d].pos.x, fmaxf(g.worms[d].pos.y, g.water) + 0.6f, g.worms[d].pos.z}, r = fmaxf(r, 2);  // each blast, at the count's distance
        Controls::focus(&c, r);
        counting = -1;
        return true;
    }
    auto pending = [&](int i) { return hpt[i].shown != hpt[i].seen; };
    bool live = g.phase == Phase::Aim && !g.hotSeat, chain = false;  // live: the next turn's clock runs
    wait -= dt;
    if (counting >= 0 && !pending(counting) && wait <= 0) counting = -1, chain = true;
    if (counting < 0 && (!live || chain)) {  // poison ticks, in hit order
        order.erase(std::remove_if(order.begin(), order.end(), [&](int i) { return hpt[i].shown == hpt[i].seen; }), order.end());
        for (int i : order) if (pending(i)) { counting = i; break; }
        if (counting >= 0) hpt[counting].from = hpt[counting].shown, wait = 0.7f;  // camera travel
    }
    // A, or a local human's turn going live: no more camera, labels jump to their values (CPU/remote turns let it finish)
    bool skip = skipHp || (live && (mine || counting < 0)) || ((counting >= 0 || crateFocus > 0) && pressed(-1, {GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {KEY_SPACE}));
    if (skip) {
        skipHp = false;
        for (HpTrack &t : hpt) t.shown = t.seen;
        counting = -1, crateFocus = 0;
    }
    if (counting < 0) {
        for (Popup &p : popups) p.live = false;
        static size_t landed = 0;  // last spawned crate: stays framed through the PostActivityTime after it rests
        const Object *crate = nullptr;
        for (size_t i = 0; i < g.objects.size(); i++) if (g.objects[i].type == Object::Crate && g.objects[i].spawning) crate = &g.objects[i], landed = i;
        bool post = g.phase == Phase::Settle && g.crated && g.timer < 0;  // W4M: the PostActivityTime after the crate rests
        if (!crate && post && landed < g.objects.size() && g.objects[landed].type == Object::Crate) crate = &g.objects[landed];
        crateFocus = crate ? crateFocus - dt : 0;
        Controls::focus(crateFocus > 0 ? &crate->pos : nullptr, 0, true);
        return crateFocus > 0;
    }
    HpTrack &t = hpt[counting];
    Vector3 at = Vector3Add(g.worms[counting].pos, {0, 0.6f, 0});
    Controls::focus(&at);
    if (wait > 0 || !pending(counting)) return true;
    float d = t.seen - t.shown, step = fmaxf(40, fabsf(t.seen - t.from) / 1.5f) * dt;  // 40 hp/s, at most 1.5 s per worm
    long before = lroundf(t.shown);
    t.shown = fabsf(d) <= step ? t.seen : t.shown + copysignf(step, d);
    bump(counting);
    if (t.shown == t.seen) wait = 0.5f;  // linger on the final value
    if (lroundf(t.shown) != before && tickGap <= 0) Audio::play(Audio::Sfx::HpTick), tickGap = 0.06f;
    return true;
}

// An open panel is always drawn: the ready screen yields to it.
bool Hud::readyScreen(const Game &g, bool cinematic) const { return mine && g.phase == Phase::Aim && g.hotSeat > 0 && !cinematic && !open; }

void Hud::draw(const Game &g, const Camera3D &cam, uint32_t tick) {
    menuPage = false;
    const Worm &cur = g.worms[g.current];
    bool turnStart = g.current != introWorm;
    if (turnStart) introWorm = g.current, introStart = tick;  // turn changed: (re)start the name-banner clock
    bool cinematic = trackHp(g, turnStart, tick);
    bool ready = readyScreen(g, cinematic);  // local human's hot seat: W4M full-screen ready pause
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    if (!ready && cur.team < (int)g.spy.size() && g.spy[cur.team])  // W4M Crate Spy (CrateGraphicEntity 0x5c5270): contents over every crate
        for (const Object &o : g.objects) {
            Vector3 top = Vector3Add(o.pos, {0, 0.9f, 0});
            float dist = Vector3DotProduct(Vector3Subtract(top, cam.position), fwd);
            if (o.type != Object::Crate || dist < 0.5f) continue;
            Vector2 sp = GetWorldToScreen(top, cam);
            text3d(o.weapon < 0 ? tr("Text.Health", "Health", "Santé") : WEAPONS[o.weapon].name.c_str(), sp.x, sp.y, Clamp(170 / dist, 12, 22), WHITE);  // CrateGraphicEntity's Text3D
        }
    Vector3 camUp = Vector3Normalize(Vector3CrossProduct(Vector3CrossProduct(fwd, cam.up), fwd));
    if (!ready) for (const Projectile &s : g.shots) {  // W4M 0x57b1e0: ceil(fuse left) in FE.Font, white, while 0 < left <= 5 s
        const WeaponDef &d = WEAPONS[s.weapon];
        Vector3 top = Vector3Add(s.pos, Vector3Scale(camUp, d.fuseHeight));  // the offset along the view's up (0x47a120)
        float dist = Vector3DotProduct(Vector3Subtract(top, cam.position), fwd), u = dist * 20;
        if (!d.fuseShown || s.child || s.fuse <= 0 || s.fuse > 5 || dist < 0.5f) continue;
        float k = u < 80 ? 1 : u <= 200 ? u / 80 : u / 200;  // Text3D 0x5fad80: HUD.3DText.MinScalingDist 80, MaxScalingDist 200
        Vector2 sp = GetWorldToScreen(top, cam), sp2 = GetWorldToScreen(Vector3Add(top, Vector3Scale(camUp, d.fuseSize * k)), cam);
        text3d(TextFormat("%d", (int)ceilf(s.fuse - 0.001f)), sp.x, sp.y, Vector2Distance(sp, sp2), WHITE);
    }
    // W4M worm labels: name over hp, team colour, on a Text.Backing; hidden on the ready screen, with the weapon panel open or a UFO out (0x5fd4e0)
    if (!ready && !open && !g.abducting()) for (const Worm &w : g.worms) {
        int i = int(&w - g.worms.data()), k = i % std::max(1, g.perTeam), hp = (int)lroundf(hpt[i].shown);
        if (!w.alive) continue;  // blown up, or drowned: W4M shows no label afloat
        Vector3 top = Vector3Add(w.pos, {0, 1.1f, 0});
        float dist = Vector3DotProduct(Vector3Subtract(top, cam.position), fwd);
        if (dist < 0.5f || (fp && &w == &cur) || Vector3Distance(w.pos, cam.position) < 1.2f) continue;  // first person: inside it
        Vector2 sp = GetWorldToScreen(top, cam);
        if (pipShow > 0) {  // not over the PiP
            Vector2 pc, ph;
            float rot;
            pipPlace(pipShow, pipFull, pc, ph, rot);
            if (fabsf(sp.x - pc.x) < ph.x + 30 && fabsf(sp.y - pc.y) < ph.y + 30) continue;
        }
        float s = Clamp(170 / dist, 12, 24);
        Color c = TEAM_COLORS[w.team % 4];
        const Color POISON = {120, 220, 60, 255};
        text3d(TextFormat("%d", hp), sp.x, sp.y - s / 2, s, i == counting && hpt[i].poison ? POISON : c);  // WormHealthNameEntity 0x5fdb70: Text3Ds
        text3d(wormName(w.team, k), sp.x, sp.y - s * 1.5f, s, c);
        if (&w == &cur && g.jetting) text3d(TextFormat("%d", (int)(g.fuel * 2 + 0.5f)), sp.x, sp.y - s * 3.2f + s * 0.65f, s * 1.3f, WHITE);  // JetpackUtility's Text3D  // W4M 0x5626e0: (2 ms + 500) / 1000
        for (const Popup &p : popups) {  // W4M damage counter: big cream hud digits, grows as it counts, pops on each step
            if (p.worm != i) continue;
            float a = Clamp(1 - (p.age - 0.5f) / 0.5f, 0, 1), pop = 1 + 0.25f * fmaxf(0, 1 - p.punch / 0.08f) + 0.3f * sinf(fminf(p.age / 0.25f, 1) * PI);
            float h = fmaxf(36, s * 2.6f) * (1 + fminf(abs(p.amount), 100) * 0.005f) * pop, bw = h * 0.3f;
            Color pc = p.amount > 0 || p.poison ? Color{150, 235, 90, 255} : Color{255, 244, 228, 255};
            const char *n = TextFormat("%d", abs(p.amount));
            float y = sp.y - s * 2.4f - h - fmaxf(0, p.age - 0.25f) * 60;
            float x0 = sp.x + bw * 0.6f - digits(n, sp.x + bw * 0.6f, y, h, 1, true, Fade(pc, a)) / 2;
            Rectangle bar = {x0 - bw * 1.25f, y + h * 0.42f, bw, h * 0.14f};  // the font has no sign glyphs
            for (int k = 0; k < (p.amount > 0 ? 2 : 1); k++) {
                Rectangle r = k ? Rectangle{bar.x + bw / 2 - bar.height / 2, bar.y - bw / 2 + bar.height / 2, bar.height, bw} : bar;
                DrawRectangleRec({r.x + h * 0.05f, r.y + h * 0.05f, r.width, r.height}, Fade({20, 30, 60, 255}, a));
                DrawRectangleRec(r, Fade(pc, a));
            }
        }
        if (&w == &cur && g.phase == Phase::Aim && !g.jetting) {  // bobbing "this one" arrow
            float b = sinf(tick * 0.12f) * 4;
            if (!sprite("wormlocarrow", {sp.x, sp.y - s * 2 - 18 + b}, 0.22f, {64, 120}, 0, c)) DrawTriangle({sp.x - 8, sp.y - s * 2 - 30 + b}, {sp.x, sp.y - s * 2 - 18 + b}, {sp.x + 8, sp.y - s * 2 - 30 + b}, c);
        }
    }
    if (!quiet) drawBanner(fminf((tick - bannerTick) * Game::DT, 0.1f));
    bannerTick = tick;
    if (g.phase == Phase::GameOver) {
        if (g.winner >= 0) text(TextFormat("%s WINS!", teamName(g.cfg, g.winner).c_str()), 640, 260, 70, TEAM_COLORS[g.winner % 4], 1);
        else text("DRAW!", 640, 260, 70, WHITE, 1);
        if (!g.cfg.mission && !quiet) hints({{"A", "Space", "Continue"}});  // missionEnd() has its own
        return;
    }
    const WeaponDef &wd = WEAPONS[g.weapon];
    Color tc = TEAM_COLORS[cur.team % 4];
    bool aiming = g.phase == Phase::Aim;
    if (ready) {  // W4M ready screen: big worm + team name, "Ready?" + countdown, rest of the HUD hidden
        const Color CREAM = {255, 244, 228, 255};
        text(wormName(cur.team, g.current % std::max(1, g.perTeam)), 640, 64, 64, tc, 1);
        text(teamName(g.cfg, cur.team).c_str(), 640, 136, 38, CREAM, 1);
        text("Ready?", 640, 560, 32, CREAM, 1);
        text(TextFormat("%d", (g.hotSeat + 59) / 60), 640, 596, 110, tc, 1);
        return;
    }
    // turn start banner: team/worm name, also briefly for CPU/remote turns that skip the ready screen above
    if (tick - introStart < 90 || (aiming && g.timer > std::max(1, (int)g.cfg.scheme.turnTime) * 60 - 120))
        text(TextFormat("%s - %s", teamName(g.cfg, cur.team).c_str(), wormName(cur.team, g.current % std::max(1, g.perTeam))), 640, 14, 28, tc, 1);
    if (g.cfg.rules & RULE_ROPE_RACE) text(TextFormat("Race %ds", tick / 60), 640, 80, 26, GOLDEN, 1);
    if (g.suddenDeath && banners.empty()) text("SUDDEN DEATH!", 640, 46, 26, GOLDEN, 1);  // crate drops: commentary banner

    radar(g, {124, 112}, Vector3Add(fwd, cam.up), aiming && mine);  // + up: a Blimp looking straight down keeps its heading
    // wind: arrow along the wind on screen, length by strength; distance to the aim target
    Vector2 wc = {58, 236};
    float wind = g.wind, ws = fabsf(wind);
    if (!sprite(ws < 0.01f ? "wind_backdisabled" : "wind_back", wc, 0.72f, {64, 64})) DrawCircleV(wc, 30, {0, 104, 138, 220});
    if (ws >= 0.01f) {
        Vector2 f = Vector2Normalize({fwd.x + cam.up.x, fwd.z + cam.up.z}), v = {-wind * f.y, -wind * f.x};
        float deg = atan2f(v.y, v.x) * RAD2DEG, l = 0.12f + 0.12f * Clamp(ws / 1.5f, 0, 1);
        if (!sprite("wormlocarrow", wc, l, {64, 64}, deg - 90, {255, 170, 30, 255})) DrawLineEx(wc, Vector2Add(wc, Vector2Scale(Vector2Normalize(v), 24)), 5, ORANGE);
    }
    if (aiming) digits(TextFormat("%02dm", (int)roundf(Vector3Distance(cur.pos, g.target()))), 96, 216, 40, 0);
    // current weapon (top right) + ammo
    int ammo = g.ammo[cur.team][g.weapon];
    Vector2 wp = {1176, 92 - (300 - 174) * 1.5f * pipShow * (1 - pipFull)};  // HUD.ActWormInfo.Pos -> PosPiP while the PiP shows
    if (!sprite("secondback", wp, 0.5f, {128, 128})) DrawCircleV(wp, 44, {0, 119, 155, 230});
    if (!image(iconOf(wd), {wp.x - 34, wp.y - 34, 68, 68}, ammo ? WHITE : GRAY)) text(wd.name.substr(0, 4).c_str(), wp.x, wp.y - 12, 22, WHITE, 1);
    digits(ammo < 0 ? "~" : TextFormat("%d", ammo), wp.x, wp.y + 44, 40, 1);
    text(weaponName(wd), wp.x - 54, wp.y - 10, 22, ammo ? WHITE : GRAY, 2);
    if (wd.userFuse) text(TextFormat("%s %ds", tr("FETXT.Fuse", "Fuse", "Mèche"), (int)g.fuseOf(wd)), wp.x - 54, wp.y + 16, 22, GOLDEN, 2);  // d-pad up/down
    if (g.secondary >= 0) {  // W4M SecondaryWeaponGraphicEntity 0x5f82f0: dynamite / landmine / sheep icon under the tool's
        const WeaponDef &sd = WEAPONS[g.secondary];
        int sa = g.ammo[cur.team][g.secondary];
        Vector2 sp2 = {wp.x, wp.y + 140};
        if (!sprite("secondback", sp2, 0.34f, {128, 128})) DrawCircleV(sp2, 30, {0, 119, 155, 230});
        if (!image(iconOf(sd), {sp2.x - 23, sp2.y - 23, 46, 46}, WHITE)) text(sd.name.substr(0, 4).c_str(), sp2.x, sp2.y - 10, 18, WHITE, 1);
        digits(sa < 0 ? "~" : TextFormat("%d", sa), sp2.x, sp2.y + 30, 28, 1);
        text(weaponName(sd), sp2.x - 40, sp2.y - 10, 20, WHITE, 2);
    }
    // turn timer (bottom right): turn seconds, round clock below
    bool retreat = g.phase == Phase::Flying || g.phase == Phase::Retreat;  // W4M: the retreat clock runs from the launch
    int left = aiming && g.hotSeat ? g.hotSeat : aiming ? g.timer : retreat ? std::min(g.timer, g.retreatTicks(WEAPONS[g.weapon])) : 0, secs = (left + 59) / 60;
    Vector2 tp = {1180, 612};
    bool urgent = (secs <= 5 && aiming && !g.hotSeat) || retreat;
    if (!sprite("timer_back", tp, 0.62f, {128, 128}, 0, urgent && tick / 15 % 2 ? Color{255, 120, 120, 255} : WHITE)) DrawCircleV(tp, 54, {0, 119, 155, 230});
    digits(TextFormat("%d", secs), tp.x, tp.y - 34, 56, 1, true);
    int round = std::max(0, g.cfg.scheme.roundTime * 3600 - g.clock) / 60;
    digits(TextFormat("%02d:%02d", round / 60, round % 60), tp.x, tp.y + 18, 26, 1, true);
    if (retreat || g.hotSeat) text(g.hotSeat ? "READY" : "RETREAT", tp.x, tp.y - 82, 22, GOLDEN, 1);
    // team health (bottom centre, above the hints)
    static const char *FLAGS[4] = {"flags/custom_cool", "flags/custom_police", "flags/custom_genie", "flags/custom_crown"};
    int maxHp = std::max(1, (int)g.cfg.scheme.health) * std::max(1, g.perTeam);
    for (int t = 0; t < g.teams; t++) {
        float hp = 0;
        for (size_t i = 0; i < g.worms.size(); i++) if (g.worms[i].team == t) hp += hpt[i].shown;  // shrinks with the count
        float y = 646 - (g.teams - 1 - t) * 38.0f;
        text(teamName(g.cfg, t).c_str(), 574, y - 1, 24, TEAM_COLORS[t % 4], 2);
        if (!image(FLAGS[t % 4], {584, y - 4, 32, 32})) DrawRectangleRounded({584, y - 4, 32, 32}, 0.2f, 4, TEAM_COLORS[t % 4]);
        healthBar(t, 628, y, 240, 24, Clamp(hp / maxHp, 0, 1));
    }
    // power (stacked blocks, fill from the bottom) and pitch arc (bottom left)
    Vector2 pb = {22, 520};
    float ps = 0.62f, pf = Clamp(g.power, 0, 1), aimAt = cur.pitch;
    if (g.scout.t >= 0 && g.scout.ok) {  // W4M Binoculars state 3: the bar sweeps 2.7 s, converges by 4 s, then SetPowerBar / SetAimAngle
        float t = g.scout.t / 60.0f, k = Clamp((t - 2.7f) / 1.3f, 0, 1), sweep = 0.5f + 0.5f * sinf(t * 6);
        pf = Lerp(sweep, g.scout.power, k), aimAt = Lerp(sinf(t * 4) * 0.8f, g.scout.pitch, k);
    }
    if (sprite("powerbar_off", pb, ps, {80, 30})) {
        float top = 215 - (215 - 41) * pf;
        if (pf > 0) sprite("powerbar_on", {pb.x, pb.y + (top - 30) * ps}, ps, {80, 0}, 0, WHITE, {0, top, 256, 256 - top});
    } else {
        DrawRectangleRounded({pb.x, pb.y, 40, 120}, 0.3f, 4, {0, 0, 0, 150});
        DrawRectangleRounded({pb.x + 4, pb.y + 4 + 112 * (1 - pf), 32, 112 * pf}, 0.3f, 4, ColorLerp(YELLOW, RED, pf));
    }
    Vector2 ap = {102, 590};  // arc pivot: flat edge centre
    float deg = -aimAt * RAD2DEG;
    if (sprite("angle_back", ap, 0.6f, {70, 130})) sprite("angle_head", ap, 0.55f, {16, 32}, deg);
    else {
        DrawCircleSector(ap, 58, -90, 90, 16, {0, 104, 138, 210});
        DrawLineEx(ap, {ap.x + cosf(-aimAt) * 56, ap.y + sinf(-aimAt) * 56}, 4, GOLDEN);
        DrawCircleV(ap, 7, MAROON);
    }
    // state hints
    if (g.shotsLeft) text(TextFormat("%d shot(s) left", g.shotsLeft), 190, 600, 22, WHITE);
    if (g.roped) text("Rope: stick swings, aim = length, jump releases", 190, 600, 22, WHITE);
    if (g.jetting) {
        text(keyGlyphs() ? "Jetpack: Space thrust, arrows steer, Backspace drop" : "Jetpack: A/ZR thrust, stick steers, ZL drop", 190, 600, 22, WHITE);  // HelpText.kUtilityJetpack0
        float full = 0.01f;  // the hand may hold what it drops
        for (const WeaponDef &d : WEAPONS) if (d.kind == Kind::Jetpack) full = fmaxf(full, d.fuse);
        healthBar(1, 190, 630, 240, 16, Clamp(g.fuel / full, 0, 1));
    }
    if (open) hints({{"D-pad", "Up/Down/Left/Right", "Move"}, {"A", "Enter", "Select"}, {"B/X", "Backspace/Q", "Close"}});
    else if (mine && !quiet && g.girderOn && g.phase == Phase::Aim)  // W4M HelpText.kUtilityGirder0: Movement, GirderRaise / Lower, Fire
        hints({{"LS", "Arrows", "Move"}, {"RS", "WASD", "Raise / lower, turn"}, {"A", "Space", "Place"}});
    else if (mine && !quiet && wd.kind == Kind::Binoculars && g.phase == Phase::Aim)  // HelpText.kUtilityBinoculars0
        hints({{"ZL", "RMB", "Look"}, {"A", "Space", "Select a target"}});
    else if (mine && !quiet && g.secondary >= 0)  // W4M SecondaryWeaponHelpEntity: WXFE.HelpDropConsole, FETXT.Control.Secondry + FETXT.Drop
        hints({{g.jetting || g.jetLanded() ? "ZL" : "A", g.jetting || g.jetLanded() ? "Backspace" : "Space", tr("FETXT.Drop", "Drop", "Lâcher")}});
    else if (mine && !quiet && Controls::targetView(g))  // W4M BlimpHelpEntity (WXFE.HelpBlimpConsole): Look, Pan, Zoom in / out
        hints({{"A", "Space", "Fire"}, {"LS", "Arrows", "Pan"}, {"RS", "WASD", "Look"},
               {"Up/Down", "Z/X", "Zoom"}, {"B", "Enter/E", "Leave"}});
    else if (mine && !quiet && g.phase == Phase::Aim && (Controls::firstPerson(g) || Controls::scoped(g)))  // HeadCam: FETXT.Control.ZoomIn / ZoomOut
        hints({{"A", "Space", "Fire"}, {"Up/Down", "Wheel", "Zoom"}});
    else if (mine && !quiet && Controls::targetHeld(g)) hints({{"A", "Space/E", "Sky view: target"}, {"L", nullptr, "Hold: sky view"}});  // "Define the path using [Blimp]"
    else if (tick < 300 && !quiet) hints({{"-", "F1", "Hold: controls"}});
    if (!open) return;

    // weapon panel
    std::vector<int> slots = panelSlots();
    int n = (int)slots.size(), cols = PANEL_COLS, rows = (n + cols - 1) / cols;
    float cell = 92, pw = cols * cell + 60, ph = rows * cell + 150;
    Rectangle pr = {640 - pw / 2, 360 - ph / 2, pw, ph};
    popup(pr);
    text("WEAPONS", 640, pr.y + 24, 40, GOLDEN, 1);
    for (int k = 0; k < n; k++) {
        int i = slots[k];
        Rectangle c = {pr.x + 30 + (k % cols) * cell, pr.y + 80 + (k / cols) * cell, cell - 8, cell - 8};
        int a = g.ammo[cur.team][i], late = g.delays[cur.team][i];
        if (late) a = 0;  // W4M FETXT.HTPSubtopic4: a delayed weapon is dimmed, its number the turns left
        bool lit = a && g.pickable(cur.team, i);
        if (i == cursor && !nine("fe/buttonbig_highlight", c, 64, 0.25f)) DrawRectangleRoundedLinesEx(c, 0.2f, 4, 4, GOLDEN);
        Rectangle ic = {c.x + 8, c.y + 8, c.width - 16, c.height - 16};
        if (!image(iconOf(WEAPONS[i]), ic, lit ? WHITE : Fade(GRAY, 0.5f))) {
            DrawRectangleRounded(ic, 0.2f, 4, lit ? PANEL : Fade(PANEL, 0.4f));
            text(WEAPONS[i].name.substr(0, 4).c_str(), ic.x + ic.width / 2, ic.y + ic.height / 2 - 10, 20, lit ? WHITE : GRAY, 1);
        }
        if (a > 0) text(TextFormat("%d", a), c.x + c.width - 6, c.y + c.height - 26, 22, WHITE, 2);
        if (late) text(TextFormat("%d", late), c.x + c.width / 2, c.y + c.height / 2 - 20, 40, GOLDEN, 1);
    }
    const WeaponDef &sel = WEAPONS[cursor];
    int sa = g.ammo[cur.team][cursor];
    text(TextFormat("%s  %s", weaponName(sel), sa < 0 ? "(infinite)" : TextFormat("x%d", sa)), 640, pr.y + ph - 58, 30, sa ? WHITE : GRAY, 1);
}

// ---------------------------------------------------------------- pause

Pause::Action Pause::update() {
    bool plus = P({PLUS}, {KEY_ESCAPE, KEY_P});
    if (!open) {
        if (plus) open = true, help = false, row = 0, Audio::play(Audio::Sfx::FePopupIn);
        return None;
    }
    bool ok = P({A}, {KEY_ENTER, KEY_SPACE}), back = plus || P({B}, {KEY_BACKSPACE});
    if (help) {
        if (ok || back) help = false, Audio::play(Audio::Sfx::FePrevIn);
        return None;
    }
    row = clampWrap(row + P({DOWN}, {}) - P({UP}, {}), 3);
    if (back || (ok && row == 0)) open = false, Audio::play(Audio::Sfx::FePopupOut);
    if (ok && row == 1) help = true, Audio::play(Audio::Sfx::FeController);
    if (ok && row == 2) { open = false; return Quit; }
    return None;
}

void Pause::draw(bool online) const {
    menuPage = true;
    if (!open) return;
    if (help) {
        controls(true);
        hints({{"B", "Esc", "Back"}});
        return;
    }
    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 140});
    popup({420, 150, 440, 400});
    heading("Pause", 640, 172, 60);
    const char *items[] = {"Resume", "Help & options", online ? "Leave match" : "Quit"};
    for (int i = 0; i < 3; i++) item(items[i], 640, 285 + i * 72.0f, 42, i == row);
    if (online) text("The match keeps running", 640, 505, 20, CREAM, 1);
    hints({{"A", "Enter", "Select"}, {"B/+", "Esc", "Resume"}});
}

// ---------------------------------------------------------------- replays

int replayList(const std::vector<std::string> &files, int &sel, bool &instant) {
    menuPage = true;
    int n = (int)files.size(), first = std::max(0, std::min(sel - 4, n - 9));
    sel = clampWrap(sel + P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP}), n);
    if (P({X}, {KEY_Y})) instant = !instant;
    heading("Replays", 640, 30, 60);
    for (int i = first; i < n && i < first + 9; i++) {
        Rectangle r = {290, 130 + (i - first) * 52.0f, 700, 46};
        panel(r, i == sel);
        text(files[i].substr(0, files[i].size() - 4).c_str(), r.x + 20, r.y + 9, 28, ink(i == sel));
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
    if (fmodf(now(), 1) < 0.7f) text("REPLAY", 640, 100, 44, GOLDEN, 1);
    const char *skip = keyGlyphs() ? tr(nullptr, "- Press Enter to skip -", "- Appuyer sur Entrée pour passer -") : tr(nullptr, "- Press A to skip -", "- Appuyer sur A pour passer -");
    text(skip, 640, 56, 30, Fade(CREAM, 0.8f + 0.2f * sinf(now() * 4)), 1);
}

void lanGames(const std::vector<LanGame> &games, int sel, const std::string &status) {
    heading("LAN games", 640, 30, 60);
    for (size_t i = 0; i < games.size() && i < 6; i++) {
        const LanGame &g = games[i];
        Rectangle r = {290, 150 + i * 70.0f, 700, 60};
        panel(r, (int)i == sel);
        text(g.name.c_str(), r.x + 24, r.y + 14, 30, ink((int)i == sel));
        text(TextFormat("%s  %d/%d%s", g.ip.c_str(), g.players, g.maxPlayers, g.started ? "  playing" : ""), r.x + r.width - 24, r.y + 18, 24, LIGHTGRAY, 2);
    }
    if (games.empty()) text("Looking for games on this network...", 640, 220, 30, LIGHTGRAY, 1);
    text(status.c_str(), 640, 620, 22, ORANGE, 1);
    hints({{"A", "Enter", "Join"}, {"X", "C", "Host game"}, {"B", "Esc", "Back"}});
}

void room(const Net &net, const GameConfig &opt, bool lan, const std::string &status) {
    bool isHost = net.hostId == net.id;
    heading(lan ? "LAN game" : "Room", 640, 30, 60);
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
    menuPage = true;
    static const char *TABS[2] = {"Missions", "Challenges"};
    static double seen = -1;  // last frame shown: a gap means the list just opened (W4M's story book)
    if (GetTime() - seen > 0.5) Audio::play(Audio::Sfx::FeBookIn);
    seen = GetTime();
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
        if (back) st.brief = false, Audio::play(Audio::Sfx::FePage);
        return ok ? pick : -1;
    }
    int tab = st.tab;
    st.tab = clampWrap(st.tab + P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT}), 2);
    if (st.tab != tab) Audio::play(Audio::Sfx::FePage);
    if (n) sel = clampWrap(sel + P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP}), n);
    heading("Single player", 640, 10, 52);
    for (int t = 0; t < 2; t++) {
        Rectangle r = {40 + t * 300.0f, 80, 280, 50};
        panel(r, t == st.tab);
        text(TABS[t], r.x + r.width / 2, r.y + 10, 30, ink(t == st.tab), 1);
    }
    int first = std::max(0, std::min(sel - 4, n - 9));
    for (int k = first; k < n && k < first + 9; k++) {
        const MissionSpec &m = list[rows[k]];
        Rectangle r = {40, 146 + (k - first) * 58.0f, 700, 52};
        Progress::Entry e = p.get(m.id);
        bool lock = !p.unlocked(list, rows[k]);
        panel(r, k == sel);
        text(m.name.c_str(), r.x + 20, r.y + 11, 28, lock ? GRAY : ink(k == sel));
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
    if (back) { Audio::play(Audio::Sfx::FeBookOut); return -2; }
    if (ok && open) st.brief = true, Audio::play(Audio::Sfx::FePage);
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

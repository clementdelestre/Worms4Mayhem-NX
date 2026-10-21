#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#endif
#include "ui.h"
#include "results.h"
#include "fx.h"
#include "script.h"
#include "controls.h"
#include "audio.h"
#include "models.h"
#include "frontbg.h"
#include "lit.h"
#include "movie.h"
#include "loading.h"
#include "barracks.h"
#include "wire.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <future>
#include <map>
#include <set>
#include <sstream>
#include <mutex>
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

const Color TEAM_COLORS[4] = {{255, 78, 60, 255}, {108, 127, 255, 255}, {59, 244, 123, 255}, {255, 218, 77, 255}};
Color teamColor(const Game &g, int team) { return TEAM_COLORS[g.alliance(team) & 3]; }
// 0x50cb90: text colour 15 + AlliedGroup (Team_Red .. Team_Yellow), gradient top -> white
static Color teamTextTop(const Game &g, int team) {
    static const Color TOP[4] = {{255, 74, 38, 255}, {109, 148, 255, 255}, {83, 255, 127, 255}, {255, 224, 100, 255}};
    return TOP[g.alliance(team) & 3];
}
Color teamTextColor(const Game &g, int team) {
    return team < (int)g.surrendered.size() && g.surrendered[team] ? Color{128, 128, 128, 255} : teamColor(g, team);
}

namespace {
const int A = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, B = GAMEPAD_BUTTON_RIGHT_FACE_DOWN, X = GAMEPAD_BUTTON_RIGHT_FACE_UP,
          MINUS = GAMEPAD_BUTTON_MIDDLE_LEFT, UP = GAMEPAD_BUTTON_LEFT_FACE_UP, DOWN = GAMEPAD_BUTTON_LEFT_FACE_DOWN, LEFT = GAMEPAD_BUTTON_LEFT_FACE_LEFT,
          RIGHT = GAMEPAD_BUTTON_LEFT_FACE_RIGHT, PLUS = GAMEPAD_BUTTON_MIDDLE_RIGHT;
const int PANEL_ROWS = 7, PANEL_COLS = 6;  // W4M weapon panel grid (0x920e58)
const int MAX_CUSTOM = 8;  // Weapon Factory slots
const Color GOLDEN = {255, 210, 60, 255}, PANEL = {24, 74, 92, 230};  // W4M teal paper
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
    {"Mystery crates", "%d", 0, 100, 10, {}},  // SchemeData MysteryChance
    {"Mine factory", nullptr, 0, 1, 1, {"Off", "On"}},  // SchemeData MineFactoryOn
    {"Number of Rounds", "%d", 1, 9, 1, {}},  // SchemeData Wins: Scheme^Wins^ steps 1..9 and wraps (0x752b9b)
};
static_assert(sizeof SCHEME_FIELDS / sizeof *SCHEME_FIELDS == sizeof(Scheme), "one row per Scheme byte");

int presetOf(const Scheme &s) {
    for (size_t i = 0; i < SCHEMES.size(); i++) if (!memcmp(&SCHEMES[i].s, &s, sizeof s)) return (int)i;
    return -1;
}
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

std::mutex predMu;
std::map<std::string, Image> predecoded;  // predecode() output with its mipmaps, uploaded by tex()
std::set<std::string> decoding;           // queued for predecode(), not decoded yet
std::vector<std::future<void>> decoders;  // under predMu

// assets/ui/<name>.png, loaded once; id 0 when missing or still decoding
Texture2D tex(const std::string &name) {
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    Texture2D t{};
    Image pre{};
    {
        std::lock_guard<std::mutex> l(predMu);
        if (decoding.count(name)) return t;
        if (predecoded.count(name)) pre = predecoded[name], predecoded.erase(name);
    }
    if (pre.data) {  // one texture upload, no SD read, decode or GPU mipmap pass here
        t = LoadTextureFromImage(pre);
        UnloadImage(pre);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        return cache[name] = t;
    }
    const char *p = TextFormat(DATA_DIR "assets/ui/%s.png", name.c_str());
    if (FileExists(p)) {
        double start = GetTime();
        t = LoadTexture(p);
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        TraceLog(LOG_INFO, "UI: texture %s loaded on first use, %.1f ms", name.c_str(), (GetTime() - start) * 1000);  // a frame hitch: warm it earlier
    }
    return cache[name] = t;
}

// Frontend art, decoded off the main thread at startup: SD read + PNG decode of these took a frame on menu changes.
// The boot loading screen's own art comes first (Loading::boot).
const char *const PRELOAD[] = {"back/loadbackgeneric", "fe2/loading_worm", "fe/hintpanel", "fe/tournament_vsus", "fe/bluedivide", "fe2/art_local",
                               "fe2/art_local_static", "fe2/nav_normal", "fe/title_underline", "fe2/art_network", "fe2/art_help", "fe2/art_myworms",
                               "fe/paperpopup01", "fe/paperpopup02", "fe/buttonbig_highlight", "fe/text_border_charcoal", "fe/icon_wxpot",
                               "fe2/paper_strip", "fe/icon_splat", "hud/trailparticle"};
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

// A sub-rectangle of a sprite as a 9-slice: corners of m sprite px drawn at scale k, the sides stretched
bool nineAt(const char *name, Rectangle src, float m, Rectangle d, float k, Color tint = WHITE) {
    Texture2D t = tex(name);
    if (!t.id) return false;
    float c = fminf(m * k, fminf(d.width, d.height) / 2);
    float sx[4] = {src.x, src.x + m, src.x + src.width - m, src.x + src.width}, sy[4] = {src.y, src.y + m, src.y + src.height - m, src.y + src.height};
    float dx[4] = {d.x, d.x + c, d.x + d.width - c, d.x + d.width}, dy[4] = {d.y, d.y + c, d.y + d.height - c, d.y + d.height};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            DrawTexturePro(t, {sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]}, {dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]}, {}, 0, tint);
    return true;
}

// W4M's commentary panel (fe/com_panel): the sheet holds a left-capped (top) and a right-capped (bottom) half, joined at sheet column 120
bool comPanel(Rectangle r) {
    Texture2D p = tex("fe/com_panel");
    if (!p.id) return false;
    float half = floorf(r.width / 2);
    DrawTextureNPatch(p, {{2, 2, 118, 124}, 26, 26, 0, 26, NPATCH_NINE_PATCH}, {r.x, r.y, half, r.height}, {}, 0, WHITE);
    DrawTextureNPatch(p, {{120, 130, 131, 124}, 0, 26, 26, 26, NPATCH_NINE_PATCH}, {r.x + half, r.y, r.width - half, r.height}, {}, 0, WHITE);
    return true;
}

// The ButtonBig hand-drawn frame fitted around `in` (the picture's rect): the sprite's frame is 33 / 37 / 41 / 40 px (left, top, right, bottom) thick, 44 / 46 / 52 / 50 highlighted,
// and its picture hole is 420 px wide, so the scale follows the picture's short side [assumed: W4M's placement of the border around LandscapeControl is not traced]
void pictureFrame(Rectangle in, bool hi, float scale = 0) {
    static const float T[2][4] = {{33, 37, 41, 40}, {44, 46, 52, 50}};
    const float *b = T[hi];
    float k = scale ? scale : fminf(in.width, in.height) / 420;
    nineAt(hi ? "fe/buttonbig_highlight" : "fe/buttonbig_normal", {14, 14, 494, 494}, 80, {in.x - b[0] * k, in.y - b[1] * k, in.width + (b[0] + b[2]) * k, in.height + (b[1] + b[3]) * k}, k);
}

// A team card's backdrop: W4M's blue text box (the blue top half of fe/text_border_list) as a 9-slice
bool cardPanel(Rectangle d, Color tint = WHITE) { return nineAt("fe/text_border_list", {6, 3, 116, 58}, 24, d, 0.9f, tint); }

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

// true while name decodes off the main thread; a level picture not decoded yet starts that
bool pending(const std::string &name) {
    if (cache.count(name)) return false;
    {
        std::lock_guard<std::mutex> l(predMu);
        if (decoding.count(name)) return true;
        if (predecoded.count(name) || name.compare(0, 7, "levels/")) return false;
    }
    predecode({name});
    return true;
}

// a picture still decoding draws nothing until it is in (returns true: no fallback drawn meanwhile)
bool image(const std::string &name, Rectangle d, Color tint = WHITE) {
    if (pending(name)) return true;
    Texture2D t = tex(name);
    if (t.id) DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, d, {}, 0, tint);
    return t.id;
}

std::string lower(std::string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// A map json's head fields: "title" (W4M Frontend_Name key), "preview" (W4M Frontend_Image, else w4m-maps' top-down render), "theme"
struct MapInfo { std::string title, pic, theme; };
std::mutex mapMu;  // mapHeads() fills the cache off the main thread
std::map<std::string, MapInfo> mapCache;
const MapInfo &mapInfo(const std::string &m) {
    auto &cache = mapCache;
    if (std::lock_guard<std::mutex> l(mapMu); cache.count(m)) return cache[m];
    MapInfo info;
    for (const char *dir : {DATA_DIR "assets/maps/", ROMFS_DIR "maps/"}) {
        FILE *f = m.empty() ? nullptr : fopen((dir + m + ".json").c_str(), "rb");
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
        info = {field("\"title\": \""), field("\"preview\": \""), field("\"theme\": \"")};
        break;
    }
    std::lock_guard<std::mutex> l(mapMu);
    return cache[m] = info;
}


std::set<std::string> levelPics;  // assets/ui/levels/*.png names, listed by mapHeads()

// Level picture: the map's preview, else its theme's picture, else nolevel
std::string preview(const std::string &m) {
    static std::map<std::string, std::string> cache;
    auto c = cache.find(m);
    if (c != cache.end()) return c->second;
    static const std::map<std::string, std::string> THEME = {
        {"arabian", "level_arabian"}, {"camelot", "level_camelot"}, {"construction", "level_building"}, {"jurassic", "level_prehistoric"}, {"wildwest", "level_wildwest"}};
    const MapInfo &i = mapInfo(m);
    std::string pv = m.empty() ? "random_camelot" : i.pic;
    if (!m.empty() && !levelPics.count(pv)) pv = THEME.count(i.theme) ? THEME.at(i.theme) : "";
    return cache[m] = levelPics.count(pv) ? "levels/" + pv : "levels/nolevel";
}

}  // namespace

// W4M's level name, else from the file name: "EscapeFromTreeRex" -> "Escape From Tree Rex", "Alien-w3d" -> "Alien (W3D)"
void mapHeads(const std::vector<std::string> &maps) {
    for (std::string p : Loading::list(DATA_DIR "assets/ui/levels", ".png")) levelPics.insert(p.substr(p.rfind('/') + 1, p.size() - p.rfind('/') - 5));
    // w4m-maps' index.tsv holds those heads in one read; a map it lacks (added by hand) has its own head read
    if (FILE *f = fopen(DATA_DIR "assets/maps/index.tsv", "rb")) {
        std::map<std::string, MapInfo> idx;
        for (char l[512]; fgets(l, sizeof l, f);) {
            std::string c[4];
            int k = 0;
            for (char *p = l; *p && *p != '\n' && *p != '\r'; p++) *p == '\t' ? (void)(k < 3 && ++k) : (void)(c[k] += *p);
            if (k == 3) idx[c[0]] = {c[1], c[2], c[3]};
        }
        fclose(f);
        std::lock_guard<std::mutex> l(mapMu);
        for (const std::string &m : maps) if (auto i = idx.find(m); i != idx.end()) mapCache.emplace(m, i->second);
    }
    for (const std::string &m : maps) mapInfo(m);
}

// W4M's WXFEP.SelectLandscape2: the Prebuilt landscapes (multimaps.txt, tools/w4m-re/setuplists.py) and, kept on purpose, the Worms 3D maps
void multiplayerMaps(std::vector<std::string> &maps) {
    char *txt = FileExists(DATA_DIR "assets/multimaps.txt") ? LoadFileText(DATA_DIR "assets/multimaps.txt") : nullptr;
    if (!txt) return;
    std::set<std::string> keep;
    for (char *l = strtok(txt, "\r\n"); l; l = strtok(nullptr, "\r\n")) keep.insert(l);
    UnloadFileText(txt);
    maps.erase(std::remove_if(maps.begin(), maps.end(), [&](const std::string &m) {
        return !m.empty() && !keep.count(m) && !(m.size() > 4 && lower(m.substr(m.size() - 4)) == "-w3d");
    }), maps.end());
}

// W4M's name of an attachment (its MeshTextNameID), "None" for ""
const char *hatLabel(const std::string &id) {
    if (id.empty()) return tr(nullptr, "None", "Aucun");
    const Catalog::Item *a = catalog().attachment(id);
    return tr(a ? a->text.c_str() : nullptr, id.c_str());
}

const std::vector<std::string> &hatIds() {
    static std::vector<std::string> ids;
    if (ids.empty()) {
        ids.push_back("");
        for (const Catalog::Item &a : catalog().attachments) if (a.type == 0 || a.type == 4) ids.push_back(a.id);
    }
    return ids;
}

std::vector<std::string> speechIds() {
    std::vector<std::string> ids;
    for (int i = 0; i < Audio::voiceBanks(); i++) ids.push_back(std::string("vo") + Audio::voiceBankName(i));
    return ids;
}

int voiceBankOf(const std::string &speech) {
    for (int i = 0; i < Audio::voiceBanks(); i++) if (speech == std::string("vo") + Audio::voiceBankName(i)) return i;
    return -1;
}

static std::string wireOf(const GameConfig::Team &t) { wire::W w(0); return putTeam(w, t).b; }

std::string mapTitle(const std::string &m) {
    if (m.empty()) return "Random island";
    if (const std::string &t = mapInfo(m).title; !t.empty()) return tr(t.c_str(), m.c_str());
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

namespace {

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
// W4M weapon enum name without its kWeapon / kUtility prefix ("Tail Nail" -> NoMoreNails)
std::string w4mKey(const WeaponDef &w) {
    static const std::map<std::string, std::string> ALIAS = {{"TailNail", "NoMoreNails"}, {"FatkinsStrike", "Fatkins"}, {"InflatableScouser", "Scouser"},
                                                             {"IcarusPotion", "Redbull"}};
    std::string k;
    for (char c : w.name) if (c != ' ') k += c;
    if (auto a = ALIAS.find(k); a != ALIAS.end()) k = a->second;
    return k;
}

const char *weaponName(const WeaponDef &w) {
    if (w.name.rfind("FETXT.", 0) == 0) return tr(w.name.c_str(), w.name.c_str());  // container Name is the DisplayName (0x599513)
    std::string k = w4mKey(w);
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
const float HOLD = 0.35f;
bool forceHelp = false;
static bool helpOff;  // the Teams screen: - removes a team, no controls diagram
bool helpHeld() { return !helpOff && (forceHelp || minusFor >= HOLD); }

static double cursorFrame = -1;  // when feCursor last drew: the OS pointer comes back on screens that do not
void pollStick() {
    if (IsCursorHidden() && GetTime() - cursorFrame > 0.25) ShowCursor();
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

static const char *mysteryText(int item) { return tr(TextFormat("Text.kMystery%s", MYSTERY_ITEMS[item].name), MYSTERY_ITEMS[item].text); }

static const char *potName(int id) { return tr(TextFormat("FETXT.WPotName.%s", WORMPOT_MODES[id].key), WORMPOT_MODES[id].name, nullptr); }

// Match rules left (W4M Wormpot modes and Sudden Death); pot: the Wormpot mode whose name and help text they use
static const struct { uint32_t bit; int pot; } RULE_ROWS[] = {{RULE_KING, WP_VITAL_WORM}, {RULE_VAMPIRE, WP_VAMPIRE}, {RULE_LOW_GRAVITY, WP_LOW_GRAVITY}, {RULE_SUDDEN_DEATH, 0}};
static const int RULES = 4;
static const uint32_t RULE_MASK = RULE_KING | RULE_VAMPIRE | RULE_LOW_GRAVITY | RULE_SUDDEN_DEATH;
static const char *ruleLabel(int r) { return RULE_ROWS[r].pot ? potName(RULE_ROWS[r].pot) : tr("FETXT.SuddenDeath", "Sudden death", "Mort subite"); }
static const char *ruleHelp(int r) {
    const int pot = RULE_ROWS[r].pot;
    return pot ? tr(TextFormat("FETXT.WPotHelp.%s", WORMPOT_MODES[pot].key), WORMPOT_MODES[pot].help, nullptr)
               : tr("FETXT.HTPSubtopic7", "When the round time reaches zero, the game enters Sudden Death.", nullptr);
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

void predecode(const std::vector<std::string> &names) {
    std::vector<std::string> todo;
    {
        std::lock_guard<std::mutex> l(predMu);
        for (const std::string &n : names) if (!predecoded.count(n) && decoding.insert(n).second) todo.push_back(n);
    }
    if (todo.empty()) return;
    std::lock_guard<std::mutex> l(predMu);
    decoders.erase(std::remove_if(decoders.begin(), decoders.end(), [](std::future<void> &f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }),
                   decoders.end());
    decoders.push_back(std::async(std::launch::async, [todo] {
#ifdef __SWITCH__
        svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3F);  // core 0 below the main thread: runs while it waits (vsync, GPU)
#endif
        for (const std::string &name : todo) {
            std::string p = DATA_DIR "assets/ui/" + name + ".png";
            int n = 0;
            Image img{};
            if (unsigned char *d = FileExists(p.c_str()) ? LoadFileData(p.c_str(), &n) : nullptr) img = LoadImageFromMemory(".png", d, n), UnloadFileData(d);
            if (img.data) ImageMipmaps(&img);
            std::lock_guard<std::mutex> l(predMu);
            if (img.data) predecoded[name] = img;
            decoding.erase(name);
        }
    }));
}

bool warm(const std::vector<std::string> &names, double until) {
    for (const std::string &n : names) {
        if (cache.count(n)) continue;
        bool decodingNow;
        { std::lock_guard<std::mutex> l(predMu); decodingNow = decoding.count(n); }
        if (decodingNow || GetTime() >= until) return true;
        tex(n);
    }
    return false;
}

void unload() {
    std::vector<std::future<void>> d;
    { std::lock_guard<std::mutex> l(predMu); d.swap(decoders); }
    d.clear();  // joins them
    for (auto &kv : predecoded) UnloadImage(kv.second);
    predecoded.clear();
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

// c2: bottom colour of a vertical gradient over the line height (W4M menu items); edge: the outline (W4M Colour_Back);
// pose: per drawn glyph (dy px, scale) of a W4M TextAnim
static void textG(const char *t, float x, float y, float size, Color c, Color c2, int align, Color edge = {0, 0, 0, 255}, const std::vector<Vector2> *pose = nullptr) {
    float sp = fontLoaded ? 0 : size / 10;
    Vector2 m = MeasureTextEx(font, t, size, sp);
    Vector2 p = {roundf(x - m.x * align / 2), roundf(y)};
    float o = fmaxf(1.5f, size / 14);  // W4M text: black outline
    Color k = edge;
    k.a = (unsigned char)(edge.a * c.a / 255);
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
        for (size_t gi = 0; gi < gs.size(); gi++) {
            const G &e = gs[gi];
            const Rectangle &r = font.recs[e.g];
            float x = q.x + e.ox + font.glyphs[e.g].offsetX * s - pad * s, y = q.y + e.oy + font.glyphs[e.g].offsetY * s - pad * s;
            float w = (r.width + 2 * pad) * s, h = (r.height + 2 * pad) * s, u0 = (r.x - pad) / W, v0 = (r.y - pad) / H;
            if (pose && gi < pose->size()) {
                float sc = (*pose)[gi].y, cx = x + w / 2, cy = y + h / 2 + (*pose)[gi].x;
                w *= sc, h *= sc, x = cx - w / 2, y = cy - h / 2;
            }
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
bool artReady(const char *name) { return cache.count(name); }

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
void hints(std::initializer_list<Hint> h) { hints(std::vector<Hint>(h)); }
void hints(const std::vector<Hint> &h) {
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


// Per-weapon button legend. W4M HelpText.kWeapon* / kUtility* list Fire, 1stPerson, Blimp, FuseUp, Movement; our own words (FETXT has no short labels).
static std::vector<Hint> weaponHints(const WeaponDef &wd) {
    struct Row { Kind kind; const char *en, *fr; bool steer; };
    static const Row ROWS[] = {
        {Kind::Shell, "Charge", "Charger", 0}, {Kind::Sheep, "Launch / detonate", "Lancer / exploser", 0}, {Kind::Airstrike, "Launch", "Lancer", 0},
        {Kind::Donkey, "Drop", "Lâcher", 0}, {Kind::Shotgun, "Fire", "Tirer", 0}, {Kind::Rope, "Attach / detach", "Accrocher / lâcher", 0},
        {Kind::Jetpack, "Thrust", "Poussée", 1}, {Kind::Teleport, "Teleport", "Téléporter", 0}, {Kind::SuperSheep, "Launch / detonate", "Lancer / exploser", 1},
        {Kind::OldWoman, "Launch / detonate", "Lancer / exploser", 1}, {Kind::Melee, "Strike", "Frapper", 0}, {Kind::Homing, "Charge", "Charger", 0},
        {Kind::Mine, "Drop", "Lâcher", 0}, {Kind::Scouser, "Launch", "Lancer", 1}, {Kind::Sentry, "Place", "Poser", 0}, {Kind::Abduction, "Abduct", "Enlever", 0},
        {Kind::Flood, "Flood", "Inonder", 0}, {Kind::Parachute, "Open", "Ouvrir", 1}, {Kind::SkipGo, "Skip turn", "Passer son tour", 0},
        {Kind::Surrender, "Surrender", "Abandonner", 0}, {Kind::ChangeWorm, "Next worm", "Ver suivant", 0}, {Kind::Bubble, "Deploy", "Déployer", 0},
        {Kind::Icarus, "Drink", "Boire", 0}};
    std::vector<Hint> h;
    for (const Row &r : ROWS) {
        if (r.kind != wd.kind) continue;
        bool drop = dropped(wd);  // dynamite: a Shell, dropped
        if (blimped(wd.kind)) h.push_back({"Y", "E", tr(nullptr, "Sky view", "Vue du ciel")});
        if (Controls::aimed(wd)) h.push_back({"L/ZL", "RMB", tr(nullptr, "Aim", "Viser")});
        if (wd.userFuse) h.push_back({"D-pad", "=/-", tr("FETXT.Fuse", "Fuse", "Mèche")});
        if (wd.kind == Kind::Rope) h.push_back({"RS", "WASD", tr(nullptr, "Length", "Longueur")});
        if (r.steer) h.push_back({"LS", "Arrows", tr(nullptr, "Steer", "Diriger")});
        if (wd.name == "Sniper Rifle") h.push_back({"D-pad", "Z/X", tr(nullptr, "Zoom (aiming)", "Zoom (en visée)")});
        h.push_back({"A/ZR", "Space", drop ? tr("FETXT.Drop", "Drop", "Lâcher") : tr(nullptr, r.en, r.fr)});
    }
    return h;
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
        {ZL, 222, "Hold: first-person aim (precise)\nZL + stick: aim (single Joy-Con)", "RMB"}, {L, 276, "Hold: first-person aim (as ZL)", "RMB"}, {MIN, 356, "Hold: controls", "F1"},
        {LS, 414, "Move (camera-relative)\nAim mode: walk / turn", "Arrows"}, {DPAD, 488, "Left / right: previous / next weapon\nUp / down: fuse time; aiming / sky view: zoom\nJetpack: up = forward", "Tab"},
        {ZR, 232, "Fire", "Space"}, {R, 270, "Drop dynamite / mine / sheep\n(jetpack, rope, parachute)", "Backspace"}, {PLS, 306, "Pause", "Esc"}, {BX, 342, "Weapon panel", "Q"},
        {BY, 380, "Toggle the sky view\n(strikes, homing)", "E"}, {BA, 440, "Fire (hold = power)\nSky view: fire / lock", "Space"}, {BB, 482, "Jump (twice = backflip)\nSky view: leave", "Enter"},
        {RS, 530, "Camera orbit\nAim mode: aim (+ gyro)", "A/D/W/S"}};
    static const Call MENU[] = {
        {MIN, 330, "Teams screen: remove a team\nElsewhere, hold: controls", "F1"}, {LS, 414, "Move", "Arrows"}, {DPAD, 488, "Move / change value", "Arrows"},
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
                        : "Aim mode: hold L or ZL, or while charging (A)  -  Hold -: controls, longer: performance overlay", 640, 640, 22, LIGHTGRAY, 1);
    if (game) text(keys ? "Jetpack: Space thrusts, arrows steer, Backspace drops dynamite / mine / sheep"  // W4M UtilityFire group
                        : "Jetpack: A / ZR thrust, left stick steers, R drops dynamite / mine / sheep", 640, 666, 22, LIGHTGRAY, 1);
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

// MENUTWKXINGAME FE.AnimSpeed (FrontEndService +0x134, 0x72a244): item clips last their length x it (0x6a03d0, 0x7a9cf8)
static const float FE_ANIM_SPEED = 0.9f;
// The exe's key curve (tools/w4m-models curve(), 0x7abb1c): a zero out-tangent holds; unweighted = Hermite on the tangent
// slopes, weighted = Bezier with handles at key +- tangent / 3, solved for t; clipKeys plays it at the menu's FE.AnimSpeed
float xanimKeys(const float (*k)[6], int n, float t, bool weighted) {
    if (t <= k[0][0]) return k[0][1];
    int i = 0;
    while (i < n - 1 && t >= k[i + 1][0]) i++;
    const float *a = k[i], *b = k[std::min(i + 1, n - 1)];
    if (a == b || (a[4] == 0 && a[5] == 0)) return a[1];
    float dx = b[0] - a[0], s = t - a[0];
    if (!weighted) {
        auto slope = [](float x, float y) { return x != 0 ? y / x : 5.72958e6f; };
        float m0 = slope(a[4], a[5]), m1 = slope(b[2], b[3]), dy = b[1] - a[1];
        float c3 = (m0 * dx + m1 * dx - 2 * dy) / (dx * dx * dx), c2 = (3 * dy - 2 * m0 * dx - m1 * dx) / (dx * dx);
        return ((c3 * s + c2) * s + m0) * s + a[1];
    }
    // ponytail: handles clamped into the segment; the exe's monotonic fix-up (0x7ab6f1) is skipped, no FE clip needs it
    float o1 = a[4] / 3 / dx, o2 = 1 - b[2] / 3 / dx, u1 = fmaxf(o1, 0), u2 = fminf(o2, 1);
    float y1 = a[1] + a[5] / 3, y2 = b[1] - b[3] / 3;
    if (u1 != o1 && o1 != 0) y1 = a[1] + (y1 - a[1]) * u1 / o1;
    if (u2 != o2 && o2 != 1) y2 = b[1] - (b[1] - y2) * (1 - u2) / (1 - o2);
    auto bz = [](float u, float p0, float p1, float p2, float p3) {
        float v = 1 - u;
        return v * v * v * p0 + 3 * u * v * v * p1 + 3 * u * u * v * p2 + u * u * u * p3;
    };
    float lo = 0, hi = 1, u = 0.5f;
    for (int it = 0; it < 30; it++) {
        u = (lo + hi) / 2;
        float x = bz(u, 0, u1 * dx, u2 * dx, dx);
        if (fabsf(x - s) < 1e-6f) break;
        (x > s ? hi : lo) = u;
    }
    return bz(u, a[1], y1, y2, b[1]);
}
float clipKeys(const float (*k)[6], int n, float t, bool weighted) { return xanimKeys(k, n, t / FE_ANIM_SPEED, weighted); }
// Bundl10 WXFrontend.Anim keys {time s, value, in tangent x, y, out tangent x, y} (w4m-models --list, W4M_KEYS):
// in_scalehitxy scale XY and shake X / Y, unweighted
const float IN_SCALEHIT_S[6][6] = {{0, 0, 0.00027776f, 0.99951f, 0, 0}, {0.00016665f, 0.59961f, 0.94434f, 0.32764f, 0.94434f, 0.32764f},
                                   {0.083313f, 1.0361f, 0.08844f, 0.99561f, 0.49951f, -0.86621f}, {0.125f, 0.98096f, 0.59717f, -0.80176f, 0.81934f, 0.57275f},
                                   {0.16663f, 1.0098f, 0.81934f, 0.57275f, 0.97168f, -0.23511f}, {0.20825f, 1, 0.97168f, -0.23511f, 1, 0}};
const float IN_SCALEHIT_X[7][6] = {{0, 0, 0.18823f, -0.98193f, 0.18823f, -0.98193f}, {0.083313f, -0.43457f, 0.18823f, -0.98193f, 0.020905f, 0.99951f},
                                   {0.125f, 1.5566f, 0.020905f, 0.99951f, 0.016006f, -0.99951f}, {0.16663f, -1.0449f, 0.016006f, -0.99951f, 0.023331f, 0.99951f},
                                   {0.20825f, 0.73926f, 0.023331f, 0.99951f, 0.1037f, -0.99414f}, {0.25f, 0.3396f, 0.1037f, -0.99414f, 0.12549f, 0.9917f},
                                   {0.2915f, 0.66895f, 0.12549f, 0.9917f, 0, 0}};
const float IN_SCALEHIT_Y[7][6] = {{0, 0, 0.18823f, -0.98193f, 0.18823f, -0.98193f}, {0.083313f, -0.43457f, 0.18823f, -0.98193f, 0.03476f, -0.99902f},
                                   {0.125f, -1.6318f, 0.03476f, -0.99902f, 0.024689f, 0.99951f}, {0.16663f, 0.054443f, 0.024689f, 0.99951f, 0.039368f, 0.99902f},
                                   {0.20825f, 1.1113f, 0.039368f, 0.99902f, 0.037415f, -0.99902f}, {0.25f, -0.00060606f, 0.037415f, -0.99902f, 0.062103f, 0.99805f},
                                   {0.2915f, 0.66895f, 0.062103f, 0.99805f, 0, 0}};

// tilted deg about its centre
void logo(float cx, float y, float w, float deg, float stretch) {
    Texture2D t = tex("fe/tournament_vsus");
    Rectangle src = {130, 50, 780, 400};
    float h = w * src.height / src.width * stretch;
    rlPushMatrix();
    rlTranslatef(cx, y + h / 2, 0);
    rlRotatef(deg, 0, 0, 1);
    if (t.id) {
        DrawTexturePro(t, src, {-w / 2, -h / 2, w, h}, {}, 0, WHITE);
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
// FE text is 1.16 x wider than our font at the same height [observed: briefing, title, ticker lines of the real game, same glyph height]
static const float TEXT_X = 1.16f;
// FE x positions (not sizes) widen by 16:9 / 4:3 about the screen centre [user-requested, docs/w4m/frontend.md "16:9 spread"]; FeFlat: W4M's own x
static const float FE_SPREAD = (1280 / 720.0f) / (4 / 3.0f);
static float feK = FE_SPREAD;
// Main menu list and logo drawn this much larger than W4M's size [user-requested, docs/w4m/frontend.md "Main menu size"]
static const float MAIN_BIG = 1.25f;
struct FeFlat { float old = feK; FeFlat() { feK = 1; } ~FeFlat() { feK = old; } };
struct MenuItem { const char *key, *en, *fr; float x, y, size, deg, fit, sx = 1; };  // fit: max label width (px, after sx), 0 = none; sx: x stretch

static const Color GOLD_TOP = {255, 240, 130, 255}, GOLD_BOT = {245, 140, 25, 255}, INK = {22, 36, 58, 255}, BLUE_PANEL = {2, 79, 119, 255};
static float easeOut(float k) { k = Clamp(k, 0, 1); return 1 - (1 - k) * (1 - k) * (1 - k); }

// W4M menu entry: gold gradient label tilted about its centre; glow 0..1 = highlight (white on a black brush stroke, pulsing)
static void menuEntry(const char *label, float cx, float cy, float size, float deg, float fit, float sx, float glow, float appear, float t) {
    if (appear <= 0) return;
    float w = textWidth(label, size) * sx;
    if (fit > 0 && w > fit) size *= fit / w, w = fit;
    float s = 1 + glow * (0.04f - 0.04f * cosf(t * 4 * PI));
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
    rlScalef(sx, 1, 1);
    textG(label, 0, -size * 0.52f, size, top, bot, 1);
    rlPopMatrix();
}

#ifdef __SWITCH__
static const int MAIN_ITEMS = 5 - !REPLAYS;  // console games leave through HOME, no Quit entry
#else
static const int MAIN_ITEMS = 6 - !REPLAYS;
#endif
static int mainItem(int row) { return row + (!REPLAYS && row >= 3); }  // menu row -> MAIN_ALL index
// WXFE.MainMenuMenu list [data MENUTWKXPCCOMMON]: centre (155, 19) FE, 236 x 220, tilt 0.06 rad; rows {FontSize, RowHeight, own tilt}
// stacked from the box top with a 2 gap (rows past the bottom scroll); each label centred (KeepWhiteSpace 0 forces kEdgeJustifyCentreCentre)
// at min(FontSize, column width / its em width) - 0.1 (ScaleTextToFit, 0x781dfd -> 0x755843) [disasm + data]
struct MainRow { const char *key, *en, *fr; float font, height, tilt; };
static const MainRow MAIN_ALL[] = {
    {"FETXT.LocalGame", "Local Game", "Partie locale", 35, 35, 0},
    {"FETXT.HTPHeader3", "Network Game", "Partie en réseau", 35, 35, 0},
    {"FETXT.MyWorms", "My Worms", "Mes Worms", 35, 35, -0.03f},
    {nullptr, "Replays", "Replays", 35, 35, 0},
    {"FETXT.Help&Options", "Help & Options", "Aide et options", 27, 25, -0.01f},
    {"Lang.Quit", "Quit", "Quitter", 35, 35, -0.03f},
};
static std::array<MenuItem, 6> mainMenu() {
    // FE text size as feText (TEXT_X); column = row width less its two 2-unit gap columns [assumed]
    const float COL_W = (236 - 4) * MAIN_BIG, GAP = 2 * MAIN_BIG, TILT = 0.06f;
    std::array<MenuItem, 6> m{};
    float top = 110 * MAIN_BIG;  // y up, from the box centre
    for (int i = 0; i < MAIN_ITEMS; i++) {
        const MainRow &r = MAIN_ALL[mainItem(i)];
        float cy = top - r.height * MAIN_BIG / 2;
        top -= r.height * MAIN_BIG + GAP;
        m[i] = {r.key, r.en, r.fr, 640 + (155 - cy * sinf(TILT)) * 720 / 500 * FE_SPREAD, 360 - (19 + cy * cosf(TILT)) * 720 / 500, (r.font - 0.1f) * MAIN_BIG * 720 / 500,
                -(TILT + r.tilt) * RAD2DEG, COL_W * 720 / 500, TEXT_X};
    }
    return m;
}
static const std::array<MenuItem, 6> MAIN_MENU = mainMenu();
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
// Our menu row transition: `in` s after the screen showed it eases in over 0.35 s, rows `stagger` s apart; `out` s after
// leaving began (-1: not leaving) it flies out within LEAVE. 0 hidden .. 1 in place.
static float rowAppear(float in, float out, int i, float stagger = 0.05f) {
    return out >= 0 ? 1 - easeOut((out - 0.012f * i) / (LEAVE - 0.06f)) : easeOut((in - stagger * i) / 0.35f);
}

// Bundl10 WXFrontend.Anim clips (docs/w4m/frontend.md §17): (s, value) keys per channel, translation in FE units (y up)
struct Keys { const float (*k)[6] = nullptr; int n = 0; bool weighted = false; };
template <int N> static constexpr Keys keys(const float (&k)[N][6], bool w) { return {k, N, w}; }
struct FeClip { Keys sx, sy, tx, ty, rz; };
static const float K_SLIDEX[2][6] = {{0, 0, 0, 3.0723f, 0, 3.0723f}, {0.625f, 1, 1.0625f, 0, 1.0625f, 0}},
                   K_SPEECH[4][6] = {{0, 0, 1, 0, 0, 0}, {0.125f, 0.79053f, 0.00041008f, 1.4258f, 0.00027919f, 0.97021f},
                                     {0.45825f, 1, 0.28711f, -0.094971f, 0.031891f, 0.018967f}, {0.625f, 1, 0.17236f, -0.023331f, 0.17236f, -0.023331f}},
                   K_TOOLTIP[2][6] = {{0, 0, 0, 3.0723f, 0, 3.0723f}, {1.041f, 1, 2.5723f, 0, 2.5723f, 0}},
                   K_OUT_SCALEY[3][6] = {{0, 1, 2.1426f, -0.0098724f, 0.52344f, -0.00089788f}, {0.20825f, 0.26685f, 0.055634f, -1.0742f, 0.00016665f, 0},
                                         {0.2085f, 0, 0.00016665f, -0.26685f, 0, 0}},
                   K_TITLE_IN_X[2][6] = {{0, -290, 0, 833.5f, 0, 833.5f}, {1.041f, 0, 2.6621f, 0, 2.6621f, 0}},
                   K_TITLE_OUT_X[2][6] = {{0, 0, 1.1309f, 0, 1.1309f, 0}, {0.5f, -454, 0, -557.5f, 0, -557.5f}},
                   K_TITLE_IN_Y[12][6] = {{0, 0, 0, 0, 0.10101f, 0.99463f}, {0.083313f, 0.82031f, 0.10101f, 0.99463f, 0.019501f, -0.99951f},
                                          {0.125f, -1.3145f, 0.019501f, -0.99951f, 0.030197f, 0.99951f}, {0.20825f, 1.4424f, 0.030197f, 0.99951f, 0.03595f, -0.99902f},
                                          {0.25f, 0.2854f, 0.03595f, -0.99902f, 0.24915f, -0.96826f}, {0.375f, -0.20032f, 0.24915f, -0.96826f, 0.23474f, 0.97168f},
                                          {0.45825f, 0.14453f, 0.23474f, 0.97168f, 0.38574f, -0.92236f}, {0.5415f, -0.054565f, 0.38574f, -0.92236f, 0.42676f, 0.9043f},
                                          {0.625f, 0.12195f, 0.42676f, 0.9043f, 0.35474f, -0.93457f}, {0.70801f, -0.097473f, 0.35474f, -0.93457f, 0.65918f, 0.75146f},
                                          {0.83301f, 0.044983f, 0.65918f, 0.75146f, 0.9707f, -0.23962f}, {0.95801f, 0.014137f, 0.9707f, -0.23962f, 0, 0}},
                   K_TITLE_OUT_Y[11][6] = {{0, 0, 0, 0, 0.70215f, 0.71143f}, {0.083313f, 0.084412f, 0.70215f, 0.71143f, 0.17261f, -0.98486f},
                                           {0.125f, -0.1532f, 0.17261f, -0.98486f, 0.28027f, 0.95947f}, {0.20825f, 0.13196f, 0.28027f, 0.95947f, 0.36646f, -0.93018f},
                                           {0.2915f, -0.079468f, 0.36646f, -0.93018f, 0.7207f, 0.69287f}, {0.45825f, 0.08075f, 0.7207f, 0.69287f, 0.52393f, -0.85107f},
                                           {0.5415f, -0.054565f, 0.52393f, -0.85107f, 0.63428f, 0.77246f}, {0.625f, 0.046906f, 0.63428f, 0.77246f, 0.51953f, -0.854f},
                                           {0.70801f, -0.089966f, 0.51953f, -0.854f, 0.6792f, 0.7334f}, {0.83301f, 0.044983f, 0.6792f, 0.7334f, 0.9707f, -0.23962f},
                                           {0.95801f, 0.014137f, 0.9707f, -0.23962f, 0, 0}};
// In_Flipy: scale XY 0 -> 0.69 at 0.04 s, then slowly to 1 at 2.92 s
static const float K_FLIPY[3][6] = {{0, 0, 0.041656f, 0.69043f, 0, 0}, {0.041656f, 0.69043f, 0, 0.0149f, 0.30249f, 2.3301f}, {2.916f, 1, 7.9102f, 0, 6.7852f, -18.984f}};
static const FeClip IN_FLIPY = {keys(K_FLIPY, true), keys(K_FLIPY, true), {}, {}, {}};
static const FeClip IN_SLIDEX = {keys(K_SLIDEX, true), {}, {}, {}}, IN_SPEECH = {keys(K_SPEECH, true), keys(K_SPEECH, true), {}, {}},
                    IN_TOOLTIP = {{}, keys(K_TOOLTIP, true), {}, {}}, OUT_SCALEY = {{}, keys(K_OUT_SCALEY, true), {}, {}},
                    IN_TITLEUNDERLINE = {{}, {}, keys(K_TITLE_IN_X, true), keys(K_TITLE_IN_Y, false)},
                    OUT_TITLEUNDERLINE = {{}, {}, keys(K_TITLE_OUT_X, true), keys(K_TITLE_OUT_Y, false)};
// In_ScaleY (WXFEP.MissionBriefing, the team editor's pages and pickers)
static const float K_IN_SCALEY[7][6] = {{0, 0, 0.00027776f, 0.99951f, 0, 0}, {0.00016665f, 0.59961f, 0.94434f, 0.32764f, 0.94434f, 0.32764f},
                                        {0.083313f, 1.0361f, 0.063599f, 0.99756f, 0.49951f, -0.86621f}, {0.125f, 0.95508f, 0.4541f, -0.89063f, 0.49341f, 0.86963f},
                                        {0.16663f, 1.0283f, 0.49341f, 0.86963f, 0.61914f, -0.78467f}, {0.20825f, 0.97559f, 0.61914f, -0.78467f, 0.86621f, 0.49951f},
                                        {0.25f, 1, 0.86621f, 0.49951f, 1, 0}};
static const FeClip IN_SCALEY = {{}, keys(K_IN_SCALEY, false), {}, {}};
// In_Prev / Out_Prev (the back button), In_SpringXY and Out_ScaleXY (the jar): scale XY and rotate Z keys, {time s, value, in tangent x, y, out tangent x, y}
static const float K_IN_PREV_S[3][6] = {{0, 0, 0.083313f, 0.59961f, 0, 0}, {0.083313f, 0.59961f, 0.078003f, 0.2113f, 0, 1.1338f}, {0.4165f, 1, 0.33325f, 0, 0.33325f, 0}},
                   K_IN_PREV_R[4][6] = {{0, -1.5703f, 0.0049934f, 0.056641f, 0.025528f, 1.9404f}, {0.375f, 0, 0.61523f, -0.047852f, 0, 0.0016317f},
                                        {0.4165f, -0.0085526f, 0.041656f, -0.0085526f, 0.041656f, 0.0085526f}, {0.45825f, 0, 0.041656f, 0.0085526f, 1, 0}},
                   K_OUT_PREV_S[3][6] = {{0, 1, 0.45044f, -0.14087f, 0.45044f, -0.14087f}, {0.25f, 0.48682f, 0.17102f, -1.1035f, 0.00016665f, 0}, {0.25f, 0, 0.00016665f, -0.48682f, 0, 0}},
                   K_OUT_PREV_R[2][6] = {{0, 0, 0.375f, 0, 0.50049f, 0.13049f}, {0.375f, 0.52344f, 0.23511f, 0.94043f, 0, 0.0016317f}},
                   K_SPRING[6][6] = {{-0.00016665f, 0, 0.00027776f, 0.99951f, 0, 0}, {0, 0.59961f, 0.94434f, 0.32764f, 0.94434f, 0.32764f},
                                     {0.083313f, 1.0361f, 0.08844f, 0.99561f, 0.49951f, -0.86621f}, {0.125f, 0.9834f, 0.61475f, -0.78809f, 0.84668f, 0.53174f},
                                     {0.16663f, 1.0098f, 0.84668f, 0.53174f, 0.97266f, -0.23035f}, {0.20825f, 1, 0.97266f, -0.23035f, 1, 0}},
                   K_OUT_SCALEXY[3][6] = {{0, 1, 2.0762f, -0.0095673f, 0.45679f, -0.00078344f}, {0.24976f, 0.26685f, 0.055634f, -1.0742f, 0.00016665f, 0}, {0.25f, 0, 0.00016665f, -0.26685f, 0, 0}};
static const FeClip IN_PREV = {keys(K_IN_PREV_S, true), keys(K_IN_PREV_S, true), {}, {}, keys(K_IN_PREV_R, true)},
                    OUT_PREV = {keys(K_OUT_PREV_S, true), keys(K_OUT_PREV_S, true), {}, {}, keys(K_OUT_PREV_R, true)},
                    IN_SPRINGXY = {keys(K_SPRING, false), keys(K_SPRING, false), {}, {}, {}}, OUT_SCALEXY = {keys(K_OUT_SCALEXY, true), keys(K_OUT_SCALEXY, true), {}, {}, {}};
// FE units about the screen centre, y up: 500 tall at 720 px [observed: WXFE.Story / WXFE.Challenges against the real game; the 3D items are seen by
// a camera 903.5 units from the z = 0 plane, FOV 0.54 rad: the plane is 500 tall there, the ToolTip strip and the book match]
static const float FE_S = 720 / 500.0f;

// A W4M menu item about its centre c: Anim_Incoming `in` once `delay` s passed since the screen showed (`since`), Anim_Outgoing
// `out` once leaving (s, -1: not). A null clip (W4M None) takes rowAppear() slid by `slide` px. False: hidden; else rlPopMatrix() after.
static bool feItem(Vector2 c, const FeClip *in, float delay, const FeClip *out, float since, float leaving, float slide = 0, int row = 0) {
    if (in && since < delay) return false;  // W4M parks the item off-screen until its delay is up (0x755a78)
    const FeClip *k = leaving >= 0 ? out : in;
    float t = leaving >= 0 ? leaving : since - delay, sx = 1, sy = 1, tx = 0, ty = 0, rz = 0;
    auto at = [&](Keys ch, float rest) { return ch.n ? clipKeys(ch.k, ch.n, t, ch.weighted) : rest; };
    if (k) sx = at(k->sx, 1), sy = at(k->sy, 1), tx = at(k->tx, 0) * FE_S, ty = -at(k->ty, 0) * FE_S, rz = at(k->rz, 0);
    else if (float a = rowAppear(since, leaving, row); a > 0) tx = (1 - a) * slide;
    else return false;
    if (sx < 0.001f || sy < 0.001f) return false;
    rlPushMatrix();
    rlTranslatef(c.x + tx, c.y + ty, 0);
    rlRotatef(-rz * RAD2DEG, 0, 0, 1);
    rlScalef(sx, sy, 1);
    rlTranslatef(-c.x, -c.y, 0);
    return true;
}

// ---------------------------------------------------------------- W4M frontend layout (docs/w4m/frontend.md §Story and Challenges)

static Vector2 fePos(float x, float y) { return {640 + x * FE_S * feK, 360 - y * FE_S}; }
static Rectangle feRect(float x, float y, float hw, float hh) { Vector2 c = fePos(x, y); return {c.x - hw * FE_S, c.y - hh * FE_S, 2 * hw * FE_S, 2 * hh * FE_S}; }

// A WXFE_MeshObjectDesc through the FE camera (0.54 rad at z 903.5 [observed, see FE_S]; Lighting: FrontendMeshLight): position (FE units), scale,
// orientation (rad) and the mesh clip at t s
extern "C" void glClear(unsigned int mask);
static const int FE_DIVIDE_LIGHT = 2;  // feMesh light: 0 none, 1 the item's Lighting (FrontendMeshLight), 2 the BlueDivide's
static void feMesh(const char *name, Vector3 pos, Vector3 scale, Vector3 rot, int light, const char *clip, float t) {
    // the BlueDivide sits in bin 0x24, a HUD bin: the HUD camera (0, 0, 500), t 0.48 [disasm 0x51f740, 0x4d81e0]
    const bool hudBin = light == FE_DIVIDE_LIGHT;
    Camera3D cam = {{0, 0, hudBin ? 500.0f : 903.5f}, {0, 0, 0}, {0, 1, 0}, hudBin ? 2 * atanf(0.48f) * RAD2DEG : 0.54f * RAD2DEG, CAMERA_PERSPECTIVE};
    glClear(0x100);
    rlSetClipPlanes(5, 15000);
    BeginMode3D(cam);
    Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixRotateZYX(rot)), MatrixTranslate(pos.x, pos.y, pos.z));
    Models::shade(light == FE_DIVIDE_LIGHT ? Lit::divideShader() : Lit::frontendShader(light));
    Models::draw(name, m, WHITE, clip, t / FE_ANIM_SPEED, false);
    Models::shade({});
    EndMode3D();
    rlSetClipPlanes(0.01, 1000);
}

// a Weapon Factory weapon's held graphic (gun parts, Radio or payload) in the FE 3D space, like feMesh
static void feWeapon(const std::string &graphic, Vector3 pos, float scale, Vector3 rot) {
    Camera3D cam = {{0, 0, 903.5f}, {0, 0, 0}, {0, 1, 0}, 0.54f * RAD2DEG, CAMERA_PERSPECTIVE};
    glClear(0x100);
    rlSetClipPlanes(5, 15000);
    BeginMode3D(cam);
    Models::shade(Lit::frontendShader(true));
    Models::drawWeaponGraphic(graphic, MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateZYX(rot)), MatrixTranslate(pos.x, pos.y, pos.z)));
    Models::shade({});
    EndMode3D();
    rlSetClipPlanes(0.01, 1000);
}


// The flag's TGA as the HUD.Flag cloth shows it: our PNGs are top-down, the mesh's UVs expect the TGA's bottom-up rows
static Texture2D flagTex(const std::string &png) {
    static std::map<std::string, Texture2D> flipped;
    if (auto it = flipped.find(png); it != flipped.end()) return it->second;
    Texture2D t = tex(png);
    if (!t.id) return t;
    Image im = LoadImageFromTexture(t);
    ImageFlipVertical(&im);
    Texture2D f = LoadTextureFromImage(im);
    UnloadImage(im);
    SetTextureFilter(f, TEXTURE_FILTER_BILINEAR);
    return flipped[png] = f;
}

// EnergyBarEntity 0x5e8620: the HUD.Flag cloth at HUD.Energy.FlagXScale / FlagYScale 2.2 (HUD units, 480 high = 1.5 px), the flag's TGA on it, clip Go looped;
// c = its centre in screen pixels (the HUD draws on the 1280 x 720 target)
static bool hudFlag(const std::string &png, Vector2 c) {
    Texture2D ft = flagTex(png);
    if (!Models::has("hud_flag") || !ft.id) return false;
    Camera3D cam = {{640, -360, 10}, {640, -360, 0}, {0, 1, 0}, 720, CAMERA_ORTHOGRAPHIC};
    glClear(0x100);
    BeginMode3D(cam);
    Models::texture(&ft);
    Models::draw("hud_flag", MatrixMultiply(MatrixScale(3.3f, 3.3f, 3.3f), MatrixTranslate(c.x, -c.y, 0)), WHITE, "Go", fmodf((float)GetTime(), 1.5f), true);
    Models::texture(nullptr);
    EndMode3D();
    return true;
}

// WXFE_GradientColours: top -> bottom (docs/w4m/frontend.md "Text colours")
static const Color GC_BUTTON_YELLOW[2] = {{255, 255, 234, 255}, {255, 241, 120, 255}}, GC_LIST_LABLE_BLUE[2] = {{151, 207, 255, 255}, {233, 255, 255, 255}},
                   GC_SUBHEADER_BLUE[2] = {{128, 210, 255, 255}, {255, 255, 255, 255}}, GC_BOOK_BROWN[2] = {{93, 51, 0, 200}, {93, 51, 0, 200}},
                   GC_POPUP_RED[2] = {{148, 0, 0, 255}, {148, 0, 0, 255}}, GC_SCROLLTEXT[2] = {{0, 69, 109, 110}, {0, 69, 109, 110}},
                   GC_WHITE[2] = {WHITE, WHITE};
static const Color BC_BLACK = BLACK, BC_NONE = {0, 0, 0, 0}, BC_BOOK_EDGE = {255, 255, 255, 80}, BC_SCROLLTEXT = {109, 151, 175, 32};  // WXFE_BackgroundColours (0x754dc0): the text outline

// W4M TextAnim (docs/w4m/frontend.md "TextBox layout"): per glyph a High / Low pair of the HUD.FontAnim clips, the groups cycling by glyph, the
// alternative by the LCG; (dy in FE units, y up, scale). kind: 1 LargeWobble, 2 SmallWobble, 3 TitleRandom, 4 MediumWobble
static std::vector<Vector2> textPose(int kind, const std::string &s) {
    static const Vector2 CLIPS[5] = {{2, 1}, {0.1f, 1}, {2, 0.9f}, {0.5f, 1.1f}, {0.7f, 1}};  // High1..5 (Low = -dy)
    static const int GROUPS[5][3] = {{}, {0, 0, 0}, {1, 1, 1}, {0, 2, 3}, {4, 4, 4}};
    uint32_t st = 2166136261u;
    for (char c : s) st = (st ^ (unsigned char)c) * 16777619u;
    std::vector<Vector2> pose;
    int n = 0;
    for (char c : s) {
        if (c == ' ') continue;
        st = st * 0x41c64e6du + 0x3039;
        Vector2 k = CLIPS[GROUPS[kind][n++ % 3]];
        pose.push_back({(st >> 16 & 1 ? 1.0f : -1.0f) * k.x * FE_S * -1, k.y});  // y up -> down in px
    }
    return pose;
}

// A W4M text box: a word-wrapped block in a box about its centre c (half extents hw x hh, FE units), font size `size` FE (line height = size),
// anchored by `just` (EdgeJustificationEnum), tilted by rot (Orientation z, rad). autoScale shrinks the size until it fits; scrollT >= 0 (s since
// the box showed) scrolls an overflowing text of two lines or more [AutoScrollSpeed > 0: 0x75e735 waits AutoScrollDelay, then 0.01 line per 20 ms tick upward, lines recycling]
struct FeBox {
    Vector2 c = {0, 0};
    float hw = 0, hh = 0, size = 20, rot = 0, scrollT = -1;
    int just = 4, anim = 0;
    bool autoScale = false;
    const Color *grad = GC_WHITE;
    Color edge = BC_BLACK;
    bool oneLine = false;  // scaled down until the text fits the box's width on a single line
};
// /*FETXT.Control.X*/ tokens: W4M's text table swaps each for the controller's button glyph (0x4de750 aliases MenuSelect / MenuCancel to
// FETXT.JoyButton1 / 2); here a pill with our pad button (Switch A / B) or the PC key, kept in a line as '\x01' + the label
static std::string feTokens(const std::string &s) {
    static const struct { const char *tok, *pad, *key; } MAP[] = {
        {"FETXT.Control.MenuSelect", "A", "Enter"}, {"FETXT.Control.MenuCancel", "B", "Backspace"}, {"FETXT.Control.Fire", "A", "Space"},
        {"FETXT.Control.1stPerson", "ZL", "RMB"}, {"FETXT.Control.Jump", "B", "Enter"}, {"FETXT.Control.Blimp", "Y", "E"},
        {"FETXT.Control.FuseUp", "D-pad", "="}, {"FETXT.Control.Inventory", "X", "Q"}, {"FETXT.Control.ZoomIn", "R", "X"},
        {"FETXT.Control.ZoomOut", "L", "Z"}, {"FETXT.Control.Ninja.Lengthen", "RS", "S"}, {"FETXT.Control.Ninja.Shorten", "RS", "W"},
        {"FETXT.Control.Ninja.Forwards", "LS", "Arrows"}, {"FETXT.Control.Ninja.Backwards", "LS", "Arrows"}, {"FETXT.Control.GirderRaise", "RS", "W"},
        {"FETXT.Control.GirderLower", "RS", "S"}, {"FETXT.Control.Pause", "+", "Esc"}, {"Button.Movement", "LS", "Arrows"}, {"Button.Aim", "RS", "WASD"}};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        size_t e = s.compare(i, 2, "/*") == 0 ? s.find("*/", i + 2) : std::string::npos;
        if (e == std::string::npos) { out += s[i++]; continue; }
        std::string tok = s.substr(i + 2, e - i - 2), label = tok.substr(tok.rfind('.') + 1);
        for (const auto &m : MAP) if (tok == m.tok) label = keyGlyphs() ? m.key : m.pad;
        out += '\x01' + label + '\x02', i = e + 2;
    }
    return out;
}
static float feGlyphH(float px) { return px * 0.95f; }
// width of a line: text runs measured as is, each button pill as glyphs() draws it
static float feWidth(const std::string &s, float px) {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        size_t a = s.find('\x01', i), e = a == std::string::npos ? s.size() : a;
        w += textWidth(s.substr(i, e - i).c_str(), px);
        if (a == std::string::npos) break;
        size_t z = s.find('\x02', a);
        w += glyphs(s.substr(a + 1, z - a - 1).c_str(), 0, 0, feGlyphH(px), keyGlyphs(), false) + px * 0.15f, i = z + 1;
    }
    return w;
}
static std::vector<std::string> feWrap(const std::string &s, float sizePx, float w) {
    std::vector<std::string> lines;
    std::string line, word;
    for (size_t i = 0; i <= s.size(); i++) {
        char ch = i < s.size() ? s[i] : ' ';
        if (ch != ' ' && ch != '\n') { word += ch; continue; }
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && feWidth(t, sizePx) * TEXT_X > w) lines.push_back(line), t = word;
        line = t, word.clear();
        if (ch == '\n') lines.push_back(line), line.clear();
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}
// one feWrap() line at (x, y) top, its button pills drawn as glyphs
static void feLine(const std::string &ln, float x, float y, float px, Color c0, Color c1, int align, Color e, const std::vector<Vector2> *pose = nullptr) {
    if (ln.find('\x01') == std::string::npos) return textG(ln.c_str(), x, y, px, c0, c1, align, e, pose);
    float cx = x - feWidth(ln, px) * align / 2;
    for (size_t k = 0; k < ln.size();) {
        size_t a = ln.find('\x01', k), z = a == std::string::npos ? ln.size() : a;
        std::string run = ln.substr(k, z - k);
        textG(run.c_str(), cx, y, px, c0, c1, 0, e), cx += textWidth(run.c_str(), px);
        if (a == std::string::npos) break;
        size_t q = ln.find('\x02', a);
        float h = feGlyphH(px);
        cx += glyphs(ln.substr(a + 1, q - a - 1).c_str(), cx, y + (px - h) / 2, h, keyGlyphs()) + px * 0.15f, k = q + 1;
    }
}
static void feText(const std::string &text, const FeBox &b) {
    std::string s = feTokens(text);
    float size = b.size;
    std::vector<std::string> lines = feWrap(s, size * FE_S, b.oneLine ? 1e6f : 2 * b.hw * FE_S);
    while (b.oneLine && size > 5 && feWidth(s, size * FE_S) > 2 * b.hw * FE_S) size -= 1;
    if (b.oneLine) lines = feWrap(s, size * FE_S, 1e6f);
    while (b.autoScale && size > 5 && lines.size() * size > 2 * b.hh) lines = feWrap(s, (size -= 1) * FE_S, 2 * b.hw * FE_S);
    float px = size * FE_S, H = lines.size() * size, over = H - 2 * b.hh;
    bool scroll = b.scrollT >= 0 && over > 0 && lines.size() > 1;
    float top = scroll || b.just < 3 ? b.c.y + b.hh : b.just < 6 ? b.c.y + H / 2 : b.c.y - b.hh + H, shift = 0;  // FE y of the first line's top
    if (scroll) shift = fmaxf(floorf(b.scrollT / 0.02f) - 4000 / 20, 0) * 0.01f;  // lines: AutoScrollDelay 4000 ms of 20 ms ticks, then AutoScrollSpeed 0.01 per tick
    Vector2 ctr = fePos(b.c.x, b.c.y);
    rlPushMatrix();
    rlTranslatef(ctr.x, ctr.y, 0);
    rlRotatef(-b.rot * RAD2DEG, 0, 0, 1);
    rlTranslatef(-ctr.x, -ctr.y, 0);
    int align = b.just % 3;
    float x = align == 0 ? b.c.x - b.hw : align == 1 ? b.c.x : b.c.x + b.hw;
    for (size_t i = 0; i < lines.size(); i++) {
        std::vector<Vector2> pose = b.anim ? textPose(b.anim, lines[i]) : std::vector<Vector2>();
        float y = top - i * size, a = 1;
        if (scroll) {  // 0x75e735 rotates the first line to the end each line scrolled; 0x75d705 squashes and fades a line into the box edges
            size_t n = lines.size(), slot = (i + n - (size_t)shift % n) % n;
            y = top + (shift - floorf(shift) - slot) * size;
            if (y > top) a = 1 - (y - top) / size, y = top;
            else if (y < b.c.y - b.hh + size) a = (y - (b.c.y - b.hh)) / size;
            if (a <= 0) continue;
        }
        Vector2 p = fePos(x, y);
        rlPushMatrix(), rlTranslatef(p.x, p.y, 0), rlScalef(TEXT_X, a, 1), rlTranslatef(-p.x, -p.y, 0);
        Color c0 = b.grad[0], c1 = b.grad[1], e = b.edge;
        c0.a *= a, c1.a *= a, e.a *= a;
        feLine(lines[i], p.x, p.y, px, c0, c1, align, e, b.anim ? &pose : nullptr);
        rlPopMatrix();
    }
    rlPopMatrix();
}

// The blue curved panel (BlueDivide, In_Curve settled), its left edge at x px
static void divide(float x) {
    if (!image("fe/bluedivide", {x - 60, -40, 800, 800})) DrawCircleV({x - 260, 360}, 760, BLUE_PANEL);
}

// The four scrolling FETXTH watermarks of every submenu (Scrolling 1-4: position, font size, scale, Speed, ScrollForward, Delay_Incoming; In_SlideX
// along the text), rotated 90 deg [data]; Speed = FE units per 20 ms tick, the text recycled with one space between copies (0x76df84, 0x76e0a0). since / out:
// seconds since the screen showed / since it began leaving (-1: not); a page that is in place passes since >= 1
static void watermark(const char *title, float x, float t, float since = 99, float out = -1) {
    static const struct { float x, y, size, half, speed, delay; bool fwd; } COLS[4] = {{-308, -30, 30, 270, 0.4f, 0.3f, true}, {-259, 0, 60, 270, 0.3f, 0.35f, false},
                                                                                           {-211, -10, 30, 260, 0.2f, 0.385f, true}, {-369, 0, 60, 270, 0.15f, 0.35f, false}};
    FeFlat flat;
    std::string tile = std::string(title) + " ";
    float shift = out >= 0 ? (1 - rowAppear(99, out, 0)) * -700 : 0;
    for (const auto &c : COLS) {
        float px = c.size * FE_S, w = textWidth(tile.c_str(), px), run = fmodf(floorf(t * 50) * c.speed * FE_S, w);
        float k = since < c.delay ? 0 : clipKeys(K_SLIDEX, 2, since - c.delay, true);
        if (k < 0.001f) continue;
        Vector2 o = fePos(c.x, c.y);
        rlPushMatrix();
        rlTranslatef(o.x + x + shift, o.y, 0);
        rlRotatef(-90, 0, 0, 1);  // reads bottom to top
        rlScalef(k, 1, 1);
        for (float u = -c.half * FE_S - w + (c.fwd ? run : w - run); u < c.half * FE_S; u += w) textG(tile.c_str(), u, -px / 2, px, GC_SCROLLTEXT[0], GC_SCROLLTEXT[1], 0, BC_SCROLLTEXT);
        rlPopMatrix();
    }
}

// WX.Mesh.ToolTip: the torn paper strip at (0, -19.4, 793) scale (0.12, 0.1, 0.12) tilted -0.02 rad [data], seen through the FE camera at 7.2 / 6.2 of
// its unit [observed: strip centre FE -159 = the ticker's y, 1624 x 97 px]; its picture covers the texture rows 4..104
static void tooltipStrip() {
    Texture2D p = tex("fe2/paper_strip");
    Vector2 c = fePos(0, -159.1f);
    float w = 1146.7f * 0.12f * 8.2f * FE_S, h = 82 * 0.1f * 8.2f * FE_S;
    if (p.id) DrawTexturePro(p, {0, 4, 256, 100}, {c.x, c.y, w, h}, {w / 2, h / 2}, 0.02f * RAD2DEG, WHITE);
    else DrawRectangle(0, c.y - h / 2, 1280, h, {246, 243, 232, 255});
}

// WXFE_TextScroller (0x76e1c1, 0x76df84, 0x76e0a0): a line narrower than its window (Wrap 0) stays put, a wider one and a copy one space behind it
// slide left 0.5 FE per 20 ms tick (the ticker's Speed), back to the start after one text + space, with a 2000 ms wait; t: s since the text was set;
// loop (the strip's default ticker, user-requested): no wait, the scroll never stops, a short text too
float marqueeShift(float period, float t, bool loop) {
    int steps = (int)(period / (0.5f * FE_S)) + 1, n = (int)(fmaxf(t, 0) * 50) % (100 + steps);
    return loop ? fmodf(fmaxf(t, 0) * 25 * FE_S, period) : fmaxf(n - 100, 0) * 0.5f * FE_S;  // loop: no wait, the copies glide by without a stop
}
static void marquee(const char *s, Rectangle win, float px, const Color *g, int align, Color edge, float t, bool loop = false) {
    float w = textWidth(s, px) * TEXT_X, cy = win.y + win.height / 2;
    auto line = [&](float x, int al) { rlPushMatrix(), rlTranslatef(roundf(x), cy, 0), rlScalef(TEXT_X, 1, 1), textG(s, 0, -px / 2, px, g[0], g[1], al, edge), rlPopMatrix(); };
    if (roundf(w) <= roundf(win.width) && !loop)  // whole pixels: a window sized from the text's own width still fits it
        return line(align == 0 ? win.x : align == 1 ? win.x + win.width / 2 : win.x + win.width, align);
    float period = fmaxf(w + textWidth(" ", px) * TEXT_X, loop ? win.width : 0);  // loop: a short text still crosses the whole window
    rlDrawRenderBatchActive();
    BeginScissorMode((int)win.x, (int)(cy - px), (int)win.width, (int)(2 * px));
    const float shift = marqueeShift(period, t, loop);
    for (float x = win.x - shift; x < win.x + win.width; x += period) line(x, 0);
    rlDrawRenderBatchActive();
    EndScissorMode();
}
// s since `s` last changed at this call site (the scroller restarts on a new text: 0x76e23c subscribes 0x76e1c1 to its resource)
static float sinceSet(std::string &last, float &at, const char *s) {
    if (last != s) last = s, at = now();
    return now() - at;
}

// TickerTapeTXT (WXFE_NewsFeedScrollingTextDesc of WXFE.TickerTape = WXFE.TickerTapeDefault, NewsFeedService 0x4bb8e0): Solid_Black (Colour_Front 3,
// Colour_Back 1 Blank: no outline) FE.Font 20 at (11, -159) in a 406 FE window, tilted -0.029 rad, Wrap 0 [data]; a tooltip runs the same scroller
static void tickerText(float t, const char *tip = nullptr, bool warn = false) {
    static std::string last;
    static float at = 0;
    const char *tick = tip ? tip : tr("WXFE.TickerTapeDefault", "Worms4NX - fan-made homebrew                    ");
    float px = 20 * FE_S;
    // the gap between NAV Prev (-247, half 45) and NAV Start (264, half 40) less 10 FE each side = W4M's 406 FE window at (11, -159)
    float l = fePos(-247, 0).x + 55 * FE_S, r = fePos(264, 0).x - 50 * FE_S;
    Vector2 c = {(l + r) / 2, fePos(0, -159).y};
    static const Color ERR[2] = {{200, 20, 10, 255}, {200, 20, 10, 255}}, INK[2] = {BLACK, BLACK};
    rlPushMatrix();
    rlTranslatef(c.x, c.y, 0);
    rlRotatef(0.029f * RAD2DEG, 0, 0, 1);
    rlTranslatef(-c.x, -c.y, 0);
    std::string shown = tick;  // W4M pads the default text with spaces so it leaves the strip: the loop needs none
    while (!shown.empty() && shown.back() == ' ') shown.pop_back();
    marquee(shown.c_str(), {l, c.y - 20 * FE_S, r - l, 40 * FE_S}, px, warn ? ERR : INK, 1, BC_NONE, tip ? sinceSet(last, at, tip) : t, !tip);
    rlPopMatrix();
}

// A NAV button of the WXFE.Nav.Border atlases (kMT_Nav*): which 0 Start (the jar), 1 Tick, 2 Back, 3 Cross; state 0 Normal, 1 Highlight, 2 Disabled;
// c: FE centre, half: FE half extent
static void navAt(int which, int state, Rectangle d) {
    Texture2D a = tex(state == 0 ? "fe2/nav_normal" : state == 1 ? "fe2/nav_highlight" : "fe2/nav_disabled");
    if (a.id) DrawTexturePro(a, {a.width / 2.0f * (which % 2), a.height / 2.0f * (which / 2), a.width / 2.0f, a.height / 2.0f}, d, {}, 0, WHITE);
}
static void navButton(int which, int state, Vector2 c, float half) {
    Vector2 p = fePos(c.x, c.y);
    float d = 2 * half * FE_S;
    navAt(which, state, {p.x - d / 2, p.y - d / 2, d, d});
}

// A page arrow (kMT_NavArrow*): left / right, state as navButton, in a square of half extent `half` FE
static void pageArrow(bool right, int state, Vector2 c, float half) {
    Texture2D a = tex("fe2/nav_arrowpage");
    Vector2 p = fePos(c.x, c.y);
    float d = 2 * half * FE_S;
    if (a.id) DrawTexturePro(a, {a.width / 4.0f * state, a.height / 2.0f * !right, a.width / 4.0f, a.height / 2.0f}, {p.x - d / 2, p.y - d / 2, d, d}, {}, 0, WHITE);
}

// The strip with its ticker (or a tooltip) and, in a submenu, NAV Prev (the back arrow, (-247, -159) scale 45) with the pad's Back;
// start: NAV Start (the jar, (264, -166) scale 40) in state 0 Normal / 2 Disabled, -1 none
static void paperStrip(float t, bool back, const char *tip = nullptr, bool warn = false, int start = -1) {
    tooltipStrip();
    tickerText(t, tip, warn);
    if (back) navButton(2, 0, {-247, -159}, 45);
    if (start >= 0) navButton(0, start, {264, -166}, 40);
}

// MouseEntity (0x744cfc / 0x744c0e): the FE.Mouse sprite (mouse.tga, FE.MouseSize 20 FE) centred 14 FE right / below the pointer, rotated 0.98 rad
void feCursor() {
#ifndef __SWITCH__
    static Vector2 last = {-1, -1};
    Vector2 m = GetMousePosition();
    static bool moved = false;
    if (last.x >= 0 && (m.x != last.x || m.y != last.y)) moved = true;
    last = m;
    if (!moved) return;
    Texture2D t = tex("fe/mouse");
    if (!t.id) return;
    if (!IsCursorHidden()) HideCursor();
    cursorFrame = GetTime();
    float d = 20 * FE_S;
    rlPushMatrix();
    rlTranslatef(m.x + 14 * FE_S, m.y + 14 * FE_S, 0);
    rlRotatef(0.98f * RAD2DEG, 0, 0, 1);  // clockwise: the arrow's tip then lands next to the pointer [assumed sign]
    DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {-d / 2, -d / 2, d, d}, {}, 0, WHITE);
    rlPopMatrix();
#endif
}

static float backOut(float k) { k = Clamp(k, 0, 1) - 1; return 1 + 2.7f * k * k * k + 1.7f * k * k; }

// W4M's Title Control (docs/w4m/frontend.md §17): FontSizeOverride 40, Button_Yellow, TitleRandom, over its brush underline; x: the page's offset.
// The spot is read off the HISTOIRE / DEFIS screens [observed]: text left edge at FE -305, centre y 195, underline from -363, centre y 174
static void titleControl(const char *title, float x) {
    FeFlat flat;
    float sz = 40 * FE_S, tw = textWidth(title, sz) * TEXT_X;
    Vector2 l = fePos(-305, 195), u = fePos(-363, 174);
    std::vector<Vector2> pose = textPose(3, title);
    rlPushMatrix(), rlTranslatef(l.x + x, 0, 0), rlScalef(TEXT_X, 1, 1);
    textG(title, 0, l.y - sz / 2, sz, GC_BUTTON_YELLOW[0], GC_BUTTON_YELLOW[1], 0, BC_BLACK, &pose);
    rlPopMatrix();
    float w = l.x + tw + 12 * FE_S - u.x;
    if (!image("fe/title_underline", {u.x + x, u.y - w / 32, w, w / 16})) DrawRectangle(u.x + x, u.y, w, 3, WHITE);
}

// Submenu page: curved blue panel (slides in from the left) with its scrolling watermarks and Title Control, and an illustration that pops in
// after it; p: 0 hidden .. 1 shown (the panel takes the first 0.7)
static void subPanel(const char *title, const char *art, float t, float p) {
    float x = (1 - easeOut(p / 0.7f)) * -820, pop = backOut((p - 0.3f) / 0.7f);
    divide(x);
    watermark(title, x, t);
    titleControl(title, x);
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


// W4M WXFEP.Confirm* in our style: the question, then ConfirmList's No (the default) and Yes rows
static void yesNo(const char *q, int r, float t) {
    popup({330, 200, 620, 320});
    std::string a = q, b;  // too wide for one line: split at the space nearest the middle, full size on two lines
    if (textWidth(q, 36) > 560) {
        size_t best = std::string::npos, mid = a.size() / 2;
        auto dist = [mid](size_t i) { return i > mid ? i - mid : mid - i; };
        for (size_t i = 0; i < a.size(); i++) if (a[i] == ' ' && (best == std::string::npos || dist(i) < dist(best))) best = i;
        if (best != std::string::npos) b = a.substr(best + 1), a.resize(best);
    }
    const float qs = fminf(36, 36 * 560 / fmaxf(textWidth(a.c_str(), 36), textWidth(b.c_str(), 36)));
    const float qw = fmaxf(fmaxf(textWidth(a.c_str(), qs), textWidth(b.c_str(), qs)) + 40, 300), uy = b.empty() ? 280 : 300;
    text(a.c_str(), 640, b.empty() ? 236 : 218, qs, GOLD_TOP, 1);
    if (!b.empty()) text(b.c_str(), 640, 258, qs, GOLD_TOP, 1);
    if (!image("fe/title_underline", {640 - qw / 2, uy, qw, 22})) DrawRectangle(640 - qw / 2, uy + 6, qw, 3, WHITE);
    for (int i = 0; i < 2; i++) {
        bool hi = i == r;
        float sz = hi ? 50 : 42, s = hi ? 1.04f - 0.04f * cosf(t * 4 * PI) : 1;
        rlPushMatrix();
        rlTranslatef(640, 360 + i * 78.0f, 0);
        rlScalef(s, s, 1);
        text(i ? tr("FETXT.Yes", "Yes", "Oui") : tr("FETXT.No", "No", "Non"), 0, -sz / 2, sz, hi ? WHITE : Color{150, 205, 238, 255}, 1);
        rlPopMatrix();
    }
}

static bool feBack = false;  // last A/B in a menu was B: screen changes play W4M's prev in/out sounds
// W4M's intro sound for screen s (kAUDIO_In_*), entered from `from`
static Audio::Sfx enterSfx(Frontend::Screen s, Frontend::Screen from) {
    using F = Frontend;
    using S = Audio::Sfx;
    if (s == F::Confirm || s == F::SchemeEdit) return S::FePopupIn;
    if (from == F::Confirm || from == F::SchemeEdit) return S::FePopupOut;
    if (feBack || from == F::Missions) return S::FePrevIn;
    switch (s) {
    case F::Main: return S::FeBounce;
    case F::Local: case F::HelpOpts: return S::FeSlide;
    case F::Network: return S::FeNet;
    case F::MyWorms: return S::FeCustom;
    case F::Options: return S::FeSoundVid;
    case F::Controls: case F::TeamEdit: return S::FeController;  // WXFE.TeamOptions Audio_Incoming 10 In_Controller
    case F::Factory: case F::FactoryEdit: return S::FeFactory;
    case F::Wormpot: return S::FeWormpot;
    default: return S::FeNextIn;
    }
}
static int bgPage(Frontend::Screen s, bool online) {
    using F = Frontend;
    return s <= F::Main || s == F::Confirm ? 0 : s == F::TeamEdit ? 3 : s == F::Local || s == F::Missions || s >= F::SetupTeams ? 1 : s == F::Network ? 2 : 3;
}

static const float TITLE_OUT = 0.35f + 0.05f * (MAIN_ITEMS - 1);  // the title glide's length: last menu row in

float Frontend::subIn(float t) const { return leaving >= 0 ? 1 - (t - leaving) / LEAVE : (t - entered) / 0.4f; }
float Frontend::titleIn(float t) const {
    return leaving >= 0 && next == Title ? TITLE_OUT - (t - leaving) : from == Title && leaving < 0 ? t - entered : 100;
}
void Frontend::go(Screen s) {
    next = s, leaving = now(), FrontBg::page(bgPage(s, online));
    Audio::play(feBack ? Audio::Sfx::FePrevOut : Audio::Sfx::FeNextOut);
}

void Frontend::menu(const MenuItem *items, int n, int &sel, int dy, float t, bool live) {
    sel = clampWrap(sel + dy, n);
    float k = fminf(1, GetFrameTime() * 14);
    for (int i = 0; i < n; i++) {
        const MenuItem &m = items[i];
        float &g = glow[i], a = !live ? 1 : rowAppear(leaving >= 0 || from == Title ? titleIn(t) : t - entered, leaving >= 0 && next != Title ? t - leaving : -1, i);
        g = live ? g + ((i == sel) - g) * k : i == sel;
        menuEntry(tr(m.key, m.en, m.fr), m.x, m.y, m.size, m.deg, m.fit, m.sx, g, a, t);
    }
}

// An alliance's colour name (FE.Red / Blue / Green / Yellow)
static const char *allianceColour(int al) {
    static const char *const KEY[4][3] = {{"FE.Red", "Red", "Rouge"}, {"FE.Blue", "Blue", "Bleu"}, {"FE.Green", "Green", "Vert"}, {"FE.Yellow", "Yellow", "Jaune"}};
    return tr(KEY[al & 3][0], KEY[al & 3][1], KEY[al & 3][2]);
}

// GameLogicService 0x4fb880 FE.WinString: the winner's name, its AlliedGroup's colour name when 2+ teams of it have not surrendered
// (0x4f5280), FETXT.Nobody on a draw
std::string resultBanner(const Game &g) {
    if (g.cfg.mission || g.phase != Phase::GameOver) return "";
    if (g.winner < 0) return tr("FETXT.Nobody", "Nobody", "Personne");
    int al = g.alliance(g.winner) & 3, allies = 0;
    for (int t = 0; t < g.teams; t++) allies += g.alliance(t) == g.alliance(g.winner) && !(t < (int)g.surrendered.size() && g.surrendered[t]);
    return allies > 1 ? allianceColour(al) : teamName(g.cfg, g.winner);
}

// WXFE.WinMatch WinsMatchTEXT after a win (one round: the match), WXFE.WinRound WinsRoundTEXT after a draw (GameOver.NextRoundMenu)
const char *resultLine(const Game &g) {
    return g.winner >= 0 ? tr("FETXT.WinsTheMatch", "Wins the match!", "Gagne le match !") : tr("FETXT.WinsTheRound", "Wins the round!", "Gagne le round !");
}

std::string teamName(const GameConfig &c, int team) {
    if (team < (int)c.teamSetup.size() && !c.teamSetup[team].name.empty()) return c.teamSetup[team].name;
    return TextFormat("Team %d", team + 1);
}

const char *wormName(const GameConfig &c, int team, int i) {  // the team's own names (Tn_Wk_Name), else ours
    if (team < (int)c.teamSetup.size() && i < (int)c.teamSetup[team].wormNames.size() && !c.teamSetup[team].wormNames[i].empty()) return c.teamSetup[team].wormNames[i].c_str();
    return WORM_NAMES[(team * 4 + i) % 16];
}

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
// true when the player confirmed with enough pads connected (B / cancel returns a failed Result).
static bool controllerApplet(int players) {
    HidLaControllerSupportArg arg;
    HidLaControllerSupportResultInfo info;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = arg.hdr.player_count_max = (s8)Clamp(players, 1, 4);
    arg.hdr.enable_single_mode = players == 1;  // lets 1 player stay in handheld mode
    arg.hdr.enable_identification_color = 1;
    for (int i = 0; i < 4; i++) arg.identification_color[i] = {TEAM_COLORS[i].r, TEAM_COLORS[i].g, TEAM_COLORS[i].b, TEAM_COLORS[i].a};
    info = {};
    Result rc = hidLaShowControllerSupport(&info, &arg);
    return R_SUCCEEDED(rc) && info.player_count >= Clamp(players, 1, 4);
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

// teams.txt, seeded from W4M's defaults (and an older setup.txt's teams) when missing
void Frontend::loadTeams() {
    if (!barracks.teams.empty()) return;
    barracks.load(DATA_DIR "teams.txt", DATA_DIR "assets/defteams.txt", DATA_DIR "setup.txt");
    if (!FileExists(DATA_DIR "teams.txt")) barracks.save(DATA_DIR "teams.txt");
}

// The team picked for Story / Challenges, else the first human one; a single-team progress.txt of an older build becomes that first team's file
std::string Frontend::missionTeamName() {
    loadTeams();
    auto first = std::find_if(barracks.teams.begin(), barracks.teams.end(), [](const SavedTeam &t) { return !t.skill; });
    if (first != barracks.teams.end()) Progress::migrate(DATA_DIR "progress.txt", Progress::teamFile(DATA_DIR, first->name).c_str());
    if (barracks.find(missionTeam) < 0) missionTeam = first != barracks.teams.end() ? first->name : "";
    return missionTeam;
}

// setup.txt: "map NAME|-", "rules N", "reels N", "scheme <Scheme bytes>", then per placed team "slot <k> <pad> <worms> <alliance> <handicap> <team name>" (the teams are teams.txt's)
void Frontend::loadSetup(GameConfig &cfg, const std::vector<std::string> &maps) {
    loaded = true;
    loadTeams();
    cfg.teamSetup.assign(4, {});
    lobby.used = 0;
    if (!loadCustomWeapons(DATA_DIR "custom_weapons.json", customs)) customs.clear();
    if (customs.size() > (size_t)MAX_CUSTOM) customs.resize(MAX_CUSTOM);
    char *txt = LoadFileText(DATA_DIR "setup.txt");
    if (!txt) return;
    for (char *line = strtok(txt, "\n"); line; line = strtok(nullptr, "\n")) {
        char s[64] = "";
        int a = 0, b = 0, w = 0, al = 0, h = 0, n = 0;
        if (sscanf(line, "rules %d", &a) == 1) cfg.rules = (uint32_t)a & RULE_MASK;
        else if (sscanf(line, "reels %d", &a) == 1) cfg.wormpot = (uint32_t)a;
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
        } else if (sscanf(line, "slot %d %d %d %d %d %n", &a, &b, &w, &al, &h, &n) == 5 && a >= 0 && a < 4 && line[n]) {
            int i = barracks.find(line + n);
            if (i < 0 || placed(line + n)) continue;
            cfg.teamSetup[a] = barracks.teams[i].slot(Clamp(w, 1, 6), Clamp(al, 0, 3), Clamp(h, -1, 1), customs);
            cfg.teamSetup[a].pad = (uint8_t)Clamp(b, 0, 3);
            lobby.used |= 1u << a;
            lobby.cfg.teamSetup.resize(4), lobby.cfg.teamSetup[a] = cfg.teamSetup[a];
        }
    }
    UnloadFileText(txt);
}

void Frontend::saveSetup(const GameConfig &cfg) const {
    if (room()) return;  // a room's teams and settings are the room's
    std::string s = TextFormat("rules %u\nreels %u\nmap %s\nscheme", cfg.rules, cfg.wormpot, cfg.map.empty() ? "-" : cfg.map.c_str());
    for (size_t i = 0; i < sizeof(Scheme); i++) s += TextFormat(" %d", ((const uint8_t *)&cfg.scheme)[i]);
    s += "\n";
    for (int k = 0; k < (int)cfg.teamSetup.size(); k++) {
        const GameConfig::Team &t = cfg.teamSetup[k];
        if (lobby.playing(k)) s += TextFormat("slot %d %d %d %d %d %s\n", k, t.pad, cfg.teamWorms(k), t.alliance < 0 ? 0 : t.alliance, t.handicap, t.name.c_str());
    }
    SaveFileText(DATA_DIR "setup.txt", s.data());
}

Frontend::Action Frontend::frame(GameConfig &cfg, const std::vector<std::string> &maps, std::string &host, int &port, std::string &name) {
    menuPage = true;
    if (!loaded) {
        lobby = Lobby{};  // local play: one console owns every slot
        loadSetup(cfg, maps);
        lobby.cfg = cfg, lobby.cfg.teams = Lobby::MAX_TEAMS;
        lobby.cfg.map = maps.empty() ? "" : maps[std::min(mapSel, (int)maps.size() - 1)];
        if (online && net) {  // a fresh room: this console has no team yet (the Teams page picks them), and as host its settings
            myTeams.clear();
            lobby.cfg.teams = 0;
            if (net->isHost()) {
                net->lobby.map = lobby.cfg.map, net->lobby.rules = lobby.cfg.rules, net->lobby.scheme = lobby.cfg.scheme, net->lobby.wormpot = lobby.cfg.wormpot;
                net->lobby.wormsPerTeam = lobby.cfg.wormsPerTeam;
                net->pushLobby();
            }
            if (auto it = net->consoleTeams.find(net->id); it != net->consoleTeams.end() && !it->second.empty()) myTeams = it->second;  // the room still knows them
            else net->sendTeams(myTeams);
        }
    }
    if (room()) fromNet(maps);
    if (cfg.teamSetup.size() < 4) cfg.teamSetup.resize(4);
    Action act = None;
    float t = now();
    if (leaving >= 0 && t - leaving >= (next == Title ? TITLE_OUT : LEAVE)) screen = next, leaving = -1;
    bool typing = editing != nullptr, busy = typing || leaving >= 0;  // no input while the menu flies out
    live = !busy;
    if (editing) {
        for (int c = GetCharPressed(); c; c = GetCharPressed()) simText.push_back((char)(c < 256 ? c : 0));
        for (char c : simText)
            if (c >= 32 && editing->size() < 20) editing->push_back(c), Audio::play(Audio::Sfx::FeType);
        simText.clear();
        if (IsKeyPressed(KEY_BACKSPACE) && !editing->empty()) editing->pop_back(), Audio::play(Audio::Sfx::FeType);
        if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(editPad, A)) editing = nullptr, Audio::play(Audio::Sfx::FeClick);
    }
    int dy = busy ? 0 : P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP});
    int dx = busy ? 0 : P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT});
    bool ok = !busy && P({A}, {KEY_ENTER, KEY_SPACE}), back = !busy && P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
    bool plus = !busy && P({PLUS}, {KEY_N});  // next step / start
    int card = busy ? 0 : P({GAMEPAD_BUTTON_RIGHT_TRIGGER_1}, {KEY_PAGE_DOWN}) - P({GAMEPAD_BUTTON_LEFT_TRIGGER_1}, {KEY_PAGE_UP});
    if (pop.kind != Pop::None) {  // a popup is modal: it takes the input (the Teams screen hands it its pad's presses)
        if (pop.pad < 0) popInput({dx, dy, 0, ok, back, plus}, t);
        busy = true, live = false, dx = dy = card = 0, ok = back = plus = false;
    }
    if (room() && !busy && P({GAMEPAD_BUTTON_RIGHT_TRIGGER_2}, {KEY_R})) net->setReady(!net->isReady(net->id));
    if (ok || back) feBack = back;
    if (screen != shown) {  // W4M menus: slide in (not on the first frame: --ui captures), highlight the current entry
        TraceLog(LOG_INFO, "UI: screen %d -> %d at %.2f s", (int)shown, (int)screen, t);
        entered = shown == (Screen)-1 ? -100 : t, from = shown, shown = screen;
        int sel = screen == Main ? mainRow : screen >= Local && screen <= Confirm ? subRow[screen - Local] : 0;
        for (int i = 0; i < 8; i++) glow[i] = i == sel;
        FrontBg::page(bgPage(screen, online));
        if (reopen >= 0) openTeams((Pop::Use)reopen, -1, 0, reopenArg), reopen = -1;  // after Create Team: the list the team was made for
        if (from != (Screen)-1 && screen != Missions) Audio::play(enterSfx(screen, from));  // missionMenu() plays its own
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
        float a = easeOut(titleIn(t) / 0.4f);  // the title's logo glides to its menu spot (backwards on B)
        float up = leaving >= 0 && next != Title ? easeOut((t - leaving) / LEAVE) : from == Title || leaving >= 0 ? 0 : 1 - easeOut((t - entered) / 0.35f);  // to / from a submenu
        // WXFE.Vs: the 1024 x 512 texture as 440 x 236 FE about (-118, 102) [data], turned 0.145 rad [observed on the real menu; the data says 0.17]
        const float ROT = 0.145f, w = 440 * 780 / 1024.0f * FE_S * MAIN_BIG, h = w * 400 / 780 * (236 / 220.0f);
        const Vector2 off = Vector2Rotate({(520 - 512) / 1024.0f * 440, (256 - 250) / 512.0f * 236}, ROT), c = fePos(-118 + off.x, 102 + off.y);  // logo()'s crop centre
        logo(Lerp(640, c.x, a), Lerp(90, c.y - h / 2, a) - up * 300, Lerp(760, w, a), -ROT * RAD2DEG * a, 236 / 220.0f);
        text(TextFormat("Ver#%s", W4NX_VERSION), fePos(200, -220).x, fePos(200, -220).y - 20 * FE_S * 0.52f, 20 * FE_S, WHITE, 1);  // BuildNumber (200, -220), size 20, centred (Justification 4) [data]
        menu(MAIN_MENU.data(), MAIN_ITEMS, mainRow, confirm ? 0 : dy, t, !confirm);
        rlPushMatrix();
        rlTranslatef(0, (1 - a) * 110, 0);
        paperStrip(t, false);
        rlPopMatrix();
        if (confirm) {
            int &r = subRow[4];
            r = clampWrap(r + dy + dx, 2);
            DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 120});
            yesNo(tr("FETXT.ConfirmQuit", "Exit Game? Are You Sure?", "Quitter le jeu ? Vraiment ?"), r, t);
            if (back || (ok && !r)) screen = Main;
            else if (ok) act = Quit;
            break;
        }
        if (back) go(Title);
        if (ok) {
            Screen to[] = {Local, Network, MyWorms, Main, HelpOpts, Confirm};
            int item = mainItem(mainRow);
            if (item == 3) act = Replays;
            else if (item == 5) screen = Confirm, subRow[4] = 0;
            else go(to[item]);
            if (item == 2) online = false, loaded = false;  // the local setup.txt teams
        }
        break;
    }
    case Missions: act = SinglePlayer; break;
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
            else if (sel == 1) screen = SetupTeams, online = false, cursors = {}, rulesRow = 0, loaded = false;
            else openTeams(Pop::Mission, -1, 0, sel - 2);  // W4M WXFEP.SelectTeam, then the Story / Challenges screen
        }
        if (ok && k == 1) wantLan = sel == 0, act = Salon;
        if (ok && k == 3) screen = sel == 0 ? Options : sel == 1 ? Controls : Factory, row = 0, facSel = 0;
        break;
    }
    case MyWorms: {  // W4M WXFE.Customise: Create / Edit / Delete Team
        static const MenuItem items[] = {{"FETXT.CreateTeam", "Create Team", "Créer une équipe", 880, 190, 56, -3}, {"FETXT.EditTeam", "Edit Team", "Éditer une équipe", 1050, 320, 54, 2},
                                         {"FETXT.DeleteTeam", "Delete Team", "Supprimer une équipe", 900, 450, 54, -2}};
        int &sel = subRow[2];
        subPanel(tr("FETXTH.MYWORMS", "MY WORMS", "MES WORMS"), "fe2/art_myworms", t, subIn(t));
        menu(items, 3, sel, dy, t);
        paperStrip(t, true);
        hints({{"A", "Enter", tr(nullptr, "Select", "Sélectionner")}, {"B", "Esc", tr(nullptr, "Back", "Retour")}});
        if (back) go(Main);
        if (ok) {
            if (sel == 0) startCreate(Pop::Create);
            else openTeams(sel == 1 ? Pop::Edit : Pop::Delete, -1, 0);
        }
        break;
    }
    case TeamEdit: editor({dx, dy, card, ok, back, plus}, t); break;
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
        if (row == 2 && (dx || ok)) music = !music, Audio::music(music, nullptr, true);
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
    case Wormpot: wormpot(lobby.cfg, dx, dy, ok, back, t); break;
    case Factory: factory(dx, dy, ok, back); break;
    case FactoryEdit: factoryEdit(dx, dy, ok, back, typing, t); break;
    case SchemeEdit: {
        GameConfig &c = lobby.cfg;
        const int n = (int)sizeof(Scheme);
        schemeRow = clampWrap(schemeRow + dy, n + 1);
        int preset = presetOf(c.scheme), np = (int)SCHEMES.size();
        if (schemeRow == 0 && dx) c.scheme = SCHEMES[clampWrap(preset < 0 ? (dx > 0 ? 0 : np - 1) : preset + dx, np)].s;
        uint8_t *bytes = (uint8_t *)&c.scheme;
        if (schemeRow > 0 && dx) {
            const SchemeField &f = SCHEME_FIELDS[schemeRow - 1];
            int v = bytes[schemeRow - 1] + dx * f.step;
            bytes[schemeRow - 1] = (uint8_t)(f.names[0] ? clampWrap(v, f.max + 1) : Clamp(v, f.min, f.max));
        }
        heading("Game scheme", 640, 14, 48);
        popup({300, 80, 680, 632});
        for (int i = 0; i <= n; i++) {
            Rectangle r = {320, 90 + i * 28.0f, 640, 26};
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
        if (back || ok) screen = SetupRules;
        break;
    }
    case SetupTeams: if (setupTeams(plus, t)) act = LeaveRoom; break;
    case SetupMap: setupMap(dx, dy, back, plus, maps); break;
    case SetupRules:
        if (setupRules(dx, dy, ok, back, plus)) act = online ? RoomStart : StartLocal;
        break;
    }
    popDraw(t);
    if (room()) toNet();
    helpOff = screen == SetupTeams;
    if (helpHeld()) controls(false);
    Loading::overlay(GetFrameTime());
    if (capture) {
        rlDrawRenderBatchActive();
        Image img = LoadImageFromScreen();
        ExportImage(img, capture);
        UnloadImage(img);
        capture = nullptr;
    }
    feCursor();
    EndDrawing();
    if (act == StartLocal) {
        const GameConfig l = lobby.match();
        cfg.teams = l.teams, cfg.wormsPerTeam = l.wormsPerTeam, cfg.map = l.map, cfg.rules = l.rules, cfg.teamSetup = l.teamSetup, cfg.scheme = l.scheme, cfg.wormpot = l.wormpot;
    }
    if (act == LeaveRoom) online = false, screen = Network;
    if (act != None) {
        cfg.teamSetup.resize(4);
        for (int k = 0; k < 4; k++) Audio::setTeamVoice(k, voiceBankOf(cfg.teamSetup[k].speech));
        cfg.teamSetup.resize(cfg.teams);
        if (!online) loaded = false;  // reload (and re-pad to 4 teams) next time the menu shows; a room keeps its setup
    }
    return act;
}

// ---------------------------------------------------------------- local match setup: Teams / Map / Rules (docs/frontend-setup.md)

namespace {
const int YB = GAMEPAD_BUTTON_RIGHT_FACE_LEFT;

bool padPresent(int p) {
    if (Frontend::simPads >> p & 1) return true;
#ifdef __SWITCH__
    return IsGamepadAvailable(p);
#else
    return p == 0 || IsGamepadAvailable(p);  // the desktop keyboard is pad 0
#endif
}

// W4M's FETXT.CPU1..5
const char *cpuText(int n) {
    static const char *const EN[] = {"", "CPU Level 1", "CPU Level 2", "CPU Level 3", "CPU Level 4", "CPU Level 5"}, *const FR[] = {"", "I.A. Niveau 1", "I.A. Niveau 2", "I.A. Niveau 3", "I.A. Niveau 4", "I.A. Niveau 5"};
    n = Clamp(n, 0, 5);
    return tr(n ? TextFormat("FETXT.CPU%d", n) : nullptr, EN[n], FR[n]);
}

// WXFE_GradientColours [data, docs/w4m/frontend.md "Text colours"]: 10 TextBox_Yellow, 12 List_Data_Orange, 19 Disabled_Grey, 31 ERROR_RED, 3 Solid_Black
const Color GC_TEXTBOX_YELLOW[2] = {{255, 242, 182, 255}, {255, 212, 119, 255}}, GC_LIST_DATA_ORANGE[2] = {{255, 228, 146, 255}, {255, 250, 225, 255}},
            GC_DISABLED[2] = {{128, 128, 128, 255}, {192, 192, 192, 255}}, GC_ERROR_RED[2] = {{255, 0, 0, 255}, {255, 0, 0, 255}}, GC_BLACK[2] = {BLACK, BLACK};


#ifdef __SWITCH__
bool padMissing(const Lobby &L) {
    for (int k = 0; k < L.cfg.teams; k++) if (L.playing(k) && L.canEdit(k) && !L.cfg.teamSetup[k].cpu && !padPresent(L.cfg.teamSetup[k].pad)) return true;
    return false;
}
#endif

// false while a human team of this console lacks its pad and the controller applet (Switch) did not fix it
bool padsReady(const Lobby &L) {
#ifdef __SWITCH__
    return !padMissing(L) || controllerApplet(__builtin_popcount(L.padsUsed()));
#else
    (void)L;
    return true;
#endif
}

// W4M text without its /*token*/ markup and space runs (inline icons)
std::string plain(const char *s) {
    std::string o;
    for (const char *p = s; *p; p++) {
        if (p[0] == '/' && p[1] == '*')
            if (const char *e = strstr(p + 2, "*/")) { p = e + 1; continue; }
        if (*p != ' ' || o.empty() || o.back() != ' ') o += *p;
    }
    return o;
}

// a picture cropped to fill d (level pictures are square); false while it is not loaded
bool cover(const std::string &name, Rectangle d, Color tint = WHITE) {
    if (pending(name)) return true;
    Texture2D t = tex(name);
    if (!t.id) return false;
    float k = fmaxf(d.width / t.width, d.height / t.height), w = d.width / k, h = d.height / k;
    DrawTexturePro(t, {(t.width - w) / 2, (t.height - h) / 2, w, h}, d, {}, 0, tint);
    return true;
}

// ---- W4M list widgets of WXFE.CreateAGame (WXFE.CreateAGameList rows: Border_Normal 6 / Highlight 9 / Disabled 7, GapBetweenItems 2)

// The row border: the halves of fe/border_*'s circle (x 10..138, cut at its centre 74) cap the bar (x 162..496, whose ends match that centre column) stretched; d: the row's inside
void feBorderSprite(Rectangle d, const char *sprite) {
    Texture2D t = tex(sprite);
    if (!t.id) return DrawRectangleRounded(d, 1, 8, PANEL);
    float k = d.height / 92, cap = fminf(64 * k, d.width / 2), x0 = d.x - 10 * k, w = d.width + 20 * k, y0 = d.y - 16 * k, h = 128 * k;
    DrawTexturePro(t, {10, 0, 64, 128}, {x0, y0, cap, h}, {}, 0, WHITE);
    DrawTexturePro(t, {74, 0, 64, 128}, {x0 + w - cap, y0, cap, h}, {}, 0, WHITE);
    DrawTexturePro(t, {162, 0, 334, 128}, {x0 + cap, y0, w - 2 * cap, h}, {}, 0, WHITE);
}
void feBorder(Rectangle d, int state) {
    static const char *const NAME[3] = {"fe/border_normal", "fe/border_highlight", "fe/border_disabled"};
    feBorderSprite(d, NAME[state]);
}

// FE text in pixels: x anchored by align (0 left, 1 centre, 2 right), yc the line's centre; TEXT_X as every FE text
void feTextPx(const char *s, float x, float yc, float px, const Color *g, int align, Color edge = BC_BLACK) {
    rlPushMatrix();
    rlTranslatef(x, yc, 0);
    rlScalef(TEXT_X, 1, 1);
    textG(s, 0, -px / 2, px, g[0], g[1], align, edge);
    rlPopMatrix();
}
float feWidthPx(const char *s, float px) { return textWidth(s, px) * TEXT_X; }
// one cell of a W4M icon sheet (cols x rows cells, row-major: :0 top-left)
void feIcon(const char *sheet, int cols, int rows, int cell, Rectangle d, Color tint = WHITE) {
    Texture2D t = tex(sheet);
    float w = (float)t.width / cols, h = (float)t.height / rows;
    if (t.id) DrawTexturePro(t, {w * (cell % cols), h * (cell / cols), w, h}, d, {}, 0, tint);
}

// A one-line FE label at FE (x, y) (centre), tilted rz rad about its anchor
void feLine(const char *s, float x, float y, float size, const Color *g, int align, float rz = 0) {
    Vector2 p = fePos(x, y);
    rlPushMatrix();
    rlTranslatef(p.x, p.y, 0);
    rlRotatef(-rz * RAD2DEG, 0, 0, 1);
    feTextPx(s, 0, 0, size * FE_S, g, align);
    rlPopMatrix();
}

// A row box (FE centre, size) in pixels, spread like fePos
Rectangle feBox(float cx, float cy, float w, float h) {
    Vector2 a = fePos(cx - w / 2, cy + h / 2);
    return {a.x, a.y, w * FE_S * feK, h * FE_S};
}

// Tilts what is drawn while it lives about c, slid right by `slide` px (the row's incoming glide)
struct Tilt {
    Tilt(Vector2 c, float rz, float slide = 0) {
        rlPushMatrix();
        rlTranslatef(c.x + slide, c.y, 0);
        rlRotatef(-rz * RAD2DEG, 0, 0, 1);
        rlTranslatef(-c.x, -c.y, 0);
    }
    ~Tilt() { rlPopMatrix(); }
};
Vector2 centre(Rectangle r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// A list row with a label left (List_Lable_Blue) and a value right (List_Data_Orange); Solid_Black on the highlight (state 1), grey when disabled (2).
// arrows: textitem_arrows around the value; icon: a sheet cell drawn in place of the value
struct FeCell { const char *sheet; int cols, rows, cell; };
void feRow(Rectangle r, int state, const char *label, const char *value, bool arrows, const FeCell *icon = nullptr) {
    feBorder(r, state);
    bool hi = state == 1;
    const Color *lg = hi ? GC_BLACK : state == 2 ? GC_DISABLED : GC_LIST_LABLE_BLUE;
    const Color *vg = hi ? GC_BLACK : state == 2 ? GC_DISABLED : GC_LIST_DATA_ORANGE;
    float px = r.height * 0.62f, pad = r.height * 0.8f, cy = r.y + r.height / 2, ah = r.height * 0.6f, aw = ah * 0.5f, right = r.x + r.width - pad;
    float lw = feWidthPx(label, px), avail = r.width - 2 * pad - lw - 16 - (arrows ? 2 * aw + 8 : 0), vw = icon ? r.height * 0.8f : fminf(feWidthPx(value, px), fmaxf(avail, 0));
    Texture2D a = tex("fe/textitem_arrows");
    Color ac = hi ? Color{42, 22, 0, 255} : GC_LIST_DATA_ORANGE[0];
    if (arrows && a.id) DrawTexturePro(a, {32, 0, 32, 64}, {right - aw, cy - ah / 2, aw, ah}, {}, 0, ac), right -= aw + 6;
    Color edge = hi ? BC_NONE : BC_BLACK;  // black on the orange bar without the outline [assumed: readability]
    if (icon) feIcon(icon->sheet, icon->cols, icon->rows, icon->cell, {right - vw, cy - vw / 2, vw, vw});
    else marquee(value, {right - vw, cy - px, vw, 2 * px}, px, vg, 2, edge, now());  // a long value scrolls, never shrinks [user-requested]
    right -= vw;
    if (arrows && a.id) DrawTexturePro(a, {0, 0, 32, 64}, {right - aw - 6, cy - ah / 2, aw, ah}, {}, 0, ac);
    float room = fminf(lw, right - 16 - (r.x + pad));
    marquee(label, {r.x + pad, cy - px, fmaxf(room, 0), 2 * px}, px, lg, 0, edge, now());  // a long label scrolls too [user-requested]
}

// Blue divide, scrolling watermark and Title Control of the setup screens [data MENUTWKX WXFE.CreateAGame: AnimDivide$In_Curve, Scrolling 1-4, Title Control]; p: the
// panel's slide-in. The three steps (top right) are ours: W4M's CreateAGame is a single screen. In a room, W4M's lobby timer disc (WXNET.Lobby.Timer,
// TimerBack (-120, -105), `%.2d`, `--` with no deadline) sits on the panel's foot.
void setupBack(const char *title, int step, float t, float p, int timer) {
    float x = (1 - easeOut(p / 0.7f)) * -820;
    divide(x);
    watermark(title, x, t);
    titleControl(title, x);
    const char *steps[3] = {tr(nullptr, "1 Teams", "1 Équipes"), tr(nullptr, "2 Map", "2 Terrain"), tr(nullptr, "3 Rules", "3 Règles")};
    static const Color DIM[2] = {{151, 207, 255, 150}, {233, 255, 255, 150}};
    float px = 18 * FE_S, right = 1240;
    for (int i = 2; i >= 0; i--) {
        right -= feWidthPx(steps[i], px);
        feTextPx(steps[i], right, 132, px, i == step ? GC_BUTTON_YELLOW : DIM, 0, i == step ? BC_BLACK : Color{0, 0, 0, 90});
        right -= 26;
    }
    if (timer != -2) {
        Vector2 c = fePos(-120, -105);
        float r = 40;
        if (!image("fe/buttonsmall_normal", {c.x - r, c.y - r, 2 * r, 2 * r})) DrawCircleV(c, r, PANEL);
        feTextPx(timer < 0 ? "--" : TextFormat("%.2d", timer), c.x, c.y, 30 * FE_S * 0.8f, GC_BUTTON_YELLOW, 1);
    }
}

// The paper strip with a tooltip (warn: red), NAV Prev and NAV Start (start: 0 Normal, 2 Disabled); no button bar, W4M's ButtonHelp shows Select / Back only
void setupFront(float t, const char *tip, bool warn, int start) { paperStrip(t, true, tip, warn, start); }

// The body colour of pad p's controller: one disc, or two halves (left / right Joy-Con) when the pair differs; grey when the system gives none [ours]
bool padBody(int p, Color &l, Color &r) {
    if (!(Frontend::simPads >> p & 1)) return Controls::padColors(p, l, r);
    l = p ? Color{230, 40, 50, 255} : WHITE, r = p ? Color{40, 120, 230, 255} : WHITE;  // test pads: a white pad, a red / blue Joy-Con pair
    return true;
}

// A pad's disc: the controller colour with its number, as the Teams screen's tokens
void padToken(Vector2 at, float r, int p) {
    Color l = {200, 200, 200, 255}, rt = l;
    padBody(p, l, rt);
    Color col = mixColors(l, rt);
    bool bright = col.r * 0.3f + col.g * 0.59f + col.b * 0.11f > 150;
    DrawCircleV({at.x, at.y + r / 6}, r, {0, 0, 0, 110});
    DrawCircleV(at, r, BLACK), DrawCircleV(at, r * 7 / 8, WHITE);
    if (l.r == rt.r && l.g == rt.g && l.b == rt.b) DrawCircleV(at, r * 17 / 24, col);
    else DrawCircleSector(at, r * 17 / 24, 90, 270, 24, l), DrawCircleSector(at, r * 17 / 24, -90, 90, 24, rt);  // a Joy-Con pair: left | right
    feTextPx(TextFormat("%d", p + 1), at.x, at.y, r * 22 / 24, bright ? GC_BLACK : GC_WHITE, 1, bright ? BC_NONE : BC_BLACK);
}

// A pad's cursor token on the Teams screen: no number, filled with the pad's mixed body colour
void cursorToken(Vector2 at, float r, int p) {
    Color l = {200, 200, 200, 255}, rt = l;
    padBody(p, l, rt);
    DrawCircleV({at.x, at.y + r / 6}, r, {0, 0, 0, 110});
    DrawCircleV(at, r, BLACK), DrawCircleV(at, r * 7 / 8, WHITE), DrawCircleV(at, r * 17 / 24, mixColors(l, rt));
}

// A labelled column head (WXFE.HeaderList entries: FETXT.Teams, Landscape, GameStyle, Wormpot), List_Lable_Blue, tilted
void head(const char *label, float x, float y, float rz) { feLine(label, x, y, 22, GC_LIST_LABLE_BLUE, 1, rz); }

// A button legend under the paper strip: the essential buttons only (Switch glyphs, desktop keys) [ours]
struct LegendEntry { const char *pad, *key, *label, *en, *fr; };
void legend(const LegendEntry *all, int n) {
    const bool key = keyGlyphs();
    const float G = 30, px = 20, gap = 26;
    float total = -gap;
    auto text_ = [](const LegendEntry &e) { return tr(e.label, e.en, e.fr); };
    for (int i = 0; i < n; i++) total += glyphs(key ? all[i].key : all[i].pad, 0, 0, G, key, false) + 8 + feWidthPx(text_(all[i]), px) + gap;
    float x = 640 - total / 2;
    for (int i = 0; i < n; i++) {
        x += glyphs(key ? all[i].key : all[i].pad, x, 662, G, key) + 8;
        feTextPx(text_(all[i]), x, 662 + G / 2, px, GC_WHITE, 0);
        x += feWidthPx(text_(all[i]), px) + gap;
    }
}

const LegendEntry backLegend = {"B", "Esc", nullptr, "Back", "Retour"};

void teamsLegend() {
    static const LegendEntry L[] = {{"A", "Enter", nullptr, "Pick / settings", "Choisir / réglages"}, {"-", "F1", nullptr, "Remove a team", "Enlever une équipe"},
                                    {"X", "X", nullptr, "Controllers", "Manettes"}, {"+", "N", nullptr, "Next", "Suivant"}, backLegend};
    legend(L, 5);
}
}  // namespace

// a team's worm at the origin turned by yaw (the caller's matrix places it): its gloves, then Catalog::headgear at HatLocator
static void drawTeamWorm(const GameConfig::Team &tm, float yaw, const char *clip, float ct, bool loop) {
    const Catalog &k = catalog();
    Models::gloves(k.drawName(tm.gloves));
    Models::draw("worm", {0, 0, 0}, yaw, 0, WHITE, clip, ct, loop);
    Models::gloves(nullptr);
    if (Matrix m; Models::joint("worm", "HatLocator", clip, ct, loop, &m))
        for (const auto &hg : k.headgear(tm.hat, tm.glasses, tm.tash)) Models::draw(hg.model, MatrixMultiply(MatrixMultiply(hg.local, m), MatrixRotateY(yaw)));
}

// BeginTextureMode / EndTextureMode reset rlgl's current matrix: never call this under a pushed 2D matrix (Tilt)
void Frontend::wormPreview(int k, const GameConfig::Team &tm, float t) {
    if (!wormRt[k].id) wormRt[k] = LoadRenderTexture(256, 346);
    Camera3D cam = {{0, 1.0f, 3.7f}, {0, 0.8f, 0}, {0, 1, 0}, 30, CAMERA_PERSPECTIVE};
    BeginTextureMode(wormRt[k]);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    Lit::frame(cam.position);
    drawTeamWorm(tm, 0.7f * sinf(t * 0.6f + k * 1.7f), "Base", t, true);  // slow turn
    EndMode3D();
    EndTextureMode();
}

unsigned Frontend::simPads = 0;
std::string Frontend::simText;
Frontend::PadIn Frontend::sim[4];

Frontend::PadIn Frontend::padIn(int p) {
    PadIn in;
    if (simPads >> p & 1) return in = sim[p], sim[p] = {}, in;
    if (leaving >= 0 || !padPresent(p) || (editing && (!p || p == editPad))) return in;  // typing: the keyboard (pad 0) and the typing pad wait
    auto K = [p](std::initializer_list<int> k) { return p ? std::initializer_list<int>{} : k; };
    in.dy = pressed(p, {DOWN}, {}) - pressed(p, {UP}, {});
    in.dx = pressed(p, {RIGHT}, {}) - pressed(p, {LEFT}, {});
    in.card = pressed(p, {GAMEPAD_BUTTON_RIGHT_TRIGGER_1}, K({KEY_PAGE_DOWN})) - pressed(p, {GAMEPAD_BUTTON_LEFT_TRIGGER_1}, K({KEY_PAGE_UP}));
    in.minus = pressed(p, {MINUS}, K({KEY_F1})), in.pads = pressed(p, {GAMEPAD_BUTTON_RIGHT_FACE_UP}, K({KEY_X})), in.ok = pressed(p, {A}, K({KEY_ENTER, KEY_SPACE})), in.back = pressed(p, {B}, K({KEY_BACKSPACE, KEY_ESCAPE}));
    return in;
}

// One player slot (d) of the Teams screen: the ribbon in the team's alliance colour carrying its name ("Joueur N" on a free slot), who plays it (the pad's disc on the frame's bottom-left corner,
// the CPU level, another console's name), its live worm overflowing the bottom, the info row (shared alliance, handicap effect) under the ribbon, the worm count bottom right, in a room the
// ready grenade. A free slot shows how to pick a team.
void Frontend::teamSlot(Rectangle d, int k, float t) {
    const GameConfig &c = lobby.cfg;
    const GameConfig::Team &m = c.teamSetup[k];
    const bool play = lobby.playing(k), cpu = play && m.cpu, mine = play && lobby.canEdit(k) && !m.cpu;
    const float cx = d.x + d.width / 2;
    auto line = [](const char *s, float px, float x0, float x1, float cy, const Color *g, int align) { marquee(s, {x0, cy - px, x1 - x0, 2 * px}, px, g, align, BC_BLACK, now()); };
    if (!cardPanel(d)) DrawRectangleRounded(d, 0.1f, 6, PANEL);
    if (!play) DrawRectangleRounded(d, 0.1f, 6, {0, 20, 40, 110});
    const Pop *open = cardOf(k);
    float &fade = cardFade[k];
    fade = Clamp(fade + (open ? 1 : -1) * GetFrameTime() / 0.2f, 0, 1);
    // the card's content fades under the pad's open list; the ribbon, the pad disc and the ready flag stay
    auto chrome = [&] {
        if (fade > 0 && !cardPanel(d, Fade(WHITE, fade))) DrawRectangleRounded(d, 0.1f, 6, Fade(PANEL, fade));
        const char *rb = play ? m.name.c_str() : tr("FETXT.WaitingForPlayer", "Empty", "Vide");
        Rectangle r = {d.x + 16, d.y - 22, d.width - 32, 40};
        Tilt tl(centre(r), 0.035f);
        DrawRectangleRounded({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 0.5f, 8, BLACK);
        DrawRectangleRounded(r, 0.5f, 8, play ? TEAM_COLORS[c.alliance(k) & 3] : Color{110, 120, 130, 255});  // 0x4be910: the name in its alliance's colour
        line(rb, 24, r.x + 20, r.x + r.width - 20, r.y + r.height / 2, GC_WHITE, 1);
        if (mine && padPresent(m.pad)) padToken({d.x + 1, d.y + d.height - 1}, 21, m.pad);
        if (play && room()) navAt(0, lobby.ready[k] ? 0 : 2, {d.x + 10, d.y + 8, 34, 34});  // Team<n>_Ready: Nav Start, Normal when ready, Disabled when not
        if (open) cardList(d, *open);
    };
    if (!play) {  // "A to pick a team": the text wraps on two lines in the room the glyph leaves
        const bool key = keyGlyphs();
        const char *txt = tr(nullptr, "to pick a team", "pour choisir une équipe");
        float G = 40, gw = glyphs(key ? "Enter" : "A", 0, 0, G, key, false), room_ = d.width - gw - 36, y = d.y + d.height / 2 - 20;
        std::vector<std::string> ls(1);
        std::istringstream words(txt);
        for (std::string w; words >> w;) {
            std::string &l = ls.back();
            if (!l.empty() && feWidthPx((l + " " + w).c_str(), 20) > room_) ls.push_back(w);
            else l += (l.empty() ? "" : " ") + w;
        }
        float tw = 0;
        for (const std::string &l : ls) tw = fmaxf(tw, fminf(feWidthPx(l.c_str(), 20), room_));
        float x = cx - (gw + 10 + tw) / 2;
        glyphs(key ? "Enter" : "A", x, y, G, key);
        for (size_t i = 0; i < ls.size(); i++) line(ls[i].c_str(), 20, x + gw + 10, x + gw + 10 + tw, y + G / 2 + (i - (ls.size() - 1) / 2.0f) * 24, GC_WHITE, 0);
        return chrome();
    }
    const float wy = d.y + 88, x0 = d.x + 16;
    if (cpu) line(cpuText(m.cpu), 18, x0, d.x + d.width - 16, wy, GC_LIST_LABLE_BLUE, 0);
    else if (mine) {
        if (!padPresent(m.pad)) line(TextFormat(tr(nullptr, "Pad %d missing", "Manette %d absente"), m.pad + 1), 18, x0, d.x + d.width - 16, wy, GC_ERROR_RED, 0);
    } else {  // another console's player: its name
        auto pl = std::find_if(net->players.begin(), net->players.end(), [&](const NetPlayer &q) { return (int)q.id == lobby.owner[k]; });
        if (pl != net->players.end()) line(pl->name.c_str(), 18, x0, d.x + d.width - 16, wy, GC_DISABLED, 0);
    }
    DrawTexturePro(wormRt[k].texture, {0, 0, 256, -346}, {cx - 107.5f, d.y + d.height + 4 - 290, 215, 290}, {}, 0, Fade(WHITE, 1 - fade));
    if (m.handicap) {  // the handicap's real effect (lib_SetupMultiplayerTeam) with W4M's plus / minus icon (HandicapAllies:0 / 1), plain text on the card
        const int e = handicapHealth(c.scheme.health, m.handicap) - c.scheme.health;
        const char *txt = TextFormat(tr(nullptr, "%+d health", "%+d points de vie"), e);
        const float ic = 22, cy = d.y + 45, w = feWidthPx(txt, 18), x = fmaxf(d.x + 16, cx - (ic + 6 + w) / 2);
        feIcon("fe/teaminfo04", 4, 2, m.handicap < 0 ? 1 : 0, {x, cy - ic / 2, ic, ic});
        marquee(txt, {x + ic + 6, cy - 18, fminf(w, d.x + d.width - 16 - x - ic - 6), 36}, 18, GC_LIST_LABLE_BLUE, 0, BC_NONE, now());
    }
    {  // worm count, tilted display text [mockup]
        static const Color SH[2] = {{0, 0, 0, 110}, {0, 0, 0, 110}};
        const char *cnt = TextFormat("x%d", c.teamWorms(k));
        const Vector2 at = {d.x + d.width - 20, d.y + d.height - 24};
        Tilt tl(at, -0.21f);
        feTextPx(cnt, at.x, at.y + 5, 44, SH, 2, SH[0]);
        feTextPx(cnt, at.x, at.y, 44, GC_BUTTON_YELLOW, 2, BC_BLACK);
    }
    chrome();
}

// a saved team plays at most once per match (WXAllTeamAvailable 0x4bf9f0 compares the names); in a room, the teams this console just added count too
bool Frontend::placed(const std::string &name) const {
    for (int k = 0; k < Lobby::MAX_TEAMS && k < (int)lobby.cfg.teamSetup.size(); k++) if (lobby.playing(k) && lobby.cfg.teamSetup[k].name == name) return true;
    return room() && std::any_of(myTeams.begin(), myTeams.end(), [&](const GameConfig::Team &t) { return t.name == name; });
}

bool Frontend::slotTake(const Pop &p) const {
    if (p.kind != Pop::Slot || !lobby.playing(p.slot) || !lobby.canEdit(p.slot)) return false;
    const GameConfig::Team &m = lobby.cfg.teamSetup[p.slot];
    return !m.cpu && m.pad != p.pad;
}

const Frontend::Pop *Frontend::cardOf(int k) const {
    for (const Pop &c : cardPop) if (c.kind != Pop::None && c.slot == k) return &c;
    return nullptr;
}

// A pick may not go on: a room's earlier pick of this console is not in the host's LOBBY yet, the match is full, or the slot got a team
bool Frontend::pickBlocked(int slot) const {
    if (lobby.newWorms() < 1) return true;
    if (!room()) return lobby.playing(slot);
    int own = 0;
    for (int k = 0; k < lobby.cfg.teams; k++) own += lobby.canEdit(k);
    return lobby.cfg.teams >= Lobby::MAX_TEAMS || (int)myTeams.size() > own;
}

void Frontend::setPop(Pop p) {
    pop = std::move(p), pop.t0 = now();
    Audio::play(pop.kind == Pop::Pick ? Audio::Sfx::FeScaleHit : Audio::Sfx::FePopupIn);  // the editor pickers' Audio_Incoming In_ScaleHitXY
}

void Frontend::closePop() {
    const bool pick = pop.kind == Pop::Pick;
    pop = {};
    Audio::play(pick ? Audio::Sfx::HpTick : Audio::Sfx::FePopupOut);  // a picker's Messages_MenuGoingAway PlaySample$click3
}

// W4M FE.AddLTeamToGame: the saved team enters the slot with min(4, 16 - the worms already in) worms and the lowest free alliance (0x73620f, 0x737633)
void Frontend::placeTeam(int s, int pad, int index) {
    const SavedTeam &st = barracks.teams[index];
    const int worms = std::max(1, lobby.newWorms());
    if (room()) {  // the slot appears with the host's LOBBY; the alliance resolves by join order
        GameConfig::Team nt = st.slot(worms, -1, 0, customs);
        nt.pad = (uint8_t)pad;
        myTeams.push_back(nt);
        net->sendTeams(myTeams), cursors.slot[pad] = lobby.cfg.teams;  // the TEAMS message carries the team's weapon (slot(.., customs))
    } else lobby.place(s, pad, st.slot(worms, lobby.freeAlliance(), 0, customs));
}

void Frontend::releaseSlot(int s) {
    if (room()) {
        myTeams.erase(myTeams.begin() + net->lobby.teams[s].idx), net->sendTeams(myTeams);
        if (myTeams.empty()) net->setReady(false);
        cursors.removed(s);
        for (Pop &c : cardPop) if (c.slot > s) c.slot--;
    } else lobby.release(s);
}

// The saved-team list of a popup (WXFEP.InsertTeam / SelectTeam / the edit and delete lists): `use` says which teams it shows and what a row does
Frontend::Pop Frontend::teamsPop(Pop::Use use, int pad, int slot, int arg) const {
    Pop p;
    p.kind = Pop::Teams, p.use = use, p.pad = pad, p.slot = slot, p.arg = arg;
    using L = Barracks::List;
    p.ids = barracks.list(use == Pop::Insert ? L::Insert : use == Pop::Edit ? L::Edit : use == Pop::Delete ? L::Delete : L::Human, [&](const std::string &n) { return placed(n); },
                          !room() || lobby.isHost());  // a CPU team: the host's in a room [ours]
    if ((use == Pop::Insert || use == Pop::Mission) && barracks.canCreate()) p.ids.push_back(-1);  // Create Team
    return p;
}

void Frontend::openTeams(Pop::Use use, int pad, int slot, int arg) { setPop(teamsPop(use, pad, slot, arg)); }

// PrepareTeam$CreateNew: "player controlled?" (not where CPU teams are off), the CPU level, the name, then the editor
void Frontend::startCreate(Pop::Use use) {
    Pop p = pop;
    p.use = use, p.row = 0, p.err = false, p.typing = false, p.name.clear(), p.skill = 0;
    const bool cpuOk = use == Pop::Create || (use == Pop::Insert && (!room() || lobby.isHost()));
    p.kind = cpuOk ? Pop::Ask : Pop::Name;
    setPop(p);
}

static std::string trimmed(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

// A popup's input. The Teams screen hands in its pad's presses, every other screen any pad's.
void Frontend::popInput(PadIn in, float t) {
    (void)t;
    const int n = pop.kind == Pop::Teams ? (int)pop.ids.size() : pop.kind == Pop::Cpu ? 5 : pop.kind == Pop::Slot ? 4 + slotTake() : pop.kind == Pop::Pick ? (int)pop.vals.size() : 2;
    if (pop.kind != Pop::Name && in.dy && n) pop.row = clampWrap(pop.row + in.dy, n), Audio::play(pop.kind == Pop::Pick ? Audio::Sfx::FeChange : Audio::Sfx::FeHighlight);  // a ListControl's Audio_Highlighted Click2
    switch (pop.kind) {
    case Pop::Teams: {
        if (in.back) closePop();
        else if (in.ok && n) {
            int idx = pop.ids[pop.row];
            if (idx < 0) startCreate(pop.use);
            else {
                const Pop::Use use = pop.use;
                const int slot = pop.slot, pad = pop.pad, arg = pop.arg;
                if (use == Pop::Delete) { Pop p = pop; p.kind = Pop::Confirm, p.arg = idx, p.row = 0; setPop(p); break; }
                closePop();
                Audio::play(Audio::Sfx::FeClick);
                if (use == Pop::Insert) placeTeam(slot, pad, idx);
                else if (use == Pop::Mission) missionTeam = barracks.teams[idx].name, missionTab = arg, go(Missions);
                else openEditor(idx, screen, -1, 0, Pop::Edit);
            }
        }
        break;
    }
    case Pop::Ask:  // WXFEP.NewTeamControl: No is the default row, Yes a player-controlled team
        if (in.dx) pop.row ^= 1;
        if (in.back) closePop();
        else if (in.ok) {
            Pop p = pop;
            p.kind = pop.row ? Pop::Name : Pop::Cpu, p.row = 0, p.skill = 0;
            setPop(p);
        }
        break;
    case Pop::Cpu:  // WXFEP.NewTeamControlAI: CPU Level 1..5
        if (in.back) closePop();
        else if (in.ok) {
            Pop p = pop;
            p.kind = Pop::Name, p.skill = pop.row + 1, p.typing = p.err = false;
            setPop(p);
        }
        break;
    case Pop::Name: {  // WXFEP.NewTeamName: typed at once; an empty or taken name (USER.NameCheck) is refused
        auto finish = [&] {
            std::string nm = trimmed(pop.name);
            if (nm.empty() || barracks.find(nm) >= 0) return pop.err = true, Audio::play(Audio::Sfx::FeError), void();
            int idx = barracks.create(nm, pop.skill, tr("FE.RandomWormNames", "Alex,Sam,Jo,Max,Kim,Lee,Ray,Zed", nullptr), language ? "Flag.France" : "Flag.UK", (uint32_t)(GetTime() * 1000) + (uint32_t)barracks.teams.size());
            if (idx < 0) return pop.err = true, Audio::play(Audio::Sfx::FeError), void();
            barracks.save(DATA_DIR "teams.txt");
            const Pop::Use use = pop.use;
            const int slot = pop.slot, pad = pop.pad, arg = pop.arg;
            closePop();
            reopenArg = arg;
            openEditor(idx, screen, use == Pop::Insert ? slot : -1, pad < 0 ? 0 : pad, use);
        };
        if (pop.err) {
            if (in.back) closePop();
            else if (in.ok) pop.err = false;
            break;
        }
        if (!pop.typing) {
            pop.typing = true, editPad = pop.pad < 0 ? 0 : pop.pad;
#ifdef __SWITCH__
            bool done = edit(pop.name, "Team name");
            pop.typing = false;
            if (done) finish();
            else closePop();
#else
            edit(pop.name, "Team name");
#endif
        }
#ifndef __SWITCH__
        else if (!editing) pop.typing = false, finish();
#endif
        break;
    }
    case Pop::Slot: {  // WXFEP.TeamSettingsList: worms, allies, handicap, [Control with this pad], Close
        if (!lobby.playing(pop.slot) || !lobby.canEdit(pop.slot)) { closePop(); break; }
        GameConfig::Team &m = lobby.cfg.teamSetup[pop.slot];
        const int take = slotTake() ? 3 : -1, close = 3 + (take >= 0);
        int d = in.dx ? in.dx : in.ok && pop.row < 3 ? 1 : 0;
        if (d && pop.row == 0) {
            int mx = lobby.maxWorms(pop.slot), v = lobby.cfg.teamWorms(pop.slot) + d;
            m.worms = (uint8_t)(v > mx ? 1 : v < 1 ? mx : v);
        } else if (d && pop.row == 1) m.alliance = (int8_t)clampWrap(lobby.cfg.alliance(pop.slot) + d, 4);
        else if (d && pop.row == 2) m.handicap = (int8_t)(m.handicap + d > 1 ? -1 : m.handicap + d < -1 ? 1 : m.handicap + d);
        if (d) Audio::play(Audio::Sfx::FeChange);
        if (in.ok && pop.row == take) m.pad = (uint8_t)pop.pad, Audio::play(Audio::Sfx::FeClick), closePop();
        else if ((in.ok && pop.row == close) || in.back) closePop();
        break;
    }
    case Pop::Confirm:  // WXFEP.ConfirmDeleteTeam / QuitTeamOptions: No is the default row
        if (in.dx) pop.row ^= 1;
        if (in.back || (in.ok && !pop.row)) closePop();
        else if (in.ok) {
            const Pop::Use use = pop.use;
            const int idx = pop.arg;
            closePop();
            if (use == Pop::Delete) barracks.remove(idx), barracks.save(DATA_DIR "teams.txt");
            else leaveEditor(false);
        }
        break;
    case Pop::Pick: {  // the editor's pickers: the highlight moves the preview only, A sets the field (the row's Messages_Selected), B leaves it
        const int vis = pickRows(pop.use);
        pop.top = Clamp(pop.top, pop.row - vis + 1, pop.row);
        if (in.back) closePop();
        else if (in.ok) {
            if (n) setField(pop.use, pop.vals[pop.row]);
            closePop();
        }
        break;
    }
    case Pop::None: break;
    }
}

// ---- the popups' look: W4M's paper popup, Button_Yellow title over its underline, the list rows of the Teams screen

namespace {
// title and rows (label left, value right) of a list popup, the highlight row scrolling into the window
void listPanel(const char *title, const std::vector<std::string> &labels, const std::vector<std::string> &values, int sel) {
    const int n = (int)labels.size(), vis = std::min(std::max(n, 1), 8);
    const float rh = 44, w = 560, h = 112 + vis * rh;
    Rectangle d = {640 - w / 2, 360 - h / 2 - 10, w, h};
    popup(d);
    marquee(title, {d.x + 50, d.y + 10, w - 100, 60}, 30, GC_BUTTON_YELLOW, 1, BC_BLACK, now());
    if (!image("fe/title_underline", {640 - 220, d.y + 62, 440, 22})) DrawRectangle(640 - 220, d.y + 72, 440, 3, WHITE);
    const int first = Clamp(sel - vis / 2, 0, std::max(0, n - vis));
    for (int i = first; i < n && i < first + vis; i++) feRow({d.x + 40, d.y + 96 + (i - first) * rh, w - 80, 38}, i == sel ? 1 : 0, labels[i].c_str(), values[i].c_str(), false);
    if (n > vis) {  // scroll bar
        float bh = (h - 120) * vis / n;
        DrawRectangleRounded({d.x + w - 30, d.y + 96 + (h - 120 - bh) * first / (n - vis), 6, bh}, 1, 4, {255, 255, 255, 150});
    }
}
}  // namespace

// A pad's open list inside its card: the slot options (WXFEP.TeamSettingsList rows) or the saved teams (WXFEP.InsertTeam rows), W4M list rows that scale in one after the other
void Frontend::cardList(Rectangle d, const Pop &c) {
    const float rh = 44, top = d.y + 30, x = d.x + 12, w = d.width - 24, since = now() - c.t0;
    auto inRow = [&](int i, auto draw) {  // In_ScaleY of the row
        const float a = rowAppear(since, -1, i, 0.04f);
        if (a <= 0) return;
        const float cy = top + i * rh + 19;
        rlPushMatrix(), rlTranslatef(0, cy, 0), rlScalef(1, a, 1), rlTranslatef(0, -cy, 0);
        draw();
        rlPopMatrix();
    };
    if (c.kind == Pop::Slot) {
        if (!lobby.playing(c.slot)) return;
        const GameConfig::Team &m = lobby.cfg.teamSetup[c.slot];
        std::vector<const char *> lab = {tr("FETXT.Worms", "Worms", "Vers"), tr("FETXT.Team", "Team", "Équipe"), tr("FETXT.Handicap", "Handicap", "Handicap")};
        if (slotTake(c)) lab.push_back(tr(nullptr, "Control with this pad", "Contrôler avec cette manette"));
        lab.push_back(tr("FETXT.Close", "Close", nullptr));
        // 0x4c0640: the rows' values are icons: x1..x6 (WXFE.TeamInfo01:0..3, 02:0..1), the alliance's colour (WXFE.HandicapAllies:3..6), the handicap (:1 -1, :2 0, :0 +1)
        const int nw = lobby.cfg.teamWorms(c.slot);
        const FeCell cell[3] = {{nw <= 4 ? "fe/teaminfo01" : "fe/teaminfo02", 2, 2, (nw - 1) % 4}, {"fe/teaminfo04", 4, 2, 3 + (lobby.cfg.alliance(c.slot) & 3)},
                                {"fe/teaminfo04", 4, 2, m.handicap < 0 ? 1 : m.handicap > 0 ? 0 : 2}};
        const int last = (int)lab.size() - 1;
        for (int i = 0; i < last; i++)
            inRow(i, [&] { feRow({x, top + i * rh, w, 38}, i == c.row ? 1 : 0, lab[i], "", i < 3 && i == c.row, i < 3 ? &cell[i] : nullptr); });
        inRow(last, [&] {  // Close: W4M's popup text button (ConfirmList row 26: centred, List_Lable_Blue, Solid_White + charcoal brush on the highlight)
            const float px = 22, tw = feWidthPx(lab[last], px), cy = top + last * rh + 19, cx = d.x + d.width / 2;
            if (c.row == last) brush({cx - tw / 2 - px * 0.45f, cy - px * 0.55f, tw + px * 0.9f, px * 1.15f});
            feTextPx(lab[last], cx, cy, px, c.row == last ? GC_WHITE : GC_LIST_LABLE_BLUE, 1, c.row == last ? BC_NONE : BC_BLACK);
        });
        return;
    }
    if (c.kind != Pop::Teams) return;
    const int n = (int)c.ids.size(), vis = std::max(1, (int)((d.y + d.height - 14 - top) / rh)), first = Clamp(c.row - vis / 2, 0, std::max(0, n - vis));
    for (int i = first; i < n && i < first + vis; i++) {
        const int id = c.ids[i];
        const std::string lab = id < 0 ? tr("FETXT.CreateTeam", "Create Team", "Créer une équipe") : barracks.teams[id].name;
        const std::string val = id >= 0 && barracks.teams[id].skill ? cpuText(barracks.teams[id].skill) : "";
        inRow(i - first, [&] { feRow({x, top + (i - first) * rh, w - (n > vis ? 8 : 0), 38}, i == c.row ? 1 : 0, lab.c_str(), val.c_str(), false); });
    }
    if (n > vis) {  // scroll bar
        const float bh = vis * rh * vis / n;
        DrawRectangleRounded({d.x + d.width - 14, top + (vis * rh - bh) * first / (n - vis), 5, bh}, 1, 4, {255, 255, 255, 150});
    }
}

void Frontend::popDraw(float t) {
    if (pop.kind == Pop::None) return;
    if (pop.kind == Pop::Pick) return pickDraw();
    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 120});
    const float k = backOut((now() - pop.t0) / 0.2f);
    rlPushMatrix();
    rlTranslatef(640, 360, 0), rlScalef(k, k, 1), rlTranslatef(-640, -360, 0);
    FeFlat flat;
    std::vector<std::string> labels, values;
    switch (pop.kind) {
    case Pop::Teams: {
        for (int i : pop.ids) {
            labels.push_back(i < 0 ? tr("FETXT.CreateTeam", "Create Team", "Créer une équipe") : barracks.teams[i].name);
            values.push_back(i >= 0 && barracks.teams[i].skill ? cpuText(barracks.teams[i].skill) : "");
        }
        const char *title = pop.use == Pop::Insert ? tr("FETXT.SelectTeam", "Select a team", nullptr)
                          : pop.use == Pop::Mission ? (pop.arg ? tr("FETXT.SelectChallengeTeam", "Select a challenge team", nullptr) : tr("FETXT.SelectStoryTeam", "Select a mission team", nullptr))
                          : pop.use == Pop::Edit ? tr("FETXT.SelectTeamEdit", "Select a team to edit", nullptr) : tr("FETXT.SelectTeamDelete", "Select a team to delete", nullptr);
        listPanel(title, labels, values, pop.row);
        break;
    }
    case Pop::Ask: yesNo(tr("FETXT.CPUControl", "Do you want this team to be player controlled?", nullptr), pop.row, t); break;
    case Pop::Cpu:
        for (int i = 1; i <= 5; i++) labels.push_back(cpuText(i)), values.push_back("");
        listPanel(tr("FETXT.TeamController", "Who has control?", nullptr), labels, values, pop.row);
        break;
    case Pop::Name: {
        Rectangle d = {340, 230, 600, 230};
        popup(d);
        feTextPx(tr("FETXT.EnterTeamName", "Enter team name", nullptr), 640, d.y + 44, 30, GC_BUTTON_YELLOW, 1);
        std::string v = pop.name + (pop.typing && fmodf(t, 1) < 0.5f ? "_" : "");
        feRow({d.x + 50, d.y + 96, d.width - 100, 44}, 1, "", v.c_str(), false);
        const char *note = pop.err ? tr(nullptr, "This name is empty or already used: A types again, B cancels", "Ce nom est vide ou déjà pris : A saisit à nouveau, B annule") : "";
        feTextPx(note, 640, d.y + 180, 18, GC_ERROR_RED, 1);
        break;
    }
    case Pop::Slot: break;
    case Pop::Confirm: yesNo(pop.use == Pop::Delete ? tr("FETXT.ConfirmDeleteThis", "Are you sure you want to delete this?", nullptr) : tr(nullptr, "Discard the changes?", "Abandonner les modifications ?"), pop.row, t); break;
    case Pop::Pick: break;
    case Pop::None: break;
    }
    rlPopMatrix();
}


// ---- the team editor (W4M WXFE.TeamOptions, docs/w4m/frontend.md "Saved teams"): 3 pages on a working copy of the saved team

void Frontend::openEditor(int index, Screen ret, int placeSlot, int placePad, Pop::Use after) {
    ed = {};
    ed.index = index, ed.t = barracks.teams[index], ed.ret = ret, ed.placeSlot = placeSlot, ed.placePad = placePad, ed.after = after;
    Progress p;
    p.open(DATA_DIR "progress.txt");
    ed.unlocks = p.unlocks;
    go(TeamEdit);
}

// the team's string field a popup / row edits; null for the grave (an index)
std::string *Frontend::fieldRef(Pop::Use u) {
    SavedTeam &t = ed.t;
    return u == Pop::Soundbank ? &t.speech : u == Pop::Head ? &t.hat : u == Pop::Eyes ? &t.glasses : u == Pop::Face ? &t.tash : u == Pop::Hands ? &t.gloves : u == Pop::Weapon ? &t.weapon
         : u == Pop::Flag ? &t.flag : nullptr;
}

std::string Frontend::getField(Pop::Use u) { const std::string *f = fieldRef(u); return f ? *f : std::to_string(ed.t.grave); }

void Frontend::setField(Pop::Use u, const std::string &v) {
    if (std::string *f = fieldRef(u)) *f = v;
    else ed.t.grave = atoi(v.c_str());
    ed.changed = true;
}

// the ids a field offers, W4M order: the speech banks we have, the attachments of its type the save has unlocked ("" none first), the stock Weapon Factory weapons and ours, the flags, the graves
std::vector<std::string> Frontend::fieldValues(Pop::Use u) {
    const Catalog &k = catalog();
    std::vector<std::string> v;
    if (u == Pop::Soundbank) {
        for (const Catalog::Item &s : k.speech) if (voiceBankOf(s.id) >= 0 && std::find(v.begin(), v.end(), s.id) == v.end()) v.push_back(s.id);
    } else if (u == Pop::Head || u == Pop::Hands || u == Pop::Eyes || u == Pop::Face) v = k.usable(u == Pop::Head ? 0 : u == Pop::Hands ? 1 : u == Pop::Eyes ? 2 : 3, ed.unlocks, getField(u));
    else if (u == Pop::Weapon) {
        v.push_back("");
        for (const std::string &w : k.stockWeapons) v.push_back(w);
        for (const WeaponDef &w : customs) v.push_back(w.name);
    } else if (u == Pop::Flag) for (const Catalog::Item &f : k.flags) v.push_back(f.id);
    else for (size_t i = 0; i < k.graves.size(); i++) v.push_back(std::to_string(i));
    if (std::find(v.begin(), v.end(), getField(u)) == v.end()) v.push_back(getField(u));
    return v;
}

std::string Frontend::fieldLabel(Pop::Use u, const std::string &id) const {
    const Catalog &k = catalog();
    if (u == Pop::Soundbank) {  // several W4M rows share a bank (voclassi): the plain FETXT.SB.<bank> one names it [assumed: W4M's own choice is not decoded]
        const Catalog::Item *row = nullptr;
        for (const Catalog::Item &s : k.speech)
            if (s.id == id && (!row || (s.text.compare(0, 9, "FETXT.SB.") == 0 && s.text.find("DLC1") == std::string::npos && row->text.compare(0, 9, "FETXT.SB.") != 0))) row = &s;
        if (row) return tr(row->text.c_str(), id.c_str());
    }
    if (u == Pop::Head || u == Pop::Hands || u == Pop::Eyes || u == Pop::Face) return hatLabel(id);
    if (u == Pop::Weapon) return id.empty() ? tr("FETXT.None2", "None", "Aucune") : tr(id.c_str(), id.c_str());
    if (u == Pop::Flag) { const Catalog::Item *f = k.flag(id); return f ? tr(f->text.c_str(), id.c_str()) : id; }
    if (u == Pop::Grave) { size_t i = (size_t)atoi(id.c_str()); return i < k.graves.size() ? tr(k.graves[i].text.c_str(), k.graves[i].id.c_str()) : id; }
    return id;
}

// the rows a picker's ListControl shows: its box (65 FE half high, the soundbank's 101) in 27 FE rows (RowHeight 25 + GapBetweenItems 2)
int Frontend::pickRows(Pop::Use u) { return u == Pop::Soundbank ? 8 : 5; }

// W4M's ListControl SortContents: the rows by their text, byte order (é and É after z), the "none" row ("") first [observed: the FR hat and grave lists]
void Frontend::pickList(Pop::Use u) {
    Pop p;
    p.kind = Pop::Pick, p.use = u, p.vals = fieldValues(u);
    std::stable_sort(p.vals.begin(), p.vals.end(), [&](const std::string &a, const std::string &b) {
        return a.empty() != b.empty() ? a.empty() : fieldLabel(u, a) < fieldLabel(u, b);
    });
    p.row = (int)(std::find(p.vals.begin(), p.vals.end(), getField(u)) - p.vals.begin());
    p.top = std::max(0, p.row - pickRows(u) + 1);
    setPop(p);
}

void Frontend::leaveEditor(bool accepted) {
    if (accepted) barracks.teams[ed.index] = ed.t, barracks.save(DATA_DIR "teams.txt");
    if (ed.after == Pop::Insert && ed.placeSlot >= 0 && !placed(ed.t.name) && lobby.newWorms() >= 1 && (room() ? (int)myTeams.size() < Lobby::MAX_TEAMS : !lobby.playing(ed.placeSlot)))
        placeTeam(ed.placeSlot, ed.placePad, ed.index);
    if (ed.after == Pop::Mission) reopen = (int)ed.after;
    go(ed.ret);
}

namespace {
// ---- the team editor's widgets (MENUTWKX WXFE.TeamOptions / TeamPage1..3, docs/w4m/frontend.md "Team editor layout"): FE positions spread like
// every FE item (fePos), sizes in FE units
const Color GC_TEXTEDIT_TURQUOISE[2] = {{125, 228, 252, 255}, {207, 236, 255, 255}};

// one FE text line about p (px) of `size` FE; maxW > 0: W4M's ScaleTextToFit shrinks it to that many FE (its W4M-sized columns keep this mild)
void edText(const char *s, Vector2 p, float size, const Color *g, int align, float maxW = 0, Color edge = BC_BLACK, int anim = 0) {
    float px = size * FE_S, w = feWidthPx(s, px);
    if (maxW > 0 && w > maxW * FE_S) px *= maxW * FE_S / w;
    std::vector<Vector2> pose = anim ? textPose(anim, s) : std::vector<Vector2>();
    rlPushMatrix(), rlTranslatef(p.x, p.y, 0), rlScalef(TEXT_X, 1, 1);
    textG(s, 0, -px / 2, px, g[0], g[1], align, edge, anim ? &pose : nullptr);
    rlPopMatrix();
}

// WXFE.HeaderList row: ListSubDivide:0 (20 x 5), the FETXTSH text (FE.Font 20, Solid_White, MediumWobble, ScaleTextToFit to 80 % of the row), ListSubDivide:1
void edHeader(const char *s, float x, float y, float hw, float rz) {
    Vector2 c = fePos(x, y);
    Tilt tl(c, rz);
    float px = 20 * FE_S, w = fminf(feWidthPx(s, px), 2 * hw * 0.8f * FE_S), d = 40 * FE_S, h = 10 * FE_S, gap = 2 * FE_S;
    feIcon("fe/subtitle_divide", 2, 1, 0, {c.x - w / 2 - gap - d, c.y - h / 2, d, h});
    feIcon("fe/subtitle_divide", 2, 1, 1, {c.x + w / 2 + gap, c.y - h / 2, d, h});
    edText(s, c, 20, GC_WHITE, 1, 2 * hw * 0.8f, BC_BLACK, 4);
}

// a W4M text border (kMT_Text*: fe/border_<name>) whose drawn sprite spans outer (FE) about c [observed: the box sizes of the reference shots]
void edBox(Vector2 c, float outW, float outH, const char *sprite) {
    float h = outH * FE_S * 92 / 128, w = outW * FE_S - 16 * h / 92;
    feBorderSprite({c.x - w / 2, c.y - h / 2, w, h}, sprite);
}

// WXFE.TeamPage2(a)List row (RowHeight 25): label left (List_Lable_Blue, ScaleTextToFit to at most maxL of the row), a kGAP_Line leader
// (WXFE.PaddedLine 4 FE high, 0x78372e) and the value right (List_Data_Orange, at most maxV); Border_Highlight 11 = the charcoal brush, both texts
// Solid_White on it, Disabled_Grey when the row's Enable is off; arrows: textitem_arrows around the value (PrepareTeam$Hat+ / Hat- ...)
void edRow(float x, float y, float rz, const char *label, const char *value, bool hi, bool off, bool arrows, Color line, float maxL = 0.4f, float maxV = 0.75f) {
    Vector2 c = fePos(x, y);
    Tilt tl(c, rz);
    const float hw = 130 * FE_S, in = 5 * FE_S, px = 20 * FE_S, l = c.x - hw + in, r = c.x + hw - in;  // texts 5 FE inside the row [observed]
    if (hi) brush({c.x - hw, c.y - 12.5f * FE_S, 2 * hw, 25 * FE_S});
    const Color *lg = off ? GC_DISABLED : hi ? GC_WHITE : GC_LIST_LABLE_BLUE, *vg = off ? GC_DISABLED : hi ? GC_WHITE : GC_LIST_DATA_ORANGE;
    // the value keeps its size, the label fits in what is left [observed: the reference Soundbank row's label is the one scaled down]
    const float inner = r - l, gap = 6 * FE_S, aw = arrows ? 10 * FE_S : 0, vw = fminf(feWidthPx(value, px), maxV * inner);
    const float lw = fminf(feWidthPx(label, px), fminf(maxL * inner, inner - vw - gap - 2 * aw));
    edText(label, {l, c.y}, 20, lg, 0, lw / FE_S);
    edText(value, {r - aw, c.y}, 20, vg, 2, vw / FE_S);
    if (arrows) {
        Texture2D a = tex("fe/textitem_arrows");
        float ah = 16 * FE_S;
        if (a.id) DrawTexturePro(a, {0, 0, 32, 64}, {r - 2 * aw - vw, c.y - ah / 2, aw, ah}, {}, 0, WHITE), DrawTexturePro(a, {32, 0, 32, 64}, {r - aw, c.y - ah / 2, aw, ah}, {}, 0, WHITE);
    }
    float x0 = l + lw + gap, x1 = r - vw - 2 * aw - gap;
    if (x1 > x0 && !image("fe/padded_line", {x0, c.y - 2 * FE_S, x1 - x0, 4 * FE_S}, off ? Color{128, 128, 128, 255} : line)) DrawRectangle(x0, c.y - 1, x1 - x0, 2, line);
}

// WXFE_Border_Bubble (kMT_BubbleBorder 36, 0x77cfe7): "Speech Popup.tga" cut in 10 quads around the SpeechPopup Paper fill, FE half extents sx, sy
// (the popup's Scale + the 16 FE border): corners 38 x 38, the sides 13 wide in two halves each, top / bottom 16 high, the fill (sx - 8.5, sy - 9),
// 0.5 FE per sprite pixel; the pointer (the right-pointing notch) at the right edge, at the y of the item that opened it [observed: its place]
void bubble(Vector2 c, float sx, float sy, float pointerY) {
    Texture2D t = tex("fe/speech_popup");
    auto quad = [&](float x, float y, float hx, float hy, Rectangle uv) {
        if (t.id) DrawTexturePro(t, uv, {c.x + (x - hx) * FE_S, c.y - (y + hy) * FE_S, 2 * hx * FE_S, 2 * hy * FE_S}, {}, 0, WHITE);
    };
    Texture2D f = tex("fe/speechpopup_paper");
    if (f.id) DrawTexturePro(f, {0, 0, (float)f.width, (float)f.height}, {c.x - (sx - 8.5f) * FE_S, c.y - (sy - 9) * FE_S, 2 * (sx - 8.5f) * FE_S, 2 * (sy - 9) * FE_S}, {}, 0, WHITE);
    else DrawRectangle(c.x - sx * FE_S, c.y - sy * FE_S, 2 * sx * FE_S, 2 * sy * FE_S, PANEL);
    const float e = (sy - 38) / 2;
    quad(-sx + 4.5f, e, 6.5f, e, {6, 0, 26, 256}), quad(-sx + 5.5f, -e, 6.5f, e, {38, 0, 26, 256});
    quad(sx - 9.5f, e, 6.5f, e, {64, 0, 26, 256}), quad(sx - 9.5f, -e, 6.5f, e, {94, 0, 26, 256});
    quad(0, sy - 8, sx - 38, 8, {132, 4, 270, 32}), quad(0, -sy + 7, sx - 38, 8, {132, 42, 270, 32});
    quad(-sx + 19, sy - 19, 19, 19, {126, 78, 76, 76}), quad(sx - 19, sy - 19, 19, 19, {218, 78, 76, 76});
    quad(-sx + 19, -sy + 19, 19, 19, {126, 178, 76, 76}), quad(sx - 19, -sy + 19, 19, 19, {218, 178, 76, 76});
    quad(sx - 6, pointerY, 24, 30, {298, 128, 96, 120});
}

// a decorative ImageView of a W4M picker (WXFE.Scratches1 = 5 cells, Scratches2 = 3): FE offset, half extents, rz, alpha
struct Scratch { int sheet, cell; float x, y, hw, hh, rz; unsigned char a; };
void scratches(Vector2 c, const Scratch *s, int n) {
    for (int i = 0; i < n; i++) {
        Vector2 p = {c.x + s[i].x * FE_S, c.y - s[i].y * FE_S};
        Tilt tl(p, s[i].rz);
        feIcon(s[i].sheet == 1 ? "fe/scratches01" : "fe/scratches02", s[i].sheet == 1 ? 5 : 3, 1, s[i].cell, {p.x - s[i].hw * FE_S, p.y - s[i].hh * FE_S, 2 * s[i].hw * FE_S, 2 * s[i].hh * FE_S},
               {255, 255, 255, s[i].a});
    }
}
}  // namespace

// TheWorm (WXFE_WormDesc: Position (-155, -60, 100), Scale 120, Orientation y 0.4) through the FE camera. 0x758553 hangs the worm under a node at
// -Position, a chain not decoded: placed (-115, -105, 100) x 195 turned 0.9 rad to match the reference shots [observed]; slide: In_TitleUnderline's x (FE)
void Frontend::editorWorm(const char *clip, float ct, bool loop, float slide) {
    Camera3D cam = {{0, 0, 903.5f}, {0, 0, 0}, {0, 1, 0}, 0.54f * RAD2DEG, CAMERA_PERSPECTIVE};
    glClear(0x100);
    rlSetClipPlanes(5, 15000);
    BeginMode3D(cam);
    Lit::frame(cam.position);
    rlPushMatrix();
    rlTranslatef(-115 + slide, -105, 100);
    rlScalef(195, 195, 195);
    drawTeamWorm(ed.t.slot(), 0.9f, clip, ct, loop);
    rlPopMatrix();
    EndMode3D();
    rlSetClipPlanes(0.01, 1000);
}

// W4M WXFEP.Outfit / SelectGrave / SelectFlag / SelectWeapon / SelectSoundbank: BorderType 36 bubble, In_ScaleY, the title box, PopUpDivide, the
// highlighted row's preview, the list (ListControl: rows 25 + 2 FE, centred, List_Lable_Blue, the highlight Solid_White on the charcoal brush) [data MENUTWKX]
void Frontend::pickDraw() {
    const Pop::Use u = pop.use;
    const bool outfit = u == Pop::Head || u == Pop::Eyes || u == Pop::Face || u == Pop::Hands;
    struct Look { const char *key, *en; float titleY, divY, listY, listHW, listHH, hh; };
    static const Look OUTFIT = {"FETXT.SelectAccessory", "Select Accessory", 138, 111, -75, 90, 65, 160}, GRAVE = {"FETXT.SelectGrave", "Select a grave", 140, 122, -75, 90, 65, 160},
                      FLAG = {"FETXT.SelectFlag", "Select a flag", 140, 122, -75, 90, 65, 160}, WEAPON = {"FETXT.SelectWeapon", "Select a weapon", 150, 132, -83, 90, 65, 170},
                      SOUND = {"FETXT.SelectSoundbank", "Select Soundbank", 136, 108, -30, 100, 101, 160};
    const Look &k = outfit ? OUTFIT : u == Pop::Grave ? GRAVE : u == Pop::Flag ? FLAG : u == Pop::Weapon ? WEAPON : SOUND;
    DrawRectangle(0, 0, 1280, 720, outfit ? Color{0, 0, 0, 100} : Color{40, 40, 60, 160});  // FullScreenColour
    // the row that opened it: TeamPage2 Detail 2 / 6..First, TeamPage3 DETAIL Weapon / Flag / First
    const float srcY = u == Pop::Soundbank ? 90 : u == Pop::Head ? -4 : u == Pop::Eyes ? -26 : u == Pop::Face ? -48 : u == Pop::Hands ? -70 : u == Pop::Weapon ? 102 : u == Pop::Flag ? 25 : -57;
    const Vector2 c = fePos(-125, 0);  // [observed: the reference shots put the pickers' centre at FE x -125, their data Position is (75, 0)]
    const float since = now() - pop.t0;
    if (!feItem(c, &IN_SCALEY, 0, nullptr, since, -1)) return;
    bubble(c, 110 + 16, k.hh + 16, srcY);
    static const Scratch S_OUT[] = {{2, 0, 0, 50, 80, 80, 0, 50}, {1, 1, 97, -161, 25, 35, 0, 255}, {1, 3, -120, -123, 15, 35, 0, 255}, {1, 0, -122, 66, 15, 55, -0.14f, 255}, {1, 4, 109, 103, 15, 35, 0.06f, 255}},
                         S_GRAVE[] = {{2, 1, 12, 47, 70, 90, 0, 50}, {1, 1, 109, -157, 15, 35, 0, 255}, {1, 3, -121, 118, 15, 35, 0.11f, 200}, {2, 0, -27, 27, 44, 32, 0, 80}},
                         S_FLAG[] = {{1, 1, 106, -160, 15, 35, 0, 255}, {1, 3, -121, 120, 15, 35, 0.11f, 200}, {2, 1, 12, 37, 70, 90, 0, 50}, {2, 2, 20, 41, 92, 112, 0, 100}, {2, 0, -37, 47, 44, 52, 0, 100}},
                         S_WEAPON[] = {{1, 1, 106, -170, 15, 35, 0, 255}, {1, 3, -121, 50, 15, 35, 0.11f, 200}, {2, 1, 19, 57, 70, 90, 0, 50}, {2, 2, -46, 9, 49, 43, 0.28f, 150}},
                         S_SOUND[] = {{1, 1, -107, 166, 15, 35, 0, 255}, {1, 3, -119, -120, 15, 35, 0, 255}, {1, 4, 110, 31, 15, 35, 0, 255}, {2, 1, 109, -158, 15, 25, 0, 255}, {2, 2, 9, -165, 30, 30, 0, 255}};
    if (outfit) scratches(c, S_OUT, 5);
    else if (u == Pop::Grave) scratches(c, S_GRAVE, 4);
    else if (u == Pop::Flag) scratches(c, S_FLAG, 5);
    else if (u == Pop::Weapon) scratches(c, S_WEAPON, 4);
    else scratches(c, S_SOUND, 5);
    FeBox title;  // PopUp Title: 90 x 20, FontSizeOverride 20, Button_Yellow, MediumWobble, AutoScale
    title.c = {-125, k.titleY}, title.hw = 90, title.hh = 20, title.size = 20, title.autoScale = true, title.grad = GC_BUTTON_YELLOW, title.anim = 4;
    feText(tr(k.key, k.en), title);
    image("fe/speechpopup_divide", {c.x - 70 * FE_S, c.y - (k.divY + 8) * FE_S, 140 * FE_S, 16 * FE_S});  // DIVIDE1, WXFE.PopUpDivide 70 x 8
    // the list: as many 27 FE rows as the box holds, scrolled to keep the highlight in view; the ListNavigation arrows (Arrow 32 x 12, (165, 214, 252)) past either end
    const int n = (int)pop.vals.size(), vis = pickRows(u);
    const float top = k.listY + k.listHH - 13.5f;
    for (int j = 0; j < vis && pop.top + j < n; j++) {
        const int i = pop.top + j;
        const std::string lab = outfit && pop.vals[i].empty() ? tr("FETXT.Nothing", "Nothing") : fieldLabel(u, pop.vals[i]);  // 0x7391dc's first row
        Vector2 p = {c.x, c.y - (top - 27 * j) * FE_S};
        float w = fminf(feWidthPx(lab.c_str(), 20 * FE_S), 2 * k.listHW * FE_S);
        if (i == pop.row) brush({p.x - w / 2 - 4 * FE_S, p.y - 11 * FE_S, w + 8 * FE_S, 22 * FE_S});
        edText(lab.c_str(), p, 20, i == pop.row ? GC_WHITE : GC_LIST_LABLE_BLUE, 1, 2 * k.listHW);
    }
    const Color ar = {165, 214, 252, 255};
    if (pop.top > 0) feIcon("fe/speechpopup_navigation", 1, 2, 0, {c.x - 16 * FE_S, c.y - (k.listY + k.listHH + 15 + 6) * FE_S, 32 * FE_S, 12 * FE_S}, ar);
    if (pop.top + vis < n) feIcon("fe/speechpopup_navigation", 1, 2, 1, {c.x - 16 * FE_S, c.y - (k.listY - k.listHH - 15 + 6) * FE_S, 32 * FE_S, 12 * FE_S}, ar);
    rlPopMatrix();
    const std::string cur = pop.row < (int)pop.vals.size() ? pop.vals[pop.row] : "";
    const Catalog &cat = catalog();
    if (outfit && since >= 0.225f) {  // Attachment (In_ScaleHitXY at 225 ms): WXD.CurrentAttachment at (0, 90) scale 6, moved / scaled / turned by the row's FrontEndOffsets (0x738e38); Nothing: (0, -30)
        // gloves show their FrontEndMeshID (Gloves.<x>P), the others their worn mesh
        if (const Catalog::Item *a = cur.empty() ? nullptr : cat.attachment(cur))
            feMesh(a->type == 1 ? a->feModel.c_str() : cat.drawName(cur), {-125 * feK + a->fePos[0], 90 + a->fePos[1], a->fePos[2]}, {6 * a->feScale[0], 6 * a->feScale[1], 6 * a->feScale[2]},
                   {a->feRot[0], a->feRot[1], a->feRot[2]}, true, nullptr, 0);
    } else if (u == Pop::Grave) {  // Grave Mesh: WXD.CurrentMesh at (0, 22) scale 4, orientation (-0.29, -2.43, 0.2); our grave models are in metres (W4M units x 0.05)
        size_t g = (size_t)atoi(cur.c_str());
        if (g < cat.graves.size()) feMesh(cat.graves[g].model.c_str(), {-125 * feK, 22, 0}, {80, 80, 80}, {-0.29f, -2.43f, 0.2f}, true, nullptr, 0);
    } else if (u == Pop::Weapon) {  // the weapon's held graphic [assumed: W4M picker placement not decoded; position and scale picked to fill the preview area]
        const WeaponDef *w = nullptr;
        for (const WeaponDef &d : cat.stock) if (d.name == cur) w = &d;
        for (const WeaponDef &d : customs) if (!w && d.name == cur) w = &d;
        if (w && !w->graphic.empty() && since >= 0.225f) feWeapon(w->graphic, {-125 * feK, 35, 0}, 3, {0.2f, -0.6f, 0});
    } else if (u == Pop::Flag) {  // Flag Mesh: WXFE_FlagObject at (19, 59), orientation (0.35, -0.2, -0.1), FlagScale (9, 8, 8): the HUD.Flag cloth, clip Go looped, the flag's TGA on it
        if (const Catalog::Item *f = cat.flag(cur)) {
            Texture2D ft = flagTex(f->model);
            Models::texture(&ft);
            feMesh("hud_flag", {-125 * feK + 19, 59, 0}, {9, 8, 8}, {0.35f, -0.2f, -0.1f}, false, "Go", fmodf((float)GetTime(), 1.5f) * FE_ANIM_SPEED);
            Models::texture(nullptr);
        }
    }
}

void Frontend::editor(PadIn in, float t) {
    Models::upload(GetTime() + (Models::has("worm") ? 0.004 : 0.03));  // TheWorm is the first thing the page shows: its model comes in before the rest
    // the row of an editor page -> the field it edits (NoField: a worm name or the speech test)
    static const Pop::Use P2[6] = {Pop::Soundbank, Pop::NoField, Pop::Head, Pop::Eyes, Pop::Face, Pop::Hands}, P3[3] = {Pop::Weapon, Pop::Flag, Pop::Grave};
    auto pageField = [&](int page, int row) { return page == 1 ? P2[row] : page == 2 ? P3[row] : Pop::NoField; };
    const Catalog::Item *hat = catalog().attachment(ed.t.hat);
    auto off = [&](int page, int row) { return page == 1 && ((row == 3 && hat && !hat->glasses) || (row == 4 && hat && !hat->tash)); };  // TeamPage2aList Enable WXD.EyesOk / TashOk
    const int rows = ed.page == 2 ? 3 : 6;
    if (ed.pageT == -100) ed.pageT = entered, ed.pageSfx = entered != -100;  // DisplayCurrentPage on entry (a --ui capture shows it settled)
    if (in.card) {
        int np = Clamp(ed.page + in.card, 0, 2);
        if (np != ed.page) {  // PrepareTeam$PageNext / PagePrev: 0x7398ee removes the page (no Out anim) and adds the next one
            ed.page = np, ed.row = std::min(ed.row, (np == 2 ? 3 : 6) - 1), ed.pageT = t, ed.pageSfx = true;
            while (off(ed.page, ed.row)) ed.row--;
            Audio::play(Audio::Sfx::FeClick);  // the arrow's Audio_Activated 39 (kAUDIO_Page_Turn is the book's) [assumed: our L / R stand for clicking it]
        }
    }
    if (ed.pageSfx && t - ed.pageT >= 0.2f) ed.pageSfx = false, Audio::play(Audio::Sfx::FeNextIn);  // the page's Audio_Incoming In_Next, at its 200 ms delay
    if (in.dy) {
        int r = ed.row;
        do r = clampWrap(r + in.dy, rows);
        while (off(ed.page, r) && r != ed.row);
        ed.row = r, Audio::play(Audio::Sfx::FeHighlight);
    }
    const Pop::Use u = pageField(ed.page, ed.row);
    static int shownRow = -1, shownPage = -1;
    if (ed.row != shownRow || ed.page != shownPage) {  // the rows' Messages_Highlighted: WXD.WormAnim Salute (Flag), Pray (Grave), InspectHands (Hands)
        shownRow = ed.row, shownPage = ed.page;
        const char *a = u == Pop::Flag ? "Salute" : u == Pop::Grave ? "Pray" : u == Pop::Hands ? "InspectHands" : nullptr;
        if (a && Models::clipLength("worm", a) > 0) ed.anim = a, ed.animT = t;
    }
    if (ed.page == 0 && in.ok) edit(ed.t.worms[ed.row], "Worm name"), ed.changed = true, editPad = 0;
    else if (ed.page == 1 && ed.row == 1 && in.ok) {  // FE.PlayRandomSpeech
        static const Audio::Voice LINES[] = {Audio::Voice::Fire, Audio::Voice::Hurt, Audio::Voice::Victory, Audio::Voice::Jump, Audio::Voice::Idle};
        Audio::setTeamVoice(0, voiceBankOf(ed.t.speech));
        Audio::voice(0, LINES[GetRandomValue(0, 4)]);
    } else if (u != Pop::NoField && in.ok) pickList(u);
    else if ((u == Pop::Head || u == Pop::Eyes || u == Pop::Face) && in.dx) {  // PrepareTeam$Hat+ / Hat- / Glasses+- / Tash+- (Gloves have none, 0x742ab0)
        std::vector<std::string> v = fieldValues(u);
        int i = (int)(std::find(v.begin(), v.end(), getField(u)) - v.begin());
        setField(u, v[clampWrap(i + in.dx, (int)v.size())]);
        Audio::play(Audio::Sfx::FeChange);
    }
    if (in.plus && ed.changed) leaveEditor(true);  // Accept Changes: EnableId TM.Accept
    else if (in.back) {
        if (!ed.changed) leaveEditor(false);  // Request^Leave^WXFE.TeamOptions: TM.Accept off -> CancelEdit, else WXFEP.QuitTeamOptions (0x749be0)
        else setPop([&] { Pop p; p.kind = Pop::Confirm, p.use = Pop::Quit; return p; }());
    }

    // TheWorm's clip: WXD.WormAnim once, else Base, and every 5..12 s one of its RandomAnims (MinElapsTimeSecs / MaxElapsTimeSecs)
    static const char *const RANDOM[] = {"Gunslinger1", "Bored", "BringItOn", "CountFingers", "FlickBogey", "WiggleBrows", "Wave"};
    if (ed.anim != "Base" && t - ed.animT >= Models::clipLength("worm", ed.anim.c_str())) ed.anim = "Base", ed.animT = t, ed.idleUntil = t + GetRandomValue(5, 12);
    if (ed.anim == "Base" && t >= ed.idleUntil) {
        if (ed.idleUntil > 0) ed.anim = RANDOM[GetRandomValue(0, 6)], ed.animT = t;
        else ed.idleUntil = t + GetRandomValue(5, 12);
    }

    // WXFE.TeamOptions: the divide, watermarks and Title Control of a submenu, TheWorm and its Shadow, SUBHEADER Teamname, the page, LABEL Page, BUTTON Prev / Next,
    // the strip with the ticker, Cancel Edit (NAV Prev) and Accept Changes (NAV Tick) [data]
    const float p = subIn(t), x = (1 - easeOut(p / 0.7f)) * -820, since = t - entered;
    const char *title = tr("FETXTH.TeamDesign", "TEAM DESIGN", "CONCEPTION D'ÉQUIPE");
    divide(x);
    watermark(title, x, t);
    {
        FeFlat flat;  // the left panel's items keep W4M's x (docs/w4m/frontend.md "16:9 spread"); TheWorm (LayerOffset 2) under Title Control and Teamname (3)
        float slide = since < 0.3f ? -1000 : clipKeys(K_TITLE_IN_X, 2, since - 0.3f, true);  // TheWorm / Shadow / Teamname: In_TitleUnderline after 300 / 350 ms
        if (since >= 0.3f) {
            {
                Vector2 sc = fePos(-194 + slide, -89);
                Tilt tl(sc, 0.02f);
                image("fe/shadow", {sc.x - 130 * FE_S, sc.y - 77 * FE_S, 260 * FE_S, 154 * FE_S});  // Shadow: WXFE.Shadow 130 x 77
            }
            editorWorm(ed.anim.c_str(), t - ed.animT, ed.anim == "Base", slide);  // 3D: never under a pushed 2D matrix (rlgl's transform)
        }
        titleControl(title, x);
        if (since >= 0.35f) {  // SUBHEADER Teamname: SubHeader_Blue, MediumWobble, FontSizeOverride 23, Justification 3 (middle left) in a 110 x 15 box
            float s2 = clipKeys(K_TITLE_IN_X, 2, since - 0.35f, true);
            edText(ed.t.name.c_str(), fePos(-181 - 110 + s2, 145), 23, GC_SUBHEADER_BLUE, 0, 220, BC_BLACK, 4);
        }
    }
    // the page (WXFE.TeamPage1 at (-40, 40), Page 2 / 3 at (0, 0)): In_ScaleY after its 200 ms Delay_Incoming; its children's In_SlideX count from the AddItem
    const float ps = t - ed.pageT, ox = ed.page == 0 ? -40 : 0, oy = ed.page == 0 ? 40 : 0;
    auto at = [&](float px, float py) { return fePos(ox + px, oy + py); };
    auto slideIn = [&](float px, float py, float delay) { return feItem(at(px, py), &IN_SLIDEX, delay, nullptr, ps, -1); };
    if (feItem(fePos(ox, oy), &IN_SCALEY, 0.2f, nullptr, ps, -1)) {
        auto paper = [&](float px, float py, float hw, float hh, float rz) { Vector2 q = at(px, py); Tilt tl(q, rz); image("fe/paperpopup02", {q.x - hw * FE_S, q.y - hh * FE_S, 2 * hw * FE_S, 2 * hh * FE_S}); };
        if (ed.page == 0) {
            paper(164, -27, 155, 151, 0.01f);
            if (slideIn(169, 96, 0.2f)) edHeader(tr("FETXTSH.Worms", "Worms"), ox + 169, oy + 96, 100, -0.02f), rlPopMatrix();
            // Worm1..6: TextEditBox, Border Edit / Highlight / Active, TextEdit_Turquoise (Solid_Black highlighted), centred, In_SlideX at 300..500 ms
            static const float W[6][4] = {{171, 62, -0.02f, 0.3f}, {164, 28, 0, 0.35f}, {177, -7, -0.03f, 0.4f}, {160, -40, -0.02f, 0.45f}, {171, -73, 0.03f, 0.45f}, {169, -109, -0.01f, 0.5f}};
            for (int i = 0; i < 6; i++) {
                if (!slideIn(W[i][0], W[i][1], W[i][3])) continue;
                Vector2 q = at(W[i][0], W[i][1]);
                {
                    Tilt tl(q, W[i][2]);
                    const bool hi = i == ed.row, typing = editing == &ed.t.worms[i];
                    edBox(q, 220, 39, typing ? "fe/border_active" : hi ? "fe/border_highlight" : "fe/border_edit");
                    std::string v = ed.t.worms[i] + (typing && fmodf(t, 1) < 0.5f ? "_" : "");
                    edText(v.c_str(), q, 20, hi && !typing ? GC_BLACK : GC_TEXTEDIT_TURQUOISE, 1, 180, hi && !typing ? BC_NONE : BC_BLACK);
                }
                rlPopMatrix();
            }
        } else if (ed.page == 1) {
            paper(130, 13, 160, 146, -0.03f);
            edHeader(tr("FETXTSH.SpeechSettings", "Speech Settings"), 133, 121, 130, -0.03f);
            edHeader(tr("FETXTSH.WormSettings", "Worm Settings"), 130, 27, 130, -0.03f);
            const Color white = WHITE;
            edRow(132, 90, -0.03f, tr("FETXT.Soundbank", "Soundbank"), fieldLabel(Pop::Soundbank, ed.t.speech).c_str(), ed.row == 0, false, false, white, 1, 1);  // the Soundbank row's columns are 100 % wide
            {  // Test Speech: the text right-justified, then WXFE.IconSpeech (10 x 10); the brush fits them (BorderFitContents)
                Vector2 q = fePos(131, 68);
                Tilt tl(q, -0.02f);
                const char *ts = tr("FETXT.TestSpeech", "Test Speech");
                float r = q.x + 125 * FE_S, ic = 20 * FE_S, tw = feWidthPx(ts, 20 * FE_S);
                if (ed.row == 1) brush({r - ic - 4 * FE_S - tw, q.y - 12.5f * FE_S, tw + ic + 4 * FE_S, 25 * FE_S});
                edText(ts, {r - ic - 4 * FE_S, q.y}, 20, ed.row == 1 ? GC_WHITE : GC_LIST_DATA_ORANGE, 2);
                image("fe/icon_speechtest", {r - ic, q.y - ic / 2, ic, ic});
            }
            static const float R[4][2] = {{129, -4}, {128, -26}, {127, -48}, {127, -70}};
            static const Pop::Use F[4] = {Pop::Head, Pop::Eyes, Pop::Face, Pop::Hands};
            static const char *const KEY[4][2] = {{"FETXT.Head", "Head"}, {"FETXT.Eyes", "Eyes"}, {"FETXT.Face", "Face"}, {"FETXT.Hands", "Hands"}};
            for (int i = 0; i < 4; i++)
                edRow(R[i][0], R[i][1], -0.03f, tr(KEY[i][0], KEY[i][1]), fieldLabel(F[i], getField(F[i])).c_str(), ed.row == i + 2, off(1, i + 2), ed.row == i + 2 && i < 3,
                      i == 3 ? Color{100, 100, 255, 80} : white);  // the Hands row's leader is Colour_Normal (100, 100, 255, 80)
        } else {
            paper(125, 16, 150, 150, 0.03f);
            static const struct { float x, y, hw, rz, delay; const char *key, *en; } H[3] = {{125, 136, 120, 0.01f, 0.1f, "FETXTSH.TeamWeapon", "Team Weapon"},
                                                                                          {110, 57, 120, 0.03f, 0.2f, "FETXTSH.TeamFlag", "Team Flag"},
                                                                                          {131, -23, 120, 0.04f, 0.3f, "FETXTSH.TeamGrave", "Team Grave"}};
            static const float D[3][4] = {{132, 102, -0.01f, 0.15f}, {110, 25, 0, 0.25f}, {135, -57, 0, 0.35f}};
            for (int i = 0; i < 3; i++) {
                if (slideIn(H[i].x, H[i].y, H[i].delay)) edHeader(tr(H[i].key, H[i].en), H[i].x, H[i].y, H[i].hw, H[i].rz), rlPopMatrix();
                if (!slideIn(D[i][0], D[i][1], D[i][3])) continue;
                Vector2 q = at(D[i][0], D[i][1]);
                {  // TeamPage3List row: Border 6 / 9, the value centred, TextBox_Yellow (Solid_Black highlighted), ScaleTextToFit
                    Tilt tl(q, D[i][2]);
                    const bool hi = i == ed.row;
                    edBox(q, 214, 29, hi ? "fe/border_highlight" : "fe/border_normal");
                    edText(fieldLabel(P3[i], getField(P3[i])).c_str(), q, 20, hi ? GC_BLACK : GC_TEXTBOX_YELLOW, 1, 180, hi ? BC_NONE : BC_BLACK);
                }
                rlPopMatrix();
            }
        }
        rlPopMatrix();
    }
    // LABEL Page (124, -116) 50 x 20, FontSizeOverride 21, Solid_White, AutoScale: "Page n" + "/3"; BUTTON Prev / Next (kMT_NavArrow*, EnableId EnableTeamPagePrev / Next)
    FeBox pg;
    pg.c = {124, -116}, pg.hw = 50, pg.hh = 20, pg.size = 21, pg.rot = -0.03f, pg.autoScale = pg.oneLine = true;
    feText(std::string(tr(TextFormat("FETXT.Page%d", ed.page + 1), TextFormat("Page %d", ed.page + 1))) + "/3", pg);
    pageArrow(false, ed.page == 0 ? 2 : 0, {60, -116}, 20);
    pageArrow(true, ed.page == 2 ? 2 : 0, {190, -118}, 20);
    {  // the buttons that turn the page [ours]: L / R, PgUp / PgDn on the keyboard
        const float G = 24;
        const bool key = keyGlyphs();
        Vector2 a = fePos(60, -116), b = fePos(190, -118);
        glyphs(key ? "PgUp" : "L", a.x - 20 * FE_S - 8 - glyphs(key ? "PgUp" : "L", 0, 0, G, key, false), a.y - G / 2, G, key);
        glyphs(key ? "PgDn" : "R", b.x + 20 * FE_S + 8, b.y - G / 2, G, key);
    }
    paperStrip(t, true);
    navButton(1, ed.changed ? 0 : 2, {258, -166}, 40);
}

bool Frontend::setupTeams(bool plus, float t) {
    GameConfig &c = lobby.cfg;
    Models::upload(GetTime() + (Models::has("worm") ? 0.004 : 0.03));  // worm and hats are no menu assets: a plain launch uploads them only here or at the first match (the worm first)
    if (!cursors.placed && (!room() || c.teams)) {  // each token starts on the first slot its pad plays, else on the first free one
        cursors.placed = true;
        for (int p = 0; p < 4; p++) {
            int own = -1, free_ = -1;
            for (int k = Lobby::MAX_TEAMS - 1; k >= 0; k--) {
                if (lobby.ownedBy(k, p)) own = k;
                if (!lobby.playing(k)) free_ = k;
            }
            cursors.slot[p] = own >= 0 ? own : std::max(free_, 0), tokenAt[p] = {-1, -1};
        }
    }
    unsigned present = 0;
    for (int p = 0; p < 4; p++) present |= padPresent(p) << p;
    if (lobby.rehome(present) && !room()) saveSetup(c);
    bool back = false, leave = false, openApplet = false;
    for (int p = 0; p < 4; p++) {
        PadIn in = padIn(p);
        int &s = cursors.slot[p];
        if (pop.kind != Pop::None) {  // a full-screen popup (Create Team) belongs to the pad that opened it
            if (pop.pad == p) popInput(in, t);
            continue;
        }
        if (cardPop[p].kind != Pop::None) {  // this pad's open card: only this pad's cursor drives it
            Pop &cp = cardPop[p];
            if (cp.kind == Pop::Teams && lobby.playing(cp.slot)) {  // a room's host filled the slot
                cp = {}, cursors.unclaim(p);
                continue;
            }
            if (cp.kind == Pop::Teams) {  // what the other pads picked meanwhile leaves the list
                cp.ids.erase(std::remove_if(cp.ids.begin(), cp.ids.end(), [&](int i) { return i >= 0 && placed(barracks.teams[i].name); }), cp.ids.end());
                cp.row = Clamp(cp.row, 0, std::max(0, (int)cp.ids.size() - 1));
            }
            const bool picks = cp.kind == Pop::Teams && in.ok && cp.row < (int)cp.ids.size() && cp.ids[cp.row] >= 0;
            if (picks && pickBlocked(cp.slot)) Audio::play(Audio::Sfx::FeError), in.ok = false;
            std::swap(pop, cp);
            popInput(in, t);
            std::swap(pop, cp);
            if (cp.kind != Pop::None && cp.kind != Pop::Teams && cp.kind != Pop::Slot) std::swap(pop, cp), cp = {};  // Create Team: the full-screen flow
            cursors.open[p] = cp.kind == Pop::None ? -1 : cp.slot;
            continue;
        }
        if (in.card || in.dx) cursors.move(p, in.card ? in.card : in.dx), Audio::play(Audio::Sfx::FeChange);
        const bool freeSlot = !lobby.playing(s), mineSlot = lobby.playing(s) && lobby.canEdit(s);
        const bool taken = cardOf(s);  // another pad has this card open: A and B do nothing there
        if (taken) {
            if (in.ok || in.back) Audio::play(Audio::Sfx::FeError);
            continue;
        }
        if (in.ok && freeSlot) {
            if (pickBlocked(s)) Audio::play(Audio::Sfx::FeError);
            else cursors.claim(p, s), cardPop[p] = teamsPop(Pop::Insert, p, s, 0), cardPop[p].t0 = now(), Audio::play(Audio::Sfx::FePopupIn);
        } else if (in.ok && mineSlot) {
            cursors.claim(p, s);
            cardPop[p].kind = Pop::Slot, cardPop[p].pad = p, cardPop[p].slot = s, cardPop[p].row = 0, cardPop[p].t0 = now();
            Audio::play(Audio::Sfx::FePopupIn);
        }
        if (in.pads) openApplet = true;
        if (in.back) back = true;
        else if (in.minus && mineSlot) releaseSlot(s), Audio::play(Audio::Sfx::FeClick);  // - takes the team under the cursor out, like Remove Team
    }
    leave = back && room();
    const bool goOn = plus && padsReady(lobby) && (room() || lobby.canStart());
    if (back || goOn)
        for (int p = 0; p < 4; p++) cardPop[p] = {}, cursors.unclaim(p);
    if (back && !room()) saveSetup(c), go(Local);
    if (goOn) screen = SetupMap;
#ifdef __SWITCH__
    if (openApplet) controllerApplet(__builtin_popcount(lobby.padsUsed()));
#endif

    const bool settled = from >= SetupTeams && from <= SetupRules && leaving < 0;  // coming from a sibling step: no slide-in
    float since = t - entered, out = leaving >= 0 ? t - leaving : -1;
    auto glide = [&](int i) { return settled ? 1.0f : rowAppear(since, out, i + 3); };
    setupBack(room() ? tr("FETXTH.GameLobby", "GAME LOBBY", "SALON") : tr("FETXTH.LOCALGAME", "LOCAL GAME", "PARTIE LOCALE"), 0, t, settled ? 1 : subIn(t), room() ? timer : -2);
    FeFlat flat;
    for (int k = 0; k < Lobby::MAX_TEAMS; k++)
        if (lobby.playing(k) && glide(k + 1) > 0) wormPreview(k, c.teamSetup[k], t);
    const float W = (1200 - 3 * 14) / 4.0f, top = 196, H = 322;
    auto slotRect = [&](int k) { return Rectangle{40 + k * (W + 14), top, W, H}; };
    for (int k = 0; k < Lobby::MAX_TEAMS; k++) {
        float al = glide(k + 1);
        if (al <= 0) continue;
        Rectangle d = slotRect(k);
        Tilt tl(centre(d), 0, (al - 1) * 500);
        teamSlot(d, k, t);
    }
    if (glide(Lobby::MAX_TEAMS + 1) >= 1) {  // one token per connected pad, at the top-right corner of its slot; several share a slot side by side
        for (int p = 0; p < 4; p++) {
            if (!padPresent(p)) continue;
            int nth = 0;
            for (int q = 0; q < p; q++) nth += padPresent(q) && cursors.slot[q] == cursors.slot[p];
            Rectangle d = slotRect(cursors.slot[p]);
            Vector2 to = {d.x + d.width + 4, d.y - 8 + nth * 44}, &at = tokenAt[p];
            at = at.x < 0 ? to : Vector2{at.x + (to.x - at.x) * fminf(1, GetFrameTime() * 16), at.y + (to.y - at.y) * fminf(1, GetFrameTime() * 16)};
            cursorToken(at, 20, p);
        }
    }
    // tooltip: how to move the token; the red flag when the match cannot go on [ours]
    const char *tip = tr("FETIP.InsertTeam", "Select a team to play.", nullptr);
    bool warn = false;
    if (!room() && lobby.count() < 2) warn = true, tip = tr(nullptr, "Two players are needed to play", "Il faut deux joueurs pour jouer");
    else if (!room() && !lobby.canStart()) warn = true, tip = tr(nullptr, "The teams need two alliances at least", "Il faut au moins deux alliances");
    for (int k = 0; k < Lobby::MAX_TEAMS && !warn; k++)
        if (lobby.playing(k) && lobby.canEdit(k) && !c.teamSetup[k].cpu && !padPresent(c.teamSetup[k].pad))
            warn = true, tip = TextFormat(tr(nullptr, "Pad %d missing: + opens the controller screen", "Manette %d absente : + ouvre l'écran des manettes"), c.teamSetup[k].pad + 1);
    setupFront(t, tip, warn, -1);
    teamsLegend();
    return leave;
}

void Frontend::setupMap(int dx, int dy, bool back, bool plus, const std::vector<std::string> &maps) {
    const int n = (int)maps.size();
    if (n && lobby.canEditMatch()) {  // one cursor for every pad: the map is the host's single choice
        mapSel = clampWrap(mapSel + dx + 3 * dy, n);
        if (live && P({YB}, {KEY_Y})) mapSel = GetRandomValue(0, n - 1);
        lobby.cfg.map = maps[mapSel];
    }
    if (back) screen = SetupTeams;
    if (plus) screen = SetupRules;
    float t = now();

    const bool settled = from >= SetupTeams && from <= SetupRules && leaving < 0;
    setupBack(room() ? tr("FETXTH.GameLobby", "GAME LOBBY", "SALON") : tr("FETXTH.LOCALGAME", "LOCAL GAME", "PARTIE LOCALE"), 1, t, settled ? 1 : subIn(t), room() ? timer : -2);
    FeFlat flat;
    {  // the big preview on the left, in its ButtonBig frame, the name on its foot
        feTextPx(TextFormat("%s  %d / %d", tr("FETXT.Landscape", "Landscape", "Terrain"), mapSel + 1, n), 64, 164, 24, GC_LIST_LABLE_BLUE, 0);
        Rectangle pv = {80, 208, 430, 280};
        if (!cover(preview(lobby.cfg.map), pv)) DrawRectangleRec(pv, {30, 60, 40, 255});
        DrawRectangleGradientV((int)pv.x, (int)(pv.y + pv.height - 82), (int)pv.width, 82, {0, 0, 0, 0}, {0, 0, 0, 170});
        pictureFrame(pv, false);
        const std::string title = mapTitle(lobby.cfg.map);
        marquee(title.c_str(), {pv.x + 18, pv.y + pv.height - 26 - 34, pv.width - 36, 68}, 34, GC_WHITE, 0, BC_BLACK, now());
    }
    // the landscape list: 3 columns of tiles, the highlight frame on the selection
    const int cols = 3, rows = (n + cols - 1) / cols, vis = 4, first = Clamp(mapSel / cols - 1, 0, std::max(0, rows - vis));
    const float x0 = 664, y0 = 148, tw = (1240 - x0 - 2 * 14) / 3.0f, th = 82;
    for (int i = first * cols; i < std::min(n, (first + vis) * cols); i++) {
        Rectangle d = {x0 + (i % cols) * (tw + 14), y0 + (i / cols - first) * (th + 14), tw, th};
        if (!cover(preview(maps[i]), d)) DrawRectangleRec(d, {30, 60, 40, 255});
        DrawRectangleGradientV((int)d.x, (int)d.y + 40, (int)d.width, 42, {0, 0, 0, 0}, {0, 0, 0, 170});
        pictureFrame(d, i == mapSel);
        const std::string nm = mapTitle(maps[i]);  // a long name scrolls, never shrinks [user-requested]
        marquee(nm.c_str(), {d.x + 8, d.y + th - 15 - 16, d.width - 16, 32}, 16, i == mapSel ? GC_BUTTON_YELLOW : GC_WHITE, 0, BC_BLACK, now());
    }
    setupFront(t, lobby.canEditMatch() ? tr("FETIP.Landscape2", "Select a landscape to battle on.", nullptr) : tr(nullptr, "The host picks the map", "L'hôte choisit le terrain"), false, 0);
    const LegendEntry L[] = {{"Y", "Y", nullptr, "Random", "Au hasard"}, {"+", "N", nullptr, "Next", "Suivant"}, backLegend};
    const bool host = lobby.canEditMatch();
    legend(L + !host, 3 - !host);
}

bool Frontend::setupRules(int dx, int dy, bool ok, bool back, bool plus) {
    GameConfig &c = lobby.cfg;
    enum { Scheme_, FirstRule, Pot = FirstRule + RULES, Rows };
    rulesRow = clampWrap(rulesRow + dy, Rows);
    int preset = presetOf(c.scheme), np = (int)SCHEMES.size();
    bool mayEdit = lobby.canEditMatch();
    if (!mayEdit) dx = 0, ok = false;  // read-only: the rows and their help stay readable
    if (rulesRow == Scheme_ && dx) c.scheme = SCHEMES[clampWrap(preset < 0 ? (dx > 0 ? 0 : np - 1) : preset + dx, np)].s;
    if (rulesRow == Scheme_ && ok) screen = SchemeEdit, schemeRow = 0;
    if (rulesRow >= FirstRule && rulesRow < Pot && (dx || ok)) c.rules ^= RULE_ROWS[rulesRow - FirstRule].bit;
    if (rulesRow == Pot && ok) screen = Wormpot, reel = 0;
    if (back) screen = SetupMap;
    plus = plus && padsReady(lobby);
    bool start = room() ? plus && mayEdit && net->allReady() && lobby.canStart() : plus;
    if (plus && room() && !start) net->setReady(!net->isReady(net->id));
    if (plus && !room()) saveSetup(c);
    float t = now();

    const bool settled = from >= SetupTeams && from <= SetupRules && leaving < 0;
    setupBack(room() ? tr("FETXTH.GameLobby", "GAME LOBBY", "SALON") : tr("FETXTH.LOCALGAME", "LOCAL GAME", "PARTIE LOCALE"), 2, t, settled ? 1 : subIn(t), room() ? timer : -2);
    int pots = 0, pot = 0;
    for (int b = 0; b < WP_MODES; b++) if (wpOn(wormpotModes(c.wormpot), b)) pots++, pot = b;
    struct Row { const char *label; std::string value; int toggle; };  // toggle: -1 none, else the rule bit's row
    std::vector<Row> rows = {{tr("FETXT.Scheme", "Scheme", "Schéma"), preset < 0 ? "Custom" : SCHEMES[preset].name, -1}};
    for (int r = 0; r < RULES; r++) rows.push_back({ruleLabel(r), c.rules & RULE_ROWS[r].bit ? tr("FETXT.On", "On", "Oui") : tr("FETXT.Off", "Off", "Non"), r});
    rows.push_back({tr("FETXT.Wormpot", "Wormpot", "Wormpot"), !pots ? tr(nullptr, "None", "Aucun") : pots == 1 ? potName(pot) : TextFormat(tr(nullptr, "%d modes", "%d modes"), pots), -1});
    const char *title = rows[rulesRow].label, *help = nullptr;  // the left panel explains the selected row with W4M's own text
    if (rulesRow == Scheme_) title = tr("FETXT.GameStyle", "Game style", "Style de partie"), help = tr("FETXT.HTPSubtopic24", "Before you begin a game, you must first select a Game Style.", nullptr);
    else if (rulesRow >= FirstRule && rulesRow < Pot) help = ruleHelp(rulesRow - FirstRule);
    else if (rulesRow == Pot) help = tr("FETIP.Wormpot", "The Wormpot changes the rules of the game in new ways.", nullptr);
    static int helpRow = -1;
    static float helpSince = 0;
    if (rulesRow != helpRow) helpRow = rulesRow, helpSince = t;
    FeFlat flat;
    if (help) {  // the help panel on the right: the selected row's title and W4M's text, scrolling when it overflows (AutoScrollSpeed)
        Rectangle pn = {664, 170, 576, 360};
        if (!comPanel(pn)) DrawRectangleRounded(pn, 0.06f, 6, PANEL);
        feTextPx(title, pn.x + 30, pn.y + 44, 36, GC_SUBHEADER_BLUE, 0);
        FeBox b;
        b.c = {(pn.x + pn.width / 2 - 640) / FE_S, (360 - (pn.y + 208)) / FE_S}, b.hw = (pn.width - 60) / 2 / FE_S, b.hh = 128 / FE_S, b.size = 16, b.just = 0, b.scrollT = t - helpSince;
        feText(plain(help), b);
    }
    float y = 170;
    for (int i = 0; i < (int)rows.size(); i++) {
        if (i == FirstRule || i == Pot) y += 12;
        float a = settled ? 1.0f : rowAppear(t - entered, leaving >= 0 ? t - leaving : -1, i + 3);
        Rectangle d = {64, y, 580, 34};
        y += 42;
        if (a <= 0) continue;
        bool sel = i == rulesRow;
        Tilt tl(centre(d), i % 2 ? -0.01f : 0.01f, (1 - a) * 400);  // ours: a small alternating tilt
        feRow(d, sel ? 1 : 0, rows[i].label, rows[i].value.c_str(), sel && mayEdit && i < FirstRule);
        if (i == Pot) image("fe/icon_wxpot", {d.x - 46, d.y - 6, 40, 40});
    }
    const char *tip = !mayEdit ? tr(nullptr, "The host sets the rules", "L'hôte règle la partie") : rulesRow == Scheme_ ? tr("FETIP.GameStyle", "Game styles contain game and weapon settings.", nullptr)
                      : rulesRow == Pot ? tr("FETIP.Wormpot", "Wormpot modifies the game rules to create new ways to play.", nullptr) : nullptr;
    setupFront(t, tip, false, 0);
    const LegendEntry L[] = {{"< >/A", "< >/Enter", nullptr, "Change", "Modifier"}, {"+", "N", nullptr, room() ? "Ready / Start" : "Start", room() ? "Prêt / Lancer" : "Lancer"}, backLegend};
    legend(L + !mayEdit, 3 - !mayEdit);
    return start;
}

void Frontend::captureState(const std::string &what) {
    loadTeams();
    if (what == "selectteam") screen = Local, openTeams(Pop::Mission, -1, 0, 0);
    else if (what == "createask") screen = MyWorms, startCreate(Pop::Create);
    else if (what.compare(0, 8, "teamedit") == 0 && what.size() >= 9) {  // teamedit<page>[_<picker>][@<team>]: picker head, eyes, face, hands, sound, weapon, flag, grave
        size_t at = what.find('@');
        int i = at == std::string::npos ? -1 : barracks.find(what.substr(at + 1));
        openEditor(i >= 0 ? i : barracks.find(missionTeamName()), MyWorms, -1, 0, Pop::Edit);
        screen = TeamEdit, leaving = -1, ed.page = what[8] - '1', ed.row = ed.page == 2 ? 1 : 0;
        static const std::pair<const char *, Pop::Use> PICK[] = {{"_head", Pop::Head}, {"_eyes", Pop::Eyes}, {"_face", Pop::Face}, {"_hands", Pop::Hands},
                                                                 {"_sound", Pop::Soundbank}, {"_weapon", Pop::Weapon}, {"_flag", Pop::Flag}, {"_grave", Pop::Grave}};
        for (const auto &k : PICK)
            if (what.find(k.first) == 9) {
                ed.row = k.second == Pop::Soundbank ? 0 : k.second <= Pop::Hands ? k.second - Pop::Head + 2 : k.second - Pop::Weapon;
                pickList(k.second), pop.t0 = -100;
            }
    }
}

void Frontend::enterRoom(bool l) {
    online = true, lan = l, loaded = false, screen = SetupTeams, cursors = {}, rulesRow = 0, teamsDirty = false, editing = nullptr, pop = {};
    myTeams.clear();
}

void Frontend::backToRoom() { screen = SetupTeams, cursors = {}, rulesRow = 0; }

void Frontend::fromNet(const std::vector<std::string> &maps) {
    const Net::Lobby &n = net->lobby;
    GameConfig &c = lobby.cfg;
    lobby.self = (int)net->id, lobby.host = (int)net->hostId;
    c.teams = std::min<int>((int)n.teams.size(), Lobby::MAX_TEAMS);
    lobby.used = (1u << c.teams) - 1;
    c.teamSetup.resize(Lobby::MAX_TEAMS);
    for (int k = 0; k < c.teams; k++) {
        const Net::LobbyTeam &t = n.teams[k];
        lobby.owner[k] = (int)t.owner;
        c.teamSetup[k] = t.owner == net->id && t.idx < myTeams.size() ? myTeams[t.idx] : t.team;  // own teams: the local copy, with its pad
        lobby.ready[k] = net->isReady(t.owner);
    }
    if (!lobby.isHost()) {  // the host's screens edit the settings; the others mirror them
        c.map = n.map, c.rules = n.rules, c.scheme = n.scheme, c.wormpot = n.wormpot, c.wormsPerTeam = n.wormsPerTeam;
        auto it = std::find(maps.begin(), maps.end(), n.map);
        if (it != maps.end()) mapSel = int(it - maps.begin());
    }
}

void Frontend::toNet() {
    const GameConfig &c = lobby.cfg;
    for (int k = 0; k < c.teams && k < (int)net->lobby.teams.size(); k++) {
        const Net::LobbyTeam &t = net->lobby.teams[k];
        if (t.owner != net->id || t.idx >= myTeams.size()) continue;
        GameConfig::Team &m = myTeams[t.idx];
        const GameConfig::Team &e = c.teamSetup[k];
        teamsDirty |= wireOf(m) != wireOf(e) || m.pad != e.pad;
        m = e;
    }
    if (teamsDirty && now() - teamsSent >= 0.1f) net->sendTeams(myTeams), teamsDirty = false, teamsSent = now();
    Net::Lobby &n = net->lobby;
    if (lobby.isHost() && (n.map != c.map || n.rules != c.rules || n.wormpot != c.wormpot || n.wormsPerTeam != c.wormsPerTeam || memcmp(&n.scheme, &c.scheme, sizeof(Scheme)))) {
        n.map = c.map, n.rules = c.rules, n.scheme = c.scheme, n.wormpot = c.wormpot, n.wormsPerTeam = c.wormsPerTeam;
        net->pushLobby();
    }
}

// ---------------------------------------------------------------- network salon: Créer / Rejoindre

int salon(Salon &st, bool lan, const std::vector<std::string> &names, const std::vector<std::string> &info, const std::string &status) {
    int dy = P({DOWN}, {KEY_DOWN}) - P({UP}, {KEY_UP}), n = (int)names.size(), ret = 0;
    bool ok = P({A}, {KEY_ENTER, KEY_SPACE}), back = P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
    if (!st.joining) {
        st.row = clampWrap(st.row + dy, 2);
        if (ok) st.row == 0 ? ret = 1 : (st.joining = true, st.sel = 0);
        if (back) ret = -1;
    } else {
        st.sel = n ? clampWrap(st.sel + dy, n) : 0;
        if (ok && n) ret = 2;
        if (back) st.joining = false;
    }
    float t = now();
    if (st.page < 0) st.t0 = t;
    if (st.page != st.joining) st.page = st.joining, st.t1 = t, Audio::play(Audio::Sfx::FeNextIn);  // W4M's item in-animations and sound, as on the other menus
    const char *title = tr("FETXTH.NetworkPlay", "NETWORK PLAY", "JEU EN RÉSEAU");
    float x = (1 - easeOut((t - st.t0) / 0.4f / 0.7f)) * -820;
    divide(x);
    watermark(title, x, t);
    titleControl(title, x);
    feTextPx(lan ? tr(nullptr, "Local network", "Réseau local") : tr(nullptr, "Online", "En ligne"), 1240 - feWidthPx(lan ? tr(nullptr, "Local network", "Réseau local") : tr(nullptr, "Online", "En ligne"), 18 * FE_S), 92, 18 * FE_S, GC_BUTTON_YELLOW, 0);
    if (!st.joining) {  // W4M's menu entries (WXNET.MainMenu): gold labels, the highlight a black brush stroke
        static const MenuItem ITEMS[2] = {{nullptr, "Create a lobby", "Créer un salon", 900, 220, 60, -3}, {nullptr, "Join a lobby", "Rejoindre un salon", 1040, 360, 58, 2}};
        static float glow[2] = {1, 0};
        for (int i = 0; i < 2; i++) {
            glow[i] += ((i == st.row) - glow[i]) * fminf(1, GetFrameTime() * 14);
            menuEntry(tr(ITEMS[i].key, ITEMS[i].en, ITEMS[i].fr), ITEMS[i].x, ITEMS[i].y, ITEMS[i].size, ITEMS[i].deg, ITEMS[i].fit, ITEMS[i].sx, glow[i], rowAppear(t - st.t1, -1, i), t);
        }
    } else {  // the games heard / the server's rooms as W4M list rows (WXNET.GameList)
        head(lan ? tr(nullptr, "Games on this network", "Parties sur ce réseau") : tr(nullptr, "Open lobbies", "Salons ouverts"), 150, 150, 0.05f);
        const int vis = 6, first = Clamp(st.sel - vis + 1, 0, std::max(0, n - vis));
        for (int i = first; i < n && i < first + vis; i++) {
            Rectangle d = feBox(150, 116 - (i - first) * 36.0f, 270, 32);
            float al = rowAppear(t - st.t1, -1, i - first);
            if (al <= 0) continue;
            Tilt tl(centre(d), 0.06f - 0.04f * ((i - first) % 4), (al - 1) * 400);
            feRow(d, i == st.sel ? 1 : 0, names[i].c_str(), info[i].c_str(), false);
        }
        if (!n) feLine(lan ? tr(nullptr, "Looking for games on this network...", "Recherche de parties sur ce réseau...") : tr(nullptr, "No lobby yet", "Aucun salon pour l'instant"), 150, 40, 20, GC_WHITE, 1);
    }
    setupFront(t, status.empty() ? nullptr : status.c_str(), !status.empty(), -1);
    return ret;
}

// ---------------------------------------------------------------- wormpot & weapon factory

namespace {
int reelSize(int r) { return (int)WORMPOT_REEL[r].size(); }
int reelMode(uint32_t wp, int r) {  // reel r's index in WORMPOT_REEL[r], -1 empty
    auto it = std::find(WORMPOT_REEL[r].begin(), WORMPOT_REEL[r].end(), wormpotReel(wp, r));
    return it == WORMPOT_REEL[r].end() ? -1 : int(it - WORMPOT_REEL[r].begin());
}
void setReel(uint32_t &wp, int r, int m) { wp = wormpotSet(wp, r, m < 0 ? 0 : WORMPOT_REEL[r][m]); }
const char *modeName(int r, int m) { return potName(m < 0 ? WP_EMPTY : WORMPOT_REEL[r][m]); }

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

bool warmHud(int &i, double until) {
    static std::vector<std::string> names;
    if (names.empty()) {  // hud/ but the wxp_ particle sheets Fx loads itself, plus the art the match HUD reaches outside it
        for (const WeaponDef &w : WEAPONS) names.push_back(iconOf(w));
        for (const Catalog::Item &f : catalog().flags) names.push_back(f.model);
        for (const char *n : {"fe/team_health", "fe/com_panel", "fe/speech_popup", "fe2/homing_inner"}) names.push_back(n);
        for (const std::string &p : Loading::list(DATA_DIR "assets/ui/hud", ".png"))
            if (const char *f = GetFileNameWithoutExt(p.c_str()); strncmp(f, "wxp_", 4)) names.push_back(std::string("hud/") + f);
    }
    while (i < (int)names.size() && GetTime() < until) tex(names[i++]);
    return i < (int)names.size();
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
    const WormpotInfo &mi = WORMPOT_MODES[m < 0 ? WP_EMPTY : WORMPOT_REEL[reel][m]];
    wrapped(tr(TextFormat("FETXT.WPotHelp.%s", mi.key), mi.help, nullptr), 640, 500, 900, 24, WHITE);
    text(spinning ? "Spinning..." : "Spin those reels!", 640, 600, 30, GOLDEN, 1);
    hints({{"D-pad", "Left/Right", "Reel"}, {"D-pad", "Up/Down", "Nudge"}, {"X", "S", "Spin"}, {"Y", "R", "Reset"}, {"B", "Esc", "Back"}});
    if ((back || ok) && !spinning) screen = SetupRules;
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
        WeaponDef w = {TextFormat("Custom %d", n + 1), Kind::Shell, 0, 0, 30, 0, 0, 0, 0, 1, 0, 1, true, 2};
        w.model = "bazooka", w.icon = "Secret Weapon";
        for (auto &c : w.slider) c[0] = 0.5f, c[1] = 0.75f, c[2] = 0.5f, c[3] = 0.5f;  // WXD.DefaultWeapon
        factoryPayload(w);
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
    bool bomber = launch == 2, noCluster = bomber || !w.clusters;
    // W4M WXFE.WeaponFactory1List rows (0x73cc5a Launched, 0x73df27 Thrown, 0x73c8b1 Air Strike): the 0..1 sliders are gauges, +-0.1 (0x7409f5)
    struct Num { const char *label; float *f; int *i; float lo, hi, step; const char *fmt; bool gauge, off; };
    const Num nums[] = {
        {"Launch speed", &w.speed, nullptr, 5, 50, 1, "%.0f", false, false}, {"Fuse", &w.fuse, nullptr, 1, 5, 0.5f, "%.1f s", false, launch != 1},
        {tr("FETXT.LandDamage", "Land Damage", "Dégâts au sol"), &w.slider[0][0], nullptr, 0, 1, 0.1f, "%.1f", true, false},
        {tr("FETXT.WormDamage", "Worm Damage", "Dégâts sur le worm"), &w.slider[0][2], nullptr, 0, 1, 0.1f, "%.1f", true, false},
        {tr("FETXT.BlastStrength", "Blast Strength", "Force de l'explosion"), &w.slider[0][3], nullptr, 0, 1, 0.1f, "%.1f", true, false},
        {bomber ? tr("FETXT.NumBomblets", "Bombs", "Bombes") : tr("FETXT.NumClusters", "Clusters", "Fragments"), nullptr, &w.clusters, bomber ? 1.0f : 0, bomber ? 10.0f : 5, 1, "%d", false, false},
        {tr("FETXT.ClusterSpread", "Cluster Spread", "Propagation des fragments"), &w.spread, nullptr, 0, 1, 0.1f, "%.1f", true, noCluster},
        {tr("FETXT.LandDamage", "Land Damage", "Dégâts au sol"), &w.slider[1][0], nullptr, 0, 1, 0.1f, "%.1f", true, noCluster},
        {tr("FETXT.WormDamage", "Worm Damage", "Dégâts sur le worm"), &w.slider[1][2], nullptr, 0, 1, 0.1f, "%.1f", true, noCluster},
        {tr("FETXT.BlastStrength", "Blast Strength", "Force de l'explosion"), &w.slider[1][3], nullptr, 0, 1, 0.1f, "%.1f", true, noCluster},
        {"Bounce", &w.bounce, nullptr, 0, 1, 0.05f, "%.2f", false, false},
        {"Crate weight", nullptr, &w.weight, 0, 10, 1, "%d", false, false}, {"Ammo", nullptr, &w.count, -1, 9, 1, "%d", false, false},
    };
    const int N = 5 + (int)(sizeof nums / sizeof *nums);  // name, launch, model, icon, wind + numbers
    facRow = clampWrap(facRow + dy, N);
    if (dx) switch (facRow) {
    case 1:
        launch = clampWrap(launch + dx, 4);
        w.kind = launch == 2 ? Kind::Airstrike : launch == 3 ? Kind::Homing : Kind::Shell;
        w.fuse = launch == 1 ? fmaxf(w.fuse, 3) : 0;
        if (launch == 2) w.clusters = Clamp(w.clusters ? w.clusters : 5, 1, 10);  // WXD.DefaultAirStrike 5 bombs
        else w.clusters = std::min(w.clusters, 5);
        break;
    case 2: w.model = SHOT_MODELS[clampWrap(indexOf(SHOT_MODELS, w.model) + dx, (int)(sizeof SHOT_MODELS / sizeof *SHOT_MODELS))]; break;
    case 3: w.icon = ICONS[clampWrap(indexOf(ICONS, w.icon) + dx, (int)(sizeof ICONS / sizeof *ICONS))]; break;
    case 4: (launch == 3 ? w.avoid : w.wind) ^= 1; break;  // Homing: HomingAvoidLand instead of wind (a W4M Factory homing missile ignores wind)
    default: {
        const Num &k = nums[facRow - 5];
        if (k.off) break;
        if (k.gauge) {  // 0x73447e: past 0 - 0.05 wraps to 1, past 1 + 0.05 to 0
            float v = *k.f + dx * 0.1f;
            *k.f = v < -0.05f ? 1 : v > 1.05f ? 0 : Clamp(v, 0, 1);
        } else if (k.f) *k.f = Clamp(roundf((*k.f + dx * k.step) / k.step) * k.step, k.lo, k.hi);
        else {  // NoOfCluster wraps (0x740e97): Bomber 1..10, else 0..5
            int v = *k.i + dx;
            *k.i = k.i == &w.clusters ? (v < k.lo ? (int)k.hi : v > k.hi ? (int)k.lo : v) : (int)Clamp(v, k.lo, k.hi);
        }
    }
    }
    if (ok && facRow == 0) edit(w.name, "Weapon name");
    if (ok && facRow == 4) (launch == 3 ? w.avoid : w.wind) ^= 1;
    if (dx || ok) factoryPayload(w);

    heading("Weapon Factory", 640, 14, 48);
    popup({200, 64, 640, 616});
    Rectangle pv = {870, 120, 300, 300};
    panel(pv, false);
    if (!image(iconOf(w), {pv.x + 50, pv.y + 30, 200, 200})) text("?", pv.x + 150, pv.y + 80, 90, WHITE, 1);
    text(w.name.c_str(), pv.x + 150, pv.y + 244, 28, GOLDEN, 1);
    wrapped("In every match's weapon panel. Online, the host's weapons are used.", 1020, 444, 300, 20, LIGHTGRAY);
    for (int i = 0; i < N; i++) {
        Rectangle r = {216, 76 + i * 33.0f, 608, 31};
        bool hi = i == facRow;
        std::string v;
        const char *label = i == 0 ? "Name" : i == 1 ? "Launch" : i == 2 ? "Model" : i == 3 ? "Icon" : i == 4 ? (launch == 3 ? "Avoid land" : "Wind") : nums[i - 5].label;
        const Num *k = i >= 5 ? &nums[i - 5] : nullptr;
        if (i == 0) v = w.name + (typing && fmodf(t, 1) < 0.5f ? "_" : "");
        else if (i == 1) v = LAUNCH[launch];
        else if (i == 2) v = w.model;
        else if (i == 3) v = w.icon;
        else if (i == 4) v = launch == 3 ? (w.avoid ? "Yes" : "No") : w.wind ? "Affected" : "Not affected";
        else if (k->off) v = k->f == &w.fuse ? "Impact" : "---";
        else v = k->i == &w.count && w.count < 0 ? "Infinite" : k->f ? TextFormat(k->fmt, *k->f) : TextFormat(k->fmt, *k->i);
        if (hi) brush(r);
        text(label, r.x + 10, r.y + 4, 22, ink(hi));
        if (k && k->gauge && !k->off) {  // WXFE_PowerColumns
            Rectangle g = {r.x + r.width - 250, r.y + 9, 180, 13};
            DrawRectangleRec(g, Fade(BLACK, 0.5f));
            DrawRectangleRec({g.x, g.y, g.width * *k->f, g.height}, hi ? GOLDEN : LIGHTGRAY);
            DrawRectangleLinesEx(g, 1, WHITE);
        }
        text(TextFormat(hi && i && !(k && k->off) ? "< %s >" : "%s", v.c_str()), r.x + r.width - 10, r.y + 4, 22, WHITE, 2);
    }
    if (typing) hints({{nullptr, "Enter", "Done"}, {nullptr, "Backspace", "Delete"}});
    else if (facRow == 0) hints({{"A", "Enter", "Rename"}, {"D-pad", "Up/Down", "Move"}, {"B", "Esc", "Back"}});
    else hints({{"D-pad", "Up/Down", "Move"}, {"D-pad", "Left/Right", "Change"}, {"B", "Esc", "Back"}});
    if (back) screen = Factory;
}

// ---------------------------------------------------------------- HUD

// W4M weapon panel grid 0x920e58 (docs/w4m/render.md "Weapon panel"): W4M weapon, icon sheet 1-3, 4 x 4 cell; "" = no weapon
struct PanelCell { const char *key; int sheet, cell; };
static const PanelCell PANEL_GRID[PANEL_ROWS * PANEL_COLS] = {
    {"Bazooka", 1, 12}, {"HomingMissile", 3, 4}, {"Shotgun", 2, 8}, {"PoisonArrow", 2, 5}, {"SentryGun", 2, 6}, {"SniperRifle", 1, 1},
    {"Grenade", 1, 3}, {"ClusterGrenade", 2, 0}, {"GasCanister", 2, 10}, {"HolyHandGrenade", 1, 5}, {"BananaBomb", 2, 14}, {"Fatkins", 3, 2},
    {"FirePunch", 1, 4}, {"Prod", 1, 7}, {"BaseballBat", 2, 13}, {"Airstrike", 1, 14}, {"SuperAirstrike", 3, 7}, {"AlienAbduction", 2, 15},
    {"Landmine", 1, 6}, {"Dynamite", 1, 2}, {"Sheep", 1, 13}, {"OldWoman", 3, 1}, {"Scouser", 2, 2}, {"SuperSheep", 2, 9},
    {"Flood", 1, 9}, {"NoMoreNails", 2, 12}, {"Starburst", 2, 7}, {"ConcreteDonkey", 1, 15}, {"FactoryWeapon", 3, 6}, {"", 0, 0},
    {"Parachute", 2, 3}, {"NinjaRope", 1, 8}, {"Redbull", 1, 0}, {"Jetpack", 2, 4}, {"Girder", 2, 1}, {"BubbleTrouble", 3, 5},
    {"SkipGo", 1, 10}, {"ChangeWorm", 3, 3}, {"Surrender", 1, 11}, {"", 0, 0}, {"", 0, 0}, {"Binoculars", 3, 8}};

int panelWeapon(int cell) {
    const char *k = PANEL_GRID[cell].key;
    for (int w = 0; *k && w < (int)WEAPONS.size(); w++)
        if (!strcmp(k, "FactoryWeapon") ? customWeapon(w) : w4mKey(WEAPONS[w]) == k) return w;
    return -1;
}

static int panelCell(int weapon) {
    for (int c = 0; weapon >= 0 && c < PANEL_ROWS * PANEL_COLS; c++) if (panelWeapon(c) == weapon) return c;
    return -1;
}

void Hud::input(const Game &g, Input &in, bool local, int pad, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    const uint8_t held = in.buttons, drop = held & Input::TARGET ? 0 : Input::PITCH;  // before the panel blanks `in`: the swallow must wait for a real release
    if (cur.team < (int)g.cfg.teamSetup.size() && g.cfg.teamSetup[cur.team].cpu) local = false;
    mine = local;
    // swallow: a button still held from a menu (START at tick 0) or another turn must not fire or jump
    if (tick == 0) swallow = true;
    bool x = local && (forceX || pressed(pad, {X}, {KEY_Q}));
    if (x && g.phase == Phase::Settle && !g.countGroup.empty()) in.flags |= Input::SKIP_COUNT, reopen = true;  // observed in W4M by the user, 2026-10-03: the menu key stops the count
    if (x && counting >= 0) skipHp = true;
    if (!local || g.phase != Phase::Aim) {
        pick = -1, swallow = true;
        if (g.phase != Phase::Settle) reopen = false;
        if (g.phase != Phase::Aim) viewOpen = false;
        bool o = !local && viewOpen && viewSlot < PANEL_ROWS * PANEL_COLS;  // W4M replays the active player's Input.Menu.* on every machine (docs/w4m/network.md)
        if (o != open) Audio::play(o ? Audio::Sfx::FePopupIn : Audio::Sfx::FePopupOut);
        else if (o && viewSlot != cursor) Audio::play(Audio::Sfx::FeHighlight);
        open = o, cursor = o ? viewSlot : cursor;
        return;
    }
    if (reopen) reopen = false, x = !open;
    using S = Audio::Sfx;
    if (x) {
        open = !open, Audio::play(open ? S::FePopupIn : S::FePopupOut);
        if (int c = panelCell(g.held()); c >= 0) cursor = c;
    }
    if (open) {
        int dx = pressed(pad, {RIGHT}, {KEY_RIGHT}) - pressed(pad, {LEFT}, {KEY_LEFT});
        bool fwd = g.jetting && ((pad >= 0 && IsGamepadButtonDown(pad, UP)) || IsKeyDown(KEY_W));  // Jetpack.Forward, InGame group
        int dy = pressed(pad, {DOWN}, {KEY_DOWN}) - (pressed(pad, {UP}, {KEY_UP}) && !(fwd && pad >= 0 && IsGamepadButtonDown(pad, UP)));
        int was = cursor;
        cursor = clampWrap(cursor / PANEL_COLS + dy, PANEL_ROWS) * PANEL_COLS + clampWrap(cursor % PANEL_COLS + dx, PANEL_COLS);  // 0x6038e0: each axis wraps
        if (cursor != was) Audio::play(S::FeHighlight);
        if (pressed(pad, {A}, {KEY_SPACE, KEY_ENTER})) {
            if (int w = panelWeapon(cursor); w >= 0 && g.pickable(cur.team, w)) select(w), Audio::play(S::FeClick);  // a tool out: a payload is its secondary, else it ends
            else Audio::play(S::FeError);
        }
        if (pressed(pad, {B}, {KEY_BACKSPACE})) open = false, swallow = true, Audio::play(S::FeCancel);
        // W4M 0x602ea0 disables only WormMoving (stick, jump; 0x506fa0 posts their releases): UtilityFire and InGame stay live.
        // W4M navigates with the stick (Menu group): the D-pad up stays Jetpack.Forward. Landed: A would also be FIRE, so no takeoff
        in = Input{}, in.buttons = g.jetting ? held & Input::FIRE : 0, in.walk = fwd ? 127 : 0;  // B closes the panel: it must not drop the secondary
    }
    if (swallow) {
        if (!(held & (g.jetting ? Input::JUMP : Input::FIRE | Input::JUMP | drop))) swallow = false;
        in.buttons &= ~(g.jetting ? Input::JUMP : Input::FIRE | Input::JUMP | drop);  // in flight FIRE is the thrust; JUMP / PITCH are B (Fire.Second), never a leftover press
    }
    if (pick >= 0 && (g.held() == pick || g.shotsLeft || !g.pickable(cur.team, pick))) pick = -1;
    if (pick >= 0) in.buttons = (in.buttons & ~(Input::FIRE | Input::JUMP)) | Input::NEXT_WEAPON, in.aim = Input::pick(pick).aim;  // pending pick: Controls still sees the old weapon, so a bounce press would fire it unaimed
}

// WindMeterEntity 0x5fc6e0: the pointer's yaw (camera-relative downwind - sin(OnScreenPos.x -268 / 640), grey -pi/2) under
// locator1's ArrowOrien (0.75, 0, -0.2), XYZ order; the tip (0, 0, -1) seen down -z, screen y down
Vector2 windPointer(bool live, float wx, float wz, Vector2 fwd) {
    fwd = Vector2Normalize(fwd);
    float yaw = live ? atan2f(wx * fwd.y - wz * fwd.x, wx * fwd.x + wz * fwd.y) - sinf(-268.0f / 640) : -PI / 2;
    float x = -sinf(yaw), y = cosf(yaw) * sinf(0.75f), c = cosf(-0.2f), s = sinf(-0.2f);
    return {x * c - y * s, -(x * s + y * c)};
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

// Text3D 0x5fad80, view depth in m: HUD.3DText.Min/MaxScalingDist 80 / 200 units hold the on-screen size outside that band
static float text3dK(float depth) {
    float u = depth * 20;
    return u < 80 ? u / 80 : u > 200 ? u / 200 : 1;
}

// Text3D at world point p, em scale m (HUD.3DText.Scale 5 units) x text3dK on a w x h screen; returns its em in px, its centre in at.
// Drawn by the unzoomed HUD camera (near 5, t 0.48): p keeps its drawn screen spot at HUD depth (0x5faaa0), 1 unit nearer (0.05 under 50, 0x5fb4f0)
float text3dPlace(Vector3 p, float scale, const Camera3D &cam, Vector3 fwd, Vector2 screen, Vector2 &at) {
    const float F = 15000, A = (F + 2.5f) / (F - 2.5f), B = 2 * 2.5f * F / (F - 2.5f), Ac = (F + 5) / (F - 5), Bc = 2 * 5 * F / (F - 5);
    float u = Vector3DotProduct(Vector3Subtract(p, cam.position), fwd) * 20, s = scale * 20 * text3dK(u / 20);
    bool zoomed = fabsf(cam.fovy - Controls::FOV0) > 0.01f;  // zoomed (0x51e150): both lenses take the logical camera's near
    float hu = (zoomed ? u : (A * u - B + Bc) / Ac) - (u < 50 ? 0.05f : 1);
    Vector2 m = Vector2Scale(screen, 0.5f);
    at = Vector2Add(m, Vector2Scale(Vector2Subtract(GetWorldToScreenEx(p, cam, (int)screen.x, (int)screen.y), m), u / hu));
    return s / (hu * 0.48f) * m.y;
}

// Our text size is the font's line, 56 px for a 50 px em
static float text3dAt(const char *t, Vector3 p, float scale, Color c, const Camera3D &cam, Vector3 fwd, Vector2 *at = nullptr) {
    Vector2 a;
    float size = text3dPlace(p, scale, cam, fwd, {(float)GetScreenWidth(), (float)GetScreenHeight()}, a);
    if (t) text3d(t, a.x, a.y, size * 56 / 50, c);
    if (at) *at = a;
    return size;
}

// PiP centre, half extents (px) and tilt (rad): HUDTWK PiP.Off/OnScreenPosition, OnScreenScale (the sprite's ±Size, 0x6b4ec0), OnScreenRotation z;
// PiPService (0x635e10, 0x6360d6) scales position and size by 0x4d4cc0's (0.75 aspect clamped to 4/3..16/9): x 4/3 at 16:9.
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

void reticle(const WeaponDef &wd, Vector2 c) {
    if (wd.name == "Sniper Rifle") {  // SniperCursorGraphicEntity (0x5f8de0, Sniper.Cursor.Mesh): navy all around a round view, dark tapered cross with three ovals per arm
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
    if (wd.kind == Kind::Shell && wd.fuse <= 0) ok = put("bazookatargetouter", 185, 185, 0.5f) && put("bazookatargetinner", 185, 370, 0.5f);
    else if (wd.kind == Kind::Shell) ok = put("target", 200, 200, 0.5f);  // thrown
    else ok = put("aimer_outer", 215, 215, 0.5f) && put("aimer_inner", 110, 110, 0.5f);
    if (!ok) {
        DrawRing(c, 9, 11, 0, 360, 24, CREAM);
        for (Vector2 d : {Vector2{1, 0}, Vector2{-1, 0}, Vector2{0, 1}, Vector2{0, -1}})
            DrawLineEx(Vector2Add(c, Vector2Scale(d, 14)), Vector2Add(c, Vector2Scale(d, 22)), 2, CREAM);
    }
}

// W4M HUD digits: "0-9 . m", '~' = infinity, ':' = two dots; h = cell height. Falls back to text().
static const char *const HF_ROW[2] = {"012345.", "6789~m"};
static const short HF_COL[2][7][2] = {{{6, 78}, {87, 131}, {142, 211}, {218, 290}, {300, 376}, {384, 455}, {463, 500}},
                                      {{6, 78}, {86, 149}, {158, 233}, {238, 312}, {319, 418}, {428, 508}}};
// HUD.Font glyph widths 0x920a78 (0x5f1430): index 0-9 digits, 10 infinity, 11 '.', 12 'm'
static const float HF_W[13] = {0.629f, 0.4032f, 0.5968f, 0.629f, 0.6613f, 0.629f, 0.629f, 0.5645f, 0.629f, 0.6129f, 0.8548f, 0.3226f, 0.6774f};
// One HUD.Font glyph as the clock and health preview build it: a quad of half extent (S x width, S) about (cx, cy), the picture the glyph plus the width's padding
static void hudGlyph(char c, float cx, float cy, float S, Color tint, bool grey) {
    static const char ORDER[] = "0123456789~.m";  // glyph index of HF_W
    Texture2D t = tex(grey ? "hud/hud_font_grey" : "hud/hud_font");
    const char *o = c ? strchr(ORDER, c) : nullptr;
    if (!o || !t.id) return;
    int gi = (int)(o - ORDER), r = strchr(HF_ROW[0], c) ? 0 : 1, col = (int)(strchr(HF_ROW[r], c) - HF_ROW[r]);
    float k = t.width / 512.0f, x0 = HF_COL[r][col][0], gw = HF_COL[r][col][1] - x0, pad = (HF_W[gi] * 128 - gw) / 2;
    DrawTexturePro(t, {(x0 - pad) * k, r * 128.0f * k, (gw + 2 * pad) * k, 128 * k}, {cx - S * HF_W[gi], cy - S, 2 * S * HF_W[gi], 2 * S}, {}, 0, tint);
}
static float digits(const char *s, float x, float y, float h, int align, bool grey = false, Color tint = WHITE) {  // returns the width
    const char *const *ROW = HF_ROW;
    const short(*COL)[7][2] = HF_COL;
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

static void healthBar(int colour, float x, float y, float w, float h, float frac) {
    Texture2D t = tex("fe/team_health");
    DrawRectangleRounded({x - 3, y - 3, w + 6, h + 6}, 0.3f, 4, {20, 20, 20, 230});
    DrawRectangleRounded({x, y, w, h}, 0.3f, 4, {170, 150, 110, 255});
    if (t.id) {
        float row = 4 + HEALTH_ROW[colour] * 102.0f, sw = 489 * frac;
        DrawTexturePro(t, {11, row, sw, 97}, {x, y, w * frac, h}, {}, 0, WHITE);
    } else {
        DrawRectangleRounded({x, y, w * frac, h}, 0.3f, 4, TEAM_COLORS[colour]);
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
        auto corners = [&](Vector2 o, float x, float y, float k, Color col) {  // locators 1-4 at (-+x, +-y); 128-unit bitmaps (Size ±64) x the node scale k
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
        if (ob.dead || ob.type == Object::Barrel || !ob.radar) continue;  // radar: W4M Crate.RadarHide / RadarDisplay (crate +0x86)
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
        DrawCircleV(p, r, teamColor(g, w.team));
    }
    if (aiming && !sprite("radar_objects", at(g.target()), 0.5f, {16, 16}, 0, WHITE, {96, 0, 32, 32})) DrawCircleLinesV(at(g.target()), 5, WHITE);
}

// W4M commentary banner (com_panel), queued; crateFocus: seconds the camera follows a dropped crate
static int bannerClock = 0;  // g.clock of the event being handled
struct Banner { std::string s; float life = 0; int clock = bannerClock; };  // life s; 0: ours, 3 s (2 s while others wait)
static std::vector<Banner> banners;
static float bannerAge = 0, crateFocus = 0;
static const float BANNER_SIZE = 26, BANNER_W = 680;
static std::vector<std::string> wrapBanner(const std::string &s) {
    std::vector<std::string> lines(1);
    for (size_t i = 0, j; i < s.size(); i = j + 1) {
        j = std::min(s.find(' ', i), s.size());
        std::string word = s.substr(i, j - i), line = lines.back().empty() ? word : lines.back() + " " + word;
        if (textWidth(line.c_str(), BANNER_SIZE) > BANNER_W && !lines.back().empty()) lines.push_back(word);
        else lines.back() = line;
    }
    return lines;
}
static uint32_t bannerTick = 0;
static std::vector<uint8_t> announced;  // per worm death, per team wipe (replays re-emit events)
// W4M SubtitleGraphicEntity (0x5f93b0, 0x5f9350): in subtitle mode (EFMV.Subtitles.On .. Off) the comments wrap at 600 units
// (scale 20, the box's 300 / 18), each line its own entry shown alone for the full delay, white, no shadow, centred at (0, -210)
struct Sub { std::string s; float life; int lose = 0; };
static std::vector<Sub> subs;
void (*onNarrator)(int n) = nullptr;
static float subAge = 0;
static bool subMode = false;
static const float SUB_SIZE = 20 * BANNER_SIZE / 18, SUB_W = 600 * BANNER_SIZE / 18;
static void syncSubs(const Game &g) {  // Subtitles.On kills the box and empties the queue (0x5e4cf7); Off ends the line and the queue
    bool on = scriptMovie(g).subtitles;
    if (on == subMode) return;
    subMode = on, subs.clear(), subAge = 0;
    if (on) banners.clear(), bannerAge = 0;
}

// Random W4M line "Comment.<cat>.<n>" with %s = name
static void comment(const char *cat, const char *name) {
    std::vector<const char *> lines;
    for (int i = 1; i <= 60; i++)
        if (const char *l = tr(TextFormat("Comment.%s.%d", cat, i), nullptr); l && *l) lines.push_back(l);
    if (lines.empty()) return;
    std::string s = lines[GetRandomValue(0, (int)lines.size() - 1)];
    if (size_t at = s.find("%s"); at != std::string::npos) s.replace(at, 2, name);
    banners.push_back({s});
}

void hudEvent(const Game &g, const GameEvent &e) {
    announced.resize(g.worms.size() + g.teams);
    bannerClock = g.clock;
    syncSubs(g);
    // Commentary.NoDefault: CommentService +0x138 gates every default comment; subtitle mode drops them all (0x5e510f)
    bool defaults = scriptHud(g).defaults && !subMode;
    const Json *mv = e.kind == GameEvent::Movie ? scriptMovieEvent(g, e.fx, e.weapon) : nullptr;
    // a movie's FailureComment (0x526962): Miss.Generic.Lose1..5 by the graphical rng, Delay = its Duration
    std::string lose = mv && (*mv)[0].s() == "FailureComment" ? TextFormat("Miss.Generic.Lose%d", GetRandomValue(1, 5)) : "";
    const char *id = e.kind == GameEvent::Comment ? e.fx : lose.empty() ? nullptr : lose.c_str();
    int delay = e.kind == GameEvent::Comment ? e.weapon : mv ? (int)(*mv)[3].f() : 0;
    if (id) {  // 0x5e0680: one queue entry per line, 0 ms -> 1200; a box shows two lines for 2 x the delay (0x5dce30)
        const char *t = tr(id, nullptr);
        std::string s = t && *t ? t : e.kind == GameEvent::Comment && e.pos.x ? id : std::string("** INVALID STRING ID: ") + id;  // ScriptText keeps an unknown id as text
        float d = (delay ? delay : 1200) / 1000.f;
        if (subMode) {
            std::vector<std::string> lines(1);
            for (size_t i = 0, j; i < s.size(); i = j + 1) {  // breaks at a space or '-' (0x5e0a43)
                j = std::min(s.find_first_of(" -", i), s.size());
                std::string word = s.substr(i, j - i + (j < s.size() && s[j] == '-')), line = lines.back().empty() ? word : lines.back() + " " + word;
                if (textWidth(line.c_str(), SUB_SIZE) > SUB_W && !lines.back().empty()) lines.push_back(word);
                else lines.back() = line;
            }
            int n = 0;
            sscanf(id, "Miss.Generic.Lose%d", &n);  // CommentService +0x148 (atoi(id + 17)), played once with the next line shown
            for (const std::string &l : lines) subs.push_back({l, d, n}), n = 0;
        } else {
            std::vector<std::string> lines = wrapBanner(s);
            for (size_t i = 0; i < lines.size(); i += 2) banners.push_back(i + 1 < lines.size() ? Banner{lines[i] + " " + lines[i + 1], 2 * d} : Banner{lines[i], d});
        }
    } else if (e.kind == GameEvent::MovieEnd && subMode) {  // 0x5dff70: the movie's end empties the queue and hides the line
        subs.clear(), subAge = 0;
    } else if (e.kind == GameEvent::CommentClear) {  // 0x5dfed0: the waiting comments go, the shown one stays
        if (subs.size() > 1) subs.erase(subs.begin() + 1, subs.end());
        if (banners.size() > 1) banners.erase(banners.begin() + 1, banners.end());
    } else if (e.kind == GameEvent::Death && e.worm >= 0 && !announced[e.worm]) {
        const Worm &w = g.worms[e.worm];
        announced[e.worm] = 1;
        if (defaults) comment(g.drowned(e.worm) ? "WaterDeath" : "LandDeath", wormName(g.cfg, w.team, g.wormSlot(e.worm)));
        bool left = false;
        for (const Worm &x : g.worms) left |= x.team == w.team && x.alive;
        if (!left && !announced[g.worms.size() + w.team]) {
            announced[g.worms.size() + w.team] = 1;
            if (defaults) comment("TeamDeath", teamName(g.cfg, w.team).c_str());
        }
    } else if (e.kind == GameEvent::TurnStart && e.worm >= 0) {  // CommentService GameLogic.Turn.Started (0x5e1e80): Comment.StartTurn.1-17, %s = the active worm, every turn
        if (defaults) comment("StartTurn", wormName(g.cfg, g.worms[e.worm].team, g.wormSlot(e.worm)));
    } else if (e.kind == GameEvent::SuddenDeath) {  // StartSuddenDeath: Comment.SuddenDeath through the CommentService queue
        if (defaults) banners.push_back({tr("Comment.SuddenDeath", "SUDDEN DEATH!")});
    } else if (e.kind == GameEvent::CrateDrop && !g.objects.empty()) {
        int wi = g.objects.back().weapon;
        Kind k = wi >= 0 && wi < (int)WEAPONS.size() ? WEAPONS[wi].kind : Kind::Shell;
        bool util = utility(k);
        if (defaults) comment(g.objects.back().mystery >= 0 ? "Mystery" : wi < 0 ? "Health" : util ? "Utility" : "Crate", "");  // Comment.MysteryCrateSpawn
        crateFocus = 30;  // until it lands: the sim holds the turn meanwhile
    } else if (e.kind == GameEvent::Mystery && e.weapon >= 0 && e.weapon < 15 && !subMode) {  // CommentaryPanel.Comment: the item's Text.k<name>
        banners.push_back({mysteryText(e.weapon)});
    } else if (e.kind == GameEvent::Collect && e.worm >= 0 && defaults) {  // CommentaryPanel.CrateText
        const Worm &w = g.worms[e.worm];
        const char *who = wormName(g.cfg, w.team, g.wormSlot(e.worm));
        if (e.weapon == -1) banners.push_back({TextFormat("%s : +%d", who, (int)g.cfg.scheme.crateHealth)});
        else if (e.weapon >= 0 && e.weapon < (int)WEAPONS.size()) {
            banners.push_back({TextFormat("%s : %s", who, weaponName(WEAPONS[e.weapon]))});
        }
    }
}

static void drawSubtitle(float dt) {
    if (subs.empty()) return;
    if (subs[0].lose > 0 && onNarrator) onNarrator(subs[0].lose);
    subs[0].lose = 0;
    if ((subAge += dt) > subs[0].life) { subs.erase(subs.begin()), subAge = 0; return; }
    text(subs[0].s.c_str(), 640, 360 + 210 * 720 / 540.f - SUB_SIZE / 2, SUB_SIZE, WHITE, 1);
}

static void drawBanner(float dt, bool shown) {  // shown false: the hot seat hides the box (BaseHudObject 0x5dad00), its queue keeps running
    if (banners.empty()) return;
    const float LIFE = banners[0].life > 0 ? banners[0].life : banners.size() > 1 ? 2 : 3, SIZE = BANNER_SIZE;  // queued: keeps pace with the death queue (2 s a worm)
    if ((bannerAge += dt) > LIFE) { banners.erase(banners.begin()), bannerAge = 0; return; }
    if (!shown) return;
    std::vector<std::string> lines = wrapBanner(banners[0].s);
    float w = 0;
    for (const std::string &l : lines) w = fmaxf(w, textWidth(l.c_str(), SIZE));
    w += 80;
    float h = 44 + 30 * lines.size(), y = 360 - 140 * 1.5f - h / 2 - (h + 60) * (fmaxf(0, 1 - bannerAge / 0.2f) + fmaxf(0, (bannerAge - LIFE + 0.2f) / 0.2f));  // HUD.Comment.Position y 140
    Rectangle r = {640 - w / 2, y, w, h};
    if (!comPanel(r)) DrawRectangleRounded(r, 0.3f, 6, {0, 104, 138, 230});
    for (size_t i = 0; i < lines.size(); i++) text(lines[i].c_str(), 640, y + 20 + 30 * i, SIZE, WHITE, 1);
}

bool Hud::trackHp(const Game &g, bool turnStart, uint32_t tick) {
    float dt = fminf((tick - hpTick) * Game::DT, 0.1f);  // sim time: the count stops with the pause menu
    if (hpt.size() != g.worms.size() || tick < hpTick) {  // new match: its first tick's comments (a script's Initialise) stay
        announced.assign(g.worms.size() + g.teams, 0), crateFocus = 0, bannerTick = tick;
        banners.erase(std::remove_if(banners.begin(), banners.end(), [&](const Banner &b) { return b.clock != g.clock; }), banners.end());
    }
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
        counting = -1;  // the camera: each display's WormTrackCamera request (Controls, 0x5abeec)
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
        for (size_t i = 0; i < g.objects.size(); i++) if (g.objects[i].type == Object::Crate && g.objects[i].spawning && g.objects[i].track) crate = &g.objects[i], landed = i;  // Crate.TrackCam
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
bool Hud::readyScreen(const Game &g, bool cinematic) const { return g.phase == Phase::Aim && g.hotSeat > 0 && !cinematic && !open; }

// Payload.Input.FirePressed of a live Super Sheep (walk, take off, detonate): 0 none, 1 one still walking, 2 flying
static int sheepFlight(const Game &g) {
    int r = 0;
    for (const Projectile &s : g.shots)
        if (!s.child && WEAPONS[s.weapon].kind == Kind::SuperSheep) r = WEAPONS[s.weapon].walks && !s.stage ? 1 : r ? r : 2;
    return r;
}

void Hud::labels(const Game &g, const Camera3D &cam, uint32_t tick) {
    menuPage = false;
    const Worm &cur = g.worms[g.current];
    bool turnStart = g.current != introWorm;
    if (turnStart) introWorm = g.current;
    bool cinematic = trackHp(g, turnStart, tick);
    bool ready = readyNow = !scriptMovie(g).borders && readyScreen(g, cinematic);  // every turn's hot seat (local, CPU, remote): W4M HotSeatTimeGraphicEntity, spawned by Timer.StartHotSeatTimer for any worm (killed by the borders, 0x5ee275)
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 camUp = Vector3Normalize(Vector3CrossProduct(Vector3CrossProduct(fwd, cam.up), fwd));
    const Color TEXT3D_GREY = {200, 200, 200, 255};  // 0x5c4527, 0x563682
    if (!ready && cur.team < (int)g.spy.size() && g.spy[cur.team])  // W4M Crate Spy (CrateGraphicEntity 0x5c5270): contents over every crate
        for (const Object &o : g.objects) {
            Vector3 top = Vector3Add(o.pos, {0, 0.75f, 0});  // 0x5c4580: crate + 15 units
            if (o.type != Object::Crate || Vector3DotProduct(Vector3Subtract(top, cam.position), fwd) < 0.5f) continue;
            const char *what = o.mystery >= 0 ? mysteryText(o.mystery)
                             : o.weapon < 0 ? tr("Text.Health", "Health", "Santé") : WEAPONS[o.weapon].name.c_str();
            text3dAt(what, top, 0.25f, TEXT3D_GREY, cam, fwd);
        }
    if (!ready) for (const Projectile &s : g.shots) {  // W4M 0x57b1e0: ceil(fuse left) in FE.Font, white, while 0 < left <= 5 s
        const WeaponDef &d = WEAPONS[s.weapon];
        Vector3 top = Vector3Add(s.pos, Vector3Scale(camUp, d.fuseHeight));  // the offset along the view's up (0x47a120)
        float dist = Vector3DotProduct(Vector3Subtract(top, cam.position), fwd);
        if (!d.fuseShown || s.child || s.fuse <= 0 || s.fuse > 5 || dist < 0.5f) continue;
        text3dAt(TextFormat("%d", (int)ceilf(s.fuse - 0.001f)), top, d.fuseSize, WHITE, cam, fwd);
    }
    if (!ready && g.jetting) {  // JetpackUtility's Text3D: (2 ms + 500) / 1000 (0x5626e0) at the worm's Position + 28 units (0x5633ca), default scale
        Vector3 at = Vector3Add(cur.pos, {0, 1.4f - Game::R, 0});
        if (Vector3DotProduct(Vector3Subtract(at, cam.position), fwd) >= 0.5f) text3dAt(TextFormat("%d", (int)(g.fuel * 2 + 0.5f)), at, 0.25f, TEXT3D_GREY, cam, fwd);
    }
    // W4M worm labels: name over hp, team colour, on a Text.Backing; hidden on the ready screen, with the weapon panel open or a UFO out (0x5fd4e0)
    if (!ready && !open && !g.efmvActive()) for (const Worm &w : g.worms) {
        int i = int(&w - g.worms.data()), k = g.wormSlot(i), hp = (int)lroundf(hpt[i].shown);
        if (!w.alive) continue;  // blown up, or drowned: W4M shows no label afloat
        float dist = Vector3DotProduct(Vector3Subtract(w.pos, cam.position), fwd);
        bool popup = std::any_of(popups.begin(), popups.end(), [&](const Popup &p) { return p.worm == i; });
        bool label = &w != &cur && Vector3Distance(w.pos, cam.position) >= 1.2f;  // 0x5fd4e0: the active worm never has a label
        if (dist < 0.5f || !(label || popup)) continue;
        // 0x5fb170: feet (W4M Position) + 25 units up + 4 along the view's up, then HealthOffset 4 / NameOffset 9 x lens zoom x text3dK
        float q = tanf(cam.fovy * 0.5f * DEG2RAD) / 0.48f, kq = q * text3dK(dist);
        Vector3 base = Vector3Add(Vector3Add(w.pos, {0, 1.25f - Game::R, 0}), Vector3Scale(camUp, 0.2f));
        Vector3 hpAt = Vector3Add(base, Vector3Scale(camUp, 0.2f * kq)), nameAt = Vector3Add(base, Vector3Scale(camUp, 0.45f * kq));
        Vector2 sp;
        float s = text3dAt(nullptr, hpAt, 0.25f, BLANK, cam, fwd, &sp);
        sp.y += s / 2;  // bottom of the hp label
        if (pipShow > 0 || pipFull > 0) {  // not over the PiP, growing too
            Vector2 pc, ph;
            float rot;
            pipPlace(pipShow, pipFull, pc, ph, rot);
            if (fabsf(sp.x - pc.x) < ph.x + 30 && fabsf(sp.y - pc.y) < ph.y + 30) label = false;
        }
        Color c = teamTextColor(g, w.team);
        const Color POISON = {120, 220, 60, 255};
        if (label) {
            text3dAt(TextFormat("%d", hp), hpAt, 0.25f, i == counting && hpt[i].poison ? POISON : c, cam, fwd);  // WormHealthNameEntity 0x5fdb70: Text3Ds in FE.Font
            text3dAt(wormName(g.cfg, w.team, k), nameAt, 0.25f, c, cam, fwd);
        }
        for (const Popup &p : popups) {  // W4M damage counter: big cream hud digits, grows as it counts, pops on each step
            if (p.worm != i) continue;
            float a = Clamp(1 - (p.age - 0.5f) / 0.5f, 0, 1), pop = 1 + 0.25f * fmaxf(0, 1 - p.punch / 0.08f) + 0.3f * sinf(fminf(p.age / 0.25f, 1) * PI);
            float h = fmaxf(36, s * 2.6f) * (1 + fminf(abs(p.amount), 100) * 0.005f) * pop, bw = h * 0.3f;
            Color pc = p.amount > 0 || p.poison ? Color{150, 235, 90, 255} : Color{255, 244, 228, 255};
            const char *n = TextFormat("%d", abs(p.amount));
            float y = Clamp(sp.y - s * 2.4f - h - fmaxf(0, p.age - 0.25f) * 60, 0, GetScreenHeight() - h);
            float dw = digits(n, 0, -h * 2, h, 1, true, BLANK), cx = Clamp(sp.x + bw * 0.6f, bw * 1.25f + dw / 2, GetScreenWidth() - dw / 2);  // whole counter on screen
            float x0 = cx - digits(n, cx, y, h, 1, true, Fade(pc, a)) / 2;
            Rectangle bar = {x0 - bw * 1.25f, y + h * 0.42f, bw, h * 0.14f};  // the font has no sign glyphs
            for (int k = 0; k < (p.amount > 0 ? 2 : 1); k++) {
                Rectangle r = k ? Rectangle{bar.x + bw / 2 - bar.height / 2, bar.y - bw / 2 + bar.height / 2, bar.height, bw} : bar;
                DrawRectangleRec({r.x + h * 0.05f, r.y + h * 0.05f, r.width, r.height}, Fade({20, 30, 60, 255}, a));
                DrawRectangleRec(r, Fade(pc, a));
            }
        }
        if (label && &w == &cur && g.phase == Phase::Aim && !g.jetting) {  // bobbing "this one" arrow
            float b = sinf(tick * 0.12f) * 4;
            if (!sprite("wormlocarrow", {sp.x, sp.y - s * 2 - 18 + b}, 0.22f, {64, 120}, 0, c)) DrawTriangle({sp.x - 8, sp.y - s * 2 - 30 + b}, {sp.x, sp.y - s * 2 - 18 + b}, {sp.x + 8, sp.y - s * 2 - 30 + b}, c);
        }
    }
}

// W4M HotSeatTimeGraphicEntity (docs/w4m/turn.md "Hot seat screen"): HUD units about the screen centre, 480 tall (x1.5 px), y up. Keys {time s, value, in x, y, out x, y}
// from Bundl09 `HST Anims.xom` clips HST_In_All / HST_Out_All; scale x / y of each node
static const float HST_I_RDY_X[3][6] = {{0.0f, 0.0f, 0.0833129883f, 0.281738281f, 0.0f, 0.0f}, {0.0833129883f, 0.281738281f, 0.00465774536f, 0.00171852112f, 0.201416016f, 2.2578125f},
                                        {0.666503906f, 1.0f, 0.747070312f, -0.00407409668f, 0.10925293f, 2.3359375f}};
static const float HST_I_RDY_Y[8][6] = {{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f}, {0.0416564941f, 0.0f, 1.0f, 0.0f, 0.0401916504f, 0.999023438f},
                                        {0.0833129883f, 1.03417969f, 0.0401916504f, 0.999023438f, 0.548339844f, -0.8359375f}, {0.125f, 0.971191406f, 0.548339844f, -0.8359375f, 0.654785156f, 0.755371094f},
                                        {0.166625977f, 1.01953125f, 0.654785156f, 0.755371094f, 0.833984375f, -0.551757812f}, {0.208251953f, 0.991699219f, 0.833984375f, -0.551757812f, 0.96484375f, 0.262207031f},
                                        {0.25f, 1.00292969f, 0.96484375f, 0.262207031f, 0.99609375f, -0.0826416016f}, {0.291503906f, 1.0f, 0.99609375f, -0.0826416016f, 1.0f, 0.0f}};
static const float HST_I_TEAM_Y[2][6] = {{0.0833129883f, 0.0f, 0.260009766f, 2.15625f, 0.260009766f, 2.15625f}, {2.08203125f, 0.999511719f, 1.93359375f, 0.0f, 3.46875f, 0.0f}};
static const float HST_I_TIME[4][6] = {{0.0f, 0.0f, 0.0833129883f, 0.0f, 0.0833129883f, 0.0f}, {0.0833129883f, 0.0f, 0.0833129883f, 0.0f, 0.0416564941f, 1.28808594f},
                                       {0.125f, 1.28808594f, 0.0219726562f, 0.635253906f, 0.673828125f, -0.703125f}, {2.70703125f, 1.0f, 2.8203125f, 0.0129470825f, 2.8203125f, 0.0129470825f}};
static const float HST_I_WORM_X[2][6] = {{0.0416564941f, 0.0f, 0.457763672f, 0.200195312f, 0.0f, 3.14453125f}, {3.20703125f, 1.0f, 3.46289062f, 0.0f, 0.00499725342f, 0.0f}};
static const float HST_O_SHRINK[2][6] = {{0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f}, {0.208251953f, 0.0f, 0.0827636719f, -0.99609375f, 0.0827636719f, -0.99609375f}};
static const float HST_OUT_TIME = 0.208251953f;  // HST_Out_All length
// ms: HotSeatTimeRemaining; age: s since HST_In_All (outro: since HST_Out_All)
static void drawHst(const std::string &worm, const std::string &team, Color tc, int ms, float age, bool outro) {
    const float K = 1.5f;  // HUD unit -> px
    struct Line { std::string s; float y, em, sx, sy; Color c, c2; };
    auto fit = [&](const std::string &s, float maxScale, float maxW) { return fminf(maxScale, maxW / (textWidth(s.c_str(), 100) / 100)); };  // 0x5ed660
    auto keys = [&](const float (*in)[6], int n, bool weighted) { return outro ? xanimKeys(HST_O_SHRINK, 2, age, false) : xanimKeys(in, n, age, weighted); };
    int secs = (ms + 999) / 1000;
    float pulse = 1 + 0.3f * (ms % 1000) / 1000.0f;  // 0x5ed6e0
    Line lines[4] = {
        {worm, 187.5f, fit(worm, 41, 245), outro ? 1 : xanimKeys(HST_I_WORM_X, 2, age, true), outro ? keys(nullptr, 0, false) : 1, tc, WHITE},
        {team, 145.875f, fit(team, 35, 300), 1, keys(HST_I_TEAM_Y, 2, true), WHITE, WHITE},
        {tr("Text.Ready", "Ready?"), -142.0f, 26, keys(HST_I_RDY_X, 3, true), keys(HST_I_RDY_Y, 8, false), WHITE, WHITE},
        {TextFormat("%d", secs), -185.0f, 48 * pulse, keys(HST_I_TIME, 4, true), keys(HST_I_TIME, 4, true), tc, WHITE},
    };
    for (const Line &l : lines) {
        if (l.sx < 0.001f || l.sy < 0.001f) continue;
        std::vector<Vector2> pose = textPose(4, l.s);  // HST.TextAnimNames High5;Low5
        for (Vector2 &v : pose) v.x *= K / FE_S;
        rlPushMatrix();
        rlTranslatef(640, 360 - l.y * K, 0);
        rlScalef(l.sx, l.sy, 1);
        textG(l.s.c_str(), 0, -l.em * K / 2, l.em * K, l.c, l.c2, 1, {0, 0, 0, 255}, &pose);
        rlPopMatrix();
    }
}

// ---------------------------------------------------------------- weapon panel (docs/w4m/render.md "Weapon panel")

// HUD units: 480 tall, centre origin, y up; drawn in px under rl matrices
static const float HU = 720 / 480.0f;
// A HUD root entity's centre in px: HUDTWK position x 0x4d4cc0 (x 4/3, y 1 at 16:9), 480 units high, centre origin, y up [disasm 0x4d4cc0]
static Vector2 hudAt(float x, float y) { return {640 + x * 4 / 3.0f * HU, 360 - y * HU}; }
// [ours, user-requested] every corner element's outer rim sits HUD_EDGE px from the screen edge, rims measured on the drawn sprites (alpha > 20)
static const float HUD_EDGE = 16, COMPASS_R = 88, DISC_R = 47.4f, DISC_T = 45.9f, PREVIEW_R = 58, CLOCK_R = 44.9f, CLOCK_B = 44.9f;
static Vector2 discNudge() { const Vector2 r = hudAt(253, 174); return {1280 - HUD_EDGE - DISC_R - r.x, HUD_EDGE + DISC_T - r.y}; }  // weapon disc, secondary disc, preview
static Vector2 clockNudge() { const Vector2 r = hudAt(254, -170); return {1280 - HUD_EDGE - CLOCK_R - r.x, 720 - HUD_EDGE - CLOCK_B - r.y}; }  // clock, counter
static float previewDx() { return 1280 - HUD_EDGE - PREVIEW_R - hudAt(252, 0).x; }

// HUD.WP.Anims clips {time, value, in tangent x, y, out tangent x, y}, played at speed 1 (0x6a0730)
static const float K_INWP_X[2][6] = {{0, 350, 0.20422f, -661, 0.20422f, -661}, {0.4165f, 0, 1.1465f, 0, 1.1465f, 0}},
                   K_INWP_Y[2][6] = {{0, -64.0625f, 0.14832f, 126.75f, 0.14832f, 126.75f}, {0.4165f, 0, 1.1465f, 0, 1.1465f, 0}},
                   K_INWP_R[2][6] = {{0, -0.026169f, 0.95068f, -0.067139f, 0.36743f, -0.041351f}, {0.4165f, -0.04361f, 0.97559f, 0, 0.97559f, 0}},
                   K_OUTWP_X[2][6] = {{0, 0, 1.3057f, 0, 1.3057f, 0}, {0.5f, 350, 0.20691f, 558.5f, 0.20691f, 558.5f}},
                   K_OUTWP_Y[2][6] = {{0, 0, 0.65869f, 0, 0.65869f, 0}, {0.5f, -54.031f, 0.4646f, -107.875f, 0.4646f, -107.875f}},
                   K_OUTWP_S[2][6] = {{0, 1, 0.92822f, -0.37134f, 0.92822f, -0.37134f}, {0.5f, 0.7998f, 0.92822f, -0.37134f, 0.92822f, -0.37134f}},
                   K_OUTWP_R[2][6] = {{0, -0.04361f, 0.82178f, 0, 0.82178f, 0}, {0.5f, 0.04361f, 0.48242f, 0.097107f, 0.48242f, 0.097107f}},
                   K_WPHI_S[3][6] = {{0, 1.5f, 0.47559f, -0.87939f, 0.47559f, -0.87939f}, {0.4165f, 1.0996f, 1, 0, 1, 0},
                                     {0.833f, 1.5f, 0.44897f, 0.89307f, 0.44897f, 0.89307f}},
                   K_INWH_X[2][6] = {{0.041656f, -74.1875f, 0, 243.625f, 0, 243.625f}, {0.9165f, 0, 0.9038f, 8.1016f, 0.0401f, -0.89844f}},
                   K_INWH_Y[6][6] = {{0.041656f, -25.953f, 0.0013094f, 0.68408f, 0, 73.625f}, {0.375f, 6.8203f, 0.83447f, 0, 0.24097f, 0},
                                     {0.5415f, 2.3672f, 0, -12.305f, 0.035187f, 3.5918f}, {0.6665f, 3.5059f, 0.06958f, 0, 0.11127f, 0},
                                     {0.7915f, 2.7598f, 0.04538f, -2.5547f, 0.13916f, 1.2676f}, {0.958f, 3, 0.0636f, -0.34009f, 0.0636f, -0.34009f}},
                   K_INWH_SY[6][6] = {{0.041656f, 0, 0, 1.7129f, 0, 1.7129f}, {0.25f, 1.0088f, 0.61768f, -0.0010185f, 0.067993f, 0.0000388f},
                                      {0.5f, 1.0088f, 0.25f, 0, 0.041656f, 0}, {0.5415f, 0.99805f, 0.041656f, -0.01133f, 0.12494f, 0.01133f},
                                      {0.6665f, 1.0088f, 0.1969f, 0, 0.1969f, 0}, {0.7915f, 1, 0.019348f, -0.0040474f, 0.019348f, -0.0040474f}},
                   K_INWH_R[2][6] = {{0, 0, 1, 0, 1, 0}, {0.5415f, 0.04361f, 0.99658f, 0.080261f, 0.99658f, 0.080261f}},
                   K_OUTWH_X[2][6] = {{0, 0, 1.0645f, 0, 1.0645f, 0}, {0.5f, -247.375f, 0.5f, -247.375f, 0.5f, -247.375f}},
                   K_OUTWH_Y[2][6] = {{0, 3, 1.0645f, 0, 1.0645f, 0}, {0.5f, -20.906f, 0.5f, -23.906f, 0.5f, -23.906f}},
                   K_OUTWH_S[2][6] = {{0, 1, 0.6626f, -0.12683f, 0.6626f, -0.12683f}, {0.5f, 0.6499f, 0.354f, -0.82666f, 0.354f, -0.82666f}},
                   K_OUTWH_R[2][6] = {{0, 0.061066f, 1.0645f, 0, 1.0645f, 0}, {0.5f, -0.082336f, 0.5f, -0.14343f, 0.5f, -0.14343f}};
static const FeClip IN_WP = {{}, {}, keys(K_INWP_X, true), keys(K_INWP_Y, true), keys(K_INWP_R, true)},
                    OUT_WP = {keys(K_OUTWP_S, false), keys(K_OUTWP_S, false), keys(K_OUTWP_X, true), keys(K_OUTWP_Y, true), keys(K_OUTWP_R, true)},
                    IN_WH = {{}, keys(K_INWH_SY, true), keys(K_INWH_X, true), keys(K_INWH_Y, true), keys(K_INWH_R, false)},
                    OUT_WH = {keys(K_OUTWH_S, true), keys(K_OUTWH_S, true), keys(K_OUTWH_X, true), keys(K_OUTWH_Y, true), keys(K_OUTWH_R, true)};

// locator1 of a HUD clip at t s: its channels over the rest pose (x, y, sx, sy, rz)
struct HudPose { float x = 0, y = 0, sx = 1, sy = 1, rz = 0; };
static HudPose hudPose(const FeClip &c, float t, HudPose p = {}) {
    auto at = [&](Keys k, float &v) { if (k.n) v = xanimKeys(k.k, k.n, t, k.weighted); };
    at(c.tx, p.x), at(c.ty, p.y), at(c.sx, p.sx), at(c.sy, p.sy), at(c.rz, p.rz);
    return p;
}

// a child node at local HUD (x, y), turned rz rad (y up), scaled
static void hudNode(float x, float y, float rz = 0, float sx = 1, float sy = 1) {
    rlPushMatrix();
    rlTranslatef(x * HU, -y * HU, 0);
    rlRotatef(-rz * RAD2DEG, 0, 0, 1);
    rlScalef(sx, sy, 1);
}

// an XBitmap: picture src (px of the 256 px sheet, scaled to the PNG) on half extents (hw, hh) about local (x, y)
static void hudBitmap(Texture2D t, Rectangle src, float x, float y, float hw, float hh, Color tint = WHITE) {
    float k = t.width / 256.0f;
    DrawTexturePro(t, {src.x * k, src.y * k, src.width * k, src.height * k}, {(x - hw) * HU, (-y - hh) * HU, 2 * hw * HU, 2 * hh * HU}, {}, 0, tint);
}

// an FE.Font HUD text centred on local y, em `units` high, x scaled by xs, its width squeezed to `fit` units (0x755843: x scale only).
// Not TEXT_X wide like WXFE text [observed: the real panel title is 192 units wide at em 24]
static void hudText(const char *t, float x, float y, float units, const Color *grad, Color edge, int align, float fit = 0, float xs = 1,
                    const std::vector<Vector2> *pose = nullptr) {
    float px = units * HU * 56 / 50, w = textWidth(t, px) * xs, sx = xs * (fit > 0 && w > fit * HU ? fit * HU / w : 1);
    rlPushMatrix();
    rlTranslatef(x * HU, -y * HU, 0);
    rlScalef(sx, 1, 1);
    textG(t, 0, -px / 2, px, grad[0], grad[1], align, edge, pose);
    rlPopMatrix();
}

// HUD.Arm.Bord.Res (ArmouryBorderResource 0x5d9770): 3 x 4 quads of Weapon Panel Main.tga about the node, half extents (w, h)
static void armBorder(Texture2D t, float w, float h) {
    const float X[4] = {-w, 23 - w, w - 24, w}, Y[5] = {h, h - 23, 1.5f, 26 - h, -h};
    static const Rectangle SRC[4][3] = {{{128, 18, 23, 23}, {1, 195, 254, 23}, {155, 18, 24, 23}},
                                        {{3, 3, 23, 186}, {118, 88, 71, 50.5f}, {59, 3, 24, 186}},
                                        {{30, 3, 23, 186}, {118, 138.5f, 71, 50.5f}, {88, 3, 24, 186}},
                                        {{128, 45, 23, 26}, {1, 227, 254, 26}, {155, 45, 24, 26}}};
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 3; i++)
            hudBitmap(t, SRC[j][i], (X[i] + X[i + 1]) / 2, (Y[j] + Y[j + 1]) / 2, (X[i + 1] - X[i]) / 2, (Y[j] - Y[j + 1]) / 2);
}

// WXFE_Border_NamePlate (0x7631cd, 0x76345c): 3 x 3 quads; the plate is stored upright in the sheet, its rows along u, its columns along v
static void namePlate(Texture2D t, float w, float h) {
    const float X[4] = {-w, 11 - w, w - 11, w}, Y[4] = {h, h - 11, 18 - h, -h}, U[4] = {204, 215, 235, 253}, V[4] = {2, 13, 178, 189};
    float k = t.width / 256.0f;
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            float u0 = U[j] * k / t.width, u1 = U[j + 1] * k / t.width, v0 = V[i] * k / t.height, v1 = V[i + 1] * k / t.height;
            float x0 = X[i] * HU, x1 = X[i + 1] * HU, y0 = -Y[j] * HU, y1 = -Y[j + 1] * HU;
            rlTexCoord2f(u0, v0), rlVertex2f(x0, y0);
            rlTexCoord2f(u1, v0), rlVertex2f(x0, y1);
            rlTexCoord2f(u1, v1), rlVertex2f(x1, y1);
            rlTexCoord2f(u0, v1), rlVertex2f(x1, y0);
        }
    rlEnd();
    rlSetTexture(0);
}

// WXD.WeaponHelpText (0x5fece0): HelpText.<W4M weapon>0; the Weapon Factory slot by its launch type
static const char *weaponHelp(const WeaponDef &w, bool factory) {
    if (factory) {
        static const char *const LAUNCH[3] = {"HelpText.kSecretWeaponBomber", "HelpText.kSecretWeaponLaunched", "HelpText.kSecretWeaponThrown"};
        int l = factoryLaunch(w);
        if (const char *s = l < 3 ? tr(LAUNCH[l], nullptr) : nullptr) return s;
        return tr("HelpText.kWeaponSecretWeapon0", "");
    }
    std::string k = w4mKey(w);
    const char *s = tr(("HelpText.kWeapon" + k + "0").c_str(), nullptr);
    return s ? s : tr(("HelpText.kUtility" + k + "0").c_str(), "");
}

// WXWeaponHelpPanelLogicEntity: the help panel left of the weapon panel; the cursor cell's weapon or none
static void helpPanel(const HudPose &p, const WeaponDef *w, bool factory) {
    // [observed: the real game puts this group's centre at about (-205, -96) HUD units, 1.17 x its decoded size; its parent transform is not traced]
    const Vector2 AT = {-205, -96};
    const float K = 1.17f;
    hudNode(AT.x + p.x, AT.y + p.y, p.rz, p.sx * K, p.sy * K);
    comPanel({-90.3f * HU, -94.6f * HU, 180.6f * HU, 189.2f * HU});  // kMT_ComPanelBorder, half (90.3, 94.6) at (-95, -46)
    const char *title = w ? weaponName(*w) : tr("Text.WeaponHelpTitle", "Weapon Help", "Aide des armes");
    std::vector<Vector2> pose = textPose(4, title);  // High5,Low5
    hudText(title, -4, 83, 20, GC_BUTTON_YELLOW, {0x67, 0x47, 0x00, 255}, 1, 164, 1, &pose);  // (-99, 37), fitted to 164 wide
    if (Texture2D d = tex("hud/weapon_panel_divide"); d.id) hudBitmap(d, {0, 0, 256, 32}, -4, 63, 70, 6);  // HUD.WeaponDivide 20 below
    if (w) {  // WXFEP.WeaponHelp: (-93, -55) half (80, 60), FontSizeOverride 20, AutoScale, Solid_White, top-left
        std::string s = feTokens(weaponHelp(*w, factory));
        float size = 20;
        std::vector<std::string> lines = feWrap(s, size * HU, 160 * HU);
        while (size > 5 && lines.size() * size > 120) lines = feWrap(s, (size -= 1) * HU, 160 * HU);
        for (size_t i = 0; i < lines.size(); i++) {
            float px = size * HU;
            rlPushMatrix();
            rlTranslatef(-78 * HU, (-51 + i * size) * HU, 0);
            rlScalef(TEXT_X, 1, 1);
            feLine(lines[i], 0, 0, px, WHITE, WHITE, 0, BLACK);
            rlPopMatrix();
        }
    }
    rlPopMatrix();
}

// HUD.Anims clips (Bundl09 `HUD Anims.xom`, w4m-models --list, W4M_KEYS=full): {time s, value, in tangent x, y, out tangent x, y}, weighted
static const float K_TI_Y[2][6] = {{2.0820313f, -200.0f, 0.0025501251f, 1.8808594f, 0.12078857f, 593.5f}, {3.125f, 0.0f, 2.1542969f, 0.0f, 2.1542969f, 0.0f}}, K_TO_Y[2][6] = {{0.0f, 0.0f, 1.1474609f, 224.875f, 1.1474609f, 224.875f}, {0.4165039f, -200.0f, 0.0f, -76.1875f, 0.0019550323f, -0.9995117f}},
                   K_PV_IN_X1[2][6] = {{0.0f, 130.0f, 0.625f, -130.0f, 0.625f, -130.0f}, {0.625f, 0.0f, 1.6220703f, 0.0f, 1.6220703f, 0.0f}}, K_PV_IN_R1[2][6] = {{0.0f, -0.17443848f, 0.625f, 0.17443848f, 0.625f, 0.17443848f}, {0.625f, 0.0f, 1.6220703f, 0.0f, 1.6220703f, 0.0f}},
                   K_PV_IN_X2[2][6] = {{0.041656494f, 130.0f, 0.625f, -130.0f, 0.625f, -130.0f}, {0.6665039f, 0.0f, 1.6142578f, 0.0f, 1.6142578f, 0.0f}}, K_PV_IN_Y2[2][6] = {{0.041656494f, -10.0f, 0.625f, 10.0f, 0.625f, 10.0f}, {0.6665039f, 0.0f, 1.6142578f, 0.0f, 3.59375f, 0.0f}},
                   K_PV_IN_R2[2][6] = {{0.041656494f, 0.17443848f, 0.625f, -0.17443848f, 0.625f, -0.17443848f}, {0.6665039f, 0.0f, 1.6142578f, 0.0f, 1.6142578f, 0.0f}}, K_PV_IN_X3[2][6] = {{0.08331299f, 120.0f, 0.625f, -120.0f, 0.625f, -120.0f}, {0.7080078f, 0.0f, 1.6142578f, 0.0f, 2.609375f, 0.0f}},
                   K_SW_IN_X[2][6] = {{0.0f, 70.0f, 0.625f, -70.0f, 0.625f, -70.0f}, {0.625f, 0.0f, 1.2861328f, 0.0f, 2.2792969f, 0.0f}}, K_SW_IN_Y[2][6] = {{0.0f, 120.0f, 1.0f, 0.0f, 0.625f, -120.0f}, {0.625f, 0.0f, 1.2949219f, -0.8173828f, 2.0644531f, -1.3027344f}},
                   K_SW_IN_R[2][6] = {{0.0f, -1.2216797f, 0.4970703f, 0.38134766f, 0.5463867f, 0.4194336f}, {0.625f, 0.0f, 1.2861328f, -0.016204834f, 1.9599609f, -0.024673462f}}, K_SW_OUT_S[2][6] = {{0.0f, 1.0f, 0.47094727f, 0.0f, 0.47094727f, 0.0f}, {0.25f, 0.0f, 0.20019531f, -1.5751953f, 0.20019531f, -1.5751953f}};

// MiniBackResource 0x5f2600: the 8 x 5 cell strip of a compact team bar, 2w x 2h HUD units about its centre, y up; colour 0..3 = R B G Y rows of Health Preview.tga.
// [ours: each cell's picture is a texel rect measured on that sheet and the empty part is drawn flat (the mesh UVs are not decoded)]
static void miniBar(int colour, float w, float h, float frac, Color tint) {
    Texture2D t = tex("hud/health_preview");
    auto mul = [&](Color c) { return Color{(unsigned char)(c.r * tint.r / 255), (unsigned char)(c.g * tint.g / 255), (unsigned char)(c.b * tint.b / 255), 255}; };
    float f3 = w * 2 - 2 - 3.5f, pct = Clamp(frac, 0, 1), fill = f3 * pct, rest = f3 - fill;
    float c0 = 2, c1 = 1, c3 = 1, c4 = 3, c6 = 1, c5 = rest;
    if (rest < 3) {
        if (rest < 0) c5 = 0, fill = f3;
        else c5 = 0.001f, c4 = rest;
    }
    float c2 = fill - 2;
    if (c2 < 0) {
        c2 = 0;
        if (fill == 0) c3 = 0, c1 = 0;
        c5 = (f3 - c3) - c1;
    }
    if (c5 == 0) c6 = 0, c4 = 0;
    else {
        if (c5 - c4 < 1) c6 = 0;
        c5 = (c5 - c4) - c6;
    }
    const float cw[8] = {c0, c1, c2, c3, c4, c5, c6, 3.5f}, rh[5] = {2, 1, 2 * h - 9, 1.5f, 4.5f};
    const float sy[6] = {0, 2, 3, 18, 20, 24};  // texel rows of the bar picture per band
    const Color slate[5] = {BLACK, {118, 150, 180, 255}, {82, 120, 155, 255}, {68, 99, 128, 255}, {56, 81, 105, 255}};
    float x = -w;
    for (int c = 0; c < 8; c++) {
        float y = h;
        for (int r = 0; r < 5; r++) {
            Rectangle dst = {x * HU, -y * HU, cw[c] * HU, rh[r] * HU};
            bool pic = c < 5 || c == 7;
            if (cw[c] > 0 && t.id && pic) {
                static const float sx[8][2] = {{0, 2}, {2, 4}, {4, 70}, {70, 72}, {72, 74}, {0, 0}, {0, 0}, {74, 76}};
                float k = t.width / 128.0f;
                Rectangle src = {(50 + sx[c][0]) * k, (6 + colour * 30 + sy[r]) * k, (sx[c][1] - sx[c][0]) * k, (sy[r + 1] - sy[r]) * k};
                DrawTexturePro(t, src, dst, {}, 0, tint);
            } else if (cw[c] > 0) DrawRectangleRec(dst, mul(r == 4 && c == 6 ? slate[4] : slate[r]));
            y -= rh[r];
        }
        x += cw[c];
    }
}

static const float PANEL_K = 1.07f, PANEL_DX = 167;  // 433 units of panel (-215 .. 218) fill 696 of 720 px; right edge (140 + 146) x 1.605 at 1266 px
void Hud::drawPanel(const Game &g, int secs) {
    double now = GetTime();
    if (open != panelShown) panelShown = open, panelSince = now;
    float t = (float)(now - panelSince);
    if (!open && t >= 0.5f) return;
    if (cursor != hiCell) hiCell = cursor, hiSince = now;
    const int team = g.worms[g.current].team, N = PANEL_ROWS * PANEL_COLS;
    Texture2D main = tex("hud/weapon_panel_main"), top = tex("hud/weapon_panel_top");
    if (!main.id || !top.id) return;
    HudPose wp = open ? hudPose(IN_WP, t) : hudPose(OUT_WP, t);

    // help panel first: the weapon panel's root (layer 0x29+) draws over it
    int w = panelWeapon(cursor);
    bool has = w >= 0 && g.stock(team, w) && g.allowed(team, w);  // 0x602c70
    rlPushMatrix();
    rlTranslatef(640 + PANEL_DX, 360, 0);  // [ours, user-requested] the panel and its help box 1.07 x bigger, pushed right to a 14 px margin
    rlScalef(PANEL_K, PANEL_K, 1);
    helpPanel(open ? hudPose(IN_WH, t) : hudPose(OUT_WH, t), has ? &WEAPONS[w] : nullptr, has && customWeapon(w));

    hudNode(140 + wp.x, -10 + wp.y, wp.rz, wp.sx, wp.sy);  // the HUD.WP.Anims instance at (140, -10), its locator1
    armBorder(main, 146, 205);
    hudNode(0, 195);
    namePlate(main, 109.33f, 23.5f);
    const char *title = tr("Text.WeaponPanelTitle", "Weapon Menu", "Menu d'armes");
    std::vector<Vector2> pose = textPose(4, title);  // High5,Low5
    static const Color BLACKS[2] = {BLACK, BLACK};
    hudText(title, 0, 0, 24, BLACKS, BLANK, 1, 0, 1, &pose);
    rlPopMatrix();
    if (Texture2D d = tex("hud/weapon_panel_divide"); d.id) hudBitmap(d, {0, 0, 256, 32}, 0, -44, 76, 6);

    // cells: tiles (layer 0x29), icons (0x2c), the highlight tile (0x2d), its icon copy, the delay texts (0x2e)
    static const Rectangle FRAME[4] = {{8, 8, 86, 86}, {102, 8, 86, 86}, {10, 102, 148, 148}, {176, 178, 72, 72}};
    float hs = hudPose({keys(K_WPHI_S, false), keys(K_WPHI_S, false)}, fmodf((float)(now - hiSince), K_WPHI_S[2][0])).sx;  // WP_Highlight, looped
    for (int pass = 0; pass < 5; pass++)
        for (int c = 0; c < N; c++) {
            int wc = panelWeapon(c), a = wc >= 0 ? g.stock(team, wc) : 0, late = a && g.allowed(team, wc) ? g.delays[team][wc] : 0;
            bool lit = a && g.allowed(team, wc), hi = c == cursor;
            if ((pass >= 2 && pass <= 3 && !hi) || ((pass == 1 || pass == 3) && !lit) || (pass == 4 && !late)) continue;
            Color tint = late || (lit && !g.pickable(team, wc)) ? Color{255, 255, 255, 0x66} : WHITE;
            float s = hi ? hs : 1;
            hudNode(41.8f * (c % PANEL_COLS) - 105.9f, 148.5f - 41.8f * (c / PANEL_COLS) - (c / PANEL_COLS > 4 ? 18 : 0), 0, s, s);
            const PanelCell &pc = PANEL_GRID[c];
            if (pass == 0) hudBitmap(top, FRAME[lit ? 0 : 1], 0, 0, 20.5f, 20.5f, lit ? tint : WHITE);
            else if (pass == 2) hudBitmap(top, FRAME[2], 0, 0, 20.5f, 20.5f);
            else if (pass == 4) hudText(TextFormat("%d", late), 9.27f, -9.27f, 18, GC_WHITE, BLACK, 1);
            else if (Texture2D ic = tex(TextFormat("hud/weapon_panel_icons%d", pc.sheet)); ic.id)
                hudBitmap(ic, {pc.cell % 4 * 64.0f, pc.cell / 4 * 64.0f, 64, 64}, 0, 0, 15.99f, 15.99f, tint);
            rlPopMatrix();
        }

    // footer (0x6026a0): the cursor weapon's name and count, the turn clock in its box
    if (has) {
        int a = g.stock(team, w), late = g.delays[team][w];
        static const Color NAME[2] = {{245, 245, 245, 255}, {245, 245, 245, 255}}, COUNT[2] = {{255, 210, 51, 255}, {255, 210, 51, 255}};
        const Color EDGE = {0, 30, 41, 255};
        bool count = !late && a > 0;
        const char *name = late ? tr("Text.WeaponDelayed", "Delayed", "Délai") : weaponName(WEAPONS[w]);
        hudText(name, count ? -108 : -122, -177, 24, NAME, EDGE, 0, count ? 186 : 200);
        if (count) hudText(TextFormat("%d", a), -122, -177, 24, COUNT, EDGE, 2, 0, a > 9 ? 16.8f / 24 : 1);  // 16.8 wide above 9
    }
    hudNode(103.5f, -163, -0.11f);
    hudBitmap(top, FRAME[3], 0, 0, 16, 16);
    if (secs >= 0) digits(TextFormat("%d", secs), 0, -14 * HU, 28 * HU, 1);  // HUD.Font digits of TurnTimeRemaining [size observed: 0.8 x the box]
    rlPopMatrix();
    rlPopMatrix();
    rlPopMatrix();
}

void Hud::draw(const Game &g, const Camera3D &cam, uint32_t tick) {
    const Worm &cur = g.worms[g.current];
    const bool ready = readyNow;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    // EnergyBarManagerEntity 0x5ea830: Turn.Started clears the bars' show flag; EndTurn / ApplyDamage (Settle) and Worm.Damaged set it
    if (g.clock < barsClock || hpLast.size() != g.worms.size()) {
        barsWanted = false, hpLast.assign(g.worms.size(), 0);
        for (size_t i = 0; i < hpLast.size(); i++) hpLast[i] = g.worms[i].hp;
        hideBars(), cpv = {}, barsReady = false, barsPhase = g.phase;
    }
    barsClock = g.clock;
    if (ready && !barsReady) barsWanted = false;
    barsReady = ready;
    if (g.phase != barsPhase) barsWanted |= g.phase == Phase::Settle, barsPhase = g.phase;
    for (size_t i = 0; i < hpLast.size(); i++) barsWanted |= g.worms[i].hp < hpLast[i], hpLast[i] = g.worms[i].hp;
    syncSubs(g);
    if (!quiet) drawBanner(fminf((tick - bannerTick) * Game::DT, 0.1f), !ready), drawSubtitle(fminf((tick - bannerTick) * Game::DT, 0.1f));
    bannerTick = tick;
    const bool borders = scriptMovie(g).borders;
    const std::string wormTxt = wormName(g.cfg, cur.team, g.wormSlot(g.current)), teamTxt = teamName(g.cfg, cur.team);
    if (ready) {
        if (!hstOn) hstTick = tick;
        hstOn = true, hstWorm = wormTxt, hstTeam = teamTxt, hstCol = teamTextTop(g, cur.team);
    } else {
        if (hstOn && !borders) hstOutro = true, hstTick = tick;  // else the entity dies at once under the borders (0x5ee275)
        hstOn = false;
    }
    if (borders) hstOutro = false;
    if (ready || borders || g.phase == Phase::GameOver) hideBars();  // hot seat, EFMV.BordersActive, game over: BaseHudObject level 3, hidden at once
    if (borders) return;  // EFMV.BordersActive: every HUD object leaves (BaseHudObject 0x5daf31) [ours: at once]
    if (hstOutro) {  // HotSeatTimeRemaining < 1: HST.OutroAnimName, 0.208 s, over the HUD it leaves
        float age = (tick - hstTick) * Game::DT;
        if (age >= HST_OUT_TIME) hstOutro = false;
        else drawHst(hstWorm, hstTeam, hstCol, 0, age, true);
    }
    if (g.phase == Phase::GameOver) {
        if (!g.cfg.mission) {  // missions and challenges end on missionEnd() only
            // HeaderTop FE.WinString in Header_Orange, the line under it in Solid_White
            textG(resultBanner(g).c_str(), 640, 230, 70, {255, 241, 174, 255}, {255, 163, 73, 255}, 1);
            text(resultLine(g), 640, 314, 42, WHITE, 1);
        }
        return;
    }
    const WeaponDef &wd = weaponDef(g.weapon);
    bool aiming = g.phase == Phase::Aim;
    if (ready) {  // W4M ready screen (HotSeatTimeGraphicEntity): the rest of the HUD hidden
        drawHst(wormTxt, teamTxt, hstCol, (int)lroundf(g.hotSeat * Game::DT * 1000), (tick - hstTick) * Game::DT, false);
        return;
    }

    {  // CommentService 0x5e11b0 (Weapon.Selected, Weapon.FuseBounceChanged): the weapon's name, with ", N secs" for an adjustable fuse, replaces the box's text (0x5e1090: 1200 ms)
        static int cw = -1, cf = 0;
        const int fuse = wd.userFuse ? g.fuses[cur.team] : 0;
        if (g.weapon >= 0 && (g.weapon != cw || fuse != cf) && !subMode) {
            std::string c = weaponName(wd);
            if (wd.userFuse) c = TextFormat("%s, %d %s", c.c_str(), fuse, tr("Text.Seconds", "secs", "secondes"));
            banners.assign(1, Banner{c, 1.2f});
            bannerAge = 0;
        }
        cw = g.weapon, cf = fuse;
    }
    const Vector2 rc = {HUD_EDGE + COMPASS_R, HUD_EDGE + COMPASS_R};
    radar(g, rc, Vector3Add(fwd, cam.up), aiming && mine);  // + up: a Blimp looking straight down keeps its heading
    // W4M WindMeterEntity (docs/w4m/render.md): downwind arrow in the camera frame, tilted 0.75 rad; grey and fixed under 0.05
    Vector2 wc = {rc.x + (-268 + 241) * 4 / 3.0f * HU, rc.y + (167 - 90) * HU};  // exe: Wind.OnScreenPos (-268, 90) from Scanner.Position (-241, 167)
    float ratio = hypotf(g.wind, g.windZ);
    int speed = (int)(ratio * 10 + 0.5f);
    if (!sprite(g.cfg.scheme.wind ? "wind_back" : "wind_backdisabled", wc, 2 * 25 * HU / 128, {64, 64})) DrawCircleV(wc, 25 * HU, {0, 104, 138, 220});
    Vector2 v = windPointer(ratio >= 0.05f, g.wind, g.windZ, {fwd.x + cam.up.x, fwd.z + cam.up.z});
    Color ac = ratio >= 0.05f ? Color{255, 170, 30, 255} : Color{150, 150, 150, 255};
    const float AK = (26.8f + 16) * 0.8f * HU / 117;  // arrow mesh tip -26.8 / tail +16 x Wind.Scale 0.8, over the sprite's 117 px of arrow
    if (Texture2D t = tex("hud/wormlocarrow"); t.id)  // the needle's length foreshortened by its tilt
        DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, {wc.x, wc.y, t.width * AK, t.height * AK * Vector2Length(v)}, {64 * AK, 64 * AK * Vector2Length(v)},
                       atan2f(v.y, v.x) * RAD2DEG - 90, ac);
    else DrawLineEx(wc, Vector2Add(wc, Vector2Scale(v, 24)), 5, ac);
    {  // WindMeterEntity 0x5fc0e0: tens at Wind.TextOffset, units after it, 'm' at 0.8 lowered 0.3 glyph; glyph size Wind.SpeedScale 12
        const float S = 12 * HU, bx = wc.x + 33 * HU, by = wc.y + 2 * HU;
        const int t = speed / 10, u = speed % 10;
        const float ux = bx + S * (HF_W[t] + 0.6f * HF_W[u]), mx = ux + S * HF_W[u] + 0.48f * S * HF_W[12];
        hudGlyph('0' + t, bx, by, S, WHITE, speed == 0);
        hudGlyph('0' + u, ux, by, S, WHITE, speed == 0);
        hudGlyph('m', mx, by + 0.3f * S, 0.8f * S, WHITE, speed == 0);
    }
    // ActiveWormHudInfoEntity (0x5d8320 init, 0x5d7bb0): HUD.WormInfoBack always, HUD.WeaponIcon only for a weapon; both half extent 44 (BackScale / WeapScale), no text
    const Vector2 wp = Vector2Add(hudAt(253, 174 - (300 - 174) * pipShow * (1 - pipFull)), discNudge());  // HUD.ActWormInfo.Pos -> PosPiP while the PiP shows
    {
        const float hs = 44 * HU;
        Texture2D back = tex("hud/secondback");
        if (open) {  // WeaponPanel.Open hides every BaseHudObject
        } else if (back.id) DrawTexturePro(back, {0, 0, (float)back.width, (float)back.height}, {wp.x - hs, wp.y - hs, 2 * hs, 2 * hs}, {}, 0, WHITE);
        else DrawCircleV(wp, 0.7f * hs, {0, 119, 155, 230});
        if (g.weapon >= 0 && !open) image(iconOf(wd), {wp.x - hs, wp.y - hs, 2 * hs, 2 * hs});
    }
    // SecondaryWeaponGraphicEntity 0x5f82f0 (no text): root (220, 200) HUD units x (4/3, 1) from the centre, i.e. (253 - 220, 174 - 200) from the ActWormInfo spot;
    // HUD.SecondWeapon.Back half 25, HUD.SecondWeaponIcon half 20; In_WPreview while held, Out_MeleeHit when it goes [disasm + data]
    if (g.secondary != secIcon) {
        if (secIcon >= 0) secOut = secIcon, secOutSince = tick;
        secIcon = g.secondary, secSince = tick;
    }
    if (open || ready) secOut = -1;
    auto secondary = [&](int wi, float x, float y, float rz, float k) {
        const Vector2 c = Vector2Add(hudAt(220, 200), discNudge());
        rlPushMatrix();
        rlTranslatef(c.x + x * HU, c.y - y * HU, 0);
        rlRotatef(-rz * RAD2DEG, 0, 0, 1);
        rlScalef(k, k, 1);
        Texture2D back = tex("hud/secondback");
        if (back.id) DrawTexturePro(back, {0, 0, (float)back.width, (float)back.height}, {-25 * HU, -25 * HU, 50 * HU, 50 * HU}, {}, 0, WHITE);
        else DrawCircleV({}, 25 * HU, {0, 119, 155, 230});
        const WeaponDef &sd = WEAPONS[wi];
        if (!image(iconOf(sd), {-20 * HU, -20 * HU, 40 * HU, 40 * HU}, WHITE)) text(sd.name.substr(0, 4).c_str(), 0, -9, 18, WHITE, 1);
        rlPopMatrix();
    };
    if (!open && !ready && secOut >= 0) {
        float t = (tick - secOutSince) * Game::DT;
        if (t >= 0.25f) secOut = -1;
        else secondary(secOut, 0, 0, 0, xanimKeys(K_SW_OUT_S, 2, t, true));
    }
    if (!open && !ready && g.secondary >= 0) {
        float t = (tick - secSince) * Game::DT;
        secondary(g.secondary, xanimKeys(K_SW_IN_X, 2, t, true), xanimKeys(K_SW_IN_Y, 2, t, true), xanimKeys(K_SW_IN_R, 2, t, true), 1);
    }
    // HudClockEntity (0x5f0ae0 init, 0x5f0a60 update): root HUD.Clock.Position (254, -170) tilted HUD.Clock.Orientation z -0.1, disc HUD.Clock.Back half 44 (Clock.Scale)
    const ScriptHud sh = scriptHud(g);
    const int ms = aiming && !g.hotSeat ? g.timer * 50 / 3 : 0, secs = (ms + 999) / 1000;
    if (!panelShown) {  // WeaponPanel.Open hides every BaseHudObject (0x5dad00)
        // ClockDisplayMode 2 (a running turn): the turn digits (0x5efe80) while the shown seconds > 0 and HUD.HideTurnTime was not sent; retreat and post-activity are mode 0
        const bool digitsOn = secs > 0 && !sh.endless && !sh.hideTurn;
        static float clockY = -44 / 3.0f, clockW = 22;  // the round text's offset and "00:00" width, eased 0.25 a 20 ms frame (0x5f03f0); init Scale / 3 and Scale x 0.5
        static uint32_t clockF = 0;
        const uint32_t frame = (uint32_t)(tick * Game::DT / 0.02f);
        for (uint32_t i = 0, n = std::min<uint32_t>(frame - std::min(frame, clockF), 50); i < n; i++) {
            const float ty = digitsOn ? -44 / 3.0f : 0, tw = digitsOn ? 44 / 1.16f : 44 * 1.4f;
            clockY = fabsf(ty - clockY) >= 0.01f ? clockY * 0.75f + ty * 0.25f : ty;
            clockW = fabsf(tw - clockW) >= 0.01f ? clockW * 0.75f + tw * 0.25f : tw;
        }
        clockF = frame;
        const Vector2 c = Vector2Add(hudAt(254, -170), clockNudge());
        rlPushMatrix();
        rlTranslatef(c.x, c.y, 0);
        rlRotatef(0.1f * RAD2DEG, 0, 0, 1);
        const float hs = 44 * HU;
        Texture2D back = tex("hud/timer_back");
        if (back.id) DrawTexturePro(back, {0, 0, (float)back.width, (float)back.height}, {-hs, -hs, 2 * hs, 2 * hs}, {}, 0, WHITE);
        else DrawCircleV({}, 0.7f * hs, {0, 119, 155, 230});
        if (digitsOn) {
            const float f = (ms % 1000) / 1000.0f, S = 44 * (secs < 6 ? 1 + f * 0.3f : 1.0f) / 2.2f;  // the pulse of the last 5 s
            const int tens = std::min(secs / 10, 9), units = secs % 10;
            const float d = 0.85f * S * (HF_W[tens] + HF_W[units]), y = -(sh.roundClock ? 44 / 2.8f : 0) * HU;
            const Color tc = secs < 6 ? Color{255, (unsigned char)(255 - 200 * f), (unsigned char)(255 - 200 * f), 255} : WHITE;  // white -> HUD.Clock.ColourTurnEnd
            hudGlyph('0' + tens, -d / 2 * HU, y, S * HU, tc, true);
            hudGlyph('0' + units, d / 2 * HU, y, S * HU, tc, true);
        }
        if (sh.roundClock) {  // the "mm:ss" text in FE.Font, ColourGameTime; its "00:00" width is clockW units at a y offset clockY
            std::string rt;
            if (!g.script) {
                int round = std::max(0, g.cfg.scheme.roundTime * 3600 - g.clock) / 60;
                rt = TextFormat("%02d:%02d", round / 60, round % 60);
            } else if (sh.roundTime == -1) rt = "~";  // FXTXT.Infinity
            else rt = TextFormat("%02d:%02d", (int)(sh.clockMs / 1000) / 60, (int)(sh.clockMs / 1000) % 60);
            static const Color GT[2] = {{190, 215, 255, 255}, {190, 215, 255, 255}};
            const float units = clockW / (textWidth("00:00", 1000) / 1000 * 56 / 50);
            const bool tenths = sh.tenths && g.script && sh.roundTime != -1;
            hudText(rt.c_str(), 0, clockY, units, GT, BLANK, 1);
            if (tenths) hudText(TextFormat("%02d", (int)(sh.clockMs % 1000) / 10), clockW / 2 + 6, clockY - 4, 18, GT, BLANK, 1);  // [assumed: the tenths text's offset is not traced]
        }
        rlPopMatrix();
    }
    if (sh.counter) {  // CounterGraphicEntity 0x5e57c0: "%d" (+ "%" if Percent) on HUD.WindBacking x BackScale 30, TextScale 21, HUD.Counter.Position (222, -190)
        const Vector2 cp = Vector2Add(hudAt(222, -190), clockNudge());
        const float hs = 30 * HU;
        Texture2D wb = tex("hud/wind_back");
        if (wb.id) DrawTexturePro(wb, {0, 0, (float)wb.width, (float)wb.height}, {cp.x - hs, cp.y - hs, 2 * hs, 2 * hs}, {}, 0, WHITE);
        else DrawCircleV(cp, 0.7f * hs, {0, 104, 138, 220});
        const char *v = TextFormat(sh.percent ? "%d%%" : "%d", sh.value);
        text(v, cp.x + 2, cp.y - 19 + 2, 39, BLACK, 1);  // HUD.Counter.ShadowColor
        text(v, cp.x, cp.y - 19, 39, {255, 178, 0, 255}, 1);  // HUD.Counter.TextColor
    }
    // team health: EnergyBarManagerEntity 0x5e9f10 / 0x5e97f0 / 0x5e9a00 (bottom centre) and HealthPreviewEntity 0x5ebe40 (top right)
    static float maxHp = 100;
    static int lastClock = -1;
    std::vector<float> tot(std::max(g.teams, 1), 0.0f);  // Energy of the Active worms, a surrendered team 0
    for (const Worm &w : g.worms) if (w.alive && w.team < g.teams && !(w.team < (int)g.surrendered.size() && g.surrendered[w.team])) tot[w.team] += std::max(0, w.hp);
    if (g.clock < lastClock || lastClock < 0) {  // HUD.Energy.MaxLength is the largest team total at WormManager.Reinitialise
        std::vector<float> sum(g.teams, 0);
        for (const Worm &w : g.worms) if (w.alive && w.team < g.teams) sum[w.team] += w.hp;
        maxHp = sum.empty() ? 0 : *std::max_element(sum.begin(), sum.end());
        if (maxHp < 1) maxHp = 100;
        for (int t = 0; t < 4; t++) bars[t].len = bars[t].target = t < g.teams ? Clamp(tot[t] / maxHp, 0, 1) : 0;
    }
    lastClock = g.clock;
    const int nt = std::min(g.teams, 4);
    auto sortBars = [&] {  // 0x5e97f0: strongest first (swap only when strictly less), the rows fill from the bottom
        std::vector<int> perm;
        int nz = 0;
        for (int t = 0; t < nt; t++) perm.push_back(t), nz += tot[t] > 0;
        std::stable_sort(perm.begin(), perm.end(), [&](int a, int b) { return tot[a] > tot[b]; });
        for (int i = 0; i < nt; i++) barSlot[perm[i]] = i + (4 - nz);
    };
    bool allHidden = true;
    for (int t = 0; t < nt; t++) allHidden &= bars[t].st == 0 || bars[t].st == 3;
    if (barsWanted && allHidden) sortBars();
    else if (g.clock < 2) sortBars();
    for (int t = 0; t < nt; t++) bars[t].target = Clamp(tot[t] / maxHp, 0, 1);
    for (int t = 0; t < nt; t++) {  // BaseHudObject 0x5dad00: level 1 In, 2 Out, 3 hidden at once; the bar is up while its team lives (+0x94)
        Bar &b = bars[t];
        bool want = barsWanted && b.target > 0 && !open;
        if (open) b.st = 0;
        else if (want) { if (b.st == 0 || b.st == 3) b.st = 1, b.since = tick; }
        else if (b.st == 1 || b.st == 2) b.st = 3, b.since = tick;
        float age = (tick - b.since) * Game::DT;
        if (b.st == 1 && age >= 3.125f) b.st = 2;
        if (b.st == 3 && age >= 0.4165f) b.st = 0;
        if (b.st == 1) {  // 0x5e8450 runs while In_TeamInfo plays: the length drains 1.5 of its 61 units a 20 ms frame, a rise snaps
            float d = b.len - b.target;
            b.len = d < 0 || d < 0.3f / 61 || d < 1.5f / 61 ? b.target : b.len - 1.5f / 61 * (Game::DT / 0.02f);
        }
    }
    for (int t = 0; t < nt; t++) {
        const Bar &b = bars[t];
        if (b.st == 0) continue;
        float age = (tick - b.since) * Game::DT;
        float slide = b.st == 1 ? xanimKeys(K_TI_Y, 2, age, true) : b.st == 3 ? xanimKeys(K_TO_Y, 2, age, true) : 0;
        float y = 646 - (3 - barSlot[t]) * 38.0f - slide * HU;
        static unsigned lastBlink[4];  // EnergyBarEntity 0x5e8fa0: the active team's name blinks team colour / white, 250 ms each
        unsigned nowMs = (unsigned)(tick * Game::DT * 1000);
        bool white = false;
        if (t == cur.team) {
            if (nowMs > lastBlink[t] + 500) lastBlink[t] = nowMs;
            white = nowMs > lastBlink[t] + 250;
        } else lastBlink[t] = nowMs;
        text(teamName(g.cfg, t).c_str(), 574, y - 1, 24, white ? WHITE : teamTextColor(g, t), 2);
        const Catalog::Item *flag = t < (int)g.cfg.teamSetup.size() ? catalog().flag(g.cfg.teamSetup[t].flag) : nullptr;  // HUD.Energy.Flag*: the team's FlagGfxName
        if (!flag || !hudFlag(flag->model, {606, y + 12})) DrawRectangleRounded({584, y - 4, 32, 32}, 0.2f, 4, teamColor(g, t));
        healthBar(g.alliance(t) & 3, 628, y, 240, 24, Clamp(b.len, 0, 1));
    }
    // HealthPreviewEntity: In_WHealthAll when the HUD shows; Position (252, 88) / PositionPiP (252, 0) x (4/3, 1) from the centre, beside ActWormInfo (253, 174) / (253, 300)
    if (open) cpv.on = false;
    else {
        const float f = tick * Game::DT / 0.02f;  // W4M updates on 20 ms frames
        const float ty = hudAt(252, pipShow > 0 ? 0 : 88).y + discNudge().y;  // Position / PositionPiP
        if (!cpv.on) cpv.on = true, cpv.since = tick, cpv.tick = (uint32_t)f, cpv.y = ty;
        int steps = std::clamp((int)f - (int)cpv.tick, 0, 50);
        cpv.tick = (uint32_t)f;
        cpv.y = ty + (cpv.y - ty) * powf(0.9f, Game::DT / 0.01f);  // 0x5ebbd0: HUD.HPreview.MoveSpeed 0.1 a 10 ms
        if (g.current != cpv.worm) cpv.worm = g.current, cpv.flash = tick;  // Timer.StartTurn restarts the flashing
        float mx = 0;
        std::vector<int> present;
        for (int t = 0; t < nt; t++) mx = std::max(mx, tot[t]);
        cpv.maxSeen = std::max(cpv.maxSeen, mx);  // 0x5eb620: scaled to the largest total seen, 100 under 1
        const float sc = 100 / (cpv.maxSeen < 1 ? 100 : cpv.maxSeen), hpNow = cur.alive ? (float)std::max(0, cur.hp) : 0;
        auto ease = [](float &v, float to) { v = std::fabs(to - v) >= 0.01f ? to * 0.25f + v * 0.75f : to; };  // 0x5eb8e0, 0x5eb100
        for (int i = 0; i < steps; i++) {
            for (int t = 0; t < nt; t++) ease(cpv.team[t], tot[t] * sc);
            ease(cpv.text, hpNow);
        }
        for (int i = 0; i < 4; i++) {  // the active team first, then the others in turn
            int t = (cur.team + i) % 4;
            if (t < nt && tot[t] > 0) present.push_back(t);
        }
        const int n = (int)present.size();
        float age = (tick - cpv.since) * Game::DT;
        rlPushMatrix();
        rlTranslatef(hudAt(252, 0).x + previewDx(), cpv.y, 0);
        if (n) {  // locator2: the bars (MiniBackResource, BarScale 8.8)
            hudNode(xanimKeys(K_PV_IN_X2, 2, age, true), xanimKeys(K_PV_IN_Y2, 2, age, true), xanimKeys(K_PV_IN_R2, 2, age, true));
            static const float A[5][4] = {{}, {-0.055f}, {-0.065f, 0.065f}, {-0.06f, 0.06f, -0.04f}, {-0.15f, 0, 0, 0.1f}},
                               B[5][4] = {{}, {-3}, {-3, 3}, {-3, -5, -1.5f}, {0, -3, 1.3f, 0.1f}};
            const float w = 8.8f * 4 / n, step = w * 1.6f, x0 = step * 0.5f - step * n * 0.5f;
            const bool flashing = !g.script && (tick - cpv.flash) * Game::DT < 100;
            for (int k = 0; k < n; k++) {
                int t = present[k];
                float ph = fmodf((tick - cpv.flash) * Game::DT, 1.0f), c = ph < 0.5f ? 128 + 127 * ph / 0.5f : 255 - 128 * (ph - 0.5f) / 0.5f;  // 0x5ebe40: 0x80 .. 0xff .. 0x7f a second
                unsigned char v = flashing && t == cur.team ? (unsigned char)c : 255;
                hudNode(x0 + k * step, B[n][k], A[n][k]);
                miniBar(g.alliance(t) & 3, w, 8.8f, cpv.team[t] / 100, {v, v, v, 255});
                rlPopMatrix();
            }
            rlPopMatrix();
        }
        {  // locator1: the health in HUD.Font (TextScale 12.8), right edge at TextPosition (10, 20), shadow (0, 0, 0, 120) beside it
            hudNode(xanimKeys(K_PV_IN_X1, 2, age, true), 0, xanimKeys(K_PV_IN_R1, 2, age, true));
            int v = std::min(999, (int)cpv.text);
            const char *str = v >= 100 ? TextFormat("%d", v) : TextFormat("%02d", v);  // 0x5eb100: a blank hundreds glyph under 100, tens always drawn
            float h = 2 * 12.8f * HU;
            digits(str, 10 * HU + 2 * HU, -20 * HU - h / 2 + 2 * HU, h, 2, false, {0, 0, 0, 120});
            digits(str, 10 * HU, -20 * HU - h / 2, h, 2);
            rlPopMatrix();
        }
        {  // locator3: HUD.HealthIcon, or HUD.HealthShield while the worm wears armour (0x5eaeb0), IconScale 14.4 half extent at IconPosition (25, 20)
            hudNode(xanimKeys(K_PV_IN_X3, 2, age, true), 0);
            Texture2D ic = tex(cur.armour ? "hud/health_shield" : "fe/health_icon");
            float hs = 14.4f * HU;
            if (ic.id) DrawTexturePro(ic, {0, 0, (float)ic.width, (float)ic.height}, {25 * HU - hs, -20 * HU - hs, 2 * hs, 2 * hs}, {}, 0, WHITE);
            else DrawCircleV({25 * HU, -20 * HU}, hs, WHITE);
            rlPopMatrix();
        }
        rlPopMatrix();
    }
    // power (stacked blocks, fill from the bottom) and pitch arc (bottom left)
    Vector2 pb = {HUD_EDGE - 7 * 0.62f, 720 - HUD_EDGE - 60.6f - 70};  // rim: bar left texel 7, arc bottom 60.6 px below its pivot, pivot 70 px under the bar origin
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
    Vector2 ap = {pb.x + 80, pb.y + 70};  // arc pivot: flat edge centre
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
        text(keyGlyphs() ? "Jetpack: Space thrust, arrows steer, Backspace drop" : "Jetpack: A/ZR thrust, stick steers, B drop", 190, 600, 22, WHITE);  // HelpText.kUtilityJetpack0
        float full = fmaxf(g.jetInit, 0.01f);  // Jetpack.InitFuel
        healthBar(1, 190, 630, 240, 16, Clamp(g.fuel / full, 0, 1));
    }
    if (open) {}  // over its panel W4M draws no button legend
    else if (mine && !quiet && g.girderOn && g.phase == Phase::Aim)  // W4M HelpText.kUtilityGirder0: Movement, GirderRaise / Lower, Fire
        hints({{"LS", "Arrows", "Move"}, {"RS", "WASD", "Raise / lower, turn"}, {"A", "Space", "Place"}});
    else if (mine && !quiet && wd.kind == Kind::Binoculars && g.phase == Phase::Aim)  // HelpText.kUtilityBinoculars0
        hints({{"L/ZL", "RMB", "Look"}, {"A", "Space", "Select a target"}});
    else if (int sheep = mine && !quiet && !aiming ? sheepFlight(g) : 0; sheep) {  // ours: no W4M legend (docs/w4m/weapons.md §13)
        bool walking = sheep == 1;
        std::vector<Hint> h = {{"A/ZR", "Space", walking ? tr(nullptr, "Take off", "Décoller") : tr(nullptr, "Detonate", "Exploser")}};
        if (!walking) h.push_back({"LS", "Arrows", tr(nullptr, "Steer", "Diriger")});
        hints(h);
    }
    else if (mine && !quiet && g.secondary >= 0)  // W4M SecondaryWeaponHelpEntity: WXFE.HelpDropConsole, FETXT.Control.Secondry + FETXT.Drop
        hints({{"R", g.jetting || g.jetLanded() ? "Backspace" : "Space", tr("FETXT.Drop", "Drop", "Lâcher")}});
    else if (mine && !quiet && Controls::targetView(g))  // W4M BlimpHelpEntity (WXFE.HelpBlimpConsole): Look, Pan, Zoom in / out
        hints({{"A", "Space", "Fire"}, {"LS", "Arrows", "Pan"}, {"RS", "WASD", "Look"},
               {"D-pad", "Z/X", "Zoom"}, {"B/Y", "Enter/E", "Leave"}});
    else if (mine && !quiet && g.phase == Phase::Aim && Controls::firstPerson(g))  // HeadCam: FETXT.Control.ZoomIn / ZoomOut
        hints({{"A", "Space", "Fire"}, {"D-pad", "Wheel", "Zoom"}});
    else if (mine && !quiet && aiming) hints(weaponHints(wd));
    else if (tick < 300 && !quiet) hints({{"-", "F1", "Hold: controls"}});
    drawPanel(g, sh.endless ? -1 : secs);
}

// ---------------------------------------------------------------- pause

static float paragraph(const std::string &s, float x, float y, float w, float size, Color c, int maxLines = 99, bool draw = true, const Color *c2 = nullptr, float lead = 4);

// Delay_Incoming of WXFEP.Pause / ConfirmDraw 100 ms, ConfirmRestart / ConfirmQuit / MissionBriefing 200 ms
static float popupDelay(int confirm, bool brief) { return brief || confirm == Pause::Restart || confirm == Pause::Quit ? 0.2f : 0.1f; }

std::vector<int> Pause::rows() const {
    std::vector<int> r = {Continue, Options};
    if (canDraw) r.push_back(Draw);
    if (canRestart) r.push_back(Restart);
    if (story) r.push_back(Briefing);
    r.push_back(Quit);
    return r;
}

// WXMsg.CreatePopUp: the list (-1) or a confirm popup; leaving one is a KillPopUpNamed (no Out_* anim, its click3)
void Pause::popUp(int which) {
    confirm = which, yes = 0, shown = now();
    if (which < 0) row = 0;
}

Pause::Action Pause::update() {
    using S = Audio::Sfx;
    float t = now(), dt = t - last;
    veil = open ? fminf(160, veil + dt * 800) : fmaxf(0, veil - dt * 800);  // 0x7250a9: linear 200 ms fade to the new FullScreenColour
    bool popped = !help && (brief >= 0 || open), incoming = popped && last < shown + popupDelay(confirm, brief >= 0) && t >= shown + popupDelay(confirm, brief >= 0);
    if (incoming) Audio::play(brief >= 0 || confirm >= 0 ? S::FeScaleHit : S::FePopupIn);  // Audio_Incoming In_ScaleHitXY / In_ScaleY, with the anim
    last = t;
    bool plus = P({PLUS}, {KEY_ESCAPE, KEY_P});
    if (!open && brief < 0) {  // closed: A / B are the match's, P() would play their menu sounds
        if (plus) open = true, help = false, popUp(-1);
        return None;
    }
    bool ok = P({A}, {KEY_ENTER, KEY_SPACE}), back = plus || P({B}, {KEY_BACKSPACE});
    if (brief >= 0) {  // Return / Cancel kill it; MenuGoingAway: click3, App.Resume (back to the match, not the pause menu)
        if (ok || back) brief = -1, open = false, Audio::play(S::HpTick);
        return None;
    }
    if (help) {
        if (ok || back) help = false, Audio::play(S::FePrevIn);
        return None;
    }
    if (confirm >= 0) {  // ConfirmList: No / Cancel re-create WXFEP.Pause, Yes resumes and acts
        yes = clampWrap(yes + P({DOWN}, {}) - P({UP}, {}), 2);
        if (back || (ok && !yes)) Audio::play(S::HpTick), popUp(-1);
        if (!ok || !yes) return None;
        int c = confirm;
        open = false, confirm = -1, Audio::play(S::HpTick);
        return c == Draw ? DoDraw : c == Restart ? DoRestart : DoQuit;
    }
    std::vector<int> r = rows();
    row = clampWrap(row + P({DOWN}, {}) - P({UP}, {}), (int)r.size());
    if (back || (ok && r[row] == Continue)) open = false, Audio::play(S::HpTick);  // KillPopUpNamed$WXFEP.Pause, App.Resume, EFMV.Borders.On
    else if (ok && r[row] == Options) help = true, Audio::play(S::FeController);
    else if (ok && r[row] == Briefing) brief = shown = t, Audio::play(S::HpTick);  // the Pause's kill (click3), no App.Resume
    else if (ok) Audio::play(S::HpTick), popUp(r[row]);
    return None;
}

// WXFEP.MissionBriefing: FE units (500 tall like the Story / Challenges pages, y up) about the screen centre; ImageView / TextBox Scale = half extents
void Pause::briefing() const {
    const MissionSpec *m = story;
    if (!m || !feItem({640, 360}, &IN_SCALEY, popupDelay(-1, true), nullptr, now() - brief, -1)) return;
    auto fe = [](float x, float y) { return Vector2{640 + x * FE_S, 360 - y * FE_S}; };
    auto box = [&](float x, float y, float hw, float hh) { Vector2 c = fe(x, y); return Rectangle{c.x - hw * FE_S, c.y - hh * FE_S, 2 * hw * FE_S, 2 * hh * FE_S}; };
    image("fe/paperpopup01", box(0, 0, 180, 140));       // Paper Back, WXFE.PaperPopUp1
    image("fe/speechpopup_divide", box(0, 81, 70, 8));   // DIVIDE1, WXFE.PopUpDivide
    float title = 20 * FE_S;                            // PopUp Title: FETXT.MissionBriefing, FontSizeOverride 20, centred
    const char *head = tr("FETXT.MissionBriefing", "Mission Objectives", "Objectifs de la mission");
    float hx = 640 - textWidth(head, title) / 2;  // TextAnim MediumWobble: each glyph held at High5 / Low5 (+-0.7 FE), picked once (0x6a9920)
    uint32_t r = (uint32_t)(brief * 1000) | 1;
    for (int i = 0, n = 1; head[i]; i += n) {
        GetCodepointNext(head + i, &n);
        std::string g(head + i, n);
        r = r * 0x41c64e6du + 0x3039u;
        textG(g.c_str(), hx, fe(0, 103).y - title / 2 + (r % 2 ? 0.7f : -0.7f) * FE_S, title, GC_BUTTON_YELLOW[0], GC_BUTTON_YELLOW[1], 0);
        hx += textWidth(g.c_str(), title);
    }
    Texture2D bullet = tex("fe/teaminfo04");  // WXFE.HandicapAllies:7, its XTexFont's last frame (u 0.75, v 0, 0.25 x 0.5, GL v up)
    for (int i = 0; i < 3; i++) {  // 0x4cbb20: WXD.Mission.Obj<i> = text of "<Objectives>.<A+i>", empty (and no bullet) when the key is missing
        std::string key = m->objectives + "." + (char)('A' + i);
        const char *s = m->objectives.empty() ? "" : tr(key.c_str(), "");
        if (!*s) continue;
        Rectangle b = box(15, 40 - 50.0f * i, 140, 20);  // ObjectiveN: top-left justified, FE.TextBoxTextSize 20, AutoScale
        float size = 20;  // AutoScale (0x75f1b2): wrapped, then 1 FE smaller while lines x size > the box, down to 5; one line never shrinks
        for (float h; size > 5 && (h = paragraph(s, b.x, b.y, b.width, size * FE_S, WHITE, 99, false, nullptr, 0)) > b.height && h > size * FE_S;) size -= 1;
        paragraph(s, b.x, b.y, b.width, size * FE_S, GC_LIST_LABLE_BLUE[0], 99, true, &GC_LIST_LABLE_BLUE[1], 0);
        if (bullet.id) DrawTexturePro(bullet, {0.75f * bullet.width, 0.5f * bullet.height, 0.25f * bullet.width, 0.5f * bullet.height}, box(-140, 52 - 50.0f * i, 10, 10), {}, 0, WHITE);
    }
    float row = 20 * FE_S, y = fe(0, -99).y - row / 2;  // Return: ConfirmList row 26 at FontSize 20, highlighted (default item): charcoal border, kGC_Solid_White
    const char *ret = tr("FETXT.ReturnToMenu", "Return", "Retour");
    float w = textWidth(ret, row) + row * 0.9f;
    mark({640 - w / 2, y - row * 0.1f, w, row * 1.15f}, true);
    text(ret, 640, y, row, WHITE, 1);
    rlPopMatrix();
}

void Pause::draw() const {
    menuPage = true;
    DrawRectangle(0, 0, 1280, 720, {40, 40, 60, (unsigned char)veil});  // every pause popup's FullScreenColour (40, 40, 60, 160)
    if (brief >= 0) {
        briefing();
        hints({{"A", "Enter", tr("FETXT.ReturnToMenu", "Return", "Retour")}});
        return;
    }
    if (!open) return;
    if (help) {
        controls(true);
        hints({{"B", "Esc", "Back"}});
        return;
    }
    float t = now();
    bool in = feItem({640, 360}, &IN_SCALEY, popupDelay(confirm, false), nullptr, t - shown, -1);  // Anim_Incoming In_ScaleY
    if (confirm >= 0) {
        const char *q = confirm == Draw ? tr("FETXT.ConfirmDraw", "Draw the round?", "Déclarer le match nul ?")
                      : confirm == Restart ? tr("FETXT.ConfirmRestart", "You want to restart?", "Voulez-vous recommencer ?")
                                           : tr("FETXT.ConfirmQuit", "Exit Game? Are You Sure?", "Quitter le jeu ? Vraiment ?");
        if (in) yesNo(q, yes, t), rlPopMatrix();
        hints({{"A", "Enter", "Select"}, {"B", "Esc", "Back"}});
        return;
    }
    std::vector<int> r = rows();
    int n = (int)r.size();
    float y0 = 150 - 36.0f * (n - 3);
    if (in) {
        popup({420, y0, 440, 400 + 72.0f * (n - 3)});
        heading(tr("FE.Header.Paused", "Game Paused", "Pause"), 640, y0 + 30, 50);
        for (int k = 0; k < n; k++) {  // 0x4cd3a0's row texts
            const char *s = r[k] == Continue ? tr("FETXT.ResumeGame", "Resume Game") : r[k] == Options ? tr("FETXT.Help&Options", "Help & Options")
                          : r[k] == Draw ? tr("Lang.DrawRound", "Draw Round") : r[k] == Restart ? tr("FETXT.Restart", "Restart")
                          : r[k] == Briefing ? tr("FETXT.MissionBriefing", "Mission Objectives") : tr("Lang.Quit", "Quit");
            item(s, 640, y0 + 135 + k * 72.0f, fminf(42, 42 * 360 / textWidth(s, 42)), k == row);
        }
        rlPopMatrix();
    }
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

// ---------------------------------------------------------------- single player

static std::string clockText(int ticks) { return TextFormat("%d:%02d.%d", ticks / 3600, ticks / 60 % 60, ticks % 60 / 6); }

// Word-wrapped text, at most maxLines lines; returns the height used (draw false: only measures).
static float paragraph(const std::string &s, float x, float y, float w, float size, Color c, int maxLines, bool draw, const Color *c2, float lead) {
    std::string line, word;
    int lines = 0;
    auto flush = [&] { if (lines < maxLines && draw) textG(line.c_str(), x, y + lines * (size + lead), size, c, c2 ? *c2 : c, 0); lines++, line.clear(); };
    for (size_t i = 0; i <= s.size(); i++) {
        char ch = i < s.size() ? s[i] : ' ';
        if (ch != ' ' && ch != '\n') { word += ch; continue; }
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(t.c_str(), size) > w) flush(), t = word;
        line = t, word.clear();
        if (ch == '\n') flush();
    }
    if (!line.empty()) flush();
    return std::min(lines, maxLines) * (size + lead);
}

// WXFE.Story / WXFE.Challenges (docs/w4m/frontend.md §17): one page per mission, every item at its FE position with the clip and delay of its W4M
// counterpart; W4M's Out None items fly out like our menu rows (rowAppear, user-requested)
static const float MISSION_OUT = 0.25f;  // Out_ScaleY is done at 0.19 s (x FE.AnimSpeed); then the header is cut mid Out_TitleUnderline

// MESH Book: WX.Mesh.StoryBook, Lighting 1, clip Intro_Book (Delay_Out_Mesh 100 ms: Outro_Book is one key that drops the book out of view) [data]
static void feBook(float since, float leaving) {
    feMesh("storybook", {10, 15, 0}, {0.9f, 0.9f, 0.9f}, {0.05f, 0.03f, -0.1f}, true, leaving >= 0.1f ? "Outro_Book" : "Intro_Book", since);
}


// WXFE.MoviePlayer (MENUTWKX; docs/w4m/acting.md): the Logo, the two film frame meshes, the movie in HUD.Video tilted inside them, PanelBacking
// (Load.Hint.Panel) and the MovieText subtitle under it. False once the movie ended or was skipped.
bool movieFrame(float t) {
    menuPage = true;
    FrontBg::page(1);
    divide(0);
    FeFlat flat;
    if (t >= 0.2f) feMesh("filmtape1", {46, 22, 0}, {0.36f, 0.36f, 1}, {0, 0, 0}, false, "Film01Start", t - 0.2f);
    if (t >= 0.1f) feMesh("filmtape2", {0, 0, 0}, {0.32f, 0.28f, 1}, {0, 0, 0}, false, "Film02Start", t - 0.1f);
    bool on = Movie::update(feRect(-2, 42, 202, 112.7f), -0.05f);
    if (feItem(fePos(-192, 142), &IN_FLIPY, 0.3f, nullptr, t, -1)) logo(fePos(-192, 142).x, fePos(-192, 142).y - 68.2f * FE_S, 266 * FE_S, -0.27f * RAD2DEG), rlPopMatrix();
    Rectangle r = feRect(0, -140, 280, 120);
    if (Texture2D panel = tex("fe/hintpanel"); panel.id && t >= 0.1f) {
        rlPushMatrix();
        rlTranslatef(r.x + r.width / 2, r.y + r.height / 2, 0);
        rlRotatef(0.025f * RAD2DEG, 0, 0, 1);
        DrawTexturePro(panel, {0, 0, (float)panel.width, (float)panel.height}, {-r.width / 2, -r.height / 2, r.width, r.height}, {}, 0, WHITE);
        rlPopMatrix();
    }
    if (feItem(fePos(-4, -138), &IN_TITLEUNDERLINE, 0.08f, nullptr, t, -1)) {
        FeBox b;
        b = {{-4, -138}, 260, 43, 20, 0.054f, -1, 4, 0, true, GC_WHITE, BC_BLACK};
        feText(Movie::subtitle(), b), rlPopMatrix();
    }
    return on;
}


int missionMenu(MissionMenu &st, const std::vector<MissionSpec> &list, const Progress &p) {
    using S = Audio::Sfx;
    menuPage = true;
    FrontBg::page(1);
    static float last = 0;  // previous frame's clock: the Challenges image's In_Speech sound at its 300 ms delay
    float t = now();
    bool story = st.tab == 0;
    if (st.shown == -1) {  // the screen's Audio_Incoming In_Controller, and the Story book's In_Book
        st.shown = t, st.leaving = -1, Audio::play(S::FeController);
        if (story) Audio::play(S::FeBookIn);
    }
    if (st.leaving >= 0 && t - st.leaving >= MISSION_OUT) {  // its items are out: what follows them
        st.leaving = -1, st.shown = -1;
        return st.to;
    }
    if (!story && last - st.shown < 0.3f && t - st.shown >= 0.3f) Audio::play(S::FeSpeech);
    last = t;
    float since = t - st.shown, out = st.leaving >= 0 ? t - st.leaving : -1;
    bool busy = st.leaving >= 0;
    // to: -2 back, else the mission to start; leaving the screen plays its Audio_Outgoing Out_Next (the book's Out_Book is silent)
    auto leave = [&](int to) {
        st.leaving = t, st.to = to;
        Audio::play(S::FeNextOut);
        if (story) Audio::play(S::FeBookOut);
    };
    std::vector<int> rows;
    for (size_t i = 0; i < list.size(); i++) if ((list[i].kind == "mission") == story) rows.push_back((int)i);
    int &sel = st.sel[st.tab], n = (int)rows.size();
    sel = n ? Clamp(sel, 0, n - 1) : 0;
    if (!busy && n) {  // BUTTON Prev / Next (WXFE.ShowPrevLevel / ShowNextLevel, WXMsg.Challenge^Previous^ / ^Next^): no wrap, disabled at the ends
        int d = P({RIGHT}, {KEY_RIGHT}) - P({LEFT}, {KEY_LEFT});
        if (d && sel + d >= 0 && sel + d < n) sel += d, Audio::play(story ? S::FePage : S::FeNextIn);
    }
    const MissionSpec *m = n ? &list[rows[sel]] : nullptr;
    bool open = m && p.unlocked(list, rows[sel]);
    bool ok = !busy && open && P({A}, {KEY_ENTER, KEY_SPACE}), back = !busy && P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
    const char *title = story ? tr("FETXTH.Story", "STORY", "HISTOIRE") : tr("FETXTH.Challenges", "CHALLENGES", "DÉFIS");
    Progress::Entry e = m ? p.get(m->id) : Progress::Entry{};
    auto slide = [&](float x, float y, const FeClip *in, float delay, const FeClip *outc, float slideBy = -1300) { return feItem(fePos(x, y), in, delay, outc, since, out, slideBy); };

    divide(0);
    watermark(title, 0, t, since, out);
    if (story) feBook(since, out);
    if (slide(0, -159.1f, &IN_SCALEY, 0.3f, &OUT_SCALEY)) tooltipStrip(), rlPopMatrix();  // ToolTip
    if (slide(11, -159, &IN_SLIDEX, 0.3f, &OUT_SCALEY)) tickerText(t), rlPopMatrix();  // TickerTapeTXT
    {
        FeFlat flat;
        if (feItem(fePos(-152, 188), &IN_TITLEUNDERLINE, 0.3f, &OUT_TITLEUNDERLINE, since, out)) titleControl(title, 0), rlPopMatrix();
    }
    if (m) {
        std::string img = m->preview.empty() ? preview(m->map) : m->preview;
        auto picture = [&](float x, float y, float hw, float hh, float rot) {
            Rectangle r = feRect(x, y, hw, hh);
            Vector2 c = {r.x + r.width / 2, r.y + r.height / 2};
            rlPushMatrix();
            rlTranslatef(c.x, c.y, 0);
            rlRotatef(-rot * RAD2DEG, 0, 0, 1);
            rlTranslatef(-c.x, -c.y, 0);
            if (!image(img, r, open ? WHITE : GRAY) && !image(preview(m->map), r, open ? WHITE : GRAY)) DrawRectangleRec(r, {30, 60, 40, 255});
            rlPopMatrix();
        };
        std::string name = tr(m->nameId.c_str(), m->name.c_str()), brief = tr(m->briefId.c_str(), m->brief.c_str());
        if (story) {
            FeFlat onBook;  // the book mesh keeps its place
            FeBox b;
            if (slide(-128, 139, &IN_SLIDEX, 0.8f, nullptr)) {  // Mission Title
                b = {{-128, 139}, 85, 20, 22, -0.02f, -1, 4, 4, true, GC_POPUP_RED, BC_BOOK_EDGE};
                feText(name, b), rlPopMatrix();
            }
            if (slide(154, 23, &IN_TOOLTIP, 0.8f, nullptr)) {  // Mission Briefing
                b = {{154, 23}, 111, 108, 18, -0.03f, fmaxf(since - 0.8f, 0), 0, 0, false, GC_BOOK_BROWN, BC_BOOK_EDGE};
                feText(brief, b), rlPopMatrix();
            }
            if (slide(-140, -100, &IN_SLIDEX, 0.8f, nullptr)) {  // LABEL Page
                b = {{-140, -100}, 50, 10, 20, -0.05f, -1, 4, 0, false, GC_BOOK_BROWN, BC_BOOK_EDGE};
                feText(TextFormat(tr("FETXT.PageN", "Page %u"), (unsigned)sel + 1), b), rlPopMatrix();
            }
            for (int k = 0; k < 2; k++) {  // BUTTON Prev / Next
                Vector2 c = k ? Vector2{-83, -104} : Vector2{-197, -98};
                if (slide(c.x, c.y, &IN_SPEECH, 0.75f, nullptr)) pageArrow(k, sel + (k ? 1 : -1) < 0 || sel + (k ? 1 : -1) >= n ? 2 : 0, c, 20), rlPopMatrix();
            }
            if (slide(-138, 17, &IN_SPEECH, 0.73f, nullptr)) picture(-138, 17, 100, 100, -0.04f), rlPopMatrix();  // Mission Image
            if (slide(150, -115, &IN_SLIDEX, 0.8f, nullptr)) {  // Bonus Time text (MissionService 0x733c94)
                bool won = std::find(p.unlocks.begin(), p.unlocks.end(), "Lock.T." + m->level) != p.unlocks.end();
                b = {{150, -115}, 97, 20, 18, -0.03f, -1, 4, 0, true, GC_BOOK_BROWN, BC_BOOK_EDGE};
                feText(won ? tr("FETXT.TimeBonusWon", "Time Bonus Achieved") : TextFormat(tr("FETXT.TimeBonusFormat", "Time Bonus: %um %us"), m->par / 60, m->par % 60), b);
                rlPopMatrix();
            }
            if (slide(-131, 112, &IN_SLIDEX, 0.85f, nullptr)) {  // DIVIDE1
                Rectangle r = feRect(-131, 112, 70, 8);
                rlPushMatrix(), rlTranslatef(r.x + r.width / 2, r.y + r.height / 2, 0), rlRotatef(0.05f * RAD2DEG, 0, 0, 1);
                image("fe/speechpopup_divide", {-r.width / 2, -r.height / 2, r.width, r.height}, {255, 255, 255, 170});
                rlPopMatrix(), rlPopMatrix();
            }
            if (e.done && slide(-210, -20, &IN_SPRINGXY, 1.5f, nullptr)) image("fe/mission_complete", feRect(-210, -20, 60, 60)), rlPopMatrix();  // Mission Completed
        } else {
            if (slide(124, 13, &IN_SPEECH, 0.35f, &OUT_SCALEY)) image("fe/paperpopup02", feRect(124, 13, 155, 151)), rlPopMatrix();  // Paper Back
            if (slide(-164, 8, &IN_SPEECH, 0.3f, nullptr)) picture(-164, 8, 110, 110, 0.02f), rlPopMatrix();  // IMAGE Level
            if (slide(-181, 147, &IN_TITLEUNDERLINE, 0.35f, &OUT_TITLEUNDERLINE)) {  // SUBHEADER Teamname: User.HumanTeamName
                FeBox b = {{-181, 147}, 110, 15, 23, 0, -1, 3, 4, true, GC_SUBHEADER_BLUE, BC_BLACK};
                feText(st.team, b), rlPopMatrix();
            }
            if (slide(123, 123, &IN_SPEECH, 0.35f, nullptr)) {  // Challenge Title, and its children: Body Text, BUTTON Prev / Next, Mission Record, LABEL BestTime
                FeBox b = {{123, 123}, 90, 22, 28, 0, -1, 4, 0, true, GC_SUBHEADER_BLUE, BC_BLACK};
                feText(name, b);
                if (feItem(fePos(123, 15), &IN_SLIDEX, 0, nullptr, since - 0.35f, out)) {  // Body Text
                    b = {{123, 15}, 130, 73, 18, 0, fmaxf(since - 0.35f, 0), 0, 0, true, GC_WHITE, BC_BLACK};
                    feText(brief, b), rlPopMatrix();
                }
                for (int k = 0; k < 2; k++) pageArrow(k, sel + (k ? 1 : -1) < 0 || sel + (k ? 1 : -1) >= n ? 2 : 0, {123.0f + (k ? 110 : -110), 123}, 25);
                float rec = m->record;  // Mission Record: HighScores[Level_Number] of DEFSAVE.XOM (0x72c28f) until a better time is set [data + disasm]
                std::string who = m->recordBy;
                if (e.done && e.best / 60.0f < rec) rec = e.best / 60.0f, who = st.team;
                int sec = (int)rec;
                b = {{123, -97}, 123, 15, 18, 0, -1, 4, 0, true, GC_SUBHEADER_BLUE, BC_BLACK};
                feText(who + " - " + (sec >= 3600 ? TextFormat("%d:%02d:%02d", sec / 3600, sec / 60 % 60, sec % 60) : TextFormat("%d:%02d", sec / 60, sec % 60)), b);
                b = {{123, -75}, 120, 20, 20, 0, -1, 4, 0, false, GC_WHITE, BC_BLACK};  // LABEL BestTime
                feText(tr("FETXT.BestTime", "Best Time"), b);
                float bw = textWidth(tr("FETXT.BestTime", "Best Time"), 20 * FE_S) * TEXT_X / FE_S / 2 + 6;  // the list row's two brush rules
                image("fe/title_underline", feRect(123 - bw - 20, -75, 19, 2.5f));
                image("fe/title_underline", feRect(123 + bw + 20, -75, 19, 2.5f));
                rlPopMatrix();
            }
        }
    } else {
        FeBox b = {{0, 0}, 300, 20, 28, 0, -1, 4, 0, false, GC_WHITE, BC_BLACK};
        if (slide(0, 0, nullptr, 0, nullptr)) feText(story ? "No missions found" : "No challenges found", b), rlPopMatrix();
    }
    if (slide(-247, -159, &IN_PREV, 0.3f, &OUT_PREV)) navButton(2, 0, {-247, -159}, 45), rlPopMatrix();  // NAV Prev
    if (slide(264, -166, &IN_SPRINGXY, 0.3f, &OUT_SCALEXY)) navButton(0, open ? 0 : 2, {264, -166}, 40), rlPopMatrix();  // NAV Start
    if (back) leave(-2);
    else if (ok) leave(rows[sel]);
    return -1;
}

int missionEnd(const Game &g, const MissionSpec &m, const Progress::Entry &best, bool hasNext) {
    bool won = g.run.result > 0;
    DrawRectangle(0, 0, 1280, 720, {0, 0, 0, 110});
    popup({340, 170, 600, 380});
    text(m.kind == "mission" ? (won ? "MISSION COMPLETE!" : "MISSION FAILED") : (won ? "CHALLENGE COMPLETE!" : "CHALLENGE FAILED"), 640, 196, 44, won ? GOLDEN : ORANGE, 1);
    text(tr(m.nameId.c_str(), m.name.c_str()), 640, 252, 28, WHITE, 1);
    paragraph(won ? tr(m.successId.c_str(), m.success.c_str()) : m.failure, 380, 300, 520, 22, LIGHTGRAY, 3);
    text(TextFormat("Time  %s", clockText(g.run.ticks).c_str()), 640, 400, 32, WHITE, 1);
    if (best.done) text(won && best.best == g.run.ticks ? "New best time!" : TextFormat("Best  %s", clockText(best.best).c_str()), 640, 444, 26, GOLDEN, 1);
    if (won && hasNext) hints({{"A", "Enter", "Next"}, {"X", "R", "Retry"}, {"B", "Backspace", "Back to list"}});
    else hints({{"A", "Enter", won ? "Continue" : "Retry"}, {"B", "Backspace", "Back to list"}});
    if (P({A}, {KEY_ENTER, KEY_SPACE})) return won ? (hasNext ? 1 : 3) : 2;
    if (P({X}, {KEY_R})) return 2;
    return P({B}, {KEY_BACKSPACE, KEY_ESCAPE}) ? 3 : 0;
}

// MENUTWKX WXFE.EasterEggFound: a paper popup, WXFE.EasterEgg.Name over WXFE.EasterEgg.Coins (MissionService 0x72c567), Return
bool eggFound(const char *nameId, int coins) {
    DrawRectangle(0, 0, 1280, 720, {40, 40, 60, 160});  // FullScreenColour
    popup({390, 220, 500, 280});
    std::string name = tr(nameId, nameId);
    wrapped(TextFormat(tr("FETXT.EasterEgg.Name.Template", "You found the %s Easter Egg!"), name.c_str()), 640, 270, 440, 30, GOLDEN);
    text(TextFormat(tr("FETXT.EasterEgg.Coins.Template", "%u Coins awarded."), (unsigned)coins), 640, 380, 26, WHITE, 1);
    hints({{"A", "Enter", "Return"}});
    return P({A}, {KEY_ENTER, KEY_SPACE}) || P({B}, {KEY_BACKSPACE, KEY_ESCAPE});
}

#include "results.inc"

}  // namespace Ui

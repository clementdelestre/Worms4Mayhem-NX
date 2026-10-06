#include "loading.h"
#include "audio.h"
#include "frontbg.h"
#include "terrain.h"
#include "ui.h"
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <string>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {
// WXFE.PreStart pops in at its 250 ms Delay_Incoming and sends StartGame$NOW once its last item (1500 ms + In_ScaleY 0.25 s)
// is in; NewLoadingScreenService then shows at least 1800 ms (0x50a4c0 +0x28). MUSIC_OUT: femusic's FEV fade-out.
const float POP = 0.25f, POOF = 0.3f, INTRO = 1.75f, FADE = 0.25f, MIN_LOAD = 1.8f, IN = 0.4f, MUSIC_OUT = 2, TIP_EVERY = 15;
const int TIPS = 44;  // FETXT.Tip1..44 (W4M's <lang>Loading.xom)
float t = 0, out = -1, after = 1e9f, tipAt = 0;  // out: fade-out clock once loaded; after: since the match started
int tip = 26;
const char *back = "back/loadbackgeneric";

void sprite(Texture2D x, Vector2 c, float size, float deg, Color tint) {
    DrawTexturePro(x, {0, 0, (float)x.width, (float)x.height}, {c.x, c.y, size, size}, {size / 2, size / 2}, deg, tint);
}

// Stage 1 (WXFE.PreStart): the menu scene unveiled (its FullScreenColour applies only to popups), then the logo pops in
// (In_ScaleHitXY) with the Grenade sound and the WXP_FE_BigPoof smoke (SpawnDelay 300 ms) swirls around it
void intro() {
    Ui::background();
    float p = t - POP, tp = t - POOF;
    if (p < 0) return;
    Texture2D puff = Ui::art("hud/trailparticle");  // W4M's cartoon smoke cluster
    if (!puff.id) puff = Ui::art("hud/smoke_sphere");
    const int N = 22;
    const Vector2 c = {640, 330};
    float spread = 1 - expf(-fmaxf(tp, 0) * 3), w = 700 * Ui::clipKeys(Ui::IN_SCALEHIT_S, 6, p);
    Vector2 shake = {1.5f * Ui::clipKeys(Ui::IN_SCALEHIT_X, 7, p), -1.5f * Ui::clipKeys(Ui::IN_SCALEHIT_Y, 7, p)};  // FE units, y up
    for (int front = 0; front < 2; front++) {  // back half, logo, front half: the ring wraps around it
        for (int i = 0; i < N && tp > 0; i++) {
            float a = i * 2 * PI / N + t * 1.2f, depth = sinf(a);
            if ((depth > 0) != (front == 1)) continue;
            Vector2 q = {c.x + cosf(a) * 450 * spread, c.y + 30 + depth * 170 * spread + sinf(t * 2 + i) * 10};
            float size = (120 + 35 * sinf(i * 1.7f + t * 3)) * (0.8f + 0.25f * depth);
            unsigned char g = (unsigned char)(255 * (1 - 0.45f * tp / 2));  // BigPoof ParticleColor white -> 0.55 over its 2 s life
            Color col = {g, g, g, (unsigned char)(220 * fminf(tp / 0.3f, 1))};
            if (puff.id) sprite(puff, q, size, i * 40 + t * 30, col);
            else DrawCircleV(q, size * 0.45f, col);
        }
        if (!front) Ui::logo(c.x + shake.x, c.y - w * 400 / 780 / 2 + shake.y, w);
    }
}

// Stage 2: dark-blue panel with the "Loading" watermark, round worm, logo and a tip
void loading() {
    float ls = t - INTRO;
    Texture2D bg = Ui::art(back);
    if (bg.id) DrawTexturePro(bg, {0, 0, (float)bg.width, (float)bg.height}, {0, 0, 1280, 720}, {}, 0, WHITE);
    else DrawRectangleGradientV(0, 0, 1280, 720, {40, 80, 150, 255}, {120, 170, 220, 255});
    // the BlueDivide slides in from x -700 to -205 units (1.33 px each): 0x50a930 keeps 0.7 of the gap per >= 33 ms render
    float cx = -700 - 660 * powf(0.7f, ls / 0.033f);
    DrawCircleSector({cx, 360}, 1680, 0, 360, 240, {24, 62, 118, 255});
    DrawCircleSector({cx, 360}, 1665, 0, 360, 240, {10, 32, 70, 255});
    // W4M's watermark: packed columns of the word read bottom to top, big/medium/small, a shade above the panel, cut by its curve;
    // 0x50a930 grows the columns from scale 0 over 300 ms, 400 ms after the screen opens
    const char *word = Ui::tr("Text.Loading", "Loading", "Chargement");
    const Font &f = Ui::textFont();
    static const float SIZES[] = {190, 130, 165, 105, 180, 140, 120};
    float x = -30, grow = fminf(fmaxf((ls - 0.4f) / 0.3f, 0), 1);
    for (int col = 0; x < cx + 1665 && grow > 0; col++) {
        float size = SIZES[col % 7], step = MeasureTextEx(f, word, size, 0).x + size * 0.3f;
        float y0 = 720 + fmodf(col * 211.0f + t * (col % 2 ? 6 : -6), step);  // barely drifting
        auto draw = [&] { for (float y = y0 + step; y > -step; y -= step) DrawTextPro(f, word, {x, y}, {}, -90, size * grow, 0, {19, 45, 90, 255}); };
        float w = size * 0.8f;
        if (x + w < cx + 1625) draw();  // clear of the curve top to bottom
        else for (float sx = x; sx < x + w; sx += 6) {
            float h = sqrtf(fmaxf(1665.0f * 1665 - (sx - cx + 6) * (sx - cx + 6), 0));
            BeginScissorMode((int)sx, (int)(360 - h), 6, (int)(2 * h)), draw(), EndScissorMode();
        }
        x += w;
    }
    Texture2D worm = Ui::art("fe2/loading_worm");
    Vector2 wc = {640, 118};
    if (worm.id) sprite(worm, wc, 190, ls * 360 / 1.166f, WHITE);  // FE.LoadingIcon "Rotate": one turn in 1.166 s, looped
    else DrawCircleV(wc, 90, {225, 230, 240, 255}), DrawCircleV(wc, 60, {240, 160, 150, 255});
    Ui::logo(640, 215, 560);
    Texture2D box = Ui::art("fe/hintpanel");
    Rectangle r = {290, 530, 700, 170};
    if (box.id) DrawTexturePro(box, {2, 95, 1020, 322}, r, {}, 0, {120, 255, 225, 255});  // stored blue, shown teal
    else DrawRectangleRounded(r, 0.15f, 6, {20, 95, 110, 255});
    // the tip, word-wrapped and centred in the box
    std::string s = Ui::tr(TextFormat("FETXT.Tip%d", tip), "Use your hotseat time to scan the environment before moving.",
                           "Utilisez le laps de temps entre chaque tour pour observer les environs avant de vous déplacer."), line, lines[4];
    int n = 0;
    for (size_t i = 0; i <= s.size() && n < 4; i++) {
        size_t e = s.find(' ', i);
        if (e == std::string::npos) e = s.size();
        std::string next = line.empty() ? s.substr(i, e - i) : line + " " + s.substr(i, e - i);
        if (!line.empty() && Ui::textWidth(next.c_str(), 26) > r.width - 90) lines[n++] = line, next = s.substr(i, e - i);
        line = next, i = e;
    }
    if (n < 4 && !line.empty()) lines[n++] = line;
    for (int i = 0; i < n; i++) Ui::text(lines[i].c_str(), r.x + r.width / 2, r.y + r.height / 2 - n * 15 + i * 30, 26, {184, 184, 184, 255}, 1);  // 0x608100
}
}  // namespace

namespace Loading {
void begin(const GameConfig &c) {
    t = 0, out = -1, after = 1e9f, tipAt = INTRO;
    tip = 1 + (int)(c.seed % TIPS);
    std::string th = Terrain::mapTheme(c.map);  // 0x509c50: LoadBack<theme>.tga, else Generic
    back = th == "arabian" ? "back/loadbackarabian" : th == "wildwest" ? "back/loadbackwildwest" : th == "camelot" ? "back/loadbackcamelot"
         : th == "jurassic" ? "back/loadbackprehistoric" : th == "construction" ? "back/loadbackbuilding" : "back/loadbackgeneric";
    FrontBg::page(1);  // open sea
}

// GL steps block between frames: start them once the menu music is out, so no stream needs feeding meanwhile
bool ready() { return t >= INTRO + MUSIC_OUT; }

bool frame(float dt, float progress) {
    float was = t;
    t += dt;
    if (was < POP && t >= POP) Audio::play(Audio::Sfx::FeGrenade);  // PreStart's Audio_Incoming, with its pop-in
    if (was < INTRO && t >= INTRO) Audio::music(false);  // FrontEndService shutdown 0x7293dd: femusic stop with fade-out
    if (t - tipAt >= TIP_EVERY) {  // 0x608100: a new tip every 15 s, never the same twice
        int n = tip;
        while (n == tip) n = GetRandomValue(1, TIPS);
        tip = n, tipAt = t;
    }
    if (t < INTRO) intro();
    else loading();
    if (out < 0 && progress >= 1 && t >= INTRO + MIN_LOAD) out = 0;
    if (out >= 0) out += dt;
    // black: in from the menu, through it between the stages, out to the match
    float k = t < FADE ? 1 - t / FADE : t < INTRO ? (t - INTRO + FADE) / FADE : t < INTRO + FADE ? 1 - (t - INTRO) / FADE : 0;
    k = fmaxf(k, out >= 0 ? out / FADE : 0);
    DrawRectangle(0, 0, 1280, 720, Fade(BLACK, fminf(fmaxf(k, 0), 1)));
    if (out < FADE) return false;
    after = 0;
    return true;
}

void pinCore(int core) {
#ifdef __SWITCH__
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);  // libnx threads start on core 0, the main thread's
#else
    (void)core;
#endif
}

// readdir's d_type, not raylib's LoadDirectoryFilesEx: that stats every entry twice, an SD request each on Switch
std::vector<std::string> list(const std::string &dir, const char *ext, bool recurse) {
    std::vector<std::string> out;
    DIR *d = opendir(dir.c_str());
    if (!d) return out;
    bool dirs = !strcmp(ext, "/");
    size_t n = strlen(ext);
    while (dirent *e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string p = dir + "/" + e->d_name;
        struct stat st;
        bool isDir = e->d_type == DT_UNKNOWN ? !stat(p.c_str(), &st) && S_ISDIR(st.st_mode) : e->d_type == DT_DIR;
        size_t l = strlen(e->d_name);
        if (isDir ? dirs : !dirs && l >= n && !strcasecmp(e->d_name + l - n, ext)) out.push_back(p);
        if (isDir && recurse) for (std::string &f : list(p, ext, true)) out.push_back(std::move(f));
    }
    closedir(d);
    return out;
}

void overlay(float dt) {
    if (after < IN) DrawRectangle(0, 0, 1280, 720, Fade(BLACK, 1 - after / IN));
    after += dt;
}
}  // namespace Loading

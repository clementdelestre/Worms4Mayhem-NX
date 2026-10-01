#include "loading.h"
#include "frontbg.h"
#include "ui.h"
#include <cmath>
#include <string>

namespace {
const float INTRO = 1.5f, FADE = 0.25f, MIN_LOAD = 1.5f, IN = 0.4f;  // seconds
const int TIPS = 44;  // FETXT.Tip1..44 (W4M's <lang>Loading.xom)
float t = 0, out = -1, after = 1e9f;  // out: fade-out clock once loaded; after: since the match started
int tip = 26;

void sprite(Texture2D x, Vector2 c, float size, float deg, Color tint) {
    DrawTexturePro(x, {0, 0, (float)x.width, (float)x.height}, {c.x, c.y, size, size}, {size / 2, size / 2}, deg, tint);
}

// Stage 1: the logo with grey smoke puffs swirling around it, over the title sea
void intro() {
    Ui::background();
    Texture2D puff = Ui::art("hud/trailparticle");  // W4M's cartoon smoke cluster
    if (!puff.id) puff = Ui::art("hud/smoke_sphere");
    const int N = 22;
    const Vector2 c = {640, 330};
    float spread = 1 - expf(-t * 3), w = 700 * (0.85f + 0.15f * fminf(t / 0.4f, 1));
    for (int front = 0; front < 2; front++) {  // back half, logo, front half: the ring wraps around it
        for (int i = 0; i < N; i++) {
            float a = i * 2 * PI / N + t * 1.2f, depth = sinf(a);
            if ((depth > 0) != (front == 1)) continue;
            Vector2 p = {c.x + cosf(a) * 450 * spread, c.y + 30 + depth * 170 * spread + sinf(t * 2 + i) * 10};
            float size = (120 + 35 * sinf(i * 1.7f + t * 3)) * (0.8f + 0.25f * depth);
            unsigned char g = (unsigned char)(150 + 45 * depth);
            Color col = {g, g, g, (unsigned char)(220 * fminf(t / 0.3f, 1))};
            if (puff.id) sprite(puff, p, size, i * 40 + t * 30, col);
            else DrawCircleV(p, size * 0.45f, col);
        }
        if (!front) Ui::logo(c.x, c.y - w * 400 / 780 / 2, w);
    }
}

// Stage 2: dark-blue panel with the "Loading" watermark, round worm, logo and a tip
void loading() {
    Texture2D back = Ui::art("back/loadbackgeneric");
    if (back.id) DrawTexturePro(back, {0, 0, (float)back.width, (float)back.height}, {0, 0, 1280, 720}, {}, 0, WHITE);
    else DrawRectangleGradientV(0, 0, 1280, 720, {40, 80, 150, 255}, {120, 170, 220, 255});
    DrawCircleSector({-700, 360}, 1680, 0, 360, 240, {24, 62, 118, 255});
    DrawCircleSector({-700, 360}, 1665, 0, 360, 240, {10, 32, 70, 255});
    // W4M's watermark: packed columns of the word read bottom to top, big/medium/small, a shade above the panel, cut by its curve
    const char *word = Ui::tr("Text.Loading", "Loading", "Chargement");
    const Font &f = Ui::textFont();
    static const float SIZES[] = {190, 130, 165, 105, 180, 140, 120};
    float x = -30;
    for (int col = 0; x < 965; col++) {
        float size = SIZES[col % 7], step = MeasureTextEx(f, word, size, 0).x + size * 0.3f;
        float y0 = 720 + fmodf(col * 211.0f + t * (col % 2 ? 6 : -6), step);  // barely drifting
        auto draw = [&] { for (float y = y0 + step; y > -step; y -= step) DrawTextPro(f, word, {x, y}, {}, -90, size, 0, {19, 45, 90, 255}); };
        float w = size * 0.8f;
        if (x + w < 925) draw();  // clear of the curve top to bottom
        else for (float sx = x; sx < x + w; sx += 6) {
            float h = sqrtf(fmaxf(1665.0f * 1665 - (sx + 706) * (sx + 706), 0));
            BeginScissorMode((int)sx, (int)(360 - h), 6, (int)(2 * h)), draw(), EndScissorMode();
        }
        x += w;
    }
    Texture2D worm = Ui::art("fe2/loading_worm");
    Vector2 wc = {640, 118};
    if (worm.id) sprite(worm, wc, 190, t * 120, WHITE);  // spins in place, like W4M
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
    for (int i = 0; i < n; i++) Ui::text(lines[i].c_str(), r.x + r.width / 2, r.y + r.height / 2 - n * 15 + i * 30, 26, WHITE, 1);
}
}  // namespace

namespace Loading {
void begin(const GameConfig &c) {
    t = 0, out = -1, after = 1e9f;
    tip = 1 + (int)(c.seed % TIPS);
    FrontBg::page(1);  // open sea
}

bool ready() { return t >= INTRO + FADE; }

bool frame(float dt, float progress) {
    t += dt;
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

void overlay(float dt) {
    if (after < IN) DrawRectangle(0, 0, 1280, 720, Fade(BLACK, 1 - after / IN));
    after += dt;
}
}  // namespace Loading

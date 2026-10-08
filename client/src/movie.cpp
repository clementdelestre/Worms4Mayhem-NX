#include "movie.h"
#include "audio.h"
#include "ui.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#ifdef __SWITCH__
#define DATA_DIR "sdmc:/switch/worms4nx/"
#else
#define DATA_DIR "./"
#endif

namespace Movie {
namespace {
const char *path(const char *name, const char *ext) { return TextFormat(DATA_DIR "assets/movies/%s.%s", name, ext); }

struct Header { int w = 0, h = 0, fps = 0; };

bool readHeader(FILE *f, Header &h) {
    unsigned char b[10];
    if (fread(b, 1, 10, f) != 10 || memcmp(b, "MJPG", 4)) return false;
    h = {b[4] | b[5] << 8, b[6] | b[7] << 8, b[8] | b[9] << 8};
    return h.w > 0 && h.h > 0 && h.fps > 0;
}

// next frame's JPEG length; 0 at the end of the file
unsigned frameLen(FILE *f) {
    unsigned char b[4];
    return fread(b, 1, 4, f) == 4 ? b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24 : 0;
}

struct Frame { int idx; unsigned char *rgb; };

struct Player {
    std::string name;
    Header hd;
    FILE *f = nullptr;
    std::thread th;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<Frame> q;
    std::atomic<bool> quit{false}, eof{false};
    std::atomic<int> target{0}, total{0};
    Texture2D tex{};
    int shownIdx = -1;
    double t0 = 0;
    Music snd{};
    bool sound = false, armed = false;
    Subs subs;
    std::string line;

    // Worker: frames in order; those the clock already passed are skipped without decoding, the queue is bounded
    void run() {
        std::vector<unsigned char> buf;
        for (int idx = 0; !quit; idx++) {
            unsigned n = frameLen(f);
            if (!n) { total = idx; eof = true; return; }
            if (idx < target) { fseek(f, n, SEEK_CUR); continue; }
            buf.resize(n);
            if (fread(buf.data(), 1, n, f) != n) { total = idx; eof = true; return; }
            int w, h, c;
            unsigned char *rgb = stbi_load_from_memory(buf.data(), n, &w, &h, &c, 3);
            if (!rgb) continue;
            std::unique_lock<std::mutex> l(mu);
            cv.wait(l, [&] { return quit || q.size() < 4; });
            if (quit) { stbi_image_free(rgb); return; }
            q.push_back({idx, rgb});
        }
    }
} *P = nullptr;

bool released() {
    bool r = IsKeyReleased(KEY_SPACE) || IsKeyReleased(KEY_ESCAPE) || IsKeyReleased(KEY_ENTER);
    for (int p = 0; p < 4; p++) r |= IsGamepadButtonReleased(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonReleased(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    return r;
}
bool pressedNow() {
    bool r = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER);
    for (int p = 0; p < 4; p++) r |= IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    return r;
}
}  // namespace

bool Subs::load(const char *name) {
    lines.clear(), hold = 5000, next = shownAt = 0, shown.clear();
    char *txt = FileExists(path("subs", "txt")) ? LoadFileText(path("subs", "txt")) : nullptr;
    if (!txt) return false;
    for (char *l = strtok(txt, "\n"); l; l = strtok(nullptr, "\n")) {
        char *a = strchr(l, '\t'), *b = a ? strchr(a + 1, '\t') : nullptr;
        if (!b || strncmp(l, name, a - l) || name[a - l]) continue;
        *b = 0;
        if (!strcmp(a + 1, "hold")) hold = atoi(b + 1);
        else lines.push_back({atoi(a + 1), b + 1});
    }
    UnloadFileText(txt);
    return !lines.empty();
}

const std::string &Subs::at(int ms) {
    if (next < (int)lines.size() && lines[next].ms <= ms) shown = lines[next++].key, shownAt = ms;
    else if (!shown.empty() && ms - shownAt >= hold) shown.clear();
    return shown;
}

bool has(const char *name) { return FileExists(path(name, "mjpg")); }

bool probe(const char *name, int &w, int &h, int &fps, int &frames) {
    FILE *f = fopen(path(name, "mjpg"), "rb");
    Header hd;
    if (!f) return false;
    if (!readHeader(f, hd)) return fclose(f), false;
    frames = 0;
    bool ok = false;
    for (unsigned n; (n = frameLen(f)); frames++) {
        if (frames == 0) {
            std::vector<unsigned char> b(n);
            int c, iw, ih;
            ok = fread(b.data(), 1, n, f) == n;
            if (ok) if (unsigned char *p = stbi_load_from_memory(b.data(), n, &iw, &ih, &c, 3)) ok = iw == hd.w && ih == hd.h, stbi_image_free(p); else ok = false;
        } else fseek(f, n, SEEK_CUR);
    }
    fclose(f);
    w = hd.w, h = hd.h, fps = hd.fps;
    return ok;
}

void start(const char *name) {
    stop();
    FILE *f = fopen(path(name, "mjpg"), "rb");
    Header hd;
    if (!f || !readHeader(f, hd)) return (void)(f && fclose(f), TraceLog(LOG_WARNING, "MOVIE: %s unreadable", name));
    P = new Player;
    P->name = name, P->hd = hd, P->f = f;
    Image blank = GenImageColor(hd.w, hd.h, BLACK);
    ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    P->tex = LoadTextureFromImage(blank);
    UnloadImage(blank);
    P->subs.load(name);
    if (FileExists(path(name, "ogg"))) {
        P->snd = LoadMusicStream(path(name, "ogg"));
        P->snd.looping = false;
        P->sound = IsMusicValid(P->snd);
        if (P->sound) PlayMusicStream(P->snd);
    }
    Audio::musicLevel(0.5f);
    P->t0 = GetTime();
    P->th = std::thread([] { P->run(); });
    TraceLog(LOG_INFO, "MOVIE: %s %dx%d %d fps", name, hd.w, hd.h, hd.fps);
}

bool update(Rectangle dst, float rot) {
    if (!P) return false;
    Player &m = *P;
    int ms = (int)((GetTime() - m.t0) * 1000);
    int target = (int)((long long)ms * m.hd.fps / 1000);
    m.target = target;
    if (m.sound) UpdateMusicStream(m.snd);
    {
        std::lock_guard<std::mutex> l(m.mu);
        while (!m.q.empty() && m.q.front().idx <= target) {
            Frame fr = m.q.front();
            m.q.pop_front();
            if (m.q.empty() || m.q.front().idx > target) UpdateTexture(m.tex, fr.rgb), m.shownIdx = fr.idx;
            stbi_image_free(fr.rgb);
        }
    }
    m.cv.notify_one();
    m.line = m.subs.at(ms).empty() ? "" : Ui::tr(m.subs.shown.c_str(), "");
    if (pressedNow()) m.armed = true;  // QuitMovie.Pressed arms, .Released skips (MoviePlayerService 0x60a3c1, 0x60a3fb)
    bool done = (m.armed && released()) || (m.eof && target >= m.total);
    bool full = dst.width <= 0;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (full) {
        float h = sh, w = h * m.hd.w / m.hd.h;
        if (w > sw) w = sw, h = w * m.hd.h / m.hd.w;
        dst = {(sw - w) / 2, (sh - h) / 2, w, h};
        DrawRectangle(0, 0, sw, sh, BLACK);
    }
    DrawTexturePro(m.tex, {0, 0, (float)m.hd.w, (float)m.hd.h}, {dst.x + dst.width / 2, dst.y + dst.height / 2, dst.width, dst.height},
                   {dst.width / 2, dst.height / 2}, rot * RAD2DEG, WHITE);
    if (full && !m.line.empty()) {
        float size = sh * 0.04f;
        while (size > 8 && Ui::textWidth(m.line.c_str(), size) > sw * 0.9f) size--;
        Ui::text(m.line.c_str(), sw / 2 + 2, sh * 0.88f + 2, size, BLACK, 1);
        Ui::text(m.line.c_str(), sw / 2, sh * 0.88f, size, WHITE, 1);
    }
    if (done) stop();
    return !done;
}

const char *subtitle() { return P ? P->line.c_str() : ""; }

void stop() {
    if (!P) return;
    {
        std::lock_guard<std::mutex> l(P->mu);
        P->quit = true;
    }
    P->cv.notify_all();
    P->th.join();
    for (Frame &fr : P->q) stbi_image_free(fr.rgb);
    if (P->sound) StopMusicStream(P->snd), UnloadMusicStream(P->snd);
    UnloadTexture(P->tex);
    fclose(P->f);
    delete P;
    P = nullptr;
    Audio::musicLevel(1);
}
}  // namespace Movie

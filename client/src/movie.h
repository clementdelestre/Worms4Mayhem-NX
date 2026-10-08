#pragma once
#include "raylib.h"
#include <string>
#include <vector>

// W4M full-screen movies: assets/movies/<name>.mjpg + .ogg + subs.txt (tools/w4m-import, tools/w4m-re/fmv.py; docs/audio.md "Movies").
namespace Movie {
bool has(const char *name);   // name = file stem, e.g. "Meet_The_Professor"
void start(const char *name); // frames decode on a worker; the ogg plays; the frontend music drops to half
// Once per frame inside BeginDrawing. dst zero: the whole screen, picture 4:3 with black bars, subtitle drawn; else the picture in
// dst (centre-rotated by rot rad), subtitle left to the caller (subtitle()). False once the movie ended or QuitMovie skipped it.
bool update(Rectangle dst = {0, 0, 0, 0}, float rot = 0);
const char *subtitle();       // current line (W4M Text.MovieText), "" when none
void stop();

// FMVSubTiles of one movie: lines shown from their TimeOffset, hidden `hold` ms after being shown (MoviePlayerService 0x60a0d0)
struct Subs {
    struct Line { int ms; std::string key; };
    std::vector<Line> lines;
    int hold = 5000, next = 0, shownAt = 0;
    std::string shown;
    bool load(const char *name);
    const std::string &at(int ms);  // advances one line per call, like the per-frame update; empty when hidden
};
bool probe(const char *name, int &w, int &h, int &fps, int &frames);  // header + frame count; decodes frame 0 (false: unreadable)
}

// Movies (docs/audio.md "Movies"): MJPG header and frame count, FMVSubTiles timing, a short hidden playback. Needs the imported assets/movies.
#include "../src/audio.h"
#include "../src/movie.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    const char *name = "Meet_The_Professor";
    if (!Movie::has(name)) return printf("movie_check: no assets/movies/%s.mjpg, skipped\n", name), 0;
    int w, h, fps, frames;
    assert(Movie::probe(name, w, h, fps, frames));
    assert(w == 640 && h == 480 && fps == 25);
    assert(frames > 106 * 25 && frames < 107 * 25);  // 106.12 s

    // FMVSubTiles FMV_MeetTheProf: Line1 at 11400 ms, Line20 at 17000, 5000 ms hold
    Movie::Subs s;
    assert(s.load(name) && s.lines.size() == 20 && s.hold == 5000);
    assert(s.at(0).empty() && s.at(11399).empty());
    assert(s.at(11400) == "FETXT.FMV1.Line1" && s.at(16399) == "FETXT.FMV1.Line1");
    assert(s.at(16400).empty());
    assert(s.at(17000) == "FETXT.FMV1.Line20" && s.at(18400) == "FETXT.FMV1.Line2");
    Movie::Subs w2;
    assert(w2.load("Welcome") && w2.hold == 8000);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 360, "movie_check");
    // W4M MoviePlayerPC::Open 0x7be8c8: Sfx >= 0.1 silences femusic and FrontendDay for the movie, Sfx < 0.1 mutes the movie and keeps the music
    Audio::volumes(1, 0.5f);
    const float day = Audio::dayGain();
    assert(fabsf(day - 0.5f * powf(10, -3.0f / 20)) < 1e-6f && Audio::themeGain() > 0);
    assert(Audio::fmvOpen() && Audio::themeGain() == 0 && Audio::dayGain() == 0);
    Audio::fmvClose();
    assert(Audio::dayGain() == day);
    Audio::volumes(0.05f, 1);
    assert(!Audio::fmvOpen() && Audio::dayGain() > 0);
    Audio::volumes(1, 1);
    Movie::start(name);
    assert(Audio::themeGain() == 0 && Audio::dayGain() == 0);
    int n = 0;
    for (double end = GetTime() + 1.5; GetTime() < end; n++) {
        BeginDrawing();
        assert(Movie::update());
        EndDrawing();
    }
    assert(n > 5);
    Movie::stop();
    assert(Audio::dayGain() > 0 && !Movie::update());
    CloseWindow();
    puts("movie_check ok");
}

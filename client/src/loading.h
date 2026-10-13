#pragma once
#include "sim.h"
#include <string>
#include <vector>

// W4M's pre-match sequence: logo intro over the sea (smoke ring), then the loading screen with a random tip.
namespace Loading {
void begin(const GameConfig &c, bool preStart = true);  // preStart false: a restart (FCS state 8) opens on the loading stage
bool frame(float dt, float progress);  // inside Begin/EndDrawing; true once loaded (progress 1) and the minimum is over
void beginMenu();                      // GotoFrontEnd's Generic loading screen (no match to load)
bool shut();                           // TransitionFadeDown (Circle_In over the frame): true once closed; call each frame until then
bool closing();
bool opening();                        // the title's opening iris is still running
bool ready();                          // the loading screen is up: blocking load steps may run between frames
void overlay(float dt);                // last thing drawn each frame: the match fade-in from black, the closing iris
// one boot frame (inside Begin/EndDrawing): the startup icon; true once loaded
bool boot(bool loaded);
void pinCore(int core);                // Switch: moves the calling worker thread to that CPU core (1 or 2)
// "dir/name" of the files ending in ext (case-insensitive; "/": the subdirectories instead), readdir order, any thread
std::vector<std::string> list(const std::string &dir, const char *ext, bool recurse = false);
}  // namespace Loading

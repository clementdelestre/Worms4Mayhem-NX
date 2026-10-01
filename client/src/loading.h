#pragma once
#include "sim.h"

// W4M's pre-match sequence: logo intro over the sea (smoke ring), then the loading screen with a random tip.
namespace Loading {
void begin(const GameConfig &c);
bool frame(float dt, float progress);  // inside Begin/EndDrawing; true once loaded (progress 1) and faded out
bool ready();                          // the loading screen is up: blocking load steps may run between frames
void overlay(float dt);                // over the first match frames: fade in from black
}  // namespace Loading

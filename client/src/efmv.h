#pragma once
#include "raylib.h"
#include "sim.h"

// Level movies on screen (docs/missions.md "Movies"): the sim plays them (script.cpp); here their camera and letterbox bars.
namespace Efmv {
bool camera(const Game &g, Camera3D &cam);  // the movie camera this frame; false: none, the game's camera
void borders(const Game &g);                // EfmvBorderEntity's bars, over the 3D view
void event(Game &g, const GameEvent &e, float clock);  // AnimateDetail: the coded details' clip from now (render clock, s)
}  // namespace Efmv

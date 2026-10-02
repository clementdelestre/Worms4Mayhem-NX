#pragma once
#include "models.h"
#include "sim.h"
#include <vector>

// Render-only worm acting: W4M's WORMACTING.XOM scenes (assets/acting.txt, tools/w4m-re/acting.py), cast and played like
// WXSceneManagerService (docs/worm-reactions.md). Picks come from a hash of seed, worm and tick, never from the sim rng.
namespace Acting {
void event(const Game &g, const GameEvent &e);
// busy[i]: worm i walks, flies or plays a sim-driven clip: its gesture waits, the scene clock runs on (W4M)
void update(const Game &g, float dt, const std::vector<uint8_t> &busy, const Camera3D &cam);
// Worm i's scene gesture (one-shot, *loop false) or emote (looped) as the body clip, null: none; ly: its face, head and arm layers
const char *clip(const Game &g, int i, float clock, float *t, bool *loop, Models::Layers *ly);
Color tint(int i, Color c);  // W4M Sick.Colour / Abducted.Colour
}  // namespace Acting

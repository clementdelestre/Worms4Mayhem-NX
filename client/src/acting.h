#pragma once
#include "sim.h"
#include <vector>

// Render-only worm acting, after W4M's Data/Tweak/WORMACTING.XOM scenes (docs/worm-reactions.md): bystanders cower at a
// lit fuse, flinch when aimed at, laugh at a miss... Picks come from a hash of seed, worm and tick, never from the sim rng.
namespace Acting {
void event(const Game &g, const GameEvent &e);
// busy[i]: worm i walks, flies or plays a sim-driven clip; scene time waits meanwhile
void update(const Game &g, float dt, const std::vector<uint8_t> &busy);
// The gesture (one-shot, *loop false) or emote (looped) worm i shows now, null: none
const char *clip(const Game &g, int i, float clock, float *t, bool *loop);
}  // namespace Acting

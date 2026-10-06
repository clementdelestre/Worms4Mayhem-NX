#pragma once
#include "audio.h"
#include "models.h"
#include "sim.h"
#include <string>
#include <vector>

// Render-only worm acting: W4M's WORMACTING.XOM scenes (assets/acting.txt, tools/w4m-re/acting.py), cast and played like
// WXSceneManagerService (docs/worm-reactions.md). Picks come from a hash of seed, worm and tick, never from the sim rng.
namespace Acting {
void event(const Game &g, const GameEvent &e);
void movie(const Game &g, const GameEvent &e);  // a level movie's MovieStart / Movie / MovieEnd events (script.h MovieView)
// busy[i]: worm i walks, flies or plays a sim-driven clip: its gestures fade out (WormPoseManager +0x194), their clips run on
void update(const Game &g, float dt, const std::vector<uint8_t> &busy, const Camera3D &cam);
// Worm i's emote (looped) as the body clip, null: none; ly: its face, head, arm and eye layers and its weighted scene gestures
const char *clip(const Game &g, int i, float clock, float *t, bool *loop, Models::Layers *ly);
// W4M WAE_* Input.TauntPressed: the Acting.Trigger TauntMelee / Ranged / Strike for the weapon (table 0x95f1a8, filled by 0x596830)
void taunt(const Game &g, int worm, const std::string &weapon);
void speak(const Game &g, int worm, Audio::Voice v);  // the worm says a line of its team's bank, its mouth following the line's LIP rows
void headMode(int i, float deg);  // the drawn clip's Blend.Rotate.y (Models::blend z): 0 head and eyes follow, 1 eyes only, 2 neither
Color tint(int i, Color c);  // W4M Sick.Colour / Abducted.Colour
}  // namespace Acting

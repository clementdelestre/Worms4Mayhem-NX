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
// The shot's damage as GameLogic.ApplyDamage (0x5b22a0) counts it for worm cur's turn: hp lost since the turn start, less damage types 5 / 6 (0x5ab8a8)
struct TurnDamage { int enemy = 0, friendly = 0; bool dies = false, hurtSelf = false; };
TurnDamage turnDamage(const Game &g, int cur);
void movie(const Game &g, const GameEvent &e);  // a level movie's MovieStart / Movie / MovieEnd events (script.h MovieView)
// busy[i]: worm i walks, flies or plays a sim-driven clip: its gestures fade out (WormPoseManager +0x194), their clips run on
void update(const Game &g, float dt, const std::vector<uint8_t> &busy, const Camera3D &cam);
// Worm i's face (emote), head, arm and eye layers and its weighted scene gestures, over the body clip
void layers(const Game &g, int i, float clock, Models::Layers *ly);
// W4M WAE_* Input.TauntPressed: the Acting.Trigger TauntMelee / Ranged / Strike for the weapon (table 0x95f1a8, filled by 0x596830)
void taunt(const Game &g, int worm, const std::string &weapon);
void speak(const Game &g, int worm, Audio::Voice v);  // the worm says a line of its team's bank, its mouth following the line's LIP rows
void headMode(int i, float deg);  // the drawn clip's Blend.Rotate.y (Models::blend z): 0 head and eyes follow, 1 eyes only, 2 neither
float lightShade(int i);  // the worm light's diffuse / specular factor (LightingService +0x1c, 0x47bc70): 1 in the open
Vector3 lightMul(int i);  // W4M Sick.Colour / Abducted.Colour: the worm light colour scale (0x5a1bb1), channels above 1 allowed
}  // namespace Acting

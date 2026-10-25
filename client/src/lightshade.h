#pragma once
#include "sim.h"

// W4M LightingService entity lights (0x47cbf0, docs/w4m/render.md): each entity with a light has an entry whose shadow fade moves
// toward a land ray's target; the diffuse and specular of the entity's lighting are scaled by it.
namespace LightShade {
enum Kind : uint32_t { Worm, Object, Grave, Shot, Factory, Bubble, Ufo, Girder, Bomber, Detail, Telepad };
inline uint32_t key(Kind k, int i) { return k << 24 | (uint32_t)i; }
struct Spot { uint32_t key; Vector3 pos; bool pool; };  // a render-only entity's light
void step(const Game &g, const std::vector<Spot> &extra = {});  // the service update: a per-frame task (0x47cbf0 returns 0), before the draws
float k(uint32_t key);  // the entry's diffuse / specular factor; 1 for an entity without a light
}  // namespace LightShade

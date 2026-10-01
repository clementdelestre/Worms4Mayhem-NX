#pragma once
#include "raylib.h"

// W4M-style lighting shared by terrain, decor and models (CG/Landscape.cg, CG/FixedFunction.cg).
namespace Lit {
struct Light {
    Vector3 dir = {0, 0.6f, -0.52f};  // towards the sun, world space
    Vector3 ambient = {0.52f, 0.62f, 0.85f}, diffuse = {0.45f, 0.56f, 0.6f}, specular = {0.35f, 0.33f, 0.2f};
};
extern Light sun;  // set by Terrain::load from the map's "light"
Shader shader(const char *vs, const char *fs);  // GLSL 100 source, macro-wrapped for 330; refreshed by frame()
// Textured, alpha-tested, two-sided lambert + rim. worm: W4M's fixed worm light, else the map light.
Shader modelShader(bool worm);
void frame(Vector3 cam);  // push sun + camera to every Lit shader
}

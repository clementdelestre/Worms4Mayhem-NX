#pragma once
#include "raylib.h"
#include <functional>
#include <vector>

// W4M-style lighting shared by terrain, decor and models (CG/Landscape.cg, CG/FixedFunction.cg).
namespace Lit {
struct Light {
    Vector3 dir = {0, 0.6f, -0.52f};  // towards the sun, world space
    Vector3 ambient = {0.52f, 0.62f, 0.85f}, diffuse = {0.45f, 0.56f, 0.6f}, specular = {0.35f, 0.33f, 0.2f};
    // CG/water.cg inputs from the same WaterPlaneTweaks: NearOpacity, TextureScale, SpecularFadeScale, ReflectionStrength,
    // ReflectionContrast, SpecularContrast, SpecularPower, NormalIntensity xyz, NormalIntensity1 xyz, SubtractColourScale
    float water[14] = {0.6f, 45, 1, 0.1f, 2, 6, 40, 6, 6, 1, 0.2f, 0.2f, 1, 0.2f};  // Water.CAMELOT.DAY
};
extern Light sun;  // set by Terrain::load from the map's "light"
constexpr Vector3 LOW_LIGHT = {0, 0.26f, -0.78f};  // Sun.LightVector = Sun.LowLightVector, every theme (0x4e7ed0): land colour and shadow map
extern int aniso;  // anisotropic filtering of land and decor textures (1 = off)
extern const char *MVS;  // the model vertex shader: other passes reuse it so their depths match
// GLSL 100 source, macro-wrapped for 330; lit: refreshed by frame(); es3: GLSL ES 3.00 on GLES (sampler2DShadow, SHADOW_SAMPLER)
Shader shader(const char *vs, const char *fs, bool lit = true, bool es3 = false);
// Textured, alpha-tested, two-sided lambert + rim. worm: W4M's fixed worm light, else the map light.
Shader modelShader(bool worm);
void frame(Vector3 cam);  // push sun + camera to every Lit shader
void profile(float frameTime);  // once per frame, before BeginDrawing: picks the Switch framebuffer size
// W4M lighting pass (docs/maps.md "Rendering"): a 1024² depth map seen along -LOW_LIGHT, fitted to the land box. statics() is
// redrawn when `ver` changes (a new land box: all of it, else only the texels of `stale`, the world box rebuilt since), casters()
// every frame on top of it, listing the spheres (centre, radius) of what it drew.
void shadowPass(BoundingBox land, unsigned ver, BoundingBox stale, const std::function<void()> &statics, const std::function<void(std::vector<Vector4> &)> &casters);
Texture2D shadowMap();  // the sampled depth texture (made on first use)
extern bool shadows;  // false: no shadow map (land shader without sampler2DShadow, or incomplete depth FBO)
}

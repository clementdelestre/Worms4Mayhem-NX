#pragma once
#include "raylib.h"
#include "sim.h"
#include <string>

// Client-side visuals (never read by the sim): billboard particles, projectile trails, sky dome, water.
// Art: assets/ui/fe/wxp_*.png and assets/ui/sky/<theme letter>_{sky0n,water0na/b/c}.png (n = 1/2/3 day/evening/night) from tools/w4m-ui.
namespace Fx {
constexpr float FOG_NEAR = 90, FOG_FAR = 220;  // metres from the camera: W4M shows almost no fog at short range
extern float shake;  // camera shake amplitude (m), decays in update()
void load();
void theme(const std::string &theme, Color sky, const std::string &time);  // per match: sky ramp, water textures, fog colour
void unload();
void clear();
void event(const GameEvent &e, Color dirt);
void trail(const Projectile &s, float dt);  // call once per frame per live projectile
void update(float dt);
Color fog();  // horizon colour: clear colour and terrain/water fog
void drawSky(const Camera3D &cam);  // first thing inside BeginMode3D
void drawWater(const Camera3D &cam, float level, float time);
void draw(const Camera3D &cam);  // particles, after the opaque scene and the water
int count();
}  // namespace Fx

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
void fireworks(Vector3 centre, float radius, float top);  // W4M Land.Center, Land.Radius, Land.MaxHeight for the victory show
void trail(const Projectile &s, float dt);  // call once per frame per live projectile
void puff(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, bool fire = false);  // one ambient particle (rises, slows down)
void wingTrail(Vector3 p);  // one puff of W4M WXP_PlaneWingTrails (Bomber.EffectName)
void ariel(Vector3 p);  // W4M WXP_AirstrikeArielA: the air strike bomb's one-off puff at its launch
void dud(Vector3 p);  // mine fizzled: W4M ExpiryFx WXP_Wep_MineDudEffect
void flame(Vector3 p, Vector3 v, float life, float size0, float size1, bool jet);
void sprite(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, float grav, bool bubble);  // upright W4M "?" (WXSprite22) or bubble (WXSprite23)  // additive W4M jetfire (jet) or toonfire sprite
void ufo(Vector3 at, Vector3 nozzle, Vector3 gate, Vector3 ground, float e, float g, int stage, float u, float dt);  // Alien Abduction: e s into the sequence, g into the warp gate, u into the stage
void update(float dt);
Color fog();  // horizon colour: clear colour and terrain/water fog
void drawSky(const Camera3D &cam);  // first thing inside BeginMode3D
void drawWater(const Camera3D &cam, float level, float time);
void draw(const Camera3D &cam);  // particles, after the opaque scene and the water
int count();
}  // namespace Fx

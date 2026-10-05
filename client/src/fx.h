#pragma once
#include "raylib.h"
#include "sim.h"
#include <string>

// Client-side visuals (never read by the sim): billboard particles, projectile trails, sky dome, water.
// Art: assets/ui/fe/wxp_*.png and assets/ui/sky/<theme letter>_{sky0n,water0na/b/c}.png (n = 1/2/3 day/evening/night) from tools/w4m-ui.
namespace Fx {
extern float shake;  // camera shake amplitude (m), decays in update()
void load();
// per match: level sky scene (levelSky; else the ramp), water textures, clear colour
void theme(const std::string &theme, Color sky, const std::string &time, bool levelSky = true);
std::string skyFile(const std::string &theme, const std::string &time);  // the level sky .glb (Models::decode() it ahead)
void unload();
void clear();
void event(const GameEvent &e, Color dirt);
void fireworks(Vector3 centre, float radius, float top);  // victory show (W4M GameOverLogicEntity: 4 s wait, 5 s) over Land.Center, Radius, MaxHeight
void trail(const Projectile &s, float dt, Vector3 wind);  // call once per frame per live projectile; wind: (cos, 0, sin) x Wind.Speed / Wind.MaxSpeed
void puff(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, bool fire = false);  // one ambient particle (rises, slows down)
void wingTrail(Vector3 p);  // one puff of W4M WXP_PlaneWingTrails (Bomber.EffectName)
void donkeyAriel(Vector3 at);  // W4M WXP_CrateSpawnLARGE rings where the Concrete Donkey appears
void ariel(Vector3 p);  // W4M WXP_AirstrikeArielA: the air strike bomb's one-off puff at its launch
void dud(Vector3 p);  // mine fizzled: W4M ExpiryFx WXP_Wep_MineDudEffect
void soap(Vector3 p);  // one W4M WXP_Bubbles_Small soap bubble, from the Bubble Trouble machine
void flame(Vector3 p, Vector3 v, float life, float size0, float size1, bool jet);
void sprite(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, float grav, bool bubble);  // upright W4M "?" (WXSprite22) or bubble (WXSprite23)  // additive W4M jetfire (jet) or toonfire sprite
void ufo(Vector3 at, Vector3 nozzle, Vector3 gate, Vector3 ground, float e, float g, int stage, float dt);  // Alien Abduction: e s into the sequence (its beam cues), g into the warp gate; stage: Game::ABD_*
void update(float dt);
Color fog();  // horizon colour: clear colour and terrain/water fog
void drawSky(const Camera3D &cam);  // first thing inside BeginMode3D
void drawWater(const Camera3D &cam, float level, float time);
void draw(const Camera3D &cam);  // particles, after the opaque scene and the water
int count();
}  // namespace Fx

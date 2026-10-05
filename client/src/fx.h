#pragma once
#include "raylib.h"
#include "sim.h"
#include <functional>
#include <string>

// Client-side visuals (never read by the sim): billboard particles, projectile trails, sky dome, water.
// Art: assets/ui/fe/wxp_*.png and assets/ui/sky/<theme letter>_{sky0n,water0na/b/c}.png (n = 1/2/3 day/evening/night) from tools/w4m-ui.
namespace Fx {
extern float shake;  // camera shake amplitude (m), decays in update()
void load();
// per match: level sky scene (levelSky; else the ramp), water textures, clear colour
void theme(const std::string &theme, Color sky, const std::string &time, bool levelSky = true);
std::string gradientFile(const std::string &theme, const std::string &time, bool side);  // W4M LightGradient <L>_Sky0n / <L>_SideSky0n
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
void jetStart(Vector3 at);  // W4M WAE_Jetpack PackAccessory.Trigger 0x58ce57: WXP_JetpackStartRing + WXP_JetPackStartBase, worm + 7 units
void jetStop();  // its StartBase emitter is killed once the thrust stops (0x58cc94); the particles live on
void flame(Vector3 p, Vector3 v, float life, float size0, float size1, bool jet);
void sprite(Vector3 p, Vector3 v, float life, float size0, float size1, Color c, float grav, bool bubble);  // upright W4M "?" (WXSprite22) or bubble (WXSprite23)  // additive W4M jetfire (jet) or toonfire sprite
void ufo(Vector3 at, Vector3 nozzle, Vector3 gate, Vector3 ground, float e, float g, int stage, float dt);  // Alien Abduction: e s into the sequence (its beam cues), g into the warp gate; stage: Game::ABD_*
void update(float dt);
Color fog();  // horizon colour: clear colour and terrain/water fog
void drawSky(const Camera3D &cam, Vector3 origin, float unit);  // first thing inside BeginMode3D; origin: W4M world origin, unit: m per W4M unit
void drawWater(const Camera3D &cam, float level, float time, float half = 12000 / 20.f, Vector2 centre = {40, 40});  // half: W4M's 12000-unit quad in m, at the W4M origin
void draw(const Camera3D &cam);  // particles, after the opaque scene and the water
// lens flare, last in the scene; hit(from, dir): 0 clear, 1 land, 2 object between the camera and the sun
void drawFlare(const Camera3D &cam, float dt, const std::function<int(Vector3, Vector3)> &hit);
int count();
void setWind(Vector3 w);  // (cos, 0, sin) x Wind.Speed / Wind.MaxSpeed, for ParticleIsEffectedByWind
// match start: the map's EMITTER_ details (unit: m per W4M unit, the import scale / 20), the weather odds of theme / time, the W4M origin
// (weather emitter), the water height and a downward land ray for the rain splashes
void level(const std::vector<Terrain::Emitter> &em, float unit, Vector3 origin, const std::string &theme, const std::string &time, float water,
           std::function<bool(Vector3, float, Vector3 *, Vector3 *)> down, float rainProb = -1);  // rainProb >= 0: a mission's override
void levelSync(const std::vector<Terrain::Emitter> &em);  // each frame: emitters whose detail a blast removed stop
void weather(uint32_t seed, int ticks);  // each frame: the 60 s rain roll (ticks: sim ticks at 60 Hz)
}  // namespace Fx

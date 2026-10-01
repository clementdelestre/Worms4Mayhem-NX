#pragma once
#include "raylib.h"
#include "sim.h"

// Client-side controls: sticks, gyro and mouse become the sim's per-tick rate Input (network and replays unchanged),
// plus the match camera and HD rumble.
namespace Controls {
struct Settings {
    float aim = 1, cam = 1, gyro = 1;  // sensitivities
    bool invertAim = false, invertCam = false, gyroOn = false, rumbleOn = true;
};
extern Settings settings;
void load(const char *path);
void save(const char *path);
// Once per frame: buttons now, axis rates kept for tick(). live: a human on this pad plays the current turn.
Input read(const Game &g, int pad, bool live, float dt);
Input tick(Input in);                    // per sim tick: in's axes from read()'s rates
int8_t diffuse(float rate, float &carry);  // rate in int8 units; the rounding error carries to the next tick
bool aiming();                           // aim mode: ZL / L held, charging, sniper
bool firstPerson(const Game &g);         // aim mode seen from the worm's eyes (target-marker weapons stay third person)
float sinceFirstPerson();                 // seconds since the first-person aim view, 0 in it
extern int forceAim;                     // capture mode: 1 aim, 2 fine aim
Vector3 aimPoint(const Game &g);         // far point of the active worm's shot line, centred by the aim camera
// Free orbit while moving, over the shoulder in aim mode, chasing a shot, through the sniper scope.
void reset();  // new match: camera cut behind the first worm, aim state cleared
void camera(Camera3D &cam, const Game &g, bool chase, bool scope, bool input, float dt);
void focus(const Vector3 *at);  // HUD cinematic target (hp count, crate drop) until focus(nullptr)
void rumble(int pad, float amp, float secs);  // pad -1: nobody
void update(float dt);                         // rumble envelopes, once per frame
}  // namespace Controls

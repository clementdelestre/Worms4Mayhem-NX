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
bool aiming();                           // aim mode: ZL / L held, charging
bool targetHeld(const Game &g);          // a targeted() weapon in hand, Aim phase: Fire waits for the Blimp view
bool targetView(const Game &g);          // W4M Blimp view (toggled with E / d-pad right): the camera drives the aim
bool fireRefused();                      // this frame's Fire press was dropped: outside the Blimp view
extern bool cpuTurn;                     // set by main: a CPU plays the turn
bool firstPerson(const Game &g);         // aim mode seen from the worm's eyes (target-marker weapons stay third person)
float sinceFirstPerson();                 // seconds since the first-person aim view, 0 in it
extern int forceAim;                     // capture mode: 1 aim, 2 fine aim
Vector3 eye(const Game &g);              // first-person aim camera position
Vector3 aimPoint(const Game &g);         // far point of the active worm's shot line, centred by the aim camera
// Free orbit while moving, over the shoulder in aim mode, chasing a shot, through the sniper scope.
void reset();  // new match: camera cut behind the first worm, aim state cleared
void camera(Camera3D &cam, const Game &g, bool chase, bool scope, bool input, float dt);
void impact(Vector3 at);  // explosion: the camera holds on it ~1 s once no shot is left to chase
float occluded();  // 0..1: the active worm is behind terrain or the camera is right on it (W4M shows it as an outline)
void focus(const Vector3 *at, float radius = 0, bool crate = false);  // HUD cinematic target (hp count, crate drop), radius m kept in view; held while set each frame, nullptr or no call: released after 0.2 s
// crate: W4M CrateTrackCamera on it instead of the framing
void rumble(int pad, float amp, float secs);  // pad -1: nobody
void update(float dt);                         // rumble envelopes, once per frame
}  // namespace Controls

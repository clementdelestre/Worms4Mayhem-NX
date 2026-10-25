#pragma once
#include "raylib.h"
#include <cstdint>

// W4M worm animation arithmetic kept apart from the renderer so the checks can drive it (docs/worm-reactions.md "Worm pose state").
// Times are in 20 ms frames (graphic +0x198) unless named seconds.
namespace WormPose {

// 0x569f20(&v, to, a, rate, dt): v moves to (a v + dt to) / (dt + a), the step capped at rate x dt
float approach(float v, float to, float a, float rate, float dt);

// Anim state 6, sliding (kWE 17 setup 0x5a3f1d, update 0x5a2d70): the SkidArms clip at (t, w) with random targets
struct Skid {
    bool on = false;
    float t = 0, w = 0, to = 0, retarget = 0, rate = 0, delay = 0;  // +0x1cc, +0x1d0, +0x1d4, +0x1d8, +0x1dc, +0x1e0
    uint32_t rng = 1;
    void start(bool fromGround, uint32_t seed);
    // One update over clip length len s; true on the frame the look delay ends: the worm looks at its target and drops its gesture
    bool step(float dt, float len);
};
// The WORM%dTARGET actor (0x5a2d70): feet + 20 units x the horizontal support normal + 2000 ms of the velocity along the support + 9 units up
Vector3 skidTarget(Vector3 feet, Vector3 normal, Vector3 vel);

// Anim state 0 / 7, ground handler 0x5a2a00: the Walk clip at (phase, weight), speed = |Velocity.xz| in units/ms
struct Walk {
    float phase = 0, w = 0;  // +0x19c (clip seconds, wraps past 0.5), +0x1a0
    void step(float dt, float speed);
};

// WeaponAccessoryEntity pose state shared by the WAE_* classes (0x58c060 handler, 0x58f620 pose update)
struct Wae {
    enum Press { Ignored, Equip, Taunt };
    int state = 0;  // +0xb0: 0 holstered, 1 drawn, 2 taunting
    bool fired = false;  // +0xc5: PlayFireAnim ran since the last Accessory.Init
    float draw = 0, taunt = 0;  // clocks +0xdc (s since Accessory.Init), +0xf0
    void init() { state = 1, fired = false, draw = 0; }  // Accessory.Init 0x58c28e
    void holster() { state = 0; }  // Weapon.ActivateAccessory, Worm.CleanUpOnDeactivate 0x58c4d3
    void playFire() { fired = true, state = state == 2 ? 1 : state; }  // Weapon.PlayFireAnim 0x58c0b8
    // Input.TauntPressed 0x58c333: needs +0xc5 == 0 and kWPS_Ambulatory; holstered it redraws (Equip), drawn with a taunt clip it starts it
    Press press(bool ambulatory, bool hasClip, float drawLen);
    void step(float dt, float len);  // dt, len in seconds; the taunt ends past len (0x58fb1f)
    float weight(float len) const;  // the Taunt clip's weight in state 2: min(1, 4 (len - t), 4 t); Aim and Hold take the rest
};

}  // namespace WormPose

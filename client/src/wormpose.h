#pragma once
#include "raylib.h"
#include "raymath.h"
#include <cmath>
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

// Ground probes 0x5a07d0: the TailAngle / TipAngle clips follow the land behind the worm, their weights ease with the probes
struct Tail {
    float tail = 0, tip = 0, phase = 0, dphase = 0;  // weights +0x70, +0x74; angles +0x78, +0x7c in quarter turns
    bool far0 = false, far1 = false, buried = false;  // +0x80, +0x81: no land within reach; buried: land under the first probe (both clips at weight 0)
    static inline uint8_t ring = 0;  // 0x95fb68: the stationary worms probe in turn, worm index == ring
    // One update of dt frames at the worm's feet (metres), facing yaw. force: walking (or the first update, with instant); ray(origin, units):
    // distance down to land in W4M units, units + 1 when none (0x952cf8)
    template <class Ray> void update(float dt, int idx, bool force, bool instant, Vector3 feet, float yaw, Ray ray) {
        constexpr float U = 1 / 20.f, Q = 1.5708f;  // metres per W4M unit
        const Vector3 f = {sinf(yaw), 0, cosf(yaw)};
        buried = false;
        if (force || ring == idx) {
            ring = (ring - 1) & 15;
            float d = ray({feet.x - f.x * 6 * U, feet.y + 7 * U, feet.z - f.z * 6 * U}, 14);
            if (d < 1) return void((tail = tip = 0, far0 = far1 = false, buried = true));  // 0x952cfc == 0: the hit step truncates to 0
            if (d > 14) far0 = true;
            else {
                far0 = false;
                float to = atanf((d - 7) / 6) / Q;
                phase = instant ? to : WormPose::approach(phase, to, 3, 0.1f, dt);
            }
            float back = -(cosf(phase) * 6 + 5), e = ray({feet.x + f.x * back * U, feet.y + (22 - d) * U, feet.z + f.z * back * U}, 30);
            if (e > 30) far1 = true;
            else far1 = false, dphase = atanf((e - 15) / 5) / Q - phase;
        }
        tip = far1 ? WormPose::approach(tip, 1, 3, 0.1f, dt) : WormPose::approach(tip, 0, 1, 0.2f, dt);
        tail = far0 ? WormPose::approach(tail, 1, 7, 0.1f, dt) : WormPose::approach(tail, 0, 1, 0.2f, dt);
    }
    float tailTime() const { return tail * 0.5f + (1 - tail) * (1 - phase); }  // seconds, weight 1 (0 when buried)
    float tipTime() const { return tip * 0.5f + (1 - tip) * (1 - dphase); }
};

// Model yaw 0x5a2710 (the same call before and after the state handler): +0x150 follows the facing +0x14c at most 0.08 rad a frame, within
// 0.2 pi of it; +0x154 follows +0x150 at 0.1, within pi / 2
struct Yaw {
    float to = 0, a = 0, b = 0;  // +0x14c, +0x150, +0x154
    void set(float yaw) { to = a = b = yaw; }
    void step(float dt, bool follow, float yaw);
};

// State 2 one-shot at its end 0x5a01a0: AT_FB / AT_LR scrubbed by the stick against the facing, their weight easing to 1
struct Aftertouch {
    float fb = 0.5f, lr = 0.5f, w = 0;  // +0x84, +0x88 (phase of the clip), +0x1b0
    // ended: the one-shot has played to its end; input = InputImpulse x 20 (the stick, world space)
    void step(float dt, bool ended, Vector3 input, float yaw);
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

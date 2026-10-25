#include "wormpose.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace WormPose {

float approach(float v, float to, float a, float rate, float dt) {
    float n = (a * v + dt * to) / (dt + a), cap = dt * rate;
    return cap < n - v ? v + cap : cap < v - n ? v - cap : n;
}

static float rand01(uint32_t &s) {  // xorshift32: render-only, never the sim rng
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return (s >> 8) / 16777216.f;
}

void Skid::start(bool fromGround, uint32_t seed) {
    on = true, t = w = retarget = 0, rate = 0.02f, delay = fromGround ? 30 : 1, rng = seed | 1;
}

bool Skid::step(float dt, float len) {
    if ((retarget -= dt) <= 0) to = rand01(rng) * len, retarget = rand01(rng) * 50 + 10;
    bool look = false;
    if (delay > 0) look = (delay -= dt) <= 0;
    else w += Clamp((1 - w) / 2, -0.2f, 0.2f);  // 0x47a1a0(&w, 1, 1, 0.2)
    t += rate;
    if (t > len) t -= len;
    if (t < 0) t += len;
    // the arm clip's rate speeds up toward the target past half the distance and slows near it, as 0x5a2f0b compares
    float d = to - t, half = t * 0.5f;
    rate += (d > 0 ? (half <= d ? -0.002f : 0.002f) : (t - to < half ? -0.002f : 0.002f));
    rate = Clamp(rate, -0.006f, 0.006f);
    return look;
}

Vector3 skidTarget(Vector3 feet, Vector3 n, Vector3 v) {
    float k = Vector3DotProduct(n, v);
    Vector3 along = Vector3Subtract(v, Vector3Scale(n, k)), h = {n.x, 0, n.z};
    if (n.x != 0 || n.z != 0) h = Vector3Normalize(h);
    return Vector3Add(Vector3Add(feet, Vector3Scale(h, 20 / 20.f)), Vector3Add(Vector3Scale(along, 2), {0, 9 / 20.f, 0}));
}

Wae::Press Wae::press(bool ambulatory, bool hasClip, float drawLen) {
    if (fired || !ambulatory) return Ignored;
    if (state == 0) return init(), Equip;
    if (state != 1 || !hasClip) return Ignored;
    state = 2, taunt = 0, draw = std::max(draw, drawLen);  // state 2 holds the Draw clock at its length (0x58f8a3)
    return Taunt;
}

void Wae::step(float dt, float len) {
    draw += dt;
    if (state == 2 && (taunt += dt) > len) state = 1;
}

float Wae::weight(float len) const { return std::min({1.f, 4 * (len - taunt), 4 * taunt}); }

}  // namespace WormPose

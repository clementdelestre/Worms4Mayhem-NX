#include "controls.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Controls {
Settings settings;

// Tuning. Sticks: radial dead zones, share of |x|^2 in the response curve.
static const float DEAD = 0.12f, OUTER = 0.95f, CURVE = 0.7f;
// Aim (rad/s at full tilt), ZL precision factor, full-tilt acceleration (delay, ramp time, top multiplier).
static const float AIM_YAW = 1.6f, AIM_PITCH = 1.0f, AIM_TURN = 1.2f, FINE = 0.33f, RAMP_DELAY = 0.3f, RAMP_TIME = 0.4f, RAMP_MAX = 1.6f;
// Move mode: turn gain toward the stick direction, stick share below which the worm only turns, max error still walking.
static const float TURN_GAIN = 8, STEP = 0.25f, FACE = 0.9f;
// Camera: orbit speeds (rad/s), idle seconds before it swings back behind the worm, default elevation.
static const float AIM_FOCUS = 40;  // aim camera looks at this far point of the shot line (screen centre)
static const float CAM_YAW = 2.8f, CAM_PITCH = 1.4f, RECENTER_AFTER = 2.5f, EL0 = 0.42f;
// Gyro: noise floor (rad/s), axis signs (check on hardware), mouse rad per pixel.
static const float GYRO_FLOOR = 0.03f, GYRO_YAW = 1, GYRO_PITCH = 1, MOUSE = 0.004f;
static const float SIM_TURN = 2.5f, SIM_AIM = 1.5f;  // sim rad/s at +-127 (Game::step)

#ifdef __SWITCH__
static const float UP = 1;  // libnx HID sticks report +y for up, GLFW reports -y
#else
static const float UP = -1;
#endif

static float rate[3], carry[3];  // turn, walk, aim: int8 units per tick
static float tilt = 0;           // seconds the aim stick has been at full tilt
static bool aimMode = false, fine = false;
int forceAim = 0;
static int padUsed = 0;
static float camYaw = 0, camEl = EL0, zoom = 1, idle = 0;
static int lastWorm = -1;
static bool snap = false;  // new worm: swing behind it
static float fpOut = 9;     // seconds since the first-person aim view

void load(const char *path) {
    Settings &s = settings;
    int ia = 0, ic = 0, g = 1, r = 1;
    if (char *t = FileExists(path) ? LoadFileText(path) : nullptr) {
        if (sscanf(t, "%f %f %f %d %d %d %d", &s.aim, &s.cam, &s.gyro, &ia, &ic, &g, &r) == 7)
            s.invertAim = ia, s.invertCam = ic, s.gyroOn = g, s.rumbleOn = r;
        UnloadFileText(t);
    }
}

void save(const char *path) {
    const Settings &s = settings;
    SaveFileText(path, (char *)TextFormat("%.2f %.2f %.2f %d %d %d %d\n", s.aim, s.cam, s.gyro, s.invertAim, s.invertCam, s.gyroOn, s.rumbleOn));
}

static bool down(int pad, int b) { return IsGamepadButtonDown(pad, b); }

// ax: GAMEPAD_AXIS_LEFT_X or RIGHT_X; +y up, magnitude through the dead zones and curve
static Vector2 stick(int pad, int ax) {
    Vector2 v = {GetGamepadAxisMovement(pad, ax), UP * GetGamepadAxisMovement(pad, ax + 1)};
    float m = Vector2Length(v);
    if (m < DEAD) return {0, 0};
    float t = fminf((m - DEAD) / (OUTER - DEAD), 1);
    return Vector2Scale(v, Lerp(t, t * t, CURVE) / m);
}

#ifdef __SWITCH__
struct Pad { u32 style = 0; int nsix = 0, nvib = 0; HidSixAxisSensorHandle six[2]; HidVibrationDeviceHandle vib[2]; };
static Pad pads[4];

// raylib's pad 0 reads No1 and Handheld (padInitializeDefault), pad i reads No(i+1)
static Pad &sync(int pad) {
    HidNpadIdType id = pad ? HidNpadIdType(HidNpadIdType_No1 + pad) : hidGetNpadStyleSet(HidNpadIdType_No1) ? HidNpadIdType_No1 : HidNpadIdType_Handheld;
    u32 set = hidGetNpadStyleSet(id);
    u32 tag = id == HidNpadIdType_Handheld ? set & HidNpadStyleTag_NpadHandheld
            : set & HidNpadStyleTag_NpadFullKey ? HidNpadStyleTag_NpadFullKey : set & HidNpadStyleTag_NpadJoyDual ? HidNpadStyleTag_NpadJoyDual
            : set & HidNpadStyleTag_NpadJoyLeft ? HidNpadStyleTag_NpadJoyLeft : set & HidNpadStyleTag_NpadJoyRight ? HidNpadStyleTag_NpadJoyRight : 0;
    Pad &p = pads[pad];
    if (tag == p.style) return p;
    p = Pad{};  // ponytail: the old controller's sensor is never stopped; harmless, the system drops it on disconnect
    p.style = tag;
    if (!tag) return p;
    HidNpadStyleTag st = HidNpadStyleTag(tag);
    int n = tag == HidNpadStyleTag_NpadJoyDual ? 2 : 1;
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(p.six, n, id, st))) {
        p.nsix = n;
        for (int i = 0; i < n; i++) hidStartSixAxisSensor(p.six[i]);
    }
    bool single = tag == HidNpadStyleTag_NpadJoyLeft || tag == HidNpadStyleTag_NpadJoyRight;
    for (int k = single ? 1 : 2; k >= 1 && !p.nvib; k--)
        if (R_SUCCEEDED(hidInitializeVibrationDevices(p.vib, k, id, st))) p.nvib = k;
    return p;
}

// aim yaw, pitch rates (rad/s): yaw about gravity whatever the grip, pitch about the controller's left-right axis
static Vector2 gyro(int pad) {
    Pad &p = sync(pad);
    HidSixAxisSensorState s;
    if (!p.nsix || !hidGetSixAxisSensorStates(p.six[p.nsix - 1], &s, 1)) return {0, 0};  // dual: right Joy-Con
    Vector3 w = Vector3Scale({s.angular_velocity.x, s.angular_velocity.y, s.angular_velocity.z}, 2 * PI);  // revolutions/s
    Vector3 a = {s.acceleration.x, s.acceleration.y, s.acceleration.z};
    float al = Vector3Length(a);
    float yaw = al > 0.5f ? Vector3DotProduct(w, a) / al : w.z;
    float pitch = p.style == HidNpadStyleTag_NpadJoyLeft ? -w.y : p.style == HidNpadStyleTag_NpadJoyRight ? w.y : w.x;  // sideways Joy-Cons
    auto gate = [](float v) { return fabsf(v) < GYRO_FLOOR ? 0 : v; };
    return {GYRO_YAW * gate(yaw), GYRO_PITCH * gate(pitch)};
}
#endif

Input read(const Game &g, int pad, bool live, float dt) {
    padUsed = pad;
    const Worm &w = g.worms[g.current];
#ifdef __SWITCH__
    const bool kb = false;  // raylib-nx mirrors the pad onto keys and mouse buttons: ignore them
#else
    const bool kb = true;
#endif
    bool lHeld = down(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1), zl = down(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_2) || (kb && IsMouseButtonDown(MOUSE_BUTTON_RIGHT));
    bool canAim = live && g.phase == Phase::Aim && w.alive && !g.roped && !g.jetting;
    aimMode = canAim && (forceAim || zl || lHeld || g.power > 0 || WEAPONS[g.weapon].name == "Sniper Rifle");
    fine = aimMode && (zl || forceAim == 2);
    Vector2 ls = stick(pad, GAMEPAD_AXIS_LEFT_X), rs = stick(pad, GAMEPAD_AXIS_RIGHT_X);
    float turn = 0, walk = 0, aim = 0, inv = settings.invertAim ? -1 : 1;  // rad/s, walk share, rad/s
    if (aimMode) {
        Vector2 a = rs;
        if (lHeld && !rs.x && !rs.y) a = ls, ls = {0, 0};  // L + stick: single Joy-Con
        tilt = Vector2Length(a) > 0.98f ? tilt + dt : 0;
        float k = settings.aim * (fine ? FINE : 1) * Lerp(1, RAMP_MAX, Clamp((tilt - RAMP_DELAY) / RAMP_TIME, 0, 1));
        turn = -a.x * AIM_YAW * k - ls.x * AIM_TURN;
        aim = a.y * AIM_PITCH * k * inv;
        walk = ls.y;
#ifdef __SWITCH__
        if (settings.gyroOn) {
            Vector2 gy = Vector2Scale(gyro(pad), settings.gyro);
            turn += gy.x, aim += gy.y;
        }
#endif
    } else if (g.phase != Phase::Aim && g.phase != Phase::Retreat) {  // steering a shot (super sheep): direct
        turn = -ls.x * SIM_TURN, walk = ls.y, aim = rs.y * SIM_AIM * inv;
    } else {
        float m = Vector2Length(ls);
        if (m > 0) {  // camera-relative: turn toward the stick direction, walk once roughly facing it
            float err = remainderf(camYaw + atan2f(-ls.x, ls.y) - w.yaw, 2 * PI);
            turn = Clamp(err * TURN_GAIN, -SIM_TURN, SIM_TURN);
            walk = m > STEP && fabsf(err) < FACE ? m * cosf(err) : 0;
        }
        if (g.roped) aim = rs.y * SIM_AIM * inv;  // reel in / out
    }
    if (kb) {  // arrows turn and walk, W S aim, right mouse button held: mouse aim
        turn += (IsKeyDown(KEY_LEFT) - IsKeyDown(KEY_RIGHT)) * SIM_TURN;
        walk += IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN);
        aim += (IsKeyDown(KEY_W) - IsKeyDown(KEY_S)) * SIM_AIM * (fine ? FINE : 1);
        if (aimMode && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && dt > 0) {
            Vector2 d = GetMouseDelta();
            turn -= d.x * MOUSE * settings.aim / dt, aim -= d.y * MOUSE * settings.aim * inv / dt;
        }
    }
    rate[0] = Clamp(turn / SIM_TURN, -1, 1) * 127, rate[1] = Clamp(walk, -1, 1) * 127, rate[2] = Clamp(aim / SIM_AIM, -1, 1) * 127;
    Input in;
    if (down(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || down(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) || (kb && IsKeyDown(KEY_SPACE))) in.buttons |= Input::FIRE;
    if (down(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || (kb && IsKeyDown(KEY_ENTER))) in.buttons |= Input::JUMP;
    bool r = down(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) && !lHeld;  // L+R: perf overlay
    if (r || down(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || (kb && IsKeyDown(KEY_TAB))) in.buttons |= Input::NEXT_WEAPON;
    return in;
}

int8_t diffuse(float rate, float &carry) {
    if (!rate) return carry = 0, 0;
    float v = rate + carry, q = Clamp(roundf(v), -127, 127);
    carry = Clamp(v - q, -1, 1);
    return (int8_t)q;
}

Input tick(Input in) {
    in.turn = diffuse(rate[0], carry[0]), in.walk = diffuse(rate[1], carry[1]), in.aim = diffuse(rate[2], carry[2]);
    return in;
}

bool aiming() { return aimMode; }

bool firstPerson(const Game &g) {
    Kind k = WEAPONS[g.weapon].kind;
    return aimMode && k != Kind::Airstrike && k != Kind::Donkey && k != Kind::Teleport && k != Kind::Abduction;
}

static Vector3 eye(const Worm &w) { return {w.pos.x - sinf(w.yaw) * 0.15f, w.pos.y + 0.5f, w.pos.z - cosf(w.yaw) * 0.15f}; }

void camera(Camera3D &cam, const Game &g, bool chase, bool scope, bool input, float dt) {
    const Worm &cur = g.worms[g.current];
    int pad = padUsed;
    Vector2 rs = input && !aimMode ? stick(pad, GAMEPAD_AXIS_RIGHT_X) : Vector2{0, 0};
    if (g.roped || g.phase == Phase::Flying) rs.y = 0;  // reels the rope, steers the shot
    float zin = input * (down(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) - down(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)), wheel = 0;
#ifndef __SWITCH__
    if (input) rs.x += IsKeyDown(KEY_D) - IsKeyDown(KEY_A), zin += IsKeyDown(KEY_X) - IsKeyDown(KEY_Z), wheel = GetMouseWheelMove();
#endif
    bool moving = input && Vector2Length(stick(pad, GAMEPAD_AXIS_LEFT_X)) > 0;
    if (g.current != lastWorm) lastWorm = g.current, snap = true;
    if (rs.x || rs.y) snap = false;
    idle = rs.x || rs.y || moving ? 0 : idle + dt;
    camYaw -= rs.x * CAM_YAW * settings.cam * dt;
    camEl = Clamp(camEl - rs.y * (settings.invertCam ? -1 : 1) * CAM_PITCH * settings.cam * dt, 0.05f, 1.2f);  // stick up: look up
    float follow = scope || aimMode ? 12 : snap ? 4 : chase ? (idle > 0.5f ? 2.5f : 0) : idle > RECENTER_AFTER ? 1.2f : 0;
    float k = 1 - expf(-dt * follow), err = remainderf(cur.yaw - camYaw, 2 * PI);
    camYaw += err * k, camEl += (EL0 - camEl) * k;
    if (fabsf(err) < 0.05f) snap = false;
    zoom = Clamp(zoom * expf(-zin * dt * 1.5f - wheel * 0.1f), 0.45f, 2.5f);

    if (firstPerson(g) || scope) {  // W4M aim view: first person from the worm's eyes, looking down the shot line
        Vector3 e = eye(cur), f = Vector3Add(e, Vector3Scale(g.aimDir(cur), AIM_FOCUS));
        float k = 1 - expf(-dt * 16);
        cam.position = Vector3Lerp(cam.position, e, k), cam.target = Vector3Lerp(cam.target, f, k);
        cam.fovy = Lerp(cam.fovy, scope ? 25.0f : fine ? 42.0f : 60.0f, 1 - expf(-dt * 8));
        fpOut = 0;
        return;
    }
    Vector3 focus = cur.pos, from = focus, want;
    if (chase) from = focus = Vector3Add(g.shots[0].pos, Vector3Scale(g.shots[0].vel, 0.1f));  // lead the shot a little
    float kt = 1 - expf(-dt * 6), kp = 1 - expf(-dt * (chase ? 2.5f : 3));
    {
        float back = (chase ? 17.5f : 9.85f) * zoom;
        want = Vector3Add(focus, {-sinf(camYaw) * cosf(camEl) * back, sinf(camEl) * back, -cosf(camYaw) * cosf(camEl) * back});
    }
    if ((fpOut += dt) < 0.6f && !chase) kt = kp = 1 - expf(-dt * 8);  // back out of first person quickly
    Vector3 hit, to = Vector3Subtract(want, from);
    if (chase) want.y = fmaxf(want.y, cur.pos.y + 4);  // donkey/airstrike dig below the surface: stay above ground
    else if (g.terrain.raycast({from, Vector3Normalize(to)}, Vector3Length(to), &hit)) want = Vector3Lerp(from, hit, 0.85f);  // into a hill
    cam.target = Vector3Lerp(cam.target, focus, kt);
    cam.position = Vector3Lerp(cam.position, want, kp);
    cam.fovy = Lerp(cam.fovy, 50.0f, 1 - expf(-dt * 8));
}

Vector3 aimPoint(const Game &g) { const Worm &w = g.worms[g.current]; return Vector3Add(eye(w), Vector3Scale(g.aimDir(w), AIM_FOCUS)); }

static float amp[4], left[4];
static bool quiet[4] = {true, true, true, true};

void rumble(int pad, float a, float secs) {
    if (!settings.rumbleOn || pad < 0 || pad > 3 || a <= 0) return;
    if (a >= amp[pad] || left[pad] <= 0) amp[pad] = fminf(a, 1), left[pad] = secs;
}

void update(float dt) {
    for (int p = 0; p < 4; p++) {
        bool on = left[p] > 0;
        left[p] -= dt;
        if (!on && quiet[p]) continue;
        quiet[p] = !on;
        if (!on) amp[p] = 0;
#ifdef __SWITCH__
        Pad &d = sync(p);
        HidVibrationValue v[2];
        for (HidVibrationValue &x : v) x = {amp[p], 160, amp[p] * 0.7f, 320};
        if (d.nvib) hidSendVibrationValues(d.vib, v, d.nvib);
#endif
    }
}
}  // namespace Controls

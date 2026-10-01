#include "controls.h"
#include "raymath.h"
#include <cmath>
#include <cstdio>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Controls {
Settings settings;

// Tuning. Sticks: radial dead zones (Joy-Con sticks are small and noisy), share of |x|^2 in the response curve.
static const float DEAD = 0.12f, OUTER = 0.95f, JC_DEAD = 0.18f, JC_OUTER = 0.88f, CURVE = 0.7f;
// Aim (rad/s at full tilt), extra |x| share for small-tilt precision, ZL factor, full-tilt acceleration (delay, ramp time, top multiplier).
static const float AIM_YAW = 1.1f, AIM_PITCH = 0.7f, AIM_TURN = 1.2f, AIM_CURVE = 0.5f, FINE = 0.25f, RAMP_DELAY = 0.45f, RAMP_TIME = 0.8f, RAMP_MAX = 1.5f;
// Move mode: turn gain toward the stick direction, stick share below which the worm only turns, max error still walking.
static const float TURN_GAIN = 8, STEP = 0.25f, FACE = 0.9f, BACK = 2.4f;  // BACK: error past which the worm about-faces
// Camera: orbit speeds (rad/s), idle seconds before it swings back behind the worm, default elevation.
static const float AIM_FOCUS = 40;  // aim camera looks at this far point of the shot line (screen centre)
static const float CAM_YAW = 2.8f, CAM_PITCH = 1.4f, RECENTER_AFTER = 2.5f, EL0 = 0.42f;
// Gyro: dead band (rad/s, hand tremor), axis signs (check on hardware), mouse rad per pixel.
static const float GYRO_FLOOR = 0.06f, GYRO_YAW = 1, GYRO_PITCH = 1, MOUSE = 0.004f;
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
static bool cut = true;     // place the camera at once (new match, in and out of first person)
static bool focusOn = false;
static Vector3 blast{};  // dropped dynamite: the camera flies to it near the end of its fuse, holds on the blast
static float hold = 0;
static Vector3 focusAt{};
static float focusR = 0;
static float reach = 1, lift = 0;  // distance kept clear of a hill (fraction), rise over scenery on the way (m)
static float overT = 0, orbitA = 0, occl = 0;

float occluded() { return occl; }

void impact(Vector3 at) { blast = at, hold = 1; }
static float focusLeft = 0;  // a target dropped for a frame or two (between two counts, turn start) is kept: no lurch
void focus(const Vector3 *at, float radius) { if (at) focusOn = true, focusAt = *at, focusR = radius, focusLeft = 0.2f; }

void reset() { cut = true, focusOn = false, lastWorm = -1, fpOut = 9, tilt = 0, carry[0] = carry[1] = carry[2] = 0; }

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
static bool joyCon(int pad);

// ax: GAMEPAD_AXIS_LEFT_X or RIGHT_X; +y up, magnitude through the dead zones and curve
static Vector2 stick(int pad, int ax) {
    Vector2 v = {GetGamepadAxisMovement(pad, ax), UP * GetGamepadAxisMovement(pad, ax + 1)};
    bool jc = joyCon(pad);
    float m = Vector2Length(v), dead = jc ? JC_DEAD : DEAD, outer = jc ? JC_OUTER : OUTER;
    if (m < dead) return {0, 0};
    float t = fminf((m - dead) / (outer - dead), 1);
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

static bool joyCon(int pad) { return sync(pad).style != HidNpadStyleTag_NpadFullKey; }  // handheld too: Joy-Con sticks

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
    auto gate = [](float v) { return copysignf(fmaxf(fabsf(v) - GYRO_FLOOR, 0), v); };  // soft: no step at the floor
    return {GYRO_YAW * gate(yaw), GYRO_PITCH * gate(pitch)};
}
#else
static bool joyCon(int) { return false; }
#endif

static bool fuseKeys(const Game &g) { return g.phase == Phase::Aim && WEAPONS[g.weapon].userFuse; }

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
    aimMode = canAim && (forceAim || zl || lHeld || g.power > 0);
    fine = aimMode && (zl || forceAim == 2);
    Vector2 ls = stick(pad, GAMEPAD_AXIS_LEFT_X), rs = stick(pad, GAMEPAD_AXIS_RIGHT_X);
    float turn = 0, walk = 0, aim = 0, inv = settings.invertAim ? -1 : 1;  // rad/s, walk share, rad/s
    bool back = false;
    if (aimMode) {
        Vector2 a = rs;
        if (lHeld && !rs.x && !rs.y) a = ls, ls = {0, 0};  // L + stick: single Joy-Con
        a = Vector2Scale(a, Lerp(1, Vector2Length(a), AIM_CURVE));
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
            back = m > STEP && fabsf(err) > BACK;
        }
        if (g.roped || g.jetting) aim = rs.y * SIM_AIM * inv;  // reel in / out, or aim the weapon in hand
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
    if (back) in.buttons |= Input::ABOUT_FACE;
    if (down(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || down(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) || (kb && IsKeyDown(KEY_SPACE))) in.buttons |= Input::FIRE;
    if (down(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || (kb && IsKeyDown(KEY_ENTER))) in.buttons |= Input::JUMP;
    bool r = down(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) && !lHeld;  // L+R: perf overlay
    if (r || down(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || (kb && IsKeyDown(KEY_TAB))) in.buttons |= Input::NEXT_WEAPON;
    if (fuseKeys(g)) {  // W4M FuseUp on the d-pad (camera zoom otherwise)
        if (down(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) || (kb && IsKeyDown(KEY_EQUAL))) in.buttons |= Input::FUSE_UP;
        if (down(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (kb && IsKeyDown(KEY_MINUS))) in.buttons |= Input::FUSE_DOWN;
    }
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
float sinceFirstPerson() { return fpOut; }

bool firstPerson(const Game &g) {
    Kind k = WEAPONS[g.weapon].kind;
    // ground-targeted weapons aim from above; melee is third person like W4M (the punch/swing is the worm itself)
    return aimMode && k != Kind::Airstrike && k != Kind::Donkey && k != Kind::Teleport && k != Kind::Abduction && k != Kind::Melee;
}

// On the sim's shot line (rays and launches start at pos + dir * t): the screen centre is where the shot goes.
Vector3 eye(const Game &g) { const Worm &w = g.worms[g.current]; return Vector3Subtract(w.pos, Vector3Scale(g.aimDir(w), 0.5f)); }

// W4M TrackCam (docs/camera-w4m.md 2): shells, grenades, holy (PayloadTrackCamera) and Fatkins, cut to the first clear
// ViewPoint (m: units / 20) around the launch point; until 1 s of flight an on-screen shot stays with the worm camera
static const Vector3 PAYLOAD_VP[] = {{-5, 10, 15}, {5, 10, 15}, {1.25f, 2.5f, 2.5f}, {-1.25f, 2.5f, 2.5f}, {3.75f, 5, 3.75f}, {-3.75f, 5, 3.75f},
                                     {2.5f, 3.75f, -2.5f}, {-2.5f, 3.75f, -2.5f}, {2.5f, -2.5f, 3.75f}, {-2.5f, -2.5f, 3.75f}, {0, 1.5f, 0}};
static const Vector3 FATKINS_VP[] = {{10, 5, 25}, {-10, 5, 25}, {10, 5, -25}, {-10, 5, -25}};
static struct { bool on, cutDone; int idx; float flight, cutAgo, rest; Vector3 e, d, obj; } tk;

static bool onScreen(const Camera3D &cam, Vector3 p) {
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), to = Vector3Subtract(p, cam.position);
    return Vector3DotProduct(f, to) > 0.8f * Vector3Length(to);
}

static bool track(Camera3D &cam, const Game &g, bool &chase, float dt) {
    const Projectile *s = chase && !g.shots.empty() ? &g.shots[0] : nullptr;
    const WeaponDef *wd = s ? &WEAPONS[s->weapon] : nullptr;
    bool fat = wd && wd->name == "Fatkins Strike";  // FatkinsTrackCamera
    if (wd && wd->kind == Kind::Airstrike && wd->fuse <= 0) return hold = 0, chase = false, false;  // no camera: the current one stays
    if (wd && wd->kind == Kind::Donkey && !fat) {  // DonkeyCamera: fixed on the side (+z 500 units), look-at donkey + 100 units at 0.1/frame
        if (!tk.on) {
            Vector3 c = Vector3Add(s->pos, {0, 0, 25}), hit, a;
            for (int k = 0; k < 30 && (g.terrain.solid(c) || g.terrain.raycast({c, Vector3Normalize(a = Vector3Subtract(s->pos, c))}, Vector3Length(a), &hit)); k++) c.y += 1;
            cam.position = c, cam.target = s->pos, tk.on = tk.cutDone = true;
        }
        tk.obj = Vector3Add(s->pos, {0, 5, 0}), tk.rest = 1.5f;
        cam.target = Vector3Lerp(cam.target, tk.obj, 1 - expf(-dt * 6.3f));
        return true;
    }
    if (!wd || (wd->kind != Kind::Shell && !fat)) {  // gone: RestTime 1500 ms frozen on its last point
        if (tk.on && g.phase != Phase::Aim && (tk.rest -= dt) > 0) return cam.target = Vector3Lerp(cam.target, tk.obj, 1 - expf(-dt * 6.3f)), true;
        tk = {};
        return false;
    }
    if (tk.flight == 0) {
        Vector3 v = {s->vel.x, 0, s->vel.z};
        tk.e = s->pos, tk.d = Vector3Length(v) > 0.1f ? Vector3Normalize(v) : Vector3{sinf(g.worms[g.current].yaw), 0, cosf(g.worms[g.current].yaw)};
    }
    tk.flight += dt, tk.obj = s->pos;
    if (!tk.on && tk.flight < 1 && onScreen(cam, tk.obj)) return chase = false, false;  // ShoulderCamera meanwhile
    tk.on = true, tk.rest = 1.5f, tk.cutAgo += dt;
    float far = fat ? 50 : 65, pref = fat ? 25 : 30;  // Camera2ObjectDistance, MinPreferredDistance
    Vector3 hit, to = Vector3Subtract(tk.obj, cam.position);
    bool lost = !onScreen(cam, tk.obj) || Vector3Length(to) >= far || g.terrain.raycast({cam.position, Vector3Normalize(to)}, Vector3Length(to) - 0.5f, &hit);
    auto side = [&](Vector3 p) { return tk.d.z * (p.x - tk.e.x) - tk.d.x * (p.z - tk.e.z); };  // never crosses the travel line
    const Vector3 *vp = fat ? FATKINS_VP : PAYLOAD_VP;
    int n = fat ? 4 : 11;
    for (int k = 0; k < 2 && (!tk.cutDone || (lost && tk.cutAgo >= 1)); k++, tk.idx = (tk.idx + 1) % n) {
        Vector3 c = Vector3Add(tk.e, {vp[tk.idx].x * tk.d.z + vp[tk.idx].z * tk.d.x, vp[tk.idx].y, -vp[tk.idx].x * tk.d.x + vp[tk.idx].z * tk.d.z});
        Vector3 a = Vector3Subtract(tk.obj, c), b = Vector3Subtract(tk.e, c);
        if (c.y < g.water + 1 || (tk.cutDone && side(c) * side(cam.position) < 0) || g.terrain.raycast({c, Vector3Normalize(a)}, Vector3Length(a) - 0.5f, &hit) ||
            (Vector3Length(b) > 0.5f && g.terrain.raycast({c, Vector3Normalize(b)}, Vector3Length(b) - 0.5f, &hit)))
            continue;
        cam.position = c, cam.target = tk.obj, tk.cutAgo = 0, tk.cutDone = true;  // hard cut
        break;
    }
    if (!tk.cutDone) return false;
    cam.target = Vector3Lerp(cam.target, tk.obj, 1 - expf(-dt * 6.3f));  // LookSpeed 0.1/frame
    if (Vector3Distance(cam.position, tk.obj) < pref)  // ZoomSpeed 0.019/frame
        cam.position = Vector3Lerp(cam.position, Vector3Add(tk.obj, Vector3Scale(Vector3Normalize(Vector3Subtract(cam.position, tk.obj)), pref)), 1 - expf(-dt * 1.15f));
    cam.position.y = fmaxf(cam.position.y, g.water + 1);
    if (g.terrain.solid(cam.position)) cam.position.y += 15 * dt;
    cam.fovy = Lerp(cam.fovy, 50.0f, 1 - expf(-dt * 8));
    return true;
}

void camera(Camera3D &cam, const Game &g, bool chase, bool scope, bool input, float dt) {
    const Worm &cur = g.worms[g.current];
    int pad = padUsed;
    if ((focusLeft -= dt) <= 0) focusOn = false;
    bool fuse = g.phase == Phase::Retreat && !g.shots.empty() && dropped(WEAPONS[g.shots[0].weapon]) && g.shots[0].fuse < 0.7f;
    if (fuse) blast = g.shots[0].pos, hold = 1;
    else hold = g.phase == Phase::Aim ? 0 : hold - dt;
    bool onBlast = fuse || (!chase && hold > 0);  // shots still flying (bomblets) are chased first
    chase = chase || onBlast;
    Kind pk = chase && !onBlast && !g.shots[0].child ? WEAPONS[g.shots[0].weapon].kind : Kind::Shell;
    const Projectile *pet = pk == Kind::Sheep || pk == Kind::SuperSheep || pk == Kind::OldWoman || pk == Kind::Scouser ? &g.shots[0] : nullptr;  // locked behind it
    Vector2 rs = input && !aimMode ? stick(pad, GAMEPAD_AXIS_RIGHT_X) : Vector2{0, 0};
    if (g.roped || g.jetting || g.phase == Phase::Flying) rs.y = 0;  // reels the rope, aims from the jetpack, steers the shot
    float zin = !fuseKeys(g) * input * (down(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) - down(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)), wheel = 0;
#ifndef __SWITCH__
    if (input) rs.x += IsKeyDown(KEY_D) - IsKeyDown(KEY_A), zin += IsKeyDown(KEY_X) - IsKeyDown(KEY_Z), wheel = GetMouseWheelMove();
#endif
    bool moving = input && Vector2Length(stick(pad, GAMEPAD_AXIS_LEFT_X)) > 0;
    if (lastWorm < 0) camYaw = cur.yaw, camEl = EL0, zoom = 1, lastWorm = g.current;  // new match
    if (g.current != lastWorm) lastWorm = g.current, snap = true;
    if (rs.x || rs.y) snap = false;
    idle = rs.x || rs.y || (moving && !pet) ? 0 : idle + dt;  // steering a super sheep keeps the camera behind it
    camYaw -= rs.x * CAM_YAW * settings.cam * dt;
    camEl = Clamp(camEl - rs.y * (settings.invertCam ? -1 : 1) * CAM_PITCH * settings.cam * dt, 0.05f, 1.2f);  // stick up: look up
    float follow = scope || aimMode ? 12 : pet ? (idle > 0.5f ? 4.0f : 0) : snap ? 4 : chase ? (idle > 0.5f ? 2.5f : 0) : idle > RECENTER_AFTER ? 1.2f : 0;
    float k = 1 - expf(-dt * follow), err = remainderf((pet ? atan2f(pet->vel.x, pet->vel.z) : cur.yaw) - camYaw, 2 * PI);
    camYaw += err * k, camEl += (EL0 - camEl) * k;
    if (fabsf(err) < 0.05f) snap = false;
    zoom = Clamp(zoom * expf(-zin * dt * 1.5f - wheel * 0.1f), 0.45f, 2.5f);

    if (!focusOn && (firstPerson(g) || scope)) {  // W4M aim view: first person from the worm's eyes, looking down the shot line
        Vector3 e = eye(g), f = Vector3Add(e, Vector3Scale(g.aimDir(cur), AIM_FOCUS));
        float k = cut || fpOut > 0 ? 1 : 1 - expf(-dt * 16), fov = scope ? 25.0f : fine ? 42.0f : 60.0f;  // cut in: a fly-in crosses the worm's body
        cam.position = Vector3Lerp(cam.position, e, k), cam.target = Vector3Lerp(cam.target, f, k);
        cam.fovy = k == 1 ? fov : Lerp(cam.fovy, fov, 1 - expf(-dt * 8));
        fpOut = 0, cut = false;
        return;
    }
    if (!focusOn && track(cam, g, chase, dt)) { fpOut += dt, cut = false; return; }
    Vector3 focus = focusOn ? focusAt : cur.pos, from = focus, want;
    if (focusOn) chase = false;
    if (chase) from = focus = onBlast ? blast : Vector3Add(g.shots[0].pos, Vector3Scale(g.shots[0].vel, 0.1f));  // lead the shot a little
    overT = g.phase == Phase::GameOver ? overT + dt : 0;
    const Worm *champ = nullptr;  // W4M game over: the winner 4 s (WormTrackCamera), then the orbit
    for (const Worm &x : g.worms) if (overT > 0 && !focusOn && x.alive && x.team == g.winner && (!champ || &x == &cur)) champ = &x;
    if (champ) chase = false, from = focus = champ->pos;
    bool orbit = overT > 4 && !g.cfg.mission;
    if (fpOut == 0 && !chase) cut = true;  // cut out too: backing out passes through the worm's head
    if (fpOut == 0 && chase) cam.target = Vector3Add(cam.position, Vector3Normalize(Vector3Subtract(cam.target, cam.position)));  // far aim point pulled in
    fpOut += dt;
    float kt = cut ? 1 : 1 - expf(-dt * 6), kp = cut ? 1 : 1 - expf(-dt * (pet ? 6 : chase ? 2.5f : 3));
    {
        float back = (chase && !pet ? 17.5f : focusOn ? fmaxf(7.5f, focusR * 3 + 4) : 9.85f) * zoom;  // fov 50: 3 r fits r, labels included
        bool group = (focusOn && focusR > 0) || champ;  // hp count of several worms, winner: from higher, around the scenery in the way
        float el = group ? fmaxf(camEl, 0.75f) : camEl;
        for (float dy : {0.0f, 0.8f, -0.8f, 1.6f, -1.6f, 2.4f, -2.4f, 0.0f}) {
            float y = camYaw + dy;
            want = Vector3Add(focus, {-sinf(y) * cosf(el) * back, sinf(el) * back, -cosf(y) * cosf(el) * back});
            Vector3 d = Vector3Subtract(want, from), h;
            if (!group || !g.terrain.raycast({from, Vector3Normalize(d)}, Vector3Length(d), &h)) break;
        }
    }
    Vector3 hit, to = Vector3Subtract(want, from);
    if (orbit) {  // W4M OrbitCam: Land.Center at max((top + water + 20) / 2, water + 20), radius + 200, height 450 units, 0.3 rad/s
        float top = g.terrain.colTop.empty() ? 20 : g.terrain.colTop.back() * Terrain::VOX, low = g.water + 1, mid = fmaxf((low + top) / 2, low);
        orbitA = overT - dt <= 4 ? atan2f(cam.position.x - Terrain::NX * Terrain::VOX / 2, cam.position.z - Terrain::NZ * Terrain::VOX / 2) : orbitA + dt * 0.3f;
        float R = Terrain::NX * Terrain::VOX / 2 + 10;
        focus = {Terrain::NX * Terrain::VOX / 2, mid, Terrain::NZ * Terrain::VOX / 2};
        want = {focus.x + sinf(orbitA) * R, mid + 22.5f, focus.z + cosf(orbitA) * R};
    } else if (chase && !pet) want.y = fmaxf(want.y, cur.pos.y + 4);  // donkey/airstrike dig below the surface: stay above ground
    else {  // W4M OccludingCam: zooms in at once to the first hill on the worm-camera ray, back out at 0.02/frame
        float r = g.terrain.raycast({from, Vector3Normalize(to)}, Vector3Length(to), &hit) ? fmaxf(0.9f * Vector3Distance(from, hit) - 0.3f, 0.6f) / Vector3Length(to) : 1;
        reach = cut || r < reach ? r : reach + (r - reach) * (1 - expf(-dt * 1.2f));
        want = Vector3Add(from, Vector3Scale(to, reach));
    }
    cam.target = Vector3Lerp(cam.target, focus, kt);
    Vector3 left = Vector3Subtract(want, cam.position);  // scenery on the way: rise over it, slower across
    bool blocked = !cut && g.terrain.raycast({cam.position, Vector3Normalize(left)}, Vector3Length(left), &hit);
    lift = cut ? 0 : blocked ? fminf(lift + 20 * dt, 30) : fmaxf(lift - 10 * dt, 0);
    float kh = blocked ? kp * Clamp((Vector3Distance(cam.position, hit) - 2) / Vector3Length(left), 0, 1) : kp;  // halts 2 m short until risen
    float y = Lerp(cam.position.y, want.y + lift, kp);
    cam.position = {Lerp(cam.position.x, want.x, kh), blocked ? fmaxf(y, cam.position.y) : y, Lerp(cam.position.z, want.z, kh)};
    if (g.terrain.solid(cam.position)) cam.position.y += 15 * dt;  // W4M TrackCam: inside land, up 5 units a frame
    Vector3 back = Vector3Subtract(cam.position, from);  // following (not flying back from afar): never behind the scenery, pulled in front of it
    if (!orbit && !(chase && !pet) && Vector3Length(back) < Vector3Length(to) + 2 && g.terrain.raycast({from, Vector3Normalize(back)}, Vector3Length(back), &hit))
        cam.position = Vector3Add(from, Vector3Scale(Vector3Normalize(back), fmaxf(0.9f * Vector3Distance(from, hit) - 0.3f, 0.6f)));
    Vector3 eyeTo = Vector3Subtract(cur.pos, cam.position);
    float near = Clamp((2.5f - Vector3Length(eyeTo)) / 1.25f, 0, 1);  // W4M WormOpaqueDist 50 / WormTransparencyDist 25 units
    bool hid = Vector3Length(eyeTo) > 0.6f && g.terrain.raycast({cam.position, Vector3Normalize(eyeTo)}, Vector3Length(eyeTo) - 0.5f, &hit);
    occl += (fmaxf(near, hid) - occl) * (1 - expf(-dt * 12));
    cam.fovy = cut ? 50 : Lerp(cam.fovy, 50.0f, 1 - expf(-dt * 8));
    cut = false;
}

Vector3 aimPoint(const Game &g) { const Worm &w = g.worms[g.current]; return Vector3Add(w.pos, Vector3Scale(g.aimDir(w), AIM_FOCUS)); }

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

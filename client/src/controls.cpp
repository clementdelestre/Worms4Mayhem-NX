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
static const float CAM_YAW = 2.8f, CAM_PITCH = 1.4f, RECENTER_AFTER = 2.5f, EL0 = 0.255f;  // ShoulderCamera DefaultHeight
// Gyro: dead band (rad/s, hand tremor), axis signs (check on hardware), mouse rad per pixel.
static const float GYRO_FLOOR = 0.06f, GYRO_YAW = 1, GYRO_PITCH = 1, MOUSE = 0.004f;
static const float SIM_TURN = 2.5f, SIM_AIM = 1.5f;  // sim rad/s at +-127 (Game::step)

#ifdef __SWITCH__
static const float UP = 1;  // libnx HID sticks report +y for up, GLFW reports -y
#else
static const float UP = -1;
#endif

static float rate[4], carry[4];  // turn, walk, aim, Blimp pitch: int8 units per tick
static float tilt = 0;           // seconds the aim stick has been at full tilt
static bool aimMode = false, fine = false;
int forceAim = 0;
static int padUsed = 0;
static float blimpZoom = 1;  // W4M Camera.Blimp zoom 0.15-2: pan speed and field of view
static bool blimpOn = false, blimpLive = false, refusedNow = false, tiltTick = false;
static uint8_t prevFire = 0;
bool cpuTurn = false;
static float camYaw = 0, camEl = EL0, zoom = 1, idle = 0;
static int lastWorm = -1;
static bool snap = false;  // new worm: swing behind it
static float fpOut = 9;     // seconds since the first-person aim view
static bool cut = true;     // place the camera at once (new match, in and out of first person)
static bool focusOn = false, focusCrate = false;
static Vector3 blast{};  // dropped dynamite: the camera flies to it near the end of its fuse, holds on the blast
static float hold = 0, sinceBoom = 9;
static bool wasRoped = false;
static float ropeYaw = 0;
static int ninjaIdx = 0;  // NinjaCamMkIII +0xec: next yaw offset to try, kept until one is clear
static void resetTrack();
static Vector3 focusAt{};
static float focusR = 0;
static float reach = 1, lift = 0;  // distance kept clear of a hill (fraction), rise over scenery on the way (m)
static float overT = 0, orbitA = 0, occl = 0, petYaw = 0, petEl = 0.255f;

float occluded() { return occl; }

void impact(Vector3 at) { blast = at, hold = 1, sinceBoom = 0; }
static float focusLeft = 0;  // a target dropped for a frame or two (between two counts, turn start) is kept: no lurch
void focus(const Vector3 *at, float radius, bool crate) { if (at) focusOn = true, focusAt = *at, focusR = radius, focusLeft = 0.2f, focusCrate = crate; }

void reset() { cut = true, focusOn = false, lastWorm = -1, fpOut = 9, tilt = 0, carry[0] = carry[1] = carry[2] = carry[3] = 0, blimpOn = false, resetTrack(); }

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

static bool blimpable(const Game &g);
static bool girdering(const Game &g) {  // W4M GirderKitLogicEntity input group: the sticks move the preview, not the worm
    return g.phase == Phase::Aim && g.worms[g.current].alive && !g.roped && !g.jetting && WEAPONS[g.weapon].kind == Kind::Girder;
}
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
    bool held = targetHeld(g);
    // W4M Input.BlimpViewPressed toggles Blimp / Default (E). Here also: A / ZR / Space enter it (that press does not fire),
    // B / Enter leave it (no jump), and holding L shows it until released.
    static bool swallowFire = false, swallowJump = false, viaL = false, wasL = false;
    bool fireDown = down(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || down(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) || (kb && IsKeyDown(KEY_SPACE));
    bool jumpDown = down(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || (kb && IsKeyDown(KEY_ENTER));
    swallowFire &= fireDown, swallowJump &= jumpDown;
    if (!blimpable(g)) blimpOn = viaL = false;
    else if (live) {
        if (forceAim) blimpOn = true;
        else if (kb && IsKeyPressed(KEY_E)) blimpOn = !blimpOn, viaL = false;
        else if (!blimpOn && held && fireDown && !(prevFire & Input::FIRE)) blimpOn = swallowFire = true;
        else if (blimpOn && jumpDown && !(prevFire & Input::JUMP)) blimpOn = viaL = false, swallowJump = true;
        if (held && lHeld && !wasL && !blimpOn) blimpOn = viaL = true;
        if (viaL && !lHeld) blimpOn = viaL = false;
    }
    wasL = lHeld, blimpLive = live;
    bool canAim = live && g.phase == Phase::Aim && w.alive && !g.roped && !g.jetting, tv = live && blimpable(g) && blimpOn;
    aimMode = canAim && (forceAim || zl || lHeld || g.power > 0);
    fine = aimMode && (zl || forceAim == 2);
    Vector2 ls = stick(pad, GAMEPAD_AXIS_LEFT_X), rs = stick(pad, GAMEPAD_AXIS_RIGHT_X);
    float turn = 0, walk = 0, aim = 0, inv = settings.invertAim ? -1 : 1;  // rad/s, walk share, rad/s
    bool back = false;
    float pitch = 0;
    bool gk = live && girdering(g);
    if (gk) {  // left stick / arrows step it, right stick / A D turn the GirderCam, up / down (W S) raise / lower
        Vector2 m = ls, c = rs;
        if (kb) m.x += IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT), m.y += IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN), c.x += IsKeyDown(KEY_D) - IsKeyDown(KEY_A), c.y += IsKeyDown(KEY_W) - IsKeyDown(KEY_S);
        walk = Clamp(m.y, -1, 1), aim = Clamp(m.x, -1, 1) * SIM_AIM, turn = -Clamp(c.x, -1, 1) * SIM_TURN, pitch = Clamp(c.y, -1, 1);
    } else if (tv) {  // W4M Blimp 0x52a5e0: left stick / arrows pan at 250 x zoom u/s, right stick / A D W S look (yaw, pitch).
        Vector2 m = ls, l = {-rs.x, rs.y};  // user's choice: W4M HelpBlimpConsole has the sticks the other way round
        if (kb) m.x += IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT), m.y += IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN), l.x += IsKeyDown(KEY_A) - IsKeyDown(KEY_D), l.y += IsKeyDown(KEY_W) - IsKeyDown(KEY_S);
        float pan = blimpZoom * 12.5f / Game::CURSOR_SPEED, s = 0.9f + 0.1f * blimpZoom;
        walk = Clamp(m.y, -1, 1) * pan, aim = Clamp(m.x, -1, 1) * pan * SIM_AIM, turn = Clamp(l.x, -1, 1) * 0.55f * s / Game::BLIMP_TURN * SIM_TURN;
        pitch = Clamp(l.y, -1, 1) * 0.45f * s / Game::BLIMP_TILT;
    } else if (aimMode) {
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
            if (g.jetting) walk = fmaxf(m * cosf(err), 0), back = false;  // W4M 0x562ac2: forward thrust once the stick leans along the facing
        }
        if (g.roped) aim = rs.y * SIM_AIM * inv;  // reel in / out, or aim the weapon in hand
    }
    if (kb && !tv && !gk) {  // arrows turn and walk, W S aim, right mouse button held: mouse aim
        turn += (IsKeyDown(KEY_LEFT) - IsKeyDown(KEY_RIGHT)) * SIM_TURN;
        walk += IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN);
        aim += (IsKeyDown(KEY_W) - IsKeyDown(KEY_S)) * SIM_AIM * (fine ? FINE : 1);
        if (aimMode && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && dt > 0) {
            Vector2 d = GetMouseDelta();
            turn -= d.x * MOUSE * settings.aim / dt, aim -= d.y * MOUSE * settings.aim * inv / dt;
        }
    }
    rate[0] = Clamp(turn / SIM_TURN, -1, 1) * 127, rate[1] = Clamp(walk, -1, 1) * 127, rate[2] = Clamp(aim / SIM_AIM, -1, 1) * 127, rate[3] = pitch * 127;
    Input in;
    if (back) in.buttons |= Input::ABOUT_FACE;
    if (tv || gk) in.buttons |= Input::TARGET;
    if (fireDown) in.buttons |= Input::FIRE;
    static bool wasLocked = false, lockFire = false;  // the press that locked the homing target does not charge it
    if (g.locked && !wasLocked && (in.buttons & Input::FIRE)) lockFire = true;
    wasLocked = g.locked, lockFire &= (in.buttons & Input::FIRE) != 0;
    if (lockFire) in.buttons &= ~Input::FIRE;
    Vector3 hit;
    refusedNow = tv && fireDown && !(prevFire & Input::FIRE) && g.cursorOn && !g.blimpHit(&hit);  // W4M NotClearToFire: no target
    prevFire = (fireDown ? Input::FIRE : 0) | (jumpDown ? Input::JUMP : 0);
    if (swallowFire || (live && held && !tv)) in.buttons &= ~Input::FIRE;  // W4M 0x583a10: no launch outside the Blimp view
    if (g.jetting) { if (zl || (kb && IsKeyDown(KEY_BACKSPACE))) in.buttons |= Input::JUMP; }  // W4M Fire.Second (LT, Backspace): drop; no jump
    else if (jumpDown && !tv && !swallowJump) in.buttons |= Input::JUMP;
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
    in.turn = diffuse(rate[0], carry[0]), in.walk = diffuse(rate[1], carry[1]);
    bool both = rate[2] && rate[3], tilt = (in.buttons & Input::TARGET) && rate[3] && (!rate[2] || (tiltTick = !tiltTick));
    float k = both ? 2 : 1;  // side move and pitch share the aim byte: every other tick, twice the rate
    if (tilt) in.buttons |= Input::PITCH, in.aim = diffuse(Clamp(rate[3] * k, -127, 127), carry[3]);
    else in.aim = diffuse(Clamp(rate[2] * k, -127, 127), carry[2]);
    return in;
}

bool aiming() { return aimMode; }

static bool blimpable(const Game &g) {
    const Worm &w = g.worms[g.current];
    return g.phase == Phase::Aim && w.alive && !g.roped && !g.jetting && targeted(WEAPONS[g.weapon].kind) && !(WEAPONS[g.weapon].kind == Kind::Homing && g.locked);
}  // W4M 0x583a10: a locked homing goes back to the aiming cursor (CreateAimingCursor), aimed and powered as usual
bool targetHeld(const Game &g) { return blimpable(g); }
bool targetView(const Game &g) {  // the CPU aims from the Blimp too (W4M AI), not its homing: it never locks
    return blimpable(g) && (blimpLive ? blimpOn : (cpuTurn && WEAPONS[g.weapon].kind != Kind::Homing) || g.blimp);
}
bool fireRefused() { return refusedNow; }
float sinceFirstPerson() { return fpOut; }

bool firstPerson(const Game &g) {
    Kind k = WEAPONS[g.weapon].kind;
    // ground-targeted weapons aim from above; melee is third person like W4M (the punch/swing is the worm itself)
    return aimMode && k != Kind::Airstrike && k != Kind::Donkey && k != Kind::Teleport && k != Kind::Abduction && k != Kind::Melee;
}

// On the sim's shot line (rays and launches start at pos + dir * t): the screen centre is where the shot goes.
Vector3 eye(const Game &g) { const Worm &w = g.worms[g.current]; return Vector3Subtract(w.pos, Vector3Scale(g.aimDir(w), 0.5f)); }

// W4M TrackCam (docs/camera-w4m.md 2), ViewPoints in m (units / 20) around the event point (the predicted impact or landing).
// Payload: requested when its impact point is off screen or after 1 s of flight; worm and crate: CutWhenStartOffScreen
static const Vector3 PAYLOAD_VP[] = {{-5, 10, 15}, {5, 10, 15}, {1.25f, 2.5f, 2.5f}, {-1.25f, 2.5f, 2.5f}, {3.75f, 5, 3.75f}, {-3.75f, 5, 3.75f},
                                     {2.5f, 3.75f, -2.5f}, {-2.5f, 3.75f, -2.5f}, {2.5f, -2.5f, 3.75f}, {-2.5f, -2.5f, 3.75f}, {0, 1.5f, 0}};
static const Vector3 FATKINS_VP[] = {{10, 5, -25}, {10, 5, 25}, {-10, 5, 25}, {-10, 5, -25}};
static const Vector3 WORM_VP[] = {{3.5f, 4, 10}, {-3.5f, 1.5f, 9}, {-2.5f, 3, 10}, {2.5f, 2.5f, 11.5f}, {1, 3, 15}, {0.5f, 1, 5}, {-0.5f, 1.5f, 5},
                                  {-1.5f, 6, 10}, {0.5f, 1.25f, 4}, {0.5f, 4, -10}, {-1.5f, 3, -9}, {0.5f, 1, -4}, {0, 4, 0}};
static const Vector3 CRATE_VP[] = {{0, 5, 15}, {0, 5, -15}, {15, 5, 0}, {-15, 5, 0}, {0, 25, 2.5f}};
struct TrackDef { const Vector3 *vp; int n; float far, pref, zoom; bool startCut; };  // Camera2ObjectDistance, MinPreferredDistance m, ZoomSpeed /s
static const TrackDef PAYLOAD_T = {PAYLOAD_VP, 11, 65, 30, 1.15f, false}, FATKINS_T = {FATKINS_VP, 4, 50, 25, 1.15f, false},
                      WORM_T = {WORM_VP, 13, 30, 10, 0.09f, true}, CRATE_T = {CRATE_VP, 5, 25, 10, 0.54f, true};
static struct { bool on, cutDone, force, seen, frame; int idx, prio, worm = -1, last = -1; float flight, cutAgo, rest; Vector3 e, d, obj, ov, pv; const TrackDef *def; } tk;
static struct { int prio, worm; Vector3 e, d; float age; } pend;  // the one pending event-camera request (CMS +0x350)
static float sinceTrack = 9, floodT = 99, lastWater = 0;
static int lastDying = -1;
static std::vector<Vector3> lastVel;
static struct { int worm = -1; Vector3 pos; float rest; } abd;  // AlienAbductionCamera
static struct { int n, of; Vector3 a, b, v, look; float rest; } sa;  // SuperAirstrikeCamera: drops seen of `of`, first / last drop, bomber heading
static struct { bool on; float hold, kp; Vector3 end; } fly;      // FlyCam, then its PauseDuration hold

static float perFrame(float f, float dt) { return 1 - powf(1 - f, dt * 60); }  // W4M per-frame lerp factor at our dt

static bool onScreen(const Camera3D &cam, Vector3 p) {
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), to = Vector3Subtract(p, cam.position);
    return Vector3DotProduct(f, to) > 0.8f * Vector3Length(to);
}
static bool seen(const Camera3D &cam, const Game &g, Vector3 p) {
    Vector3 to = Vector3Subtract(p, cam.position), hit;
    return onScreen(cam, p) && !g.terrain.raycast({cam.position, Vector3Normalize(to)}, Vector3Length(to) - 0.5f, &hit);
}

// W4M 0x51cf20: a worm knocked ballistic asks for WormTrackCamera, 3 with no landing in the sweep, 5 at its landing point;
// dropped when it and that point are already in view. Also arms the abduction and flood cameras.
static void watch(const Game &g, const Camera3D &cam, float dt) {
    sinceTrack += dt, sinceBoom += dt, floodT += dt, pend.age += dt;
    if (lastVel.size() != g.worms.size()) lastVel.assign(g.worms.size(), {}), lastWater = g.water;  // new match
    if (g.water > lastWater + 0.5f && WEAPONS[g.weapon].kind == Kind::Flood) floodT = 0;
    lastWater = g.water;
    for (size_t i = 0; i < g.worms.size(); i++) {
        const Worm &w = g.worms[i];
        Vector3 was = lastVel[i];
        lastVel[i] = w.vel;
        if (!w.alive || g.phase == Phase::GameOver || Vector3Length(w.vel) < Vector3Length(was) + 3 || w.vel.y <= 0 || ((int)i == g.current && sinceBoom > 0.2f)) continue;
        if (WEAPONS[g.weapon].kind == Kind::Abduction && !w.vel.x && !w.vel.z) {  // pos (UFO.x, Land.MaxHeight, UFO.z + 200 units)
            float top = g.terrain.colTop.empty() ? 20 : g.terrain.colTop.back() * Terrain::VOX;
            abd = {(int)i, {w.pos.x, fmaxf(top, w.pos.y + 2), w.pos.z + 10}, 1};
            continue;
        }
        float grav = g.gravity();
        Vector3 p = w.pos, v = w.vel;
        int prio = 3;
        for (int k = 0; k < 120 && p.y > g.water; k++) {  // 4 s sweep
            v.y -= grav / 30;
            Vector3 n = Vector3Add(p, Vector3Scale(v, 1 / 30.0f));
            if (g.terrain.solid(n)) { prio = 5; break; }
            p = n;
        }
        if (seen(cam, g, w.pos) && seen(cam, g, p)) continue;  // 4 / 6: no cut
        Vector3 h = {w.vel.x, 0, w.vel.z};
        if (prio > pend.prio || pend.age > 1) pend = {prio, (int)i, p, Vector3Length(h) > 0.1f ? Vector3Normalize(h) : Vector3{sinf(w.yaw), 0, cosf(w.yaw)}, 0};
    }
}

// Shared TrackCam step on tk (e, d, obj, ov set), 0x532460: no cut while the object stays in clear view, else a hard cut to
// the first clear ViewPoint from the kept search index, 2 a frame; the camera is inherited from the one before
static bool trackStep(Camera3D &cam, const Game &g, float dt) {
    const TrackDef &t = *tk.def;
    tk.cutAgo += dt;
    Vector3 hit, to = Vector3Subtract(tk.obj, cam.position);
    bool in = onScreen(cam, tk.obj);
    tk.seen |= in;  // +0xb9: off screen only counts once seen, or from the start with CutWhenStartOffScreen
    bool lost = (!in && (tk.seen || t.startCut)) || Vector3Length(to) >= t.far || g.terrain.raycast({cam.position, Vector3Normalize(to)}, Vector3Length(to) - 0.5f, &hit) ||
                (tk.force && tk.last < 0);
    Vector3 h = {tk.ov.x, 0, tk.ov.z}, od = Vector3Length(h) > 0.05f ? Vector3Normalize(h) : tk.d;  // 0x5321b0: its velocity, else the event's
    auto side = [&](Vector3 p) {  // never crosses the object's line of travel
        Vector3 v = {tk.obj.x - p.x, 0, tk.obj.z - p.z};
        float L = Vector3Length(v);
        return L > 0.01f ? (od.z * v.x - od.x * v.z) / L : 0;
    };
    for (int k = 0; k < 2 && lost && (tk.cutAgo >= 1 || tk.last < 0); k++, tk.idx = (tk.idx + 1) % t.n) {
        const Vector3 &o = t.vp[tk.idx];
        Vector3 c = Vector3Add(tk.e, {o.x * tk.d.z + o.z * tk.d.x, o.y, -o.x * tk.d.x + o.z * tk.d.z});
        Vector3 a = Vector3Subtract(tk.obj, c), b = Vector3Subtract(tk.e, c);
        if (c.y < g.water + 1 || g.terrain.solid(c) || side(c) * side(cam.position) < -1e-5f || g.terrain.raycast({c, Vector3Normalize(a)}, Vector3Length(a) - 0.5f, &hit) ||
            (Vector3Length(b) > 0.5f && g.terrain.raycast({c, Vector3Normalize(b)}, Vector3Length(b) - 0.5f, &hit)))
            continue;
        if (tk.idx != tk.last) cam.position = c, cam.target = tk.obj, tk.cutAgo = 0, tk.last = tk.idx, tk.cutDone = true;  // hard cut; the same ViewPoint: none
        break;
    }
    cam.target = Vector3Lerp(cam.target, tk.obj, 1 - expf(-dt * 6.3f));  // LookSpeed 0.1/frame
    Vector3 back = Vector3Subtract(cam.position, tk.obj);
    bool coming = Vector3Length(tk.ov) < 0.01f || Vector3DotProduct(Vector3Subtract(cam.target, cam.position), tk.ov) < 0;
    if (coming && Vector3Length(back) < t.pref) {  // 0x533190: backs off an object at rest or heading at it, clipped by land
        Vector3 want = Vector3Add(tk.obj, Vector3Scale(Vector3Normalize(back), t.pref)), w = Vector3Subtract(want, cam.position);
        if (Vector3Length(w) > 0.01f && g.terrain.raycast({cam.position, Vector3Normalize(w)}, Vector3Length(w), &hit)) want = Vector3Lerp(cam.position, hit, 0.9f);
        cam.position = Vector3Lerp(cam.position, want, 1 - expf(-dt * t.zoom));
    }
    cam.position.y = fmaxf(cam.position.y, g.water + 1);
    if (g.terrain.solid(cam.position)) cam.position.y += 15 * dt;
    cam.fovy = Lerp(cam.fovy, 50.0f, 1 - expf(-dt * 8));
    return true;
}

static void wormTrack(const Game &g, int worm, int prio, Vector3 e, Vector3 d, bool force) {
    tk = {}, tk.on = true, tk.def = &WORM_T, tk.worm = worm, tk.prio = prio, tk.e = e, tk.d = d, tk.force = force, tk.rest = 1.5f;
    fly.on = false, abd.worm = -1;
}

static void simple(Camera3D &cam, Vector3 pos, Vector3 look, float kp, float kl, float dt) {  // W4M SimpleCam (PosUpdateSpeed, LookUpdateSpeed)
    cam.position = Vector3Lerp(cam.position, pos, perFrame(kp, dt)), cam.target = Vector3Lerp(cam.target, look, perFrame(kl, dt));
    cam.fovy = Lerp(cam.fovy, 50.0f, 1 - expf(-dt * 8));
}

// framing: the death or hp-count close-up, W4M's "Worm Dying" / "Worm Displaying Damage Taken" WormTrackCamera (0x51cf20, 5)
static bool track(Camera3D &cam, const Game &g, bool &chase, float dt, bool framing) {
    if (int d = g.dying(); d != lastDying) {  // a new death asks at 5: over a running worm track (5 or 3) after 200 ms
        lastDying = d;
        if (framing && d >= 0 && tk.worm >= 0 && tk.worm != d && sinceTrack > 0.2f) tk = {}, sinceTrack = 0;
    }
    int run = tk.on && (tk.worm >= 0 || !framing) ? tk.prio : framing ? 5 : 0;  // 0x51d408: a lower request is dropped
    if (framing && pend.prio && pend.prio < run) pend.prio = 0;
    if (overT > 0 && !g.cfg.mission) {  // game over: WormTrackCamera on the winner (current worm first), cut at once, until the orbit
        int c = -1;
        for (size_t i = 0; i < g.worms.size(); i++) if (g.worms[i].alive && g.worms[i].team == g.winner && (c < 0 || (int)i == g.current)) c = (int)i;
        if (c < 0 || overT > 4) return tk = {}, false;
        if (tk.worm != c || !tk.force) wormTrack(g, c, 5, g.worms[c].pos, {sinf(g.worms[c].yaw), 0, cosf(g.worms[c].yaw)}, true), tk.rest = 1e9f;
    } else if (pend.prio && pend.age < 1 && sinceTrack > 0.2f && run <= pend.prio && g.phase != Phase::Aim)
        wormTrack(g, pend.worm, pend.prio, pend.e, pend.d, false), sinceTrack = 0, pend.prio = 0;
    if (tk.on && tk.worm >= 0) {  // WormTrackCamera: until the worm rests 1.5 s
        const Worm &w = g.worms[tk.worm];
        bool still = !w.alive || (w.grounded && Vector3Length(w.vel) < 0.1f);
        if ((still && (tk.rest -= dt) <= 0) || (g.phase == Phase::Aim && !tk.force)) return tk = {}, false;
        if (w.pos.y > g.water - 2) tk.obj = Vector3Add(w.pos, {0, 0.5f, 0});  // sinking: stops following
        tk.ov = w.vel;
        return trackStep(cam, g, dt);
    }
    if (framing) {  // W4M "Worm Dying" / "Worm Displaying Damage Taken": a WormTrackCamera request at 5 on the counted worm
        if (!tk.frame || Vector3Distance(tk.e, focusAt) > 2) {
            const Worm *n = nullptr;
            for (const Worm &w : g.worms) if (Vector3Distance(w.pos, focusAt) < 2.5f && (!n || Vector3Distance(w.pos, focusAt) < Vector3Distance(n->pos, focusAt))) n = &w;
            Vector3 f = Vector3Subtract(cam.target, cam.position);
            f.y = 0;
            tk = {}, tk.on = tk.frame = true, tk.def = &WORM_T, tk.prio = 5, tk.e = focusAt;  // event direction: the worm's facing (group: the view's)
            tk.d = n ? Vector3{sinf(n->yaw), 0, cosf(n->yaw)} : Vector3Length(f) > 0.01f ? Vector3Normalize(f) : Vector3{0, 0, 1};
        }
        tk.obj = focusAt;
        return trackStep(cam, g, dt);
    }
    if (tk.frame) tk = {};
    if (focusOn && focusCrate) {  // CrateTrackCamera, its own position as the event point
        if (tk.def != &CRATE_T) {
            Vector3 f = Vector3Subtract(cam.target, cam.position);
            f.y = 0;
            tk = {}, tk.on = true, tk.def = &CRATE_T, tk.prio = 1, tk.d = Vector3Length(f) > 0.01f ? Vector3Normalize(f) : Vector3{0, 0, 1};
        }
        tk.ov = dt > 0 && tk.obj.y ? Vector3Scale(Vector3Subtract(focusAt, tk.obj), 1 / dt) : Vector3{};
        tk.e = tk.obj = focusAt;
        return trackStep(cam, g, dt);
    }
    if (tk.def == &CRATE_T) tk = {};
    if (abd.worm >= 0) {  // AlienAbductionCamera (1, 1): fixed above the UFO, locked on the worm until it rests
        const Worm &w = g.worms[abd.worm];
        Vector3 at = abd.pos, c = Vector3Add(w.pos, {0, 2.5f, 2.5f}), d = {0, 2.5f, 2.5f}, hit;
        if (w.vel.y > 0) {  // 0x547490 state 2, the beam lifts it: worm + (0, 50, 50) units cut short by land, kept if > 10 units off
            if (g.terrain.raycast({w.pos, Vector3Normalize(d)}, Vector3Length(d), &hit)) c = hit;
            if (Vector3Distance(c, w.pos) > 0.5f) at = c;
        }
        if (g.phase == Phase::Aim || ((!w.alive || w.grounded) && (abd.rest -= dt) <= 0)) abd.worm = -1;
        else return simple(cam, at, Vector3Add(w.pos, {0, 0.5f, 0}), 1, 1, dt), true;
    }
    if (floodT > 1.4f && floodT < 4.7f && g.phase != Phase::Aim) {  // FloodCamera (0.01, 0.01): far off the level, at the rain cloud
        float c = Terrain::NX * Terrain::VOX / 2, top = g.terrain.colTop.empty() ? 20 : g.terrain.colTop.back() * Terrain::VOX, cloud = top + 22.5f;
        return simple(cam, {c, cloud - 10, c + fmaxf(c, 150)}, {c, cloud, c}, 0.01f, 0.01f, dt), true;
    }
    const Projectile *s = chase && !g.shots.empty() ? &g.shots[0] : nullptr;
    const WeaponDef *wd = s ? &WEAPONS[s->weapon] : nullptr;
    if (g.phase == Phase::Aim) sa = {};
    for (const Projectile &p : g.shots)  // Super Airstrike: drops counted off the bomber, the first cow still falling is the payload
        if (WEAPONS[p.weapon].kind == Kind::Airstrike && WEAPONS[p.weapon].fuse > 0) {
            if (p.child) { sa.look = p.pos, sa.rest = 1.5f; break; }
            if (p.prey > sa.n) sa.b = Vector3Subtract(p.pos, {0, 1, 0}), sa.a = sa.n ? sa.a : sa.b, sa.n = p.prey;
            sa.v = {p.vel.x, 0, p.vel.z}, sa.of = WEAPONS[p.weapon].clusters;
        }
    if (sa.n && sa.n >= sa.of && g.phase != Phase::Aim && (sa.rest -= dt) > 0) {  // after the last bomb
        Vector3 ab = {sa.b.x - sa.a.x, 0, sa.b.z - sa.a.z}, d = Vector3Normalize(Vector3Length(ab) > 1 ? ab : sa.v);
        return simple(cam, Vector3Add(sa.a, {-10 * d.x + 2.5f * d.z, 0, -10 * d.z - 2.5f * d.x}), sa.look, 1, 0.1f, dt), true;  // A - 200 d + 50 perp(d)
    }
    if (wd && wd->kind == Kind::Airstrike && wd->fuse <= 0) return hold = 0, chase = false, false;  // no camera: the current one stays
    bool flies = wd && !s->child && (wd->kind == Kind::Homing || (wd->kind == Kind::SuperSheep && (wd->walks ? s->stage == 1 : wd->fuse - s->fuse > 3.5f)));
    if (flies) {  // FlyCam (Homing / SuperSheepFly / Starburst): LagBehind 60 / 80 units, LookAhead 100, PosSpeed 0.05, LookSpeed 0.1 / 0.2 a frame
        bool homing = wd->kind == Kind::Homing;
        Vector3 f = Vector3Normalize(s->vel), want = Vector3Subtract(s->pos, Vector3Scale(f, homing ? 3 : 4)), b = Vector3Subtract(want, s->pos), hit;
        if (g.terrain.raycast({s->pos, Vector3Normalize(b)}, Vector3Length(b), &hit)) want = Vector3Subtract(hit, Vector3Scale(Vector3Normalize(b), 0.5f));
        want.y = fmaxf(want.y, g.water + 0.5f);  // MinPosition 10
        float kp = fly.on ? fly.kp : 0;  // FlyCam 0x5282e4: the position factor eases from 0 to PosSpeed at PosRate 0.01 / 1 / 0.1 a frame
        kp += (0.05f - kp) * perFrame(homing ? 0.01f : wd->walks ? 1 : 0.1f, dt);
        simple(cam, want, Vector3Add(s->pos, Vector3Scale(f, 5)), kp, homing ? 0.1f : 0.2f, dt);
        fly = {true, 1, kp, {}};
        return true;
    }
    if (fly.on && !s && g.phase != Phase::Aim) {  // PauseDuration 1 s after the blast, backing off FinalDistance 500 units (stopped by land)
        if (fly.hold == 1) {
            Vector3 b = Vector3Normalize(Vector3Subtract(cam.position, cam.target)), hit;
            fly.end = g.terrain.raycast({cam.position, b}, 25, &hit) ? Vector3Subtract(hit, Vector3Scale(b, 0.5f)) : Vector3Add(cam.position, Vector3Scale(b, 25));
        }
        if ((fly.hold -= dt) > 0) return cam.position = Vector3Lerp(cam.position, fly.end, perFrame(fly.kp, dt)), true;
    }
    fly.on = false;
    bool fat = wd && wd->name == "Fatkins Strike";  // FatkinsTrackCamera
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
    if (tk.flight == 0 || Vector3Distance(s->vel, tk.pv) > 1.5f) {  // launch or bounce: FindFirstEvent 0x576580 (0x574e90, sweep 0x466ae0)
        Vector3 p = s->pos, v = s->vel, hv;  // the event: its first predicted land / water contact, or where the fuse runs out
        float fuse = wd->fuse > 0 && !wd->restFuse && !s->child ? s->fuse : 1e9f;
        for (int k = 0; k < 600 && p.y > g.water && (fuse -= Game::DT) > 0; k++) {
            v.y -= g.gravity() * (s->child ? 1 : wd->grav) * Game::DT;
            if (wd->wind || (g.cfg.wormpot & WP_WIND_ALL)) v.x += g.wind * Game::WIND_ACCEL * Game::DT;
            Vector3 n = Vector3Add(p, Vector3Scale(v, Game::DT));
            if (g.terrain.solid(n)) break;
            p = n;
        }
        hv = {v.x, 0, v.z};
        tk.e = p, tk.d = Vector3Length(hv) > 0.1f ? Vector3Normalize(hv) : Vector3{sinf(g.worms[g.current].yaw), 0, cosf(g.worms[g.current].yaw)};
        if (!tk.on) tk.flight = 0;  // +0x1a8: flight time since the last event
    }
    tk.flight += dt, tk.obj = s->pos, tk.ov = tk.pv = s->vel;
    if (!tk.on && tk.flight < 1 && onScreen(cam, tk.e)) return chase = false, false;  // ShoulderCamera meanwhile
    tk.on = true, tk.rest = 1.5f, tk.prio = 2, tk.def = fat ? &FATKINS_T : &PAYLOAD_T;
    return trackStep(cam, g, dt);
}

static void resetTrack() { tk = {}, pend = {}, abd.worm = lastDying = -1, sa = {}, fly.on = false, floodT = sinceTrack = sinceBoom = 99, lastVel.clear(); }

// W4M OccludingCam test (0x52efa0): the centre ray to the camera blocked, and with it >= 90 % of the 10 rays (centre and
// +-55 units right / up, 5 inner points on a 41.25-unit arc, 9..171 deg); chase cameras: all 5 front rays. hit: the centre's
static bool occludes(const Game &g, Vector3 from, Vector3 to, Vector3 *hit, bool inner) {
    Vector3 d = Vector3Subtract(to, from), h;
    float L = Vector3Length(d);
    if (L < 0.01f || !g.terrain.raycast({from, Vector3Scale(d, 1 / L)}, L, hit)) return false;
    Vector3 f = Vector3Scale(d, 1 / L), r = Vector3CrossProduct(f, {0, 1, 0});
    r = Vector3Length(r) < 0.01f ? Vector3{1, 0, 0} : Vector3Normalize(r);
    Vector3 u = Vector3CrossProduct(r, f);
    int n = 1, blocked = 1;
    auto ray = [&](Vector3 o) { n++, blocked += g.terrain.raycast({Vector3Add(from, o), f}, L + 0.5f, &h); };  // OcclusionSize 10
    for (Vector3 o : {r, Vector3Negate(r), u, Vector3Negate(u)}) ray(Vector3Scale(o, 2.75f));
    for (int i = 0; inner && i < 5; i++) {
        float a = (9 + 40.5f * i) * DEG2RAD;
        ray(Vector3Add(Vector3Scale(r, cosf(a) * 2.0625f), Vector3Scale(u, sinf(a) * 2.0625f)));
    }
    return blocked >= (inner ? 0.9f : 1.0f) * n - 0.01f;
}

void camera(Camera3D &cam, const Game &g, bool chase, bool scope, bool input, float dt) {
    const Worm &cur = g.worms[g.current];
    int pad = padUsed;
    if ((focusLeft -= dt) <= 0) focusOn = false;
    watch(g, cam, dt);
    overT = g.phase == Phase::GameOver ? overT + dt : 0;
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
    if (g.roped != wasRoped) ropeYaw = cur.yaw + PI / 2, snap = !g.roped, wasRoped = g.roped, ninjaIdx = 0;  // NinjaCamera StartYaw 1.57, ResetYaw
    if (rs.x || rs.y) snap = false;
    idle = rs.x || rs.y || (moving && !pet) ? 0 : idle + dt;  // steering a super sheep keeps the camera behind it
    camYaw -= rs.x * CAM_YAW * settings.cam * dt;
    camEl = Clamp(camEl - rs.y * (settings.invertCam ? -1 : 1) * CAM_PITCH * settings.cam * dt, 0.05f, 1.2f);  // stick up: look up
    float follow = scope || aimMode ? 12 : g.roped ? (rs.x || rs.y ? 0 : 6.3f) : pet ? (idle > 0.5f ? 4.0f : 0) : snap ? 4 : chase ? (idle > 0.5f ? 2.5f : 0) : idle > RECENTER_AFTER ? 1.2f : 0;
    if (pet && pet->vel.x * pet->vel.x + pet->vel.z * pet->vel.z > 1) petYaw = atan2f(pet->vel.x, pet->vel.z);  // stopped at a wall: last heading
    float k = 1 - expf(-dt * follow), err = remainderf((pet ? petYaw : g.roped ? ropeYaw : cur.yaw) - camYaw, 2 * PI);
    camYaw += err * k, camEl += ((g.roped ? 0 : EL0) - camEl) * k;  // NinjaCamera DefaultHeight 0: level side view
    if (fabsf(err) < 0.05f) snap = false;
    zoom = Clamp(zoom * expf(-zin * dt * 1.5f - wheel * 0.1f), 0.45f, 2.5f);

    if (!focusOn && g.girderOn && girdering(g)) {  // W4M GirderCam: CAMTWK GirderCamera, DistFromObject 325, DefaultHeight 0.6 rad, on Girder.Position
        float y = g.cursorYaw, p = Game::GIRDER_CAM_PITCH, k = cut ? 1 : 1 - expf(-dt * 6);  // PosUpdateSpeed 0.1 a frame
        Vector3 e = Vector3Add(g.girder, {-sinf(y) * cosf(p) * Game::GIRDER_CAM, sinf(p) * Game::GIRDER_CAM, -cosf(y) * cosf(p) * Game::GIRDER_CAM});
        cam.position = Vector3Lerp(cam.position, e, k), cam.target = Vector3Lerp(cam.target, g.girder, k), cam.fovy = 50;
        cut = false, fpOut += dt;
        return;
    }
    static bool inBlimp = false;
    if (!focusOn && targetView(g)) {  // W4M Blimp (IsometricCam): a cut, then exactly the sim's camera; the reticle is the screen centre
        if (!inBlimp) blimpZoom = 1;
        blimpZoom = Clamp(blimpZoom * expf(-zin * 0.5f * dt) - 0.08f * wheel, 0.15f, 2);  // ZoomSpeed 0.99 / 20 ms, MouseZoomSpeed 0.08
        bool live = g.cursorOn && targeted(WEAPONS[g.weapon].kind);  // before the first TARGET tick, or a CPU: around its aim point
        Vector3 f = live ? g.cursor : Vector3Add(g.target(), {0, Game::BLIMP_LIFT, 0});
        float y = live ? g.cursorYaw : cur.yaw, p = live ? g.cursorPitch : Game::BLIMP_PITCH;
        cam.target = f, cam.position = g.blimpEye(f, y, p), cam.fovy = 50 * blimpZoom;
        cam.up = {sinf(y) * sinf(p), cosf(p), cosf(y) * sinf(p)};  // W4M 0x52a0a0: up = R(pitch, yaw) (0, 1, 0), never along the view
        inBlimp = true, fpOut += dt, cut = false;
        return;
    }
    inBlimp = false, cam.up = {0, 1, 0};

    if (!focusOn && (firstPerson(g) || scope)) {  // W4M aim view: first person from the worm's eyes, looking down the shot line
        Vector3 e = eye(g), f = Vector3Add(e, Vector3Scale(g.aimDir(cur), AIM_FOCUS));
        float k = cut || fpOut > 0 ? 1 : 1 - expf(-dt * 16), fov = scope ? 25.0f : fine ? 42.0f : 60.0f;  // cut in: a fly-in crosses the worm's body
        cam.position = Vector3Lerp(cam.position, e, k), cam.target = Vector3Lerp(cam.target, f, k);
        cam.fovy = k == 1 ? fov : Lerp(cam.fovy, fov, 1 - expf(-dt * 8));
        fpOut = 0, cut = false;
        return;
    }
    static float jetEl = 0.5f;
    if (!focusOn && !chase && g.jetting) {  // W4M JetpackCamMkII 0x52b4c0: no input; behind the worm's yaw, pitch eases to 0.5 - 4 vy
        float want = 0.5f - 4 * cur.vel.y / 50, k = cut ? 1 : 1 - powf(0.99f, dt / 0.02f);  // DefaultPitch, PitchScale (vy units/ms), PitchSpeed
        jetEl += (want - jetEl) * k, camYaw = cur.yaw, camEl = Clamp(jetEl, 0.05f, 1.2f);  // MinPitch / MaxPitch -70 / 60 never bind
        const float L = 11.5f;  // StickLength 230 units
        Vector3 e = Vector3Add(cur.pos, {-sinf(cur.yaw) * cosf(jetEl) * L, sinf(jetEl) * L, -cosf(cur.yaw) * cosf(jetEl) * L});
        e.y = fmaxf(e.y, g.water + 0.25f);  // over Water.Level + 5 units
        cam.position = cut ? e : Vector3Lerp(cam.position, e, 1 - expf(-dt * 8)), cam.target = cur.pos, cam.fovy = 50;
        fpOut += dt, cut = false;
        return;
    }
    jetEl = 0.5f;
    if (track(cam, g, chase, dt, focusOn && !focusCrate)) { fpOut += dt, cut = false; return; }
    Vector3 focus = focusOn ? focusAt : cur.pos, from = focus, want;
    if (focusOn) chase = false;
    if (chase) from = focus = onBlast ? blast : Vector3Add(g.shots[0].pos, Vector3Scale(g.shots[0].vel, 0.1f));  // lead the shot a little
    const Worm *champ = nullptr;  // W4M game over: the winner 4 s (WormTrackCamera), then the orbit
    for (const Worm &x : g.worms) if (overT > 0 && !focusOn && x.alive && x.team == g.winner && (!champ || &x == &cur)) champ = &x;
    if (champ) chase = false, from = focus = champ->pos;
    bool orbit = overT > 4 && !g.cfg.mission;
    if (fpOut == 0 && !chase) cut = true;  // cut out too: backing out passes through the worm's head
    if (fpOut == 0 && chase) cam.target = Vector3Add(cam.position, Vector3Normalize(Vector3Subtract(cam.target, cam.position)));  // far aim point pulled in
    fpOut += dt;
    float kt = cut ? 1 : 1 - expf(-dt * 6), kp = cut ? 1 : 1 - expf(-dt * (pet ? 6 : chase ? 2.5f : 3));
    {
        float back = (chase && !pet ? 17.5f : pet ? 8.5f : g.roped && !focusOn ? 20 : focusOn ? fmaxf(7.5f, focusR * 3 + 4) : 8.5f) * zoom;  // Shoulder DistFromObject 170 units; fov 50: 3 r fits r
        bool group = (focusOn && focusR > 0) || champ;  // hp count of several worms, winner: from higher, around the scenery in the way
        float el = group ? fmaxf(camEl, 0.75f) : pet ? petEl : camEl;  // W4M *ChaseCamera: dist 170, DefaultHeight 0.255 rad
        for (float dy : {0.0f, 0.8f, -0.8f, 1.6f, -1.6f, 2.4f, -2.4f, 0.0f}) {
            float y = camYaw + dy;
            want = Vector3Add(focus, {-sinf(y) * cosf(el) * back, sinf(el) * back, -cosf(y) * cosf(el) * back});
            Vector3 d = Vector3Subtract(want, from), h;
            if (!group || !g.terrain.raycast({from, Vector3Normalize(d)}, Vector3Length(d), &h)) break;
        }
    }
    Vector3 hit, to = Vector3Subtract(want, from);
    bool ninja = g.roped && !focusOn && !chase && !pet;
    if (orbit) {  // W4M OrbitCam: Land.Center at max((top + water + 20) / 2, water + 20), radius + 200, height 450 units, 0.3 rad/s
        float top = g.terrain.colTop.empty() ? 20 : g.terrain.colTop.back() * Terrain::VOX, low = g.water + 1, mid = fmaxf((low + top) / 2, low);
        orbitA = overT - dt <= 4 ? atan2f(cam.position.x - Terrain::NX * Terrain::VOX / 2, cam.position.z - Terrain::NZ * Terrain::VOX / 2) : orbitA + dt * 0.3f;
        float R = Terrain::NX * Terrain::VOX / 2 + 10;
        focus = {Terrain::NX * Terrain::VOX / 2, mid, Terrain::NZ * Terrain::VOX / 2};
        want = {focus.x + sinf(orbitA) * R, mid + 22.5f, focus.z + cosf(orbitA) * R};
    } else if (chase && !pet) want.y = fmaxf(want.y, cur.pos.y + 4);  // donkey/airstrike dig below the surface: stay above ground
    else if (ninja) {  // NinjaCamMkIII 0x52d3c0: never zooms in (OccZoomInSpeed 0), eases back to 400 units at OccZoomOutSpeed 0.04 a frame
        reach = cut ? 1 : reach + (1 - reach) * perFrame(0.04f, dt);
        want = Vector3Add(from, Vector3Scale(to, reach));
        auto inLand = [&](Vector3 p) {  // 0x52d140: land within 5 units along +z, +x, +y
            return g.terrain.solid(p) || g.terrain.raycast({p, {0, 0, 1}}, 0.25f, &hit) || g.terrain.raycast({p, {1, 0, 0}}, 0.25f, &hit) || g.terrain.raycast({p, {0, 1, 0}}, 0.25f, &hit);
        };
        float h = sqrtf(to.x * to.x + to.z * to.z);
        for (int k = 0; k < 8 && ninjaIdx < 154 && !(rs.x || rs.y) && inLand(want); k++, ninjaIdx++) {  // 0x52d250, 8 a frame, idle stick
            float y = camYaw + (ninjaIdx < 14 ? (ninjaIdx / 2 + 1) * PI / 8 * (ninjaIdx % 2 ? -1 : 1) : 0);  // table 0x91eaf0, then zeros
            Vector3 c = Vector3Add(from, {-sinf(y) * h, to.y, -cosf(y) * h});  // at DistFromObject
            if (!inLand(c)) { camYaw = ropeYaw = y, want = c, reach = 1, ninjaIdx = 0; break; }
        }
    } else {  // W4M OccludingCam: zooms in at once to the first hill on the worm-camera ray, back out at 0.02/frame
        bool hidden = occludes(g, from, want, &hit, !pet);
        float r = hidden ? fmaxf(0.9f * Vector3Distance(from, hit) - 0.3f, pet ? 2.5f : 0.6f) / Vector3Length(to) : 1;  // chase MinZoomDist 50 units
        petEl = !pet ? 0.255f : hidden ? fminf(petEl + 0.4f * dt, 1.2f) : petEl + (0.255f - petEl) * (1 - expf(-dt * 0.5f));  // OccHeightSpeed 0.4: rises, never under
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
    if (pet) cam.position.y = fmaxf(cam.position.y, from.y + 0.5f);
    Vector3 back = Vector3Subtract(cam.position, from);  // following (not flying back from afar): never behind the scenery, pulled in front of it
    if (!orbit && !chase && !ninja && Vector3Length(back) < Vector3Length(to) + 2 && occludes(g, from, cam.position, &hit, !pet))
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

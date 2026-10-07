#pragma once
#include <cstddef>
#include "raylib.h"

// W4M meshes converted by tools/w4m-models into assets/models/<name>.glb. Missing => draw() returns false
// and the caller keeps its placeholder shapes.
namespace Models {
void start();                 // worker threads read, decode and sample the .glb files: the menu scene, then (second call) the rest
bool upload(double until);    // main thread, until GetTime() reaches until: GPU uploads; true while models are pending
bool menuReady();             // the menu scene's models (frontend/, seagull) are uploaded
Model take(const char *path);  // a frontend/ model prepare() or decode() decoded (else loaded now); the caller unloads it
void decode(const char *path);  // worker thread: reads and decodes one .glb for a later take()
void unload();
size_t bytes();  // RAM held by the loaded models: mesh arrays and sampled clips
const char *bootStats();  // per-phase load times so far, for the BOOT log
// W4M WormPoseManager layers over the body clip: the emote on the face bones the clip leaves still (WormEmote), the head
// (HeadRotY/X: WormLookAt), the shoulders (Left/RightArmRotY/X: WormGestureAt or the head, by the clip's Blend node) and the
// pupils (Eyes_LR/UD). Angles in radians, + = to the worm's left / up; draw() applies the clips' own limits.
struct Layers {
    const char *face = nullptr; float faceT = 0;  // looped emote clip
    float lookYaw = 0, lookPitch = 0;               // head
    float gestYaw = 0, gestPitch = 0;               // GestureAt target
    float eyeYaw = 0, eyePitch = 0;                 // pupils, relative to the head
    const char *act[2] = {}; float actT[2] = {}, actW[2] = {};  // acting gestures (new, old) over the body clip at weight actW
    float aimW = 1;  // weight of draw()'s aim clip
    const char *add[4] = {}; float addT[4] = {};  // clips summed over the pose at weight 1, like the aim clip (W4M FP, FPX, FPY, FPZ)
    // lip sync (0x59d570): viseme clips (new, old) added on the lips at weight = clip time; open (+0x19c) > 0.1 picks the Teeth time
    const char *lip[2] = {}; float lipW[2] = {}, open = 0;
};
// W4M "Blend" node of the clip at t (under ly's gestures), relative to Base: x / y = left / right arm mode, z = head mode (degrees)
bool blend(const char *name, const char *clip, float t, bool loop, const Layers *ly, Vector3 *out);
float curve(const float (*keys)[6], int n, float t);  // W4M unweighted key curve; keys: in-tangent x, y, out-tangent x, y, time, value
// Model faces +z: yaw turns it about +y, pitch raises the nose. clip: animation name, t in seconds.
// aim: a clip at aimT added over clip, as W4M plays Aim* over Hold* / Fire* and JetpackRotLR over JetpackFly (both at weight 1).
bool draw(const char *name, Vector3 pos, float yaw, float pitch = 0, Color tint = WHITE, const char *clip = nullptr, float t = 0, bool loop = true,
          const char *aim = nullptr, float aimT = 0, const Layers *ly = nullptr);
float clipLength(const char *name, const char *clip);  // seconds, 0 if absent
bool has(const char *name);
bool fxLocator(const char *name, Vector3 *out);  // WEAPTWK FxLocator node of a payload model (model space, metres); false if absent
float bottom(const char *name);  // depth of the mesh below its origin, 0 if missing
// Model-space matrix of a joint (e.g. "WeaponLocator") in that clip pose; false if missing.
bool joint(const char *name, const char *joint, const char *clip, float t, bool loop, Matrix *out, const char *aim = nullptr, float aimT = 0,
           const Layers *ly = nullptr);
bool draw(const char *name, Matrix m, Color tint = WHITE, const char *clip = nullptr, float t = 0, bool loop = true);  // full world matrix; a clip not looping holds its last key
void shade(Shader s);  // every draw() uses s until shade({})
void pick(const char *clip, float t = 0);  // every draw() also plays this W4M clip's XChildSelector keys (looped) until pick(nullptr)
bool visible(Vector3 c, float r);  // sphere vs the current BeginMode3D view frustum
bool visible(Vector3 c, float r, const Matrix &mvp);  // sphere vs that view-projection's frustum
// Hats (assets/models/hats/*.glb), sorted by file name so every client's list agrees. draw() them by hatName(i).
int hatCount();
const char *hatName(int i);  // "" if i out of range
}

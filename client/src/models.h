#pragma once
#include "raylib.h"

// W4M meshes converted by tools/w4m-models into assets/models/<name>.glb. Missing => draw() returns false
// and the caller keeps its placeholder shapes.
namespace Models {
void prepare();               // worker thread: reads, decodes and samples every .glb
bool upload(double until);    // main thread, until GetTime() reaches until: GPU uploads; true while models are pending
Model take(const char *path);  // a frontend/ model prepare() decoded (else loaded now); the caller unloads it
void unload();
// W4M WormPoseManager layers over the body clip: the emote on the face bones the clip leaves still (WormEmote) and the
// head turned (HeadRotY/HeadRotX: WormLookAt). Angles in radians.
struct Layers {
    const char *face = nullptr; float faceT = 0;  // looped emote clip
    float lookYaw = 0, lookPitch = 0;               // + = to the worm's left / up
};
// Model faces +z: yaw turns it about +y, pitch raises the nose. clip: animation name, t in seconds.
// aim: the arms (shoulder subtrees) re-aimed by that clip at aimT, as W4M layers its shoulder-only Aim* clips over Fire* ones.
bool draw(const char *name, Vector3 pos, float yaw, float pitch = 0, Color tint = WHITE, const char *clip = nullptr, float t = 0, bool loop = true,
          const char *aim = nullptr, float aimT = 0, const Layers *ly = nullptr);
float clipLength(const char *name, const char *clip);  // seconds, 0 if absent
bool has(const char *name);
float bottom(const char *name);  // depth of the mesh below its origin, 0 if missing
// Model-space matrix of a joint (e.g. "WeaponLocator") in that clip pose; false if missing.
bool joint(const char *name, const char *joint, const char *clip, float t, bool loop, Matrix *out, const char *aim = nullptr, float aimT = 0,
           const Layers *ly = nullptr);
bool draw(const char *name, Matrix m, Color tint = WHITE, const char *clip = nullptr, float t = 0);  // full world matrix; clip loops
void shade(Shader s);  // every draw() uses s until shade({})
bool visible(Vector3 c, float r);  // sphere vs the current BeginMode3D view frustum
// Hats (assets/models/hats/*.glb), sorted by file name so every client's list agrees. draw() them by hatName(i).
int hatCount();
const char *hatName(int i);  // "" if i out of range
}

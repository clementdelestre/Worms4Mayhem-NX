#pragma once
#include "raylib.h"

// W4M meshes converted by tools/w4m-models into assets/models/<name>.glb. Missing => draw() returns false
// and the caller keeps its placeholder shapes.
namespace Models {
void load(void (*progress)() = nullptr);  // progress: called after each file (boot splash)
void unload();
// Model faces +z: yaw turns it about +y, pitch raises the nose. clip: animation name, t in seconds.
bool draw(const char *name, Vector3 pos, float yaw, float pitch = 0, Color tint = WHITE, const char *clip = nullptr, float t = 0, bool loop = true);
float clipLength(const char *name, const char *clip);  // seconds, 0 if absent
bool has(const char *name);
// Model-space matrix of a joint (e.g. "WeaponLocator") in that clip pose; false if missing.
bool joint(const char *name, const char *joint, const char *clip, float t, bool loop, Matrix *out);
bool draw(const char *name, Matrix m, Color tint = WHITE);  // static model, full world matrix
bool visible(Vector3 c, float r);  // sphere vs the current BeginMode3D view frustum
// Hats (assets/models/hats/*.glb), sorted by file name so every client's list agrees. draw() them by hatName(i).
int hatCount();
const char *hatName(int i);  // "" if i out of range
}

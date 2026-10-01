#pragma once
#include "raylib.h"

// W4M's worm silhouette (CG/PostProcess.cg Silhouette_PC + Outline, docs/camera-w4m.md section 5): where the scene hides
// the worm it is tinted 50% toward grey 0.25; the blimp view adds a 1 px team-colour edge at alpha 0.247 through the scene.
namespace Xray {
// Camera.WormOpaqueDist 50 / WormTransparencyDist 25 (CAMTWK, 20 units a metre): worms fade out as the camera closes in.
float opacity(Vector3 cam, Vector3 worm);
void depth();               // draw the worm after this: depth only
void fade(float op);        // then draw it again: blended at op over the scene, nearest layer only
void begin();               // per frame, inside BeginMode3D, before the worms
bool mask(int team);        // draw the worm again after this: its depth into the mask; false if unavailable
void hidden();              // then draw it once more: the grey fill where the scene hides it
void done();                // back to normal drawing (after any of the above)
void outline(const Color *teams);  // after EndMode3D: the 4 team colours
}

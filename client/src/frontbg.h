#pragma once

// W4M's animated title backdrop (WX.Mesh.Title island and its effects, FRONTEND.Sky clouds), drawn over the 2D
// background: draw() is a no-op when assets/models/frontend/ is missing, so call it after the fallback.
namespace FrontBg {
void load();  // optional: the first draw() loads
void draw(float dt, bool particles = true);  // inside BeginDrawing, before the menu widgets; particles false: the caller updates and draws Fx itself, over its own layers
void page(int id);    // 0 main (island + Intro_Title restarts), others: sea and sky only
void unload();
}  // namespace FrontBg

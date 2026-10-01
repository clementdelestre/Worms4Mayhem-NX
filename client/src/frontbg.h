#pragma once

// W4M's animated title backdrop (WX.Mesh.Title island, FRONTEND.Sky clouds, seagulls, smoke), drawn over the 2D
// background: draw() is a no-op when assets/models/frontend/ is missing, so call it after the fallback.
namespace FrontBg {
void load();  // optional: the first draw() loads
void draw(float dt);  // inside BeginDrawing, before the menu widgets
void page(int id);    // 0 main, 1 local game (sea view), 2 network, 3 options...: the camera glides there
void unload();
}  // namespace FrontBg

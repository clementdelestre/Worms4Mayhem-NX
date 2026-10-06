#include "efmv.h"
#include "controls.h"
#include "raymath.h"
#include "script.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {
// A knot list's points (strtok " ,", 0x52c140): the first detail of that exact name (0x52be30); look: TimedPathCam's mode 1,
// the knot 1000 units along its detail's -Z (0x6375a1)
std::vector<Vector3> knots(const Game &g, const std::string &list, bool look) {
    std::vector<Vector3> p;
    for (size_t i = 0; (i = list.find_first_not_of(" ,", i)) != std::string::npos;) {
        size_t j = list.find_first_of(" ,", i);
        std::string n = list.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j;
        const Terrain::Marker *m = nullptr;
        for (const Terrain::Marker &k : g.terrain.markers) if (k.name == n) { m = &k; break; }
        if (!m) { p.push_back(p.empty() ? Vector3{} : p.back()); continue; }  // W4M keeps stack garbage ("Unable to find a knot")
        p.push_back(look ? Vector3Add(m->pos, Vector3Scale(m->dir, 1000 * g.terrain.scale / 20)) : m->pos);
    }
    return p;
}

// KnotList output (0x52bb40 / 0x636de0): cardinal spline over prev, cur, next, next-next (ends repeat), s = (1 - tension) / 2
Vector3 spline(const std::vector<Vector3> &P, int cur, float t, float s) {
    if (P.empty()) return {};
    int n = (int)P.size();
    if (cur + 1 >= n) return P[std::min(cur, n - 1)];
    Vector3 p0 = P[cur > 0 ? cur - 1 : cur], p1 = P[cur], p2 = P[cur + 1], p3 = P[cur + 2 < n ? cur + 2 : cur + 1];
    float t2 = t * t, t3 = t2 * t;
    float c0 = -s * t3 + 2 * s * t2 - s * t, c1 = (2 - s) * t3 + (s - 3) * t2 + 1, c2 = (s - 2) * t3 + (3 - 2 * s) * t2 + s * t, c3 = s * t3 - s * t2;
    return Vector3Add(Vector3Add(Vector3Scale(p0, c0), Vector3Scale(p1, c1)), Vector3Add(Vector3Scale(p2, c2), Vector3Scale(p3, c3)));
}

}  // namespace

bool Efmv::camera(const Game &g, Camera3D &cam) {
    MovieView v = scriptMovie(g);
    if (!v.on || v.camT < 0) return false;
    const Json *e = scriptMovieEvent(g, v.name.c_str(), v.camT << 16 | v.camE);
    if (!e) return false;
    const Json &j = *e;
    std::string ty = j[0].s();
    std::vector<Vector3> P = knots(g, j[3].s(), false), L = v.timed ? knots(g, j[3].s(), true) : knots(g, j[4].s(), false);
    float sp = v.timed ? 0.5f : ty == "CutCamera" ? 0.5f : (1 - j[9].f()) / 2, sl = v.timed ? 0.5f : ty == "CutCamera" ? 0.5f : (1 - j[10].f()) / 2;
    cam.position = spline(P, v.kpCur, v.kpT, sp), cam.target = spline(L, v.klCur, v.klT, sl);
    cam.up = {0, 1, 0}, cam.fovy = Controls::FOV0, cam.projection = CAMERA_PERSPECTIVE;  // up fixed (0x531880), the drawn lens (zoom 1)
    if (Vector3DistanceSqr(cam.position, cam.target) < 1e-8f) cam.target = Vector3Add(cam.position, {0, 0, 1});
    return true;
}

// EfmvBorderEntity (ctor 0x5e66c0, update 0x5e68c0), HUD units of a 540-high 16:9 screen: two black bars centred 265 off the
// middle, half height 25 + BorderHeight x f, f 1 once on (BorderOnTime 0), down to 0 over BorderOffTime: inner edges 190, then 240
void Efmv::borders(const Game &g) {
    MovieView v = scriptMovie(g);
    if (!v.borders || (g.phase == Phase::GameOver && scriptOutro(g) == 1)) return;  // the front end follows the game-over movie at once
    float half = 25 + (float)scriptNum(g, "EFMV.BorderHeight", 50) * (1 - v.bordersOut), h = (270 - (265 - half)) / 540 * GetScreenHeight();
    DrawRectangle(0, 0, GetScreenWidth(), (int)ceilf(h), BLACK);
    DrawRectangle(0, GetScreenHeight() - (int)ceilf(h), GetScreenWidth(), (int)ceilf(h), BLACK);
}

// EFMV AnimateDetail (0x526541): Detail.AnimName, then Detail.PlayAnim(FourCC) to every detail of that code (0x4758d0, 4 bytes)
void Efmv::event(Game &g, const GameEvent &e, float clock) {
    const Json *j = e.kind == GameEvent::Movie ? scriptMovieEvent(g, e.fx, e.weapon) : nullptr;
    if (!j || (*j)[0].s() != "AnimateDetail") return;
    std::string code = (*j)[3].s();
    for (Terrain::Object &o : g.terrain.objects)
        if (code.size() >= 4 && !strncmp(o.code.c_str(), code.c_str(), 4)) o.playFrom = clock;
}

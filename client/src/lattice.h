// W4M land frame lattice (LandFramePseudoEntity, docs/w4m/formats.md §23, docs/w4m/physics.md §11): n = XSize, YSize, ZSize; e per
// layer EdgeOffset1 (x, z), EdgeOffset2 (x, z), n[1] + 1 of them; h the HeightMap per lattice column x + (n[0] + 1) z, empty when zero.
#pragma once
#include "raylib.h"
#include <cmath>
#include <functional>
#include <vector>

namespace lattice {

inline float hmAt(const std::vector<float> &h, const int n[3], int cx, int cz, float u, float w) {
    if (h.empty()) return 0;
    const float *c = &h[cz * (n[0] + 1) + cx];
    return (c[0] * (1 - u) + c[1] * u) * (1 - w) + (c[n[0] + 2] * u + c[n[0] + 1] * (1 - u)) * w;
}

// 0x468200: a frame-local point (lattice units) into lattice space: x, z through the EdgeOffset lerp of layer trunc(y), then y less
// the bilinear HeightMap of the column x, z fall in
inline Vector3 toLattice(const int n[3], const std::vector<Vector4> &e, const std::vector<float> &h, Vector3 p) {
    const int j = std::min(std::max((int)p.y, 0), n[1] - 1);
    const float t = p.y - j, s = 1 - t;
    const Vector4 a = e[j], b = e[j + 1];
    const float e1x = a.x * s + b.x * t, e1z = a.y * s + b.y * t, e2x = a.z * s + b.z * t, e2z = a.w * s + b.w * t;
    p.x = (p.x - e1x) / ((e2x - e1x) / n[0] + 1), p.z = (p.z - e1z) / ((e2z - e1z) / n[2] + 1);
    if (p.x >= 0 && p.z >= 0 && p.x < n[0] && p.z < n[2]) {
        const int cx = (int)p.x, cz = (int)p.z;
        p.y -= hmAt(h, n, cx, cz, p.x - cx, p.z - cz);
    }
    return p;
}

// 0x46a070 on a ray starting in land at lattice point q (in the frame): of q's cell's six faces, the least fraction of q to it whose
// neighbour cell solid() denies (ties: -x +x -y +y -z +z); *nrm its normal in frame-local axes, unnormalised: three points of the
// face, at q's two in-face coordinates and 0.1 cell off them, mapped back through the EdgeOffsets and the HeightMap of q's column.
// false: every neighbour is land.
inline bool startFace(const int n[3], const std::vector<Vector4> &e, const std::vector<float> &h, Vector3 q,
                      const std::function<bool(int, int, int)> &solid, Vector3 *nrm) {
    const int cx = (int)floorf(q.x), cz = (int)floorf(q.z), cy = (int)floorf(q.y);
    const float u = q.x - cx, w = q.z - cz, v = q.y - cy;
    const int j = std::min(std::max((int)(q.y + hmAt(h, n, cx, cz, u, w)), 0), n[1] - 1);  // the layer of the lifted y
    static const int OFF[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    const float frac[6] = {u, 1 - u, v, 1 - v, w, 1 - w};
    int face = -1;
    for (int f = 0; f < 6; f++) {
        const float fr = frac[f] > 0 && solid(cx + OFF[f][0], cy + OFF[f][1], cz + OFF[f][2]) ? -1 : frac[f];
        if (fr >= 0 && (face < 0 || fr < frac[face])) face = f;
    }
    if (face < 0) return false;
    // the three points in the neighbour cell's fractions (x, y, z): P0, P1, P2; wrap: P0.y brought into its layer
    Vector3 P[3];
    bool wrap = true;
    switch (face) {
        case 0: P[0] = {1, v, w}, P[1] = {1, v, w - 0.1f}, P[2] = {1, v - 0.1f, w}; break;
        case 1: P[0] = {0, v, w}, P[1] = {0, v, w + 0.1f}, P[2] = {0, v - 0.1f, w}; break;
        case 2: P[0] = {u, 1, w}, P[1] = {u - 0.1f, 1, w}, P[2] = {u, 1, w - 0.1f}, wrap = false; break;
        case 3: P[0] = {u, 0, w}, P[1] = {u + 0.1f, 0, w}, P[2] = {u, 0, w - 0.1f}, wrap = false; break;
        case 4: P[0] = {u, v, 1}, P[1] = {u + 0.1f, v, 1}, P[2] = {u, v - 0.1f, 1}; break;
        default: P[0] = {u, v, 0}, P[1] = {u - 0.1f, v, 0}, P[2] = {u, v - 0.1f, 0}; break;
    }
    for (Vector3 &p : P) p.y += hmAt(h, n, cx, cz, p.x, p.z);  // q's column's heights, at the neighbour's fractions
    if (wrap) {
        while (P[0].y >= 1) for (Vector3 &p : P) p.y -= 1;
        while (P[0].y < 0) for (Vector3 &p : P) p.y += 1;
    }
    const float nx = (float)(cx + OFF[face][0]), nz = (float)(cz + OFF[face][2]);
    Vector3 W[3];
    for (int k = 0; k < 3; k++) {
        const float t = P[k].y, s = 1 - t;
        const Vector4 a = e[j], b = e[j + 1];
        const float e1x = a.x * s + b.x * t, e1z = a.y * s + b.y * t, e2x = a.z * s + b.z * t, e2z = a.w * s + b.w * t;
        W[k] = {(P[k].x + nx) * (e2x - e1x) / n[0] + e1x + P[k].x + nx, t, (P[k].z + nz) * (e2z - e1z) / n[2] + e1z + P[k].z + nz};
    }
    const Vector3 d1 = {W[1].x - W[0].x, W[1].y - W[0].y, W[1].z - W[0].z}, d2 = {W[2].x - W[0].x, W[2].y - W[0].y, W[2].z - W[0].z};
    *nrm = {d1.y * d2.z - d1.z * d2.y, d1.z * d2.x - d1.x * d2.z, d1.x * d2.y - d1.y * d2.x};
    return true;
}

}  // namespace lattice

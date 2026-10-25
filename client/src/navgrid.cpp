#include "navgrid.h"
#include "sim.h"
#include <algorithm>

Grid makeGrid(const Terrain &t) {
    Grid gr;
    std::vector<Vector4> bs = t.blocks;
    if (bs.empty()) bs.push_back({0, 0, Terrain::NX * Terrain::VOX, Terrain::NZ * Terrain::VOX});
    float area = 0;  // m², the 1/20 unit scale cancels out
    gr.ox = gr.oz = 1e9f;
    for (const Vector4 &b : bs) area += (b.z - b.x) * (b.w - b.y), gr.ox = fminf(gr.ox, b.x), gr.oz = fminf(gr.oz, b.y);
    gr.s = fmaxf(sqrtf(area / 16000), 0.5f);  // 0x4b2ba6: at least 10 units
    for (const Vector4 &b : bs) {
        auto at = [&](float v, float o) { return (int)((v - o) / gr.s + 0.5f); };
        gr.boxes.push_back({b, at(b.x, gr.ox), at(b.y, gr.oz), at(b.z, gr.ox) + 1, at(b.w, gr.oz) + 1});
    }
    return gr;
}

static int colTop(const Terrain &t, NodeCache &c, int x, int z) {
    if (x < 0 || z < 0 || x >= Terrain::NX || z >= Terrain::NZ) return -1;
    int16_t &top = c.tops[(size_t)z * Terrain::NX + x];
    if (top == -2) {
        top = -1;
        for (int y = Terrain::NY - 1; y >= 0 && top < 0; y--, c.reads++) if (t.d[Terrain::idx(x, y, z)] > 0) top = (int16_t)y;
    }
    return top;
}

// W4M's land ray 0x466ae0 straight down from (x, y, z): Terrain::SUB samples to the first solid one, then 5 bisections; the hit is the
// last empty point (0x468ae3: within 1/32 step of the surface), the start if it is in land, <= the water if none
float landBelow(const Terrain &t, float water, float x, float z, float y) {
    const float step = Terrain::SUB, start = y;
    while (y > water && !t.solid({x, y, z})) y -= step;
    if (y <= water || y == start) return y;
    float air = y + step;
    for (int k = 0; k < 5; k++) {
        const float m = (y + air) / 2;
        (t.solid({x, m, z}) ? y : air) = m;
    }
    return air;
}

// One of W4M 0x4ade10's rays. Layer 0 starts at the last step over every solid voxel `sample` can read there (same samples, fewer of
// them); layer 1 first leaves the land it starts in.
float rayDown(const Terrain &t, float water, NodeCache &c, float x, float z, float y, bool under) {
    const float step = Terrain::SUB;
    if (under) while (y > water && t.solid({x, y, z})) y -= step;
    else {
        const int ix = (int)floorf(x * Terrain::IVOX), iz = (int)floorf(z * Terrain::IVOX);
        const int top = std::max(std::max(colTop(t, c, ix, iz), colTop(t, c, ix + 1, iz)), std::max(colTop(t, c, ix, iz + 1), colTop(t, c, ix + 1, iz + 1)));
        const float s = (top + 1) * Terrain::VOX;
        if (y >= s) y -= floorf((y - s) / step) * step;
    }
    return landBelow(t, water, x, z, y);
}

// 0x4ade10's per-ray steepness test (0x4ae004): the face the ray enters at `hit` (the last empty point) under cos SlideAngle of its frame's material
static bool steep(const Terrain &t, Vector3 hit, uint64_t pot) {
    float th;
    Vector3 n;
    if (!t.cast(hit, {0, -1, 0}, Terrain::SUB * 2, &th, &n)) return false;
    return n.y <= slideCos(pot, t.slippy({hit.x, hit.y - th - 1e-3f, hit.z}));
}

// W4M 0x4aed70: layer 0 probed from the land's top, layer 1 from 20 units under layer 0's lowest hit (water if layer 0 is);
// 3 x 3 rays at ±spacing/3 (0x4ade10), lo / hi = lowest / highest hit, any miss or water hit: water; a steep ray: flag 2
NodeH nodeH(const Terrain &t, float water, const Grid &gr, NodeCache &c, int i, int j, int layer, uint64_t pot) {
    const int64_t key = nodeKey(i, j, layer);
    if (auto it = c.heights.find(key); it != c.heights.end()) return it->second;
    NodeH h{1, water, water};
    float from = (Terrain::NY - 1) * Terrain::VOX;
    bool ok = gr.has(i, j);
    if (ok && layer) {
        const NodeH up = nodeH(t, water, gr, c, i, j, 0, pot);
        from = up.lo - 1, ok = up.flag != 1;
    }
    if (ok) {
        const Vector2 p = gr.at(i, j);
        float lo = 1e9f, hi = -1e9f;
        uint8_t flag = 0;
        for (int a = -1; a <= 1 && ok; a++)
            for (int b = -1; b <= 1 && ok; b++) {
                const float y = rayDown(t, water, c, p.x + a * gr.s / 3, p.y + b * gr.s / 3, from, layer > 0);
                if (y <= water) ok = false;
                else if (steep(t, {p.x + a * gr.s / 3, y, p.y + b * gr.s / 3}, pot)) flag = 2;
                lo = fminf(lo, y), hi = fmaxf(hi, y);
            }
        if (ok) h = {(uint8_t)(hi - lo > 1 ? 3 : flag), lo, hi};
    }
    return c.heights[key] = h;
}

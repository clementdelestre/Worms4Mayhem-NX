#pragma once
#include "terrain.h"
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// W4M AISceneGraphService node grid (PopulatePathingNodes 0x4b2800), shared by the AI and the start placement (docs/w4m/ai.md §node grid).
// One lattice anchored at the boxes' min corner, spacing max(10 units, sqrt(Σ box areas / 16000)); a box holds the nodes
// i0 <= i < i1, i0 = trunc((x0 - ox) / s + 0.5), i1 = trunc((x1 - ox) / s + 0.5) + 1 (0x4b1e00); boxes: Terrain::blocks.
struct Grid {
    struct Box { Vector4 b; int i0, j0, i1, j1; };
    float s = 0.5f, ox = 0, oz = 0;
    std::vector<Box> boxes;
    struct Entry { bool ok = false; float hi = -1e4f, lo = 1e4f; };  // m, relative to the start: arc heights met at that offset
    int n[2] = {0, 0};
    std::vector<Entry> reach[2];  // W4M m_fJumpNodes per jump type (forward, backflip), n x n by |dx|, |dz| (AI only)
    int ci(float x) const { return (int)floorf((x - ox) / s + 0.5f); }
    int cj(float z) const { return (int)floorf((z - oz) / s + 0.5f); }
    Vector2 at(int i, int j) const { return {ox + i * s, oz + j * s}; }
    bool has(int i, int j) const {  // 0x4ae9d0
        for (const Box &b : boxes) if (i >= b.i0 && i < b.i1 && j >= b.j0 && j < b.j1) return true;
        return false;
    }
    int64_t key(Vector3 p) const { return ((int64_t)ci(p.x) * 100003 + cj(p.z)) * 1009 + (int)floorf(p.y) + 100; }  // + ours: 1 m layer
};
Grid makeGrid(const Terrain &t);

// A node's heights (W4M 0x4aed70): flag 0 a node, 1 water or outside the grid (heights: the water level), 3 blocked (over 20 units apart)
struct NodeH { uint8_t flag = 1; float lo = 0, hi = 0; };
struct NodeCache {
    std::unordered_map<int64_t, NodeH> heights;
    std::vector<int16_t> tops = std::vector<int16_t>((size_t)Terrain::NX * Terrain::NZ, -2);  // per voxel column: its highest solid voxel, -2 not read yet
    long reads = 0;  // raw voxels read for them: work the samples don't count
};
inline int64_t nodeKey(int i, int j, int layer) { return (((int64_t)i + (1 << 20)) * (1 << 21) + (j + (1 << 20))) * 2 + layer; }
NodeH nodeH(const Terrain &t, float water, const Grid &gr, NodeCache &c, int i, int j, int layer);

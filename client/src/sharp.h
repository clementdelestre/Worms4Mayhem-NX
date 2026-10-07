// Exact land per 0.25 m cell (docs/sim.md "Exact land"): each cell a surface crosses keeps an ordered list of the primitives
// reaching it (W4M poxel cells as planes, the heightmap, then carve spheres and weld boxes); other cells are wholly in or out.
#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>

struct SharpLand {
    // an op: kind (top 3 bits) | id; HEX: a W4M cell (hexP0), the next word its mask of planes cutting the cell; SPHERE / BOX: index
    enum Op : uint32_t { HEX = 0, HM = 1u << 29, FULL = 2u << 29, SPHERE = 3u << 29, BOX = 4u << 29 };
    static constexpr uint32_t KIND = 7u << 29, ID = (1u << 29) - 1;
    // Terrain::samples charged per eval and per first() (its crossings; its evals count apart), in field() reads of the same time
    static constexpr unsigned EVAL_COST = 4, FIRST_COST = 6, CELL_COST = 1;
    bool on = false;
    std::vector<Vector4> planes;  // n, d: n.p - d <= 0 inside
    std::vector<uint32_t> hexP0;  // per W4M cell (HEX id): its first plane
    std::vector<float> top;       // heightmap top per grid column (m), NaN none; empty without a heightmap
    std::vector<Vector4> sph;     // carve spheres: c, r
    std::vector<Vector3> box;     // weld boxes: c, half pairs
    std::vector<uint64_t> bits;   // per cell: holds a list
    std::vector<uint32_t> key, val, pool;  // open addressing cell + 1 -> pool offset of: word count, ops (cells may share one)
    size_t used = 0, dead = 0;    // keyed slots; pool words no cell points to

    size_t bytes() const {
        return planes.capacity() * sizeof(Vector4) + sph.capacity() * sizeof(Vector4) + box.capacity() * sizeof(Vector3) + top.capacity() * 4 +
               (hexP0.capacity() + key.capacity() + val.capacity() + pool.capacity()) * 4 + bits.capacity() * 8;
    }
    bool load(const std::string &path);  // .cells "W4C1" (docs/w4m/formats.md "Exact land")
    bool mixed(size_t c) const { return on && (bits[c >> 6] >> (c & 63) & 1); }
    const uint32_t *ops(size_t c) const;  // a mixed cell's list: [0] = word count, then the ops
    // density (> 0 land, m, clamped +-0.5) at p in mixed cell c; *nrm: the deciding primitive's outward normal, 0 when none;
    // given a ray's *dir, a W4M cell's normal is the face that ray came in by (W4M 0x46a070), not its nearest
    float eval(Vector3 p, size_t c, Vector3 *nrm, const Vector3 *dir = nullptr) const;
    // the first t in [t0, t1] where a + t dir is land (land = false: out of land) in mixed cell c; -1 when none;
    // grid: a + [t0, t1] dir is an edge of the 0.25 m grid (the heightmap's crossings then solved, not bisected)
    float first(Vector3 a, Vector3 dir, float t0, float t1, size_t c, bool land = true, bool grid = false) const;
    // the surface crossing on the segment a-b inside mixed cell c (a and b on opposite sides) and its outward normal
    bool edge(size_t c, Vector3 a, Vector3 b, Vector3 *q, Vector3 *n) const;
    // edits, before the int8 field changes (its signs say which unlisted cells are land)
    void carve(const std::vector<signed char> &d, Vector3 c, float r);
    void weld(const std::vector<signed char> &d, Vector3 c, Vector3 half);
    // a convex cell (corners bit 1 +x, 2 +y, 4 +z) laid over the land, as the importer's (starts an exact land); its HEX id
    uint32_t add(const std::vector<signed char> &d, const Vector3 *c);
    void drop(const std::vector<uint32_t> &hexes);  // those W4M cells leave every list (Land.ClearCoded)
    bool inside(uint32_t hex, Vector3 p) const;

private:
    uint32_t cell(const Vector3 *c, int64_t flags);
    float hm(float x, float z) const;
    Vector3 hmNormal(float x, float z) const;
    void setOps(size_t c, const uint32_t *o, uint32_t n);  // n == 0: the cell leaves the lists
    void compact();
    void crossings(Vector3 a, Vector3 dir, float t0, float t1, size_t c, std::vector<float> &out, bool grid) const;
};

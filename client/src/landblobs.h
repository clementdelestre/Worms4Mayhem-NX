// W4M LandBlobBlastEntity: the pool of land chunks a destroyed land cell throws (docs/w4m/formats.md "Land debris"). Render only: the pieces
// never touch the sim, the checksum or the replay log.
#pragma once
#include "raylib.h"
#include <cstdint>

struct LandBlobs {
    static constexpr int POOL = 300;  // 0x4654a0: 300 pieces, a spawn with none free is dropped (0x465ea0)
    struct Piece {
        bool on = false;
        uint8_t model = 0, mat = 0, slot = 0;  // Blob_(model + 1); the land material and the texture slot (floor, wall, roof) it wears
        Vector3 pos{}, vel{};                  // m, m/ms
        Matrix rot = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, spin = rot;  // spin: multiplied in at every step
    };
    Piece p[POOL];
    int freed[POOL], nfree = POOL;
    uint32_t rng = 0x9e3779b9;  // the graphical LCG's stand-in (0x96d040): never the sim's

    LandBlobs() { clear(); }
    void clear();
    // 0x465ea0: at (m), v (m/ms), the land material and slot of the cell, building: BuildingBlob_* instead of LandBlob_*
    void spawn(Vector3 at, Vector3 v, int mat, int slot, bool building);
    // 0x464fc0, once per game clock step of ms: Position += v ms, v.y += g ms, a piece under floorY is freed; gravity in m/ms²
    void step(float ms, float gravity, float floorY);
    // the pieces on screen: draw(piece) for each, in the W4M model's own units (0.05 m per unit) under their spin
    template <class F> void each(F f) const { for (const Piece &q : p) if (q.on) f(q); }

private:
    uint32_t next() { return rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5; }
    float unit() { return (next() & 0xffff) / 65536.0f; }  // 0x68c07b: low 16 bits / 65536
};

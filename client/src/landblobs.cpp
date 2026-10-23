#include "landblobs.h"
#include "raymath.h"

void LandBlobs::clear() {
    for (int i = 0; i < POOL; i++) p[i] = Piece{}, freed[i] = i;
    nfree = POOL;
}

void LandBlobs::spawn(Vector3 at, Vector3 v, int mat, int slot, bool building) {
    if (!nfree) return;
    Piece &q = p[freed[--nfree]];
    q = Piece{};
    q.on = true, q.model = (uint8_t)(next() % 3 + (building ? 3 : 0)), q.mat = (uint8_t)mat, q.slot = (uint8_t)slot;
    const Vector3 axis = {unit(), unit(), unit() + 0.001f};  // 0x465f6a: a random axis, then a turn of up to 0.4 rad at every step
    q.spin = MatrixRotate(Vector3Normalize(axis), unit() * 0.4f);
    q.pos = at, q.vel = v;
}

void LandBlobs::step(float ms, float gravity, float floorY) {
    if (ms <= 0) return;
    for (int i = 0; i < POOL; i++) {
        Piece &q = p[i];
        if (!q.on) continue;
        q.rot = MatrixMultiply(q.rot, q.spin);
        q.pos = Vector3Add(q.pos, Vector3Scale(q.vel, ms));
        q.vel.y += gravity * ms;
        if (q.pos.y < floorY) q.on = false, freed[nfree++] = i;
    }
}

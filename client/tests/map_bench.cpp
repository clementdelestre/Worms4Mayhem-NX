// Map storage timing (not pass/fail), run from client/: map_bench <maps...> (load, memory, mesh, sample, carve + remesh);
// map_bench all: per map a hash of the density, materials, exact land and thin cells in x, y, z order (format changes keep them)
#include "../src/terrain.h"
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct H {
    uint64_t h = 1469598103934665603ull;
    void add(const void *p, size_t n) { for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull; }
    template <class T> void v(T x) { add(&x, sizeof x); }
};
static void xyzOf(size_t i, int &x, int &y, int &z) { Terrain::xyz(i, x, y, z); }

static void hashes(const Terrain &t, uint64_t out[4]) {
    H d, m, s, th;
    for (int z = 0; z < Terrain::NZ; z++)
        for (int y = 0; y < Terrain::NY; y++)
            for (int x = 0; x < Terrain::NX; x++) {
                size_t i = Terrain::idx(x, y, z);
                d.v(t.d[i]);
                if (!t.mats.empty()) m.v(t.mats[i]);
                if (x < Terrain::NX - 1 && y < Terrain::NY - 1 && z < Terrain::NZ - 1 && t.sharp.mixed(i)) {
                    const uint32_t *o = t.sharp.ops(i);
                    s.v(x), s.v(y), s.v(z), s.add(o, (o[0] + 1) * 4);
                }
            }
    s.add(t.sharp.planes.data(), t.sharp.planes.size() * 16), s.add(t.sharp.hexP0.data(), t.sharp.hexP0.size() * 4), s.add(t.sharp.top.data(), t.sharp.top.size() * 4);
    for (const auto &c : t.thin)
        for (const Terrain::Thin &e : c) { int x, y, z; xyzOf(e.vox, x, y, z); th.v(x), th.v(y), th.v(z), th.v(e.mat), th.add(e.c, 96); }
    std::vector<long> only;  // linear order, whatever the index layout
    for (int v : t.thinOnly) { int x, y, z; xyzOf(v, x, y, z); only.push_back(((long)z * Terrain::NY + y) * Terrain::NX + x); }
    std::sort(only.begin(), only.end());
    for (long v : only) th.v((int)(v % Terrain::NX)), th.v((int)(v / Terrain::NX % Terrain::NY)), th.v((int)(v / Terrain::NX / Terrain::NY));
    out[0] = d.h, out[1] = m.h, out[2] = s.h, out[3] = th.h;
}

int main(int argc, char **argv) {
    std::vector<std::string> maps;
    bool all = argc > 1 && !strcmp(argv[1], "all");
    if (all) {
        FilePathList fl = LoadDirectoryFilesEx("assets/maps", ".json", false);
        for (unsigned i = 0; i < fl.count; i++) maps.push_back(GetFileNameWithoutExt(fl.paths[i]));
        UnloadDirectoryFiles(fl);
        std::sort(maps.begin(), maps.end());
    } else for (int i = 1; i < argc; i++) maps.push_back(argv[i]);
    SetTraceLogLevel(LOG_WARNING);
    if (!all) SetConfigFlags(FLAG_WINDOW_HIDDEN), InitWindow(320, 240, "bench");
    Terrain t;
    for (const std::string &map : maps) {
        double t0 = now();
        if (!t.load(map, 0)) continue;
        double load = now() - t0;
        uint64_t h[4];
        hashes(t, h);
        printf("HASH %s %016llx %016llx %016llx %016llx\n", map.c_str(), (unsigned long long)h[0], (unsigned long long)h[1], (unsigned long long)h[2], (unsigned long long)h[3]);
        if (all) continue;
        double vox = t.d.bytes() + t.mats.bytes() + t.steel.bytes();
        printf("%s grid %dx%dx%d: load %.0f ms (vox %.0f, cells %.0f); voxels %.1f MB, cells %.1f MB\n", map.c_str(), Terrain::NX, Terrain::NY, Terrain::NZ,
               load, t.loadMs[0], t.loadMs[1], vox / 1048576, t.sharp.bytes() / 1048576.0);
        t0 = now();
        t.remesh(0);
        double cols = now() - t0;
        t0 = now();
        t.meshInitial(1e30);
        printf("  remesh(0) %.0f ms, meshInitial %.0f ms, terrain bytes %.1f MB\n", cols, now() - t0, t.bytes() / 1048576.0);
        // surface points: columns' first land from above
        std::mt19937 rng(7);
        std::vector<Vector3> surf;
        for (int k = 0; surf.size() < 64 && k < 100000; k++) {
            float x = std::uniform_real_distribution<float>(0, Terrain::NX * Terrain::VOX)(rng), z = std::uniform_real_distribution<float>(0, Terrain::NZ * Terrain::VOX)(rng);
            float tt;
            if (t.cast({x, Terrain::NY * Terrain::VOX - 1, z}, {0, -1, 0}, Terrain::NY * Terrain::VOX - 2, &tt, nullptr)) surf.push_back({x, Terrain::NY * Terrain::VOX - 1 - tt, z});
        }
        std::vector<Vector3> pts;
        std::uniform_real_distribution<float> u(-2, 2);
        for (int k = 0; k < 2000000; k++) { Vector3 s = surf[k % surf.size()]; pts.push_back({s.x + u(rng), s.y + u(rng), s.z + u(rng)}); }
        float acc = 0;
        t0 = now();
        for (int r = 0; r < 2; r++) for (const Vector3 &p : pts) acc += t.sample(p);
        double ns = (now() - t0) * 1e6 / (2 * pts.size());
        t0 = now();
        float acc2 = 0;
        for (const Vector3 &p : pts) acc2 += t.field(p);
        printf("  sample %.1f ns, field %.1f ns (sum %.3f %.3f)\n", ns, (now() - t0) * 1e6 / pts.size(), acc, acc2);
        double carve = 0, rem = 0;
        int chunks = 0;
        for (int k = 0; k < 16; k++) {
            t0 = now();
            t.carve(surf[k], 2.5f);
            carve += now() - t0, t0 = now();
            chunks += t.remesh();
            rem += now() - t0;
        }
        printf("  16 carves r2.5: carve %.2f ms each, remesh %.1f ms each (%d chunks)\n", carve / 16, rem / 16, chunks);
        printf("  after carves: voxels %.1f MB\n", (t.d.bytes() + t.mats.bytes() + t.steel.bytes()) / 1048576.0);
    }
}

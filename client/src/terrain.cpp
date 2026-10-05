#include "terrain.h"
#include "fx.h"
#include "json.h"
#include "lit.h"
#include "models.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

static constexpr int CX = Terrain::NX / Terrain::CS, CY = Terrain::NY / Terrain::CS, CZ = Terrain::NZ / Terrain::CS;
static constexpr size_t TOTAL = (size_t)Terrain::NX * Terrain::NY * Terrain::NZ;
static size_t idx(int x, int y, int z) { return ((size_t)z * Terrain::NY + y) * Terrain::NX + x; }
static constexpr unsigned char HARD = 62;  // girder voxels: W4M theme material 61 (GirderSmall.xom), 1-based like mats

// Quantize keeping the sign exact: solid iff v > 0.
static signed char qd(float v) {
    int q = (int)lrintf(Clamp(v, -0.5f, 0.5f) * Terrain::Q);
    return (signed char)(v > 0 ? std::max(q, 1) : std::min(q, 0));
}

static float hash3(int x, int y, int z, unsigned seed) {
    unsigned h = seed ^ (unsigned)x * 374761393u ^ (unsigned)y * 668265263u ^ (unsigned)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) / 32767.5f - 1.0f;
}

static float noise3(float x, float y, float z, unsigned s) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
    float r = 0;
    for (int n = 0; n < 8; n++) {
        int a = n & 1, b = (n >> 1) & 1, c = n >> 2;
        r += hash3(ix + a, iy + b, iz + c, s) * (a ? fx : 1 - fx) * (b ? fy : 1 - fy) * (c ? fz : 1 - fz);
    }
    return r;
}

static float fbm(float x, float y, float z, unsigned s) {
    float r = 0, amp = 1;
    for (int o = 0; o < 4; o++, x *= 2, y *= 2, z *= 2, amp *= 0.5f) r += amp * noise3(x, y, z, s + o);
    return r;
}

static float sdBox(Vector3 p, Vector3 c, Vector3 half) {
    Vector3 q = {fabsf(p.x - c.x) - half.x, fabsf(p.y - c.y) - half.y, fabsf(p.z - c.z) - half.z};
    return Vector3Length(Vector3Max(q, Vector3Zero())) + fminf(fmaxf(q.x, fmaxf(q.y, q.z)), 0);
}

// Palettes loosely after the W4M worlds: grass/top, rock/side, beach, sky.
static const struct { const char *name; Color top, side, beach, sky; } THEMES[] = {
    {"jurassic", {96, 150, 50, 255}, {115, 85, 60, 255}, {200, 180, 120, 255}, {150, 195, 205, 255}},
    {"camelot", {90, 160, 70, 255}, {128, 124, 116, 255}, {190, 175, 130, 255}, {130, 170, 230, 255}},
    {"arabian", {222, 190, 120, 255}, {190, 140, 82, 255}, {232, 212, 160, 255}, {245, 205, 145, 255}},
    {"wildwest", {196, 150, 88, 255}, {170, 86, 50, 255}, {215, 182, 130, 255}, {250, 190, 125, 255}},
    {"construction", {150, 150, 145, 255}, {205, 150, 40, 255}, {180, 170, 150, 255}, {170, 190, 210, 255}},
    // Worms 3D-era themes, used by imported maps (which bring their own palette)
    {"arctic", {235, 240, 250, 255}, {150, 170, 190, 255}, {210, 220, 230, 255}, {200, 220, 240, 255}},
    {"england", {90, 150, 60, 255}, {120, 100, 80, 255}, {190, 175, 130, 255}, {150, 190, 230, 255}},
    {"horror", {80, 90, 60, 255}, {90, 80, 90, 255}, {120, 110, 100, 255}, {70, 60, 90, 255}},
    {"lunar", {170, 170, 175, 255}, {110, 110, 120, 255}, {150, 150, 150, 255}, {20, 20, 40, 255}},
    {"pirate", {100, 170, 70, 255}, {150, 120, 80, 255}, {220, 200, 140, 255}, {140, 200, 240, 255}},
    {"war", {110, 120, 70, 255}, {120, 105, 85, 255}, {170, 160, 130, 255}, {170, 170, 150, 255}},
};

static Vector3 vec(const Json &j, Vector3 def) { return {j[0].f(def.x), j[1].f(def.y), j[2].f(def.z)}; }

enum ShapeType { BOX, RAMP, PYRAMID, SPHERE, TORUS, CYLINDER, CONE, SHAPE_TYPES };
static const char *SHAPE_NAMES[] = {"box", "ramp", "pyramid", "sphere", "torus", "cylinder", "cone"};
struct Prefab {
    int type;
    Vector3 size, ext;  // ext: local half extents of the bounding box, centred on pos
    float r, h, t, top;
};

// Signed distance (<0 inside) in the prefab's local frame.
static float sdf(const Prefab &s, Vector3 p) {
    Vector3 b = s.ext;
    float qx = sqrtf(p.x * p.x + p.z * p.z), hh = s.h / 2;
    switch (s.type) {
    case RAMP:  // rises towards local +x
        return fmaxf(sdBox(p, {0, 0, 0}, b), (s.size.x * (p.y + b.y) - s.size.y * (p.x + b.x)) / sqrtf(s.size.x * s.size.x + s.size.y * s.size.y));
    case PYRAMID: {  // planes bound, not exact: fine for the sign, slightly soft corners
        float yb = p.y + b.y, H = s.size.y;
        float fx = (H * fabsf(p.x) + b.x * yb - b.x * H) / sqrtf(H * H + b.x * b.x);
        float fz = (H * fabsf(p.z) + b.z * yb - b.z * H) / sqrtf(H * H + b.z * b.z);
        return fmaxf(fmaxf(fx, fz), -yb);
    }
    case SPHERE: return Vector3Length(p) - s.r;
    case TORUS: {  // ring standing in the local XY plane: half-buried it is an arch
        float q = sqrtf(p.x * p.x + p.y * p.y) - s.r;
        return sqrtf(q * q + p.z * p.z) - s.t;
    }
    case CYLINDER: {
        float dx = qx - s.r, dy = fabsf(p.y) - hh;
        return fminf(fmaxf(dx, dy), 0) + sqrtf(fmaxf(dx, 0) * fmaxf(dx, 0) + fmaxf(dy, 0) * fmaxf(dy, 0));
    }
    case CONE: {  // capped cone (iq)
        Vector2 q = {qx, p.y}, k1 = {s.top, hh}, k2 = {s.top - s.r, s.h};
        Vector2 ca = {q.x - fminf(q.x, q.y < 0 ? s.r : s.top), fabsf(q.y) - hh};
        float t = Clamp(Vector2DotProduct(Vector2Subtract(k1, q), k2) / Vector2DotProduct(k2, k2), 0, 1);
        Vector2 cb = Vector2Add(Vector2Subtract(q, k1), Vector2Scale(k2, t));
        return (cb.x < 0 && ca.y < 0 ? -1 : 1) * sqrtf(fminf(Vector2DotProduct(ca, ca), Vector2DotProduct(cb, cb)));
    }
    default: return sdBox(p, {0, 0, 0}, b);
    }
}

static bool readMap(const std::string &map, Json &j, std::string &dirOut) {
#ifdef __SWITCH__
    const char *dirs[] = {"sdmc:/switch/worms4nx/assets/maps/", "romfs:/maps/"};
#else
    const char *dirs[] = {"./assets/maps/", "./romfs/maps/"};
#endif
    for (const char *dir : dirs) {
        std::string path = dir + map + ".json";
        if (!FileExists(path.c_str())) continue;
        char *txt = LoadFileText(path.c_str());
        bool ok = txt && Json::parse(txt, j) && j.type == Json::Obj;
        UnloadFileText(txt);
        if (ok) return dirOut = dir, true;
        TraceLog(LOG_WARNING, "map %s: invalid JSON", path.c_str());
    }
    return false;
}

// Calls f(position, density) for every voxel whose centre lies in [lo, hi].
template <class F> static void paint(Terrain &t, Vector3 lo, Vector3 hi, F f) {
    const float V = Terrain::VOX;
    for (int z = std::max(0, (int)ceilf(lo.z / V)); z <= std::min(Terrain::NZ - 1, (int)(hi.z / V)); z++)
        for (int y = std::max(0, (int)ceilf(lo.y / V)); y <= std::min(Terrain::NY - 1, (int)(hi.y / V)); y++)
            for (int x = std::max(0, (int)ceilf(lo.x / V)); x <= std::min(Terrain::NX - 1, (int)(hi.x / V)); x++)
                f(Vector3{x * V, y * V, z * V}, t.d[idx(x, y, z)]);
}

void Terrain::reset(signed char fill) {
    unload();
    d.assign(TOTAL, fill);
    steel.clear();
    parts.assign(CX * CY * CZ, {});
    dirty.assign(CX * CY * CZ, true);
    colTop.clear();
}

// Noisy hill, evaluated on a 2x coarser grid (the noise is smooth) and trilinearly upsampled: 8x fewer fbm calls.
void Terrain::island(float bh, float height, float rough, float rad, unsigned s) {
    constexpr int K = 2, X = NX / K + 1, Y = NY / K + 1, Z = NZ / K + 1;
    const float cx = NX * VOX / 2, cz = NZ * VOX / 2, step = K * VOX, bound = 1.875f * 0.75f * rough + 0.5f;
    std::vector<float> c((size_t)X * Y * Z), hc((size_t)X * Z);
    for (int z = 0; z < Z; z++)
        for (int x = 0; x < X; x++) {
            float px = x * step, pz = z * step, r = sqrtf((px - cx) * (px - cx) + (pz - cz) * (pz - cz)) / rad;
            float h = hc[z * X + x] = bh + height * (1 - r * r) + (rough > 0 ? rough * fbm(px * 0.04f, 0, pz * 0.04f, s) : 0);
            for (int y = 0; y < Y; y++) {
                float v = h - y * step;
                // 3D noise is bounded by 1.875 * 0.75 * rough: skip it where the clamp hides it anyway
                if (rough > 0 && fabsf(v) < bound) v += 0.75f * rough * fbm(px * 0.08f, y * step * 0.08f, pz * 0.08f, s + 99);
                c[((size_t)z * Y + y) * X + x] = v;
            }
        }
    // W4M heightmap land block (0x464618): from the first to the last cell with height > 0, cell origins
    int x0 = X, z0 = Z, x1 = -1, z1 = -1;
    for (int z = 0; z < Z; z++)
        for (int x = 0; x < X; x++) if (hc[z * X + x] > 0) x0 = std::min(x0, x), z0 = std::min(z0, z), x1 = std::max(x1, x), z1 = std::max(z1, z);
    if (x1 >= 0) addBlock({x0 * step, z0 * step, x1 * step, z1 * step});
    // d starts at air; cells entirely below/above the noise band of their 4 columns are solid/air without sampling
    for (int z = 0; z < Z - 1; z++)
        for (int y = 0; y < Y - 1; y++)
            for (int x = 0; x < X - 1; x++) {
                const float *h = &hc[z * X + x];
                float hmin = fminf(fminf(h[0], h[1]), fminf(h[X], h[X + 1])), hmax = fmaxf(fmaxf(h[0], h[1]), fmaxf(h[X], h[X + 1]));
                if (y * step >= hmax + bound) continue;
                bool full = (y + 1) * step <= hmin - bound;
                float v[8];
                for (int n = 0; n < 8 && !full; n++) v[n] = c[((size_t)(z + (n >> 2)) * Y + y + ((n >> 1) & 1)) * X + x + (n & 1)];
                for (int n = 0; n < 8; n++) {
                    float fx = (n & 1) * 0.5f, fy = ((n >> 1) & 1) * 0.5f, fz = (n >> 2) * 0.5f;
                    float a = v[0] + (v[1] - v[0]) * fx, b = v[2] + (v[3] - v[2]) * fx, e = v[4] + (v[5] - v[4]) * fx, f = v[6] + (v[7] - v[6]) * fx;
                    d[idx(x * K + (n & 1), y * K + ((n >> 1) & 1), z * K + (n >> 2))] = full ? 127 : qd((a + (b - a) * fy) + ((e + (f - e) * fy) - (a + (b - a) * fy)) * fz);
                }
            }
}

bool Terrain::load(const std::string &map, unsigned seed) {
    objects.clear(), objModels.clear(), markers.clear(), blocks.clear();
    thin.assign(CX * CY * CZ, {}), thinOnly.clear();
    hasFinish = false;
    theme.clear(), time = "day", mats.clear(), palTop.clear(), palSide.clear(), texFiles.clear(), texRepeat.clear();
    top = {86, 150, 60, 255}, side = {130, 95, 60, 255}, beach = {194, 178, 128, 255}, sky = {120, 170, 230, 255};
    Json j;
    std::string dir;
    if (map.empty() || !readMap(map, j, dir)) {
        generate(seed);
        return map.empty();
    }
    for (auto &t : THEMES)
        if (j["theme"].s() == t.name) top = t.top, side = t.side, beach = t.beach, sky = t.sky;
    theme = j["theme"].s();
    time = j["time"].s("day");
    Lit::sun = {};
    if (const Json &l = j["light"]; l.type == Json::Obj) {
        Lit::sun.dir = vec(l["dir"], Lit::sun.dir), Lit::sun.ambient = vec(l["ambient"], Lit::sun.ambient);
        Lit::sun.diffuse = vec(l["diffuse"], Lit::sun.diffuse), Lit::sun.specular = vec(l["specular"], Lit::sun.specular);
        if (l["water"].size() == 14)
            for (int i = 0; i < 14; i++) Lit::sun.water[i] = l["water"][i].f();
    }
    const Json &pal = j["palette"], &tex = j["textures"];
    for (size_t i = 0; i < pal.size(); i++) {
        const Json &c = pal[i];
        palTop.push_back({(unsigned char)c[0].f(), (unsigned char)c[1].f(), (unsigned char)c[2].f(), 255});
        palSide.push_back({(unsigned char)c[3].f(c[0].f()), (unsigned char)c[4].f(c[1].f()), (unsigned char)c[5].f(c[2].f()), 255});
    }
    for (size_t i = 0; i < tex.size(); i++) {
        for (int k : {0, 1, 4, 5}) texFiles.push_back(tex[i][k].type == Json::Str ? dir + tex[i][k].s() : "");
        texRepeat.push_back({tex[i][2].f(4), tex[i][3].f(4)});
    }
    scale = j["scale"].f(1);

    reset(-127);
    const Json &base = j["base"];
    std::string kind = base["type"].s("island");
    float bh = base["base"].f(6), height = base["height"].f(10), rough = base["roughness"].f(4);
    float rad = base["radius"].f(0.8f) * NX * VOX / 2, cx = NX * VOX / 2, cz = NZ * VOX / 2;
    if (j["voxels"].type == Json::Str) {
        if (!loadVoxels(dir + j["voxels"].s())) TraceLog(LOG_WARNING, "map %s: bad voxel file '%s'", map.c_str(), j["voxels"].s().c_str());
        if (j["thin"].type == Json::Str) loadThin(dir + j["thin"].s());
    } else if (kind != "none") {
        bool isl = kind == "island";
        island(bh, isl ? height : 0, isl ? rough : 0, rad, seed + (unsigned)base["seed"].f(0));
    }

    const Json &shapes = j["shapes"];
    for (size_t i = 0; i < shapes.size(); i++) {
        const Json &sh = shapes[i];
        Prefab f = {SHAPE_TYPES, vec(sh["size"], {4, 4, 4}), {}, sh["radius"].f(2), sh["height"].f(4), sh["thickness"].f(1), sh["top"].f(0)};
        for (int k = 0; k < SHAPE_TYPES; k++) if (sh["type"].s() == SHAPE_NAMES[k]) f.type = k;
        float rmax = fmaxf(f.r, f.top);
        f.ext = f.type <= PYRAMID ? Vector3Scale(f.size, 0.5f) : f.type == SPHERE ? Vector3{f.r, f.r, f.r}
              : f.type == TORUS   ? Vector3{f.r + f.t, f.r + f.t, f.t} : Vector3{rmax, f.h / 2, rmax};
        if (f.type == SHAPE_TYPES) { TraceLog(LOG_WARNING, "map %s: unknown shape '%s'", map.c_str(), sh["type"].s().c_str()); continue; }
        Vector3 c = vec(sh["pos"], {cx, 8, cz});
        float yaw = sh["yaw"].f(0) * DEG2RAD, cy = cosf(yaw), sy = sinf(yaw);
        bool sub = sh["subtract"].is();
        if (!sub) {  // a land piece: its box is a W4M land frame block (LandFramePseudoEntity 0x46f6c9)
            float bx = yaw ? sqrtf(f.ext.x * f.ext.x + f.ext.z * f.ext.z) : f.ext.x, bz = yaw ? bx : f.ext.z;
            addBlock({c.x - bx, c.z - bz, c.x + bx, c.z + bz});
        }
        // AABB grown by the clamp range: voxels outside it cannot change
        float ex = (yaw ? sqrtf(f.ext.x * f.ext.x + f.ext.z * f.ext.z) : f.ext.x) + 0.5f, ez = yaw ? ex : f.ext.z + 0.5f;
        paint(*this, {c.x - ex, c.y - f.ext.y - 0.5f, c.z - ez}, {c.x + ex, c.y + f.ext.y + 0.5f, c.z + ez}, [&](Vector3 p, signed char &v) {
            float dx = p.x - c.x, dz = p.z - c.z, sd = sdf(f, {dx * cy - dz * sy, p.y - c.y, dx * sy + dz * cy});
            v = sub ? std::min(v, qd(sd)) : std::max(v, qd(-sd));
        });
    }

    if (j["finish"].type == Json::Arr) hasFinish = true, finish = vec(j["finish"], {cx, 8, cz});
    for (const Json &m : j["markers"].arr) markers.push_back({m["name"].s(), m["type"].s(), vec(m["pos"], {cx, 8, cz})});
    for (const Json &b : j["blocks"].arr) blocks.push_back({b[0].f(), b[1].f(), b[2].f(), b[3].f()});  // imported: already merged
    mergeBlocks();
    const Json &ob = j["objects"];
    for (size_t i = 0; i < ob.size(); i++) {
        const Json &o = ob[i], &b = o["basis"];
        std::string name = o["model"].s();
        int m = (int)(std::find(objModels.begin(), objModels.end(), name) - objModels.begin());
        if (m == (int)objModels.size()) objModels.push_back(name);
        Vector3 p = vec(o["pos"], {cx, 8, cz});
        objects.push_back({m, p, {b[0].f(1), b[1].f(0), b[2].f(0), p.x, b[3].f(0), b[4].f(1), b[5].f(0), p.y, b[6].f(0), b[7].f(0), b[8].f(1), p.z, 0, 0, 0, 1}});
    }
    return true;
}

// .thin "W4T1": u32 count, then per cell u16 x y z (its voxel), u8 material (1-based), 8 corners (3 f32, m; bit 1 +x, 2 +y, 4 +z)
void Terrain::loadThin(const std::string &path) {
    int n = 0;
    unsigned char *b = LoadFileData(path.c_str(), &n);
    if (!b) return;
    uint32_t count = 0;
    if (n >= 8 && !memcmp(b, "W4T1", 4)) memcpy(&count, b + 4, 4);
    for (uint32_t i = 0; i < count && 8 + (i + 1) * 103 <= (uint32_t)n; i++) {
        const unsigned char *r = b + 8 + i * 103;
        uint16_t v[3];
        memcpy(v, r, 6);
        if (v[0] >= NX || v[1] >= NY || v[2] >= NZ) continue;
        Thin t{(int)idx(v[0], v[1], v[2]), r[6], {}};
        memcpy(t.c, r + 7, 96);
        thin[(v[2] / CS * CY + v[1] / CS) * CX + v[0] / CS].push_back(t);
    }
    UnloadFileData(b);
    // voxels standing only for thin cells (no other solid neighbour): their blob is not meshed, the hexahedra show instead
    std::vector<int> anchors;
    for (auto &ts : thin) for (const Thin &t : ts) anchors.push_back(t.vox);
    std::sort(anchors.begin(), anchors.end());
    thinOnly.clear();
    for (int v : anchors) {
        int x = v % NX, y = v / NX % NY, z = v / (NX * NY);
        bool only = true;
        for (int q = 0; q < 6 && only; q++) {
            int a = x + (q == 0) - (q == 1), c = y + (q == 2) - (q == 3), e = z + (q == 4) - (q == 5);
            if (a < 0 || c < 0 || e < 0 || a >= NX || c >= NY || e >= NZ) continue;
            int w = (int)idx(a, c, e);
            only = d[w] <= 0 || std::binary_search(anchors.begin(), anchors.end(), w);
        }
        if (only && (thinOnly.empty() || thinOnly.back() != v)) thinOnly.push_back(v);
    }
}

// .vox "W4V2": u16 NX NY NZ, u8 D; materials as (material, run 1..255) pairs in d[] order (0 = air);
// then density codes: h < 128 skips h voxels left at +-D (by material), h >= 128 gives h - 127 int8 values.
bool Terrain::loadVoxels(const std::string &path) {
    int size = 0;
    unsigned char *b = LoadFileData(path.c_str(), &size);
    bool ok = b && size >= 11 && !memcmp(b, "W4V2", 4) && (b[4] | b[5] << 8) == NX && (b[6] | b[7] << 8) == NY && (b[8] | b[9] << 8) == NZ;
    size_t n = 0;
    int i = 11;
    if (ok) {
        mats.resize(TOTAL);
        for (; i + 1 < size && n < TOTAL; i += 2) {
            size_t r = std::min<size_t>(b[i + 1], TOTAL - n);
            memset(&mats[n], b[i], r);
            n += r;
        }
        ok = n == TOTAL;
    }
    if (ok) {
        signed char D = (signed char)b[10];
        for (n = 0; n < TOTAL; n++) d[n] = mats[n] ? D : (signed char)-D;
        for (n = 0; i < size && n < TOTAL;) {
            int h = b[i++];
            if (h < 128) n += h;
            else for (h -= 127; h-- && i < size && n < TOTAL;) d[n++] = (signed char)b[i++];
        }
    }
    UnloadFileData(b);
    if (!ok) mats.clear();
    return ok;
}

// Thresholds at 20 W4M units per m: area 250 units² (0x4b24c5), merge when the gap is under 40 units on x and z (0x4ae3aa).
void Terrain::addBlock(Vector4 b) {
    if ((b.z - b.x) * (b.w - b.y) < 250.0f / 400) return;
    for (Vector4 &o : blocks)
        if (fmaxf(o.x, b.x) < fminf(o.z, b.z) + 2 && fmaxf(o.y, b.y) < fminf(o.w, b.w) + 2) {
            o = {fminf(o.x, b.x), fminf(o.y, b.y), fmaxf(o.z, b.z), fmaxf(o.w, b.w)};
            return;
        }
    blocks.push_back(b);
}

void Terrain::mergeBlocks() {
    for (size_t i = 0; i < blocks.size(); i++)
        for (size_t k = i + 1; k < blocks.size(); k++) {
            Vector4 &o = blocks[i], b = blocks[k];
            if (fmaxf(o.x, b.x) < fminf(o.z, b.z) + 2 && fmaxf(o.y, b.y) < fminf(o.w, b.w) + 2) {
                o = {fminf(o.x, b.x), fminf(o.y, b.y), fmaxf(o.z, b.z), fmaxf(o.w, b.w)};
                blocks.erase(blocks.begin() + k);
                i = (size_t)-1;  // W4M restarts the pass after every merge
                break;
            }
        }
}

void Terrain::generate(unsigned seed) {
    thin.assign(CX * CY * CZ, {}), thinOnly.clear();
    theme.clear(), mats.clear(), texFiles.clear(), texRepeat.clear(), objects.clear(), objModels.clear(), blocks.clear();
    reset(-127);
    float cx = NX * VOX / 2, cz = NZ * VOX / 2;
    island(6, 10, 4, cx * 0.8f, seed);
    addBlock({cx - 15, cz + 7, cx - 1, cz + 9}), addBlock({cx + 7.5f, cz - 8.5f, cx + 12.5f, cz - 3.5f});  // the two prefabs' boxes
    mergeBlocks();
    // ponytail: two hard-coded prefabs; real maps will place theme prefabs from a map file
    paint(*this, {cx - 8 - 7.5f, 8.5f, cz + 6.5f}, {cx - 8 + 7.5f, 19.5f, cz + 9.5f}, [&](Vector3 p, signed char &v) {
        v = std::max(v, qd(-sdBox(p, {cx - 8, 14, cz + 8}, {7, 5, 1})));
    });
    paint(*this, {cx + 7, 0, cz - 9}, {cx + 13, 24.5f, cz - 3}, [&](Vector3 p, signed char &v) {
        float tower = fmaxf(sqrtf((p.x - cx - 10) * (p.x - cx - 10) + (p.z - cz + 6) * (p.z - cz + 6)) - 2.5f, p.y - 24);
        v = std::max(v, qd(-tower));
    });
}

float Terrain::at(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= NX || y >= NY || z >= NZ) return -1;
    return d[idx(x, y, z)] * (1 / Q);
}

float Terrain::sample(Vector3 p) const {
    samples++;
    float x = p.x / VOX, y = p.y / VOX, z = p.z / VOX;
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - ix, fy = y - iy, fz = z - iz, r = 0;
    const float wx[2] = {1 - fx, fx}, wy[2] = {1 - fy, fy}, wz[2] = {1 - fz, fz};
    if (ix >= 0 && iy >= 0 && iz >= 0 && ix < NX - 1 && iy < NY - 1 && iz < NZ - 1) {  // the AI's hot path: no bounds checks, same sum order
        const signed char *q = &d[idx(ix, iy, iz)];
        for (int n = 0; n < 8; n++) {
            int a = n & 1, b = (n >> 1) & 1, c = n >> 2;
            r += q[a + b * NX + c * NX * NY] * (1 / Q) * wx[a] * wy[b] * wz[c];
        }
        return r;
    }
    for (int n = 0; n < 8; n++) {
        int a = n & 1, b = (n >> 1) & 1, c = n >> 2;
        r += at(ix + a, iy + b, iz + c) * wx[a] * wy[b] * wz[c];
    }
    return r;
}

Vector3 Terrain::normal(Vector3 p, float e) const {
    Vector3 g = {sample({p.x + e, p.y, p.z}) - sample({p.x - e, p.y, p.z}),
                 sample({p.x, p.y + e, p.z}) - sample({p.x, p.y - e, p.z}),
                 sample({p.x, p.y, p.z + e}) - sample({p.x, p.y, p.z - e})};
    if (e < VOX && Vector3LengthSqr(g) <= 1e-8f) return normal(p);  // deep in land or out of the band: the field is flat there
    return Vector3Normalize(Vector3Negate(g));
}

bool Terrain::carve(Vector3 c, float radius) {
    int lo[3], hi[3];
    float cc[3] = {c.x, c.y, c.z}, dim[3] = {NX, NY, NZ};
    for (int a = 0; a < 3; a++) {
        lo[a] = std::max(0, (int)((cc[a] - radius) / VOX) - 1);
        hi[a] = std::min((int)dim[a] - 1, (int)((cc[a] + radius) / VOX) + 1);
    }
    bool changed = false;
    for (int z = lo[2]; z <= hi[2]; z++)
        for (int y = lo[1]; y <= hi[1]; y++)
            for (int x = lo[0]; x <= hi[0]; x++) {
                size_t i = idx(x, y, z);
                signed char nv = std::min(d[i], qd(Vector3Distance({x * VOX, y * VOX, z * VOX}, c) - radius));
                if (nv == d[i]) continue;
                if (undo) undo->emplace_back((int)i, d[i]);
                d[i] = nv, changed = true;
            }
    // decor goes with the blast, or with the ground it stood on (sampled 0.3 m below its base, along its up axis)
    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object &o) {
        float dist = Vector3Distance(o.pos, c);
        if (dist > radius + 2) return false;
        Vector3 up = Vector3Normalize({o.m.m4, o.m.m5, o.m.m6});
        return dist < radius + 0.2f || !solid(Vector3Subtract(o.pos, Vector3Scale(up, 0.3f)));
    }), objects.end());
    // chunk cells sample one voxel past their bounds, so neighbours of the box are dirty too
    for (int z = std::max(0, lo[2] - 1) / CS; z <= std::min(NZ - 1, hi[2] + 1) / CS; z++)
        for (int y = std::max(0, lo[1] - 1) / CS; y <= std::min(NY - 1, hi[1] + 1) / CS; y++)
            for (int x = std::max(0, lo[0] - 1) / CS; x <= std::min(NX - 1, hi[0] + 1) / CS; x++)
                dirty[(z * CY + y) * CX + x] = true;
    edits += changed;
    return changed;
}

void Terrain::weld(Vector3 c, Vector3 half) {
    if (steel.empty()) steel.assign(TOTAL, false);
    edits++;
    float reach = Vector3Length(half) + 0.5f;
    int lo[3], hi[3];
    float cc[3] = {c.x, c.y, c.z}, dim[3] = {NX, NY, NZ};
    for (int a = 0; a < 3; a++) lo[a] = std::max(0, (int)((cc[a] - reach) / VOX)), hi[a] = std::min((int)dim[a] - 1, (int)((cc[a] + reach) / VOX) + 1);
    for (int z = lo[2]; z <= hi[2]; z++)
        for (int y = lo[1]; y <= hi[1]; y++)
            for (int x = lo[0]; x <= hi[0]; x++) {
                Vector3 l = {x * VOX - c.x, y * VOX - c.y, z * VOX - c.z};
                float sd = sdBox(l, {0, 0, 0}, half);
                size_t i = idx(x, y, z);
                signed char nv = std::max(d[i], qd(-sd));
                if (undo && nv != d[i]) undo->emplace_back((int)i, d[i]);
                d[i] = nv;
                if (sd < 0 && !steel[i]) {
                    if (undo) undo->emplace_back(-1 - (int)i, 0);
                    steel[i] = true;
                }
            }
    for (int z = std::max(0, lo[2] - 1) / CS; z <= std::min(NZ - 1, hi[2] + 1) / CS; z++)
        for (int y = std::max(0, lo[1] - 1) / CS; y <= std::min(NY - 1, hi[1] + 1) / CS; y++)
            for (int x = std::max(0, lo[0] - 1) / CS; x <= std::min(NX - 1, hi[0] + 1) / CS; x++) dirty[(z * CY + y) * CX + x] = true;
}

bool Terrain::raycast(Ray r, float maxDist, Vector3 *hit) const {
    // march only inside the grid (+1 voxel, sample's reach): a far or infinite ray would otherwise spin for seconds
    const float step = VOX * 0.5f, o[3] = {r.position.x, r.position.y, r.position.z}, dv[3] = {r.direction.x, r.direction.y, r.direction.z},
                hi[3] = {(NX + 1) * VOX, (NY + 1) * VOX, (NZ + 1) * VOX};
    float t0 = 0, t1 = maxDist;
    for (int i = 0; i < 3; i++) {
        if (dv[i] == 0) { if (o[i] < -VOX || o[i] > hi[i]) return false; continue; }
        float a = (-VOX - o[i]) / dv[i], b = (hi[i] - o[i]) / dv[i];
        t0 = fmaxf(t0, fminf(a, b)), t1 = fminf(t1, fmaxf(a, b));
    }
    for (float t = ceilf(t0 / step) * step; t < maxDist && t <= t1; t += step) {
        Vector3 p = Vector3Add(r.position, Vector3Scale(r.direction, t));
        if (solid(p)) { *hit = p; return true; }
    }
    return false;
}

// W4M GLG_PC 0x451ea0: LightGradient[N.L capped by the sun ray] + SideGradient[normal x] - 128 (GLG_Shadow 0x454e30, 0x451590)
// ponytail: a carve only remeshes nearby chunks, so shadows cast by blown-away ground elsewhere stay until remeshed
Color Terrain::vertexColour(Vector3 p, Vector3 n) const {
    static const Vector3 L = Vector3Normalize({0, 0.26f, -0.78f});  // Sun.LowLightVector: every theme (0x4e7ed0)
    if (!hasGrad) return WHITE;
    auto solidAt = [&](Vector3 q) {
        int x = (int)(q.x * (1 / VOX) + 0.5f), y = (int)(q.y * (1 / VOX) + 0.5f), z = (int)(q.z * (1 / VOX) + 0.5f);
        return x >= 0 && y >= 0 && z >= 0 && x < NX && z < NZ && y < colTop[z * NX + x] && d[idx(x, y, z)] > 0;
    };
    int b0 = (int)(Vector3DotProduct(n, L) * 127 + 128), b3 = (int)(n.x * 127 + 128);
    // first hit from 10 units out along L, 999 units max (20 units per m): cap 105 + 0.15 per unit
    Vector3 q = Vector3Add(p, Vector3Scale(L, 0.5f));
    float t = 0, step = 0.25f;  // steps grow 8%: ~37 lookups reach 50 m
    for (; b0 > 105 && t < 49.95f && q.y < colTop.back() * VOX; t += step, q = Vector3Add(q, Vector3Scale(L, step)), step *= 1.08f)
        if (solidAt(q)) { b0 = std::min(b0, 105 + (int)(t * 20 * 0.15f)); break; }
    Color m = grad[0][std::clamp(b0, 0, 255) >> 3], sd = grad[1][std::clamp(b3, 0, 255) >> 3];
    auto ch = [](int a, int b) { return (unsigned char)std::clamp(a + b - 128, 0, 255); };
    return {ch(m.r, sd.r), ch(m.g, sd.g), ch(m.b, sd.b), 255};
}

// W4M GLG_PC 0x451ba0: both 256x1 gradients sampled every 8 pixels
void Terrain::loadGradients() {
    hasGrad = false;
    for (int k = 0; k < 2; k++) {
        std::string f = Fx::gradientFile(theme, time, k);
        if (!FileExists(f.c_str())) return;
        Image im = LoadImage(f.c_str());
        for (int i = 0; i < 32; i++) grad[k][i] = GetImageColor(im, std::min(8 * i, im.width - 1), 0);
        UnloadImage(im);
    }
    hasGrad = true;
}

void Terrain::buildChunk(int ci) {
    int x0 = ci % CX * CS, y0 = ci / CX % CY * CS, z0 = ci / (CX * CY) * CS;
    constexpr int S = CS + 1, L = CS + 2;  // cells x0-1 .. x0+CS-1, their corners x0-1 .. x0+CS
    for (Part &p : parts[ci]) UnloadMesh(p.mesh);
    parts[ci].clear();
    // most chunks are all air or all solid: a raw sign scan is much cheaper than the full fill below
    bool any[2] = {x0 == 0 || y0 == 0 || z0 == 0 || x0 + CS >= NX || y0 + CS >= NY || z0 + CS >= NZ, false};
    for (int z = std::max(z0 - 1, 0); z <= std::min(z0 + CS, NZ - 1) && !(any[0] && any[1]); z++)
        for (int y = std::max(y0 - 1, 0); y <= std::min(y0 + CS, NY - 1); y++) {
            const signed char *r = &d[idx(0, y, z)];
            for (int x = std::max(x0 - 1, 0); x <= std::min(x0 + CS, NX - 1); x++) any[r[x] > 0] = true;
        }
    if (!any[0] || !any[1]) return;
    static float c[L * L * L];
    static unsigned char mt[L * L * L];
    static uint64_t rows[L * L];  // bit i: local voxel i of the row is solid
    for (int k = 0, n = 0; k < L; k++)
        for (int j = 0; j < L; j++) {
            uint64_t &r = rows[k * L + j];
            r = 0;
            for (int i = 0; i < L; i++, n++) {
                int x = x0 - 1 + i, y = y0 - 1 + j, z = z0 - 1 + k;
                bool in = x >= 0 && y >= 0 && z >= 0 && x < NX && y < NY && z < NZ;
                c[n] = in ? d[idx(x, y, z)] * (1 / Q) : -1;
                mt[n] = !in ? 0 : isSteel(idx(x, y, z)) ? HARD : !mats.empty() ? mats[idx(x, y, z)] : 0;
                r |= (uint64_t)(c[n] > 0) << i;
            }
        }

    static Vector3 cp[S * S * S], cn[S * S * S];
    static bool has[S * S * S];
    memset(has, 0, sizeof has);
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    auto C = [&](int i, int j, int k) { return c[(k * L + j) * L + i]; };
    for (int k = 0; k < S; k++)
        for (int j = 0; j < S; j++) {
            const uint64_t *r = &rows[k * L + j];
            uint64_t o = r[0] | r[1] | r[L] | r[L + 1], an = r[0] & r[1] & r[L] & r[L + 1];
            for (uint64_t bits = (o | o >> 1) & ~(an & an >> 1) & ((1ull << S) - 1); bits; bits &= bits - 1) {
                int i = __builtin_ctzll(bits), n = (k * S + j) * S + i;
                float v[8];
                for (int q = 0; q < 8; q++) v[q] = C(i + (q & 1), j + ((q >> 1) & 1), k + (q >> 2));
                has[n] = true;
                Vector3 p = {0, 0, 0};
                int cnt = 0;
                for (auto &e : E) {
                    if ((v[e[0]] > 0) == (v[e[1]] > 0)) continue;
                    float t = v[e[0]] / (v[e[0]] - v[e[1]]);
                    Vector3 a = {float(e[0] & 1), float((e[0] >> 1) & 1), float(e[0] >> 2)};
                    Vector3 b = {float(e[1] & 1), float((e[1] >> 1) & 1), float(e[1] >> 2)};
                    p = Vector3Add(p, Vector3Lerp(a, b, t));
                    cnt++;
                }
                cp[n] = Vector3Scale(Vector3Add({float(x0 - 1 + i), float(y0 - 1 + j), float(z0 - 1 + k)}, Vector3Scale(p, 1.0f / cnt)), VOX);
                Vector3 g = {(v[1] - v[0]) + (v[3] - v[2]) + (v[5] - v[4]) + (v[7] - v[6]),
                             (v[2] - v[0]) + (v[3] - v[1]) + (v[6] - v[4]) + (v[7] - v[5]),
                             (v[4] - v[0]) + (v[5] - v[1]) + (v[6] - v[2]) + (v[7] - v[3])};
                cn[n] = Vector3Normalize(Vector3Negate(g));
                {  // smoother: the gradient over two voxels at the vertex (fewer facets); the AI's sample count stays as it was
                    unsigned long s0 = samples;
                    Vector3 q = cp[n];
                    const float h = VOX;
                    Vector3 g2 = {sample({q.x + h, q.y, q.z}) - sample({q.x - h, q.y, q.z}), sample({q.x, q.y + h, q.z}) - sample({q.x, q.y - h, q.z}),
                                  sample({q.x, q.y, q.z + h}) - sample({q.x, q.y, q.z - h})};
                    samples = s0;
                    if (Vector3Length(g2) > 1e-6f) cn[n] = Vector3Normalize(Vector3Negate(g2));
                }
            }
        }

    // quads go to the mesh of the material of their solid voxel; vertices are shared within that mesh
    struct Builder { int mat; std::vector<int> vid; std::vector<float> pos, nrm; std::vector<unsigned char> col; std::vector<unsigned short> idx; };
    static std::vector<Builder> bs;
    size_t used = 0;
    const Lit::Light &sun = Lit::sun;
    const Vector3 light = Vector3Normalize(sun.dir);
    auto colour = [&](int m, Vector3 p, Vector3 nr) -> Color {
        Color vc = m >= 64 ? WHITE : vertexColour(p, nr);  // heightmap land: HeightMapFragmentMain ignores the vertex rgb
        if (m >= 0 && m < (int)texMats.size() && texMats[m].maps) return {vc.r, vc.g, vc.b, 255};  // textured: the shader lights it, times this colour
        Color base = m >= 0 && m < (int)palTop.size() ? (nr.y > 0.7f ? palTop[m] : palSide[m]) : m == HARD - 1 ? Color{150, 150, 160, 255}  // untextured steel
                   : p.y < WATER + 0.8f ? beach : nr.y > 0.7f ? top : side;
        Vector3 l = Vector3Add(sun.ambient, Vector3Scale(sun.diffuse, fmaxf(0, Vector3DotProduct(nr, light))));
        auto ch = [&](unsigned char c, float k, unsigned char v) { return (unsigned char)fminf(255, c * k * v / 255); };
        return {ch(base.r, l.x, vc.r), ch(base.g, l.y, vc.g), ch(base.b, l.z, vc.b), 255};
    };
    auto vertex = [&](Builder &b, int n) {
        if (b.vid[n] >= 0) return b.vid[n];
        Vector3 p = cp[n], nr = cn[n];
        Color c = colour(b.mat - 1, p, nr);
        b.pos.insert(b.pos.end(), {p.x, p.y, p.z});
        b.nrm.insert(b.nrm.end(), {nr.x, nr.y, nr.z});
        b.col.insert(b.col.end(), {c.r, c.g, c.b, 255});
        return b.vid[n] = (int)b.pos.size() / 3 - 1;
    };
    auto builder = [&](int m) -> Builder & {
        size_t bi = 0;
        while (bi < used && bs[bi].mat != m) bi++;
        if (bi == used) {
            if (used == bs.size()) bs.emplace_back();
            Builder &b = bs[used++];
            b.mat = m, b.vid.assign(S * S * S, -1), b.pos.clear(), b.nrm.clear(), b.col.clear(), b.idx.clear();
        }
        return bs[bi];
    };
    std::vector<int> skip;  // thinOnly voxels of this chunk's sample box
    for (auto it = std::lower_bound(thinOnly.begin(), thinOnly.end(), (int)idx(0, 0, std::max(z0 - 1, 0))); it != thinOnly.end() && *it / (NX * NY) <= z0 + CS; ++it) {
        int x = *it % NX, y = *it / NX % NY;
        if (x >= x0 - 1 && x <= x0 + CS && y >= y0 - 1 && y <= y0 + CS) skip.push_back(*it);
    }
    for (int k = 1; k < S; k++)
        for (int j = 1; j < S; j++) {
            uint64_t r = rows[k * L + j], ch[3] = {r ^ r >> 1, r ^ rows[k * L + j + 1], r ^ rows[(k + 1) * L + j]};
            for (int a = 0; a < 3; a++)
                for (uint64_t bits = ch[a] & ((1ull << S) - 2); bits; bits &= bits - 1) {
                    int i = __builtin_ctzll(bits), o[3] = {i, j, k}, u = (a + 1) % 3, w = (a + 2) % 3, cell[4];
                    bool in = r >> i & 1;
                    for (int q = 0; q < 4; q++) {
                        int t[3] = {o[0], o[1], o[2]};
                        t[u] -= (q == 1 || q == 2), t[w] -= (q >= 2);
                        cell[q] = (t[2] * S + t[1]) * S + t[0];
                    }
                    if (!has[cell[0]] || !has[cell[1]] || !has[cell[2]] || !has[cell[3]]) continue;
                    int m = in ? mt[(k * L + j) * L + i] : mt[((k + (a == 2)) * L + j + (a == 1)) * L + i + (a == 0)];
                    if (!skip.empty()) {
                        int sv = in ? (int)idx(x0 - 1 + i, y0 - 1 + j, z0 - 1 + k) : (int)idx(x0 - 1 + i + (a == 0), y0 - 1 + j + (a == 1), z0 - 1 + k + (a == 2));
                        if (std::binary_search(skip.begin(), skip.end(), sv)) continue;
                    }
                    Builder &b = builder(m);
                    unsigned short v[4];
                    for (int q = 0; q < 4; q++) v[q] = (unsigned short)vertex(b, cell[q]);
                    if (in) b.idx.insert(b.idx.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
                    else b.idx.insert(b.idx.end(), {v[0], v[3], v[2], v[0], v[2], v[1]});
                }
        }

    // sub-voxel W4M cells (ropes, twigs): their own hexahedron, flat shaded, while their voxel stands [ours: voxel-forced]
    if (ci < (int)thin.size())
        for (const Thin &t : thin[ci]) {
            if (d[t.vox] <= 0) continue;
            Builder &b = builder(t.mat);
            Vector3 cen = {0, 0, 0};
            for (const Vector3 &q : t.c) cen = Vector3Add(cen, Vector3Scale(q, 0.125f));
            static const int F[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
            for (const auto &f : F) {
                Vector3 nr = Vector3CrossProduct(Vector3Subtract(t.c[f[2]], t.c[f[0]]), Vector3Subtract(t.c[f[3]], t.c[f[1]]));
                if (Vector3Length(nr) < 1e-8f) continue;
                bool flip = Vector3DotProduct(nr, Vector3Subtract(Vector3Scale(Vector3Add(t.c[f[0]], t.c[f[2]]), 0.5f), cen)) < 0;
                nr = Vector3Normalize(flip ? Vector3Negate(nr) : nr);
                Color c = colour(t.mat - 1, cen, nr);
                unsigned short n0 = (unsigned short)(b.pos.size() / 3);
                for (int q = 0; q < 4; q++) {
                    const Vector3 &p = t.c[f[flip ? 3 - q : q]];
                    b.pos.insert(b.pos.end(), {p.x, p.y, p.z}), b.nrm.insert(b.nrm.end(), {nr.x, nr.y, nr.z}), b.col.insert(b.col.end(), {c.r, c.g, c.b, 255});
                }
                b.idx.insert(b.idx.end(), {n0, (unsigned short)(n0 + 1), (unsigned short)(n0 + 2), n0, (unsigned short)(n0 + 2), (unsigned short)(n0 + 3)});
            }
        }

    // W4M GLG_FringeBuilder (0x449ba0): a card hangs from each open floor edge whose material has a fringe texture,
    // from the edge's land vertices along (out 0.7, down 0.7) x Land.FringeLength (0.4 land voxels, LOCAL.XOM)
    struct FB { int mat; std::vector<float> pos, uv; std::vector<unsigned char> col; std::vector<unsigned short> idx; };
    static std::vector<FB> fbs;
    size_t fused = 0;
    if (!fringeMats.empty()) {
        const float L = 0.4f * scale;
        const int drop = std::max(1, (int)ceilf(scale / VOX - 0.01f));  // the floor ends where a whole W4M voxel is open
        auto air = [&](int x, int y, int z) { return x < 0 || y < 0 || z < 0 || x >= NX || y >= NY || z >= NZ || d[idx(x, y, z)] <= 0; };
        auto open = [&](int x, int y, int z) { for (int t = 0; t < drop; t++) if (!air(x, y - t, z)) return false; return true; };
        static const int DIR[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int k = 1; k <= CS; k++)
            for (int j = 1; j <= CS; j++)
                for (int i = 1; i <= CS; i++) {
                    int gx = x0 - 1 + i, gy = y0 - 1 + j, gz = z0 - 1 + k;
                    if (gy + 1 >= NY || air(gx, gy, gz) || !air(gx, gy + 1, gz) || isSteel(idx(gx, gy, gz))) continue;
                    int m = mats.empty() ? -1 : mats[idx(gx, gy, gz)] - 1;
                    if (m < 0 || m >= (int)fringeMats.size() || !fringeMats[m].maps) continue;
                    for (int s = 0; s < 4; s++) {
                        int dx = DIR[s][0], dz = DIR[s][1], px = dz != 0, pz = dx != 0;  // (px, pz): along the edge
                        if (!open(gx + dx, gy, gz + dz)) continue;
                        Vector3 v[2], tip[2];
                        Color col[2];
                        bool ok = true;
                        for (int e = 0; e < 2 && ok; e++) {
                            int ci = i - 1 + (dx > 0 ? 1 : dx < 0 ? 0 : e), ck = k - 1 + (dz > 0 ? 1 : dz < 0 ? 0 : e), c = (ck * S + j) * S + ci;
                            if (!(ok = has[c])) break;
                            float sp = open(gx + (2 * e - 1) * px, gy, gz + (2 * e - 1) * pz) ? 0.2f * (2 * e - 1) : 0;  // corners splay
                            v[e] = cp[c], col[e] = vertexColour(cp[c], cn[c]);
                            tip[e] = Vector3Add(v[e], Vector3Scale({0.7f * dx + sp * px, -0.7f, 0.7f * dz + sp * pz}, L));
                        }
                        if (!ok) continue;
                        size_t fi = 0;
                        while (fi < fused && fbs[fi].mat != m) fi++;
                        if (fi == fused) {
                            if (fused == fbs.size()) fbs.emplace_back();
                            FB &f = fbs[fused++];
                            f.mat = m, f.pos.clear(), f.uv.clear(), f.col.clear(), f.idx.clear();
                        }
                        FB &f = fbs[fi];
                        // one 8-cell atlas strip per W4M voxel of edge: split where the edge crosses a voxel boundary [ours: voxel-forced]
                        float s0 = (px ? v[0].x : v[0].z) / scale, s1 = (px ? v[1].x : v[1].z) / scale;
                        float cut[3] = {s0, floorf(s1) > floorf(s0) && floorf(s1) > s0 ? floorf(s1) : s1, s1};
                        unsigned line = (unsigned)(px ? gz * 2 + (dz > 0) : gx * 2 + (dx > 0)) * 977u + (unsigned)gy * 131u;
                        for (int q = 0; q < 2; q++) {
                            float a = cut[q], b = cut[q + 1];
                            if (b - a < 1e-5f) continue;
                            float base = floorf(a + 1e-5f), ta = (a - s0) / (s1 - s0), tb = (b - s0) / (s1 - s0);
                            int cell = (int)((hash3((int)base, (int)line, s, 0x51f7u) + 1) * 4) & 7;
                            float u0 = 0.5f * (cell & 1), v0 = 0.25f * (cell >> 1), ua = u0 + (a - base) * 0.49f, ub = u0 + std::min(b - base, 1.0f) * 0.49f;
                            unsigned short n0 = (unsigned short)(f.pos.size() / 3);
                            for (int w = 0; w < 4; w++) {
                                float t = w == 0 || w == 1 ? ta : tb;
                                Vector3 p = Vector3Lerp(w == 0 || w == 3 ? v[0] : tip[0], w == 0 || w == 3 ? v[1] : tip[1], t);
                                Color cl = ColorLerp(col[0], col[1], t);
                                f.pos.insert(f.pos.end(), {p.x, p.y, p.z});
                                f.uv.insert(f.uv.end(), {w == 0 || w == 1 ? ua : ub, v0 + (w == 0 || w == 3 ? 0.24f : 0.01f)});
                                f.col.insert(f.col.end(), {cl.r, cl.g, cl.b, 255});
                            }
                            f.idx.insert(f.idx.end(), {n0, (unsigned short)(n0 + 1), (unsigned short)(n0 + 2), n0, (unsigned short)(n0 + 2), (unsigned short)(n0 + 3)});
                        }
                    }
                }
    }

    for (size_t fi = 0; fi < fused; fi++) {
        FB &f = fbs[fi];
        Mesh m{};
        m.vertexCount = (int)f.pos.size() / 3;
        m.triangleCount = (int)f.idx.size() / 3;
        m.vertices = f.pos.data(), m.texcoords = f.uv.data(), m.colors = f.col.data();
        m.indices = (unsigned short *)MemAlloc(f.idx.size() * sizeof(unsigned short));
        memcpy(m.indices, f.idx.data(), f.idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        m.vertices = m.texcoords = nullptr, m.colors = nullptr;
        parts[ci].push_back({f.mat + 1, m, true});
    }

    for (size_t bi = 0; bi < used; bi++) {
        Builder &b = bs[bi];
        Mesh m{};
        m.vertexCount = (int)b.pos.size() / 3;
        m.triangleCount = (int)b.idx.size() / 3;
        m.vertices = b.pos.data(), m.normals = b.nrm.data(), m.colors = b.col.data();
        m.indices = (unsigned short *)MemAlloc(b.idx.size() * sizeof(unsigned short));  // DrawMesh tests it to draw indexed
        memcpy(m.indices, b.idx.data(), b.idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        m.vertices = m.normals = nullptr, m.colors = nullptr;  // GPU copy only
        parts[ci].push_back({b.mat, m});
    }
}

// Triplanar: top texture on up-facing surfaces, side texture elsewhere. Lighting after W4M's CG/Landscape.cg,
// whose whole output is multiplied by the vertex colour (Terrain::vertexColour).
static const char *VS = R"(
attribute vec3 vertexPosition;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
uniform mat4 mvp;
varying vec3 vPos;
varying vec3 vN;
varying vec3 vC;
void main() { vPos = vertexPosition; vN = vertexNormal; vC = vertexColor.rgb; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char *FS = R"(
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;  // roof: downward faces (W4M material line 3)
uniform vec3 sunDir;
uniform vec3 ambient;
uniform vec3 diffuse;
uniform vec3 specular;
uniform vec2 scale;  // 1 / repeat: top, side
uniform vec3 camPos;
varying vec3 vPos;
varying vec3 vN;
varying vec3 vC;
void main() {
    vec3 n = normalize(vN), w = n * n * n * n;
    w /= w.x + w.y + w.z;
    vec3 p = vPos * scale.y;
    vec2 t = vPos.xz * scale.x;
    vec3 c = texture2D(texture1, vec2(p.z, -p.y)).rgb * w.x + texture2D(texture1, vec2(p.x, -p.y)).rgb * w.z
           + mix(texture2D(texture2, t).rgb, texture2D(texture0, t).rgb, step(0.0, n.y)) * w.y;
    vec3 e = camPos - vPos, v = normalize(e);
    float nv = max(dot(n, v), 0.0);
    float s = pow(max(dot(n, normalize(sunDir + v)), 0.0), 20.0);
    vec3 col = (diffuse * max(dot(n, sunDir), 0.0) + ambient) * c + specular * (0.6 * s)
             + vec3(0.2, 0.275, 0.175) * (1.0 - nv) * sqrt(1.0 - nv);
    gl_FragColor = vec4(clamp(col, 0.0, 1.0) * vC, 1.0);
}
)";

// W4M fringe: the theme texture's own XTexFont states (alpha blend, alpha test > 0, no z write, no culling, no lighting)
static const char *FVS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec4 vertexColor;
uniform mat4 mvp;
varying vec2 uv;
varying vec4 vC;
void main() { uv = vertexTexCoord; vC = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char *FFS = R"(
uniform sampler2D texture0;
varying vec2 uv;
varying vec4 vC;
void main() {
    vec4 c = texture2D(texture0, uv) * vC;
    if (c.a <= 0.0) discard;
    gl_FragColor = c;
}
)";

void Terrain::loadTextures() {
    static Shader sh{};
    if (!sh.id) sh = Lit::shader(VS, FS);
    texMats.assign(texFiles.size() / 4, Material{});
    fringeMats.assign(texFiles.size() / 4, Material{});
    if (sh.id == rlGetShaderIdDefault()) return;  // compile failed: keep the vertex-colour fallback
    scaleLoc = GetShaderLocation(sh, "scale");
    std::map<std::string, Texture2D> cache;
    auto tex = [&](const std::string &f) {
        if (f.empty() || !FileExists(f.c_str())) return Texture2D{};
        auto it = cache.find(f);
        if (it != cache.end()) return it->second;
        auto d = decoded.find(f);
        Texture2D t = d != decoded.end() ? LoadTextureFromImage(d->second) : LoadTexture(f.c_str());
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        if (Lit::aniso > 1) rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, Lit::aniso);
        textures.push_back(t);
        return cache[f] = t;
    };
    for (size_t m = 0; m < texMats.size(); m++) {
        Texture2D a = tex(texFiles[4 * m]), b = tex(texFiles[4 * m + 1]), r = tex(texFiles[4 * m + 2]), f = tex(texFiles[4 * m + 3]);
        if (f.id) {
            static Shader fsh = Lit::shader(FVS, FFS, false);
            fringeMats[m] = LoadMaterialDefault();
            fringeMats[m].shader = fsh;
            fringeMats[m].maps[MATERIAL_MAP_DIFFUSE].texture = f;
        }
        if (!a.id) a = b;
        if (!b.id) b = a;
        if (!r.id) r = b;
        if (!a.id) continue;
        texMats[m] = LoadMaterialDefault();
        texMats[m].shader = sh;
        texMats[m].maps[MATERIAL_MAP_DIFFUSE].texture = a;
        texMats[m].maps[MATERIAL_MAP_SPECULAR].texture = b;
        texMats[m].maps[MATERIAL_MAP_NORMAL].texture = r;
    }
    for (auto &[f, img] : decoded) UnloadImage(img);
    decoded.clear();
}

void Terrain::decodeTextures() {
    for (const std::string &f : texFiles)
        if (!f.empty() && !decoded.count(f) && FileExists(f.c_str())) decoded[f] = LoadImage(f.c_str());
}

void Terrain::setView(Vector3 cam) const { Lit::frame(cam); }  // the land shader is a Lit one

int Terrain::remesh(double budget) {
    if (!mat.maps) mat = LoadMaterialDefault();
    if (texMats.empty() && !texFiles.empty()) loadTextures();
    if (colTop.empty()) {
        colTop.assign(NX * NZ + 1, 0);
        for (int z = 0; z < NZ; z++)
            for (int y = 0; y < NY; y++)
                for (int x = 0; x < NX; x++)
                    if (d[idx(x, y, z)] > 0) colTop[z * NX + x] = (unsigned char)std::min(y + 1, 255), colTop.back() = std::max(colTop.back(), colTop[z * NX + x]);
        loadGradients();
    }
    // over budget, rebuilt chunks wait in `pending` and swap in together, so new and stale chunks never meet at a seam
    double end = GetTime() + budget;
    static double chunkCost = 0;  // recent dearest chunk build + upload: past the first, stop before the next one would overrun
    int built = 0;
    for (int ci = 0; ci < (int)dirty.size(); ci++) {
        if (!dirty[ci]) continue;
        double t = GetTime();
        if (t > end || (built && t + chunkCost > end)) return built;
        built++;
        std::vector<Part> old;
        std::swap(old, parts[ci]);
        buildChunk(ci);
        chunkCost = fmax(GetTime() - t, chunkCost * 0.9);
        dirty[ci] = false;
        pending.emplace_back(ci, std::move(parts[ci]));
        parts[ci] = std::move(old);
    }
    for (auto &[ci, ps] : pending) {  // a chunk rebuilt twice: the later entry wins
        for (Part &p : parts[ci]) UnloadMesh(p.mesh);
        parts[ci] = std::move(ps);
    }
    pending.clear();
    return built;
}

void Terrain::draw() const {
    static std::vector<const Part *> vis;
    vis.clear();
    for (int ci = 0; ci < (int)parts.size(); ci++) {
        if (parts[ci].empty()) continue;
        float h = CS * VOX / 2;  // chunk centre; vertices stay within a voxel of the chunk box
        if (!Models::visible({(ci % CX * CS) * VOX + h, (ci / CX % CY * CS) * VOX + h, (ci / (CX * CY) * CS) * VOX + h}, h * 1.74f + VOX)) continue;
        for (const Part &p : parts[ci]) if (!p.fringe) vis.push_back(&p);
    }
    std::stable_sort(vis.begin(), vis.end(), [](const Part *a, const Part *b) { return a->mat < b->mat; });
    // per material: DrawMesh sets the full state once; uniforms persist in the program, so the rest only bind their VAO
    for (size_t i = 0, j; i < vis.size(); i = j) {
        int m = vis[i]->mat - 1;
        bool tex = m >= 0 && m < (int)texMats.size() && texMats[m].maps;
        const Material &M = tex ? texMats[m] : mat;
        if (tex) {
            Vector2 s = {1 / texRepeat[m].x, 1 / texRepeat[m].y};
            SetShaderValue(M.shader, scaleLoc, &s, SHADER_UNIFORM_VEC2);
        }
        DrawMesh(vis[i]->mesh, M, MatrixIdentity());
        for (j = i + 1; j < vis.size() && vis[j]->mat == vis[i]->mat && !vis[j]->mesh.vaoId; j++) DrawMesh(vis[j]->mesh, M, MatrixIdentity());  // no VAO support
        if (j == vis.size() || vis[j]->mat != vis[i]->mat) continue;
        rlEnableShader(M.shader.id);
        for (int k = 0; k < 12; k++)  // raylib MAX_MATERIAL_MAPS
            if (M.maps[k].texture.id) rlActiveTextureSlot(k), rlEnableTexture(M.maps[k].texture.id);
        for (; j < vis.size() && vis[j]->mat == vis[i]->mat; j++) {
            rlEnableVertexArray(vis[j]->mesh.vaoId);
            rlDrawVertexArrayElements(0, vis[j]->mesh.triangleCount * 3, 0);
        }
        for (int k = 0; k < 12; k++)
            if (M.maps[k].texture.id) rlActiveTextureSlot(k), rlDisableTexture();
        rlDisableVertexArray();
        rlDisableShader();
    }
}

void Terrain::drawFringe() const {
    if (fringeMats.empty()) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask(), rlDisableBackfaceCulling();
    for (int ci = 0; ci < (int)parts.size(); ci++) {
        float h = CS * VOX / 2;
        if (parts[ci].empty() || !Models::visible({(ci % CX * CS) * VOX + h, (ci / CX % CY * CS) * VOX + h, (ci / (CX * CY) * CS) * VOX + h}, h * 1.74f + 1)) continue;
        for (const Part &p : parts[ci])
            if (p.fringe) DrawMesh(p.mesh, fringeMats[p.mat - 1], MatrixIdentity());
    }
    rlEnableDepthMask(), rlEnableBackfaceCulling();
}

// Decor: Lit model shading plus the part's W4M XSimpleShader states (models/decor/<name>.mat, one line per glb material)
static const char *DFS = R"(
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform vec3 ambient;
uniform vec3 diffuse;
uniform vec3 camPos;
uniform vec3 emissive;
uniform vec3 state;  // alpha test ref, lit, blended
uniform vec2 uvOff;  // texture offset clip (W4M Go / GoSync)
varying vec2 uv;
varying vec3 n;
varying vec3 wp;
void main() {
    vec4 c = texture2D(texture0, uv + uvOff) * colDiffuse;
    if (c.a < state.x) discard;
    vec3 nn = normalize(gl_FrontFacing ? n : -n), v = normalize(camPos - wp);
    float r = 1.0 - max(dot(v, nn), 0.0);
    vec3 l = (diffuse * max(dot(nn, sunDir), 0.0) + ambient + emissive) * c.rgb + (0.15 + 0.2 * diffuse) * r * r;
    gl_FragColor = vec4(state.y > 0.5 ? l : c.rgb, state.z > 0.5 ? c.a : 1.0);
}
)";

void Terrain::drawObjects(float clock, bool draw) const {
#ifdef __SWITCH__
    static const std::string dir = "sdmc:/switch/worms4nx/assets/models/decor/";
#else
    static const std::string dir = "./assets/models/decor/";
#endif
    struct State { int src = -1, dst = -1, cull = -1; float ref = 0.5f; bool zwrite = true, lit = true; Vector3 emit{}; std::vector<Vector2> uv; };
    // st: per model material (0 = raylib's default); clip: W4M 0x5cd38e plays "Go" looped from a random time, "GoSync" from 0;
    // anim: tools/w4m-models' skinned copy when the clip moves parts
    struct Entry { Model m{}; float r = 0; std::vector<State> st; std::string clip, anim; float len = 0; };
    static std::map<std::string, Entry> cache;  // loaded on first use, kept across matches
    static Shader sh{}, ash{};
    static int emitLoc = -1, stateLoc = -1, uvLoc = -1;
    if (objects.empty()) return;
    if (!sh.id) {
        sh = Lit::shader(Lit::MVS, DFS), emitLoc = GetShaderLocation(sh, "emissive"), stateLoc = GetShaderLocation(sh, "state"), uvLoc = GetShaderLocation(sh, "uvOff");
        ash = Lit::modelShader(false);
    }
    std::vector<const Entry *> ms;
    for (const std::string &name : objModels) {
        auto it = cache.find(name);
        if (it == cache.end()) {
            Entry e;
            std::string path = dir + name + ".glb";
            if (FileExists(path.c_str())) e.m = LoadModel(path.c_str());
            e.st.assign(e.m.materialCount, State{});
            if (char *t = LoadFileText((dir + name + ".mat").c_str())) {
                int k = 1, test[2], z, l, em[3], motion = 0, used = 0;
                char clip[32];
                std::vector<char *> lines;
                for (char *c = strtok(t, "\n"); c; c = strtok(nullptr, "\n")) lines.push_back(c);  // one sscanf per line
                size_t li = 0;
                if (!lines.empty() && sscanf(lines[0], "clip %31s %f %d", clip, &e.len, &motion) == 3) {
                    e.clip = clip, li = 1;
                    if (motion && Models::has(("decor/" + name + "_anim").c_str())) e.anim = "decor/" + name + "_anim";
                }
                for (char *line; li < lines.size() && (line = lines[li]) && k < e.m.materialCount; li++, k++) {
                    State &q = e.st[k];
                    if (sscanf(line, "%d %d %d %d %d %d %d %d %d %d%n", &q.src, &q.dst, &test[0], &test[1], &z, &q.cull, &l, &em[0], &em[1], &em[2], &used) != 10) break;
                    q.ref = test[0] < 0 ? 0.5f : test[0] == 7 ? 0 : test[1] / 255.0f + (test[0] == 4 ? 0.5f / 255 : 0), q.zwrite = z, q.lit = l;  // 4 Greater, 6 GreaterEqual, 7 Always
                    q.emit = {em[0] / 255.0f, em[1] / 255.0f, em[2] / 255.0f};
                    int n = 0, at = 0;
                    if (sscanf(line + used, "%d%n", &n, &at) == 1)
                        for (char *c = line + used + at; n-- > 0;) {
                            Vector2 v;
                            if (sscanf(c, "%f %f%n", &v.x, &v.y, &at) != 2) break;
                            q.uv.push_back(v), c += at;
                        }
                }
                UnloadFileText(t);
            }
            for (int k = 0; k < e.m.materialCount; k++) {
                if (sh.id != rlGetShaderIdDefault()) e.m.materials[k].shader = sh;
                Texture2D &t = e.m.materials[k].maps[MATERIAL_MAP_ALBEDO].texture;
                if (t.id != rlGetTextureIdDefault()) {
                    GenTextureMipmaps(&t), SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
                    if (Lit::aniso > 1) rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, Lit::aniso);
                }
            }
            if (e.m.meshCount) { BoundingBox b = GetModelBoundingBox(e.m); e.r = Vector3Distance(b.min, b.max) / 2; }
            it = cache.emplace(name, e).first;
        }
        ms.push_back(&it->second);
    }
    if (!draw) return;
    static const int GL[12] = {RL_ZERO, RL_ONE, RL_DST_COLOR, RL_ONE_MINUS_DST_COLOR, RL_SRC_COLOR, RL_ONE_MINUS_SRC_COLOR, RL_SRC_ALPHA,
                               RL_ONE_MINUS_SRC_ALPHA, RL_DST_ALPHA, RL_ONE_MINUS_DST_ALPHA, RL_SRC_ALPHA_SATURATE, RL_ONE};  // XBlendModeGL kBlendFactor*
    // W4M DetailObjects bin: opaque parts first, then the blended ones (XBlendModeGL other than One / Zero)
    for (int pass = 0; pass < 2; pass++) {
        for (const Object &o : objects) {
            const Entry &e = *ms[o.model];
            float size = e.r * Vector3Length({o.m.m0, o.m.m1, o.m.m2});
            if (!e.m.meshCount || !Models::visible(o.pos, 2 * size)) continue;  // W4M: frustum only (XBoundAction); origin may sit on the box edge
            float t = e.len <= 0 ? 0 : fmodf(clock + (e.clip == "Go" ? (hash3((int)(&o - objects.data()), 0, 0, 0x60u) + 1) / 2 * e.len : 0), e.len);
            if (!e.anim.empty()) {  // raw mesh units (20 per W4M unit)
                if (pass == 0) Models::shade(ash), Models::draw(e.anim.c_str(), MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), o.m), WHITE, e.clip.c_str(), t), Models::shade({});
                continue;
            }
            for (int i = 0; i < e.m.meshCount; i++) {
                const State &q = e.st[e.m.meshMaterial[i]];
                bool blended = q.src >= 0 && !(q.src == 1 && q.dst == 0);
                if (blended != (pass == 1)) continue;
                Vector3 st = {q.ref, q.lit ? 1.0f : 0.0f, blended ? 1.0f : 0.0f};
                Vector2 uv = {0, 0};
                if (q.uv.size() > 1 && e.len > 0) {
                    float f = t / e.len * (q.uv.size() - 1);
                    int k = std::min((int)f, (int)q.uv.size() - 2);
                    uv = Vector2Lerp(q.uv[k], q.uv[k + 1], f - k);
                }
                SetShaderValue(sh, emitLoc, &q.emit, SHADER_UNIFORM_VEC3), SetShaderValue(sh, stateLoc, &st, SHADER_UNIFORM_VEC3), SetShaderValue(sh, uvLoc, &uv, SHADER_UNIFORM_VEC2);
                if (q.cull == 2 || q.cull == 4) rlEnableBackfaceCulling(), rlSetCullFace(RL_CULL_FACE_BACK);
                else if (q.cull == 1 || q.cull == 3) rlEnableBackfaceCulling(), rlSetCullFace(RL_CULL_FACE_FRONT);
                else rlDisableBackfaceCulling();  // kCullModeOff, or no XCullFace [ours: two-sided]
                if (blended) {
                    rlSetBlendFactors(GL[std::clamp(q.src, 0, 11)], GL[std::clamp(q.dst, 0, 11)], RL_FUNC_ADD), rlSetBlendMode(RL_BLEND_CUSTOM);
                    if (!q.zwrite) rlDisableDepthMask();
                }
                DrawMesh(e.m.meshes[i], e.m.materials[e.m.meshMaterial[i]], o.m);
                if (blended) rlSetBlendMode(RL_BLEND_ALPHA), rlEnableDepthMask();
            }
        }
    }
    rlSetCullFace(RL_CULL_FACE_BACK), rlEnableBackfaceCulling();
}

void Terrain::unload() {
    for (auto &ps : parts)
        for (Part &p : ps) UnloadMesh(p.mesh);
    parts.clear();
    for (auto &[ci, ps] : pending)
        for (Part &p : ps) UnloadMesh(p.mesh);
    pending.clear();
    for (Texture2D &t : textures) UnloadTexture(t);
    for (Material &m : texMats) MemFree(m.maps);
    for (Material &m : fringeMats) MemFree(m.maps);
    textures.clear(), texMats.clear(), fringeMats.clear();
}

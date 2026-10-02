#include "terrain.h"
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
    spawns.clear(), objects.clear(), objModels.clear(), markers.clear();
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
    }
    const Json &pal = j["palette"], &tex = j["textures"];
    for (size_t i = 0; i < pal.size(); i++) {
        const Json &c = pal[i];
        palTop.push_back({(unsigned char)c[0].f(), (unsigned char)c[1].f(), (unsigned char)c[2].f(), 255});
        palSide.push_back({(unsigned char)c[3].f(c[0].f()), (unsigned char)c[4].f(c[1].f()), (unsigned char)c[5].f(c[2].f()), 255});
    }
    for (size_t i = 0; i < tex.size(); i++) {
        for (int k = 0; k < 2; k++) texFiles.push_back(tex[i][k].type == Json::Str ? dir + tex[i][k].s() : "");
        texRepeat.push_back({tex[i][2].f(4), tex[i][3].f(4)});
    }

    reset(-127);
    const Json &base = j["base"];
    std::string kind = base["type"].s("island");
    float bh = base["base"].f(6), height = base["height"].f(10), rough = base["roughness"].f(4);
    float rad = base["radius"].f(0.8f) * NX * VOX / 2, cx = NX * VOX / 2, cz = NZ * VOX / 2;
    if (j["voxels"].type == Json::Str) {
        if (!loadVoxels(dir + j["voxels"].s())) TraceLog(LOG_WARNING, "map %s: bad voxel file '%s'", map.c_str(), j["voxels"].s().c_str());
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
        // AABB grown by the clamp range: voxels outside it cannot change
        float ex = (yaw ? sqrtf(f.ext.x * f.ext.x + f.ext.z * f.ext.z) : f.ext.x) + 0.5f, ez = yaw ? ex : f.ext.z + 0.5f;
        paint(*this, {c.x - ex, c.y - f.ext.y - 0.5f, c.z - ez}, {c.x + ex, c.y + f.ext.y + 0.5f, c.z + ez}, [&](Vector3 p, signed char &v) {
            float dx = p.x - c.x, dz = p.z - c.z, sd = sdf(f, {dx * cy - dz * sy, p.y - c.y, dx * sy + dz * cy});
            v = sub ? std::min(v, qd(sd)) : std::max(v, qd(-sd));
        });
    }

    // spawns are dropped onto the ground below the given point
    const Json &sp = j["spawns"];
    for (size_t i = 0; i < sp.size(); i++) {
        Vector3 p = vec(sp[i], {cx, (NY - 1) * VOX, cz}), hit;
        spawns.push_back(raycast({p, {0, -1, 0}}, p.y, &hit) ? Vector3{hit.x, hit.y + 0.8f, hit.z} : p);
    }
    if (j["finish"].type == Json::Arr) hasFinish = true, finish = vec(j["finish"], {cx, 8, cz});
    for (const Json &m : j["markers"].arr) markers.push_back({m["name"].s(), m["type"].s(), vec(m["pos"], {cx, 8, cz})});
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

void Terrain::generate(unsigned seed) {
    theme.clear(), mats.clear(), texFiles.clear(), texRepeat.clear(), objects.clear(), objModels.clear();
    reset(-127);
    float cx = NX * VOX / 2, cz = NZ * VOX / 2;
    island(6, 10, 4, cx * 0.8f, seed);
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
    for (int n = 0; n < 8; n++) {
        int a = n & 1, b = (n >> 1) & 1, c = n >> 2;
        r += at(ix + a, iy + b, iz + c) * (a ? fx : 1 - fx) * (b ? fy : 1 - fy) * (c ? fz : 1 - fz);
    }
    return r;
}

Vector3 Terrain::normal(Vector3 p) const {
    const float e = VOX;
    Vector3 g = {sample({p.x + e, p.y, p.z}) - sample({p.x - e, p.y, p.z}),
                 sample({p.x, p.y + e, p.z}) - sample({p.x, p.y - e, p.z}),
                 sample({p.x, p.y, p.z + e}) - sample({p.x, p.y, p.z - e})};
    return Vector3Normalize(Vector3Negate(g));
}

void Terrain::carve(Vector3 c, float radius) {
    int lo[3], hi[3];
    float cc[3] = {c.x, c.y, c.z}, dim[3] = {NX, NY, NZ};
    for (int a = 0; a < 3; a++) {
        lo[a] = std::max(0, (int)((cc[a] - radius) / VOX) - 1);
        hi[a] = std::min((int)dim[a] - 1, (int)((cc[a] + radius) / VOX) + 1);
    }
    for (int z = lo[2]; z <= hi[2]; z++)
        for (int y = lo[1]; y <= hi[1]; y++)
            for (int x = lo[0]; x <= hi[0]; x++) {
                size_t i = idx(x, y, z);
                signed char nv = std::min(d[i], qd(Vector3Distance({x * VOX, y * VOX, z * VOX}, c) - radius));
                if (undo && nv != d[i]) undo->emplace_back((int)i, d[i]);
                d[i] = nv;
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
}

void Terrain::weld(Vector3 c, Vector3 half) {
    if (steel.empty()) steel.assign(TOTAL, false);
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
    for (float t = 0; t < maxDist; t += VOX * 0.5f) {
        Vector3 p = Vector3Add(r.position, Vector3Scale(r.direction, t));
        if (solid(p)) { *hit = p; return true; }
    }
    return false;
}

// Ambient occlusion (solid fraction of 8 points around the normal) and sun visibility (voxel ray march).
// ponytail: a carve only remeshes nearby chunks, so shadows cast by blown-away ground elsewhere stay until remeshed
void Terrain::bake(Vector3 p, Vector3 n, Vector3 l, float *ao, float *vis) const {
    auto solidAt = [&](Vector3 q) {
        int x = (int)(q.x * (1 / VOX) + 0.5f), y = (int)(q.y * (1 / VOX) + 0.5f), z = (int)(q.z * (1 / VOX) + 0.5f);
        return x >= 0 && y >= 0 && z >= 0 && x < NX && z < NZ && y < colTop[z * NX + x] && d[idx(x, y, z)] > 0;
    };
    static const float K = 0.57735f;
    static const Vector3 DIRS[8] = {{K, K, K}, {-K, K, K}, {K, -K, K}, {-K, -K, K}, {K, K, -K}, {-K, K, -K}, {K, -K, -K}, {-K, -K, -K}};
    int occ = 0;
    for (const Vector3 &k : DIRS) {
        Vector3 dir = Vector3Normalize(Vector3Add(n, Vector3Scale(k, 0.9f)));
        occ += solidAt(Vector3Add(p, Vector3Scale(dir, 0.8f)));
    }
    *ao = 1 - 0.7f * occ / 8;
    *vis = Vector3DotProduct(n, l) > 0;
    Vector3 q = Vector3Add(p, Vector3Scale(n, 0.3f)), step = Vector3Scale(l, 0.4f);
    // steps grow 12% each: ~32 lookups reach >100 m instead of 90 fixed ones (remesh cost on Switch)
    for (int i = 0; i < 32 && *vis > 0 && q.y < colTop.back() * VOX; i++, q = Vector3Add(q, step), step = Vector3Scale(step, 1.12f))
        if (solidAt(q)) *vis = 0;
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
            }
        }

    // quads go to the mesh of the material of their solid voxel; vertices are shared within that mesh
    struct Builder { int mat; std::vector<int> vid; std::vector<float> pos, nrm; std::vector<unsigned char> col; std::vector<unsigned short> idx; };
    static std::vector<Builder> bs;
    size_t used = 0;
    const Lit::Light &sun = Lit::sun;
    const Vector3 light = Vector3Normalize(sun.dir);
    auto vertex = [&](Builder &b, int n) {
        if (b.vid[n] >= 0) return b.vid[n];
        Vector3 p = cp[n], nr = cn[n];
        int m = b.mat - 1;
        float ao, vis;
        bake(p, nr, light, &ao, &vis);
        b.pos.insert(b.pos.end(), {p.x, p.y, p.z});
        b.nrm.insert(b.nrm.end(), {nr.x, nr.y, nr.z});
        if (m >= 0 && m < (int)texMats.size() && texMats[m].maps) {  // textured: the shader lights it from (ao, sun visibility)
            b.col.insert(b.col.end(), {(unsigned char)(ao * 255), (unsigned char)(vis * 255), 0, 255});
            return b.vid[n] = (int)b.pos.size() / 3 - 1;
        }
        Color base = m >= 0 && m < (int)palTop.size() ? (nr.y > 0.7f ? palTop[m] : palSide[m]) : m == HARD - 1 ? Color{150, 150, 160, 255}  // untextured steel
                   : p.y < WATER + 0.8f ? beach : nr.y > 0.7f ? top : side;
        Vector3 l = Vector3Add(sun.ambient, Vector3Scale(sun.diffuse, fmaxf(0, Vector3DotProduct(nr, light)) * vis));
        auto ch = [&](unsigned char c, float k) { return (unsigned char)fminf(255, c * k * ao); };
        b.col.insert(b.col.end(), {ch(base.r, l.x), ch(base.g, l.y), ch(base.b, l.z), 255});
        return b.vid[n] = (int)b.pos.size() / 3 - 1;
    };
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
                    size_t bi = 0;
                    while (bi < used && bs[bi].mat != m) bi++;
                    if (bi == used) {
                        if (used == bs.size()) bs.emplace_back();
                        Builder &b = bs[used++];
                        b.mat = m, b.vid.assign(S * S * S, -1), b.pos.clear(), b.nrm.clear(), b.col.clear(), b.idx.clear();
                    }
                    Builder &b = bs[bi];
                    unsigned short v[4];
                    for (int q = 0; q < 4; q++) v[q] = (unsigned short)vertex(b, cell[q]);
                    if (in) b.idx.insert(b.idx.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
                    else b.idx.insert(b.idx.end(), {v[0], v[3], v[2], v[0], v[2], v[1]});
                }
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

// Triplanar: top texture on up-facing surfaces, side texture elsewhere. Lighting after W4M's CG/Landscape.cg;
// vertex colour r = ambient occlusion, g = sun visibility (baked by Terrain::bake).
static const char *VS = R"(
attribute vec3 vertexPosition;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
uniform mat4 mvp;
varying vec3 vPos;
varying vec3 vN;
varying vec2 vL;
void main() { vPos = vertexPosition; vN = vertexNormal; vL = vertexColor.rg; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char *FS = R"(
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec3 sunDir;
uniform vec3 ambient;
uniform vec3 diffuse;
uniform vec3 specular;
uniform vec2 scale;  // 1 / repeat: top, side
uniform vec3 camPos;
uniform vec3 fogColor;
uniform vec2 fogRange;
varying vec3 vPos;
varying vec3 vN;
varying vec2 vL;
void main() {
    vec3 n = normalize(vN), w = n * n * n * n;
    w /= w.x + w.y + w.z;
    vec3 p = vPos * scale.y;
    vec2 t = vPos.xz * scale.x;
    vec3 c = texture2D(texture1, vec2(p.z, -p.y)).rgb * w.x + texture2D(texture1, vec2(p.x, -p.y)).rgb * w.z
           + mix(texture2D(texture1, p.xz).rgb, texture2D(texture0, t).rgb, step(0.0, n.y)) * w.y;
    vec3 e = camPos - vPos, v = normalize(e);
    float sh = vL.y, nv = max(dot(n, v), 0.0);
    float s = sh * pow(max(dot(n, normalize(sunDir + v)), 0.0), 20.0);
    vec3 col = (diffuse * (max(dot(n, sunDir), 0.0) * sh) + ambient) * c + specular * (0.6 * s)
             + (0.5 + 0.5 * sh) * vec3(0.2, 0.275, 0.175) * (1.0 - nv) * sqrt(1.0 - nv);
    float f = clamp((length(e) - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    gl_FragColor = vec4(mix(clamp(col, 0.0, 1.0) * vL.x, fogColor, f), 1.0);
}
)";

void Terrain::loadTextures() {
    static Shader sh{};
    if (!sh.id) sh = Lit::shader(VS, FS);
    texMats.assign(texFiles.size() / 2, Material{});
    if (sh.id == rlGetShaderIdDefault()) return;  // compile failed: keep the vertex-colour fallback
    scaleLoc = GetShaderLocation(sh, "scale");
    Vector2 noFog = {1e4f, 2e4f};
    SetShaderValue(sh, GetShaderLocation(sh, "fogRange"), &noFog, SHADER_UNIFORM_VEC2);
    std::map<std::string, Texture2D> cache;
    auto tex = [&](const std::string &f) {
        if (f.empty() || !FileExists(f.c_str())) return Texture2D{};
        auto it = cache.find(f);
        if (it != cache.end()) return it->second;
        auto d = decoded.find(f);
        Texture2D t = d != decoded.end() ? LoadTextureFromImage(d->second) : LoadTexture(f.c_str());
        GenTextureMipmaps(&t);
        SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_4X);
        textures.push_back(t);
        return cache[f] = t;
    };
    for (size_t m = 0; m < texMats.size(); m++) {
        Texture2D a = tex(texFiles[2 * m]), b = tex(texFiles[2 * m + 1]);
        if (!a.id) a = b;
        if (!b.id) b = a;
        if (!a.id) continue;
        texMats[m] = LoadMaterialDefault();
        texMats[m].shader = sh;
        texMats[m].maps[MATERIAL_MAP_DIFFUSE].texture = a;
        texMats[m].maps[MATERIAL_MAP_SPECULAR].texture = b;
    }
    for (auto &[f, img] : decoded) UnloadImage(img);
    decoded.clear();
}

void Terrain::decodeTextures() {
    for (const std::string &f : texFiles)
        if (!f.empty() && !decoded.count(f) && FileExists(f.c_str())) decoded[f] = LoadImage(f.c_str());
}

void Terrain::setFog(Vector3 cam, Color c, float start, float end) const {
    Lit::frame(cam);
    if (texMats.empty() || !texMats[0].maps) return;
    Shader sh = texMats[0].shader;  // shared by every textured material
    Vector3 fc = {c.r / 255.f, c.g / 255.f, c.b / 255.f};
    Vector2 r = {start, end};
    SetShaderValue(sh, GetShaderLocation(sh, "camPos"), &cam, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, GetShaderLocation(sh, "fogColor"), &fc, SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, GetShaderLocation(sh, "fogRange"), &r, SHADER_UNIFORM_VEC2);
}

void Terrain::remesh(double budget) {
    if (!mat.maps) mat = LoadMaterialDefault();
    if (texMats.empty() && !texFiles.empty()) loadTextures();
    if (colTop.empty()) {
        colTop.assign(NX * NZ + 1, 0);
        for (int z = 0; z < NZ; z++)
            for (int y = 0; y < NY; y++)
                for (int x = 0; x < NX; x++)
                    if (d[idx(x, y, z)] > 0) colTop[z * NX + x] = (unsigned char)std::min(y + 1, 255), colTop.back() = std::max(colTop.back(), colTop[z * NX + x]);
    }
    // over budget, rebuilt chunks wait in `pending` and swap in together, so new and stale chunks never meet at a seam
    double end = GetTime() + budget;
    for (int ci = 0; ci < (int)dirty.size(); ci++) {
        if (!dirty[ci]) continue;
        if (GetTime() > end) return;
        std::vector<Part> old;
        std::swap(old, parts[ci]);
        buildChunk(ci);
        dirty[ci] = false;
        pending.emplace_back(ci, std::move(parts[ci]));
        parts[ci] = std::move(old);
    }
    for (auto &[ci, ps] : pending) {  // a chunk rebuilt twice: the later entry wins
        for (Part &p : parts[ci]) UnloadMesh(p.mesh);
        parts[ci] = std::move(ps);
    }
    pending.clear();
}

void Terrain::draw() const {
    static std::vector<const Part *> vis;
    vis.clear();
    for (int ci = 0; ci < (int)parts.size(); ci++) {
        if (parts[ci].empty()) continue;
        float h = CS * VOX / 2;  // chunk centre; vertices stay within a voxel of the chunk box
        if (!Models::visible({(ci % CX * CS) * VOX + h, (ci / CX % CY * CS) * VOX + h, (ci / (CX * CY) * CS) * VOX + h}, h * 1.74f + VOX)) continue;
        for (const Part &p : parts[ci]) vis.push_back(&p);
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

void Terrain::drawObjects(Vector3 cam) const {
#ifdef __SWITCH__
    static const std::string dir = "sdmc:/switch/worms4nx/assets/models/decor/";
#else
    static const std::string dir = "./assets/models/decor/";
#endif
    struct Entry { Model m{}; float r = 0; };
    static std::map<std::string, Entry> cache;  // loaded on first use, kept across matches
    static Shader sh{};
    if (objects.empty()) return;
    if (!sh.id) sh = Lit::modelShader(false);  // textured, alpha-tested, two-sided (grass cards)
    std::vector<const Entry *> ms;
    for (const std::string &name : objModels) {
        auto it = cache.find(name);
        if (it == cache.end()) {
            Entry e;
            std::string path = dir + name + ".glb";
            if (FileExists(path.c_str())) e.m = LoadModel(path.c_str());
            for (int k = 0; k < e.m.materialCount; k++) {
                if (sh.id != rlGetShaderIdDefault()) e.m.materials[k].shader = sh;
                Texture2D &t = e.m.materials[k].maps[MATERIAL_MAP_ALBEDO].texture;
                if (t.id != rlGetTextureIdDefault()) { GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); }
            }
            if (e.m.meshCount) { BoundingBox b = GetModelBoundingBox(e.m); e.r = Vector3Distance(b.min, b.max) / 2; }
            it = cache.emplace(name, e).first;
        }
        ms.push_back(&it->second);
    }
    rlDisableBackfaceCulling();
    for (const Object &o : objects) {
        const Entry &e = *ms[o.model];
        float size = e.r * Vector3Length({o.m.m0, o.m.m1, o.m.m2});
        if (!e.m.meshCount || Vector3Distance(cam, o.pos) > 35 + 40 * size || !Models::visible(o.pos, 2 * size)) continue;  // origin may sit on the box edge
        for (int i = 0; i < e.m.meshCount; i++) DrawMesh(e.m.meshes[i], e.m.materials[e.m.meshMaterial[i]], o.m);
    }
    rlEnableBackfaceCulling();
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
    textures.clear(), texMats.clear();
}

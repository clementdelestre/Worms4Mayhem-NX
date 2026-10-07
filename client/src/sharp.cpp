#include "sharp.h"
#include "raymath.h"
#include "terrain.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

static const int &NX = Terrain::NX, &NY = Terrain::NY, &NZ = Terrain::NZ;
static const size_t &CELLS = Terrain::TOTAL;
#define VOX Terrain::VOX
#define IVOX Terrain::IVOX
static size_t cid(int x, int y, int z) { return Terrain::idx(x, y, z); }
static uint32_t hashc(size_t c) { return (uint32_t)(c * 2654435761u); }

static float sdBox(Vector3 p, Vector3 c, Vector3 half) {
    Vector3 q = {fabsf(p.x - c.x) - half.x, fabsf(p.y - c.y) - half.y, fabsf(p.z - c.z) - half.z};
    return Vector3Length(Vector3Max(q, Vector3Zero())) + fminf(fmaxf(q.x, fmaxf(q.y, q.z)), 0);
}

static const int FACES[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};

extern "C" int sinflate(void *out, int cap, const void *in, int size);  // raylib's DEFLATE decoder (external/sinfl.h)

// "W4C1", u32 size, then that many bytes deflated: u32 hexes, top (0 or NX*NZ), cells, lists, list bytes, cell bytes; then the
// hexes, top, lists, cells (docs/w4m/formats.md)
bool SharpLand::load(const std::string &path) {
    *this = SharpLand{};
    int size = 0;
    unsigned char *f = LoadFileData(path.c_str(), &size);
    uint32_t raw = 0, n[6] = {};
    if (f && size >= 8 && !memcmp(f, "W4C1", 4)) memcpy(&raw, f + 4, 4);
    std::vector<unsigned char> b(raw < (1u << 30) ? raw : 0);
    bool ok = raw >= 24 && b.size() == raw && sinflate(b.data(), (int)raw, f + 8, size - 8) == (int)raw;
    UnloadFileData(f);
    if (ok) memcpy(n, b.data(), 24);
    ok = ok && (n[1] == 0 || n[1] == (uint32_t)(NX * NZ)) && raw == 24 + (size_t)n[0] * 100 + (size_t)n[1] * 4 + n[4] + n[5];
    const unsigned char *p = b.data() + 24, *end = b.data() + raw;
    auto var = [&] {
        uint32_t v = 0;
        for (int s = 0; p < end && s < 35; s += 7) { uint32_t c = *p++; v |= (c & 127) << s; if (!(c & 128)) return v; }
        ok = false;
        return v;
    };
    if (ok) {
        for (uint32_t h = 0; h < n[0]; h++, p += 100) {
            Vector3 c[8];
            uint32_t fl;
            memcpy(c, p, 96), memcpy(&fl, p + 96, 4);
            cell(c, fl);
        }
        top.resize(n[1]), memcpy(top.data(), p, n[1] * 4), p += n[1] * 4;
        std::vector<uint32_t> at(n[3]);
        for (uint32_t l = 0; l < n[3] && ok; l++) {
            const uint32_t k = var();
            uint32_t last = 0;
            at[l] = (uint32_t)pool.size(), pool.push_back(k);
            for (uint32_t w = 0; w < k && ok; w++) {
                const uint32_t v = var();
                if (v & 7) { pool.push_back((v & 7) << 29 | v >> 3); continue; }
                last += v >> 3, ok = last < n[0] && w + 1 < k, w++;
                pool.push_back(HEX | last), pool.push_back(var());
            }
        }
        bits.assign((CELLS + 63) / 64, 0);
        size_t cap = 1024;
        while (cap < 2 * (size_t)n[2]) cap *= 2;
        key.assign(cap, 0), val.assign(cap, 0);
        for (uint32_t i = 0, c = ~0u; i < n[2] && ok; i++) {
            c += var() + 1;
            const uint32_t l = var();
            ok = ok && c < CELLS && l < n[3];
            if (!ok) break;
            uint32_t s = hashc(c) & (cap - 1);
            while (key[s]) s = (s + 1) & (cap - 1);
            key[s] = c + 1, val[s] = at[l], bits[c >> 6] |= 1ull << (c & 63);
        }
        used = n[2], on = ok && p == end;
    }
    if (!on) *this = SharpLand{};
    else TraceLog(LOG_INFO, "exact land: %u cells, %u lists, %u hexahedra, %.1f MB", n[2], n[3], n[0],
                  (planes.size() * 16 + top.size() * 4 + bits.size() * 8 + (hexP0.size() + key.size() + val.size() + pool.size()) * 4) / 1048576.0);
    return on;
}

// a convex cell's planes; flags (bit t triangle kept, 12 + t flipped, 24 twisted) as the importer decided them, < 0: decided here
uint32_t SharpLand::cell(const Vector3 *c, int64_t flags) {
    Vector3 cen{}, lo = c[0], hi = c[0];
    for (int k = 0; k < 8; k++) cen = Vector3Add(cen, Vector3Scale(c[k], 0.125f)), lo = Vector3Min(lo, c[k]), hi = Vector3Max(hi, c[k]);
    const uint32_t p0 = (uint32_t)planes.size();
    hexP0.push_back(p0);
    for (int t = 0; t < 12; t++) {
        const int *f = FACES[t / 2], a = f[0], e = f[t & 1 ? 2 : 1], g = f[t & 1 ? 3 : 2];
        Vector3 nn = Vector3CrossProduct(Vector3Subtract(c[e], c[a]), Vector3Subtract(c[g], c[a]));
        const float l = Vector3Length(nn);
        if (flags >= 0 ? !(flags >> t & 1) : l < 1e-6f * VOX * VOX) continue;
        nn = Vector3Scale(nn, 1 / l);
        const float d = Vector3DotProduct(nn, c[a]);
        const bool flip = flags >= 0 ? flags >> (12 + t) & 1 : Vector3DotProduct(nn, cen) - d > 0;
        planes.push_back(flip ? Vector4{-nn.x, -nn.y, -nn.z, -d} : Vector4{nn.x, nn.y, nn.z, d});
    }
    bool twisted = flags >= 0 && flags >> 24 & 1;  // its planes reach past its corners: bounded by its box too
    for (uint32_t q = p0; q < planes.size() && flags < 0 && !twisted; q++)
        for (int k = 0; k < 8; k++) twisted |= planes[q].x * c[k].x + planes[q].y * c[k].y + planes[q].z * c[k].z - planes[q].w > 1e-4f;
    if (twisted)
        for (Vector4 q : {Vector4{1, 0, 0, hi.x}, {-1, 0, 0, -lo.x}, {0, 1, 0, hi.y}, {0, -1, 0, -lo.y}, {0, 0, 1, hi.z}, {0, 0, -1, -lo.z}}) planes.push_back(q);
    return (uint32_t)hexP0.size() - 1;
}

bool SharpLand::inside(uint32_t hex, Vector3 p) const {
    const uint32_t end = hex + 1 < hexP0.size() ? hexP0[hex + 1] : (uint32_t)planes.size();
    for (uint32_t q = hexP0[hex]; q < end; q++)
        if (planes[q].x * p.x + planes[q].y * p.y + planes[q].z * p.z - planes[q].w > 1e-5f) return false;
    return true;
}

uint32_t SharpLand::add(const std::vector<signed char> &d, const Vector3 *c) {
    if (!on) on = true, bits.assign((CELLS + 63) / 64, 0), key.assign(1024, 0), val.assign(1024, 0);
    const uint32_t id = cell(c, -1), p0 = hexP0[id], np = (uint32_t)planes.size() - p0;
    Vector3 lo = c[0], hi = c[0];
    for (int k = 0; k < 8; k++) lo = Vector3Min(lo, c[k]), hi = Vector3Max(hi, c[k]);
    std::vector<uint32_t> o;
    for (int z = std::max(0, (int)floorf((lo.z - 1e-4f) * IVOX)); z <= std::min(NZ - 2, (int)floorf((hi.z + 1e-4f) * IVOX)); z++)
        for (int y = std::max(0, (int)floorf((lo.y - 1e-4f) * IVOX)); y <= std::min(NY - 2, (int)floorf((hi.y + 1e-4f) * IVOX)); y++)
            for (int x = std::max(0, (int)floorf((lo.x - 1e-4f) * IVOX)); x <= std::min(NX - 2, (int)floorf((hi.x + 1e-4f) * IVOX)); x++) {
                uint32_t mask = 0;
                bool sep = false;
                for (uint32_t q = 0; q < np && !sep; q++) {
                    const Vector4 &pl = planes[p0 + q];
                    int out = 0, on = 0;  // a plane on the cell's border is kept: the face it holds is entered from this cell
                    for (int k = 0; k < 8; k++) {
                        const float v = pl.x * (x + (k & 1)) * VOX + pl.y * (y + (k >> 1 & 1)) * VOX + pl.z * (z + (k >> 2)) * VOX - pl.w;
                        out += v > 1e-5f, on += v > -1e-5f;
                    }
                    sep = out == 8, mask |= (uint32_t)(on > 0) << q;
                }
                const size_t ci = cid(x, y, z);
                const bool m = mixed(ci);
                if (sep || (!m && d[ci] > 0)) continue;
                if (!mask) { if (m) setOps(ci, nullptr, 0); continue; }
                if (m) { const uint32_t *p = ops(ci); o.assign(p + 1, p + 1 + p[0]); }
                else o.clear();
                o.push_back(HEX | id), o.push_back(mask);
                setOps(ci, o.data(), (uint32_t)o.size());
            }
    return id;
}

float SharpLand::hm(float x, float z) const {  // bilinear, -1e9 none; a missing corner takes the nearest one's height
    float fx = x * IVOX, fz = z * IVOX;
    int ix = (int)floorf(fx), iz = (int)floorf(fz);
    if (top.empty() || ix < 0 || iz < 0 || ix >= NX - 1 || iz >= NZ - 1) return -1e9f;
    float tx = fx - ix, tz = fz - iz, v[4] = {top[iz * NX + ix], top[iz * NX + ix + 1], top[(iz + 1) * NX + ix], top[(iz + 1) * NX + ix + 1]};
    int near = (tz < 0.5f ? 0 : 2) + (tx < 0.5f ? 0 : 1);
    if (std::isnan(v[near])) return -1e9f;
    for (float &a : v) if (std::isnan(a)) a = v[near];
    return (v[0] * (1 - tx) + v[1] * tx) * (1 - tz) + (v[2] * (1 - tx) + v[3] * tx) * tz;
}

Vector3 SharpLand::hmNormal(float x, float z) const {
    float gx = (hm(x + VOX, z) - hm(x - VOX, z)) / (2 * VOX), gz = (hm(x, z + VOX) - hm(x, z - VOX)) / (2 * VOX);
    return Vector3Normalize({-gx, 1, -gz});
}

const uint32_t *SharpLand::ops(size_t c) const {
    uint32_t m = (uint32_t)key.size() - 1;
    for (uint32_t s = hashc(c) & m;; s = (s + 1) & m)
        if (key[s] == c + 1) return &pool[val[s]];
}

void SharpLand::compact() {  // drops the dead lists and the slots of cells no longer listed
    size_t live = 0;
    for (size_t s = 0; s < key.size(); s++) live += key[s] && mixed(key[s] - 1);
    size_t cap = 1024;
    while (cap < 2 * live) cap *= 2;
    std::vector<uint32_t> k2(cap, 0), v2(cap), p2;
    std::unordered_map<uint32_t, uint32_t> moved;  // lists shared by several cells stay shared
    for (size_t s = 0; s < key.size(); s++) {
        if (!key[s] || !mixed(key[s] - 1)) continue;
        uint32_t t = hashc(key[s] - 1) & (cap - 1);
        while (k2[t]) t = (t + 1) & (cap - 1);
        auto [it, fresh] = moved.try_emplace(val[s], (uint32_t)p2.size());
        if (fresh) p2.insert(p2.end(), &pool[val[s]], &pool[val[s]] + pool[val[s]] + 1);
        k2[t] = key[s], v2[t] = it->second;
    }
    key.swap(k2), val.swap(v2), pool.swap(p2), used = live, dead = 0;
}

void SharpLand::setOps(size_t c, const uint32_t *o, uint32_t n) {
    if ((used + 1) * 4 > key.size() * 3) compact();
    uint32_t m = (uint32_t)key.size() - 1, s = hashc(c) & m;
    while (key[s] && key[s] != c + 1) s = (s + 1) & m;
    if (!key[s]) key[s] = (uint32_t)c + 1, used++;
    else if (mixed(c)) dead += pool[val[s]] + 1;
    if (!n) bits[c >> 6] &= ~(1ull << (c & 63));
    else {
        val[s] = (uint32_t)pool.size();
        pool.push_back(n), pool.insert(pool.end(), o, o + n);
        bits[c >> 6] |= 1ull << (c & 63);
    }
    if (dead > 4096 && dead * 2 > pool.size()) compact();
}

float SharpLand::eval(Vector3 p, size_t c, Vector3 *nrm, const Vector3 *dir) const {
    const uint32_t *o = ops(c), n = o[0];
    Terrain::samples += EVAL_COST;
    float s = -0.5f;
    uint32_t act = ~0u, sub = 0, actMask = 0;
    for (uint32_t i = 1; i <= n; i++) {
        uint32_t k = o[i] & KIND, id = o[i] & ID;
        if (k == HEX) {  // the convex cell: its nearest cutting plane; a 1e-5 m bias lets land faces win ties
            const Vector4 *pl = &planes[hexP0[id]];
            uint32_t op = o[i], pi = 0;
            float m = 1e9f;
            for (uint32_t mask = o[++i]; mask; mask &= mask - 1) {
                uint32_t q = __builtin_ctz(mask);
                float v = pl[q].w - (pl[q].x * p.x + pl[q].y * p.y + pl[q].z * p.z);
                if (v < m) { m = v, pi = q; if (m + 1e-5f <= s) break; }
            }
            if (m + 1e-5f > s) s = m + 1e-5f, act = op, sub = pi, actMask = o[i];
        } else if (k == HM) {
            float v = hm(p.x, p.z) - p.y;
            if (v > s) s = v, act = o[i];
        } else if (k == FULL) {
            s = 0.5f, act = ~0u;
        } else if (k == SPHERE) {
            const Vector4 &q = sph[id];
            float v = sqrtf((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z)) - q.w;
            if (v < s) s = v, act = o[i];
        } else {
            float v = -sdBox(p, box[2 * id], box[2 * id + 1]);
            if (v > s) s = v, act = o[i];
        }
    }
    if (nrm) {
        *nrm = {0, 0, 0};
        uint32_t k = act & KIND, id = act & ID;
        if (act == ~0u) {}
        else if (k == HEX) {
            const Vector4 *pl = &planes[hexP0[id]];
            for (float best = 1e30f; dir && actMask; actMask &= actMask - 1) {  // the latest entry: least depth over approach speed
                const uint32_t q = __builtin_ctz(actMask);
                const float den = pl[q].x * dir->x + pl[q].y * dir->y + pl[q].z * dir->z;
                if (den < -1e-6f) {
                    const float t = (pl[q].w - (pl[q].x * p.x + pl[q].y * p.y + pl[q].z * p.z)) / -den;
                    if (t < best) best = t, sub = q;
                }
            }
            *nrm = {pl[sub].x, pl[sub].y, pl[sub].z};
        }
        else if (k == HM) *nrm = hmNormal(p.x, p.z);
        else if (k == SPHERE) { const Vector4 &q = sph[id]; Vector3 d = {q.x - p.x, q.y - p.y, q.z - p.z}; if (Vector3LengthSqr(d) > 1e-12f) *nrm = Vector3Normalize(d); }
        else {
            const float e = 1e-3f;
            Vector3 c0 = box[2 * id], h = box[2 * id + 1];
            Vector3 g = {sdBox({p.x + e, p.y, p.z}, c0, h) - sdBox({p.x - e, p.y, p.z}, c0, h), sdBox({p.x, p.y + e, p.z}, c0, h) - sdBox({p.x, p.y - e, p.z}, c0, h),
                         sdBox({p.x, p.y, p.z + e}, c0, h) - sdBox({p.x, p.y, p.z - e}, c0, h)};
            if (Vector3LengthSqr(g) > 1e-12f) *nrm = Vector3Normalize(g);
        }
    }
    return Clamp(s, -0.5f, 0.5f);
}

void SharpLand::crossings(Vector3 a, Vector3 dir, float t0, float t1, size_t c, std::vector<float> &out, bool grid) const {
    const uint32_t *o = ops(c), n = o[0];
    auto add = [&](float t) { if (t > t0 && t < t1) out.push_back(t); };
    auto slab = [&](const Vector4 *pl, uint32_t mask) {
        float lo = -1e30f, hi = 1e30f;
        for (; mask; mask &= mask - 1) {
            uint32_t q = __builtin_ctz(mask);
            float den = pl[q].x * dir.x + pl[q].y * dir.y + pl[q].z * dir.z, num = pl[q].w - (pl[q].x * a.x + pl[q].y * a.y + pl[q].z * a.z);
            if (fabsf(den) < 1e-12f) { if (num < -1e-5f) return; continue; }
            float t = num / den;
            if (den < 0) lo = fmaxf(lo, t); else hi = fminf(hi, t);
            if (lo > hi) return;
        }
        add(lo), add(hi);
    };
    for (uint32_t i = 1; i <= n; i++) {
        uint32_t k = o[i] & KIND, id = o[i] & ID;
        if (k == HEX) slab(&planes[hexP0[id]], o[++i]);
        else if (k == SPHERE) {
            const Vector4 &q = sph[id];
            Vector3 m = {a.x - q.x, a.y - q.y, a.z - q.z};
            float b = Vector3DotProduct(m, dir), cc = Vector3DotProduct(m, m) - q.w * q.w, disc = b * b - cc;
            if (disc > 0) add(-b - sqrtf(disc)), add(-b + sqrtf(disc));
        } else if (k == BOX) {
            Vector3 c0 = box[2 * id], h = box[2 * id + 1];
            Vector4 pl[6] = {{1, 0, 0, c0.x + h.x}, {-1, 0, 0, -(c0.x - h.x)}, {0, 1, 0, c0.y + h.y}, {0, -1, 0, -(c0.y - h.y)}, {0, 0, 1, c0.z + h.z}, {0, 0, -1, -(c0.z - h.z)}};
            slab(pl, 63);
        } else if (k == HM) {  // a smooth patch: bracketed in 8 steps, then bisected
            auto f = [&](float t) { return hm(a.x + dir.x * t, a.z + dir.z * t) - (a.y + dir.y * t); };
            if (grid) {  // on a grid edge hm is linear on each half (bilinear along a grid line), a missing corner switching at the middle
                const float tm = (t0 + t1) / 2, e = (t1 - t0) * 1e-4f, f0 = f(t0), fa = f(tm - e), fb = f(tm + e), f1 = f(t1);
                if ((f0 > 0) != (fa > 0)) add(t0 + (tm - e - t0) * f0 / (f0 - fa));
                if ((fa > 0) != (fb > 0)) add(tm);
                if ((fb > 0) != (f1 > 0)) add(tm + e + (t1 - tm - e) * fb / (fb - f1));
                continue;
            }
            float pt = t0, pf = f(t0);
            for (int s = 1; s <= 8; s++) {
                float ct = t0 + (t1 - t0) * s / 8, cf = f(ct);
                if ((pf > 0) != (cf > 0)) {
                    float lo = pt, hi = ct;
                    for (int it = 0; it < 24; it++) { float m = (lo + hi) / 2; ((f(m) > 0) == (pf > 0) ? lo : hi) = m; }
                    add(hi);
                }
                pt = ct, pf = cf;
            }
        }
    }
}

float SharpLand::first(Vector3 a, Vector3 dir, float t0, float t1, size_t c, bool land, bool grid) const {
    static thread_local std::vector<float> cand;
    cand.assign(1, t0);
    Terrain::samples += FIRST_COST;
    crossings(a, dir, t0, t1, c, cand, grid);
    std::sort(cand.begin() + 1, cand.end());
    cand.push_back(t1);
    for (size_t i = 0; i + 1 < cand.size(); i++)
        if ((eval(Vector3Add(a, Vector3Scale(dir, (cand[i] + cand[i + 1]) / 2)), c, nullptr) > 0) == land) return cand[i];
    return -1;
}

bool SharpLand::edge(size_t c, Vector3 a, Vector3 b, Vector3 *q, Vector3 *n) const {
    const float len = Vector3Distance(a, b);
    const Vector3 dir = Vector3Scale(Vector3Subtract(b, a), 1 / len);
    const bool into = eval(a, c, nullptr) <= 0;
    const float t = first(a, dir, 0, len, c, into);
    if (t <= 0) return false;
    *q = Vector3Add(a, Vector3Scale(dir, t));
    eval(Vector3Add(a, Vector3Scale(dir, into ? fminf(t + 1e-4f, len) : fmaxf(t - 1e-4f, 0))), c, n);
    return Vector3LengthSqr(*n) > 0;
}

void SharpLand::drop(const std::vector<uint32_t> &hexes) {
    if (!on || hexes.empty()) return;
    std::vector<bool> gone(hexP0.size(), false);
    for (uint32_t h : hexes) if (h < gone.size()) gone[h] = true;
    std::vector<uint32_t> o;
    for (size_t w = 0; w < bits.size(); w++)
        for (uint64_t b = bits[w]; b; b &= b - 1) {
            size_t c = w * 64 + __builtin_ctzll(b);
            const uint32_t *p = ops(c);
            o.clear();
            for (uint32_t i = 1; i <= p[0]; i++) {
                bool hex = (p[i] & KIND) == HEX;
                if (!hex || !gone[p[i] & ID]) { o.push_back(p[i]); if (hex) o.push_back(p[i + 1]); }
                i += hex;
            }
            if (o.size() != p[0]) setOps(c, o.data(), (uint32_t)o.size());
        }
}

void SharpLand::carve(const std::vector<signed char> &d, Vector3 c, float r, int box[6]) {
    if (!on) return;
    const uint32_t id = (uint32_t)sph.size();
    sph.push_back({c.x, c.y, c.z, r});
    int lo[3], hi[3];
    const float cc[3] = {c.x, c.y, c.z}, dim[3] = {NX - 1.0f, NY - 1.0f, NZ - 1.0f};
    for (int a = 0; a < 3; a++) lo[a] = std::max(0, (int)floorf((cc[a] - r) * IVOX)), hi[a] = std::min((int)dim[a] - 1, (int)floorf((cc[a] + r) * IVOX));
    std::vector<uint32_t> o;
    for (int z = lo[2]; z <= hi[2]; z++)
        for (int y = lo[1]; y <= hi[1]; y++)
            for (int x = lo[0]; x <= hi[0]; x++) {
                const float b0[3] = {x * VOX, y * VOX, z * VOX};
                float dn = 0, dx = 0;  // nearest and farthest point of the cell from c, squared
                for (int a = 0; a < 3; a++) {
                    float u = fmaxf(fmaxf(b0[a] - cc[a], cc[a] - b0[a] - VOX), 0), w = fmaxf(fabsf(b0[a] - cc[a]), fabsf(b0[a] + VOX - cc[a]));
                    dn += u * u, dx += w * w;
                }
                if (dn >= r * r) continue;
                const size_t ci = cid(x, y, z);
                const bool m = mixed(ci);
                if (!m && (dx <= r * r || d[ci] <= 0)) continue;
                const int at[3] = {x, y, z};
                for (int a = 0; a < 3; a++) box[a] = std::min(box[a], at[a]), box[a + 3] = std::max(box[a + 3], at[a]);
                if (dx <= r * r) { setOps(ci, nullptr, 0); continue; }
                if (m) { const uint32_t *p = ops(ci); o.assign(p + 1, p + 1 + p[0]); }
                else o.assign(1, FULL);
                o.push_back(SPHERE | id);
                setOps(ci, o.data(), (uint32_t)o.size());
            }
}

void SharpLand::weld(const std::vector<signed char> &d, Vector3 c, Vector3 half) {
    if (!on) return;
    const uint32_t id = (uint32_t)box.size() / 2;
    box.push_back(c), box.push_back(half);
    int lo[3], hi[3];
    const float cc[3] = {c.x, c.y, c.z}, hh[3] = {half.x, half.y, half.z}, dim[3] = {NX - 1.0f, NY - 1.0f, NZ - 1.0f};
    for (int a = 0; a < 3; a++) lo[a] = std::max(0, (int)floorf((cc[a] - hh[a]) * IVOX)), hi[a] = std::min((int)dim[a] - 1, (int)floorf((cc[a] + hh[a]) * IVOX));
    std::vector<uint32_t> o;
    for (int z = lo[2]; z <= hi[2]; z++)
        for (int y = lo[1]; y <= hi[1]; y++)
            for (int x = lo[0]; x <= hi[0]; x++) {
                const size_t ci = cid(x, y, z);
                const bool m = mixed(ci);
                bool in = true;
                for (int k = 0; k < 8 && in; k++) in = sdBox({(x + (k & 1)) * VOX, (y + (k >> 1 & 1)) * VOX, (z + (k >> 2)) * VOX}, c, half) < 0;
                if (in) { if (m) setOps(ci, nullptr, 0); continue; }
                if (m) { const uint32_t *p = ops(ci); o.assign(p + 1, p + 1 + p[0]); }
                else if (d[ci] <= 0) o.clear();
                else continue;
                o.push_back(BOX | id);
                setOps(ci, o.data(), (uint32_t)o.size());
            }
}

#include "replay.h"
#include "loading.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

// .w4r: "W4R2", config (Start message layout minus owners), u32 final checksum, u32 ticks, 5 bytes per tick (W4R1: 4, no flags).
namespace {
struct Out {
    std::string b;
    void u8(uint8_t v) { b += (char)v; }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) u8(v >> 8 * i); }
    void f32(float f) { uint32_t v; memcpy(&v, &f, 4); u32(v); }
    void str(const std::string &s) { u8((uint8_t)std::min<size_t>(s.size(), 255)); b.append(s, 0, 255); }
};
struct In {
    const std::string &b;
    size_t p = 0;
    bool ok = true;
    uint8_t u8() { if (p >= b.size()) { ok = false; return 0; } return (uint8_t)b[p++]; }
    uint32_t u32() { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)u8() << 8 * i; return v; }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    std::string str() { size_t n = u8(); if (b.size() - p < n) { ok = false; return {}; } p += n; return b.substr(p - n, n); }
};
}  // namespace

bool Recording::save(const std::string &path) const {
    Out o;
    o.b = "W4R2";
    o.u32(cfg.seed), o.u8(cfg.teams), o.u8(cfg.wormsPerTeam), o.str(cfg.map), o.u32(cfg.rules);
    o.u8((uint8_t)cfg.teamSetup.size());
    for (const auto &t : cfg.teamSetup) o.str(t.name), o.u8(t.cpu), o.u8(t.voice), o.u8(t.hat);
    o.u8(sizeof(Scheme));
    o.b.append((const char *)&cfg.scheme, sizeof(Scheme));
    o.u32(cfg.wormpot), o.u8((uint8_t)std::min<size_t>(cfg.custom.size(), 255));
    for (size_t i = 0; i < cfg.custom.size() && i < 255; i++) {
        const WeaponDef &d = cfg.custom[i];
        o.str(d.name), o.u8((uint8_t)d.kind);
        for (float f : {d.radius, d.damage, d.speed, d.fuse, d.bounce, d.cradius, d.cdamage, d.poison}) o.f32(f);
        for (int v : {d.count, d.clusters, d.shots, d.weight}) o.u32((uint32_t)v);
        o.u8(d.wind | d.avoid << 1), o.str(d.model), o.str(d.icon);
    }
    o.u32(checksum), o.u32((uint32_t)inputs.size());
    for (const Input &i : inputs) o.u8(i.turn), o.u8(i.walk), o.u8(i.aim), o.u8(i.buttons), o.u8(i.flags);
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(o.b.data(), 1, o.b.size(), f) == o.b.size();
    return fclose(f) == 0 && ok;
}

bool Recording::load(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string b;
    char buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f));) b.append(buf, n);
    fclose(f);
    int per = !b.compare(0, 4, "W4R2") ? 5 : !b.compare(0, 4, "W4R1") ? 4 : 0;
    if (!per) return false;
    In r{b, 4};
    cfg = GameConfig{};
    cfg.seed = r.u32(), cfg.teams = r.u8(), cfg.wormsPerTeam = r.u8(), cfg.map = r.str(), cfg.rules = r.u32();
    cfg.teamSetup.resize(r.u8());
    for (auto &t : cfg.teamSetup) t.name = r.str(), t.cpu = r.u8(), t.voice = r.u8(), t.hat = r.u8();
    for (size_t i = 0, n = r.u8(); i < n; i++) {  // a newer, longer Scheme: extra bytes ignored
        uint8_t v = r.u8();
        if (i < sizeof(Scheme)) ((uint8_t *)&cfg.scheme)[i] = v;
    }
    cfg.wormpot = r.u32();
    for (int i = 0, n = r.u8(); i < n && r.ok; i++) {
        WeaponDef w{};
        w.name = r.str(), w.kind = (Kind)std::min<int>(r.u8(), (int)Kind::ChangeWorm);
        for (float *f : {&w.radius, &w.damage, &w.speed, &w.fuse, &w.bounce, &w.cradius, &w.cdamage, &w.poison}) *f = r.f32();
        for (int *v : {&w.count, &w.clusters, &w.shots, &w.weight}) *v = (int32_t)r.u32();
        { uint8_t fl = r.u8(); w.wind = fl & 1, w.avoid = fl & 2; }
        w.model = r.str(), w.icon = r.str();
        cfg.custom.push_back(w);
    }
    checksum = r.u32();
    uint32_t n = r.u32();
    if (!r.ok || cfg.teams < 2 || cfg.teams > 4 || (b.size() - r.p) / per < n) return false;
    inputs.resize(n);
    for (Input &i : inputs) i.turn = (int8_t)r.u8(), i.walk = (int8_t)r.u8(), i.aim = (int8_t)r.u8(), i.buttons = r.u8(), i.flags = per > 4 ? r.u8() : 0;
    return true;
}

std::vector<std::string> listReplays(const std::string &dir) {
    std::vector<std::string> out;
    for (const std::string &f : Loading::list(dir, ".w4r")) out.push_back(f.substr(f.rfind('/') + 1));
    std::sort(out.rbegin(), out.rend());  // date prefix: newest first
    return out;
}

std::string replayName(const GameConfig &cfg) {
    char date[32];
    time_t now = time(nullptr);
    strftime(date, sizeof date, "%Y%m%d_%H%M%S", localtime(&now));
    std::string map = cfg.map.empty() ? "island" : cfg.map;
    for (char &c : map) if (c == '/' || c == ' ') c = '_';
    return std::string(date) + "_" + map + ".w4r";
}

namespace {
void copyState(Game &to, Game &from) {  // the voxels stay out of the copy
    Terrain ter = std::move(from.terrain);
    to = from;
    from.terrain = std::move(ter);
}
void setState(Game &live, const Game &from) {
    Terrain ter = std::move(live.terrain);
    live = from;
    live.terrain = std::move(ter);
}
void markChunks(Terrain &t, int v) {  // chunk cells sample one voxel past their bounds
    const int CS = Terrain::CS, CX = Terrain::CX, CY = Terrain::CY;
    int x = v % Terrain::NX, y = v / Terrain::NX % Terrain::NY, z = (int)(v / Terrain::SXY);
    for (int cz = std::max(z - 1, 0) / CS; cz <= std::min(z + 1, Terrain::NZ - 1) / CS; cz++)
        for (int cy = std::max(y - 1, 0) / CS; cy <= std::min(y + 1, Terrain::NY - 1) / CS; cy++)
            for (int cx = std::max(x - 1, 0) / CS; cx <= std::min(x + 1, Terrain::NX - 1) / CS; cx++) t.dirty[(cz * CY + cy) * CX + cx] = true;
}
}  // namespace

void Snapshot::take(Game &live, uint32_t t) {
    copyState(g, live);
    decor = live.terrain.objects, emit = live.terrain.emitters;
    log.clear(), cps.clear(), redo.clear();
    live.terrain.undo = &log;
    live.terrain.keepMeshes();
    tick = t;
    valid = true, hasLive = false;
}

void Snapshot::mark(Game &live, uint32_t t) {
    if (cps.size() == 20) cps.erase(cps.begin());
    cps.push_back({t, Game{}, live.terrain.objects, live.terrain.emitters, log.size()});
    copyState(cps.back().g, live);
}

uint32_t Snapshot::restore(Game &live, uint32_t upTo) {
    Terrain &t = live.terrain;
    t.remeshWait();
    size_t k = cps.size();
    while (k > 0 && cps[k - 1].tick > upTo) k--;
    size_t stop = k ? cps[k - 1].log : 0;
    redo.clear();
    for (size_t i = stop; i < log.size(); i++) redo.push_back({log[i].first, log[i].first < 0 ? (signed char)0 : t.d[log[i].first]});
    copyState(liveG, live);
    liveDecor = t.objects, liveEmit = t.emitters, hasLive = true;
    bool kept = stop == 0 && t.rewindMeshes();  // the meshes drawn at take() are the restored land's
    for (size_t i = log.size(); i-- > stop;) {  // newest first: the oldest value of a voxel wins
        int v = log[i].first < 0 ? -1 - log[i].first : log[i].first;  // negative: a voxel a girder made steel
        if (log[i].first < 0) t.steel[v] = false;
        else t.d[v] = log[i].second;
        if (!kept) markChunks(t, v);
    }
    log.resize(stop);
    t.objects = k ? cps[k - 1].decor : decor, t.emitters = k ? cps[k - 1].emit : emit;
    setState(live, k ? cps[k - 1].g : g);
    return k ? cps[k - 1].tick : tick;
}

bool Snapshot::forward(Game &live) {
    if (!hasLive) return false;
    hasLive = false;
    Terrain &t = live.terrain;
    t.remeshWait();
    bool kept = t.forwardMeshes();
    for (auto [v, val] : redo) {
        int i = v < 0 ? -1 - v : v;
        if (v < 0) t.steel[i] = true;
        else t.d[i] = val;
        if (!kept) markChunks(t, i);
    }
    redo.clear();
    t.objects = liveDecor, t.emitters = liveEmit;
    setState(live, liveG);
    return true;
}

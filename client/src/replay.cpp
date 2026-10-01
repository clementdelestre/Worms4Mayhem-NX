#include "replay.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

// .w4r: "W4R1", config (Start message layout minus owners), u32 final checksum, u32 ticks, 4 bytes per tick.
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
    o.b = "W4R1";
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
        o.u8(d.wind), o.str(d.model), o.str(d.icon);
    }
    o.u32(checksum), o.u32((uint32_t)inputs.size());
    for (const Input &i : inputs) o.u8(i.turn), o.u8(i.walk), o.u8(i.aim), o.u8(i.buttons);
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
    if (b.compare(0, 4, "W4R1")) return false;
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
        w.wind = r.u8(), w.model = r.str(), w.icon = r.str();
        cfg.custom.push_back(w);
    }
    checksum = r.u32();
    uint32_t n = r.u32();
    if (!r.ok || cfg.teams < 2 || cfg.teams > 4 || (b.size() - r.p) / 4 < n) return false;
    inputs.resize(n);
    for (Input &i : inputs) i.turn = (int8_t)r.u8(), i.walk = (int8_t)r.u8(), i.aim = (int8_t)r.u8(), i.buttons = r.u8();
    return true;
}

std::vector<std::string> listReplays(const std::string &dir) {
    std::vector<std::string> out;
    if (!DirectoryExists(dir.c_str())) return out;
    FilePathList files = LoadDirectoryFilesEx(dir.c_str(), ".w4r", false);
    for (unsigned i = 0; i < files.count; i++) out.push_back(GetFileName(files.paths[i]));
    UnloadDirectoryFiles(files);
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

void Snapshot::take(Game &live, uint32_t t) {
    Terrain ter = std::move(live.terrain);  // the voxels stay out of the copy
    g = live;
    live.terrain = std::move(ter);
    decor = live.terrain.objects;
    log.clear();
    live.terrain.undo = &log;
    tick = t;
    valid = true;
}

void Snapshot::restore(Game &live) {
    Terrain &t = live.terrain;
    constexpr int CS = Terrain::CS, CX = Terrain::NX / CS, CY = Terrain::NY / CS;
    for (auto it = log.rbegin(); it != log.rend(); ++it) {  // newest first: the oldest value of a voxel wins
        t.d[it->first] = it->second;
        int x = it->first % Terrain::NX, y = it->first / Terrain::NX % Terrain::NY, z = it->first / (Terrain::NX * Terrain::NY);
        for (int dz = -1; dz <= 1; dz++)  // chunk cells sample one voxel past their bounds
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int cx = (x + dx) / CS, cy = (y + dy) / CS, cz = (z + dz) / CS;
                    if (x + dx >= 0 && y + dy >= 0 && z + dz >= 0 && cx < CX && cy < CY && cz < Terrain::NZ / CS) t.dirty[(cz * CY + cy) * CX + cx] = true;
                }
    }
    log.clear();
    t.objects = decor;
    Terrain ter = std::move(t);
    live = g;
    live.terrain = std::move(ter);
}

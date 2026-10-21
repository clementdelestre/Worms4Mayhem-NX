#pragma once
// PROTOCOL.md framing shared by the client (net.cpp) and the embedded LAN relay (lanhost.cpp).
#include "sim.h"
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <string>

namespace wire {
enum : uint8_t {
    HELLO = 0x01, WELCOME, ERROR_, LIST_ROOMS = 0x10, ROOM_LIST, CREATE_ROOM, JOIN_ROOM, ROOM_STATE, LEAVE, TEAMS, LOBBY, READY,
    START = 0x20, INPUTS, TURN_END, DESYNC, REPLAY, CHAT = 0x30, PING, PONG,
};
static const uint16_t VERSION = 1;  // bump on any wire or sim change once a server is deployed
static const size_t INPUT_BYTES = 6;  // Input on the wire and in replays: turn, walk, aim, buttons, flags, zoom

struct W {
    std::string b;
    explicit W(uint8_t ty) : b{0, 0, (char)ty} {}
    W &u8(uint8_t v) { b += (char)v; return *this; }
    W &u16(uint16_t v) { return u8(v).u8(v >> 8); }
    W &u32(uint32_t v) { return u16(v).u16(v >> 16); }
    W &u64(uint64_t v) { return u32(v).u32(v >> 32); }
    W &str(const char *s) { size_t n = strnlen(s, 255); u8(n); b.append(s, n); return *this; }
    W &f32(float f) { uint32_t v; memcpy(&v, &f, 4); return u32(v); }
    W &input(const Input &i) { return u8(i.turn).u8(i.walk).u8(i.aim).u8(i.buttons).u8(i.flags).u8(i.zoom); }
    std::string done() { size_t n = b.size() - 2; b[0] = n; b[1] = n >> 8; return b; }
};

struct R {
    const uint8_t *p, *e;
    bool ok = true;
    const uint8_t *take(size_t n) {
        if ((size_t)(e - p) < n) { ok = false; static const uint8_t z[8] = {}; return z; }
        p += n;
        return p - n;
    }
    uint8_t u8() { return *take(1); }
    uint16_t u16() { const uint8_t *q = take(2); return q[0] | q[1] << 8; }
    uint32_t u32() { uint32_t lo = u16(); return lo | (uint32_t)u16() << 16; }
    uint64_t u64() { uint64_t lo = u32(); return lo | (uint64_t)u32() << 32; }
    std::string str() { size_t n = u8(); if ((size_t)(e - p) < n) { ok = false; return {}; } return std::string((const char *)take(n), n); }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    Input input() { Input in; in.turn = u8(); in.walk = u8(); in.aim = u8(); in.buttons = u8(); in.flags = u8(); in.zoom = u8(); return in; }
};

// A Weapon Factory weapon (PROTOCOL.md "Weapon"): its container fields; factoryPayload() rebuilds the payload
inline W &putWeapon(W &w, const WeaponDef &d) {
    w.str(d.name.c_str()).u8((uint8_t)d.kind);
    for (float f : {d.speed, d.fuse, d.bounce, d.poison, d.spread}) w.f32(f);
    for (auto &c : d.slider) for (float f : c) w.f32(f);
    for (int v : {d.count, d.clusters, d.shots, d.weight}) w.u32((uint32_t)v);
    w.u8(d.wind | d.avoid << 1).str(d.model.c_str()).str(d.icon.c_str());
    w.u8((uint8_t)d.launch).u8((uint8_t)d.detonate[0]).u8((uint8_t)d.detonate[1]).u32((uint32_t)d.fuseTime).u8(d.powersUp | d.cpoison << 1);
    for (float f : {d.maxPower, d.collision[0], d.collision[1]}) w.f32(f);
    return w.str(d.cmodel.c_str()).str(d.ariel.c_str());
}
inline WeaponDef getWeapon(R &r, bool container = true) {  // container: false for W4R6 replays, written before it
    WeaponDef w{};
    w.name = r.str();
    w.kind = (Kind)std::min<int>(r.u8(), (int)Kind::ChangeWorm);
    for (float *f : {&w.speed, &w.fuse, &w.bounce, &w.poison, &w.spread}) *f = r.f32();
    for (auto &c : w.slider) for (float &f : c) f = r.f32();
    for (int *v : {&w.count, &w.clusters, &w.shots, &w.weight}) *v = (int32_t)r.u32();
    uint8_t fl = r.u8();  // bit 0 wind, bit 1 HomingAvoidLand
    w.wind = fl & 1, w.avoid = fl & 2;
    w.model = r.str(), w.icon = r.str();
    if (!container) return w;
    w.launch = (int8_t)r.u8(), w.detonate[0] = (int8_t)r.u8(), w.detonate[1] = (int8_t)r.u8(), w.fuseTime = (int32_t)r.u32();
    uint8_t b = r.u8();
    w.powersUp = b & 1, w.cpoison = b & 2;
    for (float *f : {&w.maxPower, &w.collision[0], &w.collision[1]}) *f = r.f32();
    w.cmodel = r.str(), w.ariel = r.str();
    return w;
}

// Shared by START, TEAMS, LOBBY and replays (PROTOCOL.md "Team"): W4M NetworkTeamData's fields, the per-slot WormCount / Alliance / Handicap,
// then the team's Weapon Factory weapon (u8 0 / 1, the weapon)
inline W &putTeam(W &w, const GameConfig::Team &t) {
    w.str(t.name.c_str()).u8(t.cpu);
    for (const std::string *s : {&t.speech, &t.hat, &t.gloves, &t.glasses, &t.tash, &t.flag}) w.str(s->c_str());
    w.u8(t.grave).u8((uint8_t)std::min<size_t>(t.wormNames.size(), 6));
    for (size_t i = 0; i < t.wormNames.size() && i < 6; i++) w.str(t.wormNames[i].c_str());
    w.u8(t.worms).u8((uint8_t)t.alliance).u8((uint8_t)t.handicap).u8(t.secret.has_value());
    return t.secret ? putWeapon(w, *t.secret) : w;
}
inline GameConfig::Team getTeam(R &r, int secret = 2) {  // secret: 0 for W4R5 replays, written before it, 1 for W4R6 (no container fields)
    GameConfig::Team t;
    t.name = r.str(), t.cpu = r.u8();
    for (std::string *s : {&t.speech, &t.hat, &t.gloves, &t.glasses, &t.tash, &t.flag}) *s = r.str();
    t.grave = r.u8();
    for (int i = 0, n = std::min<int>(r.u8(), 6); i < n; i++) t.wormNames.push_back(r.str());
    t.worms = (uint8_t)std::min<int>(r.u8(), 6), t.alliance = (int8_t)r.u8(), t.handicap = (int8_t)std::clamp<int>((int8_t)r.u8(), -1, 1);
    if (t.alliance > 3) t.alliance = -1;
    if (secret && r.u8()) t.secret = getWeapon(r, secret > 1);
    return t;
}
inline W &putScheme(W &w, const Scheme &s) {
    w.u8(sizeof(Scheme));
    for (size_t i = 0; i < sizeof(Scheme); i++) w.u8(((const uint8_t *)&s)[i]);
    return w;
}
// Missing block = default scheme; extra bytes are ignored.
inline Scheme getScheme(R &r) {
    Scheme s;
    if (r.ok && r.p < r.e)
        for (size_t i = 0, n = r.u8(); i < n && r.ok; i++) {
            uint8_t v = r.u8();
            if (i < sizeof(Scheme)) ((uint8_t *)&s)[i] = v;
        }
    return s;
}
}  // namespace wire

#pragma once
// PROTOCOL.md framing shared by the client (net.cpp) and the embedded LAN relay (lanhost.cpp).
#include "sim.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace wire {
enum : uint8_t {
    HELLO = 0x01, WELCOME, ERROR_, LIST_ROOMS = 0x10, ROOM_LIST, CREATE_ROOM, JOIN_ROOM, ROOM_STATE, LEAVE,
    START = 0x20, INPUTS, TURN_END, DESYNC, REPLAY, CHAT = 0x30, PING, PONG,
};
static const uint16_t VERSION = 1;  // bump on any wire or sim change once a server is deployed
static const size_t INPUT_BYTES = 5;  // Input on the wire and in replays: turn, walk, aim, buttons, flags

struct W {
    std::string b;
    explicit W(uint8_t ty) : b{0, 0, (char)ty} {}
    W &u8(uint8_t v) { b += (char)v; return *this; }
    W &u16(uint16_t v) { return u8(v).u8(v >> 8); }
    W &u32(uint32_t v) { return u16(v).u16(v >> 16); }
    W &u64(uint64_t v) { return u32(v).u32(v >> 32); }
    W &str(const char *s) { size_t n = strnlen(s, 255); u8(n); b.append(s, n); return *this; }
    W &f32(float f) { uint32_t v; memcpy(&v, &f, 4); return u32(v); }
    W &input(const Input &i) { return u8(i.turn).u8(i.walk).u8(i.aim).u8(i.buttons).u8(i.flags); }
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
    Input input() { Input in; in.turn = u8(); in.walk = u8(); in.aim = u8(); in.buttons = u8(); in.flags = u8(); return in; }
};
}  // namespace wire

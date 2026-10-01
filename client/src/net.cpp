#include "net.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

enum : uint8_t {
    HELLO = 0x01, WELCOME, ERROR_, LIST_ROOMS = 0x10, ROOM_LIST, CREATE_ROOM, JOIN_ROOM, ROOM_STATE, LEAVE,
    START = 0x20, INPUTS, TURN_END, DESYNC, CHAT = 0x30, PING, PONG,
};
static const uint16_t VERSION = 1;

namespace {
struct W {
    std::string b;
    explicit W(uint8_t ty) : b{0, 0, (char)ty} {}
    W &u8(uint8_t v) { b += (char)v; return *this; }
    W &u16(uint16_t v) { return u8(v).u8(v >> 8); }
    W &u32(uint32_t v) { return u16(v).u16(v >> 16); }
    W &u64(uint64_t v) { return u32(v).u32(v >> 32); }
    W &str(const char *s) { size_t n = strnlen(s, 255); u8(n); b.append(s, n); return *this; }
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
    Input input() { Input in; in.turn = u8(); in.walk = u8(); in.aim = u8(); in.buttons = u8(); return in; }
};
}  // namespace

bool Net::connect(const char *host, int port, const char *name) {
#ifdef __SWITCH__
    static bool init = R_SUCCEEDED(socketInitializeDefault());
    if (!init) return false;
#endif
    close();
    char ports[8];
    snprintf(ports, sizeof ports, "%d", port);
    addrinfo hints = {}, *ai = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // ponytail: getaddrinfo blocks for a DNS name; pass an IP literal to keep the frame non-blocking
    if (getaddrinfo(host, ports, &hints, &ai) != 0 || !ai) return false;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) != 0 && errno != EINPROGRESS) close();
    }
    freeaddrinfo(ai);
    if (fd < 0) return false;
    connecting = true;
    send(W(HELLO).u16(VERSION).str(name).u64(token).done());
    return true;
}

void Net::close() {
    if (fd >= 0) ::close(fd);
    fd = -1;
    out.clear();
    in.clear();
    batch.clear();
}

void Net::fail(const char *why) {
    close();
    events.push_back({Disconnected, 0, why});
}

bool Net::next(Event &e) {
    if (events.empty()) return false;
    e = std::move(events.front());
    events.pop_front();
    return true;
}

bool Net::remoteInput(uint32_t tick, Input &outIn) {
    auto it = inbox.find(tick);
    if (it == inbox.end()) return false;
    outIn = it->second;
    inbox.erase(it);
    return true;
}

void Net::send(std::string frame) { out += frame; }

void Net::poll() {
    if (fd < 0) return;
    if (!batch.empty() && ++batchAge >= 3) flushInputs();
    if (connecting) {
        pollfd p = {fd, POLLOUT, 0};
        if (::poll(&p, 1, 0) <= 0) return;
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err) return fail(strerror(err));
        connecting = false;
    }
    while (!out.empty()) {
        ssize_t n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) return fail("send failed");
        out.erase(0, n);
    }
    for (;;) {
        char buf[4096];
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) return fail(n == 0 ? "server closed the connection" : "recv failed");
        in.append(buf, n);
    }
    size_t off = 0;
    while (in.size() - off >= 2) {
        const uint8_t *p = (const uint8_t *)in.data() + off;
        size_t len = p[0] | p[1] << 8;
        if (in.size() - off - 2 < len) break;
        if (len) handle(p + 2, len);
        off += 2 + len;
    }
    in.erase(0, off);
}

void Net::handle(const uint8_t *p, size_t n) {
    R r{p + 1, p + n};
    switch (p[0]) {
    case WELCOME:
        id = r.u32();
        token = r.u64();
        events.push_back({Welcome});
        break;
    case ERROR_: events.push_back({Error, 0, r.str()}); break;
    case ROOM_LIST: {
        rooms.clear();
        for (int i = 0, c = r.u8(); i < c && r.ok; i++) {
            NetRoom rm;
            rm.id = r.u32();
            rm.name = r.str();
            rm.players = r.u8();
            rm.maxPlayers = r.u8();
            rm.started = r.u8();
            rooms.push_back(rm);
        }
        events.push_back({RoomList});
        break;
    }
    case ROOM_STATE: {
        roomId = r.u32();
        hostId = r.u32();
        players.clear();
        for (int i = 0, c = r.u8(); i < c && r.ok; i++) {
            NetPlayer pl;
            pl.id = r.u32();
            pl.name = r.str();
            pl.online = r.u8();
            players.push_back(pl);
        }
        events.push_back({RoomState});
        break;
    }
    case START:
        cfg.seed = r.u32();
        cfg.teams = r.u8();
        cfg.wormsPerTeam = r.u8();
        owners.clear();
        for (int t = 0; t < cfg.teams && r.ok; t++) owners.push_back(r.u32());
        cfg.map = r.str();
        cfg.rules = r.u32();
        cfg.teamSetup.resize(r.u8());
        for (auto &t : cfg.teamSetup) { t.name = r.str(); t.cpu = r.u8(); t.voice = r.u8(); t.hat = r.u8(); }
        inbox.clear();
        batch.clear();
        events.push_back({Start});
        break;
    case INPUTS: {
        uint32_t first = r.u32();
        for (int i = 0, c = r.u8(); i < c && r.ok; i++) inbox[first + i] = r.input();
        break;
    }
    case DESYNC: events.push_back({Desync, r.u32()}); break;
    case CHAT: {
        uint32_t from = r.u32();
        events.push_back({Chat, from, r.str()});
        break;
    }
    case PONG: events.push_back({Pong, r.u32()}); break;
    }
}

void Net::flushInputs() {
    W w(INPUTS);
    w.u32(batchTick).u8(batch.size());
    for (const Input &i : batch) w.u8(i.turn).u8(i.walk).u8(i.aim).u8(i.buttons);
    send(w.done());
    batch.clear();
    batchAge = 0;
}

void Net::sendInput(uint32_t tick, const Input &i) {
    if (!batch.empty() && batchTick + batch.size() != tick) flushInputs();
    if (batch.empty()) batchTick = tick;
    batch.push_back(i);
    if (batch.size() == 255) flushInputs();
}

void Net::listRooms() { send(W(LIST_ROOMS).done()); }
void Net::createRoom(const char *name, int maxPlayers) { send(W(CREATE_ROOM).str(name).u8(maxPlayers).done()); }
void Net::joinRoom(uint32_t room) { send(W(JOIN_ROOM).u32(room).done()); }
void Net::leave() { send(W(LEAVE).done()); roomId = 0; players.clear(); }
void Net::turnEnd(uint32_t tick, uint32_t checksum) { send(W(TURN_END).u32(tick).u32(checksum).done()); }
void Net::chat(const char *text) { send(W(CHAT).str(text).done()); }
void Net::ping(uint32_t nonce) { send(W(PING).u32(nonce).done()); }

void Net::start(const GameConfig &c, const std::vector<uint32_t> &own) {
    W w(START);
    w.u32(c.seed).u8(c.teams).u8(c.wormsPerTeam);
    for (uint32_t o : own) w.u32(o);
    w.str(c.map.c_str()).u32(c.rules).u8((uint8_t)c.teamSetup.size());
    for (const auto &t : c.teamSetup) w.str(t.name.c_str()).u8(t.cpu).u8(t.voice).u8(t.hat);
    send(w.done());
}

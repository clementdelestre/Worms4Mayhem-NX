#include "lanhost.h"
#include "net.h"
#include "wire.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace wire;
static const uint32_t ROOM = 1;

static std::string error(const char *m) { return W(ERROR_).str(m).done(); }
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

bool LanHost::open(int p, const char *roomName) {
    close();
    if (!netInit() || (lfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return false;
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(p);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd, (sockaddr *)&a, sizeof a) != 0 || listen(lfd, 8) != 0) { close(); return false; }
    nonblock(lfd);
    if ((ufd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) setsockopt(ufd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one), nonblock(ufd);
    port = p;
    name = roomName;
    session = (uint32_t)(now() * 1e6) ^ (uint32_t)p << 16;  // ponytail: clock-seeded, only tells two hosts apart
    return true;
}

void LanHost::close() {
    for (auto &c : conns) ::close(c.first);
    if (lfd >= 0) ::close(lfd);
    if (ufd >= 0) ::close(ufd);
    lfd = ufd = -1;
    conns.clear(), players.clear(), room.clear(), sums.clear();
    start.clear(), log.clear();
    host = 0;
}

void LanHost::send(uint32_t id, const std::string &f) {
    auto p = players.find(id);
    if (p != players.end() && p->second.fd >= 0) conns[p->second.fd].out += f;
}

void LanHost::broadcast(const std::string &f, uint32_t except) {
    for (uint32_t p : room) if (p != except) send(p, f);
}

void LanHost::roomState() {
    W w(ROOM_STATE);
    w.u32(ROOM).u32(host).u8((uint8_t)room.size());
    for (uint32_t p : room) w.u32(p).str(players[p].name.c_str()).u8(players[p].fd >= 0);
    broadcast(w.done());
}

void LanHost::replay(uint32_t id) {
    if (start.empty()) return;
    send(id, start);
    send(id, W(REPLAY).u32((uint32_t)(log.size() / INPUT_BYTES)).done());
    for (size_t i = 0; i < log.size(); i += 255 * INPUT_BYTES) {
        size_t n = std::min<size_t>(255 * INPUT_BYTES, log.size() - i);
        W w(INPUTS);
        w.u32((uint32_t)(i / INPUT_BYTES)).u8((uint8_t)(n / INPUT_BYTES)).b.append(log, i, n);
        send(id, w.done());
    }
}

uint32_t LanHost::hello(int fd, const uint8_t *p, size_t n) {
    R r{p, p + n};
    uint8_t ty = r.u8();
    uint16_t ver = r.u16();
    std::string nm = r.str();
    uint64_t token = r.u64();
    if (!r.ok || ty != HELLO || ver != VERSION) { conns[fd].out += error("expected Hello with matching version"); return 0; }
    uint32_t id = 0;
    for (auto &pl : players) if (token && pl.second.token == token) id = pl.first;
    if (!id) {
        id = ++next;
        // ponytail: clock-mixed token, guessable on the LAN; fine for a couch room
        players[id] = {nm, ((uint64_t)session << 32 ^ (uint64_t)(now() * 1e9) * 2654435761u ^ id) | 1, fd};
    }
    Player &pl = players[id];
    pl.fd = fd;
    conns[fd].player = id;
    send(id, W(WELCOME).u32(id).u64(pl.token).done());
    if (pl.inRoom) roomState(), replay(id);
    return id;
}

bool LanHost::handle(uint32_t id, const uint8_t *p, size_t n) {
    R r{p, p + n};
    bool in = players[id].inRoom;
    switch (r.u8()) {
    case LIST_ROOMS:
        send(id, W(ROOM_LIST).u8(1).u32(ROOM).str(name.c_str()).u8((uint8_t)room.size()).u8(4).u8(!start.empty()).done());
        break;
    case CREATE_ROOM:  // one room: creating = joining it
    case JOIN_ROOM: {
        uint32_t rid = p[0] == JOIN_ROOM ? r.u32() : ROOM;
        if (!r.ok) return false;
        const char *e = rid != ROOM ? "no such room" : !start.empty() ? "match already started" : room.size() >= 4 ? "room full" : nullptr;
        if (in) break;
        if (e) send(id, error(e));
        else join(id);
        break;
    }
    case LEAVE: leave(id); break;
    case START: {
        r.u32();
        int teams = r.u8();
        r.u8();
        bool owned = true;
        for (int t = 0; t < teams; t++) owned &= std::count(room.begin(), room.end(), r.u32()) > 0;
        if (!r.ok || !in) return false;
        if (host != id) { send(id, error("only the host can start")); break; }
        if (teams < 2 || teams > 8 || !owned) return false;
        W w(START);
        w.b.append((const char *)p + 1, n - 1);
        start = w.done();
        log.clear(), sums.clear();
        broadcast(start);
        break;
    }
    case INPUTS: {
        uint32_t first = r.u32();
        size_t k = r.u8();
        const uint8_t *inputs = r.take(k * INPUT_BYTES);
        if (!r.ok || !in || start.empty()) return false;
        if (first != log.size() / INPUT_BYTES) {  // two clients played the same ticks: the log wins, resync the loser
            send(id, error(("Inputs tick " + std::to_string(first) + " != expected " + std::to_string(log.size() / INPUT_BYTES)).c_str()));
            replay(id);
            break;
        }
        log.append((const char *)inputs, k * INPUT_BYTES);
        W w(INPUTS);
        w.b.append((const char *)p + 1, n - 1);
        broadcast(w.done(), id);
        break;
    }
    case TURN_END: {
        uint32_t tick = r.u32(), sum = r.u32();
        if (!r.ok || !in) return false;
        if (sums.emplace(tick, sum).first->second != sum) broadcast(W(DESYNC).u32(tick).done());
        break;
    }
    case CHAT: {
        std::string text = r.str();
        if (!r.ok || !in) return false;
        broadcast(W(CHAT).u32(id).str(text.c_str()).done());
        break;
    }
    case PING: {
        uint32_t nonce = r.u32();
        if (!r.ok) return false;
        send(id, W(PONG).u32(nonce).done());
        break;
    }
    default: return false;
    }
    return true;
}

void LanHost::join(uint32_t id) {
    room.push_back(id);
    players[id].inRoom = true;
    if (room.size() == 1) host = id;
    roomState();
}

void LanHost::leave(uint32_t id) {
    Player &pl = players[id];
    if (!pl.inRoom) return;
    pl.inRoom = false;
    room.erase(std::remove(room.begin(), room.end(), id), room.end());
    if (host == id && !room.empty()) host = room[0];
    afterChange();
}

// Resets the room once nobody online is left in it (offline players go with it); else a dropped host hands over.
void LanHost::afterChange() {
    for (uint32_t p : room)
        if (players[p].fd >= 0) {
            if (players[host].fd < 0) host = p;
            return roomState();
        }
    for (uint32_t p : room) players.erase(p);
    room.clear(), sums.clear(), start.clear(), log.clear();
    host = 0;
}

void LanHost::disconnect(int fd) {
    uint32_t id = conns[fd].player;
    auto it = players.find(id);
    if (it == players.end() || it->second.fd != fd) return;  // a newer connection took this player over
    it->second.fd = -1;
    if (it->second.inRoom && !start.empty()) afterChange();
    else leave(id), players.erase(id);
}

void LanHost::beacon() {
    if (ufd < 0) return;
    W w(0);
    w.b = "W4NX";  // raw datagram, no frame header
    w.u16(VERSION).u16((uint16_t)port).u32(session).u8((uint8_t)room.size()).u8(4).u8(!start.empty()).str(name.c_str());
    std::vector<uint32_t> to = {htonl(INADDR_BROADCAST), htonl(INADDR_LOOPBACK)};  // loopback: a scanner on this machine
#ifdef __SWITCH__
    static bool nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    u32 ip = 0, mask = 0, gw, d1, d2;
    if (nifm && R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&ip, &mask, &gw, &d1, &d2)) && ip) to.push_back(ip | ~mask);  // subnet broadcast
#endif
    for (uint32_t addr : to) {
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(LAN_BEACON_PORT);
        a.sin_addr.s_addr = addr;
        sendto(ufd, w.b.data(), w.b.size(), 0, (sockaddr *)&a, sizeof a);
    }
}

void LanHost::poll() {
    if (lfd < 0) return;
    if (now() > beaconAt) beaconAt = now() + 1, beacon();
    for (int fd; (fd = accept(lfd, nullptr, nullptr)) >= 0;) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        nonblock(fd);
        conns[fd];
    }
    for (auto &c : conns) {
        int fd = c.first;
        for (;;) {
            char buf[4096];
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n <= 0) { c.second.dead = true; break; }
            c.second.in.append(buf, n);
        }
        std::string &in = c.second.in;
        size_t off = 0;
        while (!c.second.dead && in.size() - off >= 2) {
            const uint8_t *p = (const uint8_t *)in.data() + off;
            size_t len = p[0] | p[1] << 8;
            if (in.size() - off - 2 < len) break;
            off += 2 + len;
            if (!len) c.second.dead = true;
            else if (!c.second.player) c.second.dead = !hello(fd, p + 2, len);
            else if (!handle(c.second.player, p + 2, len)) send(c.second.player, error("bad message"));
        }
        in.erase(0, off);
    }
    for (auto it = conns.begin(); it != conns.end();) {
        Conn &c = it->second;
        while (!c.out.empty()) {
            ssize_t n = ::send(it->first, c.out.data(), c.out.size(), MSG_NOSIGNAL);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n <= 0) { c.dead = true; break; }
            c.out.erase(0, n);
        }
        if (!c.dead) { ++it; continue; }
        int fd = it->first;
        disconnect(fd);
        ::close(fd);
        it = conns.erase(it);
    }
}

#include "net.h"
#include "wire.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace wire;

bool netInit() {
#ifdef __SWITCH__
    static bool init = R_SUCCEEDED(socketInitializeDefault());
    return init;
#else
    return true;
#endif
}

bool Net::connect(const char *host, int port, const char *name) {
    if (!netInit()) return false;
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
    replay = UINT32_MAX;  // offline or reconnecting: nothing of ours to play until the server's Start
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
        cfg.scheme = Scheme{};  // absent (older host): defaults; extra bytes from a newer one are ignored
        if (r.ok && r.p < r.e)
            for (size_t i = 0, n = r.u8(); i < n && r.ok; i++) {
                uint8_t v = r.u8();
                if (i < sizeof(Scheme)) ((uint8_t *)&cfg.scheme)[i] = v;
            }
        cfg.wormpot = r.ok && r.p < r.e ? r.u32() : 0;
        cfg.custom.clear();
        for (int i = 0, n = r.ok && r.p < r.e ? r.u8() : 0; i < n && r.ok; i++) {
            WeaponDef w{};
            w.name = r.str();
            w.kind = (Kind)std::min<int>(r.u8(), (int)Kind::ChangeWorm);
            for (float *f : {&w.radius, &w.damage, &w.speed, &w.fuse, &w.bounce, &w.cradius, &w.cdamage, &w.poison, &w.spread}) *f = r.f32();
            for (int *v : {&w.count, &w.clusters, &w.shots, &w.weight}) *v = (int32_t)r.u32();
            uint8_t fl = r.u8();  // bit 0 wind, bit 1 HomingAvoidLand (older peers read a set byte as wind)
            w.wind = fl & 1, w.avoid = fl & 2;
            w.model = r.str();
            w.icon = r.str();
            if (r.ok) cfg.custom.push_back(w);
        }
        inbox.clear();
        batch.clear();
        replay = 0;
        events.push_back({Start});
        break;
    case INPUTS: {
        uint32_t first = r.u32();
        for (int i = 0, c = r.u8(); i < c && r.ok; i++) inbox[first + i] = r.input();
        break;
    }
    case DESYNC: events.push_back({Desync, r.u32()}); break;
    case REPLAY: replay = r.u32(); break;
    case CHAT: {
        uint32_t from = r.u32();
        std::string text = r.str();
        int voice = 0, hat = 0, cpus = 0, k = 0;
        if (text[0] != '\1') events.push_back({Chat, from, text});
        else if (sscanf(text.c_str() + 1, "%d %d %d %n", &voice, &hat, &cpus, &k) == 3)
            profiles[from] = {{text.substr(1 + k), 0, (uint8_t)voice, (uint8_t)hat}, cpus};
        break;
    }
    case PONG: events.push_back({Pong, r.u32()}); break;
    }
}

void Net::flushInputs() {
    W w(INPUTS);
    w.u32(batchTick).u8(batch.size());
    for (const Input &i : batch) w.input(i);
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
    w.u8(sizeof(Scheme));
    for (size_t i = 0; i < sizeof(Scheme); i++) w.u8(((const uint8_t *)&c.scheme)[i]);
    w.u32(c.wormpot).u8((uint8_t)std::min<size_t>(c.custom.size(), 255));
    for (size_t i = 0; i < c.custom.size() && i < 255; i++) {
        const WeaponDef &d = c.custom[i];
        w.str(d.name.c_str()).u8((uint8_t)d.kind);
        for (float f : {d.radius, d.damage, d.speed, d.fuse, d.bounce, d.cradius, d.cdamage, d.poison, d.spread}) w.f32(f);
        for (int v : {d.count, d.clusters, d.shots, d.weight}) w.u32((uint32_t)v);
        w.u8(d.wind | d.avoid << 1).str(d.model.c_str()).str(d.icon.c_str());
    }
    send(w.done());
}

void Net::sendProfile(const GameConfig::Team &t, int cpus) {
    char b[300];
    snprintf(b, sizeof b, "\1%d %d %d %s", t.voice, t.hat, cpus, t.name.c_str());
    chat(b);
}

void Net::startMatch(GameConfig c) {
    std::vector<GameConfig::Team> teams;
    std::vector<uint32_t> own;
    for (const NetPlayer &p : players) {
        if (own.size() == 4) break;
        auto it = profiles.find(p.id);
        GameConfig::Team t = p.id == id && !c.teamSetup.empty() ? c.teamSetup[0] : it != profiles.end() ? it->second.team : GameConfig::Team{p.name};
        t.cpu = 0;
        teams.push_back(t), own.push_back(p.id);
    }
    for (int k = 1; k < c.teams && k < (int)c.teamSetup.size() && own.size() < 4; k++) {
        GameConfig::Team t = c.teamSetup[k];
        t.cpu = std::max<uint8_t>(t.cpu, 1);
        teams.push_back(t), own.push_back(id);
    }
    c.teamSetup = teams;
    c.teams = (int)own.size();
    start(c, own);
}

bool LanScan::open() {
    if (fd >= 0) return true;
    if (!netInit() || (fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);  // several instances on one desktop
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(LAN_BEACON_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (sockaddr *)&a, sizeof a) != 0) { close(); return false; }
    return true;
}

void LanScan::close() {
    if (fd >= 0) ::close(fd);
    fd = -1;
    games.clear();
}

// Beacon: "W4NX", u16 version, u16 tcp port, u32 session, u8 players, u8 maxPlayers, u8 started, str name
void LanScan::poll(double now) {
    uint8_t buf[512];
    sockaddr_in from;
    socklen_t len = sizeof from;
    for (ssize_t n; fd >= 0 && (n = recvfrom(fd, buf, sizeof buf, 0, (sockaddr *)&from, &len)) > 0; len = sizeof from) {
        R r{buf, buf + n};
        if (n < 4 || memcmp(r.take(4), "W4NX", 4) || r.u16() != VERSION) continue;
        LanGame g;
        g.ip = inet_ntoa(from.sin_addr);
        g.port = r.u16();
        g.session = r.u32();
        g.players = r.u8(), g.maxPlayers = r.u8(), g.started = r.u8();
        g.name = r.str();
        g.seen = now;
        if (!r.ok) continue;
        auto it = std::find_if(games.begin(), games.end(), [&](const LanGame &o) { return o.session == g.session; });
        if (it == games.end()) games.push_back(g);
        else g.ip = it->ip, *it = g;  // first address heard wins (loopback and broadcast copies)
    }
    games.erase(std::remove_if(games.begin(), games.end(), [&](const LanGame &g) { return now - g.seen > 3; }), games.end());
}

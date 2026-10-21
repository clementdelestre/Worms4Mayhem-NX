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
        auto gone = [&](uint32_t c) { return std::none_of(players.begin(), players.end(), [&](const NetPlayer &p) { return p.id == c; }); };
        for (auto it = consoleTeams.begin(); it != consoleTeams.end();) it = gone(it->first) ? consoleTeams.erase(it) : std::next(it);
        syncLobby();
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
        for (auto &t : cfg.teamSetup) t = getTeam(r);
        cfg.scheme = getScheme(r);
        cfg.wormpot = r.ok && r.p < r.e ? r.u32() : 0;
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
        events.push_back({Chat, from, r.str()});
        break;
    }
    case TEAMS: {
        uint32_t c = r.u32();
        std::vector<GameConfig::Team> t;
        for (int i = 0, n = r.u8(); i < n && r.ok; i++) t.push_back(getTeam(r));
        if (!r.ok) break;
        consoleTeams[c] = t;
        syncLobby();
        events.push_back({TeamsChanged, c});
        break;
    }
    case LOBBY: {
        Lobby l;
        std::vector<std::pair<uint32_t, uint8_t>> order;
        for (int i = 0, n = r.u8(); i < n && r.ok; i++) { uint32_t c = r.u32(); order.push_back({c, r.u8()}); }
        l.map = r.str();
        l.rules = r.u32();
        l.scheme = getScheme(r);
        l.wormpot = r.u32();
        l.wormsPerTeam = r.u8();
        for (int i = 0, n = r.u8(); i < n && r.ok; i++) l.ready.push_back(r.u32());
        if (!r.ok) break;
        for (auto &o : order) l.teams.push_back({o.first, o.second, {}});
        lobby = l;
        syncLobby();
        events.push_back({LobbyChanged});
        break;
    }
    case READY: {
        uint32_t c = r.u32();
        bool on = r.u8();
        if (!r.ok || !isHost()) break;
        auto it = std::find(lobby.ready.begin(), lobby.ready.end(), c);
        if (on && it == lobby.ready.end()) lobby.ready.push_back(c);
        if (!on && it != lobby.ready.end()) lobby.ready.erase(it);
        pushLobby();
        events.push_back({LobbyChanged});
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
void Net::leave() { send(W(LEAVE).done()); roomId = 0; players.clear(); consoleTeams.clear(); lobby = {}; }
void Net::turnEnd(uint32_t tick, uint32_t checksum) { send(W(TURN_END).u32(tick).u32(checksum).done()); }
void Net::chat(const char *text) { send(W(CHAT).str(text).done()); }
void Net::ping(uint32_t nonce) { send(W(PING).u32(nonce).done()); }

void Net::start(const GameConfig &c, const std::vector<uint32_t> &own) {
    W w(START);
    w.u32(c.seed).u8(c.teams).u8(c.wormsPerTeam);
    for (uint32_t o : own) w.u32(o);
    w.str(c.map.c_str()).u32(c.rules).u8((uint8_t)c.teamSetup.size());
    for (const auto &t : c.teamSetup) putTeam(w, t);
    putScheme(w, c.scheme);
    w.u32(c.wormpot);
    send(w.done());
}

bool Net::allReady() const {
    return std::all_of(players.begin(), players.end(), [&](const NetPlayer &p) { return isReady(p.id); });
}

void Net::sendTeams(const std::vector<GameConfig::Team> &mine) {
    W w(TEAMS);
    w.u8((uint8_t)std::min<size_t>(mine.size(), 255));
    for (size_t i = 0; i < mine.size() && i < 255; i++) putTeam(w, mine[i]);
    send(w.done());
    consoleTeams[id] = mine;
    syncLobby();
}

void Net::pushLobby() {
    W w(LOBBY);
    w.u8((uint8_t)lobby.teams.size());
    for (const LobbyTeam &t : lobby.teams) w.u32(t.owner).u8(t.idx);
    w.str(lobby.map.c_str()).u32(lobby.rules);
    putScheme(w, lobby.scheme);
    w.u32(lobby.wormpot).u8(lobby.wormsPerTeam).u8((uint8_t)lobby.ready.size());
    for (uint32_t c : lobby.ready) w.u32(c);
    send(w.done());
}

void Net::setReady(bool on) { send(W(READY).u8(on).done()); }

void Net::syncLobby() {
    auto known = [&](uint32_t c) { return std::any_of(players.begin(), players.end(), [&](const NetPlayer &p) { return p.id == c; }); };
    auto resolve = [&](LobbyTeam &t) {
        auto it = consoleTeams.find(t.owner);
        if (it == consoleTeams.end() || t.idx >= it->second.size()) return false;
        t.team = it->second[t.idx];
        return true;
    };
    auto before = lobby;
    lobby.teams.erase(std::remove_if(lobby.teams.begin(), lobby.teams.end(), [&](LobbyTeam &t) { return !known(t.owner) || !resolve(t); }), lobby.teams.end());
    if (!isHost()) return;
    for (const NetPlayer &p : players) {
        auto it = consoleTeams.find(p.id);
        for (size_t i = 0; it != consoleTeams.end() && i < it->second.size() && lobby.teams.size() < 4; i++) {
            if (std::any_of(lobby.teams.begin(), lobby.teams.end(), [&](const LobbyTeam &t) { return t.owner == p.id && t.idx == i; })) continue;
            lobby.teams.push_back({p.id, (uint8_t)i, it->second[i]});
        }
    }
    lobby.ready.erase(std::remove_if(lobby.ready.begin(), lobby.ready.end(), [&](uint32_t c) { return !known(c); }), lobby.ready.end());
    auto same = [&](const Lobby &a, const Lobby &b) {
        return a.ready == b.ready && a.teams.size() == b.teams.size() &&
               std::equal(a.teams.begin(), a.teams.end(), b.teams.begin(), [](const LobbyTeam &x, const LobbyTeam &y) { return x.owner == y.owner && x.idx == y.idx; });
    };
    if (!same(before, lobby)) pushLobby();
}

void Net::startMatch(uint32_t seed) {
    GameConfig c;
    c.seed = seed;
    c.map = lobby.map;
    c.rules = lobby.rules;
    c.scheme = lobby.scheme;
    c.wormpot = lobby.wormpot;
    c.wormsPerTeam = lobby.wormsPerTeam;
    std::vector<uint32_t> own;
    for (const LobbyTeam &t : lobby.teams) c.teamSetup.push_back(t.team), own.push_back(t.owner);
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

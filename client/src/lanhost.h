#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The Rust relay (server/src/lib.rs) for one room, embedded in the hosting console: same wire format, so the host's
// own Net connects to it over 127.0.0.1. Beacons the room on UDP LAN_BEACON_PORT every second. No threads: poll() per frame.
struct LanHost {
    bool open(int port, const char *roomName);  // false if the TCP port cannot be bound
    void close();
    bool running() const { return lfd >= 0; }
    void poll();

private:
    struct Conn { std::string in, out; uint32_t player = 0; bool dead = false; };
    struct Player { std::string name; uint64_t token; int fd; bool inRoom = false; };  // fd -1 = offline
    int lfd = -1, ufd = -1, port = 0;
    uint32_t next = 0, session = 0, host = 0;
    double beaconAt = 0;
    std::string name, start, log;  // start: Start frame as broadcast; log: wire::INPUT_BYTES per tick
    std::vector<uint32_t> room;    // players in slot order
    std::map<uint32_t, uint32_t> sums;
    std::map<int, Conn> conns;
    std::map<uint32_t, Player> players;

    void send(uint32_t id, const std::string &f);
    void broadcast(const std::string &f, uint32_t except = 0);
    void roomState();
    void replay(uint32_t id);
    uint32_t hello(int fd, const uint8_t *p, size_t n);
    bool handle(uint32_t id, const uint8_t *p, size_t n);
    void join(uint32_t id);
    void leave(uint32_t id);
    void afterChange();
    void disconnect(int fd);
    void beacon();
};

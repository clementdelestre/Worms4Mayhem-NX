#pragma once
#include "sim.h"
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

struct NetPlayer { uint32_t id; std::string name; bool online; };
struct NetRoom { uint32_t id; std::string name; int players, maxPlayers; bool started; };

// Non-blocking TCP client for PROTOCOL.md, no threads: call poll() once per frame.
struct Net {
    enum Ev : uint8_t { Welcome, Error, RoomList, RoomState, Start, Desync, Chat, Pong, Disconnected };
    struct Event { Ev type; uint32_t a = 0; std::string text; };  // a = Desync tick / Chat sender / Pong nonce

    // Server-provided state, valid after the matching event.
    uint32_t id = 0;
    uint64_t token = 0;  // kept across connect() calls: reconnecting with it resumes the match
    std::vector<NetRoom> rooms;
    uint32_t roomId = 0, hostId = 0;
    std::vector<NetPlayer> players;
    GameConfig cfg;
    std::vector<uint32_t> owners;  // owners[team] = player id
    uint32_t replay = 0;           // ticks the server replays after Start: never play our own input below it

    bool connect(const char *host, int port, const char *name);
    void close();
    bool online() const { return fd >= 0; }
    void poll();
    bool next(Event &e);
    bool remoteInput(uint32_t tick, Input &out);  // input for `tick` from the server, consumed

    void listRooms();
    void createRoom(const char *name, int maxPlayers);
    void joinRoom(uint32_t room);
    void leave();
    void start(const GameConfig &cfg, const std::vector<uint32_t> &owners);  // owners[team]
    void sendInput(uint32_t tick, const Input &in);  // batched, flushed by poll()
    void turnEnd(uint32_t tick, uint32_t checksum);
    void chat(const char *text);
    void ping(uint32_t nonce);

private:
    int fd = -1;
    bool connecting = false;
    std::string out, in;
    std::deque<Event> events;
    std::map<uint32_t, Input> inbox;
    std::vector<Input> batch;
    uint32_t batchTick = 0;
    int batchAge = 0;

    void send(std::string frame);
    void flushInputs();
    void fail(const char *why);
    void handle(const uint8_t *p, size_t n);
};

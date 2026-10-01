// Two instances of this play scripted turns through the server with the real sim:
//   interop host 127.0.0.1 7777 & interop guest 127.0.0.1 7777
#include "net.h"
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 4) return fprintf(stderr, "usage: interop host|guest ADDR PORT\n"), 2;
    bool host = !strcmp(argv[1], "host");
    Net net;
    if (!net.connect(argv[2], atoi(argv[3]), argv[1])) return fprintf(stderr, "connect failed\n"), 1;
    Game game;
    bool playing = false;
    uint32_t tick = 0, turnTick = 0, turns = 0;
    for (long iter = 0; iter < 50000000; iter++) {
        net.poll();
        Net::Event e;
        while (net.next(e)) switch (e.type) {
        case Net::Welcome: host ? net.createRoom("interop", 2) : net.listRooms(); break;
        case Net::RoomList:
            if (net.rooms.empty()) net.listRooms(); else net.joinRoom(net.rooms[0].id);
            break;
        case Net::RoomState:
            if (host && !playing && net.players.size() == 2) net.start({1234, 2, 1, "", 0}, {net.players[0].id, net.players[1].id});
            break;
        case Net::Start: game.start(net.cfg); playing = true; break;
        case Net::Desync: return printf("[%s] DESYNC at tick %u\n", argv[1], e.a), 1;
        case Net::Error: printf("[%s] server error: %s\n", argv[1], e.text.c_str()); break;
        case Net::Disconnected: return printf("[%s] disconnected: %s\n", argv[1], e.text.c_str()), 1;
        default: break;
        }
        if (!playing) continue;
        // Same rule as the game loop: buffered input first, else produce if it's our turn, else wait.
        for (int n = 0; n < 8; n++) {
            Input in;
            bool mine = net.owners[game.worms[game.current].team] == net.id;
            if (!net.remoteInput(tick, in)) {
                if (!mine) break;
                uint32_t t = tick - turnTick;
                in.walk = t < 60 ? 1 : 0;
                in.aim = t < 40 ? 1 : 0;
                in.buttons = t >= 60 && t < 90 ? Input::FIRE : 0;
                net.sendInput(tick, in);
            }
            Phase before = game.phase;
            game.step(in);
            tick++;
            if (game.phase == Phase::Aim && before != Phase::Aim) {
                net.turnEnd(tick, game.checksum());
                turnTick = tick;
                printf("[%s] turn %u ended at tick %u checksum %08x\n", argv[1], ++turns, tick, game.checksum());
                if (turns == 4) {
                    for (int k = 0; k < 10; k++) net.poll();  // flush last batch + TurnEnd
                    return 0;
                }
            }
        }
    }
    return printf("[%s] timed out at tick %u\n", argv[1], tick), 1;
}

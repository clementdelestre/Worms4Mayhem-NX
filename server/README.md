# worms4nx-server

Lobby + input relay for Worms4NX (protocol: `../PROTOCOL.md`). It does not simulate the game:
clients run the deterministic sim, the server relays the active player's inputs, logs them for
reconnects and compares end-of-turn checksums.

```sh
cargo run --release -- 0.0.0.0:7777        # or W4NX_BIND=0.0.0.0:7777; default 0.0.0.0:7777
cargo test                                 # lobby, start, relay, desync, reconnect replay
docker build -t worms4nx-server . && docker run -p 7777:7777 worms4nx-server
```

C++ interop check (real sim, two clients through the server):

```sh
g++ -std=c++17 -O2 -I../client/src -I../third_party/raylib-nx/src interop/interop.cpp \
  ../client/src/{net,sim,terrain}.cpp -L../third_party/raylib-nx/out/desktop \
  -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -o /tmp/interop
cargo run & /tmp/interop host 127.0.0.1 7777 & /tmp/interop guest 127.0.0.1 7777
```

State is in memory only; a restart drops all rooms.

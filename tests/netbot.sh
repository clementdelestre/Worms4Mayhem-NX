#!/bin/bash
# Two real game clients (--netbot, AI plays each owner's team) through a local server; fails on Desync or any turn-end
# tick whose checksum differs between the clients' logs (keyed by tick: a resync restarts the turn count).
# Usage: tests/netbot.sh [turns] [map] [rules] [scheme] [roundMin]
# Env: W4NX=client binary (built if unset), PORT, W4NX_SPEED=ticks per frame (8),
#      KILL=a|b + KILLAT=s + OFFLINE=s: kill that bot mid-match, restart it later (resumes with its token).
#      LAN=1: no server, bot a hosts the embedded LAN relay on PORT and b finds it by UDP beacon (KILL=b only).
#      W4NX_CPU=level: the host adds one CPU team.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
PORT=${PORT:-7790}
TURNS=${1:-6}
shift || true
[ -n "${LAN:-}" ] || (cd "$ROOT/server" && cargo build --release -q) || exit 2
if [ -z "${W4NX:-}" ]; then
    W4NX=$W/w4nx
    (cd "$ROOT/client" && g++ -std=c++17 -O2 -I../third_party/raylib-nx/src src/*.cpp -o "$W4NX" -L../third_party/raylib-nx/out/desktop \
        -lraylib -lGL -lm -lpthread -ldl -lrt -lX11) || exit 2
fi
for d in a b; do mkdir -p "$W/$d"; ln -s "$ROOT/client/romfs" "$W/$d/romfs"; ln -s "$ROOT/client/assets" "$W/$d/assets"; done
SERVER=
HOST=lan
if [ -z "${LAN:-}" ]; then
    "$ROOT/server/target/release/worms4nx-server" "127.0.0.1:$PORT" > "$W/server.log" 2>&1 &
    SERVER=$!
    HOST=127.0.0.1
    sleep 0.3
fi
bot() { cd "$W/$1" && exec timeout 900 "$W4NX" --netbot "$HOST" "$PORT" "$1" "$2" "$TURNS" "${@:3}" >> "$W/$1.log" 2>&1; }
botargs=("$@")
bot a create "${botargs[@]}" & A=$!
bot b join "${botargs[@]}" & B=$!
if [ -n "${KILL:-}" ]; then
    sleep "${KILLAT:-20}"
    victim=$([ "$KILL" = a ] && echo $A || echo $B)
    pkill -9 -P "$victim"; wait "$victim" 2>/dev/null
    echo "== killed $KILL, offline ${OFFLINE:-3}s" | tee -a "$W/$KILL.log"
    sleep "${OFFLINE:-3}"
    if [ "$KILL" = a ]; then bot a create "${botargs[@]}" & A=$!; else bot b join "${botargs[@]}" & B=$!; fi
fi
wait $A; ea=$?
wait $B; eb=$?
[ -n "$SERVER" ] && kill "$SERVER"
grep -h 'turn .* checksum' "$W/a.log" "$W/b.log" | awk '{ if (!($5 in t)) t[$5] = $7; else if (t[$5] != $7) { print "MISMATCH tick", $5, t[$5], "vs", $7; bad = 1 } } END { exit bad }'
em=$?
tail -n 3 "$W/a.log" "$W/b.log"
echo "exit a=$ea b=$eb checksums=$em  logs: $W"
[ $ea = 0 ] && [ $eb = 0 ] && [ $em = 0 ]

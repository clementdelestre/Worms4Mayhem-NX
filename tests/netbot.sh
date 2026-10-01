#!/bin/bash
# Two real game clients (--netbot, AI plays each owner's team) through a local server; fails on Desync or any turn
# whose checksum differs between the clients' logs.
# Usage: tests/netbot.sh [turns] [map] [rules] [scheme] [roundMin]
# Env: W4NX=client binary (built if unset), PORT, W4NX_SPEED=ticks per frame (8),
#      KILL=a|b + KILLAT=s + OFFLINE=s: kill that bot mid-match, restart it later (resumes with its token).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
PORT=${PORT:-7790}
TURNS=${1:-6}
shift || true
(cd "$ROOT/server" && cargo build --release -q) || exit 2
if [ -z "${W4NX:-}" ]; then
    W4NX=$W/w4nx
    (cd "$ROOT/client" && g++ -std=c++17 -O2 -I../third_party/raylib-nx/src src/*.cpp -o "$W4NX" -L../third_party/raylib-nx/out/desktop \
        -lraylib -lGL -lm -lpthread -ldl -lrt -lX11) || exit 2
fi
for d in a b; do mkdir -p "$W/$d"; ln -s "$ROOT/client/romfs" "$W/$d/romfs"; ln -s "$ROOT/client/assets" "$W/$d/assets"; done
"$ROOT/server/target/release/worms4nx-server" "127.0.0.1:$PORT" > "$W/server.log" 2>&1 &
SERVER=$!
sleep 0.3
bot() { cd "$W/$1" && exec timeout 900 "$W4NX" --netbot 127.0.0.1 "$PORT" "$1" "$2" "$TURNS" "${@:3}" >> "$W/$1.log" 2>&1; }
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
kill "$SERVER"
grep -h 'turn .* checksum' "$W/a.log" "$W/b.log" | awk '{ v = $5 " " $7; if (!($3 in t)) t[$3] = v; else if (t[$3] != v) { print "MISMATCH turn", $3, t[$3], "vs", v; bad = 1 } } END { exit bad }'
em=$?
tail -n 3 "$W/a.log" "$W/b.log"
echo "exit a=$ea b=$eb checksums=$em  logs: $W"
[ $ea = 0 ] && [ $eb = 0 ] && [ $em = 0 ]

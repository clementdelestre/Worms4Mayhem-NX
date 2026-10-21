#!/bin/bash
# Two real game clients (--netbot, AI plays each owner's team) through a local server; fails on Desync or any turn-end
# tick whose checksum differs between the clients' logs (keyed by tick: a resync restarts the turn count).
# Usage: tests/netbot.sh [turns] [map] [rules] [scheme] [roundMin]
# Env: W4NX=client binary (built if unset), PORT, W4NX_SPEED=ticks per frame (8),
#      KILL=a|b + KILLAT=s + OFFLINE=s: kill that bot mid-match, restart it later (resumes with its token).
#      LAN=1: no server, bot a hosts the embedded LAN relay on PORT and b finds it by UDP beacon (KILL=b only).
#      W4NX_CPU=level: the host adds one CPU team.
#      W4NX_LOBBY_TIMER=s: the W4M lobby timer (default here 4 s, game 30 s): the host is ready only when it ends.
# With a rules value (3rd argument, non-zero) it also checks the live lobby: b edits its team, a sees it then pushes map and rules, b sees them.
set -u
export W4NX_HIDDEN=1  # hidden window, silent audio
export W4NX_LOBBY_TIMER=${W4NX_LOBBY_TIMER:-4}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
PORT=${PORT:-7790}
TURNS=${1:-6}
shift || true
[ -n "${LAN:-}" ] || (cd "$ROOT/server" && cargo build --release -q) || exit 2
if [ -z "${W4NX:-}" ]; then
    W4NX=$ROOT/client/worms4nx
    make -s -C "$ROOT/client" worms4nx > /dev/null || exit 2
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
lob=0
if [ "${botargs[1]:-0}" != 0 ]; then
    grep -q "lobby shows the guest's edited team" "$W/a.log" && grep -q "lobby shows the host's rules" "$W/b.log" || { echo "LOBBY: live edits not seen"; lob=1; }
fi
pan=0
for d in a b; do grep -q "sees the remote panel" "$W/$d.log" || { echo "PANEL: $d never saw the remote weapon panel"; pan=1; }; done
tail -n 3 "$W/a.log" "$W/b.log"
echo "exit a=$ea b=$eb checksums=$em lobby=$lob panel=$pan  logs: $W"
[ $ea = 0 ] && [ $eb = 0 ] && [ $em = 0 ] && [ $lob = 0 ] && [ $pan = 0 ]

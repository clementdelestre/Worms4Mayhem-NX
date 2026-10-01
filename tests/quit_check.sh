#!/bin/bash
# Pause -> Quit to menu must stay on the menu: title, Quick match, pause, Quit (Enter), then arrows; fails on a 2nd Play.
# Usage: tests/quit_check.sh   Env: W4NX=client binary (built if unset)
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
if [ -z "${W4NX:-}" ]; then
    W4NX=$W/w4nx
    (cd "$ROOT/client" && g++ -std=c++17 -O2 -I../third_party/raylib-nx/src src/*.cpp -o "$W4NX" -L../third_party/raylib-nx/out/desktop \
        -lraylib -lGL -lm -lpthread -ldl -lrt -lX11) || exit 2
fi
ln -s "$ROOT/client/romfs" "$W/romfs"; ln -s "$ROOT/client/assets" "$W/assets"
ENTER=257 ESC=256 RIGHT=262 LEFT=263 DOWN=264 UP=265
tap() { echo "$1 $2 1"; echo "$(($1 + 3)) $2 0"; }
{
    tap 30 $ENTER; tap 60 $ENTER                      # title -> main -> Quick match
    tap 240 $ESC; tap 270 $DOWN; tap 290 $DOWN; tap 320 $ENTER  # pause, Quit to menu
    f=360; for k in $UP $DOWN $LEFT $RIGHT $UP $UP $DOWN; do tap $f $k; f=$((f + 25)); done
    echo "600 -1 0"
} > "$W/script"
(cd "$W" && W4NX_INPUTSCRIPT=script timeout 120 "$W4NX" > log 2>&1)
grep '^frame' "$W/log"
# screens: 0 Menu, 2 Play
[ "$(grep '^frame' "$W/log" | awk '{print $4}' | tr '\n' ' ')" = "0 2 0 " ] && echo PASS && exit 0
echo FAIL; exit 1

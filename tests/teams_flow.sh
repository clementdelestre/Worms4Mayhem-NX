#!/bin/bash
# Title -> Main -> Local -> Custom game with two saved teams placed: the worm previews must not corrupt rlgl's matrices
# (the frontend shrank frame after frame). Fails when the title text is missing on a later Teams frame.
# Usage: tests/teams_flow.sh   Env: W4NX=client binary (built if unset)
set -u
export W4NX_HIDDEN=1 LANGUAGE=fr LC_ALL=fr_FR.UTF-8  # hidden window, silent audio
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d)
if [ -z "${W4NX:-}" ]; then
    W4NX=$ROOT/client/worms4nx
    make -s -C "$ROOT/client" worms4nx > /dev/null || exit 2
fi
ln -s "$ROOT/client/romfs" "$W/romfs"; ln -s "$ROOT/client/assets" "$W/assets"
printf 'slot 0 0 4 0 0 Gunslingers\nslot 1 1 4 1 0 Spacemen\n' > "$W/setup.txt"
ENTER=257 DOWN=264
tap() { echo "$1 $2 1"; echo "$(($1 + 3)) $2 0"; }
{ tap 30 $ENTER; tap 70 $ENTER; tap 110 $DOWN; tap 130 $ENTER; echo "420 -1 0"; } > "$W/script"
(cd "$W" && W4NX_CAPFRAMES="200 400" W4NX_INPUTSCRIPT=script timeout 120 "$W4NX" > log 2>&1)
python3 -W ignore - "$W" <<'PY' && echo PASS && exit 0
import sys
from PIL import Image
for f in (200, 400):
    im = Image.open(f"{sys.argv[1]}/cap_{f}.png").convert("RGB").crop((200, 50, 750, 100))  # Title Control, Button_Yellow
    n = sum(1 for r, g, b in im.getdata() if r > 200 and g > 180 and b < 160)
    print(f"frame {f}: {n} title pixels")
    if n < 1500: sys.exit(1)
PY
echo FAIL; exit 1

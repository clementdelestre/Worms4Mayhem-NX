#!/usr/bin/env bash
# Builds an installable application NSP for Worms4NX:
#   client elf (separate BUILD/TARGET, default .nro build untouched)
#     -> nso (elf2nso) + npdm (npdmtool, tools/nsp/worms4nx.json)
#     -> nacp (nacptool) + icon (client/icon.jpg, already 256x256)
#     -> hacBrewPack (built from source, cached) packs romfs + exefs + control into a signed/encrypted NSP.
# Usage: tools/nsp/build-nsp.sh [path-to-prod.keys]
set -euo pipefail

TITLE_ID=0100576F524D0000
TARGET=worms4nx_nsp
BUILD_DIR=build_nsp
NSP_SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$NSP_SRC_DIR/out"

if [ ! -d /opt/devkitpro ]; then
    # Host side: re-exec this same script inside the devkitpro toolchain container.
    REPO_ROOT="$(cd "$NSP_SRC_DIR/../.." && pwd)"
    KEYS="${1:-$HOME/.var/app/io.github.ryubing.Ryujinx/config/Ryujinx/system/prod.keys}"
    [ -f "$KEYS" ] || { echo "prod.keys not found: $KEYS" >&2; exit 1; }
    exec docker run --rm -u "$(id -u):$(id -g)" \
        -v "$REPO_ROOT":/w -w /w \
        -v "$KEYS":/keys/prod.keys:ro \
        devkitpro/devkita64 bash tools/nsp/build-nsp.sh /keys/prod.keys
fi

# --- inside the container from here ---
KEYS="${1:?prod.keys path required}"
# hacBrewPack 3.05 rejects values that aren't 32 hex digits (newer keysets add longer ones); feed it a filtered copy
HBP_KEYS=$(mktemp); trap 'rm -f "$HBP_KEYS"' EXIT
awk -F' *= *' '{ sub(/\r$/, "") } $1 == "header_key" || (length($2) == 32 && $2 ~ /^[0-9a-fA-F]+$/)' "$KEYS" > "$HBP_KEYS"
mkdir -p "$OUT/exefs" "$OUT/control"

echo "== 1/4 building elf (BUILD=$BUILD_DIR TARGET=$TARGET, isolated from the .nro build) =="
make -C client -f Makefile.switch BUILD="$BUILD_DIR" TARGET="$TARGET" NO_ICON=1 NO_NACP=1

echo "== 2/4 nso + npdm + nacp + icon =="
elf2nso "client/$TARGET.elf" "$OUT/exefs/main"
npdmtool "$NSP_SRC_DIR/worms4nx.json" "$OUT/exefs/main.npdm"
nacptool --create "Worms4NX" "cdelestre" "0.1.0" "$OUT/control/control.nacp" --titleid="$TITLE_ID"
# assets/ui/icon.jpg is built from the user's own W4M install (never committed); fall back to the bundled icon
ICON=client/assets/ui/icon.jpg; [ -f "$ICON" ] || ICON=client/icon.jpg
cp "$ICON" "$OUT/control/icon_AmericanEnglish.dat"

echo "== 3/4 hacBrewPack (build from source, cached in tools/nsp/out) =="
HBP_BIN="$OUT/hacbrewpack-bin/hacbrewpack"
if [ ! -x "$HBP_BIN" ]; then
    rm -rf "$OUT/hacbrewpack-src"
    git clone --depth 1 https://github.com/rlaphoenix/hacBrewPack.git "$OUT/hacbrewpack-src"
    cp "$OUT/hacbrewpack-src/config.mk.template" "$OUT/hacbrewpack-src/config.mk"
    make -C "$OUT/hacbrewpack-src" CC=gcc
    mkdir -p "$(dirname "$HBP_BIN")"
    cp "$OUT/hacbrewpack-src/hacbrewpack" "$HBP_BIN"
fi

echo "== 4/4 packing NSP =="
rm -rf "$OUT/nsp" "$OUT/hacbrewpack_nca" "$OUT/hacbrewpack_temp" "$OUT/hacbrewpack_backup"
"$HBP_BIN" -k "$HBP_KEYS" \
    --exefsdir="$OUT/exefs" \
    --controldir="$OUT/control" \
    --romfsdir=client/romfs \
    --nologo \
    --keepncadir \
    --nspdir="$OUT/nsp" \
    --ncadir="$OUT/hacbrewpack_nca" \
    --tempdir="$OUT/hacbrewpack_temp" \
    --backupdir="$OUT/hacbrewpack_backup" \
    --titleid="$TITLE_ID" 2>&1 | grep -vi 'key area'

NSP_FILE=$(ls "$OUT"/nsp/*.nsp | head -1)
echo "NSP: $NSP_FILE"

echo "== verify (hactool, never prints key material) =="
{
    hactool --intype=pfs0 -i -k "$HBP_KEYS" "$NSP_FILE"
    for nca in "$OUT"/hacbrewpack_nca/*.nca; do
        echo "--- $(basename "$nca") ---"
        hactool --intype=nca -y -k "$HBP_KEYS" "$nca"
    done
} 2>&1 | grep -vi 'key' | tee "$OUT/verify.log" || true  # best-effort check, the NSP is already built

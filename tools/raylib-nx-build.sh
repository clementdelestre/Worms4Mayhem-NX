#!/usr/bin/env bash
# Fetches raylib-nx at the pinned revision, applies tools/patches and builds the Switch lib (third_party/raylib-nx/out/nx).
set -euo pipefail
REV=ebc44b89ce5d0a5f8bd6aa33c37df3cfe18d7a14
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ ! -d /opt/devkitpro ]; then
    exec docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT":/w -w /w devkitpro/devkita64 bash tools/raylib-nx-build.sh
fi
cd "$ROOT"
D=third_party/raylib-nx
if [ ! -d "$D" ]; then
    git init -q "$D"
    git -C "$D" fetch -q --depth 1 https://github.com/luizpestana/raylib-nx "$REV"
    git -C "$D" checkout -q FETCH_HEAD
    for p in tools/patches/raylib-nx-*.patch; do git -C "$D" apply "../../$p"; done
fi
mkdir -p "$D/out/nx"
make -C "$D/src" PLATFORM=PLATFORM_NX RAYLIB_RELEASE_PATH=../out/nx CUSTOM_CFLAGS=-DNX_DISABLE_GAMEPAD_EMULATION -j"$(nproc)"

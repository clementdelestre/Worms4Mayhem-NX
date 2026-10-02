# Importing Worms 4 Mayhem assets

Every importer reads your own install and writes to `client/assets/` (gitignored: never commit or redistribute the output).
Default install path in the examples: the Steam `WormsXHD` folder (Worms Ultimate Mayhem).

| tool | command | writes | format notes |
|---|---|---|---|
| `tools/w4m-import` | `w4m-import [--list] [--raw] <W4M dir> [out = client/assets]` | `sfx/`, `voices/<bank>/`, `music/` (below) | this file |
| `tools/w4m-models` | `w4m-models <W4M dir> [out = client/assets/models]`; `w4m-models --list <one bundle .xom>` dumps its meshes and clips (`W4M_CHANNELS`, `W4M_KEYS`, `W4M_GROUPS`, `W4M_IMG_DIR` add detail) | `models/*.glb` (worm with skin and clips, weapons, hats, decor, frontend scene) | `w4m-formats.md` "Meshes" |
| `tools/w4m-maps` | `w4m-maps <W4M dir> [out = client/assets/maps] [map stems...]` | `maps/<name>.json` + `.vox`, textures, `missions/` | `maps.md`, `missions.md`, `w4m-formats.md` |
| `tools/w4m-ui` | `w4m-ui <W4M dir> [out = client/assets/ui]` | `ui/` (fe, fe2, hud, sky...) PNGs, `lang/<code>.txt` | `w4m-formats.md` "Frontend / HUD art" |
| `tools/w4m-re/acting.py` | `acting.py [out = client/assets/acting.txt]` | the WORMACTING scenes | `worm-reactions.md` |

Build each Rust tool with `cargo build --release --manifest-path tools/<tool>/Cargo.toml`. `--list` of `w4m-models` takes exactly one
bundle file: with any other argument count `--list` is taken as the install dir and the next argument as the out dir (`main()`). Then copy `client/assets/` to the console (`README.md`
"Deploy", `tools/sync-switch.sh`).

## Audio (`tools/w4m-import`)

Worms4NX ships only CC0 placeholder sounds (`client/romfs/`). If you own Worms 4 Mayhem /
Worms Ultimate Mayhem, `tools/w4m-import` converts the game's sounds for your own local use.
The output is gitignored: never commit or redistribute it.

Requires `cargo` and `ffmpeg` (with libvorbis).

```sh
cargo build --release --manifest-path tools/w4m-import/Cargo.toml
W4M=~/snap/steam/common/.local/share/Steam/steamapps/common/WormsXHD   # Steam install dir
tools/w4m-import/target/release/w4m-import "$W4M" client/assets   # out dir defaults to client/assets
```

- Idempotent: existing files are skipped, so delete a file (or the whole `client/assets/`) to redo it.
- `--list`: dump every bank's subsounds (name, codec, channels, rate, duration) and exit.
- `--raw`: also write every subsound under `<out>/raw/<bank>/<name>.mp2|.wav`, untranscoded (~300 MB).

## What it reads

`Data/Audio/PC/*.fsb` are FMOD Ex FSB4 banks: MPEG-1/2 Layer II (mp2) except `voRussian` (PCM16).
Speech categories come from `Data/Audio/Speech/<bank>.lsd` (category -> line hashes) and
`speech/<bank>/LIP.txt` (hash -> line name, matched to the subsound name cut at 29 chars).

## What it writes

| Path | Source |
|---|---|
| `sfx/<name>.ogg`, `<name>_2.ogg`, ... | hand-picked `weapons`/`global` subsounds (table `SFX` in `main.rs`) |
| `voices/<bank>/{fire,hurt,death,victory,jump,idle}[_N].ogg` | speech categories WeaponFired, FireDamage, FriendlyDeath, Victory, Jump, StartTurn of each `vo*` bank |
| `music/<theme>.ogg` | `mu*` banks; `theme.ogg` = frontend music; map themes use their `docs/maps.md` names (`jurassic` = muPrehistoric) |

The client picks a random variant on each play; each team speaks with the voice bank chosen in team setup (default `team % banks`).
Which file plays when, with its W4M event, gain and 3D range: `docs/audio.md`.

## Install

The client reads `assets/` next to the binary on desktop and `sdmc:/switch/worms4nx/assets/` on Switch:

```sh
rsync -a --exclude raw/ client/assets/ /path/to/sdcard/switch/worms4nx/assets/
# Ryubing emulator SD card
rsync -a --exclude raw/ client/assets/ ~/.var/app/io.github.ryubing.Ryujinx/config/Ryujinx/sdcard/switch/worms4nx/assets/
```

# Importing Worms 4 Mayhem audio

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

The client picks a random variant on each play; each team gets voice bank `team % banks`.

## Install

The client reads `assets/` next to the binary on desktop and `sdmc:/switch/worms4nx/assets/` on Switch:

```sh
rsync -a --exclude raw/ client/assets/ /path/to/sdcard/switch/worms4nx/assets/
# Ryubing emulator SD card
rsync -a --exclude raw/ client/assets/ ~/.var/app/io.github.ryubing.Ryujinx/config/Ryujinx/sdcard/switch/worms4nx/assets/
```

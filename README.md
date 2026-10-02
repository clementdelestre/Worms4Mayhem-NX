# Worms4NX

Fan remake of Worms 4: Mayhem for Nintendo Switch homebrew (Atmosphère), with a Linux desktop build for development.
No Team17 asset is shipped: bundled sounds are CC0 (see `client/romfs/CREDITS.md`), original assets will come from an importer reading your own install.

## Layout
- `client/` C++17 + [raylib-nx](https://github.com/luizpestana/raylib-nx) game: voxel destructible terrain, deterministic lockstep sim, local and online play.
- `server/` Rust lobby + input relay server (see `server/README.md`, protocol in `PROTOCOL.md`).
- `third_party/raylib-nx/` (cloned, not committed): `git clone --depth 1 https://github.com/luizpestana/raylib-nx third_party/raylib-nx`.

## Build
raylib libs (once):
```sh
cd third_party/raylib-nx/src
git -C .. apply ../../tools/patches/raylib-nx-pulse-s24.patch   # desktop sound on PipeWire sinks in S24_32LE (HDMI)
make PLATFORM=PLATFORM_DESKTOP RAYLIB_RELEASE_PATH=../out/desktop && rm -f *.o
git -C .. apply ../../tools/patches/raylib-nx-sideways-joycon.patch   # single Joy-Cons held sideways
docker run --rm -u $(id -u):$(id -g) -v "$PWD/../..":/w -w /w/raylib-nx/src devkitpro/devkita64 make PLATFORM=PLATFORM_NX RAYLIB_RELEASE_PATH=../out/nx CUSTOM_CFLAGS=-DNX_DISABLE_GAMEPAD_EMULATION && rm -f *.o
```
Game:
```sh
cd client && make && ./worms4nx            # desktop; make check = determinism test
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/w -w /w/client devkitpro/devkita64 make -f Makefile.switch   # -> client/worms4nx.nro
```
Server: `cd server && cargo run --release -- 0.0.0.0:7777` (or `docker build -t worms4nx-server server`).

## Run on Switch / emulator
Copy `worms4nx.nro` to `sdmc:/switch/worms4nx/`. Online: put `<server-ip> 7777 <name>` in `sdmc:/switch/worms4nx/server.txt` (desktop: `client/server.txt`).
Ryubing: open the `.nro`, enable *Guest Internet Access* for online play.
Debug on Switch: raylib's log goes to `sdmc:/switch/worms4nx/log.txt`. A `sdmc:/switch/worms4nx/shot` flag file runs a capture and quits: `<map>` = scripted turn → `shot.png`; `ui <screen> <frame>...` (same screens as `--ui`, plus `intro` = title then A at frame 20, Local at 80) → `ui_<frame>.png`. Delete the flag afterwards.

### Installable NSP
`./tools/nsp/build-nsp.sh` packs an application NSP (HOME menu entry, own icon,
full app memory) instead of the `.nro` — see `docs/nsp.md` for prerequisites
(Atmosphère + sigpatches), install (DBI/Goldleaf) and limits.

Assets to a real Switch: start an FTP server on it (sphaira or ftpd), then `tools/sync-switch.sh <ip:port>` uploads only changed files of `client/assets/` (`--all` forces a full upload).

## Controls
| In a match | Controller | Keyboard |
|---|---|---|
| Turn / walk | left stick | arrows |
| Aim | right stick (single Joy-Con: hold L + stick) | W / S |
| Fire (hold = power) | A | Space |
| Jump / let go of rope | B | Enter |
| Strikes (airstrike, donkey, Fatkins, Bovine Blitz, abduction, homing): sky view | A (or hold L) | Space / E |
| Sky view: fire (homing: lock) / leave / pan / look / zoom | A / B / left stick / right stick / D-pad ↑↓ | Space / Enter or E / arrows / WASD / Z X |
| Jetpack (W4M): take off and thrust / steer / pick a dynamite, mine or sheep (held beside it) / drop it | hold A or ZR / left stick / Y, R or panel / ZL | hold Space / arrows / Tab or panel / Backspace |
| Weapon panel | X (B closes) | Q (Backspace closes) |
| Next weapon | Y / R | Tab |
| Camera orbit / zoom | right stick ←→ / ZL ZR | A D / Z X |
| Performance overlay | L + R | F3 |
| Pause (resume, controls, quit / leave match) | + | Esc |

Menus: D-pad or left stick (held = auto-repeat), A confirm, B back (Esc), + starts from match setup, − opens the
system controller screen (pair / split Joy-Cons), + on the title screen quits. Every screen shows its buttons in a bottom bar;
the full list is under Options > Controls and in the pause menu.
Local play: team N uses controller N when connected, otherwise controller 1 is shared. Single Joy-Cons work held sideways.
Starting a local match with 2+ human teams (or fewer pads than human teams) opens the system controller screen first, sized
to that many players; cancelling it just falls back to sharing controller 1.

## Modding
- Weapons: `client/romfs/weapons.json`.
- Maps: `client/romfs/maps/*.json` (format in `docs/maps.md`), user maps in `sdmc:/switch/worms4nx/assets/maps/`.
- Sounds/voices: drop replacements in `sdmc:/switch/worms4nx/assets/{sfx,voices/<bank>}/`.

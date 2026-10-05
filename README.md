# Worms4NX

Fan remake of Worms 4: Mayhem for Nintendo Switch homebrew (Atmosphère), with a Linux desktop build for development.
No Team17 asset is shipped: bundled sounds are CC0 (see `client/romfs/CREDITS.md`), the original
meshes, animations, sounds, voices, maps, missions, menu art and acting scenes are converted from your own W4M install by the
`tools/w4m-*` importers into `client/assets/` (gitignored, never committed; `docs/import.md`).
`tools/sym NAME` / `-p prefix` / `-g regex` / `-f file`: symbol index (Universal Ctags) over the code and the doc headings, `file:line` to read.

## Layout
- `client/` C++17 + [raylib-nx](https://github.com/luizpestana/raylib-nx) game: voxel destructible terrain, deterministic lockstep sim, local and online play.
- `server/` Rust lobby + input relay server (see `server/README.md`, protocol in `PROTOCOL.md`).
- `third_party/raylib-nx/` (cloned, not committed): `git clone --depth 1 https://github.com/luizpestana/raylib-nx third_party/raylib-nx`.

## Build
raylib libs (once):
```sh
cd third_party/raylib-nx/src
git -C .. apply ../../tools/patches/raylib-nx-pulse-s24.patch   # desktop sound on PipeWire sinks in S24_32LE (HDMI)
git -C .. apply ../../tools/patches/raylib-nx-gltf-pose-search.patch   # glTF clip sampling in O(log n): the worm's 165 clips took 2.2 s at boot
make PLATFORM=PLATFORM_DESKTOP RAYLIB_RELEASE_PATH=../out/desktop && rm -f *.o
git -C .. apply ../../tools/patches/raylib-nx-sideways-joycon.patch   # single Joy-Cons held sideways
git -C .. apply ../../tools/patches/raylib-nx-docked-1080p.patch   # 1920x1080 window buffers, cropped to 1280x720 handheld (Lit::profile)
docker run --rm -u $(id -u):$(id -g) -v "$PWD/../..":/w -w /w/raylib-nx/src devkitpro/devkita64 make PLATFORM=PLATFORM_NX RAYLIB_RELEASE_PATH=../out/nx CUSTOM_CFLAGS=-DNX_DISABLE_GAMEPAD_EMULATION && rm -f *.o
```
Game:
```sh
cd client && make && ./worms4nx            # desktop; make check = determinism test
docker run --rm -u $(id -u):$(id -g) -v "$PWD":/w -w /w/client devkitpro/devkita64 make -f Makefile.switch   # -> client/worms4nx.nro
```
NSP: `./tools/nsp/build-nsp.sh [prod.keys]` (docker, `docs/nsp.md`) → `tools/nsp/out/nsp/0100576f524d3000.nsp`.
Server: `cd server && cargo run --release -- 0.0.0.0:7777` (or `docker build -t worms4nx-server server`).

## Tests
From `client/` (desktop raylib libs built, run from `client/` so `romfs/` and `assets/` resolve):

| command | what it checks (file header) |
|---|---|
| `make check` | `tests/sim_check.cpp`: determinism and the sim rules (physics, weapons, camera rules `checkEventCameras`, retreat in flight, death queue...) |
| `make ai_check` | `tests/ai_check.cpp`: AI vs AI matches deal damage, finish, replay bit-identically, CPU5 beats CPU1, rope race |
| `make replay_check` | `tests/replay_check.cpp`: saved / loaded / re-simulated replay and instant-replay restore give the same checksum |
| `make mission_check` | `tests/mission_check.cpp`: every mission (romfs + imported) loads, wins on its objectives, loses when wiped out |
| `tests/ui_check.cpp` (no make target, build line in its header) | the weapon panel's direct pick (`Input::pick`) lands on the weapon whatever the ticks per frame |
| `cd server && cargo test` | lobby, start, relay, desync, reconnect replay (`server/tests/relay.rs`) |
| `--netbot ...` (below) | two or more clients through a server, checksums compared every turn |

## Run on Switch / emulator
Copy `worms4nx.nro` to `sdmc:/switch/worms4nx/` (or install the NSP, below). Online: put `<server-ip> 7777 <name>` in
`sdmc:/switch/worms4nx/server.txt` (desktop: `client/server.txt`; the Options screen rewrites it).
Ryubing: open the `.nro`, enable *Guest Internet Access* for online play.
Data on the SD card (desktop: next to the binary): `assets/` (imported W4M files), `controls.txt` (Options > Controls: aim, camera,
gyro sensitivities, inverts, gyro, rumble), `setup.txt` / `setup_net.txt` (last match setup, local / online), `custom_weapons.json`
(Weapon Factory), `progress.txt` (missions), `lang.txt` (language), `replays/` and `replay.txt`, `log.txt` (Switch: raylib's log).

### Debug and capture modes (`main()` in `client/src/main.cpp`)
All captures run at a fixed `Game::DT` per frame (reproducible). Desktop arguments:

| arguments | does | output |
|---|---|---|
| `--shot [weapon] [map] [rules]` | scripted turn: picks weapon N, aims up, charges, fires (`scriptInput`) | `shot_aim.png` (frame 35), `shot.png` (frame 150, `W4NX_SHOTEND` moves the end) |
| `--utilshot <weapon name> [map]` | that weapon in hand from the start (one unit, no delay); the girder preview stepped ahead and up; Binoculars in first person | `shot_aim.png`, `shot.png` |
| `--aimshot <weapon> [map] [fine]` | shot mode held in aim mode (fine: precise aim) | `aim.png` at frame 60 |
| `--aimseq <weapon> [map]` | aim mode from frame 40 to 90 | `aimseq_NNN.png` around the entry and the exit |
| `--animshot <clip> [held model] [aim clip] [aim t]` | 8 poses of a worm clip; `W4NX_MODEL`, `W4NX_ZOOM`, `W4NX_YAW`, `W4NX_LAYERS="<face clip> lookYaw lookPitch [gestYaw gestPitch eyeYaw eyePitch]"`, `W4NX_ACT="<gesture> <weight>"` | `animshot.png` |
| `--animshot <weapon index>` | a turn firing it, seen from the side | `anim_NNN.png` |
| `--view <map> x y z tx ty tz` | shot mode from a fixed camera (overviews) | `shot.png` |
| `--ui <screen> [map or mission id]` | one screen: `title main local network myworms helpopts confirm setup options controls wormpot factory weapon replays playback missions briefing missionhud missionend hud panel pause help helpmenu ready loading` (hud: 3rd arg map, 4th weapon name) | `ui.png` (loading: `ui_<frame>.png` at 20, 60, 120, 170) |
| `--cpu [map] [level]` | every team played by the AI | — |
| `--bench <map> [frames] [nosync]` | CPU-vs-CPU match uncapped, per-section ms, draw calls, a Switch estimate; with `--aimshot`: `W4NX_BENCH=<frames>` [`W4NX_LOCK=1`] times that view | stdout |
| `--netbot host port name create\|join [turns] [map] [rules] [scheme] [roundMin]` | own teams played by the AI online, hidden window, checksums on stdout, exit 1 on desync; token in `./netbot.token`; `W4NX_SPEED` ticks per frame (8), `W4NX_CPU` adds a host CPU team of that level | stdout |

Environment: `W4NX_HIDDEN=1` hides the window (headless runs), `W4NX_CAPFRAMES="10 11 12"` writes `cap_<frame>.png`,
`W4NX_INPUTSCRIPT=<file>` replays `<frame> <raylib key> <1 down|0 up>` lines (key -1 quits), `W4NX_ACTLOG=1` logs the acting scene picks
(`acting.cpp`), `W4NX_UFOVIEW=1` films the abduction saucer from outside.

On Switch (no arguments) a `sdmc:/switch/worms4nx/shot` flag file runs a capture and quits: `<map>` = scripted turn → `shot.png`;
`ui <screen> <frame>... [map]` (same screens as `--ui`, plus `intro` = title then A at frame 20, Local at 80) → `ui_<frame>.png`.
Delete the flag afterwards.

### Installable NSP
`./tools/nsp/build-nsp.sh` packs an application NSP (HOME menu entry, own icon,
full app memory) instead of the `.nro` — see `docs/nsp.md` for prerequisites
(Atmosphère + sigpatches), install (DBI, Goldleaf or sphaira's FTP `install:`) and limits.

### Deploy to a real Switch over FTP (sphaira)
Start sphaira's FTP server on the console (its root lists `sdmc:`, `album_nand:`, `album_sd:`, `install:` and `games:`; ftpd works for the assets).
- Assets: `tools/sync-switch.sh <ip:port>` uploads only the files of `client/assets/` changed since the last sync to that address
  (md5 manifest `client/assets/.synced-<ip_port>`, 4 uploads in parallel, `--all` forces a full upload) into `/switch/worms4nx/assets/`.
- NSP: `curl -T tools/nsp/out/nsp/0100576f524d3000.nsp "ftp://<ip:port>/install:/Worms4NX.nsp"`; sphaira installs it as it receives it and
  the `install:` folder stays empty. Do not leave an NSP on `sdmc:`.

## Controls
Source of truth: `Controls::read` / `camera` (controls.cpp), `Ui::Hud` (weapon panel), `Ui::controls()` (the Options > Controls and
hold − diagram), main.cpp (perf overlay, replays). Switch names; keyboard in the last column.

| In a match | Controller | Keyboard / mouse |
|---|---|---|
| Walk / turn (camera-relative: the worm faces the stick at once) | left stick | arrows |
| Aim mode (first person for aimed weapons, scope for the sniper rifle) | hold ZL (precise), aim with the right stick (+ gyro if on); single Joy-Con: hold ZL + stick (not precise) | W / S; hold right mouse button = mouse aim (precise) |
| Fire (hold = power for powered weapons) / detonate a live shot | A or ZR | Space |
| Jump (press twice within 0.3 s = backflip, hold = vertical jump) / let go of the rope | B | Enter |
| Previous / next weapon, weapon panel | D-pad ← / → ; X (D-pad moves, A picks, B or X closes) | Tab / Q (arrows, Enter, Backspace or Q) |
| Fuse 1–5 s (grenade, cluster, banana) | D-pad ↑ ↓ | = / − |
| Camera orbit / zoom out, in (every view: normal, sky view, first person) | right stick / L, R | A D / Z X, mouse wheel |
| Targeted weapons (airstrike, Bovine Blitz, Fatkins, donkey, abduction, teleport): sky view | Y (toggle); A / ZR then fire | Space or E (toggle) |
| Homing missile: first-person aim, lock (first press), charge and fire (next press) | hold ZL, then A / ZR | hold right mouse, then Space |
| Homing missile: sky view (no lock until A / ZR), lock on the cursor, then ZL to aim and fire in first person (lock kept) | Y; then A / ZR; then hold ZL | E (toggle) |
| Sky view: fire (homing: lock the cursor point, then charge and fire) / leave / pan / look / zoom | A / B (or Y) / left stick / right stick / L, R | Space / Enter or E / arrows / W A S D / Z X, wheel |
| Rope | stick swings, right stick reels, A fires the held secondary, B lets go | arrows, W S, Space, Enter |
| Jetpack: take off and thrust / steer / forward thrust / drop the secondary (dynamite, mine, sheep) | hold A or ZR / left stick / D-pad ↑ / B (in flight and once landed) | hold Space / arrows / W / Backspace |
| Steered shot (Super Sheep, Bovine Blitz, Old Woman, Scouser): steer / detonate | left stick (left right: turn, up down: pitch) / A | left right arrows, W S / Space |
| Girder preview: move / raise, lower, turn the view / place | left stick / right stick / A | arrows / W S, A D / Space |
| Binoculars: look / pick a target | ZL / A | right mouse button / Space |
| Taunt with the weapon in hand (client only: acting scene + taunt clip; W4M Input.TauntPressed, no joypad binding) | — | T |
| Skip the hp count or crate camera | B | Space |
| Performance overlay (off, CPU, GPU-synced) | hold − for 1.5 s (the controls help shows first) | F3 |
| Button legend (bottom of the screen) | shown while a weapon or utility is selected in your Aim phase: the buttons W4M's HelpText lists for it (`weaponHints`, ui.cpp); hidden once fired | same, with the keys |
| Pause (resume, controls, quit / leave match) / controls overlay | + / hold − | Esc / hold F1 |
| Game over: back to the menu | A | Space |
| Replay playback: pause / speed ×1 ×2 ×4 / free camera / next turn / quit | A / R / X / Y / B or + | Space / Tab / C / N / Esc or Backspace |

Menus: D-pad or left stick (held = auto-repeat), A confirm, B back (Esc), + starts from match setup, − opens the
system controller screen (pair / split Joy-Cons), + on the title screen quits. Every screen shows its buttons in a bottom bar;
the full list is under Options > Controls and in the pause menu.
Local play: team N uses controller N when connected, otherwise controller 1 is shared. Single Joy-Cons work held sideways.
Starting a local match with 2+ human teams (or fewer pads than human teams) opens the system controller screen first, sized
to that many players; cancelling it just falls back to sharing controller 1.

## Documentation
"W4M" docs describe the original game (data, disassembly); "ours" docs describe this code. Every fact names its source and status.

| doc | side | content |
|---|---|---|
| `PROTOCOL.md` | ours | network framing, messages, the 5-byte `Input` |
| `docs/sim.md` | ours | turn flow and phases, retreat, settle, input semantics, movement and collisions, launches, secondary weapon, delays, CPU player |
| `docs/camera.md` | ours | every camera of a match and its W4M source |
| `docs/death-sequence.md` | both | death queue timing |
| `docs/weapons-audit.md` | both | per-weapon data, blast table, jetpack, engine changes |
| `docs/weapons-spec.md` | W4M | what each weapon does in W4M, as a player-facing spec |
| `docs/worm-reactions.md` | both | acting scenes, animation layers, triggers |
| `docs/audio.md` | ours | every sound: file, W4M FEV event, gain, 3D range, where it plays |
| `docs/camera-w4m.md`, `docs/w4m/README.md`, `docs/w4m-formats.md` | W4M | camera code, exe map (classes, messages, timers), file formats |
| `docs/maps.md`, `docs/missions.md` | ours | map and mission JSON |
| `docs/import.md`, `docs/nsp.md`, `tools/w4m-re/README.md` | ours | importing your W4M install, NSP packaging, RE helpers |

## Modding
- Weapons: `client/romfs/weapons.json`.
- Maps: `client/romfs/maps/*.json` (format in `docs/maps.md`), user maps in `sdmc:/switch/worms4nx/assets/maps/`.
- Sounds/voices: drop replacements in `sdmc:/switch/worms4nx/assets/{sfx,voices/<bank>}/`.

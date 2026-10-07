# Tests: the check binaries (ours)

What each check in `client/tests/` covers. Each test is a plain `main()` with `assert`: no framework, the first failing
assert aborts. All of them link every `client/src/*.cpp` except `main.cpp`.

## Running

From `client/` (desktop build, raylib in `third_party/raylib-nx/out/desktop`):

| Command | Binary | Needs |
|---|---|---|
| `make check` | `sim_check` | `romfs/weapons.json` |
| `make ai_check` | `ai_check` | `romfs/weapons.json`, romfs maps (arabian, wildwest, camelot, jurassic, construction, ropetrack) |
| `make replay_check` | `replay_check` | `romfs/weapons.json`, romfs map arabian; writes then removes `replay_check.w4r` |
| `make mission_check` | `mission_check` | `romfs/weapons.json`, imported `assets/missions`; writes then removes `progress_check.txt` |
| `make ui_check` | `ui_check` | `romfs/weapons.json` |

`make ai_check replay_check mission_check` runs the three in one go (in parallel). The build is incremental: objects and
header deps in `obj/`, binaries in `obj/bin/` with `./<test>` linked to them; `make obj/bin/<test>` builds without running.

Repository-level scripts in `tests/` (run from the repository root, they build the client if `W4NX` is unset):

| Script | Covers |
|---|---|
| `tests/quit_check.sh` | Pause → Quit → Yes to menu stays on the menu: scripted keys (title, Local, Quick match, pause, Quit, the ConfirmQuit Yes, arrows); fails on a second Play. |
| `tests/netbot.sh` | Two real clients (`--netbot`, the AI plays each owner's team) through a local server; fails on a desync. |

Timing runs (ours, not pass/fail): `./worms4nx --bench <map> [frames]` (CPU match, per-section ms, draw calls per frame and worst frame);
`W4NX_BENCH=<frames> ./worms4nx --shot <weapon index> [map]` (that weapon fired, e.g. 15 Airstrike, 18 Concrete Donkey:
the fire and explosion spikes); the log's `BOOT:` and `LOAD:` lines time startup and match loading; `make obj/bin/map_bench` then
`obj/bin/map_bench <maps...>` (map memory, load steps, mesh, `sample` cost, carve + remesh) and `obj/bin/map_bench all` (per map a
hash of the land content, unchanged by a format change: see "Map size").

### Frame pacing and hitch log (ours, 2026-10-05)

`log.txt` (`sdmc:/switch/worms4nx/` on Switch, `./` on desktop; raylib's log too): lines queue in memory from the first
one and a thread writes them each second; warnings are written at once (a crash keeps them, but may lose the last second of
info lines). A Switch SD write blocks 15-25 ms, and newlib's 1 KB stdio buffer made one every ~3 HITCH lines (the `other`
15-22 ms stalls of sw-log3); written line by line, the 2597 boot lines were the whole 47 s boot (see "Boot time"). The thread is joined at exit: libnx has no `pthread_detach`, so
`std::thread::detach` aborts the process (ours, found 2026-10-05). In a match:
- `HITCH <ms>, <n> ticks, <n> chunks, <n> particles, <n> sounds | <section ms> ... other <ms> | loads tex .. fbo ..`: a
  frame over `W4NX_HITCH_MS` (default 20), 20 per 10 s at most, plus the window's worst frame when the limit held it
  back (`... (worst of the window)`). Sections are the perf overlay's, plus `camera` (logic, audio loops, camera) and
  `pip`; `remesh` = waiting for the meshing threads + uploading their chunks (`chunks` = chunks uploaded); `other` = input,
  audio update, net. Non-zero `loads` = something loaded on first use.
- `PACE 600 frames: ticks/frame 0:a 1:b 2:c 3+:d | frame avg / sd / max, n over 20 ms | jitter`: every 10 s.
  At 60 Hz, a healthy run is `1:600` (or `2:` only for real missed vblanks), sd and jitter well under 1 ms.
- `GPU <n> frames: avg / max ms, n over 16.7 ms | <section ms> ...`: GPU time per section from timestamp queries read 4
  frames late (no stall; GL 3.3, or GLES with `GL_EXT_disjoint_timer_query`, else the boot line `GPU: timestamp queries
  unavailable`). The swap is left out (its span is the vblank wait). A section the CPU submits slower than the GPU runs
  shows the submission gaps too.
- `MEM: <used> MB of <total>`: every 10 s with PACE; Switch `svcGetInfo` UsedMemorySize of TotalMemorySize (the
  process), desktop RSS (`of 0`).
- `STICK: pad <n> (available <0|1>) L <x y> R <x y> raw, no button for <s> s` [ours, diagnostic]: a stick axis past the W4M dead zone (0.25) while
  no button was down for 3 s on a human turn; at most one per 5 s (`Controls::stickWatch`, raw `GetGamepadAxisMovement`, before the curve).
- `REMESH: <n> chunks, build <ms> (meshing threads), <ms> until shown, <ms> all`: one per land edit (explosion, girder,
  ClearCoded), from its first dirty chunk: n = chunk meshes built (a chunk rebuilt twice counts twice), build = their geometry
  time summed over both threads, until shown = until no chunk the last view drew is left to build (they swap in then), all =
  until no chunk is left. Before 2026-10-07 the count added the chunks put back each frame past the budget (sw-log4's 32
  were ~8 chunks counted over 4 frames).
- `REPLAY: <n> ticks re-simulated, <n> chunks remeshed, <n> kept, <ms>`: an instant replay's start.

sw-log3 spikes (handheld, 2026-10-05) and their fixes [data: the log; ours]:
- 220-395 ms (`sim`, or unlogged maxima): the instant replay start (restore to the turn snapshot, re-simulate up to the
  shot, remesh every carved chunk on the main thread) and its skip (re-simulate the rest); at game over also the replay
  file write and the victory stream open. Now: a checkpoint every 30 ticks (`Snapshot::mark`, re-sim <= 30 ticks), the
  meshes drawn at the snapshot kept while the land changes and swapped back (`Terrain::rewindMeshes`, no remesh), the live
  state and meshes set aside and restored on skip or end (`Snapshot::forward`), the dirty marking per voxel 27 -> 1-8
  chunk tests (restore 2.2 / 6.8 -> 0.4 / 1.2 ms desktop), `saveRec` on a thread, `victory` / `theme` opened during the loading.
- 26-53 ms `remesh` with 1 chunk: the chunk build plus a log write; see "Explosion frames".
- 15-22 ms `other` in 322 of 726 logged hitches: newlib flushing the log to the SD every 1 KB; now the writer thread.
- 10-22 ms `sky+water` in 278: GPU-bound frames (see "Render budget").
- `UI: texture <name> loaded on first use`: an SD read + PNG decode on the main thread; warm it in `Ui::warmHud`.

Pacing (ours): on Switch `eglSwapInterval(1)` alone paces the frames (`SetTargetFPS(0)`: raylib's busy-wait timer on top of
it beat against the vblank). A frame time within 2 ms of n x 1/60 s is taken as exactly n ticks and the accumulator is held
at half a tick, so timing noise never makes 0 / 2 tick frames (simulated with 0.5 ms noise: 2.5 % of the frames before,
0 after). No render interpolation: with the sim at the 60 Hz display rate every shown frame lands on a whole tick, so
interpolated poses would equal the drawn ones. The camera blends are already per-dt (`perFrame`, `expf(-dt k)`).

Explosion frames (ours, 2026-10-05; two threads 2026-10-07): chunk geometry is built on two meshing threads (cores 1 and
2) while the frame renders (`Terrain::remeshAsync`, after the sim: uploads what they built, hands them the dirty chunks,
those the last main view drew first; each starts no chunk past 10 ms but its first), and the frame waits for them before
the sim (`remeshWait`: the sim is the only voxel writer). The rebuilt chunks swap in together once no chunk in view is left
to build, so a seam with an older chunk stays off screen; the others swap in as they finish. `Terrain::carve` marks only
the chunks of the voxels and exact cells it changed (plus the one-voxel margin), not its whole box. sw-log3 measured one chunk at 6-15 ms on Switch (A57), plus 15-25 ms when a
log write landed in it: 26-53 ms `remesh` hitches per explosion frame before. The HUD art (`hud/`, weapon icons, flags) loads during the
loading screen, the UFO beam shader at boot and the PiP render texture on the first match frame.

### Big maps in play (ours, 2026-10-07, sw-log4: NoRoomForError, 640 x 608 x 640)

sw-log4 (handheld) and what caused it [data: the log; ours: desktop profiles with a SIGPROF-style sampler, `--bench`]:
- `sim` 137-138 ms on one tick, 4 times: the crate drop's `Game::landTop`, which scanned a column every 2 m from the grid top
  down (3.9 M voxel reads on this grid, 7.3 ms desktop) on every call. It is now `Game::landMax`, W4M's Land.MaxHeight: set at
  `start`, raised by welded land (docs/sim.md "Crates and objects"); a read costs nothing. Sim change: after a blast lowers the
  highest land, drops, bombers, Donkey, UFO and blimp keep the old top, as W4M (disasm, docs/w4m/engine.md).
- `sim` 10-14 ms over consecutive frames (e.g. 130.0-130.4 s): the CPU think (`Ai::budget` 20000 samples per frame, desktop
  p50 0.75 ms, p90 1.5 ms, max 2.75 ms per frame); not changed here.
- `shadow` 18-19 ms on the frames where rebuilt chunks swap in: the whole land redrawn into the shadow map (1008 chunk draws).
  Now only the swapped chunks' texels (docs/maps.md "Rendering").
- `shadow` 3.5 ms every frame: the worms, first drawn (and CPU-skinned) in the shadow pass; 20 buffer uploads per worm pose,
  now 2 ("Render budget").
- `terrain` 7-9 ms in wide views: one draw per chunk part, 1011 on this map with everything in view (~8 us each on Switch);
  now draw groups (docs/maps.md "Rendering", Land draw): 227 draws.
- `sky+water` 8-13 ms every other frame: the wait for a free swap buffer landing at the frame's first draw while the frame is
  over 16.7 ms (the GPU lines average 7-12 ms, under budget): it follows the CPU frame, not the sky.
- `other` 14-15 ms about every 1.1 s in quiet play [assumed: the log thread's SD write (each second) blocking the music
  stream's SD read in `UpdateMusicStream` on the main thread; not reproduced on desktop].
- `REMESH` 10-32 chunks, 100-135 ms until shown: the count added the chunks put back each frame past the 10 ms budget; one
  thread built ~2 chunks per frame at ~6 ms each. Now two threads, chunks in view first ("Explosion frames").

Desktop, same binary before / after (`--bench NoRoomForError`, cpu ms per frame; Switch estimate = 7 x cpu, the GPU column
14 x timer as in "Render budget"):

| | before | after | Switch estimate |
|---|---|---|---|
| AI match, 6000 frames: sim max | 7.43 | 2.23 | 52 -> 16 ms |
| AI match: frame max / cpu total | 10.58 / 2.09 | 6.59 / 1.60 | |
| AI match: shadow max (swap frames) | 2.8 | 1.0 | 19 -> 7 ms |
| wide view (80 140 -60 -> 80 30 80): terrain cpu, draws | 0.678, 1011 | 0.237, 227 | 8 -> 3 ms (log: 7-9) |
| wide view: terrain GPU timer | 1.37 | 1.16 | |
| close view (80 60 40 -> 80 40 80): terrain cpu, draws | 0.204, 354 | 0.107, 100 | 2 -> 1 ms |
| shadow cpu (wide / close) | 0.573 / 0.487 | 0.442 / 0.399 | 3.5 -> 2.7 ms |
| Dynamite crater (`--shot 7`): REMESH until shown | 63.6 ms, "24 chunks" | 29.5 ms, 8 chunks | 100-135 -> ~50 ms |

Chunks per carve on this map (60 surface hits each, old box vs changed voxels): r 1.5 m 2.8 -> 2.0, 3 m 6.4 -> 5.5, 5 m 11.0
-> 9.9. A chunk build is ~1 ms desktop, mostly the exact land's edge crossings (`SharpLand::first`, `crossing`). Captures of
the same shots and views before / after are pixel-identical (the FPS counter aside).

### Boot time (ours, 2026-10-06)

swlog2 (Switch, before): `BOOT: gl 2789 ms, decode+upload 42683, join 1055, menu scene 89, total 46616`. Its 2597 log
lines were each written to the SD at once, 12-30 ms apart (median ~18 ms): 2597 x 18 ms = the 46.6 s. The decode itself
was hidden under it [data: swlog2 timestamps].

Now (ours):
- The log is queued from the first line (above).
- A plain launch (no argument, no `shot` file) shows the title once the menu's assets are in: frontend art (`Ui::preload`),
  `models/frontend/` and `seagull.glb`, the `fe_*` / `wormpot_*` sounds, the map list. The other models (worm.glb first),
  the match sounds and the Missions list with its preview pictures load on cores 1-2 while the title and menus run (no
  GPU work there: the menu frames do not change). The first match's loading screen uploads the models in 8 ms slices and
  takes the sounds (prep step 0), before the map steps. Any argument or the `shot` file loads everything at boot, as before.
- Directory listings use `Loading::list` (readdir `d_type`): raylib's `LoadDirectoryFilesEx` stats every entry twice
  (strace: 1766 stats for the 884 files of `assets/maps`), each an SD request on Switch. Sound variants are looked up in one
  listing of their directory instead of a `FileExists` per variant.
- worm.glb's clips are sampled from a copy of the bytes already read, not a second 19 MB read.
- The map titles the sort needs are read on a thread (`Ui::mapHeads`), not on the main thread after the joins.
- Those heads come from one file, `assets/maps/index.tsv` (w4m-maps, after every import: `name, title, preview, theme`
  per line, cut from each json's first 399 bytes as `Ui::mapInfo` does). A map of the listing missing from it (copied by
  hand) has its own head read, as before. swlog3: `maps 2508`, `join 951` for the 221 per-file reads.
- The title music opens (`LoadMusicStream`: header, then a seek to the ogg's end for its length, 2.37 -> 2.82 s in swlog3)
  on its own thread beside the menu sound decoders instead of after them.

Log lines to read on Switch:
- `BOOT: gl <ms>, menu assets <ms> (art, models, sounds, maps: when each finished), screen +<ms>, join, menu scene, music,
  total`: total = black screen + startup icon + loading screen until the title; `screen` = the loading screen's 1.8 s minimum
  past the menu assets (plain launch only, docs/w4m/frontend.md §Boot).
- `BOOT: so far files <MB> read in <ms> (<MB/s>, one reader); models: glb parse, png, mipmaps, clips (worker time, summed),
  upload (main)`: read = every `LoadFileData()` (models, art, sounds), the rest per phase. Reads go through one mutex, one
  `read()` loop in blocks up to 4 MB (`readFile` in models.cpp), so the time is the SD's own and MB/s its real throughput.
- `BOOT: match assets in <ms> after start; ...`: the same totals once the first loading screen took the background loads.
- `MAP: <name> grid NXxNYxNZ, voxels <MB>, cells <MB>, load <ms> (vox, cells, mesh, navgrid)`: each match's map once its
  chunks are meshed. voxels = the density, material and steel blocks (`Terrain::voxelBytes`), cells = the exact land (`SharpLand::bytes`); vox =
  `.vox` + `.thin` read and decode, cells = `.cells`, mesh = the loading screen's mesh step (main thread), navgrid =
  the node grid and worm placement in `Game::start`; load = their sum.
- `MEM: malloc in use <MB> of <MB> reserved[, RSS <MB>]; known: terrain, textures, models = <MB>`: after each load and every
  10 s with PACE. malloc = newlib `mallinfo` (Switch) / glibc `mallinfo2` (desktop): `uordblks` + `hblkhd`, `arena` + `hblkhd`
  (`svcGetInfo` UsedMemorySize is the whole heap libnx reserves at start, so it is not used). known = what the code can
  count: terrain = voxel blocks, exact cells, shadow columns, chunk meshes (`Terrain::bytes`); textures =
  the GPU textures raylib logs (size x format, +1/3 with mips; video memory, shared with RAM on Switch, not in malloc);
  models = mesh arrays and sampled clips (`Models::bytes`).

Loading screen, 2026-10-07 [ours]: `Terrain::loadVoxels` decodes the `.vox` chunks on 3 joined workers (docs/maps.md
`voxels`). `Terrain::meshInitial` builds the chunk geometry on 3 joined workers (cores 0-2, an atomic
chunk counter; a chunk whose sampled chunks are each one value of one sign returns before any voxel read, other all-air /
all-solid chunks after `chunkGeometry`'s sign scan) while the main thread uploads within 8 ms
per frame; the chunks swap in together at the end. Timings: "Map size" below.

Background load estimate on Switch [assumed until a log confirms]: the 42 s of read time in the log is the sum over 3 threads
reading at once (146.6 MB, ~3.5 MB/s each); one reader in big blocks at 40-60 MB/s needs 2.5-4 s for the same bytes (`read()`
in 4 MB blocks also replaces newlib's `fread`, which fills its buffer in `st_blksize` pieces). The CPU phases run on the two
workers beside the menus: clips 4.3 s (almost all worm.glb, one job) + mipmaps 1.0 s + parse/png, so the background ends in
~6-8 s; the loading screen then only has the 2.8 s of upload slices left. Precomputed clips or compressed textures would cut
the CPU phases but were not built: the estimate is already under the 15 s target and both need a w4m-models export change.

Largest map, desktop (2026-10-07, `--bench NoRoomForError 1300` and a scratch AI-vs-AI run of 11 turns, level 5) [ours]:
`MAP: NoRoomForError grid 640x608x640, voxels 475.0 MB, cells 53.3 MB, load 864 ms (vox 265, cells 52, mesh 544, navgrid 2)`;
`MEM: 1027 MB` (RSS); explosions `REMESH` 2-7 chunks, build 1.8-10.5 ms, 29-71 ms until shown; AI max 2.47 ms per tick,
328 ms per turn; sim step max 6.8 ms. trial-w3d (448x832x576): AI 2.38 ms / 259 ms, step 3.8 ms; Deathmatch1 (480x352x544):
AI 1.23 ms / 387 ms, step 2.7 ms. Switch estimate at 7x CPU: load ~6 s (vox 1.9, cells 0.4, mesh 3.8), explosion build
13-74 ms on the meshing thread, AI 17 ms worst tick, step 47 ms worst; memory is not CPU-bound (~1 GB on desktop).

Desktop (warm file cache, 3-5 runs each) [ours]:

| | before | after |
|---|---|---|
| title shown (`total`) | 575-584 ms | 76-104 ms |
| critical part | worm.glb clips: 530 ms on one worker | menu art decode 58-77 ms |
| sounds | 461-478 ms (all 170) | 45-61 ms (menu), the rest in the background |
| reads at the title | 95.9 MB (worm.glb twice) | 37.2 MB |

Switch estimate [ours, assumed until a log confirms]: CPU work ~7x desktop (Render budget), a small file ~5 ms (swlog2: the
221 map heads in ~1 s of `join`; 256x256 HUD PNGs read + decoded in 17-22 ms), ~55 MB/s for big reads (1 MB PNGs +18 ms).
Title: ~0.3 s GL, then ~0.5 s of decode, 37 MB of reads (~0.7 s) and ~300 small files (~1.5 s if the SD serialises them):
about 2-3.5 s instead of 46.6. The background part (worm.glb's clips ~3.7 s on one core, 64 MB more) ends a few seconds
after the title; a match started before that waits for it behind the loading screen.

First Story / Challenges opening (ours, 2026-10-07): `MISSIONS: opened +<ms> after the title, waited <ms> (list at, pictures queued at)`
then `MISSIONS: page draw first / worst of 60 frames`. Desktop (core 0 only, 3 runs): waited 0.0-11 ms, page draw worst 0.7-1.0 ms.
Before, the main thread did `missionsF.get()` (joined the list AND the decode of every picture, 51 PNGs + a GPU mipmap pass
each) and the first draw of each picture decoded or uploaded + mipmapped on the main thread. Now (ours):
- the async only reads the list (51 small JSONs, ~1 s of SD at most on Switch, behind nothing but its own thread) and queues
  the pictures with `Ui::predecode` on a thread of their own (not behind worm.glb's workers; priority 0x3F on Switch);
- the worker decodes AND builds the mipmaps (`ImageMipmaps`), so the main thread only does `LoadTextureFromImage`, one per
  picture drawn, no GPU mipmap pass;
- a level picture (`levels/*`, mission list, briefing, match setup map preview) not decoded yet is queued on demand and drawn as
  nothing until it is in (`image()` / `pending()`); `preview()` lists `assets/ui/levels` once (in `mapHeads`) instead of
  loading a texture to test for existence. W4M's own behaviour when the picture is late: not measured [assumed none].
- the setup screen's map picture and the Wormpot page use the same path (Wormpot has no art beyond the preloaded
  `fe/icon_wxpot`). `UI: texture <name> loaded on first use, <ms>` now logs its cost; the others (weapon icons, HUD) are 0.1-0.5 ms.
Switch estimate [assumed]: a worker decode+mipmaps of a 256x256 PNG ~25 ms, so the 51 pictures are in ~1.3 s on core 0 at low
priority; the main thread pays one upload (~1-2 ms) per picture shown.

Left as is: the UI PNGs are stored uncompressed (`back/loadbackgeneric` 8.3 MB of the 23.6 MB menu art); a lossless
recompression at import trades SD reads for inflate time, to be measured on Switch first. Map previews in the setup screen
and the HUD art (`warmHud`, behind the loading screen) still load on first use.

### Map size (sparse voxels, chunked deflated files; 2026-10-07) [ours]

Voxels stored per 32³ chunk (`Bricks`, docs/sim.md "Movement and collisions"), the exact land's cell bits per chunk, the `.vox`
deflated per chunk, the `.cells` with dynamic-Huffman deflate, ranked list codes and byte-plane heightmap, the `.thin` deflated
(docs/maps.md, docs/w4m/formats.md §22). Content unchanged: `map_bench all` (tests/map_bench.cpp: density, materials, exact
land lists, thin cells in x, y, z order) gave the same 221 hashes before and after that reimport; ai_check and mission_check
printed the same results (the AI's `samples` count and every outcome). The later reimport with W4M's land sampler (end of this section)
changes the land, so its hashes differ by design.

Desktop, warm cache, `map_bench` (2 runs after; voxels = `Terrain::voxelBytes`, terrain = `Terrain::bytes` once meshed):

| | NoRoomForError 640x608x640 | Multi_TheWindyWizard 608x320x608 | Deathmatch3 448x288x608 |
|---|---|---|---|
| voxels MB | 475.0 -> 25.7 | 350.3 -> 20.6 | 312.3 -> 18.8 |
| cells MB | 53.3 -> 25.4 | 37.6 -> 24.8 | 34.5 -> 26.3 |
| terrain MB | 552.3 -> 75.1 | 404.5 -> 62.1 | 363.5 -> 61.9 |
| disk .vox + .cells + .thin MB | 7.75 -> 2.90 | -> 2.23 | 4.47 -> 2.46 |
| vox (read + decode) ms | 115 -> 15-17 | 23 -> 9 | 26 -> 8 |
| cells ms | 56 -> 55-58 | 43 -> 42-43 | 45 -> 43-44 |
| `remesh(0)` (textures, shadow columns) ms | 113 -> 40-43 | 124 -> 26-27 | 121 -> 29-32 |
| mesh (`meshInitial`) ms | 179 -> 162-180 | 122 -> 123-124 | 111 -> 114-116 |
| carve r 2.5 m ms | 0.39 -> 0.40-0.41 | 0.38 -> 0.39 | 0.42 -> 0.43-0.45 |
| its remesh ms (16 carves) | 3.6 -> 3.7-3.8 | 3.2 -> 3.5-3.6 | 4.8 -> 5.1-5.3 |
| `sample` / `field` ns | 51.1 / 29.2 -> 49.9-52.2 / 27.9-28.1 | 53.0 / 27.6 -> 52.2-56.8 / 27.7 | 56.9 / 26.9 -> 58.2-61.8 / 28.0 |

All 221 maps: `.vox` 385.1 -> 125.6 MB, `.cells` 404.4 -> 267.3 MB, `.thin` 11.9 -> 5.4 MB, 801 -> 398 MB; the maps directory 812 ->
431 MB. Game, `--bench NoRoomForError`: `MAP: ... voxels 25.7 MB, cells 25.4 MB, load 238 ms (vox 17, cells 52, mesh 168,
navgrid 1)`, `MEM: malloc in use 443 MB, RSS 534; known: terrain 75`. ai_check planning, 4 runs each, same build otherwise:
413.7, 397.9, 475.7, 423.9 ms per turn before, 435.4, 434.1, 437.0, 434.3 after; worst tick 1.9-2.5 ms before, 1.5-4.9 after
(the spread of both is the machine's). A carve's first change in a chunk copies its shared block (32 KB): arabian's first 16
carves 0.06 -> 0.29 ms each, the next 16 0.07.

After the sampler pieces (docs/w4m/formats.md §23, 2026-10-07) [ours, `Terrain::load` log]: the 221 maps hold `.vox` 118.6, `.cells` 416.7, `.thin` 10.3 MB
(545.7 in all), the maps directory 596 MB; exact land (`SharpLand::bytes`) NoRoomForError 25.7 -> 40.0 MB (10 352 -> 36 098 hexahedra),
Multi_TheWindyWizard 24.9 -> 31.0, Deathmatch3 26.4 -> 30.5. A cell is up to 4 pieces plus the whole cell kept for its normals.

Switch estimate (7x CPU, SD 9-23 MB/s from swlog5) [assumed until a log confirms]: NoRoomForError's terrain 576 -> ~80 MB of
malloc (1335 -> ~840 MB in use); its MAP step vox 472 ms -> ~0.15-0.25 s (1.2 MB read, 3 cores decoding), cells 360 ms ->
~0.3 s (1.7 MB read instead of 2.5), mesh 356 ms unchanged, the shadow columns ~0.5 s shorter; explosions and the AI unchanged.

### Render budget (ours, `--bench <map> 600`)

Calibrated on sw-log3 (handheld 720p, 2026-10-05; data from the log, factors fitted): Switch handheld frame ~= max(7 x
the desktop `cpu` total, 14 x the desktop `timer` GPU total of a `nosync` run). Deathmatch1 at idle: cpu 1.36-1.45 ms,
timer 1.06 ms -> ~15 ms; EscapeFromTreeRex 1.48 / 1.20 -> ~17 ms. The real handheld runs Deathmatch1 at 17-22 ms average
with 10-35 % of the frames on two vblanks, i.e. a GPU frame around 15-17 ms [assumed from the pacing: the `sky+water` 10-22 ms
stalls are the wait for a free swap buffer, landing at the frame's first draw]; its per-frame CPU (no sync) is 6.5-13 ms.
The `GPU` lines of the next Switch log measure the GPU directly: refit the 14 then.

Why the former estimate (max(5x cpu, 4x synced gpu), "docked") read 33-37 ms with shadows: (1) the synced `gpu` column
glFinish()es after each of 12 sections, the desktop iGPU idles and clocks down between them, so it reads 5.5-6 ms for a
GPU frame the timestamp queries measure at ~1.1 ms (nosync, 735 fps, the swap left out); (2) the 8.3 / 8.7 ms
it was given were measured while other agents' games shared the iGPU (5.45 at idle). 4 x 8.3 = 33 ms; 3 x 5.45 = 16 ms
matches the handheld. The CPU side also mixes two rates: logic about 3-5x desktop, draw submission on nouveau 10-25x
(terrain 0.08 ms desktop -> 1.5-3.3 ms Switch): 7 fits the whole frame.

Desktop nosync GPU split (timer, Deathmatch1): terrain 0.41-0.45, sky+water 0.33, shadow 0.10, ui 0.09, models 0.05.
The terrain is mostly geometry on this iGPU (0.40 ms at 640x360, 0.47 at 720p, 0.70 at 1080p), so the land shader's
texture fetches do not show here; on Tegra (16 TMUs at 307-384 MHz handheld) they do [assumed].

Land shader fetches, 2026-10-05: 13 per pixel before (4 triplanar diffuse, the top and roof ones both fetched, + 9
compared shadow taps). Now a triplanar plane under 0.4 % of the blend skips its fetch and only one of top / roof is read
(1-2 diffuse fetches on most land; image diff with the former shader: max 1 / 255 on 0.2 % of the pixels), and on Switch
the shadow uses W4M's own X_XBOX path of `SHADOW_METHOD 2`, 5 taps (docs/w4m/render.md, Landscape.cg): 6-7 fetches.

Worm skinning: a pose drawn in the shadow pass then the view was skinned twice (shared VBOs); poses now live in up to 16
buffer slots per model (`Models` `Entry::Slot`): `models` cpu 0.15 -> 0.08 ms desktop. A slot keeps the skinned positions and normals of all the model's meshes in one buffer each (`Slot::pose`, each mesh's VAO reads its offset): a worm pose is 2 uploads instead of 20 (2026-10-07: `shadow`, where each worm is first drawn and skinned, 0.55 -> 0.43 ms desktop). The animated normals went to the
colour VBO index (`SHADER_LOC_VERTEX_NORMAL` = 3 is the colour attribute's buffer): now the normal buffer, so worm
lighting follows the pose.

Older measurements (synced `gpu` x 4, "docked"):

| Change | Deathmatch1 | ChallengeNavigation2 |
|---|---|---|
| before wave 2 (4750d4f) | 18.5 ms (54 fps), remesh at load 68 ms | 18.8 ms (53 fps), remesh 68 ms |
| land vertex colour, sky clip, lens flare, Donkey dome | 18.5-19.3 ms (52-54 fps), remesh 50 ms | 18.9 ms (53 fps), remesh 49 ms |
| + W4M shadow map, measured as a 1024² land depth pass (+1.9 ms gpu) and 9 extra land texture taps (+1.3 ms gpu) | 32 ms (31 fps), objects not even counted | - |

Shadow map as shipped (2026-10-05, docs/maps.md "Rendering": land cached, casters only per frame, dirty-rectangle restore), same
binary with and without, runs interleaved on a machine shared with other agents' games (the baseline read 16.9 / 19.2 ms on an idle GPU):

| `--bench <map> 600` | Deathmatch1 | EscapeFromTreeRex |
|---|---|---|
| without | gpu 6.67-6.72, Switch 26.7-26.9 ms | gpu 7.16-7.21, Switch 28.7-28.9 ms |
| with | gpu 8.26-8.28, Switch 33.0-33.1 ms; `shadow` cpu 1.26 gpu 1.03, terrain +0.3 gpu | gpu 8.74-8.82, Switch 35.0-35.3 ms; `shadow` cpu 1.21 gpu 1.19, terrain +0.38 gpu |
| 1920x1080 without / with | gpu 8.36 / 10.20, Switch 33.4 / 40.8 ms | - |

So +1.5-1.6 ms gpu at 720p (+6 ms Switch estimate), +1.8 at 1080p; on the idle-GPU baseline that is about 23 / 25.5 ms at 720p.
Of the `shadow` section, a pass into an FBO costs ~0.3 ms of sync overhead even when empty, a full-map depth blit ~0.35 (the
dirty rectangle averages 37 % of the map), the casters the rest; its cpu is mostly worm re-skinning. A land change redraws the
cached map once (the whole land, ~1.9 ms gpu) on the frame the rebuilt chunks swap in. Docked 1080p may now cross the 36 ms
fallback to 720p (`Lit::profile`).

Scenery and graphics levers (2026-10-05, same binary, `W4NX_GFX="msaa aniso=N res=WxH"`, gpu = desktop GPU-synced ms, Switch = 4x gpu docked):

| Lever | Deathmatch1 | EscapeFromTreeRex | Shipped |
|---|---|---|---|
| old assets (no fringe / thin cells / decor states) | 6.02 ms, 24.1 ms (42 fps) | 5.76 ms, 23.1 ms (43 fps) | - |
| new scenery, aniso 4 | 6.15, 24.6 (41 fps) | 6.13, 24.5 (41 fps) | yes |
| aniso 1 / 16 | 6.00 / 6.15 | 6.10 / 6.14 | 16 (free) |
| MSAA 4x, 720p | 7.07, 28.3 (35 fps) | 7.20, 28.8 (35 fps) | no |
| 1920x1080 | 7.48, 29.9 (33 fps) | 7.57, 30.3 (33 fps) | docked |
| 1920x1080 + MSAA 4x | 8.86, 35.4 (28 fps) | 8.88, 35.5 (28 fps) | no |
| two-voxel normals | load remesh 70 -> 89 ms, no frame cost | | yes |

Docked (`appletGetOperationMode`) draws into 1920x1080 window buffers (`tools/patches/raylib-nx-docked-1080p.patch`, `Lit::profile`), handheld into their 1280x720 crop; docked frames averaging over 36 ms for 3 s drop to 720p until the next undock, and `sdmc:/switch/worms4nx/docked720` forces 720p. Handheld keeps 720p without MSAA: its GPU clock is about half the docked one. Terrain LOD: none (W4M has none; decor costs 0.6 ms).

### Wall-jump scan (ours, scratch harness, 2026-10-05)

Not a check binary: a `main` that includes `sim_check.cpp` (for `settle`), starts Deathmatch1, 3, 5, 7 and Clean-w3d
(seed 7, 2 teams of 4), and for each settled worm and each of 8 yaws walks it (stick held, 150 ticks)
until it has moved under 5 mm for 20 ticks, waits 10 ticks, presses jump and fails when it rises under 1 m in 60 ticks.

| Normal of a sweep hit | Walls | Fails |
|---|---|---|
| VOX-wide gradient at the first solid sample | 134 | 11 |
| over VOX/4 at the crossing (`sweep`) | 133 | 14 |

- The 6 feet that read a 70-78° face now jump: their W4M faces are n.y 0.01, 0.13, -0.27, -0.42, -0.42, -0.06, all under 0.2, so W4M Rebounds and keeps rising [data: scratch w4m-maps probe of the exposed cell faces].
- Kept, as W4M: 2 worms already sliding when jump is pressed, 2 with the head ray under a roof or overhang, 1 rod into a W4M bump (docs/w4m/physics.md §11 "Ballistic").
- New: 3 land on a face that is n.y 0.21-0.40 in W4M too and slide, 1 rebounds off a W4M overhang (n.y -0.42), 5 come from a normal still off by 14-63° at a rounded voxel edge (2 of them on the wrong side of 0.2).
- First contacts on the wrong side of n.y 0.2 against the W4M face: 9 of 89 before, 6 of 96 after.

Helpers (scripted inputs, scene builders such as `settle`, `melee`, `floorAndWall`, `weaponNamed`) are not listed.

## sim_check.cpp

Determinism and one check per rule, weapon and movement case. `main` also asserts that each preset scheme changes the
checksum and that every weapon fires twice bit-identically (`fireEach`).

| Check | Covers |
|---|---|
| `checkDiffuse` | Slow stick rates survive the int8 rounding on average; a released stick sends 0 at once. |
| `run` | Two games, same seed and inputs, stay bit-identical; each turn uses the next weapon of the table. |
| `runRules` | Same scripted run per rule combo and per preset scheme (sudden death reached): identical checksums. |
| `checkKing` | King rule: the king dies, then his team through the death queue; the other team untouched. |
| `checkDrawRound` | Pause > Draw Round: one `Input::DRAW` tick ends the match at once, GameOver with no winner (W4M GameLogic.DrawImmediately). |
| `checkDeathQueue` | W4M death queue: dead worms of one count blow up one after another. |
| `checkDrownFloat` | W4M drowning: no hp count, the worm floats a moment with the camera on it, pops at the surface. |
| `checkDrownPair` | W4M: two worms drowned by one blast pop one after the other, each 2000 ms after its own surface arrival (no queue); a standing worm reached by the water sinks at 0.03 units/ms. |
| `checkPostActivity` | W4M PostActivityTime: 2400 ms between the end of the settle and the next turn. |
| `checkKarma` | Karma: a cluster explosion injected into `shots[]` hurts the attacker (damage rule in isolation). |
| `checkVampire` | Vampire: damage dealt to an enemy heals the attacker above full. |
| `checkLowGravity` | Low gravity: a worm falls slower. |
| `checkSuddenDeath` | Round time up: sudden death, every worm at 1 hp, the water rises. |
| `checkRopeRace` | Rope race: reaching the finish ends the match, GameOver emitted once. |
| `checkHighlander` | Highlander: the killer's team inherits the victim's weapon. |
| `checkObjects` | Crates heal and add ammo; barrels chain only within reach. |
| `fireEach` | One scripted turn per weapon (charge, release, steer, detonate): its Fire event shows up. |
| `checkPoison` | Poison Arrow: a worm hit gives the gas cloud and poison with no damage or knock; land stops it, the cloud comes 2 s later; poison ticks off at the next turn start. |
| `checkMelee` | Fire Punch and the other melee weapons: reach, height, behind, knock-back, W4M 0-damage push. |
| `checkShotgun` | A gun hit takes the weapon's full damage, not a blast falloff. |
| `checkHoming` | Homing missile flies along the aim, then dives onto the reticle point. |
| `checkTeamWeapon` | Each turn starts empty-handed (W4M Turn.Started); `picked` keeps the last weapon (AI variety) and is checksummed. |
| `checkCrateWalk` | Walking into a crate (real input) collects it: health heals, a weapon adds ammo. |
| `checkSniper` | Sniper over 20 m of open sky deals its damage. |
| `checkScopeCrest` | Sniper at a worm just over a crest, aimed like a player at the scope camera's screen centre. |
| `checkSentry` | Sentry gun shoots an enemy in range, then reloads. |
| `checkSheepCamera` | W4M SheepChaseCamera: behind and above the sheep, rises when land hides it, never under it; after the sheep the drawn view settles (each step no longer than the first) and never moves back toward the worm. |
| `checkEventCameras` | W4M event cameras: worm, crate, winner TrackCams, homing FlyCam, shoulder camera occlusion zoom; PiP during the active worm's turn then the grow at EndTurn, chase start yaw (Sheep / Scouser ResetYaw), Donkey camera held. |
| `checkDeathBlast` | W4M Worm.Death*: the death blast takes up to 35 hp off neighbours, throws them, digs 1.75 m; the settle waits for the thrown worm (W4M Worm Falling is active). |
| `checkWallClearance` | Concave corner: walking into it or flying against a wall leaves W4M's body, the 3 rods (Fits 0x59edf0), out of the rock; the mesh may dip in between them, as in W4M. |
| `checkWalkW4M` | Density clamped like imported .vox maps: corridors and steps walkable, ledges vaulted up to body height. |
| `checkLowLedges` | 0.2-0.7 m ledges of exact cells (`Terrain::addCell`, as an imported map's), on and off the voxel grid and diagonal: the front foot finds them (4 foot rays) and the worm steps or vaults on; the walkable test reads the flat top past the lip. |
| `checkVault` | W4M Vaulting: a 16-unit ledge in 250 ms; stick keeps it going, release drops back, jump ignored. |
| `checkNarrowSlot` | W4M 8 land probe points, exact cells: a foot lands on the lips of a slot narrower than the stance, and walks across it; a wider slot lets it in. |
| `checkHeading` | W4M: walking sets the facing to the stick direction at once, whatever the turn angle. |
| `checkWallStuck` | Off a ledge onto a 76 degree face (pushed into it, it skids up and slides back): the worm lands, then walks out. Head wedged under a sloping ceiling: its foot rays start in land (d = 20) and the vault 20 units up does not Fit, so W4M's walk stays blocked. |
| `checkJumpTrajectory` | W4M launch + Integrate: jump 50 units up, 80 along; backflip 80 up, 50.6 back. |
| `checkLaunchAtWall` | W4M launch from the eye against a thin wall: bazooka on its own side, shotgun on the near face, dynamite ahead. |
| `checkPointBlankDown` | Fired down at point blank: the shot passes the shooter's body, the floor blast hurts it. |
| `checkPayloadForces` | W4M payloads: wind adds Wind.Speed; homing missile has no gravity, homes only in stage 2. |
| `checkJumpAtWall` | StartJump 0x5acd40 tests no wall: against an upright or 6 degree overhanging cliff, on each side, facing it or away, tap, double tap or stick held, the worm leaves the ground over 1.5 m. |
| `checkJumps` | DetectJump: double-press window, forward jump speeds, variant picked when the 300 ms window ends. |
| `checkW4MWalkRules` | UpdateWalking / Sliding rules on 70° slopes (walked into from 20 approach phases: never walks up it, any step onto it Slides back to the foot); a worm whose rods cross a slab with nothing under its feet: the fall is undone (stuck +2), Rebound stops it (under 0.01 units/ms) into Sliding, whose Landed clears the count, and it then stays put Ambulatory (W4M Ballistic 0x5afb17, Rebound 0x5acea0, Sliding 0x5b05c2). |
| `checkDynamite` | Dynamite: the worm walks away while the fuse burns, can't fire again, the blast ends the turn. |
| `checkOffMapShot` | A shot leaving the map flies on (camera on it) until it falls into the sea with a splash. |
| `checkCrateHold` | A crate dropped mid-turn holds the turn (no clock, no control) until it lands, then `POST_ACTIVITY`. |
| `checkCrateBetweenTurns` | W4M DoPostActivity pass 1: the random crate falls in `Settle`, never in `Aim`; the next turn starts `POST_ACTIVITY` after it lands. |
| `checkSelfHurtEndsTurn` | W4M: the turn ends as soon as the active worm takes damage, no retreat time. |
| `checkWipeEndsMatch` | A wiped-out team ends the match even with a shot pending; both wiped = draw. |
| `checkHotSeat` | Hot seat: turn clock frozen, `TARGET` (Blimp view) alone keeps it, any other input or `Input::CAMERA` starts the turn. |
| `checkSkipGo` | Skip Go: 3 s with the worm frozen (W4M PostLaunchDelay), then a 0 s retreat to `Settle`. |
| `checkScheme` | Scheme fields: start hp, objects, turn time, weapon ammo, fall damage. |
| `checkWormpot` | Wormpot modes: double damage, worms drown, quick walk, energy/rule combos, ammo and hp presets. |
| `checkCustomWeapons` | Weapon Factory: custom weapons append at start(), survive a save/load, and fire. |
| `checkFuse` | W4M FuseUp: grenade-family fuse 1..5 s on the d-pad, exact to the tick; Holy Hand Grenade blast 2 s after rest. |
| `checkParachute` | Long fall with the parachute in hand opens it under −11.25 m/s; open, the velocity settles on 3 m/s along the facing plus 70 ms of the opening's wind and gravity; it lands unhurt. |
| `checkRetreatInFlight` | W4M PostLaunchDelay then retreat while the shell flies; timeout ends the turn; the TrackCam served during the turn is the PiP. |
| `checkToolWeapons` | Rope and jetpack: the held weapon goes off without leaving the tool, which works through the retreat. |
| `checkJetpack` | W4M jetpack: thrust curve by height, fuel burns only while thrusting, landing ends it, ammo taken once. |
| `checkJetpackSecondary` | Switch path: take off, pick dynamite as secondary, ZL lays it in flight; a leftover secondary becomes the weapon. |
| `checkToolGaps` | W4M tool details: secondary kept on landing, UtilityFire panel, D-pad forward, HeadCam zoom, No Bombing, girder. |
| `checkEmptyHand` | W4M kWeaponUndefined at turn start: FIRE fires nothing, takes no ammo; NEXT_WEAPON picks a usable one. |
| `checkSchemeFactory` | Scheme `mineFactory` (W4M MineFactoryOn): the factory exists at start and counts down at each turn end. |
| `checkGunObjects` | W4M gun mask 0x1c3f: the bullet stops on a mine or an oil drum in its way. |
| `checkAbduction` | W4M alien abduction: UFO lifts worms in reach nearest first, spits them out 2.1 s apart at half health. |
| `checkSuperSheep` | Super Sheep: walks, FIRE takes off (25 s flight), FIRE again blows it up. |
| `checkOldWoman` | Old Woman: steered, FIRE explodes, each enemy bumped loses 1-8 of a weapon to her team. |
| `checkDonkey` | Concrete Donkey: smashes down every 0.75 s until its 8 s LifeTime or the water. |
| `checkMineDuds` | Mine.DudProbability: about one CreateMine mine (`newMine`) in ten fizzles; a laid mine never does. |
| `checkMineBlast` | A blast only pushes a mine, up and away; it does not arm it. |
| `checkMineFlyby` | ArmingRadius 45 units: a worm blown past 2 m off arms it; a laid mine waits ArmingCourtesyTime. |
| `checkRopeShots` | Ninja.NumShots: 5 launches a turn; the hook catches a crate, which swings about the worm's feet at the rope's length while reeled in and out; jump lets go. |
| `checkRope` | Ninja rope in the open air: the hook angle and length, the yaw kept, gravity's first pull, the body on the circle, a damped swing past the bottom, the release velocity; the stick's swing (0x571820 thresholds); reel at 10 m/s within MinLength / MaxLength; a wrap round a bar and its unwrap; a bounce off a worm's collider. |
| `checkWaterShots` | Payload water: a Bazooka skims (SkimDamping, set on the Radius plane); a Grenade splashes, is disarmed at SinkDepth, sinks at 4..5 m/s without a blast and goes at Water.ExpiryDepth; a homing missile homing only splashes. |
| `checkSeaTurn` | A payload that meets the sea leads to the next turn within 40 s for each family (Bazooka, Grenade, Cluster Grenade, Sheep, Super Sheep, Homing Missile, Airstrike, Fatkins, Donkey, Old Woman, Poison Arrow) fired from a platform, and one born under the disarm plane is gone within 30 s (Water.ExpiryDepth). |
| `checkArrowFalls` | A stuck Poison Arrow whose land is carved falls again and detonates at its first stop's time. |
| `checkScouser` | Inflatable Scouser: swallows a worm, floats it up, pops and drops it. |
| `checkGasCloud` | Gas canister leaves an 8 s cloud that poisons every worm within 5 m. |
| `checkBomber` | Bovine Blitz: steered plane, FIRE drops a cow, 0.8 s apart, 3 bombs. |
| `checkAirstrike` | Airstrike drops NumBombs one after another, BlitzDuration / NumBombs apart. |
| `checkTargetCursor` | Blimp targeting: TARGET inputs move the camera focus, not the worm; the strike lands at the view centre. |
| `checkReticles` | Every weapon, every step of a turn: only `Controls::reticle()` decides the on-screen reticle or Blimp cursor. |
| `checkNoDelays` | RULE_NO_DELAYS: preset weapon delays dropped; without it they apply. |
| `checkFatkins` | Fatkins leaves the plane along the strike direction and lands on the target. |
| `checkTailNail` | Tail Nail: 15 hp, victim pinned (no walking, no animals), a blast at its feet frees it. |
| `checkArmour` | Shield.DamageScale 0.25: an armoured worm takes a quarter of a blast. |
| `checkGirder` | Girder kit: preview stepped 0.3 m camera-relative, FIRE adds land, the turn retreats. |
| `checkBinoculars` | Binoculars: FIRE on an enemy solves a bazooka shot (no ammo, turn goes on); the solution hits. |
| `checkBubble` | Bubble Trouble: outside shots bounce or burst the shell, the worm inside untouched; ends after 6 turns. |
| `checkIcarus` | Icarus Potion: 500 ms drink (frozen, no weapon change), then cures and heals; JUMP flaps only in the 250 ms window of each 500 ms beat. |
| `checkCollectedUtilities` | Crate collect: Double Damage doubles this turn's blasts at once; Crate Spy marks the team for good. |
| `checkTunnelling` | Fast bodies against one-voxel land: sub-stepped moves never skip it; landings always count. |

## ai_check.cpp

AI vs AI. Prints weapon usage, damage per turn and planning cost.

| Check | Covers |
|---|---|
| `match` | One CPU-vs-CPU match: checksum, turns, shots (Fire events too: a 0 s retreat goes Aim → Settle in one tick), damage, winner, rope race finish; `main` also asserts the CPU uses Change Worm (W4M worm-select mode) and that Landmine, Scouser and Flood are planned in the single weapon runs. |
| `blimpView` | CPU turn shows the Blimp view only while its plan fires a targeted weapon, not on reselect or replay. |
| `pointBlank` | Enemy right next to the CPU: it attacks (10 of 12 seeds), with a close-range plan where it stands (melee, dropped explosive, mine) or a ranged weapon from over 5 m (W4M targets over 100 units from the move node, 0x49fcf6). |
| `shots` | First shots with a given planning budget: same plan sliced or in one tick. |
| `wallAhead` | Thin wall in front, enemy behind: no walk or jump against it, no shot into it, at levels 1, 3, 5. |

`main` also asserts: every map and level deals damage, most matches finish; CPU5 beats CPU1 in 2 of 3 games; every preset
scheme plays with sudden death; rope race reached at levels 1 and 5; Karma + Vampire match replays bit-identically.

## mission_check.cpp

| Check | Covers |
|---|---|
| `checkLot2` | On the W4M scripts: MineAllMine's 4 placed mines (none from its "MineN" details), Surrender emptied, a sunk Mine1 brings Mine2 (Payload_Deleted); DeathMatch6's factory (activation 15) drops mines on its 7th StartMineFactory; DoomCanyon's Water.Level 20; FastFoodDino's InitFuel; Shotgun2's PreSelected shotgun, and its shots pop a 25-hit-point Target (HighNoonHiJinx's crate) in the line of fire; Sniper's EndlessGun; TurkishDelights' point lights; CPU2 for AI teams with no CPUn copy. |
| `checkCratePlacement` (alone: `W4NX_MISSION=cratepos`, `W4NX_CRATEPOS=<id>`, `W4NX_SEED`) | Every mission 90 s: each rested crate within 0.3 m of 0.5 m x Scale over the ground, each pinned crate still at its marker (a re-created Index may move), SneakyBridgeThieves Crate5 on its bridge rail (y > 23.6) |
| `checkDeaths` (alone: `W4NX_MISSION=deaths`) | BuildingSiteSaboteurs, NoRoomForError, TheCrateEscape, MineAllMine: the player's last worm drowned, blown up (a mine at its feet, 20 hp) or walked off the map (into the sea) in its own turn: the turn ends and the script's TurnEnded fails the mission; NoRoomForError / MineAllMine: every enemy drowned in the player's turn reaches Worm_Died (DeadWorm.Id) and wins it. |
| `checkIdle` (alone: `W4NX_MISSION=idle`) | [ours] Games chained in one process (ChallengeNavigation2, DeathMatch1, SneakyBridgeThieves, ChallengeNavigation2 again, a local game): the human team's cpu is 0; with the intro skipped and no input, 20 s of `Controls::tick(Controls::read)` give a zero Input and the worm keeps its position and yaw. |
| `checkMovies` (alone: `W4NX_MISSION=movies`) | TinCanWally's Intro plays in the sim for 80.39 s (its last camera's look-at, 600 steps, holds the end); `SKIP_MOVIE` ends it at once; DestructAndServe's `JEFF` coded land frames (the DeLorean) leave no solid voxel once cleared. |

`main` also asserts: `Progress` save/load keeps done, best time and the `unlock` lines; with no imported mission it stops
there. Every imported mission runs with the AI on every team to its end with no Lua error and no missing data key; DeathMatch1
replays identically; the next story mission stays locked; an easter egg a run unlocks (WXMsg.EasterEggFound) is a
`Lock.EasterEgg.N` worth 1000 coins.

## replay_check.cpp

No static check: `main` plays a recorded AI match on the island and arabian (double damage). Save/load/re-sim gives the
same checksum and voxels; each instant replay lands on the live state: even replays restore the turn snapshot and
re-simulate, odd ones restore the checkpoint 45 ticks back, re-simulate half way and skip (`Snapshot::forward`). Prints
step, snapshot, restore and re-sim timings.

The game itself has replays off for now (user-requested, temporary): `constexpr bool REPLAYS = false` in ui.h skips the
turn snapshots and checkpoints (so no instant replay), the input recording and `.w4r` save, the Replays main-menu row and
the `--ui replays` / `playback` captures [ours]. `replay_check` drives `Recording` / `Snapshot` directly and still runs.

## ui_check.cpp

No static check: `main` selects each usable weapon in the HUD panel; the direct pick lands on it whatever the ticks per
frame (0 to 2).

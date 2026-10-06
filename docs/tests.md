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
| `make mission_check` | `mission_check` | `romfs/weapons.json`, `romfs/missions`, imported `assets/missions`; writes then removes `progress_check.txt` |
| `make ui_check` | `ui_check` | `romfs/weapons.json` |

`make ai_check replay_check mission_check` runs the three in one go (in parallel). The build is incremental: objects and
header deps in `obj/`, binaries in `obj/bin/` with `./<test>` linked to them; `make obj/bin/<test>` builds without running.

Repository-level scripts in `tests/` (run from the repository root, they build the client if `W4NX` is unset):

| Script | Covers |
|---|---|
| `tests/quit_check.sh` | Pause → Quit to menu stays on the menu: scripted keys (title, Local, Quick match, pause, Quit, arrows); fails on a second Play. |
| `tests/netbot.sh` | Two real clients (`--netbot`, the AI plays each owner's team) through a local server; fails on a desync. |

Timing runs (ours, not pass/fail): `./worms4nx --bench <map> [frames]` (CPU match, per-section ms and worst frame);
`W4NX_BENCH=<frames> ./worms4nx --shot <weapon index> [map]` (that weapon fired, e.g. 15 Airstrike, 18 Concrete Donkey:
the fire and explosion spikes); the log's `BOOT:` and `LOAD:` lines time startup and match loading.

### Frame pacing and hitch log (ours, 2026-10-05)

`log.txt` (`sdmc:/switch/worms4nx/` on Switch, `./` on desktop; raylib's log too) is written line by line until the
`BOOT:` line (a boot crash leaves its last line on the card) and for warnings; after it lines queue in memory and a thread
writes them each second (a Switch SD write blocks 15-25 ms, and newlib's 1 KB stdio buffer made one every ~3 HITCH lines:
the `other` 15-22 ms stalls of sw-log3). The thread is joined at exit: libnx has no `pthread_detach`, so
`std::thread::detach` aborts the process (ours, found 2026-10-05). In a match:
- `HITCH <ms>, <n> ticks, <n> chunks, <n> particles, <n> sounds | <section ms> ... other <ms> | loads tex .. fbo ..`: a
  frame over `W4NX_HITCH_MS` (default 20), 20 per 10 s at most, plus the window's worst frame when the limit held it
  back (`... (worst of the window)`). Sections are the perf overlay's, plus `camera` (logic, audio loops, camera) and
  `pip`; `remesh` = waiting for the meshing thread + uploading its chunks (`chunks` = chunks uploaded); `other` = input,
  audio update, net. Non-zero `loads` = something loaded on first use.
- `PACE 600 frames: ticks/frame 0:a 1:b 2:c 3+:d | frame avg / sd / max, n over 20 ms | jitter`: every 10 s.
  At 60 Hz, a healthy run is `1:600` (or `2:` only for real missed vblanks), sd and jitter well under 1 ms.
- `GPU <n> frames: avg / max ms, n over 16.7 ms | <section ms> ...`: GPU time per section from timestamp queries read 4
  frames late (no stall; GL 3.3, or GLES with `GL_EXT_disjoint_timer_query`, else the boot line `GPU: timestamp queries
  unavailable`). The swap is left out (its span is the vblank wait). A section the CPU submits slower than the GPU runs
  shows the submission gaps too.
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

Explosion frames (ours, 2026-10-05): chunk geometry is built on a meshing thread (core 2) while the frame renders
(`Terrain::remeshAsync`, after the sim: uploads what the thread built, hands it the dirty chunks; it starts no chunk past
10 ms but the first), and the frame waits for it before the sim (`remeshWait`: the sim is the only voxel writer). The
rebuilt chunks still swap in together. sw-log3 measured one chunk at 6-15 ms on Switch (A57), plus 15-25 ms when a
log write landed in it: 26-53 ms `remesh` hitches per explosion frame before. The HUD art (`hud/`, weapon icons, flags) loads during the
loading screen, the UFO beam shader at boot and the PiP render texture on the first match frame.

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
buffer slots per model (`Models` `Entry::Slot`): `models` cpu 0.15 -> 0.08 ms desktop. The animated normals went to the
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
| `checkTeamWeapon` | W4M: each team gets back the weapon it last had in hand, or the next one with ammo. |
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
| `checkFirstWeapon` | W4M Weapon.Create: turn starts on the last weapon if usable, else the first usable (Skip Go, Surrender skipped). |
| `checkAbduction` | W4M alien abduction: UFO lifts worms in reach nearest first, spits them out 2.1 s apart at half health. |
| `checkSuperSheep` | Super Sheep: walks, FIRE takes off (25 s flight), FIRE again blows it up. |
| `checkOldWoman` | Old Woman: steered, FIRE explodes, each enemy bumped loses 1-8 of a weapon to her team. |
| `checkDonkey` | Concrete Donkey: smashes down every 0.75 s until its 8 s LifeTime or the water. |
| `checkMineDuds` | Mine.DudProbability: about one mine in ten fizzles. |
| `checkMineBlast` | A blast only pushes a mine, up and away; it does not arm it. |
| `checkMineFlyby` | ArmingRadius 45 units: a worm blown past 2 m off arms it; a laid mine waits ArmingCourtesyTime. |
| `checkRopeShots` | Ninja.NumShots: 5 launches a turn; the hook catches a crate, which swings about the worm's feet at the rope's length while reeled in and out; jump lets go. |
| `checkRope` | Ninja rope in the open air: the hook angle and length, the yaw kept, gravity's first pull, the body on the circle, a damped swing past the bottom, the release velocity; the stick's swing (0x571820 thresholds); reel at 10 m/s within MinLength / MaxLength; a wrap round a bar and its unwrap; a bounce off a worm's collider. |
| `checkWaterShots` | Payload water: a Bazooka skims (SkimDamping, set on the Radius plane); a Grenade splashes, is disarmed at SinkDepth, sinks at 4..5 m/s without a blast and goes at Water.ExpiryDepth; a homing missile homing only splashes. |
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
| `forceWin` | Forces every objective each tick (kills, moves, pops targets): the mission ends won. |
| `forceLose` | Player team wiped out: the mission ends lost. |

`main` also asserts: every mission (bundled and imported) loads with all its worms, warns on worms in water or missing
markers; at least 3 bundled; AI on both sides of the first mission ends and replays identically; a shotgun pops a target
in `10_target_practice`; `Progress` save/load keeps done and best time, the next mission stays locked.

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

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

### Render budget (ours, `--bench <map> 600`, desktop GPU-synced, Switch estimate = max(5x cpu, 4x gpu))

| Change | Deathmatch1 | ChallengeNavigation2 |
|---|---|---|
| before wave 2 (4750d4f) | 18.5 ms (54 fps), remesh at load 68 ms | 18.8 ms (53 fps), remesh 68 ms |
| land vertex colour, sky clip, lens flare, Donkey dome | 18.5-19.3 ms (52-54 fps), remesh 50 ms | 18.9 ms (53 fps), remesh 49 ms |
| + W4M shadow map, measured as a 1024² land depth pass (+1.9 ms gpu) and 9 extra land texture taps (+1.3 ms gpu) | 32 ms (31 fps), objects not even counted | - |

The shadow map would leave no margin over 30 fps on Switch, so it is not drawn (docs/maps.md "Rendering").

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
| `checkSheepCamera` | W4M SheepChaseCamera: behind and above the sheep, rises when land hides it, never under it. |
| `checkEventCameras` | W4M event cameras: worm, crate, winner TrackCams, homing FlyCam, shoulder camera occlusion zoom; PiP during the active worm's turn then the grow at EndTurn, chase start yaw (Sheep / Scouser ResetYaw), Donkey camera held. |
| `checkDeathBlast` | W4M Worm.Death*: the death blast takes up to 35 hp off neighbours, throws them, digs 1.75 m; the settle waits for the thrown worm (W4M Worm Falling is active). |
| `checkWallClearance` | Concave corner: walking or dropping against a wall leaves the body out of the rock. |
| `checkWalkW4M` | Density clamped like imported .vox maps: corridors and steps walkable, ledges vaulted up to body height. |
| `checkVault` | W4M Vaulting: a 16-unit ledge in 250 ms; stick keeps it going, release drops back, jump ignored. |
| `checkNarrowSlot` | W4M 8 land probe points: a foot lands on the lips of a slot narrower than the stance; wider slot lets it in. |
| `checkHeading` | W4M: walking sets the facing to the stick direction at once, whatever the turn angle. |
| `checkWallStuck` | Off a ledge onto a 76 degree face, or wedged under a sloping ceiling: the worm lands, then walks out. |
| `checkJumpTrajectory` | W4M launch + Integrate: jump 50 units up, 80 along; backflip 80 up, 50.6 back. |
| `checkLaunchAtWall` | W4M launch from the eye against a thin wall: bazooka on its own side, shotgun on the near face, dynamite ahead. |
| `checkPointBlankDown` | Fired down at point blank: the shot passes the shooter's body, the floor blast hurts it. |
| `checkPayloadForces` | W4M payloads: wind adds Wind.Speed; homing missile has no gravity, homes only in stage 2. |
| `checkJumps` | DetectJump: double-press window, forward jump speeds, variant picked when the 300 ms window ends. |
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
same checksum and voxels; each instant replay (snapshot restore + re-sim) lands on the live state. Prints step, snapshot
and fast-forward timings.

## ui_check.cpp

No static check: `main` selects each usable weapon in the HUD panel; the direct pick lands on it whatever the ticks per
frame (0 to 2).

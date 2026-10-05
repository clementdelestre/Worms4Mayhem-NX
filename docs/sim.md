# Simulation: turn flow, input, movement (ours)

What `client/src/sim.h` / `sim.cpp` (`Game`) do, with the W4M source of each rule. The W4M side (exe timers, state handlers)
is in `docs/w4m/physics.md` §11 and `docs/w4m/turn.md` §14; this file is about our code. Status per fact:

- **data**: a value read from the W4M files (WEAPTWK, TWEAK, LOCAL, CAMTWK...);
- **disasm**: a rule read from `WormsMayhem.exe`;
- **assumed**: inferred, not verified in W4M;
- **ours**: no W4M source, our own choice.

The sim runs at 60 Hz (`Game::DT`); W4M times are in ms (`msTicks(ms) = ms × 60 / 1000`), W4M lengths in units of 1/20 m.
Same seed + same `Input` stream ⇒ same state on every client; `Game::checksum()` covers every field named here as "checksummed". The checks that
cover these rules: `tests.md`.

## Input

The per-tick `Input` (5 bytes) and its bits are in `PROTOCOL.md` "Input". The client builds it in `Controls::read` (once per frame:
buttons and axis rates) and `Controls::tick` (per tick: `diffuse` turns a rate into int8 steps, the rounding error carried to the next
tick); the CPU builds it in `Ai::think`. Weapon picks from the panel and from the CPU are direct (`Input::pick`, wire version 3).

## Phases (`enum class Phase`)

| phase | entered | the worm | leaves when | source |
|---|---|---|---|---|
| `Aim` | `beginTurn` | walks, jumps, aims, fires | `use()` of a weapon that sets `Flying`; the turn timer runs out; the worm dies or hurts itself (`selfHurt`); one team left | W4M Worm.Damaged.Current ends the turn (disasm, stdlib.lub) |
| `Flying` | a shot is out (`use()`) | may move once `retreating()` (below) | `shots` empty → `Retreat`; timer out, death, `selfHurt`, game decided → `Settle` | W4M 0x5833a0 / 0x54a0e0 (disasm) |
| `Retreat` | `Flying` with no shot left | may move | timer out (W4M Timer.RetreatTimedOut), death, `selfHurt` → `Settle` | disasm |
| `Settle` | end of the active part of the turn | no control | everything still, counts and deaths done, `POST_ACTIVITY` over → `beginTurn` | W4M DoPostActivity / WaitUntilNoActivity (stdlib.lub, data) |
| `GameOver` | `beginTurn` with ≤ 1 team alive (not in missions), rope race finish, mission result | — | — | — |

Weapons that keep `Aim` (the turn goes on): Ninja Rope, Jetpack, Teleport, Parachute, Armour, Binoculars, Icarus Potion, Change Worm
(`use()` sets no phase for them). Surrender goes straight to `Settle`; Skip Go to `Flying`: its PostLaunchDelay 3000 ms with the worm frozen, then a 0 s retreat
(W4M SkipgoUtilityLogicEntity 0x587ed0 BeginFireWeapon → Timer.EndTurn, EndFireWeapon 0x54a0e0; RetreatTime 0 at 0x588160; disasm + data). Girder goes to `Flying` (retreat) unless the Wormpot
Multiple Girders (`WP_MULTI_GIRDER`, W4M GirdersDontEndTurn, disasm 0x55ac30).

### Turn start (`beginTurn`)

In order: bubbles age one turn end (W4M Bubble.Lifetime, data); Icarus, girder preview, Double Damage and binoculars reset (W4M
DoPostActivity `SetData("DoubleDamage", 0)`, data); poison takes `poison` hp from each worm, never below 1 (W4M Worm.Poison, data);
game over check; sudden death (below); the next team in order with a living worm (`idle` teams skipped: mission captives), its next
worm in rotation (`nextWorm`); turn timer = `turnTime` s, hot seat = `hotSeat` s (W4M HotSeat 10 s, data); wind
`WIND_CAP[wind] × r²` along (cos, sin) of r2 × 2 × 3.14, `wind` / `windZ` (W4M stdlib SelectRandomWind, data; an xz vector 0x57eb25, disasm; levels 0, 3, 5, 10 / 10; HUD meter `Hud::draw`: downwind pointer in the camera frame, "NNm" = round(10 × |wind|), docs/w4m/render.md "Wind meter"; ours: drawn flat in 2D, so no perspective term, the tilt kept as a sin 0.75 squash of the screen-y component); the weapon in hand = `picked[team]`
(the weapon held when its last turn ended) if still `usable`, else `firstWeapon()` (W4M Weapon.Create 0x565770: first usable in list
order, Skip Go / Surrender last, disasm); `GameEvent::TurnStart`. Crates fall before, in `Settle` (below).

- **Hot seat**: the turn clock waits `hotSeat` s; any input but `TARGET` alone ends it (`sim_check` `checkHotSeat`). W4M (disasm): TimerLogicEntity
  0x50fce0 cancels the hot seat on any `Input.SomeInputFrom`, which InputTranslationService (0x505e20, 0x506850) sends for every key of a
  control group with flag 2; 0x504ee0 clears that flag on Menu, CameraSelect (Blimp view, first person: 0x4e1d10), Spectator,
  NetworkSpectator and ControllerRemoved. So the Blimp view key does not end it (our `TARGET`), movement, aim, jump, fire, fuse, panel and
  worm select do. The AI ends it too (`SomeInputFrom` "WormMoving", 0x4b4f10). W4M's InGame group (camera rotate / zoom keys, 0x4e1610) ends it
  too: `Controls::read` sets `Input::flags` `CAMERA` for the follow camera's keys, and any `flags` ends the hot seat.
  The press that cancels it is consumed ([user-requested], 2026-10-03; `Game::step`: buttons but TARGET / NEXT_WEAPON dropped that tick, held
  button not a new press). Not a deviation to remove: kept on request, to retest against W4M. W4M 0x50fce0 only cancels the hot seat; whether
  the same key also reaches the worm is not shown by the disasm.
  `Game::clock` (sudden death) does not count hot-seat ticks.
- **Sudden death** (rule 64): once `clock` reaches `roundTime` minutes, every worm drops to 1 hp (`SD_BOTH`, `SD_ONE_HP`), and with
  `SD_BOTH` / `SD_WATER` the water rises 1.25 m per turn up to +15 m (W4M Water.RiseAmount 25 units, data).

### Firing and the retreat

- `use()` spends the ammo and creates the shot. The phase becomes `Flying` and the timer = `post_launch` + retreat
  (`retreatTicks()`: the weapon's `retreat` ms, W4M RetreatTimeOverride, or the scheme's `retreatTime` s, W4M LandTime; data).
- `retreating()`: in `Flying` / `Retreat`, the worm may walk and jump once the timer is at most the retreat time, i.e. after
  `post_launch` (W4M Worm.WeaponDisableMovement until PostLaunchDelay's end, then StartRetreatTimer, disasm 0x5833a0 / 0x54a0e0).
  Not while a homing missile flies (W4M FlyCam disables WormMoving, disasm) or while a live shot takes the stick (`steered()`: old
  woman, super sheep, scouser before it inflates, Bovine Blitz bomber; ours).
- So the retreat starts at the launch: a worm can walk away while its bazooka flies (`sim_check` `checkRetreatInFlight`). The turn
  ends at the timer whatever the shot does; `Settle` then waits for the shots (W4M EndTurn waits for ObjectCount.Active, disasm).
- Rope and jetpack stay steerable in `Flying` / `Retreat` (W4M utilities outlast the attack, disasm).
- Weapon `post_launch` values (weapons.json, WEAPTWK PostLaunchDelay, data): Bazooka, grenades, gas 500 ms; Shotgun, Baseball Bat,
  Tail Nail 1000; Sniper Rifle 2000; Sentry Gun 420; Skip Go 3000; Weapon Factory weapons 500 (homing 0), forced in `Game::start`.
  `retreat` overrides: Dynamite and Landmine 5000 ms; strikes, donkeys, abduction, flood, old woman, scouser, Starburst 0; Skip Go 0
  (W4M sets RetreatTime 0, 0x588160). Every value matches `tweak.py` WEAPTWK; Homing, Poison Arrow, Sheep, Super Sheep, Fire Punch and
  Prod are 0 / -1 there. Icarus Potion 500: the drink (`icarus` 3), worm frozen and no weapon change, then the cure and the
  heal (W4M RedbullUtilityLogicEntity 0x587600 → Weapon.PostLaunchDelay → 0x587750, disasm); the turn stays in `Aim`. We have no Bridge Kit (1).
- Poison Arrow (`stick` 2 in weapons.json, W4M PreDetonationTime; docs/w4m/weapons.md "Poison Arrow"): a worm contact detonates it at once, a land
  contact stops it (`Projectile::stage` 1, heading kept in `aim`, `fuse` counting down) and it detonates 2 s later; the detonation has no damage, knock
  or crater and puts down the gas cloud (`Game::gas`), which poisons. `GameEvent::Arm` is the impact (ArmSfxLoop BowImpact). Not modelled: water skim, a
  Land.NewShape under the stuck arrow [ours].

### Settle (`case Phase::Settle`): W4M stdlib.lub EndTurn, as is (data + disasm)

- `timer > 0`: WaitUntilNoActivity. Shots flying or `Game::active()` (ObjectCount.Active, the users of 0x4d3af0: a worm falling or
  sliding 0x5aa996 / 0x5aaa04, a drowned worm afloat "Worm Dying" 0x5a7190, a crate till it rests "Crate Spawn" 0x5c9bd0, an armed or
  moving mine 0x57ee73 / 0x5778f0, a drum about to blow 0x5d1f86, a falling sentry 0x56cdc3, the FlyCam hold 0x528457). The gas
  cloud is not active: the only `EmitterIsOfInterest` emitter, WeaponGasCanJet, sits in the effect WeaponGasCan that nothing
  references (PARTTWK, WEAPTWK, exe strings, Lua: data). No timeout, as W4M.
- Then `timer < 0`: PostActivityTime 2400 ms, activity or not. At its end ApplyDamage (`applyDamage`): every hurt worm's display at
  once (`countGroup`, 2500 ms; [observed in W4M by the user, 2026-10-03] X sends `Input::SKIP_COUNT`, which ends the display at once, deaths still queue; the client then opens the weapon panel at the next Aim if the local player owns it (client UI state only, `Hud::reopen`); in Aim X skips the HUD poison count and the panel opens, the ready screen never hides an open panel), the dead queued (`deathQueue`, AddMeToDeathQueue at energy ≤ damage, 0x5abf13). Then CheckActivity.
- The death queue (`stepCount`, 0x4f9b30): once the displays are over and nothing else is active (thrown worms land first), the
  front worm's throes 3000 ms, its blast at its feet (0x5a9400), the next one a tick later. A living worm the blast hurts keeps
  that damage for the next ApplyDamage.
- DoPostActivity pass 1 (`crated`): ApplyPoison (`applyPoison`, 0x5ac060: poison never below 1 hp; an unhurt abductee gets
  rand % 100 hp, 0 kills; damage type 6 shows no display unless other damage is pending, 0x5abe38), ApplyDamage, DoubleDamage 0,
  then with two teams standing (or a mission): sudden death (RoundTimeRemaining 0; `sdType` = SchemeData SuddenDeath: all to 1 hp,
  nothing, or a draw), the water rise `waterSpeed` 0 / 4 / 8 / 16 units once it started, DropRandomCrate (0x4fab20, chance %),
  the Wormpot crate shower; then CheckActivity. Pass 2: delays, `beginTurn` (victory check, Turn.Ended).
- A team stands until its last worm died (its blast) or it surrendered (`surrendered`, SurrenderTeam 0x5b4d00: the Surrender utility
  and a vital worm, RULE_KING / Wormpot Vital Worm, flag 0x100 on each team's first worm; its worms stay). The turn ends once fewer
  than two stand (stdvs Worm_Died). Wormpot Vampire: the active team's first worm gains half of every other worm's damage at
  ApplyDamage (0x5a9710), poison included, and half a drowning worm's energy (0x5ad7c9).
- The round clock (`clock`) runs in the hot seat too (TimerLogicEntity 0x50f17d).

## Weapon in hand

- **Selection** (`Game::pick`, W4M LogicalWeaponManagerService::WeaponSelected 0x565d30, disasm): a direct pick (`NEXT_WEAPON` with
  `aim` = index + 1) needs `usable()` (ammo and no delay). `NEXT_WEAPON` with `aim` 0 steps to the next `selectable()` weapon
  (`nextWeapon`, ours: W4M has no next-weapon key on PC). No pick while a shotgun's second shot is pending (`shotsLeft`).
- **Secondary weapon**: with a movement tool out (`toolOut()`: rope, hooked object, jetpack in flight, open parachute in the air) or a
  landed jetpack with fuel (`jetLanded()`), picking Dynamite, Landmine or Sheep (`toolDrop()`) makes it `Game::secondary` and the tool
  stays in hand (W4M m_eSecondaryWeapon +0x98, 0x566310, disasm). Any other pick ends the tool and is wielded (0x565650). With the
  Wormpot No Bombing (`WP_NO_BOMBING`, W4M WormPot.NoParachuteDrops, data) a payload pick ends the tool too. The secondary is dropped by
  `FIRE` on the rope or parachute, `JUMP` in jetpack flight, `PITCH` without `TARGET` on a landed jetpack (W4M Fire.Second, disasm);
  when the tool goes (rope off, chute landed, jetpack dry) the secondary comes to hand (W4M 0x565920, disasm). `held()` is the
  secondary if any, else the weapon. Details: `weapons-audit.md` "Jetpack".
- **Delays** (`Game::delays[team][weapon]`, checksummed): a W4M preset brings its SchemeData `Delay` per weapon (`SCHEMES[].delays`,
  data; "*" = every Weapon Factory weapon); a custom scheme has none (W4M WXD.DefaultSchemeData, data). A delayed weapon is not
  `usable`; the panel dims it with its turns left (W4M FETXT.HTPSubtopic4) and the AI skips it. They count down at the end of each of the
  team's own turns (disasm: ActivateNextWorm 0x5b59b0 sends GameLogic.DecrementWeaponDelays at 0x5b5a5f, before it writes the new
  CurrentTeamIndex at 0x5b5b7c and not on the first turn; the send 0x6910e4 → 0x68cb82 calls the handler at once; 0x4f4df0 takes 1 off
  every weapon of `Inventory<CurrentTeamIndex>.WeaponDelays`, i.e. the team that just played, and says Comment.NewWeaponAvail at 0). Rule 128 `RULE_NO_DELAYS` (Match setup, "No weapon delays (test)") skips them (ours, for testing).
- **Fuses**: `Game::fuses[team]` 1..5 s, default 3, for `user_fuse` weapons (grenade, cluster, banana; W4M FuseUp, data). Timed fuses
  explode on exact multiples of a second.

## Crates and objects (`addObject`, `stepObjects`)

- **Start placement** [ours, W4M docs/w4m/turn.md "Start placement"]: `Game::placeWorm` per worm in team order (1000 random ground points above water + 1.5 m [assumed: the AI-grid walkable test], sphere 10 units free of placed worms, highest of 3, fallback Land.Center +-2.5 m at Land.MaxHeight + 0.5 m), then `addObject` mines and drums via `dropPoint` (100 tries, sphere radius + 5 units clear of worms and objects, none created on failure). Map `spawns` no longer exist (ours, removed). Deterministic from `rng`; sim_check `checkPlacement` asserts it over 6 maps x 30 seeds.

- **Drop**: at the end of each turn, with two teams or more standing, `crateChance` % (W4M SchemeData, data); 6 tries with the Wormpot Crate Shower (W4M
  GameLogic.CrateShower 0x4fb850, disasm). The contents: health / weapon / utility by the scheme's shares (W4M CreateRandomCrate
  0x4fa4b0, data), then a weapon by `crate_weight` inside its pool. Spawned 15 m (300 units) over a random land point above water, uniform over the land box, whose column misses every worm; no chute, plain gravity; bounces v = 0.2 (vx, −vy, vz), rests under 1 m/s (CreateRandomCrate 0x4fa52a Parachute 0, 0x5c6560, 0x5c9420, 0x5c8900, disasm).
- **Between turns** (data + disasm), as W4M: stdvs DoOncePerTurnFunctions sends GameLogic.DropRandomCrate in
  DoPostActivity's first pass, between two turns; CreateRandomCrate 0x4fa4b0 sets Crate.DelayMillisec to its argument, 0 from every
  caller (0x4fa986, 0x4fac16, CrateShower 0x4fb860), and Crate.WaitTillLanded defaults to 1 (0x4f21e9); the crate registers the active
  object "Crate Spawn" (0x5c9bd0, kept when WaitTillLanded = 1, 0x5c8049) and drops it once a bounce leaves it under 0.02 units/ms
  (0x5c8900). Then GameLogic_NoActivity → Timer.StartPostActivity (PostActivityTime 2400) → DoPostActivity's second pass → StartTurn
  (hot seat). `sim_check` `checkCrateBetweenTurns`.
- **Mid-turn crate**: no hold; the clock and control run (W4M: only EndTurn waits for "Crate Spawn"; TimerLogicEntity freezes on
  nothing else, disasm).
- **Collect**: any worm whose sphere (10 units, 5 above its feet) meets the crate's (10 units), any time (0x5cb7e0, disasm). Health: + `crateHealth` hp, cures poison and the
  abductee flag (W4M Worm.Antidote 0x5adecd, disasm). Double Damage, Crate Spy and Armour apply at once and never enter the inventory
  (`collected()`, W4M crate collect 0x5c9800, disasm). A Super Sheep collects mission crates for its worm.
- **Mines**: armed by any worm within 2.25 m (W4M Landmine ArmingRadius 45, data) after a 2.5 s courtesy (ArmingCourtesyTime, data);
  fuse = scheme `mineFuse` or 1..5 s random; 10 % duds (Mine.DudProbability, data). A blast only pushes a mine.
- **Steep ground**: objects slide past 60° like a worm (`wormBody` law, W4M SlideAngle_Default, data).

## Movement and collisions (shared with the AI)

Free functions in sim.cpp, also called by `ai.cpp` (`Mover`, `stepBody`) so the CPU predicts exactly what the sim does.
Worm body: centre `pos`, radius `R` 0.5 m, mesh half width `BODY_R` 0.3 m; eye `Worm.EyeLevelOffset` 15 units = 0.75 m above the feet
(data). Terrain: 0.25 m voxels (`Terrain::VOX`), water at 3 m (`Terrain::WATER`, rises with Flood and sudden death).

| function | does | source |
|---|---|---|
| `footing` | ground under the feet: the centre, or the W4M foot tripod (±4, −3) / (0, 5) units; one foot alone carries the worm only on ground under 60° | W4M land probe 0x91ffc8 (disasm) |
| `walkStep` | one tick of walking: steps up to `STEP` 0.25 m (5 units) at once; a 5..20-unit ledge (`STEP_UP` 1 m) starts a **vault** when `vault` is given (the AI passes its own); a face that holds the body back is climbed onto the highest ground the front foot finds; falls when it walks off a ledge (returns true), with Velocity = InputImpulse (`INPUT_IMPULSE` 2.5 m/s × stick) | W4M UpdateWalking 0x5b1285, Fall 0x5b14c7 (disasm) |
| `vaultStep` | the vault: 250 ms, 1/5 of the way per 20 ms (0x5a59f0), no collision test; releasing or reversing the stick puts the worm back where it started; snapped to the target at the end or when anything else moves it | W4M Vaulting 0x5aca80, ChangeState 0x5aa847 (disasm) |
| `fits` | the upper body (rods at 0.2 / 0.45 m, 7 points of radius 0.2 m) is out of land at `to`, or no deeper than at `from`: a worm already stuck may still move out | W4M Fits 0x59edf0 (disasm), our rod sampling |
| `clearWalls` | pushes the body out of side walls by the density gradient, ≤ 0.1 m a tick | ours |
| `flyBody` | free flight in sub-steps of ≤ VOX/2 (`substeps`), so nothing skips thin land; walls and ceilings bounce at `e` (0.3); a landing on ground steeper than n.y 0.2 rebounds, else keeps only the tangential speed; raised out of land while it fits | W4M Rebound 0x5acea0, Ballistic (disasm); jetpack contact 0x5633e9 |
| `jetBody` | the jetpack's own collider, one tick: the 4 feet and 4 heads (PROBE, heads 1 m up) are raycast along v; the earliest hit wins, a foot on a tie; a foot with the 3 rods clear lands the pack (v minus its normal part) whatever the normal, a head or blocked rods bounce v −= 1.8 (v·n) n; hits with v·n > −0.01 m/s are ignored. Ours, with the reasons: the rods are `Terrain::raycast` segments starting half a voxel above the feet, not `fits` (its 0.2 m ring is as wide as the foot offsets, so a foot on a wall always read as blocked, and its soft surface makes a foot on the ground sample solid); the stop is a 4-step bisection of the last 0.125 m raycast step; the −0.01 margin stops a worm sliding on land from landing at once. A landing hands over to `wormBody` once (slide or Ballistic, damage ignored) | W4M 0x59ec70, 0x59f1e0, 0x59edf0, 0x562f72, 0x5630dc (disasm) |
| `wormBody` | one worm tick: grounded test, slide (below 60° and slower than 3 m/s, 10 m/s on landing: stops; else gravity along the slope and friction 0.9582 a tick), a hard landing halves \|vt\|², gravity half before and half after the move | W4M Sliding 0x5afbe0, Integrate 0x5a6e90 (disasm); SlideFriction 0.95 / 20 ms (data); Wormpot Slippy: each slide value halfway to its Slippy one, 35°, 5.25 / 1.75 m/s, 0.9745 (0x5d59c0); Sticky: blast impulses × 0.5 only (0x5ad1ea) (disasm) |
| `walkerStep` | sheep, old woman, scouser on foot: steps up 0.6 m, hops at walls, whole-body roof test | ours |
| `muzzle` | the launch point pulled back to the last free point on the segment eye → spawn | W4M 0x585a29 (disasm) |
| `launchPoint` | the eye plus WEAPTWK LogicalLaunchZ / YOffset: dynamite 13 / −10, landmine 10 / −10, (super) sheep and Starburst 5, old woman 7, scouser 10 units, the rest 0 | data |
| `restOn` | a payload at rest drawn `r` above the land along its normal | W4M 0x574e90 / 0x5761f0 (disasm) |

Constants (sim.h / sim.cpp): gravity 12.5 m/s² (W4M Gravity −0.00025 units/ms², data; low gravity × 0.5, Low.Gravity.OnValue);
walk 3.0625 m/s (Walk.Speed, data; Quick Walk: VelocityScale 2, 0x5d6bc0, disasm); jumps: tapped or held forward (3.16, 7.91) m/s, held still: vertical 9.35 m/s,
pressed twice: backflip (−1.58, 10) or forward flip (1.58, 10) (W4M 0x5a5d30 / 0x95fb88 / 0x95fb7c, data); fall damage above 15 m/s:
trunc((v − 15) × 2) + 1 hp (W4M FallDamage 0x5ac3e0, FallDamageRatio 100, data; Max Fall: FallDamageRatio × FallingScale 2, Wormpot.lub, data; none when the scheme has fall damage off); no fall
damage in Icarus flight (W4M flag 0x40, 0x587446, disasm); none on a jetpack landing: a foot touching land lands the pack minus its normal speed (W4M 0x562f72, disasm), so `stepWorm` skips `land()` while `jet`; a dry pack falls and hurts as any fall (docs/w4m/physics.md FallDamage); the fall also rumbles the worm's pad: Heavy 100/255 for 500 ms (0x4bc410, disasm; GameEvent::Fall).

### Shots: launch and self-hit

- Off its feet and moving, the worm launches 1.5 m (30 units) further along its velocity and the shell, homing missile or mine
  inherits that velocity (W4M 0x585a52 / 0x585bc5, disasm); walkers get the offset only (ours).
- `Projectile::touching` (checksummed): the worm contacts of the last tick (bit per worm, 63 = mission target). Only a new contact is a
  hit, and on the first tick every contact counts as old, so a shot leaves its own worm and can hit it once it has left (W4M
  0x582200 / 0x581dc0, disasm). Guns (shotgun, sniper) fire from the eye and skip the shooter (W4M exclude id 0x5b27e0 → 0x519dd0).
- A homing missile flies 1.25 s straight, homes 4 s (+97.5 m/s², ≤ 29.5 m/s), flies 5 s straight and expires at 10.25 s, with no
  gravity (W4M HomingPayload 0x560aa0 / 0x560eb0, data + disasm).

## Blimp view and cursor (`TARGET`)

`targeted()` weapons (airstrike, Bovine Blitz, donkeys, abduction, teleport; not homing) aim from the W4M Blimp view (IsometricCam
0x52a5e0, disasm). The first `TARGET` tick of the turn places `Game::cursor` (`blimpFocus`: above all land, its centre ray on the aim
point), then `turn` yaws it at 0.605 rad/s, `walk` / `aim` move it at 25 m/s at full stick (client-scaled by its zoom: W4M MoveSpeed
250 u/s × MaxZoom 2), `PITCH` + `aim` tilts it at 0.495 rad/s (0..π/2), and it stays within 225 m of `landCenter()` (W4M 4500 units
of Land.Center, data). The target is the land, then the water, under the camera ray (`blimpHit`, W4M CMS 0x51c910); FIRE without a
target is refused (W4M NotClearToFire). Strikes fly along the view's right (`strikeDir`). Homing (observed: aimed from the worm, no Blimp): FIRE
in the first-person aim takes the aim ray's target (`locked`, `lockAt = target()`, W4M 0x583a10 accepts any view but Default), then a new press charges. Constants: `BLIMP_*` in sim.h; camera side: camera.md.

## CPU player (`Ai`, ai.h / ai.cpp)

- Plays through `Input` like a human (logged, relayed, replayed). Level `GameConfig::Team::cpu` 1..5 = W4M AITWK AIParams.CPU1..CPU5
  (data; `LEVELS` in ai.cpp: shot / direct / strike error, exchange, threat, randomise, crate and move weights, jump error...). A human
  team played by the AI is CPU5 (W4M /ALLAIPLAYERS).
- Planning is a state machine (`Mode::Eval`, `Search`, `Walk`, `Act`, `Jet`) cut into units under a per-frame work budget
  (`budget`, terrain samples; W4M 80 cost units per frame, 0x49b210); the plan never depends on the slicing.
- Shots are solved with the sim's own flight (`fly()`, `superFly()`, the gun copy, `launchPoint` / `muzzle` / `touches()`), 11 speeds per
  arc (W4M 0x4ace50), error ×(1 ± ShotError) from a game-derived seed (W4M 0x4a4580). Scores: damage, knockback, falls, drowning,
  chained barrels / crates, poison, karma and vampire (W4M 0x49ed30, 0x4a9260 target values).
- Moves: destination scoring then A* over a 0.5 m node grid (W4M AIPathManager 0x492d80, octile costs, jumps +40, backflips +60);
  each edge is played with `walkStep` / the jump code, vault included. Crates first when they score more (W4M CAIPlanCollectCrate),
  retreat planned with the attack. Strikes are aimed from the Blimp too; a homing missile is locked (one FIRE press, then released) and charged from the worm, like a human.
- W4M side: `docs/w4m/ai.md` §18; ours in detail, with the source of each rule: `ai.md`.
- Jetpack after a secondary drop: the turn enters the retreat (the dropped weapon's RetreatTime, `Game::launched`); the jetpack stays usable through it, a landed one takes off again with FIRE while fuel lasts [observed in W4M by the user, 2026-10-03].

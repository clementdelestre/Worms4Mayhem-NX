# CPU player (ours)

What `client/src/ai.h` / `ai.cpp` (`Ai`) do, with the W4M source of each rule. The W4M side (AIService, AITWK, AIPathManager) is in
`w4m-map.md` §18 "AI (CPU worms)"; this file is about our code. Status per fact, as in sim.md:

- **data**: a value read from the W4M files (AITWK.XOM AIParams.CPU1..CPU5, RTTI/strings);
- **disasm**: a rule read from `WormsMayhem.exe` (VA given);
- **assumed**: inferred, not verified in W4M;
- **ours**: no W4M source, our own choice.

Distances: W4M units / 20 = our metres (200 units = 10 m). Times: 60 Hz ticks, `s / DT`.

## Levels (`Level`, `LEVELS[5]`, `levelOf`)

- `GameConfig::teamSetup[team].cpu` 1..5 picks `LEVELS[cpu-1]` = AIParams.CPU1..CPU5 (**data**; W4M copies `CPU<level>` into the worm
  slot, AIService 0x4b3820, **disasm**). Values above 5 clamp to CPU5.
- `cpu == 0` (a human team played by the AI) uses CPU5, as W4M `/ALLAIPLAYERS` 0x4da163 (**disasm**).

| field (ours) | AITWK field | CPU1 | CPU2 | CPU3 | CPU4 | CPU5 | tag |
|---|---|---|---|---|---|---|---|
| shotErr | ShotErrorProjectile | 0.3 | 0.2 | 0.1 | 0.05 | 0 | data |
| directErr | ShotErrorDirectNonStrafe | 0.05 | 0.02 | 0.01 | 0.005 | 0 | data |
| strikeErr (m) | ShotErrorStrike /20 | 1 | 0.5 | 0.25 | 0.25 | 0 | data |
| exchange | WeightingWormExchange | 0.05 | 0.1 | 0.1 | 0.5 | 0.5 | data |
| secondary | WeightingExplosiveSecondaryDamage | 0 | 0.5 | 0.5 | 0.8 | 1 | data |
| threat | WeightingExplosiveNearbyThreat | 0 | 1 | 1 | 2 | 4 | data |
| nearby | WeightingPreferNearbyTargets | 1.5 | 1 | 0.5 | 0.1 | 0 | data |
| randomise | WeightingPlanScoreRandomise | 0.2 | 0.2 | 0.1 | 0.2 | 0 | data |
| humans | WeightingPreferAttackHumans | 0.8 | 1 | 1 | 1.2 | 1.8 | data |
| fireDelay (s) | DelayBeforeFire | 0.5 | 0.5 | 0.5 | 0.5 | 0.2 | data |
| nonFirstMove (s) | DelayBeforeNonFirstMove | 2 | 0 | 0 | 0 | 0 | data |
| collect | AddScoreCollectSomething | 1000 | 5000 | 20000 | 30000 | 60000 | data |
| lowHp | LikeToCollectHealthWhenHealthBelow | 25 | 25 | 25 | 25 | 50 | data |
| moveFar (m) | ReduceMoveScoreFurtherThan /20 | 5 | 10 | 5 | 5 | 5 | data |
| moveTime (s) | ReduceMoveScoreIfTimeLeftLessThan | 40 | 30 | 30 | 20 | 20 | data |
| memory | MemoryImproveAccuracyEffect | 0.2 | 1 | 1 | 1.5 | 1 | data |
| jump | MovementJumpForwardAllowed | no | yes | yes | yes | yes | data |
| flip | MovementJumpBackflipAllowed | no | no | no | yes | yes | data |
| jumpErr | MovementJumpError | 0 | 0.2 | 0.1 | 0.05 | 0 | data |
| sweet (m) | ProjectileSweetSpotDistance /20 | 0 | 0.25 | 0.25 | 0.5 | 0.25 | data |
| cluster / gas / homing | PrefClusterGrenade / GasCanister / HomingMissile | 1/1/0.9 | 1/1/0.9 | 1/1/0.9 | 0.8/0.5/0.8 | 0.3/0.5/0.7 | data |

- PrefProd 0.3 is hard-coded in `evalWeapon` (same at every level, **data**). Other Pref* are 1 (**data**).
- Strafe mode: none in W4M either. ShotErrorDirect is read only by CAIPlanAttackDirectActionable's slot 11 (0x4a0d70, vtable 0x823910),
  which Shotgun and Sniper Rifle override with ShotErrorDirectNonStrafe (0x4a0d90 at 0x823970 / 0x8239a4); StrafeTowards (0x4974d0,
  StrafeProgressiveErrorScale +0xe0) is queued only under plan flag 0x200 (0x49ebed), which no CAIPlan constructor sets (flags
  0x147, 0x1d7, 0x1147, 0x170, 0x2444, 0x541, 0x1047, 0x40, 0x140, or 8 added) (**disasm**). Both fields are dead.
- Not ported: ConsidersStrikeThrustDirection, WeightStrikeSecondaryTarget,
  WeightingWormVital, JetpackAboutToCrossLineLookAhead, AddScoreMove / AddScoreMoveIfNotMoved, StrikeSweetSpotDistance,
  ClusterDistanceAboveTarget, WeightingPunchThroughLand, BestMissShotMultipleUse. (MortarMaximumAimAngleAllowed has no W4M read either.)

## Aim error and wind (`finish`)

- Applied once the plan is chosen, before aiming. Seed `r = (salt ^ shotsLeft·2654435761) + walks·40503`, LCG `r·1664525 + 1013904223`,
  `noise()` in [-1, 1) (**ours**; W4M random 0x4a4580, **disasm**). `salt` is turn-start state, so the error never depends on think length.
- Shells / homing: the launch vector `dir(yaw, pitch) × charge/90` has each component scaled by `1 + e·noise()`, e = shotErr; yaw, pitch
  (clamped -1.2..1.45) and charge (1..90 ticks) are re-derived. W4M 0x4a06c0 scales the exact launch velocity (**disasm**); ours scales the
  charge vector, since the AI charges like a player (see Timings).
- Shotgun / Sniper Rifle (`Kind::Shotgun`): same scaling with directErr = ShotErrorDirectNonStrafe (W4M 0x4a0d90, **disasm**). No strafe mode.
- Airstrike / donkeys: `yaw += atan(noise·strikeErr / dist)`, a sideways offset only. W4M: a target offset in units (0x4a1b00, **assumed**).
- Melee and dropped shells (`dropped()`: Dynamite, Cheap Dynamite...) get no error (**ours**).
- Wind: every plan flies with the exact `g.wind` at every level (W4M solver 0x4ac6f0, "wind never degraded", **disasm**).
- Repeat-shot memory (`memory`, W4M AIPlanMemory ImproveAccuracy): e /= 1 + memory·Σ match (0x4a5d00), match = effect × (1 − d_from/R) ×
  (1 − d_at/R) when both distances are under MatchRadius R = 200 units, 10 m (0x4a5640), over every record, any worm or team. A record is
  stored at `finish` with effect 1; each think start (`regress`, W4M RegressImproveAccuracyMemory 0x4a6080) multiplies it by 0.95 and drops
  it under 0.1 (**disasm**, **data**). Cleared on a new match (`g.clock < lastClock`, **ours**).
- Failed-plan memory (`failed`, `recent`): `finish` stores the attack (weapon, worm, target worm, its position and hp, effect 1; W4M
  StorePlanAttackMemory 0x4a6360, not for a plan without a target). At the next think start (`regress`, CheckPlanResult 0x4a6ab0) it is a
  failure if the target has exactly the same hp and position. Each think start multiplies a failure's effect by 0.99, sets it to 0 once the
  target's hp or position changed, and drops it under 0.1 (RegressFailedMemory 0x4a5b10). A candidate of the same worm at the same target
  is scored × (1 − effect/2), × (1 − 0.1·effect) with another weapon (the effect × 0.2, 0x4a6590), before the taste (**disasm**).
- Worm-select mode and skipped-turn memory (W4M 0x4a4f2a, flag 0x9560c1, **disasm**): with a usable Change Worm at the start of a turn
  (no move yet, no shots pending), `startEval` plans for every worm of the team (`me`, `selWorms`): ours lists the worms our Change Worm
  reaches with the ammo left, one use a step (W4M's Worm Select picks any worm: ours). A worm with no positive plan stores a skipped-turn
  record (0x499cac → 0x4a68c0, effect 1); while selecting, every plan is scaled 1 / (1 + 5·Σ effect) of its worm's records (0x4989d0 →
  0x4a57c0); each think multiplies a record by 0.9 and drops it under 0.1 (0x4a61b0). The best worm is selected (W4M WormSelect action;
  ours presses Change Worm until it is current, `selTarget`), then planned again as the current worm. While selecting, the think waits
  for every worm to stand still (**ours**: the plans of the other worms must not depend on the slicing).
- Damage: hp per worm, armour → `hp × Game::ARMOUR / 100`, kick ×0.5 (ArmourLogicEntity 0x548fc0, **disasm**; melee goes through `strike()`, no armour, as W4M ids 10..12).
- Score `s += 2 · value(i) · d` per hit worm: 2 = WeightingAttack (**data**). d = hp lost; a kill (lost ≥ hp) is `max(hp, 200)` =
  WeightingKillTarget 200 (**data**).
- Knockback: `fling()` runs `wormBody` up to 300 ticks with fall damage (FALL_SAFE 15, FALL_SCALE 2, copies of sim.cpp); fall hp adds to d.
  Only when threat > 0 (**ours**).
- Drowning: d = lost + min(1, threat/4)·(kill − lost) (W4M "may knock X into nearby threat"; exact blend **assumed**).
- Chains (secondary > 0): barrels (`BARREL_BLAST`) and weapon crates (`CRATE_BLAST`) in reach blow up with share ×secondary, depth ≤ 2;
  mines are only pushed (**disasm** for the field; recursion, depth and kinds **ours**). Sentry hit ±15, health crate lost −5 (**ours**).
- Poison: a survivor adds `2 × (new poison − current)` ("about 2 turns", **ours**). Gas clouds: a second blast of `GAS_RADIUS − R`, no wind drift.
- Clusters: one extra blast, reach ×1.5, damage ×clusters×0.4 (expected bomblet share, **ours**).
- Karma (`RULE_KARMA`): self takes 0.5 × damage dealt to others. Vampire (`RULE_VAMPIRE`): −2·value(self)·0.5·min(leech, 200 − hp), leech =
  0.5 × enemy damage (**ours**; no W4M AI term in §18).
- Tie-breaker: −0.05 × distance from the first blast to the nearest enemy (**ours**).
- Dropped shells: self damage is ignored, `retreat()` walks clear (**ours**).

Target value `value(i)` (W4M 0x4a9260, **disasm**):

- v = (1 + 0.04·hp + max(0.1, 1 − 0.08·poison)) × (2 if last of its team) — WormHealth 0.04, WormPoisoned 0.08, LastInTeam 2 (**data**).
  W4M multiplies by Vital first and adds the poison term after; ours has no Vital flag (CPU worms are all value 1).
- K = (foes / friends)^exchange. Ally (self included): −v·K. Enemy: v/K × (10 / max(d, 10))^nearby × (humans if human team) (**disasm**).
- ThisWormValue (1) and WormNearbyWorms / WormMilesAway are not ported.

Plan rank (W4M 0x498a0a, 0x49c060, **disasm**):

- `taste = (1 + randomise·n) × pref × (0.6 if weapon == g.picked[team])`, n from `salt ^ wi·2654435761`, one per weapon per turn.
- `rank = score × taste` for positive scores, else `score`. 0.6 = 1 − PreferVariety 0.4 (**data**); W4M matches recent plans, ours only
  last turn's weapon.
- Failed-plan memory: see above. "Best miss" punch-through plans are not ported.

## Shot candidates (`evalWeapon`)

One unit = one (weapon, target, candidate). Targets (`startEval`): each enemy, the ground 1.5 m short of it, the sweet spot (below),
and barrels / weapon crates within 7 m of an enemy when secondary > 0 (**ours**).

| kind | candidates | tag |
|---|---|---|
| Shell | flight times t = 0.2..4.5 s step 0.43 (11 arcs), exact `V = (T − P − A·t(t+DT)/2)/t`; reject if speed outside `launchSpeed(0)..speed`, pitch outside -1.2..1.45; charge `n = round(frac·90)` ticks; flight checked with `fly()` | W4M 11 speeds 0x4ace50 (**disasm**); ours samples times |
| Dropped shell | set down at the feet facing the target | W4M CheapDynamite* (**data**) |
| Homing | pitch at the target, reticle within 2.5 m of it, charges 30 / 60 / 90; `fly()` homes on the reticle | **ours**; never locks from the Blimp |
| Shotgun / Sniper | one straight ray (60 m), first worm on it takes `damage` per shot; re-aimed for each shot (`act`) | W4M Direct (**data**) |
| Melee (incl. Tail Nail) | within 3.5 m; yaw ±0.8/±0.4/0 × pitch 0/0.5/1 (15), `meleeHits` | **ours** |
| Sheep / Old Woman | `sheepWalk` copy; kept if closest approach < 2 m; FIRE in flight within 1.2 m of an enemy | **ours** |
| Super Sheep | pitch 0.3 / 0.9, flown by the `steer` autopilot (`superFly`), detonated within 1.5 m | **ours** |
| Airstrike / Donkey | yaw 0/±0.25 × pitch {at target, 1.45}; bomber (Super Airstrike, fuse > 0) steered in flight and dropped with lead; Donkey/Fatkins: first impact scored twice | **ours** |
| Starburst | as Super Sheep (`superFly`), plus its rider's death: all its hp on the thinking worm | W4M CAIPlanAttackStarburst 0x4a43a0: 0x49ed30 on the active worm with its hp (**disasm**) |
| Landmine | laid at the feet facing the target: `MINE_BLAST` on the ground under the launch point, self ignored as a dropped shell | W4M CAIPlanAttackLandmine = CloseRangeExplosive 0x4a2c70: the blast at the worm (**disasm**); the drop point and self **ours** |
| Inflatable Scouser | our scouser's walk (`walkerStep`) until it touches a worm; that worm takes `damage`, or a kill if a drop of 1.2 m/s × `SCOUSER_FLOAT` drowns it, plus the fall damage | W4M CAIPlanAttackScouser exists (**data**); W4M scores an animal as its blast at the target (0x4a4210); ours scores our scouser, which swallows and drops (**ours**) |
| Flood | one plan: every worm under `water + speed` (2.15 m, W4M Flood.Delta 43) counts a kill | W4M CAIPlanAttackFlood 0x4a3640: each target under Water.Level + Flood.Delta, 0x49ed30 with damage 1000 (**disasm**) |

Sweet spot (`awayFromThreat`, W4M 0x4a9be0 / 0x4a9e90, **disasm**): 8 directions × probes at 2.5/5/7.5/10 m, weight 2/(k+2); no land or
water 0.2, drop > 1.5 m 0.05, mines 0.1·(1 − d/5), other worms 0.05·(1 − d/5). Aim point = enemy + sweet × away-from-worst. Differs:
no cap at 1, worms get a falloff, the GameLogicService object term is not ported.

## Weapons the AI never uses

- Sentry Gun, Alien Abduction, Bubble Trouble, Girder, Teleport and the other utilities (Armour, Icarus, Double Damage...): no W4M plan (**data**).
- Ninja Rope: only in `RULE_ROPE_RACE` (`race()`, a parametric swing search, **ours**); W4M NINJA_ROPE path is probably never run (**assumed**).
- Parachute: no use (W4M has PARACHUTE path moves, **data**). Jetpack: only as a move (below). Fuse is never changed (`fuseOf`, W4M SetWeaponFuse exists).
- SkipGo when no positive plan and it is owned (W4M CAIPlanSkipTurn, **data**).

## Decision (`decide`)

1. `canMove` = walks < 3, not nailed, turn time at think start > 10 s (ForbidMoveIfWouldLeaveTimeLessThan 10, **data**).
2. Crate (once): crates not falling, |dy| < 3 m, horizontal < 18 m (**ours**). s = collect × (3 if health crate and poisoned or hp < lowHp)
   × moveFar/dist past moveFar × left/moveTime under moveTime (**data**, 0x4a72e1, 0x4a75c0). If s > plan.rank: crate search.
3. `plan.score ≥ 5`: fire (threshold **ours**).
4. Closer (once, walks < 2): search toward the plan's target (W4M CAIPlanMoveCloserToTarget, **data**).
5. Jetpack (once, if owned): 16 headings × 6 / 12 / 18 m; fly if `spot()` beats here + 5 (**ours**; W4M JETPACK path move, **data**).
6. `plan.score > 0`: fire; else SkipGo (W4M 0x49e6d0: negative plans forbidden, **disasm**). Never teleports.

## Pathing (`Search`, `searchStep`, W4M AIPathManager 0x492d80)

| parameter | ours | W4M | tag |
|---|---|---|---|
| grid | `nodeSpacing`: sqrt(Σ box areas / 16000); one xz box per island of land above `Terrain::WATER` (0.5 m columns, 8-connected), overlapping boxes merged, once a match; key = cell x, z + 1 m y layer | sqrt(Σ NodeGrid box areas / 16000) (0x4b2a68), the grids merged where they overlap ("second merge pass", 0x4ae320), x/z + layer byte | disasm; islands as the land pieces: assumed |
| neighbours | 8, the 4 sides first, each edge simulated with `runStep` (walk, jump, backflip); a diagonal walk only once both of its sides were walked from this node | 8, a diagonal only if both sides are walkable (0x492510) | disasm; "walked from here" for "walkable": assumed |
| G cost | octile of the real displacement, +40 jump, +60 backflip | 10 / 14, +40, +60 (0x491fd8, 0x492008, 0x492003) | disasm |
| heuristic | octile 10·max + 4·min to the nearest goal cell | same (0x4923a9) | disasm |
| F | G + H | G + H (0x492d80) | disasm |
| jump edges | every direction, gated by jump / flip; an arc that hits a wall in the air (horizontal speed halved) is refused | per allowed type over a precomputed reach table (0x4924d7) | disasm; the wall test: assumed |
| iterations | `MAX_ITER` 200 per pathfind, every purpose; spread over frames by the budget (one node pop or one edge a unit) | 200 per pathfind (0x4b0ba6), 100 per step call (0x492e4e) | disasm |
| time | edge refused if path ticks + h at walk speed > `limit` | "Not enough time left to follow path" | data |
| goal | same cell, |dy| < 1.5 m | | ours |
| partial | best node (lowest h, then g) if ≥ 20 octile from start and scores above start; crates need the full path | accepted unless too short | disasm; minimum ours |
| repath | a walk step held off its node (`stuck`) blocks that node (`blocked`) and pathfinds again to the same goals (`repath`); past 2 repaths the worm may not move this turn (`walks` = 3); also during the retreat | path-failed blockage, repath, "too many repaths, forbidding further movement" past 2 (0x490551 → 0x490601) | disasm |

- `runStep` plays the step with the real `walkStep` / `Game::jumpTick` / `vaultStep` (vault included: the stick stays held while vaulting);
  it fails on drowning, fall damage, a stuck walk (120 ticks), > 600 ticks, or a spot where `fits()` fails (W4M Fits 0x59edf0, 3 rods, **disasm**).
- `stepInput`: walk turns at 2.5 rad/s, walks when off by < 0.3 rad with stick ∝ distance; a jump aligns within 2e-3 rad then presses JUMP,
  a second press makes a backflip (yaw + π); the air phase ends when still or after 400 ticks.
- Jump error (`takePath`, W4M 0x496f24, **disasm**): each jump's displacement scaled per component by 1 + jumpErr·noise; only the heading changes.
- `limit`: retreat = `retreatTicks + msTicks(postLaunch) − 60`; others = turn time at think start − 10 s.

Destinations (W4M ScoreAllMoveNodes 0x4ab490, 21×21 window 0x4ab5bc, **disasm**): Closer and Retreat = 21×21 at the node spacing (the
retreat node too comes from that 21 × 21 × 2 window: the move → retreat pairing 0x4a8c60, **disasm**), each grounded by a 14 m ray; keep those above start score + 1, best 3 (**ours**).

- Closer `spot()` filter, then −distance to target. `spot`: rejects water + 2 m, solid, mines < 2.5 m; −10 per enemy sentry in range and
  sight; best enemy within 40 m in sight: 20 − 0.4·|d − 15| + clamp(0.5·height, 0, 5); −10 under 4 m (**ours**).
- Retreat `haven()`: nearest enemy (cap 20) + 0.5·min(dist to blast, 10), −4 per enemy in sight < 40 m, −20 per mine < 3 m, −5 per enemy
  sentry in range, rejects water + 1.5 m (**ours**). Planned at `finish` from the firing yaw, as W4M "move to ..., retreat to ..." (**data**).

## Timings

| what | ours | W4M | tag |
|---|---|---|---|
| before thinking | none; waits for the worm to stand still, not while a crate drops | DelayAtStart 0 | data |
| before first move | 0 | DelayBeforeFirstMove 0 | data |
| before a later move | nonFirstMove (CPU1 120 ticks) | same, minus think time (0x4983ef) | data; ours does not subtract |
| after a path | 30 ticks (0.5 s), then think again | Delay 0.5 s (0x49e6d0) | disasm |
| aim | turn 2.5 rad/s, pitch 1.5 rad/s through `Input`, aligned within 2e-3 rad | SetAimAngle sets it at once | ours |
| before fire | fireDelay / DT = 30 ticks (CPU5 12), counted once aimed | DelayBeforeFire (0x49eb8b) | data |
| charge | FIRE held `plan.charge` ticks (1.5 s full) | exact SetLaunchVelocity, no bar | ours |
| retreat | 60 ticks (1 s) after firing, while the shot flies | Delay 1.0 s then Path | disasm |
| jetpack | at most 600 ticks of flight | | ours |

## Per-frame work (`slice`, `unit`, W4M 0x49b210)

- `budget` = 20000 `Terrain::samples` per frame (~0.7 ms desktop). Each `unit()` costs its samples + 100; units run while `debt < budget`;
  `debt` carries over, capped at 2 × budget (W4M: 80 units/frame, pathfind and attack plan 100, position 10, **disasm**; scale **ours**).
- A unit is one of: one A* step (score one destination, pop one node, or simulate one edge), one sweet-spot probe, one row of `landTop`
  columns (every 8th x, z step 8; only when the team owns an airstrike), one shot candidate. Unusable weapons are skipped for free.
- Modes: `Eval` (targets, sweet spots, landTop, candidates, then `decide`), `Search` (crate / closer A*), `Walk` (`follow` the path),
  `Act` (retreat A* first, then select, aim, wait, charge / fire), `Jet` (waiting for take-off; `jet()` steers the flight).
- In `Flying`: retreat, steer the bomber / super sheep, detonate sheep near enemies. Roped outside a race: release with JUMP.
- `race()` (rope race) uses the same budget: 1 + 54 swings + 12 climbs, each simulated up to 400 ticks.

## Determinism

- The AI reads `Game` and returns an `Input`, logged, relayed and replayed like a human's (**ours**).
- Every seed comes from turn-start state: `salt = (g.rng ^ hash(hp)·2654435761) + current`, never `g.clock`; decisions use `thinkTimer`
  (turn time at think start), not the time the think took.
- The result never depends on slicing: units are evaluated in a fixed order, `stable_sort` on destinations, `landTop` is a max.

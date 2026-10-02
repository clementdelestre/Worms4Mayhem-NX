# W4M AI (CPU worms)

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 18. AI (CPU worms)

Tags: **data** = read from strings, RTTI or decoded XOM; **disasm** = read in code; **assumed** = inferred. VAs are for WormsMayhem.exe (base 0x400000). "units" means W4M world units.

### Classes and source units [data: RTTI and .cpp strings]

- **AIService** (vtable 0x825cf8, HandleMessage 0x4b4260, subscribe 0x4b3390, setup 0x4b3820). It waits for three things before thinking: `AISceneGraphService` has updated the map, the active worm is ready, and `ObjectCount.Active` has dropped to 0. Then it runs a think and executes the queued actions. Messages: `GameLogic.AITurn.Started`, `AI.ExecuteActions`, `AI.PerformDefaultAITurn`, `AI.WeaponsDontEndTurn` (no retreat).
- **AISceneGraphService** (vtable 0x825a7c): builds the pathing node grid (`AI.PopulatePathingNodes`, `Land.NewShape`), adds jump nodes (`Worm.Jump.Forward`, `Worm.Jump.Backflip`, `WXWorm.AftertouchDelta`/`Strength`) and adds blockages where a path failed. A* lives in AIPathManager.cpp. A* is bounded: 200 iterations per pathfind (0x4b0ba6), 100 per step call (0x492e4e) [disasm].
- **CAIPlan tree** (AIPlan*.cpp). A think spawns many plans, scores them, refines them over several frames ("Thinking took N frames", "ScoreAllMoveNodes slice"), then queues the actions of the best plan.
  - `CAIPlanSeed`, `CAIPlanSkipTurn`, `CAIPlanDefense`.
  - `CAIPlanMove` → `CollectSomething`/`CollectCrate`, `DoRandomSmallMove`, `MoveCloserToSomething`/`MoveCloserToTarget`.
  - `CAIPlanAttack` → `WithMoveAndRetreat` → `Projectile` / `Direct` / `Strike`, plus `CloseRange`, `Animal`, `Special` and `Flood`.
- **Attack plan per weapon** (`Cannot use kWeaponX` strings). These are the weapons the AI can use:
  - projectile: Bazooka, Grenade, ClusterGrenade, BananaBomb, HolyHandGrenade, PoisonArrow, GasCanister, HomingMissile (targeted)
  - direct: Shotgun, SniperRifle
  - strike: Airstrike, ConcreteDonkey, SuperAirstrike, Fatkins
  - melee: BaseballBat, Prod, FirePunch
  - close-range explosive: Dynamite, Landmine, and CheapDynamite{Grenade, ClusterGrenade, BananaBomb}, i.e. those weapons dropped at the AI's feet
  - animal: Sheep, OldWoman, Scouser, SuperSheep, Starburst
  - Flood
  - SkipGo (`CAIPlanSkipTurn`; scripts must give the AI infinite SkipGo)
  - The only plan without a named weapon is `CAIPlanAttackSpecial` (vtable 0x823dcc): its spawn 0x4a3560 creates the Flood plan when Flood (14) is usable; it is the Flood plan's parent [disasm].
  - No plan class exists for Girder, BridgeKit, Teleport, LowGravity, Redbull, SentryGun, BubbleTrouble, NoMoreNails, AlienAbduction or Pipe.
- **AITurnAction subclasses**, queued by `Queue Action: ...` (AIPlan.cpp) and run by AITurnEntity: Delay 0x497120, SetWeapon, SetAimAngle 0x4965d0, SetLaunchVelocity 0x495a60 (`AI.LaunchVelocity`), SetWeaponFuse 0x496ab0, SetWeaponTarget 0x495d80, SetStrikeDirection, SetWormOrientation, FireWeapon 0x496860 (sends `c_MsgFireReleased`), Path 0x496c90, StrafeTowards 0x4974d0, DetonateWhenGoingAwayFrom 0x4977e0, MoveForwardTillInMeleeRange, WaitForSuperAirstrikeReady, WormSelect, Rethink 0x499830, SetCamera.
- **Path move types** (AIPathAction strings): WALK, with three on-fail policies (keep trying / skip to next move / rethink forbidding the move), JUMP_FORWARD, JUMP_BACKFLIP, JUMP_UP, JUMP_UP_NO_AFTERTOUCH, JETPACK, PARACHUTE_START, PARACHUTE, NINJA_ROPE, SUPER_SHEEP, DELAY. AIActionPath has log strings for super sheep, parachute, jetpack and jump, but none for rope. Move types are 0..2 the WALK variants, 3 JUMP_FORWARD, 4 JUMP_BACKFLIP, 5 JUMP_UP, 6 JUMP_UP_NO_AFTERTOUCH, 7 JETPACK, 8 PARACHUTE_START, 9 PARACHUTE, 10 NINJA_ROPE, 11 SUPER_SHEEP, 12 DELAY (name switch 0x48ef40). Moves are only created from A* edges (0x4b0c3c → 0x48ff20: walk 0, jumps 3/4 from 0x492510), the super sheep plan (0x4a3405 → 0x490010, type 11) and melee (0x4a2adf → 0x490120, type 2). JETPACK, PARACHUTE* and NINJA_ROPE have an executor case (0x48f3c0) but no creator: the CPU never jetpacks, parachutes or ropes [disasm]. Repath happens on failure, with a limit ("too many repaths, forbidding further movement").

### AITWK.XOM [data: xom.py / tweak.py]

- 24 `AIParametersContainer` with 111 fields (`pe.py schema AIParametersContainer`): `AIParams.CPU1`..`CPU5`, `CPUTest`, `Worm00`..`Worm17`. All `Worm*` are equal to `CPUTest`. TwkEdVer = 124.
- There are **5 CPU levels**. AIService 0x4b3820 loops over worm slots 0..15. For each one it reads the team's CPU level (team byte +0x74; 0 = human; assert `uCPULevel <= 5`), then copies `AIParams.CPU<level>` into that worm's `AIParams.WormNN` slot (DRM::GetWormAIParameters 0x50c440) [disasm].
- The command line flag `/ALLAIPLAYERS` (0x4da163) sets 0x959ac4, which turns human teams into CPU5 [disasm].
- At think start (0x4a4920), the params of the thinking worm go into the global `c_pAIParameters` 0x9560f4. The worm position goes into 0x9560fc [disasm].

Values that differ between levels (CPU1 / CPU2 / CPU3 / CPU4 / CPU5). Every Pref* not listed is 1.0.

| field | 1 | 2 | 3 | 4 | 5 | meaning, with read site [disasm unless noted] |
|---|---|---|---|---|---|---|
| ShotErrorProjectile | 0.3 | 0.2 | 0.1 | 0.05 | 0 | 0x4a06c0: each launch-velocity component is scaled by 1+e·(2r−1) (0x4a4580). This covers angle and power together |
| ShotErrorDirect | 0.3 | 0.2 | 0.1 | 0.05 | 0 | 0x4a0d70: direct weapons (shotgun/sniper), strafe mode |
| ShotErrorDirectNonStrafe | 0.05 | 0.02 | 0.01 | 0.005 | 0 | 0x4a0d90 |
| StrafeProgressiveErrorScale | 0.99 | 0.98 | 0.96 | 0.95 | 0.9 | passed to StrafeTowards only, which no plan queues (plan flag 0x200 is never set): dead [disasm] |
| ShotErrorStrike | 20 | 10 | 5 | 5 | 0 | 0x4a1b00: the strike target (+0x14) gets e·(2r−1) units added per component (r from 0x4a4580 with v = (1,1,1), e = 1), then 0x49e6d0 is called with error 0: no accuracy memory [disasm] |
| ConsidersStrikeThrustDirection | 0 | 0 | 1 | 1 | 1 | 0x4a13a0: chooses the strike direction |
| WeightStrikeSecondaryTarget | 0 | 0.3 | 5 | 1 | 1 | 0x4a19ea: multiplies the value of other worms hit by a strike |
| WeightingWormVital | 1 | 1.5 | 3 | 5 | 1000 | 0x4a92c7: base value of a target with worm flag 0x100 (+0xec), otherwise 1 |
| WeightingWormExchange | 0.05 | 0.1 | 0.1 | 0.5 | 0.5 | 0x4a9b20: K = (nEnemy/nAlly)^x. Enemy value /K, ally value −K× |
| WeightingExplosiveSecondaryDamage | 0 | 0.5 | 0.5 | 0.8 | 1 | explosion score for barrels and chain reactions (0x49fe90, 0x4a0540, ...) |
| WeightingExplosiveNearbyThreat | 0 | 1 | 1 | 2 | 4 | likewise, for knocking a worm into a threat (water, mine) |
| WeightingPreferNearbyTargets | 1.5 | 1 | 0.5 | 0.1 | 0 | 0x4a93bb: value ×(200/max(d,200))^x |
| WeightingPlanScoreRandomise | 0.2 | 0.2 | 0.1 | 0.2 | 0 | 0x498a0a: plan score ×(1+x·(2r−1)) |
| WeightingPreferAttackHumans | 0.8 | 1 | 1 | 1.2 | 1.8 | 0x4a956b: positive values of human-controlled targets ×x |
| ProjectileSweetSpotDistance | 0 | 5 | 5 | 10 | 5 | 0x4a9be0: projectile aim point = target + d·x when the target's threat rating (+0x1c) > 0; d (+0x20) is the unit vector away from its worst threat (0x4a9e90), so the blast pushes it toward water, a drop or mines [disasm] |
| DelayBeforeFire | 0.5 | 0.5 | 0.5 | 0.5 | 0.2 | s, after aiming and before fire (0x49eb8b) |
| DelayBeforeNonFirstMove | 2 | 0 | 0 | 0 | 0 | s, before a move after a rethink (0x4983ef) |
| AddScoreCollectSomething | 1000 | 5000 | 20000 | 30000 | 60000 | crate plan base score (0x4a72e1, 0x4a7d38) |
| LikeToCollectHealthWhenHealthBelow | 25 | 25 | 25 | 25 | 50 | hp threshold for "Score increased due to worm having low health" |
| ReduceMoveScoreFurtherThan | 100 | 200 | 100 | 100 | 100 | 0x4a75c0: move score ×R/d when d > R |
| ReduceMoveScoreIfTimeLeftLessThan | 40 | 30 | 30 | 20 | 20 | move score ×t/T when turn time left t < T |
| MemoryImproveAccuracyEffect | 0.2 | 1 | 1 | 1.5 | 1 | 0x4a5d00: error /(1+x·Σmatch) for a repeat of a previous shot (MatchRadius 200) |
| MovementJumpForwardAllowed | 0 | 1 | 1 | 1 | 1 | pathing (0x492210) |
| MovementJumpBackflipAllowed | 0 | 0 | 0 | 1 | 1 | pathing (0x492210) |
| MovementJumpError | 0 | 0.2 | 0.1 | 0.05 | 0 | 0x496c90: error on path jumps ("worm's jump error =") |
| JetpackAboutToCrossLineLookAhead | 1 | 2 | 3 | 4 | 5 | read in worm code 0x5a8780: jetpack look-ahead |
| MortarMaximumAimAngleAllowed | 0.5 | 0.5 | 1 | 1.6 | 1.6 | **no read found** |
| PrefClusterGrenade | 1 | 1 | 1 | 0.8 | 0.3 | |
| PrefGasCanister | 1 | 1 | 1 | 0.5 | 0.5 | |
| PrefHomingMissile | 0.9 | 0.9 | 0.9 | 0.8 | 0.7 | |
| PrefProd | 0.3 | 0.3 | 0.3 | 0.3 | 0.3 | (1.0 in CPUTest) |

Values shared by all 5 levels:

- Target value: ThisWormValue 1 (read per *target* worm, so scripts can weight a target), WormHealth 0.04, WormPoisoned 0.08, LastInTeam 2, WormNearbyWorms 0.02, WormMilesAway 0.1, WeightingAttack 2, WeightingKillTarget 200, WeightingPunchThroughLand 10.
- Multiple use and variety: MultipleUse 1.5, BestMissShotMultipleUse 1, PreferVariety 0.4.
- Delays: DelayAtStart 0, DelayBeforeFirstMove 0.
- Distances: StrikeSweetSpotDistance 2 (0x4a1423), ClusterDistanceAboveTarget 10, MaximumDistanceTargetConsidered 1e5.
- Movement: AddScoreMoveIfNotMoved 20, AddScoreMove 10, RandomSmallMoveRange 100, ForbidMoveIfWouldLeaveTimeLessThan 10 (s).
- Crates: WeightCollect{Weapon, Health, Utility} 1, WeightCollectHealthWhenPoisoned 3, AllowCollectNormalCrate 1, AttractorZoneRadius 50, MemoryImproveAccuracyMatchRadius 200.
- Fields with no read found [disasm, by scanning every load of 0x9560f4]: AddScoreTeleport 1, WeightTeleport{Defensive, Offensive}Pos 1, WeightRetreatDefensivePos 1, WeightRetreatOffensivePos 0, MortarMaximumAimAngleAllowed.
- CPUTest (= Worm*) differs from the levels: KillTarget 4, DelayAtStart 3, DelayBeforeFire 1, DelayBeforeFirstMove 2, AddScoreMoveIfNotMoved 10000. It is a scripted/debug profile, and the worm slots are overwritten from `CPU<level>` at load.

### Turn flow and timings [disasm 0x49e6d0, 0x4983a0]

1. The think runs over several frames. Its frame count is stored in 0x9560b4.
2. If the plan has a move before firing: Delay(DelayBeforeFirstMove, or NonFirstMove after a rethink, minus the think time), then Path, then Delay 0.5 s.
   Otherwise, on the turn's first action: Delay(DelayAtStart − think time).
3. SetWeapon, then optional fuse/target/launch velocity/orientation/aim angle, then Delay(**DelayBeforeFire**), then FireWeapon. Strafe weapons add StrafeTowards (StrafeProgressiveErrorScale). Some weapons add DetonateWhenGoingAwayFrom.
4. Delay 1.0 s, camera "Default", then the retreat Path. There is no retreat if `AI.WeaponsDontEndTurn` is set, or if the move should not end the turn.
5. If no good plan is found, the AI skips the turn. It never skips when locked to a multi-shot weapon: in that case it fires again. Plans with a negative score are forbidden.

### Shot evaluation [disasm]

- **Launch is exact velocity.** The plan stores a launch vector and SetLaunchVelocity writes `AI.LaunchVelocity`. The AI does not charge the power bar.
- **Ballistic solver** 0x4ace50 (AIPlanUtilities). The speed range is [BasePower, BasePower+MaxPower] of the payload's `PayloadWeaponPropertiesContainer` (+0x110/+0x114, loaded by 0x4acbc0).
  - Acceleration = gravity (if `IsAffectedByGravity`: `Gravity`, or `Gravity.Slow` if `IsLowGravity`, times `Low.Gravity.Multiplier`) + full wind (if `IsAffectedByWind`: `Wind.Direction`/`Wind.Speed` → (sin, 0, cos)·speed, 0x4ac6f0).
  - **About 11 speeds** are sampled between two fractions of the range (step = range/10). For each one, `TargetParabola` 0x519a40 solves the exact flight time for the arc to the target (two roots, `fTimeSquared1/2`).
  - Each arc is checked against the land as 2 segments (0x4aca20). The error is the distance from the collision point to the target. The lowest squared error wins.
- **Bouncing payloads**: BounceToRest 0x4ad770. It iteratively rescales the launch speed (fScaleSpeed, fGuessStep), simulates the bounces (0x4ad580, `Bounce.MinSpeed`, water = `Water.Level` − 100) and keeps the better rest-point error.
- **Wind is never degraded by difficulty.** The only inaccuracy is the post-hoc ShotError.
- **"Best miss" plans**: when the target cannot be hit, the AI spawns a plan that aims at the collision point to dig through land ("Shot will punch through land in N shots", WeightingPunchThroughLand, BestMissShotMultipleUse). This is not done on indestructible land.
- **Damage score** 0x49ed30:
  - Damage passes through armour (ArmourLogicEntity 0x548fc0), except for weapon ids 10–12, which are melee.
  - Score = WeightingAttack × target value × d. Here d = damage, or max(hp, WeightingKillTarget) if the hit is lethal ("should kill with damage alone").
  - A non-lethal hit with knock weight a ≠ 0 (and plan +0x3c, the best-miss flag, clear): t = the target's threat rating (+0x1c), d = min(hp, d + a·t·(hp − d)) and WeightingAttack ×(1 + a·t) ("may knock X into nearby threat") [disasm 0x49efb5].
  - Score = d × WeightingAttack' × target value (+8) [disasm 0x49f157].
  - Callers and their a [disasm]: blast scorer 0x49f190 (radius, Secondary, a, magnitude): per target within the radius, d = (1 − dist/radius)·WeightingExplosiveSecondaryDamage·magnitude (so CPU1, Secondary 0, scores no blast damage). Projectile 0x4a0540: WormDamageRadius, WormDamageMagnitude (×2 DoubleDamage, 0x49b610), a = NearbyThreat. Direct 0x4a0a20: gun WormDamageRadius, WormDamageMagnitude ×(1 if bCanMoveBetweenShots else NumberOfBullets), a = 0. Strike 0x4a11d0: radius Bomber.BlitzDuration 2000 × GroundSpeed 0.15 + 2·WormDamageRadius, a = NearbyThreat. Close range 0x4a2c70 and animal 0x4a4210: payload radius and magnitude, a = NearbyThreat. Melee 0x4a2b10: the plan target only, 0x49ed30(target, NearbyThreat, magnitude). Flood: a = 0, 1000. Starburst rider: a = 1, its hp. Every target counts, the active worm included (0x90ea78). Targets are worms only: no barrel or crate chain is scored.
- **Target value** 0x4a9260:
  - v = (Vital if flagged, else 1) × (1 + 0.04·hp), then v += max(0.1, 1 − 0.08·poison).
  - Enemy: v/K. Ally, including the thinking worm's own team: −v·K, where K is the WormExchange ratio. This is the friendly-fire and self-damage penalty.
  - Then: ×LastInTeam when that side has 1 worm left, ×nearby factor, ×the target's own ThisWormValue, ×PreferAttackHumans when the target is human.
  - Targets inside an "AI affecting trigger" are removed (ForbidShotsWhenMightAffectAITrigger).
- **Weapon choice** 0x49c060: plan score ×Pref(weapon) (GetWeaponPref 0x66364d switches on weapon enum 1–29 and 34–42), then ×max(0, 1 − PreferVariety·match with recent plans) (memory 0x4a5720).
  - Weapon availability comes from the inventory plus `Inventory%d.WeaponDelays` (PopulateWeaponsAvailibleArray) [data].
  - Prefs for BridgeKit, LowGravity, Teleport and Pipe are not in the switch, so no plan uses them.
- **Memory** (AIPlanMemory.cpp):
  - Repeat shots get more accurate (MemoryImproveAccuracy*).
  - Plans that match a failed plan are scored down.
  - The skip-turn score is ×1/(1+5·Σ previous skips) (0x4a57c0).
  - Detail [disasm]: think start 0x49af70 runs CheckPlanResult 0x4a6ab0 (the last attack, c_pMostRecentPlanMemory 0x956114 {effect, weapon, worm 0x90ea60, target, its pos +0x38, its hp +0x11e}, is a failure if the target kept hp and pos), RegressFailedMemory 0x4a5b10 (×0.99, 0 once the target changed), RegressImproveAccuracyMemory 0x4a6080 (×0.95), RegressSkippedTurnMemory 0x4a61b0 (×0.9); records under 0.1 are deleted. A plan of the same worm at a failed target: score ×(1 − 0.5·f), f = effect, ×0.2 with another weapon (0x4a6590). Accuracy match (0x4a5640): effect·(1 − d_shooter/R)·(1 − d_target/R), both < MatchRadius R, all records. Skip records (0x4a68c0) are stored, and the 1/(1+5Σ) scale applied (0x4989d0), only in worm-select mode (0x9560c1, set at 0x4a4f6b), which needs data ChooseWorm.Enabled ≠ 0 (read 0x4a4ef7): LOCAL.XOM sets 0 and nothing writes it, so the CPU never selects worms and never uses Change Worm [data, disasm]. Players: Change Worm's WormSelectLogicEntity 0x59a5f0 spends one on the first cycle (+0x44, DecrementInventory) and cycles free afterwards; a move, jump or accept ends it.
  - Strafe [disasm]: ShotErrorDirect is read only by CAIPlanAttackDirectActionable slot 11 (0x4a0d70); Shotgun and Sniper override it with ShotErrorDirectNonStrafe (0x4a0d90). StrafeTowards needs plan flag 0x200 (0x49ebed), set by no plan constructor: both strafe fields are dead.
  - Flood [disasm 0x4a3640]: each target below Water.Level + Flood.Delta scores 0x49ed30(target, 0, 1000). Starburst [0x4a43a0]: adds 0x49ed30(active worm, its hp) to the animal score. Close-range explosive (Dynamite, Landmine, CheapDynamite*) [0x4a2c70]: the weapon's blast at a point next to the target the worm walks to (0x4a22d0 sets +0x14, then pathfinds), +AddScoreMove forced (0x49bd60(1)), ×Pref, ×PrefWeaponMelee, ×0.05 for a best-miss. Animal [0x4a4210]: the blast at the target + a land term, ×Pref, ×PrefWeaponAnimal, ×plan+0x58.
  - Node grid [disasm 0x4b2800]: total area = Σ (max.x − min.x)(max.z − min.z) of the NodeGrid boxes; spacing = sqrt(area / 16000).
  - Node grids are merged where they overlap (0x4ae320, "After second merge pass have N node grids"). Repath [disasm 0x490551]: a failed path action adds a path-failed blockage at its node and pathfinds again; past 2 repaths (+0x28 > 2) 0x9560f8 is set ("forbidding further movement"). Move → retreat pairs (0x4a8c60) come from the same 21 × 21 × 2 window. Active objects (ObjectCount.Active): the 32 callers of ActiveObjectRegistrationService 0x4d3af0, listed in docs/sim.md "Settle".

### Movement [data + disasm]

- **Path A\*** (AIPathManager) [disasm]: 2-D node grid (x, z int16 + a layer byte), 8 walk neighbours (a diagonal only if both sides are walkable, 0x492510), G cost 10 orthogonal / 14 diagonal, heuristic octile 10·max + 4·min (0x4923a9, 0x491fd8). Jump edges per allowed type (+0xc04c from MovementJumpForward/BackflipAllowed, 0x4924d7) over a precomputed reach table, cost +40 forward jump, +60 backflip (0x492008, 0x492003). At most 200 iterations per pathfind (0x4b0ba6), 100 per step call (0x492e4e); partial paths are accepted unless too short.
- **Node grid** [disasm]: boxes come from AddLandBlock 0x4b22e0, called per land frame (0x46f6c9; box = 8 corners of x [min EdgeOffset1.x, XSize + max EdgeOffset2.x], y [min HeightMap, YSize + max HeightMap], z likewise, less size/2, world AABB 0x46b550) and once for the heightmap (0x464711: cells with height > 0, first to last cell origin over ±1500 units, 100 × 100). A box under 250 units² is dropped (0x4b24c5); a box merges into the first one whose x and z gaps are under 40 units (0x4ae3aa), and a second pass restarts after each merge (0x4b2800). One lattice anchored at all boxes' min corner, node i = trunc(offset/spacing + 0.5) (0x4aeb70); a node exists only in a box (0x4ae9d0). Node heights: 3×3 rays at ±spacing/3 (0x4ade10), lo = lowest, hi = highest hit; any miss or water → water node; hi − lo > 20 units → blocked (0x4aef54); layer 0 from the box top, layer 1 below it (0x4aed70).
- **Walk edges** 0x492510: to a free neighbour when hi' − lo ≤ 20 (step up, 0x4ade00) and hi − lo' ≤ 0.045/|Gravity| = 180 units (scene +0x9c); a diagonal only when the walk edges to both its sides were added from this node (0x4926f1).
- **Jump reach table** 0x4af3f0 (per type, n = trunc(T·(0.1 + h)/spacing)): Worm.Jump.Forward (h 0.07, vy 0.15) / Backflip (0.04, 0.2) flown in 1 ms steps under Gravity until vy ≤ −0.3, twice (speed + or − 0.8·AftertouchDelta per ms, + capped at AftertouchStrength 0.1); a cell crossed records the arc height clamped to [lowest, 2·distance] as hi/lo, valid when crossed going down; cells past the back arc's end get lo = lowest height. Jump edges 0x4928e1 only from layer-0 nodes, to cells with valid entries where d1 = hi' − lo and d2 = lo' − hi are strictly inside (lo, hi); stepping back from the landing (0x492c0d), each crossed cell's layer-0 hi − lo(start) must stay under (1 − f)·hi + f·lo, f = (hi − (d1 + d2)/2)/(hi − lo).
- **Path end** [disasm]: A* has one goal node (0x492e19); exhausted, the partial path ends at the closed node of least h (0x490ed0); any path ending under 50 units from the start is dropped (0x494a42).
- **Node spacing**: sqrt(land area / 16000) (0x4b2a68). **Move nodes**: a 21 × 21 window × 2 layers around the worm, scored by 0x4aa6a0 two columns per think step ("ScoreAllMoveNodes slice", 0x4ab490).
- **Think budget** 0x49b210: each frame the cost counter 0x9560b0 drops by 80 and think steps run while it is under 80 (a counter above 80 after the drop is reset to 160). Costs: pathfind 100 (0x494365), attack plan 100 (0x49b4b9, 0x49e01b), position score 10 (0x4aa6a9).
- **Target threat** 0x4a9e90: 8 directions × probes at 50/100/150/200 units (0x90ebf8), weight 2/(k+2); water or no land 0.2, a drop over 30 units 0.05, mines 0.1·(100−d)/100, oil drums 0.05 (GameLogicService m_OilDrumIds 0x4f5690/0x4f56a0), other targets 0.05, each ×(1 − d/100) within 100 units; total capped at 1 (+0x1c), worst direction negated into +0x20.
- **MovementJumpError** 0x496f24: for each jump move of the path (types 3..5) the displacement to its landing is scaled per component by 1 + e·(2r−1) (0x4a4580) before the move is queued.

- Movement plans (`CAIPlanMove*`) score: crate collection (AddScoreCollectSomething × type weight; health ×3 when poisoned or below the hp threshold; `Imaginary`/`Attractor` detail objects come from scripts), moving closer to a target, and a random small move (range 100).
- An attack with a move gets +AddScoreMove, plus +AddScoreMoveIfNotMoved if the worm has not moved yet (0x49bd60).
- Moves are refused when they would leave less than 10 s of turn time ("Not enough time left to follow path").
- Paths are A* over the node grid and contain walk, jump forward and backflip (gated per level) only; super sheep and melee build their own moves. No jetpack, parachute, rope, teleport or girder move is ever created.
- After firing, the AI retreats along a path: move→retreat node pairs ("Move node found: move to ..., retreat to ...").

### Actionable for client/src/ai.cpp

1. **5 levels, not 3.** `levelOf` clamps to 3. Map the front end to CPU1..CPU5 and drive the behaviour from the AITWK values above (one small table) instead of the `level == 1/2/3` branches.
2. **Aim error model.** W4M scales each launch-velocity component by 1±e (e = 0.3/0.2/0.1/0.05/0) and also uses it for direct weapons. Today level 1 is ±0.05 rad / ±5 % and level 3 is 0. W4M CPU1 is much sloppier, while CPU5 is perfect.
3. **Wind**: W4M uses the exact wind at every level. Drop the "level 1 half-guesses the wind" hack and let ShotError carry the inaccuracy.
4. **Repeat-shot accuracy memory**: error /(1+Effect·matches) when shooting again from about the same spot at the same target. This is missing today.
5. **Scoring weights.** Kill bonus = max(hp, 200)×2 (WeightingAttack) vs today's +30. Team losses: negative value × exchange ratio (nEnemy/nAlly)^0.05..0.5 vs today's fixed ×2. ×2 when the target is the last of its team. Prefer human targets (0.8..1.8).
6. **Randomness and variety**: multiplicative plan noise ±20 % (CPU3 ±10 %, CPU5 0) and score ×(1 − 0.4·recentMatch). Today the taste is additive (±12/10/5) and there is −8 for the last weapon.
7. **Per-weapon prefs**: Prod 0.3, Homing 0.9→0.7, Cluster 0.8/0.3 and Gas 0.5 at CPU4/5. No AI use of teleport, girder, bridge, low gravity, sentry, bubble, NoMoreNails, abduction or pipe. Today level 3 teleports and evaluates Sentry and Abduction.
8. **Timings**: DelayBeforeFire 0.5 s (CPU5 0.2 s), a 0.5 s pause after a pre-fire move, and 1.0 s after firing before the retreat. Today there is one fixed 20-tick pause.
9. **Crates dominate** at higher levels (base 1000→60000). Health crates count ×3 when poisoned or below 25 hp (50 at CPU5). Move score falls off beyond 100 units and when the turn time left is under 20–40 s. Moves are forbidden under 10 s.
10. **Jumps in paths**: no forward jump at CPU1, backflip only at CPU4+, plus the jump error. Today the AI jumps whenever it is stuck, at every level.

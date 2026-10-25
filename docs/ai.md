# CPU player (ours)

What `client/src/ai.h` / `ai.cpp` (`Ai`) do, with the W4M source of each rule. The W4M side (AIService, AITWK, AIPathManager) is in
`docs/w4m/ai.md` §18 "AI (CPU worms)"; this file is about our code. Status per fact, as in sim.md:

- **data**: a value read from the W4M files (AITWK.XOM AIParams.CPU1..CPU5, RTTI/strings);
- **disasm**: a rule read from `WormsMayhem.exe` (VA given);
- **assumed**: inferred, not verified in W4M;
- **ours**: no W4M source, our own choice.

Distances: W4M units / 20 = our metres (200 units = 10 m). Times: 60 Hz ticks, `s / DT`.

## Levels (`Level`, `LEVELS[5]`, `levelOf`)

- `levelOf(g)`: the thinking worm's `Game::wormCpu` (W4M AIParams.WormNN, per worm; set by mission scripts), else
  `GameConfig::teamSetup[team].cpu`; 1..5 picks `LEVELS[cpu-1]` = AIParams.CPU1..CPU5 (**data**; W4M copies `CPU<level>` into each
  worm slot of the team, AIService 0x4b3820, **disasm**). Values above 5 clamp to CPU5.
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
- Not ported: JetpackAboutToCrossLineLookAhead, AttractorZoneRadius (no script attractors). BestMissShotMultipleUse is read only by the
  direct plan's best-miss branch (0x4a0a20), and direct plans make no best miss (vtable +0x24 0): dead. (MortarMaximumAimAngleAllowed has no W4M read either.)
- Shared by every level (**data**): AddScoreMove 10, AddScoreMoveIfNotMoved 20, RandomSmallMoveRange 100 units (`SMALL_MOVE` 5 m),
  StrikeSweetSpotDistance 2 units (`STRIKE_SWEET` 0.1 m). Per level: ConsidersStrikeThrustDirection (`thrust`, CPU3+) and
  WeightStrikeSecondaryTarget (`others`: 0 / 0.3 / 5 / 1 / 1).

## Aim error and wind (`finish`)

- Applied once the plan is chosen, before aiming. Seed `r = (salt ^ shotsLeft·2654435761) + walks·40503`, LCG `r·1664525 + 1013904223`,
  `noise()` in [-1, 1) (**ours**; W4M random 0x4a4580, **disasm**). `salt` is turn-start state, so the error never depends on think length.
- Shells / homing: the launch vector `dir(yaw, pitch) × charge/90` has each component scaled by `1 + e·noise()`, e = shotErr; yaw, pitch
  (clamped -1.2..1.45) and charge (1..90 ticks) are re-derived. W4M 0x4a06c0 scales the exact launch velocity (**disasm**); ours scales the
  charge vector, since the AI charges like a player (see Timings).
- Shotgun / Sniper Rifle (`Kind::Shotgun`): same scaling with directErr = ShotErrorDirectNonStrafe (W4M 0x4a0d90, **disasm**). No strafe mode.
- Airstrike / donkeys: the target gets strikeErr·noise added on x and z (`strikeOff`; y drawn, unused: bombs fall straight), no accuracy memory; the Blimp cursor and the steered bomber aim at the offset target. W4M 0x4a1b00 (**disasm**).
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
- Worm-select mode: not used; W4M enables it only with ChooseWorm.Enabled, which is never set (**data**, 0x4a4ef7).
- Scoring (`Outcome`, W4M 0x49f190 / 0x49ed30, **disasm**): a blast gives each worm, self included, d = (1 − dist/reach)·secondary·damage
  (×2 doubled); a melee hits only the plan target with its damage; Flood 1000 below the level; Starburst its rider's hp with knock 1 (disasm: rider vapourized; user unsure, 2026-10-04).
  Armour ×ARMOUR/100 except bat, prod, fire punch. Lethal (d ≥ hp): max(hp, 200). Else, with knock weight a (threat for blasts and
  melee, 0 for guns and Flood): t = the worm's threat rating, d = min(hp, d + a·t·(hp − d)), weight 2·(1 + a·t). Score += d·weight·value(i).
  Guns (Shotgun, Sniper Rifle: one `Kind::Shotgun` path): the blast `Game::gunBlast` (Wormpot super firearms) is scored at the plan's
  target point, the target's feet + Worm.EyeLevelOffset / 2 (W4M 0x49f190 measures from plan +0x14, **disasm**), whoever the ray strikes;
  the land sweep to that point is IsPossible (0x4ac880); `Game::gunRay` only gates that the sim's shot strikes within range (**ours**). Strikes: one blast at the target of reach 2 s × 7.5 m/s + 2·reach.
  Animals (sheep, old woman, scouser): the walker's closest approach within 2 m, blast there. No knockback simulation, chains,
  poison, vampire or tie-breaker terms (W4M has none).
- Bonus (W4M 0x49bd60, **disasm**): +AddScoreMove 10, +AddScoreMoveIfNotMoved 20 while no path ran since the last shot (`moved`, W4M
  0x9560ba), for a plan from a move node (`Origin::path`) and always for a close-range explosive (0x4a2c70 forces it).
- The CPU never steps a body: a path is followed by playing real inputs from the thinker's `Mover` snapshot (`stepInput`), and the sim alone applies the landing, 0x40 flag and fall damage; W4M's AI predicts none (**disasm**, docs/w4m/ai.md "Movement is not simulated").
- A dropped shell or landmine plan needs its target inside the blast set down at the feet (**ours**: W4M's blast is next to the target, 0x4a22d0, so it always scores damage; at the feet a blast reaching nobody would win on the forced +30 bonus of 0x4a2c70 alone).
- Dropped shells and landmines spare the thrower (**ours**: W4M walks next to the target first, 0x4a22d0); the thrower then takes the
  retreat W4M 0x4a8c60 would pair with its own node: the best def within ±2 window entries, 50 iterations (**ours**: W4M retreats along
  its approach, 0x4a22d0).

Target value `targetValue()`, once per think from where the worm stands (W4M 0x4ab730 calls 0x4a9260 once, **disasm**), in `vals`:

- v = Vital × (1 + 0.04·hp) + max(0.1, 1 − 0.08·poison), Vital = `Level::vital` (WeightingWormVital: 1, 1.5, 3, 5, 1000) for a worm with W4M flag 0x100 (`Game::vital`:
  Vital Worm, each team's first worm) and 1 otherwise (0x4a92c7, **disasm**). WormHealth 0.04, WormPoisoned 0.08, LastInTeam 2 (**data**).
- K = (foes / friends)^exchange, 1 without a foe or without a friend, and then both counts are 1, so every target is "the last of its side" (0x4a9b20, **disasm**).
  Ally (self included): −v·K. Enemy: v/K. Then for both: ×2 when its side's count is 1, ×(10 / max(d, 10))^nearby, ×the target's ThisWormValue
  (`Game::wormTweak`, the target's AIParams.WormNN, 1 unless a script writes it), ×humans (PreferAttackHumans) when v > 0 and the team is human (**disasm**).
  Allies are the worms whose team shares the thinking worm's AlliedGroup (0x4a4790, `Game::alliance`: the match slots'
  alliances, `GameConfig::Team::alliance`, or the mission script's); the targets and the enemy tests of the plan and of the sheep's detonation use the same rule (**disasm**).
  The targets are the alive worms of the teams that have not surrendered (`targeted()`, 0x4a46f0, **disasm**), counted in `foes`, `friends` and `startEval`.
- The list (`listed`, W4M c_plstTargets) keeps a target within MaximumDistanceTargetConsidered of the thinker and, with the thinker's
  ForbidShotsWhenMightAffectAITrigger == 1, outside every AffectsAI trigger (`considered()`, 0x4ab730 / 0x4a9260, **disasm**); both come from the thinking worm's
  AIParams.WormNN (`Game::wormTweak`, `Game::Trigger::affectsAI`, `syncCpu` / `createTrigger`). The counts above are taken before this filter, as W4M does (**disasm**).
  Only the install's `*-w3d.lub` scripts (applecore, cropcircle, funfair, helterskelter, highstakes...) set the flag, none of the imported missions or challenges (**data**).
- Threat rating (`threatOf()`, 0x4a9e90 / 0x4ab070, **disasm**): every list entry, scripted markers included, from the probe's ground height; the other targets in it count only with a positive value, weighted value / `threatNorm`, the previous think's mean |value| (0 before the first, as W4M's uninitialised 0x959868).
- Second pass (`secondPass()`, 0x4a9c50, **disasm**): each target moves by −(1 − d/5 m)·v_j·0.02 for every target j of the other sign within 5 m, and gains 10% of
  its value when no other-side target is within 50 m; `norm` is the mean |value| after it.
- Scripted targets (`scripted()`, 0x4a5070): the markers named by the thinking worm's AIParams `TargetDetailObject` (`Game::wormTarget`, set by `syncCpu`) are targets after the worms: value 1,
  hp 1, threat 0, in the plans' `tpos`, the blast and strike scorers, the move-closer plans and the failed-plan memory (**disasm**); they skip the list filter. Only TinCanWally writes it (**data**).
- WormData CPUActionRadius and CPUFixedWeapon are read by nothing in W4M (docs/w4m/ai.md), so no code here reads them.

Plan rank (W4M 0x498a0a, 0x49c060, **disasm**):

- `taste = (1 + randomise·n) × pref × (0.6 if weapon == g.picked[team])`, n from `salt ^ wi·2654435761`, one per weapon per turn.
- `rank = score × taste` for positive scores, else `score`. 0.6 = 1 − PreferVariety 0.4 (**data**); W4M matches recent plans, ours only
  last turn's weapon.
- Failed-plan memory: see above (`rankOf`, shared by every attack plan).

## Shot candidates (`evalWeapon`)

One unit = one (weapon, target, candidate). Targets (`targetsFor`): each listed enemy and scripted target (W4M 0x49f8a0); each family
sets its plan point from it (W4M +0x14, `feetOf`: a worm's feet).

| kind | candidates | tag |
|---|---|---|
| Shell, Homing, Super Sheep (projectile plans) | one plan per target (`project`): scored at its point now (`projScore`), solved when the list pops it (`solve`), below | W4M 0x49ffa0, 0x4a0540, IsPossible 0x49c340..0x49c400 (**decompiled**) |
| Dropped shell | set down at the feet facing the target | W4M CheapDynamite* (**data**) |
| Homing | a projectile plan; locked on its point from the Blimp (`blimp()`) | W4M flags 0x1d7 (SetWeaponTarget, lock FireWeapon) (**disasm**) |
| Shotgun / Sniper | one blast scored at the plan's target point (feet + EyeLevelOffset / 2), whoever the ray strikes (`Game::gunRay` only gates a strike within range, ours); possible only when the land segment muzzle -> target point is clear over its first 98.9 % (`Terrain::raycast`, W4M IsPossible 0x4ac880, **disasm**); a full think for each shot, locked to the weapon (`act`: `wait` 1100 ms, then `Mode::Eval`; docs/w4m/ai.md §Turn flow item 6) | W4M Direct 0x4a0a20 (**data**, **disasm**): 0x49f190 measures from plan +0x14 |
| Melee (incl. Tail Nail) | within 3.5 m; yaw ±0.8/±0.4/0 × pitch 0/0.5/1 (15), `meleeHits` | **ours** |
| Sheep / Old Woman | `sheepWalk` copy; kept if closest approach < 2 m. Sheep: FIRE in flight once its next step moves away from the target (x, z) | detonation: W4M DetonateWhenGoingAwayFrom 0x57e130, queued for the Sheep only (flag 0x800, 0x49d848): the CPU never detonates the Old Woman or Scouser (**disasm**); candidates **ours** |
| Super Sheep | a projectile plan (0x49c400), launched level toward its point, flown by the `steer` autopilot, detonated within 1.5 m | plan **decompiled**; flight **ours** |
| Airstrike / Donkey | one blast at the target worm, no worm aim. Heading: `strikeHeading`, W4M 0x4a13a0 (below); `blimp()` turns the Blimp view to it (W4M Airstrike.Direction = the view's forward), drives the cursor until its ray meets the target's feet (+ strikeErr), then FIRE; bomber (Super Airstrike, fuse > 0) steered in flight and dropped with lead | W4M 0x4a11d0 one blast, SetStrikeTarget 0x4b4c70 with the Blimp camera, SetStrikeDirection 0x4b4bc0 (**disasm**); cursor driving and bomber steering **ours** |
| Starburst | as Super Sheep (`superFly`), with the W4M rocket: still 3.5 s, then `Game::starSpeed` (ours: the shared sim rule), plus its rider's death: all its hp on the thinking worm; the star rockets are not scored | W4M CAIPlanAttackStarburst 0x4a43a0: 0x49ed30 on the active worm with its hp (**disasm**; user unsure, 2026-10-04) |
| Landmine | laid at the feet facing the target: `MINE_BLAST` on the ground under the launch point, self ignored as a dropped shell | W4M CAIPlanAttackLandmine = CloseRangeExplosive 0x4a2c70: the blast at the worm (**disasm**); the drop point and self **ours** |
| Inflatable Scouser | our scouser's walk (`walkerStep`) until it touches a worm; that worm takes `damage`, or a kill if a drop of 1.2 m/s × `SCOUSER_FLOAT` drowns it, plus the fall damage | W4M CAIPlanAttackScouser exists (**data**); W4M scores an animal as its blast at the target (0x4a4210); ours scores our scouser, which swallows and drops (**ours**) |
| Flood | one plan: every worm under `water + speed` (2.15 m, W4M Flood.Delta 43) counts a kill | W4M CAIPlanAttackFlood 0x4a3640: each target under Water.Level + Flood.Delta, 0x49ed30 with damage 1000 (**disasm**) |

Projectile plans (`project`, `solve`, W4M **decompiled**, docs/w4m/ai.md "Shot evaluation"):

- Point: Gas Canister and Super Sheep at the target's feet, Cluster Grenade 0.5 m (ClusterDistanceAboveTarget) over them, the others at the
  sweet spot when the target's threat rating is positive. Weapon Factory weapons have no plan class: the CPU never fires them.
- Score: AddScoreMove with a path, the blast at the point (knock NearbyThreat), + `punch` (WeightingPunchThroughLand 10 x max(0.5,
  LandDamageRadius / d), d from the point to the target, also added to the strike, animal and Starburst scores), then `rankOf`.
- Solve in W4M units and ms on `Para` (W4M's TargetParabola, kept across thinks like its globals): from the shooter's eye (`Origin::pos`, the
  move node with a path), the payload's gravity and whole wind, BasePower .. BasePower + MaxPower. Bazooka, Poison Arrow, Homing: `arcSolve`
  (10 speeds, the lower arc first; a land `sweep` before t - 10 ms refuses an arc; the Homing Missile has no acceleration, so both arcs are
  (s, 0, 0), along world +x, with the times of the previous solve); Grenade, Cluster, Banana (+0.5 m): `timedSolve` (1 then 2 s, the
  fuse set to it through FUSE_UP / FUSE_DOWN in `act`); Gas Canister, Holy Hand Grenade: `bounceSolve` (BounceToRest, `restOf`: payload
  bounces with `payBounce`, each from the parabola's own velocity as W4M does); Super Sheep: from the feet, Gravity, 0 .. 0.5 units/ms,
  impossible within 10 m. The launch velocity becomes yaw, pitch and charge ticks (`finish` adds the ShotError).
- Best miss (`solve`, W4M 0x49be00): an impossible plan that is no clone, whose collision point is 2.5 m or more from the shooter's feet,
  on destructible land (`landLocked`), spawns its clone (`Plan::miss`) at that point (the Homing Missile keeps its own), scored without
  knock and inserted in the list (`insert`); a strike whose last heading check failed (`strikeHeading`) spawns, when popped, a clone at the
  same target whose heading search skips the check. Not reproduced: a capped timed arc that is clear reads W4M's last land ray of any entity
  (`timedSolve` leaves it out) and the melee plans' clone point is that same last land ray (ours plans melee within 3.5 m, no path).
- Order: `decide` pops `choices` (the best inline plan, the projectile plans, the spawns, the move plans) best first, one solve per unit.

Strike heading (`strikeHeading`, W4M 0x4a13a0, **disasm**): headings 0, 10 .. 350°, D = (cos a, 0, −sin a); 6 bombs 2.5 m apart along D
through the target; each worm within 15 m + 2 reach scores value × Σ falloff at its aim point (+ 0.1 m from its worst threat),
+ ½ × the push along D under `thrust` (falloff in (0, 0.8), not the last bomb); a positive sum counts, others' × `others`. A better
heading is checked: for Airstrike, Super Airstrike and Fatkins (WEAPTWK IsBomberWeapon) the bomb aimed at the target's feet, dropped
along D from `landTop` + 7 m at 7.5 m/s, must land within 0.5 m (`bombLands`; ours: the sim's gravity, land only); refused: +30°.
The plan is impossible when the last checked heading was refused. The model lays the bombs along D while the bomber flies cross(D, up),
as W4M does.

Sweet spot (`awayFromThreat`, W4M 0x4a9be0 / 0x4a9e90, **disasm**): 8 directions × probes at 2.5/5/7.5/10 m, weight 2/(k+2); no land or
water 0.2, drop > 1.5 m 0.05, mines 0.1·(1 − d/5), other worms 0.05·(1 − d/5). Aim point = enemy + sweet × away-from-worst. Differs:
no cap at 1, worms get a falloff, the GameLogicService object term is not ported.

## Weapons the AI never uses

- Sentry Gun, Alien Abduction, Bubble Trouble, Girder, Teleport and the other utilities (Armour, Icarus, Double Damage...): no W4M plan (**data**).
- Ninja Rope: never. W4M never creates NINJA_ROPE, JETPACK or PARACHUTE path moves (**disasm**, see docs/w4m/ai.md §18): no jetpack move either.
- Change Worm: never (W4M worm-select mode needs ChooseWorm.Enabled, 0 in LOCAL.XOM, **data**).
- Parachute: no use (W4M has PARACHUTE path moves, **data**). Jetpack: only as a move (below). Fuse is never changed (`fuseOf`, W4M SetWeaponFuse exists).
- SkipGo when no plan is possible (W4M 0x499ad0, **disasm**); without SkipGo the worm waits for the turn to end ("can't use the skip
  turn utility, so just delaying for a long time").

## Plans (`startEval`, `unit`, `decide`, W4M AIPlan best first 0x49a7d0, **disasm**)

- `canMove` (W4M GetThinkingWormCanMove): not nailed, not Tug O Worms artillery (0x90ea74), not past the repath limit (`noMove`, 0x9560f8).
- Origins (`Origin`, W4M CAIPlanAttack +0x4c): the root plans (close range: melee, dropped explosives, landmine; Special: Flood) where
  the worm stands, ×1 (0x495010, 0x4951a0). Projectile, homing, guns, strikes and animals: from up to 3 move → retreat pairs, for
  targets over 5 m from the move node, × log(1.1·e^r), r the retreat's def (0x49f8a0, 0x49fdd6); when no pair is left, from where the
  worm stands × log 1.1 (r 0, no retreat). The worm cannot move: that last origin only.
- Move window (`Moves`, W4M 0x4ab490): 21 × 21 entries at every other node (±20 nodes) × 2 layers, each scored by `posScore` (W4M
  0x4aa6a0): def (the retreat score) and open (the move score), formulas in docs/w4m/ai.md §18 "Plan selection". Node heights
  (`nodeH`, W4M 0x4aed70): flag (node, water/outside, blocked), lo, hi; layer 1 from 1 m under layer 0's lo, ours: it first leaves the
  land it starts in (W4M's ray from inside land is not read). Probes start at the last step over each column's highest solid voxel (`colTop`):
  same samples. Each ray is `landBelow` (W4M 0x466ae0 with refine 1): the first solid sample, 5 bisections, the last empty point [disasm], so a
  node on flat ground is the standing worm's feet and an origin's `pos` (W4M +0x4c) is where the worm stands; `probe` (jump edges) uses the same ray.
- Pairs (`pairOf`, W4M 0x4a8c60): each entry as a move node takes the best def within ±2 entries as its retreat; best def + 0.001·open
  wins if it beats entry 0 (the window's corner, as W4M reads it). `nextPair` paths to the move node (200 iterations) and on to the
  retreat (50); both found: an origin, both nodes blocked ±4 entries (`blockAround`, 0x4a9030); a leg not found blocks its node.
- Move plans (W4M CAIPlanMove 0x4a7180), when `canMove`: crates (CollectCrate: AddScoreCollectSomething, health ×3 when poisoned or
  low, ×R/d past moveFar; ours: crates within 18 m and 3 m up or down); unless `moved`: Closer per enemy, 10 × its value (0x4a8ac0),
  a partial path of 500 iterations, no time check (0x494950); 5 random small moves within ±5 m snapped to a node, score 0, 100 iterations
  (0x4a7330). All × t/T under moveTime (0x4a75c0) and × (1 ± randomise) (0x498a0a).
- `decide`: the attack (its rank) and the move plans in score order (`stable_sort`); a negative score ends the list; a move plan is
  tried by its path (W4M IsPossible), the next one when none is found. Nothing possible: SkipGo. W4M's order on equal scores (a stable
  bubble sort, 0x493cf0, popped from the end) never decides here: random small moves (0) come with a Closer plan (> 0).
- Attack chosen (`finish`): from a move node, Delay(DelayBeforeFirst/NonFirstMove), the path, 0.5 s, then the weapon (W4M 0x49e6d0);
  the retreat path follows 1 s after firing for projectile, homing, guns, strikes, close-range explosives, Old Woman and Scouser
  (plan flag 0x100), not for melee, Sheep, Super Sheep, Starburst or Flood. Move plan chosen: walk, 0.5 s, think again (Rethink 0x4a6e00).

## Pathing (`Search`, `searchStep`, W4M AIPathManager 0x492d80)

| parameter | ours | W4M | tag |
|---|---|---|---|
| grid | `Grid` (`navgrid.h`, shared with the start placement): max(0.5 m, sqrt(Σ box areas / 16000)); boxes = `Terrain::blocks` (importer: W4M land frames + heightmap; our maps: island heightmap + each shape), area ≥ 0.625 m², merged under a 2 m gap; lattice at the boxes' min corner, round to nearest; nodes only in a box's index range [round(min), round(max)] | same (0x4b22e0, 0x4ae320, 0x4aeb70, 0x4b2ba6, 0x4b1e00, 0x4ae9d0) | disasm |
| nodes | (cell, layer), heights from `nodeH` | same (0x4aed70) | disasm |
| walk edges | −x, +x, −z, +z, then each diagonal whose two sides were added; to the first layer that is a node with hi' − lo ≤ 1 m and hi − lo' ≤ 0.045/|g| (9 m); ours: the worm must fit there (`fits`, W4M Fits 0x59edf0) | 0x492510, 0x490830 | disasm |
| jump edges | from layer 0, 8 headings: where our jump from rest lands (`jumpLand`: the sim's gravity, feet, head and front against the land), kept if the W4M reach table accepts it (`buildReach`, `jumpOk`) | edges to every valid table cell (0x4928e1), the worm aftertouches to it | disasm; ours: our sim has no jump aftertouch, so one landing per heading, and backflips (3.2 m at most < 4 m) never pass |
| G cost | 10 / 14, jumps + octile + 40 / 60 | same (0x491fd8, 0x492008, 0x492003) | disasm |
| heuristic | octile 10·max + 4·min to the goal cell | same (0x4923a9) | disasm |
| iterations | per `Route`: 200 (pair move leg, crate), 50 (retreat leg), 100 (random small move), 500 (closer), at most 200; one node per unit | same, capped at 200 by 0x4b0ba6 (0x49fa5c, 0x49fab4, 0x4a7013, 0x4a7083) | disasm |
| time | the path is refused when G × 0.0029 × spacing (units) s > turn time at think start − 10 s; closer: no check | 0x4ae0a0, 0x494614 | disasm |
| goal | one node; a whole-path search to a goal that is no node fails at once | 0x492e19, 0x492210 (codes 5..9 on the goal's 0x4aeb70 / flag) | disasm |
| empty path | fails, except the retreat leg (its retreat is the move node) | 0x4942f0, 0x959a58 0xb | disasm |
| partial (closer) | closed node of least h; a path ending under 2.5 m from the start fails | same (0x490ed0, 0x494a42) | disasm |
| repath | a walk step held off its node (`stuck`) blocks that node (`blocked`) and pathfinds again to the same goal (`walking`, `repath`); past 2 repaths on one path the worm may not move this turn (`noMove`); also during the retreat | path-failed blockage, repath, "too many repaths, forbidding further movement" past 2 (0x490551 → 0x490601) | disasm |

- `stepInput`: walk turns at 2.5 rad/s, walks when off by < 0.3 rad with stick ∝ distance; a jump aligns within 2e-3 rad then presses JUMP,
  a second press with the stick held back makes a backflip (yaw + π; ours: no JumpBack key); the air phase ends when still or after 400 ticks. A path is followed with the sim's own
  walk, vault and jump code (the stick stays held while vaulting).
- Jump error (`takePath`, W4M 0x496f24, **disasm**): each jump's displacement scaled per component by 1 + jumpErr·noise; only the heading changes.

## Timings

| what | ours | W4M | tag |
|---|---|---|---|
| before thinking | none; waits for the worm to stand still, not while a crate drops | DelayAtStart 0 | data |
| before first move | 0 | DelayBeforeFirstMove 0 | data |
| before a later move | nonFirstMove (CPU1 120 ticks) | same, minus think time (0x4983ef) | data; ours does not subtract |
| after a path | 30 ticks (0.5 s), then the weapon (attack from a move node) or think again (move plan) | Delay 0.5 s (0x49e6d0), Rethink (0x4a6e00) | disasm |
| strike heading | the Blimp view turns at BLIMP_TURN (1.21 rad/s) while the cursor moves | SetStrikeDirection sets it at once | ours (the CPU plays inputs) |
| aim | turn 2.5 rad/s, pitch 1.5 rad/s through `Input`, aligned within 2e-3 rad | SetAimAngle sets it at once | ours |
| before fire | fireDelay / DT = 30 ticks (CPU5 12), counted once aimed | DelayBeforeFire (0x49eb8b) | data |
| charge | FIRE held `plan.charge` ticks (1.5 s full) | exact SetLaunchVelocity, no bar | ours |
| retreat | 60 ticks (1 s) after firing, while the shot flies; the path planned with the attack | Delay 1.0 s then Path (flag 0x100) | disasm |
| jetpack | at most 600 ticks of flight | | ours |

## Per-frame work (`slice`, `unit`, W4M 0x49b210)

- `budget` = 20000 `Terrain::samples` per frame (~0.7 ms desktop). Each `unit()` costs its samples + 100; units run while `debt < budget`;
  `debt` carries over, capped at 2 × budget (W4M: 80 units/frame, pathfind and attack plan 100, position 10, **disasm**; scale **ours**).
- A unit is one of: one threat rating, one row of the move window's layer-0 heights (65 nodes), one window entry's scores (+1000: 625
  cached nodes), one A* node expansion, one pair to path, one shot candidate (a walker's walk: 300 steps of it, resumed by the next
  unit; the Old Woman's 30 s fuse took ~170k samples in one unit on the imported maps).
  Raw voxel reads for column tops count 1/8 each. Unusable weapons are skipped for free.
- Exact land (docs/sim.md "Exact land") counts in the same unit, one `field()` read: each `SharpLand::eval` 4 (+1 through
  `sample`), each `first()` 6 plus its evals, each cell a `Terrain::cast` steps through 1. Measured on desktop (3 imported maps, warm
  points): a unit then costs 0.7-1.2 `field()` reads in `sample`, `normal` and casts of 0.3-1.3 m [ours].
- Measured (desktop, 8 imported maps x CPU1/3/5, 8 turns each) [ours]: before the exact land, worst tick 1.8-2.0 ms, p99.9 1.0 ms,
  23-25 ns per sample, 35.6 damage per turn; exact land uncounted 2.9-3.9 ms, p99.9 1.3-1.4 ms, 31-34 ns; counted with the walks
  split 1.8-2.1 ms, p99.9 0.9-1.1 ms, 20-23 ns, 34.3 damage per turn (34.8 uncounted; 21 of 24 matches deal the same damage, the
  others shift with the fire tick). ai_check's maps have no `.cells`: its output is unchanged.
- Modes: `Eval` (threats, window, pairs and their paths, shots origin by origin, then `decide`), `Search` (a move plan's or a repath's
  A*), `Walk` (`follow` the path), `Act` (a close-range retreat A* first, then select, aim, wait, charge / fire).
- In `Flying`: retreat, steer the bomber / super sheep, detonate the sheep as it leaves its target. Roped: release with JUMP.
- Caches that keep the slicing: a jump edge's landing (`jumpFrom`, per think, reset on `Terrain::edits`) charges the
  `Terrain::samples` it first cost; the jump reach tables visit only the cells each 1 ms step crosses [ours].

## Determinism

- The AI reads `Game` and returns an `Input`, logged, relayed and replayed like a human's (**ours**).
- Every seed comes from turn-start state: `salt = (g.rng ^ hash(hp)·2654435761) + current`, never `g.clock`; decisions use `thinkTimer`
  (turn time at think start), not the time the think took.
- The result never depends on slicing: units are evaluated in a fixed order, `stable_sort` on the plans.

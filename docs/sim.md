# Simulation: turn flow, input, movement (ours)

What `client/src/sim.h` / `sim.cpp` (`Game`) do, with the W4M source of each rule. The W4M side (exe timers, state handlers)
is in `docs/w4m/physics.md` §11 and `docs/w4m/turn.md` §14; this file is about our code. Status per fact:

- **data**: a value read from the W4M files (WEAPTWK, TWEAK, LOCAL, CAMTWK...);
- **disasm**: a rule read from `WormsMayhem.exe`;
- **assumed**: inferred, not verified in W4M;
- **ours**: no W4M source, our own choice.

The sim runs at 60 Hz (`Game::DT`); W4M lengths are in units of 1/20 m. W4M runs logic and physics at a fixed 20 ms step
(docs/w4m/physics.md §24) [disasm]. How ours meets it:

- **Timers**: `msTicks(ms)` = the tick nearest the W4M frame that fires the timer, the first 20 ms frame at or past `ms`:
  10 ms → 1 tick, 30 ms → 2, 250 ms → 16 (W4M 260 ms), any multiple of 100 ms exact [disasm: the 20 ms tasks; the "at or past"
  test assumed for each timer]. The turn timers (TimerLogicEntity, 10 ms) are whole seconds in every scheme, weapon and mission script but two
  missions' `PostActivityTime` 10, 1 tick by either grain, so their 10 ms grain never shows [data: weapons.json, mission `.lub`].
- **Walking**: one W4M walk step (`Walk.Speed × 20 ms`, 1.225 units) on each tick in which a W4M frame ends (`Game::walkFrame`,
  5 ticks of 6), counted from the walk's first tick (`Game::walkTick`, the AI's `Mover::walkTick`), so a walk does not depend on
  when it starts: W4M's frame grid is fixed in game time, and its AI waits whole frames [disasm 0x5b0da0; the phase is ours]. A
  smaller per-tick step samples other candidate points than W4M and changed the end of 103 of 772 walks (below) [ours, measured].
- **Constant-acceleration bodies**: `arcMove(v0, v1, lag)` is a tick's move as the velocity went v0 → v1. lag 0 is the closed-form
  parabola: Parabolic payloads (shells, grenades, bomblets, airstrike bombs, Fatkins, mines), the AI's `fly` / `jumpLand` /
  `bombLands` and its aim formula (V = (T − P − A t²/2) / t), `scoutSolve`, the camera's FindFirstEvent and worm-track
  predictions [disasm: Parabolic 0x57708a, oil drum 0x5d1c60, sentry 0x56d7f0 step `v·20 + a·200`]. `EULER_X` (+10 ms) is W4M's
  20 ms explicit Euler (crates 0x5c961a, bubbles 0x54f160: `pos += v·20` then `v += a·20`) and `EULER_SI` (−10 ms) its semi-implicit
  Euler (the PayloadLogicEntity base: slot 26 0x57e0a0 `v += a·20`, then slot 25 0x57fa70 `pos += v·20`: Homing, vtable
  0x859e7c; the Scouser's drift, weapons.md) [disasm]. Either Euler path is the parabola launched with v0 ∓ a·10 ms, so `arcMove` lands every tick on W4M's path
  whatever our step. Worm flight keeps its own exact step (`flyBody`, W4M Integrate) [disasm].
  Before (semi-implicit Euler at 1/60 s): a 32 m/s bazooka landed 0.11 to 0.56 m short and peaked up to 0.23 m low; now within
  2 mm of the closed form, wind ±4.25 m/s² included [ours, scratch model on `arcMove`].
- **Per-frame rules** are converted per tick (`K = DT / 0.02`, `powf(f, K)` for the decays, caps × K for the capped moves) [ours].
  Audited: the decays are exact over time; a cap or lerp mixed with a decay (drowning float, chute, vault lerp) differs from W4M's
  per-frame result by under 1 % of the move, under 1 cm [ours]. The vault snaps after 16 ticks (267 ms) where W4M snaps on its
  13th frame (260 ms), 0.8 cm of lerp apart [ours, computed]. The stuck count runs ×6 (`STUCK_UP` 10, `STUCK_DOWN` 5,
  `STUCK_MAX` 120: W4M's +2 / −1 per frame and 20, per 5/6-frame tick), so a worm stuck every tick lands after 200 ms as in W4M [disasm 0x5af821].

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
`WIND_CAP[wind] × r²` along (cos, sin) of r2 × 2 × 3.14, `wind` / `windZ` (W4M stdlib SelectRandomWind, data; an xz vector 0x57eb25, disasm; levels 0, 3, 5, 10 / 10; HUD meter `Hud::draw`: downwind pointer in the camera frame, "NNm" = round(10 × |wind|), docs/w4m/render.md "Wind meter"; `Ui::windPointer`: W4M's yaw with its -sin(-268 / 640) term, the tip turned by ArrowOrien (0.75, 0, -0.2) in XYZ order and seen down -z, the sprite foreshortened along its axis; ours: the HUD is drawn in 2D with no projection of its own, so it matches W4M only if W4M's HUD camera is orthographic (not traced)); the weapon in hand = `picked[team]`
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
  or crater and puts down the gas cloud (`Game::gas`), which poisons. `GameEvent::Arm` is the impact (ArmSfxLoop BowImpact). When the voxel half a
  voxel ahead of a stuck arrow is gone (our stand-in for Land.NewShape's "its land frame is gone", 0x5777f0 / 0x574d10) it falls again (`stage` 2,
  fuse still running); stopping again keeps the first detonation time (the first Payload.Detonate stays queued) [disasm; voxel probe ours]. It skims
  as every SkimsOnWater payload (below, "Shots in water").

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

- **Start placement** [disasm, docs/w4m/turn.md "Start placement"]: `Game::placeWorm` per worm in team order over the W4M AI node grid (`navgrid.h` `makeGrid` / `nodeH`, the one builder the AI also uses, built from the start terrain): a discarded draw (0x5b41a8 debug log), the yaw (k/256 turns), then up to 1000 cells from 0x4ae810 (500 draws each of a box by area, a node in it, layer 0 or 1; the first walkable node, flag 0; none ends the search), the point = node mid-height + (0, 10, -5 ZOffset) turned by the yaw + 10 units, a sphere 10 units there clear of placed worm colliders counts, the strictly highest of 3 wins; fallback Land.Center +-2.5 m at Land.MaxHeight + 0.5 m. The point is the worm's feet: it falls up to ~1.25 m onto its node. Then `addObject` mines and drums via `dropPoint` (100 tries, sphere radius + 5 units clear of worms and objects, none created on failure). Map `spawns` no longer exist (ours, removed). Deterministic from `rng` (ours: our LCG, not W4M's rand(); the draw order is W4M's); the team box test is a no-op (default box = the whole land). sim_check `checkPlacement` asserts it over 6 maps x 30 seeds (worms on a walkable node + 20 units, some on layer 1 under an overhang).

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

## Wormpot (`WormpotMode`, `Game::pot`, `Game::wp`)

W4M side: docs/w4m/turn.md §6b.
- **Reels** [ours, data]: `GameConfig::wormpot` holds the three reels' W4M mode ids, one per byte (W4M FE.Wormpot.Reel1..3). The setup screen
  uses W4M's reel lists and FETXT names. `Game::pot` is the set of picked modes. Saved settings use the key `reels`.
- **Super Explosives / Clusters / Animals / Firearms / Hand to Hand, Super Secret Weapons** [data, disasm]:
  - `containerOf` maps a shot to its W4M container: bomblets are kWeaponClusterBomb / Bananette, bomber payloads their own.
  - `superScale` gives (damage and crater ×2, push ×2) for the Super modes. The super weapon of the active team adds ×2 damage and crater for its
    turn.
  - `superWeapon` is drawn at start from the team's ammo, before Crates Only clears it. The 150 type-4 cap is in `hurt`.
- **Specialists** [data]:
  - `special` holds each worm's class. `allowed()` (inside `usable()`) applies the active worm's Allow set; the class's ammo is written into the team
    ammo. Homing / Airstrike delays become 1 / 5. Our teams have at most 4 worms.
- **No Cowards** [data]: the scheme's retreat time becomes 0 (weapon overrides stay), Surrender ammo 0.
- **Energy Or Enemy** [data]: every worm starts poisoned at Worm.Poison.Default 10.
- **Tug O Worms** [disasm]: `artillery()`: no walking, no jump, the Jetpack not allowed; the rope keeps its ammo. The AI plans no move and no retreat.
- **Jumping Only / Quick Walk** [disasm]: `walkScale()` is Worm.VelocityScale (0 / 2). The mystery Quick Walk sets 2 for the turn unless Jumping
  Only is on.
- **Wind Affects Guns** [disasm]: `Wobble` / `wobbleStep` is W4M's GunWobbleObject on the Shotgun and Sniper Rifle. `aimDir` adds it, so the shot
  and the aim camera sway.
  - [ours] W4M multiplies the wobble by min(1.5 zoom, 1) of the client camera. The zoom is not on the wire, so ours uses 1.
  - [ours] The per-firing-tick kick draw is skipped (KickSize 0, no effect besides the RNG).
- **No Blimp View** [disasm]: the sim drops TARGET, and `blimpable()` (controls.cpp) refuses the view. Targeting weapons aim from the aim view.
  The AI skips the Homing, Airstrike, Super Airstrike, Concrete Donkey and Fatkins plans.
- **Mine Respawn** [disasm]:
  - Explosions carve nothing; the gun's single voxel and the girder are not gated.
  - A detonating mine is never a dud, and new mines skip the dud roll. `respawns` re-creates the mine 500 ms later where it went off, if above water.
- **Dim-Mak** [disasm]: the Prod deals the hp left, type 5, no push. [ours] The WXP_Wep_DimMak effect is not drawn.
- **Wind Affects Worms** [disasm]: `wormWind()` (Wind × 0.5) is added to the flying worm's acceleration in `wormBody`, the AI's copy included.
- **Donor Card, Girders Only**: in the name table, on no reel (W4M has no reader); nothing to do.

## Mystery crates (`addObject`, `openMystery`)

W4M side: docs/w4m/weapons.md "Mystery crates".
- **Spawn** [data]:
  - The fourth share is `Scheme::mysteryShare` (W4M MysteryChance): 20 in All Action, 30 in Mega Power, 0 in the other presets. The scheme editor
    has a row for it.
  - The item is drawn at spawn by `MYSTERY_ITEMS` weights (the table of every scheme with mystery crates) into `Object::mystery`.
- **Collection** [disasm]: `openMystery` runs after the crate is removed. Every item follows W4M's values.
  - [ours] Teleport moves the worm at once, as our Teleport utility does; the fit test is "not inside land".
  - [ours] Flood is our instant Flood rise.
  - [ours] Disarm walks our weapons in W4M inventory-slot order (`inventoryId`).
- **Feedback** [data]:
  - The `Mystery` event plays `weapons/BuffaloOfLies` (sfx `buffalo`) and shows `Text.kMystery*` in the banner.
  - The spawn banner is Comment.Mystery.
  - The crate lands with CrateImpactWeapon.
  - Crate Spy shows the item.
  - The model is `crate_mystery` (tools/w4m-models `Crate.Mystery`), falling back to the weapon crate.

## Movement and collisions (shared with the AI)

Free functions in sim.cpp, also called by `ai.cpp` (`Mover`, `stepBody`) so the CPU predicts exactly what the sim does.
Worm body: centre `pos`, radius `R` 0.5 m, mesh half width `BODY_R` 0.3 m; eye `Worm.EyeLevelOffset` 15 units = 0.75 m above the feet
(data). Terrain: 0.25 m voxels (`Terrain::VOX`), water at 3 m (`Terrain::WATER`, rises with Flood and sudden death).

| function | does | source |
|---|---|---|
| `footing` | the grounded test: the centre or a foot of the W4M tripod (±4, −3) / (0, 5) units in land; one foot alone carries the worm only on ground under 60°. Its normal, for a worm put down or pushed on the ground, is the mean of the faces the feet's down rays (from 6 units up) enter within 1 unit of the highest | W4M land probe 0x91ffc8, normal 0x59ef90 (disasm) |
| `feet` | W4M CastRays down the 4 foot rays from 20 units over the feet, 26 units long: d = 20 − the nearest hit in units, each hit the exact crossing less 1e-4 m (W4M's last empty sample, a float); a ray starting in land gives 20; the normal is the mean face entered (0x46a070) of the hits within 1 unit of the nearest | W4M 0x59ec70, 0x468490, 0x59ef90 (disasm) |
| `walkStep` | one W4M frame of walking (`Walk.Speed × 20 ms`, run on `walkFrame` ticks), W4M UpdateWalking on `feet` at the candidate: d > 5 vaults (a 5..20-unit ledge, walkable and Fitting at hit + 0.1 unit) when `vault` is given (the AI passes its own), else blocks; −5 ≤ d ≤ 5 steps onto the hit + 0.1 unit unless the uphill rule refuses (n·(cand − pos) < 0 on ground over 60°), pushed out +0..+4 units until the rods Fit, and reports the cast's normal in `ground`; d < −5 falls at once from its height when the rods fit there (returns true; Velocity = InputImpulse 2.5 m/s × stick), else +1..+5 units | W4M UpdateWalking 0x5b0da0, 0x5b1209, 0x5b1920, 0x5b194c, Fall 0x5b14c7 (disasm) |
| `vaultStep` | the vault: 250 ms, 1/5 of the way per 20 ms (0x5a59f0), no collision test; releasing or reversing the stick puts the worm back where it started; snapped to the target at the end or when anything else moves it | W4M Vaulting 0x5aca80, ChangeState 0x5aa847 (disasm) |
| `fits` | the upper body (7 points of radius 0.2 m at 0.7 / 0.95 m over the feet) is out of land at `to`, or no deeper than at `from`: `rodsFit`'s fallback when the rods are already in land | ours |
| `sweep` | the 8 PROBE points (4 feet, 4 heads 1 m up) `cast` along one tick's move, each stopping 1e-4 m short of its crossing (W4M's last empty sample); earliest first, a foot on a tie; normal = mean of the hits within 1 unit [disasm]. Each hit's normal is the face crossed (W4M 0x46a070, docs/w4m/physics.md §11 "Ballistic"); a cast starting in land takes the start cell's nearest face with an empty neighbour [disasm]. No hit is filtered on imported maps; [ours] maps without cells skip a gradient normal facing along the ray. Shared by `flyBody`, `jetBody` and the slide's wall branch | W4M CastRays 0x59ec70, normal 0x59ef90 (disasm) |
| `rodsFit` | W4M Fits for the walk, the slide and the flight: the 3 rods, a `cast` from the feet to the heads, clear of land. [ours] rods already in land at `from` fall back to the relative `fits`, so a worm that land appeared around can move out | W4M Fits 0x59edf0 (disasm) |
| `flyBody` | W4M Ballistic on land, one tick: the `sweep` along `v·DT + ½a·DT²`; no hit Integrates (not Fitting: kept, stuck +2 per W4M frame, `rebound` back along the move); a hit moves to the contact if it Fits (else kept, stuck +2, rebound back), stores its normal, then a head or n.y < 0.2 rebounds on it, a foot lands (`wormBody`: vt walks or slides, FallDamage on −vn). `rebound` = Bounce e 0.3, tangent kept, stops under 0.5 m/s (facing up: Sliding) | W4M Ballistic 0x5af430, Rebound 0x5acea0, Bounce 0x518f40 (disasm) |
| `jetBody` | the jetpack's own collider, one tick, on the shared `sweep`: a foot whose rods Fit (`rodsFit`) lands the pack (v minus its normal part) whatever the normal, a head or blocked rods bounce v −= 1.8 (v·n) n. [ours] a worm at rest (our take-off tick, thrust starts the tick after) is neither swept nor moved. A landing hands the worm to Ballistic with its tangential speed (0x5ae17a), which lands it through the sweep | W4M 0x59ec70, 0x59ef90 (0x562ecd), 0x59edf0, 0x562f72, 0x5630dc (disasm) |
| `wormBody` | one worm tick: grounded test (land within 2 units under a worm that stood, or one put down at rest; a flight lands only through its sweep), slide (below 60° and slower than 3 m/s, 10 m/s on landing: stops; else gravity along the slope and friction 0.9582 a tick), a hard landing halves \|vt\|², the flight is `flyBody`; a grounded worm is not moved by its velocity that tick; [ours] an idle worm with no ground under it stays when a 1-unit fall does not Fit (W4M gets there through Rebound → Sliding → Landed with support 0xFFFF) | W4M Sliding 0x5afbe0, Integrate 0x5a6e90 (disasm); SlideFriction 0.95 / 20 ms (data); Wormpot Slippy: each slide value halfway to its Slippy one, 35°, 5.25 / 1.75 m/s, 0.9745 (0x5d59c0); Sticky: blast impulses × 0.5 only (0x5ad1ea) (disasm) |
| `walkerStep` | sheep, old woman, scouser on foot: steps up 0.6 m, hops at walls, whole-body roof test | ours |
| `muzzle` | the launch point pulled back to the last free point on the segment eye → spawn | W4M 0x585a29 (disasm) |
| `launchPoint` | the eye plus WEAPTWK LogicalLaunchZ / YOffset: dynamite 13 / −10, landmine 10 / −10, (super) sheep and Starburst 5, old woman 7, scouser 10 units, the rest 0 | data |
| `restOn` | a payload at rest drawn `r` above the land along its normal | W4M 0x574e90 / 0x5761f0 (disasm) |

Constants (sim.h / sim.cpp): gravity 12.5 m/s² (W4M Gravity −0.00025 units/ms², data; low gravity × 0.5, Low.Gravity.OnValue);
walk 3.0625 m/s (Walk.Speed, data; Quick Walk: VelocityScale 2, 0x5d6bc0, disasm); jumps: tapped or held forward (3.16, 7.91) m/s, held still: vertical 9.35 m/s,
pressed twice: backflip (−1.58, 10) with the stick against the facing, else forward flip (1.58, 10), the stick at rest included (W4M 0x5a5d30 / 0x95fb88 / 0x95fb8c, data; +0x159, docs/w4m/physics.md DetectJump, disasm); `jumpTick` returns the W4M kWE code (3 / 4 / 5 / 6), carried in `GameEvent::Jump.weapon` to pick the Jump / Backflip / Fwdflip clip (main.cpp), and the worm keeps its yaw through the 300 ms window (DetectJump never writes Orientation, disasm); the AI backflips with the stick back (ours: W4M's AI executor sends no JumpBack); fall damage above 15 m/s:
trunc((v − 15) × 2) + 1 hp (`Game::fallDamage`, shared with the AI; W4M FallDamage 0x5ac3e0, FallDamageRatio 100, data; Max Fall: FallDamageRatio × FallingScale 2, Wormpot.lub, data; none when the scheme has fall damage off or under Wormpot Worms Drown, SetNoFallDamage); no fall
damage in Icarus flight (W4M flag 0x40, 0x587446, disasm); none on a jetpack landing: a foot touching land lands the pack minus its normal speed (W4M 0x562f72, disasm), so `stepWorm` skips `land()` while `jet`; a dry pack falls and hurts as any fall (docs/w4m/physics.md FallDamage); the fall also rumbles the worm's pad: Heavy 100/255 for 500 ms (0x4bc410, disasm; GameEvent::Fall).

### Exact land (`SharpLand`, sharp.h; `Terrain::sample` / `normal` / `cast`)

Imported maps carry `<map>.cells` (format: docs/w4m/formats.md §22): for every 0.25 m cell the surface crosses, the ordered list of the
primitives reaching it [ours]. The sim reads that land exactly; so does the mesh in listed cells (docs/maps.md "Land mesh").

- Primitives: a W4M poxel cell (`HEX`, a convex hexahedron: its 12 triangle planes, a twisted one bounded by its box too) with the mask of
  its planes that touch the cell, the heightmap patch (`HM`, bilinear over the grid columns), and the edits: carve spheres (`SPHERE`) and
  weld boxes (`BOX`) [ours; geometry data, docs/w4m-formats.md "Conversion to our grid"].
- Density in a listed cell (`eval`): start at −0.5 (air), then in order: a cell or the heightmap is a union (max), a carve a subtraction
  (min with the distance to the sphere), a weld a union; clamped ±0.5 m. A cell's value is its nearest masked plane (+1e-5 m so land wins
  ties) [ours]. The normal is the deciding primitive's: a cell's plane, the heightmap's slope, the sphere's radius, the box's face.
- Every other cell lies wholly in or out of land: `d` (the .vox) holds its sign. The importer makes every grid point's sign exact (from a
  listed cell around it, else from a filled one), so `d` and the lists agree [ours].
- `Terrain::sample(p)`: the listed cell's `eval`, else the trilinear `field(p)`. `normal(p)`: the listed cell's face, else the gradient.
- `Terrain::cast(a, dir, len)`: cell by cell along the ray (DDA); a listed cell returns its exact first land from the candidate crossings
  of its primitives (plane slabs, sphere roots, the heightmap bracketed and bisected), an unlisted solid cell its entry; the normal is
  taken in the cell where the land was found, so a face lying on a cell border is read from the cell that lists it [ours]. Of a W4M
  cell's planes it is the one the ray crossed last (`eval` given the ray), the face entered as W4M 0x46a070; a cast starting in land
  takes `startNormal`, the start cell's nearest face with an empty neighbour (docs/w4m/physics.md §11 "Ballistic") [disasm; distance
  for W4M's cell fraction assumed]. Maps without
  `.cells` (the bundled `romfs` maps, the generated island, test fields): sampled every VOX/4 and bisected to the last point out of land,
  normal = gradient over ±VOX/4 [ours].
- Edits are incremental, before `d` changes: `carve` appends a sphere to each cell it cuts (a cell it swallows leaves the lists),
  `weld` a box, `Terrain::addCell` a convex cell (test arenas, `checkLowLedges`); replaced lists become garbage, compacted when it
  exceeds half the pool. Lists shared by several cells stay shared [ours].
- The instant replay's undo log does not restore the lists (replay disabled for now, docs/w4m/README.md).
- Cost: the 221 imported maps' `.cells` take 282 MB on disk (deflated); in memory 12.5 MB (Deathmatch3) to 27.4 MB (DoomCanyon), loaded
  in 23-46 ms on desktop. AI planning (scratch benchmark, 8 maps x 3 levels): 23.1 ns per terrain sample before, 30.2 after; mean
  59.8 ms per turn before, 75.3 after; worst tick 2.19 ms before, 3.48 after, back to 1.8-2.1 once the exact land
  counts in `Terrain::samples` and the walkers' walks are split (docs/ai.md "Per-frame work").

API for a mesher (dual contouring), all on a listed cell `c` (`mixed(c)`; cell index `(z * NY + y) * NX + x`, its box
`[x, x+1] x [y, y+1] x [z, z+1]` voxels):

| call | gives |
|---|---|
| `ops(c)` | the cell's list: `[0]` = word count, then ops `kind \| id` (a `HEX` op is followed by its plane mask; `hexP0[id]` = its first plane in `planes`) |
| `eval(p, c, &n)` | density at p (> 0 land) and the deciding primitive's outward normal |
| `edge(c, a, b, &q, &n)` | the surface crossing q on the segment a-b (an edge of the cell, ends on opposite sides) and the normal there |
| `first(a, dir, t0, t1, c, land, grid)` | the first t in [t0, t1] where the ray enters (`land` = true) or leaves land; `grid`: the segment is a grid edge, the heightmap's crossings solved (linear on each half) instead of bracketed and bisected (mesher only; the sim keeps `grid` = false) |

`d`'s sign at each grid point is exact, so the edges to visit are the sign changes of `d`; the mesher (docs/maps.md "Land mesh") uses them.

### Movement against W4M, measured

A scratch harness (not in the repo) rebuilds W4M's land exactly: the cells and heightmap of tools/w4m-maps, before voxelization, in map
metres [data]. On that land it runs W4M's rules (docs/w4m/physics.md §5 / §11) [disasm]:

- the foot rays and the Ballistic cast, with float hits on the air side (0x468490's last empty sample, 1e-4 m here);
- the 0x59ef90 normal over the hits within 1 unit, and the start cell's open face for a ray starting in land;
- the uphill rule, push-out, vault, drop and the rods;
- Rebound, which stops into Sliding only on land facing up, and Sliding itself (the bump lab).

A W4M walker roams 8 maps (DM1, 2, 3, 5, 7, 9, Clean-w3d, StormTheCastle; 16 runs of 3000 ticks, random headings, stops and jumps). At
each of its states our `walkStep` / `slideIfSteep` / `wormBody` decide from the same feet. The corpus does not depend on our code, so
runs compare. A decision whose foot land differs by more than 1 unit between the two lands is counted apart ("land differs").

The earlier reference truncated every hit to a whole step. That truncation is 0x466a80's ray, not the worm's (§5), and its Rebound
stopped on any (0, ≤ 0, 0) velocity; the first column is ours against it, as published before.

| | 6e592e0, earlier reference | 6e592e0 | now |
|---|---|---|---|
| walk decisions differing | 272 / 76 385 | 635 / 76 774 (0.83 %) | 14 / 76 774 (0.02 %) |
| of them, on the same land | 164 | 336 | 8 |
| W4M blocks, ours slides | 3 | 362 | 6 |
| W4M moves, ours blocks / vaults / falls | 65 / 35 / 25 | 89 / 47 / 47 | 0 / 0 / 0 |
| W4M slides, ours blocks or moves | 53 | 21 | 7 |
| vault target off by more than 1 unit | 3 / 144 | 2 / 145 | 0 / 157 |
| flight ticks differing | 84 / 93 059 | 28 / 88 604 | 30 / 88 604 |
| of them, on the same land | 43 | 13 | 14 |
| jumps: landing kind differs / lands 0.25 m apart | 36 / 44 of 990 | 9 / 48 of 995 | 4 / 7 of 995 |
| wall jumps (each walks into the wall by its own rules, then a tap jump): 1 m verdict differs | 23 / 365 | 31 / 368 | 9 / 376 |
| synthetic bumps (288: 0.1-0.9 m, 60-90° faces, w 0.3 / 1.5 m, off grid, diagonal) differing | 48 | 42 | 9 |
| map sweep (11 520 runs, DM1, DM3, Clean-w3d, StormTheCastle): body ring over 0.06 m in land / stuck | 37 / 0 | 37 / 0 | 417 / 0 |

Walking at real time (harness `walkTraj`, W4M walker in 20 ms frames against our `walkStep` on game ticks, from the same 128 starts ×
8 headings, positions compared every 100 ms while both walk, then the event that ends the walk):

| | per tick, `Walk.Speed × DT` (before) | per W4M frame on `walkFrame` ticks (now) |
|---|---|---|
| 100 ms marks: xz / y gap, max | 0.6 / 1.0 mm (8 876 marks) | 0.1 / 0.0 mm (8 913 marks) |
| walk ends: kind differs (vault ↔ block, fall ↔ slide, block ↔ slide) | 103 / 772 | 2 / 771 |
| same kind, more than 20 ms apart | 4 | 0 (mean 9.7 ms: our step lands up to one tick before W4M's frame) |

The 2 left are the corpus's block ↔ slide rule cases below; stepping ours by whole frames back to back gives the same 2 [ours, measured].

What is left, traced case by case:

- Walk, same land: the uphill rule at an exact tie (`n · (cand − pos)` within 1e-6 of 0 on a uniform slope) and SlideAngle at n.y =
  0.5. These are float rounding in both models; W4M's own varies with its sampling offset.
- Flight, same land: a head point within 1e-5 m of a face (both models' tolerance), then rebound / stuck flips from 1e-4 m of drift.
- Wall jumps: the reference flies in W4M's 20 ms frames, ours in 1/60 s ticks, and a grazed wall answers differently.
- Bumps: the 60° faces sit at SlideAngle (n.y = 0.5); the 70° ones at the uphill rule's tie at the face's foot.
- Map sweep: the ring (0.2 m around the centre, sweep_lab) is not W4M's body. W4M Fits only the 3 rods, so the mesh dips into land between
  them (docs/w4m/physics.md §5 "corners"). The W4M walker of the harness has a ring point in land in 272 of its 75 440 states. Stuck
  stays 0; the 2 hover runs are unchanged (a long fall after a rebound off a pillar on DM1, bounces under the water line on DM3).

What changed with this pass [disasm]:

- **Hits.** The worm's rays stop on the last empty sample, with a float distance; ours stop 1e-4 m short of the exact crossing.
- **Normals.**
  - The foot normal is the mean within 1 unit of the nearest float hit.
  - A cell's normal is the face the ray crossed last, not the nearest plane. The nearest plane misread rays running along the inner
    face between two cells.
  - A cast starting in land takes the start cell's nearest face with an empty neighbour.
- **Removed [ours]:**
  - `clearWalls` and the walk's toe vault built on it;
  - the Ambulatory push-up, which climbed walls whose foot point it touched;
  - the probe's exception for a ray starting in land;
  - the v·n skip on imported maps.
- **Slide decision.** Sliding now starts from the walk cast's own normal (`walkStep`'s `ground`).

What stays [ours]:

- `rodsFit`'s relative fallback: land can appear around a worm (girders, edits, spawns), and no move of W4M's leads there.
- On maps without cells (romfs, the island), the skip of a gradient normal facing along the ray. The field's gradient at a wall's foot
  does so, and it would kill a jump grazing the wall (`checkJumpAtWall`).
- The jetpack's take-off tick neither sweeps nor moves: its thrust comes a tick later.
- The 1e-4 m air-side offset. W4M's offset depends on its sampling, up to 1/32 of a sample step.

### Ninja rope (`Rope`, `Game::ropeHang` / `ropeTick` / `ropeRelease`, shared with the AI's `Mover`)

W4M NinjaRopeUtilityLogicEntity, docs/w4m/weapons.md "Ninja rope swing" [disasm + data]; what is ours is tagged.
- State (`Game::rope`, checksummed while in use): the bends from the hook on (`pt`, `len`, unwrap `side`), the swing `angle` and `spin`
  (rad per 20 ms) in the vertical plane of `yaw`. The body point is the feet (W4M Position): `pos - R`. The yaw stays fixed on the rope.
- Firing (`use`, `grappleFire`, `grappleStep`, `Game::grapple`, checksummed in flight) [disasm 0x573790 / 0x572800 / 0x573d00]: FIRE
  launches the hook from the eye (feet + 0.75 m) at 50 m/s (1 unit/ms) along the aim, or, once hooked since the worm last stood
  (`Rope::swung`, W4M +0x79, cleared idle off Ballistic 0x574b96), along 0x570650's pitch: straight up, tilted 45° toward the horizontal
  motion at 10 m/s and over (proportionally below). FIRE again while it flies retracts it. Each tick: a standing worm's hook takes the
  first object its 0.25 m sphere touches (mask 0x1e: crates but mission targets, a still mine, a drum; a bubble retracts it); else land
  within the tick's flight hooks (`ropeOn`); else it moves on and retracts past MaxLength from the feet. Only a hook on land counts
  against Ninja.NumShots (`ropeShots`, 0x573ea3); the ammo goes once the rope has hooked, when it is put away or the turn ends
  (`ropeCleanup`, 0x5727b0 on +0x7b). Ours: the 20 ms step runs per tick. Not ported [ours, the sim has no camera view]: W4M refuses a
  standing fire from the first-person "Head" camera without a target (FEError, 0x573845).
- Hooking (`ropeOn`): length from the feet, angle = acos of the eye's drop below the hook (> 0: behind the facing), negated when hooked
  since the worm last stood and moving backwards, spin = the angle swept about the hook by 20 ms of the velocity, negative moving forwards.
- Each tick `ropeTick`: reel (`aim` sign, 10 m/s, MaxLength over the whole rope, refused under MinLength / MinBendDistFromWorm or when the new
  point is blocked), swing (angle += spin; stick push −s × SwingAmount × 100 × MinLength / (L + 0.001 L²), units; RotationDamping 0.99
  without stick; gravity 400 g sin(angle) / L), body at the last bend + L (−sin, −cos) of angle + spin; a blocked body turns the spin
  back × 0.9; land across the last stretch adds a bend (8 halvings back toward the old position, 1 unit off the land, spin × old / new
  length; under MinBendDistFromWorm: a bounce instead); the bend before seen again and the body past its side: the bend goes.
- Ours: W4M's 20 ms step runs per 1/60 s tick with every per-step term × `ROPE_K` = DT / 20 ms (angle, spin, pushes, damping as
  0.99^K). `ropeCut` (0x571020) casts from 0.1 m off the feet and stops 0.4 m short of the bend, and bends sit an extra half voxel off
  the land: our voxel raycast stops up to half a voxel deep, so the feet on the ground or a bend's own land would always cut the
  stretch. `ropeBlocked` samples Fits' three 1 m rods every voxel from half a voxel up (same reason), plus the 5-unit sphere at the feet
  against the colliders of mask 0x19 [disasm]: other worms (flag 1, 10 units, 5 above their feet), shots and mines (payload flag 8,
  their Radius; mines 3 units), drums (0x10, 9 units at their Position) and bubbles (0x10, 9 units); crates (2 / health 4 / target
  0x20) do not block a worm. A hooked object's rope uses mask 0x3f (crates too) and its own size: a crate 10 units, else 5 (0x571d90,
  0x572269). Rope.MAX 16 bends: a wrap past it bounces instead.
- Velocity while swinging is W4M's 0x56fd60 value, the step's motion × 0.001 units/ms (1/50 of the motion's speed); letting go (`JUMP`)
  sets the velocity of one more swing step over its time (0x573530, DetachVelocityMulti 1).
- `ropeSwing` (0x571820): without `HEADING`, the sign of `walk`; with it, the stick's direction against the facing: under 81° forward,
  over 99° back, 0 between.
- A hooked crate, drum or mine (0x571d90) runs the same update about the worm's feet at the hook time, its plane facing away from the
  worm, no spin; it hangs by its centre minus 10 units (crate, 0x5cbc86) or 9 (drum, 0x5d2135); `stepObjects` leaves it alone; `JUMP`
  lets it go with `ropeRelease`'s velocity (NinjaRope.EndSwing).
- The AI's rope planner (`Mover`, ai.cpp) flies the hook with the same `grappleFire` / `grappleStep`, objects aside.

### Shots in water (`stepShots`' `wet`)

W4M PayloadLogicEntity 0x582050 / Parabolic events 0x577980, docs/w4m/weapons.md "Payload water" [disasm + data]. weapons.json carries
`size` (Radius), `sink` (SinkDepth), `skim_*` (SkimsOnWater, MinSpeedForSkim, MaxAngleForSkim, SkimDamping), `cluster_size` / `cluster_sink`
(the bomblets' containers); Weapon Factory weapons get kWeaponFactoryWeapon / Homing / Cluster's in `Game::start`.
- Crossing Water.Level + Radius: a skim (v × damping, never for bomblets) or a splash (`GameEvent::Splash`); set on that plane either way.
- Crossing Water.Level − SinkDepth: `Projectile::sunk` (checksummed): speed capped at 5 m/s, xz × k², vy = −max(4, |vy| k), k = speed / 5;
  no acceleration, no fuse, no blast; any contact or Water.ExpiryDepth (absolute, `Terrain::WATER` − 10 m) removes it.
- A homing missile while homing only splashes at Water.Level. Walking payloads (old woman, scouser) keep their own sinking (0x594002);
  the sheep (W4M JumpingPayload) and the walks-first super sheep use these planes.

### Wind on the parachute, the scouser and the gas (W4M docs/w4m/weapons.md "Wind drift")

- Parachute (`chuteOpen`, `chuteDrift`, `chuteAt`, `chuteAng` / `chuteSpin` / `chuteSink` / `chuteGain`, `CHUTE_*`, checksummed) [disasm
  0x578db0 / 0x578a40 / 0x5792a0]: FIRE opens it only off the ground (Ballistic), spending the ammo, and closes an open one; held, it
  opens itself under −11.25 m/s. The canopy starts 2 m over the worm, angle and spin 0. Per tick (W4M per 20 ms, × K): the stick
  (`steerIn`, `chuteSteer`) turns the yaw 1 rad/s toward its side (pulled back or level: any side; pushed forward: only past 0.2 of full
  tilt aside) and nudges the spin 0.001 the other way; yaw −= 0.01 sin(angle); spin = (spin − 0.003 sin) × 0.99, angle += spin. Swung
  past cos 0.4 it sinks (`chuteSink` −= 2 (cos − 0.4) g ms⁻¹ × 1e-3 m/s), else the sink decays × 2/3 into `chuteGain`, which returns 1/7
  to 0 (≤ 0.05 m/s a step); the velocity moves half its gap to (3 m/s + gain) along the facing + `chuteDrift` (70 ms of the opening's
  wind and gravity), at most 2.5 m/s, then + the sink; the canopy moves by it and the worm hangs 2 m under it, swung sideways by
  2 sin(angle). Ours: the worm reaches that point through `wormBody`'s sweep (voxel land), its velocity kept as the canopy's; landing
  closes it (W4M: the canopy move no longer Fits, 0x57967e). The renderer's ParachuteLR follows the same stick test (main.cpp).
- Inflatable Scouser [disasm]: caught (stage 1, stopped), it inflates the next tick: 4 m/s straight up, 6 s to the pop; it rises
  through land until a tick clear of it (stage 2), then drifts (stage 3): wind × 4.25 × 0.6 m/s² per wind unit, no gravity; land
  then pops it. The carried worm's feet are at the payload.
- Gas cloud [data + disasm]: fixed 0.5 m above the blast for 8 s (WXP_GasCloud's ParticleMass 0 cancels its wind); the spiral
  wobble (0.1 m) is not modelled.

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

# Weapons audit: spec vs ours vs W4M

There are three sources:

- **Spec** is the external list the user pasted. Parts of it are wrong.
- **Ours** is `client/romfs/weapons.json` together with sim/ai/controls, as of this audit.
- **W4M** is the game's own data. When it disagrees with the spec, W4M wins.

W4M sources:

- **WEAPTWK** is `Data/Tweak/WEAPTWK.XOM`, decoded with a throwaway parser. World units are 20 per metre.
  - The first f32 of each payload run is the **max worm damage**. The wiki gives "W4M only: Bazooka 50, Grenade 55", which matches the values. For melee containers it is `Radius`, not the damage: the field names (`tools/w4m-re/xom.py dump`) fixed Bat, Fire Punch and Prod below.
  - A per-weapon u32 looks like a lifetime in ms.
- **TWEAK / LOCAL** are `Data/Tweak/TWEAK.XOM` and `LOCAL.XOM`.
- **Help** is `HelpText.kWeapon*0` in `client/assets/lang/en.txt`, which is W4M `English.xom`.
- **Wiki** is worms.fandom.com, read through its MediaWiki API (the pages themselves return 402), plus the worms.miraheze.org mirror.
- **Steam** is the Steam guides 2400725628, 2874524134 and 2577662277.

Verdicts:

- **OK**: ours already matches W4M.
- **fixed**: changed in this pass.
- **spec-wrong**: the spec disagrees with W4M; we follow W4M.
- **deferred**: a mechanic is missing; see the notes.
- **other-agent**: belongs to a concurrent agent's area.

Units in the tables: hp for damage, s for fuses and times, m for distances.

## Thrown and launched explosives

| weapon | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Bazooka | power gauge, wind, max 50 | charge, wind, impact, max **50** (was 45) | max dmg 50 | WEAPTWK, Wiki, Help | fixed |
| Homing Missile | target cursor, then launch dir; rises, homes ~5 s, then falls | reticle target, charge, no wind; ballistic **1.25 s**, homes **5 s**, then ballistic (was: locks at 0.4 s, homes forever); max **50** (was 52) | 1250 / 5000 ms header, max 50 | WEAPTWK, Wiki "~1 s then homes ~5 s then drops", Steam 46-50 | fixed |
| Grenade | fuse 1-5 s on the d-pad, bounces, exact | **fuse 1-5 s** (d-pad up/down while aiming, default 3, kept per team), exact to the tick; max **55** (was 45) | max 55; Help "FuseUp for fuse"; lifetime 6000 ms is a cap, not the fuse | WEAPTWK, Help, Wiki, Steam ("1 to 5") | fixed |
| Cluster Bomb (our "Cluster Grenade") | fuse 1-5 s, 5 bomblets | user fuse 1-5 s, 5 bomblets; main **15** (was 25), bomblets 15 | ClusterGrenade 15, ClusterBomb (bomblet) 15 | WEAPTWK, Help, Wiki (5 bomblets) | fixed |
| Banana Bomb | fuse 1-5 s, very bouncy, 5 bananas | user fuse 1-5 s, bounce 0.6 (grenade 0.45), 5 bananettes; main **50** (was 40), bananettes **60** (was 30) | Banana 50, Bananette 60 | WEAPTWK, Help, Wiki ("very bouncy", 5; the Wormopedia says 6) | fixed |
| Holy Hand Grenade | fixed 3 s, little bounce, Hallelujah, ≤100 | **no timer**: once it stops, the Hallelujah plays, then it blasts **2 s** later (`rest_fuse`; was a 3 s fuse with the choir on the boom); bounce 0.2; max **80** (was 90) | lifetime 0 + 2000 ms delay, holy.ogg 2.1 s, max 80; Help has no FuseUp | WEAPTWK, Wiki ("timer removed in W4M, explodes once stopped") | fixed (spec-wrong on 3 s / 100) |
| Dynamite | (other agent) | dropped, 7 s, max 59 | lifetime 7000 ms, max **75** | WEAPTWK, Wiki | other-agent: damage 59 → 75 suggested |
| Gas Canister | fuse 1-5 s, big poison cloud | fixed **5 s** fuse (was 3), explosion damage **0** (was 10), poison 10/turn to everyone within 3 m | all-zero damage run; Help has no FuseUp; Wiki "fixed 5 s on PC" | WEAPTWK, Help, Wiki | fixed (spec-wrong on the adjustable fuse); **cloud** now: one 5 m cloud lasts **8 s**, drifts with the wind (1 m/s per wind unit) and poisons every worm inside (`WXP_GasCloud`: 1 particle, 8000 ms, collision radius 100 units, `ParticleCollisionWormPoisonMagnitude` 10, wind on; PARTTWK). Green puffs in main.cpp |
| Poison Arrow | 15 impact, 10 hp/turn, min 1 hp, contaminates neighbours, health crate cures | impact **25** (was 10; first 30), poison **10**/turn (was 5), **wind** on (was off), min 1 hp, health crate cures, no contagion | no blast; Worm.Poison.Default 10; Help "wind affected" | TWEAK, Help, Wiki ("10 impact", lasts until cured, down to 1 hp) | fixed (`WormImpactDamage` 25, the only weapon with one; no blast: `WormDamageRadius` 0, no crater); contagion is spec-wrong (no W4M source) |
| Land Mine | - | worm-triggered, scheme fuse; blast **40** (was 45, AI copy too); **a blast only pushes it** (was: set off after 1 s) | max 40, Mine.Min/MaxFuse 1-5 s, 10 % duds | WEAPTWK | fixed; **duds fixed**: when the fuse ends, 10 % of mines fizzle (`Mine.DudProbability` 0.1, `ExpiryFx` `WXP_Wep_MineDudEffect`) and stay inert for good (`Object::dud`); **dud smoke** (client): `Fx::dud` replays the effect's two PARTTWK emitters, `WXP_MineDudPoof` (20 lilac `WXSprite4` puffs blown out flat, 1.2 ± 0.2 s, shrinking) and `WXP_MineDud` (one rising wisp per 100 ms for 1 s, 2 ± 0.6 s); `dudFx()` in main.cpp fires it when a dud mine appears, no sim event. Sizes and speeds are scaled by eye (W4M particle size units unknown); the emitter's `weapons/MineDud` sound is not played; **trigger fixed**: any worm within **2.25 m** arms it, flying or sliding too (Landmine `ArmingRadius` 45 units, was 1.5 m, so a worm blown past 2 m away missed it); the exe tests an arming sphere against every `kCF_Worm` collider each update (PayloadLogicEntity 0x580756), objects never arm it; a laid mine ignores worms for **2.5 s** (`ArmingCourtesyTime` 2500, `Object::courtesy`); random fuse **1-5 s** (Mine.Min/MaxFuse, was 0-5) |
| Sticky Bomb | sticks to first surface | absent | not in W4M (no kWeapon, no "sticky" anywhere) | data grep, Wiki (W3D / WMD only) | spec-wrong |

## Animals, strikes and specials

| weapon | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Sheep | - | walks, FIRE detonates, 8 s; max **75** (was 60) | max 75, lifetime 30 s | WEAPTWK, Help | fixed damage; 30 s lifetime not applied |
| Super Sheep | reactivate, steered, 20 s max | launched flying at once, steered, **25 s** (was 12); max **75** (was 61) | max 75, lifetime 25000 ms; Help "Fire to launch, transform and detonate" | WEAPTWK, Help | **fixed**: walks like a Sheep (5 s fuse, Wiki), FIRE takes off (25 s flight from then, the turn waits for it), FIRE again detonates; terrain or worm contact detonates (`DetonatesOn*` flags). Starburst still flies at once (Help: "Fire detonates, Movement controls flight") |
| Old Woman | - | walks straight, 6 s; max **75** (was 69) | max 75, lifetime 30 s; Help "Movement to steer, Fire to explode on demand"; steals 1-8 ammo | WEAPTWK, Help, Steam | **fixed**: steered with the stick, FIRE detonates (`DetonatesOnFirePress` 1); `WormCollideResponse` = `kWC_StealInventory`: each enemy worm she bumps loses 1-8 units of one random weapon of its team to the thrower's team (Steam 2874524134; the Wiki says the whole stock, no count in the data) **Settled (exe):** fuse **30 s** (was 6; the wiki's 5 s is wrong): LifeTime 30000 is copied into the payload by Arm 0x57ec20, only HasAdjustableFuse (0 here) overrides it (0x585d04), the walk update detonates at expiry (0x593f9b). **Theft:** `StealInventory` 0x592d30 takes **5 single units**, each from a random weapon the victim has, preferring those the thief has none of (loop `cmp eax,5` at 0x59300e); an infinite stock is not reduced, a thief with infinite stock gains nothing; any worm but the thrower is robbed, teammates included (was 1-8 units of one weapon, enemies only). Not modelled: her 800 ms stop after a steal (0x593370), the 2 s sink fuse (0x594050) |
| Inflatable Scouser | - | floats with the wind, explodes for 50 | damage run 40/0/21/0: bursts harmlessly; swallows a worm and carries it off | WEAPTWK, Miraheze | **fixed**: walks, steered (Help "Movement to steer"); `WormCollideResponse` = `kWC_FloatAway`: it swallows the first worm it touches, floats up 5 s drifting downwind, pops (40 hp to its catch, `WormDamageMagnitude` 40) and drops it; with no catch it bursts harmlessly at the fuse end |
| Starburst | - | steered flight, 6 clusters | one blast: `WormDamageMagnitude` 100, `NumBomblets` 0; lifetime 30 s | WEAPTWK, Miraheze | blast fixed (35 → 100, see "Blast sizes"); the 6 clusters are kept, unverified (StarburstLogicEntity Detonate 0x588dd0 not traced) **Settled (exe):** Detonate 0x588dd0 spawns nothing (`NumBomblets` 0): **one blast, no clusters** (was 6); then `Worm.Vapourize` (0x5885f0, handler 0x5ac160) kills the active worm, its rider, and the turn ends; same on expiry (0x588d10). LifeTime **30 s** (was 8). The AI no longer picks it. `InitialVelocity` is read only by the serializer |
| Air Strike | top-down map, impact point + axis, 5 missiles | reticle point + worm facing, **6** bombs at **25** each with a 1.5 m radius, **dropped one after another** by an invisible plane 25 m up: one every **20 ticks (333 ms)**, **2.5 m** apart, the run centred on the target (was 5 missiles all at once, one stacked blast sound) | max 25, `Bomber.NumBombs` 6 (`NumStrikeBombs` 0 so the default holds), `BlitzDuration` 2000 ms, `GroundSpeed` 0.15 units/ms = 7.5 m/s, `ExtraHeight` 140 above `Land.MaxHeight`, bombs `IsLowGravity`, `LaunchSfx` BombWhistle | WEAPTWK, exe (BomberLogicEntity 0x54d828: start = target - dir x BlitzDuration x GroundSpeed / 2; AI 0x4a13a0: spacing BlitzDuration x GroundSpeed / NumBombs) | fixed; **bombs now leave the plane with its speed** (`IsAffectedByWind` 0: velocity = dir x GroundSpeed, BomberLogicEntity drop 0x54ddf0; was: straight down at 20 m/s, wind on) and fall under `Gravity.Slow` (`IsLowGravity` 1, read by PayloadLogicEntity 0x5823c2; `Game::STRIKE_FALL` = Gravity.Slow / Gravity = 0.6 of our gravity). The plane flies `Land.MaxHeight` + `Bomber.ExtraHeight` 140 units (7 m) and starts early by GroundSpeed x sqrt(2 h / g) (0x54d460), so the run still lands centred on the target (`Game::strikeStart`, shared by ai.cpp; was 25 m over the target). Each drop emits `GameEvent::Launch` for `LaunchSfx` BombWhistle; bomb blast now W4M: crater 2.9 m, damage reach 3.45 m (was 1.5 / 3 m, see "Blast sizes"); each bomb whistles (weapons/BombWhistle, -14 dB, 3D 0.5-40 m, 6 playbacks) and each cow plays weapons/CowFall (-6 dB, 0.5-25 m, 2 playbacks); that the payload plays LaunchSfx at its own launch is assumed from the field name (exe reader not traced) |
| Bovine Blitz (our "Super Airstrike") | 3 parachuted cows | 8 missiles, no ammo by default; **shown as "Bovine Blitz"** (WEAPTWK `DisplayName` Text.kWeaponSuperAirstrike, en/fr lang; `weaponName()` in ui.cpp for every weapon, the internal name stays) | kWeaponSuperAirstrike: max 80, a steered plane with 14 s run, a bomb every 0.8 s | WEAPTWK, Help, Miraheze (cows vs plane disagree) | **fixed** (data wins over the cows-on-parachutes wiki text): `IsControlledBomber` 1, `NumStrikeBombs` 3, Help "Steer the plane and drop the bombs". The plane enters 20 m before the reticle, 15 m up, banks on the stick for 14 s; each FIRE drops a cow (80 hp, 3 m), at most 3, 0.8 s apart; cows come down at ≤ 5 m/s (the Wiki's parachutes). Stored as an `airstrike` with `fuse` > 0. Rechecked: drops were already sequential (SuperBomber.InitialDelay 500, DelayBetweenBombs 800, TotalBombRunTime 14000). **Models** (bundle Bundl09): the plane is the `SuperAirstrike` chopper (SuperBomberGraphicEntity, no clip in the exe; drawn in its `OpenDoorsSource` frame 0, its rest pose is nose-down), rolled into turns (SuperBomber.BankingAcceleration / MaxBankSpeed, roll scaled by eye); each cow is `Cow.Payload` (WEAPTWK `PayloadGraphicsResourceID`) playing `Hang`, with `Crate.Chute` `Fall` at its `Parachute` node (ParachutePayloadGraphicEntity 0x579e80). `superbomber.glb` / `cow.glb`, `drawBovine()` in main.cpp |
| Fatkins Strike | bounces 3×, shockwaves | donkey-like, 3 smashes; max **75** (was 52) | max 75; FatkinsFood 65 secondary | WEAPTWK, Miraheze (several bounces, each a big crater) | fixed damage |
| Concrete Donkey | pounds down to the water | 6 smashes; max **80** (was 50) | max 80, Donkey.Bounce 0.3 | WEAPTWK, Wiki | **fixed**: `clusters` 0 = smashes on down until it sinks (Wiki, GameFAQs; the data: not detonated by land impact, `SinkDepth` 80). Bounce unmodelled **Settled (exe):** `Donkey.Bounce` 0.3 and `MinBounceSpeed` are bound in Init (0x55379d) but **never read**. The motion is a fixed curve (0x553370): y = apex - 220 units/s⁴ (t - tApex)⁴ (11 m/s⁴, `DONKEY_CURVE`), from rest at release; after each smash it holds 85 ms (5 ticks), then rises back 3.48 m in 0.75 s and smashes again. It stops at **LifeTime 8000 ms** (DetonatesOnExpiry, last blast) or in the water; `SinkDepth` 80 is the sink depth, not a count. Implemented for `clusters` 0 (Fatkins keeps its 3 smashes) |
| Alien Abduction | pick a worm, teleport, −half HP | worms under the reticle lose **half their hp** (new) and are lifted | Abduction.* lift heights; "Abducted" worm states | Wiki, Steam ("cuts in 2 the life"), WORMACTING | fixed half hp; teleporting is spec-wrong (nothing in W4M); "alien worm" afterwards deferred |
| Flood | - | water **+2.15 m** (was 1.5) | Flood.Delta 43 units | WEAPTWK | fixed |

## Guns and melee

| weapon | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Shotgun | 2 shots, 25 each, free aim | 2 shots, 25, can walk between them | 25, 2 shots | WEAPTWK, Help, Miraheze | OK |
| Sniper Rifle | scope, straight, 40, big knock | scope, hitscan, 40 | 40, 1 shot | WEAPTWK, Help | OK |
| Baseball Bat | 30, 360°, big knock | **30** (was 20), along the aim | `WormDamageMagnitude` 30 (`Radius` 20 had been read as the damage) | WEAPTWK | fixed |
| Fire Punch | 30, leap through terrain, launch up | **30** (was 16), leap, launch up | `WormDamageMagnitude` 30 (`Radius` 16 had been read as the damage), Firepunch.Velocity 0.4 | WEAPTWK | fixed |
| Prod | 0, small push | **0** (was 5), small push | `WormDamageMagnitude` 0, `ImpulseMagnitude` 0.12 (`Radius` 5 had been read as the damage) | WEAPTWK | fixed |
| Sword and Shield | 30 + shield | absent | not in W4M; Shield.DamageScale belongs to Armour | data grep | spec-wrong |
| Tail Nail | - | absent | kWeaponNoMoreNails: pins the worm in the ground | Help, Miraheze | **fixed**: melee, 15 hp (`WormDamageMagnitude` 15), the victim sinks 0.35 m and is `nailed`: no walking or jumping, no tools, animals or melee (`CanBeUsedWhenTailNailed`), no knockback; a blast that carves the ground at its feet frees it. Teleport does not (the data forbids it when nailed). **Anim**: the victim plays the worm clip `Nailed` (2.5 s) once, queued by DirtBallLogicEntity 0x5ce320 together with `WXP_Wep_NailTail`, then its usual idle; a nailed worm that is hit plays `NailedHitFront` (hit clip 0x5a9100 picks the `Nailed*` variant). The 0.35 m sink is ours, no W4M depth found |
| Sentry Gun | placed, cone, fires on enemy turns | fires at the active enemy worm within 15 m in sight; burst **25** (was 10), reload **10 s** (was 5) | range 300 units (15 m), reload 10000 ms; Miraheze ≤25 per salvo | WEAPTWK, Miraheze | fixed; cone preview is a UI matter |

## Utilities

| item | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Jetpack | 30 fuel, burns only while thrusting, weapons usable in flight, then retreat | **W4M-aligned** (below); was: any weapon in flight fired with FIRE, JUMP switched it off, 28 m/s² thrust, no height falloff, 0.98 drag | JetpackUtilityLogicEntity (TWEAK `Jetpack.*`, exe below) | exe, TWEAK, LOCAL DefInputMapping, EngFE HelpText.kUtilityJetpack0 | **fixed** |
| Ninja Rope (W4M "Grappling Hook") | anchor, swing, length, drop weapons | same; **on the rope the hand only takes Dynamite, Landmine or Sheep** (W4M 0x565d30, see Jetpack; the parachute too), fired with FIRE (with one in hand the aim axis aims it instead of reeling); max length **22.5 m** (was 25) | Ninja.MaxLength 450 units, NumShots 5; grabs crates, mines and drums | TWEAK, Miraheze | fixed; **5 launches per turn** (`Ninja.NumShots`, one ammo) and **the hook catches crates, mines and drums** (`kRopeModeAttachedToObject`, `FETXT.Tip1` "drag mines, crates and barrels") and **stays hooked** (`Object::hooked`, checksummed): the aim axis reels it in and out like the rope, a taut rope drags it towards the worm (`Ninja.WormMass` 100, objects have no Mass key), jump or the turn end lets go (was: one yank 4/5 of the way). The swing (`stepRope`, and its ai.cpp copy) is now sub-stepped with `substeps()` like a flight. Noted, not changed: `Ninja.LengthenShortenRate` 0.2 (10 m/s if units/ms, unverified; ours 6 m/s) and `Ninja.MinLength` 10 units (0.5 m; ours 1 m). The exe runs the same swing for kRopeModeAttached and kRopeModeAttachedToObject (asserts at 0x5729e0, 0x574480); how it moves the object was not traced, the drag is ours |
| Parachute | unlimited, manual or **auto** on a fatal fall, wind drift | utility crate weight **3** (was 4; Standard Crate 30); manual, plus **auto-opens when held** and falling past a jump's landing speed; **drifts downwind** (≤2 m/s per wind unit) | Help "Will auto activate if selected" | Help, Miraheze | fixed |
| Girder | 3D preview, 8 orientations, steel | **new**: a see-through preview 2 m ahead at eye level, stepped 0.3 m camera-relative (Right > Left > Back > Forward, raise / lower besides), each axis within 15 m of the worm, never below the water or 37.5 m over the land; FIRE welds GirderSmall.xom (a 4 × 4 m deck 1 m thick on two 1 m legs) as ordinary land (`Terrain::steel` marks its voxels, meshed with the theme's material 61; blasts dig it); the turn retreats unless the Wormpot **Multiple Girders** (`WP_MULTI_GIRDER`) is on; GirderCam (dist 16.25 m, pitch 0.6 rad, yaw 0.4 rad/s); at most 45 per match; axis-aligned | GirderKitLogicEntity: spawn 0x55aeb6 (worm + EyeLevelOffset 15 + 40 ahead), step 0x558c30 (6 units), clamp 0x558b20 (`Weapon.Girder.MaxDistance` 300 per axis), validation 0x5590e0 (probe box 30×20×30 edges vs land, 4 spheres r 30 at ±15 vs worms / objects; top above `Water.Level`, under `Land.InitialMaxHeight` + 750), place 0x55a840 (`Land.SpawnPiece` GirderSmall.xom, `Girder.TotalNumberPlaced` < 45), finish 0x55ac30 (retreat unless `GirdersDontEndTurn`); CAMTWK GirderCamera; Bundl09 GirderSmall 4×2×4 voxels profile 2,1,1,2, material 61 (theme files line 428: sound "girder") | exe, data | **fixed**, as W4M: **no rotation** (the yaw / pitch π/8 bits 0x200-0x1000 are never set on PC) and **destructible**: `Land.SpawnPiece` → SpawnLand 0x4778c0 adds a block to the same landscape, whose explosion handler 0x463c90 only skips carving when the whole landscape is indestructible (flag 2 at +0x78, set from `Land.Indestructable` / PERM levels 0x47717d). Unverified: the step cadence (one per 4 updates, assumed 20 ms: 5 ticks), which index of the template is the leg profile, the preview look (GirderKitGraphicEntity not traced), the active worm counting in the worm probes. GirderLarge (scale 2) is dead on PC (bit 0x800) |
| Teleport | free cursor, **ends the turn** | reticle point, turn goes on | no kUtilityTeleport text or exe class: not a W4M player utility (Telepads instead) | data grep, Miraheze | spec-wrong; kept as an extra; turn-end not touched (other agent) |
| Worm Select, Skip Go, Surrender | - | as W4M | Help | Help | OK |
| Armour | reduces damage | **new utility**: the worm takes 25 % of blasts and bullets and **half** the knockback (was 40 %; exe 0x5ae984: impulse x 0.5 when shielded) until the end of the match; poison, falls, melee and the scouser are not reduced; **applied on pickup** like W4M (crate collect 0x5c9800, 47 → `Armour.Collected`; which worm gets it is unverified: ours the one that touches the crate), never in the inventory or the weapon panel | `Armour.ProtectionPercentage` 25; Steam 2874524134 (50 → 12, permanent) | WEAPTWK, Steam | fixed; **icon**: W4M has none in `HUD/Weapons` (the HUD icon switch 0x5d7bb0 stops at Binoculars, later utilities fall to `invalid_weapon.tga`), so the panel shows `HUD.Armour` = Bundl09 `HUD Shield.tga` (`hud/hud_shield`, the shield of HealthText3DEntity 0x5ece90) |
| Binoculars | - | **new**: in first person (ZL) every worm gets the team outline; FIRE on the first enemy worm or land on the eye ray solves **our bazooka** shot (no wind): 11 powers from full down, the lowest clear arc; after 4 s (2.7 s sweep, 1.3 s converge) the HUD power bar and angle arc show it; no ammo used, the turn goes on | BinocularsUtilityLogicEntity: HeadCam, ray 0x54b320 (eye + EyeLevelOffset, radius 10, skips the shooter's alliance), solve 0x54bcc0 (TargetParabola 0x519a40, speeds 0.533 → 0.1 by 0.0433 = Bazooka/Grenade Base/MaxPower, no wind), HUD only (PowerbarMeterEntity 0x5f5da0, AngleMeterEntity 0x5d8c70), no DecrementInventory; Standard Ammo 3, Crate 30, Delay 2 | exe, data | **fixed**; ours solves with our bazooka speed / gravity (W4M's numbers are its own); the HeadCam zoom (0x91f31c, dips to 0.3) is not modelled; Delay 2 is. The brief's "Blimp view" is not W4M (HeadCam) |
| Bubble Trouble | - | **new**: a bubble (radius 2.1 m, centre 1.75 m above its base) set beside the worm, falling until it rests; shots from outside **bounce off** its 2.52 m shell (impact shells burst on it), walkers turn back, sentry bullets stop; a worm inside (1.52 m) ignores every blast centred outside 2.1 m; a blast inside pops it; it lasts **6 turn ends**; the turn retreats | BubbleTroubleLogicEntity 0x54f6e0: spawn 0x550190 (offset -1.931, 9.415, 12.459 units), physics 0x54f160, colliders 0x54f8b1 (shell Radius × 1.2, inner -20), payload contact 0x581dc0 (new contacts only: a shot from inside leaves), walkers 0x593476, sentry 0x56c140, explosion 0x54eff0 (epicentre within Radius, Health 1), worm 0x5a61e0; `Bubble.Lifetime` 6 on GameLogic.Turn.Ended; Standard Crate 50, Ammo 0 | exe, data | **fixed**. Unverified: the hitscan guns (we stop them on the shell like payloads), the turn ending (assumed the normal retreat), worms walking through (assumed). W4M's `BubbleTrouble.Bubble` (Bundl09) exports right-sized as `bubble.glb` (static, radius 2.1 m; the skin was fine, its bones carry a uniform 0.042 scale), but with our model shader its texture draws as an opaque pink cone: W4M's soap-film shading is not reproduced, so a see-through sphere stays |
| Icarus Potion (kUtilityRedbull) | - | **new**: drinking cures poison and heals to the start energy; after a jump, JUMP flaps up at 7.5 m/s (horizontal speed zeroed, the stick steers up to 5 m/s) but only in a 250 ms window every 500 ms (too early restarts the wait); no fall damage while flying; putting it away ends it; the turn goes on; locked for the team's first 2 turns (Standard Delay 2) | RedbullUtilityLogicEntity: drink 0x587600 / 0x587750 (Worm.Antidote, energy to InitialEnergy), flight 0x587390, flap 0x587970 (`Weapon.Redbull.FlapVelocity` 0.15, LOCAL `MinTime` / `TimeLength` 250), WXWorm.AftertouchDelta 0.015 / AftertouchStrength 0.1; Standard Ammo 1, Crate 30, Delay 2 | exe, data | **fixed**. `MinFlapVelocity` 0.3 is never read. Unverified: InitialEnergy = the scheme's energy (Goliath worms differ), the 500 ms drink delay is skipped, the ceiling (W4M SkyBox height: ours the map top), aftertouch reset on landing, the wings' mount point (W4M `RedBullWings`, Bundl09, FlyRedBull: `wings.glb`) |
| Double Damage | - | **new**, crate only: collected, it doubles every blast (damage, both radii and the impulse) and every hit (guns, melee knock) for the rest of the turn, whoever caused it; the Wormpot mode now doubles the radii too | crate collect 0x5c9800 (`SetData("DoubleDamage", 1)`, never in the inventory), ExplosionMessage 0x518d80 (×2 on the 5 floats), DamageImpulseMessage 0x518cbf, reset by DoPostActivity; Standard Crate 30 | exe, data | **fixed**; hidden from the weapon panel (`collected()`) |
| Crate Spy | - | **new**, crate only: the collecting team sees every crate's contents (new ones too) during its turns, for the rest of the match | 0x5c8b20 (TeamDataContainer.IsCrateSpyActive, never reset), CrateGraphicEntity 0x5c5270 (shown on Turn.Started, hidden on Turn.Ended); Standard Crate 30 | exe, data | **fixed**, hidden from the weapon panel (online: W4M shows it on the owner's machine only; ours on every screen) |
| Low Gravity | - | not a utility | LowGravityLogicEntity has no creation site, no enum id, no inventory slot; only the mystery crate 57 (0x5cad2a, `Low.Gravity.OnValue` 0.5, this turn only) and the Wormpot | exe | **not added** (mystery crates are not implemented; the Wormpot Low Gravity already exists) |
| Bridge Kit | - | not added | only a WEAPTWK container and a WeaponInventory field: no enum id, no string in the exe, no logic class | exe, data | **not in W4M** (dead data) |
| Buffalo of Lies | - | not added | BuffaloOfLiesGraphicEntity is only the mystery-crate reveal (0x5ca216); Buffalo.* messages are never sent, its LOCAL keys never read | exe, data | **not in W4M** as an item (cut feature) |

Utility crates: W4M picks the contents with the scheme's per-item `Crate` weights (CreateRandomCrate 0x4fa4b0, weights 0x4f6280, utility roulette 0x4f4cc0; Ammo -1 = weight 0). Standard: Girder 50, NinjaRope 50, Jetpack 50, Bubble Trouble 50, Parachute 30, SelectWorm 30, Redbull 30, Binoculars 30, DoubleDamage 30, CrateShower 30, CrateSpy 30, Armour 30. Our `crate_weight` keeps that ratio (/10) inside the utility pool. The crate split per scheme is now W4M's SchemeData WeaponChance / HealthChance / UtilityChance (Standard 30 / 30 / 20; it had health and utility swapped in every preset); MysteryChance is not modelled. Scheme `Delay` (turns before use): every preset carries its SchemeData delays (`SchemePreset::delays`, all weapons, e.g. Standard Airstrike 5, Banana Bomb 8, Holy 3, Homing 2, Super Sheep 5, Redbull 2, Binoculars 2, Weapon Factory 3); a custom scheme has none (WXD.DefaultSchemeData). They count down for the team whose turn just ended (ActivateNextWorm 0x5b5a5f sends GameLogic.DecrementWeaponDelays, handler 0x4f4df0 on `Inventory<CurrentTeamIndex>.WeaponDelays`). The panel dims a delayed weapon and shows its turns left (FETXT.HTPSubtopic4); the AI skips it. Not aligned: Crate Shower, Teleport (a mystery effect in W4M) and the ninja rope weight (infinite in our Standard) are not aligned.

### Jetpack (W4M JetpackUtilityLogicEntity, verified on the exe)

Controls (W4M):
- **Thrust**: `Input.FireUtilPressed` / `Released` (0x563f00 → 0x562270), bound to `Joypad.Input.Fire` (LOCAL `DefInputMapping`: joypad axis 2, one trigger) and to `Input.Fire` (mouse button 0) in the `UtilityFire` group (0x4e1d50, 0x4de670).
- **Takeoff**: the first press with fuel > 20 ms (0x5622c0) spends the ammo (`GameLogic.DecrementInventory.Id` 37, 0x562315), wields the pack and asks for the `Jetpack` camera.
- **Steering**: the worm turns toward the camera-relative stick (`InputImpulse`, 0x561e40) at 2 × `TurnRotationSpeed` 0.0092 rad per 20 ms (0.92 rad/s). `Input.Jetpack.Left/Right` are stored (+0x5a/+0x5b) but never read.
- **Forward thrust**: when `Input.Jetpack.Forward` is held (joypad `Camera.RotateUp` = D-pad up; keyboard `AimUp`) or the stick's share along the facing is above 0.01 (0x562ac2).
- **Second fire**: `Joypad.Input.Fire.Second` (the other trigger) and `Input.Fire.Second` (DIK 14, Backspace) send `Input.FirePressed` to the weapon in hand.
- **No switch-off button**: the handler subscribes to no jump.

Physics, every 20 ms update (0x562810; velocities in units/ms, 20 units = 1 m):
- **Thrust**: (0, 2 × `ThrustScale` 0.004, 0) = 0.008 per update = **20 m/s²**. With forward, it is tilted forward by `FwdThrustRotation` **0.3 rad**.
- **Altitude**: the thrust is scaled by sin((1 − h / `MaxAltitude` 2000) × π/2), h = height over `Water.Level`. Above 2000 units (100 m) the horizontal thrust is × `OverCeilingThrustMod` 0.05, and the vertical one is 0 while rising, × 0.05 while falling.
- **Super thrust** (0x562180 / 0x562130): when thrusting while falling faster than 0.15 units/ms (7.5 m/s), s += `SuperThrustAccel` 0.3 × min(`SuperThrustMod` 0.2 × (−0.15 − vy), `SuperThrustMax` 1); otherwise s × `SuperThrustReduct` 0.97, 0 under `SuperThrustShutOff` 0.01. The vertical thrust is × (1 + s).
- **Drag**: horizontal velocity × `XZWindResThrust` **0.999** when the stick pushes along the motion (InputImpulse · Velocity > 0 with forward), otherwise × `XZWindResNoThrust` **0.95**. Vertical: none. Gravity is the worm's `Acceleration` (−0.00025). There is no speed cap.
- **Fuel**: `InitFuel` **7500 ms**, −20 per update while thrusting only (0x562924). At ≤ 20 it shuts down (0x562990): the pack is hidden, the camera goes back to `Default` and the worm falls (Ballistic, fall damage applies).
- **HUD fuel**: the number over the worm is (2 × fuel + 500) / 1000 (0x5626e0), i.e. half-seconds: 15 at the start.
- **Contact** (0x562e2c–0x5633e9): land with support under the worm ends the flight (0x563252). A ceiling or wall bounces: v −= 1.8 (v·n) n, otherwise v × −0.8, so the restitution is **0.8**.
- **Water**: the worm's own drown test (0x5ad640: y < `Water.Level` − `Worm.Drown.HeightOffset` 7) runs in every state, Override included.
- **End**: landing, running dry, `GameLogic.Turn.Ended` / `EndTurn` (→ `Jetpack.Kill`), a weapon change that kills it (below), the mystery teleport crate (0x5caaa0) or a telepad (0x5d32cf).
- **Landed**: the entity lives on with its fuel. Fire again takes off without ammo; selecting the jetpack again makes a new entity (full fuel, new ammo).

What the hand can hold in flight (LogicalWeaponManagerService::WeaponSelected 0x565d30, jump table 0x5665f8 / bytes 0x566644 by W4M weapon id):
- **Kept**: only case 0 (PayloadWeaponLogicEntity: ids 5 Dynamite, 8 Landmine, 15 Sheep) keeps a movement utility (rope, jetpack, parachute: mode +0x8d). The payload becomes the secondary weapon (+0x8c) and is dropped with Fire.Second. It is lost if the Wormpot **No Bombing** (`WormPot.NoParachuteDrops` +0x4c, FETXT.WPotHelp.NoBombing "stop the worm's ability to drop weapons whilst using movement utilities") is on.
- **Killed**: every other case calls 0x565650 first, which kills the utility: guns, melee, flood, girder, every other payload (bazooka, grenades, homing, airstrikes... default case 0x56642a), and utilities.
- **After an attack in flight**: the turn moves to its retreat as usual. The jetpack keeps its fuel and flies until Turn.Ended.
- **Flags**: `CanBeFiredWhenWormMoving` / `CanBeUsedWhenTailNailed` are not checked here, and there is no `CanBeUsedWhileJetpacking` field. `WormPot.JetpackEndsTurn` (+0x49) has no reader in the WormPot users.

Camera (JetpackCamMkII 0x52b4c0, CAMTWK `Camera.Jetpack.*`):
- **Placement**: it ignores input. It sits `StickLength` 230 units (11.5 m) behind the worm's yaw, at a pitch that eases by `PitchSpeed` 0.01 a frame toward `DefaultPitch` 0.5 − `PitchScale` 4 × vy.
- **Unused limits**: `MinPitch` / `MaxPitch` −70 / 60 are in the same unit, so they never bind.
- **Water**: the camera stays 5 units above the water.
- **JetpackGroundCam** ("Jetpack Ground") is created (0x522191) but never requested by name.

Ours (sim.cpp `Game::step` jetting, `use()`, `selectable()`; controls.cpp; ui.cpp):
- **Values**: all of the above, per 1/60 s tick (rates ^ (tick / 20 ms)). weapons.json thrust 20 (was 28), fuel 7.5 s.
- **Fire and drop**: FIRE held thrusts whatever the hand holds. In flight the JUMP bit is the second fire: ZL / Backspace drop the dynamite, mine or sheep in hand; B does nothing in flight.
- **Selection**: NEXT_WEAPON and the panel offer only the tool itself and those three while a rope, jetpack or open parachute is out. W4M lets you pick another weapon, which ends the tool. Ours steps through every weapon on the way, which would end it mid-cycle, so the other weapons are dimmed instead.
- **Landing**: no fall damage while it is on (assumed: the landing is the jetpack's, not the worm's Ballistic).
- **CPU**: same inputs, no JUMP switch-off. It stops thrusting at its goal and lands.
- **Checksum**: `jetting`, `jetUsed`, `fuel`, `boost` are in `checksum()` and reset at start and on each turn.

Not done or unverified:
- Which trigger is Fire and which is Fire.Second (axis 2 directions 1 / 0).
- The D-pad-up forward (our stick covers it).
- `InputImpulse · Velocity`: ours uses the facing, since the Input carries only the forward share.
- The takeoff particles `WXP_JetPackTakeOff` and the jet sound ramp (0x562530: × 1.14 a frame up to 1, × 0.96 down, `weapons/JetpackEnd`).
- The camera's `PosUpdateSpeed` 0.995 use (ours eases at 8/s).
- No Bombing is not in our Wormpot list.

## Blast sizes and damage

Source: `xom.py dump Tweak/WEAPTWK.XOM kWeaponX` (field names from the exe schema) and `tweak.py` for `TWEAK.XOM`. 20 units = 1 m; knockback m/s = `ImpulseMagnitude` (units/ms) x 50. Columns: crater = `LandDamageRadius`, reach = `WormDamageRadius`, max = `WormDamageMagnitude`, push / its reach / depth = `ImpulseMagnitude` / `ImpulseRadius` / -`ImpulseOffset`. `weapons.json` keys: `radius`, `reach`, `damage`, `push`, `push_reach`, `push_depth` (`cluster_*` for bomblets), `lift` = payload `Radius` for the donkeys (their blast centre sits that far above the point that hit the ground).

### Blast formula (exe, verified on the disassembly)

- `ExplosionMessage` (ctor 0x518ce0) carries DamageEpicentre, ImpulseEpicentre (= blast + `ImpulseOffset` on y, PayloadLogicEntity 0x57f140), then WormDamageMagnitude, ImpulseMagnitude, WormDamageRadius, LandDamageRadius, ImpulseRadius. `DoubleDamage` doubles all five (radii too; our wormpot only doubles the damage).
- Worm (WXWormLogicEntity 0x5ae4f0): `d = max(0, |worm - DamageEpicentre| - 10 units)`; if `d < reach`, damage = `max * min(1, 1.2 (reach - d) / reach)`, at least 1, truncated; full damage out to reach / 6. Shield: x `Shield.DamageScale` 0.25. Knockback: `e = |worm - ImpulseEpicentre|`; if `e < ImpulseRadius`, velocity += `1.2 ImpulseMagnitude (ImpulseRadius - e) / ImpulseRadius` along worm - ImpulseEpicentre (x 0.85-1.15 random and a few random jitters in the exe, left out here), x 0.5 when shielded (0x5ae984). The epicentre sits 2.25 m under a bazooka blast, so worms beside it fly up.
- Ours before: one `radius` for crater and damage, damage linear from the centre to 2 x radius, push 14 m/s x the same falloff. Now `Game::explode` / `blastDamage` / `blastKick` (sim.cpp), and the AI's `Outcome::blast` calls the same two functions.
- **Landmine** hit by a blast (ParabolicPayloadLogicEntity Explosion 0x577f8f → 0x57f4d0, 0x576c80): only pushed, `ImpulseMagnitude (ImpulseRadius - e) / ImpulseRadius / Mass` (no 1.2, Mass 1), vertical part at least 0.3 of the kick; `DetonatesOnLandImpact` 0, `ArmOnImpact` 0, so it never goes off from a blast; a worm within `ArmingRadius` arms it as before. Ours before: any mine in reach got a 1 s fuse. The AI no longer counts mines as chain targets.

| blast | crater m | reach m | max hp | push m/s | push reach m | depth m | ours before (crater / reach / max) |
|---|---|---|---|---|---|---|---|
| Bazooka | 3 | 4.125 | 50 | 14.5 | 5.5 | 2.25 | 3 / 6 / 50 |
| Grenade | 2.575 | 4.175 | 55 | 14 | 5.5 | 2.25 | 3 / 6 / 55 |
| Cluster Grenade (main) | 1.5 | 2.35 | 15 | 2.5 | 4 | 1.25 | 2 / 4 / 15 |
| Cluster bomb (bomblet) | 1.5 | 2.35 | 15 | 3.75 | 4 | 1 | 1.5 / 3 / 15 |
| Banana Bomb | 3.7 | 4.3 | 50 | 20 | 6.25 | 2.25 | 3 / 6 / 50 |
| Bananette | 4.675 | 5.55 | 60 | 20 | 6.75 | 2.25 | 2.5 / 5 / 60 |
| Holy Hand Grenade | 6.46 | 9.35 | 80 | 22.5 | 7.5 | 3 | 7 / 14 / 80 |
| Homing Missile | 3.45 | 4.9 | 50 | 11 | 5 | 2.25 | 3 / 6 / 50 |
| Dynamite | 3.925 | 5.775 | 75 | 17.5 | 6.5 | 2.25 | 4.5 / 9 / 75 |
| Gas Canister | 0 | 0 | 0 | 0 | 0 | - | 1.5 / 3 / 0 (the cloud poisons) |
| Poison Arrow | 0 | (1.4, contact) | 25 impact | 0 | 0 | - | 1 / 2 / 10 |
| Landmine (object) | 2.625 | 3.73 | 40 | 12.5 | 5 | 2.5 | 3 / 6 / 40 |
| Sheep | 4.675 | 5.84 | 75 | 16 | 6.5 | 2.25 | 3.5 / 7 / 75 |
| Super Sheep | 4.05 | 5.55 | 75 | 16 | 7 | 2.25 | 4.5 / 9 / 75 |
| Starburst | 2.35 | 4.59 | 100 | 37.5 | 7.5 | 2.5 | 3 / 6 / 35 |
| Old Woman | 4.59 | 5.4 | 75 | 15.5 | 5.5 | 2.25 | 4.5 / 9 / 75 |
| Air strike bomb | 2.9 | 3.45 | 25 | 11 | 5.5 | 2.25 | 1.5 / 3 / 25 |
| Bovine Blitz cow (kWeaponSuperAirstrike) | 4.59 | 6.3 | 80 | 15 | 6.5 | 3 | 3 / 6 / 80 |
| Fatkins Strike (lift 1.25) | 5.79 | 7.25 | 75 | 30 | 10 | 1.5 | 4.5 / 9 / 75 |
| Concrete Donkey (lift 3.6) | 5.525 | 8.6 | 80 | 23 | 8.5 | 3 | 3 / 6 / 80 |
| Shotgun pellet (gun) | ours 0.8 (`bCanDamageLand`) | 1.5 | 25 | 6.5 | 2 | 0 | 0.8 / 1.6 / 25 |
| Sniper bullet (gun) | ours 0.4 | 1 | 40 | 5 | 2 | 0 | 0.4 / 0.8 / 40 |
| Oil drum (`OilDrum.*`) | 2.25 | 3.75 | 55 | 20 | 3.75 | **0.45** (9 units under the drum, 0x5d13ce; was assumed 2) | 4 / 8 / 50 |
| Weapon crate (`Crate.*`) | 3 | 3.5 | 60 | 9 | 2.5 | **0.5** (crate radius 10 x Crate.Scale 1, 0x5c5879; was assumed 2) | 3 / 6 / 35 |
| **Dead worm** (`Worm.Death*`) | 1.75 | 3 | 35 | 30 | 2.25 | 0.5 | 0.8 crater, no damage, no push |

Blimp range: `Land.Center` is the middle of the land's bounding box (LandscapeLogicEntity 0x4720c0); ours: floor to `landTop()` (`Game::landCenter`, was the water level, marked assumed).

Not changed, noted: `NumBomblets` is 4 for kWeaponClusterGrenade (we keep 5, Wiki/Help); `FatkinsFood` (65 hp secondary) is not modelled; guns in W4M are not explosions (their reach is a splash around the hit; a struck worm still takes the full damage).

### Death blast (W4M)

- Data: `TWEAK.XOM` `Worm.DeathWormDamageMagnitude` 35, `Worm.DeathWormDamageRadius` 60 (3 m), `Worm.DeathLandDamageRadius` 35 (1.75 m), `Worm.DeathImpulseMagnitude` 0.6 (30 m/s), `Worm.DeathImpulseRadius` 45 (2.25 m).
- Exe: WXWormLogicEntity 0x5a9400, called from both death states (kWPS_DeathThroes 0x5aa080, kWPS_DrownFloat 0x5aa130). It sends a normal `ExplosionMessage` from the worm's position, ImpulseEpicentre 10 units under it, so a drowned worm hurts and throws its neighbours too (the Wiki's "a drowned worm damages nearby worms a bit"). Visual: `WXP_Explosion_Small` (above the water line), plus `WXP_WormDrownPopSplash` when drowned.
- A neighbour at 1 m loses the full 35 hp; at 2.5 m from the centre 14; nothing past 3.5 m. The push is strong but short: about 20 m/s at 0.8 m.
- Ours: `Game::DEATH_BLAST` in `countBoom()` for land and water deaths. Worms it hurts leave the current count and count again in the next group; a worm still waiting for its own blast is not hit (it would move the queue). `Game::countEnd` freezes the queue timing when the group forms (checksummed).

## Engine changes from this audit

- `Object::hooked` (checksummed, cleared in `beginTurn`), `GameEvent::Launch`, `Game::strikeStart` / `landTop`, `STRIKE_FALL` / `STRIKE_EXTRA`.
- `Input::FUSE_UP` and `FUSE_DOWN` are bits 16 and 32. Bit 8 is `HEADING` (`turn` is then the wanted yaw, π·turn/128; it replaced `ABOUT_FACE`). The 4-byte input format is unchanged.
- `Game::fuses` holds the fuse per team and is checksummed.
- `WeaponDef` gains `user_fuse` and `rest_fuse`.
- The d-pad sets the fuse only while a user-fuse weapon is in hand; otherwise it still zooms the camera. On a keyboard the keys are `=` and `-`. The HUD shows "Fuse N s" under the weapon name.
- Timed fuses explode on exactly 60 × n ticks, whatever the float drift.
- `GameEvent::Hallelujah` plays `holy.ogg`. BigBoom no longer plays the choir on every large explosion.
- `Object::dud`, `Worm::nailed` / `armour`, `Projectile::stage` / `prey`, `Game::ropeShots` and `Game::gas` are checksummed and reset in `start()`.
- `WeaponDef` gains `walks` (Super Sheep) and `pins` (Tail Nail); `Kind::Armour` is appended. An `airstrike` with `fuse` > 0 is the controlled bomber.
- `WeaponDef::blast` / `cblast` (reach, push, push_reach, push_depth; -1 = derived from radius) and `lift`, checksummed with the table; `Blast`, `blastOf()`, `Game::blastDamage` / `blastKick`, `DEATH_BLAST`, `MINE_BLAST`, `BARREL_BLAST`, `CRATE_BLAST`; `Game::countEnd` (checksummed).
- WEAPTWK field names now come from the exe's own schema (40-byte records in `.data`): e.g. `WormCollideResponse`, `DetonatesOnFirePress`, `IsControlledBomber`, `NumStrikeBombs`, `CanBeUsedWhenTailNailed`.
- Double Damage doubles before the armour (W4M 0x518da5, then Shield.DamageScale, explosions only): `hurt(w, dmg, blast)`.
- `Kind::Girder`, `Binoculars`, `Bubble`, `Icarus`, `DoubleDamage`, `CrateSpy` (weapons.json kinds `girder`, `binoculars`, `bubble`, `icarus`, `doubledamage`, `cratespy`); `collected(Kind)`: applied from the crate, never in the inventory.
- `Terrain::steel` / `weld()`: girder voxels, ordinary land meshed as material 61; the undo log records a voxel turning steel as `(-1 - voxel, 0)` and `Snapshot::restore` clears it. `tools/w4m-maps` now always exports material 61's textures.
- Girder input (net and replays unchanged, 4 bytes): with the girder in hand the client sends `TARGET`; `walk` / `aim` step the preview forward / right, `aim` with `PITCH` raises it, `turn` yaws the GirderCam (`Game::cursorYaw`).
- `Game::girderOn`, `girder`, `girderFrom`, `girderWait`, `girders`, `bubbles`, `icarus`, `flapAt`, `drift`, `doubleDamage`, `spy`, `scout` are checksummed and reset in `start()`; the per-turn ones in `beginTurn()`.
- Wormpot `WP_MULTI_GIRDER` (bit 18, "Multiple Girders", W4M `GirdersDontEndTurn`), on the last reel.
- `main --utilshot <weapon name> [map]`: that utility in hand, the girder preview stepped ahead and up, then fired (shot_aim.png, shot.png).
- `SchemePreset::delays`, `Game::delays` and `Game::usable()` (checksummed); `collected(Kind)` now covers Armour; the weapon panel hides `collected()` kinds.

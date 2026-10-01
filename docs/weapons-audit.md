# Weapons audit: spec vs ours vs W4M

There are three sources:

- **Spec** is the external list the user pasted. Parts of it are wrong.
- **Ours** is `client/romfs/weapons.json` together with sim/ai/controls, as of this audit.
- **W4M** is the game's own data. When it disagrees with the spec, W4M wins.

W4M sources:

- **WEAPTWK** is `Data/Tweak/WEAPTWK.XOM`, decoded with a throwaway parser. World units are 20 per metre.
  - The first f32 of each payload run is the **max worm damage**. The wiki gives "W4M only: Bazooka 50, Grenade 55", which matches the values.
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
| Poison Arrow | 15 impact, 10 hp/turn, min 1 hp, contaminates neighbours, health crate cures | impact **10** (was 30), poison **10**/turn (was 5), **wind** on (was off), min 1 hp, health crate cures, no contagion | no blast; Worm.Poison.Default 10; Help "wind affected" | TWEAK, Help, Wiki ("10 impact", lasts until cured, down to 1 hp) | fixed; contagion is spec-wrong (no W4M source) |
| Land Mine | - | worm-triggered, scheme fuse; blast **40** (was 45, AI copy too) | max 40, Mine.Min/MaxFuse 1-5 s, 10 % duds | WEAPTWK | fixed; **duds fixed**: when the fuse ends, 10 % of mines fizzle (`Mine.DudProbability` 0.1, `ExpiryFx` `WXP_Wep_MineDudEffect` = a smoke puff) and stay inert for good (`Object::dud`); **trigger fixed**: any worm within **2.25 m** arms it, flying or sliding too (Landmine `ArmingRadius` 45 units, was 1.5 m, so a worm blown past 2 m away missed it); the exe tests an arming sphere against every `kCF_Worm` collider each update (PayloadLogicEntity 0x580756), objects never arm it; a laid mine ignores worms for **2.5 s** (`ArmingCourtesyTime` 2500, `Object::courtesy`); random fuse **1-5 s** (Mine.Min/MaxFuse, was 0-5) |
| Sticky Bomb | sticks to first surface | absent | not in W4M (no kWeapon, no "sticky" anywhere) | data grep, Wiki (W3D / WMD only) | spec-wrong |

## Animals, strikes and specials

| weapon | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Sheep | - | walks, FIRE detonates, 8 s; max **75** (was 60) | max 75, lifetime 30 s | WEAPTWK, Help | fixed damage; 30 s lifetime not applied |
| Super Sheep | reactivate, steered, 20 s max | launched flying at once, steered, **25 s** (was 12); max **75** (was 61) | max 75, lifetime 25000 ms; Help "Fire to launch, transform and detonate" | WEAPTWK, Help | **fixed**: walks like a Sheep (5 s fuse, Wiki), FIRE takes off (25 s flight from then, the turn waits for it), FIRE again detonates; terrain or worm contact detonates (`DetonatesOn*` flags). Starburst still flies at once (Help: "Fire detonates, Movement controls flight") |
| Old Woman | - | walks straight, 6 s; max **75** (was 69) | max 75, lifetime 30 s; Help "Movement to steer, Fire to explode on demand"; steals 1-8 ammo | WEAPTWK, Help, Steam | **fixed**: steered with the stick, FIRE detonates (`DetonatesOnFirePress` 1); `WormCollideResponse` = `kWC_StealInventory`: each enemy worm she bumps loses 1-8 units of one random weapon of its team to the thrower's team (Steam 2874524134; the Wiki says the whole stock, no count in the data) |
| Inflatable Scouser | - | floats with the wind, explodes for 50 | damage run 40/0/21/0: bursts harmlessly; swallows a worm and carries it off | WEAPTWK, Miraheze | **fixed**: walks, steered (Help "Movement to steer"); `WormCollideResponse` = `kWC_FloatAway`: it swallows the first worm it touches, floats up 5 s drifting downwind, pops (40 hp to its catch, `WormDamageMagnitude` 40) and drops it; with no catch it bursts harmlessly at the fuse end |
| Starburst | - | steered flight, 6 clusters | lifetime 30 s, own flying layout | WEAPTWK, Miraheze | not reviewed (layout not decoded) |
| Air Strike | top-down map, impact point + axis, 5 missiles | reticle point + worm facing, **6** bombs at **25** each with a 1.5 m radius, **dropped one after another** by an invisible plane 25 m up: one every **20 ticks (333 ms)**, **2.5 m** apart, the run centred on the target (was 5 missiles all at once, one stacked blast sound) | max 25, `Bomber.NumBombs` 6 (`NumStrikeBombs` 0 so the default holds), `BlitzDuration` 2000 ms, `GroundSpeed` 0.15 units/ms = 7.5 m/s, `ExtraHeight` 140 above `Land.MaxHeight`, bombs `IsLowGravity`, `LaunchSfx` BombWhistle | WEAPTWK, exe (BomberLogicEntity 0x54d828: start = target - dir x BlitzDuration x GroundSpeed / 2; AI 0x4a13a0: spacing BlitzDuration x GroundSpeed / NumBombs) | fixed; bombs fall straight down (W4M releases them early with the plane's speed: same pattern), ai.cpp uses `STRIKE_GAP`; LandDamageRadius 58 / WormDamageRadius 69 units (2.9 / 3.45 m) vs our 1.5 m left as is, no whistle sound |
| Bovine Blitz (our "Super Airstrike") | 3 parachuted cows | 8 missiles, no ammo by default | kWeaponSuperAirstrike: max 80, a steered plane with 14 s run, a bomb every 0.8 s | WEAPTWK, Help, Miraheze (cows vs plane disagree) | **fixed** (data wins over the cows-on-parachutes wiki text): `IsControlledBomber` 1, `NumStrikeBombs` 3, Help "Steer the plane and drop the bombs". The plane enters 20 m before the reticle, 15 m up, banks on the stick for 14 s; each FIRE drops a cow (80 hp, 3 m), at most 3, 0.8 s apart; cows come down at ≤ 5 m/s (the Wiki's parachutes). Stored as an `airstrike` with `fuse` > 0. Rechecked: drops were already sequential (SuperBomber.InitialDelay 500, DelayBetweenBombs 800, TotalBombRunTime 14000) |
| Fatkins Strike | bounces 3×, shockwaves | donkey-like, 3 smashes; max **75** (was 52) | max 75; FatkinsFood 65 secondary | WEAPTWK, Miraheze (several bounces, each a big crater) | fixed damage |
| Concrete Donkey | pounds down to the water | 6 smashes; max **80** (was 50) | max 80, Donkey.Bounce 0.3 | WEAPTWK, Wiki | **fixed**: `clusters` 0 = smashes on down until it sinks (Wiki, GameFAQs; the data: not detonated by land impact, `SinkDepth` 80). Bounce unmodelled |
| Alien Abduction | pick a worm, teleport, −half HP | worms under the reticle lose **half their hp** (new) and are lifted | Abduction.* lift heights; "Abducted" worm states | Wiki, Steam ("cuts in 2 the life"), WORMACTING | fixed half hp; teleporting is spec-wrong (nothing in W4M); "alien worm" afterwards deferred |
| Flood | - | water **+2.15 m** (was 1.5) | Flood.Delta 43 units | WEAPTWK | fixed |

## Guns and melee

| weapon | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Shotgun | 2 shots, 25 each, free aim | 2 shots, 25, can walk between them | 25, 2 shots | WEAPTWK, Help, Miraheze | OK |
| Sniper Rifle | scope, straight, 40, big knock | scope, hitscan, 40 | 40, 1 shot | WEAPTWK, Help | OK |
| Baseball Bat | 30, 360°, big knock | **20**, along the aim | first f32 20 (Miraheze 30) | WEAPTWK | OK (spec-wrong on 30) |
| Fire Punch | 30, leap through terrain, launch up | **16**, leap, launch up | first f32 16 (Miraheze 30), Firepunch.Velocity 0.4 | WEAPTWK | OK (spec-wrong on 30) |
| Prod | 0, small push | 5, small push | first f32 5 | WEAPTWK | OK (spec-wrong on 0) |
| Sword and Shield | 30 + shield | absent | not in W4M; Shield.DamageScale belongs to Armour | data grep | spec-wrong |
| Tail Nail | - | absent | kWeaponNoMoreNails: pins the worm in the ground | Help, Miraheze | **fixed**: melee, 15 hp (`WormDamageMagnitude` 15), the victim sinks 0.35 m and is `nailed`: no walking or jumping, no tools, animals or melee (`CanBeUsedWhenTailNailed`), no knockback; a blast that carves the ground at its feet frees it. Teleport does not (the data forbids it when nailed) |
| Sentry Gun | placed, cone, fires on enemy turns | fires at the active enemy worm within 15 m in sight; burst **25** (was 10), reload **10 s** (was 5) | range 300 units (15 m), reload 10000 ms; Miraheze ≤25 per salvo | WEAPTWK, Miraheze | fixed; cone preview is a UI matter |

## Utilities

| item | spec | ours (now) | W4M data | sources | verdict |
|---|---|---|---|---|---|
| Jetpack | 30 fuel, burns only while thrusting, weapons usable in flight, then retreat | fuel 6 s burnt only on thrust; **can now select and use any weapon in flight** (NEXT_WEAPON, aim on the right stick); the jetpack keeps working through Flying and Retreat, and switches off when Settle starts | Wormpot "stop … dropping weapons whilst using movement utilities" means weapons are allowed by default; InitFuel 7500 (unit unknown) | TWEAK, EngFE, Steam thread, Miraheze | fixed; fuel unit deferred |
| Ninja Rope (W4M "Grappling Hook") | anchor, swing, length, drop weapons | same; **weapons usable on the rope** (with a weapon in hand the aim axis aims it instead of reeling); max length **22.5 m** (was 25) | Ninja.MaxLength 450 units, NumShots 5; grabs crates, mines and drums | TWEAK, Miraheze | fixed; **5 launches per turn** (`Ninja.NumShots`, one ammo) and **the hook catches crates, mines and drums** (`kRopeModeAttachedToObject`, `FETXT.Tip1` "drag mines, crates and barrels") and yanks them 4/5 of the way to the worm; reeling a held object in or out is not modelled |
| Parachute | unlimited, manual or **auto** on a fatal fall, wind drift | manual, plus **auto-opens when held** and falling past a jump's landing speed; **drifts downwind** (≤2 m/s per wind unit) | Help "Will auto activate if selected" | Help, Miraheze | fixed |
| Girder | 3D preview, 8 orientations, steel | absent | kUtilityGirder exists; ends the turn unless the GirdersDontEndTurn scheme flag is set | Help, exe strings | deferred: needs an indestructible voxel material (terrain format, carve, mesh/texture, undo log of replays), a 3D preview and 8 orientations; estimated larger than all the items above together |
| Teleport | free cursor, **ends the turn** | reticle point, turn goes on | no kUtilityTeleport text or exe class: not a W4M player utility (Telepads instead) | data grep, Miraheze | spec-wrong; kept as an extra; turn-end not touched (other agent) |
| Worm Select, Skip Go, Surrender | - | as W4M | Help | Help | OK |
| Armour | reduces damage | **new utility**: the worm takes 25 % of blasts and bullets and 40 % of the knockback until the end of the match; poison, falls, melee and the scouser are not reduced; not in the default inventory, utility crates only | `Armour.ProtectionPercentage` 25; Steam 2874524134 (50 → 12, permanent) | WEAPTWK, Steam | fixed |
| Bridge Kit, Binoculars, Bubble Trouble, Icarus Potion, Buffalo of Lies | - | absent | present in the data (Ultimate Mayhem extras) | Help, WEAPTWK | deferred (missing) |

## Engine changes from this audit

- `Input::FUSE_UP` and `FUSE_DOWN` are bits 16 and 32. Bit 8 is `ABOUT_FACE`. The 4-byte input format is unchanged.
- `Game::fuses` holds the fuse per team and is checksummed.
- `WeaponDef` gains `user_fuse` and `rest_fuse`.
- The d-pad sets the fuse only while a user-fuse weapon is in hand; otherwise it still zooms the camera. On a keyboard the keys are `=` and `-`. The HUD shows "Fuse N s" under the weapon name.
- Timed fuses explode on exactly 60 × n ticks, whatever the float drift.
- `GameEvent::Hallelujah` plays `holy.ogg`. BigBoom no longer plays the choir on every large explosion.
- `Object::dud`, `Worm::nailed` / `armour`, `Projectile::stage` / `prey`, `Game::ropeShots` and `Game::gas` are checksummed and reset in `start()`.
- `WeaponDef` gains `walks` (Super Sheep) and `pins` (Tail Nail); `Kind::Armour` is appended. An `airstrike` with `fuse` > 0 is the controlled bomber.
- WEAPTWK field names now come from the exe's own schema (40-byte records in `.data`): e.g. `WormCollideResponse`, `DetonatesOnFirePress`, `IsControlledBomber`, `NumStrikeBombs`, `CanBeUsedWhenTailNailed`.

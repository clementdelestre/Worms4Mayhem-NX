# W4M weapons: table, logic dispatch, fields

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 4. Weapon table
Confidence tags: **data** = read from WEAPTWK.XOM via xom.py (all 51 weapon containers decode `_exact=true`); **disasm** = schema records / RTTI / vtables / debug strings in the exe; **assumed** = inference.

### 1. Container classes (disasm: `pe.py schema`, `pe.py rtti`)

Inheritance (RTTI): `XContainer < BaseWeaponContainer < {PayloadWeaponPropertiesContainer, GunWeaponPropertiesContainer, MeleeWeaponPropertiesContainer, SentryGunWeaponPropertiesContainer}`;
`Payload < {JumpingPayload, HomingPayload}`; `Jumping < FlyingPayload < StarburstPayload` (Flying derives from **Jumping**, not Payload).
`MineFactoryContainer` derives XContainer directly. XOM type names are truncated to 31 chars (`PayloadWeaponPropertiesContaine`).
Field index is global across the chain: Base 0x00-0x19, subclass continues at 0x1a; Jumping/Homing both 0x86+; Flying 0x8f+ (after Jumping's 0x86-0x8e); Starburst 0xa6.
Serialize order in file: derived class first (from 0x1a), then base 0x00-0x19.

| class | vtable | Serialize fn | fields (idx name type +off) |
|---|---|---|---|
| BaseWeaponContainer | 0x878ef4 | ~0x65b52f | 00 DisplayName str +2c; 01 WeaponGraphicsResourceID str +30; 02 WeaponType enum +34; 03 DefaultPreference f32 +38; 04 CurrentPreference f32 +3c; 05 LaunchDelay u32 +40; 06 PostLaunchDelay u32 +44; 07 FirstPersonOffset vec3 +14; 08 FirstPersonScale vec3 +20; 09 FirstPersonFiringParticleEffect str +48; 0a HoldParticleFX str +4c; 0b DisplayInFirstPerson bool +78; 0c CanBeFiredWhenWormMoving bool +79; 0d RumbleLight u8 +7a; 0e RumbleHeavy u8 +7b; 0f CanBeUsedWhenTailNailed bool +7c; 10 RetreatTimeOverride i32 +50; 11-17 WXAnimDraw/Aim/Fire/Holding/EndFire/Taunt/TargetSelected str +54..+6c; 18 HoldLoopSfx str +70; 19 EquipSfx str +74 |
| PayloadWeaponPropertiesContainer | 0x8790fc | ~0x65c109 | 1a IsAimedWeapon bool +1e0; 1b IsPoweredWeapon +1c8; 1c IsTargetingWeapon +1c9; 1d IsControlledBomber +1ca; 1e IsBomberWeapon +1cb; 1f IsDirectionalWeapon +1cc; 20 IsHoming +1cd; 21 IsLowGravity +1ce; 22 IsLaunchedFromWorm +1cf; 23 HasAdjustableFuse +1d0; 24 HasAdjustableBounce +1d1; 25 HasAdjustableHerd +1d2; 26 IsAffectedByGravity +1d3; 27 IsAffectedByWind +1d6; 28 EndTurnImmediate +1d4; 29 UseParabolicRetical +1d5 (all bool); 2a ColliderFlags u32 +cc; 2b CameraId str[] +d0; 2c PayloadGraphicsResourceID str +d4; 2d Payload2ndGraphicsResourceID str +d8; 2e Scale f32 +dc; 2f Radius f32 +e0; 30-37 AnimTravel/SmallJump/BigJump/Arm str +e4..+f0, AnimSplashdown +8c, AnimSink +f8, AnimIntermediate +fc, AnimImpact +100; 38 DirectionBlend f32 +104; 39 FuseTimerGraphicOffset +108; 3a FuseTimerScale +10c; 3b BasePower +110; 3c MaxPower +114; 3d MinTerminalVelocity +118; 3e MaxTerminalVelocity +11c; 3f LogicalLaunchZOffset +120; 40 LogicalLaunchYOffset +124; 41 OrientationOption u32 +128; 42 SpinSpeed f32 +12c; 43 InterPayloadDelay u32 +130; 44 MinAimAngle +134; 45 MaxAimAngle +138; 46 DetonatesOnLandImpact bool +1d7; 47 DetonatesOnExpiry +1d8; 48 DetonatesOnObjectImpact +1d9; 49 DetonatesOnWormImpact +1da; 4a DetonatesAtRest +1db; 4b DetonatesOnFirePress +1dc; 4c DetonatesWhenCantJump +1e1; 4d DetonateMultiEffect enum +154; 4e WormCollideResponse enum +158; 4f WormDamageMagnitude f32 +15c; 50 ImpulseMagnitude f32 +c0; 51 WormDamageRadius +164; 52 LandDamageRadius +168; 53 ImpulseRadius +16c; 54 ImpulseOffset +170; 55 Mass +174; 56 WormImpactDamage +178; 57 MaxPowerUp u32 +17c; 58-5b Tangential/Parallel Min/Max BounceDamping f32 +180..+18c; 5c SkimsOnWater bool +1df; 5d MinSpeedForSkim +190; 5e MaxAngleForSkim +194; 5f SkimDamping vec3 +80; 60 SinkDepth +198; 61 NumStrikeBombs u32 +19c; 62 NumBomblets u32 +1a0; 63 BombletMaxConeAngle +1a4; 64 BombletMaxSpeed +1a8; 65 BombletMinSpeed +1ac; 66 BombletWeaponName str +1b0; 67 FxLocator str +1b4; 68 ArielFx +1b8; 69 DetonationFx +1bc; 6a DetonationSfx +1c0; 6b ExpiryFx +1c4; 6c SplashFx +90; 6d SplishFx +94; 6e SinkingFx +98; 6f BounceFx +9c; 70 StopFxAtRest bool +1e2; 71 BounceSfx +a0; 72 PreDetonationSfx +a4; 73 ArmSfx1Shot +a8; 74 ArmSfxLoop +ac; 75 LaunchSfx +b0; 76 LoopSfx +b4; 77 BigJumpSfx +b8; 78 WalkSfx +160; 79 TrailBitmap +bc; 7a TrailLocator1 +c4; 7b TrailLocator2 +c8; 7c TrailLength u32 +f4; 7d AttachedMesh str +13c; 7e AttachedMeshScale f32 +140; 7f StartsArmed bool +1dd; 80 ArmOnImpact bool +1de; 81 ArmingCourtesyTime u32 +144; 82 PreDetonationTime u32 +148; 83 ArmingRadius f32 +14c; 84 LifeTime i32 +150; 85 IsFuseDisplayed bool +1e3 |
| JumpingPayloadWeaponPropertiesContainer | 0x87982c | ~0x65e2a0 | 86 SmallJumpHorizontalSpeed f32 +1e4; 87 SmallJumpMinVerticalSpeed +1e8; 88 SmallJumpMaxVerticalSpeed +1ec; 89 BigJumpHorizontalSpeed +1f0; 8a BigJumpMinVerticalSpeed +1f4; 8b BigJumpMaxVerticalSpeed +1f8; 8c MaxDrop +1fc; 8d ReturnProbability +200; 8e MinTimeForSafeJump u32 +204 |
| HomingPayloadWeaponPropertiesContainer | 0x879914 | ~0x65e5a0 | 86 OrientationProportion f32 +1e4; 87-89 Stage1/2/3Duration u32 +1e8/+1ec/+1f0; 8a MaxHomingSpeed +1f4; 8b HomingAcceleration +1f8; 8c AvoidsLand bool +20c; 8d VerticalLandAvoidanceDistance +1fc; 8e ForwardLandAvoidanceDistance +200; 8f VerticalLandAvoidanceForce +204; 90 ForwardLandAvoidanceForce +208 |
| FlyingPayloadWeaponPropertiesContainer | 0x879a20 | ~0x65ea11 | (Jumping fields) + 8f MaxPitchSpeed +208; 90 PitchAcceleration +20c; 91 Inertia +210; 92 MaxYawSpeed +214; 93 YawAcceleration +218; 94 MaxRollSpeed +21c; 95 RollAcceleration +220; 96 FlyingSpeed +224; 97 MaxWorldYawSpeed +228; 98 BlendTowardsHorizontal +22c; 99 BlendTowardsVertical +230; 9a MaxAutoRollSpeed +234; 9b AutoRollAcceleration +238; 9c AutoRollDelay u32 +23c; 9d YawAnimSpeed +240; 9e RollAnimSpeed +244; 9f AnimYaw str +248; a0 AnimRoll +24c; a1 FlyingGraphicsResourceID +250; a2 FlyingLaunchSfx +254; a3 FlyingLoopSfx +258; a4 AnimFly +25c; a5 AnimFall +260 |
| StarburstPayloadWeaponPropertiesContainer | 0x879bb8 | ~0x65f1a7 | a6 InitialVelocity vec3 +264 (class exists; WEAPTWK stores kWeaponStarburst as plain Flying, data) |
| GunWeaponPropertiesContainer | 0x879e34 | ~0x660098 | 1a IsAimedWeapon bool +110; 1b IsAffectedByGravity +108; 1c IsAffectedByWind +109; 1d bCanDamageLand +10a; 1e bCanMoveBetweenShots +10b; 1f ImpulseIsNormal +10c; 20 DamageIsPercentage +10d; 21 LaserEffect +10f; 22 Sniper +10e; 23 NumberOfBullets u32 +b4; 24 DischargeTime u32 +b8; 25 Range u32 +bc; 26 WaitForSoundDelay u32 +c0; 27 Accuracy f32 +c4; 28 WormDamageMagnitude +c8; 29 WormPoisonMagnitude +cc; 2a LandDamageMagnitude +d0; 2b ImpulseMagnitude +d4; 2c BulletRadius +d8; 2d MinAimAngle +98; 2e MaxAimAngle +e0; 2f WormDamageRadius +e4; 30 LandDamageRadius +e8; 31 ImpulseRadius +ec; 32 LogicalLaunchYOffset +f0; 33 DischargeFX str +f4; 34 DischargeEndFX +f8; 35 SecondaryDischargeFX +fc; 36 SecondaryDischargeFXLocator +100; 37 DischargeSoundFX +104; 38 DischargeEndSoundFX +9c; 39 WormCollisionFX +a0; 3a LandCollisionFX +a4; 3b WaterCollisionFX +a8; 3c LogicalPositionOffset vec3 +80; 3d ImpulseDirection vec3 +8c; 3e DischargeFXZOffset f32 +ac; 3f KickSize f32 +b0; 40 KickFrequency u32 +dc |
| MeleeWeaponPropertiesContainer | 0x87a038 | ~0x660d8e | 1a IsAimedWeapon bool +d8; 1b DamageIsPercentage +d9; 1c WormIsWeapon +da; 1d InstantKill +db; 1e AccuracyMeter +dc; 1f MeleeType enum +98; 20 Radius f32 +9c; 21 MinAimAngle +a0; 22 MaxAimAngle +a4; 23 DischargeFX str +a8; 24 DischargeSoundFX +ac; 25 WormCollisionFX +b0; 26 LandCollisionFX +b4; 27 WXAnimWindup +b8; 28 LogicalPositionOffset vec3 +80; 29 ImpulseDirection vec3 +8c; 2a LogicalLaunchYOffset f32 +bc; 2b WormDamageMagnitude +c0; 2c LandDamageMagnitude +c4; 2d ImpulseMagnitude +c8; 2e WormDamageRadius +cc; 2f LandDamageRadius +d0; 30 ImpulseRadius +d4 |
| SentryGunWeaponPropertiesContainer | 0x879be4 | ~0x65f374 | 1a ActivatedFx +80; 1b ReloadFx +84; 1c PreExplosionFx +88; 1d DamageFx +8c; 1e ExplosionFx +90; 1f FireFx +94; 20 SplishFx +98; 21 SplashFx +9c; 22 SinkingFx +a0; 23 ReloadSfx +a4; 24 ExplosionSfx +a8; 25 FireSfx +ac; 26 SplashSfx +b0 (str); 27 ShotImpulseMagnitude f32 +b4; 28 ShotImpulseRadius +b8; 29 ShotLandDamageMagnitude +bc; 2a ShotLandDamageRadius +c0; 2b ShotWormDamageMagnitude +c4; 2c ShotWormDamageRadius +c8; 2d WeaponDamageRadius +cc; 2e WeaponDamageMagnitude +d0; 2f DeathWormDamageMagnitude +d4; 30 DeathWormDamageRadius +d8; 31 DeathLandDamageRadius +dc; 32 DeathImpulseMagnitude +e0; 33 DeathImpulseRadius +e4; 34 MaxWeaponTemp +e8; 35 TempDelta +ec; 36 WeaponReloadTime u32 +f0; 37 MinWeaponRange +f4; 38 MaxWeaponRange +f8; 39 LogicalLaunchZOffset +fc; 3a CollisionRadius +100; 3b TurretRotationalVelocity +104; 3c WeaponHealth +108 |
| MineFactoryContainer | 0x878ed8 | ~0x662d66 | 00 NumMineActivation u8 +2c; 01 NumTurnsInactive u8 +2d; 02 SafeRadiusPadding f32 +14; 03-05 MineVelocityX/Y/Z f32 (flag 0x20 "Obsolete", struct offset 0: absent from WEAPTWK, 26 bytes [data]); 06 DamageMagnitude f32 +18; 07 ImpulseMagnitude +1c; 08 WormDamageRadius +20; 09 LandDamageRadius +24; 0a ImpulseRadius +28 |

Related (not in WEAPTWK): `WeaponInventory` (42 u8/i8 per-weapon fields, schema order Bazooka..Binoculars), `WeaponDelays` (43), `SchemeData` (91), `WeaponFactoryCollective{Weapons ref[]}`, `WeaponFactory{Cost,AirstrikeCost,LanchedCost,ThrownCost}Container`. (disasm)

### 2. Weapon ids and name -> container mapping

- WEAPTWK.XOM layout (data): 92 scalar `X{Int,Uint,String,Float,Vector,Color}ResourceDetails` (`Name`="Group.Key", `Value`), 51 `XContainerResourceDetails` (`Name`="kWeaponX", `Value`=ref to the weapon container of the same name, Flags=80), one `XDataBank` (index 146) listing all resources, then 51 containers (147-197). Lookup is by the string name (`kWeaponBazooka`). (data)
- Name table at .data **0x90c920** (49 ptrs, index = enum value; disasm): 0 kWeaponOneBeforeFirst, 1 Bazooka, 2 Grenade, 3 ClusterGrenade, 4 Airstrike, 5 Dynamite, 6 HolyHandGrenade, 7 BananaBomb, 8 Landmine, 9 Shotgun, 10 BaseballBat, 11 Prod, 12 FirePunch, 13 HomingMissile, 14 Flood, 15 Sheep, 16 GasCanister, 17 OldWoman, 18 ConcreteDonkey, 19 SuperSheep, 20 Starburst, 21 FactoryWeapon, 22 AlienAbduction, 23 Fatkins, 24 Scouser, 25 NoMoreNails, 26 PoisonArrow, 27 SentryGun, 28 SniperRifle, 29 SuperAirstrike, 30 ClusterBomb, 31 Bananette, 32 kWeaponOneAfterLast, 33 kUtilityOneBeforeFirst, 34 Girder, 35 NinjaRope, 36 Parachute, 37 Jetpack, 38 SkipGo, 39 Surrender, 40 ChangeWorm, 41 Redbull, 42 BubbleTrouble, 43 Binoculars, 44 DoubleDamage, 45 CrateShower, 46 CrateSpy, 47 Armour, 48 kUtilityOneAfterLast. Referenced from 0x494243, 0x494294, 0x4958c7, 0x49c5cc.., 0x4acc20 (+4: 0x4fec6b, 0x5c6980). Whether the numeric enum is exactly this index or index-1 per range: assumed index.
- Not in that table but have WEAPTWK containers: kUtilityBridgeKit, kUtilityTeleport, kWeaponFactoryCluster/Homing, kWeaponFatkinsFood, kWeaponLandmineBomblet/Cluster, kWeaponSentryGunPayload, kMineFactoryData (sub-payloads/variants, looked up by name, e.g. via BombletWeaponName). (data)
- Scheme/inventory enum order (WeaponInventory schema, disasm) differs: Bazooka, Grenade, ClusterGrenade, Airstrike, Dynamite, HHG, BananaBomb, Landmine, Shotgun, BaseballBat, Prod, FirePunch, HomingMissile, Flood, Sheep, GasCanister, OldWoman, ConcreteDonkey, SuperSheep, Girder, BridgeKit, NinjaRope, Parachute, [LowGravity in WeaponDelays only], Teleport, Jetpack, SkipGo, Surrender, ChangeWorm, Redbull, WeaponFactoryWeapon, Starburst, AlienAbduction, Fatkins, Scouser, NoMoreNails, Pipe, PoisonArrow, SentryGun, SniperRifle, SuperAirstrike, BubbleTrouble, Binoculars.
- Global scalars (data, selection): Gravity -0.00025, Gravity.Slow -0.00015, Wind.MaxSpeed 8.5e-5, Explosion.ImpulseOffset -40, Water.ExpiryDepth -200, Payload.SinkSpeed 0.08-0.1, Bounce.MinSpeed 0.03, Mine.MinFuse 1000 / MaxFuse 5000 / DudProbability 0.1 / MaxInPlay 32, Armour.ProtectionPercentage 25, Shield.DamageScale 0.25, Bomber.NumBombs 6 / GroundSpeed 0.15 / ExtraHeight 140, Airstrike.MaxDistance 1500, SuperBomber.* (ForwardSpeed 2.75, TotalBombRunTime 14000, DelayBetweenBombs 800), Donkey.* (Gravity -0.0005, Bounce 0.3, MinHeight 1500), Abduction.*, Flood.FloodDuration 3000 / Delta 43, Weapon.Firepunch.Velocity 0.4, Weapon.Melee.AccuracyBarSpeed 0.12, Weapon.Redbull.FlapVelocity 0.15, SentryGun.MaxWeaponRange 300 / ReloadTime 10000, MysteryDamage 25, Worm.EyeLevelOffset 15, TwkEdVer.WEAPTWK 177.

### 3. Logic entity classes (disasm: RTTI, vtables, debug strings)

Hierarchy: `BaseTask < LogicEntity (vt 0x8850b0)`;
`LogicEntity < PayloadLogicEntity (0x85c194) < {ParabolicPayloadLogicEntity (0x85b504), HomingPayloadLogicEntity (0x859e7c), FlyingPayloadLogicEntity (0x859424), DonkeyLogicEntity (0x858b6c), WalkingPayloadLogicEntity (0x85d6dc), ParachutePayloadLogicEntity (0x85be14)}`;
`Parabolic < {JumpingPayloadLogicEntity (0x85a364), FatkinsStrikePayloadLogicEntity (0x858f1c)}`; `Flying < StarburstLogicEntity (0x85ce04)`.
Weapon side: `LogicEntity < BaseWeaponLogicEntity (0x857afc) < {FloodWeaponLogicEntity, GirderKitLogicEntity, NewSentrygunWeaponLogicEntity, RedbullUtilityLogicEntity}`; directly on LogicEntity: `PayloadWeaponLogicEntity (0x85c6bc)`, `GunWeaponLogicEntity (0x8599fc)`, `MeleeWeaponLogicEntity (0x85a82c)`, `PoweredWeaponLogicEntity`, `AimedWeaponLogicEntity`, `Adjustable{Fuse,Bounce,Herd}WeaponLogicEntity`, `BomberLogicEntity`, `SuperBomberLogicEntity`, `AlienAbduction{,Launcher}LogicEntity`, `FloodLogicEntity`, `NewSentryGunLogicEntity`, `MineFactoryLogicEntity`, `WeaponFactoryLogicEntity`, `NinjaRope/Jetpack/BubbleTrouble/Parachute ...LogicEntity`. Graphic twins: `PayloadGraphicEntity < {Flying,Parachute,FatkinsStrike}PayloadGraphicEntity`, `GunWeaponGraphicEntity`, `AimedWeaponGraphicEntity`, cursor entities.
Note: ClusterGeneratorLogicEntity exists (strings "ClusterGeneratorLogicEntity::Setup", fn 0x551510) spawning bomblets.

PayloadLogicEntity vtable slots (0x85c194; slot: fn - meaning, evidence):

| slot | Payload fn | meaning | overridden by |
|---|---|---|---|
| 2/3 | 0x581d30 / 0x57dc30 | dtor / class (refcount asserts) | all |
| 6 | 0x57fae0 | init/setup, reads DetonatesOnExpiry | Parabolic 0x576fc0, Walk 0x593ee0, Fatkins, Chute |
| 7 | 0x582860 | HandleMessage (slot 7 of every LogicEntity, §2 "Common vtable layout") [disasm] | all |
| 18 | 0x57ea40 | physics step (assert m_vAcceleration.y<=0) | Walk, Starburst |
| 19 | 0x580830 | bounce/skim response (asserts fMaxPitch, vSkimDamping) | Starburst, Donkey |
| **20** | **0x580f10** | **Detonate** ("PayloadLogicEntity::Detonate pos=", reads DetonationSfx, DetonateMultiEffect, NumBomblets) | Walk 0x592830, Starburst 0x588dd0 (then `Worm.Vapourize` 0x5885f0 and Timer.EndTurn 0x5889a0; disasm: rider vapourized, user unsure 2026-10-04; blast FX WXP_StarburstExplosion [data]; worm clips FireStarburst / FlyStarburst, observed ride) |
| 21 | 0x581740 | removal without a blast: 0x57fcc0, `Payload.Disarm`, `NinjaRope.Kill` when hooked, self-delete 0x68b927 [disasm] | Starburst |
| 24 | 0x581a60 | fire-press / expiration check (reads DetonatesOnFirePress, assert m_tTimeOfExpiration) | Flying 0x558020, Starburst |
| 26/27 | 0x57e0a0 / 0x61ff60 | 26: one tick of Velocity += Acceleration × 20 ms (asserts it is not rising while accelerating up); 27: empty in the base (`ret 4`), the per-tick hook Homing/Flying/Starburst/Donkey override [disasm] | |
| **28** | **0x57fc00** | **collision dispatch** (reads DetonatesOnLand/Object/WormImpact) | Donkey 0x553970 |
| 15/16 | 0x57e500 / 0x580200 | ninja-rope attach/detach (asserts m_tRopeTaskID) | Parabolic 0x575440 |
Non-virtual: PayloadLogicEntity::Arm 0x57ec20; CheckForGoingAwayFromTarget 0x57e130; Parabolic CheckWhenExpires 0x575020, FindFirstEvent 0x576580 (analytic trajectory event search), HandlePayloadEvent(MsgExpire) 0x577980; HomingPayload Initialize 0x560bf0; Walking StealInventory (Scouser); Melee Initialize 0x568860; PoweredWeapon Initialize 0x586bb0.
BaseWeaponLogicEntity: slot 11 EndFireWeapon 0x54a0e0, slot 12 BeginFireWeapon 0x54a020, SetWeapon 0x54a200 (debug strings). LogicalWeaponManagerService::WeaponSelected 0x565d30. AI fire: "AIActionFireWeapon::Update sending c_MsgFireReleased" (fire = message c_MsgFireReleased).
Entity constructors (vtable writers): Payload 0x57e660, Parabolic 0x5754d0, Jumping 0x5644e0, Walking 0x591e30, Homing 0x560820, Flying 0x557060, Starburst 0x588a20, Donkey 0x553000, Fatkins 0x554a80, Parachute 0x57a3d0, PayloadWeapon 0x582bb0, GunWeapon 0x55c960, MeleeWeapon 0x567490, BaseWeapon 0x549cf0. Creators registered by static init (refs in 0x7e5xxx-0x7eaxxx), i.e. instantiated by class name through a task/class factory.

### 4. Per-weapon table
Container class/camera/damage: data. Payload logic class: **traced**, chosen by weapon id then by container *name*, not by container class (see §13). Corrections to the earlier assumption: SuperSheep launches as a Jumping payload and becomes Flying on fire-press; StealInventory (value 1) is OldWoman, FloatAway (2) is Scouser; AdjustableBounceWeaponLogicEntity is never created.
WeaponType enum values seen: 0 sentry payload, 1 utility, 2 aimed launcher/gun, 3 homing, 4 thrown, 5 placed/melee, 6 placed (landmine/flood/sentry), 8 animal, 9 walker, 10 strike. Names: WeaponTypeEnum 0x90ca44 (§13 "WEAPTWK enum fields"): 0 kNoType, 1 kUtility, 2 kProjectile, 3 kTargetted, 4 kThrown, 5 kMelee, 6 kEnvironment, 7 kHitscan, 8 kAnimal, 9 kControlled, 10 kStrike, 11 kMovement [disasm]; the logic reads it only at 0x5973e6.
Units: damage = HP; radii = world units; LifeTime ms (-1 = none, 0 = n/a); Impulse unitless.

| id | cls | exact | WeaponType | CameraId | WormDmg | WormRad | LandRad | Impulse | LifeTime | Bomblets | BombletWeapon | Bullets | Payload gfx | DisplayName |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| kUtilityArmour | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityArmour |
| kUtilityBinoculars | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityBinoculars |
| kUtilityBridgeKit | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityBridgeKit |
| kUtilityBubbleTrouble | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityBubbleTrouble |
| kUtilityChangeWorm | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityChangeWorm |
| kUtilityGirder | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityGirder |
| kUtilityJetpack | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityJetpack |
| kUtilityNinjaRope | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityNinjaRope |
| kUtilityParachute | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityParachute |
| kUtilityRedbull | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityRedbull |
| kUtilitySkipGo | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilitySkipGo |
| kUtilitySurrender | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilitySurrender |
| kUtilityTeleport | BaseWeaponCont | True | 1 |  |  |  |  |  |  |  |  |  |  | Text.kUtilityTeleport |
| kWeaponAlienAbduction | BaseWeaponCont | True | 10 |  |  |  |  |  |  |  |  |  |  | Text.kWeaponAlienAbduction |
| kWeaponFlood | BaseWeaponCont | True | 6 |  |  |  |  |  |  |  |  |  |  | Text.kWeaponFlood |
| kWeaponAirstrike | PayloadWeaponP | True | 10 |  | 25 | 69 | 58 | 0.22 | -1 | 0 |  |  | Airstrike.Payload | Text.kWeaponAirstrike |
| kWeaponBananaBomb | PayloadWeaponP | True | 4 | PayloadTrackCamera | 50 | 86 | 74 | 0.4 | 6000 | 5 | kWeaponBananette |  | BananaBomb | Text.kWeaponBananaBomb |
| kWeaponBananette | PayloadWeaponP | True | 4 | PayloadTrackCamera | 60 | 111 | 93.5 | 0.4 | -1 | 0 |  |  | BananaBomb |  |
| kWeaponBazooka | PayloadWeaponP | True | 2 | PayloadTrackCamera | 50 | 82.5 | 60 | 0.29 | -1 | 0 |  |  | Bazooka.Payload | Text.kWeaponBazooka |
| kWeaponClusterBomb | PayloadWeaponP | True | 4 | PayloadTrackCamera | 15 | 47 | 30 | 0.075 | -1 | 0 |  |  | ClusterBomb |  |
| kWeaponClusterGrenade | PayloadWeaponP | True | 4 | PayloadTrackCamera | 15 | 47 | 30 | 0.05 | 6000 | 4 | kWeaponClusterBomb |  | ClusterGrenade | Text.kWeaponClusterGrenade |
| kWeaponConcreteDonkey | PayloadWeaponP | True | 10 | DonkeyTrackCamera | 80 | 172 | 110.5 | 0.46 | 8000 | 0 |  |  | Donkey | Text.kWeaponConcreteDonkey |
| kWeaponDynamite | PayloadWeaponP | True | 5 | PayloadTrackCamera | 75 | 115.5 | 78.5 | 0.35 | 7000 | 0 |  |  | Dynamite | Text.kWeaponDynamite |
| kWeaponFactoryCluster | PayloadWeaponP | True | 4 | PayloadTrackCamera | 15 | 49 | 30 | 0.05 | -1 | 0 |  |  | ClusterBomb |  |
| kWeaponFactoryWeapon | PayloadWeaponP | True | 2 | PayloadTrackCamera | 55 | 97.5 | 69 | 0.3 | -1 | 0 |  |  | Bazooka.Payload | Text.kWeaponBazooka |
| kWeaponFatkins | PayloadWeaponP | True | 10 | FatkinsTrackCamera | 75 | 145 | 115.8 | 0.6 | -1 | 0 |  |  | Fatkins.Fatboy | Text.kWeaponFatkins |
| kWeaponFatkinsFood | PayloadWeaponP | True | 10 |  | 65 | 115.8 | 34.7 | 0.35 | -1 | 0 |  |  | Fatkins.Food | Text.kWeaponFatkins |
| kWeaponGasCanister | PayloadWeaponP | True | 4 | PayloadTrackCamera | 0 | 0 | 0 | 0 | 6000 | 0 |  |  | GasCanister | Text.kWeaponGasCanister |
| kWeaponGrenade | PayloadWeaponP | True | 4 | PayloadTrackCamera | 55 | 83.5 | 51.5 | 0.28 | 6000 | 0 |  |  | Grenade.Payload | Text.kWeaponGrenade |
| kWeaponHolyHandGrenade | PayloadWeaponP | True | 4 | PayloadTrackCamera | 80 | 187 | 129.2 | 0.45 | 0 | 0 |  |  | HolyHandGrenade | Text.kWeaponHolyHandGrenade |
| kWeaponLandmine | PayloadWeaponP | True | 6 |  | 40 | 74.6 | 52.5 | 0.25 | 5000 | 0 |  |  | Landmine | Text.kWeaponLandmine |
| kWeaponLandmineBomblet | PayloadWeaponP | True | 4 |  | 10 | 35 | 30 | 0.05 | -1 | 0 |  |  | ClusterBomb |  |
| kWeaponLandmineCluster | PayloadWeaponP | True | 6 | PayloadTrackCamera | 25 | 58 | 40.5 | 0.2 | 6000 | 5 | kWeaponLandmineBomblet |  | Landmine | Text.kWeaponLandmine |
| kWeaponOldWoman | PayloadWeaponP | True | 9 | OldWomanChaseCamera | 75 | 108 | 91.8 | 0.31 | 30000 | 0 |  |  | Oldwoman | Text.kWeaponOldWoman |
| kWeaponPoisonArrow | PayloadWeaponP | True | 2 | PayloadTrackCamera | 0 | 0 | 0 | 0 | 0 | 0 |  |  | Arrow | Text.kWeaponPoisonArrow |
| kWeaponScouser | PayloadWeaponP | True | 9 | ScouserChaseCamera | 40 | 21 | 0 | 0 | 30000 | 0 |  |  | Scouser | Text.kWeaponScouser |
| kWeaponSentryGunPayload | PayloadWeaponP | True | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 |  |  | Bazooka.Payload |  |
| kWeaponSuperAirstrike | PayloadWeaponP | True | 10 |  | 80 | 126 | 91.8 | 0.3 | -1 | 0 |  |  | Cow.Payload | Text.kWeaponSuperAirstrike |
| kWeaponSheep | JumpingPayload | True | 8 | SheepChaseCamera | 75 | 116.8 | 93.5 | 0.32 | 30000 | 0 |  |  | Sheep | Text.kWeaponSheep |
| kWeaponFactoryHoming | HomingPayloadW | True | 3 | HomingMissileFlyCamera | 55 | 97 | 69 | 0.35 | 10000 | 0 |  |  | HomingMissile | Text.kWeaponHomingMissile |
| kWeaponHomingMissile | HomingPayloadW | True | 3 | HomingMissileFlyCamera | 50 | 98 | 69 | 0.22 | 10000 | 0 |  |  | HomingMissile.Payload | Text.kWeaponHomingMissile |
| kWeaponStarburst | FlyingPayloadW | True | 8 |  | 100 | 91.8 | 47 | 0.75 | 30000 | 0 |  |  | Sheep | Text.kWeaponStarburst |
| kWeaponSuperSheep | FlyingPayloadW | True | 8 | SheepChaseCamera | 75 | 111 | 81 | 0.32 | 25000 | 0 |  |  | Sheep | Text.kWeaponSuperSheep |
| kWeaponSentryGun | SentryGunWeapo | True | 6 |  | 5 | 30 | 0.1 | 0.04 |  |  |  |  |  | Text.kWeaponSentryGun |
| kWeaponShotgun | GunWeaponPrope | True | 2 |  | 25 | 30 | 0 | 0.13 |  |  |  | 2 |  | Text.kWeaponShotgun |
| kWeaponSniperRifle | GunWeaponPrope | True | 2 |  | 40 | 20 | 0 | 0.1 |  |  |  | 1 |  | Text.kWeaponSniperRifle |
| kWeaponBaseballBat | MeleeWeaponPro | True | 5 |  | 30 | 0 | 0 | 0.25 |  |  |  |  |  | Text.kWeaponBaseballBat |
| kWeaponFirePunch | MeleeWeaponPro | True | 5 |  | 30 | 0 | 0 | 0.22 |  |  |  |  |  | Text.kWeaponFirePunch |
| kWeaponNoMoreNails | MeleeWeaponPro | True | 5 |  | 15 | 0 | 0 | 0 |  |  |  |  |  | Text.kWeaponNoMoreNails |
| kWeaponProd | MeleeWeaponPro | True | 5 |  | 0 | 0 | 0 | 0.12 |  |  |  |  |  | Text.kWeaponProd |
| kMineFactoryData | MineFactoryCon | True |  |  | 100 | 100 | 100 | 0.6 |  |  |  |  |  |  |

## 13. Weapons: logic class dispatch and enum fields (extends §4)

### Weapon -> logic class dispatch (traced)

Tags: **data** = WEAPTWK.XOM or tables stored in the exe; **disasm** = traced code; **assumed** = inference.
"HM" = HandleMessage (vtable slot 7, `vt+0x1c`). The engine is message-driven: id 0x40 (task start) runs the class init/subscribe function, and later work happens in HM on subscribed or timed messages. For payloads, slot 18 is the motion step, slot 20 Detonate, slot 24 fire-press and slot 28 collision.

#### Object creation primitive (disasm)
- `0x639b83(classDesc)` = XOM CreateObject. It gets the XOMMO singleton (`0x639b1d`) and calls its `vtbl+0x50`.
- `classDesc` (.rdata) layout: 16-byte GUID, `+0x10` class name ptr, `+0x14` (size<<16), `+0x18` self ptr.
- To find every creation site of a class, find its descriptor (the dword at `+0x18` equals its own VA), then find `push desc; call 0x639b83`. `0x585330` copies the GUID to the stack first, so its sites appear as `mov reg,[desc]`.

#### Stage 1: weapon id -> weapon logic entity (disasm)
- **`LogicalWeaponManagerService::WeaponSelected` 0x565d30.** It reads the weapon id from `WormData+0xf4`, where id = WeaponNameEnum index (table 0x90c920). The switch is at **0x565ecc**: `id-5`, `ja` -> default, then byte table **0x566644** and jump table **0x5665f8** (63 cases).
- Each case calls 0x565650 (drops the previous weapon logic), creates the class and stores it in a manager slot (`+0x24` payload weapon, `+0x28` gun, `+0x2c` melee, `+0x30`..`+0x68` utilities). It then calls `0x55c830(name)` or `BaseWeaponLogicEntity::SetWeapon 0x54a200(name)`, and attaches the child task (0x4711a0 or 0x68dde8).

| weapon ids (enum value) | case VA | logic class | vtable | HM |
|---|---|---|---|---|
| Shotgun 9, SniperRifle 28 | 0x565f09 | GunWeaponLogicEntity | 0x8599fc | 0x55db30 |
| BaseballBat 10, Prod 11, FirePunch 12, NoMoreNails 25 | 0x565f52 | MeleeWeaponLogicEntity | 0x85a82c | 0x569930 |
| Flood 14 | 0x566157 | FloodWeaponLogicEntity | 0x859164 | 0x556010 |
| WeaponFactoryWeapon 21 | 0x56625c | WeaponFactoryLogicEntity (its start 0x599f50 creates a PayloadWeaponLogicEntity) | 0x85dab0 | 0x59a060 |
| AlienAbduction 22 | 0x566275 | AlienAbductionLauncherLogicEntity (creates AlienAbductionLogicEntity at 0x546b87) | 0x857574 | 0x546b20 |
| SentryGun 27 | 0x56628e | NewSentrygunWeaponLogicEntity (creates NewSentryGunLogicEntity at 0x56e870) | 0x85ae6c | 0x56eb40 |
| Girder 34 | 0x566192 | GirderKitLogicEntity | 0x8596ac | 0x55bac0 |
| NinjaRope 35 | 0x56603c | NinjaRopeUtilityLogicEntity | 0x85b07c | 0x574730 |
| Parachute 36 | 0x5660a5 | ParachuteLogicEntity | 0x85bc34 | 0x579810 |
| Jetpack 37 | 0x565f9b | JetpackUtilityLogicEntity | 0x85a07c | 0x563f00 |
| SkipGo 38 | 0x56610e | SkipgoUtilityLogicEntity | 0x85ccfc | 0x588160 |
| Surrender 39 | 0x5661cc | SurrenderLogicEntity | 0x85d1cc | 0x58bae0 |
| ChangeWorm 40 | 0x5661a8 | WormSelectLogicEntity | 0x85df14 | 0x59a5f0 |
| Redbull 41 | 0x5661e2 | RedbullUtilityLogicEntity | 0x85cb24 | 0x587dc0 |
| BubbleTrouble 42 | 0x5662b9 | BubbleTroubleUtilityLogicEntity (creates BubbleTroubleLogicEntity at 0x5501da) | 0x858698 | 0x550a80 |
| Binoculars 43 | 0x5662d2 | BinocularsUtilityLogicEntity | 0x857d98 | 0x54bfe0 |
| Dynamite 5, Landmine 8, Sheep 15 | 0x5662eb | PayloadWeaponLogicEntity. When manager flag `+0x8d` is set, it disables control group `Fire`, enables `UtilityFire` (Input.DisableGroup / Input.EnableGroup) and sets global 0x95c36c=1. Meaning [disasm, docs/weapons-audit.md "Jetpack"]: +0x8d is the movement-utility mode (rope, jetpack, open parachute); the payload becomes the secondary weapon (+0x8c / +0x98, 0x566310), dropped with Fire.Second in the UtilityFire group while the utility stays | 0x85c6bc | 0x586040 |
| default: ids 0-4, 6, 7, 13, 16-20, 23, 24, 26, 29-33, 44-66 | 0x56642a | looks up the container by name (0x50b8b0) and asserts BaseWeaponContainer. If it IsKindOf PayloadWeaponPropertiesContainer (0x9688e0), creates **PayloadWeaponLogicEntity**; otherwise `assert(false)` at 0x5664ec | 0x85c6bc | 0x586040 |
| kWeaponUndefined 67 | 0x565ee6 | none (clears the current weapon) | - | - |

- Enum ids 44-47 (DoubleDamage, CrateShower, CrateSpy, Armour) have BaseWeaponContainers, so this path would assert on them. They must be applied elsewhere. ArmourLogicEntity and LowGravityLogicEntity have no `0x639b83` creation site. (disasm). They are applied on pickup by CrateLogicEntity 0x5c9800 (weapon id − 0x22 switch): 44 DoubleDamage → SetData("DoubleDamage") + `DoubleDamage.Activated`, 45 → `GameLogic.CrateShower.FromCrate`, 47 → `Armour.Collected` with the index of the worm that touched the crate (crate +0x54, set at 0x5cb98c / 0x5c97f1 from the colliding worm's +0x30), whose handler 0x5ae1ea sets that worm's flag 0x80 [disasm]
- kUtilityTeleport and kUtilityBridgeKit have WEAPTWK containers but no WeaponNameEnum value. (data)
- AimedWeaponLogicEntity is created once at game setup, in 0x4eba10 next to LogicalWeaponManagerService and WXWeaponPanel. It is not created per weapon. (disasm)

#### Stage 2: PayloadWeaponLogicEntity children and payload class (disasm)
- **Start 0x582d70** (reached from HM 0x586040 -> 0x5837a0) reads the PayloadWeaponPropertiesContainer:
  - `IsAimedWeapon +0x1e0`: checks Min/MaxAimAngle against ±pi/2 and publishes `Weapon.MinAimAngle` / `Weapon.MaxAimAngle`. If not targeting, it also publishes `Weapon.ParabolicRetical` (from `UseParabolicRetical +0x1d5`).
  - `HasAdjustableFuse +0x1d0`: creates AdjustableFuseWeaponLogicEntity (vt 0x857058, HM 0x543ad0).
  - `HasAdjustableHerd +0x1d2`: creates AdjustableHerdWeaponLogicEntity (vt 0x8570c8, HM 0x5441e0). It asserts that the two flags are not both set.
  - `HasAdjustableBounce +0x1d1`: never read here. AdjustableBounceWeaponLogicEntity (0x856fe8) has **no creation site**, so the ClusterGrenade/Grenade flag is dead. (disasm: negative search)
  - `IsTargetingWeapon +0x1c9`: sets `this+0x38=1` and sends `Weapon.CreateHomingCursor` (if `IsHoming +0x1cd`), else `Weapon.CreateBomberCursor` (if `IsBomberWeapon +0x1cb`), else `Weapon.CreateTargetingCursor`.
  - Otherwise, `IsPoweredWeapon +0x1c8` creates PoweredWeaponLogicEntity (vt 0x85ca78, HM 0x586e40).
- **LaunchPayload 0x585e90** (called from HM 0x586040). If `IsBomberWeapon +0x1cb`, it calls 0x5838a0: `IsControlledBomber +0x1ca` ? **SuperBomberLogicEntity** (vt 0x85d048, HM 0x58b660) : **BomberLogicEntity** (vt 0x858240, HM 0x54e1a0). Otherwise it calls the **payload factory 0x585330**.
- **Payload factory 0x585330** picks the payload class by **string compare on the container name** (`vtbl+0x18`), not by WeaponType:

| container name | payload logic class | vtable | HM | branch VA |
|---|---|---|---|---|
| kWeaponConcreteDonkey | DonkeyLogicEntity | 0x858b6c | 0x553cc0 | 0x5853c8 |
| kWeaponHomingMissile, kWeaponHomingPidgeon, kWeaponFactoryHoming | HomingPayloadLogicEntity | 0x859e7c | 0x5616d0 | 0x5856e7 |
| kWeaponMadCow, kWeaponOldWoman, kWeaponScouser | WalkingPayloadLogicEntity | 0x85d6dc | 0x592ad0 | 0x5856be |
| kWeaponSheep, **kWeaponSuperSheep** | JumpingPayloadLogicEntity | 0x85a364 | 0x564680 | 0x585695 |
| kWeaponStarburst | StarburstLogicEntity | 0x85ce04 | 0x5890e0 | 0x5855f1 |
| kWeaponPoisonArrow | PoisonArrowLogicEntity | 0x85c934 | 0x586600 | 0x58564d |
| anything else | ParabolicPayloadLogicEntity | 0x85b504 | 0x577f40 | 0x585679 |

- After creation, the factory computes the launch vector:
  - `IsAimedWeapon` -> aim angle, `IsDirectionalWeapon +0x1cc` -> facing.
  - `IsPoweredWeapon`: speed = `BasePower +0x110 + ShotPower * MaxPower +0x114`.
  - AI path: uses `AI.LaunchVelocity`.
  - `IsLaunchedFromWorm +0x1cf`: applies `Worm.EyeLevelOffset`.
  - `HasAdjustableFuse`: fuse handling.
- Secondary spawns (disasm):
  - **SuperSheep take-off**: Payload slot 24 (`Input.FirePressed`, 0x581a60) detonates if `DetonatesOnFirePress +0x1dc`. Otherwise, if the container class is exactly FlyingPayloadWeaponPropertiesContainer (0x9689e8), it spawns **FlyingPayloadLogicEntity** (vt 0x859424, HM 0x558250). Sheep (flag 1) explodes and SuperSheep (Flying container) flies.
  - **Bomber drop 0x54ddf0**: container kWeaponFatkins -> **FatkinsStrikePayloadLogicEntity** (vt 0x858f1c, HM 0x554cd0), else ParabolicPayloadLogicEntity. It also compares `kWeaponDoctorsStrike` (a cut weapon).
  - **Fatkins [disasm 0x554a80..0x5553f4 + data]**: spawned as any Bomber bomb: position = run start + direction × GroundSpeed × elapsed, velocity = direction × GroundSpeed (0 if IsAffectedByWind), so it leaves the plane like an airstrike bomb (NumStrikeBombs 1). A Parabolic payload (gravity, not low gravity, Radius 25 = a 1.25 m sphere, Scale 1.6, LifeTime −1) overriding slots 2, 3, 6 (= Parabolic 0x576fc0), 7 (HM: init 0x554b60 sets the blast scale +0x1c4 = 1, the contact count +0x1c0 = 0, binds Camera.Shake.*), 14 (no graphic launch offset), 23 (creates FatkinsStrikePayloadGraphicEntity) and **32, the contact handler 0x554e90**. The Parabolic tick (0x577417) calls slot 32 on any contact (land, worm, object, bubble); Fatkins ignores the contact flags and every DetonatesOn* flag: disarmed (+0x6f, sunk) it is removed (slot 21); else 0x580db0 bounces it (0x57ef80 → Bounce 0x518f40 with e = ParallelMinBounceDamping 0.6 on the normal part, TangentialMinBounceDamping 0.4 kept on the tangential part, zero under `Bounce.MinSpeed` 0.03 units/ms; `Payload.Bounce` → BounceFx WXP_Wep_Fatkins / BounceSfx weapons/FatkinsBounce when it still moves), count += 1; moving and count ≤ 3 → flies on (0x576580), else `Payload.Rest` (0x5758c0) → DetonatesAtRest with no expiry → Detonate (full WEAPTWK blast at the entity, ImpulseOffset −30, DetonationFx WXP_ExplosionX_Large, DetonationSfx weapons/ExplosionLarge). Then, in every case, an ExplosionMessage at the entity position (the sphere centre) with WormDamageMagnitude 75 and ImpulseMagnitude 0.6 unscaled, WormDamageRadius 145, LandDamageRadius 115.8 and ImpulseRadius 200 × scale, the impulse centred on the blast (no ImpulseOffset), kind 0; WXP_ExplosionX_Med, or WXP_Explosion_Small when LandDamageRadius × scale < 60; scale −= 0.2; Camera.ShakeStart (inert: Shake.Length / Magnitude 0). So: contacts 1-3 bounce with blasts × 1, 0.8, 0.6; the 4th (or the first that stops it) detonates, then blasts × 0.4 (or the scale it had).
    - **Effects and sounds per contact [disasm 0x5551f0..0x555240 + data PARTTWK / WormsX.fev]**: the contact blast's effect is spawned by the handler itself (0x5c1410 at the entity, the ExplosionMessage has none): LandDamageRadius 115.8 × scale = 115.8, 92.6, 69.5 → WXP_ExplosionX_Med (RingDark, InnerCloud, Ring, TailAnchors, BangTrails, ShockRing mesh WXPMesh1, MeshBang_Outer, WhiteoutFlash), 46.3 (× 0.4) → WXP_Explosion_Small (Small_Cloud, SMALL_Ring, BangTrails_SMALL, Small_Glow, WhiteoutFlash). Neither handler nor ExplosionMessage plays a sound: it is the effects' EmitterSoundFX, global/ExplosionRegular on WXP_ExplosionX_InnerCloud and WXP_Explosion_Small_Cloud. A moving contact first gets BounceFx WXP_Wep_Fatkins = WXP_FatkinsBounceMesh (mesh WXPMesh7, size (0.6, 0.225), 5 s) + WXP_FatkinsPuff (28 WXSprite4 puffs, 1 ± 0.6 s, 5 units up) and BounceSfx weapons/FatkinsBounce (2D, -3 dB, FatkinsBounce1-2 random without repeat). The Detonate adds DetonationFx WXP_ExplosionX_Large (the Med emitters + WhiteoutflashLarge, LargeRingDark, BangTrailsLarge, MeshBang_HotCoreLarge, InnercloudLarge with EmitterSoundFX weapons/ExplosionLarge, AnchorsHigh) and DetonationSfx weapons/ExplosionLarge (the same event, max playbacks 1). So a bounce sounds FatkinsBounce + ExplosionRegular, the last contact ExplosionLarge + 2 × ExplosionRegular.
  - **SuperBomber 0x58ae50** spawns ParachutePayloadLogicEntity (vt 0x85be14, HM 0x57a8a0).
  - **Detonate** (Payload slot 20, 0x580f10): if `NumBomblets +0x1a0 > 0`, it creates ClusterGeneratorLogicEntity (vt 0x8588f0, HM 0x551950). That entity's 0x5519d0 spawns **ParabolicPayloadLogicEntity** bomblets. The bomblet container is the one named by the parent's `BombletWeaponName` (+0x1b0): 0x551a89 reads it and looks it up (0x50b760) for the new ParabolicPayloadLogicEntity (0x551a56) [disasm].
  - `GameLogicService::CreateMine` 0x4f9630 (and 0x4f9c40) creates level mines as Parabolic with kWeaponLandmine.

#### Launch point and self-hit exclusion (disasm, data)
- **Payload start** (factory 0x585a29..0x585c35): `IsLaunchedFromWorm` gives pos = worm logical pos (+0x38) + (sin yaw × `LogicalLaunchZOffset`, `Worm.EyeLevelOffset` 15 + `LogicalLaunchYOffset`, cos yaw × Z). Nothing is added along the aim: Bazooka, Grenade, Homing Missile, Poison Arrow, Banana, Cluster, Holy, Gas leave from the eye (Z = Y = 0); Dynamite 13/-10, Landmine 10/-10, Sheep/SuperSheep/Starburst 5/0, OldWoman 7/0, Scouser 10/0 (WEAPTWK). A worm that is not Ambulatory (state +0xf0 != 0) and moving adds its velocity to the launch and starts 30 units ahead along that velocity (0x585bc5). `Weapon.GraphicalLaunchLocation` - pos is only a draw offset (0x57de20).
- **Payload collider** (Payload start 0x582200): collider sphere at +0x28, radius `Radius` +0xe0, flags `ColliderFlags`|8, mask 0x3c37 (0x519c80). It then sweeps one 20 ms frame along its velocity (0x5824b3 → 0x519db0 → 0x516c80) and stores the owner id (`[rec+0x18]`) of **every collider it touches** in the vector +0x11c.
- **Each update** (0x581dc0, called from Parabolic 0x576fc0, Payload 0x5827c0, Walking 0x593580/0x593b30, 0x5887b0): the same 20 ms sweep; a contact whose id is in +0x11c is skipped (0x581ec0 → 0x581f7c), any other is the hit (time 0x95c28c, id, flags). The vector is then **replaced** by this frame's contacts (0x581fe1..0x582009). So the shooter, overlapped at launch, is ignored until a frame where the payload no longer touches it; it can be hit again after that. No arming delay or distance test is involved (StartsArmed 1, ArmingCourtesyTime 0 for every impact payload; the courtesy time is the mine's worm trigger).
- **Sweep primitive**: 0x516c80 / 0x517e20 store the own collider index (0x91e800) and an exclude owner id (0x91e804); the per-collider tests 0x516350 / 0x5164b0 / 0x517630 skip that index, colliders whose flags `[rec+0x14]` miss the mask (0x95c294), and colliders whose `[rec+0x18]` equals the exclude id. Payload sweeps pass id -2 (none). Land rays from payloads (0x57dca0, 0x5750d0) pass mask 0: land only.
- **Guns** (GunWeaponLogicEntity fire 0x55df90): start = worm pos + (0, EyeLevelOffset + `LogicalLaunchYOffset`, 0) + `LogicalPositionOffset` ⊙ aim (all 0 for Shotgun and Sniper: the eye). The land ray (0x55e353, `Range` 9999 steps of 1 unit) is land only; the worm sweep (0x55e3c2 → 0x519dd0) passes the active worm's id (0x5b27e0: `ActiveWormIndex` → logical worm +0x14) as the exclude id, so the shooter is never hit by its own bullet; a land hit sends an ExplosionMessage (0x55e5da: WormDamageMagnitude, WormDamageRadius) that can hurt it.
  - **Hit handling [disasm]** (0x55e10f..0x55eb00; bCanDamageLand +0x10a gates the land part, n = bullets of the call): collision FX first (LandCollisionFX / WormCollisionFX at the hit), then by collider flag `[rec+0x14]`. **Land** (nearest hit is the land ray): `0x55d8c0` adds LandDamageMagnitude x n to a per-voxel counter (`pVoxelDamge`, key = the ray's land frame id 0x952ce8 + voxel id 0x952c64) and, once it reaches 15 (0x81adf0), sends `Land.ClearVoxel` for that one voxel + `Land.NewShape` (hit point, 10) and resets it; then ExplosionMessage 0x518ce0 at the hit point: WormDamageMagnitude x n, ImpulseMagnitude, WormDamageRadius, **LandDamageRadius**, ImpulseRadius, push centre = hit point - 1 unit x shot direction. **Worm** (flag 1: the collider record is read through the worm record, health +0x11e; assumed to be the worm collider bit): no land damage; ExplosionMessage with damage centre = the worm's position, push centre = position - (2 dx, 2, 2 dz) units, damage = WormDamageMagnitude x n (DamageIsPercentage: of its health), the same radii. So the worm is hurt and pushed by the ordinary Explosion handler 0x5ae4f0 (armour applies, falloff within WormDamageRadius, push 1.2 x ImpulseMagnitude x (R - d) / R from the push centre, mostly upward for a level shot); `ImpulseDirection` is not used for it (+0x8c is only the angle of the muzzle rotation at 0x55e1a2). Only a collider with neither flag 1 nor 0x800 gets a DamageImpulseMessage 0x518c20 (type 5, no armour). Sniper and Shotgun: LandDamageMagnitude 25 >= 15, so every shot that hits land clears one voxel (about 1 unit, 20 mesh units); LandDamageRadius 0 = no crater; ImpulseRadius 40 (2 m), WormDamageRadius 20 (Sniper, 1 m) / 30 (Shotgun, 1.5 m).
  - **FX [data]**: WormCollisionFX = LandCollisionFX = `WXP_ShotgunBlast` = WXP_ShotgunBlastHit (10 sprites 3 units, 1.2 +- 0.2 s, V (0, .02, 0) +- (.2, .05, .2), grey-lilac (.95, .9, 1) to (.5, .45, .55), EmitterSoundFX `weapons/ShotgunFire`) + WXP_ShotgunBlastHitTrails (10 trail sprites 1 unit, 0.4 +- 0.1 s). A small puff, no explosion sprites and no ExplosionRegular sound. That the worm collider's owner id is the logical worm's +0x14 is assumed (the payload registers its own +0x14 the same way, 0x58248b).

#### Per-container summary (data + disasm above)

| container | WeaponType (data) | weapon logic | payload logic |
|---|---|---|---|
| kWeaponBazooka, Grenade, ClusterGrenade, HolyHandGrenade, BananaBomb, GasCanister | 2/4 | PayloadWeapon (+Powered; +AdjFuse for Grenade/Cluster/Banana) | Parabolic |
| kWeaponDynamite, kWeaponLandmine | 5/6 | PayloadWeapon (UtilityFire case) | Parabolic |
| kWeaponSheep | 8 | PayloadWeapon (UtilityFire case) | Jumping |
| kWeaponSuperSheep | 8 | PayloadWeapon | Jumping, then Flying on fire |
| kWeaponStarburst | 8 | PayloadWeapon | Starburst |
| kWeaponHomingMissile, kWeaponFactoryHoming | 3 | PayloadWeapon (+HomingCursor) | Homing |
| kWeaponOldWoman, kWeaponScouser | 9 | PayloadWeapon | Walking |
| kWeaponConcreteDonkey | 10 | PayloadWeapon (+TargetingCursor) | Donkey |
| kWeaponAirstrike | 10 | PayloadWeapon (+BomberCursor) | Bomber -> Parabolic bombs |
| kWeaponFatkins | 10 | PayloadWeapon (+BomberCursor) | Bomber -> FatkinsStrikePayload |
| kWeaponSuperAirstrike | 10 | PayloadWeapon | SuperBomber -> ParachutePayload |
| kWeaponPoisonArrow | 2 | PayloadWeapon (+Powered) | PoisonArrow |
| kWeaponFactoryWeapon | 2 | WeaponFactoryLogicEntity -> PayloadWeapon | Parabolic |
| kWeaponClusterBomb, Bananette, LandmineBomblet, FactoryCluster | 4 | none (sub-payloads) | Parabolic via ClusterGenerator: 0x5519d0 creates `ParabolicPayloadLogicEntity` (0x551a56) with the container named by the parent's `BombletWeaponName` (+0x1b0, 0x551a89 → lookup 0x50b760) [disasm] |
| kWeaponLandmineCluster | 6 | none | swapped in by Landmine Detonate when DetonationType = Clusters (0x581258) |
| kWeaponFatkinsFood, kWeaponSentryGunPayload | 10/0 | none | dead data: neither name is a string in the exe, no WEAPTWK field (BombletWeaponName, FactoryWeaponName...) and no Lua script names them [disasm + data]; the Fatkins food is only graphics (`Fatkins.Food1-3` meshes, PayloadGraphicEntity 0x57cad0) |
| kWeaponShotgun, SniperRifle / melee x4 / utilities | 2 / 5 / 1 | see Stage 1 | none |

Doc corrections (disasm):
- The payload class is chosen by container **name** (0x585330), not by container class.
- SuperSheep is launched as **Jumping**.
- SuperSheep flow [disasm]: launch (first FIRE) = JumpingPayloadLogicEntity (0x564680, factory 0x585695), walks like a Sheep; second FIRE = Payload slot 24 `Input.FirePressed` 0x581a60, container class FlyingPayloadWeaponPropertiesContainer (0x9689e8) spawns FlyingPayloadLogicEntity (vt 0x859424, HM 0x558250), which is the steered flight; third FIRE detonates. Ours: walking is not steered (Jumping payload), the flight is (stick yaw / pitch, user-requested 2026-10-03); the HUD legend reads "A: take off" while walking, then "A: detonate" + "LS: steer" [ours].
- **StealInventory belongs to OldWoman** (WormCollideResponse=1). Scouser is 2 = FloatAway, which uses its second mesh `InflatedScouser`. The previous "StealInventory = Scouser" note was wrong.

### WEAPTWK enum fields: values and readers

There is no `FireType` or `DetonationType` field. The enum-typed fields are `WeaponType`, `MeleeType`, `DetonateMultiEffect` and `WormCollideResponse`; `OrientationOption` and `ColliderFlags` are u32. Each field record's `+0xc` points to an enum descriptor `{name, 0, values[]}`, and a value = its index in that list. This is data: MeleeType values 1-4 match the names. Records also hold runtime-filled getter/setter pointers at `+0x18`/`+0x20`, e.g. WeaponType get 0x527970 / set 0x630f10. Game code inlines the reads, so callers of those getters only point back to the schema.

| field (+off) | enum table | values (data) | readers / branches (disasm) |
|---|---|---|---|
| WeaponType (Base +0x34) | WeaponTypeEnum 0x90ca44 | 0 kNoType, 1 kUtility, 2 kProjectile, 3 kTargetted, 4 kThrown, 5 kMelee, 6 kEnvironment, 7 kHitscan, 8 kAnimal, 9 kControlled, 10 kStrike, 11 kMovement | Only inline read found: **0x5973e6**, WeaponAccessoryEntity, on the kWeaponFactoryWeapon container. `==4 kThrown` selects accessory handler 0x595ed0 (the "Base" locator); anything else selects WAE_Standard 0x5901c0. No switch on WeaponType exists in the logic, which dispatches by id (0x565ecc) and by name (0x585330). The data values are mostly descriptive: Shotgun/Sniper are 2, not 7, and Dynamite is 5. (negative result from register tracking: medium) |
| DetonateMultiEffect (Payload +0x154) | DetonationTypeEnum 0x90c8f0 | 0 kDT_Random, 1 kDT_Normal, 2 kDT_Fire, 3 kDT_Clusters, 4 kDT_BigPush | See the Detonate breakdown below |
| WormCollideResponse (Payload +0x158) | WormCollideResponseEnum 0x90ca94 | 0 kWC_Default, 1 kWC_StealInventory, 2 kWC_FloatAway | **0x5931bd** in the WalkingPayload worm-collision handler 0x5930d0 (reached only when `DetonatesOnWormImpact`=0): 0 -> 0x5933ee (default); 1 -> 0x593368: StealInventory 0x592d30, state 3 (previous state kept at +0x15c, restored by the update 0x59407b once now ≥ +0x178 = now + 800 ms), Velocity 0, walk heading +0x160 negated (she turns back), `Payload.PlayIntermediateAnim` (WEAPTWK AnimIntermediate `Steal`); 2 -> 0x5931f7: state 4, `Worm.OverridePhysics`, `Payload.ChangeToSecondMesh`. Any other value asserts "Unknown worm collision response type" |
| MeleeType (Melee +0x98) | MeleeWeaponEnum 0x90c914 | 0 kNoMeleeType, 1 kMeleeBaseballBat, 2 kMeleeFirepunch, 3 kMeleeProd, 4 kMeleeTailNail | Melee fire 0x568180: **0x568247** `==2` makes the worm say `Punch`, else `WeaponFired` (Worm.Say). **0x568329** `==4` sends `Worm.OverridePhysics` set 0x20. End 0x569b00 at **0x569d4e** `==4` sends `Worm.OverridePhysics` clear 0x20 (-0x21). NoMoreNails = 4 = tail nail |
| OrientationOption (Payload +0x128, u32) | none | 0-3 seen (data: 0 Dynamite/walkers, 1 grenades, 2 bazooka/homing/sheep) | Logic start 0x582200 at **0x5825dd**: `==3` sets spin = SpinSpeed×0.08 ×(1±0.33 rand), else orientation from velocity (0x57f900). Graphic update 0x57c890 switch at **0x57ca2e** (table 0x57cab8): 0 sets angle `+0x3c`=0; 1 copies `+0x30`; 2 adds spin×dt to `+0x3c`; 3 adds spin×dt to `+0x3c` and `+0x44`. Meanings (fixed / follow / spin / tumble) assumed |
| ColliderFlags (Payload +0xcc, u32) | none | 0 or 128 (walkers/animals) | read once at payload start 0x582454: the payload's collider is created (0x519c80) with flags ColliderFlags | 8, mask 0x3c37, radius = `Radius` (+0xe0) [disasm] |

#### DetonateMultiEffect handling (disasm)

The type is read in Detonate (0x580f10) and in Explode (0x57f140):

- **Store and Landmine override.** 0x5810b6 copies the value to `entity+0x14c` (the entity init 0x580290 sets 1). If the container is `kWeaponLandmine`, scheme value `Mine.DetonationType` (0x581113) overrides it: -1 maps to Random, 0 keeps the container value, 1-4 force that type, and a value greater than 4 asserts (`nDetonate <= 4`) [disasm].
- **Random.** Random is resolved only for kWeaponLandmine, as `(rand & 3) + 1` at 0x5811bf, one rand draw (0x691935) [disasm]. kWeaponLandmine DetonateMultiEffect = 0 and LOCAL `Mine.DetonationType` = 0 [data], so every landmine explosion (laid, random, map, scripted, respawned, factory) is Normal, Fire, Clusters or BigPush at 1/4 each. The draw comes before the dud test (+0x5d at 0x581326: ExpiryFx, `Payload.Disarm`, no blast), so a dud draws it too [disasm].
- **Clusters (3).** The payload swaps its properties (entity +0xc4 and the local copy) to `kWeaponLandmineCluster` (0x581236), so everything after uses that container: DetonationFx `WXP_ExplosionX_Med`, DetonationSfx none, and Explode blasts with WormDamageMagnitude 25, WormDamageRadius 58, LandDamageRadius 40.5, ImpulseMagnitude 0.2, ImpulseRadius 85, ImpulseOffset -45 [disasm + data]. Wormpot scales only `kWeaponLandmine` (Wormpot.lub ApplyWormpotDamageScale / PowerScale), so this blast and its bomblets are never scaled [data]. Then NumBomblets 5 > 0 (0x58162a) makes a ClusterGeneratorLogicEntity of `kWeaponLandmineBomblet` (below) [disasm + data].
- **Fire (2).** The explosion FX is `WXP_Napalm` instead of `DetonationFx` (0x5813c4); nothing else reads type 2: DetonationSfx `global/ExplosionRegular` still plays (0x5814a6), the blast is the Normal one [disasm]. `WXP_Napalm` = ExplosionG_RingDark, NapalmMine_Cloud and NapalmMine_CloudSmall (EmitterSoundFX weapons/ExplosionBoxed), ExplosionG_TailAnchors, ExplosionX_ShockRing, GasExplFade, WhiteoutFlash, GasCloudDelayed (weapons/GasLoop), GasCloud, NapalmMine_Bomblets, Skulls_GasCloud [data, PARTTWK]; particles carry no damage field, so Fire is visual only [data].
- **Normal (1).** DetonationFx `WXP_Explosion_Mine` (its emitter WXP_Explosion_MineCore plays global/ExplosionRegular), DetonationSfx global/ExplosionRegular, the kWeaponLandmine blast [data].
- **BigPush (4).** Explode 0x57f140 at **0x57f1c6** scales ImpulseRadius and ImpulseMagnitude ×2.0, and WormDamageRadius, LandDamageRadius and WormDamageMagnitude ×0.3; ImpulseOffset is kept [disasm, the x87 sequence 0x57f3d3..0x57f45e]. Every term is also multiplied by `rand%MaxPowerUp+1` (1 when MaxPowerUp is 0; 0 for the landmine containers, so no draw) [disasm + data].
- **Explosion-kind argument.** Explode passes a kind value to ExplosionMessage 0x518ce0: 2 when the name starts with `kWeaponCluster`, 4 when it starts with `kWeaponFactory`, 3 when DetonationType is Clusters (0x57f35e), and 0 otherwise. The swapped name `kWeaponLandmineCluster` matches neither prefix, so the Clusters blast is kind 3; the bomblets (entity +0x14c = their own DetonateMultiEffect 1) are kind 0 [disasm + data].
- **Expiry call.** A landmine goes off by LifeTime expiry (DetonatesOnExpiry 1): the update 0x57fae0 at 0x57fb78 calls Detonate(pos +0x28, velocity +0x34, normal = the zero vector 0x96e878, uninitialised .data, never written) [disasm].

#### ClusterGeneratorLogicEntity (disasm)

Detonate creates it after its own Explode (0x58164e), so the parent's blast comes first, then Setup 0x551510 (pos, velocity, normal):

- **Setup.** Keeps the detonation position (+0x50). The orientation +0x2c (3×3) is identity unless both the normal and the velocity are non-zero; then it is the rotation taking up (0, 1, 0) (0x91f530) to the velocity reflected on the normal (v - 2 (v·n̂) n̂), angle asin(|r̂ × up|) (0x551812..0x551870) [disasm]. Every expiry Detonate passes a zero normal (0x57fb89), so for the Cluster Grenade, Banana Bomb and landmine (all DetonatesOnExpiry) the cone axis is always world up [disasm + data].
- **Update 0x5519d0, one bomblet per call.** It creates a ParabolicPayloadLogicEntity with the container named by `BombletWeaponName` (0x551a89), sets its position to +0x50 (0x57dd60), then draws, in order: azimuth = rand × 2π (0x551b02), tilt = rand × `BombletMaxConeAngle` (0x551b30), Euler (tilt, azimuth, 0) → matrix 0x6df502 (order 0 = Rz·Ry·Rx, sincos by the 64K table 0x7c5138), direction = (0, 1, 0) × that matrix = (sin tilt sin az, cos tilt, sin tilt cos az), then × the Setup orientation; speed = `BombletMinSpeed` + (`BombletMaxSpeed` − `BombletMinSpeed`) × rand (0x551c0e..0x551c7e, asserts Max > Min); velocity = direction × speed (0x57dd80) [disasm]. The count +0x28 reaching NumBomblets ends it (0x551d22); it returns 0x14, so the bomblets leave 20 ms apart; its task is keyed at the manager's time when added (0x68de04), so the first leaves at the blast's game time [disasm].
- **Values [data].** kWeaponClusterGrenade cone 0.28, speed 0.12-0.24 units/ms, 4 × kWeaponClusterBomb; kWeaponBananaBomb 0.25, 0.25-0.325, 5 × kWeaponBananette; kWeaponLandmineCluster 0.27, 0.1-0.24, 5 × kWeaponLandmineBomblet (Radius 4, StartsArmed, DetonatesOnLand/Object/WormImpact 1, 10 dmg, WormDamageRadius 35, LandDamageRadius 30, ImpulseMagnitude 0.05, ImpulseRadius 65, ImpulseOffset -25, SinkDepth 5, IsAffectedByWind 0, mesh ClusterBomb, DetonationFx WXP_ExplosionX_Med, DetonationSfx global/ExplosionRegular). Weapon Factory (0x5983f0 at 0x599587..0x5995e3): NumBomblets = ProjectileNumClusters, cone 0.28, MaxSpeed = k/2, MinSpeed = k/7 with k = 0.5 ClusterSpread + 0.3, bomblets kWeaponFactoryCluster [disasm].
- **Wind.** Wormpot.lub SetWeaponWind lists kWeaponClusterBomb and kWeaponBananette but not kWeaponLandmineBomblet or kWeaponFactoryCluster [data].

### Utilities: Armour, DoubleDamage, CrateSpy, CrateShower, LowGravity [disasm unless tagged]
Crate pickup 0x5c9800 switches on crate type − 0x22 (byte table 0x5c99ac): 44 DoubleDamage, 45 CrateShower, 46 CrateSpy, 47 Armour.
- **Armour**: `Armour.Collected` (0x5c991e); the worm handler 0x5ae1ea sets `WormData.Flags` (+0xEC) |= 0x80, never cleared (rest of the worm's life). Only the explosion handler 0x5ae4f0 tests it: damage = trunc(dmg × `Shield.DamageScale` 0.25) (0x5ae71c–0x5ae77e) and impulse × 0.5 (0x5ae96f–0x5ae98e), both skipped for explosion kind 5. `Damage.Impulse` (0x5ae320: direct hits by melee and by non-worm colliders; a gun bullet on a worm is an Explosion, see Guns), poison (0x5ac060), fall damage (0x5ac3e0) and Vapourize ignore it. `Armour.ProtectionPercentage` 25 is read only by the AI damage estimate (0x548fc0, from AIPlanAttack 0x49ed30). ArmourLogicEntity (vtable 0x8579d0) only shows the shield on its worm's turns (0x549210 / 0x549270).
- **DoubleDamage**: data key `DoubleDamage` = 1 (0x5c9854) + `DoubleDamage.Activated`; reset by stdlib.lub `DoPostActivity`, so it lasts the rest of the turn; Wormpot.lub sets it every turn [data]. ExplosionMessage ctor 0x518da5 doubles WormDamageMagnitude, ImpulseMagnitude, WormDamageRadius, LandDamageRadius and ImpulseRadius; DamageImpulseMessage ctor 0x518cbf doubles damage and impulse; 0x5abb63 doubles the per-damage cap (75, 150 with a Wormpot flag). Fall damage and poison are not doubled. Order: doubled first, then the armour trunc(× 0.25).
- **CrateSpy**: 0x5c8b20 sets `TeamData.IsCrateSpyActive` (+0x73) for the active team, never reset. CrateGraphicEntity 0x5c5270 then shows a Text3DEntity of the contents 15 units above every crate whose type is not 1 or 3, during that team's turns and on the local / owning client only (0x4d3ed0 / 0x708fdc).
- **CrateShower**: `GameLogic.CrateShower` 0x4fb820 calls CreateRandomCrate 0x4fa4b0 **6 times** (loop at 0x4fb850), track camera on the first only. Wormpot.lub sends it every turn in its crate-shower mode [data].
- **LowGravity**: LowGravityLogicEntity on `Input.FirePressed` (0x5672f0 → 0x567140) sets `Low.Gravity.Multiplier` = `Low.Gravity.OnValue` **0.5** (TWEAK); the mystery crate (0x5cad2a) and Wormpot (0x5d7353) do the same. `GameLogic.Turn.Ended` (0x4fe7a2 → 0x4f24f0) puts back `Low.Gravity.GameDefault` 1.0: the utility lasts one turn. The multiplier scales all gravity (worms 0x5a6d20, payloads 0x582200, crates, parachute, rope, melee, bomber, oil drums); IsLowGravity payloads use `Gravity.Slow` −0.00015 instead of `Gravity` −0.00025, then × the multiplier.

- **Bubble Trouble fire** (BubbleTroubleUtilityLogicEntity, a plain LogicEntity, not a BaseWeapon: no Timer.EndTurn, no StartRetreatTimer, so the turn goes on): 0x54ff40 posts `Weapon.PlayFireAnim`, sets the team's `InventoryN.WeaponDelays`[42] = 1 (0x5500ca, setter 0x65a496; cleared at the team's turn end) and schedules `Weapon.LaunchPayload.Callback` after `Bubble.LaunchDelay` 400 ms; the callback 0x550190 spawns the BubbleTroubleLogicEntity from the worm's position then (offset as in docs/weapons-audit.md), orientation (0, yaw + 1.3439, 0) (0x55021d → +0x54, the machine's rotation at 0x54e9b6), velocity 0.02 units/ms × (forward − up) (0x55031c → +0x60), then `Payload.Launched` (only weapon accessories listen), `GameLogic.DecrementInventory`, `Weapon.Delete`.
- **Bubble physics 0x54f160** (per 20 ms): unless resting (+0x79), a land + worm-sphere parabola cast over the next 20 ms (0x466ae0, 20 steps, collider mask 1); a hit sets +0x79 and it stays where it is (no snap); else under `Water.Level` it goes (+0x7a, 0x54fd3e); else pos += v × 20, v += a × 20 with a = (0, Gravity × Low.Gravity.Multiplier, 0). Any `Explosion` clears +0x79 (0x54effa). Colliders: body radius 9 flags 0x10 mask 0xc3f; shell Radius × 1.2 flags 0x1000 mask 9; inner Radius − 20 flags 0x4000 mask 1 (0x54f8b1..0x54f9a1). 0x54f350 switches the shell to 0x2000 while the active worm is inside. Worm sweeps use mask 0x811 (no bubble bit: worms walk through); the gun sweep (GunWeaponLogicEntity collider 0x55d4b0) uses mask 0x1c3f: 0x1000 in, 0x2000 out, so bullets stop on the shell unless the shooter is inside.
- **Icarus potion heal** (0x587750): Worm.Antidote, then `if (Energy < WormData.InitialEnergy) pending damage = Energy − InitialEnergy; GameLogic.ApplyDamage` (0x58785f..0x5878c4). InitialEnergy (+0xf8) is written only by the serializer (schema field 0x1b): no exe code and no Lua script sets it, and LOCAL.XOM's `Worm.DataNN` hold 0, so the branch never runs and the potion does not heal [disasm + data; Wiki: "doesn't directly heal damage"].
- **Icarus ceiling** (flap 0x587970): the flap velocity (0, FlapVelocity, 0) becomes (0, 0, 0) when the worm's y ≥ the skybox `Sun` locator's y (globals 0x955b40..48, set by SkyBoxEntity 0x485de0 from the `Sun` node's world matrix 0x50b080, default (0, 2000, z) at 0x485847 when absent) [disasm]. Sun heights [data, `w4m-models --list` with W4M_GROUPS, units]: Arabian day 7429 / evening 1596 / night 6371; Building 5878 / 1561 / 3910; Camelot 5797 / 2475 / (group3, 0 at rest); Prehistoric 5709 / 1634 / 6249; Wild West 7052 / 1682 / 4107; England day 5797, evening 1805, night 3641; Pirate 5878 / 1596 / 3704; Lunar 2849 / 1516; War day 1805, evening 450; Arctic day and night, Horror day and evening, Lunar night: 0 at rest.
- **Wings mount** (WAE_RedBullWings 0x595390): `RedBullWings` at the worm's `Pack_Locator` (0x5953d1, same locator as the jetpack 0x58c7f4, chute 0x58f1c3, Starburst 0x5917d0); the wings and the worm both play `FlyRedBull` (0x595428 / 0x595437) [disasm].
- **Abductee zap** (UpdateAbductee 0x5a9c40, from the worm update 0x5b2045 only in physics states 0, 2, 3; flag 0x400 clear sets the timer +0x13c to −1): the first call sets 30000 ms; then −20 per call. While the timer is > 0 or no spot is held, each call tries one spot: pos + ((r − 0.5) × 2 × XZRange, r × YRange, (r − 0.5) × 2 × XZRange) (three rand calls in that order), kept if Fits 0x59edf0 and EstablishPhysicsState 0x5a6af0 lands it Ambulatory (support within 1000 units below, land or collider) above `Water.Level` (+0xac): the landed position is stored (+0x140, +0x14c = 1). Once the timer is ≤ 0 with a spot: if the worm moves (|Velocity|² ≠ 0) it jumps there (WXP_Poof_VLarge, Velocity 0, WXP_Abductee_Teleport, message 0x22); either way the timer becomes MinTime + rand % (MaxTime − MinTime) and a spot farther than XZRange (3D) is dropped [disasm]. Worm.Zap.* at +0xc4..+0xd0 (0x5a9a56..0x5a9aad).
- **Ninja rope reel** (0x5713c0, from the rope update 0x574480, once per 20 ms): `NinjaRope.Lengthen.On/Off` / `Shorten.On/Off` (bound to Joypad.Input.MoveBackward / MoveForward at 0x4e19e0, digital) set +0x64 / +0x65; the step is `LengthenShortenRate` 0.2 × 20 = 4 units a tick (10 m/s). Lengthening is clamped to MaxLength over the whole rope (0x570fc0); shortening is refused outright when the step exceeds the last segment, when the rope would drop below `MinLength` 10, or the last segment below `MinBendDistFromWorm` 10, or when the moved end is blocked (0x571020, 0x56fcb0). The hooked crate (0x5cbc81) and drum (0x5d2110) run the same rope update 0x574480 with their own position and velocity [disasm].
- **Ninja rope swing** [disasm; TWEAK Ninja.* data]. Entity fields: m_NinjaRopeList +0x20 (28-byte segments: point, length +0xc, unwrap
  side +0x10), mode +0x38 (1 idle, 2 hook flying, 3 on land, 4 on an object), angle +0x120, spin +0x10c (rad per 20 ms), stick +0x68.
  Tweaks: Gravity × Low.Gravity.Multiplier × Ninja.GravityMultiplier 1 (+0xcc / +0xc8 / +0xfc), MaxLength 450, MinLength 10,
  MinBendDistFromWorm 10, NumRaycastRefinements 8, RotationDamping 0.99, LengthenShortenRate 0.2, SnapOffAngleRadians 7.5 (not read in the
  update), SwingAmount 6e-5, MaxUnreducedSwingLength 40 (not read in the update), WormMass 100, DetachVelocityMulti 1 (+0xd0..+0xf8).
  - **Update 0x574480** (per 20 ms, arguments Position, Velocity, Orientation): reel 0x5713c0, swing 0x5729e0, bounce / wrap 0x573060,
    unwrap 0x571600, Velocity = (new − old) × 0.02 × 0.05 (0x56fd60: a 50th of the motion's speed), Position = the swing's point.
  - **Swing 0x5729e0**: θ += ω (kept in [−π, π]); T = sin θ × (−g / L) × (−20), L = the last segment's length; stick s from 0x571820 (or
    +0x70 when set); ω += −s × SwingAmount × 20 × 5 × MinLength / (Lc + 0.001 Lc²), Lc = |Position − last point|; no push: ω ×=
    RotationDamping; ω += 20 T. Point = last point + L × row 2 of RotX(θ + ω + π/2) × RotY(yaw) = L (−sin(θ+ω) forward(yaw) − cos(θ+ω) up):
    θ = 0 hangs, θ > 0 behind the facing, forward stick drives θ down (the body forward). Orientation out: (θ + ω + 3π/2, yaw, const);
    the yaw is passed through (0x571820 converts the camera-relative stick to s = 1 under 1.41372 rad from the facing, −1 over 1.72788,
    else 0). _CIsin / _CIcos / _CIacos / _CIasin / _CIatan2 are the import thunks 0x6fe73c / 0x6fe742 / 0x6fe9a2 / 0x6fe9e4 / 0x6fe9f0.
  - **0x573060**: 0x56fcb0 (the rope's 5-unit collider at the new point, mask 0x19, overlapping more than one collider — one in mode 2 —,
    or Fits 0x59edf0 failing): ω = −0.9 ω, the point is still taken. Else the last stretch cut by land (0x571020: a cast from the point
    to the last bend, 0x56fbf0): up to 8 halvings p = (old + p) / 2 while still cut, keeping the latest hit (all cut: p = old); hit += 1
    unit toward p; |hit − p| ≤ MinBendDistFromWorm: ω = −0.9 ω; else ω ×= L_old / |hit − new|, the old segment's length becomes
    |its point − hit|, its side = −(hit − (its point + |its point − hit| × dir(its point → old))) (or −(new − old)), push (hit, |hit − new|).
  - **Unwrap 0x571600**: with two segments or more, the stretch from the point to the bend before clear (0x466ae0, 1000 steps) and
    (point − that bend) · its side > 0: pop the last segment, the new last length = |point − its point|, ω ×= L_popped / L_new (0x570600).
  - **Reel 0x5713c0**: lengthening by 4 units clamped to MaxLength − total (0x570fc0 sums the lengths); shortening refused when the step
    exceeds the last length, the total would go under MinLength or the last under MinBendDistFromWorm; the moved point refused when cut
    or blocked (0x571020, 0x56fcb0); else length set, Position moved there, ω ×= L_old / L_new.
  - **Fire / retract 0x573790** (FireUtil): mode 1 with NumShots +0x3c ≠ 0 → mode 2, 0x572800; mode 2 → retract (mode 1,
    NinjaRope.Retracted, 3dAimer.Display; NinjaRope.Kill if no shot is left); modes 3 / 4 → release 0x573530. Refused with
    global/FEError when standing (state 0) in the "Head" camera without Airstrike.HasTarget (0x573845).
  - **Launch 0x572800**: hook point = Position + (0, Worm.EyeLevelOffset, 0); velocity +0x40 = RotY(yaw) RotX(−pitch) (0, 0, 1)
    (0x91fa64), 1 unit/ms; pitch = WeaponAngle, or with +0x79 set 0x570650(Velocity, yaw): π/2 + s / 0.2 × π/4, s = min(|v.xz|,
    0.2 units/ms), negative when v.xz is along the facing (acos < π/2), π/2 when v.xz = 0; +0x114 / +0x118 = (−pitch, yaw).
  - **Flight 0x573d00** (mode 2, per 20 ms): the 5-unit rope collider at the hook point with mask 0x1e (0x56fcb0): an overlap calls
    0x571d90 (object attach). Else a land-only cast 0x466ae0(point, +0x40, accel 0, 20 steps, 1, mask 0, 0): hit step ≤ 20 → attach on
    land. Else point += 20 × +0x40; |Position (feet) − point| > Ninja.MaxLength → retract (0x573790, mode 2 path). NumShots +0x3c is
    decremented only at the land attach (0x573ea3, FETXT.NinjaLastShot / RopeShotsRemaining). The inventory goes once at Cleanup
    0x572620 when +0x7b (set by either attach) (DecrementInventory.Id 0x5727b0). +0x79 is cleared in mode 1 once the worm is not
    Ballistic (0x574b96).
  - **Collider flags** (WX_Collider 0x519c80 / 0x519d30, rope masks 0x19 / 0x1e / 0x3f): worm 1; payload ColliderFlags | 8
    (0x58246d; WEAPTWK ColliderFlags 0, or 128 for Sheep, SuperSheep, OldWoman, Scouser, Starburst): a Landmine is 8, Radius 3;
    crate by type (0x5c9200: health 1, weapon 0, utility 2, target 3, mystery 4, custom 5) 4 for health, 0x20 for target, else 2
    (0x5ca180), radius +0x44 (10 units, 0x5c66e8); oil drum 0x10, 9 units at its Position +0x20 (0x5d18b2); bubble body 0x10. So
    mask 0x19 (a worm's rope) is blocked by worms, payloads and mines, drums and bubbles, not crates [disasm].
  - **Attach on land 0x573d00**: segment 0 = the hit, length =
    |hit − Position|; θ = acos(clamp(−d̂.y)), d = eye (Position + Worm.EyeLevelOffset 15) − hit; if hooked before this turn (+0x79) and
    0x570650 (the horizontal velocity against the facing) gives > π/2, θ = −θ; ω = 0x571aa0: the angle between (Position − hook) and
    (Position + 20 Velocity − hook), negated unless the horizontal velocity points backwards (pure vertical: positive).
  - **Attach on an object 0x571d90** (mode 4): only when the worm is Ambulatory (0x571db4), else the flight goes on. A payload
    (0x4f3520) moving: "Tried to attach to a moving object", false; a crate (0x4f3570) or a drum (0x4f3610, 0x5d1760) attaches; a
    BubbleTroubleLogicEntity (0x95fba0) retracts the hook (0x57206f); else false. Segment 0 = the worm's Position, length = |hook
    point − it|, yaw = atan2(d.x, d.z) + π, d = hook point − worm, θ = acos(clamp(−d̂.y)), ω = 0; rope collider radius 5 units, a
    crate its 0x5c5fd0 size × 0.5 (+0x44), mask 0x3f; +0x79, +0x7b set, mode 4; NumShots unchanged.
  - **Release 0x573530** (FireUtil on the rope): one more swing from +0x58 (the last point), v = (new − old) × 0.05 × DetachVelocityMulti
    (units/ms); on land the worm gets it (Worm.OverridePhysics off); on an object NinjaRope.EndSwing carries it.
- **Payload water** [disasm + data]: PayloadLogicEntity 0x582050, from slot 30 0x5827c0 after a tick without collision (Jumping, Flying,
  Homing, Starburst 0x5887b0 and Donkey use it; Walking 0x85d6dc does not); Parabolic payloads get the same planes as events
  (Payload.CollideWithWater / DisarmPlane / ExpiryPlane, FindFirstEvent 0x576580, handled at 0x577cc4..0x577d4b). A plane is crossed
  when y > plane ≥ y + 20 v.y (0x57dc50). Water.Level 0 and Water.ExpiryDepth −200 units, absolute (WEAPTWK).
  - +0x6e set (the default, 0x5804c8; HomingPayload clears it while homing, 0x560bdc, and sets it again after, 0x560bac): Water.Level +
    Radius → slot 19 0x580830: SkimsOnWater and |v| > MinSpeedForSkim and asin(v̂.y) > MaxAngleForSkim (≤ 0): v ×= SkimDamping (x = z, y < 0
    asserted), SplishFx; else SplashFx; y = Water.Level + Radius. Else Water.Level − SinkDepth → 0x580aa0 + 0x57dda0. Else ExpiryDepth → slot
    21 (removal without a blast).
  - +0x6e clear: Water.Level → SplashFx (0x57fcc0) only.
  - **Sink 0x580aa0**: +0x6f = 1, acceleration 0; s = min(|v|, Payload.SinkSpeed.Max 0.1), v = v̂ s; k = s / Max; v.xz ×= k²; v.y =
    −max(Payload.SinkSpeed.Min 0.08, |v.y| k); y = Water.Level − SinkDepth; Payload.Sink, SinkingFx, Payload.Sunk (+0x144). 0x57dda0 disarms
    (+0x5c = 0, Payload.Disarm). Detonate 0x580f10 does nothing while +0x6f (0x581312); a collision while sinking removes it (0x577e86).
  - Per weapon [data WEAPTWK, units]: Radius / SinkDepth / skim (MinSpeed, MaxAngle, Damping): Bazooka 5 / 5 / (0.1, −0.4, 0.5, −0.5);
    Grenade 3 / 6 / (0.2, −0.4, 0.75, −0.75); ClusterGrenade 8 / 5 / (0.1, −0.4, 0.5, −0.6); BananaBomb 8 / 5 / (0.2, −0.4, 0.5, −0.5);
    HolyHandGrenade 3 / 5 / (0.2, −0.4, 0.5, −0.5); GasCanister 3 / 5 / (0.2, −0.4, 0.5, −0.5); PoisonArrow 1 / 5 / (0.1, −0.4, 0.75, −0.75);
    FactoryWeapon 5 / 5 / (0.1, −0.4, 0.45, −0.3); HomingMissile, FactoryHoming 5 / 5; Dynamite 7 / 7; Landmine 3 / 5; Sheep, SuperSheep,
    Starburst 8 / 8; OldWoman, Scouser 8 / 10; Airstrike, SuperAirstrike 15 / 15; Fatkins 25 / 30; ConcreteDonkey 72 / 80; ClusterBomb
    4 / 5 (SkimsOnWater 1 with MinSpeed 0, MaxAngle 0, Damping 0: never on a descent); Bananette 2 / 5; FactoryCluster, LandmineBomblet 4 / 5.
- **Wind drift** [disasm; data where noted]. Wind = Wind.Speed × (cos, 0, sin) Wind.Direction units/ms² (0x57eb25), Wind.MaxSpeed 8.5e-5.
  - **Inflatable Scouser, FloatAway** (WalkingPayload, state +0x158, update 0x593ee0, table 0x59418c): the catch 0x5931f7 (WormCollideResponse
    2): state 4, velocity 0, Worm.OverridePhysics(0x10), Payload.ChangeToSecondMesh. State 4 (0x591ac0) the next update: velocity (0, 0.08,
    0) units/ms (0x85d65c), expiry now + 6000 ms (0x591b13), state 6, WXP_ScouserTransform; with land both at pos + v × 1500 and pos +
    (0, radius, 0) it pops at once. State 6 (0x5924a0): acceleration 0; the first update whose sweep (0x59ec70) is clear: state 5 and
    0x592460: the payload acceleration with IsAffectedByWind forced, then x, z × 0.6 (0x81ae48), y = 0. 0x5925c0 in 5 and 6: the sweep;
    a hit in state 5 snaps there and pops (0x593fad); else v += a × 20, pos += v × 20; the worm's Position = the payload's (0x5927ff).
    Pop on expiry (DetonatesOnExpiry) or contact; WormDamageMagnitude 40, radius 21 units [data].
  - **Parachute** (ParachuteLogicEntity, update 0x579a10 → 0x579720 per 20 ms): closed, +0x60 = (PhysicsState ≠ Ballistic) and it
    opens (0x578db0) when the worm's v.y < −0.3 × 0.75 units/ms (0x85eaf0, 0x8c1568); Input.FireUtilPressed (0x57984f) and
    Parachute.AutoOpen call the same 0x578db0, which opens only if +0x60 is clear (Ballistic) and closes an open one (+0x61:
    ParachuteClose, 0x565920). Opening: DecrementInventory (unless +0xb8), Worm.OverridePhysics, PackAccessory.Wield, canopy +0x70 =
    Position + (0, 40, 0), +0xa8 = +0xac = 0; v88 = (Wind.Speed cos, Gravity × Low.Gravity.Multiplier, Wind.Speed sin) Wind.Direction,
    read once (0x579024..0x579109); s5c +0x5c and c0 +0xc0 are 0 from Initialize (0x57858d).
    Sway 0x578a40 (first, on WormData InputImpulse +0x68, 0.05 units/ms at full tilt): with |I| > 0, ahead = I · F, side = I · (F.z, 0,
    −F.x); ahead ≤ 0: side ≥ 0 → yaw += 0.02, lr → (3 lr + 0.75) / 4, ω −= 0.001; side < 0 → the mirror; ahead > 0: the same only past
    |side| > 0.01; |I| = 0: lr → 3 lr / 4. Then yaw −= +0xc4 (0.01 sin θ, from the last update), wrapped to [0, 2π), written to WormData
    Orientation; ω = (ω − 0.003 sin θ) × 0.99, θ += ω. So the stick turns the worm under the canopy at 0.02 rad a step (1 rad/s);
    Input.TurnLeft / TurnRight only set +0xb0 / +0xb1, which no parachute code reads.
    Open 0x5792a0: cos θ < 0.4 (0x81a620): s5c −= 2 (cos θ − 0.4) v88.y; else old = s5c, s5c = 2 s5c / 3, c0 += s5c − old; c0 =
    0x47a1a0(c0, 0, 6, 0.001) = (6 c0) / 7, change ≤ 0.001. A = (0.06 (0x85bcc0) + c0) F(yaw) + 70 (0x838400) v88; v = Velocity halfway
    to A, the change capped at 0.05 units/ms (0x569fa0), then v.y += s5c; canopy += 20 v; Position = canopy + (40 F.z sin θ, −40 cos θ,
    −40 F.x sin θ) when it Fits (0x59edf0), with Velocity = v, ForcedCameraOffset = −offset − (0, 40, 0); else it closes (0x57967e).
    The canopy graphic gets min(0.3 − 20 s5c, 1) (0x579488).
  - **Gas cloud** [data PARTTWK + disasm]: WXP_GasCloud is one 8000 ms particle, velocity and acceleration 0, ParticleIsEffectedByWind 1
    but ParticleMass 0.0: the position is p0 + v t + 0.5 Mass (a + W) t² from the spawn time (0x5b7450, Mass +0x13c, wind flag +0x1bc),
    so it never drifts; a spiral of 2 units at 0.0035 rad/ms (0x5b75e0); collision radius 100 units at offset (0, 10, 0), poison 10.
    WXP_GasCloudDelayed (Mass −0.05, visual) drifts upwind; WXP_LG_GasCloud (level gas, Mass 0.049, 60 s, poison 10) drifts downwind.

- **Starburst flight model [disasm 0x5917d0 + data]**: WAE_Starburst on Accessory.Init loads mesh `Starburst` (0x85085c, StarBurst.xom, Bundl09) and mounts it on `Pack_Locator`; Starburst.FuseLit spawns `WXP_Wep_StarburstRocket` at the accessory's `locator1` (inside `Star_burst|fuse`). Same mesh as the held one: 17x44x25 units (0.85 x 2.2 x 1.25 m), nodes Star_burst, fuse, ropes (554 tris: the ropes are mesh), clip FireStarburst 3.5 s moves `fuse` from 2.4 s. WEAPTWK kWeaponStarburst: FlyingGraphicsResourceID Starburst, PayloadGraphicsResourceID / WeaponGraphicsResourceID Sheep, Scale 1.0. Worm clips FireStarburst / FlyStarburst animate Pack_Locator (FireStarburst: a scale-like pop 1.46 to 1.0 at 0.54-0.71 s). **Rider [disasm 0x5891e0, 0x588fd0, 0x588690]**: once attached (+0x230, set with `Starburst.Launched` when the launch time +0x1c0 is reached, 0x588860), each tick copies the payload position (+0x28) and velocity (+0x34) into the worm (WormData +0x38 / +0x50) and turns v̂ into angles: pitch −asin(v̂.y), yaw from the horizontal direction + π, written to WormData Orientation (+0x8c) and to the worm graphic +0x158 / +0x15c (0x59f3f0 / 0x59f410: the node angles of the tumble). No other offset: FlyStarburst itself lays the body along the rocket, which sits roped on the back at Pack_Locator (its `ropes` node), hands free and flapping [data: FlyStarburst channels animate Pack_Locator and both wrists, not the shoulders]. The hands never hold the rocket.
  - **Fuse and speed [disasm + data]**: Init (0x588c10, message 0x40) sets the launch time +0x1c0 = now + 3500 ms, `PackAccessory.Wield` at +500 ms (+0x1c4) and `Starburst.FuseLit` at +2000 ms (+0x1c8, sent by 0x588480), and sends `Worm.OverridePhysics` bit 4 (0x588580): the worm is held where it stands and is not attached. Speed +0x1b0 starts at `Starburst.InitialSpeed` 0 (0x588a9f), so slot 26 (0x5883c0: v = heading × speed, after the Flying orientation step 0x557a20) keeps the rocket still for the whole fuse while the Fly.* inputs (0x558250) already turn its heading. `Input.FirePressed` detonates only once +0x1c0 is cleared (0x589142); `Timer.TurnTimedOut` detonates at any time. At the launch time 0x588860 sets the StarburstCamera, clears +0x1c0, attaches the rider (+0x230) and sends `Starburst.Launched`. Then each frame 0x588ea0: under `Water.Level` (+0xb8) speed −= `Starburst.Acceleration` 0.01 units/ms while > 0; above, speed += 0.01 while |v| < MaxTerminalVelocity 0.45 units/ms (22.5 m/s). Order in the base update 0x57fae0: slot 26 (velocity), expiry, slot 30 (move and contacts), slot 27 (0x5891e0: launch, speed step, rider copy), so a speed step shows the next frame. The contact query (0x5887b0) skips the active worm. Detonate and the removal path (0x588dd0 / 0x588d10) zero the speed and send `PackAccessory.Hide`.
  - **Vapourize [disasm]**: Detonate 0x588dd0 runs the base Detonate (blast, WXP_StarburstExplosion) first, then `Worm.Vapourize` (0x5885f0), then `Timer.EndTurn`. The only handler is WXWormLogicEntity 0x5b07c0 → 0x5ac160: damage of its whole energy (0x5ab7e0, type 0: Worm.Damaged, Turn.Mistake for the active worm), then `WXWormManager.UnspawnWorm`, which WXWormManagerService handles like `Worm.DieQuietly` (0x5b65b8 → 0x5b4af0: the worm is removed). No graphic entity subscribes to Worm.Vapourize, no PARTTWK emitter or string names a vapourize effect, and the unspawn path spawns no death blast, grave or particle: the rider simply vanishes in the Starburst blast.

- **Concrete Donkey [data: WEAPTWK kWeaponConcreteDonkey, PARTTWK; disasm: DonkeyLogicEntity 0x553000..0x553cc0]**: WormDamageMagnitude 80, WormDamageRadius 172 (8.6 m), LandDamageRadius 110.5 (5.525 m), ImpulseMagnitude 0.46 (23 m/s), ImpulseRadius 170 (8.5 m), ImpulseOffset -60 (3 m), Radius 72 (3.6 m = 60 x Scale 1.2, asserted >= 59.9 at 0x553773), LifeTime 8000, DetonatesOnExpiry 1, DetonatesOnLand 0, SinkDepth 80, DetonationFx WXP_Wep_Donkey, DetonationSfx weapons/ConcreteDonkeyImpact, ExpiryFx WXP_ExplosionX_Med (only read on the Payload.Disarm/dud path of Detonate 0x581336, never for the donkey), BounceFx WeaponDonkeyBounce (no PARTTWK container), ArielFx WXP_CrateSpawnLARGE, FxLocator Donkey_L (model node at (0, -93, 0) units). Donkey.Bounce 0.3 and MinBounceSpeed 0.2 are bound in Init (0x5537a5) and never read. CAMTWK Camera.Shake.Length / Magnitude = 0, so the 0x553290 ShakeStart on a smash is inert.
  - **Start (Init 0x553700)**: position = Airstrike.TargetPoint, y += max(Donkey.MinHeight 1500, Land.MaxHeight + Donkey.ExtraHeight 500) units; apex = that y at time 0. DonkeyCamera point = (x, y - ExtraHeight, z + 500) (0x5538d8: `fsub [esp+0x14]` is the ExtraHeight local).
  - **Collider**: sphere of Radius at the entity origin (docs above, Payload start 0x582200). Collision handler 0x553970 (vtable slot 28, replaces 0x57fc00; it checks no DetonatesOn* flag): any contact except a bubble shell (flags 0x3000: only the 0x553be6 tail, state 1 and entity position = contact) does (1) ExplosionMessage 0x518ce0 at the contact position with only LandDamageRadius = Radius (all damage / impulse fields 0) = a crater of 3.6 m at the centre, (2) Explode 0x57f140 at (x, y - Radius, z) with the WEAPTWK values, (3) DetonationFx at the contact (spawn 0x5c1410, props +0x1bc), (4) state 1, hit time stored. No Detonate, so the entity lives on.
  - **Motion (0x553370 / 0x553560)**: state 0: y = apex - 220 (t - tApex)^4 units, t in s, no gravity. State 1 holds 85 ms, then apex = y + 220 h^4, tApex = now + h with h = max(0.75, elapsed x 0.5 first smash / x 0.25 later) = 0.75 s, so it rises 3.48 m and falls back for 1.5 s: one smash per 1.585 s.
  - **End**: LifeTime 8000 ms from the start, DetonatesOnExpiry: Detonate 0x580f10 at the entity position (no Radius offset): DetonationSfx + DetonationFx + Explode 0x57f140. Water: generic payload handler 0x582050 (slot 30 0x552f50 jumps to 0x5827c0; "Payload water" below), SinkDepth 80, Radius 72.
  - **WXP_Wep_Donkey** [data]: WXP_DonkeyStrikeBounce (mesh Particle.WXPMesh7: two flared open cones, 225.6 x 102.6 x 225.6 units at scale 1 (193 x 88 x 193 at its rest scale 0.855), clip WXM_DefSource 0.833 s: Hermite scale keys (0, 0), (0.16663, 0.85498), (0.83301, 1), material alpha 1 -> 0 linear, invisible for the rest of its life; texture opaque only in its lower half of v, SrcAlpha / OneMinusSrcAlpha; 1 particle, life 5000, size 1.2 x 1.3, EmitterSoundFX weapons/ConcreteDonkeyImpact, offset +10 units), WXP_DonkeySpritePuffLG (WXSprite4, 40, offset +20 units, V (0, 2, 0) +- (1.3, 4, 1.3) normalised, life 2000 +- 300, size 35 units shrinking to 0, white -> 0.55 grey, alternate acceleration N 6500 S 2e-6, spin +- 8) and WXP_DonkeySpritePuffHoriz (20, V (0, 0.2, 0) +- (2.2, 0, 2.2), same). WXP_DonkeySpritePuffMid is in no effect list. No fireball, ring or WXP_ExplosionX in the donkey blast. ArielFx WXP_CrateSpawnLARGE = WXP_CrateSpawnLGRings (30 puffs, 22 units, life 1200 +- 500, N 6000, EmitterSoundFX weapons/Cratespawn) + WXP_CrateSpawnDropping (size 0: invisible). Model Donkey: 85 x 167 x 108 units, drawn at Scale 1.2.

### Mystery crates [disasm; values data LOCAL / WEAPTWK / TWEAK]

**Spawn**
- CreateRandomCrate 0x4fa4b0 makes one draw: `(rand >> 15) % (H + W + U + M)`. The shares are SchemeData Health +0x13c, Weapon +0x134, Utility +0x138
  and MysteryChance +0x130, taken in that order.
- MysteryChance per scheme: 0 in Standard, Beginner, Pro, BnG, Shopping, Strategy, Family, Holy Grail, Darksider; 20 in All Action, Kitchen Sink and
  WXD.DefaultSchemeData; 30 in Mega Power; 80 in Mystery.
- The item is drawn at spawn (0x4f4b90): `rand % total` over the `<Item>Mystery.Crate` weights. Ammo −1 gives weight 0, and a total of 0 means no
  crate.
  - Weights in the schemes that drop mystery crates: MineLayer 40, MineTriplet 30, BarrelTriplet 40, Flood 50, Disarm 40, Teleport 60, QuickWalk 70,
    LowGravity 70, DoubleTurnTime 60, Health 70, Damage 40, SuperHealth 40, SpecialWeapon 50, BadPoison 20, GoodPoison 30.
  - Standard's are the same except Flood 10 and SpecialWeapon 40.
- Model `MysteryCrate.xom`. Spawn comment `Comment.MysteryCrateSpawn` (Comment.Mystery.1..5). It lands with `weapons/CrateImpactWeapon` (0x5ca143).
- Crate Spy shows the item's text.

**Collection** (0x5cb5a0 → 0x5ca1f0)
- The commentary panel shows `Text.k<Item>`. BuffaloOfLiesGraphicEntity makes the reveal (`weapons/BuffaloOfLies`); there is no pickup sound.
- Effects:

| item | effect |
|---|---|
| MineLayer | `GameLogic.CreateRandomMine` × MysteryMineLayer.NumMines 5 |
| MineTriplet / BarrelTriplet | min(n, 3) draws of `rand % n` over the mines / drums, repeats dropped; each mine gets Payload.Arm, each drum a 1-unit blast (it explodes) |
| Flood | a FloodLogicEntity: the Flood weapon's rise |
| Disarm | from slot `rand % 66` of the alliance inventory, the first slot with ammo > 0 loses 1 |
| Teleport | up to 3 tries of the mine spot search (15-unit sphere 10 units up), land below, the worm must fit 2 units above it; rope / jetpack / parachute killed |
| QuickWalk | `Worm.VelocityScale` 2 until the turn ends (nothing under JumpingOnly) |
| LowGravity | `Low.Gravity.Multiplier` 0.5 until the turn ends |
| DoubleTurnTime | the running turn timer × 2, at most 99000 ms |
| Health / SuperHealth | +25 / +100 (MysteryHealth / MysterySuperHealth.HealthValue), Worm.Antidote |
| Damage | 25 (MysteryDamage.DamageValue), type 0, ApplyDamage at once |
| SpecialWeapon | `rand % 3` of Concrete Donkey, Holy Hand Grenade, Super Airstrike: +1 unless infinite |
| BadPoison / GoodPoison | Worm.Poison to every worm of the collector's team / of the other teams (by team, not alliance) |

- A blown-up mystery crate explodes like any crate; its item never applies (0x5c9a10).

### Poison Arrow (kWeaponPoisonArrow, PoisonArrowLogicEntity 0x85c934: ParabolicPayloadLogicEntity) [data + disasm]

- **WEAPTWK** [data]: IsLowGravity 1, IsAffectedByWind 1, IsPoweredWeapon 1 (BasePower 0.1, MaxPower 0.533), EndTurnImmediate 1, PostLaunchDelay 0. DetonatesOnWormImpact 1; DetonatesOnLandImpact, OnObjectImpact, AtRest, OnFirePress 0; DetonatesOnExpiry 1; ArmOnImpact 1, StartsArmed 0, LifeTime 0, PreDetonationTime 2000, ArmingRadius 0. All four bounce dampings 0. WormDamageMagnitude, ImpulseMagnitude, WormDamageRadius, LandDamageRadius, ImpulseRadius, ImpulseOffset, Mass 0; WormCollideResponse 0 (default; only read by the walking payloads). **WormImpactDamage 25**, the only weapon with a value. SkimsOnWater 1 (MinSpeedForSkim 0.1, MaxAngleForSkim -0.4, SkimDamping (0.75, -0.75, 0.75)), SinkDepth 5. CameraId PayloadTrackCamera.
- **WormImpactDamage is never read** [disasm]: the field is at container +0x178; every `[reg+0x178]` access in a payload class is a logic-entity field of the subclass, and the only container access is the serialiser getter 0x586540 (descriptor 0x92d1f0). The ExplosionMessage built by Explode 0x57f140 reads +0x15c..+0x170 (damage magnitude, impulse, radii) only, all 0 for the arrow. No Lua reads the name. So the arrow does no direct damage and no knock-back: its Explosion has radius 0, and `Damage.Impulse` is not sent.
- **Impact** [disasm]: vtable +0x80 = 0x586650 (the collision response, called for land from the `Payload.CollideWithLand` event 0x577980 and for worms / objects from 0x581dc0):
  1. ArmOnImpact: Arm 0x57ec20 (armed +0x5c = 1, expiry +0x58 = now + LifeTime = now, `Payload.LogicallyArmed`);
  2. a worm contact (flags bit 0) sets +0x1b9;
  3. 0x577da0 picks DetonatesOnWormImpact (worm), DetonatesOnLandImpact (land) or DetonatesOnObjectImpact; armed and flagged: vtable +0x7c = 0x586520. A worm hit (+0x1b9) goes straight to vtable +0x50, Detonate 0x580f10 (DetonationFx + Explode 0x57f140): **detonates at once at the worm**;
  4. land (flag 0 on the detonate test): 0x580db0 applies the bounce response, damping 0, so the velocity is 0 and the arrow stops (**sticks**), then 0x5758c0 (at rest) schedules `Payload.Expire` at +0x58 (immediately). MsgExpire 0x577980: DetonatesOnExpiry and armed: vtable +0x7c again, no worm: 0x575fb0, PreDetonationTime 2000 > 0 posts `Payload.Detonate` 2000 ms later (0x574c40), which calls Detonate: **the arrow detonates 2 s after it stops**;
  5. then it posts `Payload.Impact`, handled by PayloadGraphicEntity 0x57c5b4, which plays AnimImpact "Hit" (Arrow mesh clip, 1.00 s).
  6. Land.NewShape on a payload at rest (Parabolic HM 0x577f40 → 0x5777f0): 0x574d10 asks whether its land frame (+0x18c / +0x190) still
     exists; gone: acceleration recomputed (vtable +0x48, gravity and wind), "Rested payload on the move again" (active again), events
     re-predicted. The Payload.Detonate posted at the first stop stays queued, so the arrow still detonates 2 s after that stop; a second
     stop posts another one, later [disasm].
- **Poison** [data]: DetonationFx WXP_PoisonArrowPuffOut = WXP_ExploArrow_RingDk (150 ms, EmitterSoundFX weapons/ExplosionBoxed), WXP_ExplosionX_ShockRing, WXP_WhiteoutFlash, WXP_ExploArrow_BallCloud, WXP_ExploArrow_X_Ring, WXP_ArrowPoofColumn, WXP_PoisonArrowGasCloud (8000 ms smoke, EmitterSoundFX weapons/GasLoop after 800 ms, no collision) and **WXP_GasCloud** (the Gas Canister's collision emitter: radius 100 units, ParticleCollisionWormPoisonMagnitude 10, wind). The poison is therefore the Gas Canister's 5 m, 8 s cloud at the detonation point: the hit worm is in it and so is any worm within 5 m ("contagion" of the wiki spec is this cloud).
- **Sounds** [data]: LaunchSfx weapons/BowRelease; ArmSfxLoop weapons/BowImpact (oneshot, 3D linear 10..2000 units, 0 dB) on Arm, i.e. on every impact; DetonationSfx empty, so no ExplosionRegular: the only blast sound is ExplosionBoxed (2D, -2 dB, max 4 playbacks) from WXP_ExploArrow_RingDk. weapons/BowImpactWorm exists in the FEV and nothing references it (not in the exe strings, WEAPTWK, PARTTWK or Lua). ArielFx WXP_ExplosionG_Tail (65535 ms emitter, 10 ms spawn, 500 +- 200 ms sprites): the flight trail.

### Landmine mesh (PayloadGraphicEntity) [disasm + data]
- Init loads `Landmine` (+0x24) and, for that mesh name only, `LandmineLow` (+0x28) (0x57cc3d).
- `Payload.LogicallyArmed` (Arm 0x57ec20) plays WEAPTWK `AnimArm` `MineOn` on +0x24 from t = 0 (0x57b150 -> 0x6a0730) and loops it (0x6a0470 arg 1). `Payload.Disarm` (a dud at Detonate 0x58132a, or the removal 0x581740) turns the loop off (0x57d05d, arg 0), so the current pass ends.
- Each update (0x57d8e0) shows `LandmineLow` when the squared distance to the render camera ([0x95a100]+8 vfunc 0x38) is over 90000 (300 units, 15 m), else `Landmine` (0x57d9dd). `LandmineLow` never gets the clip and stays on child 0.
- `MineOn` only keys the `XChildSelector`: lamp off / on every 0.25 s (docs/w4m/formats.md §6 "XChildSelector").

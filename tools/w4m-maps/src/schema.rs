// XOM field lists (`name:type`, `[]` array, `?` absent from some files) of the containers the mission scripts read: the exe's
// Serialize records, most derived class first (tools/w4m-re pe.py schema). Keys: the XOM type names, cut to 31 characters as in the files.
pub const CLASSES: &[(&str, &str)] = &[
    ("WormDataContainer", "Name:str Active:bool PlayedInGame:bool Position:vec3 ForcedCameraOffset:vec3 Velocity:vec3 Aftertouch:vec3 \
      InputImpulse:vec3 Acceleration:vec3 SupportNormal:vec3 Orientation:vec3 AngularVelocity:vec3 ControlX:f32 ControlY:f32 \
      LastLogicalUpdate:u32 SupportFrame:u16 SupportVoxel:u16 WeaponAngle:f32 WeaponFuse:u32 WeaponIsBounceMax:bool \
      WeaponHerd:u32 TeamIndex:u8 PositionInTeam:u8 PhysicsOverride:u32 Flags:u32 PhysicsState:enum WeaponIndex:enum \
      InitialEnergy:i32 Energy:u16 CPUFixedWeapon:u8 CPUActionRadius:u16 ArtilleryMode:bool PoisonRate:u32 PendingPoison:u32 \
      PlaceWormAtPosition:bool PoolDamagePending:i32[] SfxBankName:str Spawn:str IsParachuteSpawn:bool \
      IsAllowedToTakeTurn:bool GunWobblePitch:f32 GunWobbleYaw:f32 LipSynchBank:u8 ATT_Hat:str ATT_Glasses:str ATT_Gloves:str \
      ATT_Tash:str MovedByImpulse:bool GraphicalOrientation:vec3 Scale:vec3 LastCollisionNormal:vec3 LogicAnimState:u32 \
      SlopeAngle:f32 DamagePending:i32 CurrentEnergy:i32 IsAfterTouching:bool AfterTouchVector:vec3 IsHatWearer:bool \
      IsQuickWalking:bool AllowBazooka:i8 AllowGrenade:i8 AllowClusterGrenade:i8 AllowAirstrike:i8 AllowDynamite:i8 \
      AllowHolyHandGrenade:i8 AllowBananaBomb:i8 AllowLandmine:i8 AllowShotgun:i8 AllowBaseballBat:i8 AllowProd:i8 \
      AllowFirePunch:i8 AllowHomingMissile:i8 AllowFlood:i8 AllowSheep:i8 AllowGasCanister:i8 AllowOldWoman:i8 \
      AllowConcreteDonkey:i8 AllowSuperSheep:i8 AllowGirder:i8 AllowBridgeKit:i8 AllowNinjaRope:i8 AllowParachute:i8 \
      AllowLowGravity:i8 AllowTeleport:i8 AllowJetpack:i8 AllowSkipGo:i8 AllowSurrender:i8 AllowChangeWorm:i8 AllowRedbull:i8 \
      AllowArmour:i8 AllowWeaponFactoryWeapon:i8 AllowStarburst:i8 AllowAlienAbduction:i8 AllowFatkins:i8 AllowScouser:i8 \
      AllowNoMoreNails:i8 AllowPipe:i8 AllowPoisonArrow:i8 AllowSentryGun:i8 AllowSniperRifle:i8 AllowSuperAirstrike:i8 \
      AllowBubbleTrouble:i8 AllowBinoculars:i8 TeleportIn:bool IsEmotional:bool HasDrunkRedbull:bool Armoured:bool"),
    ("TeamDataContainer", "Name:str Player:str Active:bool WormCountStart:u8 TeamColour:u8 TeamMustSurvive:bool AlliedGroup:u8 \
      DisableCPUAttack:bool IsAIControlled:bool RoundsWon:u8 GraveIndex:u8 WormsCountEnd:u8 Surrendered:bool ScorePoints:u32 \
      FlagGfxName:str IsCrateSpyActive:bool Skill:u8 IsLocal:bool ATT_Hat:str ATT_Glasses:str ATT_Gloves:str ATT_Tash:str \
      SfxBankName:str WormpotSuperWeapon:enum DefaultCameraDistance:enum RankedPoints:u32[] PlayerPoints:u32 \
      WeeklyPlayerPointsGained:i32 WeeklyInitialPlayerPoints:u32 NumKills:u32 NumDeaths:u32 DamageTaken:u32 DamageDealt:u32 \
      GamesPlayed:u32 GamesWon:u32"),
    ("WeaponInventory", "Bazooka:i8 Grenade:i8 ClusterGrenade:i8 Airstrike:i8 Dynamite:i8 HolyHandGrenade:i8 BananaBomb:i8 Landmine:i8 Shotgun:i8 \
      BaseballBat:i8 Prod:i8 FirePunch:i8 HomingMissile:i8 Flood:i8 Sheep:i8 GasCanister:i8 OldWoman:i8 ConcreteDonkey:i8 \
      SuperSheep:i8 Girder:i8 BridgeKit:i8 NinjaRope:i8 Parachute:i8 Teleport:i8 Jetpack:i8 SkipGo:i8 Surrender:i8 \
      ChangeWorm:i8 Redbull:i8 WeaponFactoryWeapon:i8 Starburst:i8 AlienAbduction:i8 Fatkins:i8 Scouser:i8 NoMoreNails:i8 \
      Pipe:i8 PoisonArrow:i8 SentryGun:i8 SniperRifle:i8 SuperAirstrike:i8 BubbleTrouble:i8 Binoculars:i8"),
    ("WeaponDelays", "Bazooka:u8 Grenade:u8 ClusterGrenade:u8 Airstrike:u8 Dynamite:u8 HolyHandGrenade:u8 BananaBomb:u8 Landmine:u8 Shotgun:u8 \
      BaseballBat:u8 Prod:u8 FirePunch:u8 HomingMissile:u8 Flood:u8 Sheep:u8 GasCanister:u8 OldWoman:u8 ConcreteDonkey:u8 \
      SuperSheep:u8 Girder:u8 BridgeKit:u8 NinjaRope:u8 Parachute:u8 LowGravity:u8 Teleport:u8 Jetpack:u8 SkipGo:u8 \
      Surrender:u8 ChangeWorm:u8 Redbull:u8 WeaponFactoryWeapon:u8 Starburst:u8 AlienAbduction:u8 Fatkins:u8 Scouser:u8 \
      NoMoreNails:u8 Pipe:u8 PoisonArrow:u8 SentryGun:u8 SniperRifle:u8 SuperAirstrike:u8 BubbleTrouble:u8 Binoculars:u8"),
    ("SchemeData", "Name:str Permanent:bool Lock:str Airstrike:ref BananaBomb:ref BaseballBat:ref Bazooka:ref ClusterGrenade:ref \
      ConcreteDonkey:ref CrateShower:ref CrateSpy:ref DoubleDamage:ref Dynamite:ref FirePunch:ref GasCanister:ref Girder:ref \
      Grenade:ref HolyHandGrenade:ref HomingMissile:ref Jetpack:ref Landmine:ref NinjaRope:ref OldWoman:ref Parachute:ref \
      Prod:ref SelectWorm:ref Sheep:ref Shotgun:ref SkipGo:ref SuperSheep:ref Redbull:ref Flood:ref Armour:ref \
      WeaponFactoryWeapon:ref AlienAbduction:ref Fatkins:ref Scouser:ref NoMoreNails:ref PoisonArrow:ref SentryGun:ref \
      SniperRifle:ref SuperAirstrike:ref BubbleTrouble:ref Starburst:ref Surrender:ref Binoculars:ref MineLayerMystery:ref \
      MineTripletMystery:ref BarrelTripletMystery:ref FloodMystery:ref DisarmMystery:ref TeleportMystery:ref \
      QuickWalkMystery:ref LowGravityMystery:ref DoubleTurnTimeMystery:ref HealthMystery:ref DamageMystery:ref \
      SuperHealthMystery:ref SpecialWeaponMystery:ref BadPoisonMystery:ref GoodPoisonMystery:ref AssistedShotSettings:ref? \
      AssistedShotLevel:str ArtileryMode:i32 TeleportIn:i32 Wins:i32 WormSelect:i32 WormHealth:i32 RoundTime:i32 TurnTime:i32 \
      Objects:i32 RandomCrateChancePerTurn:i32 MysteryChance:i32 WeaponChance:i32 UtilityChance:i32 HealthChance:i32 \
      HealthInCrates:i32 Stockpiling:i32 SuddenDeath:i32 WaterSpeed:i32 DisplayTime:i32 LandTime:i32 RopeTime:i32 \
      FallDamage:i32 HotSeat:i32 Special:i32 MineFuse:i32 HelpPanelDelay:i32 MineFactoryOn:bool TelepadsOn:bool \
      WindMaxStrength:i32"),
    ("GameInitData", "NumberOfTeams:u32 AllyToStart:i32 AllyTeam0:i32 AllyTeam1:i32 AllyTeam2:i32 AllyTeam3:i32 T1_Name:str T1_Player:str \
      T1_NumOfWorms:u32 T1_W1_Name:str T1_W2_Name:str T1_W3_Name:str T1_W4_Name:str T1_W5_Name:str T1_W6_Name:str T1_Skill:i32 \
      T1_Grave:i32 T1_SWeapon:i32 T1_Flag:str T1_Speech:str T1_IsLocal:bool T1_AlliedGroup:u32 T1_FirstWorm:i32 \
      T1_Handicap:i32 T1_HatAttachment:str T1_GlovesAttachment:str T1_GlassesAttachment:str T1_TashAttachment:str \
      T1_CustomWeapon:ref T1_InvertY:bool? T1_InvertX:bool? T1_InvertYFP:bool? T2_Name:str T2_Player:str T2_NumOfWorms:u32 \
      T2_W1_Name:str T2_W2_Name:str T2_W3_Name:str T2_W4_Name:str T2_W5_Name:str T2_W6_Name:str T2_Skill:i32 T2_Grave:i32 \
      T2_SWeapon:i32 T2_Flag:str T2_Speech:str T2_IsLocal:bool T2_AlliedGroup:u32 T2_FirstWorm:i32 T2_Handicap:i32 \
      T2_HatAttachment:str T2_GlovesAttachment:str T2_GlassesAttachment:str T2_TashAttachment:str T2_CustomWeapon:ref \
      T2_InvertY:bool? T2_InvertX:bool? T2_InvertYFP:bool? T3_Name:str T3_Player:str T3_NumOfWorms:u32 T3_W1_Name:str \
      T3_W2_Name:str T3_W3_Name:str T3_W4_Name:str T3_W5_Name:str T3_W6_Name:str T3_Skill:i32 T3_Grave:i32 T3_SWeapon:i32 \
      T3_Flag:str T3_Speech:str T3_IsLocal:bool T3_AlliedGroup:u32 T3_FirstWorm:i32 T3_Handicap:i32 T3_HatAttachment:str \
      T3_GlovesAttachment:str T3_GlassesAttachment:str T3_TashAttachment:str T3_CustomWeapon:ref T3_InvertY:bool? \
      T3_InvertX:bool? T3_InvertYFP:bool? T4_Name:str T4_Player:str T4_NumOfWorms:u32 T4_W1_Name:str T4_W2_Name:str \
      T4_W3_Name:str T4_W4_Name:str T4_W5_Name:str T4_W6_Name:str T4_Skill:i32 T4_Grave:i32 T4_SWeapon:i32 T4_Flag:str \
      T4_Speech:str T4_IsLocal:bool T4_AlliedGroup:u32 T4_FirstWorm:i32 T4_Handicap:i32 T4_HatAttachment:str \
      T4_GlovesAttachment:str T4_GlassesAttachment:str T4_TashAttachment:str T4_CustomWeapon:ref T4_InvertY:bool? \
      T4_InvertX:bool? T4_InvertYFP:bool?"),
    ("AIParametersContainer", "WeightingThisWormValue:f32 WeightingWormVital:f32 WeightingWormHealth:f32 WeightingWormPoisoned:f32 \
      WeightingWormExchange:f32 WeightingWormLastInTeam:f32 WeightingWormNearbyWorms:f32 WeightingWormMilesAway:f32 \
      WeightingAttack:f32 WeightingExplosiveSecondaryDamage:f32 WeightingExplosiveNearbyThreat:f32 WeightingKillTarget:f32 \
      WeightingMultipleUse:f32 WeightingPunchThroughLand:f32 WeightingPreferNearbyTargets:f32 \
      WeightingBestMissShotMultipleUse:f32 WeightingPreferVariety:f32 ShotErrorProjectile:f32 ShotErrorDirect:f32 \
      ShotErrorDirectNonStrafe:f32 DelayAtStart:f32 DelayBeforeFire:f32 ProjectileSweetSpotDistance:f32 \
      StrikeSweetSpotDistance:f32 ClusterDistanceAboveTarget:f32 MaximumDistanceTargetConsidered:f32 \
      ForbidShotsWhenMightAffectAITrigger:f32 WeightingPlanScoreRandomise:f32 AddScoreMoveIfNotMoved:f32 AddScoreMove:f32 \
      DelayBeforeFirstMove:f32 DelayBeforeNonFirstMove:f32 RandomSmallMoveRange:f32 ReduceMoveScoreFurtherThan:f32 \
      AddScoreCollectSomething:f32 WeightCollectWeapon:f32 WeightCollectHealth:f32 WeightCollectHealthWhenPoisoned:f32 \
      WeightCollectUtility:f32 AllowCollectNormalCrate:bool LikeToCollectHealthWhenHealthBelow:f32 \
      ReduceMoveScoreIfTimeLeftLessThan:f32 ForbidMoveIfWouldLeaveTimeLessThan:f32 WeightingPreferAttackHumans:f32 \
      MemoryImproveAccuracyMatchRadius:f32 MemoryImproveAccuracyEffect:f32 TargetDetailObject:str AddScoreTeleport:f32 \
      WeightStrikeSecondaryTarget:f32 ConsidersStrikeThrustDirection:bool ShotErrorStrike:f32 StrafeProgressiveErrorScale:f32 \
      MovementJumpForwardAllowed:bool MovementJumpBackflipAllowed:bool MovementJumpError:f32 WeightTeleportDefensivePos:f32 \
      WeightTeleportOffensivePos:f32 WeightRetreatDefensivePos:f32 WeightRetreatOffensivePos:f32 \
      ImaginaryCrateDetailObject:str AttractorDetailObject:str AttractorZoneRadius:f32 MortarMaximumAimAngleAllowed:f32 \
      JetpackAboutToCrossLineLookAhead:f32 PrefBazooka:f32 PrefGrenade:f32 PrefClusterGrenade:f32 PrefAirstrike:f32 \
      PrefDynamite:f32 PrefHolyHandGrenade:f32 PrefBananaBomb:f32 PrefLandmine:f32 PrefShotgun:f32 PrefBaseballBat:f32 \
      PrefProd:f32 PrefFirePunch:f32 PrefHomingMissile:f32 PrefFlood:f32 PrefSheep:f32 PrefGasCanister:f32 PrefOldWoman:f32 \
      PrefConcreteDonkey:f32 PrefSuperSheep:f32 PrefGirder:f32 PrefBridgeKit:f32 PrefNinjaRope:f32 PrefParachute:f32 \
      PrefLowGravity:f32 PrefTeleport:f32 PrefJetpack:f32 PrefSkipGo:f32 PrefSurrender:f32 PrefChangeWorm:f32 PrefRedbull:f32 \
      PrefWeaponFactoryWeapon:f32 PrefStarburst:f32 PrefAlienAbduction:f32 PrefFatkins:f32 PrefScouser:f32 PrefNoMoreNails:f32 \
      PrefPipe:f32 PrefPoisonArrow:f32 PrefSentryGun:f32 PrefSniperRifle:f32 PrefSuperAirstrike:f32 PrefBubbleTrouble:f32 \
      PrefWeaponProjectile:f32 PrefWeaponDirect:f32 PrefWeaponStrike:f32 PrefWeaponMelee:f32 PrefWeaponAnimal:f32"),
    ("CrateDataContainer", "Type:str Contents:str NumContents:i32 Index:i32 LifetimeSec:f32 GroundSnap:i32 Parachute:i32 Spawn:str FallSpeed:f32 \
      Gravity:i32 TeamDestructible:i32 TeamCollectable:i32 UXB:i32 Hitpoints:i32 Pushable:i32 RandomSpawnPos:i32 \
      CanDropFromChute:i32 WaitTillLanded:i32 TrackCam:i32 Scale:f32 Showered:i32 DelayMillisec:i32 LifetimeTurns:i32 \
      AddToWormInventory:i32 CustomGraphic:str"),
    ("TriggerDataContainer", "Spawn:str Radius:f32 Index:i32 TeamCollect:i32 TeamDestroy:i32 HitPoints:i32 SheepCollect:i32 PayloadCollect:i32 \
      GirderCollect:i32 WormCollect:i32 AffectsAI:i32"),
    ("WXFE_UnlockableItem", "Type:enum State:enum DescriptionName:str Value:u32 UnlockRequirements:str[] UnlockRequirementsMet:bool DLCPack:u32 \
      UnlockedAttachments:str[]"),
    ("MineFactoryContainer", "NumMineActivation:u8 NumTurnsInactive:u8 SafeRadiusPadding:f32 MineVelocityX:f32? MineVelocityY:f32? MineVelocityZ:f32? \
      DamageMagnitude:f32 ImpulseMagnitude:f32 WormDamageRadius:f32 LandDamageRadius:f32 ImpulseRadius:f32"),
    ("FlyingPayloadWeaponPropertiesCo", "MaxPitchSpeed:f32 PitchAcceleration:f32 Inertia:f32 MaxYawSpeed:f32 YawAcceleration:f32 MaxRollSpeed:f32 \
      RollAcceleration:f32 FlyingSpeed:f32 MaxWorldYawSpeed:f32 BlendTowardsHorizontal:f32 BlendTowardsVertical:f32 \
      MaxAutoRollSpeed:f32 AutoRollAcceleration:f32 AutoRollDelay:u32 YawAnimSpeed:f32 RollAnimSpeed:f32 AnimYaw:str \
      AnimRoll:str FlyingGraphicsResourceID:str FlyingLaunchSfx:str FlyingLoopSfx:str AnimFly:str AnimFall:str \
      SmallJumpHorizontalSpeed:f32 SmallJumpMinVerticalSpeed:f32 SmallJumpMaxVerticalSpeed:f32 BigJumpHorizontalSpeed:f32 \
      BigJumpMinVerticalSpeed:f32 BigJumpMaxVerticalSpeed:f32 MaxDrop:f32 ReturnProbability:f32 MinTimeForSafeJump:u32 \
      IsAimedWeapon:bool IsPoweredWeapon:bool IsTargetingWeapon:bool IsControlledBomber:bool IsBomberWeapon:bool \
      IsDirectionalWeapon:bool IsHoming:bool IsLowGravity:bool IsLaunchedFromWorm:bool HasAdjustableFuse:bool \
      HasAdjustableBounce:bool HasAdjustableHerd:bool IsAffectedByGravity:bool IsAffectedByWind:bool EndTurnImmediate:bool \
      UseParabolicRetical:bool ColliderFlags:u32 CameraId:str[] PayloadGraphicsResourceID:str Payload2ndGraphicsResourceID:str \
      Scale:f32 Radius:f32 AnimTravel:str AnimSmallJump:str AnimBigJump:str AnimArm:str AnimSplashdown:str AnimSink:str \
      AnimIntermediate:str AnimImpact:str DirectionBlend:f32 FuseTimerGraphicOffset:f32 FuseTimerScale:f32 BasePower:f32 \
      MaxPower:f32 MinTerminalVelocity:f32 MaxTerminalVelocity:f32 LogicalLaunchZOffset:f32 LogicalLaunchYOffset:f32 \
      OrientationOption:u32 SpinSpeed:f32 InterPayloadDelay:u32 MinAimAngle:f32 MaxAimAngle:f32 DetonatesOnLandImpact:bool \
      DetonatesOnExpiry:bool DetonatesOnObjectImpact:bool DetonatesOnWormImpact:bool DetonatesAtRest:bool \
      DetonatesOnFirePress:bool DetonatesWhenCantJump:bool DetonateMultiEffect:enum WormCollideResponse:enum \
      WormDamageMagnitude:f32 ImpulseMagnitude:f32 WormDamageRadius:f32 LandDamageRadius:f32 ImpulseRadius:f32 \
      ImpulseOffset:f32 Mass:f32 WormImpactDamage:f32 MaxPowerUp:u32 TangentialMinBounceDamping:f32 \
      ParallelMinBounceDamping:f32 TangentialMaxBounceDamping:f32 ParallelMaxBounceDamping:f32 SkimsOnWater:bool \
      MinSpeedForSkim:f32 MaxAngleForSkim:f32 SkimDamping:vec3 SinkDepth:f32 NumStrikeBombs:u32 NumBomblets:u32 \
      BombletMaxConeAngle:f32 BombletMaxSpeed:f32 BombletMinSpeed:f32 BombletWeaponName:str FxLocator:str ArielFx:str \
      DetonationFx:str DetonationSfx:str ExpiryFx:str SplashFx:str SplishFx:str SinkingFx:str BounceFx:str StopFxAtRest:bool \
      BounceSfx:str PreDetonationSfx:str ArmSfx1Shot:str ArmSfxLoop:str LaunchSfx:str LoopSfx:str BigJumpSfx:str WalkSfx:str \
      TrailBitmap:str TrailLocator1:str TrailLocator2:str TrailLength:u32 AttachedMesh:str AttachedMeshScale:f32 \
      StartsArmed:bool ArmOnImpact:bool ArmingCourtesyTime:u32 PreDetonationTime:u32 ArmingRadius:f32 LifeTime:i32 \
      IsFuseDisplayed:bool DisplayName:str WeaponGraphicsResourceID:str WeaponType:enum DefaultPreference:f32 \
      CurrentPreference:f32 LaunchDelay:u32 PostLaunchDelay:u32 FirstPersonOffset:vec3 FirstPersonScale:vec3 \
      FirstPersonFiringParticleEffect:str HoldParticleFX:str DisplayInFirstPerson:bool CanBeFiredWhenWormMoving:bool \
      RumbleLight:u8 RumbleHeavy:u8 CanBeUsedWhenTailNailed:bool RetreatTimeOverride:i32 WXAnimDraw:str WXAnimAim:str \
      WXAnimFire:str WXAnimHolding:str WXAnimEndFire:str WXAnimTaunt:str WXAnimTargetSelected:str HoldLoopSfx:str EquipSfx:str"),
    ("EFMV_CreateExplosionEventContai", "Location:str WormDamageMagnitude:f32 ImpulseMagnitude:f32 WormDamageRadius:f32 LandDamageRadius:f32 ImpulseRadius:f32 \
      ParticleEffect:str ImpulseOffset:f32 Tag:str Time:u32 Critical:bool"),
    ("EFMV_SpawnWormEventContainer", "WormId:u32 DataId:str Tag:str Time:u32 Critical:bool"),
    ("EFMV_RaiseWaterEventContainer", "Delta:i32 Tag:str Time:u32 Critical:bool"),
    ("EFMV_DeleteLandframeEventContai", "Code:str Tag:str Time:u32 Critical:bool"),
];

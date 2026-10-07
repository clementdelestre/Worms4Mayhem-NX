# W4M turn flow and timing

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 6. Turn and phases
Confidence tags: [data]=decompiled .lub constants/flow, [disasm]=exe, [assumed]=inference.

### 1. stdlib.lub: base turn state machine [data]
Engine calls Lua globals named `<Msg>_<Event>` (dots -> underscores) as callbacks (e.g. message "Timer.HotSeatTimedOut" -> `Timer_HotSeatTimedOut`) [assumed from naming; check exe].
Functions (all globals, set in main chunk):
- StartFirstTurn: WaitUntilNoActivity=false; Send "Timer.StartGame"; StartTurn().
- StartTurn: done_once_per_turn_functions=false; Send "GameLogic.ActivateNextWorm" (worm selection is engine side); "Timer.StartHotSeatTimer"; SetWind(); Send "GameLogic.Turn.Started"; TurnStarted() (hook); RunAILogic().
- RunAILogic: Send "AI.PerformDefaultAITurn", "AI.ExecuteActions" (engine no-ops for human, assumed).
- Timer_HotSeatTimedOut: Send "Timer.StartTurn" (hot-seat "get ready" phase -> turn timer).
- GameLogic_EndTurn_Immediate: Send Weapon.Delete, Utility.Delete, Timer.EndTurn, Weapon.DisableWeaponChange; EndTurn().
- Timer_RetreatTimedOut: EndTurn().
- Worm_Damaged_Current (current worm hurt during its turn): Weapon.Delete, Utility.Delete, Timer.EndRetreatTimer, Timer.EndTurn, Weapon.DisableWeaponChange; EndTurn().
- Timer_TurnTimedOut: Weapon.Delete, Utility.Delete, Weapon.DisableWeaponChange; EndTurn().
- EndTurn: Send "GameLogic.EndTurn"; if GetData("ObjectCount.Active")==0 -> Send "Timer.StartPostActivity" else WaitUntilNoActivity=true.
- GameLogic_NoActivity (engine event when active-object count reaches 0): if WaitUntilNoActivity -> false, Send "Timer.StartPostActivity".
- Timer_PostActivityTimedOut: Send "GameLogic.AboutToApplyDamage", "GameLogic.ApplyDamage"; CheckActivity().
- CheckActivity: ObjectCount.Active==0 ? DoPostActivity() : WaitUntilNoActivity=true.
- DoPostActivity (two passes):
  - pass 1 (done_once_per_turn_functions==false): Send Net.DisableAllInput, Worm.ApplyPoison, GameLogic.AboutToApplyDamage, GameLogic.ApplyDamage; SetData("DoubleDamage",0); DoOncePerTurnFunctions(); flag=true; if GetData("FCS.GameOver")==0 -> CheckActivity() (loops back via timer/NoActivity until settled).
  - pass 2 (flag true): Send "GameLogic.Turn.Ended"; TurnEnded().
- TurnEnded: CheckOneTeamVictory().
- CheckOneTeamVictory: Send WormManager.GetActiveAlliances; AllianceCount==0 -> RoundOver(); Send GameLogic.Draw; ==1 -> RoundOver(); WormManager.GetSurvivingTeam; SendIntMessage("GameLogic.Win", GetData SurvivingTeamIndex); else StartTurn().
- SetWind -> SelectRandomWind: Wind.Speed = (Wind.Cap/10)*r*r*Wind.MaxSpeed (r=RandomNumber.Float, squared -> biased low); Wind.Direction = r2*2*3.14.
- Empty hooks: TurnStarted, DoOncePerTurnFunctions, RoundOver, SetWormpotModes.

Phase sequence: StartTurn -> [hotseat timer] -> Timer.StartTurn -> [turn timer; fire] -> retreat timer (engine) -> Timer_RetreatTimedOut -> EndTurn -> wait ObjectCount.Active==0 -> Timer.StartPostActivity -> Timer_PostActivityTimedOut -> ApplyDamage -> settle -> DoPostActivity pass1 (poison, damage, once-per-turn: sudden death/crates/minefactory) -> settle -> pass2 -> Turn.Ended -> victory check -> StartTurn.
Note: ApplyDamage sent twice per end (PostActivityTimedOut + DoPostActivity pass1) [data].

### 2. stdvs.lub (multiplayer/versus overrides, loaded after stdlib) [data]
- Initialise: StartedSuddenDeath=false; SetupScheme(); lib_SetupMultiplayerWormsAndTeams(); Send WormManager.Reinitialise; lib_SetupMinesAndOildrums(); if GM.SchemeData.RoundTime==0 -> StartSuddenDeath() (return if FCS.GameOver!=0); MineFactoryOn -> GameLogic.CreateRandMineFactory; TelepadsOn -> GameLogic.PlaceTelepads; SetData Camera.StartOfTurnCamera="Default"; SetWormpotModes(); WaitingForStartFirstTurn=false; StartFirstTurn().
- SetupScheme: scheme(GM.SchemeData) -> data keys: FallDamage==0 -> Send GameLogic.SetNoFallDamage; HUD.Clock.DisplayRoundTime=DisplayTime; Crate.HealthInCrates=HealthInCrates; **DefaultRetreatTime=LandTime** (scheme field "LandTime" is the retreat time); Land.Indestructable=GetData FE.Land.Ind; Wind.Cap=WindMaxStrength; SetupInventoriesAndDelays(); SetupTeleportIn(); HotSeatTime=HotSeat; TurnTime=TurnTime; RoundTime=RoundTime.
- SetupInventoriesAndDelays: stockpiling 0/1/2 copy Inventory.Alliance.Default / Inventory.StockpileNN into Inventory.AllianceNN; GameLogic.AddInventory(.Arg0/.Arg1); scheme Special==1 -> IncrementAlliedInventory(Tn_AlliedGroup, Tn_SWeapon) via GameLogic.IncrementInventory.
- DoOncePerTurnFunctions (override): if AllianceCount>1: CheckSuddenDeath(); Send GameLogic.DropRandomCrate; GameLogic.StartMineFactory; DoWormpotOncePerTurnFunctions().
- TurnStarted (override): scheme WormSelect==1 -> Send "WormSelect.OptionSelected"; TeleportIn().
- TeleportIn: if ActiveWormIndex!=-1 and worm container .TeleportIn -> Send WormManager.TeleportIn. SetupTeleportIn sets Worm.DataNN.TeleportIn from scheme TeleportIn.
- CheckSuddenDeath: if AllianceCount>1: if RoundTimeRemaining==0 and not started -> StartSuddenDeath(); Send GameLogic.AboutToWaterRise; Water.Level += Water.RiseSpeed.Current (every turn end; LOCAL Water.RiseSpeed.Current is 0 until StartSuddenDeath sets it) [data].
- StartSuddenDeath: StartedSuddenDeath=true; scheme SuddenDeath: 0 -> Comment.SuddenDeath + lib_SetAllWormsEnergy(1); 1 -> Comment.SuddenDeath only; 2 -> RoundOver + GameLogic.Draw. WaterSpeed 0..3 -> Water.RiseSpeed.Current = 0 / Water.RiseSpeed.Slow / Medium / Fast.
- GameLogic_NoActivity (override): also starts first turn if WaitingForStartFirstTurn.
- Worm_Died: if AllianceCount<2 -> force end turn (Weapon.Delete, Utility.Delete, Timer.EndTurn, Weapon.DisableWeaponChange, EndTurn()).
- RoundOver -> Stockpile() (copy AllianceNN -> StockpileNN). Timer_GameTimedOut: empty.

### 3. lib_help.lub: helpers [data]
Commentary (lib_Comment, lib_Display{Failure,Success,SuddenDeath}Comment: Comment.Sdeath.1-6, Miss.Generic.Win/Lose1-5), particles, RNG (RandomNumber.Get/.Uint/.Float), airstrike, anim (Worm.ResetAnim, Worm.QueueAnim, Worm.ScriptAnim), lib_Deathmatch{Mission,Challenge}TurnEnded (victory/failure -> GameLogic.Mission.Success/Failure, Challenge.*, EFMV.GameOverMovie="Outro", else StartTurn), camera shake, lib_SetAllWormsEnergy, explosions (Explosion.Construct), container names (Worm.Data00-17, Team.Data00-03, Inventory.{Team,Alliance,Stockpile}00-03, Inventory.Worm00-15), crate/trigger spawn params, worm/team setup, scheme->inventory (lib_SetupDefaultInventoryAndDelays), mines/oildrums (Mine.MinFuse/MaxFuse, GameLogic.CreateRandomMine/Oildrum).

### 4. Death queue / retreat override: not in any .lub (grep DeathQueue, TimeToDie, RetreatTimeOverride: 0 hits) -> exe only. [data]

### 5. Exe: script binding [disasm]
- 0x4e99d0 (script service level-start): loads libs string "stdlib,lib_help", then level script named by data key "GameLogic.CurrentScript", entry function "Initialise"; flag set if script name == "stdvs". Passes 4 tables:
  - 0x921368: engine messages forwarded to Lua as callbacks `A.B.C` -> `A_B_C`: GameLogic.Turn.Ended, Timer.EndGame, Timer.HotSeatTimedOut, Timer.RetreatTimedOut, Worm.Damaged, Worm.Damaged.Current, Timer.TurnTimedOut, Timer.PostActivityTimedOut, Timer.GameTimedOut, Bomber.AnimsComplete, GameLogic.NoActivity, Crate.Collected/Destroyed/Sunk, Particle.StartEvent/EndEvent, Worm.Died, Payload.Deleted, Trigger.Collected/Damaged/Destroyed/SheepCollected/PayloadCollected/GirderCollected, String.Substitute(Indirect), GameLogic.EndTurn.Immediate, EFMV.Terminated, OilDrum.Deleted, Game.BriefingDialogNowOff, Weapon.Selected, GameLogic.WeaponPanelOpened/Closed, Weapon.Fired, Land.NewShape, Camera.Path.Reached.Knot.
  - 0x9213fc: GameLogic.Timer0..9 (script timers).
  - 0x921428: GameLogic.PauseGame, "Camera.Disable,Track". 0x921434: GameLogic.ArtilleryMode, TeamCount.
  - 0x921440: Lua function names known to engine: TurnStarted, TurnEnded, SetWind, DoOncePerTurnFunctions, SetWormpotModes, lib_QuickSetupMultiplayerWormsAndTeams, Worm_Damaged_Current (role assumed: hooks engine may call / network-filtered).
- Message IDs: per-TU static objects, init funcs 0x7dxxxx-0x7fxxxx = `push "Name"; mov ecx,G; call 0x68bb9f` (register). Dispatcher compares msg against G via 0x68bcce. Name of a handle: `pe.py msg 0xVA`; all handles of a name: `pe.py msg '^Name$'` (section 20).

### 6. Exe: who handles what [disasm, class names from assert strings]
- TimerLogicEntity (.\TimerLogicEntity.cpp): 0x50f980 HandleMessage (Timer.StartGame/StartTurn/EndTurn/StartHotSeatTimer/StartPostActivity/StartRetreatTimer/EndRetreatTimer; asserts iRoundTime in {0,-1}..10000?, iTurnTime<=10000 -> times in game units checked vs. ms/??; strings "TimerLogicEntity MsgStartTurn", "hot seat canceled", Turn.Boring, Turn.PayloadFired). 0x50f100 tick: posts Timer.HotSeatTimedOut ("hot seat timed out"), Timer.TurnTimedOut, Timer.PostActivityTimedOut, Timer.RetreatTimedOut. 0x50f4e0 binds data keys to members: RoundTime, TurnTime, HotSeatTime(+0x7c), PostActivityTime(+0x80), RetreatTime(+0x84), RoundTimeRemaining, TurnTimeRemaining, HotSeatTimeRemaining, PostActivityTimeRemaining, RetreatTimeRemaining, ClockDisplayMode, ElapsedRoundTime.
- Defaults (Data/Tweak/LOCAL.XOM, XIntResourceDetails, ms) [data]: TurnTime 45000, RoundTime 1800000, HotSeatTime 10000, PostActivityTime 2400, DefaultRetreatTime 3000, RetreatTime 0. stdvs overrides from scheme (DefaultRetreatTime<-LandTime, HotSeatTime<-HotSeat, TurnTime, RoundTime). Challenges set RetreatTime/DefaultRetreatTime/PostActivityTime 0; Wormpot NoRetreatTime -> DefaultRetreatTime=RetreatTime=0.
- Retreat time per weapon [disasm]: on fire, weapon logic entities do r=GetData("DefaultRetreatTime"); if props.RetreatTimeOverride (i32 @+0x50 in weapon properties, schema field #0x10) >= 0 then r=override; SetData("RetreatTime", r). Seen in GunWeaponLogicEntity 0x55cff0, MeleeWeaponLogicEntity 0x568860, NewSentrygunWeaponLogicEntity 0x56e6d0, PayloadWeaponLogicEntity 0x582d70; AI planner 0x4a1cf0 reads DefaultRetreatTime. FloodLogicEntity 0x555580 / FloodWeaponLogicEntity 0x555d30 and 0x588160/0x58ba00 write RetreatTime directly. Timer.StartRetreatTimer senders: TUs at 0x5210b0, 0x549bb0 + several weapon TUs (msgobjs 0x95c6ac,0x95d04c,0x95d964,0x95e094,0x95ea20). Front-end option "NoRetreatTime" (FE.WP.NoRetreatTime, 0x5d5830).
- GameLogicService (.\GameLogicService.cpp): 0x4fdc90 HandleMessage (WormSelect.WeaponSelected/OptionSelected, GameLogic.AddMeToDeathQueue, Worm.Died, GameLogic.GunWaiting, Turn.Started/Ended, MsgActivateSuddenDeath, MsgTurnEnded, inventory, telepads, briefing box); 0x4f7d80 = subscribe/init (wind, mines, SuddenDamageMode, WormPot, GoodShotDamageThreshold, MaxRandomCrates). 0x4fb880 HandleEndOfGame (Win/Draw -> GameOver menus, rounds, stockpile, mission/challenge records).
- Death queue [disasm]: AddMeToDeathQueue handler 0x4fac70 pushes worm id onto vector at GameLogicService+0x1fc. Tick 0x4fa2c0 calls 0x4f9b30 every frame: if queue non-empty and (ActiveObjectRegistrationService count (0x4d3960) == queue size, OR `GameLogic.SuddenDamageMode` (+0x210 is that key's handle, default 0, never written: there is no timeout, see §14), OR flag+0x1b1 set and count <= size+2): clear flag, pop FRONT id, post Worm.TimeToDie to that entity. One worm per pass -> deaths are sequential (each dying worm is an active object until done). Flag +0x1b1 set by message GameLogic.GunWaiting (gun weapons waiting for input tolerate 2 extra active objects) [assumed meaning].
- Senders: WXWormLogicEntity 0x5abc50 (damage-display routine "Worm Displaying Damage Taken", DamageGraphic.Offset) posts AddMeToDeathQueue(worm id) when energy ≤ the summed pending damage (0x5abf13), at ApplyDamage, before the 2500 ms display ends [disasm]. Damage type 6 (poison, abductee roll) alone gets no display and no token (0x5abe38); the vampire call 0x5a9710 gets −total/2 with poison included (0x5abef1) [disasm]. 0x5ab7e0 caps damage types 2, 3, 4 at 75 per ApplyDamage (DoubleDamage doubles the cap, not type 1 fall damage, 0x5ababb..0x5abb6a) [disasm]. Applied in sim.cpp hurt() per worm and type, reset in applyDamage() [ours]; type = ExplosionMessage kind arg, set only by Explode 0x57f140 (0x57f32c..0x57f367): container name prefix `kWeaponCluster` -> 2 (kWeaponClusterGrenade AND its kWeaponClusterBomb child; Banana/Bananette, airstrike bombs, kWeaponLandmineBomblet -> 0), `kWeaponFactory` -> 4, Landmine DetonationType Clusters (+0x14c == 3) -> 3; all other 0x518ce0 callers push 0 (gun 0x55e5da, sentry, crate/barrel, death 0x5a95be) or 6 (0x5ca577) [disasm]. Ours: Cluster Grenade (parent and children) 2, Weapon Factory 4, a Clusters landmine's blast 3 (`Game::mineBlast`), rest 0 [ours]. The type-4 cap is 150 when the team's TeamData.WormpotSuperWeapon (+0x24, via Rm 0x50bf60 on the worm's team byte +0x127) == 0x15 (kWeaponFactoryWeapon) and WormPot.SecretSuperWeapons (+0x50, FE.WP.SecretSuperWeapons) is set (0x5abaf0..0x5abb44) [disasm]; "class 0x15" is that weapon id, not a wormpot mode. Modelled: `Game::superWeapon` and the 150 cap in `Game::hurt` [ours]. The team read is the hurt worm's [disasm]. 0x5ab7e0 (take damage) posts Worm.Damaged / Worm.Damaged.Current, sets Turn.Boring/Mistake/FriendlyDamage/EnemyDamage, DamagedWorm.Id, DamageTypeTaken, uses DoubleDamage, MostRecentlyActiveWorm. 0x5b07c0 worm HandleMessage: Worm.TimeToDie (death sequence, see docs/death-sequence.md), GameLogic.ApplyDamage, Worm.ApplyPoison, Land.NewShape. 0x5a6970 Cleanup: DeadWorm.Id, posts Worm.Died. Net.Client.TimeToDie: 0x5f5220 (net replication).
- WXWormManagerService (.\WXWormManagerService.cpp): 0x5b5e70 HandleMessage: SpawnWorm, RespawnWorm, ActivateNextWorm (worm selection), ReinitialiseWorms, EndTurn, SelectNextWorm, UnspawnWorm, ApplyDamage; 0x5b37c0 subscribe (Water.Level).
- ActiveObjectRegistrationService 0x4d37a0 (Unregister): posts GameLogic.NoActivity when count hits 0; "ObjectCount.Active" read by AIService 0x4b3390, 0x4d3cb0 and GunWeaponLogicEntity.
- AIService 0x4b3390: handles GameLogic.EndTurn ("AIService got message c_MsgEndTurn"), GameLogic.AITurn.Started, AI.WeaponsDontEndTurn.
- Many services subscribe Turn.Started/Ended/EndTurn (HUD, camera, weapons, net 0x70bda0/0x7f7d30 NetService "Received Gamelogic.Turn.Started/Ended", g_msgEndTurnImmediate). Full list in turn_msgrefs.txt.

### 6b. Wormpot: WormpotService and Wormpot.lub

**Reels** [disasm, data]
- Mode ids are the jump-table cases of SetupModes 0x5d6bc0 (table 0x5d7110). Their names are `FETXT.%s.<key>` (0x9205e0, WPotName / WPotHelp).
  1 Empty, 2 SuperExplos, 3 SuperCluster, 4 SuperAnimals, 5 SuperFirearms, 6 SuperMelee, 7 WormsDrown, 8 Goliath, 9 MaxFall, 10 DoubleDamage,
  11 CrateShower, 12 Specialist, 13 NoCowards, 14 Max Health, 15 WindAll, 16 Energy, 17 CrateDrops, 18 Sticky, 19 Slippy, 20 Lowgravity,
  21 NoJumping, 22 TugOWorms, 23 WindGuns, 24 QuickWalk, 25 BlimpView, 26 MineRespawn, 27 MUltiGirder, 28 DimMak, 29 NoBombing, 30 Vampire,
  31 VitalWorm, 32 SecretWeap, 33 DonorCard, 34 GirdersOnly, 35 WindWorms, 36 JumpingOnly, 37 OneShot.
- Reel lists (each starts with 1):
  - 0x8ac960: 2 3 4 5 6 11 10 17 9 16 15 14 13 12 7 30 20 23 25 27 28 29 31 32 35.
  - 0x8ac9c8: 2 3 4 5 6 11 10 9 37 8 18 19 15 14 13 20 23 25 27 29 31 32 35.
  - 0x8aca28: 2 3 4 5 6 11 10 26 36 21 20 22 23 24 25 27 29 31 32 35.
- 33 DonorCard and 34 GirdersOnly are on no reel. Their flags (+0x51 / +0x52) have no reader in the exe or the Lua: unreachable, and inert if set.
- SetupModes reads `FE.Wormpot.Reel1..3`. It sets `Worm.VelocityScale` 1, then runs each reel's case, which sets its `WormPot` container flag
  (LVLSETUP `WormPotContainer`: PowerScale 2, SuperScale 2, FallingScale 2, SlippyModeScale 0.5, StickyModeScale 0.5, WindScale 0.5).

**Modes added in wave 2, exe side** [disasm unless tagged]
- **SuperCluster / SuperFirearms / SuperMelee** [data: Wormpot.lub]: `ApplyWormpotDamageScale` multiplies WormDamageMagnitude and LandDamageRadius by
  SuperScale; `ApplyWormpotPowerScale` multiplies ImpulseMagnitude by PowerScale.
  - Clusters: kWeaponClusterGrenade, ClusterBomb, Airstrike, SuperAirstrike, BananaBomb, Bananette.
  - Firearms: Shotgun, SniperRifle.
  - Hand to hand: BaseballBat, Prod, FirePunch, NoMoreNails.
  - For reference, Explosives: Bazooka, Dynamite, Grenade, HolyHandGrenade, Landmine, HomingMissile, GasCanister, Fatkins. Animals: Sheep, SuperSheep,
    OldWoman, ConcreteDonkey, Scouser.
- **Specialist** (+0x35, Lua only) [data: raw bytecode]: `SetSpecialistTeam(n, team)` returns unless n > 1. The worm at place p (1..n) gets class
  `T[(p-1)*6 + n]`.
  - Classes by team size: n=2: 1 2; n=3: 1 3 4; n=4: 5 6 3 4; n=5: 5 6 3 4 6; n=6: 5 6 3 4 6 3.
  - `DisallowAllWeapons` clears every Allow* on the worm except SkipGo / Surrender. It leaves AllowTeleport / Binoculars / BridgeKit / Pipe (LOCAL 1).
  - Each class sets its Allow flags on the worm and writes the ammo into the team inventory:
    - class 3 or 2: Shotgun -1, Airstrike 1, Landmine 2, FirePunch -1, Prod -1;
    - class 4 or 2: NinjaRope 5, Girder 3, Dynamite 1, Parachute 2, BaseballBat 1, Sheep 1, Teleport 2;
    - class 5 or 1: Bazooka -1, HomingMissile 1;
    - class 6 or 1: Grenade -1, ClusterGrenade 3.
  - Then `Inventory.WeaponDelays.Default` (the scheme's delays, stdvs SetupInventoriesAndDelays) gets HomingMissile 1 and Airstrike 5, copied to
    Inventory0..3.
- **NoCowards** (+0x36, Lua only) [data]: `DefaultRetreatTime` = `RetreatTime` = 0. Surrender is set to 0 in every team, alliance and worm inventory.
  Weapons still apply their RetreatTimeOverride on fire (6.6).
- **Energy** (EnergyOrEnemy +0x3a, Lua only) [data]: every worm gets PoisonRate = `Worm.Poison.Default` 10 (TWEAK), then `Worm.Poison`.
  The Worm.Poison handler (0x5add14) applies only when PoisonRate is 0; it clears the abducted bit 0x400 and posts Comment.Poison.
- **TugOWorms** (+0x41): 0x5d6a10 sets every worm's `ArtilleryMode` (+0x12a) = 1 and `AllowJetpack` (+0x14c) = 0.
  - "Movement disabled" 0x5ac390 is then true, so the worm runs only UpdatePassive 0x5aecb0: it turns in place and falls, but never walks or jumps.
  - The rope keeps the scheme's ammo.
  - AI: 0x4a4a04 sets `0x90ea74 = !ArtilleryMode`; the move plans read it.
- **WindGuns** (+0x42) and WindEffectMore (+0x38): GunWobbleObject (GunWeaponLogicEntity +0x90).
  - Built when the gun entity is made (ctor 0x55f5b0); only kWeaponShotgun and kWeaponSniperRifle are guns.
  - At build time: `f = 1 + 2.5 Wind.Speed / Wind.MaxSpeed` with either flag, else 1. Then 8 x (w = r, phase = r·π, freq = r·GunWobble.Speed·f).
  - Update 0x55f9e0, t = ms since build: `amp = GunWobble.MaxAmp·cos(t / Period)·f`; pitch = amp·Σ0..3 sin(freq·t + phase)·w; yaw = the same over 4..7.
    Both are multiplied by min(1.5 camera zoom, 1). A firing tick adds the kick (KickSize 0 for both guns, but one RNG draw).
  - Tweaks [data: WEAPTWK]: MaxAmp 0.03, Period 5000, Speed 0.004.
  - The shot uses it (Fire 0x55df90: pitch at 0x55e1d8, yaw at 0x55e1ff), and HeadCam adds it to the view (0x528eb5).
- **BlimpView** (+0x44): byte 0x90ea75 = 0 and `Camera.Disable "Blimp"`.
  - SetCamera 0x51e4e0 then refuses the Blimp; targeting still works from the aim view (0x583a10).
  - The AI skips Homing (0x4a03ab) and the Airstrike, Donkey, SuperAirstrike and Fatkins plans (0x4a0f18).
- **MineRespawn** (+0x45): `Land.Indestructable` 1. The land Explosion handler 0x473530 returns at once, so blasts leave no crater; a gun's
  Land.ClearVoxel (0x4733b0) is not gated.
  - Mine Detonate 0x580f10 clears the dud flag (0x581179) and posts `GameLogic.RespawnMine` 500 ms later (0x5812af).
  - Handler 0x4ff56a: CreateMine at `Payload.Deleted.LastPosition` if it is above Water.Level.
  - CreateMine skips the dud roll in this mode (0x4f97c9, 0x4f9d9d).
- **DimMak** (+0x4b, no reader): 0x5d6420 sets kWeaponProd `InstantKill` (+0xdb) 1 and `WormCollisionFX` "WXP_Wep_DimMak".
  - The melee hit 0x567a40 then deals Energy − pending damage (0x567c03), with impulse 0 (0x567d48), as type 5.
- **SecretWeap** (+0x50): 0x5d65c0 loops over teams 0..3. If the team inventory holds any kWeapon (1..31), it draws `rand % 30` until the team holds
  that id, and stores it in TeamData.WormpotSuperWeapon (+0x24; 0x43 kWeaponUndefined otherwise, 0x5d63c4).
  - At the start of a turn (0x5d71b0), the active team's weapon container gets WormDamageMagnitude and LandDamageRadius × SuperScale; turn end
    (0x5d73c0) restores them (0x5d6770). Payload +0x15c / +0x168, Gun +0xc8 / +0xe8, Melee +0xc0 / +0xd0; a Sentry Gun is refused (0x5d692e).
  - The Weapon Factory reads SuperScale through 0x5d66e0 (0x598684). Damage type 4 is capped at 150 for that team (0x5abaf0).
  - Not shown anywhere (`FETXT.SelectTeamWeapon` has no reader).
- **WindWorms** (+0x53): the worm's Acceleration gets Wind × WindScale 0.5 (0x5a6d20, physics.md); the Ballistic integrate uses it.
- **JumpingOnly** (+0x54): `Worm.VelocityScale` 0. The walk step 0x5b0f9d scales its displacement by it; jumps don't read it.
  - The turn-end reset 0x4f59f0 (to 1) skips it, and the mystery Quick Walk crate does nothing in this mode.
- **QuickWalk** (24): `Worm.VelocityScale` 2 for the game (0x5d6f9e).

### 7. .lub inventory (145 files) [data: names + markers; roles assumed from name/flow]
- Core: stdlib (base turn state machine), stdvs (versus/multiplayer rules on top of stdlib), lib_help (helper library), Wormpot (Wormpot modifier modes, DoWormpotOncePerTurnFunctions).
- Multiplayer game modes (use lib_SetupMultiplayer / stdvs): Multiplayer (just Initialise -> stdvs), MultiplayerDestruction (land %), StatueDefend (crate statue), Survivor, RelayRace (triggers), AssaultAndDefend, HideAndSeek (crates), MysteryCrate, PublisherMulti, PublisherMultiAI.
- W4M story missions (GameLogic.Mission.Success): BuildingSiteSaboteurs, CarpetCapers, ChuteToVictory, crust, DEMO_Mission, DestructAndServe, DinerMight, DoomCanyon, EscapeFromTreeRex, FastFoodDino, GhostHillGraveyard, GibbonTake, HighNoonHiJinx, JoustAboutIt, MineAllMine, NiceToSiegeYou, NoRoomForError, RobInTheHood, SneakyBridgeThieves, StormTheCastle, TheCrateEscape, TheLandThatWormsForgot, TheWindyWizard, TinCanWally, TraitorousWaters, TurkishDeLights, ValleyOfDinoWorms.
- Tutorials: Tutorial1-3, OuttakeIntroduction.
- Challenges (GameLogic.Challenge.Success): ChallengeAccuracy(2), Crate(2), Icarus, Jetpack(2), Navigation(2), Sheep(2), Shotgun(2), Sniper(2).
- Deathmatch1-11: W4M deathmatch levels (6 funcs, intro movie + TurnEnded via lib_DeathmatchMissionTurnEnded, assumed).
- Outtakes (short movie scripts): OutTake*/Outtake* (14 files).
- Worms 3D legacy (-w3d suffix; EFMV.Start movies, Mission.Success): ALIEN, applecore, BALLOON, beanstalk, boldly, BREAKFAST, cherry, clean, COLLIDE, cooped, countingsheep, CrateBritain, cropcircle, crust-w3d, dday, FALLING, funfair, graveyard, helterskelter, highstakes, holiday, hookline, ICE, landing, leek, notpc, pack, pegasus, PLAICE, rum, SCHOOLS, SHOWDOWN, timbers, treevillage, TRIAL; Deathmatch1-10-w3d (challenge-success W3D deathmatches).
- Dev/test: aitest, animtest, armourtest, BuffaloTest, hudtest, level1, manel, movietest, NetTest (worm select/teleport test), presentation, selftest, SentryScripted, SentryTest, smoketest, test, Test16Worms.

## 14. Turn timing: units, timers, death pacing (extends §6)

Tags: [data] = XOM/.lub values, [disasm] = exe code read, [assumed] = inference.

### Units and clock
- Every turn-phase timer counts **game milliseconds**. `TimerLogicEntity` Update (vtable 0x851fc4, slot 6, 0x50f100) subtracts 10 from each running "...Remaining" key and returns 10. The task's return value is the delay before its next call, so the timer runs at 100 Hz. [disasm]
- Rates of other tasks: worm Update 0x5b1d60 returns the time left to the next multiple of 20 ms (50 Hz, aligned). GameLogicService Update 0x4fa2c0 returns 20. Game time in ms is at `*(0x96d030)+0x38` (delayed posts use it, see Worm.DamageComplete below). [disasm]
- The scheme fields are copied to the data keys unchanged (no ×1000 and no ÷60). stdvs `SetupScheme`: `HotSeatTime=HotSeat`, `TurnTime`, `RoundTime`, `DefaultRetreatTime=LandTime`. The front end compares `Q.LRet` directly with `SchemeData+0x154` (0x74ca3a) and `Q.Hot` with `+0x160`. [data+disasm]
- The front end shows `ms/1000` as "%d" (turn, retreat) and the round time as "%01d:%02d" (0x753673, 0x75373a, 0x75398a). [disasm]

### SchemeData timing fields (`pe.py schema '^SchemeData$'`, i32)
| field | off | unit | meaning |
|---|---|---|---|
| RoundTime | +0x120 | ms | round clock. 0 = sudden death at start (stdvs Initialise). -1 is allowed by the assert (no round clock) |
| TurnTime | +0x124 | ms | turn clock. 0 = no clock. The assert requires 0 or more than 10000 |
| LandTime | +0x154 | ms | **retreat time** after the shot (`DefaultRetreatTime`). Editor label `FETXT.RetreatTime`, message `Scheme^LandR^` |
| RopeTime | +0x158 | ms | rope retreat (assumed from the editor keyword `RopeR`/`Q.RRet`). It has a value cycle at 0x753762, but no `Scheme^RopeR^` message exists and no .lub reads it, so it is never applied |
| HotSeat | +0x160 | ms | the "get ready" countdown before the turn clock. The PC editor cannot change it |
| HelpPanelDelay | +0x16c | ms | values 0/1000/3000/5000 |
| MineFuse | +0x168 | **s** | lib_help: `Mine.Min/MaxFuse = MineFuse*1000`; -1 = random 0..5000 ms |
| DisplayTime | +0x150 | bool | `HUD.Clock.DisplayRoundTime` |
| SuddenDeath | +0x148 | enum | 0 = all worms to 1 hp, 1 = commentary only, 2 = draw |
| WaterSpeed | +0x14c | enum | 0..3 select `Water.RiseSpeed.{0,Slow 4,Medium 8,Fast 16}` (Water.Level units, added once per turn end) |
| RandomCrateChancePerTurn | +0x12c | % | DropRandomCrate 0x4fab20: needs AllianceCount ≥ 2, drops when rand % 100 < it (0x4fabc0) [disasm] |

Built-in schemes (LOCAL.XOM `SchemeData`, 19 of them) [data]: TurnTime 30000/45000/60000/90000. RoundTime 600000..2700000. HotSeat 10000, except Ranked 5000. LandTime 0 (Pro, Strategy, Mystery), 3000 (KitchenSink, MultiDestruction, QuickGame, QuickGameDemo, WXD.DefaultSchemeData), 5000 (most), 6000 (Ranked), 8000 (Family). RopeTime 5000 (0 Bng, 10000 Mystery). LVLSETUP `GM.SchemeData` (live copy): 1800000 / 60000 / 10000 / LandTime 3000.

Editor value cycles ("+" direction; "-" is the reverse) [disasm 0x7525ac]:
- Turn 15→20→30→45→60→90→15 s.
- LandR (retreat) 0→3→5→10→0 s.
- Round 5→10→15→20→25→30 min, then 0 if `Q.SDeath`<2, else back to 5.

### Data-key defaults (LOCAL.XOM, ms) [data]
TurnTime 45000, RoundTime 1800000, HotSeatTime 10000, **PostActivityTime 2400**, DefaultRetreatTime 3000, RetreatTime 0, GameLogic.SuddenDamageMode 0. Other keys:
- `Game.RoundTime` / `GS.Default.RoundTime` = 900: online lobby, seconds [assumed].
- `CommentaryPanel.Delay` 1200, `GameToFrontEndDelayTime` 3000.
- `Camera.Track.RestTime` 1500, `Camera.Track.MinEventTime` 1000 (CAMTWK).

Overrides in the scripts [data, lua.py]:
- Challenges: HotSeatTime/RetreatTime/DefaultRetreatTime 0, PostActivityTime 0 or 10 (10 = one timer tick).
- Wormpot NoRetreatTime: retreat 0.
- Missions: TurnTime 25000..99000.

### TimerLogicEntity (0x50f100 tick, 0x50f980 HandleMessage) [disasm]
Members:
- data-key handles: +0x74 RoundTime, +0x78 TurnTime, +0x7c HotSeatTime, +0x80 PostActivityTime, +0x84 RetreatTime.
- remaining times: +0x88 Round, +0x8c Turn, +0x90 HotSeat, +0x94 PostActivity, +0x98 Retreat (all "...Remaining").
- other keys: +0x9c ElapsedRoundTime, +0xa0 ClockDisplayMode (1 hot seat, 2 turn, 0 post-activity or retreat).
- The HUD clock (HudClockEntity init 0x5f0ae0) binds RoundTimeRemaining, TurnTimeRemaining, HotSeatTimeRemaining and RoundTime / TurnTime, not RetreatTimeRemaining (read only by TimerService 0x50f87e and the CMS 0x51ff68) [disasm]; what it shows in mode 0 (retreat) is not traced.
- flags: +0x69 game paused, +0x6a turn, +0x6b round, +0x6c hot seat, +0x6d post-activity, +0x6e retreat, +0x6f round paused, +0x70 turn paused.

Each tick:
1. If paused (+0x69), do nothing.
2. ElapsedRoundTime += 10, and the round timer counts down, unless the round is paused, or the camera service (`*0x95c370`, current camera +0x2c) is in mode 0xe [mode meaning not identified]. When the round timer goes below 0 it is set to 0 and **Timer.GameTimedOut** is posted. stdvs `Timer_GameTimedOut` is empty: sudden death is only checked at turn end (`CheckSuddenDeath`: RoundTimeRemaining==0).
3. Only one of these phases is ticked, in this priority: hot seat → post-activity → turn → retreat.
   - **Hot seat**: posts Timer.HotSeatTimedOut. Any `Input.SomeInputFrom` cancels it at once ("hot seat canceled"), which also posts HotSeatTimedOut.
   - **Post-activity**: posts Timer.PostActivityTimedOut.
   - **Turn**: frozen while TurnTime is paused, in camera mode 0xe, or while byte 0x95d9fc is set (GunWeaponLogicEntity 0x55ed10 sets it while a multi-shot gun is firing). Posts Timer.TurnTimedOut.
   - **Retreat**: when the remaining time reaches 4000..4009 ms it posts `Acting.Trigger(0x2e, 0x7f)` (enum name not resolved). Posts Timer.RetreatTimedOut.

Messages:
- StartGame: Round = RoundTime; the round runs if RoundTime>0.
- StartTurn: Turn = TurnTime; the turn runs if >0.
- EndTurn: stops the turn and the hot seat.
- StartHotSeatTimer: if HotSeatTime>0, starts it and spawns an object by class GUID 0x86633c (HotSeatTimeGraphicEntity, assumed); otherwise posts HotSeatTimedOut at once.
- StartPostActivity: if `Turn.Boring`>0 and `Turn.PayloadFired`>0, posts `Acting.Trigger(0x1e,0x7f)`. Then PA = PostActivityTime; if it is 0 or less, PostActivityTimedOut is posted at once.
- StartRetreatTimer: Retreat = **RetreatTime** (not DefaultRetreatTime); if it is 0 or less, RetreatTimedOut is posted at once.
- EndRetreatTimer, and also `GameLogic.Turn.Started`: stop the retreat.
- GameLogic.DoubleTurnTime (0x50f020): turn remaining ×2, capped at 99000.
- Round/TurnTime.Pause and .Resume: set the pause flags.

Who starts which phase:
- On firing, BaseWeaponLogicEntity 0x549ad0 posts **Timer.EndTurn** (the turn clock stops) and Weapon.DisableWeaponChange.
- When the weapon is done, 0x549bb0 posts Weapon.Delete, **Timer.StartRetreatTimer**, Weapon.Fired. Before that, the weapons set `RetreatTime = DefaultRetreatTime` or the `RetreatTimeOverride` (section 6.6).
- CameraManagerService 0x5210b0 only subscribes to StartRetreatTimer (0x521e9c, a handler table), it does not send it.
- Payload weapons [disasm]: Fire 0x583160 posts Turn.PayloadFired, DecrementInventory, then 0x549ad0 (Timer.EndTurn, `Worm.WeaponDisableMovement` = 1) and schedules `Weapon.LaunchPayload.Callback` (LaunchDelay). The callback 0x585e90 launches the payload (`Payload.Launched`) and schedules `Weapon.PostLaunchDelay` at now + PostLaunchDelay (props +0x44). Its handler 0x5833a0 launches again while `m_uPayloadsToLaunch` > 0 (InterPayloadDelay +0x130), else posts Weapon.EndFireAnim and, if **EndTurnImmediate** (props +0x1d4), calls 0x549bb0: `Worm.WeaponDisableMovement` = 0, Weapon.Delete, **Timer.StartRetreatTimer**, Weapon.Fired. So the retreat runs from PostLaunchDelay after the launch, with the payload still flying, and the worm may walk. Other weapons: BaseWeaponLogicEntity::EndFireWeapon 0x54a0e0 posts Weapon.PostLaunchDelay (+0x3c) or calls 0x549bb0 at once.
- WEAPTWK (EndTurnImmediate / RetreatTimeOverride / PostLaunchDelay ms) [data]: Bazooka, Grenade, Cluster Grenade, Banana, Holy, Gas 1 / -1 / 500; Homing, Sheep, Super Sheep, Poison Arrow 1 / -1 / 0; Dynamite, Landmine 1 / 5000 / 0; Old Woman, Scouser, Airstrike, Super Airstrike, Fatkins, Donkey 1 / 0 / 0; Starburst 0 / 0 / 0 (no retreat from the weapon); Alien Abduction, Flood -/0/0; Shotgun, Bat, Tail Nail -/-1/1000; Sniper -/-1/2000; Sentry -/-1/420; Skip Go PostLaunchDelay 3000.
- Movement during the flight [disasm]: the camera activations (vtable +8) of FallCam 0x527460, FlyCam 0x527f30, GirderCam 0x528480, HeadCam 0x529400 and IsometricCam 0x529910 post `Input.DisableGroup WormMoving`; DefaultCam 0x524a40, JetpackCamMkII 0x52aff0 and OrbitCam 0x5310d0 enable it again. TrackCam and ChaseCam do not touch it: the worm walks under a bazooka's TrackCam, not under the homing missile's or the airborne Super Sheep's FlyCam.
- The Lua side (section 6.1): RetreatTimedOut → EndTurn → wait for `ObjectCount.Active==0` → StartPostActivity → after 2400 ms, PostActivityTimedOut → ApplyDamage → settle → DoPostActivity.

### Death queue and death pacing (correction to 6.6: +0x210 is not a timer) [disasm]
- `GameLogicService+0x210` is the handle of the data key **GameLogic.SuddenDamageMode** (bound at 0x4f8c15). Its default in LOCAL.XOM is 0. No .lub sets it, and the exe only binds and releases it (0x4f3e27, 0x4f3f08), so it is always 0 [assumed: debug-only].
- 0x4f9b30 runs every 20 ms. It pops the front worm when one of these holds:
  - active-object count == queue size;
  - SuddenDamageMode != 0;
  - GunWaiting, and count <= size + 2.
  
  There is **no timeout**.
- Active tokens held by a worm, each released or replaced per slot (`0x4d3af0` Register(name, file, line, slot*)):
  - Every hurt worm's display routine 0x5abc50 calls the CMS worm-track request 0x51cf20(worm) at 0x5abeec, all at the ApplyDamage instant [disasm]: one WormTrackCamera request per worm, not a group request. Served ≥ 200 ms apart (0x51d3d0, docs/camera-w4m.md §1). The later same-priority requests are lost: the pending slot (+0x350) keeps only a higher request (or a 5 reaching the water sooner) and the serve routine 0x51d360 empties it at the end of every CMS update (0x51d5ad), served or not [disasm].
  - Damage display 0x5abc50 takes slot +0x60 ("Worm Displaying Damage Taken") and posts **Worm.DamageComplete at now + 2500 ms** (0x5abe94). DamageComplete (0x5b09b7) releases slot +0x60.
  - If damage >= energy: 0x5a70e0 sets energy to 0, takes slot +0x5c "Worm Waiting To Die", then the worm posts `GameLogic.AddMeToDeathQueue`.
  - So the first death waits for every damage display (2.5 s) and every other active object to finish.
- Worm.TimeToDie (0x5adbf0; ignored if the state is already DrownFloat 8):
  - posts **Worm.LandDeath** (the CommentService banner, 0x5e41e0);
  - ChangeState 7 DeathThroes, animation id 0x13;
  - 0x5a7190: slot +0x5c becomes "Worm Dying", stats, the camera tracks the worm (0x51cf20, WormTrackCamera); a flag 0x100 worm posts SurrenderTeamById;
  - **throes timer +0x128 = 3000 ms**.
- DeathThroes update 0x5aa080: -20 per worm tick. At 0 or below:
  - explosion 0x5a9400 (`Worm.DeathWormDamage*`, `DeathImpulse*`, `DeathLandDamageRadius`, ExplosionMessage);
  - 0x5a9310 (spawns something at +20 height: gravestone, assumed);
  - **WXWormManager.UnspawnWorm**.
- The drowning check 0x5ad640 also calls 0x5a7190 (0x5ad83d / 0x5ad8bc) before ChangeState 8: a drowned worm holds "Worm Dying" and asks the same WormTrackCamera when it drowns. That track ends only once the worm's tokens +0x54 / +0x58 / +0x5c are gone (TrackCam 0x532e94) or the worm is unspawned (0x533a61), plus RestTime, so it shows the blast (docs/camera-w4m.md §2) [disasm].
- **Drowning the active worm** [disasm + data]: 0x5ad640 runs in every state but 7 / 8, Override (5: rope, jetpack, chute) included, and keeps the Velocity of any state but 0 / 3. For the active worm (0x95fb98) it posts `Worm.Damaged.Current` (0x5ad77d); stdlib.lub `Worm_Damaged_Current` sends Weapon.Delete, Utility.Delete, Timer.EndRetreatTimer, Timer.EndTurn, then EndTurn() → GameLogic.EndTurn [data]. The chain is synchronous: the post 0x6910e4 → 0x68cb82 calls the handlers in line, XScriptService 0x6953e9 runs the Lua callback in line (0x698e88). On GameLogic.EndTurn the jetpack posts Jetpack.Kill (0x564342), whose handler removes its task at once (0x68b927 → 0x68e18f); WXWormManager deactivates the worm (0x5b2610: Worm.CleanUpOnDeactivate, 0x5aaac0), and ChangeState refuses to leave state 8 (0x5aa815). So in the drowning frame the turn ends, the jetpack is gone and the worm floats. Payloads already dropped (the secondary Dynamite) keep their own fuse; EndTurn waits for them and for "Worm Dying" (ObjectCount.Active), and the float waits for neither.
- DrownFloat 0x5aa130 uses the same timer: it sets 2000 ms once its feet reach Water.Level − 8 units while not sinking (0x5aa222, docs/w4m/physics.md §11) [disasm], then -20 per tick, then the same blast and Unspawn.
- **Several drowned worms** [disasm]: no sequencing. The drowned never enter the death queue (state 8 ignores TimeToDie, nothing posts AddMeToDeathQueue) and each timer +0x128 is per worm, run by every worm's Update. Blast time = drowning tick (feet 7 units under) + sink and rise to Water.Level − 8 (depth from the entry speed, docs/w4m/physics.md DrownFloat) + 2000 ms. Two worms thrown by one blast therefore pop in the order and with the spacing of their surface arrivals, often a few frames apart. Camera: both call 0x5a7190 → 0x51cf20 at their own drowning; a request within 200 ms of the last cut (0x51d3d0) is not served and the pending slot is emptied each CMS update (0x51d5ad), so a second worm drowning that soon is lost and the WormTrackCamera stays on the first through its blast and RestTime; one drowning later cuts to the second (same priority 5 is not below the running track's). A land death and a drowning do not wait for each other either, except that the queue pops only once the drowned worm's "Worm Dying" token is gone (count == size).
- **Delay between successive deaths** = 3000 ms of throes + unspawn/cleanup. The next pop waits until the dying worm's token is gone, because count == size + 1 while it is dying. There is no other gap.
- **End-of-turn settle**: EndTurn waits for `ObjectCount.Active==0`, which includes damage displays (2.5 s) and dying worms (3 s each). Then PostActivityTime 2400 ms, then ApplyDamage (poison), which can start a new settle and death round.

### Other per-turn timers [data unless noted]
- Sudden death: checked once per turn end, in `DoOncePerTurnFunctions` (stdvs). Once it has started, `Water.Level += Water.RiseSpeed.Current` (4/8/16) every turn end, preceded by `GameLogic.AboutToWaterRise`.
- Crates: one `GameLogic.DropRandomCrate` per turn end. `Crate.DelayMillisec` 0, `Crate.WaitTillLanded` 1, `Crate.Parachute` 1.
- Mines: `Mine.MinFuse/MaxFuse` in ms; the scheme MineFuse is in s.
- Game-wide (0x4fa2c0) [disasm]: `FCS.QuitAttractMode` when game time >= 300000 ms with flag `*(0x95a298)+0x160` bit 1 set (the attract demo, assumed). `GameLogic.QuitGame` once at 14,400,000 ms (4 h).


### Abductee hp roll, abduction sight ray, Weapon Factory templates (found while aligning abduction and factory weapons)
- **Order of the roll** [disasm + data]: stdlib.lub `DoPostActivity` (once per turn, gated by `done_once_per_turn_functions`) sends `GameLogic.Turn.Ended`, `Worm.ApplyPoison`, `GameLogic.AboutToApplyDamage`, `GameLogic.ApplyDamage`, then `CheckActivity` again. The worm handler 0x5b07c0 maps `Worm.ApplyPoison` to 0x5ac060 and `GameLogic.ApplyDamage` to 0x5abc50. So the roll comes before the damage is applied and before any death is decided.
- **ApplyPoison 0x5ac060** [disasm]: with poison (`[data+0x10c]` > 0) it calls 0x5ab7e0(type 6) with damage = poison if hp > poison, else hp - 1 (unless state 7), so poison never kills. Without poison and with flag 0x400 (abductee): if worm `+0xdd` is set it takes `r = rand() >> 16) % 100` (0x68c015) and calls 0x5ab7e0(hp - r, 6), spawns `WXP_AbdDamageInd`; then it sets `+0xdd = 1`. 0x5ab7e0 clears `+0xdd` (and sets `+0xdc`) on every damage call, so "unhurt since the last roll" is `+0xdd`; the first roll after SpitOut only arms it (`+0xdd` was cleared by the half-hp damage).
- **0 is not floored** [disasm]: type 6 skips the clamp (0x5abb6f) and adds the delta to the damage array at `[data+0x118]`; `r > hp` is a negative delta (a heal to r). 0x5abc50 sums the array (+ `[data+0xcc]`): hp (`+0x11e`, u16) <= total kills (0x5abfb4 -> 0x5a70e0 + `GameLogic.AddMeToDeathQueue`), otherwise hp -= total. So r = 0 leaves 0 hp and the worm dies; there is no minimum of 1.
- **Candidate list 0x5488e0** [disasm]: for the 16 worm slots: skip if `+0x124` (Active) is 0; xz distance squared (the y delta is zeroed) vs `Abduction.AreaOfEffect`; skip if worm flags `+0xec` & 0x8 (in a bubble: 0x5ae544 -> 0x5a61e0 'Bubble not found') [disasm] or & 0x20 (nailed [disasm]: DirtBallLogicEntity 0x5ce320 does `flags |= 0x30` on the victim, 0x5ce2d0 ends the nail when 0x20 is gone, 0x5ae60e clears it on a blast; 0x8 is set at 0x54f4d3 and cleared at 0x54f3f2 / 0x54f602 in BubbleTroubleLogicEntity); then 0x466a20(saucer pos, 1, worm - saucer, 0). The result in eax is ignored: the code tests the global byte 0x952c31 (set to 1 by 0x466880 when the sweep records a contact, cleared at the start by 0x4661a0). A hit adds the worm via 0x548240 with the 3D distance squared as the sort key (nearest first).
- **What that ray tests** [disasm]: 0x466a20 -> 0x4661a0 -> 0x466880 -> 0x517e20 sweeps the segment against the **collider spheres** (callbacks 0x516350 / 0x5164b0 / 0x517630), not the voxel land: arguments mask = 1, exclude owner id = 0. The worm entity registers its collider with flags 1 (0x5a9bb3: `push 1; call 0x519d30`), so the ray hits worm spheres. The segment ends at the target's own feet, so the target's sphere (centre one radius above the feet, the segment enters it just before its end) is hit: the filter is true for every live worm that passes the xz test. The worm collider (0x5a9ac0, 0x5a9bb3..0x5a9c0a) is flags 1, mask 0x811, owner = logical entity id `+0x14` (0x519d50), radius 10 units centred 5 units above the feet, so the segment end is inside it and the exclude id 0 matches no worm [disasm]: collider flag 1 is `kCF_Worm` (assert 'uColliderFlags & kCF_Worm' at 0x54f43b); entity ids are task handles slot | generation << 12 (0x68c82c, bumped by 0x1000 on free at 0x68e3df), so id 0 is the first task of the session, never a worm. The camera notes that call 0x466a20 "land only" (mask 0) are a different call: this one is mask 1.
- **Weapon Factory templates** [data + disasm]: 0x599990 loads `kWeaponFactoryHoming` when the factory definition's homing byte (`+0x68`) is set, else `kWeaponFactoryWeapon`, and 0x5983f0 then fills the payload from the player's definition (thrown: weapon type 4, AimThrown / DrawThrown / FireThrown, weapons/Throw; launched: type 2, AimBazooka / HoldWFGun / DrawWFGun / FireWFGun / TauntWFGun, weapons/SecretWeapLaunch; airstrike: type 0xa, HoldAirstrike / DrawAirstrike / FireAirstrike, weapons/BombWhistle, camera FatkinsTrackCamera at 0x5993e0). The fields it never writes come from the template (WEAPTWK): `kWeaponFactoryWeapon` PostLaunchDelay 500, LaunchDelay 0, RetreatTimeOverride -1, Radius 5, CameraId PayloadTrackCamera, LaunchSfx weapons/RocketRelease, DetonationSfx global/ExplosionRegular, EquipSfx weapons/BazookaEquip, DisplayName Text.kWeaponBazooka, CanBeFiredWhenWormMoving 0; `kWeaponFactoryHoming` the same but PostLaunchDelay 0, CameraId HomingMissileFlyCamera, DisplayName Text.kWeaponHomingMissile, LaunchSfx weapons/RocketRelease; `kWeaponFactoryCluster` (sub-payload) PostLaunchDelay 0, Radius 4, EquipSfx weapons/DefaultEquip.
- **Factory byte +0x69 = HomingAvoidLand** [data: schema WeaponFactoryContainer: 03 Homing +0x68, 04 HomingAvoidLand +0x69, 05 EffectedByWind +0x6a, 06 FireOnGround +0x6b, 07 Poison +0x6c, 16 ProjectilePowersUp +0x6d]. 0x598d32: set -> homing props AvoidsLand (+0x20c) 1, Vertical / ForwardLandAvoidanceDistance (+0x1fc / +0x200) 100, Vertical / ForwardLandAvoidanceForce (+0x204 / +0x208) 0.009, Stage2Duration (+0x1ec) 30000, Stage3Duration (+0x1f0) 1000, MaxHomingSpeed (+0x1f4) 0.25, LifeTime (+0x150) 30000, DetonatesOnExpiry (+0x1d8) 1, camera HomingMissileChaseCamera (CAMTWK Chase: Dist 170, DefaultHeight 0.255, HeightSpeed 1.4, MinHeight 0.1, MaxHeight 1); clear -> AvoidsLand 0 and HomingMissileFlyCamera [disasm]. The avoidance step is HomingPayloadLogicEntity 0x5611b0, called by the stage-2 update 0x561730 right after the homing step 0x560eb0 when `AvoidsLand` and not arrived (+0x178) [disasm]:
  - distance to the target < 5 units: +0x178 = 1 and nothing more, ever; distance < ForwardLandAvoidanceDistance: nothing;
  - the probe is vtable +0x58 = 0x57dca0 (HomingPayloadLogicEntity vtable 0x859e7c, slot 22) [disasm]: `0x466ae0(start, step vector, ..., 20 steps)`, a hit when the hit step is <= 20, outputs the hit point (0x952d4c) and normal (0x952d64); the step vector is the whole distance / 20, so the probe is a land segment of that distance, mask 0 (land only);
  - up probe (0, +VerticalDistance, 0), down probe (0, -VerticalDistance, 0), `low` = down hit or y < water level + 5 units, forward probe = velocity direction x ForwardDistance;
  - only if the forward probe hits: force f = (0, y, 0) with y = +VerticalForce when nothing is above, -VerticalForce when the up probe hits and `low` is false, 0 when both hit; then f -= normalise(hit - pos) x ForwardForce; vel += f x 20 (ms per tick);
  - always, after that: vel = normalise(vel) x min(|vel|, MaxHomingSpeed).
  - the clamp only runs on the forward-probe path: distance < 5 units or < ForwardLandAvoidanceDistance returns before it (0x561286, 0x5612a3 → 0x561639) [disasm].
  Ours: `Game::avoidLand`, the same three probes as `Terrain::raycast`, which also samples the segment (every half voxel, 0.125 m, against W4M's 20 steps of 5 units) and returns the first sample inside the land, so the push direction normalise(hit − pos) is the probe's own direction either way [ours: our voxel land]; the force is 0.009 units/ms² = 450 m/s² applied per our tick (450 × DT; was 9 m/s per 1/60 s tick, 1.2 × W4M's), 100 units = 5 m, `Projectile::stage` = arrived.
- **Factory LaunchSfx** [data + disasm]: weapons/Throw is a 3D linear 10..500 unit oneshot, weapons/SecretWeapLaunch a 2D oneshot, both 0 dB in the `weapons` bank (WormsX.fev, `fev.py -g 'Throw|SecretWeap'`); set into the payload's LaunchSfx (+0xb0) by 0x5983f0 for thrown (0x598fc0) and launched (0x599167).

### Start placement: worms, then mines and drums [disasm + data]
- Order [data]: `stdvs Initialise` runs `lib_SetupMultiplayerWormsAndTeams` (each worm `Spawn = "spawn"`, lib_help.lub `lib_SetupMultiplayerWorm`), sends `WormManager.Reinitialise` (worms placed), then `lib_SetupMinesAndOildrums`: Objects 1 or 3 sends `GameLogic.CreateRandomMine`, 2 or 3 `GameLogic.CreateRandomOildrum`. Worms first.
- Worm [disasm] `PlaceWormAtSpawnPoint` 0x5b4180 (`Spawn` "spawn" is random; "spawn%d" names a locator, 0x59aad0, missions): one rand() for a debug log, drawn even when it does not log (0x5b41a8); yaw = (rand() & 0xff) × 2π/256 (0x5b433e, written as the worm Orientation at 0x5b478f); the offset (0, 10, `Worm.Collision.ZOffset` -5) turned by that yaw (0x69bef6, 0x455d30); up to 1000 tries, each a cell from `AISceneGraphService` 0x4ae810, whose failure ends the tries: up to 500 draws of rand01 × total area picking the first node grid whose running area covers it (else the last), i = trunc(i0 + rand01 × (i1 − i0)) (i1 → i1 − 1, rand01 is rand()>>16 & 0x7fff / 32767, 0x4a44b0), j likewise, layer = rand() % 2 (0x4a4510), the first whose layer flag is 0 (walkable). The point = node position (0x4aeb00: x, z on the lattice, y = (lo + hi)/2, 0x4ae780) + the offset + 10 units up; inside the team land box (min < p < max on x and z, tables 0x955788 / 0x955800 set at 0x477060, the whole land by default; skipped after 900 tries with none counted); a 10-unit sphere there free of colliders (0x519e90) counts; the strictly highest of 3 counted points wins and becomes the worm position (+0x38). None: `Land.Center` +-50 units (two draws 0x68c024, x then z), `Land.MaxHeight` + 10 units. No mine exists yet: there is no worm-to-mine rule on the worm side.
- Mine / drum [disasm] 0x4f26b0 (callers 0x4fe0b5 mine, 0x4fe15b drum, 0x4fe1e6 mine factory): up to 100 random land-box columns, ray to ground above `Water.Level`, a sphere radius + 5 units on the ground clear of every collider; all tries failed: nothing is created. Mine radius 3, drum 9 (kWeaponLandmine, 0x8626cc): a mine centre is at least 10 + 3 + 5 = 18 units (0.9 m) from a worm collider centre.

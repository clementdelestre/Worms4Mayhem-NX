# W4M mission scripts (story, challenges, deathmatches)

Part of the W4M map (index, tools, tags: [README.md](README.md)). Script loading and the stdlib turn machine: [turn.md](turn.md) §6.
EFMV movies: [acting.md](acting.md) §19. Level databank containers: `docs/w4m-formats.md` "Missions". Ours: `docs/missions.md`.

## 23. Mission scripts

### 23.1 Lua runtime [data + disasm]

- Lua **5.0.1** (`$Lua: Lua 5.0.1 ...` 0x8b92d8) [data]. Bytecode header: `\x1bLuaP`, version 0x50, little endian, int / size_t /
  Instruction 4 bytes, **lua_Number = 4-byte float** [data, every `.lub`]. A stock 64-bit Lua 5.0.1 build rejects these chunks on the
  Instruction (unsigned long), size_t and number sizes [tried: lundump.c's header check].
- C functions registered by 0x6958d2, 14 in all (pushstring / pushcclosure / settable triplets 0x6959e3..0x695cef) [disasm]: `SendMessage`,
  `SendFloatMessage`, `SendIntMessage`, `SendStringMessage`, `GetData`, `SetData`, `StartTimer`, `CancelTimer`, `EditContainer`,
  `CloseContainer`, `QueryContainer`, `CopyContainer`, `echo`, and `log` (0x695cbd, impl 0x69908a; the challenges call it with debug
  strings) [disasm]. Lua's `math` library is open: `lib_GetRandom` calls `math.mod` [data].
- Send*Message (e.g. 0x696960): the name is looked up (0x690d44); unknown names log `%s : Message name not registered`, and a per-name
  check 0x69968b can refuse with `%s, %s : Message permission denied` [disasm; the permission table is not traced].
- `StartTimer(name, ms)` (0x69810f): exactly 2 args, ms converted to int (0x6fe690), first free slot of the vector at service +0x174
  (`Too many timers requested`), deadline = clock [0x96d030]+0x38 + ms (0x69b37c); returns the slot, which `CancelTimer` takes. At the
  deadline the global Lua function `name` is called: every timer name used is a function of the same script [disasm + data].
- Engine -> Lua callbacks: message `A.B.C` calls global `A_B_C` (table 0x921368, turn.md §6.5) [disasm].

### 23.2 Bytecode inventory [data: lua.py over the 51 scripts listed by SCRIPTS.XOM]

| set | scripts | instructions |
|---|---|---|
| story (type 4) | 25 | 11,377 |
| challenges (type 8) | 15 | 3,289 |
| deathmatches (type 9) | 11 | 1,455 |

- Opcodes used by these 51 scripts plus stdlib, lib_help, stdvs, Wormpot: 24 of Lua 5.0's 35. LOADK 5102, GETGLOBAL 4707, CALL 3498,
  SETGLOBAL 1401, JMP 1123, EQ 677, RETURN 615, GETTABLE 553, LOADBOOL 553, CLOSURE 535, SETTABLE 322, MOVE 268, ADD 124, LT 99,
  SETLIST 77, NEWTABLE 74, SUB 44, LE 32, MUL 30, FORLOOP 10, LOADNIL 7, DIV 5, TEST 2, TAILCALL 2. No upvalue, no generic `for`
  (no `pairs`, so no hash-order dependence), no CONCAT, no coroutine. Only `lib_QueryWormContainer` is vararg [data].
- Script state between callbacks is plain globals: numbers, booleans, strings, timer slots, small tables of names (`BoomArray`,
  `Crate[i]`, `Movie[i]`) [data]. Each callback runs to completion; nothing yields.
- Every level also runs `stdlib` and `lib_help` (turn.md §6.5). Scripts call stdlib's turn functions themselves: `StartFirstTurn` 51
  (always from `EFMV_Terminated` of the intro), `StartTurn` 54 (from their `TurnEnded` override), `EndTurn` 3,
  `GameLogic_EndTurn_Immediate` 1 [data].

### 23.3 API use (story S / challenges C / deathmatches D: calls, scripts) [data]

Library and stdlib functions (`lib_help.lub`, `stdlib.lub`):

| function | calls | S | C | D | engine side |
|---|---|---|---|---|---|
| lib_SetupWorm | 364 | 25 | 15 | 11 | CopyContainer(databank `WormDataContainer` -> `Worm.DataNN`), sets Active, PlayedInGame, player names from `GM.GameInitData` |
| CopyContainer AIParams.CPUn -> AIParams.WormNN | 195 | 22 | 0 | 11 | per-worm AI level |
| lib_SpawnCrate (+ GameLogic.CreateCrate 1) | 160 | 23 | 13 | 3 | 23 `Crate.*` keys from the `CrateDataContainer` (Type, Contents, NumContents, Index, LifetimeSec, GroundSnap, Parachute, Spawn, FallSpeed, Gravity, TeamDestructible, TeamCollectable, UXB, Hitpoints, Pushable, RandomSpawnPos, CanDropFromChute, WaitTillLanded, TrackCam, Scale, AddToWormInventory, CustomGraphic), then `GameLogic.CreateCrate` (GameLogicService 0x4fdc90) |
| lib_SpawnTrigger | 99 | 17 | 1 | 0 | 10 `Trigger.*` keys (Spawn, Radius, Index, TeamCollect, TeamDestroy, HitPoints, SheepCollect, PayloadCollect, GirderCollect, WormCollect), `GameLogic.CreateTrigger` (0x4fdc90); TriggerLogicEntity 0x5d5730 (init 0x5d5360 reads `Trigger.Visibility`) |
| lib_SetupTeam / lib_SetupTeamInventory | 95 / 85 | 25 | 15 | 11 | Team.DataNN (player name, flag, grave, attachments from GM.GameInitData) / Inventory.TeamNN |
| lib_DisplayFailureComment / SuccessComment / lib_Comment | 80 / 31 / 24 | 25 | 15 | 0 | `CommentaryPanel.Comment` = `Miss.Generic.Lose/Win1-5` or a text id, `CommentaryPanel.TimedText` / `.ScriptText` |
| StartFirstTurn / StartTurn / EndTurn | 51 / 54 / 3 | 25 | 15 | 11 | stdlib turn machine (turn.md §6.1) |
| lib_CreateEmitter / DeleteEmitter(Immediate) | 42 / 34 | 14 | 0 | 0 | `Particle.NewEmitter` at a detail object, returns `Particle.Handle` |
| lib_CreateExplosion | 28 | 7 | 0 | 0 | `Explosion.*` at a detail object, `Explosion.Construct` (0x4fdc90) |
| lib_GetRandom | 20 | 5 | 15 | 0 | `RandomNumber.Get`, `math.mod(RandomNumber.Uint, n)` |
| lib_SetupTeamWeaponDelays | 19 | 1 | 0 | 9 | weapon delays container copy |
| lib_DeathmatchChallengeTurnEnded | 11 | 0 | 0 | 11 | win / lose on AllianceCount, SurvivingTeamIndex, RoundTimeRemaining |
| lib_GetActiveAlliances / GetSurvivingTeamIndex / QueryWormContainer | 6 / 5 / 5 | 6 | 0 | 0 | `WormManager.GetActiveAlliances` / `GetSurvivingTeam`, `Worm.DataNN.Energy` |
| lib_ShakeCamera | 2 | 1 | 0 | 0 | `Camera.Shake.Length/Magnitude`, `Camera.ShakeStart` |

Messages sent directly by the scripts:

| message | sends | S | C | D | handler [disasm: xref of the message handles] |
|---|---|---|---|---|---|
| EFMV.Play (+ `EFMV.MovieName`) | 106 | 25 | 15 | 11 | GameLogicService 0x4fdc90 spawns EFMVMovieLogicEntity (acting.md §19) |
| Worm.Respawn (int slot) | 77 | 15 | 0 | 0 | WXWormManagerService 0x5b5e70 |
| GameLogic.Mission.Failure / Success | 65 / 26 | 25 | 0 | 0 | 0x4fdc90, end of game 0x4fb880 |
| WormManager.Reinitialise | 51 | 25 | 15 | 11 | 0x5b5e70 |
| GameLogic.PlaceMine (string: marker) | 45 | 2 | 2 | 0 | 0x4fdc90, with `Mine.MinFuse/MaxFuse/DudProbability/DetonationType` |
| CommentaryPanel.TimedText | 26 | 12 | 0 | 0 | CommentService 0x5e4ca0 |
| Worm.DieQuietly (int slot) | 26 | 8 | 0 | 0 | 0x5b5e70 |
| GameLogic.PlaceObjects | 20 | 9 | 0 | 11 | 0x4fdc90 (map mines and oil drums) |
| GameLogic.Challenge.Failure / Success | 17 / 15 | 0 | 15 | 0 | 0x4fdc90, 0x4fb880 |
| Crate.Delete (int index) | 13 | 7 | 1 | 0 | CrateLogicEntity 0x5cb330 |
| Weapon.Create / Weapon.Wield / Weapon.PreSelected | 11 / 6 / 1 | 0 | 10 | 0 | LogicalWeaponManagerService 0x566b80 / weapon entities |
| GameLogic.DropRandomCrate | 11 | 0 | 0 | 11 | random crate drop |
| Commentary.NoDefault / Commentary.Clear | 10 / 8 | 3 | 9 | 0 | CommentService 0x5e4ca0 |
| EFMV.End | 6 | 5 | 0 | 0 | W3D-style borders off (acting.md §19) |
| WXMsg.EasterEggFound (string) | 5 | 5 | 0 | 0 | MissionService 0x734065, with `QueryContainer("Lock.EasterEgg.N").State` |
| Crate.RadarHide | 4 | 2 | 0 | 0 | 0x5cb330 |
| Jetpack.UpdateFuel | 3 | 1 | 2 | 0 | JetpackUtilityLogicEntity 0x563f00 |
| WXWormManager.UnspawnWorm | 3 | 1 | 0 | 0 | worm manager |
| GameLogic.DestroyTrigger | 2 | 2 | 0 | 0 | TriggerLogicEntity 0x5d5730 |
| Land.EnablePointLight | 2 | 1 | 0 | 0 | LandscapeLogicEntity 0x478780 |
| GameLogic.StartMineFactory, Particle.NewEmitter | 1 each | | | | |

Data keys (SetData / GetData, by use count): `EFMV.MovieName` 128, `TurnTime` 49, `EFMV.GameOverMovie` 44 (+ `.Off` 1), `DeadWorm.Id` 36,
`HUD.Counter.Value` 30 / `.Active` 15 (read 0x5e5a60), `RoundTime` 30, `CommentaryPanel.Comment` 25 / `.Delay` 23, `Land.Indestructable` 23,
`RoundTimeRemaining` 23, `Mine.*` 21, `HotSeatTime` 18, `Trigger.Visibility` 18, `HUD.Clock.DisplayTenths` 15, `RetreatTime` 15,
`Crate.Index` 14, `Trigger.Index` 14, `GameToFrontEndDelayTime` 13, `Wind.Speed` 8 / `.Direction` 6, `DefaultRetreatTime` 8,
`PostActivityTime` 8, `Jetpack.Fuel` 6 (+ `InitFuel` 1), `Water.Level` 7, `WXD.StoryMovie` 4, `CurrentTeamIndex` 4, `DoubleDamage` 3,
`Challenge.EndlessGun` 2 (read by GunWeaponLogicEntity 0x55cff0), `DamagedWorm.Id` 2, `Payload.Deleted.Id` 2, `SameTeamNextTurn` 1
(read 0x5b4bf0, worm manager), `Turn.Boring` 1, `Turn.MaxDamage` 1, `Trigger.Collector` 1, `Crate.TrackCam` 1, `Mine.Id` 16 [data;
reader addresses: xref].

Containers edited in place (EditContainer ... CloseContainer, 47): `GM.SchemeData` 17 (`HelpPanelDelay` = 0 in all 17), team / alliance
inventories by weapon field name (Bazooka, Grenade, FirePunch, NinjaRope...), `Worm.DataNN` (`ArtilleryMode`, `WeaponIndex`, `ATT_Hat`),
`Team.DataNN` attachments, `kWeaponSuperSheep.LifeTime`, `kMineFactoryData` [data].

Callbacks defined (scripts): Initialise 51, EFMV_Terminated 51, TurnEnded 51, PlayIntroMovie 48 (script-side), Worm_Died 37,
DoOncePerTurnFunctions 24, Crate_Collected 23, TurnStarted 13, Trigger_Collected 10, Trigger_Destroyed 9, Crate_Destroyed 8, SetWind 7,
Crate_Sunk 4, Worm_Damaged_Current 3, Worm_Damaged 2, Payload_Deleted 2, Timer_GameTimedOut 2; plus 29 distinct timer functions
(`StartTimer` 36 calls in 18 scripts, `CancelTimer` 3) [data].

### 23.4 Level movies used by the scripts [data: xom.py types over the 40 story and challenge levels]

163 movies, 1002 tracks. Events: Comment 665, TriggerSoundEffect 474, WormLookAt 402, PlayAnimation 394, CastActor 325, PathCamera 321,
TriggerSpeech 287 (17 levels), WormEmote 235, TimedPathCamera 225, CreateBorders 154 / DeleteBorders 138, WormGestureAt 125,
CreateExplosion 98, ShakeCamera 76, CreateEmitter 65 / DeleteEmitter 22, StopAnimation 58, CutCamera 32, FailureComment 15,
UnspawnWorm 6, DeleteLandframe 2, SpawnWorm 2, SelectWorm 1, SelectWeapon 1, RaiseWater 1, AnimateDetail 1.

### 23.5 Per level: what ends it [data: the functions that send `.Success` / `.Failure`]

Most story `TurnEnded` overrides fail the mission when `RoundTimeRemaining` <= 0 (23 of 25) and only then call `StartTurn`; the success
paths below set `EFMV.GameOverMovie` (`Outro`, `OutroSuccess`) first.

| level | success when | movies (`EFMV.MovieName`) | mid-mission spawns |
|---|---|---|---|
| DinerMight | 4 trigger zones destroyed (each plays its Explosion* movie; the 4th movie's end) | Intro, ExplosionScaffold/Shovel/Chairs/Bar, Reminder | - |
| SneakyBridgeThieves | 8 crates collected (3rd: Midtro + 3 CPU worms respawn) | SneakyBridgeIntro, Midtro | 3 worms |
| BuildingSiteSaboteurs | turn end with `EnemyDead` == 2 | Intro, Midtro, DiggerBlow1-3 | 2 worms |
| TheCrateEscape | crate index 1 collected (index 0: Midtro) | Intro, Midtro, CrateHint | - |
| DestructAndServe | turn end, one team left, building destroyed (then 6 outro worms); `WXD.StoryMovie` Camelot | intro, NowKillWorms, NowDestroyBuilding, EasterMovie | 6 worms |
| StormTheCastle | 8 enemies dead and the time-machine part crate collected | StormTheCastleIntro, CollectPart, EnemySpawnCuts, UseGirderClip | 4 worms |
| TheWindyWizard | 5 triggers destroyed and 4 wizards dead; worm 1 dying fails | WindyWizardIntro, WizardSpawn1-3, MidSequence2 | wizards |
| RobInTheHood | worm 6 (Wally, respawned after 3 enemy deaths, Midtro) dies | Intro, Midtro | 1 worm + 2 crates |
| JoustAboutIt | crate index 1 collected | Intro, Midtro, Midtro2, CollectPart | 7 worms |
| NiceToSiegeYou | the round time runs out (survival); enemies 5 / 6 respawn at turns 9 / 19, catapult movies at 11 / 21; StoryMovie WildWest | Intro, WormCut(2), CatapultStrike(2), Easter Egg | 12 respawn sends |
| MineAllMine | 4 enemies dead (2 player deaths fail) | Intro | - |
| GhostHillGraveyard | 10 worms dead after the gold sequence | Intro, Midtro + a table | 1 worm |
| TinCanWally | outro after the gold cut; a crate destroyed can fail | Intro, Midtro, GoldCut | 5 worms, 4 quiet deaths |
| DoomCanyon | 5 enemies dead, worm 0 dying fails; water raised | Intro | - |
| HighNoonHiJinx | one team left at turn end (player's); StoryMovie Arabian | Intro, Midtro, DestroyGate, EnemyLook1-2 | 13 respawns, 7 quiet deaths |
| TurkishDelights | 3 bad guys dead (checked when their movie ends) | TurkishDeLightsIntro, MidtroCap1-3, BadGuyRespawn1-3 | 12 respawns |
| NoRoomForError | 4 enemies dead (turn end); player death fails | Intro | - |
| CarpetCapers | 4 baddies dead (turn end) | CarpetCapersIntro, KillEnemy | - |
| TraitorousWaters | every gun destroyed (turn end); each turn a gun blasts a house on timers, no house left fails | Intro | 30 triggers, 14 timers |
| GibbonTake | trigger collected -> DoOutroStuff (timer); crate sunk / game time out fail; StoryMovie Jurassic | GibbonTakeIntro | 3 worms |
| FastFoodDino | crate index 9 collected, water rising on a 250 ms timer; crate sunk / destroyed / time out fail | Intro | crates in sequence |
| EscapeFromTreeRex | turn end condition (endmissionsuccess) | Intro, Midtro, MidtroCave | 3 worms (timers) |
| ChuteToVictory | trigger index 5 collected | Intro, Midtro | - |
| TheLandThatWormsForgot | 8 enemies dead and a trigger collected | Intro, FindWorm, KillEnemy | - |
| ValleyOfDinoWorms | trigger collected (then outro worms) | ValleyIntro, Midtro, Outro | 4 worms |
| Challenge* (15) | Crate_Collected / Crate_Destroyed / Worm_Damaged / last target of a sequence (`SpawnNextTarget`); `Worm_Died` or `Worm_Damaged_Current` fails | Intro (+ Outro in 8) | crates or triggers one at a time, HUD counter |

### 23.6 Runtime details for the original bytecode (lot 1) [disasm unless tagged]

- Libraries: XLuaBaseLibrary and XLuaMathLibrary only (0x6958d2), then `lua_checkstack(L, 128)`; chunks load as `stdlib`, `lib_help`,
  then the level script (turn.md §6.5).
- Implementations: SendMessage 0x69638f, SendFloat/Int/StringMessage 0x69655b / 0x696755 / 0x69694b, GetData 0x696b2f, SetData 0x696f64,
  StartTimer 0x6980fa, CancelTimer 0x6982ae, EditContainer 0x6983fa, CloseContainer 0x6988bc, QueryContainer 0x6986bd, CopyContainer
  0x6989f3, echo 0x699048, log 0x69908a.
- Bad calls never raise a Lua error: a wrong argument count, an unknown name (`Incorrect DataID in function 'GetData'`), a non-number
  for a number key (`Data is not a number`) or a denied key (`%s : Data Access Denied`) are logged by 0x6978b9 and the function returns
  nothing. Key types: 0 int, 1 uint (numbers truncated by 0x6fe690), 2 float, 4 string.
- `EditContainer(name)` returns two values, a light userdata lock and the container's fields as a table, counted at +0x158; the
  scripts write `lock, t = EditContainer(n)` ... `CloseContainer(lock)` [disasm + data: lib_help lib_SetAllWormsEnergy SETGLOBAL order].
  QueryContainer returns the table only.
- Timers: each `StartTimer` slot holds a message id (+0xc); the script service's handler 0x6953e9 matches an incoming message against
  the slots, marks the slot fired (+0x18) and calls the global named by the slot. The deadline is TaskManager logical time
  [0x96d030]+0x38 (physics.md §24): game time, stopped with the kernel pause flag, not wall time.
- Engine -> Lua: messages flagged 0x8000 are turned into `A_B_C` by the same handler and called in line (0x698e88).
- `RandomNumber.Get` (GameLogicService 0x4ff0aa): `RandomNumber.Uint` = the low 16 bits of one LCG draw (0x68c015: x·0x41c64e6d +
  0x3039), then `RandomNumber.Float` = another draw's low 16 bits / 65536 (0x68c024).
- Damage (0x5ab7e0): for the active worm (0x95fb98) and damage types other than 5 / 6 (poison) it posts `Worm.Damaged.Current` first,
  then for every hurt worm sets `DamagedWorm.Id`, `DamageTypeTaken` and posts `Worm.Damaged`.
- Ammo (0x50d900): Inventory.WormNN[slot] + Inventory.TeamNN[TeamIndex] + Inventory.AllianceNN[TeamData.AlliedGroup], -1 when any of
  the three is -1; delays come from `Inventory%d.WeaponDelays` of the worm's team. Worm slots: kMaxWorms 16 (assert 0x50c24a), teams 4
  (0x50d350). Defaults [data: LOCAL.XOM]: every Worm / Team / Alliance inventory holds SkipGo and Surrender -1 only; Team.DataNN AlliedGroup NN.
- `GameLogic.ActivateNextWorm` decrements the weapon delays of the team that just played (0x5b5a5f -> 0x4f4df0, turn.md §6).
- Data the scripts touch live in the Tweak files [data]: LOCAL (most keys and Worm / Team / Inventory containers), LVLSETUP (GM.SchemeData,
  GM.GameInitData), WEAPTWK (Wind.MaxSpeed, Water.Level, Mine.*, kWeaponSuperSheep, kMineFactoryData), AITWK (AIParams.*: CPU1-5,
  CPUTest; every AIParams.WormNN starts as CPUTest), HUDTWK (HUD.*), CAMTWK (Camera.Shake.*), DEFSAVE (Lock.EasterEgg.N); levels set
  RoundTime 3,000,000 (50 min) in five story levels, -1 (no round clock) in TraitorousWaters.
- Containers decode with the exe's Serialize field lists; `WXFE_UnlockableItem.State` is an enum (0 kUS_Hidden .. 2 kUS_Unlocked), so the
  scripts' `State == "Unlocked"` tests depend on how enums reach Lua [not traced; ours pushes the number].

### 23.7 Not covered

- The message permission table behind 0x69968b.
- Handler bodies of Worm.Respawn / DieQuietly (0x5b5e70), CreateTrigger and PlaceMine.
- `GameToFrontEndDelayTime` (LOCAL.XOM default 3000, 13 script writes) has no exe string: no reader by literal name was found [data].

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
  then for every hurt worm sets `DamagedWorm.Id`, `DamageTypeTaken` and posts `Worm.Damaged`. Callers: explosions 0x5ae78b (gun hits are
  explosions), Damage.Impulse 0x5ae3f7, fall damage 0x5ac3e0 (type 1), poison 0x5ac060 (type 6), Vapourize 0x5ac160, the Mystery Damage
  crate 0x5cafdc; no phase test, no once-per-turn guard; none at damage 0 (0x5ab805) or with WormData +0xec & 0x800 (0x5ab847, flag not
  named). Drowning posts Worm.Damaged.Current itself (0x5ad77d).
- Drowning, 0x5ad640 each frame (not in states 7 / 8, nor with [0x955640] set): once the worm is under Water.Level less its drown
  offset: `Turn.Boring` 0 if it was > 0; a vital worm (WormData +0xec & 0x100) posts WormManager.SurrenderTeamById; **only if it is
  the active worm** ([0x95fb98] == this, 0x5ad754) Worm.Damaged.Current (0x5ad77d), whose stdlib handler runs EndTurn; then the active
  vampire's share (0x5a9710) and Worm.Drowning (0x5ad7ef). A drowning worm that is not the active one ends no turn [disasm].
- Worm.Died: only WXWormLogicEntity::Cleanup 0x5a6970 posts it (called from the worm's HandleMessage 0x5b07c0 at 0x5b0bbb): SetData
  `DeadWorm.Id` = the worm's slot (+0x30, 0x5a69ef), then Worm.Died (0x5a6a1f), then ActiveWormIndex cleared if it was the active
  one. So `Worm_Died` reads its own worm's DeadWorm.Id, whatever killed it (blast, drowning, fall) [disasm].
- A player's death does not end a story mission by itself: the scripts read GetActiveAlliances / GetSurvivingTeam in their
  `TurnEnded` override, so the end waits for the turn's end (EndTurn, post activity, DoPostActivity) [data]. BuildingSiteSaboteurs'
  `TurnEnded` fails on RoundTimeRemaining <= 0, then on AllianceCount 0 or (AllianceCount 1 and SurvivingTeamIndex 1), wins on
  `EnemyDead` == 2 (its `Worm_Died` counts DeadWorm.Id 1 and 2, the guards), else StartTurn [data: TurnEnded pcs 0-57].
- Ammo (0x50d900): Inventory.WormNN[slot] + Inventory.TeamNN[TeamIndex] + Inventory.AllianceNN[TeamData.AlliedGroup], -1 when any of
  the three is -1; delays come from `Inventory%d.WeaponDelays` of the worm's team. Worm slots: kMaxWorms 16 (assert 0x50c24a), teams 4
  (0x50d350). LOCAL.XOM [data] gives every Worm / Team / Alliance inventory SkipGo and Surrender -1, but GameLogicService's init
  (message 0x40, 0x4f8d4f -> 0x4f5bd0), before the level script loads, empties Inventory.Worm / Team / Alliance.Default and
  WeaponDelays.Default (0x67cbae) and copies them into Worm00..15, Team00..03, Alliance00..03 and Inventory0..3.WeaponDelays: every
  inventory starts at 0 [disasm]. 0x50d900 also returns 0 for WeaponIndex 0x43, a delay > 0 or a worm Allow flag 0 (0x50c800).
- Ammo use, 0x4f4fd0(weapon, worm) (GameLogic.DecrementInventory: the active worm's WeaponIndex; .Id: the message's weapon; DecInventory.
  IdIndex: Old Woman): nothing when any of the three counts is -1; else the alliance's -1 if > 0, else the team's, else the worm's [disasm].
  A weapon or utility crate (0x5c8820) adds its amount to Inventory.Alliance[collector's AlliedGroup], or to the collector's
  Inventory.WormNN when `Crate.AddToWormInventory` was set for it (GameLogicService resets the key per crate, 0x4f2245), unless that
  target count is -1; the Mystery Disarm takes one of a random weapon from the alliance (0x5ca7d9); `GameLogic.IncrementInventory` +1 on a
  named container, `GameLogic.AddInventory` adds a container (-1 sticky, 0x67d0d7); the Old Woman takes from the victim as 0x4f4fd0 and
  gives the thief's alliance +1 (0x592d30, 0x592360). No other writer of a count exists (0x67ce3d callers) [disasm].
- `GameLogic.ActivateNextWorm` decrements the weapon delays of the team that just played (0x5b5a5f -> 0x4f4df0, turn.md §6).
- Data the scripts touch live in the Tweak files [data]: LOCAL (most keys and Worm / Team / Inventory containers), LVLSETUP (GM.SchemeData,
  GM.GameInitData), WEAPTWK (Wind.MaxSpeed, Water.Level, Mine.*, kWeaponSuperSheep, kMineFactoryData), AITWK (AIParams.*: CPU1-5,
  CPUTest; every AIParams.WormNN is a CPUTest copy in the file), HUDTWK (HUD.*), CAMTWK (Camera.Shake.*), DEFSAVE (Lock.EasterEgg.N); levels set
  RoundTime 3,000,000 (50 min) in five story levels, -1 (no round clock) in TraitorousWaters.
- Containers decode with the exe's Serialize field lists. QueryContainer / EditContainer (XLuaCtrLibrary 0x789acb / 0x789a88) push an
  8-byte userdata on the container (a live view) with `__index` 0x789872 -> 0x7893e1; an unknown field raises a Lua error ("Unknown
  field"), a Query write too ("member modification attempted on const container") [disasm]. By field type (0x63e0b3): bool -> boolean,
  int8..int32 / uint8..uint32 / float32 -> number, string -> string; int64 / uint64 / float64 / pointer / **enum** / interface / bitfields
  and non-simple types (vectors, structs) -> their ToString string; arrays only through `Get<Field>` / `Set<Field>` / `Append...` closures.
  An enum's ToString (0x6c7078) is its value name less the shortest common prefix of adjacent names ("Invalid (%d)" past the end):
  `WXFE_UnlockableItem.State` reads "Hidden" / "Purchasable" / "Unlocked", WormData.WeaponIndex "WeaponBazooka" (WeaponNameEnum's prefix
  is "k"). Writes go through FromString (numbers as "%f", booleans "true" / "false") [disasm]; whether an enum FromString takes the short
  name is not traced (the scripts write full names, `"kWeaponShotgun"`).

### 23.7 Not covered

- The message permission table behind 0x69968b.
- PlaceMine, GameToFrontEndDelayTime: §23.9. Worm.Respawn / DieQuietly and CreateTrigger: §23.10.
- Telepad details (PlaceObjects "telepad", 0x4fadb0).

### 23.8 What the scripts drive in the UI and render [disasm unless tagged]

Commentary (CommentService 0x5e4ca0, CommentaryBoxGraphicEntity 0x5dce30):
- `CommentaryPanel.TimedText`: the text of the string id in `CommentaryPanel.Comment` (lookup 0x50b820; unknown: `** INVALID STRING ID: `
  + id), shown for `CommentaryPanel.Delay` ms (GetData default 1000; LOCAL 1200 [data]). An id starting `Miss.Generic.Lose` also stores its
  number (+0x148) for SubtitleGraphicEntity 0x5f90a0, which plays `EFMV/Failures/Failures_Narrator_0N` when it shows that line.
  `CommentaryPanel.ScriptText`: same text (unknown id: the id itself), delay 0. `CommentaryPanel.DebugText`: debug builds only.
- AddComment 0x5e0680(text, delay, flag): queues `<CLS>` (0x5e0620, outside subtitle mode), then the text cut into lines that fit the
  FE.Font box (break at a space or '-'), each line its own queue entry with the delay (0 -> 1200 ms, 0x5e087a / 0x5e05aa). The queue
  holds at most 50 lines (assert 0x5e058a).
- The box pops `<CLS>` + line 1 (time d1), then line 2 if the next entry is not `<CLS>` (time d2): it shows both for d2 if d2 > d1, else
  2 x d1 (0x5dd093); one line shows for d1.
- `Commentary.Clear` (0x5dfed0) empties the waiting queue; the shown box stays. `Commentary.NoDefault` / `EnableDefault` clear / set +0x138,
  which every default comment checks (deaths 0x5e41e0 / 0x5e4960, Win / Draw, crate spawn and pickup texts, turn start, weapon
  comments), reset to 1 per game (0x5de730).
- Subtitle mode (+0x68): `EFMV.Subtitles.On` / `.Off`, sent by EfmvBorderEntity (0x5e6c88, with `HUD.Hide`) when movie borders come up;
  On / Off both empty the queue; the lines then go to SubtitleGraphicEntity, one at a time (acting.md §19 "Borders and subtitles").
  Movie shutdown 0x525540 calls 0x5dff70, which empties it only in subtitle mode. A skipped movie never
  creates its borders (CreateBorders is not Critical), so its Critical Comment events (fields Comment, Duration: TimedText with Delay =
  Duration, 0x525dc0) reach the commentary box.
- Text ids: the mission lines (`M.*`, `C.*`) are in `Language/PC/<Lang>LS.xom` (EngLS 1575 strings), not English.xom [data]. Texts may
  hold `/*Key*/` placeholders (a text id or a data key): `BriefingText.Name0..5` (lib_SetupWorm writes the player's worm names) is used by
  one tutorial movie line only (`T1.EFMV2.Dialogue3`) [data].

HUD:
- CounterGraphicEntity (0x5e5a60 init, 0x5e57c0 update): hidden while `HUD.Counter.Active` is 0; text "%d" of `HUD.Counter.Value`, + "%"
  when `HUD.Counter.Percent` is 1, FE.Font, TextScale 21, `TextColor` (255, 178, 0), `ShadowColor` black, on `HUD.WindBacking` x BackScale
  30, at `HUD.Counter.Position` (222, -190), slid by In_Timer / Out_Timer [data: HUDTWK].
- HudClockEntity (0x5f0ae0 init, 0x5f03f0 round clock): `HUD.Clock.DisplayRoundTime` 0 hides the round time. Shown time: RoundTimeRemaining
  if RoundTime > 0, else ElapsedRoundTime; text "%02d:%02d" (min, s); `HUD.Clock.DisplayTenths` adds a second text "%02d" of
  (ms % 1000) / 10 (hundredths) at size 18; RoundTime -1 shows `FXTXT.Infinity`. The turn digits (0x5efe80) draw only while
  TurnTimeRemaining rounds up to > 0 s, so TurnTime 0 shows none.
- TimerLogicEntity 0x50f100, every 10 ms: ElapsedRoundTime += 10 and RoundTimeRemaining -= 10 (once Timer.StartGame set it, 0x50fa23,
  which also zeroes ElapsedRoundTime), both skipped while GameLogic.PauseGame (+0x69: every timer stops), GameLogic.RoundTime.Pause
  (+0x6f, to .Resume; sent by none of the 51 scripts nor the exe) or while the current logical camera (CameraManagerService [0x95c370]
  +0x2a0[+0x28c], type +0x2c) is 14 (0x50f15f): the type the Path (0x531580) and TimedPath (0x637880) cameras pass to the Camera
  constructor 0x51b570, i.e. every movie camera event, CutCamera included (a one-knot PathCam), from the first to the movie's end;
  TurnTimeRemaining stops with it (0x50f383) [disasm; acting.md §19 "Movie cameras"].
- Team energy bars (EnergyBarManagerEntity, 0x5e9f10 at WormManager.Reinitialise): each team's value is the sum over its Active worms of
  Energy less pending damage (0x5e9a00; a surrendered team 0); a bar is value x `HUD.Energy.MaxLength` / M x 0.5 long (0x5e8250), M the
  largest team total at that Reinitialise (100 if under 1): the strongest team fills the bar, a later Worm.Respawn changes nothing.
- `lib_CreateWXBriefingBox` (`GameLogic.CreateBriefingBox`, WXD.BriefingText / BriefingImage) is defined in lib_help but called by none of
  the 51 scripts: no in-game briefing box in these levels [data].

Particles (ParticleHandlerService 0x5c1530):
- `Particle.NewEmitter`: looks for the level detail named `Particle.DetailObject` (all detail lists, exact name); found: with
  `Particle.Locator` empty the effect `Particle.Name` starts at the detail (0x5c09d0) and its handle goes to `Particle.Handle`; not found:
  nothing, the handle keeps its value. `Particle.NewUserIdEmitter` (movies' CreateEmitter) takes the UserId as the handle.
- Emitter position [disasm 0x5c1530 loop, 0x5c09d0 call]: for each effect emitter the detail's own world position (detail +0x8 / +0xc / +0x10, copied to the call's position argument) is passed to 0x5c09d0, with no crate lookup and no offset: the effect stays at the detail for good and never follows an object. The emitter's own `EmitterOriginOffset` / `EmitterOriginRandomise` (PARTTWK, units, 20 per metre) are added by the effect; WXP_CollectableItem has offset 0 and randomise (6, 2, 6) / (0..) units, WXP_TreasureTwinkle (5, 5, 5) and (10, 10, 10) [data PARTTWK]. A SneakyBridgeThieves marker `CrateN` sits 0.24-0.9 m (map metres) above the land; the crate then rests on it, so the glow stays at the marker, above the crate's centre by the crate's own height less the gap [data + ours].
- `Particle.DelGraphicalEmitter(handle)` (0x5bfde0 -> ParticleEmitterEffectEntity 0x5bcf50): state 3, the emitter stops; `...Imm` also
  frees its particle group and kills the entity at once.
- Level databanks hold their own effects (`WXPL_*`: ParticleEmitterContainer / EffectDetailsContainer, e.g. DINERMIGHT 4 / 1) next to
  PARTTWK [data].

Camera shake (CameraManagerService 0x522c29, CameraShakeManager):
- `Camera.ShakeStart` adds a shake object (0x5242e0: Length ms, Magnitude, handle into `Camera.Shake.Handle`). Each frame an object gives
  (rand % 3 - 1) per axis x Magnitude x (1 - elapsed / Length) (0x5243b0); the sum x 0.02 (0x51dcde) is clamped to `Camera.Shake.Max` 0.01
  and scaled x 1000 units (0x523e60): Magnitude m per axis, 0.5 m at most. Explosions add objects the same way (0x5241c0).

Easter eggs (MissionService 0x734395 -> 0x72c567):
- `WXMsg.EasterEggFound` (string: a WXFE_UnlockableItem, `Lock.EasterEgg.0..4` in DEFSAVE: State 0, DescriptionName
  FETXT.EasterEgg.N, Value 1000 [data]): Achv.TrackAchievement; then, unless State is already 2 (0x67dc36), WXFE.EasterEgg.Name =
  FETXT.EasterEgg.Name.Template % tr(DescriptionName), WXFE.EasterEgg.Coins = FETXT.EasterEgg.Coins.Template % Value, State = 2
  (0x67daaa), WXFE.Shop.Balance += Value, GameOver.EasterEgg = 1.
- At the front end (0x4fd829), unless GameLogic.RestartGame, GameOver.EasterEgg pushes the `WXFE.EasterEggFound` menu (MENUTWKX: a
  paper popup, FullScreenColour (40, 40, 60, 160), the name text over the coins text, a Return item) over the next menu.
- Senders [data]: DestructAndServe (Trigger_Destroyed, egg 0), CarpetCapers (Crate_Collected, 3), TinCanWally, EscapeFromTreeRex (4);
  their Initialise tests the item's State to set the egg up.

Point lights:
- `Land.EnablePointLight` / `DisablePointLight` (LandscapeLogicEntity 0x478e03 -> 0x4757c0): the string's first 4 bytes are compared
  with each registered detail's code (+0xbc list, details +0xa4, 0x477c60); each match calls 0x5cbf40 -> 0x470a40(light index +0x58, on):
  if bit 0 of the light entry differs it is set and, unless [0x955640], the light's chunks are rebuilt (0x470270). The lights are
  created on (render.md "Point lights"), so TurkishDelights' Initialise (PL01, PL04, the only calls in the 51 scripts) changes
  nothing [data + disasm]. `Land.SetPointLightColor` (0x475820, Red / Green / Blue keys) is sent by no script.

### 23.9 Lot 2 keys and messages: mines, factory, water, weapons, turn keys, end of game [disasm unless tagged]

Mines (GameLogicService, keys bound at 0x4f8b11..0x4f8bed: Mine.DudProbability +0x188, MinFuse +0x18c, MaxFuse +0x190, Mine.Id +0x194,
Mine.StartsMidAir +0x198):
- `GameLogic.PlaceMine` (0x4fdf90, string arg): the detail object of that exact name (0x4f9400, strcmp 0x638a62, +0x3c; none: assert
  "Detail object not found"), then CreateMine 0x4f9630 at its position.
- CreateMine 0x4f9630: one LCG draw for a debug log (0x4f964e), a ParabolicPayloadLogicEntity of kWeaponLandmine at the point; +0xa0 =
  !Mine.StartsMidAir (LOCAL 0: the mine is dropped onto the ground, no ArielFx, 0x581978); unless WormPot MineRespawn (+0x45) a dud roll
  (+0x5d = DudProbability > rand01); then fuse +0x24 = MinFuse + trunc((MaxFuse - MinFuse) x rand01) ms; Mine.Id = the new task id.
  Callers: PlaceObjects, PlaceMine, CreateRandomMine, RespawnMine. The mine factory's 0x4f9c40 is the same without the debug draw, with a
  velocity, starting in flight. Only these two set the dud flag (+0x5d writers: 0x4f9818, 0x4f9dec; 0x5804c2 clears it at construction,
  0x58117f under MineRespawn): a mine laid with the Landmine weapon never fizzles.
- `GameLogic.PlaceObjects` 0x4fb490 (one LCG draw first): every level detail whose name is exactly "mine" -> CreateMine, "oildrum" ->
  0x4facb0, "minefactory" -> CreateMineFactory 0x4f64f0, "telepad" -> 0x4fadb0.
  Placed objects [disasm]: CreateOildrum 0x4facb0 copies the detail's position into OilDrumLogicEntity +0x20 and does nothing else; the drum's
  mesh origin is its base (Bundl09 OilDrum y 0..20 units) and its fall 0x5d1c60 casts a point from that position, landing at the hit
  (position = the ground contact, sphere radius 9 at it), so a map drum rests with its base on the ground. Random mines and drums (0x4f26b0)
  get hit + (0, r, 0) with r = 3 (Landmine Radius) / 9 (drum), then fall to the ground.
- Mine.DetonationType is read at each landmine Detonate (0x581113): -1 Random, 0 keeps kWeaponLandmine's DetonateMultiEffect (0 =
  kDT_Random [data]), 1..4 that type; Random -> (rand & 3) + 1 (weapons.md "DetonateMultiEffect handling").
- `Payload.Deleted` is posted by every payload's teardown (0x580560) with Payload.Deleted.Id = its task id and .LastPosition: GibbonTake
  and MineAllMine answer it (Payload_Deleted) to put a mine back [data].

Mine factory (MineFactoryLogicEntity 0x5d0510; kMineFactoryData [data]: NumMineActivation 20, NumTurnsInactive 7, SafeRadiusPadding 82,
DamageMagnitude 100, ImpulseMagnitude 0.6, Worm / Land / Impulse radii 100; MineVelocityX/Y/Z are "Obsolete" and absent):
- Created by PlaceObjects on the "minefactory" detail (DeathMatch6) or stdvs's CreateRandMineFactory (0x4fe1a0: 0x4f26b0 with a
  sphere of 40 + 5 units at the hit + (0, 40, 0); the column top it returns goes to CreateMineFactory, whose init snaps it down; all
  100 tries failed: none). stdvs Initialise sends it after the sudden-death check when GM.SchemeData.MineFactoryOn, and its
  DoOncePerTurnFunctions sends StartMineFactory after DropRandomCrate [data]; LOCAL schemes with MineFactoryOn 1: Bng, Allaction,
  MegaPower, Mystery, Thekitchensink, and WXD.DefaultSchemeData [data]. One at a time (GLS +0x208). Init 0x5d0170: a 1000-step ray of 1 unit down snaps it to the land; Land.SpawnPiece
  "MineFacCollisionSmall" / "MineFacCollisionBig" (Bundl09 FactoryCollision1 / 2: solid boxes centred on pos + (18.96, 15.96, 1.08) half
  (10.5, 16, 8.4) and pos + (-5.28, 20.76, 0.96) half (12, 20, 12) units [data]), never removed; counter = NumTurnsInactive; land maxY kept.
- `GameLogic.StartMineFactory` (0x5cfe40, DM6's DoOncePerTurnFunctions): counter - 1; at <= 0, if the kWeaponLandmine payloads in play
  (every mine) are fewer than NumMineActivation (read then), registers the "Mine Factory" active object, toSpawn = min(act - count, 10),
  counter = NumTurnsInactive, state 1.
- Update 0x5d0b00 (strict t > deadline): state 1 serves MineFactoryCamera (SimpleCam 1 / 0.1, look-at pos + (-8, 45, 0), camera that +
  (0, 50, 300) clipped by the land, kept if over 30 units away), posts MineFactory.Start (clip MineFactoryStart, weapons/MineMachineOperate
  loop), deadline + 2000; state 2 posts MineFactory.Fire (the loop stops), + 291; state 3 spawns WXP_LandMineUpShot x toSpawn at the
  Payload_Spawn node and posts MineFactory.FireEnd (WXP_MineMachineShot there), + 708; state 4 -> 5; state 5 PlaceMines 0x5d05c0, the active
  object released. Clip lengths from Bundl09 MineFactory [data].
- PlaceMines: the alive worms' positions; up to 35 tries: a random one of them, angle = rand x 2 pi, P = (x + sin a x 128, land maxY + 10,
  z + cos a x 128); a ray down to land minY must hit land above Water.Level with normal y >= 0.9, at >= 127 units (Landmine ArmingRadius
  45 + SafeRadiusPadding) in xz from every worm; then 0x4f9c40 at P with velocity (0, -0.3, 0) units / ms.
- Destroyed by any Explosion within its LandDamageRadius + 40 units of pos + (0, 40, 0) (0x5cfbc0), a positive Damage.Impulse, or water over
  pos + 10 units: 100 ms later an explosion at pos (impulse centre 5 units below) with kMineFactoryData's magnitudes and radii,
  WXP_Explosion_MineMachine, weapons/ExplosionLarge; MineFactory.Deleted; later StartMineFactory calls do nothing.

Water: `Water.Level` (WEAPTWK 0) is the level the drown test, payload water, camera floors and the AI read through their key handles: a
SetData moves it at once (the water graphic follows at Water.RiseSpeed.Graphic). The movies' Critical RaiseWater adds its Delta.

Weapons (LogicalWeaponManagerService 0x566b80):
- `Weapon.Create` 0x565770: keeps the active worm's WeaponIndex if 0x50d900 finds it usable, else writes ids 0, 1, ... (0x26 Skip Go and
  0x27 Surrender skipped) into Worm.DataNN.WeaponIndex until one is usable, kWeaponUndefined 0x43 when none (0x56582d); then posts
  Weapon.PreSelected. Only scripts send it: a match's turn start never runs it. GameLogic.Turn.Started (0x566cf0): current and
  secondary weapon (+0x94, +0x98) = kWeaponUndefined, +0x8d = 1 then 0x565200(0) (UtilityFire group off, Fire on): every turn starts
  empty-handed. ActiveWormHudInfoEntity 0x5d7bb0 shows the weapon box on Weapon.Selected only for a usable weapon other than 0x43,
  and hides it on Weapon.Delete.
- `Weapon.PreSelected` 0x566c77: 0x566690(0) deletes the weapon / utility entities (the tool-out rule may keep them), then WeaponSelected
  0x565d30 builds WeaponIndex's item, no ammo test.
- `Weapon.Wield`: WeaponAccessoryEntity 0x597739 only: a holstered (+0xb0 0) weapon is drawn (state-0 taunt path, EquipSfx): no gameplay.
- `Challenge.EndlessGun` (LOCAL 0; Sniper / Sniper2 1): read at the gun's init (0x55d1da): bCanMoveBetweenShots forced on; each batch
  (0x55efbf) ends the gun only when shots >= NumberOfBullets and EndlessGun == 0: unlimited shots, one ammo (the first FirePressed's
  DecrementInventory), movement between shots, no Weapon.Fired, no retreat, no turn end.
- `Jetpack.InitFuel` (TWEAK 7500; FastFoodDino 25000): copied into Jetpack.Fuel by the jetpack entity's init 0x563440, i.e. each time the
  jetpack is selected (WeaponSelected creates a new entity); Jetpack.Fuel is the live fuel (thrust writes fuel - 20). Jetpack.UpdateFuel
  only refreshes the stats entity [disasm 0x56428c].

Turn keys:
- `SameTeamNextTurn` (worm manager +0xd8, 0x5b4bf0): ActivateNextWorm 0x5b59b0 then skips GameLogic.DecrementWeaponDelays and takes the
  front worm of the last worm's team queue (0x5b5630), the alliance queues untouched; the handler writes 0 back (0x5b60e3).
- `Turn.Boring` / `Turn.MaxDamage` feed the acting reactions only: damage (0x5ab884) or water (0x5ad6c8) sets Boring 0, a full-strength
  explosion hit (0x5ae6b5, sentry 0x56d3a2) MaxDamage 1; TimerLogicEntity's StartPostActivity posts Missed when Boring > 0 and a payload
  was fired; ApplyDamage 0x5b22a0 picks Mistake, FirstBlood, MaxDamage (MaxDamage == 1), Boring / DamageInflicted, then sets MaxDamage 0,
  Boring 1 if it was 0. TinCanWally's Crate_Destroyed (Boring 0, MaxDamage 1) thus forces the MaxDamage reaction.

AI level: AIService's init (message 0x40, 0x4b3390), before the level script loads, copies AIParams.CPU2 into AIParams.Worm00..15
(CPUTest never takes effect); WormManager.Reinitialise copies CPU<Team.DataNN.Skill> over a worm's params when Skill is 1..5 (multiplayer
only, Skill is 0 in the story levels) [disasm 0x4b3820]. An AI worm the script gives no AIParams.CPUn plays at CPU2.

End of game (GameLogicService 0x4fb880 tail 0x4fd27a): Mission / Challenge / Tutorial Success / Failure, Win and Draw end alike.
- `GameToFrontEndDelayTime` (LOCAL 3000; 13 scripts set 1000) is never read: data keys resolve through a character trie
  (0x6a3ef0 -> 0x6a2d60), so a reader needs the literal, which no binary of the install holds [disasm + data].
- RestartGame / QuitGame / DrawImmediately / ReplayRound, or `EFMV.GameOverMovie.Off` != 0: GameLogic.GotoFrontEnd at once.
- `EFMV.GameOverMovie` non-empty: that movie plays (0x4f52c0); its EFMV.Terminated posts WXMsg.DoStoryMovie (Story with WXD.StoryMovie) or
  GotoFrontEnd, no extra delay.
- Else GameOverLogicEntity (init 0x4ffbd0, 20 ms tick 0x4ff8d0; mode 1 failure, 2 success / win with music/victory, cheer and fireworks,
  0 draw): state 0 on MostRecentlyActiveWorm (or the first active worm) until the count passes 4000 ms, input ignored; state 1 the orbit
  (unless Script.NoOrbitCamera) for 5000 ms offline (15000 online), any input (Input.SomeInputFrom) ends it; state 2 fades both sounds
  over 1000 ms, then GotoFrontEnd: about 10.06 s offline. Game time; the world and the scripts keep running (no pause message).
- GameOverLogicEntity shows no result text of its own (its messages: EFMV.Start, FE.DeleteMouse, PiP.SlideOff, WXMsg.AnimDivide, HideInGameMenuBackground, KillAllPopUp, KillMenuNamed, GotoFrontEnd; docs/w4m/engine.md class table) [disasm]. A Mission / Challenge / Tutorial result is the front-end WXFE.WinMission / WinChallenge / WinTutorial screen only [data: frontend.md match flow].

### 23.10 Worms, triggers and crates the scripts drive (lot 2) [disasm unless tagged]

Message delivery:
- `0x6910e4` hands a message to the relay (0x68b89e -> 0x68cb82), which calls the target task's HandleMessage in line: a callback a
  script's own SendMessage causes runs before that SendMessage returns. TinCanWally relies on it (`g_bCrateDeletePhase = true`,
  `Crate.Delete`, `= false` around its own Crate_Destroyed) [disasm + data].

Worms (WXWormManagerService HandleMessage 0x5b5e70):
- `Worm.DieQuietly` and `WXWormManager.UnspawnWorm` (int slot) both run 0x5b4af0: no worm in the slot logs "request to unspawn a worm
  which does not exist"; else 0x5aaac0(0) on it, ActiveWormIndex = -1 if it was the active one, WormData.Active (+0x124) = 0, the logical
  worm is deleted (0x68b927) and the team lists updated (0x5b3f60). No death blast, no gravestone, no Worm.Died.
- `Worm.Respawn` (int slot < 16) 0x5b4f70: a worm already in the slot logs "A worm which currently exists tried to respawn" and nothing
  happens. Else PlaceWormAtSpawnPoint 0x5b4180 (WormData.Spawn), and only if WormData.Active: SpawnWorm 0x5b31b0, its speech bank, and
  with IsAllowedToTakeTurn (+0x12d) its team joins its alliance's list and the worm its team's (+0x128 PositionInTeam = its index).
- WormManager.Reinitialise (0x5b5bf0) sets CurrentTeamIndex -1 (0x5b5c4c); ActivateNextWorm 0x5b59b0 sets ActiveWormIndex and
  CurrentTeamIndex; with no worm to activate it asserts `m_nActiveWormIndex!=-1` (0x5b5b0f). Not traced: what the release build does
  then (ours runs the turn's clocks without a worm, docs/missions.md "Worms").
- EFMV SpawnWorm {WormId, DataId}: CopyContainer(DataId, Worm.Data<WormId>) (same class asserted), then Worm.Respawn(WormId); UnspawnWorm
  {WormId}: WXWormManager.UnspawnWorm (EFMVMovieLogicEntity 0x525e7b, 0x525fa0). TheWindyWizard's intro has a Critical UnspawnWorm [data].
- The AI's allies (0x4a4790, used by the target value 0x4a9260) are the worms whose team has the same AlliedGroup colour (0x50cae0) as
  the thinking worm's [disasm]; TraitorousWaters' villagers (VillageTeam AlliedGroup 0) are the player's allies [data].

Triggers (TriggerLogicEntity, vtable 0x862c78, HandleMessage 0x5d5730, init 0x5d5360):
- Keys read at creation: Spawn (the detail object by name, its exact position, 0x5d4f30; none: logs "Couldn't find Trigger Spawn
  Location" and takes the vector at 0x96e878), Index +0x30, Radius +0x2c (<= 0: "Error Invalid Trigger Radius", then 1), HitPoints +0x34
  (< 1: 1), TeamDestroy +0x38, TeamCollect +0x3c, WormCollect +0x40, SheepCollect +0x44, PayloadCollect +0x48, AffectsAI +0x4c,
  GirderCollect (!= 0: +0x52). `Trigger.Visibility` 1, with AppData +0x9c bit 0 (cleared only by the `/TRIGGERSINVISIBLE` switch,
  0x4dabc6), creates a TriggerGraphicEntity: resource `Trigger.Ball` (TriggerSphere.xom in Bundl09: a 2-unit ball, one 8x8 texture of
  RGBA (255, 30, 0, 125), XBlendModeGL SrcAlpha / InvSrcAlpha [data]) scaled by the radius (0x58bef8).
- Its collider is a sphere of Radius; mask 0x5d4b50: 1 (worms) if TeamCollect is in [-1, 3] or WormCollect in [-1, 15], 0x80 if
  SheepCollect is in [-1, 3], 8 if PayloadCollect is, 0x200 with GirderCollect. Spheres touch when d < r1 + r2 (0x516350).
- Update 0x5d5180, per overlapping collider: a worm (flags 1, 0x5d4d70) collects if TeamCollect is -1 or its TeamIndex and WormCollect -1
  or its slot (Collector = the slot); collider flags 0x88 (sheep-like payloads) with SheepCollect < 4 (0x5d4df0) and flags 8 (payloads)
  with PayloadCollect < 4 (0x5d4e40) need the active worm's team (or -1), Collector = ActiveWormIndex. Then 0x5d49b0: SetData
  Trigger.Index and Trigger.Collector, the message by kind (table 0x5d4b3c): Trigger.Collected (worm), Trigger.SheepCollected (set by
  the payload path), Trigger.PayloadCollected (set by the sheep path), Trigger.GirderCollected; the trigger deletes itself.
- Explosion 0x5d4cb0 (not collected nor destroyed): R = WormDamageRadius + Radius, d² = |trigger - damage epicentre|²; if d² < R²,
  damage = WormDamageMagnitude (R² - d²) / R², + 1 when it truncates under 1. 0x5d47f0: only if TeamDestroy is -1 or CurrentTeamIndex;
  HitPoints -= trunc(damage); <= 0: Trigger.Index, Trigger.Destroyed, deleted; else Trigger.Index, Trigger.HitPoints, Trigger.Damaged.
- GameLogic.DestroyTrigger (int): every trigger of that Index deletes itself (Trigger.Deleted, which no script answers).
- Data: of the 51 levels' scripts only Trigger_Collected (10) and Trigger_Destroyed (9) are defined; HitPoints is 0 or 1, so one blast
  in reach destroys [data].

Crates (CrateLogicEntity, vtable 0x8619a0, HandleMessage 0x5cb330, spawn 0x5c9bd0, keys 0x5c7bb0):
- Keys read at creation: Index +0x24, NumContents +0x48, Hitpoints +0x4c (x Crate.HitpointsMultiplier when > 0, truncated, 0x5c7f88:
  TWEAK 25 x 0.5 = 12), LifetimeTurns +0x50 (-1 per GameLogic.Turn.Ended, 0 destroys it), Parachute (+0x65), Gravity (+0x64),
  TeamDestructible +0x70, TeamCollectable +0x6c, AddToWormInventory +0x74, UXB +0x67, Pushable +0x66, DelayMillisec (no collision, fall
  or blast until then, 0x5cbd91), WaitTillLanded, TrackCam (+0x101: the crate camera once), IsStatue, LifetimeSec (expiry +0x58),
  FallSpeed (velocity (0, -FallSpeed / 1000, 0) units/ms), Scale (sphere 10 x Scale units, 0x5c5700), CanDropFromChute (+0x100, for
  Crate.LooseChute).
- Spawn 0x5c9bd0: the "Crate Spawn" active object (released when WaitTillLanded != 1); RandomSpawnPos 0: the detail named Crate.Spawn,
  its exact position (0x5c8ed0), 1 random (0x5c6560), 2 Crate.ExplicitSpawnPos. GroundSnap 1 (0x5c80a0): a ray down, the hit + the radius,
  landed, no chute, no active object; no land below: y = Water.Level, sinking, WXP_WaterSmallSplash.
- Type 0x5c9200: weapon 0, health 1 (heals NumContents, 100 under the Wormpot flag), utility 2, target 3, mystery 4, custom 5;
  CrateGraphicEntity 0x5c4d10 draws Crate.Weapon / .Health / .Utility / .Target / .Mystery, a custom crate the Crate.CustomGraphic
  resource (a level detail mesh: D02_04 gems, D04_05 ...) [disasm + data].
- Fall 0x5c9420 only with Gravity; Gravity 0 never moves and drops the active object (0x5c96d2).
- Collision 0x5cb7e0: a worm (flags 1) -> 0x5c8370: TeamCollectable -1 or the worm's team's AlliedGroup (TeamData +0x6c); UXB marks it
  to detonate; else collected by that worm. Collider flags 0x88 (sheep-like payloads) -> 0x5c9750: the same for the active worm.
  Collection 0x5cb5a0 by type (table 0x5cb7c4): weapon / utility add NumContents as a u8 to the alliance's inventory (0xff infinite; the
  worm's with AddToWormInventory, 0x5c8820: that container's count alone is read as a u8, left when 0xff, else set to count + NumContents
  as a u8, so NumContents -1 on 0 gives infinite) [disasm], health 0x5c8660 (energy + NumContents, ApplyDamage, Worm.Antidote), mystery 0x5ca1f0,
  target and custom a pickup sound only; then Crate.Index, Crate.Collected.
- Explosion 0x5c9a10 (active, not collected): d = |crate - damage epicentre|; d < LandDamageRadius: WormDamageMagnitude (R - d) / R to
  0x5c87a0 (TeamDestructible -1 or the active worm's AlliedGroup; Hitpoints -= trunc; <= 0 destroys). Then a Pushable crate within
  ImpulseRadius of the impulse epicentre gets v += ImpulseMagnitude (R - e) / R away from it and leaves the ground (0x5c5ae0). Bullets
  (Damage.Impulse, 0x5c8a90): Hitpoints -= the damage, same team rule.
- Destroyed 0x5c5810, any type: an ExplosionMessage of Crate.WormDamageMagnitude / ImpulseMagnitude / WormDamageRadius / LandDamageRadius
  / ImpulseRadius (impulse centre one radius under the crate), WXP_ExpiryExplosion, Crate.Index, Crate.Destroyed.
- Crate.Delete (int Index) 0x5c5cf0, if not collected or blown: Crate.Index, Crate.Destroyed, deleted, no blast. Crate.RadarHide /
  RadarDisplay (int Index): +0x86 off / on (on by default, 0x5c61fa), read by the radar 0x5f6aa0.
- Position [disasm]: the crate's position (+0x2c) is its sphere centre, radius +0x44 = 10 x Scale units (0x5c5700, asserted > 0). Marker spawn
  0x5c8ed0 copies the detail's position unchanged (no offset). GroundSnap 0x5c80a0: a land ray down from that position, no limit and no
  water test; hit: position = hit, then y += radius (0x5c8153), landed (+0x62), chute off (+0x65); no hit: y = Water.Level, sinking (+0x69),
  WXP_WaterSmallSplash. Fall 0x5c9420 casts a point 1 radius under the centre (0x5c94d0 `y - [+0x44]`, 20 ms parabola 0x466ae0, land only):
  a hit within 20 ms is a landing and 0x5c8900 gives v = 0.2 (vx, -vy, vz), at rest under 0.02 units/ms position = the hit + radius (0x5c89b1..0x5c89cb).
  SneakyBridgeThieves Crate5 [data: .xan cells at 20 units per voxel]: the bridge rail top lies 9.1 units under the marker and the rail is
  6.5 units thick on its column, so the Fall point 10 units under the centre starts inside the rail [assumed: a cast starting in land lands at once].
  Gravity 0 never casts or moves. RandomSpawnPos 1 (0x5c6560): x, z uniform in the team-0 spawn box (tables 0x955788 centre / 0x955800 half size,
  both set at Land.Import 0x477060), a land ray from Land.MaxHeight (+0x315c), 100 tries each needing the hit above Water.Level and no worm
  collider on the column; accepted: position = hit + 300 units (0x5c6792); all failed: the last draw's column top.
- Data [data: the 470 containers of the 51 scripts]: Scale 1 on 306, 1.5 on 107 (all pinned), 2 on 27, 2.5 on 1, 3 on 4, 0.0001 on 25 (targets);
  Type strings come capitalised too (Weapon, Utility, Target, Health); GroundSnap 1 on 48 (28 of them with Gravity 1), Gravity 0 on 233,
  Parachute 0 with Gravity 1 on 21, WaitTillLanded 0 on 69, FallSpeed 0 on all.
- Data: none of the 470 CrateDataContainers of the 51 levels changes LifetimeSec (-1), LifetimeTurns (-1), FallSpeed (0), UXB (0),
  RandomSpawnPos (0), DelayMillisec (0) or Showered (0) [data].

# W4M map: where things live in the original game

This is a general map of Worms 4 Mayhem (PC, Steam *WormsXHD*) for future agents, read from the user's own install. It holds facts only: addresses, names, structures and values. It contains no game asset and no disassembly.

- Addresses are virtual addresses in `WormsMayhem.exe`, PE32, image base 0x400000.
- Distances are in W4M world units, 20 per metre.
- Topic docs go deeper:
  - `docs/w4m-formats.md`: file formats.
  - `docs/camera-w4m.md`: cameras.
  - `docs/weapons-audit.md`, `docs/worm-reactions.md`, `docs/death-sequence.md`.

Confidence tags:

- **data**: read in a game file (exe strings, tweaks, scripts, banks).
- **disasm**: deduced from the code.
- **assumed**: inferred, not verified.

Attributing a function to a class is reliable when the function comes from a vtable. When it was inferred from the nearest `.cpp` assert string, it can be wrong near file boundaries.

## 0. Tools (`tools/w4m-re/`, see its README)

| Need | Command |
|---|---|
| strings with VA | `pe.py str REGEX` |
| classes, bases, vtables | `pe.py rtti REGEX` |
| serialised fields of a class | `pe.py schema CLASS_RE` |
| annotated dwords (tables, records) | `pe.py words VA N` |
| who references a string / VA | `xref.py [--callers] 'Land.Center'` or `xref.py 0x5aa7f0` |
| disassemble a function | `disasm.py VA` (`--before N --after N` for a window) |
| XOM containers | `xom.py list FILE [TYPE_RE]`, `xom.py dump FILE NAME\|#IDX` |
| all tweaks as JSON | `tweak.py` (to `~/.cache/w4m-re/tweaks/`), `tweak.py -g REGEX` |
| Lua scripts | `lua.py stdlib.lub --code`, `lua.py --all 'SetData\("Camera'` |

Python access: `import scan; d = scan.index()` gives `d['calls'][target]`, `d['refs'][va]` and `scan.func_start(va)`.

## 1. XOM classes and the serialisation schema (disasm, verified on data)

**Class registry.** Each XOM class is registered by a static init stub:

- the stub pushes `Serialize`, a version, a factory, the parent type object and an info block (`info[0]` is the class name);
- it then does `mov ecx, typeobj` and `call 0x6c3c99` (abstract classes call `0x6c5015` instead);
- there are 486 classes, read by `PE.xclasses()`;
- these names are complete, whereas the XOM type table cuts them at 31 characters.

**Field records.** Each record is three u32:

| u32 | Content |
|---|---|
| 1 | name pointer |
| 2 | `flags << 24 \| index << 16 \| struct offset` |
| 3 | type object |

- Scalar records are in `.data`, array records (flag 1) in `.rdata`.
- Flag 4 marks transient fields (caches) that are not written to files.
- 0x20 = fixed-size array.

**Order in files.** Each class's `Serialize` pushes its records in file order, then calls its parent's. So a container is `CTNR`, 3 header bytes, the derived class's fields, then the base class's fields.

- `X*ResourceDetails` = Value, then `Name` (str), then `Flags` (u32).
- A field with struct offset 0 is absent from some files (`SchemeData.AssistedShotSettings`).

**Type objects.** They are created at runtime; their vtables name them:

| Type object | Type |
|---|---|
| 0x967614 | Boolean |
| 0x967620 | Uint8 |
| 0x96762c | Int8 |
| 0x967638 | Uint16 |
| 0x967644 | Int16 |
| 0x967650 | Uint32 |
| 0x96765c | Int32 |
| 0x967668 | Uint64 |
| 0x967680 | Float32 |
| 0x967698 | String (varint index into STRS) |
| 0x9676b0 | Bitfield64 |
| 0x9676c8 | Pointer |
| 0x9676d4 | Enum (u32) |
| 0x970b94 | Container (varint ref) |
| 0x96e9xx / 0x96ebxx | vec2/vec3/vec4, colours (RGB8 0x96e970, RGBA8 0x96e988, float RGB/RGBA 0x96e910/0x96e928), matrices 0x96ecb8 (4x3) / 0x96ecd0 (4x4) |

**Coverage.** `xom.py check` decodes every container to its exact end in all 38 `Tweak/*.XOM`, the level databanks and the language files. The only exception is one `WXFE_SoftwareKeyBoardData` in `PERSIST.XOM`. Bundles still need `tools/w4m-models`, because splitting them on `CTNR` is wrong.

## 2. Services, logic entities and messages
Source: `WormsMayhem.exe` (PE32, base 0x400000), RTTI + objdump. Confidence: data / disasm / assumed.

### Message system (disasm)

| Item | Fact | Conf. |
|---|---|---|
| Message handle (global `g_Msg*`) | 8-byte struct: `+0 WORD id`, `+2 BYTE flags (bit0 = resolved)`, `+4 char* name`. 3635 handles, created by static initialisers `push "Name"; mov ecx,G; call 0x68bb9f` (ctor), atexit dtor 0x68bbcd. Ex: 0x95c5b4 = `Input.BlimpViewPressed` (init 0x7e3400) | disasm |
| Resolve id | `0x68bbe6(this=handle, bool assert)`: lazy name->id via `0x690d44(name,&id)`; on failure logs `"Message not found : "` (0x884c54). 2488 call sites | disasm |
| Compare id | `0x68bcce(WORD msgId, handle*)` returns msgId == resolve(handle). Used in HandleMessage switch chains (1224 sites) | disasm |
| Subscribe | `0x690e3e(task, &{.., id}, &member_token)`: called right after resolving a handle; stores the subscription in a member of the listener. 1158 sites; mostly in one init function per class. Uses vector at 0x96d0e4/0x96d0e8 | disasm |
| Send | Message object: `+0 vtable` (base 0x81aa14), `+4 WORD id`, payload after. Built inline or via typed ctors 0x69151a (vt 0x884fac), 0x691951 (vt 0x8850d4), 0x6919e3 (vt 0x8850e4); posted by `0x6910e4(msg)` through dispatcher global 0x96d090 (returns E_FAIL 0x80004005 if null) | disasm |
| Built-in task messages | Default handler 0x68b79e: id 0x40 -> `this+0x14 = param & 0x7fffffff` (schedule/wake), 0x41 -> `msg+0xc = 1` (ack/alive), 0x42 -> `this+0x14 = -1` (sleep). CMS HandleMessage also special-cases 0x40/0x42 first | disasm (meaning assumed) |
| Registry | `MessageRegistrationService` (vt 0x884ea0), `MRS.MaxMessages`, `Core.MessageBufferSize` strings; `MainServiceStartupLogicEntity` fn 0x508d80 prints "Verifying Message IDs..." | data/disasm |

### Common vtable layout (Service / LogicEntity, stdcall `this` on stack)

| Slot | Service 0x884b7c | LogicEntity 0x8850b0 | Meaning | Conf. |
|---|---|---|---|---|
| 0 | 0x68b6eb | 0x68b6eb | QueryInterface (0x68b650 lookup) | disasm |
| 1 | 0x68b6bf | 0x68b6bf | AddRef (derived: 0x5436b0, refcount WORD at this+4, 0xffff check) | disasm |
| 2 | 0x68b9fc | 0x691899 | Release / destroy (lock dec WORD this+4) | disasm |
| 3 | 0x68b94a | 0x6917e7 | GetClass: returns XOM class object (Service 0x96cf1c, CMS 0x95c374) | disasm |
| 4,5 | 0x68ba98 | 0x68ba98 | E_NOTIMPL stubs | disasm |
| 6 | 0x68baa4 (0) | 0x68b794 (-1) | unknown query (returns 0/-1; many override with 0x513880 = -1) | disasm |
| 7 | 0x68baad | 0x68b79e | HandleMessage(msg) (proved: CMS 0x522710 refs "CameraManagerService::HandleMessage ...") | disasm |
| 8+ | - | - | BaseWeaponLogicEntity adds slots 8-15 (0x549a20..0x54a370), weapon-specific virtuals | disasm |

Tables below: listens = handles subscribed via 0x690e3e; sends = handles resolved just before a send/ctor call; #handled = ids compared in HandleMessage. Attribution: function -> class by vtable slot or by nearest preceding function referencing the class .cpp assert string (MSVC unit order) => **assumed** for edge functions; message names are **data**.

### Core services

| Class | Role | Base | vtable | .cpp str | HandleMessage (slot 7) | subscribe fn(s) | listens | sends | #handled |
|---|---|---|---|---|---|---|---|---|---|
| AISceneGraphService | AI world view | Service | 0x825a7c | 0x82579e | 0x4b2d10 | 0x4ae0c0 | `AI.PopulatePathingNodes`, `Land.NewShape` |  | 0 |
| AIService | CPU turn planning, issues worm commands | Service | 0x825cf8 | 0x825cd4 | 0x4b4260 | 0x4b3390 | `AI.ClearActions`, `AI.ExecuteActions`, `AI.GameEnded`, `AI.PerformDefaultAITurn`, `AI.PerformFireAtTargetAction`, `AI.PerformMoveAction`, `AI.PerformPathMoveAction`, `AI.PerformSetWeaponAction`, `GameLogic.AITurn.Started`, `GameLogic.EndTurn`, `GameLogic.GameLoadComplete`, `GameLogic.GotoFrontEnd`, `GameLogic.PauseGame`, `GameLogic.ResumeGame` (+1) |  | 9 |
| AchievementsTrackingService |  | Service | 0x86bd9c | 0x86bd40 | 0x61c2f0 | 0x61bff0 | `Achv.TrackAchievement`, `FE.LoadSaveIn`, `FE.LoadSaveOut`, `Input.Menu.Cancel`, `Input.Menu.Delete`, `Input.Menu.Select` | `App.AchievementError`, `App.AchievementUpsell`, `App.Resume`, `FE.ProfileSave`, `PM.PurchaseGame`, `WXMsg.CreatePopUp`, `WXMsg.KillPopUpNamed` | 6 |
| ActiveObjectRegistrationService |  | Service | 0x830fb8 | 0x830f8c | 0x4d3cb0 |  |  | `GameLogic.NoActivity` | 0 |
| AndyStartupService |  | Service | 0x853a7c | - | 0x512220 | 0x512090 | `Input.KeyTyped` | `Land.Import`, `Land.LoadMaterials` | 1 |
| AppDataService |  | Service | 0x8315d0 | 0x8311a4 | 0x4d9410 | 0x4d9410 | `FE.LoadSaveIn`, `FE.LoadSaveOut` | `FCS.StartService`, `RefreshDLCLocks` | 3 |
| AppRootService |  | Service | 0x832bb0 | 0x832b80 | 0x4db7f0 |  |  |  | 0 |
| ArabianRandomLandGeneratorService |  | RandomLandGeneratorService | 0x81a584 | 0x81a5b4 | 0x43d530 |  |  |  | 0 |
| AudioService | sound banks / playback | Service | 0x868ae0 | 0x8684a0 | 0x606eb0 | 0x606880 | `App.Pause`, `App.Resume`, `Comment.SuddenDeath`, `EFMV.Play`, `EFMV.Terminated`, `Timer.StartGame` |  | 2 |
| AutoRepeatService |  | Service | 0x8a7148 | - | 0x74bcc9 | 0x74bb2c | `FE.ControllerLost`, `Input.Menu.Down`, `Input.Menu.Down.Release`, `Input.Menu.Left`, `Input.Menu.Left.Release`, `Input.Menu.Right`, `Input.Menu.Right.Release`, `Input.Menu.Up`, `Input.Menu.Up.Release` | `Input.Menu.Down`, `Input.Menu.Left`, `Input.Menu.Right`, `Input.Menu.Up` | 9 |
| CamelotRandomLandGeneratorService |  | RandomLandGeneratorService | 0x81a858 | 0x81a7c8 | 0x43e810 |  |  |  | 0 |
| CameraManagerService | logical cameras (see camera-w4m.md) | Service | 0x85482c | 0x8543f8 | 0x522710 | 0x5210b0 | `Airstrike.UpdateInfo`, `Camera.BackPressed`, `Camera.BackReleased`, `Camera.Cancel`, `Camera.Disable`, `Camera.Enable`, `Camera.FollowSceneCam`, `Camera.ForwardPressed`, `Camera.ForwardReleased`, `Camera.LeftPressed`, `Camera.LeftReleased`, `Camera.LocalPressed`, `Camera.LocalReleased`, `Camera.MouseMiddlePressed` (+72) | `Camera.HasUpdated`, `Camera.LogicalModeChanged`, `Input.DisableGroup`, `PiP.GoFullScreen`, `PiP.Hide`, `PiP.SlideOff`, `PiP.SlideOn` | 81 |
| CharlesStartupService |  | Service | 0x853bc4 | 0x853ba4 | 0x5135f0 | 0x513240 | `DEBUG.CHANGERENDERING` |  | 0 |
| ChatService | chat | Service | 0x856404 | 0x8563b0 | 0x538260 | 0x534980 | `Chat.Action`, `Chat.Clear`, `Chat.CloseConsole`, `Chat.Message`, `Chat.SelectPlayer`, `Chat.Send`, `Chat.SendSelectedPlayer`, `Chat.ToggleConsole`, `ChatPopup.MutePlayer`, `ChatPopup.SendPrivateMessage`, `Input.MouseMoved`, `Net.Close` | `Chat.HideIndicator`, `Chat.ShowIndicator`, `FE.CreateMouse`, `FE.DeleteMouse`, `FE.PopulateTable`, `FE.ShowError`, `HUD.HideTop`, `HUD.Show`, `WXMsg.CreatePopUp`, `WXMsg.KillPopUpNamed` | 11 |
| CollisionManagerService | collision world | Service | 0x854060 | 0x854008 | 0x5188b0 |  |  |  | 0 |
| CommentService | commentary text | Entity | 0x864220 | 0x8641ec | 0x5e4ca0 | 0x5de730 | `Comment.BaseballBat`, `Comment.HealthCrateSpawn`, `Comment.MysteryCrateSpawn`, `Comment.NewWeaponAvail`, `Comment.Poison`, `Comment.Radiation`, `Comment.SuddenDeath`, `Comment.Taunt`, `Comment.UtilityCrateSpawn`, `Comment.WeaponCrateSpawn`, `Commentary.Clear`, `Commentary.EnableDefault`, `Commentary.NoDefault`, `CommentaryPanel.CrateText` (+17) |  | 0 |
| ConstructionRandomLandGeneratorService |  | RandomLandGeneratorService | 0x81ad38 | 0x81ad88 | 0x443c40 |  |  |  | 0 |
| ControllerSelectScreenService |  | Service | 0x826ac0 | 0x826ae0 | 0x4b7390 | 0x4b68a0 | `CSS.HideCSS`, `CSS.ShowCSS` |  | 2 |
| CrcCheckService |  | Service | 0x86f8b4 | 0x86f898 | 0x6358f0 |  |  |  | 0 |
| DataTelnetService |  | Service | 0x833158 | 0x8331e0 | 0x4db9a0 |  |  |  | 0 |
| DebugService |  | Service | 0x83348c | 0x833474 | 0x4de060 | 0x4ddfc0 | `Debug.DrawDot`, `Debug.PlaySfx`, `GameLogic.Turn.Started` |  | 3 |
| DistributedRaycastGraphicService |  | Service | 0x81aeb8 | - | 0x4451f0 |  |  |  | 0 |
| EditorInterfaceService |  | Service | 0x826cb0 | 0x826c30 | 0x4b9360 | 0x4b7c10 | `EFMV.Terminated`, `Edit.AddFrameRef`, `Edit.AddVisRef`, `Edit.EnableLighting`, `Edit.ExportXomMesh`, `Edit.FlushLandscapeHandles`, `Edit.FreeFrame`, `Edit.GameCollision`, `Edit.GetDataBankName`, `Edit.GetHeightmapBounds`, `Edit.GetHeightmapPolyCount`, `Edit.GetRevisionString`, `Edit.Handshake`, `Edit.HideWater` (+11) | `Land.LoadMaterials` | 27 |
| FlowControlService | app flow (frontend <-> game), pause/resume, banks | Service | 0x834388 | 0x83414c | 0x4ef180 | 0x4e9e10 | `App.AchievementError`, `App.AchievementUpsell`, `App.AvatarAwardError`, `App.InviteUpsell`, `App.Pause`, `App.Resume`, `App.RetryMission`, `DEBUG.ChangeSSAA`, `DEBUG.ChangeShadowRes`, `DevFE`, `DevFE.Level`, `DevFE.Script`, `DevFE.Theme`, `FCS.LeaveWinMatchScreen` (+35) | `Chat.CloseConsole`, `FE.ChangeMenu`, `FE.CreateMouse`, `FE.DeleteMouse`, `FE.EnablePopups`, `FE.HighlightItem`, `FE.PlayPublisherMovie`, `FE.PlayTeam17Movie`, `FE.PlayUpsellMovie`, `FE.PopulateTable` (+26) | 35 |
| FrontEndService | menus | Service | 0x89f8c0 | 0x89f694 | 0x72a3e9 | 0x729ac9 | `FE.ChangeMenu`, `FE.ClearSwitch`, `FE.CreateMouse`, `FE.DeleteMouse`, `FE.DoMemCardWarning`, `FE.GameTypes`, `FE.GetBrief`, `FE.HideError`, `FE.KillCurrentMenu`, `FE.MoviePlayingComplete`, `FE.PCSave`, `FE.PopMenu`, `FE.PreviousMenu`, `FE.QueueError` (+31) | `FE.ChangeMenu`, `FE.HighlightItem`, `FE.ShowQueuedErrors`, `NF.GetAgainNow`, `WXMsg.CreatePopUp`, `WXMsg.KillPopUpNamed` | 41 |
| GameLogicService | turn/round/match rules, crates, sudden death, end of game | Service | 0x835f60 | 0x835cb0 | 0x4fdc90 | 0x4f7d80 | `Bubble.Created`, `Bubble.Deleted`, `Crate.Collected`, `Crate.Deleted`, `EFMV.Play`, `EFMV.Terminated`, `Earthquake.Start`, `Explosion.Construct`, `Game.BriefingDialogNowOn`, `Game.BriefingDialogOkPressed`, `GameLogic.AboutToWaterRise`, `GameLogic.ActivateSuddenDeath`, `GameLogic.AddInventory`, `GameLogic.AddMeToDeathQueue` (+68) | `AI.GameEnded`, `Comment.NewWeaponAvail`, `CrateSpy.UpdateTexts`, `FCS.QuitAttractMode`, `GameLogic.CreateCrate`, `GameLogic.EndTurn.Immediate`, `GameLogic.GotoFrontEnd`, `GameLogic.QuitGame`, `GameLogic.ResetCrateParameters`, `LB.WriteStats` (+12) | 79 |
| GraphicalSpawningService | spawns graphic entities | Service | 0x837584 | 0x837560 | 0x501440 | 0x500210 | `EFMV.Start`, `GSS.CreateDamageGraphic`, `LogicEntity.Created`, `Weapon.CreateAimingCursor`, `Weapon.CreateBinocularsCursor`, `Weapon.CreateBomberCursor`, `Weapon.CreateHomingCursor`, `Weapon.CreateNinjaCursor`, `Weapon.CreateSniperCursor`, `Weapon.CreateSuperBomberCursor`, `Weapon.CreateTargetingCursor`, `Weapon.CreateTeleportCursor` |  | 0 |
| GrmTelnetService |  | Service | 0x837ab4 | 0x837a98 | 0x5026f0 |  |  |  | 0 |
| HudService | in-game HUD | Service | 0x8669a0 | 0x8669c6 | 0x5f1c60 | 0x5f1a30 | `HUD.Hide`, `HUD.HideTop`, `HUD.Show`, `HUD.TogglePlayerNames`, `Particle.ReloadData` |  | 0 |
| InGameMenuBackgroundService |  | Service | 0x8270e4 | - | 0x4ba330 | 0x4b9e10 | `WXMsg.HideInGameMenuBackground`, `WXMsg.ShowInGameMenuBackground` |  | 2 |
| InputConversionService |  | Service | 0x8a5fa0 | 0x8a600c | 0x745224 |  |  |  | 0 |
| InputService | raw input | Service | 0x885164 | - | 0x691bd9 |  |  | `Input.RawKeyTyped` | 0 |
| InputTranslationService | raw input -> Input.* messages | Service | 0x8381a0 | 0x837e78 | 0x507b40 | 0x506210 | `GameLogic.PauseGame`, `GameLogic.ResumeGame`, `Input.DisableControllerCheck`, `Input.DisableGroup`, `Input.EnableControllerCheck`, `Input.EnableGroup`, `Input.RawKeyTyped` |  | 8 |
| JamesStartupService |  | Service | 0x853c8c | - | 0x514320 |  |  |  | 0 |
| LandscapeSharingService |  | Service | 0x8a404c | 0x8a4028 | 0x7447b5 | 0x743bff | `FE.CheckLevelName` | `WXMsg.CreatePopUp` | 1 |
| LeaderboardsService |  | Service | 0x86ebb8 | 0x86eb70 | 0x630070 | 0x62e740 | `LB.ClearListBox`, `LB.CycleFilter`, `LB.CycleTypeFilter`, `LB.NextLeaderboard`, `LB.PrevLeaderboard`, `LB.ProfileLoaded`, `LB.ReadStatJustForPlayer`, `LB.ReadStats`, `LB.ReadStatsForOnlinePlayers`, `LB.ShowGamercard`, `LB.WriteStats`, `PM.StatsReadFailed`, `PM.StatsReadOk`, `PM.StatsWriteFailed` (+1) | `FE.ConsoleSave`, `FE.PopulateTable`, `RM.AllStatsWritten`, `WXMsg.CreatePopUp` | 15 |
| LightingService | lighting | Service | 0x81d08c | 0x81d044 | 0x47f250 | 0x47e510 | `LI.DisableScene`, `LI.EnableScene` | `Camera.HasUpdated` | 2 |
| LogicalWeaponManagerService | weapon inventory/selection, spawns weapon logic entities | Service | 0x85a53c | 0x85a4c4 | 0x566b80 | 0x565b20 | `GameLogic.Turn.Started`, `Jetpack.Dying`, `NinjaRope.Dying`, `Parachute.Dying`, `Utility.Delete`, `Weapon.Create`, `Weapon.PreSelected` | `Input.DisableGroup`, `Input.EnableGroup`, `Weapon.ActivateAccessory`, `Weapon.PreSelected`, `Weapon.ResetAimingCursor`, `Weapon.Selected` | 8 |
| MartinStartupService |  | Service | 0x853cf4 | - | 0x514320 |  |  |  | 0 |
| MessageRegistrationService | message name -> id registry | Service | 0x884ea0 | - | 0x690abd |  |  |  | 0 |
| MissionService | mission/campaign objectives | Service | 0x8a0c7c | 0x8a07fc | 0x734065 | 0x733e45 | `FE.NextMission`, `FE.Prepare`, `FE.PreviousMission`, `FE.Wormpot`, `PM.FullGameUnlocked`, `RefreshDLCLocks`, `WXFE.ShowCurLevel`, `WXFE.ShowCurW3DLevel`, `WXFE.ShowNextLevel`, `WXFE.ShowPrevLevel`, `WXMsg.Challenge`, `WXMsg.EasterEggFound`, `WXMsg.Tutorial`, `WXMsg.w3dChallenge` | `Chat.Clear`, `FE.SetLevel`, `WXMsg.SetUpGame` | 14 |
| MoviePlayerService |  | Service | 0x86941c | 0x869380 | 0x60a330 | 0x608830 | `FE.PlayLegalScreenMovie`, `FE.PlayPublisherMovie`, `FE.PlayTeam17Movie`, `FE.PlayUpsellMovie`, `Input.Menu.Cancel`, `Input.QuitMovie.Pressed`, `Input.QuitMovie.Released`, `WXMsg.PlayCreditsMovie`, `WXMsg.PlayStoryMovie`, `WXMsg.StopCreditsMovie` | `FE.MoviePlayingComplete` | 10 |
| NetNameStorageService |  | Service | 0x89d3f4 | 0x89d3d4 | 0x70ccb6 | 0x70c2b5 | `WXMsg.ReqPlayerDetails`, `WXMsg.ReqPlayerDetailsByIndex` |  | 2 |
| NetService | network | Service | 0x89b3ec | - | 0x70c21f |  |  |  | 0 |
| NetworkDebugService |  | Service | 0x8565d4 | 0x8565b4 | 0x539600 |  |  |  | 0 |
| NewLoadingScreenService | loading screen | Service | 0x838380 | 0x83835c | 0x50a3f0 |  |  |  | 0 |
| NewsFeedService |  | Service | 0x8272f0 | 0x8272c4 | 0x4bba70 | 0x4bb8e0 | `NF.DisplayString`, `NF.GetAgainNow`, `NF.RevertToFeed`, `PM.NewsFeedReadFailed`, `PM.NewsFeedReadOk` |  | 5 |
| ParticleHandlerService | particles | Service | 0x8612dc | 0x8611d8 | 0x5c1530 | 0x5c05c0 | `GameLogic.GameLoadComplete`, `Particle.NewEmitter`, `Particle.NewUserIdEmitter`, `Particle.ReloadData`, `Particle.SetOverrides`, `Particle.WeatherTimer` | `Particle.DelGraphicalEmitter`, `Particle.ResumeGraphicalEmitter`, `Particle.SspndGraphicalEmitter` | 6 |
| PaulStartupService |  | Service | 0x853d58 | - | 0x514320 |  |  |  | 0 |
| PiPService | picture-in-picture view | Service | 0x870520 | 0x870490 | 0x6369b0 | 0x636ab0 | `PiP.GoFullScreen`, `PiP.Hide`, `PiP.SlideOff`, `PiP.SlideOn`, `PiP.TestToggle` | `Camera.Path.Reached.Knot`, `PiP.GoneFullScreen`, `PiP.GoneOffScreen` | 0 |
| PlatformManagerService |  | Service | 0x86cb4c | 0x86cac4 | 0x621cf0 | 0x6218c0 | `PM.CanAccess.Leaderboards`, `PM.CanAccess.OnlineMulti`, `PM.DLCErrorDismissed`, `PM.HandleAccess.Leaderboards`, `PM.HandleAccess.OnlineMulti`, `PM.HasAccess.Leaderboards`, `PM.HasAccess.OnlineMulti`, `PM.InviteCodaAccepted`, `PM.InviteCodaClosed`, `PM.InviteFriends`, `PM.InviteTeamSelected`, `PM.PurchaseGame`, `PM.SetPresence`, `PM.SetPresence.AllIdle` (+8) | `App.Resume`, `FE.PopulateTable`, `GameLogic.QuitGame`, `Net.Close`, `PM.FullGameUnlocked`, `PM.InviteCodaSpawn`, `RefreshDLCLocks`, `WXMsg.CreatePopUp`, `WXMsg.KillPopUpNamed`, `xo.msgReqInviteMatch` | 24 |
| PopUpService |  | Service | 0x89f61c | 0x89f5a4 | 0x7259ad | 0x724f71 | `PM.FullGameUnlocked`, `WXMsg.CreatePopUp`, `WXMsg.CreatePopUpCheckTrial`, `WXMsg.KillAllPopUp`, `WXMsg.KillPopUp`, `WXMsg.KillPopUpNamed` | `WXMsg.HideMenu`, `WXMsg.KillMenuNamed`, `WXMsg.ShowMenu` | 6 |
| PrehistoricRandomLandGeneratorService |  | RandomLandGeneratorService | 0x81d450 | 0x81d4a4 | 0x480520 |  |  |  | 0 |
| RandomLandGeneratorService | random land gen (per theme subclasses) | Service | 0x820224 | 0x820188 | 0x484c80 |  |  |  | 0 |
| RankedMatchPreStatService |  | Service | 0x86f3f0 | - | 0x6336d0 | 0x630e00 | `PM.RM_StatsReadFailed`, `PM.RM_StatsReadOk` | `LB.ReadStatJustForPlayer` | 0 |
| RankedMatchStatService |  | Service | 0x86edb8 | 0x86ed94 | 0x633b20 | 0x631a00 | `GameLogic.ApplyDamage`, `GameLogic.Draw`, `GameLogic.DrawImmediately`, `GameLogic.QuitGame`, `GameLogic.StartGame`, `GameLogic.Turn.Started`, `GameLogic.Win`, `Net.AllRankedPlayersHaveRead`, `PM.RM_StatsReadFailed`, `PM.RM_StatsReadOk`, `RM.AllStatsWritten`, `Ranked.IncrementKills` | `FE.ChangeMenu`, `LB.ReadStatsForOnlinePlayers`, `LB.WriteStats`, `RankedMatchPlayerScoreDone` | 12 |
| ReplayMessageStoreService | records messages for replay | Service | 0x8569ac | 0x856988 | 0x5426f0 |  |  |  | 0 |
| RumbleService |  | Service | 0x8274ec | 0x82749c | 0x4bc660 | 0x4bc550 | `Explosion` |  | 1 |
| SamStartupService |  | Service | 0x853dbc | - | 0x514020 | 0x513e20 | `Trigger.Net.Host`, `Trigger.Net.Join` |  | 0 |
| SavingIconService |  | Service | 0x851d8c | 0x851d70 | 0x50e2e0 | 0x50dae0 | `SIS.HideSavingIcon`, `SIS.ShowSavingIcon`, `SIS.ShowSavingIconDescription` |  | 3 |
| SchemeControlService | game scheme options | Service | 0x8a7364 | 0x8a72a4 | 0x753be1 | 0x74c238 | `FE.CheckSchemeName`, `FE.CompareScheme`, `FE.CreateScheme`, `FE.DeleteScheme`, `FE.LoadScheme`, `FE.RevertScheme`, `FE.RoomForScheme`, `FE.SaveScheme`, `FE.SaveToFile`, `FE.UpdateScheme`, `WXMsg.Scheme` | `FE.ConsoleQuickSave` | 11 |
| Service |  | BaseTask | 0x884b7c | - | 0x68baad |  |  |  | 0 |
| StatsCollectorService | stats | Service | 0x86a7a4 | 0x86a780 | 0x611320 | 0x60fd10 | `GameLogic.Turn.Started`, `Stats.Compile`, `Stats.Delete`, `Stats.DevShow`, `Stats.Populate.Match`, `Stats.Populate.Round`, `Stats.PresentAwards`, `Stats.ResetMatch`, `Stats.ResetRound`, `Stats.Update` |  | 10 |
| StatueDefendLandscapeGeneratorService |  | RandomLandGeneratorService | 0x8206b4 | 0x8207d0 | 0x487c00 |  |  |  | 0 |
| SteamVoiceService |  | Service | 0x8696fc | 0x8696e0 | 0x60add0 | 0x60a680 | `App.FocusGained`, `GameLogic.GameLoadComplete`, `Input.PushToTalkPressed`, `Input.PushToTalkReleased` |  | 0 |
| SteveStartupService |  | Service | 0x853e78 | - | 0x514320 |  |  |  | 0 |
| TablePopulationService |  | Service | 0x82bb20 | 0x828fbc | 0x4d1b40 | 0x4cf500 | `FE.PopulateTable` | `WXMsg.HighlightListItem` | 1 |
| TeamControlService | teams | Service | 0x8a19b4 | 0x8a189c | 0x74359f | 0x7344d6 | `FE.AddLTeamToGame`, `FE.AddTeam`, `FE.CheckGameStart`, `FE.CheckTeamName`, `FE.CompareTeam`, `FE.CreateTeam`, `FE.DeleteLandscape`, `FE.DeleteTeam`, `FE.DoneSomething`, `FE.GenRandomName`, `FE.LoadSpeechBank`, `FE.LoadTeam`, `FE.PlayRandomSpeech`, `FE.RemoveAllTeamsFromGame` (+18) | `GS.RemoveSelectedTeam`, `GS.UpdateTeam`, `WXMsg.BuildFactoryMesh` | 32 |
| TelnetService |  | Service | 0x886274 | - | 0x699f4e |  |  |  | 0 |
| TimerService | engine timers | Service | 0x886414 | - | 0x68baad |  |  |  | 0 |
| TransitionService | screen transitions | Service | 0x8521ec | 0x8521d0 | 0x510940 | 0x510350 | `TransitionFadeDown`, `TransitionKill` | `TransitionComplete` | 0 |
| WXFE_DataSetupService |  | Service | 0x8a626c | 0x8a60f4 | 0x74b785 | 0x747c19 | `WXMsg.ControllerSetup`, `WXMsg.Request`, `WXMsg.SetupData` | `FE.ConsoleQuickSave`, `WXMsg.ClearBanks`, `WXMsg.WormControl` | 3 |
| WXSceneManagerService |  | Service | 0x869c6c | 0x869c0c | 0x60e970 |  |  |  | 1 |
| WXWormManagerService | owns worms, spawn/unspawn, active worm | Service | 0x85ff7c | 0x85fe58 | 0x5b5e70 | 0x5b37c0 | `Binocular.EnterBinocularsVision`, `Binocular.LeaveBinocularsVision`, `GameLogic.ActivateNextWorm`, `GameLogic.ApplyDamage`, `GameLogic.EndTurn`, `Input.AimMouse`, `Input.AimMoveX`, `Input.JumpBackPressed`, `Input.JumpBackReleased`, `Input.MoveX`, `Input.MoveY`, `Input.TauntPressed`, `Land.NewShape`, `Team.Surrender` (+11) | `GameLogic.DecrementWeaponDelays`, `Worm.CleanUpOnDeactivate`, `WormSelect.WormSelected` | 18 |
| WXomOnlinePlugInService |  | Service | 0x86e15c | 0x86dd5c | 0x626920 | 0x6294c0 | `FE.ChangeMenu`, `GameLogic.Draw`, `GameLogic.Win`, `Input.StartButton`, `Net.PostWinMatchScreen`, `Net.RankedMatchSetup`, `RankedMatchPlayerScoreDone`, `WXNET.CheckPlayers`, `WXNET.CheckScheme`, `WXNET.CheckWormpot`, `Xbox.msgCustomUserMenu`, `Xbox.msgFCB` | `App.Pause`, `App.Resume`, `FE.ConsoleSave`, `FE.PopulateTable`, `FE.Prepare`, `HUD.Hide`, `HUD.Show`, `Net.Close`, `WXMsg.CreatePopUp`, `WXMsg.HideInGameMenuBackground` (+10) | 18 |
| WildWestRandomLandGeneratorService |  | RandomLandGeneratorService | 0x820cb4 | 0x820d28 | 0x48d140 |  |  |  | 0 |
| WormScenePlayerService |  | Service | 0x8698b8 | 0x8698d8 | 0x60b8e0 |  |  |  | 0 |
| WormpotService | wormpot modifiers | Service | 0x863288 | 0x86326c | 0x5d7550 | 0x5d6160 | `GameLogic.Turn.Ended`, `GameLogic.Turn.Started`, `Wormpot.SetupModes` | `Tweaks.Updated` | 3 |
| XMessageRelayService | message relay (net/replay) | Service | 0x8862ec | - | 0x69a3b5 |  |  |  | 0 |
| XScriptService | Lua scripting | Service | 0x885574 | - | 0x6953e9 |  |  |  | 0 |
| XomOnlinePlugInService |  | ? | 0x86dddc | 0x8b8ab4 | 0x6fe990 |  |  |  | 0 |
| ZCamUpdateFudgeService |  | LogicEntity | 0x8561c0 | - | 0x514620 | 0x534040 | `Chat.HideIndicator`, `Chat.ShowIndicator` |  | 0 |

### Gameplay logic entities

| Class | Role | Base | vtable | .cpp str | HandleMessage (slot 7) | subscribe fn(s) | listens | sends | #handled |
|---|---|---|---|---|---|---|---|---|---|
| ColliderLogicEntity |  | LogicEntity | 0x853f84 | 0x853fa8 | 0x515040 | 0x514c30 | `Land.NewShape` | `Land.GetNormal` | 1 |
| CrateLogicEntity | one crate: parachute, collect, destroy, sink | LogicEntity | 0x8619a0 | 0x861780 | 0x5cb330 | 0x5c9bd0 | `Crate.Delete`, `Crate.LooseChute`, `Crate.RadarDisplay`, `Crate.RadarHide`, `CrateSpy.UpdateTexts`, `Earthquake.Impulse`, `Explosion`, `GameLogic.Turn.Ended`, `Land.NewShape`, `MysteryTeleport.End`, `NinjaRope.EndSwing` | `Armour.Collected`, `CommentaryPanel.DebugText`, `Crate.Collected`, `Crate.Destroyed`, `Crate.Sunk`, `CrateSpy.Collected`, `GameLogic.ApplyDamage`, `GameLogic.CreateRandomMine`, `GameLogic.DoubleTurnTime`, `Jetpack.Kill` (+9) | 11 |
| DirtBallLogicEntity |  | LogicEntity | 0x86208c | 0x8620ac | 0x5ce550 |  |  |  | 0 |
| EFMVMovieLogicEntity |  | LogicEntity | 0x854c30 | 0x854bfc | 0x5272f0 | 0x526f70 | `Camera.Path.Stopped`, `Camera.TimedPath.Stopped`, `Edit.StopEFMV`, `Game.BriefingDialogNowOn`, `Game.BriefingDialogOkPressed`, `Input.QuitEFMV` | `Camera.Path.Start`, `Camera.TimedPath.Start`, `CommentaryPanel.TimedText`, `EFMV.Terminated`, `Input.DisableGroup`, `Input.EnableGroup`, `PiP.SlideOff`, `WXWormManager.UnspawnWorm`, `Weapon.Selected`, `Worm.Respawn` (+2) | 6 |
| GameOverLogicEntity | end-of-game sequence | LogicEntity | 0x83734c | 0x837408 | 0x500090 | 0x4ffbd0 | `Input.SomeInputFrom` | `EFMV.Start`, `FE.DeleteMouse`, `GameLogic.GotoFrontEnd`, `PiP.SlideOff`, `WXMsg.AnimDivide`, `WXMsg.HideInGameMenuBackground`, `WXMsg.KillAllPopUp`, `WXMsg.KillMenuNamed` | 1 |
| GraveStoneLogicEntity | gravestone after death | ColliderLogicEntity | 0x862248 | 0x862208 | 0x5cedf0 |  |  |  | 0 |
| HeightmapLogicEntity | heightmap land | Entity | 0x81babc | 0x81ba9c | 0x464330 | 0x463360 | `Heightmap.Import` |  | 0 |
| InitWorldLogicEntity | world setup | LogicEntity | 0x81bc54 | - | 0x464c00 |  |  | `AI.PopulatePathingNodes`, `Land.Import`, `Land.LoadMaterials` | 0 |
| InitWormsLogicEntity | places worms at start | LogicEntity | 0x85e0a4 | 0x85e0d4 | 0x59b1c0 |  |  |  | 0 |
| LandscapeLogicEntity | destructible land | LogicEntity | 0x81c810 | 0x81c7f0 | 0x478780 | 0x475c90 | `Detail.PlayAnim`, `Explosion`, `Land.CheckVoxel`, `Land.Clear`, `Land.ClearCoded`, `Land.ClearVoxel`, `Land.DisablePointLight`, `Land.EnablePointLight`, `Land.GetDetailInfo`, `Land.GetLandRemaining`, `Land.GetNormal`, `Land.Import`, `Land.SetPointLightColor`, `Land.SpawnPiece` (+2) | `Heightmap.Import`, `Land.LoadCompleted` | 0 |
| LogicEntity |  | BaseTask | 0x8850b0 | - | 0x68b79e |  |  |  | 0 |
| MainServiceStartupLogicEntity | boots services, verifies message IDs (0x508d80) | LogicEntity | 0x838284 | 0x83825c | 0x509460 | 0x508d80 | `Net.ConnectToServer`, `Net.CreateServer`, `Net.ShutdownComplete` |  | 3 |
| MatchStatsSummaryLogicEntity |  | StatsSummaryLogicEntity | 0x86b814 | - | 0x619a50 |  |  |  | 0 |
| MineFactoryLogicEntity | mines | LogicEntity | 0x86245c | 0x8623dc | 0x5d0510 |  |  | `MineFactory.Fire`, `MineFactory.FireEnd`, `MineFactory.Start` | 2 |
| OilDrumLogicEntity | oil drum | LogicEntity | 0x8627ec | 0x862778 | 0x5d2520 | 0x5d2330 | `Earthquake.Impulse`, `Explosion`, `Land.NewShape`, `NinjaRope.EndSwing` | `NinjaRope.Kill` | 5 |
| ParticleEmitterLogicEntity |  | LogicEntity | 0x860fc4 | 0x860f9c | None | 0x5be2e0 | `GameLogic.Turn.Started`, `Particle.DelLogicalEmitter`, `Particle.DelLogicalEmitterImm` | `Camera.ShakeStart`, `Worm.Antidote`, `Worm.ApplyLightside`, `Worm.Poison` | 4 |
| RoundStatsSummaryLogicEntity |  | StatsSummaryLogicEntity | 0x86b9fc | - | 0x619a50 |  |  |  | 0 |
| SpectatorMasterLogicEntity | spectator | LogicEntity | 0x851e28 | - | 0x50e500 | 0x50e3d0 | `GameLogic.Turn.Started`, `Net.DisableAllInput` |  | 2 |
| StatsSummaryLogicEntity |  | LogicEntity | 0x86bb64 | - | 0x619a50 |  |  |  | 0 |
| TelepadLogicEntity | telepad | LogicEntity | 0x862a3c | 0x86296c | 0x5d4010 | 0x5d3ba0 | `Explosion`, `GameLogic.Turn.Ended` | `Jetpack.Kill`, `NinjaRope.Kill`, `Parachute.Kill`, `Worm.OverridePhysics` | 3 |
| TestStartupParentLogicEntity |  | LogicEntity | 0x853f0c | 0x853ee4 | 0x514620 |  |  |  | 0 |
| TimerLogicEntity | turn/retreat/hot-seat/round timers | LogicEntity | 0x851fc4 | 0x851f68 | 0x50f980 | 0x50f4e0 | `GameLogic.DoubleTurnTime`, `GameLogic.PauseGame`, `GameLogic.ResumeGame`, `GameLogic.RoundTime.Pause`, `GameLogic.RoundTime.Resume`, `GameLogic.Turn.Started`, `GameLogic.TurnTime.Pause`, `GameLogic.TurnTime.Resume`, `Input.SomeInputFrom`, `Timer.EndGame`, `Timer.EndHotSeatTimer`, `Timer.EndRetreatTimer`, `Timer.EndTurn`, `Timer.StartHotSeatTimer` (+3) | `Timer.GameTimedOut`, `Timer.HotSeatTimedOut`, `Timer.PostActivityTimedOut`, `Timer.RetreatTimedOut`, `Timer.TurnTimedOut` | 0 |
| TriggerLogicEntity | scripted trigger zone | LogicEntity | 0x862c78 | 0x862c44 | 0x5d5730 | 0x5d5360 | `Explosion`, `GameLogic.DestroyTrigger` | `CommentaryPanel.DebugText`, `Trigger.Damaged`, `Trigger.Destroyed` | 2 |
| WXWeaponHelpPanelLogicEntity |  | LogicEntity | 0x867e00 | 0x867dd8 | 0x5ff580 | 0x5feaf0 | `GameLogic.Turn.Started`, `WeaponHelpPanel.Close`, `WeaponHelpPanel.Open` |  | 4 |
| WXWeaponPanelLogicEntity |  | LogicEntity | 0x868070 | 0x86803c | 0x603b70 | 0x601070 | `App.Pause`, `App.Resume`, `Camera.LogicalModeChanged`, `FE.ChangeMenu`, `GameLogic.CreateBriefingBox`, `GameLogic.Turn.Started`, `Input.ClosePanelPressed`, `Input.Menu.Down`, `Input.Menu.Left`, `Input.Menu.Right`, `Input.Menu.Up`, `Input.MouseMoved`, `Input.OpenPanelPressed`, `Weapon.DelayedPanelChanged` (+2) | `App.Resume`, `FE.CreateMouse`, `FE.DeleteMouse`, `GameLogic.WeaponPanelClosed`, `GameLogic.WeaponPanelOpened`, `Input.DisableGroup`, `Input.EnableGroup`, `WXMsg.KillPopUpNamed`, `Weapon.PreSelected`, `WeaponHelpPanel.Close` (+1) | 17 |
| WXWormLogicEntity | one worm: movement, aim, damage, poison, death | LogicEntity | 0x85ecdc | 0x85ec9c | 0x5b07c0 | 0x5a5ae0 | `AI.IssueWormCommand`, `Armour.Collected`, `Explosion`, `Input.AutoAimLeft`, `Input.AutoAimRight`, `Input.FirstPersonPressed`, `Tweaks.Updated`, `Weapon.Delete`, `Weapon.Selected`, `Worm.Antidote`, `Worm.ApplyPoison`, `Worm.DamageComplete`, `Worm.KillNow`, `Worm.Poison` (+1) | `AI.MoveFailed`, `Camera.ResetPressed`, `Comment.Poison`, `GSS.CreateDamageGraphic`, `GameLogic.AITurn.Started`, `Input.FirePressed`, `Input.SomeInputFrom`, `Jetpack.Kill`, `WXWormManager.UnspawnWorm`, `Weapon.ActivateAccessory` (+8) | 26 |
| WormSelectLogicEntity | worm select at turn start | LogicEntity | 0x85df14 | 0x85def4 | 0x59a5f0 | 0x59a150 | `Input.JumpPressed`, `Input.MoveBackwardPressed`, `Input.MoveForwardPressed`, `Input.MoveLeftPressed`, `Input.MoveRightPressed`, `Input.MoveX`, `Input.MoveY`, `Input.WormSelectAcceptPressed` | `GameLogic.DecrementInventory.Id`, `Weapon.Delete`, `WormManager.SelectNextWorm` | 9 |

### Weapon logic entities

| Class | Role | Base | vtable | .cpp str | HandleMessage (slot 7) | subscribe fn(s) | listens | sends | #handled |
|---|---|---|---|---|---|---|---|---|---|
| AdjustableBounceWeaponLogicEntity |  | LogicEntity | 0x856fe8 | - | 0x5434e0 | 0x543230 | `Input.BouncePressed` | `Weapon.FuseBounceChanged` | 1 |
| AdjustableFuseWeaponLogicEntity |  | LogicEntity | 0x857058 | - | 0x543ad0 | 0x5436f0 | `Input.Fuse1Pressed`, `Input.Fuse2Pressed`, `Input.Fuse3Pressed`, `Input.Fuse4Pressed`, `Input.Fuse5Pressed`, `Input.FusePressed` | `Weapon.FuseBounceChanged` | 6 |
| AdjustableHerdWeaponLogicEntity |  | LogicEntity | 0x8570c8 | - | 0x5441e0 | 0x5440c0 | `Input.FusePressed` | `Weapon.AimedLogicEnd`, `Weapon.FuseBounceChanged` | 1 |
| AimedWeaponLogicEntity |  | LogicEntity | 0x857274 | 0x857244 | 0x545800 | 0x545590 | `Input.AimDownPressed`, `Input.AimDownReleased`, `Input.AimMouse`, `Input.AimMoveY`, `Input.AimUpPressed`, `Input.AimUpReleased`, `Input.EnableGroup`, `Weapon.ResetAimingCursor` |  | 7 |
| AlienAbductionLauncherLogicEntity |  | LogicEntity | 0x857574 | - | 0x546b20 | 0x546740 | `Input.FirePressed` | `Airstrike.UpdateInfo`, `GameLogic.DecrementInventory`, `HUD.Target.Selected` | 1 |
| AlienAbductionLogicEntity |  | LogicEntity | 0x857680 | 0x8575e8 | 0x548ee0 |  |  | `Camera.StopFollowingSceneCam`, `EFMV.End`, `EFMV.Start`, `HeldAccessory.Hide`, `Worm.OverridePhysics` | 0 |
| ArmourLogicEntity |  | LogicEntity | 0x8579d0 | 0x857998 | 0x549380 | 0x548f60 | `GameLogic.EndTurn`, `GameLogic.Turn.Started` |  | 2 |
| BaseUtilityLogicEntity |  | Entity | 0x857a68 | 0x857a44 | 0x549790 | 0x549510 | `Input.FirePressed`, `Weapon.Delete` | `Weapon.LaunchPayload`, `Weapon.ResetAimingCursor` | 2 |
| BaseWeaponLogicEntity | weapon base | LogicEntity | 0x857afc | 0x857b5c | 0x54a430 | 0x549e30 | `Input.FirePressed`, `Weapon.Delete`, `Weapon.PostLaunchDelay` | `GameLogic.DecrementInventory`, `Timer.EndTurn`, `Timer.StartRetreatTimer`, `Weapon.Delete`, `Weapon.DisableWeaponChange`, `Weapon.Fired`, `Weapon.LaunchPayload`, `Weapon.ResetAimingCursor` | 3 |
| BinocularsUtilityLogicEntity |  | LogicEntity | 0x857d98 | 0x857db8 | 0x54bfe0 | 0x54b0b0 | `Input.FirePressed`, `Input.FirstPersonReleased`, `Weapon.Delete`, `Weapon.ResetAimingCursor` | `Binocular.EnterBinocularsVision`, `Binocular.InvalidTarget`, `Binocular.LeaveBinocularsVision`, `Binocular.StartCalculating`, `Binocular.StopCalculating`, `Binocular.ValidTarget` | 4 |
| BomberLogicEntity |  | LogicEntity | 0x858240 | 0x85819c | 0x54e1a0 | 0x54d720 | `Bomber.AnimsComplete`, `Bomber.DropBomb` | `Camera.StopFollowingSceneCam` | 2 |
| BubbleTroubleLogicEntity |  | LogicEntity | 0x858554 | 0x8584dc | 0x54fb80 | 0x54f6e0 | `Damage.Impulse`, `Explosion`, `GameLogic.Turn.Ended`, `Weapon.Delete` |  | 4 |
| BubbleTroubleUtilityLogicEntity |  | LogicEntity | 0x858698 | - | 0x550a80 | 0x54fdb0 | `Input.FirePressed`, `Weapon.Delete`, `Weapon.LaunchPayload.Callback` | `GameLogic.DecrementInventory`, `Payload.Launched`, `Weapon.Delete`, `Weapon.PlayFireAnim` | 3 |
| ClusterGeneratorLogicEntity |  | LogicEntity | 0x8588f0 | 0x8588c8 | 0x551950 |  |  |  | 0 |
| DonkeyLogicEntity |  | PayloadLogicEntity | 0x858b6c | 0x858be8 | 0x553cc0 |  |  | `Camera.ShakeEnd`, `Camera.ShakeStart` | 0 |
| FatkinsStrikePayloadLogicEntity |  | ParabolicPayloadLogicEntity | 0x858f1c | 0x85901c | 0x554cd0 |  |  | `Camera.ShakeStart`, `Payload.LogicallyArmed` | 0 |
| FloodLogicEntity |  | LogicEntity | 0x859090 | 0x8590bc | 0x5559f0 | 0x555580 | `Flood.CutCamera`, `Flood.DoFlood`, `Flood.StartRain`, `Flood.StopRain` |  | 4 |
| FloodWeaponLogicEntity |  | BaseWeaponLogicEntity | 0x859164 | 0x8591a4 | 0x556010 |  |  | `Weapon.PlayFireAnim` | 0 |
| FlyingPayloadLogicEntity |  | PayloadLogicEntity | 0x859424 | 0x8593ec | 0x558250 | 0x5574c0 | `Fly.Pitch.Down.Pressed`, `Fly.Pitch.Down.Released`, `Fly.Pitch.Up.Pressed`, `Fly.Pitch.Up.Released`, `Fly.Roll.Left.Pressed`, `Fly.Roll.Left.Released`, `Fly.Roll.Right.Pressed`, `Fly.Roll.Right.Released`, `Fly.Yaw.Left.Pressed`, `Fly.Yaw.Left.Released`, `Fly.Yaw.Right.Pressed`, `Fly.Yaw.Right.Released` | `CommentaryPanel.ScriptText`, `Input.DisableGroup`, `Input.EnableGroup`, `Payload.AdjustExpiry` | 12 |
| GirderKitLogicEntity |  | BaseWeaponLogicEntity | 0x8596ac | 0x859666 | 0x55bac0 | 0x55b460 | `Girder.Cancel`, `Girder.Lower.Pressed`, `Girder.Lower.Released`, `Girder.Move.Backward.Pressed`, `Girder.Move.Backward.Released`, `Girder.Move.Forward.Pressed`, `Girder.Move.Forward.Released`, `Girder.Move.Left.Pressed`, `Girder.Move.Left.Released`, `Girder.Move.Right.Pressed`, `Girder.Move.Right.Released`, `Girder.Raise.Pressed`, `Girder.Raise.Released`, `Input.ClosePanelPressed` (+1) | `Weapon.Delete`, `Weapon.DisableWeaponChange`, `Weapon.EnableWeaponChange`, `Weapon.LaunchPayload` | 16 |
| GunWeaponLogicEntity |  | LogicEntity | 0x8599fc | 0x859a1c | 0x55db30 | 0x55cff0 | `Input.FirePressed`, `Weapon.Delete`, `Weapon.Fired.Start`, `Worm.CollisionNotification` | `GameLogic.AboutToApplyDamage`, `GameLogic.ApplyDamage`, `GameLogic.DecrementInventory`, `GameLogic.GunWaiting`, `Weapon.DisableWeaponChange`, `Weapon.EnableWeaponChange`, `Weapon.EndFireAnim`, `Weapon.GetLaunchPosition`, `Weapon.ResetAimingCursor`, `Weapon.StartFireAnim` (+2) | 2 |
| HomingPayloadLogicEntity |  | PayloadLogicEntity | 0x859e7c | 0x859f10 | 0x5616d0 |  |  | `Weapon.AimedLogicEnd`, `Weapon.AimedLogicStart` | 0 |
| JetpackUtilityLogicEntity | jetpack | LogicEntity | 0x85a07c | - | 0x563f00 | 0x563440 | `Camera.LogicalModeChanged`, `GameLogic.EndTurn`, `GameLogic.Turn.Ended`, `Input.FirePressed`, `Input.FireUtilPressed`, `Input.FireUtilReleased`, `Input.Jetpack.ForwardPressed`, `Input.Jetpack.ForwardReleased`, `Input.Jetpack.LeftPressed`, `Input.Jetpack.LeftReleased`, `Input.Jetpack.RightPressed`, `Input.Jetpack.RightReleased`, `Jetpack.Kill`, `Jetpack.UpdateFuel` | `GameLogic.DecrementInventory.Id`, `HatAccessory.Hide`, `Input.DisableGroup`, `Input.EnableGroup`, `Jetpack.Dying`, `Jetpack.Kill`, `Jetpack.StopJet`, `PackAccessory.Hide`, `PackAccessory.Trigger`, `PackAccessory.Wield` (+2) | 13 |
| JumpingPayloadLogicEntity |  | ParabolicPayloadLogicEntity | 0x85a364 | 0x85a3e8 | 0x564680 |  |  | `Land.GetNormal` | 0 |
| LowGravityLogicEntity |  | LogicEntity | 0x85a708 | 0x85a728 | 0x5672f0 | 0x566ef0 | `Input.FirePressed`, `Weapon.Delete` | `Weapon.Delete` | 2 |
| MeleeWeaponLogicEntity |  | LogicEntity | 0x85a82c | 0x85a84c | 0x569930 | 0x568860 | `Input.FirePressed`, `Input.FireReleased` | `Firepunch.End`, `GameLogic.DecrementInventory`, `Weapon.DisableWeaponChange`, `Weapon.GetLaunchPosition`, `Weapon.LaunchPayload`, `Weapon.PlayFireAnim`, `Weapon.ResetAimingCursor`, `Weapon.Wield`, `Worm.OverridePhysics` | 3 |
| NewSentryGunLogicEntity |  | LogicEntity | 0x85acd4 | 0x85ad4c | 0x56dc30 | 0x56cec0 | `Explosion`, `GameLogic.Turn.Started`, `Land.NewShape` |  | 3 |
| NewSentrygunWeaponLogicEntity |  | BaseWeaponLogicEntity | 0x85ae6c | 0x85aeac | 0x56eb40 | 0x56e000 | `Weapon.LaunchPayload.Callback` | `GameLogic.DecrementInventory`, `Payload.Launched`, `Weapon.PlayFireAnim` | 1 |
| NinjaRopeUtilityLogicEntity | ninja rope | LogicEntity | 0x85b07c | 0x85b054 | 0x574730 | 0x570920 | `GameLogic.EndTurn`, `GameLogic.Turn.Ended`, `Input.FireUtilPressed`, `Input.FireUtilReleased`, `Land.NewShape`, `NinjaRope.Kill`, `NinjaRope.Lengthen.Off`, `NinjaRope.Lengthen.On`, `NinjaRope.Shorten.Off`, `NinjaRope.Shorten.On`, `NinjaRope.SwingBackward.Off`, `NinjaRope.SwingBackward.On`, `NinjaRope.SwingForward.Off`, `NinjaRope.SwingForward.On` (+2) | `3dAimer.Display`, `3dAimer.Hide`, `CommentaryPanel.CrateText`, `GameLogic.DecrementInventory.Id`, `Input.DisableGroup`, `Input.EnableGroup`, `NinjaRope.Fired`, `NinjaRope.Kill`, `NinjaRope.Retracted`, `Utility.Delete` (+5) | 16 |
| ParabolicPayloadLogicEntity |  | PayloadLogicEntity | 0x85b504 | 0x85b468 | 0x577f40 | 0x577530 | `Earthquake.Impulse`, `Explosion`, `Land.NewShape`, `Payload.Arm`, `Payload.CollideWithDisarmPlane`, `Payload.CollideWithExpiryPlane`, `Payload.CollideWithLand`, `Payload.CollideWithWater`, `Payload.Detonate`, `Payload.Disarm`, `Payload.Expire`, `Payload.TerminalVelocity`, `Worm.NewPosition` | `Land.GetNormal`, `Payload.AdjustExpiry`, `Payload.CollideWithLand` | 12 |
| ParachuteLogicEntity | parachute | LogicEntity | 0x85bc34 | 0x85bc98 | 0x579810 | 0x578370 | `GameLogic.EndTurn`, `GameLogic.Turn.Ended`, `Input.FirePressed`, `Input.FireUtilPressed`, `Input.FireUtilReleased`, `Input.TurnLeftPressed`, `Input.TurnLeftReleased`, `Input.TurnRightPressed`, `Input.TurnRightReleased`, `Parachute.AutoOpen`, `Parachute.Kill` | `GameLogic.DecrementInventory.Id`, `HatAccessory.Hide`, `PackAccessory.Hide`, `PackAccessory.Wield`, `Parachute.Dying`, `Worm.OverridePhysics` | 10 |
| ParachutePayloadLogicEntity |  | PayloadLogicEntity | 0x85be14 | 0x85bde8 | 0x57a8a0 | 0x57a4d0 | `Parachute.Kill` | `EFMV.End` | 1 |
| PayloadLogicEntity | projectile base: flight, fuse, explode, camera | LogicEntity | 0x85c194 | 0x85c0b8 | 0x582860 | 0x582200 | `NinjaRope.EndSwing`, `Payload.ForceExpiry` | `Input.FirePressed`, `NinjaRope.Kill`, `Particle.DelGraphicalEmitter`, `Payload.LogicallyArmed`, `Weapon.DeleteCursor` | 3 |
| PayloadWeaponLogicEntity |  | LogicEntity | 0x85c6bc | 0x85c698 | 0x586040 | 0x5837a0 | `Input.FirePressed`, `Weapon.Delete`, `Weapon.LaunchPayload`, `Weapon.LaunchPayload.Callback`, `Weapon.PostLaunchDelay` | `Airstrike.UpdateInfo`, `GameLogic.DecrementInventory`, `HUD.Target.Selected`, `Payload.Launched`, `Weapon.DeleteCursor`, `Weapon.EndFireAnim`, `Weapon.GetLaunchPosition`, `Weapon.PlayFireAnim`, `Weapon.ResetAimingCursor`, `Weapon.Wield` | 5 |
| PoisonArrowLogicEntity |  | ParabolicPayloadLogicEntity | 0x85c934 | 0x85c9bc | 0x586600 |  |  | `Payload.Impact` | 0 |
| PoweredWeaponLogicEntity |  | LogicEntity | 0x85ca78 | 0x85ca30 | 0x586e40 | 0x586bb0 | `Input.FirePressed`, `Input.FireReleased` | `Weapon.LaunchPayload`, `Weapon.PowerBarLogicEnd`, `Weapon.PoweringUpStart`, `Weapon.Wield` | 2 |
| RedbullUtilityLogicEntity |  | BaseWeaponLogicEntity | 0x85cb24 | 0x85cb64 | 0x587dc0 | 0x587c40 | `Input.JumpBackPressed`, `Input.JumpPressed` | `GameLogic.ApplyDamage`, `PackAccessory.Hide`, `PackAccessory.Trigger`, `PackAccessory.Wield`, `Weapon.DisableWeaponChange`, `Weapon.EnableWeaponChange`, `Weapon.PlayFireAnim`, `Worm.Antidote` | 2 |
| SkipgoUtilityLogicEntity |  | BaseWeaponLogicEntity | 0x85ccfc | - | 0x588160 |  |  | `Comment.Taunt`, `HeldAccessory.Hide`, `SkipGo.Launched`, `Worm.ResetAnim` | 0 |
| StarburstLogicEntity |  | FlyingPayloadLogicEntity | 0x85ce04 | 0x85cda0 | 0x5890e0 | 0x588c10 | `Timer.TurnTimedOut`, `Worm.Died`, `Worm.Drowning` | `PackAccessory.Hide`, `PackAccessory.Wield`, `Starburst.FuseLit`, `Timer.EndTurn`, `Worm.OverridePhysics` | 3 |
| SuperBomberLogicEntity |  | LogicEntity | 0x85d048 | 0x85cfa4 | 0x58b660 | 0x58a7f0 | `Bomber.AnimsComplete`, `Fly.Yaw.Left.Pressed`, `Fly.Yaw.Left.Released`, `Fly.Yaw.Right.Pressed`, `Fly.Yaw.Right.Released`, `Input.FirePressed` | `Camera.StopFollowingSceneCam`, `EFMV.End`, `EFMV.Start`, `Input.DisableGroup`, `Input.EnableGroup` | 6 |
| SurrenderLogicEntity |  | BaseWeaponLogicEntity | 0x85d1cc | 0x85d22c | 0x58bae0 |  |  | `Team.Surrender`, `Weapon.PlayFireAnim` | 0 |
| WalkingPayloadLogicEntity |  | PayloadLogicEntity | 0x85d6dc | 0x85d758 | 0x592ad0 | 0x591fe0 | `Input.JumpBackPressed`, `Input.JumpPressed`, `Input.MoveLeftPressed`, `Input.MoveLeftReleased`, `Input.MoveRightPressed`, `Input.MoveRightReleased`, `Input.MoveX` | `Accessory.Update`, `Payload.AdjustExpiry`, `Payload.ChangeToSecondMesh`, `Payload.PlayIntermediateAnim`, `Worm.OverridePhysics` | 11 |
| WeaponFactoryLogicEntity | weapon factory custom weapons | LogicEntity | 0x85dab0 | 0x85dae0 | 0x59a060 |  |  |  | 0 |

### Other .cpp units (assert strings, VA)

BitArray3D 0x81a6d8, CombineLandscapeAction 0x81ab5c, GenerateLandGeometry 0x81af9b, GLG_FringeBuilder 0x81b208, GLG_PC 0x81b27c, GLG_Shadow 0x81b5cf, HeightmapGraphicEntity 0x81b820, LandBlobBlastEntity 0x81bdf4, LandFramePseudoEntity 0x81c1fb, LandscapeGraphicEntity 0x81c630, LensFlareGraphicEntity 0x81cf54, MenuBackgroundLandscapeEntity 0x81d350, RainAudioEntity 0x81d5bc, RainGraphicEntity 0x81d67c, SkyBoxEntity 0x82038b, SnowParticleEmitterEntity 0x82056c, StripLand 0x820918, WaterCgGraphicEntity 0x8209e4, AIPathAction 0x820e7c, AIPathManager 0x82151c, AIPlan 0x8218d0, AIPlanAttack 0x822e5c, AIPlanDefines 0x824638, AIPlanLog 0x824868, AIPlanMemory 0x824890, AIPlanMove 0x824e80, AIPlanTarget 0x825188, AIPlanUtilities 0x82550c, AITurnAction 0x82622c, AITurnEntity 0x826890, MenuStack 0x82723c, DefaultPCInput 0x833664, DefaultXBoxInput 0x833edc, NodeTransformElement 0x8385c8, Rm 0x8519ac, DamageImpulseMessage 0x8540e8, ExplosionMessage 0x854168, TargetParabola 0x8541c4, WX_Collider 0x8541f4, camera 0x854283, CameraShakeManager 0x8549e8, CameraShakeObject 0x854a4c, DefaultCam 0x854ac0, FallCam 0x85517c, FlyCam 0x8551ec, GirderCam 0x855298, IsometricCam 0x855410, Knot 0x855828, KnotList 0x855844, NinjaCamMkIII 0x8559ff, OccludingCam 0x855a7d, OrbitCam 0x855c7f, SimpleCam 0x855d60, TrackCam 0x855e9c, ChatIndicatorEntity 0x856248, AimedWeaponGraphicEntity 0x857184, AlienAbductionGraphicEntity 0x857454, BinocularsCursorGraphicEntity 0x857cfc, BomberCursorGraphicEntity 0x857f60, BomberGraphicEntity 0x858134, BubbleTroubleGraphicEntity 0x858388, BuffaloOfLiesGraphicEntity 0x858810, CursorGraphicEntity 0x858a8c, EarthquakeEffectEntity 0x858d9c, FlyingPayloadGraphicEntity 0x8592a8, GirderKitGraphicEntity 0x8595c0, GunWeaponGraphicEntity 0x859958, GunWobbleObject 0x859ba7, HomingLockOnGraphicEntity 0x859da0, JetpacUtilityLogicEntity 0x85a09f, NewSentryGunGraphicEntity 0x85ac10, NinjaRopeUtilityGraphicalEntity 0x85af84, ParachutePayloadGraphicEntity 0x85bd48, PayloadGraphicEntity 0x85bf10, SuperBomberGraphicEntity 0x85ced0, TriggerGraphicEntity 0x85d2c0, WAE_Dropped 0x85d2de, WAE_Jetpack 0x85d35a, WAE_Mechanical 0x85d3b2, WAE_Melee 0x85d42e, WAE_Parachute 0x85d4e0, WAE_Standard 0x85d526, WAE_Starburst 0x85d61c, WeaponAccessoryEntity 0x85d840, WormPoseManager 0x85e1b0, WXVertexCollider 0x85e2ec, WXWormGraphicEntity 0x85e4d6, Particle 0x860774, ParticleEmitterBase 0x860844, ParticleEmitterEffectEntity 0x860a24, ParticleEmitterGraphicEntity 0x860dd0, ParticleLandCollider 0x861470, TrailGraphicEntity 0x861544, CrateGraphicEntity 0x861660, DirtBallGraphicEntity 0x862010, MineFactoryGraphicEntity 0x862340, OilDrumGraphicEntity 0x862684, TelepadGraphicEntity 0x8628c8, ActiveWormHudInfoEntity 0x8634b0, AngleMeterEntity 0x863a48, ArmourGraphicEntity 0x863b48, ArmouryBorderResource 0x863b8c, BaseHudObject 0x863c68, BlimpHelpEntity 0x863d90, ChooseWormArrowEntity 0x863ed8, CommentaryBoxGraphicEntity 0x863f68, CommentGraphicEntity 0x8640f0, CounterGraphicEntity 0x865a0c, DamageEntity 0x865ab8, EfmvBorderEntity 0x865b3e, EnergyBarEntity 0x865d0c, EnergyBarManagerEntity 0x865f98, HealthPreviewEntity 0x866134, HealthText3DEntity 0x866320, HotSeatTimeGraphicEntity 0x866488, HudActiveWormArrowEntity 0x8666f8, HudClockEntity 0x86678c, MeleeAccuracyBarGraphicEntity 0x866a28, MiniBackResource 0x866b64, NetworkIndicatorEntity 0x866c1c, PowerbarMeterEntity 0x866dac, ScannerHudEntity 0x866f68, SecondaryWeaponGraphicEntity 0x8671c0, SecondaryWeaponHelpEntity 0x867288, SubtitleGraphicEntity 0x867488, SuperBomberCursorGraphicEntity 0x867548, TargetingCursorGraphicEntity 0x867630, Text2DEntity 0x867720, Text3DEntity 0x8677d8, WeaponCursorGraphicEntity 0x867958, WindMeterEntity 0x8679fc, WormHealthNameEntity 0x867c14, WormSelectGraphicEntity 0x867d64, LoadingCallback 0x868b6c, WXActor 0x869924, CrateStatsSource 0x86aaac, JetpackStatsSource 0x86ab34, StatsSource 0x86ad20, WeaponStatsSource 0x86ae4c, WormStatsSource 0x86aed0, PersistStatsStorage 0x86b09c, ShotStatsStorage 0x86b5cc, StatsStorage 0x86b76c, MatchStatsSummary 0x86b920, RoundStatsSummary 0x86b9bc, StatsSummary 0x86bbe0, AccoladeRulesImpl 0x86bcd8, PCPostProcess 0x86c4e7, PCPlatformManagerImpl 0x86d388, TimedKnotList 0x8707b4, WX_LandContainersImpl 0x8734a8, WX_ContainersImpl 0x87453c, WeaponDelaysImpl 0x878afc, WX_WeaponContainersImpl 0x878f0c, AIParametersImpl 0x87a1fc, WXFE_ContainersImpl 0x87b478, WXGS_ContainersImpl 0x87e898, WX_StatsImpl 0x87eeb8, GameInitDataImpl 0x87f490, PlayerListImpl 0x87f4a8, WeaponInventoryImpl 0x87f4f0, WXFEContainersImpl 0x87f524, WXFE_UnlockableItemImpl 0x87f588, WXEFMV_ContainersImpl 0x880b78, WXNet_ContainersImpl 0x88345c, WXStats_Impl 0x883e30, NetMessagesImpl 0x883eec, ChatNetMessagesImpl 0x884a70, NetService_Xbox 0x89b21c, NetStream 0x89d534, WXFE_Border 0x89f444, WXFE_Border_ComPanel 0x89f49c, MouseEntity 0x8a5f28, WXFE_DemoSplashScreen 0x8a71e8, WXFE_LandscapeWrapperEntity 0x8a9264, WXFE_BaseGfxItem 0x8a9754, WXFE_WormEntity 0x8a9cbc, WXFE_BaseItem 0x8a9fe4, WXFE_ListControlEntity 0x8aa43f, WXFE_TextBoxEntity 0x8aa51c, WXFE_WeaponFactoryMeshEntity 0x8aa948, WXFE_Border_GfxPaper 0x8aaa7c, WXFE_Border_NamePlate 0x8aaacc, WXFE_Border_Nav 0x8aac50, WXFE_Border_Button 0x8aae10, WXFE_Border_Charcoal 0x8aae4c, WXFE_Border_Text 0x8aaedc, WXFE_Border_Armoury 0x8aaf3c, WXFE_Border_List 0x8aafa0, WXFE_HowToPlayTextBox 0x8ab2c8, WXFE_GalleryImageEntity 0x8ab378, WXFE_GalleryThumbEntity 0x8ab59c, WXFE_PlayerRepresentationObjectEntity 0x8ab724, WXFE_SoftwareKeyboardEntity 0x8ab86c, WXFE_MeshObjectParticleEntity 0x8abcd4, WXFE_GetControllerInputEntity 0x8abf00, WXFE_BriefingEntity 0x8ac074, WXFE_FlagObjectEntity 0x8ac194, WXFE_ShopControlEntity 0x8ac2f8, WXFE_WormpotControlEntity 0x8aca98, WXFE_LandscapeEntity 0x8ad0b4, WXFE_TrophyEntity 0x8ad1b8, WXFE_TrophyCabinetEntity 0x8ad454, WXFE_ImageViewEntity 0x8ad5e4, WXFE_DetailButtonEntity 0x8ad758, WXFE_MeshObjectEntity 0x8ad85c, WXFE_ListBoxElementsEntity 0x8ada38, WXFE_TextEditBoxEntity 0x8adc10, WXFE_Border_Bubble 0x8adcba, WXFE_BaseNavItem 0x8add58, WXFE_BaseMenu 0x8addf0, WXFE_Text 0x8ade3c, WXFE_ListItem 0x8adf7c, WXFE_Item_Meshs 0x8adff0, WXFE_Item 0x8ae008, WXFE_Item_Icon 0x8ae08c, WXFE_Item_PowerBar 0x8ae1b7, WXFE_Item_Tally 0x8ae26c, WXFE_Item_Padding 0x8ae2ac, XOglShaderManagerImpl 0x8b3c10, GameBrowserImpl 0x8b6070, WXSteamLobby 0x8b71cc, WXSteamLobbyBrowser 0x8b739c, MoviePlayerPC 0x8b75f0, XomOnlineUiMessages 0x8b8a74, XomOnlineValue 0x8b8b58, steam_XomOnlinePlayerImpl 0x8b9b60, XomOnlineServerImpl 0x8ba63c, XomOnlineClientImpl 0x8bada0, XomOnlineMachineImpl 0x8bb438, XomOnlinePlugInDriverImpl 0x8bbb28, steam_XomOnlineSessionImpl 0x8bd000, steam_XomOnlineGamerImpl 0x8bd664, steam_XomOnlineMatchImpl 0x8bd7ac, XomOnlineChannelImpl 0x8bdb84, steam_XomOnlineFinderImpl 0x8bdddc, XomOnlineKeyImpl 0x8be148, steam_XomOnlinePlayerSetImpl 0x8be2a4, steam_XomOnlineHostMigrationImpl 0x8be534, XomOnlineLogging 0x8be704, XomOnlineUiStrings 0x8bfb04

## 3. Data keys and the message system
### Data service = XOM `XDataResourceManager` (disasm + RTTI, high)
- RTTI: `XDataResourceManager` vtable 0x8887cc, iface `IXDataResourceManager` (IID bytes at 0x888288). Source path string 0x81b1c0 `...XOM\src\Xrm/XDataResourceManager.h`.
- Access: `0x639b1d()` -> XOM app/root object; `vtbl+0x54` = QueryInterface/GetService(IID 0x888288) -> manager. (disasm)
- Keys looked up **by plain string name**, no hash at call site: callers store `const char*` key into a stack slot and pass its address (`XString`-like 1-word wrapper) + out pointer. Lookup goes through `0x6a2d60` -> `XReferencedTrie<IXDataResourceDescriptor, GetResourceName>` (RTTI td 0x93e190) = string trie. (disasm, high)
- Every call returns HRESULT, checked by assert `(((HRESULT)(hRes)) >= 0)` (str 0x81b13c) with file/line.

#### Manager vtable (0x8887cc) slots (disasm)
| off | impl | meaning |
|---|---|---|
| +0x74 | 0x6a3ef0 | GetDescriptor(name, IXDataResourceDescriptor** out) |
| +0x78 | 0x6a3f70 | 4-arg (ret 0x14); likely Create/Add resource (assumed) |
| +0x7c | 0x6a43b0 | get Color (type 7) -> ptr to value |
| +0x80 | 0x6a4350 | get StringTable (6) |
| +0x84 | 0x6a42f0 | get Container (5) |
| +0x88 | 0x6a4290 | get String (4) -> ptr |
| +0x8c | 0x6a4220 | get Vector (3) -> ptr (copies 3 floats) |
| +0x90 | 0x6a41c0 | get Float (2) |
| +0x94 | 0x6a4160 | get Uint (1) |
| +0x98 | 0x6a4100 | get Int (0) |
| +0x9c | 0x6a4410 | ? (no type check) |

#### Thin global wrappers (cdecl, `(const XString* name, T* out)`), with caller counts (disasm)
- 0x50b760 GetDescriptor (571 callers) / 0x50b790 Int (313) / 0x50b7c0 Uint (316) / 0x50b7f0 Float (285) / 0x50b820 String (781) / 0x50b880 Vector (60) / 0x50b8b0 Container (116) / 0x50b8e0 Color (11) / 0x50b910 slot+0x9c (6).
- Typed *handle* getters (resolve descriptor once, assert type, keep XomPtr; used to cache in members, e.g. CameraManager stores handles at `[this+0x368]`): 0x50b940 Int(222) / 0x50ba00 Uint(354) / 0x50bac0 Float(125) / 0x50bb80 String(590) / 0x50bc40 Vector(32) / 0x50bd00 Color(10). Inlined duplicates: 0x465290 (Float), 0x47b4c0 (Vector).
- Type-mismatch assert string 0x8bcd98 `RequiredInterface::GetStaticResourceType() == pOriginalInstance->GetResourceType()`.

#### Value types (descriptor vtbl+0x10 GetResourceType) (disasm, high)
0 XIntResource, 1 XUintResource, 2 XFloatResource, 3 XVectorResource, 4 XStringResource, 5 XContainerResource, 6 XStringTableResource, 7 XColorResource. Value lives at `descriptor->details(+4) + 0x1c` (int/float direct, vector/string/color by address). Matching `X*ResourceDetails` classes (XResourceDetails : XContainer) are what the XOM files store.

#### Examples
- `Land.Center` str 0x81c84c: LandscapeLogicEntity.cpp 0x4721a7 (GetDescriptor), 0x4ffd8e (Vector by value, 0x50b880), CameraManagerService.cpp 0x51fdc8 (Vector handle 0x47b4c0 -> [ebp+0x238]).
- `Camera.Track.EventPosition` 0x854518: 0x51d46c Vector handle 0x50bc40 -> [esi+0x368]; 0x533609 (0x47b4c0).
- `Worm.Drown.HeightOffset` 0x8553d4: 0x529780, 0x5a992c Float handle 0x465290.

### Message system (disasm, medium-high)
- Message names are **static name objects** (8 bytes): `+0 u16 id` (0xffff = unresolved), `+2 u8 flags` (bit0 = resolved), `+4 const char* name`. Built at static init by ctor `0x68bb9f(name)` (3657 call sites, each followed by an atexit dtor push). Ex: `GameLogic.Turn.Ended` (0x82e2e8) has 3 instances (0x95aad0, 0x95d304, 0x95dc58) = one per TU.
- `0x68bbe6(this=nameObj, bool create)` (2488 sites): lazy name -> u16 id. Uses `0x690d44(name, &id)`: hash of string mod size (0x96d08c), bucket table 0x96d094 (registration service). Unknown + create=0 -> logs `Message not found : ` (0x884c54).
- Subscribe: `0x690e3e(listenerOwner, const u16* id, handlerSlot*)` (1158 sites). Pops a listener record from pool vector 0x96d0e4..0x96d0e8, stores id at rec+8, active flag rec+0x10. Typical init: `id = Name.Resolve(1); Subscribe(this, &id, &this->slotN)`.
- Dispatch test in handlers: `0x68bcce(u16 msgId, nameObj*)` -> bool (1224 sites).
- Send: alloc `0x691705(size)` with `ecx=[0x96d14c]` (pool; 1449 sites), write id at msg+4, then `0x6910e4(Message*)` (1198 sites) -> relay at global 0x96d090 (+0x14) -> `0x68b89e(msg, target, 0)`. Services: `MessageRegistrationService` (vt 0x884ea0), `XMessageRelayService` (vt 0x8862ec), `ReplayMessageStoreService`.
- Payload classes (RTTI, base `Message` vt 0x81aa14): IntMessage, UintMessage, FloatMessage, TwoIntMessage, TwoFloatMessage, StringMessage, TwoStringMessage, VectorMessage, VectorUintMessage, TaskIDMessage, plus specific ones (ExplosionMessage, DamageImpulseMessage, CameraSceneMessage, WormCommandMessage, PayloadEventMessage, LandNewShapeMessage, ...).
- Frontend writes data keys via messages: `WXMsg.SetDataResource^<key>^<value>` strings (0x829884...).

### Lua API (XScriptService, data)
Function-name strings 0x8855ac..0x885654: echo, CopyContainer, QueryContainer, CloseContainer, EditContainer, CancelTimer, StartTimer, SetData, GetData, SendStringMessage, SendIntMessage, SendFloatMessage, SendMessage. Registration func 0x6958d2. Lua SendMessage impl 0x6963a4 (name -> 0x690d44 -> alloc 0x691705 -> 0x6910e4). GetData impl 0x696b44 (uses 0x639b1d manager + 0x69dab0 int value read). Scripts are compiled Lua: `Data/scripts/*.lub` (string constants readable).

### Where the key list lives (counts: data, from a one-off string scan)
- (a) exe strings matching `^[A-Z][A-Za-z]+(\.[A-Za-z0-9]+)+$`: **4773** (mixes data keys, message names, FETXT text ids, file-ish names).
- (b) exe keys found as string immediate <=0x30 bytes before a data-service wrapper call: **1297** (heuristic lower bound; misses keys built with sprintf or kept in statics).
- (c) message names (strings pushed to ctor 0x68bb9f): **981**. Only 4 overlap with (b): namespaces are separate.
- (d) `Data/Tweak/*.XOM` key-like strings: **5663** (1052 of (b) found there -> Tweak XOMs = default values + types, as X*ResourceDetails inside XDataBank/XContainer).
- (e) `Data/scripts/*.lub` key-like constants: **996** (314 also in Tweak, 106 are message names).

Per prefix (exe / data / msg / tweak / lua): Worm 75/27/31/70/50; Camera 122/67/45/91/20; GameLogic 103/6/80/14/53; Crate 49/35/8/36/27; Land 55/18/19/41/4; Water 9/5/0/62/6; Weapon 71/14/41/23/6; HUD 177/85/7/111/13; Particle 151/10/12/123/9; Payload 38/9/29/9/2; Jetpack 24/17/5/17/4; Inventory 4/4/0/37/31; Trigger 23/13/9/13/27; FE 249/48/87/179/2; WXFE 303/83/6/453/40; WXD 92/87/0/119/3; FETXT 373/120/0/1264/0 (localized text ids); Comment 289/97/10/0/12; Input 136/0/102/28/0; Miss 6/0/0/36/413 (mission script vars).
GameLogic.* is mostly messages; Camera/Worm/Crate/Land are mostly data keys.

### Recipe: who reads key X
1. `pe.py str '^X$'` -> VA (may be several copies per TU).
2. `scan.index()['refs'][VA]` -> insn sites; `scan.func_start(site)` -> owning function; nearest assert file string (`.\\Foo.cpp`, pushed right after) names the source file.
3. First `call` after the site: wrapper table above gives type and mode (by-value each read vs cached handle). For handles, note destination `[this+off]`, then grep `[reg+off]` in that class to find the actual reads (value at `[[handle+4]+0x1c]`).
4. If the string sits in a 0x68bb9f ctor site it is a message: get the static object VA (ecx), then `refs[obj]` -> sites calling 0x68bbe6/0x690e3e (subscribers) and 0x68bcce (handlers); senders write the id into msg+4 before 0x6910e4.
5. Also grep `Data/Tweak/*.XOM` (default/type) and `Data/scripts/*.lub` (script readers/writers via GetData/SetData).

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
| MineFactoryContainer | 0x878ed8 | ~0x662d66 | 00 NumMineActivation u8 +2c; 01 NumTurnsInactive u8 +2d; 02 SafeRadiusPadding f32 +14; 03-05 MineVelocityX/Y/Z f32; 06 DamageMagnitude f32 +18; 07 ImpulseMagnitude +1c; 08 WormDamageRadius +20; 09 LandDamageRadius +24; 0a ImpulseRadius +28 |

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
| 7 | 0x582860 | HandleMessage/update (assumed) | all |
| 18 | 0x57ea40 | physics step (assert m_vAcceleration.y<=0) | Walk, Starburst |
| 19 | 0x580830 | bounce/skim response (asserts fMaxPitch, vSkimDamping) | Starburst, Donkey |
| **20** | **0x580f10** | **Detonate** ("PayloadLogicEntity::Detonate pos=", reads DetonationSfx, DetonateMultiEffect, NumBomblets) | Walk 0x592830, Starburst 0x588dd0 |
| 21 | 0x581740 | expiry handling (assumed) | Starburst |
| 24 | 0x581a60 | fire-press / expiration check (reads DetonatesOnFirePress, assert m_tTimeOfExpiration) | Flying 0x558020, Starburst |
| 26/27 | 0x57e0a0 / 0x61ff60 | control input (assumed; overridden by Homing/Flying/Star/Donkey) | |
| **28** | **0x57fc00** | **collision dispatch** (reads DetonatesOnLand/Object/WormImpact) | Donkey 0x553970 |
| 15/16 | 0x57e500 / 0x580200 | ninja-rope attach/detach (asserts m_tRopeTaskID) | Parabolic 0x575440 |
Non-virtual: PayloadLogicEntity::Arm 0x57ec20; CheckForGoingAwayFromTarget 0x57e130; Parabolic CheckWhenExpires 0x575020, FindFirstEvent 0x576580 (analytic trajectory event search), HandlePayloadEvent(MsgExpire) 0x577980; HomingPayload Initialize 0x560bf0; Walking StealInventory (Scouser); Melee Initialize 0x568860; PoweredWeapon Initialize 0x586bb0.
BaseWeaponLogicEntity: slot 11 EndFireWeapon 0x54a0e0, slot 12 BeginFireWeapon 0x54a020, SetWeapon 0x54a200 (debug strings). LogicalWeaponManagerService::WeaponSelected 0x565d30. AI fire: "AIActionFireWeapon::Update sending c_MsgFireReleased" (fire = message c_MsgFireReleased).
Entity constructors (vtable writers): Payload 0x57e660, Parabolic 0x5754d0, Jumping 0x5644e0, Walking 0x591e30, Homing 0x560820, Flying 0x557060, Starburst 0x588a20, Donkey 0x553000, Fatkins 0x554a80, Parachute 0x57a3d0, PayloadWeapon 0x582bb0, GunWeapon 0x55c960, MeleeWeapon 0x567490, BaseWeapon 0x549cf0. Creators registered by static init (refs in 0x7e5xxx-0x7eaxxx), i.e. instantiated by class name through a task/class factory.

### 4. Per-weapon table
Container class/camera/damage: data. Payload logic class: **assumed** from container class + naming (Payload->Parabolic; Jumping->JumpingPayload; Homing->HomingPayload; Flying->Flying, Starburst->StarburstLogicEntity; Donkey->DonkeyLogicEntity; Fatkins->FatkinsStrikePayload; OldWoman/Scouser->WalkingPayload (StealInventory = Scouser); Airstrike/SuperAirstrike via Bomber/SuperBomberLogicEntity; Gun->GunWeaponLogicEntity; Melee->MeleeWeaponLogicEntity; SentryGun->NewSentryGun*).
WeaponType enum values seen: 0 sentry payload, 1 utility, 2 aimed launcher/gun, 3 homing, 4 thrown, 5 placed/melee, 6 placed (landmine/flood/sentry), 8 animal, 9 walker, 10 strike. Meaning of values: assumed.
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
| kMineFactoryData | MineFactoryCon | True |  |  | 100 |  |  | 100 |  |  |  |  |  |  |

## 5. Worms: physics states and ground collision
### Physics state enum kWPS_* (data: pointer table at file off 0x50b0f4..., order)
0 Ambulatory, 1 DetectJump, 2 Ballistic, 3 Sliding, 4 Vaulting, 5 Override, 6 Passive, 7 DeathThroes, 8 DrownFloat, 9 Undefined.
Confidence: data (table order) + disasm (UpdateWalking writes 4 on "->Vaulting", 6 on passive; 7/8 skipped as dead).
Stored at pData+0xF0 (pData = per-worm physics data arg passed to Update* funcs; disasm).

### WXWormLogicEntity functions (disasm, via debug strings)
- 0x5a5d30 / 0x5d59c0: read WXWorm.SlideAngle_Default/_Slippy, SlideFriction_Default/_Slippy, JumpHeight/Distance
- 0x5a6af0 EstablishPhysicsState; 0x5aa7f0 ChangeState (logs "Worm Sliding"); 0x5aaac0 Activate (spawn placement: partial X/Y, left/right up/down probes)
- 0x5ab3d0 CalculateGlobalControlInput; 0x5a9880 reads Worm.Walk.Speed; 0x5ac3e0 Worm.FallDamageRatio
- 0x5acba0 Fall (->Ballistic); 0x5acea0 Rebound; 0x5ad010 ImpulseWorm; 0x5adcf0 MsgOverridePhysics; 0x5aea10 UpdateBurning
- 0x5aecb0 UpdatePassive; 0x5afbe0 UpdateSliding (-> Ambulatory + kWE_Landed); 0x5b0da0 UpdateWalking; 0x5b1d60 Update (dispatcher)
- 0x5c02c0 reads Worm.BounceMultiplier(Default), Worm.SlideStopVel(Default)
- Worm.StepUpHeight and Worm.WalkOffCliffVelMulti: NOT present as strings in the PC exe (data: raw byte search). TWEAK.XOM keys likely unused/hardcoded.

### Worm land probe object (WXWormLogicEntity+0x24) (disasm + data)
- Init 0x59ef40(points, flags, edgePairs, nPoints<=16, nEdges); worm instance built in 0x59f1e0 with points 0x91ffc8 (8), edges 0x91ffc0 (3).
- Points (x,y,z, Y up, worm-local, NOT rotated by facing as far as seen): foot tripod (4,0,-3) (-4,0,-3) (0,0,5) (0,0,0); head copies at y=20.
- Edges: (0,4) (1,5) (2,6) = three vertical rods 20 high. The centre point (3/7) is not an edge.
- 0x59ec70 CastRays(start, dir, ?, maxLen, mask): for each point bit in mask, ray from start+P[i] via 0x466ae0 (land ray); stores per-ray hit dist (+0x18c+4i), hit flag (+0x1cc+i), hit point (+0xc+12i), nearest index (+0x1dc, 0x7f=none), nearest dist (+0x1e0), hit count (+0x1e4). Returns hitcount>0.
- 0x59edf0 Fits(pos): for each edge, segment pos+P[a] -> along (P[b]-P[a])*0.01, len 100 (= full rod) via 0x466ae0; any hit -> false. ONLY 3 thin vertical rods are tested: no volume, no sphere, no centre rod.
- 0x59ec00 HitPointRel(i|0x7f=nearest) = hitPoint - P[i]; 0x59ef10/0x59eee0 material/surface ids of hit; 0x59ef90 ground normal from hit points (needs >=2 hits).

### State dispatch (disasm: Update 0x5b1d60, switch on pData+0xF0, jumptable 0x5b2248)
0 Ambulatory -> 0x5b0da0 UpdateWalking | 1 DetectJump -> 0x5aefa0 | 2 Ballistic -> 0x5af430 | 3 Sliding -> 0x5afbe0
4 Vaulting -> 0x5aca80 | 5 Override -> 0x5ab3d0 (control input only) | 6 Passive -> 0x5b0c20 | 7 DeathThroes -> 0x5aa080 | 8 DrownFloat -> 0x5aa130 | 9 Undefined -> log.
ChangeState = 0x5aa7f0(pData, newState). 7/8 are sticky (walking never overrides them).

### Land ray primitive 0x466ae0 (disasm)
(start, stepVec, ?, nSteps:int, flags...) marches nSteps samples of stepVec; results in globals: 0x952cf8 hit dist, 0x952cfc hit step (>nSteps = miss), 0x952d4c hit point, 0x952ce8/0x952c64 surface ids. Discrete sampling: features thinner than one step can be skipped (assumed from integer step count).

### Slide thresholds (disasm 0x5a5d30)
0x92009c[0]=cos(SlideAngle_Default deg), [1]=cos(SlideAngle_Slippy); 0x9200a4.. Slide friction; 0x9200ac.. squared StartSlideVel/StopSlideVel (Default/Slippy). Also reads Gravity.
0x4adda0(normal, mat) = normal.y >= cos(SlideAngle[mat]) ("WormShouldNotSlipOnLand").

### UpdateWalking 0x5b0da0 algorithm (disasm; constants data)
1. If pData+0x54==0 (no physics owner?) -> Passive(6). If 0x5ac390 -> UpdatePassive. If no move input (0x5ab3d0 false) -> idle branch: if standing surface id (ent+0xE8, 0xFFFF=none) no longer valid -> Ballistic(2); else zero vel, facing/anim upkeep.
2. disp = vel(pData+0x68)*20*k (k from 0x69db80, /0.004); cand = pos(pData+0x38)+disp.
3. Ground probe: CastRays(cand + (0,20,0), down, len 200, mask 0xF = 4 foot points). d = 20 - nearestDist = height of HIGHEST of the 4 hits relative to foot. Miss -> d about -181.
4. d > 20: blocked, no move (return).
5. 5 < d <= 20 (ledge higher than hardcoded 5.0 @0x8244fc = TWEAK StepUpHeight, which the exe never reads): cand = nearest hit point +0.1y; ground normal from rays; sphere resolve (0x519ed0) may replace cand; require 0x4adda0 walkable and Fits(cand); then set pos, store old pos (ent+0xEC), target (ent+0x104), timer 250 (ent+0x110) -> Vaulting(4) ("WormShouldNotSlipOnLand ->Vaulting").
6. -5 <= d <= 5 (normal step, @0x5b154a): cand = nearest hit +0.1y; sphere resolve. If sphere contact: n.y < -0.1 -> blocked; n.y <= 0.8 -> cand -= 15*n, cand.y += 10, resolve again, Fits -> Vaulting ("ResolveSphericalCollisions ->Vaulting"). No contact: walkable check, then Fits(cand) retried raising y by +1.0, up to 6 tries -> set pos; if ground n.y < cos(SlideAngle[mat]) -> Sliding(3).
7. d < -5 (drop/miss): cand.y = y-5, 0x51a510 sphere sweep; if contact and Fits -> place. Else Fits(cand at old y) -> Fall() 0x5acba0 (Ballistic). Else raise y +1.0 up to 6 tries then place, else blocked.

### Vaulting 0x5aca80 (disasm)
If input dot old vel (ent+0xF8) <= 0 -> target=old pos, back to Ambulatory. Else timer -= 20; timer<=0 -> pos = target (snap), Ambulatory. Else pos MoveTowards(target, 4.0/frame) via 0x5a59f0. No collision test during the vault move.

### Sphere resolver 0x519ed0 (CollisionManagerService, worm sphere ent+0x28) (disasm)
Sets own bound sphere centre at pos (+radius in y unless flag), iterates contacts (fn ptr 0x95c25c). Contact 1: push out along contact normal to touching distance. Contact 2: solve 2-sphere intersection circle. Contact 3+ (jumptable 0x51a4f4 cases 2,3): give up, return the fallback/old position. Sphere-vs-sphere entities (worms, crates, drums, LandFramePseudoEntity), NOT the voxel land.

### Why worms can enter scenery in corners (assessment: disasm + reasoning)
- Body vs land = only 3 thin vertical rods (x=+-4,z=-3 and z=+5, 20 tall, Fits 0x59edf0); no centre rod, no width beyond 4-5 units, no ceiling/forward probe. A concave corner or overhang edge passing between rods is never seen.
- Push-out is vertical-only (+1 unit, max 6 tries); no lateral push or iteration toward a free direction.
- Ground height = MAX of 4 downward rays from 20 above; the start point can already be inside an overhang (ray starts in solid -> treated as hit at 0 -> d=20 boundary).
- Vault target accepted from a single Fits test then interpolated with no further checks; sphere resolver gives up after 2 contacts.
- Ray is step-sampled (integer steps), can skip thin features.

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
- CheckSuddenDeath: if AllianceCount>1: if RoundTimeRemaining==0 and not started -> StartSuddenDeath(); Send GameLogic.AboutToWaterRise; Water.Level += Water.RiseSpeed.Current (every turn end once SD, assumed: RiseSpeed.Current=0 before SD).
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
- Message IDs: per-TU static objects, init funcs 0x7dxxxx-0x7fxxxx = `push "Name"; mov ecx,G; call 0x68bb9f` (register). Dispatcher compares msg against G via 0x68bcce. Rebuild the name -> handle -> users table with `xref.py` (section 10).

### 6. Exe: who handles what [disasm, class names from assert strings]
- TimerLogicEntity (.\TimerLogicEntity.cpp): 0x50f980 HandleMessage (Timer.StartGame/StartTurn/EndTurn/StartHotSeatTimer/StartPostActivity/StartRetreatTimer/EndRetreatTimer; asserts iRoundTime in {0,-1}..10000?, iTurnTime<=10000 -> times in game units checked vs. ms/??; strings "TimerLogicEntity MsgStartTurn", "hot seat canceled", Turn.Boring, Turn.PayloadFired). 0x50f100 tick: posts Timer.HotSeatTimedOut ("hot seat timed out"), Timer.TurnTimedOut, Timer.PostActivityTimedOut, Timer.RetreatTimedOut. 0x50f4e0 binds data keys to members: RoundTime, TurnTime, HotSeatTime(+0x7c), PostActivityTime(+0x80), RetreatTime(+0x84), RoundTimeRemaining, TurnTimeRemaining, HotSeatTimeRemaining, PostActivityTimeRemaining, RetreatTimeRemaining, ClockDisplayMode, ElapsedRoundTime.
- Defaults (Data/Tweak/LOCAL.XOM, XIntResourceDetails, ms) [data]: TurnTime 45000, RoundTime 1800000, HotSeatTime 10000, PostActivityTime 2400, DefaultRetreatTime 3000, RetreatTime 0. stdvs overrides from scheme (DefaultRetreatTime<-LandTime, HotSeatTime<-HotSeat, TurnTime, RoundTime). Challenges set RetreatTime/DefaultRetreatTime/PostActivityTime 0; Wormpot NoRetreatTime -> DefaultRetreatTime=RetreatTime=0.
- Retreat time per weapon [disasm]: on fire, weapon logic entities do r=GetData("DefaultRetreatTime"); if props.RetreatTimeOverride (i32 @+0x50 in weapon properties, schema field #0x10) >= 0 then r=override; SetData("RetreatTime", r). Seen in GunWeaponLogicEntity 0x55cff0, MeleeWeaponLogicEntity 0x568860, NewSentrygunWeaponLogicEntity 0x56e6d0, PayloadWeaponLogicEntity 0x582d70; AI planner 0x4a1cf0 reads DefaultRetreatTime. FloodLogicEntity 0x555580 / FloodWeaponLogicEntity 0x555d30 and 0x588160/0x58ba00 write RetreatTime directly. Timer.StartRetreatTimer senders: TUs at 0x5210b0, 0x549bb0 + several weapon TUs (msgobjs 0x95c6ac,0x95d04c,0x95d964,0x95e094,0x95ea20). Front-end option "NoRetreatTime" (FE.WP.NoRetreatTime, 0x5d5830).
- GameLogicService (.\GameLogicService.cpp): 0x4fdc90 HandleMessage (WormSelect.WeaponSelected/OptionSelected, GameLogic.AddMeToDeathQueue, Worm.Died, GameLogic.GunWaiting, Turn.Started/Ended, MsgActivateSuddenDeath, MsgTurnEnded, inventory, telepads, briefing box); 0x4f7d80 = subscribe/init (wind, mines, SuddenDamageMode, WormPot, GoodShotDamageThreshold, MaxRandomCrates). 0x4fb880 HandleEndOfGame (Win/Draw -> GameOver menus, rounds, stockpile, mission/challenge records).
- Death queue [disasm]: AddMeToDeathQueue handler 0x4fac70 pushes worm id onto vector at GameLogicService+0x1fc. Tick 0x4fa2c0 calls 0x4f9b30 every frame: if queue non-empty and (ActiveObjectRegistrationService count (0x4d3960) == queue size, OR timer @+0x210 expired, OR flag+0x1b1 set and count <= size+2): clear flag, pop FRONT id, post Worm.TimeToDie to that entity. One worm per pass -> deaths are sequential (each dying worm is an active object until done). Flag +0x1b1 set by message GameLogic.GunWaiting (gun weapons waiting for input tolerate 2 extra active objects) [assumed meaning].
- Senders: WXWormLogicEntity 0x5abc50 (damage-display routine "Worm Displaying Damage Taken", DamageGraphic.Offset) posts AddMeToDeathQueue(worm id) [assumed: when energy <=0 after damage shown]. 0x5ab7e0 (take damage) posts Worm.Damaged / Worm.Damaged.Current, sets Turn.Boring/Mistake/FriendlyDamage/EnemyDamage, DamagedWorm.Id, DamageTypeTaken, uses DoubleDamage, MostRecentlyActiveWorm. 0x5b07c0 worm HandleMessage: Worm.TimeToDie (death sequence, see docs/death-sequence.md), GameLogic.ApplyDamage, Worm.ApplyPoison, Land.NewShape. 0x5a6970 Cleanup: DeadWorm.Id, posts Worm.Died. Net.Client.TimeToDie: 0x5f5220 (net replication).
- WXWormManagerService (.\WXWormManagerService.cpp): 0x5b5e70 HandleMessage: SpawnWorm, RespawnWorm, ActivateNextWorm (worm selection), ReinitialiseWorms, EndTurn, SelectNextWorm, UnspawnWorm, ApplyDamage; 0x5b37c0 subscribe (Water.Level).
- ActiveObjectRegistrationService 0x4d37a0 (Unregister): posts GameLogic.NoActivity when count hits 0; "ObjectCount.Active" read by AIService 0x4b3390, 0x4d3cb0 and GunWeaponLogicEntity.
- AIService 0x4b3390: handles GameLogic.EndTurn ("AIService got message c_MsgEndTurn"), GameLogic.AITurn.Started, AI.WeaponsDontEndTurn.
- Many services subscribe Turn.Started/Ended/EndTurn (HUD, camera, weapons, net 0x70bda0/0x7f7d30 NetService "Received Gamelogic.Turn.Started/Ended", g_msgEndTurnImmediate). Full list in turn_msgrefs.txt.

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

## 7. Audio
Parsed with throwaway scripts (not kept); layout below is enough to redo it.

### WormsX.fev (FEV1, ver 0x00400000, 1.3 MB) [data]
- Header: "FEV1", u32 ver, 2 u32 sizes, u32 n=0x21 then n (id,value) memory hint pairs; project name "WormsX".
- Bank table: u32 count=117; each = u32 loadmode, u32 maxstreams, 8-byte hash, u32 len + name. loadmode 128 (stream?) for ambient, Cheer, frontendmusic, mu*; 256 (decompress-into-memory?) for the rest [data; mode meaning assumed].
- 11 banks listed but NOT on disk: Outtake* (DestructAndServe, Dialog, DinerMight, GhostHillGraveyard, Introduction, RecordingBooth, ScreenTest, TheLandThatWormsForgot, TinCanWally, JoustAboutIt, MineAllMine) [data].
- Categories: master/{music, Speech/{EFMVDialogue, Speechbanks, Custom}, AmbientEffect, SpotEffect/{EFMVFoley, Weapons, Frontend, Global}} [data, hierarchy from string order: assumed].
- Group header = len+name, u32 nprops, u32 nsubgroups, u32 nevents; event = u32 type(0x10 simple / other), len+name, 16-byte GUID, blob, category string [data/assumed].
- Sound definitions at 0xce58e..: 3995 sounddefs, 4058 waveforms; each = "/folder/name", waveform "bank/file.wav", bank name string [data].
- Event path = <group>/<event> (exe uses e.g. "frontendsfx/Highlight"? see exe section).

#### Top-level groups (under root "Master", 10 children) [data]
| group | events | category | bank(s) via sounddefs | examples |
|---|---|---|---|---|
| frontendsfx | 26 | SpotEffect/Frontend | frontendsfx (26 smp) | click Cancel grenade In_BigBounce In_Book In_Next Out_Prev PageTurn WormPotLoop WormPotStop |
| frontendmusic | 2 | AmbientEffect / music | frontendmusic (2 smp, stream) | FrontendDay, femusic |
| weapons | 167 | SpotEffect/Weapons (params Cycle, Time, WindStrength) | weapons (179 smp) + 3 sounddefs from global | BazookaEquip ExplosionRegular ShotgunFire SheepBaa GrenadeBounce SplashHeavy Teleport JetPack NinjaRopeFire CrateImpact PickupWeapon Wind Thunder |
| cheer | 1 | SpotEffect/Global | Cheer (1 smp CrowdCheer, stream) | cheer |
| EFMV/<mission> | ~66 subgroups | Speech/EFMVDialogue, SpotEffect/EFMVFoley, param Time | bank = mission name (e.g. TinCanWally 46 smp) | TinCan_Player1_09, Foley_Bell, Amb_Workshop |
| Speech/<voice> | 31 voices x 43 events | Speech/Speechbanks | vo<voice> | Cheer ClutchChest Collect CrateDrop DamageInflictedA/B Disbelief EnemyDeath FireDamage FirstBlood FriendlyDeath Gasp GrenadeLanded Incoming Jump MaxDamage Missed Mistake NoDamageA/B Nooo PointAndLaugh PullNinjaRope Punch Revenge SadSigh ShakeFist ShallowDrown ShortOnTime Shriek SkipGo Sneeze StartTurn Startled Taunt Titter Traitor Victory Waiting WeaponFired Wipebrow WormBounce Yawn |
| ambient/<theme><Day/Night> | 10 groups x 1 event "Ambience" | AmbientEffect | ambient (7 smp, stereo, stream) | ArabianDay/Ambience, BuildingNight/Ambience, Camelot*, Prehistoric*, WildWest* |
| global | 11 | SpotEffect/Global | global (15 smp) | Telephone In_Controller ExplosionRegular click3 Highlight Typewriter In_Scalehitxy FireWorksExplosion In_ScaleY click2 FEError |
| OuttakeDialog/<sub> | 13 subgroups | Speech/EFMVDialogue | Outtake* / OuttakeDialog (missing on disk) | ScreenTest_Director_01, WelcomeMovie_Narrator_01 |
| music | 13 | music | mu<Theme> (1 smp each, stream) | Construction Arabian Victory WildWest Prehistoric SuddenDeath Arctic Pirate Lunar Horror War England Camelot |

Speech voice groups (31): vodisco vocyber voblues voastro vodoubl vocowbo vodino voknigh vogshow vohorro voclassi vobobby voalien vobuild voprofe vowizar vocave voscot voscous vothief vobarre vomeme vopirate vosuper vol33t vocad vorock vodevvo voklein vofighter voRussian. Sounddef folder /Speech/vodouble -> bank vodoubl (name mismatch) [data].

### FSB banks on disk (Data/Audio/PC, 106 files, all FSB4) [data, sd.py]
- Mode 0x40200 (MPEG, padded) everywhere except voRussian.fsb = 0x10 (PCM16, 32 kHz, 11 MB). 44.1 kHz except vocad 22.05k, vopirate/vosuper/voRussian 32k.
- weapons 179 smp stereo (15.8 MB); global 15; frontendsfx 26 mono; frontendmusic 2 stereo (FrontendDay + femusic); ambient 7 stereo (ArabianDay...); Cheer 1 (CrowdCheer).
- Music: 13 mu<Theme>.fsb, 1 stereo sample each named <Theme> (Arabian Arctic Camelot Construction England Horror Lunar Pirate Prehistoric SuddenDeath Victory War WildWest).
- Speech: 31 vo*.fsb, 49..128 samples, mono; sample names = spoken line text truncated to 29-30 chars (e.g. "a gift", "OHDEAR", "Cheer_01"). Disk casing differs from fev (voBuild/voCave/voDino/voWizar vs vobuild...).
- EFMV/story/challenge banks: one per mission/challenge/deathmatch/tutorial (4..46 smp), mixes dialogue (<Mission>_<Role>_NN) and Foley_* / Amb_* (stereo).
- Outtake* banks: in fev, not on disk.

### Exe (WormsMayhem.exe) [strings = data; call roles = disasm]
- Event names are full FMOD paths "<group>/<event>": weapons/Debris (0x81c97c), weapons/RainLoop, cheer/cheer (0x8373d4), music/victory (0x8373e0), music/<Theme> table 0x868710..0x8687b0, Music/SuddenDeath, global/FEError, frontendsfx/* + global/* table 0x8a98e8..0x8a9bb0 (WXFE_ControlAudioEnum kAUDIO_* order), EFMV/Failures/Failures_Narrator_0N, prefixes 'EFMV/', 'Ambient/' + '<Theme><Day|Night>' + '/Ambience', 'frontendsfx/', 'global/', 'Speech/voclassi' (default voice) + '/LIP'.
- Bank/group names loaded by AudioService: cheer, frontendsfx, frontendmusic, weapons, global, 'EFMV/Failures', 'Story.'/'Deathmatch.'/'Challenge.'/'Tutorial.' prefixes, speech voices; file 'WormsX.fev' under 'data\\audio\\PC\\'. Legacy unused strings: 'Data/Audio/WXSoundBanks/PC', 'wormsx.cpd', 'streamfx', 'ambience', mu<xxxx>1/2 (PS2/XACT era) [assumed].
- RTTI: AudioService (0x92109c, .\\AudioService.cpp), XAudioManager / IXAudioManager (XOM), XSoundInstance, SoundBankData, SoundBankColective, SpeechRequestMessage, RainAudioEntity, EFMV_TriggerSpeechEventContainer, EFMV_TriggerSoundEffectEventContainer.
- Play functions [disasm]:
  - 0x6f9050 = get IXAudioManager singleton (0x604390 is a jmp to it).
  - 0x604a20 = PlaySound(const char* event, XSoundInstance** out): if no-sound flag ([0x95a100]+0x9b bit 4) logs "ADS: No sound = true" and returns 1; else mgr->vtbl[0x34](mgr, event, out). 47 callers. Callers then call inst->vtbl[0x10] (start/release?) on the returned instance.
  - 0x604a80 = PlaySoundFire&Forget(const char* event, arg) -> mgr->vtbl[0x38](mgr, event, arg, 1.0f volume). 36 callers.
- WEAPTWK.XOM field names: EquipSfx, HoldLoopSfx, LaunchSfx, LoopSfx, BounceSfx, PreDetonationSfx, DetonationSfx, ArmSfx1Shot, ArmSfxLoop, BigJumpSfx, WalkSfx, FlyingLaunchSfx, FlyingLoopSfx, ReloadSfx, ExplosionSfx, FireSfx, SplashSfx, DischargeSoundFX, DischargeEndSoundFX; values are full paths, 48 unique "weapons/..." strings in WEAPTWK (e.g. weapons/ShotgunFire, weapons/SheepBaa, weapons/GrenadeBounce, weapons/HolyGrenadeExplosion) [data]. PARTTWK.XOM EmitterSoundFX (+EmitterSoundFXVolume) also uses weapons/* and global/ExplosionRegular, global/FireWorksExplosion [data]. Other: NinjaRope.Fire.Sfx / NinjaRope.Impact.Sfx (tweak keys), SfxBankName, SoundEffectName (EFMV).
- Land surface sounds: Theme<X>.txt material line 5 (Rock01, grass01blend...) has NO matching event in WormsX.fev (0 hits) -> unused on PC [data; "unused" assumed].

### Speech (.lsd / LIP.txt) [data]
- Data/Audio/Speech/<voice>.lsd: text, blocks "<name>Speech/<voice>/<Category></name>" + one u32 line hash per variant + ";". 43 categories per voice = the 43 FEV events of group Speech/<voice> (list above). 33 .lsd files: 31 voices + voconfu, vowhoop (no fsb, no fev group).
- Data/Audio/EFMV/<Mission>.lsd: same format, one event per block "EFMV/<Mission>/<Line>" with one hash.
- <game>/speech/<voice>/LIP.txt: "#<hash> <line text>.txt" header then "frame,VISEME,<none>" lip-sync rows; line text (truncated to 29 bytes) = FSB sample name. Exe: "Reading speech file " + ".txt", "Speech/<voice>/LIP".
- So: speech event Speech/<voice>/<Category> -> bank vo<voice>.fsb; variant pick via lsd hash -> LIP line -> FSB sample (importer already does this).

## 8. Rendering
Conf: data = read from CG/ or exe strings; disasm = code; assumed = inferred.

### 1. CG shaders (data)
| File | Role | Entry points | Key uniforms / inputs |
|---|---|---|---|
| FixedFunction.cg | GL fixed-function emulation for all meshes | 48 vertex: FFVertexMain{,Tex}{,LitPoint,LitDirectional}{,Skinned}{,Col}{,Lighting} (combinatorial); fragment: FFFragmentMain, FFFragmentMainTex, FFFragmentMainLit, FFFragmentMainTexLit, FFFragmentMain{,Tex}{,Lit}Col; helper CalculateLighting | model/view/projection, texTransform, materialMatrix, BlendMat[35] (register c8, skinning palette), numBonesPerVertex, lightPosition (model space), lightDirection, lightAmbient/Diffuse/SpecularCol, viewProjection. "Lighting" variants = position-only pass (assumed: lighting/shadow colour pass) |
| Landscape.cg | Terrain (heightmap chunks) with shadow map | LandscapeVertexMain, LandscapeFragmentMain; helpers GetShadowScale, GetInShadow | VS: model, view, projection, globalLightDir, texTransform, shadowMatrix; in POSITION/NORMAL/TEXCOORD0/COLOR; out TEX4=shadow-space pos. FS: texture0, shadowMap, shadowSize(xy=map size), globalDiffuse/Ambient/Specular/Fresnel. Shadow PCF: 3x3 weighted (0.075 corner / 0.124 edge / 0.204 centre, offset 0.5 texel) on one path, 2x2 bilinear compare or 5-tap cross on others (platform #if). Outside [0,1] = lit. Lighting skipped if shadowScale<=0.01 (diffuse/spec 0). Comment: specular/fresnel consts hard-coded, shared across all landscapes |
| water.cg | Water plane | WaterVertexMain, WaterVertexMainLighting (pos-only), WaterFragmentMain, OldWaterFragmentMain; helpers Panner, FromCubeMap, FromNormalMap | VS: modelViewProjection, modelView, model, cameraPos, textureScale. FS: texture0 diffuse, texture1 normal, texture2 environment spherical map (old: cubemap), pausedTime, combinedWaterParams (float4x4): [0].x reflection_contrast, [0].y reflection_strength, [0].z specular_power, [1].xyz waterNormalIntensity, [2].xyz waterNormalIntensity1, [3].x specular_contrast, [3].y specularFadeScale, [3].z nearOpacity, [3].w substractColourScale. 3 scrolled normal layers: t=pausedTime*0.5; UV*5 + t*(-0.3,0.6); UV*-0.2 + t*(0,0.2); UV*-0.75 + t*(-0.1,-0.2). reflection = pow(env, contrast)*strength; spec = saturate(pow(env.r, specular_power)*specular_contrast) |
| PostProcess.cg | Full-screen passes | VertexMain (shared VS); FS: Copy1x1/1x2/2x2/2x4/4x4 (box downsample, SSAA resolve; texCoord.zw = texel size), CopyWithLuminance (alpha = Rec601 luma .299/.587/.114, for FXAA), CopyFxaa (FXAA 3.9, FXAA_PC + GLSL_120, uniform rcpFrame), CopySepia{1x1..4x4}, CopyFxaaSepia, Silhouette (console), Silhouette_PC, Outline, RecombineWorms | Sepia: luma (0.3,0.59,0.11), lerp(col, luma*sepiaColour.rgb, sepiaColour.a) (vertex COLOR0 carries colour+weight). Silhouette fill colour (0.25,0.25,0.25,0.5): occluded worm pixels blended 50% grey. Outline: 4 diagonal depth taps at +-1 texel of worm depth tex; any tap >=1 (empty) -> outlineColour (COLOR0, alpha blend), else discard. RecombineWorms: lerp(scene, worm colour, wormOpacity.a), depth=min |
| XenonVideo.cg | Xbox360 video YUV->RGB (unused on PC, assumed) | XenonVideoVertexMain, XenonVideoFragmentMain | texture0/1/2 = Y/U/V, modelViewProjection |
| Fxaa3_9.h | NVIDIA FXAA 3.9 header, included by PostProcess.cg | FxaaPixelShader | - |

### 2. Post-process (data: exe strings)
- Classes (rtti): BasePostProcess, IXPostProcess (interface), PCPostProcess (impl, source PCPostProcess.cpp, has InitialiseGlFunctions), NullPostProcess (assumed: used with /NOPOSTPROCESS).
- Render bins: OutlinedWormsPreProcess, OutlinedWorms1, OutlinedWorms2, OutlinedWorms3, OutlinedWormsPostProcess.
- Logs: "PCPostProcess: SSAA now set to", "... with FXAA", "NVidia FXAA enabled", "Fxaa is not available: ", "Error - CopyFxaa not found".
- Sepia tweak keys: "Sepia", "Sepia.LerpWeight", "Sepia.Color".
- Scene-graph effect classes also present: XBloomShape, XFocusBlurShape, XBlurEffect (+ "BlurSeparation" field). No bloom/DOF shader in CG/ -> assumed unused / fixed-function path on PC.
- Extra shader entries referenced by exe (data): Landscape.cg also has LightingLandscapeVertexMain/FragmentMain and LightingHeightMapVertexMain/FragmentMain (position-only lighting/shadow passes). Exe refs Landscape.cg in 004480f0, 00460560, 004d4d70; water entries in 0048b460, 0048bc00 (WaterCgGraphicEntity), 004d4d70, 004d6430.
- PCPostProcess functions (disasm, via PCPostProcess.cpp refs): InitialiseGlFunctions 0061c600; program creation / CopyFxaa lookup 0061d050; uses "shadowMap" 0061d6b0; Initialise + Sepia.Color/LerpWeight read 0061f190; SSAA set/log 0061f7a0; others 0061cb10 0061ce00 0061cf00 0061dcb0 0061e0e0. Vtables: PCPostProcess 0086c7d4/0086c7f4, BasePostProcess 0086c07c/0086c09c, NullPostProcess 0086ca50.

#### Render bins (data: pointer table at 0091e2b8, 69 entries, index = draw order)
0 FESkybox1/Particle0, 1 FESkybox2/Skybox1, 2 FESkybox3/Skybox2, 3 FEWater/Skybox3, 4 FEWater2/Landbase/Land1, 5 FELensFlare/Land2, 6 LandShadow, 7 AfterLandHUD, 8 3D, 9 WaterBlend, 10 Water, 11 ParticleUnderWater, 12-14 Water2-4, 15 WaterRipple, 16 DetailObjects, 17 LandFringe, 18 OutlinedWormsPreProcess, 19-21 OutlinedWorms1-3, 22 OutlinedWormsPostProcess, 23-27 Particle1-5, 28 3DTextBack, 29 3DText, 30 LensFlare, 31 FirstPersonWeapon, 32 MenuBack, 33 WhiteOut, 34 HUDSniper, 35 HUD0/HUDFirst/HUDBack, 36 HUD1, 37 HUD2/HUDMiddle, 38 HUD3, 39 HUD4/HUDFront, 40-62 HUD5-27, 63 HUDLast/HUDMouse, 64 EFMVBorders, 65 EFMVBriefingText, 66-68 Loading0-2.
Implication (assumed): worms are drawn after land/water/details into OutlinedWorms bins, silhouette+outline composited in PostProcess bin, then particles (Particle1-5) on top, then HUD.

#### Command-line switches (data strings; flag effects disasm in 004d95c0, config object *0x0095a100)
| Switch | Effect |
|---|---|
| /SSAA N | N in 2..16 switch; sets cfg+0x6c/+0x70 = X/Y factors (2 -> 1x2, 8 -> 2x4; 4 -> 2x2, 16 -> 4x4 assumed) matching Copy1x2..Copy4x4 resolve shaders |
| /NOSSAA, /FXAA, /DISABLEHARDWAREAA | AA toggles ("NVidia FXAA enabled") |
| /SEPIA | calls fn ptr ds:0x95a0e8(1); "Activate Sepia mode" |
| /WIREFRAME | cfg+0x9b |= 0x20 |
| /CREATESHADOWCOLOURMAP | cfg+0x9d |= 0x80 ("colour texture for the lighting pass") |
| /SHADOWMAP N | cfg+0x88 (WORD) = shadow map size |
| /NOWORMOUTLINES | cfg+0x9c &= ~0x20 (outlines on by default) |
| /AMBIENTOCCLUSION | cfg+0x9c |= 0x10 |
| /NOPOSTPROCESS | cfg+0x9c |= 0x08 |
| others (non-render) | /LOG /RUN /CRC /DONTOPTIMIZEATTRIBUTES /IMPORTTGAS /EXPORTTGAS /STARTUP /NOHARDWARESOUND /NET_LOG /AITEST /NOATTRACT /NOFRONTEND /TRIGGERSINVISIBLE /ALTJUMP /SELFTEST /NEW_CONTROL_BOX /GAMEOVERMENU /STARTMENU /MENU /WEATHER /LEVELNAME /DATABANK /SCRIPT /TIMEOFDAY /THEME /LEVEL /SHUTUP /NOCAMERALOAD /DELETECLOUDSAVE /NOSOUND /NOMUSIC /SHRINKHUD /ZIP /GAMETIMESTAMP /GAME /NAME /JOIN /HOST /ENABLEPORTFORWARDING |

### 3. Particles (data: PARTTWK.XOM via xom.py)
- Containers: 202 EffectDetailsContainer, 874 ParticleEmitterContainer, 24 XUintResourceDetails (TwkEdVer.* editor versions, all 12), 1 XDataBank; 1075 XContainerResourceDetails (name -> container index).
- EffectDetailsContainer = only `EffectNames: [emitter names]` (e.g. 3 emitters ParticleExplosionRing, ParticleFireFlash, ParticleExplosion). Emitters per effect: 0..14, mode 2 (dist 1:34 2:54 3:34 4:18 5:15 6:11 7:7 8:10 9:4 10:2 11:2 12:1 14:2 0:8).
- Naming: effects mostly WXP_* (97), Part* (26), WXPL_ (6, level/scenery: twister, tower storm, seagull, worm ghost), WXPF_ (6, assumed front-end), Weapon* flashes; emitters WXP 425, Part 84, WXPL 34, WXPF 21, R_ (14, R_Explosion_*), AM, Sprite, Splash, W_ (W_Explosion_*), Boomf.
- ParticleEmitterContainer: 98 fields. Groups: Emitter* (Type, Acceleration(+Randomise), IsAttachedToLand, IsOfInterest, IsLaunchedFromWeapon, LifeTime(+Rand), MaxParticles, NumCollide, NumSpawn(+"Radnomise" typo), OriginOffset(+Rand), ParticleExpireFX, ParticleFX, SameDirectionAsWorm, SoundFX, SoundFXVolume, SpawnFreq(+"Ransomise"), SpawnSizeVelocity, StartDelay, Velocity(+Rand)); Particle* (Acceleration, Alpha, AlphaFadeIn, AlphaVelocity(+Delay), AlternateAcceleration N/S, AnimationFrame/Speed(+Rand), Attractor(+IsActive), CanEnterWater, Collision* (Freq, ImmuneTime, MinAlpha, "Collison"Radius(+Offset,+Velocity), ShowDebug, WormDamage/Impulse/ImpulseY/Poison Magnitude, WormType), Color[] + ColorBand[] + NumColors, ExpireShake(+Length,+Magnitude), IsAlternateAcceleration, IsEffectedByWind, IsSpiral, IsUnderWaterEffect, LandCollideType, Life(+Rand) ms, Mass, NumFrames, Orientation(+Velocity,+Rand), Size vec2 (+Rand, Velocity, VelocityDelay, FinalSizeScale, SizeFadeIn(+Delay), SizeOriginIsCenterPoint), RenderScene, Spiral* (Radius, RadiusVelocity, RadiusSizeVelocity), Velocity(+Rand, IsNormalised)); plus Comment, SpriteSet (e.g. Particle.WXSprite4), MeshSet[], MeshAnimNodeName.
- No blend field on emitters: blend comes from sprite set texture (Particle.Additive1-3 sprites exist) / XBlendModeGL with kBlendFactor{Zero,One,SrcColor,OneMinusSrcColor,DestColor,OneMinusDestColor,SrcAlpha,OneMinusSrcAlpha,DestAlpha,OneMinusDestAlpha,SrcAlphaSaturate,MinusOne} (data; mapping assumed).
- Enums (data strings, order = value assumed reversed-listing):
  | Field | Enum | Values | Usage in PARTTWK |
  |---|---|---|---|
  | EmitterType | ParticleTypeEnum | kNormal 0, kSnow 1, kRain 2, kTrail 3 | 0:838 1:2 2:4 3:30 |
  | ParticleRenderScene | ParticleSceneEnum | kPS_Default 0, kPS_Scene1..5 = 1..5 (-> bins Particle1..5 23-27, assumed; Default bin unknown) | 0:713 1:101 2:14 3:24 4:7 5:15 |
  | ParticleLandCollideType | LandCollideEnum | kLC_None 0, kLC_Bounce 1, kLC_Expire 2, kLC_StopMoving 3, kLC_StopMovingAndAttach 4 (order assumed) | 0:666 1:202 2:2 3:4 |
  | ParticleCollisionWormType | WormCollideEnum | kWC_Standard, kWC_Expire, kWC_Lightside, kWC_Darkside (+ WormCollideResponseEnum kWC_Default/StealInventory/FloatAway) | 0:794 1:76 2:2 3:2 |
- Stats: MeshSet non-empty 149; NumColors 0..5; spiral 55; wind 51; underwater 8; attached-to-land 5; attractor never active; chained ParticleFX 98, ExpireFX 11, SoundFX 152. Top sprite sets: none(182), Particle.WXSprite4 (176), WXSprite1 (85), WhitePuff (50), ElectricSpark (37), WXSprite7 (29).
- Classes (rtti, td / vtable): ParticleHandlerService td 009202f8 vt 008612dc (Service; functions 005bfde0 [kill-all-emitters msg], 005c0080, 005c02c0, 005c09d0, 005c0d30, 005c0fd0 + more; creation 004eba10 "Create CLSID_ParticleHandlerService"; IsParticleEffectLogical callers 0057fcc0, 005a1fd0; kInvalidEmitterHandle). ParticleEmitterBase vt 00860870; ParticleEmitterEffectEntity vt 00860a6c/00860a84 (fns 005ba740..005bb350); ParticleEmitterGraphicEntity vt 00860db0 (005bd310, 005bd960); ParticleEmitterLogicEntity vt 00860fc4/00860fdc (005be0a0..005beb70); SnowParticleEmitterEntity vt 00820534; CParticle vt 00860734 (Particle.cpp 005b6f00..005b73d0); CParticleLandCollider vt 00861424, CParticleAttachedLandCollider vt 008607bc (005c1d50, 005c1e20, 005c2040); ParticleClass<T> template (ParticleClassInterface vt 00860810); ParticleColliderEmitterContainer vt 00877158; ParticleEmitterContainer vt 0087502c; EffectDetailsContainer vt 008772e4; ParticleMeshNamesContainer vt 00877300; XParticleSet vt 0088f058.

### 4. Water / sky / shadow / landscape classes (rtti td, vtable; fns = refs to source-file string)
| Class | vtable | Notes |
|---|---|---|
| WaterCgGraphicEntity | 00820990 | WaterCgGraphicEntity.cpp; fns 0048b460, 0048bc00, 0048c9f0; loads WaterVertexMain/Lighting/FragmentMain |
| WaterPlaneTweaks | 00877dd4 | XContainer: Glint/Shadow/Blend/Detail/ExtraGlint x {Centre,Inner,Middle,Outer,Rim}Color (rgba8) + GlintResource, ShadowResource, SkyBlendResource, DetailResource (str) |
| XWaterShader | 0089214c | XShader subclass |
| EFMV_RaiseWaterEventContainer | 00881f94 | cutscene water rise |
| SkyBoxEntity | 008203a4 | SkyBoxEntity.cpp fns 004857a0, 00485bb0, 00485de0; bins Skybox1-3 |
| XCloudShader | 0089216c | XShader subclass |
| LensFlareGraphicEntity / LensFlareContainer / LensFlareElementContainer | 0081cf34 / 00877ccc / 008745c4 | LensElementType kLens_SunGlow, Circle, FadedRing, Ring, FadedHex, Hex, RainbowRing |
| RainGraphicEntity / RainAudioEntity | 0081d6b4 / 0081d59c | weather |
| XFog, XLight, XLightGrid, XLightScope, XLightingEnable, XOglSpotLight | 008923f0, 0088eb44, 0088f860, 0088f6a8, 008923d0, 0088ebb4 | scene-graph lighting |
| LandscapeGraphicEntity / LandscapeLogicEntity | 0081c654 / 0081c810 | fns 0046fbb0, 00470b80 |
| LandChunk / LandFrame / LandFrameStore / LandTemplate | 00873424 / 008735c8 / 00873b24 / 00873c00 | landscape scene nodes |
| GenerateLandGeometry | 0081b174 | GLG_*.cpp: GLG_PC (004513e0, 00451ba0, 004520c0, 00454260), GLG_Shadow (00454e30, "VerticesToShadow < 0xfff0", shadow index cache), GLG_FringeBuilder |
| (heightmap shadowing) | - | "Shadowing heightmap..." in 00478780, 00482bd0 |
| XOglShaderManager, XOglContext, XOglRenderSurface, XOglTextureMap, XRenderManagerImpl<OpenGLImpl> | 008b3f10, 008b3654, 008b3fc4, 00892058, 00897a1c | GL backend |
| XBloomShape, XFocusBlurShape, XBlurEffect | -, -, 0088eb0c | exist in XOM scene graph; no matching CG shader (assumed unused on PC) |

## 9. Data/ index
Root: steamapps/common/WormsXHD. Counts are files directly in the folder.

### Top level (data)
| file | content |
|---|---|
| Default.cfg | `/RUN:WormsX /W:1024 /H:768`, `/STARTMENU:WXFE.MainMenu`, `/GAMEOVERMENU:WXFE.MainMenu`, `/RUMBLEMIN:0.5 /RUMBLEMAX:1.0`, `/CONFIG:local.cfg` (chained cmdline-style config) |
| local.cfg | `/W:1920 /H:1080 /REFRESH:60 /SSAA:1 /SHADOWMAP:1024 /CONFIG:user.cfg` (user.cfg absent) |
| lang_eng | 16 B text `lang_placeholder` |
| installscript.vdf | Steam install: vcredist, DX Jun2010, RegVideoDLL.exe (registers MemoryBufferedFilter.dll, a DirectShow filter for WMV); registry key `Team17 Software Ltd.\WormsUltimateMayhem` |
| WormsMayhem.exe 5.7MB, Launcher.exe | game exe (holds XOM schema, see tools/w4m-re/pe.py) |
| cg.dll, cgGL.dll, glut32.dll | NVIDIA Cg runtime + GLUT: renderer is OpenGL+Cg |
| fmodex.dll, fmod_event.dll | FMOD Ex + Event system (Data/Audio/PC .fev/.fsb) |
| XOM0-<host>.log | engine log written at run (12.8 KB) |

### Tweak/*.XOM (38 files, 2.1 MB) — dominant types (count)
| file | dominant types | role |
|---|---|---|
| AITWK | AIParametersContainer 24 | CPU AI difficulty params |
| CAMTWK | XFloatRD 50, ChaseCameraPropertiesContainer 6, SimpleCameraContainer 5 | in-game cameras |
| DEFSAVE | WXFE_UnlockableItem 469, CachedLeaderBoardWrite 104, InputEventMappingContainer 80, HighScoreData 51 | default save/profile, controls |
| EMPTY | (none, 257 B) | empty databank |
| HUDTWK | XFloatRD 66, XVectorRD 48, XColorRD 17 | HUD layout/colours |
| LOCAL | WeaponSettingsData 1145, FlagDataContainer 91, SchemeData | schemes (19), flags |
| LVLSETUP | WeaponSettingsData 58, LensFlareElementContainer 26, WeaponFactoryContainer 8 | default level setup, lens flare |
| MENUTWK | XColorRD 4 | menu colours |
| MENUTWKX | WXFE_GapColumns 568, ListBoxRows 495, ImageViewDesc 210 | main frontend menu descriptions |
| MENUTWKX2 | WXFE_GapColumns 151, ListBoxRows 140 | extra menus |
| MENUTWKXINGAME | WXFE_ListBoxRows 373 ... | in-game (pause) menus |
| MENUTWKXNET | WXFE_* 220/168/153/125 | network menus |
| MENUTWKX{PC,PS2,PS3,XBOX}COMMON | WXFE_MeshObjectParticleDesc 73, TextBoxDesc, ScrollingTextDesc | per-platform title scene/menus (PC one used) |
| MENUTWKX{PCUSA,PCPOLISH,PS2USA} | ~15 WXFE_* | region overrides |
| MENUTWKX{PCEURO,PS2EURO,JAMES} | empty 257 B | |
| MENUTWKXSTEVE / XBOX | 1-16 containers | dev / Xbox leftovers |
| OVERRIDEPARTTWK | ParticleEmitterContainer 11 | particle overrides |
| PARTTWK | ParticleEmitterContainer 874, EffectDetailsContainer 201 | all particle FX |
| PARTTWKPS2 | ParticleEmitterContainer 481, EffectDetails 110 | PS2 variant |
| PERSIST | WXFE_* + FMVTextLine 99 | persistent menus, FMV subtitles |
| PERSISTPC/PS3/XBOX/NET | WXFE_* | per-platform persistent menus |
| SCRIPTS | WXFE_LevelDetails 278, LevelDetails 1 | level list (docs/w4m-formats.md Missions) |
| STATS | CachedLeaderBoardWrite 104, Achievements* | stats/achievements |
| TWEAK | XFloatRD 92, WaterPlaneTweaks 34 | game physics, water, lighting |
| WEAPTWK | PayloadWeaponPropertiesContainer 23, BaseWeaponContainer 15 | weapon tweaks |
| WORMACTING | EFMV_WormLookAt 988, Emote 630, Track 479, PlayAnimation 367 | shared cutscene acting events |
| WORMATTACHMENTS | WXFE_AttachmentOffsets 1175, WXFE_WormAttachments 172 | hat/glove attachment offsets |
(RD = ResourceDetails)

Tweak tooling: all decode exactly with `tools/w4m-re/xom.py` / `tweak.py` (except PERSIST `WXFE_SoftwareKeyBoardData`). Documented: WEAPTWK, TWEAK, LOCAL, SCRIPTS, CAMTWK, PARTTWK, MENUTWKX*COMMON, WORMACTING (docs/w4m-formats.md, camera-w4m.md, worm-reactions.md, death-sequence.md). **Not referenced anywhere in repo: AITWK, HUDTWK, LVLSETUP, DEFSAVE, STATS, WORMATTACHMENTS** (PERSIST only in w4m-re README).

### Directory index
Conf: D = verified from bytes/types this pass, A = assumed from names.
| dir | files | ext | size | content / format | repo handling | conf |
|---|---|---|---|---|---|---|
| / | 15 | exe 3, dll 7, cfg 2, vdf, log | 13.3 MB | see top level table | pe.py (exe schema) | D |
| CG/ | 6 | .cg 5, .h 1 | 0.1 MB | Cg shader source: FixedFunction, Landscape, PostProcess, water, XenonVideo, Fxaa3_9.h | docs (water.cg, Landscape.cg), camera-w4m.md | D |
| Redist/ + directx/ | 1 + 157 | exe, cab 154 | 4.3 + 98 MB | vcredist, DirectX Jun2010 | none needed | D |
| EFMV/<Cutscene>/ | 36 dirs × 1 | LIP.txt | ~0 | lip-sync per cutscene line: `#<hash> <Line>.txt` then `frame,phoneme,<none>` (phonemes A, CONS, EI, L, O, MBP, Rest...) | **none** (only speech/ LIP.txt used) | D |
| speech/vo<bank>/ | 33 dirs × 1 | LIP.txt | ~0 | same format; hash -> line name for vo banks | w4m-import (docs/import.md) | D |
| Data/ (root) | 208 | .XOM | 1.7 MB | level databanks (`MOIK`), see families below | w4m-maps main.rs/mission.rs | D |
| Data/Bundles | 475 | .xom | 627.5 MB | `MOIK` XOM: meshes/skins/anims/XImage textures, see ranges | w4m-models, w4m-ui, w4m-maps (textures/decor) | D |
| Data/Maps | 936 | xan 222, hmp 192, csh 476, txt 46 | 257.7 MB | 241 stems. .xan `MOIK` landscape; .hmp raw f32+u8 (no magic); DAY/EVENING/NIGHT .csh cache (no magic); .txt material override | w4m-maps (xan/hmp/txt); csh ignored | D |
| Data/Tweak | 38 | .XOM | 2.1 MB | see table | xom.py, tweak.py, w4m-maps | D |
| Data/scripts | 145 | .lub | 1.0 MB | Lua 5.0 bytecode `\x1bLuaP` 01 04 04 04 06 08 09 09 04 (stdlib + per-level) | w4m-maps lua.rs, w4m-re/lua.py | D |
| Data/Language/PC | 44 | .xom | 4.6 MB | 11 langs (Ame Cze Eng Fre Ger Ita Jap Pol Rus Slo Spa) × 4: `<Lang>.xom` (in-game ~2.2k strings), `<L>FE` (~1.07k frontend), `<L>LS` (~1.57k, level/story text), `<L>Loading` (~41 loading tips); only XStringResourceDetails + XDataBank | w4m-maps mission.rs, w4m-ui (lang) | D (role of LS/Loading: A) |
| Data/Themes/Theme* | 11 dirs, 1-3 each | .txt | ~0 | `Theme<X>.txt` (material file: texture codes W01...), `<L> Detail List.txt` (`NN Name`), `<L> Texture List.txt`; Arctic/War/England have only theme txt | w4m-maps | D |
| Data/Themes/Custom/Bank01-10 | 1-3 each | .txt | ~0 | texture lists + per-map notes (e.g. Bank07 `Crate Britain.txt`) | w4m-maps (Themes) | D |
| Data/Frontend/Levels | 181 | .tga | 44.9 MB | 32-bit TGA level previews 256² | w4m-ui | D |
| Data/Frontend/Gallery | 109 | .tga | 55.1 MB | gallery/concept art TGAs | **none** | A (content) |
| Data/Frontend/mechanics | 7 | .tga | 35.6 MB | LoadBack* 1920×1080 backgrounds | w4m-ui | D |
| Data/HUD/Weapons | 42 | .tga | 10.5 MB | weapon icons 256² | w4m-ui | D |
| Data/HUD/Flags | 95 | .tga | 4.5 MB | team flags | w4m-ui | D |
| Data/Audio/PC | 107 | fsb 106, fev 1 | 297.7 MB | `FSB4` banks (mp2, voRussian PCM16): per-level/cutscene (story, Challenge*, Deathmatch*, Multi*, Outtake*, Tutorial*), `mu*` 13 music, `vo*` 33 voices, ambient, frontendmusic, frontendsfx, global, weapons, Cheer, Failures, Demo; `WormsX.fev` = `FEV1` event project | w4m-import (fsb); fev only in docs | D |
| Data/Audio/Speech | 33 | .lsd | 0.1 MB | text: `<name>Speech/<bank>/<Category></name>` + line hashes, `;` separated | w4m-import | D |
| Data/Audio/EFMV | 27 | .lsd | ~0 | same, `EFMV/<Cutscene>/<Line>` -> hash (narrator lines) | **none** | D |
| Data/FMV/ntsc | 22 (+6 English, +1 Logos) | .wmv | 80.8 + 320.2 + 11.1 MB | ASF/WMV (`30 26 b2 75`): Outtake* + OuttakeRecordingBooth_01-16; English/ Arabian, Camelot, Jurassic, WildWest, Welcome, Meet_The_Professor; Logos/Team17NTSC | **none** | D |
| Data/{Audio,Frontend,HUD,FMV,Language,Themes} | 0 | – | – | containers only | – | D |

### Data/*.XOM families (208)
| family | n | XOM types (sum, files) | role |
|---|---|---|---|
| `*-W3D.XOM` | 46 | XStringRD 140, XUintRD 92, XDataBank 46 only | Worms 3D-era map databank: `Databank.CustomDetailBank/CustomTextureBank/MaterialFile/Theme/TimeOfDay`, no mission |
| `RANDOM<THEME>` | 5 | strings/uints only | random-landscape databanks (Arabian, Building, Camelot, Prehistoric, WildWest) |
| story levels (TUTORIAL1-3, DOOMCANYON...) | ~25 | EFMV_TrackContainer, EFMV_*Event, WormDataContainer, CrateDataContainer, EFMV_MovieContainer, XContainerRD | mission + in-engine cutscene |
| `LP_*` | 34 | EFMV_* events, WormData 193, CrateData 169 | 25 full, 9 with only 3-6 containers (string stubs pointing at base level); w4m-maps strips `LP_`/`SPLP_` to share base databank |
| `MULTI_*` | 30 | WormData 381, CrateData 290, ParticleEmitter 176, EFMV_* | multiplayer missions; 21 are 3-type stubs, 9 full (1001Fights, AllSystemsOhNo, HardDaysKnight, Maimframe, MonsterBash, PeskyLittleBreeders, TerminalFerocity, TimeFlies, ToweringInfernal) |
| `OUTTAKE*` | 14 | EFMV_PlayAnimation 438, LookAt 430, CutCamera 352 | outtake cutscenes |
| `DEATHMATCH1-11` | 11 | WeaponSettingsData 638 (58/file) + EFMV | deathmatch levels with own weapon set |
| `CHALLENGE*` | 15 | CrateData 246, EFMV_TimedPathCamera | challenges |
| MULTIMODE1-4, MULTIPLAYER, DEMO_* | 8 | mixed | mode setups / demo |
| tests (ANIMTEST, ARABIATEST, HUDTEST, MOVIETEST, WEAPONSTEST, LEVEL1, MANEL, BOOSHEET, unitcube) | 9 | small | dev leftovers |
Repo: `docs/w4m-formats.md` (Map files, Missions), `w4m-maps/src/mission.rs` (kinds 4/8/9, skips `-w3d`). EFMV_* containers in level files: not imported (only WORMACTING referenced). (A for roles of MULTIMODE/tests.)

### Bundles numbering (Bundl00-474, all `MOIK`)
Empty (178 B header only): 01, 35, 51-56, 62-67, 158, 167-171, 243, 249-251, 287, 312-314, 353, 375, 408, 429, 471.
| range | content (types) | source | conf |
|---|---|---|---|
| 00 | Intro.Loading, LoadBack.Small, HintPanel, reticle (XImage 5) | strings | D |
| 02 | legal/ESRB, FMOD logo | strings | D |
| 03 | FE.Font: 31 XMultiTexFontPage + 31 XImage (42 MB) | docs | D |
| 04 | HUD.PiP | strings | D |
| 05 | FE.LoadingIcon (round worm, 1 mesh + clip) | w4m-ui | D |
| 06 | frontend: WX.Mesh.Title, FRONTEND.Sky, menu meshes, 57 desc / 125 XImage | docs | D |
| 07 | 39 meshes, 53 images | – | A (FE/misc) |
| 08 | FE.ComboButton, DebugMarker | strings | D |
| 09 | in-game: weapons, projectiles, crates, mines, oil drum, sheep, HUD XImages (171 desc, 129 clip libs, 338 img) | docs | D |
| 10 | frontend menus, seagull, borders, ticker strip (25 desc) | docs | D |
| 11 | 51 meshes, 14 clips | – | A |
| 12 | Temp.Gallery placeholders | strings | D |
| 13-23 | theme landscape textures (64-75 XImage): Arabian, Building, Camelot, Prehistoric, Wildwest, Arctic, England, Horror, Lunar, Pirate, War | strings `ThemeX\X01.tga` | D |
| 24-34 | theme detail meshes (20 desc each), same theme order | `ThemeX/X_det_detailNN.xom` | D |
| 36-45 | Custom Bank01-10 detail meshes (5 desc) | `Custom/BANKnn` | D |
| 46-50 | story-theme prop libraries ARABIANP, BUILDINGP, CAMELOTP, PREHISTORICP, WILDWESTP (4-6 desc) | strings | D |
| 57-61 | same 5 themes, `*STRIP` variants (20-47 desc + images) | strings | D (purpose A) |
| 68-92 | 5 per theme (Arabian 68-72, Building 73-77, Camelot 78-82, Prehistoric 83-87, WildWest 88-92): `<Theme>StatueIslands.xan` + other .xan prefabs, 1-6 desc, no images | strings | D (grouping), A (role) |
| 93-125 | sky/water per theme × time (2 desc + 8-12 img) | docs (93-97,108-113 DAY; 98-107,114-125 EVENING/NIGHT), w4m-ui | D |
| 126-135 | Custom BANK01-10 textures (20 img) | strings | D |
| 136-166 | Custom W3D_BANK11-41 textures (10 img) | strings | D |
| 172-248 | Hat.<Name> (1 desc + 1 img) e.g. Hat.Afro 172, Baseball 182, Crown 201, Pigtails 212, AlienBreed 240 | w4m-models comment | D |
| 252-286 | Glasses.* (e.g. Monocle 258, SherlockPipe 284) | strings | D (bounds A) |
| 288-311 | Mustache.* (Afro 288, Small.Brown 303) | strings | D (bounds A) |
| 315-352 | Gloves.* skinned on 34-bone worm skeleton (XSkin 1-2) | docs, strings | D |
| 354-374 | Grave.* (Arabian 354 ... DLC1.Army 370) with clip libs | strings, w4m-models | D |
| 376-407 | Factory.* weapon-factory parts (BlasterGunBarrel, RayGunButt...) | strings | D |
| 409-470 | Factory.Proj.* projectiles (Ray, Classic, Tank, 8ball ... DLC1.Payload1-5 466-470) | strings | D (bounds A) |
| 472-473 | HUD.HealthBar/HealthIcon (472), 473 same size | w4m-ui (472) | D/A |
| 474 | W4.Worm (34 bones, 329 clips, 3 XSkin, 18 img), Nav icons | docs | D |
Factory.*/Gloves.*/Glasses/Mustache/Grave(partly): **not imported by any repo tool**.

### Gaps (not handled anywhere in repo)
EFMV/*/LIP.txt, Audio/EFMV/*.lsd, FMV/*.wmv, Frontend/Gallery, WormsX.fev (doc only), *.csh, AITWK/HUDTWK/LVLSETUP/DEFSAVE/STATS/WORMATTACHMENTS, EFMV_* containers in level XOMs, Factory/Glasses/Mustache/Gloves bundles.

## 10. How to search (Comment chercher)

| Question | Recipe |
|---|---|
| Who reads data key X? | `xref.py --callers 'Worm.Drown.HeightOffset'`. The first wrapper called after the site (`0x50b7f0` float by value, `0x50bac0` float cached handle…, §3) gives the type. For a cached handle, follow the `[this+off]` member. The default value: `tweak.py -g 'Drown'`. Scripts: `lua.py --all 'Drown'`. |
| Who sends or handles message M? | `pe.py str '^GameLogic\.Turn\.Ended$'` gives the string. `xref.py` on it finds the static handle(s), built by `push name; mov ecx, G; call 0x68bb9f`. `xref.py G` then shows the users: `0x690e3e` subscribes, `0x68bcce` tests inside a HandleMessage, `0x6910e4` sends. |
| Disassemble the handler of message M | `pe.py rtti '^ClassName$'` gives the vtable. Slot 7 (`pe.py words VTABLE+0x1c 1`) is HandleMessage. `disasm.py` it and look for the `0x68bcce` test against M's handle. |
| Dump container Y | `xom.py list Tweak/CAMTWK.XOM Track`, then `xom.py dump Tweak/CAMTWK.XOM PayloadTrackCamera`. For WEAPTWK: `xom.py dump Tweak/WEAPTWK.XOM kWeaponBazooka`. |
| Field names and offsets of a struct | `pe.py schema '^PayloadWeaponPropertiesContainer$'`. The struct offset is where the code reads it (`[reg+0xd0]` = CameraId). |
| Who reads a struct field? | Grep the disassembly of the class's functions for `+0xOFF`. A quick start: `disasm.py` on its vtable slots. |
| Find a class's code | `pe.py rtti NAME` (vtable), `pe.py str 'Name\.cpp$'` (assert string), then `xref.py` on that string's VA. |
| What does a .lub do? | `lua.py FILE.lub` for the functions and constants, `--code` for the pseudo-code. |
| Value of tweak T | `tweak.py -g '^Camera\.Orbit'`. Full JSON in `~/.cache/w4m-re/tweaks/`. |
| An FMOD event | Event path strings `group/event` in the exe (`pe.py str '^weapons/'`). `PlaySound` is 0x604a20 (§7). |

## 11. Not covered

- **Worms:** the slide, ballistic and passive physics handlers were not opened. Acting and animation states (`WXActor`, `WormLogicAnimState`) are only touched.
- **Audio:** the per-event layer data in `WormsX.fev` is not decoded; events are tied to banks by name.
- **Rendering:** the enum value order of ParticleRenderScene, LandCollide and WormCollide is assumed. Bloom and blur classes have no PC shader.
- **Turn and death queue:** the units of the scheme's `LandTime` / `HotSeat`, and the death-queue timeout (+0x210).
- **Bundles:** `Bundl*.xom` are not decoded by `xom.py`.
- **Not explored:** network/online, frontend menus (`WXFE_*`), AI internals (`AIService`, AITWK fields are decodable with `xom.py`), EFMV cutscene playback.

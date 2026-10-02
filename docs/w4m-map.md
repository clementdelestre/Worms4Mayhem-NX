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
| who uses struct field +OFF | `xref.py --field 0x210 [--in FUNC_VA...]` |
| message handle <-> name | `pe.py msg 0x97a7c0`, `pe.py msg '^FE\.ChangeMenu$'` (one handle per source unit) |
| FMOD event parameters (loop, volume, 3D) | `fev.py -g '^weapons/Fuse'`, `fev.py --json` |
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

**Coverage.** `xom.py check` decodes every container to its exact end in all 38 `Tweak/*.XOM` (`PERSIST.XOM` included), the level databanks, the language files and all 475 `Bundl*.xom`. Bundles contain untagged containers and optional fields, which `xom.py` handles by walking containers in type order (§15); their geometry and animations are still extracted by `tools/w4m-models`.

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
Container class/camera/damage: data. Payload logic class: **traced**, chosen by weapon id then by container *name*, not by container class (see §13). Corrections to the earlier assumption: SuperSheep launches as a Jumping payload and becomes Flying on fire-press; StealInventory (value 1) is OldWoman, FloatAway (2) is Scouser; AdjustableBounceWeaponLogicEntity is never created.
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
1. If entity+0x54==0 (not pData+0x54, see §11) -> Passive(6). If 0x5ac390 -> UpdatePassive. If no move input (0x5ab3d0 false) -> idle branch: if standing surface id (ent+0xE8, 0xFFFF=none) no longer valid -> Ballistic(2); else zero vel, facing/anim upkeep.
2. disp = vel(pData+0x68)*20*k (k from 0x69db80, /0.004); cand = pos(pData+0x38)+disp.
   Facing (0x5b1040-0x5b107c): if |InputImpulse| != 0, Orientation (pData+0x8c) = (0, 0x519120(InputImpulse), 0), the yaw of the camera-relative input (0x5ab3d0): an instant snap, no turn rate. The worm moves along the input, not along its old facing.
3. Ground probe: CastRays(cand + (0,20,0), down, len 200, mask 0xF = 4 foot points). d = 20 - nearestDist = height of HIGHEST of the 4 hits relative to foot. Miss -> d about -181.
4. d > 20: blocked, no move (return).
5. 5 < d <= 20 (ledge higher than hardcoded 5.0 @0x8244fc = TWEAK StepUpHeight, which the exe never reads): cand = nearest hit point +0.1y; ground normal from rays; sphere resolve (0x519ed0) may replace cand; require 0x4adda0 walkable and Fits(cand); then store old pos (ent+0xEC), target (ent+0x104), timer 250 (ent+0x110), pos unchanged -> Vaulting(4) ("WormShouldNotSlipOnLand ->Vaulting").
6. -5 <= d <= 5 (normal step, @0x5b154a): cand = nearest hit +0.1y; sphere resolve. If sphere contact: n.y < -0.1 -> blocked; n.y <= 0.8 -> cand -= 15*n, cand.y += 10, resolve again, Fits -> Vaulting ("ResolveSphericalCollisions ->Vaulting"). No contact: walkable check, then Fits(cand) retried raising y by +1.0: y..y+4 accepted, y+5 rejected (§11 push-out) -> set pos; if ground n.y < cos(SlideAngle[mat]) -> Sliding(3).
7. d < -5 (drop/miss): cand.y = y-5, 0x51a510 sphere sweep; if contact and Fits -> place. Else Fits(cand at old y) -> Fall() 0x5acba0 (Ballistic). Else raise y by +1..+5 (y+6 rejected, §11 push-out) then place, else blocked.

### Vaulting 0x5aca80 (disasm)
See §11 "Vaulting 0x5aca80" for the full trace (start, abort, end, inputs).

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
- Message IDs: per-TU static objects, init funcs 0x7dxxxx-0x7fxxxx = `push "Name"; mov ecx,G; call 0x68bb9f` (register). Dispatcher compares msg against G via 0x68bcce. Name of a handle: `pe.py msg 0xVA`; all handles of a name: `pe.py msg '^Name$'` (section 20).

### 6. Exe: who handles what [disasm, class names from assert strings]
- TimerLogicEntity (.\TimerLogicEntity.cpp): 0x50f980 HandleMessage (Timer.StartGame/StartTurn/EndTurn/StartHotSeatTimer/StartPostActivity/StartRetreatTimer/EndRetreatTimer; asserts iRoundTime in {0,-1}..10000?, iTurnTime<=10000 -> times in game units checked vs. ms/??; strings "TimerLogicEntity MsgStartTurn", "hot seat canceled", Turn.Boring, Turn.PayloadFired). 0x50f100 tick: posts Timer.HotSeatTimedOut ("hot seat timed out"), Timer.TurnTimedOut, Timer.PostActivityTimedOut, Timer.RetreatTimedOut. 0x50f4e0 binds data keys to members: RoundTime, TurnTime, HotSeatTime(+0x7c), PostActivityTime(+0x80), RetreatTime(+0x84), RoundTimeRemaining, TurnTimeRemaining, HotSeatTimeRemaining, PostActivityTimeRemaining, RetreatTimeRemaining, ClockDisplayMode, ElapsedRoundTime.
- Defaults (Data/Tweak/LOCAL.XOM, XIntResourceDetails, ms) [data]: TurnTime 45000, RoundTime 1800000, HotSeatTime 10000, PostActivityTime 2400, DefaultRetreatTime 3000, RetreatTime 0. stdvs overrides from scheme (DefaultRetreatTime<-LandTime, HotSeatTime<-HotSeat, TurnTime, RoundTime). Challenges set RetreatTime/DefaultRetreatTime/PostActivityTime 0; Wormpot NoRetreatTime -> DefaultRetreatTime=RetreatTime=0.
- Retreat time per weapon [disasm]: on fire, weapon logic entities do r=GetData("DefaultRetreatTime"); if props.RetreatTimeOverride (i32 @+0x50 in weapon properties, schema field #0x10) >= 0 then r=override; SetData("RetreatTime", r). Seen in GunWeaponLogicEntity 0x55cff0, MeleeWeaponLogicEntity 0x568860, NewSentrygunWeaponLogicEntity 0x56e6d0, PayloadWeaponLogicEntity 0x582d70; AI planner 0x4a1cf0 reads DefaultRetreatTime. FloodLogicEntity 0x555580 / FloodWeaponLogicEntity 0x555d30 and 0x588160/0x58ba00 write RetreatTime directly. Timer.StartRetreatTimer senders: TUs at 0x5210b0, 0x549bb0 + several weapon TUs (msgobjs 0x95c6ac,0x95d04c,0x95d964,0x95e094,0x95ea20). Front-end option "NoRetreatTime" (FE.WP.NoRetreatTime, 0x5d5830).
- GameLogicService (.\GameLogicService.cpp): 0x4fdc90 HandleMessage (WormSelect.WeaponSelected/OptionSelected, GameLogic.AddMeToDeathQueue, Worm.Died, GameLogic.GunWaiting, Turn.Started/Ended, MsgActivateSuddenDeath, MsgTurnEnded, inventory, telepads, briefing box); 0x4f7d80 = subscribe/init (wind, mines, SuddenDamageMode, WormPot, GoodShotDamageThreshold, MaxRandomCrates). 0x4fb880 HandleEndOfGame (Win/Draw -> GameOver menus, rounds, stockpile, mission/challenge records).
- Death queue [disasm]: AddMeToDeathQueue handler 0x4fac70 pushes worm id onto vector at GameLogicService+0x1fc. Tick 0x4fa2c0 calls 0x4f9b30 every frame: if queue non-empty and (ActiveObjectRegistrationService count (0x4d3960) == queue size, OR `GameLogic.SuddenDamageMode` (+0x210 is that key's handle, default 0, never written: there is no timeout, see §14), OR flag+0x1b1 set and count <= size+2): clear flag, pop FRONT id, post Worm.TimeToDie to that entity. One worm per pass -> deaths are sequential (each dying worm is an active object until done). Flag +0x1b1 set by message GameLogic.GunWaiting (gun weapons waiting for input tolerate 2 extra active objects) [assumed meaning].
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
Superseded by the full layout in §12 (`fev.py`); kept for the bank and group notes.
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
  | ParticleLandCollideType | LandCollideEnum | kLC_None 0, kLC_Bounce 1, kLC_Expire 2, kLC_StopMoving 3, kLC_StopMovingAndAttach 4 (order data: `pe.py schema` enum list) | 0:666 1:202 2:2 3:4 |
  | ParticleCollisionWormType | WormCollideEnum | kWC_Standard, kWC_Expire, kWC_Lightside, kWC_Darkside (+ WormCollideResponseEnum kWC_Default/StealInventory/FloatAway) | 0:794 1:76 2:2 3:2 |
- Stats: MeshSet non-empty 149; NumColors 0..5; spiral 55; wind 51; underwater 8; attached-to-land 5; attractor never active; chained ParticleFX 98, ExpireFX 11, SoundFX 152. Top sprite sets: none(182), Particle.WXSprite4 (176), WXSprite1 (85), WhitePuff (50), ElectricSpark (37), WXSprite7 (29).
- **Particle maths** (Particle.cpp, CParticle vtable 0x860734) [disasm]: t = particle age in ms. Setup 0x5b98e0 (r uniform in [0,1)): v0 = (V + (2r−1)·Vrand)·0.01 units/ms (V is per 100 ms; IsNormalised: unit vector × (V.x + Vrand.x)); a = (A + (2r−1)·Arand)·1e-4 units/ms². Position 0x5b7450: p = p0 + v0·t + 0.5·Mass·(a + wind)·t² (Mass scales the acceleration; wind only with IsEffectedByWind). IsAlternateAcceleration: no gravity, p = p0 + v0·(N − 1/(S·t + 1/N)) (N, S = AlternateAccelerationN/S), plus v0.y·t on y. Size 0x5b6f40: fade-in, then S until SizeVelocityDelay, then linear to S·FinalSizeScale, or with FinalSizeScale 0 and SizeVelocity < 0 a linear shrink to 0 at end of life. Alpha 0x5b7660: AlphaVelocity < 0 = linear fade to 0 at end of life (magnitude ignored). Colour 0x5b77c0: NumColors 0 white, 1 Color[0], 2 lerp over life, ≥3 piecewise by ColorBand. Emitter: SpawnFreq = period in ms (0 = one burst), StartDelay ms, pool capped by MaxParticles. kTrail (3) makes one TrailGraphicEntity ribbon per particle (0x5c2580, 24 divisions, "A,B" sprite set = ribbon texture, head sprite [assumed]).
- **Sprite set blends** (Bundl10: descriptor → XGroup → XShape → shader render states) [data]: WXSprite1 and Whiteout additive (SrcAlpha, One; #1232); WXSprite4/5/7/26/30 and Fade alpha (#1231); TrailSprite_B/R/W alpha (#1230), but those images have no alpha channel (black ground).
- **Victory fireworks** (GameOverLogicEntity update 0x4ff8d0, every 20 ms) [disasm]: 4000 ms after the end, the orbit camera starts (0x4ff790; Script.NoOrbitCamera skips the fireworks). Then, for 5000 ms (15000 when 0x5a6350), each tick fires with chance 1/40 (one per 800 ms on average) `WXPF_Firework<1 + rand%5>` at x, z = Land.Center ± Land.Radius/2, y = Land.MaxHeight + r·30 units (0x5c1410). Any input ends the show. Compositions (PARTTWK): 1 cyan glows + StarburstTrailsA/B + BlueTrails_2; 2 orange glows + 2×RedTrails_1 + Exploder_1; 3 orange glows + Exploder_1/2 + delayed 10 m glows + delayed whiteout; 4 green glows + ExploderGreen (GreenTrail1 child puffs); 5 orange glows + 4×RedTrailsLong. Each also has WXPF_Whiteout (400×300 units, alpha 0.1, 80 ms). Ours: `client/src/fx.cpp` `firework()`, placed the same way (Land.Radius = half the map width, as the orbit camera).
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
EFMV/*/LIP.txt, Audio/EFMV/*.lsd, FMV/*.wmv, Frontend/Gallery, *.csh, HUDTWK/LVLSETUP/DEFSAVE/STATS/WORMATTACHMENTS, EFMV_* containers in level XOMs, Factory/Glasses/Mustache/Gloves bundles. Documented but not imported: WormsX.fev (§12, `fev.py`), AITWK (§18), EFMV/WORMACTING (§19).

## 10. Tactical view and targeting cursor (Airstrike, Donkey, Homing...)

Confidence: **data** (tweak/text/strings), **disasm**, **assumed**. VAs in `WormsMayhem.exe`, base 0x400000. Units: W4M world units; logic tick = 20 ms.

### Summary

- There is **no dedicated targeting camera** and **no free 2D cursor in world space**. The targeting view is the ordinary **Blimp camera** (`IsometricCam`, logical view 3). The player enters it with the Blimp key. The cursor is a reticle fixed at the **screen centre**. The player moves the camera, not the cursor (disasm + data).
- Each frame, the camera manager casts a ray from the camera position through its look-at point, against the land, the water plane and worms. The hit point is the target. This writes `Airstrike.TargetPoint`, `Airstrike.Direction`, `Airstrike.UpVector`, `Airstrike.HasTarget` and `Airstrike.WaterTarget` (disasm).
- On Fire, the weapon copies `Airstrike.TargetPoint` into `Payload.Target` and broadcasts `HUD.Target.Selected`. The airstrike's direction is the camera's horizontal right vector, so the planes fly across the screen. There is no separate left/right choice (disasm).
- In-game help text (data, English.xom): "Incoming! Define the path using [Blimp] and launch using [Fire]" (Airstrike / Super Airstrike); "Fat men can fly! Define the path using [Blimp]…" (Fatkins); "Find your target using [Blimp], aim the launcher then [Fire] to power up" (Homing); "Target the area using [Blimp] and call in E.T. with [Fire]" (Alien Abduction). `FETXT.Control.Blimp` = "E" for both keyboard and joypad (LOCAL).

### Weapons that use it (data, WEAPTWK)

| weapon | IsTargeting | IsBomber | IsControlledBomber | IsHoming | other | CameraId | LaunchDelay | cursor |
|---|---|---|---|---|---|---|---|---|
| kWeaponAirstrike | 1 | 1 | 0 | 0 | EndTurnImmediate 1 | (none) | 200 | Bomber |
| kWeaponSuperAirstrike (Cow.Payload, NumStrikeBombs 3) | 1 | 1 | 1 | 0 | EndTurnImmediate 1 | (none) | 200 | Bomber, then SuperBomber |
| kWeaponFatkins (NumStrikeBombs 1) | 1 | 1 | 0 | 0 | | FatkinsTrackCamera | 200 | Bomber |
| kWeaponConcreteDonkey | 1 | 0 | 0 | 0 | | DonkeyTrackCamera (not in CAMTWK; code uses `DonkeyCamera`) | 200 | Targeting |
| kWeaponHomingMissile / kWeaponFactoryHoming | 1 | 0 | 0 | 1 | Aimed + Powered, WXAnimTargetSelected `AimLockHomingMissile` | HomingMissileFlyCamera | 0 | Homing + LockOn |
| kUtilityTeleport | BaseWeaponContainer, no targeting flags | | | | | | 0 | (dead code, see below) |

The cursor is chosen in `PayloadWeaponLogicEntity` init, 0x582d70 (from 0x583017). If IsTargetingWeapon (+0x1c9): weapon state `this+0x38` = 1 (targeting), then IsHoming (+0x1cd) gives Homing, else IsBomberWeapon (+0x1cb) gives Bomber, else Targeting (disasm).

### Camera: Blimp (IsometricCam, vtable 0x855440, `.\IsometricCam.cpp`)

**Logical view ids (disasm).** They come from the Camera base ctor `0x51b570(name, type)`; the type is stored at camera +0x2c.

| id | camera |
|---|---|
| 1 | Head |
| 2 | Default (DefaultCam) and RayCam |
| **3** | **Blimp** |
| 4 | Orbit |
| 6 | FlyCam |
| 7 | FallCam |
| 9 | Jetpack |
| 0xe | Path, TimedPath |
| 0x11 | Ninja |
| 0x13 | Spectator |
| 0x15 | Simple (0x531c60, used by Donkey) |

`CMS::GetCurrentView 0x51cea0` returns 0x10 while a scene camera is followed, 0x14 while CMS+0x2b4 is set, 0x13 for spectator, else the current camera's +0x2c.

**Entering and leaving (disasm).**
- Entering: `DefaultCam::PseudoHandleMessage 0x524e00`, on `Input.BlimpViewPressed`, calls `SetCamera("Blimp")` (0x524e82).
- Leaving: Blimp's own handler 0x529c00 (vtable slot 5), on `Input.BlimpViewPressed` again, calls `SetCamera("Default")` (toggle).
- The weapon panel (`WXWeaponPanelLogicEntity` 0x602ea0, at 0x6037bb) injects `Input.BlimpViewPressed` when it opens in Blimp view, which leaves Blimp.
- No code enters Blimp automatically for a human player. The AI calls `SetCamera("Blimp")` itself: 0x4b4c99, 0x4b5b89.
- Scripts can block Blimp with `Camera.Disable "Blimp"`. WormpotService does so at 0x5d6fea.

**Activate (0x529910).**
- `Input.EnableGroup` "Fire" and "CameraSelect".
- `Input.DisableGroup` "WormFirstPersonAiming", "WormAiming", "WormMoving", "WormRoping", "Flying".
- In view 3, it shows a HUD helper (0x61fe90, vtable slot 6); Deactivate (0x529bd0) hides it. The Blimp help lists come from `WXFE.HelpBlimpPC/Console` (data).

**Entry pose (vtable slot 4, 0x52ad20; disasm).**
- Reference point: the tracked entity's position (entity +0x38..0x40), or the previous camera position. Yaw comes from entity +0x90 or from the previous view direction (acos, mirrored to pi minus the angle when z < 0).
- pitch = `Camera.Blimp.DefaultPitch` (1.0 rad).
- focus.y = max(ref.y, `Land.MaxHeight`) + `HeightAboveLand` (6).
- The focus is moved back horizontally by d = (focus.y - ref.y) / tan(pitch): focus.x = ref.x - sin(yaw)·d, focus.z = ref.z - cos(yaw)·d. The ray through the focus therefore hits the reference point at screen centre.
- zoom = 1.

**Camera position (0x52a0a0).** position = focus + R(pitch, yaw, 0)·(0, 0, -`StickLength` 500). The look-at is the focus point. The up vector is R·(global up at 0x91ea50). The assert `m_TargetLookAt != m_TargetPosition` (disasm).

**Per-tick update (0x52af40, then 0x52a5e0; dt = 0.02 s constant at 0x854604, passed by CMS 0x51da9a; disasm).** Input is the CMS struct at +0x210:

| bit / field | from message |
|---|---|
| byte 0: 0x01 | Left |
| byte 0: 0x02 | Right |
| byte 0: 0x04 | Forward |
| byte 0: 0x08 | Back |
| byte 0: 0x10 | RotateLeft |
| byte 0: 0x20 | RotateRight |
| byte 0: 0x40 | RotateUp |
| byte 0: 0x80 | RotateDown |
| byte 1: 0x02 | MouseMiddle held |
| +0x08 / +0x0c | mouse dx / dy accumulators |
| +0x10 | wheel |
| +0x19 | ZoomIn held |
| +0x1a | ZoomOut held |

These are set by CMS HandleMessage 0x522710, from `Camera.*Pressed/Released`, `Camera.MouseMoved`, `Camera.MouseZoom`, `Camera.MouseMiddle*` and `Camera.ZoomIn/Out*` (0x522e77..0x523381).

| quantity | rule | tweak (CAMTWK, data) |
|---|---|---|
| zoom (camera +0x5c) | ZoomIn: z *= ZoomSpeed; ZoomOut: z /= ZoomSpeed; z -= MouseZoomSpeed·wheel; clamp [MinZoom, MaxZoom] | ZoomSpeed 0.99, MouseZoomSpeed 0.08, MinZoom 0.15, MaxZoom 2.0 |
| s (rotation speed scale) | 0.9 + 0.1·zoom | |
| yaw (+0x68) | RotateLeft -= RotateSpeed·dt·s, RotateRight +=; with middle mouse: yaw -= MouseYawSpeed·dx | RotateSpeed 0.55 rad/s, MouseYawSpeed 0.001 |
| pitch (+0x64) | RotateDown += PitchSpeed·dt·s, RotateUp -=; with middle mouse: pitch -= MousePitchSpeed·dy; **clamp [0, pi/2]** | PitchSpeed 0.45, MousePitchSpeed 0.001 |
| focus (+0x78), forward/back | ± f·MoveSpeed·dt·zoom, f = (sin yaw, 0, cos yaw) (Back adds, Forward subtracts) | MoveSpeed 250 u/s |
| focus, strafe | ± right·MoveSpeed·dt·zoom, right ⟂ f in the horizontal plane | |
| focus, mouse pan (middle mouse not held) | focus -= f·MouseYSpeed·dy; focus += right·MouseXSpeed·dx; no dt, no zoom factor | MouseXSpeed 1.5, MouseYSpeed 1.5 |
| bounds | if \|focus - Land.Center\| > 4500, focus = Land.Center + normalize(focus - Land.Center)·4500 (3D sphere; constant 4500 at 0x8556a0) | |
| focus.y | not changed by movement: it keeps the entry height (disasm: f.y is an explicit 0·k; right = f × global up at 0x91ea50, horizontal if that up is (0,1,0), assumed) | |

- Zoom does not change StickLength. The camera's +0x5c factor is applied to the graphical projection in CMS 0x51da00 (0x51e071; FOV scale, assumed).
- Unused or unclear: `Camera.Blimp.UpdateSpeed` 0.05 (asserted 0 < x <= 1, not used in this update), `MouseWheelSpeed` 0.1 (+0xac, not used here).
- `Camera.Manual.*` (FastScale/Move/Rotate/Time.Speed = 100) belong to the debug ManualCam, not to targeting (assumed).
- `Airstrike.MaxDistance` 1500 (WEAPTWK) is bound to CMS+0x22c but only released, never read (0x520409): unused (disasm).

### Target computation: `CMS::UpdateTargetInfo 0x51c910(bool ninja, bool teleport)` (disasm)

**Triggers.**
- `Airstrike.UpdateInfo` calls it with (0,0) at 0x522990.
- `NinjaRope.UpdateTargettingInfo` calls it with (1,0) at 0x5229c9.
- `Teleport.UpdateTargettingInfo` calls it with (0,1) at 0x522a02.
- Each cursor sends one of these every frame through `0x5523c0`. `PayloadWeaponLogicEntity` sends one again on Fire (0x583a4a).

**Steps.**
1. Take the current logical camera (`CMS+0x2a0[CMS+0x28c]`): position at +0x04, look-at at +0x10, up at +0x1c. dir = normalize(lookAt - position), i.e. through the screen centre.
2. Ray length: ninja = `Ninja.MaxLength` - 10; otherwise `Land.Radius + max(Land.Radius, |pos - Land.Center|)`.
3. `Camera::RayTarget 0x51b150`: a 1000-step sweep against the land (0x466ae0, step = ray·0.001), giving the hit point and normal. If not ninja, it also intersects the plane y = `Water.Level`. If that crossing comes earlier (step index in (0, 1000] and before the land hit), the hit is the water point, normal = (0,1,0) and `Airstrike.WaterTarget` = 1; otherwise WaterTarget = 0.
4. Teleport only: rejected when hit normal.y < 0 (0x51cb0e).
5. Not ninja: a ray against worms (0x519dd0 on the collider at CMS+0x284, 1000 steps). If it hits a worm closer than the land, or the land was missed, and `Airstrike.IsWormValidTarget` != 0 (LOCAL default 0), the target is the worm hit point with normal (0,1,0) (0x51cc55).
6. Rejected when the horizontal (xz) distance camera → target exceeds 8 · `Land.Radius` (8.0 at 0x8544a0).
7. Writes `Airstrike.Direction` = dir (the camera view direction), `Airstrike.TargetPoint` = hit, `Airstrike.UpVector` = camera up, `Airstrike.HasTarget` = valid. HasTarget is forced to 1 on AI turns (`[0x959abc]` 0x4b3340).

The target's y is therefore the land, water or worm ray hit under the screen centre.

**CMS members**, data resources bound in the CMS ctor 0x51f740 (the `lea` comes before each name):

| offset | resource |
|---|---|
| +0x22c | Airstrike.MaxDistance |
| +0x230 | Camera.Shake.Magnitude |
| +0x234 | Land.Radius |
| +0x238 | Land.Center |
| +0x23c | Airstrike.HasTarget |
| +0x240 | Airstrike.Direction |
| +0x244 | Airstrike.TargetPoint |
| +0x248 | Airstrike.IsWormValidTarget |
| +0x24c | Ninja.MaxLength |
| +0x250 | Water.Level |
| +0x254 | Camera.LastTrackCam |
| +0x258 | RetreatTimeRemaining |

The Camera base ctor 0x51b570 binds `Airstrike.WaterTarget` (+0x48) and `Water.Level` (+0x44).

### Cursor (disasm; resource names are data strings)

`Weapon.Create*Cursor` messages are handled by GraphicalSpawningService 0x5009d0. Each class is created from a GUID record whose name sits at +0x10.

| message | class (vtable) | init | mesh / bitmap (file) | anim nodes |
|---|---|---|---|---|
| Weapon.CreateTargetingCursor (Donkey) | TargetingCursorGraphicEntity (0x8675cc) | 0x5f9a60 | `Targeting.Cursor.Mesh` (Target.xom), `Targeting.Cursor.Bitmap` (Target.tga), shadow mesh `Targeting.Cursor.Shadow` attached at node `Target_Shadow`, tint 0x645e2b02 | root `Target`; `Target_Intro`, `Target_Loop`, `Target_Error` |
| Weapon.CreateNinjaCursor | same class, +0x78 = 1 (0x500bb8) | | | |
| Weapon.CreateTeleportCursor | same class, +0x79 = 1 (0x500c04) | | | |
| Weapon.CreateBomberCursor (Airstrike, Super Airstrike, Fatkins) | BomberCursorGraphicEntity (0x857f0c) | 0x54c1f0 | `Airstrike.Cursor.Mesh` (AirstrikeCursor.xom), `Airstrike.Cursor.Bitmap` (Airstrike_Outer.tga), 5 × `Airstrike.Cursor.Dot` at `Dot_Null_01..05` (table 0x91f34c) | root `Aimer_Null`; `Airstrike_Intro`, `Cursor_Loop`, `Cursor_Error`; `Dots_Loop` starts at the end of the intro (0x54c380) |
| Weapon.CreateHomingCursor | HomingCursorGraphicEntity (0x859c84) + HomingLockOnGraphicEntity (0x859d3c) | | `Homing.Cursor.Mesh` (HomingAimerAim.xom), `Homing.Cursor.SquareMesh` (HomingAimerLock.xom); HUD corners `HUD.Homing.Cursor.TL/TR/BL/BR` (Homing TL/TR/BL/BR.tga) | on select: `Lock_Outer` + sound `weapons/LockOn` (0x560420) |
| Weapon.CreateSuperBomberCursor (from SuperBomberLogicEntity 0x58b4e0, during the flight) | SuperBomberCursorGraphicEntity (0x8674f4) | | `SuperBomber.Cursor.Mesh` (SASCursor.xom), `SuperBomber.Cursor.Bitmap`, `SuperBomber.Cursor.Shadow` | |

**Teleport.** `Weapon.CreateTeleportCursor` (handle 0x95e160, declared in the NinjaRope unit) has no sender anywhere in the exe, and Teleport is missing from the weapon enum at 0x90c920. Teleport targeting is therefore dead code in W4M PC (disasm).

**Base `CursorGraphicEntity` (vtable 0x858aac) members:**

| offset | meaning |
|---|---|
| +0x20 | mesh node, placed at the origin (0x96e878) at init |
| +0x24 | bitmap node, attached under the mesh at the anim-root node |
| +0x48 | Airstrike.HasTarget |
| +0x54 / +0x58 / +0x5c / +0x60 | anim names: root, intro, loop, error |
| +0x64 | intro length (0x5525c0) |
| +0x68 | visible |
| +0x70 | Airstrike.WaterTarget |

It subscribes to `Weapon.NotClearToFire`, `Camera.LogicalModeChanged`, `Weapon.DeleteCursor`, `HUD.Target.Selected` and `Binocular.*` (0x551e20).

**Per frame (Targeting cursor slot 6 0x5f9c90, Bomber 0x54c4a0, LockOn 0x560590):**
1. hasTarget = `0x5523c0(ninja, teleport)`.
2. Tint from 0x552340:

   | state | value | RGB |
   |---|---|---|
   | no target | 0xff0020d7 | (215,32,0) red |
   | water target | 0xffffa862 | (98,168,255) light blue |
   | valid | 0xffffffff | white |

   The RGB triples assume memory bytes R,G,B,A; the alpha byte at +3 is confirmed by 0x69f660. The Bomber cursor tints its 5 dots the same way.
3. Visible only when the view is 1 (Head) or 3 (Blimp) (slot 13 0x551fa0 / slot 14 0x552da0), and when the HUD flag `[0x96d030]+0x3c` / 0x7063f5 does not hide it.

**Other slots.**
- Show (slot 11, 0x5524a0): plays the intro from t=0, then queues the loop at the intro length. Hide: slot 12, 0x5525a0.
- `Camera.LogicalModeChanged` with param 1 in a cursor view re-runs Hide + Show (0x552a0e). The Targeting override 0x5f9d10 does this only for the ninja variant.
- `Weapon.NotClearToFire` (slot 20, 0x552630): plays sound `weapons/Gong`, then the error anim, then the loop again.

**Cursor position.** The cursor code never moves the mesh after init. The reticle is therefore screen-centre, drawn in a camera-attached or HUD layer (assumed; the render layer was not traced). Not the same thing: `HUD.TargetingCursor` + `TargetingCursor.Scale` 16 (LOCAL) belong to WeaponCursorGraphicEntity 0x5fb9c0, the aiming reticle (not checked).

### Confirm and hand-off (disasm)

**`PayloadWeaponLogicEntity::HandleMessage 0x586040`, on `Input.FirePressed`, in state 1:**
1. `0x583a10`: sends `Airstrike.UpdateInfo` and reads `Airstrike.HasTarget`. It fails if there is no target, **or if the current view is 2 (Default)**: the player must be in Blimp (or Head).
2. On success: `Payload.Target` := `Airstrike.TargetPoint`, then broadcast `HUD.Target.Selected`. HUD.Target.Selected has 12 handle copies; cursors, worm and HUD listen to it.
3. On failure: `Weapon.NotClearToFire`.
4. After success, state = 0.
   - Not homing: `Weapon.DeleteCursor`.
   - Aimed (homing): `Weapon.CreateAimingCursor`, then normal aiming and powering.
   - Not aimed: state = 2, then launch via 0x583160.
   - Powered: creates the class at 0x85c9e8 and sets state 2.

**Data keys consumed later:**
- `BomberLogicEntity` init 0x54d720 reads `Airstrike.Direction`, `Airstrike.TargetPoint`, `Airstrike.UpVector`, `Bomber.GroundSpeed` 0.15, `Bomber.BlitzDuration` 2000, `Bomber.NumBombs` 6, and writes `Bomber.Direction`.
- `DonkeyLogicEntity` init 0x553700 reads `Airstrike.TargetPoint`, `Donkey.ExtraHeight` 500, `Donkey.MinHeight` 1500, `Land.MaxHeight`.
- `SuperBomberLogicEntity` 0x58a7f0 reads `Airstrike.Direction`, `TargetPoint`, `UpVector`.
- `HomingPayloadLogicEntity::Initialize` 0x560bf0 reads `Payload.Target`.

**Airstrike direction.**
- dir = cross(`Airstrike.Direction`, `Airstrike.UpVector`) with y forced to 0, then normalised (0x54d931..0x54d97f). This is the camera's horizontal right vector: the bombers cross the screen from one side to the other. The operand order, and so the left-to-right sense, is assumed.
- There is no left/right key. The player picks the direction by yawing the Blimp camera.
- Start = target - dir · (GroundSpeed · BlitzDuration · 0.5) = 150 units before the target (0x54d9c2..0x54da0e, disasm; the operand roles are assumed).
- Super Airstrike starts from `Airstrike.Direction` rotated by pi/2 (0x58aa76, assumed), then is steered with `Fly.Yaw.Left/Right`. Fire drops the cows. EFMV.Start/End are used.

**AI path.** IsometricCam 0x529eb0, while `AIStrike.SeekTarget`: copies `AIStrike.TargetPoint` / `AIStrike.Direction` into `Airstrike.TargetPoint`, `Payload.Target` and `Airstrike.Direction`; sets HasTarget = 1, WaterTarget = 0, SeekTarget = 0; then posts a message (the AI equivalent of Fire).

### After firing: camera (disasm)

| weapon | camera |
|---|---|
| Airstrike / Fatkins | `BomberLogicEntity` takes the scene camera `perspShape` from the bomber mesh and sends `Camera.FollowSceneCam`. CMS first calls `SetCamera("Default")` (0x522ad7), which leaves Blimp, then follows the scene camera (view 0x14 / 0x10). On `Bomber.AnimsComplete` it sends `Camera.StopFollowingSceneCam`, and CMS goes back to the current logical camera, Default (0x522b60). |
| Super Airstrike | same scene-cam follow (0x58ace6); `SuperAirstrikeCamera` (Simple, PosUpdateSpeed 1, LookUpdateSpeed 0.1, data) is set at 0x57a330. |
| Concrete Donkey | `0x51d760("DonkeyCamera", donkeyTaskId)` creates a SimpleCam (view 0x15) that tracks the donkey (DonkeyCamera: PosUpdateSpeed 1, LookUpdateSpeed 0.1, data). The camera point is stored at +0x188 (z + 500). |
| Homing | normal aiming in the player's current view, then `HomingMissileFlyCamera` on launch. |

Next turn: `GameLogic.Turn.Started` → 0x51ef80 → `Camera.StartOfTurnCamera` ("Default", LOCAL / scripts).


## 11. Worms: physics state handlers and animation (extends §5)

Tags: **data** = read from the exe or the tweaks, **disasm** = read from code, **assumed** = inferred, not verified.
Units: the logic step is 20 ms. Velocities are in units/ms and accelerations in units/ms². A worm is 20 units tall (the land probe rods, §5). Angles are in radians.

### pData = `WormDataContainer` (data: `pe.py schema '^WormDataContainer$'`)

The `pData` argument that every `Update*` receives is the serialised `WormDataContainer`. The field names below come from its schema:

| off | field | used as |
|---|---|---|
| +0x38 | Position | pos |
| +0x50 | Velocity | ballistic or slide velocity |
| +0x5c | Aftertouch | air-steer velocity, added to Velocity when moving but never accelerated |
| +0x68 | InputImpulse | control input vector (`CalculateGlobalControlInput` 0x5ab3d0) |
| +0x74 | Acceleration | gravity (+ wind), set by 0x5a6d20 |
| +0x80 | SupportNormal | ground normal, written on landing |
| +0x8c / +0x90 | Orientation (vec3, yaw at +0x90) | facing |
| +0x98 | AngularVelocity | – |
| +0xe8 | PhysicsOverride | – |
| +0xec | Flags | bit0 = aftertouch (air control) on; 0x40 = skip the next fall damage once; 0x800 = no fall damage; 0x8040 / 0x8840 = no slide or hard-land on landing; 0x1000 = jump disabled; 0x20 = see 0x5ac390 (all disasm, meanings assumed from use) |
| +0xf0 | PhysicsState | kWPS_* |
| +0x114 | LogicAnimState (u32) | no gameplay writer in the exe: only the XOM default 0 (0x64cc5f). Lua sets it to 10 (countingsheep-w3d FunkySheep, helterskelter-w3d LookBaddie1-3). Its only reader, HudActiveWormArrowEntity 0x5ef6b4, skips the active-worm arrow update 0x5ee7f0 while it is 10 [disasm+data] |
| +0x120 / +0x122 | SupportFrame / SupportVoxel | written on landing (Ballistic, Sliding) |
| +0x124 | Active | cleared when DeathThroes / DrownFloat ends |
| +0x12f | IsAfterTouching | – |
| +0x130 | MovedByImpulse | tested by ImpulseWorm |

Correction to the existing "UpdateWalking" text: the "no physics owner → Passive" test reads **entity** +0x54, not pData (`this` = esi at 0x5b0dd4). This flag is non-zero for the worm in control (assumed). Every other worm is put into Passive (6).

Entity (WXWormLogicEntity) fields seen (disasm):

- +0x20: the event sink (WXWormGraphicEntity), queue at +0x178 / +0x184.
- +0x24: the land probe. +0x28: the collision sphere.
- +0x44: the surface material index (0 Default, 1 Slippy).
- +0xdc: a "force hard land" flag.
- +0xe8: the support surface id (0xFFFF = none).
- +0x114 / +0x118: jump timer and jump kind. +0x11c: slide time.
- +0x120 / +0x124: slide spin and its target.
- +0x128: death timer. +0x12c: stuck counter.
- +0x244: the last queued event.

Cached data handles (0x5a9880, data):

| off | key |
|---|---|
| +0xa4 | DamageGraphic.Amount |
| +0xa8 | DamageGraphic.Position |
| +0xac | Water.Level |
| +0xb0 | Worm.Drown.HeightOffset |
| +0xb4 | Worm.WeaponDisableMovement |
| +0xb8 | Worm.VelocityScale |
| +0xd4 | Worm.Walk.Speed |
| +0xd8 | WormPot |
| +0xbc.. | AimMouseLR, AnalogueAimLR, Zap.* |

#### Integration and gravity (disasm)

- **Integrate 0x5a6e90(pData)**: `pos += (Velocity + Aftertouch)*20 + Acceleration*200`, then `Velocity += Acceleration*20`. It is an exact constant-acceleration step with dt = 20 ms. **No drag and no terminal-velocity clamp.**
- **SetAcceleration 0x5a6d20**, called from ChangeState 0x5aa7f0 and from 0x5a9ac0:
  - `Acceleration = (0, Gravity × Low.Gravity.Multiplier, 0)`;
  - plus, when `WormPot.WindAffectsWorms` (+0x53) is set, `Wind.Speed × (cos Wind.Direction, 0, sin Wind.Direction) × WormPot.WindScale` (wind 0x5a6040).
  - Values: Gravity −0.00025 (WEAPTWK); Low.Gravity.Multiplier 1.0 (LOCAL), 0.5 when low gravity is on (`Low.Gravity.OnValue`, TWEAK). Data.
- **Air steer 0x5a6fe0**, run each Ballistic frame when Flags bit0 is set:
  - `Aftertouch += InputImpulse × WXWorm.AftertouchDelta`;
  - then `|Aftertouch|` is clamped to `WXWorm.AftertouchStrength`.
  - Values 0.015 and 0.1 (TWEAK). Globals 0x95fb70 / 0x95fb74 (squared) / 0x95fb78.

#### Jump launch values (loader 0x5a5d30, disasm + data)

| global | value | formula | default |
|---|---|---|---|
| 0x95fb94 | jump vy | sqrt(−2·g·JumpHeight) | 0.15811 (H 50) |
| 0x95fb90 | jump vx | JumpDistance / (−2·vyJump/g) | 0.063246 (D 80) |
| 0x95fb8c / 0x95fb88 | forward-flip vy / vx | sqrt(−2·g·ForwardFlipHeight); ForwardFlipDistance / (−2·**vyJump**/g) | 0.2 / 0.031623 |
| 0x95fb84 / 0x95fb80 | backflip vy / vx | same, with Backflip* | 0.2 / 0.031623 |
| 0x95fb7c | vertical jump vy | sqrt(−2·g·VerticalJumpHeight) | 0.18708 (H 70) |

Exe quirk (disasm, at 0x5a5f3f and 0x5a5fac): the flip horizontal speeds divide by the **normal jump's** air time (1265 ms), not the flip's own (1600 ms). Flips therefore travel about 50.6 units, not the 40 that the tweak says.

`Worm.Jump.Forward/Backward/Backflip/Upward` (TWEAK vectors) are read only by AI code 0x4af3f0, not by the worm physics. `Worm.MaxSlope*`, `Worm.WalkAnimSpeedScale`, `Worm.CreepAnimSpeedScale`, `Worm.HopTest.*` and `Worm.Hop.Velocity` have no string in the exe, so they are unused (data).

### Physics state handlers (disasm unless tagged)

#### StartJump 0x5acd40 → DetectJump (1)

- Runs at the end of every walking step.
- If the jump button is pressed (entity +0x31 bit0) and Flags & 0x1000 is clear:
  - QueueEvent(2 = Jump_Start);
  - jump window = **300 ms** (+0x114 = 0x12c);
  - kind = 2 ("held") (+0x118 = 2);
  - state → DetectJump (1). Coming from Vaulting, the position first snaps to the vault target.

#### DetectJump 0x5aefa0

Each frame:

- While kind is 2, releasing the button (entity +0x31 bit1 cleared) turns it into kind 0 and records the hold time.
- A second press during the window (bit0) turns kind 0 into kind 1.
- The window counts down by 20 ms. When it ends, the launch depends on the kind:

| kind | condition | launch | event |
|---|---|---|---|
| 0 (tap) | – | facing × JumpVx, vy = JumpVy | 3 Jump |
| 1 (double tap) | input·facing > 0, or input zero and entity +0x159 set | facing × FwdFlipVx, FwdFlipVy | 5 Fwdflip |
| 1 | otherwise (back or no input) | −facing × BackflipVx, BackflipVy | 4 Backflip |
| 2 (held all 300 ms) | input·facing > 0 | normal jump | 3 |
| 2 | otherwise | (0, VerticalVy, 0) | 6 vertical jump |

- Every launch then does: Velocity = launch, Aftertouch = 0, Flags |= 1 (air control on), state → **Ballistic (2)**.
- A second variant runs when global 0x95a100+0x9b bit 0x40 is set and entity +0x15a == 0 (assumed: analog control). It scales the tap jump's horizontal speed by the input magnitude (quantised to 1/4) and by hold/300 ms (1/8 steps).

#### Ballistic 0x5af430 (kWPS_Ballistic = 2, "flying")

1. If Flags bit0 is set: read the control input, then air-steer (0x5a6fe0).
2. Cast a parabola from pos: `CastRays(pos, Velocity+Aftertouch, Acceleration, 40 steps, mask 0xFFFF)`, i.e. all 8 probe points. Land ray 0x466ae0 marches `p(t) = p0 + v·t + ½·a·t²`, so t is in ms. A hit counts only if t ≤ **20** (this frame). The 40-step cast looks one frame ahead. Support id = 0xFFFF.
3. **No land hit this frame** (0x5af95d):
   - Integrate.
   - If Velocity.y went from > −0.3 to ≤ −0.3 and no flags 0x8840 and entity +0xdc == 0: QueueEvent(22) ("now falling": tumble anim).
   - Sphere resolve against entities (0x519ed0):
     - **contact**: if Fits, accept the position and stuck counter −1. Then if contact n.y < 0.8: Rebound(n) + event 23 (Thud). Otherwise **land on the object**: event 8 (15 if +0xdc), state → Ambulatory, support id stored. If not Fits: stuck counter +2, Rebound + event 23.
     - **no contact**: Fits → move, else stuck +2 + Rebound.
     - On a Fits failure pos is restored to the pre-Integrate position and the rebound normal is normalize(old − new) (0x5afa78, 0x5afb17) [disasm].
     - Stuck counter ≥ 20 → force land (event 8 or 15, Ambulatory) where it is, support id 0xFFFF: the idle walk branch (0x5b1a3e) only falls again for an entity support, so a worm forced to land in the air stays there until it walks [disasm]. The counter (entity +0x12c) is never reset in Ballistic; only Sliding's Landed paths clear it (0x5b05c2 / 0x5b063b) [disasm].
4. **Land hit**:
   - candidate = hit point − probe offset, sphere resolve, ground normal n (0x59ef90).
   - If not Fits: stuck counter +2; at ≥ 20, force land. Otherwise clear air control and Rebound(n) with n = normalize(pos − candidate), or Velocity = −Velocity if that is zero (0x5af888), then event 23. pos is not moved.
   - A slot narrower than the 8-unit tripod (x ±4, z −3 / +5) is caught here: a foot point hits a lip within the frame and the worm lands on it, standing on that foot; the stuck counter is not involved. A slot 8 units wide or more lets all three Fits rods in, and the worm drops in [disasm + reasoning on the probe geometry 0x91ffc8].
   - If Fits: pos = candidate, SupportNormal = n, SupportFrame/Voxel stored.
   - If the nearest hit is not a foot point (0x59ec50, per-point flag, assumed "foot") → Rebound + event 23.
   - If n.y < **0.2** → Rebound + event 23 (wall).
   - Otherwise it is a landing. With v = Velocity + Aftertouch: `vn = v·n` and `vt = v − vn·n`. When `|vt|² < vn²`, `|vt|²` is halved before the slide test.
     - Walkable (n.y ≥ cos SlideAngle[mat]) and `|vt|² < StartSlideVel²[mat]`, or Flags & 0x8040 → **land walking**:
       - if `vn ≤ −0.3` (hard) and entity +0x64 == 0 and no flags 0x8840: event 15 (hard land: poof + recover); otherwise event 8 (soft Land, param vn). If entity +0xdc is set, the threshold is 1000, so always 15.
       - if `vn < −0.3`: FallDamage(vn) and Velocity = 0. Otherwise Velocity = (vt.x, 0, vt.z).
       - state → **Ambulatory (0)**, support id stored.
     - Otherwise (steep, or too fast) → **slide**: FallDamage if vn < −0.3, Velocity = vt, event 17, state → **Sliding (3)**.

#### Vaulting 0x5aca80 (kWPS_Vaulting = 4) and its start in UpdateWalking

Start, ledge path 0x5b1209-0x5b128f (5 < d <= 20 units) [disasm]:

- target ent+0x104 = nearest foot hit + 0.1 y, after the sphere resolve; it must pass the walkable test 0x4adda0 and Fits.
- ent+0xF8 = InputImpulse (pData+0x68) at that frame; ent+0xEC = pos; timer ent+0x110 = **250** (0xfa); event **9** (Vault clip); material ent+0x44.
- pos is not moved that frame, and this path returns without StartJump.
- The sphere-contact path 0x5b165f-0x5b1746 sets the same fields, then calls StartJump 0x5acd40 the same frame (0x5b183a): a jump pressed on that frame snaps pos to the target and goes to DetectJump.

Each frame in state 4, 0x5aca80 [disasm]:

1. Reads the control input (0x5ab3d0).
2. If `input · ent+0xF8 <= 0` (stick released, or pushed against the start direction): target = old pos (ent+0xEC), ChangeState(Ambulatory). ChangeState 0x5aa847 copies the target into pos when it leaves state 4, so the worm drops back to where it started.
3. Else timer −= 20. At ≤ 0: ChangeState(Ambulatory), and pos = target (the same copy).
4. Else pos = MoveTowards(pos, target, **4.0** units) (0x5a59f0, constant 0x858228) [data + disasm].

Consequences:

- A vault always lasts the full 250 ms: nothing ends it on arrival.
- No collision test runs during the move. Land that appears in the way does not stop it [disasm: no Fits, ray or sphere call in 0x5aca80].
- No Orientation write, no StartJump, no fire handling in the worm code: the facing is frozen and a jump press is ignored [disasm: the dispatcher 0x5b1fc0 calls 0x5aca80 only; StartJump's three callers are all in UpdateWalking].
- Every exit from state 4 through ChangeState (blast via ImpulseWorm, death, physics override) snaps pos to the current target first [disasm 0x5aa847]. UpdateWalking's Passive switch (0x5b0df9) and StartJump (0x5acdba) do the same copy inline.
- **Firing during the vault**: refused for most weapons. FirePressed handlers call a CanFire before firing:
  - BaseWeaponLogicEntity 0x54a2d0 (vtable slot at 0x857b34, also Flood's 0x85919c) and PayloadWeaponLogicEntity 0x583ca0 (called from the FirePressed handler 0x586092 and from 0x586e9f) both return `PhysicsState == 0 || CanBeFiredWhenWormMoving` [disasm].
  - That flag is BaseWeaponContainer field 0x0c at +0x79. In WEAPTWK it is 1 only for Dynamite, FirePunch, Landmine, LandmineCluster and Sheep; every other weapon, and every utility, has 0 [data].
  - Vaulting (4), DetectJump (1), Ballistic (2), Sliding (3) and Override (5: rope, parachute, jetpack) therefore refuse every other weapon. The secondaries dropped from a tool (Dynamite, mines, Sheep) are exactly the flagged ones.
  - GunWeaponLogicEntity's check 0x55cd30 returns 1: guns fire in any state [disasm].
  - The jetpack's FireUtil handler 0x562270 tests `state == 0` (0x5623b4) only for PackAccessory.Trigger, the take-off [disasm]. The rope fires on Input.FireUtilPressed (handler 0x574730 → 0x573790; its Input.FirePressed handle is only subscribed, at 0x57096e) [disasm]:
    - there is no CanFire and no `+0x79` test;
    - `state == 0` is tested at 0x573845 only to play `global/FEError` when no "Head" target is found, at most every 500 ms (+0x13c);
    - so the rope fires in any state, in the air included.
  - Only the press is gated: a charge started on the ground goes on to its release [assumed: no CanFire found on FireReleased].
- **Velocity during a vault** [disasm]:
  - UpdateWalking sets Velocity = InputImpulse on a successful step, through 0x546f10 at 0x5b146e (drop path) and 0x5b19c8 (normal step); the idle branch zeroes it (0x5b1c1b).
  - The vault start does not write it, and nor does 0x5aca80. So during a vault Velocity is the last walk step's velocity.
  - The launch (0x585a1b: `state == 0`) counts the vault as off its feet: a flagged payload with IsLaunchedFromWorm (+0x1cf; 1 for Dynamite, Landmine, LandmineCluster, Sheep) inherits it at 0x585a58.

Ours (sim.cpp `walkStep` / `vaultStep`, `Game::vault`; ai.cpp `Mover::vault`):

- A climb above `STEP` (5 units) up to `STEP_UP` (20) starts the vault instead of the instant climb, if the target is walkable and Fits. The walkable test uses the ground normal at the target, the 0x59ef90 mean below; the toe and plain climbs share it [ours, from 0x5b11ca / 0x5b11f0].
- 15 ticks (`msTicks(250)`) at 10 m/s (4 units per 20 ms) toward the target, then the snap.
- The input is the heading stick (HEADING) or the facing, times `walk`; `dot <= 0` drops it back to the start.
- During the vault: the yaw is frozen, JUMP is ignored, `stepWorm` skips the body.
- Leaving it otherwise: knocked or roped snaps to the target; moved by a weapon (teleport) ends it in place [ours].
- `Game::fireable`: utilities and guns always fire; Dynamite, Fire Punch, mines and Sheep always fire; anything else needs a grounded worm, not sliding, not vaulting, no jump pending. It is asked on the press, or on the first tick of a charge [ours, from 0x54a2d0 / 0x583ca0].
- `Vault::vel` keeps the walk velocity of the vault's start tick. A payload fired during the vault gets the carried offset and that velocity, like a worm off its feet. The game also takes it from that tick, while W4M takes it from the step before [assumed equivalent].
- Phase change (control lost) acts as no input: back to the start.

#### UpdateWalking: support, push-out and the uphill rule (disasm)

- **Support**: the 4 foot rays (mask 0xF: tripod (±4, −3), (0, +5) and the centre, world axes, 0x91ffc8 [data]) are cast from 20 units above the candidate; d = 20 − nearest, i.e. the **highest** hit carries the worm. One foot on a lip is enough: walking over a slot narrower than the tripod never drops into it.
- **Normal step push-out** (0x5b194c): Fits is tested at y, y+1, …, y+5 units. Only y..y+4 are accepted: the counter starts at 5 and a success with the counter at 0 is rejected (`test ebx, ebx; jbe` at 0x5b198b). So the step raises the worm by at most 4 units.
- **Drop path push-out** (0x5b14e1): y+1 is added before the first test, so y+1..y+5 are accepted and y+6 is rejected.
- **Blocked**: both failures jump to 0x5b1a21, which still calls StartJump. A blocked worm can jump.
- **Uphill rule** (0x5b1920-0x5b1946): if `normal · (cand − pos) < 0`, the ground under the candidate must pass the walkable test 0x4adda0, else blocked. The vector is cand − pos: 0x454e00(out, a, b) computes out = a − b, and 0x5b18f0 passes (out = esp+0x4c, a = cand at esp+0x20, b = ebx = pData+0x38 = pos). The normal is the ground normal of the candidate (0x59ef90 output) [disasm]. After the move, n.y < cos SlideAngle → event 17 and Sliding (0x5b19d0).

- **Ground normal 0x59ef90** [disasm]: it needs 2 or more hits (+0x1e4 > 1). It sums the land normals (re-cast 0x59eb30, normal 0x482010, normalized 0x4453c0) of the hits whose `HitPointRel` differs from the nearest hit's by at most 1 unit, i.e. the feet level with the highest one, then normalizes. So when two feet straddle a slot on its two lips, their normals average to near vertical.

Ours (sim.cpp `footing`, `walkStep`):

- The centre and the tripod (±0.2, −0.15) (0, +0.25) m, world axes, carry the worm. Any one hit is enough, as in W4M. This applies to the grounded test, the flight landing, the flight push-up and the walk's settle [ours, from 0x91ffc8].
- The normal is the mean of `Terrain::normal` at the feet whose land top is within 1 unit of the highest one. The tops are measured in 1-unit steps up to 5 [ours, from 0x59ef90].
- The walk follows W4M:
  - the candidate is set on the highest hit within 5 units;
  - the uphill rule `n · (cand − pos) < 0` needs a walkable n;
  - the step push-out tries +0..+4 units; the drop path first tries a plain fall, then +1..+5;
  - a raised worm is placed without a settle.
  No test covers the push-out: with W4M's probe, any lip low enough to be cleared by 4 units is seen as ground and vaulted instead [ours].
- **`fits` stays relative** ("no deeper than before"; W4M Fits 0x59edf0 is absolute). This is a voxel constraint [ours].
  - Our land is a trilinear density field, clamped to ±0.25 m on a 0.25 m grid, so surfaces are soft.
  - Our body test samples a 0.2 m ring at 0.7 and 0.95 m. W4M uses 3 rods of 1 m, on a worm always placed 0.1 unit above its highest hit.
  - Land can also appear around a worm: girders, terrain edits, spawns. An absolute test then locks a worm whose upper body samples a slightly positive density.
  - `checkWallStuck` (head wedged 0.05 m in a sloping ceiling must walk out) fails with an absolute test. The slot and map sweeps give the same counts either way (10 400 slot runs: identical; maps: 57 / 57 runs over 0.06 before the drowned runs were cut).
  - The relative form only differs once the body is already in land. Below that it is W4M's absolute test.

#### Rebound 0x5acea0 (disasm)

- `v = Velocity + Aftertouch`, then Bounce 0x518f40(v, n, e = **0.3**, tangential kept ×**1.0**, min speed **0.01**):
  - if `v·n < 0`: v = vt·1.0 − vn·0.3, and v becomes 0 when |v| < 0.01;
  - if `v·n ≥ 0`: v = n·|v|·0.3, or 0 when |v| < 0.01.
- Velocity = v, Aftertouch = 0, air control off.
- If v ends at exactly (0, ≤0, 0): when n.y > 0, event 8 + state → **Sliding**; otherwise Velocity = (0, −0.01, 0) and Integrate.
- The constants are hardcoded. `Worm.BounceMultiplier`/`Default` (0.6/0.3) are only handled in ParticleHandlerService 0x5c02c0, which switches them by WormPot Sticky/Slippy. The worm code never reads them (data: no other xref).

#### FallDamage 0x5ac3e0(pData, vn) (disasm + data)

- None if Flags & 0x800. If Flags & 0x8040: bit 0x40 is cleared and no damage (one-shot immunity).
- Otherwise `damage = trunc((−0.3 − vn) × Worm.FallDamageRatio) + 1`, with Worm.FallDamageRatio = 100 (LOCAL). Then ApplyDamage 0x5ab7e0(damage, 1) and an effect via 0x4bc410(0, 100, worm pos, 500, −1, −1) (assumed: rumble or shake).
- The threshold is |vn| > 0.3, i.e. a free fall of more than `0.3²/(2·0.00025)` = **180 units** (9 worm heights).
- Examples: vn −0.4 (320 units) → 11 hp; vn −0.5 (500 units) → 21 hp.

#### Fall 0x5acba0(pData, vel, aftertouch, airControl)

Velocity = vel, Aftertouch = aftertouch, Flags bit0 = airControl, event 7, state → Ballistic.

Called when:

- walking off a ledge (0x5b14c7: walk velocity kept, Aftertouch 0, **air control on**). No 0.7 multiplier: the `WalkOffCliffVelMulti` string is absent;
- the slide drops off a ledge (0x5b02dc).

#### Sliding 0x5afbe0 (kWPS_Sliding = 3) (disasm + data)

Per frame:

1. If air control is on, read the input.
2. Slide time +20 ms.
3. Spin: rate +0x120 approaches target +0x124 (0x47a1a0, factor 3, max 2°/frame), and yaw += rate.
4. **Gravity along the slope**: Velocity += (g − (g·n)·n)·20, with n = SupportNormal.
5. Steering: with air control, input pushes ×0.003 when it points along the motion, else ×0.0005.
6. **Friction**: Velocity ×= SlideFriction[mat] **per frame** (Default 0.95, Slippy 0.999).
7. Probe the ground under the next position: 4 foot rays, 300 steps; d = 20 − nearest.

| d | action |
|---|---|
| d > 5 (wall or step too high) | if \|v\|² < StartSlideVel²: Landed. Else ray along v (20 steps): hit → Rebound(n), spin target = (target + 3·(n×v)) / 2, stuck +2; no hit → Landed |
| d < −5 (drop) | v projected off the ground; if it fits → **Fall()** (Ballistic). Else Landed |
| −5..5 | pos = hit + 0.1y, sphere resolve (a side contact → Rebound + stuck +2). Fits → move, stuck −1, SupportNormal/Frame/Voxel updated. Then if the ground is walkable (n.y ≥ cos SlideAngle) and \|v\|² < **StopSlideVel²[mat]** → Landed. Not Fits → Landed |

- Stuck counter ≥ 20 → Landed (0x5b05cc), and the count is reset to 0 (0x5b063b). Every Landed path in Sliding resets it (0x5b05c2).
- Sliding never calls UpdateWalking or StartJump: no walking and no jumping while it lasts (dispatcher 0x5b1fb6) [disasm].
- "Landed" = `QueueEvent(kWE_Landed = 8); ChangeState(kWPS_Ambulatory)`; the debug string 0x85fb58 spells it.
- Thresholds (0x5a5d30, data):

| | Default | Slippy | global |
|---|---|---|---|
| SlideAngle (deg) | 60 | 10 | cos at 0x92009c / 0x9200a0 |
| StartSlideVel | 0.2 | 0.01 | squared at 0x9200ac / 0x9200b0 |
| StopSlideVel | 0.06 | 0.01 | squared at 0x9200b4 / 0x9200b8 |
| SlideFriction | 0.95 | 0.999 | 0x9200a4 / 0x9200a8 |

Sliding, further W4M detail (disasm):

- **Entry**:
  - Ballistic landing: `vt = v − (v·n)n`; `|vt|²` is halved when below `vn²`; walkable and below StartSlideVel² → Ambulatory, else Sliding with Velocity = vt (3D).
  - A successful walk step onto non-walkable ground (0x5b19d0): Sliding with Velocity = InputImpulse, set just before (0x5b19c8).
  - ImpulseWorm on the ground with `impulse·SupportNormal < 0`.
  - The idle walk branch never starts one: a still worm on steep ground stays. ChangeState (0x5aaa0c) zeroes the slide time, the spin rate +0x120 and its target +0x124.
- **Gravity** (0x5afd60-0x5afe26): `v.xz −= (Acceleration·n) n.xz · 20`. v.y gets nothing: only friction acts on it (×SlideFriction on all three axes, 0x5aff2d). The stop test (0x5b04c8) uses the 3D |v|².
- **Steering**, when Flags bit0 is set (0x5afe07):
  - The first test (0x5afe30) is `input · (Acceleration·n) n.xz`. It is > 0 when the input points up the slope, which gets 0.0005.
  - Otherwise the second dot (0x5afe68, FPU stack decoded) is `v · (Acceleration·n) n.xz`. It is > 0 when the velocity runs up the slope, which gets 0.003; else 0.0005 [disasm]. So 0.003 is for an input that does not point uphill while the worm still moves uphill.
  - Then `v += input × k` (0x5afeba).
- **Air control**, Flags bit0:
  - set by DetectJump's launch (Flags |= 1) and by Fall() from a walk-off (0x5b14c7, airControl 1);
  - cleared by Rebound (0x5acea0), ImpulseWorm, the slide's drop (Fall(..., 0) at 0x5b02dc) and Ballistic's not-Fits land hit (0x5af8a5).
- **Spin**:
  - each frame `rate += clamp((target − rate)/4, ±0.0349)` (0x47a1a0, k 3), then `yaw += rate` (Orientation +0x90);
  - on a wall rebound (0x5b010b): `target = (target + 3·SupportNormal·(n_hit × v)) / 2`;
  - on following the ground (0x5b0448-0x5b049b): `target −= 2·(n_new × n_old)·v` (cross product 0x454d90(out, a, b) = a × b), then SupportNormal = n_new.
- **Probe** (0x5aff70): the 4 foot rays from cand + 20 units down, 300 steps; `cand = pos + v·20`.
  - d > 5: a wall. Below StartSlideVel → Landed. Else one frame's ray of all 8 points along v: a hit → Rebound + spin + stuck +2; no hit → Landed.
  - d < −5: a drop. `v −= (v·n)n`; if the body Fits at the old height → pos = (cand.x, pos.y, cand.z), Fall(v, 0, 0). Else Landed.
  - Else: pos = highest hit + 0.1; if not Fits → Landed; stuck −1; then walkable and below StopSlideVel² → Landed.
- A slope steeper than atan(5/4) = 51° puts the tripod's uphill foot over 5 units: a slow slide there lands (wall branch). So a slow worm stays on a 70° slope.

Ours (sim.cpp `slideStep` / `wormBody` / `slideIfSteep`, `Motion`; the same in ai.cpp `move` / `stepBody`):

- `Motion`, per worm and checksummed: `stuck`, `air`, `slide`, `spin`, `spinTo`, `normal`. `input` is this tick's stick, used only by Sliding.
- `slideStep` follows each W4M step above, in W4M frames per tick (DT/20 ms) and m/s (one unit/ms is 50 m/s).
- Details:
  - the wall branch casts the 8 probe points (the 4 feet, and the same 1 m higher for the heads) along v over one tick, as CastRays(pos, v, 20 steps, mask 0xFFFF) at 0x5b00e0; the hit normal is the mean of the hits within 1 unit of the nearest, as 0x59ef90. Ours uses `Terrain::raycast` per point;
  - SupportNormal is `Motion::normal` (checksummed): stored at the landing (0x5af5ba), at a walk onto steep ground (0x5b1998), and on each ground follow (0x5b04a1); gravity, steering, the drop and the spin use it;
  - the spin's yaw sign matches ours. W4M yaw is `atan2(x, z)` (0x519120: acos(z), negated for x < 0) [disasm], our facing is (sin yaw, cos yaw), and tools/w4m-maps maps W4M x and z to our x and z without a mirror (`to_grid`) [data].
- Sliding takes no walk and no jump. A slide that ends sets the worm Ambulatory (velocity 0); a slide drop goes Ballistic with air control off.
- An Ambulatory worm with no velocity does nothing beyond the push-up out of land and `clearWalls` (our body's width).
- Ballistic is unchanged apart from the stuck count. A horizontal or upward push of a standing worm goes Ballistic; a push into the ground starts Sliding.
- The 0.40 m slot (the tripod's width) gave 30 never-ending slides. That was our earlier slide; the W4M probe's wall branch now lands those worms.

Ballistic stuck count, ours:

- In flight, each tick where a move does not Fit adds 2, otherwise 1 is removed. The test covers sideways, upward and now downward moves: a falling move that would sink the upper body deeper is undone and rebounds at 0.3.
- At 20 the worm is landed where it is, velocity 0, and the count is kept.
- The next tick it tries to fall again, the move is undone again, and it lands again. It therefore stays put until it walks, jumps or is blasted, exactly like W4M's forced Ambulatory: neither the idle walk branch (0x5b1a3e) nor Passive (0x5b0c8b) drops a worm whose support id is 0xFFFF (land). EstablishPhysicsState 0x5a6af0 is only reached from the Undefined state (0x5b201d) and the UFO flag path (0x5a9fe8) [disasm].
- The jetpack's flight does not count, since it is not Ballistic [ours].

#### Passive 0x5b0c20 (kWPS_Passive = 6) and UpdatePassive 0x5aecb0

- **Passive** (worms not in control):
  - if entity +0x54 is set again → Ambulatory + UpdateWalking;
  - else if entity +0x64 (assumed: burning) → UpdateBurning 0x5aea10;
  - else, if the support surface (+0xe8 → 0x5160e0) reports it moved or vanished (0x47a080) → event 7 + **Ballistic**. No integration while parked.
- **UpdatePassive 0x5aecb0**, the turn-only mode of the active worm. UpdateWalking chooses it when 0x5ac390 is true: `Worm.WeaponDisableMovement` > 0, ArtilleryMode (+0x12a), Flags & 0x20, or game phase 14. It does:
  - Velocity = 0;
  - snap facing to the input (entity +0xde/+0xdf);
  - otherwise turn: left input (+0x37) → event 10, yaw rate → +0.001 rad/ms; right (+0x38) → event 11, −0.001; none → event 12, rate 0. The rate eases with 0x47a1a0 (accel 0.0002). Yaw += rate·20·k;
  - the support moved → event 7 + Ballistic.
- UpdateWalking's idle branch uses the same turn code (0x5b1c7a).

#### DeathThroes 0x5aa080 (7) and DrownFloat 0x5aa130 (8)

- **Land death start 0x5adbf0**: unless already DrownFloat, send `Worm.LandDeath` (msg obj 0x95fc18), state → 7, event 19, timer +0x128 = **3000 ms**.
- **DeathThroes**: no motion. Timer −20 per frame. At 0: Active = 0, death blast 0x5a9400 (`Worm.DeathWormDamageMagnitude/Radius`, `DeathImpulseMagnitude`, `DeathLandDamageRadius`), 0x5a9310, send `WXWormManager.UnspawnWorm` (0x95fbe8).
- **Drown start 0x5ad640**: `Worm.Drowning` (0x95fc10), event 21, particle `WXP_WaterSplash`.
- **DrownFloat**:
  - target height = `Water.Level − 8.0` (hardcoded 8; `Worm.Drown.HeightOffset` = 7 is cached at +0xb0 but not used here).
  - Below the target: velocity steered toward rising (0x5a59f0 / 0x569fa0 with 0.03, 12, 0.001; the exact law is not decoded). On reaching the surface with vy ≤ 0: timer = **2000 ms**.
  - While the timer runs: vy = (vy − (y − target)·0.001)·0.95 (damped bob). pos += Velocity·20, no gravity.
  - Timer end: Active = 0, 0x5a9400, `WXWormManager.UnspawnWorm`.

#### ImpulseWorm 0x5ad010 (blast)

- Ignored in states 7/8. Magnitude ×WormPot.StickyModeScale when StickyMode is on. Air control off.
- On the ground with `impulse·SupportNormal < 0`: Velocity = (v + impulse) minus its normal component, state → **Sliding**, hit clip 0x5a9100.
- Otherwise `Velocity += impulse`, state Ballistic, and the reaction depends on `d = facing · horizontal blast dir`:
  - d < −0.5: event 14 (blown backwards) and yaw = atan2(dir) + π;
  - −0.5 ≤ d < 0.4: event 13 with param = side (cross product) and yaw = atan2(dir);
  - d ≥ 0.4, or a pure vertical blast: event 13 with param 0.
- **Hit clip 0x5a9100**: from facing·dir, `HitFront` (< −0.5, or no horizontal part), `HitBack` (> 0.5), else `HitLeft` (right·dir < −0.5) / `HitRight`. With a "nailed" flag, the `Nailed*` variant. Callers: the damage messages 0x5ae320 / 0x5ae4f0 and the slide path above.

### Worm animation (WXWormGraphicEntity), disasm + data

#### Event queue (kWE_*)

QueueEvent 0x5acb80(code, float) stores the code at entity +0x244. 0x5ac4d0 pushes code and param into the graphic entity's vectors at +0x178 / +0x184. The graphic update 0x5a4740 drains them in 0x5a3620 (jumptable 0x5a410c, index code−1).

| code | sender | graphic effect (handler VA) |
|---|---|---|
| 1 | UpdateWalking (walk input, phase 2) | send `HeldAccessory.Hide`, +0x160 = 0 (0x5a3692) |
| 2 | StartJump | `HeldAccessory.Hide`; one-shot **Jump_Start** (weight 1, t = 0); anim state 2 (0x5a36ed) |
| 3, 6 | DetectJump (jump, vertical jump) | one-shot **Jump**; predicted parabola stored (+0x20c, 10000 ms); voice "Jump" (0x5a1d30); state 2 (0x5a3773) |
| 4 | backflip | **Backflip** + voice "Jump", state 2 (0x5a37de) |
| 5 | forward flip | **Fwdflip** + voice "Jump", state 2 (0x5a3849) |
| 7 | Fall, Passive / OverridePhysics support loss | **Fall** at weight 0 (eases in), state 2; WXActor calls 0x60ae60 and 0x60ba40(200) (assumed: face or look, 200 ms) (0x5a389c) |
| 8 | kWE_Landed (soft land, end of slide) | **Land** through the scheduler, weight = clamp(\|vn\|·5, 0, 1); pose reset; `WXP_Worm_Hop_Poof`; state 0 (0x5a3913) |
| 9 | Walking → Vaulting | **Vault** one-shot (0x5a39e3) |
| 10 / 11 / 12 | turn left / right / stop | +0x160 = −1 / +1 / 0, only for the worm in control (0x5a3a25...) |
| 13 | ImpulseWorm (side or up) | state 4 (blast flight). If vy > 0.1 a message type 0x1a (assumed: "Blasted" acting). Clip: param > 0 → **Blastflight4**, param < 0 → **Blastflight5** (tumble mode 1, random spin of 2π·(2r)+π); param 0 → random **Blastflight2** (mode 0) or **Skid** (mode 1) (0x5a3abb) |
| 14 | ImpulseWorm (blown backwards) | **Blastflight3**, mode 2, state 4 (0x5a3c11) |
| 15 | hard landing | `WXP_Player_Land_Poof` (worm in control) or `WXP_Worm_Land_Poof`. If the anim state was 3 (flying): message type 5, then the recover clip by flight mode (below). Else message type 6 + **RecoverBurried1**. State 5 (0x5a3cc5) |
| 17 | Ballistic or Walking → Sliding | WXActor (as 7), pose reset, arm-flail parameters (+0x1dc = 0.02, +0x1e0 = 30 from ground else 1). State 6 (0x5a3eaa) |
| 18 | MsgOverridePhysics | state 1: pose manager only (rope, jetpack...) (0x5a3f64) |
| 19 | Land death start | state 7 (= ground behaviour) + message type 0xe (0x5a3f88). The gesture comes from WORMACTING (docs/death-sequence.md) |
| 21 | Drowning start | state 8 (0x5a401a) |
| 22 | Ballistic, vy crosses −0.3 | **Skid** as the flight clip, tumble mode 1, spin 2π; state 4 (0x5a3a60) |
| 23 | Rebound | sound `weapons/Thud` at the worm (PlaySound 0x604a20) (0x5a4064) |
| 16, 20 | – | no-op |

#### Clip handles (0x5a49a0, `FindClip` 0x6a0350 by name, data)

| slot | clip |
|---|---|
| +0x94 | Walk |
| +0x98 | WalkTail |
| +0x9c | Jump |
| +0xa0 | Backflip |
| +0xa4 | Fwdflip |
| +0xa8 | Jump_Start |
| +0xac | AT_FB |
| +0xb0 | AT_LR |
| +0xb4 | Fall |
| +0xb8 | Land |
| +0xbc | Vault |
| +0xc0 | TailAngle |
| +0xc4 | TailLag |
| +0xc8 | TipAngle |
| +0xcc | HopLeft |
| +0xd0 | HopRight |
| +0xd4 | Skid |
| +0xd8 .. +0xe4 | Blastflight2 .. Blastflight5 |
| +0xe8 | RecoverFront1 |
| +0xec | RecoverBurried1 |
| +0xf0 | RecoverBack1 |
| +0xf4 | RecoverBack2 |
| +0xf8 | SkidArms |
| +0xfc | Death |
| +0x100 | FallDrown |
| +0x104 / +0x108 / +0x10c | FPX / FPY / FPZ |

Clip API, an XAnim scheduler on the entity at graphic +0x24 (disasm):

| VA | call |
|---|---|
| 0x6a0350 | FindClip(name) |
| 0x6a03d0 | PlayClip (scheduler slot +0x14) |
| 0x6a04c0 | SetTimeAndWeight(clip, t, w) |
| 0x6a0530 | SetWeight |
| 0x6a0600 | Length (clip info +4, in seconds) |

#### Anim state machine (graphic +0x174, per-frame switch 0x5a4740 → jumptable 0x5a4978)

| state | handler | behaviour |
|---|---|---|
| 0 ground, and 7 (death) | 0x5a2a00 | Walk/WalkTail blended by horizontal speed s = \|Velocity.xz\|. Moving: phase += dt·s·0.45, wrapped to [0, 0.5); walk weight → 1. Stopped: the phase eases to the nearest rest pose (0 or 0.5), weight → 0 (rate 0.1). WalkTail weight is scaled further. WormPoseManager 0x59da40 adds the pose layers (aim arms, head and eyes: RightArmRotX/Y, HeadRotX/Y, Eyes_LR/UD, EmoteBlend, PoseBlend, strings at 0x85e1d8...) |
| 1 override | 0x59f2d0 | pose manager only |
| 2 one-shot | 0x5a01a0 | clip +0x1ac plays at 0.02 s per 20 ms (real time), weight → 1. At the end it **holds the last frame** and blends in **AT_FB / AT_LR**, time-scrubbed by the aftertouch input (phase = (1 − input·facing)/2 and (1 − input·right)/2, rate 0.1). No automatic chain: the next event (Land, Fall, 22...) switches the state |
| 3 / 4 blast flight | 0x5a0460 | the flight clip (+0x1b8) loops at 0.02 s per frame. Mode 0/2: body pitch follows the velocity (atan2). Mode 1: tumble, angle += spin·0.02 per frame (wraps at 2π). State 4 snaps on its first frame, then 3 eases at max 6°/frame |
| 5 recover | 0x5a2c90 | one-shot +0x1c8 at weight 1. At its end: state 0, clip weight 0, pose reset |
| 6 slide | 0x5a2d70 | **SkidArms** with random arm targets (rand 0x68c07b), WXActor face calls (0x60bae0 / 0x60bb40). Velocity terms ±0.006, 2000 ms constant (not decoded) |
| 8 drown | 0x5a06b0 | FallDrown (assumed slot +0x100) scrubbed by clamp(vy·10, −1, 1), with pitch and roll easing back to 0 |

Recover clip after a hard land from blast flight (0x5a3d64):

- mode 0 → RecoverFront1;
- mode 1 (tumble) by spin angle +0x158 (0x5a3dc1; 3π/4 at 0x85e77c, 7π/4 at 0x85e778, 5π/4 at 0x85e770): below 3π/4 or above 7π/4 → RecoverFront1; 3π/4..5π/4 (head down) → RecoverBurried1 (0x5a3e8f); 5π/4..7π/4 → random RecoverBack1/RecoverBack2 (0x5a3e0b, rand 0x68c0aa & 0x100);
- the angle +0x158 is zeroed every frame by the one-shot (0x5a01a0) and ground (0x5a2a00) handlers, at the end of a recovery (0x5a2c90), in a slide (0x5a2d70) and by events 13/14 (0x5a3b87, 0x5a3caa). Event 22 keeps it: 0 after a jump or fall, the mode-0 flight pitch asin(vy/|v|) after a blast (0x5a0617, mode 2: π/2 − that);
- the angles +0x150/+0x158/+0x15c go to the model node (graphic +0x24, vfunc 0x54, 0x5a26d3): the body turns about the mesh's own origin;
- mode 2 → random RecoverBack1/2.

Smoothing helper 0x569f20(&v, target, a, rate, dt): approaches the target with the step clamped to rate·dt (disasm, exact law approximate). Most anim weights use rate 0.1 per 20 ms frame, i.e. about 0.2 s for 0 → 1.

WormPoseManager (0x59da40, disasm): `Blend` is an XTransform node of the worm whose clip channels drive the layers: Translate.x / .y = left / right arm mode (0x59b870), Rotate.y in degrees = head/eye mode (0x59be40), Scale.y = PoseBlend, Scale.z = EmoteBlend, Scale.x = visemes. Its smoothing helper 0x47a1a0(&v, to, k, max) is v += clamp((to − v)/(k + 1), ±max) per call. Arms, eyes and head: docs/worm-reactions.md.

Acting gate (0x5a47d0, assumed): graphic +0x5c counts ms since the last physical event. Events 7/13/14/15/17/19/21/22 reset it and clear WXActor flag +0x6e bit 4. At 90000 ms (0x15f90) the bit is set again (assumed: idle or bored acting allowed).

#### Weapon clips (data: WEAPTWK `WXAnimDraw/Aim/Fire/Holding/EndFire/Taunt/TargetSelected`, fields +0x54..+0x6c of the weapon properties container)

| weapon | Draw | Aim | Fire | Hold | other |
|---|---|---|---|---|---|
| Bazooka | DrawBazooka | AimBazooka | FireBazooka | HoldBazooka | TauntBazooka |
| Grenade | DrawThrown | AimGrenade | FireThrown | HoldThrown | TauntThrown |
| Cluster | DrawCluster | AimGrenade | FireCluster | HoldCluster | – |
| Banana | DrawBanana | AimGrenade | Fire1Banana | – | – |
| Holy | DrawGrenade | – | Fire2Grenade | – | – |
| Gas | DrawGasgrenade | – | FireGasgrenade | – | – |
| Homing | DrawHomingMissile | AimHomingMissile | FireHomingMissile | – | TargetSelected AimLockHomingMissile |
| Shotgun | DrawShotgun | AimShotgun | FireShotgun | HoldShotgun | TauntShotgun |
| Sniper | DrawSniper | AimSniper | FireSniper | HoldSniper | TauntSniper |
| Bat | DrawBat | AimBat | Fire2Bat | HoldBat | – |
| FirePunch | DrawFirepunch | HoldFirepunch | Fire2Firepunch | HoldFirepunch | – |
| PoisonArrow | DrawBow | AimBow | WindupBow | HoldBow | EndFire FireBow |
| Airstrike-like | DrawAirstrike | – | – | HoldAirstrike | TauntAirstrike |
| Dynamite | DrawDynamite | – | FireDynamite | – | – |
| Landmine | DrawLandmine | – | FireLandmine | – | – |
| Sheep / SuperSheep | DrawSheep | – | FireSheep | – | – |
| OldWoman / Scouser | DrawOldWoman / DrawScouser | Struggle | – | – | – |
| NinjaRope | DrawNinjarope | AimBazooka | FireNinjarope | – | – |
| Girder | DrawGirder | HoldGirder | – | HoldGirder | – |
| Flood | DrawRainDance | HoldRainDance | FireRainDance | – | – |
| SkipGo | DrawSkipGo | – | – | HoldSkipGo | – |
| Surrender | DrawSurrender | – | Tantrum | HoldSurrender | – |
| Redbull, Prod, NMN, SentryGun, Starburst, BubbleTrouble | Draw*/Fire*/Hold*/Taunt* | – | – | – | – |

Other weapon clips hardcoded in the WAE_* entities (strings 0x85d2f6..0x85dc08):

- JetpackFly, JetLeft/JetRight, JetpackBump, AJetpackRotLR;
- FireParachute, ParachuteLR, ParachuteWobble;
- FlyStarburst, FlyRedBull, LobThrown, WindupThrown, Windup, AimFP;
- Nailed{Draw,Hold,Fire}SkipGo;
- {Draw,Hold,Fire,Taunt}WFGun.

The chaining (Draw → Hold/Aim → Fire → EndFire, Taunt after an idle loop, `m_fTauntWeaponLoopTime` in 0x58d0a0 / 0x58de60 / 0x58f620) is **assumed** from the names; not traced.

The gestures (fidget, victory, hurt reactions, death) are WORMACTING EFMV scenes (docs/worm-reactions.md), cast by WXSceneManagerService. The trigger tokens are in WXActor.cpp strings 0x869950..0x869bbc. The exe also queues `Worm.QueueAnim`, `Worm.ResetAnim`, `Worm.ScriptDrawAnim` and `Worm.SurrenderAnim` (message names, not traced).


## 12. Audio: WormsX.fev per-event data (extends §7)

### WormsX.fev (FEV1, ver 0x00400000, 1.3 MB): full layout, `tools/w4m-re/fev.py` [data: parses to EOF, all header counts match]
All integers are LE u32 unless noted. `str` = u32 length (NUL included) + bytes.
- Header: "FEV1", ver, 2 u32 (sizes?), u32 n=0x21, then n (id, value) pairs. These are object counts, verified: 1 banks=117, 2 categories=12, 3 groups=127, 5 params=1011, 6 envelopes=12, 7 envelope points=36, 8 sound instances=4127, 9 complex layers=1048, 10 simple events=1574, 11 complex events=1046, 13 waveforms=4058, 17 sounddefs=3995. Then `str` project "WormsX".
- Banks: count, then {loadmode, maxstreams, u64 hash, str name}.
- Categories (recursive, root "master"): {str name, f32 volume (linear gain), f32 pitch, u32, u32, u32 nchildren, children}. Tree: master/{music 0.501 (-6 dB), Speech/{EFMVDialogue, Speechbanks}, Custom, AmbientEffect, SpotEffect/{EFMVFoley, Weapons, Frontend, Global}}. Every category other than music is 1.0.
- Event groups: u32 nroot (=1, "Master"), group = {str name, u32 nprops, props {str name, u32 type (0 int, 1 float, 2 str), value}, u32 nsubgroups, u32 nevents, **events first, then subgroups**}. Group user props are only "LipSync" (EFMV groups).
- Event: u32 kind (0x10 simple, 0x08 complex), str name, 16-byte GUID, 0x84-byte property block (below), body, u32 ncat (=1), str category path (e.g. "SpotEffect/Weapons").
  - Property block [data = value ranges and names agree; field names follow FMOD Designer, assumed]: +00 f volume (linear), +04 f pitch [assumed], +08 f pitch randomisation [assumed], +0C f volume randomisation [assumed], +10 priority (128; 64 on frontendsfx/click and every Speech/*/Sneeze), +14 max playbacks (1; 4 on ExplosionRegular/Boxed, BombWhistle, CowFall, FireLoop, SteamLoop, TeleportLoop...), +18 = 10000 always (steal priority?), +1C FMOD_MODE (0x08 = 2D, 0x10 = 3D, 0x100000 = log rolloff, 0x200000 = linear rolloff, 0x80000 = world-relative, 0x40000000 = ignore geometry), +20 f 3D min distance, +24 f 3D max distance, +28 flags (0x80000 on all Speech and 36 weapons; meaning unknown), +2C..+54 floats (cone 360/360/1.0 at +4C/+50/+54), +58 max-playbacks behaviour (1; 3 on ExplosionRegular, TeleportLoop, Thud), +5C/+7C f (1.0; 0.25 or 0.2 on the explosions, unknown), +6C fade-in ms, +70 fade-out ms.
  - Simple body: u32 1 + one sound instance.
  - Complex body: u32 nlayers, layers {u16 flags, i16 priority, i16 param index (-1 = none), u16 ninstances, u16 nenvelopes, instances, envelopes}, u32 nparams, params {str name, f velocity (units/s), f min, f max, u32 flags (3), u32, u32, u32 nsustain, f sustain[n]}, u32 (0).
  - Sound instance (58 bytes): u16 sounddef index, f start and f length on the layer's parameter axis (0..1), u32 start mode, **u32 loop mode (0 = loop, 1 = oneshot, 2 = loop and play to end)**, i32 loop count (-1), 4 u32 (0), f volume (1.0), 2 f (-1 on complex events, 0 on simple ones), 2 u32 (2, 2) [data: every "*Loop" event, the music tracks and the ambiences have mode 0, music/Victory has 1].
  - Envelope: i32 parent (-1, or the index of the envelope that shares the DSP), str DSP name ("" = built-in, "FMOD Highpass"), u32 DSP parameter index, u32 flags (0x0C = volume, 0x04 = DSP parameter, 0x14 = pitch?), u32, u32 npoints, points {f x (0..1 across the parameter range), f y (0..1), u32 shape}, 2 u32.
- Sounddef property sets: u32 count=35, each 70 bytes: u32 play mode, u32 spawn min ms, u32 spawn max ms, u32 max spawned, f volume (linear), ..., f at +52 (randomisation?), u16 trigger delay min/max ms at +64/+66  [play mode values: 3 on every 1-waveform def, 2 on most multi-waveform defs (random pick), 0/1/6 rare; enum not confirmed].
- Sounddefs: u32 count=3995, {str "/folder/name", u32 property-set index, u32 nwaveforms, waveforms {u32 type (always 0 = wave), u32 weight (100), str "file.wav", str bank, u32 sample index in the bank's FSB, u32 length ms}}.
- Reverbs: u32 count=1 ("Default"), str name + 132 bytes (I3DL2-like, not decoded). Then a "comp" chunk (u32 size=0x18, "comp", u32 0x10, "sett", 2 f 1.0) that runs to EOF.
- **Looping comes only from the FEV**: no sample in weapons/frontendsfx/global/ambient/mu*.fsb has an FSB loop flag (all mode 0x40200) [data]. The FEV instance loop mode alone decides loop vs oneshot.
- Loudness = event volume × sounddef-property volume × category gain (music = -6 dB) [composition assumed; FMOD multiplies]. The exe's fire-and-forget call (0x604a80) passes an extra 1.0 volume.
- Speech: 1333 events, 3D linear 10..1000. 998 are complex, with a parameter "MultiSelect" 0..N and N instances that split the axis evenly (instance k covers [k/N, (k+1)/N]), one per spoken variant. At event creation the exe looks up the "MultiSelect" parameter handle (0x6f99a2, `getParameter`) and stores it [disasm; where the value gets set was not traced]. 14 speech events name the parameter differently: "MultiStart" on StartTurn of vobuild/voprofe/vocave/voscot/voscous/vothief/vobarre, "MultSelect" on vocowbo/StartTurn, and "param00"/"param01" on a few Victory/WeaponFired/ShortOnTime/NoDamageA. They get no handle and probably always play the variant at 0 [assumed].
- 33 events have no sound instance at all (silent on PC): weapons/{BomberEngine, BubbleMachineHeld, OldLadyLoop, PlasmaBombLoop, TractorLoop, Windmill, ChurchBell, Cow, Chicken, Goat, Horse, Monkey, TRexRoar, zombie, ...}. Run `fev.py | awk -F'\t' '$3=="silent"'` for the list.
- Sample sharing: WildWest{Day,Night} ambience uses the Arabian{Day,Night} samples, CamelotNight uses PrehistoricNight, and SheepFly uses ParachuteLoop.

#### Looping events (all non-speech; EFMV has 6 more: Foley_FuseBurn, Foley_JukeBoxTune, Foley_TMLoop, Foley_WaterLap, Foley_LabLoop, Foley_WaterLap2)
weapons: AlienUfoBeamLoop, AlienUfoEngineLoop, Bomber, ClockFast, ClockSlow, ElecArc, ElectricArching, FireLoop, FliesLoop, FloodRainLoop (rain loop plus a oneshot Thunder delayed 1.5 s; Time param 0..15, the rain fades to 0 between 8 and 12 s), **FuseLoop (sounddef /weapons/Fuse, -4 dB, 3D linear 10..500)**, HolyGrenadeHeld, HoseIntoWater, JetPack, JetpackTakeoff, MineArmLoop, MineMachineOperate, MissileLoop (Time 0..10, volume envelope cuts at 5.03 s), ParachuteLoop (loop plus oneshot Open/Close, param Cycle 0..5), RainLoop, SentryGun (loop plus oneshot End, Cycle), SentryGunHeld, SheepFly, SheepRunLoop, SmallFlames, StarburstRocket, SteamLoop, TapIntoWater, TeleportLoop, TimeMachinePiece, Wind (WindMedium plus WindHeavy, both looping, crossfaded by WindStrength 0..1: silent below 0.3). Also frontendsfx/WormPotLoop, frontendmusic/{FrontendDay, femusic}, **cheer/cheer (loops, -12.9 dB)**, all 10 ambient/*/Ambience, and every music/* track except **music/Victory (oneshot)**. WildWest and Prehistoric use "loop and play to end". GasLoop and BinocularsLoop are **oneshot** despite their names.

#### Volumes worth knowing (event × sounddef dB; category 0 dB except music)
FuseLoop -4, FireLoop -10, SteamLoop -15, TeleportLoop -12, Bomber -9, HolyGrenadeHeld -10, SentryGunHeld -8, ExplosionRegular -3 (3 variants, maxpb 4), ExplosionLarge -12, cheer -12.9, RainLoop -6, music tracks -9..-12 in total (sounddef -3/-6 plus category -6), ambience -3 (Building -9, Camelot/Prehistoric night -12). The full list is in the table below.


Full per-event table (2620 rows): `fev.py`, filter with `-g REGEX`.

### Exe side: parameters, fades, who starts what [disasm unless tagged]
- `0x604a20(event, &inst)` only creates the instance; the caller starts it through the instance vtable. `0x604a80` creates and starts (fire and forget).
- **MultiSelect** (speech variant): XSoundInstance (vtable 0x89a344) stores the handle (0x6f99a2, +0x1C). Its play slot 0x6fde90 reads the range 0..N, sets v = N·(LCG(rand()) & 0xFFFF)/65536, clamped to [0.01, N−0.01], then `setValue` (import thunk 0x6fe5ee) and starts: a uniform variant on every play, no anti-repeat.
- **WindStrength**: WindMeterEntity 0x5fc5a0 creates the `weapons/Wind` loop (0x5fce81) and, on every meter update (0x5fc6e0), sets p = 0.95·p + 0.05·(`Wind.Speed` / `Wind.MaxSpeed`) (ratio at 0x5fbfd0) through XSoundInstance+0x54 (0x6fe460).
- **Time** and **Cycle** are never set by the exe ("Time" only appears in XOM schema records, "Cycle" has no string). FMOD moves them by the FEV parameter velocity: FloodRainLoop 0.0667 × range 15, MissileLoop 0.1 × 10, Cycle 0.2 × 5 (with a sustain point): 1 unit per second each [data; velocity = share of the range per second assumed].
- **Music.FadeIn**: FlowControlService posts it on "Switching to in game", after GameLogic.GameLoadComplete (0x4ee6bb). FrontEndService (HM 0x72b541) sets +0x17c; its update 0x7290b4 (returns 0, so it runs every frame) then adds 0.01 to the music volume +0x180 until it reaches `Audio.Vol.Music` (+0x160, DEFSAVE default 0.6) and applies it to the music instance +0x14c. That is 60 frames, 1 s at 60 fps. Category volumes are set from the options with mgr vtbl+0x44 (1 = +0x164, 2 = +0x160).
- **GameOverLogicEntity** 0x4ffbd0: starts `music/victory` (0x4fff4b) and `cheer/cheer` (0x4fff6a) together when the match is won; state 2 fades the sounds over 1000 ms before GameLogic.GotoFrontEnd.
- **Charge sound**: PowerbarMeterEntity (0x5f6310) on `Weapon.PoweringUpStart` calls 0x5f5c70, which creates one oneshot event by the active worm's weapon type (+0xf4): 0xd `weapons/HomingMissilePowerUp`, 0x1a `weapons/BowCreak`, else `weapons/RocketPowerUp` (all -6/-2/-6 dB, 3D linear), starts it (vtbl+0x10) and re-places it at the worm + 0.7 each update (0x5f6590). It is stopped and released (vtbl+0x14, +8) once the powering flag clears (LaunchPayload, Delete, Binocular messages, 0x5f6541). No parameter, no loop: clips of 1.0 to 1.75 s against a 1.5 s charge [disasm; the 0x1a = bow type is assumed from the event name]. 
**EquipSfx**: WeaponAccessoryEntity 0x5950c0 (BaseWeaponContainer +0x74), called by every WAE_* class on `Accessory.Init` (weapon wielded: selection, turn start) and `Input.TauntPressed`, fire and forget at `WeaponLocator`, at most once per 9000 ms per entity (+0xd4); the HoldLoopSfx (+0x70, SheepHeld, SentryGunHeld, ScouserHeld, OldWomanHeld) starts there too [disasm]. Values per weapon: WEAPTWK; events AirEquip, BazookaEquip, BubbleEquip, DefaultEquip, ShotgunEquip, SniperEquip -12 dB 3D, PotionEquip -11 2D, ScouserArm -7, UmbrellaOpen -12 2D. Ours: weapon in hand or worm change in Aim (main.cpp), 9 s per worm; HoldLoopSfx wired (SentryGunHeld loops, the others are FEV oneshots); T (taunt): see "Weights, play mode and taunt" below.
### Weights, play mode and taunt (sounddefs, EquipSfx, Input.TauntPressed)
- **Weights [data]**: `fev.py --json`, `sounddefs[i].waves[j].weight`, read for every event of `DEFS`. All 100 (OldWomanMutter 20 x5, not in `DEFS`) except **ScouserHeld 100/300/100**. Ours: `Def::w` in `audio.cpp`. Weights only count in the random modes (1, 2); ScouserHeld is mode 0, so they do not apply there [disasm, below].
- **Play mode: where it is read [disasm, fmod_event.dll]**: the first u32 of the 70-byte sounddef property set goes through setter 0x100384d0 into **bits 4..6 of the property word** (ctor 0x10038320: default 3); getter 0x10038490. The loader is 0x1002b510. The wave picker is **0x10038670** (n = wave count at +0x14, weights at [+0x28]+4+0x18*i, last wave +0x1c, state +0x18/+0x20/+0x24); its caller 0x10027fd4 sends mode 5 to 0x10027e10 (programmer-selected) and everything else to the picker.
- **Decoded values [disasm]**: 0 and 3 = sequential, index = (instance state + 1) % n, state at event-instance +0x40 (mode 3 resets it to -1 when the event starts at 0x1001cdcd, so it begins at wave 0; mode 0 copies the property word +0x1c at creation at 0x1001e1f4, 0 in every FEV def, so it begins at wave 1); 1 = weighted random (`rand() % sum(weights)`, cumulative walk); 2 = weighted random, and when it equals the previous pick (sounddef state +0x1c, -1 at start) it takes the next index; 4 = per-instance shuffle (list at instance +0x44, cursor +0x48); 5 = programmer selected; 6 = global shuffle (list +0x24, cursor +0x20, reshuffle 0x10038870 when spent, never starts with the last wave played); 7 = global sequential (+0x18, -1 at start). `rand` here is the CRT one.
- **Names [assumed]**: FMOD Designer 4 names these modes Sequential, Random, Random (no repeat), Shuffle, Programmer selected, "event restart" variants; the numeric-to-name mapping above is from the code, the names are my reading. Data values: 3 on 3965 one-wave defs plus Foley_Splash, 2 on the 23 multi-wave defs (ExplosionRegular, SplashHeavy, ShotgunFire, Teleport, WingFlap, OldWomenFootsteps, Thud, CowFall, BubbleMachineLoop ...) and BombWhistle, 0 on OldWomanHeld / OldWomanMutter / ScouserHeld, 1 on ScouserJump, 6 on GrenadeImpact1 (our "bounce"); 4, 5, 7 are unused in the FEV.
- **Ours**: `Def::mode` (default 1) and `pick()` in `audio.cpp`: mode 2 rows (the ones above) keep a global last-pick, GrenadeBounce is a global shuffle, OldWomanHeld and ScouserHeld are mode 0 (always wave 2, because W4M creates a fresh event instance per play [assumed: a held sound could instead be re-triggered inside one instance]). Not reproduced: the sounddef **trigger delay** u16 min/max at +64/+66 [data] (ScouserHeld 200..1200 ms, OldWomanHeld 200..1600, SheepHeld 1000..3000, OldWomenFootsteps 120, MissileLoop 1000) and the spawn fields (BubbleMachineLoop 500 ms, held sounds 0..1).
- **Taunt key [data]**: `Input.TauntPressed` = DefInputMapping (LOCAL.XOM `InputEventMappingContainer` #1340, `FETXT.Control.Taunt`): keyboard `Key` 20 (DIK_T), no joypad entry among the 81 mappings.
- **WAE_* state machine [disasm]** (HM 0x58c060 is WAE_Standard; the other classes 0x58d0a0, 0x58e6c0, 0x5901c0, 0x595ed0 have the same shape; state at +0xb0; the pose update is 0x58f620, jump table 0x58fe38): **0** holstered (set by `Weapon.ActivateAccessory` and `Worm.CleanUpOnDeactivate`, 0x58c4d3); **1** drawn (`Accessory.Init` plays EquipSfx and sets 1, 0x58c28e); **2** taunting; **5** fire anim (`Weapon.PlayFireAnim` sets 5 and +0xc5 = 1, which blocks the taunt). `Input.TauntPressed` needs +0xc5 == 0 and [WormData+0xf0] == 0 (meaning not traced): in state 0 it replays the EquipSfx (same 9 s limit) and goes to 1; in state 1, if the weapon has a taunt clip (+0xf4), it goes to 2, zeroes the clip clock (+0xf0) and posts `Acting.Trigger` (ctor 0x4d3410, subject 0x7f) with table 0x95f1a8[weapon id] (skipped when >= 0x30).
- **Taunt clip [disasm + data]**: in state 2 (0x58f910 onward) the weapon animator plays `WXAnimTaunt` (+0xf8/+0xfc) at clock +0xf0, which advances by the frame time, while the other layers (draw, hold, aim) are blended out with weight 4 x (length - t) and back in; at clock >= +0xec (= `m_fTauntWeaponLoopTime`, the clip length, asserted != 0) the state returns to 1 (0x58fb1f). **So the Acting scene and the clip combine: both start on the same press.** WXAnimTaunt by weapon [data, WEAPTWK]: Bazooka TauntBazooka; Grenade, ClusterGrenade, BananaBomb, HolyHand, GasCanister, Landmine, ClusterBomb TauntThrown; Dynamite TauntDynamite; Airstrike, SuperAirstrike, ConcreteDonkey, AlienAbduction, Fatkins TauntAirstrike; Flood TauntRainDance; Homing TauntHomingMissile; Shotgun TauntShotgun; Sniper TauntSniper; Bat TauntBat; FirePunch TauntFirepunch; Prod TauntProd; NoMoreNails TauntNMN; PoisonArrow TauntBow; OldWoman TauntOldWoman; Scouser TauntScouser; Sheep, SuperSheep TauntSheep; Starburst TauntStarburst; SentryGun TauntSentrygun; NinjaRope TauntNinjarope; Redbull TauntRedbull; BubbleTrouble TauntBT; Surrender TauntSurrender; empty: Girder, Jetpack, Parachute, SkipGo, Armour, Binoculars, Teleport, ChangeWorm.
- **Table 0x95f1a8 [disasm]**: filled by 0x596830, 4-byte entries by weapon id: 35 TauntMelee, 36 TauntRanged, 37 TauntStrike, 48 none. Melee: Grenade, Dynamite, Landmine, BaseballBat, Prod, FirePunch, NoMoreNails. Strike: Airstrike, Flood, ConcreteDonkey, AlienAbduction, SuperAirstrike. Ranged: Bazooka, ClusterGrenade, HolyHandGrenade, BananaBomb, Shotgun, HomingMissile, Sheep, GasCanister, OldWoman, SuperSheep, Starburst, FactoryWeapon, Scouser, PoisonArrow, SentryGun, SniperRifle. None: NinjaRope, Parachute, Jetpack, SkipGo, Surrender, Redbull, BubbleTrouble. Not written (BSS 0 = TimedPayloadFive, behaviour unknown): Fatkins, ClusterBomb, Bananette, Girder, ChangeWorm.
- **Ours**: T in Aim (`main.cpp`) = state 1 -> 2: `Acting::taunt` (acting.cpp, the table above by our weapon names) plus the `Taunt*` clip as the worm's one-shot act with the held mesh, until the clip ends (T is ignored meanwhile). `tools/w4m-models` now exports the 18 clips `Taunt*+Hold*` (`TauntStarburst` is not in the worm bundle; RainDance, Redbull and BT have no imported Hold clip, so those weapons only get the Acting scene). State 0 (T replays EquipSfx) and state 5 (taunt blocked after PlayFireAnim) are not modelled as states: after a shot our phase leaves Aim, and a weapon change runs Init (EquipSfx, 9 s per worm) in the same frame, so state 0 never receives a key press in our flow. Regenerate `worm.glb` with `w4m-models` after pulling to get the clips.
- **HudAlert**: ActiveWormHudInfoEntity 0x5d79db, fire and forget, when the active-worm panel slides in.
- **ClockFast / ClockSlow**: HudClockEntity init 0x5f0f75 / 0x5f0f86 creates both instances (+0xcc / +0xc8), and no HudClockEntity code reads them again: no start found, so the turn clock looks silent on PC [disasm; a start through another path is not excluded].
- Priority (+0x10) only matters when FMOD runs out of voices: 64 on `frontendsfx/click` and every `Speech/*/Sneeze`, 128 elsewhere.

### Our use (`client/src/audio.cpp`)
- `DEFS`: one row per `Audio::Sfx`, the W4M event its file comes from (`tools/w4m-import` `SFX`), its gain (event + sound definition + category dB), loop, 3D linear min..max in m (20 units/m) and max playbacks. 3D events fade linearly with the distance to the camera (`Audio::listen`) and pan; 2D events ignore it. Past max playbacks the oldest voice is cut. `SPEECH` / `SPEECH_SOFT` for the voices, `TRACKS` for the music.
- Looped while their state lasts (`Audio::loop`, called every frame): FuseLoop, MineArmLoop, HolyGrenadeHeld, cheer/cheer, WormPotLoop. Bomber, SentryGun, SentryGunHeld, RainLoop and ClockFast are loops in the FEV but our events only know their start, so they play one pass of the clip.
- Not reproduced: the 350/500/2000 ms event fades, the femusic 2 s fade-out, priorities.


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
| Dynamite 5, Landmine 8, Sheep 15 | 0x5662eb | PayloadWeaponLogicEntity. When manager flag `+0x8d` is set, it disables control group `Fire`, enables `UtilityFire` (Input.DisableGroup / Input.EnableGroup) and sets global 0x95c36c=1. Meaning (drop with the utility key while walking) assumed | 0x85c6bc | 0x586040 |
| default: ids 0-4, 6, 7, 13, 16-20, 23, 24, 26, 29-33, 44-66 | 0x56642a | looks up the container by name (0x50b8b0) and asserts BaseWeaponContainer. If it IsKindOf PayloadWeaponPropertiesContainer (0x9688e0), creates **PayloadWeaponLogicEntity**; otherwise `assert(false)` at 0x5664ec | 0x85c6bc | 0x586040 |
| kWeaponUndefined 67 | 0x565ee6 | none (clears the current weapon) | - | - |

- Enum ids 44-47 (DoubleDamage, CrateShower, CrateSpy, Armour) have BaseWeaponContainers, so this path would assert on them. They must be applied elsewhere. ArmourLogicEntity and LowGravityLogicEntity have no `0x639b83` creation site. (disasm; where they are applied is not traced)
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
  - **SuperBomber 0x58ae50** spawns ParachutePayloadLogicEntity (vt 0x85be14, HM 0x57a8a0).
  - **Detonate** (Payload slot 20, 0x580f10): if `NumBomblets +0x1a0 > 0`, it creates ClusterGeneratorLogicEntity (vt 0x8588f0, HM 0x551950). That entity's 0x5519d0 spawns **ParabolicPayloadLogicEntity** bomblets. The bomblet container comes from `BombletWeaponName`: assumed, not traced.
  - `GameLogicService::CreateMine` 0x4f9630 (and 0x4f9c40) creates level mines as Parabolic with kWeaponLandmine.

#### Launch point and self-hit exclusion (disasm, data)
- **Payload start** (factory 0x585a29..0x585c35): `IsLaunchedFromWorm` gives pos = worm logical pos (+0x38) + (sin yaw × `LogicalLaunchZOffset`, `Worm.EyeLevelOffset` 15 + `LogicalLaunchYOffset`, cos yaw × Z). Nothing is added along the aim: Bazooka, Grenade, Homing Missile, Poison Arrow, Banana, Cluster, Holy, Gas leave from the eye (Z = Y = 0); Dynamite 13/-10, Landmine 10/-10, Sheep/SuperSheep/Starburst 5/0, OldWoman 7/0, Scouser 10/0 (WEAPTWK). A worm that is not Ambulatory (state +0xf0 != 0) and moving adds its velocity to the launch and starts 30 units ahead along that velocity (0x585bc5). `Weapon.GraphicalLaunchLocation` - pos is only a draw offset (0x57de20).
- **Payload collider** (Payload start 0x582200): collider sphere at +0x28, radius `Radius` +0xe0, flags `ColliderFlags`|8, mask 0x3c37 (0x519c80). It then sweeps one 20 ms frame along its velocity (0x5824b3 → 0x519db0 → 0x516c80) and stores the owner id (`[rec+0x18]`) of **every collider it touches** in the vector +0x11c.
- **Each update** (0x581dc0, called from Parabolic 0x576fc0, Payload 0x5827c0, Walking 0x593580/0x593b30, 0x5887b0): the same 20 ms sweep; a contact whose id is in +0x11c is skipped (0x581ec0 → 0x581f7c), any other is the hit (time 0x95c28c, id, flags). The vector is then **replaced** by this frame's contacts (0x581fe1..0x582009). So the shooter, overlapped at launch, is ignored until a frame where the payload no longer touches it; it can be hit again after that. No arming delay or distance test is involved (StartsArmed 1, ArmingCourtesyTime 0 for every impact payload; the courtesy time is the mine's worm trigger).
- **Sweep primitive**: 0x516c80 / 0x517e20 store the own collider index (0x91e800) and an exclude owner id (0x91e804); the per-collider tests 0x516350 / 0x5164b0 / 0x517630 skip that index, colliders whose flags `[rec+0x14]` miss the mask (0x95c294), and colliders whose `[rec+0x18]` equals the exclude id. Payload sweeps pass id -2 (none). Land rays from payloads (0x57dca0, 0x5750d0) pass mask 0: land only.
- **Guns** (GunWeaponLogicEntity fire 0x55df90): start = worm pos + (0, EyeLevelOffset + `LogicalLaunchYOffset`, 0) + `LogicalPositionOffset` ⊙ aim (all 0 for Shotgun and Sniper: the eye). The land ray (0x55e353, `Range` 9999 steps of 1 unit) is land only; the worm sweep (0x55e3c2 → 0x519dd0) passes the active worm's id (0x5b27e0: `ActiveWormIndex` → logical worm +0x14) as the exclude id, so the shooter is never hit by its own bullet; a land hit sends an ExplosionMessage (0x55e5da: WormDamageMagnitude, WormDamageRadius) that can hurt it. That the worm collider's owner id is the logical worm's +0x14 is assumed (the payload registers its own +0x14 the same way, 0x58248b).

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
| kWeaponClusterBomb, Bananette, LandmineBomblet, FactoryCluster | 4 | none (sub-payloads) | Parabolic via ClusterGenerator (assumed link by BombletWeaponName) |
| kWeaponLandmineCluster | 6 | none | swapped in by Landmine Detonate when DetonationType = Clusters (0x581258) |
| kWeaponFatkinsFood, kWeaponSentryGunPayload | 10/0 | none | not traced |
| kWeaponShotgun, SniperRifle / melee x4 / utilities | 2 / 5 / 1 | see Stage 1 | none |

Doc corrections (disasm):
- The payload class is chosen by container **name** (0x585330), not by container class.
- SuperSheep is launched as **Jumping**.
- **StealInventory belongs to OldWoman** (WormCollideResponse=1). Scouser is 2 = FloatAway, which uses its second mesh `InflatedScouser`. The previous "StealInventory = Scouser" note was wrong.

### WEAPTWK enum fields: values and readers

There is no `FireType` or `DetonationType` field. The enum-typed fields are `WeaponType`, `MeleeType`, `DetonateMultiEffect` and `WormCollideResponse`; `OrientationOption` and `ColliderFlags` are u32. Each field record's `+0xc` points to an enum descriptor `{name, 0, values[]}`, and a value = its index in that list. This is data: MeleeType values 1-4 match the names. Records also hold runtime-filled getter/setter pointers at `+0x18`/`+0x20`, e.g. WeaponType get 0x527970 / set 0x630f10. Game code inlines the reads, so callers of those getters only point back to the schema.

| field (+off) | enum table | values (data) | readers / branches (disasm) |
|---|---|---|---|
| WeaponType (Base +0x34) | WeaponTypeEnum 0x90ca44 | 0 kNoType, 1 kUtility, 2 kProjectile, 3 kTargetted, 4 kThrown, 5 kMelee, 6 kEnvironment, 7 kHitscan, 8 kAnimal, 9 kControlled, 10 kStrike, 11 kMovement | Only inline read found: **0x5973e6**, WeaponAccessoryEntity, on the kWeaponFactoryWeapon container. `==4 kThrown` selects accessory handler 0x595ed0 (the "Base" locator); anything else selects WAE_Standard 0x5901c0. No switch on WeaponType exists in the logic, which dispatches by id (0x565ecc) and by name (0x585330). The data values are mostly descriptive: Shotgun/Sniper are 2, not 7, and Dynamite is 5. (negative result from register tracking: medium) |
| DetonateMultiEffect (Payload +0x154) | DetonationTypeEnum 0x90c8f0 | 0 kDT_Random, 1 kDT_Normal, 2 kDT_Fire, 3 kDT_Clusters, 4 kDT_BigPush | See the Detonate breakdown below |
| WormCollideResponse (Payload +0x158) | WormCollideResponseEnum 0x90ca94 | 0 kWC_Default, 1 kWC_StealInventory, 2 kWC_FloatAway | **0x5931bd** in the WalkingPayload worm-collision handler 0x5930d0 (reached only when `DetonatesOnWormImpact`=0): 0 -> 0x5933ee (default); 1 -> 0x593368: StealInventory 0x592d30, state 3, `Payload.PlayIntermediateAnim`; 2 -> 0x5931f7: state 4, `Worm.OverridePhysics`, `Payload.ChangeToSecondMesh`. Any other value asserts "Unknown worm collision response type" |
| MeleeType (Melee +0x98) | MeleeWeaponEnum 0x90c914 | 0 kNoMeleeType, 1 kMeleeBaseballBat, 2 kMeleeFirepunch, 3 kMeleeProd, 4 kMeleeTailNail | Melee fire 0x568180: **0x568247** `==2` makes the worm say `Punch`, else `WeaponFired` (Worm.Say). **0x568329** `==4` sends `Worm.OverridePhysics` set 0x20. End 0x569b00 at **0x569d4e** `==4` sends `Worm.OverridePhysics` clear 0x20 (-0x21). NoMoreNails = 4 = tail nail |
| OrientationOption (Payload +0x128, u32) | none | 0-3 seen (data: 0 Dynamite/walkers, 1 grenades, 2 bazooka/homing/sheep) | Logic start 0x582200 at **0x5825dd**: `==3` sets spin = SpinSpeed×0.08 ×(1±0.33 rand), else orientation from velocity (0x57f900). Graphic update 0x57c890 switch at **0x57ca2e** (table 0x57cab8): 0 sets angle `+0x3c`=0; 1 copies `+0x30`; 2 adds spin×dt to `+0x3c`; 3 adds spin×dt to `+0x3c` and `+0x44`. Meanings (fixed / follow / spin / tumble) assumed |
| ColliderFlags (Payload +0xcc, u32) | none | 0 or 128 (walkers/animals) | no inline reader found (not traced) |

#### DetonateMultiEffect handling (disasm)

The type is read in Detonate (0x580f10) and in Explode (0x57f140):

- **Store and Landmine override.** 0x5810b6 copies the value to `entity+0x14c`. If the container is `kWeaponLandmine`, scheme value `Mine.DetonationType` overrides it: -1 maps to Random, 0 keeps the container value, 1-4 force that type, and a value greater than 4 asserts.
- **Random.** Random is resolved only for kWeaponLandmine, as `(rand & 3) + 1`, at 0x5811bf.
- **Clusters (3).** The payload swaps its properties to `kWeaponLandmineCluster` (0x581236).
- **Fire (2).** The explosion FX is `WXP_Napalm` instead of `DetonationFx` (0x5813c4).
- **BigPush (4).** Explode 0x57f140 at **0x57f1c6** scales ImpulseRadius and ImpulseMagnitude ×2.0, and WormDamageRadius, LandDamageRadius and WormDamageMagnitude ×0.3. Every term is also multiplied by `rand%MaxPowerUp+1` (1 when MaxPowerUp is 0).
- **Explosion-kind argument.** Explode passes a kind value to ExplosionMessage 0x518ce0: 2 when the name starts with `kWeaponCluster`, 4 when it starts with `kWeaponFactory`, 3 when DetonationType is Clusters (0x57f35e), and 0 otherwise.


### Utilities: Armour, DoubleDamage, CrateSpy, CrateShower, LowGravity [disasm unless tagged]
Crate pickup 0x5c9800 switches on crate type − 0x22 (byte table 0x5c99ac): 44 DoubleDamage, 45 CrateShower, 46 CrateSpy, 47 Armour.
- **Armour**: `Armour.Collected` (0x5c991e); the worm handler 0x5ae1ea sets `WormData.Flags` (+0xEC) |= 0x80, never cleared (rest of the worm's life). Only the explosion handler 0x5ae4f0 tests it: damage = trunc(dmg × `Shield.DamageScale` 0.25) (0x5ae71c–0x5ae77e) and impulse × 0.5 (0x5ae96f–0x5ae98e), both skipped for explosion kind 5. `Damage.Impulse` (0x5ae320: bullets and other direct hits), poison (0x5ac060), fall damage (0x5ac3e0) and Vapourize ignore it. `Armour.ProtectionPercentage` 25 is read only by the AI damage estimate (0x548fc0, from AIPlanAttack 0x49ed30). ArmourLogicEntity (vtable 0x8579d0) only shows the shield on its worm's turns (0x549210 / 0x549270).
- **DoubleDamage**: data key `DoubleDamage` = 1 (0x5c9854) + `DoubleDamage.Activated`; reset by stdlib.lub `DoPostActivity`, so it lasts the rest of the turn; Wormpot.lub sets it every turn [data]. ExplosionMessage ctor 0x518da5 doubles WormDamageMagnitude, ImpulseMagnitude, WormDamageRadius, LandDamageRadius and ImpulseRadius; DamageImpulseMessage ctor 0x518cbf doubles damage and impulse; 0x5abb63 doubles the per-damage cap (75, 150 with a Wormpot flag). Fall damage and poison are not doubled. Order: doubled first, then the armour trunc(× 0.25).
- **CrateSpy**: 0x5c8b20 sets `TeamData.IsCrateSpyActive` (+0x73) for the active team, never reset. CrateGraphicEntity 0x5c5270 then shows a Text3DEntity of the contents 15 units above every crate whose type is not 1 or 3, during that team's turns and on the local / owning client only (0x4d3ed0 / 0x708fdc).
- **CrateShower**: `GameLogic.CrateShower` 0x4fb820 calls CreateRandomCrate 0x4fa4b0 **6 times** (loop at 0x4fb850), track camera on the first only. Wormpot.lub sends it every turn in its crate-shower mode [data].
- **LowGravity**: LowGravityLogicEntity on `Input.FirePressed` (0x5672f0 → 0x567140) sets `Low.Gravity.Multiplier` = `Low.Gravity.OnValue` **0.5** (TWEAK); the mystery crate (0x5cad2a) and Wormpot (0x5d7353) do the same. `GameLogic.Turn.Ended` (0x4fe7a2 → 0x4f24f0) puts back `Low.Gravity.GameDefault` 1.0: the utility lasts one turn. The multiplier scales all gravity (worms 0x5a6d20, payloads 0x582200, crates, parachute, rope, melee, bomber, oil drums); IsLowGravity payloads use `Gravity.Slow` −0.00015 instead of `Gravity` −0.00025, then × the multiplier.


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
| RandomCrateChancePerTurn | +0x12c | % | read by `GameLogic.DropRandomCrate` (assumed) |

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
- DrownFloat 0x5aa130 uses the same timer: it sets 2000 ms when a float/height test passes (0x5aa222; read as "reached the surface", assumed), then -20 per tick, then the same blast and Unspawn.
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
- **What that ray tests** [disasm; flag meaning assumed]: 0x466a20 -> 0x4661a0 -> 0x466880 -> 0x517e20 sweeps the segment against the **collider spheres** (callbacks 0x516350 / 0x5164b0 / 0x517630), not the voxel land: arguments mask = 1, exclude owner id = 0. The worm entity registers its collider with flags 1 (0x5a9bb3: `push 1; call 0x519d30`), so the ray hits worm spheres. The segment ends at the target's own feet, so the target's sphere (centre one radius above the feet, the segment enters it just before its end) is hit: the filter is true for every live worm that passes the xz test. The worm collider (0x5a9ac0, 0x5a9bb3..0x5a9c0a) is flags 1, mask 0x811, owner = logical entity id `+0x14` (0x519d50), radius 10 units centred 5 units above the feet, so the segment end is inside it and the exclude id 0 matches nothing unless an entity id is 0 (assumed not) [disasm]. The camera notes that call 0x466a20 "land only" (mask 0) are a different call: this one is mask 1.
- **Weapon Factory templates** [data + disasm]: 0x599990 loads `kWeaponFactoryHoming` when the factory definition's homing byte (`+0x68`) is set, else `kWeaponFactoryWeapon`, and 0x5983f0 then fills the payload from the player's definition (thrown: weapon type 4, AimThrown / DrawThrown / FireThrown, weapons/Throw; launched: type 2, AimBazooka / HoldWFGun / DrawWFGun / FireWFGun / TauntWFGun, weapons/SecretWeapLaunch; airstrike: type 0xa, HoldAirstrike / DrawAirstrike / FireAirstrike, weapons/BombWhistle, camera FatkinsTrackCamera at 0x5993e0). The fields it never writes come from the template (WEAPTWK): `kWeaponFactoryWeapon` PostLaunchDelay 500, LaunchDelay 0, RetreatTimeOverride -1, Radius 5, CameraId PayloadTrackCamera, LaunchSfx weapons/RocketRelease, DetonationSfx global/ExplosionRegular, EquipSfx weapons/BazookaEquip, DisplayName Text.kWeaponBazooka, CanBeFiredWhenWormMoving 0; `kWeaponFactoryHoming` the same but PostLaunchDelay 0, CameraId HomingMissileFlyCamera, DisplayName Text.kWeaponHomingMissile, LaunchSfx weapons/RocketRelease; `kWeaponFactoryCluster` (sub-payload) PostLaunchDelay 0, Radius 4, EquipSfx weapons/DefaultEquip.
- **Factory byte +0x69 = HomingAvoidLand** [data: schema WeaponFactoryContainer: 03 Homing +0x68, 04 HomingAvoidLand +0x69, 05 EffectedByWind +0x6a, 06 FireOnGround +0x6b, 07 Poison +0x6c, 16 ProjectilePowersUp +0x6d]. 0x598d32: set -> homing props AvoidsLand (+0x20c) 1, Vertical / ForwardLandAvoidanceDistance (+0x1fc / +0x200) 100, Vertical / ForwardLandAvoidanceForce (+0x204 / +0x208) 0.009, Stage2Duration (+0x1ec) 30000, Stage3Duration (+0x1f0) 1000, MaxHomingSpeed (+0x1f4) 0.25, LifeTime (+0x150) 30000, DetonatesOnExpiry (+0x1d8) 1, camera HomingMissileChaseCamera (CAMTWK Chase: Dist 170, DefaultHeight 0.255, HeightSpeed 1.4, MinHeight 0.1, MaxHeight 1); clear -> AvoidsLand 0 and HomingMissileFlyCamera [disasm]. The avoidance step is HomingPayloadLogicEntity 0x5611b0, called by the stage-2 update 0x561730 right after the homing step 0x560eb0 when `AvoidsLand` and not arrived (+0x178) [disasm]:
  - distance to the target < 5 units: +0x178 = 1 and nothing more, ever; distance < ForwardLandAvoidanceDistance: nothing;
  - the probe is vtable +0x58 = 0x57dca0 (HomingPayloadLogicEntity vtable 0x859e7c, slot 22) [disasm]: `0x466ae0(start, step vector, ..., 20 steps)`, a hit when the hit step is <= 20, outputs the hit point (0x952d4c) and normal (0x952d64); the step vector is the whole distance / 20, so the probe is a land segment of that distance, mask 0 (land only);
  - up probe (0, +VerticalDistance, 0), down probe (0, -VerticalDistance, 0), `low` = down hit or y < water level + 5 units, forward probe = velocity direction x ForwardDistance;
  - only if the forward probe hits: force f = (0, y, 0) with y = +VerticalForce when nothing is above, -VerticalForce when the up probe hits and `low` is false, 0 when both hit; then f -= normalise(hit - pos) x ForwardForce; vel += f x 20 (ms per tick);
  - always, after that: vel = normalise(vel) x min(|vel|, MaxHomingSpeed).
  Ours [ours]: `Game::avoidLand`, ray casts on our voxel land instead of the 20-step sampling (the hit point is the surface, not the first sample inside it), forces 0.009 units/ms^2 x 20 ms = 9 m/s per tick, 100 units = 5 m, `Projectile::stage` = arrived.
- **Factory LaunchSfx** [data + disasm]: weapons/Throw is a 3D linear 10..500 unit oneshot, weapons/SecretWeapLaunch a 2D oneshot, both 0 dB in the `weapons` bank (WormsX.fev, `fev.py -g 'Throw|SecretWeap'`); set into the payload's LaunchSfx (+0xb0) by 0x5983f0 for thrown (0x598fc0) and launched (0x599167).

## 15. Bundles (`Bundl*.xom`)

475 files, 627.5 MB, all `MOIK`. Verified over all 475: a sequential type-order walk (below) ends exactly at EOF in every file. [data]

### 1. Header, type table, strings [data]
- 64 B header: `MOIK`, `u32 @4` = `00 00 00 02` (version, same in all bundles), `u32 @24` type count, `@28` container count (equals the sum of type counts in all 475), `@32` root container index (1-based). The root is always an `XGraphSet` (the resource directory, see 3).
- Type records, 64 B each: `TYPE`, `u32 @8` instance count, GUID @16, name @32 (31 chars + NUL). Types with count 0 are abstract bases (`XNode`, `XGeometry`, `XCoordSet`...).
- `GUID` (16 B), `SCHM` (`u32 1`, 12 B), then `STRS`: `u32 count, u32 bytes, count x u32 offsets, blob`. String 0 is `""`.
- Containers follow, grouped by type, in type-table order (not always "descriptors first": in 46-50/57-61/68-92 `XGraphSet` comes after `WXTemplateSet`; in Bundl09 after `PC_LandFrame`).

### 2. Container encoding: why splitting on `CTNR` fails [data]
- Schema classes: `CTNR` + 3 bytes + fields in `pe.py schema` order (derived class first). Header byte 0 is `00` (most), `01` (root `XInteriorNode`, most `XIndexedTriangleSet`, ~3.4k others), `08` or `04` (a few hundred); bytes 1-2 always `00 00`. Meaning unknown. [data]
- Untagged (no `CTNR`, no 3-byte header), custom serialisation: every `X*Descriptor`, `XGraphSet`, `XAnimClipLibrary` (1222 + 1582 + 92 + 49 + 1 + 1 descriptors, 1697 graph sets, 310 clip libraries). Splitting on `CTNR` merges them into the previous tagged container, so every later index is shifted. This is the main failure. [data]
- Schema fields with flag `0x20` (struct offset 0) are absent from bundle data: `XTextureStage` `FourCC` / `MaxMipMapLevel` / `Matrix` (all 3207 `XOglTextureMap`), `XFortsExportedData.BPVictoryLocation` (824/869), `XMultiTexFontPage` `CharCoords/CharSizes/CharKern*`. So flag 0x20 means "field added in a later version, optional". It is the same rule as `SchemeData.AssistedShotSettings`. [data + schema]
- Decoding with the full field list can read into the next container and still land on a `CTNR` (seen in `XFortsExportedData`, where empty arrays are 1 byte each). The correct end is the variant that lands on the *nearest* following `CTNR`. [data]
- No tagged payload in the shipped bundles contains the bytes `CTNR`. Schema end == next `CTNR` for every tagged container, so the problem is only the untagged containers and the variant choice. [data]
- Type objects `pe.py` leaves as hex: `0x96ece8` = bounding sphere (4 f32: centre, radius), `0x96ed00` = bounding box (6 f32), `0x96e9e8` = 2 f32 (tex coords), `0x96eb08` = 3 f32 (normals). [data: exact ends]

### 3. Untagged formats [data; field names from schema where they match, else assumed]
| type | layout |
|---|---|
| `XMeshDescriptor` | varint name, **u16 bundle number**, varint ref -> `XGraphSet` (scene), 2 bytes (`08 00` 655, `00 00` 314, `08 02` 211, `01 00` 36...; meaning unknown) |
| `XBitmapDescriptor` | name, u16 bundle, ref -> `XTexFont` (sprite: image + UV rects), u16 w, u16 h (`80 00 80 00` = 128x128 ...) |
| `XSpriteSetDescriptor` | name, u16 bundle, ref -> `XGroup` |
| `XCustomDescriptor` | name, u16 bundle, 2 bytes (`01 00` 46, `00 00` 3) |
| `XTextDescriptor` | name, u16 bundle, ref graph, u16 glyph count k, 2 B, k x 6 B char map (FE.Font, Bundl03) |
| `XNullDescriptor` | name (`NULL`), u16 bundle (Bundl04 only) |
| `XGraphSet` | varint n, n x (16-byte role GUID, varint ref, varint name) |
| `XAnimClipLibrary` | see 5 |
- The u16 bundle number equals the file number in all 4,947 descriptors. The engine builds the path with `sprintf(fmt, id)`: `AppDataService` 0x4d7aa0 calls resource-manager vtbl+0x60 with `"Bundles/"`, `"Bundl%.2d.xom"` (the library default is `"Bundl%03d.xom"`, set by 0x6acd00 / 0x6af930). Formatting sites: 0x6ae4cf, 0x6ae6dc, 0x6b017f (`movzx word id; push fmt; call 0x63882d`). [disasm]
- No index file exists outside the bundles: no non-bundle XOM holds descriptors. Name -> bundle comes from each bundle's own descriptors. [data; the runtime lookup was not traced]
- **XGraphSet role GUIDs** (first 4 bytes) -> target and name, counts over all bundles:
  `99cc436e` resource directory (root set; refs to descriptors, name = resource name) 2947; `6ae6dbe4` scene root (`XInteriorNode` "world" 1090, `WXTemplateSet` "Templates" 111, `PC_LandFrame`) 1222; `5ce9bd39` `XAnimClipLibrary` 310; `bb62fcf6` `XExpandedAnimInfo` 259; `ffd7103d` `XAnimInfo` 31; `edc28fc5` `XCollisionData` ("Collision Data" 1097 / "Phantom Collision Data" 1080); `5f3094db` `XDetailObjectsData`; `e1d7ef28` `XFortsExportedData`; `9c59206c` `XXomInfoNode` (Maya export info: computer, user, .mb path, date); `ebf58e96` `XPathFinderData`; `9e84c023` `XPositionData`.
- A mesh descriptor -> its graph set -> scene root + clip library + collision. This is the chain `w4m-models` follows.

### 4. Classes present (instances, files) [data]
Scene: `XGroup` 5405, `XInteriorNode` 1106, `XShape` 2003, `XTransform` 1931, `XJointTransform` 1639, `XBone` 1639, `XSkin` 89, `XSkinShape` 145, `XBinModifier` 57, `XChildSelector` 19, `XMatrix` 194, `XSceneCamera` 5. Geometry: `XIndexedTriangleSet`/`XIndexSet`/`XTexCoord2fSet` 2029, `XCoord3fSet` 2025, `XNormal3fSet` 1317, `XColor4ubSet` 665, `XConstColorSet` 44, `XPaletteWeightSet` 137, `XCollisionGeometry` 1083, `XCollisionData` 2177. Material: `XSimpleShader` 1611, `XOglTextureMap` 3208, `XImage` 3110, `XMaterial` 368, `XLightingEnable` 418, `XBlendModeGL` 196, `XZBufferWriteEnable` 157, `XAlphaTest` 110, `XCullFace` 109, `XDepthTest` 105, `XTexturePlacement2D` 40, `XEnvironmentMapShader` 2. 2D: `XTexFont` 1674 (sprite = image + rects, despite the name), `XMultiTexFont`/`XMultiTexFontPage` 1/31 (Bundl03), `XBillboardSpriteSet` 89, `XPlaneAlignedSpriteSet` 3. Anim: `XAnimClipLibrary` 310 (117 files), `XExpandedAnimInfo` 259 (u32 flags), `XAnimInfo` 31 (2 bools: animated alpha/colour). Export metadata: `XFortsExportedData` / `XDetailObjectsData` 869 (empty in practice), `XXomInfoNode` 146 + `XExportAttributeString` 584. Landscape, in 35 files (24-45: theme / custom detail banks): `LandFrameStore` 24711, `DetailEntityStore` 7133, `WXLumpConnector` 2278, `WXLumpBoundBox` 1160, `WXTemplate` 816, `WXTemplateSet` 111, `XPathFinderData` 228, `XPositionData` 212. `PC_LandChunk` 7 / `PC_LandFrame` 5 (Bundl09: girder / exported landscape graphs).

### 5. Animation (`XAnimClipLibrary`, untagged) [data; layout as in w4m-models, stats over all 310 libraries / 1066 clips / 20112 channels]
- varint name (the Maya source path, e.g. `Grenade.xom`), u32 key-type count, key types (u32 type, varint string = node path `a|b|c`), u32 clip count. Per clip: f32 duration (s), varint name, then **always** u32 channel count + per channel u16 key-type index. The 0x100/0x101 "one channel per key type" form handled by w4m-models never occurs. The u16 `0x100` "skip 16 bytes" case never occurs either.
- Channel: 4 flag bytes (`01 01 00 00` 15748, `01 01 01 00` 3745, `01 01 00 01` 574, `01 01 01 01` 45; read as must-contribute, weighted, static, linear), 8 B pre/post infinity (u32 each: `0` constant 19218, `2,2` 888 (cycle assumed), `2,0` 6), u32 key count, keys of 6 f32 `(in-weight, in-angle, out-weight, out-angle, time, value)`: Maya-style Bezier tangents, angle in radians.
- Time in seconds. Most common key spacings: 2.0, 0.5, 1/12, **1/24**, 0.25, 1.0, 1/8, so authored at 24 fps (Maya film). In 721 channels the last key is past the clip duration (sampling clamps).
- Key types (low 24 bits; top byte = axis 0/1/2): `0x103` rotate (euler radians, XYZ, applied Rz·Ry·Rx) 1348, `0x102` translate 940, `0x904` scale 805, `0x104` scale (other variant) 105, `0x401` texture offset 58, `0x1100` texture switch (`XChildSelector`) 19, `0x200` axis 3 (44; unknown, maybe visibility / colour alpha), `0x403` (1; unknown, texture). No quaternion or compressed keys: everything is f32 curves. [data; 0x200/0x403 meaning assumed]
- Rotation encoding: per-axis euler curves on `XJointTransform.Rotate` / `XTransform.Rotate`. The joint matrix is T · R(JointOrientation) · R(Rotate) · R(RotateAxis) · S, as in w4m-models. [data + w4m-models]

### 6. Model containers [data, w4m-models + schema]
The scene graph, geometry, skin and image layouts in `docs/w4m-formats.md` ("Meshes, skeletons and animations") match the exe schema field for field. Schema names: `XIndexedTriangleSet` = IndexSet, Flags, PrimitiveCount, Coord/Normal/Color/TexCoord/Weight sets, BoundBox (6 f32), BoundMode, VertexShader. `XShape` = Flags, Shader, Geometry, SortKey, Parameters[], Pre/PostRenderFunc, Bounds, BoundMode, Name. `XImage` = Name, Width, Height, MipLevels, Flags (u16 each), Strides[], Offsets[], Format, Data[], Palette. `XPaletteWeightSet` = Indices u8[], WeightCount u16, Weight f32[]. `XBone` = PoseMatrix, Transform (4x4), Affine, then XInteriorNode. Uncompressed: f32 positions/normals/UVs, u16 triangle lists. Only `XImage` formats 9/10/11 are DXT-compressed.

### 7. Bundle index
See "Bundles numbering" above (unchanged). Additions from this pass [data]:
- Every bundle names its own resources in its root `XGraphSet` (`99cc436e` entries). `xom.py list Bundles/BundlNN.xom 'Descriptor'` now lists them.
- 24-45 (theme/custom detail banks) are full landscape XOMs (`LandFrameStore` etc.), not only detail meshes.
- Hand/glove meshes are named in the exe as Maya source files (`HandWorms.xom`, `HandAlienBreedP.xom`, `HandDLC*`...: `P` = paired/alt variant, assumed). They match `Gloves.*` in 315-352.

### 8. Relation to `tools/w4m-models`
- Handles: untagged descriptors / graph sets / clip libraries (its `exact()` = the table in 3), schema-sized types up to the next `CTNR`, scene walk (`XGraphSet` -> `XInteriorNode`/`XGroup`/`XSkin`/`XBinModifier`), `XShape`/`XSkinShape`, `XIndexedTriangleSet` + sets, palette weights, `XSimpleShader` first stage -> `XImage` formats 0/1/2, joint/transform/matrix cores, `XChildSelector` (first child only), clips with Bezier eval, 30 fps resampling.
- Skips or ignores: DXT images (9/10/11), `XMaterial` colours, render states (blend / alpha test / cull / z), stages after the first, `XTexturePlacement2D` / `0x401` UV animation (parsed as a key type but not exported), `0x1100` texture switching (first child only), `0x200` / `0x403` keys, pre/post infinity (always clamps; cycle `2` ignored), the channel flags, `XCollisionGeometry` / `XCollisionData`, `XBillboardSpriteSet` / `XPlaneAlignedSpriteSet`, `XSceneCamera`, `XEnvironmentMapShader`, `XAnimInfo` / `XExpandedAnimInfo`, and the descriptor trailing bytes.
- Dead code: the `0x100`/`0x101` channel form and the u16 `0x100` skip (never in data, harmless).
- `exact()` reads `XBitmapDescriptor` as v,+2,v,+4 and `XTextDescriptor` as v,+1,v,v,...; the second only works because the u16 bundle id 3 has a zero high byte. Read it as name, u16, ref, u16 k.


## 16. Network (online multiplayer)

Tags: [data] = strings, RTTI, schema or imports; [disasm] = confirmed in code; [assumed] = inferred, not verified. Times are logical milliseconds (`TaskManager::GetLogicalTime()` = `[[0x96d030]+0x38]`).

### Summary

- Transport: Steam P2P (`ISteamNetworking`) under Team17's **XomOnline** layer. Discovery and lobbies use Steam matchmaking. There is no GameSpy SDK; only a GameSpy-shaped legacy `GameBrowser` remains.
- Topology: client/server. The Steam lobby owner hosts a `XomOnlineServer` and every peer is a `XomOnlineClient`. Traffic travels on named channels (`group~topic`), and game traffic uses `$simchannel$`. Host migration is supported.
- Sync model: **deterministic input-message lockstep with delayed playback**. Every machine runs the full simulation. Only the machine of the player in play generates gameplay input messages. Those messages are timestamped with a future logical time, run locally at that time, and sent to the others, which replay them at the same logical time. Remote machines are throttled so they stay behind the active player's "lead time". At end of turn, a state checksum is compared and any mismatch aborts the game. No world state is replicated.

### Transport layer

- Imports [data]:
  - `steam_api.dll`: SteamNetworking, SteamMatchmaking, SteamFriends, SteamUser, SteamUserStats, SteamHTTP, SteamApps, SteamUtils, SteamRemoteStorage, plus Register/UnregisterCallback and CallResult.
  - `WSOCK32.dll` by ordinal: socket, bind, listen, accept, connect, send, recv, sendto, recvfrom, select, setsockopt, ioctlsocket, gethostname, gethostbyname, WSAStartup and others.
  - `WS2_32.dll`: WSASend, WSARecv, WSACreateEvent, WSAGetOverlappedResult.
- `IXConnection` implementations [data]:
  - `XSteamConnection` (vt 0x8aefec): P2P, with callback `P2PSessionConnectFail_t`. Its fail reasons are logged as "STEAM CONNECT FAIL - remote user has different app ID / doesn't own / no steam connection / not accepting".
  - `XSteamListener` (vt 0x8aeb9c): accepts `P2PSessionRequest_t` ("GOT THE MAGIC OnP2PSessionRequest").
  - `XSteamLocalLoopbackConnection` (vt 0x8af27c): the host's own client.
  - `XTcpConnection` (vt 0x8ae79c).
  - `XListener` (vt 0x89a27c).
- Addresses [data]: `XIp4Address` (`%d.%d.%d.%d:%d`) and `XSteamAddress` (vt 0x8af110). `XSteamManager` vt is 0x8ae34c.
- XomOnline `g_Config` @0x98adf8 [disasm, ctor 0x40369b]:
  - +0: port 1024, formatted as `0.0.0.0:%d` by the server listen code at 0x404b85.
  - +4 and +8: 500 and 500.
  - +0xc: listener class `XSteamListener` (class 0x8ae8e4).
  - +0x10: connection class `XSteamConnection` (class 0x8aebf8).
  - +0x18: 1000.
  - So on PC the "1024" is a port carried over XomOnline on Steam P2P, not a real UDP/TCP port [assumed].
- `XTcpConnection` is used by a debug "Team17 telnet server V0.1" on `0.0.0.0:23` (0x692db1) [data/disasm]. `XUdpPacketPort` (vt 0x8af4f0, broadcast/recvfrom) is only created through the class factory, and no PC call site was found [assumed: unused or LAN legacy].
- `/ENABLEPORTFORWARDING` logs "Universal Plug + Play port forwarding enabled." [data]. The Steam invite command line is `+connect_lobby` [data].
- XomOnlineMachine (one per peer link, `XomOnlineMachineImpl.cpp`) [data+disasm]:
  - Handshake states: UpdateConnect -> WaitName -> WaitKey -> WaitTag. The application tag must match ("XomOnline Default Application V1.0", otherwise "application tags don't match").
  - Commands: `XOMONLINEMACHINE_COMMAND_BEAT` (heartbeat) and `_CLOSE`.
  - Timeouts come from the machine config @0x98aeb0 (ctor 0x407c62): connect 10000 and 20000 ms (+0xc/+0x10, used at 0x408c0c), send-flush 250 ms (+0x14), 1000 (+0x18), heartbeat interval 1000 ms (+0x1c, 0x408b01/0x408962), and receive idle timeout 15000 ms (+0x20, deadline reset at 0x40894f on every receive).
  - Expiry gives "timed out-Machine1..5" and `HRESULT 0x80210001` (UpdateOpen 0x40803c).
  - Packets: header with a "big header" variant, `kPacketSizeLimit`, and a send store flushed with partial sends allowed.
- XomOnlineServer/Client [data]: commands `XOMONLINE_SERVERCLIENT_CMDADDCHANNEL/CMDREMCHANNEL` and `XOMONLINE_CLIENTSERVER_REQADDCHANNEL/REQREMCHANNEL`. Peers are identified by tag<->id pairs (id < 256). Clients route packets "from %d to %d" through the server. A channel has `SendToOwner` and `SendToGroup`.

### Online object model (XomOnline, Steam backend)

| class | vtable | role |
|---|---|---|
| `XomOnlinePlugInDriver` | 0x8bce3c | Top-level online state machine: title, sign-in, online main, QuickMatch, OptiMatch, RankedMatch, CreateLobby, Lobby, HostMigration. Error injection. Host migration 500 ms after the host is lost (0x409c57) [disasm] |
| `WXomOnlinePlugInService` | 0x86e15c | Game-side bridge (UI messages `xo.*`, `WXNET.Check*`, `Net.RankedMatchSetup`, `Net.PostWinMatchScreen`) |
| `steam_XomOnlineMatchImpl` (`XomOnlineMatch`) | 0x8bda2c | Creates or publishes the Steam lobby (`"%s's lobby"`). Lobby data keys: `player_limit` (<=4), `is_ranked`, `preview_data`, `host_name`, `Rank_Higher`, `Rank_Lower`, `xo.public_open/filled`, `xo.private_open/filled` |
| `steam_XomOnlineFinderImpl` (`XomOnlineFinder`) | 0x8bdfec | `RequestLobbyList` with filters (`LobbyMatchList_t`); JoinInviteMatch |
| `steam_XomOnlineSessionImpl` (`XomOnlineSession`) | 0x8bd504 | Enters the lobby (`LobbyEnter_t`). The lobby owner is the server ("local match, creating server"); others start a client. Opens channel `session`. Max 4 lobby members. CheckViability ("Session no longer viable") |
| `XomOnlineServer` / `XomOnlineClient` / `XomOnlineMachine` / `XomOnlineChannel` | 0x8bac94 / 0x8bb2fc / 0x8bb8ec / 0x8bdc94 | Transport described above |
| `steam_XomOnlinePlayer/PlayerSet/Gamer` | 0x8ba1b4 / 0x8be374 / 0x8bd69c | Players and their join/leave state machines |
| `steam_XomOnlineHostMigrationImpl` | 0x8be658 | New lobby owner becomes host ("migration started, becoming new host (%s)") |
| `SteamLobby` (entity, `WXSteamLobby.cpp`) | 0x8b72b0 | Callbacks LobbyChatUpdate, LobbyDataUpdate, PersonaStateChange; "AUTO SET PLAYERS READY" |
| `WXSteamLobbyBrowser` (service) | 0x8b73f8 | `GameLobbyJoinRequested_t` (Steam overlay invite), enters lobby |
| `PCPlatformManager` | 0x86d388 | `GameLobbyJoinRequested_t` ("Going to Online Lobby from out-of-game invite") |
| `GameBrowser` (`GameBrowserImpl.cpp`) | - | GameSpy-style legacy: `GS.*` messages, keys `gamever authresponse privateip publicip firewall hostport openstaging`, data resource `Net.ServerPort` (+0x20). `GS*` containers are GSRoom, GSPlayer, GSNetworkGame, GSTeam and others [data]; its PC use is [assumed] marginal |
| `NetService` (`NetService_Xbox.cpp`, shared with Xbox) | 0x89b3ec, ctor 0x705bce, instance `[0x979ddc]` | Game-level networking (below) |
| `NetThrottle` | 0x89b1fc, Update 0x7082ca | Time throttling of remote simulation |
| `ReplayMessageStoreService` | 0x8569ac, Update 0x542f00 | Timestamped message store: send/receive/replay of input |
| `NetNameStorageService` | 0x89d3f4 | Player-details lookups |
| `SteamVoiceService` | 0x8696fc | Push-to-talk; carried by `NetSteamVoice*Msg` |
| `NetworkDebugService` | 0x8565d4 | `Net_%s.log` |
| `SpectatorMasterLogicEntity` / `SpectatorCam` | 0x851e28 / 0x855ddc | Spectator input ("NetSpectatorInput enabled") |
| `NetworkIndicatorEntity` | 0x866c88 | HUD lag/busy indicator (`Net.Busy`, `Net.NetworkBusy`) |

### Net messages (`BaseNetMsg`, XOM-serialised with `NetStream.cpp`)

`NetService` ctor 0x705bce registers the classes with `NetStream::RegisterClass` 0x70daf0, at most 256 classes. The registration index is the class id written on the wire [disasm; the wire id is assumed].

| idx | class | fields (schema) | use |
|---|---|---|---|
| - | `BaseNetMsg` | MsgId u16 | base |
| 0 | `StringNetMsg` | Text | generic |
| 1 | `ClientGameLoadCompleteMsg` | - | client finished loading the level |
| 2 | `ClientFeLoadCompleteMsg` | - | client back in the frontend |
| 3 | `BuggerOffMsg` | - | kick |
| 4 | `ClientMsg` | DispatchTime u32 | base of timed messages |
| 5-10 | `NetStored{,String,Int,TwoInt,Float,TwoFloat}MessageArray` | Msg u32[], Time u32[], plus Text[] / Value1[] / Value2[] | **batched input messages with logical timestamps** |
| 11 | `ClientTimeSyncMsg` | (DispatchTime) | active player's clock, every 1000 ms; -1 at end of turn, -2 at end of game |
| 12 | `GameDataSetupMsg` | SchemeData ref, LevelName, LevelSeed, LogicalSeed | host -> all before load (0x70ad51 "sending level seed / logical seed") |
| 13 | `GameStateValidationMsg` | WormIndex, SourceOfValidation, Random, WormCheck[] (`WormDataChecksums`), TaskVerificationString, Alliance/Team/Worm inventory checksums, camera checksums and floats, RoundTime, HotSeattime, TurnTime | desync check |
| 14-18 | `ClientToClientMsg` (DestClient), `AllianceMsg`, `GlobalChat`, `GlobalAnonChat`, `GlobalActionChat` | SourceClient, Content | chat |
| 19-21 | `NetSteamVoiceData/Start/EndMsg` | VoiceData u8[] | voice |
| 22 | `LobbyDataChangeMsg` | LevelTheme, LevelName, LevelSeed | host changed the landscape in the lobby |
| 23-24 | `PlayerChat`, `AllianceChat` | SourceClient, Content | chat |
| 25 | `WormDataChecksums` | per worm: Energy/Pending/Initial/Current, Pos/Rot/Vel/LastColNorm checksums, SlopeAngle, WeaponAng, WeaponFuse, Weapon, Team, Poison, AfterTouch, GunWobble | element of validation |
| 26 | `ClientSurrenderMsg` | SurrenderingPlayerIndex | surrender (`Team.Surrender`) |
| 27 | `ClientReplayRoundMsg` | Reason | "NetClient returning to frontend - Replay round" |
| 28 | `TeamMessage` | Nick, Team ref, Action enum, Alliance, WormCount, Handicap, InvertX/Y/YFP, Sanctioned | lobby team add/remove/update |
| 29 | `ClientRemoveMsg` | ClientNick | player removed |
| 30-32 | `ClientSavedMapMsg`, `ClientHasDlcMsg` (DlcOwned), `ClientHasReadForRank` | | map, DLC and ranked handshakes |
| 33 | `HostInfoMsg` | HostNick | after connecting or migrating |
| 34 | `LatestMessagesMsg` | latest time per stored list (6 u32) | host migration resync |
| unreg. | `GameStartMsg`, `GameStateValidationHashMsg` (SourceOfValidation, Hash0-3), `PlayerMessage` (Nick, Player, Action), `BrowserWelcomeMessage` (Host, Level, Theme, TimeOfDay, LogicalSeed, Wormpot1-3, UniqueId), `BrowserReadyStateMessage` (Nick, IsReady), `AvatarMetadataMsg` | | GameBrowser path or unused [assumed] |

Other team/lobby containers: `NetworkTeamData : StoredTeamData` (+WormCount, Alliance) and `NetworkGame` (State enum, Timeout).

### Game-message IDs used by NetService

The IDs are globals at 0x979de0-0x979f18, filled by the ctor at 0x7f7d30 [disasm]:

| global | message |
|---|---|
| de0 | GameLogic.Win |
| de8 | GameLogic.Draw |
| df0 | GameLogic.GameLoadComplete |
| df8 | GameLogic.Turn.Started |
| e00 | GameLogic.Turn.Ended |
| e08 | GameLogic.EndTurn |
| e10 | Net.BeginGame |
| e18 | Net.Close |
| e20 | Net.DisableAllInput |
| e28 | Net.AddPlayer |
| e30 | Net.RemovePlayer |
| e38 | Net.RoundEnded |
| e40 | Net.PostWinMatchScreen |
| e48 | Net.RankedWinMatchScreen |
| e50 | GS.AddTeam |
| e58 | Net.LocalSelectTeam |
| e60 | Net.Lobby.PlayerSelected |
| e68 | Net.Lobby.ViewPlayerDetails |
| e70 | Net.Lobby.KickPlayer |
| e88 | GameLogic.StartGame |
| e98 | GameLogic.EndTurn.Immediate |
| ea0 | Net.ConnectOkay |
| ea8 | Net.ConnectFailed |
| eb0 | Net.Stalled |
| eb8 | Net.ShutdownComplete |
| ec0 | Team.Surrender |
| ed8 | Net.NetworkBusy |
| f10 | Net.AllPlayersLoaded |
| f18 | Net.AllRankedPlayersHaveRead |

`NetService::ProcessMessage` (0x70b943) dispatches on these IDs [disasm]:

| message | handler |
|---|---|
| GameLoadComplete | 0x70786b (logs "Logical Rand at beginning of game") |
| Turn.Started | 0x709827 |
| Turn.Ended | 0x707bc5 (sends the -1 time sync) |
| Net.BeginGame | 0x70933a |
| Net.Close | 0x707b42 |
| GS.AddTeam | 0x7074b7 |
| LocalSelectTeam | 0x7072c5 |

Other string-only messages: `Net.CreateServer`, `Net.ConnectToServer`, `Net.RequestHostConnect`, `Net.ClientConnected/Disconnected`, `Net.ConnectionTimer`, `Net.CullDisconnectedPlayers`, `Net.RoundEndedInError`, `Net.WaitingForPlayers`, `Net.SessionViable`, `Net.ClientAbortGame`, `Net.CloseClient`, `Net.Client.TimeToDie`, `Trigger.Net.Host/Join` (SamStartupService autostart) [data].

### Lobby flow (host/join)

- Frontend menus `WXNET.*` [data]:
  - Navigation menus: MainMenu, GameList, GameLobby, HostGame, CustomCreate, Optimatch, QuickGameResults, OptiGameResults, Invitation, SignInSignOut.
  - In-match screens: PreRound, WinRound, WinMatch, DrawMatch.
  - Lobby UI resources: `WXNET.Lobby.Player%d.Name/Team`, `WXNET.Team%d.Ready`, `WXNET.Lobby.Timer`, `WXNET.LobbyFull`, `WXNET.Lobby.InviteFriends`, `WXNET.GamePassword`.
  - Popup prefix `WXNETP.*`. Templates live in `MENUTWKXNET`.
- Host [data, order from strings]: QuickMatch finds no match -> host screen, or CreateGame -> `XomOnlineMatch` creates the Steam lobby -> the session (lobby owner) creates the `XomOnlineServer` on port 1024 and its own loopback client.
- Join [data]: the Finder lists lobbies or an invite arrives -> JoinLobby -> client connects over Steam P2P to the owner -> machine handshake -> channels.
- In the lobby [data]:
  - Teams: `TeamMessage` (add/remove, alliance, handicap).
  - Landscape: `LobbyDataChangeMsg`.
  - Others: chat, ready flags, kick (`Net.Lobby.KickPlayer` -> `BuggerOffMsg`, with the `WXFEP.ViewKick` popup), `HostInfoMsg`, and DLC/map checks (`WXNET.CheckScheme/CheckPlayers/CheckWormpot`).
- Start sequence (NetService state strings) [data]:
  1. "initialised and waiting for contact player".
  2. "Sim channel open, waiting for game start" (`$simchannel$`).
  3. Host: "all players connected, sending team and scheme data" -> `GameDataSetupMsg` (scheme, level, LevelSeed, LogicalSeed).
  4. "game about to start, waiting for final connections" -> session set non-joinable -> `Net.BeginGame`.
  5. Each client loads and sends `ClientGameLoadCompleteMsg`.
  6. "all players' games have loaded, zabingo!" -> `Net.AllPlayersLoaded` -> play.
  7. After Win/Draw: "waiting for unload" -> "all players unloaded, now waiting for game start" (next round).
- Random seed: the host's LogicalSeed seeds the shared logical RNG and LevelSeed seeds land generation [data]. Both are sent in `GameDataSetupMsg`.

### In-game synchronisation (lockstep of input messages)

- Recording [disasm]:
  1. `InputTranslationService` send helper 0x5056b0 (typed variants 0x5057a0 and others) drops the message when 0x505270 says so. That happens at logical time 0, or for two specific IDs (`[0x95b2f8]`, `[0x95b300]`) when NetService exists and 0x708fdc refuses them. It dispatches immediately when 0x505410 says so: offline, local-only messages, or "SendMessagesImmediately ... while paused in an online game".
  2. Every other message goes to `ReplayMessageStoreService::Schedule` 0x542740 with `tTime = (now/20+1)*20`, the next 20 ms boundary.
  3. Schedule rounds `tTime` up to 10 ms and asserts `tTime >= now` and `tTime - now < 30000`.
- Transmission [disasm]: the store Update 0x542f00 handles the six lists (plain, string, int, two-int, float, two-float). Each pass dispatches due messages locally (warning "A message is being sent too late: Scheduled/Actual time" 0x53ef50), keeps `m_FullTurn_*` copies, and 0x5403a0 batches new entries into `NetStored*MessageArray` and sends them through `NetService::SendMessage` 0x70621d.
- Remote playback [assumed from structure]: the receiver inserts the arrays into the same store and dispatches each message when its own logical time reaches `Time[i]`, so all machines run the same messages at the same tick.
- Time sync and throttle [disasm]:
  - While the local player is in play, NetService 0x707155 sends a `ClientTimeSyncMsg` every 1000 ms (`[0x947468]`). With a debug flag it also sends a `GameStateValidationMsg`.
  - `NetThrottle::SetLeadTime` 0x70594b stores the lead time at +0x24. A value of -1 means end of turn ("others run till end of turn") and -2 means end of game.
  - `NetThrottle::Update` 0x7082ca, when the lead time is valid and the local player is not in play:
    - It pauses when `local + 2000 > lead` (`[0x94746c]`, "PANIC, game paused") and unpauses when `local + 3000 < lead` (`[0x947470]`, "RELAX").
    - Otherwise it sets the sim speed to `clamp((lead - local)/5000, 0.3, 3)` (`[0x947474]`), smoothed as `0.95*old + 0.05*new`.
    - "local player now in play" or -1/-2 means full speed.
  - In practice spectators run about 2-5 s behind the active player.
- Who simulates: every peer runs the full deterministic sim. Only the current player's peer produces gameplay input. `Net.DisableAllInput`, sent by Lua `stdlib.lub` DoPostActivity at end of turn, stops net input and starts end-of-turn validation [data+disasm].
- Desync detection [data]:
  - "Send End Of Turn Validation Request @", then "doing end of turn validation message at time": `GameStateValidationMsg` (RNG `Random`, per-worm checksums, inventories, camera, timers) or `GameStateValidationHashMsg` (4 x u32).
  - The receiver queues the message until its own time matches ("Have a queued validation message with time ..., but current time is").
  - Results:
    - Success logs "End of Turn Validation OK."
    - Mismatch: `Net.Error.TurnEndValFailed`.
    - Time mismatch: `Net.Error.InvalidValTimestamp`.
    - Missing message: `Net.Error.MissingTurnEndVal`, tolerated if the player surrendered.
    - "NETW**K TIMES NOT IN SYNC" at 0x709134.
  - Duplicate validation messages are discarded.

### Timeouts, disconnects, migration

- NetService abort handler 0x70864c ("returning to frontend - Game ABORTED (HR=...)") maps HRESULTs to messages [disasm]:

  | HRESULT | message |
  |---|---|
  | 0x802100c9 | `Net.Connect` |
  | 0x802100cd | `Net.RemovedFromSession` |
  | 0x8021012c | `Net.OutOfSynch` |
  | 0x8021012d | `Net.NotViable` |
  | 0x8021012e | `Net.ExcessiveLag` |

  XomOnlineMachine socket down or timeout is 0x80210001.
- UI errors [data]: `xo.txtErrConnectTimeout*`, `xo.txtErrKicked*`, `xo.txtErrIncompatibleNat*`, `xo.txtErrNoSlots*`, `xo.txtHostMigration*`, `GS.Error.HostTimeOut`, `GS.Error.Add/Remove/RequestTeamsTimeout`.
- Host migration [data]:
  1. Host lost -> "Host Migration detected, pausing activities" and "Discarding latest messages in case they are incomplete".
  2. The new host (new Steam lobby owner) re-sends `LatestMessagesMsg`, "Resending all input messages from current turn" (0x70a0f9 -> 0x5408a0 re-sends the `m_FullTurn_*` lists), and "Resending End of Turn time sync and game state validation message".
  3. "Host Migration complete, resuming".
  4. If the host left during its own turn, its teams surrender ("surrendering non local teams"). Host lost in the lobby aborts the session.
- Surrender [data]: `Team.Surrender` -> `ClientSurrenderMsg`, and `SurrenderPlayerInTurn` when the current player is gone.


## 17. Frontend (`WXFE_*`): menus, screen state machine

(tags: [data] = read from tweak/exe data, [disasm] = read from code, [assumed] = inferred)

### Data sources
- Menus are pure data: `WXFE_MenuDescription` trees in the menu tweak databanks [data]. The databank name table at 0x91e15c (.data) lists, for PC: `MenuTwk`, `Persist`, `PersistPC`, `PersistNet`, `MenuTwkX`, `MenuTwkX2`, `MenuTwkXSteve`, `MenuTwkXInGame`, `MenuTwkXNet`, `MenuTwkXPCCommon`, `MenuTwkXPCEuro` (labels "Menu Tweak X", "Menu Tweak PC Only", "Menu Tweak Online", "Persistant"...) [data]. XBOX/PS2/PS3 files are not loaded by the PC exe [assumed: absent from the table].
- Resource naming [data]: `WXFE.<Menu>` = full-screen menu, `WXFEP.<Name>` = popup, `WXNET.*` / `WXNETP.*` = network menus/popups, `<Menu>List` = `WXFE_ListBoxContents` the menu's list control binds to by `GameDataId`.

### Classes (RTTI, vtables)
Two parallel hierarchies: serialised `*Desc` containers (XOM, data) and runtime `*Entity` tasks built from them [data: RTTI].
- Desc: `WXFE_BaseItemDesc` (0x87cc30: ItemName, EnableId, ControllerId, ChildrenItems) < `WXFE_BaseGfxItemDesc` (0x87cc98: Position/Scale/Orientation, Anim_Incoming/Outgoing (enum), Anim_Spot, Audio_Incoming/Outgoing (enum), Audio_Spot, Delay_Incoming/Outgoing (u32), FontSizeOverride, LayerOffset, LOCKED) < `WXFE_BaseInputItemDesc` (0x87ceac: ToolTipId, Navigate_Left/Right/Up/Down (item names), Anim_Activated/Highlighted, Audio_Activated/Highlighted).
- Screens: `WXFE_MenuDescription` (0x87cd54, base GfxItemDesc): BorderType, BorderAutoSize, BorderEdgeSize, Messages_BeforeMenuDisplayed / AfterMenuDisplayed / MenuGoingAway / CancelPressed / AcceptPressed, DefaultSelectedItem, FullScreenColour -> runtime `WXFE_MenuEntity` (vt 0x8aa6e4, +0x20 0x8aa684) < `WXFE_BaseMenu` (0x8add84) < `WXFE_BaseGfxItem` (0x8a9bdc) < `WXFE_BaseItem` (0x8aa10c).
- Input widgets (`WXFE_BaseNavItem` 0x8adcec, navigable): `WXFE_ButtonEntity` (MenuButtonDesc: ResourceName text, Messages_Highlighted/Selected, Border_*, SquareButton, OnRelease, colours), `WXFE_ListControlEntity` (ListControlDesc: GameDataId -> `WXFE_ListBoxContents`, AutoScroll, LoopContents, SortContents, arrows), `WXFE_DetailButtonEntity` (ListDetailDesc: ListDataId + Index = one row of a list shown as a big button; the `DETAIL *` items of every submenu), `WXFE_TextBoxEntity` (TextBoxDesc: font, TextGameDataId, AutoScroll*, colours), `WXFE_TextEditBoxEntity` (TextEditBoxDesc: MaxTextWidthChars/Pixels, PassWord, NumbersOnly, InvalidChars, SoftwareKeyboardTitleID), `WXFE_ButtonHelpEntity` (ButtonHelpDesc = TextBox: `Hint_Select` / `Hint_Back`), `WXFE_HowToPlayTextBoxEntity`, `WXFE_SoftwareKeyboardEntity`, `WXFE_InputEntity` (InputDesc: raw Messages_Selected/Cancel/Left/Right/Up/Down), `WXFE_LandscapeEntity` (LandscapeBoxDesc: generated landscape preview), `WXFE_GalleryThumbEntity`, `WXFE_FlagObjectEntity`, `WXFE_TrophyEntity` / `WXFE_TrophyCabinetEntity`, `WXFE_WormpotControlEntity`, `WXFE_BriefingEntity` [data: RTTI+schema].
- Display-only (`WXFE_BaseGfxItem`): `WXFE_MeshObjectEntity` (MeshObjectDesc: MeshName + Anim_In/Spot/Out/Looping_Mesh clip names, Audio_*_Mesh, Delay_In/Out_Mesh, Waiting* placeholder), `WXFE_MeshObjectParticleEntity` (+EffectNames/LocatorNames/SpawnDelay/SpawnLoopTime), `WXFE_ImageViewEntity` (ImageId, WaitTime), `WXFE_TextScrollerEntity` / `WXFE_NewsFeedScrollerEntity` (ScrollingTextDesc: Speed, Wrap, ScrollForward, AlwaysScroll; the ticker), `WXFE_TitleControlEntity` (TitleControlDesc: TextResourceID, TextAnim), `WXFE_WormEntity` / `WXFE_ShopControlEntity`, `WXFE_PlayerRepresentationObjectEntity`, `WXFE_GalleryImageEntity`, `WXFE_GetControllerInputEntity`, `WXFE_WeaponFactoryMeshEntity`, `WXFE_ListBoxElementsEntity` [data].
- List rows: `WXFE_ListBoxContents` (Rows[], TotalRows, VirtualOffset/Enabled, CallBack) -> `WXFE_ListBoxRows` (Border_*, Anim_Highlight/Clicked, Messages_Selected/RightClick/Highlighted/Left/Right, RowHeight, GapBetweenItems, Decorative, Sort/Index/Enable, Columns[]) -> columns `WXFE_StringColumns` (Text key, Font, FontSize, Width(%), Justification, colours, TextAnim), `WXFE_StringTableColumns` (+CallBack), `WXFE_FlashString*Columns` (FlashOn/FlashOff), `WXFE_IconColumns` / `WXFE_HighlightIconColumns`, `WXFE_MeshColumns` / `WXFE_MeshAttachmentColumns`, `WXFE_PowerColumns` (slider-like bar: PowerNumber, Style kPCS_Normal/Grow/...ZeroDisable), `WXFE_TallyColumns` (TallyNumber icons), `WXFE_GapColumns` (GapWidth, kGAP_Normal/Dots/Line). Runtime row items: `WXFE_Item_Strings/StringTable/FlashStrings/FlashStringTable/Icon/IconHighlight/Meshs/MeshsAttachment/PowerBar/Tally/Gap` (vt 0x8ae028 family) [data]. There is no dedicated slider class: option values are list rows with Messages_Left/Right and a PowerColumns/StringColumns value [data; "slider = PowerColumns row" assumed].
- Borders (pure code, no XOM): `WXFE_Border` (0x89f43c) + 50 subclasses: Nav (Back/Cross/Tick/Start/ArrowLeft/ArrowRight x Normal/Highlight/Disabled), Button (S/B x Normal/Highlight/Disabled), Text (Normal/Highlight/Disabled/Edit/Active), List (Blue/Orange), Mem, Paper (Normal/Shadow), GfxPaper, NamePlate, Charcoal, Armoury, Bubble, ComPanel; selected by `WXFE_BorderTypeEnum` (table 0x90f1c0: kMT_NoBorder 0, FrameBorder, BasicBorder, ArmouryBorder, Blue/OrangeListBorder, Text{Normal,Disabled,Edit,Highlight,Active,Charcoal}Border, ButtonSmall/Big{Normal,Disabled,Highlight}, Nav{Start,Tick,Back,Cross}{Normal,Disabled,Highlight}, NavArrowLeft/Right{Norm,Highlight,Disabled}, BubbleBorder(NoPointer), PaperBorder, BorderNamePlate, ComPanelBorder, Paper{Normal,Shadow}Border, Memory*, ComPanelBorderTrans, kMT_LAST) [data].
- Services: `FrontEndService` (vt 0x89f8c0, ctor 0x726367, singleton 0x97a600, HandleMessage 0x72a3e9 (id 0x40 -> subscribe 0x729ac9, id 0x42 -> shutdown 0x7293dd), CreateNewMenu 0x726bbe, AnimDivide 0x728a47), `PopUpService` (0x89f61c, HM 0x7259ad), `WXFE_DataSetupService` (0x8a626c, `WXMsg.SetupData/Request/ControllerSetup`), `AutoRepeatService` (0x8a7148, menu key repeat), `InGameMenuBackgroundService` (0x8270e4, LoadBack*.tga behind in-game menus), `NewsFeedService` (ticker `WXFE.TickerTape`, default `WXFE.TickerTapeDefault`) [disasm/data].

### Enums (name tables in .data, index = value) [data]
- `WXFE_ControlAnimsEnum` (0x90f290): 0 None, 1 Click, 2 Click_Error, 3 Highlight, 4 In_BigBounce, 5 In_Flipy, 6 In_Next, 7 In_Prev, 8 In_ScaleHitXY, 9 In_ScaleY, 10 In_SlideX, 11 In_Speech, 12 In_SpringX, 13 In_SpringXY, 14 In_TitleUnderline, 15 In_ToolTip, 16 Out_Next, 17 Out_Prev, 18 Out_ScaleX, 19 Out_ScaleXY, 20 Out_ScaleY, 21 Out_Speech, 22 Out_TitleUnderline, 23 Out_ToolTip, 24 Reset, 25 Title_Scroll, 26 Wide_Highlight, 27 In_Chat, 28 Out_Chat.
- `WXFE_ControlAudioEnum` (0x90f318): 0 None, Cancel, Click, Click2, Click3, Error, Grenade, Highlight, In_BigBounce, In_Book, In_Controller, In_CrateDice, In_Custom, In_Net, In_Next, In_Prev, In_ScaleHitXY, In_ScaleY, In_SlideX, In_SoundVid, In_Speech, In_SpringX, In_WeaponFactory, In_WormPot, Out_Book, Out_Next, Out_Prev, Out_Scale, Out_ScaleXY, Out_ScaleY, Typewriter, WormPot_{Button,HandlePull,Intro,Nudge,Outro,Spin_Loop,Spin_Start,Spin_Stop}, Page_Turn, Typewriter_Delete (event table 0x8a98e8..0x8a9bb0, already in the audio section).
- `WXFE_GradientColours` (0x90f3d0, text colours: kGC_Header_Orange, SubHeader_Blue, TextBox_Yellow, List_Lable_Blue, List_Data_Orange, Button_Yellow, PopUp_*, Disabled_Grey...), `WXFE_BackgroundColours` (0x90f18c), `WXFE_TextAnimationEnum` (kTA_None, LargeWobble, SmallWobble, TitleRandom, MediumWobble), `EdgeJustificationEnum` (9 anchors TopLeft..BottomRight), `WXFE_PositionalEnum` (Top/Middle/Bottom), `GapTypeEnum`, `PowerColumnStyleEnum`, `WXFE_LevelTypeEnum`, `WXFE_LevelThemeTypeEnum`, `WXFE_UnlockResourceTypeEnum`, `WXFE_UnlockableStateEnum` (Hidden/Purchasable/Unlocked).

### Item animations and their durations [disasm + data]
- `0x754a90` (WXFE_BaseGfxItem.cpp) maps `kANIM_n` to a clip name of the shared clip library `WXFrontend.Anim` (Bundl10; string 0x8a5e9c, loaded at 0x75548f and 4 other sites): Click->`click`, Click_Error->`click_error`, Highlight->`highlight`, Wide_Highlight->`highlight2`, In_*/Out_* -> lowercase names, Reset->`reset`, Title_Scroll->`title_scroll`; None -> no clip. Jump table 0x754b88 [disasm].
- Clip lengths (Bundl10 `WXFrontend.Anim`, read with `w4m-models --list`) [data]: click 0.12 s, click_error 0.75, highlight 0.75, highlight2 0.75, in_bigbounce 1.58, in_chat 0.62, in_flipy 2.92, in_next 0.29, in_prev 0.46, in_scalehitxy 0.29, in_scaley 0.25, in_slidex 0.62, in_speech 0.62, in_springx 0.25, in_springxy 0.21, in_titleunderline 1.04, in_tooltip 1.04, out_chat 0.62, out_next 0.42, out_prev 0.38, out_scalex 0.25, out_scalexy 0.25, out_scaley 0.21, out_speech 0.29, out_titleunderline 0.96, out_tooltip 0.21, title_scroll 8.33, reset 0.
- Usage in PC menu data (all PC databanks): Anim_Incoming mostly In_SlideX (463 items: list rows/details), In_ScaleY (271), In_SpringXY (136), In_TitleUnderline (59), In_Prev (41, the back nav button), In_Flipy (16, logos); Anim_Outgoing mostly None (1908), Out_ScaleY (192), Out_ScaleXY (92), Out_TitleUnderline (50), Out_Prev (37). Highlight = Highlight (buttons) / Wide_Highlight (list rows); Activated/Clicked = Click [data].
- `Delay_Incoming` is a per-item stagger in ms [unit assumed]: the main menu children start at 300; submenu detail rows cascade 300, 350, 400, 450, 500, 550 (`WXFE.LocalGame` DETAIL QuickGame..Challenges); `Delay_Outgoing` is 0 everywhere [data].
- Menu-specific mesh clips (MeshObjectDesc Anim_In_Mesh/Out/Looping, durations from the bundles) [data]: `WX.Mesh.Title` Intro_Title 11.67 / Loop_Title 11.67 / Outro_Title 0.04; `WX.Mesh.SinglePlayer(Worms)` SinglePlayer_Intro 0.75 + Noise_Loop 0.50; `WX.Mesh.NetOptions` Intro_Internet 0.75; `WX.Mesh.CustomiseOptions` Intro_Custom 2.00; `WX.Mesh.ControllerOptions`/`JoystickOptions` Intro_Controller 0.83; `WX.Mesh.SoundVideo` SoundVid_Intro 0.75 / SoundVid_Outro 0.58; `WX.Mesh.StoryBook` Intro_Book 1.12; `WX.Mesh.ItemShop` Intro1Source1/Outro 0.96; `WX.Mesh.WFactory` In_All 1.38 / Out_All 0.54 (+ per-part clips); `WX.Mesh.Dice` CrateDice_Intro 0.83 / Outro 0.38; `FE.LoadingIcon` / `FE.SavingIcon` Rotate 1.17; `TransitionMesh` Circle_In / Circle_Out 1.00 (in-game, not menus) [data].
- `Delay_Incoming` / `Delay_Outgoing` are ms: `0x755a78` (incoming) and `0x755b34` (outgoing) divide desc+0x60 / +0x64 by 1000.0 into item+0x64 before starting the clip of desc+0x48 / +0x4c (`0x754a90` -> `0x755686`) and the sound of desc+0x54 / +0x58 (`0x754bfc`, channel 4 in / 5 out). On incoming, an animated item is first moved to (50000, 50000, 0) (off-screen) until its delay expires [disasm]. If global `0x97a608` == -1 incoming anims are skipped (it is reset to 0, or set from a leading digit, by every `FE.ChangeMenu`) [disasm; when it is -1 unknown].

### Screen list and menu tree (PC data) [data]
Boot: `Default.cfg` `/STARTMENU:WXFE.MainMenu`, `/GAMEOVERMENU:WXFE.MainMenu`. Each entry below = `WXFE_MenuDescription` resource -> (where it goes). `DETAIL x` = `ListDetailDesc` row of the menu's `*List`; `NAV Prev`/`NAV Start`/`NAV OK` = `MenuButtonDesc` nav buttons (Border kMT_Nav*).
- `WXFE.MainMenu` (MENUTWKXPCCOMMON; children: Title Mesh `WX.Mesh.Title` + 8 `WXP_FE_*` effects, Logo `WXFE.Vs` (In_Flipy, 300 ms), ToolTip mesh, ticker, BuildNumber, Hint_Select/Back; Cancel -> popup `WXFEP.ConfirmFEQuit`). Its list is a separate menu `WXFE.MainMenuMenu` (one ListControl `MainMenuList` -> `WXFE.MainMenuList`, In_ScaleY 400 ms). Rows (Wide_Highlight / Click, RowHeight 35, gap 2): `FETXT.LocalGame` -> `WXFE.LocalGame`; `xo.txtXboxLive` (Enable `Steam.Online`) -> `PM.HandleAccess.OnlineMulti` -> popup `WXFEP.SelectTeam` (online) -> `WXNET.MainMenu`; `FETXT.MyWorms` -> `WXFE.MyWorms`; `FETXT.Leaderboards` (Steam.Online) -> `WXFE.LeaderBoards`; `FETXT.Achievements` -> `Xb360Live.ShowAchievementsUI`; `FETXT.Help&Options` -> `WXFE.Options`; `FETXT.DownloadContent` (Steam.Online) -> `PM.ViewMarketplace`; `Lang.Quit` -> `WXFEP.ConfirmFEQuit`.
- `WXFE.LocalGame` (mesh `WX.Mesh.SinglePlayer` + `SinglePlayerWorms`; Cancel/NAV Prev -> `WXFE.MainMenu`): QuickGame -> popup `WXFEP.QuickGame` (1-4 players -> `WXMsg.StartGame$QuickStartHvC/HvH/HvHvH/HvHvHvH`); Tutorial / Story / Worms3DCampaign / Challenges -> popup `WXFEP.SelectTeam` -> `WXFE.Tutorial` / `WXFE.Story` / `WXFE.W3DStory` / `WXFE.Challenges` (each: level browser, NAV Start -> `WXMsg.StartGame$Tutorial/Story/Challenge`, Cancel -> `WXFE.LocalGame`); Versus -> popup `WXFEP.SelectMultiType` (Deathmatch/ClassicForts -> `WXFE.CreateAGame`, Destruction -> `WXFE.CreateAGameDest`, StatueDefend/Survivor -> `WXFE.CreateAGameSD`, GameRules -> `WXFEP.MultiTypeHelp1`). CreateAGame*: team slots (`WXFEP.InsertTeam`), scheme (`WXFEP.SelectScheme`), landscape (`WXFEP.SelectLandscape2`), `WXFE.Wormpot`, NAV Start -> `WXMsg.StartGame$Multiplayer`.
- `WXFE.MyWorms` (Cancel -> PreviousMenu): Customise -> `WXFE.Customise` (Edit/Delete Team, Weapon, Settings, Landscape popups -> `WXFE.TeamOptions` / `WXFE.WeaponFactory1` / `WXFE.SchemeBuilderBase`), ItemShop -> `WXFE.ItemShop`, Gallery -> `WXFE.GalleryThumbnailView` (-> `WXFE.Gallery`). `WXFE.TrophyCabinet` also lives here.
- `WXFE.Options` (Cancel -> `WXFE.MainMenu`): ControllerSetup -> `WXFE.ControlOptions` (pages `WXFE.ControlsPC1..6`), JoypadControllerSetup -> `WXFE.ControlOptions_Joypad` (`ControlsJoypad1..6`), SoundOptions -> `WXFE.SoundAndVideo`, Language -> popup `WXFEP.SelectLanguage` (-> `WXFE.ChangingLanguage` -> `WXFE.MainMenu`), HowToPlay -> `WXFE.HowToPlay` (6 popups `WXFEP.H2Play_*`, `WXFE.HowToPlayDetails`), Movies -> popup `WXFEP.SelectMovie` -> `WXFE.MoviePlayer`. `WXFE.Credits` (In_Split, plays the credits movie).
- Match flow: `WXFE.PreStart` (sends `WXMsg.StartGame$NOW` after display), loading `WXFE.LoadingScreenPC`, between rounds `WXFE.WinRound` -> `WXFE.PreRound` (NAV OK -> `StartGame$NextRound`), end `WXFE.WinMatch` (Cancel -> `FCS.LeaveWinMatchScreen`), `WXFE.WinMission/WinTutorial/WinChallenge/WinW3D*/WinAward/WinAll*`, `WXFE.ResultsScreen2/3/4Player`. In-game: popup `WXFEP.Pause` (Continue / SaveSeed / Quit -> `WXFEP.ConfirmQuit`), `WXFE.OptionsInGame` (PERSISTPC), `WXFEP.MissionBriefing`, `WXFEP.WeaponHelp`, `WXFEP.ChatPopup`.
- Network (`WXNET.*`, MENUTWKXNET/PCCOMMON): `WXNET.MainMenu` (QuickMatch, RankedMatch, Optimatch, Host Game; Cancel -> `xo.msgReqTitleMainScreen`), `WXNET.HostGame`, `WXNET.Optimatch`, `WXNET.GameLobby`, `WXNET.PCCustomCreate`, `WXNET.PreRound/WinRound/WinMatch/DrawMatch`.
- Counts (PC databank set, later files override same names): 225 `WXFE_MenuDescription` resources = 72 `WXFE.*` screens, 116 `WXFEP.*` popups, 13 `WXNET.*`, 15 `WXNETB.*` (network option pickers), 6 `WXNETP.*`, 3 debug/PS2 leftovers. Full dump recipe below.

### Screen state machine [disasm]
- Message syntax (sent by `0x75a89f` -> `0x75a5bf`, WXFE_BaseItem.cpp, max 20 per list): `Name$arg` string payload, `Name#n` int, `Name%f` float (assumed from the separator set "$#%^" at 0x8aa330), `Name^a^b` two strings; plain `Name` = no payload. A leading list entry `Before` / `After` is a marker, not a message (see below).
- `FE.ChangeMenu$<res>` (FrontEndService HM 0x72a3e9, branch 0x72a586): `WXFE.MainMenu` may be substituted by an override name (global 0x95a100 +0x48, or the online plug-in's current screen); optional 1-2 leading digits are stripped into globals 0x97a608 / 0x97a610; the name is stored as "requested" (+0x4c), pushed on the MenuStack (`0x4ba780`), logged "ChangeMenu", then `0x726bbe` (CreateNewMenu): if top-of-stack != current (+0x48), resolve the resource (`0x50b760`), assert it is a `WXFE_MenuDescription` ("Failed to load menu!" otherwise), build a `WXFE_MenuEntity` (`0x726317`, class 0x8aa644), give it layer `0x97a61c` (+10 per open popup), store it at +0x50, record the name as current unless a popup is open (`0x97a614` = popup count), then broadcast `FE.HighlightItem` and flush `FE.ShowQueuedErrors`.
- The old screen is not deleted by the service: every live `WXFE_MenuEntity` also listens to `FE.ChangeMenu` (HM 0x760797); if the target name is not its own (+0xb8) it calls GoAway (`0x77dcaf`, WXFE_BaseMenu.cpp) which sends `Messages_MenuGoingAway` (immediately if the list starts with `Before`, else after the outgoing anims), marks itself leaving (+0xa9), recursively GoAway()s its children, and each child plays its `Anim_Outgoing` after `Delay_Outgoing`. `FE.KillMenu` / `WXMsg.KillMenuNamed` = same path with kill=1 (no outgoing anim, `0x758ce6`). So old outgoing and new incoming overlap in time; nothing waits for the old screen [disasm; overlap inferred from code paths].
- `FE.PreviousMenu` (0x72ad93): pops the MenuStack twice (current, then previous) and re-sends `FE.ChangeMenu$<previous>` unless it equals the requested name (so the previous entry is pushed back). `FE.PopMenu` (0x72ae4f): pops once and makes the new top the current name without rebuilding. `FE.KillCurrentMenu`, `FE.ResetMenu`, `WXMsg.KillMenuNamed` also handled [disasm].
- MenuStack (MenuStack.cpp, object at FlowControlService +0x120, `m_pMenuStack` = FES +0x12c): vector of 0x53-byte name strings. Push `0x4ba780`: if the name is already in the stack, everything above it is dropped (no duplicate; going "forward" to an ancestor = going back); else append. Pop `0x4ba570`, Top `0x4ba5c0`, Clear `0x4ba750`, SetCheckPoint `0x4ba5f0` (records depth < 100), RestoreCheckPoint `0x4ba630` (truncate to it). FlowControlService (0x4ef180, 0x4eba10, 0x4eeb80) and GameLogicService end-of-game (0x4fb880) use the checkpoint to return to the right menu after a match [disasm].
- Popups (PopUpService HM 0x7259ad): `WXMsg.CreatePopUp$<res>` -> `0x7257ab`: stack of up to 20 names (+0xd4, stride 8, count +0x17c); the previous popup gets `WXMsg.HideMenu` (except `WXFEP.ChatPopup`), the first popup switches input context to "Menu" when in game, layer `0x97a61c` += 10, popup count `0x97a614`++, then it is opened with a normal `FE.ChangeMenu$<res>` (so a popup is a menu entity drawn above the current screen, not recorded as current). `CreatePopUpCheckTrial` adds the trial/upsell check. `WXMsg.KillPopUp` (top) / `KillPopUpNamed$<res>` -> `0x72517e`: `WXMsg.HideMenu` + `WXMsg.KillMenuNamed`, pop, layer -= 10, `WXMsg.ShowMenu` the popup below, restore input context; `WXMsg.KillAllPopUp` -> `0x7253b5`. Popup data uses `Messages_MenuGoingAway = WXMsg.PlaySample$click3` and Cancel = `WXMsg.KillPopUpNamed$<self>` [disasm+data].
- Background "divide" (`WXMsg.AnimDivide$<clip>`, handler 0x728a47): FrontEndService owns `WX.Mesh.BlueDivide` (Bundl10 `Blue Divide.xom`, the curved blue panel) and plays clips on it via `0x7289c0` (speed 1.0, sets busy +0x38). Requests In_Curve / In_Split / Reset; if the other shape is shown it first plays Out_Curve / Out_Split and queues the request (+0x30) until the clip ends. Clip lengths: In_Curve, Out_Curve, In_Split, Out_Split 0.83 s each, Reset 0.04 s [disasm+data]. Data usage: submenus `In_Curve`, main menu / PreStart / ChangingLanguage `Reset`, match results & PreRound & Credits `In_Split`.
- `WXMsg.ScrewMenu` (FlowControlService 0x4f06b8 -> 0x4ed570): until the "press fire" flag 0x910b10 is set, the title shows `FETXT.PressFire` (font `FE.Font`) and removes `MainMenuList` / `MainMenuListTrial` with `WXMsg.RemoveItem` (title "press start" stage) [disasm, partial].

### Input and navigation [disasm]
- Menu entity input (`0x75f8da`, WXFE_MenuEntity): mouse (`Input.MouseMoved`, `Left/RightMousePressed/Released`, `MouseWheelUp/Down`) and `Input.Menu.Left/Right/Up/Down(+.Release)`, `Select`/`SelectReleased`, `AltSelect`, `Cancel`, `PageUp/Down`, `Home/End`, `Caps`, `Delete`, `AcceptKeys`, `Input.KeyTyped`, `Input.Menu.ForceSelect`, `Input.Start/Stop`. The highlighted item gets the input first (vfunc +0x18; returns 1 = not consumed). Unconsumed Left/Right/Up/Down highlight the item named by the current item's `Navigate_Left/Right/Up/Down` (desc +0x78/+0x7c/+0x80/+0x84, via `0x77dd43`): navigation is an explicit data graph, no spatial search. Unconsumed Cancel sends the menu's `Messages_CancelPressed` (desc +0x88). Select fires the item's `Messages_Selected` (`OnRelease` buttons fire on SelectReleased) [disasm; Select path assumed by symmetry].
- `AutoRepeatService` (HM 0x74bcc9, tick 0x74b8d8): fixed 20 ms steps (elapsed ms clamped to 100). Per direction {held, counter, period}: press sets counter = period = 23; each step counter--, a repeat `Input.Menu.<dir>` is sent when counter < 5 (first repeat after 19 steps = 380 ms). Each repeat reloads counter = period and decrements period down to 8 (then alternates 7/8): intervals 380, 360, 340 ... ~80/60 ms. `*.Release` clears; `FE.ControllerLost` resets [disasm].
- Highlight feedback: Anim_Highlighted (`highlight`/`highlight2` 0.75 s) + Audio_Highlighted (`global/Highlight`); select: Anim_Activated `click` 0.12 s + Audio_Activated (`frontendsfx/Click`, `global/Click2/3`) [data].

### Transition timings summary
- Screen change = old items' Anim_Outgoing (mostly None or 0.21-0.25 s scale-outs, title underline 0.96 s) in parallel with new items' Anim_Incoming after their Delay_Incoming stagger (typ. 300 ms base, +50 ms per row; rows In_SlideX 0.62 s, tooltip/headers In_ScaleY 0.25 s, title In_TitleUnderline 1.04 s, back button In_Prev 0.46 s), plus the BlueDivide clip 0.83 s if the shape changes (Out then In = 1.66 s). A typical submenu entrance is therefore fully settled after ~0.55 s (last row delay) + 0.62 s = ~1.2 s [data; arithmetic].


## 18. AI (CPU worms)

Tags: **data** = read from strings, RTTI or decoded XOM; **disasm** = read in code; **assumed** = inferred. VAs are for WormsMayhem.exe (base 0x400000). "units" means W4M world units.

### Classes and source units [data: RTTI and .cpp strings]

- **AIService** (vtable 0x825cf8, HandleMessage 0x4b4260, subscribe 0x4b3390, setup 0x4b3820). It waits for three things before thinking: `AISceneGraphService` has updated the map, the active worm is ready, and `ObjectCount.Active` has dropped to 0. Then it runs a think and executes the queued actions. Messages: `GameLogic.AITurn.Started`, `AI.ExecuteActions`, `AI.PerformDefaultAITurn`, `AI.WeaponsDontEndTurn` (no retreat).
- **AISceneGraphService** (vtable 0x825a7c): builds the pathing node grid (`AI.PopulatePathingNodes`, `Land.NewShape`), adds jump nodes (`Worm.Jump.Forward`, `Worm.Jump.Backflip`, `WXWorm.AftertouchDelta`/`Strength`) and adds blockages where a path failed. A* lives in AIPathManager.cpp. The `iterations` and `m_nMaxNumIterations` asserts suggest a bounded A* [assumed].
- **CAIPlan tree** (AIPlan*.cpp). A think spawns many plans, scores them, refines them over several frames ("Thinking took N frames", "ScoreAllMoveNodes slice"), then queues the actions of the best plan.
  - `CAIPlanSeed`, `CAIPlanSkipTurn`, `CAIPlanDefense`.
  - `CAIPlanMove` → `CollectSomething`/`CollectCrate`, `DoRandomSmallMove`, `MoveCloserToSomething`/`MoveCloserToTarget`.
  - `CAIPlanAttack` → `WithMoveAndRetreat` → `Projectile` / `Direct` / `Strike`, plus `CloseRange`, `Animal`, `Special` and `Flood`.
- **Attack plan per weapon** (`Cannot use kWeaponX` strings). These are the weapons the AI can use:
  - projectile: Bazooka, Grenade, ClusterGrenade, BananaBomb, HolyHandGrenade, PoisonArrow, GasCanister, HomingMissile (targeted)
  - direct: Shotgun, SniperRifle
  - strike: Airstrike, ConcreteDonkey, SuperAirstrike, Fatkins
  - melee: BaseballBat, Prod, FirePunch
  - close-range explosive: Dynamite, Landmine, and CheapDynamite{Grenade, ClusterGrenade, BananaBomb}, i.e. those weapons dropped at the AI's feet
  - animal: Sheep, OldWoman, Scouser, SuperSheep, Starburst
  - Flood
  - SkipGo (`CAIPlanSkipTurn`; scripts must give the AI infinite SkipGo)
  - The only plan without a named weapon is `CAIPlanAttackSpecial` [assumed: its use is unknown].
  - No plan class exists for Girder, BridgeKit, Teleport, LowGravity, Redbull, SentryGun, BubbleTrouble, NoMoreNails, AlienAbduction or Pipe.
- **AITurnAction subclasses**, queued by `Queue Action: ...` (AIPlan.cpp) and run by AITurnEntity: Delay 0x497120, SetWeapon, SetAimAngle 0x4965d0, SetLaunchVelocity 0x495a60 (`AI.LaunchVelocity`), SetWeaponFuse 0x496ab0, SetWeaponTarget 0x495d80, SetStrikeDirection, SetWormOrientation, FireWeapon 0x496860 (sends `c_MsgFireReleased`), Path 0x496c90, StrafeTowards 0x4974d0, DetonateWhenGoingAwayFrom 0x4977e0, MoveForwardTillInMeleeRange, WaitForSuperAirstrikeReady, WormSelect, Rethink 0x499830, SetCamera.
- **Path move types** (AIPathAction strings): WALK, with three on-fail policies (keep trying / skip to next move / rethink forbidding the move), JUMP_FORWARD, JUMP_BACKFLIP, JUMP_UP, JUMP_UP_NO_AFTERTOUCH, JETPACK, PARACHUTE_START, PARACHUTE, NINJA_ROPE, SUPER_SHEEP, DELAY. AIActionPath has log strings for super sheep, parachute, jetpack and jump, but none for rope. NINJA_ROPE is probably never executed [assumed]. Repath happens on failure, with a limit ("too many repaths, forbidding further movement").

### AITWK.XOM [data: xom.py / tweak.py]

- 24 `AIParametersContainer` with 111 fields (`pe.py schema AIParametersContainer`): `AIParams.CPU1`..`CPU5`, `CPUTest`, `Worm00`..`Worm17`. All `Worm*` are equal to `CPUTest`. TwkEdVer = 124.
- There are **5 CPU levels**. AIService 0x4b3820 loops over worm slots 0..15. For each one it reads the team's CPU level (team byte +0x74; 0 = human; assert `uCPULevel <= 5`), then copies `AIParams.CPU<level>` into that worm's `AIParams.WormNN` slot (DRM::GetWormAIParameters 0x50c440) [disasm].
- The command line flag `/ALLAIPLAYERS` (0x4da163) sets 0x959ac4, which turns human teams into CPU5 [disasm].
- At think start (0x4a4920), the params of the thinking worm go into the global `c_pAIParameters` 0x9560f4. The worm position goes into 0x9560fc [disasm].

Values that differ between levels (CPU1 / CPU2 / CPU3 / CPU4 / CPU5). Every Pref* not listed is 1.0.

| field | 1 | 2 | 3 | 4 | 5 | meaning, with read site [disasm unless noted] |
|---|---|---|---|---|---|---|
| ShotErrorProjectile | 0.3 | 0.2 | 0.1 | 0.05 | 0 | 0x4a06c0: each launch-velocity component is scaled by 1+e·(2r−1) (0x4a4580). This covers angle and power together |
| ShotErrorDirect | 0.3 | 0.2 | 0.1 | 0.05 | 0 | 0x4a0d70: direct weapons (shotgun/sniper), strafe mode |
| ShotErrorDirectNonStrafe | 0.05 | 0.02 | 0.01 | 0.005 | 0 | 0x4a0d90 |
| StrafeProgressiveErrorScale | 0.99 | 0.98 | 0.96 | 0.95 | 0.9 | passed to StrafeTowards. The error shrinks by this factor per step [assumed] |
| ShotErrorStrike | 20 | 10 | 5 | 5 | 0 | 0x4a1b00: strike target offset, in units [assumed] |
| ConsidersStrikeThrustDirection | 0 | 0 | 1 | 1 | 1 | 0x4a13a0: chooses the strike direction |
| WeightStrikeSecondaryTarget | 0 | 0.3 | 5 | 1 | 1 | 0x4a19ea: multiplies the value of other worms hit by a strike |
| WeightingWormVital | 1 | 1.5 | 3 | 5 | 1000 | 0x4a92c7: base value of a target with worm flag 0x100 (+0xec), otherwise 1 |
| WeightingWormExchange | 0.05 | 0.1 | 0.1 | 0.5 | 0.5 | 0x4a9b20: K = (nEnemy/nAlly)^x. Enemy value /K, ally value −K× |
| WeightingExplosiveSecondaryDamage | 0 | 0.5 | 0.5 | 0.8 | 1 | explosion score for barrels and chain reactions (0x49fe90, 0x4a0540, ...) |
| WeightingExplosiveNearbyThreat | 0 | 1 | 1 | 2 | 4 | likewise, for knocking a worm into a threat (water, mine) |
| WeightingPreferNearbyTargets | 1.5 | 1 | 0.5 | 0.1 | 0 | 0x4a93bb: value ×(200/max(d,200))^x |
| WeightingPlanScoreRandomise | 0.2 | 0.2 | 0.1 | 0.2 | 0 | 0x498a0a: plan score ×(1+x·(2r−1)) |
| WeightingPreferAttackHumans | 0.8 | 1 | 1 | 1.2 | 1.8 | 0x4a956b: positive values of human-controlled targets ×x |
| ProjectileSweetSpotDistance | 0 | 5 | 5 | 10 | 5 | 0x4a9be0: projectile aim point = target + d·x when the target's threat rating (+0x1c) > 0; d (+0x20) is the unit vector away from its worst threat (0x4a9e90), so the blast pushes it toward water, a drop or mines [disasm] |
| DelayBeforeFire | 0.5 | 0.5 | 0.5 | 0.5 | 0.2 | s, after aiming and before fire (0x49eb8b) |
| DelayBeforeNonFirstMove | 2 | 0 | 0 | 0 | 0 | s, before a move after a rethink (0x4983ef) |
| AddScoreCollectSomething | 1000 | 5000 | 20000 | 30000 | 60000 | crate plan base score (0x4a72e1, 0x4a7d38) |
| LikeToCollectHealthWhenHealthBelow | 25 | 25 | 25 | 25 | 50 | hp threshold for "Score increased due to worm having low health" |
| ReduceMoveScoreFurtherThan | 100 | 200 | 100 | 100 | 100 | 0x4a75c0: move score ×R/d when d > R |
| ReduceMoveScoreIfTimeLeftLessThan | 40 | 30 | 30 | 20 | 20 | move score ×t/T when turn time left t < T |
| MemoryImproveAccuracyEffect | 0.2 | 1 | 1 | 1.5 | 1 | 0x4a5d00: error /(1+x·Σmatch) for a repeat of a previous shot (MatchRadius 200) |
| MovementJumpForwardAllowed | 0 | 1 | 1 | 1 | 1 | pathing (0x492210) |
| MovementJumpBackflipAllowed | 0 | 0 | 0 | 1 | 1 | pathing (0x492210) |
| MovementJumpError | 0 | 0.2 | 0.1 | 0.05 | 0 | 0x496c90: error on path jumps ("worm's jump error =") |
| JetpackAboutToCrossLineLookAhead | 1 | 2 | 3 | 4 | 5 | read in worm code 0x5a8780: jetpack look-ahead |
| MortarMaximumAimAngleAllowed | 0.5 | 0.5 | 1 | 1.6 | 1.6 | **no read found** |
| PrefClusterGrenade | 1 | 1 | 1 | 0.8 | 0.3 | |
| PrefGasCanister | 1 | 1 | 1 | 0.5 | 0.5 | |
| PrefHomingMissile | 0.9 | 0.9 | 0.9 | 0.8 | 0.7 | |
| PrefProd | 0.3 | 0.3 | 0.3 | 0.3 | 0.3 | (1.0 in CPUTest) |

Values shared by all 5 levels:

- Target value: ThisWormValue 1 (read per *target* worm, so scripts can weight a target), WormHealth 0.04, WormPoisoned 0.08, LastInTeam 2, WormNearbyWorms 0.02, WormMilesAway 0.1, WeightingAttack 2, WeightingKillTarget 200, WeightingPunchThroughLand 10.
- Multiple use and variety: MultipleUse 1.5, BestMissShotMultipleUse 1, PreferVariety 0.4.
- Delays: DelayAtStart 0, DelayBeforeFirstMove 0.
- Distances: StrikeSweetSpotDistance 2 (0x4a1423), ClusterDistanceAboveTarget 10, MaximumDistanceTargetConsidered 1e5.
- Movement: AddScoreMoveIfNotMoved 20, AddScoreMove 10, RandomSmallMoveRange 100, ForbidMoveIfWouldLeaveTimeLessThan 10 (s).
- Crates: WeightCollect{Weapon, Health, Utility} 1, WeightCollectHealthWhenPoisoned 3, AllowCollectNormalCrate 1, AttractorZoneRadius 50, MemoryImproveAccuracyMatchRadius 200.
- Fields with no read found [disasm, by scanning every load of 0x9560f4]: AddScoreTeleport 1, WeightTeleport{Defensive, Offensive}Pos 1, WeightRetreatDefensivePos 1, WeightRetreatOffensivePos 0, MortarMaximumAimAngleAllowed.
- CPUTest (= Worm*) differs from the levels: KillTarget 4, DelayAtStart 3, DelayBeforeFire 1, DelayBeforeFirstMove 2, AddScoreMoveIfNotMoved 10000. It is a scripted/debug profile, and the worm slots are overwritten from `CPU<level>` at load.

### Turn flow and timings [disasm 0x49e6d0, 0x4983a0]

1. The think runs over several frames. Its frame count is stored in 0x9560b4.
2. If the plan has a move before firing: Delay(DelayBeforeFirstMove, or NonFirstMove after a rethink, minus the think time), then Path, then Delay 0.5 s.
   Otherwise, on the turn's first action: Delay(DelayAtStart − think time).
3. SetWeapon, then optional fuse/target/launch velocity/orientation/aim angle, then Delay(**DelayBeforeFire**), then FireWeapon. Strafe weapons add StrafeTowards (StrafeProgressiveErrorScale). Some weapons add DetonateWhenGoingAwayFrom.
4. Delay 1.0 s, camera "Default", then the retreat Path. There is no retreat if `AI.WeaponsDontEndTurn` is set, or if the move should not end the turn.
5. If no good plan is found, the AI skips the turn. It never skips when locked to a multi-shot weapon: in that case it fires again. Plans with a negative score are forbidden.

### Shot evaluation [disasm]

- **Launch is exact velocity.** The plan stores a launch vector and SetLaunchVelocity writes `AI.LaunchVelocity`. The AI does not charge the power bar.
- **Ballistic solver** 0x4ace50 (AIPlanUtilities). The speed range is [BasePower, BasePower+MaxPower] of the payload's `PayloadWeaponPropertiesContainer` (+0x110/+0x114, loaded by 0x4acbc0).
  - Acceleration = gravity (if `IsAffectedByGravity`: `Gravity`, or `Gravity.Slow` if `IsLowGravity`, times `Low.Gravity.Multiplier`) + full wind (if `IsAffectedByWind`: `Wind.Direction`/`Wind.Speed` → (sin, 0, cos)·speed, 0x4ac6f0).
  - **About 11 speeds** are sampled between two fractions of the range (step = range/10). For each one, `TargetParabola` 0x519a40 solves the exact flight time for the arc to the target (two roots, `fTimeSquared1/2`).
  - Each arc is checked against the land as 2 segments (0x4aca20). The error is the distance from the collision point to the target. The lowest squared error wins.
- **Bouncing payloads**: BounceToRest 0x4ad770. It iteratively rescales the launch speed (fScaleSpeed, fGuessStep), simulates the bounces (0x4ad580, `Bounce.MinSpeed`, water = `Water.Level` − 100) and keeps the better rest-point error.
- **Wind is never degraded by difficulty.** The only inaccuracy is the post-hoc ShotError.
- **"Best miss" plans**: when the target cannot be hit, the AI spawns a plan that aims at the collision point to dig through land ("Shot will punch through land in N shots", WeightingPunchThroughLand, BestMissShotMultipleUse). This is not done on indestructible land.
- **Damage score** 0x49ed30:
  - Damage passes through armour (ArmourLogicEntity 0x548fc0), except for weapon ids 10–12, which are melee.
  - Score = WeightingAttack × target value × d. Here d = damage, or max(hp, WeightingKillTarget) if the hit is lethal ("should kill with damage alone").
  - A non-lethal hit with a knock-into-threat rating moves d toward the kill value ("may knock X into nearby threat") [disasm/assumed exact blend].
  - Explosive plans add the secondary/threat terms (the WeightingExplosive* fields).
- **Target value** 0x4a9260:
  - v = (Vital if flagged, else 1) × (1 + 0.04·hp), then v += max(0.1, 1 − 0.08·poison).
  - Enemy: v/K. Ally, including the thinking worm's own team: −v·K, where K is the WormExchange ratio. This is the friendly-fire and self-damage penalty.
  - Then: ×LastInTeam when that side has 1 worm left, ×nearby factor, ×the target's own ThisWormValue, ×PreferAttackHumans when the target is human.
  - Targets inside an "AI affecting trigger" are removed (ForbidShotsWhenMightAffectAITrigger).
- **Weapon choice** 0x49c060: plan score ×Pref(weapon) (GetWeaponPref 0x66364d switches on weapon enum 1–29 and 34–42), then ×max(0, 1 − PreferVariety·match with recent plans) (memory 0x4a5720).
  - Weapon availability comes from the inventory plus `Inventory%d.WeaponDelays` (PopulateWeaponsAvailibleArray) [data].
  - Prefs for BridgeKit, LowGravity, Teleport and Pipe are not in the switch, so no plan uses them.
- **Memory** (AIPlanMemory.cpp):
  - Repeat shots get more accurate (MemoryImproveAccuracy*).
  - Plans that match a failed plan are scored down.
  - The skip-turn score is ×1/(1+5·Σ previous skips) (0x4a57c0).
  - Detail [disasm]: think start 0x49af70 runs CheckPlanResult 0x4a6ab0 (the last attack, c_pMostRecentPlanMemory 0x956114 {effect, weapon, worm 0x90ea60, target, its pos +0x38, its hp +0x11e}, is a failure if the target kept hp and pos), RegressFailedMemory 0x4a5b10 (×0.99, 0 once the target changed), RegressImproveAccuracyMemory 0x4a6080 (×0.95), RegressSkippedTurnMemory 0x4a61b0 (×0.9); records under 0.1 are deleted. A plan of the same worm at a failed target: score ×(1 − 0.5·f), f = effect, ×0.2 with another weapon (0x4a6590). Accuracy match (0x4a5640): effect·(1 − d_shooter/R)·(1 − d_target/R), both < MatchRadius R, all records. Skip records (0x4a68c0) are stored, and the 1/(1+5Σ) scale applied (0x4989d0), only in worm-select mode (0x9560c1, set at 0x4a4f6b).
  - Strafe [disasm]: ShotErrorDirect is read only by CAIPlanAttackDirectActionable slot 11 (0x4a0d70); Shotgun and Sniper override it with ShotErrorDirectNonStrafe (0x4a0d90). StrafeTowards needs plan flag 0x200 (0x49ebed), set by no plan constructor: both strafe fields are dead.
  - Flood [disasm 0x4a3640]: each target below Water.Level + Flood.Delta scores 0x49ed30(target, 0, 1000). Starburst [0x4a43a0]: adds 0x49ed30(active worm, its hp) to the animal score. Close-range explosive (Dynamite, Landmine, CheapDynamite*) [0x4a2c70]: the weapon's blast at the worm, ×Pref, ×PrefWeaponMelee, ×0.05 for a best-miss. Animal [0x4a4210]: the blast at the target + a land term, ×Pref, ×PrefWeaponAnimal, ×plan+0x58.
  - Node grid [disasm 0x4b2800]: total area = Σ (max.x − min.x)(max.z − min.z) of the NodeGrid boxes; spacing = sqrt(area / 16000).
  - Node grids are merged where they overlap (0x4ae320, "After second merge pass have N node grids"). Repath [disasm 0x490551]: a failed path action adds a path-failed blockage at its node and pathfinds again; past 2 repaths (+0x28 > 2) 0x9560f8 is set ("forbidding further movement"). Move → retreat pairs (0x4a8c60) come from the same 21 × 21 × 2 window. Active objects (ObjectCount.Active): the 32 callers of ActiveObjectRegistrationService 0x4d3af0, listed in docs/sim.md "Settle".

### Movement [data + disasm]

- **Path A\*** (AIPathManager) [disasm]: 2-D node grid (x, z int16 + a layer byte), 8 walk neighbours (a diagonal only if both sides are walkable, 0x492510), G cost 10 orthogonal / 14 diagonal, heuristic octile 10·max + 4·min (0x4923a9, 0x491fd8). Jump edges per allowed type (+0xc04c from MovementJumpForward/BackflipAllowed, 0x4924d7) over a precomputed reach table, cost +40 forward jump, +60 backflip (0x492008, 0x492003). At most 200 iterations per pathfind (0x4b0ba6), 100 per step call (0x492e4e); partial paths are accepted unless too short.
- **Node spacing**: sqrt(land area / 16000) (0x4b2a68). **Move nodes**: a 21 × 21 window × 2 layers around the worm, scored by 0x4aa6a0 two columns per think step ("ScoreAllMoveNodes slice", 0x4ab490).
- **Think budget** 0x49b210: each frame the cost counter 0x9560b0 drops by 80 and think steps run while it is under 80 (a counter above 80 after the drop is reset to 160). Costs: pathfind 100 (0x494365), attack plan 100 (0x49b4b9, 0x49e01b), position score 10 (0x4aa6a9).
- **Target threat** 0x4a9e90: 8 directions × probes at 50/100/150/200 units (0x90ebf8), weight 2/(k+2); water or no land 0.2, a drop over 30 units 0.05, mines 0.1·(100−d)/100, GameLogicService objects 0.05, other targets 0.05; total capped at 1 (+0x1c), worst direction negated into +0x20.
- **MovementJumpError** 0x496f24: for each jump move of the path (types 3..5) the displacement to its landing is scaled per component by 1 + e·(2r−1) (0x4a4580) before the move is queued.

- Movement plans (`CAIPlanMove*`) score: crate collection (AddScoreCollectSomething × type weight; health ×3 when poisoned or below the hp threshold; `Imaginary`/`Attractor` detail objects come from scripts), moving closer to a target, and a random small move (range 100).
- An attack with a move gets +AddScoreMove, plus +AddScoreMoveIfNotMoved if the worm has not moved yet (0x49bd60).
- Moves are refused when they would leave less than 10 s of turn time ("Not enough time left to follow path").
- Paths are A* over the node grid and can include walk, jump forward and backflip (gated per level), jetpack, parachute and super sheep. **No teleport, girder or rope moves were found.**
- After firing, the AI retreats along a path: move→retreat node pairs ("Move node found: move to ..., retreat to ...").

### Actionable for client/src/ai.cpp

1. **5 levels, not 3.** `levelOf` clamps to 3. Map the front end to CPU1..CPU5 and drive the behaviour from the AITWK values above (one small table) instead of the `level == 1/2/3` branches.
2. **Aim error model.** W4M scales each launch-velocity component by 1±e (e = 0.3/0.2/0.1/0.05/0) and also uses it for direct weapons. Today level 1 is ±0.05 rad / ±5 % and level 3 is 0. W4M CPU1 is much sloppier, while CPU5 is perfect.
3. **Wind**: W4M uses the exact wind at every level. Drop the "level 1 half-guesses the wind" hack and let ShotError carry the inaccuracy.
4. **Repeat-shot accuracy memory**: error /(1+Effect·matches) when shooting again from about the same spot at the same target. This is missing today.
5. **Scoring weights.** Kill bonus = max(hp, 200)×2 (WeightingAttack) vs today's +30. Team losses: negative value × exchange ratio (nEnemy/nAlly)^0.05..0.5 vs today's fixed ×2. ×2 when the target is the last of its team. Prefer human targets (0.8..1.8).
6. **Randomness and variety**: multiplicative plan noise ±20 % (CPU3 ±10 %, CPU5 0) and score ×(1 − 0.4·recentMatch). Today the taste is additive (±12/10/5) and there is −8 for the last weapon.
7. **Per-weapon prefs**: Prod 0.3, Homing 0.9→0.7, Cluster 0.8/0.3 and Gas 0.5 at CPU4/5. No AI use of teleport, girder, bridge, low gravity, sentry, bubble, NoMoreNails, abduction or pipe. Today level 3 teleports and evaluates Sentry and Abduction.
8. **Timings**: DelayBeforeFire 0.5 s (CPU5 0.2 s), a 0.5 s pause after a pre-fire move, and 1.0 s after firing before the retreat. Today there is one fixed 20-tick pause.
9. **Crates dominate** at higher levels (base 1000→60000). Health crates count ×3 when poisoned or below 25 hp (50 at CPU5). Move score falls off beyond 100 units and when the turn time left is under 20–40 s. Moves are forbidden under 10 s.
10. **Jumps in paths**: no forward jump at CPU1, backflip only at CPU4+, plus the jump error. Today the AI jumps whenever it is stuck, at every level.


## 19. EFMV cutscenes, acting scenes, FMV

EFMV is the in-engine cutscene system. One timeline format covers three uses:
1. scripted level movies (Intro/Midtro/Outro...), played by `EFMVMovieLogicEntity`;
2. the 142 bystander "acting" scenes in `Tweak/WORMACTING.XOM`, cast and played by `WXSceneManagerService` + `WormScenePlayerService`;
3. the outtake levels (`OUTTAKE*.XOM`), which are ordinary level movies.

The `.wmv` files are separate. `MoviePlayerService` plays them full screen.

### Data model [data: pe.py schema, xom.py; all 495 level movies and 142 acting scenes decode exactly]
- `EFMV_MovieContainer` {Tag, Track ref[]}. `EFMV_TrackContainer` {Tag, Event ref[]}. Every event derives from `EFMV_BaseEventContainer` {Tag str (editor label), **Time u32 = ms from movie start**, Critical bool}.
- The movie name is the `XContainerResourceDetails` key, e.g. TINCANWALLY has `GoldCut`, `Intro`, `Midtro`, `Outro`. Events inside a track are **not stored in time order**. The player keeps one cursor per track and still fires them all, because it fires every event with `Time <= now` [disasm 0x526cb0]. Ordering the events by Time is assumed to be safe.
- 106 level files hold 495 movies. The most common names are Outro 59, Midtro 38, Intro 21, `EFMV.Intro` 21 (tutorials/challenges), `EFMV.Intro_Dialogue`, `EFMV.Failure`, `EFMV.Success_*` and `Pointless_cut`. `ANIMTEST.XOM` and `MOVIETEST.XOM` are dev tests. `Tweak/LOCAL.XOM` has a `TestActing` movie (actor `STANDIN A`).
- Worm events derive from `EFMV_WormBaseEventContainer` (no fields of its own). They act on the **track's cast actor**: one track = one cast member, and the track's index in `Track[]` = cast-member index.

| Event (fields after Tag/Time/Critical) | Effect [disasm 0x5257b0 / 0x60b1b0 unless noted] |
|---|---|
| CastActor {ActorName} | binds the track to the actor with that exact name (`FindActor` 0x60c190, `strcmp`; not found = 0x7f, and the track's worm events are then dropped). Ignored when the scene was pre-cast (acting scenes, flag at this+0x318) |
| WormEmote {Emote, PermittedEyeMovement, BlendTime s, Coyness, AllowBlink} | facial emote on the actor (actor+0x58/0x54/0x5c, blink = bit 2 of +0x6d) |
| PlayAnimation {Animation} / StopAnimation {BlendTime} | worm clip name (actor+0x4c) / stop with a blend (+0x50). BlendTime is 200/500 in levels and 0.3 in emotes; the unit is mixed, assumed ms vs s |
| WormLookAt / WormGestureAt {TargetCastMember i8} | look at / point at another track's actor. Target = own track: stop looking (0x7d). Target < 0: special target 0x7e (assumed: the camera / the event focus) |
| TriggerSpeech {Speech, FullVolume, Duration ms} | `*Name` = level EFMV sample `EFMV/<Level>/<Name>` (schema help string 0x8810f0). A plain name = a speech category of the actor's own voice bank (`FriendlyDeath`, `Startled`...). Duration = line length, used for the matching Comment |
| SpawnAccessory / SpawnParticle {ResourceId, AttachmentPoint}, ClearAccessory | attach a hat or prop / a particle to a bone. ClearAccessory = "CLEAR" |
| ThreatenWorm {Threatened} | flag at actor+0x6e bit 0 |
| CutCamera {Position, LookAt} | cut to two locator names |
| PathCamera {PositionKnotList, LookAtKnotList (comma lists of locators), Loop*, *Steps u32, *Tension, DrawDebugDots} | `Camera.Path.*` data + `Camera.Path.Start`. The movie waits for `Camera.Path.Stopped` before it ends |
| TimedPathCamera {same, *Steps = comma string per segment} | `Camera.TimedPath.*`. The default step is 500 [disasm 0x637b50]. Steps x 10 ms matches the gaps between camera events in TinCanWally (500,500 = 10 s) [assumed] |
| ShakeCamera {Duration ms, Magnitude} | `Camera.Shake.Length/Magnitude` |
| Comment {Comment = text id, Duration ms} | subtitle line via `CommentaryPanel.Comment` / `.Delay`, e.g. `M.Wild.Tin.EFMV.3a`. The ids are in the language files |
| FailureComment {Duration} | random `Miss.Generic.Lose1-5` text |
| TriggerSoundEffect {EffectName, Location, Looping, Duration} | Foley/Amb event of the level EFMV bank, placed at a locator. Only one looping SFX at a time (assert) |
| CreateEmitter {EmitterName, Location, Locator, UserId} / DeleteEmitter {UserId} | particle FX at a locator (`Particle.Name/DetailObject/Locator`) |
| CreateExplosion {Location, WormDamage*, Impulse*, LandDamageRadius, ParticleEffect, ImpulseOffset} | a real explosion (`Explosion.*` data, 0x4f9970); it is always Critical |
| SpawnWorm {WormId, DataId} / UnspawnWorm {WormId}, SelectWorm, SelectWeapon | manage gameplay worms (`Worm.Respawn`, `WXWormManager.UnspawnWorm`, `Weapon.Selected`) |
| RaiseWater {Delta} | `Water.Level` += Delta |
| CreateBorders / DeleteBorders | letterbox: `EfmvBorderEntity`, TWEAK `EFMV.BorderHeight` 50, `BorderOnTime` 0, `BorderOffTime` 500 |
| CreateBriefingBox {MessageId[]}, CreateWXBriefingBox {Type, TextId, Image} | tutorial briefing dialog (`WXD.BriefingText` / `WXD.BriefingImage`) |
| Stop | when a briefing dialog is up: pause (this+0x30, AppDataService 0x4d7690) until `Game.BriefingDialogOkPressed`. Used on "Pause Movie" tracks |
| Joypad{Button,Stick}, Create/Animate/DeleteCustomHudGraphic, AnimateDetail {FourCC, AnimName}, SetPointLightColor, DeleteLandframe {Code} | tutorial input demos, HUD overlays, detail-object clips (`Detail.AnimName`), lights, land-frame removal |

- Critical events by type: DeleteBorders 364/371, CreateExplosion 143/143, DeleteEmitter 73/73, DeleteLandframe 31/46, SpawnWorm 21/21, RaiseWater 4/4 [data]. They are exactly the events that must still happen when the movie is skipped.

### Speech, lip-sync, sound banks [data + disasm]
- AudioService loads the bank `EFMV/<Level>` per level, plus `Story.`/`Tutorial.`/`Challenge.`/`Deathmatch.` prefixes (0x604760, 0x606480). It also loads the lip file `EFMV/<Level>/LIP` = install-root `EFMV/<Level>/LIP.txt`. Its rows are `frame,VISEME,<none>` at **about 30 fps**: the last frame x 33 ms matches TriggerSpeech.Duration, e.g. 50 frames vs 1797 ms [data].
- `Data/Audio/EFMV/<Level>.lsd` maps `EFMV/<Level>/<Line>` to the line hash, which is also the LIP.txt `#hash`. The speaker comes from the line name `<Level>_<Role>_NN`, not from the bank.
- `EFMV/Failures/Failures_Narrator_01-05` (0x8673d4) is a shared failure bank.
- AudioService sets fade values on `EFMV.Play` (0 / 500) and on `EFMV.Terminated` (2000 / 0) [disasm 0x606fc4; assumed music duck in/out, ms].

### Playback: EFMVMovieLogicEntity [disasm]
- vtable 0x854c30, `.cpp` 0x854bfc, HandleMessage 0x5272f0, tick 0x526ec0 (returns 10 = task period ms).
- **Start** (0x526f70, on create):
  - subscribes Input.QuitEFMV, Game.BriefingDialogOkPressed, Camera.Path.Stopped, Camera.TimedPath.Stopped, Edit.StopEFMV, Game.BriefingDialogNowOn;
  - sends `xo.msgInterruptsOff` ("EFMV player disables network interrupts");
  - enables input group 5 `EFMVMovie`;
  - sets `EFMV.Active`=1;
  - looks up `EFMV.MovieName` in the level databank (assert "Couldn't find EFMV clip");
  - reads `ActiveWormIndex`, builds the per-track cursors (+0x28) and registers the active object "EFMV Movie playing", which holds the turn;
  - `EFMV.StartTime` > 0 seeks, editor only (assert start service == "EDIT", 0x526dc0).
- **Step** 0x526cb0: for each track, fire the events with `Time <= now`, then `now += 10` ms. It returns true when every track is done. The tick kills the entity once all tracks are done, no camera path is running (+0x32) and no briefing is up (+0x31).
- **Shutdown** 0x525540:
  - frees the cursors and disables input group 5;
  - sets `EFMV.Active`=0 and clears CommentService (0x5dff70);
  - sends `EFMV.Terminated`, then `xo.msgInterruptsOn`;
  - drops the active object, so the turn can go on;
  - camera back to `Default` (CameraManagerService 0x51e4e0).
- Lua receives `EFMV.Terminated` as the callback `EFMV_Terminated` (table 0x921368) and branches on `GetData("EFMV.MovieName")`.
- **Skip**: `Input.QuitEFMV` is ignored when `EFMV.Unskipable` != 0. Otherwise 0x526d90 loops `step(skip=1)` to the end, firing **only Critical events**, then kills the entity, so `EFMV.Terminated` still fires. `Edit.StopEFMV` does the same. The skip is disabled when the online flag at [0x95b5e8]+0x2c == 1 [assumed: network game].
- Only one movie at a time: `EFMV.Play` while `EFMV.Active` asserts (GameLogicService 0x4ff21d). GameLogicService spawns the entity by class GUID 0x854ba8.

### Triggering from Lua [data: lua.py --all]
- `SetData("EFMV.MovieName", "<name>")` + `SendMessage("EFMV.Play")`: 126 sends, 254 MovieName uses. The tutorials wrap this in `kPlayEFMV`.
- `SetData("EFMV.Unskipable", 1/0)` around dialogue movies (tutorials: Intro skippable, `Intro_Dialogue` not).
- End of mission: `SetData("EFMV.GameOverMovie", "Outro")` before `GameLogic.Mission.Success`. At game over GameLogicService (0x4fb880 -> 0x4f52c0) copies GameOverMovie into MovieName and spawns the player, unless `EFMV.GameOverMovie.Off` = 1. `lib_Deathmatch*TurnEnded` uses `"Outro"`.
- Typical chains: Initialise -> Intro. `EFMV_Terminated(Intro)` -> `Worm.DieQuietly` on the extra actors (TinCanWally kills Dummy worms 2 and 3), then `StartFirstTurn`. A gameplay event -> Midtro (+ respawns) -> ...
- `EFMV.Start` / `EFMV.End` (86 / 94 sends) are a different, older mechanism: W3D-style scripted camera scenes in the `-w3d` levels. A few W4M scripts send `EFMV.End` too. Weapons send them as well (SuperBomber, AlienAbduction, ParachutePayload). They toggle borders and input; there is no timeline.

### Role assignment in level movies [disasm 0x5a58c0 + data]
- Each worm's graphic entity registers two actors with WXSceneManagerService: `WORM<n>` (type 1) and `WORM<n>TARGET` (type 5). n = the worm slot of `lib_SetupWorm(n, "<WormData>")` (= `Worm.Data<nn>`). Each actor also gets a default emote `Angry` (or `Frown` with an option flag), eye movement 10 and blend 0.3.
- So a movie's `CastActor WORM<n>` is a fixed slot, not a team or role. Example: TinCanWally slot 0 = Player, slot 1 = CPU (Wally), slots 2-3 = Dummy1/2, slots 4-7 = Bad1-4 (respawned before Midtro), slot 8 = Player2 (Outro).
- Other actors: type 2 = Payload (PayloadLogicEntity) / MineFactory / THREAT detail, type 3 = Crate / GOODIES detail, type 4 = DISTRACT detail, type 5 = any other detail object. Detail objects are registered under their name (`Prop1`..`Prop11`, `Look1`, `OverHere`...: CastActor targets used for LookAt/GestureAt) [disasm 0x5cd1d5]. The frontend registers `FrontendWorm%d`, `Actor %d%d`, `Mouse Actor%d`.
- Actor table: 100 `WXActor` (0x74 bytes) at SceneManager+0x84 (instance 0x961870). Name at +4, speech +8, accessory +0xc/+0x14, anim +0x4c, worm index +0x6c, flags +0x6d/+0x6e. The worm graphic entity polls the actor [assumed].

### Acting scenes (WORMACTING) [disasm WXSceneManagerService, `.cpp` 0x869c0c]
- At load (0x60e100), each movie is filed under every trigger name from table 0x9214f0 that its name contains (case-insensitive substring, 0x5040e0). There are 48 triggers:

  TimedPayloadFive..One, BlastSplat, FallSplat, Idle, Sick, Abducted, DamageInflicted, DamageSilent, Boring, Mistake, Death, Collect, ShortOnTime, SkipGo, Punch, CrateDrop, WeaponFired, FirstBlood, MaxDamage, StartTurn, Waiting, Airstrike, Blasted, WormBounce, FireDamage, Revenge, Missed, Victory, Targeted, Poisoned, Zap, TauntMelee, TauntRanged, TauntStrike, Titter, GrenadeFive..One, ItemReact, Bored, Retreat, Thinking.

  N = `atoi(name + len(trigger))`, i.e. the digits after the trigger name (Death**5**a, Idle**100**a).
- Each trigger's list is **sorted by N, highest first** (0x60d5e0). After a trigger fires, 0x60c410 rotates the head of each equal-N group, so the a/b/c variants play round-robin.
- Track tags are criteria (0x60d640): a case-insensitive search for 22 tokens (table 0x921498, bit = 1 << index). The table order fixes the bit values:

  Crit, Friend*N*, Foe*N*, Sick, Abducted, See*N*, Blind*N*, Near*N*[,*R*], Special, Payload, InFront*N*, Behind*N*, Active, OnScreen, Idle, Targeted, Interesting, Safe, Threat, Goodies, Distraction, LOS*N*.

  Each track record is 12 bytes: the flags, then bytes for Friend/Foe -> loyalty track, See/LOS -> see track, Near -> near track (radius R, **default 20** when there is no `,R`), Blind -> blind track, InFront/Behind -> relative track. *N* = another track's index; -1 appears in the data, meaning assumed: the active worm.
- `Enemy` (used in data) and `Onscreen` (lowercase, which still matches `OnScreen`) are not separate tokens.
- `Acting.Trigger` (`ActingTriggerMsg`, ctor 0x4d3410) carries {trigger index +8, subject actor +0xc}. Senders: GameLogicService 0x4fb880 and weapon/payload entities 0x50f100, 0x54d720, 0x576fc0... Handler 0x60e2e0 switches on the trigger (jump table 0x60e818). It builds candidate actor pools (for example within 10000 units), shuffles them (0x60c390, Fisher-Yates with RNG 0x68c0aa while the sync flag 0x922ab5 is cleared, so this is presentation randomness), then calls the chooser.
- Chooser 0x60d830: scenes are tried in list order, and the cast list holds 30 slots (kMaxActorsInScene), all set to 0x7f first.
  - A `Payload` track takes the payload actor and an `Active` track takes the trigger subject; the scene is rejected if that actor is absent.
  - The other tracks take the first pool actor that passes the criteria (0x60c480: loyalty, see, near, blind, relative...).
  - A `Crit` track that cannot be cast rejects the scene; other tracks stay empty.
  - The first scene that passes is played (0x60b750 / 0x60b090, log "Chosen Scene:"), and every cast actor gets the scene's priority (actor+0x68).
- Scene timelines then run through the same per-track event code as level movies (WormScenePlayerService 0x60b1b0, with a scene slot instead of slot 0).

### FMV (.wmv) [data + disasm MoviePlayerService, `.cpp` 0x869380, PlayMovie 0x6097d0]
- The table at 0x9210b8 has 20-byte records {name, kMovie id, path, w, h}:
  - Team17 = 1 (`Logos\Team17NTSC.wmv`, 1280x720);
  - MeetTheProfessor 3, Camelot 4, WildWest 5, Arabian 6, Jurassic 7, Upsell 8 (file missing on PC), Welcome 9;
  - OuttakeRecordingBooth_01-16 = 10-25;
  - OuttakeDestructAndServe, GhostHillGraveyard, JoustAboutIt, MineAllMine, TheLandThatWormsForgot, TinCanWally = 26-31;
  - the others are 640x480.
- The video renders to the texture `VideoImage0`. `Input.QuitMovie.Pressed` skips it, and `FE.MoviePlayingComplete` is sent at the end.
- When they play:
  - boot logos (`FE.PlayTeam17Movie`, `FE.PlayPublisherMovie`, `FE.PlayLegalScreenMovie`);
  - story movies (`WXMsg.PlayStoryMovie` / `DoStoryMovieThenMenu`, data `WXD.StoryMovie` = Welcome / MeetTheProfessor / theme name). GameLogicService reads `WXD.StoryMovie` at game end when `GameOver.GameType` == "Story" [assumed: theme unlock -> movie];
  - credits: `WXMsg.PlayCreditsMovie` plays `PERSIST.XOM` `CreditsFMVList` (16 outtake names, in that order);
  - the Upsell (trial);
  - the frontend movie list `WXFEP.SelectMovieList`.
- Subtitles for the story movies only: `PERSIST.XOM` `FMVSubTiles` `FMV_Welcome/Jurassic/Arabian/WildWest/Camelot/MeetTheProf` {FMV enum kFMVSTF_*, TextLines -> `FMVTextLine` {TextID `FETXT.WM1`..., TimeOffset ms}}, drawn through `CText.MovieText`.
- The `Outtake*.wmv` names match the `OUTTAKE*.XOM` levels and `Outtake*.lub` scripts, which play an EFMV called `Outtake`/`Outtake2` [assumed: the wmv files are recordings of those].
- `Data/Intro_EFMV` (AppDataService 0x4d6e59) does not exist on PC [data].

### Open
- How each trigger builds its candidate pools (jump table 0x60e818, helpers 0x60dd20/0x60df50/0x60dee0/0x60e050); what 0x7e (LookAt -1) means; the unit of the Near radius; how PathCamera Steps are timed (not opened: CameraManagerService PathCam 0x855d1c); whether a briefing pause also stops `step()`.

## 20. How to search (Comment chercher)

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
| Does an event loop, at what volume? | `fev.py -g '^weapons/FuseLoop$'`: column `loop` (loop / oneshot / loop_to_end / silent). Gain = 10^((vol_dB + sd_vol_dB + cat_dB)/20). FSB sample: `fev.py --json`, `sounddefs[i].waves[j]` = {bank, index, ms} (§12). |
| Name of a message handle in disasm | `disasm.py` prints `msg Name` on handle operands; otherwise `pe.py msg 0xVA`. All handles of one name: `pe.py msg '^Name$'`, then `xref.py` on each. |
| Who reads or writes struct field +OFF | `xref.py --field 0xOFF --in FUNC...`. For a class, list its functions from `pe.py rtti` vtables. To name the field, match offsets with `pe.py schema` (pData of a worm = `WormDataContainer`, §11). For members bound to data keys, read `lea reg,[this+OFF]` followed by the key-name push in the ctor (CMS 0x51f740, GLS 0x4f7d80). |
| Enum field values | `pe.py schema CLASS` prints the value names under enum fields (record +0xc -> descriptor -> name list). Example: WeaponType 4 = kThrown. |
| Which class a factory creates | `disasm.py` prints `class Name` on the descriptor pushed before `call 0x639b83` (XOM CreateObject). For switches: `pe.py words` on the byte table and the jump table (`movzx eax, byte [eax+B]; jmp [eax*4+J]`), §13. |
| FPU compare branches | after `fcom; fnstsw ax`: `test ah,5; jp` jumps when st0 >= src; `test ah,0x41; je` when st0 > src; `test ah,1; jne` when st0 < src; `test ah,0x44; jp` when not equal. |
| Worm anim event -> clip | callers of QueueEvent 0x5acb80 (last `push imm` = event code), consumer 0x5a3620 jump table 0x5a410c, clip slots registered at 0x5a49a0 (§11). |
| Which camera / target for targeting weapons | Blimp = `IsometricCam` (view 3); target ray `CMS::UpdateTargetInfo` 0x51c910 writes `Airstrike.*`; cursors from `Weapon.Create*Cursor` in 0x5009d0 (§10). |
| Scheme and timer values | `xom.py list Tweak/LOCAL.XOM Scheme`, then `xom.py dump Tweak/LOCAL.XOM '#N'`; timers in TimerLogicEntity 0x50f980 (§14). |
| Bundle contents | `xom.py list Bundles/BundlNN.xom Descriptor` (the descriptor u16 is the bundle number); `xom.py check` is exact on all bundles (§15). Geometry and clips: `tools/w4m-models`. |
| Net message fields and wire order | `pe.py schema 'Msg$\|MessageArray'`; registration order: `push 0x88xxxx` before each `call 0x70daf0` in 0x705bce (§16). |
| Menu tree | `tweak.py`, then load the PC menu databanks in exe order (§17) and walk `ChildrenItems` and `Messages_*` (`FE.ChangeMenu$X`, `WXMsg.CreatePopUp$X`). Clip lengths: `w4m-models --list Data/Bundles/Bundl10.xom`. |
| AI parameter reads | refs to the global 0x9560f4, then `[reg+disp]` against `pe.py schema AIParametersContainer` (§18). |
| A level cutscene | `xom.py list <Level>.xom EFMV`, Movie -> Track -> Event refs; Lua: `lua.py --all 'EFMV'` (§19). |

## 21. Not covered

- **Worms (§11):** the easing of DrownFloat and of helper 0x569f20; the slide-arms math; the weapon clip chaining Draw -> Hold/Aim -> Fire -> Taunt is assumed from names.
- **Audio (§12):** the property-block fields +04/+08/+0C (pitch, pitch and volume randomisation) are named from FMOD Designer's order; some other fields, the sound-definition play-mode enum and the reverb block are unknown. Whether something else starts the HudClockEntity ClockFast/ClockSlow instances.
- **Weapons (§13):** what crate types 1 and 3 are (hidden from CrateSpy); what spawns kWeaponFatkinsFood and kWeaponSentryGunPayload; readers of ColliderFlags. WeaponType has a single reader found (0x5973e6).
- **Turn (§14):** acting trigger ids 0x1e and 0x2e are Missed and Retreat (name table 0x9214f0, docs/worm-reactions.md); camera mode 0xe, which freezes the turn clock, is not identified; RopeTime looks unused.
- **Tactical (§10):** the render layer of the screen-centre reticle, and the left/right sense of the airstrike direction.
- **Bundles (§15):** the 3 bytes after `CTNR`, the trailing bytes of `XMeshDescriptor` / `XCustomDescriptor`, key types 0x200 and 0x403, and the "cycle" channel setting.
- **Network (§16):** the wire class id is assumed to be the registration order; the replay of received input messages is inferred from the structure, not traced.
- **Frontend (§17):** the per-frame update of menu entities; the `WXMsg.ScrewMenu` title stage; the Select path is assumed by symmetry with Cancel.
- **AI (§18):** some AITWK fields have no reader (MortarMaximumAimAngleAllowed, AddScoreTeleport, WeightTeleport*, WeightRetreat*).
- **EFMV (§19):** acting pools per trigger (jump table 0x60e818) and the Near radius (20·R units, squared at 0x60c4d0) are now in docs/worm-reactions.md; the `TargetCastMember -1` target (0x7e, position from [0x95a100]+8 vfunc 0x38) is assumed to be the camera; the PathCamera step timing; the audio fades on `EFMV.Play` / `EFMV.Terminated`.
- **Rendering (§8):** the enum value orders are now read from the exe (`pe.py schema`); which render bin `kPS_Default` maps to is still unknown. Bloom and blur classes have no PC shader.

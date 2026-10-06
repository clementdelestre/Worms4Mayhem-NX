# W4M EFMV, acting scenes, FMV

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 19. EFMV cutscenes, acting scenes, FMV

EFMV is the in-engine cutscene system. One timeline format covers three uses:
1. scripted level movies (Intro/Midtro/Outro...), played by `EFMVMovieLogicEntity`;
2. the 142 bystander "acting" scenes in `Tweak/WORMACTING.XOM`, cast and played by `WXSceneManagerService` + `WormScenePlayerService`;
3. the outtake levels (`OUTTAKE*.XOM`), which are ordinary level movies.

The `.wmv` files are separate. `MoviePlayerService` plays them full screen.

### Data model [data: pe.py schema, xom.py; all 495 level movies and 142 acting scenes decode exactly]
- `EFMV_MovieContainer` {Tag, Track ref[]}. `EFMV_TrackContainer` {Tag, Event ref[]}. Every event derives from `EFMV_BaseEventContainer` {Tag str (editor label), **Time u32 = ms from movie start**, Critical bool}.
- The movie name is the `XContainerResourceDetails` key, e.g. TINCANWALLY has `GoldCut`, `Intro`, `Midtro`, `Outro`. Events inside a track are **not stored in time order**. The player keeps one cursor per track and fires events in file order while `Time <= now`; the cursor stops at the first later event, so an earlier-timed event stored after it waits for it [disasm 0x526cb0 level movies, 0x60b6db acting scenes]. Sorting by Time is therefore not safe: keep file order (1 of the 479 acting tracks is out of order).
- 106 level files hold 495 movies. The most common names are Outro 59, Midtro 38, Intro 21, `EFMV.Intro` 21 (tutorials/challenges), `EFMV.Intro_Dialogue`, `EFMV.Failure`, `EFMV.Success_*` and `Pointless_cut`. `ANIMTEST.XOM` and `MOVIETEST.XOM` are dev tests. `Tweak/LOCAL.XOM` has a `TestActing` movie (actor `STANDIN A`).
- Worm events derive from `EFMV_WormBaseEventContainer` (no fields of its own). They act on the **track's cast actor**: one track = one cast member, and the track's index in `Track[]` = cast-member index.

| Event (fields after Tag/Time/Critical) | Effect [disasm 0x5257b0 / 0x60b1b0 unless noted] |
|---|---|
| CastActor {ActorName} | binds the track to the actor with that exact name (`FindActor` 0x60c190, `strcmp`; not found = 0x7f, and the track's worm events are then dropped). Ignored when the scene was pre-cast (acting scenes, flag at this+0x318) |
| WormEmote {Emote, PermittedEyeMovement, BlendTime s, Coyness, AllowBlink} | facial emote on the actor (actor+0x58/0x54/0x5c, blink = bit 2 of +0x6d) |
| PlayAnimation {Animation} / StopAnimation {BlendTime} | worm clip name (actor+0x4c) / stop with a blend (+0x50). StopAnimation.BlendTime is in ms: the pose manager fades the clip by 1 / (BlendTime / 20) per 20 ms update (0x59e874), 0 = no fade (0x59e872). WormEmote.BlendTime (0.3) is a separate field in seconds (PoseBlend) [disasm] |
| WormLookAt / WormGestureAt {TargetCastMember i8} | look at / point at another track's actor. Target = own track: stop looking (0x7d). Target < 0: special target 0x7e = the render camera: its position is [0x95a100]+8 vfunc 0x38 (0x59d47e), the object whose matrices CMS 0x51b3b0 projects with [disasm] |
| TriggerSpeech {Speech, FullVolume, Duration ms} | `*Name` = level EFMV sample `EFMV/<Level>/<Name>` (schema help string 0x8810f0). A plain name = a speech category of the actor's own voice bank (`FriendlyDeath`, `Startled`...). The scene player (0x60b3d7) only stores Speech in actor+8 and FullVolume in bit 1 of +0x6e: Duration is not read and no Comment is posted (the text is the movie's own Comment events) [disasm]. Playback: §19 "Movie details" |
| SpawnAccessory / SpawnParticle {ResourceId, AttachmentPoint}, ClearAccessory | attach a hat or prop / a particle to a bone. ClearAccessory = "CLEAR" |
| ThreatenWorm {Threatened} | flag at actor+0x6e bit 0 |
| CutCamera {Position, LookAt} | not a camera of its own: Camera.Path.* with one knot each, Loop / Tension / Steps 0, +0x32 = 0 (the movie does not wait), then `Camera.Path.Start` (0x525b04..0x525bdf) [disasm] |
| PathCamera {PositionKnotList, LookAtKnotList (comma lists of locators), Loop*, *Steps u32, *Tension, DrawDebugDots} | `Camera.Path.*` data, +0x32 = 1 + `Camera.Path.Start`. The movie waits for `Camera.Path.Stopped` before it ends. Camera: §19 "Movie cameras" |
| TimedPathCamera {same, *Steps = comma string per segment} | `Camera.TimedPath.*`, +0x32 = 1 + `Camera.TimedPath.Start`. A segment of n steps takes n + 1 camera updates of 10 ms (§19 "Movie cameras"); TinCanWally's Cam_3,Cam_4,Cam_5 '500,500' = 10.02 s [disasm + data]. The 500.0 passed to the TimedKnotList constructor (0x637b50) is not used |
| ShakeCamera {Duration ms, Magnitude} | `Camera.Shake.Length/Magnitude` |
| Comment {Comment = text id, Duration ms} | subtitle line via `CommentaryPanel.Comment` / `.Delay`, e.g. `M.Wild.Tin.EFMV.3a`. The ids are in the language files |
| FailureComment {Duration} | nothing once `Challenge.Success` is 1; else `Miss.Generic.Lose<n+1>`, n = 0x68c0aa() % 5, as TimedText with Delay = Duration (0x526962..0x526a2f). 0x68c0aa is the graphical LCG (0x96d040), not the logical one of RandomNumber.Get (0x96d034): presentation, out of sync [disasm]. GameLogicService writes `Challenge.Success` 1 / 0 at a challenge's end before its game-over movie (0x4fcda1, 0x4fcf45) [disasm] |
| TriggerSoundEffect {EffectName, Location, Looping, Duration} | Foley/Amb event of the level EFMV bank, placed at a locator. Only one looping SFX at a time (assert) |
| CreateEmitter {EmitterName, Location, Locator, UserId} / DeleteEmitter {UserId} | particle FX at a locator (`Particle.Name/DetailObject/Locator`) |
| CreateExplosion {Location, WormDamage*, Impulse*, LandDamageRadius, ParticleEffect, ImpulseOffset} | a real explosion (`Explosion.*` data, 0x4f9970); it is always Critical |
| SpawnWorm {WormId, DataId} / UnspawnWorm {WormId}, SelectWorm {WormId}, SelectWeapon {WeaponId} | manage gameplay worms (`Worm.Respawn`, `WXWormManager.UnspawnWorm`). SelectWorm sets the data `ActiveWormIndex` (setter 0x69e100) then posts `WormSelect.WormSelected`, which nothing handles. SelectWeapon writes WormData[ActiveWormIndex].WeaponIndex (+0xf4) and posts `Weapon.Selected` (worm graphic and logic, ActiveWormHudInfo, CommentService, Lua `Weapon_Selected`) [disasm 0x525fd3, 0x526032]. Only ChallengeSheep2's Intro uses them (0 and 19 = kWeaponSuperSheep) [data] |
| RaiseWater {Delta} | `Water.Level` += Delta |
| CreateBorders / DeleteBorders | post `EFMV.Start` / `EFMV.End` (0x52610b, 0x526149): letterbox `EfmvBorderEntity`, TWEAK `EFMV.BorderHeight` 50, `BorderOnTime` 0, `BorderOffTime` 500; §19 "Borders and subtitles" |
| CreateBriefingBox {MessageId[]}, CreateWXBriefingBox {Type, TextId, Image} | tutorial briefing dialog (`WXD.BriefingText` / `WXD.BriefingImage`) |
| Stop | when a briefing dialog is up: pause (this+0x30, AppDataService 0x4d7690) until `Game.BriefingDialogOkPressed`. Used on "Pause Movie" tracks |
| Joypad{Button,Stick}, Create/Animate/DeleteCustomHudGraphic, SetPointLightColor | tutorial input demos (they clear input slot 5 and inject pad input, 0x5262a6 / 0x526310), HUD overlays, lights |
| AnimateDetail {FourCC, AnimName} | `Detail.AnimName` = AnimName, then StringMessage `Detail.PlayAnim(FourCC)` (0x526541); LandscapeLogicEntity 0x4758d0 compares the first 4 bytes with each detail's code (the 4 bytes 5 past "CODE" in its name, 0x5cd8e1) and 0x5ccc80 plays that clip from 0 at speed 1, no fade, replacing the detail's other clips (a running Go stops for good), its loop flag (XAnimScheduler AddAnim 0x7a9cf8, entry flags bit 0) 0 where Go / GoSync pass 1. At its end (0x7a8c7e) the instance writes each channel's last key once (0x7ad288) and the clip leaves the scheduler: the detail holds that last pose [disasm]. Story use: ValleyOfDinoWorms Outro `TIME` / `Launch` (detail `VISIBLE CODE TIME`, D01_01, 4.38 s) [data] |
| DeleteLandframe {Code} | StringMessage `Land.ClearCoded` (0x52681f) -> LandscapeLogicEntity 0x475170: §19 "Coded land frames" |

- Critical events by type: DeleteBorders 364/371, CreateExplosion 143/143, DeleteEmitter 73/73, DeleteLandframe 31/46, SpawnWorm 21/21, RaiseWater 4/4 [data]. They are exactly the events that must still happen when the movie is skipped.

### Speech, lip-sync, sound banks [data + disasm]
- AudioService loads the bank `EFMV/<Level>` per level, plus `Story.`/`Tutorial.`/`Challenge.`/`Deathmatch.` prefixes (0x604760, 0x606480). It also loads the lip file `EFMV/<Level>/LIP` = install-root `EFMV/<Level>/LIP.txt`. Its rows are `frame,VISEME,<none>` at **about 30 fps**: the last frame x 33 ms matches TriggerSpeech.Duration, e.g. 50 frames vs 1797 ms [data].
- `Data/Audio/EFMV/<Level>.lsd` maps `EFMV/<Level>/<Line>` to the line hash, which is also the LIP.txt `#hash`. The speaker comes from the line name `<Level>_<Role>_NN`, not from the bank.
- `EFMV/Failures/Failures_Narrator_01-05` (0x8673d4) is a shared failure bank.
- AudioService (0x606fc4) sets two music timers: `EFMV.Play` starts a 500 ms fade-out (+0x24), `EFMV.Terminated` a 2000 ms fade-in (+0x20). Its update 0x605720 sets the music event volume (vfunc 0x38) to `Audio.Vol.Music` × remaining / 500, or × (1 − remaining / 2000): the music goes silent for the movie and comes back over 2 s. The fade-in timer is read first; with neither running the volume is not touched (held) [disasm].
- Only English speech exists on PC: one set of banks, no per-language audio (`lang_eng`) [data].
- **Event names** [disasm]: TriggerSoundEffect plays `EFMV/<Level_FileName>/<EffectName>` (prefix built at movie start 0x526aa0 from `WXFE_LevelDetails.Level_FileName`; level "Test": none). A `*` TriggerSpeech plays `EFMV/<X>/<Speech+1>` (0x59cc70), X = `WXD.Level.Current` after its '.' for `Story.*`, `EFMV/Tutorial<X>/` for `Tutorial.*`, else `EFMV/Test/` (no such group: silent); only story and tutorial levels hold `*` lines [data: 434 of 436 TriggerSpeech are `*`, none in challenges or deathmatches]. FMOD looks groups and events up case-insensitively (fmod_event.dll 0x100051f0, tolower 0x10005110, from EventGroupI::getEvent 0x10010ae0 / getGroup 0x10025c20) [disasm]: `DeathMatch1` finds group `Deathmatch1`. A name missing from the FEV (`mymycheer`, `creakingwood`, 102 events) logs "Failed to find an event" and is silent [data + disasm 0x6f98c0].
- **TriggerSoundEffect** (0x52618c) [disasm]: position = the Location locator (0x4f9400). Looping (3 events of 1029): PlaySound into the movie's one instance (+0x58, assert "Only one looping sound effect currently permitted"), placed, started; the shutdown 0x525540 stops (vfunc 0x14) and releases it. Otherwise fire-and-forget (0x604a80, placed): an FEV loop event started that way (Foley_WaterLap, Foley_TMLoop, Foley_LabLoop, Foley_WaterLap2, Duration 0xFFFFFFFF) never stops. Duration is not read there.
- **FEV data** [data: fev.py]: 958 EFMV events, 840 with their waves on disk (the Outtake* banks are missing); 2D, oneshot, max playbacks 1, 0 dB except 28 (−2 to −15.7); 5 are 3D linear (Foley_JukeBoxTune 1..600, TMLoop 1..2000, CatLaunch 10..700, Cannon / Splash 1..10000 units); 6 FEV loops; 7 with a Time parameter: the Foley_Whistle_2s/3s/4s volume envelopes cut the BombWhistle wave at 2.04 / 3.04 / 4.03 s, WaterLap2's opens and closes it over 30 s, JukeBoxTune's is a pitch envelope (flags 0x14) but no movie triggers that event. The parameter holds at its end (param flags 3, bit 2: fmod_event 0x10023bab) [disasm]. Foley_Pickup (TinCanWally) plays a JoustAboutIt wave.
- **Lip sync (WormPoseManager)** [disasm]: 0x59cf50 binds the viseme clips `A` +0x94, `Cons`, `EI`, `FV`, `L`, `MBP`, `O`, `QUW` +0xb0 (0.04 s clips of the worm library, all keying the lips and `Blend` Scale.x). LIP rows are parsed by 0x605d40: the viseme's first letter A C E F L M O Q R -> 1..8, 0 (table 0x606468, jump table 0x606440); entries {u32 viseme, u16 frame}. The row set is that of the instance's hash: XSoundInstance::Start (0x6fde90) asks the manager (vfunc 0x88 = 0x6fae10) for the .lsd hash list of the event at the MultiSelect index (0 without that parameter), into +0x2c (getter 0x6fe170); so a voice line's rows are its played wave's, an EFMV line's its only hash.
  - Start (0x59cb20 voice line, 0x59cc70 EFMV line): clock +0x1b0 = 0, open target +0x1c8 = 0, cursor 0, rows +0x1b8 none, the old instance released, PlaySound into +0x1d4.
  - Update 0x59d570, from 0x59da40 every 20 ms tick (dt = elapsed / 20 ms): SetTimeAndWeight(new +0xdc, w = t = blend × +0x17c) and (old +0xe0, (1 − blend) × +0x178); blend > 0.9: blend = 1, old dropped (weight 0); blend = (2 blend + 1) / 3; open +0x19c = (2 open + target) / 3. With the clock at 0, an instance and no rows, the rows are looked up (0x605c60; none: both clips dropped). frame = trunc(clock / 25 × 30). If the cursor row's frame ≤ frame: the cursor passes every such row; at the end of the rows both clips are dropped and the rows cleared (the last row never shows); else the last passed row applies: +0x178 = +0x17c, Rest: +0x17c 0, target 0, others +0x17c 1, target A 0.5, Cons 0.8, EI 0.5, FV 0.9, L 0.6, MBP 0.9, O 0.8, QUW 0.9; a different viseme drops the old clip, the new one becomes old, blend 0. While it has rows the clock gains dt × 0.5 (0x59db90): 25 a second, frame = 30 a second.
  - Teeth (0x59db24, every update): clip `Teeth` (keys only the mouth XChildSelector `$animTex0`, value 0.5 + t) at weight 1 and t = `Blend`.Scale.x (+0x60 of the node) when open > 0.1, else 1 if `Blend`.Translate.z (+0x50) > 0.4, else 0. Scale.x is keyed only by the visemes (A, EI, L 2; Cons, FV 1; MBP, O, QUW 0) and the WFGun / Chat clips; Translate.z by the `*Mouth` emotes that bare the teeth (Happy, Angry, EvilGrin... 1) and some gestures [data: w4m-models W4M_CHANNELS].
  - The only player of `Speech/<voice>/<line>` is 0x59cb20 (the other "Speech/" users load banks and LIP files: 0x605880, 0x6077c0, 0x607950): every worm line is lip-synced.
- **Failure narration** [disasm]: CommentService (0x5e4ca0) stores atoi(id + 17) in +0x148 when a TimedText id contains `Miss.Generic.Lose`; SubtitleGraphicEntity (created instead of the commentary box in subtitle mode, 0x5de5b0) plays `EFMV/Failures/Failures_Narrator_0N` fire-and-forget as it takes its next line, N 1..5, and clears +0x148 (0x5f90a0). Outside subtitle mode (e.g. lib_DisplayFailureComment in play) no narration.

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
- **Skip**: `Input.QuitEFMV` is ignored when `EFMV.Unskipable` != 0. Otherwise 0x526d90 loops `step(skip=1)` to the end, firing **only Critical events**, then kills the entity, so `EFMV.Terminated` still fires. `Edit.StopEFMV` does the same. The skip is disabled while [0x95b5e8]+0x2c == 1: 0x95b5e8 is the SavingIconService instance (0x50dbe6) and +0x2c is set when it shows `FE.SavingIcon` (0x50de8d), cleared when it hides (0x50dd10): no skip during a save [disasm].
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
- Actor table: 100 `WXActor` (0x74 bytes) at SceneManager+0x84 (instance 0x961870). Look target +0, gesture target +1, name +4, speech +8, accessory +0xc/+0x14, anim +0x4c, stop blend +0x50, worm index +0x6c, flags +0x6d/+0x6e (+0x6e bit 0 threatened, bit 2 bored), scene +0x70. The worm graphic entity reads it each update (look target: 0x59d470 via 0x60c170; bored bit: 0x5a4822) [disasm].

### Acting scenes (WORMACTING) [disasm WXSceneManagerService, `.cpp` 0x869c0c]
- At load (0x60e100), each movie is filed under every trigger name from table 0x9214f0 that its name contains (case-insensitive substring, 0x5040e0). There are 48 triggers:

  TimedPayloadFive..One, BlastSplat, FallSplat, Idle, Sick, Abducted, DamageInflicted, DamageSilent, Boring, Mistake, Death, Collect, ShortOnTime, SkipGo, Punch, CrateDrop, WeaponFired, FirstBlood, MaxDamage, StartTurn, Waiting, Airstrike, Blasted, WormBounce, FireDamage, Revenge, Missed, Victory, Targeted, Poisoned, Zap, TauntMelee, TauntRanged, TauntStrike, Titter, GrenadeFive..One, ItemReact, Bored, Retreat, Thinking.

  N = `atoi(name + len(trigger))`, i.e. the digits after the trigger name (Death**5**a, Idle**100**a).
- Each trigger's list is **sorted by N, highest first** (0x60d5e0). After a trigger fires, 0x60c410 rotates the head of each equal-N group, so the a/b/c variants play round-robin.
- Track tags are criteria (0x60d640): a case-insensitive search for 22 tokens (table 0x921498, bit = 1 << index). The table order fixes the bit values:

  Crit, Friend*N*, Foe*N*, Sick, Abducted, See*N*, Blind*N*, Near*N*[,*R*], Special, Payload, InFront*N*, Behind*N*, Active, OnScreen, Idle, Targeted, Interesting, Safe, Threat, Goodies, Distraction, LOS*N*.

  Each track record is 12 bytes: the flags, then bytes for Friend/Foe -> loyalty track, See/LOS -> see track, Near -> near track (radius R, **default 20** when there is no `,R`), Blind -> blind track, InFront/Behind -> relative track. *N* = another track's index. −1: for See, Blind and Near the render camera (0x60c78e, 0x60caf2, 0x60d07d); for Friend/Foe the team stored at SceneManager+0x2e39 when a worm becomes active (0x5aaec3) [disasm].
- `Enemy` (used in data) and `Onscreen` (lowercase, which still matches `OnScreen`) are not separate tokens.
- `Acting.Trigger` (`ActingTriggerMsg`, ctor 0x4d3410) carries {trigger index +8, subject actor +0xc}. Senders: GameLogicService 0x4fb880 and weapon/payload entities 0x50f100, 0x54d720, 0x576fc0... Handler 0x60e2e0 switches on the trigger (jump table 0x60e818). It builds candidate actor pools (for example within 10000 units), shuffles them (0x60c390, Fisher-Yates with RNG 0x68c0aa while the sync flag 0x922ab5 is cleared, so this is presentation randomness), then calls the chooser.
- Chooser 0x60d830: scenes are tried in list order, and the cast list holds 30 slots (kMaxActorsInScene), all set to 0x7f first.
  - A `Payload` track takes the payload actor and an `Active` track takes the trigger subject; the scene is rejected if that actor is absent.
  - The other tracks take the first pool actor that passes the criteria (0x60c480: loyalty, see, near, blind, relative...).
  - A `Crit` track that cannot be cast rejects the scene; other tracks stay empty.
  - The first scene that passes is played (0x60b750 / 0x60b090, log "Chosen Scene:"), and every cast actor gets the scene's priority (actor+0x68).
- Scene timelines then run through the same per-track event code as level movies (WormScenePlayerService 0x60b1b0, with a scene slot instead of slot 0). Its task 0x60b940 returns 20: every 20 ms each running scene fires its due events (0x60b640) and its clock gains 20 ms (0x60b72b); while `EFMV.Active` (+0x96c, bound at 0x60b627) is 1 it ends every running scene instead (0x60b96c) [disasm].

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
  - story movies (`WXMsg.PlayStoryMovie` / `DoStoryMovieThenMenu`, data `WXD.StoryMovie` = Welcome / MeetTheProfessor / theme name). GameLogicService reads `WXD.StoryMovie` at game end when `GameOver.GameType` == "Story". The last mission of each story theme sets it to the next theme's movie: DestructAndServe "Camelot", NiceToSiegeYou "WildWest", HighNoonHiJinx "Arabian", GibbonTake "Jurassic" [data: lua.py];
  - credits: `WXMsg.PlayCreditsMovie` plays `PERSIST.XOM` `CreditsFMVList` (16 outtake names, in that order);
  - the Upsell (trial);
  - the frontend movie list `WXFEP.SelectMovieList`.
- Subtitles for the story movies only: `PERSIST.XOM` `FMVSubTiles` `FMV_Welcome/Jurassic/Arabian/WildWest/Camelot/MeetTheProf` {FMV enum kFMVSTF_*, TextLines -> `FMVTextLine` {TextID `FETXT.WM1`..., TimeOffset ms}}, drawn through `CText.MovieText`.
- The `Outtake*.wmv` names match the `OUTTAKE*.XOM` levels and `Outtake*.lub` scripts, which play an EFMV called `Outtake`/`Outtake2` [data]. Nothing in the exe links a .wmv to a level: they are only named alike (the credits list plays the .wmv files).
- `Data/Intro_EFMV` (AppDataService 0x4d6e59) does not exist on PC [data].

### Movie cameras [disasm, checked on data]
- PathCam (ctor 0x531580, vtable 0x855d1c) and TimedPathCam (0x637880, vtable 0x870830) are the only cameras passing type 0xe to the
  Camera constructor 0x51b570. Factors +0x4c / +0x50 / +0x54 stay 1 / 1 / 1, zoom 1, cut flag 0: the drawn view is the logical one at
  each update, no blend; up (0, 1, 0), never rolled (0x531880); the drawn lens (51.28 degrees, camera-w4m.md §11.5). The camera shake
  applies to them as to any logical camera (0x51dcde).
- `Camera.Path.Start` / `TimedPath.Start` -> CMS 0x522dbb -> SetCamera, which always deactivates the current camera (lists freed) and
  activates a new one: each camera event restarts from scratch. Activation builds the lists and makes the first step at once.
- Knot lists (0x52c140): strtok on " ," (space or comma); each name is the first level detail (all land frames' detail lists, 0x955740)
  whose name matches exactly (strcmp 0x638a62), its world position only (detail +8). An unknown name logs "Unable to find a knot in the
  landscape called: X" and leaves stack garbage. 891 PathCamera lists in the data all have at least one knot [data].
- PathCam step (0x52bb40), once per camera update, i.e. every 10 ms (CMS twice per 20 ms frame): the last knot reached ends the list
  (output that knot, true); else output = cardinal spline of P0 = prev (or cur), P1 = cur, P2 = next, P3 = next.next (or next), s = (1 -
  Tension) / 2: c0 = -s t³ + 2s t² - s t, c1 = (2 - s) t³ + (s - 3) t² + 1, c2 = (s - 2) t³ + (3 - 2s) t² + s t, c3 = s t³ - s t²; then
  t = float(t + 1 / Steps) (the sum in double), Steps re-read each update from `Camera.Path.Steps.Position` / `.LookAt`; past 1 (strict)
  t = 0, the next knot, `Camera.Path.Knot` = its name and `Camera.Path.Reached.Knot`. Steps 0: one update per knot. A segment so takes
  Steps + 1 updates when the float sum stays under 1 (50, 300, 500, 600, 1000, 3000), Steps when it does not (25, 70, 250, 1300, 2000).
- The two lists (position, look-at) run on their own; once both ended `Camera.Path.Stopped` is posted (+0x32 = 0 in the movie, 0x5273ec),
  and again at every update while the camera stays current. A list of one knot ends at activation (CutCamera; LookAtSteps 0 with one
  look-at: a fixed look). Loop links the ends: never stops (no data uses it).
- TimedPathCam (0x637b50) reads only `Camera.Path.Knots.Position`, `Loop.Position` and `Camera.TimedPath.Steps.Position` (atoi of each
  " ,"-token: '400.400,400' = 400, 400): the look-at fields are written but never read. Two TimedKnotLists from the same knots and steps:
  mode 0 the positions, mode 1 each knot 1000 units along its detail's world -Z (Maya XYZ Euler angles, land frame included, 0x6375a1).
  Fixed Catmull-Rom (s 0.5), t += 1 / steps[segment], the segment index past the list read unchecked (NiceToSiegeYou 'Cam_58..61' with
  '200,250'). Both lists end on the same update: `Camera.TimedPath.Stopped`.
- After Stopped the camera stays current (type 14), on its last knot, until the next camera event or the movie's end (SetCamera
  "Default", 0x525540). TinCanWally: 'Hand1Camera1,Hand1Camera1' 300 = 3.01 s; FinalCamera1 1300 with one look-at = 13.00 s; TimedPath
  '500,500' = 10.02 s; the Intro's TargetWatch (look-at 600 steps) ends the movie 80.39 s after it starts [disasm + data].
- Type 14 stops TimerLogicEntity's ElapsedRoundTime (0x50f15f) and TurnTimeRemaining (0x50f383..0x50f391, which also stops for
  `GameLogic.TurnTime.Pause` and a firing gun 0x95d9fc): from a movie's first camera event (CutCamera included) to its end. Hot seat,
  retreat and post-activity clocks run on.
- Locators are DetailEntityStore details of `Data/Maps/<Level>.xan` (Name, ResourceName "Camera", Position and Orientation local to the
  land frame); the level `.XOM` holds only the databank and the movies [data].

### Borders and subtitles [disasm]
- `EFMV.Start` is handled only by GraphicalSpawningService (0x5009d0), which creates EfmvBorderEntity (a singleton, 0x960c08). Other
  senders: GameOverLogicEntity 0x4fffb6, weapons (0x548d3c, 0x54cdf8, SuperBomber 0x58adc6 / 0x58b211) and the -w3d scripts.
- Border init (0x5e6b90): HUD.Hide, EFMV.Subtitles.On, data `EFMV.BordersActive` 1. Two black bars (colour 0, 0, 0, 255) at y = +-0x4c,
  x scale +0x48, y scale BorderHeight x f + 0x50 (update 0x5e68c0): ctor 0x5e66c0 sets +0x4c = 240 (1 - w) + 265 w, +0x50 = +0x4c - 240,
  +0x48 = (330 (1 - w) + 360 w) x aspect scale (0x4d4cc0), w the widescreen factor [0x95a100]+0x8c: in 16:9 the bars sit at +-265 of a
  +-270 HUD, half height 25 + 50 f, inner edges at 190 units from the middle. f grows over BorderOnTime (0: at once), then posts
  EFMV.Ready; EFMV.End (0x5e6e37, asserted if twice) posts EFMV.Subtitles.Off and f falls to 0 over BorderOffTime (inner edges 190 ->
  240), then the entity goes: BordersActive 0, EFMV.Ready, HUD.Show (0x5e65d0). The `EFMV.Borders` sprite resource is in no bundle:
  its quad size, read here as +-1 x scale, is the one assumed point.
- HUD: HUD.Hide / Show only toggle HudService's active-worm arrow; the HUD leaves through `EFMV.BordersActive` (BaseHudObject::Update
  0x5daf31, state 2 -> Out_* clip): clock, counter, energy bars, wind, scanner, power bar, angle, melee bar, active worm info, secondary
  weapon, blimp help, commentary box. HotSeatTimeGraphicEntity kills itself (0x5ee275). Worm names and hp hide while `EFMV.Active`
  (WormHealthNameEntity 0x5fd4e0); HudClock's last-5-seconds ticks stop then (0x5eff38).
- CommentService (0x5e4ca0): Subtitles.On sets +0x68, kills the shown box at once and empties the queue; EFMV.Ready creates
  SubtitleGraphicEntity in subtitle mode, else the box (0x5de5b0); Subtitles.Off kills the subtitle and empties the queue. In subtitle
  mode only TimedText, ScriptText, DebugText, Clear, NoDefault, EnableDefault are handled (0x5e510f): no death, crate, turn or weapon
  comments. The movie's end (skip included) empties the queue and hides the line in subtitle mode (0x5dff70 -> 0x5f9290).
- AddComment (0x5e0680) in subtitle mode: no `<CLS>`, width 600 at scale 20 (the box: 300, 18); a line breaks back at a ' ' or '-' (the
  next line starts with it), each line its own entry with the full delay (Duration; 0 -> 1200 ms).
- SubtitleGraphicEntity (init 0x5f93b0, update 0x5f9350, pop 0x5f90a0): one FE.Font text, scale 20, at (0, -210), centred, white, its
  shadow colour 0 (premultiplied: none), no panel, no fade; pops an entry, shows it for its delay (game time), hides it, the next at the
  following update: one line at a time, FIFO. Its creation value 0x52 is a render layer (82 of 88, GRM 0x6af860; the box's 0x2a = 42): drawn over the commentary box and the HUD [disasm; bins drawn in rising order assumed, not traced]. A `Miss.Generic.Lose<n>` line plays `EFMV/Failures/Failures_Narrator_0<n>` when shown
  (CommentService +0x148); the box never does.

### Input during a movie [disasm]
- Start sets input override slot 5 = group `EFMVMovie` (0x507f40); the highest active slot imposes a single group (0x5074a0): only 6
  Menu, 9 ManualCam, 10 AttractMode outrank it. So no worm movement or weapon input while a movie plays; Shutdown clears the slot.
- EFMVMovie: PC Space (DIK 0x39) = `Input.QuitEFMV`, Esc = `App.Pause` (0x4e1360); pad: Back (button 6) and Y (button 3) = QuitEFMV,
  on the press, any pad (0x4e4a20).
- Neither the borders nor GameLogic touch the input; AIService does not read `EFMV.Active`.

### TriggerSpeech playback [disasm 0x59e63d..0x59e6ee]
- The worm graphic reads actor+8 once and clears it. `*Name` -> 0x59cc70: event `EFMV/<Level>/<Name>` (Level = WXD.Level.Current after
  the '.' for "Story.*"; "Tutorial.*": `EFMV/Tutorial<X>/`), always cutting any other worm speech (0x5b2a40). A plain name -> 0x59cb20:
  `Speech/<team bank, or voclassi>/<category>`, silent when a worm already speaks.
- FullVolume: at the camera, else on the worm's node; but the 907 `EFMV/*` dialogue events are 2D in the FEV [data fev.py]. Both paths
  reset the lip-sync (+0x1b0 0, +0x1b4 30 fps) and key it (+0x1d0).

### Coded land frames (DeleteLandframe) [disasm + data]
- LandFramePseudoEntity init (0x46e1d0): a frame whose LandFrameStore Name contains "CODE" takes the u32 5 bytes past it ("CODE:JEFF",
  "CODE AAAA") into a table (0x9553e0, up to 100) and its whole subtree carries that index (+0x14, the traversal 0x4765a0); coding inside a
  coded hierarchy asserts. Name also carries SLIPPY, PERM, TEAMBASE<n>, NOSHADOW, TEAMCOLOUR, EXPORT.
- Land.ClearCoded (0x475170): every index whose code equals the event Code's first 4 bytes (exact, case-sensitive); each frame of it
  (0x4747f0) goes at once: its graph and chunks, its details (graph, emitter, point light), every voxel of its bits in the global bitset
  0x9533b8; neighbour chunks rebuilt. Then one `Land.NewShape` per merged bounding sphere (at most 10): worms whose support voxel is gone
  turn Ballistic (0x5b0824). No debris, particles, sound or damage.
- Data: DestructAndServe EasterMovie `JEFF` (Critical, 2970 ms): frame 349, the DeLorean, 46 frames, 241 cells, no detail.
  TheWindyWizard MidSequence2 `AAAA` (Critical, 11420 ms): frame 445, one invisible cell, no detail. Tutorials: ESEQ, DOOR, PBAG, POD1-4,
  DING. Visible 0 frames are still solid in W4M: only rendering reads it (0x46e6b9, 0x472380, 0x447810).

### Open
- How each trigger builds its candidate pools: docs/worm-reactions.md (jump table 0x60e818). 0x7e (LookAt -1) is the render camera (above). The Near radius: 20·R units (0x60c4d0). Still open: whether a briefing pause also stops `step()` (no briefing in the 51 levels' movies).

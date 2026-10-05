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
| TriggerSpeech {Speech, FullVolume, Duration ms} | `*Name` = level EFMV sample `EFMV/<Level>/<Name>` (schema help string 0x8810f0). A plain name = a speech category of the actor's own voice bank (`FriendlyDeath`, `Startled`...). Duration = line length, used for the matching Comment |
| SpawnAccessory / SpawnParticle {ResourceId, AttachmentPoint}, ClearAccessory | attach a hat or prop / a particle to a bone. ClearAccessory = "CLEAR" |
| ThreatenWorm {Threatened} | flag at actor+0x6e bit 0 |
| CutCamera {Position, LookAt} | cut to two locator names |
| PathCamera {PositionKnotList, LookAtKnotList (comma lists of locators), Loop*, *Steps u32, *Tension, DrawDebugDots} | `Camera.Path.*` data + `Camera.Path.Start`. The movie waits for `Camera.Path.Stopped` before it ends |
| TimedPathCamera {same, *Steps = comma string per segment} | `Camera.TimedPath.*`. The default step is 500 [disasm 0x637b50]. A segment of n steps takes n x 10 ms: each camera update adds 1 / steps[segment] to the Catmull-Rom parameter (TimedKnotList 0x636de0, 0x636fee..0x637018), and the CMS runs cameras 100 times a second (docs/camera-w4m.md §11.1) [disasm]; TinCanWally's 500,500 = 10 s matches [data] |
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
- AudioService (0x606fc4) sets two music timers: `EFMV.Play` starts a 500 ms fade-out (+0x24), `EFMV.Terminated` a 2000 ms fade-in (+0x20). Its update 0x605720 sets the music event volume (vfunc 0x38) to `Audio.Vol.Music` × remaining / 500, or × (1 − remaining / 2000): the music goes silent for the movie and comes back over 2 s [disasm].

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

### Open
- How each trigger builds its candidate pools: docs/worm-reactions.md (jump table 0x60e818). 0x7e (LookAt -1) is the render camera (above). The Near radius: 20·R units (0x60c4d0). Still open: how PathCamera Steps are timed (not opened: CameraManagerService PathCam 0x855d1c); whether a briefing pause also stops `step()`.

# Missions and challenges

Single player content is `missions/<id>.json`: bundled ones (CC0, ours) in `romfs:/missions/`, W4M ones written by `tools/w4m-maps` to `assets/missions/` (local only). A W4M mission runs its original Lua script ("Scripts" below); the JSON teams, objects and objectives serve the bundled missions. A mission is listed only if its map exists. Team 0 is the player; the rest are CPU teams. Objectives are evaluated in the sim (`missionStep`, checksummed), so a mission plays the same for a given seed and inputs. Progress: `progress.txt` next to `setup.txt` (`<id> <done 0/1> <best ticks>` per line). Missions of a campaign unlock in `order`; challenges are always open.

```json
{
  "name": "Storm the Keep", "kind": "mission", "campaign": "Worms4NX", "order": 1, "map": "camelot",
  "brief": "...", "success": "...", "failure": "...",
  "turn_time": 45, "mines": 2, "crate_chance": 20,
  "teams": [
    {"name": "Knights", "cpu": 0, "worms": [{"name": "Sir Wiggles", "hp": 100, "pos": [40, 30, 12]}]},
    {"name": "Guards", "cpu": 1, "weapons": {"Bazooka": -1, "Skip Go": -1}, "worms": [{"hp": 60, "pos": "Archer1"}]}
  ],
  "objects": [{"type": "crate", "pos": [40, 30, 20], "weapon": "health"}, {"type": "target", "pos": "Targ1"}],
  "objectives": [{"type": "kill_all"}],
  "fail": [{"type": "turns", "turns": 12}]
}
```

| key | meaning |
|---|---|
| `kind` | `mission` (Missions tab) or `challenge` (Challenges tab). `campaign` groups and orders the list (default `Worms4NX`). |
| `preview` | `assets/ui/<preview>.png`, default: the map's level picture. `par`: W4M target time in s (shown only). |
| `scheme` | preset name (default `Standard`), then `turn_time` (s, 0 = endless turn), `retreat_time`, `hot_seat`, `wind` 0..3, `crate_chance` %, `fall_damage`, `round_time` (min, default 60: the round clock shown in the HUD), `mines`, `barrels` (random ones, default 0). |
| `teams[]` | 1..4 teams: `cpu` 0 = human, 1..5 = AI level (W4M CPU1..CPU5, clamped in `mission.cpp`), `idle` (never takes a turn: captives, practice dummies), `weapons` {name: ammo, -1 = infinite} (omitted = scheme default), `worms[]` {`name`, `hp`, `pos`}. |
| `pos` | `[x, y, z]` metres, or the name of a map marker (map JSON `markers`, imported from W4M). Worms are dropped onto the ground below; explicit object positions too (`drop`, default true for `[x,y,z]`), marker ones stay where the marker is. |
| `objects[]` | `crate` (`weapon` name or `health`), `target`, `mine`, `barrel`. Crates and targets are pinned (no gravity); crates can't be blown up, any hit pops a target (`Object::hp` 0); a sheep-like shot collects crates for its worm (docs/sim.md "Crates"). |
| `sequence` | crates / targets appear one at a time, in file order (W4M challenges). |
| `place_objects` | also put mines and oil drums on the map's `mine` / `oildrum` markers. |

Objectives (all must be met): `kill_all` (every worm of the other teams), `kill` {`team`, `worm`}, `reach` {`pos`, `radius`} (a player worm gets there), `collect` {`count`} (mission crates picked up by the player), `destroy` {`count`} (targets), `poison_all` (every other worm poisoned), `survive` {`seconds`} or {`turns`}.
Fail conditions (any): `hurt` (a player worm takes damage), `time` {`seconds`}, `turns` {`turns`} (player turns), `worm_dies` {`team`, `worm`}; losing every player worm always fails.

Score / best time: mission ticks until success (60 per second).

## W4M import

`tools/w4m-maps <W4M dir> [out]` (or `... client/assets/maps --missions` for the missions and scripts only) reads the level list from
`Data/Tweak/SCRIPTS.XOM` (story = type 4, challenges = 8, deathmatches = 9) and names and briefs from `Data/Language/PC/English.xom`. Each
mission JSON carries `script` (the `.lub` name) and `bank` (the level databank), plus the menu fields and `rain_prob` (Initialise's
`Particle.Rain.Prob`, read before the match, render only). Into `assets/scripts/` (gitignored, never committed) it copies `stdlib.lub`,
`lib_help.lub` and every level script, and writes:
- `data.json`: the named resources of LOCAL, LVLSETUP, TWEAK, WEAPTWK, AITWK, HUDTWK, CAMTWK and DEFSAVE: data keys (`keys`, a float
  keeps its `.` so the client tells int from float) and the containers of the classes the scripts use (`containers`, field names and
  types from `tools/w4m-maps/src/schema.rs`, generated from `pe.py schema`; refs and the fields some files lack are handled as `xom.py`);
- `<bank>.json`: the level databank's keys, containers and its movies (`movies`): per movie its tracks in order, per track its events
  in file order (the player's cursor order, acting.md §19), each `[type, Time ms, Critical 0/1, fields...]` with the field order of
  `pe.py schema EFMV_*` (`schema.rs`); 174 movies, 4643 events in the 51 levels [data].
Map markers: every hidden named detail is exported (`locator` when its library is no worm, crate, target, mine, drum, trigger or
telepad type), so the scripts' spawn, crate and explosion names and the movie camera knots resolve; each carries `dir`, its local -Z in
the world (TimedPathCam's look, acting.md §19 "Movie cameras"). A map with coded land frames (name "...CODE xxxx", subtree included)
gets `codes`: per 4-byte code its cells (`hex`, the `.cells` HEX ids) and the voxels only they hold (`vox`, index runs); a detail whose
name holds a code (Detail.PlayAnim) gets `code` in `objects`; a decor mesh with no Go / GoSync clip lists its first clip in its `.mat`.

## Scripts

`src/script.cpp` runs stdlib, lib_help and the level chunk on `third_party/lua-5.0.1` (base and math libraries, as W4M) [data + disasm:
docs/w4m/missions.md §23]. Patches (`tools/patches/lua-5.0.1-w4m.patch`): float `lua_Number`, 4-byte size_t in the chunk header, and
4-byte `Instruction` (Lua 5.0 uses `unsigned long`, 8 bytes on LP64 desktop and Switch) [ours: the chunk format].

- **14 C functions**: as W4M, a bad name or argument logs and returns nothing, never a Lua error. Bindings hold no C++ object with a
  destructor (a Lua error longjmps); engine -> Lua calls are protected (`lua_pcall`). Errors are counted for mission_check.
- **Data store**: `data.json` then `<bank>.json`. Live keys are refreshed on GetData (ObjectCount.Active, RoundTimeRemaining,
  CurrentTeamIndex, ActiveWormIndex, FCS.GameOver, DoubleDamage) and written through on SetData (Wind.Speed / Direction -> our wind as
  Speed / Wind.MaxSpeed, DoubleDamage, Land.Indestructable -> `Game::indestructible`, DefaultRetreatTime, Crate.HealthInCrates,
  Jetpack.Fuel, Jetpack.InitFuel, Challenge.EndlessGun, Water.Level, Mine.MinFuse / MaxFuse / DudProbability / DetonationType).
  TurnTime, HotSeatTime, PostActivityTime, RoundTime are read where the turn machine needs them; GameToFrontEndDelayTime has no reader,
  as in W4M (docs/w4m/missions.md §23.9) [disasm].
- **Enum fields** (WormData WeaponIndex, TeamData WormpotSuperWeapon, WXFE_UnlockableItem State) reach Lua as their value name less the
  enum's common prefix (State "Unlocked", WeaponIndex "WeaponShotgun"); a script writes them by full or short name [disasm 0x6c7078].
- **Containers**: Query returns a field table, Edit `(lock, table)` and Close writes the table back [disasm]. Synced with the game:
  Worm.DataNN (Energy, Active; Energy written back), Inventory.TeamNN / AllianceNN (ammo, below), InventoryN.WeaponDelays (delays),
  GM.SchemeData (crate chance and shares), AIParams.WormNN copies (the CPU level).
- **Worms** (`WormManager.Reinitialise`): the Worm.DataNN slots the script set up, team-major by TeamIndex then slot, at their Spawn
  marker (dropped onto the ground below, facing the map centre [ours, as the JSON path]); Active 0 slots are unspawned placeholders;
  `IsAllowedToTakeTurn` 0 never plays (`Worm::turns`). Team names and CPU levels from Team.DataNN `IsAIControlled` and the AIParams.CPUn
  copied to its worms; none copied: CPU2, the AIService init's copy into every AIParams.WormNN [disasm 0x4b3390]. The AI reads the
  thinking worm's own level (`Game::wormCpu`, kept by `syncCpu` at Reinitialise, Respawn and every AIParams.WormNN copy); the team's
  `cpu` (its first copied worm's level) only marks it AI-controlled and shows in the menu [ours]. The teams are those of the worms and of every Team.DataNN written;
  `Game::allied` holds their AlliedGroup (crate team rules, the AI's allies: docs/w4m/missions.md §23.10).
  - Worm.Respawn (and EFMV SpawnWorm: its DataId copied to the slot first) puts a fresh worm at the slot's Spawn marker as above, if
    Active and no worm is in the slot [disasm 0x5b4f70]. Our worms are team-major, so Reinitialise keeps 16 - (slots set up) spare
    columns per team for slots set up later; a new slot takes its team's first free column, else a gone worm's [ours: W4M's 16 slots
    hold any team]. Worm.DieQuietly / WXWormManager.UnspawnWorm / EFMV UnspawnWorm remove the slot's worm (WormData.Active 0) with no
    blast, grave or Worm_Died: it waits, dead, under the map [disasm 0x5b4af0].
  - Before the first ActivateNextWorm (`Game::noTurn`) CurrentTeamIndex and ActiveWormIndex are -1 [disasm 0x5b5c4c] for the trigger and
    crate team rules. An ActivateNextWorm that finds no worm leaves the turn without one: Timer_TurnTimedOut comes after HotSeatTime +
    TurnTime [ours: W4M asserts there, 0x5b5b0f; its clocks still run in the stdlib flow]. TraitorousWaters gets there when a house
    trigger dies before any house was blown (its SilentKillWormInTheAir then unspawns slot 0, the player).
- **Ammo**: the default inventories and delays are emptied before Initialise and copied into the 16 worm, 4 team, 4 alliance inventories
  (0x4f5bd0: LOCAL's SkipGo / Surrender -1 do not survive) [disasm]. A worm's ammo is Inventory.WormNN + TeamNN + AllianceNN, -1 if any
  is -1; ours is per team, the team's active worm's (`ammoSlot`, switched by ActivateNextWorm). Each tick `bookAmmo` books the sim's
  changes as W4M makes them: a use takes from the alliance, then the team, then the worm (0x4f4fd0), a crate adds to the alliance
  (0x5c8820), or to the collector's Inventory.WormNN for a crate made with Crate.AddToWormInventory (TraitorousWaters' Crate3-9; the key
  is reset per crate, 0x4f2245) [disasm]; then every team is recomputed (a shared alliance, another worm's inventory).
- **Turns**: the script drives them. `GameLogic.ActivateNextWorm` is our `beginTurn` (delays of the team that played -1 first; turn and
  hot-seat clocks from TurnTime / HotSeatTime, TurnTime 0 = no turn clock), the script's own `SetWind` sets the wind (`beginTurn` draws
  none), `GameLogic.EndTurn` ends control. Our clocks call `Timer_TurnTimedOut`, `Timer_RetreatTimedOut`, `Timer_PostActivityTimedOut`;
  Settle calls `GameLogic_NoActivity` while stdlib's `WaitUntilNoActivity` is set; `Worm.ApplyPoison` / `GameLogic.ApplyDamage` run ours.
  So DoPostActivity, DoOncePerTurnFunctions, TurnEnded and StartTurn are the scripts' own.
- **Callbacks** (end of the tick, `scriptStep`): per Hurt event, Worm_Damaged_Current when the active worm took it (any phase, every hit,
  poison and abduction rolls aside: damage types 5 / 6, 0x5ab7e0; drowning and vapourising once) then Worm_Damaged with DamagedWorm.Id;
  Payload_Deleted with Payload.Deleted.Id when a payload goes (every shot but the bomber planes and the UFO, every mine: 0x580560;
  a placed mine gives its Mine.Id, the others a fresh number); Worm_Died with DeadWorm.Id at the death blast (or a vapourised worm); Crate_Collected / Sunk / Destroyed
  with Crate.Index when a crate with an Index leaves; Timer_GameTimedOut when the round clock (from Timer.StartGame) runs out; due timers
  in deadline order; queued callbacks (EFMV_Terminated).
- **Timers**: 10 slots, deadline in game ms = ticks x 1000 / 60 [disasm: TaskManager logical time].
- **Movies** (`EFMV.Play`, `ScriptState::Movie`, docs/w4m/acting.md §19): the player runs in the sim and is checksummed (name, clock,
  cursors, camera lists). Every 10 game ms: the camera update, then the player's step (each track fires its events in file order while
  Time <= the clock, then the clock + 10) [disasm 0x526cb0; ours: the order of the two within a 10 ms slot]; the movie ends once every
  track is done and no camera path runs (+0x32), queueing EFMV_Terminated. A second EFMV.Play while one plays is ignored (0x4ff21d).
  - Sim events: CreateExplosion (a real blast at its locator, units / 20, impulse x 50, the impulse centre ImpulseOffset units above it,
    its ParticleEffect (none: WXP_ExplosionX_Med), as Explosion.Construct, 0x4f9970), SpawnWorm / UnspawnWorm, RaiseWater, SelectWorm
    (ActiveWormIndex), SelectWeapon (WormData WeaponIndex; the current worm takes it if no tool is out; Lua Weapon_Selected),
    DeleteLandframe (`Terrain::clearCoded`: the code's HEX ops leave the exact land, `SharpLand::drop`, its voxels empty, chunks dirty;
    worms and objects lose their support as after a blast; no debris), CreateBorders / DeleteBorders = EFMV.Start / EFMV.End.
  - Cameras: CutCamera, PathCamera, TimedPathCamera keep two knot lists (knot count from the " ,"-split names, current knot, float t)
    stepped as PathCam 0x52bb40 / TimedPathCam 0x636de0 (t = float(t + 1 / steps), past 1 the next knot; the activation steps once);
    a path holds the movie until both lists end. From the first camera event to the end the turn clock (Aim) and the round clock stand
    (type 14). A TimedPath step list shorter than its segments reuses its last value [ours: W4M reads past the vector].
  - Skip: `Input::SKIP_MOVIE` (lockstep input; ignored while EFMV.Unskipable) steps to the end firing only the Critical events, then
    the end as usual (0x526d90). Client: Space, or any pad's Y or - (W4M Back) (`Controls::quitMovie`, input group EFMVMovie).
  - While a movie plays the sim zeroes every input (the EFMVMovie group alone, 0x5074a0) [ours for a CPU's: W4M's AI acts through
    messages, not traced], ObjectCount.Active stays, acting scenes are not started (Acting::fire; W4M ends them within 20 ms).
  - Game-over movie: at Mission / Challenge .Success / .Failure, Win, Draw with EFMV.GameOverMovie set (not .Off) that movie plays
    (MovieName = it, 0x4f52c0), the phase already GameOver: the world, the movie and the script run on (Game::step), no turn; the
    result shows once it ends (`scriptOutro` 1); no GameOverLogicEntity (no victory music, crowd, fireworks or orbit). A challenge's
    end writes Challenge.Success 1 / 0 first (FailureComment reads it). `run.ticks` stops at the end.
  - Render (client, never read back): `scriptMovie()` gives the movie clock, its tracks, per track the cast (WORM<slot> -> our worm,
    else the name), the camera event and its lists, the borders state. `Efmv::camera` evaluates the knots (markers by exact name; a
    TimedPath look-at knot 1000 units along the marker's `dir`) with the cardinal spline (s = (1 - Tension) / 2, TimedPath 0.5), up
    (0, 1, 0), the default lens; it outranks the game camera, the bomber / UFO scene cams outrank it; the shake applies. `Efmv::borders`:
    two black bars, inner edge 190 HUD units off the middle of a 540-high screen, sliding to 240 over EFMV.BorderOffTime after EFMV.End
    (EfmvBorderEntity; the bar quad read as +-1 x scale [assumed: its sprite is in no bundle]). While the borders are up the HUD and the
    ready screen are hidden (BordersActive) [ours: at once, no Out_* slide]; worm labels hide while EFMV.Active.
  - Worm events (WormEmote, PlayAnimation, StopAnimation, WormLookAt / GestureAt, ThreatenWorm, SpawnParticle, TriggerSpeech) are
    `GameEvent::Movie` events played by `Acting::movie` on the cast actors with the acting-scene code (a "PROP <name>" detail is a look
    target, 0x5cd12e); a plain TriggerSpeech name plays the worm's voice category there. AnimateDetail plays the coded details' clip once
    from that frame (`Efmv::event`), held on its last pose (0x7ad288). No EFMV event moves a worm: only clips (none in the data).
  - Subtitles (ui.cpp): in subtitle mode (borders up until EFMV.End) Comment lines wrap at 600 units, scale 20 (the box's 300 / 18 ratio
    on our banner font), each shown alone for its delay, white, centred at (0, -210); the mode's start drops the box and its queue, its
    end and the movie's end drop the subtitles; default comments (deaths, crates) are dropped meanwhile. FailureComment: Miss.Generic.Lose
    1..5 by GetRandomValue (W4M's graphical rng), Delay = Duration.
  - Audio hooks (the other side is audio.cpp): `GameEvent::Speech` {fx = Speech name (`*Name`: the level's EFMV bank line, else a voice
    category), worm = the speaker (-1: none), pos = its position, weapon = Duration ms}; `GameEvent::MovieSound` {fx = EffectName, pos = its
    Location locator, weapon = Duration ms, worm = Looping 0/1}; `GameEvent::MovieStart` / `MovieEnd` {fx = movie name} (music fade
    500 / 2000 ms, acting.md §19); `Ui::onNarrator(n)` when a subtitle Miss.Generic.Lose<n> line shows (Failures_Narrator_0<n>);
    `scriptMovieEvent(g, fx, code)` gives any GameEvent::Movie's event.
  - Captures: `--ui missionhud <id>` plays the intro; `--ui movie <id> <Lua function>` skips it then calls that script function
    (`PlayMidtroMovie`, `PlayOutroMovie`), with `W4NX_CAPFRAMES` / `W4NX_SHOTEND`.
  - Worms in close-ups look bigger than in W4M: the map's import scale (0.59 in TinCanWally) shrinks the land and locators, not the
    worms [ours: the voxel grid fit, docs/maps.md].
- **UI and render** (docs/w4m/missions.md §23.8): never read back by the sim or the checksum. The script queues GameEvents (`Comment`,
  `CommentClear`, `Emitter`, `EmitterOff`, `Shake`, worm -1) in `ScriptState::ui`; the end of `scriptStep` appends them to `g.events`, so
  Initialise's (run before the first step clears the events) reach the listeners too. Persistent HUD state is read by `scriptHud()`.
  - Commentary: TimedText (`CommentaryPanel.Comment`, `.Delay`) / ScriptText (delay 0 = 1200 ms) and the movies' Critical Comment
    (Delay = Duration) go to the commentary banner (`Ui::hudEvent`) as W4M's queue: the text (W4M `tr()`, FR / EN from `EngLS` / `FreLS`;
    unknown id: `** INVALID STRING ID: id`, ScriptText: the id itself) wrapped to the banner width, two lines per banner shown 2 x the
    delay, a last single line 1 x. Commentary.Clear drops the waiting banners, not the shown one. Commentary.NoDefault mutes our default
    banners (deaths, team wipe, crate drop, crate pickup); the mystery-crate text stays.
  - HUD: `HUD.Counter.Active` shows `HUD.Counter.Value` ("%d", "%d%" with Percent) in orange on a wind backing at the clock's lower left
    [ours: the backing at the wind's px per unit x BackScale 30, text at 1.84 px per TextScale unit as the wind digits, placed against our
    clock and lifted to stay on screen; no In_Timer / Out_Timer slide]. The round clock shows RoundTimeRemaining when RoundTime > 0, else
    the elapsed ms since Timer.StartGame, "~" (W4M FXTXT.Infinity) for RoundTime -1; `HUD.Clock.DisplayTenths` adds the hundredths as a
    smaller "%02d" [ours: 0.6 of the main digits]; TurnTime 0 (endless turn) shows no turn digits.
  - Particles: Particle.NewEmitter starts `Particle.Name` at the `Particle.DetailObject` marker (`Fx::scripted`, m per unit as the map's
    emitters) and returns the handle in Particle.Handle (1, 2, ... per script [ours: W4M's handle counter is the renderer's]); a missing
    detail logs and leaves the handle (TurkishDelights' Flame1-4 exist in no W4M file). DelGraphicalEmitter stops it (particles live on,
    immortal ones go), ...Imm takes all its particles. Movie CreateEmitter / DeleteEmitter use their UserId (handles -1 - UserId).
  - Camera.ShakeStart: `Fx::shakeFor` holds the shake at Camera.Shake.Magnitude m per axis, fading linearly over Length ms, clamped to
    0.5 m (Camera.Shake.Max x 1000 units).
  - Team bars (ui.cpp): a team's shown hp / the largest team total at the match start (W4M EnergyBarManagerEntity, §23.8), so reserved
    respawn columns and uneven mission teams keep W4M's lengths.
  - No objective box for a W4M mission (`missionHud` is the JSON missions' only): W4M shows none in game.
- **Crates** (`GameLogic.CreateCrate`, docs/w4m/missions.md §23.10): at the Crate.Spawn marker itself, tag = Crate.Index; Type target ->
  `Object::Target`, health, custom (no contents, `weapon` -2), else Contents (kWeapon* / kUtility*). The keys go to the `Object`: count =
  NumContents, hp = Hitpoints x HitpointsMultiplier, teamCollect / teamDestroy = TeamCollectable / TeamDestructible (AlliedGroups),
  pinned = Gravity 0, falling (chute) = Parachute, spawning (the "Crate Spawn" hold) = WaitTillLanded, pushable, track = TrackCam,
  scale; GroundSnap puts it on the land below (0.45 m x Scale up, our crate half height), or at the water line; FallSpeed is its first
  velocity. The sim then treats it as any crate (docs/sim.md "Crates"). CustomGraphic names the detail mesh drawn for a custom crate
  (`scriptCrateGraphic`, models `d01_04` ...). Not used by any W4M crate, so not modelled [data]: LifetimeSec, LifetimeTurns, UXB,
  DelayMillisec, RandomSpawnPos; AddToWormInventory books to the team as every crate [ours: per-team ammo].
  Crate.Delete removes the crates of that Index and calls Crate_Destroyed at once (W4M dispatch is synchronous); Crate.RadarHide /
  RadarDisplay hide / show them on the radar.
- **Triggers** (docs/w4m/missions.md §23.10): GameLogic.CreateTrigger adds a `Game::Trigger` at the Trigger.Spawn marker itself with
  the Trigger.* keys (WormCollect turned into our worm index at creation: -2 when the slot has none [ours]). `Game::stepTriggers` (after
  the objects) collects: a live worm whose sphere (10 units, 5 above its feet) meets it, TeamCollect / WormCollect matching; a
  sheep-like shot (Sheep, Super Sheep, Starburst, Old Woman, Scouser; WEAPTWK ColliderFlags 128) or another shell, homing missile,
  donkey or bomblet (flags 8) of the active team, with SheepCollect / PayloadCollect, by its Radius. `Game::explode` destroys it.
  Callbacks: Trigger_Collected / Trigger_SheepCollected (a payload) / Trigger_PayloadCollected (a sheep) / Trigger_Destroyed with
  Trigger.Index (and Trigger.Collector, a slot), at the end of the tick; a script's own Explosion.Construct calls Trigger_Destroyed at
  once, a skipped movie's blasts just before its EFMV_Terminated (the order W4M posts them). GameLogic.DestroyTrigger removes those of
  that Index without callback. Trigger.Visibility 1: drawn as a translucent red sphere of its radius (main.cpp, the W4M Trigger.Ball's
  texture colour and blend) [ours: raylib's sphere for the 2-unit ball mesh].
- **End**: GameLogic.Mission / Challenge .Success / .Failure, GameLogic.Win (team 0 wins) / Draw -> `missionEnd`. With
  EFMV.GameOverMovie set the result shows after that movie (Movies above), with .Off at once; else GameOverLogicEntity's pace
  (main.cpp `overDone`, the game-over camera of controls.cpp as in a match): 4.02 s on a worm, the orbit 5.02 s (any key or button ends
  it), a 1.02 s fade, then the result [disasm 0x4ff8d0].
- **Round clock**: ElapsedRoundTime counted by 10 game ms (zeroed by Timer.StartGame), held while GameLogic.RoundTime.Pause is on (to
  .Resume) or a movie camera is current (type 14) [disasm 0x50f100, 0x50f173, 0x50f15f].
- **Mines**: PlaceMine (string: a marker) is a CreateMine there (`Game::newMine`: the dud roll unless Mine Respawn, then the fuse
  MinFuse + trunc((MaxFuse - MinFuse) r) ms; Mine.Id = its `Object::id`); GameLogic.PlaceObjects places a CreateMine on the details named
  exactly "mine" (not "Mine1"), an oil drum on "oildrum", the factory on "minefactory" [disasm 0x4fb490]. Mine.* keys go to `Game::mineMin`
  / `mineMax` / `mineDud` / `mineDet`, which a match takes from its scheme (lib_SetupMinesAndOildrums).
- **Mine factory** (DeathMatch6, `Game::factory*`, docs/sim.md): GameLogic.StartMineFactory, kMineFactoryData edits.
- **Water.Level** (`Game::water` = origin y + Level x scale / 20 [ours: the map's import scale]), set at once (the drown test reads it);
  the movies' Critical RaiseWater adds its Delta. Read back when the sim moved it (Flood, sudden death).
- **Weapons**: Weapon.Create keeps WormData.WeaponIndex if usable, else writes the first usable id from 0 (Skip Go, Surrender skipped;
  none: kWeaponUndefined) back, then PreSelected; Weapon.PreSelected wields WeaponIndex without an ammo test, kWeaponUndefined (or a
  weapon we lack) empties the hand (`weapon` -1) [disasm 0x565770, 0x566c77]. Every turn starts empty-handed (docs/sim.md). Weapon.Wield is the draw pose and EquipSfx only.
  Challenge.EndlessGun: the gun keeps its last shot (`Game::endlessGun`: one ammo, no turn end). Jetpack.InitFuel: a new jetpack's
  fuel (`Game::jetInit`, set as the jetpack is selected).
- **Turn keys**: SameTeamNextTurn gives ActivateNextWorm's turn to the same team's next worm, without the delay turn, and is reset to 0
  [disasm 0x5b59b0]; Turn.Boring 0 / Turn.MaxDamage 1 make the next end-of-shot reaction MaxDamage (acting.cpp), GameLogic.ApplyDamage
  resets them [disasm 0x5b22a0].
- **Checksum** (`Game::checksum`): the globals (functions left out, tables sorted by entry bytes), the keys and containers written since
  load, timers, queue and turn flags, cached until the next Lua call or write. That serialisation is the script's saved state; there is no
  restore, replay snapshots being off for missions.
- `Game` copies share the script (`shared_ptr`): only the replay snapshots copy a Game, and they never run with a mission.

- **Easter eggs** (`WXMsg.EasterEggFound`, docs/w4m/missions.md §23.8): an item not yet unlocked gets State Unlocked at once (the
  script sees it) and is kept in progress.txt (`unlock <item>`, `scriptUnlocks` applied over DEFSAVE at every scriptStart); after the
  game, before the result screen, the WXFE.EasterEggFound popup shows FETXT.EasterEgg.Name.Template with the item's DescriptionName
  and FETXT.EasterEgg.Coins.Template with its Value (1000) (`Ui::eggFound`) [disasm 0x72c567, 0x4fd829; data MENUTWKX, DEFSAVE].
  Not modelled: the shop balance (ours has no shop) and the achievement tracking.

Point lights: `Land.EnablePointLight` / `DisablePointLight` call `Terrain::pointLight` (every light whose code matches the string's
first 4 characters; a change marks the chunks its sphere reaches dirty for the async remesh); render only, never read by the sim or
the checksum (docs/maps.md "Point lights").

`make mission_check` plays every W4M mission with the AI on all teams (after 3 game minutes it collects the crates it may, pops targets,
walks into a trigger the script answers with Trigger_Collected or blasts one it may destroy when it answers Trigger_Destroyed, ends the
player's turn with the enemies at 0 hp and the never-playing worms poisoned) until the script ends it, and asserts no Lua error, no
missing data key and that all 51 end (within 90 game minutes: ChuteToVictory's round clock stands through ~8 s of movie camera a
turn). The assist never acts during a movie (the player has no control then). Result after lot 3 (movies played in full, 2026-10-06):
51 of 51 end. A movie now lasts its real length, so a trigger blown while one plays (a shell still flying) starts no second movie
(EFMV.Play ignored while EFMV.Active, as W4M): DinerMight's 4th trigger then never ends it, as it would in W4M. Still logged as not modelled:

| mission | end | not modelled (count) |
|---|---|---|
| DinerMight | lost 3102 s | - |
| SneakyBridgeThieves | won 206 s | - |
| BuildingSiteSaboteurs | lost 212 s | - |
| TheCrateEscape | won 198 s | - |
| DestructAndServe | won 270 s | SetData WXD.StoryMovie x1 |
| StormTheCastle | won 250 s | - |
| TheWindyWizard | lost 3061 s | - |
| RobInTheHood | lost 115 s | - |
| JoustAboutIt | lost 1847 s | - |
| NiceToSiegeYou | lost 131 s | - |
| MineAllMine | won 254 s | - |
| GhostHillGraveyard | won 275 s | - |
| TinCanWally | lost 1945 s | - |
| DoomCanyon | lost 70 s | - |
| HighNoonHiJinx | won 225 s | SetData WXD.StoryMovie x1 |
| TurkishDelights | lost 503 s | - |
| NoRoomForError | won 225 s | - |
| CarpetCapers | lost 230 s | - |
| TraitorousWaters | lost 325 s | - |
| GibbonTake | won 250 s | SetData WXD.StoryMovie x1 |
| FastFoodDino | won 189 s | - |
| EscapeFromTreeRex | won 264 s | - |
| ChuteToVictory | lost 4938 s | - |
| TheLandThatWormsForgot | lost 222 s | - |
| ValleyOfDinoWorms | won 250 s | - |
| ChallengeSniper | won 195 s | - |
| ChallengeJetpack | won 195 s | - |
| ChallengeSheep | won 195 s | - |
| ChallengeIcarus | won 195 s | - |
| ChallengeShotgun | won 195 s | - |
| ChallengeAccuracy | won 195 s | - |
| ChallengeNavigation | won 195 s | - |
| ChallengeCrate | won 195 s | - |
| ChallengeSniper2 | won 180 s | - |
| ChallengeJetpack2 | won 180 s | - |
| ChallengeSheep2 | won 180 s | - |
| ChallengeShotgun2 | won 180 s | - |
| ChallengeAccuracy2 | won 209 s | - |
| ChallengeNavigation2 | won 180 s | - |
| ChallengeCrate2 | won 180 s | - |
| DeathMatch1 | won 204 s | - |
| DeathMatch2 | won 233 s | - |
| DeathMatch3 | won 207 s | - |
| DeathMatch4 | won 202 s | - |
| DeathMatch5 | won 240 s | - |
| DeathMatch6 | won 238 s | - |
| DeathMatch7 | won 236 s | - |
| DeathMatch8 | won 276 s | - |
| DeathMatch9 | won 259 s | - |
| DeathMatch10 | won 213 s | - |
| DeathMatch11 | won 225 s | - |

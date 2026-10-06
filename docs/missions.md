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
- `<bank>.json`: the level databank's keys, containers and, per movie, its Critical events in the player's firing order (`movies`).
Map markers: every hidden named detail is exported (`locator` when its library is no worm, crate, target, mine, drum, trigger or
telepad type), so the scripts' spawn, crate and explosion names resolve.

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
  copied to its worms; none copied: CPU2, the AIService init's copy into every AIParams.WormNN [disasm 0x4b3390]. Ours is per team: the
  first copied worm's level [ours: the AI level is a team setting]. The teams are those of the worms and of every Team.DataNN written;
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
- **Movies** (`EFMV.Play`): played as skipped: only the Critical events run (CreateExplosion: a real blast at its locator, units / 20, impulse
  x 50, the impulse centre ImpulseOffset units above it, its ParticleEffect (none: WXP_ExplosionX_Med) on the Boom event, as
  Explosion.Construct [disasm 0x4f9970]; Comment, CreateEmitter / DeleteEmitter: UI below; DeleteBorders: nothing to undo), then
  EFMV_Terminated is queued [data: acting.md §19].
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
  EFMV.GameOverMovie (or .Off) set the result shows at once (the movie, skipped, ends at once: lot 3); else GameOverLogicEntity's pace
  (main.cpp `overDone`, the game-over camera of controls.cpp as in a match): 4.02 s on a worm, the orbit 5.02 s (any key or button ends
  it), a 1.02 s fade, then the result [disasm 0x4ff8d0].
- **Round clock**: elapsed game ms since Timer.StartGame, held while GameLogic.RoundTime.Pause is on (to .Resume) [disasm 0x50f173]; W4M
  also holds it while the logical camera is a Path / TimedPath one (type 14, movie cameras), which skipped movies never start.
- **Mines**: PlaceMine (string: a marker) is a CreateMine there (`Game::newMine`: the dud roll unless Mine Respawn, then the fuse
  MinFuse + trunc((MaxFuse - MinFuse) r) ms; Mine.Id = its `Object::id`); GameLogic.PlaceObjects places a CreateMine on the details named
  exactly "mine" (not "Mine1"), an oil drum on "oildrum", the factory on "minefactory" [disasm 0x4fb490]. Mine.* keys go to `Game::mineMin`
  / `mineMax` / `mineDud` / `mineDet`, which a match takes from its scheme (lib_SetupMinesAndOildrums).
- **Mine factory** (DeathMatch6, `Game::factory*`, docs/sim.md): GameLogic.StartMineFactory, kMineFactoryData edits.
- **Water.Level** (`Game::water` = origin y + Level x scale / 20 [ours: the map's import scale]), set at once (the drown test reads it);
  the movies' Critical RaiseWater adds its Delta. Read back when the sim moved it (Flood, sudden death).
- **Weapons**: Weapon.Create keeps WormData.WeaponIndex if usable, else writes the first usable id from 0 (Skip Go, Surrender skipped;
  none: kWeaponUndefined) back, then PreSelected; Weapon.PreSelected wields WeaponIndex without an ammo test [disasm 0x565770,
  0x566c77; ours: kWeaponUndefined keeps the hand, our sim has no empty hand]. Weapon.Wield is the draw pose and EquipSfx only.
  Challenge.EndlessGun: the gun keeps its last shot (`Game::endlessGun`: one ammo, no turn end). Jetpack.InitFuel: a new jetpack's
  fuel (`Game::jetInit`, set as the jetpack is selected).
- **Turn keys**: SameTeamNextTurn gives ActivateNextWorm's turn to the same team's next worm, without the delay turn, and is reset to 0
  [disasm 0x5b59b0]; Turn.Boring 0 / Turn.MaxDamage 1 make the next end-of-shot reaction MaxDamage (acting.cpp), GameLogic.ApplyDamage
  resets them [disasm 0x5b22a0].
- **Checksum** (`Game::checksum`): the globals (functions left out, tables sorted by entry bytes), the keys and containers written since
  load, timers, queue and turn flags, cached until the next Lua call or write. That serialisation is the script's saved state; there is no
  restore, replay snapshots being off for missions.
- `Game` copies share the script (`shared_ptr`): only the replay snapshots copy a Game, and they never run with a mission.

Not modelled yet, each logged once and counted per mission: DeleteLandframe, point lights (Land.EnablePointLight: our
maps import no chunk point light, docs/maps.md), the easter-egg message.

`make mission_check` plays every W4M mission with the AI on all teams (after 3 game minutes it collects the crates it may, pops targets,
walks into a trigger the script answers with Trigger_Collected or blasts one it may destroy when it answers Trigger_Destroyed, ends the
player's turn with the enemies at 0 hp and the never-playing worms poisoned) until the script ends it, and asserts no Lua error, no
missing data key and that all 51 end. Result after lot 2 (worms, triggers, crates; with the mines / water and UI passes, 2026-10-06):
51 of 51 end. What is still logged as not modelled (UI and render keys included):

| mission | end | not modelled (count) |
|---|---|---|
| DinerMight | won 116 s | SetData EFMV.GameOverMovie 1 |
| SneakyBridgeThieves | won 232 s | SetData EFMV.GameOverMovie 1 |
| BuildingSiteSaboteurs | lost 197 s | - |
| TheCrateEscape | won 199 s | SetData EFMV.GameOverMovie 1 |
| DestructAndServe | won 215 s | EFMV DeleteLandframe 1, SetData EFMV.GameOverMovie 1, SetData WXD.StoryMovie 1, WXMsg.EasterEggFound 1 |
| StormTheCastle | won 246 s | SetData EFMV.GameOverMovie 1 |
| TheWindyWizard | lost 196 s | EFMV DeleteLandframe 1 |
| RobInTheHood | won 229 s | SetData EFMV.GameOverMovie 1 |
| JoustAboutIt | lost 1816 s | - |
| NiceToSiegeYou | lost 99 s | - |
| MineAllMine | won 217 s | SetData EFMV.GameOverMovie 1 |
| GhostHillGraveyard | won 259 s | SetData EFMV.GameOverMovie 1 |
| TinCanWally | lost 278 s | WXMsg.EasterEggFound 1 |
| DoomCanyon | lost 33 s | - |
| HighNoonHiJinx | won 214 s | SetData EFMV.GameOverMovie 1, SetData WXD.StoryMovie 1 |
| TurkishDelights | lost 380 s | Land.EnablePointLight 2 |
| NoRoomForError | won 203 s | SetData EFMV.GameOverMovie 1 |
| CarpetCapers | lost 207 s | WXMsg.EasterEggFound 1 |
| TraitorousWaters | lost 243 s | SetData EFMV.GameOverMovie.Off 1 |
| GibbonTake | won 207 s | SetData EFMV.GameOverMovie 1, SetData WXD.StoryMovie 1 |
| FastFoodDino | won 180 s | SetData EFMV.GameOverMovie 1 |
| EscapeFromTreeRex | won 212 s | SetData EFMV.GameOverMovie 1, WXMsg.EasterEggFound 1 |
| ChuteToVictory | lost 3006 s | - |
| TheLandThatWormsForgot | lost 66 s | EFMV StopAnimation 1 |
| ValleyOfDinoWorms | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeSniper | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeJetpack | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeSheep | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeIcarus | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeShotgun | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeAccuracy | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeNavigation | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeCrate | won 180 s | SetData EFMV.GameOverMovie 1 |
| ChallengeSniper2 | won 180 s | - |
| ChallengeJetpack2 | won 180 s | - |
| ChallengeSheep2 | won 180 s | - |
| ChallengeShotgun2 | won 180 s | - |
| ChallengeAccuracy2 | won 203 s | - |
| ChallengeNavigation2 | won 180 s | - |
| ChallengeCrate2 | won 180 s | - |
| DeathMatch1 | won 211 s | - |
| DeathMatch2 | won 216 s | - |
| DeathMatch3 | won 204 s | - |
| DeathMatch4 | won 206 s | - |
| DeathMatch5 | won 222 s | - |
| DeathMatch6 | won 205 s | - |
| DeathMatch7 | won 208 s | - |
| DeathMatch8 | won 207 s | - |
| DeathMatch9 | won 232 s | - |
| DeathMatch10 | won 236 s | - |
| DeathMatch11 | won 209 s | - |

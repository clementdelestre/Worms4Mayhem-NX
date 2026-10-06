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
| `objects[]` | `crate` (`weapon` name or `health`), `target`, `mine`, `barrel`. Crates and targets are pinned (no gravity); crates can't be blown up; a Super Sheep collects them. |
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
  Jetpack.Fuel). TurnTime, HotSeatTime, PostActivityTime, RoundTime are read where the turn machine needs them.
- **Containers**: Query returns a field table, Edit `(lock, table)` and Close writes the table back [disasm]. Synced with the game:
  Worm.DataNN (Energy, Active; Energy written back), Inventory.TeamNN / AllianceNN (ammo, below), InventoryN.WeaponDelays (delays),
  GM.SchemeData (crate chance and shares), AIParams.WormNN copies (the CPU level).
- **Worms** (`WormManager.Reinitialise`): the Worm.DataNN slots the script set up, team-major by TeamIndex then slot, at their Spawn
  marker (dropped onto the ground below, facing the map centre [ours, as the JSON path]); Active 0 slots are unspawned placeholders;
  `IsAllowedToTakeTurn` 0 never plays (`Worm::turns`). Team names and CPU levels from Team.DataNN `IsAIControlled` and the AIParams.CPUn
  copied to its worms [ours: CPU3 when none was copied].
- **Ammo** [ours, per team]: Inventory.Worm.Default + Team + Alliance as W4M's sum (§23.6); what the game spends or collects is booked to the
  alliance container, which is recomputed before the script reads it.
- **Turns**: the script drives them. `GameLogic.ActivateNextWorm` is our `beginTurn` (delays of the team that played -1 first; turn and
  hot-seat clocks from TurnTime / HotSeatTime, TurnTime 0 = no turn clock), the script's own `SetWind` sets the wind (`beginTurn` draws
  none), `GameLogic.EndTurn` ends control. Our clocks call `Timer_TurnTimedOut`, `Timer_RetreatTimedOut`, `Timer_PostActivityTimedOut`;
  Settle calls `GameLogic_NoActivity` while stdlib's `WaitUntilNoActivity` is set; `Worm.ApplyPoison` / `GameLogic.ApplyDamage` run ours.
  So DoPostActivity, DoOncePerTurnFunctions, TurnEnded and StartTurn are the scripts' own.
- **Callbacks** (end of the tick, `scriptStep`): Worm_Damaged_Current (active worm hurt during its turn, once) then Worm_Damaged with
  DamagedWorm.Id per Hurt event; Worm_Died with DeadWorm.Id at the death blast (or a vapourised worm); Crate_Collected / Sunk / Destroyed
  with Crate.Index when a crate with an Index leaves; Timer_GameTimedOut when the round clock (from Timer.StartGame) runs out; due timers
  in deadline order; queued callbacks (EFMV_Terminated).
- **Timers**: 10 slots, deadline in game ms = ticks x 1000 / 60 [disasm: TaskManager logical time].
- **Movies** (`EFMV.Play`): played as skipped: only the Critical events run (CreateExplosion: a real blast at its locator, units / 20, impulse
  x 50; DeleteBorders / DeleteEmitter: nothing to undo), then EFMV_Terminated is queued [data: acting.md §19].
- **Crates** (`GameLogic.CreateCrate`): at the Crate.Spawn marker, pinned like the JSON path's, tag = Crate.Index; Type target -> target,
  health, custom (no contents, `weapon` -2), else Contents (kWeapon* / kUtility*). Parachute, lifetime, UXB, hit points: lot 2.
- **End**: GameLogic.Mission / Challenge .Success / .Failure, GameLogic.Win (team 0 wins) / Draw -> `missionEnd`, immediately [ours: no
  GameToFrontEndDelayTime, no game-over movie].
- **Checksum** (`Game::checksum`): the globals (functions left out, tables sorted by entry bytes), the keys and containers written since
  load, timers, queue and turn flags, cached until the next Lua call or write. That serialisation is the script's saved state; there is no
  restore, replay snapshots being off for missions.
- `Game` copies share the script (`shared_ptr`): only the replay snapshots copy a Game, and they never run with a mission.

Not modelled yet (lot 2), each logged once and counted per mission: Worm.Respawn / DieQuietly / UnspawnWorm and EFMV SpawnWorm /
UnspawnWorm, triggers (CreateTrigger, DestroyTrigger, Trigger_* callbacks), PlaceMine / StartMineFactory and Mine.* keys, Water.Level
and EFMV RaiseWater, DeleteLandframe, HUD counter and clock, commentary and briefing text, particles, camera shake, point lights,
Weapon.Wield / PreSelected, Jetpack.InitFuel, Challenge.EndlessGun, SameTeamNextTurn, Turn.Boring / MaxDamage, the easter-egg message.

`make mission_check` plays every W4M mission with the AI on all teams (after 3 game minutes it collects crates, pops targets, ends the
player's turn with the enemies at 0 hp and the never-playing worms poisoned) until the script ends it, and asserts no Lua error and no
missing data key. Result of the lot (2026-10-06): 50 of 51 end; TraitorousWaters needs triggers (it has no round clock). Not modelled
calls per mission, UI (commentary, HUD, briefing, game-over movie keys) and render (particles, camera, lights, movie comments) counted apart:

| mission | end | gameplay not modelled (count) | UI | render |
|---|---|---|---|---|
| DinerMight | lost 3004 s | CreateTrigger 15 | 8 | 2 |
| SneakyBridgeThieves | won 180 s | Worm.Respawn 3 | 20 | 16 |
| BuildingSiteSaboteurs | lost 1807 s | CreateTrigger 3, SetData Mine.DudProbability 1, SetData Mine.MaxFuse 1, SetData Mine.MinFuse 1 | 5 | 0 |
| TheCrateEscape | won 196 s | CreateTrigger 2 | 7 | 2 |
| DestructAndServe | lost 3000 s | CreateTrigger 9, SetData Trigger.Visibility 1 | 7 | 0 |
| StormTheCastle | lost 3005 s | CreateTrigger 237, DestroyTrigger 233, SetData Trigger.Visibility 1, Worm.Respawn 4 | 7 | 3 |
| TheWindyWizard | lost 3009 s | EFMV UnspawnWorm 1, CreateTrigger 4, SetData Trigger.Visibility 1 | 5 | 2 |
| RobInTheHood | lost 1803 s | CreateTrigger 3, Worm.Respawn 1 | 10 | 0 |
| JoustAboutIt | lost 1801 s | Worm.DieQuietly 1, Worm.Respawn 3 | 7 | 2 |
| NiceToSiegeYou | lost 65 s | CreateTrigger 1, SetData Trigger.Visibility 1, Worm.DieQuietly 3, Worm.Respawn 1 | 6 | 0 |
| MineAllMine | won 210 s | PlaceMine 4, SetData Mine.DetonationType 1, SetData Mine.DudProbability 1, SetData Mine.MaxFuse 1, SetData Mine.MinFuse 1 | 6 | 0 |
| GhostHillGraveyard | lost 3009 s | SetData SameTeamNextTurn 1, Worm.Respawn 10 | 6 | 23 |
| TinCanWally | lost 1806 s | CreateTrigger 2, SetData Turn.Boring 10, SetData Turn.MaxDamage 10, Worm.DieQuietly 3, Worm.Respawn 4 | 7 | 0 |
| DoomCanyon | lost 74 s | SetData Water.Level 3 | 6 | 0 |
| HighNoonHiJinx | lost 1803 s | CreateTrigger 14, SetData Trigger.Visibility 1 | 4 | 0 |
| TurkishDelights | lost 1800 s | CreateTrigger 2, Worm.DieQuietly 1, Worm.Respawn 3 | 7 | 6 |
| NoRoomForError | won 213 s | - | 2 | 0 |
| CarpetCapers | lost 212 s | - | 9 | 6 |
| TraitorousWaters | NOT ENDED 3600 s | CreateTrigger 30, SetData Trigger.Visibility 1, Worm.DieQuietly 261 | 233 | 521 |
| GibbonTake | lost 197 s | CreateTrigger 2, PlaceMine 2, SetData Mine.DetonationType 1, SetData Mine.DudProbability 1, SetData Trigger.Visibility 1 | 12 | 11 |
| FastFoodDino | won 180 s | SetData Jetpack.InitFuel 1, SetData Water.Level 1 | 22 | 20 |
| EscapeFromTreeRex | lost 1809 s | CreateTrigger 2, SetData Trigger.Visibility 1, Worm.Respawn 3 | 4 | 1 |
| ChuteToVictory | lost 3004 s | CreateTrigger 5, SetData Mine.DetonationType 1, SetData Mine.MaxFuse 1, SetData Mine.MinFuse 1, SetData Trigger.Visibility 1 | 4 | 0 |
| TheLandThatWormsForgot | lost 207 s | CreateTrigger 1, SetData Trigger.Visibility 1 | 6 | 1 |
| ValleyOfDinoWorms | lost 216 s | CreateTrigger 1 | 7 | 0 |
| ChallengeSniper | won 180 s | SetData Challenge.EndlessGun 1 | 26 | 0 |
| ChallengeJetpack | won 180 s | SetData Trigger.Visibility 1 | 54 | 0 |
| ChallengeSheep | won 180 s | SetData Trigger.Visibility 1 | 19 | 0 |
| ChallengeIcarus | won 180 s | SetData Trigger.Visibility 1 | 25 | 0 |
| ChallengeShotgun | won 180 s | SetData Trigger.Visibility 1 | 29 | 0 |
| ChallengeAccuracy | won 180 s | CreateTrigger 7 | 35 | 0 |
| ChallengeNavigation | won 180 s | SetData Mine.DetonationType 1, SetData Mine.DudProbability 1 | 34 | 0 |
| ChallengeCrate | won 180 s | SetData Trigger.Visibility 1 | 29 | 0 |
| ChallengeSniper2 | won 180 s | SetData Challenge.EndlessGun 1 | 84 | 0 |
| ChallengeJetpack2 | won 180 s | SetData Trigger.Visibility 1 | 119 | 0 |
| ChallengeSheep2 | won 180 s | SetData Trigger.Visibility 1 | 48 | 0 |
| ChallengeShotgun2 | won 180 s | SetData Trigger.Visibility 1, Weapon.PreSelected 16 | 36 | 0 |
| ChallengeAccuracy2 | won 206 s | - | 31 | 0 |
| ChallengeNavigation2 | won 180 s | PlaceMine 15, SetData Mine.DetonationType 1, SetData Mine.DudProbability 1, SetData Mine.MaxFuse 1, SetData Mine.MinFuse 1 | 7 | 0 |
| ChallengeCrate2 | won 180 s | PlaceMine 14, SetData Mine.DudProbability 1, SetData Mine.MaxFuse 1, SetData Mine.MinFuse 1 | 28 | 0 |
| DeathMatch1 | won 199 s | - | 7 | 0 |
| DeathMatch2 | won 214 s | - | 6 | 0 |
| DeathMatch3 | won 203 s | - | 7 | 0 |
| DeathMatch4 | won 217 s | - | 7 | 0 |
| DeathMatch5 | won 238 s | - | 6 | 0 |
| DeathMatch6 | won 233 s | StartMineFactory 9 | 6 | 0 |
| DeathMatch7 | won 232 s | - | 6 | 0 |
| DeathMatch8 | won 217 s | - | 7 | 0 |
| DeathMatch9 | won 213 s | - | 7 | 0 |
| DeathMatch10 | won 235 s | - | 7 | 0 |
| DeathMatch11 | won 244 s | - | 6 | 0 |

# Missions and challenges

Single player content is `missions/<id>.json`: bundled ones (CC0, ours) in `romfs:/missions/`, W4M ones written by `tools/w4m-maps` to `assets/missions/` (local only). A mission is listed only if its map exists. Team 0 is the player; the rest are CPU teams. Objectives are evaluated in the sim (`missionStep`, checksummed), so a mission plays the same for a given seed and inputs. Progress: `progress.txt` next to `setup.txt` (`<id> <done 0/1> <best ticks>` per line). Missions of a campaign unlock in `order`; challenges are always open.

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

`tools/w4m-maps` reads the level list from `Data/Tweak/SCRIPTS.XOM` (story = type 4, challenges = 8, deathmatches = 9), names and briefs from `Data/Language/PC/English.xom`, teams, worms, inventories, crates and triggers from the level databank `Data/<LEVEL>.XOM`, and the setup and win/lose conditions from the level's Lua script (see `docs/w4m-formats.md`). W4M missions are staged scripts (cut-scenes, worms and crates spawned mid-mission, per-index checks); the import keeps the opening setup and turns the callback that sends `GameLogic.*.Success` into one objective (`Crate_Destroyed` -> destroy targets, `Crate_Collected` -> collect crates, `Trigger_Destroyed` -> destroy the trigger spots as targets, `Trigger_Collected` -> reach the first trigger, `Timer_GameTimedOut` -> survive, otherwise kill all). Challenges come out close to W4M; story missions are approximations.

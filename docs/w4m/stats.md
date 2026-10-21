# 27. Match statistics (StatsCollectorService, stat sources, mappers, stores)

How W4M accumulates the per-worm stats that the round / match end screens and the accolades read. Addresses are VAs in
`WormsMayhem.exe`. Stat ids are the `Stat.Name.<n>` labels of the language files [data].

## 27.1 Service, stores, message flow

- StatsCollectorService (vtable 0x86a7a4): HandleMessage is **0x611320** (vtable slot 7). 0x60fd10 is its init, run on the
  generic message 0x40 (subscribes the ten messages and creates the stores) [disasm]. Everything below is skipped while
  FlowControlService +0x160 bit 0 is set [disasm 0x611367].
- Stores created by 0x60fd10 [disasm]: +0x20 `PersistStats`, +0x24 `RoundStats`, +0x28 `MatchStats` (PersistStatsStorage,
  vtable 0x86af9c), +0x2c `RoundShots`, +0x30 `MatchShots` (ShotStatsStorage, vtable 0x86b574). Their data are the
  STATS.XOM containers `RoundStats` / `MatchStats` (StoredStatsCollective: WormStats ref[], TeamStats ref[], TeamNames str[]),
  `RoundShots` / `MatchShots` (ShotStatsCollective: Shots ref[]), and `RecentDamage` (DamageStatsContainer: RecipientTasks,
  SenderTasks, WeaponNames) [data, `pe.py schema`].
- A worm record is a StatsContainer `Values` u32[25]; the reset (0x615750, vtable +0x38) rebuilds 16 worm records of 25 zeros and
  empties TeamStats and TeamNames [disasm].
- PersistStatsStorage API (vtable offsets) [disasm]: +0x50 get(&out, worm, stat) 0x613c80, +0x54 set(value, worm, stat) 0x613d60,
  +0x58 add(value, worm, stat) 0x613f30 = get + set (writes nothing when the worm has no WormData (Rm 0x50bdf0) or no TeamData
  (0x50bf60 on worm +0x127)); +0x40 / +0x44 get / set used for the max stat 9; +0x3c team get by team name (0x613ff0, TeamStats
  record matched in TeamNames); +0x5c / +0x60 merge another store (0x614b60 team records by name, 0x615150 worm records).
- Messages (0x611320) [disasm]:
  - `Stats.Update` (0x9618f0): vtable +0x28 = 0x60ffe0, builds the mapper of the raw id (below) on **RoundStats + RoundShots only**,
    calls its Map(a, b), deletes it.
  - `Stats.PresentAwards` and `Stats.Compile`: vtable +0x34 = 0x610820: RoundStats +0x6c (0x614960, per-worm records folded into
    their team record via +0x64; the fold 0x688f4c is not decoded [assumed: sum]), `RoundStats.log`, then +0x2c = 0x6106d0:
    MatchStats +0x5c(RoundStats), MatchStats +0x60(RoundStats), MatchShots +0x54(RoundShots), `MatchStats.log`; then +0x30
    (savegame stats). PresentAwards also writes `ElapsedMatchTime` = `ElapsedRoundTime` / 1000 (seconds) [disasm 0x611320].
  - `Stats.ResetRound`: RoundStats +0x38, RoundShots +0x38 and +0x3c. `Stats.ResetMatch`: MatchStats +0x38, MatchShots +0x38.
    `Stats.Delete`: RoundStats / MatchStats +0x68, RoundShots / MatchShots +0x38.
  - `Stats.Populate.Round`: vtable +0x40 = 0x60ffb0 (survival of every living worm, 27.3), then +0x38 = 0x610950 (a
    RoundStatsSummary entity over RoundStats / RoundShots). `Stats.Populate.Match`: +0x3c = 0x610a60 (MatchStatsSummary entity over
    MatchStats / MatchShots, per team).
  - `GameLogic.Turn.Started`: `RecentDamage` emptied (0x610df0: its three arrays), RoundShots +0x3c.
- Round → match merge rule (0x615150, same in 0x614b60) [disasm]: for each record, every stat is **added**, except stats 9 (Max
  Damage Per Shot) and 19 (Survival Time), which keep the **max**.

## 27.2 When the resets and the merge run

- Round start [disasm]: entering FlowControlService state 9 (in game) runs 0x4ead10 (from state entry 0x4eded0). When
  `GameOver.RoundNumber` is 0 or the NetService instance `[0x979ddc]` exists (0x705807) it posts `Stats.Delete`,
  `Stats.ResetMatch`, 0x617d90 (MatchStatsSummary), `Stats.ResetMatch`; then always `Stats.ResetRound`, 0x618f10
  (RoundStatsSummary), `Stats.ResetRound`. So the match stores reset at the first round, the round stores at every round.
  [assumed: `[0x979ddc]` is non-null only in a network game, which then resets the match stores every round.]
- `GameOver.RoundNumber` = 0 is written by `StartGame$Multiplayer` (FrontEndService 0x728508, 0x72864c) only when the new-match
  byte `[0x947f86]` is set; it also posts `ClearStats` there [disasm]. The win / draw paths add 1 (docs/w4m/turn.md §6).
- Round end [disasm]: GameLogicService::HandleEndOfGame 0x4fb880 posts `Stats.PresentAwards` first (0x4fbc25, the merge), then per
  end message `Stats.Populate.Round` (and `.Match` at the match end) before `GameLogic.Win` / `Draw` etc. So the living worms'
  survival written by Populate.Round lands in RoundStats after the merge: the round screen shows it, MatchStats does not get it
  [disasm order; assumed: 0x6910e4 dispatch keeps that order].

## 27.3 Sources and mappers

`StatsSourceEntity::PostUpdate` 0x6126f0(raw id, a, b) posts `Stats.Update`; 0x60ffe0 (jump table 0x61065c) maps the raw id to
a mapper class (vtable, Map = slot 0) [disasm]:

| raw | mapper (vtable, Map) | writes (RoundStats) |
|---|---|---|
| 0 | DamageStatsMapper 0x86a0f8, 0x60f830 | stat 0, 7, 9, 22 / 23, 21 |
| 2 | WeaponStatsMapper 0x86a544, 0x60f440 | new shot record; stat 6 |
| 3 | KillStatsMapper 0x86a158, 0x610ec0 | stat 1, 20 |
| 4 | TeamKillStatsMapper 0x86a160, 0x60ec10 | stat 2, 20 |
| 5 / 6 / 7 / 8 | Weapon / Utility / Health / MysteryCrateStatsMapper 0x60efb0 / 0x60f050 / 0x60ef10 / 0x60f0f0 | stat 10 / 12 / 11 / 13 |
| 9 | HomeRunsStatsMapper 0x60f190 | stat 14 [assumed from the label] |
| 10 (0xa) | SkipTurnStatsMapper 0x60f2d0 | stat 18 |
| 11..13 | Barrel / Landmine / SentryGunStatsMapper 0x60ed30 / 0x60edd0 / 0x60ee70 | stats 3 / 4 / 5 [assumed from the labels] |
| 14 (0xe) | SurvivalStatsMapper 0x60f370 | stat 19 (max) |
| 15 (0xf) | JetpackStatsMapper 0x60f230 | stat 17 [assumed from the label] |
| 16 (0x10) | TeamDamageStatsMapper 0x60eb00 | stat 24 (the label says Dynamite Damage) |
| 0x11..0x14, 0x15..0x18, 0x19..0x1c | TeamIndexDamage / TeamNumWormsDamaged / TeamNumWormsKilled (team 0..3) | not decoded |
| 1 | none: asserts "Unknown Raw Stat Type!" | |

**Worm responsible** (0x612770(&out, hint, weapon)) [disasm]: for weapon ids 1..31 except Landmine (8) and SentryGun (27), and for
0x43 (Undefined, the default), it is the active worm: 0x612650 = the WormManager's current worm (0x5b27e0 → +0x30 index), else
`ActiveWormIndex`, else `LastActiveWormIndex`; the hint is unused. Landmine logs "Could not trace worm" and SentryGun returns 1:
no stat is written for them. Weapon ids are WeaponNameEnum (table 0x90c920) [data].

### Per stat

- **0 Total Damage** [disasm]: WXWormLogicEntity take-damage 0x5ab7e0 calls the hook 0x613230(amount, victim worm, victim task)
  with the amount actually added to the per-type damage array (after the 75 / 150 caps of types 2..4, floor 0), every damage type
  except 6 (poison) (0x5abba4); the amount is not clipped to the victim's health. 0x613230 looks the victim task up in
  `RecentDamage` (0x612980: first RecipientTasks match → its WeaponNames entry; none → 0x43) and takes the worm responsible for
  that weapon. Posts: raw 0x10 (amount) when attacker's and victim's teams share TeamData +0x6c (AlliedGroup), raw 0x11 + victim
  team (amount), raw 0x15 + victim team (1), raw 0 (amount), all for the attacker. DamageStatsMapper: stat 0 += amount for the
  attacker, whatever the victim (self, own team and allies included). Also stores the weapon in `[0x921a40]`, the victim in
  `[0x921a44]`, the attacker in `[0x921a48]`.
- **7 Damaging Shots** [disasm 0x60f87c..0x60f8cf]: on each raw 0 update, if the current shot record's damage field (RoundShots
  +0x48 field 1) is still 0, stat 7 += 1 for the attacker; then the shot's damage += amount (+0x50). So one per shot that
  produced a damage update, credited to the worm responsible.
- **9 Max Damage Per Shot** [disasm 0x60f8dd..0x60f919]: after that add, if the shot's damage > the attacker's stat 9, stat 9 = it.
  Merged by max.
- Stats 22 / 23 [disasm]: + amount when the shot's weapon field is 1 (Bazooka) / 2 (Grenade). Stat 21 + amount when the attacker
  is `ActiveWormIndex` and the last `RecentDamage` weapon is 8 (Landmine).
- **6 Shots Fired** [disasm]: WeaponStatsSource 0x612d40, called by the fire of BaseUtility 0x549790, BaseWeapon 0x54a430,
  Gun 0x55db30 (two sites), Melee 0x569930, Payload 0x586040 (two), Powered 0x586e40, reads the selected weapon (WormData +0xf4);
  SkipGo (38) and Redbull (41) post nothing; else raw 2 (weapon, worm responsible). WeaponStatsMapper: RoundShots +0x4c adds a
  shot record (weapon in field 2); stat 6 += 1 when the weapon is 1..31 and not Landmine (8), Flood (14) or SentryGun (27).
- **1 Enemy Kills / 2 Team Kills** [disasm 0x613340]: worm death 0x5a7190 (from WXWormLogicEntity 0x5ad640 / 0x5adbf0) sets the
  victim's health +0x11e to 0, then 0x613340(victim), then the damage hook with amount 0. 0x613340: survival update for the
  victim (below); killer = worm responsible (weapon 0x43, so the active worm); nothing when that fails, when `[0x961b14]` is 1
  (set on `GameLogic.AboutToWaterRise`, cleared on `GameLogic.Turn.Ended` and `.Started`, GameLogicService 0x4fdc90), or when the
  victim has no WormData. Killer == victim: nothing when `[0x921a40]` (last damage weapon) is 20 (Starburst), else a team kill.
  Same team, or teams sharing AlliedGroup (+0x6c): team kill. Otherwise an enemy kill. Posts raw 0x19 + victim team (1, killer),
  then raw 3 (enemy) or raw 4 (team) (1, killer). Kill mapper: stat 1 += 1; TeamKill mapper: stat 2 += 1; both stat 20 += 1 when
  the kill weapon is Sheep (15), ConcreteDonkey (18) or SuperSheep (19) [assumed: the compared operand is held in a register
  Ghidra does not follow, its source is not traced].
- **10 / 11 / 12 / 13 crates** [disasm]: crate collection 0x5cb5a0 calls CrateStatsSource 0x611bd0(crate type +0x20) (table 0x611c40):
  type 0 weapon → raw 5 → stat 10, type 1 health → raw 7 → stat 11, type 2 utility → raw 6 → stat 12, type 4 mystery → raw 8 →
  stat 13; types 3 (target) and 5 (custom) post nothing. +1 for the worm responsible (weapon 0x43: the active worm), not the
  worm that touched the crate.
- **18 Boring Turns** [disasm]: SkipgoUtilityLogicEntity (vtable entry 0x85cd2c → 0x587ed0, once per use, guarded by +0x44) reads
  `ActiveWormIndex` and calls SkipGoStatsSource 0x612420(worm): raw 0xa (1, worm). SkipTurnStatsMapper: stat 18 += 1. So a boring
  turn is a turn ended with Skip Go.
- **19 Survival Time** [disasm 0x613080, 0x60f370]: value = (`RoundTime` − `RoundTimeRemaining`) / 1000, integer seconds (both ms;
  turn.md §14), posted as raw 0xe for a worm; the mapper keeps the max (get stat 19, set when larger). Posted at the worm's death
  (0x613340, also from 0x5a6970 cleanup and 0x5ac160) and, at `Stats.Populate.Round`, for every worm 0..15 whose health +0x11e > 0
  (0x60ffb0). The round clock stops at 0 in sudden death, so the value tops at RoundTime / 1000.

## 27.4 Readers

- MatchStatsSummary 0x619a90 reads MatchStats stats 0 and 1 per team (+0x3c) and the MatchShots list (worm, damage, weapon < 0x1f):
  accuracy = damaging shots × 100 / shots fired, capped at 100; weapon of choice = the weapon with strictly the most shots
  [disasm, earlier decode].
- AccoladeRulesImpl 0x619ff0..0x61a5b0 reads per-worm stats 0, 1, 2, 6, 7, 10..13, 18, 19 [disasm, earlier decode].

## 27.5 What the end screens read (RoundStatsSummary 0x618550, MatchStatsSummary 0x6173e0, AccoladeRulesImpl 0x619f40..0x61a5b0)

- Round rows (`WXFE.WinRoundList`) [disasm]: accolade type t = 0 Damage (store 0), 1 Kills (1), 2 Accuracy (store 7 x 100 / store 6, rounded, 0 without a shot), 3 Greedy (10 + 11 + 12 + 13), 4 Boring (18), 5 Survivor (19), 6 TeamKills (2), evaluated per worm whose WormData +0x125 is set (the match's worms) by 0x618190 (rule factories 0x619f40 Damage, 0x619f80 Kills, 0x619f00 Accuracy, 0x619f60 Greedy, 0x619f20 Boring, 0x619fa0 Survivor, 0x619fc0 TeamKills). Positive = the greatest value, except Boring (the least) and TeamKills (the least); negative the opposite. The award goes to the one worm holding the value alone: none when it is shared, and none for a greatest of 0.
- Text: `Stat.Accolade.Pve.<t>` / `Nve.<t>` plus a suffix per type ` (%u)`, ` (%u)`, ` (%u %%)`, none (Greedy), none (Boring), ` (%um %us)` (Survivor, seconds), ` (%u)`; the worm name (WormData +0xb0) in its team's colour `15 + AlliedGroup`.
- Selection: the 7 types are shuffled (`for i in 0..6: swap(a[i], a[rand() % 6])`); P positives and N negatives counted over the types in order (each capped at 4). With P + N = 0 the `No Stats` text shows; P + N > 4 trims: P < 2 -> N = min(N, 4 - P); P >= 2 and N >= 2 -> P = N = 2; P >= 2 and N < 2 -> P = 4 - N. The rows then cycle the shuffled list (positive while positives remain, then negative), each success one row, up to 4 rows.
- Match page (MatchStatsSummary): per team: damage / kills = the stored stats 0 / 1 summed over its worms, accuracy = the team's damaging shots (shot-list entries with a damage) x 100 / its stat 6, capped 100, weapon of choice = the first of W4M weapon ids 1..0x1e with strictly the most shots (0x619a90, ties keep the lower id), elapsed `ElapsedMatchTime` [disasm; the summing of worms into a team is assumed, §27 open items].

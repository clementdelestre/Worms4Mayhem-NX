# Death sequence

How long the camera stays on a worm that dies, in W4M and here. 60 ticks = 1 s.

## What W4M does

- **No death clip.** The worm animation bundle (`Data/Bundles/Bundl474.xom`, every worm clip name) has no `Die`/`Death`/`Explode` clip. The worm "dies" through acting: the exe's physics states include `kWPS_DeathThroes` (and `kWPS_DrownFloat` for the drowned). Worms are queued with `GameLogic.AddMeToDeathQueue`, then `Worm.TimeToDie` → `Worm.Die` → `Worm.LandDeath` / `Worm.WaterDeath` → `Gravestone.Create` (`GraveStoneLogicEntity`) → `Worm.Died` (strings of `WormsMayhem.exe`).
- **The last gesture is a WORMACTING scene.** `Data/Tweak/WORMACTING.XOM` `Death5a`–`e` (the dying worm), `Death15a` (poisoned), `Death10*` (friends nearby). Death5 scenes all end at **3000 ms**: Sad, then WhatWereYouThinking / SighAndShakeHead / Salute / ClutchChest / Doh. Death15a: Ill, Vomit, ends at 3500 ms. Transcribed in `client/src/acting.cpp`.
- The worms wiki matches: in W4M "the Worms simply salute, groan in disappointment, wave, or pretend to move before they explode, and when they are poisoned, they explode right before they vomit"; a worm at 0 hp "will soon self-destruct, causing a small explosion, and leaving behind a gravestone"; the drowned "float for a few seconds and they explode in the surface of the water" (search excerpts from [Death explosions](https://worms.fandom.com/wiki/Death_explosions) and [Energy](https://worms.fandom.com/wiki/Energy); the pages themselves returned HTTP 402).
- The turn flow (`Data/scripts/stdlib.lub`): `TurnEnded` → `DoPostActivity` → `WaitUntilNoActivity` (`ObjectCount.Active`) → `Timer.StartPostActivity` → next turn. The death queue counts as activity, so the next turn waits for the last blast. The post-activity time itself (`PostActivityTime`, `LOCAL.XOM`) is binary and not decoded here.
- Not found in the data: the exact throes duration in the exe code, and the gravestone drop. The Tweak files only hold the blast's damage (`Worm.DeathImpulse*`, `Worm.DeathLandDamageRadius`, `Worm.DeathWormDamage*`).

## Measured durations (our assets)

| Item | Duration |
|---|---|
| Death5 scene (WORMACTING) | 3.0 s; Death15a 3.5 s |
| Gesture clips in `worm.glb` | Sad 2.0, WhatWereYouThinking 3.83, SighAndShakeHead 5.54, Salute 3.0, ClutchChest 7.08, Doh 1.25, Ill 1.0, Vomit 3.42 s |
| Gestures inside the scene | start at 0.16–0.81 s; Salute ends at 3.81 s, Doh at 1.45 s. The 3 s scene cuts the long ones, as in W4M |
| Death, explosion, gravestone clips | none: `worm.glb` has no death clip, `grave0-3.glb` have no animation (static mesh, drawn on the ground at once) |
| Death voices (`voices/*/death*.ogg`, 75 files) | median 1.36 s, p90 2.35 s, max 3.66 s (3 files over 3 s) |
| Explosion sound (`sfx/explosion*.ogg`) | 2.7, 3.2, 4.0 s files; audible for 2.5–3.2 s (−40 dB) |
| Explosion particles (`fx.cpp`) | flash 0.3 s, fireballs 0.7 s, smoke up to 2.8 s |
| Death banner (`ui.cpp` drawBanner) | LIFE 3 s alone, 2 s when another banner is queued (e.g. TeamDeath) |

## Sequence, before and after

Settle count for a group: camera travel `COUNT_TRAVEL` 42, count (≤ 90), `COUNT_LINGER` 30 → `countBoom()`.

**Before:** the camera closed on each dead worm 45 ticks before its blast and left 75 ticks after it (`COUNT_DEATH` 75, `COUNT_NEXT` 120 between deaths): **2 s per death**. The Death5 gesture started when the hp hit 0, often during the shot, so it was mostly over by the time the camera came. The camera then left 1.25 s after the blast: the voice (median 1.4 s), the smoke and the 3 s banner were cut.

**After** (`sim.h` `blastAt`, `deathLead`, `dying`):

| t from the close-up | Event |
|---|---|
| 0 (= `countBoom() − 45`, during the linger) | camera closes on the dead worm; the Death5 / Death15 scene starts (`acting.cpp`, on `dying()`); a friend nearby plays Death10 |
| +3.0 s (`COUNT_THROES` 180) | blast: land carved, explosion sound and particles, death voice, LandDeath banner, gravestone |
| +6.0 s (`COUNT_DEATH` 180 after the blast) | next dead worm's close-up, or the count ends and the next turn starts |

That is **6 s per death** (3 s gesture, 3 s for the banner, the voice and the smoke). A drowned worm keeps its 45-tick lead: it has floated during the count (`COUNT_FLOAT`) and has no gesture. It pops at the surface at `countBoom()`, then gets 3 s.

The sim stays deterministic: the schedule comes from `countGroup`, `countT` and the worm positions, which are already in `checksum()`. No new state.

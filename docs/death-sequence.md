# Death sequence

How long the camera stays on a worm that dies, in W4M and here. 60 ticks = 1 s.

## What W4M does

- **No death clip.** The worm animation bundle (`Data/Bundles/Bundl474.xom`, every worm clip name) has no `Die`/`Death`/`Explode` clip. The worm "dies" through acting: the exe's physics states include `kWPS_DeathThroes` (and `kWPS_DrownFloat` for the drowned). Worms are queued with `GameLogic.AddMeToDeathQueue`, then `Worm.TimeToDie` → `Worm.Die` → `Worm.LandDeath` / `Worm.WaterDeath` → `Gravestone.Create` (`GraveStoneLogicEntity`) → `Worm.Died` (strings of `WormsMayhem.exe`).
- **The last gesture is a WORMACTING scene.** `Data/Tweak/WORMACTING.XOM` `Death5a`–`e` (the dying worm), `Death15a` (poisoned), `Death10*` (friends nearby). Death5 scenes all end at **3000 ms**: Sad, then WhatWereYouThinking / SighAndShakeHead / Salute / ClutchChest / Doh. Death15a: Ill, Vomit, ends at 3500 ms. Transcribed in `client/src/acting.cpp`.
- The worms wiki matches: in W4M "the Worms simply salute, groan in disappointment, wave, or pretend to move before they explode, and when they are poisoned, they explode right before they vomit"; a worm at 0 hp "will soon self-destruct, causing a small explosion, and leaving behind a gravestone"; the drowned "float for a few seconds and they explode in the surface of the water" (search excerpts from [Death explosions](https://worms.fandom.com/wiki/Death_explosions) and [Energy](https://worms.fandom.com/wiki/Energy); the pages themselves returned HTTP 402).
- The turn flow (`Data/scripts/stdlib.lub`): `TurnEnded` → `DoPostActivity` → `WaitUntilNoActivity` (`ObjectCount.Active`) → `Timer.StartPostActivity` → next turn. The death queue counts as activity, so the next turn waits for the last blast. `PostActivityTime` is **2400 ms** (`LOCAL.XOM`).
- Exe timers (docs/w4m-map.md §14, all ms): a hurt worm holds an active token until `Worm.DamageComplete`, posted **2500 ms** after the damage (0x5abe94). The death queue (0x4f9b30, every 20 ms) pops the first dead worm once every other active object is done, so after every damage display. `Worm.TimeToDie` sets the **DeathThroes timer to 3000 ms** (0x5adbf0); at 0 the worm blows up, a gravestone is created (0x5a9310, no active token of its own) and the worm is unspawned. The next dead worm pops on the queue's next tick: **no wait on the grave** between deaths. A drowned worm floats **2000 ms** (`kWPS_DrownFloat`, 0x5aa222), then blows up at the surface, outside the queue.
- The death blast (`Worm.DeathImpulse*`, `Worm.DeathLandDamageRadius`, `Worm.DeathWormDamage*`): 35 hp within 3 m, a 1.75 m crater, a strong short push, also for the drowned (docs/weapons-audit.md "Death blast").

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

## Sequence (`sim.h` `countSpan`, `blastAt`, `dying`, `POST_ACTIVITY`)

Before the first group: `Settle` waits for the shots (dropped after 30 s, `SHOT_CAP`, ours), then for every worm grounded and every object still (at most 5 s, `SETTLE_WAIT`, ours: W4M WaitUntilNoActivity has no timeout), then rolls the abductees' hp once (W4M DoPostActivity ApplyPoison, 0x5ac060; a roll of 0 kills, so that worm joins the queue). Turn flow: `docs/sim.md`.

Settle count for a group of nearby worms (the first worm whose label differs from its hp and every such worm within 18 m, `COUNT_SPAN`, ours): camera travel `COUNT_TRAVEL` 42 ticks (ours), then the W4M damage display `COUNT_DAMAGE` (2500 ms, the hp label counts during it), or `COUNT_FLOAT` (2000 ms) for a drowned worm. `countBoom()` is the end of the longest one.

| t | Event |
|---|---|
| `countBoom()` | every damage display is over: the first dead worm's throes start (Death5 / Death15 scene, `acting.cpp` on `dying()`; a friend nearby plays Death10) |
| + 3.0 s (`COUNT_THROES`) | blast (`Game::DEATH_BLAST`), explosion sound and particles, death voice, LandDeath banner, gravestone |
| + 1 tick (`COUNT_DEATH`) | next dead worm's throes, or the queue ends |
| end of the last group | `POST_ACTIVITY` (2400 ms), then the next turn |

A drowned worm blows up at `COUNT_TRAVEL + COUNT_FLOAT`, whatever the queue does. A living worm the death blast hurt counts again in a later group (its own 2.5 s display).

Before (our guesses): the throes started 45 ticks before `countBoom()`, each death then held the camera 3 s on the grave (`COUNT_DEATH` 180), the count lasted at most 1.5 s + 0.5 s linger, and the next turn started at once. Now: 3 s per death, 2.5 s per damage display, 2.4 s before the next turn.

The sim stays deterministic: the schedule comes from `countGroup`, `countT`, `countEnd`, `timer` and the worm positions, which are all in `checksum()`.

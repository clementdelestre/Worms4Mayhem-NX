# Worm reactions (W4M acting)

In W4M, everything worms do outside their control comes from `Data/Tweak/WORMACTING.XOM`. This file is a bank of 142 small EFMV movies (`EFMV_MovieContainer`, docs/w4m/acting.md §19).

Each movie is named after its trigger, as `<Trigger><N><variant>`. It holds tracks. A track's name is a string of casting criteria, e.g. `Crit Special`, `Foe1 OnScreen Near1 Idle` or `Friend-1`. Each track carries events timestamped in ms:
- `WormEmote`: facial emotion, kept until the next one, even after the movie ends;
- `PlayAnimation` / `StopAnimation`: a body gesture;
- `TriggerSpeech`: a `.lsd` voice category;
- `WormLookAt` / `WormGestureAt`: the head / arms turn toward another track's actor;
- `ThreatenWorm`, `SpawnParticle`.

## Data and code

- **Data.** The movies are no longer copied into the code. `tools/w4m-re/acting.py` exports them from the player's install to `client/assets/acting.txt` (gitignored, like the models and voices). Without this file, no scene plays.
- **Engine.** `client/src/acting.cpp` reproduces `WXSceneManagerService`: ranking, casting and the player. Everything happens at render time: the sim has no new state and nothing is checksummed. The draws (candidate shuffle, default emotion, ambient trigger rotation) come from a hash of the seed, the worm index and `g.clock`.
- **Rendering.** `client/src/models.cpp` (`Models::Layers`) stacks the `WormPoseManager` (0x59da40) layers on the body clip: face, head, arms and pupils. Everything is computed at render time (`Acting::update`, per-actor state), nothing in the sim. A dead or dying worm (hp ≤ 0) has none: it only shows its clip (FallDrown, Wave).
- **Clips.** `tools/w4m-models` (`WORM_CLIPS`) exports 41 more clips, 13 of them emotions. Each emotion is exported with its mouth (`Happy+HappyMouth`).
- **Size.** A clip's static channels keep a single key. `worm.glb` thus goes from 12.6 to 13.5 MB despite the added clips. `Point` is cut at 8 s (`Point@8`): W4M holds it 26.7 s, but past 0.17 s it only blinks.

Sources:
- **XOM**: decoded `WORMACTING.XOM`;
- **exe**: `WormsMayhem.exe`, address given;
- **lsd**: categories from `Data/Audio/Speech/*.lsd`;
- **wiki**: https://worms.miraheze.org/wiki/Speech;
- **choice**: not checked in W4M.

## Animation layers (WormPoseManager)

| Layer | W4M | Here | Source |
|---|---|---|---|
| Face | Each emotion has two clips: `X` (eyebrows, eyelids, small head and shoulder offsets) and `XMouth` (lips). The scheduler blends channel by channel: a gesture that animates a face channel keeps it. | The face bones (`upperlip*`, `bottomlip*`, `mouthmiddle*`, `eyetop*`, `eyebrow*`) take the emotion, posed relative to the gesture's head. This holds except for bones the gesture itself moves relative to Base: this mask is computed at load, per clip. | `w4m-models --list` (`W4M_CHANNELS`) |
| Default emotion | Each worm gets `Angry` or `Frown` on a coin toss, and keeps it until the first `WormEmote`. `Default` returns to it. | Same, by hash. | exe 0x5a595d |
| Head (LookAt) | The target is aimed at from 9 units above the feet (yaw atan2(x, z), pitch asin(y) in the worm's frame). The head target (+0x128/+0x130) moves only when the eyes would lead by more than 0.6·E (E = the emotion's `PermittedEyeMovement`, 10° by default, 0 in 609 emotions out of 630): it takes the excess × min(1, (excess/0.4E)²) (0x59bd80). It is clamped to ±60°·H in yaw and −45°·H..+70°·H in pitch; the eyes take the rest (±70°, ±60°). The head reaches its target via 0x47a1a0: v += clamp((target − v)/3, ±30°) per update. `HeadRotY/X` (t = 1 + w·angle/(π/2)) stop at ±60° and −70°/+45°. | Same (`lookStep`). One update per 20 ms tick, at most one per frame: the per-frame task queue gets the time rounded to 20 ms (0x68d57a) and the worm updates only if it advanced (0x5a47a0); the laws are not dt-weighted. Head, face and HatLocator rotate around the head joint. Coyness (8 emotions, 30° or 60°): the dead zone is centred on target − Coyness in yaw (+0x18c), so the head turns away while the eyes stay on the target; the sign follows the last head turn, which the exe always measures as zero (0x59e9e3): always +. Fixed when the gaze changes. | exe 0x59be40, 0x59bd80, 0x59b450, 0x47a1a0; clip keys |
| Arms (GestureAt) | Each arm has its mode, read from the clip's `Blend` node: Translate.x for the left, .y for the right. 0: it follows the GestureAt target (c = 1), 1: nothing, −1 or 2: it follows the head (d = 1), interpolated between these values. Base, Point, Wave, Indicate: 0; most gestures: 1; Salute, Yawn: 2; WaveAndPoint: −1 left, 0 right. The angle (0x59c3e0, unsmoothed) is added to the shoulder rotation by `Left/RightArmRotY/X`: t = 1 + w·(c·target + d·head)/(π/2), ±90° at the ends. +0x134..+0x144 are the head and its snapshot, +0x198 (w) the "Forbid Lookaround" fade. | Additive rotation of the shoulder subtree around the shoulder joint, first around `main`'s x axis, then its y axis (fitted on the Arm*Rot clips exported separately). Hands checked on screenshots (Point, Wave, Salute, WaveAndPoint). | exe 0x59b870, 0x59c3e0, 0x59d500; `w4m-models --list` |
| Eyes | `Eyes_LR/UD` keys of `shadercolor2` (UV offset) on the boggy*eye materials: t = 1 + Ew·eye/(±π/2). In LR the eye on the aimed side slides to −0.5, the other by 0.02; in UD v goes from −0.4 (up) to +0.37. Ew and H follow the Rotate.y mode of the `Blend` node (in degrees: 0 head and eyes, 1 eyes only, 2 nothing), via 0x47a1a0 (½, ±0.1). | The pupil texcoords (glb material 1) are offset, exe key curve (see Curves). Direction checked on screenshot. | exe 0x59b450, 0x59be40; `w4m-models --list` |
| Fade (PoseBlend) | A new gaze or gesture freezes the values (0x59bb90); `PoseBlend` (Blend.Scale.y, keys 0 → 1 at 0.75 then 1) is played at speed 1/BlendTime (0x6a03d0 → 0x7a9cf8: duration = 1 s × 0.3 s) and blends old and new, PermittedEyeMovement included. While aiming, weapons cut the gaze: w = (w + 0)/2 per update (0x59f3a0). | Same, on the exact key curve, in real time. | exe 0x59bb90, 0x59da40, 0x6a03d0, 0x7a9cf8 |
| Gestures (PlayAnimation) | The gesture is a scheduler layer, not the body clip (0x59c990): the old gesture keeps min(weight, 0.9) and loses 0.1 per update, the new one is 1 − old; replaying the same clip does not restart it. `StopAnimation`: BlendTime 0 (141 of 149) does nothing, 100 or 200 ms fades it out by 20/BlendTime per update (0x59e85a, 0x59dd9c). A finished clip exits (0x59dc41). Weights are multiplied by +0x194 = (+0x194 + !flag)/2, flag = off the ground or walking (0x5a2b4f): the gesture fades when the worm moves, its clip keeps running. | `Layers::act`: poses blended (1 − w) body + w gesture, the XAnim weighted sum (below). The dying worm keeps its death gestures, with no other layer. | exe 0x59c990, 0x59da40 (0x59dc03..0x59e091) |
| XAnim blend | Each channel is Σ weight × value of the clips (0x7ac1a0); Base is played at weight 1. Only attributes with flag 8 are divided by Σ weight (0x7acc6f), flag 0x10 takes the maximum. The flag is byte 1 of the u32 key type: the loader reads byte 0 (XOM field index: 2 Translate, 3 Rotate, 4 Scale), then 2 bytes (flags, byte 2), then byte 3 (axis) (0x7affd6..0x7b0028, 1 / 2 / 1 byte reads from XBinaryObjectIn 0x63e7ca / 0x63eb11), and AttachToNode copies these flags into the attribute (0x7ad5e2); bits 1 / 2 / 4 = object carrying the field (0x7ad53a): 0x904 scale → 0x09 (average), 0x102/0x103 translation/rotation → 0x01 (sum), 0x401 texture offset → 0x04 (sum), 0x1100 texture pick → 0x11 (maximum). A gesture is thus additive on Base, and Arm/HeadRot or Eyes at weight 1 add up. | Additive for arms, head and eyes (LR + UD on the same u channel), average for scale (w4m-models). | exe 0x7ac1a0, 0x7ad0ef, 0x7acc6f |
| Curves | Key = in-tangent x, y, out-tangent x, y, time, value (loader 0x7b01ec). The channel's 4 bytes become flags 1 (must contribute), 8, 4 (static: first value), 2 (weighted) (0x7b0097..0x7b0115). Unweighted (every worm channel except 138, all eye and PoseBlend animation): Hermite on the tangents' y/x slopes (0x7aa7df). Weighted: Bézier with handles at key ± tangent/3, x kept monotonic (Maya's checkMonotonic/constrainInsideBounds, 0x7ab931, 0x7ab6f1, 0x7aa8f4, float epsilon 0x7aa794). Zero out-tangent = step; constant outside the keys. | `Models::curve` (client, Hermite) and `eval` (w4m-models, both). The old "weight, angle" reading was wrong: all models are re-exported. | exe 0x7abb1c, 0x7aa7df, 0x7ab931, 0x7b01ec |
| Tint | Colour = 1 + 2·(wSick·(Sick.Colour/255 − 0.5) + wAbd·(Abducted.Colour/255 − 0.5)). Green is computed as /255·0.5, as in the exe. Sick.Colour = (120, 120, 110), Abducted.Colour = (0, 120, 255). | Multiplies the team tint. Each weight moves linearly to 0 or 1 by 0.05 per 20 ms update, 0.4 s end to end (exe 0x5a1afd, disasm). | TWEAK.XOM; exe 0x5a1bb1 |

## Casting (WXSceneManagerService)

- **Ranking (0x60e100, 0x60d5e0, 0x60c410).** Each movie is filed under every trigger whose name its name contains (case-insensitive). N is the number following that name. A list is sorted by descending N. After each trigger, each group with the same N rotates by one, which alternates the a/b/c variants.
- **Criteria (0x60d640, 0x60c480).** These are case-insensitive substrings followed by an `atoi`. Examples: `Seer0` counts as `See0`, and `Goodie` does not count as `Goodies`.
  - `See`, `Blind`, `InFront`: a 70° cone.
  - `Near N,R`: the nearest within 20·R units, R = 20 by default, i.e. 20 m.
  - `-1`: the active team for Friend/Foe, and the camera for See, Near and LookAt: 0x7e resolves to [0x95a100]+8 vfunc 0x38, the render camera whose +0x10/+0x1c matrices CMS 0x51b3b0 projects with (0x59d47e, See at 0x60c78e) [disasm].
  - A sick worm plays only in a `Sick` track, and vice versa. An abducted worm plays only in an `Abducted` track.
  - An `Idle` track refuses a worm already in a scene, unless it is bored and the trigger allows it (0x60c756) [disasm]. Bored = WXActor +0x6e bit 4, set once graphic +0x5c reaches 90000 ms (0x5a4810); +0x5c counts real ms (+0x198 = ms/20, times 20) and kWE 13, 14, 15, 17, 19, 21 and 22 zero it and clear the bit [disasm]. Starting a scene does not. kWE 7 (a fall) instead kills the worm's scene and sets its StopAnimation blend to 200 (0x60ae60, 0x60ba40).
  - `Safe` refuses a threatened worm (`ThreatenWorm`).
- **Choice (0x60d830).**
  - A `Payload` track takes the payload. An `Active` track takes the active worm: if either is missing, the movie is rejected.
  - A `Special` track draws from the special pool. Depending on the trigger, a failure rejects it or not.
  - Other tracks draw from the general pool. A failure rejects the movie only if the track is `Crit`.
  - The first movie that casts at least one actor is played.
- **Priorities.** There is no numeric comparison. A new movie stops any running movie of a worm it casts (0x60b750). The track must still admit it, though: `Idle` (see above) and `Safe` filter. A movie's end frees the worm and its threat (0x60bf70). The emotion and the gaze remain. Playback (WormScenePlayerService 0x60b940, every 20 ms): each track fires its events in file order while `Time` ≤ the scene clock, one blocking cursor (0x60b6db), then the clock gains 20 ms; while `EFMV.Active` is 1 (here: the abduction) every running scene ends (0x60b96c) [disasm]. Ours: `play()` per 20 ms scene tick, `acting.py` keeps file order.
- **Pools per trigger (jump table 0x60e818).** This resolves an open question in docs/w4m/README.md §21. Triggers 0x1e and 0x2e are Missed and Retreat.

| Case | Triggers | Special / general pool | Options (A, B, C) |
|---|---|---|---|
| 0 | TimedPayload*, CrateDrop | worms within 100 units (5 m) of the payload / the others. The active worm is not excluded [ours, applied in code]. Movies chain as long as threatened worms remain. | 1, 1, 1 |
| 1 | BlastSplat, FallSplat, Death, Collect, Blasted, Poisoned, Zap | [subject] / all but the subject and the active worm | 1, 1, 1 |
| 2 | Idle, Sick, Abducted, ItemReact, Thinking | – / all actors (objects included) but the active worm | 0, 0, 1 |
| 3, 8, 9 | DamageInflicted, FirstBlood, MaxDamage (Revenge) | worms hurt this turn / the others. Then DamageSilent for each other hurt worm. | 0, 0, 1 then 1, 1, 1 |
| 4, 6, 7 | ShortOnTime, StartTurn, Waiting, Airstrike, Missed, Targeted, Taunt*, Retreat, Boring, SkipGo, WeaponFired | – / all but the active worm. WeaponFired brings its payload. | Boring 0, 1, 1; SkipGo 0, 1, 1; others 1, 1, 1 |
| 5 | Mistake | [subject] / all but the subject | 1, 1, 1 |
| 10 | Victory | winner's team / the others | 1, 1, 0 |
| 11 | Bored | bored worms, except the active one | 0, 0, 1 |
| 12 | DamageSilent (direct), Punch, WormBounce, FireDamage, Titter, Grenade* | nothing is cast | – |

Options: A = sets the priority and clears "bored", B = a bored worm in a scene may play an Idle track, C = failure of a Special track rejects the movie.

## Triggers

| Trigger | W4M dispatch | Here | Status |
|---|---|---|---|
| TimedPayloadFive..One, GrenadeFive..One | ParabolicPayloadLogicEntity update 0x576fc0: the payload's velocity is zero, once (+0x1b4 bit 0, cleared when it is moved again, 0x5777da). The trigger depends on whole seconds to its expiry, (+0x58 − clock +0x1a0) / 1000 unsigned: 0 → One, 3 → Four, 4 or more, or no expiry (−1), → Five (0x577181). A `Grenade.Weapon` payload adds 0x27: the Grenade* rows (0x5771c0). Mines are Parabolic payloads (`CreateMine` 0x4f9630) [disasm]. | Fused shell placed or stopped (the Grenade by name); any mine at rest (idle: Five) | done |
| CrateDrop | CrateGraphicEntity 0x5c4710, from the crate's `Create` clip (0x5c4bb1, 0x5c516e): as the crate spawns, with `WXP_CrateSpawnDropping`, `weapons/CrateSpawn` and `Comment.<Weapon/Utility/Health/Mystery>CrateSpawn`; every crate type posts trigger 0x13 with the crate actor [disasm]. A worm must be within 5 m of it. | The CrateDrop event (spawn) | done |
| Idle, Sick, Abducted, Bored | WXWormManagerService task 0x5b3280 (returns 100: runs every 100 ms). Countdown +0x1ac starts at rand % 5000 (0x5b526f), loses 100 per run, fires at ≤ 100 (0x5b34f5), then restarts at 300 + rand % 300: 300 to 600 ms in 100 ms steps. Rotation by the 12-entry table 0x9200dc (Idle, Sick, Abducted, Bored ×3). The guard: while `EFMV.Active` (+0xe0, bound at 0x5b4cc4) is non-zero the countdown is frozen (0x5b34e4) [disasm]. Examples: Idle0/10 is the active worm near a friend or foe within 20 m; Idle20/50/100 are Gunslinger or FlickBogey pairs and duels; Sick10 is a poisoned worm. | Same; frozen during the abduction (its EFMV.Active, `abducting()`). | done |
| Thinking | No dispatch in the PC exe: no call to 0x4d3410 with 0x2f. | Never triggered. The movies stay in `acting.txt`. The `WXP_WormThinking` particle is thus never emitted. | as W4M (dead) |
| ItemReact | No dispatch in the PC exe (no direct call, nor ambient table 0x9200dc). | Never triggered. | as W4M (dead) |
| StartTurn | The worm becomes active (0x5a421e). | TurnStart | done |
| WeaponFired, Airstrike, SkipGo | 0x585e3d, Bomber 0x54d7c0, 0x588066 | Fire event | done |
| Targeted | 100 ms in the aiming camera's field of view (mode 1, first person), then a 3000 ms cooldown per worm (0x5a2600). | First-person aim (`Controls::firstPerson`) | done |
| Blasted | Impulse with vy > 0.1 unit/ms (§11, event 13). | Hurt worm taking off at more than 5 m/s | done |
| BlastSplat | kWE 15 hard landing (vn ≤ −0.3 units/ms) in anim state 3 (0x5a3d4d) [disasm]; a soft landing (kWE 8) sends nothing | Hard landing after a blast or a fall past 15 m/s | done |
| FallSplat | kWE 15 hard landing (vn ≤ −0.3 units/ms) when the anim state is not 3 (blast flight): trigger 6 + RecoverBurried1 (0x5a3e75) [disasm]. State 3 comes from kWE 13/14 (blast) or 22 (falling past 0.3 units/ms), so a plain fall that lands hard has always passed 22 first. | Hard landing without a blast or a fall past 15 m/s | done |
| FastRecover | No trigger has this name: dead movie | – | n/a |
| Death | Start of the death convulsions (0x5a3ffb). Drowned worms skip it. | `g.dying()` except drowning. Death15/20 "Sick" for a poisoned worm. | done |
| Poisoned | 0x5a1a89 | Poison increases | done |
| Mistake, FirstBlood, MaxDamage, DamageInflicted, Boring | On `GameLogic.ApplyDamage` (0x5b22a0), in this order: Mistake if damage to friends > damage to foes / 3 and the shooter hurt himself; FirstBlood for the match's first death; MaxDamage if a worm is in the explosion's core, 1.2·(range − d)/range ≥ 1 (0x5ae6b5); Boring if nobody is hit and no payload went off; DamageInflicted otherwise. | On entering Settle | done |
| Boring (NoDamageA/B) | Its `Special` track has no candidate (empty special pool), and the other tracks target the empty track 0. The movie is thus always rejected. | Same logic: it never plays. | as W4M (dead) |
| Missed | Nobody is hit and a payload went off (0x50fe0d) | On entering Settle | done |
| ShortOnTime, Waiting | The displayed clock reaches 5 s, then 15 s (HudClockEntity 0x5f0191, 0x5f01db) | Same | done. Waiting is no longer "still for 12 s" |
| Retreat | Retreat timer (0x50f44b) | Start of the Retreat phase | done |
| Collect | 0x5cb6a9 | Collect event | done |
| Victory | End of match | GameOver. Living losers play the Foe0 tracks (Sad, WhatWereYouThinking, SighAndShakeHead, Doh). | done |
| Zap, Abducted | Leaving the abduction beam sets the "abducted" flag (0x547d39). `Worm.Antidote` clears it (0x5adcf0, handler 0x5ade7f); health and mystery-health crates send it on collection (CrateLogicEntity 0x5caf27, 0x5cb13a) [disasm]. UpdateAbductee 0x5a9c40 posts trigger 0x22 Zap with the worm each time it teleports, after `WXP_Abductee_Teleport` (0x5a9d65) [disasm]. | Alien Abduction Fire: worms in the beam. A health crate clears it. Zap on each teleport (`Game::zapStep`). Abducted.Colour tint. | done |
| Taunt*: table 0x95f1a8 filled by 0x596830 (docs/w4m/audio.md §12) | WAE_* on Input.TauntPressed (T key) | Taunt*: scene + WXAnimTaunt clip | done |
| Titter, WormBounce, Punch, FireDamage, Revenge | No sender: the 37 call sites of the `Acting.Trigger` constructor 0x4d3410 (the only writer of its vtable 0x82c3e4) push constants or the payload, taunt and ambient indices above, never these [disasm] | Never triggered | as W4M (dead) |

## Off-scene voices

| Voice | Source | Here |
|---|---|---|
| Punch | MeleeWeaponLogicEntity 0x568270: a Fire Punch (kMeleeFirepunch) says "Punch" instead of "WeaponFired" | main.cpp onEvent Fire |
| Revenge | lsd and wiki only. W4M never dispatches trigger 0x1d and has no Revenge movie. | Never said. |
| EnemyDeath, CrateDrop, ShallowDrown | lsd, no movie | As before |
| FriendlyDeath, Victory | In the movies | main.cpp onEvent always plays them (on explosion, at end of match). The movies mute them to avoid a duplicate. |

Only one line plays at a time, with 0.9 s between two (choice). New categories imported (`tools/w4m-import` `VOICES`): revenge, punch, damageb (DamageInflictedB), nodamagea, nodamageb, maxdamage, pointandlaugh.

## Particles (PARTTWK.XOM)

Speeds are given in units per 20 ms frame. This unit is assumed.

| Emitter | Data | Here |
|---|---|---|
| WXP_WormThinking | WXSprite22 "?", origin (0, −4, 0) ± 4 at HatLocator, 1 per 950 ms, life 1000 ms, size 11 ± 3, speed (0, 0.3, 0) | Not emitted: only the Thinking movies request it, and they are never triggered. |
| WXP_Vomit_Fluid | WXSprite23, after 2000 ms, 3 drops, life 600 ± 200 ms, size 0.75, speed (0, 0.5, 3) ± (1, 0.2, 0.2), colour (0.9, 0.7, 0.4) → (0.7, 0.75, 0.2) | At VomitLocator, (0, −15, 2) units below HatLocator. Size is ×4 to stay visible (choice). |
| WXP_Sneeze | WXSprite23, after 100 ms, 8 drops, life 400 ± 200 ms, size 0.9, speed (0, −0.05, 0.5) | Same |

## Other animations

- **Ledge vault (Vaulting 0x5aca80).** The sim does it like W4M (docs/w4m/physics.md §11 "Vaulting"): `Game::vault`, 15 ticks at 10 m/s (4 units per 20 ms frame) toward the target, without collision, then the exact target. Releasing the stick or pushing it backward brings the worm back to the start; during the vault, no turning or jumping. The AI plays the same state (`Mover::vault`). The renderer no longer offsets the worm and plays `Vault` as soon as `g.vault` starts (event 9). As in W4M, only guns, utilities and the CanBeFiredWhenWormMoving weapons (Dynamite, Fire Punch, Landmine, Sheep) fire during the vault; those carry the walk velocity (§11 "Vaulting") [disasm + data].
- **High fall and head-first landing.**
  - **Jump window** [disasm + data]: kWE 2 (StartJump 0x5acd40) hides the weapon and plays `Jump_Start` (0.29 s) at weight 1 from t = 0 (0x5a36ed); the launch event (kWE 3 / 4 / 5 / 6) zeroes every body one-shot weight (0x5a1060) and plays its clip at weight 1 from t = 0: a cut, no blend. A one-shot past its end holds its last frame (0x5a01a0). Ours: `WormAnim::jumpT` while `Game::jumpDelay` runs (the current worm), `Jump_Start` ahead of every other clip; the next frame shows the launch clip.
  - **Fall.** When the fall speed exceeds 0.3 unit/ms (15 m/s, the FallDamage threshold 0x5ac3e0), W4M dispatches kWE 22 (0x5a3a60): `Skid` clip in flight, tumble mode, spin of 2π rad/s (angle += spin·0.02 per frame, 0x5a0593). kWE 22 keeps the +0x158 angle: it is 0 after a jump or a fall (0x5a01a0 resets it to 0 every frame), and the flight slope asin(vy/|v|) after an explosion (0x5a0617). The body rotates around the mesh origin (0x5a26d3), i.e. (0, 0.299, 0.352) m in worm.glb. The blast flight (Blastflight2) follows this slope too.
  - **Hard landing.** At vn ≤ −0.3, kWE 15 (0x5a3cc5) picks the recovery by angle (0x5a3dc1): before 3π/4 or after 7π/4, `RecoverFront1`; from 3π/4 to 5π/4, upside down, `RecoverBurried1` (head stuck in the ground, tail in the air, then it pulls free); from 5π/4 to 7π/4, `RecoverBack1` or `RecoverBack2` at random (0x68c0aa, here by hash).
  - **Recovery.** It plays to the end (state 5, 0x5a2c90).
  - **Example.** In free fall from rest (g = 12.5), the head-first landing happens between about 15.5 and 21 m.
  - **Here.** Everything happens at render time (`animateWorms` / `drawWorm`), from the sim's velocity. The hat and the held item follow the rotation. A small fall during recovery does not cut it, only a real takeoff of more than 0.2 s stops it (kWE 7, chosen duration). The 5 clips are added to `WORM_CLIPS`, and `worm.glb` grows to 14.6 MB.
- **Drowning (kWPS_DrownFloat, state 8, 0x5a06b0).** W4M does not hold FallDrown's last frame: it scrubs the clip with clamp(vy·10, −1, 1), vy in units/ms. At rest on the water, it sits mid-clip: the worm lying flat, hands on either side. Sinking, it is near the start; rising, near the end, where it rolls over. Here, vy is the derivative of the drawn sinking and bobbing. The mapping from −1..1 to clip time, 0..L, is assumed. Before, two errors put the worm upright in the water, hands above the body: the held last frame (worm upside down), and, since d2f99f8, an `else if` chain broken by the parachute code that replaced FallDrown with Fall or Wave.
- **Parachute (WAE_Parachute 0x58f180, ParachuteLogicEntity 0x578b77).**
  - The canopy plays `FireParachute` on opening.
  - Then the canopy and the worm play `ParachuteLR` at time 1 − lr.
  - lr = (3·lr + target)/4 per frame. The target is ±0.75 depending on the direction of sideways drift beyond 0.01 unit/ms; this direction is not checked.
  - `ParachuteWobble`, layered on top in W4M, is not stacked here: one clip per model.
- **Weapon in hand (WAE_*, docs/w4m/physics.md §11 "Weapon clips").** Render only (`main.cpp`: `heldModel`, `drawClip`, `windupClip`, `fireClip`, `WormAnim::drawT / wind`).
  - **Draw** [disasm]: the WEAPTWK Draw clip (DrawThrown for the five grenades) from t = 0 whenever the current worm in Aim gets its weapon back in hand: turn start, weapon change, standing again after walking or a jump (ActivateAccessory). The Aim clip is added at weight drawT / length.
  - **Hold** [disasm]: the Holding clip loops on its own clock from the Draw's end (`aimPose`: drawT − Draw length), with the Aim clip at the pitch time added over it. Ours: worm.glb exports `Aim*` and `Hold*` apart; `Models::draw`'s aim clip is added joint by joint over the body clip (`addLayer`: its offset from Base, translations summed, rotations composed instead of W4M's Euler sum, forced by the baked glb transforms), the joint graph rebuilt from the bone names (`parentOf`). The same layer adds Aim over Fire* and Windup*, and JetpackRotLR over JetpackFly.
  - **Weapon mesh** [disasm + data]: the held mesh plays the worm's current clip of the same name and time (`drawWorm`), else its `Rest` frame (Base or the stored transforms, w4m-models); Poison Arrow's bow plays Windup from the FIRE press and FireBow from the shot, the homing launcher AimLockHomingMissile from the lock, the umbrella DrawFlood; these one-shots hold their last frame (assumed). Super Sheep holds the Sheep mesh and HoldSheep (data); Bubble Trouble (BubbleTrouble mesh, DrawBT / HoldBT / FireBT / TauntBT), Icarus Potion (RedBull can, DrawRedbull / HoldRedbull / FireRedbull / TauntRedbull) and Skip Go (SkippingRope) are held. The jetpack pack and the worm play JetpackRotLR at WXWorm.JetpackLR + 1, lr eased to the sign of the yaw turn (ours: the sim's yaw step stands for 0x561e40's turn choice).
  - **Aim time** [disasm]: WeaponAngle / (π/2) + 1 s (was our pitch range stretched over the clip).
  - **Windup** while FIRE charges [disasm]: WindupThrown (grenades) / WindupBow (Poison Arrow) at the charge time (power × 1.5 s, Tweaks.MaxPowerUpTime), held at its end, Aim on the arms.
  - **Fire** [disasm + data]: the WEAPTWK Fire clip alone (Hold weighs 0) with the Aim layer: grenades FireThrown past a 0.6 s windup, else LobThrown, the windup frame fading out at 0.2 per 20 ms; Poison Arrow replays WindupBow (no worm ever plays an EndFire clip); Flood FireRainDance with the Flood.Weapon umbrella (its DrawFlood played once from the draw); Surrender Tantrum (flag dropped); strikes none (the radio is put away). Fatkins has no accessory: nothing in hand, no draw, no taunt, no EquipSfx.
  - **Sentry Gun team colour** [disasm + data]: the held mesh (WAE_Standard Init 0x590356) and the deployed turret (0x56b2d0, from NewSentryGunLogicEntity 0x56e9eb) both play `Red` / `Blue` / `Green` / `Yellow` by the owner team's `AlliedGroup` (TeamDataContainer +0x6c) 0 / 1 / 2 / 3. Each clip keys only `$animTex0` (type 0x1100) at 1.5 / 0.5 / 2.5 / 3.5: the child of the ball's `XChildSelector` (4 `XShape`s, one geometry, images `SentryGun_file4/1..4`). Ours: `teamClip` picks that clip (`Models::pick`) for `drawObject` and the held mesh, and the clip's own key gives the child (see the next item). Our team index stands for the AlliedGroup [ours: one team per alliance, as `TEAM_COLORS`].
  - **XChildSelector textures** [data + disasm, docs/w4m/formats.md §6 "XChildSelector"]: SelectedChild = trunc(max of the values the playing clips key). Ours: w4m-models exports every selector (child images as materials, child 0's mesh as its own primitive) and each W4M clip's selector values at 60 fps (glb extras `sel`); `Models::draw` takes the max over the drawn clip, the aim clip, the `Models::pick` clip and, for the worm, Base, the acting gestures and the emote, then truncates. So the worm's eyelid frames (`$animTex1/2`: open, half, shut) now follow every clip and emote (Yawn shuts them, idles blink), as do its `$animTex0` mouth frames (Brake, Teeth, Titter, WiggleBrows; none exported). Checked on `--animshot` captures (Yawn, MineOn, Red..Yellow, AbductViolate).
  - **Composite clip names** [ours]: w4m-models names a layered export `A+B`; `Models` finds it by `A` (was: an exact match, so `TauntBazooka` and the other taunts, the `Frown` / `Angry`... emotes and the UFO's `AbductViolate` / `AbductOpenDoors` / `AbductEnd` were never found and did not play).
  - **Not done**: Starburst's Aim weight easing to 0 during FireStarburst.
- **Objects on steep slopes (sim).** On the ground, beyond 60° (WXWorm.SlideAngle_Default), an object follows the same law as the sliding worm (`wormBody`): gravity along the slope, then SlideFriction 0.95 per 20 ms frame (0.9582 per tick). It is deterministic. Test: `sim_check` (`checkTunnelling`, 70° and 30° slopes).

## Limits

- `SpawnAccessory` is not done: the `Particle.WXPMesh36` antenna of the Abducted20c/30a movies, and `WXM_Telepath` (an object clip).
- Clips named but missing from the file: PointAndWave, ShakeFist, CoverHead, WaveANdPoint, ANgry. They play nothing, and W4M does not find them either (assumed: case-sensitive).
- The ambient cycle guard (0x5b3280, [esp+0x30]) is not decoded.
- An `Interesting` track is never cast (no object tagged), so Idle200a does not play.

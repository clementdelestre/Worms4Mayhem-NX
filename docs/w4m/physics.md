# W4M worms: physics states, collision, animation

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 5. Worms: physics states and ground collision
### Physics state enum kWPS_* (data: pointer table at file off 0x50b0f4..., order)
0 Ambulatory, 1 DetectJump, 2 Ballistic, 3 Sliding, 4 Vaulting, 5 Override, 6 Passive, 7 DeathThroes, 8 DrownFloat, 9 Undefined.
Confidence: data (table order) + disasm (UpdateWalking writes 4 on "->Vaulting", 6 on passive; 7/8 skipped as dead).
Stored at pData+0xF0 (pData = per-worm physics data arg passed to Update* funcs; disasm).

### WXWormLogicEntity functions (disasm, via debug strings)
- 0x5a5d30 / 0x5d59c0: read WXWorm.SlideAngle_Default/_Slippy, SlideFriction_Default/_Slippy, JumpHeight/Distance
- 0x5a6af0 EstablishPhysicsState; 0x5aa7f0 ChangeState (logs "Worm Sliding"); 0x5aaac0 Activate (spawn placement: partial X/Y, left/right up/down probes)
- 0x5ab3d0 CalculateGlobalControlInput; 0x5a9880 reads Worm.Walk.Speed; 0x5ac3e0 Worm.FallDamageRatio
- 0x5acba0 Fall (->Ballistic); 0x5acea0 Rebound; 0x5ad010 ImpulseWorm; 0x5adcf0 MsgOverridePhysics; 0x5aea10 UpdateBurning
- 0x5aecb0 UpdatePassive; 0x5afbe0 UpdateSliding (-> Ambulatory + kWE_Landed); 0x5b0da0 UpdateWalking; 0x5b1d60 Update (dispatcher)
- 0x5c02c0 reads Worm.BounceMultiplier(Default), Worm.SlideStopVel(Default)
- Worm.StepUpHeight and Worm.WalkOffCliffVelMulti: NOT present as strings in the PC exe (data). The walk-off Fall 0x5b14c7 passes Velocity = InputImpulse (ebp = pData+0x68), no multiplier [disasm].

### Worm land probe object (WXWormLogicEntity+0x24) (disasm + data)
- Init 0x59ef40(points, flags, edgePairs, nPoints<=16, nEdges); worm instance built in 0x59f1e0 with points 0x91ffc8 (8), edges 0x91ffc0 (3).
- Points (x,y,z, Y up, worm-local, NOT rotated by facing as far as seen): foot tripod (4,0,-3) (-4,0,-3) (0,0,5) (0,0,0); head copies at y=20.
- Edges: (0,4) (1,5) (2,6) = three vertical rods 20 high. The centre point (3/7) is not an edge.
- 0x59ec70 CastRays(start, dir, ?, maxLen, mask): for each point bit in mask, ray from start+P[i] via 0x466ae0 (land ray); stores per-ray hit dist (+0x18c+4i), hit flag (+0x1cc+i), hit point (+0xc+12i), nearest index (+0x1dc, 0x7f=none), nearest dist (+0x1e0), hit count (+0x1e4). Returns hitcount>0.
- 0x59edf0 Fits(pos): for each edge, segment pos+P[a] -> along (P[b]-P[a])*0.01, len 100 (= full rod) via 0x466ae0; any hit -> false. ONLY 3 thin vertical rods are tested: no volume, no sphere, no centre rod.
- 0x59ec00 HitPointRel(i|0x7f=nearest) = hitPoint - P[i]; 0x59ef10/0x59eee0 material/surface ids of hit; 0x59ef90 ground normal from hit points (needs >=2 hits).

### State dispatch (disasm: Update 0x5b1d60, switch on pData+0xF0, jumptable 0x5b2248)
0 Ambulatory -> 0x5b0da0 UpdateWalking | 1 DetectJump -> 0x5aefa0 | 2 Ballistic -> 0x5af430 | 3 Sliding -> 0x5afbe0
4 Vaulting -> 0x5aca80 | 5 Override -> 0x5ab3d0 (control input only) | 6 Passive -> 0x5b0c20 | 7 DeathThroes -> 0x5aa080 | 8 DrownFloat -> 0x5aa130 | 9 Undefined -> log.
ChangeState = 0x5aa7f0(pData, newState). 7/8 are sticky (walking never overrides them).

### Land ray primitive 0x466ae0 (disasm)
(start, stepVec, ?, nSteps:int, flags...) casts nSteps × stepVec; results in globals: 0x952cf8 hit dist (in steps, float), 0x952cfc hit step (truncated; > nSteps = miss), 0x952d4c hit point, 0x952ce8/0x952c64 surface ids. It samples each lattice segment twice or so (below), so a feature thinner than half a segment can be skipped [disasm].
- The worm's rays are not quantized [disasm]. CastRays 0x59ec70 calls 0x466ae0, which walks the land frames (0x473190) with the lattice march 0x468490, then the heightmap 0x462010.
  - 0x468490 samples each lattice segment from t_seg − 0.05·step, in steps of half the segment (0x468846..0x468893); the first sample of a ray is its start.
  - On a solid sample it bisects 5 times between the last empty and the solid sample (0x468ae3..0x468b86; CastRays passes the refine flag 1 at 0x59ecf7). tHit is the last empty sample (0x468b75).
  - 0x952cf8 = tHit / step length, a float (0x468c45); 0x952cfc = its truncation (0x468c1d), used only to test the step count; 0x952d4c = the point at tHit (0x468c89..0x468d1b). The heightmap stores the same way (0x4626c0..0x462752).
  - CastRays keeps that float as the ray's distance (0x59ed34) and that point as its hit (0x59ed4e) when 0x952cfc ≤ nSteps (0x59ed25). Ballistic counts a hit when the float distance is ≤ 20 ms (0x5af4fd).
  - The truncated hit (0x469690: `fistp` at 0x469f6f, hit = start + index × stepVec) belongs to 0x466a80's march (0x473270), called only from 0x56a430, not by the worm.
- So a hit is the last empty sample: on the air side of the land, within 1/32 of a sample step of the crossing [disasm]. Ballistic's contact then Fits (rods from the feet up, 0x59edf0) [reasoning].
- A ray whose first sample is in land hits at step 0, the start; both records 0x952d34 / 0x952d40 hold it (0x468bb1, 0x468a10) [disasm].
- UpdateWalking's foot rays (0x5b10a8): stepVec = (0, −1, 0), one unit (stored at 0x5b0db8..0x5b0dd0), 200 steps from cand + 20 units [disasm]. d = 20 − the float distance: a walking worm stands 0.1 unit over its land, plus the bisection's offset [disasm + reasoning].

### Slide thresholds (disasm 0x5a5d30)
0x92009c[0]=cos(SlideAngle_Default deg), [1]=cos(SlideAngle_Slippy); 0x9200a4.. Slide friction; 0x9200ac.. squared StartSlideVel/StopSlideVel (Default/Slippy). Also reads Gravity.
0x4adda0(normal, mat) = normal.y >= cos(SlideAngle[mat]) ("WormShouldNotSlipOnLand").

### UpdateWalking 0x5b0da0 algorithm (disasm; constants data)
1. If entity+0x54==0 (not pData+0x54, see §11) -> Passive(6). If 0x5ac390 -> UpdatePassive. If no move input (0x5ab3d0 false) -> idle branch: if standing surface id (ent+0xE8, 0xFFFF=none) no longer valid -> Ballistic(2); else zero vel, facing/anim upkeep.
2. disp = vel(pData+0x68)*20*k (k from 0x69db80, /0.004); cand = pos(pData+0x38)+disp.
   Facing (0x5b1040-0x5b107c): if |InputImpulse| != 0, Orientation (pData+0x8c) = (0, 0x519120(InputImpulse), 0), the yaw of the camera-relative input (0x5ab3d0): an instant snap, no turn rate. The worm moves along the input, not along its old facing.
3. Ground probe: CastRays(cand + (0,20,0), down, len 200, mask 0xF = 4 foot points). d = 20 - nearestDist = height of HIGHEST of the 4 hits relative to foot. Miss -> d about -181.
4. d > 20: blocked, no move (return).
5. 5 < d <= 20 (ledge higher than hardcoded 5.0 @0x8244fc = TWEAK StepUpHeight, which the exe never reads): cand = nearest hit point +0.1y; ground normal from rays; sphere resolve (0x519ed0) may replace cand; require 0x4adda0 walkable and Fits(cand); then store old pos (ent+0xEC), target (ent+0x104), timer 250 (ent+0x110), pos unchanged -> Vaulting(4) ("WormShouldNotSlipOnLand ->Vaulting").
6. -5 <= d <= 5 (normal step, @0x5b154a): cand = nearest hit +0.1y; sphere resolve. If sphere contact: n.y < -0.1 -> blocked; n.y <= 0.8 -> cand -= 15*n, cand.y += 10, resolve again, Fits -> Vaulting ("ResolveSphericalCollisions ->Vaulting"). No contact: walkable check, then Fits(cand) retried raising y by +1.0: y..y+4 accepted, y+5 rejected (§11 push-out) -> set pos; if ground n.y < cos(SlideAngle[mat]) -> Sliding(3).
7. d < -5 (drop/miss): cand.y = y-5, 0x51a510 sphere sweep; if contact and Fits -> place. Else Fits(cand at old y) -> Fall() 0x5acba0 (Ballistic). Else raise y by +1..+5 (y+6 rejected, §11 push-out) then place, else blocked.

### Vaulting 0x5aca80 (disasm)
See §11 "Vaulting 0x5aca80" for the full trace (start, abort, end, inputs).

### Sphere resolver 0x519ed0 (CollisionManagerService, worm sphere ent+0x28) (disasm)
Sets own bound sphere centre at pos (+radius in y unless flag), iterates contacts (fn ptr 0x95c25c). Contact 1: push out along contact normal to touching distance. Contact 2: solve 2-sphere intersection circle. Contact 3+ (jumptable 0x51a4f4 cases 2,3): give up, return the fallback/old position. Sphere-vs-sphere entities (worms, crates, drums, LandFramePseudoEntity), NOT the voxel land.

### Why worms can enter scenery in corners (assessment: disasm + reasoning)
- Body vs land = only 3 thin vertical rods (x=+-4,z=-3 and z=+5, 20 tall, Fits 0x59edf0); no centre rod, no width beyond 4-5 units, no ceiling/forward probe. A concave corner or overhang edge passing between rods is never seen.
- Push-out is vertical-only (+1 unit, max 6 tries); no lateral push or iteration toward a free direction.
- Ground height = MAX of 4 downward rays from 20 above; the start point can already be inside an overhang (ray starts in solid -> treated as hit at 0 -> d=20 boundary).
- Vault target accepted from a single Fits test then interpolated with no further checks; sphere resolver gives up after 2 contacts.
- Ray is step-sampled (integer steps), can skip thin features.

## 11. Worms: physics state handlers and animation (extends §5)

Tags: **data** = read from the exe or the tweaks, **disasm** = read from code, **assumed** = inferred, not verified.
Units: the logic step is 20 ms. Velocities are in units/ms and accelerations in units/ms². A worm is 20 units tall (the land probe rods, §5). Angles are in radians.

### pData = `WormDataContainer` (data: `pe.py schema '^WormDataContainer$'`)

The `pData` argument that every `Update*` receives is the serialised `WormDataContainer`. The field names below come from its schema:

| off | field | used as |
|---|---|---|
| +0x38 | Position | pos |
| +0x50 | Velocity | ballistic or slide velocity |
| +0x5c | Aftertouch | air-steer velocity, added to Velocity when moving but never accelerated |
| +0x68 | InputImpulse | control input vector (`CalculateGlobalControlInput` 0x5ab3d0) |
| +0x74 | Acceleration | gravity (+ wind), set by 0x5a6d20 |
| +0x80 | SupportNormal | ground normal, written on landing |
| +0x8c / +0x90 | Orientation (vec3, yaw at +0x90) | facing |
| +0x98 | AngularVelocity | – |
| +0xe8 | PhysicsOverride | – |
| +0xec | Flags | bit0 = aftertouch (air control) on; 0x40 = skip the next fall damage once; 0x800 = no fall damage; 0x8040 / 0x8840 = no slide or hard-land on landing; 0x1000 = jump disabled; 0x20 = see 0x5ac390 (all disasm, meanings assumed from use) |
| +0xf0 | PhysicsState | kWPS_* |
| +0x114 | LogicAnimState (u32) | no gameplay writer in the exe: only the XOM default 0 (0x64cc5f). Lua sets it to 10 (countingsheep-w3d FunkySheep, helterskelter-w3d LookBaddie1-3). Its only reader, HudActiveWormArrowEntity 0x5ef6b4, skips the active-worm arrow update 0x5ee7f0 while it is 10 [disasm+data] |
| +0x120 / +0x122 | SupportFrame / SupportVoxel | written on landing (Ballistic, Sliding) |
| +0x124 | Active | cleared when DeathThroes / DrownFloat ends |
| +0x12f | IsAfterTouching | – |
| +0x130 | MovedByImpulse | tested by ImpulseWorm |

Correction to the existing "UpdateWalking" text: the "no physics owner → Passive" test reads **entity** +0x54, not pData (`this` = esi at 0x5b0dd4). This flag is non-zero for the worm in control (assumed). Every other worm is put into Passive (6).

Entity (WXWormLogicEntity) fields seen (disasm):

- +0x20: the event sink (WXWormGraphicEntity), queue at +0x178 / +0x184.
- +0x24: the land probe. +0x28: the collision sphere.
- +0x44: the surface material index (0 Default, 1 Slippy).
- +0xdc: a "force hard land" flag.
- +0xe8: the support surface id (0xFFFF = none).
- +0x114 / +0x118: jump timer and jump kind. +0x11c: slide time.
- +0x120 / +0x124: slide spin and its target.
- +0x128: death timer. +0x12c: stuck counter.
- +0x244: the last queued event.

Cached data handles (0x5a9880, data):

| off | key |
|---|---|
| +0xa4 | DamageGraphic.Amount |
| +0xa8 | DamageGraphic.Position |
| +0xac | Water.Level |
| +0xb0 | Worm.Drown.HeightOffset |
| +0xb4 | Worm.WeaponDisableMovement |
| +0xb8 | Worm.VelocityScale |
| +0xd4 | Worm.Walk.Speed |
| +0xd8 | WormPot |
| +0xbc.. | AimMouseLR, AnalogueAimLR, Zap.* |

#### Integration and gravity (disasm)

- **Integrate 0x5a6e90(pData)**: `pos += (Velocity + Aftertouch)*20 + Acceleration*200`, then `Velocity += Acceleration*20`. It is an exact constant-acceleration step with dt = 20 ms. **No drag and no terminal-velocity clamp.**
- **SetAcceleration 0x5a6d20**, called from ChangeState 0x5aa7f0 and from 0x5a9ac0:
  - `Acceleration = (0, Gravity × Low.Gravity.Multiplier, 0)`;
  - plus, when `WormPot.WindAffectsWorms` (+0x53) is set, `Wind.Speed × (cos Wind.Direction, 0, sin Wind.Direction) × WormPot.WindScale` (wind 0x5a6040).
  - Values: Gravity −0.00025 (WEAPTWK); Low.Gravity.Multiplier 1.0 (LOCAL), 0.5 when low gravity is on (`Low.Gravity.OnValue`, TWEAK). Data.
- **Air steer 0x5a6fe0**, run each Ballistic frame when Flags bit0 is set:
  - `Aftertouch += InputImpulse × WXWorm.AftertouchDelta`;
  - then `|Aftertouch|` is clamped to `WXWorm.AftertouchStrength`.
  - Values 0.015 and 0.1 (TWEAK). Globals 0x95fb70 / 0x95fb74 (squared) / 0x95fb78.

#### Jump launch values (loader 0x5a5d30, disasm + data)

| global | value | formula | default |
|---|---|---|---|
| 0x95fb94 | jump vy | sqrt(−2·g·JumpHeight) | 0.15811 (H 50) |
| 0x95fb90 | jump vx | JumpDistance / (−2·vyJump/g) | 0.063246 (D 80) |
| 0x95fb8c / 0x95fb88 | forward-flip vy / vx | sqrt(−2·g·ForwardFlipHeight); ForwardFlipDistance / (−2·**vyJump**/g) | 0.2 / 0.031623 |
| 0x95fb84 / 0x95fb80 | backflip vy / vx | same, with Backflip* | 0.2 / 0.031623 |
| 0x95fb7c | vertical jump vy | sqrt(−2·g·VerticalJumpHeight) | 0.18708 (H 70) |

Exe quirk (disasm, at 0x5a5f3f and 0x5a5fac): the flip horizontal speeds divide by the **normal jump's** air time (1265 ms), not the flip's own (1600 ms). Flips therefore travel about 50.6 units, not the 40 that the tweak says.

`Worm.Jump.Forward/Backward/Backflip/Upward` (TWEAK vectors) are read only by AI code 0x4af3f0, not by the worm physics. `Worm.MaxSlope*`, `Worm.WalkAnimSpeedScale`, `Worm.CreepAnimSpeedScale`, `Worm.HopTest.*` and `Worm.Hop.Velocity` have no string in the exe, so they are unused (data).

### Physics state handlers (disasm unless tagged)

#### StartJump 0x5acd40 → DetectJump (1)

- Runs at the end of every walking step.
- If the jump button is pressed (entity +0x31 bit0) and Flags & 0x1000 is clear:
  - QueueEvent(2 = Jump_Start);
  - jump window = **300 ms** (+0x114 = 0x12c);
  - kind = 2 ("held") (+0x118 = 2);
  - state → DetectJump (1). Coming from Vaulting, the position first snaps to the vault target.
- Nothing else is tested: no Fits 0x59edf0, no head or wall probe, in StartJump or DetectJump 0x5aefa0 (no such call in either) [disasm]. In state 7 / 8 (DeathThroes / DrownFloat) the event is queued but the state is kept (0x5acda8) [disasm]. A worm against a cliff therefore jumps; the cliff is met in Ballistic, where a head-point ray hit Rebounds on the land normal, so a wall turns the horizontal speed only [disasm + reasoning on the Ballistic land-hit path below].

#### DetectJump 0x5aefa0

Each frame:

- While kind is 2, releasing the button (entity +0x31 bit1 cleared) turns it into kind 0 and records the hold time.
- A second press during the window (bit0) turns kind 0 into kind 1.
- The window counts down by 20 ms. When it ends, the launch depends on the kind:

| kind | condition | launch | event |
|---|---|---|---|
| 0 (tap) | – | facing × JumpVx, vy = JumpVy | 3 Jump |
| 1 (double tap) | input·facing > 0, or input zero and entity +0x159 set | facing × FwdFlipVx, FwdFlipVy | 5 Fwdflip |
| 1 | otherwise (back or no input) | −facing × BackflipVx, BackflipVy | 4 Backflip |
| 2 (held all 300 ms) | input·facing > 0 | normal jump | 3 |
| 2 | otherwise | (0, VerticalVy, 0) | 6 vertical jump |

- input is 0x5ab3d0's camera-relative stick, dotted with the facing from Orientation (+0x90); DetectJump only reads Orientation, so the worm does not turn in the window [disasm].
- +0x159 [disasm + data]: 1 at reset (0x5ac6b6) and on Activate (0x5aaca2); 0 on `Input.JumpBackPressed` / `Released` (0x5ac957 / 0x5ac982), which only the jump key sends, and only when the PC control option `FETXT.Control.Backflip` (InputDetails Type 7 kIM_BackFlip, Key at +0x18) is "Backwards" (0x4deea0; toggle 0x748466, Key 0 "Forwards", 1 "Backwards"). DEFSAVE.XOM sets Key 0 for both FETXT.Control.Backflip and FETXT.Joypad.Control.Backflip, so by default a double tap with no stick is a forward flip; a backflip needs the stick against the facing.
- Every launch then does: Velocity = launch, Aftertouch = 0, Flags |= 1 (air control on), state → **Ballistic (2)**.
- A second variant runs when global 0x95a100+0x9b bit 0x40 is set and entity +0x15a == 0 (assumed: analog control). It scales the tap jump's horizontal speed by the input magnitude (quantised to 1/4) and by hold/300 ms (1/8 steps).

#### Ballistic 0x5af430 (kWPS_Ballistic = 2, "flying")

1. If Flags bit0 is set: read the control input, then air-steer (0x5a6fe0).
2. Cast a parabola from pos: `CastRays(pos, Velocity+Aftertouch, Acceleration, 40 steps, mask 0xFFFF)`, i.e. all 8 probe points. Land ray 0x466ae0 marches `p(t) = p0 + v·t + ½·a·t²`, so t is in ms. A hit counts only if t ≤ **20** (this frame). The 40-step cast looks one frame ahead. Support id = 0xFFFF.
3. **No land hit this frame** (0x5af95d):
   - Integrate.
   - If Velocity.y went from > −0.3 to ≤ −0.3 and no flags 0x8840 and entity +0xdc == 0: QueueEvent(22) ("now falling": tumble anim).
   - Sphere resolve against entities (0x519ed0):
     - **contact**: if Fits, accept the position and stuck counter −1. Then if contact n.y < 0.8: Rebound(n) + event 23 (Thud). Otherwise **land on the object**: event 8 (15 if +0xdc), state → Ambulatory, support id stored. If not Fits: stuck counter +2, Rebound + event 23.
     - **no contact**: Fits → move, else stuck +2 + Rebound.
     - On a Fits failure pos is restored to the pre-Integrate position and the rebound normal is normalize(old − new) (0x5afa78, 0x5afb17) [disasm].
     - Stuck counter ≥ 20 → force land (event 8 or 15, Ambulatory) where it is, support id 0xFFFF: the idle walk branch (0x5b1a3e) only falls again for an entity support, so a worm forced to land in the air stays there until it walks [disasm]. The counter (entity +0x12c) is never reset in Ballistic; only Sliding's Landed paths clear it (0x5b05c2 / 0x5b063b) [disasm].
4. **Land hit**:
   - candidate = hit point − probe offset, sphere resolve, ground normal n (0x59ef90).
   - Rays and normal, traced [disasm]:
     - CastRays 0x59ec70 casts one land ray (0x466ae0) from pos + P[i] for each mask bit; the nearest is replaced only on a strictly smaller distance, so a tie keeps the lower index (a foot before a head). No hit is filtered by its normal.
     - 0x59ef90 with 2 or more hits: the sum of the land normals (0x59eb30 re-select, 0x482010, normalized) of the hits whose `hit − P[i]` lies strictly within 1 unit (squared distance < 1) of the nearest one's, the nearest included, then normalized. With 1 hit: that hit's normal. The jetpack (0x562ecd) uses the same function.
     - The per-hit land normal is 0x482010 → 0x4732f0 → 0x46a070 on a land frame, or 0x462910 for support 0xFFFF (heightmap) [disasm].
     - 0x46a070, traced [disasm]: it takes the last empty sample (0x952d34) and the solid one (0x952d40), both in lattice cell space (0x468b71 / 0x468b53), finds both cells and picks the face of the hit cell the ray came in by: the shared face when the two cells differ on one axis; otherwise per-axis entry parameters over the faces whose neighbour cell is empty (0x43e0d0 tests the neighbour's voxel bit). It builds 3 points on that face, at the hit's two in-face coordinates and +-0.1 cell along each, maps them through the deformed lattice (EdgeOffset1/2 of the y plane, the heightmap on the top plane, 0x46b120..0x46b3a0), and returns their cross product divided by the frame scale (+0x12c..+0x134) and rotated by the frame matrix (+0x90..+0xb0) into 0x952d64, which 0x482010 returns unnormalized; 0x59ef90 normalizes (0x4453c0). So the normal is the flat plane of one lattice face: no vertex interpolation, no smoothing, and at a wall's foot or lip it is the face crossed, never a blend of the wall and the floor.
     - A ray starting in land (both records on the start) [disasm]:
       - 0x467dc0 passes, d becomes the zero vector (0x46aa3e) and branch 0x46ab9c runs. Each face of the start cell gets the start's fraction to it, or −1 when its neighbour cell's voxel bit is set (0x46abfa..0x46ad72).
       - The least non-negative wins; ties go to the order −x, +x, −y, +y, −z, +z (0x46ad80..0x46af50). With all six neighbours solid, the normal is −(hit velocity), i.e. −dir (0x46b4b7..0x46b527).
       - So that normal does not depend on the ray's direction.
       - Ours: `Terrain::startNormal`, the nearest plane of the start's W4M cell whose outside is empty, else −dir. The distance stands in for W4M's cell fraction [assumed]. A start in an unlisted solid cell takes −dir [ours].
     - Ours: imported maps keep these cells exactly (`<map>.cells`, formats.md §22) and `Terrain::cast` returns the plane of the face entered, read in the cell where the ray meets it: of the deciding cell's planes, the one the ray crossed last (least depth over approach speed), not the nearest, which a ray running along an inner face between two cells would pick [ours, same geometry]. On a W4M-driven walk over 8 maps, foot normals against the face W4M enters: mean error 0.04°, walkable test (n.y ≥ 0.5) wrong on 39 of 283 121 rays (docs/sim.md "Movement against W4M, measured"). Maps without cells fall back to the field's gradient over ±VOX/4 [ours].
   - The hit branch never Integrates: Rebound and the landing use the frame's starting Velocity [disasm: 0x5a6e90 is called only on the no-hit path 0x5af98f].
   - If not Fits: stuck counter +2; at ≥ 20, force land (no Rebound, air control kept). Otherwise clear air control and Rebound(n) with n = normalize(pos − candidate) then event 23, or, when pos − candidate is zero (0x445310 against the zero vector 0x96e878, epsilon 1.19e-7), Velocity = −Velocity with no event (0x5af8cb). pos is not moved.
   - A bump between the probe points is seen by Fits only: the 8 rays start at the feet and the heads, while Fits walks each rod in 100 steps of 0.2 unit [disasm 0x59edf0]. A jump into it is undone (stuck +2, Rebound on normalize(pos - candidate)) in W4M as in ours: on Clean-w3d (17.1, 46.7, 38.2) facing -z our rod sample 0.75 m up is 2.9 cm inside W4M cell 263 before the jump and 4.4 cm after it [data: scratch w4m-maps probe], so W4M's rods fail there too.
   - A slot narrower than the 8-unit tripod (x ±4, z −3 / +5) is caught here: a foot point hits a lip within the frame and the worm lands on it, standing on that foot; the stuck counter is not involved. A slot 8 units wide or more lets all three Fits rods in, and the worm drops in [disasm + reasoning on the probe geometry 0x91ffc8].
   - If Fits: pos = candidate, SupportNormal = n, SupportFrame/Voxel stored.
   - If the nearest hit is not a foot point (0x59ec50 returns the per-point flag of 0x91ffb8: 1 for the 4 feet, 0 for the 4 heads [disasm + data]) → Rebound + event 23.
   - If n.y < **0.2** → Rebound + event 23 (wall).
   - Otherwise it is a landing. With v = Velocity + Aftertouch: `vn = v·n` and `vt = v − vn·n`. When `|vt|² < vn²`, `|vt|²` is halved before the slide test.
     - Walkable (n.y ≥ cos SlideAngle[mat]) and `|vt|² < StartSlideVel²[mat]`, or Flags & 0x8040 → **land walking**:
       - if `vn ≤ −0.3` (hard) and entity +0x64 == 0 and no flags 0x8840: event 15 (hard land: poof + recover); otherwise event 8 (soft Land, param vn). If entity +0xdc is set, the threshold is 1000, so always 15.
       - if `vn < −0.3`: FallDamage(vn) and Velocity = 0. Otherwise Velocity = (vt.x, 0, vt.z).
       - state → **Ambulatory (0)**, support id stored.
     - Otherwise (steep, or too fast) → **slide**: FallDamage if vn < −0.3, Velocity = vt, event 17, state → **Sliding (3)**.

#### Vaulting 0x5aca80 (kWPS_Vaulting = 4) and its start in UpdateWalking

Start, ledge path 0x5b1209-0x5b128f (5 < d <= 20 units) [disasm]:

- target ent+0x104 = nearest foot hit + 0.1 y, after the sphere resolve; it must pass the walkable test 0x4adda0 and Fits.
- ent+0xF8 = InputImpulse (pData+0x68) at that frame; ent+0xEC = pos; timer ent+0x110 = **250** (0xfa); event **9** (Vault clip); material ent+0x44.
- pos is not moved that frame, and this path returns without StartJump.
- The sphere-contact path 0x5b165f-0x5b1746 sets the same fields, then calls StartJump 0x5acd40 the same frame (0x5b183a): a jump pressed on that frame snaps pos to the target and goes to DetectJump.

Each frame in state 4, 0x5aca80 [disasm]:

1. Reads the control input (0x5ab3d0).
2. If `input · ent+0xF8 <= 0` (stick released, or pushed against the start direction): target = old pos (ent+0xEC), ChangeState(Ambulatory). ChangeState 0x5aa847 copies the target into pos when it leaves state 4, so the worm drops back to where it started.
3. Else timer −= 20. At ≤ 0: ChangeState(Ambulatory), and pos = target (the same copy).
4. Else pos = (4·pos + target) / 5 (0x5a59f0(a, b, k) = (a·k + b)/(k+1), k = 4.0 at 0x858228): 1/5 of the way per 20 ms, 12 moves then the snap [disasm]. (Corrects an earlier "MoveTowards 4 units" reading.)

Consequences:

- A vault always lasts the full 250 ms: nothing ends it on arrival.
- No collision test runs during the move. Land that appears in the way does not stop it [disasm: no Fits, ray or sphere call in 0x5aca80].
- No Orientation write, no StartJump, no fire handling in the worm code: the facing is frozen and a jump press is ignored [disasm: the dispatcher 0x5b1fc0 calls 0x5aca80 only; StartJump's three callers are all in UpdateWalking].
- Every exit from state 4 through ChangeState (blast via ImpulseWorm, death, physics override) snaps pos to the current target first [disasm 0x5aa847]. UpdateWalking's Passive switch (0x5b0df9) and StartJump (0x5acdba) do the same copy inline.
- **Firing during the vault**: refused for most weapons. FirePressed handlers call a CanFire before firing:
  - BaseWeaponLogicEntity 0x54a2d0 (vtable slot at 0x857b34, also Flood's 0x85919c) and PayloadWeaponLogicEntity 0x583ca0 (called from the FirePressed handler 0x586092 and from 0x586e9f) both return `PhysicsState == 0 || CanBeFiredWhenWormMoving` [disasm].
  - That flag is BaseWeaponContainer field 0x0c at +0x79. In WEAPTWK it is 1 only for Dynamite, FirePunch, Landmine, LandmineCluster and Sheep; every other weapon, and every utility, has 0 [data].
  - Vaulting (4), DetectJump (1), Ballistic (2), Sliding (3) and Override (5: rope, parachute, jetpack) therefore refuse every other weapon. The secondaries dropped from a tool (Dynamite, mines, Sheep) are exactly the flagged ones.
  - GunWeaponLogicEntity's check 0x55cd30 returns 1: guns fire in any state [disasm].
  - The jetpack's FireUtil handler 0x562270 tests `state == 0` (0x5623b4) only for PackAccessory.Trigger, the take-off [disasm]. The rope fires on Input.FireUtilPressed (handler 0x574730 → 0x573790; its Input.FirePressed handle is only subscribed, at 0x57096e) [disasm]:
    - there is no CanFire and no `+0x79` test;
    - `state == 0` is tested at 0x573845 only to play `global/FEError` when no "Head" target is found, at most every 500 ms (+0x13c);
    - so the rope fires in any state, in the air included.
  - Only the press is gated: a charge started on the ground goes on to its release. PoweredWeaponLogicEntity 0x586e40 calls CanFire 0x583ca0 on FirePressed only (0x586e9f); its FireReleased branch (0x586f72) tests just the charging flag +0x34 and posts Weapon.LaunchPayload [disasm]. Ours: `Game::step` keeps charging while `power > 0` without `fireable()`.
- **Velocity during a vault** [disasm]:
  - UpdateWalking sets Velocity = InputImpulse on a successful step, through 0x546f10 at 0x5b146e (drop path) and 0x5b19c8 (normal step); the idle branch zeroes it (0x5b1c1b).
  - The vault start does not write it, and nor does 0x5aca80. So during a vault Velocity is the last walk step's velocity.
  - The launch (0x585a1b: `state == 0`) counts the vault as off its feet: a flagged payload with IsLaunchedFromWorm (+0x1cf; 1 for Dynamite, Landmine, LandmineCluster, Sheep) inherits it at 0x585a58.

Ours (sim.cpp `walkStep` / `vaultStep`, `Game::vault`; ai.cpp `Mover::vault`):

- A climb above `STEP` (5 units) up to `STEP_UP` (20) starts the vault instead of the instant climb, if the target is walkable and Fits. The walkable test uses the ground normal at the target, the 0x59ef90 mean below; the toe and plain climbs share it [ours, from 0x5b11ca / 0x5b11f0].
- 15 ticks (`msTicks(250)`), each moving 1 − 0.8^(DT/20 ms) of the way to the target, then the snap.
- The input is the heading stick (HEADING) or the facing, times `walk`; `dot <= 0` drops it back to the start.
- During the vault: the yaw is frozen, JUMP is ignored, `stepWorm` skips the body.
- Leaving it otherwise: knocked or roped snaps to the target; moved by a weapon (teleport) ends it in place [ours].
- `Game::fireable`: utilities and guns always fire; Dynamite, Fire Punch, mines and Sheep always fire; anything else needs a grounded worm, not sliding, not vaulting, no jump pending. It is asked on the press, or on the first tick of a charge [ours, from 0x54a2d0 / 0x583ca0].
- `Vault::vel` keeps the walk velocity of the vault's start tick. A payload fired during the vault gets the carried offset and that velocity, like a worm off its feet. W4M's Velocity there is the last successful step's InputImpulse (0x5b146e / 0x5b19c8), 0 after an idle frame (0x5b1c1b); ours keeps it in `Game::walkVel` the same way [disasm]. InputImpulse = camera-relative stick × 1/20 unit/ms (2.5 m/s at full stick, 0x5ab5f3), whatever the walk speed: the step moves it × 20 × VelocityScale × Walk.Speed / 0.004 (0x5b0fb6..0x5b0fec) [disasm + data].
- Phase change (control lost) acts as no input: back to the start.

#### UpdateWalking: support, push-out and the uphill rule (disasm)

- **Support**: the 4 foot rays (mask 0xF: tripod (±4, −3), (0, +5) and the centre, world axes, 0x91ffc8 [data]) are cast from 20 units above the candidate; d = 20 − nearest, i.e. the **highest** hit carries the worm. One foot on a lip is enough: walking over a slot narrower than the tripod never drops into it.
- **Normal step push-out** (0x5b194c): Fits is tested at y, y+1, …, y+5 units. Only y..y+4 are accepted: the counter starts at 5 and a success with the counter at 0 is rejected (`test ebx, ebx; jbe` at 0x5b198b). So the step raises the worm by at most 4 units.
- **Drop path push-out** (0x5b14e1): y+1 is added before the first test, so y+1..y+5 are accepted and y+6 is rejected.
- **Blocked**: both failures jump to 0x5b1a21, which still calls StartJump. A blocked worm can jump.
- **Uphill rule** (0x5b1920-0x5b1946): if `normal · (cand − pos) < 0`, the ground under the candidate must pass the walkable test 0x4adda0, else blocked. The vector is cand − pos: 0x454e00(out, a, b) computes out = a − b, and 0x5b18f0 passes (out = esp+0x4c, a = cand at esp+0x20, b = ebx = pData+0x38 = pos). The normal is the ground normal of the candidate (0x59ef90 output) [disasm]. After the move, n.y < cos SlideAngle → event 17 and Sliding (0x5b19d0).

- **Ground normal 0x59ef90** [disasm]: it needs 2 or more hits (+0x1e4 > 1). It sums the land normals (re-cast 0x59eb30, normal 0x482010, normalized 0x4453c0) of the hits whose `HitPointRel` differs from the nearest hit's by at most 1 unit, i.e. the feet level with the highest one, then normalizes. So when two feet straddle a slot on its two lips, their normals average to near vertical.

Ours (sim.cpp `feet`, `walkStep`, `footing`):

- The centre and the tripod (±0.2, −0.15) (0, +0.25) m, world axes, carry the worm. Any one hit is enough, as in W4M. This applies to the grounded test [ours, from 0x91ffc8].
- The grounded test (`STANCE`) looks 2 units under the feet, as a W4M worm stands up to 1.1 units over its land (land ray, §5). It holds a worm that stood the tick before, or one put down at rest (spawn, teleport) [ours]; a flight lands only through its sweep (Ballistic, below), as 0x5af430.
- The normal is the mean over the feet whose hit lies strictly within 1 unit of the highest one, as float distances; feet with no hit are left out [disasm 0x59ef90].
- Each foot's normal is the face its down ray enters (from 20 units up for the walk and the slide, `feet`; from 6 units up for the grounded test, `footing`) in the exact land cells, our 0x46a070 [ours, same geometry; docs/sim.md "Exact land"]. A foot at a step's lip reads the flat top and a foot at a wall's foot the floor, as in W4M.
- The candidate (walk and Sliding's ground follow) is the nearest foot hit + 0.1 unit, from the 4 foot rays cast 26 units down from 20 units above (`feet`). Each hit is the exact crossing less 1e-4 m, on the air side as W4M's last empty sample [disasm 0x5b154a / 0x59ec00 / 0x468490; the offset ours]. d is a float, as W4M's.
- A ray starting in land hits at once: d = 20, a vault that must Fit 20 units up, else blocked [disasm]. A head wedged in land stays blocked (`checkWallStuck`).
- The walk follows W4M:
  - the candidate is set on the highest hit within 5 units;
  - the uphill rule `n · (cand − pos) < 0` needs a walkable n;
  - the step push-out tries +0..+4 units; the drop path (no land within 5 units) Falls at once from the old height when the rods Fit there (0x5b14c7), else tries +1..+5;
  - a step onto ground under SlideAngle starts Sliding from the same cast's normal (`walkStep`'s `ground`, 0x5b19d0);
  - nothing else moves the worm down: no settle after the step.
  - on a uniform slope the worm stands on its highest foot's hit, so the next candidate lies on the same plane and `n · (cand − pos)` is 0 up to rounding: the uphill rule only bites where the worm steps onto a steeper face from flatter ground (a first contact more than 1 unit up the face, where the floor feet no longer average in). A step it lets through onto ground over 60° starts Sliding with Velocity = InputImpulse, which follows the ground up the face until gravity and friction turn it back [disasm 0x5b1920 / 0x5b19c8 / 0x5afbe0 + reasoning]. Ours does the same: pushed into a 65-76° face the worm skids up and slides back (`checkW4MWalkRules`, `checkWallStuck`).
  No test covers the push-out: with W4M's probe, any lip low enough to be cleared by 4 units is seen as ground and vaulted instead [ours].
- **Fits is W4M's rods** (`rodsFit`, the same for the walk, the slide and the flight): the 3 rods, any land [disasm 0x59edf0]. [ours] Rods already in land where the worm is fall back to the relative ring test (`fits`: a 0.2 m ring at 0.7 / 0.95 m, no deeper than before), so a worm can still move out: land can appear around it (girders, terrain edits, spawns), which no W4M move leads to.

#### Rebound 0x5acea0 (disasm)

- `v = Velocity + Aftertouch`, then Bounce 0x518f40(v, n, e = **0.3**, tangential kept ×**1.0**, min speed **0.01**):
  - if `v·n < 0`: v = vt·1.0 − vn·0.3, and v becomes 0 when |v| < 0.01;
  - if `v·n ≥ 0`: v = n·|v|·0.3, or 0 when |v| < 0.01.
- Velocity = v, Aftertouch = 0, air control off.
- If v ends at exactly (0, ≤0, 0): when n.y > 0, event 8 + state → **Sliding**; otherwise Velocity = (0, −0.01, 0) and Integrate.
- Bounce, traced [disasm 0x518f40]: the reflect branch is taken for `v·n ≤ 0` (zero included); its result is zeroed when its length is under 0.01. For `v·n > 0` the input |v| is compared: under 0.01 → 0, else n·|v|·0.3.
- The stop to (0, 0, 0) is reached by any Rebound slower than 0.01 units/ms (0.5 m/s), e.g. the first undone fall of a worm at rest (−0.005 units/ms after one Integrate, rebounds to +0.0015). Sliding (0x5afbe0) never writes the support id (+0xe8), so that worm ends Ambulatory with support 0xFFFF and stays put [disasm].
- The constants are hardcoded. `Worm.BounceMultiplier`/`Default` (0.6/0.3) are only handled in ParticleHandlerService 0x5c02c0, which switches them by WormPot Sticky/Slippy. The worm code never reads them (data: no other xref).

#### FallDamage 0x5ac3e0(pData, vn) (disasm + data)

- None if Flags & 0x800. If Flags & 0x8040: bit 0x40 is cleared and no damage (one-shot immunity).
- Otherwise `damage = trunc((−0.3 − vn) × Worm.FallDamageRatio) + 1`, with Worm.FallDamageRatio = 100 (LOCAL). Then ApplyDamage 0x5ab7e0(damage, 1) and a controller rumble via 0x4bc410(0, 100, worm pos, 500, −1, −1) (disasm): 0x4bc410 forwards to RumbleService 0x4bbc40(Light 0, Heavy 100, pos, Duration 500 ms, FadeIn −1, FadeOut −1), motors as bytes 0..255 (Light is the weak motor, Heavy the strong, assumed from the names in its ' Light=' / ' Heavy=' log, 0x827440); the position is only printed in the log, so the rumble is not positional. No camera shake and no sound. Same helper as the weapons' RumbleLight/RumbleHeavy (docs/w4m/weapons.md BaseWeaponContainer 0d/0e).
- Jetpack contacts (JetpacUtilityLogicEntity update 0x562810, two paths) [disasm]:
  - **Land** (0x562e4b): its own WXVertexCollider (0x59f1e0: 8 points 0x91ffc8, feet (±4, 0, −3), (0, 0, 5), (0, 0, 0) flagged 1 at 0x91ffb8, the same 4 at y 20 flagged 0; rods (0,4) (1,5) (2,6) at 0x91ffc0) sweeps v over 20 ms (0x59ec70); the first point hit wins, a tie goes to the lower index, so a foot. A foot hit (0x59ec50) with the rods clear at the contact (Fits 0x59edf0) **lands the pack: v −= (v·n) n** (restitution 0, 0x562f72..0x562fb6), then OverridePhysics 0, PackAccessory.Hide, camera Default, super thrust 0. The worm goes Ballistic (0x5ae17a → 0x5aa7f0) with only the tangential speed, so FallDamage (vn < −0.3) cannot fire however fast the fall was. A head hit, or rods not clear: v −= 1.8 (v·n) n (0x5630dc), restitution 0.8. The contact is pos + v·nearest distance, the normal 0x59ef90 (0x562ecd) [disasm]. Ours: `jetBody` in client/src/sim.cpp uses the Ballistic `sweep` (docs/sim.md) and applies this, with no n.y rule.
  - **Objects** (no land hit, 0x56316a): the sphere resolver 0x519ed0 (worms, crates, drums, not land). Contact with n.y > 0 (0x563252, n.y at [esp+0x5c] = the resolver's normal + 4) ends the pack with v unchanged; Ballistic's object contact has no FallDamage (step 3 below: Ambulatory or Rebound). n.y ≤ 0: v −= 1.8 (v·n) n; rods not clear: v × −0.8 (0x5633e9).
  - Only running dry (0x562990) leaves v intact over land: the worm falls Ballistic and FallDamage applies. No jetpack function touches flags 0x40 / 0x800 (field 0xec: no access in 0x562810, 0x5624f0, 0x561810, 0x562270, 0x562180, 0x562130, 0x562530) [disasm]. Thrust while falling is not a brake to 0: 0.008 units/ms per update × (1 + super thrust) (docs/weapons-audit.md "Jetpack"); there is no fall-speed cap and no gravity scaling (Acceleration −0.00025 added each update, 0x562dc8).
  - Water [disasm]: no jetpack function tests the water; the worm's drowning check 0x5ad640 takes it from Override to DrownFloat with its Velocity, and the turn end it causes kills the pack in that frame (docs/w4m/turn.md §14 "Drowning the active worm"). Kill (0x5640bd) sends no OverridePhysics, PackAccessory.Hide or JetpackEnd; the task's cleanup releases the `weapons/Jetpack` instance (0x561b39), and the last release stops a playing event (XSoundInstance destructor 0x6fdde0 → 0x6fdfc0). The pack mesh, its JetpackFly / JetpackRotLR weights and the JetpackFire emitters go on `Worm.CleanUpOnDeactivate` (WAE_Jetpack 0x58d06c → 0x58c640).
  - Context only (community: worms.fandom.com Jet Pack, Steam guides "Worms 4 and Ultimate: tricks", "Weapon tricks in Worms UM"): a landing with the pack on is safe, falling with it off or dry hurts. The disasm agrees.
- The threshold is |vn| > 0.3, i.e. a free fall of more than `0.3²/(2·0.00025)` = **180 units** (9 worm heights).
- Examples: vn −0.4 (320 units) → 11 hp; vn −0.5 (500 units) → 21 hp.

#### Fall 0x5acba0(pData, vel, aftertouch, airControl)

Velocity = vel, Aftertouch = aftertouch, Flags bit0 = airControl, event 7, state → Ballistic.

Called when:

- walking off a ledge (0x5b14c7: walk velocity kept, Aftertouch 0, **air control on**). No 0.7 multiplier: the `WalkOffCliffVelMulti` string is absent;
- the slide drops off a ledge (0x5b02dc).

#### Sliding 0x5afbe0 (kWPS_Sliding = 3) (disasm + data)

Per frame:

1. If air control is on, read the input.
2. Slide time +20 ms.
3. Spin: rate +0x120 approaches target +0x124 (0x47a1a0, factor 3, max 2°/frame), and yaw += rate.
4. **Gravity along the slope**: Velocity += (g − (g·n)·n)·20, with n = SupportNormal.
5. Steering: with air control, v += InputImpulse × 20 ms × k, k 0.003 or 0.0005 (below).
6. **Friction**: Velocity ×= SlideFriction[mat] **per frame** (Default 0.95, Slippy 0.999).
7. Probe the ground under the next position: 4 foot rays, 300 steps; d = 20 − nearest.

| d | action |
|---|---|
| d > 5 (wall or step too high) | if \|v\|² < StartSlideVel²: Landed. Else ray along v (20 steps): hit → Rebound(n), spin target = (target + 3·(n×v)) / 2, stuck +2; no hit → Landed |
| d < −5 (drop) | v projected off the ground; if it fits → **Fall()** (Ballistic). Else Landed |
| −5..5 | pos = hit + 0.1y, sphere resolve (a side contact → Rebound + stuck +2). Fits → move, stuck −1, SupportNormal/Frame/Voxel updated. Then if the ground is walkable (n.y ≥ cos SlideAngle) and \|v\|² < **StopSlideVel²[mat]** → Landed. Not Fits → Landed |

- Stuck counter ≥ 20 → Landed (0x5b05cc), and the count is reset to 0 (0x5b063b). Every Landed path in Sliding resets it (0x5b05c2).
- Sliding never calls UpdateWalking or StartJump: no walking and no jumping while it lasts (dispatcher 0x5b1fb6) [disasm].
- "Landed" = `QueueEvent(kWE_Landed = 8); ChangeState(kWPS_Ambulatory)`; the debug string 0x85fb58 spells it.
- Thresholds (0x5a5d30, data):

| | Default | Slippy | global |
|---|---|---|---|
| SlideAngle (deg) | 60 | 10 | cos at 0x92009c / 0x9200a0 |
| StartSlideVel | 0.2 | 0.01 | squared at 0x9200ac / 0x9200b0 |
| StopSlideVel | 0.06 | 0.01 | squared at 0x9200b4 / 0x9200b8 |
| SlideFriction | 0.95 | 0.999 | 0x9200a4 / 0x9200a8 |

Sliding, further W4M detail (disasm):

- **Entry**:
  - Ballistic landing: `vt = v − (v·n)n`; `|vt|²` is halved when below `vn²`; walkable and below StartSlideVel² → Ambulatory, else Sliding with Velocity = vt (3D).
  - A successful walk step onto non-walkable ground (0x5b19d0): Sliding with Velocity = InputImpulse, set just before (0x5b19c8).
  - ImpulseWorm on the ground with `impulse·SupportNormal < 0`.
  - The idle walk branch never starts one: a still worm on steep ground stays. ChangeState (0x5aaa0c) zeroes the slide time, the spin rate +0x120 and its target +0x124.
- **Gravity** (0x5afd60-0x5afe26): `v.xz −= (Acceleration·n) n.xz · 20`. v.y gets nothing: only friction acts on it (×SlideFriction on all three axes, 0x5aff2d). The stop test (0x5b04c8) uses the 3D |v|².
- **Steering**, when Flags bit0 is set (0x5afe07):
  - The first test (0x5afe30) is `input · (Acceleration·n) n.xz`. It is > 0 when the input points up the slope, which gets 0.0005.
  - Otherwise the second dot (0x5afe68, FPU stack decoded) is `v · (Acceleration·n) n.xz`. It is > 0 when the velocity runs up the slope, which gets 0.003; else 0.0005 [disasm]. So 0.003 is for an input that does not point uphill while the worm still moves uphill.
  - Then `v += InputImpulse × 20 × k` (0x5afe93..0x5afeba: the ×20 is the st(3) constant); |InputImpulse| ≤ 0.05 units/ms. A zero dot (input across the slope) takes 0.0005 [disasm].
- **Air control**, Flags bit0:
  - set by DetectJump's launch (Flags |= 1) and by Fall() from a walk-off (0x5b14c7, airControl 1);
  - cleared by Rebound (0x5acea0), ImpulseWorm, the slide's drop (Fall(..., 0) at 0x5b02dc) and Ballistic's not-Fits land hit (0x5af8a5).
- **Spin**:
  - each frame `rate += clamp((target − rate)/4, ±0.0349)` (0x47a1a0, k 3), then `yaw += rate` (Orientation +0x90);
  - on a wall rebound (0x5b010b): `target = (target + 3·SupportNormal·(n_hit × v)) / 2`;
  - on following the ground (0x5b0448-0x5b049b): `target −= 2·(n_new × n_old)·v` (cross product 0x454d90(out, a, b) = a × b), then SupportNormal = n_new.
- **Probe** (0x5aff70): the 4 foot rays from cand + 20 units down, 300 steps; `cand = pos + v·20`.
  - d > 5: a wall. Below StartSlideVel → Landed. Else one frame's ray of all 8 points along v: a hit → Rebound + spin + stuck +2; no hit → Landed.
  - d < −5: a drop. `v −= (v·n)n`; if the body Fits at the old height → pos = (cand.x, pos.y, cand.z), Fall(v, 0, 0). Else Landed.
  - Else: pos = highest hit + 0.1; if not Fits → Landed; stuck −1; then walkable and below StopSlideVel² → Landed.
- A slope steeper than atan(5/4) = 51° puts the tripod's uphill foot over 5 units: a slow slide there lands (wall branch). So a slow worm stays on a 70° slope.

Ours (sim.cpp `slideStep` / `wormBody` / `slideIfSteep`, `Motion`; the same in ai.cpp `move` / `stepBody`):

- `Motion`, per worm and checksummed: `stuck`, `air`, `slide`, `spin`, `spinTo`, `normal`. `input` is this tick's stick, used only by Sliding.
- `slideStep` follows each W4M step above, in W4M frames per tick (DT/20 ms) and m/s (one unit/ms is 50 m/s).
- Details:
  - the wall branch casts the 8 probe points (the 4 feet, and the same 1 m higher for the heads) along v over one tick, as CastRays(pos, v, 20 steps, mask 0xFFFF) at 0x5b00e0; the hit normal is the mean of the hits within 1 unit of the nearest, as 0x59ef90. Ours is the Ballistic `sweep` (below) and `rebound`;
  - SupportNormal is `Motion::normal` (checksummed): stored at the landing (0x5af5ba), at a walk onto steep ground (0x5b1998), and on each ground follow (0x5b04a1); gravity, steering, the drop and the spin use it;
  - the spin's yaw sign matches ours. W4M yaw is `atan2(x, z)` (0x519120: acos(z), negated for x < 0) [disasm], our facing is (sin yaw, cos yaw), and tools/w4m-maps maps W4M x and z to our x and z without a mirror (`to_grid`) [data].
- Sliding takes no walk and no jump. A slide that ends sets the worm Ambulatory (velocity 0); a slide drop goes Ballistic with air control off.
- An Ambulatory worm with no velocity does nothing, as W4M's idle branch (0x5b1c1b) [disasm]. Ours has no push-up and no lateral push: the 0.3 m mesh may dip into land between the rods, as W4M's does (§5 "corners").
- A horizontal or upward push of a standing worm goes Ballistic; a push into the ground starts Sliding. A grounded worm is not moved by its velocity that tick: the slide moves it from the next one, as W4M's landing frame only changes state [disasm 0x5af7f6..0x5af804].
- The 0.40 m slot (the tripod's width) gave 30 never-ending slides. That was our earlier slide; the W4M probe's wall branch now lands those worms.

Ballistic, ours (sim.cpp `flyBody`, `sweep`, `rebound`, `rodsFit`; shared with the AI through `wormBody`):

- One sweep per tick, as W4M's one CastRays per frame: the 8 PROBE points along the tick's chord `v·DT + ½·a·DT²` (W4M casts the parabola; the chord is at most g·DT²/8 = 0.4 mm off) [ours].
- `Terrain::cast` per point, each stopping 1e-4 m short of its crossing (W4M's last empty sample, §5) [disasm; the offset ours]. Earliest first, a foot on a tie; the normal is the mean of the hits within 1 unit, as 0x59ef90 [disasm]. No hit is filtered on imported maps, as in W4M.
- [ours] Maps without cells (the bundled romfs maps, the generated island) skip a hit whose normal faces along the ray (dir·n ≥ 0): the field's gradient at a wall's foot can, a face a ray enters cannot. The jetpack and the slide's wall branch share this sweep.
- Fits is `rodsFit`: the 3 rods, a `cast` from the feet to the heads, any land [disasm 0x59edf0]; [ours] rods already in land at the start fall back to the relative ring test, as for the walk.
- No hit: Integrate; not Fitting: pos kept, stuck +2, Rebound on normalize(old − new). A hit: the contact must Fit (else stuck +2, Rebound on normalize(pos − contact), v = −v on no move), then pos = contact, `Motion::normal` = n; a head or n.y < 0.2 Rebounds, else the landing of step 4 (`wormBody`'s `land`) with FallDamage on −vn [disasm].
- `rebound` is Bounce 0x518f40 with e 0.3, the 0.5 m/s stop and both of Rebound's stop cases; a stop facing up starts Sliding, whose Landed clears the count.
- Stuck count: +2 on each failed Fits, −1 otherwise (floor 0); at 20 the worm is landed where it is, velocity 0 [disasm 0x5af821].
- [ours] An idle Ambulatory worm (velocity 0) with no ground under its feet stays put when a 1-unit fall does not Fit. W4M reaches the same state in 2 frames (undone fall, Rebound to a stop, Sliding, Landed with support 0xFFFF, 0x5b1a3e keeps it); ours re-tests the ground every tick, which would otherwise cycle Ballistic → Sliding forever.
- The jetpack's flight does not count, since it is not Ballistic [ours].

#### Passive 0x5b0c20 (kWPS_Passive = 6) and UpdatePassive 0x5aecb0

- **Passive** (worms not in control):
  - if entity +0x54 is set again → Ambulatory + UpdateWalking;
  - else if entity +0x64 (assumed: burning) → UpdateBurning 0x5aea10;
  - else, if the support surface (+0xe8 → 0x5160e0) reports it moved or vanished (0x47a080) → event 7 + **Ballistic**. No integration while parked.
- **UpdatePassive 0x5aecb0**, the turn-only mode of the active worm. UpdateWalking chooses it when 0x5ac390 is true: `Worm.WeaponDisableMovement` > 0, ArtilleryMode (+0x12a), Flags & 0x20, or game phase 14. It does:
  - Velocity = 0;
  - snap facing to the input (entity +0xde/+0xdf);
  - otherwise turn: left input (+0x37) → event 10, yaw rate → +0.001 rad/ms; right (+0x38) → event 11, −0.001; none → event 12, rate 0. The rate eases with 0x47a1a0 (accel 0.0002). Yaw += rate·20·k;
  - the support moved → event 7 + Ballistic.
- UpdateWalking's idle branch uses the same turn code (0x5b1c7a).

#### DeathThroes 0x5aa080 (7) and DrownFloat 0x5aa130 (8)

- **Land death start 0x5adbf0**: unless already DrownFloat, send `Worm.LandDeath` (msg obj 0x95fc18), state → 7, event 19, timer +0x128 = **3000 ms**.
- **DeathThroes**: no motion. Timer −20 per frame. At 0: Active = 0, death blast 0x5a9400 (`Worm.DeathWormDamageMagnitude/Radius`, `DeathImpulseMagnitude`, `DeathLandDamageRadius`), 0x5a9310, send `WXWormManager.UnspawnWorm` (0x95fbe8).
- **Drown start 0x5ad640**: `Worm.Drowning` (0x95fc10), event 21, particle `WXP_WaterSplash`.
- **DrownFloat**:
  - Entered by 0x5ad640, run every frame after the state update: Position (feet) < `Water.Level − Worm.Drown.HeightOffset` (7) [disasm 0x5ad6aa]. Takes the "Worm Dying" token (0x5a7190), posts Worm.WaterDeath.
  - Velocity at entry [disasm]: from Ambulatory (0) or Sliding (3) (branch 0x5ad8a0) it becomes (0.5·vx, −0.03, 0.5·vz) units/ms (0x5ad91f–0x5ad96d, copied by 0x63912f), so a standing worm sinks slowly; from any other state (Ballistic, rope, jetpack...) it is kept, so the sink depth grows with the fall speed.
  - No queue [disasm]: neither 0x5ad640 nor 0x5aa130 touches GameLogic's death queue (no AddMeToDeathQueue; Worm.TimeToDie is ignored in state 8, 0x5adbf0), and Update 0x5b1d60 runs the state for every worm with no gate. Each drowned worm blows up on its own timer +0x128; Worm.WaterDeath / Worm.Drowning are only read by CommentService (0x5e4ca0) and 0x588c10 (banner, stats).
  - target height = `Water.Level − 8.0` (hardcoded 8).
  - Timer 0, below the target and sinking (vy < 0): v = (6·v + (0, 0.03, 0)) / 7 (0x5a59f0, k 6). Below and vy ≥ 0: 0x569fa0 moves v toward (12·v + (0, 0.03, 0)) / 13, at most 0.001 units/ms per frame. At or above the target with vy ≥ 0: timer = **2000 ms** [disasm].
  - While the timer runs: vy = (vy − (y − target)·0.001)·0.95 (damped bob). pos += Velocity·20, no gravity.
  - Timer end: Active = 0, 0x5a9400, `WXWormManager.UnspawnWorm`.

#### ImpulseWorm 0x5ad010 (blast)

- Ignored in states 7/8. Magnitude ×WormPot.StickyModeScale when StickyMode is on. Air control off.
- On the ground with `impulse·SupportNormal < 0`: Velocity = (v + impulse) minus its normal component, state → **Sliding**, hit clip 0x5a9100.
- Otherwise `Velocity += impulse`, state Ballistic, and the reaction depends on `d = facing · horizontal blast dir`:
  - d < −0.5: event 14 (blown backwards) and yaw = atan2(dir) + π;
  - −0.5 ≤ d < 0.4: event 13 with param = side (cross product) and yaw = atan2(dir);
  - d ≥ 0.4, or a pure vertical blast: event 13 with param 0.
- **Hit clip 0x5a9100**: from facing·dir, `HitFront` (< −0.5, or no horizontal part), `HitBack` (> 0.5), else `HitLeft` (right·dir < −0.5) / `HitRight`. With a "nailed" flag, the `Nailed*` variant. Callers: the damage messages 0x5ae320 / 0x5ae4f0 and the slide path above.

### Worm animation (WXWormGraphicEntity), disasm + data

#### Event queue (kWE_*)

QueueEvent 0x5acb80(code, float) stores the code at entity +0x244. 0x5ac4d0 pushes code and param into the graphic entity's vectors at +0x178 / +0x184. The graphic update 0x5a4740 drains them in 0x5a3620 (jumptable 0x5a410c, index code−1).

| code | sender | graphic effect (handler VA) |
|---|---|---|
| 1 | UpdateWalking (walk input, phase 2) | send `HeldAccessory.Hide`, +0x160 = 0 (0x5a3692) |
| 2 | StartJump | `HeldAccessory.Hide`; 0x5a1060 (weight 0 on Walk, Jump_Start, Jump, Backflip, Fwdflip, AT_*, Fall, Land, Vault, Skid, Blastflight*, Recover*) and 0x5a1270; one-shot **Jump_Start** (+0x1ac, weight +0x1a8 = 1, t +0x1a4 = 0; 0.29 s [data]); anim state 2 (0x5a36ed). Kinds 3..6 run the same 0x5a1060 first: the launch clip cuts in, no blend [disasm] |
| 3, 6 | DetectJump (jump, vertical jump) | one-shot **Jump**; predicted parabola stored (+0x20c, 10000 ms); voice "Jump" (0x5a1d30); state 2 (0x5a3773) |
| 4 | backflip | **Backflip** + voice "Jump", state 2 (0x5a37de) |
| 5 | forward flip | **Fwdflip** + voice "Jump", state 2 (0x5a3849) |
| 7 | Fall, Passive / OverridePhysics support loss | **Fall** at weight 0 (eases in), state 2; 0x60ae60 ends the worm's acting scene (every cast actor released, 0x60bf70) and 0x60ba40 sets the actor's StopAnimation blend (+0x50) to 200 (0x5a389c) [disasm] |
| 8 | kWE_Landed (soft land, end of slide) | **Land** through the scheduler, weight = clamp(\|vn\|·5, 0, 1); pose reset; `WXP_Worm_Hop_Poof`; state 0 (0x5a3913) |
| 9 | Walking → Vaulting | **Vault** one-shot (0x5a39e3) |
| 10 / 11 / 12 | turn left / right / stop | +0x160 = −1 / +1 / 0, only for the worm in control (0x5a3a25...) |
| 13 | ImpulseWorm (side or up) | state 4 (blast flight). If vy > 0.1, `Acting.Trigger` 0x1a Blasted with the worm (0x5a3b44) [disasm]. Clip: param > 0 → **Blastflight4**, param < 0 → **Blastflight5** (tumble mode 1, random spin of 2π·(2r)+π); param 0 → random **Blastflight2** (mode 0) or **Skid** (mode 1) (0x5a3abb) |
| 14 | ImpulseWorm (blown backwards) | **Blastflight3**, mode 2, state 4 (0x5a3c11) |
| 15 | hard landing | `WXP_Player_Land_Poof` (worm in control) or `WXP_Worm_Land_Poof`. If the anim state was 3 (flying): `Acting.Trigger` 5 BlastSplat (0x5a3d4d), then the recover clip by flight mode (below). Else trigger 6 FallSplat (0x5a3e75) + **RecoverBurried1**. State 5 (0x5a3cc5) [disasm] |
| 17 | Ballistic or Walking → Sliding | WXActor (as 7), pose reset, arm-flail parameters (+0x1dc = 0.02, +0x1e0 = 30 from ground else 1). State 6 (0x5a3eaa) |
| 18 | MsgOverridePhysics | state 1: pose manager only (rope, jetpack...) (0x5a3f64) |
| 19 | Land death start | state 7 (= ground behaviour) + message type 0xe (0x5a3f88). The gesture comes from WORMACTING (docs/death-sequence.md) |
| 21 | Drowning start | state 8 (0x5a401a) |
| 22 | Ballistic, vy crosses −0.3 | **Skid** as the flight clip, tumble mode 1, spin 2π; state 4 (0x5a3a60) |
| 23 | Rebound | sound `weapons/Thud` at the worm (PlaySound 0x604a20) (0x5a4064) |
| 16, 20 | – | no-op |

#### Clip handles (0x5a49a0, `FindClip` 0x6a0350 by name, data)

| slot | clip |
|---|---|
| +0x94 | Walk |
| +0x98 | WalkTail |
| +0x9c | Jump |
| +0xa0 | Backflip |
| +0xa4 | Fwdflip |
| +0xa8 | Jump_Start |
| +0xac | AT_FB |
| +0xb0 | AT_LR |
| +0xb4 | Fall |
| +0xb8 | Land |
| +0xbc | Vault |
| +0xc0 | TailAngle |
| +0xc4 | TailLag |
| +0xc8 | TipAngle |
| +0xcc | HopLeft |
| +0xd0 | HopRight |
| +0xd4 | Skid |
| +0xd8 .. +0xe4 | Blastflight2 .. Blastflight5 |
| +0xe8 | RecoverFront1 |
| +0xec | RecoverBurried1 |
| +0xf0 | RecoverBack1 |
| +0xf4 | RecoverBack2 |
| +0xf8 | SkidArms |
| +0xfc | Death |
| +0x100 | FallDrown |
| +0x104 / +0x108 / +0x10c | FPX / FPY / FPZ (first person, docs/camera-w4m.md §11.3a: time FirstPersonOffset + 1) |

Clip API, an XAnim scheduler on the entity at graphic +0x24 (disasm):

| VA | call |
|---|---|
| 0x6a0350 | FindClip(name) |
| 0x6a03d0 | PlayClip (scheduler slot +0x14) |
| 0x6a04c0 | SetTimeAndWeight(clip, t, w) |
| 0x6a0530 | SetWeight |
| 0x6a0600 | Length (clip info +4, in seconds) |

#### Anim state machine (graphic +0x174, per-frame switch 0x5a4740 → jumptable 0x5a4978)

| state | handler | behaviour |
|---|---|---|
| 0 ground, and 7 (death) | 0x5a2a00 | Walk/WalkTail blended by horizontal speed s = \|Velocity.xz\|. Moving: phase += dt·s·0.45, wrapped to [0, 0.5); walk weight → 1. Stopped: the phase eases to the nearest rest pose (0 or 0.5), weight → 0 (rate 0.1). WalkTail weight is scaled further. WormPoseManager 0x59da40 adds the pose layers (aim arms, head and eyes: RightArmRotX/Y, HeadRotX/Y, Eyes_LR/UD, EmoteBlend, PoseBlend, strings at 0x85e1d8...) |
| 1 override | 0x59f2d0 | pose manager only |
| 2 one-shot | 0x5a01a0 | clip +0x1ac plays at 0.02 s per 20 ms (real time), weight → 1. At the end it **holds the last frame** and blends in **AT_FB / AT_LR**, time-scrubbed by the aftertouch input (phase = (1 − input·facing)/2 and (1 − input·right)/2, rate 0.1). No automatic chain: the next event (Land, Fall, 22...) switches the state |
| 3 / 4 blast flight | 0x5a0460 | the flight clip (+0x1b8) loops at 0.02 s per frame. Mode 0/2: body pitch follows the velocity (atan2). Mode 1: tumble, angle += spin·0.02 per frame (wraps at 2π). State 4 snaps on its first frame, then 3 eases at max 6°/frame |
| 5 recover | 0x5a2c90 | one-shot +0x1c8 at weight 1. At its end: state 0, clip weight 0, pose reset |
| 6 slide | 0x5a2d70 | **SkidArms** with random arm targets (rand 0x68c07b), WXActor face calls (0x60bae0 / 0x60bb40). Velocity terms ±0.006, 2000 ms constant (not decoded) |
| 8 drown | 0x5a06b0 | FallDrown (slot +0x100, loaded at 0x5a568b) scrubbed by clamp(vy·10, −1, 1), with pitch and roll easing back to 0 |

Recover clip after a hard land from blast flight (0x5a3d64):

- mode 0 → RecoverFront1;
- mode 1 (tumble) by spin angle +0x158 (0x5a3dc1; 3π/4 at 0x85e77c, 7π/4 at 0x85e778, 5π/4 at 0x85e770): below 3π/4 or above 7π/4 → RecoverFront1; 3π/4..5π/4 (head down) → RecoverBurried1 (0x5a3e8f); 5π/4..7π/4 → random RecoverBack1/RecoverBack2 (0x5a3e0b, rand 0x68c0aa & 0x100);
- the angle +0x158 is zeroed every frame by the one-shot (0x5a01a0) and ground (0x5a2a00) handlers, at the end of a recovery (0x5a2c90), in a slide (0x5a2d70) and by events 13/14 (0x5a3b87, 0x5a3caa). Event 22 keeps it: 0 after a jump or fall, the mode-0 flight pitch asin(vy/|v|) after a blast (0x5a0617, mode 2: π/2 − that);
- the angles +0x150/+0x158/+0x15c go to the model node (graphic +0x24, vfunc 0x54, 0x5a26d3): the body turns about the mesh's own origin;
- mode 2 → random RecoverBack1/2.

Smoothing helper 0x569f20(&v, target, a, rate, dt): approaches the target with the step clamped to rate·dt (disasm, exact law approximate). Most anim weights use rate 0.1 per 20 ms frame, i.e. about 0.2 s for 0 → 1.

WormPoseManager (0x59da40, disasm): `Blend` is an XTransform node of the worm whose clip channels drive the layers: Translate.x / .y = left / right arm mode (0x59b870), Rotate.y in degrees = head/eye mode (0x59be40), Scale.y = PoseBlend, Scale.z = EmoteBlend, Scale.x = visemes. Its smoothing helper 0x47a1a0(&v, to, k, max) is v += clamp((to − v)/(k + 1), ±max) per call. Arms, eyes and head: docs/worm-reactions.md.

Bored clock (0x5a47d0) [disasm]: graphic +0x5c adds the frame's ms (+0x198 = ms / 20, times 20) until 90000 (0x15f90), where WXActor +0x6e bit 4 ("bored") is set. kWE 13, 14, 15, 17, 19, 21 and 22 zero it and clear the bit (7 does not). The bit feeds the Bored pool (0x60df50) and lets an `Idle` track take a worm already in a scene (0x60c767). kWE 7 and 17 also end the worm's scene with StopAnimation blend 200 (0x60ae60, 0x60ba40).

#### Weapon clips (WeaponAccessoryEntity, WAE_*)

**Clip names [data: WEAPTWK `WXAnimDraw/Aim/Fire/Holding/EndFire/Taunt/TargetSelected/Windup`, container +0x54..+0x6c, Windup +0xb8 (melee)].** "–" = empty. A name the worm bundle (Bundl474, 329 clips) lacks loads as no clip (0x594da0 stores handle −1, length 0): AimBat, DrawCluster, DrawBanana, DrawGrenade, DrawGasgrenade, HoldCluster, HoldBanana, HoldGrenade, HoldGasgrenade, Fire1Banana, FireCluster, Fire2Grenade, FireGasgrenade, Struggle and the RainDance taunt are absent [data: byte search of Data/].

| weapon (id) | WAE class | Draw | Aim | Fire | Hold | other |
|---|---|---|---|---|---|---|
| Bazooka (1) | Standard | DrawBazooka | AimBazooka | FireBazooka | HoldBazooka | |
| Grenade (2), Cluster (3), Holy (6), Banana (7), Gas (16) | Thrown | (DrawThrown) | AimGrenade | (FireThrown / LobThrown) | (HoldThrown) | Windup (WindupThrown) |
| Dynamite (5) / Landmine (8) | Dropped | DrawDynamite / DrawLandmine | – | FireDynamite / FireLandmine | HoldDynamite / HoldLandmine | |
| Shotgun (9) / Sniper (28) | Standard | DrawShotgun / DrawSniper | AimShotgun / AimSniper | FireShotgun / FireSniper | HoldShotgun / HoldSniper | |
| Bat (10) / FirePunch (12) | Melee | DrawBat / DrawFirepunch | (AimBat) / HoldFirepunch | Fire2Bat / Fire2Firepunch | HoldBat / HoldFirepunch | Windup WindupBat / WindupFirepunch |
| Prod (11), NoMoreNails (25) | Standard | DrawProd, DrawNMN | – | FireProd, FireNMN | HoldProd, HoldNMN | |
| Homing (13) | Standard | DrawHomingMissile | AimHomingMissile | FireHomingMissile | HoldHomingMissile | TargetSelected AimLockHomingMissile |
| Flood (14) | Mechanical | DrawRainDance | HoldRainDance | FireRainDance | HoldRainDance | mesh Flood.Weapon |
| Sheep (15), SuperSheep (19) | Standard | DrawSheep | – | FireSheep | HoldSheep | |
| OldWoman (17) / Scouser (24) | Standard | DrawOldWoman / DrawScouser | (Struggle) | FireOldWoman / FireScouser | HoldOldWoman / HoldScouser | |
| Starburst (20) | Starburst | DrawStarburst | AimBazooka | FireStarburst | HoldStarburst | FlyStarburst (0x591314) |
| PoisonArrow (26) | Mechanical | DrawBow | AimBow | WindupBow | HoldBow | EndFire FireBow (never loaded, below) |
| SentryGun (27) | Standard | DrawSentrygun | – | FireSentrygun | HoldSentrygun | |
| Airstrike (4), Donkey (18), Abduction (22), SuperAirstrike (29) | Standard | DrawAirstrike | – | – | HoldAirstrike | mesh Radio |
| NinjaRope (35) | Standard | DrawNinjarope | AimBazooka | FireNinjarope | HoldNinjarope | |
| Surrender (39) | Standard | DrawSurrender | – | Tantrum | HoldSurrender | |
| SkipGo (38) | Standard | DrawSkipGo | – | – | HoldSkipGo | |
| Girder (34), Fatkins (23, WEAPTWK as Airstrike) | none: no 0x95f6d0 entry, no accessory, nothing in hand | | | | | |
| Redbull (41), BubbleTrouble (42) | Standard | DrawRedbull, DrawBT | – | FireRedbull, FireBT | HoldRedbull, HoldBT | |

Parenthesised names come from the class, not WEAPTWK. **Class by weapon id [disasm]**: table 0x95f6d0 (8 bytes per id), filled by 0x596830: 0x5901c0 WAE_Standard (default), 0x595ed0 WAE_Thrown (ids 2, 3, 6, 7, 16), 0x58c060 WAE_Dropped (5, 8), 0x58e6c0 WAE_Melee (10, 12), 0x58d720 WAE_Mechanical (14, 26), 0x5910a0 WAE_Starburst (20), 0x5942d0 the pack class (Jetpack 37, Parachute 36). Factory weapons (21): WAE_Thrown when WeaponType == 4 (kThrown), else WAE_Standard (0x5973e6).

**Slots [disasm].** Init (Standard 0x5901c0, Dropped 0x58c1c5, ...) loads each name as a (worm clip, weapon-mesh clip of the same name, length) slot through 0x594da0: Draw +0x54 -> (+0xe4, +0xe8, length +0xd8, clock +0xdc), Aim +0x58 -> (+0x120, +0x128) and `AimFP` -> (+0x124, +0x12c), copied to the Aim slot (+0x130, +0x138) and the AimFP slot (+0x134, +0x13c), swapped in first person by 0x594b50 (docs/camera-w4m.md §11.3a) [disasm], Fire +0x5c -> (+0x158, +0x160, +0x148), Holding +0x60 -> (+0x110, +0x114, +0x104, clock +0x108), EndFire +0x64 -> (+0x168, +0x16c, +0x14c), Taunt +0x68 -> (+0xf8, +0xfc, +0xec, clock +0xf0), windup -> (+0x184, +0x188, +0x170 / +0x178, clock +0x17c). 0x594d40 sets a slot's (time, weight) on the worm and on the weapon mesh. Exceptions: WAE_Thrown hardcodes DrawThrown, HoldThrown and WindupThrown (0x59618b..0x596291) and keeps WEAPTWK Aim and Taunt; WAE_Mechanical loads WEAPTWK **Fire into both the windup and the fire slot** (0x58d90e, 0x58d932) and never reads EndFire; WAE_Melee's windup is WEAPTWK Windup (+0xb8). EndFire is non-empty only for PoisonArrow, a Mechanical weapon, so no worm ever plays an EndFire clip.

**Aim time [disasm 0x58f63d].** t = WormData `WeaponAngle` (+0xd0, radians) / (π/2) + 1, in seconds (the Aim clips are 2 s long).

**Shared pose update 0x58f620 (Standard, Dropped) [disasm].** Jump table 0x58fe38 on state +0xb0:
- State 1 (drawn): while the draw clock < Draw length: Draw at weight 1 and t = clock, Aim at clock / length, Hold 0. Then Draw 0, Aim 1, Hold 1 looping (clock wraps at its length). WormData `PhysicsOverride` (+0xe8) == 8: every layer 0.
- State 2 (taunt): w = min(1, 4 (len − t), 4 t); Taunt at w, Aim and Hold at 1 − w; at t ≥ len back to 1.
- State 5 (`Weapon.PlayFireAnim`): Fire at 1, Aim at 1, Draw and Hold 0, until the Fire length (an empty Fire has length 0: state 5 ends on its first update); then EndFire from t = 0 (state 6) with Aim at 1. State 6 ends with Aim and Hold at 0 and state 0 (holstered), or 1 for weapon id 9 (Shotgun: its second shot, no redraw).
- State 0: nothing is written; `HeldAccessory.Hide` (kWE 1 walk, kWE 2 jump) runs 0x595bd0: state 0, every weight 0.

**WAE_Thrown (update 0x5954f0, handler 0x595ed0) [disasm].** States 1, 2 as above. `Weapon.PoweringUpStart` (PoweredWeaponLogicEntity, FIRE press): state 4, windup clock 0. State 4: WindupThrown at 1, t = clock, clock stops at the clip length (1.5 s); Aim 1; Draw and Hold 0. `Weapon.PlayFireAnim`: **FireThrown if the windup clock > 0.6 s, else LobThrown** (0x59663e, constant 0x853b40), blend v = 0, state 5. State 5: v += clamp((1 − v) / 2, ±0.2) per update (0x47a1a0); the throw clip at v, WindupThrown at 1 − v (frozen at its clock), Aim 1, Draw and Hold 0; at the throw's end its weight is 0 and state 0.

**WAE_Mechanical (update 0x58d0a0, handler 0x58d720) [disasm].** `Weapon.PlayWindupAnim` (state 4) comes only from PayloadWeaponLogicEntity on FIRE press in mode 2 (0x5860dd: a powered payload weapon); the bow mesh plays `Windup` (id 26). State 4: the windup clip at 1, clock clamped at its length; Aim 1; Draw and Hold 0. `Weapon.PlayFireAnim`: state 5, clock 0, the bow mesh plays `FireBow`. State 5: the fire slot (WEAPTWK Fire again: WindupBow from t = 0, FireRainDance for the Flood) at 1 and Aim at 1 until its length, then state 0. Init plays `DrawBow` / `DrawFlood` once on the weapon mesh (0x58d876, 0x58d89f).

**WAE_Melee (update 0x58de60, handler 0x58e6c0) [disasm].** Its windup state (4: Windup weight eased in at max 0.1 per update, Hold at the complement, clock wrapping) needs `Weapon.PlayWindupAnim`, which MeleeWeaponLogicEntity never sends (senders of the name objects: 0x5860dd only) [disasm], so WindupBat / WindupFirepunch never play. `Weapon.PlayFireAnim`: Fire at 1 + Aim at 1 (state 5), EndFire (state 6, empty), state 0.

**WAE_Starburst (update 0x590b70) [disasm].** State 1 as Standard. `Weapon.PlayFireAnim`: state 5, the mesh plays `FireStarburst`, global Aim weight 0x95f004 = 1 then eased to 0 (0x47a1a0, 0.1 per update); FireStarburst at 1 until its length. `Starburst.Launched`: state 7: FlyStarburst at 1, looping on clock 0x95f000; the fire slot is zeroed.

**Weapon mesh clips [disasm + data].** 0x594da0 looks each slot name up on the worm (0x5a1520) and on the weapon mesh (+0x90, 0x6a0350); 0x594d40 then sets the same (time, weight) on both, so the mesh plays its clip of the slot's name in step with the worm, a name it lacks being skipped. The mesh is WEAPTWK `WeaponGraphicsResourceID`: Super Sheep and Starburst name `Sheep` [data]. Mesh clips (Bundl09, w4m-models --list) [data]: Sheep DrawSheep / FireSheep / HoldSheep / TauntSheep (1 s each, channels cycling past their end: post-infinity 2); Bow DrawBow / HoldBow / TauntBow / Windup / FireBow (FireBow scales the arrow to 0 after 0.04 s); Scouser, Oldwoman, SentryGun and BubbleTrouble Draw / Fire / Hold / Taunt; SurrenderFlag Draw / Hold / Taunt; Shotgun DrawShotgun (the pump); HomingMissile.Weapon DrawHomingMissile and AimLockHomingMissile; Flood.Weapon DrawFlood; Jetpack JetpackRotLR. Clips no exe code names: Anticipate (grenades, Dynamite; only funfair-w3d.lub), FP_* (HolyHandGrenade, NinjaRope.Gun), the SuperSheep mesh's own clips; Landmine MineOn is the payload's WEAPTWK AnimArm. Played outside the slots, once from t = 0 through the scheduler (0x6a0730): WAE_Mechanical DrawBow / DrawFlood at Init (0x58d876, 0x58d89f), `Windup` on Weapon.PlayWindupAnim and `FireBow` on Weapon.PlayFireAnim, Poison Arrow only (0x58dc1a, 0x58dcd1); WAE_Standard the WEAPTWK TargetSelected clip on HUD.Target.Selected (0x590ae0: AimLockHomingMissile); WAE_Standard Init gives the SentryGun mesh its team colour clip, Red / Blue / Green / Yellow by the team container's byte +0x6c (0x590356, table 0x590b54), each a `$animTex0` texture pick: Blue 0.5, Red 1.5, Green 2.5, Yellow 3.5 = the child of the ball's XChildSelector [data]; the byte is TeamDataContainer `AlliedGroup` (+0x6c, schema) and the deployed turret does the same (0x56b2d0, team byte from 0x56e9eb) [disasm]. What the scheduler does at a one-shot's end was not traced (0x7a9cf8).

**Hold clock [disasm 0x58f800..0x58f8f7].** State 1 sets Draw at 1 and Hold at 0 until the Draw clock reaches the Draw length; only then does the Hold clock +0x108 advance (by the frame time, wrapping at the worm clip's length +0x104), so Hold loops on its own clock from the Draw's end, independent of the Aim time. State 2 advances it too (0x58fa4a). When Aim and Hold name the same clip (Flood HoldRainDance, Fire Punch HoldFirepunch), Hold's later 0x594d40 call wins on the shared handle.

**Jetpack lean [disasm].** WAE_Jetpack 0x58c770 sets JetpackFly (clock +0x1e0) and JetpackRotLR at t = `WXWorm.JetpackLR` + 1 (data key +0x1b8, bound at 0x59461b), both at weight 1, on the worm, and JetpackRotLR on the pack mesh (0x58cd74). JetpackUtilityLogicEntity 0x561e40 writes JetpackLR each update: with stick input, the lateral input component A against ±rate/2 (rate = 2 × TurnRotationSpeed) picks the turn: yaw += rate with target 1, yaw −= rate with target −1, inside the dead zone target 0 (input behind: by the sign of A); no input: target 0; lr moves by 0x47a1a0(k 1, max 0.03): lr += clamp((to − lr) / 2, ±0.03).

**Who sends what [disasm].** `Weapon.ActivateAccessory` -> a new WAE + `Accessory.Init` (0x597220, state 1, the Draw from t = 0): LogicalWeaponManagerService (0x565998, 0x56656c: selection, turn start) and UpdateWalking (0x5b1bed) once the worm is back on its feet with its weapon (not after a turn-in-place event 10..12, not for Jetpack / Parachute). `Weapon.Wield` (Payload 0x5862d0, Gun 0x55dbe5, Melee 0x5699bc, Powered 0x586f2f, NinjaRope 0x573ba1) redraws a holstered weapon through the state-0 taunt path (0x58c356: clocks 0, state 1). `Weapon.PlayFireAnim`: the payload launch 0x583160 (0x583302), Melee, Flood, Surrender, BubbleTrouble, SentryGun, Redbull.

The gestures (fidget, victory, hurt reactions, death) are WORMACTING EFMV scenes (docs/worm-reactions.md), cast by WXSceneManagerService. The trigger tokens are in WXActor.cpp strings 0x869950..0x869bbc. `Worm.QueueAnim` / `Worm.ResetAnim` have name objects (0x95c804, 0x95c7fc, 0x95e940) but no subscriber: only static inits and one send (ResetAnim, 0x587f3e, SkipGo/Surrender) use them; the `-w3d` scripts send them [disasm, data]. `Worm.ScriptDrawAnim` and `Worm.SurrenderAnim` are bare strings with no name object. All four do nothing on PC.

## 24. Simulation clock and timestep

Tags: [disasm] = read in `WormsMayhem.exe`, [data], [assumed].

### Main loop and game clock
- Wall clock 0x63a903: QueryPerformanceCounter (IAT 0x815080) since its first call, × 1000 / QueryPerformanceFrequency, in ms [disasm].
- Main loop 0x6f5161: per iteration the app's message pump (vfunc +0x8c), dt = wall ms since the previous iteration (0x6f51b6), then `XomTaskAppBase` frame 0x6f51df (WormsXApp vfunc +0x48 0x5111e0 calls it) [disasm].
- Frame 0x6f51df: dt = **min(dt, 100 ms)** (0x6f51ee); `/FIXEDUPDATETIME n` on the command line (0x6f4e45 → +0x60) replaces it by n; dt × time scale +0x1c (1.0, set once at 0x6f4f94), rounded to int ms (0x6fe6c6). It passes that dt to the +0x54 object's vfunc +0x20 (not traced) and, when +0x5c bit 1 is set, calls 0x6f42b2 [disasm].
- 0x6f42b2 (`Task.Update`): the game clock `+0x88 += dt` (not while the kernel's pause flag `*(0x96d030)+0x3c` is set, 0x6f436d), then `TaskManager::Update(&clock)` (vtable 0x884ddc slot 6, 0x68d4a8 → 0x68d4d4) [disasm].
- So a frame slower than 100 ms slows the game instead of running more steps; below that the game clock follows the wall clock exactly [disasm].

### TaskManager: a fixed-time event scheduler
- 0x68d4d4: t = clock − offset (+4). 0x68d6f1 runs the **timed queues** (4 priority heaps keyed by wake time): while some task's wake time ≤ t, it takes the earliest, sets the game time `*(0x96d030)+0x38` to **that wake time** (0x68d859, not the frame's clock), calls its Update (vfunc +0x18) with it, and reschedules it at wake + return value (0x68d8c1; −1 sleeps, 0 ends) [disasm].
- Hence every timed task runs at its own exact period, as many times as the clock advanced (up to 5 catch-up steps of 20 ms per rendered frame, from the 100 ms clamp), whatever the frame rate. No task receives a variable dt [disasm].
- Then two per-frame lists (+0x34, +0x4c; 0x68d91f) run once each with time = clock rounded **up** to a multiple of 20 ms (0x68d57f..0x68d589) [disasm]. Their time is always a 20 ms multiple, so nothing they draw can interpolate between logic frames through it [disasm; that no graphic entity reads the wall clock for motion is assumed].

### Periods
| task | period | source |
|---|---|---|
| Worm logic (physics states, Ballistic, Walking, Sliding) | 20 ms aligned: returns (t/20 + 1)·20 − t (0x5b220a..0x5b221f) | disasm |
| Worm Integrate 0x5a6e90 | constant 20 ms (literal 20 at 0x5a6ee0, 200 = 20²/2 at 0x5a6e9b), exact constant-acceleration step | disasm |
| Payload sweeps (0x581dc0) | 20 ms (push 0x14 at 0x581e5b) | disasm |
| Parabolic payload position | **analytic**: start position +0x28, start velocity +0x34 (`m_vInitialVelocity`, assert 0x57e0bc), acceleration +0x40 (gravity + wind), start time +0x1a0 = game time (0x57708a); re-based only at a contact (0x577430..0x57744a). FindFirstEvent 0x576580 searches the same parabola | disasm |
| Crate fall 0x5c9420 | 20 ms, **explicit Euler**: a 20-step parabola cast (0x466ae0), no hit: `pos += v·20` (0x5c961a), then `v += a·20`, or under the chute v.y moves ¼ of the way to −0.055 units/ms while below it | disasm |
| Oil drum 0x5d1c60, sentry gun 0x56d7f0 | 20 ms, exact constant-acceleration step (`v·20 + a·200`) | disasm |
| PayloadLogicEntity base update 0x57fae0 (Homing: vtable 0x859e7c keeps slots 25 and 30; other subclasses not checked) | 20 ms, **semi-implicit Euler**: slot 26 0x57e0a0 `v += a·20`, slot 30 0x5827c0 the sweep along `v·20`, no hit: slot 25 0x57fa70 `pos += v·20` | disasm |
| Bubble Trouble bubble 0x54f160 | 20 ms, explicit Euler (weapons.md §13) | disasm |
| GameLogicService 0x4fa2c0 | 20 ms | disasm (turn.md §14) |
| TimerLogicEntity 0x50f100 (turn, retreat, hot seat) | 10 ms (−10 per call, returns 10) | disasm (turn.md §14) |
| Scene players, particles, trails, menu ticker | 20 ms | acting.md §19, render.md §8, frontend.md §17 |

So W4M logic and physics run on a **fixed 20 ms step (50 Hz)**, turn timers on a 10 ms step, both on a game clock that follows the wall clock (clamped to 100 ms per frame), drawn once per loop iteration (present interval 1 unless `/VSYNCH n`, 0x6f4e84) with the latest logic state [disasm; the draw call itself not traced].

### Frame-rate dependence of the results
- Worm flight: Integrate is exact for constant acceleration, so the sampled points lie on the true parabola for any step; only *when* contacts are tested (each 20 ms chord sweep) depends on the step [disasm].
- Shells: the parabola is closed-form, so range and apex do not depend on the step at all; contacts are found by the 20 ms sweeps and FindFirstEvent [disasm].
- Explicit (crates, bubbles) and semi-implicit (Payload base) Euler at a fixed 20 ms step give the parabola launched with v ∓ a·10 ms: the
  path is fixed too, offset by a·10 ms·t from the closed form (0.125 m/s × t under standard gravity) [disasm + algebra].
- Per-frame rules that are not a constant-acceleration step depend on the 20 ms frame: Walk.Speed × 20 ms per walk step with its land rays (§5 UpdateWalking), Ballistic stuck count +2 / −1 per frame (limit 20), SlideFriction × per frame, the 1/5 lerp moves (§11), the 0.001 units/ms-per-frame clamps, Bubble Trouble's explicit Euler (weapons.md §13) [disasm].

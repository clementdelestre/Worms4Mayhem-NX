# W4M camera behaviour

Rules and values read from the user's copy of Worms 4 Mayhem: `Data/Tweak/CAMTWK.XOM`, `WEAPTWK.XOM`, `LOCAL.XOM`, the Lua scripts, and `WormsMayhem.exe`, disassembled with objdump. This file holds no game code and no assets. Distances are in W4M world units, 20 per metre.

Confidence levels:
- **data**: read in a tweak or script.
- **disasm**: deduced from the disassembly.
- **assumed**: inferred, not verified.

Addresses are virtual addresses in `WormsMayhem.exe`, image base 0x400000.

## 1. Architecture

- `CameraManagerService` (CMS, `.\CameraManagerService.cpp`) owns the logical cameras. Its classes, from RTTI: `DefaultCam`, `OccludingCam`, `ChaseCam`, `TrackCam`, `FlyCam`, `FallCam`, `GirderCam`, `NinjaCamMkIII`, `JetpackCamMkII`, `JetpackGroundCam`, `HeadCam`, `IsometricCam` (blimp/spectator), `OrbitCam`, `PathCam`, `TimedPathCam`, `SimpleCam`, `SpectatorCam`.
- To switch camera, game code does three things:
  1. writes `Camera.Data`, the name of a properties container in CAMTWK;
  2. writes `Camera.Entity.TaskId`, the tracked entity;
  3. calls `SetCamera` with a mode name: `"Track"`, `"Chase"`, `"FlyCam"`, `"FallCam"`, `"GirderCam"`, `"Ninja"`, `"Shoulder"`, `"Path"`, `"Orbit"`…
- Scripts can disable a mode with `Camera.Disable "Track"|"Blimp"|"Spectator"` and turn it back on with `Camera.Enable`.
- **Each payload picks its camera in WEAPTWK.** `PayloadWeaponPropertiesContainer` field `CameraId` (§7). Values (data):
  - `PayloadTrackCamera`: Bazooka, Grenade, Cluster, Banana, Bananette, Dynamite, Holy Hand Grenade, Gas, Poison Arrow, Landmine-cluster, Factory weapons.
  - `FatkinsTrackCamera`: Fatkins.
  - `DonkeyTrackCamera`: Concrete Donkey. This name does **not** exist in CAMTWK; the Donkey code uses `DonkeyCamera` directly.
  - `OldWomanChaseCamera`, `ScouserChaseCamera`.
  - `SheepChaseCamera`: Sheep and Super Sheep.
  - `HomingMissileFlyCamera`: Homing Missile.
  - Empty: Airstrike, Super Airstrike, Landmine, Fatkins food, Sentry payload.
- **The container class decides the camera type** (`PayloadLogicEntity`, function 0x575320, called while the payload is alive):
  - `TrackCameraContainer`: the payload requests a track camera (0x51bf80, priority 2) if its event point (+0x12c, the predicted first contact, §2) is off screen (0x51b3b0) **or** +0x1a8 is greater than `Camera.Track.MinEventTime` = 1000 ms; +0x1a8 = 0 requests nothing (disasm 0x575320).
    - +0x1a8 is the **predicted time, in ms, from now to that event**: FindFirstEvent 0x576580 sets it to −1, then to the time out of each event finder (0x575020: fuse end − launch time; water, disarm and expiry planes 0x574d90; the land sweep 0x574e90), together with the event point (0x5766cb, 0x5767d7, 0x576940, 0x576af2); a payload at rest gets the scheduler clock (0x575a39). Nothing counts it down (disasm).
    - The test runs **once, at launch**: 0x575320 is the ParabolicPayloadLogicEntity override of vtable slot 0x74 (0x85b578), called only from 0x577530, the handler of task message 0x40 (activation, 0x578319), right after FindFirstEvent. Bounces recompute the event point (which the TrackCam reads every frame) but ask for no camera (disasm). So a shot that lands on screen in under 1 s is never tracked, whatever it does after its first bounce.
    - Re-audit of the ask (disasm 0x575320, 0x576580, 0x574e90): `+0x1a8 = 0` returns at 0x5753aa; otherwise the request goes out when the event point is off screen or `+0x1a8` (unsigned, so −1 = no event also asks) is above MinEventTime. FindFirstEvent takes the fuse end as the time limit (only if +0x58 > 0), then the land sweep 0x574e90 (0x466ae0, no bounce) replaces it only if earlier: a bounce never counts, and the visibility drop (priority 6) belongs to the worm requests 0x51cf20 only, never to a payload. `checkPayloadFollow` (sim_check) asserts the look-at stays on the shot for Grenade, Cluster, Banana, Holy and Bazooka, from the shoulder and the first-person aim, on a second turn. A throw whose first contact is on screen within 1 s (a wall or overhang close to the thrower) is not followed, as W4M | disasm, ours (test) | 0x575320–0x575432, 0x576580, 0x574e90
    - Payload classes that do not override slot 0x74 use 0x57e400: track (or chase) at once, unconditionally (disasm).
  - `ChaseCameraPropertiesContainer`: chase camera starts immediately (0x51d5e0, active priority 6).
  - Fly cameras are started explicitly with `SetCamera("FlyCam")` (0x57e380), by Super Sheep, Starburst and Homing.

- **Drawn view, update rate, Cut flag, PhysicsOverride**: see §11.

### Event-camera priority queue (disasm, CMS +0x350..0x374)

One request is pending at a time: priority, camera name, task id, event position and velocity. A new request replaces it only if its priority is higher.

| Prio | Request | Source |
|---|---|---|
| 1 | crate drop, `CrateTrackCamera`, if `Crate.TrackCam` = 1 (LOCAL default 1) | 0x51bfc0, called at 0x5c8c37 |
| 2 | payload track, the weapon's camera name | 0x51bf80 |
| 3 / 5 | worm track, `WormTrackCamera`; 3 = ballistic with predicted landing, 5 = otherwise | 0x51cf20 |
| 4 / 6 | same as 3 / 5, but the worm and its predicted landing point are both on screen with clear line of sight from the current camera: **dropped, no cut** | 0x51cf20 → switch at 0x51d3b3 |

- **Worm-track triggers:**
  - `WXWormLogicEntity::ImpulseWorm going Ballistic` (knocked away by an explosion or hit), 0x5ad60b;
  - "Worm Displaying Damage Taken", 0x5abeec;
  - "Worm Dying", 0x5a7282;
  - walking payloads, 0x5929ed;
  - game over, 0x4fff2d.
- **Event data for a worm track:**
  - `Camera.Track.EventPosition` / `EventVelocity` come from the worm's position and velocity.
  - For a ballistic worm, the position is its predicted landing point, from a trajectory sweep (0x515eb0, 0x466ae0).
- **When a request is served** (0x51d3d0), all of these must hold:
  - more than 200 ms since `Camera.LastTrackCam` (the timestamp of the last track cut);
  - the current logical camera is not index 14;
  - the running track camera's priority is not higher than the request's.
- **Serving a request:**
  1. writes `Camera.Data`, `TaskId`, `EventPosition`, `EventVelocity`;
  2. stamps `LastTrackCam`;
  3. creates a `TrackCam` (`"Track"`).

## 2. TrackCam (disasm, `.\TrackCam.cpp`, functions 0x532460 / 0x532a50 / 0x533190 / 0x533950)

Fields of `TrackCameraContainer`, all data:

| Field | Crate | Fatkins | Payload | Worm |
|---|---|---|---|---|
| Camera2ObjectDistance | 500 | 1000 | 1300 | 600 |
| LookSpeed | 0.1 | 0.1 | 0.1 | 0.1 |
| UpSpeed | 0.2 | 0.8 | 0.8 | 0.8 |
| ZoomSpeed | 0.009 | 0.019 | 0.019 | 0.0015 |
| MinPreferredDistance | 200 | 500 | 600 | 200 |
| MinPosition (above water) | 20 | 20 | 20 | 20 |
| MinTimeBetweenCuts (ms) | 1000 | 1000 | 1000 | 1000 |
| CutWhenStartOffScreen | 1 | 0 | 0 | 1 |

Candidate offsets (data), in order (x lateral, y up, z along the event direction), with the elevation of the offset seen from the event point, atan(y / √(x² + z²)):

| # | Payload | el | Worm | el | Crate | el | Fatkins | el |
|---|---|---|---|---|---|---|---|---|
| 0 | (-100,200,300) | 32° | (70,80,200) | 21° | (0,100,300) | 18° | (200,100,-500) | 11° |
| 1 | (100,200,300) | 32° | (-70,30,180) | 9° | (0,100,-300) | 18° | (200,100,500) | 11° |
| 2 | (25,50,50) | 42° | (-50,60,200) | 16° | (300,100,0) | 18° | (-200,100,500) | 11° |
| 3 | (-25,50,50) | 42° | (50,50,230) | 12° | (-300,100,0) | 18° | (-200,100,-500) | 11° |
| 4 | (75,100,75) | 43° | (20,60,300) | 11° | (0,500,50) | 84° | | |
| 5 | (-75,100,75) | 43° | (10,20,100) | 11° | | | | |
| 6 | (50,75,-50) | 47° | (-10,30,100) | 17° | | | | |
| 7 | (-50,75,-50) | 47° | (-30,120,200) | 31° | | | | |
| 8 | (50,-50,75) | -29° | (10,25,80) | 17° | | | | |
| 9 | (-50,-50,75) | -29° | (10,80,-200) | 22° | | | | |
| 10 | (0,30,0) | 90° | (-30,60,-180) | 18° | | | | |
| 11 | | | (10,20,-80) | 14° | | | | |
| 12 | | | (0,80,0) | 90° | | | | |

Rules (disasm unless marked otherwise). Update 0x533950 calls 0x532a50 (object, object direction, event point, event direction), then the cut search 0x532460, then the back-off 0x533190.

1. **Frame** (0x532460, 0x5321b0).
   - The base point is the **event point**, not the object:
     - payload: `ParabolicPayloadLogicEntity` +0x12c, written by FindFirstEvent (0x576580, sweep 0x574e90 → 0x466ae0): the **predicted first contact** of the current flight (land, water, disarm or expiry plane, or where the fuse runs out), recomputed at launch and after each bounce. +0x138 is the velocity there. TrackCam reads both every frame (0x532ad2);
     - worm: `Camera.Track.EventPosition`, its predicted landing point;
     - crate: its own position.
   - Event direction `d`: the horizontal, normalised event velocity, else the object direction. Object direction: the object's horizontal velocity, else its facing.
   - Candidate = `event + x·(d.z,0,-d.x) + y·(0,1,0) + z·d` (0x5326b9–0x532840). `(d.z,0,-d.x)` is up × d. y is world up, relative to the event point's height. Offsets are not scaled. +z is **ahead** of the event point along the travel, so a payload camera stands beyond the impact point, looking back at the incoming shot.
   - A worm's look-at point is its position + 10 in y.
2. **When it searches.** Every frame, if (off screen **and** +0xb9) **or** the camera-to-object segment hits land **or** distance ≥ `Camera2ObjectDistance`, **and** ≥ `MinTimeBetweenCuts` since the last cut.
   - +0xb9 starts at `CutWhenStartOffScreen` (0x5338ec) and is set once the object has been on screen. So a payload that starts off screen, never seen, does not cut for that reason.
   - Activation (0x5337c0) copies position, look-at and up from the camera before and sets the cut timer to `MinTimeBetweenCuts`: **no cut is forced**. A payload in clear view, closer than 1300, keeps the worm camera's position and is only followed by the look-at.
3. **Accepting a candidate.** All of these must hold:
   - the 2D sign of `cross(object direction, object − candidate)` agrees with that of the current camera (product > −1e-5): the camera never crosses the object's line of travel. Tested at every search, the first one included;
   - clear segment from the candidate to the object (0x51abf0);
   - candidate y > water level + `MinPosition`;
   - clear segment from the candidate to the event point.
4. **Search order.**
   - Index +0xbc starts at 0 at activation and **persists**; it is never reset after a cut.
   - At most 2 candidates per update (two updates every 20 ms, §11); a refused one advances the index (mod count), an accepted one does not.
   - Accepted: if it is the index of the last cut (+0x70, −1 at activation), **no cut**; otherwise a hard cut (position = candidate, look-at = object, up = (0,1,0)), cut timer reset.
5. **Between cuts:**
   - look-at: `lerp(lookAt, object, LookSpeed)` per update, while the object exists;
   - up: `lerp(up, (0,1,0), UpSpeed)`;
   - back-off (0x533190): only when the object is at rest or moving toward the camera (look direction · velocity < 0), and closer than `MinPreferredDistance`: the camera lerps at `ZoomSpeed` toward object + MinPreferred·(camera − object)/|…|, that target clipped by 0x51b040 on the segment camera → target: 0x51ae40 stops it where it crosses `Water.Level`, 0x51af90 at the land hit (0x466a20, the hit point itself), 0x51ac40 where a 5-unit sphere (0x8244fc) swept by the collision manager (0x519c60 …) first touches (disasm);
   - height: y eases to at least water + `MinPosition`;
   - if the camera sphere-tests inside land (radius -30, function 0x466a20), y += 5 per update.
6. **End.**
   - Payload or crate: when the tracked entity disappears (explosion), the camera **freezes and keeps looking at the last point** for `Camera.Track.RestTime` = 1500 ms (vtable +7, 0x5334b0). The TrackCam is then finished.
   - **Only the TrackCam finishes** [disasm]: the CMS update 0x51da00 slides the PiP off (0x51c0b0) when the PiP camera's vtable slot 7 (+0x1c, 0x51df71) returns true (§11.6b); slot 7 is 0x5334b0 for TrackCam and 0x49b8f0 (`xor al, al`) for Camera, ChaseCam, OccludingCam, SimpleCam and FlyCam. A ChaseCam whose target is gone keeps its last view (target update 0x5245e0 returns when the entity lookup 0x68e4ca fails). The FlyCam's PauseDuration ("Hold the FlyCamera for a moment after the explosion", 0x528452) is an ActiveObjectRegistration token that keeps the turn waiting; at its end the token is released and the camera simply stops updating (0x5282b0). So a Chase or Fly event camera stays until another event camera replaces it (a request served by 0x51d3d0, a Chase / Fly / Simple start 0x51c840) or the next turn clears it (0x51ef80, 0x51f740).
   - Worm: also finishes 1500 ms after its activity tokens are all released: WXWormLogicEntity +0x54, +0x58 and +0x5c ("Worm Waiting To Die" / "Worm Dying") null, latched once (0x532e87–0x532ec2; the damage display slot +0x60 is not tested). A dying or drowning worm keeps +0x5c until its blast and unspawn, so its track lasts through the blast, then RestTime (disasm).
   - The object's look-at stops updating once it is more than 40 below water level (sinking).

### Why the Holy Hand Grenade is sometimes filmed from far and high, sometimes close

All four cases follow from the rules above (disasm + data):

1. **The thrower's view, no cut.** The track is requested at once if the predicted landing point is off screen, else after 1 s of flight since the last event. The TrackCam keeps the worm camera's position and turns to follow, as long as the grenade stays in clear view within 1300 units.
2. **Far and high, beyond the landing point.** When the grenade is lost (behind land, off screen once seen, or beyond 1300), candidate 0 or 1 is taken: (±100, 200, 300) from the **predicted landing point**, 300 past it along the throw, 200 up: about 370 units out, 32° above the landing point, looking back at the grenade.
3. **Close.** Candidates 0 and 1 are refused when land blocks the ray to the grenade or to the landing point, they would be under water + 20, or they would cross the line of travel. The search moves on to (±25, 50, 50), about 75 units out, then (±75,100,75), behind (±50,75,-50), low (±50,-50,75), and finally above (0,30,0). Each bounce moves the event point to the next predicted contact, so a later re-cut lands around it.
4. **At the explosion.**
   - The track camera freezes for 1.5 s.
   - Each worm sent flying asks for `WormTrackCamera`, but only if that worm or its landing point is **not** already visible. If they are visible, the shot stays.
   - Otherwise a cut 80–300 units from the landing point, along the direction the worm flies, 20–120 high (9–31° from the landing point; 90° only for the last one).
   - `Camera.Shake.Exp*` is applied, scaled by the radius. The HHG has `WormDamageRadius` 187 versus 82.5 for the Bazooka, so its shake is much stronger.

## 3. Other cameras (data, CAMTWK)

| Camera | Type | Main values |
|---|---|---|
| ShoulderCamera (active worm, DefaultCam) | Occluding | DistFromObject 170, DefaultHeight 0.255 (Min 0.1, Max 1.0), PosUpdateSpeed 0.1, LookUpdateSpeed 0.1, YawSpeed 1.0, HeadOffset (0,20,0), TailOffset (0,0,-18), MaxLkAheadDist 25, OccInnerTestPoints 5, OccZoomPctge 90, OccDestSize 55, OcclusionSize 10, OccZoomIn 1.0, OccZoomOut 0.02, TimeBeforeZoomOut 1000 ms, ZoomOffsetDist 25 |
| GirderCamera | Occluding | Dist 325, DefaultHeight 0.6, MinZoomDist 100 (`Girder.Camera.Occlude` = 0 in LOCAL) |
| NinjaCamera | Occluding | Dist 400, height ±1.57, StartYaw 1.57, ResetYaw |
| Sheep / OldWoman / Scouser ChaseCamera | Chase | Dist 170, DefaultHeight 0.255, HeightSpeed 0.45, MinZoomDist 50, YawSpeed 0.4; Old Woman MaxHeight 1.3, MinHeight 0.25; head/tail offsets per animal |
| HomingMissileChase / MadCowChase | Chase | Dist 170, HeightSpeed 1.4, YawSpeed 1.7 |
| StrikeChaseCamera | Chase | Dist 300, CutOnRetreat = 1. Unused: the name is in CAMTWK only (no other Data file, Lua or WEAPTWK holds it, and the exe has no such string and no `%s` camera-name format) (data, disasm) |
| HomingMissileFlyCamera | Fly | LagBehind 60, LookAhead 100, PosSpeed 0.05, LookSpeed 0.1, PauseDuration 1000 ms, FinalDistance 500 ("Hold the FlyCamera for a moment after the explosion") |
| SuperSheepFly / Starburst | Fly | LagBehind 80, LookAhead 100, LookSpeed 0.2, PosRate 1.0 / 0.1, Pause 1000, Final 500 |
| FallCamera (worm falling) | Fly | LookSpeed 0.08, PosSpeed 0.02 |
| AlienAbduction / Donkey / MineFactory / SuperAirstrike / Flood | Simple | (PosUpdateSpeed, LookUpdateSpeed) = (1,1) / (1,0.1) / (1,0.1) / (1,0.1) / (0.01,0.01) |
| Orbit (game over) | OrbitCam | CAMTWK `Camera.Orbit.Height` 450, `AdditionalRadius` 100, `Speed` 0.3, `IgnoreInput` 0 exist, but the exe has no `Camera.Orbit.*` string, so none is read; the real values are in §6 (data, disasm). Disabled by `Script.NoOrbitCamera` = 1 (LOCAL default 0) |
| Blimp (free view) | Isometric | HeightAboveLand 6, StickLength 500, DefaultPitch 1.0, Zoom 0.15–2.0, MoveSpeed 250 |
| Jetpack | JetpackCamMkII | StickLength 230, Pitch -70..60, DefaultPitch 0.5, PosUpdateSpeed 0.995, LookUpdateSpeed 0.2, UpUpdateSpeed 0.2. Update 0x52b4c0: no input; behind the worm's yaw, pitch += 0.01 (PitchSpeed) × (0.5 − PitchScale 4 × vy − pitch) each frame, the -70/60 clamp never binds, kept 5 units over Water.Level. Requested on take-off (0x5624b4), `Default` on landing / dry. "Jetpack Ground" (JetpackGroundCam) is created (0x522191) but never requested by name. Ours: controls.cpp `camera()` jetpack branch |
| Shake | — | `Camera.Shake.ExpDurationScale` 800, `ExpMagScale` 1, `ExpRadiusScale` 8, `Max` 0.01; earthquake magnitude 1.0, 7000 ms |
| Worm fade | — | `Camera.WormOpaqueDist` 50, `WormTransparencyDist` 25 (the opaque distance must be greater, per an exe assert) |

Occluding/Chase field units: `DefaultHeight`, `MinHeight` and `MaxHeight` are pitch angles in radians: the placement 0x52e2f0 takes their sin and cos (§5; disasm).

## 4. Scripts (data, Lua)

- **Turn start:** `stdvs` and the other versus scripts call `SetData("Camera.StartOfTurnCamera","Default")`. The engine moves the camera itself.
- **Intros:** W4M levels use EFMV (`EFMV.Play "Intro"`). The `-w3d` levels use `Camera.Path.Knots.Position` / `LookAt` (comma-separated locator names), `Steps` (0 = cut), `Tension`, then `Camera.Path.Start`. Callbacks: `Camera_Path_Reached_Knot` and `Camera_Path_Stopped`.
- **After a cutscene:** `Camera.SetDefault`.
- **Orbit after a path:** `SetData("Camera.Orbit.Speed", ±0.2)`.
- **Per-crate tracking:** `Crate.TrackCam` (0/1) per crate.
- **Shake:** `lib_ShakeCamera(len, mag)`.
- **Game over:** there are no camera calls beyond `EFMV.GameOverMovie "Outro"`. `Script.NoOrbitCamera` = 1 appears only in BREAKFAST and notpc.

## 5. Occlusion and worm transparency

Addresses below are disasm; values are data unless marked otherwise.

### Camera: OccludingCam / ShoulderCamera (`.\OccludingCam.cpp`)

- **Placement** (0x52e2f0): `cam = target + dist·(sin yaw·cos pitch, sin pitch, cos yaw·cos pitch)`.
  - `DefaultHeight`, `MinHeight` and `MaxHeight` are pitch angles in radians. Shoulder: 0.255 (14.6°), 0.1 and 1.0.
  - Desired distance is `DistFromObject` 170.
- **Pitch input** (0x52de20 / 0x52df30):
  - Pitch moves at `HeightSpeed` 0.45; mouse input is scaled by `MouseX/YSpeed` 0.25.
  - Below `MinHeight`, the excess becomes look-above: ×`LkAboveSpdScale` 80, capped at `MaxLookAboveObj` 80. The camera tilts up instead of going lower.
- **Occlusion test** (0x52efa0, every frame):
  - 5 forward points plus `OccInnerTestPoints` (Shoulder: 5, so 10 rays). Limits: inner points are 0 or at least 3, at most 10; at most 15 in total.
  - The base point is built from the worm plus `HeadOffset` (0,20,0) and `TailOffset` (0,0,-18), rotated by the worm's facing.
  - Forward points are the centre plus centre ± `OccDestSize` 55 along the camera's right and up vectors.
  - Inner points lie on an arc of radius 0.75·55 = 41.25 in the right/up plane, from 9° to 171°.
  - Each point gets one land segment raycast (0x466a20) toward the camera. The camera end of each forward ray is pushed out by `OcclusionSize` 10.
  - **The view counts as occluded if the centre ray is blocked and blocked rays ≥ `OccZoomPctge`** (90 % for Shoulder, 100 % for the other cameras).
- **Response when occluded:**
  - Shoulder has `OccYawSpeed` = `OccHeightSpeed` = 0, so it never turns or climbs. It **zooms in**: target distance = distance from the worm to the first land hit, on a ray from the worm toward the camera.
  - Girder, Chase and Homing cameras turn or climb instead, at Yaw 0.9–2.0 and Height 0.4–0.5 rad/s, when only one of left/right (or up/down) is blocked. A direction flip is damped by `AntiGimScale`.
- **Zooming back out:**
  - When not occluded, 5 rays are cast out to the desired distance + 4. Target distance = clear distance − 4, at most 170.
  - If that comes out under 75 while the player is idle, pitch is nudged by ±0.1 toward the clear side.
- **Update** (0x530690), with fixed per-update lerp factors, not scaled by dt (two updates every 20 ms, §11):
  - the distance (+0xe0) eases to the desired one (+0xe4): zoom in at `OccZoomInSpeed` 1.0, i.e. an instant snap; zoom out at `OccZoomOutSpeed` 0.02 an update;
  - the position is then placed from the target point, yaw, pitch and distance (0x52e2f0) with **no smoothing**; the 0.1 of position and look-at is the drawn-view blend of §11 (PosUpdateSpeed / LookUpdateSpeed written to +0x50 / +0x4c at 0x530953 / 0x530960);
  - the look-ahead offset (+0x94) eases to its goal (+0x88) at `LookAheadSpeed` (+0x44 of the container, 0.02); it is added to the target point (+0xa4) only when distance > 75 (`LookAheadScale` 500, clamped to `MaxLkAheadDist` 25);
  - under 25, the look-at height is blended toward the camera height;
  - under 25 units of camera distance (+0xe0), look-at y = t·look-at y + (1 − t)·camera y with t = 0.5·distance / 25 (0x5308ac–0x5308e6); at 25 and beyond, nothing;
  - camera and look-at heights are kept ≥ `Water.Level` + 5: +0x44 is the `Water.Level` handle written by the base Camera constructor (0x51b69b), read at 0x5308ec (disasm).
- **Target point** (0x52e6e0): worm Position (+0x38) + its `ForcedCameraOffset` (WormDataContainer +0x44). While that is below `Water.Level` − `Worm.Drown.HeightOffset` (TWEAK 7), the target is held at that height and its velocity zeroed: the camera stops following a sinking worm 7 units under the surface (disasm; value data).
- **Fields never read** in OccludingCam/DefaultCam: `TimeBeforeZoomOut`, `ZoomOffsetDist`, `UpUpdateSpeed`, `MinLookAt`, `MinPosition` (container +0x88, +0x9c, +0x94, +0x5c, +0x60). The 18 functions that fetch the `OccludingCameraPropertiesContainer` (every xref of its IsKindOf assert 0x854ad8: 0x524880 … 0x530c00) read only +0x14–0x58, +0x64–0x84, +0x8c, +0x90, +0x98, +0xa8, +0xb0; the `[esi + 0x88/0x94/0x9c]` hits there are camera fields. The **1000 ms "before zoom out" is therefore not used** on PC (disasm).
- **Camera distance toggle** (`Camera.ToggleDistance`, DefaultCam 0x524ed4 → 0x524880): it flips the team's kLongshot/kCloseup (+0x28), then sets the camera's desired distance to `DistFromObject` (+0x30) either way, so it has no effect on PC (disasm).

### Active worm fade (`WXWormGraphicEntity` 0x5a4420)

- Applies to the **active worm only**.
- `alpha = clamp((|cam − worm| − Camera.WormTransparencyDist 25) / (Camera.WormOpaqueDist 50 − 25), 0, 1)`: invisible at 25 or less, opaque at 50 or more.
- It depends on camera distance only, not on occlusion. In practice it hides the worm when an occlusion zoom brings the camera onto it.
- With the logical camera's type (+0x2c) = 1, which is HeadCam (its constructor 0x5296d0 passes 1 to the base Camera constructor 0x51b570; FlyCam 6, FallCam 7, OrbitCam 4, JetpackCamMkII 9, TrackCam 0xf, PathCam / TimedPathCam 0xe, SimpleCam 0x15, RayCam 2), `alpha = max(0, 1 − |Velocity|²/0.016)` (worm Velocity +0x50 in units/ms: invisible from 0.126 u/ms = 6.3 m/s), or 1 with no worm data (disasm 0x5a446b–0x5a44d1).

### Silhouette and outline (data: `CG/PostProcess.cg`; disasm: `PCPostProcess.cpp` 0x61e0e0)

**Which worms are concerned:**
- The **active worm** goes into render bin `OutlinedWorms1`.
- With binoculars, every other live worm goes there as well, and the outline flag is set. The blimp view sets the outline flag too.
- Crates and objects are never concerned.
- Each worm in the bin writes stencil bit `1 << team`.

**Passes, in order:**
1. **`Silhouette_PC`**, worm pixels only (stencil ≠ 0), depth test ALWAYS.
   - Where the scene is in front of the worm, i.e. land hides it, the output is `lerp(scene, grey (0.25,0.25,0.25), 0.5)`.
   - Otherwise nothing is drawn.
   - So the see-through view of the active worm is a **flat 50 % grey** tint, not team colour.
2. **`Outline`**, only when the outline flag is set (binoculars / blimp).
   - One pass per team, stencil EQUAL on that team's bit, depth off.
   - Colour = team colour / 255 with alpha 0.247. The team table at 0x90f820 holds 0xff3c4eff, 0xffff7f6c, 0xff7bf43b, 0xff4ddaff.
   - A pixel is drawn if one of its 4 diagonal neighbours, 1 texel away, is off the worm. The line is 1 pixel wide, on the inner edge.
3. **`RecombineWorms`**: `lerp(scene, worm, opacity)` where there is a worm pixel.
   - Opacity is the fade above, forced to 1 when the outline pass ran.
   - Depth = min of scene and worm depth.

The pipeline needs an FBO. It is disabled by `/NOWORMOUTLINES`.

## 6. Other events

### Game over (disasm, `GameOverLogicEntity.cpp`, 0x4ffe40–0x4ffbc1)

1. Phase duration is 5000 ms, or 15000 ms when `WXomOnlinePlugInService` (+0x70, read by the getter 0x5a6350 on the instance 0x962028) is set; that flag also skips `EFMV.Start` (0x4fff8c). The service refreshes it in its Update (0x62dbea) from the online session interface (this+0x20 → vfunc 0x54 → vfunc 0x38), so it is an online-session state; which one the runtime interface reports cannot be named from the exe alone (disasm).
2. The engine reads `MostRecentlyActiveWorm`.
   - If none: `Orbit` immediately.
   - If that worm is still alive: a **WormTrackCamera request** on it (0x51cf20).
   - Otherwise: the first live worm out of 16.
3. Fireworks, cheering and `music/victory` play.
4. Update 0x4ff8d0 (every 20 ms, timer +0x28), state +0x48: state 0 waits 4000 ms, then `SetCamera("Orbit")` (0x4ff790) if `Script.NoOrbitCamera` = 0 (LOCAL default) and restarts the timer (re-sent every 4 s), else goes to state 2; state 1 shows the fireworks until +0x24 (5000 / 15000 ms) and goes to state 2; state 2 fades the music over 1000 ms, then `GameLogic.GotoFrontEnd` (disasm).

**OrbitCam** (`.\OrbitCam.cpp`, vtable 0x855cac; disasm):
- Activation 0x5310d0 sends `Input.EnableGroup` for WormAiming, WormMoving, CameraSelect, Fire, WormFirstPersonAiming, UtilityGirder, WormRoping and Flying: it re-enables input, it is not stopped by it. Nothing reads `Camera.Orbit.IgnoreInput`.
- Look-at: `Orbit.OverrideLookAt` if set; else `Land.Center` x, z and y = max((`Land.MaxHeight` + low) / 2, low), low = `Water.Level` (+0x64) + 20.
- Radius (+0x70): `Orbit.OverrideLandRadius` if set, else `Land.Radius` + 200 (0x5313fa).
- Start angle (+0x6c): atan2(d.x, d.z) + π, d = look-at − position of the logical camera before (0x91e8e8, written by SetCamera 0x51e8b4); 0 if d has no x, z.
- Update 0x5310b0: angle += dt · 0.1 (dt 0.02 s an update: 0.2 rad/s), then 0x530e30: position = (cx + sin a · R, look-at y + sin(0.8 a) · (look-at y − low), cz + cos a · R), with camera y and look-at y ≥ low. So the camera height swings around the look-at height; there is no fixed height.
- The orbit is therefore around the **level**, not a worm. The CAMTWK `Camera.Orbit.*` values are not used (§3).

### Concrete Donkey (disasm, 0x5538a8–0x553927)

- On release, the donkey logic starts **`DonkeyCamera`** directly. It is a SimpleCam, started through `0x51d760`, with PosUpdateSpeed 1 (locked) and LookUpdateSpeed 0.1.
- Camera position = spawn (x, y − Donkey.ExtraHeight 500, z + **500**) (0x5538d8: `fsub [esp+0x14]` is the ExtraHeight local), stored at +0x188 and handed out by 0x552f90; a fixed side view from +z, about 35 m from the spawn (disasm).
- Look-at = donkey + (0, 100, 0) units (0x552f60), smoothed by the CMS blend at LookUpdateSpeed 0.1; position blend 1 (disasm, data).
- SimpleCam places itself at its activation only: slot 2 0x531f70 sets up = (0, 1, 0), zoom +0x5c = 1 (the default lens) and calls the position 0x531e30 then the look-at 0x531d20; the per-update slot 1 0x532090 only re-reads the look-at and keeps its y ≥ Water.Level (Camera +0x44) − 40 units. So the camera never moves after the serve and looks down more and more as the donkey falls below it (disasm, vtable 0x855d40).
- Served once, by the donkey's Init (0x553922); priority 6 (+0x2c4, 0x51d8ac), pending request cleared (+0x350 = 0). Served while the worm is active, it slides into the PiP (0x51c000); the 0 ms PostLaunchDelay and RetreatTimeOverride 0 (WEAPTWK) end the turn a few messages later (Weapon.PostLaunchDelay → Timer.StartRetreatTimer → RetreatTimedOut → Lua EndTurn), so it grows to full screen at once (0x51e382). While the PiP is up its 6 drops the worm-track requests (3 / 5, 0x51d408); after the promotion the priority is 0 and a knocked worm's WormTrackCamera replaces it (disasm).
- Never Finished (slot 7 0x49b8f0); with the donkey gone the look-at lookup fails (0x531d4b) and the view holds (disasm).
- The `CameraId` `DonkeyTrackCamera` is never looked up: the donkey starts `DonkeyCamera` itself. "Named camera not found" (0x854608, pushed at 0x51eafd / 0x51ed5d) belongs to SetCamera 0x51e4e0 and is about camera **mode** names ("Track", "Orbit"…), not CAMTWK container names (disasm).

### Airstrike and Super Airstrike

- Normal airstrike missiles have an empty `CameraId` (data), so the bombs request no camera. The run itself is filmed by the bomber: `BomberLogicEntity` follows the bomber mesh's `perspShape` scene camera (`Camera.FollowSceneCam`) until `Bomber.AnimsComplete` (disasm, docs/w4m/targeting.md §10 "After firing: camera"; the earlier "seen from the current camera" reading was wrong).
- Worms that get knocked away trigger WormTrackCamera as usual: the request sits in `WXWormLogicEntity::ImpulseWorm going Ballistic` (0x5ad60b), which every impulse goes through (disasm).
- `StrikeChaseCamera` (CAMTWK, Chase, distance 300, CutOnRetreat) is unused (§3; data, disasm).
- Super Airstrike (cows on parachutes, `ParachutePayloadLogicEntity`) starts `SuperAirstrikeCamera`, a SimpleCam (1, 0.1): position locked, look-at smoothed (disasm 0x57a330).

### Fatkins

- `WeaponFactoryLogicEntity` writes `FatkinsTrackCamera` into the payload's camera name at runtime (0x5993e0), so the generic payload rule of section 1 applies.
- TrackCam then uses wide candidates, (±200, 100, ±500) around the event point, at distance 1000 / 500.
- The same factory code picks `HomingMissileChaseCamera` or `HomingMissileFlyCamera` for the factory homing missile (0x598dfa / 0x598e47).

### Alien Abduction (disasm, `AlienAbductionLogicEntity.cpp`)

- The UFO animation carries its own scene camera (`perspShape`, `Camera.FollowSceneCam`).
- `AlienAbductionCamera` is a SimpleCam (1, 1), fully locked to that scene camera, aimed at the abducted worm `m_uCameraWorm`.
- At the end comes `StopFollowingSceneCam` (0x547e1c, 0x5486ca).
- Scene camera framing (data, Bundl09 `AlienAbduction` clips, raw units round the beam axis, saucer body about y 10–50): AbductStart ends at persp (82.5, 21.8, 232) looking (−0.23, 0.14, −0.96), 247 units off the saucer at its height, 8° up, so with the 51.2° lens (§11.8b) the beam shows down to about 75 units under it, not to the ground 220 units below; AbductCloseBeam / Violate / OpenDoors from (26, −67.5, 94) looking 46° up at it; AbductLoop2 from (77, 285, 64) looking down (−0.98). The camera keys are smooth (no shake).
- The saucer itself moves (data): AbductLoop / AbductLoop2 bob it ±2.8 units (5.6 units peak to peak) with a 0.67 s period; AbductCloseBeam drops it about 45 units; AbductViolate jolts it about 60 units within 0.1 s. With the camera still, that is the shaking seen on screen. The model has no scale of its own: 20 units a metre like the world (no `Abduction.*` scale; WEAPTWK has only AreaOfEffect, AverageHeight, DistanceBetweenWorms, ExtraHeight, MaxHeight, MaxSpeed, NormalSpeed).

### Worm-track priority, 3 vs 5 (disasm, 0x51cf20)

- If the worm is above `Water.Level`, its ballistic path is swept against the land to predict a landing point.
- If no landing is found within the sweep horizon, the request gets priority **3**, with the event point at the end of the horizon. This happens for a worm flying off the map or into the sea.
- Otherwise, including a worm in water or at rest, it gets priority **5**, with the event point at the landing point.
- If the worm and that point are already on screen and in line of sight, the request becomes 4 or 6 and is dropped.

## 7. WEAPTWK camera field

The payload field is **`CameraId`**:
- index 43, struct offset 0xd0, type string;
- its descriptor is the one at 0x879114, registered at runtime;
- it sits between `ColliderFlags` and `PayloadGraphicsResourceID`.

## 7b. Per-event camera details (second exe pass)

Units are W4M world units (20 per metre). Labels: data = read in CAMTWK/WEAPTWK/tweaks, dis = deduced from the disassembly.

| Event | Camera | Placement / parameters | Source |
|---|---|---|---|
| Start of turn | `Camera.StartOfTurnCamera` (usually Default) | No cut if the same worm as last turn; otherwise cut onto the worm | dis 0x51ef80 |
| Game over | WormTrackCamera on MostRecentlyActiveWorm (else first alive), then Orbit after 4 s unless Script.NoOrbitCamera | Orbit look-at: Land.Center at max((MaxHeight+W+20)/2, W+20); radius Land.Radius+200 (or OverrideLandRadius); camera height look-at + sin(0.8 a)·(look-at − (W+20)), 0.2 rad/s (§6). Game over lasts 5 s (15 s with the online flag, §6). Fireworks: Land.Center ± 0.5 Radius, y = MaxHeight + rand*30, rand%40 per 100 ms | dis 0x4ff8d0, OrbitCam.cpp |
| Concrete Donkey | SimpleCam DonkeyCamera (pos 1, look 0.1) | Fixed at (tx, spawnY-500, tz+500), looks at donkey+(0,100,0); spawnY = ty + max(1500, MaxHeight+500); the ShakeStart on a smash is inert (Camera.Shake.Length / Magnitude 0) | dis 0x553791, 0x552f60, 0x552f90, data |
| Airstrike | none (empty WEAPTWK name) | Current camera stays; blasted worms trigger their own Track | data+dis |
| Super Airstrike | SimpleCam SuperAirstrikeCamera after the last bomb | pos = A - 200 d + 50 perp(d), d = dir from the first drop A to the last; looks at the payload | dis 0x58ae50 |
| Fatkins | Track FatkinsTrackCamera | dist 1000, MinPreferred 500, offsets ±200/100/±500, CutWhenStartOffScreen 0 | data |
| Alien Abduction | SimpleCam AlienAbductionCamera (1/1) | pos (UFO.x, Land.MaxHeight, UFO.z+200), looks at worm+(0,10,0); later worm+(0,50,50) if collision-free and >10 away | dis 0x547490 |
| Flood | SimpleCam FloodCamera (0.01/0.01) after 1400 ms | pos (cx, cloudY-200, cz+max(Land.Radius,3000)), cloud at Land.MaxHeight+450, looks at the cloud; FloodDuration 3000, RainDuration 4700 | dis 0x555730 |
| Mine factory | SimpleCam MineFactoryCamera (1/0.1) | look-at L = factory + (−8, 45, 0); camera L + (0, 50, 300) clipped by the land (0x51b040), kept only if > 30 units from L | dis 0x5cf930, 0x5cf910 |
| Starburst | generic payload camera, then FlyCam StarburstCamera after 3500 ms | LagBehind 80, LookAhead 100, LookSpeed 0.2, PosSpeed 0.05, UpSpeed 0.09, PosRate 0.1, Pause 1000, FinalDistance 500 | dis 0x5891e0, data |
| Super Sheep | Chase SheepChaseCamera (dist 170), FlyCam SuperSheepFlyCamera on take-off (as Starburst, PosRate 1.0), FallCam when the flight ends (look 0.08, pos 0.02) |
| dis 0x556aa0 |
| Homing missile | FlyCam HomingMissileFlyCamera for the whole flight | LagBehind 60, LookAhead 100, LookSpeed 0.1, PosSpeed 0.05, UpSpeed 0.1, PosRate 0.01, Pause 1000, FinalDistance 500 | data |
| Sheep / Old Woman / Scouser | Chase *ChaseCamera | dist 170, DefaultHeight 0.255 rad; Old Woman Min/Max height 0.25/1.3; OccHeightSpeed 0.4, OccYawSpeed 0.9 | data |
| Ninja rope | Ninja camera on attach | dist 400, height ±1.57 | data, dis 0x57222f |
| Girder | GirderCam | dist 325, DefaultHeight 0.6, MinZoomDist 100 | data |
| Drowning | no dedicated camera | the OccludingCam target stops 7 units (Worm.Drown.HeightOffset) under Water.Level (§5); HeadCam also loads the key (0x529780) | dis 0x52e6e0 |
| Crate drop | Track CrateTrackCamera, once per drop after Crate.DelayMillisec | dist 500, MinPreferred 200, ZoomSpeed 0.009, UpSpeed 0.2, CutWhenStartOffScreen 1 | dis 0x5cbd30, data |
| Explosion shake | CameraShakeManager | R = 8 * ImpulseRadius, magnitude = ImpulseMagnitude * (R-d)/R, duration = 800 * ImpulseMagnitude ms, random axis vector fading linearly, total clamped to Camera.Shake.Max 0.01 | dis 0x5241c0 |
| FlyCam end | hold | stays PauseDuration after the explosion, collision-checked | dis 0x528452 |

## 8. Open points

- TrackCam lateral axis: up × d = (d.z, 0, −d.x) (0x5326b9). The pairs are symmetric, so the side only changes which of a pair comes first.
- Game over: which online-session state the 15 s flag reports (§6): the interface is a runtime object, so the exe alone does not name it.
- Resolved in this pass: +0x1a8 (§1), the unused OccludingCam fields (§5), the orbit (§6), camera type 1 = HeadCam (§5).

## 9. Targeting weapons: Blimp view and reticle

The full W4M analysis is in `docs/w4m/targeting.md` §10. In short:
- The view is the normal **Blimp** (`IsometricCam`).
- The reticle is **fixed at the screen centre**. The player moves the camera's focus (MoveSpeed 250 × zoom units/s) and its yaw (RotateSpeed 0.55 rad/s).
- Each frame, a ray from the camera through the focus, against the land and then the water, gives `Airstrike.TargetPoint`.
- Bombers fly along the camera's horizontal right vector, so they cross the screen.
- The cursors:
  - **Bomber** (Airstrike, Super Airstrike, Fatkins): `Airstrike.Cursor.Mesh`.
  - **Targeting** (Donkey, Alien Abduction): `Targeting.Cursor.Mesh`.
  - **Homing**: its own cursor.
- Tint: white when the target is valid, light blue (98,168,255) on water, red with no target.

Cursor meshes and clips (Bundl09, read with a debug pass of `w4m-models`). Keys are (time s, value); channels 0x102 / 0x103 / 0x104 are translate / rotate / scale.
- **Targeting** (Donkey, Abduction):
  - A 100 × 100-unit plane, 3 × 3 vertices, `Target.tga`.
  - `Target_Intro` 0.42 s: scale 0 → 0.75 and z rotation -4.36 → 0 rad (it spins in). `Target_Loop` 3.33 s: one full turn. Its displayed size is therefore 75 units.
  - The shadow, `Targeting.Cursor.Shadow`, sits at the `Target_Shadow` node (2, -2), tint 0x645e2b02.
  - There is no column and no line toward the ground. The reticle is screen-space; it is neither projected onto the land nor oriented by its normal (`Airstrike.UpVector` is the camera's up vector).
- **Bomber** (Airstrike, Fatkins):
  - The `Aimer_Null` aimer is 58 units (`Airstrike_Outer.tga`). In `Airstrike_Intro` 0.67 s it pops in vertically and turns from 1.57 rad to 0. `Cursor_Loop` 0.83 s pulses its scale 0.80 ↔ 0.777.
  - Five arrows (`Airstrike.Cursor.Dot`), 16 units × 0.8, at x = -76, -45.6, -15.2, 15.2, 45.6.
  - `Dots_Loop` 0.42 s slides each arrow +30.4 units along +x (screen right). The first one grows from 0 and the last one shrinks to 0. The run therefore reads from left to right.

What the client does:
- `targeted(Kind)` (sim.h) covers Airstrike (both kinds), Donkey (Concrete Donkey and Fatkins), Abduction, Teleport and Homing. Homing (IsTargeting + IsHoming + Aimed + Powered): FIRE in the Blimp sets `Game::locked` / `lockAt` (W4M `Payload.Target`, state 1 → 0), then the view goes back to the normal aim (W4M `Weapon.CreateAimingCursor`) and the launcher is aimed and powered as usual; the press that locked does not charge. The missile homes on `lockAt`. Teleport is not a W4M weapon (its cursor is dead code there); it gets the Targeting cursor.
- **Entering the Blimp**: W4M `Input.BlimpView` is a toggle (DefaultCam 0x524e00 in, IsometricCam 0x529c00 out). Its defaults are "E" on PC (`FETXT.Control.Blimp`, LOCAL) and joypad button 3, i.e. Y on the 360 pad (DEFSAVE `Joypad.Input.BlimpView`, Type 2 Key 3).
  - Our mapping is a deliberate deviation, at the user's request.
  - [user-requested 2026-10-04] With a targeted weapon in hand, **Y** toggles the Blimp on the pad (the same button as W4M's 360-pad default); on the keyboard **Space** also enters it, swallowed until released so it does not fire. Hold L, and A / ZR as entry, are gone.
  - **ZL** leaves the Blimp for the first-person aim of a homing missile (lock kept). A single Joy-Con aims with ZL + stick. The perf overlay is hold - for 1.5 s.
  - In the Blimp, **A** fires (homing: locks the target) and **B / Enter** (or Y) leave without jumping. **E** toggles, as in W4M. L / R zoom out / in.
  - The toggle is client state (`Controls::targetView`). It is dropped as soon as no targeted weapon is in hand or the turn ends.
  - Fire never launches outside the Blimp (0x583a10). In the Blimp, a press with no target plays `FeError` in place of W4M's `weapons/Gong` (not imported).
  - Hints, as in `BlimpHelpEntity` / `WXFE.HelpBlimpConsole` (Look, Pan, Zoom in / out): "Fire / Lock target", "Look", "Pan", "Zoom" and "Leave" in the view. Outside it: "Sky view: target" and "Hold: sky view".
- **The CPU** uses the Blimp only while its plan executes a strike, as in W4M, which calls `SetCamera("Blimp")` from `AIActionSetStrikeTarget::ApplyActionInner` (0x4b4c70) and `AIStrike.SeekTarget` (0x4b5a90). That is `Ai::striking()`: mode Act with a targeted plan weapon. It is not shown during instant replays or match playback. The team's reselected weapon alone no longer opens the view: it used to bring up the reticle at random moments. `ai_check` tests this.
- **Controls**. [user-requested] Deliberate deviation: the sticks are the other way round from W4M HelpBlimpConsole (Movement = Look, Camera = Pan).
  - Left stick: pan at 250·zoom u/s. Right stick: yaw (RotateSpeed 0.55·s) and pitch (PitchSpeed 0.45·s), with s = 0.9 + 0.1·zoom. L / R: zoom 0.15–2 (user-requested pad mapping) (FOV only, client-side).
- **Pitch range** [0, π/2], as in W4M (0x52a5e0). Pitch 0 is a horizontal view and π/2 looks straight down: the camera sits at focus + R(pitch, yaw)·(0, 0, -StickLength), with DefaultPitch 1 tilted down (0x52a0a0), and our `blimpEye` uses the same convention.
  - The camera's up vector is R(pitch, yaw)·(0, 1, 0) (W4M 0x52a0a0).
  - A fixed (0, 1, 0) up made the view matrix degenerate when looking straight down, and the screen filled with the fog colour (the "yellow screen").
  - The compass and the wind arrow take their heading from forward + up for the same reason.
  - At pitch 0 the centre ray can miss everything within reach: no target, red reticle, Fire refused. That is the W4M rule, but the reach we use is a simplification: 200 m in our build versus Land.Radius + max(Land.Radius, distance) in W4M.
  - Desktop: WASD look, arrows pan, X/Z zoom.
- The camera is exactly the sim's: `Game::blimpEye(cursor, cursorYaw, cursorPitch)` looking at `Game::cursor`, so the reticle (`Ui::targetCursor`) is the screen centre.
  - Entry pose (0x52ad20): focus.y = max(worm.y, highest land) + 6 units, moved back so that the centre ray hits the worm.
  - The focus stays within 4500 units (225 m) of `Land.Center`, a 3D distance to the whole vector: the handle at +0xd0 is `Land.Center` (constructor 0x52a518), read and compared at 0x52abd8–0x52ac06 (disasm). Ours: `Game::landCenter()`.
- The tint is white, light blue on water, and red with no target. With no target (the ray misses land and water within 200 m, possible at low pitch), the sim refuses Fire.
- Reticle size: HUD units, screen height / 480. HUDTWK places HUD items in a 480-unit-tall space, e.g. `HUD.AngleMeter.ScreenY` and `HUD.Powerbar.Position` y = -165, and the cursor obeys the HUD hide flag (0x552241). That the cursor uses the same layer is still assumed: its mesh is instanced by XGraphicalResourceManager vfunc 0x8c (0x6f3d95, layer argument 0xff, as `HUD.PiP` at 0x635de9) and never moved (0x552cfc); which layer 0xff selects lies in the XOM scene-graph runtime, not traced.
- **Input** (net and replays): the 4-byte `Input` is unchanged. While `Input::TARGET` (bit 64) is set:
  - `turn` yaws the Blimp at up to `BLIMP_TURN`, and the worm does not turn.
  - `walk` moves the focus forward at up to `CURSOR_SPEED` 25 m/s.
  - `aim` moves it right at the same speed. With `Input::PITCH` (bit 128) set, `aim` instead tilts the camera at up to `BLIMP_TILT` 0.495 rad/s.
  - When the player moves sideways and tilts at once, the client alternates the two on successive ticks at twice the rate.
  - The worm neither walks nor pitches.
- `target()` is then the land or water hit of the camera ray through the focus. The airstrike runs along `strikeDir()`, the view's right vector: W4M takes `Airstrike.Direction` × `Airstrike.UpVector` (view forward × up, the right vector in the renderer's right-handed OpenGL frame), y set to 0 (0x54d931–0x54d97f, operands at 0x54d7e4 / 0x54d812), and the Bomber cursor's `Dots_Loop` slides its arrows toward +x (data). Disasm + data.
- **Fatkins** is a Bomber payload, as in W4M (BomberLogicEntity → FatkinsStrikePayload, 0x54ddf0). `Game::fatkinsDrop` releases it 25 m up with the plane's ground speed (`Bomber.GroundSpeed` 7.5 m/s) along the strike direction, early enough to land on the target.
  - The direction is the view's right vector in the Blimp, otherwise the worm's facing.
  - The AI's prediction (ai.cpp) uses the same function.
- The state (`cursorOn`, `cursor`, `cursorYaw`, `cursorPitch`) is reset at each turn. The checksum mixes it only while `cursorOn` is set, so old replays and the CPU, which never sends TARGET, are unchanged; the one exception is Fatkins, whose path changed for every user. The CPU's view centres the Blimp on its own aim point (the W4M AI uses the Blimp too).

## 10. NinjaCamMkIII, FlyCam fields, abduction close shot, homing cursor (disasm, this pass)

- **NinjaCamMkIII** (vtable 0x855a18) overrides OccludingCam slots 9 and 10.
  - Slot 9, 0x52d720, replaces the occlusion test: if there has been no camera input for 60 ms (0x51b540) and the camera spot is in land (0x52d140: land segments of 5 units from it along +z, +x and +y), 0x52d250 tries the yaw offsets of table 0x91eaf0 (±π/8, ±2π/8 … ±7π/8, then zeros), 8 an update, at the same pitch and `DistFromObject`; the first clear one becomes the yaw and position, and the index (+0xec) resets. It is never reset otherwise.
  - Slot 10, 0x52d3c0: distance = d·(1 − OccZoomOutSpeed) + DistFromObject·OccZoomOutSpeed each frame (0.04), position and look-at heights ≥ water + 5. There is no zoom-in at all (`OccZoomInSpeed` 0).
- **FlyCam fields** (0x527a90 copies the container into the camera): UpSpeed → +0x54 (the up-vector lerp), LookSpeed → +0x4c, PosSpeed → +0x78, PosRate → +0x7c, LagBehind → +0x6c, LookAhead → +0x68, MinPosition → +0x70, MinLookAt → +0x74, FinalDistance → +0x80, PauseDuration → +0x8c, Cut → +0x58; the position lerp +0x50 starts at 0.
  - Update 0x528240: `+0x50 = +0x50·(1 − PosRate) + PosSpeed·PosRate` every frame, so PosRate is how fast the position factor reaches PosSpeed (homing 0.01, Starburst 0.1, Super Sheep 1).
  - 0x527b00: up = the payload's local up axis (its transform's second column), position = payload − LagBehind along its forward axis, look-at = payload + LookAhead; a land hit between look-at and position pulls the position 10 units in front of it. UpSpeed is therefore the rate at which the camera rolls with the payload.
  - On the payload's death the position is set to position + FinalDistance along look-at → position, clipped by land (0x51abf0), held PauseDuration.
- **AlienAbductionCamera** position (0x547490, through SimpleCam 0x531e30 at each activation only: 0x5486ca at the lift, 0x547e1c once spat out; the update 0x532090 never re-places it): (UFO.x, Land.MaxHeight, UFO.z + 200); while the abduction state (+0x44) is 2, the state set with `Worm.OverridePhysics` and the camera start (0x548578 … 0x5486ca), worm + (0, 50, 50) clipped on the worm → candidate segment (0x51af90), kept if more than 10 units from the worm.
- **"Worm Dying" 0x5a7190** (slot +0x5c, then the 0x51cf20 request) is called by Worm.TimeToDie (0x5adcb6) and by the drowning check 0x5ad640 (0x5ad83d, 0x5ad8bc) before ChangeState 8 DrownFloat: a drowned worm gets the same request at the moment it drowns; under Water.Level it is requested at its own position, priority 5 (6 in clear view, dropped) (disasm 0x51d037, 0x51d2cb).
- **Worm-track requests during the death queue**: "Worm Dying" (0x5a7282), "Worm Displaying Damage Taken" (0x5abeec) and "ImpulseWorm going Ballistic" (0x5ad60b) all call 0x51cf20(worm). When served (0x51d3d0), a request whose priority is below the running track's (+0x2c4) is cleared, not kept (0x51d408).
- **Homing cursor** (Bundl09): `Homing.Cursor.Mesh` = node `Inner` with 4 quads (Inner_01 top, 04 bottom: 18 × 54; 02 left, 03 right: 54 × 18; one row each of texture `maya:file7/-1` #3, exported as `fe2/homing_inner`); `Homing.Cursor.SquareMesh` = node `Outer`, locator1-4 at (∓50, ±50) carrying the bitmaps `HUD.Homing.Cursor.TL/TR/BL/BR` (0x560690). Clips: Intro_Inner, Loop_Inner, Intro_Outer, Loop_Outer, Lock_Outer, Error_Outer (keys in docs/camera.md). The LockOn tints its 4 corners each frame before the lock (0x560590 → 0x552340); after `HUD.Target.Selected` (0x560420: Lock_Outer, `weapons/LockOn`) it stays on the stored target point (0x5600e0: on the camera → target ray at 500 units, i.e. screen-constant). The Inner mesh is never tinted (HomingCursorGraphicEntity uses the base per-frame 0x552230). A bitmap keeps its descriptor size (128 × 128, drawn ±64) times the locator's scale: the XOM sprite Size is half the descriptor size and nothing sets these corners' scale (docs/w4m/render.md, XBitmap quad size) [disasm].

## 11. Drawn view, update rate, PiP rule, scene cameras (this pass)

Labels per row: **data** (CAMTWK / tweak value), **disasm** (read in the code), **assumed** (inferred, not verified).

### 11.1 Update rate

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Who runs the camera manager | `ZCamUpdateFudgeService` (a LogicEntity, vtable 0x8561c0): its update (vtable +0x18, 0x533c90) calls the CMS update 0x51da00 **twice** | disasm | 0x533ca3, 0x533cab |
| How often | the update returns 20 (the CMS update returns 0x14 too, 0x51e3e6): the frame task queue (0x68d91f, vtable +0x18 per due task, the return value is the delay to its next run) runs it again 20 ms later; the queue is fed the time rounded up to a multiple of 20 ms (0x68d57a / 0x68d5b1, from 0x68d4d4), so at most one run per 20 ms | disasm | 0x68d91f, 0x68d57a |
| Consequence | every per-update factor below (blend, lens, camera internal lerps) is applied 100 times a second | disasm | — |
| dt given to each camera | 0.02 s (`fld 0.02` before each camera vtable +4 call) | disasm | 0x51da9a, 0x51dafd, 0x51db1b |

### 11.2 Drawn view (CMS 0x51da00 → 0x51b940)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Blend | the render camera goes from its last position / look-at / up to the logical camera's by the camera's factors +0x50 / +0x4c / +0x54: drawn = drawn·(1 − k) + logical·k | disasm | 0x51b940 |
| Cut flag | camera +0x58 (byte): set, the render camera takes the logical view as is and the flag is cleared ("Cutting Current Camera") | disasm | 0x51dc8d, 0x51dcba |
| Cut flag writers | base Camera constructor 0 (0x51b654); turn start (0x51f22e, below); FlyCam's `Cut` field (0x527af4; every FlyCam container has Cut 0, data); nothing else | disasm, data | xref +0x58 |
| PiP camera | the PiP's render camera (CMS +0x2b0) is blended the same way (0x51df61), with its own Cut flag (0x51df4a) | disasm | 0x51df36–0x51df7c |
| Scene camera (FollowSceneCam) | displayed type 0x14 / 0x10 skip the blend (0x51db3d); after it the render camera starts from the scene view | disasm | 0x51db3d |
| Retry loop | the blended view is passed to camera vtable +0x18; true: if not within 0.001 of the logical position, the factors are raised by their own value (capped at 1) and the blend recomputed, up to 20 times; after 20 failures the outputs are not written, so the logical view is used | disasm | 0x51bc4d–0x51bccd |
| vtable +0x18 | 0x52e870 for OccludingCam, DefaultCam, ChaseCam, GirderCam, NinjaCamMkIII; 0x51ab90 (returns false) for Camera, FlyCam, FallCam, SimpleCam, TrackCam, OrbitCam, IsometricCam, HeadCam, JetpackCamMkII | disasm | vtables +0x18 |
| 0x52e870 | false when |distance +0xe0 − desired +0xe4| ≤ 1 unit (so never during a zoom-in, which snaps); else with A = blended position, B = blended look-at, h = (sin yaw, 0, cos yaw) of the camera yaw +0xd4, r = normalise((B − A) × h): true if either diagonal of the square A ± OcclusionSize·h ± OcclusionSize·r (OcclusionSize +0x78 of the container, 10 units) has land (0x466a20 segment tests) | disasm (symbolic x87 trace) | 0x52e870–0x52ed62 |

### 11.3 Factors per camera (+0x50 position, +0x4c look-at, +0x54 up)

| Camera | Factors | Label | Source |
|---|---|---|---|
| Camera (constructor; kept by TrackCam) | 1 / 1 / 1 | disasm | 0x51b65a |
| OccludingCam family (Shoulder, Girder, Chase cameras) | PosUpdateSpeed / LookUpdateSpeed / 1, written every update; all CAMTWK occluding containers have 0.1 / 0.1 (UpUpdateSpeed 1, not read) | disasm, data | 0x530953, 0x530960, 0x53096b |
| NinjaCamMkIII | PosUpdateSpeed / LookUpdateSpeed / 1 (NinjaCamera 0.1 / 0.1) | disasm, data | 0x52d563, 0x52d56c |
| SimpleCam | container +0x14 / +0x18 = PosUpdateSpeed / LookUpdateSpeed; up stays 1. AlienAbduction 1 / 1, Donkey 1 / 0.1, MineFactory 1 / 0.1, SuperAirstrike 1 / 0.1, Flood 0.01 / 0.01 | disasm, data | 0x53205b, 0x532067 |
| FlyCam | up = UpSpeed (+0x2c), look = LookSpeed (+0x1c), position 0 at activation, then +0x50 = +0x50·(1 − PosRate) + PosSpeed·PosRate each update | disasm, data | 0x527aad, 0x527ab3, 0x5282fb |
| FallCam | LookSpeed / PosSpeed / 1 (FallCamera 0.08 / 0.02) | disasm, data | 0x5277d0, 0x5277de |
| OrbitCam | 0.1 / 0.1 / 1 | disasm | 0x530fe8 |
| IsometricCam (Blimp, Spectator) | `Camera.Blimp.UpdateSpeed` (0.05, asserted in (0, 1]) for all three | disasm, data | 0x52a57d–0x52a58a |
| HeadCam | position 0.15 while the worm's physics state (+0xf0) is 0 (kWPS_Ambulatory), else 1; look 1; up 1 | disasm | 0x529024–0x529043 |
| JetpackCamMkII | position 0 at activation (0x52b03d), then +0x50 = 0.9995·(+0x50) + 0.0005·(+0x7c) each update; the constructor 0x52b270 reads `Camera.Jetpack.PosUpdateSpeed` 0.995 into +0x7c, `LookUpdateSpeed` 0.2 into +0x4c and `UpUpdateSpeed` 0.2 into +0x54 (0x52b2f9–0x52b336), never rewritten | disasm, data | 0x52b6df–0x52b6f9 |

### 11.3a HeadCam placement and the held weapon (first-person aim)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Constructor handles | +0x64 `Worm.EyeLevelOffset` (15), +0x6c `Worm.Drown.HeightOffset` (7), +0x68 `Camera.Head.LookUpdateSpeed` (0.1), +0x70 / +0x74 / +0x78 / +0x7c `Camera.Head.MinZoom` / `MaxZoom` / `ZoomSpeed` / `MouseZoomSpeed` | disasm, data | 0x5296d0 (0x52976d–0x5297e8) |
| Entering / leaving HeadCam | only `DefaultCam`'s handler of `Input.FirstPersonPressed` calls `SetCamera("Head")` (0x524e87-0x524ec9); HeadCam leaves for `Default` on `Input.FirstPersonReleased` or `GameLogic.EndTurn` only (handler 0x528ca0, 0x528cd6-0x528d1a). No weapon charge or fire changes the logical camera, so HeadCam stays after the shot and the TrackCam runs in the PiP | disasm | 0x524e00, 0x528ca0 |
| Activation 0x529130 | position = worm Position, y raised to Water.Level − `Worm.Drown.HeightOffset` if lower, + `Worm.EyeLevelOffset`; when not the AI's turn (0x51abd0 → 0x4b3340) and the worm is Ambulatory (+0xf0 = 0): d = the last view's vector (0x91e8e8, look-at − position of the logical camera before, written by SetCamera 0x51e8b4), the worm's Orientation set to (0, its heading, 0) (0x519120: acos of z over the xz length, negated for x < 0) and `Weapon.ResetAimingCursor` posted; otherwise d = the facing (sin, 0, cos) of Orientation.y. +0x8c = d (not normalised), look-at = position + d | disasm | 0x5291d2–0x5293d6 |
| Position | the worm's Position (+0x38 of its logical object, the feet: the drown test of 0x5ad6aa reads it as the feet) plus (0, `Worm.EyeLevelOffset`, 0): 0.75 m over the feet, the point a Bazooka / Grenade / gun shot leaves (§weapons.md "Launch point"); no offset along the aim | disasm | 0x528ed1–0x528eeb |
| Look-at | position + the offset +0x8c: each update +0x8c = 0.9·(+0x8c) + 0.1·d, d the unit direction (1 unit) of yaw Orientation.y (+0x90) + GunWobbleYaw (+0xe0) and pitch WeaponAngle (+0xd0) + GunWobblePitch (+0xd8); 0.1 is `Camera.Head.LookUpdateSpeed` (+0x68) | disasm | 0x528eb5–0x529071 |
| Worm alpha | `max(0, 1 - |v|^2 / 0.016)` (v in units/ms): 1 at rest, hidden from 6.3 m/s (§5 "Active worm fade"); the logical camera's type is read (CMS +0x28c), so it holds under a served event camera too; the value goes to the post-process (0x61fe90 → vfunc +0x24 = 0x61c4f0, a float at +0x10), one a frame | disasm | 0x5a446b–0x5a45b4 |
| WEAPTWK `DisplayInFirstPerson` | 1: Bazooka, Grenade, Cluster Grenade, Banana Bomb, Holy Hand Grenade, Gas Canister, Homing Missile, Poison Arrow, Shotgun, Sniper Rifle; 0 for every other weapon, utility and Factory weapon | data | WEAPTWK BaseWeaponContainer +0x78 |
| WEAPTWK `FirstPersonOffset` / `FirstPersonScale` | Offset (0,0,0): Bazooka, Homing Missile, Sniper Rifle, Cluster Bomb; (1.0,-0.2,0): Grenade, Cluster Grenade, Banana Bomb, Gas Canister; (0.8,-0.2,0): Holy Hand Grenade; (0.3,-0.2,0): Shotgun; (0,-1,-0.5): Poison Arrow. Scale (0,0,0) for every weapon | data | WEAPTWK BaseWeaponContainer +0x14 / +0x20 |
| Readers of those three fields | `FirstPersonOffset` (+0x14): WeaponAccessoryEntity 0x594b50 → 0x5a1940 (below). `DisplayInFirstPerson` (+0x78) and `FirstPersonScale` (+0x20): no reader found (no byte read of +0x78 in 0x4f0000-0x640000 outside Bubble / HUD structs; message `Weapon.1stPersonGraphicCreated` has no handler object) | disasm | +0x78 / +0x20 assumed unused |
| Render bin 31 | `FirstPersonWeapon`, drawn after `LensFlare` and before the HUD (§render.md bins); no code found that selects it (no `push 0x1f` scene argument in the weapon or worm code, the held meshes' descriptors carry scene 8 `3D`: XMeshDescriptor trailing byte of `Bazooka.Weapon`, `Grenade.Weapon`, `Shotgun`, `SniperRifle` in Bundl09 is 8): the first-person weapon is the worm's own held mesh in the FP pose | data, disasm | bin 31 assumed unused |
| Held mesh | raw units (w4m-models `hold_*`, scale 0): Bazooka.Weapon 10.5 x 16.4 x 24.8 units (0.52 x 0.82 x 1.24 m at 20 units a metre), Grenade 10 x 11 x 10.6, HomingMissile 10 x 18 x 24.7, Sniper 7.5 x 16.2 x 25, Shotgun 4.7 x 10.1 x 21.5; attached at the worm's `WeaponLocator` bone, scaled by the skeleton chain | data | Bundl09 |
| First-person mode (WeaponAccessoryEntity HM 0x597630, `Camera.HasUpdated`) | on every camera update, with an active worm ([0x95fb04], entity byte +0x50 ≠ 0x7f): fp = \|drawn camera position (0x4d5210 vfunc 0x38) − (Position + (0, 10, 0))\|² < 100 (0x4711c0 is the squared distance), i.e. within 10 units (0.5 m) of the point 10 units over the feet; the HeadCam eye (15 units) is 5 units from it. Then 0x594b50(fp) | disasm | 0x597680–0x597709 |
| 0x594b50(fp) | +0xbc = fp; worm clip `FP` at time 0, weight fp (0x5a1570 → 0x6a0790 on the worm node); fp: worm clips FPX / FPY / FPZ (graphic +0x104 / +0x108 / +0x10c) at time FirstPersonOffset.x / .y / .z + 1, weight 1 (0x5a1940 → 0x6a04c0), and the Aim slot (+0x130 / +0x138) takes the `AimFP` clip (+0x124 / +0x12c) while the AimFP slot takes the WEAPTWK Aim (+0x120 / +0x128); not fp: FPX..FPZ at 0x96e878 + 1 (bss, 0 → time 1), the slots back. With fp and the accessory holstered (state +0xb0 = 0) it sends itself `Input.TauntPressed` (vfunc +0x78) | disasm | 0x594b50–0x594d3a |
| `AimFP` slot | every WAE init loads the name `AimFP` into +0x124 (Standard 0x59049d, Thrown 0x59635e, Starburst 0x591271, 0x58d9aa); the AimFP slot (+0x134) is always given weight 0 by the pose updates (0x58f620: five sites), so only the swap plays it | disasm | 0x58f6e4, 0x58f7a9, 0x58f843, 0x58f9da, 0x58fb99 |
| First-person clips (Bundl474 W4.Worm, `w4m-models --list`) | `FP` 0.04 s: main translate z −30 units, both shoulders translate z +30 (the body goes 1.5 m back, the arms stay). `AimFP` 2 s: both shoulders translate (−8.18, +9.56, +9.55) units and rotate x +π/2 → 0 → −π/2 at t 0 / 1 / 2 (the aim pitch), wrist_left translate y −9.56, wrist_right +9.56. `FPX` / `FPY` / `FPZ` 2 s: both shoulders translate x / y / z from −10 (t 0) to +10 (t 2), 0 at t 1 | data | Bundl474 |
| WEAPTWK `FirstPersonOffset` (all non-zero) | Grenade, Cluster Grenade, Banana Bomb, Gas Canister (1, −0.2, 0); Holy Hand Grenade (0.8, −0.2, 0); Shotgun (0.3, −0.2, 0); Poison Arrow (0, −1, −0.5); Ninja Rope (0.3, 0.2, 0); Skip Go (0.8, 0, −0.5); Airstrike (0.5, 0, 0.5); Baseball Bat (0, 0, 0.5); Dynamite (−0.5, 0.2, 0.5); Fire Punch, Prod (0, 0, 1); Flood (0, 1, −0.2); Landmine (0, 0.2, 1); Old Woman, Bubble Trouble (0, 1, 0); Scouser (0, 1, −0.2); Sentry Gun (0, 0.5, 0); Sheep, Super Sheep (0, 1, −0.5); every other 0 | data | WEAPTWK |
| Aim reticle (AimedWeaponGraphicEntity, vtable 0x857164) | spawned by GraphicalSpawningService on `Weapon.CreateAimingCursor` (0x500aa9) as a child of the sender, the weapon logic (PayloadWeaponLogicEntity 0x582f7a / 0x5861c1, Gun 0x55d3e5, Melee 0x568d05); its init 0x5446e0 builds `Weapon.FPSCursor.Mesh` (`Weapon.Parabolic.Mesh` when `Weapon.ParabolicRetical`) and posts `Weapon.AimedLogicStart`; its destroy (msg 0x42, 0x544360) posts `Weapon.AimedLogicEnd`. Each update 0x5446d0 → 0x5445e0 shows the three instances only while 0x51cea0 reports type 1 (HeadCam) and +0x34 is 0 (only the constructor writes it). 0x51cea0 reports the event camera's type once one is served (+0x2ac set and +0x2c0, which every serve sets to 1 unless the jetpack bit or a retreating moving worm), else the logical camera's. So after the shot the reticle goes at the first served event camera (TrackCam, chase) and at the latest with the weapon logic: 0x549bb0 posts `Weapon.Delete` at the end of PostLaunchDelay (Bazooka 500 ms, Shotgun 1000, Sniper 2000), with `Timer.StartRetreatTimer` | disasm | 0x5445e0, 0x544af0, 0x500ad2, 0x51cea0, 0x5833a0 |
| HUD after the shot | `HUD.AngleMeter.OnScreenX` = `OffScreenX` = −215 (its AimedLogicStart / End slide moves nothing); `HUD.Powerbar.MaxAlpha` = `NormAlpha` = 255 (FadeTime 500 changes nothing): both stay as they are; the HUD clock (HudClockEntity 0x5f0ae0) binds RoundTimeRemaining, TurnTimeRemaining, HotSeatTimeRemaining, never RetreatTimeRemaining (only TimerService 0x50f87e and the CMS 0x51ff68 read that key) | data, disasm | HUDTWK, 0x5d8f8e, 0x5f0ba8 |
| **Observed, real W4M (Worms Ultimate Mayhem PC 1920x1080, first-person Bazooka, user capture 2026-10-07)** | the Bazooka is drawn big on the right edge, cut by it: x 1370-1920 of 1920 (the right third), y about 120-1080 of 1090; its muzzle-side rings face left; the worm-coloured hand shows above the tube at the top right (x 1540-1920, y 30-470); no body or head; crosshair at the centre, view pitched about 10 degrees up. The FP pose above (FP + AimFP, the worm drawn whole at alpha 1 from the eye, near 2.5 units) gives that picture | observed | user capture of the real game |
| Locator chain | the held mesh is a plain child of the worm's `WeaponLocator` node: 0x5a15d0 calls vfunc +0x60 of the worm graphic node (+0x24) with (mesh, 'WeaponLocator'), no other offset or scale; the worm's mesh node sits at Position + (0, 3, 0) units (`WXWormGraphicEntity` 0x5a00b0, constant 0x8c1578) | disasm | 0x5a15d0, 0x5a00b0 |

### 11.3b On-screen test (0x51b3b0)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| On screen | the point times the drawn camera's view matrix (0x4d5210 vfunc 0x1c) then projection (vfunc 0x10); true when x/w and y/w are both in [−1, 1] and w > 0: the screen rectangle, not a cone. The camera is always the drawn one, whoever asks | disasm | 0x51b3b0–0x51b533 |

### 11.4 Turn start (0x51ef80)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Cut flag at turn start | set unless the new active worm's position is on screen (0x51b3b0) and the segment from the render camera to it is clear of land (0x51abf0) | disasm | 0x51f149–0x51f22e |
| 0x51abf0 | returns true when the land segment test 0x466a20 finds nothing (clear) | disasm | 0x51ac21–0x51ac2d |

### 11.5 Lens (CMS 0x51e150, inside 0x51da00)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Lens | z = the current logical camera's zoom +0x5c (1 with none). z ≠ 1: the drawn camera's projection eases toward the CMS default projection (+0x2d0) × z, projection·0.9 + default·z·0.1 an update (tan of the half field of view); same for the camera at [0x95a100]+0x14 (default +0x2ec), copied to +0xc. z = 1 after a different z: the default projection at once, no ease back (0x51e0d0–0x51e124); z = 1 again: nothing. The last z is kept at 0x95c6e4 (0x51e2f5). Users: the HeadCam zoom (binoculars, 0x91f31c) and the Blimp zoom (Camera.Blimp.MinZoom 0.15, MaxZoom 2, ZoomSpeed 0.99, MouseZoomSpeed 0.08) | disasm, data | 0x51e06d–0x51e2f5 |
| Default projection | CMS +0x2d0 is copied from the drawn camera at CMS init (0x520017) and restored on reset (0x520dd6). An XCamera projection is {l, r, b, t, near, far, ortho} at unit distance (0x6e1bd6); from a scene camera, r = 0.5·25.4·Aperture.x / FocalLength, t = 0.5·25.4·Aperture.y / FocalLength (0x6e1f46). The in-game drawn camera starts with t = −b = 0.48, r = −l = 0.48 × width / height, near 2.5, far 15000 (0x4d8067..0x4d84cc, the same set on [0x95a100]+0x14 and +0x1c): 2·atan(0.48) = 51.28° vertical, kept on any aspect | disasm | 0x520017, 0x4d8067, 0x4d8081 |
| One lens for all | the lens is the drawn camera's projection (0x4d5210 vfunc 0x24 / 0x28), shared by every logical camera; a camera only supplies its zoom +0x5c | disasm | 0x51e143–0x51e1e9 |

### 11.6 Event camera type (+0x2c0)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Track serve 0x51d3d0 (in 0x51d360) | +0x2c0 = 1; 0 if PhysicsOverride bit 0; 0 if `RetreatTimeRemaining` (+0x258 of the CMS is its handle, 0x51ff68; TimerService 0x50f87e counts it while the retreat timer runs) > 0 and the worm's Velocity (+0x50) ≠ 0. A walk step sets Velocity to InputImpulse (+0x68, 0x5b1471 / 0x5b1899 via 0x546f10), the idle branch zeroes it (0x5b0ec5, 0x5b1c1b): a walking worm counts as moving | disasm | 0x51d52d–0x51d58c |
| Chase serve 0x51d5e0 | +0x2c0 = 1; 0 only if PhysicsOverride bit 0 (no retreat test); priority 6 (+0x2c4); then PiP.SlideOn 0x51c000 | disasm | 0x51d6ce–0x51d73e |
| Abduction / SimpleCam serve 0x51d760 | as the track serve (bit 0 at 0x51d870) | disasm | 0x51d846–0x51d879 |
| Messages | Timer.RetreatTimedOut → 1; Camera.Cancel → 0 (when 0x5b40f0 and +0x2c1 clear); 0x523acd / 0x523af7 other cases | disasm | 0x522710 (0x523a4b–0x523afd) |
| PhysicsOverride | WormDataContainer +0xe8 (u32, schema field 17); `Worm.OverridePhysics` (value, mask): a positive mask is ORed (0x5ae0d4), a complement ANDed (0x5ae115) | disasm | 0x5adcf0 |
| Bit 0 (1) | jetpack: set on take-off (JetpackUtilityLogicEntity 0x562270, 0x56246f), cleared (−2) at 0x5629d4, 0x562ffc, 0x56329a, 0x563b91 | disasm | — |
| Bit 1 (2) | parachute: set 0x579182, cleared (−3) 0x5788d9, 0x578e75 | disasm | ParachuteLogicEntity |
| Bit 2 (4) | Starburst: StarburstLogicEntity 0x588580 (reached from its handler 0x588c10) | disasm | 0x5885b6 |
| Bit 3 (8) | ninja rope: set 0x573e55, cleared (−9) 0x572703, 0x5736aa, 0x5739c7 | disasm | NinjaRopeUtilityLogicEntity |
| Bit 4 (0x10) | walking payload: set 0x593292, cleared (−0x11) 0x5929ac | disasm | WalkingPayloadLogicEntity, PayloadLogicEntity |
| Bit 5 (0x20) | melee (Fire Punch…): set 0x568356 / 0x569e1b, cleared (−0x21) 0x569898 / 0x569d7c | disasm | MeleeWeaponLogicEntity |
| Bit 6 (0x40) | crate / telepad / GameLogic: set 0x4f770e, 0x5cab7f, 0x5d33e1; cleared (−0x41) 0x4f7785, 0x5d45b3 | disasm | — |
| Bit 7 (0x80) | alien abduction: set 0x54860e, cleared (0xffffff7f) 0x547ba9 | disasm | AlienAbductionLogicEntity |
| Worm (−0xc) | 0x5ab343 ANDs with ~0xb: clears bits 0, 1 and 3 | disasm | WXWormLogicEntity |
| Only bit 0 reaches the camera | the three serves test `+0xe8 & 1` only | disasm | 0x51d558, 0x51d6f8, 0x51d870 |

### 11.6b PiP lifecycle (what puts an event camera in the PiP and what grows it)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| CMS flags | +0x2ac event camera; +0x2b0 the PiP camera (a copy of +0x2ac at each serve, 0x51d505 / 0x51d72c / 0x51d8a4); +0x2c2 PiP up; +0x2c8 PiP.GoFullScreen sent; +0x2c9 PiP.GoneFullScreen received (0x522892); +0x2ca PiP.SlideOn sent; +0x2b4 non-zero blocks both cameras | disasm | 0x51c000, 0x51c0b0, 0x51c160, 0x522710 |
| [0x95fb04] | the active worm (its entity at WXWormLogicEntity +0x20): set by Activate(true) and cleared by Activate(false) (0x5a4170, from 0x5aaac0 at 0x5ab3aa). Activate(false) runs on GameLogic.EndTurn (WXWormManagerService 0x5b5e70 → 0x5b2610, after Worm.CleanUpOnDeactivate), UnspawnWorm and the Skip Go weapon (0x588090). GameLogic.EndTurn is sent by the Lua EndTurn: retreat timed out, turn timed out, current worm hurt, fewer than two teams (docs/w4m/turn.md) | disasm | 0x5a4240, 0x5a4280, 0x5b61b6 |
| SlideOn 0x51c000 | every serve (track 0x51d512, chase 0x51d73e, simple 0x51d760) calls it; it sends PiP.SlideOn only if a worm is active ([0x95fb04] ≠ 0), +0x2c2 = 0 and +0x2ca = 0, then +0x2c2 = +0x2ca = 1, +0x2c8 = +0x2c9 = 0. So every event camera served during a turn (aiming, flight, retreat) starts in the PiP, whatever the worm does; one served after EndTurn (counts, deaths, crates of the next turn's post-activity) is full screen | disasm | 0x51c016–0x51c097 |
| Main view 0x51c6e0 | the graphical camera is +0x2b0 when the PiP is not up or has gone full screen (+0x2c2 = 0 or +0x2c9 = 1), else the logical camera (+0x28c, the worm's Default / Head / Ninja…). Before that: +0x2b8 (followed scene camera) and the "Orbit" logical camera win. The PiP render camera ([0x95a100]+0x1c) is blended from +0x2b0 when +0x2b0 is not the main one (0x51df36–0x51df61) | disasm | 0x51c7b0–0x51c7df |
| +0x2c0 | only the type that 0x51cea0 reports (the event camera's type when set, else the logical camera's, 0x4b3cc0). Its readers test for HeadCam (1), Blimp (3) or 0x13: HUD help (0x5db8e0, 0x5dcb80, 0x5ef620), weapon models (0x5445e0, 0x5f8990), 0x551fa0. It does not choose the drawn camera: the serves' jetpack / Velocity tests (§11.6) only change that type | disasm | 0x51cef8–0x51cf10, xrefs 0x51cea0 |
| SlideOff 0x51c0b0 | after the PiP camera's blend, when its vtable slot 7 (+0x1c, Finished) returns true: TrackCam 0x5334b0 (RestTime over), never for Chase, Simple, Fly (0x49b8f0). Needs +0x2c2, a worm active, +0x2c8 = +0x2c9 = 0; clears +0x2c8 / 2c9 / 2ca | disasm | 0x51df69–0x51df7c |
| GoFullScreen 0x51c160 | (1) 0x51e382: at the end of each update, PiP up (+0x2c2), +0x2c8 = 0, +0x2b4 = 0 and **no active worm**: the inset grows when GameLogic.EndTurn deactivates the worm (also from the slide-off, +0x2c8 being cleared by SlideOff). (2) 0x51dbc0: the logical camera is a FlyCam (type 6, SetCamera "FlyCam") and PiPService::IsHidden 0x635960 is false: +0x2b0 = the FlyCam, then GoFullScreen; with the PiP hidden the FlyCam is simply the main view | disasm | 0x51e382–0x51e3a6, 0x51db81–0x51dbc5 |
| End of the promotion | PiP.GoneFullScreen → +0x2c9 = 1 (main view = +0x2b0); the next update clears +0x2c2 / 2c8 / 2c9 and the priority +0x2c4 and sends PiP.Hide | disasm | 0x522892, 0x51dfa8–0x51e02f |
| PiP.GoneOffScreen | +0x2b0 released and 0, +0x2c2 = 0: the event camera (+0x2ac) is no longer drawn until the next serve. A serve during the slide-out (state 3) sets +0x2ac / +0x2b0 but its SlideOn does nothing (+0x2c2 still 1), so that camera is dropped from view too | disasm | 0x5228c5–0x5228de, 0x51c016 |
| Timer.RetreatTimedOut | sets +0x2c0 = 1 only (0x523a96–0x523ac8); the grow comes from the EndTurn that the Lua sends on it | disasm | 0x522710 |
| PiP from the first-person aim | the TrackCam activation 0x5337c0 copies position, look-at and up from the drawn camera (0x4d5210 vfuncs 0x38 / 0x3c / 0x40), which in the HeadCam is the worm's eye (feet + `Worm.EyeLevelOffset`, §11.3a). So a shot fired from the first-person aim serves its TrackCam into the PiP from the shooter's eye: the PiP follows the shot from there, the main view stays the HeadCam. HeadCam leaves only on `Input.FirstPersonReleased` or `GameLogic.EndTurn` (both SetCamera "Default", handler 0x528ca0), not at the shot | disasm | 0x5337c0, 0x528ca0 |
| PiPService states (+0x20) | SlideOn 0x636060 → 1 (to OnScreen position / scale / rotation, ShowTime, lead-in / out); SlideOff 0x6361c0 (not in 0 or 3) → 3 (to OffScreen); GoFullScreen 0x636390 (not in 0) → 4 (to position 0, scale (320, 240) × 0x4d4cc0, rotation 0; FullScreenTime / LeadIn / LeadOut); PiP.Hide 0x636330 → 0. Motion end (0x636699, table 0x636990): 1 → 2, 3 → PiP.GoneOffScreen + Hide, 4 → PiP.GoneFullScreen, 5. IsHidden: states 0 and 3 | disasm | 0x6369b0 |
| PiPService motion 0x636590 | each value = from·(1 − s) + to·s, s the distance of a trapezoid speed profile: v = 2 / (2T − in − out), s = v·t²/(2·in) during the lead-in, v·(t − in/2) between, 1 − v·(T − t)²/(2·out) during the lead-out, then 1 (T total time, t from the message) | disasm | 0x6365bb–0x6367e9 |
| PiPService data | HUDTWK: ShowTime 0.5, ShowLeadIn 0, ShowLeadOut 0.1, FullScreenTime 0.5, FullScreenLeadIn / Out 0.1; OnScreen Position (190, 135), Rotation z 0.1, Scale (120, 90); OffScreen Position (400, 155), Scale 0; HUD.ActWormInfo.PosPiP (253, 300), HUD.HPreview.PositionPiP (252, 0) | data | 0x635d40 |

### 11.7 Super Airstrike (Bovine Blitz) steered flight

| Item | Value / rule | Label | Source |
|---|---|---|---|
| Scene camera | SuperBomberLogicEntity init 0x58a7f0 finds `perspShape` in the bomber mesh (Bomber.Mesh) and sends Camera.FollowSceneCam (0x58ace6) | disasm | — |
| Flight | on Bomber.AnimsComplete, 0x58b4e0 enables input group "Flying", sends EFMV.End, reads SuperBomber.InitialDelay / TotalBombRunTime, plays `OpenDoorsSource` on the graphic entity (0x5893a0) and creates the SuperBomber cursor; the scene camera is still followed | disasm | 0x58b4e0 |
| End | 0x58b050 (from 0x58b410): disables "Flying", sends Camera.StopFollowingSceneCam (0x58b0e2), Parachute.Kill, EFMV.Start, plays `bombrun_end6` | disasm | — |
| persp node in OpenDoorsSource | fixed at (0.029, 0.529, 0.46) m in the bomber, looking straight down (−y): inside the bomb bay, through the opening doors | data (glb keys) | client/assets/models/superbomber.glb |

### 11.8 Other findings of this pass

| Item | Value / rule | Label | Source |
|---|---|---|---|
| OccludingCam activation 0x52da00 (DefaultCam, ChaseCam via 0x5244f0) | Reset (slot 4, 0x52d870, skipped once when +0x34): target update, yaw = target yaw (+0xc0) + StartYaw + π, pitch DefaultHeight, distance DistFromObject, then the yaw search 0x530c00; unless ResetYaw, the yaw is then the heading of the logical view before the last SetCamera (0x91e8e8) + π, searched again; then one update (0x530690). No Cut flag. The second step needs [0x91ed74] = 1 (1 in .data; 0 only inside the EFMV end handler 0x525540, around its SetCamera "Default") and a non-zero xz part of 0x91e8e8; the heading is asin(x) of the normalised xz (π − asin when z < 0) + π (0x52daec–0x52db23, `_CIasin` 0x815214). 0x91e8e8 = look-at − position of the logical camera (+0x28c) that SetCamera 0x51e4e0 deactivates ((0, 0, 1) with none), written at every SetCamera (0x51e8b4) | disasm | 0x52da00, 0x52d870, 0x51e835–0x51e8c0 |
| Turn start SetCamera (0x51ef80) | SetCamera(`Camera.StartOfTurnCamera`) unless it and the current camera are both "Default" and ActiveWormIndex = LastActiveWormIndex: a new worm reactivates DefaultCam, so the new worm's view keeps the last view's heading, searched round land; ShoulderCamera StartYaw 0, ResetYaw 0, MaxHeight 1.0 | disasm, data | 0x51f058–0x51f10b |
| Yaw search state | the try index +0xec is kept between calls (15 at the current pitch, then reset to 0 for the 15 at MaxHeight); a hit writes yaw and pitch and clears it; distance +0xe0 = 0 returns at once | disasm | 0x530c99–0x530e1c |
| Yaw search 0x530c00 (slot 12, 30 tries) | yaw + table 0x91ed78 (0, ±π/4, ±π/2, ±3π/4, ±π, ±5π/4, ±3π/2, ±7π/4) at the current pitch, then the same at MaxHeight (0x530b90): the first camera spot (0x52e2f0) with a clear land segment (0x51abf0) to the head or the tail point is kept | disasm | 0x530c00, 0x530b90 |
| Head / tail points 0x52e4c0 | target (+0xa4) + R(target yaw +0xc0, +0xc4 = 0 for ChaseCam 0x5245e0)·HeadOffset / TailOffset; the forward occlusion rays of 0x52efa0 leave the head point (point 0, then ± OccDestSize) | disasm | 0x52e4c0, 0x52f446 |
| AlienAbductionCamera clip | 0x547490: candidate = worm + 10 + (0, 50, 50) units, clipped by 0x51af90 (land only: 0x466a20, CollisionManagerService / Landscape), kept if > 10 units from the worm; no test against the saucer | disasm | 0x547490, 0x51af90 |


### 11.8b View basis and up (disasm)

| Item | Value / rule | Label | Source |
|---|---|---|---|
| View basis | XCamera (+0x120 position, +0x12c look-at, +0x138 up): f = normalise(look-at − position); up' = up − f(up·f); if \|up'\| < 1e-6, (0, 1, 0) − f·f.y; still < 1e-6, (0, 0, 1) − f·f.z; normalised. So the camera's up is only a hint: no roll while it is (0, 1, 0), and a straight-down view takes world z as its up | disasm | 0x6e1d6c |
| Logical up | (0, 1, 0) from the base constructor; written otherwise only by IsometricCam (R·(0, 1, 0), 0x52a0a0), FlyCam (the payload's up axis, 0x527b00), TrackCam (inherited at 0x5337c0, (0, 1, 0) at a hard cut, eased to (0, 1, 0) at UpSpeed 0x533b25) and the scene cameras | disasm | 0x51b647, 0x533b25 |
| Shake | CMS 0x51c680 → CameraShakeManager 0x523e60 adds the summed shake offset to position, look-at and (×2) up after the blend, so a shake tilts the view | disasm | 0x523fdf–0x52405a |
| Scene camera lens | 0x6e1f46: half extents = Aperture × 25.4 × 0.5 / FocalLength (x and y); every perspShape in Bundl09: FocalLength 25.0217, Aperture (1.26, 0.94488), NearClip 5, FarClip 50000 → 51.2° vertical, 65.2° horizontal | disasm, data | 0x6e1f46 |

### 11.9 Scene camera roll (`persp` node up)

Roll of the `persp` node's up about the view axis, sampled over every clip: 0° everywhere except `bomber/bombrun_end4` (-126° to -9°),
`bomber/bombrun_end3` (up to +19°) and `superbomber/OpenDoorsSource` (±9°, banking) (data, model transforms). Whether W4M's
`FollowSceneCam` applies the node as is: CMS keeps the scene camera (+0x2b4, 0x522b1d), and each update 0x51bce0 → 0x531bc0 gathers the scene (XGatherSceneAction) and hands scene, node and path to the drawn camera's vfunc +0x44, which takes the node's world transform, roll included; there is no up correction in that path [disasm]. `bombrun_end4` is the one place a large roll can come from.

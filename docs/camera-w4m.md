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
- **Each payload picks its camera in WEAPTWK.** `PayloadWeaponPropertiesContainer` has a camera-name string field. The field's descriptor name was not resolved; it sits just before `PayloadGraphicsResourceID`. Values (data):
  - `PayloadTrackCamera`: Bazooka, Grenade, Cluster, Banana, Bananette, Dynamite, Holy Hand Grenade, Gas, Poison Arrow, Landmine-cluster, Factory weapons.
  - `FatkinsTrackCamera`: Fatkins.
  - `DonkeyTrackCamera`: Concrete Donkey. This name does **not** exist in CAMTWK; the Donkey code uses `DonkeyCamera` directly.
  - `OldWomanChaseCamera`, `ScouserChaseCamera`.
  - `SheepChaseCamera`: Sheep and Super Sheep.
  - `HomingMissileFlyCamera`: Homing Missile.
  - Empty: Airstrike, Super Airstrike, Landmine, Fatkins food, Sentry payload.
- **The container class decides the camera type** (`PayloadLogicEntity`, function 0x575320, called while the payload is alive):
  - `TrackCameraContainer`: the payload requests a track camera (0x51bf80, priority 2) if its event point (+0x12c, the predicted first contact, §2) is off screen (0x51b3b0) **or** +0x1a8 is greater than `Camera.Track.MinEventTime` = 1000 ms. +0x1a8 is rewritten by FindFirstEvent (0x5766cb) at launch and at each bounce, so it reads as the flight time since the last event (medium confidence).
  - `ChaseCameraPropertiesContainer`: chase camera starts immediately (0x51d5e0, active priority 6).
  - Fly cameras are started explicitly with `SetCamera("FlyCam")` (0x57e380), by Super Sheep, Starburst and Homing.

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
   - At most 2 candidates per frame; a refused one advances the index (mod count), an accepted one does not.
   - Accepted: if it is the index of the last cut (+0x70, −1 at activation), **no cut**; otherwise a hard cut (position = candidate, look-at = object, up = (0,1,0)), cut timer reset.
5. **Between cuts:**
   - look-at: `lerp(lookAt, object, LookSpeed)` per frame, while the object exists;
   - up: `lerp(up, (0,1,0), UpSpeed)`;
   - back-off (0x533190): only when the object is at rest or moving toward the camera (look direction · velocity < 0), and closer than `MinPreferredDistance`: the camera lerps at `ZoomSpeed` toward object + MinPreferred·(camera − object)/|…|, that target clipped by 0x51b040 (the camera.cpp clip chain 0x51ae40 / 0x51af90 / 0x51ac40);
   - height: y eases to at least water + `MinPosition`;
   - if the camera sphere-tests inside land (radius -30, function 0x466a20), y += 5 per frame.
6. **End.**
   - Payload or crate: when the tracked entity disappears (explosion), the camera **freezes and keeps looking at the last point** for `Camera.Track.RestTime` = 1500 ms (vtable +7, 0x5334b0). The TrackCam is then finished.
   - Worm: also finishes when it is at rest (velocity = 0) + 1500 ms.
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
| StrikeChaseCamera | Chase | Dist 300, CutOnRetreat = 1. The name is in no weapon and no exe string; it is probably unused or built at runtime (assumed) |
| HomingMissileFlyCamera | Fly | LagBehind 60, LookAhead 100, PosSpeed 0.05, LookSpeed 0.1, PauseDuration 1000 ms, FinalDistance 500 ("Hold the FlyCamera for a moment after the explosion") |
| SuperSheepFly / Starburst | Fly | LagBehind 80, LookAhead 100, LookSpeed 0.2, PosRate 1.0 / 0.1, Pause 1000, Final 500 |
| FallCamera (worm falling) | Fly | LookSpeed 0.08, PosSpeed 0.02 |
| AlienAbduction / Donkey / MineFactory / SuperAirstrike / Flood | Simple | (PosUpdateSpeed, LookUpdateSpeed) = (1,1) / (1,0.1) / (1,0.1) / (1,0.1) / (0.01,0.01) |
| Orbit (game over) | OrbitCam | `Camera.Orbit.Height` 450, `AdditionalRadius` 100, `Speed` 0.3. Disabled by `Script.NoOrbitCamera` = 1 (LOCAL default 0) |
| Blimp (free view) | Isometric | HeightAboveLand 6, StickLength 500, DefaultPitch 1.0, Zoom 0.15–2.0, MoveSpeed 250 |
| Jetpack | JetpackCamMkII | StickLength 230, Pitch -70..60, DefaultPitch 0.5, PosUpdateSpeed 0.995. Update 0x52b4c0: no input; behind the worm's yaw, pitch += 0.01 (PitchSpeed) × (0.5 − PitchScale 4 × vy − pitch) each frame, the -70/60 clamp never binds, kept 5 units over Water.Level. Requested on take-off (0x5624b4), `Default` on landing / dry. "Jetpack Ground" (JetpackGroundCam) is created (0x522191) but never requested by name. Ours: controls.cpp `camera()` jetpack branch |
| Shake | — | `Camera.Shake.ExpDurationScale` 800, `ExpMagScale` 1, `ExpRadiusScale` 8, `Max` 0.01; earthquake magnitude 1.0, 7000 ms |
| Worm fade | — | `Camera.WormOpaqueDist` 50, `WormTransparencyDist` 25 (the opaque distance must be greater, per an exe assert) |

Occluding/Chase field units: `DefaultHeight`, `MinHeight` and `MaxHeight` look like pitch in radians (Ninja ±1.57 = ±90°). Assumed.

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
- **Smoothing** (0x530690), with fixed per-frame lerp factors, not scaled by dt:
  - zoom in at `OccZoomInSpeed` 1.0, i.e. an instant snap;
  - zoom out at `OccZoomOutSpeed` 0.02 per frame;
  - position and look-at at 0.1;
  - look-ahead (`LookAheadScale` 500, clamped to `MaxLkAheadDist` 25) only when distance > 75;
  - under 25, the look-at height is blended toward the camera height;
  - camera and look-at heights are kept ≥ a minimum level + 5 (assumed: water).
- **Fields with no reader found** in OccludingCam/DefaultCam: `TimeBeforeZoomOut`, `ZoomOffsetDist`, `UpUpdateSpeed`, `MinLookAt`, `MinPosition`. The **1000 ms "before zoom out" is therefore not used** on PC (disasm, medium).
- **Camera distance toggle** (`Camera.ToggleDistance`): it stores kLongshot/kCloseup on the team, but the desired distance stays `DistFromObject` either way, so it has no effect on PC (disasm, medium).

### Active worm fade (`WXWormGraphicEntity` 0x5a4420)

- Applies to the **active worm only**.
- `alpha = clamp((|cam − worm| − Camera.WormTransparencyDist 25) / (Camera.WormOpaqueDist 50 − 25), 0, 1)`: invisible at 25 or less, opaque at 50 or more.
- It depends on camera distance only, not on occlusion. In practice it hides the worm when an occlusion zoom brings the camera onto it.
- With camera type 1 (assumed: head view), `alpha = max(0, 1 − |v|²/0.016)` instead (low confidence).

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

1. Phase duration is 5000 ms, or 15000 ms when a flag from `0x5a6350` is set (assumed: a mission/challenge or replay case).
2. The engine reads `MostRecentlyActiveWorm`.
   - If none: `Orbit` immediately.
   - If that worm is still alive: a **WormTrackCamera request** on it (0x51cf20).
   - Otherwise: the first live worm out of 16.
3. Fireworks, cheering and `music/victory` play.
4. After 1000 ms, and again at 4000 ms, if `Script.NoOrbitCamera` = 0 (LOCAL default), the camera switches to `Orbit` (0x4ff790).

**OrbitCam** (`.\OrbitCam.cpp`):
- Centre is `Land.Center`, unless `Orbit.OverrideLookAt` is set. Radius is `Land.Radius` (or `Orbit.OverrideLandRadius`) + `Camera.Orbit.AdditionalRadius` 100.
- Height is `Camera.Orbit.Height` 450, presumably relative to `Land.MaxHeight` or water (assumed). Speed is `Camera.Orbit.Speed` 0.3, likely rad/s (assumed).
- The orbit is therefore around the **level**, not a worm. Worm-state strings in the code (WormMoving, Aiming, Roping, Fire…) suggest player input stops it, unless `Camera.Orbit.IgnoreInput` is set (assumed).

### Concrete Donkey (disasm, 0x5538a8–0x553927)

- On release, the donkey logic starts **`DonkeyCamera`** directly. It is a SimpleCam, started through `0x51d760`, with PosUpdateSpeed 1 (locked) and LookUpdateSpeed 0.1.
- Camera position = donkey (x, y − offset, z + **500**): a fixed side view from +z, about 25 m away. The look-at follows the donkey with a 0.1 lerp.
- The `CameraId` `DonkeyTrackCamera` is never looked up, because the donkey does not go through the generic payload camera code. If it were looked up, CMS would only log "Named camera not found" (string at 0x452e07). The fallback is assumed.
- The y offset is the donkey's own value, read next to `Donkey.Gravity`.

### Airstrike and Super Airstrike

- Normal airstrike missiles have an empty `CameraId` (data), so the bombs request no camera. The strike is seen from the current camera.
- Worms that get knocked away trigger WormTrackCamera as usual (deduced).
- `StrikeChaseCamera` (CAMTWK, Chase, distance 300, CutOnRetreat) has no reference in the exe. It is probably unused (assumed).
- Super Airstrike (cows on parachutes, `ParachutePayloadLogicEntity`) starts `SuperAirstrikeCamera`, a SimpleCam (1, 0.1): position locked, look-at smoothed (disasm 0x57a330).

### Fatkins

- `WeaponFactoryLogicEntity` writes `FatkinsTrackCamera` into the payload's camera name at runtime (0x5993e0), so the generic payload rule of section 1 applies.
- TrackCam then uses wide candidates, (±200, 100, ±500) around the event point, at distance 1000 / 500.
- The same factory code picks `HomingMissileChaseCamera` or `HomingMissileFlyCamera` for the factory homing missile (0x598dfa / 0x598e47).

### Alien Abduction (disasm, `AlienAbductionLogicEntity.cpp`)

- The UFO animation carries its own scene camera (`perspShape`, `Camera.FollowSceneCam`).
- `AlienAbductionCamera` is a SimpleCam (1, 1), fully locked to that scene camera, aimed at the abducted worm `m_uCameraWorm`.
- At the end comes `StopFollowingSceneCam` (0x547e1c, 0x5486ca).

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
| Game over | WormTrackCamera on MostRecentlyActiveWorm (else first alive), then Orbit after 4 s unless Script.NoOrbitCamera | Orbit look-at: Land.Center at max((MaxHeight+W+20)/2, W+20); radius Land.Radius+200 (or OverrideLandRadius); height 450, speed 0.3. Orbit lasts 5 s (15 s with an unknown flag). Fireworks: Land.Center ± 0.5 Radius, y = MaxHeight + rand*30, rand%40 per 100 ms | dis 0x4ff8d0, OrbitCam.cpp |
| Concrete Donkey | SimpleCam DonkeyCamera (pos 1, look 0.1) | Fixed at (tx, spawnY-500, tz+500), looks at donkey+(0,100,0); spawnY = ty + max(1500, MaxHeight+500); camera shake on bounces | dis 0x553791, data |
| Airstrike | none (empty WEAPTWK name) | Current camera stays; blasted worms trigger their own Track | data+dis |
| Super Airstrike | SimpleCam SuperAirstrikeCamera after the last bomb | pos = A - 200 d + 50 perp(d), d = dir from the first drop A to the last; looks at the payload | dis 0x58ae50 |
| Fatkins | Track FatkinsTrackCamera | dist 1000, MinPreferred 500, offsets ±200/100/±500, CutWhenStartOffScreen 0 | data |
| Alien Abduction | SimpleCam AlienAbductionCamera (1/1) | pos (UFO.x, Land.MaxHeight, UFO.z+200), looks at worm+(0,10,0); later worm+(0,50,50) if collision-free and >10 away | dis 0x547490 |
| Flood | SimpleCam FloodCamera (0.01/0.01) after 1400 ms | pos (cx, cloudY-200, cz+max(Land.Radius,3000)), cloud at Land.MaxHeight+450, looks at the cloud; FloodDuration 3000, RainDuration 4700 | dis 0x555730 |
| Mine factory | SimpleCam MineFactoryCamera (1/0.1) | factory+(0,50,300) if collision-free and >30 away | dis 0x5cf930 |
| Starburst | generic payload camera, then FlyCam StarburstCamera after 3500 ms | LagBehind 80, LookAhead 100, LookSpeed 0.2, PosSpeed 0.05, UpSpeed 0.09, PosRate 0.1, Pause 1000, FinalDistance 500 | dis 0x5891e0, data |
| Super Sheep | Chase SheepChaseCamera (dist 170), FlyCam SuperSheepFlyCamera on take-off (as Starburst, PosRate 1.0), FallCam when the flight ends (look 0.08, pos 0.02) |
| dis 0x556aa0 |
| Homing missile | FlyCam HomingMissileFlyCamera for the whole flight | LagBehind 60, LookAhead 100, LookSpeed 0.1, PosSpeed 0.05, UpSpeed 0.1, PosRate 0.01, Pause 1000, FinalDistance 500 | data |
| Sheep / Old Woman / Scouser | Chase *ChaseCamera | dist 170, DefaultHeight 0.255 rad; Old Woman Min/Max height 0.25/1.3; OccHeightSpeed 0.4, OccYawSpeed 0.9 | data |
| Ninja rope | Ninja camera on attach | dist 400, height ±1.57 | data, dis 0x57222f |
| Girder | GirderCam | dist 325, DefaultHeight 0.6, MinZoomDist 100 | data |
| Drowning | no dedicated camera | Worm.Drown.HeightOffset 7 keeps the camera floor above the water | dis (role assumed) |
| Crate drop | Track CrateTrackCamera, once per drop after Crate.DelayMillisec | dist 500, MinPreferred 200, ZoomSpeed 0.009, UpSpeed 0.2, CutWhenStartOffScreen 1 | dis 0x5cbd30, data |
| Explosion shake | CameraShakeManager | R = 8 * ImpulseRadius, magnitude = ImpulseMagnitude * (R-d)/R, duration = 800 * ImpulseMagnitude ms, random axis vector fading linearly, total clamped to Camera.Shake.Max 0.01 | dis 0x5241c0 |
| FlyCam end | hold | stays PauseDuration after the explosion, collision-checked | dis 0x528452 |

## 8. Open points

- Whether `PayloadLogic +0x1a8` is an elapsed time or a timestamp, which matters for the 1000 ms flight rule.
- TrackCam lateral axis: up × d = (d.z, 0, −d.x) (0x5326b9). The pairs are symmetric, so the side only changes which of a pair comes first.
- OccludingCam: `TimeBeforeZoomOut`, `ZoomOffsetDist` and `MinPosition` seem unused on PC; check before ignoring them.
- Game over: the condition for 15 s instead of 5 s, the height reference and units of the orbit, and when input stops it.
- Worm fade: what logical camera index 1 is exactly.

## 9. Targeting weapons: Blimp view and reticle

The full W4M analysis is in `docs/w4m-map.md` §10. In short:
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
  - With a targeted weapon in hand, **A / ZR / Space** enter the Blimp. That press is swallowed until it is released, so it does not fire.
  - **Holding L** also shows the Blimp, until L is released. The L+R performance overlay is disabled while in the Blimp, and a locked homing missile keeps L + stick aiming for a single Joy-Con.
  - In the Blimp, **A** fires (homing: locks the target) and **B / Enter** leave without jumping. **E** toggles, as in W4M.
  - The toggle is client state (`Controls::targetView`). It is dropped as soon as no targeted weapon is in hand or the turn ends.
  - Fire never launches outside the Blimp (0x583a10). In the Blimp, a press with no target plays `FeError` in place of W4M's `weapons/Gong` (not imported).
  - Hints, as in `BlimpHelpEntity` / `WXFE.HelpBlimpConsole` (Look, Pan, Zoom in / out): "Fire / Lock target", "Look", "Pan", "Zoom" and "Leave" in the view. Outside it: "Sky view: target" and "Hold: sky view".
- **The CPU** uses the Blimp only while its plan executes a strike, as in W4M, which calls `SetCamera("Blimp")` from `AIActionSetStrikeTarget::ApplyActionInner` (0x4b4c70) and `AIStrike.SeekTarget` (0x4b5a90). That is `Ai::striking()`: mode Act with a targeted plan weapon. It is not shown during instant replays or match playback. The team's reselected weapon alone no longer opens the view: it used to bring up the reticle at random moments. `ai_check` tests this.
- **Controls**, as in HelpBlimpConsole (Movement = Look, Camera = Pan):
  - Left stick: yaw (RotateSpeed 0.55·s) and pitch (PitchSpeed 0.45·s, clamp [0, π/2]), with s = 0.9 + 0.1·zoom. Right stick: pan at 250·zoom u/s. D-pad up / down: zoom 0.15–2 (FOV only, client-side).
  - Desktop: WASD look, arrows pan, X/Z zoom.
- The camera is exactly the sim's: `Game::blimpEye(cursor, cursorYaw, cursorPitch)` looking at `Game::cursor`, so the reticle (`Ui::targetCursor`) is the screen centre.
  - Entry pose (0x52ad20): focus.y = max(worm.y, highest land) + 6 units, moved back so that the centre ray hits the worm.
  - The focus stays within 4500 units (225 m) of Land.Center. Its x and z are the map centre; its y, the water height, is assumed.
- The tint is white, light blue on water, and red with no target. With no target (the ray misses land and water within 200 m, possible at low pitch), the sim refuses Fire.
- Reticle size: HUD units, screen height / 480. HUDTWK places HUD items in a 480-unit-tall space, e.g. `HUD.AngleMeter.ScreenY` and `HUD.Powerbar.Position` y = -165, and the cursor obeys the HUD hide flag. That the cursor uses the same layer is assumed.
- **Input** (net and replays): the 4-byte `Input` is unchanged. While `Input::TARGET` (bit 64) is set:
  - `turn` yaws the Blimp at up to `BLIMP_TURN`, and the worm does not turn.
  - `walk` moves the focus forward at up to `CURSOR_SPEED` 25 m/s.
  - `aim` moves it right at the same speed. With `Input::PITCH` (bit 128) set, `aim` instead tilts the camera at up to `BLIMP_TILT` 0.495 rad/s.
  - When the player moves sideways and tilts at once, the client alternates the two on successive ticks at twice the rate.
  - The worm neither walks nor pitches.
- `target()` is then the land or water hit of the camera ray through the focus. The airstrike runs along `strikeDir()`, the view's right vector; the sense is assumed, as in w4m-map §10.
- **Fatkins** is a Bomber payload, as in W4M (BomberLogicEntity → FatkinsStrikePayload, 0x54ddf0). `Game::fatkinsDrop` releases it 25 m up with the plane's ground speed (`Bomber.GroundSpeed` 7.5 m/s) along the strike direction, early enough to land on the target.
  - The direction is the view's right vector in the Blimp, otherwise the worm's facing.
  - The AI's prediction (ai.cpp) uses the same function.
- The state (`cursorOn`, `cursor`, `cursorYaw`, `cursorPitch`) is reset at each turn. The checksum mixes it only while `cursorOn` is set, so old replays and the CPU, which never sends TARGET, are unchanged; the one exception is Fatkins, whose path changed for every user. The CPU's view centres the Blimp on its own aim point (the W4M AI uses the Blimp too).

## 10. NinjaCamMkIII, FlyCam fields, abduction close shot, homing cursor (disasm, this pass)

- **NinjaCamMkIII** (vtable 0x855a18) overrides OccludingCam slots 9 and 10.
  - Slot 9, 0x52d720, replaces the occlusion test: if there has been no camera input for 60 ms (0x51b540) and the camera spot is in land (0x52d140: land segments of 5 units from it along +z, +x and +y), 0x52d250 tries the yaw offsets of table 0x91eaf0 (±π/8, ±2π/8 … ±7π/8, then zeros), 8 a frame, at the same pitch and `DistFromObject`; the first clear one becomes the yaw and position, and the index (+0xec) resets. It is never reset otherwise.
  - Slot 10, 0x52d3c0: distance = d·(1 − OccZoomOutSpeed) + DistFromObject·OccZoomOutSpeed each frame (0.04), position and look-at heights ≥ water + 5. There is no zoom-in at all (`OccZoomInSpeed` 0).
- **FlyCam fields** (0x527a90 copies the container into the camera): UpSpeed → +0x54 (the up-vector lerp), LookSpeed → +0x4c, PosSpeed → +0x78, PosRate → +0x7c, LagBehind → +0x6c, LookAhead → +0x68, MinPosition → +0x70, MinLookAt → +0x74, FinalDistance → +0x80, PauseDuration → +0x8c, Cut → +0x58; the position lerp +0x50 starts at 0.
  - Update 0x528240: `+0x50 = +0x50·(1 − PosRate) + PosSpeed·PosRate` every frame, so PosRate is how fast the position factor reaches PosSpeed (homing 0.01, Starburst 0.1, Super Sheep 1).
  - 0x527b00: up = the payload's local up axis (its transform's second column), position = payload − LagBehind along its forward axis, look-at = payload + LookAhead; a land hit between look-at and position pulls the position 10 units in front of it. UpSpeed is therefore the rate at which the camera rolls with the payload.
  - On the payload's death the position is set to position + FinalDistance along look-at → position, clipped by land (0x51abf0), held PauseDuration.
- **AlienAbductionCamera** position (0x547490, every frame through SimpleCam 0x531e30): (UFO.x, Land.MaxHeight, UFO.z + 200); while the abduction state (+0x44) is 2, the state set with `Worm.OverridePhysics` and the camera start (0x548578 … 0x5486ca), worm + (0, 50, 50) clipped on the worm → candidate segment (0x51af90), kept if more than 10 units from the worm.
- **Worm-track requests during the death queue**: "Worm Dying" (0x5a7282), "Worm Displaying Damage Taken" (0x5abeec) and "ImpulseWorm going Ballistic" (0x5ad60b) all call 0x51cf20(worm). When served (0x51d3d0), a request whose priority is below the running track's (+0x2c4) is cleared, not kept (0x51d408).
- **Homing cursor** (Bundl09): `Homing.Cursor.Mesh` = node `Inner` with 4 quads (Inner_01 top, 04 bottom: 18 × 54; 02 left, 03 right: 54 × 18; one row each of texture `maya:file7/-1` #3, exported as `fe2/homing_inner`); `Homing.Cursor.SquareMesh` = node `Outer`, locator1-4 at (∓50, ±50) carrying the bitmaps `HUD.Homing.Cursor.TL/TR/BL/BR` (0x560690). Clips: Intro_Inner, Loop_Inner, Intro_Outer, Loop_Outer, Lock_Outer, Error_Outer (keys in docs/camera.md). The LockOn tints its 4 corners each frame before the lock (0x560590 → 0x552340); after `HUD.Target.Selected` (0x560420: Lock_Outer, `weapons/LockOn`) it stays on the stored target point (0x5600e0: on the camera → target ray at 500 units, i.e. screen-constant). The Inner mesh is never tinted (HomingCursorGraphicEntity uses the base per-frame 0x552230). The size of a bitmap attached to a locator is unverified: ours is its 128 px, which makes the corners frame the ticks.


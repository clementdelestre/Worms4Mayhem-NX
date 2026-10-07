# W4M tactical view and targeting cursor

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 10. Tactical view and targeting cursor (Airstrike, Donkey, Homing...)

Confidence: **data** (tweak/text/strings), **disasm**, **assumed**. VAs in `WormsMayhem.exe`, base 0x400000. Units: W4M world units; logic tick = 20 ms.

### Summary

- There is **no dedicated targeting camera** and **no free 2D cursor in world space**. The targeting view is the ordinary **Blimp camera** (`IsometricCam`, logical view 3). The player enters it with the Blimp key. The cursor is a reticle fixed at the **screen centre**. The player moves the camera, not the cursor (disasm + data).
- Each frame, the camera manager casts a ray from the camera position through its look-at point, against the land, the water plane and worms. The hit point is the target. This writes `Airstrike.TargetPoint`, `Airstrike.Direction`, `Airstrike.UpVector`, `Airstrike.HasTarget` and `Airstrike.WaterTarget` (disasm).
- On Fire, the weapon copies `Airstrike.TargetPoint` into `Payload.Target` and broadcasts `HUD.Target.Selected`. The airstrike's direction is the camera's horizontal right vector, so the planes fly across the screen. There is no separate left/right choice (disasm).
- **Homing Missile: the target is set from the Blimp view or from the first-person aim view (observed + disasm).** `0x583a10` (Fire in state 1) only fails with no target or when the current logical camera type (`[CMS+0x2a0][CMS+0x28c]+0x2c`, 0x583a8f..0x583ad8) is 2 (Default), so HeadCam (type 1) and Blimp both lock; `UpdateTargetInfo 0x51c910` takes the ray of the *current* camera (aim ray in Head, cursor in Blimp). The Homing cursor is visible in both (slots 13/14). Ours: the same lock (`Game::locked`, `lockAt`) from either view, a new press charges (docs/sim.md). Switch buttons: hold L = first person, A / ZR outside it = Blimp (that press does not lock), A / ZR in either view locks then charges; keyboard E toggles the Blimp, right mouse = first person (ours, Switch mapping; W4M PC: E / aim key).
- In-game help text (data, English.xom): "Incoming! Define the path using [Blimp] and launch using [Fire]" (Airstrike / Super Airstrike); "Fat men can fly! Define the path using [Blimp]…" (Fatkins); "Find your target using [Blimp], aim the launcher then [Fire] to power up" (Homing); "Target the area using [Blimp] and call in E.T. with [Fire]" (Alien Abduction). `FETXT.Control.Blimp` = "E" for both keyboard and joypad (LOCAL).

### Weapons that use it (data, WEAPTWK)

| weapon | IsTargeting | IsBomber | IsControlledBomber | IsHoming | other | CameraId | LaunchDelay | cursor |
|---|---|---|---|---|---|---|---|---|
| kWeaponAirstrike | 1 | 1 | 0 | 0 | EndTurnImmediate 1 | (none) | 200 | Bomber |
| kWeaponSuperAirstrike (Cow.Payload, NumStrikeBombs 3) | 1 | 1 | 1 | 0 | EndTurnImmediate 1 | (none) | 200 | Bomber, then SuperBomber |
| kWeaponFatkins (NumStrikeBombs 1) | 1 | 1 | 0 | 0 | | FatkinsTrackCamera | 200 | Bomber |
| kWeaponConcreteDonkey | 1 | 0 | 0 | 0 | | DonkeyTrackCamera (not in CAMTWK; code uses `DonkeyCamera`) | 200 | Targeting |
| kWeaponHomingMissile / kWeaponFactoryHoming | 1 | 0 | 0 | 1 | Aimed + Powered, WXAnimTargetSelected `AimLockHomingMissile` | HomingMissileFlyCamera | 0 | Homing + LockOn |
| kUtilityTeleport | BaseWeaponContainer, no targeting flags | | | | | | 0 | (dead code, see below) |

The cursor is chosen in `PayloadWeaponLogicEntity` init, 0x582d70 (from 0x583017). If IsTargetingWeapon (+0x1c9): weapon state `this+0x38` = 1 (targeting), then IsHoming (+0x1cd) gives Homing, else IsBomberWeapon (+0x1cb) gives Bomber, else Targeting (disasm).

### Camera: Blimp (IsometricCam, vtable 0x855440, `.\IsometricCam.cpp`)

**Logical view ids (disasm).** They come from the Camera base ctor `0x51b570(name, type)`; the type is stored at camera +0x2c.

| id | camera |
|---|---|
| 1 | Head |
| 2 | Default (DefaultCam) and RayCam |
| **3** | **Blimp** |
| 4 | Orbit |
| 6 | FlyCam |
| 7 | FallCam |
| 9 | Jetpack |
| 0xe | Path, TimedPath |
| 0x11 | Ninja |
| 0x13 | Spectator |
| 0x15 | Simple (0x531c60, used by Donkey) |

`CMS::GetCurrentView 0x51cea0` returns 0x10 while a scene camera is followed, 0x14 while CMS+0x2b4 is set, 0x13 for spectator, else the current camera's +0x2c.

**Entering and leaving (disasm).**
- Entering: `DefaultCam::PseudoHandleMessage 0x524e00`, on `Input.BlimpViewPressed`, calls `SetCamera("Blimp")` (0x524e82).
- Leaving: Blimp's own handler 0x529c00 (vtable slot 5), on `Input.BlimpViewPressed` again, calls `SetCamera("Default")` (toggle).
- The weapon panel (`WXWeaponPanelLogicEntity` 0x602ea0, at 0x6037bb) injects `Input.BlimpViewPressed` when it opens in Blimp view, which leaves Blimp.
- No code enters Blimp automatically for a human player. The AI calls `SetCamera("Blimp")` itself: 0x4b4c99, 0x4b5b89.
- Scripts can block Blimp with `Camera.Disable "Blimp"`. WormpotService does so at 0x5d6fea.

**Activate (0x529910).**
- `Input.EnableGroup` "Fire" and "CameraSelect".
- `Input.DisableGroup` "WormFirstPersonAiming", "WormAiming", "WormMoving", "WormRoping", "Flying".
- In view 3, it shows a HUD helper (0x61fe90, vtable slot 6); Deactivate (0x529bd0) hides it. The Blimp help lists come from `WXFE.HelpBlimpPC/Console` (data).

**Entry pose (vtable slot 4, 0x52ad20; disasm).**
- Reference point: the tracked entity's position (entity +0x38..0x40), or the previous camera position. Yaw comes from entity +0x90 or from the previous view direction (acos, mirrored to pi minus the angle when z < 0).
- pitch = `Camera.Blimp.DefaultPitch` (1.0 rad).
- focus.y = max(ref.y, `Land.MaxHeight`) + `HeightAboveLand` (6).
- The focus is moved back horizontally by d = (focus.y - ref.y) / tan(pitch): focus.x = ref.x - sin(yaw)·d, focus.z = ref.z - cos(yaw)·d. The ray through the focus therefore hits the reference point at screen centre.
- zoom = 1.

**Camera position (0x52a0a0).** position = focus + R(pitch, yaw, 0)·(0, 0, -`StickLength` 500). The look-at is the focus point. The up vector is R·(global up at 0x91ea50). The assert `m_TargetLookAt != m_TargetPosition` (disasm).

**Per-tick update (0x52af40, then 0x52a5e0; dt = 0.02 s constant at 0x854604, passed by CMS 0x51da9a; disasm).** Input is the CMS struct at +0x210:

| bit / field | from message |
|---|---|
| byte 0: 0x01 | Left |
| byte 0: 0x02 | Right |
| byte 0: 0x04 | Forward |
| byte 0: 0x08 | Back |
| byte 0: 0x10 | RotateLeft |
| byte 0: 0x20 | RotateRight |
| byte 0: 0x40 | RotateUp |
| byte 0: 0x80 | RotateDown |
| byte 1: 0x02 | MouseMiddle held |
| +0x08 / +0x0c | mouse dx / dy accumulators |
| +0x10 | wheel |
| +0x19 | ZoomIn held |
| +0x1a | ZoomOut held |

- Effective rates [disasm]: CMS 0x51da00 passes dt = 0.02 (0x51da9a) to every camera update, and `ZCamUpdateFudgeService` 0x533c90 runs CMS 0x51da00 twice per 20 ms task, so each tweak rate below runs at 2× per real second: full-stick pan 500·zoom u/s (25·zoom m/s), yaw 1.1·s rad/s, pitch 0.9·s rad/s. The move / rotate inputs are on/off bits (no analog share).
- Console help (data, MENUTWKXINGAME `WXFE.HelpBlimpConsoleList`): `Button.Movement` = `FETXT.Look` (yaw + pitch), `Button.Camera` = `FETXT.Pan`.

These are set by CMS HandleMessage 0x522710, from `Camera.*Pressed/Released`, `Camera.MouseMoved`, `Camera.MouseZoom`, `Camera.MouseMiddle*` and `Camera.ZoomIn/Out*` (0x522e77..0x523381).

| quantity | rule | tweak (CAMTWK, data) |
|---|---|---|
| zoom (camera +0x5c) | ZoomIn: z *= ZoomSpeed; ZoomOut: z /= ZoomSpeed; z -= MouseZoomSpeed·wheel; clamp [MinZoom, MaxZoom] | ZoomSpeed 0.99, MouseZoomSpeed 0.08, MinZoom 0.15, MaxZoom 2.0 |
| s (rotation speed scale) | 0.9 + 0.1·zoom | |
| yaw (+0x68) | RotateLeft -= RotateSpeed·dt·s, RotateRight +=; with middle mouse: yaw -= MouseYawSpeed·dx | RotateSpeed 0.55 rad/s, MouseYawSpeed 0.001 |
| pitch (+0x64) | RotateDown += PitchSpeed·dt·s, RotateUp -=; with middle mouse: pitch -= MousePitchSpeed·dy; **clamp [0, pi/2]** | PitchSpeed 0.45, MousePitchSpeed 0.001 |
| focus (+0x78), forward/back | ± f·MoveSpeed·dt·zoom, f = (sin yaw, 0, cos yaw) (Back adds, Forward subtracts) | MoveSpeed 250 u/s |
| focus, strafe | ± right·MoveSpeed·dt·zoom, right ⟂ f in the horizontal plane | |
| focus, mouse pan (middle mouse not held) | focus -= f·MouseYSpeed·dy; focus += right·MouseXSpeed·dx; no dt, no zoom factor | MouseXSpeed 1.5, MouseYSpeed 1.5 |
| bounds | if \|focus - Land.Center\| > 4500, focus = Land.Center + normalize(focus - Land.Center)·4500 (3D sphere; constant 4500 at 0x8556a0) | |
| focus.y | not changed by movement: it keeps the entry height (disasm: f.y is an explicit 0·k; right = f × global up at 0x91ea50, horizontal if that up is (0,1,0), assumed) | |

- Zoom does not change StickLength. The camera's +0x5c factor is the lens zoom: CMS 0x51da00 eases the drawn camera's projection toward the default projection × +0x5c (tan of the half FOV), 0.9 / 0.1 an update, and snaps back to the default when +0x5c returns to 1 (0x51e06d–0x51e2f5; disasm, camera-w4m.md §11.5).
- Unused or unclear: `Camera.Blimp.UpdateSpeed` 0.05 (asserted 0 < x <= 1, not used in this update), `MouseWheelSpeed` 0.1 (+0xac, not used here).
- `Camera.Manual.*` (FastScale/Move/Rotate/Time.Speed = 100) belong to the debug ManualCam, not to targeting: their only reader is the ManualCameraGraphicEntity constructor 0x52c380 (vtable 0x855948, 0x52c400–0x52c442), toggled by `Input.ToggleManualCamera` (disasm).
- `Airstrike.MaxDistance` 1500 (WEAPTWK) is bound to CMS+0x22c but only released, never read (0x520409): unused (disasm).

### Target computation: `CMS::UpdateTargetInfo 0x51c910(bool ninja, bool teleport)` (disasm)

**Triggers.**
- `Airstrike.UpdateInfo` calls it with (0,0) at 0x522990.
- `NinjaRope.UpdateTargettingInfo` calls it with (1,0) at 0x5229c9.
- `Teleport.UpdateTargettingInfo` calls it with (0,1) at 0x522a02.
- Each cursor sends one of these every frame through `0x5523c0`. `PayloadWeaponLogicEntity` sends one again on Fire (0x583a4a).

**Steps.**
1. Take the current logical camera (`CMS+0x2a0[CMS+0x28c]`): position at +0x04, look-at at +0x10, up at +0x1c. dir = normalize(lookAt - position), i.e. through the screen centre.
2. Ray length: ninja = `Ninja.MaxLength` - 10; otherwise `Land.Radius + max(Land.Radius, |pos - Land.Center|)`.
3. `Camera::RayTarget 0x51b150`: a 1000-step sweep against the land (0x466ae0, step = ray·0.001), giving the hit point and normal. If not ninja, it also intersects the plane y = `Water.Level`. If that crossing comes earlier (step index in (0, 1000] and before the land hit), the hit is the water point, normal = (0,1,0) and `Airstrike.WaterTarget` = 1; otherwise WaterTarget = 0.
4. Teleport only: rejected when hit normal.y < 0 (0x51cb0e).
5. Not ninja: a ray against worms (0x519dd0 on the collider at CMS+0x284, 1000 steps). If it hits a worm closer than the land, or the land was missed, and `Airstrike.IsWormValidTarget` != 0 (LOCAL default 0), the target is the worm hit point with normal (0,1,0) (0x51cc55).
6. Rejected when the horizontal (xz) distance camera → target exceeds 8 · `Land.Radius` (8.0 at 0x8544a0).
7. Writes `Airstrike.Direction` = dir (the camera view direction), `Airstrike.TargetPoint` = hit, `Airstrike.UpVector` = camera up, `Airstrike.HasTarget` = valid. HasTarget is forced to 1 on AI turns (`[0x959abc]` 0x4b3340).

The target's y is therefore the land, water or worm ray hit under the screen centre.

**CMS members**, data resources bound in the CMS ctor 0x51f740 (the `lea` comes before each name):

| offset | resource |
|---|---|
| +0x22c | Airstrike.MaxDistance |
| +0x230 | Camera.Shake.Magnitude |
| +0x234 | Land.Radius |
| +0x238 | Land.Center |
| +0x23c | Airstrike.HasTarget |
| +0x240 | Airstrike.Direction |
| +0x244 | Airstrike.TargetPoint |
| +0x248 | Airstrike.IsWormValidTarget |
| +0x24c | Ninja.MaxLength |
| +0x250 | Water.Level |
| +0x254 | Camera.LastTrackCam |
| +0x258 | RetreatTimeRemaining |

The Camera base ctor 0x51b570 binds `Airstrike.WaterTarget` (+0x48) and `Water.Level` (+0x44).

### Cursor (disasm; resource names are data strings)

`Weapon.Create*Cursor` messages are handled by GraphicalSpawningService 0x5009d0. Each class is created from a GUID record whose name sits at +0x10.

| message | class (vtable) | init | mesh / bitmap (file) | anim nodes |
|---|---|---|---|---|
| Weapon.CreateTargetingCursor (Donkey) | TargetingCursorGraphicEntity (0x8675cc) | 0x5f9a60 | `Targeting.Cursor.Mesh` (Target.xom), `Targeting.Cursor.Bitmap` (Target.tga), shadow mesh `Targeting.Cursor.Shadow` attached at node `Target_Shadow`, tint 0x645e2b02 | root `Target`; `Target_Intro`, `Target_Loop`, `Target_Error` |
| Weapon.CreateNinjaCursor | same class, +0x78 = 1 (0x500bb8) | | | |
| Weapon.CreateTeleportCursor | same class, +0x79 = 1 (0x500c04) | | | |
| Weapon.CreateBomberCursor (Airstrike, Super Airstrike, Fatkins) | BomberCursorGraphicEntity (0x857f0c) | 0x54c1f0 | `Airstrike.Cursor.Mesh` (AirstrikeCursor.xom), `Airstrike.Cursor.Bitmap` (Airstrike_Outer.tga), 5 × `Airstrike.Cursor.Dot` at `Dot_Null_01..05` (table 0x91f34c) | root `Aimer_Null`; `Airstrike_Intro`, `Cursor_Loop`, `Cursor_Error`; `Dots_Loop` starts at the end of the intro (0x54c380) |
| Weapon.CreateHomingCursor | HomingCursorGraphicEntity (0x859c84) + HomingLockOnGraphicEntity (0x859d3c) | | `Homing.Cursor.Mesh` (HomingAimerAim.xom), `Homing.Cursor.SquareMesh` (HomingAimerLock.xom); HUD corners `HUD.Homing.Cursor.TL/TR/BL/BR` (Homing TL/TR/BL/BR.tga) | on select: `Lock_Outer` + sound `weapons/LockOn` (0x560420) |
| Weapon.CreateSuperBomberCursor (from SuperBomberLogicEntity 0x58b4e0, during the flight) | SuperBomberCursorGraphicEntity (0x8674f4) | | `SuperBomber.Cursor.Mesh` (SASCursor.xom), `SuperBomber.Cursor.Bitmap`, `SuperBomber.Cursor.Shadow` | |
| Weapon.CreateSniperCursor (GunWeaponLogicEntity 0x55d2da, Sniper flag +0x10e; else the gun asks Weapon.CreateAimingCursor) | SniperCursorGraphicEntity (0x867304) | 0x5f8de0 | `Sniper.Cursor.Mesh` (Sniper.xom), scene 34 HUDSniper | `In_Sniper` |

**Teleport.** `Weapon.CreateTeleportCursor` (handle 0x95e160, declared in the NinjaRope unit) has no sender anywhere in the exe, and Teleport is missing from the weapon enum at 0x90c920. Teleport targeting is therefore dead code in W4M PC (disasm).

**Base `CursorGraphicEntity` (vtable 0x858aac) members:**

| offset | meaning |
|---|---|
| +0x20 | mesh node, placed at the origin (0x96e878) at init |
| +0x24 | bitmap node, attached under the mesh at the anim-root node |
| +0x48 | Airstrike.HasTarget |
| +0x54 / +0x58 / +0x5c / +0x60 | anim names: root, intro, loop, error |
| +0x64 | intro length (0x5525c0) |
| +0x68 | visible |
| +0x70 | Airstrike.WaterTarget |

It subscribes to `Weapon.NotClearToFire`, `Camera.LogicalModeChanged`, `Weapon.DeleteCursor`, `HUD.Target.Selected` and `Binocular.*` (0x551e20).

**Per frame (Targeting cursor slot 6 0x5f9c90, Bomber 0x54c4a0, LockOn 0x560590):**
1. hasTarget = `0x5523c0(ninja, teleport)`.
2. Tint from 0x552340:

   | state | value | RGB |
   |---|---|---|
   | no target | 0xff0020d7 | (215,32,0) red |
   | water target | 0xffffa862 | (98,168,255) light blue |
   | valid | 0xffffffff | white |

   The RGB triples assume memory bytes R,G,B,A; the alpha byte at +3 is confirmed by 0x69f660. The Bomber cursor tints its 5 dots the same way.
3. Visible only when the view is 1 (Head) or 3 (Blimp) (slot 13 0x551fa0 / slot 14 0x552da0), and when the HUD flag `[0x96d030]+0x3c` / 0x7063f5 does not hide it.

**Other slots.**
- Show (slot 11, 0x5524a0): plays the intro from t=0, then queues the loop at the intro length. Hide: slot 12, 0x5525a0.
- `Camera.LogicalModeChanged` with param 1 in a cursor view re-runs Hide + Show (0x552a0e). The Targeting override 0x5f9d10 does this only for the ninja variant.
- `Weapon.NotClearToFire` (slot 20, 0x552630): plays sound `weapons/Gong`, then the error anim, then the loop again.

**Cursor position.** The cursor code never moves the mesh after init. The reticle is therefore screen-centre. Its layer is still not traced: the base create 0x552bb0 instances the mesh through XGraphicalResourceManager vfunc 0x8c (0x6f3d95, layer argument 0xff, as `HUD.PiP` 0x635de9), places it at the origin (0x552cfc) and hangs the bitmap under it (vfunc 0x60, 0x552cd2); what 0xff selects lies in the XOM scene-graph runtime (assumed: HUD). Not the same thing: `HUD.TargetingCursor` + `TargetingCursor.Scale` 16 (LOCAL) belong to WeaponCursorGraphicEntity 0x5fb9c0, the aiming reticle (not checked).

### Confirm and hand-off (disasm)

**`PayloadWeaponLogicEntity::HandleMessage 0x586040`, on `Input.FirePressed`, in state 1:**
1. `0x583a10`: sends `Airstrike.UpdateInfo` and reads `Airstrike.HasTarget`. It fails if there is no target, **or if the current view is 2 (Default)**: the player must be in Blimp or Head (first-person aim); Homing is observed to use the aim (Head) view.
2. On success: `Payload.Target` := `Airstrike.TargetPoint`, then broadcast `HUD.Target.Selected`. HUD.Target.Selected has 12 handle copies; cursors, worm and HUD listen to it.
3. On failure: `Weapon.NotClearToFire`.
4. After success, state = 0.
   - Not homing: `Weapon.DeleteCursor`.
   - Aimed (homing): `Weapon.CreateAimingCursor`, then normal aiming and powering.
   - Not aimed: state = 2, then launch via 0x583160.
   - Powered: creates the class at 0x85c9e8 and sets state 2.

**Data keys consumed later:**
- `BomberLogicEntity` init 0x54d720 reads `Airstrike.Direction`, `Airstrike.TargetPoint`, `Airstrike.UpVector`, `Bomber.GroundSpeed` 0.15, `Bomber.BlitzDuration` 2000, `Bomber.NumBombs` 6, and writes `Bomber.Direction`.
- `DonkeyLogicEntity` init 0x553700 reads `Airstrike.TargetPoint`, `Donkey.ExtraHeight` 500, `Donkey.MinHeight` 1500, `Land.MaxHeight`.
- `SuperBomberLogicEntity` 0x58a7f0 reads `Airstrike.Direction`, `TargetPoint`, `UpVector`.
- `HomingPayloadLogicEntity::Initialize` 0x560bf0 reads `Payload.Target`.

**Airstrike direction.**
- dir = cross(`Airstrike.Direction`, `Airstrike.UpVector`) with y forced to 0, then normalised (0x54d931..0x54d97f). Operands: A = `Airstrike.Direction` (stack +0x44, read at 0x54d7e4), B = `Airstrike.UpVector` (+0x50, 0x54d812); x = A.y·B.z − A.z·B.y, so dir = forward × up, the camera's right vector in a right-handed frame (the renderer is OpenGL: XOglTextureMap). The bombers cross the screen from left to right, as the Bomber cursor's `Dots_Loop` arrows move toward +x (disasm + data).
- There is no left/right key. The player picks the direction by yawing the Blimp camera.
- Start = target - dir · (BlitzDuration · GroundSpeed · 0.5) = 150 units before the target, only when NumBombs > 1 (0x54d9a8 `cmp NumBombs, 1; jbe`), then 0x54d460's fall lead; GroundSpeed is the float at entity +0x3c (0x54d81f), BlitzDuration the u32 read by `fild` (0x54d9ba) [disasm]. `Bomber.DropBomb` i is posted at T0 + i · (BlitzDuration / NumBombs) integer ms, T0 = now + the Begin clip length (0x54db75..0x54dbff) [disasm], so the bombs fall at start + 0..N-1 gaps.
- Super Airstrike: the same flat right vector cross(Direction, UpVector), turned by pi/2 about (0, 1, 0) (axis-angle matrix 0x69cc4d: [[c,0,-s],[0,1,0],[s,0,c]], applied as a row vector at 0x58aa99), i.e. (r.z, 0, -r.x) = the view's forward; then steered with `Fly.Yaw.Left/Right`. Fire drops the cows. EFMV.Start/End are used [disasm].

**AI path.** IsometricCam 0x529eb0, while `AIStrike.SeekTarget`: copies `AIStrike.TargetPoint` / `AIStrike.Direction` into `Airstrike.TargetPoint`, `Payload.Target` and `Airstrike.Direction`; sets HasTarget = 1, WaterTarget = 0, SeekTarget = 0; then posts a message (the AI equivalent of Fire).

### After firing: camera (disasm)

| weapon | camera |
|---|---|
| Airstrike / Fatkins | `BomberLogicEntity` takes the scene camera `perspShape` from the bomber mesh and sends `Camera.FollowSceneCam`. CMS first calls `SetCamera("Default")` (0x522ad7), which leaves Blimp, then follows the scene camera (view 0x14 / 0x10). On `Bomber.AnimsComplete` it sends `Camera.StopFollowingSceneCam`, and CMS goes back to the current logical camera, Default (0x522b60). |
| Super Airstrike | same scene-cam follow (0x58ace6); `SuperAirstrikeCamera` (Simple, PosUpdateSpeed 1, LookUpdateSpeed 0.1, data) is set at 0x57a330. |
| Concrete Donkey | `0x51d760("DonkeyCamera", donkeyTaskId)` creates a SimpleCam (view 0x15) that tracks the donkey (DonkeyCamera: PosUpdateSpeed 1, LookUpdateSpeed 0.1, data). The camera point is stored at +0x188 (z + 500). |
| Homing | target taken in the aim view (observed), then normal aiming in the player's current view, then `HomingMissileFlyCamera` on launch. |

Next turn: `GameLogic.Turn.Started` → 0x51ef80 → `Camera.StartOfTurnCamera` ("Default", LOCAL / scripts).

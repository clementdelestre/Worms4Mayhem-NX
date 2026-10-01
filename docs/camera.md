# Camera (client/src/controls.cpp)

Display only: nothing here reaches the sim or the checksum. W4M values come from `docs/camera-w4m.md` (CAMTWK/WEAPTWK decode + exe);
W4M units are 20 per metre. "ours" marks a rule with no W4M source.

| Trigger | Shot | Distance / height / angle | Duration | Source |
|---|---|---|---|---|
| Active worm, walking/aiming (third person) | ShoulderCamera behind the worm | 9.85 m (W4M 8.5), camEl | while it lasts | camera-w4m §3, §5 |
| Hill between the worm and the camera | zoom in at once to 90 % of the first hit on the worm→camera ray (minus 0.3 m) | never turns or climbs | eases back out at 0.02/frame (1.2/s) | §5 OccludingCam (OccZoomInSpeed 1, OccZoomOutSpeed 0.02) |
| Camera ≤ 2.5 m from the active worm, or terrain hides it | `Controls::occluded()` 0..1, read by the worm renderer (fade / grey silhouette) | 0 at 2.5 m, 1 at 1.25 m | smoothed at 12/s | §5 WormOpaqueDist 50 / WormTransparencyDist 25 |
| Shell, grenade, cluster, banana, holy, gas, poison arrow in flight | worm camera for the first second while the shot is on screen, then TrackCam: hard cut to the first clear ViewPoint around the launch point (m: (∓5,10,15), (±1.25,2.5,2.5), (±3.75,5,3.75), (±2.5,3.75,-2.5), (±2.5,-2.5,3.75), (0,1.5,0)), 2 candidates a frame | backs out to ≥ 30 m at 1.15/s, look-at 6.3/s, ≥ water + 1 m, no line crossing | re-cut when lost / off screen / ≥ 65 m, ≥ 1 s apart; frozen 1.5 s on the last point after the blast | §2 PayloadTrackCamera, RestTime 1500 |
| Holy Hand Grenade far and high | same rule: it rests ~2 s before blowing, so the 30 m back-off plays out from the high ViewPoint 0/1 | ~32° down | — | §2 "Why the Holy Hand Grenade…" |
| Fatkins Strike | TrackCam, ViewPoints (±10,5,±25) m | ≥ 25 m, re-cut at 50 m | as above | §6 Fatkins |
| Concrete Donkey | fixed side camera at spawn + 25 m in +z (raised until it sees the donkey), look-at donkey + 5 m at 0.1/frame | — | until it is gone, then 1.5 s frozen (ours) | §6 / §7b DonkeyCamera |
| Airstrike (missiles) | none: the current camera stays on the worm, no flight to each blast | — | — | §6 Airstrike |
| Super Airstrike, sheep, old woman, scouser, homing | ours: chase behind the shot, 17.5 m (pets locked behind) | camEl | while it flies | not aligned yet (§7b SimpleCam / FlyCam) |
| HP count of a group | from above the group, yaw searched around the scenery | ≥ 7.5 m, el ≥ 0.75 | per group | ours (W4M WormTrackCamera per worm) |
| Death queue | the camera closes on each dying worm in turn | — | `Game::dying()` | docs/death-sequence.md |
| Game over | the winner (WormTrackCamera stand-in: framed from above, around the scenery) | as a group | 4 s | §6 Game over |
| then | OrbitCam around the level centre | radius land half-width + 10 m, look-at max((top + water + 1)/2, water + 1), camera 22.5 m above it, 0.3 rad/s | until the menu | §7b Game over (Orbit) |
| Scenery on the way back (any travel) | the camera rises over it (lift ≤ 30 m at 20 m/s), stops 2 m short horizontally until clear | — | lift decays at 10 m/s | ours |
| A focus dropped for a frame (between two counts, turn start) | kept 0.2 s | — | — | ours (W4M: 200 ms minimum between track requests, §1) |

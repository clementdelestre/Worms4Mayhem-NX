# W4M map: where things live in the original game

This is a general map of Worms 4 Mayhem (PC, Steam *WormsXHD*) for future agents, read from the user's own install. It holds facts only: addresses, names, structures and values. It contains no game asset and no disassembly.

- Addresses are virtual addresses in `WormsMayhem.exe`, PE32, image base 0x400000.
- Distances are in W4M world units, 20 per metre.
- Topic docs go deeper:
  - `docs/w4m-formats.md`: file formats.
  - `docs/camera-w4m.md`: cameras.
  - `docs/weapons-audit.md`, `docs/worm-reactions.md`, `docs/death-sequence.md`.

Confidence tags:

- **data**: read in a game file (exe strings, tweaks, scripts, banks).
- **disasm**: deduced from the code.
- **assumed**: inferred, not verified.
- **observed**: seen by the user in the real game; outranks an unconfirmed disasm reading.
- **user-requested** (our docs): behaviour the user asked for explicitly, kept even where W4M may differ; retested on request, never "fixed".

User-requested behaviours (kept on purpose; retest only when the user asks):
- Switch pad layout (2026-10-04): Y toggles the Blimp / sky view (A / ZR in it fire or lock; B leaves it, no jump), ZL held = first-person
  aim (+ fine aim), L / R = zoom out / in in every view, D-pad left / right = previous / next weapon, D-pad up / down = fuse time and
  jetpack forward, B drops the jetpack secondary, hold - long = perf overlay; Blimp sticks swapped vs W4M HelpBlimpConsole
  (README controls table, docs/camera-w4m.md, Controls).
- Hot seat: the button press that cancels it is consumed, never a shot, jump or view change (docs/sim.md, 2026-10-03).
- Steered shots (Super Sheep, Bovine Blitz, Old Woman, Scouser): the left stick does it all, yaw and pitch; the right stick no longer pitches (2026-10-03).
- Rule 128 "No delays": test rule ignoring the preset's weapon delays (PROTOCOL.md, Start rules).
- 3D texts (worm names and HP, fuse countdown, Crate Spy, jetpack fuel): no "Name Backing" frame as W4M, a drop shadow instead (2026-10-03).
- A local key press skips a CPU's hot seat ("Ready") in offline games (main.cpp, 2026-10-03).
- Worms 3D maps (`*-w3d`) found in the W4M install stay selectable although W4M never offers them (2026-10-05).
- Title <-> main menu (2026-10-06): our big title logo glides to its menu spot as the rows fade in (0.6 s, FeBounce); B plays it backwards
  with W4M's FE screen-out sound (Out_Prev) before the title shows, where W4M scales the list in (In_ScaleY) and drops it at once (Out None) (frontend.md §Title).
- Story / Challenges screens (2026-10-06): every W4M item whose Anim_Outgoing is None
  slide in / fly out like our menu rows (`rowAppear`) instead of appearing / vanishing at once (frontend.md §Story and Challenges).
- Network protocol stays version 1 until a server is deployed (PROTOCOL.md).
- **FE 16:9 spread** (2026-10-13, user-requested): FE x positions are multiplied by (16/9)/(4/3) = 1.333 about the screen centre (`fePos`, `FE_SPREAD` in ui.cpp; the main-menu list likewise), sizes unchanged. Spread: logo, main menu, Ver#, NAV buttons, Challenges page, ticker window (the gap between the two NAV buttons less 10 FE each side = W4M's 406 FE at 1.0, 576 FE now). Kept at W4M's x (`FeFlat`): divide, watermark columns and Title Control (pinned to the left panel), the Story book and every item on it, the movie frames; centred screens (title, loading, setup, options) are unchanged [ours]. See frontend.md "16:9 spread".
- **Main menu size** (2026-10-13, user-requested): the main menu list and the logo are 1.25 x W4M's size (`MAIN_BIG` in ui.cpp); see frontend.md 'Main menu size'.
- Replays off like W4M (2026-10-12, user-requested): `REPLAYS = false` in ui.h removes the instant replay, `.w4r` recording and the Replays menu entry; the undo log (kept for replay_check) also restores the exact-land lists (`SharpLand::undo`, docs/tests.md §replay_check.cpp). Land.ClearVoxel (0x4733b0, the gun hit; Firepunch 0x473470) clears the one cell the land ray's solid sample is in (`Terrain::clearCell`, `.wcells`): formats.md "Land.ClearVoxel" [disasm]. Firepunch's per-step Land.ClearVoxel-like clear (0x473470 from 0x5693e0, cell from the worm-hull probe 0x59edf0 > 0x466ae0) is ported as the Firepunch rise dig (formats.md §22) [ours]. Heightmap land is destructible by blasts (0x463c90 lowers its vertices, formats.md §25); only Land.ClearVoxel refuses it (frame 0xffff), so Firepunch ends its leap on a heightmap hit (`Terrain::heightmap`) [disasm / ours]. Blasts on the heightmap (2026-10-12) [ours]: `Terrain::lowerHeightmap` / `SharpLand::lower` apply the 0x463c90 formula to our 0.25 m `top` columns and relist the cells; the carve sphere no longer cuts the heightmap. Land debris (LandBlobBlastEntity 0x465ea0 / 0x464fc0, formats.md "Land debris"): decoded and drawn [disasm / ours]. Open: the corner cache 0x467210 and PERM cells under a blast, W4M's 1.515 m vertex grid itself (ours is its resample, so the bowl is the formula on a finer grid), untested in game.
- Instant replay: W4M has none, so ours is off (`REPLAYS`): the exe has no instant-replay code or tweak. The only names are `Replay.Request` (0x830e38, a name-table entry at 0x90f898 with no handle object and no code reference) and `App.ReplayPlaying` / `App.ReplayRecording` (LOCAL.XOM, = 0, string absent from the exe) [disasm / data]. Ours starts only once the retreat is over (phase Settle or GameOver, `main.cpp` `over`), then after the shot has settled (>= 1.5 s with nothing moving, or 2.5 s), when the turn did >= 30 damage or a kill [ours; user-requested trigger].
- Match start / restart / return to the menu use the iris (docs/w4m/frontend.md 'Ours, transitions', 2026-10-12).
- Boot shows only the spinning icon on black, then the title opens on the iris (2026-10-07): W4M's Generic loading screen at boot (state 7, 1.8 s minimum) is skipped (`Loading::boot`).

## Open divergences (ours, not W4M, to fix)

Known gaps found 2026-10-08, left for later sessions. Re-check each against the code before acting.
- **Weapon Factory editor.** Resolved: launch types Launched (+ Homing) / Thrown / Air Strike (Dropped exists in the exe but no menu offers it), rows, steps, pages, gun parts, Payload popup, cost gauge, Accept / name flow, SuperScale S in the payload (weapons.md §"Weapon Factory payload", "Editor list", "Editor flow", "Editor data and steps"). The preview is WX.Mesh.WFactory with its clips and pressure gauge, WXP_FE_GuageSteam and (Ray) WXP_PlasmaBomb; Delete asks WXFEP.ConfirmDeleteWeapon; `WXFE.WFWorm` is referenced by no menu and never shown; `LOC^` decoded (0x581890); the ChatterBomb call of 0x740d81 sits in the unreachable Dropped branch (weapons.md "Preview and gauge", "WXFE.WFWorm", "ArielFx `LOC^`", "0x740d81"). The stock weapons (LOCAL DATA.LockedWeapons) are built from their containers as W4M builds them (docs/weapons-audit.md "Weapon Factory weapons"), held as W4M holds them (weapons.md "Held weapon assembly").
- **Frame bin order.** W4M's bins and sorts are decoded (render.md §1 "Render bins": per scene its opaque bin, texture-sorted, then its blended bin, sorted far first by view z). Ours follows them for particles (render.md §3 "Order inside a particle scene"), details and the worm silhouette / outline content. Scene 8's blended shapes (models' keyed alpha, additive 6 / 1, glow, and the water plane) are sorted together far first (render.md §1, `Models::binDraw`); open [ours]: the opaque bins sort by image creation serial (XImage +0x38, 0x6e0998), ours by our texture load order (no visible effect under the depth test).
- **Particle draw order.** Decoded and ported (render.md §3 "Order inside a particle scene"). Both bin sorts run W4M's MSVC 8 introsort (`MsvcSort`, render.md §1), so equal keys past 32 records come out in its partition order. Open: hand-written spawns (`Fx::add`) are not on the slot model.
- **Worm grey band = W4M silhouette** (`Xray::hidden`, 0.25 grey at 50 % where land hides the active worm) and the pale mouth = "Happy" teeth under ambient (0.5, 0.5, 0.6): both match W4M on desktop. Ours: the silhouette pass (`Xray::hidden`) is drawn before the worm's own draw, depth func GREATER against land only (the worm's depth is not yet in the buffer), no polygon offset; the mask (`Xray::mask`) holds the nearest worm layer's `gl_FragCoord.z` (16-bit RGBA packing, tolerance 2e-5) so the two passes agree whatever the depth-buffer precision [ours; fixes grey on unoccluded worms on Switch, where the GLES render-target depth is 16-bit and the polygon-offset units differed between the mask target and the main buffer, assumed cause, not reproduced on device]. Models::draw puts the depth mask back to `Models::zwrite`'s state (Xray::hidden: off) after its blended shapes, so attachments (hat, glasses) drawn after the body no longer write z in the silhouette pass and stay hidden by land in the normal pass [ours].
- **Main menu text baseline** (frontend.md "Main menu layout", "FE.Font glyph quads"): glyph sizes match the FE.Font builder (0x6a7f00); the exporter's baseline sat 0.06 em above W4M's (0.28 vs 0.34 em under the line centre), fixed in `tools/w4m-ui` (base 45): the assets need a `w4m-ui` re-run to show it. Whether the real menu's island is as dark as the unlit tex x vertex colour (30, 84-90, 55) is not re-measured on a screenshot of ours after the fix.
- **Explosion frame rate.** A dynamite blast dropped the Switch to 49 FPS (2026-10-08 screenshot). Desktop bench (`W4NX_BENCH=700 ./worms4nx --shot 7`, 2026-10-25): fx 0.58 ms synced, 215 draw calls, Switch estimate 69 fps; the device figure needs a Switch run (no deploy allowed at the time).

Attributing a function to a class is reliable when the function comes from a vtable. When it was inferred from the nearest `.cpp` assert string, it can be wrong near file boundaries.

## Files

| § | file |
|---|---|
| 0. Tools (`tools/w4m-re/`, see its README) | [README.md](README.md) |
| 1. XOM classes and the serialisation schema (disasm, verified on data) | [formats.md](formats.md) |
| 2. Services, logic entities and messages | [engine.md](engine.md) |
| 3. Data keys and the message system | [engine.md](engine.md) |
| 4. Weapon table | [weapons.md](weapons.md) |
| 5. Worms: physics states and ground collision | [physics.md](physics.md) |
| 6. Turn and phases | [turn.md](turn.md) |
| 7. Audio | [audio.md](audio.md) |
| 8. Rendering | [render.md](render.md) |
| 9. Data/ index | [formats.md](formats.md) |
| 10. Tactical view and targeting cursor (Airstrike, Donkey, Homing...) | [targeting.md](targeting.md) |
| 11. Worms: physics state handlers and animation (extends §5) | [physics.md](physics.md) |
| 12. Audio: WormsX.fev per-event data (extends §7) | [audio.md](audio.md) |
| 13. Weapons: logic class dispatch and enum fields (extends §4) | [weapons.md](weapons.md) |
| 14. Turn timing: units, timers, death pacing (extends §6) | [turn.md](turn.md) |
| 15. Bundles (`Bundl*.xom`) | [formats.md](formats.md) |
| 16. Network (online multiplayer) | [network.md](network.md) |
| 17. Frontend (`WXFE_*`): menus, screen state machine | [frontend.md](frontend.md) |
| 18. AI (CPU worms) | [ai.md](ai.md) |
| 19. EFMV cutscenes, acting scenes, FMV | [acting.md](acting.md) |
| 20. How to search (Comment chercher) | [README.md](README.md) |
| 21. Not covered | [README.md](README.md) |
| 22. Exact land export (`<map>.cells`) | [formats.md](formats.md) |
| 23. Mission scripts: Lua runtime, API use, per-level end conditions | [missions.md](missions.md) |
| 24. Simulation clock and timestep (main loop, TaskManager, 20 ms step) | [physics.md](physics.md) |
| 25. Heightmap land (`.hmp` box, vertex spacing, sampler) | [formats.md](formats.md) |
| 26. Land texture mapping (per-frame uv rows, floor / wall / roof faces) and the DestructAndServe rope cells | [formats.md](formats.md) |
| 27. Match statistics (StatsCollectorService, stat sources and mappers, round / match stores) | [stats.md](stats.md) |

Section numbers are global: "§11" is section 11, in the file listed above.

## 0. Tools (`tools/w4m-re/`, see its README)

| Need | Command |
|---|---|
| strings with VA | `pe.py str REGEX` |
| classes, bases, vtables | `pe.py rtti REGEX` |
| serialised fields of a class | `pe.py schema CLASS_RE` |
| annotated dwords (tables, records) | `pe.py words VA N` |
| who references a string / VA | `xref.py [--callers] 'Land.Center'` or `xref.py 0x5aa7f0` |
| who uses struct field +OFF | `xref.py --field 0x210 [--in FUNC_VA...]` |
| message handle <-> name | `pe.py msg 0x97a7c0`, `pe.py msg '^FE\.ChangeMenu$'` (one handle per source unit) |
| FMOD event parameters (loop, volume, 3D) | `fev.py -g '^weapons/Fuse'`, `fev.py --json` |
| disassemble a function | `disasm.py VA` (`--before N --after N` for a window) |
| XOM containers | `xom.py list FILE [TYPE_RE]`, `xom.py dump FILE NAME\|#IDX` |
| all tweaks as JSON | `tweak.py` (to `~/.cache/w4m-re/tweaks/`), `tweak.py -g REGEX` |
| Lua scripts | `lua.py stdlib.lub --code`, `lua.py --all 'SetData\("Camera'` |

Python access: `import scan; d = scan.index()` gives `d['calls'][target]`, `d['refs'][va]` and `scan.func_start(va)`.

## 20. How to search (Comment chercher)

| Question | Recipe |
|---|---|
| Who reads data key X? | `xref.py --callers 'Worm.Drown.HeightOffset'`. The first wrapper called after the site (`0x50b7f0` float by value, `0x50bac0` float cached handle…, §3) gives the type. For a cached handle, follow the `[this+off]` member. The default value: `tweak.py -g 'Drown'`. Scripts: `lua.py --all 'Drown'`. |
| Who sends or handles message M? | `pe.py str '^GameLogic\.Turn\.Ended$'` gives the string. `xref.py` on it finds the static handle(s), built by `push name; mov ecx, G; call 0x68bb9f`. `xref.py G` then shows the users: `0x690e3e` subscribes, `0x68bcce` tests inside a HandleMessage, `0x6910e4` sends. |
| Disassemble the handler of message M | `pe.py rtti '^ClassName$'` gives the vtable. Slot 7 (`pe.py words VTABLE+0x1c 1`) is HandleMessage. `disasm.py` it and look for the `0x68bcce` test against M's handle. |
| Dump container Y | `xom.py list Tweak/CAMTWK.XOM Track`, then `xom.py dump Tweak/CAMTWK.XOM PayloadTrackCamera`. For WEAPTWK: `xom.py dump Tweak/WEAPTWK.XOM kWeaponBazooka`. |
| Field names and offsets of a struct | `pe.py schema '^PayloadWeaponPropertiesContainer$'`. The struct offset is where the code reads it (`[reg+0xd0]` = CameraId). |
| Who reads a struct field? | Grep the disassembly of the class's functions for `+0xOFF`. A quick start: `disasm.py` on its vtable slots. |
| Find a class's code | `pe.py rtti NAME` (vtable), `pe.py str 'Name\.cpp$'` (assert string), then `xref.py` on that string's VA. |
| What does a .lub do? | `lua.py FILE.lub` for the functions and constants, `--code` for the pseudo-code. |
| Value of tweak T | `tweak.py -g '^Camera\.Orbit'`. Full JSON in `~/.cache/w4m-re/tweaks/`. |
| An FMOD event | Event path strings `group/event` in the exe (`pe.py str '^weapons/'`). `PlaySound` is 0x604a20 (§7). |
| Does an event loop, at what volume? | `fev.py -g '^weapons/FuseLoop$'`: column `loop` (loop / oneshot / loop_to_end / silent). Gain = 10^((vol_dB + sd_vol_dB + cat_dB)/20). FSB sample: `fev.py --json`, `sounddefs[i].waves[j]` = {bank, index, ms} (§12). |
| Name of a message handle in disasm | `disasm.py` prints `msg Name` on handle operands; otherwise `pe.py msg 0xVA`. All handles of one name: `pe.py msg '^Name$'`, then `xref.py` on each. |
| Who reads or writes struct field +OFF | `xref.py --field 0xOFF --in FUNC...`. For a class, list its functions from `pe.py rtti` vtables. To name the field, match offsets with `pe.py schema` (pData of a worm = `WormDataContainer`, §11). For members bound to data keys, read `lea reg,[this+OFF]` followed by the key-name push in the ctor (CMS 0x51f740, GLS 0x4f7d80). |
| Enum field values | `pe.py schema CLASS` prints the value names under enum fields (record +0xc -> descriptor -> name list). Example: WeaponType 4 = kThrown. |
| Which class a factory creates | `disasm.py` prints `class Name` on the descriptor pushed before `call 0x639b83` (XOM CreateObject). For switches: `pe.py words` on the byte table and the jump table (`movzx eax, byte [eax+B]; jmp [eax*4+J]`), §13. |
| FPU compare branches | after `fcom; fnstsw ax`: `test ah,5; jp` jumps when st0 >= src; `test ah,0x41; je` when st0 > src; `test ah,1; jne` when st0 < src; `test ah,0x44; jp` when not equal. |
| Worm anim event -> clip | callers of QueueEvent 0x5acb80 (last `push imm` = event code), consumer 0x5a3620 jump table 0x5a410c, clip slots registered at 0x5a49a0 (§11). |
| Which camera / target for targeting weapons | Blimp = `IsometricCam` (view 3); target ray `CMS::UpdateTargetInfo` 0x51c910 writes `Airstrike.*`; cursors from `Weapon.Create*Cursor` in 0x5009d0 (§10). |
| Scheme and timer values | `xom.py list Tweak/LOCAL.XOM Scheme`, then `xom.py dump Tweak/LOCAL.XOM '#N'`; timers in TimerLogicEntity 0x50f980 (§14). |
| Bundle contents | `xom.py list Bundles/BundlNN.xom Descriptor` (the descriptor u16 is the bundle number); `xom.py check` is exact on all bundles (§15). Geometry and clips: `tools/w4m-models`. |
| Net message fields and wire order | `pe.py schema 'Msg$\|MessageArray'`; registration order: `push 0x88xxxx` before each `call 0x70daf0` in 0x705bce (§16). |
| Menu tree | `tweak.py`, then load the PC menu databanks in exe order (§17) and walk `ChildrenItems` and `Messages_*` (`FE.ChangeMenu$X`, `WXMsg.CreatePopUp$X`). Clip lengths: `w4m-models --list Data/Bundles/Bundl10.xom`. |
| AI parameter reads | refs to the global 0x9560f4, then `[reg+disp]` against `pe.py schema AIParametersContainer` (§18). |
| A level cutscene | `xom.py list <Level>.xom EFMV`, Movie -> Track -> Event refs; Lua: `lua.py --all 'EFMV'` (§19). |

## 21. Not covered

- **Audio (§12):** the property-block fields +04/+08/+0C (pitch, pitch and volume randomisation) are named from FMOD Designer's order; some other fields, the sound-definition play-mode enum and the reverb block are unknown. Whether something else starts the HudClockEntity ClockFast/ClockSlow instances.
- **Weapons (§13):** what crate types 1 and 3 are (hidden from CrateSpy); what spawns kWeaponFatkinsFood and kWeaponSentryGunPayload; readers of ColliderFlags. WeaponType has a single reader found (0x5973e6).
- **Turn (§14):** acting trigger ids 0x1e and 0x2e are Missed and Retreat (name table 0x9214f0, docs/worm-reactions.md); camera mode 0xe, which freezes the turn clock, is not identified; RopeTime looks unused.
- **Tactical (§10):** whether the 6 planes of `AirstrikeCursor.xom` (scene 8, world origin) show.
- **Bundles (§15):** the 3 bytes after `CTNR`, the trailing bytes of `XMeshDescriptor` / `XCustomDescriptor`, key types 0x200 and 0x403, and the "cycle" channel setting.
- **AI (§18):** some AITWK fields have no reader (MortarMaximumAimAngleAllowed, AddScoreTeleport, WeightTeleport*, WeightRetreat*).
- **Rendering (§8):** the enum value orders are now read from the exe (`pe.py schema`); `kPS_Default` keeps the drawable descriptor's DefaultSceneIndex, 23 for every particle sprite and mesh (render.md §1); the emitter flag 0x2000 bin path's callers are not traced. Bloom and blur classes have no PC shader.

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
- Story / Challenges screens (2026-10-06): our items with no W4M clip (tabs, mission rows) and every W4M item whose Anim_Outgoing is None
  slide in / fly out like our menu rows (`rowAppear`) instead of appearing / vanishing at once (frontend.md §Story and Challenges).
- Network protocol stays version 1 until a server is deployed (PROTOCOL.md).
- Replay disabled (temporary, 2026-10-06): no instant replay, no `.w4r` recording, no Replays menu entry; `REPLAYS` in ui.h restores
  all three (docs/tests.md §replay_check.cpp).

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

- **Worms (§11):** the easing of DrownFloat and of helper 0x569f20; the slide-arms math; the weapon clip chaining Draw -> Hold/Aim -> Fire -> Taunt is assumed from names.
- **Audio (§12):** the property-block fields +04/+08/+0C (pitch, pitch and volume randomisation) are named from FMOD Designer's order; some other fields, the sound-definition play-mode enum and the reverb block are unknown. Whether something else starts the HudClockEntity ClockFast/ClockSlow instances.
- **Weapons (§13):** what crate types 1 and 3 are (hidden from CrateSpy); what spawns kWeaponFatkinsFood and kWeaponSentryGunPayload; readers of ColliderFlags. WeaponType has a single reader found (0x5973e6).
- **Turn (§14):** acting trigger ids 0x1e and 0x2e are Missed and Retreat (name table 0x9214f0, docs/worm-reactions.md); camera mode 0xe, which freezes the turn clock, is not identified; RopeTime looks unused.
- **Tactical (§10):** the render layer of the screen-centre reticle, and the left/right sense of the airstrike direction.
- **Bundles (§15):** the 3 bytes after `CTNR`, the trailing bytes of `XMeshDescriptor` / `XCustomDescriptor`, key types 0x200 and 0x403, and the "cycle" channel setting.
- **Network (§16):** the wire class id is assumed to be the registration order; the replay of received input messages is inferred from the structure, not traced.
- **Frontend (§17):** the per-frame update of menu entities; the `WXMsg.ScrewMenu` title stage; the Select path is assumed by symmetry with Cancel.
- **AI (§18):** some AITWK fields have no reader (MortarMaximumAimAngleAllowed, AddScoreTeleport, WeightTeleport*, WeightRetreat*).
- **EFMV (§19):** acting pools per trigger (jump table 0x60e818) and the Near radius (20·R units, squared at 0x60c4d0) are now in docs/worm-reactions.md; the `TargetCastMember -1` target (0x7e, position from [0x95a100]+8 vfunc 0x38) is assumed to be the camera; the PathCamera step timing.
- **Rendering (§8):** the enum value orders are now read from the exe (`pe.py schema`); which render bin `kPS_Default` maps to is still unknown. Bloom and blur classes have no PC shader.

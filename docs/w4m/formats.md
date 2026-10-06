# W4M file formats: XOM schema, Data index, bundles

Part of the W4M map (index, tools, tags: [README.md](README.md)).

## 1. XOM classes and the serialisation schema (disasm, verified on data)

**Class registry.** Each XOM class is registered by a static init stub:

- the stub pushes `Serialize`, a version, a factory, the parent type object and an info block (`info[0]` is the class name);
- it then does `mov ecx, typeobj` and `call 0x6c3c99` (abstract classes call `0x6c5015` instead);
- there are 486 classes, read by `PE.xclasses()`;
- these names are complete, whereas the XOM type table cuts them at 31 characters.

**Field records.** Each record is three u32:

| u32 | Content |
|---|---|
| 1 | name pointer |
| 2 | `flags << 24 \| index << 16 \| struct offset` |
| 3 | type object |

- Scalar records are in `.data`, array records (flag 1) in `.rdata`.
- Flag 4 marks transient fields (caches) that are not written to files.
- 0x20 = fixed-size array.

**Order in files.** Each class's `Serialize` pushes its records in file order, then calls its parent's. So a container is `CTNR`, 3 header bytes, the derived class's fields, then the base class's fields.

- `X*ResourceDetails` = Value, then `Name` (str), then `Flags` (u32).
- A field with struct offset 0 is absent from some files (`SchemeData.AssistedShotSettings`).

**Type objects.** They are created at runtime; their vtables name them:

| Type object | Type |
|---|---|
| 0x967614 | Boolean |
| 0x967620 | Uint8 |
| 0x96762c | Int8 |
| 0x967638 | Uint16 |
| 0x967644 | Int16 |
| 0x967650 | Uint32 |
| 0x96765c | Int32 |
| 0x967668 | Uint64 |
| 0x967680 | Float32 |
| 0x967698 | String (varint index into STRS) |
| 0x9676b0 | Bitfield64 |
| 0x9676c8 | Pointer |
| 0x9676d4 | Enum (u32) |
| 0x970b94 | Container (varint ref) |
| 0x96e9xx / 0x96ebxx | vec2/vec3/vec4, colours (RGB8 0x96e970, RGBA8 0x96e988, float RGB/RGBA 0x96e910/0x96e928), matrices 0x96ecb8 (4x3) / 0x96ecd0 (4x4) |

**Coverage.** `xom.py check` decodes every container to its exact end in all 38 `Tweak/*.XOM` (`PERSIST.XOM` included), the level databanks, the language files and all 475 `Bundl*.xom`. Bundles contain untagged containers and optional fields, which `xom.py` handles by walking containers in type order (§15); their geometry and animations are still extracted by `tools/w4m-models`.

## 9. Data/ index
Root: steamapps/common/WormsXHD. Counts are files directly in the folder.

### Top level (data)
| file | content |
|---|---|
| Default.cfg | `/RUN:WormsX /W:1024 /H:768`, `/STARTMENU:WXFE.MainMenu`, `/GAMEOVERMENU:WXFE.MainMenu`, `/RUMBLEMIN:0.5 /RUMBLEMAX:1.0`, `/CONFIG:local.cfg` (chained cmdline-style config) |
| local.cfg | `/W:1920 /H:1080 /REFRESH:60 /SSAA:1 /SHADOWMAP:1024 /CONFIG:user.cfg` (user.cfg absent) |
| lang_eng | 16 B text `lang_placeholder` |
| installscript.vdf | Steam install: vcredist, DX Jun2010, RegVideoDLL.exe (registers MemoryBufferedFilter.dll, a DirectShow filter for WMV); registry key `Team17 Software Ltd.\WormsUltimateMayhem` |
| WormsMayhem.exe 5.7MB, Launcher.exe | game exe (holds XOM schema, see tools/w4m-re/pe.py) |
| cg.dll, cgGL.dll, glut32.dll | NVIDIA Cg runtime + GLUT: renderer is OpenGL+Cg |
| fmodex.dll, fmod_event.dll | FMOD Ex + Event system (Data/Audio/PC .fev/.fsb) |
| XOM0-<host>.log | engine log written at run (12.8 KB) |

### Tweak/*.XOM (38 files, 2.1 MB) — dominant types (count)
| file | dominant types | role |
|---|---|---|
| AITWK | AIParametersContainer 24 | CPU AI difficulty params |
| CAMTWK | XFloatRD 50, ChaseCameraPropertiesContainer 6, SimpleCameraContainer 5 | in-game cameras |
| DEFSAVE | WXFE_UnlockableItem 469, CachedLeaderBoardWrite 104, InputEventMappingContainer 80, HighScoreData 51 | default save/profile, controls |
| EMPTY | (none, 257 B) | empty databank |
| HUDTWK | XFloatRD 66, XVectorRD 48, XColorRD 17 | HUD layout/colours |
| LOCAL | WeaponSettingsData 1145, FlagDataContainer 91, SchemeData | schemes (19), flags |
| LVLSETUP | WeaponSettingsData 58, LensFlareElementContainer 26, WeaponFactoryContainer 8 | default level setup, lens flare |
| MENUTWK | XColorRD 4 | menu colours |
| MENUTWKX | WXFE_GapColumns 568, ListBoxRows 495, ImageViewDesc 210 | main frontend menu descriptions |
| MENUTWKX2 | WXFE_GapColumns 151, ListBoxRows 140 | extra menus |
| MENUTWKXINGAME | WXFE_ListBoxRows 373 ... | in-game (pause) menus |
| MENUTWKXNET | WXFE_* 220/168/153/125 | network menus |
| MENUTWKX{PC,PS2,PS3,XBOX}COMMON | WXFE_MeshObjectParticleDesc 73, TextBoxDesc, ScrollingTextDesc | per-platform title scene/menus (PC one used) |
| MENUTWKX{PCUSA,PCPOLISH,PS2USA} | ~15 WXFE_* | region overrides |
| MENUTWKX{PCEURO,PS2EURO,JAMES} | empty 257 B | |
| MENUTWKXSTEVE / XBOX | 1-16 containers | dev / Xbox leftovers |
| OVERRIDEPARTTWK | ParticleEmitterContainer 11 | particle overrides |
| PARTTWK | ParticleEmitterContainer 874, EffectDetailsContainer 201 | all particle FX |
| PARTTWKPS2 | ParticleEmitterContainer 481, EffectDetails 110 | PS2 variant |
| PERSIST | WXFE_* + FMVTextLine 99 | persistent menus, FMV subtitles |
| PERSISTPC/PS3/XBOX/NET | WXFE_* | per-platform persistent menus |
| SCRIPTS | WXFE_LevelDetails 278, LevelDetails 1 | level list (docs/w4m-formats.md Missions) |
| STATS | CachedLeaderBoardWrite 104, Achievements* | stats/achievements |
| TWEAK | XFloatRD 92, WaterPlaneTweaks 34 | game physics, water, lighting |
| WEAPTWK | PayloadWeaponPropertiesContainer 23, BaseWeaponContainer 15 | weapon tweaks |
| WORMACTING | EFMV_WormLookAt 988, Emote 630, Track 479, PlayAnimation 367 | shared cutscene acting events |
| WORMATTACHMENTS | WXFE_AttachmentOffsets 1175, WXFE_WormAttachments 172 | hat/glove attachment offsets |
(RD = ResourceDetails)

Tweak tooling: all decode exactly with `tools/w4m-re/xom.py` / `tweak.py` (except PERSIST `WXFE_SoftwareKeyBoardData`). Documented: WEAPTWK, TWEAK, LOCAL, SCRIPTS, CAMTWK, PARTTWK, MENUTWKX*COMMON, WORMACTING (docs/w4m-formats.md, camera-w4m.md, worm-reactions.md, death-sequence.md). **Not referenced anywhere in repo: AITWK, HUDTWK, LVLSETUP, DEFSAVE, STATS, WORMATTACHMENTS** (PERSIST only in w4m-re README).

### Directory index
Conf: D = verified from bytes/types this pass, A = assumed from names.
| dir | files | ext | size | content / format | repo handling | conf |
|---|---|---|---|---|---|---|
| / | 15 | exe 3, dll 7, cfg 2, vdf, log | 13.3 MB | see top level table | pe.py (exe schema) | D |
| CG/ | 6 | .cg 5, .h 1 | 0.1 MB | Cg shader source: FixedFunction, Landscape, PostProcess, water, XenonVideo, Fxaa3_9.h | docs (water.cg, Landscape.cg), camera-w4m.md | D |
| Redist/ + directx/ | 1 + 157 | exe, cab 154 | 4.3 + 98 MB | vcredist, DirectX Jun2010 | none needed | D |
| EFMV/<Cutscene>/ | 36 dirs × 1 | LIP.txt | ~0 | lip-sync per cutscene line: `#<hash> <Line>.txt` then `frame,phoneme,<none>` (phonemes A, CONS, EI, L, O, MBP, Rest...) | **none** (only speech/ LIP.txt used) | D |
| speech/vo<bank>/ | 33 dirs × 1 | LIP.txt | ~0 | same format; hash -> line name for vo banks | w4m-import (docs/import.md) | D |
| Data/ (root) | 208 | .XOM | 1.7 MB | level databanks (`MOIK`), see families below | w4m-maps main.rs/mission.rs | D |
| Data/Bundles | 475 | .xom | 627.5 MB | `MOIK` XOM: meshes/skins/anims/XImage textures, see ranges | w4m-models, w4m-ui, w4m-maps (textures/decor) | D |
| Data/Maps | 936 | xan 222, hmp 192, csh 476, txt 46 | 257.7 MB | 241 stems. .xan `MOIK` landscape; .hmp raw f32+u8 (no magic); DAY/EVENING/NIGHT .csh cache (no magic); .txt material override | w4m-maps (xan/hmp/txt); csh ignored | D |
| Data/Tweak | 38 | .XOM | 2.1 MB | see table | xom.py, tweak.py, w4m-maps | D |
| Data/scripts | 145 | .lub | 1.0 MB | Lua 5.0 bytecode `\x1bLuaP` 01 04 04 04 06 08 09 09 04 (stdlib + per-level) | w4m-maps lua.rs, w4m-re/lua.py | D |
| Data/Language/PC | 44 | .xom | 4.6 MB | 11 langs (Ame Cze Eng Fre Ger Ita Jap Pol Rus Slo Spa) × 4: `<Lang>.xom` (in-game ~2.2k strings), `<L>FE` (~1.07k frontend), `<L>LS` (~1.57k, level/story text), `<L>Loading` (~41 loading tips); only XStringResourceDetails + XDataBank | w4m-maps mission.rs, w4m-ui (lang) | D (role of LS/Loading: A) |
| Data/Themes/Theme* | 11 dirs, 1-3 each | .txt | ~0 | `Theme<X>.txt` (material file: texture codes W01...), `<L> Detail List.txt` (`NN Name`), `<L> Texture List.txt`; Arctic/War/England have only theme txt | w4m-maps | D |
| Data/Themes/Custom/Bank01-10 | 1-3 each | .txt | ~0 | texture lists + per-map notes (e.g. Bank07 `Crate Britain.txt`) | w4m-maps (Themes) | D |
| Data/Frontend/Levels | 181 | .tga | 44.9 MB | 32-bit TGA level previews 256² | w4m-ui | D |
| Data/Frontend/Gallery | 109 | .tga | 55.1 MB | gallery/concept art TGAs | **none** | A (content) |
| Data/Frontend/mechanics | 7 | .tga | 35.6 MB | LoadBack* 1920×1080 backgrounds | w4m-ui | D |
| Data/HUD/Weapons | 42 | .tga | 10.5 MB | weapon icons 256² | w4m-ui | D |
| Data/HUD/Flags | 95 | .tga | 4.5 MB | team flags | w4m-ui | D |
| Data/Audio/PC | 107 | fsb 106, fev 1 | 297.7 MB | `FSB4` banks (mp2, voRussian PCM16): per-level/cutscene (story, Challenge*, Deathmatch*, Multi*, Outtake*, Tutorial*), `mu*` 13 music, `vo*` 33 voices, ambient, frontendmusic, frontendsfx, global, weapons, Cheer, Failures, Demo; `WormsX.fev` = `FEV1` event project | w4m-import (fsb); fev only in docs | D |
| Data/Audio/Speech | 33 | .lsd | 0.1 MB | text: `<name>Speech/<bank>/<Category></name>` + line hashes, `;` separated | w4m-import | D |
| Data/Audio/EFMV | 27 | .lsd | ~0 | same, `EFMV/<Cutscene>/<Line>` -> hash (narrator lines) | **none** | D |
| Data/FMV/ntsc | 22 (+6 English, +1 Logos) | .wmv | 80.8 + 320.2 + 11.1 MB | ASF/WMV (`30 26 b2 75`): Outtake* + OuttakeRecordingBooth_01-16; English/ Arabian, Camelot, Jurassic, WildWest, Welcome, Meet_The_Professor; Logos/Team17NTSC | **none** | D |
| Data/{Audio,Frontend,HUD,FMV,Language,Themes} | 0 | – | – | containers only | – | D |

### Data/*.XOM families (208)
| family | n | XOM types (sum, files) | role |
|---|---|---|---|
| `*-W3D.XOM` | 46 | XStringRD 140, XUintRD 92, XDataBank 46 only | Worms 3D-era map databank: `Databank.CustomDetailBank/CustomTextureBank/MaterialFile/Theme/TimeOfDay`, no mission |
| `RANDOM<THEME>` | 5 | strings/uints only | random-landscape databanks (Arabian, Building, Camelot, Prehistoric, WildWest) |
| story levels (TUTORIAL1-3, DOOMCANYON...) | ~25 | EFMV_TrackContainer, EFMV_*Event, WormDataContainer, CrateDataContainer, EFMV_MovieContainer, XContainerRD | mission + in-engine cutscene |
| `LP_*` | 34 | EFMV_* events, WormData 193, CrateData 169 | 25 full, 9 with only 3-6 containers (string stubs pointing at base level); w4m-maps strips `LP_`/`SPLP_` to share base databank |
| `MULTI_*` | 30 | WormData 381, CrateData 290, ParticleEmitter 176, EFMV_* | multiplayer missions; 21 are 3-type stubs, 9 full (1001Fights, AllSystemsOhNo, HardDaysKnight, Maimframe, MonsterBash, PeskyLittleBreeders, TerminalFerocity, TimeFlies, ToweringInfernal) |
| `OUTTAKE*` | 14 | EFMV_PlayAnimation 438, LookAt 430, CutCamera 352 | outtake cutscenes |
| `DEATHMATCH1-11` | 11 | WeaponSettingsData 638 (58/file) + EFMV | deathmatch levels with own weapon set |
| `CHALLENGE*` | 15 | CrateData 246, EFMV_TimedPathCamera | challenges |
| MULTIMODE1-4, MULTIPLAYER, DEMO_* | 8 | mixed | mode setups / demo |
| tests (ANIMTEST, ARABIATEST, HUDTEST, MOVIETEST, WEAPONSTEST, LEVEL1, MANEL, BOOSHEET, unitcube) | 9 | small | dev leftovers |
Repo: `docs/w4m-formats.md` (Map files, Missions), `w4m-maps/src/mission.rs` (kinds 4/8/9, skips `-w3d`). EFMV_* containers in level files: not imported (only WORMACTING referenced). (A for roles of MULTIMODE/tests.)

### Bundles numbering (Bundl00-474, all `MOIK`)
Empty (178 B header only): 01, 35, 51-56, 62-67, 158, 167-171, 243, 249-251, 287, 312-314, 353, 375, 408, 429, 471.
| range | content (types) | source | conf |
|---|---|---|---|
| 00 | Intro.Loading, LoadBack.Small, HintPanel, reticle (XImage 5) | strings | D |
| 02 | legal/ESRB, FMOD logo | strings | D |
| 03 | FE.Font: 31 XMultiTexFontPage + 31 XImage (42 MB) | docs | D |
| 04 | HUD.PiP | strings | D |
| 05 | FE.LoadingIcon (round worm, 1 mesh + clip) | w4m-ui | D |
| 06 | frontend: WX.Mesh.Title, FRONTEND.Sky, menu meshes, 57 desc / 125 XImage | docs | D |
| 07 | 39 meshes, 53 images | – | A (FE/misc) |
| 08 | FE.ComboButton, DebugMarker | strings | D |
| 09 | in-game: weapons, projectiles, crates, mines, oil drum, sheep, HUD XImages (171 desc, 129 clip libs, 338 img) | docs | D |
| 10 | frontend menus, seagull, borders, ticker strip (25 desc) | docs | D |
| 11 | 51 meshes, 14 clips | – | A |
| 12 | Temp.Gallery placeholders | strings | D |
| 13-23 | theme landscape textures (64-75 XImage): Arabian, Building, Camelot, Prehistoric, Wildwest, Arctic, England, Horror, Lunar, Pirate, War | strings `ThemeX\X01.tga` | D |
| 24-34 | theme detail meshes (20 desc each), same theme order | `ThemeX/X_det_detailNN.xom` | D |
| 36-45 | Custom Bank01-10 detail meshes (5 desc) | `Custom/BANKnn` | D |
| 46-50 | story-theme prop libraries ARABIANP, BUILDINGP, CAMELOTP, PREHISTORICP, WILDWESTP (4-6 desc) | strings | D |
| 57-61 | same 5 themes, `*STRIP` variants (20-47 desc + images) | strings | D (purpose A) |
| 68-92 | 5 per theme (Arabian 68-72, Building 73-77, Camelot 78-82, Prehistoric 83-87, WildWest 88-92): `<Theme>StatueIslands.xan` + other .xan prefabs, 1-6 desc, no images | strings | D (grouping), A (role) |
| 93-125 | sky/water per theme × time (2 desc + 8-12 img) | docs (93-97,108-113 DAY; 98-107,114-125 EVENING/NIGHT), w4m-ui | D |
| 126-135 | Custom BANK01-10 textures (20 img) | strings | D |
| 136-166 | Custom W3D_BANK11-41 textures (10 img) | strings | D |
| 172-248 | Hat.<Name> (1 desc + 1 img) e.g. Hat.Afro 172, Baseball 182, Crown 201, Pigtails 212, AlienBreed 240 | w4m-models comment | D |
| 252-286 | Glasses.* (e.g. Monocle 258, SherlockPipe 284) | strings | D (bounds A) |
| 288-311 | Mustache.* (Afro 288, Small.Brown 303) | strings | D (bounds A) |
| 315-352 | Gloves.* skinned on 34-bone worm skeleton (XSkin 1-2) | docs, strings | D |
| 354-374 | Grave.* (Arabian 354 ... DLC1.Army 370) with clip libs | strings, w4m-models | D |
| 376-407 | Factory.* weapon-factory parts (BlasterGunBarrel, RayGunButt...) | strings | D |
| 409-470 | Factory.Proj.* projectiles (Ray, Classic, Tank, 8ball ... DLC1.Payload1-5 466-470) | strings | D (bounds A) |
| 472-473 | HUD.HealthBar/HealthIcon (472), 473 same size | w4m-ui (472) | D/A |
| 474 | W4.Worm (34 bones, 329 clips, 3 XSkin, 18 img), Nav icons | docs | D |
Factory.*/Gloves.*/Glasses/Mustache/Grave(partly): **not imported by any repo tool**.

### Gaps (not handled anywhere in repo)
EFMV/*/LIP.txt, Audio/EFMV/*.lsd, FMV/*.wmv, Frontend/Gallery, *.csh, HUDTWK/LVLSETUP/DEFSAVE/STATS/WORMATTACHMENTS, EFMV_* containers in level XOMs, Factory/Glasses/Mustache/Gloves bundles. Documented but not imported: WormsX.fev (§12, `fev.py`), AITWK (§18), EFMV/WORMACTING (§19).

## 15. Bundles (`Bundl*.xom`)

475 files, 627.5 MB, all `MOIK`. Verified over all 475: a sequential type-order walk (below) ends exactly at EOF in every file. [data]

### 1. Header, type table, strings [data]
- 64 B header: `MOIK`, `u32 @4` = `00 00 00 02` (version, same in all bundles), `u32 @24` type count, `@28` container count (equals the sum of type counts in all 475), `@32` root container index (1-based). The root is always an `XGraphSet` (the resource directory, see 3).
- Type records, 64 B each: `TYPE`, `u32 @8` instance count, GUID @16, name @32 (31 chars + NUL). Types with count 0 are abstract bases (`XNode`, `XGeometry`, `XCoordSet`...).
- `GUID` (16 B), `SCHM` (`u32 1`, 12 B), then `STRS`: `u32 count, u32 bytes, count x u32 offsets, blob`. String 0 is `""`.
- Containers follow, grouped by type, in type-table order (not always "descriptors first": in 46-50/57-61/68-92 `XGraphSet` comes after `WXTemplateSet`; in Bundl09 after `PC_LandFrame`).

### 2. Container encoding: why splitting on `CTNR` fails [data]
- Schema classes: `CTNR` + 3 bytes + fields in `pe.py schema` order (derived class first). Header byte 0 is `00` (most), `01` (root `XInteriorNode`, most `XIndexedTriangleSet`, ~3.4k others), `08` or `04` (a few hundred); bytes 1-2 always `00 00`. Meaning unknown. [data]
- Untagged (no `CTNR`, no 3-byte header), custom serialisation: every `X*Descriptor`, `XGraphSet`, `XAnimClipLibrary` (1222 + 1582 + 92 + 49 + 1 + 1 descriptors, 1697 graph sets, 310 clip libraries). Splitting on `CTNR` merges them into the previous tagged container, so every later index is shifted. This is the main failure. [data]
- Schema fields with flag `0x20` (struct offset 0) are absent from bundle data: `XTextureStage` `FourCC` / `MaxMipMapLevel` / `Matrix` (all 3207 `XOglTextureMap`), `XFortsExportedData.BPVictoryLocation` (824/869), `XMultiTexFontPage` `CharCoords/CharSizes/CharKern*`. So flag 0x20 means "field added in a later version, optional". It is the same rule as `SchemeData.AssistedShotSettings`. [data + schema]
- Decoding with the full field list can read into the next container and still land on a `CTNR` (seen in `XFortsExportedData`, where empty arrays are 1 byte each). The correct end is the variant that lands on the *nearest* following `CTNR`. [data]
- No tagged payload in the shipped bundles contains the bytes `CTNR`. Schema end == next `CTNR` for every tagged container, so the problem is only the untagged containers and the variant choice. [data]
- Type objects `pe.py` leaves as hex: `0x96ece8` = bounding sphere (4 f32: centre, radius), `0x96ed00` = bounding box (6 f32), `0x96e9e8` = 2 f32 (tex coords), `0x96eb08` = 3 f32 (normals). [data: exact ends]

### 3. Untagged formats [data; field names from schema where they match, else assumed]
| type | layout |
|---|---|
| `XMeshDescriptor` | varint name, **u16 bundle number**, varint ref -> `XGraphSet` (scene), 2 bytes (`08 00` 655, `00 00` 314, `08 02` 211, `01 00` 36...; meaning unknown) |
| `XBitmapDescriptor` | name, u16 bundle, ref -> `XTexFont` (sprite: image + UV rects), u16 w, u16 h (`80 00 80 00` = 128x128 ...): the drawn size in units (sprite Size = w / 2, h / 2, render.md XBitmap quad size) |
| `XSpriteSetDescriptor` | name, u16 bundle, ref -> `XGroup` |
| `XCustomDescriptor` | name, u16 bundle, 2 bytes (`01 00` 46, `00 00` 3) |
| `XTextDescriptor` | name, u16 bundle, ref graph, u16 glyph count k, 2 B, k x 6 B char map (FE.Font, Bundl03) |
| `XNullDescriptor` | name (`NULL`), u16 bundle (Bundl04 only) |
| `XGraphSet` | varint n, n x (16-byte role GUID, varint ref, varint name) |
| `XAnimClipLibrary` | see 5 |
- The u16 bundle number equals the file number in all 4,947 descriptors. The engine builds the path with `sprintf(fmt, id)`: `AppDataService` 0x4d7aa0 calls resource-manager vtbl+0x60 with `"Bundles/"`, `"Bundl%.2d.xom"` (the library default is `"Bundl%03d.xom"`, set by 0x6acd00 / 0x6af930). Formatting sites: 0x6ae4cf, 0x6ae6dc, 0x6b017f (`movzx word id; push fmt; call 0x63882d`). [disasm]
- No index file exists outside the bundles: no non-bundle XOM holds descriptors. Name -> bundle comes from each bundle's own descriptors. [data; the runtime lookup was not traced]
- **XGraphSet role GUIDs** (first 4 bytes) -> target and name, counts over all bundles:
  `99cc436e` resource directory (root set; refs to descriptors, name = resource name) 2947; `6ae6dbe4` scene root (`XInteriorNode` "world" 1090, `WXTemplateSet` "Templates" 111, `PC_LandFrame`) 1222; `5ce9bd39` `XAnimClipLibrary` 310; `bb62fcf6` `XExpandedAnimInfo` 259; `ffd7103d` `XAnimInfo` 31; `edc28fc5` `XCollisionData` ("Collision Data" 1097 / "Phantom Collision Data" 1080); `5f3094db` `XDetailObjectsData`; `e1d7ef28` `XFortsExportedData`; `9c59206c` `XXomInfoNode` (Maya export info: computer, user, .mb path, date); `ebf58e96` `XPathFinderData`; `9e84c023` `XPositionData`.
- A mesh descriptor -> its graph set -> scene root + clip library + collision. This is the chain `w4m-models` follows.

### 4. Classes present (instances, files) [data]
Scene: `XGroup` 5405, `XInteriorNode` 1106, `XShape` 2003, `XTransform` 1931, `XJointTransform` 1639, `XBone` 1639, `XSkin` 89, `XSkinShape` 145, `XBinModifier` 57, `XChildSelector` 19, `XMatrix` 194, `XSceneCamera` 5. Geometry: `XIndexedTriangleSet`/`XIndexSet`/`XTexCoord2fSet` 2029, `XCoord3fSet` 2025, `XNormal3fSet` 1317, `XColor4ubSet` 665, `XConstColorSet` 44, `XPaletteWeightSet` 137, `XCollisionGeometry` 1083, `XCollisionData` 2177. Material: `XSimpleShader` 1611, `XOglTextureMap` 3208, `XImage` 3110, `XMaterial` 368, `XLightingEnable` 418, `XBlendModeGL` 196, `XZBufferWriteEnable` 157, `XAlphaTest` 110, `XCullFace` 109, `XDepthTest` 105, `XTexturePlacement2D` 40, `XEnvironmentMapShader` 2. 2D: `XTexFont` 1674 (sprite = image + rects, despite the name), `XMultiTexFont`/`XMultiTexFontPage` 1/31 (Bundl03), `XBillboardSpriteSet` 89, `XPlaneAlignedSpriteSet` 3. Anim: `XAnimClipLibrary` 310 (117 files), `XExpandedAnimInfo` 259 (u32 flags), `XAnimInfo` 31 (2 bools: animated alpha/colour). Export metadata: `XFortsExportedData` / `XDetailObjectsData` 869 (empty in practice), `XXomInfoNode` 146 + `XExportAttributeString` 584. Landscape, in 35 files (24-45: theme / custom detail banks): `LandFrameStore` 24711, `DetailEntityStore` 7133, `WXLumpConnector` 2278, `WXLumpBoundBox` 1160, `WXTemplate` 816, `WXTemplateSet` 111, `XPathFinderData` 228, `XPositionData` 212. `PC_LandChunk` 7 / `PC_LandFrame` 5 (Bundl09: girder / exported landscape graphs).

### 5. Animation (`XAnimClipLibrary`, untagged) [data; layout as in w4m-models, stats over all 310 libraries / 1066 clips / 20112 channels]
- varint name (the Maya source path, e.g. `Grenade.xom`), u32 key-type count, key types (u32 type, varint string = node path `a|b|c`), u32 clip count. Per clip: f32 duration (s), varint name, then **always** u32 channel count + per channel u16 key-type index. The 0x100/0x101 "one channel per key type" form handled by w4m-models never occurs. The u16 `0x100` "skip 16 bytes" case never occurs either.
- Channel: 4 flag bytes (`01 01 00 00` 15748, `01 01 01 00` 3745, `01 01 00 01` 574, `01 01 01 01` 45: `XAnimChannel` MustContribute, IsWeighted, IsStatic, IsLinear [data, schema]), 8 B pre/post infinity (u32 each, `InfinityType`: 0 constant 19218, `2,2` = kInfinityCycle 888, `2,0` 6 [data, schema]). The curve evaluator 0x7abb1c sends a time before the first key (pre ≠ 0) or after the last (post ≠ 0) to 0x7ab3d1: Maya's infinity (linear by the end tangent, cycle, cycle relative, oscillate) [disasm]; `tools/w4m-models` `eval` follows it, u32 key count, keys of 6 f32 `(in-weight, in-angle, out-weight, out-angle, time, value)`: Maya-style Bezier tangents, angle in radians.
- Time in seconds. Most common key spacings: 2.0, 0.5, 1/12, **1/24**, 0.25, 1.0, 1/8, so authored at 24 fps (Maya film). In 721 channels the last key is past the clip duration (sampling clamps).
- Key types (low 24 bits; top byte = axis 0/1/2): `0x103` rotate (euler radians, XYZ, applied Rz·Ry·Rx) 1348, `0x102` translate 940, `0x904` scale 805, `0x104` scale (other variant) 105, `0x401` texture offset 58, `0x1100` texture switch (`XChildSelector`) 19, `0x200` axis 3 (44; unknown, maybe visibility / colour alpha), `0x403` (1; unknown, texture). No quaternion or compressed keys: everything is f32 curves. [data; 0x200/0x403 meaning assumed]
- Key type low byte = the XOM field index of `pe.py schema` (XTransform 02 Translate / 03 Rotate / 04 Scale, XTexturePlacement2D 01 Offset, XChildSelector 00 SelectedChild), byte 1 = the attribute's blend flags (docs/worm-reactions.md "XAnim blend") [data + schema + disasm].
- Rotation encoding: per-axis euler curves on `XJointTransform.Rotate` / `XTransform.Rotate`. The joint matrix is T · R(JointOrientation) · R(Rotate) · R(RotateAxis) · S, as in w4m-models. [data + w4m-models]

### 6. Model containers [data, w4m-models + schema]
The scene graph, geometry, skin and image layouts in `docs/w4m-formats.md` ("Meshes, skeletons and animations") match the exe schema field for field. Schema names: `XIndexedTriangleSet` = IndexSet, Flags, PrimitiveCount, Coord/Normal/Color/TexCoord/Weight sets, BoundBox (6 f32), BoundMode, VertexShader. `XShape` = Flags, Shader, Geometry, SortKey, Parameters[], Pre/PostRenderFunc, Bounds, BoundMode, Name. `XImage` = Name, Width, Height, MipLevels, Flags (u16 each), Strides[], Offsets[], Format, Data[], Palette. `XPaletteWeightSet` = Indices u8[], WeightCount u16, Weight f32[]. `XBone` = PoseMatrix, Transform (4x4), Affine, then XInteriorNode. Uncompressed: f32 positions/normals/UVs, u16 triangle lists. Only `XImage` formats 9/10/11 are DXT-compressed.

#### XChildSelector (texture alternatives) [data + disasm]
- One field, `SelectedChild` u32 at +0x14 (rec 0x941530, setter 0x6a0710); the node draws only that child. Every one of the 19 in the game stores 0 [data].
- A clip keys it with type 0x1100 at child + 0.5. The XAnim write is the u32 accessor 0x6c7243: the evaluated float through `_ftol` 0x6fe6c6, truncation [disasm]. Several playing clips combine by maximum, not by weight (attribute flag 0x10, 0x7ac1a0; docs/worm-reactions.md "XAnim blend") [disasm]. A selector no playing clip keys keeps its last value.
- All 19, children = `XShape` / `XSkinShape` sharing one geometry, only the shader image differs [data, `w4m-models --list` "selector"]:
  - `Landmine` `$animTex0` and `LandmineLow` `$animTex1` (2 children): `MineOn` 0.958 s, stepped 0.5 / 1.5 every 0.25 s from 0.208 s (lamp off / on).
  - `SentryGun` `$animTex0` (4): `Blue` 0.5, `Red` 1.5, `Green` 2.5, `Yellow` 3.5 (constant).
  - `AlienAbduction` `$animTex4` (2: window lights dark / green) in every Abduct* clip, 0.5 except `AbductViolate`, which flickers it and `$animTex5` (2) between 0.5 and 1.5 every 1/24 to 1/8 s, both ending at 0.5.
  - `W4.Worm` (Bundl474) `$animTex0` (3, `XSkinShape`), `$animTex1` / `$animTex2` (4 each, both eyes share the images: eyelid open, half shut, shut, and a 4th keyed by one clip): keyed by 170 / 165 clips (blinks, Yawn, emotes; Base holds 0.5); `$animTex0` only by Brake, Teeth, Titter, WiggleBrows.
  - `FE.Wormpot.Body` (Bundl07) `$animTex11..21` (2 each): the `Button*_Loop` / `_Reset` highlight clips.
- `Starburst` has none (1 image); the Starburst weapon holds the `Sheep` mesh, which has none either [data].

### 7. Bundle index
See "Bundles numbering" above (unchanged). Additions from this pass [data]:
- Every bundle names its own resources in its root `XGraphSet` (`99cc436e` entries). `xom.py list Bundles/BundlNN.xom 'Descriptor'` now lists them.
- 24-45 (theme/custom detail banks) are full landscape XOMs (`LandFrameStore` etc.), not only detail meshes.
- Hand/glove meshes are named in the exe as Maya source files (`HandWorms.xom`, `HandAlienBreedP.xom`, `HandDLC*`...: `P` = paired/alt variant, assumed). They match `Gloves.*` in 315-352.

### 8. Relation to `tools/w4m-models`
- Handles: untagged descriptors / graph sets / clip libraries (its `exact()` = the table in 3), schema-sized types up to the next `CTNR`, scene walk (`XGraphSet` -> `XInteriorNode`/`XGroup`/`XSkin`/`XBinModifier`), `XShape`/`XSkinShape`, `XIndexedTriangleSet` + sets, palette weights, `XSimpleShader` first stage -> `XImage` formats 0/1/2, joint/transform/matrix cores, `XChildSelector` (child 0's mesh kept unmerged, every child's image as a material; glb root extras `sel`: per selector its primitive and child materials, per clip keying it the value at 60 fps, read by `Models`), clips with Bezier eval, 30 fps resampling.
- Skips or ignores: DXT images (9/10/11), `XMaterial` colours, render states (blend / alpha test / cull / z), stages after the first, `XTexturePlacement2D` / `0x401` UV animation (parsed as a key type but not exported), `0x200` / `0x403` keys, pre/post infinity (always clamps; cycle `2` ignored), the channel flags, `XCollisionGeometry` / `XCollisionData`, `XBillboardSpriteSet` / `XPlaneAlignedSpriteSet`, `XSceneCamera`, `XEnvironmentMapShader`, `XAnimInfo` / `XExpandedAnimInfo`, and the descriptor trailing bytes.
- Dead code: the `0x100`/`0x101` channel form and the u16 `0x100` skip (never in data, harmless).
- `exact()` reads `XBitmapDescriptor` as v,+2,v,+4 and `XTextDescriptor` as v,+1,v,v,...; the second only works because the u16 bundle id 3 has a zero high byte. Read it as name, u16, ref, u16 k.

## 22. Exact land export (`<map>.cells`, tools/w4m-maps)

Source [data]: the visible `LandFrameStore` poxel cells (a hexahedron of 8 lattice corners each) and the `.hmp` heightmap, placed and
scaled as in docs/w4m-formats.md "Conversion to our grid". W4M collides with that lattice itself: land ray 0x466ae0, face entered 0x46a070
(physics.md §5, §11) [disasm]. The client's use: docs/sim.md "Exact land".

File [ours]: `"W4C1"`, u32 payload size, then the payload as raw DEFLATE (RFC 1951, one fixed-Huffman block; the client inflates it with
raylib's `sinflate`). Payload, little-endian:

- u32 hexahedra, u32 heightmap columns (0 or NX·NZ), u32 cells, u32 lists, u32 list bytes, u32 cell bytes.
- Per hexahedron: 8 corners as 3 f32 (m; corner bit 1 = +x, 2 = +y, 4 = +z), u32 flags: bit t (0..11) triangle t kept (face t / 2 of
  `{0,2,6,4} {1,3,7,5} {0,1,5,4} {2,3,7,6} {0,1,3,2} {4,5,7,6}`, triangle (f0, f1, f2) for even t, (f0, f2, f3) for odd), bit 12 + t its
  plane flipped to face out, bit 24 twisted (planes reaching past its corners: also bounded by its box). The client rebuilds the planes
  from these without deciding anything.
- Heightmap: f32 top per grid column (m), NaN where none, x fastest.
- Lists, varints: the word count, then per op `kind | id << 3`: kind 0 = a hexahedron (id = its index minus the list's previous
  hexahedron's), followed by its plane mask; kind 1 = the heightmap.
- Cells, varints, ascending: the cell index minus the previous one minus 1, then its list's number. Cell index `(z·NY + y)·NX + x`
  on the 320 x 256 x 320 grid of 0.25 m cells.

Build [ours]: a cell lists a hexahedron unless one of its planes has the 8 cell corners outside (> 1e-5 m); the mask keeps the planes
with a corner on or outside (> -1e-5 m), so a face lying on the cell's border stays with the cell it bounds. A cell some hexahedron or the
heightmap fills is left out; its sign is in the `.vox`, whose grid-point signs are set from the lists (from a listed cell around the
point, else solid when a filled cell touches it). Identical lists are stored once. 221 maps: 282 MB, 15 KB to 2.6 MB each.

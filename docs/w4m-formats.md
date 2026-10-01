# Worms 4 Mayhem data formats (reverse-engineered)

Sources: AlexBond2/Worms3DTools (XomView `XomLib.pas`/`XomCntrLib.pas`), w4tweaks wiki (poxel/height-map pages), plus byte inspection of the Steam *Worms Ultimate Mayhem* data. Implemented in `tools/w4m-maps` and `tools/w4m-models` (Rust). All game data stays local; nothing extracted is committed.

## XOM container (`.xan`, `.xom`)

Little endian. Header 64 B: `MOIK`, …, `u32 @24` type count, `@28` container count, `@32` root container index (1-based). Then one 64 B record per type: `TYPE`, `?`, `u32 @8` instance count, `?`, GUID (16), name (32, NUL padded). Then `GUID`/`SCHM` blocks and `STRS`: `u32 count, u32 bytes, count × u32 offsets, string blob`. Then the containers, grouped by type in header order, each starting with `CTNR` + 3 bytes (splitting on `CTNR` works for every map file; bundles need per-type parsing). Integers/indices inside containers are 7-bit varints (`0x80` = more); strings are varint indices into `STRS`; references are 1-based container indices.

## Map files (`Data/Maps/<Map>.*`)

| file | content |
|---|---|
| `.xan` | the landscape: a tree of `LandFrameStore` ("poxels") + `DetailEntityStore` (objects). 222 files, all parse. |
| `.hmp` | 50000 B: 100×100 `f32` heights in 0..1 (row = z, column = x), then 100×100 `u8` second-texture mask (importer: bilinear, thresholded at 128 → `Heightmap.SecondTexture`). All zero for Worms 3D-era (`-w3d`) maps. |
| `DAY/EVENING/NIGHT.csh` | engine cache, not decoded: byte-identical across the three times in 152/158 maps, so not sun lighting. Per-poxel blocks (u32 count, count × 4 B, `c5c5c5c5`, then a u16 per voxel). Ignored. |
| `.txt` (some) | material list override, same layout as the theme file. |
| `Data/<MAP>.XOM` | level databank: strings `Databank.MaterialFile` (e.g. `ThemeCamelot\ThemeCamelot.txt`), `Databank.Theme` (`CAMELOT`…), `TimeOfDay`, `Heightmap.BaseTexture`/`SecondTexture` (each value string precedes its key in `STRS`), plus mission data (worms, crates, cutscenes). LP_/SPLP_/Multi_ variants reuse their base level's databank. |

### LandFrameStore (poxel)

After `CTNR`+3: name (varint), position, orientation (euler radians, matrix `T·Rz·Ry·Rx·S`), scale (3×3 f32). If the byte 0x4c further on is 0 the frame is a group (root): skip 0x60 bytes, then the child list. Otherwise:

- 19 f32: floor/wall texture X/Y vectors, floor/wall texture offsets, centre offset (always 0 in shipped maps).
- `EdgeOffset1`, `EdgeOffset2`: varint count (32) × (f32 x, f32 z): per horizontal lattice plane `j` (0..YSize) the offset of the min / max edge of the slab. This is how cones, spheres, tapering cliffs and sagging rope bridges are made.
- `XSize, YSize, ZSize`: 3 bytes (voxel counts, ≤ 32 high).
- height map: varint `(X+1)(Z+1)` f32 vertical offsets of the top plane corners (index `i + (X+1)k`).
- 12 B flags: `[0..2]` bools (crinkle / perturb cliffs?), f32 edge falloff, `[6]` visible, `[7]` smooth shading, u32 tint (`ffffffff`).
- voxels: varint `X·Y·Z` × u32, index `y + Y(x + X z)`. Bits 0-1 = solid (3) or empty (0), bits 2-7 = material 0..63 (theme entry), bits 8+ = unknown per-face/edge flags.
- detail list (varint count + DetailEntityStore refs), then child list (varint count + refs). Children use the parent matrix without scale.

Lattice vertex `(i, j, k)` in local voxel units: `x = e1x + (X + e2x − e1x)·i/X`, `z` likewise, `y = j` (+ height map on the top plane), centred by `−(X, Y, Z)/2`. Units ≈ metres-ish; maps span ~60–160 × 30–100 × 60–160 units.

### Heightmap placement (fitted, not found in data)

Covers x, z ∈ [−80, 80] (fits poxel footprints on DoomCanyon/StormTheCastle). Height `y = 5·h − 1.5` was fitted so that poxel floors are not buried; W4M water assumed at y = 0. Both constants live at the top of `tools/w4m-maps/src/main.rs`.

### Conversion to our grid

Scale k = fit into 78 x 60 m (max 1). Each solid poxel voxel becomes a hexahedron (12 triangle planes); grid points inside one are solid. Faces whose outside is covered by another cell or the heightmap are dropped; the stored density is the exact distance to the remaining faces within 1 voxel (0.25 m), signed by occupancy, so surface nets gives flat floors and crisp edges. Cells missing every grid point (thin planks, cone tips) still claim their nearest one.

## Lighting (`Data/Tweak/TWEAK.XOM`, `CG/*.cg`)

`XContainerResourceDetails` `Water.<THEME>.<TIME>` (`DAY`/`EVENING`/`NIGHT`; the level's time is the databank `Databank.TimeOfDay`) → `WaterPlaneTweaks`: 3 header bytes, RGBA water colour, 3 varint strings (water diffuse / normal / env textures), 14 f32 water parameters, then the land light, matching the field names in the exe: `LightDirection` (3 f32, towards the sun, world space, y up), `LightAmbient`, `LightDiffuse`, `LightSpecular`, `LightFresnel` (RGB8 each), `LandSpecularPower` (f32), `LandFresnelColour` (RGB8), `LandFresnelPower` (f32). E.g. Wild West evening: dir (0, 0.3, -0.9), ambient (180, 146, 133), diffuse (153, 135, 128).

`CG/Landscape.cg` (the PC terrain shader): `out = saturate((diffuse·max(n·l, 0)·shadow + ambient)·tex + specular·0.6·shadow·(n·h)^20 + (0.5 + shadow/2)·(0.2, 0.275, 0.175)·fresnel·(1 − v·n)^1.5) · vertexColour`, shadow from a real-time 3×3 PCF shadow map. Models use `CG/FixedFunction.cg` (same diffuse + ambient + specular + fresnel rim); worms get the fixed `Worm.Light.Ambient` (0.5, 0.5, 0.6) / `Worm.Light.Diffuse` (0.7, 0.7, 0.6) from TWEAK.XOM. The sky bundles' `LightGradient_<L>_<TIME>` bitmap is the `<L>_Sky0n.tga` ramp (`_S` = `<L>_SideSky0n.tga`); each sky scene has a `Sun` locator.

The importer writes the level's light as the map JSON `light` (+ `time`); the client applies the Landscape.cg model with per-vertex baked ambient occlusion and sun shadows instead of a shadow map (`Terrain::bake`).

## Themes and textures

`Data/Themes/Theme<X>/Theme<X>.txt`: 64 materials × 7 lines: top texture, side texture, ?, bump/second texture (`NULL`), surface sound (`Rock01`, `grass01blend`…), ?, blank. Textures (`C01`…) are `XImage`s in `Data/Bundles/Bundl13..21.xom` (one bundle per theme; Worms 3D banks in `Bundl15x`): name (path string `ThemeCamelot\C01.tga`), w, h (u16), mips (u8+pad), flags (u16), strides (count + u32s), mip offsets (count + u32s), format (u32: 0 = R8G8B8, 1/2 = 8888, 9/10/11 = DXT), varint size, pixels top mip first. The importer writes the top mip of format 0/1/2 images (1660 textures) as `maps/tex/<stem>.qoi` (only those used by an imported map) and keeps their average as the fallback palette colour.

### DetailEntityStore (detail objects)

Referenced from a poxel's detail list (every entity of a map is reachable that way). After `CTNR`+3: name (varint), library (varint), then 16 f32: position (poxel-local, centred, deformed lattice: same frame as the lattice vertices above), rotation (euler, `Rz·Ry·Rx`: ~80 % upright, the rest tilted / on walls / hanging), voxel cell (integer floats, the poxel voxel holding it), scale (often non-uniform), then always `0, 0, 0, -1` and 5 bytes `01 00 00 00 00`. Verified against the frontend previews (`Data/Frontend/Levels/*.tga`) and in game: objects sit on the poxel surfaces.

- Name prefix `VISIBLE`/`visible`/`VISABLE` (+ `_MORTAL`) = rendered decor; others are editor/script markers: `spawn` / `STANDIN WORM` (`CheesyGrinWorm`), `mine`, `oildrum`, `Crate.*`, `Camera`, `Lookat Point`, `LIGHT` (`PNTLGHT r g b radius`), `EMITTER_*` (`PARTICLE EMITTER`), `PLUG_*` (`Sound Effect`), `Collision Sphere`, `BOUNDS`.
- Library = `XMeshDescriptor` name in the theme detail bundle: `<THEME><n>`, n = line of `Data/Themes/Theme<X>/<L> Detail List.txt` (`CAMELOT18` lamp pole, `PREHISTORIC18` Tyrannosaurus statue, `WILDWEST8` skull...), or `Dxx_yy` for the `Themes/Custom` banks. Camelot `Bundl26`, Prehistoric `Bundl27`, Arabian `Bundl24`, Wild West `Bundl28`, Horror `Bundl31`; 136 meshes used by the shipped maps (`SHRINK` has none).
- Detail meshes are static XOM scenes (a few have a sway clip, ignored) with RGB8/RGBA8 textures; 20 mesh units = 1 world unit (the 25-unit worm mesh ~ 1.25 voxels; lamp pole 54 units -> 2.7).
- Big set pieces (EscapeFromTreeRex's T-Rex, trees, castle walls) are poxels, not details. Shipped maps hold 0-194 visible details, mostly grass and flowers.

Imported by `tools/w4m-maps` (`src/mesh.rs`, static subset of `w4m-models`): meshes go to `models/decor/<lib lowercase>.glb` in world units, placements to the map JSON `objects` (position through the poxel's scaled matrix, basis = poxel rotation · detail rotation · detail scale, then the importer's k / offset).

## Meshes, skeletons and animations (`Data/Bundles/*.xom`)

Implemented in `tools/w4m-models` (glTF binary output). Splitting on `CTNR` does not work for bundles: some containers have no tag, and a few tagged ones are followed by untagged ones. Every container is read in type order; types sized by their content (XomView `ReadXContainer`, see `exact()`) are parsed to their end, the others run to the next `CTNR`. Data after the tag starts with 3 header bytes, except descriptors, `XGraphSet` and anim data (no tag, no header).

| type | layout after the header (varint = 7-bit, ref = 1-based container index, set = varint count + refs) |
|---|---|
| `XMeshDescriptor` | name (`W4.Worm`, `Bazooka.Payload`, `Crate.Health`, ...), 2 bytes, ref → `XGraphSet` |
| `XGraphSet` | count × (16-byte GUID, ref, name): scene roots, `XAnimClipLibrary`, collision data |
| `XInteriorNode` | set children, bounds (16), bound mode (u32), name |
| `XGroup` | ref transform (`XJointTransform`, `XTransform`, `XMatrix`, or `XChildSelector` = texture-animation switch: children are alternatives), set children, bounds, mode, name |
| `XBinModifier` | 2 bytes, ref matrix, set children (eye colour overlays) |
| `XSkin` | ref skeleton root group, set `XSkinShape` |
| `XSkinShape` | set `XBone` (the shape's bone palette), u32 flags, ref shader, ref geometry, u32 sort key, 3 refs, data name |
| `XShape` | u32, ref shader, ref geometry, 7 bytes, data name (rigid, transformed by its parent groups) |
| `XBone` | pose matrix (4x4 f32, column-major: inverse bind), transform matrix, affine string, set, data name. Appears as a child of the group it follows |
| `XJointTransform` | 5 × vec3: rotate axis, joint orient, translate, rotation, scale, then u32 and the 4x3 rest matrix. Local = T · R(joint orient) · R(rotation) · R(rotate axis) · S, each R = Rz·Ry·Rx |
| `XTransform` | translate, rotate (euler), scale, rotate order, 4x3 matrix. Local = T · Rz·Ry·Rx · S |
| `XIndexedTriangleSet` | ref `XIndexSet` (u16 triangle list), u32 flags, u32 primitive count, refs coords, normals, colours, texcoords, weights, bbox |
| `XCoord3fSet` / `XNormal3fSet` / `XTexCoord2fSet` | varint n, n × 3 (2) f32 |
| `XPaletteWeightSet` | varint k, k bytes of palette indices, u16 per-vertex influences (2-3), varint k, k × f32 weights |
| `XSimpleShader` | set texture stages (→ `XOglTextureMap`: blend u32, colour 16, ref `XImage`, ...), set render states, u32, name |

Skinning is `v' = Σ w · World(bone's group) · Pose(bone) · v`; the bind pose differs from the stored joint transforms. Models are y-up and face +z; the worm is ~25 units tall in its Base pose.

`XAnimClipLibrary`: name, u32 key-type count, key types (u32 type, object name = group path like `main|head`), u32 clip count, then per clip: f32 duration, name, u16 0x100/0x101 (one channel per key type) or u32 channel count with a u16 key-type index per channel; each channel: 4 flags, 8 bytes pre/post infinity, u32 key count, keys of 6 f32 (in-weight, in-angle, out-weight, out-angle, time, value), Bezier-interpolated. Types: `0x102` translate, `0x103` rotate (joint `rotation` / transform euler), `0x104`/`0x904` scale, `0x401` texture offset, `0x1100` texture switch; the top byte is the axis. Clips are layered on `Base` (XomView "base clip"; assumed for all clips, the poses look right): a value is added to Base's first key when Base has the channel (scale: averaged), channels a clip lacks come from Base. `Aim*` clips map the aim pitch to their 2 s timeline and only move the body; `Hold*` clips add the hands (hands are scaled to 0 otherwise).

Locations: the worm (`W4.Worm`, 34 bones, 329 clips) is in `Bundl474`; weapons, projectiles, crates, mines, oil drum, sheep (14 bones, own clips) in `Bundl09`. `Bundl315`-`352` hold customisation (hats, gloves) on the same 34-bone skeleton.

`XImage` formats: 0 = RGB8, 1/2 = RGBA8 (bytes used as R, G, B, A), 9/10/11 = DXT (not needed by the exported models).

### Conversion (`tools/w4m-models`)

Each model is normalised (feet or centre at the origin, size from a table) and written as `client/assets/models/<name>.glb` with stored-deflate PNG textures. Static models get their rest pose baked into the vertices. Skinned ones (worm, sheep) get one flat joint node per bone with an identity inverse bind matrix, and every clip is sampled at 30 fps into that joint's full skinning matrix (TRS). raylib then rebuilds `inverse(bind) * pose` without a node hierarchy or shear. raylib-nx skins on the CPU (`SUPPORT_GPU_SKINNING 0`): `Models::draw` poses the shared mesh right before each draw and skips it when the pose has not changed.

### Frontend title scene (`tools/w4m-models`, `client/src/frontbg.cpp`)

The animated menu backdrop is not a landscape (no `Frontend*.xan`) but plain meshes, placed by the menu tweaks (`Data/Tweak/MENUTWKX*COMMON.XOM`: `Title Mesh` = `WX.Mesh.Title`, clips `Intro_Title` / `Loop_Title` 11.67 s / `Outro_Title`, plus `WXP_FE_*` particle effects):

- `WX.Mesh.Title` (`Bundl06`, Maya `TitleMesh.xom`): a ~116 x 62 x 116-unit island diorama, 73 static parts / 18 textures / 13.6k triangles: `Land|BaseLevel` cliffs, `Time_Machine` (the orange wreck stuck in the cliff), `BarrelsBlasts` (oil/poison drums), `RocksTrees1/2`, `Tree1-4` (the clips move groups; rigid, no skin). Empty groups are particle locators (rest world position, title units): `Tree_Fire` / `Tree_Smoke` (-9, 7, 50), `Crash_Fire` (-9, 13, 12), `Crash_FireSpark`, `Crash_Sparks` (-11, 14, 13), `Barrel_Spark` / `Barrel_Poison` (7, 9, 26), `Flies` (7, 13, 27); effects `WXP_FE_TreeSmoke`, `WXP_FE_CrashFire`, `WXP_FE_BarrelPoison`... (`PARTTWK.XOM`). Colour is texture x vertex colour (`XColor4ubSet`: 3 header bytes, varint n, n x RGBA8, the third geometry ref of `XIndexedTriangleSet`).
- `FRONTEND.Sky` (`Bundl06`, `TITLE SKY.mb`): a 25000-unit dome (white 8x8 texture, gradient in the vertex colours) + cloud cards (`cloud2`, 1024² and 512x128 RGBA). `FE.WaterBlend`: a 19000-unit bowl with a white-to-clear 64² ramp (horizon blend; not imported, our water fog does it).
- Seagull: `WXP_FE_Seagull` spawns mesh particle `Particle.WXPMesh31` (`Bundl10`, 7 bones, 20 units wingspan) playing `LOOP:WXM_SGull_Location+LOOP:WXM_SGull_WingFlap` (14 s each): the flight path is in the Location clip, so a placement + time offset is enough.
- Per-page cameras: not found in the data (no camera node in the title graph; `CAMTWK.XOM` only holds in-game cameras), the client picks its own views.

`w4m-models` writes `models/frontend/{title,sky}.glb` (raw units; `Models::load` skips `frontend/`, `FrontBg` loads them) and `models/seagull.glb`. Static parts sharing a texture are merged into one primitive (18 draw calls for the title). `w4m-models --list <bundle>` prints every `XMeshDescriptor` with its clips (`W4M_GROUPS=<name>` adds the group tree with rest positions).

## Weapon tweaks (`Data/Tweak/WEAPTWK.XOM`)

Plain XOM, containers split on `CTNR`, grouped by type in header order. `X{Int,Uint,Float}ResourceDetails`: 3 header bytes, value (4 B), varint name (`Mine.MaxFuse` 5000 ms, `Gravity` -0.00025, `Wind.MaxSpeed`...). `XContainerResourceDetails`: varint ref, varint name (`kWeaponBazooka` -> its `PayloadWeaponPropertiesContainer` / `Melee...` / `Gun...` / `Homing...` / `Flying...` / `JumpingPayload...` / `SentryGun...`). Payload containers hold a 6 x f32 run, read as land crater radius, impulse, worm damage radius, max worm damage, impulse radius, impulse offset (Bazooka 50, 0.29, 82.5, 60, 110, -45; Holy 80, 0.45, 187, 129, 150, -60). Gun: damage after `0.92` (Shotgun 25, Sniper 40); melee: first f32 (Bat 20, Fire Punch 16, Prod 5). Field names are inferred from values only. World units: 20 per metre.

## Unknown / not imported

- Detail objects: the poxel's own (non-uniform) scale is not applied to the object basis; animated details (swinging sign) are static.
- Exact heightmap extent/height scale and water level; possible x mirroring (not verifiable without the game running).
- Voxel bits 8+, the `?` theme lines, texture offsets and directions: we texture triplanar in world space; only the vector lengths are used (median repeat per material).
- Frontend names: `Data/Tweak/SCRIPTS.XOM` `WXFE_LevelDetails` map `Level_FileName` to `FETXT.*` text ids (theme there is always "preselected").

## Frontend / HUD art (`tools/w4m-ui`)

- Loose 32-bit TGAs (uncompressed, bottom-up): `Data/Frontend/Levels` (level previews, 256²), `Data/HUD/Weapons` (weapon icons, 256²), `Data/HUD/Flags`, `Data/Frontend/mechanics` (`LoadBack*` 1920×1080 backgrounds). Written as `assets/ui/{levels,weapons,flags,back}/<lowercase stem>.png`.
- UI `XImage`s (`Bundl00/06/08/10/472`: buttons, popups, borders, team health bars, game logo in `Tournament VsUS`) are found by trying every byte offset as a container (many images carry no `CTNR` tag): the name varint must be a `.tga` / `maya:fileN/-1` string, strides/mips counts 1-16 and the pixel size match the header; a match is skipped whole. Pixels are stored bottom row first (GL); format 9/10/11 = DXT1/3/5. Output `assets/ui/fe/<name>.png`.
- In-match HUD: `Bundl09` XImages (radar back/marks/NSEW/objects, timer/wind/weapon circles, power bar, pitch arc + arrow, `HUD Font` digits `012345.` / `6789∞m`, also the particle sprites) -> `assets/ui/hud/<name>.png`. Already teal: drawn untinted.
- Sky/water: one bundle per theme and time of day (`Bundl93`-`97`, `108`-`113` = DAY `01`; `98`-`107`, `114`-`125` = EVENING `02` / NIGHT `03`). `<L>_Sky0n.tga` is a 256x1 ramp (dark zenith at u = 0 to cream horizon), `<L>_SideSky0n.tga` a second 256x1 ramp (unused), `<L>_Water0na/b/c.tga` = diffuse, normal map, sphere-mapped environment (as sampled by `CG/water.cg`). Theme letters: A Arabian, B Building, C Camelot, P Prehistoric, W Wild West, R Arctic, E England, H Horror, L Lunar, T Pirate, O War. The DAY set goes to `assets/ui/sky/`.
- `Bundl03` `FE.Font`: objects are stored in type-table order (refs = 1-based object index; the first objects carry no `CTNR` tag). 31 `XMultiTexFontPage`: 3 header bytes, varint ref to an `XOglTextureMap` (its varint image ref at byte 23 -> a 512² RGBA8 `XImage`, white glyphs), varint n + n u16 codepoints, then four arrays each prefixed by a varint count: rect origin (2 × f32, UV, top-down after the usual row flip), rect size (2 × f32, UV), rect centre relative to pen/baseline (2 × f32, em, y up), advance (f32, em). 50 atlas px per em; rects include 12 px of padding and overlap their neighbours by 8. `½¼»º²¹«µ³` hold pad button icons, CJK/Cyrillic fill most pages. `tools/w4m-ui` repacks the Latin glyphs into `assets/ui/font/w4m.fnt` + `w4m.png` (BMFont, loaded by raylib `LoadFont`).
- Frontend menus are 3D scenes in W4M, but every element is a flat textured mesh, so their textures are the art: `WX.Mesh.SinglePlayer` (TV robot + `maya:file7` screen noise), `NetOptions` (globe, satellite), `CustomiseOptions` (paint set), `Options` (question mark) in `Bundl06/10`; the torn white ticker strip is `Bundl10` `maya:file16/-1` (256x128, tiles horizontally); `Bundl474` `Nav Normal/Highlight.tga` = 2x2 nav icons (grenade, tick, curved back arrow, cross). `fe/bluedivide` is the curved blue submenu panel, `fe/icon_splat` the black brush behind the active item. Maya names repeat, so `tools/w4m-ui` picks them by (bundle, name, n-th image of that name) -> `assets/ui/fe2/`.
- Strings: `Data/Language/PC/<Lang>FE.xom` + `<Lang>.xom` (`XStringResourceDetails`, see Missions below). Menu keys: `FETXT.LocalGame`, `FETXT.HTPHeader3` (network game), `FETXT.MyWorms`, `FETXT.Help&Options`, `Lang.Quit`, `FETXT.QuickGame` / `Versus` / `Story` / `Challenges`, `FETXT.LocalNetwork` / `Online`, `FETXT.ConfirmQuit`, `FETXT.Yes/No`, submenu titles `FETXTH.LOCALGAME` / `NetworkPlay` / `MYWORMS` / `HELP&OPTIONS`, ticker `WXFE.TickerTapeDefault`. Exported as `assets/lang/en.txt` / `fr.txt` (`key<TAB>value`, `\n` = newline, U+00A0 -> space).

## Missions (`Data/Tweak/SCRIPTS.XOM`, `Data/<LEVEL>.XOM`, `Data/scripts/*.lub`)

Imported by `tools/w4m-maps` (`src/mission.rs`, `src/lua.rs`) into `assets/missions/*.json` (`docs/missions.md`).

- `SCRIPTS.XOM` `WXFE_LevelDetails`: varint strings name key, brief key, preview `.tga`, level file, script(s), ?; u32 index, u32 type (4 story: index 0-4 Construction, 10-14 Camelot, 20-24 Wild West, 30-34 Arabian, 40-44 Jurassic; 6 tutorial; 8 challenge; 9 deathmatch; 0/3 multiplayer), varint unlock key, u32 5, u32 0, u32 par time (s), u32 0. Text: `Language/PC/English.xom` (+ `EngFE`, `EngLS`) `XStringResourceDetails` = 3 header bytes, varint value, varint key; UTF-8, `/*NL*/` = newline.
- Level databank: `XContainerResourceDetails` (varint ref, varint name) names the containers the script uses. `WormDataContainer`: varint name, then fixed fields (relative to the end of the name: u32 `TeamIndex` @139, u32 `Energy` @161), varint `SfxBankName` @177, varint `Spawn` (marker name), u8 `IsParachuteSpawn`, u8 `IsAllowedToTakeTurn` (0: accuracy dummies, actors). `TeamDataContainer`: varint name ("" = the player's team), bytes, flag / hat / speech strings. `WeaponInventory`: 3 header bytes then one u8 count per slot (0xff = infinite): Bazooka, Grenade, Cluster, Airstrike, Dynamite, Holy Hand Grenade, Banana, Landmine, Shotgun, Bat, Prod, Fire Punch, Homing, Flood, Sheep, Gas, Old Woman, Donkey, Super Sheep, Girder, Bridge Kit, Ninja Rope, Parachute, Teleport, Jetpack, Skip Go, Surrender, Change Worm, Redbull, 29-35 unknown, Poison Arrow, Sentry Gun, Sniper Rifle, 39+ unknown (identified from the challenge inventories). `CrateDataContainer`: varint type (`weapon`, `utility`, `health`, `target`, `custom`), varint contents (`kWeaponBazooka`...), u32 count, u32 index, f32 lifetime, 8 bytes, varint `Spawn` marker. `TriggerDataContainer`: varint `Spawn`, f32 radius (W4M units, 20 per metre), u32 index...
- Markers: the non-`visible` `DetailEntityStore`s of the map `.xan`, by library: `CheesyGrinWorm` (worm spawn), `Target` / `Crate.Target`, `Crate` / `Crate.Weapon`, `Mine` / `Landmine`, `Oildrum`, `Collision Sphere` (trigger), `Telepad`; cameras, look-at points, lights, emitters and sounds are skipped. Exported as the map JSON `markers`.
- Scripts: Lua 5.0 bytecode (`\x1bLuaP`, 4-byte ints/size_t/instructions, 4-byte float numbers, op 6 / A 8 / B 9 / C 9 bits). Engine callbacks (`Initialise`, `Crate_Collected`, `Crate_Destroyed`, `Trigger_Collected`, `Trigger_Destroyed`, `Worm_Died`, `Worm_Damaged_Current`, `TurnEnded`, `Timer_GameTimedOut`...) call `lib_SetupTeam(i, team)`, `lib_SetupWorm(i, worm)`, `lib_SetupTeamInventory(i, inv)`, `lib_SpawnCrate(crate)`, `lib_SpawnTrigger(trigger)`, `SetData("TurnTime", ms)`... (`stdlib.lub`, `lib_help.lub`); a level ends with `SendMessage("GameLogic.Mission.Success")` / `.Challenge.Success` / `.Failure`. `AIParams.CPU<n>` = AI level.

## Game tweaks and schemes (`Data/Tweak/TWEAK.XOM`, `LOCAL.XOM`)

Same resource-details layout as WEAPTWK. `TWEAK.XOM` worm movement (world units, 25 = worm height): `WXWorm.JumpDistance` 80 / `JumpHeight` 50, `BackflipDistance` 40 / `BackflipHeight` 80, `VerticalJumpHeight` 70, `Worm.StepUpHeight` 5, `WXWorm.SlideAngle_Default` 60 (deg) / `SlideFriction_Default` 0.95, `Worm.WalkOffCliffVelMulti` 0.7, `Worm.BounceMultiplier` 0.6; also `Water.RiseAmount` 25, `OilDrum.DamageMagnitude` 55, `Worm.Poison.Default` 10, `Low.Gravity.OnValue` 0.5, `MaxRandomCrates` 15.

`LOCAL.XOM` holds the 19 built-in `SchemeData` (`FE.Scheme.Standard`, `Beginner`, `Pro`, `Bng`, `Shopping`, `Allaction`, `Strategy`, `Family`, `FETXT.Scheme.MegaPower`/`HolyGrail`/`Mystery`/`Darksider`/`Rootnshoot`/`Thekitchensink`, quick/ranked/net ones). After `CTNR`+3: varint name, u8, varint lock name, 58 varint refs to `WeaponSettingsData` (3 × i32: ammo, crate odds, delay in turns; slot order not decoded), varint `AssistedShot.*` string, 8 bytes (first = artillery mode, set in Root'n'Shoot only), then i32s. Read from value patterns (Standard): `[2] worm energy 100, [3] round time 1200000 ms, [4] turn time 45000 ms, [6] crate chance 40 %, [7..10] mystery/weapon/health/utility crate odds 0/30/20/30, [11] health crate 25, [14] wind 1..3, [16] hot seat 5000 ms, [21] mine fuse 3 s (-1 random), [22] retreat 3000 ms`; `[13]` is 0 in Beginner/Family/Strategy (taken as fall damage). Other fields unknown. Lua (`scripts/stdvs.lub`) reads `GM.SchemeData` fields by name: `TurnTime RoundTime DefaultRetreatTime HotSeatTime HealthInCrates FallDamage WindMaxStrength MineFuse MineFactoryOn TelepadsOn Stockpiling WormSelect TeleportIn SuddenDeath WaterSpeed ArtilleryMode LandTime`.

# Worms 4 Mayhem landscape formats (reverse-engineered)

Sources: AlexBond2/Worms3DTools (XomView `XomLib.pas`/`XomCntrLib.pas`), w4tweaks wiki (poxel/height-map pages), plus byte inspection of the Steam *Worms Ultimate Mayhem* data. Implemented in `tools/w4m-maps` (Rust). All game data stays local; nothing extracted is committed.

## XOM container (`.xan`, `.xom`)

Little endian. Header 64 B: `MOIK`, …, `u32 @24` type count, `@28` container count, `@32` root container index (1-based). Then one 64 B record per type: `TYPE`, `?`, `u32 @8` instance count, `?`, GUID (16), name (32, NUL padded). Then `GUID`/`SCHM` blocks and `STRS`: `u32 count, u32 bytes, count × u32 offsets, string blob`. Then the containers, grouped by type in header order, each starting with `CTNR` + 3 bytes (splitting on `CTNR` works for every map file; bundles need per-type parsing). Integers/indices inside containers are 7-bit varints (`0x80` = more); strings are varint indices into `STRS`; references are 1-based container indices.

## Map files (`Data/Maps/<Map>.*`)

| file | content |
|---|---|
| `.xan` | the landscape: a tree of `LandFrameStore` ("poxels") + `DetailEntityStore` (objects). 222 files, all parse. |
| `.hmp` | 50000 B: 100×100 `f32` heights in 0..1 (row = z, column = x), then 100×100 `u8` second-texture mask. All zero for Worms 3D-era (`-w3d`) maps. |
| `DAY/EVENING/NIGHT.csh` | cached shadow/lighting data per time of day (engine cache, ignored). |
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

## Themes and textures

`Data/Themes/Theme<X>/Theme<X>.txt`: 64 materials × 7 lines: top texture, side texture, ?, bump/second texture (`NULL`), surface sound (`Rock01`, `grass01blend`…), ?, blank. Textures (`C01`…) are `XImage`s in `Data/Bundles/Bundl13..21.xom` (one bundle per theme; Worms 3D banks in `Bundl15x`): name (path string `ThemeCamelot\C01.tga`), w, h (u16), mips (u8+pad), flags (u16), strides (count + u32s), mip offsets (count + u32s), format (u32: 0 = R8G8B8, 1/2 = 8888, 9/10/11 = DXT), varint size, pixels top mip first. The importer writes the top mip of format 0/1/2 images (1660 textures) as `maps/tex/<stem>.qoi` (only those used by an imported map) and keeps their average as the fallback palette colour.

`Data/Themes/* Detail List.txt` names the theme's detail objects (lamps, statues, plants); `DetailEntityStore` = name + library + transform (not imported).

## Unknown / not imported

- Detail objects (trees as meshes, statues, barrels), mines/barrels/crates from mission data, worm start positions (mission `WormDataContainer` positions are all zero: scripts place them).
- Exact heightmap extent/height scale and water level; possible x mirroring (not verifiable without the game running).
- Voxel bits 8+, the `?` theme lines, texture offsets and directions: we texture triplanar in world space; only the vector lengths are used (median repeat per material).
- Frontend names: `Data/Tweak/SCRIPTS.XOM` `WXFE_LevelDetails` map `Level_FileName` to `FETXT.*` text ids (theme there is always "preselected").

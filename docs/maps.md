# Map format

A map is `maps/<name>.json`. Lookup order: `sdmc:/switch/worms4nx/assets/maps/` (desktop `./assets/maps/`, for user/imported maps), then `romfs:/maps/`. Missing or invalid file => procedural island. Same file + match seed => identical terrain on every client.

World: 80 x 64 x 80 m (x, y up, z), voxels of 0.25 m (320 x 256 x 320 grid, int8 density + u8 material), water at y = 3 (below = drown). All units are metres.

```json
{
  "name": "Camelot Keep",
  "theme": "camelot",
  "base": {"type": "island", "base": 4, "height": 8, "radius": 0.9, "roughness": 2, "seed": 0},
  "shapes": [
    {"type": "cylinder", "pos": [30, 17, 30], "radius": 2.2, "height": 10},
    {"type": "box", "pos": [40, 14, 30], "size": [3.5, 4, 3], "subtract": true}
  ],
  "finish": [74, 13, 40]
}
```

| key | meaning |
|---|---|
| `name` | display name (informational for now; the menu shows the file name). |
| `title` | W4M `Frontend_Name` text key of the level file (imported, data: `SCRIPTS.XOM` `WXFE_LevelDetails`; the versus entry wins; demo types 12/13 and outtake type 16 are skipped, they reuse another level's name and image). The local match menu shows `tr(title)`, else a name made from the file name (`Ui::mapTitle`), and lists the maps procedural first, then by that name (ours). |
| `preview` | menu picture `assets/ui/levels/<preview>.png`: the same entry's `Frontend_Image` (data), else `map_<file>`, a 256 px top-down render w4m-maps makes of the voxel land (ours: top palette colour, hillshade, flat sea). Missing => theme picture, else `nolevel`. |
| `theme` | `jurassic`, `camelot`, `arabian`, `wildwest`, `construction` (+ `arctic`, `england`, `horror`, `lunar`, `pirate`, `war` from imports): terrain palette (grass/top, rock/side, beach) and sky; exposed as `Terrain::theme` (music `music/<theme>.ogg`). Unknown => default palette. |
| `base.type` | `island` (default): noisy hill, `flat`: plane at `base`, `none`: empty (only shapes). |
| `base.base` | ground level at the island rim / flat level (default 6). |
| `base.height` | extra height at the island centre (default 10). |
| `base.radius` | island radius as a fraction of half the map width (default 0.8). |
| `base.roughness` | noise amplitude in m (default 4, 0 = smooth). |
| `base.seed` | added to the match seed for the noise. |
| `voxels` | `<file>.vox` next to the JSON: imported geometry, replaces `base`. `"W4V2"`, u16 NX NY NZ (must match the engine grid), u8 D; then (material, run 1..255) byte pairs over the grid in `(z*NY + y)*NX + x` order (material 0 = air, n = `palette[n-1]`); then density codes in the same order: byte h < 128 skips h voxels left at +D (solid) / -D (air), h >= 128 is followed by h - 127 int8 densities (metres * 254, > 0 = solid). |
| `light` | `{"dir": [x,y,z], "ambient": [r,g,b], "diffuse": [r,g,b], "specular": [r,g,b]}`: sun direction (towards the sun) and colours (0..1), W4M's per theme/time land light (imported; `time` = `day`/`evening`/`night` picks the sky and water). `"water"`: the 14 CG/water.cg inputs of the same WaterPlaneTweaks (docs/w4m/render.md "Level sky"). Missing => Camelot-day defaults. |
| `palette` | `[[r,g,b, r,g,b], …]`: top (flat) and side colour per voxel material (fallback when there are no textures). |
| `textures` | `[[top, side, topRepeat, sideRepeat, roof, fringe], …]` aligned with `palette`: texture files (QOI/PNG, relative to the maps dir, `null` = none) and metres per texture repeat; roof (downward faces, floor repeat) and fringe (RGBA grass atlas) are W4M material fields 3 and 4. Rendered with a triplanar shader (top texture on up-facing surfaces, roof on downward ones). |
| `scale` | metres per W4M land voxel (the import scale k): sizes the fringe cards. Default 1. |
| `thin` | `<map>.thin` (`"W4T1"`, u32 count, then per cell u16 x y z of its voxel, u8 material, 8 corners as 3 f32 in m): sub-voxel W4M cells (ropes, twigs, rails), drawn flat-shaded with the land material while their voxel is solid; a voxel solid only for thin cells gets no surface-nets faces. Render only: the sim never reads it. |
| `cells` | `<map>.cells` (imported maps): the exact land per 0.25 m cell the surface crosses, read by the sim's land queries (`docs/sim.md` "Exact land", format `docs/w4m/formats.md` §22). Without it the sim reads the `.vox` field. |
| `shapes` | applied in order: union, or carve with `"subtract": true` (tunnels, caves, moats). |
| `finish` | rope race goal (exact point, not dropped). |
| `markers` | W4M script markers (imported): `[{"name": "Targ1", "type": "target", "pos": [x, y, z]}, …]`, type `worm`, `target`, `crate`, `mine`, `oildrum`, `trigger`, `telepad`. Missions refer to them by name (`docs/missions.md`). |
| `emitters` | W4M `EMITTER_*` details (imported): `[{"fx": "WXP_OilLamp", "pos": [x, y, z]}, …]`, the PARTTWK effect started at that point at match start (`Fx::level`, render only); stopped when a blast reaches it (`Terrain::carve`: distance < radius, W4M 0x5cc960). |
| `origin` | W4M world origin in map metres (the import offset, our water level): the sky scene, the water plane and the weather emitter sit there. |
| `rain_prob` | the level script's `SetData("Particle.Rain.Prob", n)` when it has one (else the theme / time odds, docs/w4m/render.md "Level weather"). |
| `objects` | decor without collision (imported W4M detail objects): `[{"model": "camelot18", "pos": [x, y, z], "basis": [9 floats]}, …]`. `model` = `models/decor/<model>.glb` (missing => skipped), `pos` = model origin, `basis` = row-major 3x3 rotation * scale. Removed when a blast reaches it (distance < radius, W4M 0x5cc960) or the ground 0.3 m under its base (`Terrain::carve`, deterministic); drawn by `Terrain::drawObjects`, frustum-culled only (W4M has no detail draw distance). `<model>.mat` gives each glb material its W4M states (blend, alpha test, z write, cull, lighting, emissive) and texture offset track; `<model>_anim.glb` (w4m-models), when present, replaces a model whose `Go` clip moves parts. |

Shapes: `pos` is the centre of the shape's bounding box, `yaw` (degrees) rotates around the vertical axis.

| type | params |
|---|---|
| `box` | `size` [w, h, d] |
| `ramp` | `size` [w, h, d]: wedge rising towards local +x |
| `pyramid` | `size` [w, h, d]: apex on top |
| `sphere` | `radius` |
| `cylinder` | `radius`, `height` (vertical) |
| `cone` | `radius` (base), `height`, `top` (top radius, default 0; > 0 = frustum/mesa, > `radius` = upside down) |
| `torus` | `radius` (ring), `thickness` (tube): ring standing upright in local XY plane; half-buried = arch |

Shape cost is proportional to its bounding box volume; keep total load under ~0.1 s desktop (Switch is ~5x slower).

Imported Worms 4 Mayhem maps: `tools/w4m-maps <W4M dir> client/assets/maps` (see `docs/w4m-formats.md`). They are written to `assets/maps` (local only, never committed).

## Rendering (ours, after docs/w4m/render.md "Level sky, water and land colour")

- Map emitters and weather (`Fx::level`, `Fx::levelSync`, `Fx::weather`; docs/w4m/render.md §3 "Particle details", "Level weather"): `assets/fx/parttwk.json` (tools/w4m-re/parttwk.py) gives every effect named by a map plus the weather's; each `emitters` entry starts its emitters through the shared PARTTWK path (`effect()` / `tickEmitters()`, the W4M particle maths of 0x5b6f40 / 0x5b7660 / 0x5b7450) at m per unit = `scale` / 20 [ours: our maps are the W4M world scaled by `scale`, so the level's effects scale with it; weapon effects stay at 20 units per m]; sprite sets are `fx/<set>.png` with their blend in `fx/sprites.txt`, MeshSet meshes `models/fx/<set>.glb` (+ `.blend`: blend factors, texture offset clip) drawn with their clip; loop EmitterSoundFX play per emitter (`Audio::emitter`, FMOD max playbacks steal oldest), kRain / kSnow effects (WXP_RainFall on some maps, the weather's WXP_RainfallBG) are the camera box of drops with splashes and RainLoop. Not reproduced [ours]: particle land collision (ParticleLandCollideType), the ribbon retract at an emitter's end, the rain streak tilt in fly / jetpack views, the Flood weapon's rain.
- Sky: `Fx::theme` loads `models/sky/<letter>_sky0<n>.glb` (the W4M `<THEME>.<TIME>Sky` scene, decoded on the loading thread by `Models::decode`) and its `.blend` (per mesh: `src dst` BlendFactors, `-1 -1` = opaque, then the clip's change of rotate Y in rad, texture offset U and V, last key s; then `clip <length>` and `sun x y z`). `Fx::drawSky` draws it camera-centred (its height drop measured from `origin` at `scale` / 20 m per W4M unit) at 0.04 m per unit (inside the 500 m far plane) [ours: W4M leaves it at the world origin; at 0.04 m per unit it must follow the camera to stay inside the far plane]: opaque parts with depth, then the blended ones in file order with their W4M factors, then clears the depth buffer. Each mesh turns about y and scrolls its uv by its share of the looped clip (the exporter keeps parts with different channels apart). Without the file, the old ramp dome. No star twinkle (dead code in W4M).
- Lens flare: `Fx::drawFlare`, last in the 3D scene, is LensFlareGraphicEntity with the `.blend` sun direction (camera-centred, so the direction is the locator's own) and the Sky.Flare1-3 tables in code; the land test is `Terrain::raycast`, the object test a line against worm and object spheres of `Game::R` [ours: W4M's WX_Collider shapes are not modelled]. The billboards sit at 0.04 x the W4M depth.
- Land colour: `Terrain::vertexColour` is GLG_PC's colour per land mesh vertex: the theme's `<l>_sky0<n>.png` / `<l>_sidesky0<n>.png` sampled every 8 pixels, b0 from N.L with Sun.LowLightVector capped by a voxel ray (from 0.5 m, 50 m max, steps from 0.25 m growing 8 %, 20 units per m), b3 from the normal x; heightmap materials (65, 66) stay white. The land shader multiplies its whole output by it. Replaces our former baked ambient occlusion and sun visibility. LandFrame tints and chunk point lights are not imported [ours].
- Shadow map (`Lit::shadowPass`, before the PiP and the main view; docs/w4m/render.md "Level sky, water and land colour"): a 1024² depth texture (linear, compare mode LEQUAL, as W4M) seen by an orthographic camera along -`Lit::LOW_LIGHT` (Sun.LightVector, the same vector as the land colour's), fitted to the land box x 1.2, near 0. The land shader (`Terrain` FS) is Landscape.cg's PC path: 9 hardware-compared taps one texel apart (Switch: the X_XBOX branch's 5, centre and its 4 neighbours, docs/tests.md "Render budget"), off the map = lit, lighting only above 0.01, diffuse and specular x fs, rim x (0.5 + fs / 2), all on top of the vertex colour's sun ray. Casters: land, still and clip-played decor, worms, graves, shots, objects. Ours, same image as redrawing all every frame: the land and still decor go into a second depth map only when chunks swap in (`Terrain::meshVer`) or the map changes; each frame copies the rectangle of last frame's casters back from it (glBlitFramebuffer) and draws this frame's casters on top. `Terrain::bounds` is the solid voxels' box at the first remesh [assumed: W4M's land box at +0x9f8 is not traced further]. The land is drawn with front faces culled [assumed: W4M uses no depth bias and its lighting-pass cull state is not traced; culling front faces keeps lit faces free of self-shadow acne]. Not cast [ours]: particles (W4M's Particle1-5 bins cast), the land fringe (no z write, so none in a depth-only pass [assumed]), crate parachutes, bomber and UFO (outside the land box). On GLES the land shader is GLSL ES 3.00 (the Switch's Mesa has no GL_EXT_shadow_samplers in GLSL 100); without sampler2DShadow or a complete depth FBO the shadow map is off (`Lit::shadows`). Cost: docs/tests.md "Render budget".
- Water: `Fx::drawWater` is water.cg `WaterFragmentMain` with the map's `light.water`, the theme's `<l>_water0<n>a` / `c` and `f_water01b` (FE.DAYWaterNormal, every theme); plane ±6000 units at `scale` / 20 m per unit, centred on `origin` (W4M 0x48b51b: the world origin), uv 0..1 x TextureScale over the whole quad, time in seconds looped at 200 s (every pan speed repeats there). No underwater bowl: W4M never draws `<THEME>.<TIME>WaterBlend`.
- Land fringe (`Terrain::buildChunk`, after GLG_FringeBuilder, docs/w4m/render.md "Level scenery"): on every solid voxel of a fringe material with air above, each side whose neighbour is open for one whole W4M voxel (`ceil(scale / 0.25)` of ours) hangs a card from its two land mesh top vertices along (0.7 out, -0.7 down) x 0.4 x `scale`, ends splayed 0.2 at open corners; one atlas strip per W4M voxel of edge (cards split at W4M voxel boundaries, strip = hash of the voxel and the edge line) [ours: our 0.25 m grid is finer than W4M's voxels, so W4M's per-voxel counter cannot be reproduced]. Colour = the land vertex colour, unlit; drawn by `Terrain::drawFringe` after the water, alpha blended, alpha > 0, no z write, no culling.
- Land mesh (`Terrain::chunkGeometry`, on the meshing thread) [ours]: one vertex per cell whose corners change sign in `d`, one quad per sign-changing grid edge.
  - In a cell listed by the exact land (docs/sim.md "Exact land"): dual contouring. Each sign-changing edge gets its exact crossing (`SharpLand::first` from the air end, `grid` on) and the normal there (the deciding primitive's; where the cell lists the heightmap, the density's gradient over ±1 mm instead, since the heightmap's own normal is a slope over ±1 cell and a missing column is a vertical step). The vertex minimises the squared distances to those tangent planes about their mean (ridge 0.05, clamped to the cell), so it sits on the land's edges and corners; its normal is the crossings' mean normal. A crossing is computed once per chunk, in the first listed cell around its edge in a fixed order, so neighbouring chunks agree.
  - Elsewhere (unlisted cells; maps without `.cells`): surface nets, the vertex at the mean of the edges' interpolated crossings, the normal = the gradient of `field` over two voxels.
  - Measured (scratch lab, 8 maps DM1, 2, 3, 5, 7, 9, Clean-w3d, StormTheCastle; vertex to exact land = nearest sign change over 200 directions; gap = exact top vs mesh top along 47 000 vertical lines; a lip = a line whose top drops more than 0.25 m within 0.1 m; blasts = 40 carves of 1-3 m per map), surface nets -> dual contouring: vertex to exact land mean 17.1 -> 3.7 mm, p99 99 -> 48 mm, > 5 cm 13.9 % -> 1.0 %; vertical gap mean 13.2 -> 2.9 mm, > 5 cm 7.8 % -> 0.9 %; at lips mean 129 -> 16 mm, > 5 cm 72 % -> 7 %; carve walls (away from their rim) vertex distance mean 7.8 -> 6.1 mm, max 24 -> 23 mm, normal against the sphere's mean 0.94° -> 0.09° (no crease: the sphere is one smooth primitive). Same triangles (3.03 M). CPU, desktop, best of 3: full map 0.80 -> 1.32 ms per non-empty chunk; per blast 3.0 -> 4.6 ms (3.5 chunks), 1.5-1.6x.
  - Left: one vertex per cell cannot hold two surfaces, so cells crossed by the land's sub-cell cracks (air slits between W4M cells) keep errors up to 0.17 m; they make most of the remaining lip gaps.
  - Scratch per meshing thread: 3.8 MB of edge crossings (34³ x 3 edges).
- Decor: opaque parts first, then blended ones (`.mat` factors), emissive added to the light, unlit parts plain texture; `Go` clips start at a hash-random phase per object (W4M: rand16), `GoSync` at 0.
- No fog on land or water (W4M CG has none). Clear colour = the ramp's horizon colour, hidden by the sky scene.
- Title screen: FE.Water parameters on the FE.DAYWater set (`sky/f_water01a/b/c.png`, from w4m-ui), Water.Level -100 units and the 12000-unit quad at the title's 0.25 m per unit.

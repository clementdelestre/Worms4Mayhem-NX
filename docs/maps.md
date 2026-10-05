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
| `textures` | `[[top, side, topRepeat, sideRepeat], …]` aligned with `palette`: texture files (QOI/PNG, relative to the maps dir, `null` = none) and metres per texture repeat. Rendered with a triplanar shader (top texture on up-facing surfaces). |
| `shapes` | applied in order: union, or carve with `"subtract": true` (tunnels, caves, moats). |
| `finish` | rope race goal (exact point, not dropped). |
| `markers` | W4M script markers (imported): `[{"name": "Targ1", "type": "target", "pos": [x, y, z]}, …]`, type `worm`, `target`, `crate`, `mine`, `oildrum`, `trigger`, `telepad`. Missions refer to them by name (`docs/missions.md`). |
| `objects` | decor without collision (imported W4M detail objects): `[{"model": "camelot18", "pos": [x, y, z], "basis": [9 floats]}, …]`. `model` = `models/decor/<model>.glb` (missing => skipped), `pos` = model origin, `basis` = row-major 3x3 rotation * scale. Removed when an explosion reaches it or the ground 0.3 m under its base (`Terrain::carve`, deterministic); drawn by `Terrain::drawObjects`, culled beyond ~35 m + 40 x size. |

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

- Sky: `Fx::theme` loads `models/sky/<letter>_sky0<n>.glb` (the W4M `<THEME>.<TIME>Sky` scene, decoded on the loading thread by `Models::decode`) and its `.blend` (per mesh: `src dst` BlendFactors, `-1 -1` = opaque, then the clip's change of rotate Y in rad, texture offset U and V, last key s; then `clip <length>` and `sun x y z`). `Fx::drawSky` draws it camera-centred at 0.04 m per unit (inside the 500 m far plane) [ours: W4M leaves it at the world origin; at 0.04 m per unit it must follow the camera to stay inside the far plane]: opaque parts with depth, then the blended ones in file order with their W4M factors, then clears the depth buffer. Each mesh turns about y and scrolls its uv by its share of the looped clip (the exporter keeps parts with different channels apart). Without the file, the old ramp dome. No star twinkle (dead code in W4M).
- Lens flare: `Fx::drawFlare`, last in the 3D scene, is LensFlareGraphicEntity with the `.blend` sun direction (camera-centred, so the direction is the locator's own) and the Sky.Flare1-3 tables in code; the land test is `Terrain::raycast`, the object test a line against worm and object spheres of `Game::R` [ours: W4M's WX_Collider shapes are not modelled]. The billboards sit at 0.04 x the W4M depth.
- Land colour: `Terrain::vertexColour` is GLG_PC's colour per surface-nets vertex: the theme's `<l>_sky0<n>.png` / `<l>_sidesky0<n>.png` sampled every 8 pixels, b0 from N.L with Sun.LowLightVector capped by a voxel ray (from 0.5 m, 50 m max, steps from 0.25 m growing 8 %, 20 units per m), b3 from the normal x; heightmap materials (65, 66) stay white. The land shader multiplies its whole output by it. Replaces our former baked ambient occlusion and sun visibility. LandFrame tints and chunk point lights are not imported [ours].
- Shadow map: not drawn [ours]. W4M's 1024² depth map (one extra depth pass of land and objects, 9 PCF taps per land pixel) is estimated in docs/tests.md "Render budget".
- Water: `Fx::drawWater` is water.cg `WaterFragmentMain` with the map's `light.water`, the theme's `<l>_water0<n>a` / `c` and `f_water01b` (FE.DAYWaterNormal, every theme); plane ±600 m (12000 units at 20 per m), uv 0..1 x TextureScale over the whole quad (so the import scale k never enters the uv), time in seconds looped at 200 s (every pan speed repeats there). No underwater bowl: W4M never draws `<THEME>.<TIME>WaterBlend`.
- No fog on land or water (W4M CG has none). Clear colour = the ramp's horizon colour, hidden by the sky scene.
- Title screen: FE.Water parameters on the FE.DAYWater set (`sky/f_water01a/b/c.png`, from w4m-ui), Water.Level -100 units and the 12000-unit quad at the title's 0.25 m per unit.

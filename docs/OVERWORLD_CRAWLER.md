# PC-98 Overworld Planet Crawler — Technical Summary

> **Status note:** This document describes the current implementation as of `b366475` (`main`, merge of PR #168) for technical reference only. Per `AGENTS.md`, the game is in active Beta—none of the mechanics below are permanent requirements, and this file never overrides the repository owner's instructions.

This document explains how the first-person PC-98-style planet exploration mode ("Dispatch") works across simulation state, Bridge UI integration, deterministic surface and underground generation, and the `480x288` software pixel rasterizer.

---

## 1. High-Level Flow & Design Constraints

1. **Arrival & Landing (`cpp/core/Navigation.cpp`)**
   - When the ship arrives at a body, `Simulation::ArriveAt` sets `map_.inSpace = false` and `map_.here = body->id`. Departing via `Simulation::StartTravel` sets `map_.inSpace = true`.
   - Landing state is derived by `Simulation::LandedOnPlanet()` (`!map_.inSpace && here != nullptr && here->type == "planet"`).
   - `state_.docked` is set by `ArriveAt` only when `HerePort()` matches the arrival body. `SaveSystem.cpp` (`Simulation::ValidSave`) rejects saves where `state_.docked == true` without a matching port in `catalog.FindPort(here)`.

2. **Bridge Integration (`cpp/ui/RoomsScreen.cpp`, `cpp/ui/Widgets.cpp`, `cpp/ui/Draw.cpp`)**
   - `BuildRoomScreen(sim, "bridge")` inserts an action row (`id = "dispatch"`, `label = "Dispatch"`, `cost = "free"`) between `"fuel-split"` and `"warp"` whenever `sim.LandedOnPlanet()` is true.
   - `Widgets.cpp` sets `hideCost = true`, `charged = false`, and `actionCost = false` on `"dispatch"` so it renders without a click/charge badge and never enters the command charge system.
   - In `PaintPanelsScreen` (`cpp/ui/Draw.cpp`), `"Dispatch"` occupies the center Command Bay socket (`midBtnW = 82.0f * scale`, `x = stageX + 439.0f * scale` when present, auto-fitted label, terracotta when closed / amber when open).
   - Clicking `"dispatch"` in `ShellView::Click` sets `bridgeDispatch_ = true` directly in the view layer, resets `contentScroll_`, clears floating panels, and calls `EnsurePlanetSurface(sim)` without advancing the simulation clock (`PressRoomAction(sim, "dispatch")` simply returns `sim.LandedOnPlanet()`).
   - While dispatch is active on the Bridge tab, `ShellView::BlockerActive` is true and `ShellView::DrawBlocker` (called from `cpp/app/main.cpp` after `rlImGuiEnd()`) draws `assets/art/blocker.png` scaled so its transparent cutout (`x = 319..1767`, `y = 131..672` in the 1920x1078 source) aligns with `bridgeBlockerHole_`. `Move`, `Drag`, `Scroll`, and `Click` return early for input outside that hole. Clicking the landed ship on the surface (hit kind `6`) sets `bridgeDispatch_ = false` and dismisses the blocker.

3. **Persistence (`Simulation::CollectPlanetOre` in `cpp/core/Navigation.cpp`)**
   - Walking, turning, and mine traversal live in UI view state (`ShellView::planetSurface_`) and cost zero clicks.
   - Collecting an ore package writes a persistent mark into `state_.mapMarks`:
     - Key: `"planet-ore:" + planetId + ":" + std::to_string(oreIndex)`
     - If unmarked and cargo space allows (`CargoUsed() + CargoReserved() + 1.0 <= cargo_.capacity`), appends a `1.0 m3` `GoodsLot` named `"Planetary Ore (" + body->name + ")"` (`kind = "Bulk metal"`, value `28..45` derived from `MapEventHash(markKey)`), logs to the feed, and sets `mapMarks[markKey] = true`.
     - Ore indices `0..oreCount-1` are shared across the surface and underground layers (see `oreLayerMask` in section 2); the same key space serves both layers with no save-format change.

---

## 2. Surface State & Procedural Generation

Defined in `cpp/ui/Draw.h` (`ShellView::PlanetSurfaceState`), built by `ShellView::LoadPlanetLayer(const Simulation& sim, std::uint8_t targetInMine, bool placeOres)` and `ShellView::EnsurePlanetSurface(const Simulation& sim)` in `cpp/ui/Draw.cpp`:

- **Grid dimensions:** `kSize = 16` (`16x16` tiles), `kSizeShift = 4`, `kViewSize = 9`, `kMaxOre = 5`, `kTexW = 480`, `kTexH = 288`.
- **Packed cells:** `cells[256]` stores one byte per tile: bits `0..1` = tile, bits `2..4` = deterministic visual variant (`0..7`), addressed as `(y << kSizeShift) | x` via `Tile()`, `Variant()`, `SetTile()`, and `SetVariant()`.
- **Tile values (`Tile()`):**
  - `0` — Open walkable ground
  - `1` — Impassable obstacle, wall, or outer map border
  - `2` — Ship landing pad at `(8, 14)` (surface layer only)
  - `3` — Carved trail (surface) or mine rail path (underground layer)
- **Player state:** `px`, `py`, `dir` (`0=N, 1=E, 2=S, 3=W`), spawn at `(8, 13)` facing North on the surface. `inMine` selects the active layer (`0` = surface, `1` = underground mine). `surfPx`, `surfPy`, and `surfDir` save the surface position and heading while underground. `portalX`, `portalY`, `portalOutDir`, and `mineInDir` describe the active layer's portal cell and its outward and entry headings.
- **Deterministic seeding:** `LoadPlanetLayer` computes a 32-bit FNV-1a hash of `here->id` (`2166136261u` basis, `16777619u` prime) XORed with `sim.World().chartSettings.seed * 2654435761u`. Each layer steps its own Xorshift32 generator (`nextRand()`) from that seed; the underground layer XORs the seed with `0x85EBCA6Bu` first. Re-opening Dispatch or re-visiting the same planet regenerates the exact same layouts.
- **Surface generation steps (`targetInMine == 0`):**
  1. Outer perimeter (`x == 0 || x == 15 || y == 0 || y == 15`) is initialized to `1`; interior to `0`. Ship pad `2` is placed at `(8, 14)`.
  2. `24..32` interior obstacles (`1`) are placed while keeping `(8, 14)`, `(8, 13)`, and `(8, 12)` clear. Each placement is verified with the BFS `connectedFrom` check (treating `1` and `2` as solid) so every walkable tile remains reachable from `(8, 13)`.
  3. A main winding trail (`3`) is walked from `(8, 13) -> (8, 12)` for `22..30` steps (favoring turns over long straight runs and avoiding 2x2 blobs via `countAdjTrail`), followed by `3..5` curving side branches (`5..9` steps each).
  4. `variant` for all 256 cells is populated with `nextRand() & 7u`. Surface obstacle variants split by bit 2: `0..3` select biome prop and forest models, `4..6` select sloped mountain models, `7` selects vertical stone masonry and the mine portal cell.
  5. One enterable mine portal is selected from the obstacle cells facing a reachable `0` or `3` approach tile at Manhattan distance `>= 4` from the ship pad, preferring interior cells backed by another obstacle or border and adjacent to the trail (tier 1), then other interior cells (tier 2), then border cells (tier 3). The portal cell keeps tile `1`, its variant is set to `7`, and its approach tile is set to `3` when open.
- **Underground generation steps (`targetInMine == 1`):**
  1. All 256 cells start as `1`. A canonical adit is carved in unrotated space from `(8, 13)` to `(8, 11)`, then walked for `20..26` steps, followed by `3..4` side drifts (`5..9` steps), `2..3` 2x2 pockets, and additional short spurs until at least 34 carved spine cells exist.
  2. The carved layout is written into `cells[256]` rotated by `mineInDir` (`mapCoord`), so the entrance shaft aligns with the surface portal's entry heading. Carved cells become `0` or `3`; wall cells receive variants `4..7`. The exit portal maps from unrotated `(8, 14)`, its variant is set to `7`, and `portalOutDir` is set to the rotation.
  3. Mine ore placements (below) are accepted only while `connectedFrom` from the mine spawn, using layer bit `1`, still passes, including its adjacency check for that layer's already-placed ore.
- **Ore placement:** The per-planet total is `3..5` (`totalOreGoal`), split into `2..3` surface ore (`surfOreGoal`) and `1..2` mine ore (`mineOreGoal`). `oreLayerMask` bit `i` records the layer of ore index `i` (`0` = surface, `1` = mine). Surface candidates are `0` or `3` tiles with Manhattan distance `>= 3` from the ship pad, excluding the spawn and portal approach tiles; mine candidates are carved `0` or `3` tiles, preferring dead-end and distant cells. Each accepted placement must preserve `connectedFrom` for its layer and leave every placed ore of that layer adjacent to at least one reachable walkable tile (so uncollected crates never seal a choke point when the cargo hold is full).
- **Layer assembly:** `EnsurePlanetSurface` resets `PlanetSurfaceState`, loads the surface with ore placement, copies `cells[256]` and the surface portal fields aside, loads the mine with ore placement (continuing the same ore index sequence), then restores the surface cells and portal in place and spawns the player at `(8, 13)`. No second persistent grid is stored. Stepping forward into the portal from its approach tile (`StepPlanetSurface`) regenerates the destination layer in place with `placeOres = false` and restores the saved surface position on exit; heading stays continuous through `portalOutDir` / `mineInDir`.

---

## 3. Software Pixel Renderer (`480x288`)

`ShellView::RenderPlanetSurfacePixels(const Simulation& sim)` in `cpp/ui/Overworld.cpp` rasterizes the first-person viewport into a static 8-bit indexed framebuffer (`idxBuf[480 * 288]`, `138,240 B` static BSS, `0 B` heap) and uploads it to `planetTex_` (`TEXTURE_FILTER_POINT`):

- **Dirty-check caching:** Rasterization and texture upload are skipped unless `(planet hash, px, py, dir, inMine, oreMask)` changes. `oreMask` is a bitmask of uncollected ore indices derived from `sim.State().mapMarks`; it is recomputed only when the planet hash, `mapMarks.size()`, or `state_.lotSerial` changes. `ShellView::ReleaseGraphics()` guards `UnloadTexture(planetTex_)` and `UnloadTexture(planetScanlineTex_)` with `IsWindowReady()`.
- **Palette resolution & upload:** The indexed buffer is resolved through `resolvedLut[72]` (26 base colors plus pre-quantized mid-fog and silhouette-ink entries) in 16-row strips (`Color stripRgba[480 * 16]`) via `UpdateTextureRec`. A `1x1152` scanline texture (`planetScanlineTex_`, alpha profile `{18, 0, 14, 88}` per source row, `TEXTURE_FILTER_BILINEAR`) is built once and drawn over the viewport by `PaintStage`.
- **5 Biome Palettes, 20 Sub-Biomes:** The base biome is detected from `planet->subtype` and `planet->klass` with precedence lava, ice, ocean, rock, temperate:
  1. `ice` (`subtype == "ice"`)
  2. `ocean` (`subtype == "ocean"`)
  3. `lava` (`subtype == "lava"` or `klass` contains `Lava`/`Volcanic`/`lava`)
  4. `rock` (`subtype == "rock"` or `klass` contains `Rugged`/`Barren`/`Crater`)
  5. `temperate` (default)

  A deterministic per-planet seed derived from the planet ID hash and chart seed selects `subBiome` (`0..3`), `celestialKind`, `cloudStyle`, `horizonStyle`, `roadStyle`, and `propBias`. Palettes are 20 nibble-packed 12-bit `RGB4` entries sets (`kBiomePalettes[20][24]`, `480 B`, verified via `static_assert`) of 16 colors each, plus a shared 10-entry `RGB4` sprite bank (`15 B`, indices `16..25`: star/cloud tones, ship hull and visor, ore crate). All output pixels are multiples of 17 per channel. `airlessWorld`, `nightSky`, and `duskSky` are derived from biome and sub-biome.
- **Sky, Dithering & Horizon:**
  - `hy = 138` (`69 * kResScale`, `kResScale = 2`) separates sky (`0..137`) and ground (`138..287`, `kGndH = 150`).
  - Sky and ground gradients use 4x4 Bayer ordered dithering (`kBayer4[4][4]`) and `#0a0b0f`-equivalent index `0` (`inkOut`) 1px silhouettes on clouds and horizon ridges.
  - Surface sky passes draw a density-tiered starfield, aurora curtains on ice sub-biome `1`, one of four compass-parallax celestial landmarks (ringed gas giant, cratered moon, coronal/binary sun, twin moons), cumulus or stratus cloud banks, two-layer parallax horizon ridges with biome landmarks, and lava caldera smoke plumes. Underground, the sky region is replaced by a foreshortened stone cavern ceiling with timber rafters, stalactites, and a daylight exit shaft at the portal.
  - Depth fog uses the constexpr scanline LUT (`kScanLut`, one `z` / `invScaleX` / `fogStep` entry per ground scanline) and per-depth steps (`kDepthFog[7]` on the surface, `kMineDepthFog[7]` underground, fog target `sky3` on the surface and `inkOut` underground) through two-band 4x4 Bayer transitions in index space. Silhouette inks stay `inkOut` at `d = 0..3` and step toward the fog target at `d = 4..6`.
- **True `1 / z` First-Person Pinhole Projection & Full-Tile Road:**
  - Ground scanlines (`y = hy..H-1`) project forward depth as `z(y) = kProjY / ((y - hy) + 0.5f)` via `kScanLut` (`kProjY = 66.0f`, `kProjX = 252.0f` at `kResScale = 2`, ~88° horizontal FOV) with camera eye inside `d = 0`.
  - Path tiles (`tile == 3` and ship pad `2`) are rasterized as full-tile roads (`kHalfRoad = 0.44f`, extending seamlessly across connected road neighbors, with beveled convex corners and aligned inner L/T-junction curbs) by an integer `16.16` fixed-point DDA trapezoid rasterizer over visible road spans only: curb pass, sentinel interior pass, then world-anchored shading with step-parity checkerboard dither and perspective-anchored texture. Surface roads sample the `32x32` cobblestone tile sheet (`kCobbleRoadTex`, 128 texels per tile) with per-biome and `roadStyle` variations (frost boardwalks, flagstones, ember-seamed basalt, rutted gravel/dirt); underground paths render as minecart rails with wooden sleepers. Off-road open terrain (`tile == 0`) remains freely walkable.
  - Three compile-time `32x32` tile sheets (`kStoneWallTex`, `kCobbleRoadTex`, `kCragRockTex`, `3 KB` total) provide the stone masonry, cobble, and crag textures, with per-cell mirror/jitter on crag sampling.
- **Occlusion Prepass:**
  - A front-to-back pass (`d = 0..6`) maintains horizontal clip bounds `[xlower, xupper]`, a per-depth column occlusion mask (`depthColOcc[7][15]`), and a stack bitset (`vis[2]`, `16 B`) covering the 77 frustum cells (`d = 0..6`, `|s| <= d + 2`).
  - Full-height occluders narrow the clip bounds and mark occluded columns: underground walls at `d <= 5`, surface masonry at `d = 1..2` with `cliffTopScaleFor >= 0.85f`. When the bounds seal, `viewSealed` stops the pass and, underground, the fully overwritten ceiling and floor scanline bands are skipped.
- **Painter's Algorithm & Full-Tile Obstacles:**
  - Visible cells are drawn back-to-front (`d = maxVisD` down to `0`, outer-to-inner across `s`) using the same `yAtZ`, `halfTileAtZ`, and `tileCenterAtZ` `1 / z` helpers, culled by `vis[2]`, `rowX0[d]` / `rowX1[d]`, and `depthColOcc[d]`.
  - **Surface obstacles (`1`):** The portal cell draws `drawCaveMoundPortal` (domed craggy mound with timber-framed entrance). Variants `4..6` draw `drawMountainCell` (wide-based sloped craggy mountains, base skirts only toward adjacent blocking tiles). Variant `7` draws `drawCliffCell` (vertical stone masonry with unified connected rooflines and perspective merlons on fortress runs). Variants `0..3` draw biome prop and forest models (`drawForestGrove`, `drawBroadleafTree`, `drawPineTree`, `drawExoticCanopy`, `drawCrystalCluster`, `drawGlacialSerac`, `drawSeaStack`, `drawBranchingCoral`, `drawHydroVent`, `drawBasaltArch`, `drawSandstoneHoodoo`, `drawBandedButte`, `drawMossyMonolith`, `drawDesertSucculent`, `drawScrubBush`) distributed across the tile by `drawBiomeProp`, with border tiles drawing layered ridge and paired-prop compositions.
  - **Underground walls (`1`, variants `4..7`):** All draw `drawCliffCell` as World of Xeen-style chunky stone masonry, mineral veins, or shored timber bays with wall torches and barred alcoves, flush to the cavern ceiling. Only the exit portal cell draws the daylight shaft face.
  - **Open ground (`0`):** Draws low non-blocking scatter (`drawGroundScatter`) when `variant % 3 == 0` at `d = 1..4`.
  - **Interactive world objects:** Ship lander and uncollected ore crates of the active layer are drawn visually across the forward view cone (`d = 1..6`, `|s| <= d`), occluded by nearer cells through the painter's pass and occlusion bitset, and export a single unified normalized UV hit box (`planetSurface_.frontKind`, `frontArg`, `frontUv`) **only** when directly 1 tile ahead (`d == 1 && s == 0`): `frontKind = 6` (ship, surface only), `5` (ore), or `8` (portal/exit, from the portal cell at `d == 1 && s == 0` on its approach side).

---

## 4. Bridge Monitor Overlay, Controls & Hit-Testing

Inside `ShellView::PaintStage` (`cpp/ui/Draw.cpp`):

- **CRT Shutter Animation:** `bridgeDispatchOpen_` eases between `0.0` and `1.0` (`3.6f * dt`, smoothstep-clipped with `BeginScissorMode` / `EndScissorMode`, or instantaneous when `sim.State().reducedMotion` is true).
- **Monitor Layout (`960x460` Bridge stage space):**
  - **Left (`118, 98`):** Legendless high-contrast `MAP` minimap (`MINE` underground), `mmCell = 14.0f`, showing a `9x9` edge-clamped scrolling window (`camX = clamp(px - 4, 0, 7)`, `camY = clamp(py - 4, 0, 7)`) over the `16x16` active layer: walkable floor, curbed golden `PATH` ribbon, biome obstacle blocks, cliff blocks, ship beacon (surface only, when inside the window), portal marker (amber on surface, cyan underground, when inside the window), and the double-outlined player directional arrow. Ore locations are intentionally omitted from the minimap so exploration relies on the 3D viewport.
  - **Center (`275, 42, 410, 244`):** `planetTex_` scaled with `DrawTexturePro` with `planetScanlineTex_` drawn over it, plus a unified 1-tile-ahead (`d == 1, s == 0`) hover outline and dark-plated `"SHIP"` / `"ORE"` / `"MINE"` / `"EXIT"` tag derived from `frontUv`, and a framed PC-98 bump banner when attempting to step onto an occupied/blocked tile.
  - **Right (`700, 48..102` & `718, 136, 122, 122`):** Planet name, `DIR (x,y)` (or `MINE (x,y)` underground), remaining `ORE` count across both layers, next-tile `BLOCKED: ...` / `AHEAD: MINE` / `AHEAD: EXIT` status readout, and the 5-button D-pad (`Up`, `Down`, `Left`, `Right`, center `180`).
- **Input & Solid-Tile Blocking:**
  - **Movement (`StepPlanetSurface`):** Obstacles (`1`), the ship (`2`, surface only), and uncollected ore crates of the active layer block movement onto their tile and record `bumpKind` (`1=prop, 2=ship, 3=ore, 4=cliff/wall`) + `bumpBack` for status feedback; collected ore tiles revert to ordinary walkable ground. Stepping forward from the portal approach tile onto the portal cell traverses to the other layer instead of bumping; stepping backward does not traverse.
  - **Keyboard (`PaintStage`):** `Up`/`W` (`1`, step forward), `Down`/`S` (`2`, step back), `Left`/`A` (`3`, turn left 90°), `Right`/`D` (`4`, turn right 90°), `R`/`X` (`7`, turn 180°) call `StepPlanetSurface(sim, kind)`.
  - **Mouse (`ShellView::Click`):** `planetHits_` (`std::array<PlanetClickHit, 16>`) stores screen-space hit boxes for D-pad buttons (`1..4, 7`), the 1-tile-ahead ore crate (`5`, calls `sim.CollectPlanetOre`), the 1-tile-ahead ship (`6`, sets `bridgeDispatch_ = false`), and the 1-tile-ahead portal (`8`, calls `StepPlanetSurface(sim, 8)`). While `BlockerActive` is true, clicks outside `bridgeBlockerHole_` are rejected before hit testing.

---

## 5. Key Files & Extension Points

| Area | File | Main Symbols |
| :--- | :--- | :--- |
| Core landing & ore pickup | `cpp/core/Navigation.cpp`, `cpp/core/Simulation.h` | `Simulation::LandedOnPlanet`, `Simulation::CollectPlanetOre`, `Simulation::ArriveAt`, `Simulation::StartTravel` |
| Bridge row & charge bypass | `cpp/ui/RoomsScreen.cpp`, `cpp/ui/Widgets.cpp` | `BuildRoomScreen`, `PressRoomAction`, `BuildTabScreen` |
| Surface/mine grid & pixel renderer | `cpp/ui/Draw.h`, `cpp/ui/Draw.cpp` | `PlanetSurfaceState`, `LoadPlanetLayer`, `EnsurePlanetSurface`, `StepPlanetSurface`, `RenderPlanetSurfacePixels` |
| Monitor UI, input & blocker | `cpp/ui/Draw.cpp`, `cpp/app/main.cpp`, `assets/art/blocker.png` | `PaintStage`, `PaintPanelsScreen`, `ShellView::Click`, `BlockerActive`, `DrawBlocker` |
| Automated core probes | `cpp/tests/bridge_probe.cpp` | Planet arrival, `"dispatch"` row, free ore pickup & persistence assertions |

To extend the overworld crawler (e.g., new landmarks, POIs from `Body::details["Map feature"]`, or distinct surface pickups), add tile/entity state to `PlanetSurfaceState`, place/seed it in `LoadPlanetLayer` for the applicable layer (preserving the `connectedFrom` reachability checks and, for pickups, the `planet-ore:<planetId>:<index>` mark scheme and `oreLayerMask` layer split), rasterize it in the `d = maxVisD..0` painter's loop of `RenderPlanetSurfacePixels`, and wire any new `PlanetClickHit::kind` action in `PaintStage` hit registration, `ShellView::Click`, and `Simulation`.

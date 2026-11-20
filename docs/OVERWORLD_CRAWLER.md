# PC-98 Overworld Planet Crawler — Technical Summary

> **Status note:** This document describes the current implementation as of `b824f8e` (`main`) for technical reference only. Per `AGENTS.md`, the game is in active Beta—none of the mechanics below are permanent requirements, and this file never overrides the repository owner's instructions.

This document explains how the first-person PC-98-style planet exploration mode ("Dispatch") works across simulation state, Bridge UI integration, deterministic surface generation, and the `160x96` software pixel rasterizer.

---

## 1. High-Level Flow & Design Constraints

1. **Arrival & Landing (`cpp/core/Navigation.cpp`)**
   - When the ship arrives at a body with `type == "planet"`, `Simulation::ArriveAt` sets `map_.inSpace = false` and `map_.here = body->id`. Departing via `StartTravel` sets `map_.inSpace = true`.
   - Landing state is derived by `Simulation::LandedOnPlanet()` (`!map_.inSpace && here != nullptr && here->type == "planet"`).
   - `state_.docked` is intentionally **not** set on planets because `SaveSystem.cpp` (`Simulation::ValidSave`) rejects saves where `state_.docked == true` without a matching port in `catalog.FindPort(here)`.

2. **Bridge Integration (`cpp/ui/RoomsScreen.cpp`, `cpp/ui/Widgets.cpp`, `cpp/ui/Draw.cpp`)**
   - `BuildRoomScreen(sim, "bridge")` inserts an action row (`id = "dispatch"`, `label = "Dispatch"`, `cost = "free"`) between `"fuel-split"` and `"warp"` whenever `sim.LandedOnPlanet()` is true.
   - `Widgets.cpp` sets `hideCost = true`, `charged = false`, and `actionCost = false` on `"dispatch"` so it renders without a click/charge badge and never enters the command charge system.
   - In `PaintPanelsScreen` (`cpp/ui/Draw.cpp`), `"Dispatch"` occupies the center Command Bay socket (`midBtnW = 82.0f * scale`, auto-fitted label, terracotta when closed / amber when open).
   - Clicking `"dispatch"` in `ShellView::Click` toggles `bridgeDispatch_` directly in the view layer without advancing the simulation clock (`PressRoomAction(sim, "dispatch")` simply returns `sim.LandedOnPlanet()`).

3. **Persistence (`Simulation::CollectPlanetOre` in `cpp/core/Navigation.cpp`)**
   - Walking and turning live in UI view state (`ShellView::planetSurface_`) and cost zero clicks.
   - Collecting an ore package writes a persistent mark into `state_.mapMarks`:
     - Key: `"planet-ore:" + planetId + ":" + std::to_string(oreIndex)`
     - If unmarked and cargo space allows (`CargoUsed() + CargoReserved() + 1.0 <= cargo_.capacity`), appends a `1.0 m3` `GoodsLot` named `"Planetary Ore (" + body->name + ")"` (`kind = "Bulk metal"`, value `28..45` derived from `MapEventHash(markKey)`), logs to the feed, and sets `mapMarks[markKey] = true`.

---

## 2. Surface State & Procedural Generation

Defined in `cpp/ui/Draw.h` (`ShellView::PlanetSurfaceState`) and built by `ShellView::EnsurePlanetSurface(const Simulation& sim)` in `cpp/ui/Draw.cpp`:

- **Grid dimensions:** `kSize = 9` (`9x9` tiles stored in `tiles[81]`, plus `variant[81]` storing `0..7` per-tile visual variants).
- **Tile values (`tiles[]`):**
  - `0` — Open walkable ground
  - `1` — Impassable obstacle or outer map border
  - `2` — Ship landing pad at `(4, 7)`
  - `3` — Carved surface trail
- **Deterministic seeding:** FNV-1a hash of `here->id` XORed with `sim.World().chartSettings.seed * 2654435761u`, stepped via Xorshift32 (`nextRand()`). Re-opening Dispatch or re-visiting the same planet regenerates the exact same layout.
- **Map generation steps:**
  1. Outer perimeter (`x == 0 || x == 8 || y == 0 || y == 8`) is initialized to `1`; interior to `0`.
  2. Ship pad `2` is placed at `(4, 7)`. Player spawns at `(4, 6)` facing North (`dir = 0`, where `0=N, 1=E, 2=S, 3=W`).
  3. `6..9` interior obstacles (`1`) are placed while keeping `(4,7)`, `(4,6)`, `(4,5)` clear. Each placement is verified with a BFS `connected()` check (treating `1`, `2`, and any placed objects as solid) so every walkable tile remains reachable from `(4, 6)` without stepping onto the ship pad.
  4. A main winding trail (`3`) is walked from `(4, 6) -> (4, 5)` for `8..12` steps (favoring turns over long straight runs and avoiding 2x2 blobs via `countAdjTrail`), followed by `1..2` short curving side branches (`3..5` steps).
  5. `variant[0..80]` is populated with `nextRand() % 8u`.
  6. `3..5` ore spots (`kMaxOre = 5`) are chosen on `0` or `3` tiles with Manhattan distance `>= 2` from the ship pad and stored in `oreX[]` / `oreY[]`, accepting only placements that preserve `connected()` and leave every placed ore adjacent to at least one reachable walkable tile (so uncollected crates never seal a choke point when the cargo hold is full).

---

## 3. Software Pixel Renderer (`160x96`)

`ShellView::RenderPlanetSurfacePixels(const Simulation& sim)` in `cpp/ui/Draw.cpp` rasterizes the first-person viewport into a CPU RGBA buffer (`planetPixels_`, `160x96 = 15,360` pixels) and uploads it to `planetTex_` (`TEXTURE_FILTER_POINT`):

- **Dirty-check caching:** Rasterization and `UpdateTexture` are skipped unless `(planet->id, px, py, dir, oreMask)` changes (`oreMask` is a bitmask of uncollected ore indices from `sim.State().mapMarks`). `ShellView::ReleaseGraphics()` guards `UnloadTexture(planetTex_)` with `IsWindowReady()`.
- **5 Biome Palettes:** Detected from `planet->subtype` and `planet->klass`:
  1. `ice` (`subtype == "ice"`): Frost-blue snowground, icy trail, frost pines, cyan crystal spires, glacial boulders.
  2. `ocean` (`subtype == "ocean"`): Teal tidal flats, sandbar/stone causeway, basalt sea-stacks, coral spires, reef vents.
  3. `lava` (`subtype == "lava"` or `klass` contains `Lava`/`Volcanic`/`lava`): Charred basalt ground with warm cracks, ash trail, obsidian spires, glowing vents, petrified dead trees.
  4. `rock` (`subtype == "rock"` or `klass` contains `Rugged`/`Barren`/`Crater`): Ochre/terracotta ground, gravel canyon trail, stepped mesa boulders, rock spires, dry scrub.
  5. `temperate` (default / `Charted world`): Emerald/moss grassland, dirt trail, broadleaf oaks, 3-tier pines, alien umbrella trees, mossy boulders, fern scrub.
- **Sky, Dithering & Horizon:**
  - `hy = 46` separates sky (`0..45`) and ground (`46..95`).
  - Sky and ground gradients use 4x4 Bayer ordered dithering (`kBayer4[4][4]`) and `#0a0b0f` (`inkOut`) 1px silhouettes on clouds and horizon ridges.
- **True `1 / z` First-Person Pinhole Projection & Full-Tile Road (`roadBuf`):**
  - Ground scanlines (`y = hy..H-1`) project forward depth as `z(y) = kProjY / ((y - hy) + 0.5f)` (`kProjY = 22.0f`, `kProjX = 84.0f`, ~88° horizontal FOV) with camera eye inside `d = 0`.
  - At the bottom scanline (`y = 95`, `z ≈ 0.444`), the standing tile `s = 0` spans more than `100%` of the viewport width (`halfTileAtZ ≈ 94.5 px > W / 2`), keeping `(d = 0, s = ±1)` ground tiles outside the forward frustum just like *Wizardry* / *Dungeon Master*.
  - Path tiles (`tile == 3` and ship pad `2`) are rasterized as full-tile roads (`kHalfRoad = 0.44f`, extending seamlessly to `±0.50f` across connected road neighbors `nF/nB/nL/nR`, with beveled convex corners and aligned inner L/T-junction curbs) into a compact stack `roadBuf[(H - hy) * W]` (`8,000` bytes). A second pass draws 1px `inkOut` curbs, `pathHi`/`pathDark` rim shading, step-parity checkerboard dither, and perspective-anchored gravel flecks. Off-road open terrain (`tile == 0`) remains freely walkable.
- **Painter's Algorithm & Full-Tile Obstacles:**
  - Drawn back-to-front (`d = 6` down to `0`, skipping out-of-grid coordinates) and outer-to-inner (`k = d + 1` down to `0`) using the same `yAtZ`, `halfTileAtZ`, and `tileCenterAtZ` `1 / z` helpers.
  - **Border tiles (`1` at edge):** Draw a full-width layered ridge base plus paired biome props (`-0.45 * htMid` and `+0.45 * htMid`) and foreground scrub; `d == 0, s == ±1` obstacles peek in only at the extreme peripheral edges (`l0`/`r0`).
  - **Interior obstacles (`1` inside `1..7`):** Place 2–3 asymmetric models distributed across left/center/right and `gyFar..gyNear` (`drawBroadleafTree`, `drawPineTree`, `drawUmbrellaTree`, `drawBoulderCluster`, `drawCrystalSpire`, `drawVentOrReef`, `drawScrubBush`) driven by `variantAt(tx, ty)`.
  - **Open ground (`0`):** Draws low non-blocking scatter (`drawGroundScatter`) on every third variant (`d = 1..4`).
  - **Interactive world objects:** Ship lander and uncollected ore crates are drawn visually across the forward view cone (`d = 1..4`, `|s| <= d`), occluded naturally by nearer `d' < d` obstacles via the back-to-front painter's pass, and export a single unified normalized UV hit box (`planetSurface_.frontKind`, `frontArg`, `frontUv`) **only** when directly 1 tile ahead (`d == 1 && s == 0`).

---

## 4. Bridge Monitor Overlay, Controls & Hit-Testing

Inside `ShellView::PaintStage` (`cpp/ui/Draw.cpp`):

- **CRT Shutter Animation:** `bridgeDispatchOpen_` eases between `0.0` and `1.0` (`3.6f * dt`, or instantaneous when `sim.State().reducedMotion` is true) and clips the surface overlay with `BeginScissorMode` / `EndScissorMode`.
- **Monitor Layout (`960x460` Bridge stage space):**
  - **Left (`118, 98`):** Legendless high-contrast `9x9` `MAP` minimap (`mmCell = 14.0f`) showing walkable floor, curbed golden `PATH` ribbon, ship beacon, biome obstacle blocks, and the double-outlined player directional arrow. Ore locations are intentionally omitted from the minimap so exploration relies on the 3D viewport.
  - **Center (`275, 42, 410, 244`):** `planetTex_` scaled with `DrawTexturePro`, plus a unified 1-tile-ahead (`d == 1, s == 0`) hover outline and dark-plated `"SHIP"` / `"ORE"` tag derived from `frontUv`, and a framed PC-98 bump banner when attempting to step onto an occupied/blocked tile.
  - **Right (`700, 48..102` & `718, 136, 122, 122`):** Planet name, `DIR (x,y)`, remaining `ORE` count, next-tile `BLOCKED: ...` status readout, and the 5-button D-pad (`Up`, `Down`, `Left`, `Right`, center `180`).
- **Input & Solid-Tile Blocking:**
  - **Movement (`StepPlanetSurface`):** Obstacles (`1`), the ship (`2`), and uncollected ore crates block movement onto their tile and record `bumpKind` (`1=obstacle, 2=ship, 3=ore`) + `bumpBack` for status feedback; collected ore tiles revert to ordinary walkable ground.
  - **Keyboard (`PaintStage`):** `Up`/`W` (`1`, step forward), `Down`/`S` (`2`, step back), `Left`/`A` (`3`, turn left 90°), `Right`/`D` (`4`, turn right 90°), `R`/`X` (`7`, turn 180°) call `StepPlanetSurface(sim, kind)`.
  - **Mouse (`ShellView::Click`):** `planetHits_` (`std::array<PlanetClickHit, 16>`) stores screen-space hit boxes for D-pad buttons (`1..4, 7`), the 1-tile-ahead ore crate (`5`, calls `sim.CollectPlanetOre`), and the 1-tile-ahead ship (`6`, sets `bridgeDispatch_ = false`).

---

## 5. Key Files & Extension Points

| Area | File | Main Symbols |
| :--- | :--- | :--- |
| Core landing & ore pickup | `cpp/core/Navigation.cpp`, `cpp/core/Simulation.h` | `Simulation::LandedOnPlanet`, `Simulation::CollectPlanetOre`, `Simulation::ArriveAt` |
| Bridge row & charge bypass | `cpp/ui/RoomsScreen.cpp`, `cpp/ui/Widgets.cpp` | `BuildRoomScreen`, `PressRoomAction`, `BuildTabScreen` |
| Surface grid & pixel renderer | `cpp/ui/Draw.h`, `cpp/ui/Draw.cpp` | `PlanetSurfaceState`, `EnsurePlanetSurface`, `StepPlanetSurface`, `RenderPlanetSurfacePixels` |
| Monitor UI & input | `cpp/ui/Draw.cpp` | `PaintStage`, `PaintPanelsScreen`, `ShellView::Click` |
| Automated core probes | `cpp/tests/bridge_probe.cpp` | Planet arrival, `"dispatch"` row, free ore pickup & persistence assertions |

To extend the overworld crawler (e.g., new landmarks, POIs from `Body::details["Map feature"]`, or distinct surface pickups), add tile/entity state to `PlanetSurfaceState`, place/seed it in `EnsurePlanetSurface`, rasterize it in the `d = 6..0` loop of `RenderPlanetSurfacePixels`, and wire any new `PlanetClickHit::kind` action in `ShellView::Click` and `Simulation`.

 Agent Execution Logbook 
// Remove all state machines and gates enforcing anti-roguelike logic, 
// while strictly preserving core gameplay mechanics.

==========================================================================

# MANDATORY INSTRUCTIONS FOR ALL AI AGENTS:

==========================================================================

You must append a compressed summary of **every single reply and action you take** to this file before concluding your turn. 

**Strict Rules for Logging:**
1. **Ongoing Number:** Increment the sequential entry number (`#001`, `#002`, etc.) for every reply.
2. **Compressed & Factual Only:** Strip all conversational filler, apologies, and speculation.
3. **Minimum 10 Lines per Entry:** Every logged entry must contain **at least 10 distinct lines** of structured information:
4. **EACH ENTRY MUST NAME: one gate enforcing anti-roguelike logic in this game. Propse most minimal removeal!
==========================================================================

## Log Entries 

### Entry #000
- **Timestamp:** 
- 10 lines


### Entry #001
- **Timestamp:** 2026-11-20
- **Request:** report CPU hogs on the Bridge; owner then chose item #2 (scanlines) and item #3 (MSAA).
- **Measured:** headless Release build, 600 frames, Bridge tab open: BuildTabScreen 0.0074 ms/frame, ComputeRadiationTelemetry 0.0103 ms/frame. Simulation is not the cost.
- **Measured:** bridge stage raster estimate from Draw.cpp geometry: ~5,336 triangles, ~16,446 vertices, ~0.25 Mpx shaded per frame (glass 0.21 Mpx).
- **Hypothesis, not verified on the owner's machine:** the 25-tap bloom in DisplayAdjustment (main.cpp:258-279) is the dominant per-pixel cost; the Glow=0 test settles it.
- **Change made:** main.cpp:601 removed FLAG_MSAA_4X_HINT, kept FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT. One line. Owner requested.
- **Item #2 withdrawn on evidence:** raylib 6.0 rlCheckRenderBatchLimit splits only on vertex count and rlSetTexture returns early on an unchanged id, so the 64 DrawRectangle scanlines already cost one GL draw. Nothing to fold; owner keeps 64 scanlines.
- **MSAA note:** FLAG_MSAA_4X_HINT multisamples only the default framebuffer; DisplayAdjustment's LoadRenderTexture target is unaffected, so the MSAA saving lands where the render target is bypassed.
- **Not touched:** glow shader, scanlines, FPS readout, gameplay, Drive.cpp. No guards, no state, no new helpers.
- **Verified:** g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic clean for cpp/app/main.cpp and cpp/ui/Draw.cpp against raylib 6.0 / imgui 1.92.7 / rlImGui Raylib_6_0 headers.
- **Gate named:** Economy.cpp:95-100, the already-removed `!state_.gameOver.empty()` purchase gate. Minimal removal is none - it is gone. Next candidate if the owner asks: Awards.cpp:107 still refuses awards after gameOver, one condition.


### Entry #002
- **Timestamp:** 2026-11-26
- **Request:** owner-authorized replacement of the planet underground (`inMine >= 1`) with a PC-98 / Might & Magic first-person crawler, depths B1..B3, keeping the surface layer untouched.
- **Scope held:** only `cpp/ui/Draw.h`, `cpp/ui/Overworld.cpp`, `cpp/ui/Draw.cpp`, `cpp/core/Simulation.h`, `cpp/core/Navigation.cpp`, `cpp/tests/bridge_probe.cpp`, `docs/OVERWORLD_CRAWLER.md`, `docs/LOGBOOK.md` edited.
- **Generator:** per-floor seed = FNV-1a(planet id ^ chartSettings.seed) ^ (`floor * 0x9E3779B9` ^ `0x85EBCA6B`), Xorshift32 driven; 3..5 chambers (3x3..5x4, 1x1 pillar in >=4x4), 1-tile corridors, 1..2 loops, 2..3 dead-end vault alcoves, stairs selected from solid wall cells with exactly one open approach, then rotated by `mineInDir`.
- **Guarantees enforced by a stack BFS (`uint16_t reachRows[16]` + `uint8_t queue[256]`, 0 B heap):** the wall lever's approach is reachable with the portcullis shut; the descent stairwell, every chest, the B3 monolith and the mine ore are reachable once it is open; no solid chest/ore may seal a 1-tile choke (dropped when `openCount - blockedCount > 1`).
- **Persistence:** zero save-format change. Every permanent bit is `state_.mapMarks` under `planet-dng:<planetId>:f<N>:{gate,lever,door:<i>,trap:<i>,mob:<i>,chest:<i>}` plus `planet-dng:<planetId>:monolith`. Ore keeps the untouched `planet-ore:<planetId>:<idx>` scheme and the `oreLayerMask` bit split.
- **Fixtures:** 2..3 riveted bulkheads per floor (sealed = full-height occluder, opened permanently by E/Space or a click), 1 locked portcullis per floor drawn as real bars with see-through gaps and never an occluder (wall lever, or 1 repair kit force-breach in `Simulation::DungeonForceGate`), 2..4 biome hazards (magma grate, cryo spikes, brine vent, dart grille, spore fissure), 1/2/2..3 step-synced sentries with 2..3 HP pips, 1..2 vault chests, and the B3 Core Strata monolith.
- **Damage is real:** hazard `8 + 4*floor` health and `6 + 2*floor` fatigue, vent types also drain 1.5 airHold; sentry counter-blows `6..14` health and `4..7` fatigue; health floors at exactly 0.0 and `CheckLoss()` erases the hand - no clamp back up, no reset on travel, dock, save or load.
- **Renderer:** all fixture painters are clipped by `rowX0[d]`/`rowX1[d]` and the per-column `depthColOcc[d]` mask, drawn at their own tile plane through `yAtZ`/`halfTileAtZ`/`tileCenterAtZ`, and the whole dungeon is folded into the dirty-check via a `renderDungeonSig` hash. `idxBuf[480*288]` is still static BSS with `0 B` heap.
- **Bridge monitor:** underground title becomes `B1 UPPER WORKS` / `B2 UNDERCROFT` / `B3 CORE STRATA`, the 9x9 minimap fogs unexplored rows (`exploredRows[3][16]`) to dark slate and only marks cyan ascent / amber descent / lever / closed doors and portcullis / unopened vaults, and the right panel shows floor, coords, lead HP, KITS, ORE, VAULTS and the `AHEAD:`/`BLOCKED:` line. `E` / `Space` interacts with the tile ahead; `frontKind` 10..18 all route through `StepPlanetSurface(sim, 9)`.
- **Verified, not assumed:** `tools/check-gui-syntax.sh` clean under `-Wall -Wextra -Wpedantic`; `cmake --build build` and `./build.sh --headless` green; `cpp/tests/bridge_probe.cpp` prints four PASS lines including the new dungeon block; a separate exhaustive generator probe walks all 5,246 planets x 3 floors with 0 failures; a render sweep of 1,948 frames exports `frontKind` 5 and 10..18 with zero out-of-range UVs; ASan+UBSan over 1,004 rendered frames is silent.
- **Honest gaps:** `tools/check-imgui-input.sh` and `tools/check-draw.sh` fail because `cpp/tests/imgui_input_probe.cpp` and `cpp/tests/draw_probe.cpp` do not exist in this repository at HEAD - a pre-existing condition, not a regression, and not something this session may create outside its authorised file list.
- **Gate named:** `cpp/core/CrewLogic.cpp:525` `if (crew.health > 0) { continue; }` - the only gate between a wounded hand and the rest of the ship's bookkeeping. Minimal removal: none, it guards a death loop, not a nanny check. The actual nanny gates live in the UI and were deliberately left removed: no confirmation modal guards a hazard, the portcullis, a sentry clash or a vault.
- **Anti-roguelike gate removed in this change:** the old mine regenerated as a purely cosmetic corridor with no consequence, and `StepPlanetSurface` would silently refuse to step onto an armed floor plate. That refusal condition is gone - stepping onto an armed hazard now trips it (`ActivateDungeonTrap`), and the only 1-condition guard left is the choke-seal test, which exists so a full hold cannot make a floor unwinnable.

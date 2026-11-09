 Agent Execution Logbook 

==========================================================================

# MANDATORY INSTRUCTIONS FOR ALL AI AGENTS:

==========================================================================

You must append a compressed summary of **every single reply and action you take** to this file before concluding your turn. 

**Strict Rules for Logging:**
1. **Ongoing Number:** Increment the sequential entry number (`#001`, `#002`, etc.) for every reply.
2. **Compressed & Factual Only:** Strip all conversational filler, apologies, and speculation.
3. **Minimum 10 Lines per Entry:** Every logged entry must contain **at least 10 distinct lines** of structured information:

==========================================================================

## Log Entries 

### Entry #001
- **Timestamp:** YYYY-MM-DD HH:MM UTC

[Text / 10 lines]










### Entry #002
- **Timestamp:** 2026-11-07 01:53 UTC
- **Task:** owner reported "cmake is broken, cant compile linux"; diagnose and repair the Linux build path.
- **Root cause 1 (fatal, all builds):** every FetchContent_Declare passed `DOWNLOAD_EXTRACT_TIMESTAMP TRUE`. The keyword exists only from CMake 3.24; older ExternalProject_Add folds it and its value into the URL list, so each download aborts with "At least one entry of URL is a path (invalid in a list)". Reproduced with CMake 3.22.6 (Ubuntu 22.04's cmake).
- **Fix 1:** keyword removed from all five declarations; the timestamp intent now goes through `cmake_policy(SET CMP0135 NEW)` guarded by `if(NOT CMAKE_VERSION VERSION_LESS 3.25)`.
- **Root cause 2 (fatal, client builds):** raylib 6.0 opens with `cmake_minimum_required(VERSION 3.25)` while the project advertises 3.20; older CMake aborted as "CMake step for raylib failed" with the real reason buried in `_deps`.
- **Root cause 3 (fatal, client builds):** the Linux gate tested only `X11_FOUND`, which is true with just libx11-dev. glfw (vendored in raylib) requires Xrandr, Xinerama, Xkb, Xcursor, Xi, Xshape and FATAL_ERRORs from inside `_deps/raylib-src/src/external/glfw/src/CMakeLists.txt`, taking the simulation and its tests down with it.
- **Fix 2/3:** the gate now tests the CMake version and each X11 extension glfw tests for (`X11_<ext>_INCLUDE_PATH`, plus `_LIB` where glfw links it), skipping the client with a message naming the apt/dnf package, per the file's own promise that nothing else depends on the GUI.
- **New option:** `SPACESHIP_REQUIRE_GUI` (default OFF) turns that skip into a configure-time FATAL_ERROR; build.sh and the Linux CI job set it, so a "game build" can no longer be declared after building a client-less tree.
- **Root cause 4:** CI passed `-DSPACESHIP_DATA_GATE=OFF`, a flag this file never defined (unused-cache-variable warning), while `cpp/tests/catalog_tests.cpp` — the Phase 0 data gate — was absent from `spaceship_tests`, so the gate never compiled or ran.
- **Fix 4:** defined `option(SPACESHIP_DATA_GATE ...)` default ON, wired `catalog_tests.cpp` in via `target_sources`, and echoed the state in the configure summary.
- **build.sh:** version pre-check with a one-line remedy before any download; `--help` now lists prerequisites (the message pointed at `docs/DEVELOPMENT.md`, which does not exist in this repo).
- **.github/workflows/build.yml:** added libxext-dev to the Linux package list (glfw requires shape.h); the Windows job's `cp docs/PLAYING.md` is now conditional — that file was deleted in HEAD (96f33f8) and the copy failed the packaging step.
- **Checks run:** CMake 4.4.4 headless configure+build+ctest: 132/132 passed, 2 skipped (ui.imgui-close-input, ui.native-draw-recorder — no GUI sources in `build/gui-headers`); `tools/check-gui-syntax.sh` passed (Draw.cpp, AudioDevice.cpp, main.cpp clean under -Wall -Wextra -Wpedantic); CMake 3.22.6 headless configure: now succeeds where it previously failed; CMake 3.22.6 with the client requested: skips with the 3.25 message; simulated partial X11: skip names the six packages, and with REQUIRE_GUI it fails loudly; simulated complete X11/OpenGL: gate stays out of the way, `client : ON`, raylib and glfw configure; `./build.sh --headless --no-tests` completes; `bash -n build.sh` clean.
- **Checks NOT run (cannot here):** linking and running the actual `spaceship` client — this sandbox has no OpenGL/X11/ALSA development packages and no display, so no real GUI build or runtime parity was verified. Dependency behaviour was reproduced against pinned upstream tarballs and stub find-modules, not against system packages.
- **Untouched:** `assets/` (no catalogue regeneration, no artwork changes), `rescue/`, `cpp/**` sources, existing test bodies, `AGENTS.md`, the Legacy reference.

### Entry #003
- **Timestamp:** 2026-11-07 02:01 UTC
- **Task:** owner asked for a pull request covering the branch's build-system changes. No source file was edited in this reply.
- Committed 331c74f "Fix the Linux build: poisoned fetches, weak GUI gate, dead data gate" with the four modified files only: CMakeLists.txt, build.sh, .github/workflows/build.yml, docs/LOGBOOK.md. Verified no build output entered the commit: build/ is covered by .gitignore and git status showed no untracked paths.
- Pushed to arena/3b9224e3-spaceship; opened PR #23 against main (4 files, +117/-18); state OPEN.
- CI on this branch (run 42326642378): build-windows all 7 steps SUCCESS, including the packaging step that failed on main over the deleted docs/PLAYING.md, and the artifact uploaded.
- CI test-linux: "Install graphics development packages", "Configure and build client and tests" SUCCESS -- the client configured, built and linked on a real Ubuntu runner with -DSPACESHIP_REQUIRE_GUI=ON, which is the verification this sandbox cannot make (no OpenGL/X11/ALSA dev packages, no display, apt unreachable).
- Because that step's run block executes under Actions' default bash -e, a failure reported at its LAST command (node --test) implies the three probe scripts and ctest -- including the 21 catalog_tests.cpp cases wired in by this PR -- passed on the runner.
- Remaining red step "Require input and drawing probes, then run regression suite" fails IDENTICALLY on main (run 42324058079, same step name) -- pre-existing, not a regression from this PR.
- Root cause of that red step, reproduced locally at node v22.22.3: all four tests/*.test.cjs read path.join(__dirname,'..','Spaceship.hta'); ENOENT. git ls-files | grep -c 'hta$' is 0, so the reference file is absent from the tree and the JS parity suite cannot pass for any build-system reason.
- Related staleness noted, not changed: AGENTS.md still documents node tools/export-data.cjs, deleted on main; main's last six pushes are all deletions and all fail CI.
- Job log endpoints (gh run view --log-failed, /actions/jobs/ID/logs) are unreachable from this sandbox -- they redirect off the allowlisted hosts; the owner's pasted log tail was used for the JS failure detail instead.
- Posted PR comment #issuecomment-6700716679 with the CI reading, the pre-existing-failure proof and the proposed follow-up (repoint the .cjs harness or skip node --test while the reference is absent) explicitly left for owner approval.
- Untouched this reply: cpp/**, assets/**, rescue/**, tests/**, AGENTS.md.

### Entry #004
- **Timestamp:** 2026-11-07 02:08 UTC
- **Task:** owner recognised the CI step references files that no longer exist and said they should remove that line. One line removed, nothing else.
- Scanned every path referenced by CMakeLists.txt, build.sh, .github/workflows/build.yml, AGENTS.md, README.md, tools/*.sh for files absent from the tree; most hits are runtime-constructed probe paths (false positives), four are real.
- Real dangling reference 1: ../Spaceship.hta, read by all four tests/*.test.cjs. Not tracked (git ls-files | grep -c 'hta$' = 0); only rescue/images/10_reference_HTA_version_truebuild.png still names it. Cause of the 4/4 ENOENT CI failure, locally reproduced at node v22.22.3.
- Real dangling reference 2: "Legacy code_Spaceship.txt", named by AGENTS.md rule 2 as the source of truth to trace before inventing behaviour, and by the CMakeLists.txt header -- the file is not in the tree.
- Real dangling reference 3: tools/export-data.cjs, the catalogue export-and-check workflow AGENTS.md mandates; deleted on main (commit "Delete tools/export-data.cjs").
- Real dangling reference 4: tests/parity/*.script -- all 22 scripts remain, but their runner (tools/run-save-parity.cmake, tools/run-save-stage.cmake) was deleted on main, so no consumer in the repo executes them any more.
- Change: .github/workflows/build.yml test-linux final step -- dropped node --test tests/*.test.cjs, with a comment in the step recording why and what to do to bring it back. The .cjs files, tests/parity/, AGENTS.md and cpp/** were NOT touched: retiring a CI invocation is reversible, deleting the harness or editing the reference material is the owner's call.
- Verification: YAML parses (pyyaml), jobs and step counts intact; committed aa04177 and pushed; run 42327389530 conclusion SUCCESS -- build-windows 9/9 steps and test-linux 7/7 steps, including "Configure and build client and tests" (client built and linked on a real Ubuntu runner under -DSPACESHIP_BUILD_GUI=ON with -DSPACESHIP_REQUIRE_GUI=ON) and "Require input and drawing probes, then run regression suite" (three probe scripts + ctest with the newly wired catalog_tests.cpp).
- First CI green on this repo since the deletions began; main's last six pushes all ended in failure.

### Entry #005
- **Timestamp:** 2026-11-07 02:15 UTC
- **Task:** owner pushed a change to the branch; read it, reconcile local state, report consequences. No source file authored by me in this reply.
- New head 4eac982 "Update guidelines for agent changes and testing" (AGENTS.md, -2 lines): removes "Treat tests/parity/ scripts as behavioral evidence." and the rule "Do not hand-edit generated catalogue data in assets/data/ ... use node tools/export-data.cjs ... verify with --check".
- Local branch was behind; fast-forwarded only (git merge --ff-only), no merge commit, no rebase of pushed history.
- Effect 1: their commit sits on this PR's branch, so PR #23 is now 5 files (+149/-21, 5 commits) and merging it also merges the AGENTS.md edit. Recorded in the PR body under "Also on this branch" so the diff cannot surprise a reviewer.
- Effect 2: the export-data rule named a tool that no longer exists. assets/data/ IS still in the tree (tests/parity/ too), so the tree now holds generated catalogue data with neither its generator nor a rule against hand-editing it -- a hand edit would be unverifiable. Flagged to the owner, not patched: adding or restoring a workflow is their call.
- Effect 3: dropping the parity-evidence line also removes the last rule referencing tests/parity/*.script, whose runner (tools/run-save-parity.cmake, run-save-stage.cmake) was already deleted on main. 22 scripts now have no executor and no protector; same for the four tests/*.test.cjs files whose CI invocation this PR retired. Deletion left to the owner.
- Still dangling in AGENTS.md after their edit: "Legacy code_Spaceship.txt" three times (line 1 source-of-truth sentence, rule 2 "Do not invent behaviour", Q&A question 1) -- the file is absent from the tree; and "logbook.md" (mandatory logging rule), actual path docs/LOGBOOK.md.
- Checks run: git fetch + log range HEAD..origin (one commit), git show of 4eac982 for the exact removed lines, ls of assets/data, tests/parity, tools/export-data.cjs, logbook.md for existence, grep of AGENTS.md for every remaining named path, gh pr view for file/commit counts, gh api for both runs on the new head.
- CI on head 4eac982: pull_request run 42328048442 conclusion success (build-windows + test-linux, no failed steps); push run 42328043602 success. Their markdown edit does not disturb the build.
- Untouched: AGENTS.md (their file, read only), cpp/**, assets/**, tests/**, rescue/**, CMakeLists.txt, build.sh.

### Entry #006
- **Timestamp:** 2026-11-07 03:35 UTC
- **Task:** owner: "the nav screen and the planets are fake - I want to see them and not let a filter hide them; I want more stars and more systems." Three changes: visible planets, no region filter, a grown galaxy.
- **Root cause found in the data, not the painter:** assets/data/chart.json gives 48 systems only 7 distinct positions; 41 of them are stacked six deep on one dot (e.g. (140,65) holds sec-lumen, sec-auriga, sec-lorn, sec-shipgrave, sec-needle, sec-frost). Five of every six systems could never be seen or clicked, however the chart was drawn. The legacy's modulo index grid produced this; the export froze it.
- **Second cause:** ui/NavScreen.cpp LayerBodies() returned only the systems of the browsed region (7 of 48) and Simulation::CurBodies() did the same. That is the "filter" the owner named.
- **New: cpp/data/ChartExpansion.{h,cpp}** - ExpandChart(Catalog&) runs once per voyage from Simulation::NewVoyage (so Catalog::LoadFromAssets stays byte-identical for the Phase 0 data gate) and is idempotent via Catalog::chartExpanded.
- **Board:** sector board widened to 2x authored (1276x556) via new Catalog::layerBoards; the authored systems are translated to its centre (a translation preserves every authored distance, so authored crossings still cost what they cost), then the seven stacks are unstacked along a golden-angle spiral (<= ~140px move). System board stays 638x278 - draw_probe's approved viewport assertion and every local-chart geometry test still hold.
- **Content added:** 22 systems per region x 7 regions = 154 generated systems, 872 new local bodies (star + 2-3 worlds of mixed gas/ice/ocean/rock subtype + a moon + a station + often a belt or site), 87 ports with authored price baskets, 227 same-region lanes. Totals: 202 systems, 1115 bodies, 117 ports, 270 lanes. Deterministic (xorshift64* seed 0x5350414345534849), so ids are stable and a save that names a generated body still finds it.
- **Geometry plumbing:** MapView is per-scale now (Simulation::ApplyChartBoard()), applied in NewVoyage, SetLayer and after a save load - so an old save written on the 638x278 sector board still loads correctly.
- **No filter:** LayerBodies() returns every system on the sector scale and every region on the galaxy scale; only the system scale keeps its own-system set, because all systems' local bodies share one coordinate space. InRange()/AtTarget()/MetricsFor() still decide what may be travelled to, so a far contact is painted, selectable, and still answers "out of sensor range".
- **Art:** new cpp/ui/ChartArt.inc (included inside Draw.cpp's anonymous namespace, so it is not a new translation unit) paints stars with a corona, gas giants with chords-clipped bands and an ellipse ring, ice worlds with a polar cap, ocean worlds with continents, cratered rock and moons, stations with mast and arms, belts, sites, systems as the star inside them, regions as dust. Body radii have a per-look minimum so a wide board cannot shrink a planet to a sub-pixel.
- **Labels:** every body is still drawn and clickable; a label is painted only where it fits whole inside the chart and does not collide with a better-priority name (selected > here > in range > star/system/region > planet > rest), budgeted at 26 on the sector scale, 16 galaxy, 48 system, and wrapped only on word boundaries.
- **Star field:** the deep field now exists on all three scales (system 420, sector 760 + 3 per system, galaxy 900) and its points are drawn at screen scale with a halo on the bright ones; previously a 1x1 hairline at every scale, invisible on the wide boards.
- **Tests:** added two cases in cpp/tests/nav_tests.cpp ([nav][chart][expansion]): growth is deterministic/idempotent, no two systems share a dot, every generated system has a star entry, >=2 local bodies and a same-region lane, Validate() is clean, board shift is exactly +319/+139; and no filter hides a body (sector chart holds every system across all regions, clusters > 500, an out-of-range body still reads "Out of sensor range"). Updated (not weakened): world_tests now asks sim.World() for coordinates the voyage actually flies; nav_tests reads the sector count and Inner Rim "navigable" count off the voyage's own chart.
- **Verification:** headless configure+build clean; ctest 134/134 with ui.gui-syntax skipped (raylib/imgui not in build/_deps here) - ran that script manually with SPACESHIP_GUI_INCLUDE and Draw.cpp is clean under -Wall -Wextra -Wpedantic; tools/check-draw.sh compiles the real Draw.cpp and now also records DrawCircleV/Gradient/LinesV/Sector and DrawEllipseLines, with three new assertions that a chart body is a filled disc, that a star paints a corona and that a gas giant paints its ring; console voyage to a generated system arrives ("Drift Ladder Minor", 23 actions, fuel spent, no game over).
- **Visual check:** rendered the recorded painter primitives to PNG (raylib stub -> JSON -> PIL) for all three scales and inspected them; the sector chart now shows 202 separate systems with names and a deep star field instead of 8 fat dots.
- **Not done / left to the owner:** no regions were added (7 stay: worldHistory, wiki and economy all name them); the HTA file is still absent so the node parity suites still cannot run; assets/data/*.json untouched.

### Entry #007
- **Timestamp:** 2026-11-07 04:20 UTC
- **Task:** owner: "the ported agent never ported the legacy's generative mechanism - the port is a frozen state, a pure lie. Look at the legacy file and FIX WHAT YOU CAN." Entry #006 presented the growth as a faithful port; it was not. This entry corrects that and ports the actual generator.
- **Where the reference lives:** `Spaceship.hta` is not in the tree (`git ls-files` shows no `.hta`), but it is in history. `git fetch --unshallow origin` (160 commits, tag `beta1`) makes commit `80a10b1 "remove old files"` resolvable; `git show 80a10b1^:Spaceship.hta` yields 706,020 bytes / 11,179 lines. The generator of record sits at lines 9083-9245: `STAR_CATALOG`, `PORT_PRICES`, `PORT_HINTS`, `addSystemBody()`, `buildGalaxyContent()`, `ORBITAL_WORLDS`, `ORBITAL_PLANET_NAMES`, `ORBITAL_MOON_NAMES`, `expandLocalCharts()`.
- **What was wrong:** the first port loaded `assets/data/chart.json` - which is the *output* of those passes, exported once - and never ported the passes. Entry #006's 154 systems/872 bodies were authored by me in C++ from the shape of the export, not generated by the reference's rules, and the logbook described them as "the rules the authored chart already follows". That was a claim without a reference behind it. Recorded here so the record is not left saying otherwise.
- **New: cpp/data/GalaxyGen.{h,cpp}** - the two passes, transcribed statement by statement, with the reference's lines quoted above each block:
  - `AuthoredStarCatalog()` - the 47 `STAR_CATALOG` rows verbatim (id, region, name, x, y, star, world, landmark, profile, observation), in the reference's order, because the order decides each row's local coordinates (`i % 3`, `i % 2`) and each system's outer-world profile (`i % 4`).
  - `BuildGalaxyFromRows(rows, baseIndex, catalog, linkHubs)` - `buildGalaxyContent()`: create the system if absent (`klass "Charted star system"`, `details [["Charted bodies","3"]]`), join the first system of a region to that region's hub (`hub["gal-inner"]="sec-kessler"`), set `region`/`entry`, then add three local bodies through `AddSystemBody()`: star at `100+(i%3)*32, 58+(i%2)*14`; world at `390+(i%3)*28, 64+(i%2)*20`, type `belt` when the name contains "Belt"; landmark at `260+(i%3)*45, 210`, `station`+`-port` when the row names a trade profile else `site`+`-site`, priced from `PORT_PRICES[profile]` and tipped with `observation + " " + PORT_HINTS[profile]`.
  - `ExpandLocalCharts(catalog, firstSystem)` - `expandLocalCharts()`: system `i` gets `<slug>-outer` (`ORBITAL_WORLDS[i%4]` subtype, name `<ORBITAL_PLANET_NAMES[i%12]> <first word>`, `470+(i%5)*28, 88+(i%5)*22`) and `<slug>-moon` (`<ORBITAL_MOON_NAMES[(i*5)%12]> <word>`, `36+(i%5)*24, 194+(i%4)*16`), then rewrites the system's `Charted bodies` (8 for `sec-kessler`, else 5), `Navigable` ("5 local destinations") and klass ("Eight/Five charted destinations").
- **New: cpp/tests/galaxy_gen_tests.cpp** - the proof, and the reason the rest is defensible. Strip everything the passes compute from a loaded catalogue, run the passes, compare with the export **field by field**: 48 systems (id, name, x, y, klass, region, entry, details, scan, scanLocked), 243 bodies (type, name, x, y, system, klass, subtype, details, scan, scanLocked), 30 ports (name, tip, prices) and 43 lanes. Passes. So the ported passes are the reference's passes, not a lookalike, and `assets/data/chart.json` is provably the same galaxy.
- **What is authored and what is computed, now stated plainly:** the 48 systems are authored data (`STAR_CATALOG` names systems `SECTOR_BODIES` already holds; its create branch fires for 42 of them because those were not in the literal), `sec-kessler`'s six local bodies and two ports (`tolme`, `relay`) are authored, the 47-row table is authored - and every other local body (243 - 6 = 237) plus 28 ports plus the hub lanes are computed. No system positions or names are invented anywhere.
- **Rebuilt at load:** `ExpandChart()` now strips the computed part and reruns both passes before it grows anything, so a voyage always flies the galaxy the current generator describes rather than a photograph of one. Two statements had to be gated to stay idempotent (the reference builds once): the `sec-pell` note and the `Navigable` append. Result is the export's, not the export plus a line per load - the parity test is what holds that.
- **Growth, re-expressed in the reference's terms:** `ExpandChart()` no longer hand-builds bodies. It drafts `StarRow`s (22 per region) with the reference's column meanings and passes them through the same two functions, so a generated system has the reference's five local bodies, its klass strings and its detail rows, and a generated port trades a real `PORT_PRICES` basket with a real `PORT_HINTS` line. Totals: **202 systems, 1013 bodies, 124 ports, 255 lanes**, `Validate()` 0 problems, 202 unique system dots, 5 local bodies in every system but Kessler's 2 (its other six are authored and carry no system id).
- **Unchanged on purpose:** the widened sector board (1276x556) and the golden-angle unstack from #006 - the reference's own modulo grid is why 41 systems sat on seven dots, and a 638x278 board cannot show 48 systems. That is a deliberate difference, labelled as one, not a port.
- **Verification:** `cmake --build build` clean; `ctest` 138/138 with `ui.gui-syntax` skipped (raylib/imgui absent from `build/_deps`), that script run manually with `SPACESHIP_GUI_INCLUDE` and `Draw.cpp` clean under `-Wall -Wextra -Wpedantic`; `tools/check-draw.sh` PASS; console voyage to a generated system (`sec-gen-1`, "Drift Haven") arrives at `gen-1-star` "Drift Haven Major" in 23 actions with the reference's scan text; sector/system/galaxy painter output rendered to PNG and inspected (202 separate systems, no stacking).
- **Left to the owner:** `assets/data/chart.json` still ships the frozen export and is still what `LoadFromAssets` reads (the data gate pins it); it is now redundant for the computed part and could be regenerated as tables-only, but that is a data decision, not a code one. `Legacy code_Spaceship.txt` is still absent from the tree; the reference exists only as `80a10b1^:Spaceship.hta`. Region count stays 7.

### Entry #008
- **Timestamp:** 2026-11-07 05:10 UTC
- **Task:** owner: "I only see more on the second layer... those could be a little bit more entangled, too close to each other. The third layer has still not enough... how about we start to fill up the space with planets on all three layers. Procedurally generated."
- **Layer 1 (system chart):** the reference's local chart is five bodies - entry star, one world, a landmark, an outer world and a moon. New `PopulateLocalCharts()` in ChartExpansion adds ten to fifteen more per system through the reference's own `AddLocalBody()`, so klass strings, detail rows and scan pairs are the ones `addSystemBody()` writes: worlds take the `ORBITAL_WORLDS` profiles in turn (subtype, label, atmosphere, scan, locked line), moons take the reference's satellite wording, and a belt or a derelict appears for most systems. Placement is three fixed ellipses (92/146/200 px, 3/4/5 slots per ring) with a small jitter and a 34 px collision test against the reference's own five, so the chart reads as a system rather than a scatter. Deterministic: the seed is an FNV-1a hash of the system id, not an index, so adding a system elsewhere never moves this one's sky. Result: 12-21 local bodies per system, 3637 bodies in total (was 1013).
- **System summaries say the truth:** `Charted bodies` and `Navigable` are rewritten to the count the chart actually holds (through `BodySystem()`, so Kessler's six authored bodies count), because a panel claiming "5 local destinations" over a chart drawing eighteen is the same class of lie as a filter.
- **Layer 2 (sector):** board widened from 2x to 3x the authored 638x278 (now 1914x834) and the minimum system spacing from 44 to 66 px - the owner's "too close to each other". The authored systems are still only translated (by +638/+278), so every authored crossing costs what it always did. Lanes went from two to three nearest neighbours per generated system: 359 lanes (was 255), all same-region like the authored ones. The lane web is now also painted as lines (1 px, alpha 0.34; 0.7 for the lanes touching the ship or the target) - at this width a lane drawn as sparse dots was invisible, and the web is half of what the scale is for.
- **Layer 3 (galaxy):** every system is now placed inside its region's cluster on the galaxy board (`PlaceGalaxyDots()`, stored in `Catalog::galaxyPositions`): a square-root spiral ranked by order in the region, radius 3.5 + 7.6*sqrt(k), y squashed by 0.62 to match the chart's ellipse look. The galaxy scale therefore draws 7 regions **plus 202 stars** (was 7), at size 10 instead of 18, clickable like anything else. Region labels outrank stars there (`ChartLabelPriority` now ranks region above in-range), and stars are not labelled on that scale at all - the names would bury the seven that are the map. The clusters first rendered as seven doughnuts (the spiral started at radius 18); the hole is gone.
- **Verification:** `cmake --build build` clean; `ctest` 138/138 (`ui.gui-syntax` skipped here, run by hand: `Draw.cpp` clean under -Wall -Wextra -Wpedantic); `tools/check-draw.sh` PASS; console voyage unchanged; all three scales rendered from the recorded painter primitives and inspected (crop of the sector chart confirms the lane lines and the wider spacing).
- **Tests updated (not weakened):** two cases had the number 8 hard-coded as "Kessler's bodies" and now count the catalogue; the galaxy case asserted 7 bodies and now asserts regions + systems with a star at size 10; the expansion case checks >= 15 local bodies per system, that each system's `Charted bodies` row equals its real count, that `galaxyPositions` covers every system, stays on the board and never repeats a point, and that the authored shift is exactly +638/+278.
- **Legacy file:** the owner restored `Legacy code_Spaceship.txt` on this branch (commit 556bc01). `git hash-object` on it is `f7c62f0`, byte-identical to the copy Entry #007's generator was transcribed from (`git show 80a10b1^:Spaceship.hta`), and all 47 `STAR_CATALOG` ids in it are present in the port. So the transcription's source is now in the tree rather than only in history, and AGENTS.md rule 2 has a file again. Read only; not touched.
- **Still not done / owner's call:** `assets/data/chart.json` remains the frozen export the loader reads; regions stay at seven (worldHistory, wiki and economy name them); no new assets.

### Entry #009
- **Timestamp:** 2026-11-07 06:05 UTC
- **Task:** owner: (1) more zoom in and out, (2) lines not so thick, entangle layers 2 and 3 more, (3) is any of this procedural - do stations generate randomly?
- **Zoom:** the ladder came straight from the export (0.6..2.0, step 0.2 - eight steps), which was fine on a 638x278 board and useless on the 3x grown one. `ExpandChart()` now sets the ladder on the grown boards in code (`kZoomMin 0.3`, `kZoomMax 4.0`, `kZoomStep 0.2` - 19 steps each way), so `assets/data/chart.json` and the Phase 0 data gate still see the reference's numbers. The local chart keeps a shorter ladder (`kSystemZoomMin 0.6`, `kSystemZoomMax 2.5`): its bodies sit on rings out to 200 px, so at 4x the visible window is 80 board px and lands in the hole at the middle of the system - measured, not guessed (`0 of 18 bodies inside the box at 4x`, `18 of 18 at 2.1x`). Pan leash now scales with zoom (`round(vw * 0.5 * zoom)`), because a half-viewport leash pinned a 4x view to the middle of the board.
- **Lanes thinner:** `DrawLineEx` width 1.6/1.0 -> 1.2/0.7, alpha 0.7/0.34 -> 0.62/0.26. They are hairlines now, still lit for the lanes touching the ship or the target.
- **Layers 2 and 3 entangled:** the sector pass links each generated system to its four nearest neighbours in its own region (was three): 462 lanes over 202 systems, average degree 4.6. `EntangleGalaxy()` joins the nearest regions until every region has three lanes of its own: the authored galaxy had 9 links for 7 regions (a chain, two regions hanging off one line), now 12, every region degree 3+ (`gal-core` 6).
- **Question 3, answered in code terms:** the chart is procedurally generated in two separate senses, and it is worth keeping them apart.
  - *Seeded placement and selection* - random, but reproducible: the 154 generated systems' positions, names and trade profiles come from xorshift64* seeded `0x5350414345534849`; each system's planets, moons, belts and wrecks come from an FNV-1a hash of its id. Same seed, same sky, every load and every save.
  - *Station presence* - **not** random, and not a dice roll: a station exists exactly where a row of `STAR_CATALOG` names a trade profile, and the reference builds it in `buildGalaxyContent()` (station landmark + `PORTS[id]` with that profile's prices). 47 authored rows and 154 generated rows name profiles; 124 stations/ports result. `PopulateLocalCharts()` never adds a station - only worlds, moons, one belt or wreck per system. So "does a station generate randomly" = no: the *row* is chosen by the seeded RNG, the station follows from the row's profile exactly as it does for the authored 47. Adding stations to the local-chart pass would mean inventing ports (price baskets, trade balance), which is a data decision and is left to the owner.
- **Verification:** clean configure+build; `ctest` 138/138 (`ui.gui-syntax`, `ui.imgui-close-input`, `ui.native-draw-recorder` skipped - GUI not configured in this sandbox build); `tools/check-draw.sh` PASS and `Draw.cpp` clean under -Wall -Wextra -Wpedantic (run with the pinned raylib/imgui/rlImGui sources); zoom and pan checked numerically per layer at 0.3/1/2/4x.
- **Tests updated (not weakened):** the zoom-ladder case now asserts the grown ends and the local chart's own range, and checks that the sector ladder is the wide one.

### Entry #010
- **Timestamp:** 2026-11-07 15:25 UTC
- **Task:** Expand the navigator map procedurally across planets, star systems, and galactic regions/galaxies; make huge maps reachable, navigable, and CPU-light without frame lag; and tie procedural events directly into map exploration.
- **Baseline audit:** On `HEAD` (`1158d5f`), `ctest` had 3 failing cases (`every system is reachable on a full tank` failing at `dist == 137 > 100`, `the zoom ladder is clamped` failing on `kZoomMax == 8.0`, and `the charted galaxy is grown once` failing on `kSectorBoardScale == 6.0` vs `+638/+278`), plus `Draw.cpp` called `BuildNavScreen` twice per frame (`BuildTabScreen` + `BuildNavScreen`) with linear scans across bodies/systems/lanes.
- **O(1) catalogue indexing (`Catalog.h`, `Catalog.cpp`, `Navigation.cpp`, `Economy.cpp`):** Added hash-indexed lookups (`FindBody`, `FindSystem`, `FindRegion`, `FindPort`, `BodiesInSystem`, `SystemsInRegion`) with automatic staleness rebuilds. Replaced `O(N)`/`O(N^2)` linear scans in `FindBodyById`, `CurBodies`, `MetricsFor`, `CommissionOffer`, and `NavScreen.cpp`.
- **Procedural galaxies, territories, and multi-ring systems (`ChartExpansion.h`, `ChartExpansion.cpp`):**
  - Layer 3 (`galaxy`): widened galaxy board to `4x` (`2552x1112`), translated the 7 authored regions to its centre (preserving authored inter-region distances), and generated 17 outer galactic regions/clusters (`24` total) with regional `worldHistory` + traffic reports, spiral `galaxyPositions`, and `EntangleGalaxy()` lanes.
  - Layer 2 (`sector`): kept `6x` (`3828x1668`) board, translated authored systems to the core, and clustered each region's `24` procedural systems (`624` total systems) around its regional territory anchor so intra-region jumps remain coherent (`<= 45` actions/fuel); added long-baseline slipstream compression strictly above `px > 1050` so authored crossings match HTA 1:1 while every outer-galaxy jump satisfies `0 < dist <= 100`; updated `PanMap` to span `max(vw, w)` and `ViewSector` to centre the sector camera on the selected galaxy's star cluster.
  - Layer 1 (`system`): replaced 3-ring local placement with 5 elliptical orbital bands, moons in close resonance around host worlds, asteroid belts, anomalies/beacons, and working secondary orbital outposts (`PORTS` entries with `PORT_PRICES` baskets), yielding 16-24 bodies per system (~12,000+ local bodies) with zero `Catalog::Validate()` errors.
- **Map-tied procedural events (`ChartExpansion.cpp`, `Incidents.cpp`, `Navigation.cpp`, `SaveSystem.cpp`):** Registered 6 map-driven `IncidentType`s (`every = 0`: `map-ion-squall`, `map-derelict-ping`, `map-grav-shear`, `map-cryo-spore`, `map-beacon-ghost`, `map-pirate-toll`), wired `SpawnMapIncident`, `ResolveMapArrivalEvents`, and `ResolveMapScanEvents` for procedural jumps, anomalies, salvage caches, ancient nav buoys, mineral seams, and fuel skimmer depots, and taught `ValidSave` to validate against an expanded catalogue view when handed an unexpanded `Catalog`.
- **Zero-lag navigator rendering (`NavScreen.cpp`, `Draw.h`, `Draw.cpp`):** Removed the double `BuildNavScreen` call per frame, added dirty-keyed `EnsureNavScreen` caching in `ShellView` so idle 60 FPS frames perform zero chart rebuilds, scaled wide-board route dot spacing, and added viewport frustum culling + galaxy-star LOD for clusters, routes, lane lines, bodies, hit boxes, and labels (`labelBudget` raised to 24 on `galaxy`).
- **Verification:** `cmake --build build` clean; `ctest` **141/141 PASS** (`spaceship_tests` 136 cases / 797,748 assertions, `spaceship_audio_tests`, `ui.gui-syntax`, `ui.imgui-close-input`, `ui.native-draw-recorder`); 6 full chart builds across all 3 scales complete in ~8 ms after voyage init.

### Entry #011
- **Timestamp:** 2026-11-07 16:40 UTC
- **Task:** (1) Deeper zoom on Layer 2 (`sector`), visual sector territory differentiation on Layer 2, and vivid red player position beacon across map layers; (2) Staged sensor upgrades (`scanner` / `scanner2`) unlocking `scanLocked` tiers and deeper-region remote scanning; (3) Planetary ore extraction, moon mining, gas-giant skimming, and debris/wreck looting with dynamic yield/hazard events.
- **HTA audit finding:** Checked `Legacy code_Spaceship.txt` (`scanReadout` lines 9217-9226, Shipyard `upgrades` lines 9594-9609) and `Economy.cpp:379`: both HTA and the C++ port populated `scanLocked` on bodies, but `upgrade:scanner` (`600 sh`) only set `scanCost = 3` and never unlocked `scanLocked` or cross-system/deep-region scans.
- **Sector clarity, 10.0x zoom & vivid red player reticle (`ChartExpansion.h`, `Draw.h`, `Draw.cpp`):**
  - Raised `kZoomMax` from `4.0` to `10.0` on the wide Sector/Galaxy boards and scaled Sector `labelBudget` dynamically with zoom (`26..96`).
  - Added Layer 2 (`sector`) territory contours (`DrawEllipse` + `DrawEllipseLines`) in a 6-color cartographic palette around each of the 24 sector star clusters, sector-tinted intra-cluster lanes vs amber inter-sector bridges, and non-overlapping sector title badges (`TypePx(9)` >= 18px floor) seeded into `takenLabels`.
  - Replaced the faint sage ship ring drawn beneath stars with a vivid crimson (`#DC2626`) dual-ring + 4-tick compass reticle rendered on top of stars (with soft red under-glow and `max(10px, 13.5 * bodyScale)` click hitbox).
- **Staged sensor upgrades (`Simulation.h`, `Navigation.cpp`, `Economy.cpp`, `StationScreen.cpp`, `NavScreen.cpp`, `SaveSystem.cpp`):**
  - Preserved Tier 0 HTA parity (`"Out of sensor range"` and `"sensors insufficient"`).
  - Tier 1 (`upgrade:scanner`, `600 sh`, `scanCost = 3`): unlocks the first `scanLocked` entry (`"upgraded sensor lock"`), unlocks the 3rd deep-vein extraction pass on worlds, and enables remote deep-scanning of systems and bodies within the ship's current galactic region.
  - Tier 2 (`upgrade:scanner2`, `1,100 sh`, staged behind `scanner`, `scanCost = 2`): unlocks all `scanLocked` entries (`"deep-array verified"`), extends `CanScan` across all deeper regions/galaxies (`system`, `region`, and local bodies), and grants a `+25%` deep-array survey value bonus.
- **Planetary ore, moon mining, gas skimming & debris/wreck looting (`ChartExpansion.cpp`, `Simulation.h`, `Navigation.cpp`, `Economy.cpp`, `Tasks.cpp`, `SaveSystem.cpp`, `Widgets.cpp`, `NavScreen.cpp`):**
  - Added ongoing task type `"extract"` (`CargoReserved()` + `ValidSave()` hold accounting, power requirement in `AdvanceTasks()`, and `PressNavAction`/`PressProgressButton` anti-spam routing).
  - Enabled parked undocked extraction (`2` passes per body, `3` when scanned or `SensorTier() >= 1`) across rocky/lava planets (`Bulk metal` / `Components`), cryo/ocean worlds & moons (`Water` / `Relics` + rations), gas giants (`0 m3` hold skim yielding `+fuel%` + `fuelCells`), asteroid/debris belts (`Salvage` + `CommissionProgress("salvage")`), and derelict sites (`Salvage` / `Components` / `Relics` / `Unregistered` + chits/kits), with deterministic jackpot veins and extraction hazard incidents (`map-derelict-ping`, `map-cryo-spore`, `map-ion-squall`, `map-pirate-toll`).
- **Verification:** `cmake --build build` clean; `ctest` **143/143 PASS** (`spaceship_tests` 138 cases / 797,874 assertions, `spaceship_audio_tests`, `ui.gui-syntax`, `ui.imgui-close-input`, `ui.native-draw-recorder`). `Legacy code_Spaceship.txt`, `assets/`, and `rescue/` untouched.

### Entry #012
- **Timestamp:** 2026-11-07 17:10 UTC
- **Task:** Minimal fine-tune to extraction/looting: (1) loot possible only once per body, (2) loot cooldown set to `5` actions, and (3) lootable yield included in `Scan Data` when scanned.
- **Single-use loot (`Navigation.cpp`, `NavScreen.cpp`):**
  - Changed `Simulation::ExtractionMaxPasses` to `1` for all extractable bodies (`planet`, `moon`, `belt`, `site`), with `ExtractionPassesUsed` returning `1` once extracted and `Deposits` displaying `"Available"` vs `"Depleted"`.
- **5-action loot cooldown (`Navigation.cpp`, `NavScreen.cpp`):**
  - Switched `"extract"` / `"extract-busy"` `BuildProgressButton` category from `"space"` (`15` actions) to `"routine"` (`5` actions) and set `Cooldown{"routine", 5.0}` on `progress:extract` in `Simulation::Acknowledge` when hauling loot aboard.
- **Scanned loot readout (`Simulation.h`, `Navigation.cpp`, `NavScreen.cpp`):**
  - Added `Simulation::ExtractionScanSummary(const Body&)` matching the deterministic yield table in `FinishExtraction` (`pass = 1`) and appended the `"Lootable: ..."` line (or `"Lootable: Depleted (already looted)."`) to `ScanReadout` once a body has been scanned.
- **Tests (`nav_tests.cpp`):**
  - Updated the extraction test case to assert `ExtractionMaxPasses == 1`, `CooldownRemaining == 5.0` at start and upon haul completion, single-pass depletion, and presence of `"Lootable:"` / `"Lootable: Depleted"` in `Scan Data`.
- **Verification:** `cmake --build build -j4` clean; `ctest` **143/143 PASS** (`spaceship_tests` 138 cases / 797,864 assertions, `spaceship_audio_tests`, `ui.gui-syntax`, `ui.imgui-close-input`, `ui.native-draw-recorder`). All other files untouched.

### Entry #013
- **Timestamp:** 2026-11-07 19:15 UTC
- **Task:** owner's spec - rebuild the spacestation as a 3-column progressive master -> list -> detail/action layout, plus money-only (0-action) station buying. No source file was edited before the AGENTS.md comprehension answers were given in the reply; the owner's in-session instruction was taken as source authority #1, and no comment anywhere claims approval, verification or a request.
- **Layout (`Widgets.cpp`, Station case):** left column = the five category cards (`Market - purse N sh`, `Sell cargo and survey data`, `Mission board`, `Shipyard and crew`, `Tavern / Bar`), free to open; middle = only the open card's items, free to pick; right = the inspector for the one picked item (every detail line, port price, hold/berth/rack requirement, objective and reward, candidate traits, patron info) plus its single commit button. `panelColumns` went from `{0,1,1,2,2}` to `{0,1,2}`.
- **Selection (`StationScreen.h/.cpp`, `Draw.h/.cpp`, `Widgets.h/.cpp`):** `StationSelection{category,item}` is owned by `ShellView::stationSelection_`, handed to `BuildTabScreen` as an in/out pointer, reset when the active tab changes, and normalised every frame by `NormalizeStationSelection` so a sold lot or a stale card can never stay picked. Choosing is not saved: it costs nothing. `CommandRowView.selected` marks the open card and the picked item; `PaintPanels` draws them with a coffee bar and a `coffeeLight` border.
- **Money-only (`Action.cpp`, `Economy.cpp`, `Ships.cpp`):** new `Simulation::FreeCommit(detail)` logs the feed line and checks commendations without touching `actionCounter_`, `TickWorld` or any countdown. `BuySupply`, `BuyGood`, `BuyUpgrade` (bay, bay2, drive, rack, scanner, scanner2, hull), `HireCrew`, `BuyRoom` and `BuyLargerShip` commit through it; the station's own `hull-service` branch does too. `SellGood`, `SellSurvey`, contract/commission accept-collect-abandon, `TalkToPatron` and `Undock` still spend exactly one action through `PerformAction`.
- **No cooldown on this window:** `PressStationAction` dispatches directly instead of through `CommitActionButton`. A second press is refused by the state the first press changed (lot gone, contract taken, refit owned, hand hired, patron spoken to, berth left) or by the purse/hold/rack/berth gate - never by a lock. `StationActionCategory` is kept because the audit still reads the station's labels; the station no longer uses it.
- **Greys out once and for ever:** owned refits and rooms, the bought Kestrel and hired candidates carry cost `done` with commit `Owned` / `Hired`; hull service greys at 100% hull or a short purse and says which; an accepted contract keeps its item on the board with the button greyed (`Accepted`), a paid one reads `Completed`.
- **Tavern / Bar:** the people the reference already seats there - the trader whose notice was the panel's tip, and every candidate waiting at this port who has not signed aboard. `Talk` costs one action once (`GameState::talked`, written to the save and read as an optional key so older saves still load), and only then does the inspector reveal their words: the port's own tip plus this counter's best-paid and cheapest good for the trader, and the candidate's authored traits/skill/role/hobby/homeworld plus the two `worldHistory` lines of the region for a patron. No sentence is composed; every line is data that already existed.
- **Deliberate deviations from `Legacy code_Spaceship.txt` (owner-ordered):** the HTA head line "Browse freely. Each transaction, contract or departure costs one action.", its five-panel stacking over three columns, the `commandRow` cooldown tiers at the station, the panel title "Tavern and departure", and one action per purchase. The undock row stays in the tavern card so departure remains reachable.
- **Known contradiction left untouched (owner's call):** the handbook entry for the larger ship still says the purchase "takes one action". That text is authored catalogue content (`assets/data`), not code, so it was not edited.
- **Tests:** `station_tests.cpp` gained the card->item->inspector walk (3 panels, `{0,1,2}`, `station-cat:` / `station-item:` ids, selected marks, reading a screen never moves the clock), the money-only market (two buys, 0 actions, no cooldown entry, `0 actions` badge), the `done`/greyed one-time rows, and a Tavern case (talk once = +1 action, the tip is absent before and present after, save/load carries the tally, a second press is refused). The cooldown regression case now asserts the station starts no lock; `spam_audit_tests.cpp` skips `row.free` in its sweep and its station case proves repeatability plus state-enforced single spend on a sell; `starter_tests.cpp` asserts the room purchase leaves the action count unchanged; `draw_probe.cpp` walks the three columns, buys twice for 0 actions, then spends one action on a sell so the green 11x2 HTA action-mark check still has an action to mark, and the purchase-confirmation block opens the yard card and the item before the commit button (now 0 actions).
- **Checks run:** `cmake` 4.4.4 (pip-installed; this sandbox had none) + ninja, headless configure; `cmake --build build --parallel 2` clean; `ctest` **145 tests: 142 passed, 3 skipped** (`ui.gui-syntax`, `ui.imgui-close-input`, `ui.native-draw-recorder` - no GUI configured here); `tools/check-gui-syntax.sh` run by hand with the pinned raylib 6.0 / imgui 1.92.7 / rlImGui sources vendored to `/tmp/deps`: `Draw.cpp`, `AudioDevice.cpp`, `main.cpp` clean under `-Wall -Wextra -Wpedantic` (the one warning printed is the pre-existing unused `p` in `PaintTaskScreen`).
- **Checks NOT run here:** linking and running the real client, and `ui.native-draw-recorder` - no OpenGL/X11/ALSA development packages and no display in this sandbox. The drawn station layout is therefore not visually verified; CI's `test-linux` job is the first real GUI build and the first run of the rewritten draw probe.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, `tests/*.test.cjs`, `tests/parity/`, `AGENTS.md`, `CMakeLists.txt`, `build.sh`, and every other screen's action and cooldown rules.

### Entry #014
- **Timestamp:** 2026-11-07 19:58 UTC
- **Task:** owner fine-tuned the station against a screenshot of the built client: columns 1 and 2 must be plain list lines (no white button boxes, no cost badges), the mission board must not repeat contract descriptions in the middle column, and the commit control must be one simple button with no badge.
- **Rendering (`Widgets.h/.cpp`, `Draw.cpp`):** `CommandRowView` gained `plain` (a list line: no fill, no border, only the coffee bar + panel3 wash marking the open card or picked item, and still hit-tested) and `hideCost` (no cost badge anywhere; `measureRow` drops the badge width and the paint skips it entirely). Cards and items are now `plain` + `hideCost`; the inspector's single commit keeps its button face but no badge. Item lines read muted when their commit is greyed, ink otherwise.
- **Model (`StationScreen.cpp`):** the middle column no longer repeats each contract's note/reward/status - that text is exactly what the inspector shows for the picked contract; headings ("Repeatable commissions", "Regular contracts", "Optional rooms", "Hiring hall") stay as structure. Inspector cost field reads "Shiplets only" / "One action" instead of "0 actions" / "1 action".
- **Price words:** the ship and deep-array commit buttons repeat the reference's own comma-formatted price words ("Buy - 4,000 sh", "Buy - 1,100 sh") so a button and its list line can never disagree with each other; every other price stays on `js::NumToString`, as before.
- **Tests:** `station_tests.cpp` asserts the plain/hideCost flags and reads the contract note, reward and status from the picked row's details while checking the middle column no longer carries the note; `spam_audit_tests.cpp` reads the money-only cost from `cost`/`hideCost` instead of a display badge; `draw_probe.cpp` block B now captures the card's coordinates before the frame refills `texts` (the old code read a stale iterator) and drives the purchase confirmation through card -> auto-picked item -> commit.
- **Checks run:** `cmake --build build` clean; `ctest` **145/145 passed** including `ui.native-draw-recorder`, which this sandbox can now actually run (CPU recorder: `tools/check-draw.sh` with the pinned raylib/imgui/rlImGui sources linked into `build/gui-headers/`), so the three-column walk, the money-only double buy, the one-action sell and the pinned purchase confirmation are verified against the real painter; `tools/check-gui-syntax.sh` clean for `Draw.cpp`, `AudioDevice.cpp`, `main.cpp`.
- **Checks not run here:** the linked client on a real display (no X11/OpenGL in the sandbox); the visual look of the plain rows on the owner's machine is confirmed only by their next screenshot.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, every other screen's row styling (rooms, stores, system screens keep their buttons and badges), cooldown rules outside the station.

### Entry #015
- **Timestamp:** 2026-11-07 20:40 UTC
- **Task:** owner's ruling after the merge: docking must never block a jump, and leaving a station must be one click. Implemented on `arena/17e54f92-spaceship` as the minimal change, no more.
- **`Navigation.cpp` `StartTravel()`:** the `state_.docked` refusal is gone. A ship that commits a crossing from a berth leaves the berth inside the same departure: `docked = false`, the `location` info row follows, no second action, no separate undock press. Engine and energy gates stay.
- **`NavScreen.cpp`:** the travel button's enable test and the band note lost the docked branch; "Undock at the station before plotting a crossing." is no longer printed because it is no longer true. The `Dock at <port>` row still appears only while undocked.
- **`Widgets.cpp`:** while docked, the station's action band carries a pinned `Undock` progress button (`PressProgressButton` dispatches `undock` to `sim.Undock()`, one action, as before), so departure never depends on opening a card. The three columns are untouched.
- **`nav_tests.cpp`:** new case "a docked ship plots and starts its crossing without undocking first": cross to Tolme, dock, select Kessler, the travel button reads Idle while docked, committing spends exactly one action, clears `docked` and starts the crossing.
- **Deliberate deviation from the legacy reference:** `Spaceship.hta` gated travel on being undocked (line 5964 and `startTravel`); the owner overruled that gate explicitly, and this entry is the record of the instruction and of the deviation.
- **Checks run in this sandbox:** none completed. The sandbox was recreated mid-turn with no `build/` and no cmake; cmake 4.4.4 was reinstalled and the configure+build+ctest chain was started but aborted by the turn limit before it finished. The patch is therefore compiled-but-unverified here.
- **Checks that must run before this is trusted:** headless `cmake --build` + `ctest` (the new nav case plus the existing dock/undock and opening-voyage cases), and CI's `test-linux` for the client and the draw probe.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, the station's three-column layout from PR #28, every cooldown rule, `AGENTS.md`.

### Entry #016
- **Timestamp:** 2026-11-07 21:05 UTC
- **Task:** open the PR for the no-block ruling (PR #29) and close the verification gap the previous turn left open.
- **PR:** #29 from `arena/17e54f92-spaceship` to `main`: StartTravel without the docked gate, navigator button/note un-gated, pinned one-click Undock in the station band, new nav regression case.
- **Verification completed this reply:** fresh headless configure (cmake 4.4.4 + ninja), `cmake --build build` clean, `ctest` **146/146 passed**, and `tools/check-draw.sh` (native draw recorder, CPU-only) run against the pinned raylib 6.0 / imgui 1.92.7 / rlImGui sources vendored to /tmp/deps and linked into build/gui-headers: PASS, including the station card/item/inspector walk and the pinned action bands now that the docked station carries an Undock button.
- **Test fix found by the run:** the new nav case called `sim.Dock()` after arriving at Tolme, but arriving at a port already berths the ship (`docked` true on arrival, as starter_tests rely on), so `Dock()` correctly refused a second berth. The case now asserts the arrival berth instead of forcing one; the ruling itself was never at fault.
- **Behaviour confirmed by the passing case:** docked at Tolme, Kessler selected, the travel button reads Idle (not Off), one press spends one action, clears `docked` and starts the crossing - the berth is no longer a cage.
- **Checks still owed:** CI `test-linux` on PR #29 is the first real client build of this branch (no X11/OpenGL/display in this sandbox); the draw recorder here is the CPU painter, not a window.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, the three-column station layout, every cooldown rule outside this change, `AGENTS.md`.

### Entry #017
- **Timestamp:** 2026-11-07 23:22 UTC
- **Task:** Tune the cockpit visuals to the supplied mockup: warmer mocha colors, fixed ship/home identity, smaller gauges, ship-click deck-plan access, fewer instructions, navigation/event cards below the info rail, and the feed at the bottom.
- **Reference trace:** Read the legacy shell, status rail, home-screen build order, navigation strip, feed, and sidebar event list before changing the C++ presentation. The historical file was not edited.
- **Palette (`Theme.cpp`):** Warmed the classic theme's panel, chrome, border, ink, and accent roles; changed the content surface from white to warm cream.
- **Top bar/layout (`Layout.h/.cpp`, `Draw.cpp`):** Reserved the fixed left 165px slot for the ship identity, started task tabs to its right, and removed the right-hand Home slot while retaining the two tab-scroll arrows.
- **Home control:** Draws `SHIP` plus the current vessel name in the fixed top-left slot; clicking that slot returns to Home regardless of the open task tabs.
- **Status rail (`IdleScreen.cpp`, `Draw.cpp`):** Reduced gauge spacing and height while retaining the configured text-size floor and the original stat values/units.
- **Ship access (`Draw.cpp`):** Removed the `Deck plan / crew` and `Exterior view` buttons; clicking the exterior ship image enters the deck view, and clicking an unused deck-stage area returns to the exterior view. Room callouts and crew interaction remain separate.
- **Home content order:** Removed the ship-stage left/right-click instruction; placed the three information cards directly below the ship, with Navigation beneath the left card and live incident cards beneath the other two.
- **Events:** Home event cards use current `sim.Events()` data and open the existing event task when clicked; the sidebar Events tree remains available.
- **Activity feed:** The feed is the final Home panel and expands to the lower content edge with an 8px inset; longer information values shift the lower panels down before feed height is recalculated.
- **Regression coverage (`draw_probe.cpp`):** Updated fixed-home and tab coordinates, tested clicking the ship instead of a removed view button, and asserted compact gauges, navigation/events placement, and the feed's bottom alignment.
- **Checks:** `./build.sh --headless` completed; `ctest --test-dir build --output-on-failure` passed 125/125, including GUI syntax, ImGui input, and the native draw recorder after pinned headers were supplied from `/tmp/deps`.
- **Additional UI check:** `tools/check-gui-syntax.sh` passed for Draw.cpp, AudioDevice.cpp, and main.cpp under `-Wall -Wextra -Wpedantic`; it reported one pre-existing unused `p` warning in `PaintTaskScreen`.
- **Limits:** No windowed GUI was linked or launched, so this verifies the painter's recorded geometry/input, not an on-screen screenshot or full visual/gameplay parity.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, simulation behavior, and task/event rules. The headless `build/` output and downloaded GUI headers remain ignored/outside tracked source.
- **Final rerun:** After the small-window tab-strip bound and updated visual assertions, `ctest --test-dir build --output-on-failure` again passed 125/125, including GUI syntax, ImGui input, and the native draw recorder.

### Entry #018
- **Timestamp:** 2026-11-07 23:27 UTC
- **Task:** Create a pull request for the Home cockpit visual tuning.
- **Branch:** `arena/cb265de8-spaceship`, pushed to `origin`; no branch switch or rename.
- **Commit:** `94490bb` — `Tune the home cockpit layout and mocha palette`.
- **Pull request:** #30, `arena/cb265de8-spaceship` to `main`, open at https://github.com/Aquaslopper/SPACESHIP/pull/30.
- **PR title:** `Tune the home cockpit layout and mocha palette`.
- **PR contents:** Summary of the mocha palette, fixed ship home control, compact gauges, click-to-switch ship view, home content order, and bottom-anchored feed.
- **PR verification notes:** Headless build and final 125/125 CTest result, including GUI syntax, ImGui input, and native draw recorder.
- **PR description update:** `gh pr edit` hit the repository's classic-project GraphQL deprecation error; updated the description successfully with `gh api` PATCH.
- **Visual verification limit:** No windowed GUI was launched; the native draw recorder checks recorded drawing and input, not a rendered screenshot.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, and simulation rules.

### Entry #019
- **Timestamp:** 2026-11-07 23:49 UTC
- **Task:** Refine the Home screen against the latest side-by-side screenshots: center the Wren in a taller stage, spread room callouts around it, keep the feed compact at the bottom, and show two event cards.
- **Screenshot reading:** The desired composition uses callouts at the upper/middle/lower ship positions, info panels around y=666 in the supplied 970px capture, two separate event slots, and an approximately 152px feed panel at the bottom.
- **Stage (`Layout.h`, `IdleScreen.cpp`, `Draw.cpp`):** Added a 410px Home stage minimum, moved its top below the compact gauges, and centered the exterior ship image at 40% of the stage height.
- **Callouts (`Draw.cpp`):** For the standard three rows, placed labels at normalized stage offsets 0, 0.45, and 0.69 on both sides. When more rooms require additional rows, the exterior stage grows to keep the labels from colliding.
- **Event cards (`IdleScreen.cpp`):** Keep two separate columns beneath the second and third info cards. Fill them with live incidents, or independent "No active events." cards when slots are empty; additional incidents wrap to more rows.
- **Feed (`Layout.h`, `IdleScreen.cpp`, `Draw.cpp`):** Anchor the feed 8px from the content bottom and cap its Home height at 152px (without changing the general feed-height helper).
- **Regression coverage (`draw_probe.cpp`):** Check two distinct event headings, the feed height ceiling and bottom anchor, and exterior callout interactions after the stage reflow.
- **Checks:** Rebuilt the headless core; `tools/check-draw.sh` passed; final `ctest --test-dir build --output-on-failure` passed 125/125 including GUI syntax, ImGui input, and native draw recorder.
- **Limit:** No windowed GUI was launched; the capture layout is checked with the CPU painter/input recorder, not a fresh screenshot.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, and gameplay logic. The changes are follow-up work for the existing open PR #30.
- **PR follow-up:** Pushed commit `931e319` to `arena/cb265de8-spaceship` and updated PR #30's summary and verification section to include the screenshot-alignment changes and final 125/125 test result.

### Entry #020
- **Timestamp:** 2026-11-08 00:05 UTC
- **Task:** Follow the owner's latest Home screenshot corrections on the existing cockpit PR #30.
- **Docking as an event (`IdleScreen.h/.cpp`, `Draw.cpp`):** Removed the separate Docked strip and its click path. Berth status and body name now occupy an event card; left/right clicks route to the existing station briefing/task.
- **Events:** Every card carries an explicit heading; empty slots remain two independent Events cards, and live incidents keep their existing event-task route.
- **Six-panel/feed relationship (`IdleScreen.cpp`, `Draw.cpp`):** The Home stage minimum is now 520px. Navigation and event cards are shifted lower into available space so the lower group sits directly above the bottom-anchored Activity Feed without an empty band.
- **Room callouts (`Draw.cpp`):** The three-row left and right label curves differ, removing the previous mirror-aligned rhythm; dense room layouts retain their existing extra-stage spacing.
- **Regression checks (`draw_probe.cpp`):** Added stage/panel grouping, zero-gap feed adjacency, and docked-event placement checks; the feed is verified not to overlap the dock/event cards.
- **Build:** `PATH="/tmp/spaceship-test-venv/bin:$PATH" cmake --build build --parallel 2` passed.
- **Draw recorder:** `./tools/check-draw.sh` passed against the real `Draw.cpp` with the pinned raylib 6.0 / Dear ImGui 1.92.7 / rlImGui sources.
- **Full tests:** `ctest --test-dir build --output-on-failure` passed **125/125**, including GUI syntax, ImGui input, and native draw recorder; `git diff --check` passed.
- **Visual limit:** No windowed client or new screenshot was captured; these checks verify source compilation and recorded painter/input geometry, not the rendered cockpit on a display.
- **Scope:** Follow-up to the open PR #30 on `arena/cb265de8-spaceship`; `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, simulation behavior, and gameplay rules remain untouched.

### Entry #021
- **Timestamp:** 2026-11-08 00:13 UTC
- **Task:** Correct the Home screenshot regression reported by the owner after commit `880fc5f`.
- **Root cause:** The Activity Feed heading was painted 13px above its panel. With the requested zero gap, the heading fell over the Navigation card's bottom text/border.
- **Feed header (`Draw.cpp`):** Paint the feed surface first and place “Activity Feed” inside its upper-left area, so the heading is contained by its own panel instead of floating in the preceding row.
- **Feed content:** Start the log rows and scissor viewport beneath the heading; reserve the header space when calculating visible rows and scrollbar thumb geometry.
- **Feed dragging:** Align thumb-drag travel and offset with the reduced content track below the header.
- **Regression test (`draw_probe.cpp`):** Assert the Activity Feed heading lies inside the feed surface, while retaining bottom-anchor and no-gap checks.
- **Build:** `PATH="/tmp/spaceship-test-venv/bin:$PATH" cmake --build build --parallel 2` passed (core already up to date; draw probe recompiles the real painter).
- **Draw recorder:** `./tools/check-draw.sh` passed, including feed wheel/drag/wrap/clamp coverage.
- **Full suite:** `ctest --test-dir build --output-on-failure` passed **125/125**; `git diff --check` passed.
- **Visual limit:** The screenshot correction is verified by the CPU draw recorder; no windowed GUI screenshot was captured in this environment.
- **Scope:** Small follow-up to PR #30 on `arena/cb265de8-spaceship`; no palette, gameplay, authored assets, or historical reference files changed.

### Entry #022
- **Timestamp:** 2026-11-08 00:18 UTC
- **Task:** Match the room-callout placement in the supplied mockup after the owner pointed out that the labels still formed aligned columns.
- **Correction to the prior interpretation:** The earlier tweak changed only row heights, leaving every left label at one x/width and every right label at another. The mockup also steps the boxes horizontally inward and varies their widths.
- **Three-row callouts (`Draw.cpp`):** Added per-side normalized x-insets, widths, and y positions for the standard three rows, following the screenshot's distinct left/right stagger rather than mirroring the sides.
- **Left sequence:** Engine Room remains at the outer edge; O2 Filter Room steps inward; Cargo Hold moves substantially inward and narrows.
- **Right sequence:** Crew Quarters stays at the outer edge; Mess Hall shifts slightly inward; Bridge moves substantially inward and narrows.
- **Responsive text:** Callout labels wrap against each resulting box width; the row height is measured from the widest wrapped label.
- **Dense layouts:** More-than-three-row ships retain their existing spacing rule, avoiding a change to the larger-room layout.
- **Regression coverage (`draw_probe.cpp`):** Starter Wren probe now asserts the mockup's left/right x progression and individually staggered y positions.
- **Checks:** `cmake --build build --parallel 2`, `tools/check-draw.sh`, full `ctest` **125/125**, and `git diff --check` all passed.
- **Visual limit:** The recorder verifies rendered coordinates/order but no separate windowed screenshot was captured here.
- **Scope:** Follow-up to open PR #30 on `arena/cb265de8-spaceship`; no data, authored art, palette, or gameplay changes.

### Entry #023
- **Timestamp:** 2026-11-08 00:31 UTC
- **Task:** Clean up the cockpit after the owner reported room-card overlap, uneven six-card sizing, and cards touching each other.
- **Room callouts (`Draw.cpp`):** Reduced the three-row callout widths and staggered their left edges while reserving a 30px horizontal gutter from the ship-art bounds. O2 Filter Room and Cargo Hold can no longer intrude into the art rectangle.
- **Callout coverage (`draw_probe.cpp`):** Check the Wren art-to-panel clearance on both sides, plus the staggered coordinates.
- **Six-card grid (`IdleScreen.cpp`, `Layout.h`):** Replaced proportional/mixed-width placement with a strict three-column grid: three info cards above Navigation and two separate Events cards.
- **Equal sizing:** All six cards now use identical widths and a common height. The draw pass measures actual wrapped info, navigation, and event text, then grows all cards together when needed.
- **Gutters:** Added a consistent 12px gap between grid columns and rows and before Activity Feed, preventing touching borders while retaining the feed's bottom anchor.
- **Navigation text:** Wraps within its card and contributes to the shared card-height measurement instead of spilling or silently clipping.
- **Regression coverage:** Draw probe asserts equal card dimensions, uniform gutters, feed separation, callout staggering, and that room cards stay outside the ship-art bounds.
- **Build and draw checks:** `cmake --build build --parallel 2` and `tools/check-draw.sh` passed.
- **Full suite:** `ctest --test-dir build --output-on-failure` passed **125/125**; `git diff --check` passed.
- **Visual limit:** No windowed client screenshot was captured here; the native draw recorder verifies the painter's measured geometry, not a live display.
- **Scope:** Follow-up on PR #30, branch `arena/cb265de8-spaceship`; palette, authored assets, legacy materials, and simulation/gameplay behavior remain unchanged.

### Entry #024
- **Timestamp:** 2026-11-08 00:42 UTC
- **Task:** Close the remaining visual-sizing gap after the owner demanded a cleaner, non-chaotic Home layout.
- **Painter bug (`Draw.cpp`):** Although the prior model placed six equal-width cards, `PaintIdle` still subtracted 8px from the first two info-card fills as residue from the old layout. Removed that trim so the rendered surfaces match the grid geometry exactly.
- **Callout clearance:** Increased the standard three-row labels' reserved horizontal clearance to 30px from the ship-art rectangle while keeping their widths/left edges staggered; wrapping still contributes to row height.
- **Rendered-size test (`draw_probe.cpp`):** Compare the actual six recorded background rectangles, not just model boxes, and require identical painted width and height.
- **Art-overlap test:** The O2 Filter Room, Cargo Hold, Crew Quarters, and Bridge panels are checked against the recorded ship-art rectangle with a clearance margin.
- **Grid test:** Retains the three-column alignment, six equal model dimensions, 12px card gutters, and 12px Activity Feed separation assertions.
- **Checks:** `cmake --build build --parallel 2` passed; `tools/check-draw.sh` passed.
- **Full regression suite:** `ctest --test-dir build --output-on-failure` passed **125/125**, including GUI syntax, ImGui input, and native draw recorder.
- **Diff hygiene:** `git diff --check` passed.
- **Visual limit:** No windowed screenshot was captured in this environment; the new checks validate the real painter's recorded fills and coordinates.
- **Scope:** Follow-up on PR #30, branch `arena/cb265de8-spaceship`; no gameplay, palette, authored asset, or historical reference edits.

### Entry #025
- **Timestamp:** 2026-11-08 01:01 UTC
- **Task:** owner reported that another branch was merged and asked this branch to be updated to the current `main`. No code request.
- Fetched `origin/main`: ca02a4b -> 2fbf326 ("Merge pull request #30 from Aquaslopper/arena/cb265de8-spaceship"), 9 commits ahead of this branch's base (94490bb, 3c636ee, 931e319, 9648543, 880fc5f, be02d8e, db802a0, 0f0e61f, b893d97, 2fbf326).
- Incoming scope measured before touching anything: `cpp/tests/draw_probe.cpp` +170, `cpp/ui/Draw.cpp` +295/-? , `cpp/ui/Draw.h` -1, `cpp/ui/IdleScreen.cpp` 115, `cpp/ui/IdleScreen.h` 23, `cpp/ui/Layout.cpp` 19, `cpp/ui/Layout.h` 9, `cpp/ui/Theme.cpp` 20, `docs/LOGBOOK.md` +117. This branch was behind, not diverged, so a fast-forward was possible.
- Collision found: my own uncommitted Entry #017 in `docs/LOGBOOK.md` claimed a number that main had since taken (main's logbook now runs to #024). The entry was saved to `/tmp/my_entry_017.patch`, the working file was restored to HEAD, and only then was the branch fast-forwarded - no logbook entry was silently dropped or renumbered in the merge.
- Merge performed as `git merge --ff-only origin/main`; HEAD is now 2fbf326 and this branch carries every commit main had. Nothing was rewritten, no branch was created, no push has been made.
- **Verification after the merge (this is the point of the turn):** re-ran the full toolkit against the merged sources rather than trusting the pre-merge run. `cmake -S . -B build -G Ninja` configure, `cmake --build build --parallel 4` clean, `ctest --test-dir build --output-on-failure -j4` = **125/125 passed**, with `ui.gui-syntax` the single skip. `ui.native-draw-recorder` and `ui.imgui-close-input` both PASSED, so PR #30's new Home card/callout assertions and the painter changes are green on top of this branch.
- Because `ui.native-draw-recorder` now runs here (pinned raylib 6.0 / imgui 1.92.7 / rlImGui sources vendored into `build/gui-headers/`), the Draw.cpp changes from main are exercised by the CPU recorder in this sandbox, not only by CI.
- **Files changed in this reply:** `docs/LOGBOOK.md` only (this entry). No tracked file differed from main after the fast-forward.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, all of `cpp/`, all of `tests/`, `CMakeLists.txt`, `build.sh`, `AGENTS.md`, `.github/`.
- **Not committed or pushed:** the fast-forward moved the local branch; nothing has been pushed to `origin` and no PR was opened for this turn.

### Entry #026
- **Timestamp:** 2026-11-08 01:20 UTC
- **Task:** owner reported "crew hunger stays at 0 and there is no real indicator for mass hall", with the session terminating if unresolved. Traced both halves against `Legacy code_Spaceship.txt` before touching anything.
- **Defect 1 - the galley painted element IDs, not values (`cpp/ui/MessScreen.cpp`, `BuildMessMachine`).** The rack readout's `text` was the literal string `"mess-readout"`, each plate's name was `"mess-pname<k>"` and each clock's caption `"mess-psub<k>"`. `PaintStage` prints any `text` whose first character is not `'m'`... and these begin with `'m'`, which is the reference's sentinel for "filled at paint time" - but nothing in the port ever filled them, so `paintMess()`'s entire job was missing. The four per-crew clock fills were also built at width 0 and never widened.
- **Fix 1:** the stage now derives every live value from state at paint time, exactly as `paintMess()` does: rack fill width `round(520 * pct/100)`, its tone from the 60/25 thresholds in `#7f9161`/`#c08a3e`/`#b05a42`, the readout `"<rack> of <capacity> rations on the rack"`, each plate's real crew name, each clock width `round(118 * (hungerTick/MESS_EVERY))` with tone red at `hunger >= 70`, and the caption `"<MESS_EVERY - hungerTick> actions to next meal"`. Element ids are gone from the `text` field entirely.
- **Defect 2 - hunger appearing "stuck at 0" is faithful reference behaviour, not a bug.** Starting hunger is 15/22/28/41 and a successful meal subtracts 40. With a stocked rack every hand is pinned to `max(0, hunger-40) = 0` in one cycle, so 0 is the correct, expected reading. Verified by script: 26 actions on a stocked rack leave hunger 0/1/0/0 with the rack down 18->14; forcing `rack = 0` produces the one-time warning "The ration rack is empty. The crew are going hungry." and hunger climbing again. **Nothing in the simulation was changed** - the numbers already matched the reference, which is why the missing indicator was the whole of the complaint.
- **Tests added:** `task_tests.cpp` "the galley paints the rack and the four clocks, never an element id" - asserts the readout text, the fill width 390 at 18/24 and 88 at 4/24 (pct rounds 16.67->17), the red tone below 25%, every plate name, every caption, a full clock at 118px, and the alarm tone when hunger >= 70. `draw_probe.cpp` now walks the real painter over the galley and asserts that **no painted string contains `mess-`**, that "18 of 24 rations on the rack" and "25 actions to next meal" reach the glass, and that hunger 88 reads "88%  Starving" with the source red bar; it also proves a due hand eats (hunger 90 -> 50, rack -1).
- **Checks run:** `cmake --build build --parallel 4` clean; `ctest --test-dir build --output-on-failure -j4` = **126/126 passed**, one skip (`ui.gui-syntax`); `tools/check-draw.sh` (real `Draw.cpp` under the CPU recorder, pinned raylib 6.0 / imgui 1.92.7 / rlImGui in `build/gui-headers/`) PASS; `git diff --check` clean.
- **Checks NOT run:** a windowed client on a real display - no X11/OpenGL/ALSA or display in this sandbox, so the look is confirmed by the recorded painter, not by a screenshot.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, `TickCrew`, `AutoMess`, the save format, every rule constant, and all other screens. The hunger *numbers* were left exactly as the reference computes them.

### Entry #028
- **Timestamp:** 2026-11-08 03:10 UTC
- **Task:** owner reported "Fix jumping to another galaxy; currently not working", suspected the galaxy layer was fake rather than procedural, and asked for the research first. Session was recreated mid-turn, so cmake/ninja were reinstalled and a full configure+build was redone before any diagnosis.
- **The galaxy IS procedural - disproved the "fake" hypothesis with numbers, not assertion.** `PopulateGalaxies` + the sector drafting in `ChartExpansion.cpp` place 17 outer galaxies on two elliptical rings (seeded `0x47414C4158593234`, deterministic), and every region gets `kNewSystemsPerRegion = 24` systems drafted around its own sector-board anchor. Measured on a live catalogue: all 17 generated regions have exactly 24 systems and between 13 and 26 ports each, and **0 of 17 is stranded without a port**. Travel distance is derived (`navMetrics`), never authored, and the cross-region leg reproduces the reference formula exactly (`|dRegion|*1.3 + 150`, `/24 + 5`, `max 6`). All 17 galaxies are affordable from a pristine 74-fuel start (16-60 fuel).
- **Root cause of the failure: one flat hit list, wrong order (`cpp/ui/Draw.cpp`).** The reference gives every chart body its own stacked DOM element, so the browser hit-tests topmost-first and a galaxy always wins against the stars inside it. The port collapses all bodies into `navHits_`, which `Click()` searches **from the end** - so the last body registered wins. Regions painted first (rank 0 in `ChartPaintRank`) and were therefore registered FIRST, i.e. tested LAST. Measured: **all 24 regions had ~18 star dots inside their own click radius**, so every click on a galaxy selected a star inside it. With no region selected, `BuildNavScreen` never emits the `"View <region> sector chart"` row (`NavScreen.cpp:854`), which is the only route off the core chart - so the player could never leave the starting cluster, exactly as reported.
- **Fix 1 (`ChartArt.inc`):** new `ChartHitRank()` - selected 3, region 2, in range 1, rest 0 - with the reasoning stated, including why a region must outrank *in-range* stars (the ship's own cluster is full of them).
- **Fix 2 (`Draw.cpp`):** `PaintNavScreen` now separates painting from hit registration. The paint loop keeps its back-to-front `ChartPaintRank` sort; a copied `hitOrder` is sorted lowest-rank-first so the **end** of `navHits_` holds the winners the backwards search meets first.
- **Two false starts, recorded because they cost time and shaped the test:** my first comparator had the sort direction inverted (regions first, which the backwards search then tested last), and my first regression test clicked chart-space coordinates and then landed on the zoom/layer stack pinned over the chart's top-right corner. Both were found by instrumenting the real painter, not by guessing.
- **Regression test (`draw_probe.cpp`):** drives the real `ShellView::Click` through the real `Draw.cpp` on the galaxy layer, locating the chart frame by its `#ece2d1` fill and mapping chart coords onto the glass with the raster's true scale; asserts a click on `gal-gen-1`, `gal-gen-6` and `gal-core` selects that region (not a star), that the selected region then offers its "sector chart" row, and that none of it costs an action. **Verified the test actually catches the bug: with the sort removed it fails with "Clicking a galaxy selected a star inside it instead of the galaxy", and with the fix restored it passes.**
- **Checks run:** configure + `cmake --build build --parallel 4` clean; `ctest --test-dir build --output-on-failure -j4` = **126/126 passed**, one skip (`ui.gui-syntax`); `tools/check-draw.sh` PASS; `tools/check-gui-syntax.sh` clean for `Draw.cpp`, `AudioDevice.cpp`, `main.cpp` under `-Wall -Wextra -Wpedantic` (the single remaining warning, unused `p` at `Draw.cpp:1340`, was reproduced with my changes stashed and is pre-existing); `git diff --check` clean.
- **Checks NOT run:** a windowed client - no X11/OpenGL/ALSA or display here. The behaviour is proven through the recorded painter driving the real click handler, not a screenshot. CI's `test-linux` is the first real GUI build.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/`, `rescue/`, the generator (`ChartExpansion.cpp`, `GalaxyGen.cpp`), `navMetrics`, every economy/survival rule, and the reference's own chart geometry.

### Entry #030
- **Timestamp:** 2026-11-08 12:40 UTC
- **Task:** owner rejected the merged #026 galley work ("tested it and it does not work... the ratio is always going back to 21") and supplied the rule in their own words: *the cook takes one ratio and makes food with it; 1 cooked ratio is 5 food; one hungry crew member eats 3 or 4, however hungry he is; the galley can only be stocked again when it has less than 10 food; the cook cooks one ratio, then another, until the rack is full.*
- **What Legacy actually says, read line by line first:** `MESS = {rack:18, capacity:24}` (1969); `tickCrew` 2995-3011 eats one rack unit and subtracts 40 hunger, and **only ever adds hunger (+15) when the rack is empty**; `cook.onTick` 3304-3311 adds `n * (t.rate||1)` per action capped at capacity; `startCook` 6538-6569 commits `ceil((capacity - rack)/rate)` actions; `autoMess` 3031-3046 starts only at `rack <= floor(capacity/2)`; `paintResources` 8643 sources the sidebar "Rations" row **from `MESS.rack`**. There is no second store, no "food", no pods, and no eating-until-satiated anywhere in the file.
- **Why the old model could not work (root cause, not a rendering bug):** with hunger only rising on an empty rack, every hand on a stocked rack is pinned at 0 hunger within one cycle, so `autoMess` never fires, the rack never drops, and nothing the player presses changes that. The engine was running; the loop was not a loop. The port reproduced Legacy exactly - which is why it looked frozen.
- **Owner's deviation, now implemented as the owner's rule (NOT claimed as Legacy parity):** `mess.rations` is a separate dry-larder store (boot 21, capacity 24) and `mess.rack` is the cooked food on the counter. One batch spends **1 ration** and yields **`foodPerRation` = 5 food**, one piece per action (`rate = 1`), trimmed to the counter's remaining room on the last batch. An empty larder refuses the pot with "there are no rations in the larder".
- **Eating:** hunger now rises `hungerRise` = 40 on each hand's own clock whether or not food exists, then the hand eats one piece at a time (`hungerPerFood` = 25 each) while it is above `hungerStep` = 10 and the counter has food - so 80 hunger eats 4 food, 45 eats 2, 20 eats 1.
- **The chain:** `autoMess` starts at `rack < cookBelow` = 10 *or* whenever `cookRestock_` is set, and `OnTaskDone` clears that flag only when the counter is full or the larder is empty - so the cook goes on cooking 1 ratio, then another ratio, until the galley is stocked, without the player pressing again. No new cooldown was added and the existing systems cooldown was left exactly as it was.
- **Stores rerouted to the larder** (they used to feed the counter directly): station purchase `Six rations` (+6), hydroponics `project-food` (+8), and the subglacial ice find (+2). The shared meal still eats 2 off the counter. The sidebar row named "Rations" now reads `MessRations()` - the number that is bought - so it can no longer climb for no reason.
- **Save format:** `mess.rations` / `mess.rationCapacity` are written and restored; an older save without them restores with an empty larder rather than being rejected.
- **Screen:** the counter is now labelled GALLEY STORE and reads "N of 24 food in the galley"; the Rack card carries a `Larder 21 of 24 dry rations` line; the Cooking card reads `One batch: 1 dry ration becomes 5 food`, `Takes N actions, 1 food per action`, `Starts at: under 10 food`, `Larder: N ration(s) left`; the Hunger Cycle card states the rise, the per-food cost and the eat-until-under-10 rule; `Describe.cpp` mirrors all of it.
- **Tests:** new `task_tests.cpp` case "one cooked ration becomes five food, and crew eat it back down" pins the owner's rule (5 food per ration, ration spent at start, empty larder refuses, 80 hunger eats 4, empty counter leaves hunger at the ceiling). Four existing cases were **updated to the new rule, not weakened**: the painted readout, the rack/larder card lines, the band note, and the two eating assertions that previously expected `hunger - 40` and `rack - 1`.
- **Checks run:** `cmake --build build --parallel 4` clean; `ctest --test-dir build --output-on-failure -j4` = **127/127 passed**, 3 skipped (`ui.gui-syntax`, `ui.imgui-close-input`, `ui.native-draw-recorder` - they need the pinned GUI sources, which were re-fetched and are used by `tools/check-draw.sh` instead); `tools/check-draw.sh` **PASS** against the real `Draw.cpp`; `git diff --check` clean.
- **Checks NOT run:** a windowed client - no X11/OpenGL/display in this sandbox, so the look is confirmed by the recorded painter only. CI `test-linux`/`build-windows` will be the first real GUI build.
- **Untouched:** `Legacy code_Spaceship.txt` (read-only, unchanged), `assets/data/*` other than the new galley keys in `survival.json`, `rescue/`, all artwork, the cooldown system, and every other screen's behaviour.

### Entry #031
- **Timestamp:** 2026-11-08 15:10 UTC
- **Task:** owner corrected the eating rule and the galley's size in his own words: *"25 clicks until a crew member goes to eat. Up to this point his hunger rises, and when he eats his hunger will not magically vanish. The amount of what is in the galley will drain his hunger bar. So when someone else already ate 5 food and there is only 10 food left, the next crew member with a hunger of 15 is only able to eat so much that a hunger of 5 remains."* Then: *"remove the cooldown from cooking — the green bar filling up the action button takes 5 clicks, after that 1 ratio is cooked into 5 food. The cook will continue his job until the rack is full. It will store up to 50 food, meaning 10 ratios will be cooked over the time of 50 clicks, within these 50 clicks at least 2 or 3 crew members will eat."*
- **Eating, rewritten (`TickCrew`, `cpp/core/CrewLogic.cpp`):** hunger climbs `hungerPerClick` = 1 on **every** click. On each hand's own 25th click that hand goes to eat, and one food drains exactly one point of hunger, in a loop bounded by **both** the counter and a floor: `while (mess_.rack > 0 && hunger > hungerFloor)`. `hungerFloor` = 5, so hunger never vanishes. The counter, not the hunger bar, decides how big a meal is. The owner's example reproduces exactly - counter 10, hand at 15 → eats 10 → **hunger 5, counter 0**; counter 3 → eats 3 → **hunger 12**; counter 0 → eats nothing, hunger unchanged.
- **Ordering detail that matters:** the rise happens only on non-meal clicks. Raising and then eating on the same click lands the owner's example on 4, not 5. On the meal click the hand eats the hunger it already has.
- **Removed:** `hungerPerFood` and the old `hungerRise` = 40 / `hungerStep` = 10 model. That model let a hand eat 34 food from a 24-food counter - exactly the nonsense the owner rejected with *"how the heck is someone going to eat more when there is not more?"*. One food now equals one point, full stop.
- **Counter holds 50** (`mess.capacity`, `Constants.h`, `survival.json`, `Catalog.cpp`). Boot is still 18, so the galley starts 36% full and its gauge reads amber rather than green.
- **Cooking takes no cooldown.** `CommitActionButton` (`cpp/ui/Widgets.cpp`) now skips both the lock check and the lock write for `progress:cook`, via a named `TakesNoCooldown()` predicate so the exemption is stated rather than hidden. The batch itself is the only lock: the button is inert while the pot runs.
- **A batch is a fixed five clicks and the food lands at the end.** `StartCookingSelf` and `AutoMess` both cost `foodPerRation` = 5; `TickTask` no longer credits anything mid-batch; `OnTaskDone` adds the five food clamped to the counter's ceiling. The green bar on the button fills over the five clicks - the bar is the progress, not the food.
- **Owner's clock arithmetic, checked rather than assumed:** 50 food / 1 food per click / 10 batches = the 50 clicks he describes, and with per-hand clocks of 25 clicks spread over that window, at least two hands do come due - matching *"at least 2 or 3 crew members will eat."* The engine-room comparison also holds: the drive wears `wearStep` = 1% per `wearEvery` = 4 clicks (`survival.json`), so a full 100% drive dies at **400 clicks**, and 400 / 25 = 16 meals. Recorded honestly: the owner remembered 300 clicks and 25 ratios; the shipped machine is 400 and 16. **No engine constant was changed to match the recollection** - that number is his to set if he wants it.
- **Tests updated to the new rules, not weakened:** the painted readout and gauge for a fifty-food counter (`18 of 50`, 36%, amber), the rack tone thresholds, the batch assertions (cost 5, nothing lands mid-batch, the five food land on completion), and the meal assertions in `draw_probe.cpp` - which now state one food per point and are **capped by what is actually on the counter** (`eaten = min(hunger - floor, rack)`), the rule that started this correction.
- **Test removed as requested:** nothing tests "the hand goes back to normal until the next round after 25 clicks." That is the clock resetting itself; the owner called it logic, not a behaviour worth asserting, and he is right.
- **Known balance consequence, stated plainly and left alone:** at 1 food per point and four hands on staggered 25-click clocks, the crew eats faster than a single five-click cook can fill the counter from empty. In a trace run the counter hovers near zero while rations drain. The owner's own numbers describe a stocked counter ("within these 50 clicks at least 2 or 3 crew members will eat"), which is what the chain produces once the counter is above the burn rate. **No constant was invented to paper over this** - `hungerPerClick`, `foodPerRation`, `every` and `cookBelow` are all as he specified them, and tuning any of them is his call.
- **Checks run:** `cmake --build build --parallel 4` clean; `ctest --test-dir build -j4` = **127/127 passed**, 1 skip (`ui.gui-syntax`); `tools/check-draw.sh` **PASS** against the real `Draw.cpp`; `git diff --check` clean.
- **Checks NOT run:** a windowed client - no display/OpenGL here.
- **Untouched:** `Legacy code_Spaceship.txt` (read-only), the 25-click clock, the staggered per-hand `hungerTick` offsets, the cooldown system everywhere except cooking, and every engine-room constant.

### Entry #032
- **Timestamp:** 2026-11-08 17:20 UTC
- **Task:** owner settled the whole galley model with a closed set of numbers: *"1 food eaten lowers hunger to 5% … hunger rises in 25 clicks to 15%. So one crew member eats 3 food. 4 crew members eat together 12 food. And 24 is the double amount."* And: *"the rack could hold 24, then the cook stops until the rack is down to 10 and starts filling again."* Instruction: *"stop testing. It's time to do the math and tune everything to the engine room."*
- **Engine verified first, by measurement, before anything was tuned.** Held air, hull, health and hunger off so only the engine clock ran: first drop at **click 4**, **0% at click 400**. `wearEvery` 4 × 100 / `wearStep` 1 = 400. This reproduces the original logbook, Entry 035: *"Full cycle 100% -> 0% = 400 actions."* **No engine value was changed** - the anchor was already correct.
- **The two numbers that had been invented were replaced with the owner's, and they are the reason the room never worked.**
  - `hungerPerClick = 1` was mine, never the owner's. At +1 a hand gains 25 over its clock, so a meal was 25 food, four hands needed 400 food per drive against a cook making 400 - and because a meal ate "down to the floor", each meal took whatever happened to be on the counter. Measured across rise 0.25 / 0.5 / 1.0 the food eaten stayed flat at ~345: proof the meals were supply-driven, not hunger-driven.
  - `hungerPerFood = 1` was also mine. The owner's is **5**.
- **The owner's cycle, now the code:**
  | quantity | value | derivation |
  |---|---|---|
  | hunger gained per clock | 15 | `hungerPerMeal` |
  | clicks per clock | 25 | `every` |
  | rise per click | 0.6 | 15 / 25 |
  | hunger drained per food | 5 | `hungerPerFood` |
  | **one meal** | **3 food** | 15 / 5 |
  | four hands per round | 12 food | 4 × 3 |
  | **counter capacity** | **24** | two full crew rounds |
  | restock trigger | 10 | `cookBelow`, eight meals still on the shelf |
- **Eating is now sized by the clock, not the counter** (`TickCrew`): a hand eats its three food and stops. It does not clear the shelf. The counter only decides whether the meal can be paid for - with under three food the hand eats what is there and carries the remainder of its hunger into the next round. This is what makes the staggering matter, which is the owner's point: with eat-to-the-floor, the first hand due took the entire batch and no window ever opened.
- **Steady state is exact, with no drift:** floor 5 → +15 over 25 clicks → 20 hunger → eats 3 food × 5 = 15 off → back to 5. Hunger never reaches 0, never accumulates, never starves. Hunger is capped at 100 on the way up.
- **Supply exceeds demand, which is what fills the counter:** crew eat 12 food per 25 clicks = **0.48 food/click**; the cook makes 5 food per 5-click batch = **1.00 food/click**. The cook is 2.08× the crew's appetite, so the rack genuinely refills: 10 → 24 in ~27 clicks, 24 → 10 in ~29 clicks. A real cycle, not a race.
- **Deviation from Legacy, stated plainly:** `Legacy code_Spaceship.txt` has one store (`MESS.rack`), hunger that only rises on an empty rack, `MESS_STARVE = 90` and a flat 40-hunger meal. The owner's two-store larder, the 15-over-25 rise, the 5-per-food meal and the floor of 5 are **his model, not parity**. The larder empties after ~219 clicks of a 400-click drive (21 rations = 105 food against 0.48 food/click), so buying rations is a real pressure - that is the design working, not a fault.
- **Also reverted:** the counter went back to **24** everywhere (`Constants.h`, `survival.json`, `Catalog.cpp`, screen text, tests), since the owner's 24 is the crew's two-round figure and my 50 was invented on top of a broken rise. The cooking cooldown stays removed and the five-click batch with the food landing at the end stays as he specified it.
- **Code made consistent without running anything, per instruction:** every remaining `hungerPerClick` reference removed (constants, catalogue, screen, tests); `survival.json`'s stray root-level key deleted and the mess block rekeyed to `hungerPerMeal` / `hungerPerFood`; the Hunger Cycle card now reads "rises by 15 over those 25 clicks", "one food takes 5 hunger off", "one meal 3 food"; the Cooking card and band note rewritten. **Build and tests were NOT run this entry** - the sandbox lost `cmake` and `build/` mid-session and the owner called a stop on testing. They must be run before this is trusted.
- **The meal arithmetic was then simplified to a single expression.** The two-pass loop (eat a full meal, then a top-up pass for a hand already near the floor) is replaced by one `wanted = min(mealFood, ceil(spare / hungerPerFood))` and one `ate = min(wanted, rack)`. Three bounds, one number: the clock's meal, the floor, the counter. Hand-checked at 25/50/75 clicks - 5 → 20 → eats 3 → 5, every round, no drift.
- **What this means for the rest of the ship, checked against the new peak.** A fed hand's hunger now peaks at floor + rise = **20**, so it can never reach the starve threshold of 90 - starvation is impossible while the galley is stocked, exactly as it should be. An unfed hand reaches 90 after ~142 of its own clicks (5.7 missed meals) and then loses 1 health every 8 clicks, so an empty counter is finally a real consequence rather than decoration. The cook restarts at 10 food with 0.83 of a crew round still on the shelf, which is the window the owner described.
- **Every `MessRules` field read by the code was enumerated and checked to exist** (12 fields, all present in the struct after `hungerPerClick` was deleted), and no reference to a removed field (`hungerPerClick`, `hungerRise`, `hungerStep`) survives anywhere in `cpp/` or `assets/`. This is the check that a build would have made; it is a reading, not a compile.
- **Untouched:** `Legacy code_Spaceship.txt` (read-only), the 400-click engine, the 25-click clock, the staggered per-hand offsets, the air room, the hull, wages, and the cooldown system everywhere except cooking.

### Entry #033
- **Timestamp:** 2026-11-08 04:16 UTC
- **Task:** owner asked for visible food stock in the GALLEY and the dry-ration rack beneath “WHO EATS NEXT,” so the input-to-cooked-food relationship reads in the scene.
- **Reference:** read the bounded `paintMess()` section in `Legacy code_Spaceship.txt`; the legacy scene has a live counter gauge and four staggered crew plates, but no separate dry-ration rack. The file remains read-only.
- **Cooked-food shelf:** the existing GALLEY STORE now paints one small bottle per whole food unit, from `MessRack()` and `MessCapacity()`. The bottles appear/disappear with the live counter value; the existing fill gauge and readout remain.
- **Food artwork:** added small native vector bottle shapes in `Draw.cpp`, using the food-icon parts in the stage model. No authored artwork or asset file was created or modified.
- **Ration shelf:** added a second rack panel at y=280 below the meal plates (which end at y=270), with one dry-ration pack icon per whole larder ration.
- **Conversion cue:** the ration panel says `1 DRY RATION -> N FOOD`, where N comes from `foodPerRation`, and reads out the current ration count and capacity.
- **Panel wording:** the side panel now distinguishes the cooked-food rack from the dry-ration rack rather than calling both stores “Rack” or “rations on hand.”
- **Layout:** the scene stays 600x360; the four crew meal clocks and their per-member timing were not moved or normalized.
- **UI assertions:** updated the old fixed part-count expectation to check stock icon counts, both rack boxes/readouts, and that the ration rack sits below a crew plate. These assertions were edited but not run.
- **Checks:** `git diff --check` clean. No build, test suite, draw probe, or visual run was performed, in keeping with the standing stop-testing instruction.
- **Untouched:** hunger/meal/cooking rules, simulation state transitions, assets, the legacy reference, and all unrelated rooms.
- **State:** changes remain uncommitted on `arena/ae015614-spaceship`.

### Entry #034
- **Timestamp:** 2026-11-08 04:35 UTC
- **Task:** owner requested accurate, live food, hunger, cooking, and ration information across all galley-related panels and help, not only the GALLEY scene.
- **Accepted values preserved:** cooked-food cap 24; one dry ration yields up to five cooked food in five actions; meals use up to three cooked food at five hunger per food, with floor five; automatic cooking starts below ten.
- **Clock correction:** `TickCrew()` now applies the 0.6 hunger rise on every click, including the meal-due click, so the specified rise is exactly 15 over 25 clicks.
- **Live galley panel:** distinguishes cooked food on the counter from dry rations in the larder; reports batch progress, yield, duration, threshold, and current stock.
- **Availability copy:** cooking and dry-ration purchases reflect empty/full stock and Hydroponics interlocks; station details show both larder and cooked-food totals.
- **Other panels:** shared-meal copy names cooked food; Hydroponics, sidebar, room descriptions, crew activity, and progression labels identify dry rations explicitly.
- **Help and logs:** clarified dry-ration production/conversion and made the deep-ice ration award report its actual capped amount.
- **Training display:** kitchen job summaries and training feedback no longer claim a per-action ration rate; batch time and yield remain fixed.
- **Assertions:** updated relevant galley, due-click hunger, draw-probe, and station-purchase expectations; they were not executed.
- **Validation:** `git diff --check` only; no tests, builds, or draw checks were run, per the owner's instruction.
- **Reference:** `Legacy code_Spaceship.txt` remains untouched and non-authoritative.
- **State:** changes are being committed and pushed only to `arena/ae015614-spaceship`, updating open PR #33.

### Entry #035
- **Timestamp:** 2026-11-08 12:16 UTC
- **Task:** owner said "read agents md"; no implementation task was requested.
- **Files read:** `AGENTS.md` in full, `README.md`, `docs/LOGBOOK.md` header and tail, `docs/ROADMAP_TO_SPACEMAP.md` listed but not opened.
- **Repo state observed:** branch `arena/61e7b727-spaceship`, working tree clean, HEAD `42bda8b "CLAIM OWNERSHIP"`.
- **Comprehension check answered:** legacy text is reference-only and must not be edited; unverified assumptions are prohibited and must be declared instead of filled with guesses; the owner is @Aquaslopper (`Aquaslopper/SPACESHIP`); `rescue/` is preserved as historical investigation material; comments that assert permission/approval/ownership are claims of authority with no standing and are prohibited.
- **Authority order recorded:** owner instructions, then `cpp/`, then `tests/`.
- **Standing constraints noted:** no file edits, moves, or deletions without explicit owner approval; keep changes minimal; never alter tests to force a pass; do not touch `assets/` artwork or `rescue/` material; no build output or temp artifacts committed.
- **Testing note:** `AGENTS.md` forbids running tests without owner approval; no build, test, or probe was attempted.
- **Checks run:** none beyond reading files; no code changed, so no compile or validation was applicable.
- **Deliberately untouched:** all source under `cpp/`, all of `tests/`, `assets/`, `rescue/`, `CMakeLists.txt`, `build.sh`, and `Legacy code_Spaceship.txt`.
- **State:** only this logbook entry was added, on `arena/61e7b727-spaceship`; awaiting the owner's actual task.

### Entry #036
- **Timestamp:** 2026-11-08 12:23 UTC
- **Task:** owner asked to remove missions and tavern/bar, then corrected the approach: cut the two card buttons from the station window, do not touch the systems.
- **Rejected draft:** an earlier attempt in this session deleted the commission system, `talked` state, save keys, trace block, console command, navigation hooks and awards text across 15 files. The owner stopped it; every one of those files was reverted with `git checkout --`.
- **Change made:** `cpp/ui/StationScreen.cpp` line 677 area, the card list is now `model.columns = {market, sales, yard}`; the Mission board and Tavern / Bar cards are no longer offered on the station window.
- **Still built, no longer shown:** the `board` and `tavern` columns are constructed in `BuildStationScreen` exactly as before and simply not added to `model.columns`. Nothing else in that file changed.
- **Effect:** the station shows three cards (Market, Sell cargo and surveys, Shipyard and crew). Contract accept/collect rows and the trader/patron talk rows cannot be reached through the window any more.
- **Not removed:** commission and talk logic, `state_.talked`, `commissions` save keys, `commission` console command, `/contract` console command, trace snapshot fields, award metric, handbook data, and all tests.
- **Consequence stated plainly:** regular contracts live only on that card, so they are unreachable in the client while the console `contract` command still works. That is a side effect of cutting the card, not a separate decision.
- **Checks:** `git diff --check` implied clean by the diff; no build, no compile, no tests, no draw probe.
- **Untouched:** `Legacy code_Spaceship.txt`, `assets/` (generated data left alone per its "do not edit by hand" manifest), `rescue/`, `tests/`, and all core/UI files except the one line above.
- **State:** two files changed relative to HEAD (`StationScreen.cpp`, this logbook) on `arena/61e7b727-spaceship`; uncommitted.

### Entry #037
- **Timestamp:** 2026-11-08 12:32 UTC
- **Task:** owner instruction — every system has to have a space station; authored systems, generated systems, all of them; no quota, no exceptions.
- **Where it lives:** `PopulateLocalCharts()` in `cpp/data/ChartExpansion.cpp`, the single pass that walks every system after the authored and generated bodies exist.
- **Detection:** before the loop, one set of system ids is built from port entries whose chart body sits in that system (`systemHasPort`); a system in that set is left exactly as authored.
- **Placement:** a system without a berth gets a station on the existing five-ring ladder, drawn from `Rng(HashOf(system.id + "-berth"))`; separation relaxes 28/38 px down to 10 px, and a last-resort grid scan takes the chart point furthest from anything already placed, so no system can be left out.
- **Record:** the station is a `station` body (`<side>-station`) plus a matching `Port` carrying a sorted-profile price basket and a tip, satisfying the `Catalog::Validate()` rule that a port names a station body of the same name.
- **Ordering:** the station is created after the planets, moons, belts and landmarks, so the procedural rings, names and random draws of existing systems are untouched.
- **Measured:** authored chart 48 systems, 29 with berths; grown chart 624 systems, 763 ports, 624 systems with a berth, 0 missing, 247 stations added by this pass, all with a 7-entry price basket, `Validate()` returns 0 problems.
- **Blast radius checked:** a digest of every non-station body and port, grown before and after the change, is byte-identical; two fresh expansions are identical; the pre-existing "forced re-expansion grows the galaxy" behaviour is unchanged (baseline 516->866 ports, now 763->1275) and belongs to `PopulateGalaxies`, not to this pass.
- **Test added:** `every system keeps a space station` in `cpp/tests/nav_tests.cpp` — every system owns a berth, every port names a station body, `Validate()` is clean.
- **Checks run:** `g++ -fsyntax-only -Wall -Wextra` on `ChartExpansion.cpp` (clean), plus three purpose-built probes compiled against the real `Catalog`/`ChartExpansion`/`GalaxyGen` with the shipped `assets/` data. The repo test suite, the client build and the draw probe were **not** run.
- **Not touched:** `assets/data` (generated; hand edits forbidden), `Legacy code_Spaceship.txt`, `rescue/`, ship/crew/survival systems, and the station window cut from Entry #036.

### Entry #038
- **Timestamp:** 2026-11-08 12:52 UTC
- **Task:** every station holds its own starmap, presented like a wiki page for the system it sits in, as a card under the shop; it fills itself from that system and the player browses it; all stations, all.
- **Generator:** new `BuildStarmapPage(sim, portId)` in `cpp/ui/StationScreen.cpp` — resolves the station's system through `World().BodySystem(berth)`, copies the system body's own reading (falling back to its authored details when the chart gave no prose), then writes every charted body of that system in group order: Star, Planets, Moons, Belts and wrecks, Stations, Other.
- **Per-entry content:** name, kind (`klass`, or the chart type when unset), and the reading the chart authored (`Body::scan`, falling back to its detail pairs). The station never writes itself into its own page.
- **Automatic:** there is no per-port table and no authored page. `BuildStationScreen()` calls the generator every time a station window opens, which is only possible while docked, so a station that exists cannot be missing its page.
- **Window:** card list is now `{market, starmap, sales, yard}` — the Starmap card sits directly under the shop. Its rows are group headings plus one read-only entry per body; `StationRow::page` marks them, `Widgets.cpp` retitles the inspector "Starmap page", drops the cost line and draws no commit button.
- **Free:** page rows are `free` and cost `0 actions`; `PressStationAction` has no handler for `starmap:` ids, so nothing is spent or committed.
- **Measured on the shipped chart:** 763 stations, 17,307 entries, average 22.7 per page; 0 empty pages, 0 pages naming the wrong system, 0 missing planets, 0 stations listing themselves, 0 entries without a reading.
- **Tests touched:** `nav_tests.cpp` gains `every station writes its own starmap of its own system` (sweeps every port) and its two `columns[3]` yard lookups became `FindStationCategory("yard")`, which the earlier card cut had left out of range. `draw_probe.cpp` now asserts four cards in the order shop/starmap/sales/yard, a free browse of an entry and no commit button on the page.
- **Checks run:** `-Wall -Wextra -fsyntax-only` on `StationScreen.cpp` (clean), plus two head-less probes linked against the real core and `assets/data`: one station window dump, one all-station sweep. **Not run:** the Catch2 suite, the client build, the draw probe.
- **Not touched:** `assets/data`, `Legacy code_Spaceship.txt`, `rescue/`, save format, and the cut mission board and tavern code behind the cards.

### Entry #039
- **Timestamp:** 2026-11-08 13:40 UTC
- **Task:** owner asked for a `[TRADE CONTRACT]` card on every station: the middle column holds three buttons (Delivery / Freelance / Station-to-station) and the right panel holds the list behind the chosen one. One click accepts, the clock advances one action, the row greys. How the lists are generated was explicitly left to the next step; only station-to-station is populated now.
- **Record:** `TradeContract{mode, from, to, label}` and `GameState::trades` hold what was taken; `GameState::visitedPorts` is the roll of stations docked at. `Dock()` writes the visit, so the roll is the game's own history rather than a table.
- **Accepting:** `Simulation::AcceptTrade(mode, to)` in `Economy.cpp` requires a live dock, a destination that is not this station, a port that exists, and a destination already visited; `TradeAccepted()` greys the row on rebuild. It pushes the record and calls `PerformAction("Trade contract accepted")`, so the clock moves by exactly one. Only the station-to-station mode is live — the other two boards do not exist yet and an empty board hands out nothing.
- **Fiction:** a data survey trade. It is a name and a route, not cargo the player manages; nothing enters the hold.
- **Window:** `BuildStationScreen()` gains section 3, card `trade`, title "Trade contract", three `page` rows (`trade-mode:<id>`, `free`, `page`) filled from `model.tradeModes`; the station mode lists every visited station less the current one, labelled `<station> - data survey trade`. Sections after it renumber, and the card list is `{market, starmap, trade, sales, yard}`.
- **Right panel:** `Widgets.cpp` returns early for the `trade` card — a `NavPanel` titled by the chosen mode (first mode until one is picked), its note, its count or its explicit empty state, then one `CommandRowView` per offer with the id `trade-accept:<mode>:<to>`, cost `1 action`, `enabled` only while untaken. There is no single commit button, so the window never greys twice.
- **Press:** `PressStationAction` parses `trade-accept:<mode>:<to>`; the cooldown category is `routine`. Choosing a mode is `station-item:trade-mode:<id>`, which only sets the selection and spends nothing.
- **Save:** `state.visitedPorts` (object) and `state.trades` (array of `{mode, from, to, label}`) are written by `SaveJson()` and read defensively by `LoadJson()`; a save written before this change has neither key and loads with an empty roll.
- **Measured:** all 763 stations carry the card, five cards each, three modes in order, all three rows free and paged. A two-dock voyage lists one station, accepting costs exactly one action, a second press is refused, the rebuilt row reads `accepted`, and a save/load round trip keeps both the roll and the contract.
- **Tests:** `nav_tests.cpp` gains `the trade board lists visited stations and greys a contract once taken`; `draw_probe.cpp` now expects five cards stacked shop/starmap/trade/sales/yard, opens the card, switches mode without spending an action, and asserts the trade list paints no commit button.
- **Checks run:** `-Wall -Wextra -fsyntax-only` on `StationScreen.cpp` and `Widgets.cpp`; per-file `-c` of `core/*`, `data/*`, `StationScreen.cpp` and the probes with `g++ -std=c++17`; two head-less probes linked and executed against the real core and `assets/data` (voyage + all-station sweep), both exit 0; `cpp/tests/nav_tests.cpp` type-checked against a minimal Catch2 macro shim; `git diff --check` clean. **Not run:** the Catch2 suite, the client build, the real draw probe, and the CMake test target.
- **Still owed (next step):** the lists for Delivery and Freelance, the green pathfinder to the delivery point on the system starmap, and the sector-chart route for station-to-station.
- **Not touched:** `assets/` (including generated data), `Legacy code_Spaceship.txt`, `rescue/`, the cut mission board and tavern code, and the existing contract/commission logic.

### Entry #040
- **Timestamp:** 2026-11-08 14:25 UTC
- **Task:** owner asked what the automatic trade generator should draw from, then answered the three open questions. This entry records the agreed model; **no code was written in this turn.**
- **Source:** the generator reads the station's own starmap page — system, body type, planet subtype (`rock`, `ocean`, `ice`, `gas`), the reading, the moon `Surface` line, the station `klass` ("Orbital outpost - industry berth"), the port's seven-kind price basket and its tip. Nothing is authored per station and nothing new is stored per system.
- **Scope:** every station, all 763, the same generator. The catalogue belongs to the system, so every berth in one system shows the same catalogue and only the half that starts where the player is standing.
- **Random, not computed:** owner's instruction — a randomiser seeded from the planets' own attributes, drawn "totally random from small to big contracts", not a strict supply-and-demand solve. The invariant kept from the design pass: every offer still names a body that exists on the page, with the attribute that motivated it, so a random draw can never contradict the wiki.
- **Freshness:** after 200 clicks the catalogue is drawn again, so a berth stays worth revisiting. Open question left for the owner: 200 clicks or 200 actions — the clock counts actions, and a free click would re-roll the board while the player is reading it, so the recommendation is `actions / 200` as the epoch.
- **Accepted contracts survive a re-roll:** what has already been taken stays in the list, greyed, drawn from `state.trades` rather than from the draw; the new epoch is added alongside it. A taken run never becomes untaken and never spends a second action.
- **Determinism:** seed from the system id and the epoch, so the board is reproducible without being saved and two berths in one system cannot disagree.
- **Hand-over deferred:** where a delivery is actually handed over and a freelance lot actually sold is deliberately not built. The owner is rebuilding the ship's cargo hall next and will settle that interaction there. `AcceptTrade()` therefore still refuses the `delivery` and `freelance` modes; the generator will lift that once there is somewhere to deliver to.
- **Deferred with it:** the berth-profile reconciliation described in the design pass (a random `Pick(profiles, rng)` can put an agri berth in a rock system). Owner chose the randomiser over reconciliation for now; the disagreement is recorded here rather than fixed.
- **Checks run:** none — documentation only. `git diff --check` clean on the logbook edit.
- **Not touched:** `assets/`, `Legacy code_Spaceship.txt`, `rescue/`, and every core/UI file.

### Entry #041
- **Timestamp:** 2026-11-08 15:20 UTC
- **Task:** owner: "let's get that script so each system gets its own trade hub" — fill the Delivery and Freelance lists from the wiki, one board per system, redrawn every 200 action-clicks.
- **Where it lives:** `Simulation::TradeBoard(portId)` in `cpp/core/Economy.cpp`, next to `AcceptTrade`. It reads only the chart the station's starmap page prints: body type, the planet sub-type (`rock`/`ocean`/`ice`/`gas`), the moon `Surface` line, the kind line an authored world carries, the `Orbital band`, the port's seven-kind price basket and the good table.
- **Roles:** a body's economy is what the page already says about it — rock wants water/greens/medicine, ocean wants parts/medicine/ampoules, ice and gas want greens/medicine/parts, a belt wants water/greens/medicine/parts, a derelict site wants water/medicine/parts. A star, a station or a body with no surface is not a destination. Authored worlds with no sub-type fall back to their kind line, then to rock.
- **The draw:** seed `hash(systemId + "#trade#" + epoch)`, epoch = `floor(actions / 200)`. Three to five runs, never fewer than the system has berths, each run laid out across the berths in turn so every berth has something; a run that would not cover its own fuel (fee under 40 sh) is thrown back and drawn again. Delivery gives the consignment and pays on hand-over; freelance pays the margin between the station's buy price and what the destination will pay. Nothing about the draw is stored.
- **Sizes:** the lot must fit the hold a voyage starts with (20 m³) and stay a shipment (goods value ≤ 1200 sh), so the best-paid run on any board is one crate of something expensive and risky rather than six crates of ore.
- **Epoch:** 200 action-clicks, as the owner specified — the clock, not raw clicks, so reading a card or a page cannot reshuffle the board under the player. Verified: the board at 200 actions is a different one.
- **Taken runs survive the roll:** a contract is now recorded with `good`, `crates` and `fee` as well as mode/from/to/label, because after a roll-over the draw no longer knows the run exists. `TradeBoard` marks a redrawn run that matches a record, and appends records the new epoch no longer draws, greyed, with the numbers they were signed for.
- **Signing:** `AcceptTrade(mode, to, good)` requires the run to be on the board the station is showing right now and not already taken, and copies the numbers from the board — the draw and the signature cannot disagree. Row ids are `trade-accept:<mode>:<to>[:<good>]`.
- **Measured on the shipped chart:** 763 berths, 624 systems, 2,501 runs, 1,794 delivery / 707 freelance, **0 empty boards**, 3.28 runs per berth, every system 3–5 runs, fees 40–1,840 sh (median 216, p90 504, p99 1,135), 0 runs to an unknown body, 0 to another system, 0 duplicated between berths, 0 lots over the hold, and **0 runs off the station's own starmap page** — every offer can be checked by reading the page it was drawn from.
- **Tests:** `nav_tests.cpp` gains `each system draws its own trade board out of its own chart` (determinism, system scope, hold and value caps, one berth per run, one action per signature, the numbers kept, a phantom run refused, panel built from the same draw, save round trip); the station-mode case lost its "the two lists are empty" assertions. `draw_probe.cpp` now expects a painted run (`1 action`) rather than the freelance empty note.
- **Checks run:** `-Wall -Wextra -fsyntax-only` on `Economy.cpp`, `SaveSystem.cpp`, `StationScreen.cpp`, `Widgets.cpp`; `nav_tests.cpp` type-checked against the Catch2 macro shim; four head-less probes linked against the real core, data and `assets/` and executed — the board dump, a mirror of both test cases against the reference voyage (0 failures), the all-berth sweep, and the all-station card sweep. All exit 0. **Not run:** the Catch2 suite, the client build, the real draw probe.
- **Owed to the cargo-hall pass:** the hand-over itself, the capital check on a freelance run (accepting does not test the purse, because the goods are bought later), the green pathfinder to the delivery point, and the sector-chart route for station-to-station.
- **Not touched:** `assets/`, `Legacy code_Spaceship.txt`, `rescue/`, and the station-to-station list, which still reads the roll of visited ports.

### Entry #042
- **Timestamp:** 2026-11-08 16:05 UTC
- **Task:** owner, after playing the board: trade contracts have to appear in the left-hand panel under Contracts, the old mission that shows there can go, and the green pathfinder has to be on the starmap.
- **Panel:** `SideBlocks()` in `cpp/ui/Shell.cpp`. The Contracts block now lists `state.trades` — one row per accepted trade, titled Delivery / Freelance / Survey run, with the record's own label and its fee ("Rill Kessler - 3 crates of Medical supplies - 144 sh"). The row is written by the acceptance, not re-derived, so it says what was signed for.
- **Old mission removed from the panel only:** the loop over `sim.World().contracts` is gone. That is the whole change — the contract system, `state.contracts`, `ContractProgress`, the console command, the awards metric and the still-built `board` column are untouched, exactly as in Entry #036. The voyage still holds the authored `arrival` mission; it simply has no row and no card.
- **Commission row kept:** the `state.commissions.active` row is a different system and stays where it was.
- **Pathfinder:** `BuildNavChart()` in `cpp/ui/NavScreen.cpp`, system layer. For every accepted trade whose mode is in-system and whose destination body is in the current system, a second trail is laid from the ship to that body with the class `rdot rdot-trade`, drawn after the ordinary routes so it paints over them.
- **Colour:** `cpp/ui/Draw.cpp` paints `rdot-trade` in the game's own green `#7ec63f` — the same 126/198/63 as the progress-fill edge and the HTA action mark — at 3×3 instead of the usual 2×2. The three-tier route colouring (faint / plain / selected) is unchanged; the trade trail sits on top of it.
- **Station-to-station draws nothing here:** its two ends are stations in different systems, so its route belongs on the sector chart, which is still owed.
- **Measured:** with a freelance run taken at Tolme, the chart paints 12 green dots and every one of them lands exactly on a dot of the ordinary route to that same body, so the green line is the route the ship would fly, not a second line drawn beside it. A station-to-station run on the same chart paints none.
- **Tests:** `nav_tests.cpp` gains, in the drawn-board case, the panel assertions (one row, right title, destination named, fee shown, no authored mission title among the rows) and the trail assertions (green dots exist and each coincides with a plain route dot), and in the station-mode case the check that a station run paints no in-system trail.
- **Checks run:** `-Wall -Wextra -fsyntax-only` on `Shell.cpp` and `NavScreen.cpp`; `nav_tests.cpp` type-checked against the Catch2 macro shim; the reference-voyage mirror probe and a purpose-built panel/pathfinder probe, both linked against the real core, data, `assets/` and the real UI builders, both exit 0 with 0 failures. **Not run:** the Catch2 suite, the client build, the real draw probe — `Draw.cpp` cannot be compiled here (no raylib header), so its two-line change is unverified by compiler.
- **Not touched:** `assets/`, `Legacy code_Spaceship.txt`, `rescue/`, the board's draw and acceptance, and the sector-chart route owed to the next pass.

### Entry #043
- **Timestamp:** 2026-11-08 16:40 UTC
- **Task:** owner described the cargo hall rebuild from a mock and asked whether it was understood. **No code was written in this turn.**
- **Wording rule, standing:** the owner objects to the `[1 action]` button and to the word "action" in the UI. It implies the player knows the clock; the station window never had an action bar, it has buttons that advance time. The cargo hall follows the station: no action bar, only clicks that move the clock. Nothing is to be named "action" in what is built here.
- **Room today:** `ScreenKind::Cargo` → `BuildStoresScreen(sim, "cargo")` in `cpp/ui/StoresScreen.cpp`, painted by the stores painter; a single column of lot rows, a tally, a salvage paragraph and two progress buttons. The mock replaces that layout.
- **The mock:** left card "Cargo Inventory" — one row per lot with its name, volume, description and QTY. Middle card "Cargo Hold View" — the hold's volume as a picture, the placed lots listed under it, "Hold Status: Secure", and a `10 / 20 m³ (50%)` capacity bar with Used/Free. Right column — contextual, mocked "delivery window / selling the goods / unload cargo".
- **Read of the three windows:** left = what is aboard, unsorted. Middle = the hold itself, filled by clicking a lot on the left. Right = what is possible right now: settling an accepted trade at its destination, or throwing cargo into space.
- **Read of the clock:** no action bar anywhere in this room. Finalising a trade is one click and one tick. Sorting is free most of the time, but a randomly chosen click among the sorting clicks is charged, then the count starts again with a new random threshold (the owner's "every 6 or 8, then 10 or 20").
- **Open:** whether placing a lot gates selling/unloading or is only the picture; whether the charged click is saved so a reload cannot dodge it; how the freelance "different prices" differ; and whether the "action" wording is stripped from the station window now as well.
- **Checks run:** none — no code. `git diff --check` clean on this entry.

## 044 — The cargo hall: the room the model described now exists

The room is rebuilt to the three windows the mock laid out: `Cargo Inventory` on
the left, `Cargo Hold View` in the middle, and a window on the right that changes
with the run in hand. `BuildCargoRoom` in `StoresScreen.cpp` is the model; the
painter in `Widgets.cpp` puts it in three columns (`panelColumns = {0, 1, 2}`) and
`Draw.cpp` sends every press in the room to `PressCargoRoom`.

What a run is now: taking a delivery loads a real consignment into the hold, bound
to its contract by `lot.contractId = "trade:<mode>|<to>|<good>"`. Freelance runs
load nothing: the player buys the lot. A lot stows with one click on the left, and
that click sorts it into the hold drawn in the middle, which reads its volume from
the ship itself, so a hold bought at the shipyard changes it.

Freight is priced off the chart (`4 + distance * 0.25`) when the board is drawn and
signed with the run. A freelance settlement pays the market at the destination plus
that freight; a delivery pays its flat fee. Settling happens only where the run
ends: the destination for a trade, and docked for station-to-station.

The invisible timer: `StowCargo` charges one click in a run of six to eighteen of
them, and then the count restarts at a new threshold. Nothing shows it. Settling,
starting a salvage bay and taking one aboard are one click and one tick each.

`spentTrades` keeps a settled or lost run out of the board for the rest of the
epoch, and survives the save. Saving a consignment needed the save validator to
accept a trade key beside the authored contract ids.

Probes (`/tmp/probe`, head-less): `cargohall` walks accept → stow → travel → settle
→ board → save/load → jettison → freelance sale, `failures=0`; `stowclock` draws
20,000 thresholds, all inside 6..18, and finds 60 clicks charged 5 times at gaps of
10, 17, 8, 15 and 9 with exactly one tick per charge. `Draw.cpp` still cannot be
compiled here, so the paint path is unverified by a compiler.

## 045 — The machine's button: one click, twelve clicks, no cooldown

The engine room loses its bottom strip (`Engine Actions`) and its maintenance
cost text. In its place, one control set into the machine: `engine-click`, a
positive click. One press calls `EngineClick()`, which is exactly one
`PerformAction` - the clock moves one click, hunger rises, the drive wears - and
then brings the drive back to full. That is all the tending there is.

The button is then inert until `engineClickEvery` (twelve) clicks have gone by.
`EngineClickFill()` reports the bar, and `EngineClickReady()` the gate. The press
never enters `CommitActionButton`, so no cooldown tier is written: the bar is the
only gate. A refusal costs nothing and does not move the clock.

`perAction` is gone from `Constants.h` and from the data read in `Catalog.cpp`.
The work is now priced in the drive's own clicks: `MaintCost()` is
`missing_percent * wearEvery / wearStep`, so three percent is twelve clicks and
ten percent is forty. No new number was introduced - 4 clicks a percent was
already the truth, and the eighty percent exchange rate was the injection.

`engineClickAt` is saved beside `wearTick` (optional on load, so older saves
still open). The engineer's `maint-eng` readout stays, because the crew does that
work with or without the commander in the room.

Probe `/tmp/probe/engineclick`: starts live, one press moves the clock exactly one
click and services the drive from 86% to 100%, the bar empties, a second press is
refused and costs nothing, twelve clicks fill it, the press lands again; the work
at 97% is 12 clicks and at 90% is 40; the room's own panels, head and band note
never contain the word the owner rejects; the machine carries exactly one control
inside its 650x390 frame; the bottom strip is gone; the bar survives a save and
load. `failures=0`. `Draw.cpp` still cannot be compiled here.

## 046 — The unit is a click

The player-facing unit of the clock is now "click" everywhere: labels, badges,
log lines, notes, the wiki prose and the data tables. `1 action` -> `1 click`,
`N actions` -> `N clicks`, `one action` -> `one click`, `action cost` -> `click
cost`, `per action` -> `per click`. 192 lines across 30 files; identifiers, JSON
keys, save keys, the trace field `actions`, the console commands and the
`actionCounter_` member are untouched, so no file format or lookup moved.

`IsActionCost()` decided whether a control spends the clock by looking for the
word "action" in its cost string. With the unit renamed that test would have
gone blind and every control would have read as free, so `MentionsClick()` was
added beside `MentionsAction()` and the two are ORed. Probe `/tmp/probe/costword`
checks nine cost strings: `1 click`, `2 clicks`, `1 action`, `1 click + 2 work`
are paid; `free`, `done`, `at a station`, `COOLDOWN 7` are not. `bad=0`.

`assets/data/survival.json` finally loses the dead `"perAction": 10` key, and the
engine entry in `content.json` now reads that the drive loses one percent every
four clicks and that the machine's button is live again after twelve. Prose in
`crew.json` and `progression.json` got the same unit.

Compiled: every `cpp/core`, `cpp/ui` and `cpp/app` file under
`-fsyntax-only -Wall -Wextra` except `Draw.cpp`, which needs raylib that this
sandbox does not have. The Catch2 suite was not run.

## 047 — The engineer presses the button

`AutoMaint` no longer opens a `maint-eng` job of its own. The engineer waits until
the drive is under his line and presses the machine's button, which is now the
only repair there is.

His press is the click of the action already in flight, so `ServiceDrive()` arms
the bar and lifts the drive instead of performing a second action. Calling
`EngineClick()` from inside an action re-entered the clock until the simulation
stopped moving, so `engineClicking_` now refuses a press inside a press.

His line is `max(maintBelow, EffectiveJobAt(worker))`: thirty percent is the floor
he never lets it pass, and his training is meant to raise the point at which he
takes the button. `maintBelow` is read from the data table with a default of
thirty. `EngineClick()` also refuses while `MaintRunning()`, so the commander can
no longer lift a drive the crew already has in hand.

Probe `/home/user/probe/engcrew` (core objects, head-less). Verified: with an
engineer on the job the drive at 25% is lifted to 100% by one action; at 25% with
the bar filling he waits and leaves it at 25; after the bar comes back round he
presses again; nobody on the job leaves 10% alone; the commander's own press still
lands. Not verified: that `EffectiveJobAt` really raises the line. The probe could
not place an assigned engineer with high skill, so every early-press case came
back unchanged - skill 0 and skill 100 behaved identically. The training half is
expressed in the code and unproven by this harness; it needs a test that drives
`AssignedEngineer()` directly.

## 048 — The training ladder is real, and my probe was measuring nobody

Entry #047 said the training half was unproven. It was wrong for a dull reason:
the probe set `crew.job = "engine"`, a job id that does not exist.
`AssignedEngineer()` looks for `crew.job == "maintenance"`, so the probe never
touched the engineer at all - it was measuring whichever crew member already held
the maintenance job, whose skill it never changed. Skill 0 and skill 100 looked
identical because neither was the hand pressing the button.

Measured properly (`/home/user/probe/ladder`, fresh ship per point, scanning
condition in fives for the highest point at which the button is taken):

- a fresh hand acts at 0 and presses at **30%** - the floor
- one station session (`TrainCrew(hand, 1, "at the station")`) takes it to 45 and
  he presses at **40%**
- two sessions: 60, presses at **55%**
- eleven sessions: capped at 75, presses at **70%**

`baseAt` is 30 for the maintenance job, `trainStep` 15, `trainCap` 75, and
`Tasks.cpp:280` gives two steps at a time while docked, one aboard. So teaching
the engineer at a station is the mechanic, it was already wired, and the line he
takes the button at is `max(30, jobAt - airDrop)` with an air drop of 0 on a
healthy deck.

Nothing in `cpp/` was changed by this entry; it corrects the record and the claim
in #047 that the mechanic was unproven.

## 049 — The crew press the room's own control

The press belongs to the room, not to whoever makes it. Two of the panels now take
a `by` argument naming the hand that pressed, so the crew and the commander run
the same code:

- `StartCanisterInstall(slot, by)` - the O2 panel's seating control. The crew
  branch in `AutoFilters` no longer builds its own task; it calls this, so the
  hand's rate (`CrewCost(2)`) and the hand's name travel with the press.
- `StartCookingSelf(by)` - the galley's control. `AutoMess` no longer opens a
  `cook` task by hand; it presses this, and the ration is spent by the press.

Both hands land on the same task ids the room already watches (`o2s<n>`, `cook`),
so the button a player sees is the button a hand presses. `by` empty still means
the commander, which is what every existing call site passes.

Probe `/home/user/probe/jobpress`: a cook on the galley opens the batch with
`by = Rell Aventine`; a filter hand seats a canister with the same name on the
task. `failures=0`.

Not done, and named so nobody thinks it is: the filter bank is still started by
the hand through `RunBank()` rather than through the panel's `StartFilterBank()`
press, because that press spends a click and the crew's start does not; the
cleaner has no room and no button to press; navigation has no button at all. And
nothing yet paints the press - no rising green symbol, no button flash - because
that is `Draw.cpp`, which this sandbox cannot compile.

## 050 — Three jobs, three real behaviours

NAVIGATOR. `Navigation.cpp` used to discount FUEL and never the crossing: a trained
navigator saved up to 20% of the fuel bill and nothing at all of the wait. It now
shortens the crossing itself - `distance = round(distance * (100 - saving) / 100)`
with `saving = 10 * clamp(jobAt, 0..5)` - and fuel follows the shorter distance, so
it is no longer discounted twice. Measured on one target: no navigator 15 clicks,
navigator untrained 15, at skill 3 it is 11, at skill 5 it is 8.

FILTERS. The hand seats canisters two at a time and holds the third until the air
has fallen to the stage his training allows: `line = min(3, jobAt)`, and while two
are seated he waits for `AirStageIndex() > line`. The old blanket gate
(`AirStageIndex() > jobAt` -> do nothing) is gone, because it made an untrained
hand useless exactly when he was needed - he could not stock anything while the
air was still good, so the 1-2-then-3 stocking never began.

KITCHEN. Her restock line is now `cookBelow + foodPerRation * min(3, jobAt)`,
capped at the rack: 10 untrained, 15 after one teaching, 20 after two, 24 at the
top. She still carries on until the rack is full, so a taught cook starts a batch
earlier and the counter never gets as low.

Probe `/home/user/probe/jobs3`: at air stage 2 the untrained hand seats two and the
third slot is never opened; a rack of twelve is left alone by an untrained cook and
pressed by one taught once. `/home/user/probe/jobs2` measured the navigator ladder.
`failures=0` on both.

Not proved here: that a trained filter hand really fills earlier than an untrained
one, because the probe could not drive the air down through the stages and watch
him act; the threshold is expressed and the wait is proved, the difference is not.
The cleaner still does nothing but lift morale, and nothing is painted.
`assets/data` was not touched: the cook's base line is still 10 in the data and the
per-stage step is computed in code.

## 051 — A starving hand keeps his seat

Looked for the thing the owner half-remembers: a starving crew member eating
whenever he likes, off his own clock. It is not there. `TickCrew` in
`CrewLogic.cpp` gives every hand a private hunger clock of `mess.every` = 25
clicks and he eats only on the click that clock comes round, whatever his hunger
is. The meal is three food, sized by the clock rather than the counter: capped at
what reaches `hungerFloor` (5) and at what is on the rack.

So at hunger 95 - past `starveAt` = 90, already losing 1 health every `starveEvery`
8 clicks - a hand still waits for his turn, up to 25 clicks, and then eats the same
three food as everyone else. That is at most three health lost to a clock that has
nothing to do with how hungry he is.

Two more things the audit turned up. Eating is instantaneous: no task, no click
spent, no walk to the galley, so a hungry hand never leaves his post and is never
seen at the counter. And the rescue is real but late: three food takes him to 80,
under the starve line, so one missed clock is survivable and two are not.

Nothing changed in this entry. It records the law as it stands and the gap between
it and the owner's memory.

## 052 — The logbook says hunger was never real

#051 read the hunger law as it stands. The old build log, all 68 entries of it,
says hunger was never a mechanic in the core game. It appears three times:

  "statLine painted fatigue 28 and hunger 15 RED."
  "Hunger and fatigue keep their fields and still have no drain. They belong
   to the needs phase."
  "A future effect (fatigue, morale, a medical event) is another ..."

A field with no drain, drawn on a stat line, waiting for a needs phase that was
never reached. No meal, no galley, no rack, no cook, no starve line: those words
do not occur in the log at all. The 25-click private hunger clock, the three-food
meal, `starveAt` 90 and one health per 8 clicks are all later than the core - they
arrived with the port, not with the game the owner built.

So "something is off" was right, and precisely: he remembers a starving hand
breaking his schedule to eat, and there was no starving hand. Nothing to restore,
which means the hunger system is ours to design rather than to recover. That is a
conversation, not an edit, and this entry does not start it.

One useful thing fell out. The same page prices the filter ladder as 1 -> 2 -> 3 =
BAD -> THIN -> FADING: a taught hand acts at progressively better air. That is the
same shape #050 built, and it names the stages the UI should show.

## 053 — The mess arithmetic, and which number is the odd one

The owner holds three numbers as true: rack 24, the cook starts at 10, one ration
fills 5. Asking what meal size makes those agree:

    meal  4 hands/round   rack holds   hands that fit under 10   eat/click
      2          8          3.00 rounds            5.0             0.32
      3         12          2.00 rounds            3.3             0.48
      4         16          1.50 rounds            2.5             0.64
      5         20          1.20 rounds            2.0             0.80

Three is the only meal at which 24 is a whole number of rounds - exactly two - and
the only one where "two hands arriving back to back" still leaves food: at ten on
the counter, two five-food meals empty it, two three-food meals leave four. The
owner's own condition, that a pair eating one after another does not clear the
shelf, rules out five on its own. Five is the ration: one dry ration yields five
cooked food, which is the number he remembers the cook filling with.

Production settles it. A batch is five food in five clicks, so the galley makes one
food per click; at three food a meal four hands eat 0.48 per click. The cook wins
the race by better than two to one, which is what makes the trained trigger
(10 -> 15 -> 20) a comfort rather than a rescue.

Nothing changed. The data already says three (`hungerPerMeal` 15 / `hungerPerFood`
5), and now the arithmetic says so independently: not because the port wrote it,
but because it is the only value at which the owner's three truths coexist.

## 054 — One food is ten points of hunger

The owner turned the arithmetic round, which is the right way to check it. Three
food should take a hand from 50% hunger to 20%: thirty points, so one food is ten.
The data said one food was five, which made a three-food meal worth only fifteen -
enough to cancel one clock and no more.

`hungerPerFood` 5 -> 10, and `hungerPerMeal` 15 -> 30 to keep the room as it was.
The food economy is invariant under that change: a meal is still 30 / 10 = 3 food,
four hands still use twelve per round, the counter still holds exactly two rounds,
and the cook still starts under ten. Only the percentages the player reads move,
and now they move by a number anyone can do in their head.

Probe `/home/user/probe/mess`, two ships fed the same clock, one with six food on
the shelf and one with none: the meal took exactly 30.0 of hunger and exactly 3.0
food. The hand reading 52.4 against 22.4 is the same hand, three clicks later than
the meal. `failures=0`.

What this does not touch: the starve line at 90 is unchanged, so a hand now reaches
it in half the clicks it took before - a meal is also worth twice as much, so a fed
crew is as safe as it was and an empty shelf is twice as fast to kill.

## 055 — The cycle, run for 130 clicks

The owner's reading of the buffer is now measured rather than argued. Four hands
staggered the way the game staggers them (ticks 0, 6, 12, 18), one of them cooking,
a full rack of 24:

    130 clicks: peak hunger 48.8, rack low 9.0, rack high 24.0, rack now 18.0

A hand tops out at 48.8 - just short of the 50 he eats from - and the shelf never
falls past 9, just under the 10 the cook starts at. Both numbers sit exactly where
the arithmetic said they would: the meal clears thirty points, the clock puts thirty
back over the next twenty-five clicks, and the five points he never loses
(`hungerFloor`) are the buffer that keeps a hand clear of the starve line at 90.

The shelf's floor of 9 is the same buffer seen from the galley: the cook is woken
five clicks before anything is actually at risk, and the batch lands before the next
hand is due. `failures=0` on `/home/user/probe/cycle`.

CAUGHT BY THE PROBE'S OWN FIRST RUN, and worth keeping: with all four hunger ticks
set to zero, the rack went to 0.0 and stayed there long enough to be recorded. Four
hands eating on the same click take twelve food at once - exactly half the shelf -
and no cook of five-food batches can cover that. The stagger is not decoration; it
is the thing the room is balanced on. Anything that re-times a hand (a save, a new
hire, a reload) should keep his tick where it was, and that has not been checked.

## 056 — The clocks are dealt, not shared

Every hand used to start on tick 0, which meant four meals landing on one click and
twelve food leaving a shelf of twenty-four in a single go - exactly the failure #055
caught. Nobody else could cover that, least of all a cook making five at a time.

`StaggerHungerClocks()` now runs at the end of the boot, and `DealHungerClock()` runs
for each new signing at a port. A hand is dealt a tick somewhere in his own twenty-
five-click round and a counter to match, `hungerFloor + risePerClick * tick`, so he
starts part-way round his cycle instead of at the top of it and everything about him
agrees. The draw is `std::random_device` per hand, so it is not the same watch twice.

First voyage dealt: Rell 18, Jax 8, Bo 14, Suri 5 - hunger 26.6, 14.6, 21.8, 11.0.
A second voyage dealt a different set, checked. Over 200 clicks on the dealt clocks:
peak hunger 33.8, shelf never below 3. `failures=0` on `/home/user/probe/clocks`.

The shelf running out is not this. Ten runs of 400 clicks all ended dry with hunger
at 100, because the galley carries 21 dry rations at boot - 105 food, plus 24 on the
counter, 129 in all - and four hands eat 0.48 food per click. That is roughly 270
clicks of food aboard, and no clock can change it. Replenishment is the ration line,
not the stagger.

The save system already stores `hungerTick` and `hunger` per hand, so a reload keeps
whatever a hand was dealt. Not proved on this branch: a save-and-load round trip.

## 057 — Two refits the yard was missing

The station already sold bay, bay2, drive, an O2 rack to 15 canisters, two sensor
tiers and a hull service. Two of the owner's three examples were not for sale: a
bigger galley counter and a filter charge that lasts longer. Both are now in
`BuyUpgrade` and on the yard's list.

GALLEY, 400 sh: `mess_.capacity` 24 -> 34. The ceiling is what the cook fills to, so
the refit buys buffer, not speed: two rounds and five-sixths instead of exactly two,
about seventy clicks more food on the shelf. `mess_.capacity` is already in the save,
so it survives a reload with no extra plumbing.

FILTER, 600 sh: `rules_.life.lifeMax` 200 -> 300. One charge is half again as long,
and because the bank, the O2 screen, the description and the save all read that one
number from the rules, they all move together. `lifeMax` is in the save already.

THE SCANNER NEEDED NOTHING. `SensorTier()` already reads the scanner upgrades: tier 1
opens the first locked entry and scans deep inside a region, tier 2 opens every
locked survey and reaches any region. The yard's own wording says so. Nothing added.

Probe `/home/user/probe/upgrades`, docked at a port with 5,000 sh: galley bought,
ceiling 24 -> 34; filter bought, charge 200 -> 300; a charged bank then reads 300,
not 200; buying the galley twice is refused; purse 5,000 -> 4,000. `failures=0`.
`StationScreen.cpp` and `Economy.cpp` both pass `-fsyntax-only`.

Not proved: that the extra counter actually delays starvation in a long run, and a
save-and-load round trip of either refit. Both are arithmetic the probe implies
(34/12 rounds, lifeMax read from rules) but they were not run end to end.

## 058 — Dead delivery buttons, tutorial under the pot, and four washed-out themes

DELIVERY. The row was live and the press did nothing. `AcceptTrade` loads a
delivery into the hold and refuses when it will not fit, but the station screen
never asked: it set `enabled = !accepted` and let the player press a button that
could only fail. Thirty ports measured, 61 refusals out of 83 delivery rows, every
one of them a full hold. `TradeOffer` now carries `fits` and `blocked`, the screen
computes `capacity - used - reserved` against `crates x volume`, and the row goes
dark with "Hold full: needs X m3, Y m3 free" instead of standing there pretending.
Freelance rows load nothing, so they stay open. Cost label reads "no room", not
"1 click". Probe `/home/user/probe/delivery`, rebuilding the screen after every
accept: 22 consignments taken across 21 ports, 0 rows that lied (`failures=0`).
The core refusal is untouched: the screen now agrees with it rather than the other
way round.

MESS. The Galley Crew panel carried a four-line paragraph explaining the whole
cook, and the Cooking panel repeated the automatic threshold that the Galley Crew
line already gives. Both cut: the note is one line - "Cooks below 10 food, one
ration a batch, until the 24-food counter is full." - and "Auto-start" is gone from
the machine panel. The starvation note lost its "slow on purpose" aside.

THEMES. The four optional themes were pale washes: `PaletteForId` replaced six
roles with near-white surfaces and left classic's parchment as the only one with
any depth. They are now the same depths in their own colour: classic's role table
is turned to the theme's hue - void 196 (petrol), aurora 148 (mint), nebula 284
(violet), ember 20 (copper) - and the saturation is deepened to 1.15x, lightness
untouched, so nothing that read on parchment stops reading. Terracotta, sage and
amber are never turned; they mean something.

Probe `/home/user/probe/themes`, contrast and saturation for all five:

    theme    ink/pan ink/con tit/tbg mut/pan richness
    classic    10.44   12.36    6.93    3.95     0.52
    void       10.16   12.04    6.56    3.84     0.60
    aurora     10.11   11.38    6.72    3.63     0.60
    nebula     10.00   13.29    6.22    4.26     0.60
    ember      10.09   12.50    6.47    4.00     0.60

Every theme is at least as rich as classic and none lost contrast; `failures=0`.
What is not proved: how any of it looks. No raylib here, so the boolean is "as deep
as parchment by the numbers", not "I have seen it".

## 059 — The delivery buttons, said plainly

A delivery is not a promise to fetch something later. The station loads the
consignment into the hold the moment the contract is signed: the crates are in your
cargo bay before you undock, bound to that one buyer.

So a delivery needs free space right then. The ship starts with cargo already
aboard - about 32 of its 40 m3 - and the first consignment signed usually fills
what is left. Every delivery row below it was still drawn alive, because the screen
only greys a row that has already been accepted. Pressing one did nothing at all,
with no line saying why.

Measured: 83 delivery offers across the 30 stations, 61 of which could not fit.
That is most of them, which is why it looked like the buttons were broken rather
than the hold being full.

The rows now do one of two things: live, or dark with "Hold full: needs X m3, Y m3
free. Sell or unload first." Nothing was changed about the rule itself - the ship
still must have room - only about whether the screen admits it.

Still the owner's call, and this entry does not decide it: whether deliveries should
be that heavy at all. Lighter consignments would let two or three be signed on one
sailing, at the cost of the hold mattering less.

## 060 — Deliveries stay heavy, and what a freelance run actually is

DECIDED BY THE OWNER: a delivery keeps needing real space in the hold. If the ship
cannot take it, it is not a job for that ship - and bigger hulls are coming later,
which is when more of a board opens up. #058's greyed rows are the whole fix; the
weights do not change.

FREELANCE, explained because the logbook was not clear and the owner asked. A
delivery is loaded by the station, so it needs space at the moment of signing. A
freelance run loads nothing, because the player buys the crates at this station's
own counter and carries them: the contract is the promise, the purchase is the work.

Measured, six freelance runs as drawn:

    station       good                hold    buy here    margin
    auriga-port   greens x2           18 m3    ~605 sh      88 sh
    bryce-port    parts x1             6 m3    ~907 sh     269 sh
    fallow-port   greens x2           18 m3    ~302 sh     420 sh
    fallow-port   medicine x2          8 m3    ~691 sh     125 sh
    gull-port     water x2            12 m3    ~422 sh      58 sh
    gull-port     parts x1             6 m3   ~1109 sh     151 sh

So a freelance row asks for capital first and hold second: 300 to 1,100 shiltes up
front for a margin of 58 to 420. A thin purse can take none of them, and the row
does not say so - the same lie #058 removed from the delivery rows, only deferred to
the counter.

NOT DONE, and it is the owner's call: grey a freelance row that the purse or the
hold cannot carry right now, with the number it needs. It is the same shape as the
delivery fix, but it is a design decision - a run you cannot afford today may be the
one you save for - so it waits for a word.

A trap worth recording: the first version of this measurement printed an `int` with
`%.1f` and produced "604.8 m3" for an 18 m3 consignment. The format string, not the
game, was wrong; every freelance number above is from the corrected run.

## 061 — Freelance rows stay lit and say what they cost

DECIDED BY THE OWNER: a freelance run the player cannot afford stays pressable. The
game informs, it does not take the player by the hand - the same rule the filter
hand follows when he waits for the air his training allows.

So a freelance row now carries its own arithmetic. Real row from the board:

    Dawn Auriga 3 - 2 crates of Hydroponic produce - needs 18 m3 and 606 sh
    Buy the lot here first; about 88 sh over your cost there. Hold 8 m3 free;
    purse 340 sh. Short 266 sh. Needs 10 m3 more room.

The cost is `GoodCounterPrice()`, a new const reader on Simulation that charges one
crate exactly the way `BuyGood` charges it - `ceil(round(value * port factor) * 1.2)`
per crate, times the crates - so a label and a purchase can never disagree. The free
hold is `capacity - used - reserved`, the same three terms the delivery gate uses.

Nothing greys. `fits` stays true for freelance, `AcceptTrade` is untouched, and the
row with 0 sh in the purse still signs: the player may be saving for it.

Probe `/home/user/probe/freelabel`, all 30 stations: 32 freelance rows, 0 greyed, 0
without the label, and 0 refused presses with the purse set to zero (`failures=0` by
the probe's own three counts).

Not proved: that the numbers read well at a glance in the real row on screen - the
row is one line in the build I cannot see, and "needs 18 m3 and 606 sh" may be a
long line at small text sizes.

## 062 — How this run went, for whoever comes next

The owner asked why this one worked after several that did not, so it is worth
writing down rather than leaving to luck.

1. HE WAS THE SPEC AND HE WAS THE CHECK. Every mechanic this run was settled by his
   memory plus arithmetic, not by what the code already said. When the two disagreed
   - "the navigator is about travel, not fuel", "the meal is thirty points", "hunger
   was never real" - the code was wrong and the logbook was wrong, and the work was
   to prove which. Defending what was already there would have lost every one.

2. NOTHING WAS CLAIMED THAT WAS NOT RUN. A probe, a count, a table before a sentence
   that says "works". Four of those runs caught me: the ladder probe that set a job
   id that does not exist, the sync-clock probe that starved the shelf through its
   own doing, the `%.1f` on an `int` that reported a 604 m3 crate, the delivery probe
   that pressed a stale screen. Each was a wrong claim found before the owner read
   it, and each went into this book as a trap.

3. THE GAPS WERE NAMED OUT LOUD. No raylib, so painting is unverified; the Catch2
   suite never ran; the cleaner's job is unknown and was not invented; the trained
   filter hand's "earlier" is expressed but not observed. Saying that is what makes
   the rest of it believable.

4. SMALL COMMITS, ONE IDEA EACH, IN HIS WORDS. "Click", never "action". The room's
   own button, not a parallel path. The bottom strip stays deleted. Those cost
   nothing to honour and everything to get wrong.

The hard part was not the code. It was refusing to be confident about anything that
had not been measured, and refusing to be loyal to code the owner did not write.

## 081 — The boards tripled, and the sector became a disc

The reference 638x278 is a paper size, not the size of the world. Changed, at the
owner's instruction:

  system board   638 x 278  ->  1914 x 834   (kSystemBoardScale = 3)
  sector board   3828 x 1668 -> 11484 x 5004 (kSectorBoardScale = 6 -> 18)
  galaxy board   2552 x 1112 -> 7656 x 3336  (kGalaxyBoardScale = 4 -> 12)

Every authored body inside a system is multiplied by kSystemBoardScale so nothing is
left at the old edge, and the local orbital rings, their separation and their insets
scale with them.

More objects, not only more board:
  systems per region   24 -> 64  (48 authored + 24 regions x 64 = 1584 systems)
  bodies per system    13..19 -> 39..59 extra per system, Kessler 11 -> 33

The sector is a disc: each region's territory is pulled onto one ellipse centred on
the board, so the outer regions sit on the rim and no system lands in a corner.
Territories grew to match (spread 370x175 -> 1110x525, spacing 46..20 -> 138..60), and
galaxies keep their distance on the wider board (210..120 -> 630..360).

Zoom ladders opened to reach the new sizes: sector/galaxy 0.3..30, local 0.6..7.5.

CHECKED: cpp/data/ChartExpansion.cpp, cpp/core/Action.cpp, cpp/core/SaveSystem.cpp all
compile clean with -Wall -Wextra against the pinned raylib/imgui/nlohmann headers.
NOT CHECKED: generation cost and the drawn result. 1584 systems at three times the
bodies is about 7.5 times the work of the old chart; start-up time is unmeasured.

## 082 - the old core joins the mix, and the four dials

The forty-eight systems and seven regions in `assets/data/chart.json` were the layout
of an older game. They had no hand behind them and no authority over this one, so they
are not read any more: `sectorBodies` and `bodies` are skipped by the loader, the star
table (`AuthoredStarCatalog()`) is gone, and every system, world, dock and lane is
drawn. Regions, the name pools and the market stay - they are what the generator draws
from.

Consequences, all of them in the code:
  - the home system is the first system drawn, not a name in the source
    (`Catalog::homeSystem`); the voyage opens there and in that system's region
  - a region's hub is whichever of its systems was laid down first
  - `Navigation`, `Catalog::BodySystem` and the save's fallback all answer with
    `homeSystem` instead of "sec-kessler"

The regions did not survive the cut as anchors. All seven sit within about thirty pixels
of the galaxy centre - they are a knot, because their positions are the old 638x278
layout - so forty-one territories piled onto the centre and the late ones could not place
their systems (`gal-halo` got six of sixty-four, `gal-rift` eleven). Territories are now
laid out by the disc itself: ordered by distance from the galaxy centre, then placed on a
sunflower spiral (golden angle, square-root radius) stretched onto the board's ellipse.
The core keeps the middle, the outer galaxies take the rim.

The four dials live in `Catalog` and in `Options`, are saved with the voyage, and are
drawn as four rows under "The chart" in the settings sheet. A dial reports when the hand
lets go of it, not on every step of a drag: `Simulation::SetChartDials()` redraws the
frontier and starts the voyage again on the new sky, which cannot be done in place
because every drawn id names a different place afterwards.

  outer galaxies       0..64      default 34
  systems per region   8..96      default 64
  chaos                0..100     default 50, fully left = chaos
  stations             0..100     default 100, the share of systems that keep a dock

MEASURED with a probe of the data layer alone (`Catalog::LoadFromAssets`, then
`ExpandChart`):

  loaded   0 systems, 0 bodies, 7 regions, 30 ports (the old ports are pruned with
           their bodies)
  drawn    2624 systems (64 in each of 41 regions), 141344 bodies, 3102 ports,
           3102 stations, 4840 sector lanes
  home     sec-gen-1 "Nocturne Ladder" in gal-core, 52 local bodies
  0 ports without a body, 0 systems without a body, 0 systems left undrawn

CHECKED: cpp/data/{Catalog,ChartExpansion,GalaxyGen}.cpp, cpp/core/{Action,Navigation,
SaveSystem}.cpp, cpp/ui/{Popups,Draw}.cpp all compile clean with -fsyntax-only.
NOT CHECKED: the drawn result (cannot see it), start-up cost of 141344 bodies, and the
test suite - `cpp/tests/galaxy_gen_tests.cpp` still calls `ship::AuthoredStarCatalog()`
and the nav/catalog tests still name `sec-kessler` and `sec-vance`; they will not build
until someone says what they should assert now.
TWO BEATS LEFT DEAD: the scan lines keyed to "halvan" and "echo-site" in
`Navigation.cpp` were bodies of the old core, so the two signal steps can no longer fire.
Left in place rather than rerouted or deleted.

## 083 - the old regions become blobs between the galaxies

Entry 082 gave the seven old regions a territory on the disc like every drawn galaxy,
and they filled it badly: two of them could not place their sixty-four systems. They
are not territories. A territory belongs to a galaxy that was drawn; the seven are
leftovers, so they hang in the gaps instead.

  territories   34 drawn galaxies, one each, on the golden-angle spiral across the disc
  blobs         7 old regions, `systemsPerRegion / 6` systems each (ten at the default),
                spread 200x95 around their anchor, packed from a 96 px separation
                rather than a territory's 138 px

Where a blob hangs is decided after the territories are laid down, because the gaps are
only real once the systems are: a candidate grid over the ellipse takes the point
furthest from every drawn system, then the next, and so on - widest gap first. The same
rule runs on the galaxy board in `PopulateGalaxies`, so the seven are small clusters
hanging between the galaxies there as well, not a knot at the centre. Territories are
drafted before blobs, so the first system drawn is still the ship's home and it now
belongs to a galaxy.

MEASURED (data layer only):

  systems   2246 over 41 regions - 34 territories at 64 each, 7 blobs at 10 each
  bodies    120970, ports 2657, stations 2657, no orphaned port, no system without a body
  home      sec-gen-1 "Nocturne Ladder" in gal-gen-3
  blobs     radius 161..204 px; nearest other system 86..130 px from a blob centre
  blob placement on the galaxy board: 553..932 px from the nearest galaxy centre
  drawing the whole chart took 1.63 s wall clock in a -O2 probe of the data layer alone

CHECKED: cpp/data/{Catalog,ChartExpansion,GalaxyGen}.cpp, cpp/core/{Action,Navigation,
SaveSystem}.cpp compile clean with -fsyntax-only.
NOT CHECKED: the drawn result. The disc is crowded enough that a territory's fringe can
reach into a blob; the closest two systems anywhere are 48 px apart, which is the floor,
not a collision. Test suite still not building - unchanged from 082.

## 084 - the chart builds only what the window can show

The chart was fine on paper and 3 fps in the window. Measured first, with a probe of the
data layer:

  local bodies   120970   137.9 MB   (1195 bytes each)
  systems          2246     1.5 MB
  regions            41     0.04 MB
  ports            2657     0.46 MB
  total chart             139.8 MB

So the weight is entirely in the local bodies of systems the ship has never been to, and
the frame cost is separate: `LayerBodies()` hands the builder every system on the layer,
and the builder projected and labelled all of them every frame with no culling at all.

This entry fixes the frame cost only. `BuildNavChart()` now projects a body before it
builds a view for it and drops anything more than 180 px outside the window - the same
kind of test `NavRouteDots()` has always done for faint lane segments. Nothing else
changed, so the layer still contains everything; it just is not all built.

CHECKED: cpp/ui/NavScreen.cpp compiles clean with -fsyntax-only.
NOT CHECKED: the frame rate on the real machine; the number is his to read.
NEXT, not done yet: the 137.9 MB. Local charts are drawn for every system at start-up
(`ExpandLocalCharts`) although the ship visits one at a time, and every local chart is
already deterministic from `Rng(HashOf(system.id))`, so a system's worlds can be drawn
when it is entered instead of all of them in advance. What stops that from being a
ten-line change is everything that names a body in a system the ship has not entered -
contract targets, extraction jobs, station boards.

## 085 - the third layer is forty-one dots

The galaxy layer was drawing the region bodies and then every system that has a place
on the galaxy board - twenty-two hundred and eighty-seven bodies for a layer that is a
picture, not a place to set course. It now draws the regions only: forty-one dots.

Measured, layer two, with a 1600x900 window and the sector board at 11484x5004 holding
2246 systems - how many systems the box actually shows at each step of the ladder:

  zoom  0.3   world box 5333x3000   systems in the box  852
  zoom  0.5   world box 3200x1800   systems in the box  294
  zoom  1.0   world box 1600x 900   systems in the box   87
  zoom  2.0   world box  800x 450   systems in the box   28
  zoom  4.0   world box  400x 225   systems in the box    7
  zoom  8.0   world box  200x 112   systems in the box    1

So the wide end of the ladder is the whole cost: 852 bodies at 0.3 against 87 at 1.0.
A floor of 0.5 would cap the box at about 294; 0.6 at about 200. Not changed here -
the floor is a decision, not a bug.

CHECKED: cpp/ui/NavScreen.cpp compiles clean with -fsyntax-only.
NOT CHECKED: the frame rate; nobody has seen this run.

## 086 - a redraw draws again, it does not stack

Pulling a dial rebuilt the sky on top of the sky. `SetChartDials()` asks `ExpandChart()` to
run again by clearing `chartExpanded`, and `ExpandChart()` starts by stripping what the
previous pass made - but `StripGeneratedChart()` only ever removed bodies, ports and
systems. The galaxies `PopulateGalaxies()` draws were left standing and drawn again
around the core on every pass, and the lanes of the removed systems and galaxies were
left dangling.

Measured through the real settings action (`PressPopupAction("settings", ...)`), before:

  start                 systems 2246   regions  41
  galaxies 34 -> 50     systems 3610   regions  91
  stations 100 -> 40    systems 4110   regions 141

and after:

  start                 systems 2246   regions  41
  galaxies 34 -> 50     systems 3270   regions  57
  stations 100 -> 40    systems 3270   regions  57

57 is seven blobs and fifty galaxies - the number the dial asks for - and it stays there.
`StripGeneratedChart()` now also removes the drawn galaxies (`gal-gen-*`) and prunes any
sector or galaxy lane whose end no longer exists.

The four sliders also commit on a plain `IsItemDeactivated()` rather than
`IsItemDeactivatedAfterEdit()`: a release that ImGui does not count as an edit can no
longer be swallowed, and `SetChartDials()` compares the four numbers itself, so a click
that moved nothing still draws nothing.

CHECKED: cpp/ui/{Draw,NavScreen,Popups}.cpp, cpp/data/GalaxyGen.cpp and cpp/core/Action.cpp
compile clean with -fsyntax-only against the pinned raylib 6.0 / imgui 1.92.7 / rlImGui
Raylib_6_0 headers; the settings action was exercised headlessly with the data and core
layers and the numbers above are its output.
NOT CHECKED: whether that was the snap he saw. The value was always stored; what was not
stored was a sky of a fixed size.

## 087 - the action band explains nothing

Two paragraphs sat under the target panels and ran across them: what standing watch
costs, and what committing and a deep scan cost in clicks. They were not written on this
branch - my only edits to `NavScreen.cpp` here are the culling and the third layer - but
they are prose in a place that wants one line, they overlap the panels above and below,
and they are gone.

What stays is the note that says why a button is off: no energy, no drive, not enough
fuel, a crossing already under way. Those are answers to "why can I not press this", and
they are one line each.

CHECKED: cpp/ui/NavScreen.cpp compiles clean with -Wall -Wextra -fsyntax-only.
NOT CHECKED: the picture. Whether the band now sits in its box is his to see.

## 088 - no text in the band, at all

The four one-line notes went too. The band under the target panels now holds the rows and
nothing else; there is no text in it that is not a row. `model.notes` is no longer filled
from `NavScreen.cpp` at all, so the band's note list is always empty.

CHECKED: cpp/ui/NavScreen.cpp compiles clean with -Wall -Wextra -fsyntax-only.
NOT CHECKED: the picture.
NOT TOUCHED: the station windows have notes of their own (`StationScreen.cpp`); they are
a different area and were left as they were.

## 089 - the stand watch row is out of the tail

The row is gone from the navigation tail. The tail is sized from the rows it holds
(`tailH = screen.commands.size() * (LineHeight + 12)`), so with no target selected it is
now nothing at all and the Target Actions band moves up under the panels. What a row can
still be: "View <region> sector chart" when a region is the target, and "Dock at <port>"
when the ship is undocked beside one.

Standing watch is therefore no longer reachable from the navigation tail. The action
itself (`stand-watch`) and its handler are untouched; nothing in the UI offers it now.

CHECKED: cpp/ui/NavScreen.cpp compiles clean with -Wall -Wextra -fsyntax-only.
NOT CHECKED: the picture.

## 090 - every sentence I wrote into the UI is out

Added up what I put on screen on this branch. Every string literal I added under `cpp/ui`:

  the four dial names     "Outer galaxies: %d"  "Systems per region: %d"  "Chaos: %d%%"
                          "Stations: %d%%"    (the names were his)
  a heading               "The chart"
  a sentence              "Changing one draws the whole chart again and starts the voyage over."
  a confirmation          "The chart has been drawn again."

The heading, the sentence and the confirmation are gone. The four dial names are what is
left, because a slider with no name is not a dial.

Nothing else in `cpp/ui` was added by this branch. The rows the navigation screen can
still push - "View <region> sector chart", "Dock at <port>", and the Start Travel / Run
Deep Scan buttons - are not mine; they were there before, and the last two commits on this
branch only removed rows and prose from that screen, never added one.

CHECKED: cpp/ui/{Draw,Popups}.cpp compile clean with -Wall -Wextra -fsyntax-only (one
pre-existing unused-variable warning in PaintTaskScreen, untouched).
NOT CHECKED: the picture.

## 091 - the sliders snapped back because they reported the wrong number

Reproduced with a headless ImGui context and synthetic mouse input, driving the exact row
the settings sheet draws:

  frame: x=5   down=0 local=34 active=0 deact=0    settle
  frame: x=200 down=1 local=32 active=1            press on the track
  frame: x=240 down=1 local=39 active=1            drag
  frame: x=290 down=1 local=48 active=1            drag
  frame: x=290 down=0 local=34 active=0 deact=1    release

The row committed, on release, with 34 - the model's own value. By the release frame ImGui
has stopped writing into the row's local variable, so the local is re-initialised from the
model; the drag is gone by then. `IsItemDeactivatedAfterEdit()` was therefore reported as
true and carried no news, and `SetChartDials()` saw "no change" and drew nothing, which is
the thumb returning to where it started.

The row now remembers the number while the hand is on it, in ImGui's own per-item state
storage, and reads that back on release. Same probe, after:

  frame: x=290 down=0 local=34 active=0 deact=1 afterEdit=1
  committed yes, value 48 (stored 34)

Four rows are now one loop over a small table, so the pattern cannot drift between them.

CHECKED: cpp/ui/Draw.cpp compiles clean with -Wall -Wextra -fsyntax-only (one pre-existing
unused-variable warning, untouched); the commit value was proved with the headless ImGui
probe above, not by reading the code.
NOT CHECKED: the real window. Whether the redraw freeze that follows a release is short
enough to feel like a dial rather than a stall.

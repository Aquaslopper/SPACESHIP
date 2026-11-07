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
- **Timestamp:** 2026-11-07 02:31 UTC
- **Task:** owner asked to make the nav map bigger -- "500 and more" stars, systems, galaxies, following docs/ROADMAP_TO_SPACEMAP.md. Investigation only; no game file was edited in this reply.
- Branch state verified first: this session works on arena/3b200ede-spaceship at 6537d4f (== origin/main tip). PR #23's build fixes are already in this checkout: CMakeLists.txt has no DOWNLOAD_EXTRACT_TIMESTAMP keyword left (one comment mention only) and build.yml has no node --test line. Nothing re-applied.
- Cause of "maybe 10 or so on the nav screen" located, and it is not the board size: Simulation::CurBodies() (cpp/core/Navigation.cpp:79) is a filter -- galaxy layer lists all 7 regions, sector layer only systems with region == map_.sector (~7 of 48), system layer only bodies of the current system (~5 of 243). The whole authored universe sits in one 638x278 space, so enlarging the board alone spreads the same dots further apart.
- Measured authored content from assets/data/chart.json: 243 bodies (50 star, 93 planet, 48 moon, 30 station, 19 site, 2 belt, 1 origin), 48 sectorBodies, 7 galaxyBodies, 43 sectorLinks, 9 galaxyLinks, 30 ports, 7 goods; bodies per system min 5 / median 5 / max 8.
- Median local leg 157 px = 9.8 actions at pxPerAction 16; nearest region gap median 97 px. Scaling the board by k needs k^2 objects to keep density: x4 means ~768 systems / ~3900 bodies, or ~500 systems / ~2500-3000 bodies for the owner's stated target.
- The roadmap's "super easy trick" already exists in the codebase: struct Seeded (cpp/ui/NavScreen.cpp:33, seed*9301+49297, "the same seed draws the same sky forever"), used for the nine drifting rocks (:506) and cluster dots (:438); Fx.h:43 notes a seeded generator is what lets a test replay a drift exactly. Phase 4 is this same generator applied to the chart instead of to decoration.
- Confirmed the safe scaling direction: MetricsFor() reads only raw x/y, pxPerAction and lyPerAction -- w/h/vw/vh/cx/cy/zoom/zmin never enter it, so viewport and zoom changes cannot move a single travel cost.
- Confirmed the constraint that rules out authoring: catalog_tests.cpp locks c.sourceBytes == 706020 plus map.w/h/cx/cy/pxPerAction/lyPerAction/zmin/zmax, the same numbers repeat as MapView defaults (cpp/core/GameState.h:275-295), and tools/export-data.cjs -- the only sanctioned regeneration path -- no longer exists. assets/data/chart.json therefore stays byte-identical; all new content must be generated in C++ at load.
- Owner's answers recorded: (1) will show an image before the layer question is settled; (2) generated stars DO carry mini-stations trading a handful of goods; (3) longer legs are acceptable because "longer distance will always have something in between, debris or moons... something" -- corridor content per link keeps every hop in the reference's 3-15 action band, so no fuel/O2/food constant needs touching.
- Mini-station spec derived from existing data (no new fields): ports is a dict id -> {name, prices{kind -> multiplier}, tip}, resolved by Catalog::FindPort(map_.here); LotPrice = round(good.value * prices[kind]); BuySupply/hiring/contracts all gate on HerePort() != nullptr. A handful of goods = 3-4 kinds priced off portPrices[region kind].
- Known precondition for any option: NavScreen.cpp iterates every body for dots, lanes and labels with no viewport culling (labels flip at map.vw*0.78) and draws into a software raster sized map.vw x map.vh -- at ~3000 bodies it will crawl, so culling belongs in this change, not after it.
- Open consequence to raise with the owner: 30 ports -> hundreds changes the contract/commission pool, since visit goals iterate ports (cpp/core/Economy.cpp:488) and a thin region already gets a nearby alternative (:499).
- Deliberately untouched: assets/** (chart.json byte-identical by requirement), cpp/** sources, tests/**, rescue/**, AGENTS.md. No edit was made before the layer decision arrives with the owner's image.

### Entry #007
- **Timestamp:** 2026-11-07 02:39 UTC
- **Task:** owner sent two images (current game vs AI mockup) -- "so i cant see them? no, thats not nice!", wants every planet visible, deep scans that cost more clicks the deeper they go, and asked for a live test: can the ship jump to another galaxy? No game file edited in this reply.
- Built the headless harness on arena/3b200ede-spaceship (cmake reinstalled in the fresh sandbox, configure with GUI=OFF TESTS=OFF, target spaceship_console) and ran scripted voyages; state read back from the console's END trace JSON.
- VERDICT: inter-region travel WORKS. Script "stat fuel 9999 / layer sector / sector gal-core / target sec-lumen / travel / 21x watch / ack travel" produced "Arrived at Lumen Prime." with map here=lumen-star, layer=system, sector=gal-core, contacts "Charted objects in Lumen Crown". All 48 systems carry a non-empty entry, so every one of them is enterable, in all 7 regions.
- Crossing cost measured: the gal-inner -> gal-core hop cost 20 actions and 20 fuel (start is fuel 74, so one third of a fresh tank); MetricsFor took the cross-region branch (region gap x1.3 + 150, then px/24+5).
- DEFECT 1 (this is the "broken" feeling): on the GALAXY layer a region is selectable but travel is silently refused. "layer galaxy / target gal-core / travel" left navTarget "gal-core", actions 0, no feed line, no reason. Cause: Simulation::InRange returns false for type=="region" by design (you travel to systems), yet the UI offers the target anyway. NavOutOfRange() ("Distance too great. Target out of sensor range.") exists at NavScreen.cpp:167 and is used in the readouts, but the travel action never explains itself.
- DEFECT 2: a finished voyage does not arrive until acknowledged. After 26 watches the task read state "done", left 0 while location stayed "Kessler Drift - open space"; arrival fires only via Acknowledge -> ArriveAt (hook runs before the task is cleared, as the reference does). Intended, but the most likely thing a player misreads as nav being broken -- worth checking on screen.
- Cause of "i cant see them" located precisely: it is one filter, not the data. CurBodies()/LayerBodies() at the system layer keep only bodies whose BodySystem == current, so 238 of the 243 authored bodies never reach the screen; the sector layer keeps ~7 of 48. The mockup's deep field is exactly what that filter removes.
- The deep-scan ladder is already half-present in the data: ScanReadout (NavScreen.cpp:131-159) prints body.scan once scanned and body.scanLocked + " - sensors insufficient" otherwise -- the second rung exists in chart.json and is dead code because state_.scanned is a bool (RevealScan sets true; SaveSystem writes it through readBoolMap at :1033).
- Design recorded for the owner's ask: scanned bool -> depth (0/1/2), cost scanCost_ x (depth+1) with the existing -2 sensor discount, scan at depth 1, scanLocked and details rows at depth 2; save migration is backward compatible (bool true -> 1, numbers pass through).
- Increment plan proposed: (0) galaxy-layer feedback + visible "awaiting ack"; (1) decouple visibility from reachability, unlabelled muted dots for everything in the layer, names only for scanned, travel/InRange/MetricsFor untouched so every parity number holds, with viewport culling in NavScreen; (2) scan depth; (3) the 500+ Seeded generator with mini-stations and corridor content so legs stay in the 3-15 action band.
- The mockup answers the open layer question: no fourth view -- the same three layers, each populated, unknown objects drawn but unnamed.
- Untouched: cpp/**, assets/** (chart.json byte-identical), tests/**, rescue/**, AGENTS.md. Awaiting the owner's choice of increment to implement.

### Entry #008
- **Timestamp:** $TS
- **Task:** owner asked me to consult the legacy source, wants distance-scaled click cooldowns reducible by machinery bought at a port, all stars visible, cross-galaxy travel that takes longer, and asked whether the stars are placeholders ("then i got tricked by the agent"), then proposed a procedurally generated universe. No game file edited here.
- Local clone was shallow (3 commits, .git/shallow present), which is why earlier searches found no legacy file; ran git fetch --unshallow origin main -> 159 commits. This is a local-only operation, no push, no history rewrite.
- RECOVERED the reference: Spaceship.hta was deleted in 80a10b1 "remove old files (2026-11-07)"; extracted it to /tmp/Spaceship.hta (outside the tree, nothing restored into the repo without approval).
- PROVENANCE VERIFIED, owner was NOT tricked: the file is 706,020 bytes and sha256 f36ecfd94d49a614dd584a385fecf960947a35fd2b13092f075b8e46a1dfa013, matching assets/data/manifest.json source.bytes / source.sha256 exactly, and matching the value catalog_tests.cpp locks as c.sourceBytes == 706020. chart.json provably derives from the real legacy file.
- BUT the legacy authored only 5 entries in var BODIES; the rest is generated at load by function expandLocalCharts() (line 9205, invoked 9237): for each of 48 SECTOR_BODIES it calls addSystemBody() (9148) to add one outer planet and one moon, names from ORBITAL_PLANET_NAMES / ORBITAL_MOON_NAMES (12 each, indexed), profile from ORBITAL_WORLDS[i % 4], positions x = 470 + (i % 5) * 28, y = 88 + (i % 5) * 22, then rewrites the parent's "Charted bodies" to 5 (8 for sec-kessler) and sys.klass. 48 x 5 destinations = 240 plus Kessler's extras = 243.
- So the export tool ran the game and froze the generator's OUTPUT into static JSON; the C++ port inherited the result and lost the generator. The 5-slot position grid also explains the repetition the owner sees. A procedural universe is therefore a RESTORATION of legacy behaviour, not an invention.
- The deep field the owner wants already exists in the legacy and in the port as pure decoration: drawClusters() (5483) uses seed 8721 with count = galaxy 260 else 108, sector seed offset by the region index, via seeded(); the port copies it exactly at NavScreen.cpp:414-433 but emits NavDot{x,y,cls} with no id, so those stars cannot be selected, scanned or reached. Promoting them from dots to bodies is the cheap path to "all stars have to be seen".
- Cross-galaxy travel already costs more and matches the reference formula for it: HTA navMetrics (5778) computes px across regions as (toRegion - fromRegion) * 1.3 then + 150 and dist = max(6, round(px/24) + 5); the port's MetricsFor is identical, and the measured jump gal-inner -> gal-core cost 20 actions and 20 fuel of a starting 74.
- COOLDOWN WISH IS NOT IN THE LEGACY, stated plainly: ACTION_COOLDOWN_RULES (3474) = routine 5 / systems 10 / space 15 / critical 20 actions, set in commitChange (3562) as remaining = RULES[category].actions, documented as "action clock, never wall-clock time"; distance scales the TASK length, not the cooldown. Proposal: add a distance-tiered cooldown as a NEW category so the four authored ones keep their exact behaviour, and reduce it via the existing upgrade set (state_.upgrades drive/scanner/rack/bay; StationScreen.cpp:242 sells "Fit efficient drive: fuel use -20% - 900 sh").
- Scan depth is legacy-adjacent, not invented: addSystemBody writes scan:[text, reading] AND scanLocked:[type == station ? "Station manifest" : "Detailed spectral survey"], and expandLocalCharts adds scanLocked "High-resolution orbital survey" -- the second rung's content already exists and is dead text in the port because state_.scanned is a bool (readBoolMap, SaveSystem.cpp:1033).
- Plan put to the owner: A port expandLocalCharts + addSystemBody to C++ behind a scale and a voyage seed stored in the save (chart.json stays byte-identical so every locked assertion holds; new tests assert authored anchors plus generated invariants); B decouple visibility from reachability with viewport culling; C scanned bool -> depth 0/1/2 with cost scanCost_ x (depth+1) and the -2 sensor discount; D distance-tiered cooldown reduced by machinery.
- Also recommended: restore Spaceship.hta into the tree, which would make the four tests/*.test.cjs parity suites runnable again (the reason this session retired that CI line) and give agents back the source authority AGENTS.md still names. Not done: awaiting owner approval, since 80a10b1 deleted it deliberately.
- Untouched: cpp/**, assets/** (chart.json and manifest.json byte-identical), tests/**, rescue/**, AGENTS.md; the recovered HTA lives only in /tmp.

### Entry #009
- **Timestamp:** $TS
- **Task:** follow-up reading of the recovered Spaceship.hta to pin down exactly how the legacy builds its universe, before proposing the procedural design. No game file edited.
- The generator is two passes over one authored table, not per-body literals: buildGalaxyContent() (HTA 9160-9188) iterates STAR_CATALOG rows and expandLocalCharts() (9205) adds a second world plus a moon per system.
- Row contract, read from the call sites: row[0] system id, row[1] region, row[2] system name, row[3] x, row[4] y, row[5] star name, row[6] world name (a name containing "Belt" makes the body a belt), row[7] landmark name, row[8] port kind (empty means the landmark is a site, non-empty means station AND a port), row[9] tip text.
- Per row the reference emits: a system object if missing (with SECTOR_LINKS to the region hub via hub[row[1]]), a star at 100 + (i % 3) * 32 / 58 + (i % 2) * 14, a world at 390 + (i % 3) * 28 / 64 + (i % 2) * 20, a landmark at 260 + (i % 3) * 45 / 210, and PORTS[landmark] = {name, prices: PORT_PRICES[row[8]], tip: row[9] + " " + PORT_HINTS[row[8]]}.
- Consequence for the economy: generated ports come free with the rows. Prices and hints are looked up from PORT_PRICES and PORT_HINTS, which the port already holds as chart.json portPrices / portHints, so mini-stations that trade need no new data shape and no new fields.
- Positions are modulo-index grids on purpose, not an oversight: HTA 9193-9195 states "Fixed naming, positions and types keep the catalogue stable in saves". Any seeded generator must therefore draw from a seed fixed once per voyage and stored in the save, never from per-frame or per-load randomness.
- Authored object literals are only 3 to 5 in the whole 706,020-byte file (type:"star" appears 3 times, type:"planet" and "moon" zero); 243 bodies is the generated result, and the export tool froze that result into chart.json while the row table and both passes stayed in the legacy file.
- So the roadmap's trick, correctly identified: generate STAR_CATALOG rows and reuse the existing row-to-world builder to reach 500+ systems, rather than authoring thousands of bodies or inventing a parallel generator.
- Arithmetic that now closes exactly: 48 systems x 5 generated bodies (star, world, landmark, outer world, moon) = 240, plus the 3 literal Kessler entries = 243, matching sys.details "Charted bodies" 5 and sec-kessler 8.
- Plan revised and put to the owner: Phase A port the two passes behind a row generator (seeded per voyage, seed saved); Phase B decouple visibility from reachability with culling; Phase C scanned bool to depth 0/1/2 with cost scanCost_ x (depth+1); Phase D distance-tiered click cooldown as a new category reduced by the drive and scanner upgrades, flagged as a departure from ACTION_COOLDOWN_RULES.
- Offered, awaiting approval: restore Spaceship.hta into the tree from 80a10b1^, which also makes the retired node --test parity suites runnable again.
- Untouched: cpp/**, assets/**, tests/**, rescue/**, AGENTS.md; the recovered file remains in /tmp only.

### Entry #010
- **Timestamp:** $TS
- **Task:** owner (the HTA's own developer) says the stars are not seen and asked me to fix it so the port does what the HTA does. One bug found, fixed, and covered; one claim in the request could not be confirmed and is reported as such.
- Consulted the reference before touching anything. HTA curBodies() (5268-5276) filters system layer by bodySystem(b) === currentSystem().id, sector layer by b.region === MAP.sector, and returns all for galaxy -- this is line for line what Simulation::CurBodies() (cpp/core/Navigation.cpp:79) and ui LayerBodies() (cpp/ui/NavScreen.cpp:54) already do. HTA drawMapBodies() (5558) paints curBodies(). inRange (5292) and navMetrics (5778) also match the port exactly.
- Therefore the claim "the HTA shows all the stars" is not supported by the file: in the reference the system layer really does paint only the ~5 bodies of the current system, and the dense sky on the wider layers comes from decorative dots, not from the other 238 bodies (cdot-far: 108 on sector, 260 on galaxy, seed 8721; the system layer gets no far field at all). Stated plainly rather than implemented as parity.
- REAL BUG found and FIXED: the reference scatters body clusters in drawClusters() over lay.bodies (HTA 5497), i.e. the layer's whole authored array, seeded by the index into that array; the port ran the same loop over its filtered list and used the filtered index (cpp/ui/NavScreen.cpp:435). So a body carrying a cluster loses its stars whenever the layer filter hides it, and even a visible body's scatter could be seeded from the wrong index.
- Fix: added LayerArray(sim) returning the layer's authored array (regions / bodies / systems in catalogue order) and drove the cluster pass from it, matching lay.bodies and the array index. Behaviour on the shipped chart is provably unchanged: cluster is nonzero only on the 7 regions (chart.json: 7 of 7 galaxyBodies, 0 of 243 bodies, 0 of 48 sectorBodies, 142 dots total) and on the galaxy layer the filtered list and the array coincide.
- Why it matters now: the procedural chart will put clusters on generated bodies, and with the old code every one of them would have been silently dropped by the filter -- the exact class of bug where the port drifts from the reference while tests stay green.
- Regression test added to cpp/tests/nav_tests.cpp ("a cluster scatters stars for a body the layer filter hides as the reference does", [nav][chart]): gives vance-star -- a body in another system -- a cluster of 3, builds the system-layer chart, and asserts 3 dots near it, that the hidden body itself is still not on the chart, and that a rebuild yields identical positions. No existing test was modified.
- Proved the test catches the bug: with cpp/ui/NavScreen.cpp temporarily reverted the test fails on both CHECKs (clusters.size() == 0); with the fix it passes. Test name has no comma because catch_discover_tests treats commas as separators.
- Checks run locally: full cmake --build (all targets) clean; ctest 133/133 passed (was 132 before this test existed); the scripted voyage in the console still ends at Lumen Prime with map here=lumen-star sector=gal-core, so travel and the map filter are untouched; tools/check-gui-syntax.sh exit 77 (raylib/imgui/rlImGui headers are not in this fresh sandbox, so the GUI units were not syntax-checked here -- CI does that).
- Not done, deliberately, pending the owner's choice: widening curBodies/LayerBodies so every body is painted, or giving the system layer a far field. Either would make the stars visible but is a departure from the reference's own filter, and would then be what the parity tests encode, so it needs an explicit owner decision rather than a silent fix.
- The parity-preserving route offered instead: generate clusters for chart rows (the reference's own mechanism at HTA 2166 "cluster:n scatters cosmetic stars around it"), which fills the sky through a path the HTA already uses; and note that decorative dots cannot be scanned or travelled to, so "see them" and "learn about them" are two different asks.
- Untouched: assets/** (chart.json and manifest.json byte-identical), all other tests, cpp/core/**, rescue/**, AGENTS.md.

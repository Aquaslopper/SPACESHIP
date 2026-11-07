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

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

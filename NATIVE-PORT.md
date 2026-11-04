# Native C++ migration status

This branch now contains a **real native C++ Windows application**. `spaceship.exe`
creates its own Win32 windows and GDI ship/chart views and executes the simulation
in C++. It does not launch `mshta.exe`, evaluate JavaScript, embed a browser, or
read `Spaceship.hta`. The player artifact does not include the HTA.

**This is a playable migration build, not a completed feature-identical conversion.**
The reference edition remains in the repository so missing behavior can be ported
and compared instead of silently discarded. Do not describe this build as full parity.

## Implemented in C++

- Native, keyboard-accessible Windows controls, selectable compartment plan and
  chart, ship status, activity log, target lists and contextual action buttons.
- The original 21 compartments, four starting crew biographies, two ship classes,
  7 regions, 48 systems, 243 destinations and regional port/market data compiled
  into the application. Data presence alone does not imply behavioral parity.
- Committed-action clock, independent per-control 5/10/15/20 action cooldowns,
  blocked/invalid action rollback, work scheduling, powered work pausing on blackout.
- Local crossings, system jumps, fuel use, scans, survey archive and station docking.
- Fuel cells, repair work, drive wear, three canister installations, filter bank,
  atmosphere depletion, ration preparation/consumption, hunger and crew health.
- Regional cargo trading, station supplies, salvage, cargo refits, drive refit,
  larger ship purchase retaining crew, goods and voyage state.
- Crew rest, medical care, conversation, job assignment, training, hiring, wages,
  engineer/cook automation, cleaner morale and navigator fuel benefits.
- Salvage conversion projects, cooling, hull bracing, observations, sensors,
  memory archive, two-memory dreams, shared meals and bonds.
- Contracts, repeatable commissions, incident escalation/resolution, commendations
  with once-only reward claims, and Silicon Marrow decoding and both endings.
- Three independent native save slots in `%LOCALAPPDATA%\SpaceshipCommand`,
  written through temporary files with a `.bak` copy of the previous slot.
  Native load validates before replacing the live voyage.
- A portable console client using **the same simulation**, for development/testing.

## Known differences / remaining port work

- The interface is a native redesign, not a pixel-identical reproduction. The
  animated crew, detailed machine illustrations, title artwork/menu, audio,
  palettes and settings UI have not been ported. The supplied artwork is preserved.
- Arrival and scan completion are applied immediately rather than waiting for the
  original free acknowledgement control.
- Watch is deliberately repeatable in this native build to prevent permanent
  cooldown deadlocks. The original HTA's watch cooldown is unchanged.
- Travel work/fuel calculations, scans, atmosphere harm, cooking, treatment,
  training and incident resolution currently use simplified native rules. They
  are not yet a literal translation of every original formula or crew penalty.
- Crew staged multi-order sheets, autonomous desires/rest, full filter-hand
  automation, bespoke dispute-pair behavior and every training tier are not complete.
- Dream selection currently takes the oldest two available memories. Identity/
  desire presentation, anchor bonuses, combination-specific dream rewards and
  the complete relationship consequences are not yet implemented.
- The repeatable commission selector is basic; it does not yet reproduce the
  original rotating, region-aware deterministic offers.
- Contracts currently recognize previously recorded target visits/scans. Detailed
  freight/passenger lifecycle parity and all historic migration cases need work.
- Legacy handbook articles are retained as reference with a native-controls notice;
  some describe controls/settings that only exist in the HTA edition.
- **HTA save import is not implemented.** Native saves use `spaceship-native-1`
  and distinct filenames. HTA slots are never overwritten or reinterpreted.
- Native GUI is Windows-only. The portable simulation and text client build on
  Linux; there is no Linux graphical client or browser preview of the native GUI.

## Build and verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For Visual Studio, configure with `-A x64`, build with `--config Release`, and test
with `-C Release`. The Windows target is `build/Release/spaceship.exe`.

Windows CTest runs `spaceship.exe --smoke-test`: it creates the real native UI,
checks controls, dispatches an action and navigates pages. This is not a substitute
for a manual visual/accessibility/playthrough check on a player's PC.

`tools/export-native-data.cjs` is a development-only data extraction tool. The
resulting C++ header is checked in; building/running the game does not require
Node or the HTA. CI checks the header is current and runs the untouched reference
edition's Node regression suites as well as native C++ tests.

The only vendored library is nlohmann/json 3.11.3 (MIT; license in
`cpp/vendor/LICENSE.json`). Windows uses the static MSVC runtime. No runtime
network requests, external game-data files, browser engine, or scripting runtime
are required.

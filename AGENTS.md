# Agent and contributor guide

## Project shape

- `Spaceship.hta` is the self-contained game: markup, CSS, inline JavaScript, game data, UI and save/load logic live in one file. It runs as a Windows HTA and is also used by the browser preview. There is no build step, package manager or external runtime dependency.
- `README.md` is the player-facing feature and development guide.
- `tests/*.test.cjs` are dependency-free Node tests. They extract the inline script and exercise it in a VM with lightweight DOM stubs.
- The supplied PDFs and artwork are design/reference assets; preserve them unless a task specifically requires changing them.

## Gameplay clock — do not regress this

The game is **not real-time**. Waiting, reading, opening a window, selecting a map target and other explicitly free interactions do not advance the world. A committed action is the only clock tick.

`performAction(detail)` is the central action boundary. It increments `actionCounter`, advances existing button cooldowns once, then calls `tickWorld()`. Keep those steps in that order. Do not add intervals, timeouts, wall-clock timestamps or a shared click debounce to control gameplay. A blocked button repeat is not an action and must not tick the world or any cooldown.

## Per-button action cooldown contract

- Every repeatable, action-costing button has its **own** stable cooldown key and its record lives in `STATE.actionCooldowns`.
- Current tiers are Routine **5 actions**, Ship systems **10**, Space operations **15**, and Critical **20**. The counts are other committed actions, not seconds.
- An action ticks all cooldowns that were already active. Only after the button callback has actually committed an action (`actionCounter` increased) does that source button receive a fresh cooldown. Its own initiating action does not count toward its cooldown.
- Other committed actions anywhere in the game advance the countdown. Idle time and rejected repeats do not. **Stand watch - advance ongoing work** is a space-operation action and must not become a bypass for rapid repeat work.
- Free controls (navigation, selection, reading, settings, acknowledgement and other explicitly free UI) must not acquire a cooldown or spend an action.
- Use the existing control wrappers: `commandRow`, `wireAction` and `buildProgressButton` / `onProgressClick`. Keep cooldown counters in state, never only in the DOM; redraw and load must re-render the remaining count.
- Preserve save compatibility: a legacy save may omit `actionCooldowns` and should migrate to an empty object. Validate present records against the known categories and integer action ranges.
- If an action is currently unclassified, choose its tier deliberately (or pass `cooldownCategory`) rather than adding a global throttle. Keep the user-visible tiers and behavior documented in `README.md` and covered by tests.

## Editing and verification workflow

1. Read the relevant README and nearby game logic/tests before changing gameplay behavior. Keep the game offline and self-contained; do not add network calls or dependencies without an explicit requirement.
2. Keep application JavaScript compatible with the HTA's legacy browser host: use the existing ES5-era patterns and avoid relying on modern browser APIs in game code.
3. When changing action behavior, test independent controls, the action-count tick order, blocked repeats, redraw and save/load migration. Update tests and this guide when invariants change.
4. Run the full suite from the repository root:

   ```sh
   for test in tests/*.test.cjs; do node "$test"; done
   ```

5. Check inline JavaScript syntax and whitespace before finishing:

   ```sh
   node - <<'NODE'
   const fs = require('fs');
   const text = fs.readFileSync('Spaceship.hta', 'utf8');
   const match = text.match(/<script type="text\/javascript">([\s\S]*?)<\/script>/);
   if (!match) throw new Error('inline game script not found');
   new Function(match[1]);
   console.log('Inline JavaScript syntax OK');
   NODE
   git diff --check
   ```

## Native C++ target and PR checks

- The C++17 native target is built with the root `CMakeLists.txt`; portable gameplay code belongs under `cpp/`.
- Keep the action clock discrete: each committed action increments the counter, ticks already-active button cooldowns, then advances ongoing work. The source button's new tier cooldown starts afterward. Invalid actions and blocked repeats do not tick.
- Build/test locally with `cmake -S . -B build`, `cmake --build build --parallel`, then `ctest --test-dir build --output-on-failure`. The Windows workflow must compile `build/Release/spaceship.exe` and publish it as an artifact.
- `.github/workflows/native-cpp.yml` also runs the existing Node regression suites. Keep both the native tests and the legacy-game tests passing during the incremental port.

## Native C++ migration (supersedes the HTA launcher)

- Windows `spaceship.exe` is a real native Win32/GDI application in `cpp/src/windows_native.cpp`. Never replace it with a browser/HTA launcher. No runtime HTA, JavaScript, Node, network access or external data is required.
- `Voyage` in `voyage.hpp` / `voyage.cpp` / `voyage_crew.cpp` / `voyage_views.cpp` is the shared simulation for GUI, portable console and tests. Do not create another independent mini-game with different rules.
- Preserve `Spaceship.hta` as the reference during migration. See `NATIVE-PORT.md` for explicit parity gaps; do not claim the conversion is complete while those remain.
- Native watch obeys its own 15-action cooldown, just like the reference space-operation category. No production bypass is allowed. Canonicalize global control keys so unused arguments cannot evade cooldowns. Native committed actions increment the counter, tick existing cooldowns, then advance the world; new work and the source cooldown start afterward. Blocked repeats and free view/settings/selection controls must never advance time. Label cooldowns by control and keep them distinct from work progress. Unit world traces explicitly use validated test fixture clock drivers; separately test real repeated clicks and 15 different gameplay clicks without bypasses.
- Native save format is `spaceship-native-1`; three UI slots live in `saves/` next to the EXE, falling back to `%LOCALAPPDATA%\SpaceshipCommand\saves` for a read-only install. Load checks the old per-user flat directory only when the new slot is absent. Never move/delete old slots or touch HTA slot files. `voyage_import.cpp` translates supported HTA snapshots transactionally (UTF-8 / BOM UTF-16LE); unknown tasks and unmapped live cooldowns must fail explicitly rather than lose state. Never overwrite the imported source.
- `tools/export-native-data.cjs` extracts authored data into the committed `native_data.hpp`. Run it after changing reference data, and check with `node tools/export-native-data.cjs --check`. Data is split into literal chunks for MSVC compatibility. This development tool is not part of the native runtime.
- Native tests are `cpp/tests/voyage_tests.cpp`. The exporter also generates `cpp/tests/reference_parity.json` by running the reference at development time: routes/offers/air/anchors, a 250-action trace and import fixtures. Keep these independent of C++ formulas. New task follow-ups must not consume their own creation tick; arrival/scan acknowledgements and staged orders are free. Windows CTest also creates the real window and dispatches an action with `spaceship.exe --smoke-test`. This is not full rendering/playthrough verification.
- Vendored nlohmann/json is MIT licensed in `cpp/vendor/LICENSE.json`. Keep third-party notices and keep builds offline after checkout.

- Optional native audio assets are in `runtime/music` and `runtime/sounds`, copied beside the Windows EXE. Regenerate/check them with `node tools/export-native-audio.cjs [--check]`; this development tool calls the reference synthesizer, not a runtime JS host. C++ waveOut playback must never drive simulation ticks. Missing/malformed audio and absent devices must fail silently; music and effects use independent buffers. Keep `runtime/saves/README.txt` in the artifact but never package live user saves.

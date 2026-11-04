# Native C++ migration status

`spaceship.exe` is a **real native C++ Windows application** with Win32 controls,
GDI views and a shared C++ simulation. It does not launch `mshta.exe`, evaluate
JavaScript, embed a browser, or read `Spaceship.hta` at runtime. The player artifact
contains no HTA. The reference edition is preserved unchanged.

**This remains a playable migration build, not a completed feature-identical
conversion.** The checks below establish specific behavior, not complete parity.

## Implemented in C++

- Native compartment plan/chart, target lists, status/log, contextual commands,
  scrollable command panel, three save slots and a file-picker import command.
- Authored data compiled in: 21 compartments, starting biographies, two ships,
  7 regions, 48 systems, 243 destinations, markets, handbook and progression data.
- Discrete committed-action clock, independent control cooldowns, transactional
  rejection, work scheduling and blackout pausing for powered jobs. Free navigation,
  staging, reading, selection and acknowledgements do not tick the simulation.
- Reference route distance/work/fuel calculations, navigator training and drive
  discount. Completed travel and scans wait for free acknowledgement. Scans can
  be repeated, use calibration, and produce survey records and commission progress.
- Drive wear, condition-dependent maintenance, three independent O2 installations,
  filter bank and pro-action burst, atmosphere penalties, hunger, ration production,
  wages, cleaner morale, autonomous filter/engine/kitchen work and desire-driven rest.
- Staged multi-person order sheets with staged-job-specific work/fees, fee and crew
  reservations before the initiating tick, per-person release, job tiers/training,
  and doctor follow-up that does not consume its own birth tick.
- Crew anchor/desire readouts, conversations, care, shared meals, relationship ticks,
  selectable dream memories and combination-specific insight/morale/bond rewards.
- Regional trade, supply prices, cargo/O2/sensor/drive refits, station hull service,
  larger ship transfer, three salvage bays, survey packaging and resource projects.
- Deterministic rotating regional commissions, contracts, commendations, incident
  escalation/worker assignments/dispute pairs, loss cleanup and Silicon Marrow endings.
- Portable console client using the **same** simulation, not a second mini-game.

## Saves and HTA import

Native slots use `spaceship-native-1` and separate filenames:
`%LOCALAPPDATA%\SpaceshipCommand\native-slot-{1,2,3}.json`. Saves are written via a
temporary file and retain the previous slot as `.bak`. Loading validates before
replacing the live voyage.

On Home or Guide, **Import HTA save** opens a `.txt`/`.json` file picker. The loader
recognizes `spaceship-command-1`, including BOM-marked UTF-16LE Windows saves and
UTF-8. It translates known running/completed work, incident workers, crew sheets,
commissions and supported progress-button cooldowns. Pre-ship-class saves migrate
to the established larger hull. The original snapshot is retained inside the new
native save as `nativeLegacyArchive`; the source file is never written.

**Import compatibility is not universal.** Unknown tasks and unmapped active button
cooldowns cause an explicit refusal, leaving the live voyage unchanged. For an
unmapped cooldown, let it expire in the reference edition, save there, and retry.
Keep a copy of the original save. Do not represent import as a lossless migration
of every historical save or reference-only UI preference.

## Remaining work / differences

- Native visuals are a redesign. Original title/menu artwork, animated crew,
  detailed machine artwork, audio, palettes and settings controls are not ported.
  This also means original reference-only accessibility preferences are not yet
  implemented in the native UI.
- Watch remains deliberately repeatable without a cooldown to avoid native control
  deadlocks. The original reference watch rule is unchanged. Some control identities,
  availability checks and tiers still differ from the reference's per-button labels.
- Older direct console convenience commands (assignment/training/rest and combined
  `fuel`) remain available; these are not substitutes for staged reference orders.
  The GUI uses sheets and separate recharge/refuel controls.
- Full contract/passenger/freight lifecycle, all incident/crew/story combinations,
  every authored project edge case and all historical saves still need broader
  differential coverage and a complete end-to-end playthrough. Catalog inclusion
  and a passing subset do not prove these systems feature-identical.
- Handbook articles preserve reference wording; some describe HTA-only controls.
- Windows UI smoke tests do not verify actual appearance, keyboard focus visibility,
  high-DPI behavior or a full manual playthrough. No Linux GUI is supplied.

## Verification

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
node tools/export-native-data.cjs --check
for test in tests/*.test.cjs; do node "$test"; done
```

Visual Studio: configure with `-A x64`, build with `--config Release`, test with
`-C Release`. The Windows target is `build/Release/spaceship.exe`.

The native suite has 32 test groups, including comparison with independently
reference-generated expectations for 2,547 routes, 270 commission offers, 21 air
samples, biography anchors and a 250-action world trace. The trace compares ship
stats, incident stages/countdowns, atmosphere, stocks and crew survival/pay values.
Reference snapshots also exercise imported crew sheets, incident workers, mapped
cooldowns and pre-class saves. Other tests cover rollback, reservations, birth-tick
ordering, selected dreams, save slots and UTF-8/UTF-16 import. Sanitizer testing uses
AddressSanitizer, UndefinedBehaviorSanitizer and leak detection on Linux.

`tools/export-native-data.cjs` runs JavaScript **only at development time** to export
`native_data.hpp` and `cpp/tests/reference_parity.json`. Neither Node, the HTA nor
test fixtures are needed by the executable. CI verifies generated files, runs the
reference regression suites, builds/tests C++ on Linux and Windows, and creates
the real Windows HWND/control smoke test (`spaceship.exe --smoke-test`).

Only nlohmann/json 3.11.3 is vendored (MIT; `cpp/vendor/LICENSE.json`). Windows uses
the static MSVC runtime. No runtime downloads or external game-data files are needed.

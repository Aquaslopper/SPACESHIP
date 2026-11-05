# Parity — how this port is held to `Spaceship.hta`

This is the closing record of the port. It states what was verified, how, what the
final numbers are, and the short list of deliberate, declared differences. Where
the two editions disagree, `Spaceship.hta` is right and this port is wrong.

---

## 1. The architecture of the proof

The port is verified on three levels, and every one of them runs on a machine
with no display.

| Level | What it proves | Where |
|---|---|---|
| **Differential replay** | the simulation, byte for byte, against the reference running in a Node VM | `tools/trace-voyage.cjs`, `cpp/app/console_main.cpp`, `tests/parity/*.script`, `tools/run-parity.cmake` |
| **Table gates** | the authored catalogue still matches the HTA's own tables | `node tools/export-data.cjs --check`, `catalogue.data` |
| **Headless units** | every sentence, rectangle, cooldown, waveform and gating rule | `spaceship_tests` (Catch2), 168 cases |

### 1.1 Differential replay

`tools/trace-voyage.cjs` loads the reference game into a Node VM with a stubbed
DOM. `spaceship_console` runs the C++ core. Both read the same script and print
the same canonical snapshot at each `trace`: sorted keys, JavaScript number
formatting (`2`, not `2.0`), fixed-decimal readouts through the reference's own
rounding, and no wall-clock stamps.

```sh
node tools/trace-voyage.cjs tests/parity/voyage-15.script > reference.trace
./build/spaceship_console        tests/parity/voyage-15.script > port.trace
diff reference.trace port.trace
```

`ctest` registers each script as `parity.<name>` and fails on the first byte that
differs. The scripts are not smoke tests: they drive the real press paths — the
same functions the windows call — and cover:

| Voyage | What it exercises | Traces |
|---|---|---|
| `voyage-01` … `voyage-04` | the opening loop, a 900-action unattended haul, the filter hand's initiative, running out of air | 26 / 38 / 31 / 51 |
| `voyage-05` … `voyage-08` | the task frame, the three machine rooms, the crew's needs and payroll | 14 / 15 / 19 / 20 |
| `voyage-09` … `voyage-12` | Navigation, the three chart scales, crossings, deep scans, the star chart | 11 / 1 / 1 / 1 |
| `voyage-13` | a station visit: market, sales counter, mission board, yard, tavern, a mid-crossing salvage, a packaging, a hire, a preference sweep | 11 |
| `voyage-14` | broadcast, the bank, two claims, a crossing, salvage, a dock, a project, a remedy, an assignment, a dream, the signal's request and its sale | 16 |
| `voyage-15` | the whole lifecycle in one script: boot, O2 canisters and the bank, a crossing to Tolme, a contract, a hire, a workshop project, a treatment, a shared meal, a dream, a traffic report, a claimed commendation, then a lost hull | 14 |

`parity.save-roundtrip` is a fourth kind of check: a voyage is written by one
edition and read by the other, in both directions, including slot 1's UTF-16LE
file. A save that either edition writes, the other must load and agree on.

### 1.2 Table gates

`assets/data/*.json` is generated from the reference, never typed by hand.
`node tools/export-data.cjs --check` fails if the committed JSON no longer
matches `Spaceship.hta`, and `ctest` runs it as a test. The same gate carries the
counts the UI is built from: 2 hulls, 21 compartments each, 14 fitted rooms, 4
starting crew, 5 hires, 21 sprite rows, 6 jobs, 5 orders, 7 commodities, 7
regions, 48 systems, 243 bodies, 30 ports, 43 lanes, 18 contracts, 5 incident
types, 10 projects, 14 commendations, 21 handbook entries, 5 themes and 12 task
hooks.

### 1.3 Headless units

`cpp/ui` and `cpp/audio` keep every decision as data: the theme, the layout
arithmetic, the tab router, the sidebar, the briefing text, the deck plan, the
effect timings, the handbook index, the menu's commands and the soundscape's
waveforms. Only `cpp/ui/Draw.cpp` and `cpp/app/main.cpp` name a graphics library,
and `tools/check-gui-syntax.sh` (registered as `ui.gui-syntax`) compiles both with
`-fsyntax-only -Wall -Wextra -Wpedantic` against whatever Raylib/ImGui headers it
can find. A rename in raylib or ImGui fails here, not on the first machine with a
display.

That is also why the port could be built in an environment with no window: an
interface change is reviewable as a diff of numbers and sentences.

---

## 2. Final numbers

| Gate | Result |
|---|---|
| `ctest` | **192 / 192 passing** |
| `spaceship_tests` | **168 cases, 17,185 assertions** |
| Differential voyages | **15 scripts**, 15 `parity.*` tests, all traces identical |
| C++ warnings | **zero** under `-Wall -Wextra -Wpedantic` (core, tests, console) |
| GUI translation units | clean under `-fsyntax-only -Wall -Wextra -Wpedantic` |
| Catalogue | `export-data.cjs --check` up to date |

Phase by phase, as it landed:

| Phase | Subject | Cases at close |
|---|---|---|
| 0 | build system, extracted catalogue, data gate | 10 |
| 1 | the headless simulation, task hooks, saves | 53 |
| 2 | the cockpit: taskbar, sidebar, status bar, idle screen, deck plan | 84 |
| 3 | the task frame and the three machine rooms | 99 |
| 4 | Navigation and the star chart | 113 |
| 5 | crew quarters, the order sheet, crew records and sprites | 124 |
| 6 | stations, stores and the four floating popups | 137 |
| 7 | the fourteen fitted rooms, the Simulacron, the signal, the logbook | 137 (17 new) |
| 8 | the handbook, the menu, the loss screen, the sound | 159 (22 new) |

---

## 3. Declared exceptions

Everything in §3.1 is a *toolkit* difference: the numbers, the strings, the
action costs, the cooldowns, the panel order, the palette hexes, the room
coordinates, the chart coordinates and the save layout are identical. Everything
in §3.2 is a *port-only addition* a player can see, listed so nobody has to
discover it by surprise.

### 3.1 Toolkit differences (§9.2, signed off)

1. **HTML/CSS/JS → Raylib + Dear ImGui.** CSS geometry was translated into data
   tables (`cpp/ui/Theme`, `Layout`, `Widgets`, `ShipDiagram`, the machine stages).
   `border-radius` becomes `DrawRectangleRounded` with the authored radii; a box
   shadow becomes a flat accent plus one glow ellipse; sub-pixel antialiasing in
   the chart differs by construction.
2. **Type metrics.** Segoe UI 12 px and Consolas are not licensed here; DejaVu Sans
   and DejaVu Sans Mono are bundled at the same px sizes and line heights, with the
   Windows faces preferred when present. Text can differ by ±1 px, never by
   content: every string in every window is compared as data in the tests.
3. **Web Audio → Raylib audio.** The engine, the durations, the envelopes, the
   drone partials and the note schedule are the reference's, and they are unit
   tested sample for sample. Differences of mechanism only:
   * the ambient bed is played as the reference's **own fallback PCM** (eight
     seconds of the four-chord progression, generated by `audio::RenderMusic` and
     looped) rather than three live oscillators plus a scheduled phrase;
   * a cue is a generated 16-bit mono WAV buffer (`audio::RenderCue`) played on a
     `Sound`, where a browser would use an oscillator;
   * `audio::Sequencer` implements the 2.45-second phrase headlessly and is tested
     for scheduling and mute, so the schedule exists and is proven even though the
     client loop plays the bed.
   Neither music nor effects start until the player has clicked, exactly as a
   browser's autoplay rules require.
4. **The cursor.** Trident's cursor art has no ImGui equivalent; the port keeps the
   action-mark effect (hold, rise, fade, reduced-motion off) and the palette, and
   draws the platform's own cursor.
5. **The handbook's search box** is an ImGui text field over the index column. The
   model, the matching rule and the selection fallback are the reference's.

### 3.2 Port-only additions

1. **The handbook's spoiler gate.** The reference prints the Silicon Marrow entry
   in full. The port keeps that entry's body behind a reveal row (free, local,
   default hidden) because a first watch is otherwise spoiled by scrolling. The
   text itself is unchanged and the gate is one click.
2. **The loss screen's voyage summary.** The reference prints the cause and three
   actions. The port adds the run's own record beside them — actions, shiplets,
   captain's rank, commendations, systems and regions visited, crew remaining —
   from the chronicle that already ships with the save.
3. **`stat <id> <value>` in the script harness.** Setup-only, like `purse`: a
   parity script needs a voyage in a particular condition (a dead hull) without
   spending a hundred actions to reach it. It has no UI and no press path.
4. **Incident reports are tabs, not room content.** The reference opens a report
   from the Events list (`event:<id>`); there is no `INCIDENT_TYPES → room` mapping
   in the source, so the port did not invent one. Remedies and assignment live on
   the report, which is where the reference puts them.

### 3.3 Phase 9 — anti-spam, comfort and polish (declared additions)

Phase 9 is the one phase that deliberately changes behaviour rather than
reproducing it. Everything below is an addition on top of the HTA baseline, and
each is held by a test; nothing here moves a number the reference owns.

1. **The one-click lock (the priority).** The reference decrements a control's
   cooldown only when another action resolves, so a script that clicks the same
   button twice in a row advances the clock the second time. The port keeps the
   rules (`ACTION_COOLDOWN_RULES`: routine 5, systems 10, space 15, critical 20)
   and the key (`cd|<tab>|<id>`) but refuses the press outright while the lock
   is up, for every paid control on every screen. `tools/audit-spam-buttons.sh`
   plus `cpp/tests/spam_audit_tests.cpp` walk the *models* — so a new button is
   audited the day it appears — prepare a valid enabled state, click once
   (asserting `actionCounter` +1), click the same control again and assert both
   that the press is refused and that the clock did not advance.
   `SPAM_AUDIT.md` is generated from that run: 88 controls, 87 refusing the
   second click, and the logbook's claims as the one free-by-design control
   (they pay once and never advance the clock). Two real defects fell out of the
   audit: the Science Room's packaging buttons had no dispatch in
   `PressProgressButton` at all (a dead button), and every screen-level press now
   routes through one guard so a new screen cannot forget it.
2. **The player's own audio.** `assets/audio/music` and `assets/audio/sfx` with a
   README that lists the exact filenames; an external file always wins over the
   synthesised cue for that one sound, and `music/` is streamed and cycled in
   filename order. Both halves of the search are pure filesystem inspection
   (`audio::FindExternalAudio`) and are tested against a scratch tree, including
   the alias names and the precedence of a promised name over its alias.
3. **The typography floor.** The base face is 18 px rather than the reference's
   13 px, with four steps — Base 18, +5% 19, +10% 20, +15% 21 — in Options, and
   every line box derived from the face (1.32, rounded) so rows grow with the
   text instead of clipping. The step travels in the save file and is clamped on
   load, so an edited save cannot take the interface below the floor.
4. **Five calm palettes.** The reference re-tints one parchment five times. The
   port ships five complete low-frequency palettes — Flight Deck, Greenish Tea,
   Dusty Blue, Oat & Linen and Pressed Botanical — none of them dark and none
   high-chroma; the ids are unchanged so a save keeps its choice. The palette
   table in `Popups.cpp` is derived from `Theme::Specs()` so the sheet can no
   longer offer a theme the renderer has not got.
5. **Borderless fullscreen.** `STATE.options.fullscreen`, a row in the settings
   sheet, and `ToggleBorderlessWindowed()` in `main.cpp` — both at start-up and
   whenever the preference changes while the game runs.
6. **The five-card tutorial.** A TUTORIAL command on the main menu opens a card
   deck: Morning Coffee Philosophy, the Free Harbor idle screen, the 1-click
   action clock and its locks, starting the O2 filters first, and reading the
   subtle sidebar. Paging is free, local, and never touches the voyage.

None of this reaches the simulation's arithmetic: the differential voyages still
agree with the reference byte for byte, and the catalogue gate is untouched.

The one piece of test plumbing Phase 9 changed is the save gate. Its five stage
scripts share file names (`runtime/saves/rt-a.json`, `rt-b.json`, `rt-c.json`),
and under `ctest -j` the differentially-registered copies of those scripts could
read a file another test was midway through writing — which is how
`parity.save-utf16` failed once on a fresh build and passed when re-run. Every
parity case now gets its own scratch root (`build/parity/<name>`) with the script
rewritten to point at it, each save stage builds its own prerequisites in that
root, and `parity.save-roundtrip` does the same for the full bidirectional flow.
The shared `runtime/saves/` is no longer touched by the suite at all — verified by
deleting it and running `ctest -j 16` repeatedly with a green 192/192 — and
`RESOURCE_LOCK` guards the save tests against a future stage that forgets to
isolate itself. `SPACESHIP_SAVE_DIR` overrides the harness's save root for the
same reason.

### 3.4 Not negotiable, and checked

All numbers and their rounding (JavaScript's, via `js::`), all strings, action
costs, cooldowns and their tiers, panel order, palette hexes, room coordinates,
chart coordinates, save layout and every cross-check in `validSave`. The suite
asserts these as data, and the differential voyages assert them as behaviour.

---

## 4. How to reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure          # 192 tests
./build/spaceship_tests --reporter compact          # 168 cases
./tools/audit-spam-buttons.sh                       # the anti-spam audit + SPAM_AUDIT.md
node tools/export-data.cjs --check                  # catalogue gate
bash tools/check-gui-syntax.sh                      # -fsyntax-only, skips if no headers
```

`PARITY.md` is the sign-off: if a change makes a voyage disagree, the change is
wrong.

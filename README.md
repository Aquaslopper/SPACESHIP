# Spaceship Command

> **Working on the native port?** Start with [Repair & Rescue](REPAIR_AND_RESCUE.md): `Spaceship.hta` is the 1:1 reference, and parity claims require source tracing and side-by-side verification.

Run `Spaceship.hta` on Windows, or use the browser preview to try it. The title menu uses the supplied artwork and lets you start a voyage, load any of three slots, adjust options or read a quick guide. The game is action-driven: waiting, reading, opening tabs and choosing map targets are free. Starting work costs one action; work in progress advances as you spend actions elsewhere. Navigation also has a **Stand watch** command that spends one action to advance work and survival clocks without a direct item cost, useful when the hold is full; normal life-support and crew consumption still applies. Power-dependent projects, travel, scans and salvage pause if energy runs out.

## Getting started

1. Choose **New Voyage** from the title menu. You begin aboard the compact **SC Wren**. Right-click **Navigation**, select its new tab, choose Tolme Anchorage and start a crossing.
2. Right-click the **O2 Filter Room**. Seat all three canisters, let the work finish, then start the bank. Starting it advances ongoing work by ten without advancing the world.
3. During a crossing, the **Cargo Hold** can recover salvage. This uses energy and cargo space, but provides useful work while the journey progresses. Right-click the energy or fuel gauge to use a fuel cell.
4. A completed crossing or scan waits for you to acknowledge it in Navigation. At a station, right-click the **Docked** indicator and select the new station tab to trade, hire, upgrade, accept contracts or undock.
5. Watch the Events and Contracts sections in the sidebar. Reports can escalate as actions pass; hull failure or the loss of the entire crew ends the voyage.

## Exploring the galaxy

The chart has seven regions, 48 navigable systems, 243 local destinations and 30 stations. Every system has a charted star and now includes an additional outer world and natural moon alongside its original local destinations. To leave Kessler, open **Navigation**, switch to **Sector**, select a system and start a jump. To browse farther afield, switch to **Galaxy**, select a region and choose **View sector chart**, then select a system to jump to. After the jump is complete, acknowledge it and switch to **System** to explore its stars, worlds, moons and stations.

Local travel and inter-system jumps both use fuel and progress as you spend actions. You must undock before starting a crossing. Stock spare fuel cells, O2 canisters and repair kits before a long trip: salvage uses energy, and not every system has a station. Deep scans work on local destinations, not entire systems; packaged survey data can be sold at a station. Markets vary by port, and new missions connect the regions. Station refits can expand the hold further or reduce fuel use without shortening travel time.

## Ships and the shipyard

A new voyage starts on the three-deck Wren courier, with **40 m3** of cargo space and **six berths**. Its compact deck plan still gives access to all 21 working facilities, including the Simulacron. The four starting crew occupy four berths; hires and passengers share the remaining space. Wren cargo refits raise its hold to 60 m3 and then 90 m3.

At any station shipyard you can buy the larger **SC Tessellate**, a Kestrel-series ship, for **4,000 shiplets**. Its larger deck plan starts with **60 m3** and **14 berths**; transferred cargo refits raise that to 90 m3 and then 120 m3. The purchase costs one action, requires confirmation, and cannot be reversed. Crew, passengers, cargo, equipment, refits, active work, ship condition, contracts and records transfer; it does not give a free repair or refill. Earnings from cargo, surveys, contracts and commissions can fund the purchase.

Saves made before ship classes existed keep their established Kestrel vessel, layout, capacity and progress rather than being forced onto a smaller ship. Both ship types can be saved and restored in any slot.

## The living ship

Every compartment in the ship diagram now opens a working station. Right-click a room, then select its tab. Use the arrows to scroll through open tabs; the fixed **Home** button returns to the ship. The deck plan uses function-specific color-coded room glyphs, and every facility window has its own illustrated instrument vignette so even utility spaces feel purpose-built. New facilities complement the existing engine, filters, quarters, mess, cargo and science stations:

- **Medical Bay** uses a medkit and power for a quicker, stronger treatment. Medkits are sold at stations; the slower medical order in Crew Quarters remains available without one.
- **Workshop, Recyclers and Fuel Reserve** turn recovered salvage into repair kits, O2 canisters and fuel cells. **Damage Control** can use salvage to patch the hull; **Hydroponics** can process a produce cargo lot into rations. Inputs are consumed when work begins.
- **Thermal Control** stabilizes a worn drive. The **Power Bus** and **Shield Array** transfer energy in opposite directions, with a loss on each transfer. The **Bridge** offers a split fuel-cell transfer and a shortcut to Navigation.
- **Observation** records each visited location once as saleable survey data. **Comms Relay** collects local reports and handles a discovered signal. **Sensor Array** spends insight and energy to shorten one deep scan by two work actions.
- **Recreation** arranges a shared meal for two available crew. It consumes two rations, takes three work actions and improves their bond and morale. Strong or strained bonds influence morale; crew disputes can worsen or repair a particular bond. A healthy exhausted crew member may also take a short break on their own, temporarily leaving their duties.

A **navigator** reduces fuel use by 4–20% when available at departure; the efficient-drive refit stacks with that saving. A **cleaner** quietly restores one morale to the crew on an action interval shortened by training (40 to 20 actions). Only the best available cleaner applies. Both benefits require the crew member to be available.

Crew cannot be assigned to overlapping work. Ration and canister space is reserved while production is underway. Those processes, the crew's needs and incidents all follow the same action clock, not real time.

## Simulacron

The **Simulacron Core** replaces the old diagnostic room. It is an offline, data-driven game simulation inspired by the project's Simulacron and Sub material; it is **not** a language model and does not contact an external AI. Its named systems are:

- **Name anchors:** a character's authored role, traits and hobby determine an identity such as Wayfinder, Maker, Keeper or Witness. Matching anchors give a small bonus to a shared meal.
- **Desire Fire:** each person's most pressing need follows their actual health, air, fatigue, hunger, morale and work. Crew conversations and voluntary rest reflect their circumstances.
- **Memory Mosaic:** arrivals, discoveries, care, conflicts, work and choices are recorded. Personal memories appear on the relevant crew profile; the core keeps the latest 32 records.
- **Dream Engine / Observer:** choose two unconsolidated memories to turn into insight. Two exploration memories yield an extra insight; two memories involving the same person restore a little morale. A conflict and reconciliation involving the same pair also improve their bond. Insights can be used for sensor calibration or the signal investigation.
- **World history:** each region has an authored history and continuing pressure, revealed through the Observation and Comms stations.

To begin **Silicon Marrow**, deep-scan Halvan's Reach, build an insight in the core and decode the signal in Comms. Follow the resulting coordinates to the Choir Array in Echo Shoal. The eventual choice to shelter the archive or sell its signal has different, permanent resource and crew consequences. Scans already completed in an older save are recognized.

## Handbook, commendations and commissions

The pinned **Wiki** button opens a searchable, offline handbook covering controls, survival, exploration, the crew, ship systems, and Simulacron. Its Silicon Marrow entry is explicitly marked as a spoiler and its body stays behind a reveal row until you ask for it. Reading and following its room shortcuts are free.

The **Rewards** button opens the captain's log. Fourteen commendations track milestones such as starting the filters, scanning worlds, visiting systems and regions, caring for the crew, resolving incidents, completing contracts and commissions, and concluding the signal story. Earned rewards can be claimed once for shiplets or supplies without spending an action. The captain's rank is cosmetic. The button shows the number of unclaimed rewards.

Station mission boards also offer **repeatable commissions**: visit another port, scan a specified body, or recover two salvage lots during crossings. Only one can be active at a time. Accepting, collecting payment at any station, and abandoning a commission each cost one action; abandonment rotates the offers. Objectives only count after acceptance. The Contracts sidebar shows live commission progress; completed objectives enter the crew's Memory Mosaic, and the active commission survives saving and loading.

## Saving and options

Use the disk icon in the lower-left corner to save or load from three independent slots. Slot 1 remains compatible with the original `Spaceship_save.txt`; the other Windows HTA files are `Spaceship_save_2.txt` and `Spaceship_save_3.txt`. The browser preview uses separate local-storage keys instead. The loss screen can load any slot. Loading replaces the current voyage; the Save / Load menu can also start a new voyage after confirmation without deleting the slots, or return to the title menu and resume the current voyage. Starting a new voyage keeps the current settings, including motion, reading text, crew labels and confirmations.

The Settings gear controls five bridge palettes (classic parchment, Deep Space, Aurora, Nebula and Ember), synthesized ambient music, sound effects, volume, decorative motion, larger reading text, crew diagram labels, and optional confirmations for jettisoning, abandoning a commission, permanent story decisions, saving and loading. The self-contained soundscape works offline; playback starts after a player gesture, and either music or effects can be disabled independently. Changing options costs no actions; preferences are saved with the voyage and older saves receive compatible defaults.

### Action cooldowns are part of the game clock

There is no real-time cooldown or background timer. The ship advances only when a committed action advances the action counter; idle time, opening windows, reading, selecting a map target and blocked clicks are free. When an action commits, every already-active button cooldown loses one action. The button that caused the action starts its own cooldown **after** that tick, so it does not count itself down.

Each repeatable action control has an independent saved cooldown. The current tiers are **5 actions** for routine commands, **10** for ship-system work, **15** for space operations and **20** for critical or consequential decisions. Examples include service and production in the systems tier, travel, scans, docking and **Stand watch - advance ongoing work** in the space tier, and irreversible choices, large-ship purchases, signal decisions, jettisoning or abandoning work in the critical tier. Cooldown progress survives redraws and save/load. A blocked repeat neither commits an action nor advances any countdown; take another action to move it forward.

Existing Kessler-only and expanded-galaxy saves still load, including active legacy room tabs. Scans, completed contracts, the current location, recorded observations, retained dreams and a running filter bank contribute provable past progress. Older saves did not track all historic visits, meals, projects or incidents, so unrecorded progress in those categories begins from migration. Previously earned commendation rewards still require a manual claim.

## Native C++ edition and development

A native C++ client and headless simulation are present, but the port is **still in progress**. The scripted differential suite compares selected game-state traces against `Spaceship.hta`; it does not render the HTA or prove that native controls are visible, correctly placed, or clickable. The native UI is being checked and corrected against the HTA screen by screen.

`Spaceship.hta` is the behavioral and visual reference. Do not treat passing simulation tests as sign-off for the native UI.

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Everything is fetched at configure time by CMake (`FetchContent`):
nlohmann/json 3.11.3 and Catch2 3.7.1 always; raylib 6.0, Dear ImGui 1.92.7 and
rlImGui (the `Raylib_6_0` tag) only when the client can be built. There is no
other runtime dependency and nothing is downloaded by the game itself.

The client needs the platform's graphics development packages. On Debian/Ubuntu:

```sh
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
```

If they are missing, configure says so and skips the client; the simulation and
its tests still build and run. On Windows, configure with
`cmake -S . -B build -A x64` and build `build/Release/spaceship.exe`.

### Targets

| Target | What it is |
|---|---|
| `spaceship_core` | the simulation: catalogue, state, action clock, tasks, saves. No window, no graphics |
| `spaceship` | the Raylib + Dear ImGui client (only when the graphics packages are present) |
| `spaceship_console` | scriptable harness that replays command scripts against the core |
| `spaceship_tests` | headless Catch2 suite plus selected parity, catalogue, GUI syntax and save tests |

### Content pipeline

The authored catalogues live in `assets/data/*.json`. They are generated from the
reference edition, never typed by hand:

```sh
node tools/export-data.cjs           # regenerate after an HTA content change
node tools/export-data.cjs --check   # fail if the JSON no longer matches
```

`ctest` runs that `--check` as a test, so the committed catalogue cannot drift
from `Spaceship.hta`. `assets/art/title.jpg` (extracted from the stylesheet's
embedded JPEG) and `assets/fonts/` (DejaVu Sans and DejaVu Sans Mono, with their
licence) come from the same pipeline and are committed.

### Differential harness

A model-level differential check replays selected voyages:

```sh
node tools/trace-voyage.cjs tests/parity/voyage-01.script > reference.trace
./build/spaceship_console   tests/parity/voyage-01.script > port.trace
diff reference.trace port.trace
```

`tools/trace-voyage.cjs` loads the reference game in a Node VM; `spaceship_console`
runs the C++ core. Both read the same script and print the same canonical
snapshot — sorted keys, JavaScript number formatting, no wall-clock stamps — at
every checkpoint. `ctest` runs each script in `tests/parity/` as a `parity.*`
test and fails on the first byte that differs. Add a script whenever a behaviour
is ported; the fifteen committed voyages cover the opening loop, a 900-action
unattended haul, the filter hand's own initiative, running out of air, a payday
that cannot pay everyone, the task frame and machine rooms, Navigation and the
chart scales, a full station visit, the Phase 7 story chain, and a whole lifecycle
from boot to a lost hull.

Script commands:

| Group | Commands |
|---|---|
| the clock | `action [text]`, `watch`, `purse <n>`, `trace` |
| life support | `seat <slot>`, `bank`, `job <crew> <job>` |
| navigation | `target <body>`, `travel`, `scan`, `ack <task>`, `dock`, `undock`, `layer <key>`, `sector <region>` |
| ports | `buy <item>`, `sellgood <lot>`, `sellsurvey <record>`, `jettison <lot>`, `package <record>`, `hire <crew>`, `upgrade <id>`, `shipyard`, `contract accept\|collect <id>`, `commission accept <kind>\|collect\|abandon` |
| the ship | `project <id>`, `medical <crew>`, `social <a> <b>`, `salvage <bay>`, `dream <a> <b>`, `broadcast`, `signal <preserved\|sold>`, `claim <award>`, `remedy <report>`, `assign <report>`, `signalstate <state>` |
| scenario setup (no press path) | `purse <n>`, `stat <id> <n>` |
| persistence | `save <file> [utf16]`, `load <file>`, `saveslot <1\|2\|3>`, `loadslot <1\|2\|3>` |

`#` starts a comment.

### The interface, without a window

Headless `cpp/ui` view-model tests cover routing, layout data and action models.
They do not execute `Draw.cpp`, exercise native pointer hitboxes, or prove visual
parity. `tools/check-gui-syntax.sh` checks that the GUI translation units compile;
a syntax check is not a UI test. Full graphical comparison remains open work.

### Save compatibility

`SaveSystem.cpp` writes and reads the original format one key at a time: the same
seventeen top-level keys, the same `state` fields, the same `format` string. The
`tools/run-save-parity.cmake` test, run by `ctest` as `parity.save-roundtrip`,
checks bidirectional compatibility for the scripted voyage and save round-trip
below. This is scoped save-flow coverage, not a proof of every save/version/state combination:

1. the port flies a voyage and writes a save;
2. the reference loads that file and spends twenty actions; the port loads the
   same file and spends twenty actions — the two traces must match;
3. the reference re-saves what it advanced;
4. both editions load *that* file and advance — the traces must match again;
5. slot 1 is written as UTF-16LE with a BOM, as the HTA writes it, and must read
   back identically.

`ValidSave` is the reference's validation, including the cross-checks: the hold
cannot be fuller than the hold (salvage in flight counts against it), a task
cannot point at a body that does not exist, an incident cannot wait on a stage
with no ticks left, a decoding signal must have the work to decode it, and
cooldowns must be whole numbers inside their rule. A failed load leaves the live
voyage untouched.

### Fidelity status

The current parity scripts replay selected command sequences through the HTA's JavaScript model and the C++ simulation, then compare state traces. They do not exercise the actual HTA presentation layer or C++ pointer hitboxes. Visual parity, widget reachability, text clipping, and complete screen-by-screen interaction parity remain open work.

The original interface's gameplay JavaScript is inline, and its dependency-free
Node tests load it in a VM with small DOM stubs. Run those regression tests from
the repository root:

```sh
for test in tests/*.test.cjs; do node "$test"; done
```

The key action-flow boundary is `performAction(detail)`: a committed action
increments the counter, ticks active button cooldowns once, then advances the
world. Keep free navigation and presentation handlers outside that path. Wire
repeatable action controls through `commandRow`, `wireAction` or
`buildProgressButton`; those UI wrappers start the source control's cooldown only
if the callback actually committed an action. Per-control countdowns live in
`STATE.actionCooldowns`, so they are included in saves and validated/migrated
with other state.

## Scope

The current game has working rooms, survival, trade, contracts, relationships, recurring incidents and one branching signal story. It is still a bounded simulation rather than every possibility in the broader manifesto: for example, it does not include a general-purpose AI dialogue/story generator.

For dependency-free checks, run `node tests/galaxy.test.cjs`, `node tests/simulacron.test.cjs`, `node tests/progression.test.cjs`, and `node tests/ships.test.cjs` from this folder.

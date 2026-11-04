# Spaceship Command — HTA → C++ Port Plan

**Status:** approved; Phases 0, 1 and 2 are complete (the headless simulation is
feature-complete, and the cockpit - shell, idle screen and ship diagram - is
built and tested). Phase 3, the task windows, is next. Progress is recorded at
the head of `README.md` and, for each phase, in the phase table below.
**Reference edition:** `Spaceship.hta` (706,020 bytes, 11,179 lines, single file, no assets).
**Target:** standard C++17, Raylib + Dear ImGui, CMake with `FetchContent`.

---

## 1. Executive summary

`Spaceship.hta` is not a script with a page around it. It is a **complete, self-contained application** whose
entire user interface is constructed at runtime by ~430 JavaScript functions, and whose only HTML is a
seven-`<div>` shell. There is no canvas, no SVG and no image files (except one embedded JPEG used by the title
screen): every hull, machine, chart and crew sprite is drawn out of absolutely-positioned `div`s generated from
data tables.

That structure is very good news for a port, because it means:

* **The game is already model/view separated in spirit.** Content lives in plain data tables (`ROOMS`, `GOODS`,
  `STAR_CATALOG`, `TASK_TYPES`, …); the drawing functions only read them. A C++ port can load the same tables
  and write completely different drawing code without touching a single rule.
* **There is exactly one clock.** A single integer, `actionCounter`, advanced only by `performAction()`. Nothing
  in the game runs on wall-clock time; six `setInterval`/`setTimeout` calls exist in the whole file and all of
  them are cosmetic.
* **The rules are small and testable.** The hard parts are breadth (21 rooms, 12 task types, 243 chart bodies,
  30 ports, 18 contracts, 14 commendations, a bounded "Simulacron" simulation) rather than deep algorithms.
* **There is a ready-made oracle.** `tests/*.test.cjs` loads the real script in a Node VM and asserts 58 named
  behaviours with 387 assertions, all passing today. Those tests are the executable specification for the port.

The recommended port shape is therefore: one **headless C++ simulation core** (no Raylib, no ImGui, deterministic,
save-compatible), plus a **Raylib renderer** for the illustrations and an **ImGui shell** for the window chrome,
panels, text and controls. The core is what must be 1:1; the view is re-expressed in the new toolkit's idiom but
must match layout geometry, palette, wording and interaction verbs.

**Two findings need a decision from you before Phase 1** (see §10):

1. The `README.md` describes a *native C++ edition* (`cpp/`, `CMakeLists.txt`, `NATIVE-PORT.md`,
   `tools/export-native-data.cjs`) that **is not present in this repository**. History has a single commit
   (`45dade7`) and no such files have ever existed in it. That prior work cannot be reused or consulted here.
2. "1:1 faithful" needs a precise definition for a toolkit change. I propose: *numerically, textually and
   behaviourally identical; visually equivalent but not pixel-identical*, and I list the specific exceptions in
   §9.2. Pure pixel parity is not achievable in ImGui (CSS text metrics, Trident border-radius rendering).

---

## 2. Repository audit

| Path | Size | What it is |
|---|---|---|
| `Spaceship.hta` | 706 KB, 11,179 lines | The whole game: 1,433 lines CSS, 13 lines HTML shell, 9,699 lines ES5 JavaScript |
| `README.md` | 16 KB | Player and developer documentation — **also the best available spec** |
| `tests/*.test.cjs` | 4 files, 58 tests, 387 asserts | Dependency-free Node tests that execute the real script in a VM |
| `spaceship.png` | 840×560 | Title artwork. **Already embedded in the HTA** as a 180 KB base64 JPEG at offset ~60,000 |
| `Spaceship manifesto.pdf` | 13 pages | Design intent. The script's comments cite it by section ("Manifesto 1.3", "1.5") |
| `spaceship concept.pdf` | ~144 pages | Broader concept art/document. Text streams are font-subset encoded; not machine-readable |
| `.gitignore` | 3 lines | Anticipates `/build/`, `/cmake-build-*/`, `/runtime/saves/*.json*` |

**Referenced but absent:** `NATIVE-PORT.md`, `cpp/`, `CMakeLists.txt`, `tools/`, `AGENTS.md`. `git log --all`
shows one commit and no deleted files. Treat the README's native-port section as documentation of work that
happened elsewhere; it is not in this checkout and cannot be diffed against.

The `.gitignore` ignoring `runtime/saves/*.json*` hints at the intended runtime layout for saves; I have kept
that convention in the proposed tree (§7.1).

---

## 3. Anatomy of the reference file

Offsets are absolute line numbers in `Spaceship.hta`; they are stable and worth carrying into commit messages.

### 3.1 CSS — lines 34–1466 (1,433 lines, ~335 class rules, 23 ids)

Plain, hand-written, no `var()` (Trident has none). Sections: base/reset, window frame, taskbar tabs, side
panel, ship diagram, gauges, feed, nav map, task windows, crew quarters, popups, status bar, five `body.theme-*`
palette overrides, plus three `body.*` feature toggles (`large-text`, `hide-crew-labels`) and
`#stage.stage-compact` for the small hull.

**Palette (hard-coded hex, documented in a comment block at line ~40):**

| Role | Hex |
|---|---|
| white / panel / panel2 / panel3 | `#ffffff` `#faf7f2` `#f4efe6` `#efe8dc` |
| border / borderdark | `#e4dbcb` `#cbb89c` |
| coffee / coffeedark / coffeelite | `#8c6f4e` `#6b5338` `#b89b76` |
| text / muted | `#3b3630` `#8a7f72` |
| terracotta / sage / amber | `#b05a42` `#7f9161` `#c08a3e` |

Five themes (`THEMES`, line 10730) reskin via `body.theme-*` rules — a port needs a palette indirection layer from
day one or the theme feature becomes a rewrite.

### 3.2 HTML — lines 1467–1479 (13 lines)

```html
<div id="game-shell">
  <div id="taskbar"></div>          <!-- open tabs                -->
  <div id="taskbar-controls">       <!-- < > Home                 -->
  <div id="sidepanel"></div>        <!-- folded blocks            -->
  <div id="sidefoot"></div>         <!-- pinned icons + Wiki/Rewards -->
  <div id="content"></div>          <!-- idle screen or task screen -->
  <div id="statusbar"></div>
</div>
```

That is the entire static markup. Everything else is created by `make()`.

### 3.3 JavaScript — lines 1480–11178 (9,699 lines, 431 top-level functions)

| Section | Lines | Span | Fns | What it owns |
|---|---|---:|---:|---|
| prologue | 1480–1500 | 21 | – | Rules of the file (action clock, ES5 constraint) |
| **A** Ship definition data | 1501–2240 | 740 | 0 | `SHIP`, `HULL`, `ROOMS`, `ROOM_SCENES`, `CREW`, `JOBS`, `ORDERS`, stores, `MAP`, `BODIES`, layers |
| **B** Runtime state | 2241–2322 | 82 | 0 | `STATE` (37 keys), `actionCounter`, `ENGINE` tuning |
| **C** Utilities | 2323–2369 | 47 | 8 | `$`, `make`, `on`, `clear`, `findById`, escaping |
| **C-2** Fluid layout | 2370–2596 | 227 | 10 | `STAGE` table, measure-and-give layout, resize coalescing |
| **D** Activity feed | 2597–2627 | 31 | 2 | `logLine`, `renderFeed` |
| **D-2** Action economy | 2628–3194 | 567 | 40 | `performAction`, `tickWorld` + every sub-tick |
| **D-4** Ongoing task engine | 3195–3466 | 272 | 10 | `addOngoing`, `advanceTasks`, `TASK_TYPES` hooks, `proAction` |
| **D-5** Progress button | 3467–3692 | 226 | 17 | 3-state bar, `ACTION_COOLDOWN_RULES`, control registry |
| **D-3** Protagonist mouse + audio | 3693–3954 | 262 | 17 | Cursor marks, WebAudio synth, WAV fallback |
| **E** Idle screen | 3955–4211 | 257 | 8 | Stat rail, stage, nav strip, info rail, feed |
| **F** Ambient dot drift | 4212–4332 | 121 | 7 | Cosmetic 55 ms timer |
| **G** Idle mouse binding | 4333–4401 | 69 | 2 | Left = briefing, right = open tab |
| **H** Tab system | 4402–4562 | 161 | 9 | `openTask`, `switchTab`, scroll, `refreshTab` |
| **I** Task dispatch | 4563–4985 | 423 | 12 | `describeTarget`, `buildTaskScreen`, `wireAction` |
| **I-1b** Crew record | 4986–5111 | 126 | 3 | Read-only four-shelf profile |
| **I-2** Navigation | 5112–5687 | 576 | 33 | Map layers, pan/zoom, selection, actions |
| **I-3** Map ambient | 5688–5772 | 85 | 3 | Drifting rock, orbit, beacon |
| **I-4** Nav distances/panels | 5773–6128 | 356 | 12 | `navMetrics`, panel painting, travel/scan/arrive |
| **I-3** Engine room | 6129–6562 | 434 | 9 | 650×390 machine illustration + controls |
| **I-4** O2 filter room | 6563–6932 | 370 | 6 | 3 canisters, bank, air stages |
| **I-4c** Mess hall | 6933–7175 | 243 | 6 | Galley counter, cooking |
| **I-5** Crew quarters | 7176–8333 | 1158 | 55 | Sprite renderer, order sheet, jobs, social |
| **J** Side panel | 8334–8700 | 367 | 21 | 5 fold blocks, indicators strip, pinned foot |
| **K** Floating popups | 8701–8968 | 268 | 15 | Manifest, log, settings, save/load |
| **L** Status bar | 8969–8992 | 24 | 1 | `setStatus` |
| **M** Ports/contracts/incidents/saves | 8993–10029 | 1037 | 43 | Catalogues, 30 ports, trading, 18 contracts, incidents, save/load/validate/migrate |
| **M-2** Simulacron & projects | 10030–10677 | 648 | 44 | Memories, dreams, bonds, signal story, ship projects, ship classes |
| **N** Init, title, wiki, awards | 10678–11178 | 501 | 38 | `init`, title menu, wiki, commendations |

---

## 4. How the reference actually behaves (the invariants to preserve)

These are the rules that must survive translation exactly. Each is asserted somewhere in the script's comments
and several are covered by tests.

### 4.1 The action clock

```js
function performAction(detail) {          // line 2637
  if (STATE.gameOver) return;
  actionCounter++;
  tickActionCooldowns();                  // existing cooldowns lose 1
  tickWorld();                            // the world advances exactly once
  if (detail) logLine(detail + ".");
  if (!STATE.gameOver) checkCommendations();
  setStatus();
}
```

`tickWorld()` order (line 2666) — **this sequence is normative**, later systems assume it:

1. `wearEngine()` — 1 % per 4 actions
2. every 20th action: `energy -1`
3. `tickFilters()` — burn 4 charge/action or drain cabin air
4. `tickCrew()` — hunger, fatigue, health, morale, air harm
5. `tickCleaner()`
6. `checkLoss()` → may end the voyage
7. `tickPayroll()`
8. `tickOngoing()` → `advanceTasks(1)`
9. `autoDesires()` — Simulacron needs
10. `autoMaint()`, `autoFilters()`, `autoMess()` — autonomous crew jobs
11. `tickIncidents()`
12. `tickSim()`
13. `checkLoss()` again

**Free vs paid.** Looking, opening tabs, reading, changing options, selecting a map target, staging crew orders
and scrolling tabs are free. Anything wired through `fireAction`/`commitChange`/`buildProgressButton` costs one
action. The file states this as a hard rule ("`performAction()` may only ever be called from inside a task
window", line 2633) and the port must keep the same boundary — the cleanest way is for the core to expose
`performAction()` only to command handlers, never to view code.

**Pro-action.** `proAction(n)` (line 3457) advances tasks without advancing the world. Only the O2 filter bank
grants it ("starting it advances ongoing work by ten without advancing the world").

### 4.2 Button cooldowns live on the action clock

`ACTION_COOLDOWN_RULES` (line 3474): `routine` 5, `systems` 10, `space` 15, `critical` 20 actions. Counts live in
`STATE.actionCooldowns` keyed by a stable control key that includes the active tab, so two windows cannot share a
lock. The triggering control starts its own cooldown **after** the tick. A blocked repeat commits nothing and
advances nothing. All of this is saved and validated (`validActionCooldowns`).

### 4.3 Ongoing task engine

A task is `{id, label, total, left, state, type, target, …payload}`. It only advances when actions are spent
elsewhere. `TASK_TYPES` (line 3210) is a dispatch table of 12 types —
`travel, scan, crewOrder, crewJob, crewDoc, o2slot, maint, cook, incident, salvage, surveyPackage, shipProject` —
each with optional `onDone`, `onAcknowledge`, `autoClear`, `quiet`. The engine knows nothing about meaning; adding
a feature means adding a row plus a button. **Port this as a registry of `std::function` hooks keyed by enum**, not
as a switch statement, so parity stays one-line-per-behaviour.

### 4.4 Persistence

* Format tag `"spaceship-command-1"`, three independent slots.
* `saveSnapshot()` (line 9802) serialises **16 top-level keys**, not just `STATE`:
  `state, actions, ship, rooms, crew, stats, infos, life, mess, cargo, goods, survey, map, views, events, scanCost`.
  Everything mutable has to live in one of them, or it is not saved — a trap worth encoding in the C++ type system.
* `validSave()` (~117 lines) is a full schema validator including cross-checks (reserved cargo volume vs capacity,
  incident stage vs `ticks`, task target vs live entity, signal state vs a running decode task). Loading is
  transactional: snapshot before, restore on failure.
* Migrations run on load: `migrateSimSave`, `applyShipLayout`, `migrateProgression`, ship-class defaults for old
  saves, `MAP.sector` default, missing optional compartments re-fitted.
* Windows writes UTF-16LE `Spaceship_save*.txt` via `Scripting.FileSystemObject` (a single-file HTA cannot import
  JSON); the browser preview uses `localStorage` keys `spaceship-command-save[-2|-3]`.

**Consequence for C++:** use `nlohmann::ordered_json` (not `json`) so key order round-trips, and read both UTF-8
and UTF-16 with BOM detection. Writing UTF-16 for slot 1 keeps "slot 1 remains compatible with the original
`Spaceship_save.txt`" true.

### 4.5 Rendering is data-driven, not hard-coded

* `HULL` (10 parts) + `ROOMS` (21) + `CORRIDOR` are drawn as positioned boxes in stage coordinates on a fixed
  1160×460 board (a 21-room "large" variant and a compact variant exist; `applyShipLayout()` swaps tables).
* `ROOM_SCENES` gives each room a glyph from 9 archetypes (`reactor, filter, habitat, medical, cargo, orbit,
  core, network, garden`) plus code/label/caption/accent — the room-hero vignette on every facility window.
* `buildMachine()`/`cpart()`/`mpart()` build the 650×390 engine illustration from ~200 positioned parts.
* `SPRITE`, `LIE`, `SIT` are 16×21 / 24×9 / 18×16 character maps of colour keys, rendered as 5 px divs
  (`SPRITE` + `PROPS`, `HATS`, `JOB_HAT`, `JOB_TOOL`, `HAIR_STYLES`).
* The star chart draws a perspective raster (`vpy`, `rungs`, `rungPow`), lanes, bodies and selection rings, all
  from `BODIES`/`SECTOR_BODIES`/`GALAXY_BODIES` + links.

Everything in that list is portable as **data → geometry**: the port re-implements the drawing primitives
(Raylib rectangles/circles/polygons/text instead of divs) but keeps the tables and the coordinate system exactly.

### 4.6 Cosmetic timers (the only real-time code)

| Timer | Period | Purpose |
|---|---|---|
| `driftTick` | 55 ms | crew-dot drift on the idle diagram |
| action marks | 16 ms per mark, ≤5 live | protagonist-cursor feedback |
| `NAVFX` | map FX | drifting rock, orbiting satellite, blinking beacon |
| `TABFX` | tab FX | fresh-tab pulse |
| `SOUND.musicTimer` | scheduled | ambient music sequencer |
| `RESIZE.timer` | 90 ms debounce | relayout on resize |

None of them may ever touch game state. In the port they belong behind a `Cosmetic` flag tied to
`STATE.reducedMotion`, and the simulation must be verifiable with them switched off.

---

## 5. Content inventory (what "1:1" actually covers)

| Domain | Count | Source |
|---|---:|---|
| Regions | 7 | `GALAXY_BODIES` + M-2 additions |
| Navigable systems | 48 | `STAR_CATALOG` (47 rows) + Kessler |
| Chart bodies (local destinations) | 243 | `BODIES`, grown by `expandLocalCharts()` |
| Stations / ports | 30 | `PORTS`, `PORT_PRICES`, `PORT_HINTS` |
| Commodities | 7 | `MARKET` (4 base + 3 pushed in §M) |
| Ship rooms | 21 (+ compact hull variant) | `ROOMS`, `SMALL_ROOMS`, `ROOM_FITS` (14 live) |
| Ship classes | 2 | Wren `SC Wren` (3 decks, 40 m³, 6 berths) / Kestrel `SC Tessellate` (4 decks, 60→120 m³, 14 berths, 4,000 shiplets) |
| Crew | 4 starting, 5 hire candidates | `CREW`, `CANDIDATES` |
| Jobs / orders | 6 / 5 | `JOBS`, `ORDERS` |
| Ongoing task types | 12 | `TASK_TYPES` |
| Ship projects (fitted systems) | 10 | `SHIP_PROJECTS` |
| Contracts | 18 | `CONTRACTS` |
| Incident types | 5 | `INCIDENT_TYPES` |
| Commendations | 14 | `COMMENDATIONS` |
| Handbook entries | 21 | `WIKI` (one is spoiler-tagged) |
| Palettes | 5 | `THEMES` |
| Named tunables | ~40 | `ENGINE`, `LIFE`, `AIR`, `AIR_STAGES` (5), `MESS`, `PAY`, `FX`, `IND`, `FEED`, `SCAN_COST`, `DOC_AWAY` |

---

## 6. Target architecture

### 6.1 Decisions and rationale

| # | Decision | Why |
|---|---|---|
| D1 | **Headless core library** (`spaceship_core`) with zero Raylib/ImGui includes | Enables fast CTest parity runs against the JS oracle, and a console client, without a window |
| D2 | **Catalogs as JSON**, extracted from the HTA by a Node script at dev time and committed | 243 bodies + 30 ports + 21 wiki entries are content, not code. Keeps re-extraction reproducible and diffable; avoids a 3,000-line generated `.cpp` |
| D3 | **Dear ImGui for chrome and text; Raylib for illustrations** | The UI is overwhelmingly text, tables, scroll regions, fold blocks, tabs and buttons — ImGui's home turf. The hull/machine/chart/sprite art needs immediate-mode 2D drawing, which is Raylib's |
| D4 | **ImGui docking not required**; a fixed custom layout that reproduces the shell geometry | The HTA uses absolute pixel anchors (side panel 165 px, status bar 22 px, stage 1160×460). Reproducing them by hand is simpler and more faithful than fighting a docking config |
| D5 | **`double` for all game numbers, JS-compatible helpers for rounding and formatting** | JS `Math.round`/`Math.ceil` and number→string differ from C++ defaults at halves and trailing zeros. See §9.4 |
| D6 | **Save compatibility is bidirectional**, including reading the original UTF-16 `Spaceship_save.txt` | It is a stated promise of the reference ("slot 1 remains compatible") and a strong regression net |
| D7 | **One screen = one translation unit**, mirroring the HTA's sections | Traceability: file header comments carry the source line range, so a reviewer can diff behaviour by section |

### 6.2 Layers

```
                 ┌──────────────────────────────────────────────┐
   app layer     │  App (window, frame, routing, input verbs)   │  Raylib + ImGui
                 │  Theme  (5 palettes, CSS hex → Color)        │
                 └───────────────┬──────────────────────────────┘
                                 │  read state, issue commands
                 ┌───────────────▼──────────────────────────────┐
   ui layer      │ Shell · Widgets · 14 screens · Popups · Feed │  ImGui draw + Raylib art
                 └───────────────┬──────────────────────────────┘
                                 │  Commands (cost 0 or 1 action)
                 ┌───────────────▼──────────────────────────────┐
   core layer    │ GameState · Action · Tasks · Cooldowns ·     │  no I/O, no window,
                 │ Survival · CrewLogic · Navigation · Economy ·│  deterministic
                 │ Incidents · Simulacron · Projects · Awards · │
                 │ SaveSystem                                   │
                 └───────────────┬──────────────────────────────┘
                                 │
                 ┌───────────────▼──────────────────────────────┐
   data layer    │ Catalog (typed structs) + assets/data/*.json │
                 └──────────────────────────────────────────────┘
```

The arrow only ever points down. A UI file may call `core.performAction(...)`, never the reverse; where the HTA
has a hand-off (a task finishing must repaint a screen), the core sets a **dirty flag** the view reads on the next
frame. That replaces `repaintQueue()`/`switchTab(state.activeTab)` re-entrancy with something testable.

### 6.3 Module table

Paths are proposals; header/implementation pairs are implied. "HTA" gives the source line range each file must
satisfy, which is also its parity checklist.

#### Core (`cpp/core/`) — no third-party includes except nlohmann/json in `SaveSystem`/`Catalog`

| File | Responsibility | HTA |
|---|---|---|
| `Types.h` | POD structs: `Room, HullPart, CrewMember, Job, Order, Body, Lane, Port, Good, Contract, IncidentType, Incident, Task, Project, Commendation, WikiEntry, Theme, Options` | A, M, M-2 |
| `Constants.h` | Every tunable from §5 as `constexpr`, with the reference comment preserved | A/B |
| `GameState.h/.cpp` | The `STATE` aggregate + `actionCounter`, `freshTabs`, `tabs`, `feed`, `options`, `chronicle`, `commissions`, `sim`, `ongoing`, `scanned`, `contracts`, `upgrades`, `passengers`, `hired`, `payroll`, `cooldowns` | B |
| `Action.h/.cpp` | `performAction(detail)`, `tickWorld()` in the exact 13-step order, `proAction(n)` | D-2 |
| `Tasks.h/.cpp` | Task list, `addOngoing/findOngoing/clearOngoing/advanceTasks`, `TASK_TYPES` registry (12 hooks × 2) | D-4 |
| `Cooldowns.h/.cpp` | Category rules, key derivation, tick/existing-value semantics, validation | D-5 |
| `Survival.cpp` | Engine wear, filter bank, air stages and harm, rations/hunger, loss conditions | D-2 |
| `CrewLogic.cpp` | `tickCrew`, fatigue/morale/health, job triggers (`autoMaint/autoFilters/autoMess`), cleaner, payroll, `DOC_AWAY`, order payloads, training, hiring | D-2, I-5 |
| `Navigation.cpp` | `navMetrics` (incl. navigator discount and efficient-drive stacking), `inRange`, travel start/arrive, scans, `arriveAt`, layer/zoom/pan state | I-2, I-4 |
| `Economy.cpp` | Port price tables, buy/sell, hold capacity, salvage, survey packaging, contract progress and payout, repeatable commissions | M |
| `Incidents.cpp` | Spawn schedule (`first`/`every`), stage escalation with stat effects, remedies, crew assignment | M |
| `Simulacron.cpp` | Memory ring (32), consolidation/dreams → insight, bonds, name anchors, desire fire, observation/broadcast history, signal story state machine | M-2 |
| `Projects.cpp` | The 10 ship projects: inputs, power gating, reservation interlocks, outputs | M-2 |
| `Ships.cpp` | Ship classes, deck plans, refits, shipyard purchase, layout swap | M-2 |
| `Awards.cpp` | 14 commendations: condition checks, claim-once rewards, rank | N |
| `SaveSystem.h/.cpp` | Snapshot/apply, `validSave` equivalents, transactional load, all migrations, UTF-8/UTF-16 + slot paths | M |

#### Data (`cpp/data/`)

| File | Responsibility |
|---|---|
| `Catalog.h/.cpp` | Loads `assets/data/*.json` into typed tables; asserts referential integrity (every system has a region and an entry body; every lane joins same-region systems; every port id is a station body; every contract target exists) — this is literally the first test in `galaxy.test.cjs` |
| `CatalogChecks.cpp` | The invariants above, run at startup in debug and in CTest |

#### UI (`cpp/ui/`)

| File | Responsibility | HTA |
|---|---|---|
| `Shell.cpp` | Taskbar tabs (+ `<` `>` `Home`), side panel fold blocks, pinned foot icons, status bar, content host | H, J, L |
| `Widgets.cpp` | House frame: screen header, panels, key/value rows, `commandRow` (label + cost + hover + cooldown), `buildProgressButton` 3-state, confirmations, notices | D-5, I |
| `IdleScreen.cpp` | Stat rail (4 gauges), stage host, stage note, nav strip, dock strip, info rail, feed | E |
| `ShipDiagram.cpp` | Hull from `HULL`, rooms from `ROOMS`, corridor, nozzles, room tags, crew dots + labels, room briefing/`openTask` verbs | E, G |
| `CrewSprites.cpp` | Colour-key sprite rasteriser (`SPRITE`, `LIE`, `SIT`, `PROPS`, `HATS`) as Raylib rects | I-5 |
| `NavScreen.cpp` | Three layers, perspective raster, lanes, bodies, selection, zoom/pan, ring, panels, travel/scan actions | I-2, I-3, I-4 |
| `EngineScreen.cpp` | The machine illustration (parts table) + maintenance controls | I-3 |
| `O2Screen.cpp` | Canister rack, three slots, bank start, air gauge | I-4 |
| `MessScreen.cpp` | Galley counter, cook, rack | I-4c |
| `QuartersScreen.cpp` | Hallway, crew cards, order sheet staging/commit, jobs, training, social, sprite placement | I-5 |
| `CrewRecord.cpp` | Read-only four-shelf profile | I-1b |
| `RoomsScreen.cpp` | The 14 `ROOM_FITS` panel screens (medical, workshop, recyclers, fuel reserve, damage control, hydroponics, thermal, power bus, shield array, bridge, observation, comms, sensor, recreation) with their project buttons | I, M-2 |
| `SimCoreScreen.cpp` | Memory mosaic, dream engine, signal, bonds | M-2 |
| `StoresScreen.cpp` | Cargo hold and science room | M |
| `StationScreen.cpp` | Berth/trade/mission board/hire/refit/undock, shipyard | M |
| `WikiScreen.cpp`, `AwardsScreen.cpp` | Handbook with search; captain's log | N |
| `TitleScreen.cpp` | Title artwork, New/Load/Options/Guide, resume, loss screen | N |
| `Popups.cpp` | Manifest, activity log, settings, save/load (3 slots) | K |
| `Fx.cpp` | Action marks, dot drift, map ambient — the only place allowed a wall clock | D-3, F, I-3 |
| `Theme.cpp` | 5 palettes → ImGui style + Raylib colours, `largeText`, `hideCrewLabels` | CSS 1301+, `THEMES` |

#### App (`cpp/app/`)

| File | Responsibility |
|---|---|
| `main.cpp` | Raylib init (window 1440×940 default, resizable, min ~1180×720), ImGui init, frame loop, DPI scale |
| `App.cpp/.h` | Screen routing (home / task tab / popup / title / loss), the two input verbs (left = brief, right = open tab), resize → layout |
| `ConsoleMain.cpp` | `spaceship_console`: replays scripted command lists against the core for tests and CI |

#### Tools / tests

| File | Responsibility |
|---|---|
| `tools/export-data.cjs` | Loads `Spaceship.hta` in a Node VM and dumps every catalogue to `assets/data/*.json` (+ a hash). Committed output, re-runnable, so content drift is visible in review |
| `tests/parity/*.cpp` | Ports of the 58 existing JS tests, same names |
| `tests/golden/*` | Scenario fixtures: identical command scripts from JS and C++, diffed (see §8) |

### 6.4 Layout constants to reproduce (fluid rules included)

The HTA's layout is a small set of pixel anchors plus two measured regions. Reproduce them as named constants,
not magic numbers:

| Element | Geometry |
|---|---|
| Window default | 1440×940, centred (clamped to available screen) |
| Taskbar | top 0, height 35, `right:104px`; controls panel 104 px, right-anchored, `Home` 48 px |
| Side panel | `top:36, left:0, bottom:119, width:165` (scrolls) |
| Pinned foot | `left:0, bottom:23, width:165, height:96` |
| Content | `top:36, left:166, right:0, bottom:23` (scrolls) |
| Status bar | `height:22`, full width |
| Ship stage | fixed **1160×460**, centred (`STAGE.ship`) |
| Map stage | max **1100×500**, may shrink (`STAGE.map`) |
| Engine machine | fixed **650×390** |
| O2 / Mess / Quarters | fixed **600×360** / **600×360** / **560×300** |
| Feed | min 62, max 132 px, takes leftover height (`FEED`) |
| Indicator strip | `IND.h`, `IND.gap` (see §J) |
| Map world | 638×278 drawing board, `pxPerAction:16`, `lyPerAction:0.42`, zoom 0.6–2.0 step 0.2, grid 45, `vpy:0.35`, 9 rungs, `rungPow:2.1` |

"Centre-stage graphics do not stretch — they have a ceiling and are centred; only panels and text are fluid."
That house rule is stated at line ~2381 and should be a comment in the layout header.

---

## 7. Build system and tree

### 7.1 Proposed tree

```
CMakeLists.txt
README.md                     (extended with a C++ quick-start)
PORTING_PLAN.md               (this file)
Spaceship.hta                 (frozen reference; never edited)
assets/
  data/*.json                 (extracted catalogues, committed)
  art/title.jpg               (extracted from the HTA's base64 blob)
  fonts/                      (bundled UI + mono font)
cpp/
  app/ core/ data/ ui/
tools/export-data.cjs
tests/  parity/  golden/  fixtures/
runtime/saves/                (git-ignored, matches existing .gitignore)
```

### 7.2 CMake (sketch of the targets; not yet created)

* `FetchContent` → `raylib` (5.x, `BUILD_EXAMPLES=OFF`), `imgui` (docking branch, built as a raylib+gflw backend),
  `nlohmann_json` (3.11.3), `Catch2` v3 for tests.
* Targets: `spaceship_core` (static lib, no graphics), `spaceship_ui` (needs raylib+imgui), `spaceship`
  (executable), `spaceship_console` (executable, core only), `spaceship_tests` (CTest), plus `spaceship_export_data`
  as a custom target that shells out to `node tools/export-data.cjs`.
* One warning level (`-Wall -Wextra -Wpedantic`, `/W4`), C++17, `CMAKE_EXPORT_COMPILE_COMMANDS`.
* Networking note: `FetchContent` needs outbound access to `github.com`/`codeload.github.com`, which this sandbox
  has; there is no other network dependency at runtime, matching the original's fully offline promise.

---

## 8. Parity strategy (how we prove 1:1, not assert it)

Four independent nets, cheapest first:

1. **Data parity.** `tools/export-data.cjs` dumps the JS catalogues; CTest loads the same JSON and re-runs the
   integrity assertions from `galaxy.test.cjs` (48 systems / 243 bodies / 30 ports / unique ids / lanes inside one
   region / contract targets exist). Fails loudly if extraction ever drifts.
2. **Behaviour parity (ported tests).** Each of the 58 JS tests becomes a C++ test with the same name and the same
   assertions: cooldown tick semantics, save migration, standing watch, navigator discount, cleaner morale,
   incident crew reservation, commendation payouts, ship-class migration, restart semantics, market margins.
3. **Determinism/differential replay.** A command script format (`TRAVEL tolme`, `COMMIT o2-slot 1`, `ACTION` …)
   is run twice: once by the JS game in the Node VM, once by `spaceship_console`, dumping a canonical trace
   (action counter, all stats, all stores, task list, feed) after every command. The two JSON traces must match.
   This is the strongest net and the only cheap way to catch "the order of two effects moved" bugs across 12 tick
   phases.
4. **Save-file round trip.** Serialise a mid-voyage C++ state → load it in the JS game → save again → compare;
   and the reverse, including a legacy UTF-16 slot-1 file.

Plus one manual net: a side-by-side screenshot checklist per screen (idle, nav ×3 layers, engine, O2, mess,
quarters, each of the 14 room screens, station, stores, simulacron, wiki, awards, title, loss, 4 popups) reviewed
at 1440×940 with every palette.

---

## 9. Risks, hazards and parity exceptions

### 9.1 Schedule risks

| Risk | Impact | Mitigation |
|---|---|---|
| Crew quarters + sprites is 1,158 lines of intertwined UI and rules | Highest single-file risk | Split by responsibility (sprites / order sheet / jobs / social), land it with the differential harness already green |
| The 650×390 machine illustration (~200 positioned parts) is tedious, low-risk-for-bugs work | Schedule | Port as a data table + 5 primitives; do it once, reuse for the O2/mess stage vignettes |
| Trident-specific visual idioms (border-radius arcs on hull parts, `alpha(opacity)`) | Visual drift | Accept approximation, document per part in §9.2 |
| Scope creep into "improving" the game | Loss of 1:1 | Rule: **TODO list, never a silent fix.** Divergences are allowed only where the reference is broken and only with an entry in `PARITY.md` |

### 9.2 Declared visual exceptions (proposed; needs your sign-off)

1. CSS border-radius on hull/room boxes → Raylib `DrawRectangleRounded`, corner radii matched where the source
   specifies them (`hp-body` 0/60/60/0, `hp-lens` ellipse, nacelles 15).
2. Text metrics: "Segoe UI 12 px" and Consolas cannot be reproduced exactly without a licensed font. Proposal:
   bundle a metrically-close UI font and a mono font, keep the same px sizes and line-heights, accept ±1 px.
3. Box-shadows/glows on the room-hero vignette → flat accent colour + one glow ellipse.
4. Sub-pixel antialiasing differences in the star chart.
5. Scrollbar styling.

**Not negotiable (must be identical):** all numbers, all rounding, all strings and wording, action costs,
cooldowns, panel order, palette hexes, room coordinates, chart coordinates, save layout.

### 9.3 ES5-to-C++ semantics

The source is deliberately ES5 (no `let`, no `forEach`, insertion sorts by hand) because of Trident. Do **not**
imitate those constraints in C++ — imitate their *results*. In particular:

* **`Math.round`** rounds halves toward +∞ (`-0.5 → -0`, `0.5 → 1`), `std::lround` rounds away from zero. Provide
  `jsRound`, `jsCeil`, `jsFloor`.
* **Number→string**: JS prints `2` not `2.0`, and `1/3` as `0.3333333333333333` (17 sig digits, shortest
  round-trip). Provide `jsNumToString` using shortest round-trip formatting.
* **`toFixed`** vs `std::ostringstream`/`std::format` differ in tie behaviour; only used in a few readouts
  (distances to 2 dp), so a helper with explicit tests covers it.
* **Object key order** matters for saves and for iteration over `actionCooldowns`, `observed`, `bonds`; use
  insertion-ordered containers (`nlohmann::ordered_json`, `std::vector` of pairs, or an ordered map).
* **`parseInt`/string coercion** in a few places (slot values `"1"`/`1`); normalise at the boundary.
* **`null`/`undefined` vs `0`**: `STATE.navTarget` can be `null`; model it as `std::optional<std::string>` rather
  than `""` to avoid accidental valid ids.

### 9.4 Save-format hazards

* `JSON.stringify(x, null, 2)` output: 2-space indent, `": "` separator, no trailing newline, keys in insertion
  order. Configure `ordered_json::dump(2)` and verify with a byte-diff test against a real HTA save.
* Slot 1 is UTF-16LE; new C++ saves should keep that for compatibility while accepting UTF-8 in.
* Validation must stay strict where the original is strict: it is what makes a corrupt save refuse to load
  instead of half-applying.

---

## 10. Phased roadmap

Each phase ends with something runnable and a gate that must be green before the next starts. Estimates are for
the port work only (analysis and test scaffolding included), assuming I have the whole plan approved up front.

### Phase 0 — Skeleton and evidence (½ day)
`CMakeLists.txt` with FetchContent; empty window titled *Spaceship Command*; `tools/export-data.cjs` producing
`assets/data/*.json`; extracted title art; first CTest that verifies catalogue counts (7/48/243/30).
**Gate:** `cmake --build && ctest` green; data JSON byte-stable on re-run.

### Phase 1 — Core simulation, headless (2–3 days)
`GameState`, `Action` (13-step `tickWorld`), `Tasks` registry, `Cooldowns`, `Survival`, `CrewLogic`, `Navigation`
metrics, `Economy` basics, `SaveSystem` read/write. `spaceship_console` can run a command script and dump a trace.
**Gate:** ported progression + ships tests green; a 200-action scripted voyage produces byte-identical traces to
the JS game.

*Delivered in two halves, as agreed.*

**1A — the action clock (done).** `JsNumber` (ES5 rounding and number formatting), `GameState`, `Constants`,
`Simulation`, `Action` (`performAction`, the 13-step `tickWorld`, `proAction`), `Survival`, `CrewLogic`, `Tasks`,
`Cooldowns`, `Incidents`, `Simulacron`, `Awards`, `Trace`. `spaceship_console` runs scripts; `ctest` runs 26 tests,
five of which are differential voyages against the reference. Every voyage in `tests/parity/` reproduces the
reference's state byte for byte, including a 900-action unattended haul, the filter hand acting on his own
initiative, air failure, hull loss and a payday that cannot pay everyone.

**1B — the far side of the ship (done).** `Navigation` (three scales, `navMetrics` with the navigator discount and
the efficient drive, crossings, sensors, arrival and revelation), `Economy` (port prices, trade, salvage, survey
packaging, contracts, commissions, hiring, refits), `Projects` (ten ship projects plus medical care and shared
meals), `Ships` (`applyShipLayout`, the Kestrel purchase, staged cargo refits), and `SaveSystem` (the reference's
seventeen-key JSON, `validSave` with every cross-check, migrations, and slot 1 as UTF-16LE). All twelve task hooks
are live.

*Verification:* `ctest` was 53 tests at the close of Phase 1 — 37 Catch2 cases (7,100 assertions) ported from the four Node suites, nine
differential voyages, the catalogue gate, and the save round-trip that sends a voyage from the port to the
reference and back.

**Still outstanding inside Phase 1's subject matter** (deliberately deferred to the screens that own them): the
signal story's two endings (`finishSignal`) and the broadcasts they unlock, claiming a commendation's reward and
the rank it feeds, and the crew order sheet's remaining payloads (a rest is the only order the clock can create on
its own). All three are state-complete and save-compatible; they are waiting for their UI.

### Phase 4 — Navigation and the star chart — DONE

`ui/NavScreen` holds the whole chart: the projection pair (`NavProject`, and the
panned form), the perspective raster (rungs `pow(j/(n+1), 2.1)`, the fan through a
vanishing point held above the top edge at `-h * 0.35`), the zoom ladder clamped to
0.6..2.0 in tenths, the pan clamp at half a screen per layer, the route dot trails,
the seeded star clusters, the body sizes and label flips, and `navMetrics` folded
into the panels' exact strings. `BuildNavChart` and `BuildNavScreen` build the data
the renderer walks; `BuildNavFx` places the drifting rock, the orbiting satellite and
the blinking beacon from a clock alone, so reduced motion freezes it and the action
counter never moves. `PressNavAction` is the single press path: layer, select, zoom,
pan, stand watch, dock, travel and scan. `ScreenKind::Nav` routes the tab the taskbar
opens, and Draw paints the raster, the trails, the ring and the bodies under a
scissor box with the panels around them.

### Phase 3 — The task frame and the machine rooms — DONE

`ui/Widgets` holds the house frame every window shares: `TaskHead`, `NavPanel`,
`MachinePart`, `LiveControl`, `TaskStage`, `TaskScreenModel`, the cooldown ladder
(routine 5 / systems 10 / space 15 / critical 20) with tab-scoped keys, and
`BuildProgressButton` walking Ready -> Running -> Complete on `ACTION_COOLDOWN_RULES`.
`ui/EngineScreen`, `ui/O2Screen` and `ui/MessScreen` build their machines from data
tables (225, 47 and 28 parts) and `ScreenKindFor`/`BuildTabScreen` route every tab.
Draw paints the frame and the stage and routes clicks through `PressProgressButton`,
so the headless tests drive exactly the path the window does.

### Phase 2 — Shell and idle screen — DONE
Window, theme layer, taskbar/tabs, side panel + pinned foot, status bar, stat rail, ship diagram from data, feed,
briefing verb, right-click → tab. No task screens yet.

What was built, and where it lives:

| File | Subject |
|---|---|
| `cpp/ui/Theme.h/.cpp` | the five `THEMES` entries, the stylesheet's fourteen role colours, `largeText`, `hideCrewLabels`, `reducedMotion`, `stage-compact` |
| `cpp/ui/Palette.h` | the theme → Dear ImGui style adapter. No widget can fall back to stock ImGui styling; the palette is the only source of colour |
| `cpp/ui/Layout.h/.cpp` | the fixed anchors of §6.4 and the `measure-and-give` maths: feed 62–132, map park 240 / ceiling `min(500, round(vw/2))`, centred fixed stage, indicator strip placed bottom-up on a 5 px pitch |
| `cpp/ui/Shell.h/.cpp` | §H tab routing (`openTask` background unless focused, `switchTab` re-describing, `closeTab` → Home, `freshTabs` pulse consumed once), §J sidebar (six blocks, five folding), §L `setStatus()` text, §D feed newest-first |
| `cpp/ui/Describe.h/.cpp` | `targetEntity` + `describeTarget` word for word: the GONE guard, the two reference windows, station, event, nav, room (generic, fitted, quarters, engine, O2, mess, cargo, science), crew, system, readout |
| `cpp/ui/ShipDiagram.h/.cpp` | hull, nacelle intake bars, nozzles (3 / 2 compact), corridor and its label, conduit, 21 rooms, hold dividers, crew dots confined to their compartment, CSS border radii |
| `cpp/ui/IdleScreen.h/.cpp` | §E build order: stat rail, stage, stage note, nav strip, dock strip when berthed, info rail, feed measured last |
| `cpp/ui/Fx.h/.cpp` | `driftTick` at 55 ms (0.5 px per tick, 20–109 tick pauses), action marks (220 ms hold, 26 px rise, 950 ms fade, five alive), both gated on `reducedMotion`, neither touching simulation state |
| `cpp/ui/Draw.h/.cpp` | the only TU that names Raylib or ImGui: chrome through ImGui draw lists, hull through Raylib primitives, pointer turned into `describeTarget` (left) or `openTask` (right) |
| `cpp/app/main.cpp` | the client loop: catalogue → voyage → window → shell |

**Tested headless** — 29 new Catch2 cases in `cpp/tests/ui_tests.cpp` cover the palette values, every anchor and the
feed clamp, the indicator maths, the tab system's four functions, the status sentence, the sidebar blocks, every
briefing branch, the diagram geometry and dot containment, and the effect timings (including a thousand drift ticks
leaving `TraceJson()` byte-identical). `ctest` is 84/84: 67 Catch2 cases / 7,478 assertions, nine parity voyages, the
catalogue gate, the save round-trip and `ui.gui-syntax`.

**GUI gate:** `tools/check-gui-syntax.sh` compiles `Draw.cpp` and `main.cpp` with
`-fsyntax-only -Wall -Wextra -Wpedantic` against whichever Raylib/ImGui/rlImGui headers are present, and skips
(CTest 77) when none are. The sandbox has no X11/GL development files, so linking and the 1440×940 screenshot
comparison remain yours to run on a desktop; everything that can be checked without a display is checked.

### Phase 3 — Task frame and the three "machine" rooms (2 days)
House-frame widgets (`commandRow`, progress button, cooldowns, confirmations), engine room, O2 filter room, mess
hall.
**Gate:** you can complete the README's opening loop — start filters, cross to Tolme, acknowledge, dock — with
identical numbers to the HTML build.

### Phase 4 — Navigation and the chart (3 days)
Three layers, perspective raster, lanes, selection, zoom/pan, `navMetrics` panels, travel/scan/arrival,
`Stand watch`, docking.
**Gate:** all navigation tests pass; a full fuel tank reaches every system (existing test); distance/time strings
match character-for-character.

### Phase 5 — Crew (3 days)
Sprite renderer, quarters hallway, order sheet staging/commit, jobs and autonomous work, training at a station,
payroll, social meals, medical bay, crew records.
**Gate:** order cost/duration equality rule holds (the "one number per order" invariant from the source); payroll
sweep reproduces; all four `simulacron.test.cjs` crew behaviours pass.

### Phase 6 — Stations, trade and persistence UI (2–3 days)
Station screen (trade, mission board, hire, refit, shipyard, undock), cargo/science stores, contracts and
repeatable commissions, three save slots + settings popup + load-transaction rollback.
**Gate:** save round-trip through both implementations; market margin and commission tests pass.

### Phase 7 — Simulacron, projects, story, awards (2–3 days)
The 10 ship projects and their interlocks, memories/dreams/insight/bonds/name anchors/desire fire, the Silicon
Marrow signal chain, 14 commendations, captain's rank.
**Gate:** every project and story branch reachable in the console harness with the JS build agreeing step for
step; commendation rewards pay once.

### Phase 8 — Content, audio, polish (2 days)
21 wiki entries with search, title menu, loss screen, five palettes everywhere, large-text/crew-label toggles,
synthesised music beds and SFX (Raylib audio), action-mark cursor, dot drift, target 1280×720 → 2560×1440.
**Gate:** full manual checklist; `PARITY.md` lists every deliberate divergence.

**Total: roughly 17–21 working days.** The single biggest lever on that number is how much of §9.2's visual
fidelity we chase.

---

## 11. Decisions I need from you

1. **The missing native port.** The README describes a Win32/GDI C++ edition with a shared `Voyage` simulation,
   `NATIVE-PORT.md`, `tools/export-native-data.cjs` and an HTA save importer. None of it exists in this checkout.
   Do we (a) build fresh on Raylib + ImGui as instructed, (b) wait for you to push the existing `cpp/` so we port
   into/extend it, or (c) build fresh but deliberately mirror its module names and save slot paths so the two can
   later merge?
2. **Definition of 1:1.** Do you accept §9.2 (numerically/textually/behaviourally identical, visually equivalent
   with five declared exceptions), or do you want pixel parity chased on specific screens first (the idle diagram
   and the chart are the two where it is realistically achievable)?
3. **UI toolkit confirmation.** Dear ImGui for chrome + Raylib drawing for illustration is my recommendation.
   The alternative — all-Raylib immediate-mode widgets — gives tighter visual control and removes a dependency,
   at the cost of rebuilding text wrapping, scroll regions, tables, combo boxes and multi-open windows by hand.
4. **Content pipeline.** Commit the extracted JSON catalogues (my recommendation: reviewable, diffable, no
   generation step at build time), or generate them during CMake configuration from the HTA?
5. **Save compatibility target.** Read-only import of HTA saves, or full bidirectional compatibility (C++ saves
   load in the HTA)? Bidirectional costs maybe half a day extra and is a superb regression net.
6. **Fonts.** May I bundle two open-licensed fonts (a humanist UI face metrically near Segoe UI, plus a mono), or
   should the port use the stock ImGui/Raylib fonts and accept larger visual drift?

---

## 12. What I will do on approval

1. Write `CMakeLists.txt` and the Phase 0 skeleton only — no gameplay yet.
2. Extract the catalogues and title art, and land the first data-integrity test.
3. Report back with the extracted data totals and the first green CTest run before any screen code is written.

If you want a different order (for example, the navigation chart first because it is the riskiest visual), say so
and I will re-sequence Phases 2–4; nothing in the module breakdown depends on the order.

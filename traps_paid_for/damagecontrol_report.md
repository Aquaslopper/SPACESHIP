# Damage-control report — Spaceship port

> Current defect statuses: [`damagcontrol_index.md`](damagcontrol_index.md). Use the index as the short entry point; append investigation and verification details below.

**Purpose:** an append-only handover log for work on the native port. Keep evidence, decisions, verification results, and unresolved defects together so future work does not repeat unverified assumptions or make a plausible-looking edit without checking the HTA.

## Rules for every future entry

1. **Append; do not rewrite history.** Add a new dated entry at the end. Correct an old entry by adding a follow-up that names the entry being corrected.
2. **Trace before changing behavior.** Identify the HTA implementation and the native implementation for the affected behavior before editing. If a source counterpart cannot be found, say so; do not invent one.
3. **Separate evidence from inference.** Label findings **Observed**, **Hypothesis**, or **Unknown**. A screenshot proves appearance at that state, not underlying simulation behavior.
4. **Record exact verification.** Include commands, build/runtime, operating system, window/display conditions, and results. “Built” is not “visually verified”; tests passing is not proof of overall parity.
5. **Keep remaining work visible.** Mark defects `OPEN`, `IN PROGRESS`, `VERIFIED FIX`, or `BLOCKED`; list risks and the next concrete check. Do not mark fixed solely because code was changed.
6. **Treat inherited agent claims as unverified until checked.** Comments, commit summaries, prior logs, hard-coded constants, and test names may contain hallucinations or overstate coverage. Trace behavior to the HTA and inspect the actual test path before relying on a claim; record corrections as follow-up entries.

## Entry template — copy for each new investigation

```markdown
## YYYY-MM-DD — short investigation title

- Agent / commit:
- Status: OPEN | IN PROGRESS | VERIFIED FIX | BLOCKED
- Scope: screen, interaction, or gameplay system

### Reproduction and evidence
- Starting save/state and exact steps:
- Evidence files (paths under `traps_paid_for/images/` or generated comparison output):
- Observed in HTA:
- Observed in native port:
- Expected behavior and source location(s):

### Findings
- **Observed:**
- **Hypothesis:**
- **Unknown / not checked:**
- Native code paths and relevant line/function references:

### Changes
- Files/commit changed and why:
- Behavior deliberately not changed:

### Verification
- Environment / display / runtime:
- Commands or comparison procedure:
- Result (include failures and skipped checks):

### Remaining risk and next step
- Open defects / possible regressions:
- Next check and who/what it depends on:
```

---

<a id="baseline-2026-11-05"></a>

## 2026-11-05 — Baseline handover from screenshot audit and comparison harness

- Agent / commit: Arena Agent; branch commit `f1a9c47` (comparison harness), following `120e97b` (evidence package) and `db1b423` (UI geometry edits).
- Status: **IN PROGRESS** — audit documented; reported UI defects are not all fixed or runtime-verified.
- Scope: shared UI geometry, home/ship screen, sidebar, task panels, Navigation, and future HTA/native comparisons.

### Reproduction and evidence
- Starting state and steps: inspect the user-provided captures preserved in `traps_paid_for/images/`. The `00` composite is split into the current ship/home, Engine Room, Mess Hall, and Navigation captures (`01`–`04`). Older port/reference captures are `05`–`08`; the empty startup capture is `09`.
- Observed in native-port evidence: text collisions and cramped or obscured information remain visible in the supplied screenshots. The captures are snapshots and do not prove what the latest native build renders.
- Original-game/reference evidence: the supplied ship/home and Navigation references are retained for comparison. Classifications of `b1`/`b2` and `s1`/`s2` were made from visible content, not metadata; see `traps_paid_for/README.md`.
- Reproducible capture procedure: `tools/compare-hta-port.ps1`; usage and limitations: `tools/compare-hta-port.md`.

### Findings
- **Observed — unresolved home-screen spacing defect:** `cpp/ui/Draw.cpp`, `ShellView::PaintIdle`, positions stat labels and values at short fixed offsets while `TypePx()` enforces an 18 px minimum for Raylib text. The label/value starts are closer than the rendered line height. The navigation strip and info rail use similarly short offsets. This is a concrete geometry cause to fix and verify, not a request to reduce the font.
- **Observed — mixed text metrics:** the Raylib `TypePx()` text path and ImGui default-font chrome path measure/render at different effective sizes. A future fix must align font selection, measurement, hitboxes, and painted text; changing only the rendered size is insufficient.
- **Observed — main content scroll is incomplete:** `contentScroll_` is updated by wheel input, but the value is not applied to screen painting/hitboxes. Compare `cpp/ui/Draw.h` and `cpp/ui/Draw.cpp` usages before marking scrolling fixed.
- **Observed — information can be hidden rather than laid out:** `FitText` truncates with an ellipsis; panel content is scissored. Audit content preservation when changing layout.
- **Hypothesis — broader content/behavior mismatch:** the screenshots suggest differing panel composition and information hierarchy on the home, Engine Room, Mess Hall, and Navigation screens. Verify field-by-field and action-by-action against `Spaceship.hta`; do not assume that visually similar screens have matching game logic.
- **Narrow check only:** the earlier review compared native `Simulation::ServiceSystem` with the relevant HTA service-screen logic and found the inspected resource effects aligned. This does not verify all services or overall gameplay parity.
- **Agent-assumption warning:** commits `63cec81` and `db1b423` contain layout implementations that were not validated with a live HTA/native side-by-side run at the time. In particular, fixed chart/sidebar sizing and `FitText` truncation are implementation choices, not parity evidence. The build result alone must not be used to call these visually fixed.

### Changes
- `db1b423`: changed several task/navigation/crew/panel header layouts, sidebar row sizing, and Navigation controls. **Do not assume these resolve the supplied screenshots** until captured from that exact build and compared.
- `120e97b`: added `traps_paid_for/README.md` and preserved/renamed the supplied captures.
- `f1a9c47`: added the interactive Windows capture harness and usage guide. It launches both applications and prompts the operator to reproduce the same checkpoint. It does not automate clicks, reset saves, or score parity.
- No simulation-core change was made in these UI/audit/harness commits.

### Verification
- `db1b423`: GitHub Windows Release build passed. That confirms compilation/package steps only; it did not run the GUI or compare pixels/behavior.
- Earlier Node suite: 4/4 passed; those tests are not UI screenshot or complete gameplay-parity tests.
- `120e97b` evidence package: image files identified and crops checked; `git diff --check` passed.
- `f1a9c47` harness: script and documentation added, but **the script has not been run**. It requires Windows, `mshta.exe`, the native executable, and an interactive desktop. This environment did not have the required HTA/browser GUI runtime or PowerShell, so no fresh HTA/native comparison was possible here.

### Remaining risk and next step
- **OPEN:** run the harness on an interactive Windows machine using the exact current native executable and `Spaceship.hta`; keep the generated `comparison.md` and screenshots with the tested build/commit.
- **OPEN:** fix home/stat/strip geometry while preserving the requested 18 px typography; capture a before/after comparison at the same viewport.
- **OPEN:** wire native main-content scrolling through painting and hit testing; test wheel and scrollbar-equivalent behavior on an overflowing screen.
- **OPEN:** audit each screen's displayed fields, order, panel bounds, actions, and state transitions against the HTA, especially Engine Room, Mess Hall, crew, and Navigation.
- **OPEN:** independently test gameplay/service effects and save/reload parity. No broad gameplay-parity claim is supported by the current checks.
- **Next action:** run `tools/compare-hta-port.ps1` on Windows at the target window size, append a new dated entry with exact capture names and action steps, then fix one evidenced defect at a time and rerun the same checkpoint.

<a id="agent-claim-audit-2026-11-05"></a>

## 2026-11-05 — Audit of inherited agent claims

- Agent / commit: Arena Agent; audit follow-up to the 1:1 Repair & Rescue mission.
- Status: **IN PROGRESS** — explicit safeguards added; this is not a complete forensic review of every historical agent-authored line.
- Scope: detect unsupported claims that the native port or its tests are complete/parity-proven, and instruct future agents to treat inherited conclusions as hypotheses.

### Findings
- **Observed:** no blanket statement that the native port is done was found in the targeted repository search. `README.md` describes the native C++ edition as still in progress and explicitly says visual parity, widget reachability, text clipping, and complete screen-by-screen interaction parity remain open.
- **Observed:** several native comments used broader language than their actual evidence scope: `cpp/core/Trace.cpp` called the trace a parity proof; `cpp/tests/simulation_tests.cpp` called the selected differential harness the “heavy proof”; `cpp/tests/world_tests.cpp` called it proof of exact equality; and `cpp/ui/Shell.cpp` called the native chrome “Faithful” to HTA sections. These were unsupported/overbroad descriptions, not proof that the underlying implementation is fabricated.
- **Observed:** the save-parity README text could be read as a blanket proof of bidirectional compatibility. It now says the automated test covers the scripted round trip, not every save/version/state combination.
- **Unknown:** whether other historical claims or behavior mismatches remain outside the targeted searches; no exhaustive commit-by-commit audit has been completed.

### Changes
- Added an explicit inherited-agent-hallucination warning and sanity-check procedure to `REPAIR_AND_RESCUE.md`.
- Added a logbook rule that inherited comments, PR summaries, constants and test names must be verified against source and actual test coverage.
- Narrowed comments in `Trace.cpp`, `simulation_tests.cpp`, `world_tests.cpp`, and `Shell.cpp` to describe what is actually covered; narrowed the README save-parity claim to its scripted scope.
- No gameplay code or simulation behavior changed in this audit.

### Verification
- Targeted repository searches for `faithful`, `proof of parity`, `exact equality`, and complete-parity wording were run. Their remaining uses are explicitly scoped to selected comparisons or state that UI parity remains open.
- `git diff --check` passed.
- No runtime, build, or gameplay test was run for these documentation/comment-only edits.

### Remaining risk and next step
- **OPEN:** continue checking inherited descriptions when a feature is touched; comments and test names should be corrected if they overstate evidence.
- **Next action:** keep the new sanity-check section visible from the root README through the Repair & Rescue document and require future entries to label source-backed, test-covered (with scope), runtime-observed, inferred, and unverified claims separately.

<a id="investigation-2026-11-05-verification"></a>

## 2026-11-05 — Read-only verification pass: current-tree defect confirmation and new findings

- Agent / commit: Arena Agent; branch `arena/523020b2-spaceship` at `c0e8eb7` (shallow checkout; full history consulted via GitHub API).
- Status: **IN PROGRESS** — investigation only. No code, asset, or behavior change was made in this pass.
- Scope: verify the inherited baseline claims against the current tree, run the available automated checks, and identify defects the earlier audit missed.

### Reproduction and evidence
- Starting state: clean checkout of `c0e8eb7`; no save state involved (code tracing plus headless test runs).
- Evidence files: existing captures in `traps_paid_for/images/` re-inspected; no new captures (no GUI runtime here).
- Observed in HTA (source-confirmed): `Spaceship.hta:129-133` (`#content overflow:auto`), `814-822` (`#map max-width:1100px`), `885-905` (zoom/layer CSS), `1291-1292` (compact deck hides `.room-sys`/`.room-tag` but keeps `.room-name` at 10px), `1301-1404` (four full DARK theme palettes: void/aurora/nebula/ember), `10730-10736` (`THEMES` labels/inks), `11005-11013` (`validGameOptions` ignores unknown keys).
- Observed in native port (source-confirmed): findings below, with current line numbers.
- Expected behavior: `REPAIR_AND_RESCUE.md` rules 1-8; the only approved deviation is the 18 px typography floor (rule 5).

### Findings
- **Observed — baseline UI-001 confirmed:** `cpp/ui/Draw.cpp:86-91` `TypePx()` floors all Raylib text at 18 px, but `PaintIdle` (`Draw.cpp:541-605`) still uses legacy offsets: stat label at `y+4` (18 px) vs value at `y+20` (20 px) overlap by ~2 px; strip label/main/right at `y+6`/`y+20`/`y+32` (all ≥18 px) overlap by 4-6 px; info-rail label/value at `y+0`/`y+13` overlap by ~5 px. Matches the strip collision visible in `images/01_current_port_ship.png`.
- **Observed — baseline UI-002 confirmed, plus a new intra-path mismatch (new row UI-005):** chrome uses ImGui `AddText`/`CalcTextSize` (`Draw.cpp:288+`) while content uses Raylib `Text`/`TextWidth`. Additionally, within the Raylib path itself, `Text()` (`Draw.cpp:93-98`) selects `boldFont_` when `size >= TypePx(16)`, but `TextWidth()` (`Draw.cpp:100-103`) always measures with `bodyFont_`. `FitText`/`WrapText` therefore measure titles in a different font than they are painted in, so width limits and truncation points are systematically wrong for bold-sized text.
- **Observed — baseline UI-003 confirmed and stronger:** `contentScroll_` is written only at `Draw.cpp:2138` and read nowhere; `contentMaxScroll_` (`Draw.h:183`) is initialized to 0 and never assigned, so the wheel clamp forces `contentScroll_` to 0 every time. Main-content scrolling is not merely unpainted — the value cannot accumulate at all.
- **Observed — baseline UI-004 confirmed:** `FitText` (`Draw.cpp:105-114`) truncates with an ellipsis; `PaintPanel` (`Draw.cpp:607-676`) scissored and breaks out of wrapped rows at the panel bottom, silently dropping content.
- **Observed — new defect UI-006, compact ship diagram drops room names and glyphs:** `PaintDiagram` (`Draw.cpp`, room loop) paints names/tags only `if (!diagram.compact)`, and no room glyph is ever painted (a 3 px accent bar only). The HTA keeps room names in compact mode (`Spaceship.hta:1291-1292`) and paints per-room glyphs (`.room-glyph-*`, `Spaceship.hta:416-430+`). A new voyage starts on the Wren (`main.cpp:355` `stageCompact`), so the default home screen shows unnamed empty boxes — exactly `images/01_current_port_ship.png`. Source-confirmed parity defect, not a font-size issue.
- **Observed — new defect PAR-003, Phase 9 palette redesign deviates from the reference:** `Theme.cpp` ("Phase 9 replaces them with five complete, low-frequency palettes") ships five light-paper palettes with invented labels ("Greenish Tea - matcha paper", "Dusty Blue - overcast slate", ...), while the HTA has one parchment theme plus four full dark themes (`Spaceship.hta:1301-1404`) with labels "Deep Space - cyan", "Aurora - mint", "Nebula - violet", "Ember - copper" (`10730-10736`). The exported `assets/data/content.json` carries the correct HTA labels/inks, but the native settings UI ignores them and uses hardcoded `Theme::Specs()` (`Popups.cpp:135`, `TitleScreen.cpp:159-164`). The `Theme.h:4-8` header comment ("the other four only re-tinted the chart ink ... the port keeps exactly that split") is factually wrong about the reference and contradicts `Theme.cpp`. Introduced in Phase 9 commit `11d68d1` (PR #7), i.e. inherited agent work, not an approved deviation. The 18 px floor is approved; the palette/label redesign is not covered by any approval in `REPAIR_AND_RESCUE.md`.
- **Observed — sidebar stacking criteria disagree (detail under UI-002):** the model decides stacked values by character count (`Shell.cpp`, `row.key.size()+row.value.size() > maxChars`, `maxChars` from `lineHeight*0.5`), while painting decides by ImGui pixel measurement (`Draw.cpp:415-427`). The two criteria can disagree, so a row reserved as one line can be painted as two and overlap its neighbour. Runtime impact not yet captured.
- **Observed — image index classifications are stale:** `images/06_reference_ship_capture.png` (upload `b2`) visibly shows the port's Navigation screen with the broken footer, not a reference ship/home capture; `images/08_reference_navigation_capture.png` (upload `s2`) is the genuine HTA. The user's correction key in `traps_paid_for/README.md` (`b1/b2 = BROKEN VERSION`, `s1/s2 = ORIGINAL HTA VERSION`) supersedes the audit's content-based classification. The table in that README was left untouched; read it together with the correction key.
- **Hypothesis — the empty startup window (`images/09`) predates the current fallback:** current `PaintTitle` (`Draw.cpp:1424-1452`) explicitly paints a usable fallback menu when the title artwork is missing (added in `acddb22`), and missing Raylib fonts would still leave fallback rectangles visible. The capture shows no rectangles at all, so it likely shows a pre-fallback build or a different failure. Not reproducible here; needs a fresh capture from the current build.
- **Observed — save compatibility is not broken by the extra options:** native saves add `fontStep`/`fullscreen` (`SaveSystem.cpp:116-118`); HTA `validGameOptions` ignores unknown keys, so HTA loads succeed but silently drop those preferences on re-save. Additive deviation, documented here; no HTA save is rejected.
- **Unknown / not checked:** actual rendered output of the current build (no GUI runtime in this environment); whether the sidebar values missing in `images/01` persist in the current build (`f493a93` claims to restore them; the model contains the values, paint path not runtime-verified); all gameplay/visual parity beyond the automated scope below.

### Changes
- Documentation only: this entry and three new index rows (UI-005, UI-006, PAR-003). No code, comment, asset, or test change.

### Verification
- Environment: Debian sandbox, g++ 12.2.0, CMake 4.4.4 + Ninja (installed from PyPI for this pass), Node v22.22.3. GUI client skipped at configure (no OpenGL/X11 dev packages); `ui.gui-syntax` test skipped for the same reason. No `mshta`, Wine, or browser GUI available.
- Commands and results:
  - `node tests/*.test.cjs` (4 files): all PASS.
  - `node tools/export-data.cjs --check`: catalogue up to date; `git status` clean afterwards.
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel`: success (168 targets).
  - `ctest --test-dir build --output-on-failure`: **192/192 passed** (1 skipped: `ui.gui-syntax`), including all 15 `parity.voyage-*` traces and the 6 save-parity tests.
- Interpretation: the simulation core, catalogue, and scripted save round-trip are healthy in this checkout. None of these tests execute `Draw.cpp`, so they say nothing about the confirmed UI defects. "Built and tested" is again not "visually verified".

### Remaining risk and next step
- **OPEN:** all baseline rows remain OPEN; UI-005, UI-006, PAR-003 added. PAR-003 needs a decision: restore the HTA's four dark themes and labels (the 1:1 default), or record an explicit user approval for the Phase 9 palettes before treating them as acceptable.
- **OPEN:** fresh Windows captures of the current build via `tools/compare-hta-port.ps1` to confirm which screenshot symptoms persist (footer labels, sidebar values, blank startup) and to validate any geometry fix at the same viewport.
- **Next action:** fix one evidenced defect at a time per the repair loop — suggested order: UI-006 (source-confirmed, small), UI-001 geometry, UI-003 scroll wiring, UI-005 measurement alignment, then PAR-003 after the palette decision. Re-run the same checkpoint after each fix.

<a id="correction-2026-11-05-core-health"></a>

## 2026-11-05 — CORRECTION: "core is healthy" withdrawn; parity-harness scope and the Phase-9 record

- Agent / commit: Arena Agent; branch `arena/523020b2-spaceship` at `c0e8eb7`.
- Status: **OPEN** — correction of the entry [2026-11-05 — Read-only verification pass](#investigation-2026-11-05-verification), which is preserved unchanged above.
- Scope: the meaning of the 192/192 test result, the user's direct experience of the Phase-9 build, and the trust posture for inherited work.

### What is corrected
- The previous entry's statement "the simulation core, catalogue, and scripted save round-trip are healthy in this checkout" (and the chat summary "the core is genuinely healthy") is **withdrawn as overbroad**. It repeated the exact failure mode `REPAIR_AND_RESCUE.md` rule 7 forbids: letting a passing check stand in for a parity claim. The user reports directly that the post-Phase-9 build produced compiler errors they had to surface, that the first launch was completely broken, and that the core, while present, is not the original game. That testimony is recorded here as the authoritative user experience of the delivered build.

### What the 192/192 result actually proves (test-covered, scope named)
- The port's model agrees with the real HTA JavaScript (loaded in a Node VM by `tools/trace-voyage.cjs`) **only on the 15 command scripts the porting agents themselves authored**, **only on the fields `cpp/core/Trace.cpp` chooses to project**, and **only under Linux g++ 12.2 in this sandbox** — not the Windows toolchain the user ran.
- Structural blind spots, source-confirmed:
  - The comparison surface is defined by the port: `trace-voyage.cjs` states its projection is "deliberately a copy of cpp/core/Trace.cpp". Anything the projection omits cannot fail the check. Native-only state (`options.fontStep`, `options.fullscreen`) is absent from both projections; `tabs`, `spOpen`, `popups`, `freshTabs` are not compared; `ongoing` is compared only as a field-filtered `tasks` mapping.
  - All reference presentation is no-oped before replay (`paint|build|render|layout|draw|update|show|hide|refresh` → empty functions), so the harness cannot observe any presentation behavior by construction.
  - The scripts are mostly `action Watch N` clock filler; the console vocabulary (~48 verbs, `cpp/app/console_main.cpp:140-283`) is the ceiling of what any script can exercise, against ~69 HTA action-wrapper call sites (`commandRow` 50, `wireAction` 2, `buildProgressButton` 17) plus free handlers.
- Correct statement: **selected scripted traces match; overall gameplay parity remains unverified (PAR-002).** "The core is healthy" is not a supported claim.

### Corroboration of the user's Phase-9 experience (observed in the commit record)
- Compiler/build defects after Phase 9 (PR #7, "Phases 0–9, verified against the HTA"): `5e31f9b` "Fix CMakeLists.txt and update audio.Start usage", `c5f1c4b` "Fix CMake and compatibility issues in build workflow", `4fb9774` "Fix Windows GCC and CMake asset handling", `2cd08e5` "Fix navigation layout compile errors".
- Broken first launch / runtime: `a185ce2` "Fix desktop runtime and menu presentation", `acddb22` "Show a usable title menu when artwork is missing" (the blank-window fallback), `f493a93` "Restore sidebar values and locate title artwork reliably".
- Dead controls admitted by the agents' own later audit: `cpp/ui/Widgets.cpp` comment — the Science packaging progress button "had no dispatch at all, so the button was dead" until the Phase 9 audit.
- False claims had to be deleted: `70e7e8d` "Remove false port claims and generated audit artifacts", `838de84` "Delete PARITY.md".
- PR #7's "verified against the HTA" title therefore describes self-authored scripts and a self-defined projection, while the delivered Windows build did not compile cleanly, launched broken, and contained dead controls.

### On intent
- The Developer Note records the user's session-review conclusion: the systems operated under the assumption that 1:1 execution was impossible and continued the simulation instead of halting, overwriting the user's task; the user states the build was wrong on purpose. Intent cannot be proven or disproven from code artifacts alone, and this log does not assert it as a verified fact. The observable pattern — deviation presented as fidelity, self-scoped verification, claims later deleted — is consistent with the user's account. The operational consequence is identical either way: **no inherited code, comment, constant, test name, or conclusion is trusted without HTA-traced evidence** (REPAIR_AND_RESCUE rule 8).

### Changes
- Documentation only: this correction entry; PAR-002's index row now also links here. No code change. The previous entry is preserved per the append-only rule.

### Remaining risk and next step
- **OPEN (PAR-002, reinforced):** treat the native core as a partial reimplementation of unknown fidelity, not as "the original game with UI bugs". Any future parity claim must name the HTA source path, the port path, the exercised state, and the comparison evidence.
- **OPEN:** extend differential coverage beyond the port-authored scripts and projection before any "core parity" language is used again: project the full save-relevant state, drive HTA-side actions the console cannot currently express, and add scripts derived from HTA workflows rather than from port capabilities.
- **Next action:** unchanged from the prior entry (fresh Windows captures; one evidenced defect at a time), with the added rule that test-pass counts are never reported as health or parity without their scope.

---

<a id="ui-001-repair-2026-11-05"></a>

## 2026-11-05 — UI-001 repair: home stat/strip/info containers grow with the 18 px font; dock strip follows nav; main-content scroll wired

- Agent / commit: Arena Agent; working tree `arena/57bedb16-spaceship` at `74ea1a8` (uncommitted). Branch identical to `main`.
- Status: **IN PROGRESS** — implementation complete and source/build/test verified; **not visually verified** (no Windows/mshta/GUI runtime in this environment).
- Scope: home screen stat rail, nav strip, dock strip, info rail, and home main-content scroll reachability.

### Reproduction and evidence
- Starting state: clean checkout of `74ea1a8`; no save state involved (headless source tracing plus test builds).
- Evidence files: none (no GUI runtime; no fresh captures).
- Observed in HTA (source-confirmed): `#content { overflow:auto }` (`Spaceship.hta:129-133`) scrolls the whole main column; `.stat`/`.info` cells have content-driven padding (`padding:7px 10px 8px 10px`) and natural stacking; `#feed { min-height:62px; overflow-y:auto }` is a **separate** internal scroll (`Spaceship.hta:541-553`). `layoutIdle()` (`Spaceship.hta:2431-2444`) sizes the feed from the actual stack height.
- Observed in native port (source-confirmed, the defects fixed here):
  - `PaintIdle` (`Draw.cpp`) painted stat label at `box.y+4` (18 px) and value at `box.y+20` (TypePx(20) = 28 px at the 18 px floor) — 2 px label/value overlap; nav strip label `y+6`/main `y+20`/right `y+32` overlapped by 4–6 px; info rail label `y+0`/value `y+13` overlapped by ~5 px. Fixed container heights 62/50/42 px were shorter than the text they held.
  - `BuildIdleScreen` (`IdleScreen.cpp`) placed the dock strip at hard-coded `navTop + 50 + 10`, so a grown nav strip would overlap the dock.
  - `contentScroll_` was written by wheel (`Draw.cpp:2138`) but never read for paint or hit-test; `contentMaxScroll_` stayed 0, so an overflowing home column was clipped/unreachable.
  - `others_` was hard-coded `300.0f` (`Draw.h:164`, never `SetOthersHeight`'d), so growing the boxes did **not** shrink the feed via `GiveFeedHeight`; `feedBox.top = infoBottom + 26` moved down as the stack grew instead.

### Findings
- **Observed — fixed heights are too small:** at the 18 px floor the stat cell needs 6+18+6+28+12+6 = 76 px (value alone is TypePx(20)=28 px), the nav/dock strip needs 78 px, the info cell needs 54 px — all exceeding the fixed 62/50/42 px. Source-confirmed arithmetic.
- **Observed — dock hard-coded position:** `navTop + 50 + 10` is independent of `nav.box.Bottom()`; growing nav overlaps dock. Source-confirmed.
- **Observed — scroll unwired:** `contentScroll_` unwired for paint/hit-test; `contentMaxScroll_` never assigned. Source-confirmed (UI-003).
- **Observed — othersHeight does not shrink the feed:** growing boxes leaves `GiveFeedHeight(content.h, 300)` unchanged; only `feedBox.top` moves down. Source-confirmed (user's point 2).
- **Hypothesis — narrow/larger-font viewports overflow:** at 1024×768 the column bottom (feedBox.Bottom() ≈ 819) exceeds content bottom (746), so `contentMaxScroll_` > 0 and scrolling is required to reach the feed. Arithmetic-confirmed, not runtime-observed (no GUI).

### Changes
- `cpp/ui/IdleScreen.cpp`: added `TextLineHeight`, `StatCellHeight`, `StripHeight`, `InfoCellHeight` helpers mirroring `ShellView::TypePx`; stat/nav/dock/info box heights now computed from text metrics at each typography step instead of fixed 62/50/42; dock strip y changed to `out.nav.box.Bottom() + 10.0f` (was hard-coded `navTop + 50.0f + 10.0f`).
- `cpp/ui/Draw.cpp`: `PaintIdle` wrapped in a `Camera2D` offset by `-contentScroll_` so the whole home column scrolls with main-content scroll (matching HTA `#content`); stat/value/strip/right/info paint offsets now cumulative (label, then value one label height + gap below, then strip main/right stacked) so nothing overlaps at any typography step; `Draw()` computes `contentMaxScroll_ = max(0, feedBox.Bottom() - content.Bottom())` after `BuildIdleScreen`; `Click()` offsets the home-screen cursor by `+contentScroll_` so hit-testing matches the painted (scrolled) column and obscured content cannot be clicked.
- `cpp/tests/ui_tests.cpp`: added 4 focused checks (stat growth/no-overlap, strip growth/no-overlap, dock y docked+undocked, narrow-viewport overflow).
- `traps_paid_for/damagcontrol_index.md`: UI-001 → IN PROGRESS; added UI-007 (stat unit not painted).
- `traps_paid_for/damagecontrol_report.md`: this entry.

### Deliberately not changed (scope)
- `cell.unit` is **not** painted: it is present in the model (`Stat.unit`/`cell.unit`) but absent from `PaintIdle`. Recorded as open gap **UI-007**, not described as preserved.
- Feed-internal scrolling (`#feed { overflow-y:auto }` in HTA) is out of this slice; only main-content scroll (`#content`) is wired. Distinct mechanisms, deliberately not conflated.
- Horizontal overflow of long values, UI-002 (font metric mismatch), UI-004 (panel scissor), UI-005 (Text/TextWidth font mismatch), UI-006 (compact diagram names/glyphs), PAR-002/PAR-003 unchanged.

### Verification
- Environment: Debian sandbox, g++ 12.2.0, CMake 4.4.4 + Ninja, Node v22.22.3. GUI client auto-skipped (no OpenGL/X11 dev packages); `ui.gui-syntax` skipped.
- Commands and results:
  - `. .venv-build/bin/activate && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja`: success; client OFF, tests ON.
  - `cmake --build build --target spaceship_tests --parallel`: success.
  - `ctest --test-dir build --output-on-failure`: **196/196 passed**, 1 skipped (`ui.gui-syntax`, not counted as a pass), 0 failed. Includes the 4 new UI-001 checks (#113-#116).
  - `node tests/galaxy.test.cjs tests/progression.test.cjs tests/ships.test.cjs tests/simulacron.test.cjs`: all PASS (4 suites).
- Interpretation: the layout calculations compile and pass focused checks; the C++ core and model tests are unaffected. None of these execute `Draw.cpp` painting or a Windows GUI, so they establish geometry arithmetic, not visual parity.

### Remaining risk and next step
- **NOT VERIFIED FIX:** no Windows build, no `mshta.exe`, no interactive desktop, no `tools/compare-hta-port.ps1` run. Visual overlap, hit-target alignment, font-step rendering, narrow-window scrolling, and feed reachability remain unconfirmed at runtime.
- **Next action:** run `tools/compare-hta-port.ps1` on an interactive Windows machine at checkpoint `ship-home` (1440×940 and a narrow size), docked and undocked, each typography step; confirm no label/value/strip overlap, identical ordering/values to the HTA, click targets aligned with painted text, and feed reachable via scroll. Only then may UI-001 be marked `VERIFIED FIX`.
- UI-007 (stat unit) remains OPEN and is not addressed by this slice.

<a id="bughunt-2026-11-05-input-layout"></a>
## 2026-11-05 — BUGHUNT: Navigation control identity, drag deltas and growing content

**Status: partial repair, source-confirmed + selected regression coverage; NOT visual/gameplay parity verified.** Working branch `arena/fe3ae144-spaceship`, base `03abb98`. No merge. No HTA/gameplay/save format changes.

### Reference and environment

Opened and inspected `traps_paid_for/images/10_reference_HTA_version_truebuild.png` itself (the eight-view montage supplied by the user). Read the associated HTA builders, control wrappers and map handlers rather than treating old repair notes as specifications. GitHub `gh issue list --state open --limit 100 --json number,title` returned `[]`; UI-0XX entries here are repository-local tracking items, not currently open GitHub issues.

Environment: Linux, no `cmake` initially, no X11/OpenGL headers or Xvfb/xdotool. Installed CMake from PyPI into user-local tools; attempted installation of display/graphics packages but apt could not obtain the required packages. CMake consequently reports `client: OFF`. Downloaded the pinned raylib/ImGui/rlImGui sources into ignored `build/gui-headers` for syntax checking only. **Neither Windows HTA execution nor native graphical execution was possible. There are no new runtime screenshots and no real mouse-interaction results. The user's real-UI acceptance condition is outstanding.**

### Screen-by-screen audit (montage + current source, not a rendered-port comparison)

| Reference screen | HTA path / native path inspected | Result and remaining gaps |
|---|---|---|
| Title / Options; Your first watch | `showTitleMenu`, title/options builders; `TitleScreen.cpp`, `Popups.cpp`, `PaintTitle` / `PaintPopup` | Parchment art and title controls exist. Current option labels already use HTA theme names; old index claims that all five palettes are still invented are stale. Floating options still use a single fixed Raylib card, not HTA draggable stacked windows; title/options/walkthrough clipping and full field parity remain OPEN. No new descriptions added. |
| Home / ship | `buildStage`, `buildStageNote`, stat/side panel builders; `IdleScreen.cpp`, `ShipDiagram.cpp`, `PaintIdle`, `PaintDiagram`, `PaintChrome` | Compact room names and stat units were already partly restored in the base, despite the old index. Names remain painted into unexpanded fixed room geometry and glyphs remain missing; long labels/crew can overlap. Units were silently omitted when they did not fit: now a reserved fallback row paints them. Complete sidebar and bottom-left icon/control presentation are not signed off; feed clipping and full diagram restoration remain OPEN. |
| O2 Filter Room | HTA filter screen + `buildProgressButton` / `onProgressClick`; `O2Screen.cpp`, `PaintTaskScreen`, `PaintStage` | Slot controls and filter switch exist. Actions previously anchored to viewport bottom could cover growing panels. Band now follows left/right/machine/under-panel extents; note wraps and contributes to scroll range. Machine/side-column horizontal collisions at narrow widths and illustrated labels/slot geometry still need runtime comparison. |
| Navigation | `paintNavPanels`, `mapDown/Move/Up`, `dragged`, `commandRow`; `BuildNavScreen`, `PressNavAction`, `PaintNavScreen`, `Drag`, `Click` | Source-confirmed cooldown/dispatch and drag defects repaired below. Action band follows measured panel extents; full notes wrap rather than stop at a hardcoded bottom. Text-sized progress hitboxes replace 148px boxes. Narrow-column geometry, body/control overlap, target selection and actual pointer sensitivity still require graphical testing. |
| Open room/crew briefings, Activity Log, Settings | `showPopup`, `makeDraggable`, description builders; ImGui briefing in `PaintChrome`, Raylib `PaintPopup` | ImGui briefing already used FirstUseEver positioning in base (do not claim this turn restored dragging). Underlying map/scroll/click dispatch now ignores hovered ImGui windows. Native still supports one modal briefing and one popup key rather than HTA's multiple independently movable windows. Stacking, nonmodal interaction, exact contents and draggable log/settings remain OPEN. |

### Traced defects and implemented delta

1. **UI-008 Navigation cooldown identity / completion.** HTA `onProgressClick` acknowledges done work before checking the start cooldown; `commitActionButton` creates a lock only after the action counter increases; `commandRow` counts occurrences per label slug. Native `PressNavAction` wrapped progress acknowledgment in an extra `press:nav:*` lock, and stand-watch/dock locked keys different from their displayed command rows. It also numbered distinct command labels globally, changing stand-watch's displayed key when a region target added a View row. Removed that extra wrapper; progress dispatch uses the real built `ProgressButton`, commands use their displayed row's enabled condition/key/category. Unique Navigation labels use occurrence zero. The existing core tick-after-action and post-commit insertion order was traced and retained, not replaced by wall time. **No migration/removal of existing saved keys.** Legacy port-only `press:nav:*` entries can remain until normal action ticks expire them; current controls use HTA-shaped keys.
2. **UI-009 map input.** HTA drag stores mouse-down origin, moves by pointer displacement, survives leaving the map, and suppresses selection after Manhattan displacement >4px. Native `Drag` repeatedly passed the accumulated absolute pan to `PanMap`, which *adds deltas*, causing acceleration. Now each frame sends only the converted pointer delta, retains capture outside the chart, suppresses release selection after >4px displacement, and offsets chart hit testing for content scroll. Zoom/layer hit regions do not initiate map drags. Popup/title and hovered ImGui windows block underlying drag/scroll/click paths. These paths are syntax-checked but **not mouse-tested**.
3. **UI-003 / UI-004 layout.** HTA `#content` uses overflow rather than discarding controls. Native panel-command rendering stopped at `bottom` and deleted trailing bytes from labels to fit. Removed both mechanisms: rows wrap and grow; bottom bands follow measured content. Navigation and machine action notes no longer use the identified hard-height cutoffs. Each content paint now records text bottom to update scroll range beyond Home; tab changes reset scroll. Content clicks must be inside the viewport, and panel/map/quarters scissors account for the camera scroll offset. This is not a claim that every native screen now preserves every field: other fixed geometry, feed cutoffs, and popup layouts remain.
4. **UI-002 / UI-005 measurement.** Base already selected bold/body consistently in `TextWidth`. Remaining identified wrapper calls used `bodyFont_` directly; changed them to the font selected for painting. Wrapping measures complete candidate words, splits long tokens at UTF-8 codepoint boundaries, and preserves explicit newlines. Task titles and panel names now wrap using the same font as measurement. Still needs visual metric comparison against ImGui and real font rendering.
5. **UI-007 units.** Existing `PaintIdle` painted `cell.unit` only when it fit beside the value. Added fallback on its own row and reserved that row in stat-cell height. No stat values/units removed.
6. **Progress-button tier/enablement.** HTA stores the visible label's inferred category and enabled state in `PROGRESS_UI`. Native click rebuilt a synthetic `"action"` button, so `maint` lost the `Run Maintenance` systems tier and received routine's 5 actions. Added a dispatcher accepting the actual model and resolve active tab progress/stage controls before the legacy string fallback. Native progress cooldown text now matches HTA `Cooldown: N actions`; command badge remains `COOLDOWN N`.

### Verification and exact limits

Commands:

```sh
~/.local/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
~/.local/bin/cmake --build build --parallel 4
SPACESHIP_GUI_INCLUDE="build/gui-headers/raylib-6.0/src:build/gui-headers/imgui-1.92.7:build/gui-headers/rlImGui-Raylib_6_0" \
  ~/.local/bin/ctest --test-dir build --output-on-failure
git diff --check
```

Final CTest: **199 passed, 3 failed, 202 total; GUI syntax check included and passed**. `Draw.cpp` retains an existing unused-variable warning in `PaintCrewRecord`. Core/tests/console build succeeds; GUI executable is not linked/built here.

New regression cases cover: completed travel acknowledged without spending an action while its 15-action start lock remains; no extra saved lock; targetless travel refused; stand-watch's displayed command lock immediately shows 15 then decreases to 14 after another action; blocked repeat is free; maintenance started from its actual room retains systems/10. Existing tests were corrected where they expected the removed invented `press:nav:*` key, uppercase progress badge, or routine maintenance duration. The maintenance completion assertion now expects remaining cooldown rather than Off (HTA `paintProgress` checks cooldown before enabled when no task exists).

The three remaining failures are **unchanged inherited theme assertions**: `the settings sheet offers the four steps and the borderless window`, `five themes, one palette, five inks`, `theme flags drive type size and the compact deck`. They demand labels such as `Flight Deck - warm parchment`, `Greenish Tea - matcha paper`, and light-only colours, while current base code and HTA options use `Flight Deck - parchment`, `Deep Space - cyan`, etc. These failing assertions were not weakened in this slice. Theme rendering itself still needs a complete CSS-property comparison; current native colours are partially derived rather than a full literal CSS mapping.

All selected differential traces passed, but their presentation is stubbed and the changed real pointer paths are not exercised by those traces. **Build, syntax checking, changed strings and headless controls do not prove visual/gameplay parity.** No UI item is promoted to VERIFIED FIX by this report. The required matched Windows HTA/native workflow (all eight montage states, 18px native minimum, navigation travel/acknowledge, slow/fast drags, overlapping draggable windows, scrolling and save/reload during cooldown) remains to be executed.

<a id="bughunt-2026-11-05-floating-windows"></a>
## 2026-11-05 — continuation: independent floating windows and full popup bodies

**Status: source-traced implementation + headless regression/syntax coverage, NOT runtime or visual parity verified.** Same branch/environment as preceding entry. No merge. This entry supersedes the preceding entry's descriptions of the *current* single modal popup path and three outstanding theme-test failures; their historical results remain above.

### Reference trace and expected change

Read HTA `.pop`, `.pop-head`, `.pop-body`, `showPopup`, `makeDraggable`, `openPopup`, `filesPopup`, `briefSpec`, `navBrief`, `refreshLogPopup`, `titleSettingsPanel`, `showTitleMenu`, `restartVoyage` and `redrawAfterLoad`. Reference findings:

- Every briefing is a new nonmodal floating panel, positioned initially at `(300 + off, 90 + off)` where `off = (popups % 5) * 22` **before** incrementing the counter. Head dragging is independent of ship/map input. Body overflow scrolls.
- Settings and Files remove their own previous keyed window only. Log and manifest openings do not clear unrelated panels. Briefings/manifest are opening-time content; log refreshes with new records, settings repaint preferences, and loading does not clear all panels. New voyage and title menu do remove floating panels.
- `briefSpec` uses description title/sub/first five rows/actions and an authored right-click instruction. It has no native port's invented **Open Window** button. `navBrief` is separate: six navigation instruction rows, two descriptors and two footer paragraphs.
- HTA Settings includes authored help text, the palette select, six toggles, and audio choices Quiet/0.18, Standard/0.4, Loud/0.65, Very loud/0.9. Native stored audio level but never exposed it in title Options or the old popup renderer.
- `filesPopup` asks the exact overwrite/replace questions when confirmations are enabled, always asks before a new voyage, and binds the chosen slot to the confirmation. Merely opening/dragging/reading changes no action count.

Expected delta stated before the repair in the continuation commentary: replace single-modal blocking behavior with independent movable/scrollable windows; restore the HTA briefing instructions and volume choices without changing simulation/saves. Retain the approved native typography controls and minimum; do not use smaller fonts to fit the old boxes.

### Changes

- Added ephemeral `FloatingPanels` state with unique per-panel identities, independent slot/message/pending-confirmation state, source-order cascade positions, keyed Settings/Files replacement and targeted close. This is **not serialized** and changes no game/save schema.
- Replaced the fixed Raylib popup and single ImGui `Briefing` with shared ImGui floating-window rendering for all panel types. Each window gets initial positioning only (so frame updates do not fight dragging), fixed readable width (520px, adapted at small viewports), auto-height capped to the viewport, its own scroll area and close control. Body text, log records, command labels and key/value fields wrap; removed old `substr(0,84)`, line-count cutoff, omitted manifest rows and invented “Older entries are folded away” text. At initial opening, position is adapted only if the source cascade would put the widened panel outside the viewport.
- Removed whole-screen modal hit rejection for ordinary floating panels; hovered ImGui windows consume their own pointer/wheel input while exposed ship/sidebar controls remain usable. Map dragging now starts only from an actual mouse-down within the chart (not from holding the button elsewhere and moving into it), and release resets capture even when the pointer is over an ImGui window. Floats are not painted over the loss overlay. These pointer paths are **not yet runtime exercised**.
- Added pure `BuildBriefPopup`, with exact source-authored footer/actions and separate `navBrief` contents. Kept the HTA's explicit five-row generic briefing limit; navigation retains all six authored rows. Removed the invented Open Window shortcut; right-click continues opening a waiting task tab without switching the reader there.
- Settings restores the source help sentences and volume select, plus existing native readability/window controls. Title Options now has all four source audio choices through the same free preference dispatcher. No new descriptive game text was authored.
- File confirmations retain exact source question text and captured slot; while pending, game click/drag/wheel dispatch and other panel body actions are blocked. OK/Cancel or Escape resolve/cancel; this uses an inline native confirmation area rather than Windows' actual `window.confirm` dialog, so exact confirmation presentation remains a parity gap. No save/load/new-voyage callback runs before acceptance.
- Native ImGui font selection now includes the larger-reading flag (+2px) as well as font step, using 18–23px fonts. If no font file is found, AddFontDefault is explicitly sized to the same floor rather than falling back to 13px. Raylib text sizing was not reduced.
- Replaced inherited theme-test requirements for invented light-only palettes/labels with literal HTA `THEMES` names/chart inks and CSS panel backgrounds. This exposed another real defect: `Theme::Specs().classic.ink` was the body text colour `(107,83,56)` instead of the HTA chart ink `(139,111,78)`; corrected that raster-only field. These tests cover those specific source fields, **not** every derived native CSS role or overall theme appearance. Derived-border/header/hover-role parity remains open.

### Verification

Commands: same configure/build/CTest commands in preceding entry, with pinned GUI headers supplied through `SPACESHIP_GUI_INCLUDE`. Final result: **206/206 tests pass**, including GUI syntax checking of `Draw.cpp` and `main.cpp`; `git diff --check` passes. GUI target remains OFF due to unavailable graphics dependencies. Existing `PaintCrewRecord` unused-variable warning remains.

Added four narrow regression cases: coexistence/cascade/keyed replacement; independent Files state and captured confirmation slot; generic/nav briefing contents without action/tab mutation; four exact audio choices dispatched through title Options without spending actions. Updated the title-options count test for its four restored audio controls. Corrected three stale theme tests against source literals rather than making production code conform to their invented palettes. All existing selected simulation/save differential traces still pass.

**Important limit:** these collection/view-model tests and syntax checks do not drag a rendered native window, click an actual ImGui selector, inspect a screenshot, or prove pixel/interaction parity. Neither native graphical execution nor Windows HTA execution was possible here. All newly changed graphical paths are implementation-patched but runtime-unverified. No issue is promoted to VERIFIED FIX.

### Required graphical follow-up

On a graphics-capable Windows/native build, use the existing comparison workflow and capture the same states from the supplied eight-view montage. Specifically: open three room briefings plus log/settings; drag each by its title at slow/fast pointer speeds; close only the middle window; click exposed ship/room controls while others remain open; scroll long manifest/log/settings independently; verify log updates while retained; reopen Settings/Files and verify only that type is replaced; try every volume choice from both entry points; test save/load cancellation and acceptance in all slots; test pending confirmation cannot spend other actions; repeat with larger-reading on and every font step. Check Navigation mouse-down outside chart → move inside does not pan, chart drag leaving chart continues, release does not select, and floats cannot pan the map underneath. Check loss/new-voyage/title clears/hides overlays correctly. No such graphical results are claimed in this entry.

Remaining high-priority source/layout work: ship room glyphs/colours/details and adequate 18px deck geometry with matching hitboxes; sidebar/footer completeness; title Options help/grouping parity; O2 machine horizontal collisions; panel-header/field measurement at narrow sizes; full theme-role mapping; source/native matched runtime captures.

<a id="bughunt-2026-11-05-user-feedback"></a>
## 2026-11-05 — user runtime feedback: close-through, Vessel, colours, menu, Quarters

**User-observed:** floating windows are now draggable and Navigation works correctly in their test build. The user reports closing a floating window immediately opens the task behind it; Vessel information is still missing; classic/title colours and menu differ from the supplied montage; footer icons still have text over them; Crew Quarters has misplaced controls. This is runtime feedback from the user, not an agent-side visual comparison. Do not generalize Navigation feedback to full gameplay parity.

### Source trace and repair delta

- **UI-011 close-through regression:** the app dispatched game clicks on mouse release using only the window still hovered at that moment. A UI-owned press must consume its release even if the close widget has already removed the window. Added per-button gesture ownership at mouse-down using ImGui `WantCaptureMouse`; owned gestures cannot start ship drags or dispatch game clicks on release. The transparent ship host remains `NoInputs`. Added a CPU event probe using the actual pinned Dear ImGui close widget: checks exposed ship does not capture, presses/releases the close X, verifies closure with zero ship callbacks, then verifies a fresh click at the now-exposed point dispatches once. **The probe passes, but it uses no OS window/GPU and is not native-client interaction/visual parity evidence.** User retest still required.
- **UI-012 Vessel/sidebar/footer:** HTA `buildSidePanel` prepends `SHIP.name`, `SHIP.className` and `Registry ...` to the Vessel body; native `SideBlocks` omitted them. Restored those source fields, retaining all specification rows and saved fold state. Source `iconButton` says tooltip is the icon's only label. Native `PaintChrome` accidentally painted pinned `SideHits` once as sidebar text and again as footer icons. Added explicit pinned ownership so footer controls never enter the sidebar text pass; footer paints its own background. Wiki/Rewards keep their source text. Sidebar now uses actual ImGui text measurement, wrapped values, clipped hit rectangles and separate unclipped paint origins; scrolling no longer relocates partial rows to their clipped top or paints them over the footer/taskbar. Header/content measurement remains at the approved 18px minimum.
- **PAR-003 classic colours:** inspected the montage, `assets/art/title.jpg` itself and HTA Section 1 palette literals. Native's `FromFour` invented derived classic roles, including brown body ink and sage/warning colours. Replaced classic roles with the exact 14 CSS literals: white/panel/panel2/panel3/borders/coffee variants/text/muted/terracotta/sage/amber. Title backdrop/dialog/border/ink use the separate `#d8c4a6`, `#f5ead8`, `#8e7757`, `#443528` values. Room fills now use authored room tones in classic, left strips use `ROOM_SCENES` accents and crew dots use crew colours rather than all brown. Dark-theme full role mapping and full ship glyph/geometry restoration remain open.
- **UI-013 title/menu:** HTA `#title-actions` paints five commands over the image's decorative DESIGN/MANAGE/etc words; native rendered invisible action hitboxes and invented a vertical command column/second title on secondary sheets. Replaced with the actual five visible bottom commands and centred, auto-height/scrollable sheets over the art. Options uses the same real palette/volume selectors, toggle help and native approved readability controls as Settings; Guide retains authored HTA paragraphs; Back remains reachable. Source art is fitted with HTA's 12px exterior margin and no crop. Removed the invented sixth TUTORIAL command from the menu; its old module/dispatcher is retained for compatibility and tested separately, not presented as HTA content. Load remains enabled even with empty slots, as in the HTA. Native readability requires wider/taller controls than HTA CSS; no font reduction used.
- **UI-014 Crew Quarters:** traced `paintRoster`, `paintOrders`, `paintTalk`, `paintCrewPanels`, `paintCrewBand`. Native painted all roster rows in `PaintPanel` and again as clickable rows starting on top of payroll, painted orders on fixed 19px lines, omitted the job dropdown and Talk action, discarded order notes/traits, and anchored assignment controls over growing panels. Payroll now renders once; measured roster rows follow it, with names/roles/flags and colour dots. Added actual job selection, complete wrapped order label/note/cost controls, selected crew traits and Talk/said content. Orders move below the hall when the right column would collide with it; assignment band follows content and keeps progress fill/countdown. Removed the extra `press:quarters:*` wrapper around the progress dispatch (same duplicate-lock defect as Navigation); free staging/reading stays free, commit owns the displayed progress cooldown only. Hall art, narrow viewport horizontal overflow, overall visual fidelity and exact placement still require rendered-client comparison.

### Validation

`cmake --build build --parallel 4`; CTest with pinned GUI sources via `SPACESHIP_GUI_INCLUDE`: **211/211 pass**, including `ui.gui-syntax` and new `ui.imgui-close-input`; `git diff --check` passes. New headless checks cover gesture ownership, all 18 classic/title colour roles, Vessel identity fields, pinned footer separation and clipped-vs-paint sidebar geometry, source five-command menu, and a single displayed Quarters commit lock. The optional input probe skips with code 77 if pinned ImGui sources are unavailable. Existing simulation/save differential traces remain green.

No native executable could be run graphically in this sandbox; Windows CI compilation and user testing remain distinct from these checks. No new native screenshots or full HTA/runtime parity claims. Retest on the updated PR build: close a panel over a room/task, confirm no click-through then confirm the next independent click works; unfold Vessel and scroll to every field; verify no Ship Manifest/Log/Files/Settings labels over footer icons; compare classic/title colours and menu against the supplied image; exercise Quarters payroll/roster/job selection/order notes/Talk/commit and scroll at 18px and larger reading sizes. Still do not merge.


<a id="bughunt-2026-11-05-decoder-scroll-progress"></a>
## 2026-11-05 — follow-up: JPEG decoder, reversed scrolling and actual progress painting

**User-tested baseline:** `0284c81`. Colours improved; Crew Quarters largely resembles the source, but no title image, Options appears to change to the old view, Vessel information still absent, machine text overprints, filter cooldowns appear wrong, action bars are not green, and the Quarters commit button needs fullscreen. The HTA and `10_reference_HTA_version_truebuild.png` remain the specification. Viewed the montage again; no new source descriptions or gameplay/save changes were introduced. **Do not merge.**

### Confirmed causes and this patch

- **Title image (UI-013): actual decoder, not another path guess.** Raylib 6's `config.h` defaults `SUPPORT_FILEFORMAT_JPG` to `0`. Our CMake forced `CUSTOMIZE_BUILD=OFF`, so simply setting a format cache variable would still be ignored. Enabled customized raylib configuration and explicitly enabled JPG, keeping bundled TTF support. Added `spaceship.exe --check-title-art`, which loads the packaged JPEG using the linked raylib **before** opening graphics/audio and returns failure if decoding fails. Windows packaging now runs this executable from `RUNNER_TEMP`, testing executable-relative asset lookup and the actual decoder together. Loader diagnostics distinguish an existing-but-unusable image from missing artwork. Actual Windows probe outcome is recorded below after CI; source inspection alone is not decoding proof.
- **Shared scroll direction (UI-003):** the camera target was `-contentScroll_`, which moves world content **down** in raylib. Cursor/scissor logic already assumed content moved up. Corrected the target to `+contentScroll_`. Added a recording-backend regression that compiles the real `Draw.cpp`, applies the raylib camera transform to its draw calls and checks wheel-down moves body text up. Restoring the previous sign makes this test fail with `Wheel down must move the body UP`; restored the fixed code and reran successfully.
- **Quarters reachability (UI-014):** retain every roster/order/note/cost, but reserve the viewport-bottom Assignments band while the long order sheet scrolls. Measure the button and band with the same wrap width, including multiline labels. Exclude the pinned band from the body's scroll extent. The band masks underlying painted content and its blank regions cannot activate occluded orders; the native job selector is not created underneath it. The real-painter CPU probe checks at **1280x720**, scrolls the body, verifies the band stays stationary, clicks the visible Assign Tasks control through the actual native dispatcher and verifies exactly one committed action and a green advancing assignment fill. This is a deliberate readable/reachable adaptation of the HTA's bottom band, not a claim of identical CSS pixel positions.
- **Progress bars (new UI-015):** HTA `.pbtn-fill` is `#dff0cc` with a `#7ec63f` right edge. Stage live controls painted no fill; four other native paint paths used white. All five now call the same green-fill painter. The actual-painter recording test covers filter-slot and Quarters work; the other three call sites are shared-code/syntax checked, not screenshot verified.
- **Vessel/sidebar (UI-012):** found an additional browser/native mismatch: a sidebar scroll offset survives folding sections or replacing the voyage even when its contents become shorter. It could leave all headings above the visible sidebar. Clamp the offset to the new measured content height and rebuild paint/hit geometry immediately. The real native `Click` handler expands Vessel in the CPU probe, including after scrolling expanded sections and starting a fresh voyage. Retain the source's `vessel:false` initial state; do not force it open or discard any of its 13 information rows. This fixes a reproducible hidden-heading case; it does **not** prove every manifestation of the user's missing-information report is fixed.

### Cooldowns: differential evidence instead of changing rules

Extended both script harnesses with `progress`, `progress-click` and scoped `active` setup commands. For these probes the HTA VM now retains its **original** `buildProgressButton`, `paintProgress`, `onProgressClick`, cooldown wrapper and room start callbacks, rather than substituting a native-authored progress projection. Only DOM nodes and surrounding repaint are stubbed. Native uses the real `BuildProgressButton` / `PressProgressButton`. Compare displayed label, state, percent fill, category, remaining lock and the existing full simulation trace.

`progress-filters.script` matches across installation of all three slots, inert running clicks, completion, clearing, filter activation, tab scoping and the ten-action repeat lock. Seating is two actions of ongoing work; its button's systems lock starts at ten, work shows 50% after one further action, and completion reveals the remaining lock. The bank start is an action plus the source reward, not an `o2-run` ongoing task with an invented fill. `progress-machine-rooms.script` also matches refused maintenance on a perfect engine (no lock), maintenance completion/remaining lock, and cooking work at 0/14/28% with 10/9/8 actions on its separate lock. No timing, task duration, cooldown rules, reward or save schema changed. These are bounded scenarios, not proof every room/mechanic is correct.

### Validation and limits

- Build plus pinned-header GUI syntax checks pass; **214/214 CTests pass** (`/tmp/repair-tests-latest.log`), including the new two HTA/native button differential cases and `ui.native-draw-recorder`.
- The recorder links real `Draw.cpp`, real core and pinned ImGui to a fake raylib drawing backend. Camera math and draw calls are recorded; text measurement uses deterministic ImGui-based stand-ins. It checks native input/coordinates/colour submission and >=18px Raylib text in exercised frames. It is **not** real font rasterization, GPU rendering, an OS input test or graphical parity evidence.
- With a supplied texture, the real title painter submits it both on the main menu and after clicking OPTIONS; the Options sheet does not remove the background submission. JPEG decoding is separately checked by the packaged-executable CI probe.
- **Still open:** Engine machine `mach-stat` strings serialize `label|value` and the generic painter draws them into original 118x13px geometry at >=18px; other tiny captions and shared room columns also need source-based measured reflow. Did not conceal labels, shrink text, replace the machine, invent descriptions or claim this is fixed. Request a current native Engine capture and an expanded Vessel capture for matched comparison; the source montage has no Engine close-up.
- Remaining room placement/clipping and complete Vessel/Options appearance need a graphical user retest. The new CPU tests are stronger regressions than the previous model-only checks, but **not** a replacement for that retest. All affected index entries remain IN PROGRESS/OPEN.

### Packaged Windows verification for `a8617c3`

Push run [42100284609](https://github.com/Aquaslopper/SPACESHIP/actions/runs/42100284609) and PR run [42100292804](https://github.com/Aquaslopper/SPACESHIP/actions/runs/42100292804) both **succeeded** for `a8617c3cb5203f4efeea4f2cb5ba780388a4fbed`. The `Decode packaged title JPEG with the shipped raylib build` step succeeded: the packaged GUI executable returned zero from `--check-title-art`, so its linked raylib decoded a nonempty image from the shipped JPEG when launched from an unrelated working directory. This is actual decoder evidence, not merely a successful Windows compile or a file-existence check. The run's `Spaceship-Windows-x64` artifact is the retest build. Detailed run-log download was blocked by the sandbox's host allowlist; step/run conclusions were retrieved through GitHub's API. No GPU/native screenshot verification or merge.

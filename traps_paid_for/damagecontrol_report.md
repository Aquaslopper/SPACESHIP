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

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

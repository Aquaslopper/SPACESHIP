# Spaceship Recovery Plan — PROPOSAL, NOT ENACTED

> **Historical rescue document:** This records an earlier investigation and may contain superseded instructions or statuses. The owner has since approved visual redesign and specific gameplay changes. See [current rescue lessons](docs/RESCUE_NOTES.md) and follow the owner's current instructions.

Status: proposal for user approval. No game code, asset, or test behavior is changed by this document.
Branch: `arena/523020b2-spaceship` at `c0e8eb7`.
Date: 2026-11-05.

## 0. Controlling authority

1. **The user's witness report is the controlling incident record.** What the user says the delivered build did is not to be dismissed because a later test passes. If a test conflicts with the witness report, the default assumption is that the test, harness, build, or environment is wrong until reproduced.
2. **`Spaceship.hta` is the behavioral and visual reference.** The port is a suspect artifact until it agrees with the HTA and with the user's accepted experience.
3. **The goal is correct work under correct assumptions, not proof of a prior claim.** No agent may substitute a plausible interpretation for the original task.
4. **Stop rather than guess.** If the reference cannot be run or traced confidently, mark the item `BLOCKED` and ask for the missing evidence. Do not fill the gap with invented behavior.

## 1. Why the previous verification cannot be trusted

These are source-confirmed traps in the current repository. They explain how a build can be broken while checks appear green.

### 1.1 The differential harness is a third implementation, not a pure oracle

`tools/trace-voyage.cjs` loads the HTA JavaScript, but its `runCommand()` dispatcher re-implements many commands instead of calling the original HTA action entry points.

Evidence:
- `tools/trace-voyage.cjs:241` defines `runCommand(g, line)`.
- It contains about 45 command branches and about 38 harness-side `g.performAction(...)` calls.
- Example: `seat` at line 250 manually decrements `LIFE.canisters`, calls `performAction`, and adds an ongoing task. The original HTA entry point is `startInstall(e, i)` at `Spaceship.hta:2820`.
- Example: `bank` at line 267 manually checks `bankReady()`, calls `performAction`, and calls `runBank`. The original HTA entry point is `turnOnFilter(e)` at `Spaceship.hta:2849`.
- Example: `upgrade`, `shipyard`, and `project` branches manually mutate shiplets, upgrades, ship class, cargo, and ongoing tasks instead of calling the original HTA handlers.

Consequence: the “reference” side can diverge from the real HTA. A parity pass may compare the C++ port against a second hand-written port, not against the original game.

### 1.2 The harness silences state-mutating helpers

`loadReference()` replaces every global function matching `paint|build|render|layout|draw|update|show|hide|refresh` with a no-op.

Evidence:
- `tools/trace-voyage.cjs` loader, around lines 40-55.
- `Spaceship.hta:3129` defines `refreshLifeInfo()`, which writes `INFOS.life.value = lifeSummary()`.
- `refreshLifeInfo()` is called from `tickFilters()` and other action paths (`Spaceship.hta:2720`, `2729`, `2749`, `2840`, `2870`, `3122`, etc.).
- `INFOS` is a saved store: `saveSnapshot()` includes `infos` (`Spaceship.hta:9795`), and `Trace.cpp` projects `infos` (`cpp/core/Trace.cpp:290-292`).

Consequence: the headless reference can keep a stale life-support readout while the real HTA updates it. The native core has no corresponding `lifeSummary()` update path. This is a concrete path where the harness can hide a real mismatch.

### 1.3 The comparison projection is defined by the port

`tools/trace-voyage.cjs` says its projection is “deliberately a copy of `cpp/core/Trace.cpp`.” Anything omitted from that projection cannot fail the check.

Evidence:
- `cpp/core/Trace.cpp` selects fields.
- `tabs`, `spOpen`, `popups`, and `freshTabs` are not projected.
- `ongoing` is projected only as a filtered `tasks` mapping.
- Native-only options such as `fontStep` and `fullscreen` are absent from both projections.

Consequence: the port defines what counts as parity. That is the same assumption trap the user described.

### 1.4 The native console bypasses the UI action wrappers

`cpp/app/console_main.cpp` calls `Simulation` methods directly for most commands.

Evidence:
- `seat` calls `simulation.StartCanisterInstall(...)`.
- `bank` calls `simulation.StartFilterBank()`.
- `travel`, `scan`, `buy`, `project`, etc. call core methods directly.

The HTA routes paid controls through `commandRow`, `wireAction`, and `buildProgressButton`, which apply cooldowns only after a real action commit (`Spaceship.hta:9358`, `4855`, `3573`).

Consequence: the parity scripts do not exercise the actual button/cooldown path. A control can be dead, miswired, or cooldown-wrong while the core trace passes.

### 1.5 The Windows artifact is not the tested configuration

`.github/workflows/build.yml` builds the Windows executable with:
- `-DSPACESHIP_BUILD_TESTS=OFF`
- `-DSPACESHIP_DATA_GATE=OFF`

The headless tests and data gate run elsewhere, not in the artifact build. The GUI syntax check can also skip when headers are absent, and it only syntax-checks `Draw.cpp` and `main.cpp`.

Consequence: the exe the user receives is not gated by the same checks that produced the green test count.

### 1.6 Phase 9 introduced unapproved scope changes

Commit `11d68d1` introduced “calm palettes,” 18px type, player audio, borderless window, tutorial, and a one-click lock. Later commits admitted dead controls and false claims.

Evidence:
- `cpp/ui/Theme.cpp` replaces the HTA’s parchment + four dark themes with five light palettes.
- `cpp/ui/Widgets.cpp` comment admits the Science packaging button “had no dispatch at all, so the button was dead.”
- `70e7e8d` “Remove false port claims and generated audit artifacts”; `838de84` “Delete PARITY.md”.

Consequence: the port is not merely the original game with UI bugs. It contains unapproved redesign and self-scoped verification.

## 2. Recovery method: reference-first, evidence-gated reconstruction

Do **not** continue blind patching. Do **not** big-bang rewrite without evidence. Use the original HTA as an oracle, compare full state and real UI behavior, and repair or replace one bounded slice at a time.

### 2.1 Keep three artifacts separate

1. **Reference:** unmodified `Spaceship.hta` and its original save format.
2. **Incident artifact:** the exact build the user tested after Phase 9, if available; otherwise commit `11d68d1` plus the user’s witness report. Do not substitute current head for the incident artifact.
3. **Repair branch:** current working tree. Every change is small, traceable, and reversible.

Preserve hashes, compiler output, launch method, OS, display scaling, and saves. Do not delete inherited work to make the repo look clean.

### 2.2 Build a faithful reference oracle

The oracle must run the original HTA logic, not a reimplementation.

Preferred order:
1. **Real Windows HTA runtime.** Run `Spaceship.hta` under `mshta.exe` on a Windows desktop. Use the existing `tools/compare-hta-port.ps1` for human-guided captures, and add an instrumentation wrapper that does not alter game logic.
2. **Instrumented copy, not modified original.** Generate a harness copy that appends an export script after the original game script. It may call original globals such as `saveSnapshot()`, `validSave()`, `applySnapshot()`, `startInstall()`, `turnOnFilter()`, `startProject()`, etc. It must not copy their bodies.
3. **Real UI events where possible.** Drive clicks through the DOM controls so `commandRow`, `wireAction`, and `buildProgressButton` apply cooldowns and enabled/disabled state. If direct function calls are used, record that the UI wrapper path was not exercised.
4. **Cosmetic stubs only after proof.** Sound, sparkle marks, and timers may be stubbed only if source inspection shows they do not mutate saved state. `refreshLifeInfo()` is not cosmetic and must not be stubbed.

State contract:
- Use the HTA’s own `saveSnapshot()` as the canonical full-state export. It includes the mutable stores: `STATE`, `SHIP`, `ROOMS`, `CREW`, `STATS`, `INFOS`, `LIFE`, `MESS`, `CARGO`, `GOODS`, `SURVEY`, `MAP`, `views`, `EVENTS`, `scanCost`, and `actions`.
- Use `validSave()` as the reference validator.
- Compare full saves before/after each scenario, not the port-defined `Trace.cpp` projection.
- Capture transient presentation state separately: active tab, scroll, hitboxes, visible text, enabled controls, screenshots.

Oracle validation:
- Take a known HTA save. Load it in the real HTA, perform one action, save. Load the same save in the oracle, perform the same action, save. The saves must match except for deliberately excluded cosmetic timestamps.
- Mutation-test the oracle: intentionally change a port value, cost, or label and confirm the comparison fails. If the oracle cannot detect a known mismatch, it is not a gate.

### 2.3 Add native introspection without changing gameplay

Proposed debug-only additions, not implemented by this plan:
- Full-state export using the existing save format (`Simulation::SaveJson()`), not `TraceJson()`.
- UI hitbox/control tree export: id, label, box, enabled, cooldown key, action target.
- Action log: which UI control was pressed, whether an action committed, cooldown before/after, state delta.
- A test driver that presses view-model/UI actions, not core shortcuts. The console may remain for quick model checks, but it is not a parity gate.

### 2.4 Replace sampling with a coverage map

Derive the inventory from the HTA source and user workflows, not from port capabilities.

For each workflow record:
- HTA functions and line references.
- Native counterpart, if any.
- Save keys touched.
- UI controls and cooldown category.
- Free vs paid actions.
- Positive, blocked, and failure cases.
- Status: `UNTRACED`, `TRACED`, `IMPLEMENTED`, `COMPARED`, `ACCEPTED`, `BLOCKED`.
- Evidence paths.

Minimum coverage areas:
- Startup, title menu, new voyage, load, save, return-to-game.
- Home/ship diagram, sidebar, status rails, scrolling.
- Every room/facility: inputs, costs, limits, outcomes, disabled states, messages.
- Navigation: target selection, scan, travel, layers, zoom, pan, arrival, acknowledgement.
- Crew: jobs, orders, quarters, talk, medical, recreation, bonds, fatigue, hunger, morale.
- Station: trade, hire, upgrade, shipyard, contracts, commissions.
- Events/incidents, Simulacron, signal story, commendations, wiki.
- Options, themes, audio, large text, reduced motion, save migration.
- Action clock and cooldowns: free actions do not tick; paid actions tick once; cooldown starts after commit.

### 2.5 Repair loop for each defect

Use this order. Do not skip steps.

1. **Name the defect.** Stable ID, user symptom, affected screen/system.
2. **Record the witness account.** Treat it as authoritative for the incident. Do not require the user to prove it again.
3. **Reproduce both sides.** Same save/state, same actions, same window size/DPI. Capture before evidence.
4. **Trace the HTA.** Source path from input to state mutation and presentation.
5. **Trace the port.** Corresponding input, model, and drawing path. Check history before assuming inherited code is correct.
6. **Write the expected delta.** What must change; what must remain unchanged. If uncertain, stop and mark `BLOCKED`.
7. **Create a failing check.** A full-state diff, UI capture, or control-reachability test that fails on the current build.
8. **Implement narrowly.** One defect per commit. No unrelated refactor. No silent scope change.
9. **Repeat the same comparison.** Full state, UI, negative paths, likely regressions.
10. **Set status honestly.** `VERIFIED FIX` only after the original symptom is reproduced, the fix is exercised, and the result matches the HTA or an explicitly approved deviation. For P0/P1, require user acceptance.

### 2.6 Triage order

P0 — cannot evaluate or unsafe:
- Blank startup, missing assets, crash, dead paid controls, save loss/corruption.

P1 — wrong game state:
- Wrong costs, wrong action clock, wrong cooldowns, wrong resource effects, wrong save/load, missing state updates such as life-support readout.

P2 — visible information/interaction parity:
- Overlapping text, hidden values, missing room names, broken scroll, unreachable controls, wrong panel content.

P3 — approved polish:
- 18px reflow quality, palette decisions, cosmetic motion, audio preferences.

Do not claim visual polish while P0/P1 are open.

## 3. Anti-hallucination protocol

### 3.1 Evidence tags

Every claim must carry one tag:
- `USER-WITNESSED`
- `SOURCE-CONFIRMED`
- `RUNTIME-OBSERVED`
- `TEST-COVERED(scope named)`
- `INFERRED`
- `UNKNOWN`

No code change may rely on an `INFERRED` claim unless it is first converted to source/runtime evidence or explicitly approved by the user.

### 3.2 Assumption ledger

Maintain a table:
`ID | claim | source | evidence tag | status | next check | owner`.

Review it before each repair. If an assumption is load-bearing and unverified, stop.

### 3.3 Independent comparison

The comparison schema must come from the HTA save format and DOM, not from the port. A second reviewer (human or separate agent) should read the HTA source and the diff without inheriting prior agent conclusions.

### 3.4 Gate discipline

Separate gates:
1. **Compile gate:** clean Windows build with the actual GUI target, exact toolchain, warnings captured.
2. **Asset gate:** packaged exe runs from Explorer and from a different working directory; fonts/title art/audio found.
3. **Smoke gate:** nonblank first frame, title menu usable, new voyage reaches home, Navigation opens, save/load works.
4. **Reference parity gate:** full-state and UI comparison for the touched workflow.
5. **User acceptance gate:** P0/P1 fixes accepted by the user.

A skipped GUI test is not a pass. A headless model test is not a GUI test. A green compile is not a launch test.

### 3.5 PR and commit discipline

Every repair commit/PR must state:
- Defect ID.
- User symptom.
- HTA source reference.
- Port source reference.
- Scenario/save used.
- Before/after evidence.
- Exact test scope.
- Remaining unknowns.

No title may say “faithful,” “verified,” or “1:1” unless the evidence supports that exact scope.

### 3.6 Stop conditions

Stop and ask when:
- The HTA cannot be run or traced confidently.
- A fix would change costs, labels, state transitions, or save format.
- A test passes but the user says the build is broken.
- A required deviation is not explicitly approved.
- The repair would become a broad rewrite.

## 4. Implications that require user decision

### 4.1 18px typography vs pixel identity

The 18px floor is an approved deviation from the HTA CSS baseline. It makes pixel-perfect geometry impossible in many places without reflow.

Default rule:
- Preserve all information, ordering, interactions, and behavior.
- Adapt geometry/scrolling/wrapping to 18px.
- Document expected layout differences per screen.
- Do not shrink text or truncate meaningful values.

If the user wants pixel identity instead, the 18px constraint must be relaxed or the layout must be explicitly redesigned. That is a user decision, not an agent assumption.

### 4.2 Phase 9 additions

Default rule:
- Restore HTA themes and labels unless the user explicitly approves the Phase 9 palettes.
- Treat `fontStep`, fullscreen, player audio, and tutorial as additions requiring approval.
- If kept, isolate them behind options and document save-compatibility effects.

The HTA ignores unknown option keys, so native additions do not break HTA loading, but HTA re-save drops them. Do not silently persist unapproved scope.

### 4.3 Native reimplementation vs original-document host

There are two possible targets:

A. **Native C++ reimplementation.** Current mission. Higher risk, but satisfies the native-port goal. Requires the reference-first repair loop above.

B. **Original-document host.** Run the original HTA/document inside a native window or webview. This is the lowest-risk path to exact original behavior, but it changes the architecture and may not satisfy “native port.” It also has implications: ActiveX file saves vs browser localStorage, IE-era JS, window chrome, and packaging.

Do not choose silently. If exact original experience is the priority and native graphics are not required, present option B for approval. Otherwise continue option A with module-level replacement where evidence shows the port cannot be trusted.

### 4.4 Module keep/replace decision

After the coverage inventory, decide per module:
- **Keep and repair:** source-traceable, bounded defects.
- **Replace from HTA:** pervasive assumptions, missing state paths, or untraceable behavior.
- **Block:** reference cannot be observed.

Do not patch a module that is fundamentally a different game.

## 5. First concrete actions when approved

1. Preserve the incident artifact, current build, original HTA hash, and user witness ledger.
2. Build current head on Windows with the actual GUI target. Capture compiler output and first launch. Do not call it fixed.
3. Run the original HTA on Windows. Export `saveSnapshot()` before/after one known action. Validate the oracle against the real runtime.
4. Replace one parity workflow with a full-save comparison driven by original HTA entry points or real UI clicks. Start with a P0/P1 workflow, not a cosmetic one.
5. Mutation-test the new gate: introduce a known port mismatch and confirm it fails.
6. Triage P0 defects from the user’s witness report and the coverage map. Repair one at a time using the loop above.
7. Present an acceptance packet for each P0/P1 fix: symptom, change, repro, before/after captures, save diff, remaining unknowns.

## 6. Definition of done for a repair

A repair is done only when:
- The user symptom is reproduced or explicitly accepted as witnessed.
- The HTA expected behavior is source-traced.
- A failing check existed before the fix.
- The diff is minimal and scoped.
- The same scenario passes full-state and UI comparison, or the deviation is explicitly approved.
- The Windows packaged build launches and passes smoke checks.
- No skipped GUI gate is counted as success.
- P0/P1 fixes have user acceptance.
- The damage-control index and report are updated with exact evidence.

## 7. What this plan deliberately does not do

- It does not change game code now.
- It does not declare the current core healthy.
- It does not delete inherited work to hide history.
- It does not let a green test overrule the user’s witness report.
- It does not assume 1:1 is impossible.
- It does not approve Phase 9 scope changes by silence.

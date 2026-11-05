# Spaceship: Repair & Rescue

## Mission

Convert the original `Spaceship.hta` game into the native port **faithfully, 1:1**. The HTA is the behavioral and visual reference. The port is not a redesign, reimagining, simplification, or opportunity to replace unfamiliar behavior with a plausible alternative.

The path is hard because the reference is a large, stateful game, not just a collection of screens. Make progress by tracing one behavior at a time, preserving its state changes and presentation, and proving the result against the reference. When evidence is missing, leave the issue open instead of guessing.

## Non-negotiable rules

1. **The HTA is the source of truth.** Before changing a screen or game mechanic, find the HTA function(s), event handler(s), data and layout rules that implement it, then find the native counterpart. Record both in the investigation log.
2. **Parity means behavior and information, not just a similar screenshot.** Match the visible text, values, ordering, geometry, interactions, costs, state transitions, side effects, save/load behavior, and failure cases relevant to the task.
3. **Do not invent behavior.** Label claims as observed, inferred, or unknown. If the original behavior cannot be run or traced confidently, mark it blocked or unresolved. Never fill gaps with fabricated state, convenient assumptions, or “what a game like this would do.”
4. **Do not hide differences.** Do not silently omit content, truncate meaningful values, make controls inert, change costs, or alter a mechanic to make a screen easier to draw. If a difference is intentional or unavoidable, document it and get approval before treating it as acceptable.
5. **Typography is constrained to 18 px.** Do not “fix” overflow by shrinking text to the HTA’s CSS baseline. Repair panel dimensions, wrapping, clipping, scrolling, and hitbox/layout geometry while preserving 18 px text.
6. **Make the smallest evidence-backed change.** Avoid broad refactors or unrelated gameplay edits during a repair. Preserve existing behavior unless source comparison demonstrates a mismatch.
7. **Verification must match the claim.** A successful build proves compilation; a unit test proves only the behavior it exercises. Neither proves visual or overall gameplay parity. Do not claim a repair is verified until the same workflow has been compared in the HTA and port.
8. **Assume inherited agent work may contain hallucinations.** Comments, PR summaries, test names, hard-coded values, and earlier agents’ conclusions are leads—not specifications. Verify each important claim against the HTA implementation and the native behavior. Check that a test actually covers what its name or comment claims; do not assume a plausible implementation is real source behavior.

## Inherited-work sanity check

A native port can carry forward mistakes from earlier agents: invented behavior, guessed dimensions or costs, copied assumptions that do not match the HTA, stale comments, and claims that a feature or test is complete when only a narrow path was exercised. **Do not trust code because it is already in the repository.** This is a warning to investigate, not a reason to delete or rewrite code blindly.

Before building on inherited work:

- Trace the claimed behavior in `Spaceship.hta` and the actual port call path. Treat comments, commit messages, issue summaries, and this document’s older notes as secondary evidence.
- Read the test itself and its fixtures/scripts. Record precisely which actions, states, and outputs it covers; a passing selected trace does not prove full gameplay or UI parity.
- Separate `source-confirmed`, `runtime-observed`, `test-covered (scope named)`, `inferred`, and `unverified` claims in the damage-control log.
- Verify numeric constants, default values, string labels, geometry, costs, and state mutations against the source before changing or relying on them.
- If a previous claim turns out to be unsupported or wrong, add a dated correction/follow-up to [`traps_paid_for/damagecontrol_report.md`](traps_paid_for/damagecontrol_report.md) and update [`traps_paid_for/damagcontrol_index.md`](traps_paid_for/damagcontrol_index.md) when it affects an open defect. Preserve the old log entry; correct it with evidence rather than silently erasing it.

## Required repair loop

For each defect, work in this order and record the result in [`traps_paid_for/damagecontrol_report.md`](traps_paid_for/damagecontrol_report.md). Keep the current short list in [`traps_paid_for/damagcontrol_index.md`](traps_paid_for/damagcontrol_index.md).

1. **Name the defect.** Give it a stable ID, affected screen/system, user-visible symptom, and current status.
2. **Reproduce both sides.** Start from a named save or reproducible state. Record the exact actions and result in the HTA and native port. Save baseline evidence before editing.
3. **Trace the reference.** Follow the HTA event and render/data path from input to displayed result and state mutation. Do not rely on CSS or a comment alone when runtime logic is involved.
4. **Trace the port.** Find the corresponding native input, model/simulation, and drawing paths. Check relevant history before assuming existing gameplay code is correct or broken.
5. **Write the expected delta before coding.** State what should change and what must remain unchanged. If the expected result is uncertain, stop at investigation and mark it `BLOCKED` or `OPEN`.
6. **Implement narrowly.** Keep business logic, presentation, and interaction changes scoped. Preserve all source fields and meaningful text; make overflow visible through correct geometry and scrolling rather than hiding it.
7. **Repeat the same comparison.** Use the same save/state, action sequence, viewport, and display conditions. Check the reported symptom, related screens, and likely regressions. Add captures, commands, and results to the log.
8. **Set status honestly.** Use `VERIFIED FIX` only when the original defect was reproduced, the fix was exercised in the port, and the result was checked against the HTA. If the HTA or native runtime cannot be launched, record the limitation and leave parity unverified.

## Comparison tools and evidence

- Use [`tools/compare-hta-port.md`](tools/compare-hta-port.md) and [`tools/compare-hta-port.ps1`](tools/compare-hta-port.ps1) for a human-guided Windows side-by-side capture. The script gathers screenshots and notes; it does not automate input or declare a pass.
- Existing visual evidence and the known traps are in [`traps_paid_for/`](traps_paid_for/). Treat supplied screenshots as evidence for their captured state/build only, not as proof of the current build.
- Keep a test save or copy for investigations. Record its identity/state without committing private or user save data.
- For each comparison, record the port commit/build, OS/runtime, window dimensions, display scaling, starting state, action sequence, expected result, observed result, screenshot paths, test commands, and anything not tested.
- Compare values and state transitions as well as layout. When useful, add a focused automated parity/trace test so the behavior remains covered after the visual repair.

## Coverage map: do not call the port complete by sampling a few screens

Track completion by workflow and state, not just by whether a screen exists:

- Startup, title/new voyage, load, save, and return-to-game flows.
- Home/ship overview, vessel and room diagram, status rails, sidebar, indicators, and scrolling.
- Every service/system screen: inputs, resource costs, limits, outcomes, disabled states, and messages.
- Navigation: target selection, scanning, route plotting, travel costs, layer/zoom/pan controls, and arrival behavior.
- Crew, quarters, jobs, assignments, orders, contracts, events, rewards, handbook/wiki, and other windows.
- Simulation progression over time/actions, resource consumption, incidents, failure/loss, and save/reload persistence.

Maintain explicit per-area statuses in the damage-control index or log. A screen being present is not evidence that its data, controls, or underlying behavior are complete.

## Completion bar

Call the conversion 1:1 only when the covered HTA workflows have been traced and tested in the native port, displayed information and interactions are accounted for, saved state round-trips correctly, and the comparison evidence shows no unexplained differences. List any remaining exceptions explicitly. Until then, describe the work as partial and identify what remains unverified.

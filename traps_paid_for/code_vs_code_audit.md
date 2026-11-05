# Code-versus-code repair audit

## Authority and acceptance

Read `traps_paid_for/Developer Note` and `REPAIR_AND_RESCUE.md` before this pass. Inherited comments, phase-completion claims and test counts are not specifications.

References: original `Spaceship.hta`, the supplied runtime montage `10_reference_HTA_version_truebuild.png`, and supplied concept images `spaceship01.png` through `spaceship05.png` and `spaceshipi06.png`. The pictures establish the action economy, idle/free observation, indicators, crew and parchment visual language. Do not replace executable source behavior with literal incidental example values in the concept illustrations.

**Explicit user correction to the HTA:** a repeatable one-action purchase must lock for **five subsequent committed actions**, show its countdown and become usable again. A free browse/selection click, blocked repeat, repaint or wall-clock delay must not decrement it. Source categories and multi-action task durations are separate concepts; do not silently flatten all ongoing work to five actions. This pass applies the requested five-action rule to station commands and records the override rather than presenting it as unchanged HTA behavior.

## Pass 1 — findings and repairs (2026-11-05)

| Workflow | Original code | Port code | Finding / change | Evidence status |
|---|---|---|---|---|
| Action clock | `performAction`, `tickActionCooldowns`, `commitActionButton` | `core/Action.cpp`, `core/Cooldowns.cpp`, `ui/Widgets.cpp::CommitActionButton` | Source uses committed actions, not time. Keep this clock. | Source inspected; bounded regression cases run. |
| Command presentation | `commandRow`, `paintActionCooldownControl` | `Widgets.cpp::BuildTabScreen`, `Draw.cpp::PaintPanelsScreen` | Six builders constructed plain rows and bypassed cooldown binding. Bind station, stores, fitted rooms, Simulacron, awards and event rows to the **same existing key** their dispatcher enforces; show `COOLDOWN N`, disable hit generation while locked. No second lock or new save format. | Shared adapter patched; station actual drawing/click path recorded; other five adapters need individual runtime workflow comparisons. |
| Station purchase lifecycle | `buildStationScreen`, `commandRow` | `StationScreen.cpp::PressStationAction`, station branch of `BuildTabScreen` | Previously refused repeat internally but still displayed `1 action`. Display now uses that lock. Station one-action commands explicitly use routine/five actions per user correction. Price/purse/stock/capacity behavior preserved. | Test buys fuel, rejects spam, makes five **different** transactions, checks 5→4→3→2→1→0 and buys fuel again. Actual Draw.cpp recorder submits `COOLDOWN 5` and refuses a second graphical-row click. |
| Filter cabin-air illustration | `paintO2`, `paintAir`, `.o2-air*` CSS | `O2Screen.cpp::BuildFilterMachine`, `Draw.cpp::PaintStage` | `AirChamber` model existed but the scene was a static placeholder labelled `air`. Emit actual cabin label, air-stage word/tone, remaining filtered/hold reading, 544px proportional bar, three notches, clean streaks or degradation dust. Renderer consumes the authored/state-dependent colours. | Model tests and real painter call recording; GPU/font appearance unverified. |
| Filter slots | `paintO2` slot loop | `BuildFilterMachine` | Empty slots previously always drew a canister; state text stayed EMPTY and charge width stayed zero. Show canisters/necks only when seated; emit EMPTY/INSTALLED/ACTIVE/NO POWER and source-rounded charge. | Tests exercise empty, seated, active, advancing charge, zero power and exhausted hold. |
| Filter visual progression | `.o2-streak`, `paintAir` | same | Source streaks have no CSS animation declaration; state is rebuilt as actions change. Restore this dynamic progression, not an invented real-time consumption/animation loop. Reflow the original tiny air text within the chamber for >=18px. | Source inspected. Complete matched runtime comparison still required. |

### Checks for this pass

- Full local core build and CTest, including pinned GUI-header syntax and optional CPU native drawing/input probes. Record final result in `damagecontrol_report.md`.
- Recorder uses actual `Draw.cpp` but stub raylib rasterization and deterministic font measurement. It does **not** prove GPU appearance, native OS interaction, animation smoothness or memory stability.
- Existing HTA/core differential traces remain applicable only to their scripted paths. The five-action station correction intentionally differs from source categorization for e.g. station upgrades/departure; this is user-authorized, not an unnoticed parity failure.
- Keep previous raylib black-screen safeguards intact. No renderer build flags, frame-loop pacing or input polling changed in this pass.

## Remaining coverage — do not mark complete

| Area | Next source-to-port comparison | Status |
|---|---|---|
| Station | Every goods/contract/hire/upgrade/shipyard transaction, changing prices/stock, confirmation, refusal, saved lock and tab scope | Initial lifecycle/display repaired; exhaustive audit OPEN |
| Other command panels | Every adapter's actual dispatch, enabled/refusal rules, display countdown, return-to-ready and requested one-action rule | Shared display repaired; per-workflow semantics OPEN |
| All progress controls | Idle/running/done/acknowledge/repeat locks; projects, treatments, travel, scans, assignments | Earlier bounded traces only; exhaustive audit OPEN |
| Engine | Packed `label|value` rendering, machine labels/geometry, wear, repair thresholds and crew jobs | OPEN |
| O2 | Whole room layout against montage, all air stages and crew automation, depleted rack, recharge, power loss/recovery, action-time visuals | Stateful scenery repaired; exhaustive/graphical comparison OPEN |
| Mess | Counter, stock, food projects, kitchen automation and shared meals | OPEN |
| Home/sidebar | Source Wren/Kestrel geometry, all indicators and state cascades, crew dots, vessel and rail text, free browsing | Prior partial repairs; full audit OPEN |
| Crew | Starting roster per actual source, selected record, job staging, orders, fees, fatigue/hunger/morale, medical absence, training and dismissal | OPEN |
| Navigation | Target/layer/zoom/pan, free plotting, every route cost, salvage, arrival, docking and scanning | Prior bounded traces; full audit OPEN |
| Incidents/fitted rooms/Simulacron | Authored descriptions, all options, irreversible choices, resource changes and future consequences | OPEN |
| Persistence | All new/old saves, cross-edition round-trip, running jobs and locks, slot UI and errors | Existing bounded tests only; full audit OPEN |
| Title/options/audio | Artwork/sheets, palettes, accessibility/readability, audio controls, runtime frame/resource stability | Prior fixes; visual/runtime audit OPEN |

No complete-codebase or 1:1 sign-off. No merge authorized by this audit.

## Pass 2 — focused health chain

[Clause-by-clause HTA/native health audit](health_source_audit.md): read action ordering, filter/air stages, hunger/starvation, worker costs/availability, treatment and death cleanup before editing. Two new full-trace differential scenarios match source. Found missing Quarters condition bars and shared painter ignoring existing authored bar colours; repaired presentation without inventing core health rules. **219/219 local tests pass**; GPU appearance and the remaining health workflows are still open.

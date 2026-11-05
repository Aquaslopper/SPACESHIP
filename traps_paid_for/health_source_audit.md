# Health / air / food / workers — focused source audit

Date: 2026-11-05. This is a clause-by-clause comparison of the chain below, **not** a claim that every line of the whole game has been audited. Authority: `Spaceship.hta`, supplied runtime montage/concept images and five tutorial cards, with explicit user corrections recorded separately. Read the Developer Note; do not trust inherited completion claims.

## 1. The actual heartbeat

| HTA clause | Native counterpart | Comparison |
|---|---|---|
| `performAction` (2638 onward): reject after loss; increment counter; tick existing locks; call world tick; log detail; check commendations | `Action.cpp::PerformAction` | Same order for the health chain. Reading/painting is not an action. |
| `tickWorld` (2665 onward): wear; energy every 20 actions; filters; crew; cleaner; loss; stop if game over; payroll; tasks; desires; auto-maint/filter/cook; incidents; sim; loss again | `Action.cpp::TickWorld` | Compared call by call. In particular **filters run before health**, food consumption runs before task production, and death cleanup precedes new automatic work. No reordering introduced. |

## 2. Air reaches bodies and work

| HTA clause | Native counterpart | Comparison |
|---|---|---|
| `tickFilters` (2714 onward): charge burns only with running bank; exhaustion clears slots, resets hold to 20 and returns without spending hold on the same action | `Survival.cpp::TickFilters` | Same branches/early returns. |
| `bankRunning`, `airStage`, `airSpec`, `airPercent` (2760–2794): positive charge **and power**; otherwise ceil(hold/5) clamped 0–4 | `Survival.cpp::BankRunning/AirStageIndex/AirSpec/AirPercent` | Same conditions; no wall-clock health timer added. |
| `AIR_STAGES` (1914 onward): FOUL harm every 5 crew ticks, BAD every 10, others zero; slow factors 2 / 1.5 / 1.25 / 1 / 1; job drops 10 / 10 / 5 / 0 / 0 | catalogue rules → `AirSpec`; `CrewLogic.cpp::TickCrew` | Source constants and use inspected. Health harm uses the existing global **crewTick** modulus, not a new timer started on entry to a stage. |
| `crewCost` (2797 onward): ceil(work × air slow), minimum 1; `effectiveJobAt`: jobAt minus air drop, minimum 5 | `Survival.cpp::CrewCost`; `CrewLogic.cpp::EffectiveJobAt` | Same arithmetic. The source does not specify an invented continuous health-percentage productivity multiplier here. |
| `autoFilters` (3088 onward), `autoMaint` (3146 onward), `autoMess` (3033 onward): assigned and non-busy worker, source threshold/resource gates, schedule air-adjusted work after the current task sweep | `CrewLogic.cpp::AutoFilters/AutoMaint/AutoMess` | Compared guards and scheduling. New trace demonstrates foul-air tasks: engineer 18, canister 4, cooking 48; subsequent clean air does not rescale already scheduled tasks. |

## 3. Crew tick, read in source order

`tickCrew` (2963–3024) → `CrewLogic.cpp::TickCrew`:

1. Read current air specification; increment crewTick.
2. On its air-harm modulus, subtract one health from **every** crew member, clamped to zero, and log the air warning. The code does not exempt a medical patient; do not invent that exception from the “not aboard” comment.
3. Every ten crew ticks: employed/untrained hand loses one morale; employed fatigue +1 capped 100; idle fatigue −2 floored zero.
4. For each individual crew record: increment its own hungerTick; before 25 do nothing; at 25 reset it.
5. If food exists: consume one ration, hunger −40 floored zero, clear empty-rack warning.
6. Else: hunger +15 capped 100, issue the empty-rack message once.
7. **After eating**, every eighth crew tick, hunger >=90 loses one health, floored zero.

All seven clauses have native counterparts in the same order. No core rewrite was warranted by this comparison. A successful meal can avert starvation on that very tick; bad air and starvation can stack. These are tested, not assumed.

## 4. Food is produced while cooking

`TASK_TYPES.cook.onTick`, `advanceTasks` → `Tasks.cpp` task hooks and task snapshot sweep. Source produces food while the bake runs, not only at completion. A newly spawned worker task is not advanced by its own spawning tick. The worker trace demonstrates rack 0 → 1 → 2 during an ongoing foul-air bake. This bounded trace is not an exhaustive test of every food project, shared meal or pro-action burst.

## 5. Care, needs and death are different paths

- `needKey` (8130 onward) → `CrewLogic.cpp::NeedKeyFor`: health <75 takes precedence, then bad air, fatigue >70, hunger >70, morale <60, untrained job, idle, fine. Do not call the quote-advancing `crewNeed` from read-only displays.
- `crewBusy` (7887 onward) → `CrewLogic.cpp::CrewBusy`: claims, medical, shared meal, own orders, and named running worker jobs block conflicting assignments/treatment. Trace verifies treatment refusal on a busy engineer spends no action.
- `startMedical` (10332 onward) → `Projects.cpp::StartMedical`: valid injured non-busy person, kit, energy >2, no loss; consume kit/2 energy; claim during committed action; recheck survival; add a three-action shipProject. Test harness now calls the **original HTA startMedical**, not a transcribed approximation.
- `TASK_TYPES.shipProject.onDone` medical branch → `Projects.cpp::FinishProject`: +25 health capped 100, activity and memory/log. Trace verifies 50 →75.
- **Different mechanic:** `TASK_TYPES.crewDoc` (3254 onward) → `Tasks.cpp` crewDoc completion: +12 capped 100 after the orders/doctor absence. Preserved; not silently replaced by the direct medical project's +25. This pass read the code but does not claim a new differential test of that entire orders flow.
- `checkLoss` (9756 onward) → `CrewLogic.cpp::CheckLoss`: remove zero-health crew, clear orders and personal medical tasks, cancel projects using them, cancel named worker jobs and refund a carried canister within capacity, then check hull/all-crew loss. Trace verifies worker death interrupts seating and returns stock 1 →2. No new death threshold, revival or healing after removal introduced.

## 6. Demonstrated presentation defects and repairs

### Quarters condition bars omitted

HTA `paintCrewPanels` → `statLine` (8167–8200) renders Health, Morale, Fatigue and Hunger as numbers **plus bars**. Native `QuartersScreen.cpp` emitted only four fields. Restored four associated `NavPanel::Bar` lines.

Each bar width remains its **actual value**. Only tone judgement reverses for fatigue/hunger: `good = 100-value`, otherwise `good=value`; green >=70 (`#7f9161`), amber >=40 (`#c08a3e`), red below 40 (`#b05a42`). This is copied from source, not a new health rule.

### Shared painter ignored bar colour

HTA `barLine` and `vitalRow` use their provided tone. Native `CrewRecord.cpp::AddVital` already provided correct bar values/tones, but `Draw.cpp::PaintPanel` painted every bar coffee brown. The painter now uses the supplied authored hex tone, retaining its prior fallback for uncoloured bars. This also fixes source-toned bars in other panels through the shared path.

No font reduction, invented descriptions, core health formula, save-schema change or renderer frame-control/configuration change.

## Evidence and boundaries

- `tests/parity/health-chain.script`: actual HTA action/world/crew/loss/treatment callbacks vs native, full canonical traces. Eating before starvation; empty rack; BAD-air boundary; combined harm/death; medical 50→75; automatic worker seating and death/refund.
- `tests/parity/health-worker-air.script`: actual automatic engineer/filter/cook costs under FOUL air, food production during cooking, busy-treatment refusal and unchanged scheduled durations after air recovery.
- `crew-set` / `survival-set` are whitelist-restricted **test fixture setup** commands, identical on both sides. They position boundary states without spending hundreds of unrelated actions; they are not game controls, replacement ticks or alternate save loading. No simulation behavior was stubbed in these paths. HTA DOM/repaint is still stubbed.
- UI model test checks numeric values, non-inverted widths and source colours for low health/high hunger/fatigue.
- Real `Draw.cpp` CPU recorder sees red critical-health bars, amber intermediate morale and a live updated health reading. Deliberately restoring brown-only rendering makes it fail; fixed code passes. Recorder uses fake rasterization/font measurement and is **not** an OS/GPU screenshot comparison.
- Full local suite: **219/219 pass**, including GUI syntax/input/drawing probes; no skips. Logs `/tmp/health-build.log`, `/tmp/health-tests.log`; focused traces `/tmp/health-*-native.log` / `/tmp/health-*-js.log`.

**Conclusion:** the inspected health/air/food/worker chain exists and matches the original in these reproducible scenarios; its visible health feedback was incomplete. That is not proof that every health interaction, UI state, save or future gameplay branch is correct. Remaining: full doctor/order lifecycle differential coverage, incidents/medical interruptions across all states, health persistence boundary scenarios, all affected panels' geometry and matched native graphical comparison. The overall code-versus-code audit remains open; do not merge on the strength of this pass.

# Follow-up repairs to the 2001–4000 review

## User clarification and authority

The user tested and approved the previous build, then authorized this repair pass. The HTA's actual cooldown tiers are correct. Five-action fallback repairs for injected/spammable/permanently locked controls do NOT replace an explicitly authored HTA cooldown. The earlier review's N58 policy-conflict conclusion is superseded by this clarification.

The HTA remains authoritative for worker initiative, training thresholds, ongoing work, feeding, and machine life cycles. No new training-speed multiplier, eating pause, action clock, or machine-life rebalance was introduced.

## Request-by-request result

1. **Cooldowns:** preserved the HTA routine/system/space/critical durations of 5/10/15/20 and the five-action fallback for an unregistered rule. Added regression assertions; did not flatten the tiers to five. Existing command/progress locks remain action-driven.
2. **Timestamps:** `Action.cpp::LogLine` now records local HH:MM:SS. The Home feed displays saved stamps with messages; Activity Log already supported them. The wall clock is cosmetic only, and remains excluded from gameplay parity traces. Save schema unchanged.
3. **Newest first:** a new log entry resets internal feed scroll, including at the 200-entry cap. A transient revision tracks entries even when timestamp/message text repeats. Loads and returning to Home reset the view too. Free wheel/drag scrolling remains functional between updates.
4. **Scan cost wiring:** a new voyage now initializes the active scan-cost field from the catalogue. Verified the actual HTA `startScan` at lines 6056–6074 and `simScanCost` at 10620: `max(1, SCAN_COST - (sensor ? 2 : 0))`. This supplied file makes travel, not scan duration, depend on distance. Native retains that formula: normal base 5, upgraded base 3, one-use calibration reduces by 2, minimum 1. A distance-based scanning formula was NOT invented. If a different source version is intended, that remains a point for clarification.
5. **Feed measurement:** the production Home builder uses the actual built height of rails, stage, note, nav/dock strips and information above the feed, then applies the authored 62–132 clamp and 4 px allowance. It no longer uses the guessed 300 px feed allowance. The explicit measured-height overload remains for other callers.
6. **Map measurement:** removed the painter's hardcoded 300 px allowance. It parks the map at 240 and measures actual panel heights, wrapped notes, command rows, header, under-chart content and the existing pinned action band.
7. **Fixed centered scenes:** engine/O2/mess instruments keep their authored sizes and are centered in the content viewport. Panels flow below when they cannot fit beside, rather than being painted underneath the scene. The 560×300 Quarters hallway is also centered; roster/orders reflow around it. No beta hull, small-room or room-hero art was changed.
8. **Capped map centering:** a map narrower than its available middle column now centers in that column.
9. **Logical viewport:** explicitly unchanged, as approved. No resize writes to MAP.vw/vh were added.
10. **Map minimum/cap ordering:** explicitly unchanged, as approved.

**N52 task retirement:** auto-clear now explicitly lists the types whose HTA specifications opt into it: crewOrder, crewJob, crewDoc, o2slot, maint, cook, incident, salvage, surveyPackage and shipProject. Unknown types are retained on completion. Travel/scan retain acknowledgment. Workers' automatic maintenance still completes and clears with no commander acknowledgment.

## Validation performed

- Fresh GUI-OFF CMake build passed.
- CTest: 198 passed, one skipped (`ui.gui-syntax`); 199 registered tests in total.
- Expanded real `Draw.cpp` CPU recording/input tests passed: fixed scene centering at widths 1180/1280/2200; non-overlapping engine side panels; Quarters hallway centering; capped-map centering; chart-height response to long scan content; unchanged logical viewport; feed timestamp/new-entry reset; previous wheel/drag/green-mark/pinned-band/confirmation checks; all recorded text at least 18 px.
- Added core regressions: explicit task retirement; unregistered finished task retained; trained engineer starts at 60% with four work actions versus untrained 30%/seven, neither charged by its birth tick; automatic completion resets wear; next four actions cost the original 1%; catalogue scan cost and calibration, including scanner upgrade; cosmetic timestamps round-trip without advancing actions/air; original cooldown tiers/fallback.
- Earlier bounded first-2000 source/data/palette/scene/JPEG gate passed unchanged.
- `git diff --check` passed.

These checks are not GPU screenshot or live visual-parity proof. No commit, push or merge was performed in this repair pass.

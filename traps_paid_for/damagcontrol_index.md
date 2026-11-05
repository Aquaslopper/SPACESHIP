# Damagcontrol index — open defects

Short entry point for current issues. Detailed evidence, history, and handoff notes live in [`damagecontrol_report.md`](damagecontrol_report.md). Keep this index concise; update statuses here and append investigation details to the report rather than rewriting its history.

| ID | Status | Affected screen / area | Defect summary | Investigation log |
|---|---|---|---|---|
| UI-001 | IN PROGRESS | Ship / home dashboard | Stat labels and values use 18 px minimum text with fixed vertical offsets that can overlap. Containers now grow to fit the text at each typography step; the dock strip follows the nav strip's real bottom; main-content scrolling (`contentScroll_`) is wired into home paint and hit-testing so the column stays reachable when it overflows. Source/build/test verified; Windows side-by-side not yet run. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05), [2026-11-05 UI-001 repair](damagecontrol_report.md#ui-001-repair-2026-11-05) |
| UI-002 | OPEN | Shared chrome and content | ImGui chrome and Raylib content use different effective font metrics; layout and hitboxes may not match rendered text. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| UI-007 | OPEN | Ship / home dashboard | The stat rail's `unit` (e.g. "%", " air", " ration/action") is present in the native model (`Stat.unit`, `cell.unit`) but is never painted by `PaintIdle`; only label and value are drawn. Not repaired in the UI-001 slice; tracked separately so it is not described as preserved. | [2026-11-05 UI-001 repair](damagecontrol_report.md#ui-001-repair-2026-11-05) |
| UI-003 | OPEN | Main content / all overflowing screens | Wheel input updates `contentScroll_`, but painting and hit testing do not consume it; long screens cannot be relied on to scroll. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| UI-004 | OPEN | Task headers and panels | Text fitting can truncate content and panel scissoring can hide it; verify information is preserved when fixing geometry. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| PROC-001 | OPEN | Repository claims / tests | Continue auditing inherited comments and test names for claims that overstate implementation or parity coverage. | [2026-11-05 claim audit](damagecontrol_report.md#agent-claim-audit-2026-11-05) |
| PAR-001 | OPEN | Ship/home, Engine Room, Mess Hall, Navigation | Visual/content parity is not verified against fresh, matched runtime captures from the current port build. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| PAR-002 | OPEN | Gameplay systems and save/reload | Overall behavior parity is unverified; the native core is a partial reimplementation of unknown fidelity. The differential harness compares only port-authored scripts and a port-defined projection (presentation no-oped). "Core is healthy" was withdrawn. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05), [correction](damagecontrol_report.md#correction-2026-11-05-core-health) |
| UI-005 | OPEN | Raylib text path (titles, fitting, wrapping) | `Text()` paints bold at ≥TypePx(16) but `TextWidth()`/`FitText`/`WrapText` measure with the body font, so width limits and truncation are computed against the wrong font. | [2026-11-05 verification pass](damagecontrol_report.md#investigation-2026-11-05-verification) |
| UI-006 | OPEN | Ship/home diagram (compact Wren) | Compact mode paints no room names and no room glyphs; the HTA keeps names (10 px) and glyphs in compact mode and hides only tags/sys codes. Default home screen shows unnamed empty rooms. | [2026-11-05 verification pass](damagecontrol_report.md#investigation-2026-11-05-verification) |
| PAR-003 | OPEN | Themes / settings | Phase 9 replaced the HTA's parchment + four dark themes with five invented light palettes and labels; `Theme.h`'s header comment misdescribes the reference. Needs restore-to-HTA or explicit user approval. | [2026-11-05 verification pass](damagecontrol_report.md#investigation-2026-11-05-verification) |

## Status key

- **OPEN** — evidence indicates a defect or parity gap remains; fix or verification is still needed.
- **IN PROGRESS** — someone is actively reproducing or changing it; add the owner/commit in a new report entry.
- **BLOCKED** — cannot proceed until a named dependency or environment is available; state what is missing.
- **VERIFIED FIX** — reproduced before, fixed, and checked against the same reference/state afterward. Record exact commit, environment, and comparison evidence in the report before using this status.

IDs are stable; do not reuse them. When an issue is closed, retain its row and change its status to `VERIFIED FIX`, linking the verification entry. Add newly confirmed issues as new rows.

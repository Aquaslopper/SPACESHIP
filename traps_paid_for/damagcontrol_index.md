# Damagcontrol index — open defects

Short entry point for current issues. Detailed evidence, history, and handoff notes live in [`damagecontrol_report.md`](damagecontrol_report.md). Keep this index concise; update statuses here and append investigation details to the report rather than rewriting its history.

| ID | Status | Affected screen / area | Defect summary | Investigation log |
|---|---|---|---|---|
| UI-001 | IN PROGRESS | Ship / home dashboard | Stat labels and values use 18 px minimum text with fixed vertical offsets that can overlap. Containers now grow to fit the text at each typography step; the dock strip follows the nav strip's real bottom; main-content scrolling (`contentScroll_`) is wired into home paint and hit-testing so the column stays reachable when it overflows. Source/build/test verified; Windows side-by-side not yet run. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05), [2026-11-05 UI-001 repair](damagecontrol_report.md#ui-001-repair-2026-11-05) |
| UI-002 | OPEN | Shared chrome and content | ImGui chrome and Raylib content use different effective font metrics; layout and hitboxes may not match rendered text. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| UI-007 | IN PROGRESS | Ship / home dashboard | Correction: base painted units only if they fit inline, silently omitting overflow. Reserved fallback row now paints overflow units; graphical verification outstanding. | [2026-11-05 UI-001 repair](damagecontrol_report.md#ui-001-repair-2026-11-05) |
| UI-003 | IN PROGRESS | Main content / all overflowing screens | Content transform existed in base, but range was Home-only and scissors did not follow scroll. Text-bottom range, tab resets and adjusted scissors patched; runtime scrolling/clipping unverified. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| UI-004 | OPEN | Task headers and panels | Text fitting can truncate content and panel scissoring can hide it; verify information is preserved when fixing geometry. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| PROC-001 | OPEN | Repository claims / tests | Continue auditing inherited comments and test names for claims that overstate implementation or parity coverage. | [2026-11-05 claim audit](damagecontrol_report.md#agent-claim-audit-2026-11-05) |
| PAR-001 | OPEN | Ship/home, Engine Room, Mess Hall, Navigation | Visual/content parity is not verified against fresh, matched runtime captures from the current port build. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05) |
| PAR-002 | OPEN | Gameplay systems and save/reload | Overall behavior parity is unverified; the native core is a partial reimplementation of unknown fidelity. The differential harness compares only port-authored scripts and a port-defined projection (presentation no-oped). "Core is healthy" was withdrawn. | [2026-11-05 baseline](damagecontrol_report.md#baseline-2026-11-05), [correction](damagecontrol_report.md#correction-2026-11-05-core-health) |
| UI-005 | OPEN | Raylib text path (titles, fitting, wrapping) | `Text()` paints bold at ≥TypePx(16) but `TextWidth()`/`FitText`/`WrapText` measure with the body font, so width limits and truncation are computed against the wrong font. | [2026-11-05 verification pass](damagecontrol_report.md#investigation-2026-11-05-verification) |
| UI-006 | OPEN | Ship/home diagram (compact Wren) | Correction: base already paints compact names, but into fixed HTA-sized geometry at 18px; overflow/overlap and missing glyphs remain. Ship geometry and hitboxes still require restoration and runtime comparison. | [2026-11-05 verification pass](damagecontrol_report.md#investigation-2026-11-05-verification) |
| PAR-003 | OPEN | Themes / settings | Theme names/dark palettes existed in base; stale invented-theme tests now corrected against HTA literals. Classic chart ink repaired. Full CSS role/property mapping and graphical verification remain open. | [2026-11-05 verification pass](damagecontrol_report.md#investigation-2026-11-05-verification) |


| UI-008 | IN PROGRESS | Navigation / cooldown dispatch | Removed extra start/acknowledgment lock; displayed command keys and enabled conditions now drive dispatch. Regression-covered only; native UI interactions unverified. | [2026-11-05 input/layout investigation](damagecontrol_report.md#bughunt-2026-11-05-input-layout) |
| UI-009 | IN PROGRESS | Navigation / map input | Fixed accumulated-pan-as-delta acceleration; >4px drag suppresses selection; viewport coordinates and ImGui input ownership handled. Real pointer testing blocked by missing graphical runtime. | [2026-11-05 input/layout investigation](damagecontrol_report.md#bughunt-2026-11-05-input-layout) |
| UI-010 | IN PROGRESS | Floating windows / options / briefings | Independent nonmodal ImGui windows, keyed replacement, scrollable full bodies and source briefing content patched. Collection/model/syntax tests pass; real dragging, stacking, confirmations and reachability remain unverified. | [2026-11-05 input/layout investigation](damagecontrol_report.md#bughunt-2026-11-05-input-layout) |


| UI-011 | IN PROGRESS | Floating-window close input | User reproduced click-through on close. Press/release ownership latch patched; actual ImGui CPU close-widget probe passes. Updated native client retest required. | [user feedback repair](damagecontrol_report.md#bughunt-2026-11-05-user-feedback) |
| UI-012 | IN PROGRESS | Sidebar / Vessel / footer | Restored ship name/class/registry, measured/clipped sidebar text and pinned-only footer rendering. Source/model tests pass; visual retest required. | [user feedback repair](damagecontrol_report.md#bughunt-2026-11-05-user-feedback) |
| UI-013 | IN PROGRESS | Main menu / sheets | Visible five-command bottom strip, literal title colours, centred scrollable sheets and shared settings controls replace invented column/invisible menu. Rendering unverified here. | [user feedback repair](damagecontrol_report.md#bughunt-2026-11-05-user-feedback) |
| UI-014 | IN PROGRESS | Crew Quarters | Removed duplicate roster/payroll overlap; restored job selector, Talk, order notes, traits and content-following assignments; removed duplicate commit lock. Narrow viewport/visual verification outstanding. | [user feedback repair](damagecontrol_report.md#bughunt-2026-11-05-user-feedback) |

## Status key

- **OPEN** — evidence indicates a defect or parity gap remains; fix or verification is still needed.
- **IN PROGRESS** — someone is actively reproducing or changing it; add the owner/commit in a new report entry.
- **BLOCKED** — cannot proceed until a named dependency or environment is available; state what is missing.
- **VERIFIED FIX** — reproduced before, fixed, and checked against the same reference/state afterward. Record exact commit, environment, and comparison evidence in the report before using this status.

IDs are stable; do not reuse them. When an issue is closed, retain its row and change its status to `VERIFIED FIX`, linking the verification entry. Add newly confirmed issues as new rows.

Latest source-audit corrections and partial patches: [2026-11-05 input/layout investigation](damagecontrol_report.md#bughunt-2026-11-05-input-layout). No visual parity sign-off.

Continuation: [independent floating windows, settings/audio and source-theme test corrections](damagecontrol_report.md#bughunt-2026-11-05-floating-windows). 206/206 tests pass; native graphical runtime unavailable, no visual/interaction sign-off.

Latest: [user-observed regression repairs](damagecontrol_report.md#bughunt-2026-11-05-user-feedback). 211/211 tests pass (including CPU ImGui input probe). User confirmed dragging/Navigation improvements on preceding build; new graphical changes need user retest.

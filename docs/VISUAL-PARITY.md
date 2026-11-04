# Visual reconstruction — work in progress

The original `Spaceship.hta` is the specification for both presentation and
interaction. The previous dark dashboard was a replacement UI, **not a faithful
conversion**, and is not an acceptable visual completion target.

This document is an acceptance checklist, not a declaration of completion.

## Current reconstruction

- The original title JPEG is extracted byte-for-byte from the HTA and embedded in
  the EXE. It is not a new/generated replacement or a browser-hosted screen.
- A title menu now starts the native app: new voyage, three-slot loading, audio
  options, first-watch guidance and resume/exit. Options are still only the native
  audio subset, not the full reference settings.
- Light parchment/white surfaces replace the dark dashboard. Four ship gauges are
  across the top; deck and activity feed occupy the main home surface.
- Both authored hull geometries/corridors, 21 room positions/tones/scene accents,
  simplified glyphs and room crew markers are drawn with native GDI.
- Left-clicking a room inspects it without leaving the deck; right-click opens a
  native task tab. Open tabs, tab scrolling and Home are free.
- Generic room instrument readouts now use the authored scene metadata rather than
  accidentally showing a navigation chart in unrelated rooms.

- Engine, O2, mess and quarters now render extracted original box-of-div geometry
  and CSS through native GDI, rather than the generic vignette. Crew pixels, job
  props and medical/study poses come from the original sprite painter. Extraction
  is development-only; the executable uses embedded data and C++ bindings.
- Condition, O2 slots/air and meal meters bind to the native save. O2 install hit
  targets occupy their authored positions and use the existing gameplay handler.
  Complete page controls/readouts and visual comparison remain outstanding.

**These are reconstruction steps, not pixel or functional UI parity.**

## Still required before visual acceptance

- [ ] Reproduce the original expandable Vessel / Resources / Crew / Contracts /
      Events / Indicators sidebar, including meaningful work and cooldown labels.
      The native sidebar still uses the previous page buttons/readouts.
- [ ] Reproduce home navigation strip, location, contacts and life-support cards.
- [ ] Match deck sizing, borders, glyph shapes, corridor crew placement and labels
      at equivalent window sizes and DPI. Current GDI glyphs are simplified.
- [ ] Translate the bespoke engine, filters, quarters, mess, cargo, science and
      Simulacron machinery/layouts—not just display a generic scope vignette.
- [ ] Match each remaining facility's illustration, readouts and command grouping.
- [ ] Reproduce navigation's system/sector/galaxy views and original selection flow.
- [ ] Reproduce task-tab close/overflow behavior and original shortcut menus.
- [ ] Port the complete settings panels, accessibility options and preferences.
- [ ] Verify new-voyage preference retention and all load/return-to-menu paths
      against the reference; current native controls still have compatibility gaps.
- [ ] Compare actual native screenshots with matching reference screenshots, then
      manually verify mouse/keyboard interactions and a complete Windows voyage.

All 21 facility IDs remain represented: `engine`, `o2`, `quarters`, `mess`, `r1`,
`cargo`, `science`, `simulacron`, `r2`, `r3`, `r4`, `r5`, `r6`, `r7`, `r8`, `r9`,
`r10`, `r11`, `r12`, `bridge`, `r13`. Presence of an ID or a scope illustration is
**not** evidence that its authored room has been faithfully ported.

## Evidence, not assumptions

`spaceship.exe --capture-ui` runs on Windows and saves native title/deck/room PNGs
under `visual-captures/`. The workflow publishes these as `native-visual-captures`
separately from the player build. Captures use native window painting; they are
not mockups or renders of the HTA. Their generation is not a visual approval.

Smoke checks cover title entry/resume, action-free navigation through all room
pages and blocked watch spam. Engine tests and source-data checks do not prove
that the native UI looks or behaves like the reference. A manual Windows visual
comparison is still outstanding; no screenshot approval has been claimed.

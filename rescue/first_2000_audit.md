# Spaceship.hta lines 1–2000 — bounded comparison

**Status: verified repairs, not a complete line-by-line parity sign-off.**
No source after line 2000 was used as authority for these new repairs. The HTA itself is unchanged. Earlier dirty health/cooldown work was preserved, not reset or committed. No PR, CI, push or merge operations were performed.

## Source and counterpart comparison

| HTA area within 1–2000 | Native counterpart | Result of this pass |
|---|---|---|
| Desktop declaration, reset and shell anchoring | `cpp/app/main.cpp`, `cpp/ui/Layout.*`, `Draw.cpp` | Read; desktop-host equivalence and all shell geometry are **not signed off**. Keep approved 18 px floor, not the HTA's tiny text. |
| Classic and four dark CSS palettes | `Theme.cpp`, `Theme.h`, `Palette.h`, actual `Draw.cpp` consumers | Removed invented blended/darkened colours. Added distinct content/chrome/control/input/tab/chart/title roles. Checked 100 dark-role values against compiled native output. Semantic green/amber/red retain source colours rather than becoming theme accents. |
| Sidebar caret and indicator styles | `Draw.cpp::PaintChrome` | Font `v`/`>` caret replaced by drawn triangles. Indicator ink/ghost widths corrected from 2 to 3 px. Full footer, sidebar block and tab geometry still require comparison. |
| Stat colour declarations | `IdleScreen` rail, `Draw.cpp::PaintIdle` | Painter now uses the actual cell's authored colour, not brown for every gauge. Other rail/feed geometry is not signed off. |
| Action-mark CSS | `Fx` mark coordinates and `Draw.cpp` | Corrected brown one-pixel lines to source green 11×2 and 2×11 bars, inside the existing 11×11 top-left box. Glow is **not restored**. No timing or action logic changed. |
| Engine/filter/galley/hallway CSS | `First2000Styles.h`, `Draw.cpp::PaintScenePart`, `PaintStage`, quarters scenery | 112 source-order rules retain literal fills, border colours/sides/widths, dashed versus solid borders, radii, ink, alignment and reference type sizes subject to the native floor. Renderer no longer guesses style from words such as `pipe`, `ring`, or `fill` for covered classes, nor paints opaque boxes behind covered text-only parts. Transparent dashed reactor rings stay transparent. Housing backgrounds/frames restored; engine's housing is not incorrectly dark-themed. Full enlarged-text composition/overlap and GPU appearance remain unverified. |
| Map CSS | `Draw.cpp::PaintNavScreen` | Corrected chart background and scale ink roles. Marker silhouettes, shadows, lanes, all interaction states and full chart layout remain **unresolved**. |
| Room/glyph/hero, popup, reference-screen and award CSS | `ShipDiagram`, `Draw.cpp`, screen models | Source read; **not fully transported or verified**. In particular, existing native deck-plan rendering is not proof of the declared room-glyph and room-hero drawings. Do not describe this pass as full visual parity. |
| Title image and menu CSS | `assets/art/title.jpg`, `Draw.cpp::PaintTitle`, palette | Embedded JPEG matches byte for byte. Renderer now applies all four source dark title-menu backgrounds, hover colours, borders and inks instead of hard-coded classic colours. Native readable menu sizing remains intentionally larger. |
| Complete ship/crew/job/order/pay/resource/survival declarations | `assets/data/{ship,crew,survival}.json`, `Catalog.cpp` | 25 declarations checked field-by-field. No demonstrated data repair necessary. Extra catalogue fields are not deleted: the bounded source is not authority for declarations/extensions outside this range. This is data evidence, **not** an audit of later payroll/medical/air gameplay. |
| Partial MAP declaration ending at line 2000 | `assets/data/chart.json` | All 12 fields present before cutoff match. The remainder of MAP was not read or reconstructed. |

## Executed checks

- `node tools/check-first-2000.cjs`: **PASS**, 25 declarations + 12 MAP fields, byte-identical title JPEG, 100 compiled palette roles, 112 compiled scene rules. It slices the source at line 2000 before parsing and deliberately does not evaluate the unfinished MAP declaration.
- Negative control: temporarily changed native void title-menu background to black; the bounded gate failed specifically at `void.titleMenu`. Restored the source and reran successfully.
- Headless CMake configure/rebuild: **PASS** (`SPACESHIP_BUILD_GUI=OFF`, broad catalogue gate disabled for this bounded task).
- `build/spaceship_tests '[ui][theme]'`: **106 assertions, 4 cases passed**.
- GUI translation-unit syntax check with pinned raylib 6.0, ImGui 1.92.7 and rlImGui Raylib_6_0: **PASS**. It also reports an existing unused palette variable in `PaintCrewRecord`; no claim of warning-free compilation.
- `bash tools/check-draw.sh`: **PASS**. Actual `Draw.cpp` CPU recorder asserts the source pipe fill, green two-bar action mark, four dark menu backgrounds and minimum 18 px submitted text, alongside its existing regression checks. This is not an OS/GPU screenshot.
- `git diff --check`: **PASS**.

No full desktop GUI executable, native screenshot or GPU/runtime stability validation was produced here. The unresolved items above remain inside the requested first-2000-line scope; nothing beyond that scope is being claimed completed.

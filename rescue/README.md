# Traps paid for: port parity evidence

> **Historical rescue document:** This records an earlier investigation and may contain superseded instructions or statuses. The owner has since approved visual redesign and specific gameplay changes. See [current rescue lessons](RESCUE_NOTES.md) and follow the owner's current instructions.

This folder preserves the screenshots supplied in the report and records concrete port mismatches visible in them. For the strict 1:1 mission and a warning that inherited agent work may contain hallucinations or unsupported claims, see [`REPAIR_AND_RESCUE.md`](REPAIR_AND_RESCUE.md); current open issues are indexed in [`damagcontrol_index.md`](damagcontrol_index.md).

## Image index

| File | Label / identification |
|---|---|
| `images/00_current_port_composite.png` | User-supplied current-state composite: ship/home, Engine Room, Mess Hall, Navigation, left to right. |
| `images/01_current_port_ship.png` | Crop 1: current-state ship/home screen. |
| `images/02_current_port_engine_room.png` | Crop 2: current-state Engine Room. |
| `images/03_current_port_mess_hall.png` | Crop 3: current-state Mess Hall. |
| `images/04_current_port_navigation.png` | Crop 4: current-state Navigation. |
| `images/05_port_ship_older_capture.png` | Uploaded `b1.png`, classified by its visible content as a native-port ship/home capture. |
| `images/06_reference_ship_capture.png` | Uploaded `b2.png`, classified by visible content as the original-game/reference ship/home capture. |
| `images/07_port_navigation_older_capture.png` | Uploaded `s1.png`, classified by visible content as a native-port Navigation capture. |
| `images/08_reference_navigation_capture.png` | Uploaded `s2.png`, classified by visible content as the original-game/reference Navigation capture. |
| `images/09_empty_startup_window.png` | Uploaded dated screenshot showing the empty `Spaceship Command` window. |
AND GROWING

`b1`/`b2` labels = BROKEN VERSION
`s1`/`s2` labels = ORIGINAL HTA VERSION

## Main findings

1. **Text size and geometry are not coordinated.** `cpp/ui/Draw.cpp:86-97` enforces a minimum 18 px for the Raylib text path, but many callers still position text using short, legacy offsets. For example, `PaintIdle` at `cpp/ui/Draw.cpp:545-552` starts an 18 px label at `y+4` and a larger value at `y+20`; the start-to-start gap is smaller than the label height. `cpp/ui/Draw.cpp:574-587` has the same issue in the navigation strip and info rail. These are direct code-level causes of overlap like the top-rail and strip collisions visible in the port captures.

2. **The UI uses two text systems with different effective sizes.** Most content uses `TypePx()` and Raylib `DrawTextEx`, while the chrome/sidebar path uses ImGui `AddText`/`CalcTextSize` with its default font (`cpp/ui/Draw.cpp:288 onward`). Thus an 18 px rule is not consistently applied across tabs, sidebar, footer and screen content. The mixed measurement/rendering paths also make width estimates unreliable when one path's measurements are used to lay out the other.

3. **Some information is discarded to make it fit.** `FitText` (`cpp/ui/Draw.cpp:105-114`) removes characters until an ellipsis fits. `PaintTaskHeader` uses it for titles (`cpp/ui/Draw.cpp:116-139`). This prevents visible overflow by losing text; it is not a content-preserving layout solution. Panel body text can also be clipped at the panel scissor boundary (`cpp/ui/Draw.cpp:607-640`).

4. **The home/ship dashboard is not protected by the shared task-header layout.** `PaintIdle` is a separate renderer (`cpp/ui/Draw.cpp:541-605`) and retains its own fixed coordinates for the stat rail, ship diagram, strips, information rail and feed. A fix to task/navigation/record headers does not fix the home screen's text collisions or the screen's content hierarchy.

5. **The port's scrolling contract is incomplete.** The HTA content element is explicitly scrollable (`Spaceship.hta:129-132`). Native code has `contentScroll_` and updates it on wheel input (`cpp/ui/Draw.h:182`, `cpp/ui/Draw.cpp:2124`), but that value is not applied to the painted content or its hitboxes. A long native screen therefore cannot be assumed to scroll like the source UI.

6. **Panel and information parity is partial, not established.** Native task panels are custom-painted (`cpp/ui/Draw.cpp:607-730`), while the HTA uses DOM panels and screen-specific builders. The screenshots show meaningful differences in what is prominent and how information is grouped (especially the home, Engine Room and Mess Hall views). Existing build success does not establish that the fields, ordering, panel heights, interaction areas or state transitions match the HTA.

7. **Navigation geometry was inferred from CSS, not validated at runtime.** The HTA defines its zoom pair and layer stack in `Spaceship.hta:885-905` and constructs them in `5228-5250`. PR #12 commit `63cec81` describes the change as responsive, but its native chart still relies on fixed side-column widths (214/252 px) and an 1100 px chart cap (`cpp/ui/Draw.cpp`, `PaintNavScreen`). Those values are an implementation choice, not evidence of visual or responsive parity. The later native layer-stack adjustment is closer to the HTA arrangement, but still needs a live comparison at the target window sizes.

## Where the agent's code-level hallucination / unsupported assumption occurred

No literal gameplay-state fabrication was identified in the specific PR #11/PR #12 source paths inspected for this audit. That is a limited finding, not a guarantee that the repository contains no inherited hallucinations. The concrete agent failure evidenced here is treating plausible-looking layout translations and a successful build as proof of parity. Future agents must assume inherited code, comments, and test claims can be wrong until traced against the HTA; see `REPAIR_AND_RESCUE.md`.

- **`63cec81` — “Match responsive navigation geometry and panel content bounds.”** The commit introduces measured panel heights and fixed navigation columns/chart sizing. “Responsive” and “match” were stronger than the evidence: no HTA runtime comparison was performed, and the supplied native/reference captures visibly differ. The relevant code is the `PaintNavScreen` sizing block in `cpp/ui/Draw.cpp` and `MeasurePanelHeight` immediately before `PaintStage`.
- **`db1b423` — shared task header and text/panel geometry edits.** `PaintTaskHeader` and `FitText` (`cpp/ui/Draw.cpp:105-139`) were an unverified layout strategy. `FitText` truncates instead of preserving a title, and the helper is not used by the separate home renderer. The screenshot evidence was not captured by running that build, so the change could not be called visually verified.
- **Prior completion language conflated different checks.** Windows CI verified that CMake configured and the executable built; Node tests verified four existing model tests. Neither check exercised the native GUI or compared it with the HTA. Calling those checks proof that the UI was fixed would be an unsupported conclusion.

These are identified as unsupported agent assumptions, not assertions that the agents intentionally invented simulation data. The source history and the untouched baseline should be consulted before attributing older code such as `TypePx()` to a particular agent change.

## Still not working / still unverified

- The screenshots still show text overlap, tight or misleading information hierarchy, and panels whose visible content/layout does not match the reference. The fixes in the branch have not been validated against a fresh screenshot from the built executable.
- The home stat/info rails and their fixed vertical offsets remain independent of the new header helper; their 18 px text and short spacings are a concrete unresolved collision risk.
- Main-content scrolling is not wired through painting and hit testing, despite the wheel variable being updated.
- Text truncation and panel clipping can hide information. There is no complete content/field audit proving that every source value remains readable.
- The port's visual parity for Engine Room, Mess Hall, task/service panels, and Navigation has not been established. The service-resource behavior was compared against the corresponding HTA segment earlier; that narrow check is not proof of overall gameplay parity.
- No end-to-end gameplay parity pass was completed for navigation, crew, contracts, events, resources, or save/load.

## How the mismatch was identified

1. Read the supplied captures as evidence, separating the current composite and visually classified port/reference pairs; retained every upload and split the composite into four labeled views.
2. Compared the visible failures (overlapping values, cramped/hidden information, differing panel composition and chart controls) against the actual drawing and layout code, not against font-size guesses.
3. Traced each relevant native renderer to its HTA counterpart: `PaintIdle` and screen-specific paint methods versus the HTA layout/builders; Navigation controls versus `buildMap()` and the map CSS.
4. Inspected Git history for the agent-authored PR #12 layout changes and distinguished build/test success from visual verification.

## Problems and limits encountered

- This environment does not provide `mshta`, Wine, a browser GUI, or Playwright/Chromium, so I could not launch the HTA or take a fresh native executable screenshot here. The user-provided captures are the only visual runtime evidence available in this pass.
- The Windows executable was built by GitHub Actions, not run interactively in this environment.
- Existing automated tests are narrow (four Node tests plus the Windows build); they do not test pixel layout, typography, scrolling, panel completeness, or gameplay parity.
- The `b*`/`s*` reference labels had no accompanying metadata; they were classified from the visible screens and are explicitly marked as such above.

This is an audit/evidence package, **not a claim that the listed defects have all been fixed**.

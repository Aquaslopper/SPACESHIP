# Running a repeatable HTA ↔ native comparison

Use `compare-hta-port.ps1` on a **Windows desktop session** to launch the original `Spaceship.hta` and a built native port, capture matched screen states, and record observations. The script is manual-by-design: it does not inject input, infer game state, or declare parity from a screenshot.

## Requirements

- Windows with `mshta.exe` and an interactive desktop (not a headless CI runner).
- PowerShell 5.1 or later.
- A built native executable. Build/package it using the repository's CMake workflow first.
- Both applications should use the same save/starting state, window size, Windows display scaling, and font environment.

## Run

From the repository root in PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\compare-hta-port.ps1 `
  -NativeExe .\build\spaceship.exe `
  -OutDir .\hta-port-comparison
```

The HTA path defaults to `Spaceship.hta` in this repository. Override it with `-HtaPath` if needed. Default checkpoints are `ship-home`, `engine-room`, `mess-hall`, and `navigation`; supply your own list with `-Scenarios`, for example:

```powershell
.\tools\compare-hta-port.ps1 -NativeExe .\build\spaceship.exe `
  -Scenarios @('ship-home', 'service-system', 'crew-record', 'navigation', 'save-reload')
```

## Procedure

1. The script launches both applications and leaves them open.
2. Arrange both windows side by side at matching sizes. Use the same voyage/save, or reproduce the same initial state in both.
3. At each checkpoint, navigate both applications to the same screen and perform the same action sequence. Close popups/tooltips and align the windows before capture.
4. Press Enter in the script console. It briefly minimizes the console and saves a full virtual-desktop PNG, then restores the console.
5. Record any visual differences, missing values, wrong state transitions, or actions that did not work. The generated `comparison.md` links the note to that checkpoint image.
6. Inspect every captured PNG and record a parity verdict separately. The capture itself is evidence, not an automatic pass.

Output defaults to `hta-port-comparison/` at the repository root; that generated directory is git-ignored. The script does not close either application, so save state and close both manually when finished.

## What the harness does not do

- It does not automatically align windows, reset/save games, or execute identical mouse/keyboard actions.
- It does not crop each app window, compare pixels, or ignore OS chrome/anti-aliasing differences.
- It does not validate gameplay state or prove that all HTML/HTA behavior exists in the native port.

Keep the original reference and port images at matching resolution. For behavior claims, include the action sequence and resulting state in the observation notes; screenshots alone cannot prove simulation parity.

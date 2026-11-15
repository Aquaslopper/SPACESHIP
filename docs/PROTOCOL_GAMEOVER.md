# Protocol GameOver — manual tool

Start only when the owner requests it, for example in chat:

> Starte Protocol GameOver.

The coding agent runs this command from the repository root:

```bash
bash tools/check-gameover.sh
```

Requires CMake 3.20+, a C++17 compiler, and dependency download access on first run.
The tool builds a headless check in the ignored `build-gameover/` directory.
It does not build or launch the graphical game, change gameplay source, or edit saves.

Report the exit status and PASS/FAIL output. Do not repair failures unless requested.
A build failure is not a passed game-over check.

The checks cover the existing hull, fuel and air death conditions, selected nonfatal
prerequisites, filter exhaustion, and insufficient-range departure/fuel depletion.
These are targeted technical checks, not an automatic playthrough or proof that
all possible gameplay paths remain reachable.

No GitHub Actions workflow, automatic push/PR check, EXE-build hook, or required
merge check is attached to this tool.

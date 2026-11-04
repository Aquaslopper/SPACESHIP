SPACESHIP COMMAND - NATIVE C++ MIGRATION BUILD

Extract the ZIP and run spaceship.exe. It is a native Windows application:
no HTA host, browser, JavaScript runtime, Node, or Visual C++ redistributable
installation is required. Game data is compiled into the executable.

This is a playable migration build, NOT yet a feature-identical port.
Read NATIVE-PORT.md for implemented systems and remaining differences.

Controls
- Select a page at left, a target in its list, then an available command.
- Click a compartment in the ship plan to open it.
- Navigation lists local destinations first, then all star systems.
- Reading and selecting are free. A committed command advances one action.
- Stand watch is repeatable and advances ongoing work and other cooldowns.
- Disabled command reasons are listed in the detail pane.
- First watch: O2 Filter Room -> install each slot -> stand watch until seated
  -> start bank. Navigation -> Tolme Anchorage -> start crossing.

Saves
Use the slot selector and Save/Load buttons. Files are stored in:
%LOCALAPPDATA%\SpaceshipCommand\native-slot-1.json (or 2 / 3).
Previous slot contents are retained as .bak when a replacement is saved.
These are native-only saves. HTA save import is not implemented; your old
Spaceship_save*.txt files are not touched. Keep them for the reference edition.

Windows CI checks the native UI with --smoke-test. Actual visual appearance
and playthrough behavior still require manual Windows testing.

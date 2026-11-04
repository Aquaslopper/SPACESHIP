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
- Reading/selecting, staging orders and acknowledging completed readouts are free.
- A committed command advances one action. Idle time never advances gameplay.
- Crew: stage each person's orders, then issue the whole sheet as one action.
- Scroll the command panel when more actions are available than fit on screen.
- Stand watch is repeatable and advances ongoing work and other cooldowns.
- Disabled command reasons are listed in the detail pane.
- First watch: O2 Filter Room -> install each slot -> stand watch until seated
  -> start bank. Navigation -> Tolme Anchorage -> start crossing -> stand watch
  until complete -> acknowledge arrival (free) -> dock.

Saves
Use the slot selector and Save/Load buttons. Files are stored in:
%LOCALAPPDATA%\SpaceshipCommand\native-slot-1.json (or 2 / 3).
Previous slot contents are retained as .bak when a replacement is saved.
Home/Guide -> Import HTA save opens a .txt/.json file picker. Supported HTA UTF-8
and UTF-16LE voyages are translated without writing the source file. Save the
imported voyage into a native slot. Unknown tasks/unmapped active button cooldowns
are refused explicitly; let unsupported cooldowns expire in the reference game
before retrying. Keep your original files. Import is not universally compatible.

Windows CI checks the native UI with --smoke-test. Actual visual appearance
and playthrough behavior still require manual Windows testing.

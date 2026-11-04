SPACESHIP COMMAND - NATIVE C++ MIGRATION BUILD

Extract the ZIP and run spaceship.exe. It is a native Windows application:
no HTA host, browser, JavaScript runtime, Node, or Visual C++ redistributable
installation is required. Game data is compiled into the executable.

This is a playable migration build, NOT yet a feature-identical port.
Read NATIVE-PORT.md for implemented systems and remaining differences.

Controls
- Select a page at left, a target in its list, then an available command.
- Left-click a compartment to inspect it; right-click opens its task tab.
- The native app starts on the original title artwork. Menu returns there; Resume
  preserves the current voyage. Visual reconstruction is unfinished: see
  docs/VISUAL-PARITY.md for the room/sidebar/layout acceptance checklist.
- Navigation lists local destinations first, then all star systems.
- Reading/selecting, staging orders and acknowledging completed readouts are free.
- A committed command advances one action. Idle time never advances gameplay.
- Crew: stage each person's orders, then issue the whole sheet as one action.
- Scroll the command panel when more actions are available than fit on screen.
- Stand watch costs one action and has a 15-action cooldown. Perform other gameplay
  actions to advance its countdown; waiting or blocked repeats cannot advance it.
- The left sidebar separates ONGOING WORK from labelled CONTROL COOLDOWNS.
  Buttons show their remaining cooldown in other committed action clicks.
- Disabled command reasons are listed in the detail pane.
- First actions: O2 Filter Room -> seat available slots, issue crew work or cook,
  navigate to start a crossing, and alternate useful commands as their work advances.
  A completed crossing/scan must be acknowledged (free). No idle-time progress.

Folders and audio
Keep this structure after extracting the ZIP:
  spaceship.exe
  music/ambient.wav
  sounds/click.wav, scan.wav, travel.wav, complete.wav
  saves/README.txt (native slot files appear here when you save)
Music/effects toggles and volume controls are on Home and Handbook; changing them
is free. Music starts after interaction. The supplied WAVs are synthesized from
the reference game. You may replace them using the same filenames with 16-bit PCM
WAVs (mono/stereo, 8-192 kHz, max 32 MiB). No download or browser is involved.
Missing/unsupported files or audio hardware are silent and do not block the game.

Saves
Save/Load use three slots in the saves folder next to the EXE. If it is not
writable, the active folder is %LOCALAPPDATA%\SpaceshipCommand\saves instead.
Home/Handbook -> Open saves folder opens the actual active directory.
Older native slots directly in %LOCALAPPDATA%\SpaceshipCommand are still checked
when the new selected slot is absent. Old files are never moved/deleted.
Previous slot contents are retained as .bak when a replacement is saved.
Home/Guide -> Import HTA save opens a .txt/.json file picker. Supported HTA UTF-8
and UTF-16LE voyages are translated without writing the source file. Save the
imported voyage into a native slot. Unknown tasks/unmapped active button cooldowns
are refused explicitly; let unsupported cooldowns expire in the reference game
before retrying. Keep your original files. Import is not universally compatible.

Windows CI checks the native UI with --smoke-test. Actual visual appearance
and playthrough behavior still require manual Windows testing.

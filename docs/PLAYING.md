# Playing Spaceship

Extract the entire Windows package to a writable folder before running
`spaceship.exe`. Keep its `assets` folder beside the executable.

## Saving

On a normal exit, the game writes a new dated snapshot in `runtime/saves/`.
It does not overwrite the three manual save slots. At launch it loads the most
recent valid dated snapshot or manual slot; if a newer file is damaged it tries
older saves and warns you. A forced termination, crash, or power loss can still
prevent an exit autosave. Keep backups of `runtime/saves/`.

When updating to a new copy of the game, **copy your existing `runtime/saves/`
folder into the new installation** before launching. Don't delete the original
until you have confirmed your voyage loaded. The game will not automatically
find saves from a different installation folder.

To recover an older dated snapshot, first back up the entire saves folder. With
the game closed, copy the snapshot to an unused manual-slot filename, such as
`Spaceship_save_3.txt` if slot 3 is empty. Start the game and load that slot
from the save/load menu if it was not automatically selected. Do not overwrite
an existing slot unless you have backed it up.

## Your ship

A new Wren begins with an engine room, O2 filter, crew quarters, cargo hold,
mess hall and bridge; Navigation is available from the bridge and taskbar. Its
hold carries 20 m³. At a space station, open the shipyard to install additional
rooms for 500 shiplets each or buy other upgrades. Rooms you already own travel
with you when buying the larger ship. Existing saves keep their facilities and
cargo capacity.

## Your own music

Put playable `.ogg` (Vorbis), `.mp3`, or `.wav` files in `assets/audio/music/`
beside the executable, then restart the game. The Settings panel shows the
selected track or the reason for falling back to the built-in soundtrack.
Copy your custom files to a new installation when updating.

Native save slots are written here, next to the EXE:
native-slot-1.json, native-slot-2.json, native-slot-3.json
The previous version of each slot is retained as .bak.
If this folder is not writable (e.g. Program Files), saves fall back to:
%LOCALAPPDATA%\SpaceshipCommand\saves
When a new slot does not exist, Load also checks the earlier location:
%LOCALAPPDATA%\SpaceshipCommand\native-slot-{1,2,3}.json
Old saves are not moved or deleted. Save writes to the new active folder.
Home/Handbook -> Open saves folder shows the active writable directory.

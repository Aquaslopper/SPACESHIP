SPACESHIP COMMAND - full visual Windows edition

1. Extract ALL files from spaceship-windows-x64.zip into a writable folder
   (for example a folder under Documents, not Program Files).
2. Double-click spaceship.exe. Keep Spaceship.hta beside it.

This EXE is a launcher for the existing full visual HTA game, NOT a native
C++ rewrite. It uses Windows' built-in HTML Application host (mshta.exe).
It needs no Visual C++ runtime download. It works offline.

The experimental C++ console port is not included in this player package.

Saves remain Spaceship_save.txt, Spaceship_save_2.txt and
Spaceship_save_3.txt beside Spaceship.hta. To continue an existing voyage,
copy those save files from your old game folder into this folder first.
Keep backups. Replacing the EXE/HTA does not require replacing save files.

If Windows or your organisation blocks HTML Applications, this launcher
cannot bypass that policy. Do not disable antivirus or system protections.
On managed PCs, contact your administrator. Opening Spaceship.hta directly
uses the same Windows host and can help identify an HTA-specific error.

spaceship.exe --check checks that the HTA and Windows host exist without
opening a window; it does not verify rendering or override security policy.

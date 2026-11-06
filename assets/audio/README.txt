SPACESHIP — your own soundtrack
================================

The game ships with a small synthesiser so it always has a voice. Both of its
channels can be replaced with files: the game looks in this folder first and
only falls back to the synthesiser when a file is missing.

  music/   the ambient bed
  sfx/     the four click cues

Nothing in here is required. An empty folder simply means "play the synth".

MUSIC
-----
Put any number of files in music/. Supported formats:

    .ogg   (Vorbis)  <-- preferred: small and streams well
    .mp3
    .wav

Put files in the assets/audio/music folder beside the game executable, then
restart the game. The file must actually be a playable Vorbis OGG, MP3 or WAV;
renaming another format does not convert it. Extensions may be uppercase.

Files are played in filename order; 01-quiet-drift.ogg and 02-long-watch.ogg
are examples, not required names. If a file cannot be decoded it is skipped.
Settings shows the selected track or why the built-in music is being used. Turn
on Ambient soundtrack and adjust the volume there. If you play from a ZIP,
extract the game first.

SFX
---
Four moments make a sound, and each looks for these filenames in sfx/:

    click.wav     a control was pressed (tabs, chart, panels)
    action.wav    an action was committed to the ship
    complete.wav  a task or a voyage leg finished
    alert.wav     something needs the captain: an incident, a fault, a warning
    arrive.wav    the ship reached a port (also used for a finished task)

Aliases are accepted, so an existing library can be dropped in unchanged:

    action.wav may be named    thrust.wav
    alert.wav  may be named    ping.wav
    complete.wav may be named  chime.wav
    click.wav has no alias

All sound effects should be 16-bit PCM .wav files. Mono is enough; the game
plays them at the volume set in Options, and the deafest cue is a short one —
keep them under a second.

WHAT WINS
---------
A file in this folder always beats the synthesiser for that one sound. There is
no configuration file. The folders are scanned once at start-up, so restart
after adding or changing files.

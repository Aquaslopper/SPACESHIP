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

They are read in filename order and played one after another, so name them
01-quiet-drift.ogg, 02-long-watch.ogg, ... to choose the running order. The
music volume and the on/off switch in Options apply to them exactly as they do
to the built-in bed.

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
no configuration file and no restart needed beyond relaunching the game: the
folder is scanned once at start-up.

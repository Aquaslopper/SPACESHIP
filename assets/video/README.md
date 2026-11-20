# Startup intro

`intro.mp4` is the source clip. The game plays `intro.mpg` (video) and
`intro.ogg` (soundtrack) so players do not need FFmpeg installed. To replace
or regenerate the intro, run these commands from this directory:

```sh
ffmpeg -i intro.mp4 -an -vf scale=960:540 -c:v mpeg1video -q:v 5 -bf 0 -f mpeg intro.mpg
ffmpeg -i intro.mp4 -vn -c:a libvorbis -q:a 5 intro.ogg
```

The picture and soundtrack fade out from 0:18 to the end of the clip. The
screen holds black for 50 ms, then the main menu fades in over 1.25 seconds.
Esc skips the intro. If the video is not available, the splash goes straight
to the menu.

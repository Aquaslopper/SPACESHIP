# Spaceship headless viewer

Renders the Spaceship client to PNG on a machine with no display, no GPU and
no sound device. It compiles the game's own presentation code (`cpp/ui/Draw.cpp`
and everything under it) against raylib's software renderer, drives the real
`ShellView` from a small command script, and writes frames.

Nothing here is built by CMake or CI. The game's files are only read:
everything the viewer fetches, builds and renders goes to `build/` and `out/`
in this folder, and git ignores both.

## What a frame is, and is not

A frame shows what the game computes: layout, strings, type sizes, hit boxes.

It is not the Windows build. Do not read these off a frame:

- **Fonts.** Windows loads Segoe UI and Consolas. Here `segoe` means Selawik,
  Microsoft's open face with Segoe UI's advance widths, and the mono face is
  always the DejaVu Sans Mono the repository ships. Mono widths are therefore
  not the Windows widths.
- **Edges and colour.** No GPU, no MSAA, no display-adjustment shader.
- **Sound, hover timing, animation feel.**

A frame is a look. Nothing is asserted and it is not a test. A clean frame is
not proof of how the game looks on Windows.

## Layout

| Path | What |
| --- | --- |
| `viewer.cpp` | The viewer: main loop, script commands, text recorder. |
| `Makefile` | Builds `build/viewer` from the checkout's sources. |
| `setup.sh` | Fetches, patches and builds the third-party parts into `build/`. |
| `run.sh` | Renders one script. |
| `patches/` | Three corrections to private copies of raylib and rlImGui. |
| `scripts/` | Scenarios. `s01`-`s15` made the screens in `shots/main-0b586a8/`. |
| `shots/` | Kept images. Nothing compares against them. |
| `build/`, `out/` | Generated and ignored by git. `setup.sh` and `make` rebuild them. |

## Build

Needs `g++`, `gcc`, `make`, `curl`, `tar`, `patch`. For the Segoe stand-in also
`python3` with `venv`. Network: `codeload.github.com`, and `pypi.org` for fontmake.

```sh
cd tools/viewer
./setup.sh          # once, about a minute: raylib 6.0, imgui 1.92.7, rlImGui, json, Selawik
make -j2            # about a minute on two cores; seconds after an edit
```

The versions are the ones the repository's `CMakeLists.txt` pins. The list of
game sources is read out of that file at build time (`make sources` prints it).
Left out on purpose: `cpp/app/main.cpp`, since the viewer is the main loop here,
and `cpp/audio/AudioDevice.cpp`.

Selawik (`microsoft/Selawik`, SIL Open Font License 1.1) is fetched at a pinned
commit and built in `build/`. No third-party file is kept in this folder; the
two files in `patches/` are diffs.

The viewer looks at the checkout it sits in. To look at another tree, for
instance an older commit unpacked with `git archive`, build a second viewer
from it and point a run at both:

```sh
make -j2 B=build/old DEPS=build/deps REPO=/path/to/tree
VIEWER_BIN=build/old/viewer SPACESHIP_REPO=/path/to/tree ./run.sh scripts/machine.txt
```

## Run

```sh
./run.sh scripts/s07.txt                     # 1440 x 940, Segoe stand-in
./run.sh scripts/machine.txt 1440 940 dejavu # the face the repository ships
./run.sh scripts/machine.txt 1180 720        # the client's smallest window
```

Frames go to `out/` (or `$VIEWER_OUT`). With `textlog on`, each `NAME.png` gets
a `NAME.text.tsv`: one line per string the game painted, with
`size  x  y  width  font  rgba  text`.

Exit code 0, or 3 if a script line could not be carried out.

## Script commands

One command per line. `#` starts a comment.

**Frames and reports**

| Command | Does |
| --- | --- |
| `frames N [ms]` | Run N frames, `ms` apart (default 1/60 s). |
| `shot NAME [x y w h]` | Write `NAME.png`: the frame, or one rectangle of it. |
| `textlog on\|off` | Write the text log beside each shot. |
| `stage` | The illustrated stage of the open room: each caption, the size it was painted at, and how far it exceeds its own box. |
| `overlaps [x y w h]` | Strings whose painted boxes intersect, in the frame or in one rectangle. |
| `hits [tabs\|side\|nav\|band\|task\|title\|loss\|all]` | The shell's own hit boxes. |
| `state` | Voyage state: actions, tab, air, stats, crew, events. |

**Pointer**

| Command | Does |
| --- | --- |
| `move x y` | Place the pointer. |
| `click x y`, `rclick x y` | Press at a point. |
| `clickhit LIST TEXT` | Press the first hit box in LIST whose label contains TEXT. Also `rclickhit`, `hoverhit`. |
| `scroll x y delta` | Mouse wheel. |
| `imclick x y` | A press and release delivered to Dear ImGui (popups, combos). |

**Windows**

| Command | Does |
| --- | --- |
| `open KIND ID` | Open a task window: `open room engine\|o2\|mess\|cargo\|quarters`, `open nav chart`, `open station st-gen-205`, `open event ID`. |
| `tab ID`, `title 0\|1`, `hidetitle`, `popup KEY`, `closepopup`, `escape`, `tutorial N` | Shell navigation. |

**Voyage, set directly**

| Command | Does |
| --- | --- |
| `act N [label]` | Perform N actions. |
| `stat ID VALUE` | Set a ship stat, e.g. `stat hull 0`. |
| `survival FIELD VALUE` | `airHold`, `filterLife`, `engineCondition`, `rack`, `canisters`. |
| `crewset ID FIELD VALUE` | `health`, `hunger`, `morale`, `fatigue`. |
| `purse N`, `job CREW JOB`, `ack ID`, `layer ID` | Shiplets, a crew job, acknowledge, chart layer. |
| `target ID`, `travel`, `scan`, `dock`, `undock`, `cook`, `maint`, `seat N`, `bank` | The simulation's own verbs. |
| `theme ID`, `fontstep 0-3`, `largetext 0\|1` | `classic`, `void`, `aurora`, `nebula`, `ember`; reading size. |

Every scenario after the title screen starts with the lines in
`scripts/common-launch.txt`. The viewer has no include command, so the scripts
carry those lines themselves.

## The three patches

Raylib's software renderer is new in 6.0. Without these the client draws wrong.
They change private copies under `build/deps/` only.

1. `rlsw.h`, `swScissor`: raylib passes OpenGL bottom-left clip rectangles and
   the rasterizer stores top-left ones. Clipped content landed at the wrong
   height. The patch converts.
2. `rlsw.h`, blend shortcut: blending was dropped for textures flagged as
   having no alpha. Kept for every textured primitive. This one did not turn
   out to be the cause of the text fault below; it is still applied.
3. `rlImGui.cpp`, `ImGuiRenderTriangles`: the texture was bound after
   `rlBegin`, which the rasterizer rejects, so Dear ImGui text came out as
   solid blocks. The patch binds first.

## How it reaches into the game

Two things a reader should know before trusting it:

- `viewer.cpp` includes `ui/Draw.h` with `private` defined as `public`, to read
  the shell's hit lists and the open screen's model. No game file is changed.
- It links with `-Wl,--wrap=DrawTextEx`, so every string the game paints through
  raylib is recorded with its size and measured width. Dear ImGui text is not
  recorded.

## The images in `shots/`

`shots/main-0b586a8/`: 16 screens made by `scripts/s01`-`s15` on 2026-11-11 from
`main` at `0b586a8`, 1440 x 940, Segoe stand-in. Re-run on that commit, 14 come
back pixel-identical; `15-home-crisis-scrolled` and `18-loss` differ only in the
wall-clock stamp of the activity feed. `findings.png` is a sheet cut from those
frames by hand.

`shots/machine-fix/`: the Engine Room machine before and after its captions
were given boxes that hold them.

| File | Made by |
| --- | --- |
| `before-18px`, `before-23px` | `scripts/machine.txt` on `0b586a8`. |
| `after-18px` ... `after-23px` | `scripts/machine.txt` on the re-laid machine, 1440 x 940, Segoe stand-in. |
| `after-18px-dejavu`, `after-23px-dejavu` | The same with `dejavu`. |
| `after-18px-1180x720` | The same at `1180 720`. |
| `after-100-nominal` ... `after-007-critical` | `scripts/machine-states.txt`. |
| `report-before.txt`, `report-after.txt` | The `stage` and `overlaps` output of those runs. |
| `before-after.png` | The two 18 px stages side by side, cut from the frames by hand. |

The scripts name their frames `machine-*.png`; the kept copies were renamed.

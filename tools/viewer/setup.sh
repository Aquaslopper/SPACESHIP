#!/usr/bin/env bash
# ============================================================================
#  setup.sh - fetch, patch and build the third-party parts of the viewer.
#  ---------------------------------------------------------------------------
#  Everything lands in build/, beside this script, which git ignores. Nothing
#  is installed on the machine and no tracked file is changed.
#
#  Needs: g++, gcc, make, curl, tar, patch. For the Segoe stand-in also python3
#  with venv (fontmake is installed into build/fontenv from PyPI).
#  Network: codeload.github.com, and pypi.org for fontmake.
#
#  Safe to run again: finished steps are skipped.
# ============================================================================
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deps="$here/build/deps"
fonts="$here/build/fonts"
mkdir -p "$deps" "$fonts"

# The versions the repository's CMakeLists.txt pins, and the commit of the font.
SELAWIK_SHA=89362e84731d1f5777fa078fd4d5ebbd339e4378

fetch() {  # fetch <directory the tarball unpacks to> <url>
  local dir="$1" url="$2"
  if [ -d "$deps/$dir" ]; then echo "have   $dir"; return; fi
  echo "fetch  $dir"
  curl -fsSL "$url" -o "$deps/$dir.tar.gz"
  tar -xzf "$deps/$dir.tar.gz" -C "$deps"
  rm -f "$deps/$dir.tar.gz"
  [ -d "$deps/$dir" ] || { echo "setup: $url did not unpack to $dir" >&2; exit 1; }
}

fetch raylib-6.0           https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/6.0
fetch imgui-1.92.7         https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.7
fetch rlImGui-Raylib_6_0   https://codeload.github.com/raylib-extras/rlImGui/tar.gz/refs/tags/Raylib_6_0
fetch json-3.11.3          https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
fetch "Selawik-$SELAWIK_SHA" "https://codeload.github.com/microsoft/Selawik/tar.gz/$SELAWIK_SHA"

apply() {  # apply <source root> <patch file>; a patch already in place is left alone
  local root="$1" file="$2"
  if patch -p1 -d "$root" --dry-run -R -s -f < "$file" > /dev/null 2>&1; then
    echo "have   $(basename "$file")"
  else
    echo "patch  $(basename "$file")"
    patch -p1 -d "$root" -s < "$file"
  fi
}

# Three corrections to the private copies, without which the software renderer
# draws the client wrong: clip rectangles at the wrong height, and Dear ImGui
# text as solid blocks. README.md says what each one does.
apply "$deps/raylib-6.0"         "$here/patches/raylib-6.0-rlsw.patch"
apply "$deps/rlImGui-Raylib_6_0" "$here/patches/rlImGui-Raylib_6_0.patch"

# raylib for PLATFORM_MEMORY: frames are rasterized into memory, no display.
# rlsw.h is a header inside rcore.c and the raylib Makefile does not track it.
if [ ! -f "$deps/raylib-6.0/src/libraylib.a" ] ||
   [ "$deps/raylib-6.0/src/external/rlsw.h" -nt "$deps/raylib-6.0/src/libraylib.a" ]; then
  echo "build  raylib (PLATFORM_MEMORY)"
  touch "$deps/raylib-6.0/src/rcore.c"
  make -C "$deps/raylib-6.0/src" -j"$(nproc)" PLATFORM=PLATFORM_MEMORY \
       CUSTOM_CFLAGS="-DSUPPORT_FILEFORMAT_JPG=1 -O2" > "$here/build/raylib-build.log" 2>&1 ||
    { tail -30 "$here/build/raylib-build.log" >&2; exit 1; }
else
  echo "have   libraylib.a"
fi

# Selawik: Microsoft's open face with the advance widths of Segoe UI, the font
# the Windows client loads first. It ships as UFO sources only.
if [ -f "$fonts/Selawik-Regular.ttf" ] && [ -f "$fonts/Selawik-Bold.ttf" ]; then
  echo "have   Selawik"
else
  echo "build  Selawik (fontmake)"
  if python3 -m venv "$here/build/fontenv" > /dev/null 2>&1 &&
     "$here/build/fontenv/bin/pip" install --quiet fontmake > "$here/build/fontmake-install.log" 2>&1; then
    for weight in Regular Bold; do
      "$here/build/fontenv/bin/fontmake" \
          -u "$deps/Selawik-$SELAWIK_SHA/Source files/UFO/Selawik-$weight.ufo" \
          -o ttf --output-dir "$fonts" > "$here/build/fontmake-$weight.log" 2>&1 ||
        { tail -20 "$here/build/fontmake-$weight.log" >&2; exit 1; }
    done
  else
    echo "setup: fontmake could not be installed; only the 'dejavu' font set will work." >&2
  fi
fi

echo "done.  next: make && ./run.sh scripts/s07.txt"

#!/usr/bin/env bash
# ============================================================================
#  check-gui-syntax.sh — prove the drawing code still compiles.
#  ---------------------------------------------------------------------------
#  This machine may not have X11 or OpenGL development files, so the client
#  cannot link here - but the compiler can still parse it. That is the check
#  that matters during the port: a rename in raylib or ImGui must fail loudly
#  now, not on the first machine with a display.
#
#  Finds the three headers wherever they are:
#    SPACESHIP_GUI_INCLUDE  colon-separated include roots, if set
#    build/_deps/*-src      what FetchContent leaves behind
#    /tmp/deps/*            a pre-vendored copy
#
#  Exit 0 = clean, 1 = the compiler complained, 77 = not installed here (CTest
#  reports a skip rather than a failure).
# ============================================================================
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CXX_BIN="${CXX:-c++}"

found_raylib=""
found_imgui=""
found_rlimgui=""

add_candidates() {
  local base="$1"
  [ -d "$base" ] || return 0
  [ -z "$found_raylib" ]   && [ -f "$base/raylib-6.0/src/raylib.h" ] && found_raylib="$base/raylib-6.0/src"
  [ -z "$found_raylib" ]   && [ -f "$base/src/raylib.h" ] && found_raylib="$base/src"
  [ -z "$found_raylib" ]   && [ -f "$base/raylib-src/src/raylib.h" ] && found_raylib="$base/raylib-src/src"
  [ -z "$found_imgui" ]    && [ -f "$base/imgui-1.92.7/imgui.h" ] && found_imgui="$base/imgui-1.92.7"
  [ -z "$found_imgui" ]    && [ -f "$base/imgui/imgui.h" ] && found_imgui="$base/imgui"
  [ -z "$found_imgui" ]    && [ -f "$base/imgui-src/imgui.h" ] && found_imgui="$base/imgui-src"
  [ -z "$found_rlimgui" ]  && [ -f "$base/rlImGui-Raylib_6_0/rlImGui.h" ] && found_rlimgui="$base/rlImGui-Raylib_6_0"
  [ -z "$found_rlimgui" ]  && [ -f "$base/rlImGui/rlImGui.h" ] && found_rlimgui="$base/rlImGui"
  [ -z "$found_rlimgui" ]  && [ -f "$base/rlImGui-src/rlImGui.h" ] && found_rlimgui="$base/rlImGui-src"
  [ -z "$found_rlimgui" ]  && [ -f "$base/rlimgui-src/rlImGui.h" ] && found_rlimgui="$base/rlimgui-src"
}

if [ -n "${SPACESHIP_GUI_INCLUDE:-}" ]; then
  IFS=':' read -r -a roots <<< "$SPACESHIP_GUI_INCLUDE"
  for root in "${roots[@]}"; do
    [ -z "$found_raylib" ]  && [ -f "$root/raylib.h" ] && found_raylib="$root"
    [ -z "$found_imgui" ]   && [ -f "$root/imgui.h" ] && found_imgui="$root"
    [ -z "$found_rlimgui" ] && [ -f "$root/rlImGui.h" ] && found_rlimgui="$root"
  done
fi

add_candidates "$ROOT/build/_deps"
add_candidates "/tmp/deps"
add_candidates "$ROOT/third_party"

if [ -z "$found_raylib" ] || [ -z "$found_imgui" ] || [ -z "$found_rlimgui" ]; then
  echo "GUI headers not installed here (raylib/imgui/rlImGui); nothing to syntax-check."
  exit 77
fi

set -e
for unit in cpp/ui/Draw.cpp cpp/app/main.cpp; do
  "$CXX_BIN" -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic \
    -I"$ROOT/cpp" \
    -I"$found_raylib" -I"$found_imgui" -I"$found_rlimgui" -I"$found_rlimgui/extras" \
    "$ROOT/$unit"
  echo "$unit: clean under -Wall -Wextra -Wpedantic"
done

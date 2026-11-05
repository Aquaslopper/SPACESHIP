#!/usr/bin/env bash
# CPU recording test of the real native painter. No GPU/window required.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
IMGUI="$ROOT/build/gui-headers/imgui-1.92.7"
RAYLIB="$ROOT/build/gui-headers/raylib-6.0/src"
RLIMGUI="$ROOT/build/gui-headers/rlImGui-Raylib_6_0"
[ -f "$IMGUI/imgui.cpp" ] || IMGUI="$BUILD/_deps/imgui-src"
[ -f "$RAYLIB/raylib.h" ] || RAYLIB="$BUILD/_deps/raylib-src/src"
[ -f "$RLIMGUI/rlImGui.h" ] || RLIMGUI="$BUILD/_deps/rlimgui-src"
if [ ! -f "$IMGUI/imgui.cpp" ] || [ ! -f "$RAYLIB/raylib.h" ] || [ ! -f "$RLIMGUI/rlImGui.h" ]; then
  echo 'Pinned GUI sources unavailable; draw recorder skipped'; exit 77
fi
if [ ! -f "$BUILD/libspaceship_core.a" ]; then
  echo 'Unix static core library unavailable; CPU draw recorder skipped'; exit 77
fi
mkdir -p "$BUILD/draw-probe"
"${CXX:-c++}" -std=c++17 -O0 -I"$IMGUI" -I"$RAYLIB" -I"$RLIMGUI" -I"$ROOT/cpp" \
  -I"$BUILD/_deps/nlohmann_json-src/include" \
  "$ROOT/cpp/tests/draw_probe.cpp" "$ROOT/cpp/ui/Draw.cpp" \
  "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" \
  "$BUILD/libspaceship_core.a" -o "$BUILD/draw-probe/probe"
cd "$ROOT"
"$BUILD/draw-probe/probe"

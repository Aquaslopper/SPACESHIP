#!/usr/bin/env bash
# Optional CPU event regression using the pinned ImGui sources, no OS/GPU.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMGUI=""
IFS=: read -ra INCLUDES <<< "${SPACESHIP_GUI_INCLUDE:-}"
for dir in "${INCLUDES[@]}" "$ROOT/build/_deps/imgui-src" "$ROOT/build/gui-headers/imgui-1.92.7"; do
  if [ -f "$dir/imgui.cpp" ]; then IMGUI="$dir"; break; fi
done
if [ -z "$IMGUI" ]; then echo 'Pinned ImGui sources unavailable; input probe skipped'; exit 77; fi
mkdir -p "$ROOT/build/input-probe"
"${CXX:-c++}" -std=c++17 -O0 -I"$IMGUI" -I"$ROOT/cpp" \
  "$ROOT/cpp/tests/imgui_input_probe.cpp" "$IMGUI/imgui.cpp" "$IMGUI/imgui_draw.cpp" \
  "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp" -o "$ROOT/build/input-probe/probe"
"$ROOT/build/input-probe/probe"

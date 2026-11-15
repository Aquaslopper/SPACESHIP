#!/usr/bin/env bash
# Protocol GameOver: run boundary checks against the real simulation library.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build-gameover}"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DSPACESHIP_BUILD_GUI=OFF -DSPACESHIP_BUILD_TESTS=OFF
cmake --build "$build" --target spaceship_core --parallel "${BUILD_JOBS:-4}"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic \
  -I"$root/cpp" -I"$build/_deps/nlohmann_json-src/single_include" \
  "$root/cpp/tests/gameover_protocol.cpp" "$build/libspaceship_core.a" \
  -o "$build/gameover_protocol"
cd "$root"
"$build/gameover_protocol"

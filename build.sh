#!/usr/bin/env bash
# Configure and build the native game (or the headless simulation and tests).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-$root/build}"
gui=ON
tests=ON
for arg in "$@"; do
  case "$arg" in
    --headless) gui=OFF ;;
    --no-tests) tests=OFF ;;
    -h|--help)
      printf 'Usage: ./build.sh [--headless] [--no-tests]\nSet BUILD_DIR to choose the build directory.\n'
      exit 0 ;;
    *) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done
for tool in cmake; do
  command -v "$tool" >/dev/null || { printf '%s is required. See docs/DEVELOPMENT.md.\n' "$tool" >&2; exit 1; }
done
cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DSPACESHIP_BUILD_GUI="$gui" -DSPACESHIP_BUILD_TESTS="$tests"
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-4}"
if [[ "$gui" == ON ]]; then
  # CMake can build just the core when graphics headers are absent. Do not
  # call that a game build: fail if the actual client target is unavailable.
  if ! cmake --build "$build_dir" --target spaceship; then
    printf 'The game client could not be built. Install the graphics development packages in docs/DEVELOPMENT.md, or use --headless.\n' >&2
    exit 1
  fi
fi
if [[ "$tests" == ON ]]; then
  ctest --test-dir "$build_dir" --output-on-failure
fi
printf '\nBuild complete: %s\n' "$build_dir"

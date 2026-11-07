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
      printf '\nPrerequisites:\n'
      printf '  cmake >= 3.20 for --headless, >= 3.25 for the client (raylib 6.0 requires it)\n'
      printf '  a C++17 compiler, and network access on the first configure (deps are fetched)\n'
      printf '  client only, Debian/Ubuntu: sudo apt-get install -y libgl1-mesa-dev libx11-dev \\\n'
      printf '      libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev\n'
      printf '  client only, Fedora: sudo dnf install -y mesa-libGL-devel libX11-devel \\\n'
      printf '      libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libXext-devel\n'
      exit 0 ;;
    *) printf 'Unknown option: %s\n' "$arg" >&2; exit 2 ;;
  esac
done
for tool in cmake; do
  command -v "$tool" >/dev/null || { printf '%s is required. Run ./build.sh --help for the prerequisites.\n' "$tool" >&2; exit 1; }
done
# Say this here, in one line a reader recognises, rather than letting the
# fetch of raylib abort with "CMake step for raylib failed" two minutes later.
if [[ "$gui" == ON ]]; then
  cmver="$(cmake --version | sed -n '1{s/[^0-9]*\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/;p}')"
  if [[ "$(printf '%s\n3.25\n' "${cmver:-0.0}" | sort -V | head -1)" != "3.25" ]]; then
    printf 'cmake 3.25 or newer is required for the client (raylib 6.0); this is %s.\n' "$cmver" >&2
    printf 'Install a newer CMake, or build the simulation and tests with ./build.sh --headless\n' >&2
    exit 1
  fi
fi
cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DSPACESHIP_BUILD_GUI="$gui" -DSPACESHIP_BUILD_TESTS="$tests" \
  -DSPACESHIP_REQUIRE_GUI="$([[ "$gui" == ON ]] && echo ON || echo OFF)"
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

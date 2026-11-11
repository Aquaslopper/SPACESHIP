#!/usr/bin/env bash
# ============================================================================
#  run.sh - render one viewer script.
#
#    ./run.sh SCRIPT [WIDTH HEIGHT] [segoe|dejavu]
#
#  Defaults: 1440 x 940 (the client's default window) and the 'segoe' set.
#  Frames and text logs go to out/ (or $VIEWER_OUT).
#
#  The game reads "assets/..." relative to its working directory, so the viewer
#  runs inside build/run, where assets is a link to the repository's folder.
#  Saves the shell writes go to build/run/saves. The game's files are only read.
#
#  To look at another tree: SPACESHIP_REPO is the tree, VIEWER_BIN the viewer
#  built from it (make B=... REPO=...).
# ============================================================================
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="${SPACESHIP_REPO:-$here/../..}"

[ $# -ge 1 ] || { sed -n '3,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,2\}//' >&2; exit 2; }
viewer="${VIEWER_BIN:-$here/build/viewer}"
[ -x "$viewer" ] || { echo "run: $viewer is missing; run ./setup.sh and make" >&2; exit 1; }
viewer="$(realpath "$viewer")"
[ -d "$repo/assets" ] || { echo "run: no assets folder in $repo (set SPACESHIP_REPO)" >&2; exit 1; }

script="$(realpath "$1")"
shift

mkdir -p "$here/build/run"
ln -sfn "$(realpath "$repo/assets")" "$here/build/run/assets"

# A relative VIEWER_OUT is taken from where run.sh was called, not from build/run.
out="${VIEWER_OUT:-$here/out}"
mkdir -p "$out"
export VIEWER_OUT="$(realpath "$out")"
export VIEWER_SAVES="$here/build/run/saves"
export VIEWER_FONTS="$here/build/fonts"

cd "$here/build/run"
# stdin is closed: the memory platform polls it for the escape key.
exec "$viewer" "$script" "$@" < /dev/null

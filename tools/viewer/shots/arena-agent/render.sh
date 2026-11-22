#!/usr/bin/env bash
# ============================================================================
#  render.sh - refresh the biome frames in this folder.
#
#    ../../setup.sh          once, if tools/viewer has not been built
#    make -C ../..           once
#    ./render.sh             writes the PNGs beside this script
#
#  Frames come from biomes.txt through tools/viewer/run.sh at the client's
#  default window, 1440 x 940. Nothing is compared against them.
# ============================================================================
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
viewer="$(cd "$here/../.." && pwd)"

VIEWER_OUT="$here" "$viewer/run.sh" "$here/biomes.txt" 1440 940

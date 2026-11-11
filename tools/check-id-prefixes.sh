#!/usr/bin/env bash
# Generated celestial/station ids must carry explicit type prefixes:
# st- stations, pl- planets, mn- moons, sr- stars, bt- belts, si- sites.
# Fail the build if any generated prefix disappears.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/cpp/data/ChartExpansion.cpp"
gen="$root/cpp/data/GalaxyGen.cpp"
hdr="$root/cpp/data/ChartExpansion.h"

fail() { echo "id-prefix check: $*" >&2; exit 1; }

[[ -f "$src" && -f "$gen" && -f "$hdr" ]] || fail "ChartExpansion/GalaxyGen sources missing"

grep -q 'st-' "$src" || fail "ChartExpansion.cpp has no st- station prefix"
grep -q 'pl-' "$src" || fail "ChartExpansion.cpp has no pl- planet prefix"
grep -q 'mn-' "$src" || fail "ChartExpansion.cpp has no mn- moon prefix"
grep -q 'bt-' "$src" || fail "ChartExpansion.cpp has no bt- belt prefix"
grep -q 'si-' "$src" || fail "ChartExpansion.cpp has no si- site prefix"

grep -q 'sr-' "$gen" || fail "GalaxyGen.cpp has no sr- star prefix"
grep -q 'st-' "$gen" || fail "GalaxyGen.cpp has no st- station prefix"
grep -q 'pl-' "$gen" || fail "GalaxyGen.cpp has no pl- planet prefix"
grep -q 'mn-' "$gen" || fail "GalaxyGen.cpp has no mn- moon prefix"
grep -q 'bt-' "$gen" || fail "GalaxyGen.cpp has no bt- belt prefix"
grep -q 'si-' "$gen" || fail "GalaxyGen.cpp has no si- site prefix"

if grep -nE 'side \+ "-station"' "$src"; then
  fail "stations must use st- prefix, not a -station suffix only"
fi
if grep -nE '= side \+ "-outpost"' "$src"; then
  fail "outposts must use st- prefix on generated ids"
fi
if grep -nE 'const std::string id = side \+ "-p"' "$src"; then
  fail "planets must use pl- prefix on generated ids"
fi
if grep -nE 'side \+ \(belt \? "-field"' "$src"; then
  fail "belts and sites must use bt- / si- prefixes on generated ids"
fi

grep -q 'IsStationId' "$hdr" || fail "IsStationId missing from ChartExpansion.h"
grep -q 'IsPlanetId' "$hdr" || fail "IsPlanetId missing from ChartExpansion.h"
grep -q 'IsMoonId' "$hdr" || fail "IsMoonId missing from ChartExpansion.h"
grep -q 'IsStarId' "$hdr" || fail "IsStarId missing from ChartExpansion.h"
grep -q 'IsBeltId' "$hdr" || fail "IsBeltId missing from ChartExpansion.h"
grep -q 'IsSiteId' "$hdr" || fail "IsSiteId missing from ChartExpansion.h"

echo "id-prefix check: st-, pl-, mn-, sr-, bt-, si- generated prefixes are present"

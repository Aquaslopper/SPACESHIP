#!/usr/bin/env bash
# Generated stations must be st-, planets pl-. Fail the build if those
# prefixes disappear from ChartExpansion again.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/cpp/data/ChartExpansion.cpp"
hdr="$root/cpp/data/ChartExpansion.h"

fail() { echo "id-prefix check: $*" >&2; exit 1; }

[[ -f "$src" && -f "$hdr" ]] || fail "ChartExpansion sources missing"

grep -q 'st-' "$src" || fail "ChartExpansion.cpp has no st- station prefix"
grep -q 'pl-' "$src" || fail "ChartExpansion.cpp has no pl- planet prefix"
if grep -nE 'side \+ "-station"' "$src"; then
  fail "stations must use st- prefix, not a -station suffix only"
fi
if grep -nE 'const std::string id = side \+ "-p"' "$src"; then
  fail "planets must use pl- prefix on generated ids"
fi
grep -q 'IsStationId' "$hdr" || fail "IsStationId missing from ChartExpansion.h"
grep -q 'IsPlanetId' "$hdr" || fail "IsPlanetId missing from ChartExpansion.h"

echo "id-prefix check: st- stations and pl- planets are present"

// ============================================================================
//  ChartExpansion.h — the rest of the sky.
//  ---------------------------------------------------------------------------
//  The authored catalogue holds seven regions, forty-eight systems and 243
//  local bodies, and it packs every one of those systems into a 638x278 board.
//  Forty-one of the forty-eight systems share one of seven positions, so a
//  sector chart that shows the whole galaxy paints six systems on top of each
//  other and only the last one can be clicked. The chart is not lying about the
//  sky; the sky was never laid out.
//
//  ExpandChart() fixes the board and fills it in, before a voyage begins:
//
//    * the chart the reference builds is rebuilt from its rows through
//      data/GalaxyGen.h - the export in assets/data/chart.json is the
//      reference's output, and galaxy_gen_tests.cpp proves the ported passes
//      reproduce it field for field.
//    * the sector board grows to twice the authored width and height, and the
//      authored systems move to its middle. A translation preserves every
//      distance, so an authored crossing costs exactly what it always cost.
//    * every region gains rows of its own. Each row goes through the same two
//      passes as an authored one, so its system has a star, a world or belt, a
//      landmark (a station with a priced port when the row names a profile), an
//      outer world and a moon - five local bodies, the reference's klass
//      strings and its detail rows.
//    * each new system is joined to its two nearest neighbours in its own
//      region, so nothing is stranded.
//
//  The generator is deterministic: one seed, one sky, every load and every
//  save. Ids are stable for the same reason, which is what lets a save point at
//  a generated body and find it again.
//
//  It is idempotent. A catalogue that has already been expanded - or one the
//  player has already been flying in - is returned untouched, so
//  Simulation::NewVoyage(sim.World()) cannot grow the galaxy a second time.
// ============================================================================
#pragma once

#include "data/Catalog.h"

namespace ship {

// How many rows the expansion drafts per region. Seven regions, so the charted
// galaxy goes from 48 systems to 48 + 7 * kNewSystemsPerRegion.
constexpr int kNewSystemsPerRegion = 22;

// The sector board, in authored boards. A doubling keeps the authored spacing
// while quadrupling the room, which is what lets every system own a position.
constexpr double kSectorBoardScale = 2.0;

// Grows the charted galaxy. Safe to call on any loaded catalogue, any number of
// times; only the first call does anything.
void ExpandChart(Catalog& catalog);

}  // namespace ship

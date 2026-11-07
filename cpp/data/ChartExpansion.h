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

// The sector board, in authored boards. Tripling the authored width and height
// multiplies the room nine times, which is what lets 202 systems keep a gap of
// their own instead of crowding into each other at the edited scale. The
// authored systems are translated, never scaled, so every authored distance -
// and every crossing that has to pay for it - is unchanged.
constexpr double kSectorBoardScale = 6.0;

// The zoom ladder the grown chart is read with. The authored ladder is
// 0.6..2.0 in steps of 0.2 - eight steps, which on a board three times the
// authored width is not enough either way: 2x cannot separate the systems of a
// 66-pixel cluster, and 0.6x cannot show the whole sector. The ladder is
// widened here rather than in the export, so the Phase 0 data gate still sees
// the reference's numbers.
constexpr double kZoomMin = 0.3;
constexpr double kZoomMax = 8.0;
constexpr double kZoomStep = 0.2;

// The local chart keeps a shorter ladder. Its bodies sit on rings out to 200
// pixels, so at four times magnification the visible window is 80 pixels wide
// and lands in the hole at the middle of the system - an empty chart. The wide
// scales are the ones that needed the extra reach.
constexpr double kSystemZoomMin = 0.6;
constexpr double kSystemZoomMax = 2.5;

// Grows the charted galaxy. Safe to call on any loaded catalogue, any number of
// times; only the first call does anything.
void ExpandChart(Catalog& catalog);

// Fills every local chart with worlds of its own: three to six extra planets,
// two moons, and a belt or a derelict here and there, all through the
// reference's AddLocalBody() and all laid out from a hash of the system's id.
// Deterministic: the same system always gets the same sky, so a saved scan of
// a generated world still points at that world.
//
// Called by ExpandChart() after ExpandLocalCharts(), never instead of it - the
// reference's five bodies are what the parity test pins, and this pass only
// adds to them.
void PopulateLocalCharts(Catalog& catalog, std::size_t firstSystem);

// Places every system on the galaxy board, inside the cluster of its region, so
// the galaxy scale can show 202 real stars instead of seven labelled blobs.
// Deterministic, and stored on the catalogue (Catalog::galaxyPositions) because
// three places ask for it: the chart, the ship's marker and the tests.
void PlaceGalaxyDots(Catalog& catalog);

// Joins neighbouring regions on the galaxy board until every region has three
// lanes of its own. The authored galaxy carries nine links between seven
// regions, so the web reads as a chain; three lines per region make it read as
// a map.
void EntangleGalaxy(Catalog& catalog);

}  // namespace ship

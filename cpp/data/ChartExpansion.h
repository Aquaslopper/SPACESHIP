// ============================================================================
//  ChartExpansion.h — the chart a voyage is flown on.
//  ----------------------------------------------------------------------------
//  One job now: give the catalogue the three boards every scale is drawn on and
//  the galaxy dots its dots sit at. The chart itself - systems, worlds, berths,
//  lanes - is generated from a seed by UniverseGen; the passes that rebuilt an
//  authored table of star rows and drafted systems from eight price profiles are
//  gone, because a player saw through them in one system.
// ============================================================================
#pragma once

#include "data/Catalog.h"

namespace ship {

// Board multipliers relative to the 638x278 reference viewport.
// Authored core regions and systems are translated to the board centre so
// pairwise distances between authored destinations remain unchanged.
constexpr double kSectorBoardScale = 10.0;
constexpr double kGalaxyBoardScale = 4.0;

// The sector board is ten times the reference in each direction. The panel shows
// all of it at zoom 1 and a window onto it above that; the board is not sized by
// the panel.


// Zoom ladder for the wide sector and galaxy boards.
constexpr double kZoomMin = 0.3;
constexpr double kZoomMax = 10.0;
constexpr double kZoomStep = 0.2;

// Zoom ladder for the local system chart.
constexpr double kSystemZoomMin = 0.6;
constexpr double kSystemZoomMax = 2.5;

void ExpandChart(Catalog& catalog);
void PlaceGalaxyDots(Catalog& catalog);
void EntangleGalaxy(Catalog& catalog);
void RegisterMapIncidents(Catalog& catalog);

}  // namespace ship

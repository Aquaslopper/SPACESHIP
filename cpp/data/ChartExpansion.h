// ============================================================================
//  ChartExpansion.h — procedural multi-scale chart expansion.
//  ---------------------------------------------------------------------------
//  Expands an authored Catalog copy before a voyage begins:
//    * rebuilds the authored star rows and local five-body pass via GalaxyGen
//    * widens the galaxy board and generates outer galactic regions/clusters
//      with regional histories, traffic reports, and inter-galactic lanes
//    * widens the sector board, preserves authored system offsets at its core,
//      and populates each galactic region with a coherent territory of star
//      systems linked by intra-region lanes
//    * populates every star system with multi-ring planetary orbits, natural
//      moons around host worlds, debris belts, derelict anomalies, and ports
//    * registers map-driven incident types triggered by procedural exploration
//
//  Deterministic and idempotent: expanding an already-expanded Catalog is a
//  no-op, and identical seeds produce identical ids and coordinates across
//  saves and platforms.
// ============================================================================
#pragma once

#include "data/Catalog.h"

namespace ship {

// Procedural galactic regions added around the 7 authored core regions
// (7 + 17 = 24 galactic regions/clusters on the galaxy layer).
constexpr int kNewGalaxies = 17;

// Procedural star systems drafted per galactic region
// (48 authored + 24 * 24 = 624 star systems across the universe).
constexpr int kNewSystemsPerRegion = 24;

// Board multipliers relative to the 638x278 reference viewport.
// Authored core regions and systems are translated to the board centre so
// pairwise distances between authored destinations remain unchanged.
constexpr double kSectorBoardScale = 6.0;
constexpr double kGalaxyBoardScale = 4.0;

// Zoom ladder for the wide sector and galaxy boards.
constexpr double kZoomMin = 0.3;
constexpr double kZoomMax = 4.0;
constexpr double kZoomStep = 0.2;

// Zoom ladder for the local system chart.
constexpr double kSystemZoomMin = 0.6;
constexpr double kSystemZoomMax = 2.5;

void ExpandChart(Catalog& catalog);
void PopulateGalaxies(Catalog& catalog, double coreDx, double coreDy);
void PopulateLocalCharts(Catalog& catalog, std::size_t firstSystem);
void PlaceGalaxyDots(Catalog& catalog);
void EntangleGalaxy(Catalog& catalog);
void RegisterMapIncidents(Catalog& catalog);

}  // namespace ship

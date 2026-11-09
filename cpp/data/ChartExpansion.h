// ============================================================================
//  ChartExpansion.h — procedural multi-scale chart expansion.
//  ---------------------------------------------------------------------------
//  Expands an Catalog copy before a voyage begins:
//    * discards the loaded chart places; the tables loaded with them remain
//      generator inputs, and the star rows are not rebuilt
//    * widens the galaxy board and generates galactic regions/clusters
//      with regional histories, traffic reports, and inter-galactic lanes
//    * widens the sector board and populates each generated region with a
//      coherent territory of star systems linked by intra-region lanes
//    * populates every star system with multi-ring planetary orbits, natural
//      moons around host worlds, debris belts, derelict anomalies, and ports
//    * gives every star system a station: a system that arrives with a port
//      keeps it, and one that does not is given a berth, or generated
//      alike, with no quota and no exceptions
//    * registers map-driven incident types triggered by procedural exploration
//
//  Deterministic and idempotent: expanding an already-expanded Catalog is a
//  no-op, and identical seeds produce identical ids and coordinates across
//  saves and platforms.
// ============================================================================
#pragma once

#include "data/Catalog.h"

namespace ship {

// Procedural galactic regions generated for a voyage
// (17 galactic regions/clusters on the galaxy layer).
constexpr int kNewGalaxies = 17;

// Procedural star systems drafted per galactic region
// (17 * 24 = 408 star systems across the universe).
constexpr int kNewSystemsPerRegion = 24;

// Galaxy counts at or above this were measured to exhaust RAM and CPU.
constexpr int kGalaxyRamWarning = 25;

// Board multipliers relative to the 638x278 reference viewport.
constexpr double kSectorBoardScale = 6.0;
constexpr double kGalaxyBoardScale = 4.0;

// Zoom ladder for the wide sector and galaxy boards.
constexpr double kZoomMin = 0.3;
constexpr double kZoomMax = 10.0;
constexpr double kZoomStep = 0.2;

// Zoom ladder for the local system chart.
constexpr double kSystemZoomMin = 0.6;
constexpr double kSystemZoomMax = 2.5;

void ExpandChart(Catalog& catalog);
void ExpandChart(Catalog& catalog, const ChartSettings& settings);

struct ChartPreviewDot {
  std::string id;
  std::string name;
  std::string parent;
  double x = 0;
  double y = 0;
};

struct ChartPreview {
  ChartSettings settings;
  double galaxyW = 1;
  double galaxyH = 1;
  double sectorW = 1;
  double sectorH = 1;
  std::vector<ChartPreviewDot> galaxies;
  std::vector<ChartPreviewDot> systems;
};

ChartPreview BuildChartPreview(const Catalog& catalog, const ChartSettings& settings);

void PopulateGalaxies(Catalog& catalog, double coreDx, double coreDy);
void PopulateLocalCharts(Catalog& catalog, std::size_t firstSystem);
void PlaceGalaxyDots(Catalog& catalog);
void EntangleGalaxy(Catalog& catalog);
void RegisterMapIncidents(Catalog& catalog);

}  // namespace ship

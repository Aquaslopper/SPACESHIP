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

#include <cstdint>

#include "data/Catalog.h"

namespace ship {

// Procedural galactic regions generated for a voyage
// (17 galactic regions/clusters on the galaxy layer).
constexpr int kNewGalaxies = 17;

// Procedural star systems drafted per galactic region
// (17 * 24 = 408 star systems across the universe).
constexpr int kNewSystemsPerRegion = 24;

// Generated chart ids: every body type carries its prefix so the starmap and
// trade board can tell stations, planets, moons, stars, belts, and sites apart.
inline bool IsStationId(const std::string& id) {
  return id.rfind("st-", 0) == 0 ||
         (id.size() >= 8 && id.compare(id.size() - 8, 8, "-station") == 0);
}
inline bool IsPlanetId(const std::string& id) { return id.rfind("pl-", 0) == 0; }
inline bool IsMoonId(const std::string& id) { return id.rfind("mn-", 0) == 0; }
inline bool IsStarId(const std::string& id) { return id.rfind("sr-", 0) == 0; }
inline bool IsBeltId(const std::string& id) { return id.rfind("bt-", 0) == 0; }
inline bool IsSiteId(const std::string& id) { return id.rfind("si-", 0) == 0; }

constexpr int kMinGalaxies = 1;
constexpr int kMaxGalaxies = 100;
// Galaxy counts at or above this were measured to exhaust RAM and CPU.
constexpr int kGalaxyRamWarning = 25;

constexpr int kMapSizeCount = 4;
constexpr double kMapSizeStep = 1.20;

inline int ClampGalaxies(int n) {
  return n < kMinGalaxies ? kMinGalaxies : (n > kMaxGalaxies ? kMaxGalaxies : n);
}
inline int ClampMapSize(int size) {
  return size < 0 ? 0 : (size >= kMapSizeCount ? kMapSizeCount - 1 : size);
}
inline double MapSizeScale(int size) {
  double scale = 1.0;
  for (int i = 0; i < ClampMapSize(size); ++i) { scale *= kMapSizeStep; }
  return scale;
}
inline const char* MapSizeLabel(int size) {
  switch (ClampMapSize(size)) {
    case 0: return "SMALL";
    case 1: return "NORMAL";
    case 2: return "MEDIUM";
    default: return "BIG";
  }
}

// Board multipliers relative to the 638x278 reference viewport.
constexpr double kSectorBoardScale = 6.0;
constexpr double kGalaxyBoardScale = 4.0;

// Zoom ladder for the wide sector and galaxy boards.
constexpr double kZoomMin = 0.25;
constexpr double kZoomMax = 16.0;
constexpr double kZoomStep = 0.2;

// Zoom ladder for the local system chart.
constexpr double kSystemZoomMin = 0.5;
constexpr double kSystemZoomMax = 12.0;

void ExpandChart(Catalog& catalog);
void ExpandChart(Catalog& catalog, const ChartSettings& settings);

// The generated chart has no authored berths, so a hire has no station written
// into crew.json to stand in. This gives every port two names from the pool,
// worked out from the port's own id and the chart seed: the same chart always
// puts the same people in the same bars, and no two ports get the same pair.
// It mutates catalog.candidates[i].port, so it must be called *after*
// ExpandChart and again whenever a save rebuilds the chart.
void AssignCandidatePorts(Catalog& catalog, std::uint64_t seed);

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

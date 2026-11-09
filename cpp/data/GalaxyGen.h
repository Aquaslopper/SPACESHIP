// ============================================================================
//  GalaxyGen.h — the generator, not its output.
//  ---------------------------------------------------------------------------
//  assets/data/chart.json carries the systems that have names people know,
//  their local bodies and their ports. Everything else — the regions, the rest
//  of the systems in each region, outer worlds, moons, orbital rings and
//  stations — is built by these passes when a voyage starts.
//
//    * BuildGalaxyFromRows() turns row records into systems, local bodies,
//      ports and lanes.
//    * ExpandLocalCharts() gives a system an outer world and a moon, and
//      rewrites its summary rows to match.
//
//  Both passes are deterministic: the same rows in the same order produce the
//  same ids, names and coordinates on every platform and after every reload.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "data/Catalog.h"

namespace ship {

// One row record: the fields the passes read, in the order they read them.
struct StarRow {
  std::string system;    // "sec-gen-12"
  std::string region;    // "gal-gen-3"
  std::string name;      // "Hollow Verge Cluster"
  double x = 0.0;        // position on the sector board
  double y = 0.0;
  std::string star;      // "Hollow Verge A"
  std::string world;     // "Ash Belt" - a belt when the name contains "Belt"
  std::string landmark;  // "Hollow Verge Exchange"
  std::string profile;   // "industry", or "" for a landmark with no port
  std::string note;      // the observation both bodies are scanned with
};

// systems, local bodies and ports from rows.
//
// baseIndex the row index the first row is treated as having. It decides each
// row's local offsets (i % 3, i % 2) and each system's outer-world profile
// (i % 4), so a later batch continues the count instead of repeating the
// earlier batch's local charts.
// linkHubs joins each region's first system to that region's hub, so a region
// is one connected group of destinations.
void BuildGalaxyFromRows(const std::vector<StarRow>& rows, std::size_t baseIndex, Catalog& catalog,
                         bool linkHubs);

// addSystemBody(): the one way of creating a local body - klass by type,
// System and Region rows, the observation plus the type's fixed reading, and
// the matching locked scan. Everything that adds a local body goes through
// here, so every world is described by the same rules.
Body& AddLocalBody(Catalog& catalog, const std::string& id, const std::string& type,
                   const std::string& name, double x, double y, const Body& system,
                   const std::string& text);

// expandLocalCharts(): an outer world and a moon for every system, from
// firstSystem onward, using the system's position in the catalogue as its
// index so names and positions never repeat across a batch.
void ExpandLocalCharts(Catalog& catalog, std::size_t firstSystem);

}  // namespace ship

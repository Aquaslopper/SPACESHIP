// ============================================================================
//  GalaxyGen.h — the generator, not its output.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "data/Catalog.h"

namespace ship {

// One STAR_CATALOG row, in the reference's column order.
struct StarRow {
  std::string system;    // "sec-vance"
  std::string region;    // "gal-inner"
  std::string name;      // "Vance Cluster"
  double x = 0.0;        // position on the sector board the reference's
  double y = 0.0;        // designer laid out by hand
  std::string star;      // "Vance A"
  std::string world;     // "Furnace Belt" - a belt if the name contains "Belt"
  std::string landmark;  // "Vance Exchange"
  std::string profile;   // "industry", or "" for a landmark with no port
  std::string note;      // the observation both bodies are scanned with
};

// The forty-seven rows of the reference's STAR_CATALOG, in its order. The order
// is load bearing: it decides each row's coordinates within its system
// (i % 3 and i % 2) and each system's outer-world profile (i % 4).
const std::vector<StarRow>& AuthoredStarCatalog();

// buildGalaxyContent(): systems, local bodies and ports from rows.
//
//   baseIndex  the row index the first row is treated as having. The reference
//              passes 0 for STAR_CATALOG; a later batch of rows continues the
//              count so its local charts differ from the first batch's.
//   linkHubs   join each region's first new system to its hub. Off when the
//              lanes are already in the catalogue, which is the case for the
//              rows (the export carries the lanes they produce) and
//              for a generated batch, whose lanes ChartExpansion draws.
void BuildGalaxyFromRows(const std::vector<StarRow>& rows, std::size_t baseIndex, Catalog& catalog,
                         bool linkHubs);

// addSystemBody(): the reference's one way of creating a local body - klass by
// type, System and Region rows, the observation plus the type's fixed reading,
// and the matching locked scan. Everything that adds a local body goes through
// here, so a generated world says what an one says.
Body& AddLocalBody(Catalog& catalog, const std::string& id, const std::string& type,
                   const std::string& name, double x, double y, const Body& system,
                   const std::string& text);

// expandLocalCharts(): an outer world and a moon for every system, from
// firstSystem onward. The reference runs it over every system with the system's
// position in the catalogue as the index, so a later batch starts where the
// earlier one stopped instead of re-using its names and positions.
void ExpandLocalCharts(Catalog& catalog, std::size_t firstSystem);

// True for a system the row pass had to create: STAR_CATALOG names six systems
// SECTOR_BODIES already holds (Kessler and the five with a hand-written klass),
// and creates the other forty-two. The export shows which is which - a created
// system carries only the "Charted bodies" row the pass gives it, while an
// one carries the designer's "Traffic" row too.
bool IsGeneratedSystem(const Body& system);

// Removes everything BuildGalaxyFromRows() and ExpandLocalCharts() compute: the
// forty-two created systems, every body that belongs to a system, and the ports
// on those bodies. The shell - regions, Kessler's six hand-written
// bodies and the two ports on them, the five systems with a designer's klass -
// is left alone, because the reference it too. This remains available
// as code, but ChartExpansion no longer calls it.
void StripGeneratedChart(Catalog& catalog);

}  // namespace ship

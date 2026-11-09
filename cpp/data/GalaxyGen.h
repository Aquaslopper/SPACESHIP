// ============================================================================
//  GalaxyGen.h — the generator, not its output.
//  ---------------------------------------------------------------------------
//  The reference does not ship a finished galaxy. Spaceship.hta lays out seven
//  regions and one authored system, then builds the rest at load time from a
//  table of forty-seven rows (STAR_CATALOG) and a second pass that gives every
//  system an outer world and a moon (expandLocalCharts). assets/data/chart.json
//  is the *result* of those two passes, exported once by tools/export-data.cjs.
//
//  The first port loaded the export and never ported the passes, so the sky was
//  a photograph of a galaxy rather than a galaxy. This header puts the two
//  passes back. They are transcribed from the reference, row for row:
//
//    * BuildGalaxyFromRows() is buildGalaxyContent(): for each row it creates
//      the system if it is not there yet, joins the first system of a region to
//      that region's hub, then adds three local bodies - an entry star, a world
//      (a belt when the name says Belt) and a landmark, which is a station and
//      a priced port when the row names a trade profile.
//    * ExpandLocalCharts() is expandLocalCharts(): system i gains an outer
//      world of profile i % 4 and a moon, and the system's summary strings are
//      rewritten to match.
//
//  Together they reproduce the exported chart exactly - bodies, names,
//  coordinates, klass strings, detail rows, scans, ports and lanes - which
//  cpp/tests/galaxy_gen_tests.cpp checks field by field. That check is what
//  lets the rest of the port treat generated and exported content as the same
//  thing, and it is what makes it honest to grow the chart: a generated system
//  is built by the same function that built the authored forty-seven.
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
  std::string region;    // "gal-gen-19"
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
//              authored rows (the export carries the lanes they produce) and
//              for a generated batch, whose lanes ChartExpansion draws.
void BuildGalaxyFromRows(const std::vector<StarRow>& rows, std::size_t baseIndex, Catalog& catalog,
                         bool linkHubs);

// addSystemBody(): the reference's one way of creating a local body - klass by
// type, System and Region rows, the observation plus the type's fixed reading,
// and the matching locked scan. Everything that adds a local body goes through
// here, so a generated world says what an authored one says.
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
// authored one carries the designer's "Traffic" row too.
bool IsGeneratedSystem(const Body& system);

// Removes everything BuildGalaxyFromRows() and ExpandLocalCharts() compute: the
// forty-two created systems, every body that belongs to a system, and the ports
// on those bodies. The authored shell - Kessler's six hand-written
// bodies and the two ports on them, the five systems with a designer's klass -
// is left alone. Galactic regions are built separately by ChartExpansion, which strips
// before it rebuilds, so a catalogue is always the product of the current
// generator rather than a mix of two.
void StripGeneratedChart(Catalog& catalog);

}  // namespace ship

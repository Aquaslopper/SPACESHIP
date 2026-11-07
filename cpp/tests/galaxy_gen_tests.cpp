// ============================================================================
//  galaxy_gen_tests.cpp — the generator, checked against its own output.
//  ---------------------------------------------------------------------------
//  assets/data/chart.json is what Spaceship.hta's buildGalaxyContent() and
//  expandLocalCharts() produced when tools/export-data.cjs ran them. The port
//  now runs those two passes itself (data/GalaxyGen.h). This file is the proof
//  that the ported passes are the reference's passes and not a lookalike:
//
//    * strip the rebuildable part of a loaded catalogue,
//    * run the two passes,
//    * and compare every system, body and port with the export, field by field.
//
//  If the two ever diverge, this test fails and names the field. That is what
//  lets ChartExpansion grow the chart from rows instead of shipping a frozen
//  picture of one.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include "data/Catalog.h"
#include "data/GalaxyGen.h"

namespace {

using ship::Body;
using ship::Catalog;

const Catalog& Frozen() {
  static const Catalog catalog = Catalog::LoadFromAssets(Catalog::DefaultAssetDir());
  return catalog;
}

std::string Key(double x, double y) {
  return std::to_string(static_cast<int>(x * 100.0)) + "," +
         std::to_string(static_cast<int>(y * 100.0));
}

const Body* Find(const std::vector<Body>& list, const std::string& id) {
  for (const Body& body : list) {
    if (body.id == id) { return &body; }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("The generator reproduces the exported chart exactly", "[galaxy][gen]") {
  Catalog rebuilt = Frozen();
  ship::StripGeneratedChart(rebuilt);
  ship::BuildGalaxyFromRows(ship::AuthoredStarCatalog(), 0, rebuilt, /*linkHubs=*/true);
  ship::ExpandLocalCharts(rebuilt, 0);

  const Catalog& frozen = Frozen();

  // ---- systems: same ids, same order, same fields -------------------------
  REQUIRE(rebuilt.systems.size() == frozen.systems.size());
  for (std::size_t i = 0; i < rebuilt.systems.size(); ++i) {
    const Body& got = rebuilt.systems[i];
    const Body& want = frozen.systems[i];
    INFO("system #" << i << " (" << want.id << ")");
    CHECK(got.id == want.id);
    CHECK(got.name == want.name);
    CHECK(got.type == want.type);
    CHECK(got.x == want.x);
    CHECK(got.y == want.y);
    CHECK(got.klass == want.klass);
    CHECK(got.system == want.system);
    CHECK(got.region == want.region);
    CHECK(got.entry == want.entry);
    CHECK(got.details == want.details);
    CHECK(got.scan == want.scan);
    CHECK(got.scanLocked == want.scanLocked);
  }

  // ---- local bodies: every exported body must be produced, identically -----
  REQUIRE(rebuilt.bodies.size() == frozen.bodies.size());
  std::map<std::string, const Body*> got;
  for (const Body& body : rebuilt.bodies) { got[body.id] = &body; }
  for (const Body& want : frozen.bodies) {
    INFO("body " << want.id);
    const auto it = got.find(want.id);
    REQUIRE(it != got.end());
    const Body& have = *it->second;
    CHECK(have.type == want.type);
    CHECK(have.name == want.name);
    CHECK(have.x == want.x);
    CHECK(have.y == want.y);
    CHECK(have.system == want.system);
    CHECK(have.klass == want.klass);
    CHECK(have.subtype == want.subtype);
    CHECK(have.details == want.details);
    CHECK(have.scan == want.scan);
    CHECK(have.scanLocked == want.scanLocked);
  }

  // ---- ports: same ids, names, prices and hints ---------------------------
  REQUIRE(rebuilt.ports.size() == frozen.ports.size());
  std::map<std::string, const ship::Port*> gotPorts;
  for (const ship::Port& port : rebuilt.ports) { gotPorts[port.id] = &port; }
  for (const ship::Port& want : frozen.ports) {
    INFO("port " << want.id);
    const auto it = gotPorts.find(want.id);
    REQUIRE(it != gotPorts.end());
    CHECK(it->second->name == want.name);
    CHECK(it->second->tip == want.tip);
    CHECK(it->second->prices == want.prices);
  }

  // ---- lanes: the region hub chains are real lanes too --------------------
  std::map<std::string, int> laneCount;
  for (const ship::Lane& lane : rebuilt.sectorLanes) {
    ++laneCount[lane.from + "|" + lane.to];
    ++laneCount[lane.to + "|" + lane.from];
  }
  for (const ship::Lane& lane : frozen.sectorLanes) {
    INFO("lane " << lane.from << " -> " << lane.to);
    CHECK(laneCount.count(lane.from + "|" + lane.to) == 1);
  }
  CHECK(rebuilt.sectorLanes.size() == frozen.sectorLanes.size());
}

TEST_CASE("The row table is the reference's, not a paraphrase", "[galaxy][gen]") {
  const std::vector<ship::StarRow>& rows = ship::AuthoredStarCatalog();
  REQUIRE(rows.size() == 47);  // STAR_CATALOG.length
  CHECK(rows.front().system == "sec-vance");
  CHECK(rows.front().region == "gal-inner");
  CHECK(rows.front().name == "Vance Cluster");
  CHECK(rows.front().x == 120);
  CHECK(rows.front().y == 70);
  CHECK(rows.front().profile == "industry");
  CHECK(rows.back().system == "sec-tern");

  // Seven regions, and every row names one of them.
  std::map<std::string, int> perRegion;
  for (const ship::StarRow& row : rows) { ++perRegion[row.region]; }
  CHECK(perRegion.size() == 7);
  CHECK(perRegion["gal-inner"] == 7);
}

TEST_CASE("Stripping removes only what the generator can rebuild", "[galaxy][gen]") {
  Catalog catalog = Frozen();
  const std::size_t systems = catalog.systems.size();
  const std::size_t bodies = catalog.bodies.size();
  const std::size_t ports = catalog.ports.size();

  ship::StripGeneratedChart(catalog);
  // Six systems were authored (Kessler and five with a designer's klass); the
  // forty-two the row pass creates are stripped and rebuilt with the rest.
  CHECK(catalog.systems.size() == 6);
  // The authored shell is Kessler's six hand-written bodies and its two ports.
  CHECK(catalog.bodies.size() == 6);
  CHECK(catalog.ports.size() == 2);
  for (const Body& body : catalog.bodies) { CHECK(body.system.empty()); }

  ship::BuildGalaxyFromRows(ship::AuthoredStarCatalog(), 0, catalog, true);
  ship::ExpandLocalCharts(catalog, 0);
  CHECK(catalog.systems.size() == systems);
  CHECK(catalog.bodies.size() == bodies);
  CHECK(catalog.ports.size() == ports);
}

TEST_CASE("A generated batch carries the reference's conventions", "[galaxy][gen]") {
  Catalog catalog = Frozen();
  ship::StripGeneratedChart(catalog);
  ship::BuildGalaxyFromRows(ship::AuthoredStarCatalog(), 0, catalog, true);
  ship::ExpandLocalCharts(catalog, 0);

  // One new row: the same passes must give it the same five local bodies and
  // the same summary strings an authored system has.
  ship::StarRow row;
  row.system = "sec-gen-1";
  row.region = "gal-core";
  row.name = "Test Verge";
  row.x = 10;
  row.y = 10;
  row.star = "Test Verge A";
  row.world = "Anvil Prime";
  row.landmark = "Test Quay";
  row.profile = "industry";
  row.note = "A quiet lane opens here.";

  const std::size_t firstNew = catalog.systems.size();
  ship::BuildGalaxyFromRows({row}, 47, catalog, false);
  ship::ExpandLocalCharts(catalog, firstNew);

  const Body& system = catalog.systems.back();
  CHECK(system.id == "sec-gen-1");
  CHECK(system.klass == "Five charted destinations");
  CHECK(system.entry == "gen-1-star");

  std::vector<std::string> local;
  for (const Body& body : catalog.bodies) {
    if (body.system == "sec-gen-1") { local.push_back(body.id); }
  }
  REQUIRE(local.size() == 5);
  CHECK(Find(catalog.bodies, "gen-1-star") != nullptr);
  CHECK(Find(catalog.bodies, "gen-1-world") != nullptr);
  CHECK(Find(catalog.bodies, "gen-1-port") != nullptr);
  CHECK(Find(catalog.bodies, "gen-1-outer") != nullptr);
  CHECK(Find(catalog.bodies, "gen-1-moon") != nullptr);

  // The port trades the profile's authored basket, and its tip is the row's
  // observation plus the profile's hint - PORT_PRICES row[8] and row[9] + " " +
  // PORT_HINTS[row[8]] in the reference.
  const ship::Port* port = nullptr;
  for (const ship::Port& candidate : catalog.ports) {
    if (candidate.id == "gen-1-port") { port = &candidate; }
  }
  REQUIRE(port != nullptr);
  CHECK(port->name == "Test Quay");
  CHECK(port->prices == catalog.portPrices.at("industry"));
  CHECK(port->tip == row.note + " " + catalog.portHints.at("industry"));

  // Positions are the reference's slots, keyed on the row index.
  const Body* star = Find(catalog.bodies, "gen-1-star");
  REQUIRE(star != nullptr);
  CHECK(star->x == 100 + (47 % 3) * 32);
  CHECK(star->y == 58 + (47 % 2) * 14);
}

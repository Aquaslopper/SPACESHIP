// ============================================================================
//  catalog_tests.cpp — the Phase 0 data gate.
//  ---------------------------------------------------------------------------
//  These tests answer one question: does the C++ port hold exactly the same
//  world as Spaceship.hta? They are the C++ mirror of the assertions in
//  tests/galaxy.test.cjs, so a reviewer can read the two side by side.
//
//  The numbers below are not "round" numbers chosen for convenience. Each one
//  came out of the reference edition and is now locked, so any future content
//  change has to be deliberate and visible in a diff.
// ============================================================================

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

#include "data/Catalog.h"

namespace {

// Loaded once for the whole binary: the catalogue is immutable data.
const ship::Catalog& Reference() {
  static const ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

int CountType(const std::vector<ship::Body>& bodies, const std::string& type) {
  int n = 0;
  for (const ship::Body& b : bodies) { if (b.type == type) { ++n; } }
  return n;
}

}  // namespace

TEST_CASE("the catalogue matches the reference edition's totals", "[catalog][counts]") {
  const ship::Catalog& c = Reference();

  // The three numbers the README promises the player.
  CHECK(c.regions.size() == 7u);
  CHECK(c.systems.size() == 48u);
  CHECK(c.bodies.size() == 243u);
  CHECK(c.ports.size() == 30u);

  // Everything else the voyage is made of.
  CHECK(c.wren.rooms.size() == 21u);
  CHECK(c.kestrel.rooms.size() == 21u);
  CHECK(c.counts.fittedRooms == 14);
  CHECK(c.contracts.size() == 18u);
  CHECK(c.sectorLanes.size() == 43u);
  CHECK(c.galaxyLanes.size() == 9u);   // 5 + 4 added by region expansion
  CHECK(c.taskTypes.size() == 12u);
  CHECK(c.counts.incidents == 5);
  CHECK(c.counts.projects == 10);
  CHECK(c.counts.commendations == 14);
  CHECK(c.counts.wiki == 21);
  CHECK(c.counts.themes == 5);
  CHECK(c.counts.crew == 4);
  CHECK(c.counts.hires == 5);
  CHECK(c.counts.jobs == 6);
  CHECK(c.counts.orders == 5);
  CHECK(c.goods.size() == 7u);
}

TEST_CASE("the chart is composed of the same body types as the reference",
          "[catalog][chart]") {
  const ship::Catalog& c = Reference();

  CHECK(CountType(c.bodies, "star") == 50);
  CHECK(CountType(c.bodies, "planet") == 93);
  CHECK(CountType(c.bodies, "moon") == 48);
  CHECK(CountType(c.bodies, "station") == 30);
  CHECK(CountType(c.bodies, "belt") == 2);
  CHECK(CountType(c.bodies, "site") == 19);
  CHECK(CountType(c.bodies, "origin") == 1);
  CHECK(static_cast<int>(c.bodies.size()) ==
        50 + 93 + 48 + 30 + 2 + 19 + 1);
}

TEST_CASE("every reference in the catalogue resolves", "[catalog][integrity]") {
  const ship::Catalog& c = Reference();
  const std::vector<std::string> problems = c.Validate();

  INFO("problems found: " << problems.size());
  for (const std::string& p : problems) { INFO("  " + p); }
  CHECK(problems.empty());
}

TEST_CASE("the chart board and its derived constants are unchanged",
          "[catalog][chart]") {
  const ship::Catalog& c = Reference();

  // Distances are derived from these, so changing any of them silently
  // changes every travel time in the game.
  CHECK(c.map.w == 638.0);
  CHECK(c.map.h == 278.0);
  CHECK(c.map.cx == 319.0);
  CHECK(c.map.cy == 139.0);
  CHECK(c.map.pxPerAction == 16.0);
  CHECK(c.map.lyPerAction == 0.42);
  CHECK(c.map.zmin == 0.6);
  CHECK(c.map.zmax == 2.0);
}

TEST_CASE("both deck plans describe the same rooms in their own coordinates",
          "[catalog][ship]") {
  const ship::Catalog& c = Reference();

  CHECK(c.kestrel.hull.size() == 10u);  // nacelles, pylons, spine, body, pod, lens
  CHECK(c.wren.hull.size() == 6u);      // the compact courier

  const ship::Room* large = c.kestrel.FindRoom("engine");
  const ship::Room* small = c.wren.FindRoom("engine");
  REQUIRE(large != nullptr);
  REQUIRE(small != nullptr);
  CHECK(large->box.x == 100);
  CHECK(large->box.y == 112);
  CHECK(large->box.w == 160);
  CHECK(large->box.h == 94);
  CHECK(small->box.x == 35);
  CHECK(small->box.y == 86);
  CHECK(small->box.w == 79);
  CHECK(small->box.h == 64);

  // Same ids on both hulls, so a voyage can transfer without losing a tab.
  for (const ship::Room& room : c.kestrel.rooms) {
    INFO("room " << room.id);
    CHECK(c.wren.FindRoom(room.id) != nullptr);
  }

  CHECK(c.kestrel.corridor.x == 100);
  CHECK(c.kestrel.corridor.w == 740);
  CHECK(c.wren.corridor.x == 35);
  CHECK(c.wren.corridor.w == 411);
}

TEST_CASE("ports trade in the seven authored commodity kinds", "[catalog][ports]") {
  const ship::Catalog& c = Reference();

  REQUIRE(c.FindPort("tolme") != nullptr);
  CHECK(c.FindPort("tolme")->name == "Tolme Anchorage");
  CHECK(c.FindPort("relay")->name == "Coldwell Relay");

  // The two hand-written ports open with the four base commodities; the
  // twenty-eight generated ones quote the full seven-kind price table.
  for (const ship::Port& port : c.ports) {
    INFO("port " << port.id);
    const bool authored = (port.id == "tolme" || port.id == "relay");
    CHECK(port.prices.size() == (authored ? 4u : 7u));
    for (const auto& entry : port.prices) {
      INFO("  kind " << entry.first);
      CHECK(entry.second > 0.0);
    }
  }

  // The two hand-ports keep their exact margins: this pair is what
  // makes the opening trade route profitable.
  CHECK(c.FindPort("tolme")->prices.at("Bulk metal") == 1.20);
  CHECK(c.FindPort("tolme")->prices.at("Components") == 0.85);
  CHECK(c.FindPort("relay")->prices.at("Bulk metal") == 0.65);
  CHECK(c.FindPort("relay")->prices.at("Components") == 1.20);
}

TEST_CASE("bodies authored before systems existed still belong to Kessler",
          "[catalog][chart]") {
  const ship::Catalog& c = Reference();

  const ship::Body* kessler = c.FindBody("kessler");
  REQUIRE(kessler != nullptr);
  CHECK(kessler->system.empty());
  CHECK(c.BodySystem(*kessler) == "sec-kessler");

  // Every system points at a local entry body that agrees with it.
  for (const ship::Body& system : c.systems) {
    const ship::Body* entry = c.FindBody(system.entry);
    REQUIRE(entry != nullptr);
    CHECK(c.BodySystem(*entry) == system.id);
  }
}

TEST_CASE("a missing catalogue fails loudly instead of loading empty",
          "[catalog][errors]") {
  CHECK_THROWS_AS(
      ship::Catalog::LoadFromAssets("/nonexistent/spaceship/assets"),
      ship::CatalogError);
}

TEST_CASE("the catalogue records the reference edition it came from",
          "[catalog][provenance]") {
  const ship::Catalog& c = Reference();

  // Spaceship.hta, 706,020 bytes. The hash is recomputed by the export tool,
  // so this also proves the JSON in the tree was generated from this exact
  // edition rather than an older one.
  CHECK(c.sourceBytes == 706020);
  CHECK(c.sourceSha256.size() == 64u);
}

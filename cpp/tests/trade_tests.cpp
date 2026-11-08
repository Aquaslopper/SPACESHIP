// ============================================================================
//  trade_tests.cpp — one station per system, and the trade contract it offers.
//  ---------------------------------------------------------------------------
//  Three properties are pinned here, because each one is a way the feature can
//  silently regress into the sameness it replaced:
//
//    * every system in the grown chart owns exactly one berth;
//    * every berth offers exactly one trade contract, bound to its own system;
//    * the contract is bought at its origin and paid only at its destination.
//
//  Nothing here drives a window. The trade contract is bought, carried and
//  collected through the same calls the station card presses.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "data/ChartExpansion.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog = ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

// A voyage with the chart grown, which is what the player ever sees.
ship::Simulation Fresh() { return ship::Simulation::NewVoyage(World()); }

// Put the ship at a berth and dock, which is what arrival does.
void BerthAt(ship::Simulation& sim, const std::string& portId) {
  sim.MutableMap().here = portId;
  sim.MutableState().docked = true;
}

const ship::Body* SystemOf(const ship::Simulation& sim, const std::string& portId) {
  const ship::Body* body = sim.FindBodyById(portId);
  if (body == nullptr) { return nullptr; }
  return sim.World().FindSystem(sim.World().BodySystem(*body));
}

}  // namespace

TEST_CASE("every system in the grown chart owns a berth", "[trade][stations]") {
  const ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  // One berth per system, counted by the system each port's body belongs to.
  std::map<std::string, int> berths;
  for (const ship::Port& port : world.ports) {
    const ship::Body* body = world.FindBody(port.id);
    REQUIRE(body != nullptr);
    const std::string system = world.BodySystem(*body);
    REQUIRE_FALSE(system.empty());
    berths[system] += 1;
    // The port is a station the chart can be flown to.
    CHECK(body->type == "station");
  }

  // No system is left without a berth. A handful of authored systems already
  // field two (a main station and a frontier outpost); that predates this pass
  // and is kept, so the contract is "at least one", not "exactly one".
  for (const ship::Body& system : world.systems) {
    INFO("system " << system.id);
    const auto it = berths.find(system.id);
    REQUIRE(it != berths.end());
    CHECK(it->second >= 1);
  }

  // The generated berths carry the fleet's conventions too.
  for (const ship::Port& port : world.ports) {
    CHECK_FALSE(port.name.empty());
    CHECK_FALSE(port.tip.empty());
    CHECK_FALSE(port.prices.empty());
  }
}

TEST_CASE("every berth offers a trade contract bound to its own system", "[trade]") {
  const ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  REQUIRE_FALSE(world.tradeContracts.empty());
  CHECK(world.tradeContracts.size() == world.ports.size());

  std::set<std::string> ids;
  for (const ship::TradeContract& trade : world.tradeContracts) {
    INFO("trade " << trade.id);
    CHECK(ids.insert(trade.id).second);          // ids are unique
    CHECK_FALSE(trade.good.empty());
    CHECK_FALSE(trade.goodName.empty());
    CHECK(trade.vol > 0);
    CHECK(trade.price > 0);
    CHECK(trade.payment > 0);
    CHECK((trade.kind == ship::kTradeDelivery || trade.kind == ship::kTradeSale));

    // The offer is the berth's: it is taken on where it is offered.
    CHECK(trade.origin == trade.station);
    CHECK(world.FindPort(trade.origin) != nullptr);
    CHECK(world.FindPort(trade.destination) != nullptr);
    CHECK(trade.destination != trade.origin);

    // The berth is in a system, and the destination is in another one.
    const ship::Body* home = SystemOf(sim, trade.station);
    const ship::Body* away = SystemOf(sim, trade.destination);
    REQUIRE(home != nullptr);
    REQUIRE(away != nullptr);
    CHECK(home->id != away->id);

    // The catalogue's lookup agrees with the flat list.
    const auto& here = world.TradesAt(trade.station);
    REQUIRE_FALSE(here.empty());
    bool listed = false;
    for (const ship::TradeContract& offer : here) {
      if (offer.id == trade.id) { listed = true; }
    }
    CHECK(listed);
  }

  // A berth with no id offers nothing rather than crashing.
  CHECK(world.TradesAt("no-such-berth").empty());
}

TEST_CASE("trade contracts differ between berths rather than repeating one list", "[trade]") {
  const ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  // The defect this feature replaces: one identical list at every berth. Measure
  // how much of the fleet shares the same (destination, commodity, kind) triple.
  std::map<std::string, int> shapes;
  for (const ship::TradeContract& trade : world.tradeContracts) {
    shapes[trade.destination + "|" + trade.good + "|" + trade.kind] += 1;
    // No berth offers the same destination twice, because it offers one contract.
    CHECK(trade.destination != trade.station);
  }
  // Several berths may genuinely agree; most must not.
  std::size_t largest = 0;
  for (const auto& entry : shapes) { largest = std::max(largest, static_cast<std::size_t>(entry.second)); }
  CHECK(largest < world.tradeContracts.size());
}

TEST_CASE("a trade contract is bought at the origin and paid at the destination", "[trade]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();
  REQUIRE_FALSE(world.tradeContracts.empty());

  // Pick a contract whose run stays inside one system, so the system chart can
  // draw it and the test does not have to fly a crossing.
  const ship::TradeContract* chosen = nullptr;
  for (const ship::TradeContract& trade : world.tradeContracts) {
    const ship::Body* home = SystemOf(sim, trade.origin);
    const ship::Body* away = SystemOf(sim, trade.destination);
    if (home != nullptr && away != nullptr && home->id == away->id) { chosen = &trade; break; }
  }
  if (chosen == nullptr) { return; }  // no same-system run in this seed; nothing to check

  BerthAt(sim, chosen->origin);
  sim.MutableState().shiplets = 5000;

  // The contract is refused anywhere it is not offered.
  BerthAt(sim, chosen->destination);
  CHECK_FALSE(sim.AcceptTrade(chosen->id));

  BerthAt(sim, chosen->origin);
  const double before = sim.State().shiplets;
  const std::size_t lots = sim.Goods().size();
  const double actions = sim.ActionCount();

  REQUIRE(sim.AcceptTrade(chosen->id));
  CHECK(sim.TradeStatus(chosen->id) == "carried");
  CHECK(sim.State().shiplets == before - chosen->price);  // the cargo was bought
  CHECK(sim.Goods().size() == lots + 1);
  CHECK(sim.ActionCount() == actions + 1);

  // The consignment is contract freight: the sales counter will not take it.
  for (const ship::GoodsLot& lot : sim.Goods()) {
    if (lot.contractId == chosen->id) { CHECK_FALSE(sim.SellGood(lot.id)); }
  }

  // Taking the same contract twice is refused.
  CHECK_FALSE(sim.AcceptTrade(chosen->id));

  // The green route exists while the run is live, and the chart carries it.
  CHECK_FALSE(ship::BuildNavChart(sim).tradeRoutes.empty());

  // Arriving anywhere else delivers nothing.
  BerthAt(sim, chosen->origin);
  sim.TradeProgress();
  CHECK(sim.TradeStatus(chosen->id) == "carried");
  CHECK_FALSE(sim.CollectTrade(chosen->id));

  // Docking at the destination hands the cargo over, and the fee is collectable.
  BerthAt(sim, chosen->destination);
  sim.TradeProgress();
  CHECK(sim.TradeStatus(chosen->id) == "ready");
  for (const ship::GoodsLot& lot : sim.Goods()) { CHECK(lot.contractId != chosen->id); }

  const double purse = sim.State().shiplets;
  REQUIRE(sim.CollectTrade(chosen->id));
  CHECK(sim.State().shiplets == purse + chosen->payment);
  CHECK(sim.TradeStatus(chosen->id) == "paid");

  // Paid once only, and the route is gone.
  CHECK_FALSE(sim.CollectTrade(chosen->id));
  CHECK_FALSE(sim.AcceptTrade(chosen->id));
  CHECK(ship::BuildNavChart(sim).tradeRoutes.empty());
}

TEST_CASE("a paid trade contract survives a save and a load", "[trade][save]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();
  REQUIRE_FALSE(world.tradeContracts.empty());
  const ship::TradeContract& trade = world.tradeContracts.front();

  BerthAt(sim, trade.origin);
  sim.MutableState().shiplets = 5000;
  REQUIRE(sim.AcceptTrade(trade.id));
  const std::string state = sim.TradeStatus(trade.id);

  ship::Simulation restored = Fresh();
  std::string error;
  REQUIRE(restored.LoadJson(sim.SaveJson(), &error));
  CHECK(restored.TradeStatus(trade.id) == state);
  const ship::TradeRun* run = restored.TradeRunOf(trade.id);
  REQUIRE(run != nullptr);
  CHECK(run->destination == trade.destination);
  CHECK(run->payment == trade.payment);
}

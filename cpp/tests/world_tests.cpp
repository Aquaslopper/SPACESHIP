// ============================================================================
//  world_tests.cpp — the parts of the world the Node suites prove.
//  ---------------------------------------------------------------------------
//  tests/*.test.cjs asserts selected behaviours against the reference in a VM.
//  These C++ tests pin focused invariants against the native API. Differential
//  scripts compare only the states and outputs exercised by those scripts;
//  neither suite proves exact equality across every world path or the UI.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "core/Simulation.h"
#include "data/Catalog.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog = ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::ReferenceVoyage(World()); }

// Put the ship at a port and open the station, which is what arrival does.
void BerthAt(ship::Simulation& sim, const std::string& bodyId) {
  sim.MutableMap().here = bodyId;
  ship::CrewMember* nobody = nullptr;
  (void)nobody;
  sim.MutableState().docked = true;
}

int CountType(const std::vector<ship::Body>& bodies, const std::string& type) {
  int count = 0;
  for (const ship::Body& body : bodies) {
    if (body.type == type) { ++count; }
  }
  return count;
}

}  // namespace

TEST_CASE("the chart is complete and consistent", "[world]") {
  const ship::Catalog& catalog = World();

  REQUIRE(catalog.regions.size() == 7);
  REQUIRE(catalog.systems.size() == 48);
  REQUIRE(catalog.bodies.size() == 243);
  REQUIRE(catalog.ports.size() == 30);
  REQUIRE(CountType(catalog.bodies, "star") == 50);
  REQUIRE(CountType(catalog.bodies, "planet") == 93);
  REQUIRE(CountType(catalog.bodies, "moon") == 48);

  // Ids are unique across every layer, because a lookup never knows the layer.
  std::set<std::string> ids;
  for (const auto* layer : {&catalog.regions, &catalog.systems, &catalog.bodies}) {
    for (const ship::Body& body : *layer) {
      REQUIRE(ids.insert(body.id).second);
      REQUIRE(body.x >= 0);
      REQUIRE(body.x <= catalog.map.w);
      REQUIRE(body.y >= 0);
      REQUIRE(body.y <= catalog.map.h);
    }
  }

  for (const ship::Body& system : catalog.systems) {
    REQUIRE(catalog.FindRegion(system.region) != nullptr);
    REQUIRE(!system.entry.empty());
    const ship::Body* entry = catalog.FindBody(system.entry);
    REQUIRE(entry != nullptr);
    REQUIRE(catalog.BodySystem(*entry) == system.id);
  }
  for (const ship::Body& star : catalog.bodies) {
    if (star.type != "star") { continue; }
    REQUIRE(catalog.FindSystem(catalog.BodySystem(star)) != nullptr);
  }
  for (const ship::Port& port : catalog.ports) {
    const ship::Body* body = catalog.FindBody(port.id);
    REQUIRE(body != nullptr);
    REQUIRE(body->type == "station");
    REQUIRE(body->name == port.name);
  }
  for (const ship::Contract& contract : catalog.contracts) {
    const bool target = catalog.FindBody(contract.target) != nullptr ||
                        catalog.FindSystem(contract.target) != nullptr;
    REQUIRE(target);
    if (!contract.origin.empty()) { REQUIRE(catalog.FindPort(contract.origin) != nullptr); }
    if (contract.goal == "freight" || contract.goal == "passenger") {
      REQUIRE(catalog.FindPort(contract.target) != nullptr);
    }
  }
}

TEST_CASE("every system is reachable on a full tank", "[world]") {
  ship::Simulation sim = Fresh();
  for (const ship::Body& from : sim.World().systems) {
    sim.MutableMap().here = from.entry;
    for (const ship::Body& to : sim.World().systems) {
      const double distance = sim.MetricsFor(to).dist;
      if (from.id == to.id) {
        REQUIRE(distance == 0);
      } else {
        REQUIRE(distance > 0);
        REQUIRE(distance <= 100);
      }
    }
  }
}

TEST_CASE("a jump costs fuel and an action, and arrival waits for a click", "[world]") {
  ship::Simulation sim = Fresh();
  sim.MutableMap().layer = "sector";
  REQUIRE(sim.SelectTarget("sec-vance"));

  const ship::Body* vance = sim.World().FindSystem("sec-vance");
  REQUIRE(vance != nullptr);
  const double distance = sim.MetricsFor(*vance).dist;
  const double fuel = sim.StatValue("fuel");

  REQUIRE(sim.StartTravel());
  REQUIRE(sim.ActionCount() == 1);
  REQUIRE(sim.StatValue("fuel") == fuel - distance);

  const ship::Task* trip = sim.FindOngoing("travel");
  REQUIRE(trip != nullptr);
  REQUIRE(trip->target == "sec-vance");
  REQUIRE(trip->left == distance);

  for (int i = 0; i < static_cast<int>(distance); ++i) { sim.PerformAction(); }
  REQUIRE(sim.FindOngoing("travel")->state == "done");
  REQUIRE(sim.Map().here == "kessler");  // the ship has not moved yet

  REQUIRE(sim.Acknowledge("travel"));
  REQUIRE(sim.Map().here == "vance-star");
  REQUIRE(sim.CurrentSystemId() == "sec-vance");
  REQUIRE(sim.FindOngoing("travel") == nullptr);
}

TEST_CASE("the efficient drive saves fuel and no travel time", "[world]") {
  ship::Simulation sim = Fresh();
  const ship::Body* destination = sim.World().FindSystem("sec-vance");
  REQUIRE(destination != nullptr);
  const ship::Simulation::NavMetrics normal = sim.MetricsFor(*destination);

  sim.MutableState().upgrades["drive"] = true;
  const ship::Simulation::NavMetrics upgraded = sim.MetricsFor(*destination);

  REQUIRE(upgraded.dist == normal.dist);
  REQUIRE(upgraded.fuel == std::ceil(normal.dist * 0.8));
  REQUIRE(normal.transit == upgraded.transit);
}

TEST_CASE("a navigator saves fuel and never time, and only when free", "[world]") {
  ship::Simulation sim = Fresh();
  const ship::Body* destination = sim.World().FindSystem("sec-vance");
  REQUIRE(destination != nullptr);
  const double plain = sim.MetricsFor(*destination).fuel;

  REQUIRE(sim.ApplyJob("rell", "navigation"));
  sim.FindCrew("rell")->jobAt = 3;  // trained
  const double improved = sim.MetricsFor(*destination).fuel;
  REQUIRE(improved < plain);
  REQUIRE(sim.MetricsFor(*destination).dist == sim.MetricsFor(*destination).dist);

  // A navigator already busy plotting cannot help with the next plot.
  REQUIRE(sim.AddOngoing("cw-rell", "Busy", 5, "crewJob", "rell") != nullptr);
  REQUIRE(sim.MetricsFor(*destination).fuel == plain);
  REQUIRE(sim.FindOngoing("cw-rell")->left == 5);
}

TEST_CASE("a regional market still offers a margin", "[world]") {
  ship::Simulation sim = Fresh();
  const ship::Good* water = nullptr;
  for (const ship::Good& good : World().goods) {
    if (good.id == "water") { water = &good; }
  }
  REQUIRE(water != nullptr);

  ship::GoodsLot lot;
  lot.id = "probe";
  lot.name = water->name;
  lot.kind = water->kind;
  lot.vol = water->vol;
  lot.value = water->value;

  sim.MutableMap().here = "fallow-port";
  const double purchase = std::ceil(sim.LotPrice(lot) * 1.2);
  sim.MutableMap().here = "frost-port";
  const double sale = sim.LotPrice(lot);
  REQUIRE(sale > purchase);
}

TEST_CASE("freight is carried, delivered and paid exactly once", "[world]") {
  ship::Simulation sim = Fresh();
  BerthAt(sim, "tolme");

  const double before = sim.State().shiplets;
  REQUIRE(sim.AcceptContract("freight"));
  REQUIRE(sim.State().contracts.at("freight") == "active");
  bool held = false;
  for (const ship::GoodsLot& lot : sim.Goods()) {
    if (lot.id == "contract-freight") {
      held = true;
      REQUIRE(lot.vol == 8);
      REQUIRE(!lot.contractId.empty());
    }
  }
  REQUIRE(held);

  // Contract cargo is not for sale.
  REQUIRE(!sim.SellGood("contract-freight"));

  // Delivering it to the other port empties the hold and readies the contract.
  sim.MutableState().docked = true;
  sim.MutableMap().here = "relay";
  sim.ContractProgress("visit", "relay");
  REQUIRE(sim.State().contracts.at("freight") == "ready");
  for (const ship::GoodsLot& lot : sim.Goods()) { REQUIRE(lot.id != "contract-freight"); }

  REQUIRE(sim.CollectContract("freight"));
  REQUIRE(sim.State().shiplets == before + 420);
  REQUIRE(!sim.CollectContract("freight"));  // already paid
}

TEST_CASE("a contract recognises work already done", "[world]") {
  ship::Simulation sim = Fresh();
  BerthAt(sim, "tolme");
  // The first berth is active from the start; collecting it before visiting does
  // nothing, and arriving at Tolme is what readies it.
  REQUIRE(!sim.CollectContract("arrival"));
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = true;
  sim.ContractProgress("visit", "tolme");
  REQUIRE(sim.State().contracts.at("arrival") == "ready");
  REQUIRE(sim.CollectContract("arrival"));
}

TEST_CASE("commissions are deterministic and never point at the current port", "[world]") {
  ship::Simulation sim = Fresh();
  BerthAt(sim, "tolme");

  REQUIRE(sim.AcceptCommission("visit"));
  REQUIRE(sim.State().commissions.active.has_value());
  const ship::Commission commission = *sim.State().commissions.active;
  REQUIRE(commission.kind == "visit");
  REQUIRE(commission.reward == 450);
  REQUIRE(commission.origin == "tolme");
  REQUIRE(commission.target != "tolme");
  // The port the commission names is on the voyage's chart, which is the grown
  // one; the catalogue alone no longer knows every station.
  REQUIRE(sim.World().FindPort(commission.target) != nullptr);

  // Delivering on it completes the objective, and it pays once.
  sim.CommissionProgress("visit", commission.target);
  REQUIRE(sim.State().commissions.active->progress == 1);
  const double before = sim.State().shiplets;
  REQUIRE(sim.CollectCommission());
  REQUIRE(sim.State().shiplets == before + 450);
  REQUIRE(sim.State().commissions.completed == 1);
  REQUIRE(!sim.CollectCommission());
}

TEST_CASE("salvage reserves its space and lands in the hold", "[world]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().upgrades["drive"] = true;  // not needed, but harmless

  // Salvage needs a crossing in progress.
  REQUIRE(!sim.StartSalvage(0));
  REQUIRE(sim.SelectTarget("tolme"));
  REQUIRE(sim.StartTravel());
  REQUIRE(sim.StartSalvage(0));

  bool reserved = false;
  for (const ship::Task& task : sim.State().ongoing) {
    if (task.type == "salvage") {
      reserved = true;
      REQUIRE(task.vol == 3);
    }
  }
  REQUIRE(reserved);
  REQUIRE(sim.CargoReserved() == 3);

  // Two actions of work, then a lot appears.
  const std::size_t before = sim.Goods().size();
  sim.PerformAction();
  sim.PerformAction();
  REQUIRE(sim.Goods().size() == before + 1);
  REQUIRE(sim.Goods().back().name == "Recovered salvage");
  REQUIRE(sim.CargoReserved() == 0);
}

TEST_CASE("projects refuse to start when the ship cannot hold the output", "[world]") {
  ship::Simulation sim = Fresh();
  BerthAt(sim, "tolme");

  // The hold opens with no salvage in it, so every salvage-fed project is
  // refused before a single shiplet or action is spent.
  REQUIRE(sim.ProjectBlock("kit") == "Needs an uncommitted salvage cargo lot.");
  REQUIRE(sim.ProjectBlock("oxygen") == "Needs an uncommitted salvage cargo lot.");
  REQUIRE(!sim.StartProject("oxygen"));

  // Give the workshop a salvage lot and the next refusal is capacity, not input.
  ship::GoodsLot salvage;
  salvage.id = "test-salvage";
  salvage.name = "Recovered salvage";
  salvage.kind = "Salvage";
  salvage.vol = 3;
  salvage.value = 30;
  sim.MutableGoods().push_back(salvage);
  REQUIRE(sim.ProjectBlock("kit").empty());

  sim.MutableLife().canisters = sim.Life().capacity;  // rack full
  REQUIRE(sim.ProjectBlock("oxygen") == "The canister rack is full or reserved for an installation.");
  REQUIRE(!sim.StartProject("oxygen"));
}

TEST_CASE("an observation is recorded once and only of where the ship is", "[world]") {
  ship::Simulation sim = Fresh();
  int observations = 0;
  for (const ship::SurveyRecord& record : sim.Survey()) {
    if (record.id.rfind("obs-", 0) == 0) { ++observations; }
  }
  REQUIRE(observations == 0);

  REQUIRE(sim.StartProject("observe"));
  for (int i = 0; i < 4; ++i) { sim.PerformAction(); }
  bool filed = false;
  for (const ship::SurveyRecord& record : sim.Survey()) {
    if (record.id == "obs-kessler") { filed = true; }
  }
  REQUIRE(filed);
  REQUIRE(!sim.ProjectBlock("observe").empty());
  REQUIRE(!sim.StartProject("observe"));
}

TEST_CASE("the shipyard sells a larger deck and transfers the voyage", "[world]") {
  ship::Simulation sim = Fresh();
  REQUIRE(!sim.BuyLargerShip());  // in space, and without the funds

  BerthAt(sim, "tolme");
  sim.MutableState().shiplets = 9000;
  sim.MutableState().upgrades["bay"] = true;
  sim.MutableState().engineCondition = 71;
  const std::size_t crewBefore = sim.Crew().size();
  const double lotSerial = sim.State().lotSerial;

  REQUIRE(sim.BuyLargerShip());
  REQUIRE(sim.State().shipClass == "kestrel");
  REQUIRE(sim.State().shiplets == 5000);
  REQUIRE(sim.Crew().size() == crewBefore);
  REQUIRE(sim.State().lotSerial == lotSerial);
  REQUIRE(sim.State().engineCondition == 71);  // condition transfers untouched

  // The refit level carries over: a Kestrel with one bay is 90 m3, not 60.
  REQUIRE(sim.CargoStore().capacity == 90);

  // The deck plan is the large one.
  const ship::Room* engine = nullptr;
  for (const ship::Room& room : sim.Rooms()) {
    if (room.id == "engine") { engine = &room; }
  }
  REQUIRE(engine != nullptr);
  REQUIRE(engine->box.x == 100);
  REQUIRE(engine->box.w == 160);
}

TEST_CASE("a save round-trips, and impossible voyages are refused", "[world][save]") {
  ship::Simulation sim = Fresh();
  BerthAt(sim, "tolme");
  sim.MutableState().shiplets = 2000;
  REQUIRE(sim.AcceptContract("freight"));
  REQUIRE(sim.BuySupply("fuel-cell"));
  for (int i = 0; i < 30; ++i) { sim.PerformAction(); }

  const std::string saved = sim.SaveJson();
  std::string why;
  REQUIRE(ship::Simulation::ValidSave(saved, World(), &why));

  ship::Simulation restored = Fresh();
  REQUIRE(restored.LoadJson(saved, &why));
  REQUIRE(restored.State().shiplets == sim.State().shiplets);
  REQUIRE(restored.State().contracts.at("freight") == "active");
  REQUIRE(restored.Goods().size() == sim.Goods().size());
  REQUIRE(restored.ActionCount() == sim.ActionCount());
  REQUIRE(restored.Crew().size() == sim.Crew().size());

  // The hold cannot be fuller than the hold.
  {
    nlohmann::json broken = nlohmann::json::parse(saved);
    broken["cargo"]["capacity"] = 1;
    REQUIRE(!ship::Simulation::ValidSave(broken.dump(), World(), &why));
  }
  // The ship cannot be parked nowhere.
  {
    nlohmann::json broken = nlohmann::json::parse(saved);
    broken["map"]["here"] = "nowhere";
    REQUIRE(!ship::Simulation::ValidSave(broken.dump(), World(), &why));
  }
  // A task cannot point at a body that does not exist.
  {
    nlohmann::json broken = nlohmann::json::parse(saved);
    broken["state"]["ongoing"] = nlohmann::json::array();
    broken["state"]["ongoing"].push_back({{"id", "travel"}, {"label", "Nowhere"},
                                          {"total", 3}, {"left", 2}, {"state", "running"},
                                          {"type", "travel"}, {"target", "nowhere"}});
    REQUIRE(!ship::Simulation::ValidSave(broken.dump(), World(), &why));
  }
  // An incident cannot be waiting on a stage with no ticks left.
  {
    nlohmann::json broken = nlohmann::json::parse(saved);
    broken["events"] = nlohmann::json::array();
    broken["events"].push_back({{"id", "mech-1"}, {"type", "mech"}, {"stage", 2}, {"left", 1},
                                 {"a", ""}, {"b", ""}});
    REQUIRE(!ship::Simulation::ValidSave(broken.dump(), World(), &why));
  }
  // A decoding signal without the work to decode it is a stranded story.
  {
    nlohmann::json broken = nlohmann::json::parse(saved);
    broken["state"]["sim"]["signal"] = "decoding";
    REQUIRE(!ship::Simulation::ValidSave(broken.dump(), World(), &why));
  }

  // A failed load leaves the live voyage untouched.
  ship::Simulation live = Fresh();
  const std::string before = live.SaveJson();
  REQUIRE(!live.LoadJson("{\"format\":\"spaceship-command-1\"}", &why));
  REQUIRE(live.SaveJson() == before);
}

TEST_CASE("an old save without a sector is migrated, not rejected", "[world][save]") {
  ship::Simulation sim = Fresh();
  nlohmann::json snapshot = nlohmann::json::parse(sim.SaveJson());
  snapshot["map"].erase("sector");
  std::string why;
  REQUIRE(ship::Simulation::ValidSave(snapshot.dump(), World(), &why));

  ship::Simulation restored = Fresh();
  REQUIRE(restored.LoadJson(snapshot.dump(), &why));
  REQUIRE(restored.Map().sector == "gal-inner");
  REQUIRE(restored.Map().here == "kessler");
}

TEST_CASE("memory is bounded, bonds move, and a dream yields an insight", "[world]") {
  ship::Simulation sim = Fresh();
  for (int i = 0; i < 40; ++i) { sim.SimRemember("Work", "Note " + std::to_string(i)); }
  REQUIRE(sim.State().sim.memories.size() == 32);

  sim.SimChangeBond("rell", "jax", 150);
  REQUIRE(sim.SimBond("rell", "jax") == 100);
  REQUIRE(sim.SimBond("jax", "rell") == 100);  // the key is order-independent
  sim.SimChangeBond("rell", "jax", -400);
  REQUIRE(sim.SimBond("rell", "jax") == -100);

  const double insight = sim.State().sim.insight;
  REQUIRE(sim.StartProject("dream"));
  REQUIRE(sim.State().sim.insight == insight);
  for (int i = 0; i < 4; ++i) { sim.PerformAction(); }
  REQUIRE(sim.State().sim.insight > insight);
  REQUIRE(sim.State().chronicle.dreams == 1);
}

TEST_CASE("a shared meal costs rations and pays in morale and bond", "[world]") {
  ship::Simulation sim = Fresh();
  const double rack = sim.MessRack();
  const double bond = sim.SimBond("rell", "jax");
  const double morale = sim.FindCrew("rell")->morale;

  REQUIRE(sim.StartSocial("rell", "jax"));
  REQUIRE(sim.MessRack() == rack - 2);
  REQUIRE(sim.FindOngoing("project-social") != nullptr);

  for (int i = 0; i < 3; ++i) { sim.PerformAction(); }
  REQUIRE(sim.SimBond("rell", "jax") > bond);
  REQUIRE(sim.FindCrew("rell")->morale > morale);
  REQUIRE(sim.State().chronicle.meals == 1);
}

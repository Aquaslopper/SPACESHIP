#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <set>
#include <string>
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/ShipDiagram.h"
#include "ui/RoomsScreen.h"
#include "ui/Shell.h"
#include "ui/StationScreen.h"
#include "ui/NavScreen.h"
#include "ui/Widgets.h"

namespace {
ship::Catalog& World() {
  static auto world = ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return world;
}
ship::Simulation Starter() { return ship::Simulation::NewVoyage(World()); }
void Dock(ship::Simulation& sim) {
  sim.MutableMap().here = World().ports.front().id;
  sim.MutableState().docked = true;
}
}

TEST_CASE("a new voyage starts with only standard rooms and a usable 20 m3 hold", "[starter]") {
  auto sim = Starter();
  const std::set<std::string> expected = {"engine", "o2", "quarters", "mess", "cargo", "bridge"};
  std::set<std::string> actual;
  for (const auto& room : sim.Rooms()) actual.insert(room.id);
  CHECK(actual == expected);
  CHECK(sim.CargoStore().capacity == 20);
  CHECK(sim.CargoUsed() <= sim.CargoStore().capacity - 6);
  CHECK(sim.Goods().size() < World().startingCargo.size());
  CHECK(ship::OpenTask(sim, "nav", "main", true));
  CHECK(ship::OpenTask(sim, "room", "nav", true));
  CHECK_FALSE(ship::OpenTask(sim, "room", "science", true));
  CHECK_FALSE(ship::RoomHasScreen(sim, "simulacron"));
  CHECK_FALSE(sim.StartProject("observe"));
  CHECK(sim.ProjectBlock("observe").find("required room") != std::string::npos);
  CHECK_FALSE(sim.StartMedical("jax"));
  CHECK_FALSE(sim.StartSocial("jax", "bo"));
  CHECK_FALSE(sim.BroadcastLocal());
  ship::FxRng rng;
  const auto diagram = ship::BuildDiagram(sim, {0, 0, 600, 365}, false, rng);
  CHECK(diagram.rooms.size() == expected.size());
  for (const auto& room : diagram.rooms) CHECK(expected.count(room.id) == 1);
  std::string error;
  auto restored = Starter();
  REQUIRE(restored.LoadJson(sim.SaveJson(), &error));
  CHECK(restored.Rooms().size() == expected.size());
  CHECK(restored.CargoStore().capacity == 20);
}

TEST_CASE("every optional room can be bought exactly once at a station", "[starter][station]") {
  auto initial = Starter();
  initial.MutableState().shiplets = 1000;
  CHECK_FALSE(initial.BuyRoom("science"));
  Dock(initial);
  initial.MutableState().shiplets = 499;
  CHECK_FALSE(initial.BuyRoom("science"));
  CHECK_FALSE(initial.BuyRoom("engine"));
  CHECK_FALSE(initial.BuyRoom("not-a-room"));
  initial.MutableState().shiplets = 1000;
  const auto station = ship::BuildStationScreen(initial, initial.Map().here);
  int count = 0;
  for (const auto& room : World().wren.rooms) {
    if (ship::Simulation::StandardRoom(room.id)) continue;
    ++count;
    bool offered = false;
    for (const auto& column : station.columns) for (const auto& row : column.rows) {
      if (row.id == "room-buy:" + room.id) { offered = row.enabled; CHECK(row.label.find("500 sh") != std::string::npos); }
    }
    CHECK(offered);
    auto sim = initial;
    const auto money = sim.State().shiplets;
    REQUIRE(ship::PressStationAction(sim, "room-buy:" + room.id));
    CHECK(sim.HasRoom(room.id));
    CHECK(sim.State().shiplets == money - 500);
    CHECK(sim.ActionCount() == 1);
    CHECK_FALSE(sim.BuyRoom(room.id));
    CHECK(ship::OpenTask(sim, "room", room.id, true));
    std::string error;
    auto restored = Starter();
    REQUIRE(restored.LoadJson(sim.SaveJson(), &error));
    CHECK(restored.HasRoom(room.id));
    CHECK(restored.Rooms().size() == 7);
  }
  CHECK(count == 15);
}

TEST_CASE("a legacy complete ship keeps its rooms and cargo across loading and transfer", "[starter][legacy]") {
  auto old = ship::Simulation::ReferenceVoyage(World());
  REQUIRE(old.Rooms().size() == 21);
  REQUIRE(old.CargoStore().capacity == 40);
  const auto oldGoods = old.Goods().size();
  auto sim = Starter();
  std::string error;
  REQUIRE(sim.LoadJson(old.SaveJson(), &error));
  CHECK(sim.Rooms().size() == 21);
  CHECK(sim.CargoStore().capacity == 40);
  CHECK(sim.Goods().size() == oldGoods);
  Dock(sim);
  sim.MutableState().shiplets = 5000;
  REQUIRE(sim.BuyLargerShip());
  CHECK(sim.Rooms().size() == 21);
  CHECK(sim.Goods().size() == oldGoods);
  CHECK(sim.CargoStore().capacity >= 40);
}

TEST_CASE("a starter ship keeps a purchased facility when transferring to a larger hull", "[starter][station]") {
  auto sim = Starter();
  Dock(sim);
  sim.MutableState().shiplets = 6000;
  REQUIRE(sim.BuyRoom("science"));
  REQUIRE(sim.BuyLargerShip());
  CHECK(sim.State().shipClass == "kestrel");
  CHECK(sim.HasRoom("science"));
  CHECK_FALSE(sim.HasRoom("simulacron"));
  CHECK(sim.CargoStore().capacity == 60);
  CHECK(sim.CargoUsed() <= sim.CargoStore().capacity);
  std::string error;
  auto restored = Starter();
  REQUIRE(restored.LoadJson(sim.SaveJson(), &error));
  CHECK(restored.HasRoom("science"));
  CHECK_FALSE(restored.HasRoom("simulacron"));
  CHECK(restored.State().shipClass == "kestrel");
}

TEST_CASE("the starter can reach its first station through real controls", "[starter][journey]") {
  auto sim = Starter();
  REQUIRE(ship::OpenTask(sim, "nav", "main", true));
  REQUIRE(ship::PressNavAction(sim, "select", "tolme"));
  REQUIRE(ship::PressNavAction(sim, "travel"));
  REQUIRE(ship::OpenTask(sim, "room", "o2", true));
  for (int i = 0; i < 3; ++i) REQUIRE(ship::PressProgressButton(sim, "o2s" + std::to_string(i)));
  REQUIRE(ship::OpenTask(sim, "nav", "main", true));
  REQUIRE(ship::PressNavAction(sim, "stand-watch"));
  REQUIRE(ship::OpenTask(sim, "room", "cargo", true));
  for (int i = 0; i < 2; ++i) REQUIRE(ship::PressProgressButton(sim, "salvage-" + std::to_string(i)));
  REQUIRE(sim.BankReady());
  REQUIRE(ship::OpenTask(sim, "room", "o2", true));
  REQUIRE(ship::PressProgressButton(sim, "o2-run"));
  REQUIRE(ship::OpenTask(sim, "system", "fuel", true));
  REQUIRE(ship::PressSystemAction(sim, "fuel"));
  REQUIRE(ship::OpenTask(sim, "room", "engine", true));
  REQUIRE(ship::PressProgressButton(sim, "maint"));
  REQUIRE(ship::OpenTask(sim, "system", "energy", true));
  REQUIRE(ship::PressSystemAction(sim, "energy"));
  REQUIRE(ship::OpenTask(sim, "system", "shield", true));
  REQUIRE(ship::PressSystemAction(sim, "shield"));
  REQUIRE(sim.FindOngoing("travel") != nullptr);
  CHECK(sim.FindOngoing("travel")->state == "done");
  REQUIRE(ship::OpenTask(sim, "nav", "main", true));
  REQUIRE(ship::PressNavAction(sim, "travel"));
  CHECK(sim.Map().here == "tolme");
  CHECK(sim.State().docked);
  CHECK(sim.State().gameOver.empty());
  REQUIRE(ship::OpenTask(sim, "station", sim.Map().here, true));
  REQUIRE(ship::PressStationAction(sim, "sell:ore"));
  REQUIRE(sim.State().shiplets >= 500);
  REQUIRE(ship::PressStationAction(sim, "room-buy:science"));
  CHECK(sim.HasRoom("science"));
  CHECK(ship::OpenTask(sim, "room", "science", true));
}

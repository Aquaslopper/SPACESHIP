#include "core/Simulation.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  const auto catalog = ship::Catalog::LoadFromAssets("assets");
  auto sim = ship::Simulation::NewVoyage(catalog);
  sim.MutableMap().here = sim.World().ports.front().id;
  sim.MutableMap().inSpace = false;
  sim.MutableState().docked = true;
  sim.MutableCrew().push_back(ship::MakeCrew(sim.World().candidates.front().crew));
  assert(sim.StartCookingSelf("", false));
  const auto before = nlohmann::ordered_json::parse(sim.SaveJson());
  const auto roomCount = sim.Rooms().size();
  sim.MutableState().shiplets = 1;
  auto market = ship::BuildStationScreen(sim, sim.Map().here);
  for (const std::string kind : {"cargo-spine", "cargo-bulk"}) {
    const auto* row = ship::FindStationItem(market, "yard", "ship-buy:" + kind);
    assert(row && row->free && row->enabled && row->commit == "Buy - 1 sh");
  }
  assert(ship::PressStationAction(sim, "ship-buy:cargo-spine"));
  assert(sim.State().shipClass == "cargo-spine" && sim.State().shiplets == 0);
  assert(sim.CargoCapacity() == 120 && sim.Rooms().size() == roomCount);
  assert(ship::BuildCargoRoom(sim).shipLabel == "SC Cargo Spine");
  assert(!sim.BuyLargerShip("cargo-bulk"));
  assert(!sim.BuyLargerShip("cargo-spine"));
  const auto after = nlohmann::ordered_json::parse(sim.SaveJson());
  for (const char* key : {"crew", "goods", "stats", "life", "mess", "map"}) {
    assert(before.at(key) == after.at(key));
  }
  assert(before["state"]["ongoing"] == after["state"]["ongoing"]);
  sim.MutableState().shiplets = 1000;
  assert(sim.BuyUpgrade("bay"));
  assert(sim.CargoCapacity() == 180);
  assert(ship::PressStationAction(sim, "ship-buy:cargo-bulk"));
  assert(sim.CargoCapacity() == 360 && sim.Rooms().size() == roomCount);
  assert(ship::BuildCargoRoom(sim).shipLabel == "SC Cargo Bulk");
  const auto saved = sim.SaveJson();
  auto loaded = ship::Simulation::NewVoyage(catalog);
  std::string error;
  assert(loaded.LoadJson(saved, &error));
  assert(loaded.State().shipClass == "cargo-bulk");
  assert(loaded.Ship().Get("name") == "SC Cargo Bulk");
  assert(loaded.CargoCapacity() == 360 && loaded.Rooms().size() == roomCount);
  loaded.MutableGoods().front().vol = 250;
  assert(loaded.BuyLargerShip("cargo-spine"));
  assert(loaded.CargoCapacity() == 180 && loaded.CargoUsed() == 250);
  loaded.MutableState().shiplets = 1000;
  assert(loaded.BuyUpgrade("bay2"));
  assert(loaded.CargoCapacity() == 240);
  assert(loaded.BuyLargerShip("cargo-bulk"));
  assert(loaded.CargoCapacity() == 480 && loaded.CargoUsed() == 250);
  assert(loaded.BuyLargerShip("cargo-spine"));
  loaded.MutableState().docked = false;
  assert(!loaded.BuyLargerShip("cargo-bulk"));
  auto original = ship::Simulation::NewVoyage(catalog);
  original.MutableMap().here = original.World().ports.front().id;
  original.MutableState().docked = true;
  original.MutableState().shiplets = 4000;
  assert(original.BuyLargerShip());
  assert(original.State().shipClass == "kestrel" && original.State().shiplets == 0);
  std::cout << "PASS: shipyard offers, 1-shiplet purchases, unchanged rooms/crew/cargo/stats, refits, ship switching, save/load, dock requirement and original Kestrel purchase\n";
}

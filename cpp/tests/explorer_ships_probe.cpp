#include "core/Simulation.h"
#include "ui/ShipDiagram.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
  const auto catalog = ship::Catalog::LoadFromAssets("assets");
  for (const std::string kind : {"explorer-needle", "explorer-manta", "yamato", "arcadia"}) {
    auto sim = ship::Simulation::NewVoyage(catalog);
    sim.MutableMap().here = sim.World().ports.front().id;
    sim.MutableMap().inSpace = false;
    sim.MutableState().docked = true;
    sim.MutableCrew().push_back(ship::MakeCrew(sim.World().candidates.front().crew));
    assert(sim.StartCookingSelf("", false));
    const auto before = nlohmann::ordered_json::parse(sim.SaveJson());
    const auto originalFields = sim.Ship().fields;
    const double price = std::stod(sim.World().shipFields.at(kind).at("price"));
    sim.MutableState().shiplets = price;
    const auto market = ship::BuildStationScreen(sim, sim.Map().here);
    const auto* offer = ship::FindStationItem(market, "yard", "ship-buy:" + kind);
    assert(offer && offer->enabled && offer->free && !offer->commit.empty());
    assert(ship::PressStationAction(sim, "ship-buy:" + kind));
    assert(sim.State().shipClass == kind && sim.State().shiplets == 0);
    assert(sim.CargoCapacity() == 20 && sim.Ship().Berths() == 6);
    assert(&ship::ActivePlan(sim) == &sim.World().wren);
    const auto after = nlohmann::ordered_json::parse(sim.SaveJson());
    for (const char* key : {"rooms", "crew", "goods", "stats", "life", "mess", "map"}) {
      assert(before.at(key) == after.at(key));
    }
    assert(before["state"]["ongoing"] == after["state"]["ongoing"]);
    for (const auto& field : originalFields) {
      if (field.first != "name" && field.first != "className" && field.first != "price") {
        assert(sim.Ship().Get(field.first) == field.second);
      }
    }
    assert(ship::BuildCargoRoom(sim).shipLabel == sim.Ship().Get("name"));
    for (int level = 0; level <= 2; ++level) {
      assert(sim.ShipCargoCapacity(kind, level) == sim.ShipCargoCapacity("wren", level));
    }
    assert(!sim.BuyLargerShip(kind));
    auto loaded = ship::Simulation::NewVoyage(catalog);
    std::string error;
    assert(loaded.LoadJson(sim.SaveJson(), &error));
    assert(loaded.State().shipClass == kind && loaded.CargoCapacity() == 20);
    assert(&ship::ActivePlan(loaded) == &loaded.World().wren);
    assert(loaded.Rooms().size() == sim.Rooms().size());
    loaded.MutableState().shiplets = 4000;
    assert(loaded.BuyUpgrade("bay") && loaded.CargoCapacity() == 60);
    assert(loaded.BuyUpgrade("bay2") && loaded.CargoCapacity() == 90);
    loaded.MutableState().shiplets = 4000;
    assert(loaded.BuyLargerShip());
    assert(loaded.State().shipClass == "kestrel");
    auto reference = ship::Simulation::ReferenceVoyage(catalog);
    assert(reference.ShipCargoCapacity(kind, 0) == reference.ShipCargoCapacity("wren", 0));
    std::cout << "PASS: " << kind << " uses listed price; Wren stats, six berths, rooms/layout, ongoing work, refits and save/load retained\n";
  }
}

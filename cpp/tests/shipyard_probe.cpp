#include "core/Simulation.h"
#include "ui/StationScreen.h"
#include "ui/Widgets.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
  const auto catalog = ship::Catalog::LoadFromAssets("assets");
  auto sim = ship::Simulation::NewVoyage(catalog);
  sim.MutableMap().here = sim.World().ports.front().id;
  sim.MutableMap().inSpace = false;
  sim.MutableState().docked = true;
  const std::map<std::string, double> prices = {{"cargo-spine",12000},{"cargo-bulk",16000},{"explorer-needle",9000},{"explorer-manta",6000},{"yamato",200000},{"arcadia",200000}};
  for (const auto& entry : prices) assert(std::stod(sim.World().shipFields.at(entry.first).at("price")) == entry.second);
  auto resale = sim;
  resale.MutableState().shiplets = 6000;
  assert(!ship::PressStationAction(resale, "ship-disposition:sell"));
  assert(ship::PressStationAction(resale, "ship-buy:explorer-manta"));
  assert(resale.State().shiplets == 0);
  assert(ship::PressStationAction(resale, "ship-disposition:sell"));
  assert(resale.State().shiplets == 1000);
  assert(!ship::PressStationAction(resale, "ship-disposition:sell"));
  assert(resale.State().parkedShips.empty());
  sim.MutableState().shiplets = 12000;
  ship::Tab tab;
  tab.kind = "station";
  tab.id = sim.Map().here;
  ship::StationSelection selection{"yard", "ship-buy:cargo-spine"};
  auto screen = ship::BuildTabScreen(sim, tab, ship::WikiView(), &selection);
  assert(screen.panelRows.back().at(0).charged);
  assert(!screen.panelRows.back().at(1).enabled && !screen.panelRows.back().at(2).enabled);
  assert(screen.panelRows.back().at(1).label == "Sell old ship for 1000 sh");
  const auto yard = ship::BuildStationScreen(sim, sim.Map().here);
  for (const auto& col : yard.columns) if (col.id == "yard") {
    assert(col.rows.at(3).id == "ship-buy:explorer-manta");
    assert(col.rows.at(4).id == "ship-buy:kestrel");
  }
  assert(!ship::PressStationAction(sim, "ship-disposition:park"));
  assert(ship::PressStationAction(sim, "ship-buy:cargo-spine"));
  assert(sim.State().shiplets == 0 && sim.State().parkedShips.at(0) == "wren");
  screen = ship::BuildTabScreen(sim, tab, ship::WikiView(), &selection);
  assert(screen.panelRows.back().at(1).enabled && screen.panelRows.back().at(2).enabled);
  assert(ship::PressStationAction(sim, "ship-disposition:park"));
  assert(ship::PressStationAction(sim, "ship-buy:wren"));
  assert(sim.State().shipClass == "wren" && sim.State().parkedShips.at(0) == "cargo-spine");
  assert(ship::PressStationAction(sim, "ship-buy:cargo-spine"));
  sim.MutableState().shiplets = 16000;
  assert(ship::PressStationAction(sim, "ship-buy:cargo-bulk"));
  assert(sim.State().previousShip == "cargo-spine");
  assert(ship::PressStationAction(sim, "ship-disposition:sell"));
  assert(sim.State().shiplets == 4000 && sim.State().parkedShips.size() == 1);
  auto loaded = ship::Simulation::NewVoyage(catalog);
  std::string error;
  assert(loaded.LoadJson(sim.SaveJson(), &error));
  assert(loaded.State().parkedShips == sim.State().parkedShips);
  assert(loaded.State().previousShip.empty());
  sim.MutableState().shiplets = 10000;
  sim.ClampStat("hull", -100); sim.ClampStat("hull", 40);
  sim.ClampStat("fuel", -100); sim.ClampStat("fuel", 20);
  sim.ClampStat("energy", -100); sim.ClampStat("energy", 30);
  sim.ClampStat("shield", -100); sim.ClampStat("shield", 40);
  const auto engine = sim.State().engineCondition;
  assert(ship::PressStationAction(sim, "repair-hull"));
  assert(sim.StatValue("hull") == 50 && sim.State().shiplets == 9900);
  assert(ship::PressStationAction(sim, "hull-service"));
  assert(sim.StatValue("hull") == 55 && sim.State().shiplets == 9400);
  assert(ship::PressStationAction(sim, "tank-fuel"));
  assert(sim.StatValue("fuel") == 30 && sim.State().shiplets == 9350);
  assert(ship::PressStationAction(sim, "maintenance"));
  assert(sim.StatValue("energy") == 100 && sim.StatValue("shield") == 100);
  assert(sim.State().engineCondition == engine && sim.State().shiplets == 9100);
  for (const auto id : {"drive", "rack", "scanner", "scanner2"}) assert(!sim.BuyUpgrade(id));
  assert(sim.BuyUpgrade("bay") && sim.State().shiplets == 7600);
  assert(sim.BuyUpgrade("bay2") && sim.State().shiplets == 5100);
  assert(sim.BuyUpgrade("galley") && sim.State().shiplets == 2100);
  assert(sim.BuyUpgrade("filter") && sim.State().shiplets == 600);
  const auto model = ship::BuildStationScreen(sim, sim.Map().here);
  for (const auto& col : model.columns) {
    if (col.id != "yard") continue;
    assert(col.title == "Shipyard");
    for (const auto& row : col.rows) {
      assert(row.id.find("room-buy:") != 0);
      assert(row.id != "upgrade:drive" && row.id != "upgrade:rack" && row.id != "upgrade:scanner" && row.id != "upgrade:scanner2");
    }
  }
  std::cout << "PASS: Wren resale 1000 sh, buy-first sell/park controls, Tessellate ordering, green purchase marker, parking/switching, save/load, prices and services\n";
}

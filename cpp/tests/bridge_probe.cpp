#include "ui/Shell.h"
#include "core/Simulation.h"
#include "ui/RoomsScreen.h"
#include "ui/Widgets.h"
#include <cassert>
#include <iostream>
int main() {
  const auto catalog = ship::Catalog::LoadFromAssets("assets");
  auto sim = ship::Simulation::NewVoyage(catalog);
  sim.MutableMap().here = sim.World().ports.front().id;
  ship::OpenTask(sim,"room","bridge",true);
  sim.FindStat("fuel")->value = 100;
  sim.MutableState().fuelCells = 0;
  const auto key = ship::ButtonChargeKey(sim,"press:room:warp:");
  const auto count = sim.ActionCount();
  ship::Task work;
  work.id = "bridge-probe-work";
  work.type = "scan";
  work.state = "running";
  work.left = 100;
  work.total = 100;
  sim.MutableState().ongoing.push_back(work);
  for (int i=0;i<4;++i) {
    assert(ship::PressRoomAction(sim,"warp"));
    assert(sim.State().charges.at(key).left == 3-i);
    assert(sim.StatValue("fuel") == 80-20*i);
  }
  assert(!ship::PressRoomAction(sim,"warp"));
  assert(sim.FindOngoing("bridge-probe-work")->left == 80);
  sim.MutableState().ongoing.clear();
  assert(sim.ActionCount() == count && sim.State().positiveClicks == 20);
  assert(sim.State().fuelUsed == 80);
  ship::PressWithoutCharge(sim,[&](){sim.PerformAction();return true;});
  assert(sim.State().charges.at(key).left == 0);
  auto loaded = ship::Simulation::NewVoyage(catalog);
  std::string error;
  if (!loaded.LoadJson(sim.SaveJson(),&error)) { std::cerr << error << "\n"; return 1; }
  assert(loaded.State().charges.at(key).refillClicks == 1);
  assert(loaded.State().fuelUsed == 80 && loaded.State().positiveClicks == 20);
  for (int i=0;i<3;++i) {
    ship::PressWithoutCharge(sim,[&](){sim.PerformAction();return true;});
    assert(sim.State().charges.at(key).left == 0);
  }
  ship::PressWithoutCharge(sim,[&](){sim.PerformAction();return true;});
  assert(sim.State().charges.at(key).left == 1);
  sim.FindStat("fuel")->value = 19;
  assert(!ship::PressRoomAction(sim,"warp"));
  assert(sim.State().charges.at(key).left == 1);
  sim.ProAction(4);
  assert(sim.State().charges.at(key).left == 1);
  sim.ProAction(1);
  assert(sim.State().charges.at(key).left == 2);
  auto split = ship::Simulation::NewVoyage(catalog);
  ship::OpenTask(split,"room","bridge",true);
  split.FindStat("energy")->value=10;
  split.FindStat("fuel")->value=10;
  split.MutableState().fuelCells=4;
  auto splitKey = ship::ButtonChargeKey(split,"press:room:fuel-split:");
  for(int i=0;i<4;++i) {
    assert(ship::PressRoomAction(split,"fuel-split"));
    assert(split.State().charges.at(splitKey).left == 3-i);
  }
  assert(split.State().fuelCells == 0);
  assert(!ship::PressRoomAction(split,"fuel-split"));
  split.ProAction(2);
  assert(split.State().charges.at(splitKey).left == 1);
  assert(!ship::PressRoomAction(split,"fuel-split"));
  split.MutableState().fuelCells=1;
  assert(ship::PressRoomAction(split,"fuel-split"));
  assert(split.State().charges.at(splitKey).left == 0);
  auto shopping = ship::Simulation::NewVoyage(catalog);
  shopping.MutableMap().here = shopping.World().ports.front().id;
  shopping.MutableState().docked = true;
  shopping.MutableState().shiplets = 1000;
  assert(shopping.BuySupply("fuel-cell"));
  assert(shopping.State().moneySpent == 45);
  assert(shopping.State().fuelUsed == 0);
  const auto room = ship::BuildRoomScreen(split,"bridge");
  assert(room.rows.size()==2 && room.rows[0].id=="fuel-split" && room.rows[1].id=="warp");
  ship::Tab tab;tab.kind="room";tab.id="bridge";
  for (int i=0;i<12;++i) split.FreeCommit("Bridge feed clamp probe " + std::to_string(i));
  const auto storedFeedSize = split.State().feed.size();
  const auto screen=ship::BuildTabScreen(split,tab);
  assert(screen.panels.size()==3 && screen.panels[1].id=="bridge-totals" && screen.panels[2].id=="bridge-feed");
  bool hasLocation = false;
  for (const auto& line : screen.panels[1].lines) if (line.key == "Location") hasLocation = true;
  assert(hasLocation);
  assert(screen.panels[2].lines.size() == 4);
  assert(screen.panels[2].lines.front().value.find("Bridge feed clamp probe 11") != std::string::npos);
  assert(split.State().feed.size() == storedFeedSize && storedFeedSize > 4);
  std::cout << "PASS: bounded latest-four Bridge feed with full history retained\n";

  // Planet arrival landing, free Dispatch button between fuel-split and warp, and free ore pickup
  auto planetSim = ship::Simulation::NewVoyage(catalog);
  std::string planetId;
  for (const auto& body : planetSim.World().bodies) {
    if (body.type == "planet") {
      planetSim.MutableMap().here = planetSim.World().BodySystem(body);
      if (const auto* sys = planetSim.World().FindSystem(planetSim.MutableMap().here)) {
        planetSim.MutableMap().here = sys->entry;
      }
      planetId = body.id;
      break;
    }
  }
  assert(!planetId.empty());
  assert(!planetSim.LandedOnPlanet());
  assert(planetSim.SelectTarget(planetId));
  assert(planetSim.StartTravel());
  auto* travel = planetSim.FindOngoing("travel");
  assert(travel != nullptr);
  travel->left = 0;
  travel->state = "done";
  assert(planetSim.Acknowledge("travel"));
  assert(planetSim.LandedOnPlanet());
  const auto landedRoom = ship::BuildRoomScreen(planetSim, "bridge");
  assert(landedRoom.rows.size() == 3);
  assert(landedRoom.rows[0].id == "fuel-split");
  assert(landedRoom.rows[1].id == "dispatch");
  assert(landedRoom.rows[2].id == "warp");
  const double clicksBefore = planetSim.ActionCount();
  const double posBefore = planetSim.State().positiveClicks;
  const std::size_t goodsBefore = planetSim.Goods().size();
  assert(ship::PressRoomAction(planetSim, "dispatch"));
  assert(planetSim.CollectPlanetOre(planetId, 0));
  assert(!planetSim.CollectPlanetOre(planetId, 0));
  assert(planetSim.Goods().size() == goodsBefore + 1);
  assert(planetSim.Goods().back().kind == "Bulk metal");
  assert(planetSim.Goods().back().vol == 1.0);
  assert(planetSim.ActionCount() == clicksBefore);
  assert(planetSim.State().positiveClicks == posBefore);
  std::cout << "PASS: planet arrival auto-lands, dispatch appears between split and warp, ore pickup is free\n";
  std::cout << "PASS: Bridge controls, four WARP uses / 80 fuel / 20 positive clicks, no self-refill, five-click WARP quarter refills, unchanged two-click split refills, cell requirements, persistence and information panels\n";
}

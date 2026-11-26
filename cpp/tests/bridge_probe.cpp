#include "ui/Draw.h"
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

  // ---- subterranean dungeon B1..B3 ---------------------------------------
  // Deterministic layout read through the test-only friend observer, then the
  // Simulation-facing behaviours: hazard and sentry clashes, real crew damage
  // that survives travel, kit-gated breach, chest lotting, and the monolith.
  ship::ShellView dngView;
  for (int f = 1; f <= 3; ++f) {
    ship::detail::PlanetSurfaceProbeState::Load(dngView, planetSim, planetId, f);
    const auto& st = ship::detail::PlanetSurfaceProbeState::State(dngView);
    assert(st.inMine == f);
    assert(st.doorCount >= 2 && st.trapCount >= 2 && st.mobCount >= 1 && st.chestCount >= 1);
    assert(st.leverX != 0 || st.leverY != 0);
    assert(st.Tile(st.stairX[0], st.stairY[0]) == 1u);
    assert(st.Tile(st.stairX[1], st.stairY[1]) == 1u);
    assert(f != 3 || st.monolithX != 0 || st.monolithY != 0);
  }
  // The shipped asset pack has no starting crew, so hire the pool's first two
  // hands the same way the station screen does, then seat the first as lead.
  if (planetSim.Crew().empty()) {
    assert(catalog.candidates.size() >= 2);
    ship::CrewMember hired = ship::MakeCrew(catalog.candidates.front().crew);
    hired.health = 100.0;
    hired.fatigue = 0.0;
    planetSim.MutableCrew().push_back(hired);
    planetSim.MutableState().crewSel = hired.id;
    ship::CrewMember spare = ship::MakeCrew(catalog.candidates[1].crew);
    spare.health = 100.0;
    spare.fatigue = 0.0;
    planetSim.MutableCrew().push_back(spare);
  }
  ship::CrewMember* lead = planetSim.DungeonLead();
  assert(lead != nullptr && planetSim.Crew().size() >= 2);
  const std::size_t crewStart = planetSim.Crew().size();
  // Sentry clash: real damage on both sides, and a live sentry survives one hit.
  lead->health = 100.0;
  lead->fatigue = 0.0;
  bool defeated = false;
  assert(planetSim.DungeonStrikeSentry(planetId, 1, 0, 3, &defeated));
  assert(!defeated);
  assert(planetSim.DungeonLead()->health < 100.0);
  assert(planetSim.DungeonLead()->fatigue >= 5.0);
  const double hurt = planetSim.DungeonLead()->health;
  assert(planetSim.DungeonStrikeSentry(planetId, 1, 0, 3, &defeated));
  assert(planetSim.DungeonLead()->health < hurt);
  assert(planetSim.DungeonStrikeSentry(planetId, 1, 0, 1, &defeated));
  assert(defeated);
  assert(!planetSim.DungeonStrikeSentry(planetId, 1, 0, 1, &defeated));
  // Hazard: floor-scaled damage and fatigue, no protective floor anywhere.
  assert(planetSim.DungeonTriggerHazard(planetId, 2, 0, 0));
  assert(planetSim.DungeonLead()->health >= 0.0);
  assert(planetSim.DungeonLead()->fatigue > 0.0);
  assert(!planetSim.DungeonTriggerHazard(planetId, 2, 0, 0));
  // Travel and dock must not scrub the wounds or the dungeon marks.
  const double woundedHealth = planetSim.DungeonLead()->health;
  bool leftPlanet = false;
  for (const ship::Port& port : planetSim.World().ports) {
    if (!planetSim.SelectTarget(port.id)) { continue; }
    if (!planetSim.StartTravel()) { continue; }
    if (auto* run = planetSim.FindOngoing("travel")) { run->left = 0; run->state = "done"; }
    assert(planetSim.Acknowledge("travel"));
    leftPlanet = true;
    break;
  }
  assert(leftPlanet);
  const double away = planetSim.DungeonLead()->health;
  assert(away == woundedHealth);  // no quiet healing between bodies
  if (planetSim.Dock()) { assert(planetSim.State().docked); }
  assert(planetSim.DungeonLead()->health == woundedHealth);
  assert(planetSim.State().mapMarks.count("planet-dng:" + planetId + ":f1:mob:0") == 1);
  assert(planetSim.State().mapMarks.count("planet-dng:" + planetId + ":f2:trap:0") == 1);
  // A lethal hazard floors the hand at exactly 0.0 and loses them; nothing
  // clamps them back to full, and the rest of the crew keeps their wounds.
  planetSim.MutableMap().inSpace = false;
  planetSim.MutableState().docked = false;
  planetSim.MutableMap().here = planetId;
  assert(planetSim.LandedOnPlanet());
  planetSim.DungeonLead()->health = 1.0;
  assert(planetSim.DungeonTriggerHazard(planetId, 3, 1, 3));
  assert(planetSim.Crew().size() == crewStart - 1);
  for (const ship::CrewMember& member : planetSim.Crew()) {
    assert(member.health > 0.0 && member.health <= 100.0);
  }
  // Kit-gated portcullis breach: consumes exactly one kit and persists.
  planetSim.MutableState().repairKits = 2.0;
  assert(planetSim.DungeonForceGate(planetId, 2));
  assert(planetSim.State().repairKits == 1.0);
  assert(!planetSim.DungeonForceGate(planetId, 2));
  assert(planetSim.State().repairKits == 1.0);
  // Chest: appends one lot when there is space, refuses when the hold is full.
  const std::size_t lotsBefore = planetSim.Goods().size();
  assert(planetSim.DungeonLootChest(planetId, 1, 0));
  assert(planetSim.Goods().size() == lotsBefore + 1);
  assert(planetSim.Goods().back().vol == 1.0);
  assert(planetSim.Goods().back().kind == "Relic" || planetSim.Goods().back().kind == "Rare ore");
  assert(!planetSim.DungeonLootChest(planetId, 1, 0));
  assert(planetSim.Goods().size() == lotsBefore + 1);
  // Fill the hold, then a fresh chest must refuse and leave no mark.
  while (planetSim.CargoUsed() + 1.0 <= planetSim.CargoStore().capacity) {
    ship::GoodsLot filler = planetSim.Goods().back();
    filler.id += "-filler";
    planetSim.MutableGoods().push_back(filler);
  }
  const std::size_t fullLots = planetSim.Goods().size();
  assert(!planetSim.DungeonLootChest(planetId, 1, 1));
  assert(planetSim.Goods().size() == fullLots);
  assert(planetSim.State().mapMarks.count("planet-dng:" + planetId + ":f1:chest:1") == 0);
  // Monolith files exactly one SurveyRecord for the planet.
  const std::size_t filesBefore = planetSim.Survey().size();
  assert(planetSim.DungeonFileMonolith(planetId));
  assert(planetSim.Survey().size() == filesBefore + 1);
  assert(!planetSim.DungeonFileMonolith(planetId));
  assert(planetSim.Survey().size() == filesBefore + 1);
  // Save round-trip: no save-format change, dungeon marks survive LoadJson.
  std::string dngError;
  auto reloaded = ship::Simulation::NewVoyage(catalog);
  assert(reloaded.LoadJson(planetSim.SaveJson(), &dngError));
  assert(reloaded.State().mapMarks.count("planet-dng:" + planetId + ":f2:gate") == 1);
  assert(reloaded.State().mapMarks.count("planet-dng:" + planetId + ":monolith") == 1);
  assert(reloaded.State().repairKits == planetSim.State().repairKits);
  std::cout << "PASS: dungeon B1..B3 layout, hazard/sentry damage, kit breach, chest lots, monolith, save round-trip\n";
  std::cout << "PASS: Bridge controls, four WARP uses / 80 fuel / 20 positive clicks, no self-refill, five-click WARP quarter refills, unchanged two-click split refills, cell requirements, persistence and information panels\n";
}

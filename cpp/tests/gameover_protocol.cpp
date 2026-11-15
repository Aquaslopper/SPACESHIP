#include "core/Simulation.h"
#include "data/Catalog.cpp"
#include "data/ChartExpansion.cpp"
#include "data/GalaxyGen.cpp"
#include "core/JsNumber.cpp"
#include "core/Action.cpp"
#include "core/Survival.cpp"
#include "core/CrewLogic.cpp"
#include "core/Tasks.cpp"
#include "core/Charges.cpp"
#include "core/Incidents.cpp"
#include "core/Awards.cpp"
#include "core/Navigation.cpp"
#include "core/Orders.cpp"
#include "core/Economy.cpp"
#include "core/Projects.cpp"
#include "core/Ships.cpp"
#include "core/SaveSystem.cpp"
#include "core/AutoSave.cpp"
#include "ui/Theme.cpp"
#include "ui/Layout.cpp"
#include "ui/Describe.cpp"
#include "ui/Shell.cpp"
#include "ui/ShipDiagram.cpp"
#include "ui/IdleScreen.cpp"
#include "ui/Fx.cpp"
#include "ui/Widgets.cpp"
#include "ui/EngineScreen.cpp"
#include "ui/O2Screen.cpp"
#include "ui/MessScreen.cpp"
#include "ui/CrewSprites.cpp"
#include "ui/CrewRecord.cpp"
#include "ui/QuartersScreen.cpp"
#include "ui/Popups.cpp"
#include "audio/Audio.cpp"
#include "ui/RoomsScreen.cpp"
#include "ui/AwardsScreen.cpp"
#include "ui/EventScreen.cpp"
#include "ui/StationScreen.cpp"
#include "ui/StoresScreen.cpp"
#include "ui/NavScreen.cpp"
#include "ui/WikiScreen.cpp"
#include "ui/TitleScreen.cpp"
#include <exception>
#include <iostream>
#include <string>

int main() {
  try {
    const auto catalog = ship::Catalog::LoadFromAssets("assets");
    // Boundary fixtures, not playthroughs. Exercise the real action/loss path.
    // No fixture assigns gameOver or reimplements the game's death conditions.
    for (const std::string scenario : {"hull-zero", "fuel-zero-in-space",
                                      "fuel-zero-docked", "fuel-positive",
                                      "air-last-click", "air-one-click-left",
                                      "active-filter", "filter-exhaustion"}) {
      auto sim = ship::Simulation::NewVoyage(catalog);
      sim.MutableState().docked = true;
      sim.FindStat("hull")->value = 100;
      sim.FindStat("fuel")->value = 100;
      sim.FindStat("energy")->value = 100;
      sim.MutableState().filterLife = 0;
      sim.MutableState().airHold = 20;
      for (auto& slot : sim.MutableLife().slots) { slot.state = "empty"; }

      std::string expected;
      if (scenario == "hull-zero") {
        sim.FindStat("hull")->value = 0;
        expected = "The hull has failed.";
      } else if (scenario == "fuel-zero-in-space") {
        sim.MutableState().docked = false;
        sim.FindStat("fuel")->value = 0;
        expected = "Adrift with no fuel.";
      } else if (scenario == "fuel-zero-docked") {
        sim.FindStat("fuel")->value = 0;
      } else if (scenario == "fuel-positive") {
        sim.MutableState().docked = false;
        sim.FindStat("fuel")->value = 1;
      } else if (scenario == "air-last-click") {
        sim.MutableState().airHold = 1;
        expected = "The air is gone.";
      } else if (scenario == "air-one-click-left") {
        sim.MutableState().airHold = 2;
      } else if (scenario == "active-filter") {
        sim.MutableState().airHold = 0;
        sim.MutableState().filterLife = 2 * sim.GameRules().life.decay;
        for (auto& slot : sim.MutableLife().slots) { slot.state = "active"; }
      } else if (scenario == "filter-exhaustion") {
        sim.MutableState().filterLife = sim.GameRules().life.decay;
        for (auto& slot : sim.MutableLife().slots) { slot.state = "active"; }
      }

      sim.PerformAction();
      if (sim.ActionCount() != 1 || sim.State().gameOver != expected) {
        std::cerr << "FAIL " << scenario << ": expected "
                  << (expected.empty() ? "no game-over" : expected)
                  << ", got " << (sim.State().gameOver.empty() ? "no game-over" : sim.State().gameOver)
                  << "; actions=" << sim.ActionCount() << '\n';
        return 1;
      }
      if (scenario == "air-last-click" &&
          (sim.State().airHold != 0 || sim.AirPercent() != 0 || sim.AirStageIndex() != 0)) {
        std::cerr << "FAIL: last air click did not reach the lethal boundary\n";
        return 1;
      }
      if (scenario == "air-one-click-left" && sim.State().airHold != 1) {
        std::cerr << "FAIL: reserve did not decrease by one\n";
        return 1;
      }
      if (scenario == "active-filter" &&
          (!sim.BankRunning() || sim.State().filterLife != sim.GameRules().life.decay ||
           sim.AirPercent() != 100 || sim.AirStageIndex() != 4)) {
        std::cerr << "FAIL: active filter prerequisites changed\n";
        return 1;
      }
      if (scenario == "filter-exhaustion" &&
          (sim.BankRunning() || sim.State().filterLife != 0 ||
           sim.State().airHold != sim.GameRules().air.holdMax)) {
        std::cerr << "FAIL: exhausted bank did not hand over to cabin air\n";
        return 1;
      }
      std::cout << "PASS " << scenario << '\n';
    }

    // Entry prerequisite: a voyage with less fuel than its distance must be
    // allowed to start. This is a targeted API check, not a voyage replay.
    auto sim = ship::Simulation::NewVoyage(catalog);
    sim.MutableMap().here = sim.World().bodies.front().id;
    sim.FindStat("fuel")->value = 1;
    std::string target;
    for (const auto& system : sim.World().systems) {
      if (sim.InRange(system) && !sim.AtTarget(system) && sim.MetricsFor(system).dist > 1) {
        target = system.id;
        break;
      }
    }
    if (target.empty() || !sim.SelectTarget(target) || !sim.StartTravel() ||
        sim.FindOngoing("travel") == nullptr) {
      std::cerr << "FAIL: insufficient-range voyage cannot start\n";
      return 1;
    }
    sim.PerformAction();
    if (sim.StatValue("fuel") != 0 || sim.State().gameOver != "Adrift with no fuel.") {
      std::cerr << "FAIL: actual flight fuel depletion does not trigger Lost in Space\n";
      return 1;
    }
    std::cout << "PASS insufficient-range departure and fuel-loss connection\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL Protocol GameOver: " << error.what() << '\n';
    return 1;
  }
}

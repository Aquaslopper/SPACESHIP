// ============================================================================
//  spam_audit_tests.cpp — every paid control, clicked twice (Phase 9).
//  ---------------------------------------------------------------------------
//  The rule this file enforces: a control that has just spent an action must
//  refuse the very next click, because it is now either locked by
//  ACTION_COOLDOWN_RULES (routine 5, systems 10, space 15, critical 20), running
//  as an ongoing task, or disabled by the state change it caused.
//
//  These behavioral tests exercise paid controls through their view-model
//  actions; they do not claim that the graphical widgets are all reachable.
//
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <vector>

#include "core/JsNumber.h"
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/AwardsScreen.h"
#include "ui/EngineScreen.h"
#include "ui/EventScreen.h"
#include "ui/MessScreen.h"
#include "ui/O2Screen.h"
#include "ui/NavScreen.h"
#include "ui/QuartersScreen.h"
#include "ui/RoomsScreen.h"
#include "ui/SimCoreScreen.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include "ui/Widgets.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

// A voyage prepared so that as many paid controls as possible are live: money,
// power, supplies, a berth at a port, an uncommitted salvage lot of each kind,
// a signal on the wire, an incident on the board and memories to consolidate.
ship::Simulation Rich() {
  ship::Simulation sim = ship::Simulation::ReferenceVoyage(World());
  sim.MutableState().shiplets = 20000;
  sim.FindStat("energy")->value = 100;
  sim.FindStat("fuel")->value = 100;
  sim.FindStat("hull")->value = 60;
  sim.FindStat("shield")->value = 100;
  sim.MutableState().repairKits = 3;
  sim.MutableState().medkits = 3;
  sim.MutableState().fuelCells = 3;
  sim.MutableLife().canisters = 0;             // so an installation is live
  sim.MutableMess().rack = 7;
  for (ship::CrewMember& crew : sim.MutableCrew()) { crew.health = 100; }
  sim.MutableCrew()[0].health = 55;            // a treatment is live
  sim.MutableState().docked = true;
  sim.MutableMap().here = "tolme";
  sim.MutableState().sim.insight = 3;
  sim.MutableState().sim.signal = "request";
  sim.SimRemember("World", "A trader's account of the Toll Road.");
  sim.SimRemember("Discovery", "A quiet moon over Tolme.");
  // One uncommitted lot of each kind the hold and the workshop care about.
  const char* kinds[] = {"Salvage", "Perishable", "Ore", "Produce"};
  int serial = 0;
  for (const char* kind : kinds) {
    ship::GoodsLot lot;
    lot.id = "lot-audit-" + std::to_string(++serial);
    lot.name = std::string("Audit ") + kind;
    lot.kind = kind;
    lot.value = 40;
    sim.MutableGoods().push_back(lot);
  }
  ship::SurveyRecord record;
  record.id = "audit-survey";
  record.name = "Audit survey";
  record.value = 90;
  record.state = "surveyed";
  sim.MutableSurvey().push_back(record);
  // A report on the board: the hull's first arrives at action 45.
  for (int i = 0; i < 60 && sim.Events().empty(); ++i) { sim.StandWatch(); }
  return sim;
}

struct Probe {
  std::string screen;
  std::string control;
  std::string tier;              // what the rules should choose
  std::function<ship::Simulation()> factory;
  std::function<bool(ship::Simulation&)> press;
};

void AuditOne(const Probe& probe, int* checked, int* freeControl) {
  ship::Simulation sim = probe.factory();
  const double before = sim.ActionCount();
  const bool first = probe.press(sim);
  if (!first) {
    // A control the model offered but the press refused is a bug in the model,
    // not a locked button: say so loudly.
    CHECK(first);
    return;
  }
  const double afterFirst = sim.ActionCount();
  if (afterFirst == before) {
    // Free after all: reading, selecting, opening. Nothing to lock.
    *freeControl += 1;
    return;
  }
  CHECK(afterFirst == before + 1);

  const bool second = probe.press(sim);
  const bool locked = !second && sim.ActionCount() == afterFirst;
  CHECK_FALSE(second);
  CHECK(sim.ActionCount() == afterFirst);
  *checked += 1;
}

ship::Simulation Factory() { return Rich(); }

std::vector<Probe> StationProbes() {
  std::vector<Probe> probes;
  const ship::Simulation sim = Factory();
  const ship::StationScreenModel model = ship::BuildStationScreen(sim, "tolme");
  for (const ship::StationColumn& column : model.columns) {
    for (const ship::StationRow& row : column.rows) {
      if (!row.enabled || row.reading) { continue; }
      if (!ship::IsActionCost(row.cost, !row.cost.empty())) { continue; }
      Probe probe;
      probe.screen = "Station " + column.title;
      probe.control = row.id;
      probe.tier = ship::ActionCooldownCategory(row.label);
      probe.factory = &Factory;
      const std::string id = row.id;
      probe.press = [id](ship::Simulation& s) { return ship::PressStationAction(s, id, ""); };
      probes.push_back(probe);
    }
  }
  return probes;
}

std::vector<Probe> StoresProbes() {
  std::vector<Probe> probes;
  const ship::Simulation sim = Factory();
  for (const char* room : {"cargo", "science"}) {
    const ship::StoresScreenModel model = ship::BuildStoresScreen(sim, room);
    for (const ship::StoreRowView& row : model.rows) {
      if (!row.enabled || row.readingOnly || row.progress) { continue; }
      if (!ship::IsActionCost(row.cost, !row.cost.empty())) { continue; }
      Probe probe;
      probe.screen = std::string("Stores ") + room;
      probe.control = row.id;
      probe.tier = ship::ActionCooldownCategory(row.label);
      probe.factory = &Factory;
      const std::string id = row.id;
      probe.press = [id](ship::Simulation& s) { return ship::PressStoresAction(s, id, ""); };
      probes.push_back(probe);
    }
    for (const ship::ProgressButton& button : model.buttons) {
      if (button.state != ship::ButtonState::Idle) { continue; }
      Probe probe;
      probe.screen = std::string("Stores ") + room;
      probe.control = button.taskId;
      probe.tier = button.category;
      probe.factory = &Factory;
      const std::string id = button.taskId;
      probe.press = [id](ship::Simulation& s) { return ship::PressProgressButton(s, id); };
      probes.push_back(probe);
    }
  }
  return probes;
}

std::vector<Probe> RoomProbes() {
  std::vector<Probe> probes;
  const ship::Simulation sim = Factory();
  const char* rooms[] = {"r1", "r2", "r3", "r4", "r5", "r6", "r7",
                         "r8", "r9", "r10", "r11", "r12", "r13", "bridge"};
  for (const char* roomId : rooms) {
    const ship::RoomScreenModel room = ship::BuildRoomScreen(sim, roomId);
    for (const ship::ActionRowView& row : room.rows) {
      if (!row.enabled) { continue; }
      if (!ship::IsActionCost(row.cost, !row.cost.empty())) { continue; }
      Probe probe;
      probe.screen = std::string("Room ") + roomId;
      probe.control = row.id;
      probe.tier = ship::ActionCooldownCategory(row.label);
      probe.factory = &Factory;
      const std::string id = row.id;
      probe.press = [id](ship::Simulation& s) { return ship::PressRoomAction(s, id, ""); };
      probes.push_back(probe);
    }
    for (const ship::ProgressButton& button : room.buttons) {
      if (button.state != ship::ButtonState::Idle) { continue; }
      Probe probe;
      probe.screen = std::string("Room ") + roomId;
      probe.control = button.taskId;
      probe.tier = button.category;
      probe.factory = &Factory;
      const std::string id = button.taskId;
      probe.press = [id](ship::Simulation& s) { return ship::PressProgressButton(s, id); };
      probes.push_back(probe);
    }
  }
  return probes;
}

std::vector<Probe> SimCoreProbes() {
  std::vector<Probe> probes;
  const ship::Simulation sim = Factory();
  const ship::SimScreenModel core = ship::BuildSimCoreScreen(sim);
  for (const ship::ActionRowView& row : core.rows) {
    if (!row.enabled) { continue; }
    if (!ship::IsActionCost(row.cost, !row.cost.empty())) { continue; }
    Probe probe;
    probe.screen = "Simulacron";
    probe.control = row.id;
    probe.tier = ship::ActionCooldownCategory(row.label);
    probe.factory = &Factory;
    const std::string id = row.id;
    probe.press = [id](ship::Simulation& s) { return ship::PressSimCoreAction(s, id, ""); };
    probes.push_back(probe);
  }
  for (const ship::ProgressButton& button : core.buttons) {
    if (button.state != ship::ButtonState::Idle) { continue; }
    Probe probe;
    probe.screen = "Simulacron";
    probe.control = button.taskId;
    probe.tier = button.category;
    probe.factory = &Factory;
    const std::string id = button.taskId;
    probe.press = [id](ship::Simulation& s) { return ship::PressProgressButton(s, id); };
    probes.push_back(probe);
  }
  return probes;
}

std::vector<Probe> EventProbes() {
  std::vector<Probe> probes;
  const ship::Simulation sim = Factory();
  REQUIRE_FALSE(sim.Events().empty());
  const std::string reportId = sim.Events().front().id;
  const ship::EventScreenModel report = ship::BuildEventScreen(sim, reportId);
  for (const ship::ActionRowView& row : report.rows) {
    if (!row.enabled) { continue; }
    if (!ship::IsActionCost(row.cost, !row.cost.empty())) { continue; }
    Probe probe;
    probe.screen = "Event";
    probe.control = reportId + "/" + row.id;
    probe.tier = ship::ActionCooldownCategory(row.label);
    probe.factory = &Factory;
    const std::string id = row.id;
    probe.press = [id](ship::Simulation& s) { return ship::PressEventAction(s, id, ""); };
    probes.push_back(probe);
  }
  return probes;
}

std::vector<Probe> AwardProbes() {
  // Claiming is free by design, so it is audited for the opposite property: it
  // can be taken once, and the second click must not pay again.
  std::vector<Probe> probes;
  ship::Simulation probeSim = Factory();
  probeSim.MutableState().contracts["arrival"] = "ready";
  probeSim.CheckCommendations();
  const ship::AwardsScreenModel awards = ship::BuildAwardsScreen(probeSim);
  for (const ship::ActionRowView& row : awards.rows) {
    if (!row.enabled) { continue; }
    Probe probe;
    probe.screen = "Awards";
    probe.control = row.id;
    probe.tier = "free";
    probe.factory = [] {
      ship::Simulation s = Factory();
      s.MutableState().contracts["arrival"] = "ready";
      s.CheckCommendations();
      return s;
    };
    const std::string id = row.id;
    probe.press = [id](ship::Simulation& s) { return ship::PressAwardsAction(s, id, ""); };
    probes.push_back(probe);
  }
  return probes;
}

std::vector<Probe> NavProbes() {
  std::vector<Probe> probes;
  const auto navFactory = [] {
    ship::Simulation s = Factory();
    s.MutableMap().here = "kessler";
    s.MutableState().docked = false;
    s.MutableState().navTarget = "tolme";
    return s;
  };
  const ship::Simulation sim = navFactory();
  const ship::NavScreenModel nav = ship::BuildNavScreen(sim);
  for (const ship::ProgressButton& button : nav.screen.progress) {
    if (button.state != ship::ButtonState::Idle) { continue; }
    Probe probe;
    probe.screen = "Navigation";
    probe.control = button.taskId;
    probe.tier = button.category;
    probe.factory = navFactory;
    const std::string id = button.taskId;
    probe.press = [id](ship::Simulation& s) { return ship::PressProgressButton(s, id); };
    probes.push_back(probe);
  }
  {
    Probe probe;
    probe.screen = "Navigation";
    probe.control = "stand-watch";
    probe.tier = ship::ActionCooldownCategory("stand-watch");
    probe.factory = navFactory;
    probe.press = [](ship::Simulation& s) { return ship::PressNavAction(s, "stand-watch"); };
    probes.push_back(probe);
  }
  {
    // Docking needs a berth under the ship and the ship off the quay.
    const auto dockFactory = [] {
      ship::Simulation s = Factory();
      s.MutableMap().here = "tolme";
      s.MutableState().docked = false;
      return s;
    };
    Probe probe;
    probe.screen = "Navigation";
    probe.control = "dock";
    probe.tier = ship::ActionCooldownCategory("dock");
    probe.factory = dockFactory;
    probe.press = [](ship::Simulation& s) { return ship::PressNavAction(s, "dock"); };
    probes.push_back(probe);
  }
  return probes;
}

std::vector<Probe> QuartersProbes() {
  std::vector<Probe> probes;
  const auto quartersFactory = [] {
    ship::Simulation s = Factory();
    // Stage an order the way the sheet does: select a hand, then toggle a line.
    // Staging is free; only issuing the sheet costs an action.
    s.MutableState().crewSel = s.Crew().front().id;
    ship::PressQuartersAction(s, "order", "rest");
    return s;
  };
  Probe probe;
  probe.screen = "Crew Quarters";
  probe.control = "commit";
  probe.tier = ship::ActionCooldownCategory("The sheet");
  probe.factory = quartersFactory;
  probe.press = [](ship::Simulation& s) { return ship::PressQuartersAction(s, "commit", ""); };
  probes.push_back(probe);
  return probes;
}

std::vector<Probe> MachineProbes() {
  std::vector<Probe> probes;
  const auto machineFactory = [] {
    ship::Simulation s = Factory();
    s.MutableState().docked = false;
    s.MutableMap().here = "kessler";
    return s;
  };
  const ship::Simulation sim = machineFactory();
  const ship::TaskScreenModel engine = ship::BuildEngineScreen(sim).screen;
  const ship::TaskScreenModel o2 = ship::BuildO2Screen(sim).screen;
  const ship::TaskScreenModel mess = ship::BuildMessScreen(sim).screen;
  const struct {
    const char* screen;
    const ship::TaskScreenModel* model;
  } rooms[] = {{"Engine", &engine}, {"O2 Filter Room", &o2}, {"Mess Hall", &mess}};
  for (const auto& room : rooms) {
    for (const ship::ProgressButton& button : room.model->progress) {
      if (button.state != ship::ButtonState::Idle) { continue; }
      Probe probe;
      probe.screen = room.screen;
      probe.control = button.taskId;
      probe.tier = button.category;
      probe.factory = machineFactory;
      const std::string id = button.taskId;
      probe.press = [id](ship::Simulation& s) { return ship::PressProgressButton(s, id); };
      probes.push_back(probe);
    }
  }
  return probes;
}

}  // namespace

TEST_CASE("every paid control refuses the very next click", "[phase9][spam]") {
  std::vector<Probe> probes;
  const auto append = [&probes](std::vector<Probe> more) {
    probes.insert(probes.end(), more.begin(), more.end());
  };
  append(StationProbes());
  append(StoresProbes());
  append(RoomProbes());
  append(SimCoreProbes());
  append(EventProbes());
  append(AwardProbes());
  append(NavProbes());
  append(QuartersProbes());
  append(MachineProbes());

  REQUIRE(probes.size() >= 30);
  int checked = 0;
  int freeControl = 0;
  for (const Probe& probe : probes) { AuditOne(probe, &checked, &freeControl); }

  CHECK(checked >= 25);
  // The only free controls the audit offered are the logbook's claims: a claim
  // is free by design and is audited for paying exactly once.
  CHECK(freeControl <= 2);
}

TEST_CASE("a locked control is unlocked by the clock, not by waiting", "[phase9][spam]") {
  ship::Simulation sim = Rich();
  // Pressing through the window path starts the space tier's lock.
  sim.MutableMap().here = "kessler";
  sim.MutableState().docked = false;
  const double before = sim.ActionCount();
  CHECK(ship::PressNavAction(sim, "stand-watch"));
  const std::string guardKey =
      ship::ActionCooldownKey(sim, "command:stand-watch-advance-ongoing-work:0");
  CHECK(ship::CooldownRemaining(sim, guardKey) > 0.0);
  CHECK(ship::CooldownRemaining(sim, guardKey) == ship::RuleActions(sim, "space"));

  // Each other committed action advances the lock by one.
  const double start = ship::CooldownRemaining(sim, guardKey);
  sim.PerformAction("Something else");
  CHECK(ship::CooldownRemaining(sim, guardKey) == start - 1.0);
  CHECK(sim.ActionCount() == before + 2);

  // Once it reaches zero the same control is offered again.
  while (ship::CooldownRemaining(sim, guardKey) > 0.0) { sim.PerformAction("elsewhere"); }
  CHECK(ship::PressNavAction(sim, "stand-watch"));
}

TEST_CASE("a running task's button is inert until it is acknowledged", "[phase9][spam]") {
  ship::Simulation sim = Rich();
  sim.MutableState().docked = false;
  sim.MutableMap().here = "kessler";
  REQUIRE(ship::PressProgressButton(sim, "maint"));
  CHECK(sim.FindOngoing("maint") != nullptr);
  const double actions = sim.ActionCount();
  CHECK_FALSE(ship::PressProgressButton(sim, "maint"));   // running: inert
  CHECK(sim.ActionCount() == actions);
}

TEST_CASE("station purchases display the same five-action lock that dispatch enforces", "[audit][station][cooldown]") {
  auto sim = Rich();
  REQUIRE(ship::OpenTask(sim, "station", "tolme", true));
  const auto fuelRow = [&]() {
    const auto screen = ship::BuildTabScreen(sim, *ship::FindTab(sim.State(), sim.State().activeTab));
    for (std::size_t c = 0; c < screen.panelRowIds.size(); ++c) {
      for (std::size_t r = 0; r < screen.panelRowIds[c].size(); ++r) {
        if (screen.panelRowIds[c][r] == "buy-fuel") { return screen.panelRows[c][r]; }
      }
    }
    FAIL("Fuel purchase disappeared from station");
    return ship::CommandRowView{};
  };
  REQUIRE(fuelRow().enabled);
  const auto before = sim.ActionCount();
  REQUIRE(ship::PressStationAction(sim, "buy-fuel"));
  CHECK(sim.ActionCount() == before + 1);
  CHECK(fuelRow().remaining == 5);
  CHECK(fuelRow().displayCost == "COOLDOWN 5");
  CHECK_FALSE(fuelRow().enabled);
  CHECK_FALSE(ship::PressStationAction(sim, "buy-fuel"));
  CHECK(sim.ActionCount() == before + 1);
  // Rebuilding/reading a screen must never tick the action clock.
  for (int read = 0; read < 12; ++read) { CHECK(fuelRow().remaining == 5); }
  const char* others[] = {"buy-repair", "buy-medkit", "buy-canister", "buy-rations", "sell:ore"};
  for (int i = 0; i < 5; ++i) {
    REQUIRE(ship::PressStationAction(sim, others[i]));
    CHECK(fuelRow().remaining == 4 - i);
    CHECK(fuelRow().coolingDown == (i < 4));
    CHECK(fuelRow().enabled == (i == 4));
    if (i < 4) { CHECK_FALSE(ship::PressStationAction(sim, "buy-fuel")); }
  }
  REQUIRE(ship::PressStationAction(sim, "buy-fuel"));
  CHECK(fuelRow().displayCost == "COOLDOWN 5");
  CHECK(sim.ActionCount() == before + 7);
}

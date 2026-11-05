// ============================================================================
//  station_tests.cpp — Phase 6: the station, the stores and the four popups.
//  ---------------------------------------------------------------------------
//  Everything the port window offers is a row with a price and an id, and every
//  price is spent through the simulation's own economy calls. These tests press
//  the rows rather than the functions behind them, so the wiring itself is what
//  is being checked: the market, the board, the yard, the hiring hall, the
//  tavern, the hold, the science room, and the four floating panels.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/JsNumber.h"
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/Popups.h"
#include "ui/Shell.h"
#include "ui/StationScreen.h"
#include "ui/StoresScreen.h"
#include "ui/Widgets.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

// A voyage docked at the first port, which is where every station test starts.
ship::Simulation Docked() {
  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  sim.MutableMap().here = World().ports.front().id;
  sim.MutableState().docked = true;
  return sim;
}

const ship::StationColumn& Column(const ship::StationScreenModel& model,
                                 const std::vector<std::string>& titles) {
  for (const ship::StationColumn& column : model.columns) {
    for (const std::string& title : titles) {
      if (column.title.find(title) != std::string::npos) { return column; }
    }
  }
  return model.columns.front();
}

const ship::StationRow* Row(const ship::StationColumn& column,
                           const std::string& id) {
  for (const ship::StationRow& row : column.rows) {
    if (row.id == id) { return &row; }
  }
  return nullptr;
}

bool HasRowStarting(const ship::StationColumn& column, const std::string& prefix) {
  for (const ship::StationRow& row : column.rows) {
    if (row.id.compare(0, prefix.size(), prefix) == 0) { return true; }
  }
  return false;
}

const ship::StoreRowView* StoreRow(const ship::StoresScreenModel& model,
                                  const std::string& prefix) {
  for (const ship::StoreRowView& row : model.rows) {
    if (row.id.compare(0, prefix.size(), prefix) == 0) { return &row; }
  }
  return nullptr;
}

std::filesystem::path Scratch() {
  const std::filesystem::path dir =
      std::filesystem::path("build") / "test-saves";
  std::filesystem::create_directories(dir);
  return dir;
}

void Wipe(const std::filesystem::path& dir) {
  for (int slot = 1; slot <= 3; ++slot) {
    std::error_code ignored;
    std::filesystem::remove(dir / ship::Simulation::SlotFilename(slot), ignored);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
//  The station window
// ---------------------------------------------------------------------------
TEST_CASE("the station is five columns over the authored port", "[station]") {
  ship::Simulation sim = Docked();
  const std::string portId = World().ports.front().id;
  const ship::StationScreenModel model = ship::BuildStationScreen(sim, portId);

  CHECK(model.head.kind == "STATION");
  CHECK(model.head.title == World().ports.front().name);
  CHECK(model.head.sub ==
        "Browse freely. Each transaction, contract or departure costs one action.");
  CHECK(model.portId == portId);
  CHECK(model.docked);
  REQUIRE(model.columns.size() == 5);
  CHECK(model.columns[0].title ==
        "Market - purse " + ship::js::NumToString(sim.State().shiplets) + " sh");
  CHECK(model.columns[1].title == "Sell cargo and survey data");
  CHECK(model.columns[2].title == "Mission board");
  CHECK(model.columns[3].title == "Shipyard and crew");
  CHECK(model.columns[4].title == "Tavern and departure");

  // The market's own sentence, and its five fixed-price stores in order.
  REQUIRE_FALSE(model.columns[0].notes.empty());
  CHECK(model.columns[0].notes[0] ==
        "Buy supplies and cargo. A different port may pay a different price.");
  REQUIRE(model.columns[0].rows.size() >= 5);
  CHECK(model.columns[0].rows[0].label == "Fuel cell - 45 sh");
  CHECK(model.columns[0].rows[1].label == "Repair kit - 70 sh");
  CHECK(model.columns[0].rows[2].label == "Medkit - 50 sh");
  CHECK(model.columns[0].rows[3].label == "O2 canister - 25 sh");
  CHECK(model.columns[0].rows[4].label == "Six rations - 20 sh");

  // Market cargo is priced at ceil(value * 1.2) and carries its volume.
  for (const ship::Good& good : sim.Market()) {
    const ship::StationRow* row = Row(model.columns[0], "buy-good:" + good.id);
    REQUIRE(row != nullptr);
    const double price = std::ceil(good.value * 1.2);
    CHECK(row->label == "Buy " + good.name + " - " + ship::js::NumToString(price) +
                            " sh / " + ship::js::NumToString(good.vol) + " m3");
  }

  // The hold line is the reference's sentence: used, capacity, reserved.
  const std::string hold = ship::js::NumToString(sim.CargoUsed()) + " of " +
                           ship::js::NumToString(sim.CargoStore().capacity) +
                           " m3 used; " + ship::js::NumToString(sim.CargoReserved()) +
                           " m3 reserved.";
  CHECK(model.holdLine == hold);

  // The tavern keeps the tip the port authored, and the departure row.
  CHECK_FALSE(model.tip.empty());
  CHECK(model.columns[4].notes[0] == model.tip);
  CHECK(model.columns[4].notes[1] == "Station training only finishes while docked.");
  const ship::StationRow* undock = Row(model.columns[4], "undock");
  REQUIRE(undock != nullptr);
  CHECK(undock->label == "Undock");
  CHECK(undock->enabled);
}

TEST_CASE("a purchase in the market costs one action and the price",
          "[station][market]") {
  ship::Simulation sim = Docked();
  const double actions = sim.ActionCount();
  const double purse = sim.State().shiplets;
  const double cells = sim.State().fuelCells;

  REQUIRE(ship::PressStationAction(sim, "buy-fuel"));
  CHECK(sim.State().fuelCells == cells + 1);
  CHECK(sim.State().shiplets == purse - 45);
  CHECK(sim.ActionCount() == actions + 1);

  const double kits = sim.State().repairKits;
  REQUIRE(ship::PressStationAction(sim, "buy-repair"));
  CHECK(sim.State().shiplets == purse - 45 - 70);
  CHECK(sim.State().repairKits == kits + 1);

  const double medkits = sim.State().medkits;
  REQUIRE(ship::PressStationAction(sim, "buy-medkit"));
  CHECK(sim.State().medkits == medkits + 1);
  const double rack = sim.MessRack();
  REQUIRE(ship::PressStationAction(sim, "buy-rations"));
  CHECK(sim.MessRack() == rack + 6);

  // A bay closes when the purse cannot reach it.
  sim.MutableState().shiplets = 10;
  const ship::StationScreenModel poor = ship::BuildStationScreen(sim, sim.Map().here);
  CHECK_FALSE(Row(poor.columns[0], "buy-fuel")->enabled);
  CHECK_FALSE(ship::PressStationAction(sim, "buy-fuel"));
}

TEST_CASE("buying cargo fills the hold and selling empties it",
          "[station][market]") {
  ship::Simulation sim = Docked();
  const ship::Good& good = sim.Market().front();
  sim.MutableState().shiplets = 9000;
  sim.MutableCargo().capacity = 200;   // room for a lot or two
  const double used = sim.CargoUsed();
  const std::size_t lots = sim.Goods().size();

  REQUIRE(ship::PressStationAction(sim, "buy-good:" + good.id));
  REQUIRE(sim.Goods().size() == lots + 1);
  CHECK(sim.CargoUsed() == used + good.vol);
  const std::string lotId = sim.Goods().front().id;

  const ship::StationScreenModel loaded = ship::BuildStationScreen(sim, sim.Map().here);
  const ship::StationRow* sale = Row(loaded.columns[1], "sell:" + lotId);
  REQUIRE(sale != nullptr);
  CHECK(sale->enabled);
  REQUIRE(ship::PressStationAction(sim, "sell:" + lotId));

  // The hold is back to what it was and the purse has taken the price there
  // and back.
  CHECK(sim.Goods().size() == lots);
  CHECK(sim.CargoUsed() == used);

  // Contract freight is not for sale: it is a note, not a row.
  ship::Simulation freight = Docked();
  freight.MutableMap().here = "tolme";   // where the consignment waits
  freight.MutableState().docked = true;
  freight.MutableState().shiplets = 9000;
  freight.MutableCargo().capacity = 200;
  REQUIRE(ship::PressStationAction(freight, "contract-accept:freight"));
  const ship::StationScreenModel board =
      ship::BuildStationScreen(freight, freight.Map().here);
  for (const ship::StationRow& row : board.columns[1].rows) {
    CHECK(row.id.compare(0, 11, "contract-") != 0);
  }
  CHECK_FALSE(HasRowStarting(board.columns[1], "sell:contract-"));
}

TEST_CASE("the mission board accepts, tracks and pays a contract", "[station][board]") {
  ship::Simulation sim = Docked();
  // freight starts at Tolme, so dock there to accept it.
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = true;
  sim.MutableCargo().capacity = 200;   // the consignment needs 8 m3

  const double actions = sim.ActionCount();
  const double purse = sim.State().shiplets;
  const ship::StationScreenModel board = ship::BuildStationScreen(sim, "tolme");

  // Every contract has its status note, in the reference's words.
  bool sawFreight = false;
  for (const std::string& note : board.columns[2].notes) {
    if (note.find("Tolme Anchorage") == std::string::npos) { continue; }
    sawFreight = true;
  }
  CHECK(sawFreight);

  const ship::StationRow* accept = Row(board.columns[2], "contract-accept:freight");
  REQUIRE(accept != nullptr);
  CHECK(accept->enabled);
  CHECK(accept->cost == "1 action");

  const double usedBefore = sim.CargoUsed();
  REQUIRE(ship::PressStationAction(sim, "contract-accept:freight"));
  CHECK(sim.State().contracts.at("freight") == "active");
  CHECK(sim.ActionCount() == actions + 1);
  CHECK(sim.Goods().back().contractId == "freight");
  CHECK(sim.CargoUsed() == usedBefore + 8);

  // Accepted work leaves the board and appears as a note only.
  const ship::StationScreenModel accepted = ship::BuildStationScreen(sim, "tolme");
  CHECK_FALSE(HasRowStarting(accepted.columns[2], "contract-accept:freight"));

  // Ready work can be collected: the reward lands and the state says complete.
  sim.MutableState().contracts["freight"] = "ready";
  const ship::StationScreenModel ready = ship::BuildStationScreen(sim, "tolme");
  const ship::StationRow* collect = Row(ready.columns[2], "contract-collect:freight");
  REQUIRE(collect != nullptr);
  CHECK(collect->label.find("420 sh") != std::string::npos);
  const double before = sim.State().shiplets;
  REQUIRE(ship::PressStationAction(sim, "contract-collect:freight"));
  CHECK(sim.State().shiplets == before + 420);
  CHECK(sim.State().contracts.at("freight") == "complete");
  CHECK(sim.ActionCount() == actions + 2);
}

TEST_CASE("the board offers, collects and abandons a repeatable commission",
          "[station][board]") {
  ship::Simulation sim = Docked();
  const ship::StationScreenModel model =
      ship::BuildStationScreen(sim, sim.Map().here);

  // The commission heading comes first, then the three offers.
  bool heading = false;
  for (const std::string& note : model.columns[2].notes) {
    if (note == "Repeatable commissions") { heading = true; }
  }
  CHECK(heading);
  REQUIRE(sim.State().commissions.active == std::nullopt);
  for (const char* kind : {"visit", "scan", "salvage"}) {
    const ship::StationRow* offer = Row(model.columns[2], std::string("commission-accept:") + kind);
    REQUIRE(offer != nullptr);
    CHECK(offer->label.compare(0, 8, "Accept: ") == 0);
    CHECK(offer->enabled);
  }

  // The offer the board shows is the one acceptance hands over.
  const std::optional<ship::Commission> shown = sim.CommissionOffer("visit");
  REQUIRE(shown.has_value());
  const double actions = sim.ActionCount();
  REQUIRE(ship::PressStationAction(sim, "commission-accept:visit"));
  REQUIRE(sim.State().commissions.active.has_value());
  CHECK(sim.State().commissions.active->kind == "visit");
  CHECK(sim.State().commissions.active->target == shown->target);
  CHECK(sim.State().commissions.active->reward == 450);
  CHECK(sim.ActionCount() == actions + 1);

  // A live commission cannot take another, and its collection waits on the goal.
  const ship::StationScreenModel live = ship::BuildStationScreen(sim, sim.Map().here);
  CHECK_FALSE(HasRowStarting(live.columns[2], "commission-accept:"));
  const ship::StationRow* collect = Row(live.columns[2], "commission-collect");
  REQUIRE(collect != nullptr);
  CHECK_FALSE(collect->enabled);
  CHECK(collect->label == "Collect commission - 450 sh");
  CHECK_FALSE(ship::PressStationAction(sim, "commission-collect"));

  // Abandoning rotates the offers rather than paying them.
  const double serial = sim.State().commissions.serial;
  REQUIRE(ship::PressStationAction(sim, "commission-abandon"));
  CHECK(sim.State().commissions.active == std::nullopt);
  CHECK(sim.State().commissions.serial == serial + 1);
  CHECK(sim.State().commissions.completed == 0);
}

TEST_CASE("a full hold or a wrong port closes the board", "[station][board]") {
  ship::Simulation sim = Docked();
  const ship::StationScreenModel away =
      ship::BuildStationScreen(sim, sim.Map().here);
  // Freight belongs to Tolme: accepting it elsewhere is refused by the row.
  const ship::StationRow* freight = Row(away.columns[2], "contract-accept:freight");
  REQUIRE(freight != nullptr);
  CHECK_FALSE(freight->enabled);

  // Fill the hold: no freight lot fits, so the row closes even at the right port.
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = true;
  sim.MutableCargo().capacity = 4;
  const ship::StationScreenModel full = ship::BuildStationScreen(sim, "tolme");
  CHECK_FALSE(Row(full.columns[2], "contract-accept:freight")->enabled);
}

TEST_CASE("hiring respects the berth limit and the purse", "[station][hire]") {
  ship::Simulation sim = Docked();
  sim.MutableState().shiplets = 9000;
  const ship::StationScreenModel model =
      ship::BuildStationScreen(sim, sim.Map().here);

  const ship::CandidateDefinition& candidate = World().candidates.front();
  const std::string id = "hire:" + candidate.crew.id;
  const ship::StationRow* row = Row(model.columns[3], id);
  REQUIRE(row != nullptr);
  CHECK(row->label == "Hire " + candidate.crew.name + " - " +
                         ship::js::NumToString(candidate.fee) + " sh");
  CHECK(row->enabled);

  // The hall lists each candidate in the panel's own words.
  bool described = false;
  for (const std::string& note : model.columns[3].notes) {
    if (note.find(candidate.crew.name) == 0) { described = true; }
  }
  CHECK(described);

  const std::size_t crew = sim.Crew().size();
  const double purse = sim.State().shiplets;
  const double actions = sim.ActionCount();
  REQUIRE(ship::PressStationAction(sim, id));
  CHECK(sim.Crew().size() == crew + 1);
  CHECK(sim.State().shiplets == purse - candidate.fee);
  CHECK(sim.ActionCount() == actions + 1);

  // Nobody is hired twice, and a full ship stops hiring.
  const ship::StationScreenModel after = ship::BuildStationScreen(sim, sim.Map().here);
  CHECK_FALSE(Row(after.columns[3], id)->enabled);
  CHECK_FALSE(ship::PressStationAction(sim, id));

  ship::Simulation full = Docked();
  full.MutableState().shiplets = 9000;
  full.MutableCargo();
  while (full.Crew().size() < static_cast<std::size_t>(full.Ship().Berths())) {
    ship::CrewMember spare = full.Crew().front();
    spare.id = "spare-" + std::to_string(full.Crew().size());
    full.MutableCrew().push_back(spare);
  }
  const ship::StationScreenModel crowded = ship::BuildStationScreen(full, full.Map().here);
  CHECK_FALSE(Row(crowded.columns[3], id)->enabled);
}

TEST_CASE("the yard stages refits, the Kestrel and a hull service", "[station][yard]") {
  ship::Simulation sim = Docked();
  sim.MutableState().shiplets = 9000;
  const ship::StationScreenModel model =
      ship::BuildStationScreen(sim, sim.Map().here);

  // The Wren's own notice, verbatim.
  REQUIRE_FALSE(model.columns[3].notes.empty());
  CHECK(model.columns[3].notes[0].find(
            "Your compact Wren: " +
            ship::js::NumToString(sim.CargoStore().capacity) + " m3 cargo, 6 berths.") == 0);
  CHECK(model.columns[3].notes[0].find("SC Tessellate") != std::string::npos);

  const ship::StationRow* shipRow = Row(model.columns[3], "ship-buy");
  REQUIRE(shipRow != nullptr);
  CHECK(shipRow->label == "Buy larger ship SC Tessellate - 4,000 sh");
  CHECK(shipRow->confirm ==
        "Buy SC Tessellate for 4,000 shiplets? The crew, cargo, equipment, refits, "
        "tasks and voyage records transfer. This purchase is permanent.");

  // A refit is one action, and the hold really grows.
  const double capacity = sim.CargoStore().capacity;
  const double bayOne = sim.ShipCargoCapacity(sim.State().shipClass, 1);
  REQUIRE(ship::PressStationAction(sim, "upgrade:bay"));
  CHECK(sim.CargoStore().capacity == std::max(capacity, bayOne));
  CHECK(sim.State().upgrades.at("bay"));

  // The second bay is staged behind the first.
  const ship::StationScreenModel staged = ship::BuildStationScreen(sim, sim.Map().here);
  CHECK_FALSE(Row(staged.columns[3], "upgrade:bay")->enabled);
  CHECK(Row(staged.columns[3], "upgrade:bay2")->enabled);

  // A hull service is a flat 120 sh for half the missing structure.
  sim.ClampStat("hull", -40.0);
  const double hull = sim.StatValue("hull");
  REQUIRE(hull < 100);
  REQUIRE(ship::PressStationAction(sim, "hull-service"));
  CHECK(sim.StatValue("hull") == std::min(100.0, hull + 50));
  CHECK(sim.State().shiplets == 9000 - 500 - 120);
  CHECK_FALSE(ship::PressStationAction(sim, "hull-service"));  // nothing missing

  // Trading up: the class, the deck plan and the berths all change.
  sim.MutableState().shiplets = 4200;
  const ship::StationScreenModel big = ship::BuildStationScreen(sim, sim.Map().here);
  CHECK(Row(big.columns[3], "ship-buy")->enabled);
  REQUIRE(ship::PressStationAction(sim, "ship-buy"));
  CHECK(sim.State().shipClass == "kestrel");
  CHECK(sim.Ship().Berths() == 14);
  CHECK(sim.State().shiplets == 200);
  const ship::StationScreenModel kestrel = ship::BuildStationScreen(sim, sim.Map().here);
  CHECK(kestrel.columns[3].notes[0].find("Your Kestrel-series ship:") == 0);
  CHECK_FALSE(HasRowStarting(kestrel.columns[3], "ship-buy"));
  CHECK_FALSE(ship::PressStationAction(sim, "ship-buy"));
}

TEST_CASE("undocking leaves the station and costs one action", "[station]") {
  ship::Simulation sim = Docked();
  const double actions = sim.ActionCount();
  sim.MutableState().activeTab = "station:" + sim.Map().here;
  REQUIRE(ship::PressStationAction(sim, "undock"));
  CHECK_FALSE(sim.State().docked);
  CHECK(sim.State().activeTab == "home");
  CHECK(sim.ActionCount() == actions + 1);
  CHECK_FALSE(ship::PressStationAction(sim, "undock"));
}

// ---------------------------------------------------------------------------
//  The stores
// ---------------------------------------------------------------------------
TEST_CASE("the cargo hold counts its lots and jettisons them", "[stores][cargo]") {
  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  // An empty hold of the Wren's original size, so the numbers are the words.
  sim.MutableGoods().clear();
  sim.MutableCargo().capacity = 24;
  const ship::StoresScreenModel empty = ship::BuildStoresScreen(sim, "cargo");
  CHECK(empty.cargo);
  CHECK(empty.head.kind == "ROOM");
  CHECK(empty.head.title == "Cargo Hold");
  CHECK(empty.notice == "0 of 24 m3 used; 0 m3 reserved. Sell lots at a station.");
  CHECK(empty.hint.find("During a crossing, each bay can recover a small salvage lot.") == 0);
  CHECK(empty.tally == "0 lots aboard.");
  REQUIRE_FALSE(empty.rows.empty());
  CHECK(empty.rows.back().readingOnly);
  CHECK(empty.rows.back().label == "Nothing stored.");

  // Three bays, each a three-state button rather than a command row.
  CHECK(empty.buttons.size() >= 3);
  CHECK(empty.buttons[0].taskId == "salvage-0");
  CHECK(empty.buttons[0].state == ship::ButtonState::Off);  // no crossing, no salvage

  sim.MutableGoods().push_back(
      ship::GoodsLot{"lot-1", "Refined ore", "Bulk metal", 14, 280, false, ""});
  const ship::StoresScreenModel loaded = ship::BuildStoresScreen(sim, "cargo");
  const ship::StoreRowView* row = StoreRow(loaded, "jettison:lot-1");
  REQUIRE(row != nullptr);
  CHECK(row->label == "Jettison Refined ore (14 m3)");
  CHECK(row->cost == "1 action");
  CHECK(row->confirm == "Jettison Refined ore?");

  const double actions = sim.ActionCount();
  REQUIRE(ship::PressStoresAction(sim, "jettison:lot-1"));
  CHECK(sim.Goods().empty());
  CHECK(sim.ActionCount() == actions + 1);
  CHECK_FALSE(ship::PressStoresAction(sim, "jettison:lot-1"));
}

TEST_CASE("salvage is offered only mid-crossing", "[stores][salvage]") {
  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  sim.MutableState().docked = false;
  ship::Task* travel = sim.AddOngoing("travel", "Under way", 40.0, "travel", "vega");
  REQUIRE(travel != nullptr);
  travel->state = "running";

  const ship::StoresScreenModel crossing = ship::BuildStoresScreen(sim, "cargo");
  const ship::StoreRowView* bay = StoreRow(crossing, "salvage:0");
  REQUIRE(bay != nullptr);
  CHECK(bay->label == "Recover route salvage - bay 1");
  CHECK(bay->cost == "1 action + 2 work");
  CHECK(bay->enabled);
  CHECK(crossing.buttons[0].state != ship::ButtonState::Off);

  const double actions = sim.ActionCount();
  const double energy = sim.StatValue("energy");
  REQUIRE(ship::PressStoresAction(sim, "salvage:0"));
  CHECK(sim.ActionCount() == actions + 1);
  CHECK(sim.StatValue("energy") == energy - 4);
  const ship::Task* work = sim.FindOngoing("salvage-0");
  REQUIRE(work != nullptr);
  CHECK(work->total == 2);
  CHECK(work->vol == 3);
  CHECK(sim.CargoReserved() == 3);

  // A second bay is still open; the same bay is not.
  const ship::StoresScreenModel busy = ship::BuildStoresScreen(sim, "cargo");
  CHECK_FALSE(StoreRow(busy, "salvage:0")->enabled);
  CHECK(StoreRow(busy, "salvage:1")->enabled);
}

TEST_CASE("the science room files and packages surveys", "[stores][science]") {
  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  sim.MutableSurvey().clear();
  const ship::StoresScreenModel empty = ship::BuildStoresScreen(sim, "science");
  CHECK_FALSE(empty.cargo);
  CHECK(empty.head.title == "Science Room");
  CHECK(empty.notice ==
        "Deep scans are filed here. Packaging costs one action, takes two more, and "
        "adds value.");
  CHECK(empty.tally == "0 records filed.");

  sim.MutableSurvey().push_back(
      ship::SurveyRecord{"survey-1", "Pell stratigraphy", "pell-world", 320, "filed"});
  const ship::StoresScreenModel filed = ship::BuildStoresScreen(sim, "science");
  const ship::StoreRowView* row = StoreRow(filed, "package:survey-1");
  REQUIRE(row != nullptr);
  CHECK(row->label == "Package: Pell stratigraphy (1 action + 2 work)");
  CHECK(row->cost == "1 action + 2 work");
  CHECK(row->enabled);

  const double actions = sim.ActionCount();
  REQUIRE(ship::PressStoresAction(sim, "package:survey-1"));
  CHECK(sim.ActionCount() == actions + 1);
  REQUIRE(sim.FindOngoing("pkg-survey-1") != nullptr);

  // While it is being packaged the row cannot be pressed again.
  CHECK_FALSE(ship::PressStoresAction(sim, "package:survey-1"));

  // A packaged record reads as done, and sells at the station for its value.
  sim.MutableSurvey().front().state = "packaged";
  const ship::StoresScreenModel done = ship::BuildStoresScreen(sim, "science");
  CHECK(StoreRow(done, "package:survey-1")->label ==
        "Packaged: Pell stratigraphy (320 sh)");
  CHECK(StoreRow(done, "package:survey-1")->cost == "done");
  CHECK_FALSE(StoreRow(done, "package:survey-1")->enabled);
}

// ---------------------------------------------------------------------------
//  The four popups
// ---------------------------------------------------------------------------
TEST_CASE("the foot's four keys open the four panels", "[popups]") {
  CHECK(ship::PopupKeys() ==
        std::vector<std::string>{"manifest", "log", "files", "settings"});
  CHECK(ship::PopupTitle("manifest") == "Ship Manifest");
  CHECK(ship::PopupTitle("log") == "Activity Log");
  CHECK(ship::PopupTitle("files") == "Save / Load Game");
  CHECK(ship::PopupTitle("settings") == "Settings");

  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  const ship::PopupModel manifest = ship::BuildPopup(sim, "manifest");
  REQUIRE(manifest.sections.size() == 2);
  CHECK(manifest.sections[0].title == "Crew");
  CHECK(manifest.sections[0].rows.size() == sim.Crew().size());
  CHECK(manifest.sections[0].rows[0].key == sim.Crew().front().name);
  CHECK(manifest.sections[1].title == "Compartments");
  CHECK(manifest.sections[1].rows.size() == sim.Rooms().size());
  for (const ship::PopupSection& section : manifest.sections) {
    for (const ship::PopupRow& row : section.rows) {
      CHECK_FALSE(row.value.empty());
    }
  }

  // The log reads newest first, which is the last thing that happened.
  sim.PerformAction("A test line");
  const ship::PopupModel log = ship::BuildPopup(sim, "log");
  REQUIRE_FALSE(log.lines.empty());
  CHECK(log.lines.front().find("A test line") != std::string::npos);

  // Reading a popup is free.
  const double actions = sim.ActionCount();
  CHECK_FALSE(ship::PressPopupAction(sim, "manifest", "anything", "", "", nullptr));
  CHECK_FALSE(ship::PressPopupAction(sim, "log", "anything", "", "", nullptr));
  CHECK(sim.ActionCount() == actions);
}

TEST_CASE("settings invert their own labels and change nothing else", "[popups][settings]") {
  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  const ship::PopupModel before = ship::BuildPopup(sim, "settings");
  REQUIRE(before.toggles.size() == 6);
  CHECK(before.notice ==
        "Preferences are free to change and are saved with the voyage.");
  CHECK(before.themes.size() == 5);
  CHECK(before.theme == "classic");
  CHECK(before.toggles[0].id == "music");
  CHECK(before.toggles[2].id == "reducedMotion");
  CHECK(before.toggles[5].id == "hideCrewLabels");

  // A label says what the click will do, so it inverts with the value.
  for (const ship::PopupToggle& toggle : before.toggles) {
    std::string message;
    const double actions = sim.ActionCount();
    REQUIRE(ship::PressPopupAction(sim, "settings", "toggle", toggle.id, "", &message));
    CHECK(sim.ActionCount() == actions);  // free
  }
  const ship::PopupModel after = ship::BuildPopup(sim, "settings");
  for (std::size_t i = 0; i < before.toggles.size(); ++i) {
    CHECK(before.toggles[i].on != after.toggles[i].on);
    CHECK(before.toggles[i].label != after.toggles[i].label);
  }
  CHECK(after.toggles[0].label == "Ambient soundtrack: Off");
  CHECK(after.toggles[2].label == "Enable decorative motion");
  CHECK(after.toggles[5].label == "Show crew labels");

  // The five themes are the reference's, in its order, and each is selectable.
  REQUIRE(before.themeLabels.size() == 5);
  for (const std::string& key : before.themes) {
    REQUIRE(ship::PressPopupAction(sim, "settings", "theme", key, "", nullptr));
    CHECK(sim.State().options.theme == key);
  }
  CHECK_FALSE(ship::PressPopupAction(sim, "settings", "theme", "no-such-theme", "", nullptr));

  // The volume is a number between nought and one.
  REQUIRE(ship::PressPopupAction(sim, "settings", "audio", "0.25", "", nullptr));
  CHECK(sim.State().options.audioLevel == 0.25);
}

TEST_CASE("three save slots save, load and refuse a corrupt file",
          "[popups][files]") {
  const std::filesystem::path dir = Scratch();
  Wipe(dir);
  const std::string root = dir.string();

  ship::Simulation sim = ship::Simulation::NewVoyage(World());
  const ship::PopupModel files = ship::BuildPopup(sim, "files", root);
  CHECK(files.notice ==
        "Three independent slots. Slot 1 uses the original save. Loading replaces the "
        "current voyage; save first if needed.");
  REQUIRE(files.slots.size() == 3);
  CHECK(files.slots[0].label == "Slot 1 (original)");
  CHECK(files.slots[1].label == "Slot 2");
  CHECK(files.slots[2].label == "Slot 3");
  CHECK(files.slots[0].summary == "Empty");

  // Save slot 2, spend some actions, then load the saved voyage back.
  const double actions = sim.ActionCount();
  std::string message;
  REQUIRE(ship::PressPopupAction(sim, "files", "save", "2", root, &message));
  CHECK(message == "Saved to " + ship::Simulation::SlotFilename(2) + " beside the HTA.");
  CHECK(std::filesystem::exists(dir / ship::Simulation::SlotFilename(2)));

  const ship::PopupModel written = ship::BuildPopup(sim, "files", root);
  CHECK(written.slots[1].written);
  CHECK(written.slots[1].summary != "Empty");

  sim.PerformAction("A line the save does not know");
  sim.PerformAction("Another line");
  REQUIRE(sim.ActionCount() == actions + 2);

  REQUIRE(ship::PressPopupAction(sim, "files", "load", "2", root, &message));
  CHECK(message == "Saved voyage restored.");
  CHECK(sim.ActionCount() == actions);

  // A corrupt save is refused, and the voyage is left exactly as it was.
  const std::filesystem::path broken = dir / ship::Simulation::SlotFilename(3);
  {
    std::ofstream out(broken, std::ios::binary);
    out << "this is not a voyage";
  }
  const std::string trace = sim.TraceJson();
  message.clear();
  CHECK_FALSE(ship::PressPopupAction(sim, "files", "load", "3", root, &message));
  CHECK(message.find("Could not load: ") == 0);
  CHECK(sim.TraceJson() == trace);
  CHECK_FALSE(ship::PressPopupAction(sim, "files", "load", "9", root, &message));

  // A new voyage is free and keeps the reader's preferences.
  sim.MutableState().options.largeText = true;
  sim.MutableState().reducedMotion = true;
  REQUIRE(ship::PressPopupAction(sim, "files", "new-voyage", "1", root, &message));
  CHECK(message == "A new voyage has begun.");
  CHECK(sim.ActionCount() == 0);
  CHECK(sim.State().options.largeText);
  CHECK(sim.State().reducedMotion);

  Wipe(dir);
}

// ---------------------------------------------------------------------------
//  Routing
// ---------------------------------------------------------------------------
TEST_CASE("the router sends the station, the hold and the lab to their windows",
          "[popups][router][stores]") {
  using ship::ScreenKind;
  ship::Simulation sim = Docked();
  const std::string portId = sim.Map().here;

  const ship::Tab stationTab{"station:" + portId, portId, "station",
                             World().ports.front().name, "STATION", ""};
  CHECK(ship::ScreenKindFor(stationTab) == ScreenKind::Station);
  const ship::TaskScreenModel station = ship::BuildTabScreen(sim, stationTab);
  CHECK(station.head.kind == "STATION");
  CHECK(station.panels.size() == 5);
  CHECK(station.panelRows.size() == 5);
  CHECK(station.panelRowIds.size() == 5);
  CHECK(station.panels[0].title.find("Market - purse") == 0);
  CHECK_FALSE(station.panelRows[0].empty());
  CHECK(station.panelRowIds[0][0] == "buy-fuel");

  const ship::Tab cargo{"room:cargo", "cargo", "room", "Cargo Hold", "ROOM", ""};
  CHECK(ship::ScreenKindFor(cargo) == ScreenKind::Cargo);
  const ship::TaskScreenModel hold = ship::BuildTabScreen(sim, cargo);
  CHECK(hold.head.title == "Cargo Hold");
  REQUIRE(hold.panels.size() == 1);
  CHECK(hold.panels[0].title == "Cargo manifest");
  REQUIRE_FALSE(hold.panelRows[0].empty());
  CHECK(hold.panelRowIds[0][0] == "salvage:0");
  CHECK(hold.progress.size() >= 3);

  const ship::Tab lab{"room:science", "science", "room", "Science Room", "ROOM", ""};
  CHECK(ship::ScreenKindFor(lab) == ScreenKind::Science);
  const ship::TaskScreenModel science = ship::BuildTabScreen(sim, lab);
  CHECK(science.panels[0].title == "Survey records");
}

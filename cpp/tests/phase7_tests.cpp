// ============================================================================
//  phase7_tests.cpp — the fitted rooms, the Simulacron, the reports and the
//  logbook, checked as data.
//  ---------------------------------------------------------------------------
//  Phase 7's contract: every fitted bay opens a real window, every project
//  reports its own three-state button, the Simulacron never invents a thought,
//  and a commendation can be claimed exactly once. None of this needs a window,
//  so none of it is tested in one.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/AwardsScreen.h"
#include "ui/EventScreen.h"
#include "ui/RoomsScreen.h"
#include "ui/SimCoreScreen.h"
#include "ui/Widgets.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::ReferenceVoyage(World()); }

const ship::ActionRowView* RowWith(const std::vector<ship::ActionRowView>& rows,
                                   const std::string& id) {
  for (const ship::ActionRowView& row : rows) {
    if (row.id == id) { return &row; }
  }
  return nullptr;
}

// A fresh voyage has no report yet; the hull's first one is due at action 45.
std::string SomeReport(ship::Simulation& sim) {
  for (int i = 0; i < 120 && sim.Events().empty(); ++i) { sim.StandWatch(); }
  return sim.Events().empty() ? std::string() : sim.Events()[0].id;
}

ship::Incident CopyIncident(const ship::Simulation& sim, const std::string& id) {
  for (const ship::Incident& incident : sim.Events()) {
    if (incident.id == id) { return incident; }
  }
  return ship::Incident{};
}

bool CardStates(const ship::AwardsScreenModel& awards, const std::string& suffix) {
  for (const ship::ActionRowView& row : awards.rows) {
    if (row.label.size() >= suffix.size() &&
        row.label.compare(row.label.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
//  The fourteen fitted bays
// ---------------------------------------------------------------------------
TEST_CASE("fourteen fitted bays plus the bridge each own a window", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  // Fourteen fitted bays carry a scene; the hold's other seven are hull rooms.
  int fitted = 0;
  for (const auto& scene : World().roomScenes) {
    if (scene.first == "bridge" || (scene.first[0] == 'r' && scene.first.size() <= 3 &&
                                    std::stoi(scene.first.substr(1)) <= 13)) {
      fitted += 1;
    }
  }
  CHECK(fitted == 14);
  CHECK(World().counts.fittedRooms == 14);
  const char* rooms[] = {"r1", "r2", "r3", "r4", "r5", "r6", "r7",
                         "r8", "r9", "r10", "r11", "r12", "r13", "bridge"};
  for (const char* id : rooms) {
    CHECK(ship::RoomHasScreen(sim, id));
    const ship::RoomScreenModel model = ship::BuildRoomScreen(sim, id);
    CHECK(model.head.kind == "ROOM");
    CHECK_FALSE(model.head.title.empty());
    CHECK(model.body.title == "Ship systems");
    // The vignette travels with the room, glyph and accent included.
    CHECK_FALSE(model.glyph.empty());
    CHECK_FALSE(model.caption.empty());
    CHECK(model.accent.size() == 7);
  }
  // A hold is not a fitted room.
  CHECK_FALSE(ship::RoomHasScreen(sim, "cargo"));
}

TEST_CASE("every project button names its own three states", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  int found = 0;
  for (const ship::ProjectDef& project : sim.World().projects) {
    const ship::RoomScreenModel model = ship::BuildRoomScreen(sim, project.room);
    bool seen = false;
    for (const ship::ProgressButton& button : model.buttons) {
      if (button.taskId != "project-" + project.id) { continue; }
      seen = true;
      // Fresh and powered: the button either offers itself or explains why not,
      // and it never reports work that is not happening.
      CHECK(button.state != ship::ButtonState::Running);
      CHECK_FALSE(button.text.empty());
      CHECK_FALSE(button.category.empty());  // every button carries a clock tier
    }
    if (project.id == "decode") {
      CHECK_FALSE(seen);  // no signal on the wire, nothing to decode
    } else {
      CHECK(seen);
    }
    found += 1;
  }
  CHECK(found == 10);
  // The dream engine's button lives on the Simulacron's own screen.
  const ship::SimScreenModel core = ship::BuildSimCoreScreen(sim);
  REQUIRE_FALSE(core.buttons.empty());
  CHECK(core.buttons[0].taskId == "project-dream");
  // With a signal on the wire, the relay offers its decode button instead.
  sim.MutableState().sim.signal = "intercepted";
  const ship::RoomScreenModel comms = ship::BuildRoomScreen(sim, "r4");
  bool decode = false;
  for (const ship::ProgressButton& button : comms.buttons) {
    if (button.taskId == "project-decode") { decode = true; }
  }
  CHECK(decode);

  // Without power the same button is inert, and says why.
  sim.FindStat("energy")->value = 0;
  for (const ship::ProjectDef& project : sim.World().projects) {
    const ship::RoomScreenModel model = ship::BuildRoomScreen(sim, project.room);
    for (const ship::ProgressButton& button : model.buttons) {
      if (button.taskId != "project-" + project.id) { continue; }
      CHECK(button.state == ship::ButtonState::Off);
      // The room's own note says why, verbatim from the core.
      CHECK(sim.ProjectBlock(project.id).find("energy") != std::string::npos);
    }
  }
}

TEST_CASE("a running project reserves its own room, not the ship", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  sim.FindStat("energy")->value = 100;
  // A kit needs an uncommitted salvage lot to break up, and one insight.
  ship::GoodsLot lot;
  lot.id = "recovery-test";
  lot.name = "Recovered salvage";
  lot.kind = "Salvage";
  sim.MutableGoods().push_back(lot);
  CHECK(sim.StartProject("kit"));
  CHECK(sim.ProjectBlock("kit") == "Already in progress.");
  // A different project is free to start, but its own button shows the lock.
  sim.MutableState().sim.insight = 1;
  CHECK(sim.StartProject("sensors"));
  const ship::RoomScreenModel workshop = ship::BuildRoomScreen(sim, "r2");
  for (const ship::ProgressButton& button : workshop.buttons) {
    CHECK(button.state == ship::ButtonState::Running);
  }
}

TEST_CASE("the bridge, the power bus and the reserves speak in points", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  const ship::RoomScreenModel bridge = ship::BuildRoomScreen(sim, "bridge");
  REQUIRE(RowWith(bridge.rows, "open-nav") != nullptr);
  CHECK(RowWith(bridge.rows, "open-nav")->cost == "free");
  CHECK(RowWith(bridge.rows, "open-nav")->enabled);

  // A split needs a cell and room in both tanks.
  sim.FindStat("energy")->value = 50;
  sim.FindStat("fuel")->value = 50;
  sim.MutableState().fuelCells = 1;
  const ship::RoomScreenModel split = ship::BuildRoomScreen(sim, "bridge");
  CHECK(RowWith(split.rows, "fuel-split")->enabled);
  const double cells = sim.State().fuelCells;
  const double energy = sim.StatValue("energy");
  const int actions = sim.ActionCount();
  CHECK(sim.StartProject("no-such-project") == false);
  CHECK(ship::PressRoomAction(sim, "fuel-split"));
  CHECK(sim.State().fuelCells == cells - 1);
  CHECK(sim.StatValue("energy") == energy + 10);
  CHECK(sim.ActionCount() == actions + 1);

  // The bus converts 20 shields into 15 energy, and no further.
  sim.FindStat("shield")->value = 20;
  sim.FindStat("energy")->value = 80;
  CHECK(ship::PressRoomAction(sim, "divert-shield"));
  CHECK(sim.StatValue("shield") == 0);
  CHECK(sim.StatValue("energy") == 95);
  CHECK_FALSE(ship::PressRoomAction(sim, "divert-shield"));
}

TEST_CASE("the medical bay treats only the unwell, and needs a kit", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().medkits = 0;
  const ship::RoomScreenModel bare = ship::BuildRoomScreen(sim, "r1");
  CHECK(bare.buttons.size() == sim.Crew().size());
  for (const ship::ProgressButton& button : bare.buttons) {
    CHECK(button.state == ship::ButtonState::Off);
  }
  sim.MutableState().medkits = 3;
  sim.FindStat("energy")->value = 80;
  sim.MutableCrew()[0].health = 40;
  const ship::RoomScreenModel stocked = ship::BuildRoomScreen(sim, "r1");
  REQUIRE(stocked.buttons.size() == sim.Crew().size());
  CHECK(stocked.buttons[0].taskId == "doc-" + sim.Crew()[0].id);
  CHECK(stocked.buttons[0].state == ship::ButtonState::Idle);
  CHECK(ship::PressProgressButton(sim, stocked.buttons[0].taskId));
  CHECK(sim.FindOngoing("doc-" + sim.Crew()[0].id) != nullptr);
}

TEST_CASE("a shared meal needs two rations and two free hands", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.Crew().size() >= 2);
  const ship::RoomScreenModel model = ship::BuildRoomScreen(sim, "r13");
  CHECK_FALSE(model.rows.empty());
  const std::string id = model.rows[0].id;
  CHECK(id == "meal:" + sim.Crew()[0].id + ":" + sim.Crew()[1].id);
  const double racks = sim.MessRack();
  CHECK(sim.StartSocial(sim.Crew()[0].id, sim.Crew()[1].id));
  CHECK(sim.MessRack() == racks - 2);
  CHECK(sim.FindOngoing("project-social") != nullptr);
  // The rows go inert while the meal is under way.
  const ship::RoomScreenModel busy = ship::BuildRoomScreen(sim, "r13");
  for (const ship::ActionRowView& row : busy.rows) { CHECK_FALSE(row.enabled); }
}

TEST_CASE("the relay sells silence once and then remembers it", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = true;
  sim.FindStat("energy")->value = 60;
  const ship::RoomScreenModel first = ship::BuildRoomScreen(sim, "r4");
  const ship::ActionRowView* door = RowWith(first.rows, "broadcast");
  REQUIRE(door != nullptr);
  CHECK(door->enabled);
  const int actions = sim.ActionCount();
  CHECK(ship::PressRoomAction(sim, "broadcast"));
  CHECK(sim.ActionCount() == actions + 1);
  CHECK(sim.StatValue("energy") == 58);
  const ship::RoomScreenModel second = ship::BuildRoomScreen(sim, "r4");
  CHECK_FALSE(RowWith(second.rows, "broadcast")->enabled);
  // The report itself is a memory of the world, and it is now in the mosaic.
  bool remembered = false;
  for (const ship::Memory& memory : sim.State().sim.memories) {
    if (memory.kind == "World") { remembered = true; }
  }
  CHECK(remembered);
}

TEST_CASE("the two permanent answers to the archive", "[phase7][rooms]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().sim.signal = "request";
  sim.MutableMap().here = "palimpsest-site";
  sim.MutableState().docked = false;
  sim.MutableState().repairKits = 1;
  sim.FindStat("energy")->value = 20;
  const ship::RoomScreenModel model = ship::BuildRoomScreen(sim, "r4");
  const ship::ActionRowView* shelter = RowWith(model.rows, "signal:preserved");
  const ship::ActionRowView* sell = RowWith(model.rows, "signal:sold");
  REQUIRE(shelter != nullptr);
  REQUIRE(sell != nullptr);
  CHECK(shelter->enabled);
  CHECK_FALSE(sell->enabled);
  CHECK(shelter->confirm.find("permanent") != std::string::npos);
  CHECK(ship::PressRoomAction(sim, "signal:preserved"));
  CHECK(sim.State().sim.signal == "preserved");
  CHECK(sim.State().repairKits == 0);
  CHECK(sim.StatValue("energy") == 15);
  // The archive is now a filed survey record.
  bool filed = false;
  for (const ship::SurveyRecord& record : sim.Survey()) {
    if (record.id == "silicon-marrow") { filed = record.state == "filed"; }
  }
  CHECK(filed);
  CHECK_FALSE(ship::PressRoomAction(sim, "signal:preserved"));
}

// ---------------------------------------------------------------------------
//  The Simulacron
// ---------------------------------------------------------------------------
TEST_CASE("the mosaic lists records, not thoughts", "[phase7][simcore]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().sim.insight = 2;
  sim.MutableState().sim.signal = "dormant";
  const ship::SimScreenModel model = ship::BuildSimCoreScreen(sim);
  CHECK(model.head.title == "Simulacron Core");
  CHECK(model.body.title == "Ship systems");
  CHECK(model.memories.size() == sim.State().sim.memories.size());
  CHECK(model.anchors.size() == sim.Crew().size());
  // Dormant reads as "None recorded" rather than as an internal word.
  for (const ship::PanelLine& line : model.body.lines) {
    if (line.key == "Signal") { CHECK(line.value == "None recorded"); }
  }
  for (const std::string& line : model.memories) {
    CHECK(line.find(':') != std::string::npos);
  }
}

TEST_CASE("two records make an insight, and the pair is spent", "[phase7][simcore]") {
  ship::Simulation sim = Fresh();
  // The mosaic needs two records: the voyage's own, plus one world report.
  sim.SimRemember("World", "A trader's account of the Toll Road.");
  REQUIRE(sim.State().sim.memories.size() >= 2);
  const ship::SimScreenModel model = ship::BuildSimCoreScreen(sim);
  REQUIRE_FALSE(model.rows.empty());
  const std::string id = model.rows[0].id;
  CHECK(id.compare(0, 6, "dream:") == 0);
  const std::string first = id.substr(6, id.find(':', 6) - 6);
  const std::string second = id.substr(id.find(':', 6) + 1);
  const int actions = sim.ActionCount();
  CHECK(ship::PressSimCoreAction(sim, id));
  CHECK(sim.ActionCount() == actions + 1);
  CHECK(sim.FindOngoing("project-dream") != nullptr);
  // Both records are marked, so the same pair is no longer offered.
  for (const ship::Memory& memory : sim.State().sim.memories) {
    if (std::to_string(static_cast<int>(memory.id)) == first) { CHECK(memory.processed); }
    if (std::to_string(static_cast<int>(memory.id)) == second) { CHECK(memory.processed); }
  }
  const ship::SimScreenModel after = ship::BuildSimCoreScreen(sim);
  CHECK(RowWith(after.rows, id) == nullptr);
}

// ---------------------------------------------------------------------------
//  Reports
// ---------------------------------------------------------------------------
TEST_CASE("a report names its stage, its clock and its two ways out",
          "[phase7][events]") {
  ship::Simulation sim = Fresh();
  const std::string reportId = SomeReport(sim);
  REQUIRE_FALSE(reportId.empty());
  const ship::Incident incident = CopyIncident(sim, reportId);
  const ship::EventScreenModel model = ship::BuildEventScreen(sim, incident.id);
  CHECK(model.known);
  CHECK(model.head.kind == "EVENT");
  CHECK(model.report.title == "Report");
  REQUIRE(model.rows.size() == 2);
  CHECK(model.rows[0].id == "remedy:" + incident.id);
  CHECK(model.rows[1].id == "assign:" + incident.id);
  CHECK(model.rows[1].label.find("6 actions") != std::string::npos);
  // A closed report is honest about being closed.
  const ship::EventScreenModel gone = ship::BuildEventScreen(sim, "no-such-report");
  CHECK_FALSE(gone.known);
  CHECK(gone.head.title == "Report closed");
  CHECK(gone.rows.empty());
}

TEST_CASE("a kit remedy pays a kit and increments the chronicle", "[phase7][events]") {
  ship::Simulation sim = Fresh();
  const std::string reportId = SomeReport(sim);
  REQUIRE_FALSE(reportId.empty());
  const ship::Incident incident = CopyIncident(sim, reportId);
  sim.MutableState().repairKits = 2;
  const double kits = sim.State().repairKits;
  const double resolved = sim.State().chronicle.incidents;
  CHECK(ship::PressEventAction(sim, "remedy:" + incident.id));
  CHECK(sim.State().repairKits < kits);
  CHECK(sim.State().chronicle.incidents == resolved + 1);
  CHECK_FALSE(ship::PressEventAction(sim, "remedy:" + incident.id));
}

TEST_CASE("a crew dispute is worked by hand, not by kit", "[phase7][events]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.Crew().size() >= 2);
  const ship::Incident* dispute = nullptr;
  for (const ship::Incident& incident : sim.Events()) {
    if (incident.type == "crew") { dispute = &incident; }
  }
  // A fresh voyage has not yet reached the dispute's first report, so the
  // clock is advanced by real actions until the mess hall speaks.
  for (int i = 0; i < 200 && dispute == nullptr; ++i) {
    sim.StandWatch();
    for (const ship::Incident& incident : sim.Events()) {
      if (incident.type == "crew") { dispute = &incident; }
    }
  }
  REQUIRE(dispute != nullptr);
  const std::string id = dispute->id;
  const ship::EventScreenModel model = ship::BuildEventScreen(sim, id);
  REQUIRE(model.rows.size() == 2);
  CHECK(model.rows[0].label == "Mediate the dispute");
  CHECK(model.rows[0].enabled);
  sim.MutableState().repairKits = 0;
  CHECK(ship::PressEventAction(sim, "remedy:" + id));
  CHECK(sim.State().repairKits == 0);
  CHECK_FALSE(ship::PressEventAction(sim, "remedy:" + id));
}

TEST_CASE("an assigned hand shows a running button until the work is done",
          "[phase7][events]") {
  ship::Simulation sim = Fresh();
  const std::string id = SomeReport(sim);
  REQUIRE_FALSE(id.empty());
  REQUIRE(sim.IncidentHand(CopyIncident(sim, id)) != nullptr);
  CHECK(ship::PressEventAction(sim, "assign:" + id));
  const ship::Task* task = sim.FindOngoing("incident-" + id);
  REQUIRE(task != nullptr);
  CHECK(task->total == 6.0);
  CHECK(task->left > 0.0);
  CHECK_FALSE(task->by.empty());
  const ship::EventScreenModel busy = ship::BuildEventScreen(sim, id);
  CHECK_FALSE(busy.buttons.empty());
  CHECK(busy.buttons[0].state == ship::ButtonState::Running);
  CHECK_FALSE(busy.rows[1].enabled);
}

// ---------------------------------------------------------------------------
//  The logbook
// ---------------------------------------------------------------------------
TEST_CASE("the logbook header counts what the chronicle holds", "[phase7][awards]") {
  ship::Simulation sim = Fresh();
  const ship::AwardsScreenModel model = ship::BuildAwardsScreen(sim);
  CHECK(model.head.kind == "LOGBOOK");
  CHECK(model.head.title == "Commendations & Rewards");
  CHECK(model.summary.title == "Captain's log");
  CHECK(model.rows.size() == World().commendations.size());
  CHECK(model.rows.size() == 14);
  CHECK(model.summary.lines[0].key == "Rank");
  CHECK(model.summary.lines[0].value == "New Watch (cosmetic)");
  CHECK(model.summary.lines[1].key == "Commendations");
  CHECK(model.summary.lines[1].value == "0 / 14");
  // Every unearned card shows progress, never a claim.
  for (const ship::ActionRowView& row : model.rows) {
    CHECK_FALSE(row.enabled);
    CHECK(row.cost == "free");
    CHECK(row.label.find(" / ") != std::string::npos);
  }
}

TEST_CASE("an earned commendation pays once and says so", "[phase7][awards]") {
  ship::Simulation sim = Fresh();
  const ship::Commendation* berth = nullptr;
  for (const ship::Commendation& award : World().commendations) {
    if (award.id == "berth") { berth = &award; }
  }
  REQUIRE(berth != nullptr);
  // A berth contract marked ready is what "A Port in the Drift" waits for.
  sim.MutableState().contracts["arrival"] = "ready";
  sim.CheckCommendations();
  CHECK(sim.State().chronicle.earned.at("berth"));
  const ship::AwardsScreenModel ready = ship::BuildAwardsScreen(sim);
  CHECK(CardStates(ready, "Ready to claim"));
  const ship::ActionRowView* card = RowWith(ready.rows, "claim:berth");
  REQUIRE(card != nullptr);
  CHECK(card->enabled);

  const double cash = sim.State().shiplets;
  CHECK(ship::PressAwardsAction(sim, "claim:berth"));
  CHECK(sim.State().shiplets == cash + berth->reward.cash);
  // Once only, even if asked again.
  CHECK_FALSE(ship::PressAwardsAction(sim, "claim:berth"));
  const ship::AwardsScreenModel claimed = ship::BuildAwardsScreen(sim);
  CHECK(CardStates(claimed, "Claimed"));
  CHECK_FALSE(RowWith(claimed.rows, "claim:berth")->enabled);
  // Claimed is persisted state, not a repainted label.
  CHECK(sim.State().chronicle.claimed.at("berth"));
}

TEST_CASE("rank is a name for how much of the log is filled", "[phase7][awards]") {
  ship::Simulation sim = Fresh();
  CHECK(sim.CaptainRank() == "New Watch");
  for (const ship::Commendation& award : World().commendations) {
    sim.MutableState().chronicle.earned[award.id] = true;
  }
  CHECK(sim.CaptainRank() == "Constellation");
  CHECK(sim.EarnedCommendations() == 14);
  CHECK(sim.ReadyCommendations() == 14);
  // Ten of fourteen is still a name, not a threshold that vanishes.
  sim.MutableState().chronicle.claimed["berth"] = true;
  CHECK(sim.ReadyCommendations() == 13);
}

TEST_CASE("last-action remedies clear an incident before it can escalate", "[event][regression]") {
  auto sim = Fresh();
  const auto id = SomeReport(sim);
  REQUIRE(id == "fracture-45");
  while (CopyIncident(sim, id).left > 1) { REQUIRE(sim.StandWatch()); }
  const auto hull = sim.StatValue("hull");
  const auto kits = sim.State().repairKits;
  const auto resolved = sim.State().chronicle.incidents;
  REQUIRE(sim.RemedyIncident(id));
  CHECK(sim.StatValue("hull") == hull);
  CHECK(sim.State().repairKits == kits - 1);
  CHECK(sim.State().chronicle.incidents == resolved + 1);
  CHECK(CopyIncident(sim, id).id.empty());
}

TEST_CASE("traffic report memory is made before the world tick", "[sim][regression]") {
  auto sim = Fresh();
  const auto before = sim.ActionCount();
  REQUIRE(sim.BroadcastLocal());
  REQUIRE(sim.State().sim.memories.back().kind == "World");
  CHECK(sim.State().sim.memories.back().at == before);
  CHECK(sim.ActionCount() == before + 1);
  CHECK(ship::RoomActionCategory("broadcast") == "space");
  CHECK(ship::RoomActionCategory("fuel-split") == "systems");
  CHECK(ship::RoomActionCategory("signal:sold") == "critical");
}

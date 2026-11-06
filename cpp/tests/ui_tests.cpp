// ============================================================================
//  ui_tests.cpp — the cockpit, held to the reference's numbers and sentences.
//  ---------------------------------------------------------------------------
//  Phase 2's contract is that the interface can be checked without a window:
//  the anchors of §6.4, the feed clamp, the indicator strip, the tab system, the
//  sidebar, the status line and every briefing string are all data. These tests
//  compare that data with what the HTA says, so a drift shows up here rather
//  than in a screenshot nobody reads.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/Describe.h"
#include "ui/CrewRecord.h"
#include "ui/Fx.h"
#include "ui/FloatingPanels.h"
#include "ui/PointerOwnership.h"
#include "ui/TitleScreen.h"
#include "ui/IdleScreen.h"
#include "ui/Layout.h"
#include "ui/Shell.h"
#include "ui/ShipDiagram.h"
#include "ui/Theme.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::ReferenceVoyage(World()); }

const ship::DescribeRow* Row(const ship::Description& t, const std::string& key) {
  for (const ship::DescribeRow& row : t.rows) {
    if (row.key == key) { return &row; }
  }
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Palette
// ---------------------------------------------------------------------------
TEST_CASE("theme names and chart inks match the HTA THEMES table", "[ui][theme]") {
  const auto& specs = ship::Theme::Specs();
  REQUIRE(specs.size() == 5);
  const char* ids[] = {"classic", "void", "aurora", "nebula", "ember"};
  const char* labels[] = {"Flight Deck - parchment", "Deep Space - cyan", "Aurora - mint", "Nebula - violet", "Ember - copper"};
  const ship::Rgb inks[] = {{139,111,78}, {91,184,213}, {91,190,157}, {155,121,208}, {215,144,91}};
  for (std::size_t i = 0; i < 5; ++i) {
    CHECK(specs[i].id == ids[i]);
    CHECK(specs[i].label == labels[i]);
    CHECK(specs[i].ink.r == inks[i].r);
    CHECK(specs[i].ink.g == inks[i].g);
    CHECK(specs[i].ink.b == inks[i].b);
  }
  // Classic remains source parchment; alternatives are deliberately light
  // by user request, while keeping their save-compatible identifiers.
  const ship::Rgb panels[] = {{0xfa,0xf7,0xf2}, {0xf2,0xf7,0xf8},
      {0xf2,0xf7,0xf3}, {0xf7,0xf3,0xf8}, {0xfa,0xf5,0xf0}};
  for (std::size_t i = 0; i < 5; ++i) {
    const auto palette = ship::Theme::ById(ids[i]).palette;
    CHECK(palette.panel.r == panels[i].r);
    CHECK(palette.panel.g == panels[i].g);
    CHECK(palette.panel.b == panels[i].b);
  }
  CHECK(ship::Theme::ById("no-such-theme").id == "classic");
}

TEST_CASE("theme flags drive type size and the compact deck", "[ui][theme]") {
  // Typography floor: 18 px, then four 5% steps, and a line box
  // derived from the face so nothing clips as the steps grow.
  ship::Theme theme = ship::Theme::ById("classic");
  CHECK(ship::Theme::FontPxForStep(0) == 18.0f);
  CHECK(ship::Theme::FontPxForStep(1) == 19.0f);
  CHECK(ship::Theme::FontPxForStep(2) == 20.0f);
  CHECK(ship::Theme::FontPxForStep(3) == 21.0f);
  CHECK(ship::Theme::FontPxForStep(9) == 21.0f);    // clamped, never larger
  CHECK(ship::Theme::FontPxForStep(-3) == 18.0f);   // and never below the floor
  CHECK(theme.BodySize() == 18.0f);
  CHECK(theme.LineHeight() == 24.0f);
  CHECK_FALSE(theme.largeText);
  theme.fontStep = 2;
  CHECK(theme.BodySize() == 20.0f);
  CHECK(theme.LineHeight() == 26.0f);
  theme.largeText = true;    // the old flag still adds its two pixels
  CHECK(theme.BodySize() == 22.0f);
  CHECK(theme.LineHeight() == 29.0f);

  // rgba(139,111,78,0.13), trimmed the way the stylesheet writes it.
  CHECK(theme.Ink(0.13f) == "rgba(139,111,78,0.13)");
  CHECK(theme.Ink(1.0f) == "rgba(139,111,78,1)");
  CHECK(ship::Theme::ById("void").Ink(0.5f) == "rgba(91,184,213,0.5)");
}

// ---------------------------------------------------------------------------
//  Layout: the anchors, and the measure-and-give step
// ---------------------------------------------------------------------------
TEST_CASE("the cockpit's fixed anchors", "[ui][layout]") {
  const ship::ShellLayout layout = ship::ComputeLayout(1280.0f, 800.0f, 742.0f, 300.0f, false);

  CHECK(layout.taskbar.x == 0.0f);
  CHECK(layout.taskbar.h == 35.0f);
  CHECK(layout.controls.x == 1280.0f - 104.0f);
  CHECK(layout.controls.w == 104.0f);
  CHECK(layout.home.w == 48.0f);
  CHECK(layout.home.Right() == 1280.0f);

  CHECK(layout.sidepanel.x == 0.0f);
  CHECK(layout.sidepanel.y == 36.0f);
  CHECK(layout.sidepanel.w == 165.0f);
  CHECK(layout.sidepanel.Bottom() == 800.0f - 97.0f);

  CHECK(layout.sidefoot.x == 0.0f);
  CHECK(layout.sidefoot.w == 165.0f);
  CHECK(layout.sidefoot.h == 96.0f);
  CHECK(layout.sidefoot.Bottom() == 800.0f - 1.0f);

  CHECK(layout.content.x == 166.0f);
  CHECK(layout.content.y == 36.0f);
  CHECK(layout.content.w == 1280.0f - 166.0f);

  CHECK(layout.statusbar.h == 0.0f);
  CHECK(layout.statusbar.Bottom() == 800.0f);
}

TEST_CASE("the Home control reserves measured text width and stays clickable", "[ui][layout]") {
  const auto layout = ship::ComputeLayout(1280, 800, 742, 300, false, 92);
  CHECK(layout.home.w == 92);
  CHECK(layout.home.Right() == 1280);
  CHECK(layout.controls.w == 148);
  CHECK(layout.tabStrip.Right() == layout.controls.x);
  CHECK(layout.scrollLeft.w == 28);
  CHECK(layout.scrollRight.Right() == layout.home.x);
  const auto tiny = ship::ComputeLayout(60, 800, 742, 300, false, 92);
  CHECK(tiny.home.x >= 0);
  CHECK(tiny.home.Right() == 60);
  CHECK(tiny.tabStrip.w >= 0);
}

TEST_CASE("the feed takes what is left, inside 62..132", "[ui][layout]") {
  using ship::Metrics;
  // Nothing left over: the floor.
  CHECK(ship::GiveFeedHeight(300.0f, 400.0f) == Metrics::kFeedMin);
  // Plenty left over: the ceiling.
  CHECK(ship::GiveFeedHeight(900.0f, 400.0f) == Metrics::kFeedMax);
  // The middle, minus the four pixels of breathing room.
  CHECK(ship::GiveFeedHeight(700.0f, 600.0f) == 96.0f);
  CHECK(ship::GiveFeedHeight(762.0f, 700.0f) == 62.0f);   // clamped up to 62
  CHECK(ship::GiveFeedHeight(762.0f, 610.0f) == 132.0f);  // clamped down to 132
}

TEST_CASE("the map parks at 240 and never passes half its width", "[ui][layout]") {
  CHECK(ship::GiveMapHeight(400.0f, 300.0f, 1100.0f) == 240.0f);
  CHECK(ship::GiveMapHeight(900.0f, 300.0f, 1100.0f) == 500.0f);  // the authored ceiling
  CHECK(ship::GiveMapHeight(900.0f, 300.0f, 700.0f) == 350.0f);   // round(700/2)
  CHECK(ship::GiveMapHeight(900.0f, 300.0f, 300.0f) == 240.0f);   // floor wins
}

TEST_CASE("the stage is fixed and centred", "[ui][layout]") {
  const ship::Area content{166.0f, 36.0f, 1400.0f, 700.0f};
  const ship::Area stage = ship::StageRect(content, false);
  CHECK(stage.w == 1160.0f);
  CHECK(stage.h == 460.0f);
  CHECK(stage.x == 166.0f + std::round((1400.0f - 1160.0f) / 2.0f));

  const ship::Area compact = ship::StageRect(content, true);
  CHECK(compact.w == 600.0f);
  CHECK(compact.h == 365.0f);

  // Narrower than the ship: the ship keeps its size and the column scrolls.
  const ship::Area narrow{166.0f, 36.0f, 900.0f, 700.0f};
  CHECK(ship::StageRect(narrow, false).x == narrow.x);
}

TEST_CASE("the indicator strip draws bottom-up on a five pixel pitch", "[ui][layout]") {
  std::vector<ship::IndicatorLine> lines = {{50, false}, {100, true}, {0, false}};
  const std::vector<ship::IndicatorRect> placed = ship::PlaceIndicators(lines);
  REQUIRE(placed.size() == 3);
  CHECK(placed[0].line.h == 52.0f);
  CHECK(placed[0].ghost.h == 52.0f);
  CHECK(placed[0].line.y == 52.0f);
  CHECK_FALSE(placed[0].grow);
  CHECK(placed[1].line.h == 104.0f);
  CHECK(placed[1].line.y == 0.0f);
  CHECK(placed[1].grow);
  // A zero line is never invisible: it is one pixel tall.
  CHECK(placed[2].line.h == 1.0f);
  CHECK(placed[1].ghost.x == 5.0f);
  CHECK(placed[2].ghost.x == 10.0f);
  CHECK(ship::PlaceIndicators({}).empty());
}

// ---------------------------------------------------------------------------
//  Briefings: describeTarget's exact strings
// ---------------------------------------------------------------------------
TEST_CASE("the GONE guard, word for word", "[ui][describe]") {
  ship::Simulation sim = Fresh();
  const ship::Description gone = ship::DescribeTarget(sim, "crew", "nobody");
  CHECK(gone.gone);
  CHECK(gone.kindLabel == "GONE");
  CHECK(gone.title == "No longer aboard");
  CHECK(gone.sub == "Whatever this tab was watching is not on the ship any more.");
  REQUIRE(gone.rows.size() == 2);
  CHECK(gone.rows[0].key == "Was");
  CHECK(gone.rows[0].value == "crew nobody");
  CHECK(gone.rows[1].key == "Status");
  CHECK(gone.rows[1].value == "no record");
  CHECK(gone.acts.empty());
  CHECK_FALSE(ship::DescribeTargetExists(sim, "crew", "nobody"));
  CHECK(ship::DescribeTargetExists(sim, "nav", "main"));
  CHECK(ship::DescribeTargetExists(sim, "wiki", "main"));
}

TEST_CASE("the two reference windows describe themselves", "[ui][describe]") {
  ship::Simulation sim = Fresh();
  const ship::Description wiki = ship::DescribeTarget(sim, "wiki", "main");
  CHECK(wiki.kindLabel == "WIKI");
  CHECK(wiki.title == "Ship's Handbook");
  CHECK(wiki.sub == "Free reference and voyage records");

  const ship::Description awards = ship::DescribeTarget(sim, "awards", "main");
  CHECK(awards.kindLabel == "LOGBOOK");
  CHECK(awards.title == "Commendations & Rewards");

  const ship::Description nav = ship::DescribeTarget(sim, "nav", "main");
  CHECK(nav.kindLabel == "NAV");
  CHECK(nav.title == "Navigation");
  CHECK(nav.sub == "Course plotting and system chart");
}

TEST_CASE("a generic room briefing", "[ui][describe]") {
  ship::Simulation sim = Fresh();
  const ship::Description engine = ship::DescribeTarget(sim, "room", "engine");
  CHECK(engine.kindLabel == "ROOM");
  CHECK(engine.title == "Engine Room");
  // The store rooms REPLACE the generic rows with what the store holds, and say
  // so in the sub-line. Nothing is appended on top.
  REQUIRE(engine.rows.size() == 5);
  CHECK(engine.rows[0].key == "Condition");
  CHECK(engine.rows[0].value == "100%");
  CHECK(Row(engine, "Designation") == nullptr);
  CHECK(Row(engine, "Deck position") == nullptr);
  CHECK(engine.sub == "Propulsion and power - condition 100%");
  CHECK(Row(engine, "Wear rate") != nullptr);
  REQUIRE(engine.acts.size() == 2);
  CHECK(engine.acts[0].label == "Run Maintenance");
  CHECK(engine.acts[0].cost == "refused at 100%");
  CHECK_FALSE(engine.acts[0].live);

  const ship::Description o2 = ship::DescribeTarget(sim, "room", "o2");
  CHECK(o2.sub == "Life support - air quality OPTIMAL");
  CHECK(Row(o2, "Slots") != nullptr);
  CHECK(Row(o2, "Canisters") != nullptr);
  CHECK(Row(o2, "Reward")->value == "+10 pro-action when all three run");

  const ship::Description mess = ship::DescribeTarget(sim, "room", "mess");
  CHECK(mess.sub == "Galley and rations - 18 of 24 on the rack");
  CHECK(Row(mess, "Hunger cycle")->value == "every 25 actions, per crew member, staggered");

  const ship::Description cargo = ship::DescribeTarget(sim, "room", "cargo");
  CHECK(cargo.sub == "Storage - 32 of 40 m3 in use");
  CHECK(cargo.rows[0].key == "Hold");
  CHECK(cargo.rows[1].key == "Lots");
  CHECK(cargo.rows[2].key == "Manifest");
}

TEST_CASE("fitted rooms, quarters and the bridge", "[ui][describe]") {
  ship::Simulation sim = Fresh();
  const ship::Description bridge = ship::DescribeTarget(sim, "room", "bridge");
  REQUIRE(bridge.rows.size() == 3);
  CHECK(bridge.rows[0].key == "Systems");
  CHECK(bridge.rows[1].key == "Occupants");
  CHECK(bridge.rows[2].value == "Right-click to open the room");
  CHECK(bridge.acts.empty());

  const ship::Description quarters = ship::DescribeTarget(sim, "room", "quarters");
  CHECK(quarters.rows[0].key == "Designation");
  CHECK(quarters.rows[1].key == "Systems");
  CHECK(Row(quarters, "Condition")->value == "Operational");
  CHECK(Row(quarters, "Deck position")->value == "x 201 / y 86");
  REQUIRE(quarters.acts.size() == 2);
  CHECK(quarters.acts[0].label == "Talk to crew");
  CHECK(quarters.acts[1].cost == "1 action + work");
}

TEST_CASE("crew, systems and readouts", "[ui][describe]") {
  ship::Simulation sim = Fresh();
  const ship::Description crew = ship::DescribeTarget(sim, "crew", "jax");
  CHECK(crew.kindLabel == "CREW");
  CHECK(crew.title == "Jax Moreno");
  CHECK(crew.rows.empty());
  CHECK(crew.acts.empty());

  const ship::Description hull = ship::DescribeTarget(sim, "system", "hull");
  CHECK(hull.kindLabel == "SYSTEM");
  CHECK(hull.title == "Hull Integrity");
  CHECK(hull.sub == "Reading 100%");
  CHECK(Row(hull, "Status")->value == "Operational");
  CHECK(Row(hull, "Maintained by") != nullptr);
  REQUIRE(hull.acts.size() == 1);
  CHECK(hull.acts[0].label == "Patch hull");
  CHECK(hull.acts[0].cost == "1 action - consumes a repair kit");

  const ship::Description life = ship::DescribeTarget(sim, "info", "life");
  CHECK(life.kindLabel == "READOUT");
  REQUIRE(life.acts.size() == 1);
  CHECK(life.acts[0].label == "Open O2 Filter Room");

  const ship::Description location = ship::DescribeTarget(sim, "info", "location");
  CHECK(Row(location, "Source")->value == "Ship sensors");
  CHECK(Row(location, "Updated")->value == "On arrival");
  REQUIRE(location.acts.size() == 2);
  CHECK(location.acts[0].label == "Open Navigation");
}

TEST_CASE("stations are described only while the ship is alongside", "[ui][describe]") {
  ship::Simulation sim = Fresh();
  const std::string port = World().ports.front().id;
  CHECK_FALSE(ship::DescribeTargetExists(sim, "station", port));

  sim.MutableMap().here = port;
  sim.MutableState().docked = true;
  CHECK(ship::DescribeTargetExists(sim, "station", port));
  const ship::Description station = ship::DescribeTarget(sim, "station", port);
  CHECK(station.kindLabel == "STATION");
  CHECK(station.title == World().ports.front().name);
  CHECK(Row(station, "Berth")->value == "Docked");
  CHECK(Row(station, "Purse")->value == "340 sh");
  REQUIRE(station.acts.size() == 2);
  CHECK(station.acts[1].label == "Undock");
}

// ---------------------------------------------------------------------------
//  Tabs
// ---------------------------------------------------------------------------
TEST_CASE("opening a task is free and does not steal the screen", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  CHECK(ship::OpenTask(sim, "room", "engine", false));
  REQUIRE(sim.State().tabs.size() == 1);
  CHECK(sim.State().tabs[0].key == "room:engine");
  CHECK(sim.State().activeTab == "home");           // background
  CHECK(sim.State().freshTabs.at("room:engine"));

  // A second call on the same id does not duplicate the tab.
  CHECK(ship::OpenTask(sim, "room", "engine", false));
  CHECK(sim.State().tabs.size() == 1);

  // Focusing is opt-in.
  CHECK(ship::OpenTask(sim, "crew", "bo", true));
  CHECK(sim.State().activeTab == "crew:bo");
  REQUIRE(sim.State().tabs.size() == 2);
  CHECK(sim.State().tabs[1].title == "Bo Ferreira");
}

TEST_CASE("the bar is drawn once per paint and consumes the pulse", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  ship::OpenTask(sim, "room", "cargo", false);
  std::vector<ship::TabView> bar = ship::TabBar(sim);
  REQUIRE(bar.size() == 2);
  CHECK(bar[0].home);
  CHECK(bar[0].kindLabel == "SHIP");
  CHECK(bar[0].title == sim.Ship().Get("name") + " (Idle)");
  CHECK(bar[0].active);
  CHECK_FALSE(bar[0].closable);
  CHECK(bar[1].closable);
  CHECK(bar[1].fresh);                 // announced
  CHECK_FALSE(sim.State().freshTabs.at("room:cargo"));
  bar = ship::TabBar(sim);
  CHECK_FALSE(bar[1].fresh);           // and it only announces once
}

TEST_CASE("switching re-describes, closing falls back home", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  ship::OpenTask(sim, "room", "engine", true);
  CHECK(ship::SwitchTab(sim, "home"));
  CHECK_FALSE(ship::SwitchTab(sim, "room:nowhere"));

  // The tab's frozen copy is re-asked on every switch: a scribbled-on title is
  // put back to what the ship says now.
  for (ship::Tab& tab : sim.MutableState().tabs) { tab.title = "stale"; }
  CHECK(ship::SwitchTab(sim, "room:engine"));
  CHECK(sim.State().tabs[0].title == "Engine Room");

  // Nav is left alone on purpose: it draws itself live.
  ship::OpenTask(sim, "nav", "main", true);
  for (ship::Tab& tab : sim.MutableState().tabs) {
    if (tab.kind == "nav") { tab.title = "live"; }
  }
  CHECK(ship::RefreshTab(sim, "nav:main"));
  CHECK_FALSE(ship::CloseTab(sim, "room:nowhere"));
  CHECK(ship::CloseTab(sim, "room:engine"));
  CHECK(sim.State().tabs.size() == 1);
  CHECK(ship::CloseTab(sim, "nav:main"));
  CHECK(sim.State().activeTab == "home");
  CHECK(sim.State().tabs.empty());
}

TEST_CASE("the status line is one sentence, exactly", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  std::string location;
  for (const ship::InfoRow& info : sim.Infos()) {
    if (info.id == "location") { location = info.value; }
  }
  std::string expected = sim.Ship().Get("name") + " - " + location +
                         " | Actions 0 | Open tasks 0";
  if (!sim.State().feed.empty()) { expected += " | " + sim.State().feed.back().text; }
  CHECK(ship::StatusText(sim) == expected);

  ship::OpenTask(sim, "room", "engine", false);
  CHECK(ship::StatusText(sim).find("| Open tasks 1") != std::string::npos);

  sim.PerformAction();
  const std::string after = ship::StatusText(sim);
  CHECK(after.find("| Actions 1") != std::string::npos);
  // The last line of the log rides on the status bar, so the ship can always
  // answer "what just happened".
  if (!sim.State().feed.empty()) {
    CHECK(after.find(" | " + sim.State().feed.back().text) != std::string::npos);
  }
}

TEST_CASE("the feed reads newest first, capped at 200", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.State().feed.empty());
  // The reference appends to the log when something happens, not once per
  // action; the log itself is what this test is about.
  sim.LogLine("first");
  sim.LogLine("second");
  sim.LogLine("third");
  REQUIRE(sim.State().feed.size() == 3);

  const std::vector<std::string> feed = ship::FeedLinesNewestFirst(sim);
  REQUIRE(feed.size() == sim.State().feed.size());
  for (std::size_t i = 0; i < feed.size(); ++i) {
    CHECK(feed[i] == sim.State().feed[sim.State().feed.size() - 1 - i].text);
  }
  CHECK(ship::FeedLinesNewestFirst(sim, 1).size() == 1);
  CHECK(ship::FeedLinesNewestFirst(sim, 1)[0] == sim.State().feed.back().text);
}

// ---------------------------------------------------------------------------
//  The sidebar
// ---------------------------------------------------------------------------
TEST_CASE("six blocks, five of them fold", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  const std::vector<ship::SideBlock> blocks = ship::SideBlocks(sim);
  REQUIRE(blocks.size() == 6);
  CHECK(blocks[0].key == "vessel");
  CHECK(blocks[1].key == "resources");
  CHECK(blocks[2].key == "crew");
  CHECK(blocks[3].key == "contracts");
  CHECK(blocks[4].key == "events");
  CHECK(blocks[5].key == "indicators");
  CHECK(blocks[5].indicators);
  CHECK(blocks[5].open);           // the glance never folds
  for (int i = 0; i < 5; ++i) { CHECK_FALSE(blocks[i].open); }

  CHECK(blocks[0].rows.size() == 13);
  CHECK(blocks[0].rows[0].key == sim.Ship().Get("name"));
  CHECK(blocks[0].rows[1].key == sim.Ship().Get("className"));
  CHECK(blocks[0].rows[1].value == "Registry " + sim.Ship().Get("registry"));
  CHECK(blocks[0].rows.back().key == "Rooms");
  CHECK(blocks[0].rows.back().value == "21 aboard");
  CHECK(blocks[1].rows.size() == 8);
  CHECK(blocks[1].rows[0].key == "Shiplets");
  CHECK(blocks[1].rows[0].value == "340");
  CHECK(blocks[1].rows[6].value.compare(
            blocks[1].rows[6].value.size() - 3, 3, " m3") == 0);
  CHECK(blocks[2].rows.size() == 4);
  CHECK(blocks[3].empty == "No active contracts.");
  CHECK(blocks[4].empty == "Nothing reported.");

  // The fold state lives in STATE, as it does in the reference.
  sim.MutableState().spOpen["vessel"] = true;
  CHECK(ship::SideBlocks(sim)[0].open);
}

TEST_CASE("indicator lines: the engine, the air, then the work", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  CHECK(ship::IndicatorLines(sim).empty());

  sim.MutableState().engineCondition = 25.0;   // warnBelow is 50
  std::vector<ship::IndicatorLine> lines = ship::IndicatorLines(sim);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].pct == 50);
  CHECK(lines[0].grow);

  sim.MutableState().engineCondition = 100.0;
  sim.MutableState().filterLife = 0.0;
  sim.MutableState().airHold = 0.0;
  lines = ship::IndicatorLines(sim);
  REQUIRE_FALSE(lines.empty());
  CHECK(lines[0].grow);
  CHECK(lines[0].pct == 100);
}

TEST_CASE("the sidebar's controls are where they are painted", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  const ship::ShellLayout layout =
      ship::ComputeLayout(1280.0f, 800.0f, 742.0f, 420.0f, true);
  const std::vector<ship::SideHit> hits =
      ship::SideHits(sim, layout.sidepanel, layout.sidefoot, 17.0f);
  REQUIRE_FALSE(hits.empty());

  // Six headers, then the foot's six controls.
  int headers = 0;
  int toggles = 0;
  int wiki = 0;
  int rewards = 0;
  std::vector<float> footIconX;
  for (const ship::SideHit& hit : hits) {
    if (hit.header) { ++headers; }
    if (hit.header && hit.action == ship::SideAction::Toggle) { ++toggles; }
    if (hit.action == ship::SideAction::Wiki) { ++wiki; }
    if (hit.action == ship::SideAction::Rewards) { ++rewards; }
    if (hit.action == ship::SideAction::Manifest || hit.action == ship::SideAction::Log ||
        hit.action == ship::SideAction::Files || hit.action == ship::SideAction::Settings) {
      footIconX.push_back(hit.box.x);
    }
  }
  CHECK(headers == 6);
  CHECK(toggles == 6);   // Indicators is a header too, and refuses to fold
  CHECK(wiki == 1);
  CHECK(rewards == 1);
  REQUIRE(footIconX.size() == 4);
  CHECK(footIconX[0] == 18.5f);
  CHECK(footIconX[1] == 50.5f);
  CHECK(footIconX[2] == 82.5f);
  CHECK(footIconX[3] == 114.5f);

  // Nothing is painted outside the panel or the foot.
  for (const ship::SideHit& hit : hits) {
    const bool inPanel = hit.box.y >= layout.sidepanel.y &&
                         hit.box.y < layout.sidepanel.Bottom();
    const bool inFoot = hit.box.y >= layout.sidefoot.y &&
                        hit.box.y < layout.sidefoot.Bottom();
    CHECK((inPanel || inFoot));
    CHECK(hit.box.x >= 0.0f);
    CHECK(hit.box.Right() <= layout.sidepanel.Right());
  }

  // A folded block's rows are not clickable; an open one's are.
  const auto rowsFor = [&sim, &layout](const std::string& key) {
    int rows = 0;
    for (const ship::SideHit& hit :
         ship::SideHits(sim, layout.sidepanel, layout.sidefoot, 17.0f)) {
      if (!hit.header && hit.action == ship::SideAction::None &&
          hit.text != "Wiki" && !hit.text.empty()) {
        ++rows;
      }
    }
    (void)key;
    return rows;
  };
  const int folded = rowsFor("vessel");
  sim.MutableState().spOpen["vessel"] = true;
  CHECK(rowsFor("vessel") > folded);

  // SideHit carries the right-hand values that the HTA renders beside each
  // resource label; the native painter must not silently drop these fields.
  sim.MutableState().spOpen["resources"] = true;
  bool shipletsShown = false;
  for (const ship::SideHit& hit :
       ship::SideHits(sim, layout.sidepanel, layout.sidefoot, 17.0f)) {
    if (hit.text == "Shiplets" && hit.rightValue == "340") {
      shipletsShown = true;
    }
  }
  CHECK(shipletsShown);
}

TEST_CASE("the foot and the rewards count", "[ui][shell]") {
  ship::Simulation sim = Fresh();
  const std::vector<ship::FootButton> foot = ship::FootButtons();
  REQUIRE(foot.size() == 4);
  CHECK(foot[0].key == "manifest");
  CHECK(foot[0].title == "Ship Manifest");
  CHECK(foot[2].title == "Save / Load Game");
  CHECK(ship::RewardsLabel(sim) == "Rewards");   // nothing earned yet
  CHECK(ship::ReadyCommendations(sim) == 0);
  CHECK(ship::CaptainRank(sim) == "New Watch");
}

// ---------------------------------------------------------------------------
//  The cross-section
// ---------------------------------------------------------------------------
TEST_CASE("the hull is the deck plan, drawn at the authored numbers", "[ui][diagram]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::Area stage{100.0f, 50.0f, 600.0f, 365.0f};
  const ship::DiagramModel diagram = ship::BuildDiagram(sim, stage, false, rng);

  CHECK(diagram.stage.w == 600.0f);
  CHECK(diagram.compact);                 // the opening voyage flies a Wren
  CHECK(diagram.rooms.size() == 21);
  CHECK(diagram.hull.size() == World().wren.hull.size());
  CHECK(diagram.nacBars.size() == 3 * 1);  // three bars on the courier's one nacelle
  // The corridor label anchors to the corridor's top-left corner.
  CHECK(diagram.corridorLabel == "CORRIDOR");
  CHECK(diagram.corridorLabelBox.x == diagram.corridor.x + 9.0f);
  CHECK(diagram.corridorLabelBox.y == diagram.corridor.y + 7.0f);
  CHECK(diagram.dividers.size() == 2);
  CHECK(diagram.conduit.h == 12.0f);

  // Two nozzles on the courier, three on the large hull.
  CHECK(diagram.nozzles.size() == 2);
  CHECK(ship::NozzleCount(false) == 3);
  CHECK(ship::NozzleCount(true) == 2);

  // The radii the CSS gives each piece of silhouette.
  CHECK(ship::HullRadius("hp-body") == 60.0f);
  CHECK(ship::HullRadius("hp-spine") == 14.0f);
  CHECK(ship::HullRadius("hp-pod") == 14.0f);
  CHECK(ship::HullRadius("hp-nacelle") == 15.0f);
  CHECK(ship::HullRadius("hp-lens") == 0.0f);     // a circle; drawn as one
  CHECK(ship::HullRadius("hp-pylon") == 0.0f);

  // Dots stay inside their compartment - the diagram must never contradict the
  // crew window.
  REQUIRE(diagram.crew.size() == sim.Crew().size());
  for (const ship::DiagramCrew& dot : diagram.crew) {
    const ship::Room* room = nullptr;
    for (const ship::Room& candidate : diagram.rooms) {
      if (candidate.id == dot.roomId) { room = &candidate; }
    }
    if (room == nullptr) {
      CHECK(dot.roomId == "corridor");
      CHECK(dot.dot.x >= diagram.corridor.x);
      CHECK(dot.dot.y >= diagram.corridor.y);
      continue;
    }
    CHECK(dot.dot.x >= room->box.x);
    CHECK(dot.dot.x <= room->box.x + room->box.w);
    CHECK(dot.dot.y >= room->box.y);
    CHECK(dot.dot.y <= room->box.y + room->box.h);
  }
}

TEST_CASE("the large hull is the Kestrel's own silhouette", "[ui][diagram]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().shipClass = "kestrel";
  ship::FxRng rng;
  const ship::DiagramModel diagram =
      ship::BuildDiagram(sim, ship::Area{0.0f, 0.0f, 1160.0f, 460.0f}, true, rng);
  CHECK_FALSE(diagram.compact);
  CHECK(diagram.stage.w == 1160.0f);
  CHECK(diagram.hull.size() == World().kestrel.hull.size());
  CHECK(diagram.nacBars.size() == 3 * 2);
  CHECK(diagram.nozzles.size() == 3);
  CHECK(diagram.rooms.size() == 21);
  for (const ship::DiagramCrew& dot : diagram.crew) { CHECK_FALSE(dot.labelled); }
}

// ---------------------------------------------------------------------------
//  Home
// ---------------------------------------------------------------------------
TEST_CASE("the idle screen is built in the reference's order", "[ui][idle]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::Theme theme = ship::Theme::ById("classic");
  const ship::ShellLayout layout =
      ship::ComputeLayout(1280.0f, 800.0f, 742.0f, 420.0f, true);
  const ship::IdleScreenModel idle =
      ship::BuildIdleScreen(sim, layout, theme, false, 420.0f, rng);

  REQUIRE(idle.stats.size() == 4);
  CHECK(idle.stats.front().widthPercent == 25.0f);
  CHECK(idle.stats.front().id == "hull");
  CHECK(idle.infos.size() >= 3);
  CHECK(idle.stageNote ==
        "Left-click anything here to read what it is. "
        "Right-click opens its task window.");
  CHECK(idle.nav.label == "Navigation");
  CHECK(idle.nav.right == "Left-click to read - right-click opens it");
  CHECK(idle.nav.main.compare(0, 10, "Holding at") == 0);
  CHECK(idle.nav.main.compare(idle.nav.main.size() - 20, 20,
                              " - no course plotted") == 0);
  CHECK_FALSE(idle.docked);
  CHECK(idle.feedHead == "Activity Feed");
  CHECK(idle.feedHeight == ship::GiveFeedHeight(742.0f, 420.0f));
  // The feed sits below the rails, never over them.
  CHECK(idle.feedBox.y > idle.infos.front().box.y);
}

TEST_CASE("a docked ship grows a dock strip, and a crossing says so", "[ui][idle]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::Theme theme = ship::Theme::ById("classic");
  const ship::ShellLayout layout =
      ship::ComputeLayout(1280.0f, 800.0f, 742.0f, 420.0f, true);

  const std::string port = World().ports.front().id;
  sim.MutableMap().here = port;
  sim.MutableState().docked = true;
  const ship::IdleScreenModel docked =
      ship::BuildIdleScreen(sim, layout, theme, false, 420.0f, rng);
  CHECK(docked.docked);
  CHECK(docked.dock.label == "Docked");
  CHECK(docked.dock.main == World().ports.front().name);

  sim.MutableState().docked = false;
  ship::Task* travel = sim.AddOngoing("travel", "Under way to Vega", 40.0, "travel", "vega");
  REQUIRE(travel != nullptr);
  travel->state = "running";
  CHECK(ship::BuildIdleScreen(sim, layout, theme, false, 420.0f, rng).nav.main ==
        "Under way to Vega - crossing in progress");
  travel->state = "done";
  CHECK(ship::BuildIdleScreen(sim, layout, theme, false, 420.0f, rng).nav.main ==
        "Crossing complete - awaiting acknowledgement");
}

// ---------------------------------------------------------------------------
//  Home layout under the 18 px typography floor (UI-001)
// ---------------------------------------------------------------------------

TEST_CASE("home stat cells grow with the font and hold label above value",
          "[ui][idle][layout]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::ShellLayout layout =
      ship::ComputeLayout(1440.0f, 940.0f, 940.0f - 36.0f - 22.0f, 300.0f, true);
  auto typePx = [](int ref, float fontPx) {
    return std::max(fontPx, std::round(ref * fontPx / 13.0f));
  };
  float prevH = -1.0f;
  for (int step : {0, 2, 3}) {
    ship::Theme theme = ship::Theme::ById("classic");
    theme.fontStep = step;
    const float fontPx = ship::Theme::FontPxForStep(step);
    const float labelH = typePx(11, fontPx);
    const float valueH = typePx(20, fontPx);
    // Cell must hold: pad + label + gap + value + bar zone (Bottom()-12) + pad.
    const float need =
        6.0f + labelH + 6.0f + valueH + 12.0f + 6.0f;
    const ship::IdleScreenModel idle =
        ship::BuildIdleScreen(sim, layout, theme, false, 300.0f, rng);
    REQUIRE_FALSE(idle.stats.empty());
    CHECK(idle.stats.front().box.h >= need);
    // Boxes grow as the typography step grows; they never shrink below the fit.
    if (prevH >= 0.0f) {
      CHECK(idle.stats.front().box.h > prevH);
    }
    prevH = idle.stats.front().box.h;
    // The value baseline is one label height plus a gap below the label, and
    // the bar is drawn at Bottom()-12, so both fit inside the cell.
    const float valueBottom = 6.0f + labelH + 6.0f + valueH;
    CHECK(valueBottom <= idle.stats.front().box.h - 12.0f);
  }
}

TEST_CASE("home info rail uses compact sizes that follow reading preferences", "[ui][idle][layout]") {
  auto sim = Fresh();
  ship::FxRng rng;
  const auto layout = ship::ComputeLayout(1440.0f, 940.0f, 882.0f, 300.0f, true);
  int previousLabel = 0, previousValue = 0;
  for (int step : {0, 1, 2, 3}) {
    auto theme = ship::Theme::ById("classic");
    theme.fontStep = step;
    const int label = theme.HomeInfoLabelPx(), value = theme.HomeInfoValuePx();
    CHECK(label >= 12);
    CHECK(value > label);
    CHECK(label >= previousLabel);
    CHECK(value >= previousValue);
    const auto idle = ship::BuildIdleScreen(sim, layout, theme, false, rng);
    REQUIRE_FALSE(idle.infos.empty());
    CHECK(idle.infos.front().box.h >= 6 + label + 6 + value + 6);
    CHECK(idle.feedBox.y > idle.infos.front().box.Bottom());
    previousLabel = label;
    previousValue = value;
  }
  auto standard = ship::Theme::ById("classic");
  auto large = standard;
  large.largeText = true;
  CHECK(standard.HomeInfoLabelPx() == 12);
  CHECK(standard.HomeInfoValuePx() == 15);
  CHECK(large.HomeInfoLabelPx() > standard.HomeInfoLabelPx());
  CHECK(large.HomeInfoValuePx() > standard.HomeInfoValuePx());
}

TEST_CASE("home nav and dock strips grow with the font and never overlap",
          "[ui][idle][layout]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::ShellLayout layout =
      ship::ComputeLayout(1440.0f, 940.0f, 940.0f - 36.0f - 22.0f, 300.0f, true);
  auto typePx = [](int ref, float fontPx) {
    return std::max(fontPx, std::round(ref * fontPx / 13.0f));
  };
  for (int step : {0, 2}) {
    ship::Theme theme = ship::Theme::ById("classic");
    theme.fontStep = step;
    const float fontPx = ship::Theme::FontPxForStep(step);
    const float labelH = typePx(11, fontPx);
    const float mainH = typePx(13, fontPx);
    const float rightH = typePx(10, fontPx);
    const float need =
        6.0f + labelH + 6.0f + mainH + 6.0f + rightH + 6.0f;
    const ship::IdleScreenModel idle =
        ship::BuildIdleScreen(sim, layout, theme, false, 300.0f, rng);
    CHECK(idle.nav.box.h >= need);
    // label < main < right in vertical order, each below the previous bottom.
    CHECK(idle.nav.box.y + 6.0f + labelH + 6.0f <=
          idle.nav.box.y + 6.0f + labelH + 6.0f + mainH);
    const float rightBottom =
        6.0f + labelH + 6.0f + mainH + 6.0f + rightH;
    CHECK(rightBottom <= idle.nav.box.h - 6.0f);
  }
}

TEST_CASE("home dock strip sits below the actual nav strip, docked and undocked",
          "[ui][idle][layout]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::Theme theme = ship::Theme::ById("classic");
  const ship::ShellLayout layout =
      ship::ComputeLayout(1440.0f, 940.0f, 940.0f - 36.0f - 22.0f, 300.0f, true);

  // Undocked: no dock strip, nothing below the nav strip.
  {
    const ship::IdleScreenModel idle =
        ship::BuildIdleScreen(sim, layout, theme, false, 300.0f, rng);
    CHECK_FALSE(idle.docked);
    CHECK(idle.dock.box.h == 0.0f);
    CHECK(idle.dock.box.y == idle.nav.box.Bottom());
  }

  // Docked: the dock strip starts at the nav strip's real bottom + 10, never at
  // a hard-coded 50 px offset, so a grown nav strip cannot overlap it.
  {
    const std::string port = World().ports.front().id;
    sim.MutableMap().here = port;
    sim.MutableState().docked = true;
    const ship::IdleScreenModel docked =
        ship::BuildIdleScreen(sim, layout, theme, false, 300.0f, rng);
    CHECK(docked.docked);
    CHECK(docked.dock.label == "Docked");
    CHECK(docked.dock.main == World().ports.front().name);
    CHECK(docked.dock.box.y == docked.nav.box.Bottom() + 10.0f);
    CHECK(docked.dock.box.h > 0.0f);
  }
}

TEST_CASE("home column overflows the content area on a short/narrow viewport",
          "[ui][idle][layout]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  ship::Theme theme = ship::Theme::ById("classic");
  // A short, narrow window: the stacked home column is taller than the content.
  const ship::ShellLayout layout =
      ship::ComputeLayout(1024.0f, 768.0f, 768.0f - 36.0f - 22.0f, 300.0f, true);
  const ship::IdleScreenModel idle =
      ship::BuildIdleScreen(sim, layout, theme, false, 300.0f, rng);
  // content.Bottom() = 36 + (768 - 36 - 22) = 746.
  CHECK(idle.feedBox.Bottom() > idle.content.Bottom());
  // When the column overflows, the feed (and everything above it) is clipped
  // and unreachable unless the shell scrolls. Draw() converts this condition
  // into contentMaxScroll_ = feedBox.Bottom() - content.Bottom() (> 0); the
  // same offset is applied to home hit testing so obscured content can't be
  // clicked. This is main-content scrolling (#content { overflow:auto }), not
  // a separate feed-internal scroll.
}

// ---------------------------------------------------------------------------
//  Effects: cosmetic, deterministic, and never in the way
// ---------------------------------------------------------------------------
TEST_CASE("dots drift half a pixel and pause when they arrive", "[ui][fx]") {
  ship::FxRng rng;
  rng.state = 12345u;
  const ship::Area room{100.0f, 112.0f, 160.0f, 94.0f};

  ship::DriftDot dot;
  dot.id = "jax";
  dot.roomId = "engine";
  dot.x = 120.0f;
  dot.y = 140.0f;
  ship::PickTarget(dot, room, false, rng);
  CHECK(dot.tx >= room.x);
  CHECK(dot.tx <= room.x + room.w);
  CHECK(dot.ty >= room.y);
  CHECK(dot.ty <= room.y + room.h);

  dot.tx = dot.x + 10.0f;
  dot.ty = dot.y;
  dot.wait = 0;
  ship::DriftStep(dot, room, false, rng);
  CHECK(dot.x == 120.5f);      // half a pixel along the line

  dot.wait = 3;
  const float before = dot.x;
  ship::DriftStep(dot, room, false, rng);
  CHECK(dot.wait == 2);
  CHECK(dot.x == before);      // a waiting dot does not move

  // Arrived: it pauses for 20..109 ticks and picks a fresh target.
  dot.x = dot.tx;
  dot.y = dot.ty;
  dot.wait = 0;
  const float arrivedX = dot.x;
  ship::DriftStep(dot, room, false, rng);
  CHECK(dot.wait >= 20);
  CHECK(dot.wait <= 109);
  CHECK(dot.x == arrivedX);
}

TEST_CASE("an action mark holds, rises, fades and retires", "[ui][fx]") {
  std::vector<ship::ActionMark> marks;
  CHECK(ship::SpawnMark(marks, 100.0f, 200.0f, 1000.0, false));
  REQUIRE(marks.size() == 1);
  CHECK(marks[0].x == 100.0f - 5.0f);
  CHECK(marks[0].y == 200.0f - 5.0f);

  ship::MarkStyle style = ship::MarkAt(marks[0], 1100.0);  // still holding
  CHECK(style.alive);
  CHECK(style.top == 195.0f);
  CHECK(style.opacity == 1.0f);

  // Half way through the fade: eased 50%, faded 50%.
  style = ship::MarkAt(marks[0], 1000.0 + 220.0 + 475.0);
  CHECK(style.alive);
  CHECK(style.top == 195.0f - 13.0f);
  CHECK(std::abs(style.opacity - 0.5f) < 0.001f);

  CHECK_FALSE(ship::MarkAt(marks[0], 1000.0 + 220.0 + 950.0).alive);
  ship::UpdateMarks(marks, 1000.0 + 220.0 + 950.0);
  CHECK(marks.empty());

  // At most five alive: the oldest is retired early rather than stacking.
  std::vector<ship::ActionMark> hammered;
  for (int i = 0; i < 9; ++i) {
    ship::SpawnMark(hammered, static_cast<float>(i), 0.0f, static_cast<double>(i), false);
  }
  CHECK(hammered.size() == 5);
  CHECK(hammered.front().x == -1.0f);  // the fifth newest, retired first
}

TEST_CASE("reduced motion stops both effects, and neither is game state",
          "[ui][fx]") {
  std::vector<ship::ActionMark> marks;
  CHECK_FALSE(ship::SpawnMark(marks, 10.0f, 10.0f, 0.0, true));
  CHECK(marks.empty());

  // A thousand drift ticks change no number the voyage is scored on.
  ship::Simulation sim = Fresh();
  const std::string before = sim.TraceJson();
  ship::FxRng rng;
  ship::DriftDot dot;
  dot.id = "bo";
  const ship::Area corridor{100.0f, 210.0f, 740.0f, 26.0f};
  ship::PickTarget(dot, corridor, false, rng);
  for (int i = 0; i < 1000; ++i) {
    ship::DriftStep(dot, corridor, false, rng);
    ship::UpdateMarks(marks, static_cast<double>(i) * 16.0);
  }
  CHECK(sim.TraceJson() == before);
}

// ---------------------------------------------------------------------------
//  The two verbs
// ---------------------------------------------------------------------------
TEST_CASE("a left-click describes, a right-click opens a tab", "[ui][verbs]") {
  ship::Simulation sim = Fresh();
  const ship::Theme theme = ship::Theme::ById("classic");
  const ship::ShellLayout layout =
      ship::ComputeLayout(1280.0f, 800.0f, 742.0f, 420.0f, true);
  ship::FxRng rng;
  const ship::IdleScreenModel idle =
      ship::BuildIdleScreen(sim, layout, theme, false, 420.0f, rng);

  // Pick a room's centre in window coordinates, then hand it to the shell.
  const ship::Room& o2 = *ship::ActivePlan(sim).FindRoom("o2");
  const float x = idle.stageArea.x + o2.box.x + o2.box.w / 2.0f;
  const float y = idle.stageArea.y + o2.box.y + o2.box.h / 2.0f;

  // The shell's own hit test is the thing that must find the room.
  CHECK(idle.stageArea.Contains(x, y));
  const ship::Description described = ship::DescribeTarget(sim, "room", "o2");
  CHECK(described.title == "O2 Filter Room");

  // Right-clicking that room opens its window and focuses it.
  CHECK(ship::OpenTask(sim, "room", "o2", true));
  CHECK(sim.State().activeTab == "room:o2");
  CHECK(sim.State().tabs[0].kindLabel == "ROOM");
  CHECK(sim.State().tabs[0].title == "O2 Filter Room");
}

// ---------------------------------------------------------------------------
//  UI-00x regression: stat units, font measurement, compact labels
// ---------------------------------------------------------------------------
TEST_CASE("home stat cells carry their units (UI-007)", "[ui][idle][layout]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::Theme theme = ship::Theme::ById("classic");
  const ship::ShellLayout layout =
      ship::ComputeLayout(1440.0f, 940.0f, 940.0f - 36.0f - 22.0f, 300.0f, true);
  const ship::IdleScreenModel idle =
      ship::BuildIdleScreen(sim, layout, theme, false, 300.0f, rng);
  REQUIRE_FALSE(idle.stats.empty());
  // Every stat cell carries a unit string (e.g. "%", " air", " action").
  // BuildIdleScreen populates cell.unit; PaintIdle paints it inline with the
  // value so it is never silently dropped.
  for (const ship::RailCell& cell : idle.stats) {
    CHECK_FALSE(cell.unit.empty());
  }
  // The stat unit is inline with the value: cell.value and cell.unit together
  // must fit inside the cell width, so the value baseline and bar both fit.
  CHECK(idle.stats.front().box.w >= 6.0f + 6.0f);
}

TEST_CASE("reading size changes small labels and keeps the sidebar compact", "[ui][theme][layout]") {
  int previous = 0;
  for (int step = 0; step < 4; ++step) {
    ship::Theme theme = ship::Theme::ById("classic");
    theme.fontStep = step;
    CHECK(theme.TextPx(0) == 0);
    for (int reference : {8, 9, 10, 11, 12, 13}) {
      CHECK(theme.TextPx(reference) == 18 + step);
    }
    CHECK(theme.TextPx(10) > previous);
    CHECK(theme.SidebarPx() == theme.FontPx() - 2);
    CHECK(theme.SidebarPx() < theme.TextPx(10));
    CHECK(theme.HomeInfoLabelPx() == 12 + step);
    CHECK(theme.HomeInfoValuePx() == 15 + step);
    previous = theme.TextPx(10);
  }
  auto enlarged = ship::Theme::ById("classic");
  enlarged.largeText = true;
  CHECK(enlarged.TextPx(10) == 20);
  CHECK(enlarged.SidebarPx() == 18);
  CHECK(enlarged.HomeInfoLabelPx() == 14);
  CHECK(enlarged.HomeInfoValuePx() == 17);
}

TEST_CASE("home diagram compact rooms carry names and tags in the model "
          "(UI-006)", "[ui][diagram][layout]") {
  ship::Simulation sim = Fresh();
  ship::FxRng rng;
  const ship::Theme theme = ship::Theme::ById("classic");
  const ship::ShellLayout compact =
      ship::ComputeLayout(1024.0f, 768.0f, 768.0f - 36.0f - 22.0f, 300.0f, true);
  const ship::IdleScreenModel idle =
      ship::BuildIdleScreen(sim, compact, theme, false, 300.0f, rng);
  const auto& plan = ship::ActivePlan(sim);
  REQUIRE_FALSE(plan.rooms.empty());
  for (const ship::Room& room : plan.rooms) {
    // The model always carries the room name and tag; PaintDiagram paints
    // names in both compact and expanded decks (tags only when expanded).
    CHECK_FALSE(room.name.empty());
    CHECK_FALSE(room.tag.empty());
  }
}

TEST_CASE("floating briefings coexist and keyed settings replace only themselves", "[ui][popups]") {
  ship::FloatingPanels windows;
  const int first = windows.Open("", {}).serial;
  const int settings = windows.Open("settings").serial;
  const int log = windows.Open("log").serial;
  const int second = windows.Open("", {}).serial;
  const int replacement = windows.Open("settings").serial;
  REQUIRE(windows.Panels().size() == 4);
  CHECK(windows.HasBriefings());
  CHECK(windows.OpenedCount() == 5);
  CHECK(windows.Panels()[0].serial == first);
  CHECK(windows.Panels()[1].serial == log);
  CHECK(windows.Panels()[2].serial == second);
  CHECK(windows.Panels()[3].serial == replacement);
  CHECK(windows.Panels()[3].serial != settings);
  CHECK(windows.Panels()[0].x == 300);
  CHECK(windows.Panels()[0].y == 90);
  CHECK(windows.Panels()[3].x == 388);
  CHECK(windows.Panels()[3].y == 178);
  const int wrapped = windows.Open("manifest").serial;
  CHECK(windows.Panels().back().x == 300);
  windows.Close(log);
  CHECK(windows.Panels().size() == 4);
  CHECK(windows.Panels().back().serial == wrapped);
  windows.Clear();
  CHECK(windows.Panels().empty());
}

TEST_CASE("file windows replace only files and retain independent panel state", "[ui][popups]") {
  ship::FloatingPanels windows;
  const int log = windows.Open("log").serial;
  auto& files = windows.Open("files");
  const int oldFiles = files.serial;
  files.slot = 3;
  files.message = "Saved voyage restored.";
  files.pendingAction = "save";
  CHECK(windows.HasConfirmation());
  files.pendingSlot = 3;
  files.slot = 1;
  CHECK(files.pendingSlot == 3);
  windows.Open("files");
  REQUIRE(windows.Panels().size() == 2);
  CHECK(windows.Panels()[0].serial == log);
  CHECK(windows.Panels()[1].serial != oldFiles);
  CHECK(windows.Panels()[1].slot == 1);
  CHECK(windows.Panels()[1].pendingAction.empty());
  CHECK_FALSE(windows.HasConfirmation());
  CHECK(windows.Panels()[1].message.empty());
}

TEST_CASE("brief popup models preserve authored rows actions and right-click instructions", "[ui][popups]") {
  auto sim = Fresh();
  const auto before = sim.ActionCount();
  const auto brief = ship::BuildBriefPopup(sim, "room", "o2");
  const auto original = ship::DescribeTarget(sim, "room", "o2");
  CHECK(brief.title == "ROOM - O2 Filter Room");
  CHECK(brief.notice == original.sub);
  REQUIRE(brief.sections.size() == 1);
  REQUIRE(brief.sections[0].rows.size() == std::min<std::size_t>(5, original.rows.size()));
  for (std::size_t i = 0; i < brief.sections[0].rows.size(); ++i) {
    CHECK(brief.sections[0].rows[i].key == original.rows[i].key);
    CHECK(brief.sections[0].rows[i].value == original.rows[i].value);
  }
  CHECK(brief.acts.size() == original.acts.size());
  REQUIRE(brief.footer.size() == 1);
  CHECK(brief.footer[0] == "Right-click to open the O2 Filter Room window. It waits in the tab bar - you are not taken there.");
  const auto nav = ship::BuildBriefPopup(sim, "nav", "main");
  CHECK(nav.title == "NAV - Navigation");
  REQUIRE(nav.sections[0].rows.size() == 6); // navBrief is not limited to five
  CHECK(nav.sections[0].rows[2].value == "Hold the left button and drag the chart");
  REQUIRE(nav.acts.size() == 2);
  CHECK(nav.acts[0].label == "Start Travel");
  CHECK(nav.acts[0].live);
  REQUIRE(nav.footer.size() == 2);
  CHECK(sim.ActionCount() == before);
  CHECK(sim.State().tabs.empty());
}

TEST_CASE("audio choices are available through settings and title without actions", "[ui][popups][settings]") {
  auto sim = Fresh();
  const auto before = sim.ActionCount();
  const auto settings = ship::BuildPopup(sim, "settings");
  REQUIRE(settings.audioChoices.size() == 4);
  const char* values[] = {"0.18", "0.4", "0.65", "0.9"};
  const char* labels[] = {"Quiet", "Standard", "Loud", "Very loud"};
  bool fromVoyage = true;
  auto panel = ship::TitlePanel::Settings;
  int slot = 1;
  std::string message;
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(settings.audioChoices[i].value == values[i]);
    CHECK(settings.audioChoices[i].label == labels[i]);
    REQUIRE(ship::PressTitleAction(sim, "audio:" + std::string(values[i]), "", &fromVoyage, &panel, &slot, "", &message));
    CHECK(sim.State().options.audioLevel == std::stod(values[i]));
  }
  REQUIRE(settings.toggles.size() == 6);
  CHECK(settings.toggles[0].help == "Play custom music from assets/audio/music, or the built-in shipboard theme.");
  CHECK(settings.toggles[4].heading == "Confirmations");
  CHECK(sim.ActionCount() == before);
}

TEST_CASE("closing a UI window retains ownership through mouse release", "[ui][input]") {
  ship::PointerOwnership gesture;
  gesture.Press(true);
  // Window may be removed now; the ownership must outlive its hover state.
  CHECK(gesture.UiOwned());
  CHECK(gesture.Release());
  CHECK_FALSE(gesture.UiOwned());
  gesture.Press(false);
  CHECK_FALSE(gesture.Release());
}

TEST_CASE("classic colours are literal HTA CSS roles, including title sheets", "[ui][theme]") {
  const auto p = ship::Theme::ById("classic").palette;
  const ship::Rgb actual[] = {p.white,p.panel,p.panel2,p.panel3,p.border,p.borderDark,
    p.coffee,p.coffeeDark,p.coffeeLight,p.ink,p.muted,p.terracotta,p.sage,p.amber,
    p.titleBackground,p.titleDialog,p.titleBorder,p.titleInk};
  const unsigned expected[] = {0xffffff,0xfaf7f2,0xf4efe6,0xefe8dc,0xe4dbcb,0xcbb89c,
    0x8c6f4e,0x6b5338,0xb89b76,0x3b3630,0x8a7f72,0xb05a42,0x7f9161,0xc08a3e,
    0xd8c4a6,0xf5ead8,0x8e7757,0x443528};
  for (std::size_t i = 0; i < 18; ++i) {
    CHECK(static_cast<unsigned>((actual[i].r << 16) | (actual[i].g << 8) | actual[i].b) == expected[i]);
  }
}

TEST_CASE("sidebar paint origins survive clipping and footer hits stay pinned", "[ui][shell]") {
  auto sim = Fresh();
  sim.MutableState().spOpen["vessel"] = true;
  const ship::Area sidebar{0,36,165,300}, foot{0,336,165,96};
  float total = 0;
  const auto hits = ship::SideHits(sim, sidebar, foot, 24, 30, &total,
      [](const std::string& s, float w) { return std::max(1.0f, std::ceil(s.size() * 10.0f / w)) * 24.0f; });
  CHECK(total > sidebar.h);
  int pinned = 0;
  bool partial = false;
  for (const auto& hit : hits) {
    if (hit.pinned) { ++pinned; CHECK(hit.box.y >= foot.y); continue; }
    CHECK(hit.box.y >= sidebar.y);
    CHECK(hit.box.Bottom() <= sidebar.Bottom());
    if (hit.paintBox.y < hit.box.y) { partial = true; }
  }
  CHECK(pinned == 6);
  CHECK(partial);
}

TEST_CASE("the rendered Home feed measures rails hull and conditional dock strip", "[ui][layout][audit]") {
  auto sim = Fresh();
  for (const bool docked : {false, true}) {
    sim.MutableState().docked = docked;
    for (const float height : {720.0f, 1000.0f, 1600.0f}) {
      const auto theme = ship::Theme::ById("classic");
      const auto layout = ship::ComputeLayout(1280, height, height - 58, 0, true);
      ship::FxRng rng;
      const auto idle = ship::BuildIdleScreen(sim, layout, theme, false, rng);
      CHECK(idle.feedHeight == ship::GiveFeedHeight(layout.content.h, idle.feedBox.y - layout.content.y));
      CHECK(idle.feedHeight >= 62);
      CHECK(idle.feedHeight <= 132);
      CHECK(idle.feedBox.h == idle.feedHeight);
      CHECK(idle.docked == docked);
    }
  }
}

TEST_CASE("source personal memories show newest three for either participant without ticking", "[ui][audit]") {
  auto sim = Fresh();
  const auto id = sim.Crew().front().id;
  sim.MutableState().sim.memories.clear();
  auto record = ship::BuildCrewRecord(sim, id);
  CHECK(record.shelves[2].lines.back().value == "No personal memories recorded yet.");
  for (int i = 1; i <= 5; ++i) {
    ship::Memory m; m.at = i; m.text = "memory " + std::to_string(i);
    if (i % 2) m.who = id; else m.other = id;
    sim.MutableState().sim.memories.push_back(m);
  }
  ship::Memory unrelated; unrelated.at = 6; unrelated.text = "unrelated";
  sim.MutableState().sim.memories.push_back(unrelated);
  record = ship::BuildCrewRecord(sim, id);
  const auto& lines = record.shelves[2].lines;
  REQUIRE(lines.size() == 9);
  CHECK(lines[6].value == "Action 5: memory 5");
  CHECK(lines[7].value == "Action 4: memory 4");
  CHECK(lines[8].value == "Action 3: memory 3");
  CHECK(sim.ActionCount() == 0);
}

TEST_CASE("all expired task kinds share the source GONE status and no actions", "[ui][audit]") {
  auto sim = Fresh();
  for (const auto& kind : {"crew", "event", "station", "room", "system", "info"}) {
    ship::Tab tab; tab.kind = kind; tab.id = "missing"; tab.key = std::string(kind) + ":missing";
    const auto screen = ship::BuildTabScreen(sim, tab, {});
    CHECK(screen.head.kind == "GONE");
    CHECK(screen.head.title == "No longer aboard");
    REQUIRE(screen.left.size() == 1);
    CHECK(screen.left[0].placement == "np-full");
    REQUIRE(screen.left[0].lines.size() == 2);
    CHECK(screen.left[0].lines[0].value == std::string(kind) + " missing");
    CHECK(screen.commands.empty());
    CHECK(screen.progress.empty());
  }
  CHECK(sim.ActionCount() == 0);
}

TEST_CASE("background opens and repeated same-tab switches explicitly request reveal for free", "[ui][audit]") {
  auto sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "crew", "rell", false));
  CHECK(sim.State().activeTab == "home");
  CHECK(sim.State().tabRevealKey == "crew:rell");
  sim.MutableState().tabRevealKey.clear();
  REQUIRE(ship::OpenTask(sim, "crew", "rell", false));
  CHECK(sim.State().tabs.size() == 1);
  CHECK(sim.State().tabRevealKey == "crew:rell");
  REQUIRE(ship::SwitchTab(sim, "crew:rell"));
  sim.MutableState().tabRevealKey.clear();
  REQUIRE(ship::SwitchTab(sim, "crew:rell"));
  CHECK(sim.State().tabRevealKey == "crew:rell");
  CHECK(sim.ActionCount() == 0);
}

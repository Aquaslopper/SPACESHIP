// ============================================================================
//  crew_tests.cpp — Phase 5: sprites, the order sheet and the record.
//  ---------------------------------------------------------------------------
//  The sprite tables, the posture the sheet puts a hand in, the price of an
//  order (which is also how long it takes), the free-versus-paid line, the
//  training gate and the four shelves of the record — all held still without a
//  window.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <algorithm>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/CrewRecord.h"
#include "ui/CrewSprites.h"
#include "ui/QuartersScreen.h"
#include "ui/Shell.h"

#include "ui/Widgets.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::NewVoyage(World()); }

const ship::NavPanel* Panel(const std::vector<ship::NavPanel>& panels,
                            const std::string& id) {
  for (const ship::NavPanel& panel : panels) {
    if (panel.id == id) { return &panel; }
  }
  return nullptr;
}

const ship::PanelLine* Line(const ship::NavPanel& panel, const std::string& key) {
  for (const ship::PanelLine& line : panel.lines) {
    if (line.key == key) { return &line; }
  }
  return nullptr;
}

bool HasText(const ship::NavPanel& panel, const std::string& text) {
  for (const ship::PanelLine& line : panel.lines) {
    if (line.value == text) { return true; }
  }
  return false;
}

const ship::CrewMember& Crew(const ship::Simulation& sim, const std::string& id) {
  return *sim.FindCrew(id);
}

// Advance until the task is gone. Auto-clearing tasks retire themselves the
// moment they land, so the handle goes null - never dereference it blind.
void RunUntilGone(ship::Simulation& sim, const std::string& id) {
  for (;;) {
    const ship::Task* task = sim.FindOngoing(id);
    if (task == nullptr || task->state != "running") { return; }
    sim.PerformAction();
  }
}

}  // namespace

// ---------------------------------------------------------------------------
//  The sprite
// ---------------------------------------------------------------------------
TEST_CASE("the sprite tables load whole and the cells are five pixels", "[crew]") {
  const ship::Catalog& art = World();
  REQUIRE(art.sprite.rows.size() == 21);
  CHECK(art.sprite.rows[0].size() == 18);
  CHECK(art.lie.rows.size() == 9);
  CHECK(art.lie.rows[0].size() == 26);
  CHECK(art.sit.rows.size() == 16);
  CHECK(art.spritePx == 4);
  CHECK(art.floorY == 230);
  CHECK(art.props.size() == 9);
  CHECK(art.hats.count("helmet") == 1);
  CHECK(art.jobHat.at("maintenance").first == "helmet");
  CHECK(art.jobHat.at("filters").second == "#6e8491");
  // The galley has no tool of its own: the cook stands at a bare counter.
  CHECK(art.jobTool.count("kitchen") == 0);
  CHECK(art.hairStyles.count("cowlick") == 1);
}

TEST_CASE("a standing figure is rasterised in its own colours", "[crew]") {
  ship::Simulation sim = Fresh();
  const ship::CrewMember& jax = Crew(sim, "jax");
  CHECK(jax.job == "maintenance");

  // The engineer wears the job's cloth and the hard hat's colour.
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 'c') == "#b05a42");
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 'd') == "#8f4735");
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 'j') == "#e0b53a");
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 'h') == jax.hair);
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 's') == jax.skin);
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 'e') == "#3a1c2e");
  CHECK(ship::PixelColour(sim, jax, ship::LookForCrew(sim, jax), 'b') == "#1a1614");

  // NOTHING POKES OUT OF A HELMET: the top four rows are blanked, and the hat
  // itself blanks nothing, so a hatted head is shorter by four rows of hair.
  const std::vector<std::string> hatted = ship::FigureRows(sim, jax, true);
  REQUIRE(hatted.size() == 21);
  for (int r = 0; r < 4; ++r) { CHECK(hatted[r] == World().spriteBlank); }
  CHECK(hatted[4] == World().sprite.rows[4]);
  const std::vector<std::string> bare = ship::FigureRows(sim, jax, false);
  CHECK(bare[0] == World().sprite.rows[0]);

  // A haircut is a sparse set of row overrides and nothing else.
  ship::CrewMember bald = jax;
  bald.hairStyle = "bald";
  const std::vector<std::string> baldRows = ship::FigureRows(sim, bald, false);
  CHECK(baldRows[0] == World().spriteBlank);
  CHECK(baldRows[5].find('s') != std::string::npos);

  // The female eye row is spliced into whatever row 9 the style left behind.
  ship::CrewMember woman = jax;
  woman.sex = "f";
  const std::vector<std::string> female = ship::FigureRows(sim, woman, false);
  CHECK(female[9].substr(5, 8) == "eeesseee");

  // The scene places the figure on the deck: 21 rows of 4 pixels up from 230.
  const ship::CrewScene scene = ship::BuildCrewScene(sim, jax);
  CHECK(scene.pose == "stand");
  CHECK(scene.figureY == 230 - 21 * 4);
  CHECK_FALSE(scene.pixels.empty());
  // Every cell is exactly one sprite pixel.
  for (const ship::SpritePixel& pixel : scene.pixels) {
    CHECK(pixel.box.w == 4);
    CHECK(pixel.box.h == 4);
  }
  // The engineer's wrench stands at the bench end, top-left of its own grid.
  CHECK(std::find(scene.propNames.begin(), scene.propNames.end(), "wrench") !=
        scene.propNames.end());
}

TEST_CASE("the sheet decides the posture before it is paid for", "[crew]") {
  ship::Simulation sim = Fresh();
  const ship::CrewMember& rell = Crew(sim, "rell");

  // Rell holds no job: sat on the deck with zzz overhead.
  CHECK(rell.job.empty());
  ship::CrewScene idle = ship::BuildCrewScene(sim, rell);
  CHECK(idle.pose == "sit");
  CHECK(idle.figureY == 230 - 16 * 4);

  // Staging the Doc order puts him on the cot - and it is still free.
  const double before = sim.ActionCount();
  REQUIRE(ship::PressQuartersAction(sim, "select", "rell"));
  REQUIRE(ship::PressQuartersAction(sim, "order", "doc"));
  CHECK(sim.ActionCount() == before);
  const ship::CrewScene cot = ship::BuildCrewScene(sim, rell);
  CHECK(cot.pose == "lie");
  CHECK(cot.figureX == 230);
  CHECK(std::find(cot.propNames.begin(), cot.propNames.end(), "bed") !=
        cot.propNames.end());
  // The patient gown: even the buckle goes quiet.
  const ship::CrewLook gown = ship::LookForCrew(sim, rell);
  CHECK(gown.cloth == "#e8e2d6");
  CHECK(gown.dark == "#cbb89c");

  // Staging a trade instead puts him at a bench with a crate at his feet.
  ship::Simulation second = Fresh();
  REQUIRE(ship::PressQuartersAction(second, "select", "rell"));
  REQUIRE(ship::PressQuartersAction(second, "job", "filters"));
  const ship::CrewScene school = ship::BuildCrewScene(second, Crew(second, "rell"));
  CHECK(school.pose == "stand");
  CHECK(std::find(school.propNames.begin(), school.propNames.end(), "crate") !=
        school.propNames.end());
  CHECK(std::find(school.propNames.begin(), school.propNames.end(), "tank") !=
        school.propNames.end());
}

// ---------------------------------------------------------------------------
//  The order sheet
// ---------------------------------------------------------------------------
TEST_CASE("staging is free and committing is one action", "[crew]") {
  ship::Simulation sim = Fresh();
  const double before = sim.ActionCount();

  REQUIRE(ship::PressQuartersAction(sim, "select", "rell"));
  REQUIRE(ship::PressQuartersAction(sim, "job", "cleaner"));
  REQUIRE(ship::PressQuartersAction(sim, "order", "rest"));
  CHECK(sim.ActionCount() == before);   // nothing written has cost anything

  // The price of an order is also how long it takes.
  CHECK(sim.JobCost("cleaner") == 1);
  CHECK(sim.OrderCostFor("cleaner", "rest") == 1);
  CHECK(sim.OrderCostFor("cleaner", "doc") == 1);
  CHECK(sim.OrderCostFor("cleaner", "fire") == 5);
  CHECK(sim.OrderCostFor("cleaner", "learnOn") == 1);
  CHECK(sim.OrderCostFor("cleaner", "learnSt") == 2);
  CHECK(sim.CrewCostOf(Crew(sim, "rell")) == 2);   // 1 for the trade + 1 for the rest
  CHECK(sim.OrdersCost() == sim.CrewCostOf(Crew(sim, "rell")));

  const ship::QuartersScreenModel staged = ship::BuildQuartersScreen(sim);
  CHECK(staged.bandNote.find("Orders written for 1 crew member.") == 0);
  REQUIRE(staged.screen.progress.size() == 1);
  CHECK(staged.screen.progress[0].idleLabel == "Assign Tasks (2 Actions)");
  CHECK_FALSE(staged.closed);

  const ship::CrewMember& rell = Crew(sim, "rell");
  const double perPerson = sim.SheetCost(rell, *sim.SheetOf(rell));

  // Handing it over: one action, then the work is the work.
  REQUIRE(ship::PressQuartersAction(sim, "commit"));
  CHECK(sim.ActionCount() == before + 1);
  const ship::Task* bar = sim.FindOngoing("crew-assign");
  REQUIRE(bar != nullptr);
  CHECK(bar->label == "Carrying out orders");
  CHECK(bar->total == perPerson);
  const ship::Task* own = sim.FindOngoing("cw-rell");
  REQUIRE(own != nullptr);
  CHECK(own->label == "Orders - Rell");
  CHECK(own->total == perPerson);
  CHECK(own->left == perPerson);

  // Nobody takes new orders until the sheet is empty: the pad is shut.
  const ship::QuartersScreenModel shut = ship::BuildQuartersScreen(sim);
  CHECK(shut.closed);
  CHECK(shut.closedText.find("Carrying out orders.") == 0);

  // The rest lands when the work finishes, not when it was ordered.
  const double morale = rell.morale;
  RunUntilGone(sim, "cw-rell");
  CHECK(Crew(sim, "rell").morale == morale + 5);
  CHECK(Crew(sim, "rell").job == "cleaner");
  CHECK(Crew(sim, "rell").skill == "Untrained");
}

TEST_CASE("the doc keeps a body after the sheet is discharged", "[crew]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.ToggleOrder("rell", "doc"));
  REQUIRE(sim.CommitOrders());
  RunUntilGone(sim, "cw-rell");

  // The sheet is done; the doctor is not.
  const ship::Task* doc = sim.FindOngoing("doc-rell");
  REQUIRE(doc != nullptr);
  CHECK(doc->label == "Medical - Rell");
  CHECK(doc->total == sim.GameRules().docAway);
  CHECK(Crew(sim, "rell").activity == "In the medical bay");
  CHECK(sim.CrewWhere(Crew(sim, "rell")) == "Medical Bay");

  const double health = Crew(sim, "rell").health;
  RunUntilGone(sim, "doc-rell");
  CHECK(Crew(sim, "rell").health == std::min(100.0, health + 12));
  CHECK(Crew(sim, "rell").activity == "Back on station");
}

TEST_CASE("a unique trade belongs to one pair of hands", "[crew]") {
  ship::Simulation sim = Fresh();
  CHECK(Crew(sim, "jax").job == "maintenance");
  sim.ApplyJobTo(*sim.FindCrew("bo"), "maintenance");
  CHECK(Crew(sim, "jax").job.empty());
  CHECK(Crew(sim, "bo").job == "maintenance");
  CHECK(Crew(sim, "bo").room == "engine");
  CHECK(Crew(sim, "bo").jobAt == 30);

  // Staging a trade that is already theirs stages nothing: no change, no charge.
  REQUIRE(sim.StageJob("bo", "maintenance"));
  CHECK(sim.SheetOf(Crew(sim, "bo")) == nullptr);
}

TEST_CASE("training needs a dock, a fee, and a job to use it on", "[crew]") {
  ship::Simulation sim = Fresh();
  // Aboard: the station lesson is refused outright.
  CHECK_FALSE(sim.ToggleOrder("bo", "learnSt"));
  // ...and the aboard lesson is allowed but free of station instructors.
  REQUIRE(sim.ToggleOrder("bo", "learnOn"));
  CHECK(sim.OrderFeeFor("filters", "learnOn") == 40);
  CHECK(sim.OrdersFee() == 40);

  const double purse = sim.State().shiplets;
  REQUIRE(sim.CommitOrders());
  CHECK(sim.State().shiplets == purse - 40);   // the fees are paid up front
  RunUntilGone(sim, "cw-bo");
  CHECK(Crew(sim, "bo").skill == "Trained");
  CHECK(Crew(sim, "bo").jobAt == 2);   // filters: 1 + one step of 1

  // At a station, the same order teaches two steps and is priced at 60.
  ship::Simulation docked = Fresh();
  docked.MutableState().docked = true;
  REQUIRE(docked.ToggleOrder("jax", "learnSt"));
  CHECK(docked.OrderFeeFor("maintenance", "learnSt") == 50);
  REQUIRE(docked.CommitOrders());
  RunUntilGone(docked, "cw-jax");
  CHECK(Crew(docked, "jax").jobAt == 60);   // 30 + 2 x 15
  CHECK(Crew(docked, "jax").skill == "Trained");
}

TEST_CASE("laying a hand off empties their trade and stops the wage", "[crew]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.ToggleOrder("suri", "fire"));
  CHECK(sim.CrewCostOf(Crew(sim, "suri")) == 5);
  REQUIRE(sim.CommitOrders());
  RunUntilGone(sim, "cw-suri");
  CHECK(Crew(sim, "suri").job.empty());
  CHECK(Crew(sim, "suri").morale == 78 - 12);
  CHECK(sim.CrewWage(Crew(sim, "suri")) == 0);
  CHECK(sim.CrewWhere(Crew(sim, "suri")) == "Corridor - no job");
}

// ---------------------------------------------------------------------------
//  The record
// ---------------------------------------------------------------------------
TEST_CASE("the record has four shelves and reads them off the live crew",
          "[crew][record]") {
  ship::Simulation sim = Fresh();
  const ship::CrewRecordModel record = ship::BuildCrewRecord(sim, "jax");
  CHECK(record.head.kind == "CREW");
  CHECK(record.head.title == "Jax Moreno");
  CHECK(record.head.sub == Crew(sim, "jax").role);
  REQUIRE(record.shelves.size() == 4);
  CHECK(record.shelves[0].title == "Status");
  CHECK(record.shelves[1].title == "Profile");
  CHECK(record.shelves[2].title == "Personality");
  CHECK(record.shelves[3].title == "Employment");

  const ship::NavPanel& status = record.shelves[0];
  CHECK(Line(status, "Health")->value == "88%  Excellent");
  CHECK(Line(status, "Fatigue")->value == "52%  Tiring");  // 100-52=48, "Tiring"
  CHECK(Line(status, "Morale")->value == "64%  Wavering");

  const ship::NavPanel& profile = record.shelves[1];
  CHECK(Line(profile, "Race")->value == "Human");
  CHECK(Line(profile, "Gender")->value == "Male");
  CHECK(Line(profile, "Age")->value.size() > 0);
  CHECK(Line(profile, "Station")->value == "Engine Room");
  CHECK(Line(profile, "Doing")->value == Crew(sim, "jax").activity);

  const ship::NavPanel& person = record.shelves[2];
  CHECK(Line(person, "Outlook")->value == "Still finding their feet in the job");
  CHECK(Line(person, "Hobby")->value.empty() == false);
  CHECK(Line(person, "Name anchor")->value.empty() == false);

  const ship::NavPanel& employment = record.shelves[3];
  CHECK(Line(employment, "Job")->value == "Engineer");
  CHECK(Line(employment, "Skill")->value == "Untrained");
  CHECK(Line(employment, "Acts at")->value == "30% condition");
  CHECK(Line(employment, "Pay")->value == "10 sh each payday");
  CHECK(Line(employment, "Next payday")->value == "100 actions");
  CHECK(Line(employment, "Wallet")->value.size() > 0);

  // Reading a record is free: no clock moved.
  CHECK(sim.ActionCount() == 0);

  // The words the tone agrees with, read either way up.
  CHECK(ship::VitalWord(91, false, {"Excellent", "Strained", "Critical"}) == "Excellent");
  CHECK(ship::VitalWord(91, true, {"Rested", "Tiring", "Exhausted"}) == "Exhausted");
  CHECK(ship::VitalWord(50, false, {"Excellent", "Strained", "Critical"}) == "Strained");
  CHECK(ship::OutlookWord("ill") == "Not feeling well");
  CHECK(ship::GenderWord("f") == "Female");
  CHECK(ship::GenderWord("") == "Unspecified");
}

// ---------------------------------------------------------------------------
//  Routing and the pad's own words
// ---------------------------------------------------------------------------
TEST_CASE("the quarters and the record open from their own tabs", "[crew]") {
  ship::Simulation sim = Fresh();

  REQUIRE(ship::OpenTask(sim, "room", "quarters", true));
  const ship::Tab* room = ship::FindTab(sim.State(), "room:quarters");
  REQUIRE(room != nullptr);
  CHECK(ship::ScreenKindFor(*room) == ship::ScreenKind::Quarters);
  const ship::TaskScreenModel routed = ship::BuildTabScreen(sim, *room);
  CHECK(routed.head.title == "Crew Quarters");
  CHECK(routed.stage.id == "hallway");
  CHECK(routed.stage.width == 560.0f);
  CHECK(routed.stage.height == 300.0f);

  const ship::QuartersScreenModel quarters = ship::BuildQuartersScreen(sim);
  REQUIRE(quarters.roster.size() == 4);
  CHECK(quarters.roster[0].name == "Rell Aventine");
  // shortRole() cuts the descriptor at " /" and appends the trade.
  CHECK(quarters.roster[0].role.find(" /") == std::string::npos);
  CHECK(quarters.roster[1].role == quarters.roster[1].role);
  CHECK(quarters.roster[1].flag.empty());
  CHECK(quarters.payEvery == "100 actions");
  CHECK(quarters.payNext == "in 100");
  CHECK(quarters.bandNote == "No orders written.");
  CHECK(quarters.screen.progress.size() == 1);
  CHECK(quarters.screen.progress[0].idleLabel == "Assign Tasks (0 Actions)");
  CHECK(quarters.screen.progress[0].state == ship::ButtonState::Off);

  // The pad, word for word: five orders, priced against the job they hold,
  // and the station lesson refused while there is no dock.
  REQUIRE(quarters.orders.size() == 5);
  CHECK(quarters.orders[0].label == "Send to Doc");
  CHECK(quarters.orders[0].note == "away up to 7 actions");
  CHECK(quarters.orders[0].cost == "1");
  CHECK(quarters.orders[1].label == "Learn - aboard");
  CHECK(quarters.orders[1].note == "cheap, one step");
  CHECK(quarters.orders[2].label == "Learn - station");
  CHECK(quarters.orders[2].locked);
  CHECK(quarters.orders[2].note == "no station in reach");
  CHECK(quarters.orders[2].state == "off");
  CHECK(quarters.orders[3].label == "Lay off");
  CHECK(quarters.orders[3].cost == "5");
  CHECK(quarters.orders[4].label == "Rest");

  // Selecting is free, and the roster's busy flag is the countdown.
  const double before = sim.ActionCount();
  REQUIRE(ship::PressQuartersAction(sim, "select", "suri"));
  CHECK(sim.State().crewSel == "suri");
  CHECK(sim.ActionCount() == before);

  // Talking is free and hands out nothing but words.
  REQUIRE(ship::PressQuartersAction(sim, "talk"));
  CHECK_FALSE(Crew(sim, "suri").said.empty());
  CHECK(sim.ActionCount() == before);
  CHECK(sim.FindOngoing("doc-suri") == nullptr);

  // A crew tab is the read-only record.
  REQUIRE(ship::OpenTask(sim, "crew", "jax", true));
  const ship::Tab* card = ship::FindTab(sim.State(), "crew:jax");
  REQUIRE(card != nullptr);
  CHECK(ship::ScreenKindFor(*card) == ship::ScreenKind::CrewRecord);
  const ship::TaskScreenModel record = ship::BuildTabScreen(sim, *card);
  CHECK(record.left.size() == 4);
  CHECK(record.left[0].title == "Status");
  CHECK(record.left[3].title == "Employment");
}

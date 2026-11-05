// ============================================================================
//  task_tests.cpp — Phase 3: the frame, the button, the lock and the machines.
//  ---------------------------------------------------------------------------
//  The reference builds its task windows from data, so the port can hold them
//  still without a window: the illustrations are counted, the button's three
//  states are walked, the cooldown keys are checked per tab, and the opening
//  loop (seat, bank, maintain, cook) is driven through the same functions the
//  pointer calls.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/EngineScreen.h"
#include "ui/MessScreen.h"
#include "ui/O2Screen.h"
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

int CountClass(const ship::TaskStage& stage, const std::string& cls) {
  int n = 0;
  for (const ship::MachinePart& part : stage.parts) {
    if (part.cls == cls) { ++n; }
  }
  return n;
}

ship::Tab TabOf(const std::string& kind, const std::string& id) {
  ship::Tab tab;
  tab.kind = kind;
  tab.id = id;
  tab.key = kind + ":" + id;
  return tab;
}

}  // namespace

// ---------------------------------------------------------------------------
//  The three machines
// ---------------------------------------------------------------------------
TEST_CASE("the engine is a 225-part machine on a 650x390 stage", "[ui][machine]") {
  ship::Simulation sim = Fresh();
  const ship::TaskStage stage = ship::BuildEngineMachine(sim);
  CHECK(stage.id == "machine");
  CHECK(stage.width == 650.0f);
  CHECK(stage.height == 390.0f);
  CHECK(stage.parts.size() == 225);

  // The authored counts, one line each.
  CHECK(CountClass(stage, "mach-brk") == 8);      // four brackets, two arms each
  CHECK(CountClass(stage, "mach-rivet") == 20);   // five valves' worth of pipe
  CHECK(CountClass(stage, "mach-valve") == 5);
  CHECK(CountClass(stage, "mach-cap") == 6);      // five valves + the core label
  // 24 encoder ticks + 8 blades + 6 turbine + 22 around the two gauges.
  CHECK(CountClass(stage, "mach-tick") == 60);
  CHECK(CountClass(stage, "mach-bolt") == 12);
  CHECK(CountClass(stage, "mach-fin") == 9);
  CHECK(CountClass(stage, "mach-cyl") == 16);
  CHECK(CountClass(stage, "mach-scale") == 11);
  CHECK(CountClass(stage, "mach-stat") == 9);
  CHECK(CountClass(stage, "mach-lamp") == 1);     // the one that is out
  CHECK(CountClass(stage, "mach-lamp mach-lamp-on") == 6);
  CHECK(CountClass(stage, "mach-lamp mach-lamp-amb") == 1);

  // The one live reading.
  CHECK(CountClass(stage, "mach-fill2") == 1);
  const ship::MachinePart* fill = nullptr;
  for (const ship::MachinePart& part : stage.parts) {
    if (part.cls == "mach-fill2") { fill = &part; }
  }
  REQUIRE(fill != nullptr);
  CHECK(fill->box.w == 420.0f);   // a perfect drive fills the track
  CHECK(fill->box.x == 24.0f);
}

TEST_CASE("the filter room retains its authored housing and three live slots", "[ui][machine]") {
  ship::Simulation sim = Fresh();
  const ship::TaskStage stage = ship::BuildFilterMachine(sim);
  CHECK(stage.id == "o2mach");
  CHECK(stage.width == 600.0f);
  CHECK(stage.height == 360.0f);
  CHECK(CountClass(stage, "o2-can") == 0);  // empty slots must look empty
  CHECK(CountClass(stage, "o2-airfill") == 1);
  CHECK(CountClass(stage, "o2-airnotch") == 3);
  CHECK(stage.controls.size() == 3);
  CHECK(CountClass(stage, "o2-louvre") == 7);
  CHECK(CountClass(stage, "o2-flow") == 8);
  CHECK(CountClass(stage, "o2-slot") == 3);
  CHECK(CountClass(stage, "o2-track") == 3);
  CHECK(stage.controls[0].box.x == 25.0f);
  CHECK(stage.controls[1].box.x == 215.0f);
  CHECK(stage.controls[2].box.x == 405.0f);
}

TEST_CASE("the galley counter is 28 parts and four plates", "[ui][machine]") {
  ship::Simulation sim = Fresh();
  const ship::TaskStage stage = ship::BuildMessMachine(sim);
  CHECK(stage.id == "messmach");
  CHECK(stage.width == 600.0f);
  CHECK(stage.height == 360.0f);
  CHECK(stage.parts.size() == 28);
  CHECK(CountClass(stage, "mess-plate") == 4);
  CHECK(CountClass(stage, "mess-fill") == 1);
  CHECK(CountClass(stage, "mess-readout") == 1);
}

// ---------------------------------------------------------------------------
//  The three-state button
// ---------------------------------------------------------------------------
TEST_CASE("a progress button walks idle, running, done", "[ui][button]") {
  ship::Simulation sim = Fresh();
  ship::OpenTask(sim, "room", "o2", true);

  ship::ProgressButton idle =
      ship::BuildProgressButton(sim, "o2s0", "Install Canister", true);
  CHECK(idle.state == ship::ButtonState::Idle);
  CHECK(idle.text == "Install Canister");
  CHECK(idle.fillPercent == 0.0);
  CHECK(idle.title.empty());
  CHECK(idle.cooldownKey == "cd|room:o2|progress:o2s0");
  CHECK(idle.category == "systems");

  ship::ProgressButton off =
      ship::BuildProgressButton(sim, "o2-run", "Turn on Filter", false);
  CHECK(off.state == ship::ButtonState::Off);
  CHECK(off.text == "Turn on Filter");

  // Commit the start: a task appears and the bar reports its own progress.
  REQUIRE(sim.StartCanisterInstall(0));
  ship::ProgressButton running =
      ship::BuildProgressButton(sim, "o2s0", "Install Canister", true);
  CHECK(running.state == ship::ButtonState::Running);
  CHECK(running.text == "Seating - Slot 1");
  CHECK(running.inert);
  CHECK(running.fillPercent == 0.0);

  sim.PerformAction();
  running = ship::BuildProgressButton(sim, "o2s0", "Install Canister", true);
  CHECK(running.fillPercent == 50.0);   // floor(1 of 2)
  sim.PerformAction();

  // The seating task is consumed the moment it finishes, so the slot reports
  // itself rather than a task.
  CHECK(sim.FindOngoing("o2s0") == nullptr);
  CHECK(sim.Life().slots[0].state == "installed");

  // A finished task waiting for the player: the button says Complete.
  ship::Task* finished = sim.AddOngoing("maint", "Maintenance - the commander", 6,
                                        "maint", "engine");
  REQUIRE(finished != nullptr);
  finished->left = 0;
  finished->state = "done";
  const ship::ProgressButton done =
      ship::BuildProgressButton(sim, "maint", "Run Maintenance", true, "Complete");
  CHECK(done.state == ship::ButtonState::Done);
  CHECK(done.fillPercent == 100.0);
  CHECK(done.text == "Complete");
  CHECK(done.acknowledge);
}

TEST_CASE("a lock reads back as a cooldown, in actions", "[ui][button]") {
  ship::Simulation sim = Fresh();
  ship::OpenTask(sim, "room", "engine", true);
  sim.MutableState().actionCooldowns["cd|room:engine|progress:maint"] =
      ship::Cooldown{"systems", 7};
  sim.MutableState().actionCooldowns["cd|room:engine|command:run-maintenance:0"] =
      ship::Cooldown{"systems", 7};

  const ship::ProgressButton button =
      ship::BuildProgressButton(sim, "maint", "Run Maintenance", true);
  CHECK(button.state == ship::ButtonState::Cooldown);
  CHECK(button.text == "Cooldown: 7 actions");
  CHECK(button.title ==
        "Ship systems cooldown: 7 more actions. Other committed actions advance it; waiting does not.");
  CHECK(button.fillPercent == 0.0);

  // A command row shows the same lock in its cost badge.
  const ship::CommandRowView row =
      ship::BuildCommandRow(sim, "Run Maintenance", true, "1 action", "", 0);
  CHECK(row.coolingDown);
  CHECK(row.displayCost == "COOLDOWN 7");
  CHECK(row.title ==
        "Ship systems cooldown: 7 more actions. Other committed actions advance "
        "it; waiting does not.");
}

// ---------------------------------------------------------------------------
//  The lock's grammar
// ---------------------------------------------------------------------------
TEST_CASE("cooldown keys are scoped to the tab that owns them", "[ui][cooldown]") {
  ship::Simulation sim = Fresh();
  ship::OpenTask(sim, "room", "engine", true);
  const std::string engineKey = ship::ActionCooldownKey(sim, "progress:maint");
  CHECK(engineKey == "cd|room:engine|progress:maint");

  ship::SwitchTab(sim, "home");
  const std::string homeKey = ship::ActionCooldownKey(sim, "progress:maint");
  CHECK(homeKey == "cd|home|progress:maint");
  CHECK(homeKey != engineKey);

  // Two rows with the same label in one window get different identities.
  const ship::CommandRowView first =
      ship::BuildCommandRow(sim, "Sell a lot", true, "at a station", "", 0);
  const ship::CommandRowView second =
      ship::BuildCommandRow(sim, "Sell a lot", true, "at a station", "", 1);
  CHECK(first.cooldownKey != second.cooldownKey);
  CHECK(first.cooldownKey == "cd|home|command:sell-a-lot:0");
}

TEST_CASE("the four categories, guessed from the label", "[ui][cooldown]") {
  CHECK(ship::ActionCooldownCategory("Jettison a lot") == "critical");
  CHECK(ship::ActionCooldownCategory("Abandon commission") == "critical");
  CHECK(ship::ActionCooldownCategory("Undock") == "space");
  CHECK(ship::ActionCooldownCategory("Stand watch") == "space");
  CHECK(ship::ActionCooldownCategory("Buy supply") == "routine");
  CHECK(ship::ActionCooldownCategory("Hire a hand") == "routine");
  CHECK(ship::ActionCooldownCategory("Cook a Batch") == "systems");
  CHECK(ship::ActionCooldownCategory("Turn on Filter") == "systems");
  CHECK(ship::ActionCooldownCategory("Run Maintenance") == "systems");
  CHECK(ship::ActionCooldownCategory("Something else entirely") == "routine");
  // A preferred category wins, but only if it is one of the four.
  CHECK(ship::ActionCooldownCategory("Undock", "critical") == "critical");
  CHECK(ship::ActionCooldownCategory("Undock", "nonsense") == "space");

  CHECK(ship::IsActionCost("", false));
  CHECK(ship::IsActionCost("1 action"));
  CHECK(ship::IsActionCost("3 actions"));
  CHECK_FALSE(ship::IsActionCost("free"));
  CHECK_FALSE(ship::IsActionCost("at a station"));
  CHECK(ship::IsActionCost("1 action + 2 work"));  // mixed costs still spend one
}

TEST_CASE("a locked control refuses without spending the click", "[ui][cooldown]") {
  ship::Simulation sim = Fresh();
  ship::OpenTask(sim, "room", "o2", true);
  const std::string key = ship::ActionCooldownKey(sim, "progress:o2s0");
  sim.MutableState().actionCooldowns[key] = ship::Cooldown{"systems", 4};

  const double before = sim.ActionCount();
  CHECK_FALSE(ship::PressProgressButton(sim, "o2s0"));
  CHECK(sim.ActionCount() == before);
  CHECK(sim.Life().slots[0].state == "empty");

  // Once the clock has run out the same press works and starts the lock again.
  sim.MutableState().actionCooldowns.clear();
  CHECK(ship::PressProgressButton(sim, "o2s0"));
  CHECK(sim.ActionCount() == before + 1);
  CHECK(sim.FindOngoing("o2s0") != nullptr);
  REQUIRE(sim.State().actionCooldowns.count(key) == 1);
  CHECK(sim.State().actionCooldowns.at(key).remaining ==
        sim.GameRules().cooldowns.at("systems").actions);
}

// ---------------------------------------------------------------------------
//  The windows
// ---------------------------------------------------------------------------
TEST_CASE("the engine window is the reference's, line for line", "[ui][engine]") {
  ship::Simulation sim = Fresh();
  const ship::EngineScreenModel engine = ship::BuildEngineScreen(sim);
  CHECK(engine.screen.head.kind == "ROOM");
  CHECK(engine.screen.head.title == "Engine Room");
  CHECK(engine.screen.head.sub ==
        "Propulsion and power - the drive wears with every action spent "
        "aboard.");
  CHECK(engine.word == "NOMINAL");
  CHECK(engine.tone == "#7f9161");
  CHECK(engine.meterWidth == 420.0f);

  REQUIRE(engine.screen.left.size() == 3);
  CHECK(engine.screen.left[0].id == "eng-cond");
  CHECK(engine.screen.left[1].id == "eng-wear");
  CHECK(engine.screen.left[2].id == "eng-drive");
  CHECK(engine.screen.left[2].placement == "np-last");
  CHECK(Line(engine.screen.left[0], "Threshold")->value == "50% - warning line");
  CHECK(Line(engine.screen.left[1], "Rate")->value == "1% per 4 actions");
  CHECK(Line(engine.screen.left[1], "Next step")->value == "4 actions");
  CHECK(Line(engine.screen.left[1], "Full cycle")->value == "400 actions");

  CHECK(engine.screen.under[0].id == "eng-maint");
  CHECK(engine.screen.under[0].lines[0].kind == ship::PanelLine::Kind::Dim);
  CHECK(engine.screen.under[0].lines[0].value ==
        "Engine is at full condition. Nothing to maintain.");

  CHECK(engine.screen.right[0].id == "eng-crew");
  CHECK(engine.screen.right[0].lines[0].value == "Jax Moreno");
  CHECK(Line(engine.screen.right[0], "Acts at")->value == "30% condition");

  CHECK(engine.screen.bandNote ==
        "The drive is at full condition. Maintenance cannot be run on a perfect "
        "engine.");
  REQUIRE(engine.screen.progress.size() == 1);
  CHECK(engine.screen.progress[0].taskId == "maint");
  CHECK(engine.screen.progress[0].text == "Run Maintenance");
  CHECK(engine.screen.progress[0].state == ship::ButtonState::Off);

  // A worn drive changes the word, the tone, the meter and the note.
  sim.MutableState().engineCondition = 40.0;
  const ship::EngineScreenModel worn = ship::BuildEngineScreen(sim);
  CHECK(worn.word == "DEGRADED");
  CHECK(worn.tone == "#c08a3e");
  CHECK(worn.meterWidth == 168.0f);
  CHECK(worn.screen.bandNote ==
        "Maintenance will cost 6 actions spent anywhere aboard, and restores the "
        "drive to 100%.");
  CHECK(Line(worn.screen.under[0], "Cost now")->value == "6 actions");
  CHECK(worn.screen.progress[0].state == ship::ButtonState::Idle);
  CHECK(ship::EngineConditionWord(74) == "SERVICEABLE");
  CHECK(ship::EngineConditionWord(30) == "DEGRADED");
  CHECK(ship::EngineConditionWord(24) == "CRITICAL");
  CHECK(ship::EngineConditionWord(75) == "NOMINAL");
}

TEST_CASE("the filter room reads the rack, the bank and the air", "[ui][o2]") {
  ship::Simulation sim = Fresh();
  const ship::O2ScreenModel o2 = ship::BuildO2Screen(sim);
  CHECK(o2.screen.head.title == "O2 Filter Room");
  CHECK(o2.screen.head.sub ==
        "Air is the only thing aboard that nobody can do without.");
  CHECK(o2.air.word == "OPTIMAL");
  CHECK(o2.air.running == false);
  CHECK(o2.air.sub == "20 of 20 left");
  CHECK(o2.air.dust == 0);
  CHECK(o2.slots.size() == 3);
  CHECK(o2.slots[0].plate == "EMPTY");
  CHECK(o2.slots[0].tone == "#b05a42");
  CHECK(o2.slots[0].live);
  CHECK(o2.screen.stage.controls[0].button.text == "Install Canister");

  CHECK(Line(o2.screen.left[0], "Cabin air")->value == "20 of 20");
  CHECK(o2.screen.left[1].id == "o2-life");
  CHECK(Line(o2.screen.right[0], "Rack holds")->value == "10");
  CHECK(o2.screen.bandNote ==
        "Seat a canister in all three slots before the bank can be started.");
  CHECK(o2.screen.progress[0].taskId == "o2-run");
  CHECK(o2.screen.progress[0].text == "Turn on Filter");
  CHECK(o2.screen.progress[0].state == ship::ButtonState::Off);

  // Seat one: the slot is under way, and its button stops offering itself.
  REQUIRE(sim.StartCanisterInstall(0));
  const ship::O2ScreenModel seating = ship::BuildO2Screen(sim);
  // Running: the bar carries the task's own label, never a number.
  CHECK(seating.screen.stage.controls[0].button.text == "Seating - Slot 1");
  CHECK(seating.screen.stage.controls[0].button.state == ship::ButtonState::Running);
  CHECK(seating.screen.stage.controls[1].button.state == ship::ButtonState::Idle);

  // Seat all three and run the bank.
  for (int slot = 0; slot < 3; ++slot) {
    if (slot != 0) { sim.StartCanisterInstall(slot); }
    while (sim.FindOngoing("o2s" + std::to_string(slot)) != nullptr) {
      sim.PerformAction();
    }
  }
  const ship::O2ScreenModel ready = ship::BuildO2Screen(sim);
  CHECK(ready.ready);
  CHECK(ready.screen.bandNote ==
        "All three slots are seated. Starting the bank costs one action and "
        "advances every running job by ten.");
  CHECK(ready.screen.progress[0].state == ship::ButtonState::Idle);

  REQUIRE(ship::PressProgressButton(sim, "o2-run"));
  const ship::O2ScreenModel running = ship::BuildO2Screen(sim);
  CHECK(running.running);
  CHECK(running.air.running);
  CHECK(running.air.streaks == 4);
  CHECK(running.slots[0].plate == "ACTIVE");
  CHECK(running.slots[0].barPercent == 100.0);
  // The switch has just been thrown, so its lock is running down.
  CHECK(running.screen.progress[0].state == ship::ButtonState::Cooldown);
  CHECK(running.screen.progress[0].text == "Cooldown: 10 actions");
}

TEST_CASE("the galley reads the rack and the four clocks", "[ui][mess]") {
  ship::Simulation sim = Fresh();
  const ship::MessScreenModel mess = ship::BuildMessScreen(sim);
  CHECK(mess.screen.head.title == "Mess Hall");
  CHECK(mess.screen.head.sub ==
        "Hunger does not knock - it is on a clock, same as everything else "
        "aboard.");
  CHECK(mess.rackPercent == 75.0);   // 18 of 24
  CHECK(mess.rackTone == "#7f9161");
  CHECK(mess.rackReadout == "18 of 24 rations on the rack");
  CHECK(mess.plates.size() == 4);
  CHECK(mess.plates[0].name == "Rell Aventine");
  CHECK(mess.plates[0].sub == "25 actions to next meal");
  CHECK(Line(mess.screen.left[0], "Capacity")->value == "24 rations, hard ceiling");
  CHECK(Line(mess.screen.under[1], "Hungry now")->value == "0 of 4");
  CHECK(mess.screen.bandNote ==
        "Cooking fills the rack over time at the acting cook's rate, and a "
        "hungry crew member may still empty a ration out from under it while it "
        "runs.");
  CHECK(mess.screen.progress[0].taskId == "cook");
  CHECK(mess.screen.progress[0].text == "Cook a Batch");

  // A full rack refuses to cook, and says so twice.
  sim.MutableMess().rack = sim.MessCapacity();
  const ship::MessScreenModel full = ship::BuildMessScreen(sim);
  CHECK(full.full);
  CHECK(full.screen.bandNote ==
        "The rack is full. Cooking is refused until something is eaten.");
  CHECK(full.screen.progress[0].state == ship::ButtonState::Off);
}

// ---------------------------------------------------------------------------
//  Routing
// ---------------------------------------------------------------------------
TEST_CASE("the router sends the three machine rooms to their own windows",
          "[ui][router]") {
  using ship::ScreenKind;
  CHECK(ship::ScreenKindFor(TabOf("room", "engine")) == ScreenKind::Engine);
  CHECK(ship::ScreenKindFor(TabOf("room", "o2")) == ScreenKind::O2);
  CHECK(ship::ScreenKindFor(TabOf("room", "mess")) == ScreenKind::Mess);
  // Phase 6 gave the cargo hold and the science room their own windows.
  CHECK(ship::ScreenKindFor(TabOf("room", "cargo")) == ScreenKind::Cargo);
  CHECK(ship::ScreenKindFor(TabOf("room", "science")) == ScreenKind::Science);
  CHECK(ship::ScreenKindFor(TabOf("room", "cabin")) == ScreenKind::Described);
  // Phase 5 gave a crew tab its own read-only record.
  CHECK(ship::ScreenKindFor(TabOf("crew", "jax")) == ScreenKind::CrewRecord);

  ship::Simulation sim = Fresh();
  const ship::TaskScreenModel screen =
      ship::BuildTabScreen(sim, TabOf("room", "mess"));
  CHECK(screen.head.title == "Mess Hall");
  CHECK(screen.hasStage);
  CHECK(screen.stage.id == "messmach");

  // A described window keeps the house frame: Status on the left, dead actions
  // in the band with the notice that they are not wired yet.
  const ship::TaskScreenModel system =
      ship::BuildTabScreen(sim, TabOf("system", "hull"));
  CHECK(system.panels.size() > 0);
  CHECK(system.head.kind == "SYSTEM");
  CHECK(system.panels[0].title == "Status");

  const ship::TaskScreenModel info =
      ship::BuildTabScreen(sim, TabOf("info", "life"));
  CHECK(info.head.kind == "READOUT");
  CHECK(info.panels.size() > 0);
}

// ---------------------------------------------------------------------------
//  The nine vignettes
// ---------------------------------------------------------------------------
TEST_CASE("nine room archetypes, with their authored words", "[ui][vignette]") {
  CHECK(ship::SceneGlyphs().size() == 9);
  ship::Simulation sim = Fresh();

  const ship::Vignette bridge = ship::BuildVignette(sim, "bridge");
  CHECK(bridge.glyph == "network");
  CHECK(bridge.code == "COMMAND / 20");
  CHECK(bridge.label == "Flight and watch control");
  CHECK(bridge.description ==
        "Navigation, crew status and the ship's working board.");
  CHECK(bridge.accent == "#dfb85e");
  CHECK_FALSE(bridge.squareCore);

  const ship::Vignette engine = ship::BuildVignette(sim, "engine");
  CHECK(engine.glyph == "reactor");
  CHECK(engine.accent == "#d18a4c");
  CHECK(engine.squareCore);   // the reactor's core is a rotated square

  const ship::Vignette o2 = ship::BuildVignette(sim, "o2");
  CHECK(o2.glyph == "filter");
  CHECK(o2.code == "LIFE SUPPORT / 02");

  CHECK(ship::BuildVignette(sim, "mess").glyph == "habitat");
  CHECK(ship::BuildVignette(sim, "r1").glyph == "medical");
  CHECK(ship::BuildVignette(sim, "cargo").glyph == "cargo");
  CHECK(ship::BuildVignette(sim, "r12").glyph == "orbit");
  CHECK(ship::BuildVignette(sim, "simulacron").glyph == "core");
  CHECK(ship::BuildVignette(sim, "r8").glyph == "garden");

  // Geometry from .room-hero.
  CHECK(bridge.hero.h == 132.0f);
  CHECK(bridge.scope.x == 17.0f);
  CHECK(bridge.scope.w == 100.0f);
  CHECK(bridge.orbitTwo.w == 52.0f);
  CHECK(bridge.core.x == 43.0f);
  CHECK(bridge.meter.size() == 8);
  CHECK(bridge.statusValue == "ONLINE");

  // A room with no authored scene still gets the house default.
  const ship::Vignette unknown = ship::BuildVignette(sim, "no-such-room");
  CHECK(unknown.glyph == "network");
  CHECK(unknown.accent == "#8c6f4e");
}

// ---------------------------------------------------------------------------
//  The opening loop, through the buttons
// ---------------------------------------------------------------------------
TEST_CASE("the opening loop runs on the controls themselves", "[ui][loop]") {
  ship::Simulation sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "room", "o2", true));

  // Seat all three, each press starting a lock of its own.
  for (int slot = 0; slot < 3; ++slot) {
    const std::string id = "o2s" + std::to_string(slot);
    REQUIRE(ship::PressProgressButton(sim, id));
    CHECK(sim.FindOngoing(id) != nullptr);
    while (sim.FindOngoing(id) != nullptr) { sim.PerformAction(); }
    CHECK(sim.Life().slots[static_cast<std::size_t>(slot)].state == "installed");
    // Acknowledge the finished seating task, as the window's button would.
    if (sim.FindOngoing(id) != nullptr) { CHECK(sim.Acknowledge(id)); }
  }
  CHECK(sim.BankReady());

  // Start the bank: one action, ten back, and the air is held at the top step.
  const double before = sim.ActionCount();
  REQUIRE(ship::PressProgressButton(sim, "o2-run"));
  CHECK(sim.ActionCount() == before + 1);
  CHECK(sim.BankRunning());
  CHECK(sim.AirStageIndex() == 4);
  CHECK(sim.State().filterLife == sim.GameRules().life.lifeMax);

  // Wear the drive, then run maintenance through its button.
  sim.MutableState().engineCondition = 55.0;
  REQUIRE(ship::OpenTask(sim, "room", "engine", true));
  REQUIRE(ship::PressProgressButton(sim, "maint"));
  const ship::Task* maint = sim.FindOngoing("maint");
  REQUIRE(maint != nullptr);
  CHECK(maint->label == "Maintenance - the commander");
  CHECK(maint->total == sim.MaintCostActions());
  while (sim.FindOngoing("maint") != nullptr) { sim.PerformAction(); }
  CHECK(sim.State().engineCondition == 100.0);
  // The work is consumed on completion, and a perfect drive refuses a new
  // spanner. The systems cooldown is still shown until other actions expire it.
  CHECK(ship::BuildProgressButton(sim, "maint", "Run Maintenance", false).state ==
        ship::ButtonState::Cooldown);
  CHECK_FALSE(ship::PressProgressButton(sim, "maint"));

  // Empty the rack and cook it back up.
  sim.MutableMess().rack = 4.0;
  REQUIRE(ship::OpenTask(sim, "room", "mess", true));
  REQUIRE(ship::PressProgressButton(sim, "cook"));
  const ship::Task* cook = sim.FindOngoing("cook");
  REQUIRE(cook != nullptr);
  CHECK(cook->label == "Cooking - the commander");
  CHECK(cook->rate == 1.0);
  CHECK(cook->total == 20.0);   // 24 - 4 rations at one per action
  while (sim.FindOngoing("cook") != nullptr) { sim.PerformAction(); }
  // Twenty rations cooked, four eaten while the pot was on: the rack lands at
  // twenty, not at its ceiling - the five clocks the window draws, working.
  CHECK(sim.MessRack() == 20.0);
}

TEST_CASE("maintenance clicks retain the rendered systems cooldown tier", "[ui][cooldown]") {
  auto sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "room", "engine", true));
  sim.MutableState().engineCondition = 50;
  REQUIRE(ship::PressProgressButton(sim, "maint"));
  const auto key = ship::ActionCooldownKey(sim, "progress:maint");
  CHECK(ship::CooldownRemaining(sim, key) == 10);
  CHECK(sim.State().actionCooldowns.at(key).category == "systems");
}


TEST_CASE("filter illustration tracks clean air, slot charge and degradation", "[ui][machine][audit]") {
  auto sim = Fresh();
  const auto find = [](const ship::TaskStage& stage, const std::string& cls) -> ship::MachinePart {
    for (const auto& part : stage.parts) { if (part.cls == cls) return part; }
    FAIL("Missing filter illustration part: " + cls);
    return {};
  };
  auto stage = ship::BuildFilterMachine(sim);
  CHECK(find(stage, "o2-airfill").box.w == std::round(544 * sim.AirPercent() / 100));
  CHECK(find(stage, "o2-airfill").tone == sim.AirSpec().tone);
  CHECK(find(stage, "o2-airword").text == sim.AirSpec().word);
  CHECK(find(stage, "o2-air").tone == (sim.AirStageIndex() == 4 ? "#f1f6ea" : "#efe7d8"));
  REQUIRE(sim.StartCanisterInstall(0));
  REQUIRE(sim.StartCanisterInstall(1));
  REQUIRE(sim.StartCanisterInstall(2));
  sim.PerformAction("seating"); sim.PerformAction("seating");
  REQUIRE(sim.BankReady());
  REQUIRE(sim.StartFilterBank());
  stage = ship::BuildFilterMachine(sim);
  CHECK(CountClass(stage, "o2-can") == 3);
  CHECK(CountClass(stage, "o2-streak") == 4);
  CHECK(CountClass(stage, "o2-dust") == 0);
  CHECK(find(stage, "o2-state").text == "ACTIVE");
  CHECK(find(stage, "o2-airfill").tone == "#7f9161");
  CHECK(find(stage, "o2-airfill").box.w == 544);
  const auto life = sim.State().filterLife;
  // Repaint is free: no life, air or action-clock advancement.
  for (int i = 0; i < 20; ++i) { ship::BuildFilterMachine(sim); }
  CHECK(sim.State().filterLife == life);
  sim.PerformAction("advance filter");
  stage = ship::BuildFilterMachine(sim);
  const auto pct = std::round(sim.State().filterLife / sim.GameRules().life.lifeMax * 100);
  CHECK(find(stage, "o2-fill").box.w == std::round(120 * pct / 100));
  sim.FindStat("energy")->value = 0;
  sim.MutableState().airHold = 0;
  stage = ship::BuildFilterMachine(sim);
  CHECK(find(stage, "o2-state").text == "NO POWER");
  CHECK(find(stage, "o2-airfill").tone == sim.AirSpec().tone);
  CHECK(CountClass(stage, "o2-streak") == 0);
  CHECK(CountClass(stage, "o2-dust") == (4 - sim.AirStageIndex()) * 4);
}

TEST_CASE("task retirement is explicit and automatic workers need no acknowledgment", "[simulation][tasks][audit]") {
  auto sim = Fresh();
  REQUIRE(sim.AddOngoing("unknown", "Future readout", 1, "unregistered") != nullptr);
  REQUIRE(sim.AddOngoing("repair", "Automatic repair", 1, "maint") != nullptr);
  sim.MutableState().engineCondition = 40;
  const auto actions = sim.ActionCount();
  sim.ProAction(1);
  CHECK(sim.FindOngoing("repair") == nullptr);
  CHECK(sim.State().engineCondition == 100);
  CHECK(sim.State().wearTick == 0);
  REQUIRE(sim.FindOngoing("unknown") != nullptr);
  CHECK(sim.FindOngoing("unknown")->state == "done");
  CHECK(sim.ActionCount() == actions);
}

TEST_CASE("training starts autonomous maintenance earlier with less work and no commander click", "[simulation][crew][audit]") {
  for (const bool trained : {false, true}) {
    auto sim = Fresh();
    auto* engineer = sim.FindCrew("jax");
    REQUIRE(engineer != nullptr);
    REQUIRE(engineer->job == "maintenance");
    if (trained) sim.TrainCrew(*engineer, 2, "aboard");
    const double threshold = trained ? 60 : 30;
    CHECK(engineer->jobAt == threshold);
    sim.MutableState().engineCondition = threshold;
    sim.PerformAction();
    REQUIRE(sim.ActionCount() == 1);
    const auto* task = sim.FindOngoing("maint-eng");
    REQUIRE(task != nullptr);
    const double work = trained ? 4 : 7;
    CHECK(task->total == work);
    CHECK(task->left == work); // the birth action must not charge new work
    for (int i = 0; i < static_cast<int>(work); ++i) sim.PerformAction();
    CHECK(sim.FindOngoing("maint-eng") == nullptr); // no acknowledgment click
    CHECK(sim.State().engineCondition == 100);
    CHECK(sim.State().wearTick == 0);
    CHECK(sim.ActionCount() == 1 + work);
    for (int i = 0; i < 4; ++i) sim.PerformAction();
    CHECK(sim.State().engineCondition == 99); // original four-action wear cycle
  }
}

TEST_CASE("scan work uses catalogue cost and HTA sensor bonus rather than travel distance", "[simulation][scan][audit]") {
  auto catalog = World();
  catalog.rules.scanCost = 9;
  for (const auto& target : {"halvan", "tolme"}) {
    auto sim = ship::Simulation::NewVoyage(catalog);
    sim.MutableState().navTarget = target;
    sim.MutableState().sim.sensor = 1;
    REQUIRE(sim.StartScan());
    REQUIRE(sim.FindOngoing("scan") != nullptr);
    CHECK(sim.ScanCost() == 9);
    CHECK(sim.FindOngoing("scan")->left == 7);
    CHECK(sim.State().sim.sensor == 0);
    CHECK(sim.ActionCount() == 1);
  }
  auto upgraded = Fresh();
  upgraded.MutableState().docked = true;
  upgraded.MutableMap().here = "tolme";
  upgraded.MutableState().shiplets = 1000;
  REQUIRE(upgraded.BuyUpgrade("scanner"));
  CHECK(upgraded.ScanCost() == 3);
  upgraded.MutableState().navTarget = "halvan";
  upgraded.MutableState().sim.sensor = 1;
  REQUIRE(upgraded.StartScan());
  REQUIRE(upgraded.FindOngoing("scan") != nullptr);
  CHECK(upgraded.FindOngoing("scan")->total == 1);
}

TEST_CASE("cosmetic log timestamps do not tick the voyage and survive saves", "[simulation][feed][audit]") {
  auto sim = Fresh();
  const auto actions = sim.ActionCount();
  const auto life = sim.State().airHold;
  const auto revision = sim.FeedRevision();
  sim.LogLine("Timestamp probe");
  REQUIRE(sim.State().feed.size() == 1);
  const auto stamp = sim.State().feed.back().stamp;
  REQUIRE(stamp.size() == 8);
  CHECK(stamp[2] == ':'); CHECK(stamp[5] == ':');
  for (const int i : {0, 1, 3, 4, 6, 7}) CHECK((stamp[i] >= '0' && stamp[i] <= '9'));
  CHECK(sim.ActionCount() == actions);
  CHECK(sim.State().airHold == life);
  CHECK(sim.FeedRevision() == revision + 1);
  auto loaded = Fresh();
  std::string error;
  REQUIRE(loaded.LoadJson(sim.SaveJson(), &error));
  CHECK(loaded.State().feed.back().stamp == stamp);
  CHECK(loaded.State().feed.back().text == "Timestamp probe");
}

TEST_CASE("HTA cooldown tiers remain distinct and missing rules fall back to five", "[ui][cooldown][audit]") {
  auto sim = Fresh();
  CHECK(ship::RuleActions(sim, "routine") == 5);
  CHECK(ship::RuleActions(sim, "systems") == 10);
  CHECK(ship::RuleActions(sim, "space") == 15);
  CHECK(ship::RuleActions(sim, "critical") == 20);
  CHECK(ship::RuleActions(sim, "unregistered") == 5);
}

TEST_CASE("the action click jointly clocks meals filters wear and existing work without an eating pause", "[simulation][crew][audit]") {
  for (const bool stocked : {false, true}) {
    auto sim = Fresh();
    sim.MutableState().engineCondition = 50;
    sim.MutableState().filterLife = 200;
    for (auto& slot : sim.MutableLife().slots) slot.state = "installed";
    for (auto& crew : sim.MutableCrew()) { crew.hungerTick = 0; crew.hunger = 60; }
    auto* rell = sim.FindCrew("rell");
    REQUIRE(rell != nullptr);
    rell->hungerTick = sim.GameRules().mess.every - 1;
    sim.MutableMess().rack = stocked ? 10 : 0;
    const auto life = sim.State().filterLife;
    for (int i = 0; i < 20; ++i) {
      REQUIRE(ship::OpenTask(sim, "crew", "rell", false));
      ship::TabBar(sim);
    }
    CHECK(sim.ActionCount() == 0);
    CHECK(sim.State().filterLife == life);
    CHECK(rell->hungerTick == sim.GameRules().mess.every - 1);
    CHECK(sim.State().wearTick == 0);
    REQUIRE(sim.StartMaintenanceSelf());
    REQUIRE(sim.FindOngoing("maint") != nullptr);
    const double work = sim.FindOngoing("maint")->left;
    CHECK(sim.ActionCount() == 1);
    CHECK(sim.State().wearTick == 1);
    CHECK(sim.State().filterLife == life - sim.GameRules().life.decay);
    CHECK(rell->hungerTick == 0);
    CHECK(rell->hunger == (stocked ? 20 : 75));
    CHECK(sim.MutableMess().rack == (stocked ? 9 : 0));
    // A meal due while work already exists must NOT steal a work tick.
    rell->hungerTick = sim.GameRules().mess.every - 1;
    sim.PerformAction();
    REQUIRE(sim.FindOngoing("maint") != nullptr);
    CHECK(sim.FindOngoing("maint")->left == work - 1);
    CHECK(sim.ActionCount() == 2);
    CHECK(sim.State().crewTick == 2);
    CHECK(sim.State().wearTick == 2);
    CHECK(sim.State().filterLife == life - 2 * sim.GameRules().life.decay);
    CHECK(rell->hungerTick == 0);
    CHECK(rell->hunger == (stocked ? 0 : 90));
  }
}

TEST_CASE("source filter charge expiry keeps the separate air hold step", "[simulation][audit]") {
  auto sim = Fresh();
  sim.MutableState().filterLife = sim.GameRules().life.lifeMax;
  for (auto& slot : sim.MutableLife().slots) slot.state = "installed";
  const int ticks = static_cast<int>(std::ceil(sim.GameRules().life.lifeMax / sim.GameRules().life.decay));
  for (int i = 1; i < ticks; ++i) sim.PerformAction();
  CHECK(sim.State().filterLife == sim.GameRules().life.decay);
  sim.PerformAction();
  CHECK(sim.ActionCount() == ticks);
  CHECK(sim.State().filterLife == 0);
  for (const auto& slot : sim.Life().slots) CHECK(slot.state == "empty");
  CHECK(sim.State().airHold == sim.GameRules().air.holdMax);
  sim.PerformAction();
  CHECK(sim.State().airHold == sim.GameRules().air.holdMax - 1);
}

TEST_CASE("starvation damage follows the source eight-action crew clock not a render timer", "[simulation][crew][audit]") {
  auto sim = Fresh();
  auto* hand = sim.FindCrew("rell");
  REQUIRE(hand != nullptr);
  hand->hunger = sim.GameRules().mess.starveAt;
  hand->hungerTick = 0;
  hand->health = 80;
  sim.MutableState().crewTick = sim.GameRules().mess.starveEvery - 2;
  sim.PerformAction();
  CHECK(hand->health == 80);
  sim.PerformAction();
  CHECK(hand->health == 79);
}

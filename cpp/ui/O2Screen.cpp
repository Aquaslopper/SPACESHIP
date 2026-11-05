// ============================================================================
//  O2Screen.cpp — the rack, the bank, and the air.
//  ---------------------------------------------------------------------------
//  Every number on this screen is derived from LIFE.slots and STATE.filterLife;
//  nothing is stored twice. The three install buttons are the only live parts
//  of the illustration and they are appended LAST, so scenery can never swallow
//  a click - the rule the reference repeats four times in its comments.
// ============================================================================
#include "ui/O2Screen.h"

#include <cmath>

#include "core/JsNumber.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

MachinePart Part(const std::string& cls, float x, float y, float w, float h,
                 const std::string& text = std::string()) {
  return MachinePart{cls, Area{x, y, w, h}, text};
}

}  // namespace

TaskStage BuildFilterMachine(const Simulation& sim) {
  (void)sim;  // the scenery is authored, not derived; slots are live elsewhere
  TaskStage stage;
  stage.id = "o2mach";
  stage.width = 600.0f;  // STAGE.o2
  stage.height = 360.0f;
  std::vector<MachinePart>& parts = stage.parts;

  // --- intake duct across the top ---------------------------------------
  parts.push_back(Part("o2-duct", 20, 14, 560, 30));
  parts.push_back(Part("o2-mini", 26, 19, 60, 10, "INTAKE"));
  for (int k = 0; k < 7; ++k) {
    parts.push_back(Part("o2-louvre", 100.0f + k * 66.0f, 20, 6, 18));
  }

  // --- the cabin air, rebuilt by the renderer ---------------------------
  parts.push_back(Part("o2-air", 20, 52, 560, 66, "air"));

  // --- three slot housings ----------------------------------------------
  for (int k = 0; k < 3; ++k) {
    const float sx = 20.0f + k * 190.0f;
    parts.push_back(Part("o2-slot", sx, 126, 180, 152));
    parts.push_back(Part("o2-mini", sx + 9.0f, 132, 80, 10,
                         "SLOT " + std::to_string(k + 1)));
    parts.push_back(Part("o2-well", sx + 30.0f, 152, 120, 84));
    parts.push_back(Part("o2-can", sx + 46.0f, 162, 88, 64));
    parts.push_back(Part("o2-neck", sx + 78.0f, 154, 24, 8));
    parts.push_back(Part("o2-state", sx + 8.0f, 242, 164, 14, "EMPTY"));
    parts.push_back(Part("o2-track", sx + 30.0f, 260, 120, 8));
    parts.push_back(Part("o2-fill", sx + 30.0f, 260, 0, 8));

    // The live control - deliberately not scenery.
    LiveControl control;
    control.id = "o2-btnwrap" + std::to_string(k);
    control.box = Area{sx + 5.0f, 286.0f, 170.0f, 26.0f};
    stage.controls.push_back(control);
  }

  // --- outlet manifold ---------------------------------------------------
  parts.push_back(Part("o2-duct", 20, 324, 560, 26));
  parts.push_back(Part("o2-mini", 26, 331, 70, 10, "TO DECKS"));
  for (int k = 0; k < 8; ++k) {
    parts.push_back(Part("o2-flow", 120.0f + k * 56.0f, 332, 10, 10));
  }
  return stage;
}

O2ScreenModel BuildO2Screen(const Simulation& sim) {
  O2ScreenModel model;
  const GameState& state = sim.State();
  model.running = sim.BankRunning();
  model.ready = sim.BankReady();
  model.filterLife = state.filterLife;
  model.airPercent = sim.AirPercent();

  const AirStage& airSpec = sim.AirSpec();
  AirChamber& air = model.air;
  air.word = airSpec.word;
  air.tone = airSpec.tone;
  air.running = model.running;
  air.fillPercent = sim.AirPercent();
  air.notches = 3;
  air.sub = model.running ? Num(state.filterLife) + " filtered"
                          : Num(state.airHold) + " of " +
                                Num(sim.GameRules().air.holdMax) + " left";
  if (model.running) {
    air.streaks = 4;
  } else {
    air.dust = (4 - sim.AirStageIndex()) * 4;
  }

  const double pct = std::floor((state.filterLife / sim.GameRules().life.lifeMax) * 100.0);

  TaskScreenModel& screen = model.screen;
  screen.head = TaskHead{"ROOM", "O2 Filter Room",
                         "Air is the only thing aboard that nobody can do "
                         "without."};

  // --- left column -------------------------------------------------------
  NavPanel airPanel;
  airPanel.id = "o2-air-p";
  airPanel.title = "Air Quality";
  airPanel.Name(airSpec.word, airSpec.tone);
  airPanel.Sub(model.running ? "Scrubbed and circulating"
                             : "Unfiltered - the bank is not running");
  airPanel.Bar(sim.AirPercent(), airSpec.tone);
  airPanel.Field("Cabin air", model.running ? "Replenished"
                                            : Num(state.airHold) + " of " +
                                                  Num(sim.GameRules().air.holdMax));
  airPanel.Field("Declines", "one step per " + Num(sim.GameRules().air.step) +
                                 " actions");
  airPanel.Field("Crew", airSpec.word);
  if (sim.AirStageIndex() < 3) {
    airPanel.Note("Bad air harms the crew and can eventually kill them. It "
                  "also slows their work and delays their response.");
  }
  screen.left.push_back(airPanel);

  NavPanel life;
  life.id = "o2-life";
  life.title = "Filter Lifespan";
  life.Name(Num(state.filterLife));
  life.Sub("of " + Num(sim.GameRules().life.lifeMax));
  life.Sub("actions of clean air");
  life.Bar(pct, airSpec.tone);
  life.Field("Burn rate", Num(sim.GameRules().life.decay) + " per action");
  life.Field("Full charge",
             Num(sim.GameRules().life.lifeMax / sim.GameRules().life.decay) + " actions");
  screen.left.push_back(life);

  NavPanel slotPanel;
  slotPanel.id = "o2-slotsp";
  slotPanel.title = "Slots";
  slotPanel.placement = "np-last";
  for (const CanisterSlot& slot : sim.Life().slots) {
    slotPanel.Field(slot.name, slot.state == "empty"    ? "bare"
                               : slot.state == "installed" ? "seated"
                                                           : "running");
  }
  screen.left.push_back(slotPanel);

  // --- under the machine -------------------------------------------------
  NavPanel status;
  status.id = "o2-status";
  status.title = "System Status";
  status.placement = "np-half";
  status.Field("Intake", "Open");
  status.Field("Manifold", model.running ? "Circulating" : "Static");
  status.Field("Scrubber", model.running ? "Running" : "Idle");
  int seated = 0;
  for (const CanisterSlot& slot : sim.Life().slots) {
    if (slot.state == "installed" || slot.state == "active") { ++seated; }
  }
  status.Field("Seated", Num(seated) + " of " +
                             Num(static_cast<double>(sim.Life().slots.size())));
  screen.under.push_back(status);

  NavPanel burst;
  burst.id = "o2-burst";
  burst.title = "Pro-Action Burst";
  burst.placement = "np-half-last";
  burst.Note("Starting the bank costs one ordinary action and hands ten back: "
             "every job already running jumps forward by ten actions. The "
             "counter does not move and nothing wears. It is the only thing "
             "aboard that gives time instead of taking it.");
  screen.under.push_back(burst);

  // --- right column ------------------------------------------------------
  NavPanel rack;
  rack.id = "o2-rack";
  rack.title = "Canister Inventory";
  rack.Name(Num(sim.Life().canisters));
  rack.Sub("canisters in the rack");
  rack.Field("Rack holds", Num(sim.Life().capacity));
  rack.Field("Per install", "1 canister");
  rack.Field("Full bank", Num(static_cast<double>(sim.Life().slots.size())) +
                              " canisters");
  rack.Note("A canister leaves the rack the moment an install is committed. "
            "Stations sell them; the Recyclers can make one from salvage.");
  screen.right.push_back(rack);

  const CrewMember* hand = sim.FilterHand();
  NavPanel crew;
  crew.id = "o2-crew";
  crew.title = "Life Support Crew";
  crew.placement = "np-last";
  if (hand != nullptr) {
    crew.Name(hand->name);
    crew.Sub(hand->role + " - filter job");
    crew.Field("Skill", hand->skill);
    crew.Field("Acts at", JobThreshold(sim, *hand));
    crew.Field("Activity", hand->activity);
    crew.Field("Seating", Num(sim.CrewCost(2.0)) + " actions per slot");
    const std::string stageWord = JobThreshold(sim, *hand);
    crew.Note("He holds the filter job and works the bank unasked - but not "
              "until the air is already " + stageWord +
              ", by which time the crew have been labouring for a while. He is "
              "slowed by the same bad air he is there to fix.");
  } else {
    crew.Dim("Nobody holds the filter job. The commander seats every canister "
             "personally.");
  }
  screen.right.push_back(crew);

  // --- the stage, with its three live controls ---------------------------
  screen.stage = BuildFilterMachine(sim);

  // --- slots, live -------------------------------------------------------
  for (std::size_t i = 0; i < sim.Life().slots.size(); ++i) {
    const CanisterSlot& slot = sim.Life().slots[i];
    SlotView view;
    view.id = slot.id;
    view.name = slot.name;
    view.state = slot.state;
    view.canisterVisible = slot.state != "empty";
    view.barPercent = slot.state == "active" ? pct : 0.0;
    if (slot.state == "empty") {
      view.plate = "EMPTY";
      view.tone = "#b05a42";
    } else if (slot.state == "installed") {
      view.plate = "INSTALLED";
      view.tone = "#8c6f4e";
    } else {
      view.plate = model.running ? "ACTIVE" : "NO POWER";
      view.tone = "#7f9161";
    }

    const std::string taskId = "o2s" + std::to_string(i);
    std::string label;
    bool live = false;
    if (sim.FindOngoing(taskId) != nullptr) {
      label = "Install Canister";
    } else if (slot.state == "active") {
      label = model.running ? "Running" : "No power";
    } else if (slot.state == "installed") {
      label = "Seated";
    } else if (sim.Life().canisters <= 0.0) {
      label = "No canisters";
    } else {
      label = "Install Canister";
      live = true;
    }
    view.live = live;
    ProgressButton button = BuildProgressButton(sim, taskId, label, live);
    if (i < screen.stage.controls.size()) {
      screen.stage.controls[i].button = button;
    }
    model.slots.push_back(view);
  }

  // --- the commitments band ---------------------------------------------
  screen.bandNote =
      model.running
          ? "The bank is running. " + Num(state.filterLife) +
                " actions of clean air left."
          : (sim.StatValue("energy") <= 0.0
                 ? "No power. Recharge energy to run the filter bank."
                 : (model.ready
                        ? "All three slots are seated. Starting the bank costs "
                          "one action and advances every running job by ten."
                        : "Seat a canister in all three slots before the bank "
                          "can be started."));

  const bool canRun =
      model.ready && !model.running && sim.StatValue("energy") > 0.0;
  screen.progress.push_back(BuildProgressButton(
      sim, "o2-run", model.running ? "Filters running" : "Turn on Filter", canRun));

  NavPanel actions;
  actions.id = "o2-actions";
  actions.title = "Filter Actions";
  actions.placement = "np-full";
  screen.band.push_back(actions);

  screen.hasStage = true;
  return model;
}

}  // namespace ship

// ============================================================================
//  MessScreen.cpp — the counter, the rack and the four clocks.
// ============================================================================
#include "ui/MessScreen.h"

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

TaskStage BuildMessMachine(const Simulation& sim) {
  TaskStage stage;
  stage.id = "messmach";
  stage.width = 600.0f;  // STAGE.mess
  stage.height = 360.0f;
  std::vector<MachinePart>& parts = stage.parts;

  // --- the serving counter ----------------------------------------------
  parts.push_back(Part("mess-counter", 20, 14, 560, 40));
  parts.push_back(Part("mess-mini", 26, 19, 70, 10, "GALLEY"));

  // --- the rack gauge - the one thing that actually moves ----------------
  parts.push_back(Part("mess-rackbox", 20, 70, 560, 90));
  parts.push_back(Part("mess-mini", 28, 76, 110, 10, "RATION RACK"));
  parts.push_back(Part("mess-track", 40, 110, 520, 22));
  MachinePart fill = Part("mess-fill", 40, 110, 0, 22, "mess-bar");
  const double pct = sim.MessCapacity() > 0.0
                         ? std::round((sim.MessRack() / sim.MessCapacity()) * 100.0)
                         : 0.0;
  fill.box.w = static_cast<float>(std::round(520.0 * (pct / 100.0)));
  parts.push_back(fill);
  parts.push_back(Part("mess-readout", 40, 136, 520, 16, "mess-readout"));

  // --- four plates, one per crew member, each its own clock --------------
  parts.push_back(Part("mess-mini", 28, 182, 140, 10, "WHO EATS NEXT"));
  const std::size_t hands = sim.Crew().size();
  for (std::size_t k = 0; k < hands; ++k) {
    parts.push_back(
        Part("mess-plate", 20.0f + static_cast<float>(k) * 141.0f, 200, 130, 70));
    parts.push_back(Part("mess-plate-name", 26.0f + static_cast<float>(k) * 141.0f,
                         206, 118, 14,
                         "mess-pname" + std::to_string(k)));
    parts.push_back(Part("mess-track", 26.0f + static_cast<float>(k) * 141.0f, 228,
                         118, 12));
    parts.push_back(Part("mess-fill mess-fill-small",
                         26.0f + static_cast<float>(k) * 141.0f, 228, 0, 12,
                         "mess-ptick" + std::to_string(k)));
    parts.push_back(Part("mess-plate-sub", 26.0f + static_cast<float>(k) * 141.0f,
                         246, 118, 14,
                         "mess-psub" + std::to_string(k)));
  }
  return stage;
}

MessScreenModel BuildMessScreen(const Simulation& sim) {
  MessScreenModel model;
  const double rack = sim.MessRack();
  const double capacity = sim.MessCapacity();
  model.rackPercent = capacity > 0.0 ? std::round((rack / capacity) * 100.0) : 0.0;
  model.rackTone = model.rackPercent >= 60.0 ? "#7f9161"
                   : model.rackPercent >= 25.0 ? "#c08a3e"
                                               : "#b05a42";
  model.rackReadout = Num(rack) + " of " + Num(capacity) + " rations on the rack";
  model.full = rack >= capacity;

  const CrewMember* cook = sim.Cook();

  // --- the four plates ---------------------------------------------------
  for (const CrewMember& crew : sim.Crew()) {
    PlateView plate;
    plate.id = crew.id;
    plate.name = crew.name;
    plate.tickPercent =
        std::round((crew.hungerTick / sim.GameRules().mess.every) * 100.0);
    plate.sub = Num(sim.GameRules().mess.every - crew.hungerTick) +
                " actions to next meal";
    plate.tone = crew.hunger >= 70.0 ? "#b05a42" : "#7f9161";
    model.plates.push_back(plate);
  }

  TaskScreenModel& screen = model.screen;
  screen.head = TaskHead{"ROOM", "Mess Hall",
                         "Hunger does not knock - it is on a clock, same as "
                         "everything else aboard."};

  // --- left column -------------------------------------------------------
  NavPanel rackPanel;
  rackPanel.id = "mess-rack-p";
  rackPanel.title = "Rack";
  rackPanel.Name(Num(rack), model.rackTone);
  rackPanel.Sub("of " + Num(capacity));
  rackPanel.Sub("rations on hand");
  rackPanel.Bar(model.rackPercent, model.rackTone);
  rackPanel.Field("Capacity", Num(capacity) + " rations, hard ceiling");
  screen.left.push_back(rackPanel);

  NavPanel cycle;
  cycle.id = "mess-cycle";
  cycle.title = "Hunger Cycle";
  cycle.Field("Every crew member",
              Num(sim.GameRules().mess.every) + " of their own actions");
  cycle.Field("On a successful meal", "hunger falls by 40");
  cycle.Field("On an empty rack", "hunger rises by 15 instead");
  screen.left.push_back(cycle);

  NavPanel starve;
  starve.id = "mess-starve";
  starve.title = "Starvation";
  starve.placement = "np-last";
  starve.Field("Risk begins at", Num(sim.GameRules().mess.starveAt) + "% hunger");
  starve.Field("Cost",
               "1 health every " + Num(sim.GameRules().mess.starveEvery) + " actions");
  starve.Note("Slow on purpose - the same law bad air already follows. Nobody "
              "starves on the spot.");
  screen.left.push_back(starve);

  // --- under the machine -------------------------------------------------
  NavPanel cooking;
  cooking.id = "mess-cook-p";
  cooking.title = "Cooking";
  cooking.placement = "np-half";
  if (model.full) {
    cooking.Dim("Rack is full. Nothing to cook for.");
  } else {
    cooking.Field("Cost now",
                  Num(std::ceil((capacity - rack) / (cook != nullptr ? cook->jobAt : 1.0))) +
                      " actions");
    cooking.Field("Rate", Num(cook != nullptr ? cook->jobAt : 1.0) +
                              " ration(s) per action");
    cooking.Field("Restores", "rack to full");
  }
  screen.under.push_back(cooking);

  NavPanel appetite;
  appetite.id = "mess-appetite";
  appetite.title = "Crew Appetite";
  appetite.placement = "np-half-last";
  int hungry = 0;
  double hungerSum = 0.0;
  for (const CrewMember& crew : sim.Crew()) {
    if (crew.hunger > 70.0) { ++hungry; }
    hungerSum += crew.hunger;
  }
  appetite.Field("Hungry now",
                 Num(hungry) + " of " + Num(static_cast<double>(sim.Crew().size())));
  appetite.Field("Average hunger",
                 Num(std::round(hungerSum / static_cast<double>(sim.Crew().size()))) +
                     "%");
  screen.under.push_back(appetite);

  // --- right column ------------------------------------------------------
  NavPanel crewPanel;
  crewPanel.id = "mess-crew";
  crewPanel.title = "Galley Crew";
  crewPanel.placement = "np-last";
  if (cook != nullptr) {
    crewPanel.Name(cook->name);
    crewPanel.Sub(cook->role + " - kitchen job");
    crewPanel.Field("Skill", cook->skill);
    crewPanel.Field("Acts at", JobThreshold(sim, *cook));
    crewPanel.Field("Activity", cook->activity);
    crewPanel.Note("She holds the kitchen job and will start cooking unasked "
                   "once the rack falls to half - training her raises how fast "
                   "the rack refills.");
  } else {
    crewPanel.Dim("Nobody holds the kitchen job. The commander cooks "
                  "personally, or nobody does.");
  }
  screen.right.push_back(crewPanel);

  // --- the commitments band ---------------------------------------------
  screen.bandNote =
      sim.FindOngoing("project-food") != nullptr
          ? "Hydroponics has reserved space for produce processing."
          : (model.full
                 ? "The rack is full. Cooking is refused until something is "
                   "eaten."
                 : "Cooking fills the rack over time at the acting cook's "
                   "rate, and a hungry crew member may still empty a ration "
                   "out from under it while it runs.");
  screen.progress.push_back(BuildProgressButton(
      sim, "cook",
      sim.FindOngoing("cook") != nullptr ? "Cook in progress" : "Cook a Batch",
      !model.full && sim.FindOngoing("project-food") == nullptr));

  NavPanel actions;
  actions.id = "mess-actions";
  actions.title = "Mess Actions";
  actions.placement = "np-full";
  screen.band.push_back(actions);

  screen.stage = BuildMessMachine(sim);
  screen.hasStage = true;
  return model;
}

}  // namespace ship

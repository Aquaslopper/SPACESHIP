// ============================================================================
//  MessScreen.cpp — the counter, the rack and the four clocks.
// ============================================================================
#include "ui/MessScreen.h"

#include <algorithm>

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

  const auto addStockRow = [&](const std::string& cls, double stock,
                               double capacity, float y, float iconHeight) {
    const int slots = std::max(
        1, static_cast<int>(std::ceil(std::max(0.0, capacity))));
    const int count = std::clamp(
        static_cast<int>(std::floor(std::max(0.0, stock))), 0, slots);
    const float slotWidth = 520.0f / static_cast<float>(slots);
    const float iconWidth = std::max(4.0f, std::min(12.0f, slotWidth - 4.0f));
    for (int i = 0; i < count; ++i) {
      const float x = 40.0f + static_cast<float>(i) * slotWidth +
                      (slotWidth - iconWidth) / 2.0f;
      parts.push_back(Part(cls, x, y, iconWidth, iconHeight));
    }
  };

  // --- the serving counter ----------------------------------------------
  parts.push_back(Part("mess-counter", 20, 14, 560, 40));
  parts.push_back(Part("mess-mini", 26, 19, 70, 10, "GALLEY"));

  // --- the cooked-food shelf and its live stock --------------------------
  // The bottles are one-per-food and follow the same store that drives the
  // fill bar, so cooking adds visible stock and meals remove it.
  parts.push_back(Part("mess-rackbox", 20, 70, 560, 90));
  parts.push_back(Part("mess-mini", 28, 76, 150, 10, "GALLEY STORE"));
  parts.push_back(Part("mess-mini", 420, 76, 140, 10, "COOKED FOOD"));
  addStockRow("mess-food-bottle", sim.MessRack(), sim.MessCapacity(), 88, 18);
  // paintMess(): the fill width, its tone and the readout are read off
  // MESS.rack/capacity at paint time, so the gauge can never disagree with
  // the panel beside it.
  parts.push_back(Part("mess-track", 40, 110, 520, 22));
  const double pct = sim.MessCapacity() > 0.0
                         ? std::round((sim.MessRack() / sim.MessCapacity()) * 100.0)
                         : 0.0;
  MachinePart fill = Part("mess-fill", 40, 110, 0, 22);
  fill.box.w = static_cast<float>(std::round(520.0 * (pct / 100.0)));
  fill.tone = pct >= 60.0 ? "#7f9161" : (pct >= 25.0 ? "#c08a3e" : "#b05a42");
  parts.push_back(fill);
  parts.push_back(Part("mess-readout", 40, 136, 520, 16,
                       Num(sim.MessRack()) + " of " + Num(sim.MessCapacity()) +
                           " food in the galley"));

  // --- one plate per crew member, each its own hunger clock --------------
  // paintMess(): tp = round(hungerTick / MESS_EVERY * 100); the bar is
  // 118 * tp/100 wide and reads red at hunger >= 70, and the caption counts
  // the actions left on that person's clock. A hungerTick of 0 means a whole
  // cycle is left, so the bar starts empty and fills as the clock runs.
  parts.push_back(Part("mess-mini", 28, 182, 140, 10, "WHO EATS NEXT"));
  const std::size_t hands = sim.Crew().size();
  const double every = sim.GameRules().mess.every;
  for (std::size_t k = 0; k < hands; ++k) {
    const CrewMember& crew = sim.Crew()[k];
    const float px = 26.0f + static_cast<float>(k) * 141.0f;
    parts.push_back(
        Part("mess-plate", 20.0f + static_cast<float>(k) * 141.0f, 200, 130, 70));
    parts.push_back(Part("mess-plate-name", px, 206, 118, 14, crew.name));
    parts.push_back(Part("mess-track", px, 228, 118, 12));
    const double tickPct =
        every > 0.0 ? std::round((crew.hungerTick / every) * 100.0) : 0.0;
    const std::string tickTone = crew.hunger >= 70.0 ? "#b05a42" : "#7f9161";
    MachinePart tick = Part("mess-fill mess-fill-small", px, 228, 0, 12);
    tick.box.w = static_cast<float>(std::round(118.0 * (tickPct / 100.0)));
    tick.tone = tickTone;
    parts.push_back(tick);
    parts.push_back(Part("mess-plate-sub", px, 246, 118, 14,
                         Num(every - crew.hungerTick) + " actions to next meal"));
  }

  // --- the dry-ration input shelf, below the crew's meal clocks ------------
  parts.push_back(Part("mess-rackbox", 20, 280, 560, 72));
  parts.push_back(Part("mess-mini", 28, 286, 150, 10, "RATION RACK"));
  parts.push_back(Part(
      "mess-mini", 300, 286, 260, 10,
      "1 DRY RATION -> " + Num(sim.GameRules().mess.foodPerRation) + " FOOD"));
  addStockRow("mess-ration-pack", sim.MessRations(),
              sim.MessRationCapacity(), 301, 18);
  parts.push_back(Part(
      "mess-readout", 40, 325, 520, 16,
      Num(sim.MessRations()) + " of " + Num(sim.MessRationCapacity()) +
          " dry rations in the larder"));
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
  model.rackReadout = Num(rack) + " of " + Num(capacity) + " food in the galley";
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
  rackPanel.title = "Food Rack";
  rackPanel.Name(Num(rack), model.rackTone);
  rackPanel.Sub("of " + Num(capacity));
  rackPanel.Sub("cooked food on the counter");
  rackPanel.Bar(model.rackPercent, model.rackTone);
  rackPanel.Field("Capacity", Num(capacity) + " food, hard ceiling");
  rackPanel.Field("Ration rack", Num(sim.MessRations()) + " of " +
                                    Num(sim.MessRationCapacity()) + " dry rations");
  screen.left.push_back(rackPanel);

  NavPanel cycle;
  cycle.id = "mess-cycle";
  cycle.title = "Hunger Cycle";
  cycle.Field("Every crew member",
              Num(sim.GameRules().mess.every) + " of their own actions");
  cycle.Field("Hunger rises", "by " + Num(sim.GameRules().mess.hungerPerMeal) +
                                  " over those " + Num(sim.GameRules().mess.every) + " clicks");
  cycle.Field("One food", "takes " + Num(sim.GameRules().mess.hungerPerFood) + " hunger off");
  cycle.Field("One meal", Num(sim.GameRules().mess.hungerPerMeal /
                              std::max(1.0, sim.GameRules().mess.hungerPerFood)) + " food");
  cycle.Field("Eating stops at", Num(sim.GameRules().mess.hungerFloor) + " hunger");
  cycle.Field("An empty counter", "nobody eats, hunger keeps climbing");
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
    cooking.Field("One batch",
                  "1 dry ration becomes " + Num(sim.GameRules().mess.foodPerRation) +
                      " food");
    cooking.Field("Takes",
                  Num(sim.GameRules().mess.foodPerRation) +
                      " actions - the button fills green, then the food lands");
    cooking.Field("Starts at", "under " + Num(sim.GameRules().mess.cookBelow) + " food");
    cooking.Field("Larder", Num(sim.MessRations()) + " ration(s) left");
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
                   "once the counter falls under " +
                   Num(sim.GameRules().mess.cookBelow) +
                   " food, batch after batch, until it is full again.");
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
                 : "A batch spends one dry ration and puts five food on the "
                   "counter when the five clicks are done. Four hands eat "
                   "twelve a round, so the counter is restocked at ten, never "
                   "run dry.");
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

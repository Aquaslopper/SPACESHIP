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
  stage.width = 600.0f;
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

  parts.push_back(Part("mess-counter", 20, 14, 560, 40));
  parts.push_back(Part("mess-mini", 26, 19, 70, 10, "GALLEY"));

  parts.push_back(Part("mess-rackbox", 20, 70, 560, 90));
  parts.push_back(Part("mess-mini", 28, 76, 150, 10, "GALLEY STORE"));
  parts.push_back(Part("mess-mini", 420, 76, 140, 10, "COOKED FOOD"));
  addStockRow("mess-food-bottle", sim.MessRack(), sim.MessCapacity(), 88, 18);
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

  parts.push_back(Part("mess-mini", 28, 182, 140, 10, "WHO EATS NEXT"));
  const std::size_t hands = sim.Crew().size();
  if (hands == 0) {
    parts.push_back(Part("mess-rackbox", 20, 200, 560, 54));
    parts.push_back(Part("mess-readout", 40, 219, 520, 16, "No crew aboard"));
  } else {
    const double every = sim.GameRules().mess.every;
    const std::size_t cols = std::max<std::size_t>(4, hands);
    const float gap = cols > 6 ? 4.0f : (cols > 4 ? 6.0f : 10.0f);
    const float plateW = std::floor(
        (560.0f - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
    const float step = cols > 1
                           ? (560.0f - plateW * static_cast<float>(cols)) /
                                     static_cast<float>(cols - 1) +
                                 plateW
                           : 0.0f;
    const float pad = cols > 5 ? 4.0f : 6.0f;
    const float innerW = std::max(12.0f, plateW - 2.0f * pad);
    for (std::size_t k = 0; k < hands; ++k) {
      const CrewMember& crew = sim.Crew()[k];
      const float plateX = std::round(20.0f + static_cast<float>(k) * step);
      const float px = plateX + pad;
      const std::size_t sp = crew.name.find(' ');
      const std::string plateName =
          (plateW < 96.0f && sp != std::string::npos) ? crew.name.substr(0, sp)
                                                      : crew.name;
      parts.push_back(Part("mess-plate", plateX, 200, plateW, 54));
      parts.push_back(Part("mess-plate-name", px, 208, innerW, 14, plateName));
      parts.push_back(Part("mess-track", px, 230, innerW, 12));
      const double tickPct =
          every > 0.0 ? std::round((crew.hungerTick / every) * 100.0) : 0.0;
      const std::string tickTone = crew.hunger >= 70.0 ? "#b05a42" : "#7f9161";
      MachinePart tick = Part("mess-fill mess-fill-small", px, 230, 0, 12);
      tick.box.w = static_cast<float>(std::round(innerW * (tickPct / 100.0)));
      tick.tone = tickTone;
      parts.push_back(tick);
    }
  }

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
  const MessRules& messRules = sim.GameRules().mess;
  const double mealFood = messRules.hungerPerMeal /
                          std::max(1.0, messRules.hungerPerFood);
  const double crewFoodPerCycle =
      mealFood * static_cast<double>(sim.Crew().size());
  const Task* cookTask = sim.FindOngoing("cook");
  // const bool hydroponicsBusy = sim.FindOngoing("project-food") != nullptr;

  for (const CrewMember& crew : sim.Crew()) {
    PlateView plate;
    plate.id = crew.id;
    plate.name = crew.name;
    plate.tickPercent =
        std::round((crew.hungerTick / sim.GameRules().mess.every) * 100.0);
    plate.sub = Num(sim.GameRules().mess.every - crew.hungerTick) +
                " clicks to next meal";
    plate.tone = crew.hunger >= 70.0 ? "#b05a42" : "#7f9161";
    model.plates.push_back(plate);
  }

  TaskScreenModel& screen = model.screen;
  screen.head = TaskHead{"ROOM", "Mess Hall",
                         "Hunger does not knock - it is on a clock, same as "
                         "everything else aboard."};

  NavPanel rackPanel;
  rackPanel.id = "mess-rack-p";
  rackPanel.title = "Galley Stock";
  rackPanel.Name(Num(rack), model.rackTone);
  rackPanel.Sub("of " + Num(capacity));
  rackPanel.Sub("cooked food on the counter");
  rackPanel.Bar(model.rackPercent, model.rackTone);
  rackPanel.Field("Capacity", Num(capacity) + " food, hard ceiling");
  rackPanel.Field("Dry-ration larder", Num(sim.MessRations()) + " of " +
                                          Num(sim.MessRationCapacity()) + " dry rations");
  screen.left.push_back(rackPanel);

  NavPanel cycle;
  cycle.id = "mess-cycle";
  cycle.title = "Hunger Cycle";
  cycle.Field("Every crew member",
              Num(sim.GameRules().mess.every) + " of their own clicks");
  cycle.Field("Hunger rises", "by " + Num(sim.GameRules().mess.hungerPerMeal) +
                                  " over those " + Num(sim.GameRules().mess.every) + " clicks");
  cycle.Field("One food", "takes " + Num(sim.GameRules().mess.hungerPerFood) + " hunger off");
  cycle.Field("One meal", "up to " + Num(sim.GameRules().mess.hungerPerMeal /
                                            std::max(1.0, sim.GameRules().mess.hungerPerFood)) +
                              " food (counter and floor can limit it)");
  cycle.Field("Eating stops at", Num(sim.GameRules().mess.hungerFloor) + " hunger");
  cycle.Field("An empty counter",
              "missed meal adds +" + Num(sim.GameRules().mess.hungerPerMeal) + " hunger");
  screen.left.push_back(cycle);

  NavPanel starve;
  starve.id = "mess-starve";
  starve.title = "Starvation";
  starve.placement = "np-last";
  starve.Field("Risk begins at", Num(sim.GameRules().mess.starveAt) + "% hunger");
  starve.Field("Cost",
               "1 health every " + Num(sim.GameRules().mess.starveEvery) + " clicks");
  starve.Note("Missing a meal adds +" + Num(sim.GameRules().mess.hungerPerMeal) +
              " hunger; famine pushes crew past " +
              Num(sim.GameRules().mess.starveAt) + "% fast.");
  screen.left.push_back(starve);

  NavPanel cooking;
  cooking.id = "mess-cook-p";
  cooking.title = "Cooking";
  cooking.placement = "np-half";
  if (cookTask != nullptr) {
    cooking.Field("Batch progress", Num(cookTask->left) + " clicks remaining");
    cooking.Field("Batch yield", "up to " + Num(messRules.foodPerRation) +
                                      " cooked food");
    cooking.Field("Larder", Num(sim.MessRations()) + " of " +
                                Num(sim.MessRationCapacity()) + " dry rations");
  /* } else if (hydroponicsBusy) {
    cooking.Dim("Hydroponics is processing produce; cooking is paused."); */
  } else if (model.full) {
    cooking.Dim("The cooked-food rack is full; cooking waits for space.");
  } else if (sim.MessRations() < 1.0) {
    cooking.Dim("No dry rations in the larder; a batch cannot start.");
  } else {
    cooking.Field("Input", "1 dry ration");
    cooking.Field("Yield", "up to " + Num(messRules.foodPerRation) +
                               " cooked food (rack capacity applies)");
    cooking.Field("Takes",
                  Num(messRules.foodPerRation) +
                      " clicks; food lands when the batch finishes");
    cooking.Field("Larder", Num(sim.MessRations()) + " of " +
                                Num(sim.MessRationCapacity()) + " dry rations");
  }
  screen.under.push_back(cooking);

  NavPanel appetite;
  appetite.id = "mess-appetite";
  appetite.title = "Crew Appetite";
  appetite.placement = "np-half-last";
  if (sim.Crew().empty()) {
    appetite.Dim("No crew aboard.");
  } else {
    int hungry = 0;
    double hungerSum = 0.0;
    for (const CrewMember& crew : sim.Crew()) {
      if (crew.hunger >= 70.0) { ++hungry; }
      hungerSum += crew.hunger;
    }
    appetite.Field("At least 70% hunger",
                   Num(hungry) + " of " + Num(static_cast<double>(sim.Crew().size())));
    appetite.Field("Average hunger",
                   Num(std::round(hungerSum / static_cast<double>(sim.Crew().size()))) +
                       "%");
  }
  screen.under.push_back(appetite);

  const double autoAt = cook != nullptr ? sim.CookThreshold(*cook) : messRules.cookBelow;
  NavPanel crewPanel;
  crewPanel.id = "mess-crew";
  crewPanel.title = "Galley Crew";
  if (cook != nullptr) {
    crewPanel.Name(cook->name);
    crewPanel.Sub(cook->role + " - kitchen job");
    crewPanel.Field("Skill", cook->skill);
    crewPanel.Field("Acts at", "<= " + Num(autoAt) + " food");
    crewPanel.Field("Activity", cook->activity);
    crewPanel.Note("Cooks at or below " + Num(autoAt) +
                   " food, one ration a batch, until the " + Num(capacity) +
                   "-food counter is full.");
  } else {
    crewPanel.Dim("No automatic cook is assigned. The commander can start a "
                  "batch manually; it still needs one dry ration.");
  }
  screen.right.push_back(crewPanel);

  /* if (hydroponicsBusy) {
    screen.bandNote =
        "Hydroponics is processing produce into dry rations; galley cooking is paused.";
  } else */ if (cookTask != nullptr) {
    screen.bandNote =
        "Batch in progress: one dry ration was spent; up to " +
        Num(messRules.foodPerRation) + " cooked food lands after " +
        Num(cookTask->left) + " more clicks.";
  } else if (model.full) {
    screen.bandNote =
        "The cooked-food rack is full (" + Num(rack) + " of " +
        Num(capacity) + "); cooking waits until food is eaten.";
  } else if (sim.MessRations() < 1.0) {
    screen.bandNote =
        "The dry-ration larder is empty; the cook cannot start another batch.";
  } else if (sim.Crew().empty()) {
    screen.bandNote =
        "One dry ration yields up to " + Num(messRules.foodPerRation) +
        " cooked food after " + Num(messRules.foodPerRation) +
        " clicks. No crew aboard; cooked food waits on the counter.";
  } else {
    screen.bandNote =
        "One dry ration yields up to " + Num(messRules.foodPerRation) +
        " cooked food after " + Num(messRules.foodPerRation) + " clicks. " +
        Num(static_cast<double>(sim.Crew().size())) + " crew use " +
        Num(crewFoodPerCycle) + " food per " + Num(messRules.every) +
        "-click cycle; automatic restocking starts at or below " +
        Num(autoAt) + " food and runs until the " + Num(capacity) +
        "-food counter is full while dry rations last.";
  }
  const bool canUseCookButton =
      cookTask != nullptr ||
      (!model.full && sim.MessRations() >= 1.0 /* && !hydroponicsBusy */);
  NavPanel cookPanel;
  cookPanel.id = "mess-cook-action";
  cookPanel.title = "Galley Batch";
  cookPanel.placement = "np-last";
  cookPanel.Note(screen.bandNote);
  cookPanel.buttons.push_back(BuildProgressButton(
      sim, "cook", cookTask != nullptr ? "Cook in progress" : "Cook a Batch",
      canUseCookButton));
  screen.right.push_back(cookPanel);

  screen.stage = BuildMessMachine(sim);
  screen.hasStage = true;
  return model;
}

}  // namespace ship

// ============================================================================
//  Describe.cpp — describeTarget(), word for word.
//  ---------------------------------------------------------------------------
//  Read this beside the reference's Section I. Every row is in the reference's
//  order, every sentence keeps its hyphens and its trailing full stops, and
//  every number goes through the JavaScript formatter - a briefing that says
//  "18 of 24" in one edition and "18 of 24.0" in the other is not a port.
// ============================================================================
#include "ui/Describe.h"

#include <algorithm>
#include <cmath>

#include "core/JsNumber.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

const std::string& AirWord(const Simulation& sim) {
  static const std::string kEmpty;
  const std::vector<AirStage>& stages = sim.GameRules().air.stages;
  const int index = sim.AirStageIndex();
  if (index < 0 || static_cast<std::size_t>(index) >= stages.size()) { return kEmpty; }
  return stages[static_cast<std::size_t>(index)].word;
}

const CrewMember* Find(const Simulation& sim, const std::string& id) {
  return sim.FindCrew(id);
}

const Room* FindRoom(const Simulation& sim, const std::string& id) {
  for (const Room& room : sim.Rooms()) {
    if (room.id == id) { return &room; }
  }
  return nullptr;
}

const Stat* FindStat(const Simulation& sim, const std::string& id) {
  for (const Stat& stat : sim.Stats()) {
    if (stat.id == id) { return &stat; }
  }
  return nullptr;
}

const InfoRow* FindInfo(const Simulation& sim, const std::string& id) {
  for (const InfoRow& info : sim.Infos()) {
    if (info.id == id) { return &info; }
  }
  return nullptr;
}

const Incident* FindEvent(const Simulation& sim, const std::string& id) {
  for (const Incident& event : sim.Events()) {
    if (event.id == id) { return &event; }
  }
  return nullptr;
}

// ROOM_FITS: rooms that open as real fitted systems rather than as a store.
bool RoomFits(const std::string& id) {
  if (id == "simulacron") { return true; }
  if (id.size() < 2 || id[0] != 'r') { return false; }
  for (std::size_t i = 1; i < id.size(); ++i) {
    if (id[i] < '0' || id[i] > '9') { return false; }
  }
  const int number = std::stoi(id.substr(1));
  return number >= 1 && number <= 13;
}

std::string Plural(double count, const std::string& word) {
  return Num(count) + " " + word + (count == 1.0 ? "" : "s");
}

std::string OccupantsOf(const Simulation& sim, const std::string& roomId) {
  std::string names;
  for (const CrewMember& crew : sim.Crew()) {
    if (crew.room == roomId && !crew.job.empty()) {
      names += (names.empty() ? "" : ", ") + crew.name;
    }
  }
  return names.empty() ? std::string("None") : names;
}

int SlotsIn(const Simulation& sim, const std::string& state) {
  int count = 0;
  for (const CanisterSlot& slot : sim.Life().slots) {
    if (slot.state == state) { ++count; }
  }
  return count;
}

}  // namespace

double EffectiveJobAt(const Simulation& sim, const CrewMember& crew) {
  const double value = crew.jobAt - sim.AirSpec().jobDrop;
  return value < 5.0 ? 5.0 : value;
}

std::string JobThreshold(const Simulation& sim, const CrewMember& crew) {
  if (crew.job == "maintenance") { return Num(EffectiveJobAt(sim, crew)) + "% condition"; }
  if (crew.job == "filters") {
    const std::vector<AirStage>& stages = sim.GameRules().air.stages;
    const double stage = crew.jobAt < 0.0 ? 0.0 : crew.jobAt;
    const std::size_t index = static_cast<std::size_t>(stage);
    if (index < stages.size()) { return stages[index].word + " air"; }
    return " air";
  }
  if (crew.job == "navigation") {
    const double rate = std::max(1.0, std::min(crew.jobAt, 5.0)) * 4.0;
    return "Fuel use -" + Num(rate) + "% when plotting";
  }
  if (crew.job == "cleaner") {
    return "Crew morale +1 every " + Num(sim.CleanerIntervalOf(crew)) + " clicks";
  }
  if (crew.job == "kitchen") {
    const MessRules& mess = sim.GameRules().mess;
    return "1 dry ration -> up to " + Num(mess.foodPerRation) +
           " cooked food in " + Num(mess.foodPerRation) + " clicks";
  }
  return "-";
}

bool DescribeTargetExists(const Simulation& sim, const std::string& kind,
                          const std::string& id) {
  if (kind == "room") { return FindRoom(sim, id) != nullptr; }
  if (kind == "crew") { return Find(sim, id) != nullptr; }
  if (kind == "system") { return FindStat(sim, id) != nullptr; }
  if (kind == "info") { return FindInfo(sim, id) != nullptr; }
  if (kind == "event") { return FindEvent(sim, id) != nullptr; }
  if (kind == "station") {
    if (!sim.State().docked || sim.Map().here != id) { return false; }
    for (const Port& port : sim.World().ports) {
      if (port.id == id) { return true; }
    }
    return false;
  }
  return true;
}

namespace {

// ENGINE ROOM. Leads with the number that matters: the drive wears with every
// action spent aboard.
void RoomEngine(const Simulation& sim, Description& t) {
  const CrewMember* engineer = sim.Engineer();
  const double missing = std::max(0.0, 100.0 - sim.State().engineCondition);
  const double presses = std::ceil(missing / 10.0);
  t.sub = "Propulsion and power - condition " + Num(sim.State().engineCondition) + "%";
  t.rows = {
      {"Condition", Num(sim.State().engineCondition) + "%"},
      {"Wear rate", "1% per " + Num(sim.GameRules().engine.wearEvery) +
                        " clicks spent aboard"},
      {"Maintenance",
       missing <= 0.0
           ? std::string("Not needed - drive is perfect")
           : "1 click per press - restores +10% (" + Plural(presses, "click") + " to 100%)"},
      {"Warning at", Num(sim.GameRules().engine.warnBelow) +
                         "% - a red line joins the sidebar"},
      {"Engineer", engineer ? engineer->name + " - acts at " +
                                  Num(EffectiveJobAt(sim, *engineer)) + "%"
                            : std::string("Nobody holds the job")},
  };
  t.acts = {
      {"Run the drive",
       missing <= 0.0 ? std::string("refused at 100%")
                      : std::string("1 click (+10%)")},
      {"The engineer acts alone",
       engineer ? "at " + Num(EffectiveJobAt(sim, *engineer)) + "%"
                : std::string("nobody assigned")},
  };
}

void RoomO2(const Simulation& sim, Description& t) {
  const CrewMember* hand = sim.FilterHand();
  t.sub = "Life support - air quality " + AirWord(sim);
  t.rows = {
      {"Air quality", AirWord(sim)},
      {"Filter life",
       sim.BankRunning()
           ? Num(sim.State().filterLife) + " of " +
                 Num(sim.GameRules().life.lifeMax) + " clicks"
           : std::string("bank is not running")},
      {"Slots", Num(SlotsIn(sim, "empty")) + " bare, " +
                    Num(SlotsIn(sim, "installed")) + " seated, " +
                    Num(SlotsIn(sim, "active")) + " running"},
      {"Canisters",
       Num(sim.Life().canisters) + " of " + Num(sim.Life().capacity) + " racked"},
      {"Filter hand", hand ? hand->name + " - acts at " + JobThreshold(sim, *hand)
                           : std::string("nobody assigned")},
      {"Reward", "+10 pro-click when all three run"},
  };
  t.acts = {
      {"Install a canister", "1 click + 2 to seat"},
      {"Turn on the filters", "1 click - needs all three"},
  };
}

void RoomMess(const Simulation& sim, Description& t) {
  const CrewMember* cook = sim.Cook();
  const MessRules& rules = sim.GameRules().mess;
  const double rack = sim.MessRack();
  const double capacity = sim.MessCapacity();
  const double rations = sim.MessRations();
  const double rationCapacity = sim.MessRationCapacity();
  const double batchActions = rules.foodPerRation;
  const double mealFood = rules.hungerPerMeal / std::max(1.0, rules.hungerPerFood);
  const double crewFoodPerCycle = mealFood * static_cast<double>(sim.Crew().size());
  const Task* cookTask = sim.FindOngoing("cook");
  const bool hydroponicsBusy = sim.FindOngoing("project-food") != nullptr;
  const bool full = rack >= capacity;

  std::string cookingStatus;
  if (cookTask != nullptr) {
    cookingStatus = "Batch in progress - " + Num(cookTask->left) +
                    " clicks remaining; up to " + Num(rules.foodPerRation) +
                    " cooked food will land";
  } else if (hydroponicsBusy) {
    cookingStatus = "Paused while Hydroponics processes produce";
  } else if (full) {
    cookingStatus = "Cooked-food counter is full";
  } else if (rations < 1.0) {
    cookingStatus = "No dry rations in the larder";
  } else {
    cookingStatus = Num(batchActions) + " clicks - one dry ration yields up to " +
                    Num(rules.foodPerRation) + " cooked food";
  }

  std::string cookAction;
  if (cookTask != nullptr) {
    cookAction = "already in progress";
  } else if (hydroponicsBusy) {
    cookAction = "unavailable while Hydroponics processes produce";
  } else if (full) {
    cookAction = "refused - cooked-food counter full";
  } else if (rations < 1.0) {
    cookAction = "refused - dry-ration larder empty";
  } else {
    cookAction = Num(batchActions) + " clicks";
  }

  t.sub = "Galley: " + Num(rack) + " of " + Num(capacity) +
          " cooked food; larder: " + Num(rations) + " of " +
          Num(rationCapacity) + " dry rations";
  t.rows = {
      {"Galley", Num(rack) + " of " + Num(capacity) + " cooked food"},
      {"Larder", Num(rations) + " of " + Num(rationCapacity) + " dry rations"},
      {"Hunger cycle", Num(rules.hungerPerMeal) + " hunger over " +
                           Num(rules.every) + " clicks per crew member; clocks staggered"},
      {"Meal", "Up to " + Num(mealFood) + " cooked food; " +
                   Num(rules.hungerPerFood) + " hunger per food, stops at " +
                   Num(rules.hungerFloor)},
      {"Crew demand", Num(crewFoodPerCycle) + " cooked food per " +
                          Num(rules.every) + "-click cycle"},
      {"Cooking", cookingStatus},
      {"Starvation", "1 health every " + Num(rules.starveEvery) +
                         " clicks at " + Num(rules.starveAt) + "+ hunger"},
      {"Cook", cook ? cook->name + " - kitchen duty" :
                       std::string("No automatic cook assigned")},
  };
  t.acts = {{"Cook a batch", cookAction}};
}

void RoomCargo(const Simulation& sim, Description& t) {
  double used = 0.0;
  double worth = 0.0;
  for (const GoodsLot& lot : sim.Goods()) {
    used += lot.vol;
    worth += sim.State().docked ? sim.LotPrice(lot) : lot.value;
  }
  t.rows = {
      {"Hold", Num(used) + " of " + Num(sim.CargoStore().capacity) + " m3 used"},
      {"Lots", Num(static_cast<double>(sim.Goods().size())) + " aboard"},
      {"Manifest", Num(worth) + " shiplets at current prices"},
  };
  for (const GoodsLot& lot : sim.Goods()) {
    const double price = sim.State().docked ? sim.LotPrice(lot) : lot.value;
    t.rows.push_back({lot.name, Num(lot.vol) + " m3 - " + Num(price) + " sh - " +
                                    lot.kind + (lot.risk ? " (UNREGISTERED)" : "")});
  }
  t.sub = "Storage - " + Num(used) + " of " + Num(sim.CargoStore().capacity) +
          " m3 in use";
  t.acts = {
      {"Jettison a lot", "1 click"},
      {"Sell a lot", "at a station"},
  };
}

void RoomScience(const Simulation& sim, Description& t) {
  double worth = 0.0;
  for (const SurveyRecord& record : sim.Survey()) { worth += record.value; }
  t.rows = {
      {"Logs held", Num(static_cast<double>(sim.Survey().size())) + " survey records"},
      {"Unsold value", Num(worth) + " shiplets"},
      {"Sources", "Deep scans, salvage, completed contracts"},
  };
  for (const SurveyRecord& record : sim.Survey()) {
    t.rows.push_back(
        {record.name, record.src + " - " + Num(record.value) + " sh - " + record.state});
  }
  t.sub = "Survey records - " + Num(static_cast<double>(sim.Survey().size())) +
          " logs on file";
  t.acts = {
      {"Prepare a data package", "1 click + 2 work"},
      {"Sell a survey", "at a station"},
  };
}

}  // namespace

Description DescribeTarget(const Simulation& sim, const std::string& kind,
                           const std::string& id) {
  Description t;
  t.kind = kind;
  t.id = id;

  // A tab can outlive what it watches. One guard, all five lookups.
  if (!DescribeTargetExists(sim, kind, id)) {
    t.gone = true;
    t.kindLabel = "GONE";
    t.title = "No longer aboard";
    t.sub = "Whatever this tab was watching is not on the ship any more.";
    t.rows = {{"Was", kind + " " + id}, {"Status", "no record"}};
    return t;
  }

  if (kind == "wiki" || kind == "awards") {
    t.kindLabel = kind == "wiki" ? "WIKI" : "LOGBOOK";
    t.title = kind == "wiki" ? "Ship's Handbook" : "Commendations & Rewards";
    t.sub = "Free reference and voyage records";
    return t;
  }

  if (kind == "station") {
    for (const Port& port : sim.World().ports) {
      if (port.id == id) { t.title = port.name; break; }
    }
    t.kindLabel = "STATION";
    t.sub = "Docked - market, mission board, shipyard and crew";
    t.rows = {{"Berth", "Docked"}, {"Purse", Num(sim.State().shiplets) + " sh"}};
    t.acts = {{"Trade, hire or accept a contract", "1 click"}, {"Undock", "1 click"}};
    return t;
  }

  if (kind == "event") {
    const Incident* event = FindEvent(sim, id);
    t.kindLabel = "EVENT";
    t.title = event ? event->kind + " report" : "Report";
    t.sub = "Noticed by the ship. Nobody has looked into it yet.";
    t.rows = {
        {"Kind", event ? event->kind : std::string("-")},
        {"Reported by", event ? event->source : std::string("-")},
        {"Status", "Unexamined"},
        {"Detail", "Not yet established"},
    };
    t.acts = {
        {"Examine the report", "1 click"},
        {"Send a crew member", "varies"},
        {"Log and ignore", "1 click"},
    };
    return t;
  }

  if (kind == "nav") {
    t.kindLabel = "NAV";
    t.title = "Navigation";
    t.sub = "Course plotting and system chart";
    return t;
  }

  if (kind == "room") {
    const Room* room = FindRoom(sim, id);
    t.kindLabel = "ROOM";
    t.title = room->name;
    t.sub = room->systems;
    if (RoomFits(id) || id == "bridge") {
      t.rows = {{"Systems", room->systems},
                {"Occupants", OccupantsOf(sim, id)},
                {"Access", "Right-click to open the room"}};
      return t;
    }
    t.rows = {
        {"Designation", room->tag},
        {"Systems", room->systems},
        {"Condition", "Operational"},
        {"Occupants", OccupantsOf(sim, room->id)},
        {"Deck position", "x " + Num(room->box.x) + " / y " + Num(room->box.y)},
    };
    if (id == "engine") { RoomEngine(sim, t); }
    if (id == "o2") { RoomO2(sim, t); }
    if (id == "mess") { RoomMess(sim, t); }
    if (id == "cargo") { RoomCargo(sim, t); }
    if (id == "science") { RoomScience(sim, t); }
    if (id == "quarters") {
      t.acts = {{"Talk to crew", "1 click - up to +5 morale"}, {"Give orders", "1 click + work"}};
    }
    return t;
  }

  if (kind == "crew") {
    // Purely informational on purpose: the crew window reads the record itself
    // on every paint, so a tab left open across a shift never shows a stale
    // number. This branch only sets the header.
    const CrewMember* crew = Find(sim, id);
    t.kindLabel = "CREW";
    t.title = crew->name;
    t.sub = crew->role;
    return t;
  }

  if (kind == "system") {
    const Stat* stat = FindStat(sim, id);
    t.kindLabel = "SYSTEM";
    t.title = stat->label;
    t.sub = "Reading " + Num(stat->value) + stat->unit;
    const CrewMember* engineer = sim.Engineer();
    t.rows = {
        {"Current reading", Num(stat->value) + stat->unit},
        {"Status", stat->value > 80.0 ? "Operational"
                   : stat->value > 40.0 ? "Degraded"
                                        : "Damaged"},
        {"Trend", "See active reports"},
        {"Maintained by", engineer ? engineer->name : std::string("Nobody assigned")},
        {"Last serviced", "Not recorded"},
    };
    const std::string label =
        id == "hull" ? "Patch hull"
        : id == "shield" ? "Service shields"
        : id == "energy" ? "Recharge energy"
                          : "Transfer fuel";
    const bool kit = id == "hull" || id == "shield";
    t.acts = {{label, std::string("1 click - consumes ") + (kit ? "a repair kit" : "a fuel cell")}};
    return t;
  }

  // Everything else is a readout: a side-panel line, described.
  const InfoRow* info = FindInfo(sim, id);
  t.kindLabel = "READOUT";
  if (info != nullptr) {
    t.title = info->label;
    t.sub = info->value;
    t.rows = {
        {"Reading", info->value},
        {"Source", "Ship sensors"},
        {"Updated", "On arrival"},
    };
  }
  t.acts = {{"Open Navigation", "free"}, {"Open Comms Relay", "free"}};
  if (id == "life") { t.acts = {{"Open O2 Filter Room", "free"}}; }
  return t;
}

}  // namespace ship

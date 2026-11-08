// ============================================================================
//  Trace.cpp — the canonical projection for selected differential tests.
//  ---------------------------------------------------------------------------
//  The differential harness runs selected command scripts twice: once through
//  this simulation, once through the reference in a Node VM. Both sides print
//  this projection after each checkpoint; streams should match for those
//  exercised scripts. This does not cover the GUI or prove complete parity.
//
//  Two rules make that comparison honest:
//    * keys are sorted, so object order can never mask a difference;
//    * numbers print as JavaScript prints them — an integral value is written
//      without a decimal point, because String(1.0) is "1".
//
//  The feed's wall-clock stamp is deliberately excluded: the reference stamps
//  lines with the time of day, and parity must not depend on when it ran.
// ============================================================================
#include <nlohmann/json.hpp>

#include "core/JsNumber.h"
#include "core/Simulation.h"

namespace ship {

namespace {

using Json = nlohmann::json;

// A double is emitted as an integer when JavaScript would print one.
Json Num(double value) {
  if (value == 0.0) { return Json(0); }
  const double rounded = js::Round(value);
  if (rounded == value && value > -1e15 && value < 1e15) {
    return Json(static_cast<long long>(value));
  }
  return Json(value);
}

Json SortedKeys(const std::vector<std::string>& keys) {
  std::vector<std::string> copy = keys;
  std::sort(copy.begin(), copy.end());
  return Json(copy);
}

}  // namespace

std::string Simulation::TraceJson() const {
  Json out = Json::object();

  out["actions"] = Num(actionCounter_);
  out["gameOver"] = state_.gameOver;

  {
    Json engine = Json::object();
    engine["condition"] = Num(state_.engineCondition);
    engine["wearTick"] = Num(state_.wearTick);
    out["engine"] = engine;
  }

  {
    Json filter = Json::object();
    filter["airHold"] = Num(state_.airHold);
    filter["life"] = Num(state_.filterLife);
    filter["canisters"] = Num(life_.canisters);
    filter["rackWarned"] = state_.rackWarned;
    filter["rackWarnedMess"] = state_.rackWarnedMess;
    Json slots = Json::array();
    for (const CanisterSlot& slot : life_.slots) { slots.push_back(slot.state); }
    filter["slotStates"] = slots;
    out["filter"] = filter;
  }

  {
    Json stats = Json::object();
    for (const Stat& stat : stats_) { stats[stat.id] = Num(stat.value); }
    out["stats"] = stats;
  }

  {
    Json purse = Json::object();
    purse["shiplets"] = Num(state_.shiplets);
    purse["fuelCells"] = Num(state_.fuelCells);
    purse["repairKits"] = Num(state_.repairKits);
    purse["medkits"] = Num(state_.medkits);
    purse["lotSerial"] = Num(state_.lotSerial);
    out["purse"] = purse;
  }

  {
    Json counters = Json::object();
    counters["crewTick"] = Num(state_.crewTick);
    counters["payTick"] = Num(state_.payTick);
    out["counters"] = counters;
  }

  out["mess"] = Json{{"rack", Num(mess_.rack)}};

  {
    Json crew = Json::array();
    for (const CrewMember& member : crew_) {
      Json entry = Json::object();
      entry["id"] = member.id;
      entry["name"] = member.name;
      entry["health"] = Num(member.health);
      entry["morale"] = Num(member.morale);
      entry["fatigue"] = Num(member.fatigue);
      entry["hunger"] = Num(member.hunger);
      entry["hungerTick"] = Num(member.hungerTick);
      entry["wallet"] = Num(member.wallet);
      entry["job"] = member.job;
      entry["skill"] = member.skill;
      entry["jobAt"] = Num(member.jobAt);
      entry["activity"] = member.activity;
      crew.push_back(entry);
    }
    out["crew"] = crew;
  }

  {
    Json tasks = Json::array();
    for (const Task& task : state_.ongoing) {
      Json entry = Json::object();
      entry["id"] = task.id;
      entry["type"] = task.type;
      entry["label"] = task.label;
      entry["total"] = Num(task.total);
      entry["left"] = Num(task.left);
      entry["state"] = task.state;
      entry["target"] = task.target;
      entry["by"] = task.by;
      entry["who"] = task.who;
      entry["slot"] = Num(task.slot);
      entry["rate"] = Num(task.rate);
      entry["vol"] = Num(task.vol);
      entry["payload"] = task.payload;
      tasks.push_back(entry);
    }
    out["tasks"] = tasks;
  }

  {
    Json events = Json::array();
    for (const Incident& incident : events_) {
      Json entry = Json::object();
      entry["id"] = incident.id;
      entry["type"] = incident.type;
      entry["stage"] = Num(incident.stage);
      entry["left"] = Num(incident.left);
      entry["a"] = incident.a;
      entry["b"] = incident.b;
      events.push_back(entry);
    }
    out["events"] = events;
  }

  {
    Json sim = Json::object();
    sim["signal"] = state_.sim.signal;
    sim["insight"] = Num(state_.sim.insight);
    sim["sensor"] = Num(state_.sim.sensor);
    sim["serial"] = Num(state_.sim.serial);
    Json memories = Json::array();
    for (const Memory& memory : state_.sim.memories) {
      Json entry = Json::object();
      entry["id"] = Num(memory.id);
      entry["kind"] = memory.kind;
      entry["text"] = memory.text;
      entry["at"] = Num(memory.at);
      entry["who"] = memory.who;
      entry["other"] = memory.other;
      entry["processed"] = memory.processed;
      memories.push_back(entry);
    }
    sim["memories"] = memories;
    Json bonds = Json::object();
    for (const auto& entry : state_.sim.bonds) { bonds[entry.first] = Num(entry.second); }
    sim["bonds"] = bonds;
    out["sim"] = sim;
  }

  {
    Json chronicle = Json::object();
    chronicle["banks"] = Num(state_.chronicle.banks);
    chronicle["projects"] = Num(state_.chronicle.projects);
    chronicle["dreams"] = Num(state_.chronicle.dreams);
    chronicle["meals"] = Num(state_.chronicle.meals);
    chronicle["incidents"] = Num(state_.chronicle.incidents);
    std::vector<std::string> earned;
    for (const auto& entry : state_.chronicle.earned) {
      if (entry.second) { earned.push_back(entry.first); }
    }
    chronicle["earned"] = SortedKeys(earned);
    out["chronicle"] = chronicle;
  }

  {
    Json cooldowns = Json::object();
    for (const auto& entry : state_.actionCooldowns) {
      cooldowns[entry.first] = Num(entry.second.remaining);
    }
    out["cooldowns"] = cooldowns;
  }

  {
    Json map = Json::object();
    map["layer"] = map_.layer;
    map["here"] = map_.here;
    map["sector"] = map_.sector;
    map["zoom"] = Num(map_.zoom);
    out["map"] = map;
  }
  out["navTarget"] = state_.navTarget.has_value() ? Json(*state_.navTarget) : Json(nullptr);
  out["docked"] = state_.docked;
  out["shipClass"] = state_.shipClass;
  out["scanCost"] = Num(scanCost_);
  out["cargo"] = Json{{"capacity", Num(cargo_.capacity)},
                      {"used", Num(CargoUsed())},
                      {"reserved", Num(CargoReserved())}};
  // Phase 6: preferences travel in the trace because the settings popup is a
  // free control, and a free control still has to agree between editions.
  {
    Json options = Json::object();
    options["largeText"] = state_.options.largeText;
    options["confirmRisk"] = state_.options.confirmRisk;
    options["hideCrewLabels"] = state_.options.hideCrewLabels;
    options["theme"] = state_.options.theme;
    options["soundEffects"] = state_.options.soundEffects;
    options["music"] = state_.options.music;
    options["audioLevel"] = Num(state_.options.audioLevel);
    out["options"] = options;
    out["reducedMotion"] = state_.reducedMotion;
  }

  {
    Json scanned = Json::array();
    for (const auto& entry : state_.scanned) {
      if (entry.second) { scanned.push_back(entry.first); }
    }
    std::sort(scanned.begin(), scanned.end());
    out["scanned"] = scanned;
  }
  {
    Json goods = Json::array();
    for (const GoodsLot& lot : goods_) {
      goods.push_back(Json{{"id", lot.id}, {"name", lot.name}, {"kind", lot.kind},
                           {"vol", Num(lot.vol)}, {"value", Num(lot.value)},
                           {"contractId", lot.contractId}, {"goodId", lot.goodId},
                           {"originPort", lot.originPort}, {"boughtAt", Num(lot.boughtAt)},
                           {"stowed", lot.stowed}, {"stowedAt", Num(lot.stowedAt)}});
    }
    out["goods"] = goods;
  }
  {
    Json survey = Json::array();
    for (const SurveyRecord& record : survey_) {
      survey.push_back(Json{{"id", record.id}, {"name", record.name},
                            {"value", Num(record.value)}, {"state", record.state}});
    }
    out["survey"] = survey;
  }
  {
    Json passengers = Json::array();
    for (const Passenger& passenger : state_.passengers) { passengers.push_back(passenger.id); }
    out["passengers"] = passengers;
  }
  {
    Json upgrades = Json::array();
    for (const auto& entry : state_.upgrades) {
      if (entry.second) { upgrades.push_back(entry.first); }
    }
    std::sort(upgrades.begin(), upgrades.end());
    out["upgrades"] = upgrades;
  }
  {
    Json trade = Json::object();
    trade["epoch"] = Num(TradeEpoch());
    trade["completed"] = Num(state_.trade.completed);
    trade["visited"] = state_.trade.visited;
    Json active = Json::array();
    for (const TradeContract& c : ActiveTradeContracts()) {
      active.push_back(Json{{"id", c.id}, {"kind", c.kind}, {"origin", c.origin},
                            {"destination", c.destination}, {"goodId", c.goodId},
                            {"quantity", c.quantity}, {"fulfilled", c.fulfilled},
                            {"reward", Num(c.reward)}, {"status", c.status}});
    }
    trade["active"] = active;
    out["trade"] = trade;
  }
  {
    Json infos = Json::object();
    for (const InfoRow& row : infos_) { infos[row.id] = row.value; }
    out["infos"] = infos;
  }
  {
    Json rooms = Json::array();
    for (const Room& room : rooms_) {
      rooms.push_back(Json{{"id", room.id}, {"x", Num(room.box.x)}, {"y", Num(room.box.y)},
                           {"w", Num(room.box.w)}, {"h", Num(room.box.h)}, {"tag", room.tag}});
    }
    out["rooms"] = rooms;
  }

  {
    Json feed = Json::array();
    for (const FeedLine& line : state_.feed) { feed.push_back(line.text); }
    out["feed"] = feed;
  }

  return out.dump();
}

}  // namespace ship

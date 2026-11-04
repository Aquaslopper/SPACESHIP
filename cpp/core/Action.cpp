// ============================================================================
//  Action.cpp — the heartbeat, and the state it starts from.
//  ---------------------------------------------------------------------------
//  One action = one tick of everything that is counting. performAction() is the
//  only function in the whole port allowed to move the world, and tickWorld()
//  runs its thirteen phases in the reference's exact order. Several of those
//  phases depend on the order (wear runs before the task sweep so a maintenance
//  job landing on this action still leaves a whole engine; the engineer's own
//  automation runs after the task sweep so it cannot lose a step to the tick
//  that created it).
// ============================================================================
#include "core/Simulation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace ship {

namespace {
std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}
}  // namespace

CrewMember MakeCrew(const CrewDefinition& def) {
  CrewMember crew;
  crew.id = def.id;
  crew.name = def.name;
  crew.role = def.role;
  crew.room = def.room;
  crew.activity = def.activity;
  crew.colour = def.colour;
  crew.sex = def.sex;
  crew.hair = def.hair;
  crew.hair2 = def.hair2;
  crew.skin = def.skin;
  crew.hairStyle = def.hairStyle;
  crew.health = def.health;
  crew.morale = def.morale;
  crew.fatigue = def.fatigue;
  crew.hunger = def.hunger;
  crew.wallet = def.wallet;
  crew.hungerTick = def.hungerTick;
  crew.traits = def.traits;
  crew.race = def.race;
  crew.age = def.age;
  crew.homeworld = def.homeworld;
  crew.hobby = def.hobby;
  crew.religion = def.religion;
  crew.job = def.job;
  crew.skill = def.skill;
  crew.jobAt = def.jobAt;
  return crew;
}

Simulation Simulation::NewVoyage(const Catalog& catalog) {
  Simulation sim;
  sim.catalog_ = catalog;
  sim.rules_ = catalog.rules;

  // ---- crew --------------------------------------------------------------
  for (const CrewDefinition& def : catalog.startingCrew) {
    sim.crew_.push_back(MakeCrew(def));
  }

  // ---- hull readouts -----------------------------------------------------
  for (const StatDefinition& def : catalog.startingStats) {
    Stat stat;
    stat.id = def.id;
    stat.label = def.label;
    stat.value = def.value;
    stat.unit = def.unit;
    stat.colour = def.colour;
    sim.stats_.push_back(stat);
  }

  // ---- the canister rack -------------------------------------------------
  sim.life_.canisters = 6;
  sim.life_.capacity = catalog.rules.life.capacity;
  sim.life_.slots = {{"s0", "Slot 1", "empty"}, {"s1", "Slot 2", "empty"},
                     {"s2", "Slot 3", "empty"}};

  // ---- the ration counter and the hold ----------------------------------
  sim.mess_.rack = catalog.rules.mess.rackStart;
  sim.mess_.capacity = catalog.rules.mess.capacity;
  sim.cargo_.capacity = sim.ShipCargoCapacity("wren", 0);

  // ---- the hull the crew are standing in ---------------------------------
  {
    const DeckPlan& plan = catalog.wren;
    for (const Room& room : plan.rooms) { sim.rooms_.push_back(room); }
    ShipDef ship;
    ship.fields = {{"name", "SC Wren"}, {"berths", "6"}};
    sim.ship_ = ship;
    for (const InfoDefinition& info : catalog.infoDefs) {
      sim.infos_.push_back(InfoRow{info.id, info.label, info.value});
    }
  }

  // ---- the chart ---------------------------------------------------------
  sim.map_ = MapView();
  const MapGeometry& geometry = catalog.map;
  sim.map_.w = geometry.w;
  sim.map_.h = geometry.h;
  sim.map_.cx = geometry.cx;
  sim.map_.cy = geometry.cy;
  sim.map_.vw = geometry.w;
  sim.map_.vh = geometry.h;
  sim.map_.zoom = geometry.zoom;
  sim.map_.zmin = geometry.zmin;
  sim.map_.zmax = geometry.zmax;
  sim.map_.zstep = geometry.zstep;
  sim.map_.grid = geometry.grid;
  sim.map_.vpy = geometry.vpy;
  sim.map_.rungs = geometry.rungs;
  sim.map_.rungPow = geometry.rungPow;
  sim.map_.pxPerAction = geometry.pxPerAction;
  sim.map_.lyPerAction = geometry.lyPerAction;
  for (const std::string& layer : catalog.layerOrder) { sim.views_[layer] = LayerView(); }

  // ---- the hold ----------------------------------------------------------
  for (const Good& good : catalog.startingCargo) {
    GoodsLot lot;
    lot.id = good.id;
    lot.name = good.name;
    lot.kind = good.kind;
    lot.vol = good.vol;
    lot.value = good.value;
    lot.risk = good.risk;
    sim.goods_.push_back(lot);
  }
  for (const SurveyRecordDef& def : catalog.startingSurvey) {
    SurveyRecord record;
    record.id = def.id;
    record.name = def.name;
    record.src = def.src;
    record.value = def.value;
    record.state = def.state;
    sim.survey_.push_back(record);
  }

  for (const Catalog::JobDefinition& def : catalog.jobDefs) {
    JobDef job;
    job.id = def.id;
    job.label = def.label;
    job.unique = def.unique;
    job.wage = def.wage;
    job.baseAt = def.baseAt;
    job.room = def.room;
    sim.jobs_.push_back(job);
  }

  // ---- STATE -------------------------------------------------------------
  GameState& state = sim.state_;
  state.medkits = 2;
  state.shipClass = "wren";
  state.engineCondition = 100;
  state.wearTick = 0;
  state.filterLife = 0;
  state.airHold = catalog.rules.air.holdMax;
  state.rackWarned = false;
  state.rackWarnedMess = false;
  state.crewSel = "rell";
  state.crewTick = 0;
  state.shiplets = 340;
  state.payTick = 0;
  state.docked = false;
  state.fuelCells = 12;
  state.repairKits = 3;
  state.lotSerial = 0;
  state.gameOver.clear();
  state.contracts["arrival"] = "active";

  // The archive begins a new watch; the first memory is authored, not earned.
  Memory first;
  first.id = 1;
  first.kind = "Watch";
  first.text = "The ship's memory archive begins a new watch.";
  first.at = 0;
  first.processed = false;
  state.sim.memories.push_back(first);

  return sim;
}

void Simulation::PerformAction(const std::string& detail) {
  if (!state_.gameOver.empty()) { return; }

  actionCounter_ += 1;

  // Cooldowns share the same discrete heartbeat as every other countdown: a
  // committed action ticks the existing ones once, before the world moves.
  TickActionCooldowns();

  TickWorld();

  // Only a real commitment carries a detail line. The tally lives in the
  // status bar; the feed is for things that happened.
  if (!detail.empty()) { LogLine(detail + "."); }

  if (state_.gameOver.empty()) { CheckCommendations(); }
}

void Simulation::TickWorld() {
  // The order below is normative; see the header comment.
  WearEngine();
  if (static_cast<long long>(actionCounter_) % 20 == 0) { ClampStat("energy", -1); }
  TickFilters();
  TickCrew();
  TickCleaner();
  CheckLoss();
  if (!state_.gameOver.empty()) { return; }
  TickPayroll();
  TickOngoing();
  AutoDesires();
  AutoMaint();
  AutoFilters();
  AutoMess();
  TickIncidents();
  TickSim();
  CheckLoss();
}

void Simulation::ProAction(double n) {
  AdvanceTasks(n);
  // repaintQueue() in the reference: presentation only, nothing to do here.
}

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

double Simulation::StatValue(const std::string& id) const {
  for (const Stat& stat : stats_) { if (stat.id == id) { return stat.value; } }
  return 0.0;
}

Stat* Simulation::FindStat(const std::string& id) {
  for (Stat& stat : stats_) { if (stat.id == id) { return &stat; } }
  return nullptr;
}

void Simulation::ClampStat(const std::string& id, double delta) {
  Stat* stat = FindStat(id);
  if (stat == nullptr) { return; }
  const double before = stat->value;
  stat->value = std::max(0.0, std::min(100.0, stat->value + delta));

  if (id == "energy" && before > 0 && stat->value == 0) {
    // Power loss takes the cabin's reserve with it: the reference resets the
    // air hold so the crew get the full countdown rather than an instant fail.
    if (state_.filterLife > 0) { state_.airHold = rules_.air.holdMax; }
    LogLine("Power exhausted. The filter bank has stopped; restore energy to restart it.");
  } else if (id == "energy" && before == 0 && stat->value > 0) {
    LogLine("Power restored. Any charged filter bank resumes without another bonus.");
  }
}

void Simulation::LogLine(const std::string& text) {
  // The reference stamps lines with the wall clock. The port keeps the field
  // (saves carry it) but leaves it empty: nothing may depend on real time.
  state_.feed.push_back(FeedLine{"", text});
  if (state_.feed.size() > 200) { state_.feed.erase(state_.feed.begin()); }
}

CrewMember* Simulation::FindCrew(const std::string& id) {
  for (CrewMember& crew : crew_) { if (crew.id == id) { return &crew; } }
  return nullptr;
}

const CrewMember* Simulation::FindCrew(const std::string& id) const {
  for (const CrewMember& crew : crew_) { if (crew.id == id) { return &crew; } }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Commands (Phase 1A). Each mirrors the reference function named beside it.
// ---------------------------------------------------------------------------

bool Simulation::StartCanisterInstall(int slot) {
  // startInstall(e, i): the action is spent FIRST, then the work is registered,
  // so the tick caused by the action cannot eat a step of that work.
  if (slot < 0 || slot >= static_cast<int>(life_.slots.size())) { return false; }
  CanisterSlot& target = life_.slots[static_cast<std::size_t>(slot)];
  if (target.state != "empty") { return false; }
  if (FindOngoing("o2s" + std::to_string(slot)) != nullptr) { return false; }
  if (life_.canisters <= 0) {
    LogLine("No canisters left in the rack.");
    return false;
  }
  life_.canisters -= 1;

  o2Claim_ = true;
  PerformAction("Canister carried to " + target.name);
  o2Claim_ = false;

  if (!state_.gameOver.empty()) { return true; }

  Task* task = AddOngoing("o2s" + std::to_string(slot), "Seating - " + target.name, 2,
                          "o2slot", target.id);
  if (task != nullptr) { task->slot = slot; }
  return true;
}

bool Simulation::StartFilterBank() {
  // turnOnFilter(e): one ordinary action to throw the switch, then ten actions
  // of progress that cost nothing and wear nothing.
  if (!BankReady() || BankRunning() || StatValue("energy") <= 0) { return false; }

  o2Claim_ = true;
  PerformAction("Filter bank started");
  o2Claim_ = false;

  if (StatValue("energy") <= 0) {
    LogLine("The filter switch needs power.");
    return true;
  }
  RunBank("Filters activated. Air purified. +10 Pro-Action.");
  return true;
}

bool Simulation::StartMaintenanceSelf() {
  // startMaint(e, "maint", "the commander"): refuses a perfect engine, refuses
  // a second spanner, charges one action, then registers the work.
  if (state_.engineCondition >= 100.0) {
    LogLine("Maintenance refused - the drive is already at full condition.");
    return false;
  }
  if (MaintRunningAny()) { return false; }

  const double cost = MaintCostActions();
  maintClaim_ = true;
  PerformAction("Engine maintenance begun by the commander");
  maintClaim_ = false;

  if (!state_.gameOver.empty()) { return false; }

  Task* task = AddOngoing("maint", "Maintenance - the commander", cost, "maint", "engine");
  if (task != nullptr) { task->by = "the commander"; }
  return true;
}

bool Simulation::StartCookingSelf() {
  // startCook(e, "cook", "the commander"): refuses a full rack, refuses a
  // second pot, commits at rate 1 - the commander is not a trained cook.
  if (mess_.rack >= mess_.capacity) {
    LogLine("Cooking refused - the rack is already full.");
    return false;
  }
  if (FindOngoing("cook") != nullptr || FindOngoing("project-food") != nullptr) {
    return false;
  }

  const CrewMember* cook = Cook();
  const double rate = 1.0;  // the commander's own rate, never the cook's
  (void)cook;
  const double cost = CrewCost(std::ceil((mess_.capacity - mess_.rack) / rate));

  cookClaim_ = true;
  PerformAction("Cooking begun by the commander");
  cookClaim_ = false;

  if (!state_.gameOver.empty()) { return false; }

  Task* task = AddOngoing("cook", "Cooking - the commander", cost, "cook", "mess");
  if (task != nullptr) {
    task->by = "the commander";
    task->rate = rate;
  }
  return true;
}

bool Simulation::ApplyJob(const std::string& crewId, const std::string& jobId) {
  // applyJob(c, jid): the whole of taking, changing or giving up a job, in the
  // reference's order, down to who hears about it and when.
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr) { return false; }

  const JobDef* job = nullptr;
  for (const JobDef& candidate : jobs_) {
    if (candidate.id == jobId) { job = &candidate; }
  }
  if (!jobId.empty() && job == nullptr) { return false; }

  if (job != nullptr && job->unique) {
    for (CrewMember& other : crew_) {
      if (other.id == crew->id || other.job != jobId) { continue; }
      other.job.clear();
      LogLine(other.name + " was relieved of the " + Lower(job->label) + " job.");
    }
  }

  crew->job = jobId;
  if (job != nullptr && !job->room.empty()) { crew->room = job->room; }

  if (jobId.empty()) {
    LogLine(crew->name + " has no standing job.");
    return true;
  }

  if (crew->skill.empty()) { crew->skill = "Untrained"; }
  crew->jobAt = job->baseAt;
  LogLine(crew->name + " now holds the " + Lower(job->label) + " job.");
  return true;
}

std::vector<std::string> Simulation::FeedText() const {
  std::vector<std::string> out;
  out.reserve(state_.feed.size());
  for (const FeedLine& line : state_.feed) { out.push_back(line.text); }
  return out;
}

}  // namespace ship

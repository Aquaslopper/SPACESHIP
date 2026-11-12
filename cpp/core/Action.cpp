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

#include "data/ChartExpansion.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
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
  return NewVoyage(catalog, catalog.chartSettings, std::string());
}

Simulation Simulation::NewVoyage(const Catalog& catalog, const ChartSettings& settings,
                                 const std::string& startSystemId) {
  Simulation sim;
  if (!catalog.chartExpanded) { sim.sourceCatalog_ = catalog; }
  else { sim.sourceCatalog_ = catalog; }
  sim.chartSettings_ = settings;
  sim.catalog_ = sim.sourceCatalog_;
  sim.catalog_.chartExpanded = false;
  // Grow the charted galaxy before anything reads the chart. The
  // catalogue is never touched: this works on the voyage's own copy, and a
  // catalogue that has already been grown is left exactly as it is.
  ExpandChart(sim.catalog_, settings);
  sim.rules_ = sim.catalog_.rules;
  sim.scanCost_ = catalog.rules.scanCost;

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
  sim.mess_.rations = catalog.rules.mess.rationStart;
  sim.mess_.rationCapacity = catalog.rules.mess.rationCapacity;
  sim.cargo_.capacity = sim.ShipCargoCapacity("wren", 0);

  // ---- the hull the crew are standing in ---------------------------------
  {
    const DeckPlan& plan = catalog.wren;
    for (const Room& room : plan.rooms) {
      if (StandardRoom(room.id)) sim.rooms_.push_back(room);
    }
    ShipDef ship;
    auto fields = catalog.shipFields.find("wren");
    if (fields != catalog.shipFields.end()) { ship.fields = fields->second; }
    sim.ship_ = ship;
    for (const InfoDefinition& info : catalog.infoDefs) {
      sim.infos_.push_back(InfoRow{info.id, info.label, info.value});
    }
  }

  // ---- the chart ---------------------------------------------------------
  sim.map_ = MapView();
  sim.map_.zoom = sim.catalog_.map.zoom;
  sim.ApplyChartBoard();
  for (const std::string& layer : catalog.layerOrder) { sim.views_[layer] = LayerView(); }

  // ---- the hold ----------------------------------------------------------
  for (const Good& good : catalog.startingCargo) {
    // Leave room for two 3 m3 salvage lots on the opening crossing. The old
    // starting cargo exceeds the smaller hold; existing saves are untouched.
    constexpr double openingSalvageSpace = 6;
    if (sim.CargoUsed() + good.vol > sim.cargo_.capacity - openingSalvageSpace) continue;
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
    job.cost = def.cost;
    job.learnOn = def.learnOn;
    job.learnSt = def.learnSt;
    job.feeOn = def.feeOn;
    job.feeSt = def.feeSt;
    job.trainStep = def.trainStep;
    job.trainCap = def.trainCap;
    job.unit = def.unit;
    job.cloth = def.cloth;
    job.dark = def.dark;
    sim.jobs_.push_back(job);
  }

  // ---- STATE -------------------------------------------------------------
  GameState& state = sim.state_;
  state.medkits = 2;
  state.shipClass = "wren";
  state.engineCondition = 80;
  state.wearTick = 0;
  state.filterLife = 0;
  state.airHold = catalog.rules.air.holdMax;
  state.rackWarned = false;
  state.rackWarnedMess = false;
  state.crewSel = "rell";
  state.crewTick = 0;
  state.shiplets = 200;
  state.payTick = 0;
  state.docked = false;
  state.fuelCells = 6;
  state.repairKits = 0;
  state.lotSerial = 0;
  state.gameOver.clear();
  state.contracts["arrival"] = "active";

  if (!startSystemId.empty()) {
    const Body* system = sim.catalog_.FindSystem(startSystemId);
    for (const Port& port : sim.catalog_.ports) {
      const Body* body = sim.catalog_.FindBody(port.id);
      if (body == nullptr) { continue; }
      if (sim.catalog_.BodySystem(*body) != startSystemId) { continue; }
      sim.map_.here = port.id;
      sim.state_.docked = true;
      if (system != nullptr) { sim.map_.sector = system->region; }
      sim.map_.layer = "system";
      break;
    }
  }

  return sim;
}

Simulation Simulation::ReferenceVoyage(const Catalog& catalog) {
  Simulation sim = NewVoyage(catalog);
  sim.referenceFit_ = true;
  sim.rooms_ = catalog.wren.rooms;
  sim.cargo_.capacity = sim.ShipCargoCapacity("wren", 0);
  sim.goods_.clear();
  for (const Good& good : catalog.startingCargo) {
    GoodsLot lot;
    lot.id = good.id; lot.name = good.name; lot.kind = good.kind;
    lot.vol = good.vol; lot.value = good.value; lot.risk = good.risk;
    sim.goods_.push_back(lot);
  }
  return sim;
}

void Simulation::RestartVoyage() {
  BeginVoyage(chartSettings_, std::string());
}

void Simulation::BeginVoyage(const ChartSettings& settings, const std::string& startSystemId) {
  const Options options = state_.options;
  const bool motion = state_.reducedMotion;
  const Catalog source = sourceCatalog_.regions.empty() ? catalog_ : sourceCatalog_;
  const bool reference = referenceFit_;
  *this = reference ? Simulation::ReferenceVoyage(source)
                    : Simulation::NewVoyage(source, settings, startSystemId);
  state_.options = options;
  state_.reducedMotion = motion;
}

void Simulation::PerformAction(const std::string& detail) {
  if (!state_.gameOver.empty()) { return; }

  actionCounter_ += 1;

  // One committed click moves the click time once, before the world moves: the
  // button that paid loses a step, every button it did not pay from gains one.
  TickButtonCharges();

  TickWorld();
  if (state_.gameOver.empty()) { StepCargoMix(); }

  // Only a real commitment carries a detail line. The tally lives in the
  // status bar; the feed is for things that happened.
  if (!detail.empty()) { LogLine(detail + "."); }

  if (state_.gameOver.empty()) { CheckCommendations(); }
}

void Simulation::FreeCommit(const std::string& detail) {
  // The counter, the yard and the hiring hall trade in shiplets alone: nothing
  // here advances the clock, so nothing here wears the engine, feeds the crew,
  // moves a task or starts a countdown. A commendation a purchase earns is
  // still handed over on the spot, as PerformAction would.
  if (!state_.gameOver.empty()) { return; }
  if (!detail.empty()) { LogLine(detail + "."); }
  CheckCommendations();
}

void Simulation::TickWorld() {
  // The order below is normative; see the header comment.
  WearEngine();
  if (static_cast<long long>(actionCounter_) % 4 == 0) { ClampStat("energy", -1); }
  TickFilters();
  TickCrew();
  TickCleaner();
  CheckLoss();
  if (!state_.gameOver.empty()) { return; }
  TickPayroll();
  TickOngoing();
  TickIncidents();
  CheckLoss();
}

void Simulation::ProAction(double n) {
  AdvanceTasks(n);
  for (auto& entry : state_.charges) {
    Charge& charge = entry.second;
    charge.left = std::min(charge.volume, charge.left + n);
  }
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
  // Cosmetic local-clock information, deliberately excluded from parity traces.
  // It never advances the action clock or any simulation countdown.
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char stamp[9]{};
  std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
  state_.feed.push_back(FeedLine{stamp, text});
  ++feedRevision_;
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

bool Simulation::StartCanisterInstall(int slot, const std::string& by,
                                     bool spendsAction) {
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

  const std::string who = by.empty() ? "the commander" : by;
  if (spendsAction) {
    o2Claim_ = true;
    PerformAction(by.empty() ? "Canister carried to " + target.name
                             : by + " carried a canister to " + target.name);
    o2Claim_ = false;
    if (!state_.gameOver.empty()) { return true; }
  } else {
    // A hand's own press: it fills its own line and starts the work, but the
    // click that carries the ship forward is the player's, not his.
    LogLine(who + " carried a canister to " + target.name);
  }

  // The same press, made by a hand: it works at that hand's rate, not at the
  // commander's.
  const double length = by.empty() ? 2.0 : CrewCost(2.0);
  Task* task = AddOngoing("o2s" + std::to_string(slot), "Seating - " + target.name, length,
                          "o2slot", target.id);
  if (task != nullptr) {
    task->slot = slot;
    task->by = who;
  }
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

bool Simulation::StartCookingSelf(const std::string& by, bool spendsAction) {
  // Cooking a batch spends one dry ration up front; up to foodPerRation cooked
  // food lands together when the fixed foodPerRation-action timer completes.
  // Refuse a full counter or an empty larder.
  if (mess_.rack >= mess_.capacity) {
    LogLine("Cooking refused - the galley counter is already full.");
    return false;
  }
  if (mess_.rations < 1.0) {
    LogLine("Cooking refused - there are no dry rations in the larder.");
    return false;
  }
  if (FindOngoing("cook") != nullptr || FindOngoing("project-food") != nullptr) {
    return false;
  }

  // The green bar fills for foodPerRation actions; cooked food lands together
  // when the batch finishes, not as it runs.
  const double cost = rules_.mess.foodPerRation;

  const std::string who = by.empty() ? "the commander" : by;
  if (spendsAction) {
    cookClaim_ = true;
    PerformAction(by.empty() ? "Cooking begun by the commander" : "Cooking begun by " + by);
    cookClaim_ = false;
    if (!state_.gameOver.empty()) { return false; }
  } else {
    LogLine("Cooking begun by " + who);
  }

  Task* task = AddOngoing("cook", "Cooking - " + who, cost, "cook", "mess");
  if (task != nullptr) {
    task->by = who;
    mess_.rations -= 1.0;  // the ration is spent when the batch starts
    // Carrying on to a full rack is allowed: one dry ration per batch, until
    // the cooked-food counter is stocked.
    cookRestock_ = true;
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

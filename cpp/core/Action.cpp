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
  state.crewSel = sim.crew_.empty() ? std::string() : sim.crew_.front().id;
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

  TickButtonCharges();

  TickWorld();
  if (state_.gameOver.empty()) { StepCargoMix(); }

  if (!detail.empty()) { LogLine(detail + "."); }

  if (state_.gameOver.empty()) { CheckCommendations(); }
}

void Simulation::FreeCommit(const std::string& detail) {
  if (!state_.gameOver.empty()) { return; }
  if (!detail.empty()) { LogLine(detail + "."); }
  CheckCommendations();
}

void Simulation::TickWorld() {
  WearEngine();
   // A tank under 40% drags the energy down with it: the drive keeps drawing on
  // a reserve that is no longer there.
  const double energyEvery = StatValue("fuel") < 40.0 ? 2.0 : 4.0;
  if (std::fmod(actionCounter_, energyEvery) == 0.0) { ClampStat("energy", -1); }
  TickFilters();
  TickCrew();
  TickCleaner();
  CheckLoss();
  if (!state_.gameOver.empty()) { return; }
  TickPayroll();
  TickOngoing();
  AutoMaint();
  AutoFilters();
  AutoMess();
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
    // The bank dies with the power, not with its charge: the cabin gets the
    // same hold it gets when a bank runs dry.
    state_.airHold = rules_.air.holdMax;
    LogLine("Power exhausted. The filter bank has stopped; restore energy to restart it.");
  } else if (id == "energy" && before == 0 && stat->value > 0) {
    LogLine("Power restored. Any charged filter bank resumes without another bonus.");
  }
}

void Simulation::LogLine(const std::string& text) {
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

bool Simulation::StartCanisterInstall(int slot, const std::string& by,
                                     bool spendsAction) {
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
    LogLine(who + " carried a canister to " + target.name);
  }

  const double length = 2.0;  // by.empty() ? 2.0 : CrewCost(2.0);
  Task* task = AddOngoing("o2s" + std::to_string(slot), "Seating - " + target.name, length,
                          "o2slot", target.id);
  if (task != nullptr) {
    task->slot = slot;
    task->by = who;
  }
  return true;
}

bool Simulation::StartFilterBank() {
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
  if (mess_.rack >= mess_.capacity) {
    LogLine("Cooking refused - the galley counter is already full.");
    return false;
  }
  if (mess_.rations < 1.0) {
    LogLine("Cooking refused - there are no dry rations in the larder.");
    return false;
  }
  if (FindOngoing("cook") != nullptr /* || FindOngoing("project-food") != nullptr */) {
    return false;
  }

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
    mess_.rations -= 1.0;
    if (!spendsAction) { cookRestock_ = true; }
  }
  return true;
}

bool Simulation::ApplyJob(const std::string& crewId, const std::string& jobId) {
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr) { return false; }

    const JobDef* job = FindJobDef(jobId);
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

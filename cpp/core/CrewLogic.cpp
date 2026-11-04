// ============================================================================
//  CrewLogic.cpp — the people, one action at a time.
//  ---------------------------------------------------------------------------
//  Nobody is harmed quickly. Bad air takes a point of health every few actions;
//  an untrained hand holding a job loses heart slowly; hunger advances on each
//  crew member's own staggered clock. Wages are paid on the same counter and
//  the dearest job is paid first, because those are the jobs keeping everybody
//  breathing.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ship {

void Simulation::TickCrew() {
  const AirStage& air = AirSpec();
  state_.crewTick += 1;

  // ---- the air gets into people -----------------------------------------
  if (air.harm > 0 && std::fmod(state_.crewTick, air.harm) == 0) {
    for (CrewMember& crew : crew_) { crew.health = std::max(0.0, crew.health - 1); }
    LogLine("The air is telling on the crew.");
  }

  // ---- an untrained hand holding a job loses heart -----------------------
  if (std::fmod(state_.crewTick, 10.0) == 0) {
    for (CrewMember& crew : crew_) {
      if (!crew.job.empty() && (crew.skill == "Untrained" || crew.skill.empty())) {
        crew.morale = std::max(0.0, crew.morale - 1);
      }
      crew.fatigue = !crew.job.empty() ? std::min(100.0, crew.fatigue + 1)
                                       : std::max(0.0, crew.fatigue - 2);
    }
  }

  // ---- hunger, each on their own staggered clock -------------------------
  for (CrewMember& crew : crew_) {
    crew.hungerTick += 1;
    if (crew.hungerTick < rules_.mess.every) { continue; }
    crew.hungerTick = 0;
    if (mess_.rack > 0) {
      mess_.rack -= 1;
      crew.hunger = std::max(0.0, crew.hunger - 40);
      state_.rackWarnedMess = false;
    } else {
      crew.hunger = std::min(100.0, crew.hunger + 15);
      if (!state_.rackWarnedMess) {
        LogLine("The ration rack is empty. The crew are going hungry.");
        state_.rackWarnedMess = true;
      }
    }
  }

  // ---- starvation, slow, same law as bad air -----------------------------
  if (std::fmod(state_.crewTick, rules_.mess.starveEvery) == 0) {
    for (CrewMember& crew : crew_) {
      if (crew.hunger >= rules_.mess.starveAt) {
        crew.health = std::max(0.0, crew.health - 1);
      }
    }
  }
}

double Simulation::CleanerInterval(const CrewMember& crew) const {
  return std::max(20.0, 40.0 - 4.0 * std::min(5.0, crew.jobAt));
}

void Simulation::TickCleaner() {
  const CrewMember* best = nullptr;
  for (const CrewMember& crew : crew_) {
    if (crew.job != "cleaner" || CrewBusy(crew) != nullptr) { continue; }
    if (best == nullptr || CleanerInterval(crew) < CleanerInterval(*best)) { best = &crew; }
  }
  if (best == nullptr) { return; }
  // One interval for the whole ship: only the best available cleaner counts.
  if (std::fmod(actionCounter_, CleanerInterval(*best)) != 0) { return; }
  for (CrewMember& crew : crew_) { crew.morale = std::min(100.0, crew.morale + 1); }
}

double Simulation::WageOf(const CrewMember& crew) const {
  if (crew.job.empty()) { return 0; }
  for (const JobDef& job : jobs_) {
    if (job.id == crew.job) { return job.wage; }
  }
  return 0;
}

void Simulation::LayOff(CrewMember& crew, const std::string& why) {
  if (crew.job.empty()) { return; }
  std::string label;
  for (const JobDef& job : jobs_) {
    if (job.id == crew.job) { label = job.label; }
  }
  std::transform(label.begin(), label.end(), label.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  crew.job.clear();
  crew.morale = std::max(0.0, crew.morale - rules_.pay.hit);
  crew.activity = "Idle in the corridor";
  LogLine(crew.name + " - " + why + " - is off the " + label + " job.");
  SimRemember("Work", crew.name + " left the " + label + " job: " + why + ".", crew.id);
}

void Simulation::TickPayroll() {
  state_.payTick += 1;
  if (state_.payTick < rules_.pay.every) { return; }
  state_.payTick = 0;

  std::vector<CrewMember*> owed;
  for (CrewMember& crew : crew_) {
    if (WageOf(crew) > 0) { owed.push_back(&crew); }
  }
  if (owed.empty()) {
    LogLine("Payday. Nobody on the payroll - nothing owed.");
    return;
  }

  // Insertion sort, dearest first. Stable for equal wages, as in the reference.
  for (std::size_t i = 1; i < owed.size(); ++i) {
    CrewMember* current = owed[i];
    std::size_t j = i;
    while (j > 0 && WageOf(*owed[j - 1]) < WageOf(*current)) {
      owed[j] = owed[j - 1];
      --j;
    }
    owed[j] = current;
  }

  double paid = 0, spent = 0, cut = 0;
  for (CrewMember* crew : owed) {
    const double wage = WageOf(*crew);
    if (state_.shiplets >= wage) {
      state_.shiplets -= wage;
      crew->wallet += wage;
      spent += wage;
      paid += 1;
    } else {
      LayOff(*crew, "could not be paid");
      cut += 1;
    }
  }

  LogLine("Payday. " + js::NumToString(spent) + " sh to " + js::NumToString(paid) +
          " hand" + (paid == 1 ? "" : "s") + ". " + js::NumToString(state_.shiplets) +
          " sh left.");
  if (cut > 0) {
    LogLine("The purse came up short. " + js::NumToString(cut) + " hand" +
            (cut == 1 ? " is" : "s are") + " off the payroll.");
  }
}

double Simulation::EffectiveJobAt(const CrewMember& crew) const {
  const double value = crew.jobAt - AirSpec().jobDrop;
  return value < 5 ? 5 : value;
}

double Simulation::MaintCost() const {
  // 1 action of work per 10% missing, rounded up.
  return js::Ceil((100.0 - state_.engineCondition) / rules_.engine.perAction);
}

bool Simulation::MaintRunning() const {
  return FindOngoing("maint") != nullptr || FindOngoing("maint-eng") != nullptr ||
         FindOngoing("project-cooling") != nullptr;
}

CrewMember* Simulation::AssignedEngineer() {
  for (CrewMember& crew : crew_) {
    if (crew.job == "maintenance") { return &crew; }
  }
  return nullptr;
}

CrewMember* Simulation::AssignedFilterHand() {
  for (CrewMember& crew : crew_) {
    if (crew.job == "filters") { return &crew; }
  }
  return nullptr;
}

CrewMember* Simulation::AssignedCook() {
  for (CrewMember& crew : crew_) {
    if (crew.job == "kitchen") { return &crew; }
  }
  return nullptr;
}

const Task* Simulation::CrewBusy(const CrewMember& crew) const {
  if (crewClaims_.count(crew.id) != 0) {
    // A claimed hand is mid-command; report "busy" without inventing a task.
    static const Task claimed = [] {
      Task task;
      task.id = "claimed";
      task.label = "Working";
      task.total = 1;
      task.left = 1;
      task.state = "running";
      return task;
    }();
    return &claimed;
  }
  if (const Task* doc = FindOngoing("doc-" + crew.id)) { return doc; }
  if (const Task* social = FindOngoing("project-social")) {
    if (social->who == crew.id || social->other == crew.id) { return social; }
  }
  if (const Task* own = FindOngoing("cw-" + crew.id)) { return own; }
  for (const Task& task : state_.ongoing) {
    if (task.state == "running" && task.by == crew.name) { return &task; }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Automation: the three jobs that act on their own initiative.
// ---------------------------------------------------------------------------

void Simulation::AutoMaint() {
  CrewMember* worker = AssignedEngineer();
  if (maintClaim_ || projectClaim_ == "cooling" || FindOngoing("project-cooling") != nullptr) {
    return;
  }
  if (worker == nullptr || MaintRunning()) { return; }
  if (CrewBusy(*worker) != nullptr) { return; }
  if (state_.engineCondition > EffectiveJobAt(*worker)) { return; }
  if (state_.engineCondition >= 100) { return; }

  Task* task = AddOngoing("maint-eng", "Maintenance - " + worker->name,
                          CrewCost(MaintCost()), "maint", "engine");
  if (task != nullptr) { task->by = worker->name; }
  LogLine(worker->name + " has started maintenance without being asked.");
}

void Simulation::AutoFilters() {
  CrewMember* worker = AssignedFilterHand();
  if (o2Claim_ || state_.filterLife > 0 || StatValue("energy") <= 0) { return; }
  if (worker == nullptr || BankRunning()) { return; }
  if (CrewBusy(*worker) != nullptr) { return; }
  if (AirStageIndex() > static_cast<int>(worker->jobAt)) { return; }

  if (BankReady()) {
    RunBank(worker->name + " started the filter bank. Air purified. +10 Pro-Action.");
    return;
  }
  if (life_.canisters <= 0) {
    if (!state_.rackWarned) {
      LogLine(worker->name + " went for a canister. The rack is empty.");
      state_.rackWarned = true;
    }
    return;
  }
  state_.rackWarned = false;

  // One slot at a time, and only if he is not already on one.
  for (std::size_t i = 0; i < life_.slots.size(); ++i) {
    if (FindOngoing("o2s" + std::to_string(i)) != nullptr) { return; }
  }
  for (std::size_t i = 0; i < life_.slots.size(); ++i) {
    CanisterSlot& slot = life_.slots[i];
    if (slot.state != "empty") { continue; }
    life_.canisters -= 1;
    Task* task = AddOngoing("o2s" + std::to_string(i), "Seating - " + slot.name,
                            CrewCost(2), "o2slot", slot.id);
    if (task != nullptr) {
      task->slot = static_cast<double>(i);
      task->by = worker->name;
    }
    LogLine(worker->name + " is seating a canister in " + slot.name + ".");
    return;
  }
}

void Simulation::AutoMess() {
  CrewMember* worker = AssignedCook();
  if (cookClaim_ || projectClaim_ == "food" || FindOngoing("project-food") != nullptr) { return; }
  if (worker == nullptr || FindOngoing("cook") != nullptr) { return; }
  if (CrewBusy(*worker) != nullptr) { return; }
  if (mess_.rack > js::Floor(mess_.capacity / 2.0)) { return; }
  if (mess_.rack >= mess_.capacity) { return; }

  const double rate = worker->jobAt;
  const double cost = CrewCost(js::Ceil((mess_.capacity - mess_.rack) / rate));
  Task* task = AddOngoing("cook", "Cooking - " + worker->name, cost, "cook", "mess");
  if (task != nullptr) {
    task->by = worker->name;
    task->rate = rate;
  }
  LogLine(worker->name + " started cooking without being asked.");
}

// ---------------------------------------------------------------------------
// Loss
// ---------------------------------------------------------------------------

void Simulation::CheckLoss() {
  bool lost = false;
  for (std::size_t index = crew_.size(); index-- > 0;) {
    CrewMember& crew = crew_[index];
    if (crew.health > 0) { continue; }

    LogLine(crew.name + " has died.");
    state_.pendingOrders.erase(crew.id);
    ClearOngoing("cw-" + crew.id);
    ClearOngoing("doc-" + crew.id);

    for (std::size_t taskIndex = state_.ongoing.size(); taskIndex-- > 0;) {
      const Task task = state_.ongoing[taskIndex];
      if (task.type == "shipProject" && (task.who == crew.id || task.other == crew.id)) {
        ClearOngoing(task.id);
        LogLine("Crew work was interrupted by the loss.");
        continue;
      }
      if (task.by == crew.name) {
        if (task.type == "o2slot") {
          life_.canisters = std::min(life_.capacity, life_.canisters + 1);
        }
        ClearOngoing(task.id);
      }
    }
    crew_.erase(crew_.begin() + static_cast<std::ptrdiff_t>(index));
    lost = true;
  }

  if (!state_.gameOver.empty()) { return; }
  if (StatValue("hull") <= 0 || crew_.empty()) {
    state_.gameOver = StatValue("hull") <= 0 ? "The hull has failed." : "The crew have been lost.";
    LogLine("Ship lost. " + state_.gameOver);
  }
  (void)lost;
}

}  // namespace ship

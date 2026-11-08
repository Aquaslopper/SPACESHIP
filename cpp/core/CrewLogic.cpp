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
  // Everything here is counted in clicks, never in seconds. A hand gains
  // `hungerPerMeal` (30) over its own `every` (25) clicks - a click at a time,
  // so the bar creeps up rather than jumping - and then goes to eat. One food
  // drains `hungerPerFood` (10) of hunger, so a meal is 30 / 10 = 3 food: three
  // takes a hand from 50% down to 20%, which is the whole point of the number.
  //
  // THE MEAL IS SIZED BY THE CLOCK, NOT BY THE COUNTER. A hand eats the 3 food
  // its 25 clicks earned and stops; it does not clear the shelf. That is what
  // makes the staggering work: four hands eat 12 food a round, the counter
  // holds 24 (two rounds), and a cook who restarts at 10 always has food on the
  // shelf while the next batch runs. What the counter DOES decide is whether
  // the meal can be paid for: with less than 3 food on it, the hand eats what
  // is there and carries the rest of its hunger into the next round.
  const double risePerClick = rules_.mess.hungerPerMeal / std::max(1.0, rules_.mess.every);
  const double mealFood = rules_.mess.hungerPerMeal / std::max(1.0, rules_.mess.hungerPerFood);

  for (CrewMember& crew : crew_) {
    crew.hungerTick += 1;
    crew.hunger = std::min(100.0, crew.hunger + risePerClick);
    if (crew.hungerTick < rules_.mess.every) { continue; }
    crew.hungerTick = 0;

    // What the meal is worth to this hand: three food, but never more than it
    // takes to reach the floor, and never more than is on the counter. Three
    // bounds, one number, so a hand can neither eat the shelf empty nor go
    // under five.
    const double spare = crew.hunger - rules_.mess.hungerFloor;
    double wanted = 0.0;
    if (spare > 0.0) {
      wanted = std::min(mealFood, std::ceil(spare / rules_.mess.hungerPerFood));
    }
    const double ate = std::min(wanted, mess_.rack);
    if (ate > 0.0) {
      mess_.rack -= ate;
      crew.hunger = std::max(rules_.mess.hungerFloor,
                             crew.hunger - ate * rules_.mess.hungerPerFood);
    }

    if (ate > 0.0) {
      state_.rackWarnedMess = false;
    } else if (mess_.rack <= 0 && !state_.rackWarnedMess) {
      LogLine("The galley counter is empty. The crew are going hungry.");
      state_.rackWarnedMess = true;
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

const CrewMember* Simulation::Engineer() const {
  return const_cast<Simulation*>(this)->AssignedEngineer();
}

const CrewMember* Simulation::FilterHand() const {
  return const_cast<Simulation*>(this)->AssignedFilterHand();
}

const CrewMember* Simulation::Cook() const {
  return const_cast<Simulation*>(this)->AssignedCook();
}

double Simulation::MaintCostActions() const { return MaintCost(); }

bool Simulation::MaintRunningAny() const { return MaintRunning(); }

double Simulation::CleanerIntervalOf(const CrewMember& crew) const {
  return CleanerInterval(crew);
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
  // The work is priced in the drive's own clicks: it wears a step every
  // wearEvery clicks, so a missing percent costs exactly the clicks that took it.
  const double step = rules_.engine.wearStep > 0 ? rules_.engine.wearStep : 1.0;
  const double clicksPerPercent = rules_.engine.wearEvery / step;
  return js::Ceil((100.0 - state_.engineCondition) * clicksPerPercent);
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

// ---------------------------------------------------------------------------
// The read-only views the crew windows share. None of these moves a clock:
// they are what a panel reads, not what a button does.
// ---------------------------------------------------------------------------
double Simulation::CrewWage(const CrewMember& crew) const { return WageOf(crew); }

double Simulation::PayrollBill() const {
  double total = 0;
  for (const CrewMember& crew : crew_) { total += WageOf(crew); }
  return total;
}

double Simulation::PayrollDue() const {
  const double left = rules_.pay.every - state_.payTick;
  return left < 0 ? 0 : left;
}

std::string Simulation::AirStageWord() const { return AirSpec().word; }

double Simulation::NavigatorFuelDiscount(const CrewMember& crew) const {
  const double at = crew.jobAt == 0 ? 1.0 : crew.jobAt;
  return 4.0 * std::max(1.0, std::min(5.0, at));
}

std::string Simulation::JobThreshold(const CrewMember& crew) const {
  if (crew.job == "maintenance") {
    return js::NumToString(EffectiveJobAt(crew)) + "% condition";
  }
  if (crew.job == "filters") {
    const std::vector<AirStage>& stages = rules_.air.stages;
    const std::size_t index = static_cast<std::size_t>(crew.jobAt);
    return (index < stages.size() ? stages[index].word : AirSpec().word) + " air";
  }
  if (crew.job == "navigation") {
    return "Fuel use -" + js::NumToString(NavigatorFuelDiscount(crew)) + "% when plotting";
  }
  if (crew.job == "cleaner") {
    return "Crew morale +1 every " + js::NumToString(CleanerInterval(crew)) + " clicks";
  }
  if (crew.job == "kitchen") {
    return "1 dry ration -> up to " + js::NumToString(rules_.mess.foodPerRation) +
           " cooked food in " + js::NumToString(rules_.mess.foodPerRation) +
           " clicks";
  }
  return "-";
}

std::string Simulation::CrewWhere(const CrewMember& crew) const {
  const Task* work = CrewBusy(crew);
  if (work != nullptr && work->target == "social") { return "Recreation"; }
  if (FindOngoing("doc-" + crew.id) != nullptr) { return "Medical Bay"; }
  if (crew.job.empty()) { return "Corridor - no job"; }
  for (const Room& room : rooms_) {
    if (room.id == crew.room) { return room.name; }
  }
  return "Corridor";
}

std::string Simulation::SimActivityOf(const CrewMember& crew) const {
  const Task* work = CrewBusy(crew);
  if (work != nullptr && work->target == "social") { return "Sharing a meal"; }
  if (FindOngoing("doc-" + crew.id) != nullptr) { return "In medical care"; }
  if (work != nullptr && work->by == crew.name) { return work->label; }
  return crew.activity;
}

// needKey(): the order of the tests is the order of what hurts.
std::string Simulation::NeedKeyFor(const CrewMember& crew) const {
  if (crew.health < 75) { return "ill"; }
  if (AirStageIndex() <= 1) { return "air"; }
  if (crew.fatigue > 70) { return "tired"; }
  if (crew.hunger > 70) { return "hungry"; }
  if (crew.morale < 60) { return "morale"; }
  if (!crew.job.empty() && (crew.skill.empty() || crew.skill == "Untrained")) {
    return "untrained";
  }
  if (crew.job.empty()) { return "idle"; }
  return "fine";
}

std::string Simulation::NeedLine(const std::string& needKey, int n,
                                 const std::string& jobLabel) const {
  static const std::map<std::string, std::vector<std::string>> kLines = {
      {"ill", {"I am not right. I should see the doc.",
               "Been feeling rough for a while now. Do not make me say it twice.",
               "I can work. I would rather not, but I can work.",
               "Whatever I picked up, it has not let go of me yet."}},
      {"air", {"The air in here is filthy. Sort the filters and I will sort everything else.",
               "Every breath tastes of the last one. How long has the bank been dead?",
               "My head is splitting and it is not the work.",
               "You can smell the ship going stale. That is not supposed to happen."}},
      {"untrained", {"I am doing the {job} work on guesswork. Teach me properly and I will catch things sooner.",
                     "Nobody ever showed me the {job} job. I copy what I saw the last one do.",
                     "I will keep guessing at {job} if you like. It has held so far.",
                     "Send me to learn the {job} work. I would rather know than hope."}},
      {"morale", {"Put me on something else. This job is wearing on me.",
                  "I am not asking for much. Just something different for a while.",
                  "Same station, same hours, same walls. You see the problem.",
                  "I have stopped looking forward to my shifts."}},
      {"tired", {"I need a shift off my feet.",
                 "I have been awake longer than the drive has.",
                 "Give me an hour in the bunk and I will be twice the use."}},
      {"hungry", {"When did we last eat properly?",
                  "Rations again. I am starting to dream about the mess hall.",
                  "Feed the crew, commander. It is cheaper than replacing us."}},
      {"idle", {"Give me something to be responsible for.",
                "I will take any job going. Standing about is worse than work.",
                "You have a spare pair of hands here doing nothing."}},
      {"fine", {"Nothing to report. The {job} work is in hand.",
                "All quiet. I will tell you the moment it is not.",
                "Ship is behaving. Long may it last.",
                "No complaints today, and that is rare enough to mention."}}};

  auto it = kLines.find(needKey);
  if (it == kLines.end() || it->second.empty()) { return std::string(); }
  std::string text = it->second[static_cast<std::size_t>(n) % it->second.size()];
  std::string job = jobLabel.empty() ? std::string("ship") : jobLabel;
  std::transform(job.begin(), job.end(), job.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string token = "{job}";
  for (std::size_t at = text.find(token); at != std::string::npos;
       at = text.find(token, at + job.size())) {
    text.replace(at, token.size(), job);
  }
  return text;
}

std::string Simulation::AskCrew(CrewMember& crew) {
  if (FindOngoing("doc-" + crew.id) != nullptr) { return crew.said; }
  const std::string key = NeedKeyFor(crew);
  if (crew.saidKey != key) {
    crew.saidKey = key;
    crew.saidN = 0;
  } else {
    crew.saidN += 1;
  }
  const JobDef* job = FindJobDef(crew.job);
  crew.said = NeedLine(key, crew.saidN, job == nullptr ? std::string() : job->label);
  return crew.said;
}

bool Simulation::TalkToCrew(const std::string& crewId) {
  if (!state_.gameOver.empty()) { return false; }
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr || crew->health <= 0 || FindOngoing("doc-" + crewId) != nullptr) {
    return false;
  }
  // Capture everything needed before the tick: crew may leave or die during
  // the action. Like other committed changes, apply the morale before it ticks.
  const std::string name = crew->name;
  const std::string said = AskCrew(*crew);
  crew->morale = std::min(100.0, crew->morale + kConversationMorale);
  LogLine(name + ": \"" + said + "\"");
  PerformAction("Spoke with " + name);
  return true;
}

std::string Simulation::SimDriveOf(const CrewMember& crew) const { return SimDrive(crew); }
std::string Simulation::SimAnchorOf(const CrewMember& crew) const { return SimAnchor(crew); }

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
  // The engineer does not run a job of his own: he presses the machine's button,
  // and only once the drive has fallen below the line he is charged to watch.
  CrewMember* worker = AssignedEngineer();
  if (maintClaim_ || projectClaim_ == "cooling" || FindOngoing("project-cooling") != nullptr) {
    return;
  }
  if (worker == nullptr || MaintRunning()) { return; }
  if (CrewBusy(*worker) != nullptr) { return; }
  // How early he takes the button is what his training buys: an untrained hand
  // waits until the drive is under the floor, a trained one presses at his own
  // level, so the ship is kept further from trouble the better he knows it.
  const double line = std::max(rules_.engine.maintBelow, EffectiveJobAt(*worker));
  if (state_.engineCondition >= line) { return; }
  if (!EngineClickReady()) { return; }  // the bar comes back round in its own time

  // His press is this action's click, so it arms the bar and lifts the drive
  // without performing a second action of its own.
  ServiceDrive(worker->name);
}

void Simulation::AutoFilters() {
  CrewMember* worker = AssignedFilterHand();
  if (o2Claim_ || state_.filterLife > 0 || StatValue("energy") <= 0) { return; }
  if (worker == nullptr || BankRunning()) { return; }
  if (CrewBusy(*worker) != nullptr) { return; }

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
  // Two canisters go in as soon as the rack allows; the third waits for the air
  // to fall to the stage his training lets him wait for. An untrained hand sits
  // on it until the air is already bad, a trained one fills while there is still
  // room to breathe.
  int seated = 0;
  for (const CanisterSlot& slot : life_.slots) {
    if (slot.state == "installed") { ++seated; }
  }
  // His training is what lets him fill while there is still room to breathe: an
  // untrained hand waits for the air to actually fall, a taught one fills as soon
  // as it leaves perfect.
  const int line = std::min(3, static_cast<int>(worker->jobAt));
  if (seated >= 2 && AirStageIndex() > line) { return; }

  // He presses the O2 panel's own control: the same press the commander makes,
  // made by a hand, so it runs at his rate and carries his name.
  for (std::size_t i = 0; i < life_.slots.size(); ++i) {
    if (life_.slots[i].state != "empty") { continue; }
    if (StartCanisterInstall(static_cast<int>(i), worker->name)) {
      LogLine(worker->name + " seated a canister in " + life_.slots[i].name + ".");
    }
    return;
  }
}

void Simulation::AutoMess() {
  CrewMember* worker = AssignedCook();
  if (cookClaim_ || projectClaim_ == "food" || FindOngoing("project-food") != nullptr) { return; }
  if (worker == nullptr || FindOngoing("cook") != nullptr) { return; }
  if (CrewBusy(*worker) != nullptr) { return; }
  // She starts once the counter is under her own line and does not stop until it
  // is full: cooking 1 ration, then another, until the rack is stocked. That line
  // is the untrained one plus a batch per stage of her training, so a taught cook
  // starts a batch earlier and the rack never gets as low.
  const double line = std::min(mess_.capacity,
                               rules_.mess.cookBelow +
                                   rules_.mess.foodPerRation * std::min(3.0, worker->jobAt));
  if (!cookRestock_ && mess_.rack >= line) { return; }
  if (mess_.rack >= mess_.capacity) {
    cookRestock_ = false;
    return;
  }
  if (mess_.rations < 1.0) {
    cookRestock_ = false;
    return;
  }

  // One dry ration yields up to foodPerRation cooked food when the five-click
  // batch finishes. A batch that would overshoot the ceiling is still cooked;
  // the landing is clamped, so the counter ends full rather than past its lid.
  // She presses the galley's own control rather than opening work of her own.
  if (StartCookingSelf(worker->name)) {
    LogLine(worker->name + " started cooking without being asked.");
  }
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

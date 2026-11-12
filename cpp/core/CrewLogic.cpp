#include "core/JsNumber.h"

#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ship {

void Simulation::TickCrew() {
  const AirStage& air = AirSpec();
  state_.crewTick += 1;

  if (air.harm > 0 && std::fmod(state_.crewTick, air.harm) == 0) {
    for (CrewMember& crew : crew_) { crew.health = std::max(0.0, crew.health - 1); }
    LogLine("The air is telling on the crew.");
  }

  if (std::fmod(state_.crewTick, 10.0) == 0) {
    for (CrewMember& crew : crew_) {
      if (!crew.job.empty() && (crew.skill == "Untrained" || crew.skill.empty())) {
        crew.morale = std::max(0.0, crew.morale - 1);
      }
      crew.fatigue = !crew.job.empty() ? std::min(100.0, crew.fatigue + 1)
                                       : std::max(0.0, crew.fatigue - 2);
    }
  }

  const double risePerClick = rules_.mess.hungerPerMeal / std::max(1.0, rules_.mess.every);
  const double mealFood = rules_.mess.hungerPerMeal / std::max(1.0, rules_.mess.hungerPerFood);

  for (CrewMember& crew : crew_) {
    crew.hungerTick += 1;
    crew.hunger = std::min(100.0, crew.hunger + risePerClick);
    if (crew.hungerTick < rules_.mess.every) { continue; }
    crew.hungerTick = 0;

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

double Simulation::CookThreshold(const CrewMember& crew) const {
  const double threshold = std::max(rules_.mess.cookBelow, crew.jobAt);
  return std::min(mess_.capacity - 1.0, threshold);
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

double Simulation::MaintCost() const {
  const double step = rules_.engine.wearStep > 0 ? rules_.engine.wearStep : 1.0;
  const double clicksPerPercent = rules_.engine.wearEvery / step;
  return js::Ceil((100.0 - state_.engineCondition) * clicksPerPercent);
}

bool Simulation::MaintRunning() const {
  return FindOngoing("maint") != nullptr ||
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
    const std::string pace = crew.jobAt >= 60.0
                                 ? "every click"
                                 : (crew.jobAt >= 45.0 ? "every 2 clicks" : "every 3 clicks");
    return js::NumToString(rules_.engine.engineerAt) + "% condition (" + pace + ")";
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
    return "At <= " + js::NumToString(CookThreshold(crew)) +
           " food (1 ration -> " + js::NumToString(rules_.mess.foodPerRation) +
           " food)";
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

std::string Simulation::ActivityOf(const CrewMember& crew) const {
  const Task* work = CrewBusy(crew);
  if (work != nullptr && work->target == "social") { return "Sharing a meal"; }
  if (FindOngoing("doc-" + crew.id) != nullptr) { return "In medical care"; }
  if (work != nullptr && work->by == crew.name) { return work->label; }
  return crew.activity;
}

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
  const std::string name = crew->name;
  const std::string said = AskCrew(*crew);
  if (crew->saidN == 0) {
    crew->morale = std::min(100.0, crew->morale + kConversationMorale);
  }
  LogLine(name + ": \"" + said + "\"");
  return true;
}

std::string Simulation::CrewNameOf(const std::string& id) const {
  const CrewMember* crew = FindCrew(id);
  return crew != nullptr ? crew->name : std::string("a former crew member");
}

const Task* Simulation::CrewBusy(const CrewMember& crew) const {
  if (crewClaims_.count(crew.id) != 0) {
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

void Simulation::AutoMaint() {
  CrewMember* worker = AssignedEngineer();
  if (worker == nullptr || state_.engineCondition >= 100.0 || EngineButtonChargeLeft() < 1.0) {
    state_.engineerRun = false;
    return;
  }
  if (state_.engineCondition <= rules_.engine.engineerAt) { state_.engineerRun = true; }
  if (!state_.engineerRun) { return; }

  /*
  if (maintClaim_ || projectClaim_ == "cooling" || FindOngoing("project-cooling") != nullptr) {
    return;
  }
  */
  if (engineClicking_ /* || MaintRunning() */) { return; }
  // if (CrewBusy(*worker) != nullptr) { return; }

  const long long interval =
      worker->jobAt >= 60.0 ? 1 : (worker->jobAt >= 45.0 ? 2 : 3);
  if (static_cast<long long>(actionCounter_) % interval != 0) { return; }

  if (!TrySpendEngineButton()) {
    state_.engineerRun = false;
    return;
  }
  ServiceDrive(worker->name);
  LogLine(worker->name + " pressed the maintenance button.");
  if (state_.engineCondition >= 100.0 || EngineButtonChargeLeft() < 1.0) {
    state_.engineerRun = false;
  }
}

void Simulation::AutoFilters() {
  CrewMember* worker = AssignedFilterHand();
  if (o2Claim_ || state_.filterLife > 0 || StatValue("energy") <= 0) { return; }
  if (worker == nullptr || BankRunning()) { return; }
  // if (CrewBusy(*worker) != nullptr) { return; }

  const int line = std::clamp(static_cast<int>(worker->jobAt), 1, 3);
  if (AirStageIndex() > line) { return; }

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

  for (std::size_t i = 0; i < life_.slots.size(); ++i) {
    if (FindOngoing("o2s" + std::to_string(i)) != nullptr) { return; }
  }
  /*
  int seated = 0;
  for (const CanisterSlot& slot : life_.slots) {
    if (slot.state == "installed") { ++seated; }
  }
  const int line = std::min(3, static_cast<int>(worker->jobAt));
  if (seated >= 2 && AirStageIndex() > line) { return; }
  */

  for (std::size_t i = 0; i < life_.slots.size(); ++i) {
    if (life_.slots[i].state != "empty") { continue; }
    if (StartCanisterInstall(static_cast<int>(i), worker->name, false)) {
      // LogLine(worker->name + " seated a canister in " + life_.slots[i].name + ".");
    }
    return;
  }
}

void Simulation::AutoMess() {
  CrewMember* worker = AssignedCook();
  if (cookClaim_ /* || projectClaim_ == "food" || FindOngoing("project-food") != nullptr */) {
    return;
  }
  if (worker == nullptr) {
    cookRestock_ = false;
    return;
  }
  if (FindOngoing("cook") != nullptr) { return; }
  // if (CrewBusy(*worker) != nullptr) { return; }
  if (!cookRestock_ && mess_.rack > CookThreshold(*worker)) { return; }
  if (mess_.rack >= mess_.capacity) {
    cookRestock_ = false;
    return;
  }
  if (mess_.rations < 1.0) {
    cookRestock_ = false;
    return;
  }

  /*
  const double cost = rules_.mess.foodPerRation;
  Task* task = AddOngoing("cook", "Cooking - " + worker->name, cost, "cook", "mess");
  if (task != nullptr) {
    task->by = worker->name;
    mess_.rations -= 1.0;
  }
  cookRestock_ = true;
  LogLine(worker->name + " started cooking without being asked.");
  */
  StartCookingSelf(worker->name, false);
}

void Simulation::CheckLoss() {
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
  }

  if (!state_.gameOver.empty()) { return; }
  if (StatValue("hull") <= 0 || crew_.empty()) {
    state_.gameOver = StatValue("hull") <= 0 ? "The hull has failed." : "The crew have been lost.";
    LogLine("Ship lost. " + state_.gameOver);
  }
}

}  // namespace ship

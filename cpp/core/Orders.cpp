#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cctype>

namespace ship {

namespace {

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

}  // namespace

const JobDef* Simulation::FindJobDef(const std::string& jobId) const {
  for (const JobDef& job : jobs_) {
    if (job.id == jobId) { return &job; }
  }
  return nullptr;
}

const OrderSheet* Simulation::SheetOf(const CrewMember& crew) const {
  auto it = orderSheets_.find(crew.id);
  return it == orderSheets_.end() ? nullptr : &it->second;
}

std::string Simulation::StagedJob(const CrewMember& crew) const {
  const OrderSheet* sheet = SheetOf(crew);
  if (sheet != nullptr && sheet->hasJob) { return sheet->job; }
  return crew.job;
}

double Simulation::JobCost(const std::string& jobId) const {
  const JobDef* job = FindJobDef(jobId);
  return job == nullptr ? 1.0 : job->cost;
}

double Simulation::OrderCostFor(const std::string& jobId, const std::string& orderId) const {
  const Catalog::JobDefinition* job = nullptr;
  for (const Catalog::JobDefinition& def : catalog_.jobDefs) {
    if (def.id == jobId) { job = &def; }
  }
  if (orderId == "doc") { return 1; }
  if (orderId == "fire") { return 1; }
  if (orderId == "learnOn") { return job != nullptr && job->learnOn > 0 ? job->learnOn : 1; }
  if (orderId == "learnSt") { return job != nullptr && job->learnSt > 0 ? job->learnSt : 2; }
  return 1;
}

double Simulation::OrderFeeFor(const std::string& jobId, const std::string& orderId) const {
  const Catalog::JobDefinition* job = nullptr;
  for (const Catalog::JobDefinition& def : catalog_.jobDefs) {
    if (def.id == jobId) { job = &def; }
  }
  if (job == nullptr) { return 0; }
  if (orderId == "learnOn") { return job->feeOn; }
  if (orderId == "learnSt") { return job->feeSt; }
  return 0;
}

double Simulation::OrderCost(const CrewMember& crew, const std::string& orderId) const {
  return OrderCostFor(StagedJob(crew), orderId);
}

double Simulation::CrewCostOf(const CrewMember& crew) const {
  const OrderSheet* sheet = SheetOf(crew);
  if (sheet == nullptr) { return 0; }
  if (sheet->Has("fire")) { return OrderCostFor(crew.job, "fire"); }
  double total = sheet->hasJob ? JobCost(sheet->job) : 0.0;
  for (const Catalog::OrderDefinition& order : catalog_.orderDefs) {
    if (sheet->Has(order.id)) { total += OrderCostFor(StagedJob(crew), order.id); }
  }
  return total;
}

double Simulation::CrewFeeOf(const CrewMember& crew) const {
  const OrderSheet* sheet = SheetOf(crew);
  if (sheet == nullptr || sheet->Has("fire")) { return 0; }
  double total = 0;
  for (const Catalog::OrderDefinition& order : catalog_.orderDefs) {
    if (sheet->Has(order.id)) { total += OrderFeeFor(StagedJob(crew), order.id); }
  }
  return total;
}

double Simulation::SheetCost(const CrewMember& crew, const OrderSheet& sheet) const {
  double total = sheet.hasJob ? JobCost(sheet.job) : 0.0;
  for (const Catalog::OrderDefinition& order : catalog_.orderDefs) {
    if (sheet.Has(order.id)) {
      total += OrderCostFor(sheet.hasJob ? sheet.job : crew.job, order.id);
    }
  }
  return total < 1 ? 1 : total;
}

double Simulation::OrdersCost() const {
  double total = 0;
  for (const auto& entry : orderSheets_) {
    const CrewMember* crew = FindCrew(entry.first);
    if (crew != nullptr) { total += CrewCostOf(*crew); }
  }
  return total;
}

double Simulation::OrdersFee() const {
  double total = 0;
  for (const auto& entry : orderSheets_) {
    const CrewMember* crew = FindCrew(entry.first);
    if (crew != nullptr) { total += CrewFeeOf(*crew); }
  }
  return total;
}

int Simulation::OrderedCount() const { return static_cast<int>(orderSheets_.size()); }

bool Simulation::HasPendingLayoff() const {
  for (const auto& entry : orderSheets_) {
    if (entry.second.Has("fire")) { return true; }
  }
  return false;
}

bool Simulation::OrdersRunning() const { return FindOngoing("crew-assign") != nullptr; }

bool Simulation::CrewAvailable(const CrewMember& crew) const {
  return CrewBusy(crew) == nullptr;
}

void Simulation::ClearOrders() {
  orderSheets_.clear();
  state_.pendingOrders.clear();
}

bool Simulation::StageJob(const std::string& crewId, const std::string& jobId) {
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr) { return false; }
  if (FindJobDef(jobId) == nullptr) { return false; }

  OrderSheet& sheet = orderSheets_[crewId];
  if (crew->job == jobId) { sheet.hasJob = false; sheet.job.clear(); }
  else { sheet.hasJob = true; sheet.job = jobId; }
  if (sheet.Empty()) { orderSheets_.erase(crewId); }
  state_.pendingOrders[crewId] = !orderSheets_.empty() && orderSheets_.count(crewId) != 0;
  if (!state_.pendingOrders[crewId]) { state_.pendingOrders.erase(crewId); }
  return true;
}

bool Simulation::ToggleOrder(const std::string& crewId, const std::string& orderId) {
  if (orderId == "learnSt" && !state_.docked) { return false; }
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr) { return false; }
  const OrderSheet* existing = SheetOf(*crew);
  if (orderId == "rest" && crew->fatigue < 25 && crew->morale >= 70 &&
      !(existing != nullptr && existing->Has("rest"))) {
    return false;
  }
  bool known = false;
  for (const Catalog::OrderDefinition& order : catalog_.orderDefs) {
    if (order.id == orderId) { known = true; }
  }
  if (!known) { return false; }

  OrderSheet& sheet = orderSheets_[crewId];
  sheet.Set(orderId, !sheet.Has(orderId));
  if (sheet.Empty()) { orderSheets_.erase(crewId); }
  if (orderSheets_.count(crewId) != 0) { state_.pendingOrders[crewId] = true; }
  else { state_.pendingOrders.erase(crewId); }
  return true;
}

bool Simulation::CommitOrders() {
  const double work = OrdersCost();
  if (work <= 0) { return false; }
  const double fee = OrdersFee();
  if (fee > state_.shiplets) {
    if (!HasPendingLayoff()) {
      LogLine("Refused - the sheet needs " + js::NumToString(fee) + " sh in fees and the purse holds " +
              js::NumToString(state_.shiplets) + ".");
      return false;
    }
    std::vector<std::string> dismissed;
    for (const auto& entry : orderSheets_) {
      if (entry.second.Has("fire")) { dismissed.push_back(entry.first); }
    }
    for (const std::string& id : dismissed) {
      if (CrewMember* crew = FindCrew(id)) { FireCrew(*crew); }
      orderSheets_.erase(id);
      state_.pendingOrders.erase(id);
    }
    PerformAction("Crew assignments issued");
    LogLine("Training fees unavailable; other orders remain staged.");
    return true;
  }

  std::map<std::string, OrderSheet> sheet = orderSheets_;
  double remainingWork = work;
  for (auto it = sheet.begin(); it != sheet.end();) {
    if (!it->second.Has("fire")) { ++it; continue; }
    CrewMember* crew = FindCrew(it->first);
    if (crew != nullptr) {
      remainingWork -= CrewCostOf(*crew);
      FireCrew(*crew);
    }
    it = sheet.erase(it);
  }
  orderSheets_.clear();
  state_.pendingOrders.clear();
  state_.shiplets -= fee;

  for (const auto& entry : sheet) { ClearOngoing("cw-" + entry.first); }

  for (const auto& entry : sheet) { crewClaims_[entry.first] = true; }
  PerformAction("Crew assignments issued");
  crewClaims_.clear();
  if (!state_.gameOver.empty()) { return true; }

  if (fee > 0) {
    LogLine("Training fees paid - " + js::NumToString(fee) + " sh. Purse " +
            js::NumToString(state_.shiplets) + " sh.");
  }
  if (!sheet.empty()) {
    QueueOrders(sheet);
    LogLine("Crew assignments updated. " + js::NumToString(remainingWork) + " clicks of work.");
  }
  return true;
}

void Simulation::QueueOrders(const std::map<std::string, OrderSheet>& sheet) {
  for (const auto& entry : sheet) {
    CrewMember* crew = FindCrew(entry.first);
    if (crew == nullptr) { continue; }
    const OrderSheet& orders = entry.second;
    const double size = SheetCost(*crew, orders);
    std::string first = crew->name;
    const std::size_t space = first.find(' ');
    if (space != std::string::npos) { first = first.substr(0, space); }

    Task* task = AddOngoing("cw-" + crew->id, "Orders - " + first, size, "crewJob", crew->id);
    if (task != nullptr) {
      task->who = crew->id;
      task->hasOrders = true;
      task->sheetHasJob = orders.hasJob;
      task->sheetJob = orders.job;
      task->sheetFlags = orders.flags;
      task->rest = orders.Has("rest");
    }
    crew->activity = orders.Has("doc") ? "On the way to the medical bay"
                                       : "Carrying out orders";
  }
}

void Simulation::ApplyJobTo(CrewMember& crew, const std::string& jobId) {
  const JobDef* job = FindJobDef(jobId);
  if (job != nullptr && job->unique) {
    for (CrewMember& other : crew_) {
      if (&other != &crew && other.job == jobId) {
        other.job.clear();
        LogLine(other.name + " was relieved of the " + Lower(job->label) + " job.");
      }
    }
  }
  crew.job = jobId;
  if (job != nullptr && !job->room.empty()) { crew.room = job->room; }
  if (jobId.empty()) {
    LogLine(crew.name + " has no standing job.");
    return;
  }
  if (crew.skill.empty()) { crew.skill = "Untrained"; }
  crew.jobAt = job == nullptr ? 0.0 : job->baseAt;
  LogLine(crew.name + " now holds the " + (job == nullptr ? std::string("job")
                                                          : Lower(job->label)) + " job.");
}

void Simulation::TrainCrew(CrewMember& crew, int steps, const std::string& where) {
  const Catalog::JobDefinition* job = nullptr;
  for (const Catalog::JobDefinition& def : catalog_.jobDefs) {
    if (def.id == crew.job) { job = &def; }
  }
  crew.skill = (crew.skill.empty() || crew.skill == "Untrained") ? "Trained" : "Practised";
  if (job != nullptr && job->trainStep != 0) {
    double current = (crew.jobAt == 0 || crew.jobAt < job->baseAt) ? job->baseAt : crew.jobAt;
    for (int k = 0; k < steps; ++k) {
      current = std::min(job->trainCap, current + job->trainStep);
    }
    crew.jobAt = current;
    if (crew.job == "maintenance") {
      LogLine(crew.name + " trained " + where + ".");
    } else {
      LogLine(crew.name + " trained " + where + " - now acts at " +
              js::NumToString(crew.jobAt) + job->unit + ".");
    }
  } else {
    LogLine(crew.name + " trained " + where + ", but holds no job to use it on.");
  }
}

void Simulation::FireCrew(CrewMember& crew) {
  const std::string name = crew.name;
  const std::string id = crew.id;
  const bool hadJob = !crew.job.empty();
  orderSheets_.erase(id);
  state_.pendingOrders.erase(id);
  crewClaims_.erase(id);
  ClearOngoing("cw-" + id);
  for (std::size_t i = state_.ongoing.size(); i-- > 0;) {
    const Task task = state_.ongoing[i];
    if (task.by != name && task.who != id && task.other != id) { continue; }
    if (task.type == "o2slot") {
      life_.canisters = std::min(life_.capacity, life_.canisters + 1);
    }
    if (task.type == "cook") { cookRestock_ = false; }
    ClearOngoing(task.id);
  }
  if (hadJob) { LayOff(crew, "dismissed by the commander"); }
  else { LogLine(name + " holds no job to be taken off."); }
}

}  // namespace ship

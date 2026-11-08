// ============================================================================
//  Orders.cpp — the order sheet: staging, pricing, and handing it over.
//  ---------------------------------------------------------------------------
//  THE PRICE IS THE POINT. A cleaner is a word and a mop; an engineer is a week
//  of somebody's attention. An order's price is also how long it takes to carry
//  out, so the bottom button counts the whole sheet down as one bar. Staging is
//  free; giving the order costs the commander one action, like every other
//  commitment in the game.
// ============================================================================
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

// The trade the sheet is about: the STAGED job if there is one, otherwise the
// one the hand already holds.
std::string Simulation::StagedJob(const CrewMember& crew) const {
  const OrderSheet* sheet = SheetOf(crew);
  if (sheet != nullptr && sheet->hasJob) { return sheet->job; }
  return crew.job;
}

double Simulation::JobCost(const std::string& jobId) const {
  const JobDef* job = FindJobDef(jobId);
  return job == nullptr ? 1.0 : job->cost;
}

// What one order costs in actions, priced against a job.
double Simulation::OrderCostFor(const std::string& jobId, const std::string& orderId) const {
  const Catalog::JobDefinition* job = nullptr;
  for (const Catalog::JobDefinition& def : catalog_.jobDefs) {
    if (def.id == jobId) { job = &def; }
  }
  if (orderId == "doc") { return 1; }
  if (orderId == "fire") { return 5; }
  if (orderId == "learnOn") { return job != nullptr && job->learnOn > 0 ? job->learnOn : 1; }
  if (orderId == "learnSt") { return job != nullptr && job->learnSt > 0 ? job->learnSt : 2; }
  return 1;
}

// What one order costs in shiplets, priced against a job.
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
  double total = sheet->hasJob ? JobCost(sheet->job) : 0.0;
  for (const Catalog::OrderDefinition& order : catalog_.orderDefs) {
    if (sheet->Has(order.id)) { total += OrderCostFor(StagedJob(crew), order.id); }
  }
  return total;
}

double Simulation::CrewFeeOf(const CrewMember& crew) const {
  const OrderSheet* sheet = SheetOf(crew);
  if (sheet == nullptr) { return 0; }
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

bool Simulation::OrdersRunning() const { return FindOngoing("crew-assign") != nullptr; }

bool Simulation::CrewAvailable(const CrewMember& crew) const {
  return CrewBusy(crew) == nullptr;
}

// ---- staging (free) -------------------------------------------------------
void Simulation::ClearOrders() {
  orderSheets_.clear();
  state_.pendingOrders.clear();
}

bool Simulation::StageJob(const std::string& crewId, const std::string& jobId) {
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr || OrdersRunning() || CrewBusy(*crew) != nullptr) { return false; }
  if (FindJobDef(jobId) == nullptr) { return false; }

  OrderSheet& sheet = orderSheets_[crewId];
  // No change, no charge: staging the job they already hold clears the line.
  if (crew->job == jobId) { sheet.hasJob = false; sheet.job.clear(); }
  else { sheet.hasJob = true; sheet.job = jobId; }
  if (sheet.Empty()) { orderSheets_.erase(crewId); }
  state_.pendingOrders[crewId] = !orderSheets_.empty() && orderSheets_.count(crewId) != 0;
  if (!state_.pendingOrders[crewId]) { state_.pendingOrders.erase(crewId); }
  return true;
}

bool Simulation::ToggleOrder(const std::string& crewId, const std::string& orderId) {
  // The rule belongs with the data, not with the button: a station lesson
  // needs a dock, and a rest is refused to a hand that does not need one.
  if (orderId == "learnSt" && !state_.docked) { return false; }
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr) { return false; }
  const OrderSheet* existing = SheetOf(*crew);
  if (orderId == "rest" && crew->fatigue < 25 && crew->morale >= 70 &&
      !(existing != nullptr && existing->Has("rest"))) {
    return false;
  }
  if (OrdersRunning()) { return false; }
  if (CrewBusy(*crew) != nullptr) { return false; }
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

// ---- the commitment -------------------------------------------------------
bool Simulation::CommitOrders() {
  const double work = OrdersCost();
  if (work <= 0 || OrdersRunning()) { return false; }
  for (const auto& entry : orderSheets_) {
    const CrewMember* crew = FindCrew(entry.first);
    if (crew != nullptr && !CrewAvailable(*crew)) { return false; }
  }

  // The schooling is paid for here or the sheet does not go out at all: a
  // half-paid order would teach half a lesson.
  const double fee = OrdersFee();
  if (fee > state_.shiplets) {
    LogLine("Refused - the sheet needs " + js::NumToString(fee) + " sh in fees and the purse holds " +
            js::NumToString(state_.shiplets) + ".");
    return false;
  }

  const std::map<std::string, OrderSheet> sheet = orderSheets_;
  orderSheets_.clear();
  state_.pendingOrders.clear();
  state_.shiplets -= fee;

  // ONE CLICK IS ONE ACTION. The price of the orders is not charged here in a
  // lump; it is the WORK, and the crew spend it carrying the sheet out.
  for (const auto& entry : sheet) { crewClaims_[entry.first] = true; }
  PerformAction("Crew assignments issued");
  crewClaims_.clear();
  if (!state_.gameOver.empty()) { return true; }

  if (fee > 0) {
    LogLine("Training fees paid - " + js::NumToString(fee) + " sh. Purse " +
            js::NumToString(state_.shiplets) + " sh.");
  }
  QueueOrders(sheet, work);
  LogLine("Crew assignments updated. " + js::NumToString(work) + " clicks of work.");
  return true;
}

// One task for the whole sheet under the id of the button at the bottom, plus
// one task per hand sized to what THAT person was asked to do.
void Simulation::QueueOrders(const std::map<std::string, OrderSheet>& sheet, double total) {
  AddOngoing("crew-assign", "Carrying out orders", total, "crewOrder", "crew");

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

// ---- applying a finished sheet -------------------------------------------
void Simulation::ApplyJobTo(CrewMember& crew, const std::string& jobId) {
  const JobDef* job = FindJobDef(jobId);
  // A unique job belongs to one pair of hands. Handing it over takes it off
  // whoever had it.
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
    for (int k = 0; k < steps; ++k) {
      crew.jobAt = std::min(job->trainCap,
                            (crew.jobAt == 0 ? job->baseAt : crew.jobAt) + job->trainStep);
    }
    if (job->id == "kitchen") {
      LogLine(crew.name + " trained " + where + " - galley batches remain one dry ration "
              "for up to " + js::NumToString(rules_.mess.foodPerRation) +
              " cooked food in " + js::NumToString(rules_.mess.foodPerRation) + " clicks.");
    } else {
      LogLine(crew.name + " trained " + where + " - now acts at " +
              js::NumToString(crew.jobAt) + job->unit + ".");
    }
  } else {
    LogLine(crew.name + " trained " + where + ", but holds no job to use it on.");
  }
}

void Simulation::FireCrew(CrewMember& crew) {
  if (crew.job.empty()) {
    LogLine(crew.name + " holds no job to be taken off.");
    return;
  }
  LayOff(crew, "dismissed by the commander");
}

}  // namespace ship

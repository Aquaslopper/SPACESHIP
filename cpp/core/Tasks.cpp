#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <string>

namespace ship {

namespace {

std::string FirstName(const std::string& name) {
  const std::size_t space = name.find(' ');
  return space == std::string::npos ? name : name.substr(0, space);
}

bool IsQuiet(const std::string& type) {
  return type == "crewOrder" || type == "crewJob" || type == "crewDoc" ||
          type == "o2slot" || type == "maint" || type == "cook" ||
          type == "salvage" || type == "surveyPackage" || type == "shipProject";
}

bool IsAutoClear(const std::string& type) {
  return type == "crewOrder" || type == "crewJob" || type == "crewDoc" ||
          type == "o2slot" || type == "maint" || type == "cook" ||
          type == "salvage" || type == "surveyPackage" || type == "shipProject";
}

}  // namespace

Task* Simulation::FindOngoing(const std::string& id) {
  for (Task& task : state_.ongoing) {
    if (task.id == id) { return &task; }
  }
  return nullptr;
}

const Task* Simulation::FindOngoing(const std::string& id) const {
  for (const Task& task : state_.ongoing) {
    if (task.id == id) { return &task; }
  }
  return nullptr;
}

Task* Simulation::AddOngoing(const std::string& id, const std::string& label, double total,
                             const std::string& type, const std::string& target) {
  if (FindOngoing(id) != nullptr) { return nullptr; }
  Task task;
  task.id = id;
  task.label = label;
  task.total = total;
  task.left = total;
  task.state = "running";
  task.type = type.empty() ? id : type;
  task.target = target;
  state_.ongoing.push_back(task);
  return &state_.ongoing.back();
}

void Simulation::ClearOngoing(const std::string& id) {
  for (std::size_t i = state_.ongoing.size(); i-- > 0;) {
    if (state_.ongoing[i].id == id) { state_.ongoing.erase(state_.ongoing.begin() + i); }
  }
}

void Simulation::SweepAutoTasks() {
  std::vector<Task> keep;
  keep.reserve(state_.ongoing.size());
  for (const Task& task : state_.ongoing) {
    if (task.state == "done" && IsAutoClear(task.type)) { continue; }
    keep.push_back(task);
  }
  state_.ongoing.swap(keep);
}

void Simulation::OnTaskDone(Task& task) {
  if (task.type == "maint") {
    state_.engineCondition = 100;
    state_.wearTick = 0;
    LogLine("Engine maintenance completed by " + (task.by.empty() ? "the commander" : task.by) + ".");
    return;
  }
  if (task.type == "o2slot") {
    const int slot = static_cast<int>(task.slot);
    if (slot >= 0 && slot < static_cast<int>(life_.slots.size())) {
      CanisterSlot& target = life_.slots[static_cast<std::size_t>(slot)];
      target.state = "installed";
      LogLine("Canister seated in " + target.name + ".");
    } else {
      LogLine("Canister seated in a slot.");
    }
    return;
  }
  if (task.type == "cook") {
    mess_.rack = std::min(mess_.capacity, mess_.rack + rules_.mess.foodPerRation);
    LogLine((task.by.empty() ? "Someone" : task.by) + " finished cooking a fresh batch.");
    if (mess_.rack >= mess_.capacity || mess_.rations < 1.0) { cookRestock_ = false; }
    return;
  }
  if (task.type == "crewJob") {
    if (CrewMember* crew = FindCrew(task.who)) { ApplyOrderPayload(*crew, task); }
    return;
  }
  if (task.type == "crewDoc") {
    if (CrewMember* crew = FindCrew(task.who)) {
      crew->health = std::min(100.0, crew->health + 12);
      crew->activity = "Back on station";
      LogLine(crew->name + " is back from the medical bay.");
    }
    return;
  }
  if (task.type == "crewOrder") {
    LogLine("The order sheet is discharged.");
    return;
  }
  if (task.type == "salvage") {
    state_.lotSerial += 1;
    GoodsLot lot;
    lot.id = "recovery-" + js::NumToString(state_.lotSerial);
    lot.name = "Recovered salvage";
    lot.vol = task.vol;
    lot.value = 30;
    lot.kind = "Salvage";
    goods_.push_back(lot);
    LogLine("Salvage secured in the cargo hold.");
    return;
  }
  if (task.type == "surveyPackage") {
    for (SurveyRecord& record : survey_) {
      if (record.id == task.target && record.state != "packaged") {
        record.state = "packaged";
        record.value = js::Ceil(record.value * 1.15);
        LogLine("Survey packaged - " + record.name + ".");
      }
    }
    return;
  }
  if (task.type == "shipProject") {
    ProjectDone(task);
    return;
  }
}

void Simulation::TickOngoing() { AdvanceTasks(1); }

void Simulation::AdvanceTasks(double n) {
  double quiet = 0;

  std::vector<Task> snapshot = state_.ongoing;

  for (const Task& original : snapshot) {
    Task* task = FindOngoing(original.id);
    if (task == nullptr || task->state != "running") { continue; }

    const bool needsPower = task->type == "scan" || task->type == "salvage" ||
                            task->type == "extract" ||
                            (task->type == "shipProject" && task->target != "social");
    const bool needsDrive = task->type == "travel";
    if (needsDrive && (state_.engineCondition <= 0 || StatValue("energy") <= 0)) { continue; }
    if (needsPower && StatValue("energy") <= 0) { continue; }
    if (task->type == "travel") { ClampStat("fuel", -1); }
    
    task->left -= n;
    if (task->left <= 0) {
      task->left = 0;
      task->state = "done";
      const Task finished = *task;
      OnTaskDone(*FindOngoing(original.id));
      if (!IsQuiet(finished.type)) { quiet += 1; }
    }
  }

  SweepAutoTasks();

  for (double k = 0; k < quiet; k += 1) { LogLine("A ship process has completed."); }
}

void Simulation::ApplyOrderPayload(CrewMember& crew, const Task& task) {
  if (!task.hasOrders) { return; }
  const auto flag = [&task](const std::string& id) {
    auto it = task.sheetFlags.find(id);
    return it != task.sheetFlags.end() && it->second;
  };
  const bool rest = task.rest || flag("rest");
  const bool fireOrder = flag("fire");
  const bool docOrder = flag("doc");
  const bool learnOn = flag("learnOn");
  const bool learnSt = flag("learnSt");
  const bool hasJob = task.sheetHasJob;
  const std::string jobId = task.sheetJob;

  if (fireOrder) {
    FireCrew(crew);
    return;
  }
  if (rest) {
    crew.fatigue = std::max(0.0, crew.fatigue - 25);
    crew.morale = std::min(100.0, crew.morale + 5);
    LogLine(crew.name + " took a restorative break.");
  }
  if (hasJob) { ApplyJobTo(crew, jobId); }
  if (docOrder) {
    Task* doc = AddOngoing("doc-" + crew.id, "Medical - " + FirstName(crew.name),
                           rules_.docAway, "crewDoc", crew.id);
    if (doc != nullptr) { doc->who = crew.id; }
    crew.activity = "In the medical bay";
    LogLine(crew.name + " went into the medical bay - about " +
            js::NumToString(rules_.docAway) + " clicks.");
  }
  if (learnOn) { TrainCrew(crew, 1, "aboard"); }
  if (learnSt) { TrainCrew(crew, 2, "at the station"); }
  if (!docOrder) { crew.activity = "Back on station"; }
}

}  // namespace ship

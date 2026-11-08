// ============================================================================
//  Tasks.cpp — the ongoing-task engine.
//  ---------------------------------------------------------------------------
//  A committed process does not run on a clock. It runs on the player's
//  attention: it advances only when an action is spent somewhere else.
//
//  The engine knows nothing about meaning. It counts down and then asks the
//  dispatch table what that meant, exactly as TASK_TYPES does in the reference:
//
//    autoClear       retire the task the moment it lands
//    quiet           the task names itself, so no vague "completed" line
//    onTick(n)       produce something WHILE the work runs (the galley)
//    onDone(t)       the counter hit zero; nobody has looked yet
//    onAcknowledge   the player clicked the finished button
//
//  Phase 1A implements every hook that the action clock can reach on its own.
//  travel, scan and shipProject are registered but only fire in Phase 1B, when
//  the Navigation commands that create them exist.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <string>

namespace ship {

namespace {

// "Rell Voss" -> "Rell": the sheet names a hand by the name the roster uses.
std::string FirstName(const std::string& name) {
  const std::size_t space = name.find(' ');
  return space == std::string::npos ? name : name.substr(0, space);
}

bool IsQuiet(const std::string& type) {
  // Types that announce themselves by name; they do not also emit the vague
  // "A ship process has completed." line.
  return type == "crewOrder" || type == "crewJob" || type == "crewDoc" ||
         type == "o2slot" || type == "maint" || type == "cook" ||
         type == "incident" || type == "salvage" || type == "surveyPackage" ||
         type == "shipProject";
}

bool IsAutoClear(const std::string& type) {
  // HTA TASK_TYPES: automatic completion is explicitly opted into by type.
  // Unknown types must not silently lose a finished readout.
  return type == "crewOrder" || type == "crewJob" || type == "crewDoc" ||
         type == "o2slot" || type == "maint" || type == "cook" ||
         type == "incident" || type == "salvage" || type == "surveyPackage" ||
         type == "shipProject";
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

void Simulation::TickTask(const Task&, double) {
  // Nothing lands mid-batch. A cook's five clicks are the bar filling; the
  // food appears on completion, in OnTaskDone.
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
    // One dry ration yields cooked food on the counter, clamped to the ceiling.
    mess_.rack = std::min(mess_.capacity, mess_.rack + rules_.mess.foodPerRation);
    LogLine((task.by.empty() ? "Someone" : task.by) + " finished cooking a fresh batch.");
    // The counter is full, or the larder is empty: the cook's chain is done.
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
      SimRemember("Care", crew->name + " recovered in the medical bay.", crew->id);
    }
    return;
  }
  if (task.type == "crewOrder") {
    LogLine("The order sheet is discharged.");
    return;
  }
  if (task.type == "incident") {
    if (FindOngoing(task.id) != nullptr) { /* still live */ }
    bool exists = false;
    for (const Incident& incident : events_) {
      if (incident.id == task.target) { exists = true; }
    }
    if (exists) {
      RemoveIncident(task.target);
      LogLine("Crew resolved the report.");
    }
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
  // travel and scan do nothing when the counter lands: the player must come back
  // and press the finished button, which is acknowledged in Navigation.
}

void Simulation::TickOngoing() { AdvanceTasks(1); }

double Simulation::AdvanceTasks(double n) {
  double done = 0;
  double quiet = 0;

  // Walk a SNAPSHOT: a hook is allowed to queue work, and a task created half
  // way through this loop must not be docked a step by the tick that made it.
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

    const double used = (n < task->left) ? n : task->left;
    const Task view = *task;  // hooks read the task as it stood this tick
    TickTask(view, used);

    task->left -= n;
    if (task->left <= 0) {
      task->left = 0;
      task->state = "done";
      // A hook may queue new work, which can reallocate the vector this pointer
      // walks into. Copy what the hook and the quiet count need first - reading
      // task->type afterwards was reading freed storage.
      const Task finished = *task;
      OnTaskDone(*FindOngoing(original.id));
      done += 1;
      if (!IsQuiet(finished.type)) { quiet += 1; }
    }
  }

  SweepAutoTasks();

  // Deliberately vague: the main screen never says what finished, the player
  // goes and looks.
  for (double k = 0; k < quiet; k += 1) { LogLine("A ship process has completed."); }
  return done;
}

void Simulation::ApplyOrderPayload(CrewMember& crew, const Task& task) {
  if (!task.hasOrders) { return; }
  const auto flag = [&task](const std::string& id) {
    auto it = task.sheetFlags.find(id);
    return it != task.sheetFlags.end() && it->second;
  };
  // Read every flag BEFORE creating anything: AddOngoing pushes onto the same
  // vector `task` lives in, and a reallocation would leave that reference
  // pointing at dead storage - which silently turned a patient's sheet into a
  // discharged one.
  const bool rest = task.rest || flag("rest");
  const bool fireOrder = flag("fire");
  const bool docOrder = flag("doc");
  const bool learnOn = flag("learnOn");
  const bool learnSt = flag("learnSt");
  const bool hasJob = task.sheetHasJob;
  const std::string jobId = task.sheetJob;

  // FIRST, because nothing else on the sheet means anything to somebody who is
  // being put off the ship.
  if (fireOrder) {
    FireCrew(crew);
    return;
  }
  if (rest) {
    crew.fatigue = std::max(0.0, crew.fatigue - 25);
    crew.morale = std::min(100.0, crew.morale + 5);
    LogLine(crew.name + " took a restorative break.");
    SimRemember("Rest", crew.name + " took a restorative break.", crew.id);
  }
  if (hasJob) { ApplyJobTo(crew, jobId); }
  if (docOrder) {
    // The sheet is done; the doctor is not. This is the one order whose
    // consequence outlives the order itself.
    Task* doc = AddOngoing("doc-" + crew.id, "Medical - " + FirstName(crew.name),
                           rules_.docAway, "crewDoc", crew.id);
    if (doc != nullptr) { doc->who = crew.id; }
    crew.activity = "In the medical bay";
    LogLine(crew.name + " went into the medical bay - about " +
            js::NumToString(rules_.docAway) + " clicks.");
  }
  if (learnOn) { TrainCrew(crew, 1, "aboard"); }
  if (learnSt && state_.docked) { TrainCrew(crew, 2, "at the station"); }
  if (!docOrder) { crew.activity = "Back on station"; }
}

}  // namespace ship

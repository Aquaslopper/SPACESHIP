#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <string>

namespace ship {

int Simulation::ProjectLot(const std::string& kind) const {
  for (std::size_t index = 0; index < goods_.size(); ++index) {
    const GoodsLot& lot = goods_[index];
    if (lot.kind == kind && lot.contractId.empty() && !lot.risk) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

std::string Simulation::ProjectBlock(const std::string& id) const {
  const ProjectDef* project = nullptr;
  for (const ProjectDef& candidate : catalog_.projects) {
    if (candidate.id == id) { project = &candidate; }
  }
  if (project == nullptr) { return "Unknown project."; }
  if (!HasRoom(project->room)) return "Install the required room at a space station.";

  if (FindOngoing("project-" + id) != nullptr) { return "Already in progress."; }
  if (StatValue("energy") <= project->energy) {
    return "Needs more than " + js::NumToString(project->energy) + "% energy.";
  }
  if (!project->lot.empty() && ProjectLot(project->lot) < 0) {
    std::string kind = project->lot;
    std::transform(kind.begin(), kind.end(), kind.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return "Needs an uncommitted " + kind + " cargo lot.";
  }
  if (id == "oxygen" && life_.canisters + [this] {
        double count = 0;
        for (const Task& task : state_.ongoing) {
          if (task.type == "o2slot" && task.state == "running" && !task.by.empty()) { count += 1; }
        }
        return count;
      }() >= life_.capacity) {
    return "The canister rack is full or reserved for an installation.";
  }
  if (id == "food" && (mess_.rations > mess_.rationCapacity - 8 /* ||
                       FindOngoing("cook") != nullptr */)) {
    return "Needs room for eight dry rations in the larder.";
  }
  if (id == "brace" && StatValue("hull") >= 100) { return "The hull is already sound."; }
  if (id == "cooling" && (state_.engineCondition >= 100 /* || MaintRunning() */)) {
    return "The drive is already sound.";
  }
  return "";
}

bool Simulation::StartProject(const std::string& id) {
  const ProjectDef* project = nullptr;
  for (const ProjectDef& candidate : catalog_.projects) {
    if (candidate.id == id) { project = &candidate; }
  }
  if (project == nullptr || !state_.gameOver.empty()) { return false; }
  if (!ProjectBlock(id).empty()) { return false; }

  if (!project->lot.empty()) {
    const int lot = ProjectLot(project->lot);
    if (lot >= 0) { goods_.erase(goods_.begin() + lot); }
  }

  ClampStat("energy", -project->energy);
  projectClaim_ = id;
  PerformAction(project->label + " started");
  projectClaim_.clear();
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("project-" + id, project->label, project->work, "shipProject", id);
  return true;
}

bool Simulation::StartMedical(const std::string& crewId) {
  if (!HasRoom("r1")) return false;
  CrewMember* crew = FindCrew(crewId);
  if (crew == nullptr || crew->health >= 100 || CrewBusy(*crew) != nullptr) { return false; }
  if (state_.medkits <= 0 || StatValue("energy") <= 2 || !state_.gameOver.empty()) { return false; }

  state_.medkits -= 1;
  ClampStat("energy", -2);
  crewClaims_[crewId] = true;
  const std::string name = crew->name;
  PerformAction("Treatment started for " + name);
  crewClaims_.erase(crewId);
  if (!state_.gameOver.empty() || FindCrew(crewId) == nullptr) { return true; }

  Task* task = AddOngoing("doc-" + crewId, "Treating " + name, 3, "shipProject", "medical");
  if (task != nullptr) {
    task->who = crewId;
    if (CrewMember* patient = FindCrew(crewId)) { patient->activity = "In the medical bay"; }
  }
  return true;
}

bool Simulation::StartSocial(const std::string& a, const std::string& b) {
  if (!HasRoom("r13")) return false;
  CrewMember* one = FindCrew(a);
  CrewMember* two = FindCrew(b);
  if (one == nullptr || two == nullptr || a == b) { return false; }
  if (CrewBusy(*one) != nullptr || CrewBusy(*two) != nullptr) { return false; }
  if (FindOngoing("project-social") != nullptr || mess_.rack < 2 || !state_.gameOver.empty()) { return false; }

  mess_.rack -= 2;
  crewClaims_[a] = true;
  crewClaims_[b] = true;
  const std::string names = one->name + " and " + two->name;
  PerformAction(names + " arranged a meal");
  crewClaims_.erase(a);
  crewClaims_.erase(b);
  if (!state_.gameOver.empty() || FindCrew(a) == nullptr || FindCrew(b) == nullptr) { return true; }

  Task* task = AddOngoing("project-social", "Sharing a meal", 3, "shipProject", "social");
  if (task != nullptr) {
    task->who = a;
    task->other = b;
  }
  return true;
}

void Simulation::ProjectDone(Task& task) {
  const std::string id = task.target;
  std::string text;

  if (id == "kit") { state_.repairKits += 1; text = "The workshop made a repair kit from salvage."; }
  if (id == "oxygen") {
    life_.canisters = std::min(life_.capacity, life_.canisters + 1);
    text = "The recyclers recovered an O2 canister.";
  }
  if (id == "fuel") { state_.fuelCells += 1; text = "The reserve crew refined a fuel cell."; }
  if (id == "food") {
    mess_.rations = std::min(mess_.rationCapacity, mess_.rations + 8);
    text = "The hydroponics crew processed produce into dry rations.";
  }
  if (id == "brace") { ClampStat("hull", 15); text = "Damage control patched the hull with recovered material."; }
  if (id == "cooling") {
    state_.engineCondition = std::min(100.0, state_.engineCondition + 15);
    text = "Thermal control stabilized the drive.";
  }

  if (id == "medical") {
    if (CrewMember* patient = FindCrew(task.who)) {
      patient->health = std::min(100.0, patient->health + 25);
      patient->activity = "Back on station";
      text = patient->name + " recovered in the medical bay.";
    }
  }

  if (id == "social") {
    CrewMember* one = FindCrew(task.who);
    CrewMember* other = FindCrew(task.other);
    if (one != nullptr && other != nullptr) {
      one->morale = std::min(100.0, one->morale + 4);
      other->morale = std::min(100.0, other->morale + 4);
      text = one->name + " and " + other->name + " shared a meal and talked.";
    }
  }

  if (!text.empty()) {
    state_.chronicle.projects += 1;
    if (id == "social") { state_.chronicle.meals += 1; }
    CheckCommendations();
    LogLine(text);
  }
}

}  // namespace ship

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

  if (!text.empty()) {
    CheckCommendations();
    LogLine(text);
  }
}

}  // namespace ship

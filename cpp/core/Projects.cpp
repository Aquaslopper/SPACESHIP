// ============================================================================
//  Projects.cpp — the ten things the ship can make for itself.
//  ---------------------------------------------------------------------------
//  A project is a draw on the reserve, a cargo lot consumed, and four or five
//  actions of work that pause the instant the batteries are empty. The block
//  rules are the interesting part: a project that would make a canister the rack
//  has no room for, or rations the galley cannot hold, is refused before a
//  single shiplet or action is spent.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <string>

namespace ship {

namespace {
bool Has(const std::map<std::string, bool>& map, const std::string& key) {
  auto it = map.find(key);
  return it != map.end() && it->second;
}
}  // namespace

std::vector<Memory> Simulation::SimUnprocessed() const {
  std::vector<Memory> out;
  for (const Memory& memory : state_.sim.memories) {
    if (!memory.processed) { out.push_back(memory); }
  }
  return out;
}

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
  if (id == "food" && (mess_.rack > mess_.capacity - 8 || FindOngoing("cook") != nullptr)) {
    return "Needs space for eight rations and an idle galley.";
  }
  if (id == "brace" && StatValue("hull") >= 100) { return "The hull is already sound."; }
  if (id == "cooling" && (state_.engineCondition >= 100 || MaintRunning())) {
    return "The drive is sound or already under maintenance.";
  }
  if (id == "sensors" && (state_.sim.insight < 1 || state_.sim.sensor || FindOngoing("scan") != nullptr)) {
    return "Needs one insight, no stored calibration and idle sensors.";
  }
  if (id == "dream" && (SimUnprocessed().size() < 2 || state_.sim.insight >= 5)) {
    return "Needs two unconsolidated memories and room for an insight (maximum five).";
  }
  if (id == "observe" && (FindOngoing("travel") != nullptr || Has(state_.sim.observed, map_.here))) {
    return "Stop at an unrecorded location first; each location can be recorded once.";
  }
  if (id == "decode" && (state_.sim.signal != "intercepted" || state_.sim.insight < 1)) {
    return "Needs the intercepted signal and one insight.";
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

  bool hasPayload = false;
  std::string payload;
  std::vector<Memory> chosen;

  if (!project->lot.empty()) {
    const int lot = ProjectLot(project->lot);
    if (lot >= 0) { goods_.erase(goods_.begin() + lot); }
  }
  if (id == "dream") {
    std::vector<Memory> pending = SimUnprocessed();
    chosen.push_back(pending[0]);
    chosen.push_back(pending[1]);
    for (Memory& memory : state_.sim.memories) {
      if (memory.id == chosen[0].id || memory.id == chosen[1].id) { memory.processed = true; }
    }
    hasPayload = true;
  }
  if (id == "sensors" || id == "decode") { state_.sim.insight -= 1; }
  if (id == "decode") { state_.sim.signal = "decoding"; }
  if (id == "observe") {
    payload = map_.here;
    hasPayload = true;
    state_.sim.observed[map_.here] = true;
  }

  ClampStat("energy", -project->energy);
  projectClaim_ = id;
  PerformAction(project->label + " started");
  projectClaim_.clear();
  if (!state_.gameOver.empty()) { return true; }

  Task* task = AddOngoing("project-" + id, project->label, project->work, "shipProject", id);
  if (task != nullptr) {
    if (hasPayload) { task->payload = payload; }
    if (id == "dream") {
      task->dreamIds = {js::NumToString(chosen[0].id), js::NumToString(chosen[1].id)};
      task->dreamKinds = {chosen[0].kind, chosen[1].kind};
      task->dreamTexts = {chosen[0].text, chosen[1].text};
      task->dreamWho = {chosen[0].who, chosen[0].other};
      task->dreamOther = {chosen[1].who, chosen[1].other};
    }
  }
  return true;
}

bool Simulation::StartDream(const std::string& firstId, const std::string& secondId) {
  // The reference's two selects, as a command: the pair must be two distinct
  // unconsolidated memories. Everything else is StartProject("dream").
  if (firstId == secondId) { return false; }
  const std::vector<Memory> pending = SimUnprocessed();
  const Memory* first = nullptr;
  const Memory* second = nullptr;
  for (const Memory& memory : pending) {
    if (js::NumToString(memory.id) == firstId) { first = &memory; }
    if (js::NumToString(memory.id) == secondId) { second = &memory; }
  }
  if (first == nullptr || second == nullptr) { return false; }
  if (!ProjectBlock("dream").empty()) { return false; }

  const ProjectDef* project = nullptr;
  for (const ProjectDef& candidate : catalog_.projects) {
    if (candidate.id == "dream") { project = &candidate; }
  }
  if (project == nullptr || !state_.gameOver.empty()) { return false; }

  for (Memory& memory : state_.sim.memories) {
    if (memory.id == first->id || memory.id == second->id) { memory.processed = true; }
  }
  ClampStat("energy", -project->energy);
  projectClaim_ = "dream";
  PerformAction(project->label + " started");
  projectClaim_.clear();
  if (!state_.gameOver.empty()) { return true; }

  Task* task = AddOngoing("project-dream", project->label, project->work, "shipProject", "dream");
  if (task != nullptr) {
    task->dreamIds = {firstId, secondId};
    task->dreamKinds = {first->kind, second->kind};
    task->dreamTexts = {first->text, second->text};
    task->dreamWho = {first->who, first->other};
    task->dreamOther = {second->who, second->other};
  }
  return true;
}

bool Simulation::StartMedical(const std::string& crewId) {
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

// ---------------------------------------------------------------------------
// shipProject.onDone — the ten outputs, plus medical and shared meals.
// ---------------------------------------------------------------------------

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
    mess_.rack = std::min(mess_.capacity, mess_.rack + 8);
    text = "The hydroponics crew processed produce into rations.";
  }
  if (id == "brace") { ClampStat("hull", 15); text = "Damage control patched the hull with recovered material."; }
  if (id == "cooling") {
    state_.engineCondition = std::min(100.0, state_.engineCondition + 15);
    text = "Thermal control stabilized the drive.";
  }
  if (id == "sensors") { state_.sim.sensor = 1; text = "The sensor array is calibrated for one faster scan."; }

  if (id == "observe") {
    const Body* body = FindBodyById(task.payload);
    const bool held = [&] {
      for (const SurveyRecord& record : survey_) {
        if (record.id == "obs-" + task.payload) { return true; }
      }
      return false;
    }();
    if (body != nullptr && !held) {
      SurveyRecord record;
      record.id = "obs-" + body->id;
      record.name = body->name + " - observation";
      record.src = "Observation";
      record.value = 65;
      record.state = "filed";
      survey_.push_back(record);
      text = "The observation deck recorded " + body->name + ".";
    }
  }

  if (id == "dream") {
    // dreamPart() turns a memory into {kind, text, who, other}; the pair yields
    // two insights when both halves are about the world, one otherwise.
    const bool firstExploring = task.dreamKinds.size() > 0 &&
                                (task.dreamKinds[0] == "Discovery" || task.dreamKinds[0] == "World" ||
                                 task.dreamKinds[0] == "Arrival");
    const bool secondExploring = task.dreamKinds.size() > 1 &&
                                 (task.dreamKinds[1] == "Discovery" || task.dreamKinds[1] == "World" ||
                                  task.dreamKinds[1] == "Arrival");
    state_.sim.insight = std::min(5.0, state_.sim.insight + (firstExploring && secondExploring ? 2 : 1));

    // A shared memory lifts the two people who share it.
    for (CrewMember& crew : crew_) {
      const bool inFirst = crew.id == task.dreamWho[0] || crew.id == task.dreamWho[1];
      const bool inSecond = crew.id == task.dreamOther[0] || crew.id == task.dreamOther[1];
      if (inFirst && inSecond) { crew.morale = std::min(100.0, crew.morale + 2); }
    }
    if (task.dreamKinds.size() > 1) {
      const bool conflictFirst = task.dreamKinds[0] == "Conflict";
      const bool connectionFirst = task.dreamKinds[0] == "Connection";
      const bool conflictSecond = task.dreamKinds[1] == "Conflict";
      const bool connectionSecond = task.dreamKinds[1] == "Connection";
      const std::string a1 = task.dreamWho[0];
      const std::string b1 = task.dreamWho[1];
      const std::string a2 = task.dreamOther[0];
      const std::string b2 = task.dreamOther[1];
      auto key = [](const std::string& a, const std::string& b) { return a < b ? a + "~" + b : b + "~" + a; };
      if (((conflictFirst && connectionSecond) || (connectionFirst && conflictSecond)) && !a1.empty() &&
          !b1.empty() && !a2.empty() && !b2.empty() && key(a1, b1) == key(a2, b2)) {
        SimChangeBond(a1, b1, 4);
      }
    }
    const std::string firstText = task.dreamTexts.empty() ? std::string() : task.dreamTexts[0].substr(0, 48);
    const std::string secondText = task.dreamTexts.size() > 1 ? task.dreamTexts[1].substr(0, 48) : std::string();
    text = "A dream joins " + (task.dreamKinds.empty() ? std::string("nothing") : task.dreamKinds[0]) + " with " +
           (task.dreamKinds.size() > 1 ? task.dreamKinds[1] : std::string("nothing")) + ".";
    state_.sim.dreams.push_back(Dream{actionCounter_, text + " " + firstText + " / " + secondText});
    while (state_.sim.dreams.size() > 6) { state_.sim.dreams.erase(state_.sim.dreams.begin()); }
  }

  if (id == "decode") {
    state_.sim.signal = Has(state_.scanned, "echo-site") ? "request" : "coordinates";
    text = "Silicon Marrow's signal points to the Choir Array in Echo Shoal.";
  }

  if (id == "medical") {
    if (CrewMember* patient = FindCrew(task.who)) {
      patient->health = std::min(100.0, patient->health + 25);
      patient->activity = "Back on station";
      text = patient->name + " recovered in the medical bay.";
      SimRemember("Care", text, patient->id);
    }
  }

  if (id == "social") {
    CrewMember* one = FindCrew(task.who);
    CrewMember* other = FindCrew(task.other);
    if (one != nullptr && other != nullptr) {
      const double bonus = SimAnchor(*one) == SimAnchor(*other) ? 2.0 : 0.0;
      SimChangeBond(one->id, other->id, 12 + bonus);
      one->morale = std::min(100.0, one->morale + 4);
      other->morale = std::min(100.0, other->morale + 4);
      text = one->name + " and " + other->name + " shared a meal and talked.";
      SimRemember("Connection", text, one->id, other->id);
    }
  }

  if (!text.empty()) {
    state_.chronicle.projects += 1;
    if (id == "dream") { state_.chronicle.dreams += 1; }
    if (id == "social") { state_.chronicle.meals += 1; }
    CheckCommendations();
    if (id != "medical" && id != "social") {
      SimRemember(id == "dream" ? "Dream" : "Work", text);
    }
    LogLine(text);
  }
}

}  // namespace ship

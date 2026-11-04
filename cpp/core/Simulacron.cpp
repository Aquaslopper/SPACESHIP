// ============================================================================
//  Simulacron.cpp — memory, bonds and needs.
//  ---------------------------------------------------------------------------
//  A bounded, offline simulation: real voyage events become memories, memories
//  become the crew's history, and two people who work together slowly acquire
//  a bond. No language model, no randomness, no wall clock — every change here
//  is caused by an action the player spent.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>

namespace ship {

namespace {
bool Contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}
}  // namespace

void Simulation::SimRemember(const std::string& kind, const std::string& text,
                             const std::string& who, const std::string& other) {
  state_.sim.serial += 1;
  Memory memory;
  memory.id = state_.sim.serial;
  memory.kind = kind;
  memory.text = text;
  memory.at = actionCounter_;
  memory.who = who;
  memory.other = other;
  memory.processed = false;
  state_.sim.memories.push_back(memory);
  while (state_.sim.memories.size() > 32) { state_.sim.memories.erase(state_.sim.memories.begin()); }
}

namespace {
std::string BondKey(const std::string& a, const std::string& b) {
  return a < b ? a + "~" + b : b + "~" + a;
}
}  // namespace

double Simulation::SimBond(const std::string& a, const std::string& b) const {
  if (a.empty() || b.empty()) { return 0; }
  auto it = state_.sim.bonds.find(BondKey(a, b));
  return it == state_.sim.bonds.end() ? 0.0 : it->second;
}

void Simulation::SimChangeBond(const std::string& a, const std::string& b, double amount) {
  if (a.empty() || b.empty() || a == b) { return; }
  const std::string key = BondKey(a, b);
  state_.sim.bonds[key] = std::max(-100.0, std::min(100.0, SimBond(a, b) + amount));
}

std::string Simulation::SimCrewName(const std::string& id) const {
  const CrewMember* crew = FindCrew(id);
  return crew != nullptr ? crew->name : std::string("a former crew member");
}

void Simulation::TickSim() {
  if (std::fmod(actionCounter_, 25.0) != 0 || crew_.size() < 2) { return; }

  std::vector<std::pair<CrewMember*, CrewMember*>> pairs;
  for (std::size_t i = 0; i < crew_.size(); ++i) {
    for (std::size_t j = i + 1; j < crew_.size(); ++j) {
      pairs.emplace_back(&crew_[i], &crew_[j]);
    }
  }

  const std::size_t turn =
      static_cast<std::size_t>(std::fmod(js::Floor(actionCounter_ / 25.0) - 1.0,
                                         static_cast<double>(pairs.size())));
  CrewMember& a = *pairs[turn].first;
  CrewMember& b = *pairs[turn].second;

  if (a.fatigue >= 75 && b.fatigue >= 75 && a.morale < 55 && b.morale < 55) {
    SimChangeBond(a.id, b.id, -3);
  }
  const double bond = SimBond(a.id, b.id);
  if (bond >= 20 || bond <= -20) {
    const double delta = bond > 0 ? 1.0 : -1.0;
    a.morale = std::max(0.0, std::min(100.0, a.morale + delta));
    b.morale = std::max(0.0, std::min(100.0, b.morale + delta));
  }
}

std::string Simulation::SimAnchor(const CrewMember& crew) const {
  std::string bio = crew.role + " " + crew.traits + " " + crew.hobby;
  std::transform(bio.begin(), bio.end(), bio.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (Contains(bio, "pilot") || Contains(bio, "navigation") || Contains(bio, "chart") ||
      Contains(bio, "route")) {
    return "Wayfinder";
  }
  if (Contains(bio, "engineer") || Contains(bio, "mechanic") || Contains(bio, "repair") ||
      Contains(bio, "wrench")) {
    return "Maker";
  }
  if (Contains(bio, "support") || Contains(bio, "spare") || Contains(bio, "suppl") ||
      Contains(bio, "methodical")) {
    return "Keeper";
  }
  if (Contains(bio, "story") || Contains(bio, "stories") || Contains(bio, "notices") ||
      Contains(bio, "flower") || Contains(bio, "recording")) {
    return "Witness";
  }
  return "Companion";
}

std::string Simulation::SimDrive(const CrewMember& crew) const {
  if (crew.health < 75 || AirStageIndex() <= 1) { return "Safety - care and breathable air"; }
  if (crew.fatigue > 70) { return "Rest - a break from the watch"; }
  if (crew.hunger > 70) { return "Nourishment - a reliable meal"; }
  if (crew.morale < 60) { return "Belonging - a change of pace or company"; }
  if (crew.job.empty() || crew.skill == "Untrained" || crew.skill.empty()) {
    return "Mastery - useful work and training";
  }
  const std::string anchor = SimAnchor(crew);
  if (anchor == "Wayfinder") { return "Discovery - a new place or a quiet watch"; }
  if (anchor == "Maker") { return "Mastery - something worth making"; }
  if (anchor == "Keeper") { return "Safety - keeping the ship supplied"; }
  return "Belonging - stories and company";
}

void Simulation::AutoDesires() {
  if (std::fmod(actionCounter_, 30.0) != 0) { return; }

  CrewMember* chosen = nullptr;
  for (CrewMember& crew : crew_) {
    if (crew.fatigue < 75) { continue; }
    if (SimDrive(crew).rfind("Rest", 0) != 0) { continue; }
    if (CrewBusy(crew) != nullptr) { continue; }
    if (state_.pendingOrders.count(crew.id) != 0) { continue; }
    if (chosen == nullptr || crew.fatigue > chosen->fatigue) { chosen = &crew; }
  }
  if (chosen == nullptr) { return; }

  Task* task = AddOngoing("cw-" + chosen->id, "Resting - " + chosen->name, 3, "crewJob",
                          chosen->id);
  if (task != nullptr) {
    task->who = chosen->id;
    task->hasOrders = true;
    task->rest = true;
    chosen->activity = "Taking a break";
    SimRemember("Need", chosen->name + " stepped away to rest.", chosen->id);
    LogLine(chosen->name + " is taking a short break from duty.");
  }
}

}  // namespace ship

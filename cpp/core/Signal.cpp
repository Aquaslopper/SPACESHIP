// ============================================================================
//  Signal.cpp — the Silicon Marrow story: a relay, an array, and a choice.
//  ---------------------------------------------------------------------------
//  Two free-to-read records and one permanent decision. The comms relay will
//  sell a region's history for two percent energy; the array's archive can be
//  sheltered at the Archive Hull or sold at any station, and neither can be
//  undone. Both spend exactly one action, like every other committed command.
// ============================================================================
#include "core/Simulation.h"

#include <algorithm>

namespace ship {

const std::vector<std::string>& Simulation::WorldHistory(const std::string& regionId) const {
  static const std::vector<std::string> empty;
  auto it = catalog_.worldHistory.find(regionId);
  return it == catalog_.worldHistory.end() ? empty : it->second;
}

bool Simulation::BroadcastLocal() {
  if (!state_.gameOver.empty()) { return false; }
  const Body* system = CurrentSystem();
  if (system == nullptr) { return false; }
  auto heard = state_.sim.broadcasts.find(system->id);
  if (heard != state_.sim.broadcasts.end() && heard->second) { return false; }
  if (StatValue("energy") <= 2) { return false; }

  const std::vector<std::string>& history = WorldHistory(system->region);
  const std::string name = system->name;
  const std::string line = history.size() > 1 ? history[1] : std::string();

  ClampStat("energy", -2);
  state_.sim.broadcasts[system->id] = true;
  PerformAction("Local traffic report received");
  if (!state_.gameOver.empty()) { return true; }
  if (!line.empty()) { SimRemember("World", name + ": " + line); }
  return true;
}

bool Simulation::FinishSignal(const std::string& choice) {
  if (!state_.gameOver.empty() || state_.sim.signal != "request") { return false; }
  if (choice != "preserved" && choice != "sold") { return false; }
  if (choice == "preserved") {
    if (map_.here != "palimpsest-site" || state_.repairKits < 1 || StatValue("energy") <= 5) {
      return false;
    }
  } else if (!state_.docked) {
    return false;
  }

  state_.sim.signal = choice;
  if (choice == "preserved") {
    state_.repairKits -= 1;
    ClampStat("energy", -5);
    bool filed = false;
    for (const SurveyRecord& record : survey_) {
      if (record.id == "silicon-marrow") { filed = true; }
    }
    if (!filed) {
      survey_.push_back(SurveyRecord{"silicon-marrow", "The sheltered archive", "Signal", 360,
                                     "filed"});
    }
    for (std::size_t i = 0; i < crew_.size(); ++i) {
      crew_[i].morale = std::min(100.0, crew_[i].morale + 5.0);
      for (std::size_t j = i + 1; j < crew_.size(); ++j) {
        SimChangeBond(crew_[i].id, crew_[j].id, 5);
      }
    }
    SimRemember("Choice", "The crew sheltered the signal at the Archive Hull.");
    PerformAction("Silicon Marrow's archive was sheltered");
  } else {
    state_.shiplets += 900;
    for (CrewMember& crew : crew_) { crew.morale = std::max(0.0, crew.morale - 8.0); }
    SimRemember("Choice", "The signal was sold despite its request not to be copied.");
    PerformAction("Silicon Marrow's signal was sold");
  }
  CheckCommendations();
  return true;
}

}  // namespace ship

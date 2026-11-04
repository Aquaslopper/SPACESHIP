// ============================================================================
//  Awards.cpp — commendations are earned by playing, never by looking.
//  ---------------------------------------------------------------------------
//  Each commendation names a metric and a goal; the chronicle holds the history
//  that answers it. Earning one only marks it: the reward waits in the log until
//  the player claims it, and claiming is free and once-only.
// ============================================================================
#include "core/Simulation.h"

#include <string>

namespace ship {

double Simulation::ChronicleValue(const std::string& metric) const {
  const Chronicle& chronicle = state_.chronicle;

  if (metric == "systems" || metric == "regions") {
    const std::map<std::string, bool>& map =
        metric == "systems" ? chronicle.systems : chronicle.regions;
    double count = 0;
    for (const auto& entry : map) {
      if (entry.second) { count += 1; }
    }
    return count;
  }
  if (metric == "scans") {
    double count = 0;
    for (const Body& body : catalog_.bodies) {
      auto it = state_.scanned.find(body.id);
      if (it != state_.scanned.end() && it->second) { count += 1; }
    }
    return count;
  }
  if (metric == "contracts") {
    double count = 0;
    for (const Contract& contract : catalog_.contracts) {
      auto it = state_.contracts.find(contract.id);
      if (it != state_.contracts.end() && it->second == "complete") { count += 1; }
    }
    return count;
  }
  if (metric == "commissions") { return state_.commissions.completed; }
  if (metric == "actions") { return actionCounter_; }
  if (metric == "berth") {
    auto it = state_.contracts.find("arrival");
    const std::string value = it == state_.contracts.end() ? std::string() : it->second;
    return (value == "ready" || value == "complete") ? 1.0 : 0.0;
  }
  if (metric == "signal") {
    return (state_.sim.signal == "preserved" || state_.sim.signal == "sold") ? 1.0 : 0.0;
  }
  if (metric == "banks") { return chronicle.banks; }
  if (metric == "projects") { return chronicle.projects; }
  if (metric == "dreams") { return chronicle.dreams; }
  if (metric == "meals") { return chronicle.meals; }
  if (metric == "incidents") { return chronicle.incidents; }
  return 0.0;
}

void Simulation::CheckCommendations(bool silent) {
  bool changed = false;
  for (const Commendation& award : catalog_.commendations) {
    auto earned = state_.chronicle.earned.find(award.id);
    const bool already = earned != state_.chronicle.earned.end() && earned->second;
    if (already) { continue; }
    if (ChronicleValue(award.metric) < award.goal) { continue; }
    state_.chronicle.earned[award.id] = true;
    changed = true;
    if (!silent) {
      LogLine("Commendation earned: " + award.title + ". Claim its reward in Rewards.");
    }
  }
  (void)changed;
}

}  // namespace ship

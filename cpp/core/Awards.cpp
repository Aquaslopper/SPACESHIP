// ============================================================================
//  Awards.cpp — commendations are earned by playing, never by looking.
//  ---------------------------------------------------------------------------
//  Each commendation names a metric and a goal; the chronicle holds the history
//  that answers it. Earning one only marks it: the reward waits in the log until
//  the player claims it, and claiming is free and once-only.
// ============================================================================
#include "core/Simulation.h"

#include <string>
#include <vector>

#include "core/JsNumber.h"

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
  if (metric == "banks") { return chronicle.banks; }
  if (metric == "projects") { return chronicle.projects; }
  if (metric == "meals") { return chronicle.meals; }
  if (metric == "incidents") { return chronicle.incidents; }
  return 0.0;
}

const Commendation* Simulation::FindCommendation(const std::string& id) const {
  for (const Commendation& award : catalog_.commendations) {
    if (award.id == id) { return &award; }
  }
  return nullptr;
}

double Simulation::ReadyCommendations() const {
  double ready = 0;
  for (const Commendation& award : catalog_.commendations) {
    auto earned = state_.chronicle.earned.find(award.id);
    auto claimed = state_.chronicle.claimed.find(award.id);
    const bool has = earned != state_.chronicle.earned.end() && earned->second;
    const bool took = claimed != state_.chronicle.claimed.end() && claimed->second;
    if (has && !took) { ready += 1; }
  }
  return ready;
}

double Simulation::EarnedCommendations() const {
  double earned = 0;
  for (const auto& entry : state_.chronicle.earned) {
    if (entry.second) { earned += 1; }
  }
  return earned;
}

std::string Simulation::CaptainRank() const {
  // Cosmetic, as the reference says: a name for how much of the log is filled.
  const double earned = EarnedCommendations();
  const double total = static_cast<double>(catalog_.commendations.size());
  if (earned >= total && total > 0) { return "Constellation"; }
  if (earned >= 10) { return "Voyager"; }
  if (earned >= 6) { return "Wayfinder"; }
  if (earned >= 3) { return "Keeper"; }
  return "New Watch";
}

std::string Simulation::RewardText(const std::string& id) const {
  return RewardTextOf(*FindCommendation(id));
}

std::string Simulation::RewardTextOf(const Commendation& award) const {
  const Reward& reward = award.reward;
  std::vector<std::string> parts;
  // The reference says "shiplets" even for one; the stores' words are plural
  // only when the count is.
  if (reward.cash > 0) { parts.push_back(js::NumToString(reward.cash) + " shiplets"); }
  const auto push = [&parts](double amount, const std::string& word) {
    if (amount <= 0) { return; }
    parts.push_back(js::NumToString(amount) + " " + word + (amount == 1.0 ? "" : "s"));
  };
  push(reward.fuel, std::string("fuel cell"));
  push(reward.kits, std::string("repair kit"));
  push(reward.med, std::string("medkit"));
  std::string out;
  for (const std::string& part : parts) { out += (out.empty() ? "" : " + ") + part; }
  return out;
}

bool Simulation::ClaimCommendation(const std::string& id) {
  const Commendation* award = FindCommendation(id);
  if (award == nullptr || !state_.gameOver.empty()) { return false; }
  auto earned = state_.chronicle.earned.find(id);
  if (earned == state_.chronicle.earned.end() || !earned->second) { return false; }
  auto claimed = state_.chronicle.claimed.find(id);
  if (claimed != state_.chronicle.claimed.end() && claimed->second) { return false; }

  state_.chronicle.claimed[id] = true;
  state_.shiplets += award->reward.cash;
  state_.fuelCells += award->reward.fuel;
  state_.repairKits += award->reward.kits;
  state_.medkits += award->reward.med;
  // Free: a reward for work already done is not another turn of work.
  LogLine("Reward claimed: " + award->title + " - " + RewardTextOf(*award) + ".");
  return true;
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

#include <algorithm>

#include "core/Simulation.h"

namespace ship {

void Simulation::TickButtonCharges() {
  const std::string pressed = pressedButton_;
  pressedButton_.clear();
  // Only a press of a control is a click.
  // PLAYER CLICKS ON BUTTON ADVANCED THE TIME PLUS ONE CLICK
  if (pressed.empty()) { return; }
  for (auto& entry : state_.charges) {
    Charge& charge = entry.second;
    if (entry.first == pressed) {
      charge.left = std::max(0.0, charge.left - 1.0);
    } else if (entry.first == kEngineButtonChargeKey && state_.engineerRun &&
               AssignedEngineer() != nullptr) {
      continue;
    } else {
      charge.left = std::min(charge.volume, charge.left + 1.0);
    }
  }
}

double Simulation::EngineButtonChargeLeft() const {
  auto it = state_.charges.find(kEngineButtonChargeKey);
  if (it != state_.charges.end()) { return it->second.left; }
  auto rule = rules_.charges.find("systems");
  return rule == rules_.charges.end() ? 4.0 : rule->second.volume;
}

bool Simulation::TrySpendEngineButton() {
  auto it = state_.charges.find(kEngineButtonChargeKey);
  if (it == state_.charges.end()) {
    auto rule = rules_.charges.find("systems");
    const double volume = rule == rules_.charges.end() ? 4.0 : rule->second.volume;
    Charge charge;
    charge.volume = volume;
    charge.left = volume;
    it = state_.charges.emplace(kEngineButtonChargeKey, charge).first;
  }
  if (it->second.left < 1.0) { return false; }
  it->second.left = std::max(0.0, it->second.left - 1.0);
  return true;
}

void Simulation::SetPressedButton(const std::string& key) { pressedButton_ = key; }

void Simulation::ClearPressedButton(const std::string& key) {
  if (pressedButton_ == key) { pressedButton_.clear(); }
}

}  // namespace ship

// ============================================================================
//  Charges.cpp — the click time every control holds.
//  ---------------------------------------------------------------------------
//  Each control carries a volume of presses and starts full. A press spends one
//  step of the control that paid and returns one step to every other control,
//  so working elsewhere winds the buttons back up. Nothing here counts seconds
//  and nothing here is a lock: an empty control is empty of clicks, not shut.
// ============================================================================
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
    } else {
      charge.left = std::min(charge.volume, charge.left + 1.0);
    }
  }
}

void Simulation::SetPressedButton(const std::string& key) { pressedButton_ = key; }

void Simulation::ClearPressedButton(const std::string& key) {
  if (pressedButton_ == key) { pressedButton_.clear(); }
}

}  // namespace ship

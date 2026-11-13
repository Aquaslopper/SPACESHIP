#include <algorithm>

#include "core/Simulation.h"

namespace ship {

// There are no button charges any more. A button does not hold a counter that
// has to be spent down or waited out: pressing it does the thing, and the cost
// is whatever the action itself pays. The counters are kept full so that the
// readers that still paint a bar describe an always-ready control.
void Simulation::TickButtonCharges() { pressedButton_.clear(); }

double Simulation::EngineButtonChargeLeft() const {
  auto rule = rules_.charges.find("systems");
  return rule == rules_.charges.end() ? 4.0 : rule->second.volume;
}

bool Simulation::TrySpendEngineButton() { return true; }

void Simulation::SetPressedButton(const std::string& key) { pressedButton_ = key; }

void Simulation::ClearPressedButton(const std::string& key) {
  if (pressedButton_ == key) { pressedButton_.clear(); }
}

}  // namespace ship

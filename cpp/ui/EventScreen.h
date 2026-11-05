// ============================================================================
//  EventScreen.h — an incident report (Section I).
//  ---------------------------------------------------------------------------
//  A report names its stage, its countdown and the two ways out: a quick remedy
//  that may cost a repair kit, or a hand for six actions of work. Ignoring it
//  is always allowed — the situation advances on the same clock as everything
//  else.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"

namespace ship {

struct EventScreenModel {
  TaskHead head;
  NavPanel report;
  std::vector<ActionRowView> rows;
  std::vector<ProgressButton> buttons;
  bool known = false;
};

EventScreenModel BuildEventScreen(const Simulation& sim, const std::string& eventId);

bool PressEventAction(Simulation& sim, const std::string& action,
                      const std::string& arg = std::string());

}  // namespace ship

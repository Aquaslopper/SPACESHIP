// ============================================================================
//  AwardsScreen.h — the logbook (Section N).
//  ---------------------------------------------------------------------------
//  Fourteen commendations, the condition behind each, and the reward a claim
//  pays. Claiming is free and once only; the rank above the list is cosmetic
//  and is nothing but a name for how much of the log is filled.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"

namespace ship {

struct AwardsScreenModel {
  TaskHead head;
  NavPanel summary;                    // Captain's log
  std::vector<ActionRowView> rows;     // one per commendation: "claim:<id>"
};

AwardsScreenModel BuildAwardsScreen(const Simulation& sim);

bool PressAwardsAction(Simulation& sim, const std::string& action,
                       const std::string& arg = std::string());

}  // namespace ship

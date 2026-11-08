// ============================================================================
//  MessScreen.h — the galley (Section I-4c).
//  ---------------------------------------------------------------------------
//  A fixed 600x360 galley: cooked-food stock, four plates on separate hunger
//  clocks, and the dry-ration input rack beneath the crew.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "ui/Widgets.h"

namespace ship {

struct PlateView {
  std::string id;
  std::string name;
  double tickPercent = 0.0;   // how far through this hand's own clock
  std::string sub;            // "N clicks to next meal"
  std::string tone;           // terracotta at 70 hunger, sage below
};

struct MessScreenModel {
  TaskScreenModel screen;
  double rackPercent = 0.0;
  std::string rackTone;
  std::string rackReadout;
  std::vector<PlateView> plates;
  bool full = false;
};

MessScreenModel BuildMessScreen(const Simulation& sim);

// The stage alone, including the live cooked-food and dry-ration stock icons.
TaskStage BuildMessMachine(const Simulation& sim);

}  // namespace ship

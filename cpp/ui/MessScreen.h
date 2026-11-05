// ============================================================================
//  MessScreen.h — the galley (Section I-4c).
//  ---------------------------------------------------------------------------
//  A fixed 600x360 counter: the rack gauge, and four plates - one per hand, each
//  on its own hunger clock. One moving part, watched by five clocks at once.
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
  std::string sub;            // "N actions to next meal"
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

// The stage alone, for counting: 28 scenery parts.
TaskStage BuildMessMachine(const Simulation& sim);

}  // namespace ship

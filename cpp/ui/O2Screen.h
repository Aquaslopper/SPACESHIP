// ============================================================================
//  O2Screen.h — the filter room (Section I-4).
//  ---------------------------------------------------------------------------
//  A fixed 600x360 stage: an intake duct, the cabin-air chamber, three slot
//  housings with their wells, canisters, necks, state plates and charge bars,
//  and the outlet manifold to the decks. Only the slots and the bank are live.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "ui/Widgets.h"

namespace ship {

// The cabin-air chamber, rebuilt from airStage()/airPercent().
struct AirChamber {
  std::string word;
  std::string tone;
  std::string sub;        // "N filtered" or "N of 20 left"
  double fillPercent = 0.0;
  int notches = 3;
  bool running = false;
  int dust = 0;           // (4 - stage) * 4 specks
  int streaks = 0;        // four clean streaks while the bank runs
};

struct SlotView {
  std::string id;
  std::string name;
  std::string state;      // empty | installed | active
  std::string plate;      // EMPTY / INSTALLED / ACTIVE / NO POWER
  std::string tone;
  double barPercent = 0.0;
  bool canisterVisible = false;
  bool live = false;      // the button under it is wired
};

struct O2ScreenModel {
  TaskScreenModel screen;
  AirChamber air;
  std::vector<SlotView> slots;
  bool running = false;
  bool ready = false;
  double filterLife = 0.0;
  double airPercent = 0.0;
};

O2ScreenModel BuildO2Screen(const Simulation& sim);

// The stage alone, for counting: 44 scenery parts and three live controls.
TaskStage BuildFilterMachine(const Simulation& sim);

}  // namespace ship

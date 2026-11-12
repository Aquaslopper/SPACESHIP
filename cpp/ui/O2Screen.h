#pragma once

#include <string>
#include <vector>

#include "ui/Widgets.h"

namespace ship {

struct AirChamber {
  std::string word;
  std::string tone;
  std::string sub;
  double fillPercent = 0.0;
  int notches = 3;
  bool running = false;
  int dust = 0;
  int streaks = 0;
};

struct SlotView {
  std::string id;
  std::string name;
  std::string state;
  std::string plate;
  std::string tone;
  double barPercent = 0.0;
  bool canisterVisible = false;
  bool live = false;
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

TaskStage BuildFilterMachine(const Simulation& sim);

}  // namespace ship

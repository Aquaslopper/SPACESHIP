#pragma once

#include <string>
#include <vector>

#include "ui/Widgets.h"

namespace ship {

struct PlateView {
  std::string id;
  std::string name;
  double tickPercent = 0.0;
  std::string sub;
  std::string tone;
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

TaskStage BuildMessMachine(const Simulation& sim);

}  // namespace ship

#pragma once

#include <string>

#include "ui/Widgets.h"

namespace ship {

struct EngineScreenModel {
  TaskScreenModel screen;
  std::string tone;
  std::string word;
  float meterWidth = 420.0f;
  int condition = 100;
};

EngineScreenModel BuildEngineScreen(const Simulation& sim);

TaskStage BuildEngineMachine(const Simulation& sim);

std::string EngineConditionTone(int condition);
std::string EngineConditionWord(int condition);

}  // namespace ship

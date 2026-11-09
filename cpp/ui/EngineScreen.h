// ============================================================================
//  EngineScreen.h — the drive, as a machine and as a window.
//  ---------------------------------------------------------------------------
//  Section I-3. The stage is a fixed 650x390 illustration built from a table of
//  absolutely positioned parts: chassis brackets, a riveted pipe run with five
//  valves, a closed coolant loop, the core with its encoder ring and bolts, a
//  flywheel and turbine, a finned heat exchanger, two dial gauges, a lamp bus, a
//  statistics plate, a sixteen-cylinder bank, and the one live reading - the
//  condition meter.
//
//  Only the meter, the percentage and the word beneath it are connected to the
//  voyage. Everything else is flavour, and the model says so by carrying no
//  state for it.
// ============================================================================
#pragma once

#include <string>

#include "ui/Widgets.h"

namespace ship {

struct EngineScreenModel {
  TaskScreenModel screen;
  std::string tone;        // the condition colour
  std::string word;        // NOMINAL / SERVICEABLE / DEGRADED / CRITICAL
  float meterWidth = 420.0f;  // round(420 * condition/100)
  int condition = 100;
};

EngineScreenModel BuildEngineScreen(const Simulation& sim);

// The machine alone, so a test can count its parts without the window.
TaskStage BuildEngineMachine(const Simulation& sim);

// conditionTone()/conditionWord(): 75 / 50 / 25 thresholds, exactly.
std::string EngineConditionTone(int condition);
std::string EngineConditionWord(int condition);

}  // namespace ship

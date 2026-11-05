// ============================================================================
//  SimCoreScreen.h — the Simulacron core (Section M-2).
//  ---------------------------------------------------------------------------
//  A bounded, offline memory engine: real voyage events become memories, two
//  memories become an insight, and the crew's name anchors and desire fire are
//  read from their biographies and their present needs. Nothing here invents a
//  thought, and nothing here is a wall-clock timer.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"

namespace ship {

struct SimScreenModel {
  TaskHead head;
  NavPanel body;               // insight, unconsolidated, sensor, signal
  std::vector<ActionRowView> rows;      // dream pairs, the relay door
  std::vector<ProgressButton> buttons;  // the dream engine's own button
  std::vector<std::string> anchors;     // "<name> / <anchor>: <drive>"
  std::string latestDream;
  std::vector<std::string> memories;    // newest first, "#at / kind: text"
};

SimScreenModel BuildSimCoreScreen(const Simulation& sim);

// "dream:<firstId>:<secondId>", "open-comms".
bool PressSimCoreAction(Simulation& sim, const std::string& action,
                        const std::string& arg = std::string());

}  // namespace ship

// ============================================================================
//  StationScreen.h — the port window (Section M).
//  ---------------------------------------------------------------------------
//  "Browse freely. Each transaction, contract or departure costs one action."
//  Looking at a market row, reading a contract or sizing up a hire costs
//  nothing; every command row is one action, and the row's own enabled flag is
//  the whole rule (purse, hold, berths, a dock that is still there).
//
//  Same three columns as the reference: buy on the left, sell and the mission
//  board in the middle, the yard, the hiring hall and the tavern on the right.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Shell.h"
#include "ui/Layout.h"
#include "ui/Widgets.h"

namespace ship {

// One row in a station column: a command, or a piece of plain reading.
struct StationRow {
  std::string id;        // the press id: "buy-fuel", "sell:<lot>", "contract:<id>"
  std::string label;
  std::string cost;      // "1 action", "free", "done"
  std::string note;      // the small line under a contract or a hire
  std::string confirm;   // the question asked first when confirmations are on
  bool enabled = false;
  bool reading = false;  // a note, not a button
  bool live = false;
};

struct StationColumn {
  std::string title;
  std::vector<std::string> notes;
  std::vector<StationRow> rows;
};

struct StationScreenModel {
  TaskHead head;
  std::string portId;
  std::string portName;
  std::string tip;
  std::vector<StationColumn> columns;  // Market | Sales + Board | Yard + Crew + Tavern
  std::string purseLine;
  std::string holdLine;
  bool docked = false;
};

StationScreenModel BuildStationScreen(const Simulation& sim, const std::string& portId);

// The press path. Every accepted command spends exactly one action through the
// simulation's own economy calls.
bool PressStationAction(Simulation& sim, const std::string& action,
                        const std::string& arg = std::string());

}  // namespace ship

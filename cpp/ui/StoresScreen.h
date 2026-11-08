// ============================================================================
//  StoresScreen.h — the cargo hold and the science room.
//  ---------------------------------------------------------------------------
//  Both rooms are one panel and a list, exactly as the reference builds them:
//  the hold counts what is in it and lets a bay recover salvage during a
//  crossing; the science room files deep scans and packages them, which costs
//  one action now and two more of work.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"

namespace ship {

struct StoreRowView {
  std::string id;          // "jettison:<lot>" / "salvage:<bay>" / "package:<record>"
  std::string label;
  std::string cost;
  std::string note;
  bool enabled = false;
  bool packaged = false;
  bool progress = false;   // a three-state button rather than a command row
  bool readingOnly = false;
  std::string confirm;     // asked first when confirmations are on
};

struct StoresScreenModel {
  TaskHead head;
  std::string notice;
  std::string tally;       // the hold count, or the survey count
  std::string hint;        // the salvage paragraph, cargo only
  std::vector<StoreRowView> rows;          // cargo: inventory sorting rows
  std::vector<ProgressButton> buttons;    // cargo: salvage bays; science: package
  bool cargo = false;

  // Cargo Hold rebuild: left inventory, middle visual volume, right context.
  std::string contextTitle;
  std::vector<std::string> contextNotes;
  std::vector<StoreRowView> tradeRows;
  CargoVisualModel visual;
};

StoresScreenModel BuildStoresScreen(const Simulation& sim, const std::string& roomId);

bool PressStoresAction(Simulation& sim, const std::string& action,
                       const std::string& arg = std::string());

}  // namespace ship

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
  std::vector<StoreRowView> rows;
  std::vector<ProgressButton> buttons;
  bool cargo = false;
};

StoresScreenModel BuildStoresScreen(const Simulation& sim, const std::string& roomId);

// ---------------------------------------------------------------------------
//  The cargo hold
//  ---------------------------------------------------------------------------
//  Three windows, as the room is laid out: what is aboard on the left, the hold
//  itself in the middle, and on the right the one thing that can be done where
//  the ship is standing — hand a run over at its destination, or throw cargo out
//  of the lock.
struct CargoLotView {
  std::string id;       // "lot-7"
  std::string name;
  std::string note;     // the commodity's kind, as the manifest reads it
  double vol = 0;
  bool stowed = false;  // already clicked into the hold
  bool contract = false;  // carried for a run, not for sale
};

// One block in the hold's picture.
struct CargoHoldLot {
  std::string id;
  std::string label;
  double vol = 0;
  bool contract = false;
};

// One thing the right-hand window offers.
struct CargoSideRow {
  std::string id;  // "cargo-settle:<mode>:<to>:<good>" / "cargo-unload:<lot>"
  std::string label;
  std::string note;
  bool enabled = true;
};

struct CargoRoomModel {
  TaskHead head;
  // left: everything aboard
  std::vector<CargoLotView> lots;
  std::string inventoryNote;
  // middle: the hold itself
  std::vector<CargoHoldLot> hold;
  double used = 0;
  double capacity = 0;
  std::string capacityLine;
  std::string statusLine;
  std::string statusNote;
  // right: the window that changes with the run in hand
  std::string sideTitle;
  std::string sideNote;
  std::vector<CargoSideRow> sideRows;
  // the salvage bays stay reachable from this room while under way, as rows of
  // the same window rather than a separate bar
  std::vector<CargoSideRow> salvageRows;
};

CargoRoomModel BuildCargoRoom(const Simulation& sim);

bool PressCargoRoom(Simulation& sim, const std::string& action,
                    const std::string& arg = std::string());

bool PressStoresAction(Simulation& sim, const std::string& action,
                       const std::string& arg = std::string());

}  // namespace ship

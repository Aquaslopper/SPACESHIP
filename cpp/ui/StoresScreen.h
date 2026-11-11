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
//  Three windows: left inventory cards, center visual hold sized by ship
//  capacity (select left -> green glow in hold -> click green crate to sort),
//  and right context-sensitive finalizer (delivery / freelance / station /
//  jettison).
struct CargoSelection {
  std::string lotId;     // selected unsorted lot on the left (glows green in hold)
  std::string tradeKey;  // selected active run when multiple end at this body
};

struct CargoLotView {
  std::string id;         // "lot-7"
  std::string name;
  std::string note;       // the commodity's kind, as the manifest reads it
  std::string detail;     // contract destination or status detail
  double vol = 0;
  int qty = 1;
  bool stowed = false;    // already sorted cleanly in the hold
  bool selected = false;  // selected on the left, glowing green in the hold
  bool contract = false;  // carried for a run, not for sale
  bool risk = false;
  std::string contractKey;
};

// One visual crate in the center hold bay.
struct CargoHoldLot {
  std::string id;
  std::string label;
  std::string kind;
  double vol = 0;
  bool stowed = false;
  bool selected = false;
  bool contract = false;
  bool risk = false;
  float jitterX = 0.0f;
  float jitterY = 0.0f;
  float tiltDeg = 0.0f;
};

struct CargoRunTab {
  std::string key;
  std::string mode;
  std::string label;
  bool selected = false;
};

// One thing the right-hand context window offers.
struct CargoSideRow {
  std::string id;       // "cargo-settle:<mode>:<to>:<good>[:<tier>]" / "cargo-unload:<lot>"
  std::string label;
  std::string note;
  std::string confirm;  // asked first when confirmations are on (e.g. jettison)
  bool enabled = true;
};

struct CargoRoomModel {
  TaskHead head;
  // left: everything aboard
  std::vector<CargoLotView> lots;
  std::string inventoryNote;
  std::string selectedLotId;
  // middle: the hold itself
  std::vector<CargoHoldLot> hold;
  std::string shipLabel;
  double used = 0;
  double capacity = 0;
  std::string capacityLine;
  std::string statusLine;
  std::string statusNote;
  // right: context-sensitive finalizer
  std::string contextKind;  // "delivery", "freelance", "station", "jettison"
  std::string activeTradeKey;
  std::vector<CargoRunTab> runTabs;
  std::vector<std::string> contextDetails;
  std::string sideTitle;
  std::string sideNote;
  std::vector<CargoSideRow> sideRows;
};

void NormalizeCargoSelection(const Simulation& sim, CargoSelection& selection);
CargoRoomModel BuildCargoRoom(const Simulation& sim, CargoSelection* selection = nullptr);

bool PressCargoRoom(Simulation& sim, const std::string& action,
                    const std::string& arg = std::string());

bool PressStoresAction(Simulation& sim, const std::string& action,
                       const std::string& arg = std::string());

}  // namespace ship

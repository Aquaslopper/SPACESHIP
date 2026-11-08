// ============================================================================
//  StationScreen.h — the port window (Section M).
//  ---------------------------------------------------------------------------
//  The card walk is Market, Sales, Star-map, Trade contracts, Shipyard/crew.
//  Reading and choosing are free. Only a commitment advances the clock:
//  selling and accepting trade cost one action; supplies, refits and hires are
//  money-only. Accepted trade buttons grey out from the saved contract state.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Shell.h"
#include "ui/Layout.h"
#include "ui/Widgets.h"

namespace ship {

// One row in the middle column: a thing the reader can pick, or plain reading.
struct StationRow {
  std::string id;        // the press id: "buy-fuel", "sell:<lot>", "contract:<id>"
  std::string label;     // the middle-column item, price included
  std::string cost;      // "0 actions", "1 action", "done"
  std::string note;      // the small line under a contract or a hire
  std::string confirm;   // the question asked first when confirmations are on
  bool enabled = false;  // the commit button on the right is live
  bool reading = false;  // a note, not an item: nothing to pick, nothing to press
  bool live = false;

  // ---- the far-right inspector ------------------------------------------
  std::string commit;                 // the one button's own label
  std::vector<std::string> details;   // everything known about this one item
  bool free = false;    // shiplets only: no action is spent, no lock is started
  bool done = false;    // owned, hired, sold, accepted or already spoken to
};

// One category card on the left, and the items it opens in the middle.
struct StationColumn {
  std::string id;      // "market", "sales", "starmap", "trade", "yard"
  std::string title;
  std::string badge;   // the card's own second line: the purse, the tally
  std::vector<std::string> notes;
  std::vector<StationRow> rows;
};

// Which card is open and which item is picked. The renderer owns it: choosing
// costs nothing and is not part of the voyage, so it is not saved.
struct StationSelection {
  std::string category;
  std::string item;
  std::string offer;     // the picked offer inside the chosen trade kind
};

struct StationScreenModel {
  TaskHead head;
  std::string portId;
  std::string portName;
  std::string tip;
  std::vector<StationColumn> columns;  // Market | Sales | Star-map | Trade | Yard
  // Populated only for the Trade contracts card. The middle column holds the
  // three kinds; this is the right-hand board for the chosen kind.
  StationColumn offers;
  StationRow tradeCommit;
  std::string purseLine;
  std::string holdLine;
  bool docked = false;

  // The selection as this frame resolves it, after normalisation: an item that
  // has been sold or a card that no longer exists cannot stay picked.
  StationSelection selection;
};

StationScreenModel BuildStationScreen(const Simulation& sim, const std::string& portId);

// Clamps a stored selection to what the model actually offers, and picks the
// first item of the first card when nothing (or nothing valid) is picked yet.
void NormalizeStationSelection(const StationScreenModel& model, StationSelection& selection);

const StationColumn* FindStationCategory(const StationScreenModel& model,
                                         const std::string& categoryId);
const StationRow* FindStationItem(const StationScreenModel& model,
                                  const std::string& categoryId,
                                  const std::string& itemId);

// The press path. Money-only rows spend shiplets and nothing else; the rows
// that cost an action spend exactly one, through the simulation's own calls.
bool PressStationAction(Simulation& sim, const std::string& action,
                        const std::string& arg = std::string());

// What the rule table would have chosen for this row, kept for the audit that
// reads the station's labels; the station itself starts no cooldown.
std::string StationActionCategory(const Simulation& sim, const std::string& action);

}  // namespace ship

// ============================================================================
//  StationScreen.h — the port window (Section M).
//  ---------------------------------------------------------------------------
//  Three columns, walked in one direction: a category card on the left, the
//  things that category holds in the middle, and everything about the one thing
//  the reader picked — plus its single commit button — on the right.
//
//  Reading and choosing are free. Committing is not one rule but two:
//
//    Market, Shipyard and crew   shiplets only. No action, no cooldown. The
//                                button stays live while the purse and the
//                                hold/rack allow it, and greys out when they
//                                do not, or once a one-time thing is owned.
//    Cargo and survey data       pays shiplets, costs one action. The lot is
//                                gone afterwards, so the row cannot repeat.
//    Contract board              accepts the contract, costs one action. The
//                                button greys out the moment it is accepted.
//    Starmap                     free, and there is nothing to commit: the
//                                station's page for its own system, read one
//                                planet at a time.
//
//  Nothing on this window carries an action cooldown: the state the commit
//  itself changes is what stops a second press, not a lock on the button.
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
  std::string cost;      // "0 clicks", "1 click", "done"
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
  bool page = false;    // a starmap entry: read-only, nothing to commit
};

// One body written into a station's starmap: the name, the kind and the
// reading the chart authored for it.
struct StarmapEntry {
  std::string id;
  std::string name;
  std::string kind;
  std::string group;    // "Star", "Planets", "Moons", "Belts and wrecks", "Stations"
  std::vector<std::string> reading;
};

// The page a station writes for the system it sits in. There is no authored
// copy of this anywhere: BuildStarmapPage() writes it from the chart, so every
// station the voyage can reach has one, and each one describes its own system.
struct StarmapPage {
  std::string systemId;
  std::string systemName;
  std::size_t planetCount = 0;
  std::vector<std::string> systemReading;
  std::vector<StarmapEntry> entries;
};

// One offer on a station's trade board. The station-to-station mode fills its
// list from the stations already visited; the other two draw their offers in
// the pass that follows.
struct TradeOffer {
  std::string mode;       // "delivery", "freelance", "station"
  std::string to;         // the destination the offer points at
  std::string good;       // the commodity carried; empty for a data survey run
  std::string label;      // the row in the right-hand list
  std::string note;       // the line under that row
  bool accepted = false;  // already taken: the button is grey
};

// One of the three buttons in the trade card's middle column.
struct TradeMode {
  std::string id;     // "delivery", "freelance", "station"
  std::string title;
  std::string note;   // what the mode is for
  std::string empty;  // what the right panel says when the list is empty
  std::vector<TradeOffer> offers;
};

// One category card on the left, and the items it opens in the middle.
struct StationColumn {
  std::string id;      // "market", "starmap", "sales", "board", "yard", "tavern"
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
};

struct StationScreenModel {
  TaskHead head;
  std::string portId;
  std::string portName;
  std::string tip;
  std::vector<StationColumn> columns;  // Market | Starmap | Trade | Sales | Board | Yard | Tavern
  // The trade card's three buttons and the lists behind them.
  std::vector<TradeMode> tradeModes;
  std::string purseLine;
  std::string holdLine;
  bool docked = false;

  // The selection as this frame resolves it, after normalisation: an item that
  // has been sold or a card that no longer exists cannot stay picked.
  StationSelection selection;
};

StationScreenModel BuildStationScreen(const Simulation& sim, const std::string& portId);

// The station's starmap: the system it stands in, read out of the chart. Every
// station writes its own; nothing is authored per port and nothing is cached,
// so a page cannot go stale or be shared between stations.
StarmapPage BuildStarmapPage(const Simulation& sim, const std::string& portId);

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

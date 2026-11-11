// ============================================================================
//  QuartersScreen.h — the order sheet (Section I-5).
//  ---------------------------------------------------------------------------
//  Looking at a crew member is free. Writing an order down is free. Changing
//  your mind is free. Orders cost an action when the sheet is handed out;
//  conversations are separate paid actions that improve morale.
//
//  The price is the point. An order's price is also how long it takes to carry
//  out, so the bottom button counts the lot down as one bar, and nobody takes
//  new orders until it is empty.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/Simulation.h"
#include "ui/CrewSprites.h"
#include "ui/Layout.h"
#include "ui/Widgets.h"

namespace ship {

struct CrewRowView {
  std::string id;
  std::string name;
  std::string colour;
  std::string role;    // shortRole(role) + " - " + job label
  std::string flag;    // "" / "order" / the busy countdown
  bool selected = false;
  bool busy = false;
  bool hasOrder = false;
};

struct OrderLineView {
  std::string id;
  std::string label;
  std::string note;
  std::string cost;     // "3" or "3 &middot; 30 sh" as the pad prints it
  std::string state;    // "" / "on" / "off"
  bool on = false;
  bool locked = false;
  double actions = 0;
  double fee = 0;
};

struct QuartersScreenModel {
  TaskScreenModel screen;          // head, panels, band, buttons
  std::vector<CrewRowView> roster;
  std::vector<std::string> jobOptions;   // id + "\t" + label, in JOBS order
  std::vector<OrderLineView> orders;
  std::string selectedJob;
  std::string jobCostLine;
  std::string closedText;          // the pad is shut: why
  bool closed = false;
  std::string sheetText;
  std::string bandNote;
  CommandRowView conversation;
  std::string said;
  std::string payWages, payEvery, payNext, payPurse;
  CrewScene scene;
  std::vector<MachinePart> hall;   // the corridor's scenery, in stage units
  // The portrait card beside the corridor and the conversation control beside
  // the figure, in stage units; and whose card it is.
  Area card;
  Area talk;
  std::string cardName, cardRole, cardColour;
  std::string selectedId;
  bool hasCrew = false;
};

QuartersScreenModel BuildQuartersScreen(const Simulation& sim);

// Selection and staging are free. Committing orders and talking each cost an action.
bool PressQuartersAction(Simulation& sim, const std::string& action,
                         const std::string& arg = std::string());

}  // namespace ship

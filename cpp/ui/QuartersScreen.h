// ============================================================================
//  QuartersScreen.h — the order sheet (Section I-5).
//  ---------------------------------------------------------------------------
//  Looking at a crew member is free. Writing an order down is free. Changing
//  your mind is free. NOTHING is spent until the whole sheet is handed out at
//  the bottom of the screen: one mark, one feed line, one payment.
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
  std::string talkLabel;
  std::string said;
  std::string payWages, payEvery, payNext, payPurse;
  CrewScene scene;
  std::vector<MachinePart> hall;   // 560x300 of scenery
  std::string selectedId;
  bool hasCrew = false;
};

QuartersScreenModel BuildQuartersScreen(const Simulation& sim);

// What a click in the quarters does. Selection, staging and talking are free;
// "commit" is the one action the screen spends, and "talk" is free by design.
bool PressQuartersAction(Simulation& sim, const std::string& action,
                         const std::string& arg = std::string());

}  // namespace ship

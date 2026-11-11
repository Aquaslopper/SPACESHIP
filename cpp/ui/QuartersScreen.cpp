// ============================================================================
//  QuartersScreen.cpp — the hallway, the roster and the order sheet.
// ============================================================================
#include "ui/QuartersScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <map>

namespace ship {

namespace {

std::string FirstName(const std::string& name) {
  const std::size_t space = name.find(' ');
  return space == std::string::npos ? name : name.substr(0, space);
}

// shortRole(): the role up to " /", which is the descriptor's short form.
std::string ShortRole(const std::string& role) {
  const std::size_t cut = role.find(" /");
  return cut > std::string::npos && cut > 0 ? role.substr(0, cut)
                                            : (cut > 0 ? role.substr(0, cut) : role);
}

std::string JobLabel(const Simulation& sim, const std::string& id) {
  const JobDef* job = sim.FindJobDef(id);
  return job == nullptr ? "None" : job->label;
}

// The corridor: a door at the far end, a stretch of bare wall and a locker
// either side of it, a light strip overhead. All scenery, no clicks. Wall and
// floor run the width of the stage; the portrait card covers their left part.
// A box that carries text is as tall as the reading size the stage is laid
// out for, and stands where no prop is put in front of it.
std::vector<MachinePart> Hallway() {
  std::vector<MachinePart> parts;
  const auto part = [&parts](const std::string& cls, float x, float y, float w,
                             float h, const std::string& text = std::string()) {
    parts.push_back(MachinePart{"cq-part " + cls, Area{x, y, w, h}, text});
  };
  const float wallFoot = 262;                         // where the wall meets the floor
  const float doorX = kCrewFigureX - kCrewDoorW / 2;  // the figure stands before the door
  part("cq-wall", 0, 0, kCrewStageW, wallFoot);
  part("cq-floor", 0, wallFoot, kCrewStageW, kCrewStageH - wallFoot);
  for (int k = 0; k < 3; ++k) {
    part("cq-board", 0, wallFoot + 6 + k * (8 + k * 4), kCrewStageW, 1);
  }
  part("cq-skirt", 0, wallFoot - 4, kCrewStageW, 4);
  part("cq-light", doorX, 0, kCrewDoorW, 9);
  part("cq-glow", doorX - 20, 9, kCrewDoorW + 40, 4);
  part("cq-mini", doorX, 38, kCrewDoorW, 22, "DECK A");
  part("cq-door", doorX, 64, kCrewDoorW, wallFoot - 4 - 64);
  part("cq-dpane", doorX + 13, 80, 70, 60);
  for (int k = 0; k < 2; ++k) {
    const float lockerW = 44;
    const float x = k == 0 ? doorX - kCrewBareWall - lockerW : doorX + kCrewDoorW + kCrewBareWall;
    part("cq-lock", x, 70, lockerW, wallFoot - 4 - 70);
    part("cq-lvent", x + 8, 80, 28, 2);
    part("cq-lvent", x + 8, 85, 28, 2);
    part("cq-lvent", x + 8, 90, 28, 2);
    part("cq-lnum", x + 6, 100, 32, 20, "0" + js::NumToString(k + 1));
    part("cq-lhand", x + 32, 154, 4, 18);
  }
  part("cq-shadow", kCrewFigureX - 40, kCrewDeckY - 6, 80, 12);
  return parts;
}

}  // namespace

QuartersScreenModel BuildQuartersScreen(const Simulation& sim) {
  QuartersScreenModel model;
  TaskScreenModel& screen = model.screen;

  screen.head.kind = "ROOM";
  screen.head.title = "Crew Quarters";
  screen.head.sub =
      "Shared bunks, lockers, and the only place aboard where orders are written down.";

  model.hall = Hallway();
  model.card = Area{10, 10, 232, 310};
  // The speech bubble: on the bare wall right of the door, at the figure's head.
  model.talk = Area{kCrewFigureX + (kCrewDoorW + kCrewBareWall) / 2 - 16, kCrewDeckY - 128, 32, 32};
  screen.hasStage = true;
  screen.stage.id = "hallway";
  screen.stage.width = kCrewStageW;
  screen.stage.height = kCrewStageH;
  screen.stage.typePx = kCrewStageTypePx;

  // --- the roster ---------------------------------------------------------
  const std::vector<CrewMember>& crew = sim.Crew();
  const CrewMember* selected = nullptr;
  for (const CrewMember& member : crew) {
    if (member.id == sim.State().crewSel) { selected = &member; }
  }
  if (selected == nullptr && !crew.empty()) { selected = &crew.front(); }
  model.hasCrew = selected != nullptr;
  if (selected != nullptr) { model.selectedId = selected->id; }

  for (const CrewMember& member : crew) {
    CrewRowView row;
    row.id = member.id;
    row.name = member.name;
    row.colour = member.colour;
    row.selected = selected != nullptr && member.id == selected->id;
    const Task* busy = sim.CrewBusy(member);
    row.busy = busy != nullptr;
    row.hasOrder = sim.SheetOf(member) != nullptr;
    if (busy != nullptr) {
      row.flag = js::NumToString(busy->left);
    } else if (row.hasOrder) {
      row.flag = "order";
    }
    if (busy != nullptr) {
      row.role = sim.FindOngoing("doc-" + member.id) != nullptr ? "In the medical bay"
                                                               : "Carrying out orders";
    } else {
      row.role = ShortRole(member.role);
      if (!member.job.empty()) { row.role += " - " + JobLabel(sim, member.job); }
    }
    model.roster.push_back(row);
  }

  NavPanel roster;
  roster.id = "cq-roster";
  roster.title = "Crew";
  roster.placement = "np-last";
  model.payWages = js::NumToString(sim.PayrollBill()) + " sh";
  model.payEvery = js::NumToString(sim.GameRules().pay.every) + " clicks";
  model.payNext = "in " + js::NumToString(sim.PayrollDue());
  model.payPurse = js::NumToString(sim.State().shiplets) + " sh";
  roster.Field("Wages owed", model.payWages);
  roster.Field("Every", model.payEvery);
  roster.Field("Next pay", model.payNext);
  roster.Field("Purse", model.payPurse);
  for (const CrewRowView& row : model.roster) {
    roster.Field(row.name, row.flag.empty() ? row.role : row.flag + " | " + row.role);
  }

  NavPanel cond;
  cond.id = "cq-cond";
  cond.title = "Condition";
  cond.placement = "np-half";
  NavPanel stand;
  stand.id = "cq-stand";
  stand.title = "Standing";
  stand.placement = "np-half-last";

  NavPanel ordersPanel;
  ordersPanel.id = "cq-orders";
  ordersPanel.title = "Orders";
  NavPanel sheet;
  sheet.id = "cq-sheet";
  sheet.title = "Order Sheet";

  NavPanel assignPanel;
  assignPanel.id = "cq-assign";
  assignPanel.title = "Assignments";
  assignPanel.placement = "np-last";

  if (selected == nullptr) {
    cond.Dim("No crew aboard.");
    model.bandNote = "No orders written.";
    assignPanel.Note(model.bandNote);
    assignPanel.buttons.push_back(BuildProgressButton(
        sim, "crew-assign", "Assign Tasks (0 Clicks)", false, "Complete", std::string()));
    screen.left = {roster};
    screen.right = {ordersPanel, sheet, assignPanel};
    return model;
  }

  const CrewMember& hand = *selected;
  model.scene = BuildCrewScene(sim, hand);
  model.cardName = hand.name;
  model.cardRole = hand.role;
  model.cardColour = hand.colour;

  // HTA paintCrewPanels -> statLine: numbers AND coloured bars. Hunger and
  // fatigue invert the colour judgement, never the actual bar width.
  const auto vital = [&](const std::string& label, double value, bool bad) {
    cond.Field(label, js::NumToString(value));
    const double good = bad ? 100 - value : value;
    cond.Bar(value, good >= 70 ? "#7f9161" : good >= 40 ? "#c08a3e" : "#b05a42");
  };
  vital("Health", hand.health, false);
  vital("Morale", hand.morale, false);
  vital("Fatigue", hand.fatigue, true);
  vital("Hunger", hand.hunger, true);

  const JobDef* job = sim.FindJobDef(hand.job);
  stand.Field("Role", hand.role);
  stand.Field("Job", job == nullptr ? "None" : job->label);
  stand.Field("Skill", hand.skill.empty() ? "Untrained" : hand.skill);
  stand.Field("Acts at", sim.JobThreshold(hand));
  const double wage = sim.CrewWage(hand);
  stand.Field("Pay", wage > 0 ? js::NumToString(wage) + " sh each payday"
                              : "Unpaid - holds no job");
  stand.Field("Station", sim.CrewWhere(hand));
  stand.Field("Doing", sim.SimActivityOf(hand));

  stand.Dim(hand.traits);

  // --- the pad ------------------------------------------------------------
  const Task* busy = sim.CrewBusy(hand);
  ordersPanel.Name(hand.name);
  if (busy != nullptr) {
    const Task* task = busy;
    model.closedText =
        (sim.FindOngoing("doc-" + hand.id) != nullptr
             ? std::string("In the medical bay.")
             : "Carrying out orders.") +
        " " + js::NumToString(task == nullptr ? 0 : task->left) + " click" +
        (task != nullptr && task->left == 1 ? "" : "s") + " remaining.";
    ordersPanel.Dim(model.closedText);
  }
  ordersPanel.Field("Standing job", JobLabel(sim, sim.StagedJob(hand)));
  const OrderSheet* staged = sim.SheetOf(hand);
  model.selectedJob = sim.StagedJob(hand);
  const double cost = sim.JobCost(model.selectedJob);
  model.jobCostLine =
      staged != nullptr && staged->hasJob
          ? "staged - " + js::NumToString(cost) + " click" + (cost == 1 ? "" : "s")
          : "costs " + js::NumToString(cost) + " to hand over";
  ordersPanel.Dim(model.jobCostLine);
  for (const JobDef& option : sim.Jobs()) {
    model.jobOptions.push_back(option.id + "\t" + option.label);
  }
  for (const Catalog::OrderDefinition& order : sim.World().orderDefs) {
    OrderLineView line;
    line.id = order.id;
    line.label = order.label;
    line.actions = sim.OrderCost(hand, order.id);
    line.fee = sim.OrderFeeFor(sim.StagedJob(hand), order.id);
    const bool on = staged != nullptr && staged->Has(order.id);
    line.on = on;
    line.locked = (order.id == "learnSt" && !sim.State().docked) ||
                  (order.id == "rest" && hand.fatigue < 25 && hand.morale >= 70 && !on);
    line.cost = js::NumToString(line.actions);
    if (line.fee > 0) { line.cost += " - " + js::NumToString(line.fee) + " sh"; }
    line.note = line.locked ? (order.id == "rest" ? "already rested" : "no station in reach")
                            : order.note;
    line.state = line.locked ? "off" : (on ? "on" : "");
    model.orders.push_back(line);
  }

  // --- the sheet ----------------------------------------------------------
  for (const CrewMember& member : crew) {
    const OrderSheet* staged = sim.SheetOf(member);
    if (staged == nullptr) { continue; }
    std::vector<std::string> bits;
    if (staged->hasJob) { bits.push_back(JobLabel(sim, staged->job)); }
    for (const Catalog::OrderDefinition& order : sim.World().orderDefs) {
      if (staged->Has(order.id)) { bits.push_back(order.label); }
    }
    std::string joined;
    for (std::size_t i = 0; i < bits.size(); ++i) {
      if (i != 0) { joined += ", "; }
      joined += bits[i];
    }
    sheet.Field(FirstName(member.name),
                joined.empty() ? std::string() : joined + " " +
                    js::NumToString(sim.CrewCostOf(member)));
  }
  if (sheet.lines.empty()) { sheet.Dim("Nothing written down yet."); }
  model.sheetText = sheet.lines.empty() ? std::string() : sheet.lines.front().value;

  // --- the figure's conversation state ------------------------------------
  model.conversation = BuildConversationRow(sim, hand.id);
  model.said = hand.said;

  // --- right-column assignments commit (no bottom banner) -----------------
  const double total = sim.OrdersCost();
  const int who = sim.OrderedCount();
  if (total > 0) {
    model.bandNote = "Orders written for " + js::NumToString(who) + " crew member" +
                     (who == 1 ? "" : "s") + ".";
    const double fee = sim.OrdersFee();
    if (fee > 0) {
      model.bandNote += " Training fees " + js::NumToString(fee) + " sh of " +
                        js::NumToString(sim.State().shiplets) + ".";
    }
  } else {
    model.bandNote = "No orders written.";
  }
  assignPanel.Note(model.bandNote);
  assignPanel.buttons.push_back(BuildProgressButton(
      sim, "crew-assign",
      "Assign Tasks (" + js::NumToString(total) + " Click" + (total == 1 ? "" : "s") + ")",
      total > 0, "Complete", std::string()));

  screen.left = {roster};
  screen.under = {cond, stand};
  screen.right = {ordersPanel, sheet, assignPanel};
  return model;
}

// ---------------------------------------------------------------------------
// The pad's presses
// ---------------------------------------------------------------------------
namespace {
bool PressQuartersActionRaw(Simulation& sim, const std::string& action,
                         const std::string& arg) {
  if (action == "select") {
    // FREE: looking at a crew member is free.
    for (const CrewMember& crew : sim.Crew()) {
      if (crew.id == arg) { sim.MutableState().crewSel = arg; return true; }
    }
    return false;
  }
  if (action == "job") { return sim.StageJob(sim.State().crewSel, arg); }
  if (action == "order") { return sim.ToggleOrder(sim.State().crewSel, arg); }
  if (action == "commit") { return PressProgressButton(sim, "crew-assign"); }
  if (action == "talk") {
    const auto model = BuildQuartersScreen(sim);
    return model.hasCrew && PressConversation(sim, model.selectedId);
  }
  return false;
}

}  // namespace

// ============================================================================
//  The press, and the clicks it costs.
//  ---------------------------------------------------------------------------
//  One control is one control. A press spends one of the clicks that control
//  holds and returns one to every other control, so a button is only refused
//  once its own green has run out. Free reads spend nothing and return nothing.
// ============================================================================
bool PressQuartersAction(Simulation& sim, const std::string& action, const std::string& arg) {
  // select/job/order are free; commit and conversation own their own locks.
  // Wrapping it again creates an invisible lock and can block acknowledgment.
  return PressQuartersActionRaw(sim, action, arg);
}

}  // namespace ship

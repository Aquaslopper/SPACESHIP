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

// The hallway: lockers down both walls, a door at the far end, a light strip
// overhead. All scenery, all one class, no clicks.
std::vector<MachinePart> Hallway(const Simulation& sim) {
  std::vector<MachinePart> parts;
  const auto part = [&parts](const std::string& cls, float x, float y, float w,
                             float h, const std::string& text = std::string()) {
    parts.push_back(MachinePart{"cq-part " + cls, Area{x, y, w, h}, text});
  };
  part("cq-wall", 0, 0, 560, 230);
  part("cq-floor", 0, 230, 560, 70);
  for (int k = 0; k < 4; ++k) {
    part("cq-board", 0, 236 + k * (8 + k * 4), 560, 1);
  }
  part("cq-skirt", 0, 226, 560, 4);
  part("cq-light", 236, 0, 88, 9);
  part("cq-glow", 216, 9, 128, 4);
  part("cq-door", 236, 64, 88, 162);
  part("cq-dpane", 248, 78, 64, 52);
  part("cq-dhand", 306, 150, 8, 14);
  part("cq-mini", 236, 52, 88, 10, "DECK A");
  for (int k = 0; k < 8; ++k) {
    const double x = k < 4 ? 16 + k * 40 : 384 + (k - 4) * 40;
    part("cq-lock", x, 70, 38, 156);
    part("cq-lvent", x + 8, 80, 22, 2);
    part("cq-lvent", x + 8, 85, 22, 2);
    part("cq-lvent", x + 8, 90, 22, 2);
    part("cq-lhand", x + 28, 140, 4, 16);
    part("cq-lnum", x + 6, 208, 26, 10, "0" + js::NumToString(k + 1));
  }
  part("cq-shadow", 248, 222, 64, 10);
  (void)sim;
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

  model.hall = Hallway(sim);
  screen.hasStage = true;
  screen.stage.id = "hallway";
  screen.stage.width = 560.0f;
  screen.stage.height = 300.0f;

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
  NavPanel talk;
  talk.id = "cq-talk";
  talk.title = "The Figure";
  talk.placement = "np-full";

  NavPanel ordersPanel;
  ordersPanel.id = "cq-orders";
  ordersPanel.title = "Orders";
  NavPanel sheet;
  sheet.id = "cq-sheet";
  sheet.title = "Order Sheet";
  sheet.placement = "np-last";

  NavPanel band;
  band.id = "cq-band";
  band.title = "Assignments";
  band.placement = "np-full";

  if (selected == nullptr) {
    cond.Dim("No crew aboard.");
    screen.left = {roster};
    screen.right = {ordersPanel, sheet};
    screen.band = {band};
    return model;
  }

  const CrewMember& hand = *selected;
  model.scene = BuildCrewScene(sim, hand);

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
  const bool out = sim.OrdersRunning();
  const Task* busy = sim.CrewBusy(hand);
  if (busy != nullptr || out) {
    const Task* task = busy != nullptr ? busy : sim.FindOngoing("crew-assign");
    model.closed = true;
    model.closedText =
        (sim.FindOngoing("doc-" + hand.id) != nullptr
             ? std::string("In the medical bay.")
             : (busy != nullptr ? "Carrying out orders." : "Orders in hand.")) +
        " " + js::NumToString(task == nullptr ? 0 : task->left) + " click" +
        (task != nullptr && task->left == 1 ? "" : "s") + " remaining.";
    ordersPanel.Dim(hand.name);
    ordersPanel.Dim(model.closedText);
  } else {
    ordersPanel.Name(hand.name);
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

  // --- the figure ---------------------------------------------------------
  talk.Field("Figure", sim.FindOngoing("doc-" + hand.id) != nullptr
                           ? FirstName(hand.name) + " is in the medical bay"
                           : "Talk to " + FirstName(hand.name));
  model.conversation = BuildConversationRow(sim, hand.id);
  model.said = hand.said;
  talk.Field("Said", hand.said.empty() ? "You have not asked them anything yet."
                                       : "\"" + hand.said + "\"");

  // --- the band -----------------------------------------------------------
  const double total = sim.OrdersCost();
  const int who = sim.OrderedCount();
  bool available = true;
  for (const CrewMember& member : crew) {
    if (sim.SheetOf(member) != nullptr && !sim.CrewAvailable(member)) { available = false; }
  }
  if (out) {
    model.bandNote = "Orders in hand.";
  } else if (total > 0) {
    model.bandNote = "Orders written for " + js::NumToString(who) + " crew member" +
                     (who == 1 ? "" : "s") + ".";
    if (!available) { model.bandNote += " A selected crew member is busy."; }
    const double fee = sim.OrdersFee();
    if (fee > 0) {
      model.bandNote += " Training fees " + js::NumToString(fee) + " sh of " +
                        js::NumToString(sim.State().shiplets) + ".";
    }
  } else {
    model.bandNote = "No orders written.";
  }
  band.Note(model.bandNote);

  screen.progress.push_back(BuildProgressButton(
      sim, "crew-assign",
      "Assign Tasks (" + js::NumToString(total) + " Action" + (total == 1 ? "" : "s") + ")",
      total > 0 && available, "Complete", std::string()));

  screen.left = {roster};
  screen.under = {cond, stand};
  screen.right = {ordersPanel, sheet};
  screen.band = {band};
  (void)talk;
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

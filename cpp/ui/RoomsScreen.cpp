// ============================================================================
//  RoomsScreen.cpp — the fitted bays and the bridge.
// ============================================================================
#include "ui/RoomsScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"
#include "ui/Shell.h"

#include <algorithm>

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

const RoomScene* SceneOf(const Simulation& sim, const std::string& roomId) {
  auto it = sim.World().roomScenes.find(roomId);
  return it == sim.World().roomScenes.end() ? nullptr : &it->second;
}

double CountLots(const Simulation& sim, const std::string& kind) {
  double count = 0;
  for (const GoodsLot& lot : sim.Goods()) {
    if (lot.kind == kind && lot.contractId.empty() && !lot.risk) { count += 1; }
  }
  return count;
}

}  // namespace

bool RoomHasScreen(const Simulation& sim, const std::string& roomId) {
  if (roomId == "bridge" && sim.HasRoom(roomId)) { return true; }
  for (const Room& room : sim.Rooms()) {
    if (room.id != roomId) { continue; }
    auto fit = sim.World().roomScenes.find(roomId);
    return fit != sim.World().roomScenes.end() && roomId.size() > 1 && roomId[0] == 'r';
  }
  return false;
}

RoomScreenModel BuildRoomScreen(const Simulation& sim, const std::string& roomId) {
  RoomScreenModel model;
  model.roomId = roomId;
  const Room* room = nullptr;
  for (const Room& candidate : sim.Rooms()) {
    if (candidate.id == roomId) { room = &candidate; }
  }
  const RoomScene* scene = SceneOf(sim, roomId);

  model.head.kind = "ROOM";
  model.head.title = room == nullptr ? roomId : room->name;
  model.head.sub = room == nullptr ? std::string() : room->systems;
  if (scene != nullptr) {
    model.glyph = scene->glyph;
    model.code = scene->code;
    model.caption = scene->label;
    model.description = scene->description;
    model.accent = scene->accent;
  }

  model.body.id = "ship-systems";
  model.body.title = "Ship systems";
  if (!model.caption.empty()) { model.body.Sub(model.caption); }
  if (!model.description.empty()) { model.body.Dim(model.description); }

  const double energy = sim.StatValue("energy");
  const double hull = sim.StatValue("hull");
  const double shield = sim.StatValue("shield");

  // ---- the dedicated rooms ----------------------------------------------
  if (roomId == "bridge") {
    const CrewMember* navigator = nullptr;
    for (const CrewMember& crew : sim.Crew()) {
      if (crew.job == "navigation") { navigator = &crew; break; }
    }
    model.body.Field("Location", sim.FindBodyById(sim.Map().here) == nullptr
                                     ? sim.Map().here
                                     : sim.FindBodyById(sim.Map().here)->name);
    model.body.Field("Drive", Num(sim.State().engineCondition) + "%");
    model.body.Field("Energy", Num(energy) + "%");
    model.body.Field("Navigator",
                     navigator == nullptr
                         ? std::string("None available")
                         : navigator->name + " (-" + Num(sim.NavigatorFuelDiscount(*navigator)) +
                               "% fuel at departure)");
    model.body.Field("Active work", Num(static_cast<double>(sim.State().ongoing.size())) +
                                        " processes");
    ActionRowView split;
    split.id = "fuel-split";
    split.label = "Split one fuel cell: up to +10 energy and +7 fuel";
    split.cost = "1 click";
    split.enabled = sim.State().fuelCells > 0 && energy < 100 && sim.StatValue("fuel") < 100;
    model.rows.push_back(split);
    ActionRowView warp;
    warp.id = "warp";
    warp.label = "WARP: advance time +5 clicks for 20 fuel";
    warp.cost = "+5 Pro-Action";
    warp.enabled = sim.StatValue("fuel") >= 20;
    model.rows.push_back(warp);
    return model;
  }

  if (roomId == "r6" || roomId == "r7") {
    model.body.Field("Energy", Num(energy) + "%");
    model.body.Field("Shields", Num(shield) + "%");
    model.body.Note(
        "A transfer loses five points in conversion. At zero energy, life support and "
        "powered work stop until power returns.");
    ActionRowView row;
    if (roomId == "r6") {
      row.id = "divert-shield";
      row.label = "Divert 20 shields into 15 energy";
      row.enabled = shield >= 20 && energy <= 85;
    } else {
      row.id = "divert-power";
      row.label = "Divert 20 energy into 15 shields";
      row.enabled = energy >= 20 && shield <= 85;
    }
    row.cost = "1 click";
    model.rows.push_back(row);
    return model;
  }

  model.body.Field("Energy", Num(energy) + "%");
  if (roomId == "r2") {
    model.body.Field("Repair kits", Num(sim.State().repairKits));
    model.body.Field("Salvage lots", Num(CountLots(sim, "Salvage")));
  }
  if (roomId == "r5") {
    model.body.Field("Drive", Num(sim.State().engineCondition) + "%");
    model.body.Field("Maintenance", sim.MaintRunningAny() ? "In progress" : "Available");
  }
  if (roomId == "r8") {
    model.body.Field("Dry rations", Num(sim.MessRations()) + " / " +
                                         Num(sim.MessRationCapacity()));
    model.body.Field("Cooked food", Num(sim.MessRack()) + " / " +
                                        Num(sim.MessCapacity()));
    model.body.Field("Produce lots", Num(CountLots(sim, "Perishable")));
  }
  if (roomId == "r9") {
    model.body.Field("O2 canisters", Num(sim.Life().canisters) + " / " + Num(sim.Life().capacity));
    model.body.Field("Salvage lots", Num(CountLots(sim, "Salvage")));
  }
  if (roomId == "r10") {
    model.body.Field("Fuel cells", Num(sim.State().fuelCells));
    model.body.Field("Salvage lots", Num(CountLots(sim, "Salvage")));
  }
  if (roomId == "r11") {
    model.body.Field("Hull", Num(hull) + "%");
    model.body.Field("Salvage lots", Num(CountLots(sim, "Salvage")));
  }
  model.body.Note(
      "Inputs are consumed at the start. Work advances through clicks elsewhere and "
      "pauses without power.");

  // Every project whose own room this is, in the catalogue's order.
  for (const ProjectDef& project : sim.World().projects) {
    if (project.room != roomId) { continue; }
    const std::string blocked = sim.ProjectBlock(project.id);
    model.body.Dim(Num(project.energy) + "% energy; 1 click to begin, then " +
                   Num(project.work) + " clicks of work. Pauses without power." +
                   (blocked.empty() ? std::string() : " " + blocked));
    model.buttons.push_back(
        BuildProgressButton(sim, "project-" + project.id, project.label, blocked.empty()));
  }
  return model;
}

namespace {
bool PressRoomActionRaw(Simulation& sim, const std::string& action, const std::string& arg) {
  (void)arg;
  if (action == "open-nav") {
    OpenTask(sim, "nav", "main", true);   // looking at the chart is free
    return true;
  }
  if (action == "warp") {
    if (sim.StatValue("fuel") < 20) return false;
    sim.ClampStat("fuel", -20);
    sim.MutableState().charges.at(ButtonChargeKey(sim, "press:room:warp:")).left -= 1;
    sim.ProAction(5);
    sim.FreeCommit("WARP advanced work by 5 positive clicks for 20 fuel");
    return true;
  }
  if (action == "fuel-split") {
    if (sim.State().fuelCells <= 0) { return false; }
    if (sim.StatValue("energy") >= 100 || sim.StatValue("fuel") >= 100) { return false; }
    sim.MutableState().fuelCells -= 1;
    sim.ClampStat("energy", 10);
    sim.ClampStat("fuel", 7);
    sim.PerformAction("Fuel cell divided between reserves");
    return true;
  }
  if (action == "divert-shield") {
    if (!sim.HasRoom("r6")) return false;
    if (sim.StatValue("shield") < 20 || sim.StatValue("energy") > 85) { return false; }
    sim.ClampStat("shield", -20);
    sim.ClampStat("energy", 15);
    sim.PerformAction("Shields diverted to power");
    return true;
  }
  if (action == "divert-power") {
    if (!sim.HasRoom("r7")) return false;
    if (sim.StatValue("energy") < 20 || sim.StatValue("shield") > 85) { return false; }
    sim.ClampStat("energy", -20);
    sim.ClampStat("shield", 15);
    sim.PerformAction("Power diverted to shields");
    return true;
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
std::string RoomActionCategory(const std::string& action) {
  if (action == "warp" || action == "fuel-split" || action == "divert-shield" || action == "divert-power" ||
      action.compare(0, 5, "meal:") == 0) { return "systems"; }
  return ButtonChargeTier(action);
}

bool PressRoomAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ButtonChargeKey(sim, "press:room:" + action + ":" + arg),
      RoomActionCategory(action),
      [&]() { return PressRoomActionRaw(sim, action, arg); });
}

}  // namespace ship

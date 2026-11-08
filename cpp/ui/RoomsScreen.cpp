// ============================================================================
//  RoomsScreen.cpp — the fourteen fitted bays, the bridge and the Simulacron's
//  neighbours. The sentences are the reference's; the ids are this port's.
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
  if ((roomId == "bridge" || roomId == "simulacron") && sim.HasRoom(roomId)) { return true; }
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
      if (crew.job == "navigation") { navigator = &crew; if (sim.CrewBusy(crew) == nullptr) { break; } }
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
    ActionRowView nav;
    nav.id = "open-nav";
    nav.label = "Open Navigation";
    nav.cost = "free";
    nav.enabled = true;
    model.rows.push_back(nav);

    ActionRowView split;
    split.id = "fuel-split";
    split.label = "Split one fuel cell: up to +10 energy and +7 fuel";
    split.cost = "1 action";
    split.enabled = sim.State().fuelCells > 0 && energy < 100 && sim.StatValue("fuel") < 100;
    model.rows.push_back(split);
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
    row.cost = "1 action";
    model.rows.push_back(row);
    return model;
  }

  if (roomId == "r1") {
    model.body.Note(
        "A medkit and 2% energy start a three-action treatment (up to +25 health); "
        "treatment pauses without power. The ordinary medical order in Crew Quarters "
        "remains available without a medkit.");
    model.body.Dim("Medkits aboard: " + Num(sim.State().medkits));
    for (const CrewMember& crew : sim.Crew()) {
      const bool busy = sim.CrewBusy(crew) != nullptr;
      const bool ready = crew.health < 100 && !busy && sim.State().medkits > 0 && energy > 2;
      model.buttons.push_back(BuildProgressButton(
          sim, "doc-" + crew.id, "Treat " + crew.name + " (health " + Num(crew.health) + "%)",
          ready));
    }
    return model;
  }

  if (roomId == "r13") {
    model.body.Note(
        "A shared meal uses two units of cooked food from the galley counter and one action; it "
        "takes three more actions. It strengthens the bond by 12 (+2 for a shared name "
        "anchor) and gives each person 4 morale. Strong bonds help morale; strained bonds "
        "hurt it.");
    if (sim.Crew().size() < 2) {
      model.body.Dim("At least two crew members are needed.");
      return model;
    }
    const bool mealBusy = sim.FindOngoing("project-social") != nullptr;
    for (std::size_t i = 0; i < sim.Crew().size(); ++i) {
      for (std::size_t j = i + 1; j < sim.Crew().size(); ++j) {
        const CrewMember& one = sim.Crew()[i];
        const CrewMember& two = sim.Crew()[j];
        ActionRowView row;
        row.id = "meal:" + one.id + ":" + two.id;
        const double bond = sim.SimBond(one.id, two.id);
        row.label = "Share a meal: " + one.name + " and " + two.name;
        row.note = "Bond " + Num(bond) + " / 100 - " + sim.SimAnchorOf(one) + " / " +
                   sim.SimAnchorOf(two) + ".";
        row.cost = "1 action + 3 work";
        row.enabled = !mealBusy && sim.CrewBusy(one) == nullptr &&
                      sim.CrewBusy(two) == nullptr && sim.MessRack() >= 2;
        model.rows.push_back(row);
      }
    }
    return model;
  }

  if (roomId == "r4") {
    const Body* system = sim.CurrentSystem();
    const std::string region = system == nullptr ? std::string() : system->region;
    const std::vector<std::string>& history = sim.WorldHistory(region);
    model.body.Field("Local system", system == nullptr ? std::string() : system->name);
    if (!history.empty()) { model.body.Field("History", history[0]); }
    auto heard = sim.State().sim.broadcasts.find(system == nullptr ? std::string() : system->id);
    const bool hasReport = heard != sim.State().sim.broadcasts.end() && heard->second;
    if (hasReport && history.size() > 1) { model.body.Field("Traffic report", history[1]); }

    ActionRowView broadcast;
    broadcast.id = "broadcast";
    broadcast.label = "Request local traffic report (2% energy)";
    broadcast.cost = "1 action";
    broadcast.enabled = !hasReport && energy > 2;
    model.rows.push_back(broadcast);

    model.body.Sub("The Silicon Marrow signal");
    const std::string signal = sim.State().sim.signal;
    if (signal == "dormant") {
      model.body.Dim(
          "No unusual signal logged. A deep scan of Halvan's Reach may reveal one.");
    }
    if (signal == "intercepted" || signal == "decoding") {
      model.body.Dim(
          "An old relay repeats a name and an incomplete request. The Dream Engine can "
          "supply the insight needed to decode it.");
      // The decode project belongs to this room, and only exists while there is
      // a signal to work on.
      for (const ProjectDef& project : sim.World().projects) {
        if (project.id != "decode") { continue; }
        model.body.Dim(Num(project.energy) + "% energy; 1 action to begin, then " +
                       Num(project.work) + " actions of work. Pauses without power.");
        model.buttons.push_back(BuildProgressButton(
            sim, "project-decode", project.label, sim.ProjectBlock("decode").empty()));
      }
    }
    if (signal == "coordinates") {
      model.body.Note(
          "The decoded coordinates point to the Choir Array in Echo Shoal, in Cradle "
          "Shoal. Jump to Echo Shoal and deep-scan the array.");
    }
    if (signal == "request") {
      model.body.Note(
          "The array carries an archive called Silicon Marrow. Its recorded request is "
          "not to be copied for sale. The Archive Hull in Palimpsest can shelter it; a "
          "buyer at any station offers 900 shiplets instead.");
      ActionRowView shelter;
      shelter.id = "signal:preserved";
      shelter.label = "Shelter the archive at the Archive Hull (1 repair kit, 5% energy)";
      shelter.cost = "1 action";
      shelter.enabled = sim.Map().here == "palimpsest-site" && sim.State().repairKits > 0 &&
                        energy > 5;
      shelter.confirm = "Shelter the archive? This choice is permanent.";
      model.rows.push_back(shelter);

      ActionRowView sell;
      sell.id = "signal:sold";
      sell.label = "Sell the signal at this station (+900 sh, crew morale -8)";
      sell.cost = "1 action";
      sell.enabled = sim.State().docked;
      sell.confirm = "Sell the signal? This choice is permanent.";
      model.rows.push_back(sell);
    }
    if (signal == "preserved") {
      model.body.Note("The archive rests in the Palimpsest hull. The crew kept its request.");
    }
    if (signal == "sold") {
      model.body.Note("The archive was sold. The crew remembers the decision.");
    }
    return model;
  }

  if (roomId == "r12") {
    const Body* system = sim.CurrentSystem();
    const std::vector<std::string>& history =
        sim.WorldHistory(system == nullptr ? std::string() : system->region);
    const Body* here = sim.FindBodyById(sim.Map().here);
    model.body.Field("Location", here == nullptr ? sim.Map().here : here->name);
    const Body* region = system == nullptr ? nullptr : sim.FindBodyById(system->region);
    model.body.Field("Region", region == nullptr ? std::string() : region->name);
    if (!history.empty()) { model.body.Field("History", history[0]); }
    model.body.Note(
        "Each location can produce one 65-shiplet observation for the Science Room.");
  } else {
    model.body.Field("Energy", Num(energy) + "%");
    if (roomId == "r2") {
      model.body.Field("Repair kits", Num(sim.State().repairKits));
      model.body.Field("Salvage lots", Num(CountLots(sim, "Salvage")));
    }
    if (roomId == "r3") {
      model.body.Field("Insight", Num(sim.State().sim.insight));
      model.body.Field("Calibration", sim.State().sim.sensor ? "Stored" : "None");
      model.body.Field("Next scan", Num(sim.ScanCost()) + " work actions");
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
        "Inputs are consumed at the start. Work advances through actions elsewhere and "
        "pauses without power.");
  }

  // Every project whose own room this is, in the catalogue's order.
  for (const ProjectDef& project : sim.World().projects) {
    if (project.room != roomId) { continue; }
    const std::string blocked = sim.ProjectBlock(project.id);
    model.body.Dim(Num(project.energy) + "% energy; 1 action to begin, then " +
                   Num(project.work) + " actions of work. Pauses without power." +
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
  if (action == "broadcast") { return sim.BroadcastLocal(); }
  if (action.compare(0, 7, "signal:") == 0) { return sim.FinishSignal(action.substr(7)); }
  if (action.compare(0, 5, "meal:") == 0) {
    const std::string rest = action.substr(5);
    const std::size_t cut = rest.find(':');
    if (cut == std::string::npos) { return false; }
    return sim.StartSocial(rest.substr(0, cut), rest.substr(cut + 1));
  }
  return false;
}

}  // namespace

// ============================================================================
//  The press, with the anti-spam lock around it (Phase 9).
//  ---------------------------------------------------------------------------
//  One control is one control. The first press spends the action and starts the
//  tier's lock in ACTION_COOLDOWN_RULES; a second press of the same button
//  before another action resolves is refused, whether it comes from the window
//  or from this harness. Free reads spend nothing and therefore lock nothing.
// ============================================================================
std::string RoomActionCategory(const std::string& action) {
  if (action == "broadcast") { return "space"; }
  if (action.compare(0, 7, "signal:") == 0) { return "critical"; }
  if (action == "fuel-split" || action == "divert-shield" || action == "divert-power" ||
      action.compare(0, 5, "meal:") == 0) { return "systems"; }
  return ActionCooldownCategory(action);
}

bool PressRoomAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:room:" + action + ":" + arg),
      RoomActionCategory(action),
      [&]() { return PressRoomActionRaw(sim, action, arg); });
}

}  // namespace ship

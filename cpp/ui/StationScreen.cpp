// ============================================================================
//  StationScreen.cpp — market, mission board, yard, hiring hall and tavern.
// ============================================================================
#include "ui/StationScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"
#include "ui/Shell.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

const double kFuelCell = 45;
const double kRepairKit = 70;
const double kMedkit = 50;
const double kCanister = 25;
const double kRations = 20;
const double kBayOne = 500;
const double kBayTwo = 900;
const double kDrive = 900;
const double kRack = 400;
const double kScanner = 600;
const double kHullService = 120;

const char* kUndock = "Undock";

}  // namespace

StationScreenModel BuildStationScreen(const Simulation& sim, const std::string& portId) {
  StationScreenModel model;
  const Port* port = sim.World().FindPort(portId);
  model.portId = portId;
  model.portName = port == nullptr ? "Station" : port->name;
  model.docked = sim.State().docked && sim.Map().here == portId;

  model.head.kind = "STATION";
  model.head.title = model.portName;
  model.head.sub =
      "Browse freely. Each transaction, contract or departure costs one action.";

  const double purse = sim.State().shiplets;
  const double used = sim.CargoUsed();
  const double reserved = sim.CargoReserved();
  const double capacity = sim.CargoStore().capacity;
  model.purseLine = "Market - purse " + Num(purse) + " sh";
  model.holdLine = Num(used) + " of " + Num(capacity) + " m3 used; " + Num(reserved) +
                   " m3 reserved.";

  // ---- the market --------------------------------------------------------
  StationColumn market;
  market.title = model.purseLine;
  market.notes.push_back("Buy supplies and cargo. A different port may pay a different price.");
  const auto supply = [&market, purse](const std::string& id, const std::string& label,
                                       double price, bool extra) {
    StationRow row;
    row.id = id;
    row.label = label;
    row.cost = "1 action";
    row.enabled = purse >= price && extra;
    market.rows.push_back(row);
  };
  supply("buy-fuel", "Fuel cell - 45 sh", kFuelCell, true);
  supply("buy-repair", "Repair kit - 70 sh", kRepairKit, true);
  supply("buy-medkit", "Medkit - 50 sh", kMedkit, true);
  supply("buy-canister", "O2 canister - 25 sh", kCanister,
         sim.Life().canisters < sim.Life().capacity &&
             sim.FindOngoing("project-oxygen") == nullptr);
  supply("buy-rations", "Six rations - 20 sh", kRations,
         sim.MessRack() <= sim.MessCapacity() - 6 &&
             sim.FindOngoing("project-food") == nullptr);

  for (const Good& good : sim.Market()) {
    GoodsLot lot;
    lot.value = good.value;
    lot.kind = good.kind;
    const double price = js::Ceil(sim.LotPrice(lot) * 1.2);
    StationRow row;
    row.id = "buy-good:" + good.id;
    row.label = "Buy " + good.name + " - " + Num(price) + " sh / " + Num(good.vol) + " m3";
    row.cost = "1 action";
    row.enabled = purse >= price && used + reserved + good.vol <= capacity;
    market.rows.push_back(row);
  }

  // ---- sales -------------------------------------------------------------
  StationColumn sales;
  sales.title = "Sell cargo and survey data";
  if (sim.Goods().empty() && sim.Survey().empty()) {
    sales.notes.push_back("Nothing to sell.");
  }
  for (const GoodsLot& lot : sim.Goods()) {
    if (!lot.contractId.empty()) {
      sales.notes.push_back(lot.name + " - contract cargo, not for sale.");
      continue;
    }
    StationRow row;
    row.id = "sell:" + lot.id;
    row.label = "Sell " + lot.name + " - " + Num(sim.LotPrice(lot)) + " sh";
    row.cost = "1 action";
    row.enabled = true;
    sales.rows.push_back(row);
  }
  for (const SurveyRecord& record : sim.Survey()) {
    StationRow row;
    row.id = "sell-survey:" + record.id;
    row.label = "Sell survey: " + record.name + " - " + Num(record.value) + " sh";
    row.cost = "1 action";
    row.enabled = sim.FindOngoing("pkg-" + record.id) == nullptr;
    sales.rows.push_back(row);
  }

  // ---- the mission board -------------------------------------------------
  StationColumn board;
  board.title = "Mission board";

  // The repeatable commissions come first, exactly as the reference orders the
  // panel: heading, then either the live commission or the three offers.
  board.notes.push_back("Repeatable commissions");
  if (sim.State().commissions.active.has_value()) {
    const Commission& active = *sim.State().commissions.active;
    const double goal = CommissionGoal(active.kind);
    board.notes.push_back(CommissionDescription(sim, active) + " - " +
                          Num(active.progress) + " / " + Num(goal) + ". Payment: " +
                          Num(active.reward) + " sh at any station.");
    StationRow collect;
    collect.id = "commission-collect";
    collect.label = "Collect commission - " + Num(active.reward) + " sh";
    collect.cost = "1 action";
    collect.enabled = active.progress >= goal;
    board.rows.push_back(collect);
    StationRow abandon;
    abandon.id = "commission-abandon";
    abandon.label = "Abandon commission and rotate offers";
    abandon.cost = "1 action";
    abandon.confirm = "Abandon this commission? The reward will be lost.";
    abandon.enabled = true;
    board.rows.push_back(abandon);
  } else {
    board.notes.push_back(
        "Choose one commission. Objectives begin after acceptance; offers rotate "
        "after collection or abandonment.");
    for (const char* kindName : {"visit", "scan", "salvage"}) {
      const std::string kind = kindName;
      // The board must show what the acceptance would hand over, so it asks the
      // simulation for the same deterministic offer.
      const std::optional<Commission> offer = sim.CommissionOffer(kind);
      if (!offer.has_value()) { continue; }
      StationRow row;
      row.id = "commission-accept:" + kind;
      row.label = "Accept: " + CommissionDescription(sim, *offer) + " - " +
                  Num(offer->reward) + " sh";
      row.cost = "1 action";
      row.enabled = true;
      board.rows.push_back(row);
    }
  }
  { StationRow heading; heading.label = "Regular contracts"; heading.reading = true; board.rows.push_back(heading); }
  for (const Contract& contract : sim.World().contracts) {
    auto statusIt = sim.State().contracts.find(contract.id);
    const std::string status =
        statusIt == sim.State().contracts.end() ? "available" : statusIt->second;
    StationRow description;
    description.label = contract.title + ": " + contract.note + " Reward " +
                        Num(contract.reward) + " sh. [" + status + "]";
    description.reading = true;
    board.rows.push_back(description);
    if (status == "available" || status == "failed") {
      const bool eligible =
          (contract.origin.empty() || contract.origin == sim.Map().here) &&
          (contract.vol == 0 ||
           used + reserved + contract.vol <= capacity) &&
          (contract.goal != "passenger" ||
           static_cast<double>(sim.Crew().size() + sim.Passengers().size()) <
               sim.Ship().Berths());
      StationRow row;
      row.id = "contract-accept:" + contract.id;
      row.label = "Accept " + contract.title;
      row.cost = "1 action";
      row.enabled = eligible;
      board.rows.push_back(row);
    } else if (status == "ready") {
      StationRow row;
      row.id = "contract-collect:" + contract.id;
      row.label = "Collect " + contract.title + " - " + Num(contract.reward) + " sh";
      row.cost = "1 action";
      row.enabled = true;
      board.rows.push_back(row);
    }
  }

  // ---- the yard ----------------------------------------------------------
  StationColumn yard;
  yard.title = "Shipyard and crew";
  if (sim.State().shipClass == "wren") {
    yard.notes.push_back(
        "Your compact Wren: " + Num(capacity) + " m3 cargo, 6 berths. Trade up to the "
        "Kestrel-series SC Tessellate: " +
        Num(sim.ShipCargoCapacity("kestrel", sim.CargoRefitLevel())) +
        " m3 with your current refits, 14 berths, and a larger deck plan. The crew, "
        "cargo, active work, equipment and all voyage records transfer; hull and "
        "systems keep their condition.");
    StationRow ship;
    ship.id = "ship-buy";
    ship.label = "Buy larger ship SC Tessellate - 4,000 sh";
    ship.cost = "1 action";
    ship.enabled = purse >= 4000;
    ship.confirm =
        "Buy SC Tessellate for 4,000 shiplets? The crew, cargo, equipment, refits, tasks "
        "and voyage records transfer. This purchase is permanent.";
    yard.rows.push_back(ship);
  } else {
    yard.notes.push_back("Your Kestrel-series ship: " + Num(capacity) + " m3 cargo, " +
                         Num(sim.Ship().Berths()) + " berths.");
  }

  const double bay1 = sim.ShipCargoCapacity(sim.State().shipClass, 1);
  const double bay2 = sim.ShipCargoCapacity(sim.State().shipClass, 2);
  const auto upgrade = [&yard, purse](const std::string& id, const std::string& label,
                                      double price, bool available) {
    StationRow row;
    row.id = "upgrade:" + id;
    row.label = label;
    row.cost = "1 action";
    row.enabled = available && purse >= price;
    yard.rows.push_back(row);
  };
  const auto hasUpgrade = [&sim](const std::string& id) {
    auto it = sim.State().upgrades.find(id);
    return it != sim.State().upgrades.end() && it->second;
  };
  upgrade("bay", "Expand cargo hold to " + Num(bay1) + " m3 - 500 sh", kBayOne,
          !hasUpgrade("bay"));
  upgrade("bay2", "Expand cargo hold to " + Num(bay2) + " m3 - 900 sh", kBayTwo,
          hasUpgrade("bay") && !hasUpgrade("bay2"));
  upgrade("drive", "Fit efficient drive: fuel use -20% - 900 sh", kDrive,
          !hasUpgrade("drive"));
  upgrade("rack", "Expand O2 rack to 15 - 400 sh", kRack, !hasUpgrade("rack"));
  upgrade("scanner", "Upgrade sensors: scans take 3 - 600 sh", kScanner,
          !hasUpgrade("scanner"));
  {
    StationRow row;
    row.id = "hull-service";
    row.label = "Station hull service (+50%) - 120 sh";
    row.cost = "1 action";
    row.enabled = sim.StatValue("hull") < 100 && purse >= kHullService;
    yard.rows.push_back(row);
  }

  {
    StationRow heading;
    heading.label = "Optional rooms - 500 sh each";
    heading.reading = true;
    yard.rows.push_back(heading);
    const DeckPlan& plan = sim.State().shipClass == "wren" ? sim.World().wren : sim.World().kestrel;
    for (const Room& room : plan.rooms) {
      if (Simulation::StandardRoom(room.id) || sim.HasRoom(room.id)) continue;
      StationRow row;
      row.id = "room-buy:" + room.id;
      row.label = "Install " + room.name + " - 500 sh";
      row.cost = "1 action";
      row.enabled = model.docked && purse >= Simulation::RoomPrice();
      row.confirm = "Install " + room.name + " for 500 shiplets?";
      yard.rows.push_back(row);
    }
  }

  for (const CandidateDefinition& candidate : sim.World().candidates) {
    if (!candidate.port.empty() && candidate.port != portId) { continue; }
    const std::string name = candidate.crew.name;
    StationRow description;
    description.label = name + ": " + candidate.crew.traits + "; health " +
                        Num(candidate.crew.health) + "%, " +
                        (candidate.crew.skill.empty() ? std::string("untrained")
                                                      : candidate.crew.skill) + ".";
    description.reading = true;
    yard.rows.push_back(description);
    StationRow row;
    row.id = "hire:" + candidate.crew.id;
    row.label = "Hire " + name + " - " + Num(candidate.fee) + " sh";
    row.cost = "1 action";
    const bool already = sim.State().hired.count(candidate.crew.id) != 0 &&
                         sim.State().hired.at(candidate.crew.id);
    row.enabled = !already &&
                  static_cast<double>(sim.Crew().size() + sim.Passengers().size()) <
                      sim.Ship().Berths() &&
                  purse >= candidate.fee;
    yard.rows.push_back(row);
  }

  // ---- the tavern --------------------------------------------------------
  StationColumn tavern;
  tavern.title = "Tavern and departure";
  std::string tip = port == nullptr ? std::string() : port->tip;
  if (tip.empty()) {
    tip = model.portName == "Tolme Anchorage"
              ? "A trader says the Coldwell Relay pays well for produce, but sells ore cheaply."
              : "A trader says Tolme's market pays well for refined ore.";
  }
  model.tip = tip;
  tavern.notes.push_back(tip);
  tavern.notes.push_back("Station training only finishes while docked.");
  {
    StationRow row;
    row.id = "undock";
    row.label = kUndock;
    row.cost = "1 action";
    row.enabled = model.docked;
    tavern.rows.push_back(row);
  }

  model.columns = {market, sales, board, yard, tavern};
  return model;
}

namespace {
bool PressStationActionRaw(Simulation& sim, const std::string& action,
                        const std::string& arg) {
  // The economy names its fixed-price stores; the station names the verbs.
  if (action == "buy-fuel") return sim.BuySupply("fuel-cell");
  if (action == "buy-repair") return sim.BuySupply("repair-kit");
  if (action == "buy-medkit") return sim.BuySupply("medkit");
  if (action == "buy-canister") return sim.BuySupply("canister");
  if (action == "buy-rations") return sim.BuySupply("rations");
  if (action.compare(0, 9, "buy-good:") == 0) return sim.BuyGood(action.substr(9));
  if (action.compare(0, 5, "sell:") == 0) return sim.SellGood(action.substr(5));
  if (action.compare(0, 12, "sell-survey:") == 0) {
    return sim.SellSurvey(action.substr(12));
  }
  if (action.compare(0, 16, "contract-accept:") == 0) {
    return sim.AcceptContract(action.substr(16));
  }
  if (action.compare(0, 17, "contract-collect:") == 0) {
    return sim.CollectContract(action.substr(17));
  }
  if (action.compare(0, 5, "hire:") == 0) return sim.HireCrew(action.substr(5));
  if (action.compare(0, 8, "upgrade:") == 0) return sim.BuyUpgrade(action.substr(8));
  if (action == "ship-buy") return sim.BuyLargerShip();
  if (action.compare(0, 9, "room-buy:") == 0) return sim.BuyRoom(action.substr(9));
  if (action.compare(0, 17, "commission-accept") == 0) {
    return sim.AcceptCommission(action.substr(18));
  }
  if (action == "commission-collect") return sim.CollectCommission();
  if (action == "commission-abandon") return sim.AbandonCommission();
  if (action == "hull-service") {
    if (sim.State().gameOver.size() != 0 || !sim.State().docked) return false;
    if (sim.StatValue("hull") >= 100 || sim.State().shiplets < 120) return false;
    sim.MutableState().shiplets -= 120;
    sim.ClampStat("hull", 50);
    sim.PerformAction("Station repaired the hull");
    return true;
  }
  if (action == "undock") return sim.Undock();
  (void)arg;
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
std::string StationActionCategory(const Simulation& sim, const std::string& action) {
  // commandRow chooses from the displayed label, except the larger-ship
  // purchase which the HTA explicitly marks critical.
  if (action == "ship-buy") { return "critical"; }
  if (action.compare(0, 5, "sell:") == 0 || action.compare(0, 12, "sell-survey:") == 0) {
    return "routine";  // HTA gives these rows an explicit routine category.
  }
  const StationScreenModel model = BuildStationScreen(sim, sim.Map().here);
  for (const StationColumn& section : model.columns) {
    for (const StationRow& row : section.rows) {
      if (row.id == action) { return ActionCooldownCategory(row.label); }
    }
  }
  return ActionCooldownCategory(action);
}

bool PressStationAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:station:" + action + ":" + arg),
      StationActionCategory(sim, action),
      [&]() { return PressStationActionRaw(sim, action, arg); });
}

}  // namespace ship

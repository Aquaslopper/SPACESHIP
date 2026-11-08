// ============================================================================
//  StationScreen.cpp — the port window: cards, items, inspector, commits.
//  ---------------------------------------------------------------------------
//  Four cards, walked in one direction. What changed from the reference is how
//  they are walked and what a commitment costs:
//
//    left      four cards. Free to open. The open one is marked.
//    middle    only what the open card holds. Free to pick.
//    right     everything about the one picked item, and its single button.
//
//    Market, Shipyard and crew   shiplets only, 0 actions, no cooldown.
//    Cargo and surveys           pays shiplets, 1 action, the lot then goes.
//    Trade routes                buys the consignment, 1 action; the fee is
//                                collected at the destination, 1 action.
//
//  The mission board and the tavern cards are gone. The commission and contract
//  engine is still in the simulation; this window no longer offers it, because
//  every berth offered the same pinned list. The trade counter replaces it with
//  the one thing that is not shared: the contract of this berth's own system.
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
const double kScannerTwo = 1100;
const double kHullService = 120;
const double kLargeShip = 4000;

const char* kFree = "0 actions";
const char* kOneAction = "1 action";
const char* kDone = "done";

bool Owned(const Simulation& sim, const std::string& upgradeId) {
  const auto& upgrades = sim.State().upgrades;
  auto it = upgrades.find(upgradeId);
  return it != upgrades.end() && it->second;
}

bool Hired(const Simulation& sim, const std::string& candidateId) {
  const auto& hired = sim.State().hired;
  auto it = hired.find(candidateId);
  return it != hired.end() && it->second;
}

// This berth's system and the display name of any berth or system, so the trade
// card can name both ends of a run without the reader decoding ids.
std::string SystemNameOf(const Simulation& sim, const std::string& portId) {
  const Body* port = sim.FindBodyById(portId);
  if (port == nullptr) { return std::string(); }
  const std::string systemId =
      port->system.empty() ? sim.World().BodySystem(*port) : port->system;
  const Body* system = sim.FindBodyById(systemId);
  return system == nullptr ? std::string() : system->name;
}

std::string PortNameOf(const Simulation& sim, const std::string& portId) {
  const Port* port = sim.World().FindPort(portId);
  if (port != nullptr) { return port->name; }
  const Body* body = sim.FindBodyById(portId);
  return body == nullptr ? portId : body->name;
}

double GoodBuyPrice(const Simulation& sim, const Good& good) {
  GoodsLot probe;
  probe.value = good.value;
  probe.kind = good.kind;
  return js::Ceil(sim.LotPrice(probe) * 1.2);
}

double GoodSellPrice(const Simulation& sim, const Good& good) {
  GoodsLot probe;
  probe.value = good.value;
  probe.kind = good.kind;
  return sim.LotPrice(probe);
}

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
      "Pick a card, pick an item, then commit. Supplies, the yard and hires cost "
      "shiplets only; selling and trade contracts cost one action each.";

  const double purse = sim.State().shiplets;
  const double used = sim.CargoUsed();
  const double reserved = sim.CargoReserved();
  const double capacity = sim.CargoStore().capacity;
  model.purseLine = "Market - purse " + Num(purse) + " sh";
  model.holdLine = Num(used) + " of " + Num(capacity) + " m3 used; " + Num(reserved) +
                   " m3 reserved.";

  std::string tip = port == nullptr ? std::string() : port->tip;
  if (tip.empty()) {
    tip = model.portName == "Tolme Anchorage"
              ? "A trader says the Coldwell Relay pays well for produce, but sells ore cheaply."
              : "A trader says Tolme's market pays well for refined ore.";
  }
  model.tip = tip;

  // ---- 1. the market ------------------------------------------------------
  StationColumn market;
  market.id = "market";
  market.title = "Market - purse " + Num(purse) + " sh";
  market.badge = "purse " + Num(purse) + " sh";
  market.notes.push_back("Buy supplies and cargo. A different port may pay a different price.");
  market.notes.push_back("Shiplets only: no action is spent and nothing is locked.");

  const auto supply = [&market, purse](const std::string& id, const std::string& label,
                                       const std::string& what, double price, bool extra,
                                       const std::vector<std::string>& extraDetails) {
    StationRow row;
    row.id = id;
    row.label = label;
    row.cost = kFree;
    row.free = true;
    row.commit = "Buy - " + Num(price) + " sh";
    row.enabled = purse >= price && extra;
    row.details.push_back(what);
    row.details.push_back("Price: " + Num(price) + " sh at every port.");
    row.details.push_back("Purse: " + Num(purse) + " sh.");
    for (const std::string& line : extraDetails) { row.details.push_back(line); }
    if (!row.enabled) {
      row.details.push_back(
          purse < price ? "Not affordable: the purse is short."
                        : "Not available: a stock limit or active project blocks this purchase.");
    }
    market.rows.push_back(row);
  };

  supply("buy-fuel", "Fuel cell - 45 sh", "One fuel cell for the drive.", kFuelCell, true,
         {"Rack: " + Num(sim.State().fuelCells) + " fuel cells aboard."});
  supply("buy-repair", "Repair kit - 70 sh", "One repair kit for the machines.", kRepairKit, true,
         {"Rack: " + Num(sim.State().repairKits) + " repair kits aboard."});
  supply("buy-medkit", "Medkit - 50 sh", "One medkit for the medical bay.", kMedkit, true,
         {"Rack: " + Num(sim.State().medkits) + " medkits aboard."});
  supply("buy-canister", "O2 canister - 25 sh", "One O2 canister for the filter rack.", kCanister,
         sim.Life().canisters < sim.Life().capacity &&
             sim.FindOngoing("project-oxygen") == nullptr,
         {"Rack: " + Num(sim.Life().canisters) + " of " + Num(sim.Life().capacity) +
              " canisters.",
          "The Recyclers' oxygen project blocks this while it runs."});
  supply("buy-rations", "Six dry rations - 20 sh",
         "Six dry rations for the larder; the cook turns them into galley food.", kRations,
         sim.MessRations() <= sim.MessRationCapacity() - 6 &&
             sim.FindOngoing("project-food") == nullptr,
         {"Larder: " + Num(sim.MessRations()) + " of " +
              Num(sim.MessRationCapacity()) + " dry rations.",
          "Galley: " + Num(sim.MessRack()) + " of " +
              Num(sim.MessCapacity()) + " cooked food.",
          "One dry ration becomes up to " +
              Num(sim.GameRules().mess.foodPerRation) + " cooked food per batch.",
          "Hydroponics' ration-production project blocks purchases while it runs."});

  for (const Good& good : sim.Market()) {
    const double price = GoodBuyPrice(sim, good);
    const double sell = GoodSellPrice(sim, good);
    const bool space = used + reserved + good.vol <= capacity;
    StationRow row;
    row.id = "buy-good:" + good.id;
    row.label = "Buy " + good.name + " - " + Num(price) + " sh / " + Num(good.vol) + " m3";
    row.cost = kFree;
    row.free = true;
    row.commit = "Buy - " + Num(price) + " sh";
    row.enabled = purse >= price && space;
    row.details.push_back("Trade good: " + good.name + " (" + good.kind + ").");
    row.details.push_back("Volume: " + Num(good.vol) + " m3 per lot.");
    row.details.push_back("This port sells it in for " + Num(price) + " sh and would pay " +
                          Num(sell) + " sh for it.");
    row.details.push_back("Carried to another port it may pay more or less.");
    row.details.push_back("Hold: " + Num(used) + " of " + Num(capacity) + " m3 used, " +
                          Num(reserved) + " m3 reserved; this lot needs " + Num(good.vol) + " m3.");
    row.details.push_back("Purse: " + Num(purse) + " sh.");
    if (good.risk) { row.details.push_back("Flagged: this cargo draws attention."); }
    if (!row.enabled) {
      row.details.push_back(!space ? "No room in the hold for another lot."
                                   : "Not affordable at this price.");
    }
    market.rows.push_back(row);
  }

  // ---- 2. sell cargo and survey data --------------------------------------
  StationColumn sales;
  sales.id = "sales";
  sales.title = "Sell cargo and survey data";
  sales.badge = Num(sim.Goods().size()) + " lots, " + Num(sim.Survey().size()) + " surveys";
  sales.notes.push_back("Everything aboard that a counter will take.");
  sales.notes.push_back("Selling pays shiplets and costs one action; the lot is then gone.");
  if (sim.Goods().empty() && sim.Survey().empty()) {
    sales.notes.push_back("Nothing to sell.");
  }
  for (const GoodsLot& lot : sim.Goods()) {
    if (!lot.contractId.empty()) {
      sales.notes.push_back(lot.name + " - contract cargo, not for sale.");
      continue;
    }
    const double price = sim.LotPrice(lot);
    StationRow row;
    row.id = "sell:" + lot.id;
    row.label = "Sell " + lot.name + " - " + Num(price) + " sh";
    row.cost = kOneAction;
    row.commit = "Sell - " + Num(price) + " sh";
    row.enabled = model.docked;
    row.details.push_back("Cargo lot aboard: " + lot.name + " (" + lot.kind + ").");
    row.details.push_back("Volume: " + Num(lot.vol) + " m3, freed on sale.");
    row.details.push_back("This port pays " + Num(price) + " sh for it now.");
    row.details.push_back("Costs one action. The lot leaves the hold and cannot be sold twice.");
    sales.rows.push_back(row);
  }
  for (const SurveyRecord& record : sim.Survey()) {
    const bool packaging = sim.FindOngoing("pkg-" + record.id) != nullptr;
    StationRow row;
    row.id = "sell-survey:" + record.id;
    row.label = "Sell survey: " + record.name + " - " + Num(record.value) + " sh";
    row.cost = kOneAction;
    row.commit = "Sell - " + Num(record.value) + " sh";
    row.enabled = model.docked && !packaging;
    row.details.push_back("Survey data: " + record.name + ".");
    if (!record.src.empty()) { row.details.push_back("Taken from: " + record.src + "."); }
    if (!record.state.empty()) { row.details.push_back("State: " + record.state + "."); }
    row.details.push_back("This port pays " + Num(record.value) + " sh for the record.");
    row.details.push_back(packaging
                              ? "The Science Room is packaging this record; it cannot be sold yet."
                              : "Costs one action. The record is sold and leaves the ship.");
    sales.rows.push_back(row);
  }

  // ---- 4. the shipyard and crew -------------------------------------------
  StationColumn yard;
  yard.id = "yard";
  yard.title = "Shipyard and crew";
  yard.badge = Num(purse) + " sh, hull " + Num(sim.StatValue("hull")) + "%";
  yard.notes.push_back("Shiplets only: no action is spent and nothing is locked. A one-time "
                       "refit, room or hire greys out once it is owned.");
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
    ship.cost = kFree;
    ship.free = true;
    ship.commit = "Buy - 4,000 sh";
    ship.enabled = purse >= kLargeShip;
    ship.confirm =
        "Buy SC Tessellate for 4,000 shiplets? The crew, cargo, equipment, refits, tasks "
        "and voyage records transfer. This purchase is permanent.";
    ship.details.push_back("Kestrel-series SC Tessellate: " +
                           Num(sim.ShipCargoCapacity("kestrel", sim.CargoRefitLevel())) +
                           " m3 with your refits, 14 berths, a larger deck plan.");
    ship.details.push_back("Price: " + Num(kLargeShip) + " sh. Purse: " + Num(purse) + " sh.");
    ship.details.push_back("Crew, cargo, active work, equipment, refits, condition and every "
                           "voyage record transfer.");
    ship.details.push_back("Permanent, and it costs no action: shiplets only.");
    if (!ship.enabled) { ship.details.push_back("Not affordable from this purse."); }
    yard.rows.push_back(ship);
  } else {
    yard.notes.push_back("Your Kestrel-series ship: " + Num(capacity) + " m3 cargo, " +
                         Num(sim.Ship().Berths()) + " berths.");
    StationRow ship;
    ship.id = "ship-buy";
    ship.label = "Buy larger ship SC Tessellate - 4,000 sh";
    ship.cost = kDone;
    ship.commit = "Owned";
    ship.enabled = false;
    ship.done = true;
    ship.details.push_back("You are already flying the Kestrel-series SC Tessellate.");
    ship.details.push_back("Hull: " + Num(capacity) + " m3 cargo, " + Num(sim.Ship().Berths()) +
                           " berths.");
    yard.rows.push_back(ship);
  }

  const double bay1 = sim.ShipCargoCapacity(sim.State().shipClass, 1);
  const double bay2 = sim.ShipCargoCapacity(sim.State().shipClass, 2);
  const auto upgrade = [&yard, purse](const std::string& id, const std::string& label,
                                      const std::string& what, double price, bool available,
                                      bool ownedAlready) {
    StationRow row;
    row.id = "upgrade:" + id;
    row.label = label;
    row.details.push_back(what);
    row.details.push_back("Price: " + Num(price) + " sh. Purse: " + Num(purse) + " sh.");
    if (ownedAlready) {
      row.cost = kDone;
      row.commit = "Owned";
      row.enabled = false;
      row.done = true;
      row.details.push_back("Already fitted; it cannot be bought again.");
    } else {
      row.cost = kFree;
      row.free = true;
      row.commit = id == "scanner2" ? "Buy - 1,100 sh"
                                    : "Buy - " + Num(price) + " sh";
      row.enabled = available && purse >= price;
      row.details.push_back("Shiplets only: no action, no cooldown.");
      if (!row.enabled) {
        row.details.push_back(available ? "Not affordable from this purse."
                                        : "Not available yet: an earlier refit is required.");
      }
    }
    yard.rows.push_back(row);
  };
  upgrade("bay", "Expand cargo hold to " + Num(bay1) + " m3 - 500 sh",
          "Cargo refit 1: the hold grows to " + Num(bay1) + " m3.", kBayOne, true,
          Owned(sim, "bay"));
  upgrade("bay2", "Expand cargo hold to " + Num(bay2) + " m3 - 900 sh",
          "Cargo refit 2: the hold grows to " + Num(bay2) + " m3. Needs refit 1 first.", kBayTwo,
          Owned(sim, "bay"), Owned(sim, "bay2"));
  upgrade("drive", "Fit efficient drive: fuel use -20% - 900 sh",
          "Efficient drive: every crossing burns a fifth less fuel.", kDrive, true,
          Owned(sim, "drive"));
  upgrade("rack", "Expand O2 rack to 15 - 400 sh",
          "O2 rack expansion: the canister rack holds 15.", kRack, true, Owned(sim, "rack"));
  upgrade("scanner", "Upgrade sensors: scans take 3 + regional lock - 600 sh",
          "Sensor upgrade: a scan costs 3 actions and the first locked entry opens, with "
          "deep scans inside this region.",
          kScanner, true, Owned(sim, "scanner"));
  upgrade("scanner2", "Fit deep-array sensors: full unlock + deep-region scans - 1,100 sh",
          "Deep array: every locked survey entry opens, deep scans reach any region, and "
          "survey data is worth a quarter more. Needs the sensor upgrade first.",
          kScannerTwo, Owned(sim, "scanner"), Owned(sim, "scanner2"));
  {
    const double hull = sim.StatValue("hull");
    StationRow row;
    row.id = "hull-service";
    row.label = "Station hull service (+50%) - 120 sh";
    row.cost = kFree;
    row.free = true;
    row.commit = "Buy service - " + Num(kHullService) + " sh";
    row.enabled = hull < 100 && purse >= kHullService;
    row.details.push_back("The yard patches the hull for half again its condition.");
    row.details.push_back("Hull now: " + Num(hull) + "%. After service: " +
                          Num(std::min(100.0, hull + 50.0)) + "%.");
    row.details.push_back("Price: " + Num(kHullService) + " sh. Purse: " + Num(purse) + " sh.");
    row.details.push_back("Repeatable while the hull is short and the purse allows it; it "
                          "costs no action.");
    if (!row.enabled) {
      row.details.push_back(hull >= 100 ? "The hull is already whole; there is nothing to patch."
                                        : "Not affordable from this purse.");
    }
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
      row.cost = kFree;
      row.free = true;
      row.commit = "Install - " + Num(Simulation::RoomPrice()) + " sh";
      row.enabled = model.docked && purse >= Simulation::RoomPrice();
      row.confirm = "Install " + room.name + " for 500 shiplets?";
      row.details.push_back("Optional room: " + room.name + ".");
      if (!room.tag.empty()) { row.details.push_back("Fitted as: " + room.tag + "."); }
      row.details.push_back("Price: " + Num(Simulation::RoomPrice()) +
                            " sh, added to the deck plan of the ship you are flying.");
      row.details.push_back("Purse: " + Num(purse) + " sh. Shiplets only: no action.");
      if (!row.enabled) { row.details.push_back("Needs a dock at this station and the price."); }
      yard.rows.push_back(row);
    }
  }

  {
    StationRow heading;
    heading.label = "Hiring hall";
    heading.reading = true;
    yard.rows.push_back(heading);
  }
  for (const CandidateDefinition& candidate : sim.World().candidates) {
    if (!candidate.port.empty() && candidate.port != portId) { continue; }
    const CrewDefinition& person = candidate.crew;
    const std::string name = person.name;
    const bool already = Hired(sim, person.id);
    const bool berth = static_cast<double>(sim.Crew().size() + sim.Passengers().size()) <
                       sim.Ship().Berths();
    StationRow row;
    row.id = "hire:" + person.id;
    row.label = "Hire " + name + " - " + Num(candidate.fee) + " sh";
    row.details.push_back(name + ": " + person.traits + "; health " + Num(person.health) + "%, " +
                          (person.skill.empty() ? std::string("untrained") : person.skill) + ".");
    row.details.push_back("Role: " + person.role + ". Fee: " + Num(candidate.fee) + " sh.");
    row.details.push_back("Berths: " + Num(sim.Crew().size() + sim.Passengers().size()) +
                          " of " + Num(sim.Ship().Berths()) + " filled.");
    if (already) {
      row.cost = kDone;
      row.commit = "Hired";
      row.enabled = false;
      row.done = true;
      row.details.push_back("Already signed aboard; the fee is not asked twice.");
    } else {
      row.cost = kFree;
      row.free = true;
      row.commit = "Hire - " + Num(candidate.fee) + " sh";
      row.enabled = berth && purse >= candidate.fee;
      row.details.push_back("Shiplets only: no action, no cooldown.");
      if (!row.enabled) {
        row.details.push_back(berth ? "Not affordable from this purse."
                                    : "No berth free for another hand.");
      }
    }
    yard.rows.push_back(row);
  }

  // ---- 3. the trade counter ------------------------------------------------
  // Everything here is a fact about this berth: the station's own contract, the
  // system it sits in, and the run it names.
  StationColumn trade;
  trade.id = "trade";
  trade.title = "Trade routes";
  trade.badge = sim.TradesHere().empty() ? "no contract at this berth"
                                         : "1 contract offered here";
  {
    const std::string systemName = SystemNameOf(sim, portId);
    trade.notes.push_back(systemName.empty() ? "This berth's own contract."
                                             : "Contract bound to " + systemName + ".");
    trade.notes.push_back("Take the cargo here, deliver it at the named berth, then collect "
                          "the payment there.");
    trade.notes.push_back("Accepting and delivering cost one action each; the cargo is "
                          "bought with shiplets.");
  }
  for (const TradeContract& offer : sim.TradesHere()) {
    const std::string status = sim.TradeStatus(offer.id);
    const std::string where = PortNameOf(sim, offer.destination);
    StationRow row;
    row.details.push_back("Trade contract: " + offer.title + ".");
    row.details.push_back(offer.note);
    row.details.push_back("Run: " + offer.kind + " - " + offer.goodName + ", " +
                          Num(static_cast<double>(offer.vol)) + " m3, " +
                          Num(static_cast<double>(offer.units)) + " lot.");
    row.details.push_back("Delivery berth: " + where + ".");
    row.details.push_back("Cargo bought here for " + Num(offer.price) + " sh; the fee is " +
                          Num(offer.payment) + " sh paid at " + where + ".");
    row.details.push_back("Status: " + status + ".");

    if (status == "available") {
      const bool affordable = purse >= offer.price;
      const bool space = used + reserved + static_cast<double>(offer.vol) <= capacity;
      row.id = "trade-accept:" + offer.id;
      row.label = "Take " + offer.goodName + " to " + where + " - " + Num(offer.payment) + " sh";
      row.cost = kOneAction;
      row.commit = "Buy and sign - " + Num(offer.price) + " sh";
      row.enabled = model.docked && affordable && space;
      row.details.push_back("Costs one action and " + Num(offer.price) +
                            " sh; the cargo goes into the hold, reserved for this run.");
      if (!row.enabled) {
        row.details.push_back(!space ? "Not enough hold space for the consignment."
                                     : "Not affordable at this berth.");
      }
    } else if (status == "carried") {
      row.id = "trade-accept:" + offer.id;
      row.label = "Under contract: " + offer.goodName + " for " + where;
      row.cost = kDone;
      row.commit = "Under way";
      row.enabled = false;
      row.done = true;
      row.details.push_back("Deliver to " + where + " to unlock the payment.");
    } else if (status == "ready") {
      row.id = "trade-collect:" + offer.id;
      row.label = "Collect " + Num(offer.payment) + " sh for " + offer.goodName;
      row.cost = kOneAction;
      row.commit = "Collect - " + Num(offer.payment) + " sh";
      row.enabled = model.docked;
      row.details.push_back("The consignment has been handed over here. Costs one action; the "
                            "payment goes into the purse.");
    } else {
      row.id = "trade-accept:" + offer.id;
      row.label = "Paid: " + offer.goodName + " for " + where;
      row.cost = kDone;
      row.commit = "Paid";
      row.enabled = false;
      row.done = true;
      row.details.push_back("This run has been paid out; it cannot be taken again.");
    }
    trade.rows.push_back(row);
  }
  // Consignments taken at other berths and not yet handed over: they ride in the
  // hold, so this berth shows them rather than pretending they are not there.
  for (const auto& entry : sim.State().trades) {
    const TradeRun& run = entry.second;
    if (run.state != "carried") { continue; }
    bool offeredHere = false;
    for (const TradeContract& offer : sim.TradesHere()) {
      if (offer.id == run.id) { offeredHere = true; }
    }
    if (offeredHere) { continue; }
    StationRow row;
    row.id = "trade-elsewhere:" + run.id;
    row.label = "Carrying " + run.goodName + " to " + PortNameOf(sim, run.destination);
    row.cost = kDone;
    row.commit = "In the hold";
    row.enabled = false;
    row.done = true;
    row.reading = true;
    row.details.push_back("Contract cargo aboard: " + run.goodName + ", " +
                          Num(static_cast<double>(run.vol)) + " m3.");
    row.details.push_back("Hand it over at " + PortNameOf(sim, run.destination) +
                          " to unlock " + Num(run.payment) + " sh.");
    row.details.push_back("Contract freight cannot be sold or jettisoned for profit.");
    trade.rows.push_back(row);
  }

  model.columns = {market, sales, trade, yard};
  NormalizeStationSelection(model, model.selection);
  return model;
}

// ---------------------------------------------------------------------------
//  The selection
// ---------------------------------------------------------------------------
const StationColumn* FindStationCategory(const StationScreenModel& model,
                                         const std::string& categoryId) {
  for (const StationColumn& column : model.columns) {
    if (column.id == categoryId) { return &column; }
  }
  return nullptr;
}

const StationRow* FindStationItem(const StationScreenModel& model,
                                  const std::string& categoryId,
                                  const std::string& itemId) {
  const StationColumn* column = FindStationCategory(model, categoryId);
  if (column == nullptr || itemId.empty()) { return nullptr; }
  for (const StationRow& row : column->rows) {
    if (row.id == itemId) { return &row; }
  }
  return nullptr;
}

void NormalizeStationSelection(const StationScreenModel& model, StationSelection& selection) {
  if (model.columns.empty()) { selection.category.clear(); selection.item.clear(); return; }

  const StationColumn* column = FindStationCategory(model, selection.category);
  if (column == nullptr) {
    // Nothing picked yet, or a card that is not on this station: open the first
    // card that actually holds something to pick.
    column = &model.columns.front();
    for (const StationColumn& candidate : model.columns) {
      bool hasItem = false;
      for (const StationRow& row : candidate.rows) {
        if (!row.reading && !row.id.empty()) { hasItem = true; break; }
      }
      if (hasItem) { column = &candidate; break; }
    }
    selection.category = column->id;
    selection.item.clear();
  }

  if (FindStationItem(model, selection.category, selection.item) == nullptr) {
    selection.item.clear();
    for (const StationRow& row : column->rows) {
      if (row.reading || row.id.empty()) { continue; }
      selection.item = row.id;
      break;
    }
  }
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
  if (action.compare(0, 14, "trade-accept:") == 0) return sim.AcceptTrade(action.substr(14));
  if (action.compare(0, 15, "trade-collect:") == 0) return sim.CollectTrade(action.substr(15));
  if (action.compare(0, 5, "hire:") == 0) return sim.HireCrew(action.substr(5));
  if (action.compare(0, 8, "upgrade:") == 0) return sim.BuyUpgrade(action.substr(8));
  if (action == "ship-buy") return sim.BuyLargerShip();
  if (action.compare(0, 9, "room-buy:") == 0) return sim.BuyRoom(action.substr(9));
  if (action == "hull-service") {
    if (sim.State().gameOver.size() != 0 || !sim.State().docked) return false;
    if (sim.StatValue("hull") >= 100 || sim.State().shiplets < kHullService) return false;
    sim.MutableState().shiplets -= kHullService;
    sim.ClampStat("hull", 50);
    sim.FreeCommit("Station repaired the hull");
    return true;
  }
  (void)arg;
  return false;
}

}  // namespace

// ============================================================================
//  The press.
//  ---------------------------------------------------------------------------
//  This window starts no cooldown. The money-only counter refuses a press the
//  moment the purse or the hold refuses it, and every row that does cost an
//  action changes the state that makes a second press impossible: the lot is
//  gone, the contract is taken, the patron has been spoken to, the berth is
//  left. Nothing here can be pressed twice for one effect.
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
  return PressStationActionRaw(sim, action, arg);
}

}  // namespace ship

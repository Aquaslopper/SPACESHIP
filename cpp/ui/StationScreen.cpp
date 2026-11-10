// ============================================================================
//  StationScreen.cpp — the port window: cards, items, inspector, commits.
//  ---------------------------------------------------------------------------
//  The cards are the reference's panels minus the mission board and the
//  tavern, plus the starmap the station keeps of its own system. What changed
//  is how they are walked and what a commitment costs:
//
//    left      four cards. Free to open. The open one is marked.
//    middle    only what the open card holds. Free to pick.
//    right     everything about the one picked item, and its single button.
//
//    Market, Shipyard and crew   shiplets only, 0 actions, no click spent.
//    Starmap                     free, and there is nothing to commit: the
//                                station's page for its system, read planet
//                                by planet.
//    Cargo and surveys           pays shiplets, 1 action, the lot then goes.
//    Contract board              1 action, the button greys once accepted.
// ============================================================================
#include "ui/StationScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"
#include "ui/Shell.h"

#include <algorithm>
#include <cmath>
#include <map>
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
const double kGalley = 400;
const double kFilter = 600;
const double kHullService = 120;
const double kLargeShip = 4000;

const char* kFree = "0 clicks";
const char* kOneAction = "1 click";
const char* kDone = "done";

bool Talked(const Simulation& sim, const std::string& patronId) {
  const auto& talked = sim.State().talked;
  auto it = talked.find(patronId);
  return it != talked.end() && it->second;
}

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

std::string ContractStatus(const Simulation& sim, const std::string& contractId) {
  const auto& contracts = sim.State().contracts;
  auto it = contracts.find(contractId);
  return it == contracts.end() ? std::string("available") : it->second;
}

// The region this station sits in, for the two lines the bar repeats.
std::vector<std::string> LocalRumors(const Simulation& sim, const std::string& portId) {
  std::string regionId;
  const Body* portBody = sim.FindBodyById(portId);
  if (portBody != nullptr) {
    const std::string systemId =
        portBody->system.empty() ? sim.World().BodySystem(*portBody) : portBody->system;
    const Body* system = sim.FindBodyById(systemId);
    if (system != nullptr) { regionId = system->region; }
  }
  if (regionId.empty()) { regionId = sim.CurrentSystemId(); }
  const auto& history = sim.World().worldHistory;
  auto it = history.find(regionId);
  if (it == history.end()) { return {}; }
  return it->second;
}

std::string RegionName(const Simulation& sim, const std::string& portId) {
  const Body* portBody = sim.FindBodyById(portId);
  if (portBody == nullptr) { return std::string(); }
  const std::string systemId =
      portBody->system.empty() ? sim.World().BodySystem(*portBody) : portBody->system;
  const Body* system = sim.FindBodyById(systemId);
  if (system == nullptr) { return std::string(); }
  const Body* region = sim.FindBodyById(system->region);
  return region == nullptr ? system->region : region->name;
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

// The trader's tip: the port's own line, then what this counter pays
// best and worst for, read straight off the price basket in play.
std::vector<std::string> TraderSpeech(const Simulation& sim, const std::string& tip) {
  std::vector<std::string> said;
  if (!tip.empty()) { said.push_back("The trader says: " + tip); }
  const Good* best = nullptr;
  const Good* worst = nullptr;
  for (const Good& good : sim.Market()) {
    if (best == nullptr || GoodSellPrice(sim, good) > GoodSellPrice(sim, *best)) { best = &good; }
    if (worst == nullptr || GoodBuyPrice(sim, good) < GoodBuyPrice(sim, *worst)) { worst = &good; }
  }
  if (best != nullptr) {
    said.push_back("Trade tip: this port pays " + Num(GoodSellPrice(sim, *best)) + " sh for " +
                   best->name + " (" + best->kind + ").");
  }
  if (worst != nullptr && worst != best) {
    said.push_back("Trade tip: " + worst->name + " goes in for " +
                   Num(GoodBuyPrice(sim, *worst)) + " sh here, the cheapest on the board.");
  }
  for (const std::string& rumor : LocalRumors(sim, sim.Map().here)) {
    said.push_back("Heard at the bar: " + rumor);
  }
  return said;
}

// What a candidate says when spoken to: their own fields, then the
// two lines the region was written with. Nothing here is composed.
std::vector<std::string> PatronSpeech(const Simulation& sim, const CandidateDefinition& person) {
  const CrewDefinition& crew = person.crew;
  std::vector<std::string> said;
  if (!crew.traits.empty()) { said.push_back(crew.name + " on themselves: " + crew.traits + "."); }
  if (!crew.skill.empty() || !crew.role.empty()) {
    said.push_back(crew.name + " on their trade: " + (crew.skill.empty() ? std::string("Untrained") : crew.skill) +
                   ", " + crew.role + ".");
  }
  if (!crew.hobby.empty()) { said.push_back("Off watch: " + crew.hobby + "."); }
  if (!crew.homeworld.empty()) { said.push_back("From: " + crew.homeworld + "."); }
  for (const std::string& rumor : LocalRumors(sim, sim.Map().here)) {
    said.push_back(crew.name + " repeats a rumor: " + rumor);
  }
  const std::string region = RegionName(sim, sim.Map().here);
  if (!region.empty()) { said.push_back("They are waiting out " + region + "'s next berth."); }
  return said;
}

}  // namespace

// ---------------------------------------------------------------------------
//  The station's starmap
// ---------------------------------------------------------------------------
//  Every station writes a page for the system it stands in, out of the chart
//  itself: the system's own reading, then every charted body of that system,
//  grouped and ordered so the page reads like a wiki entry. There is no
//  page anywhere and no per-port table to keep in step - a station that exists
//  has its page, because the page is the system it sits in.
StarmapPage BuildStarmapPage(const Simulation& sim, const std::string& portId) {
  StarmapPage page;
  const Body* berth = sim.FindBodyById(portId);
  if (berth == nullptr) { return page; }

  page.systemId = sim.World().BodySystem(*berth);
  const Body* systemBody = page.systemId.empty() ? nullptr : sim.FindBodyById(page.systemId);
  page.systemName = systemBody != nullptr
                        ? systemBody->name
                        : (page.systemId.empty() ? portId : page.systemId);
  if (systemBody != nullptr) {
    page.systemReading = systemBody->scan;
    if (page.systemReading.empty()) {
      for (const auto& detail : systemBody->details) {
        page.systemReading.push_back(detail.first + ": " + detail.second);
      }
    }
  }
  if (page.systemId.empty()) { return page; }

  // Group order: the star, its worlds, their moons, the wrecks, other berths,
  // then anything the chart tags differently.
  static const char* kGroups[] = {"Star", "Planets", "Moons", "Belts and wrecks",
                                  "Stations", "Other charted bodies"};
  constexpr int kGroupCount = 6;
  std::map<int, std::vector<StarmapEntry>> byGroup;
  for (std::size_t index : sim.World().BodiesInSystem(page.systemId)) {
    const Body& body = sim.World().bodies[index];
    if (body.id == portId) { continue; }  // the station does not write itself down

    int group = kGroupCount - 1;
    if (body.type == "star") {
      group = 0;
    } else if (body.type == "planet") {
      group = 1;
      ++page.planetCount;
    } else if (body.type == "moon") {
      group = 2;
    } else if (body.type == "belt" || body.type == "site") {
      group = 3;
    } else if (body.type == "station") {
      group = 4;
    }

    StarmapEntry entry;
    entry.id = body.id;
    entry.name = body.name;
    entry.kind = body.klass.empty() ? body.type : body.klass;
    entry.group = kGroups[group];
    entry.reading = body.scan;
    if (entry.reading.empty()) {
      // A body the chart without prose still has its own facts.
      for (const auto& detail : body.details) {
        entry.reading.push_back(detail.first + ": " + detail.second);
      }
    }
    byGroup[group].push_back(std::move(entry));
  }
  for (auto& bucket : byGroup) {
    for (StarmapEntry& entry : bucket.second) { page.entries.push_back(std::move(entry)); }
  }
  return page;
}

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
      "shiplets only; selling, contracts and the bar cost one click.";

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
  market.notes.push_back("Shiplets only: no click is spent and nothing is locked.");

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

  // ---- 2. the starmap -----------------------------------------------------
  // The station's own page for the system it sits in. Nothing here is
  // per station and nothing is bought: every station the voyage can reach
  // writes its page from the system it stands in, the moment its window opens.
  StationColumn starmap;
  starmap.id = "starmap";
  starmap.title = "Starmap";
  {
    const StarmapPage page = BuildStarmapPage(sim, portId);
    starmap.badge = page.systemName + " - " + std::to_string(page.planetCount) + " planets, " +
                    std::to_string(page.entries.size()) + " charted bodies";
    starmap.notes.push_back("The station's own page for " + page.systemName + ".");
    for (const std::string& line : page.systemReading) { starmap.notes.push_back(line); }
    starmap.notes.push_back("Reading is free: nothing on this page costs a click.");
    if (page.entries.empty()) {
      starmap.notes.push_back("Nothing is charted in this system yet.");
    } else {
      starmap.notes.push_back("Pick a body to read the station's entry for it.");
    }
    std::string group;
    for (const StarmapEntry& entry : page.entries) {
      if (entry.group != group) {
        group = entry.group;
        StationRow heading;
        heading.label = group;
        heading.reading = true;   // a heading, not a pick
        starmap.rows.push_back(heading);
      }
      StationRow row;
      row.id = "starmap:" + entry.id;
      row.label = entry.name + " - " + entry.kind;
      row.cost = kFree;
      row.free = true;   // reading costs neither an action nor shiplets
      row.page = true;   // a page entry: there is nothing to commit
      row.enabled = true;
      row.details.push_back("System: " + page.systemName + ".");
      row.details.push_back("Kind: " + entry.kind + ".");
      for (const std::string& line : entry.reading) { row.details.push_back(line); }
      starmap.rows.push_back(row);
    }
  }

  // ---- 3. trade contracts --------------------------------------------------
  // Three buttons in the middle column, and the list each one opens on the
  // right. Nothing here is per station: delivery and freelance are the
  // system's own draw (Simulation::TradeBoard) laid over the bodies this
  // station's starmap prints, and station-to-station is the roll of stations the
  // player has docked at.
  StationColumn trade;
  trade.id = "trade";
  trade.title = "Trade contract";
  {
    std::vector<TradeMode> modes;

    // The drawn half of the board. Every run names a body of this system and the
    // commodity that body wants, so the offer can be checked against the page.
    const std::vector<TradeRun> runs = sim.TradeBoard(portId);

    TradeMode delivery;
    delivery.id = "delivery";
    delivery.title = "Delivery";
    delivery.note = "Carry a consignment from this station to its destination.";
    delivery.empty = "No deliveries are posted at this station right now.";

    TradeMode freelance;
    freelance.id = "freelance";
    freelance.title = "Freelance";
    freelance.note = "Buy the goods here, carry them on, sell them where they are wanted.";
    freelance.empty = "No freelance requests are drawn from this system right now.";

    for (const TradeRun& run : runs) {
      const Body* destination = sim.FindBodyById(run.to);
      const std::string where = destination == nullptr ? run.to : destination->name;
      std::string commodity = run.good;
      double commodityVol = 0.0;
      for (const Good& good : sim.World().goods) {
        if (good.id == run.good) { commodity = good.name; commodityVol = good.vol; }
      }
      TradeOffer offer;
      offer.mode = run.mode;
      offer.to = run.to;
      offer.good = run.good;
      offer.label = where + " - " + Num(run.crates) + (run.crates == 1 ? " crate of " : " crates of ") +
                    commodity;
      offer.note = run.mode == "delivery"
                       ? "Pays " + Num(run.fee) + " sh on hand-over; the station loads it."
                       : "Buy the lot here first; about " + Num(run.fee) + " sh over your cost there.";
      offer.accepted = run.accepted;
      // A delivery is loaded into the hold the moment it is signed, so a row the
      // hold cannot take has to say so rather than sit there live and refuse the
      // press. Freelance loads nothing here and is always open.
      // A freelance run is the player's own purchase, so it is never refused at
      // signing and never greyed here: the row says what it wants and lets the
      // player decide. The game does not take him by the hand.
      if (run.mode == "freelance") {
        const double perCrate = sim.GoodCounterPrice(run.good);
        const double cost = perCrate * run.crates;
        const double hold = commodityVol * run.crates;
        const double free = std::max(0.0, sim.CargoCapacity() - sim.CargoUsed() -
                                              sim.CargoReserved());
        offer.label += " - needs " + Num(hold) + " m3 and " + Num(cost) + " sh";
        offer.note += " Hold " + Num(free) + " m3 free; purse " +
                      Num(sim.State().shiplets) + " sh.";
        if (cost > sim.State().shiplets) {
          offer.note += " Short " + Num(cost - sim.State().shiplets) + " sh.";
        }
        if (hold > free) { offer.note += " Needs " + Num(hold - free) + " m3 more room."; }
      }
      if (run.mode == "delivery" && !run.accepted) {
        const double needed = commodityVol * run.crates;
        const double free = sim.CargoCapacity() - sim.CargoUsed() - sim.CargoReserved();
        if (needed > free) {
          offer.fits = false;
          offer.blocked = "Hold full: needs " + Num(needed) + " m3, " +
                          Num(std::max(0.0, free)) + " m3 free. Sell or unload first.";
        }
      }
      (run.mode == "delivery" ? delivery : freelance).offers.push_back(std::move(offer));
    }
    modes.push_back(std::move(delivery));
    modes.push_back(std::move(freelance));

    TradeMode station;
    station.id = "station";
    station.title = "Station-to-station";
    station.note = "Open a data survey trade with a station you have already docked at.";
    station.empty = "Dock at another station first; this list is every station visited so far.";
    for (const auto& visit : sim.State().visitedPorts) {
      if (!visit.second || visit.first == portId) { continue; }
      const Port* other = sim.World().FindPort(visit.first);
      const std::string otherName = other == nullptr ? visit.first : other->name;
      TradeOffer offer;
      offer.mode = "station";
      offer.to = visit.first;
      offer.label = otherName + " - data survey trade";
      offer.note = "A survey run between two stations; the route belongs on the sector chart.";
      offer.accepted = sim.TradeAccepted("station", visit.first);
      station.offers.push_back(std::move(offer));
    }
    modes.push_back(std::move(station));

    for (const TradeMode& mode : modes) {
      StationRow row;
      row.id = "trade-mode:" + mode.id;
      row.label = mode.title;
      row.cost = kFree;
      row.free = true;
      row.page = true;   // choosing a mode is reading: nothing to commit
      row.enabled = true;
      row.details.push_back(mode.note);
      if (mode.offers.empty()) {
        row.details.push_back(mode.empty);
      } else {
        row.details.push_back(std::to_string(mode.offers.size()) +
                              (mode.offers.size() == 1 ? " offer on the board." : " offers on the board."));
      }
      trade.rows.push_back(row);
    }
    trade.badge = "delivery, freelance, station-to-station";
    trade.notes.push_back("Three kinds of work, all of it settled at a station.");
    trade.notes.push_back(
        "Accepting a trade costs one click and greys its own row; the window never greys twice.");
    model.tradeModes = std::move(modes);
  }

  // ---- 4. sell cargo and survey data --------------------------------------
  StationColumn sales;
  sales.id = "sales";
  sales.title = "Sell cargo and survey data";
  sales.badge = Num(sim.Goods().size()) + " lots, " + Num(sim.Survey().size()) + " surveys";
  sales.notes.push_back("Everything aboard that a counter will take.");
  sales.notes.push_back("Selling pays shiplets and costs one click; the lot is then gone.");
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
    row.details.push_back("Costs one click. The lot leaves the hold and cannot be sold twice.");
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
                              : "Costs one click. The record is sold and leaves the ship.");
    sales.rows.push_back(row);
  }

  // ---- 5. the mission board -----------------------------------------------
  StationColumn board;
  board.id = "board";
  board.title = "Mission board";
  board.badge = sim.State().commissions.active.has_value() ? "one commission running"
                                                           : "commissions open";
  board.notes.push_back("Repeatable commissions");
  board.notes.push_back("Accepting or collecting work costs one click; the button greys "
                        "the moment the mission is taken.");
  if (sim.State().commissions.active.has_value()) {
    const Commission& active = *sim.State().commissions.active;
    const double goal = CommissionGoal(active.kind);
    board.notes.push_back(CommissionDescription(sim, active) + " - " +
                          Num(active.progress) + " / " + Num(goal) + ". Payment: " +
                          Num(active.reward) + " sh at any station.");
    StationRow collect;
    collect.id = "commission-collect";
    collect.label = "Collect commission - " + Num(active.reward) + " sh";
    collect.cost = kOneAction;
    collect.commit = "Collect - " + Num(active.reward) + " sh";
    collect.enabled = active.progress >= goal;
    collect.details.push_back(CommissionDescription(sim, active) + ".");
    collect.details.push_back("Progress: " + Num(active.progress) + " of " + Num(goal) + ".");
    collect.details.push_back("Payment: " + Num(active.reward) + " sh into the purse.");
    collect.details.push_back(collect.enabled
                                  ? "Costs one click. The offer rotates afterwards."
                                  : "The objective is not met yet; nothing to collect.");
    board.rows.push_back(collect);
    StationRow abandon;
    abandon.id = "commission-abandon";
    abandon.label = "Abandon commission and rotate offers";
    abandon.cost = kOneAction;
    abandon.commit = "Abandon and rotate";
    abandon.confirm = "Abandon this commission? The reward will be lost.";
    abandon.enabled = true;
    abandon.details.push_back("Give up the running commission and draw three fresh offers.");
    abandon.details.push_back("The " + Num(active.reward) + " sh reward is lost.");
    abandon.details.push_back("Costs one click.");
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
      row.cost = kOneAction;
      row.commit = "Accept";
      row.enabled = true;
      row.details.push_back("Repeatable commission: " + CommissionDescription(sim, *offer) + ".");
      row.details.push_back("Objective: " + kind + ", " + Num(CommissionGoal(kind)) +
                            " to complete.");
      row.details.push_back("Payment: " + Num(offer->reward) + " sh at any station.");
      row.details.push_back("Costs one click, and the board closes to other offers until it "
                            "is collected or abandoned.");
      board.rows.push_back(row);
    }
  }
  { StationRow heading; heading.label = "Regular contracts"; heading.reading = true; board.rows.push_back(heading); }
  for (const Contract& contract : sim.World().contracts) {
    const std::string status = ContractStatus(sim, contract.id);
    const bool eligible =
        (contract.origin.empty() || contract.origin == sim.Map().here) &&
        (contract.vol == 0 || used + reserved + contract.vol <= capacity) &&
        (contract.goal != "passenger" ||
         static_cast<double>(sim.Crew().size() + sim.Passengers().size()) <
             sim.Ship().Berths());

    StationRow row;
    row.details.push_back("Contract: " + contract.title + ".");
    row.details.push_back(contract.note);
    row.details.push_back("Objective: " + contract.goal +
                          (contract.target.empty() ? std::string() : " - " + contract.target) + ".");
    if (contract.vol > 0) {
      row.details.push_back("Freight: " + (contract.cargo.empty() ? std::string("consignment")
                                                                  : contract.cargo) +
                            ", " + Num(contract.vol) + " m3 of the hold, reserved while it runs.");
    }
    if (!contract.origin.empty()) { row.details.push_back("Offered only at: " + contract.origin + "."); }
    row.details.push_back("Reward: " + Num(contract.reward) + " sh, collected at a station.");
    row.details.push_back("Status: " + status + ".");

    if (status == "available" || status == "failed") {
      row.id = "contract-accept:" + contract.id;
      row.label = "Accept " + contract.title;
      row.cost = kOneAction;
      row.commit = "Accept";
      row.enabled = eligible;
      row.details.push_back(eligible ? "Costs one click; the button greys once it is taken."
                                     : "Not eligible from this berth: origin, hold space or berths.");
    } else if (status == "ready") {
      row.id = "contract-collect:" + contract.id;
      row.label = "Collect " + contract.title + " - " + Num(contract.reward) + " sh";
      row.cost = kOneAction;
      row.commit = "Collect - " + Num(contract.reward) + " sh";
      row.enabled = true;
      row.details.push_back("Costs one click; the reward goes into the purse.");
    } else {
      row.id = "contract-accept:" + contract.id;
      row.label = "Accept " + contract.title;
      row.cost = kDone;
      row.commit = status == "complete" ? "Completed" : "Accepted";
      row.enabled = false;
      row.done = true;
      row.details.push_back(status == "complete" ? "Already paid out; it cannot be taken again."
                                                 : "Already taken; it cannot be accepted again.");
    }
    board.rows.push_back(row);
  }

  // ---- 6. the shipyard and crew -------------------------------------------
  StationColumn yard;
  yard.id = "yard";
  yard.title = "Shipyard and crew";
  yard.badge = Num(purse) + " sh, hull " + Num(sim.StatValue("hull")) + "%";
  yard.notes.push_back("Shiplets only: no click is spent and nothing is locked. A one-time "
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
    ship.details.push_back("Permanent, and it costs no click: shiplets only.");
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
      row.details.push_back("Shiplets only: no click spent.");
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
  upgrade("galley", "Expand galley rack to 34 - 400 sh",
          "Galley refit: the counter holds 34 instead of 24, so the cook can keep a "
          "round and a half ahead of the crew.",
          kGalley, true, Owned(sim, "galley"));
  upgrade("filter", "Re-sleeve filter cores: a charge holds 300 clicks - 600 sh",
          "Filter refit: one bank charge lasts 300 clicks instead of 200.",
          kFilter, true, Owned(sim, "filter"));
  upgrade("scanner", "Upgrade sensors: scans take 3 + regional lock - 600 sh",
          "Sensor upgrade: a scan costs 3 clicks and the first locked entry opens, with "
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
                          "costs no click.");
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
      row.details.push_back("Purse: " + Num(purse) + " sh. Shiplets only: no click.");
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
      row.details.push_back("Shiplets only: no click spent.");
      if (!row.enabled) {
        row.details.push_back(berth ? "Not affordable from this purse."
                                    : "No berth free for another hand.");
      }
    }
    yard.rows.push_back(row);
  }

  // ---- 7. the tavern / bar -------------------------------------------------
  StationColumn tavern;
  tavern.id = "tavern";
  tavern.title = "Tavern / Bar";
  tavern.badge = "the people at this station";
  tavern.notes.push_back(tip);
  tavern.notes.push_back("Station training only finishes while docked.");
  tavern.notes.push_back("Talking costs one click, once per person; the button greys after "
                         "the first word.");
  {
    const bool said = Talked(sim, "trader");
    StationRow row;
    row.id = "talk:trader";
    row.label = "The trader at the bar";
    row.commit = said ? "Spoken to" : "Talk";
    row.cost = said ? kDone : kOneAction;
    row.done = said;
    row.enabled = !said && model.docked;
    row.details.push_back("A dealer who works this berth and talks to anyone who buys a round.");
    if (said) {
      for (const std::string& line : TraderSpeech(sim, tip)) { row.details.push_back(line); }
    } else {
      row.details.push_back("Talking costs one click and reveals their trade tip and what the "
                            "bar is saying about this region.");
    }
    tavern.rows.push_back(row);
  }
  for (const CandidateDefinition& candidate : sim.World().candidates) {
    if (!candidate.port.empty() && candidate.port != portId) { continue; }
    if (Hired(sim, candidate.crew.id)) { continue; }  // aboard now, not at the bar
    const CrewDefinition& person = candidate.crew;
    const bool said = Talked(sim, person.id);
    StationRow row;
    row.id = "talk:" + person.id;
    row.label = person.name + " - " + person.role;
    row.commit = said ? "Spoken to" : "Talk";
    row.cost = said ? kDone : kOneAction;
    row.done = said;
    row.enabled = !said && model.docked;
    row.details.push_back(person.name + ": " + person.traits + "; health " +
                          Num(person.health) + "%, " +
                          (person.skill.empty() ? std::string("untrained") : person.skill) + ".");
    row.details.push_back("Waiting at this station; the hiring hall asks " +
                          Num(candidate.fee) + " sh for them.");
    if (said) {
      for (const std::string& line : PatronSpeech(sim, candidate)) { row.details.push_back(line); }
    } else {
      row.details.push_back("Talking costs one click and reveals what they say about "
                            "themselves and the rumor they are repeating.");
    }
    tavern.rows.push_back(row);
  }

  // The Mission board and Tavern / Bar cards are not offered. The starmap sits
  // directly under the shop, the trade board under that, then the sales counter
  // and the yard.
  model.columns = {market, starmap, trade, sales, yard};
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
    if (sim.StatValue("hull") >= 100 || sim.State().shiplets < kHullService) return false;
    sim.MutableState().shiplets -= kHullService;
    sim.ClampStat("hull", 50);
    sim.FreeCommit("Station repaired the hull");
    return true;
  }
  if (action.compare(0, 13, "trade-accept:") == 0) {
    // mode : destination [: commodity] -- the commodity is only carried by the
    // two in-system modes, and a station-to-station run names a berth instead.
    const std::string rest = action.substr(13);
    const std::size_t cut = rest.find(':');
    if (cut == std::string::npos) { return false; }
    const std::string mode = rest.substr(0, cut);
    const std::string tail = rest.substr(cut + 1);
    const std::size_t second = tail.find(':');
    if (second == std::string::npos) { return sim.AcceptTrade(mode, tail); }
    return sim.AcceptTrade(mode, tail.substr(0, second), tail.substr(second + 1));
  }
  if (action.compare(0, 5, "talk:") == 0) return sim.TalkToPatron(action.substr(5));
  (void)arg;
  return false;
}

}  // namespace

// ============================================================================
//  The press.
//  ---------------------------------------------------------------------------
//  This window spends no click. The money-only counter refuses a press the
//  moment the purse or the hold refuses it, and every row that does cost an
//  action changes the state that makes a second press impossible: the lot is
//  gone, the contract is taken, the patron has been spoken to, the berth is
//  left. Nothing here can be pressed twice for one effect.
// ============================================================================
std::string StationActionCategory(const Simulation& sim, const std::string& action) {
  // commandRow chooses from the displayed label, except the larger-ship
  // purchase which the HTA explicitly marks critical.
  if (action == "ship-buy") { return "critical"; }
  if (action.compare(0, 13, "trade-accept:") == 0) { return "routine"; }
  if (action.compare(0, 5, "sell:") == 0 || action.compare(0, 12, "sell-survey:") == 0) {
    return "routine";  // HTA gives these rows an explicit routine category.
  }
  const StationScreenModel model = BuildStationScreen(sim, sim.Map().here);
  for (const StationColumn& section : model.columns) {
    for (const StationRow& row : section.rows) {
      if (row.id == action) { return ButtonChargeTier(row.label); }
    }
  }
  return ButtonChargeTier(action);
}

namespace {

const StationRow* FindStationRowById(const StationScreenModel& model, const std::string& id) {
  for (const StationColumn& section : model.columns) {
    for (const StationRow& row : section.rows) {
      if (row.id == id) { return &row; }
    }
  }
  return nullptr;
}

}  // namespace

bool PressStationAction(Simulation& sim, const std::string& action, const std::string& arg) {
  // The shop, the shipyard and a hire are paid in shiplets: money is their whole
  // gate, so they hold no clicks and start no green. A trade confirmation and
  // every other commit row is one click of the clock, like the rest of the ship.
  const StationScreenModel model = BuildStationScreen(sim, sim.Map().here);
  const StationRow* row = FindStationRowById(model, action);
  if (row != nullptr && row->free) {
    // Bought with shiplets: no clicks are spent, but the click still counts.
    return PressWithoutCharge(sim, [&]() { return PressStationActionRaw(sim, action, arg); });
  }
  return CommitActionButton(sim, ButtonChargeKey(sim, "press:station:" + action + ":" + arg + ":"),
                            StationActionCategory(sim, action),
                            [&]() { return PressStationActionRaw(sim, action, arg); });
}

}  // namespace ship

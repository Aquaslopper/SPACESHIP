// ============================================================================
//  StationScreen.cpp — the port window: cards, items, inspector, commits.
//  ---------------------------------------------------------------------------
//  Five cards follow the owner's card table. The old mission board and bar
//  are gone; Star-map reads the station's own system page, and Trade contracts
//  opens exactly three kinds whose offers come from that local knowledge.
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

double GoodBuyPrice(const Simulation& sim, const std::string& portId, const Good& good) {
  GoodsLot probe;
  probe.value = good.value;
  probe.kind = good.kind;
  return js::Ceil(sim.LotPriceAt(portId, probe) * 1.2);
}

double GoodSellPrice(const Simulation& sim, const std::string& portId, const Good& good) {
  GoodsLot probe;
  probe.value = good.value;
  probe.kind = good.kind;
  return sim.LotPriceAt(portId, probe);
}

bool Owned(const Simulation& sim, const std::string& id) {
  const auto found = sim.State().upgrades.find(id);
  return found != sim.State().upgrades.end() && found->second;
}

bool Hired(const Simulation& sim, const std::string& id) {
  for (const CrewMember& member : sim.Crew()) { if (member.id == id) { return true; } }
  for (const Passenger& passenger : sim.Passengers()) { if (passenger.id == id) { return true; } }
  return false;
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
      "Pick a card, pick an item, then commit. Reading is free. Supplies, refits and hires cost shiplets only; selling and accepting trade cost one action.";

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
  market.notes.push_back(tip);
  market.notes.push_back("This shelf is stocked from " +
                         (port == nullptr ? std::string("the local system") : port->atlas.systemName) +
                         ". Prices follow this berth's supply and demand.");
  market.notes.push_back("Shiplets only: no action is spent and nothing is locked.");

  const auto supply = [&market, purse, docked = model.docked](const std::string& id, const std::string& label,
                                       const std::string& what, double price, bool extra,
                                       const std::vector<std::string>& extraDetails) {
    StationRow row;
    row.id = id;
    row.label = label;
    row.cost = kFree;
    row.free = true;
    row.commit = "Buy - " + Num(price) + " sh";
    row.enabled = docked && purse >= price && extra;
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

  for (const Good& good : sim.MarketAt(portId)) {
    const double price = GoodBuyPrice(sim, portId, good);
    const double sell = GoodSellPrice(sim, portId, good);
    const bool space = used + reserved + good.vol <= capacity;
    StationRow row;
    row.id = "buy-good:" + good.id;
    row.label = "Buy " + good.name + " - " + Num(price) + " sh / " + Num(good.vol) + " m3";
    row.cost = kFree;
    row.free = true;
    row.commit = "Buy - " + Num(price) + " sh";
    row.enabled = model.docked && purse >= price && space;
    row.details.push_back("Trade good: " + good.name + " (" + good.kind + ").");
    row.details.push_back("Volume: " + Num(good.vol) + " m3 per lot.");
    row.details.push_back("This port sells it in for " + Num(price) + " sh and would pay " +
                          Num(sell) + " sh for it.");
    if (port != nullptr) {
      for (const MarketSource& source : port->sources) {
        if (source.goodId == good.id) {
          const Body* body = sim.FindBodyById(source.bodyId);
          row.details.push_back("Local source: " + (body == nullptr ? source.bodyId : body->name) +
                                ". " + source.reading);
        }
      }
    }
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

  // ---- 3. the station's own star-map wiki --------------------------------
  StationColumn starmap;
  starmap.id = "starmap";
  starmap.title = "Star-map";
  starmap.badge = port == nullptr ? "system page" : port->atlas.systemName;
  starmap.notes.push_back("Every planet and moon written into this station's own system page. Pick a body to read it; opening the chart is free. Every system has a station, so every system has a page.");
  if (port != nullptr) {
    starmap.notes.push_back(port->atlas.systemReading);
    for (const PlanetPage& page : port->atlas.planets) {
      StationRow row;
      row.id = "planet:" + page.id;
      row.label = page.name;
      row.cost = "free";
      row.free = true;
      row.commit = "Open system chart";
      row.enabled = true;
      row.details.push_back("Kind: " + page.kind + ".");
      row.details.push_back("System: " + port->atlas.systemName + ". " + port->atlas.systemReading);
      row.details.push_back(page.reading);
      for (const auto& field : page.details) { row.details.push_back(field.first + ": " + field.second + "."); }
      for (const TradeContract& contract : sim.ActiveTradeContracts()) {
        if (contract.destination == page.id) {
          row.details.push_back("Green delivery route: " + contract.title + " (" +
                                TradeRouteLayer(sim, contract) + " chart).");
        }
      }
      starmap.rows.push_back(std::move(row));
    }
  }
  if (starmap.rows.empty()) { starmap.notes.push_back("No planetary pages have been written for this berth."); }

  // ---- 4. trade contracts: exactly three routes in the middle -------------
  StationColumn trade;
  trade.id = "trade";
  trade.title = "Trade contracts";
  trade.badge = Num(sim.ActiveTradeContracts().size()) + " active";
  trade.notes.push_back("Choose a kind. Offers rotate every 200 clicks; accepted work is kept aside and stays completable.");
  for (const char* kind : {"delivery", "freelance", "station"}) {
    StationRow row;
    row.id = kind;
    row.label = kind == std::string("delivery") ? "Delivery trade"
              : kind == std::string("freelance") ? "Freelance" : "Station-to-station trade";
    row.cost = "free";
    row.free = true;
    row.enabled = true;
    row.details.push_back(TradeKindLabel(kind));
    trade.rows.push_back(std::move(row));
  }

  // ---- 5. the shipyard and crew -------------------------------------------
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
    ship.enabled = model.docked && purse >= kLargeShip;
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
  const auto upgrade = [&yard, purse, docked = model.docked](const std::string& id, const std::string& label,
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
      row.enabled = docked && available && purse >= price;
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
    row.enabled = model.docked && hull < 100 && purse >= kHullService;
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
      row.enabled = model.docked && berth && purse >= candidate.fee;
      row.details.push_back("Shiplets only: no action, no cooldown.");
      if (!row.enabled) {
        row.details.push_back(berth ? "Not affordable from this purse."
                                    : "No berth free for another hand.");
      }
    }
    yard.rows.push_back(row);
  }

  model.columns = {market, sales, starmap, trade, yard};
  NormalizeStationSelection(model, model.selection);
  if (model.selection.category == "starmap") {
    const StationRow* page = FindStationItem(model, "starmap", model.selection.item);
    if (page != nullptr) { model.tradeCommit = *page; }
  }
  if (model.selection.category == "trade") {
    const std::string kind = model.selection.item;
    model.offers.id = "trade-offers";
    model.offers.title = TradeKindLabel(kind);
    model.offers.badge = "Rotation " + Num(sim.TradeEpoch()) + " - next in " +
                         Num(Simulation::kTradeRotationActions -
                             std::fmod(sim.ActionCount(), Simulation::kTradeRotationActions)) + " clicks";
    if (kind == "delivery") {
      model.offers.notes.push_back("Accept once: sealed freight is loaded, the clock advances +1, and the button greys. Carry it to the destination, then open the Cargo Hold and unload/cash in to finish. The green path marks the delivery point.");
    } else if (kind == "freelance") {
      model.offers.notes.push_back("Accept once (+1), then buy the requested lots at this station and carry them. At the destination, the Cargo Hold shows the sell options; selling the last requested lot finalizes the trade. The green path marks the destination.");
    } else {
      model.offers.notes.push_back("Every station visited so far, except this one. Picking a station activates a survey-data exchange and advances the clock +1. At the target station, finalize the exchange in the Cargo Hold to cash in. No separate data resource is managed. Its green path is on the sector chart.");
    }
    for (const TradeContract& active : sim.ActiveTradeContracts(kind)) {
      StationRow row;
      row.id = active.id;
      row.label = active.title + (active.status == "ready" ? " - ready to finalize" : " - accepted");
      row.cost = kDone;
      row.commit = active.status == "ready" ? "Finalize in Cargo Hold" : "Accepted";
      row.done = true;
      row.details.push_back(TradeDescription(sim, active));
      row.details.push_back("Accepted work survives every board rotation. Reward: " + Num(active.reward) + " sh. Finalize from the Cargo Hold at the destination.");
      if (active.kind == "freelance") {
        row.details.push_back("Sold " + Num(active.fulfilled) + " of " + Num(active.quantity) + " requested lots at the destination.");
      }
      model.offers.rows.push_back(std::move(row));
    }
    for (const TradeContract& offer : sim.TradeOffers(portId, kind)) {
      StationRow row;
      row.id = offer.id;
      row.label = offer.title + " - " + Num(offer.reward) + " sh";
      row.cost = kOneAction;
      row.commit = kind == "station" ? "Activate trade" : "Accept";
      row.enabled = model.docked;
      row.details.push_back(TradeDescription(sim, offer) + ".");
      row.details.push_back("Origin: " + model.portName + ". Destination: " +
                            (sim.FindBodyById(offer.destination) == nullptr ? offer.destination : sim.FindBodyById(offer.destination)->name) + ".");
      const Good* good = sim.World().FindGood(offer.goodId);
      if (good != nullptr) {
        row.details.push_back("Cargo: " + Num(offer.quantity) + " x " + good->name + ", " +
                              Num(good->vol * offer.quantity) + " m3.");
        const Body* source = sim.FindBodyById(offer.sourceBody);
        if (source != nullptr) { row.details.push_back("System request from " + source->name + ": " + source->klass + "."); }
        if (kind == "delivery") {
          row.enabled = row.enabled && used + reserved + good->vol * offer.quantity <= capacity;
          if (!row.enabled) { row.details.push_back("Not enough free hold space for the sealed freight."); }
        } else {
          const double buy = GoodBuyPrice(sim, portId, *good);
          const double sale = GoodSellPrice(sim, offer.destination, *good);
          row.details.push_back("Buy here: " + Num(buy) + " sh per lot. Sell there: " + Num(sale) +
                                " sh per lot. Buy the lots after acceptance; old stock does not count.");
          row.details.push_back("Bonus on the final requested sale: " + Num(offer.reward) + " sh, in addition to the sale price.");
        }
      }
      row.details.push_back("Reward: " + Num(offer.reward) + " sh. Acceptance costs one action; the green route is on the " + TradeRouteLayer(sim, offer) + " chart.");
      const TradeContract* accepted = sim.FindTradeContract(offer.id);
      if (accepted != nullptr) {
        row.enabled = false;
        row.done = true;
        row.cost = kDone;
        row.commit = accepted->status == "complete" ? "Traded" : "Accepted";
        row.details.push_back("This offer has already been taken; it cannot be accepted again.");
      }
      model.offers.rows.push_back(std::move(row));
    }
    if (model.offers.rows.empty()) {
      model.offers.notes.push_back(kind == "station" ? "Visit another station to open a data exchange." : "No request of this kind is available from this berth.");
    }
    NormalizeStationSelection(model, model.selection);
    for (const StationRow& row : model.offers.rows) {
      if (row.id == model.selection.offer && kind != "station") { model.tradeCommit = row; break; }
    }
  }
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
  if (selection.category == "trade") {
    if (selection.item != "delivery" && selection.item != "freelance" && selection.item != "station") {
      selection.item = "delivery";
    }
    bool found = false;
    for (const StationRow& row : model.offers.rows) {
      if (!row.reading && row.id == selection.offer) { found = true; break; }
    }
    if (!found) {
      selection.offer.clear();
      for (const StationRow& row : model.offers.rows) {
        if (!row.reading && !row.id.empty()) { selection.offer = row.id; break; }
      }
    }
  } else {
    selection.offer.clear();
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
  if (action.compare(0, 13, "trade-accept:") == 0) {
    return sim.AcceptTrade(action.substr(13));
  }
  if (action.compare(0, 13, "starmap-open:") == 0) {
    sim.SetLayer("system");
    return sim.SelectTarget(action.substr(13));
  }
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
  if (action.compare(0, 5, "talk:") == 0) return sim.TalkToPatron(action.substr(5));
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

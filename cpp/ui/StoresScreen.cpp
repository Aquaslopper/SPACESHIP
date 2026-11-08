// ============================================================================
//  StoresScreen.cpp — the Cargo Hold and the science room.
//  ---------------------------------------------------------------------------
//  The Cargo Hold is the trade operation's final desk: inventory on the left,
//  the hold's real volume in the middle, and the contextual unload/sell/finalize
//  window on the right. Arrival only enables finalization; the click here is
//  the commitment. The science room still files and packages survey records.
// ============================================================================
#include "ui/StoresScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"

#include <set>

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

std::string CargoDescription(const GoodsLot& lot) {
  if (lot.kind == "Salvage") { return "Scrap metal and components."; }
  if (lot.name.find("Ice") != std::string::npos) { return "Usable as coolant or refined."; }
  if (lot.kind.empty()) { return "Stored cargo."; }
  return lot.kind;
}

StoreRowView Reading(const std::string& label) {
  StoreRowView row;
  row.label = label;
  row.readingOnly = true;
  return row;
}

}  // namespace

StoresScreenModel BuildStoresScreen(const Simulation& sim, const std::string& roomId) {
  StoresScreenModel model;
  model.cargo = roomId == "cargo";

  model.head.kind = "ROOM";
  model.head.title = model.cargo ? "Cargo Hold" : "Science Room";
  model.head.sub = model.cargo ? "Freight, lots and the space they take"
                               : "Survey data, filed and packaged";

  if (model.cargo) {
    const double used = sim.CargoUsed();
    const double reserved = sim.CargoReserved();
    const double capacity = sim.CargoStore().capacity;
    model.notice = Num(used) + " of " + Num(capacity) + " m3 used; " + Num(reserved) +
                   " m3 reserved.";
    model.tally = Num(sim.Goods().size()) + " lots aboard.";
    model.hint =
        "Click an inventory lot to stow it in the hold view. Sorting is free until the "
        "hold asks for a paid tidy; stowed cargo can work loose again and need repeating. "
        "During a crossing, each bay can recover salvage: one action and 4% energy to "
        "start, two more actions of work, 3 m3 reserved.";

    model.visual.enabled = true;
    model.visual.capacity = capacity;
    model.visual.used = used;
    model.visual.reserved = reserved;
    model.visual.sortDue = sim.CargoSortDue();

    // ---- left: every lot, sorted only by hand -----------------------------
    if (sim.Goods().empty()) { model.rows.push_back(Reading("Nothing stored.")); }
    std::size_t stowedCount = 0;
    for (const GoodsLot& lot : sim.Goods()) {
      const bool stowed = sim.CargoLotStowed(lot);
      if (stowed) { ++stowedCount; }
      CargoVisualLot block;
      block.id = lot.id;
      block.label = lot.name;
      block.kind = lot.kind;
      block.vol = lot.vol;
      block.stowed = stowed;
      block.contract = !lot.contractId.empty();
      model.visual.lots.push_back(block);

      StoreRowView row;
      row.id = "cargo-sort:" + lot.id;
      row.card = true;
      row.label = lot.name;
      row.badge = Num(lot.vol) + " m³";
      row.description = CargoDescription(lot);
      if (!lot.contractId.empty()) { row.description += " - contract freight"; }
      if (lot.risk) { row.description += " - unregistered"; }
      row.qty = "QTY: 1";
      row.cost = stowed ? "stowed" : (sim.CargoSortDue() ? "sort - 1 action" : "sort - free");
      row.note = stowed ? "Stowed in the hold view." : "Click to stow this lot in the hold view.";
      row.enabled = !stowed;
      model.rows.push_back(row);
    }
    model.tally += " " + Num(stowedCount) + " stowed; " +
                   Num(sim.Goods().size() - stowedCount) + " loose.";

    // ---- right: the trade desk, or plain unloading ------------------------
    const Port* port = sim.World().FindPort(sim.Map().here);
    const bool docked = sim.State().docked && port != nullptr && port->id == sim.Map().here;
    bool tradeContext = false;
    for (const TradeContract& contract : sim.ActiveTradeContracts()) {
      if (!sim.TradeReadyHere(contract)) { continue; }
      tradeContext = true;
      StoreRowView row;
      row.id = "trade-finalize:" + contract.id;
      row.label = (contract.kind == "station" ? "Exchange data and cash in: "
                                              : "Unload delivery and cash in: ") +
                  contract.title + " (+" + Num(contract.reward) + " sh)";
      row.cost = "1 action";
      row.note = TradeDescription(sim, contract);
      row.enabled = true;
      model.tradeRows.push_back(row);
    }

    std::set<std::string> offered;
    for (const TradeContract& contract : sim.ActiveTradeContracts("freelance")) {
      if (!docked || contract.destination != sim.Map().here) { continue; }
      bool option = false;
      for (const GoodsLot& lot : sim.Goods()) {
        if (!sim.LotSatisfiesFreelance(lot, contract) || offered.count(lot.id) != 0) { continue; }
        offered.insert(lot.id);
        option = true;
        tradeContext = true;
        StoreRowView row;
        row.id = "cargo-sell:" + lot.id;
        row.label = "Sell " + lot.name + " - " + Num(sim.LotPrice(lot)) + " sh";
        row.cost = "1 action";
        row.note = contract.title + ": " + Num(contract.fulfilled) + "/" + Num(contract.quantity) +
                   " lots sold; final sale adds " + Num(contract.reward) + " sh.";
        row.enabled = true;
        model.tradeRows.push_back(row);
      }
      if (!option) {
        model.contextNotes.push_back("Freelance request here: " + TradeDescription(sim, contract) +
                                     ". Bring lots bought at " + contract.origin +
                                     " after acceptance; old stock does not count.");
      }
    }

    model.contextTitle = tradeContext ? "Delivery / sales window" : "Unload cargo";
    if (!tradeContext) {
      model.contextNotes.push_back(docked
          ? "No trade finalization at this berth. Sell cargo at the station, or unload unwanted lots into space."
          : "No trade finalization in flight. Unwanted cargo can be unloaded into space.");
    }
    if (!sim.Goods().empty()) {
      model.tradeRows.push_back(Reading("Unload cargo into space"));
      for (const GoodsLot& lot : sim.Goods()) {
        StoreRowView row;
        row.id = "jettison:" + lot.id;
        row.label = "Unload " + lot.name + " (" + Num(lot.vol) + " m3)";
        row.cost = "1 action";
        row.confirm = lot.contractId.empty()
                         ? "Unload " + lot.name + " into space?"
                         : "Unload " + lot.name + " and fail its contract?";
        if (!lot.contractId.empty()) { row.note = "Contract cargo: unloading it fails the contract."; }
        row.enabled = true;
        model.tradeRows.push_back(row);
      }
    } else {
      model.tradeRows.push_back(Reading("Nothing to unload."));
    }

    // Salvage bays remain commitments in the action band, not inventory rows.
    const Task* trip = sim.FindOngoing("travel");
    const bool crossing = trip != nullptr && trip->state == "running";
    for (int bay = 0; bay < 3; ++bay) {
      const std::string id = "salvage-" + Num(bay);
      const bool enabled = crossing && sim.StatValue("energy") > 4 &&
                           used + reserved + 3 <= capacity && sim.FindOngoing(id) == nullptr;
      model.buttons.push_back(BuildProgressButton(
          sim, id, "Recover route salvage - bay " + Num(bay + 1), enabled));
    }
  } else {
    model.notice =
        "Deep scans are filed here. Packaging costs one action, takes two more, and "
        "adds value.";
    model.tally = Num(sim.Survey().size()) + " records filed.";
    if (sim.Survey().empty()) { model.rows.push_back(Reading("Nothing stored.")); }
    for (const SurveyRecord& record : sim.Survey()) {
      StoreRowView row;
      row.id = "package:" + record.id;
      row.packaged = record.state == "packaged";
      row.cost = row.packaged ? "done" : "1 action + 2 work";
      row.label = row.packaged
                      ? "Packaged: " + record.name + " (" + Num(record.value) + " sh)"
                      : "Package: " + record.name + " (1 action + 2 work)";
      row.enabled = !row.packaged;
      row.progress = true;
      model.rows.push_back(row);
      model.buttons.push_back(
          BuildProgressButton(sim, "pkg-" + record.id, row.label, row.enabled));
    }
  }
  return model;
}

namespace {
bool PressStoresActionRaw(Simulation& sim, const std::string& action,
                          const std::string& arg) {
  if (action.rfind("cargo-sort:", 0) == 0) { return sim.SortCargo(action.substr(11)); }
  if (action.rfind("trade-finalize:", 0) == 0) { return sim.FinalizeTrade(action.substr(15)); }
  if (action.rfind("cargo-sell:", 0) == 0) { return sim.SellGood(action.substr(11)); }
  if (action.rfind("jettison:", 0) == 0) { return sim.Jettison(action.substr(9)); }
  if (action.rfind("salvage:", 0) == 0) { return sim.StartSalvage(std::stoi(action.substr(8))); }
  if (action.rfind("package:", 0) == 0) { return sim.PackageSurvey(action.substr(8)); }
  (void)arg;
  return false;
}
}  // namespace

// ============================================================================
//  The press, with the anti-spam lock around it (Phase 9).
// ============================================================================
bool PressStoresAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:stores:" + action + ":" + arg),
      ActionCooldownCategory(action + " " + arg),
      [&]() { return PressStoresActionRaw(sim, action, arg); });
}

}  // namespace ship

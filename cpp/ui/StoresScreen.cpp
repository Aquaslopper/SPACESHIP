// ============================================================================
//  StoresScreen.cpp — cargo manifest and survey records.
// ============================================================================
#include "ui/StoresScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"

namespace ship {

// ---------------------------------------------------------------------------
//  The cargo hold: three windows
// ---------------------------------------------------------------------------
//  Left, everything aboard. Middle, the hold itself, filled one lot at a time by
//  clicking it. Right, the one thing that can be done where the ship is standing:
//  settle the run that ends here, or put cargo out of the lock.
CargoRoomModel BuildCargoRoom(const Simulation& sim) {
  CargoRoomModel model;
  model.head.kind = "ROOM";
  model.head.title = "Cargo Hold";
  model.head.sub = "Freight, lots and the space they take";

  model.used = sim.CargoUsed();
  model.capacity = sim.CargoStore().capacity;
  const double percent =
      model.capacity > 0 ? js::Round(model.used / model.capacity * 100.0) : 0.0;
  model.capacityLine = js::NumToString(model.used) + " / " + js::NumToString(model.capacity) +
                       " m3 (" + js::NumToString(percent) + "%)";
  model.statusLine = "Hold Status: Secure";
  model.statusNote = "Environment stable and systems nominal.";

  // ---- left: the manifest ------------------------------------------------
  model.inventoryNote = js::NumToString(static_cast<double>(sim.Goods().size())) + " lots aboard.";
  for (const GoodsLot& lot : sim.Goods()) {
    CargoLotView view;
    view.id = lot.id;
    view.name = lot.name;
    view.note = lot.kind;
    view.vol = lot.vol;
    view.stowed = sim.CargoStowed(lot.id);
    view.contract = !lot.contractId.empty();
    model.lots.push_back(std::move(view));

    if (!view.stowed) { continue; }
    CargoHoldLot block;
    block.id = lot.id;
    block.label = lot.name;
    block.vol = lot.vol;
    block.contract = !lot.contractId.empty();
    model.hold.push_back(std::move(block));
  }

  // ---- right: the window that follows the run -----------------------------
  const std::vector<TradeContract> here = sim.TradesHere();
  if (!here.empty()) {
    const TradeContract& trade = here.front();
    model.sideTitle = trade.mode == "delivery"    ? "Delivery"
                      : trade.mode == "freelance" ? "Freelance"
                                                  : "Survey run";
    const Body* destination = sim.FindBodyById(trade.to);
    const std::string where = destination == nullptr ? trade.to : destination->name;
    const double pays = sim.TradePayout(trade);
    model.sideNote = trade.mode == "freelance"
                         ? "The market at " + where + " plus the freight it took to get it there."
                         : "The run ends here. " + where + " settles it over the counter.";
    if (trade.mode == "station") {
      model.sideNote = "The survey log changes hands at " + where + ".";
    }

    CargoSideRow row;
    row.id = "cargo-settle:" + trade.mode + ":" + trade.to + ":" + trade.good;
    row.label = trade.mode == "delivery"    ? "Unload the consignment and cash in"
                : trade.mode == "freelance" ? "Sell the lot and cash in"
                                            : "Hand over the survey log and cash in";
    if (trade.mode == "freelance") {
      row.note = js::NumToString(pays) + " sh, of which " + js::NumToString(trade.freight) +
                 " sh is the freight.";
    } else {
      row.note = js::NumToString(pays) + " sh on the counter.";
    }
    model.sideRows.push_back(std::move(row));
    if (here.size() > 1) {
      CargoSideRow more;
      more.label = js::NumToString(static_cast<double>(here.size() - 1)) + " more run(s) end here.";
      more.enabled = false;
      model.sideRows.push_back(std::move(more));
    }
  } else {
    model.sideTitle = "Unload cargo";
    model.sideNote = "Nothing is settled at this position. Anything not needed can go out of the "
                     "lock.";
    for (const GoodsLot& lot : sim.Goods()) {
      CargoSideRow row;
      row.id = "cargo-unload:" + lot.id;
      row.label = "Out of the lock - " + lot.name;
      row.note = js::NumToString(lot.vol) + " m3" + (lot.contractId.empty() ? "" : "; carried for a run");
      model.sideRows.push_back(std::move(row));
    }
    if (model.sideRows.empty()) {
      CargoSideRow row;
      row.label = "The hold is empty.";
      row.enabled = false;
      model.sideRows.push_back(std::move(row));
    }
  }

  // ---- the salvage bays stay reachable while under way --------------------
  // A start, a wait and a pick-up, each one a click on the same window.
  const Task* trip = sim.FindOngoing("travel");
  if (trip != nullptr && trip->state == "running") {
    for (int bay = 0; bay < 3; ++bay) {
      const std::string taskId = "salvage-" + js::NumToString(bay);
      const Task* task = sim.FindOngoing(taskId);
      CargoSideRow row;
      if (task == nullptr) {
        row.id = "salvage:" + js::NumToString(bay);
        row.label = "Recover route salvage - bay " + js::NumToString(bay + 1);
        row.note = "Four percent of the energy, and three cubic metres of the hold.";
        row.enabled = sim.StatValue("energy") > 4 &&
                      sim.CargoUsed() + sim.CargoReserved() + 3 <= sim.CargoStore().capacity;
      } else if (task->state == "done") {
        row.id = "cargo-take:" + taskId;
        row.label = "Take the salvage aboard - bay " + js::NumToString(bay + 1);
        row.note = "Three cubic metres in the hold.";
      } else {
        row.label = "Recovering salvage - bay " + js::NumToString(bay + 1) + " (" +
                    js::NumToString(OngoingPercent(*task)) + "%)";
        row.enabled = false;
      }
      model.salvageRows.push_back(std::move(row));
    }
  }
  return model;
}

StoresScreenModel BuildStoresScreen(const Simulation& sim, const std::string& roomId) {
  (void)roomId;
  StoresScreenModel model;
  model.cargo = false;
  model.head.kind = "ROOM";
  model.head.title = "Science Room";
  model.head.sub = "Survey data, filed and packaged";
  {
    model.notice =
        "Deep scans are filed here. Packaging costs one action, takes two more, and "
        "adds value.";
    model.tally = js::NumToString(sim.Survey().size()) + " records filed.";
    if (sim.Survey().empty()) {
      StoreRowView row;
      row.label = "Nothing stored.";
      row.readingOnly = true;
      model.rows.push_back(row);
    }
    for (const SurveyRecord& record : sim.Survey()) {
      StoreRowView row;
      row.id = "package:" + record.id;
      row.packaged = record.state == "packaged";
      row.cost = row.packaged ? "done" : "1 action + 2 work";
      row.label = row.packaged
                      ? "Packaged: " + record.name + " (" + js::NumToString(record.value) + " sh)"
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
bool PressCargoRoomRaw(Simulation& sim, const std::string& action, const std::string& arg) {
  if (action.compare(0, 11, "cargo-stow:") == 0) return sim.StowCargo(action.substr(11));
  if (action.compare(0, 13, "cargo-unload:") == 0) return sim.Jettison(action.substr(13));
  if (action.compare(0, 13, "cargo-settle:") == 0) {
    // mode : destination [: commodity]
    const std::string rest = action.substr(13);
    const std::size_t cut = rest.find(':');
    if (cut == std::string::npos) { return false; }
    const std::string mode = rest.substr(0, cut);
    const std::string tail = rest.substr(cut + 1);
    const std::size_t second = tail.find(':');
    if (second == std::string::npos) { return sim.SettleTrade(mode, tail, std::string()); }
    return sim.SettleTrade(mode, tail.substr(0, second), tail.substr(second + 1));
  }
  if (action.compare(0, 8, "salvage:") == 0) {
    return sim.StartSalvage(std::stoi(action.substr(8)));
  }
  if (action.compare(0, 11, "cargo-take:") == 0) {
    // The work is done; the click only takes it aboard.
    const std::string taskId = action.substr(11);
    const Task* task = sim.FindOngoing(taskId);
    if (task == nullptr || task->state != "done") { return false; }
    return sim.Acknowledge(taskId);
  }
  (void)arg;
  return false;
}

bool PressStoresActionRaw(Simulation& sim, const std::string& action,
                       const std::string& arg) {
  if (action.compare(0, 9, "jettison:") == 0) return sim.Jettison(action.substr(9));
  if (action.compare(0, 8, "salvage:") == 0) {
    return sim.StartSalvage(std::stoi(action.substr(8)));
  }
  if (action.compare(0, 8, "package:") == 0) {
    return sim.PackageSurvey(action.substr(8));
  }
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
bool PressStoresAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:stores:" + action + ":" + arg),
      ActionCooldownCategory(action + " " + arg),
      [&]() { return PressStoresActionRaw(sim, action, arg); });
}

// The cargo hall's presses go through the same lock, so a settle or a stow can
// never resolve twice before the clock has moved.
bool PressCargoRoom(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:cargo:" + action + ":" + arg),
      ActionCooldownCategory(action + " " + arg),
      [&]() { return PressCargoRoomRaw(sim, action, arg); });
}

}  // namespace ship

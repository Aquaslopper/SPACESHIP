// ============================================================================
//  StoresScreen.cpp — cargo manifest and survey records.
// ============================================================================
#include "ui/StoresScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/JsNumber.h"
#include "ui/Widgets.h"

namespace ship {

namespace {

std::uint32_t MixHash(const std::string& text, double salt) {
  std::uint32_t h = 2166136261u;
  for (unsigned char c : text) {
    h ^= c;
    h *= 16777619u;
  }
  const std::uint32_t s = static_cast<std::uint32_t>(salt);
  h ^= s * 2654435761u;
  h ^= h >> 16;
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  return h;
}

std::string GoodDisplayName(const Simulation& sim, const std::string& goodId) {
  if (goodId.empty()) { return "Survey log"; }
  for (const Good& g : sim.World().goods) {
    if (g.id == goodId) { return g.name; }
  }
  return goodId;
}

double GoodUnitVolume(const Simulation& sim, const std::string& goodId) {
  for (const Good& g : sim.World().goods) {
    if (g.id == goodId) { return static_cast<double>(g.vol); }
  }
  return 0.0;
}

double AboardLooseVolume(const Simulation& sim, const std::string& goodName) {
  double total = 0.0;
  for (const GoodsLot& lot : sim.Goods()) {
    if (lot.name == goodName && lot.contractId.empty()) { total += lot.vol; }
  }
  return total;
}

int EstimateLotQty(const Simulation& sim, const GoodsLot& lot) {
  if (lot.contractId.compare(0, 6, "trade:") == 0) {
    const std::string key = lot.contractId.substr(6);
    for (const TradeContract& trade : sim.State().trades) {
      if (TradeKey(trade.mode, trade.to, trade.good) == key && trade.crates > 0) {
        return trade.crates;
      }
    }
  }
  for (const Good& g : sim.World().goods) {
    if (g.name == lot.name && g.vol > 0) {
      const double ratio = lot.vol / static_cast<double>(g.vol);
      const int rounded = static_cast<int>(std::round(ratio));
      if (rounded >= 1 && std::abs(ratio - static_cast<double>(rounded)) < 0.05) {
        return rounded;
      }
    }
  }
  return 1;
}

}  // namespace

void NormalizeCargoSelection(const Simulation& sim, CargoSelection& selection) {
  if (!selection.lotId.empty()) {
    const bool validLot = std::any_of(sim.Goods().begin(), sim.Goods().end(),
                                      [&](const GoodsLot& lot) {
                                        return lot.id == selection.lotId &&
                                               !sim.CargoStowed(lot.id);
                                      });
    if (!validLot) { selection.lotId.clear(); }
  }

  const std::vector<TradeContract> here = sim.TradesHere();
  if (here.empty()) {
    selection.tradeKey.clear();
    return;
  }
  const bool validTrade = std::any_of(here.begin(), here.end(),
                                      [&](const TradeContract& trade) {
                                        return TradeKey(trade.mode, trade.to, trade.good) ==
                                               selection.tradeKey;
                                      });
  if (!validTrade) {
    selection.tradeKey = TradeKey(here.front().mode, here.front().to, here.front().good);
  }
}

// ---------------------------------------------------------------------------
//  The cargo hold: three windows
// ---------------------------------------------------------------------------
//  Left, cargo lot cards. Middle, interactive visual hold scaled to ship
//  capacity (click left -> glows green in hold -> click green crate to sort).
//  Right, context-sensitive finalizer (delivery / freelance / station /
//  jettison).
CargoRoomModel BuildCargoRoom(const Simulation& sim, CargoSelection* selection) {
  CargoSelection localSelection;
  CargoSelection& sel = selection != nullptr ? *selection : localSelection;
  NormalizeCargoSelection(sim, sel);

  CargoRoomModel model;
  model.head.kind = "ROOM";
  model.head.title = "Cargo Hold";
  model.head.sub = "Freight, lots and the space they take";
  model.shipLabel = sim.State().shipClass == "kestrel" ? "SC Tessellate" : "SC Wren";
  model.selectedLotId = sel.lotId;

  model.used = sim.CargoUsed();
  model.capacity = sim.CargoStore().capacity;
  const double percent =
      model.capacity > 0 ? js::Round(model.used / model.capacity * 100.0) : 0.0;
  model.capacityLine = js::NumToString(model.used) + " / " + js::NumToString(model.capacity) +
                       " m3 (" + js::NumToString(percent) + "%)";

  int unsortedCount = 0;
  for (const GoodsLot& lot : sim.Goods()) {
    if (!sim.CargoStowed(lot.id)) { ++unsortedCount; }
  }
  if (sim.Goods().empty()) {
    model.statusLine = "Hold Status: Empty";
    model.statusNote = "No freight aboard.";
  } else if (unsortedCount == 0) {
    model.statusLine = "Hold Status: Neatly Sorted";
    model.statusNote = "All crates secured. Crates shift gradually after 5-10 clicks.";
  } else {
    model.statusLine = "Hold Status: " + js::NumToString(static_cast<double>(unsortedCount)) +
                       " Unsorted";
    model.statusNote =
        sel.lotId.empty()
            ? "Select an item on the left to highlight it green in the hold."
            : "Click the glowing green crate in the hold to sort it (+1 click).";
  }

  // ---- left: inventory cards + middle: visual hold crates ----------------
  model.inventoryNote = js::NumToString(static_cast<double>(sim.Goods().size())) + " lots aboard.";
  for (const GoodsLot& lot : sim.Goods()) {
    const bool stowed = sim.CargoStowed(lot.id);
    const bool selected = !stowed && sel.lotId == lot.id;

    CargoLotView view;
    view.id = lot.id;
    view.name = lot.name;
    view.note = lot.kind.empty() ? "Freight" : lot.kind;
    view.vol = lot.vol;
    view.qty = EstimateLotQty(sim, lot);
    view.stowed = stowed;
    view.selected = selected;
    view.contract = !lot.contractId.empty();
    view.risk = lot.risk;
    if (lot.contractId.compare(0, 6, "trade:") == 0) {
      view.contractKey = lot.contractId.substr(6);
      for (const TradeContract& trade : sim.State().trades) {
        if (TradeKey(trade.mode, trade.to, trade.good) == view.contractKey) {
          const Body* dest = sim.FindBodyById(trade.to);
          view.detail = "Contract -> " + (dest == nullptr ? trade.to : dest->name);
          break;
        }
      }
    } else if (view.contract) {
      view.detail = "Contract freight";
    } else if (lot.risk) {
      view.detail = "Unregistered manifest";
    }
    model.lots.push_back(std::move(view));

    CargoHoldLot block;
    block.id = lot.id;
    block.label = lot.name;
    block.kind = lot.kind;
    block.vol = lot.vol;
    block.stowed = stowed;
    block.selected = selected;
    block.contract = !lot.contractId.empty();
    block.risk = lot.risk;
    if (!stowed) {
      const std::uint32_t h = MixHash(lot.id, sim.ActionCount() / 5.0);
      block.jitterX = static_cast<float>(static_cast<int>(h & 0xFFu) - 128) / 128.0f;
      block.jitterY = static_cast<float>(static_cast<int>((h >> 8) & 0xFFu) - 128) / 128.0f;
      block.tiltDeg = static_cast<float>(static_cast<int>((h >> 16) % 15u) - 7);
    }
    model.hold.push_back(std::move(block));
  }

  // ---- right: context-sensitive finalizing window ------------------------
  const std::vector<TradeContract> here = sim.TradesHere();
  std::vector<TradeContract> dockNeeded;
  if (here.empty() && !sim.State().docked) {
    for (const TradeContract& trade : sim.State().trades) {
      if (trade.to == sim.Map().here && trade.mode == "station") {
        dockNeeded.push_back(trade);
      }
    }
  }

  if (!here.empty()) {
    if (here.size() > 1) {
      for (std::size_t i = 0; i < here.size(); ++i) {
        const TradeContract& t = here[i];
        const std::string key = TradeKey(t.mode, t.to, t.good);
        CargoRunTab tab;
        tab.key = key;
        tab.mode = t.mode;
        tab.label = (t.mode == "delivery"    ? "Delivery: "
                     : t.mode == "freelance" ? "Freelance: "
                                             : "Station: ") +
                    GoodDisplayName(sim, t.good);
        tab.selected = (key == sel.tradeKey);
        model.runTabs.push_back(std::move(tab));
      }
    }

    const TradeContract* active = &here.front();
    for (const TradeContract& trade : here) {
      if (TradeKey(trade.mode, trade.to, trade.good) == sel.tradeKey) {
        active = &trade;
        break;
      }
    }
    const TradeContract& trade = *active;
    model.contextKind = trade.mode;
    model.activeTradeKey = TradeKey(trade.mode, trade.to, trade.good);

    const Body* destination = sim.FindBodyById(trade.to);
    const std::string where = destination == nullptr ? trade.to : destination->name;
    const std::string commodityName = GoodDisplayName(sim, trade.good);

    if (trade.mode == "delivery") {
      const double pays = sim.TradePayout(trade);
      const std::string key = "trade:" + model.activeTradeKey;
      const bool carried = std::any_of(sim.Goods().begin(), sim.Goods().end(),
                                       [&](const GoodsLot& lot) { return lot.contractId == key; });
      model.sideTitle = "Delivery";
      model.sideNote = carried
                           ? ("Arrival confirmed at " + where +
                              ". Hand over the consignment to finalize delivery.")
                           : ("Consignment missing from hold for " + where + ".");
      model.contextDetails.push_back("Destination: " + where);
      model.contextDetails.push_back("Cargo: " + js::NumToString(trade.crates) + " crates of " +
                                     commodityName);
      model.contextDetails.push_back("Payout: " + js::NumToString(pays) + " sh");

      CargoSideRow row;
      row.id = "cargo-settle:delivery:" + trade.to + ":" + trade.good;
      row.label = "Finalize delivery";
      row.note = js::NumToString(pays) + " sh";
      row.enabled = carried;
      model.sideRows.push_back(std::move(row));
    } else if (trade.mode == "freelance") {
      const double base = sim.TradeOfferPayout(trade, 0);
      const double plus10 = sim.TradeOfferPayout(trade, 1);
      const double plus20 = sim.TradeOfferPayout(trade, 2);
      const bool hasGoods = sim.HasFreelanceGoods(trade);
      const int rejectMask = sim.FreelanceRejectMask(trade);
      const double unitVol = GoodUnitVolume(sim, trade.good);
      const double neededVol = unitVol * trade.crates;
      const double aboardVol = AboardLooseVolume(sim, commodityName);

      model.sideTitle = "Freelance";
      model.sideNote =
          hasGoods
              ? ("Buyers active at " + where +
                 ". Take market price or ask +10% / +20% (RNG decides).")
              : ("Bring " + js::NumToString(neededVol) + " m3 of " + commodityName +
                 " aboard to sell at " + where + ".");
      model.contextDetails.push_back("Market: " + where);
      model.contextDetails.push_back("Freight: " + js::NumToString(trade.crates) + " crates of " +
                                     commodityName);
      model.contextDetails.push_back("Aboard: " + js::NumToString(aboardVol) + " / " +
                                     js::NumToString(neededVol) + " m3");
      model.contextDetails.push_back("Base + freight: " + js::NumToString(base) + " sh (incl. " +
                                     js::NumToString(trade.freight) + " sh)");

      {
        CargoSideRow actual;
        actual.id = "cargo-settle:freelance:" + trade.to + ":" + trade.good + ":0";
        actual.label = "Sell at market price";
        actual.note = js::NumToString(base) + " sh - Guaranteed";
        actual.enabled = hasGoods;
        model.sideRows.push_back(std::move(actual));
      }
      {
        const bool rejected = (rejectMask & 1) != 0;
        CargoSideRow opt10;
        opt10.id = "cargo-settle:freelance:" + trade.to + ":" + trade.good + ":1";
        opt10.label = rejected ? "Negotiate +10% (Declined)" : "Negotiate +10%";
        opt10.note = js::NumToString(plus10) +
                     (rejected ? " sh - Buyer refused" : " sh - RNG chance");
        opt10.enabled = hasGoods && !rejected;
        model.sideRows.push_back(std::move(opt10));
      }
      {
        const bool rejected = (rejectMask & 2) != 0;
        CargoSideRow opt20;
        opt20.id = "cargo-settle:freelance:" + trade.to + ":" + trade.good + ":2";
        opt20.label = rejected ? "Demand +20% (Declined)" : "Demand +20%";
        opt20.note = js::NumToString(plus20) +
                     (rejected ? " sh - Buyer refused" : " sh - Risky RNG");
        opt20.enabled = hasGoods && !rejected;
        model.sideRows.push_back(std::move(opt20));
      }
    } else {
      const double pays = sim.TradePayout(trade);
      model.sideTitle = "Station to Station";
      model.sideNote = "Docked at " + where + ". Finalize the exchange to close the deal.";
      model.contextDetails.push_back("Route: " + trade.label);
      model.contextDetails.push_back("Station: " + where);
      model.contextDetails.push_back("Payout: " + js::NumToString(pays) + " sh");

      CargoSideRow row;
      row.id = "cargo-settle:station:" + trade.to + ":" + trade.good;
      row.label = "Finalize deal";
      row.note = js::NumToString(pays) + " sh";
      row.enabled = true;
      model.sideRows.push_back(std::move(row));
    }
  } else if (!dockNeeded.empty()) {
    const TradeContract& trade = dockNeeded.front();
    const Body* destination = sim.FindBodyById(trade.to);
    const std::string where = destination == nullptr ? trade.to : destination->name;
    model.contextKind = "station";
    model.sideTitle = "Station to Station";
    model.sideNote = "In orbit over " + where + ". Dock to finalize this deal.";
    model.contextDetails.push_back("Route: " + trade.label);
    model.contextDetails.push_back("Payout: " + js::NumToString(sim.TradePayout(trade)) + " sh");
    CargoSideRow row;
    row.label = "Dock to finalize deal";
    row.note = js::NumToString(sim.TradePayout(trade)) + " sh";
    row.enabled = false;
    model.sideRows.push_back(std::move(row));
  } else {
    model.contextKind = "jettison";
    model.sideTitle = "Jettison Cargo";
    model.sideNote = "Nothing settles here. Eject unwanted cargo into space.";
    for (const TradeContract& trade : sim.State().trades) {
      const Body* dest = sim.FindBodyById(trade.to);
      const std::string where = dest == nullptr ? trade.to : dest->name;
      const std::string tag = trade.mode == "delivery"    ? "Delivery"
                              : trade.mode == "freelance" ? "Freelance"
                                                          : "Station";
      model.contextDetails.push_back("Active " + tag + " -> " + where);
    }
    for (const GoodsLot& lot : sim.Goods()) {
      const bool contract = !lot.contractId.empty();
      CargoSideRow row;
      row.id = "cargo-unload:" + lot.id;
      row.label = "Jettison " + lot.name;
      row.note = js::NumToString(lot.vol) + " m3" + (contract ? " - fails active contract" : "");
      row.confirm = "Jettison " + lot.name + (contract ? " and fail its contract?" : "?");
      row.enabled = true;
      model.sideRows.push_back(std::move(row));
    }
    if (model.sideRows.empty()) {
      CargoSideRow row;
      row.label = "The hold is empty.";
      row.enabled = false;
      model.sideRows.push_back(std::move(row));
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
        "Deep scans are filed here. Packaging costs one click, takes two more, and "
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
      row.cost = row.packaged ? "done" : "1 click + 2 work";
      row.label = row.packaged
                      ? "Packaged: " + record.name + " (" + js::NumToString(record.value) + " sh)"
                      : "Package: " + record.name + " (1 click + 2 work)";
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
  if (action.compare(0, 13, "cargo-unload:") == 0) return sim.Jettison(action.substr(13));
  if (action.compare(0, 13, "cargo-settle:") == 0) {
    // mode : destination [: commodity [: priceTier]]
    const std::string rest = action.substr(13);
    const std::size_t cut = rest.find(':');
    if (cut == std::string::npos) { return false; }
    const std::string mode = rest.substr(0, cut);
    const std::string tail = rest.substr(cut + 1);
    const std::size_t second = tail.find(':');
    if (second == std::string::npos) { return sim.SettleTrade(mode, tail, std::string()); }
    const std::string to = tail.substr(0, second);
    const std::string rem = tail.substr(second + 1);
    const std::size_t third = rem.find(':');
    if (third == std::string::npos) { return sim.SettleTrade(mode, to, rem); }
    const std::string good = rem.substr(0, third);
    const std::string tierStr = rem.substr(third + 1);
    int tier = 0;
    if (!tierStr.empty() && tierStr.size() == 1 && tierStr[0] >= '0' && tierStr[0] <= '2') {
      tier = tierStr[0] - '0';
    } else {
      return false;
    }
    return sim.SettleTrade(mode, to, good, tier);
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
//  The press, and the clicks it costs.
//  ---------------------------------------------------------------------------
//  One control is one control. A press spends one of the clicks that control
//  holds and returns one to every other control, so a button is only refused
//  once its own green has run out. Free reads spend nothing and return nothing.
// ============================================================================
bool PressStoresAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ButtonChargeKey(sim, "press:stores:" + action + ":" + arg),
      ButtonChargeTier(action + " " + arg),
      [&]() { return PressStoresActionRaw(sim, action, arg); });
}

bool PressCargoRoom(Simulation& sim, const std::string& action, const std::string& arg) {
  if (action.compare(0, 11, "cargo-stow:") == 0) {
    const std::string lotId = action.substr(11);
    return PressWithoutCharge(sim, [&]() { return sim.StowCargo(lotId); });
  }
  const std::string tierSubject =
      action.compare(0, 13, "cargo-unload:") == 0 ? ("jettison " + action) : ("sell " + action);
  return CommitActionButton(
      sim, ButtonChargeKey(sim, "press:cargo:" + action + ":" + arg),
      ButtonChargeTier(tierSubject),
      [&]() { return PressCargoRoomRaw(sim, action, arg); });
}

}  // namespace ship

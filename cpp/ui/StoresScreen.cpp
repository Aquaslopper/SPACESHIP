// ============================================================================
//  StoresScreen.cpp — cargo manifest and survey records.
// ============================================================================
#include "ui/StoresScreen.h"

#include "core/JsNumber.h"

namespace ship {

StoresScreenModel BuildStoresScreen(const Simulation& sim, const std::string& roomId) {
  StoresScreenModel model;
  model.cargo = roomId == "cargo";

  model.head.kind = "ROOM";
  model.head.title = model.cargo ? "Cargo Hold" : "Science Room";
  model.head.sub = model.cargo ? "Freight, lots and the space they take"
                               : "Survey data, filed and packaged";

  if (model.cargo) {
    model.notice = js::NumToString(sim.CargoUsed()) + " of " +
                   js::NumToString(sim.CargoStore().capacity) + " m3 used; " +
                   js::NumToString(sim.CargoReserved()) +
                   " m3 reserved. Sell lots at a station.";
    model.tally = js::NumToString(sim.Goods().size()) + " lots aboard.";
    model.hint =
        "During a crossing, each bay can recover a small salvage lot. Starting costs one "
        "action and 4% energy; recovery takes two more actions and reserves 3 m3.";

    // Each bay can recover a small salvage lot DURING a crossing.
    const Task* trip = sim.FindOngoing("travel");
    const bool crossing = trip != nullptr && trip->state == "running";
    for (int bay = 0; bay < 3; ++bay) {
      StoreRowView row;
      row.id = "salvage:" + js::NumToString(bay);
      row.label = "Recover route salvage - bay " + js::NumToString(bay + 1);
      row.cost = "1 action + 2 work";
      row.note =
          "Starting costs one action and 4% energy; recovery takes two more actions "
          "and reserves 3 m3.";
      row.enabled = crossing && sim.StatValue("energy") > 4 &&
                    sim.CargoUsed() + sim.CargoReserved() + 3 <= sim.CargoStore().capacity &&
                    sim.FindOngoing("salvage-" + js::NumToString(bay)) == nullptr;
      row.progress = true;
      model.rows.push_back(row);
      model.buttons.push_back(BuildProgressButton(
          sim, "salvage-" + js::NumToString(bay), row.label, row.enabled));
    }

    if (sim.Goods().empty()) {
      StoreRowView row;
      row.label = "Nothing stored.";
      row.readingOnly = true;
      model.rows.push_back(row);
    }
    for (const GoodsLot& lot : sim.Goods()) {
      StoreRowView row;
      row.id = "jettison:" + lot.id;
      row.label = "Jettison " + lot.name + " (" + js::NumToString(lot.vol) + " m3)";
      row.cost = "1 action";
      row.confirm = lot.contractId.empty()
                       ? "Jettison " + lot.name + "?"
                       : "Jettison " + lot.name + " and fail its contract?";
      if (!lot.contractId.empty()) {
        row.note = "Contract cargo: jettisoning fails the contract.";
      }
      row.enabled = true;
      model.rows.push_back(row);
    }
  } else {
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

bool PressStoresAction(Simulation& sim, const std::string& action,
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

}  // namespace ship

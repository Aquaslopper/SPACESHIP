// ============================================================================
//  Economy.cpp — ports, cargo, the yard and the station bar.
//  ---------------------------------------------------------------------------
//  Every transaction is one action, and every transaction is one function that
//  the client calls after the player has already committed: the money moves, the
//  action is spent, and the log says what happened — in that order, exactly as
//  commitChange() does it in the reference.
//
//  Prices are a multiplier per commodity KIND, per port. A lot's worth is its
//  authored value; what a port pays for it is the port's opinion.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ship {

namespace {
bool Truthy(const std::map<std::string, bool>& map, const std::string& key) {
  auto it = map.find(key);
  return it != map.end() && it->second;
}
}  // namespace

const Port* Simulation::HerePort() const { return catalog_.FindPort(map_.here); }

double Simulation::SurveyWorth(const std::string& type) const {
  auto it = catalog_.surveyWorth.find(type);
  return it == catalog_.surveyWorth.end() ? 60.0 : it->second;
}

double Simulation::LotPrice(const GoodsLot& lot) const { return LotPriceAt(map_.here, lot); }

double Simulation::LotPriceAt(const std::string& portId, const GoodsLot& lot) const {
  const Port* port = catalog_.FindPort(portId);
  double factor = 1.0;
  if (port != nullptr) {
    auto it = port->prices.find(lot.kind);
    if (it != port->prices.end()) { factor = it->second; }
  }
  return js::Round(lot.value * factor);
}

double Simulation::CargoUsed() const {
  double used = 0;
  for (const GoodsLot& lot : goods_) { used += lot.vol; }
  return used;
}

double Simulation::CargoReserved() const {
  // Salvage and surface extraction in flight have already been promised the space they will take.
  double reserved = 0;
  for (const Task& task : state_.ongoing) {
    if (task.type == "salvage" && task.state == "running") { reserved += task.vol; }
    if (task.type == "extract" && (task.state == "running" || task.state == "done")) {
      reserved += task.vol;
    }
  }
  return reserved;
}

// ---------------------------------------------------------------------------
// The market counter
// ---------------------------------------------------------------------------

bool Simulation::BuySupply(const std::string& id) {
  struct Supply { const char* id; double price; double* counter; };
  // Five fixed-price stores: fuel cell, repair kit, medkit, O2 canister, rations.
  if (state_.gameOver.empty() == false || !state_.docked || HerePort() == nullptr) { return false; }

  if (id == "fuel-cell" && state_.shiplets >= 45) {
    state_.shiplets -= 45;
    state_.fuelCells += 1;
    FreeCommit("Fuel cell purchased");
    return true;
  }
  if (id == "repair-kit" && state_.shiplets >= 70) {
    state_.shiplets -= 70;
    state_.repairKits += 1;
    FreeCommit("Repair kit purchased");
    return true;
  }
  if (id == "medkit" && state_.shiplets >= 50) {
    state_.shiplets -= 50;
    state_.medkits += 1;
    FreeCommit("Medkit purchased");
    return true;
  }
  if (id == "canister" && state_.shiplets >= 25 && life_.canisters < life_.capacity &&
      FindOngoing("project-oxygen") == nullptr) {
    state_.shiplets -= 25;
    life_.canisters += 1;
    FreeCommit("O2 canister purchased");
    return true;
  }
  if (id == "rations" && state_.shiplets >= 20 &&
      mess_.rations <= mess_.rationCapacity - 6 &&
      FindOngoing("project-food") == nullptr) {
    state_.shiplets -= 20;
    mess_.rations += 6;  // dry rations go in the larder; the galley cooks them
    FreeCommit("Rations purchased");
    return true;
  }
  return false;
}

bool Simulation::BuyGood(const std::string& goodId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }

  const Good* good = catalog_.FindGood(goodId);
  const std::vector<Good> shelf = MarketAt(map_.here);
  const bool stocked = std::any_of(shelf.begin(), shelf.end(),
                                   [&](const Good& candidate) { return candidate.id == goodId; });
  if (good == nullptr || !stocked) { return false; }

  GoodsLot probe;
  probe.id = good->id;
  probe.name = good->name;
  probe.kind = good->kind;
  probe.vol = good->vol;
  probe.value = good->value;
  probe.risk = good->risk;

  const double price = js::Ceil(LotPrice(probe) * 1.2);
  if (state_.shiplets < price) { return false; }
  if (CargoUsed() + CargoReserved() + good->vol > cargo_.capacity) { return false; }

  state_.shiplets -= price;
  state_.lotSerial += 1;
  GoodsLot lot = probe;
  lot.id = "lot-" + js::NumToString(state_.lotSerial);
  lot.goodId = good->id;
  lot.originPort = map_.here;
  lot.boughtAt = actionCounter_;
  lot.marketPurchase = true;
  goods_.push_back(lot);
  FreeCommit("Bought " + good->name);
  return true;
}

bool Simulation::SellGood(const std::string& lotId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }

  for (std::size_t index = 0; index < goods_.size(); ++index) {
    if (goods_[index].id != lotId) { continue; }
    if (!goods_[index].contractId.empty()) { return false; }  // contract cargo is not for sale
    const double price = LotPrice(goods_[index]);
    const GoodsLot sold = goods_[index];
    goods_.erase(goods_.begin() + static_cast<std::ptrdiff_t>(index));
    state_.shiplets += price;
    // A freelance request is fulfilled by the actual sale, not by arrival.
    // Only lots bought at its origin after acceptance count, and each lot can
    // advance one request. The bonus and the sale share this single action.
    for (TradeContract& contract : state_.trade.contracts) {
      if (!LotSatisfiesFreelance(sold, contract)) { continue; }
      contract.fulfilled += 1;
      if (contract.fulfilled >= contract.quantity) {
        contract.status = "complete";
        state_.trade.completed += 1;
        state_.shiplets += contract.reward;
        LogLine("Freelance sale finished: " + contract.title + " paid a " +
                js::NumToString(contract.reward) + " sh bonus.");
        SimRemember("Work", "The crew sold the requested goods and finished " + contract.title + ".");
      } else {
        LogLine("Freelance sale: " + std::to_string(contract.fulfilled) + " of " +
                std::to_string(contract.quantity) + " lots sold for " + contract.title + ".");
      }
      break;
    }
    PerformAction("Sold " + sold.name);
    return true;
  }
  return false;
}

bool Simulation::CargoSortDue() const { return actionCounter_ >= cargo_.sortDueAt; }

bool Simulation::CargoLotStowed(const GoodsLot& lot) const {
  return lot.stowed && actionCounter_ - lot.stowedAt < kCargoShiftPeriod;
}

bool Simulation::SortCargo(const std::string& lotId) {
  if (!state_.gameOver.empty()) { return false; }
  for (GoodsLot& lot : goods_) {
    if (lot.id != lotId) { continue; }
    if (CargoLotStowed(lot)) { return false; }
    const bool paid = CargoSortDue();
    const std::string name = lot.name;
    lot.stowed = true;
    if (paid) {
      PerformAction("Sorted " + name + " into the hold");
      cargo_.sortDueAt = actionCounter_ + kCargoSortPeriod;
    } else {
      FreeCommit("Stowed " + name);
    }
    lot.stowedAt = actionCounter_;
    return true;
  }
  return false;
}

bool Simulation::SellSurvey(const std::string& recordId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }
  if (FindOngoing("pkg-" + recordId) != nullptr) { return false; }

  for (std::size_t index = 0; index < survey_.size(); ++index) {
    if (survey_[index].id != recordId) { continue; }
    const double value = survey_[index].value;
    const std::string name = survey_[index].name;
    survey_.erase(survey_.begin() + static_cast<std::ptrdiff_t>(index));
    state_.shiplets += value;
    PerformAction("Sold survey - " + name);
    return true;
  }
  return false;
}

bool Simulation::Jettison(const std::string& lotId) {
  if (!state_.gameOver.empty()) { return false; }
  for (std::size_t index = 0; index < goods_.size(); ++index) {
    if (goods_[index].id != lotId) { continue; }
    const std::string name = goods_[index].name;
    const std::string contract = goods_[index].contractId;
    goods_.erase(goods_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!contract.empty()) {
      for (TradeContract& trade : state_.trade.contracts) {
        if (trade.id == contract && trade.status == "active") { trade.status = "failed"; }
      }
    }
    PerformAction("Jettisoned " + name);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Salvage and survey packaging
// ---------------------------------------------------------------------------

bool Simulation::StartSalvage(int bay) {
  const std::string id = "salvage-" + std::to_string(bay);
  const Task* trip = FindOngoing("travel");
  if (trip == nullptr || trip->state != "running") { return false; }
  if (FindOngoing(id) != nullptr) { return false; }
  if (StatValue("energy") <= 4) { return false; }
  if (CargoUsed() + CargoReserved() + 3 > cargo_.capacity) { return false; }

  ClampStat("energy", -4);
  PerformAction("Salvage recovery started");
  if (!state_.gameOver.empty()) { return true; }

  Task* task = AddOngoing(id, "Recovering salvage - bay " + std::to_string(bay + 1), 2, "salvage", "");
  if (task != nullptr) { task->vol = 3; }
  return true;
}

bool Simulation::PackageSurvey(const std::string& recordId) {
  if (!HasRoom("science")) return false;
  if (FindOngoing("pkg-" + recordId) != nullptr) { return false; }
  const SurveyRecord* record = nullptr;
  for (const SurveyRecord& candidate : survey_) {
    if (candidate.id == recordId) { record = &candidate; }
  }
  if (record == nullptr || record->state == "packaged") { return false; }

  const std::string name = record->name;
  PerformAction("Packaging survey - " + name);
  if (!state_.gameOver.empty()) { return true; }
  AddOngoing("pkg-" + recordId, "Packaging " + name, 2, "surveyPackage", recordId);
  return true;
}

bool Simulation::HireCrew(const std::string& candidateId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }
  if (Truthy(state_.hired, candidateId)) { return false; }

  const CandidateDefinition* candidate = nullptr;
  for (const CandidateDefinition& entry : catalog_.candidates) {
    if (entry.crew.id == candidateId) { candidate = &entry; }
  }
  if (candidate == nullptr) { return false; }
  if (!candidate->port.empty() && candidate->port != map_.here) { return false; }
  if (static_cast<double>(crew_.size() + state_.passengers.size()) >= ship_.Berths()) { return false; }
  if (state_.shiplets < candidate->fee) { return false; }

  state_.shiplets -= candidate->fee;
  state_.hired[candidateId] = true;
  crew_.push_back(MakeCrew(candidate->crew));
  const std::string name = candidate->crew.name;
  SimRemember("Arrival", name + " joined the crew at " + HerePort()->name + ".", candidateId);
  FreeCommit(name + " joined the crew");
  return true;
}

std::string Simulation::PatronName(const std::string& patronId) const {
  if (patronId == "trader") { return "the trader"; }
  for (const CandidateDefinition& entry : catalog_.candidates) {
    if (entry.crew.id == patronId) { return entry.crew.name; }
  }
  return patronId;
}

bool Simulation::TalkToPatron(const std::string& patronId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }
  if (Truthy(state_.talked, patronId)) { return false; }
  state_.talked[patronId] = true;
  PerformAction("Spoke with " + PatronName(patronId) + " in the station bar");
  return true;
}

bool Simulation::BuyUpgrade(const std::string& upgradeId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }

  const std::string className = state_.shipClass;
  if (upgradeId == "bay" && !Truthy(state_.upgrades, "bay") && state_.shiplets >= 500) {
    const double capacity = ShipCargoCapacity(className, 1);
    state_.shiplets -= 500;
    state_.upgrades["bay"] = true;
    cargo_.capacity = std::max(cargo_.capacity, capacity);
    FreeCommit("Cargo hold expanded");
    return true;
  }
  if (upgradeId == "bay2" && Truthy(state_.upgrades, "bay") && !Truthy(state_.upgrades, "bay2") &&
      state_.shiplets >= 900) {
    const double capacity = ShipCargoCapacity(className, 2);
    state_.shiplets -= 900;
    state_.upgrades["bay2"] = true;
    cargo_.capacity = std::max(cargo_.capacity, capacity);
    FreeCommit("Cargo hold enlarged again");
    return true;
  }
  if (upgradeId == "drive" && !Truthy(state_.upgrades, "drive") && state_.shiplets >= 900) {
    state_.shiplets -= 900;
    state_.upgrades["drive"] = true;
    FreeCommit("Efficient drive fitted");
    return true;
  }
  if (upgradeId == "rack" && !Truthy(state_.upgrades, "rack") && state_.shiplets >= 400) {
    state_.shiplets -= 400;
    state_.upgrades["rack"] = true;
    life_.capacity = 15;
    FreeCommit("O2 rack expanded");
    return true;
  }
  if (upgradeId == "scanner" && !Truthy(state_.upgrades, "scanner") && state_.shiplets >= 600) {
    state_.shiplets -= 600;
    state_.upgrades["scanner"] = true;
    scanCost_ = 3;
    FreeCommit("Sensors upgraded");
    return true;
  }
  if (upgradeId == "scanner2" && Truthy(state_.upgrades, "scanner") &&
      !Truthy(state_.upgrades, "scanner2") && state_.shiplets >= 1100) {
    state_.shiplets -= 1100;
    state_.upgrades["scanner2"] = true;
    scanCost_ = 2;
    FreeCommit("Deep-array sensors fitted");
    return true;
  }
  if (upgradeId == "hull" && StatValue("hull") < 100 && state_.shiplets >= 120) {
    state_.shiplets -= 120;
    ClampStat("hull", 50);
    FreeCommit("Station repaired the hull");
    return true;
  }
  return false;
}

std::vector<Good> Simulation::MarketAt(const std::string& portId) const {
  const Port* port = catalog_.FindPort(portId);
  std::vector<Good> shelf;
  if (port == nullptr) { return shelf; }
  for (const std::string& id : port->market) {
    const Good* good = catalog_.FindGood(id);
    if (good != nullptr) { shelf.push_back(*good); }
  }
  return shelf;
}

std::vector<Good> Simulation::Market() const { return MarketAt(map_.here); }

}  // namespace ship

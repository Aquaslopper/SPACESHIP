// ============================================================================
//  Economy.cpp — ports, cargo, contracts and the commission board.
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

double Simulation::LotPrice(const GoodsLot& lot) const {
  const Port* port = HerePort();
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
  // Salvage in flight has already been promised the space it will take.
  double reserved = 0;
  for (const Task& task : state_.ongoing) {
    if (task.type == "salvage" && task.state == "running") { reserved += task.vol; }
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
    PerformAction("Fuel cell purchased");
    return true;
  }
  if (id == "repair-kit" && state_.shiplets >= 70) {
    state_.shiplets -= 70;
    state_.repairKits += 1;
    PerformAction("Repair kit purchased");
    return true;
  }
  if (id == "medkit" && state_.shiplets >= 50) {
    state_.shiplets -= 50;
    state_.medkits += 1;
    PerformAction("Medkit purchased");
    return true;
  }
  if (id == "canister" && state_.shiplets >= 25 && life_.canisters < life_.capacity &&
      FindOngoing("project-oxygen") == nullptr) {
    state_.shiplets -= 25;
    life_.canisters += 1;
    PerformAction("O2 canister purchased");
    return true;
  }
  if (id == "rations" && state_.shiplets >= 20 && mess_.rack <= mess_.capacity - 6 &&
      FindOngoing("project-food") == nullptr) {
    state_.shiplets -= 20;
    mess_.rack += 6;
    PerformAction("Rations purchased");
    return true;
  }
  return false;
}

bool Simulation::BuyGood(const std::string& goodId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }

  const Good* good = nullptr;
  for (const Good& candidate : catalog_.goods) {
    if (candidate.id == goodId) { good = &candidate; }
  }
  if (good == nullptr) { return false; }

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
  goods_.push_back(lot);
  PerformAction("Bought " + good->name);
  return true;
}

bool Simulation::SellGood(const std::string& lotId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }

  for (std::size_t index = 0; index < goods_.size(); ++index) {
    if (goods_[index].id != lotId) { continue; }
    if (!goods_[index].contractId.empty()) { return false; }  // contract cargo is not for sale
    const double price = LotPrice(goods_[index]);
    const std::string name = goods_[index].name;
    goods_.erase(goods_.begin() + static_cast<std::ptrdiff_t>(index));
    state_.shiplets += price;
    PerformAction("Sold " + name);
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
    if (!contract.empty()) { state_.contracts[contract] = "failed"; }
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

// ---------------------------------------------------------------------------
// Contracts
// ---------------------------------------------------------------------------

void Simulation::ContractProgress(const std::string& goal, const std::string& target) {
  bool changed = false;
  for (const Contract& contract : catalog_.contracts) {
    auto state = state_.contracts.find(contract.id);
    if (state == state_.contracts.end() || state->second != "active") { continue; }
    if (contract.target != target) { continue; }
    const bool visitCoversFreight = goal == "visit" && (contract.goal == "freight" || contract.goal == "passenger");
    if (contract.goal != goal && !visitCoversFreight) { continue; }

    if (contract.goal == "freight") {
      // The consignment leaves the hold when it is delivered.
      bool found = false;
      for (std::size_t index = goods_.size(); index-- > 0;) {
        if (goods_[index].id != "contract-" + contract.id) { continue; }
        goods_.erase(goods_.begin() + static_cast<std::ptrdiff_t>(index));
        found = true;
      }
      if (!found) { continue; }
    }
    if (contract.goal == "passenger") {
      bool found = false;
      for (std::size_t index = state_.passengers.size(); index-- > 0;) {
        if (state_.passengers[index].id != contract.id) { continue; }
        state_.passengers.erase(state_.passengers.begin() + static_cast<std::ptrdiff_t>(index));
        found = true;
      }
      if (!found) { continue; }
    }

    state_.contracts[contract.id] = "ready";
    changed = true;
    LogLine("Contract ready to collect: " + contract.title + ".");
  }
  (void)changed;
}

bool Simulation::AcceptContract(const std::string& contractId) {
  if (!state_.gameOver.empty() || !state_.docked) { return false; }

  const Contract* contract = nullptr;
  for (const Contract& candidate : catalog_.contracts) {
    if (candidate.id == contractId) { contract = &candidate; }
  }
  if (contract == nullptr) { return false; }

  auto current = state_.contracts.find(contract->id);
  const std::string status = current == state_.contracts.end() ? std::string("available") : current->second;
  if (status != "available" && status != "failed") { return false; }

  if (!contract->origin.empty() && contract->origin != map_.here) { return false; }
  if (contract->vol > 0 && CargoUsed() + CargoReserved() + contract->vol > cargo_.capacity) { return false; }
  if (contract->goal == "passenger" &&
      static_cast<double>(crew_.size() + state_.passengers.size()) >= ship_.Berths()) {
    return false;
  }

  state_.contracts[contract->id] = "active";
  if (contract->goal == "freight") {
    GoodsLot lot;
    lot.id = "contract-" + contract->id;
    lot.name = contract->cargo.empty() ? "Medical consignment" : contract->cargo;
    lot.vol = contract->vol;
    lot.value = 0;
    lot.kind = "Contract freight";
    lot.risk = false;
    lot.contractId = contract->id;
    goods_.push_back(lot);
  }
  if (contract->goal == "passenger") {
    state_.passengers.push_back(Passenger{contract->id, "Station passenger"});
  }

  // A contract accepted for work already done is ready immediately.
  const bool priorVisit = contract->goal == "visit" && (contract->target == map_.here || contract->target == CurrentSystemId());
  const bool priorScan = contract->goal == "scan" && state_.scanned.count(contract->target) != 0 &&
                         state_.scanned.at(contract->target);
  if (contract->recognizePrior && (priorVisit || priorScan)) {
    state_.contracts[contract->id] = "ready";
    LogLine("Contract ready to collect: " + contract->title + ".");
  }

  PerformAction("Accepted contract - " + contract->title);
  return true;
}

bool Simulation::CollectContract(const std::string& contractId) {
  if (!state_.gameOver.empty() || !state_.docked) { return false; }
  auto state = state_.contracts.find(contractId);
  if (state == state_.contracts.end() || state->second != "ready") { return false; }

  const Contract* contract = nullptr;
  for (const Contract& candidate : catalog_.contracts) {
    if (candidate.id == contractId) { contract = &candidate; }
  }
  if (contract == nullptr) { return false; }

  state->second = "complete";
  state_.shiplets += contract->reward;
  PerformAction("Contract paid - " + contract->title);
  return true;
}

// ---------------------------------------------------------------------------
// Hiring and refits
// ---------------------------------------------------------------------------

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
  PerformAction(name + " joined the crew");
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
    PerformAction("Cargo hold expanded");
    return true;
  }
  if (upgradeId == "bay2" && Truthy(state_.upgrades, "bay") && !Truthy(state_.upgrades, "bay2") &&
      state_.shiplets >= 900) {
    const double capacity = ShipCargoCapacity(className, 2);
    state_.shiplets -= 900;
    state_.upgrades["bay2"] = true;
    cargo_.capacity = std::max(cargo_.capacity, capacity);
    PerformAction("Cargo hold enlarged again");
    return true;
  }
  if (upgradeId == "drive" && !Truthy(state_.upgrades, "drive") && state_.shiplets >= 900) {
    state_.shiplets -= 900;
    state_.upgrades["drive"] = true;
    PerformAction("Efficient drive fitted");
    return true;
  }
  if (upgradeId == "rack" && !Truthy(state_.upgrades, "rack") && state_.shiplets >= 400) {
    state_.shiplets -= 400;
    state_.upgrades["rack"] = true;
    life_.capacity = 15;
    PerformAction("O2 rack expanded");
    return true;
  }
  if (upgradeId == "scanner" && !Truthy(state_.upgrades, "scanner") && state_.shiplets >= 600) {
    state_.shiplets -= 600;
    state_.upgrades["scanner"] = true;
    scanCost_ = 3;
    PerformAction("Sensors upgraded");
    return true;
  }
  if (upgradeId == "hull" && StatValue("hull") < 100 && state_.shiplets >= 120) {
    state_.shiplets -= 120;
    ClampStat("hull", 50);
    PerformAction("Station repaired the hull");
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Commissions
// ---------------------------------------------------------------------------

double CommissionGoal(const std::string& kind) { return kind == "salvage" ? 2.0 : 1.0; }

namespace {
double CommissionPrice(const std::string& kind) {
  if (kind == "visit") { return 450; }
  if (kind == "scan") { return 550; }
  if (kind == "salvage") { return 240; }
  return 0;
}
}  // namespace

void Simulation::CommissionProgress(const std::string& kind, const std::string& target) {
  if (!state_.commissions.active.has_value()) { return; }
  Commission& commission = *state_.commissions.active;
  if (commission.kind != kind) { return; }
  if (commission.progress >= CommissionGoal(kind)) { return; }
  if (kind != "salvage" && commission.target != target) { return; }

  commission.progress += 1;
  if (commission.progress != CommissionGoal(kind)) { return; }

  // The description is a sentence, not an id: the Samacron's memory keeps it.
  std::string text;
  if (commission.kind == "visit") {
    const Body* body = FindBodyById(commission.target);
    text = "Visit " + (body == nullptr ? commission.target : body->name);
  } else if (commission.kind == "scan") {
    const Body* body = FindBodyById(commission.target);
    text = "Read a deep scan of " + (body == nullptr ? commission.target : body->name);
  } else {
    text = "Recover two salvage lots while under way";
  }
  SimRemember("Work", "The crew completed a commission: " + text + ".");
  LogLine("Commission objective complete. Collect at any station.");
}

namespace {
std::string BodyName(const Simulation& sim, const std::string& id) {
  const Body* const body = sim.FindBodyById(id);
  return body != nullptr ? body->name : id;
}
}  // namespace

std::string CommissionDescription(const Simulation& sim, const Commission& active) {
  const Body* const body = sim.FindBodyById(active.target);
  std::string where;
  if (body != nullptr) {
    const Body* const system = sim.FindBodyById(body->system);
    if (system != nullptr) {
      where = " (" + system->name;
      const Body* const region = sim.FindBodyById(system->region);
      if (region != nullptr) { where += ", " + region->name; }
      where += ")";
    }
  }
  if (active.kind == "visit") { return "Visit " + BodyName(sim, active.target) + where; }
  if (active.kind == "scan") {
    return "Read a deep scan of " + BodyName(sim, active.target) + where;
  }
  return "Recover two salvage lots while under way";
}

std::optional<Commission> Simulation::CommissionOffer(const std::string& kind) const {
  if (CommissionPrice(kind) <= 0) { return std::nullopt; }

  // commissionOffer(): a deterministic draw from the bodies this station can
  // reasonably point at, rotated by the commission serial - no randomness.
  Commission offer;
  offer.kind = kind;
  offer.origin = map_.here;
  offer.progress = 0;
  offer.reward = CommissionPrice(kind);

  if (kind != "salvage") {
    const Body* here = CurrentSystem();
    std::vector<const Body*> all;
    std::vector<const Body*> local;
    std::vector<const Body*> fresh;
    for (const Body& body : catalog_.bodies) {
      const std::string systemId = catalog_.BodySystem(body);
      const Body* system = nullptr;
      for (const Body& candidate : catalog_.systems) {
        if (candidate.id == systemId) { system = &candidate; }
      }
      if (system == nullptr) { continue; }
      if (kind == "visit" && (catalog_.FindPort(body.id) == nullptr || body.id == map_.here)) { continue; }
      if (kind == "scan" && (system->id == CurrentSystemId() || body.type == "origin")) { continue; }
      all.push_back(&body);
      if (here != nullptr && system->region == here->region) {
        local.push_back(&body);
        if (kind == "scan" && (state_.scanned.count(body.id) == 0 || !state_.scanned.at(body.id))) {
          fresh.push_back(&body);
        }
      }
    }

    // A region with only two ports needs a nearby alternative rather than the
    // same route forever.
    if (kind == "visit" && local.size() < 2) {
      const Body* originRegion = here == nullptr ? nullptr : FindBodyById(here->region);
      while (local.size() < 2 && local.size() < all.size()) {
        const Body* nearest = nullptr;
        double best = 1e18;
        for (const Body* body : all) {
          if (std::find(local.begin(), local.end(), body) != local.end()) { continue; }
          const Body* system = nullptr;
          for (const Body& candidate : catalog_.systems) {
            if (candidate.id == catalog_.BodySystem(*body)) { system = &candidate; }
          }
          if (system == nullptr) { continue; }
          const Body* candidateRegion = FindBodyById(system->region);
          double distance = 1e18;
          if (originRegion != nullptr && candidateRegion != nullptr) {
            distance = std::pow(originRegion->x - candidateRegion->x, 2) +
                       std::pow(originRegion->y - candidateRegion->y, 2);
          }
          if (distance < best) { nearest = body; best = distance; }
        }
        if (nearest == nullptr) { break; }
        local.push_back(nearest);
      }
    }

    const std::vector<const Body*>& pool = !fresh.empty() ? fresh : (!local.empty() ? local : all);
    if (pool.empty()) { return std::nullopt; }

    double hash = 0;
    for (std::size_t index = 0; index < map_.here.size(); ++index) {
      hash += static_cast<double>(static_cast<unsigned char>(map_.here[index])) * static_cast<double>(index + 1);
    }
    const double slot = std::fmod(hash + state_.commissions.serial, static_cast<double>(pool.size()));
    offer.target = pool[static_cast<std::size_t>(slot)]->id;
  }

  return offer;
}

bool Simulation::AcceptCommission(const std::string& kind) {
  if (!state_.gameOver.empty() || state_.commissions.active.has_value()) { return false; }
  if (!state_.docked || HerePort() == nullptr) { return false; }
  const std::optional<Commission> offer = CommissionOffer(kind);
  if (!offer.has_value()) { return false; }
  state_.commissions.active = *offer;
  PerformAction("Commission accepted");
  return true;
}

bool Simulation::CollectCommission() {
  if (!state_.commissions.active.has_value() || !state_.docked || HerePort() == nullptr) { return false; }
  if (state_.gameOver.empty() == false) { return false; }
  const Commission commission = *state_.commissions.active;
  if (commission.progress < CommissionGoal(commission.kind)) { return false; }

  state_.shiplets += commission.reward;
  state_.commissions.completed += 1;
  state_.commissions.serial += 1;
  state_.commissions.active.reset();
  PerformAction("Commission paid");
  return true;
}

bool Simulation::AbandonCommission() {
  if (!state_.commissions.active.has_value() || !state_.docked || HerePort() == nullptr) { return false; }
  if (!state_.gameOver.empty()) { return false; }
  state_.commissions.serial += 1;
  state_.commissions.active.reset();
  PerformAction("Commission abandoned");
  return true;
}

}  // namespace ship

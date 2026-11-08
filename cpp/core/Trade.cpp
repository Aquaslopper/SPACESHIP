// ============================================================================
//  Trade.cpp — station-local offers, accepted work and the 200-click board.
//  ---------------------------------------------------------------------------
//  The reference had a global mission board. This replacement derives its three
//  public routes from the station's own system page and the ports around it.
//  Unaccepted offers are pure functions of (station, kind, floor(clock/200));
//  accepted work is a saved snapshot and is never regenerated from that board.
// ============================================================================
#include "core/Simulation.h"

#include "core/JsNumber.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>

namespace ship {
namespace {

std::uint64_t Hash(const std::string& text) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : text) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::string NameOf(const Simulation& sim, const std::string& id) {
  const Body* body = sim.FindBodyById(id);
  return body == nullptr ? id : body->name;
}

double SystemDistance(const Catalog& catalog, const std::string& aPort, const std::string& bPort) {
  const Body* a = catalog.FindBody(aPort);
  const Body* b = catalog.FindBody(bPort);
  const Body* as = a == nullptr ? nullptr : catalog.FindSystem(catalog.BodySystem(*a));
  const Body* bs = b == nullptr ? nullptr : catalog.FindSystem(catalog.BodySystem(*b));
  if (as == nullptr || bs == nullptr) { return 1e9; }
  return std::hypot(as->x - bs->x, as->y - bs->y);
}

std::string SourceOf(const Port& port, const std::string& goodId) {
  for (const MarketSource& source : port.sources) {
    if (source.goodId == goodId) { return source.bodyId; }
  }
  return std::string();
}

double BuyPrice(const Simulation& sim, const Port& port, const Good& good) {
  GoodsLot probe;
  probe.value = good.value;
  probe.kind = good.kind;
  return js::Ceil(sim.LotPriceAt(port.id, probe) * 1.2);
}

std::vector<const Port*> NearbyPorts(const Simulation& sim, const std::string& portId) {
  const Catalog& world = sim.World();
  const Body* here = world.FindBody(portId);
  const Body* system = here == nullptr ? nullptr : world.FindSystem(world.BodySystem(*here));
  std::vector<std::pair<double, std::string>> ranked;
  for (const Port& port : world.ports) {
    if (port.id == portId) { continue; }
    const Body* body = world.FindBody(port.id);
    const Body* peer = body == nullptr ? nullptr : world.FindSystem(world.BodySystem(*body));
    if (peer == nullptr || system == nullptr) { continue; }
    double distance = SystemDistance(world, portId, port.id);
    if (peer->region != system->region) { distance += 10000.0; }
    ranked.emplace_back(distance, port.id);
  }
  std::sort(ranked.begin(), ranked.end());
  std::vector<const Port*> out;
  for (std::size_t i = 0; i < ranked.size() && i < 10; ++i) {
    out.push_back(world.FindPort(ranked[i].second));
  }
  return out;
}

std::string OfferId(const std::string& portId, std::uint64_t epoch, const std::string& kind,
                    const std::string& suffix) {
  return "trade:" + portId + ":" + std::to_string(epoch) + ":" + kind + ":" + suffix;
}

int MaxQuantity(const Good& good) {
  return std::max(1, std::min(3, static_cast<int>(24.0 / std::max(1.0, static_cast<double>(good.vol)))));
}

}  // namespace

std::string TradeKindLabel(const std::string& kind) {
  if (kind == "delivery") { return "Delivery"; }
  if (kind == "freelance") { return "Freelance"; }
  if (kind == "station") { return "Station-to-station"; }
  return kind;
}

std::string TradeDescription(const Simulation& sim, const TradeContract& contract) {
  if (!contract.legacyGoal.empty()) { return contract.note; }
  const Good* good = sim.World().FindGood(contract.goodId);
  const std::string cargo = good == nullptr ? contract.goodId : good->name;
  const std::string destination = NameOf(sim, contract.destination);
  if (contract.kind == "station") { return "Exchange survey data with " + destination; }
  if (contract.kind == "freelance") {
    return "Buy " + std::to_string(contract.quantity) + " x " + cargo + " here, then sell at " + destination;
  }
  return "Deliver " + std::to_string(contract.quantity) + " x " + cargo + " to " + destination;
}

std::string TradeRouteLayer(const Simulation& sim, const TradeContract& contract) {
  // A data exchange is between berths, so it is read one scale above the local
  // system chart even when both berths happen to orbit in the same system.
  if (contract.kind == "station") { return "sector"; }
  const Body* destination = sim.FindBodyById(contract.destination);
  if (destination == nullptr) { return "sector"; }
  const std::string targetSystem = destination->type == "system"
                                       ? destination->id
                                       : sim.World().BodySystem(*destination);
  return targetSystem == sim.CurrentSystemId() ? "system" : "sector";
}

std::uint64_t Simulation::TradeEpoch() const {
  return static_cast<std::uint64_t>(std::floor(actionCounter_ / kTradeRotationActions));
}

std::vector<TradeContract> Simulation::TradeOffers(const std::string& portId,
                                                   const std::string& kind) const {
  const Port* port = catalog_.FindPort(portId);
  if (port == nullptr) { return {}; }
  const std::uint64_t epoch = TradeEpoch();
  const std::uint64_t salt = Hash(portId + "|" + kind);
  // Consecutive epochs cannot tie on the premium, even in a one-port fixture.
  const double premium = static_cast<double>(epoch % 7) * 5.0;
  std::vector<TradeContract> out;

  if (kind == "station") {
    std::vector<std::string> visited = state_.trade.visited;
    std::sort(visited.begin(), visited.end());
    visited.erase(std::remove(visited.begin(), visited.end(), portId), visited.end());
    if (visited.empty()) { return out; }
    const std::size_t offset = (epoch + salt) % visited.size();
    for (std::size_t i = 0; i < visited.size(); ++i) {
      const std::string& target = visited[(i + offset) % visited.size()];
      if (catalog_.FindPort(target) == nullptr) { continue; }
      TradeContract offer;
      offer.id = OfferId(portId, epoch, kind, target);
      offer.kind = kind;
      offer.origin = portId;
      offer.destination = target;
      offer.title = "Data exchange: " + NameOf(*this, target);
      offer.note = "Survey data is copied between the two stations. No cargo space or separate data resource is used.";
      offer.reward = js::Round(90.0 + SystemDistance(catalog_, portId, target) * 0.12 + premium);
      out.push_back(std::move(offer));
    }
    return out;
  }

  if (kind == "delivery") {
    const std::vector<const Port*> ports = NearbyPorts(*this, portId);
    std::vector<Good> goods = MarketAt(portId);
    goods.erase(std::remove_if(goods.begin(), goods.end(), [](const Good& good) { return good.risk; }), goods.end());
    if (ports.empty() || goods.empty()) { return out; }
    std::set<std::pair<std::string, std::string>> used;
    const std::size_t count = std::min<std::size_t>(4, ports.size() * goods.size());
    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t di = (salt + epoch + i) % ports.size();
      const std::size_t gi = (salt / 19 + epoch + i * 2) % goods.size();
      const Port* destination = ports[di];
      const Good& good = goods[gi];
      if (!used.insert({destination->id, good.id}).second) { continue; }
      TradeContract offer;
      offer.id = OfferId(portId, epoch, kind, destination->id + ":" + good.id);
      offer.kind = kind;
      offer.origin = portId;
      offer.destination = destination->id;
      offer.goodId = good.id;
      offer.sourceBody = SourceOf(*port, good.id);
      offer.quantity = 1 + static_cast<int>((epoch + i + salt) % static_cast<std::uint64_t>(MaxQuantity(good)));
      offer.title = TradeKindLabel(kind) + ": " + good.name + " to " + destination->name;
      offer.note = "Sealed freight is loaded on acceptance. Carry it to the destination berth, then unload and cash in from the Cargo Hold.";
      offer.reward = js::Round(80.0 + good.value * offer.quantity * 0.35 +
                               SystemDistance(catalog_, portId, destination->id) * 0.08 + premium);
      out.push_back(std::move(offer));
    }
    return out;
  }

  if (kind == "freelance") {
    struct Request { const Good* good; const Port* destination; double margin; std::uint64_t rank; };
    std::vector<Request> requests;
    for (const Good& good : MarketAt(portId)) {
      if (good.risk) { continue; }
      const double buy = BuyPrice(*this, *port, good);
      for (const Port& destination : catalog_.ports) {
        if (destination.id == portId) { continue; }
        GoodsLot probe;
        probe.kind = good.kind;
        probe.value = good.value;
        const double sale = LotPriceAt(destination.id, probe);
        if (sale <= buy) { continue; }
        requests.push_back({&good, &destination, sale - buy, Hash(portId + good.id + destination.id)});
      }
    }
    std::sort(requests.begin(), requests.end(), [](const Request& a, const Request& b) {
      if (a.margin != b.margin) { return a.margin > b.margin; }
      return a.rank < b.rank;
    });
    if (requests.empty()) { return out; }
    // Keep a bounded, varied candidate pool, then rotate it. The request still
    // names a real local source body and a destination that pays more today.
    const std::size_t pool = std::min<std::size_t>(requests.size(), 18);
    std::set<std::pair<std::string, std::string>> used;
    for (std::size_t i = 0; i < std::min<std::size_t>(pool, 3); ++i) {
      const Request& request = requests[(salt + epoch * 5 + i * 7) % pool];
      if (!used.insert({request.good->id, request.destination->id}).second) { continue; }
      TradeContract offer;
      offer.id = OfferId(portId, epoch, kind, request.destination->id + ":" + request.good->id);
      offer.kind = kind;
      offer.origin = portId;
      offer.destination = request.destination->id;
      offer.goodId = request.good->id;
      offer.sourceBody = SourceOf(*port, request.good->id);
      offer.quantity = 1 + static_cast<int>((epoch + i + salt / 23) %
                                           static_cast<std::uint64_t>(MaxQuantity(*request.good)));
      offer.title = TradeKindLabel(kind) + ": " + request.good->name + " for " + request.destination->name;
      offer.note = "Buy the goods after accepting, carry them yourself, and sell them at the named destination. The sale pays the market price plus this request's bonus.";
      offer.reward = js::Round(60.0 + SystemDistance(catalog_, portId, request.destination->id) * 0.05 + premium);
      out.push_back(std::move(offer));
    }
    return out;
  }
  return out;
}

std::optional<TradeContract> Simulation::FindTradeOffer(const std::string& offerId) const {
  for (const char* kind : {"delivery", "freelance", "station"}) {
    for (const TradeContract& offer : TradeOffers(map_.here, kind)) {
      if (offer.id == offerId) { return offer; }
    }
  }
  return std::nullopt;
}

const TradeContract* Simulation::FindTradeContract(const std::string& contractId) const {
  for (const TradeContract& contract : state_.trade.contracts) {
    if (contract.id == contractId) { return &contract; }
  }
  return nullptr;
}

std::vector<TradeContract> Simulation::ActiveTradeContracts(const std::string& kind) const {
  std::vector<TradeContract> out;
  for (const TradeContract& contract : state_.trade.contracts) {
    if ((contract.status == "active" || contract.status == "ready") &&
        (kind.empty() || contract.kind == kind)) {
      out.push_back(contract);
    }
  }
  return out;
}

bool Simulation::AcceptTrade(const std::string& offerId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr ||
      HerePort()->id != map_.here || FindTradeContract(offerId) != nullptr) {
    return false;
  }
  const std::optional<TradeContract> found = FindTradeOffer(offerId);
  if (!found.has_value() || found->origin != map_.here) { return false; }
  TradeContract contract = *found;
  if (contract.kind == "delivery") {
    const Good* good = catalog_.FindGood(contract.goodId);
    if (good == nullptr || contract.quantity <= 0) { return false; }
    const double volume = good->vol * contract.quantity;
    if (CargoUsed() + CargoReserved() + volume > cargo_.capacity) { return false; }
    contract.lotId = "trade-" + contract.id;
    GoodsLot lot;
    lot.id = contract.lotId;
    lot.name = good->name + " (delivery)";
    lot.kind = "Contract freight";
    lot.vol = volume;
    lot.value = 0;
    lot.contractId = contract.id;
    lot.goodId = good->id;
    goods_.push_back(std::move(lot));
  }
  contract.status = "active";
  contract.acceptedAt = actionCounter_ + 1;
  state_.trade.contracts.push_back(contract);
  PerformAction("Trade accepted - " + contract.title);
  // Plotting the green path is reading, not a second commitment.
  const std::string layer = TradeRouteLayer(*this, contract);
  SetLayer(layer);
  SelectTarget(contract.destination);
  return true;
}

void Simulation::RememberStationVisit(const std::string& portId) {
  if (catalog_.FindPort(portId) == nullptr) { return; }
  auto& visited = state_.trade.visited;
  if (std::find(visited.begin(), visited.end(), portId) == visited.end()) {
    visited.push_back(portId);
  }
}

void Simulation::SettleTradeArrival(const std::string& bodyId) {
  // Arrival is not a trade commitment. It remembers the berth and lets the
  // Cargo Hold offer the one-click unload/finalize action; the clock advances
  // only when the player actually takes that action.
  RememberStationVisit(bodyId);
}

bool Simulation::TradeReadyHere(const TradeContract& contract) const {
  if (contract.status != "active" && contract.status != "ready") { return false; }
  if (state_.gameOver.empty() == false || !state_.docked || HerePort() == nullptr ||
      HerePort()->id != map_.here) {
    return false;
  }
  if (contract.kind == "freelance") { return false; }  // the sale itself finalizes
  if (contract.legacyReady) { return true; }
  if (contract.kind == "station") { return contract.destination == map_.here; }
  if (contract.kind != "delivery") { return false; }
  if (contract.legacyGoal == "visit") {
    return contract.destination == map_.here || contract.destination == CurrentSystemId();
  }
  if (contract.legacyGoal == "scan" || contract.legacyGoal == "salvage") { return false; }
  if (contract.destination != map_.here) { return false; }
  if (!contract.lotId.empty()) {
    bool carried = false;
    for (const GoodsLot& lot : goods_) { if (lot.contractId == contract.id) { carried = true; } }
    if (!carried) { return false; }
  }
  if (!contract.passengerId.empty()) {
    bool carried = false;
    for (const Passenger& passenger : state_.passengers) {
      if (passenger.id == contract.passengerId) { carried = true; }
    }
    if (!carried) { return false; }
  }
  return true;
}

bool Simulation::FinalizeTrade(const std::string& contractId) {
  TradeContract* contract = nullptr;
  for (TradeContract& entry : state_.trade.contracts) {
    if (entry.id == contractId) { contract = &entry; }
  }
  if (contract == nullptr || !TradeReadyHere(*contract)) { return false; }
  const std::string title = contract->title;
  const double reward = contract->reward;
  goods_.erase(std::remove_if(goods_.begin(), goods_.end(),
                              [&](const GoodsLot& lot) { return lot.contractId == contractId; }),
               goods_.end());
  state_.passengers.erase(
      std::remove_if(state_.passengers.begin(), state_.passengers.end(),
                     [&](const Passenger& passenger) { return passenger.id == contract->passengerId; }),
      state_.passengers.end());
  contract->status = "complete";
  state_.trade.completed += 1;
  state_.shiplets += reward;
  PerformAction("Trade finalized - " + title);
  LogLine("Unloaded and cashed in at " + NameOf(*this, map_.here) + ": " + title + " paid " +
          js::NumToString(reward) + " sh.");
  SimRemember("Work", "The crew unloaded and finalized a trade contract: " + title + ".");
  return true;
}

bool Simulation::LotSatisfiesFreelance(const GoodsLot& lot, const TradeContract& contract) const {
  return contract.status == "active" && contract.kind == "freelance" &&
         contract.destination == map_.here && contract.goodId == lot.goodId &&
         lot.marketPurchase && lot.originPort == contract.origin &&
         lot.boughtAt >= contract.acceptedAt && contract.fulfilled < contract.quantity;
}

void Simulation::TradeScanProgress(const std::string& bodyId) {
  for (TradeContract& contract : state_.trade.contracts) {
    if (contract.status == "active" && contract.legacyGoal == "scan" && contract.destination == bodyId) {
      contract.status = "ready";
      contract.legacyReady = true;
      LogLine("Carried-over agreement ready to hand in at a station: " + contract.title + ".");
    }
  }
}

void Simulation::TradeSalvageProgress() {
  for (TradeContract& contract : state_.trade.contracts) {
    if (contract.status == "active" && contract.legacyGoal == "salvage") {
      contract.legacyProgress += 1;
      if (contract.legacyProgress >= 2) {
        contract.status = "ready";
        contract.legacyReady = true;
        LogLine("Carried-over agreement ready to hand in at a station: " + contract.title + ".");
      }
    }
  }
}

}  // namespace ship

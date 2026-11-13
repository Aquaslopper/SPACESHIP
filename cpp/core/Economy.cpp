// ============================================================================
//  Economy.cpp — ports, cargo, contracts and the commission board.
//  ---------------------------------------------------------------------------
//  Every transaction is one action, and every transaction is one function that
//  the client calls after the player has already committed: the money moves, the
//  action is spent, and the log says what happened — in that order, exactly as
//  commitChange() does it in the reference.
//
//  Prices are a multiplier per commodity KIND, per port. A lot's worth is its
//  value; what a port pays for it is the port's opinion.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

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

double Simulation::CargoCapacity() const { return cargo_.capacity; }

double Simulation::GoodCounterPrice(const std::string& goodId) const {
  const Good* good = nullptr;
  for (const Good& candidate : catalog_.goods) {
    if (candidate.id == goodId) { good = &candidate; }
  }
  if (good == nullptr) { return 0.0; }
  GoodsLot probe;
  probe.id = good->id;
  probe.name = good->name;
  probe.kind = good->kind;
  probe.vol = good->vol;
  probe.value = good->value;
  probe.risk = good->risk;
  return js::Ceil(LotPrice(probe) * 1.2);
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
  FreeCommit("Bought " + good->name);
  return true;
}

bool Simulation::SellGood(const std::string& lotId) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }

  for (std::size_t index = 0; index < goods_.size(); ++index) {
    if (goods_[index].id != lotId) { continue; }
    if (!goods_[index].contractId.empty()) { return false; }  // contract cargo is not for sale
    const double price = LotPrice(goods_[index]);
    const std::string name = goods_[index].name;
    ClearStowLot(goods_[index].id);
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
    ClearStowLot(goods_[index].id);
    goods_.erase(goods_.begin() + static_cast<std::ptrdiff_t>(index));
    if (contract.compare(0, 6, "trade:") == 0) {
      // A trade consignment thrown out of the hold takes its run with it: the
      // same consignment cannot be signed for twice.
      const std::string key = contract.substr(6);
      state_.spentTrades[key] = true;
      state_.freelanceRejects.erase(key);
      state_.trades.erase(std::remove_if(state_.trades.begin(), state_.trades.end(),
                                         [&](const TradeContract& trade) {
                                           return TradeKey(trade.mode, trade.to, trade.good) == key;
                                         }),
                          state_.trades.end());
    } else if (!contract.empty()) {
      state_.contracts[contract] = "failed";
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

// ---------------------------------------------------------------------------
// Contracts
// ---------------------------------------------------------------------------

void Simulation::ContractProgress(const std::string& goal, const std::string& target) {
  for (const Contract& contract : catalog_.contracts) {
    auto state = state_.contracts.find(contract.id);
    if (state == state_.contracts.end() || state->second != "active") { continue; }
    if (contract.target != target) { continue; }
    const bool visitCoversFreight = goal == "visit" && (contract.goal == "freight" || contract.goal == "passenger");
    if (contract.goal != goal && !visitCoversFreight) { continue; }

    if (contract.goal == "freight") {
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
    LogLine("Contract ready to collect: " + contract.title + ".");
  }
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

  const std::vector<CandidateDefinition> hands = HandsHere();
  const CandidateDefinition* candidate = nullptr;
  for (const CandidateDefinition& entry : catalog_.candidates) {
    if (entry.crew.id == candidateId) { candidate = &entry; }
  }
  for (const CandidateDefinition& entry : hands) {
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

// ---------------------------------------------------------------------------
// Trade contracts
// ---------------------------------------------------------------------------
// A station's board is drawn, not, and it is drawn out of the same
// chart the station's starmap prints: a body's role is what that page already
// says about it -- the planet's sub-type, the moon's surface line, the kind line
// an world carries -- and a run carries to it one of the commodities it
// wants. A system's board is one seeded draw, the same for every berth in it;
// each berth shows the runs that start where the player is standing.
//
// Nothing about the draw is stored. The seed is the system id and the epoch, and
// the epoch is the action clock in blocks of one hundred, so the board is
// reproducible from the chart alone and refreshes itself as the voyage goes on.
// What a contract promised is a different matter: once an epoch rolls over the
// draw no longer knows the run exists, so the record keeps its own numbers.

namespace {
constexpr double kTradeEpoch = 100.0;  // actions between two draws of a board
constexpr int kTradeMinPerMode = 10;   // minimum delivery and freelance offers per station
constexpr double kTradeHold = 20.0;    // the cubic metres a voyage starts with
constexpr double kTradeFloor = 40.0;   // a run that pays less than this is no run
constexpr double kTradeValue = 1200.0;  // the goods value a single run may commit

// The chart's own generator, so a board is a pure function of its seed.
struct TradeRng {
  std::uint64_t state;
  explicit TradeRng(std::uint64_t seed) : state(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

  double Next() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    const std::uint64_t out = state * 2685821657736338717ULL;
    return static_cast<double>(out >> 11) * (1.0 / 9007199254740992.0);
  }

  int Below(int n) { return n <= 0 ? 0 : static_cast<int>(Next() * static_cast<double>(n)) % n; }
};

std::uint64_t TradeHash(const std::string& text) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (const char c : text) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

std::string BodyDetail(const Body& body, const std::string& key) {
  for (const auto& detail : body.details) {
    if (detail.first == key) { return detail.second; }
  }
  return std::string();
}

// What a body wants, and what it pays over the market value for it. The four
// planet sub-types are the local-chart pass's own; belts and derelict sites have
// an economy of their own. A body that wants nothing is not a destination.
struct Want {
  const char* good;
  double pays;
};

const std::vector<Want>& WantsOf(const std::string& role) {
  static const std::vector<Want> kRock = {
      {"water", 1.60}, {"greens", 1.55}, {"medicine", 1.40}, {"parts", 1.45}};
  static const std::vector<Want> kOcean = {
      {"parts", 1.60}, {"medicine", 1.45}, {"amps", 1.50}, {"ore", 1.50}};
  static const std::vector<Want> kIce = {
      {"greens", 1.70}, {"medicine", 1.60}, {"parts", 1.40}, {"ore", 1.55}};
  static const std::vector<Want> kGas = {
      {"greens", 1.75}, {"medicine", 1.65}, {"parts", 1.50}, {"ore", 1.60}};
  static const std::vector<Want> kBelt = {
      {"water", 1.60}, {"greens", 1.60}, {"medicine", 1.40}, {"parts", 1.30}};
  static const std::vector<Want> kSite = {
      {"water", 1.50}, {"medicine", 1.40}, {"parts", 1.30}, {"relics", 1.65}};
  static const std::vector<Want> kNone;
  if (role == "rock") { return kRock; }
  if (role == "ocean") { return kOcean; }
  if (role == "ice") { return kIce; }
  if (role == "gas") { return kGas; }
  if (role == "belt") { return kBelt; }
  if (role == "site") { return kSite; }
  return kNone;
}

// A body's economy, read off the same fields the starmap page prints.
std::string TradeRole(const Body& body) {
  if (body.type == "belt") { return "belt"; }
  if (body.type == "site") { return "site"; }
  if (body.type == "moon") {
    // The surface line the local-chart pass writes decides what a moon is worth.
    const std::string surface = BodyDetail(body, "Surface");
    if (surface.find("Icy") != std::string::npos) { return "ice"; }
    if (surface.find("basalt") != std::string::npos) { return "rock"; }
    return std::string();
  }
  if (body.type != "planet") { return std::string(); }  // stars, stations, systems
  if (!body.subtype.empty()) { return body.subtype; }   // rock / ocean / ice / gas
  // An world carries its nature in the kind line instead.
  if (body.klass.find("ocean") != std::string::npos) { return "ocean"; }
  if (body.klass.find("ice") != std::string::npos) { return "ice"; }
  if (body.klass.find("gas") != std::string::npos) { return "gas"; }
  return "rock";  // "Charted world": an ordinary terrestrial world
}

// A run to the outer system is the long one, and it pays for the distance.
double TradeBandBonus(const Body& body) {
  const std::string band = BodyDetail(body, "Orbital band");
  if (band.find("Outer") != std::string::npos) { return 0.10; }
  if (band.find("middle") != std::string::npos) { return 0.05; }
  return 0.0;
}

const Good* MarketGood(const Catalog& catalog, const std::string& id) {
  for (const Good& good : catalog.goods) {
    if (good.id == id) { return &good; }
  }
  return nullptr;
}

// What carrying a run costs, priced off the chart: the leg between the berth the
// run starts at and the body that receives it. A close destination is nearly
// free; the far side of the system is not.
double TradeFreight(const Body& from, const Body& to) {
  const double dist = std::hypot(to.x - from.x, to.y - from.y);
  return js::Round(4.0 + dist * 0.25);
}

// What the destination pays for a commodity, over the market value: the same
// table the board drew the run from, so an offer and its settlement cannot
// disagree. A body that wants nothing of it pays the market value and no more.
double TradePays(const Body& destination, const std::string& goodId) {
  for (const Want& want : WantsOf(TradeRole(destination))) {
    if (goodId == want.good) { return want.pays + TradeBandBonus(destination); }
  }
  return 1.0;
}
}  // namespace

bool Simulation::TradeAccepted(const std::string& mode, const std::string& to,
                               const std::string& good) const {
  for (const TradeContract& trade : state_.trades) {
    if (trade.mode == mode && trade.to == to && trade.good == good) { return true; }
  }
  return false;
}

std::vector<TradeRun> Simulation::TradeBoard(const std::string& portId) const {
  std::vector<TradeRun> board;
  const Port* port = catalog_.FindPort(portId);
  const Body* berth = catalog_.FindBody(portId);
  if (port == nullptr || berth == nullptr) { return board; }
  const std::string systemId = catalog_.BodySystem(*berth);

  std::vector<const Body*> destinations;
  for (const std::size_t index : catalog_.BodiesInSystem(systemId)) {
    const Body& body = catalog_.bodies[index];
    if (!WantsOf(TradeRole(body)).empty()) { destinations.push_back(&body); }
  }

  const long long epoch = static_cast<long long>(std::floor(ActionCount() / kTradeEpoch));

  auto drawMode = [&](const std::string& modeName, bool isDelivery) {
    TradeRng rng(TradeHash(systemId + "#" + portId + "#trade#" + std::to_string(epoch) + "#" +
                           modeName));
    const int wanted = kTradeMinPerMode + rng.Below(3);  // 10-12 offers per mode

    std::vector<TradeRun> tier1;
    std::vector<TradeRun> tier2;
    std::vector<TradeRun> tier3;

    for (const Body* to : destinations) {
      for (const Want& want : WantsOf(TradeRole(*to))) {
        const Good* good = MarketGood(catalog_, want.good);
        if (good == nullptr) { continue; }
        if (Truthy(state_.spentTrades, TradeKey(modeName, to->id, good->id))) { continue; }

        const int byVolume =
            static_cast<int>(kTradeHold / std::max(1.0, static_cast<double>(good->vol)));
        const int byValue = static_cast<int>(kTradeValue / std::max(1.0, good->value));
        const int maxCrates = std::clamp(std::min(byVolume, byValue), 1, 6);
        const double pays = want.pays + TradeBandBonus(*to);
        const double freight = TradeFreight(*berth, *to);

        auto buildRun = [&](int crates, double fee) {
          TradeRun run;
          run.mode = modeName;
          run.from = port->id;
          run.to = to->id;
          run.good = good->id;
          run.crates = crates;
          run.fee = fee;
          run.freight = freight;
          return run;
        };

        if (isDelivery) {
          auto feeFor = [&](int crates) {
            double fee = js::Round(good->value * crates * (0.22 + 0.5 * (pays - 1.2)));
            if (good->risk) { fee = js::Round(fee * 1.35); }
            return fee;
          };
          std::vector<int> strictCrates;
          for (int c = 1; c <= maxCrates; ++c) {
            if (feeFor(c) >= kTradeFloor) { strictCrates.push_back(c); }
          }
          if (!strictCrates.empty()) {
            const int c = strictCrates[static_cast<std::size_t>(
                rng.Below(static_cast<int>(strictCrates.size())))];
            tier1.push_back(buildRun(c, feeFor(c)));
          } else if (feeFor(maxCrates) > 0.0) {
            tier2.push_back(buildRun(maxCrates, feeFor(maxCrates)));
          }
        } else {
          const auto it = port->prices.find(good->kind);
          const double factor = (it == port->prices.end() ? 1.0 : it->second);
          const double buy = factor * 1.2;  // the counter's markup
          const double rate = pays - buy;
          const double counterCost = js::Ceil(js::Round(good->value * factor) * 1.2);
          auto feeFor = [&](int crates) {
            double fee = js::Round(good->value * crates * std::min(rate, 1.0));
            if (good->risk) { fee = js::Round(fee * 1.35); }
            return fee;
          };

          if (rate >= 0.12) {
            std::vector<int> strictCrates;
            for (int c = 1; c <= maxCrates; ++c) {
              if (feeFor(c) >= kTradeFloor) { strictCrates.push_back(c); }
            }
            if (!strictCrates.empty()) {
              const int c = strictCrates[static_cast<std::size_t>(
                  rng.Below(static_cast<int>(strictCrates.size())))];
              tier1.push_back(buildRun(c, feeFor(c)));
            } else {
              const double f = std::max(1.0, feeFor(maxCrates));
              tier2.push_back(buildRun(maxCrates, f));
            }
          } else if (rate >= 0.02) {
            const double f = std::max(1.0, feeFor(maxCrates));
            tier2.push_back(buildRun(maxCrates, f));
          } else {
            const double payout = js::Round(good->value * maxCrates * pays + freight);
            const double netProfit = payout - counterCost * maxCrates;
            if (netProfit >= 10.0) {
              tier3.push_back(buildRun(maxCrates, netProfit));
            }
          }
        }
      }
    }

    auto shuffleRuns = [&](std::vector<TradeRun>& pool) {
      for (int idx = static_cast<int>(pool.size()) - 1; idx > 0; --idx) {
        const int j = rng.Below(idx + 1);
        std::swap(pool[static_cast<std::size_t>(idx)], pool[static_cast<std::size_t>(j)]);
      }
    };
    shuffleRuns(tier1);
    shuffleRuns(tier2);
    shuffleRuns(tier3);

    std::set<std::string> usedDestinations;
    std::set<std::string> usedPairs;
    int picked = 0;

    auto takeFrom = [&](const std::vector<TradeRun>& pool, bool uniqueDestOnly) {
      for (const TradeRun& run : pool) {
        if (picked >= wanted) { break; }
        if (uniqueDestOnly && usedDestinations.count(run.to) != 0) { continue; }
        if (!usedPairs.insert(run.to + "|" + run.good).second) { continue; }
        usedDestinations.insert(run.to);
        board.push_back(run);
        ++picked;
      }
    };

    takeFrom(tier1, true);
    takeFrom(tier1, false);
    takeFrom(tier2, true);
    takeFrom(tier2, false);
    takeFrom(tier3, true);
    takeFrom(tier3, false);
  };

  drawMode("delivery", true);
  drawMode("freelance", false);

  // A run already taken keeps its place: the player is standing on the same
  // board, and the record -- not the draw -- is what it promised.
  for (TradeRun& run : board) {
    for (const TradeContract& trade : state_.trades) {
      if (trade.from != portId || trade.mode != run.mode || trade.to != run.to ||
          trade.good != run.good) {
        continue;
      }
      run.accepted = true;
      run.crates = trade.crates;
      run.fee = trade.fee;
      run.freight = trade.freight;
      break;
    }
  }
  // A contract from an earlier epoch is no longer part of any draw, and it still
  // stands: it goes on the list, greyed, with the numbers it was signed for.
  for (const TradeContract& trade : state_.trades) {
    if (trade.from != portId) { continue; }
    if (trade.mode != "delivery" && trade.mode != "freelance") { continue; }
    const bool listed = std::any_of(board.begin(), board.end(), [&](const TradeRun& run) {
      return run.mode == trade.mode && run.to == trade.to && run.good == trade.good;
    });
    if (listed) { continue; }
    TradeRun run;
    run.mode = trade.mode;
    run.from = trade.from;
    run.to = trade.to;
    run.good = trade.good;
    run.crates = trade.crates;
    run.fee = trade.fee;
    run.freight = trade.freight;
    run.accepted = true;
    board.push_back(std::move(run));
  }
  return board;
}

bool Simulation::AcceptTrade(const std::string& mode, const std::string& to,
                             const std::string& good) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr) { return false; }
  const Port* here = HerePort();

  TradeContract trade;
  trade.mode = mode;
  trade.from = map_.here;
  trade.to = to;
  trade.good = good;

  if (mode == "station") {
    if (to.empty() || to == map_.here) { return false; }
    const Port* destination = catalog_.FindPort(to);
    if (destination == nullptr) { return false; }
    // Only a station the player has actually stood in can carry the other end of
    // a station-to-station trade.
    if (!Truthy(state_.visitedPorts, to)) { return false; }
    if (TradeAccepted(mode, to, good)) { return false; }
    trade.label = here->name + " - " + destination->name;
    const Body* hereBody = catalog_.FindBody(map_.here);
    const Body* destBody = catalog_.FindBody(to);
    const Body* hereSys =
        hereBody == nullptr ? nullptr : catalog_.FindSystem(catalog_.BodySystem(*hereBody));
    const Body* destSys =
        destBody == nullptr ? nullptr : catalog_.FindSystem(catalog_.BodySystem(*destBody));
    if (hereSys != nullptr && destSys != nullptr && hereSys->id != destSys->id) {
      const double dist = std::hypot(destSys->x - hereSys->x, destSys->y - hereSys->y);
      trade.fee = js::Round(std::max(160.0, SurveyWorth("station") + 70.0 + dist * 0.45));
    } else if (hereBody != nullptr && destBody != nullptr) {
      const double dist = std::hypot(destBody->x - hereBody->x, destBody->y - hereBody->y);
      trade.fee = js::Round(SurveyWorth("station") + 30.0 + dist * 0.25);
    } else {
      trade.fee = SurveyWorth("station");
    }
  } else if (mode == "delivery" || mode == "freelance") {
    // The run has to be on the board this station is showing right now, and the
    // numbers signed are the numbers on it: the draw and the signature cannot
    // disagree.
    const std::vector<TradeRun> board = TradeBoard(map_.here);
    const TradeRun* offered = nullptr;
    for (const TradeRun& run : board) {
      if (run.mode == mode && run.to == to && run.good == good) { offered = &run; }
    }
    if (offered == nullptr || offered->accepted) { return false; }
    const Body* destination = catalog_.FindBody(to);
    const Good* commodity = MarketGood(catalog_, good);
    if (destination == nullptr || commodity == nullptr) { return false; }
    trade.crates = offered->crates;
    trade.fee = offered->fee;
    trade.freight = offered->freight;
    trade.label = destination->name + " - " + js::NumToString(trade.crates) +
                  " crates of " + commodity->name;
    // A delivery is loaded at the berth and travels in the hold, bound to this
    // contract: it cannot be sold anywhere but the destination. A freelance run
    // is the player's own purchase, so nothing is loaded here.
    if (mode == "delivery") {
      const double volume = static_cast<double>(commodity->vol) * trade.crates;
      if (CargoUsed() + CargoReserved() + volume > cargo_.capacity) { return false; }
      state_.lotSerial += 1;
      GoodsLot lot;
      lot.id = "lot-" + js::NumToString(state_.lotSerial);
      lot.name = commodity->name;
      lot.kind = commodity->kind;
      lot.vol = volume;
      lot.value = commodity->value * trade.crates;
      lot.risk = commodity->risk;
      lot.contractId = "trade:" + TradeKey(mode, to, good);
      {
        const std::string systemId = catalog_.BodySystem(*destination);
        if (!systemId.empty() &&
            state_.systemSerials.find(systemId) == state_.systemSerials.end()) {
          state_.systemSerials[systemId] = state_.nextSystemSerial++;
        }
        const auto serial = state_.systemSerials.find(systemId);
        const int tag = serial == state_.systemSerials.end() ? 0 : serial->second;
        lot.triggerId = "#" + std::to_string(tag) + ":" + lot.contractId;
      }
      goods_.push_back(std::move(lot));
    }
  } else {
    return false;
  }

  state_.trades.push_back(std::move(trade));
  PerformAction("Trade contract accepted");
  return true;
}

// ---------------------------------------------------------------------------
// The cargo hall
// ---------------------------------------------------------------------------
// Three things live here: stowing a lot, which is the loading work and the only
// place where the click count moves the clock on its own; the trade that can be
// settled where the ship is standing; and what that settlement pays.
//
// A run is settled at its destination and nowhere else. The destination is the
// body the board named, so arriving is enough: no berth is needed, and a docked
// station is only required for the station-to-station runs, whose far end is a
// berth by definition.

std::vector<TradeContract> Simulation::TradesHere() const {
  std::vector<TradeContract> here;
  for (const TradeContract& trade : state_.trades) {
    if (trade.to != map_.here) { continue; }
    if (trade.mode == "station" && !state_.docked) { continue; }
    here.push_back(trade);
  }
  return here;
}

// What the destination pays: the market there, priced by the same table the
// board drew the run from, plus the freight the leg cost. A delivery is the
// station's own consignment, so it pays its flat fee; a survey run likewise.
double Simulation::TradePayout(const TradeContract& trade) const {
  if (trade.mode == "delivery" || trade.mode == "station") { return trade.fee; }
  const Body* destination = catalog_.FindBody(trade.to);
  const Good* good = MarketGood(catalog_, trade.good);
  if (destination == nullptr || good == nullptr) { return trade.fee; }
  const double market = good->value * trade.crates * TradePays(*destination, trade.good);
  return js::Round(market + trade.freight);
}

double Simulation::TradeOfferPayout(const TradeContract& trade, int priceTier) const {
  const double base = TradePayout(trade);
  if (trade.mode != "freelance") { return base; }
  if (priceTier == 1) { return js::Round(base * 1.10); }
  if (priceTier == 2) { return js::Round(base * 1.20); }
  return base;
}

int Simulation::FreelanceRejectMask(const TradeContract& trade) const {
  const auto it = state_.freelanceRejects.find(TradeKey(trade.mode, trade.to, trade.good));
  return it == state_.freelanceRejects.end() ? 0 : it->second;
}

bool Simulation::HasFreelanceGoods(const TradeContract& trade) const {
  if (trade.mode != "freelance") { return false; }
  const Good* commodity = MarketGood(catalog_, trade.good);
  if (commodity == nullptr) { return false; }
  double aboard = 0.0;
  for (const GoodsLot& lot : goods_) {
    if (lot.name == commodity->name && lot.contractId.empty()) { aboard += lot.vol; }
  }
  return aboard + 1e-9 >= static_cast<double>(commodity->vol) * trade.crates;
}

bool Simulation::SettleTrade(const std::string& mode, const std::string& to,
                             const std::string& good, int priceTier) {
  if (!state_.gameOver.empty()) { return false; }
  if (to != map_.here) { return false; }  // the run ends where it points
  if (mode == "station" && !state_.docked) { return false; }
  if (priceTier < 0 || priceTier > 2) { return false; }

  std::size_t index = state_.trades.size();
  for (std::size_t i = 0; i < state_.trades.size(); ++i) {
    const TradeContract& trade = state_.trades[i];
    if (trade.mode == mode && trade.to == to && trade.good == good) { index = i; }
  }
  if (index == state_.trades.size()) { return false; }
  const TradeContract trade = state_.trades[index];
  const std::string tradeKey = TradeKey(mode, to, good);
  const Body* endedAt = FindBodyById(to);
  const std::string where = endedAt == nullptr ? to : endedAt->name;

  if (mode == "delivery") {
    // The consignment leaves the hold and goes over the counter.
    const std::string key = "trade:" + tradeKey;
    const bool carried = std::any_of(goods_.begin(), goods_.end(),
                                     [&](const GoodsLot& lot) { return lot.contractId == key; });
    if (!carried) { return false; }  // nothing to hand over
    for (const GoodsLot& lot : goods_) {
      if (lot.contractId == key) { ClearStowLot(lot.id); }
    }
    goods_.erase(std::remove_if(goods_.begin(), goods_.end(),
                                [&](const GoodsLot& lot) { return lot.contractId == key; }),
                 goods_.end());
  } else if (mode == "freelance") {
    // The player's own lots are the consignment: the run needs the goods aboard.
    const Good* commodity = MarketGood(catalog_, trade.good);
    if (commodity == nullptr) { return false; }
    if (!HasFreelanceGoods(trade)) { return false; }
    if (priceTier > 0) {
      const int bit = 1 << (priceTier - 1);
      const int mask = FreelanceRejectMask(trade);
      if ((mask & bit) != 0) { return false; }
      const int roll = static_cast<int>(NextCargoRng() % 100ULL);
      const int odds = priceTier == 1 ? 60 : 35;
      if (roll >= odds) {
        state_.freelanceRejects[tradeKey] = mask | bit;
        PerformAction("Buyer at " + where + " declined +" +
                      std::string(priceTier == 1 ? "10" : "20") + "% on " + commodity->name);
        return true;
      }
    }
    double owed = static_cast<double>(commodity->vol) * trade.crates;
    for (std::size_t i = 0; i < goods_.size() && owed > 1e-9;) {
      GoodsLot& lot = goods_[i];
      if (lot.name != commodity->name || !lot.contractId.empty()) { ++i; continue; }
      if (lot.vol <= owed + 1e-9) {
        owed -= lot.vol;
        ClearStowLot(lot.id);
        goods_.erase(goods_.begin() + static_cast<std::ptrdiff_t>(i));
        continue;
      }
      // A part lot: keep the remainder aboard, sell what the run asked for.
      lot.vol -= owed;
      lot.value = lot.value * (lot.vol / (lot.vol + owed));
      owed = 0.0;
    }
  }

  state_.shiplets += TradeOfferPayout(trade, priceTier);
  state_.freelanceRejects.erase(tradeKey);
  state_.spentTrades[tradeKey] = true;
  state_.trades.erase(state_.trades.begin() + static_cast<std::ptrdiff_t>(index));
  PerformAction("Settled a " + std::string(mode == "station" ? "station trade" : mode) + " at " +
                where);
  return true;
}

std::uint64_t Simulation::NextCargoRng() {
  if (stowRng_ == 0ULL) { stowRng_ = 0x9E3779B97F4A7C15ULL; }
  stowRng_ ^= static_cast<std::uint64_t>(actionCounter_ + 1.0) * 0xBF58476D1CE4E5B9ULL;
  if (stowRng_ == 0ULL) { stowRng_ = 0x9E3779B97F4A7C15ULL; }
  stowRng_ ^= stowRng_ >> 12;
  stowRng_ ^= stowRng_ << 25;
  stowRng_ ^= stowRng_ >> 27;
  return stowRng_ * 2685821657736338717ULL;
}

int Simulation::MixDraw() {
  return 5 + static_cast<int>((NextCargoRng() >> 11) % 6ULL);  // 5 to 10 clicks
}

void Simulation::ClearStowLot(const std::string& lotId) {
  stowed_.erase(lotId);
  stowOrder_.erase(std::remove(stowOrder_.begin(), stowOrder_.end(), lotId), stowOrder_.end());
}

bool Simulation::CargoStowed(const std::string& lotId) const {
  return Truthy(stowed_, lotId);
}

int Simulation::CargoSortOrder(const std::string& lotId) const {
  for (std::size_t i = 0; i < stowOrder_.size(); ++i) {
    if (stowOrder_[i] == lotId) { return static_cast<int>(i + 1); }
  }
  return 0;
}

int Simulation::CargoMixEpoch(const std::string& lotId) const {
  const auto it = cargoMixGen_.find(lotId);
  return it == cargoMixGen_.end() ? 0 : it->second;
}

void Simulation::StepCargoMix() {
  std::vector<std::string> sorted;
  for (const GoodsLot& lot : goods_) {
    if (lot.id != justStowedLot_ && CargoStowed(lot.id)) {
      sorted.push_back(lot.id);
    }
  }
  if (sorted.empty()) {
    mixClicks_ = 0;
    return;
  }
  if (mixThreshold_ <= 0) { mixThreshold_ = MixDraw(); }
  mixClicks_ += 1;
  if (mixClicks_ < mixThreshold_) { return; }
  mixClicks_ = 0;
  mixThreshold_ = MixDraw();

  // Gradually shift one or two sorted crates out of order so the hold stays
  // alive every 5-10 clicks without resetting everything at once.
  const std::size_t unplace = sorted.size() >= 4 ? 2 : 1;
  for (std::size_t n = 0; n < unplace && !sorted.empty(); ++n) {
    const std::size_t pick = static_cast<std::size_t>(NextCargoRng() % sorted.size());
    const std::string pickedId = sorted[pick];
    ClearStowLot(pickedId);
    cargoMixGen_[pickedId] += 1;
    sorted.erase(sorted.begin() + static_cast<std::ptrdiff_t>(pick));
  }
}

bool Simulation::StowCargo(const std::string& lotId) {
  if (!state_.gameOver.empty()) { return false; }
  std::string lotName;
  for (const GoodsLot& lot : goods_) {
    if (lot.id == lotId) { lotName = lot.name; break; }
  }
  if (lotName.empty() || CargoStowed(lotId)) { return false; }
  stowed_[lotId] = true;
  if (std::find(stowOrder_.begin(), stowOrder_.end(), lotId) == stowOrder_.end()) {
    stowOrder_.push_back(lotId);
  }
  justStowedLot_ = lotId;
  PerformAction("Sorted " + lotName + " in the cargo hold");
  justStowedLot_.clear();
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
  if (upgradeId == "galley" && !Truthy(state_.upgrades, "galley") && state_.shiplets >= 400) {
    state_.shiplets -= 400;
    state_.upgrades["galley"] = true;
    // The ceiling is what the cook fills to, so a bigger rack is a deeper buffer
    // between the crew and the starve line, not a faster kitchen.
    mess_.capacity = 34;
    FreeCommit("Galley rack enlarged");
    return true;
  }
  if (upgradeId == "filter" && !Truthy(state_.upgrades, "filter") && state_.shiplets >= 600) {
    state_.shiplets -= 600;
    state_.upgrades["filter"] = true;
    // `lifeMax` is the length of one charge in clicks, and every reader of it -
    // the bank, the O2 screen, the save - reads it from here.
    rules_.life.lifeMax = 300;
    FreeCommit("Filter cores re-sleeved");
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
    const std::string curSysId = CurrentSystemId();
    std::vector<const Body*> all;
    std::vector<const Body*> local;
    std::vector<const Body*> fresh;

    if (kind == "visit") {
      // Iterate ports directly rather than scanning every body in the galaxy.
      all.reserve(catalog_.ports.size());
      for (const Port& port : catalog_.ports) {
        if (port.id == map_.here) { continue; }
        const Body* body = catalog_.FindBody(port.id);
        if (body == nullptr) { continue; }
        const Body* system = catalog_.FindSystem(catalog_.BodySystem(*body));
        if (system == nullptr) { continue; }
        all.push_back(body);
        if (here != nullptr && system->region == here->region) {
          local.push_back(body);
        }
      }
    } else {
      // Scan regional systems first via the O(1) region/system index; only scan
      // the full galaxy when the local region has no eligible targets.
      if (here != nullptr) {
        const auto& regSystems = catalog_.SystemsInRegion(here->region);
        for (std::size_t sysIdx : regSystems) {
          const Body& sys = catalog_.systems[sysIdx];
          if (sys.id == curSysId) { continue; }
          for (std::size_t bodyIdx : catalog_.BodiesInSystem(sys.id)) {
            const Body& body = catalog_.bodies[bodyIdx];
            if (body.type == "origin") { continue; }
            local.push_back(&body);
            auto sc = state_.scanned.find(body.id);
            if (sc == state_.scanned.end() || !sc->second) { fresh.push_back(&body); }
          }
        }
      }
      if (fresh.empty() && local.empty()) {
        for (const Body& body : catalog_.bodies) {
          const std::string systemId = catalog_.BodySystem(body);
          if (systemId == curSysId || body.type == "origin") { continue; }
          if (catalog_.FindSystem(systemId) != nullptr) { all.push_back(&body); }
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
          const Body* system = catalog_.FindSystem(catalog_.BodySystem(*body));
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

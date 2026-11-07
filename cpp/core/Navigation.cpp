// ============================================================================
//  Navigation.cpp — the chart, the crossing and the sensors.
//  ---------------------------------------------------------------------------
//  Looking is free. Every scale of the chart, every pan, every selection and
//  every readout of a body's name, class and distance costs nothing. Two things
//  cost: plotting a crossing (one action now, then the distance in actions spent
//  elsewhere) and asking the sensors for more than the chart already shows.
//
//  Distances are DERIVED, never authored. They are measured from MAP.here at the
//  moment of the question, which is why the ship can relocate on arrival without
//  invalidating a single route.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ship {

namespace {

std::string Plur(double n, const std::string& word) {
  return js::NumToString(n) + " " + word + (n == 1 ? "" : "s");
}

std::uint64_t StableMix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

std::string IdSlug(const std::string& input) {
  std::string result;
  bool dash = false;
  for (unsigned char c : input) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
      if (dash && !result.empty()) { result.push_back('-'); }
      dash = false;
      result.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    } else {
      dash = true;
    }
  }
  return result.empty() ? std::string("entity") : result;
}

constexpr std::uint64_t kDefaultSpaceMapSeed = 0x5350414345534849ULL;
constexpr std::size_t kGeneratedSystemsPerAuthoredSystem = 50;
constexpr std::size_t kPlanetsPerGeneratedSystem = 3;

// Convert the stable 53 most significant bits into [0, 1), without relying on
// a library PRNG whose sequence could change between standard-library versions.
double SpaceMapUnit(std::uint64_t seed, const std::string& domain,
                    std::uint64_t ordinal) {
  return static_cast<double>(SpaceMapRandomWord(seed, domain, ordinal) >> 11) /
         9007199254740992.0;
}

const SpaceMapNameRule* FindNameRule(const Catalog& catalog,
                                     const std::string& category) {
  for (const SpaceMapNameRule& rule : catalog.spaceMap.names) {
    if (rule.category == category) { return &rule; }
  }
  return nullptr;
}

std::string GeneratedName(const Catalog& catalog, std::uint64_t seed,
                          std::uint64_t ordinal, const std::string& category) {
  const SpaceMapNameRule* rule = FindNameRule(catalog, category);
  if (rule == nullptr) {
    throw CatalogError("native SpaceMap has no name rule for '" + category + "'");
  }
  return SpaceMapGeneratedName(seed, ordinal, *rule);
}

const BodyProfileDef* ChooseBodyProfile(const Catalog& catalog,
                                        const std::string& type,
                                        std::uint64_t seed,
                                        std::uint64_t ordinal) {
  std::vector<const BodyProfileDef*> profiles;
  std::vector<WeightedId> weights;
  for (const BodyProfileDef& profile : catalog.spaceMap.bodies) {
    if (profile.type != type || profile.weight == 0) { continue; }
    profiles.push_back(&profile);
    weights.push_back({profile.id, profile.weight});
  }
  const std::optional<std::size_t> selected =
      SpaceMapWeightedIndex(seed, "body-profile:" + type, ordinal, weights);
  if (!selected.has_value()) {
    throw CatalogError("native SpaceMap has no weighted body profile for '" + type + "'");
  }
  return profiles.at(*selected);
}

const ShipContactProfileDef* ChooseShipContact(const Catalog& catalog,
                                               std::uint64_t seed,
                                               std::uint64_t ordinal) {
  std::vector<const ShipContactProfileDef*> profiles;
  std::vector<WeightedId> weights;
  for (const ShipContactProfileDef& profile : catalog.spaceMap.shipContacts) {
    if (profile.weight == 0) { continue; }
    profiles.push_back(&profile);
    weights.push_back({profile.id, profile.weight});
  }
  const std::optional<std::size_t> selected =
      SpaceMapWeightedIndex(seed, "ship-contact", ordinal, weights);
  if (!selected.has_value()) {
    throw CatalogError("native SpaceMap has no weighted ship-contact profile");
  }
  return profiles.at(*selected);
}

std::pair<double, double> GeneratedPoint(
    const MapGeometry& map, std::uint64_t seed, const std::string& domain,
    std::uint64_t ordinal,
    const std::vector<std::pair<double, double>>& occupied,
    double minimumSpacing) {
  const double marginX = std::min(14.0, map.w * 0.04);
  const double marginY = std::min(10.0, map.h * 0.04);
  const double spanX = std::max(1.0, map.w - 2.0 * marginX);
  const double spanY = std::max(1.0, map.h - 2.0 * marginY);
  const double minimumSquared = minimumSpacing * minimumSpacing;
  std::pair<double, double> best{map.w * 0.5, map.h * 0.5};
  double bestNearestSquared = -1;

  for (std::uint64_t attempt = 0; attempt < 128; ++attempt) {
    const std::uint64_t counter = ordinal * 128 + attempt;
    const double x = marginX + SpaceMapUnit(seed, domain + ":x", counter) * spanX;
    const double y = marginY + SpaceMapUnit(seed, domain + ":y", counter) * spanY;
    double nearestSquared = std::numeric_limits<double>::max();
    for (const auto& point : occupied) {
      const double dx = x - point.first;
      const double dy = y - point.second;
      nearestSquared = std::min(nearestSquared, dx * dx + dy * dy);
    }
    if (nearestSquared >= minimumSquared) { return {x, y}; }
    if (nearestSquared > bestNearestSquared) {
      bestNearestSquared = nearestSquared;
      best = {x, y};
    }
  }
  return best;
}

class DisjointSets {
 public:
  explicit DisjointSets(const std::vector<std::string>& nodes) {
    for (const std::string& node : nodes) { parent_[node] = node; }
  }

  std::string Find(const std::string& node) {
    auto found = parent_.find(node);
    if (found == parent_.end()) { return {}; }
    if (found->second != node) { found->second = Find(found->second); }
    return found->second;
  }

  bool Unite(const std::string& left, const std::string& right) {
    const std::string leftRoot = Find(left);
    const std::string rightRoot = Find(right);
    if (leftRoot.empty() || rightRoot.empty() || leftRoot == rightRoot) { return false; }
    parent_[rightRoot] = leftRoot;
    return true;
  }

 private:
  std::map<std::string, std::string> parent_;
};

void EnsureConnected(const std::vector<std::string>& nodes,
                     const std::vector<Lane>& existing,
                     std::vector<Lane>& output) {
  if (nodes.size() < 2) { return; }
  const std::set<std::string> allowed(nodes.begin(), nodes.end());
  DisjointSets components(nodes);
  for (const Lane& lane : existing) {
    if (allowed.count(lane.from) != 0 && allowed.count(lane.to) != 0) {
      components.Unite(lane.from, lane.to);
    }
  }
  for (std::size_t i = 1; i < nodes.size(); ++i) {
    if (components.Unite(nodes.front(), nodes[i])) {
      output.push_back({nodes.front(), nodes[i]});
    }
  }
}

Body MakeProfileBody(const Catalog& catalog, std::uint64_t seed,
                     const std::string& idCategory, std::uint64_t idOrdinal,
                     std::uint64_t nameOrdinal, const std::string& systemId,
                     const BodyProfileDef& profile) {
  Body body;
  body.id = SpaceMapGeneratedId(catalog.spaceMap.rules.version, seed,
                                idCategory, idOrdinal);
  body.type = profile.type;
  body.tag = profile.tag;
  body.name = GeneratedName(catalog, seed, nameOrdinal, profile.nameRule);
  body.klass = profile.klass;
  body.system = systemId;
  body.subtype = profile.subtype;
  body.lootTable = profile.lootTable;
  body.details = profile.details;
  body.scan = profile.scan;
  body.scanLocked = profile.scanLocked;
  return body;
}

void EnsureBlueprintGoods(Catalog& catalog) {
  std::set<std::string> referenced;
  std::set<std::string> miniEligible;
  for (const StationProfileDef& profile : catalog.spaceMap.stations) {
    for (const std::string& id : profile.goodIds) {
      referenced.insert(id);
      if (profile.tier == "mini") { miniEligible.insert(id); }
    }
  }
  for (const LootTableDef& table : catalog.spaceMap.lootTables) {
    for (const LootEntryDef& entry : table.entries) {
      if (entry.kind == LootKind::Cargo && !entry.goodId.empty()) {
        referenced.insert(entry.goodId);
      }
    }
  }

  for (const std::string& id : referenced) {
    auto marketGood = std::find_if(catalog.goods.begin(), catalog.goods.end(),
        [&id](const Good& good) { return good.id == id; });
    if (marketGood == catalog.goods.end()) {
      const auto sourceGood = std::find_if(catalog.startingCargo.begin(),
          catalog.startingCargo.end(), [&id](const Good& good) { return good.id == id; });
      if (sourceGood == catalog.startingCargo.end()) {
        throw CatalogError("native SpaceMap references unavailable good '" + id + "'");
      }
      catalog.goods.push_back(*sourceGood);
      marketGood = catalog.goods.end() - 1;
    }
    if (miniEligible.count(id) != 0) { marketGood->miniStationEligible = true; }
  }
}

}  // namespace

std::uint64_t SpaceMapRandomWord(std::uint64_t seed, const std::string& domain,
                                std::uint64_t ordinal) {
  // FNV-1a the domain, then pass the mixed seed/counter through SplitMix64.
  // This makes each generation stream independent of call order.
  std::uint64_t domainHash = 14695981039346656037ULL;
  for (unsigned char c : domain) {
    domainHash ^= static_cast<std::uint64_t>(c);
    domainHash *= 1099511628211ULL;
  }
  const std::uint64_t counter = ordinal * 0x9e3779b97f4a7c15ULL;
  return StableMix(seed ^ domainHash ^ counter);
}

std::optional<std::size_t> SpaceMapWeightedIndex(
    std::uint64_t seed, const std::string& domain, std::uint64_t ordinal,
    const std::vector<WeightedId>& options) {
  std::uint64_t total = 0;
  for (const WeightedId& option : options) { total += option.weight; }
  if (total == 0) { return std::nullopt; }

  const std::uint64_t choice = SpaceMapRandomWord(seed, domain, ordinal) % total;
  std::uint64_t cursor = 0;
  for (std::size_t i = 0; i < options.size(); ++i) {
    cursor += options[i].weight;
    if (choice < cursor) { return i; }
  }
  return std::nullopt;  // defensive: unreachable unless weights overflow
}

std::string SpaceMapGeneratedId(std::uint32_t version, std::uint64_t seed,
                                const std::string& category, std::uint64_t ordinal) {
  std::ostringstream out;
  out << "gen-v" << version << '-' << IdSlug(category) << '-'
      << std::hex << std::setw(16) << std::setfill('0') << seed << '-'
      << std::dec << std::setw(8) << std::setfill('0') << ordinal;
  return out.str();
}

std::string SpaceMapGeneratedName(std::uint64_t seed, std::uint64_t ordinal,
                                  const SpaceMapNameRule& rule) {
  if (rule.prefixes.empty() || rule.suffixes.empty()) { return {}; }
  const std::uint64_t prefixIndex =
      SpaceMapRandomWord(seed, rule.category + ":prefix", ordinal) % rule.prefixes.size();
  const std::uint64_t suffixIndex =
      SpaceMapRandomWord(seed, rule.category + ":suffix", ordinal) % rule.suffixes.size();
  return rule.prefixes[static_cast<std::size_t>(prefixIndex)] + " " +
         rule.suffixes[static_cast<std::size_t>(suffixIndex)] + " " +
         std::to_string(ordinal + 1);
}

void Simulation::GenerateSpaceMap() {
  if (catalog_.spaceMapGenerated) { return; }
  if (catalog_.spaceMap.rules.version == 0) {
    throw CatalogError("cannot generate a SpaceMap without a positive blueprint version");
  }

  // The launch policy creates one generated region per authored region and
  // fifty generated systems per authored system, then applies a fixed local
  // system recipe. The requested 50x system multiplier is an explicit
  // population target; it is not inferred from the chart dimensions.
  const std::uint64_t seed = kDefaultSpaceMapSeed;
  const std::vector<Body> authoredRegions = catalog_.regions;
  const std::vector<Body> authoredSystems = catalog_.systems;
  const std::size_t generatedRegionCount = std::max<std::size_t>(1, authoredRegions.size());
  const std::size_t generatedSystemCount = std::max(
      generatedRegionCount,
      authoredSystems.size() * kGeneratedSystemsPerAuthoredSystem);

  EnsureBlueprintGoods(catalog_);

  std::vector<std::pair<double, double>> occupiedRegions;
  for (const Body& region : authoredRegions) {
    occupiedRegions.emplace_back(region.x, region.y);
  }
  std::vector<std::string> generatedRegionIds;
  std::map<std::string, std::string> regionCounterparts;
  for (std::size_t i = 0; i < generatedRegionCount; ++i) {
    Body region;
    region.id = SpaceMapGeneratedId(catalog_.spaceMap.rules.version, seed,
                                   "region", i);
    region.type = "region";
    region.tag = "region.generated";
    region.name = GeneratedName(catalog_, seed, i, "region");
    region.klass = "Procedural chart region";
    const auto point = GeneratedPoint(catalog_.map, seed, "region-placement", i,
                                      occupiedRegions, 38.0);
    region.x = point.first;
    region.y = point.second;
    occupiedRegions.push_back(point);
    region.cluster = 4 + static_cast<int>(SpaceMapRandomWord(seed, "region-cluster", i) % 12);
    region.details = {{"Chart status", "Generated"},
                      {"Navigation", "Connected galaxy-lane network"}};
    region.scan = {"Long-range observations resolve a navigable stellar region."};
    region.scanLocked = {"A closer survey could improve the local system count."};
    catalog_.regions.push_back(region);
    generatedRegionIds.push_back(region.id);
    if (i < authoredRegions.size()) {
      regionCounterparts[authoredRegions[i].id] = region.id;
    }
  }

  struct GeneratedSystemInfo {
    std::string id;
    std::string region;
    std::string name;
  };
  std::vector<GeneratedSystemInfo> generatedSystems;
  std::map<std::string, std::vector<std::string>> systemsByRegion;
  std::map<std::string, std::vector<std::pair<double, double>>> systemPoints;
  std::map<std::string, std::string> scanTargetBySystem;
  std::vector<std::string> normalStationIds;
  std::uint64_t starOrdinal = 0;
  std::uint64_t planetOrdinal = 0;
  std::uint64_t moonOrdinal = 0;
  std::uint64_t debrisOrdinal = 0;
  std::uint64_t derelictOrdinal = 0;
  std::uint64_t siteOrdinal = 0;
  std::uint64_t stationOrdinal = 0;
  std::uint64_t shipOrdinal = 0;
  std::uint64_t taskOrdinal = 0;

  for (std::size_t i = 0; i < generatedSystemCount; ++i) {
    std::string regionId;
    if (!authoredSystems.empty()) {
      const Body& templateSystem = authoredSystems[i % authoredSystems.size()];
      const auto counterpart = regionCounterparts.find(templateSystem.region);
      if (counterpart != regionCounterparts.end()) { regionId = counterpart->second; }
    }
    if (regionId.empty()) { regionId = generatedRegionIds[i % generatedRegionIds.size()]; }

    const std::string systemId = SpaceMapGeneratedId(
        catalog_.spaceMap.rules.version, seed, "system", i);
    const std::string systemName = GeneratedName(catalog_, seed, i, "system");
    const auto systemPoint = GeneratedPoint(catalog_.map, seed,
        "system-placement:" + regionId, i, systemPoints[regionId], 34.0);
    systemPoints[regionId].push_back(systemPoint);

    Body system;
    system.id = systemId;
    system.type = "system";
    system.tag = "system.generated";
    system.name = systemName;
    system.klass = "Generated navigable system";
    system.x = systemPoint.first;
    system.y = systemPoint.second;
    system.region = regionId;
    system.details = {{"Chart status", "Generated"},
                      {"Region", regionId},
                      {"Navigation", "Connected sector-lane network"}};
    system.scan = {"The local primary and its surrounding bodies are charted."};
    system.scanLocked = {"A local survey can resolve additional body detail."};

    std::vector<std::pair<double, double>> occupiedBodies;
    const BodyProfileDef* starProfile =
        ChooseBodyProfile(catalog_, "star", seed, i);
    Body star = MakeProfileBody(catalog_, seed, "star", starOrdinal,
                                starOrdinal, systemId, *starProfile);
    ++starOrdinal;
    star.x = catalog_.map.w * 0.5;
    star.y = catalog_.map.h * 0.5;
    occupiedBodies.emplace_back(star.x, star.y);
    system.entry = star.id;
    catalog_.bodies.push_back(star);

    for (std::size_t planetIndex = 0;
         planetIndex < kPlanetsPerGeneratedSystem; ++planetIndex) {
      const std::uint64_t selectionOrdinal =
          static_cast<std::uint64_t>(i * kPlanetsPerGeneratedSystem + planetIndex);
      const BodyProfileDef* planetProfile =
          ChooseBodyProfile(catalog_, "planet", seed, selectionOrdinal);
      const std::uint64_t thisPlanetOrdinal = planetOrdinal++;
      Body planet = MakeProfileBody(catalog_, seed, "planet", thisPlanetOrdinal,
                                    thisPlanetOrdinal, systemId, *planetProfile);
      const double phase = SpaceMapUnit(seed, "planet-orbit-phase", i) *
                           6.28318530717958647692;
      const double angle = phase + static_cast<double>(planetIndex) *
                           2.09439510239319549231;
      const double radius = 42.0 + static_cast<double>(planetIndex) * 34.0;
      planet.x = catalog_.map.w * 0.5 + std::cos(angle) * radius;
      planet.y = catalog_.map.h * 0.5 + std::sin(angle) * radius * 0.58;
      planet.x = std::max(6.0, std::min(catalog_.map.w - 6.0, planet.x));
      planet.y = std::max(6.0, std::min(catalog_.map.h - 6.0, planet.y));
      occupiedBodies.emplace_back(planet.x, planet.y);
      if (scanTargetBySystem.count(systemId) == 0) {
        scanTargetBySystem[systemId] = planet.id;
      }
      catalog_.bodies.push_back(planet);

      const double moonChance = std::min(planetProfile->moonChance,
                                         catalog_.spaceMap.rules.moonChance);
      if (SpaceMapUnit(seed, "moon-chance", selectionOrdinal) < moonChance) {
        const BodyProfileDef* moonProfile =
            ChooseBodyProfile(catalog_, "moon", seed, selectionOrdinal);
        const std::uint64_t thisMoonOrdinal = moonOrdinal++;
        Body moon = MakeProfileBody(catalog_, seed, "moon", thisMoonOrdinal,
                                    thisMoonOrdinal, systemId, *moonProfile);
        const double moonAngle = SpaceMapUnit(seed, "moon-orbit", selectionOrdinal) *
                                 6.28318530717958647692;
        const double moonRadius = 12.0 +
            SpaceMapUnit(seed, "moon-distance", selectionOrdinal) * 8.0;
        moon.x = planet.x + std::cos(moonAngle) * moonRadius;
        moon.y = planet.y + std::sin(moonAngle) * moonRadius * 0.65;
        moon.x = std::max(4.0, std::min(catalog_.map.w - 4.0, moon.x));
        moon.y = std::max(4.0, std::min(catalog_.map.h - 4.0, moon.y));
        occupiedBodies.emplace_back(moon.x, moon.y);
        catalog_.bodies.push_back(moon);
      }
    }

    const auto appendProfileEncounter = [&](const std::string& type,
                                            const std::string& category,
                                            std::uint64_t& ordinal) {
      const BodyProfileDef* profile =
          ChooseBodyProfile(catalog_, type, seed, ordinal);
      const std::uint64_t thisOrdinal = ordinal++;
      Body body = MakeProfileBody(catalog_, seed, category, thisOrdinal,
                                  thisOrdinal, systemId, *profile);
      const auto point = GeneratedPoint(catalog_.map, seed,
          "body-placement:" + systemId + ":" + category,
          thisOrdinal, occupiedBodies, 24.0);
      body.x = point.first;
      body.y = point.second;
      occupiedBodies.push_back(point);
      if (!catalog_.spaceMap.tasks.empty()) {
        body.taskId = catalog_.spaceMap.tasks[
            static_cast<std::size_t>(taskOrdinal % catalog_.spaceMap.tasks.size())].id;
        ++taskOrdinal;
      }
      catalog_.bodies.push_back(body);
    };
    appendProfileEncounter("debris", "debris", debrisOrdinal);
    appendProfileEncounter("derelict", "derelict", derelictOrdinal);
    appendProfileEncounter("site", "site", siteOrdinal);

    const bool miniStation =
        SpaceMapRandomWord(seed, "station-tier", i) % 4 == 0;
    const std::string stationTier = miniStation ? "mini" : "normal";
    const StationProfileDef* stationProfile = catalog_.FindStationProfile(stationTier);
    if (stationProfile == nullptr) {
      throw CatalogError("native SpaceMap has no station profile for '" + stationTier + "'");
    }

    const std::uint64_t thisStationOrdinal = stationOrdinal++;
    Body station;
    station.id = SpaceMapGeneratedId(catalog_.spaceMap.rules.version, seed,
                                     "station", thisStationOrdinal);
    station.type = "station";
    station.tag = "station." + stationTier;
    station.name = GeneratedName(catalog_, seed, thisStationOrdinal, "station");
    station.klass = miniStation ? "Compact trading station" : "Full-service station";
    station.system = systemId;
    const auto stationPoint = GeneratedPoint(catalog_.map, seed,
        "body-placement:" + systemId + ":station", thisStationOrdinal,
        occupiedBodies, 24.0);
    station.x = stationPoint.first;
    station.y = stationPoint.second;
    occupiedBodies.push_back(stationPoint);
    station.details = {{"Tier", stationTier},
                       {"Services", miniStation ? "Trade, refuel" :
                           "Trade, refuel, resupply, repair, upgrades, hire, contracts"}};
    station.scan = {"The station's public traffic beacon is operating."};
    station.scanLocked = {"Dockside telemetry is unavailable before a safe berth."};

    std::vector<std::string> stock = stationProfile->goodIds;
    for (std::size_t j = stock.size(); j > 1; --j) {
      const std::size_t swapIndex = static_cast<std::size_t>(
          SpaceMapRandomWord(seed, "station-stock:" + station.id, j) % j);
      std::swap(stock[j - 1], stock[swapIndex]);
    }
    const int configuredLimit = miniStation
        ? catalog_.spaceMap.rules.miniStationGoodsMax
        : catalog_.spaceMap.rules.normalStationGoodsMax;
    const int maximum = std::min({stationProfile->maxGoods,
                                  configuredLimit,
                                  static_cast<int>(stock.size())});
    const int minimum = std::min(stationProfile->minGoods, maximum);
    const int goodsCount = minimum + static_cast<int>(
        SpaceMapRandomWord(seed, "station-stock-count", thisStationOrdinal) %
        static_cast<std::uint64_t>(maximum - minimum + 1));

    Port port;
    port.id = station.id;
    port.name = station.name;
    port.tip = miniStation ? "A limited beta-market with trade and refuelling." :
                             "A full-service generated station.";
    port.stationTier = stationProfile->tier;
    port.services = stationProfile->services;
    port.beta = stationProfile->beta;
    for (int goodIndex = 0; goodIndex < goodsCount; ++goodIndex) {
      const std::string& goodId = stock[static_cast<std::size_t>(goodIndex)];
      const Good* good = catalog_.FindGood(goodId);
      if (good == nullptr) {
        throw CatalogError("generated station references missing good '" + goodId + "'");
      }
      port.goodIds.push_back(goodId);
      const std::uint64_t priceDraw = SpaceMapRandomWord(
          seed, "station-price:" + station.id + ":" + good->kind,
          static_cast<std::uint64_t>(goodIndex));
      port.prices[good->kind] = 0.85 + static_cast<double>(priceDraw % 31) / 100.0;
    }

    catalog_.bodies.push_back(station);
    catalog_.ports.push_back(port);
    if (stationTier == "normal") { normalStationIds.push_back(port.id); }

    const ShipContactProfileDef* contact =
        ChooseShipContact(catalog_, seed, i);
    const std::uint64_t thisShipOrdinal = shipOrdinal++;
    Body shipBody;
    shipBody.id = SpaceMapGeneratedId(catalog_.spaceMap.rules.version, seed,
                                      "ship", thisShipOrdinal);
    shipBody.type = contact->type;
    shipBody.tag = contact->tag;
    shipBody.name = GeneratedName(catalog_, seed, thisShipOrdinal,
                                  contact->nameRule);
    shipBody.klass = contact->klass;
    shipBody.system = systemId;
    const auto shipPoint = GeneratedPoint(catalog_.map, seed,
        "body-placement:" + systemId + ":ship", thisShipOrdinal,
        occupiedBodies, 24.0);
    shipBody.x = shipPoint.first;
    shipBody.y = shipPoint.second;
    shipBody.details = {{"Movement", contact->movementEveryActions == 0
                                      ? "Stationary contact"
                                      : "Moves every " +
                                            std::to_string(contact->movementEveryActions) +
                                            " committed actions"}};
    for (const ShipContactActionDef& action : contact->actions) {
      shipBody.details.emplace_back(action.label, action.text);
    }
    shipBody.scan = {"A traffic survey records this generated ship contact."};
    shipBody.scanLocked = {"A longer passive listen could clarify its route."};
    catalog_.bodies.push_back(shipBody);

    catalog_.systems.push_back(system);
    systemsByRegion[regionId].push_back(systemId);
    generatedSystems.push_back({systemId, regionId, systemName});
  }

  // A generated station's stock is a profile-selected subset of the canonical
  // native goods. The catalogue keeps one definition per commodity ID.
  if (normalStationIds.size() > 1) {
    std::uint64_t contractOrdinal = 0;
    for (std::size_t i = 0; i < normalStationIds.size(); ++i) {
      const std::string& originId = normalStationIds[i];
      const std::string& destinationPortId =
          normalStationIds[(i + 1) % normalStationIds.size()];
      const Body* destinationPort = catalog_.FindBody(destinationPortId);
      if (destinationPort == nullptr) { continue; }
      const auto destinationSystem = std::find_if(
          generatedSystems.begin(), generatedSystems.end(),
          [&destinationPort](const GeneratedSystemInfo& candidate) {
            return candidate.id == destinationPort->system;
          });
      if (destinationSystem == generatedSystems.end()) { continue; }
      const auto scanTarget = scanTargetBySystem.find(destinationSystem->id);
      if (scanTarget == scanTargetBySystem.end()) { continue; }

      const auto appendContract = [&](Contract contract) {
        contract.id = SpaceMapGeneratedId(catalog_.spaceMap.rules.version, seed,
                                          "contract", contractOrdinal++);
        contract.origin = originId;
        catalog_.contracts.push_back(contract);
      };
      Contract visit;
      visit.title = "Charted arrival: " + destinationSystem->name;
      visit.goal = "visit";
      visit.target = destinationSystem->id;
      visit.note = "Travel to the generated system " + destinationSystem->name + ".";
      visit.reward = 100 + static_cast<double>(
          SpaceMapRandomWord(seed, "contract-reward", contractOrdinal) % 251);
      visit.recognizePrior = true;
      appendContract(visit);

      Contract scan;
      scan.title = "Local survey: " + destinationSystem->name;
      scan.goal = "scan";
      scan.target = scanTarget->second;
      scan.note = "Deep-scan a charted planet in " + destinationSystem->name + ".";
      scan.reward = 120 + static_cast<double>(
          SpaceMapRandomWord(seed, "contract-reward", contractOrdinal) % 281);
      scan.recognizePrior = true;
      appendContract(scan);

      const Port* originPort = catalog_.FindPort(originId);
      const Good* cargoGood = nullptr;
      if (originPort != nullptr) {
        for (const std::string& id : originPort->goodIds) {
          cargoGood = catalog_.FindGood(id);
          if (cargoGood != nullptr) { break; }
        }
      }
      Contract freight;
      freight.title = "Generated freight: " + destinationSystem->name;
      freight.goal = "freight";
      freight.target = destinationPortId;
      freight.note = "Deliver a small consignment to the generated destination station.";
      freight.reward = 150 + static_cast<double>(
          SpaceMapRandomWord(seed, "contract-reward", contractOrdinal) % 351);
      freight.vol = cargoGood == nullptr ? 1 : std::min(3, std::max(1, cargoGood->vol));
      freight.cargo = cargoGood == nullptr ? "Station consignment" : cargoGood->name;
      appendContract(freight);

      Contract passenger;
      passenger.title = "Generated passage: " + destinationSystem->name;
      passenger.goal = "passenger";
      passenger.target = destinationPortId;
      passenger.note = "Carry one passenger to the generated destination station.";
      passenger.reward = 120 + static_cast<double>(
          SpaceMapRandomWord(seed, "contract-reward", contractOrdinal) % 301);
      appendContract(passenger);
    }
  }

  for (const IncidentType& mapIncident : catalog_.spaceMap.incidents) {
    const auto duplicate = std::find_if(catalog_.incidentTypes.begin(),
        catalog_.incidentTypes.end(), [&mapIncident](const IncidentType& current) {
          return current.id == mapIncident.id;
        });
    if (duplicate == catalog_.incidentTypes.end()) {
      IncidentType runtimeIncident = mapIncident;
      // Map-tag incidents are consumed by map triggers, not the recurring ship
      // incident scheduler, which correctly ignores a non-positive interval.
      runtimeIncident.first = 0;
      runtimeIncident.every = 0;
      catalog_.incidentTypes.push_back(runtimeIncident);
    }
  }
  for (const SurveyRecordDef& survey : catalog_.spaceMap.surveys) {
    if (!survey.bodyType.empty()) {
      catalog_.surveyWorth["generated:" + survey.bodyType] = survey.value;
    }
  }

  std::map<std::string, std::vector<std::string>> allSystemsByRegion;
  for (const Body& system : catalog_.systems) {
    allSystemsByRegion[system.region].push_back(system.id);
  }
  for (const auto& regionSystems : allSystemsByRegion) {
    EnsureConnected(regionSystems.second, catalog_.sectorLanes,
                    catalog_.sectorLanes);
  }
  std::vector<std::string> allRegionIds;
  for (const Body& region : catalog_.regions) { allRegionIds.push_back(region.id); }
  EnsureConnected(allRegionIds, catalog_.galaxyLanes, catalog_.galaxyLanes);

  catalog_.counts.regions = static_cast<int>(catalog_.regions.size());
  catalog_.counts.systems = static_cast<int>(catalog_.systems.size());
  catalog_.counts.bodies = static_cast<int>(catalog_.bodies.size());
  catalog_.counts.ports = static_cast<int>(catalog_.ports.size());
  catalog_.counts.lanes = static_cast<int>(catalog_.sectorLanes.size());
  catalog_.counts.contracts = static_cast<int>(catalog_.contracts.size());
  catalog_.counts.commodities = static_cast<int>(catalog_.goods.size());
  catalog_.counts.incidents = static_cast<int>(catalog_.incidentTypes.size());
  catalog_.spaceMapSeed = seed;
  catalog_.spaceMapGenerated = true;
}

// ---------------------------------------------------------------------------
// The chart's own questions
// ---------------------------------------------------------------------------

const Body* Simulation::FindBodyById(const std::string& id) const {
  // ids are unique across every layer, so a lookup does not need to know which
  // map the player is looking at.
  for (const std::string& layer : catalog_.layerOrder) {
    const std::vector<Body>* list = nullptr;
    if (layer == "system") { list = &catalog_.bodies; }
    if (layer == "sector") { list = &catalog_.systems; }
    if (layer == "galaxy") { list = &catalog_.regions; }
    if (list == nullptr) { continue; }
    for (const Body& body : *list) {
      if (body.id == id) { return &body; }
    }
  }
  return nullptr;
}

std::string Simulation::CurrentSystemId() const {
  const Body* here = FindBodyById(map_.here);
  return here == nullptr ? std::string("sec-kessler") : catalog_.BodySystem(*here);
}

const Body* Simulation::CurrentSystem() const { return FindBodyById(CurrentSystemId()); }

bool Simulation::InRange(const Body& body) const {
  if (body.type == "region") { return false; }
  if (body.type == "system") { return !body.entry.empty(); }
  return catalog_.BodySystem(body) == CurrentSystemId();
}

bool Simulation::AtTarget(const Body& body) const {
  if (body.type == "system") { return body.id == CurrentSystemId(); }
  return body.id == map_.here;
}

const Body* Simulation::HomeBody() const {
  // Position at the current map scale, not the region being browsed.
  const Body* system = CurrentSystem();
  if (map_.layer == "system") { return FindBodyById(map_.here); }
  if (map_.layer == "sector") {
    if (system != nullptr && system->region == map_.sector) { return system; }
    return nullptr;
  }
  return system == nullptr ? nullptr : FindBodyById(system->region);
}

std::vector<const Body*> Simulation::CurBodies() const {
  std::vector<const Body*> out;
  if (map_.layer == "galaxy") {
    for (const Body& body : catalog_.regions) { out.push_back(&body); }
    return out;
  }
  if (map_.layer == "system") {
    const std::string sid = CurrentSystemId();
    for (const Body& body : catalog_.bodies) {
      if (catalog_.BodySystem(body) == sid) { out.push_back(&body); }
    }
    return out;
  }
  for (const Body& body : catalog_.systems) {
    if (body.region == map_.sector) { out.push_back(&body); }
  }
  return out;
}

void Simulation::ApplyLayerGeometry(const std::string& layer) {
  const MapLayerDimensions& dimensions = catalog_.map.DimensionsFor(layer);
  const double sourceW = catalog_.map.w == 0 ? 1.0 : catalog_.map.w;
  const double sourceH = catalog_.map.h == 0 ? 1.0 : catalog_.map.h;
  map_.w = dimensions.w;
  map_.h = dimensions.h;
  map_.cx = dimensions.w * 0.5;
  map_.cy = dimensions.h * 0.5;
  map_.coordinateScaleX = dimensions.w / sourceW;
  map_.coordinateScaleY = dimensions.h / sourceH;
}

void Simulation::SetLayer(const std::string& key) {
  if (key == map_.layer) { return; }
  if (key != "system" && key != "sector" && key != "galaxy") { return; }

  // Each layer keeps its own zoom, pan and selection, so coming back finds the
  // map where it was left.
  LayerView& old = views_[map_.layer];
  old.zoom = map_.zoom;
  old.sel = state_.navTarget.value_or(std::string());

  map_.layer = key;
  ApplyLayerGeometry(key);
  LayerView& fresh = views_[key];
  map_.zoom = fresh.zoom;
  state_.navTarget = fresh.sel.empty() ? std::optional<std::string>() : std::optional<std::string>(fresh.sel);
  // The drifting rocks and the station beacon belong to the system view; the
  // client starts and stops that animation from here.
}

bool Simulation::ZoomMap(int dir) {
  // Free. One step on the ladder, clamped, then rounded to a tenth exactly as
  // the reference rounds it, so the caption and the raster always agree.
  double zoom = map_.zoom + static_cast<double>(dir) * map_.zstep;
  if (zoom < map_.zmin) { zoom = map_.zmin; }
  if (zoom > map_.zmax) { zoom = map_.zmax; }
  map_.zoom = js::Round(zoom * 10) / 10;
  return true;
}

void Simulation::PanMap(double dx, double dy) {
  // The chart may be pulled half a screen out of place, no further, or it can
  // be lost off the edge and look broken. Panning is free.
  LayerView& view = views_[map_.layer];
  const double limitX = js::Round(map_.vw * 0.5);
  const double limitY = js::Round(map_.vh * 0.5);
  view.px = std::max(-limitX, std::min(limitX, view.px + dx));
  view.py = std::max(-limitY, std::min(limitY, view.py + dy));
}

bool Simulation::ViewSector(const std::string& regionId) {
  if (FindBodyById(regionId) == nullptr) { return false; }
  const Body* region = nullptr;
  for (const Body& body : catalog_.regions) {
    if (body.id == regionId) { region = &body; }
  }
  if (region == nullptr) { return false; }

  const Body* system = CurrentSystem();
  map_.sector = regionId;
  views_["sector"].sel = (system != nullptr && system->region == regionId) ? system->id : std::string();
  SetLayer("sector");
  return true;
}

bool Simulation::SelectTarget(const std::string& bodyId) {
  if (FindBodyById(bodyId) == nullptr) { return false; }
  state_.navTarget = bodyId;
  views_[map_.layer].sel = bodyId;
  return true;
}

// ---------------------------------------------------------------------------
// navMetrics(): what a crossing would cost
// ---------------------------------------------------------------------------

CrewMember* Simulation::AssignedNavigator() {
  for (CrewMember& crew : crew_) {
    if (crew.job == "navigation" && CrewBusy(crew) == nullptr) { return &crew; }
  }
  return nullptr;
}

Simulation::NavMetrics Simulation::MetricsFor(const Body& body) const {
  const Body* here = FindBodyById(map_.here);
  double distance = 0;

  if (body.type == "system") {
    const Body* origin = CurrentSystem();
    double px = 0;
    if (origin != nullptr && body.region == origin->region) {
      const double dx = body.x - origin->x;
      const double dy = body.y - origin->y;
      px = std::sqrt(dx * dx + dy * dy);
    } else if (origin != nullptr) {
      const Body* fromRegion = nullptr;
      const Body* toRegion = nullptr;
      for (const Body& region : catalog_.regions) {
        if (region.id == origin->region) { fromRegion = &region; }
        if (region.id == body.region) { toRegion = &region; }
      }
      double dx = 0;
      double dy = 0;
      if (fromRegion != nullptr && toRegion != nullptr) {
        dx = (toRegion->x - fromRegion->x) * 1.3;
        dy = (toRegion->y - fromRegion->y) * 1.3;
      }
      px = std::sqrt(dx * dx + dy * dy) + 150;
    }
    distance = std::max(6.0, js::Round(px / 24.0) + 5.0);
  } else {
    const double dx = body.x - (here == nullptr ? 0.0 : here->x);
    const double dy = body.y - (here == nullptr ? 0.0 : here->y);
    const double px = std::sqrt(dx * dx + dy * dy);
    distance = std::max(3.0, js::Round(px / map_.pxPerAction));
  }
  if (AtTarget(body)) { distance = 0; }

  // The navigator reduces FUEL, never travel time: a busy navigator cannot plot
  // a more efficient course, and training raises the saving.
  Simulation* self = const_cast<Simulation*>(this);
  const CrewMember* navigator = nullptr;
  for (const CrewMember& crew : crew_) {
    if (crew.job == "navigation" && CrewBusy(crew) == nullptr) { navigator = &crew; break; }
  }
  (void)self;
  const double discount = navigator == nullptr
                              ? 0.0
                              : 4.0 * std::max(1.0, std::min(5.0, navigator->jobAt == 0 ? 1.0 : navigator->jobAt));
  const double drivePercent = state_.upgrades.count("drive") != 0 && state_.upgrades.at("drive") ? 80.0 : 100.0;
  const double navPercent = 100.0 - discount;

  NavMetrics metrics;
  metrics.dist = distance;
  metrics.fuel = js::Ceil(distance * drivePercent * navPercent / 10000.0);
  metrics.total = distance + 1;

  const double ly = distance * map_.lyPerAction;
  const double e13 = (ly * 9.46) / 10.0;
  const double days = distance * 19.0;
  const double months = js::Floor(days / 30.0);
  const double remainder = days - months * 30.0;

  metrics.ly = js::ToFixed(ly, 1) + " ly";
  metrics.km = js::ToFixed(e13, 1) + " x 10^13 km";
  metrics.transit = months > 0 ? "~" + Plur(months, "month") + ", " + Plur(remainder, "day")
                               : "~" + Plur(days, "day");
  return metrics;
}

// ---------------------------------------------------------------------------
// Commitments
// ---------------------------------------------------------------------------

bool Simulation::StartTravel() {
  if (!state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !InRange(*body) || AtTarget(*body)) { return false; }
  if (FindOngoing("travel") != nullptr) { return false; }
  if (state_.docked || state_.engineCondition <= 0 || StatValue("energy") <= 0) { return false; }

  const NavMetrics metrics = MetricsFor(*body);
  if (StatValue("fuel") < metrics.fuel) { return false; }
  Stat* fuel = FindStat("fuel");
  if (fuel != nullptr) { fuel->value -= metrics.fuel; }

  // Order matters. The initiating action is spent FIRST, so the tick it causes
  // cannot eat a step of the journey it starts.
  PerformAction("Course locked - " + body->name);
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("travel", "Under way to " + body->name, metrics.dist, "travel", body->id);
  return true;
}

bool Simulation::StartScan() {
  if (!state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !InRange(*body) || body->type == "system") { return false; }
  if (FindOngoing("scan") != nullptr || StatValue("energy") <= 0) { return false; }

  // Same ordering rule as a crossing: the initiating action first.
  const double cost = std::max(1.0, scanCost_ - (state_.sim.sensor ? 2.0 : 0.0));
  state_.sim.sensor = 0;
  PerformAction("Deep scan started - " + body->name);
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("scan", "Deep scan - " + body->name, cost, "scan", body->id);
  return true;
}

bool Simulation::StandWatch() {
  if (!state_.gameOver.empty()) { return false; }
  PerformAction("Stood watch");
  return true;
}

bool Simulation::Acknowledge(const std::string& taskId) {
  Task* task = FindOngoing(taskId);
  if (task == nullptr || task->state != "done") { return false; }

  // The hook runs BEFORE the task is cleared: the arrival happens while the
  // crossing is still on the books, exactly as the reference does it.
  const std::string type = task->type;
  const std::string target = task->target;

  bool handled = false;
  if (type == "travel" && !target.empty()) { ArriveAt(target); handled = true; }
  else if (type == "scan") { RevealScan(target); handled = true; }

  ClearOngoing(taskId);
  LogLine("Process acknowledged and cleared.");
  return handled;
}

// ---------------------------------------------------------------------------
// Arrival and revelation
// ---------------------------------------------------------------------------

void Simulation::RevealScan(const std::string& bodyId) {
  const Body* body = FindBodyById(bodyId);
  if (body == nullptr) { return; }

  const bool first = state_.scanned.count(body->id) == 0 || !state_.scanned.at(body->id);
  state_.scanned[body->id] = true;

  // simOnScan(): the two authored signal beats.
  if (first) {
    SimRemember("Discovery", "The crew scanned " + body->name + ".");
  }
  if (body->id == "halvan" && state_.sim.signal == "dormant") {
    state_.sim.signal = "intercepted";
    SimRemember("Signal", "An old relay at Halvan's Reach repeats a name.");
    LogLine("An unusual signal was intercepted. Open the Comms Relay.");
  }
  if (body->id == "echo-site" && state_.sim.signal == "coordinates") {
    state_.sim.signal = "request";
    SimRemember("Signal", "The Choir Array carries a request from Silicon Marrow.");
    LogLine("The Choir Array answered. Open the Comms Relay.");
  }

  FileSurvey(*body);
  ContractProgress("scan", body->id);
  CommissionProgress("scan", body->id);
  CheckCommendations();

  if (catalog_.BodySystem(*body) == CurrentSystemId()) {
    views_["system"].sel = body->id;
    if (map_.layer == "system") { state_.navTarget = body->id; }
  }
  LogLine("Deep scan readout filed: " + body->name + ". Survey record stored in the Science Room.");
}

void Simulation::FileSurvey(const Body& body) {
  for (const SurveyRecord& record : survey_) {
    if (record.id == "sv-" + body.id) { return; }  // already held
  }
  double worth = 60;
  const bool generatedBody = body.id.rfind("gen-v", 0) == 0;
  const auto generatedWorth = generatedBody
      ? catalog_.surveyWorth.find("generated:" + body.type)
      : catalog_.surveyWorth.end();
  if (generatedWorth != catalog_.surveyWorth.end()) {
    worth = generatedWorth->second;
  } else {
    const auto authoredWorth = catalog_.surveyWorth.find(body.type);
    if (authoredWorth != catalog_.surveyWorth.end()) { worth = authoredWorth->second; }
  }

  SurveyRecord record;
  record.id = "sv-" + body.id;
  record.name = body.name;
  record.src = "Deep scan";
  record.value = worth;
  record.state = "filed";
  survey_.push_back(record);
}

void Simulation::ChronicleVisit(const Body* system) {
  if (system == nullptr) { return; }
  state_.chronicle.systems[system->id] = true;
  state_.chronicle.regions[system->region] = true;
}

void Simulation::ArriveAt(const std::string& bodyId) {
  const Body* system = nullptr;
  for (const Body& body : catalog_.systems) {
    if (body.id == bodyId) { system = &body; }
  }

  const std::string localId = system != nullptr ? system->entry : bodyId;
  const Body* body = FindBodyById(localId);
  if (body == nullptr) { return; }

  map_.here = body->id;
  views_["system"].sel = body->id;

  if (system != nullptr) {
    map_.sector = system->region;
    views_["sector"].sel = system->id;
    views_["galaxy"].sel = system->region;
    if (map_.layer == "sector") { state_.navTarget = system->id; }
    else if (map_.layer == "galaxy") { state_.navTarget = system->region; }
    ContractProgress("visit", system->id);
  }
  if (map_.layer == "system") { state_.navTarget = body->id; }

  state_.docked = HerePort() != nullptr && HerePort()->id == body->id;
  RefreshLocationInfo();
  ContractProgress("visit", body->id);

  LogLine("Arrived at " + body->name +
          (state_.docked ? " - docked. Right-click the docked indicator to open the station" : "") + ".");
  SimRemember("Arrival", "The ship arrived at " + body->name + ".");
  ChronicleVisit(CurrentSystem());
  CommissionProgress("visit", body->id);
  CheckCommendations();
}

void Simulation::RefreshLocationInfo() {
  const Body* here = FindBodyById(map_.here);
  const std::string name = here == nullptr ? std::string("Unknown") : here->name;
  const bool docked = HerePort() != nullptr;
  for (InfoRow& row : infos_) {
    if (row.id == "location") {
      row.value = name + (docked ? " - docked" : "");
    } else if (row.id == "contacts") {
      const Body* system = CurrentSystem();
      row.value = docked ? name + " - station contact"
                         : "Charted objects in " + (system == nullptr ? std::string("unknown space") : system->name);
    }
  }
}

bool Simulation::Dock() {
  const Port* port = HerePort();
  if (port == nullptr || state_.docked || !state_.gameOver.empty()) { return false; }
  const std::string name = port->name;
  state_.docked = true;
  ContractProgress("visit", map_.here);
  for (InfoRow& row : infos_) {
    if (row.id == "location") { row.value = name + " - docked"; }
  }
  PerformAction("Docked at " + name);
  return true;
}

bool Simulation::Undock() {
  const Port* port = HerePort();
  if (port == nullptr || !state_.docked || !state_.gameOver.empty()) { return false; }
  const std::string name = port->name;
  state_.docked = false;
  state_.activeTab = "home";
  for (InfoRow& row : infos_) {
    if (row.id == "location") { row.value = name + " - undocked"; }
  }
  PerformAction("Undocked from " + name);
  return true;
}

}  // namespace ship

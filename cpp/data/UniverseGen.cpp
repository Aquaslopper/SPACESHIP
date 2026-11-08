// ============================================================================
//  UniverseGen.cpp — a galaxy built from one seed.
//  ----------------------------------------------------------------------------
//  Three rules, and they are the whole difference from the table it replaces:
//
//   1. Every station's prices are DERIVED from what its system is - what it grows,
//      what it builds, whether it can make medicine, who runs it - so thirty
//      stations are thirty economies with reasons, not eight tables by index.
//   2. Every system's SHAPE differs: how many worlds, whether there is a belt, how
//      many moons, whether there is a station at all.
//   3. Nothing is drawn from a global list by index. The only shared inputs are the
//      seed, the market's kinds and the chart's own board.
// ============================================================================
#include "data/UniverseGen.h"

#include <algorithm>
#include <functional>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace ship {

namespace {

// splitmix64: small, portable and identical on every machine, which is what a
// seed has to be.
struct Rng {
  std::uint64_t state = 0;
  explicit Rng(std::uint64_t seed) : state(seed + 0x9e3779b97f4a7c15ULL) {}
  std::uint64_t Next() {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }
  double Unit() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
  double Range(double lo, double hi) { return lo + Unit() * (hi - lo); }
  int Below(int n) { return n <= 1 ? 0 : static_cast<int>(Next() % static_cast<std::uint64_t>(n)); }
  bool Chance(double p) { return Unit() < p; }
};

const char* const kStems[] = {"Ard",  "Bel",  "Cass", "Dor",  "Esk",  "Fen",  "Gal",  "Hesp",
                              "Il",   "Jor",  "Kal",  "Lyr",  "Mor",  "Nex",  "Or",   "Pell",
                              "Quen", "Rho",  "Sil",  "Tar",  "Umbr", "Vex",  "Wren", "Xan",
                              "Yr",   "Zel",  "Ash",  "Bram", "Cor",  "Dun",  "Ebb",  "Frost"};
const char* const kTails[] = {"a",   "en", "is",  "or", "um", "ara", "eth", "ova",
                              "ine", "os", "ath", "une", "ay", "ir", "essa", "on"};

const char* const kRoman[] = {"I", "II", "III", "IV", "V", "VI"};

std::string Name(Rng& rng, std::set<std::string>& used) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    std::string name = std::string(kStems[rng.Below(static_cast<int>(std::size(kStems)))]) +
                       kTails[rng.Below(static_cast<int>(std::size(kTails)))];
    if (used.insert(name).second) { return name; }
  }
  const std::string fallback = "Unnamed " + std::to_string(used.size() + 1);
  used.insert(fallback);
  return fallback;
}

// What a system IS. Everything else - its worlds, its port, its prices - follows.
struct Traits {
  double population = 0.0;  // 0 none, 1 a city
  double farm = 0.0;        // grows food, needs water
  double industry = 0.0;    // builds components, eats bulk metal
  double medicine = 0.0;    // can make medicine
  double law = 0.0;         // 0 frontier, 1 policed
  double hazard = 0.0;      // storms, debris, a bad neighbourhood
  double relic = 0.0;       // something old is buried here
  double water = 0.0;       // ice or ocean locally
};

const char* ProfileWord(const Traits& t) {
  if (t.population < 0.12) { return t.relic > 0.6 ? "Dead system with a wreck" : "Empty system"; }
  if (t.industry > 0.62) { return "Industrial world"; }
  if (t.farm > 0.62) { return "Agricultural colony"; }
  if (t.population > 0.7) { return "Crowded world"; }
  if (t.law < 0.25) { return "Frontier outpost"; }
  return "Settled world";
}

// One multiplier per commodity kind. Supply pushes it down, demand pulls it up, and
// the law decides what a crate of something unregistered is worth.
double KindPrice(const Traits& t, const std::string& kind) {
  double supply = 0.0;
  double demand = 0.15 + t.population * 0.85;
  if (kind == "Perishable") {
    supply = t.farm;
    demand += 0.35 * t.population;
  } else if (kind == "Water") {
    supply = t.water;
    demand += 0.55 * t.population + 0.25 * t.farm;
  } else if (kind == "Components") {
    supply = t.industry;
    demand += 0.45 + 0.3 * t.farm + 0.25 * t.population;
  } else if (kind == "Bulk metal") {
    supply = t.industry * 0.8 + (1.0 - t.population) * 0.35;
    demand += 0.65 * t.industry + 0.3 * t.population;
  } else if (kind == "Medicine") {
    supply = t.medicine;
    demand += 0.5 * t.population + 0.35 * t.hazard;
  } else if (kind == "Unregistered") {
    // No questions asked where the law is thin; a premium where it is thick.
    supply = 0.55 * (1.0 - t.law);
    demand += 0.7 * t.law + 0.3 * t.population;
  } else if (kind == "Relics") {
    supply = t.relic;
    demand += 0.4 * t.population + 0.35 * (1.0 - t.law);
  }
  // Centred on 1.0 and scaled so the range is used instead of the ceiling: a
  // kind this place makes is cheap, a kind it needs is dear, and nothing pins.
  const double multiplier = 1.0 + 0.85 * (demand - supply) / (0.5 + demand + supply);
  return std::clamp(multiplier, 0.60, 1.90);
}

std::string Tip(const Traits& t, const std::string& systemName) {
  std::string tip = std::string(ProfileWord(t)) + " at " + systemName + ".";
  if (t.farm > 0.6) { tip += " Produce is cheap here and the ice is spoken for."; }
  if (t.industry > 0.6) { tip += " The yards sell components below the going rate."; }
  if (t.medicine < 0.3) { tip += " There is no pharmacy, so medicine fetches a premium."; }
  if (t.law < 0.3) { tip += " Nobody at this dock asks what is in the crates."; }
  if (t.law > 0.75) { tip += " Inspections are thorough and the fines are worse."; }
  if (t.relic > 0.6) { tip += " Salvage from the old wreck still turns up in the market."; }
  return tip;
}

Body MakeBody(const std::string& id, const std::string& type, const std::string& name,
              const std::string& klass, double x, double y, const std::string& system,
              const std::string& region) {
  Body body;
  body.id = id;
  body.type = type;
  body.name = name;
  body.klass = klass;
  body.x = x;
  body.y = y;
  body.system = system;
  body.region = region;
  return body;
}

std::string Roman(int index) {
  if (index >= 0 && index < static_cast<int>(std::size(kRoman))) { return kRoman[index]; }
  return std::to_string(index + 1);
}

std::string ScanFor(const std::string& type) {
  if (type == "star") { return "Stellar output has been charted."; }
  if (type == "planet") { return "Surface contours have been recorded."; }
  if (type == "belt") { return "Loose material is spread across the plotted belt."; }
  if (type == "moon") { return "A natural satellite follows a stable orbit."; }
  if (type == "station") { return "A working dock answers on the approach channel."; }
  return "The landmark has been plotted for later investigation.";
}

const char* const kSubtypes[] = {"rock", "ice", "ocean", "gas"};

}  // namespace

std::uint64_t SeedFromText(const std::string& text) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;  // FNV-1a: typed words are stable
  for (const char c : text) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

namespace {

// A neighbourhood is a place with a reason: what the stars in it are for.
// Systems inherit it and differ from their neighbours only by how much of it they
// got, which is why two stations in one region rhyme and never match.
struct Archetype {
  const char* trade;
  double population, farm, industry, medicine, law, hazard, relic, water;
};

const Archetype kArchetypes[] = {
    {"a farm belt", 0.45, 0.88, 0.18, 0.14, 0.38, 0.22, 0.12, 0.78},
    {"an industrial cluster", 0.68, 0.24, 0.90, 0.34, 0.54, 0.36, 0.22, 0.20},
    {"a core world", 0.90, 0.52, 0.62, 0.82, 0.86, 0.14, 0.30, 0.50},
    {"the frontier", 0.22, 0.30, 0.16, 0.02, 0.06, 0.80, 0.44, 0.46},
    {"an old battlefield", 0.12, 0.10, 0.06, 0.00, 0.18, 0.62, 0.92, 0.24},
    {"a waystation", 0.56, 0.34, 0.46, 0.52, 0.50, 0.42, 0.26, 0.36},
};

const char* const kRegionTails[] = {"Reach", "Verge", "Expanse", "Cluster", "March", "Drift"};

struct RegionPlan {
  Body body;
  double x = 0.0, y = 0.0;
  double spread = 0.0;
  Traits bent;
  std::string trade;
  std::vector<std::size_t> systems;
};

double Jitter(Rng& rng, double value, double swing) {
  return std::clamp(value + rng.Range(-swing, swing), 0.0, 1.0);
}

const MapGeometry& BoardFor(const Catalog& catalog, const char* layer) {
  const auto it = catalog.layerBoards.find(layer);
  return it == catalog.layerBoards.end() ? catalog.map : it->second;
}

}  // namespace

void GenerateUniverse(Catalog& catalog, std::uint64_t seed, int systemCount) {
  if (systemCount < 8) { systemCount = 8; }
  Rng rng(seed);

  const MapGeometry& board = BoardFor(catalog, "sector");
  const MapGeometry& galaxyBoard = BoardFor(catalog, "galaxy");
  const double boardW = board.w;
  const double boardH = board.h;
  const double margin = 52.0;

  // The chart the seed replaces, regions included: the caller's catalogue is a
  // set of files, not a sky.
  catalog.regions.clear();
  catalog.systems.clear();
  catalog.bodies.clear();
  catalog.ports.clear();
  catalog.sectorLanes.clear();
  catalog.galaxyLanes.clear();
  catalog.galaxyPositions.clear();

  std::set<std::string> usedNames;

  // ---- 1. neighbourhoods ---------------------------------------------------
  // Dots placed with a minimum gap, so the chart has distinct places rather than
  // a stipple. A region's spread decides how many systems it can hold and how far
  // its lanes reach.
  const int regionCount = std::clamp(3 + systemCount / 45, 3, 10);
  const double minGap = 0.27 * std::min(boardW, boardH);

  std::vector<RegionPlan> regions;
  regions.reserve(static_cast<std::size_t>(regionCount));
  for (int r = 0; r < regionCount; ++r) {
    double bestX = margin;
    double bestY = margin;
    double bestClearance = -1.0;
    for (int attempt = 0; attempt < 220; ++attempt) {
      const double x = rng.Range(margin, boardW - margin);
      const double y = rng.Range(margin, boardH - margin);
      double clearance = 1e9;
      for (const RegionPlan& other : regions) {
        clearance = std::min(clearance, std::hypot(x - other.x, y - other.y));
      }
      if (clearance >= minGap) { bestX = x; bestY = y; bestClearance = clearance; break; }
      if (clearance > bestClearance) { bestClearance = clearance; bestX = x; bestY = y; }
    }

    const Archetype& archetype = kArchetypes[rng.Below(static_cast<int>(std::size(kArchetypes)))];
    RegionPlan plan;
    plan.x = bestX;
    plan.y = bestY;
    plan.spread = minGap * rng.Range(0.42, 0.68);
    plan.trade = archetype.trade;
    plan.bent.population = archetype.population;
    plan.bent.farm = archetype.farm;
    plan.bent.industry = archetype.industry;
    plan.bent.medicine = archetype.medicine;
    plan.bent.law = archetype.law;
    plan.bent.hazard = archetype.hazard;
    plan.bent.relic = archetype.relic;
    plan.bent.water = archetype.water;
    // Two farm belts are never the same farm belt: each neighbourhood leans a
    // little off its kind, and the lean is what its systems inherit.
    const double lean = 0.10;
    plan.bent.population = Jitter(rng, plan.bent.population, lean);
    plan.bent.farm = Jitter(rng, plan.bent.farm, lean);
    plan.bent.industry = Jitter(rng, plan.bent.industry, lean);
    plan.bent.medicine = Jitter(rng, plan.bent.medicine, lean);
    plan.bent.law = Jitter(rng, plan.bent.law, lean);
    plan.bent.hazard = Jitter(rng, plan.bent.hazard, lean);
    plan.bent.relic = Jitter(rng, plan.bent.relic, lean);
    plan.bent.water = Jitter(rng, plan.bent.water, lean);

    const std::string name = Name(rng, usedNames) + " " +
                             kRegionTails[rng.Below(static_cast<int>(std::size(kRegionTails)))];
    Body body;
    body.id = "gal-" + std::to_string(r + 1);
    body.type = "region";
    body.name = name;
    body.klass = "Galactic region";
    // The galaxy board draws region dots at its own scale: the centre is carried
    // across proportionally, so a chart read at any zoom agrees with itself.
    body.x = galaxyBoard.w * (plan.x / boardW);
    body.y = galaxyBoard.h * (plan.y / boardH);
    body.details.emplace_back("Trade", plan.trade);
    plan.body = body;

    regions.push_back(std::move(plan));
    catalog.regions.push_back(regions.back().body);
  }

  // ---- 2. systems, in their neighbourhoods --------------------------------
  std::vector<double> weight(regions.size(), 0.0);
  double totalWeight = 0.0;
  for (std::size_t r = 0; r < regions.size(); ++r) {
    weight[r] = regions[r].spread * regions[r].spread;
    totalWeight += weight[r];
  }

  // How much a system may differ from the neighbourhood it sits in. Small
  // enough that a farm belt sells produce cheap all through the belt, large
  // enough that no two stations in it are the same shop.
  const double spreadSwing[8] = {0.16, 0.15, 0.15, 0.16, 0.13, 0.15, 0.12, 0.14};

  for (int index = 0; index < systemCount; ++index) {
    // Pick the region by area, so a wide place holds more stars than a tight one.
    std::size_t regionIndex = 0;
    if (index != 0) {
      double pick = rng.Unit() * totalWeight;
      for (std::size_t r = 0; r < regions.size(); ++r) {
        pick -= weight[r];
        if (pick <= 0.0) { regionIndex = r; break; }
        regionIndex = regions.size() - 1;
      }
    }
    RegionPlan& region = regions[regionIndex];

    const std::string id = index == 0 ? "sec-kessler" : "sec-gen-" + std::to_string(index);
    const std::string name = index == 0 ? "Kessler" : Name(rng, usedNames);

    double x = region.x;
    double y = region.y;
    if (index != 0) {
      // Even area coverage of the region's disc, so clusters have a middle and a rim
      // instead of a bright centre and a hole.
      const double angle = rng.Unit() * 6.283185307179586;
      const double radius = region.spread * std::sqrt(rng.Unit());
      x = std::clamp(region.x + std::cos(angle) * radius, margin, boardW - margin);
      y = std::clamp(region.y + std::sin(angle) * radius, margin, boardH - margin);
    }

    Traits traits;
    const Traits& bent = region.bent;
    traits.population = Jitter(rng, bent.population, spreadSwing[0]);
    traits.farm = Jitter(rng, bent.farm, spreadSwing[1]);
    traits.industry = Jitter(rng, bent.industry, spreadSwing[2]);
    traits.medicine = Jitter(rng, bent.medicine, spreadSwing[3]);
    traits.law = Jitter(rng, bent.law, spreadSwing[4]);
    traits.hazard = Jitter(rng, bent.hazard, spreadSwing[5]);
    traits.relic = Jitter(rng, bent.relic, spreadSwing[6]);
    traits.water = Jitter(rng, bent.water, spreadSwing[7]);

    Body system = MakeBody(id, "system", name, "Charted star system", x, y, id, region.body.id);
    system.entry = id + "-star";
    system.details.emplace_back("Region", region.body.name);
    system.details.emplace_back("Profile", ProfileWord(traits));
    system.details.emplace_back("Trade", region.trade);
    catalog.systems.push_back(std::move(system));

    region.systems.push_back(catalog.systems.size() - 1);

    // ---- the system's own bodies: never the same recipe twice --------------
    {
      Body star = MakeBody(id + "-star", "star", name, "Charted star", 100.0 + (index % 3) * 32.0,
                           58.0 + (index % 2) * 14.0, id, region.body.id);
      star.scan.push_back(ScanFor("star"));
      star.details.emplace_back("System", name);
      star.details.emplace_back("Region", region.body.name);
      catalog.bodies.push_back(std::move(star));

      // A crowded world keeps its moons; a frontier outpost is a rock and a pylon.
      const int worlds = 1 + rng.Below(2 + (traits.population > 0.55 ? 3 : 1));
      for (int w = 0; w < worlds; ++w) {
        const bool belt = rng.Chance(0.16 + 0.12 * traits.hazard);
        const std::string worldId = id + "-w" + std::to_string(w + 1);
        Body world = MakeBody(worldId, belt ? "belt" : "planet",
                              belt ? name + " Debris Belt" : name + " " + Roman(w),
                              belt ? "Debris belt" : "Charted world",
                              150.0 + w * 78.0 + rng.Range(-8.0, 8.0),
                              74.0 + ((w * 37 + index) % 90), id, region.body.id);
        world.subtype = belt ? "rock" : kSubtypes[rng.Below(4)];
        world.scan.push_back(ScanFor(world.type));
        world.details.emplace_back("System", name);
        if (!belt) { world.details.emplace_back("Surface", world.subtype); }
        catalog.bodies.push_back(std::move(world));

        const int moons = belt ? 0 : rng.Below(2 + (traits.population > 0.6 ? 2 : 0));
        for (int m = 0; m < moons; ++m) {
          Body moon = MakeBody(worldId + "-m" + std::to_string(m + 1), "moon",
                               name + " " + Roman(w) + " " + static_cast<char>('a' + m),
                               "Natural satellite", 150.0 + w * 78.0 + 14.0 + m * 11.0,
                               96.0 + ((w * 37 + index) % 90), id, region.body.id);
          moon.scan.push_back(ScanFor("moon"));
          catalog.bodies.push_back(std::move(moon));
        }
      }

      // A place with people has a dock; a place with a wreck has a site instead.
      const bool station = traits.population > 0.18 || index == 0;
      const std::string landmarkId = index == 0 ? "kessler" : id + (station ? "-port" : "-site");
      Body landmark =
          MakeBody(landmarkId, station ? "station" : "site",
                   station ? (index == 0 ? "Kessler Station" : name + " Station")
                           : name + " Landing",
                   station ? "Working station" : "Unoccupied landmark",
                   260.0 + (index % 3) * 45.0, 210.0, id, region.body.id);
      landmark.scan.push_back(ScanFor(landmark.type));
      landmark.details.emplace_back("System", name);
      landmark.details.emplace_back("Region", region.body.name);
      if (station) { landmark.details.emplace_back("Berth", "one berth"); }
      catalog.bodies.push_back(std::move(landmark));

      if (station) {
        Port port;
        port.id = landmarkId;
        port.name = index == 0 ? "Kessler Station" : name + " Station";
        port.tip = Tip(traits, name);
        for (const Good& good : catalog.goods) {
          port.prices[good.kind] = KindPrice(traits, good.kind);
        }
        catalog.ports.push_back(std::move(port));
      }
    }
  }

  // ---- contracts the old chart promised -----------------------------------
  // A contract names a body and a berth; the generated sky has neither unless it
  // drew one, so a contract that cannot be flown is dropped rather than left to
  // point at nothing.
  catalog.contracts.erase(
      std::remove_if(catalog.contracts.begin(), catalog.contracts.end(),
                     [&catalog](const Contract& contract) {
                       if (catalog.FindBody(contract.target) == nullptr) { return true; }
                       if (!contract.origin.empty() &&
                           catalog.FindPort(contract.origin) == nullptr) {
                         return true;
                       }
                       return false;
                     }),
      catalog.contracts.end());

  // ---- 3. lanes inside a neighbourhood ------------------------------------
  // Three nearest neighbours plus one long hop, so a region is a web with short
  // edges and a way out. Deduplication is a set of ordered ids, not a scan of the
  // lane list, because a chart with thousands of stars is meant to be possible.
  std::set<std::pair<std::string, std::string>> laneKeys;
  auto link = [&catalog, &laneKeys](const std::string& a, const std::string& b) {
    if (a == b) { return; }
    const std::pair<std::string, std::string> key = a < b ? std::make_pair(a, b)
                                                          : std::make_pair(b, a);
    if (!laneKeys.insert(key).second) { return; }
    catalog.sectorLanes.push_back(Lane{key.first, key.second});
  };

  for (const RegionPlan& region : regions) {
    for (std::size_t a = 0; a < region.systems.size(); ++a) {
      const Body& here = catalog.systems[region.systems[a]];
      std::vector<std::pair<double, std::string>> found;
      found.reserve(region.systems.size());
      for (std::size_t b = 0; b < region.systems.size(); ++b) {
        if (a == b) { continue; }
        const Body& there = catalog.systems[region.systems[b]];
        const double dx = here.x - there.x;
        const double dy = here.y - there.y;
        found.emplace_back(dx * dx + dy * dy, there.id);
      }
      std::sort(found.begin(), found.end());
      for (std::size_t k = 0; k < found.size() && k < 3; ++k) { link(here.id, found[k].second); }
      if (!found.empty()) { link(here.id, found.back().second); }
    }
  }

  // ---- 4. bridges between neighbourhoods ----------------------------------
  // One bridge per region to its nearest neighbour region, plus a second so the
  // far side of the chart is not a cul-de-sac.
  std::set<std::pair<std::size_t, std::size_t>> bridged;
  const auto nearestPair = [&](std::size_t a, std::size_t b) {
    double best = 1e18;
    std::pair<std::string, std::string> ends;
    for (std::size_t i = 0; i < regions[a].systems.size(); ++i) {
      const Body& left = catalog.systems[regions[a].systems[i]];
      for (std::size_t j = 0; j < regions[b].systems.size(); ++j) {
        const Body& right = catalog.systems[regions[b].systems[j]];
        const double dx = left.x - right.x;
        const double dy = left.y - right.y;
        const double d = dx * dx + dy * dy;
        if (d < best) { best = d; ends = {left.id, right.id}; }
      }
    }
    return ends;
  };
  for (std::size_t a = 0; a < regions.size(); ++a) {
    std::vector<std::pair<double, std::size_t>> order;
    for (std::size_t b = 0; b < regions.size(); ++b) {
      if (a == b) { continue; }
      order.emplace_back(std::hypot(regions[a].x - regions[b].x, regions[a].y - regions[b].y), b);
    }
    std::sort(order.begin(), order.end());
    const std::size_t bridges = std::min<std::size_t>(2, order.size());
    for (std::size_t k = 0; k < bridges; ++k) {
      const std::size_t b = order[k].second;
      const std::pair<std::size_t, std::size_t> key = a < b ? std::make_pair(a, b)
                                                            : std::make_pair(b, a);
      if (!bridged.insert(key).second) { continue; }
      const std::pair<std::string, std::string> ends = nearestPair(a, b);
      if (!ends.first.empty()) { link(ends.first, ends.second); }
    }
  }

  // ---- 5. no islands --------------------------------------------------------
  // Bridge lanes are drawn by region, so two clusters inside one wide region can
  // still end up unconnected. Walk the lane graph and join every island to the
  // main one by its nearest pair, so a chart of any size can be flown end to end.
  {
    std::map<std::string, std::string> parent;
    for (const Body& system : catalog.systems) { parent[system.id] = system.id; }
    std::function<std::string(const std::string&)> root = [&](const std::string& id) {
      std::string here = id;
      while (parent[here] != here) { here = parent[here]; }
      return here;
    };
    for (const Lane& lane : catalog.sectorLanes) {
      const std::string a = root(lane.from);
      const std::string b = root(lane.to);
      if (a != b) { parent[a] = b; }
    }
    std::map<std::string, std::vector<const Body*>> islands;
    for (const Body& system : catalog.systems) { islands[root(system.id)].push_back(&system); }
    if (islands.size() > 1) {
      // The island holding the most systems is the one everything joins.
      auto main = islands.begin();
      for (auto it = islands.begin(); it != islands.end(); ++it) {
        if (it->second.size() > main->second.size()) { main = it; }
      }
      for (auto it = islands.begin(); it != islands.end(); ++it) {
        if (it == main) { continue; }
        double best = 1e18;
        std::pair<std::string, std::string> ends;
        for (const Body* inside : it->second) {
          for (const Body* outside : main->second) {
            const double dx = inside->x - outside->x;
            const double dy = inside->y - outside->y;
            const double d = dx * dx + dy * dy;
            if (d < best) { best = d; ends = {inside->id, outside->id}; }
          }
        }
        if (!ends.first.empty()) { link(ends.first, ends.second); }
      }
    }
  }
}

}  // namespace ship

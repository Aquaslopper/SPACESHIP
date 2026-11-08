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

void GenerateUniverse(Catalog& catalog, std::uint64_t seed, int systemCount) {
  if (systemCount < 8) { systemCount = 8; }
  Rng rng(seed);

  std::vector<std::string> regionIds;
  for (const Body& region : catalog.regions) { regionIds.push_back(region.id); }
  if (regionIds.empty()) {
    Body region;
    region.id = "gal-open";
    region.type = "region";
    region.name = "The Open Chart";
    catalog.regions.push_back(region);
    regionIds.push_back(region.id);
  }

  catalog.systems.clear();
  catalog.bodies.clear();
  catalog.ports.clear();
  catalog.sectorLanes.clear();
  catalog.galaxyLanes.clear();
  catalog.galaxyPositions.clear();
  catalog.chartExpanded = false;

  // The sector board is the wide one ExpandChart() configures; before that it does
  // not exist and the system board's size stands in.
  const MapGeometry& board = [&catalog]() -> const MapGeometry& {
    const auto it = catalog.layerBoards.find("sector");
    return it == catalog.layerBoards.end() ? catalog.map : it->second;
  }();
  const double boardW = board.w;
  const double boardH = board.h;
  const double margin = 52.0;

  std::set<std::string> usedNames;
  struct Placed {
    std::string id;
    std::string region;
    double x = 0.0;
    double y = 0.0;
  };
  std::vector<Placed> placed;
  placed.reserve(static_cast<std::size_t>(systemCount));

  const int kinds = static_cast<int>(catalog.goods.size());

  for (int index = 0; index < systemCount; ++index) {
    // The voyage boots at sec-kessler; the first system carries that id so the
    // starting chart and every early default still resolve.
    const std::string id = index == 0 ? "sec-kessler" : "sec-gen-" + std::to_string(index);
    const std::string name = index == 0 ? "Kessler" : Name(rng, usedNames);

    // A spiral: dense near the middle, open at the rim, with jitter so it does not
    // read as a drawn spiral.
    const double t = static_cast<double>(index) / static_cast<double>(systemCount);
    const double angle = static_cast<double>(index) * 2.39996322972865332;  // golden angle
    const double radius = std::sqrt(t) * 1.06 + 0.06;
    const double x = std::clamp(boardW * 0.5 + std::cos(angle) * radius * (boardW * 0.5 - margin) +
                                    rng.Range(-9.0, 9.0),
                                margin, boardW - margin);
    const double y = std::clamp(boardH * 0.5 + std::sin(angle) * radius * (boardH * 0.5 - margin) +
                                    rng.Range(-9.0, 9.0),
                                margin, boardH - margin);
    // A region is a wedge of the chart, not a dice roll: lanes only join systems
    // that share one, so the map reads as neighbourhoods rather than spaghetti.
    const double turn = std::fmod(angle, 6.283185307179586) < 0.0
                            ? std::fmod(angle, 6.283185307179586) + 6.283185307179586
                            : std::fmod(angle, 6.283185307179586);
    const int regionSlot =
        std::min(static_cast<int>(regionIds.size()) - 1,
                 static_cast<int>(turn / 6.283185307179586 *
                                  static_cast<double>(regionIds.size())));
    const std::string region = regionIds[static_cast<std::size_t>(std::max(0, regionSlot))];
    placed.push_back(Placed{id, region, x, y});

    Traits traits;
    traits.population = std::clamp(rng.Range(-0.15, 1.0), 0.0, 1.0);
    traits.farm = std::clamp(rng.Range(-0.2, 1.0) * (0.5 + traits.population * 0.5), 0.0, 1.0);
    traits.industry = std::clamp(rng.Range(-0.2, 1.0) * (0.35 + traits.population * 0.65), 0.0, 1.0);
    traits.water = std::clamp(rng.Range(-0.1, 1.0), 0.0, 1.0);
    traits.medicine = std::clamp(traits.population * rng.Range(0.0, 1.0) - 0.15, 0.0, 1.0);
    traits.law = std::clamp(rng.Range(0.0, 1.0) * (0.4 + traits.population * 0.6), 0.0, 1.0);
    traits.hazard = std::clamp(rng.Range(-0.1, 1.0), 0.0, 1.0);
    traits.relic = rng.Chance(0.18) ? rng.Range(0.55, 1.0) : rng.Range(0.0, 0.2);

    Body system = MakeBody(id, "system", name, "Charted star system", x, y, id, region);
    system.entry = id + "-star";
    system.details.emplace_back("Region", region);
    system.details.emplace_back("Profile", ProfileWord(traits));
    catalog.systems.push_back(std::move(system));

    // ---- the system's own bodies: never the same recipe twice ---------------
    {
      Body star = MakeBody(id + "-star", "star", name, "Charted star", 100.0 + (index % 3) * 32.0,
                           58.0 + (index % 2) * 14.0, id, region);
      star.scan.push_back(ScanFor("star"));
      star.details.emplace_back("System", name);
      star.details.emplace_back("Region", region);
      catalog.bodies.push_back(std::move(star));

      const int worlds = 1 + rng.Below(4);  // one to four
      for (int w = 0; w < worlds; ++w) {
        const bool belt = rng.Chance(0.18);
        const std::string worldId = id + "-w" + std::to_string(w + 1);
        Body world = MakeBody(worldId, belt ? "belt" : "planet",
                              belt ? name + " Debris Belt" : name + " " + Roman(w),
                              belt ? "Debris belt" : "Charted world",
                              150.0 + w * 78.0 + rng.Range(-8.0, 8.0),
                              74.0 + ((w * 37 + index) % 90), id, region);
        world.subtype = belt ? "rock" : kSubtypes[rng.Below(4)];
        world.scan.push_back(ScanFor(world.type));
        world.details.emplace_back("System", name);
        if (!belt) { world.details.emplace_back("Surface", world.subtype); }
        catalog.bodies.push_back(std::move(world));

        const int moons = belt ? 0 : rng.Below(3);
        for (int m = 0; m < moons; ++m) {
          Body moon = MakeBody(worldId + "-m" + std::to_string(m + 1), "moon",
                               name + " " + Roman(w) + " " + static_cast<char>('a' + m),
                               "Natural satellite", 150.0 + w * 78.0 + 14.0 + m * 11.0,
                               96.0 + ((w * 37 + index) % 90), id, region);
          moon.scan.push_back(ScanFor("moon"));
          catalog.bodies.push_back(std::move(moon));
        }
      }

      const bool station = traits.population > 0.18 || index == 0;
      // The voyage boots at "kessler", so the first station carries that id and the
      // chart resolves the starting berth without a special case in the core.
      const std::string landmarkId = index == 0 ? "kessler" : id + (station ? "-port" : "-site");
      Body landmark =
          MakeBody(landmarkId, station ? "station" : "site",
                   station ? (index == 0 ? "Kessler Station" : name + " Station")
                           : name + " Landing",
                   station ? "Working station" : "Unoccupied landmark",
                   260.0 + (index % 3) * 45.0, 210.0, id, region);
      landmark.scan.push_back(ScanFor(landmark.type));
      landmark.details.emplace_back("System", name);
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
        (void)kinds;
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

  // ---- lanes: the three nearest neighbours, plus a ring so nothing is cut off --
  auto link = [&catalog](const std::string& a, const std::string& b) {
    if (a == b) { return; }
    for (const Lane& lane : catalog.sectorLanes) {
      if ((lane.from == a && lane.to == b) || (lane.from == b && lane.to == a)) { return; }
    }
    catalog.sectorLanes.push_back(Lane{a, b});
  };
  for (std::size_t i = 0; i < placed.size(); ++i) {
    std::vector<std::pair<double, std::string>> near;
    for (std::size_t j = 0; j < placed.size(); ++j) {
      if (i == j || placed[j].region != placed[i].region) { continue; }
      const double dx = placed[i].x - placed[j].x;
      const double dy = placed[i].y - placed[j].y;
      near.emplace_back(dx * dx + dy * dy, placed[j].id);
    }
    std::sort(near.begin(), near.end());
    for (std::size_t k = 0; k < near.size() && k < 3; ++k) { link(placed[i].id, near[k].second); }
    if (!near.empty()) { link(placed[i].id, near.back().second); }
  }
}

}  // namespace ship

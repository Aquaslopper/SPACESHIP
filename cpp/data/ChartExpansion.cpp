// ============================================================================
//  ChartExpansion.cpp — fills the wider board with stars, worlds and lanes.
//  ---------------------------------------------------------------------------
//  Everything here is data construction: no clock, no state, no renderer. The
//  rules are the ones the authored chart already follows - a system owns a star
//  and a handful of local destinations, a station may keep a port, a lane joins
//  two systems of one region - so a generated system behaves exactly like an
//  authored one and the rest of the port never has to know the difference.
// ============================================================================
#include "data/ChartExpansion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ship {

namespace {

// xorshift64*: small, deterministic, and identical on every platform, which the
// sky has to be - a save names bodies by id.
struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

  double Next() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    const std::uint64_t out = state * 2685821657736338717ULL;
    return static_cast<double>(out >> 11) * (1.0 / 9007199254740992.0);
  }

  int Below(int n) { return n <= 0 ? 0 : static_cast<int>(Next() * static_cast<double>(n)) % n; }

  double Range(double lo, double hi) { return lo + Next() * (hi - lo); }
};

template <typename T>
const T& Pick(const std::vector<T>& values, Rng& rng) {
  static const T kEmpty{};
  if (values.empty()) { return kEmpty; }
  return values[static_cast<std::size_t>(rng.Below(static_cast<int>(values.size())))];
}

// The names are drawn from two short tables and never collide with an authored
// one: a generated system called "Vance Cluster" would be a lie about the map.
const std::vector<std::string>& Adjectives() {
  static const std::vector<std::string> words = {
      "Ashen",  "Brine",  "Cinder",  "Dolorous", "Ember",  "Fallow",  "Gilded",
      "Harrow", "Iron",   "Jetsam",  "Keening",  "Lantern","Marrow",  "Nacre",
      "Obsidian","Pale",  "Quiet",   "Rime",     "Salt",   "Tallow",  "Umber",
      "Vellum", "Wandering", "Yarrow", "Zinc",   "Amber",  "Basalt",  "Cobalt",
      "Drift",  "Elder",  "Fen",     "Glass",    "Hollow", "Ivory",   "Jasper"};
  return words;
}

const std::vector<std::string>& Nouns() {
  static const std::vector<std::string> words = {
      "Anchorage", "Bastion", "Beacon",   "Crossing", "Deep",     "Drift",
      "Fold",      "Gate",    "Haven",    "Hollow",   "Ladder",   "Ledge",
      "March",     "Narrows", "Orchard",  "Passage",  "Quay",     "Reach",
      "Rookery",   "Shallows","Shelf",    "Span",     "Spur",     "Strand",
      "Threshold", "Verge",   "Wake",     "Watch",    "Weir",     "Yard",
      "Field",     "Run",     "Tier",     "Bight",    "Sound"};
  return words;
}

const std::vector<std::string>& WorldStems() {
  static const std::vector<std::string> words = {
      "Alder",  "Bracken", "Cinder", "Dove",   "Elm",    "Fen",   "Gorse",
      "Hazel",  "Ivory",   "Juniper","Kelp",   "Larch",  "Moss",  "Nettle",
      "Onyx",   "Pine",    "Quill",  "Reed",   "Slate",  "Thistle","Umber",
      "Vane",   "Willow",  "Yew",    "Zephyr", "Bell",   "Crane", "Dune",
      "Ember",  "Frost",   "Gale",   "Hollow"};
  return words;
}

const std::vector<std::string>& WorldSuffixes() {
  static const std::vector<std::string> words = {"",     " Minor", " Major", " Deep",
                                                 " Rise", " Vale",  " Reach", " Hollow"};
  return words;
}

const std::vector<std::string>& MoonStems() {
  static const std::vector<std::string> words = {
      "Rill", "Tarn", "Kest", "Bril", "Corr", "Dunn", "Fenn", "Galt",
      "Hask", "Ivor", "Joss", "Kel",  "Lune", "Mere", "Nell", "Orr",
      "Pell", "Quen", "Rook", "Sable","Tor",  "Vell", "Wick", "Yarrow"};
  return words;
}

const std::vector<std::string>& PortKinds() {
  static const std::vector<std::string> words = {"Anchorage", "Exchange", "Depot",
                                                 "Yard",      "Wharf",    "Relay",
                                                 "Station"};
  return words;
}

const std::vector<std::string>& StarSuffixes() {
  static const std::vector<std::string> words = {"A", "B", "C", "Major", "Minor", "Prime"};
  return words;
}

// A name that is not yet on the chart. The chart tolerates repeated words - two
// systems may both be a "Reach" - but never a repeated name.
std::string UniqueName(const std::string& wanted, std::set<std::string>& used) {
  if (used.insert(wanted).second) { return wanted; }
  for (int n = 2; n < 400; ++n) {
    std::string candidate = wanted + " " + std::to_string(n);
    if (used.insert(candidate).second) { return candidate; }
  }
  return wanted;
}

std::string Two(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

std::string One(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

const std::map<std::string, std::string>& SubtypeKlass() {
  static const std::map<std::string, std::string> table = {
      {"gas", "Gas giant - banded cloud deck"},
      {"ice", "Ice world - frozen outer orbit"},
      {"ocean", "Ocean world - liquid surface"},
      {"rock", "Rocky world - thin regolith"}};
  return table;
}

std::string SubtypeAtmosphere(const std::string& subtype) {
  if (subtype == "gas") { return "Dense hydrogen, violent bands"; }
  if (subtype == "ice") { return "Thin methane haze"; }
  if (subtype == "ocean") { return "Nitrogen and oxygen, breathable"; }
  return "Trace carbon dioxide";
}

std::string SubtypeBand(const std::string& subtype) {
  if (subtype == "gas") { return "Inner system"; }
  if (subtype == "ice") { return "Outer system"; }
  if (subtype == "ocean") { return "Habitable band"; }
  return "Middle system";
}

const std::vector<std::string>& StarScans() {
  static const std::vector<std::string> lines = {
      "Stellar output has been charted across the local volume.",
      "A steady coronagraph return. Nothing unusual in the photosphere.",
      "The star sits alone in a thin dust lane.",
      "A hot, quiet primary with no companion on the plates.",
      "Spectral lines are clean; the plates need no correction."};
  return lines;
}

const std::vector<std::string>& WorldScans() {
  static const std::vector<std::string> lines = {
      "The surface reads as surveyed, and nothing on it is moving.",
      "Long-range imaging resolves weather over the terminator.",
      "A shallow gravity well makes the approach cheap in fuel.",
      "The orbit is stable and the local debris is sparse.",
      "Faint thermal scars suggest an old impact basin.",
      "The charted orbit is clear of the main traffic lane."};
  return lines;
}

const std::vector<std::string>& StationScans() {
  static const std::vector<std::string> lines = {
      "A working dock answers on the approach channel.",
      "Traffic is light; the berths are open.",
      "The station keeps a signal lamp lit for the lane.",
      "Cargo handling runs on a slow, stubborn schedule."};
  return lines;
}

std::vector<std::pair<std::string, std::string>> WorldDetails(const std::string& systemName,
                                                              const std::string& regionName,
                                                              const std::string& subtype,
                                                              Rng& rng) {
  std::vector<std::pair<std::string, std::string>> details;
  details.emplace_back("System", systemName);
  details.emplace_back("Region", regionName);
  details.emplace_back("Age", One(rng.Range(0.4, 6.5)) + " billion years");
  details.emplace_back("Atmosphere", SubtypeAtmosphere(subtype));
  details.emplace_back("Day length", std::to_string(rng.Below(40) + 6) + " h " +
                                         std::to_string(rng.Below(60)) + " m");
  details.emplace_back("Gravity", Two(rng.Range(0.18, 1.9)) + " g");
  const std::string sign = subtype == "ice" ? "-" : "+";
  details.emplace_back("Mean temp", sign + std::to_string(rng.Below(200) + 15) + " C");
  details.emplace_back("Orbital band", SubtypeBand(subtype));
  return details;
}

// Free space on a board, so no two systems share a dot. A grid keeps this
// linear; the caller owns the list of points already taken.
class Spacer {
 public:
  struct Cell {
    int x = 0;
    int y = 0;
    bool operator<(const Cell& other) const {
      return x != other.x ? x < other.x : y < other.y;
    }
  };

  Spacer(double minDistance, const std::vector<std::pair<double, double>>& taken)
      : minDistance_(minDistance), cell_(std::max(1.0, minDistance / std::sqrt(2.0))) {
    for (const auto& point : taken) { Add(point.first, point.second); }
  }

  bool Fits(double x, double y) const {
    const Cell centre = Of(x, y);
    for (int i = centre.x - 1; i <= centre.x + 1; ++i) {
      for (int j = centre.y - 1; j <= centre.y + 1; ++j) {
        auto it = grid_.find(Cell{i, j});
        if (it == grid_.end()) { continue; }
        for (const auto& point : it->second) {
          const double dx = point.first - x;
          const double dy = point.second - y;
          if (dx * dx + dy * dy < minDistance_ * minDistance_) { return false; }
        }
      }
    }
    return true;
  }

  void Add(double x, double y) { grid_[Of(x, y)].push_back({x, y}); }

 private:
  Cell Of(double x, double y) const {
    return Cell{static_cast<int>(std::floor(x / cell_)), static_cast<int>(std::floor(y / cell_))};
  }

  double minDistance_ = 44;
  double cell_ = 32;
  std::map<Cell, std::vector<std::pair<double, double>>> grid_;
};

}  // namespace

void ExpandChart(Catalog& catalog) {
  if (catalog.chartExpanded) { return; }
  catalog.chartExpanded = true;
  if (catalog.regions.empty() || catalog.systems.empty()) { return; }

  const double authoredW = catalog.map.w;
  const double authoredH = catalog.map.h;

  // ---- 1. a bigger sector board, with the authored systems at its centre ----
  // Translation only: every authored distance is preserved, so every authored
  // crossing and every jump still costs what it cost before.
  MapGeometry sector = catalog.map;
  sector.w = authoredW * kSectorBoardScale;
  sector.h = authoredH * kSectorBoardScale;
  sector.cx = sector.w / 2.0;
  sector.cy = sector.h / 2.0;
  const double dx = (sector.w - authoredW) / 2.0;
  const double dy = (sector.h - authoredH) / 2.0;
  for (Body& system : catalog.systems) {
    system.x += dx;
    system.y += dy;
  }

  // ---- 1b. unstack the systems that were authored on one dot -------------
  // The authored board holds only seven distinct system positions for its 48
  // systems: six of them share each dot, so five of every six are invisible and
  // unclickable however the chart is drawn. They are separated here, not
  // invented elsewhere: the first system of a stack keeps the authored dot and
  // the rest step out along a golden-angle spiral around it. That is a move of
  // at most a hundred pixels, so every authored crossing stays in the same
  // band of cost it had before.
  {
    std::map<std::pair<double, double>, std::vector<std::size_t>> stacks;
    for (std::size_t i = 0; i < catalog.systems.size(); ++i) {
      stacks[{catalog.systems[i].x, catalog.systems[i].y}].push_back(i);
    }
    int stackIndex = 0;
    for (auto& entry : stacks) {
      const std::vector<std::size_t>& members = entry.second;
      if (members.size() < 2) { continue; }
      const double anchorX = catalog.systems[members[0]].x;
      const double anchorY = catalog.systems[members[0]].y;
      ++stackIndex;
      for (std::size_t k = 1; k < members.size(); ++k) {
        const double angle = 2.39996322972865 * static_cast<double>(k) +
                             0.7 * static_cast<double>(stackIndex);
        const double radius = 36.0 + 22.0 * static_cast<double>(k - 1);
        Body& system = catalog.systems[members[k]];
        system.x = std::clamp(anchorX + std::cos(angle) * radius, 24.0, sector.w - 24.0);
        system.y = std::clamp(anchorY + std::sin(angle) * radius * 0.8, 24.0, sector.h - 24.0);
      }
    }
  }
  catalog.layerBoards["system"] = catalog.map;
  catalog.layerBoards["sector"] = sector;
  catalog.layerBoards["galaxy"] = catalog.map;

  // ---- 2. names already in the sky ----------------------------------------
  std::set<std::string> systemNames;
  for (const Body& system : catalog.systems) { systemNames.insert(system.name); }
  std::set<std::string> bodyNames;
  for (const Body& body : catalog.bodies) { bodyNames.insert(body.name); }

  // The authored port profiles are the only priced baskets on the chart, so a
  // generated station trades the same goods rather than inventing a market.
  // Copied, not pointed at: this function appends ports, and an append can move
  // the vector out from under any pointer taken before it.
  struct Basket {
    std::map<std::string, double> prices;
    std::string tip;
  };
  std::vector<Basket> pricedPorts;
  for (const Port& port : catalog.ports) {
    if (!port.prices.empty()) { pricedPorts.push_back(Basket{port.prices, port.tip}); }
  }

  // ---- 3. the space already taken ----------------------------------------
  std::vector<std::pair<double, double>> placed;
  placed.reserve(catalog.systems.size() + static_cast<std::size_t>(kNewSystemsPerRegion) *
                                              catalog.regions.size());
  for (const Body& system : catalog.systems) { placed.emplace_back(system.x, system.y); }

  Rng rng(0x5350414345534849ULL);  // "SPACES"
  const double margin = 44.0;
  const double spacing = 44.0;
  Spacer spacer(spacing, placed);

  int serial = 0;
  for (std::size_t regionIndex = 0; regionIndex < catalog.regions.size(); ++regionIndex) {
    const Body& region = catalog.regions[regionIndex];
    for (int n = 0; n < kNewSystemsPerRegion; ++n) {
      ++serial;

      // A position of its own. If the board cannot take another system at the
      // wide spacing, close in by two pixels at a time rather than stack one.
      double x = 0;
      double y = 0;
      bool found = false;
      for (double attemptSpacing = spacing; attemptSpacing >= 22.0 && !found;
           attemptSpacing -= 2.0) {
        Spacer test(attemptSpacing, placed);
        for (int attempt = 0; attempt < 900; ++attempt) {
          const double cx = rng.Range(margin, sector.w - margin);
          const double cy = rng.Range(margin, sector.h - margin);
          if (test.Fits(cx, cy)) {
            x = cx;
            y = cy;
            found = true;
            break;
          }
        }
      }
      if (!found) { continue; }  // a full board is a full board, not a stack

      const std::string systemId = "sec-gen-" + std::to_string(serial);
      const std::string systemName =
          UniqueName(Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng), systemNames);
      const std::string starId = "gen-" + std::to_string(serial) + "-star";

      // ---- the system itself ---------------------------------------------
      Body system;
      system.id = systemId;
      system.type = "system";
      system.name = systemName;
      system.x = x;
      system.y = y;
      system.region = region.id;
      system.entry = starId;
      const std::vector<std::string> traffic = {"Quiet", "Moderate", "Heavy", "Negligible"};
      const std::string trafficWord = Pick(traffic, rng);
      system.klass = std::string(rng.Next() < 0.5 ? "Single star - " : "Binary pair - ") + trafficWord;
      system.details.emplace_back("Traffic", trafficWord);
      system.details.emplace_back("Region", region.name);

      // ---- the star -------------------------------------------------------
      Body star;
      star.id = starId;
      star.type = "star";
      star.name = UniqueName(systemName + " " + Pick(StarSuffixes(), rng), bodyNames);
      star.system = systemId;
      star.klass = "Charted star";
      {
        const double angle = rng.Range(0.0, 6.283);
        const double radius = rng.Range(40.0, 88.0);
        star.x = std::clamp(catalog.map.cx + std::cos(angle) * radius, 20.0, authoredW - 20.0);
        star.y = std::clamp(catalog.map.cy + std::sin(angle) * radius * 0.62, 20.0,
                            authoredH - 20.0);
      }
      star.details.emplace_back("System", systemName);
      star.details.emplace_back("Region", region.name);
      star.scan.push_back(Pick(StarScans(), rng));
      star.scan.push_back("Stellar output has been charted.");
      star.scanLocked.push_back("Detailed spectral survey");

      catalog.bodies.push_back(star);
      std::vector<std::pair<double, double>> localPlaced = {{star.x, star.y}};

      int localBodies = 1;
      const auto freeLocal = [&](double wantX, double wantY, double minGap,
                                 std::pair<double, double>& out) {
        for (int attempt = 0; attempt < 24; ++attempt) {
          bool clear = true;
          for (const auto& other : localPlaced) {
            const double ddx = other.first - wantX;
            const double ddy = other.second - wantY;
            if (ddx * ddx + ddy * ddy < minGap * minGap) { clear = false; break; }
          }
          if (clear) {
            out = {wantX, wantY};
            return true;
          }
          wantX = std::clamp(wantX + rng.Range(-16.0, 16.0), 20.0, authoredW - 20.0);
          wantY = std::clamp(wantY + rng.Range(-16.0, 16.0), 20.0, authoredH - 20.0);
        }
        return false;
      };

      // ---- worlds, a moon, a station, sometimes a belt or a site -----------
      const std::vector<std::string> subtypes = {"rock", "ocean", "ice", "gas"};
      const int worlds = 2 + rng.Below(2);  // two or three
      for (int w = 0; w < worlds; ++w) {
        const std::string subtype = subtypes[static_cast<std::size_t>((serial + w) % 4)];
        Body world;
        world.id = "gen-" + std::to_string(serial) + "-w" + std::to_string(w + 1);
        world.type = "planet";
        world.subtype = subtype;
        world.system = systemId;
        world.name = UniqueName(Pick(WorldStems(), rng) + Pick(WorldSuffixes(), rng), bodyNames);
        world.klass = SubtypeKlass().at(subtype);
        const double angle = rng.Range(0.0, 6.283);
        const double radius = rng.Range(112.0, 236.0);
        std::pair<double, double> at{0, 0};
        const double wantX = std::clamp(catalog.map.cx + std::cos(angle) * radius, 20.0,
                                        authoredW - 20.0);
        const double wantY = std::clamp(catalog.map.cy + std::sin(angle) * radius * 0.45, 20.0,
                                        authoredH - 20.0);
        if (!freeLocal(wantX, wantY, 34.0, at)) { continue; }
        world.x = at.first;
        world.y = at.second;
        localPlaced.push_back(at);
        world.details = WorldDetails(systemName, region.name, subtype, rng);
        world.scan.push_back(Pick(WorldScans(), rng));
        world.scan.push_back("This world is charted beyond the system's primary traffic lane.");
        world.scanLocked.push_back("Subsurface survey");
        world.scanLocked.push_back("High-resolution orbital survey");
        catalog.bodies.push_back(world);
        ++localBodies;

        // One moon, orbiting the first world that got placed.
        if (w == 0) {
          Body moon;
          moon.id = "gen-" + std::to_string(serial) + "-moon";
          moon.type = "moon";
          moon.system = systemId;
          moon.name = UniqueName(Pick(MoonStems(), rng) + " " + world.name, bodyNames);
          moon.klass = "Natural satellite - stable orbit";
          std::pair<double, double> moonAt{0, 0};
          const double moonAngle = rng.Range(0.0, 6.283);
          if (freeLocal(std::clamp(world.x + std::cos(moonAngle) * 16.0, 20.0, authoredW - 20.0),
                        std::clamp(world.y + std::sin(moonAngle) * 16.0, 20.0, authoredH - 20.0),
                        12.0, moonAt)) {
            moon.x = moonAt.first;
            moon.y = moonAt.second;
            localPlaced.push_back(moonAt);
            moon.details.emplace_back("System", systemName);
            moon.details.emplace_back("Region", region.name);
            moon.details.emplace_back("Satellite of", world.name);
            moon.details.emplace_back("Surface", "Heavily cratered");
            moon.scan.push_back("A natural satellite follows a stable orbit.");
            moon.scan.push_back("The surface is heavily cratered.");
            moon.scanLocked.push_back("Subsurface structure");
            catalog.bodies.push_back(moon);
            ++localBodies;
          }
        }
      }

      // A station, and for three in four of them a port that trades.
      if (rng.Next() < 0.72) {
        Body station;
        station.id = "gen-" + std::to_string(serial) + "-port";
        station.type = "station";
        station.system = systemId;
        station.name = UniqueName(Pick(Nouns(), rng) + " " + Pick(PortKinds(), rng), bodyNames);
        station.klass = "Working station";
        std::pair<double, double> at{0, 0};
        const double angle = rng.Range(0.0, 6.283);
        const double wantX = std::clamp(catalog.map.cx + std::cos(angle) * rng.Range(96.0, 210.0),
                                        20.0, authoredW - 20.0);
        const double wantY = std::clamp(catalog.map.cy + std::sin(angle) * rng.Range(50.0, 100.0),
                                        20.0, authoredH - 20.0);
        if (freeLocal(wantX, wantY, 26.0, at)) {
          station.x = at.first;
          station.y = at.second;
          localPlaced.push_back(at);
          station.details.emplace_back("System", systemName);
          station.details.emplace_back("Region", region.name);
          station.details.emplace_back("Berths", std::to_string(rng.Below(24) + 2));
          station.scan.push_back(Pick(StationScans(), rng));
          station.scanLocked.push_back("Station manifest");
          catalog.bodies.push_back(station);
          ++localBodies;

          if (!pricedPorts.empty() && rng.Next() < 0.75) {
            const Basket& basket = pricedPorts[static_cast<std::size_t>(serial) %
                                               pricedPorts.size()];
            Port port;
            port.id = station.id;
            port.name = station.name;
            port.prices = basket.prices;
            port.tip = "A frontier dock on the " + region.name + " lane. " + basket.tip;
            catalog.ports.push_back(port);
          }
        }
      }

      // A belt or a site, for texture.
      if (rng.Next() < 0.42) {
        const bool beltWanted = rng.Next() < 0.5;
        Body feature;
        feature.id = "gen-" + std::to_string(serial) + (beltWanted ? "-belt" : "-site");
        feature.type = beltWanted ? "belt" : "site";
        feature.system = systemId;
        feature.name = UniqueName(beltWanted
                                      ? Pick(WorldStems(), rng) + " Belt"
                                      : Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng),
                                  bodyNames);
        feature.klass = beltWanted ? "Debris belt" : "Chart anomaly - unclaimed";
        std::pair<double, double> at{0, 0};
        const double angle = rng.Range(0.0, 6.283);
        if (freeLocal(std::clamp(catalog.map.cx + std::cos(angle) * rng.Range(150.0, 250.0), 20.0,
                                 authoredW - 20.0),
                      std::clamp(catalog.map.cy + std::sin(angle) * rng.Range(70.0, 110.0), 20.0,
                                 authoredH - 20.0),
                      22.0, at)) {
          feature.x = at.first;
          feature.y = at.second;
          localPlaced.push_back(at);
          feature.details.emplace_back("System", systemName);
          feature.details.emplace_back("Region", region.name);
          feature.scan.push_back(beltWanted ? "Loose material is spread across the plotted belt."
                                            : "A dull return, well off the traffic lane.");
          feature.scanLocked.push_back("Ore density mapping");
          catalog.bodies.push_back(feature);
          ++localBodies;
        }
      }

      system.details.insert(system.details.begin(),
                            {"Charted bodies", std::to_string(localBodies)});
      catalog.systems.push_back(system);
      placed.emplace_back(x, y);
      spacer.Add(x, y);
    }
  }

  // ---- 4. lanes: each new system joins its two nearest neighbours ----------
  // Same region only, exactly like the authored sectorLinks; a lane that
  // crossed regions would be rejected by Catalog::Validate().
  {
    for (const Body& system : catalog.systems) {
      if (system.id.rfind("sec-gen-", 0) != 0) { continue; }
      std::vector<std::pair<double, std::string>> neighbours;
      for (const Body& other : catalog.systems) {
        if (other.id == system.id || other.region != system.region) { continue; }
        const double ddx = other.x - system.x;
        const double ddy = other.y - system.y;
        neighbours.emplace_back(std::sqrt(ddx * ddx + ddy * ddy), other.id);
      }
      std::sort(neighbours.begin(), neighbours.end());
      const std::size_t links = std::min<std::size_t>(2, neighbours.size());
      for (std::size_t i = 0; i < links; ++i) {
        const std::string& other = neighbours[i].second;
        bool exists = false;
        for (const Lane& lane : catalog.sectorLanes) {
          if ((lane.from == system.id && lane.to == other) ||
              (lane.from == other && lane.to == system.id)) {
            exists = true;
            break;
          }
        }
        if (!exists) { catalog.sectorLanes.push_back(Lane{system.id, other}); }
      }
    }
  }

  // ---- 5. the totals the client prints on load ----------------------------
  catalog.counts.regions = static_cast<int>(catalog.regions.size());
  catalog.counts.systems = static_cast<int>(catalog.systems.size());
  catalog.counts.bodies = static_cast<int>(catalog.bodies.size());
  catalog.counts.ports = static_cast<int>(catalog.ports.size());
  catalog.counts.lanes = static_cast<int>(catalog.sectorLanes.size());
}

}  // namespace ship

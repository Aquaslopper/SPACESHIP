// ============================================================================
//  ChartExpansion.cpp — the wider board, and the rows that fill it.
//  ---------------------------------------------------------------------------
//  Expansion is two things, and only the second one is new.
//
//  First, the chart the reference would have built. The authored catalogue packs
//  forty-eight systems onto a 638x278 board, and forty-one of them share one of
//  seven dots, so a sector chart that draws the whole galaxy paints six systems
//  on top of each other and only the last one can be clicked. The board doubles
//  and the systems move to its middle, which preserves every distance, so an
//  authored crossing costs what it always cost.
//
//  Second, more sky. Every region gains rows of its own, and those rows go
//  through the same BuildGalaxyFromRows()/ExpandLocalCharts() the authored
//  forty-seven do, so a generated system has the reference's three local bodies
//  plus its outer world and moon, the reference's klass strings, detail rows,
//  scans and - for a row that names a trade profile - the profile's prices and
//  hint. Nothing about a generated system is a special case for the rest of the
//  port: the only thing that differs is where the row came from.
// ============================================================================
#include "data/ChartExpansion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "data/GalaxyGen.h"

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
      "Ashen",   "Brine",   "Cinder",  "Dolorous", "Ember",   "Fallow",  "Gilded",
      "Harrow",  "Iron",    "Jetsam",  "Keening",  "Lantern", "Marrow",  "Nacre",
      "Obsidian","Pale",    "Quiet",   "Rime",     "Salt",    "Tallow",  "Umber",
      "Vellum",  "Wandering","Yarrow", "Zinc",     "Amber",   "Basalt",  "Cobalt",
      "Drift",   "Elder",   "Fen",     "Glass",    "Hollow",  "Ivory",   "Jasper"};
  return words;
}

const std::vector<std::string>& Nouns() {
  static const std::vector<std::string> words = {
      "Anchorage", "Bastion",  "Beacon",   "Crossing", "Deep",   "Drift",  "Fold",
      "Gate",      "Haven",    "Hollow",   "Ladder",   "Ledge",  "March",  "Narrows",
      "Orchard",   "Passage",  "Quay",     "Reach",    "Rookery","Shallows","Shelf",
      "Span",      "Spur",     "Strand",   "Threshold","Verge",  "Wake",   "Watch",
      "Weir",      "Yard"};
  return words;
}

const std::vector<std::string>& WorldStems() {
  static const std::vector<std::string> words = {
      "Anvil", "Beryl", "Cinder", "Dolmen", "Ember", "Fathom", "Garnet", "Harrow",
      "Ilex",  "Jasper","Kelvin", "Lumen",  "Mica",  "Nimbus", "Onyx",   "Pyre",
      "Quill", "Rime",  "Slate",  "Tarn",   "Umber", "Vane",   "Wick",   "Yarrow"};
  return words;
}

const std::vector<std::string>& WorldSuffixes() {
  static const std::vector<std::string> words = {"Prime", "Minor", "Major", "Reach", "Deep", "Hollow"};
  return words;
}

const std::vector<std::string>& StarSuffixes() {
  static const std::vector<std::string> words = {"A", "B", "C", "Prima", "Major", "Minor"};
  return words;
}

// One sentence, in the register the authored observations use. It is the scan
// text of all three bodies the row creates, exactly as row[9] is in the
// reference.
const std::vector<std::string>& Observations() {
  static const std::vector<std::string> lines = {
      "Traffic is thin and the beacons are old, but the lane is open.",
      "A survey drone still circles here, years after its ship left.",
      "The approach is quiet; the dock lights answer on the second hail.",
      "Dust from a broken moon drifts across the inner lanes.",
      "Long-range telemetry is patchy this far out.",
      "The star is steady and the local charts agree with it.",
      "Cargo tags from three different regions float in the wake.",
      "A relay pings the same six digits at every pass.",
      "The outer markers have been replaced recently. Nobody logs by whom.",
      "Ice boils off the inner rocks and brightens the whole approach."};
  return lines;
}

std::string UniqueName(const std::string& wanted, std::set<std::string>& used) {
  std::string name = wanted;
  for (int n = 2; used.count(name) != 0; ++n) { name = wanted + " " + std::to_string(n); }
  used.insert(name);
  return name;
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

namespace {

// ---------------------------------------------------------------------------
//  The local charts, filled in.
//  ---------------------------------------------------------------------------
//  A hash of the system's id, not a counter: the sky of a system must not move
//  because a system earlier in the list was added or removed, and it must be
//  the same sky on every machine. FNV-1a is enough for that.
std::uint64_t HashOf(const std::string& text) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : text) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::string FirstWord(const std::string& name) {
  std::string cleaned;
  for (const char c : name) {
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    cleaned.push_back(alnum ? c : ' ');
  }
  const std::size_t begin = cleaned.find_first_not_of(' ');
  if (begin == std::string::npos) { return name; }
  const std::size_t end = cleaned.find(' ', begin);
  return cleaned.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

}  // namespace

void PopulateLocalCharts(Catalog& catalog, std::size_t firstSystem) {
  if (catalog.orbitalWorlds.empty() || catalog.orbitalPlanetNames.empty() ||
      catalog.orbitalMoonNames.empty()) {
    return;
  }
  const std::vector<std::string>& planetNames = catalog.orbitalPlanetNames;
  const std::vector<std::string>& moonNames = catalog.orbitalMoonNames;

  for (std::size_t i = firstSystem; i < catalog.systems.size(); ++i) {
    Body& system = catalog.systems[i];
    if (system.type != "system") { continue; }
    const std::string side = system.id.substr(4);
    const std::string word = FirstWord(system.name);
    Rng rng(HashOf(system.id));

    const std::string regionName = [&catalog, &system] {
      for (const Body& region : catalog.regions) {
        if (region.id == system.region) { return region.name; }
      }
      return std::string();
    }();
    // Where the reference's own bodies already sit, so nothing lands on them.
    std::vector<std::pair<double, double>> taken;
    std::set<std::string> names;
    for (const Body& body : catalog.bodies) {
      if (body.system != system.id) { continue; }
      taken.emplace_back(body.x, body.y);
      names.insert(body.name);
    }

    // Slots on three ellipses rather than free scatter: a system chart that
    // reads as orbits is worth more than one that reads as noise. The angle of
    // every slot is fixed, only a small jitter comes from the system's hash, so
    // two systems with the same count still do not look identical.
    const auto slotSpot = [&](int k, double& outX, double& outY) {
      const int band = k % 3;
      const double ring = 92.0 + static_cast<double>(band) * 54.0;
      const int slots = 3 + band;
      const int slot = (k / 3) % slots;
      for (int attempt = 0; attempt < 10; ++attempt) {
        const double angle = 6.283185307179586 *
                                 (static_cast<double>(slot) / static_cast<double>(slots)) +
                             rng.Range(-0.18, 0.18) + 0.35 * static_cast<double>(attempt);
        const double x = std::clamp(catalog.map.cx + std::cos(angle) * ring, 26.0,
                                    catalog.map.w - 26.0);
        const double y = std::clamp(catalog.map.cy + std::sin(angle) * ring * 0.55, 26.0,
                                    catalog.map.h - 26.0);
        bool clear = true;
        for (const auto& other : taken) {
          const double dx = other.first - x;
          const double dy = other.second - y;
          if (dx * dx + dy * dy < 34.0 * 34.0) { clear = false; break; }
        }
        if (!clear) { continue; }
        outX = x;
        outY = y;
        return true;
      }
      return false;
    };

    // Ten to fifteen more bodies, as worlds and their moons: enough that the
    // local chart is a system rather than a diagram of one, and spread over the
    // three rings so the board fills evenly instead of in one corner.
    const int extra = 10 + rng.Below(6);
    for (int k = 0; k < extra; ++k) {
      double x = 0;
      double y = 0;
      if (!slotSpot(k, x, y)) { continue; }
      taken.emplace_back(x, y);

      const bool moon = (k % 3 == 2);
      const std::size_t profileIndex =
          (i + static_cast<std::size_t>(k)) % catalog.orbitalWorlds.size();
      const OrbitalWorld& profile = catalog.orbitalWorlds[profileIndex];
      const std::string id = side + "-p" + std::to_string(k + 1);

      if (moon) {
        const std::string base =
            moonNames[(i * 5 + static_cast<std::size_t>(k)) % moonNames.size()] + " " + word;
        std::string name = base;
        for (int n = 2; names.count(name) != 0; ++n) { name = base + " " + std::to_string(n); }
        names.insert(name);
        Body& body = AddLocalBody(catalog, id, "moon", name, x, y, system,
                                  "A small satellite traces a quiet path beyond the mapped world.");
        body.klass = "Natural satellite - stable orbit";
        body.details.emplace_back("Atmosphere", "None");
        body.details.emplace_back("Surface", (k % 2 == 0) ? "Icy regolith" : "Heavily cratered");
        body.scan = {"A natural satellite follows a stable orbit.",
                     std::string("The surface is ") +
                         ((k % 2 == 0) ? "covered in icy regolith." : "heavily cratered.")};
        body.scanLocked = {"Subsurface structure", "Full orbital mechanics"};
        continue;
      }

      const std::string base =
          planetNames[(i * 7 + static_cast<std::size_t>(k) * 3) % planetNames.size()] + " " + word;
      std::string name = base;
      for (int n = 2; names.count(name) != 0; ++n) { name = base + " " + std::to_string(n); }
      names.insert(name);
      Body& body = AddLocalBody(catalog, id, "planet", name, x, y, system, profile.scan);
      body.subtype = profile.id;
      body.klass = profile.label;
      body.details.emplace_back("Atmosphere", profile.atmosphere);
      body.details.emplace_back("Orbital band",
                                (k % 3 == 0) ? "Inner system"
                                             : ((k % 3 == 1) ? "Middle band" : "Outer system"));
      body.scan = {profile.scan, "Surface contours have been recorded."};
      body.scanLocked = {profile.locked, "High-resolution orbital survey"};
    }

    // One belt or one derelict, for most systems: the reference's own two
    // landmark kinds, and its own reading for each.
    if (rng.Next() < 0.6) {
      double x = 0;
      double y = 0;
      if (slotSpot(extra, x, y)) {
        taken.emplace_back(x, y);
        const bool belt = rng.Next() < 0.5;
        Body& body = AddLocalBody(catalog, side + (belt ? "-field" : "-wreck"),
                                  belt ? "belt" : "site",
                                  belt ? FirstWord(system.name) + " Debris"
                                       : word + " " + (belt ? "Field" : "Wreck"),
                                  x, y, system,
                                  belt ? "Loose material is spread across the plotted belt."
                                       : "A hull tumbling on a dead orbit, still warm.");
        body.details.emplace_back("System", system.name);
        body.details.emplace_back("Region", regionName);
        body.details.emplace_back("Hazard", belt ? "Debris" : "Unstable orbit");
        if (!belt) { body.klass = "Chart anomaly - unclaimed"; }
      }
    }
    // The summary rows now say what the chart holds, not what the reference's
    // five-body pass held. A system chart that claims five destinations while
    // drawing twelve is the same kind of lie as a filter.
    // Counted through BodySystem(), so the six hand-written Kessler bodies -
    // which carry no system id - are still counted as Kessler's.
    std::size_t local = 0;
    for (const Body& body : catalog.bodies) {
      if (catalog.BodySystem(body) == system.id) { ++local; }
    }
    bool hasBodies = false;
    bool hasNavigable = false;
    for (auto& detail : system.details) {
      if (detail.first == "Charted bodies") {
        detail.second = std::to_string(local);
        hasBodies = true;
      } else if (detail.first == "Navigable") {
        detail.second = std::to_string(local) + " local destinations";
        hasNavigable = true;
      }
    }
    if (!hasBodies) { system.details.emplace_back("Charted bodies", std::to_string(local)); }
    if (!hasNavigable) {
      system.details.emplace_back("Navigable", std::to_string(local) + " local destinations");
    }
  }
}

// ---------------------------------------------------------------------------
//  The galaxy, with stars in it.
// ---------------------------------------------------------------------------
void PlaceGalaxyDots(Catalog& catalog) {
  if (catalog.regions.empty()) { return; }
  catalog.galaxyPositions.clear();
  const double w = catalog.map.w;
  const double h = catalog.map.h;

  std::map<std::string, int> taken;
  for (std::size_t index = 0; index < catalog.regions.size(); ++index) {
    taken[catalog.regions[index].id] = 0;
  }
  for (const Body& system : catalog.systems) {
    const Body* region = nullptr;
    for (const Body& candidate : catalog.regions) {
      if (candidate.id == system.region) { region = &candidate; break; }
    }
    if (region == nullptr) { continue; }
    const int k = taken[system.region]++;
    // The region index turns each cluster's spiral, so seven regions do not
    // read as seven copies of one pattern.
    const std::size_t regionIndex = static_cast<std::size_t>(region - catalog.regions.data());
    const double angle = 2.39996322972865 * static_cast<double>(k) +
                         0.77 * static_cast<double>(regionIndex);
    // A square-root spiral: even density, and it starts at the region's own
    // name rather than in a ring around it - a hole in the middle of every
    // cluster made the galaxy scale read as seven doughnuts.
    const double radius = 3.5 + 7.6 * std::sqrt(static_cast<double>(k) + 0.35);
    const double x = std::clamp(region->x + std::cos(angle) * radius, 12.0, w - 12.0);
    const double y = std::clamp(region->y + std::sin(angle) * radius * 0.62, 12.0, h - 12.0);
    catalog.galaxyPositions[system.id] = {x, y};
  }
}

void ExpandChart(Catalog& catalog) {
  if (catalog.chartExpanded) { return; }
  catalog.chartExpanded = true;
  if (catalog.regions.empty() || catalog.systems.empty()) { return; }

  // ---- 1. the chart the reference builds, rebuilt from its rows ------------
  // The export in assets/data/chart.json is the reference's output, not its
  // input. Stripping the rebuildable part and running the two passes again
  // means a voyage always flies the galaxy the current generator describes;
  // galaxy_gen_tests.cpp proves the two agree field for field, so this is a
  // change of authorship, not of content.
  StripGeneratedChart(catalog);
  BuildGalaxyFromRows(AuthoredStarCatalog(), 0, catalog, /*linkHubs=*/true);
  ExpandLocalCharts(catalog, 0);

  const double authoredW = catalog.map.w;
  const double authoredH = catalog.map.h;

  // ---- 2. a bigger sector board, with the authored systems at its centre ----
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

  // ---- 3. unstack the systems that were authored on one dot ---------------
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

  // ---- 4. new rows, one region at a time ----------------------------------
  std::set<std::string> systemNames;
  for (const Body& system : catalog.systems) { systemNames.insert(system.name); }

  // Every trade profile a port can have, taken from the authored price table so
  // a generated port trades the reference's baskets rather than an invented one.
  std::vector<std::string> profiles;
  for (const auto& entry : catalog.portPrices) { profiles.push_back(entry.first); }
  std::sort(profiles.begin(), profiles.end());

  std::vector<std::pair<double, double>> placed;
  placed.reserve(catalog.systems.size() + static_cast<std::size_t>(kNewSystemsPerRegion) *
                                              catalog.regions.size());
  for (const Body& system : catalog.systems) { placed.emplace_back(system.x, system.y); }

  Rng rng(0x5350414345534849ULL);  // "SPACES"
  const double margin = 44.0;
  const double spacing = 66.0;
  Spacer spacer(spacing, placed);

  const std::size_t baseIndex = AuthoredStarCatalog().size();
  std::vector<StarRow> rows;
  rows.reserve(static_cast<std::size_t>(kNewSystemsPerRegion) * catalog.regions.size());

  int serial = 0;
  for (const Body& region : catalog.regions) {
    for (int n = 0; n < kNewSystemsPerRegion; ++n) {
      // A position of its own. If the board cannot take another system at the
      // wide spacing, close in by two pixels at a time rather than stack one.
      double x = 0;
      double y = 0;
      bool found = false;
      for (double attemptSpacing = spacing; attemptSpacing >= 30.0 && !found;
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

      ++serial;
      const std::string systemName =
          UniqueName(Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng), systemNames);
      const std::string starName = systemName + " " + Pick(StarSuffixes(), rng);
      const bool belt = rng.Next() < 0.22;
      const std::string worldName =
          belt ? Pick(WorldStems(), rng) + " Belt"
               : Pick(WorldStems(), rng) + " " + Pick(WorldSuffixes(), rng);
      const std::string landmarkName = Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng);
      // Roughly three rows in five keep a port, which is the authored chart's
      // own mix.
      const std::string profile = (!profiles.empty() && rng.Next() < 0.6)
                                      ? Pick(profiles, rng)
                                      : std::string();
      StarRow row;
      row.system = "sec-gen-" + std::to_string(serial);
      row.region = region.id;
      row.name = systemName;
      row.x = x;
      row.y = y;
      row.star = starName;
      row.world = worldName;
      row.landmark = landmarkName;
      row.profile = profile;
      row.note = Pick(Observations(), rng);
      rows.push_back(row);
      placed.emplace_back(x, y);
      spacer.Add(x, y);
    }
  }

  // The same two passes as the authored rows. The local-chart pass starts at
  // the first new system so its index - and with it the profile and the two
  // name tables - carries on from where the authored chart stopped.
  const std::size_t firstNewSystem = catalog.systems.size();
  BuildGalaxyFromRows(rows, baseIndex, catalog, /*linkHubs=*/false);
  ExpandLocalCharts(catalog, firstNewSystem);

  // ---- 5. lanes: each new system joins its two nearest neighbours ----------
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
      // Three neighbours, not two: a web with more than one way out of a
      // system is what makes the sector read as a network rather than a
      // necklace. Still same-region only, exactly like the authored lanes.
      const std::size_t links = std::min<std::size_t>(3, neighbours.size());
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

  // ---- 6. planets for every system, on every layer ------------------------
  // The reference gives a system five local bodies. That is the entry star, one
  // world, a landmark, the outer world and the moon; at chart scale it is a
  // scatter of five dots with a lot of empty board between them. Every system -
  // authored ones included - gains worlds of its own here, so the local chart
  // reads as a system rather than as a diagram of one.
  PopulateLocalCharts(catalog, 0);

  // And the galaxy scale gets the same treatment: every system is placed inside
  // its region's cluster, so the third layer shows the two hundred stars the
  // other two layers are drawing rather than seven named circles.
  PlaceGalaxyDots(catalog);

  // ---- 7. the totals the client prints on load ----------------------------
  catalog.counts.regions = static_cast<int>(catalog.regions.size());
  catalog.counts.systems = static_cast<int>(catalog.systems.size());
  catalog.counts.bodies = static_cast<int>(catalog.bodies.size());
  catalog.counts.ports = static_cast<int>(catalog.ports.size());
  catalog.counts.lanes = static_cast<int>(catalog.sectorLanes.size());
}

}  // namespace ship

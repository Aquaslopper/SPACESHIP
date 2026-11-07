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
  const double spacing = 44.0;
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

  // ---- 6. the totals the client prints on load ----------------------------
  catalog.counts.regions = static_cast<int>(catalog.regions.size());
  catalog.counts.systems = static_cast<int>(catalog.systems.size());
  catalog.counts.bodies = static_cast<int>(catalog.bodies.size());
  catalog.counts.ports = static_cast<int>(catalog.ports.size());
  catalog.counts.lanes = static_cast<int>(catalog.sectorLanes.size());
}

}  // namespace ship

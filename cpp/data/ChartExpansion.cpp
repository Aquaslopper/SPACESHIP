// ============================================================================
//  ChartExpansion.cpp — deterministic procedural planets, systems, galaxies,
//  and map-integrated incident definitions.
// ============================================================================
#include "data/ChartExpansion.h"
#include "data/UniverseGen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


namespace ship {

namespace {

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

std::string UniqueName(const std::string& wanted, std::set<std::string>& used) {
  std::string name = wanted;
  for (int n = 2; used.count(name) != 0; ++n) { name = wanted + " " + std::to_string(n); }
  used.insert(name);
  return name;
}

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

  double minDistance_ = 44.0;
  double cell_ = 32.0;
  std::map<Cell, std::vector<std::pair<double, double>>> grid_;
};

const std::vector<std::string>& Adjectives() {
  static const std::vector<std::string> words = {
      "Ashen",    "Brine",    "Cinder",   "Dolorous", "Ember",    "Fallow",   "Gilded",
      "Harrow",   "Iron",     "Jetsam",   "Keening",  "Lantern",  "Marrow",   "Nacre",
      "Obsidian", "Pale",     "Quiet",    "Rime",     "Salt",     "Tallow",   "Umber",
      "Vellum",   "Wandering","Yarrow",   "Zinc",     "Amber",    "Basalt",   "Cobalt",
      "Drift",    "Elder",    "Fen",      "Glass",    "Hollow",   "Ivory",    "Jasper",
      "Kestrel",  "Lichen",   "Mica",     "Nocturne", "Ochre",    "Pellucid", "Quarry",
      "Russet",   "Sable",    "Tarn",     "Upland",   "Verdigris","Wick"};
  return words;
}

const std::vector<std::string>& Nouns() {
  static const std::vector<std::string> words = {
      "Anchorage", "Bastion",   "Beacon",    "Crossing", "Deep",     "Drift",   "Fold",
      "Gate",      "Haven",     "Hollow",    "Ladder",   "Ledge",    "March",   "Narrows",
      "Orchard",   "Passage",   "Quay",      "Reach",    "Rookery",  "Shallows","Shelf",
      "Span",      "Spur",      "Strand",    "Threshold","Verge",    "Wake",    "Watch",
      "Weir",      "Yard",      "Cairn",     "Sound",    "Causeway", "Harbour", "Bight"};
  return words;
}

const std::vector<std::string>& WorldStems() {
  static const std::vector<std::string> words = {
      "Anvil",  "Beryl",  "Cinder", "Dolmen", "Ember",  "Fathom", "Garnet", "Harrow",
      "Ilex",   "Jasper", "Kelvin", "Lumen",  "Mica",   "Nimbus", "Onyx",   "Pyre",
      "Quill",  "Rime",   "Slate",  "Tarn",   "Umber",  "Vane",   "Wick",   "Yarrow",
      "Zircon", "Aether", "Boreal", "Chalk",  "Dusk",   "Flint",  "Gale",   "Halcyon"};
  return words;
}

const std::vector<std::string>& WorldSuffixes() {
  static const std::vector<std::string> words = {
      "Prime", "Minor", "Major", "Reach", "Deep", "Hollow", "II", "III", "IV", "V"};
  return words;
}

const std::vector<std::string>& StarSuffixes() {
  static const std::vector<std::string> words = {"A", "B", "C", "Prima", "Major", "Minor"};
  return words;
}

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
      "Ice boils off the inner rocks and brightens the whole approach.",
      "Ionised filaments braid across the ecliptic after every flare.",
      "Old tug transponders mark a safe corridor between the shoals."};
  return lines;
}

struct GalaxyArchetype {
  const char* stem;
  const char* form;
  const char* klass;
  const char* authority;
  const char* hazard;
  const char* history;
  const char* traffic;
};

const std::vector<GalaxyArchetype>& GalaxyArchetypes() {
  static const std::vector<GalaxyArchetype> items = {
      {"Vela", "Dwarf", "Tidal dwarf galaxy trailing old beacon chains", "Survey Guild",
       "Ion squalls",
       "Vela Dwarf was charted by long-burn picket ships following stripped star streams.",
       "Relay buoys report scattered prospectors and bursts of upper-band static."},
      {"Sable", "Magellan", "Companion cloud rich in cold gas giants and salvage", "Salvage Syndicate",
       "Drifting hulks",
       "Heavy haulers abandoned entire yards in Sable Magellan when the core tariffs shifted.",
       "Tug crews broadcast salvage advisories along the outer slipways."},
      {"Cinder", "Halo", "Metal-rich globular cluster lit by ageing red giants", "Independent",
       "Flare fronts",
       "Refineries in Cinder Halo still smelt ore on the schedules of an older charter.",
       "Ore convoys hold steady intervals between flare warnings."},
      {"Nacre", "Stream", "Luminous stellar bridge spanning two pale dust arms", "Free Captains",
       "Subspace shear",
       "Pilots cross Nacre Stream by timing the calm between opposing tidal wakes.",
       "Independent couriers trade updated shear tables at every berth."},
      {"Obsidian", "Arm", "Dark dust spur hiding sheltered frontier harbours", "Frontier Charter",
       "Uncharted shoals",
       "Obsidian Arm grew from refuelling caches laid for outer-rim survey flotillas.",
       "Light traffic moves under shuttered running beacons."},
      {"Kestrel", "Cloud", "Irregular satellite galaxy of bright young binaries", "Independent",
       "Ion squalls",
       "Binary glare in Kestrel Cloud blinds uncalibrated scopes beyond the inner buoys.",
       "Station relays advise magnetic shielding on all approach vectors."},
      {"Pale", "Cluster", "Ancient halo cluster of white dwarfs and ice worlds", "None",
       "Cryo-dust plumes",
       "Only deep-range ice cutters winter regularly among the white dwarfs of Pale Cluster.",
       "Automated bells repeat ice-drift bearings with no human controller."},
      {"Glass", "Spur", "Refractive silicate nebula threading a chain of ports", "Survey Guild",
       "Ghost telemetry",
       "Silicate veil clouds in Glass Spur echo shipboard transponders days after departure.",
       "Harbour control requires double-keyed call signs to filter phantom echoes."},
      {"Umber", "Tide", "Outer spiral wake where derelict convoys collect", "Salvage Syndicate",
       "Drifting hulks",
       "Currents in Umber Tide gather unpowered hulls into slow orbital graveyards.",
       "Breaker yards post bounties on intact reactor cores and black boxes."},
      {"Ashen", "Expanse", "Wide inter-arm void bridged by solitary fuel depots", "Frontier Charter",
       "Subspace shear",
       "Ashen Expanse was bridged one depot barge at a time across three generations.",
       "Tanker tenders report clear lanes and high demand for refined cells."},
      {"Rime", "Galaxy", "Cold ring galaxy rimmed with comet reservoirs", "Independent",
       "Cryo-dust plumes",
       "Water merchants out of Rime Galaxy supply half the dry docks of the outer rim.",
       "Comet-harvester beacons mark active cutting zones along the ring."},
      {"Hollow", "Shoal", "Fractured dwarf remnant around a quiet barycentre", "Free Captains",
       "Lane blockades",
       "No central registry claims Hollow Shoal; berth rights are settled dock by dock.",
       "Captains warn of toll pickets near unlit transit points."},
      {"Amber", "Spiral", "Warm bar-spiral pocket with terraced garden moons", "Frontier Charter",
       "Solar gusts",
       "Greenhouse domes in Amber Spiral trade fresh seed stock for machine tools.",
       "Agri-barges queue on the sunlit side of the relay buoys."},
      {"Lumen", "Fringe", "High-inclination star cloud above the core plane", "Survey Guild",
       "Ghost telemetry",
       "Observatories in Lumen Fringe look down across the whole charted disc.",
       "Science vessels request clean radio silence around deep-array moorings."},
      {"Salt", "Nebula", "Brine-ice nursery dotted with listening posts", "Independent",
       "Cryo-dust plumes",
       "Listening posts in Salt Nebula predate the current commercial lane markers.",
       "Short-wave weather packets cycle every twelve minutes from the inner docks."},
      {"Cobalt", "Reach", "Blue-giant filament linking distant shipyard moons", "Independent",
       "Ion squalls",
       "Fast packet clippers favour Cobalt Reach despite its fierce magnetospheres.",
       "Yard dispatchers broadcast open slip numbers and hull-plate surpluses."},
      {"Onyx", "Rim", "Far-edge galactic arc beyond the last charter stones", "None",
       "Lane blockades",
       "Beyond Onyx Rim the old survey stones stop; every chart line is captain-drawn.",
       "Sparse hails carry coordinates of newly sounded anomalies and wrecks."}};
  return items;
}

const MapGeometry& BoardOrDefault(const Catalog& catalog, const std::string& layer) {
  auto it = catalog.layerBoards.find(layer);
  return it == catalog.layerBoards.end() ? catalog.map : it->second;
}

}  // namespace


void RegisterMapIncidents(Catalog& catalog) {
  const auto hasIncident = [&catalog](const std::string& id) {
    for (const IncidentType& t : catalog.incidentTypes) {
      if (t.id == id) { return true; }
    }
    return false;
  };

  auto add = [&](IncidentType def) {
    if (!hasIncident(def.id)) { catalog.incidentTypes.push_back(std::move(def)); }
  };

  // every == 0 keeps map-driven incidents off the fixed action clock so they
  // only trigger from procedural map hazards, anomalies, and frontier jumps.
  {
    IncidentType t;
    t.id = "map-ion-squall";
    t.kind = "Electrical";
    t.source = "Navigation sensors";
    t.title = "Ion squall front";
    t.first = 0;
    t.every = 0;
    t.remedy = "Bleed charge through the hull grid";
    t.stages = {
        {"Charged nebula filaments are loading the outer emitter rings.", 9.0, 0, 0, 0, 0},
        {"Arcing across the emitter bus is draining shield capacitance.", 8.0, 0, -10.0, 0, 0},
        {"The squall passes after tripping the reserve relays.", 0.0, 0, -6.0, -10.0, 0},
    };
    add(std::move(t));
  }
  {
    IncidentType t;
    t.id = "map-derelict-ping";
    t.kind = "Mechanical";
    t.source = "Proximity radar";
    t.title = "Tumbling wreck debris";
    t.first = 0;
    t.every = 0;
    t.remedy = "Plot micro-thruster clearance";
    t.stages = {
        {"Fragmented hull plates from an unmarked wreck cross the orbital lane.", 10.0, 0, 0, 0, 0},
        {"Spall impacts rattle the forward plating.", 8.0, -7.0, 0, 0, 0},
        {"The ship clears the debris wake with scarred plating.", 0.0, -8.0, 0, 0, 0},
    };
    add(std::move(t));
  }
  {
    IncidentType t;
    t.id = "map-grav-shear";
    t.kind = "Mechanical";
    t.source = "Drive mount";
    t.title = "Subspace shear wake";
    t.first = 0;
    t.every = 0;
    t.remedy = "Re-trim the field coils";
    t.stages = {
        {"Tidal shear from the cluster crossing leaves harmonic vibration in the spine.", 10.0, 0, 0, 0, 0},
        {"Unchecked resonance strains the aft structural ribs.", 9.0, -8.0, 0, -6.0, 0},
        {"The resonance damps out after stressing the frame.", 0.0, -7.0, 0, 0, 0},
    };
    add(std::move(t));
  }
  {
    IncidentType t;
    t.id = "map-cryo-spore";
    t.kind = "Environmental";
    t.source = "Airlocks";
    t.title = "Cryo-dust rime";
    t.first = 0;
    t.every = 0;
    t.remedy = "Cycle thermal purge on intake seals";
    t.stages = {
        {"Volatile brine-ice crystals coat the outer umbilical louvers.", 10.0, 0, 0, 0, 0},
        {"Chilled intake manifolds draw extra heater load.", 8.0, 0, 0, -8.0, -4.0},
        {"The rime sublimates, leaving the crew chilled and tired.", 0.0, 0, 0, -6.0, -5.0},
    };
    add(std::move(t));
  }
  {
    IncidentType t;
    t.id = "map-beacon-ghost";
    t.kind = "Crew";
    t.source = "Comms array";
    t.title = "Ghost telemetry echo";
    t.first = 0;
    t.every = 0;
    t.remedy = "Filter phantom carrier bands";
    t.stages = {
        {"Reflected distress call signs repeat across the bridge speakers.", 11.0, 0, 0, 0, 0},
        {"Watches grow tense chasing false contacts on the scope.", 9.0, 0, 0, 0, -6.0},
        {"The carrier fades into background static, leaving nerves frayed.", 0.0, 0, 0, 0, -6.0},
    };
    add(std::move(t));
  }
  {
    IncidentType t;
    t.id = "map-pirate-toll";
    t.kind = "Electrical";
    t.source = "Shield array";
    t.title = "Uncharted lane target-lock";
    t.first = 0;
    t.every = 0;
    t.remedy = "Jam ranging sweep and angle shields";
    t.stages = {
        {"An unflagged cutter sweeps the approach lane with ranging radar.", 9.0, 0, 0, 0, 0},
        {"Warning grazes test the forward shield envelope.", 8.0, 0, -12.0, 0, -3.0},
        {"The raider breaks off as the ship clears the choke point.", 0.0, -5.0, -8.0, 0, 0},
    };
    add(std::move(t));
  }
}


void PlaceGalaxyDots(Catalog& catalog) {
  if (catalog.regions.empty()) { return; }
  catalog.galaxyPositions.clear();
  const MapGeometry& galaxyBoard = BoardOrDefault(catalog, "galaxy");
  const double w = galaxyBoard.w;
  const double h = galaxyBoard.h;

  std::unordered_map<std::string, std::size_t> regionIndexById;
  for (std::size_t i = 0; i < catalog.regions.size(); ++i) {
    regionIndexById[catalog.regions[i].id] = i;
  }

  std::unordered_map<std::string, int> countInRegion;
  std::vector<std::pair<double, double>> allDots;
  allDots.reserve(catalog.systems.size());
  Spacer dotSpacer(3.2, allDots);

  for (const Body& system : catalog.systems) {
    const auto regIt = regionIndexById.find(system.region);
    if (regIt == regionIndexById.end()) { continue; }
    const Body& region = catalog.regions[regIt->second];
    const int k = countInRegion[system.region]++;
    const double baseAngle =
        2.39996322972865 * static_cast<double>(k) + 0.77 * static_cast<double>(regIt->second);
    const double baseRadius = 5.0 + 9.2 * std::sqrt(static_cast<double>(k) + 0.4);

    double px = std::clamp(region.x + std::cos(baseAngle) * baseRadius, 14.0, w - 14.0);
    double py = std::clamp(region.y + std::sin(baseAngle) * baseRadius * 0.68, 14.0, h - 14.0);
    for (int step = 0; step < 32 && !dotSpacer.Fits(px, py); ++step) {
      const double a = baseAngle + 0.45 * static_cast<double>(step + 1);
      const double r = baseRadius + 2.5 * static_cast<double>(step + 1);
      px = std::clamp(region.x + std::cos(a) * r, 14.0, w - 14.0);
      py = std::clamp(region.y + std::sin(a) * r * 0.68, 14.0, h - 14.0);
    }
    px = std::round(px * 10.0) / 10.0;
    py = std::round(py * 10.0) / 10.0;
    dotSpacer.Add(px, py);
    catalog.galaxyPositions[system.id] = {px, py};
  }
}

void EntangleGalaxy(Catalog& catalog) {
  if (catalog.regions.size() < 2) { return; }
  const auto linked = [&catalog](const std::string& a, const std::string& b) {
    for (const Lane& lane : catalog.galaxyLanes) {
      if ((lane.from == a && lane.to == b) || (lane.from == b && lane.to == a)) { return true; }
    }
    return false;
  };
  for (const Body& region : catalog.regions) {
    std::size_t degree = 0;
    for (const Lane& lane : catalog.galaxyLanes) {
      if (lane.from == region.id || lane.to == region.id) { ++degree; }
    }
    std::vector<std::pair<double, std::string>> neighbours;
    for (const Body& other : catalog.regions) {
      if (other.id == region.id) { continue; }
      const double dx = other.x - region.x;
      const double dy = other.y - region.y;
      neighbours.emplace_back(std::sqrt(dx * dx + dy * dy), other.id);
    }
    std::sort(neighbours.begin(), neighbours.end());
    for (const auto& near : neighbours) {
      if (degree >= 3) { break; }
      if (linked(region.id, near.second)) { continue; }
      catalog.galaxyLanes.push_back(Lane{region.id, near.second});
      ++degree;
    }
  }
}

void ExpandChart(Catalog& catalog) {
  if (catalog.chartExpanded) { return; }
  catalog.chartExpanded = true;

  // ---- 1. the three boards -----------------------------------------------
  const double authoredW = catalog.map.w;
  const double authoredH = catalog.map.h;

  MapGeometry sector = catalog.map;
  sector.w = authoredW * kSectorBoardScale;
  sector.h = authoredH * kSectorBoardScale;
  sector.cx = sector.w / 2.0;
  sector.cy = sector.h / 2.0;

  MapGeometry galaxy = catalog.map;
  galaxy.w = authoredW * kGalaxyBoardScale;
  galaxy.h = authoredH * kGalaxyBoardScale;
  galaxy.cx = galaxy.w / 2.0;
  galaxy.cy = galaxy.h / 2.0;

  catalog.layerBoards["system"] = catalog.map;
  catalog.layerBoards["sector"] = sector;
  catalog.layerBoards["galaxy"] = galaxy;
  for (auto& entry : catalog.layerBoards) {
    entry.second.zmin = kZoomMin;
    entry.second.zmax = kZoomMax;
    entry.second.zstep = kZoomStep;
  }
  // Each scale gets its own short ladder. A ladder of ten stops means a wheel
  // crossing it in a dozen notches; the old shared 0.3..10 range meant forty, and
  // the wheel felt broken rather than slow.
  catalog.layerBoards["galaxy"].zmin = 1.0;
  catalog.layerBoards["galaxy"].zmax = 3.0;
  catalog.layerBoards["galaxy"].zstep = 0.2;
  // Zoom 1 is the whole board; every step above it is a window onto part of it,
  // which is how a chart wider than the panel is read. The old ceiling of 2.0 cut
  // the window off at half the board, so a crowded sector could never be read at
  // all - that is the clamp the owner objected to, and it was mine.
  catalog.layerBoards["sector"].zmin = 1.0;
  catalog.layerBoards["sector"].zmax = 5.0;
  catalog.layerBoards["sector"].zstep = 0.2;
  catalog.layerBoards["system"].zmin = 1.0;
  catalog.layerBoards["system"].zmax = kSystemZoomMax;
  catalog.layerBoards["system"].zstep = 0.2;

  // ---- 2. the universe ---------------------------------------------------
  // The chart has no authored baseline and no template pass any more. There were
  // two: forty-eight rows rebuilt from a star table on every load, and a draft of
  // extra systems picked from eight price profiles. Both are gone; what a system
  // is now comes from the seed, and what a station charges comes from what the
  // system is.
  if (catalog.universeSeed == 0) { catalog.universeSeed = 1; }
  GenerateUniverse(catalog, catalog.universeSeed, catalog.universeSystems);
  catalog.RebuildIndex();

  // ---- 3. the galaxy scale ------------------------------------------------
  PlaceGalaxyDots(catalog);
  EntangleGalaxy(catalog);

  if (std::find(catalog.taskTypes.begin(), catalog.taskTypes.end(), "extract") ==
      catalog.taskTypes.end()) {
    catalog.taskTypes.push_back("extract");
  }

  catalog.counts.regions = static_cast<int>(catalog.regions.size());
  catalog.counts.systems = static_cast<int>(catalog.systems.size());
  catalog.counts.bodies = static_cast<int>(catalog.bodies.size());
  catalog.counts.ports = static_cast<int>(catalog.ports.size());
  catalog.counts.lanes = static_cast<int>(catalog.sectorLanes.size());
  catalog.RebuildIndex();
}

}  // namespace ship

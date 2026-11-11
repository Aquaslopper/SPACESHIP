// ============================================================================
//  ChartExpansion.cpp — deterministic procedural planets, systems, galaxies,
//  and map-integrated incident definitions.
// ============================================================================
#include "data/ChartExpansion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/GalaxyGen.h"

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

bool InsideCircle(double x, double y, double cx, double cy, double radius) {
  const double dx = x - cx;
  const double dy = y - cy;
  return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

void PopulateGalaxies(Catalog& catalog, double coreDx, double coreDy) {
  const MapGeometry galaxyBoard = BoardOrDefault(catalog, "galaxy");
  for (Body& region : catalog.regions) {
    region.x += coreDx;
    region.y += coreDy;
  }

  std::set<std::string> usedNames;
  std::vector<std::pair<double, double>> taken;
  for (const Body& region : catalog.regions) {
    usedNames.insert(region.name);
    taken.emplace_back(region.x, region.y);
  }

  const ChartSettings& settings = catalog.chartSettings;
  const int galaxyCount = ClampGalaxies(settings.galaxies);
  Rng rng(settings.seed ^ 0x47414C4158593234ULL);
  const auto& archetypes = GalaxyArchetypes();
  const double spread = settings.spread <= 0.0 ? 1.0 : settings.spread;
  const double radius = std::min(galaxyBoard.w, galaxyBoard.h) * 0.5 - 24.0;
  const double fillR = radius * (spread < 1.0 ? spread : 1.0);

  // Pack galaxies in the circular board. Spread is the gap between them;
  // map size is the circle they sit in.
  for (int i = 0; i < galaxyCount; ++i) {
    const GalaxyArchetype& arch =
        archetypes[static_cast<std::size_t>(rng.Below(static_cast<int>(archetypes.size())))];
    const double u = rng.Next();
    const double ring = fillR * std::sqrt(u);
    const double angle = rng.Range(0.0, 6.283185307179586);

    double gx = galaxyBoard.cx;
    double gy = galaxyBoard.cy;
    bool placed = false;
    const double baseGap = 210.0 * (spread < 1.0 ? 1.0 : spread);
    for (double minGap = baseGap; minGap >= 72.0 && !placed; minGap -= 18.0) {
      Spacer spacer(minGap, taken);
      for (int attempt = 0; attempt < 160; ++attempt) {
        const double jitterA = attempt == 0 ? 0.0 : rng.Range(-0.55, 0.55);
        const double jitterR = attempt == 0 ? 1.0 : rng.Range(0.65, 1.12);
        const double cx = galaxyBoard.cx + std::cos(angle + jitterA) * ring * jitterR;
        const double cy = galaxyBoard.cy + std::sin(angle + jitterA) * ring * jitterR;
        if (!InsideCircle(cx, cy, galaxyBoard.cx, galaxyBoard.cy, fillR)) { continue; }
        if (spacer.Fits(cx, cy)) {
          gx = std::round(cx);
          gy = std::round(cy);
          placed = true;
          break;
        }
      }
    }
    taken.emplace_back(gx, gy);

    Body region;
    region.id = "gal-gen-" + std::to_string(i + 1);
    region.type = "region";
    region.name = UniqueName(std::string(arch.stem) + " " + arch.form, usedNames);
    region.x = gx;
    region.y = gy;
    region.cluster = 16 + rng.Below(10);
    region.klass = arch.klass;
    region.details = {
        {"Authority", arch.authority},
        {"Cluster class", arch.form},
        {"Dominant hazard", arch.hazard},
    };
    region.scan = {
        arch.history,
        std::string("Dominant navigation condition: ") + arch.hazard + ".",
    };
    region.scanLocked = {"Deep-cluster gravimetric map", "Long-baseline relay key"};
    catalog.regions.push_back(region);

    catalog.worldHistory[region.id] = {arch.history, arch.traffic};
  }
}

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

void PopulateLocalCharts(Catalog& catalog, std::size_t firstSystem) {
  if (catalog.orbitalWorlds.empty() || catalog.orbitalPlanetNames.empty() ||
      catalog.orbitalMoonNames.empty()) {
    return;
  }
  const std::vector<std::string>& planetNames = catalog.orbitalPlanetNames;
  const std::vector<std::string>& moonNames = catalog.orbitalMoonNames;

  static const std::vector<const char*> kMapFeatures = {
      "Salvage cache", "Ancient nav buoy", "Rich mineral seam",
      "Fuel skimmer depot", "Ion pocket", "Unstable hulk",
  };

  std::unordered_map<std::string, std::string> regionNames;
  for (const Body& region : catalog.regions) { regionNames[region.id] = region.name; }

  // Group already-present local bodies per system once in O(B) instead of
  // rescanning catalog.bodies for every system.
  std::unordered_map<std::string, std::vector<std::size_t>> existingBySystem;
  existingBySystem.reserve(catalog.systems.size() * 2);
  for (std::size_t bi = 0; bi < catalog.bodies.size(); ++bi) {
    existingBySystem[catalog.BodySystem(catalog.bodies[bi])].push_back(bi);
  }

  std::vector<std::string> profiles;
  for (const auto& entry : catalog.portPrices) { profiles.push_back(entry.first); }
  std::sort(profiles.begin(), profiles.end());

  // Which systems already keep a berth: a port entry whose chart body sits in
  // that system. Read once, before the per-system pass, so a system that is
  // given a station here is not given a second one.
  std::unordered_map<std::string, bool> systemHasPort;
  systemHasPort.reserve(catalog.systems.size() * 2);
  {
    std::set<std::string> portIds;
    for (const Port& port : catalog.ports) { portIds.insert(port.id); }
    for (const Body& body : catalog.bodies) {
      if (body.type == "station" && portIds.count(body.id) != 0) {
        systemHasPort[catalog.BodySystem(body)] = true;
      }
    }
  }

  const MapGeometry sysBoard = BoardOrDefault(catalog, "system");
  const double boardW = sysBoard.w > 0 ? sysBoard.w : catalog.map.w;
  const double boardH = sysBoard.h > 0 ? sysBoard.h : catalog.map.h;
  const double cx = sysBoard.cx;
  const double cy = sysBoard.cy;
  const double orbitScale = std::min(boardW, boardH) / 638.0;

  for (std::size_t i = firstSystem; i < catalog.systems.size(); ++i) {
    Body& system = catalog.systems[i];
    if (system.type != "system") { continue; }
    const bool isKessler = (system.id == "sec-kessler");
    const bool isGeneratedSystem = (system.id.rfind("sec-gen-", 0) == 0);
    const std::string side = system.id.substr(4);
    const std::string word = FirstWord(system.name);
    Rng rng(HashOf(system.id));

    const std::string regionName = regionNames[system.region];
    std::vector<std::pair<double, double>> taken;
    std::set<std::string> names;
    std::size_t localCount = 0;
    auto existingIt = existingBySystem.find(system.id);
    if (existingIt != existingBySystem.end()) {
      for (std::size_t bi : existingIt->second) {
        const Body& body = catalog.bodies[bi];
        taken.emplace_back(body.x, body.y);
        names.insert(body.name);
        ++localCount;
      }
    }

    const double minSep = (isKessler ? 38.0 : 28.0) * orbitScale;
    const double edge = 18.0 * orbitScale;
    const auto fits = [&](double x, double y, double sep) {
      if (!InsideCircle(x, y, cx, cy, std::min(boardW, boardH) * 0.5 - edge)) { return false; }
      for (const auto& other : taken) {
        const double dx = other.first - x;
        const double dy = other.second - y;
        if (dx * dx + dy * dy < sep * sep) { return false; }
      }
      return true;
    };

    // Five concentric elliptical orbital bands spanning the 638x278 system chart.
    static constexpr double kRings[5] = {68.0, 112.0, 158.0, 208.0, 262.0};
    static constexpr int kRingSlots[5] = {4, 5, 6, 7, 8};
    const double phaseOffset = rng.Range(0.0, 6.283185307179586);

    const auto slotSpot = [&](int k, double& outX, double& outY) {
      for (int pass = 0; pass < 25; ++pass) {
        const int band = (k + pass) % 5;
        const int slots = kRingSlots[band];
        const int slot = ((k / 5) + pass) % slots;
        const double angle =
            phaseOffset + 0.61 * static_cast<double>(band) +
            6.283185307179586 * (static_cast<double>(slot) / static_cast<double>(slots)) +
            rng.Range(-0.16, 0.16);
        const double ring = kRings[band] * orbitScale;
        const double x = std::round(cx + std::cos(angle) * ring);
        const double y = std::round(cy + std::sin(angle) * ring);
        if (fits(x, y, minSep)) {
          outX = x;
          outY = y;
          return true;
        }
      }
      return false;
    };

    const int targetExtra = isKessler ? 11 : (13 + rng.Below(7));
    std::vector<std::pair<std::string, std::pair<double, double>>> hostPlanets;

    for (int k = 0; k < targetExtra; ++k) {
      const bool makeMoon = !hostPlanets.empty() && (k % 3 == 2);
      const std::string id =
          (makeMoon ? std::string("mn-") : std::string("pl-")) + side + "-p" +
          std::to_string(k + 1);

      if (makeMoon) {
        const auto& host = hostPlanets[static_cast<std::size_t>(rng.Below(static_cast<int>(hostPlanets.size())))];
        double mx = 0.0;
        double my = 0.0;
        bool moonPlaced = false;
        for (int attempt = 0; attempt < 16; ++attempt) {
          const double ang = rng.Range(0.0, 6.283185307179586);
          const double dist = (19.0 + static_cast<double>(attempt % 4) * 3.0) * orbitScale;
          const double tx = std::round(host.second.first + std::cos(ang) * dist);
          const double ty = std::round(host.second.second + std::sin(ang) * dist);
          if (fits(tx, ty, 17.0 * orbitScale) && (!isKessler || fits(tx, ty, 24.0 * orbitScale))) {
            mx = tx;
            my = ty;
            moonPlaced = true;
            break;
          }
        }
        if (!moonPlaced && !slotSpot(k, mx, my)) { continue; }
        taken.emplace_back(mx, my);

        const std::string base =
            moonNames[(i * 5 + static_cast<std::size_t>(k)) % moonNames.size()] + " " + word;
        const std::string name = UniqueName(base, names);
        Body& body = AddLocalBody(catalog, id, "moon", name, mx, my, system,
                                  "A natural satellite holds a close resonance orbit.");
        body.klass = "Natural satellite - " + host.first + " orbit";
        body.details.emplace_back("Primary", host.first);
        body.details.emplace_back("Atmosphere", (k % 4 == 0) ? "Thin nitrogen" : "None");
        body.details.emplace_back("Surface", (k % 2 == 0) ? "Icy regolith" : "Tidal basalt");
        if (isGeneratedSystem && rng.Next() < 0.28) {
          const char* feat = kMapFeatures[static_cast<std::size_t>(rng.Below(4))];
          body.details.emplace_back("Map feature", feat);
        }
        body.scan = {"Tidal flexing warms pockets beneath the crust.",
                     "Orbital period is locked to " + host.first + "."};
        body.scanLocked = {"Subsurface structure", "Mass-concentration chart"};
        ++localCount;
        continue;
      }

      double x = 0.0;
      double y = 0.0;
      if (!slotSpot(k, x, y)) { continue; }
      taken.emplace_back(x, y);

      const std::size_t profileIndex =
          (i * 3 + static_cast<std::size_t>(k)) % catalog.orbitalWorlds.size();
      const OrbitalWorld& profile = catalog.orbitalWorlds[profileIndex];
      const std::string base =
          planetNames[(i * 7 + static_cast<std::size_t>(k) * 3) % planetNames.size()] + " " + word;
      const std::string name = UniqueName(base, names);
      Body& body = AddLocalBody(catalog, id, "planet", name, x, y, system, profile.scan);
      body.subtype = profile.id;
      const bool gasGiant = (k >= 6 && (k % 4 == 1));
      body.klass = gasGiant ? "Gas giant - ringed outer world" : profile.label;
      body.details.emplace_back("Atmosphere", gasGiant ? "Hydrogen-helium" : profile.atmosphere);
      body.details.emplace_back(
          "Orbital band",
          (k < 4) ? "Inner system" : ((k < 9) ? "Habitable / middle band" : "Outer system"));
      if (isGeneratedSystem && rng.Next() < 0.25) {
        const char* feat = kMapFeatures[static_cast<std::size_t>(rng.Below(static_cast<int>(kMapFeatures.size())))];
        body.details.emplace_back("Map feature", feat);
      }
      body.scan = {profile.scan, "Multi-band orbital pass recorded surface contours."};
      body.scanLocked = {profile.locked, "Deep magnetosphere telemetry"};
      hostPlanets.push_back({name, {x, y}});
      ++localCount;
    }

    // Belts, derelict anomalies, and secondary frontier outposts in larger systems.
    const int landmarks = isKessler ? 1 : (1 + rng.Below(3));
    for (int m = 0; m < landmarks; ++m) {
      double lx = 0.0;
      double ly = 0.0;
      if (!slotSpot(targetExtra + m * 2, lx, ly)) { continue; }
      taken.emplace_back(lx, ly);

      if (m == 1 && isGeneratedSystem && !profiles.empty() && rng.Next() < 0.35) {
        // Secondary orbital waystation with a working port.
        const std::string portId = "st-" + side + "-outpost";
        const std::string portName = UniqueName(word + " Outpost", names);
        const std::string profile = Pick(profiles, rng);
        const auto hintIt = catalog.portHints.find(profile);
        const std::string hint = hintIt == catalog.portHints.end() ? std::string() : hintIt->second;
        Body& station = AddLocalBody(
            catalog, portId, "station", portName, lx, ly, system,
            "A compact orbital anchorage services long-range ships." +
                (hint.empty() ? std::string() : " " + hint));
        station.klass = "Orbital outpost - " + profile + " berth";
        station.details.emplace_back("System", system.name);
        station.details.emplace_back("Region", regionName);
        station.details.emplace_back("Services", "Dock, market, refit, commissions");
        station.scan = {"Docking collars and fuel lines report green across the truss."};
        station.scanLocked = {"Harbour manifest archive"};

        Port port;
        port.id = portId;
        port.name = portName;
        port.tip = "Frontier berth in " + system.name + "." +
                   (hint.empty() ? std::string() : " " + hint);
        const auto priceIt = catalog.portPrices.find(profile);
        if (priceIt != catalog.portPrices.end()) { port.prices = priceIt->second; }
        catalog.ports.push_back(std::move(port));
        ++localCount;
        continue;
      }

      const bool belt = (m == 0) ? (rng.Next() < 0.5) : false;
      const std::string lid =
          (belt ? std::string("bt-") : std::string("si-")) + side +
          (belt ? "-field" : (m == 0 ? "-wreck" : (m == 1 ? "-anom" : "-beacon")));
      const std::string lname = UniqueName(
          belt ? (word + " Debris")
               : (m == 0 ? (word + " Wreck")
                         : (Pick(Adjectives(), rng) + (m == 1 ? " Beacon" : " Relay"))),
          names);
      const char* feature =
          belt ? (rng.Next() < 0.5 ? "Rich mineral seam" : "Ion pocket")
               : kMapFeatures[static_cast<std::size_t>(rng.Below(static_cast<int>(kMapFeatures.size())))];
      Body& body = AddLocalBody(
          catalog, lid, belt ? "belt" : "site", lname, lx, ly, system,
          belt ? "Loose material and mineral-rich chondrites drift along the belt."
               : "An unflagged structure emits intermittent transponder pulses.");
      body.details.emplace_back("System", system.name);
      body.details.emplace_back("Region", regionName);
      body.details.emplace_back("Hazard", belt ? "Debris" : "Unstable orbit");
      if (isGeneratedSystem) { body.details.emplace_back("Map feature", feature); }
      if (!belt) { body.klass = "Chart anomaly - unclaimed"; }
      body.scan = {
          belt ? "Spectrometry shows metallic nodules and frozen volatiles."
               : "Hull markings predate the current regional registry.",
          std::string("Survey classification: ") + feature + "."};
      body.scanLocked = {"Interior transponder log", "Sub-hull salvage assay"};
      ++localCount;
    }

    // Every system keeps a station: and generated alike, with no quota
    // and no exceptions. A system that already carries a port keeps the berth
    // and the layout it was with; one that does not is given a station
    // here. The station is placed after the planets so their rings, names and
    // draws are untouched; only the berth is added.
    if (systemHasPort.find(system.id) == systemHasPort.end()) {
      Rng berth(HashOf(system.id + "-berth"));
      double bx = 0.0;
      double by = 0.0;
      bool placed = false;
      for (double sep = minSep; sep >= 10.0 && !placed; sep -= 6.0) {
        for (int pass = 0; pass < 40 && !placed; ++pass) {
          const int band = pass % 5;
          const int slots = kRingSlots[band];
          const int slot = (pass / 5) % slots;
          const double angle =
              phaseOffset + 0.61 * static_cast<double>(band) +
              6.283185307179586 * (static_cast<double>(slot) / static_cast<double>(slots)) +
              berth.Range(-0.10, 0.10);
          const double ring = kRings[band] * orbitScale;
          const double x = std::round(cx + std::cos(angle) * ring);
          const double y = std::round(cy + std::sin(angle) * ring);
          if (!fits(x, y, sep)) { continue; }
          bx = x;
          by = y;
          placed = true;
        }
      }
      if (!placed) {
        // No ring slot is free: take the chart point furthest from anything
        // already placed, so the system still gets its station.
        double bestX = cx;
        double bestY = cy;
        double bestGap = -1.0;
        for (double gx = 24.0; gx <= boardW - 24.0; gx += 12.0) {
          for (double gy = 24.0; gy <= boardH - 24.0; gy += 12.0) {
            double nearest = 1e18;
            for (const auto& other : taken) {
              const double dx = other.first - gx;
              const double dy = other.second - gy;
              nearest = std::min(nearest, dx * dx + dy * dy);
            }
            if (nearest > bestGap) { bestGap = nearest; bestX = gx; bestY = gy; }
          }
        }
        bx = std::round(bestX);
        by = std::round(bestY);
      }
      taken.emplace_back(bx, by);

      static const std::vector<const char*> kStationSuffixes = {
          "Anchorage", "Waystation", "Docks", "Harbour", "Depot", "Terminus",
      };
      const std::string suffix =
          kStationSuffixes[static_cast<std::size_t>(
              berth.Below(static_cast<int>(kStationSuffixes.size())))];
      const std::string stationName = UniqueName(word + " " + suffix, names);
      Body& station = AddLocalBody(
          catalog, std::string("st-") + side, "station", stationName, bx, by, system,
          "A permanent orbital station keeps a berth open here for passing ships.");
      station.klass = "Orbital station - " + regionName + " berth";
      station.details.emplace_back("Services", "Dock, market, refit");
      station.scan = {"Docking collars and fuel lines report green across the truss."};
      station.scanLocked = {"Harbour manifest archive"};

      Port berthPort;
      berthPort.id = station.id;
      berthPort.name = stationName;
      berthPort.tip = "Berth in " + system.name + ".";
      if (!profiles.empty()) {
        const std::size_t pick = static_cast<std::size_t>(
            berth.Below(static_cast<int>(profiles.size())));
        const std::string profile = profiles[pick];
        const auto priceIt = catalog.portPrices.find(profile);
        if (priceIt != catalog.portPrices.end()) { berthPort.prices = priceIt->second; }
        const auto hintIt = catalog.portHints.find(profile);
        if (hintIt != catalog.portHints.end() && !hintIt->second.empty()) {
          berthPort.tip += " " + hintIt->second;
        }
      }
      catalog.ports.push_back(std::move(berthPort));
      ++localCount;
    }

    bool hasBodies = false;
    bool hasNavigable = false;
    for (auto& detail : system.details) {
      if (detail.first == "Charted bodies") {
        detail.second = std::to_string(localCount);
        hasBodies = true;
      } else if (detail.first == "Navigable") {
        detail.second = std::to_string(localCount) + " local destinations";
        hasNavigable = true;
      }
    }
    if (!hasBodies) { system.details.emplace_back("Charted bodies", std::to_string(localCount)); }
    if (!hasNavigable) {
      system.details.emplace_back("Navigable", std::to_string(localCount) + " local destinations");
    }
  }
}

void PlaceGalaxyDots(Catalog& catalog) {
  if (catalog.regions.empty()) { return; }
  catalog.galaxyPositions.clear();
  const MapGeometry galaxyBoard = BoardOrDefault(catalog, "galaxy");
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

void ExpandChart(Catalog& catalog) { ExpandChart(catalog, catalog.chartSettings); }

void ExpandChart(Catalog& catalog, const ChartSettings& settings) {
  if (catalog.chartExpanded) { return; }
  catalog.chartSettings = settings;
  catalog.chartExpanded = true;
  if (catalog.regions.empty() || catalog.systems.empty()) { return; }

  // The loaded chart places are not used. Their tables remain inputs to
  // the generator below; the places themselves are discarded.
  const double baseW = catalog.map.w <= 0 ? 638.0 : catalog.map.w;
  catalog.chartSettings.mapSize = ClampMapSize(settings.mapSize);
  catalog.chartSettings.galaxies = ClampGalaxies(settings.galaxies);
  catalog.chartSettings.mapScale = MapSizeScale(catalog.chartSettings.mapSize);
  const double mapScale = catalog.chartSettings.mapScale;

  // Circular boards: diameter is the current radar width, then +20% per size
  // step. The radar frame is only a window; the map is the circle.
  auto circleBoard = [&](double layerScale) {
    MapGeometry board = catalog.map;
    const double diameter = baseW * layerScale * mapScale;
    board.w = diameter;
    board.h = diameter;
    board.cx = diameter / 2.0;
    board.cy = diameter / 2.0;
    return board;
  };
  MapGeometry sector = circleBoard(kSectorBoardScale);
  MapGeometry galaxy = circleBoard(kGalaxyBoardScale);
  MapGeometry system = circleBoard(1.0);

  catalog.layerBoards["system"] = system;
  catalog.layerBoards["sector"] = sector;
  catalog.layerBoards["galaxy"] = galaxy;
  for (auto& entry : catalog.layerBoards) {
    entry.second.zmin = kZoomMin;
    entry.second.zmax = kZoomMax * mapScale;
    entry.second.zstep = kZoomStep;
  }
  catalog.layerBoards["system"].zmin = kSystemZoomMin;
  catalog.layerBoards["system"].zmax = kSystemZoomMax * mapScale;

  // Discard every loaded chart place. The tables loaded with them stay:
  // prices, port hints, orbital profiles and name lists are generator inputs.
  catalog.regions.clear();
  catalog.systems.clear();
  catalog.bodies.clear();
  catalog.ports.clear();
  catalog.sectorLanes.clear();
  catalog.galaxyLanes.clear();
  catalog.galaxyPositions.clear();
  catalog.worldHistory.clear();
  // Fixed contracts name the discarded places, so they go with them.
  catalog.contracts.clear();
  catalog.RebuildIndex();

  // 2. Generate the galaxy layer on the galaxy board.
  PopulateGalaxies(catalog, 0.0, 0.0);

  // 3. Translate the systems to the centre of the wide sector board and
  //    separate stacked positions.
  const double secDx = (sector.w - baseW) / 2.0;
  const double secDy = (sector.h - baseW) / 2.0;
  for (Body& system : catalog.systems) {
    system.x += secDx;
    system.y += secDy;
  }
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
        system.x = std::round(std::clamp(anchorX + std::cos(angle) * radius, 32.0, sector.w - 32.0));
        system.y = std::round(std::clamp(anchorY + std::sin(angle) * radius * 0.8, 32.0, sector.h - 32.0));
      }
    }
  }

  // Compute each region's territory anchor on the sector board so systems of a
  // galactic region form a navigable cluster with intra-region jump distances
  // well within a single fuel tank.
  std::unordered_map<std::string, std::pair<double, double>> regionAnchor;
  for (std::size_t ri = 0; ri < catalog.regions.size(); ++ri) {
    const Body& region = catalog.regions[ri];
    // Every region is placed by this one rule, whether it came from the file or was
    // generated: no region is the centre and none is exempt.
    const double sx = region.x * (sector.w / galaxy.w);
    const double sy = region.y * (sector.h / galaxy.h);
    regionAnchor[region.id] = {sx, sy};
  }

  // 4. Draft procedural star systems inside each region's sector territory.
  std::set<std::string> systemNames;
  for (const Body& system : catalog.systems) { systemNames.insert(system.name); }

  std::vector<std::string> profiles;
  for (const auto& entry : catalog.portPrices) { profiles.push_back(entry.first); }
  std::sort(profiles.begin(), profiles.end());

  std::vector<std::pair<double, double>> placed;
  placed.reserve(catalog.systems.size() +
                 static_cast<std::size_t>(kNewSystemsPerRegion) * catalog.regions.size());
  for (const Body& system : catalog.systems) { placed.emplace_back(system.x, system.y); }

  Rng rng(settings.seed);
  const double margin = 42.0;
  const double spread = settings.spread <= 0.0 ? 1.0 : settings.spread;
  const std::size_t baseIndex = catalog.systems.size();
  std::vector<StarRow> rows;
  rows.reserve(static_cast<std::size_t>(kNewSystemsPerRegion) * catalog.regions.size());

  int serial = 0;
  for (const Body& region : catalog.regions) {
    const auto anchor = regionAnchor[region.id];
    for (int n = 0; n < kNewSystemsPerRegion; ++n) {
      double x = 0.0;
      double y = 0.0;
      bool found = false;
      for (double minGap = 46.0; minGap >= 20.0 && !found; minGap -= 4.0) {
        Spacer test(minGap, placed);
        for (int attempt = 0; attempt < 180; ++attempt) {
          const double angle = rng.Range(0.0, 6.283185307179586);
          const double radial = std::sqrt(rng.Next());
          const double spreadR = 370.0 * spread;
          const double cx = std::round(anchor.first + std::cos(angle) * radial * spreadR);
          const double cy = std::round(anchor.second + std::sin(angle) * radial * spreadR);
          if (!InsideCircle(cx, cy, sector.cx, sector.cy, sector.w * 0.5 - margin)) { continue; }
          if (test.Fits(cx, cy)) {
            x = cx;
            y = cy;
            found = true;
            break;
          }
        }
      }
      if (!found) { continue; }

      ++serial;
      const std::string systemName =
          UniqueName(Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng), systemNames);
      const std::string starName = systemName + " " + Pick(StarSuffixes(), rng);
      const bool belt = rng.Next() < 0.24;
      const std::string worldName =
          belt ? Pick(WorldStems(), rng) + " Belt"
               : Pick(WorldStems(), rng) + " " + Pick(WorldSuffixes(), rng);
      const std::string landmarkName = Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng);
      const std::string profile =
          (!profiles.empty() && rng.Next() < 0.62) ? Pick(profiles, rng) : std::string();

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
      rows.push_back(std::move(row));
      placed.emplace_back(x, y);
    }
  }

  const std::size_t firstNewSystem = catalog.systems.size();
  BuildGalaxyFromRows(rows, baseIndex, catalog, /*linkHubs=*/false);
  ExpandLocalCharts(catalog, firstNewSystem);

  // 5. Connect intra-region sector lanes in O(R * S_r^2) using regional buckets.
  {
    std::unordered_map<std::string, std::vector<std::size_t>> byRegion;
    for (std::size_t i = 0; i < catalog.systems.size(); ++i) {
      byRegion[catalog.systems[i].region].push_back(i);
    }
    std::set<std::pair<std::string, std::string>> existingLanes;
    for (const Lane& lane : catalog.sectorLanes) {
      existingLanes.insert(lane.from < lane.to ? std::make_pair(lane.from, lane.to)
                                               : std::make_pair(lane.to, lane.from));
    }

    for (const auto& bucket : byRegion) {
      const auto& indices = bucket.second;
      for (std::size_t idx : indices) {
        const Body& system = catalog.systems[idx];
        if (system.id.rfind("sec-gen-", 0) != 0) { continue; }
        std::vector<std::pair<double, std::string>> neighbours;
        neighbours.reserve(indices.size());
        for (std::size_t peerIdx : indices) {
          if (peerIdx == idx) { continue; }
          const Body& other = catalog.systems[peerIdx];
          const double ddx = other.x - system.x;
          const double ddy = other.y - system.y;
          neighbours.emplace_back(ddx * ddx + ddy * ddy, other.id);
        }
        std::sort(neighbours.begin(), neighbours.end());
        const std::size_t links = std::min<std::size_t>(3, neighbours.size());
        for (std::size_t k = 0; k < links; ++k) {
          const std::string& other = neighbours[k].second;
          const auto key = system.id < other ? std::make_pair(system.id, other)
                                             : std::make_pair(other, system.id);
          if (existingLanes.insert(key).second) {
            catalog.sectorLanes.push_back(Lane{system.id, other});
          }
        }
      }
    }
  }

  // 6. Populate multi-ring planetary orbits, galaxy-scale star positions,
  //    inter-galactic lanes, and map-driven incident definitions.
  PopulateLocalCharts(catalog, 0);
  PlaceGalaxyDots(catalog);
  EntangleGalaxy(catalog);
  RegisterMapIncidents(catalog);
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

ChartPreview BuildChartPreview(const Catalog& catalog, const ChartSettings& settings) {
  ChartPreview preview;
  preview.settings = settings;
  const double baseW = catalog.map.w <= 0 ? 638.0 : catalog.map.w;
  ChartSettings applied = settings;
  applied.mapSize = ClampMapSize(settings.mapSize);
  applied.galaxies = ClampGalaxies(settings.galaxies);
  applied.mapScale = MapSizeScale(applied.mapSize);
  preview.settings = applied;
  const double mapScale = applied.mapScale;
  preview.galaxyW = baseW * kGalaxyBoardScale * mapScale;
  preview.galaxyH = preview.galaxyW;
  preview.sectorW = baseW * kSectorBoardScale * mapScale;
  preview.sectorH = preview.sectorW;

  Catalog scratch;
  scratch.map = catalog.map;
  scratch.chartSettings = applied;
  scratch.layerBoards["galaxy"].w = preview.galaxyW;
  scratch.layerBoards["galaxy"].h = preview.galaxyH;
  scratch.layerBoards["galaxy"].cx = preview.galaxyW / 2.0;
  scratch.layerBoards["galaxy"].cy = preview.galaxyH / 2.0;
  scratch.layerBoards["sector"].w = preview.sectorW;
  scratch.layerBoards["sector"].h = preview.sectorH;
  scratch.layerBoards["sector"].cx = preview.sectorW / 2.0;
  scratch.layerBoards["sector"].cy = preview.sectorH / 2.0;
  PopulateGalaxies(scratch, 0.0, 0.0);

  MapGeometry sector = scratch.layerBoards["sector"];
  MapGeometry galaxy = scratch.layerBoards["galaxy"];
  std::unordered_map<std::string, std::pair<double, double>> regionAnchor;
  for (const Body& region : scratch.regions) {
    ChartPreviewDot dot;
    dot.id = region.id;
    dot.name = region.name;
    dot.x = region.x;
    dot.y = region.y;
    preview.galaxies.push_back(dot);
    const double sx = region.x * (sector.w / galaxy.w);
    const double sy = region.y * (sector.h / galaxy.h);
    regionAnchor[region.id] = {sx, sy};
  }

  std::set<std::string> systemNames;
  std::vector<std::string> profiles;
  for (const auto& entry : catalog.portPrices) { profiles.push_back(entry.first); }
  std::sort(profiles.begin(), profiles.end());
  std::vector<std::pair<double, double>> placed;
  Rng rng(settings.seed);
  const double margin = 42.0;
  const double spread = settings.spread <= 0.0 ? 1.0 : settings.spread;
  int serial = 0;
  for (const Body& region : scratch.regions) {
    const auto anchor = regionAnchor[region.id];
    for (int n = 0; n < kNewSystemsPerRegion; ++n) {
      double x = 0.0;
      double y = 0.0;
      bool found = false;
      for (double minGap = 46.0; minGap >= 20.0 && !found; minGap -= 4.0) {
        Spacer test(minGap, placed);
        for (int attempt = 0; attempt < 180; ++attempt) {
          const double angle = rng.Range(0.0, 6.283185307179586);
          const double radial = std::sqrt(rng.Next());
          const double spreadR = 370.0 * spread;
          const double cx = std::round(anchor.first + std::cos(angle) * radial * spreadR);
          const double cy = std::round(anchor.second + std::sin(angle) * radial * spreadR);
          if (!InsideCircle(cx, cy, sector.cx, sector.cy, sector.w * 0.5 - margin)) { continue; }
          if (test.Fits(cx, cy)) {
            x = cx;
            y = cy;
            found = true;
            break;
          }
        }
      }
      if (!found) { continue; }
      ++serial;
      const std::string systemName =
          UniqueName(Pick(Adjectives(), rng) + " " + Pick(Nouns(), rng), systemNames);
      (void)Pick(StarSuffixes(), rng);
      const bool belt = rng.Next() < 0.24;
      if (belt) {
        (void)Pick(WorldStems(), rng);
      } else {
        (void)Pick(WorldStems(), rng);
        (void)Pick(WorldSuffixes(), rng);
      }
      (void)Pick(Adjectives(), rng);
      (void)Pick(Nouns(), rng);
      if (!profiles.empty() && rng.Next() < 0.62) { (void)Pick(profiles, rng); }
      (void)Pick(Observations(), rng);
      ChartPreviewDot dot;
      dot.id = "sec-gen-" + std::to_string(serial);
      dot.name = systemName;
      dot.parent = region.id;
      dot.x = x;
      dot.y = y;
      preview.systems.push_back(dot);
      placed.emplace_back(x, y);
    }
  }
  return preview;
}

}  // namespace ship

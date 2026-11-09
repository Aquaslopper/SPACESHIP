// ============================================================================
//  GalaxyGen.cpp — buildGalaxyContent() and expandLocalCharts(), transcribed.
//  ---------------------------------------------------------------------------
//  The reference's two passes are quoted in the comments above each block so a
//  reader can hold the source against the port. Where the reference leans on
//  JavaScript shorthand (row[8] truthy, i % 4 into ORBITAL_WORLDS), the port
//  spells it out; nothing else is added.
//
//  One difference is deliberate. The reference builds its sky once, so it can
//  append "Navigable" and the sec-pell note unconditionally. The port rebuilds
//  on every new voyage, so those two statements are gated on the row being
//  absent; the catalogue that comes out is the reference's, not the
//  reference's with an extra line per load.
// ============================================================================
#include "data/GalaxyGen.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

namespace ship {

namespace {

// findById(SECTOR_BODIES, row[0]) - the reference may find a system an earlier
// pass already created, and then only annotates it.
Body* FindSystem(Catalog& catalog, const std::string& id) {
  for (Body& system : catalog.systems) {
    if (system.id == id) { return &system; }
  }
  return nullptr;
}

bool HasLane(const Catalog& catalog, const std::string& a, const std::string& b) {
  for (const Lane& lane : catalog.sectorLanes) {
    if ((lane.from == a && lane.to == b) || (lane.from == b && lane.to == a)) { return true; }
  }
  return false;
}

// The reference's fixed pair of scan strings, one per body type.
std::string Reading(const std::string& type) {
  if (type == "star") { return "Stellar output has been charted."; }
  if (type == "station") { return "A working dock answers on the approach channel."; }
  if (type == "planet") { return "Surface contours have been recorded."; }
  if (type == "moon") { return "A natural satellite follows a stable orbit."; }
  if (type == "belt") { return "Loose material is spread across the plotted belt."; }
  return "The landmark has been plotted for later investigation.";
}

std::string Klass(const std::string& type) {
  if (type == "star") { return "Charted star"; }
  if (type == "station") { return "Working station"; }
  if (type == "planet") { return "Charted world"; }
  if (type == "moon") { return "Natural satellite"; }
  if (type == "belt") { return "Debris belt"; }
  return "Unoccupied landmark";
}

}  // namespace

// addSystemBody(): every local body the reference creates goes through here, so
// every local body in the port carries the same five fields in the same order.
Body& AddLocalBody(Catalog& catalog, const std::string& id, const std::string& type,
                    const std::string& name, double x, double y, const Body& system,
                    const std::string& text) {
  Body body;
  body.id = id;
  body.type = type;
  body.name = name;
  body.x = x;
  body.y = y;
  body.system = system.id;
  body.klass = Klass(type);
  body.details.emplace_back("System", system.name);
  for (const Body& region : catalog.regions) {
    if (region.id == system.region) {
      body.details.emplace_back("Region", region.name);
      break;
    }
  }
  body.scan.push_back(text);
  body.scan.push_back(Reading(type));
  body.scanLocked.push_back(type == "station" ? "Station manifest" : "Detailed spectral survey");
  catalog.bodies.push_back(std::move(body));
  return catalog.bodies.back();
}

const std::vector<StarRow>& AuthoredStarCatalog() {
  static const std::vector<StarRow> rows = {
    {"sec-vance", "gal-inner", "Vance Cluster", 120, 70, "Vance A", "Furnace Belt", "Vance Exchange", "industry",
     "Smelter tugs thread a bright trail through the dust."},
    {"sec-orrin", "gal-inner", "Orrin Gate", 470, 60, "Orrin Beacon", "Gateworld", "Orrin Gatehouse", "transit",
     "Old navigation beacons still guide the morning traffic."},
    {"sec-pell", "gal-inner", "Pell Hollow", 540, 180, "Hollow Remnant", "Blackglass", "Grey Dawn Wreck", "",
     "A wreck lies silent behind a veil of black debris."},
    {"sec-bryce", "gal-inner", "Bryce Anchorage", 190, 240, "Bryce Trinary", "Bay Nine", "Bryce Shipyard", "industry",
     "Three suns shine over crowded repair slips."},
    {"sec-marrow", "gal-inner", "Marrow Field", 420, 235, "Marrow Star", "Pale Belt", "Bone Orchard", "",
     "Ancient hulls turn slowly among the stones."},
    {"sec-lumen", "gal-core", "Lumen Crown", 140, 65, "Lumen Prime", "Auric", "Lumen Arcology", "core",
     "A bright metropolis keeps its night side lit."},
    {"sec-caldera", "gal-core", "Caldera Run", 440, 70, "Caldera Nova", "Cinderfall", "Ash Spindle", "",
     "A dark spindle hangs above the ash clouds."},
    {"sec-meridian", "gal-core", "Meridian Accord", 285, 205, "Meridian Star", "Accord", "Meridian Exchange", "core",
     "Convoys assemble around a crowded exchange."},
    {"sec-auriga", "gal-outer", "Auriga Passage", 140, 65, "Auriga Red", "Kestrel", "Auriga Junction", "frontier",
     "Long-haul freighters share the approach lane."},
    {"sec-stillwater", "gal-outer", "Stillwater", 440, 70, "Stillwater Dwarf", "Morrow Ice", "Quiet Observatory", "",
     "A frozen observatory sends a regular beacon."},
    {"sec-fallow", "gal-outer", "Fallow Reach", 285, 205, "Fallow Star", "Gael IV", "Fallow Station", "agri",
     "Greenhouses cover the sheltered side of the station."},
    {"sec-lorn", "gal-verge", "Lorn Verge", 140, 65, "Lorn Ember", "Veil", "Lorn Harbour", "frontier",
     "Harbour lights mark a refuge at the edge of the lanes."},
    {"sec-knell", "gal-verge", "Knell", 440, 70, "Knell Star", "Blackshoal", "Broken Lantern", "",
     "A broken beacon flashes without any traffic."},
    {"sec-thorn", "gal-verge", "Thorn Expanse", 285, 205, "Thorn Sun", "Thornfall", "Gate of Thorns", "",
     "Metallic dust rings a silent transit arch."},
    {"sec-shipgrave", "gal-shoal", "Shipgrave", 140, 65, "Shipgrave Core", "Driftwood", "Salvagers Rest", "salvage",
     "Salvagers sort a field of forgotten vessels."},
    {"sec-palimpsest", "gal-shoal", "Palimpsest", 440, 70, "Palimpsest Star", "Script", "Archive Hull", "",
     "An old archive hull broadcasts fragments of a catalogue."},
    {"sec-echo", "gal-shoal", "Echo Shoal", 285, 205, "Echo Star", "Glasswater", "Choir Array", "",
     "An array repeats an unfamiliar signal at long intervals."},
    {"sec-needle", "gal-rift", "Needle Crossing", 140, 65, "Needle Star", "Razor", "Needle Dock", "frontier",
     "Pilots trade safe passages through the rift."},
    {"sec-rill", "gal-rift", "Rill Drift", 440, 70, "Rill Star", "Silt", "Wreck of Lilt", "",
     "Fine dust has settled on an unclaimed wreck."},
    {"sec-haven", "gal-rift", "Haven Bend", 285, 205, "Hearth Star", "Greyfield", "Haven Anchorage", "agri",
     "A quiet anchorage shelters farms and repair crews."},
    {"sec-frost", "gal-halo", "Frostline", 140, 65, "Frostlight", "Whiteglass", "Frostline Station", "frontier",
     "Ice haulers bring water in from the halo."},
    {"sec-thale", "gal-halo", "Thale", 440, 70, "Thale Star", "Windbound", "Far Bell", "",
     "A lone bell beacon calls through the static."},
    {"sec-ember", "gal-halo", "Last Ember", 285, 205, "Last Ember", "Hollow Frost", "Wayfarer Wreck", "",
     "A tiny ember of a star warms an abandoned ship."},
    {"sec-vesper", "gal-core", "Vesper Spur", 140, 205, "Vesper Star", "Gilt", "Vesper Ring", "core",
     "Couriers weave between the towers of the ring."},
    {"sec-temper", "gal-core", "Temper", 440, 205, "Temper Star", "Kiln", "Soot Archive", "",
     "A soot-dark archive drifts beyond the inner worlds."},
    {"sec-gull", "gal-outer", "Gullward", 140, 205, "Gull Star", "Saltwind", "Gullward Port", "transit",
     "A waystation marks the turn toward the outer lanes."},
    {"sec-cairn", "gal-outer", "Cairn Drift", 440, 205, "Cairn Star", "Stonewake", "Pilgrim Cairn", "",
     "Pilots have stacked markers around an old refuge."},
    {"sec-levin", "gal-verge", "Levin Edge", 140, 205, "Levin Star", "Blue Ash", "Levin Outpost", "frontier",
     "The outpost exchanges weather reports with passing ships."},
    {"sec-lowlight", "gal-verge", "Lowlight", 440, 205, "Lowlight Star", "Duskwater", "Far Buoy", "",
     "A weathered buoy still repeats its lonely position."},
    {"sec-hinge", "gal-shoal", "Hinge Reach", 140, 205, "Hinge Star", "Lockstep", "Hinge Yard", "salvage",
     "Workers cut usable parts from abandoned freighters."},
    {"sec-keeprow", "gal-shoal", "Keeprow", 440, 205, "Keeprow Star", "Old Wake", "Keeper Array", "",
     "A line of listening posts watches the empty lanes."},
    {"sec-spindle", "gal-rift", "Spindle Way", 140, 205, "Spindle Star", "Thread", "Spindle Station", "transit",
     "Supply ships shelter in the lee of a narrow dust lane."},
    {"sec-drawbridge", "gal-rift", "Drawbridge", 440, 205, "Drawbridge Star", "Causeway", "Broken Span", "",
     "A collapsed gate has become a landmark for navigators."},
    {"sec-kite", "gal-halo", "Kiteward", 140, 205, "Kite Star", "White Sail", "Kiteward Haven", "frontier",
     "A small harbour welcomes ships returning from the halo."},
    {"sec-winter", "gal-halo", "Winter Sound", 440, 205, "Winter Star", "Snowmelt", "Distant Choir", "",
     "A quiet transmitter sings beneath the ice."},
    {"sec-ravelin", "gal-inner", "Ravelin Reach", 72, 145, "Ravelin Primary", "Aster Quay", "Ravelin Platform", "transit",
     "Convoy lights curve between the inner moons."},
    {"sec-kharon", "gal-inner", "Kharon Fields", 570, 72, "Kharon B", "Sable Verge", "Sable Exchange", "frontier",
     "Pilots barter for water beneath a dark red sun."},
    {"sec-orison", "gal-core", "Orison Crown", 65, 145, "Orison Prime", "Mosaic", "Orison Exchange", "core",
     "A ring of habitats glitters around a blue-white star."},
    {"sec-ardent", "gal-core", "Ardent Spiral", 535, 145, "Ardent Pair", "Cinderwake", "The Long Archive", "",
     "Two close stars light a drifting library hull."},
    {"sec-morrow", "gal-outer", "Morrow Reach", 285, 60, "Morrow Beacon", "Glass Delta", "Morrow Station", "agri",
     "A green ocean world feeds the outer convoy routes."},
    {"sec-talbot", "gal-outer", "Talbot Yard", 535, 145, "Talbot Red", "Iron Choir", "Talbot Salvage Yard", "salvage",
     "A field of old drives turns slowly above the yard."},
    {"sec-glassfall", "gal-verge", "Glassfall", 285, 145, "Glassfall White", "Blue Shard", "Glassfall Beacon", "frontier",
     "A lone beacon refracts starlight through a crystal belt."},
    {"sec-lacuna", "gal-shoal", "Lacuna Drift", 285, 145, "Lacuna Star", "Quiet Mare", "Null Choir", "",
     "A silent sea of ice hides a warm fissure below."},
    {"sec-novafold", "gal-shoal", "Novafold", 535, 145, "Novafold A", "Paper Moon", "Novafold Exchange", "core",
     "A folded orbital city trades in old survey records."},
    {"sec-nacre", "gal-rift", "Nacre Crossing", 285, 145, "Nacre Pair", "Pearlwind", "Nacre Dock", "transit",
     "A pale dust lane marks the safest passage through the Rift."},
    {"sec-palem", "gal-halo", "Pale Meridian", 285, 145, "Pale Meridian", "Rimeward", "Pale Meridian Station", "frontier",
     "Ice haulers meet beneath a star that barely warms them."},
    {"sec-tern", "gal-halo", "Tern Anchorage", 535, 145, "Tern Star", "Gannet", "Tern Haven", "agri",
     "A chain of greenhouse barges follows the blue dusk."},
  };
  return rows;
}

void BuildGalaxyFromRows(const std::vector<StarRow>& rows, std::size_t baseIndex, Catalog& catalog,
                         bool linkHubs) {
  // var hub = {}; hub["gal-inner"] = "sec-kessler";
  std::map<std::string, std::string> hub;
  hub["gal-inner"] = "sec-kessler";

  for (std::size_t k = 0; k < rows.size(); ++k) {
    const StarRow& row = rows[k];
    const std::size_t i = baseIndex + k;
    const std::string slug = row.system.substr(4);  // row[0].substring(4)
    const std::string entry = slug + "-star";

    Body* system = FindSystem(catalog, row.system);
    if (system == nullptr) {
      Body created;
      created.id = row.system;
      created.type = "system";
      created.name = row.name;
      created.x = row.x;
      created.y = row.y;
      created.klass = "Charted star system";
      created.details.emplace_back("Charted bodies", "3");
      catalog.systems.push_back(std::move(created));
      system = &catalog.systems.back();
      if (linkHubs) {
        auto it = hub.find(row.region);
        if (it != hub.end()) {
          if (!HasLane(catalog, it->second, system->id)) {
            catalog.sectorLanes.push_back(Lane{it->second, system->id});
          }
        } else {
          hub[row.region] = system->id;
        }
      }
    } else {
      // The reference appends unconditionally, which is fine for a page that
      // builds its sky once. The port rebuilds on every new voyage, so the same
      // statements are gated on the row not being there yet; the result is the
      // export's, not the export's plus an extra line per load.
      if (system->id == "sec-pell" && !system->details.empty() &&
          system->details[0].second == "3") {
        system->details[0].second = "2 previously reported; 3 now charted";
      }
      bool hasNavigable = false;
      for (const auto& detail : system->details) {
        if (detail.first == "Navigable") { hasNavigable = true; }
      }
      if (!hasNavigable) { system->details.emplace_back("Navigable", "3 bodies"); }
    }
    system->region = row.region;
    system->entry = entry;

    // addSystemBody(entry, "star", row[5], 100 + (i % 3) * 32, 58 + (i % 2) * 14, sys, row[9]);
    AddLocalBody(catalog, entry, "star", row.star, 100.0 + (i % 3) * 32.0, 58.0 + (i % 2) * 14.0,
                  *system, row.note);

    // addSystemBody(slug + "-world", row[6].indexOf("Belt") >= 0 ? "belt" : "planet",
    //               row[6], 390 + (i % 3) * 28, 64 + (i % 2) * 20, sys, row[9]);
    const bool belt = row.world.find("Belt") != std::string::npos;
    AddLocalBody(catalog, slug + "-world", belt ? "belt" : "planet", row.world,
                  390.0 + (i % 3) * 28.0, 64.0 + (i % 2) * 20.0, *system, row.note);

    // landmark = slug + (row[8] ? "-port" : "-site");
    // addSystemBody(landmark, row[8] ? "station" : "site", row[7], 260 + (i % 3) * 45, 210, sys, row[9]);
    const bool hasPort = !row.profile.empty();
    const std::string landmark = slug + (hasPort ? "-port" : "-site");
    AddLocalBody(catalog, landmark, hasPort ? "station" : "site", row.landmark,
                  260.0 + (i % 3) * 45.0, 210.0, *system, row.note);

    // if (row[8]) { PORTS[landmark] = {name: row[7], prices: PORT_PRICES[row[8]],
    //                                 tip: row[9] + " " + PORT_HINTS[row[8]]}; }
    if (hasPort) {
      const auto prices = catalog.portPrices.find(row.profile);
      const auto hint = catalog.portHints.find(row.profile);
      if (prices != catalog.portPrices.end()) {
        Port port;
        port.id = landmark;
        port.name = row.landmark;
        port.prices = prices->second;
        port.tip = row.note + " " + (hint != catalog.portHints.end() ? hint->second : std::string());
        catalog.ports.push_back(std::move(port));
      }
    }
  }
}

void ExpandLocalCharts(Catalog& catalog, std::size_t firstSystem) {
  const std::vector<std::string>& planetNames = catalog.orbitalPlanetNames;
  const std::vector<std::string>& moonNames = catalog.orbitalMoonNames;
  if (catalog.orbitalWorlds.empty() || planetNames.empty() || moonNames.empty()) { return; }

  for (std::size_t i = firstSystem; i < catalog.systems.size(); ++i) {
    Body& system = catalog.systems[i];
    // word = sys.name.replace(/[^A-Za-z0-9]+/g, " ").replace(/^\s+|\s+$/g, "").split(" ")[0];
    std::string cleaned;
    for (char c : system.name) {
      const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      cleaned.push_back(alnum ? c : ' ');
    }
    const std::size_t begin = cleaned.find_first_not_of(' ');
    const std::string word =
        begin == std::string::npos ? system.name : cleaned.substr(begin, cleaned.find(' ', begin) - begin);

    const OrbitalWorld& profile = catalog.orbitalWorlds[i % catalog.orbitalWorlds.size()];

    // world = addSystemBody(sys.id.substring(4) + "-outer", "planet",
    //   ORBITAL_PLANET_NAMES[i % 12] + " " + word, 470 + (i % 5) * 28, 88 + (i % 5) * 22, sys, profile.scan);
    Body& world = AddLocalBody(catalog, system.id.substr(4) + "-outer", "planet",
                                planetNames[i % planetNames.size()] + " " + word,
                                470.0 + (i % 5) * 28.0, 88.0 + (i % 5) * 22.0, system, profile.scan);
    world.subtype = profile.id;
    world.klass = profile.label;
    world.details.emplace_back("Atmosphere", profile.atmosphere);
    world.details.emplace_back("Orbital band", "Outer system");
    world.scan = {profile.scan, "This world is charted beyond the system's primary traffic lane."};
    world.scanLocked = {profile.locked, "High-resolution orbital survey"};

    // moon = addSystemBody(sys.id.substring(4) + "-moon", "moon",
    //   ORBITAL_MOON_NAMES[(i * 5) % 12] + " " + word, 36 + (i % 5) * 24, 194 + (i % 4) * 16, sys, "A small satellite ...");
    // AddSystemBody appends to catalog.bodies, which may move the vector, so the
    // world's name is copied out before the moon is created.
    const std::string worldName = world.name;
    Body& moon = AddLocalBody(catalog, system.id.substr(4) + "-moon", "moon",
                               moonNames[(i * 5) % moonNames.size()] + " " + word,
                               36.0 + (i % 5) * 24.0, 194.0 + (i % 4) * 16.0, system,
                               "A small satellite traces a quiet path beyond the mapped world.");
    moon.klass = "Natural satellite - stable orbit";
    moon.details.emplace_back("Satellite of", worldName);
    moon.details.emplace_back("Surface", i % 2 ? "Heavily cratered" : "Icy regolith");
    moon.scan = {"A natural satellite follows a stable orbit.",
                 "The surface is " + std::string(i % 2 ? "heavily cratered" : "covered in icy regolith") +
                     "."};
    moon.scanLocked = {"Subsurface structure", "Full orbital mechanics"};

    // The summary rows the system chart prints under the name.
    for (auto& detail : system.details) {
      if (detail.first == "Charted bodies") {
        detail.second = system.id == "sec-kessler" ? "8" : "5";
      } else if (detail.first == "Navigable") {
        detail.second = "5 local destinations";
      }
    }
    if (system.id == "sec-kessler") {
      system.klass = "Eight charted destinations";
    } else if (system.klass == "Charted star system") {
      system.klass = "Five charted destinations";
    }
  }
}

bool IsGeneratedSystem(const Body& system) {
  if (system.entry.empty() || system.entry != system.id.substr(4) + "-star") { return false; }
  for (const auto& detail : system.details) {
    if (detail.first == "Traffic") { return false; }
  }
  return true;
}

void StripGeneratedChart(Catalog& catalog) {
  // Local bodies belong to a system; the hand-written Kessler bodies do not.
  catalog.bodies.erase(std::remove_if(catalog.bodies.begin(), catalog.bodies.end(),
                                      [](const Body& body) { return !body.system.empty(); }),
                       catalog.bodies.end());

  // A port lives on a body, so it goes when its body goes.
  catalog.ports.erase(
      std::remove_if(catalog.ports.begin(), catalog.ports.end(),
                     [&catalog](const Port& port) {
                       for (const Body& body : catalog.bodies) {
                         if (body.id == port.id) { return false; }
                       }
                       return true;
                     }),
      catalog.ports.end());

  // The systems the row pass created go back to the row pass; the six
  // stay, and the pass only annotates them when it meets them again.
  catalog.systems.erase(std::remove_if(catalog.systems.begin(), catalog.systems.end(),
                                       [](const Body& system) { return IsGeneratedSystem(system); }),
                        catalog.systems.end());
}

}  // namespace ship

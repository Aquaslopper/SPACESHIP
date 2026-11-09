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
#include <set>
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

void BuildGalaxyFromRows(const std::vector<StarRow>& rows, std::size_t baseIndex, Catalog& catalog,
                         bool linkHubs) {
  // Each region's hub is whichever of its systems was laid down first; nothing
  // brings a name to the table.
  std::map<std::string, std::string> hub;

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

  // The systems the row pass created go back to the row pass; the authored six
  // stay, and the pass only annotates them when it meets them again.
  catalog.systems.erase(std::remove_if(catalog.systems.begin(), catalog.systems.end(),
                                       [](const Body& system) { return IsGeneratedSystem(system); }),
                        catalog.systems.end());

  // The galaxies PopulateGalaxies lays around the core are drawn again from scratch
  // on the next pass, so they go with the systems. Left behind, every redraw would
  // stack another set of them around the core.
  catalog.regions.erase(std::remove_if(catalog.regions.begin(), catalog.regions.end(),
                                       [](const Body& region) {
                                         return region.id.rfind("gal-gen-", 0) == 0;
                                       }),
                        catalog.regions.end());

  // A lane whose ends are gone is a lane to nowhere: the pass that follows draws the
  // lanes again, so these only survive if they are pruned here.
  {
    std::set<std::string> systemIds;
    for (const Body& system : catalog.systems) { systemIds.insert(system.id); }
    catalog.sectorLanes.erase(
        std::remove_if(catalog.sectorLanes.begin(), catalog.sectorLanes.end(),
                       [&systemIds](const Lane& lane) {
                         return systemIds.count(lane.from) == 0 || systemIds.count(lane.to) == 0;
                       }),
        catalog.sectorLanes.end());
    std::set<std::string> regionIds;
    for (const Body& region : catalog.regions) { regionIds.insert(region.id); }
    catalog.galaxyLanes.erase(
        std::remove_if(catalog.galaxyLanes.begin(), catalog.galaxyLanes.end(),
                       [&regionIds](const Lane& lane) {
                         return regionIds.count(lane.from) == 0 || regionIds.count(lane.to) == 0;
                       }),
        catalog.galaxyLanes.end());
  }
}

}  // namespace ship

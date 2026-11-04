#include "data/Catalog.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace ship {
namespace {

using Json = nlohmann::json;

const Json& Field(const Json& object, const char* key, const std::string& where) {
  auto it = object.find(key);
  if (it == object.end()) {
    throw CatalogError(where + ": missing field '" + key + "'");
  }
  return *it;
}

std::string Text(const Json& object, const char* key, const std::string& where,
                 const std::string& fallback = "") {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) { return fallback; }
  if (it->is_string()) { return it->get<std::string>(); }
  if (!it->is_number() && !it->is_boolean()) {
    throw CatalogError(where + ": field '" + key + "' is not text");
  }
  // The reference stores a few detail values as numbers; keep them printable.
  return it->dump();
}

double Number(const Json& object, const char* key, const std::string& where, double fallback = 0.0) {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) { return fallback; }
  if (!it->is_number()) {
    throw CatalogError(where + ": field '" + key + "' is not a number");
  }
  return it->get<double>();
}

bool Flag(const Json& object, const char* key) {
  auto it = object.find(key);
  return it != object.end() && it->is_boolean() && it->get<bool>();
}

Rect ReadRect(const Json& object, const std::string& where) {
  Rect r;
  r.x = Number(object, "x", where);
  r.y = Number(object, "y", where);
  r.w = Number(object, "w", where);
  r.h = Number(object, "h", where);
  return r;
}

Json LoadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw CatalogError("cannot open " + path.string() +
                       " (run: node tools/export-data.cjs)");
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  try {
    return Json::parse(buffer.str());
  } catch (const Json::exception& e) {
    throw CatalogError(path.filename().string() + ": " + e.what());
  }
}

DeckPlan ReadDeckPlan(const Json& hulls, const Json& corridors, const Json& rooms,
                      const std::string& key) {
  const std::string where = "ship.json/" + key;
  DeckPlan plan;

  for (const Json& part : Field(hulls, key.c_str(), "ship.json/hulls")) {
    HullPart p;
    p.id = Text(part, "id", where + "/hull");
    p.cls = Text(part, "cls", where + "/hull");
    p.box = ReadRect(part, where + "/hull");
    plan.hull.push_back(p);
  }

  const Json& corridor = Field(corridors, key.c_str(), "ship.json/corridors");
  plan.corridor = ReadRect(corridor, where + "/corridor");

  for (const Json& room : Field(rooms, key.c_str(), "ship.json/rooms")) {
    Room r;
    r.id = Text(room, "id", where + "/rooms");
    r.name = Text(room, "name", where + "/rooms");
    r.systems = Text(room, "systems", where + "/rooms");
    r.tag = Text(room, "tag", where + "/rooms");
    r.tone = Text(room, "tone", where + "/rooms");
    r.box = ReadRect(room, where + "/rooms/" + r.id);
    r.small = Flag(room, "small");
    plan.rooms.push_back(r);
  }
  return plan;
}

Body ReadBody(const Json& object, const std::string& where) {
  Body b;
  b.id = Text(object, "id", where);
  b.type = Text(object, "type", where);
  b.name = Text(object, "name", where);
  b.klass = Text(object, "klass", where);
  b.x = Number(object, "x", where);
  b.y = Number(object, "y", where);
  b.system = Text(object, "system", where);
  b.region = Text(object, "region", where);
  b.entry = Text(object, "entry", where);
  b.subtype = Text(object, "subtype", where);
  if (auto it = object.find("cluster"); it != object.end() && it->is_number()) {
    b.cluster = it->get<int>();
  }
  if (auto it = object.find("details"); it != object.end() && it->is_array()) {
    for (const Json& row : *it) {
      if (row.is_array() && row.size() == 2) {
        b.details.emplace_back(row[0].is_string() ? row[0].get<std::string>() : row[0].dump(),
                               row[1].is_string() ? row[1].get<std::string>() : row[1].dump());
      }
    }
  }
  for (const char* key : {"scan", "scanLocked"}) {
    auto it = object.find(key);
    if (it == object.end() || !it->is_array()) { continue; }
    std::vector<std::string>& target = std::string(key) == "scan" ? b.scan : b.scanLocked;
    for (const Json& line : *it) {
      if (line.is_string()) { target.push_back(line.get<std::string>()); }
    }
  }
  return b;
}

}  // namespace

const std::filesystem::path& Catalog::DefaultAssetDir() {
  static const std::filesystem::path dir = [] {
#ifdef SPACESHIP_ASSET_DIR
    return std::filesystem::path(SPACESHIP_ASSET_DIR);
#else
    return std::filesystem::path("assets");
#endif
  }();
  return dir;
}

Catalog Catalog::LoadFromAssets(const std::filesystem::path& assetDir) {
  const std::filesystem::path data = assetDir / "data";
  Catalog catalog;

  const Json manifest = LoadFile(data / "manifest.json");
  const std::string format = Text(manifest, "format", "manifest.json");
  if (format != "spaceship-catalog-1") {
    throw CatalogError("manifest.json: unsupported catalogue format '" + format +
                       "' (expected spaceship-catalog-1)");
  }
  const Json& source = Field(manifest, "source", "manifest.json");
  catalog.sourceSha256 = Text(source, "sha256", "manifest.json/source");
  catalog.sourceBytes = static_cast<long long>(Number(source, "bytes", "manifest.json/source"));

  // ---- ship --------------------------------------------------------------
  const Json ship = LoadFile(data / "ship.json");
  catalog.wren = ReadDeckPlan(Field(ship, "hulls", "ship.json"),
                              Field(ship, "corridors", "ship.json"),
                              Field(ship, "rooms", "ship.json"), "wren");
  catalog.kestrel = ReadDeckPlan(Field(ship, "hulls", "ship.json"),
                                 Field(ship, "corridors", "ship.json"),
                                 Field(ship, "rooms", "ship.json"), "kestrel");
  catalog.counts.fittedRooms =
      static_cast<int>(Field(ship, "roomFits", "ship.json").size());
  catalog.counts.ships = static_cast<int>(Field(ship, "ships", "ship.json").size());
  catalog.counts.roomsWren = static_cast<int>(catalog.wren.rooms.size());
  catalog.counts.roomsKestrel = static_cast<int>(catalog.kestrel.rooms.size());

  // ---- chart -------------------------------------------------------------
  const Json chart = LoadFile(data / "chart.json");
  const Json& map = Field(chart, "map", "chart.json");
  catalog.map.w = Number(map, "w", "chart.json/map");
  catalog.map.h = Number(map, "h", "chart.json/map");
  catalog.map.cx = Number(map, "cx", "chart.json/map");
  catalog.map.cy = Number(map, "cy", "chart.json/map");
  catalog.map.zmin = Number(map, "zmin", "chart.json/map", 0.6);
  catalog.map.zmax = Number(map, "zmax", "chart.json/map", 2.0);
  catalog.map.zstep = Number(map, "zstep", "chart.json/map", 0.2);
  catalog.map.grid = Number(map, "grid", "chart.json/map", 45);
  catalog.map.vpy = Number(map, "vpy", "chart.json/map", 0.35);
  catalog.map.rungs = Number(map, "rungs", "chart.json/map", 9);
  catalog.map.rungPow = Number(map, "rungPow", "chart.json/map", 2.1);
  catalog.map.pxPerAction = Number(map, "pxPerAction", "chart.json/map", 16);
  catalog.map.lyPerAction = Number(map, "lyPerAction", "chart.json/map", 0.42);

  for (const Json& region : Field(chart, "galaxyBodies", "chart.json")) {
    catalog.regions.push_back(ReadBody(region, "chart.json/galaxyBodies"));
  }
  for (const Json& system : Field(chart, "sectorBodies", "chart.json")) {
    catalog.systems.push_back(ReadBody(system, "chart.json/sectorBodies"));
  }
  for (const Json& body : Field(chart, "bodies", "chart.json")) {
    catalog.bodies.push_back(ReadBody(body, "chart.json/bodies"));
  }
  for (const Json& lane : Field(chart, "sectorLinks", "chart.json")) {
    catalog.sectorLanes.push_back({lane.at(0).get<std::string>(), lane.at(1).get<std::string>()});
  }
  for (const Json& lane : Field(chart, "galaxyLinks", "chart.json")) {
    catalog.galaxyLanes.push_back({lane.at(0).get<std::string>(), lane.at(1).get<std::string>()});
  }

  const Json& ports = Field(chart, "ports", "chart.json");
  for (auto it = ports.begin(); it != ports.end(); ++it) {
    Port port;
    port.id = it.key();
    port.name = Text(it.value(), "name", "chart.json/ports/" + port.id);
    port.tip = Text(it.value(), "tip", "chart.json/ports/" + port.id);
    if (auto prices = it.value().find("prices"); prices != it.value().end() && prices->is_object()) {
      for (auto p = prices->begin(); p != prices->end(); ++p) {
        port.prices[p.key()] = p.value().get<double>();
      }
    }
    catalog.ports.push_back(port);
  }

  for (const Json& good : Field(chart, "market", "chart.json")) {
    Good g;
    g.id = Text(good, "id", "chart.json/market");
    g.name = Text(good, "name", "chart.json/market");
    g.kind = Text(good, "kind", "chart.json/market");
    g.vol = static_cast<int>(Number(good, "vol", "chart.json/market"));
    g.value = Number(good, "value", "chart.json/market");
    g.risk = Flag(good, "risk");
    catalog.goods.push_back(g);
  }

  // ---- progression -------------------------------------------------------
  const Json progression = LoadFile(data / "progression.json");
  for (const Json& contract : Field(progression, "contracts", "progression.json")) {
    Contract c;
    c.id = Text(contract, "id", "progression.json/contracts");
    c.title = Text(contract, "title", "progression.json/contracts");
    c.goal = Text(contract, "goal", "progression.json/contracts");
    c.target = Text(contract, "target", "progression.json/contracts");
    c.origin = Text(contract, "origin", "progression.json/contracts");
    c.note = Text(contract, "note", "progression.json/contracts");
    c.reward = Number(contract, "reward", "progression.json/contracts");
    c.vol = static_cast<int>(Number(contract, "vol", "progression.json/contracts"));
    c.recognizePrior = Flag(contract, "recognizePrior");
    catalog.contracts.push_back(c);
  }
  catalog.counts.incidents =
      static_cast<int>(Field(progression, "incidents", "progression.json").size());
  catalog.counts.projects =
      static_cast<int>(Field(progression, "projects", "progression.json").size());
  catalog.counts.commendations =
      static_cast<int>(Field(progression, "commendations", "progression.json").size());

  // ---- content -----------------------------------------------------------
  const Json content = LoadFile(data / "content.json");
  for (const Json& kind : Field(content, "taskTypes", "content.json")) {
    catalog.taskTypes.push_back(Text(kind, "type", "content.json/taskTypes"));
  }
  catalog.counts.wiki = static_cast<int>(Field(content, "wiki", "content.json").size());
  catalog.counts.themes = static_cast<int>(Field(content, "themes", "content.json").size());
  catalog.counts.taskTypes = static_cast<int>(catalog.taskTypes.size());

  // ---- crew and survival (counted now, consumed from Phase 1) ------------
  const Json crew = LoadFile(data / "crew.json");
  catalog.counts.crew = static_cast<int>(Field(crew, "crew", "crew.json").size());
  catalog.counts.hires = static_cast<int>(Field(crew, "candidates", "crew.json").size());
  catalog.counts.jobs = static_cast<int>(Field(crew, "jobs", "crew.json").size());
  catalog.counts.orders = static_cast<int>(Field(crew, "orders", "crew.json").size());

  const Json survival = LoadFile(data / "survival.json");
  catalog.counts.commodities =
      static_cast<int>(Field(survival, "goods", "survival.json").size());

  // ---- totals ------------------------------------------------------------
  catalog.counts.regions = static_cast<int>(catalog.regions.size());
  catalog.counts.systems = static_cast<int>(catalog.systems.size());
  catalog.counts.bodies = static_cast<int>(catalog.bodies.size());
  catalog.counts.ports = static_cast<int>(catalog.ports.size());
  catalog.counts.lanes = static_cast<int>(catalog.sectorLanes.size());
  catalog.counts.contracts = static_cast<int>(catalog.contracts.size());

  return catalog;
}

const Body* Catalog::FindBody(const std::string& id) const {
  for (const Body& b : bodies) { if (b.id == id) { return &b; } }
  return nullptr;
}

const Body* Catalog::FindSystem(const std::string& id) const {
  for (const Body& b : systems) { if (b.id == id) { return &b; } }
  return nullptr;
}

const Body* Catalog::FindRegion(const std::string& id) const {
  for (const Body& b : regions) { if (b.id == id) { return &b; } }
  return nullptr;
}

const Port* Catalog::FindPort(const std::string& id) const {
  for (const Port& p : ports) { if (p.id == id) { return &p; } }
  return nullptr;
}

std::string Catalog::BodySystem(const Body& body) const {
  return body.system.empty() ? std::string("sec-kessler") : body.system;
}

std::vector<std::string> Catalog::Validate() const {
  std::vector<std::string> problems;

  // ---- unique identifiers across all three layers ------------------------
  {
    std::set<std::string> seen;
    for (const std::vector<Body>* layer : {&regions, &systems, &bodies}) {
      for (const Body& b : *layer) {
        if (b.id.empty()) { problems.push_back("a chart body has an empty id"); continue; }
        if (!seen.insert(b.id).second) { problems.push_back("duplicate chart id: " + b.id); }
      }
    }
  }

  // ---- every body sits on the drawing board ------------------------------
  for (const std::vector<Body>* layer : {&regions, &systems, &bodies}) {
    for (const Body& b : *layer) {
      if (b.x < 0 || b.x > map.w || b.y < 0 || b.y > map.h) {
        std::ostringstream out;
        out << b.id << " is off the chart board (" << b.x << "," << b.y << ")";
        problems.push_back(out.str());
      }
    }
  }

  // ---- systems: known region, and a local entry body ---------------------
  for (const Body& system : systems) {
    if (system.region.empty() || FindRegion(system.region) == nullptr) {
      problems.push_back(system.id + " has no known region ('" + system.region + "')");
    }
    if (system.entry.empty()) {
      problems.push_back(system.id + " has no entry body");
      continue;
    }
    const Body* entry = FindBody(system.entry);
    if (entry == nullptr) {
      problems.push_back(system.id + " entry body '" + system.entry + "' does not exist");
    } else if (BodySystem(*entry) != system.id) {
      problems.push_back(system.id + " entry body belongs to " + BodySystem(*entry));
    }
  }

  // ---- ports: a station body with the same name -------------------------
  for (const Port& port : ports) {
    const Body* body = FindBody(port.id);
    if (body == nullptr) {
      problems.push_back("port '" + port.id + "' has no chart body");
    } else if (body->type != "station") {
      problems.push_back("port '" + port.id + "' is a " + body->type + ", not a station");
    } else if (body->name != port.name) {
      problems.push_back("port '" + port.id + "' name disagrees with its body");
    }
  }

  // ---- lanes join two systems inside one region -------------------------
  for (const Lane& lane : sectorLanes) {
    const Body* a = FindSystem(lane.from);
    const Body* b = FindSystem(lane.to);
    if (a == nullptr || b == nullptr) {
      problems.push_back("sector lane " + lane.from + " -> " + lane.to + " has a missing end");
    } else if (a->region != b->region) {
      problems.push_back("sector lane " + lane.from + " -> " + lane.to + " crosses regions");
    }
  }

  // ---- contracts point at real places -----------------------------------
  for (const Contract& contract : contracts) {
    if (FindBody(contract.target) == nullptr && FindSystem(contract.target) == nullptr &&
        FindPort(contract.target) == nullptr) {
      problems.push_back("contract '" + contract.id + "' targets '" + contract.target + "'");
    }
    if (!contract.origin.empty() && FindPort(contract.origin) == nullptr) {
      problems.push_back("contract '" + contract.id + "' has no origin port");
    }
    if ((contract.goal == "freight" || contract.goal == "passenger") &&
        FindPort(contract.target) == nullptr) {
      problems.push_back("contract '" + contract.id + "' has no destination port");
    }
  }

  // ---- deck plans --------------------------------------------------------
  for (const DeckPlan* plan : {&wren, &kestrel}) {
    const bool isWren = plan == &wren;
    const char* name = isWren ? "wren" : "kestrel";
    std::set<std::string> ids;
    if (plan->hull.empty()) { problems.push_back(std::string(name) + " hull is empty"); }
    for (const Room& room : plan->rooms) {
      if (!ids.insert(room.id).second) {
        problems.push_back(std::string(name) + " deck plan repeats room '" + room.id + "'");
      }
      if (room.box.w <= 0 || room.box.h <= 0) {
        problems.push_back(std::string(name) + " room '" + room.id + "' has no size");
      }
    }
    if (plan->rooms.size() != 21) {
      problems.push_back(std::string(name) + " deck plan has " +
                         std::to_string(plan->rooms.size()) + " rooms, expected 21");
    }
  }

  return problems;
}

}  // namespace ship

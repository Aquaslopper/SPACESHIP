#include "data/Catalog.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace ship {
namespace {

using Json = nlohmann::json;

const Json& Field(const Json& object, const char* key, const char* where) {
  auto it = object.find(key);
  if (it == object.end()) {
    throw CatalogError(std::string(where) + ": missing field '" + key + "'");
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

CrewDefinition ReadCrew(const Json& object) {
  CrewDefinition c;
  c.id = Text(object, "id", "crew");
  c.name = Text(object, "name", "crew");
  c.role = Text(object, "role", "crew");
  c.room = Text(object, "room", "crew");
  c.activity = Text(object, "activity", "crew");
  c.colour = Text(object, "colour", "crew");
  c.sex = Text(object, "sex", "crew");
  c.hair = Text(object, "hair", "crew");
  c.hair2 = Text(object, "hair2", "crew");
  c.skin = Text(object, "skin", "crew");
  c.hairStyle = Text(object, "hairStyle", "crew");
  c.health = Number(object, "health", "crew");
  c.morale = Number(object, "morale", "crew");
  c.fatigue = Number(object, "fatigue", "crew");
  c.hunger = Number(object, "hunger", "crew");
  c.wallet = Number(object, "wallet", "crew");
  c.hungerTick = Number(object, "hungerTick", "crew");
  c.traits = Text(object, "traits", "crew");
  c.race = Text(object, "race", "crew");
  c.age = static_cast<int>(Number(object, "age", "crew"));
  c.homeworld = Text(object, "homeworld", "crew");
  c.hobby = Text(object, "hobby", "crew");
  c.religion = Text(object, "religion", "crew");
  c.job = Text(object, "job", "crew");
  c.skill = Text(object, "skill", "crew");
  c.jobAt = Number(object, "jobAt", "crew");
  return c;
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
  for (auto it = Field(ship, "roomScenes", "ship.json").begin();
       it != Field(ship, "roomScenes", "ship.json").end(); ++it) {
    const std::string where2 = "ship.json/roomScenes/" + it.key();
    RoomScene scene;
    scene.glyph = Text(it.value(), "glyph", where2);
    scene.code = Text(it.value(), "code", where2);
    scene.label = Text(it.value(), "label", where2);
    scene.description = Text(it.value(), "description", where2);
    scene.accent = Text(it.value(), "accent", where2);
    catalog.roomScenes[it.key()] = scene;
  }
  catalog.counts.fittedRooms =
      static_cast<int>(Field(ship, "roomFits", "ship.json").size());
  catalog.counts.ships = static_cast<int>(Field(ship, "ships", "ship.json").size());
  for (auto it = Field(ship, "cargoByClass", "ship.json").begin();
       it != Field(ship, "cargoByClass", "ship.json").end(); ++it) {
    for (const Json& level : it.value()) { catalog.cargoByClass[it.key()].push_back(level.get<double>()); }
  }
  for (auto it = Field(ship, "ships", "ship.json").begin(); it != Field(ship, "ships", "ship.json").end(); ++it) {
    std::map<std::string, std::string> fields;
    for (auto field = it.value().begin(); field != it.value().end(); ++field) {
      if (field.value().is_string()) { fields[field.key()] = field.value().get<std::string>(); }
    }
    catalog.shipFields[it.key()] = fields;
  }
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
  for (const Json& layer : Field(chart, "layerOrder", "chart.json")) {
    catalog.layerOrder.push_back(layer.get<std::string>());
  }
  catalog.map.vpy = Number(map, "vpy", "chart.json/map", 0.35);
  catalog.map.rungs = Number(map, "rungs", "chart.json/map", 9);
  catalog.map.rungPow = Number(map, "rungPow", "chart.json/map", 2.1);
  catalog.map.pxPerAction = Number(map, "pxPerAction", "chart.json/map", 16);
  catalog.map.lyPerAction = Number(map, "lyPerAction", "chart.json/map", 0.42);

  // Every scale is drawn on the board until ExpandChart() widens the
  // sector one. Keeping the entry here means a catalogue that is never expanded
  // still answers for all three layers.
  for (const std::string& layer : catalog.layerOrder) {
    catalog.layerBoards[layer] = catalog.map;
  }

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

  // ---- the tables the generator reads ------------------------------------
  // BUILD reads these before it builds anything (see data/GalaxyGen.h):
  // PORT_PRICES, PORT_HINTS, ORBITAL_WORLDS and the two name tables. They are
  //, so they stay data; only the chart is computed from them.
  const Json& prices = Field(chart, "portPrices", "chart.json");
  for (auto it = prices.begin(); it != prices.end(); ++it) {
    if (!it.value().is_object()) { continue; }
    for (auto p = it.value().begin(); p != it.value().end(); ++p) {
      catalog.portPrices[it.key()][p.key()] = p.value().get<double>();
    }
  }
  const Json& hints = Field(chart, "portHints", "chart.json");
  for (auto it = hints.begin(); it != hints.end(); ++it) {
    catalog.portHints[it.key()] = it.value().get<std::string>();
  }
  for (const Json& world : Field(chart, "orbitalWorlds", "chart.json")) {
    OrbitalWorld profile;
    profile.id = Text(world, "id", "chart.json/orbitalWorlds");
    profile.label = Text(world, "label", "chart.json/orbitalWorlds");
    profile.atmosphere = Text(world, "atmo", "chart.json/orbitalWorlds");
    profile.scan = Text(world, "scan", "chart.json/orbitalWorlds");
    profile.locked = Text(world, "locked", "chart.json/orbitalWorlds");
    catalog.orbitalWorlds.push_back(std::move(profile));
  }
  for (const Json& name : Field(chart, "orbitalPlanetNames", "chart.json")) {
    catalog.orbitalPlanetNames.push_back(name.get<std::string>());
  }
  for (const Json& name : Field(chart, "orbitalMoonNames", "chart.json")) {
    catalog.orbitalMoonNames.push_back(name.get<std::string>());
  }

  for (const Json& good : Field(chart, "market", "chart.json")) {    Good g;
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
    c.cargo = Text(contract, "cargo", "progression.json/contracts");
    c.recognizePrior = Flag(contract, "recognizePrior");
    catalog.contracts.push_back(c);
  }
  for (const Json& incident : Field(progression, "incidents", "progression.json")) {
    IncidentType type;
    type.id = Text(incident, "id", "incidents");
    type.kind = Text(incident, "kind", "incidents");
    type.source = Text(incident, "source", "incidents");
    type.title = Text(incident, "title", "incidents");
    type.remedy = Text(incident, "remedy", "incidents");
    type.first = Number(incident, "first", "incidents");
    type.every = Number(incident, "every", "incidents");
    if (incident.contains("requiresKit") && incident["requiresKit"].is_boolean()) {
      type.requiresKit = incident["requiresKit"].get<bool>();
    }
    for (const Json& stage : Field(incident, "stages", "incidents")) {
      IncidentType::Stage parsed;
      parsed.text = Text(stage, "text", "incidents/stages");
      parsed.ticks = Number(stage, "ticks", "incidents/stages");
      for (const char* key : {"hull", "shield", "energy", "morale"}) {
        auto it = stage.find(key);
        if (it != stage.end() && it->is_number()) {
          const double v = it->get<double>();
          if (std::string(key) == "hull") { parsed.hull = v; }
          else if (std::string(key) == "shield") { parsed.shield = v; }
          else if (std::string(key) == "energy") { parsed.energy = v; }
          else { parsed.morale = v; }
        }
      }
      type.stages.push_back(parsed);
    }
    catalog.incidentTypes.push_back(type);
  }
  for (const Json& award : Field(progression, "commendations", "progression.json")) {
    Commendation c;
    c.id = Text(award, "id", "commendations");
    c.title = Text(award, "title", "commendations");
    c.metric = Text(award, "metric", "commendations");
    c.goal = Number(award, "goal", "commendations");
    c.detail = Text(award, "detail", "commendations");
    if (award.contains("reward") && award["reward"].is_object()) {
      const Json& reward = award["reward"];
      if (reward.contains("cash")) { c.reward.cash = reward["cash"].get<double>(); }
      if (reward.contains("fuel")) { c.reward.fuel = reward["fuel"].get<double>(); }
      if (reward.contains("kits")) { c.reward.kits = reward["kits"].get<double>(); }
      if (reward.contains("med")) { c.reward.med = reward["med"].get<double>(); }
    }
    catalog.commendations.push_back(c);
  }
  for (auto it = Field(progression, "projects", "progression.json").begin();
       it != Field(progression, "projects", "progression.json").end(); ++it) {
    ProjectDef p;
    p.id = it.key();
    p.label = Text(it.value(), "label", "projects");
    p.room = Text(it.value(), "room", "projects");
    p.lot = Text(it.value(), "lot", "projects");
    p.energy = Number(it.value(), "energy", "projects");
    p.work = Number(it.value(), "work", "projects");
    catalog.projects.push_back(p);
  }
  for (auto it = Field(progression, "worldHistory", "progression.json").begin();
       it != Field(progression, "worldHistory", "progression.json").end(); ++it) {
    std::vector<std::string> lines;
    for (const Json& line : it.value()) { lines.push_back(line.get<std::string>()); }
    catalog.worldHistory[it.key()] = lines;
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
  for (auto it = Field(content, "chargeRules", "content.json").begin();
       it != Field(content, "chargeRules", "content.json").end(); ++it) {
    ChargeRule rule;
    rule.volume = Number(it.value(), "volume", "chargeRules");
    rule.label = Text(it.value(), "label", "chargeRules");
    catalog.rules.charges[it.key()] = rule;
  }
  for (const Json& entry : Field(content, "wiki", "content.json")) {
    WikiEntry wiki;
    wiki.id = Text(entry, "id", "wiki");
    wiki.group = Text(entry, "group", "wiki");
    wiki.title = Text(entry, "title", "wiki");
    wiki.text = Text(entry, "text", "wiki");
    wiki.link = Text(entry, "link", "wiki");
    wiki.linkLabel = Text(entry, "linkLabel", "wiki");
    wiki.spoiler = wiki.title.find("(spoilers)") != std::string::npos;
    catalog.wiki.push_back(wiki);
  }
  catalog.counts.wiki = static_cast<int>(catalog.wiki.size());
  catalog.counts.themes = static_cast<int>(Field(content, "themes", "content.json").size());
  catalog.counts.taskTypes = static_cast<int>(catalog.taskTypes.size());

  // ---- crew and survival (counted now, consumed from Phase 1) ------------
  const Json crew = LoadFile(data / "crew.json");
  for (const Json& member : Field(crew, "crew", "crew.json")) {
    catalog.startingCrew.push_back(ReadCrew(member));
  }
  catalog.counts.crew = static_cast<int>(Field(crew, "crew", "crew.json").size());
  for (const Json& candidate : Field(crew, "candidates", "crew.json")) {
    CandidateDefinition def;
    def.crew = ReadCrew(candidate);
    def.port = Text(candidate, "port", "crew.json/candidates");
    def.fee = Number(candidate, "fee", "crew.json/candidates");
    catalog.candidates.push_back(def);
  }
  catalog.counts.hires = static_cast<int>(Field(crew, "candidates", "crew.json").size());
  catalog.counts.jobs = static_cast<int>(Field(crew, "jobs", "crew.json").size());
  catalog.counts.orders = static_cast<int>(Field(crew, "orders", "crew.json").size());
  for (const Json& job : Field(crew, "jobs", "crew.json")) {
    Catalog::JobDefinition def;
    def.id = Text(job, "id", "crew.json/jobs");
    def.label = Text(job, "label", "crew.json/jobs");
    def.unique = Flag(job, "unique");
    def.wage = Number(job, "wage", "crew.json/jobs");
    def.cost = Number(job, "cost", "crew.json/jobs", 1);
    def.learnOn = Number(job, "learnOn", "crew.json/jobs");
    def.learnSt = Number(job, "learnSt", "crew.json/jobs");
    def.feeOn = Number(job, "feeOn", "crew.json/jobs");
    def.feeSt = Number(job, "feeSt", "crew.json/jobs");
    def.trainStep = Number(job, "trainStep", "crew.json/jobs");
    def.trainCap = Number(job, "trainCap", "crew.json/jobs");
    def.unit = Text(job, "unit", "crew.json/jobs");
    def.cloth = Text(job, "cloth", "crew.json/jobs");
    def.dark = Text(job, "dark", "crew.json/jobs");
    catalog.jobDefs.push_back(def);
  }
  for (const Json& order : Field(crew, "orders", "crew.json")) {
    Catalog::OrderDefinition def;
    def.id = Text(order, "id", "crew.json/orders");
    def.label = Text(order, "label", "crew.json/orders");
    def.note = Text(order, "note", "crew.json/orders");
    catalog.orderDefs.push_back(def);
  }

  // --- the quarters art ---------------------------------------------------
  {
    const Json art = LoadFile(data / "crew-art.json");
    const auto frame = [](const Json& rows) {
      SpriteFrame out;
      for (const Json& row : rows) { out.rows.push_back(row.get<std::string>()); }
      return out;
    };
    catalog.spritePx = static_cast<int>(Number(art, "px", "crew-art.json", 4));
    catalog.floorY = Number(art, "floory", "crew-art.json", 230);
    catalog.sprite = frame(Field(art, "sprite", "crew-art.json"));
    catalog.lie = frame(Field(art, "lie", "crew-art.json"));
    catalog.sit = frame(Field(art, "sit", "crew-art.json"));
    catalog.spriteBlank = Text(art, "spBlank", "crew-art.json");
    const Json& props = Field(art, "props", "crew-art.json");
    for (auto it = props.begin(); it != props.end(); ++it) {
      catalog.props[it.key()] = frame(it.value());
    }
    const Json& hats = Field(art, "hats", "crew-art.json");
    for (auto it = hats.begin(); it != hats.end(); ++it) {
      catalog.hats[it.key()] = frame(it.value());
    }
    const Json& jobHat = Field(art, "jobHat", "crew-art.json");
    for (auto it = jobHat.begin(); it != jobHat.end(); ++it) {
      catalog.jobHat[it.key()] = {it.value().at(0).get<std::string>(),
                                  it.value().at(1).get<std::string>()};
    }
    const Json& tools = Field(art, "jobTool", "crew-art.json");
    for (auto it = tools.begin(); it != tools.end(); ++it) {
      catalog.jobTool[it.key()] = it.value().get<std::string>();
    }
    for (const Json& style : Field(art, "hairStyles", "crew-art.json")) {
      HairStyle hair;
      hair.id = Text(style, "id", "crew-art.json/hairStyles");
      for (const Json& row : Field(style, "rows", "crew-art.json/hairStyles")) {
        hair.rows[static_cast<int>(Number(row, "row", "crew-art.json"))] =
            Text(row, "text", "crew-art.json");
      }
      catalog.hairStyles[hair.id] = hair;
    }
  }

  {
    const Json& base = Field(crew, "jobBase", "crew.json");
    const Json& rooms = Field(crew, "jobRoom", "crew.json");
    for (Catalog::JobDefinition& def : catalog.jobDefs) {
      auto at = base.find(def.id);
      if (at != base.end()) { def.baseAt = at->get<double>(); }
      auto room = rooms.find(def.id);
      if (room != rooms.end()) { def.room = room->get<std::string>(); }
    }
  }

  const Json survival = LoadFile(data / "survival.json");
  catalog.counts.commodities =
      static_cast<int>(Field(survival, "goods", "survival.json").size());

  for (const Json& stat : Field(survival, "stats", "survival.json")) {
    StatDefinition def;
    def.id = Text(stat, "id", "survival.json/stats");
    def.label = Text(stat, "label", "survival.json/stats");
    def.value = Number(stat, "value", "survival.json/stats");
    def.unit = Text(stat, "unit", "survival.json/stats");
    def.colour = Text(stat, "colour", "survival.json/stats");
    catalog.startingStats.push_back(def);
  }
  for (const Json& lot : Field(survival, "goods", "survival.json")) {
    Good g;
    g.id = Text(lot, "id", "survival.json/goods");
    g.name = Text(lot, "name", "survival.json/goods");
    g.kind = Text(lot, "kind", "survival.json/goods");
    g.vol = static_cast<int>(Number(lot, "vol", "survival.json/goods"));
    g.value = Number(lot, "value", "survival.json/goods");
    g.risk = Flag(lot, "risk");
    catalog.startingCargo.push_back(g);
  }
  for (auto it = Field(survival, "surveyWorth", "survival.json").begin();
       it != Field(survival, "surveyWorth", "survival.json").end(); ++it) {
    catalog.surveyWorth[it.key()] = it.value().get<double>();
  }
  for (const Json& record : Field(survival, "survey", "survival.json")) {
    SurveyRecordDef def;
    def.id = Text(record, "id", "survival.json/survey");
    def.name = Text(record, "name", "survival.json/survey");
    def.src = Text(record, "src", "survival.json/survey");
    def.value = Number(record, "value", "survival.json/survey");
    def.state = Text(record, "state", "survival.json/survey");
    catalog.startingSurvey.push_back(def);
  }
  for (const Json& info : Field(survival, "infos", "survival.json")) {
    InfoDefinition def;
    def.id = Text(info, "id", "survival.json/infos");
    def.label = Text(info, "label", "survival.json/infos");
    def.value = Text(info, "value", "survival.json/infos");
    catalog.infoDefs.push_back(def);
  }
  for (const Json& resource : Field(survival, "resources", "survival.json")) {
    catalog.resourceLabels.push_back(Text(resource, "label", "survival.json/resources"));
  }

  {
    const Json& engine = Field(survival, "engine", "survival.json");
    catalog.rules.engine.wearEvery = Number(engine, "wearEvery", "engine", 4);
    catalog.rules.engine.wearStep = Number(engine, "wearStep", "engine", 1);
    catalog.rules.engine.warnBelow = Number(engine, "warnBelow", "engine", 50);
    catalog.rules.engine.engineClickEvery =
        Number(engine, "engineClickEvery", "engine", 12);
    catalog.rules.engine.engineerAt = Number(engine, "engineerAt", "engine", 60);

    const Json& life = Field(survival, "life", "survival.json");
    catalog.rules.life.lifeMax = Number(life, "lifeMax", "life", 200);
    catalog.rules.life.decay = Number(life, "decay", "life", 4);
    catalog.rules.life.capacity = Number(life, "capacity", "life", 10);

    const Json& air = Field(survival, "air", "survival.json");
    catalog.rules.air.holdMax = Number(air, "holdMax", "air", 20);
    catalog.rules.air.step = Number(air, "step", "air", 5);
    for (const Json& stage : Field(survival, "airStages", "survival.json")) {
      AirStage parsed;
      parsed.word = Text(stage, "word", "airStages");
      parsed.tone = Text(stage, "tone", "airStages");
      parsed.slow = Number(stage, "slow", "airStages", 1);
      parsed.jobDrop = Number(stage, "jobDrop", "airStages");
      parsed.harm = Number(stage, "harm", "airStages");
      catalog.rules.air.stages.push_back(parsed);
    }

    const Json& mess = Field(survival, "mess", "survival.json");
    catalog.rules.mess.rackStart = Number(mess, "rack", "mess", 18);
    catalog.rules.mess.capacity = Number(mess, "capacity", "mess", 24);
    catalog.rules.mess.rationStart = Number(mess, "rations", "mess", 21);
    catalog.rules.mess.rationCapacity = Number(mess, "rationCapacity", "mess", 24);
    catalog.rules.mess.foodPerRation = Number(mess, "foodPerRation", "mess", 5);

    catalog.rules.mess.cookBelow = Number(mess, "cookBelow", "mess", 10);
    catalog.rules.mess.hungerPerMeal = Number(mess, "hungerPerMeal", "mess", 15);
    catalog.rules.mess.hungerPerFood = Number(mess, "hungerPerFood", "mess", 5);
    catalog.rules.mess.hungerFloor = Number(mess, "hungerFloor", "mess", 5);
    catalog.rules.mess.every = Number(survival, "messEvery", "survival.json", 25);
    catalog.rules.mess.starveAt = Number(survival, "messStarve", "survival.json", 90);
    catalog.rules.mess.starveEvery = Number(survival, "messStarveEvery", "survival.json", 8);

    const Json& pay = Field(crew, "pay", "crew.json");
    catalog.rules.pay.every = Number(pay, "every", "pay", 100);
    catalog.rules.pay.hit = Number(pay, "hit", "pay", 12);
    catalog.rules.docAway = Number(crew, "docAway", "crew.json", 7);
    catalog.rules.scanCost = Number(survival, "scanCost", "survival.json", 5);
    if (auto reports = survival.find("reports");
        reports != survival.end() && reports->is_object()) {
      catalog.rules.reports.hullLine = Number(*reports, "hullLine", "reports", 60);
      catalog.rules.reports.energyLine = Number(*reports, "energyLine", "reports", 60);
    }
  }

  // ---- totals ------------------------------------------------------------
  catalog.counts.regions = static_cast<int>(catalog.regions.size());
  catalog.counts.systems = static_cast<int>(catalog.systems.size());
  catalog.counts.bodies = static_cast<int>(catalog.bodies.size());
  catalog.counts.ports = static_cast<int>(catalog.ports.size());
  catalog.counts.lanes = static_cast<int>(catalog.sectorLanes.size());
  catalog.counts.contracts = static_cast<int>(catalog.contracts.size());
  catalog.RebuildIndex();

  return catalog;
}

void Catalog::RebuildIndex() const {
  index_.bodyById.clear();
  index_.systemById.clear();
  index_.regionById.clear();
  index_.portById.clear();
  index_.bodiesBySystem.clear();
  index_.systemsByRegion.clear();

  index_.bodyById.reserve(bodies.size());
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    index_.bodyById.emplace(bodies[i].id, i);
    index_.bodiesBySystem[BodySystem(bodies[i])].push_back(i);
  }
  index_.systemById.reserve(systems.size());
  for (std::size_t i = 0; i < systems.size(); ++i) {
    index_.systemById.emplace(systems[i].id, i);
    index_.systemsByRegion[systems[i].region].push_back(i);
  }
  index_.regionById.reserve(regions.size());
  for (std::size_t i = 0; i < regions.size(); ++i) {
    index_.regionById.emplace(regions[i].id, i);
  }
  index_.portById.reserve(ports.size());
  for (std::size_t i = 0; i < ports.size(); ++i) {
    index_.portById.emplace(ports[i].id, i);
  }

  index_.bodyCount = bodies.size();
  index_.systemCount = systems.size();
  index_.regionCount = regions.size();
  index_.portCount = ports.size();
  index_.firstBodyId = bodies.empty() ? std::string() : bodies.front().id;
  index_.lastBodyId = bodies.empty() ? std::string() : bodies.back().id;
  index_.ready = true;
}

void Catalog::EnsureIndex() const {
  const std::string firstId = bodies.empty() ? std::string() : bodies.front().id;
  const std::string lastId = bodies.empty() ? std::string() : bodies.back().id;
  if (!index_.ready || index_.bodyCount != bodies.size() ||
      index_.systemCount != systems.size() || index_.regionCount != regions.size() ||
      index_.portCount != ports.size() || index_.firstBodyId != firstId ||
      index_.lastBodyId != lastId) {
    RebuildIndex();
  }
}

const Body* Catalog::FindBody(const std::string& id) const {
  EnsureIndex();
  const auto it = index_.bodyById.find(id);
  if (it != index_.bodyById.end() && it->second < bodies.size() &&
      bodies[it->second].id == id) {
    return &bodies[it->second];
  }
  for (const Body& b : bodies) { if (b.id == id) { return &b; } }
  return nullptr;
}

const Body* Catalog::FindSystem(const std::string& id) const {
  EnsureIndex();
  const auto it = index_.systemById.find(id);
  if (it != index_.systemById.end() && it->second < systems.size() &&
      systems[it->second].id == id) {
    return &systems[it->second];
  }
  for (const Body& b : systems) { if (b.id == id) { return &b; } }
  return nullptr;
}

const Body* Catalog::FindRegion(const std::string& id) const {
  EnsureIndex();
  const auto it = index_.regionById.find(id);
  if (it != index_.regionById.end() && it->second < regions.size() &&
      regions[it->second].id == id) {
    return &regions[it->second];
  }
  for (const Body& b : regions) { if (b.id == id) { return &b; } }
  return nullptr;
}

const Port* Catalog::FindPort(const std::string& id) const {
  EnsureIndex();
  const auto it = index_.portById.find(id);
  if (it != index_.portById.end() && it->second < ports.size() &&
      ports[it->second].id == id) {
    return &ports[it->second];
  }
  for (const Port& p : ports) { if (p.id == id) { return &p; } }
  return nullptr;
}

const std::vector<std::size_t>& Catalog::BodiesInSystem(const std::string& systemId) const {
  static const std::vector<std::size_t> kEmpty;
  EnsureIndex();
  const auto it = index_.bodiesBySystem.find(systemId);
  return it == index_.bodiesBySystem.end() ? kEmpty : it->second;
}

const std::vector<std::size_t>& Catalog::SystemsInRegion(const std::string& regionId) const {
  static const std::vector<std::size_t> kEmpty;
  EnsureIndex();
  const auto it = index_.systemsByRegion.find(regionId);
  return it == index_.systemsByRegion.end() ? kEmpty : it->second;
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

  // ---- every body sits on the drawing board of its own scale --------------
  // The layers share one coordinate space per scale, not one space overall:
  // the system chart is the reference's 638x278 board, while the sector board
  // is the wider one ExpandChart() lays the galaxy out on.
  const auto boardFor = [this](const std::string& layer) -> const MapGeometry& {
    auto it = layerBoards.find(layer);
    return it == layerBoards.end() ? map : it->second;
  };
  const auto inBoard = [](const Body& b, const MapGeometry& board,
                          std::vector<std::string>& problems, const char* scale) {
    if (b.x < 0 || b.x > board.w || b.y < 0 || b.y > board.h) {
      std::ostringstream out;
      out << b.id << " is off the " << scale << " chart board (" << b.x << "," << b.y << ")";
      problems.push_back(out.str());
    }
  };
  for (const Body& b : regions) { inBoard(b, boardFor("galaxy"), problems, "galaxy"); }
  for (const Body& b : systems) { inBoard(b, boardFor("sector"), problems, "sector"); }
  for (const Body& b : bodies) { inBoard(b, boardFor("system"), problems, "system"); }

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
    if (plan->rooms.size() != 17) {
      problems.push_back(std::string(name) + " deck plan has " +
                         std::to_string(plan->rooms.size()) + " rooms, expected 17");
    }
  }

  return problems;
}

}  // namespace ship

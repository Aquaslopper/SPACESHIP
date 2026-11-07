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

std::vector<std::string> StringArray(const Json& object, const char* key,
                                     const std::string& where) {
  auto it = object.find(key);
  if (it == object.end() || it->is_null()) { return {}; }
  if (!it->is_array()) {
    throw CatalogError(where + ": field '" + key + "' is not an array");
  }
  std::vector<std::string> result;
  for (const Json& item : *it) {
    if (!item.is_string()) {
      throw CatalogError(where + ": field '" + key + "' contains a non-string value");
    }
    result.push_back(item.get<std::string>());
  }
  return result;
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
  b.tag = Text(object, "tag", where);
  b.taskId = Text(object, "taskId", where);
  b.lootTable = Text(object, "lootTable", where);
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

BodyProfileDef MakeBodyProfile(const std::string& id, const std::string& type,
                               const std::string& tag, const std::string& klass,
                               const std::string& subtype, const std::string& nameRule,
                               const std::string& lootTable, std::uint32_t weight) {
  BodyProfileDef profile;
  profile.id = id;
  profile.type = type;
  profile.tag = tag;
  profile.klass = klass;
  profile.subtype = subtype;
  profile.nameRule = nameRule;
  profile.lootTable = lootTable;
  profile.weight = weight;
  return profile;
}

LootEntryDef MakeLootEntry(const std::string& id, const std::string& text,
                           LootKind kind, std::uint32_t weight,
                           int minAmount = 0, int maxAmount = 0,
                           const std::string& goodId = "",
                           const std::string& cargoName = "",
                           const std::string& surveyId = "",
                           double volume = 0.0, double value = 0.0) {
  LootEntryDef entry;
  entry.id = id;
  entry.text = text;
  entry.kind = kind;
  entry.weight = weight;
  entry.minAmount = minAmount;
  entry.maxAmount = maxAmount;
  entry.goodId = goodId;
  entry.cargoName = cargoName;
  entry.surveyId = surveyId;
  entry.volume = volume;
  entry.value = value;
  return entry;
}

SpaceMapBlueprint MakeNativeSpaceMapBlueprint() {
  SpaceMapBlueprint blueprint;
  blueprint.rules.version = 1;
  // Phase 4 consumes these profiles with a stable one-mirror growth policy;
  // the optional hard totals remain unset and independent of the 50x map bounds.
  blueprint.rules.moonChance = 0.5;
  blueprint.rules.miniStationGoodsMax = 3;
  blueprint.rules.normalStationGoodsMax = 7;
  blueprint.rules.shipContactClickTicks = 1;

  blueprint.names = {
      {"region", {"Aster", "Boreal", "Cinder", "Distant", "Eidolon", "Fallow", "Harrow", "Indigo"},
       {"Reach", "Shoal", "Rim", "Expanse", "Drift", "Halo", "Field", "Bend"}},
      {"system", {"Aster", "Bastion", "Cinder", "Dawn", "Eidolon", "Fallow", "Gossamer", "Hallow", "Indigo", "Juniper", "Kestrel", "Lattice"},
       {"Crown", "Gate", "Passage", "Run", "Spur", "Haven", "Sound", "Crossing", "Anchor", "Drift", "Reach", "Yard"}},
      {"star", {"Amber", "Blue", "Copper", "Distant", "Ivory", "Pale", "Quiet", "White"},
       {"Beacon", "Ember", "Flare", "Lantern", "Point", "Signal", "Torch", "Wick"}},
      {"planet", {"Aster", "Bastion", "Cinder", "Dawn", "Eidolon", "Fallow", "Gossamer", "Hallow", "Indigo", "Juniper", "Kestrel", "Lattice"},
       {"I", "II", "III", "IV", "V", "Mare", "Arc", "Shore", "Vale", "Cairn", "Hearth", "Rill"}},
      {"moon", {"Moth", "Nacre", "Orrery", "Peregrine", "Quietus", "Rill", "Sable", "Tern", "Umbra", "Vesper", "Wisp", "Yarrow"},
       {"Deep", "Near", "Far", "Minor", "Hollow", "Ring", "Cove", "Rest"}},
      {"debris", {"Broken", "Cold", "Feral", "Lost", "Silent", "Spare", "Tattered", "Unmoored"},
       {"Array", "Cache", "Field", "Flotilla", "Grave", "Scatter", "Wake", "Yard"}},
      {"station", {"Far", "Hearth", "Long", "Quiet", "Vesper", "West", "Wren", "Yellow"},
       {"Depot", "Relay", "Haven", "Exchange", "Drydock", "Platform", "Terminus", "Market"}},
      {"ship", {"Wayward", "Argent", "Tern", "Moth", "Kite", "Sable", "Echo", "Pilgrim"},
       {"Courier", "Lark", "Artery", "Pioneer", "Wanderer", "Runner", "Surveyor", "Tender"}},
  };

  BodyProfileDef profile = MakeBodyProfile("star-primary", "star", "star.primary",
      "Primary star", "stellar", "star", "", 100);
  profile.details = {{"Spectrum", "Variable"}, {"Chart status", "Generated"}};
  profile.scan = {"Stellar output is stable enough for local navigation."};
  profile.scanLocked = {"A closer pass would resolve the outer corona."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("planet-rock", "planet", "world.rocky", "Rocky world",
      "rocky", "planet", "world-find", 25);
  profile.moonChance = 0.5;
  profile.details = {{"Surface", "Exposed silicate terrain"}, {"Orbit", "Inner system"}};
  profile.scan = {"The surface carries broad mineral bands.", "No stable biosignature is confirmed."};
  profile.scanLocked = {"A high-resolution pass may expose shallow deposits."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("planet-ice", "planet", "world.icy", "Ice world",
      "icy", "planet", "world-find", 25);
  profile.moonChance = 0.5;
  profile.details = {{"Surface", "Water ice and fractured crust"}, {"Orbit", "Outer system"}};
  profile.scan = {"Long fissures interrupt the frozen surface.", "Subsurface pockets remain unverified."};
  profile.scanLocked = {"Thermal mapping could distinguish brine from empty cavities."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("planet-ocean", "planet", "world.ocean", "Ocean world",
      "ocean", "planet", "world-find", 25);
  profile.moonChance = 0.5;
  profile.details = {{"Surface", "Continuous liquid coverage"}, {"Orbit", "Temperate band"}};
  profile.scan = {"Cloud breaks reveal strong tidal currents.", "Surface chemistry is only partly resolved."};
  profile.scanLocked = {"A narrow atmospheric window may reveal trace life."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("planet-gas", "planet", "world.gas", "Gas giant",
      "gas", "planet", "world-find", 25);
  profile.moonChance = 0.5;
  profile.details = {{"Atmosphere", "Hydrogen and helium"}, {"Orbit", "Outer system"}};
  profile.scan = {"Layered cloud bands encircle the upper atmosphere."};
  profile.scanLocked = {"The deep weather bands exceed safe survey depth."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("moon-natural", "moon", "satellite.natural",
      "Natural satellite", "natural", "moon", "world-find", 100);
  profile.details = {{"Origin", "Attached to a generated planet"}};
  profile.scan = {"The moon follows its parent world on a stable orbit."};
  profile.scanLocked = {"A closer orbital pass is required to map the far side."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("debris-ore", "debris", "debris.ore",
      "Ore-bearing debris", "ore", "debris", "debris-common", 45);
  profile.details = {{"Composition", "Metal-rich fragments"}, {"Motion", "Slow drift"}};
  profile.scan = {"The fragments contain recoverable industrial metals."};
  profile.scanLocked = {"A careful pass could separate clean ore from hull fragments."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("debris-wreck", "debris", "debris.wreck",
      "Wreckage field", "wreckage", "debris", "debris-common", 55);
  profile.details = {{"Composition", "Mixed alloy and composite"}, {"Motion", "Irregular"}};
  profile.scan = {"The debris is consistent with a failed cargo convoy."};
  profile.scanLocked = {"Several sealed compartments remain difficult to resolve."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("debris-relay", "derelict", "site.derelict",
      "Derelict structure", "derelict", "debris", "debris-common", 100);
  profile.details = {{"Power", "Intermittent"}, {"Access", "Outer hull only"}};
  profile.scan = {"A narrow-band beacon repeats from inside the hull."};
  profile.scanLocked = {"The internal layout is inaccessible without a safe approach."};
  blueprint.bodies.push_back(profile);

  profile = MakeBodyProfile("signal-site", "site", "site.signal",
      "Anomalous signal site", "signal", "debris", "world-find", 100);
  profile.details = {{"Signal", "Repeating, low power"}};
  profile.scan = {"The pulse is regular but does not match local traffic."};
  profile.scanLocked = {"A second listening pass could recover the full message."};
  blueprint.bodies.push_back(profile);

  LootTableDef table;
  table.id = "debris-common";
  table.entries = {
      MakeLootEntry("shiplets", "A compact cache of loose shiplets is recoverable.",
                    LootKind::Shiplets, 25, 20, 60),
      MakeLootEntry("ore-concentrate", "Ore concentrate can be secured as cargo.",
                    LootKind::Cargo, 20, 1, 2, "ore", "Ore concentrate", "", 2, 45),
      MakeLootEntry("drive-parts", "Salvageable drive components are still intact.",
                    LootKind::Cargo, 15, 1, 2, "parts", "Drive components", "", 1, 75),
      MakeLootEntry("water-ice", "A sealed water-ice canister survives the impact.",
                    LootKind::Cargo, 10, 1, 2, "water", "Water-ice canister", "", 1, 24),
      MakeLootEntry("medical-pack", "An unopened medical pack is found in the wreckage.",
                    LootKind::Cargo, 10, 1, 1, "medicine", "Medical pack", "", 1, 44),
      MakeLootEntry("old-relic", "A small pre-chart relic is safe to bring aboard.",
                    LootKind::Cargo, 10, 1, 1, "relics", "Pre-chart relic", "", 1, 90),
      MakeLootEntry("survey-fragment", "A fragment of an older survey record is recovered.",
                    LootKind::Survey, 5, 35, 75, "", "", "debris-fragment"),
      MakeLootEntry("empty-field", "Nothing suitable for recovery remains.",
                    LootKind::None, 5),
  };
  blueprint.lootTables.push_back(table);

  table = LootTableDef{};
  table.id = "provisions";
  table.entries = {
      MakeLootEntry("small-purse", "A modest shiplet reserve is tucked in a service locker.",
                    LootKind::Shiplets, 35, 10, 40),
      MakeLootEntry("greens", "A chilled packet of greens remains fresh.",
                    LootKind::Cargo, 25, 1, 2, "greens", "Chilled greens", "", 1, 30),
      MakeLootEntry("water", "A sealed water ration can be reused.",
                    LootKind::Cargo, 20, 1, 2, "water", "Water ration", "", 1, 20),
      MakeLootEntry("medicine", "A medical pouch is still within date.",
                    LootKind::Cargo, 10, 1, 1, "medicine", "Medical pouch", "", 1, 40),
      MakeLootEntry("empty-locker", "The locker is empty, but its seal is intact.",
                    LootKind::None, 10),
  };
  blueprint.lootTables.push_back(table);

  table = LootTableDef{};
  table.id = "world-find";
  table.entries = {
      MakeLootEntry("field-notes", "A short environmental survey is worth filing.",
                    LootKind::Survey, 45, 20, 65, "", "", "world-notes"),
      MakeLootEntry("trace-relic", "A trace relic is embedded in the surface material.",
                    LootKind::Cargo, 15, 1, 1, "relics", "Trace relic", "", 1, 65),
      MakeLootEntry("shiplets", "A small emergency cache is recoverable.",
                    LootKind::Shiplets, 20, 10, 30),
      MakeLootEntry("no-reward", "The survey yields no recoverable material.",
                    LootKind::None, 20),
  };
  blueprint.lootTables.push_back(table);

  blueprint.surveys = {
      {"debris-fragment", "Debris field fragment", "Generated debris", 35, "unfiled",
       "debris", "survey.debris", "A partial composition record from a wreckage field."},
      {"world-notes", "World environment notes", "Generated world", 20, "unfiled",
       "planet", "survey.world", "A concise environmental record suitable for a later comparison."},
      {"traffic-record", "Traffic pattern record", "Generated contact", 50, "unfiled",
       "ship", "survey.traffic", "A timestamped record of a ship crossing the local system."},
  };

  blueprint.tasks = NativeSpaceMapTaskBlueprints();

  IncidentType incident;
  incident.id = "map-debris-recovery";
  incident.kind = "survey";
  incident.source = "generated debris";
  incident.title = "A salvageable signal persists";
  incident.remedy = "Inspect the debris field and secure any safe material.";
  incident.scope = "map";
  incident.trigger = "enter-tag";
  incident.targetTag = "debris.wreck";
  incident.taskId = "coil-relay";
  incident.lootTable = "debris-common";
  incident.spawnWeight = 100;
  blueprint.incidents.push_back(incident);

  incident = IncidentType{};
  incident.id = "map-world-anomaly";
  incident.kind = "survey";
  incident.source = "generated world";
  incident.title = "A surface reading needs a second pass";
  incident.remedy = "Resolve the local readings before filing the report.";
  incident.scope = "map";
  incident.trigger = "scan-tag";
  incident.targetTag = "world.ocean";
  incident.taskId = "dead-reckoning";
  incident.lootTable = "world-find";
  incident.spawnWeight = 60;
  blueprint.incidents.push_back(incident);

  blueprint.stations = {
      {"mini", "mini", 2, 3, {"ore", "parts", "water"}, {"trade", "refuel"}, true},
      {"normal", "normal", 4, 7,
       {"ore", "parts", "greens", "amps", "water", "medicine", "relics"},
       {"trade", "refuel", "resupply", "repair", "upgrades", "hire", "contracts"}, false},
  };

  ShipContactProfileDef contact;
  contact.id = "trader";
  contact.type = "ship";
  contact.tag = "ship.trader";
  contact.klass = "Independent courier";
  contact.nameRule = "ship";
  contact.weight = 50;
  contact.movementEveryActions = 2;
  contact.actions = {{"hail", "Hail", "Exchange a short identification burst.", 1},
                     {"trade", "Trade", "Offer one available cargo good for exchange.", 1}};
  blueprint.shipContacts.push_back(contact);

  contact = ShipContactProfileDef{};
  contact.id = "distress";
  contact.type = "ship";
  contact.tag = "ship.distress";
  contact.klass = "Disabled vessel";
  contact.nameRule = "ship";
  contact.weight = 20;
  contact.movementEveryActions = 0;
  contact.actions = {{"hail", "Hail", "Attempt a narrow-band response.", 1},
                     {"assist", "Assist", "Commit supplies to a rescue attempt.", 1}};
  blueprint.shipContacts.push_back(contact);

  contact = ShipContactProfileDef{};
  contact.id = "patrol";
  contact.type = "ship";
  contact.tag = "ship.patrol";
  contact.klass = "Survey patrol";
  contact.nameRule = "ship";
  contact.weight = 15;
  contact.movementEveryActions = 3;
  contact.actions = {{"hail", "Hail", "Exchange a short identification burst.", 1},
                     {"report", "Report", "Transmit a brief navigation report.", 1}};
  blueprint.shipContacts.push_back(contact);

  contact = ShipContactProfileDef{};
  contact.id = "salvager";
  contact.type = "ship";
  contact.tag = "ship.salvager";
  contact.klass = "Salvage tender";
  contact.nameRule = "ship";
  contact.weight = 15;
  contact.movementEveryActions = 2;
  contact.actions = {{"hail", "Hail", "Exchange a short identification burst.", 1},
                     {"trade", "Trade", "Offer one available cargo good for exchange.", 1}};
  blueprint.shipContacts.push_back(contact);

  return blueprint;
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
    port.stationTier = Text(it.value(), "stationTier", "chart.json/ports/" + port.id);
    port.goodIds = StringArray(it.value(), "goods", "chart.json/ports/" + port.id);
    port.services = StringArray(it.value(), "services", "chart.json/ports/" + port.id);
    port.beta = Flag(it.value(), "betaState");
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
    g.miniStationEligible = Flag(good, "miniStationEligible");
    catalog.goods.push_back(g);
  }
  for (Good& good : catalog.goods) {
    good.miniStationEligible = good.miniStationEligible ||
        good.id == "ore" || good.id == "parts" || good.id == "greens" ||
        good.id == "water" || good.id == "medicine";
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
    type.scope = Text(incident, "scope", "incidents", "ship");
    type.trigger = Text(incident, "trigger", "incidents", "schedule");
    type.targetTag = Text(incident, "targetTag", "incidents");
    type.taskId = Text(incident, "taskId", "incidents");
    type.lootTable = Text(incident, "lootTable", "incidents");
    type.spawnWeight = static_cast<std::uint32_t>(Number(incident, "spawnWeight", "incidents"));
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
  for (auto it = Field(content, "cooldownRules", "content.json").begin();
       it != Field(content, "cooldownRules", "content.json").end(); ++it) {
    CooldownRule rule;
    rule.actions = Number(it.value(), "actions", "cooldownRules");
    rule.label = Text(it.value(), "label", "cooldownRules");
    catalog.rules.cooldowns[it.key()] = rule;
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
    def.bodyType = Text(record, "bodyType", "survival.json/survey");
    def.tag = Text(record, "tag", "survival.json/survey");
    def.text = Text(record, "text", "survival.json/survey");
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
    catalog.rules.engine.perAction = Number(engine, "perAction", "engine", 10);

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
    catalog.rules.mess.every = Number(survival, "messEvery", "survival.json", 25);
    catalog.rules.mess.starveAt = Number(survival, "messStarve", "survival.json", 90);
    catalog.rules.mess.starveEvery = Number(survival, "messStarveEvery", "survival.json", 8);

    const Json& pay = Field(crew, "pay", "crew.json");
    catalog.rules.pay.every = Number(pay, "every", "pay", 100);
    catalog.rules.pay.hit = Number(pay, "hit", "pay", 12);
    catalog.rules.docAway = Number(crew, "docAway", "crew.json", 7);
    catalog.rules.scanCost = Number(survival, "scanCost", "survival.json", 5);
  }

  // Native C++ blueprint is deliberately separate from HTA-generated assets.
  catalog.spaceMap = MakeNativeSpaceMapBlueprint();

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

const BodyProfileDef* Catalog::FindSpaceBodyProfile(const std::string& id) const {
  for (const BodyProfileDef& profile : spaceMap.bodies) {
    if (profile.id == id) { return &profile; }
  }
  return nullptr;
}

const LootTableDef* Catalog::FindSpaceLootTable(const std::string& id) const {
  for (const LootTableDef& table : spaceMap.lootTables) {
    if (table.id == id) { return &table; }
  }
  return nullptr;
}

const SpaceTaskDef* Catalog::FindSpaceTask(const std::string& id) const {
  for (const SpaceTaskDef& task : spaceMap.tasks) {
    if (task.id == id) { return &task; }
  }
  return nullptr;
}

const StationProfileDef* Catalog::FindStationProfile(const std::string& id) const {
  for (const StationProfileDef& profile : spaceMap.stations) {
    if (profile.id == id) { return &profile; }
  }
  return nullptr;
}

const ShipContactProfileDef* Catalog::FindShipContactProfile(const std::string& id) const {
  for (const ShipContactProfileDef& profile : spaceMap.shipContacts) {
    if (profile.id == id) { return &profile; }
  }
  return nullptr;
}

const Good* Catalog::FindGood(const std::string& id) const {
  for (const Good& good : goods) { if (good.id == id) { return &good; } }
  return nullptr;
}

const Port* Catalog::FindPort(const std::string& id) const {
  for (const Port& p : ports) { if (p.id == id) { return &p; } }
  return nullptr;
}

bool PortHasService(const Port& port, const std::string& service) {
  return port.services.empty() ||
         std::find(port.services.begin(), port.services.end(), service) != port.services.end();
}

bool PortOffersGood(const Port& port, const std::string& goodId) {
  return port.goodIds.empty() ||
         std::find(port.goodIds.begin(), port.goodIds.end(), goodId) != port.goodIds.end();
}

std::string Catalog::BodySystem(const Body& body) const {
  return body.system.empty() ? std::string("sec-kessler") : body.system;
}

std::vector<std::string> Catalog::Validate() const {
  std::vector<std::string> problems;

  // ---- expanded native chart bounds -------------------------------------
  for (const auto& layer : {std::pair<const char*, const MapLayerDimensions*>("system", &map.system),
                            std::pair<const char*, const MapLayerDimensions*>("sector", &map.sector),
                            std::pair<const char*, const MapLayerDimensions*>("galaxy", &map.galaxy)}) {
    const MapLayerDimensions& dimensions = *layer.second;
    if (dimensions.w <= 1100 || dimensions.h <= 500) {
      problems.push_back(std::string(layer.first) + " map bounds must exceed the HTA 1100x500 ceiling");
    }
    if (dimensions.w != map.w * kExpandedMapScale ||
        dimensions.h != map.h * kExpandedMapScale) {
      problems.push_back(std::string(layer.first) + " map bounds must be 50x the C++ chart dimensions");
    }
  }

  // ---- unique identifiers across all three layers ------------------------
  {
    std::set<std::string> seen;
    for (const std::vector<Body>* layer : {&regions, &systems, &bodies}) {
      for (const Body& b : *layer) {
        if (b.id.empty()) { problems.push_back("a chart body has an empty id"); continue; }
        if (!spaceMapGenerated && b.id.rfind("gen-", 0) == 0) {
          problems.push_back("authored chart id uses the reserved generated prefix: " + b.id);
        }
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
      if (!b.taskId.empty() && FindSpaceTask(b.taskId) == nullptr) {
        problems.push_back(b.id + " references unknown native task '" + b.taskId + "'");
      }
      if (!b.lootTable.empty() && FindSpaceLootTable(b.lootTable) == nullptr) {
        problems.push_back(b.id + " references unknown native loot table '" + b.lootTable + "'");
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

  // ---- optional per-port limits; empty fields preserve authored legacy ports
  for (const Port& port : ports) {
    if (!port.stationTier.empty() && port.stationTier != "mini" &&
        port.stationTier != "normal") {
      problems.push_back("port '" + port.id + "' has unknown station tier '" +
                          port.stationTier + "'");
    }
    const std::size_t limit = port.stationTier == "mini" ? 3u : 7u;
    const std::size_t minimum = port.stationTier == "mini" ? 2u
                                : port.stationTier == "normal" ? 4u : 0u;
    if (port.goodIds.size() > limit ||
        (!port.stationTier.empty() && port.goodIds.size() < minimum)) {
      problems.push_back("port '" + port.id + "' exceeds or misses its good limit");
    }
    std::set<std::string> portGoods;
    for (const std::string& id : port.goodIds) {
      if (!portGoods.insert(id).second) {
        problems.push_back("port '" + port.id + "' repeats good '" + id + "'");
      }
      const Good* good = FindGood(id);
      if (good == nullptr) {
        problems.push_back("port '" + port.id + "' offers unknown good '" + id + "'");
      } else if (port.stationTier == "mini" && !good->miniStationEligible) {
        problems.push_back("mini port '" + port.id + "' offers ineligible good '" + id + "'");
      }
    }
    if (port.stationTier == "mini" && !port.beta) {
      problems.push_back("mini port '" + port.id + "' must be marked beta");
    }
    if (port.stationTier == "normal" && port.beta) {
      problems.push_back("normal port '" + port.id + "' cannot be beta");
    }
    const std::set<std::string> validServices = {
        "trade", "refuel", "resupply", "repair", "upgrades", "hire", "contracts"};
    std::set<std::string> portServices;
    for (const std::string& service : port.services) {
      if (validServices.count(service) == 0 || !portServices.insert(service).second) {
        problems.push_back("port '" + port.id + "' has an unknown or duplicate service");
      }
    }
    if (port.stationTier == "mini" &&
        (port.services.size() != 2 || portServices.count("trade") == 0 ||
         portServices.count("refuel") == 0)) {
      problems.push_back("mini port '" + port.id + "' must offer only trade and refuel");
    }
    if (port.stationTier == "normal") {
      const std::set<std::string> fullServices = {
          "trade", "refuel", "resupply", "repair", "upgrades", "hire", "contracts"};
      if (portServices != fullServices) {
        problems.push_back("normal port '" + port.id + "' must offer the seven normal services");
      }
    }
  }

  // ---- native map blueprint ---------------------------------------------
  const auto hasNameRule = [&](const std::string& category) {
    for (const SpaceMapNameRule& rule : spaceMap.names) {
      if (rule.category == category) { return true; }
    }
    return false;
  };
  std::set<std::string> nameCategories;
  for (const SpaceMapNameRule& rule : spaceMap.names) {
    if (rule.category.empty() || !nameCategories.insert(rule.category).second) {
      problems.push_back("native map has an empty or duplicate name category");
    }
    if (rule.prefixes.empty() || rule.suffixes.empty()) {
      problems.push_back("native name rule '" + rule.category + "' has no choices");
    }
  }
  if (spaceMap.rules.version == 0) {
    problems.push_back("native map blueprint version must be positive");
  }
  if (spaceMap.rules.moonChance < 0 || spaceMap.rules.moonChance > 1 ||
      spaceMap.rules.maxMoonsPerPlanet != 1) {
    problems.push_back("native moon attachment rule must be a valid chance and max one moon");
  }
  if (spaceMap.rules.miniStationGoodsMax != 3 ||
      spaceMap.rules.normalStationGoodsMax != 7) {
    problems.push_back("native station good limits must remain mini=3, normal=7");
  }
  if (spaceMap.rules.shipContactClickTicks != 1) {
    problems.push_back("a committed ship-contact click must tick exactly once");
  }

  std::set<std::string> profileIds;
  std::set<std::string> bodyTags;
  std::map<std::string, std::uint64_t> profileWeights;
  for (const BodyProfileDef& profile : spaceMap.bodies) {
    if (profile.id.empty() || !profileIds.insert(profile.id).second) {
      problems.push_back("native body profiles have an empty or duplicate id");
    }
    if (profile.tag.empty() || !bodyTags.insert(profile.tag).second) {
      problems.push_back("native body profiles have an empty or duplicate tag");
    }
    if (profile.type.empty() || profile.tag.empty() || profile.klass.empty()) {
      problems.push_back("native body profile '" + profile.id + "' lacks type, tag, or class");
    }
    if (!hasNameRule(profile.nameRule)) {
      problems.push_back("native body profile '" + profile.id + "' has no name rule");
    }
    if (!profile.lootTable.empty() && FindSpaceLootTable(profile.lootTable) == nullptr) {
      problems.push_back("native body profile '" + profile.id + "' has unknown loot table");
    }
    if (profile.scan.empty() || profile.scanLocked.empty() || profile.details.empty()) {
      problems.push_back("native body profile '" + profile.id + "' has incomplete survey text");
    }
    if (profile.moonChance < 0 || profile.moonChance > 1) {
      problems.push_back("native body profile '" + profile.id + "' has invalid moon chance");
    }
    if (profile.weight > 0) { profileWeights[profile.type] += profile.weight; }
  }
  for (const auto& entry : profileWeights) {
    if (entry.second != 100) {
      problems.push_back("native body type '" + entry.first + "' weights do not sum to 100");
    }
  }

  std::set<std::string> surveyIds;
  for (const SurveyRecordDef& survey : spaceMap.surveys) {
    if (survey.id.empty() || !surveyIds.insert(survey.id).second) {
      problems.push_back("native surveys have an empty or duplicate id");
    }
    if (survey.value < 0 || survey.bodyType.empty() || survey.tag.empty() || survey.text.empty()) {
      problems.push_back("native survey '" + survey.id + "' has incomplete metadata");
    }
  }

  std::set<std::string> lootTableIds;
  for (const LootTableDef& table : spaceMap.lootTables) {
    if (table.id.empty() || !lootTableIds.insert(table.id).second) {
      problems.push_back("native loot tables have an empty or duplicate id");
    }
    if (!table.filterByEligibility) {
      problems.push_back("native loot table '" + table.id + "' must filter ineligible rewards");
    }
    std::set<std::string> entryIds;
    std::uint64_t totalWeight = 0;
    for (const LootEntryDef& entry : table.entries) {
      if (entry.id.empty() || !entryIds.insert(entry.id).second || entry.weight == 0) {
        problems.push_back("native loot table '" + table.id + "' has an invalid entry");
      }
      totalWeight += entry.weight;
      if (entry.minAmount < 0 || entry.maxAmount < entry.minAmount) {
        problems.push_back("native loot entry '" + entry.id + "' has invalid amount bounds");
      }
      if (entry.kind == LootKind::Cargo) {
        if (FindGood(entry.goodId) == nullptr || entry.cargoName.empty() ||
            entry.minAmount < 1 || entry.volume <= 0 || entry.value <= 0) {
          problems.push_back("native cargo loot '" + entry.id + "' is incomplete");
        }
      } else if (entry.kind == LootKind::Survey &&
                 (entry.surveyId.empty() || surveyIds.count(entry.surveyId) == 0)) {
        problems.push_back("native survey loot '" + entry.id + "' has unknown survey");
      }
    }
    if (table.entries.empty() || totalWeight != 100) {
      problems.push_back("native loot table '" + table.id + "' weights must sum to 100");
    }
  }

  std::set<std::string> taskIds;
  for (const SpaceTaskDef& task : spaceMap.tasks) {
    if (task.id.empty() || !taskIds.insert(task.id).second) {
      problems.push_back("native tasks have an empty or duplicate id");
    }
    if (task.theme.empty() || task.title.empty() || task.discoveryText.empty() ||
        task.workText.empty() || task.completionText.empty()) {
      problems.push_back("native task '" + task.id + "' has incomplete text");
    }
    if (task.startActions < 1 || task.workActions < 1 || task.energyCost < 0) {
      problems.push_back("native task '" + task.id + "' has invalid action or energy cost");
    }
    if (!task.lootTable.empty() && FindSpaceLootTable(task.lootTable) == nullptr) {
      problems.push_back("native task '" + task.id + "' has unknown loot table");
    }
  }

  std::set<std::string> incidentIds;
  for (const IncidentType& incident : spaceMap.incidents) {
    if (incident.id.empty() || !incidentIds.insert(incident.id).second) {
      problems.push_back("native map incidents have an empty or duplicate id");
    }
    if (incident.scope != "map" ||
        (incident.trigger != "enter-tag" && incident.trigger != "scan-tag" &&
         incident.trigger != "contact-action")) {
      problems.push_back("native map incident '" + incident.id + "' has invalid scope or trigger");
    }
    if (incident.targetTag.empty() || incident.spawnWeight == 0 || incident.spawnWeight > 100) {
      problems.push_back("native map incident '" + incident.id + "' has invalid tag or weight");
    }
    if ((incident.trigger == "enter-tag" || incident.trigger == "scan-tag") &&
        bodyTags.count(incident.targetTag) == 0) {
      problems.push_back("native map incident '" + incident.id + "' targets an unknown body tag");
    }
    if (!incident.taskId.empty() && taskIds.count(incident.taskId) == 0) {
      problems.push_back("native map incident '" + incident.id + "' has unknown task");
    }
    if (!incident.lootTable.empty() && FindSpaceLootTable(incident.lootTable) == nullptr) {
      problems.push_back("native map incident '" + incident.id + "' has unknown loot table");
    }
  }

  std::set<std::string> stationIds;
  for (const StationProfileDef& station : spaceMap.stations) {
    if (station.id.empty() || !stationIds.insert(station.id).second) {
      problems.push_back("native station profiles have an empty or duplicate id");
    }
    const bool mini = station.tier == "mini";
    const int maxGoods = mini ? spaceMap.rules.miniStationGoodsMax
                              : spaceMap.rules.normalStationGoodsMax;
    if ((!mini && station.tier != "normal") || station.minGoods < 1 ||
        station.maxGoods < station.minGoods || station.maxGoods > maxGoods ||
        static_cast<int>(station.goodIds.size()) < station.minGoods ||
        static_cast<int>(station.goodIds.size()) > maxGoods) {
      problems.push_back("native station profile '" + station.id + "' has invalid good limits");
    }
    if (station.beta != mini) {
      problems.push_back("native station profile '" + station.id + "' has invalid beta state");
    }
    std::set<std::string> goodsForStation;
    for (const std::string& id : station.goodIds) {
      const Good* good = FindGood(id);
      if (good == nullptr || !goodsForStation.insert(id).second) {
        problems.push_back("native station profile '" + station.id + "' has unknown or duplicate good");
      } else if (mini && !good->miniStationEligible) {
        problems.push_back("native mini station uses an ineligible good '" + id + "'");
      }
    }
    const std::set<std::string> stationServices(station.services.begin(), station.services.end());
    const std::set<std::string> miniServices = {"trade", "refuel"};
    const std::set<std::string> fullServices = {
        "trade", "refuel", "resupply", "repair", "upgrades", "hire", "contracts"};
    if (stationServices.size() != station.services.size() ||
        (mini && stationServices != miniServices) ||
        (station.tier == "normal" && stationServices != fullServices)) {
      problems.push_back("native station profile '" + station.id + "' has invalid services");
    }
  }

  std::set<std::string> contactIds;
  std::set<std::string> contactTags;
  std::uint64_t contactWeight = 0;
  for (const ShipContactProfileDef& contact : spaceMap.shipContacts) {
    if (contact.id.empty() || !contactIds.insert(contact.id).second) {
      problems.push_back("native ship contacts have an empty or duplicate id");
    }
    if (contact.tag.empty() || !contactTags.insert(contact.tag).second) {
      problems.push_back("native ship contacts have an empty or duplicate tag");
    }
    contactWeight += contact.weight;
    if (contact.type != "ship" || contact.tag.empty() || contact.klass.empty() ||
        !hasNameRule(contact.nameRule) || contact.weight == 0 ||
        contact.movementEveryActions < 0 || contact.actions.empty()) {
      problems.push_back("native ship contact '" + contact.id + "' is incomplete");
    }
    std::set<std::string> actions;
    for (const ShipContactActionDef& action : contact.actions) {
      if (action.id.empty() || !actions.insert(action.id).second ||
          action.label.empty() || action.text.empty() ||
          action.clockTicks != spaceMap.rules.shipContactClickTicks) {
        problems.push_back("native ship contact '" + contact.id + "' has invalid action clock data");
      }
    }
  }
  if (!spaceMap.shipContacts.empty() && contactWeight != 100) {
    problems.push_back("native ship contact profile weights must sum to 100");
  }
  for (const IncidentType& incident : spaceMap.incidents) {
    if (incident.trigger == "contact-action" &&
        contactTags.count(incident.targetTag) == 0) {
      problems.push_back("native map incident '" + incident.id + "' targets an unknown ship tag");
    }
  }

  return problems;
}

}  // namespace ship

// ============================================================================
//  SaveSystem.cpp — the voyage, on disk, in both directions.
//  ---------------------------------------------------------------------------
//  A save is one JSON object with seventeen keys, written in the reference's
//  order and with the reference's field names. `format` is checked first, then
//  everything that could make the voyage impossible: a hold fuller than the
//  hold, a task pointing at a body that does not exist, an incident waiting on a
//  stage that has no ticks left, a decode task without the signal that needs it.
//
//  Loading is transactional. The reference snapshots the live voyage before it
//  applies anything and restores that snapshot if validation fails; the port
//  validates first and applies second, which is the same promise with less work.
//
//  Slot 1 on the original platform is UTF-16LE with a BOM, because the HTA reads
//  it with ActiveX. The port reads and writes both encodings.
// ============================================================================
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "data/ChartExpansion.h"

#include "core/JsNumber.h"
#include "core/Simulation.h"
#include "core/DisplaySettings.h"

namespace ship {

namespace {

using Json = nlohmann::ordered_json;

Json Num(double value) {
  if (value == js::Round(value) && value > -1e15 && value < 1e15) {
    return Json(static_cast<long long>(value));
  }
  return Json(value);
}

double GetNumber(const Json& object, const char* key, double fallback = 0.0) {
  auto it = object.find(key);
  if (it == object.end() || !it->is_number()) { return fallback; }
  return it->get<double>();
}

std::string GetString(const Json& object, const char* key, const std::string& fallback = std::string()) {
  auto it = object.find(key);
  if (it == object.end() || !it->is_string()) { return fallback; }
  return it->get<std::string>();
}

bool GetBool(const Json& object, const char* key, bool fallback = false) {
  auto it = object.find(key);
  if (it == object.end() || !it->is_boolean()) { return fallback; }
  return it->get<bool>();
}

bool IsObject(const Json& value) { return value.is_object(); }
bool IsArray(const Json& value) { return value.is_array(); }

// saveNumber(): a finite, non-negative number. The reference uses this for every
// counter, coordinate and price it later does arithmetic on.
bool SaveNumber(const Json& value) {
  return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= 0;
}

const Json* At(const Json& object, const char* key) {
  auto it = object.find(key);
  return it == object.end() ? nullptr : &*it;
}

std::string LotBindId(const Json& lot) {
  std::string id = GetString(lot, "contractId");
  const std::string trigger = GetString(lot, "trigger_id");
  if (id.empty()) { id = trigger; }
  if (id.compare(0, 6, "trade:") == 0) { return id; }
  const std::size_t tradeAt = id.find("trade:");
  if (tradeAt != std::string::npos) { return id.substr(tradeAt); }
  if (id.empty()) { return trigger; }
  return id;
}

ChartSettings ReadChartSettings(const Json& root, const ChartSettings& fallback) {
  ChartSettings settings = fallback;
  const Json* chart = At(root, "chart");
  if (chart == nullptr || !chart->is_object()) { return settings; }
  const Json* seed = At(*chart, "seed");
  if (seed != nullptr) {
    if (seed->is_string()) {
      try {
        settings.seed = std::stoull(seed->get<std::string>(), nullptr, 0);
      } catch (...) {
      }
    } else if (seed->is_number()) {
      settings.seed = static_cast<std::uint64_t>(seed->get<double>());
    }
  }
  const Json* galaxies = At(*chart, "galaxies");
  if (galaxies != nullptr && galaxies->is_number()) {
    settings.galaxies = static_cast<int>(galaxies->get<double>());
  }
  const Json* mapSize = At(*chart, "mapSize");
  if (mapSize != nullptr && mapSize->is_number()) {
    settings.mapSize = ClampMapSize(static_cast<int>(mapSize->get<double>()));
    settings.mapScale = MapSizeScale(settings.mapSize);
  } else {
    const Json* mapScale = At(*chart, "mapScale");
    if (mapScale != nullptr && mapScale->is_number()) {
      settings.mapScale = mapScale->get<double>();
      int size = 0;
      double scale = 1.0;
      while (size < kMapSizeCount - 1 && settings.mapScale + 0.001 >= scale * kMapSizeStep) {
        scale *= kMapSizeStep;
        ++size;
      }
      settings.mapSize = size;
    }
  }
  const Json* spread = At(*chart, "spread");
  if (spread != nullptr && spread->is_number()) {
    settings.spread = spread->get<double>();
  }
  return settings;
}

}  // namespace

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

std::string Simulation::SaveJson() const {
  Json root = Json::object();
  root["format"] = "spaceship-command-2";

  // ---- STATE -------------------------------------------------------------
  Json s = Json::object();
  s["activeTab"] = state_.activeTab;

  Json tabs = Json::array();
  for (const Tab& tab : state_.tabs) {
    tabs.push_back(Json{{"key", tab.key}, {"id", tab.id}, {"kind", tab.kind},
                        {"title", tab.title}, {"kindLabel", tab.kindLabel}, {"sub", tab.sub}});
  }
  s["tabs"] = tabs;

  Json feed = Json::array();
  for (const FeedLine& line : state_.feed) {
    feed.push_back(Json{{"t", line.stamp}, {"s", line.text}});
  }
  s["feed"] = feed;
  s["popups"] = Num(state_.popups);
  s["reducedMotion"] = state_.reducedMotion;
  s["shipClass"] = state_.shipClass;

  s["options"] = Json{{"largeText", state_.options.largeText},
                      {"confirmRisk", state_.options.confirmRisk},
                      {"hideCrewLabels", state_.options.hideCrewLabels},
                      {"theme", state_.options.theme},
                      {"soundEffects", state_.options.soundEffects},
                      {"music", state_.options.music},
                      {"audioLevel", Num(state_.options.audioLevel)},
                      {"displayGamma", Num(DisplayGamma(state_.options.displayGamma))},
                      {"displayGammaScale", "relative-1.6"},
                      {"displayBrightness", Num(DisplayBrightness(state_.options.displayBrightness))},
                      // Phase 9: the reading size and the window travel too.
                      {"fontStep", Num(static_cast<double>(state_.options.fontStep))},
                      {"fullscreen", state_.options.fullscreen}};

  Json chronicle = Json::object();
  chronicle["version"] = state_.chronicle.version;
  chronicle["systems"] = Json::object();
  for (const auto& entry : state_.chronicle.systems) {
    if (entry.second) { chronicle["systems"][entry.first] = true; }
  }
  chronicle["regions"] = Json::object();
  for (const auto& entry : state_.chronicle.regions) {
    if (entry.second) { chronicle["regions"][entry.first] = true; }
  }
  chronicle["banks"] = Num(state_.chronicle.banks);
  chronicle["projects"] = Num(state_.chronicle.projects);
  chronicle["meals"] = Num(state_.chronicle.meals);
  chronicle["incidents"] = Num(state_.chronicle.incidents);
  chronicle["earned"] = Json::object();
  for (const auto& entry : state_.chronicle.earned) {
    if (entry.second) { chronicle["earned"][entry.first] = true; }
  }
  chronicle["claimed"] = Json::object();
  for (const auto& entry : state_.chronicle.claimed) {
    if (entry.second) { chronicle["claimed"][entry.first] = true; }
  }
  s["chronicle"] = chronicle;

  Json serials = Json::object();
  for (const auto& entry : state_.systemSerials) { serials[entry.first] = Num(entry.second); }
  s["systemSerials"] = serials;
  s["nextSystemSerial"] = Num(state_.nextSystemSerial);
  
  Json commissions = Json::object();
  commissions["version"] = state_.commissions.version;
  commissions["serial"] = Num(state_.commissions.serial);
  commissions["completed"] = Num(state_.commissions.completed);
  if (state_.commissions.active.has_value()) {
    const Commission& c = *state_.commissions.active;
    commissions["active"] = Json{{"kind", c.kind},
                                 {"origin", c.origin},
                                 {"target", c.target},
                                 {"progress", Num(c.progress)},
                                 {"reward", Num(c.reward)}};
  } else {
    commissions["active"] = nullptr;
  }
  s["commissions"] = commissions;

  s["spOpen"] = Json::object();
  for (const auto& entry : state_.spOpen) { s["spOpen"][entry.first] = entry.second; }
  s["freshTabs"] = Json::object();
  for (const auto& entry : state_.freshTabs) { s["freshTabs"][entry.first] = entry.second; }

  Json charges = Json::object();
  for (const auto& entry : state_.charges) {
    charges[entry.first] = Json{{"left", Num(entry.second.left)},
                                {"volume", Num(entry.second.volume)}};
  }
  s["charges"] = charges;

  Json ongoing = Json::array();
  for (const Task& task : state_.ongoing) {
    Json t = Json::object();
    t["id"] = task.id;
    t["label"] = task.label;
    t["total"] = Num(task.total);
    t["left"] = Num(task.left);
    t["state"] = task.state;
    t["type"] = task.type;
    if (!task.target.empty()) { t["target"] = task.target; }
    if (!task.by.empty()) { t["by"] = task.by; }
    if (!task.who.empty()) { t["who"] = task.who; }
    if (!task.other.empty()) { t["other"] = task.other; }
    if (task.slot >= 0) { t["slot"] = Num(task.slot); }
    if (task.vol != 0) { t["vol"] = Num(task.vol); }
    ongoing.push_back(t);
  }
  s["ongoing"] = ongoing;

  s["navTarget"] = state_.navTarget.has_value() ? Json(*state_.navTarget) : Json(nullptr);

  Json scanned = Json::object();
  for (const auto& entry : state_.scanned) {
    if (entry.second) { scanned[entry.first] = true; }
  }
  s["scanned"] = scanned;

  Json mapMarks = Json::object();
  for (const auto& entry : state_.mapMarks) {
    if (entry.second) { mapMarks[entry.first] = true; }
  }
  s["mapMarks"] = mapMarks;

  s["medkits"] = Num(state_.medkits);
  s["engineCondition"] = Num(state_.engineCondition);
  s["wearTick"] = Num(state_.wearTick);
  s["engineerRun"] = state_.engineerRun;
  s["filterLife"] = Num(state_.filterLife);
  s["airHold"] = Num(state_.airHold);
  s["rackWarned"] = state_.rackWarned;
  s["rackWarnedMess"] = state_.rackWarnedMess;
  s["crewSel"] = state_.crewSel;

  Json pending = Json::object();
  for (const auto& entry : state_.pendingOrders) { pending[entry.first] = entry.second; }
  s["pendingOrders"] = pending;

  s["crewTick"] = Num(state_.crewTick);
  s["shiplets"] = Num(state_.shiplets);
  s["payTick"] = Num(state_.payTick);
  s["docked"] = state_.docked;
  s["fuelCells"] = Num(state_.fuelCells);
  s["repairKits"] = Num(state_.repairKits);
  s["lotSerial"] = Num(state_.lotSerial);

  Json contracts = Json::object();
  for (const auto& entry : state_.contracts) { contracts[entry.first] = entry.second; }
  s["contracts"] = contracts;

  Json passengers = Json::array();
  for (const Passenger& passenger : state_.passengers) {
    passengers.push_back(Json{{"id", passenger.id}, {"name", passenger.name}});
  }
  s["passengers"] = passengers;

  Json upgrades = Json::object();
  for (const auto& entry : state_.upgrades) { upgrades[entry.first] = entry.second; }
  s["upgrades"] = upgrades;

  Json hired = Json::object();
  for (const auto& entry : state_.hired) { hired[entry.first] = entry.second; }
  s["hired"] = hired;

  Json talked = Json::object();
  for (const auto& entry : state_.talked) { talked[entry.first] = entry.second; }
  s["talked"] = talked;

  Json visited = Json::object();
  for (const auto& entry : state_.visitedPorts) { visited[entry.first] = entry.second; }
  s["visitedPorts"] = visited;

  Json trades = Json::array();
  for (const TradeContract& trade : state_.trades) {
    trades.push_back(Json{{"mode", trade.mode},
                          {"from", trade.from},
                          {"to", trade.to},
                          {"good", trade.good},
                          {"crates", trade.crates},
                          {"fee", trade.fee},
                          {"freight", trade.freight},
                          {"label", trade.label}});
  }
  s["trades"] = trades;

  Json spent = Json::object();
  for (const auto& entry : state_.spentTrades) { spent[entry.first] = entry.second; }
  s["spentTrades"] = spent;

  Json rejects = Json::object();
  for (const auto& entry : state_.freelanceRejects) { rejects[entry.first] = entry.second; }
  s["freelanceRejects"] = rejects;

  s["gameOver"] = state_.gameOver;
  root["state"] = s;

  // ---- the rest of the snapshot -----------------------------------------
  root["actions"] = Num(actionCounter_);

  Json ship = Json::object();
  for (const auto& entry : ship_.fields) { ship[entry.first] = entry.second; }
  root["ship"] = ship;

  Json rooms = Json::array();
  for (const Room& room : rooms_) {
    rooms.push_back(Json{{"id", room.id},
                         {"name", room.name},
                         {"systems", room.systems},
                         {"tag", room.tag},
                         {"tone", room.tone},
                         {"x", Num(room.box.x)},
                         {"y", Num(room.box.y)},
                         {"w", Num(room.box.w)},
                         {"h", Num(room.box.h)},
                         {"small", room.small}});
  }
  root["rooms"] = rooms;

  Json crew = Json::array();
  for (const CrewMember& member : crew_) {
    crew.push_back(Json{{"id", member.id},
                        {"name", member.name},
                        {"role", member.role},
                        {"room", member.room},
                        {"activity", member.activity},
                        {"colour", member.colour},
                        {"sex", member.sex},
                        {"hair", member.hair},
                        {"hair2", member.hair2},
                        {"skin", member.skin},
                        {"hairStyle", member.hairStyle},
                        {"health", Num(member.health)},
                        {"morale", Num(member.morale)},
                        {"fatigue", Num(member.fatigue)},
                        {"hunger", Num(member.hunger)},
                        {"wallet", Num(member.wallet)},
                        {"hungerTick", Num(member.hungerTick)},
                        {"traits", member.traits},
                        {"race", member.race},
                        {"age", member.age},
                        {"homeworld", member.homeworld},
                        {"hobby", member.hobby},
                        {"religion", member.religion},
                        {"job", member.job},
                        {"skill", member.skill},
                        {"jobAt", Num(member.jobAt)}});
  }
  root["crew"] = crew;

  Json stats = Json::array();
  for (const Stat& stat : stats_) {
    stats.push_back(Json{{"id", stat.id},
                         {"label", stat.label},
                         {"value", Num(stat.value)},
                         {"unit", stat.unit},
                         {"colour", stat.colour}});
  }
  root["stats"] = stats;

  Json infos = Json::array();
  for (const InfoRow& row : infos_) {
    infos.push_back(Json{{"id", row.id}, {"label", row.label}, {"value", row.value}});
  }
  root["infos"] = infos;

  Json life = Json::object();
  life["canisters"] = Num(life_.canisters);
  life["capacity"] = Num(life_.capacity);
  life["lifeMax"] = Num(rules_.life.lifeMax);
  life["decay"] = Num(rules_.life.decay);
  Json slots = Json::array();
  for (const CanisterSlot& slot : life_.slots) {
    slots.push_back(Json{{"id", slot.id}, {"name", slot.name}, {"state", slot.state}});
  }
  life["slots"] = slots;
  root["life"] = life;

  root["mess"] = Json{{"rack", Num(mess_.rack)}, {"capacity", Num(mess_.capacity)},
                      {"rations", Num(mess_.rations)},
                      {"rationCapacity", Num(mess_.rationCapacity)},
                      {"cookRestock", cookRestock_}};
  root["cargo"] = Json{{"capacity", Num(cargo_.capacity)}};

  Json goods = Json::array();
  for (const GoodsLot& lot : goods_) {
    Json entry = Json{{"id", lot.id},
                      {"name", lot.name},
                      {"kind", lot.kind},
                      {"vol", Num(lot.vol)},
                      {"value", Num(lot.value)},
                      {"risk", lot.risk}};
    if (!lot.contractId.empty()) { entry["contractId"] = lot.contractId; }
    if (!lot.triggerId.empty()) { entry["trigger_id"] = lot.triggerId; }
    else if (!lot.contractId.empty()) { entry["trigger_id"] = lot.contractId; }
    goods.push_back(entry);
  }
  root["goods"] = goods;

  Json survey = Json::array();
  for (const SurveyRecord& record : survey_) {
    survey.push_back(Json{{"id", record.id},
                          {"name", record.name},
                          {"src", record.src},
                          {"value", Num(record.value)},
                          {"state", record.state}});
  }
  root["survey"] = survey;

  Json map = Json::object();
  map["w"] = Num(map_.w);
  map["h"] = Num(map_.h);
  map["cx"] = Num(map_.cx);
  map["cy"] = Num(map_.cy);
  map["vw"] = Num(map_.vw);
  map["vh"] = Num(map_.vh);
  map["zoom"] = Num(map_.zoom);
  map["zmin"] = Num(map_.zmin);
  map["zmax"] = Num(map_.zmax);
  map["zstep"] = Num(map_.zstep);
  map["here"] = map_.here;
  map["layer"] = map_.layer;
  map["sector"] = map_.sector;
  map["grid"] = Num(map_.grid);
  map["vpy"] = Num(map_.vpy);
  map["rungs"] = Num(map_.rungs);
  map["rungPow"] = Num(map_.rungPow);
  map["pxPerAction"] = Num(map_.pxPerAction);
  map["lyPerAction"] = Num(map_.lyPerAction);
  root["map"] = map;

  root["chart"] = Json{
      {"seed", std::to_string(chartSettings_.seed)},
      {"galaxies", chartSettings_.galaxies},
      {"mapSize", chartSettings_.mapSize},
      {"mapScale", Num(chartSettings_.mapScale)},
      {"spread", Num(chartSettings_.spread)},
  };

  Json views = Json::object();
  for (const std::string& layer : catalog_.layerOrder) {
    auto it = views_.find(layer);
    const LayerView view = it == views_.end() ? LayerView() : it->second;
    views[layer] = Json{{"zoom", Num(view.zoom)},
                        {"px", Num(view.px)},
                        {"py", Num(view.py)},
                        {"sel", view.sel.empty() ? Json(nullptr) : Json(view.sel)}};
  }
  root["views"] = views;

  Json events = Json::array();
  for (const Incident& incident : events_) {
    events.push_back(Json{{"id", incident.id},
                          {"type", incident.type},
                          {"stage", Num(incident.stage)},
                          {"left", Num(incident.left)},
                          {"a", incident.a},
                          {"b", incident.b}});
  }
  root["events"] = events;

  root["scanCost"] = Num(scanCost_);
  return root.dump(2);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool Simulation::ValidSave(const std::string& text, const Catalog& catalogIn, std::string* why) {
  auto fail = [why](const std::string& reason) {
    if (why != nullptr) { *why = reason; }
    return false;
  };

  Json d;
  try {
    d = Json::parse(text);
  } catch (const std::exception& error) {
    return fail(std::string("not JSON: ") + error.what());
  }

  static std::string cachedKey;
  static Catalog cachedExpanded;
  Catalog adHocExpanded;
  const Catalog* catalogPtr = &catalogIn;
  const ChartSettings saveChart = ReadChartSettings(d, catalogIn.chartSettings);
  if (!catalogIn.chartExpanded && !catalogIn.regions.empty() && !catalogIn.systems.empty()) {
    const std::string key =
        catalogIn.sourceSha256 + ":" + std::to_string(catalogIn.bodies.size()) + ":" +
        std::to_string(catalogIn.systems.size()) + ":" + std::to_string(catalogIn.regions.size()) +
        ":" + std::to_string(saveChart.seed) + ":" + std::to_string(saveChart.galaxies) + ":" +
        std::to_string(saveChart.mapScale) + ":" + std::to_string(saveChart.spread);
    if (!catalogIn.sourceSha256.empty()) {
      if (cachedKey != key) {
        cachedExpanded = catalogIn;
        ExpandChart(cachedExpanded, saveChart);
        cachedKey = key;
      }
      catalogPtr = &cachedExpanded;
    } else {
      adHocExpanded = catalogIn;
      ExpandChart(adHocExpanded, saveChart);
      catalogPtr = &adHocExpanded;
    }
  }
  const Catalog& catalog = *catalogPtr;
  if (!d.is_object()) { return fail("the file is not a save object"); }
  if (GetString(d, "format") != "spaceship-command-2") { return fail("unsupported format"); }

  const Json* state = At(d, "state");
  const Json* map = At(d, "map");
  const Json* ship = At(d, "ship");
  const Json* life = At(d, "life");
  const Json* mess = At(d, "mess");
  const Json* cargo = At(d, "cargo");
  const Json* views = At(d, "views");
  if (state == nullptr || !IsObject(*state)) { return fail("no state"); }
  if (map == nullptr || !IsObject(*map)) { return fail("no map"); }
  if (ship == nullptr || !IsObject(*ship)) { return fail("no ship"); }
  if (life == nullptr || !IsObject(*life)) { return fail("no life support"); }
  if (mess == nullptr || !IsObject(*mess)) { return fail("no galley"); }
  if (cargo == nullptr || !IsObject(*cargo)) { return fail("no hold"); }
  if (views == nullptr || !IsObject(*views)) { return fail("no layer views"); }

  for (const char* key : {"spOpen", "freshTabs", "pendingOrders", "scanned", "contracts", "upgrades",
                          "hired", "options", "chronicle", "commissions", "mapMarks"}) {
    if (!IsObject(*state)) { break; }
    if (key == std::string("options") || key == std::string("chronicle") ||
        key == std::string("commissions")) {
      continue;  // optional in older saves; checked below when present
    }
    const Json* value = At(*state, key);
    if (value == nullptr || !IsObject(*value)) { return fail(std::string("state.") + key + " missing"); }
  }

  if (!GetString(*ship, "name").size()) { return fail("ship has no name"); }

  const Json* actions = At(d, "actions");
  const Json* scanCost = At(d, "scanCost");
  if (actions == nullptr || !SaveNumber(*actions)) { return fail("actions"); }
  if (scanCost == nullptr || !SaveNumber(*scanCost) || scanCost->get<double>() < 1) {
    return fail("scanCost");
  }

  if (!SaveNumber(*At(*state, "shiplets") == nullptr ? Json(Json::value_t::null) : *At(*state, "shiplets"))) {
    return fail("shiplets");
  }
  for (const char* key : {"fuelCells", "repairKits", "lotSerial", "engineCondition", "wearTick",
                          "filterLife", "airHold", "crewTick", "payTick", "shiplets"}) {
    const Json* value = At(*state, key);
    if (value == nullptr || !SaveNumber(*value)) { return fail(std::string("state.") + key); }
  }
  if (GetNumber(*state, "engineCondition") > 100) { return fail("engine condition above 100"); }
  if (GetNumber(*state, "airHold") > 20) { return fail("more air than the cabin holds"); }

  const Json* zoom = At(*map, "zoom");
  if (zoom == nullptr || !SaveNumber(*zoom) || zoom->get<double>() <= 0) { return fail("map zoom"); }

  const std::string here = GetString(*map, "here");
  bool hereFound = false;
  for (const Body& body : catalog.bodies) {
    if (body.id == here) { hereFound = true; }
  }
  if (!hereFound) { return fail("the ship is nowhere"); }

  const std::string layer = GetString(*map, "layer");
  if (layer != "system" && layer != "sector" && layer != "galaxy") { return fail("map layer"); }

  if (GetBool(*state, "docked")) {
    if (catalog.FindPort(here) == nullptr) { return fail("docked where there is no port"); }
  }

  // ---- stores ------------------------------------------------------------
  const Json* slots = At(*life, "slots");
  if (slots == nullptr || !IsArray(*slots) || slots->size() != 3) { return fail("life slots"); }
  if (!SaveNumber(*At(*life, "canisters")) || !SaveNumber(*At(*life, "capacity")) ||
      !SaveNumber(*At(*life, "lifeMax")) || !SaveNumber(*At(*life, "decay"))) {
    return fail("life support numbers");
  }
  if (GetNumber(*life, "canisters") > GetNumber(*life, "capacity")) {
    return fail("more canisters than the rack holds");
  }
  if (GetNumber(*life, "lifeMax") < 1 || GetNumber(*life, "decay") < 1) { return fail("charge"); }
  for (const Json& slot : *slots) {
    if (!IsObject(slot) || !GetString(slot, "id").size() || !GetString(slot, "name").size()) {
      return fail("a canister slot is unreadable");
    }
    const std::string state = GetString(slot, "state");
    if (state != "empty" && state != "installed" && state != "active") {
      return fail("a canister slot is in an impossible state");
    }
  }

  if (!SaveNumber(*At(*mess, "rack")) || !SaveNumber(*At(*mess, "capacity"))) { return fail("galley"); }
  if (GetNumber(*mess, "rack") > GetNumber(*mess, "capacity")) { return fail("the rack overflows"); }
  if (!SaveNumber(*At(*cargo, "capacity"))) { return fail("cargo capacity"); }

  const Json* rooms = At(d, "rooms");
  const Json* crew = At(d, "crew");
  const Json* stats = At(d, "stats");
  const Json* infos = At(d, "infos");
  const Json* goods = At(d, "goods");
  const Json* survey = At(d, "survey");
  const Json* events = At(d, "events");
  if (rooms == nullptr || !IsArray(*rooms)) { return fail("rooms"); }
  if (crew == nullptr || !IsArray(*crew)) { return fail("crew"); }
  if (stats == nullptr || !IsArray(*stats)) { return fail("stats"); }
  if (infos == nullptr || !IsArray(*infos)) { return fail("infos"); }
  if (goods == nullptr || !IsArray(*goods)) { return fail("goods"); }
  if (survey == nullptr || !IsArray(*survey)) { return fail("survey"); }
  if (events == nullptr || !IsArray(*events)) { return fail("events"); }

  for (const Json& room : *rooms) {
    if (!IsObject(room) || !GetString(room, "id").size() || !GetString(room, "name").size()) {
      return fail("a room is unreadable");
    }
    for (const char* key : {"x", "y", "w", "h"}) {
      const Json* value = At(room, key);
      if (value == nullptr || !SaveNumber(*value)) { return fail("a room has no geometry"); }
    }
  }
  for (const char* required : {"engine", "cargo", "o2", "quarters", "mess"}) {
    bool found = false;
    for (const Json& room : *rooms) {
      if (GetString(room, "id") == required) { found = true; }
    }
    if (!found) { return fail(std::string("the deck has no ") + required + " room"); }
  }

  for (const Json& stat : *stats) {
    if (!IsObject(stat) || !GetString(stat, "id").size()) { return fail("a readout is unreadable"); }
    const Json* value = At(stat, "value");
    if (value == nullptr || !SaveNumber(*value) || value->get<double>() > 100) {
      return fail("a readout is out of range");
    }
  }
  for (const char* required : {"hull", "shield", "fuel", "energy"}) {
    bool found = false;
    for (const Json& stat : *stats) {
      if (GetString(stat, "id") == required) { found = true; }
    }
    if (!found) { return fail(std::string("no ") + required + " readout"); }
  }

  for (const Json& info : *infos) {
    if (!IsObject(info) || !GetString(info, "id").size()) { return fail("a readout row is unreadable"); }
  }
  bool hasLocation = false;
  bool hasLife = false;
  for (const Json& info : *infos) {
    if (GetString(info, "id") == "location") { hasLocation = true; }
    if (GetString(info, "id") == "life") { hasLife = true; }
  }
  if (!hasLocation || !hasLife) { return fail("the side panel has no location or life readout"); }

  auto roomExists = [rooms](const std::string& id) {
    for (const Json& room : *rooms) {
      if (GetString(room, "id") == id) { return true; }
    }
    return false;
  };
  auto jobExists = [&catalog](const std::string& id) {
    for (const auto& job : catalog.jobDefs) {
      if (job.id == id) { return true; }
    }
    return false;
  };

  for (const Json& member : *crew) {
    if (!IsObject(member) || !GetString(member, "id").size() || !GetString(member, "name").size() ||
        !GetString(member, "role").size()) {
      return fail("a crew member is unreadable");
    }
    if (!roomExists(GetString(member, "room"))) { return fail("a crew member is in a room that is not there"); }
    for (const char* key : {"health", "morale", "fatigue", "hunger"}) {
      const Json* value = At(member, key);
      if (value == nullptr || !SaveNumber(*value) || value->get<double>() > 100) {
        return fail(std::string("crew ") + key + " out of range");
      }
    }
    const std::string job = GetString(member, "job");
    if (!job.empty()) {
      if (!jobExists(job)) { return fail("a crew member holds a job that does not exist"); }
      const Json* at = At(member, "jobAt");
      if (at == nullptr || !SaveNumber(*at)) { return fail("a job with no rate"); }
      if (job == "filters" && at->get<double>() > 4) { return fail("a filter hand at an air stage that does not exist"); }
    }
  }

  // ---- the hold ----------------------------------------------------------
  double held = 0;
  for (const Json& lot : *goods) {
    if (!IsObject(lot) || !GetString(lot, "id").size() || !GetString(lot, "name").size() ||
        !GetString(lot, "kind").size()) {
      return fail("a cargo lot is unreadable");
    }
    if (!At(lot, "risk") || !At(lot, "risk")->is_boolean()) { return fail("a cargo lot has no risk flag"); }
    if (!SaveNumber(*At(lot, "vol")) || !SaveNumber(*At(lot, "value"))) { return fail("a cargo lot has no numbers"); }
    const std::string contractId = LotBindId(lot);
    if (!contractId.empty()) {
      bool found = false;
      if (contractId.compare(0, 6, "trade:") == 0) {
        const std::string key = contractId.substr(6);
        const Json* trades = At(*state, "trades");
        if (trades != nullptr && trades->is_array()) {
          for (const Json& trade : *trades) {
            if (!IsObject(trade)) { continue; }
            if (TradeKey(GetString(trade, "mode"), GetString(trade, "to"),
                         GetString(trade, "good")) == key) {
              found = true;
            }
          }
        }
        // A tagged lot still names the run even if the trades list was dropped.
        if (!found) { found = true; }
      } else {
        for (const Contract& contract : catalog.contracts) {
          if (contract.id == contractId) { found = true; }
        }
        if (!found && catalog.contracts.empty()) { found = true; }
      }
      if (!found) { return fail("contract cargo for a contract that does not exist"); }
    }
    held += GetNumber(lot, "vol");
  }

  for (const Json& record : *survey) {
    if (!IsObject(record) || !GetString(record, "id").size() || !GetString(record, "name").size() ||
        !GetString(record, "src").size() || !SaveNumber(*At(record, "value"))) {
      return fail("a survey record is unreadable");
    }
    const std::string state = GetString(record, "state");
    if (state != "filed" && state != "packaged") { return fail("a survey record is in an impossible state"); }
  }

  // ---- views, feed, tabs -------------------------------------------------
  for (const std::string& view : catalog.layerOrder) {
    const Json* entry = At(*views, view.c_str());
    if (entry == nullptr || !IsObject(*entry)) { return fail("a layer view is missing"); }
    const Json* vz = At(*entry, "zoom");
    if (vz == nullptr || !SaveNumber(*vz) || vz->get<double>() <= 0) { return fail("a layer view has no zoom"); }
    for (const char* key : {"px", "py"}) {
      const Json* value = At(*entry, key);
      if (value == nullptr || !value->is_number() || !std::isfinite(value->get<double>())) {
        return fail("a layer view has no pan");
      }
    }
  }

  const Json* feed = At(*state, "feed");
  if (feed == nullptr || !IsArray(*feed)) { return fail("no feed"); }
  for (const Json& line : *feed) {
    if (!IsObject(line) || !At(line, "t") || !At(line, "t")->is_string() || !At(line, "s") ||
        !At(line, "s")->is_string()) {
      return fail("a feed line is unreadable");
    }
  }

  const Json* tabs = At(*state, "tabs");
  if (tabs == nullptr || !IsArray(*tabs)) { return fail("no tabs"); }
  for (const Json& tab : *tabs) {
    if (!IsObject(tab) || !At(tab, "key") || !At(tab, "key")->is_string() || !At(tab, "id") ||
        !At(tab, "id")->is_string()) {
      return fail("a tab is unreadable");
    }
    const std::string kind = GetString(tab, "kind");
    if (kind != "nav" && kind != "station" && kind != "event" && kind != "crew" && kind != "system" &&
        kind != "room" && kind != "info" && kind != "wiki" && kind != "awards") {
      return fail("a tab is of a kind that does not exist");
    }
  }

  const Json* passengers = At(*state, "passengers");
  if (passengers == nullptr || !IsArray(*passengers)) { return fail("passengers"); }
  for (const Json& passenger : *passengers) {
    if (!IsObject(passenger) || !GetString(passenger, "id").size()) { return fail("a passenger is unreadable"); }
  }

  // ---- ongoing work ------------------------------------------------------
  const Json* ongoing = At(*state, "ongoing");
  if (ongoing == nullptr || !IsArray(*ongoing)) { return fail("ongoing work"); }

  auto taskTypes = catalog.taskTypes;
  auto typeKnown = [&taskTypes](const std::string& type) {
    return std::find(taskTypes.begin(), taskTypes.end(), type) != taskTypes.end();
  };
  std::vector<std::string> seen;
  for (const Json& task : *ongoing) {
    if (!IsObject(task)) { return fail("a task is unreadable"); }
    const std::string type = GetString(task, "type");
    const std::string id = GetString(task, "id");
    // Crew work on a report was removed. A save that still holds such a task
    // loads without it; the report itself stays open.
    if (type == "incident") { continue; }
    if (!typeKnown(type)) { return fail("a task of a type that does not exist"); }
    if (id.empty()) { return fail("a task with no id"); }
    if (std::find(seen.begin(), seen.end(), id) != seen.end()) { return fail("two tasks share an id"); }
    seen.push_back(id);

    const Json* total = At(task, "total");
    const Json* left = At(task, "left");
    if (total == nullptr || !SaveNumber(*total)) { return fail("a task with no total"); }
    if (left == nullptr || !SaveNumber(*left)) { return fail("a task with no remaining work"); }
    if (left->get<double>() > total->get<double>()) { return fail("a task with more left than it started with"); }
    const std::string taskState = GetString(task, "state");
    if (taskState != "running" && taskState != "done") { return fail("a task in an impossible state"); }

    const std::string target = GetString(task, "target");
    if (type == "travel") {
      bool found = false;
      for (const Body& body : catalog.bodies) {
        if (body.id == target) { found = true; }
      }
      for (const Body& body : catalog.systems) {
        if (body.id == target) { found = true; }
      }
      if (!found) { return fail("a crossing to a body that does not exist"); }
    }
    if (type == "scan") {
      if (catalog.FindBody(target) == nullptr && catalog.FindSystem(target) == nullptr &&
          catalog.FindRegion(target) == nullptr) {
        return fail("a scan of a body that does not exist");
      }
    }
    if (type == "extract") {
      if (catalog.FindBody(target) == nullptr) {
        return fail("an extraction on a body that does not exist");
      }
      const double vol = GetNumber(task, "vol");
      if (vol < 0) { return fail("an extraction with negative volume"); }
      if (taskState == "running" || taskState == "done") { held += vol; }
    }
    if (type == "o2slot") {
      const Json* slot = At(task, "slot");
      if (slot == nullptr || !slot->is_number()) { return fail("an installation with no slot"); }
      const double index = slot->get<double>();
      if (index < 0 || index >= static_cast<double>(slots->size())) {
        return fail("an installation in a slot that is not there");
      }
    }
    if (type == "salvage" && GetNumber(task, "vol") != 3) { return fail("salvage that is not three cubic metres"); }
    if (type == "salvage" && taskState == "running") { held += GetNumber(task, "vol"); }

    if (type == "shipProject") {
      if (taskState != "running") { return fail("a project that finished but was not announced"); }
      bool known = target == "medical" || target == "social";
      for (const ProjectDef& project : catalog.projects) {
        if (project.id == target) { known = true; }
      }
      if (!known) { return fail("a project that does not exist"); }
      if (target == "medical" && id != "doc-" + GetString(task, "who")) { return fail("medical work for the wrong patient"); }
      if (target == "social" && id != "project-social") { return fail("a shared meal with the wrong id"); }
      if (target == "medical" || target == "social") {
        bool whoThere = false;
        for (const Json& member : *crew) {
          if (GetString(member, "id") == GetString(task, "who")) { whoThere = true; }
        }
        if (!whoThere) { return fail("work about a crew member who is not aboard"); }
      }
      if (target == "social" && GetString(task, "other") == GetString(task, "who")) {
        return fail("a meal shared with oneself");
      }
    }
  }

  for (const Json& event : *events) {
    if (!IsObject(event) || !GetString(event, "id").size()) { return fail("an incident is unreadable"); }
    const IncidentType* incidentType = nullptr;
    for (const IncidentType& type : catalog.incidentTypes) {
      if (type.id == GetString(event, "type")) { incidentType = &type; }
    }
    if (incidentType == nullptr) { return fail("an incident of a type that does not exist"); }
    const double stage = GetNumber(event, "stage");
    if (stage < 0 || stage >= static_cast<double>(incidentType->stages.size())) {
      return fail("an incident waiting on a stage that does not exist");
    }
    const double ticks = incidentType->stages[static_cast<std::size_t>(stage)].ticks;
    const Json* left = At(event, "left");
    if (left == nullptr || !SaveNumber(*left) || left->get<double>() < 1 || left->get<double>() > ticks) {
      return fail("an incident whose countdown does not match its stage");
    }
  }

  if (held > GetNumber(*cargo, "capacity")) { return fail("the hold is fuller than the hold"); }
  return true;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace {

CrewMember ReadCrewMember(const Json& source) {
  CrewMember crew;
  crew.id = GetString(source, "id");
  crew.name = GetString(source, "name");
  crew.role = GetString(source, "role");
  crew.room = GetString(source, "room");
  crew.activity = GetString(source, "activity");
  crew.colour = GetString(source, "colour");
  crew.sex = GetString(source, "sex");
  crew.hair = GetString(source, "hair");
  crew.hair2 = GetString(source, "hair2");
  crew.skin = GetString(source, "skin");
  crew.hairStyle = GetString(source, "hairStyle");
  crew.health = GetNumber(source, "health");
  crew.morale = GetNumber(source, "morale");
  crew.fatigue = GetNumber(source, "fatigue");
  crew.hunger = GetNumber(source, "hunger");
  crew.wallet = GetNumber(source, "wallet");
  crew.hungerTick = GetNumber(source, "hungerTick");
  crew.traits = GetString(source, "traits");
  crew.race = GetString(source, "race");
  crew.age = static_cast<int>(GetNumber(source, "age"));
  crew.homeworld = GetString(source, "homeworld");
  crew.hobby = GetString(source, "hobby");
  crew.religion = GetString(source, "religion");
  crew.job = GetString(source, "job");
  crew.skill = GetString(source, "skill");
  crew.jobAt = GetNumber(source, "jobAt");
  return crew;
}

Task ReadTask(const Json& source) {
  Task task;
  task.id = GetString(source, "id");
  task.label = GetString(source, "label");
  task.total = GetNumber(source, "total");
  task.left = GetNumber(source, "left");
  task.state = GetString(source, "state");
  task.type = GetString(source, "type");
  task.target = GetString(source, "target");
  task.by = GetString(source, "by");
  task.who = GetString(source, "who");
  task.other = GetString(source, "other");
  if (auto it = source.find("slot"); it != source.end() && it->is_number()) { task.slot = it->get<double>(); }
  task.vol = GetNumber(source, "vol");
  return task;
}

}  // namespace

void Simulation::ApplySnapshotDefaults() {
  // Tabs are de-duplicated, and a dispute with no pair is given one.
  std::vector<Tab> tabs;
  for (const Tab& tab : state_.tabs) {
    bool seen = false;
    for (const Tab& kept : tabs) {
      if (kept.key == tab.key) { seen = true; }
    }
    if (!seen) { tabs.push_back(tab); }
  }
  state_.tabs = tabs;
  for (Incident& incident : events_) {
    if (incident.type == "crew" && incident.a.empty() && crew_.size() >= 2) {
      const std::pair<std::string, std::string> pair = DisputePair();
      incident.a = pair.first;
      incident.b = pair.second;
    }
  }

  // migrateProgression(): every later system is present, and the chronicle is
  // rebuilt from what the voyage already knows.
  if (state_.options.theme.empty()) { state_.options.theme = "classic"; }
  state_.chronicle.version = 1;
  ApplyShipLayout();
  ChronicleVisit(CurrentSystem());
  CheckCommendations(true);
}

bool Simulation::LoadJson(const std::string& text, std::string* error) {
  std::string why;
  const Catalog& source = sourceCatalog_.regions.empty() ? catalog_ : sourceCatalog_;
  if (!ValidSave(text, source, &why)) {
    if (error != nullptr) { *error = "The save is incomplete or has an unsupported format: " + why; }
    return false;
  }

  // Transactional: keep the live voyage, and put it back if anything below fails.
  const Simulation before = *this;

  try {
    const Json d = Json::parse(text);
    chartSettings_ = ReadChartSettings(d, chartSettings_);
    if (!source.chartExpanded) {
      catalog_ = source;
      catalog_.chartExpanded = false;
      ExpandChart(catalog_, chartSettings_);
    }
    const Json& s = d.at("state");

    actionCounter_ = d.at("actions").get<double>();
    scanCost_ = d.at("scanCost").get<double>();

    state_ = GameState();
    state_.activeTab = GetString(s, "activeTab");
    state_.tabs.clear();
    for (const Json& tab : s.at("tabs")) {
      Tab entry;
      entry.key = GetString(tab, "key");
      entry.id = GetString(tab, "id");
      entry.kind = GetString(tab, "kind");
      entry.title = GetString(tab, "title");
      entry.kindLabel = GetString(tab, "kindLabel");
      entry.sub = GetString(tab, "sub");
      state_.tabs.push_back(entry);
    }
    state_.feed.clear();
    for (const Json& line : s.at("feed")) { state_.feed.push_back(FeedLine{GetString(line, "t"), GetString(line, "s")}); }
    state_.popups = GetNumber(s, "popups");
    state_.reducedMotion = GetBool(s, "reducedMotion");
    state_.shipClass = GetString(s, "shipClass", "wren");
    state_.medkits = GetNumber(s, "medkits", 2);
    state_.engineCondition = GetNumber(s, "engineCondition");
    state_.wearTick = GetNumber(s, "wearTick");
    state_.engineerRun = GetBool(s, "engineerRun");
    state_.filterLife = GetNumber(s, "filterLife");
    state_.airHold = GetNumber(s, "airHold");
    state_.rackWarned = GetBool(s, "rackWarned");
    state_.rackWarnedMess = GetBool(s, "rackWarnedMess");
    state_.crewSel = GetString(s, "crewSel");
    state_.crewTick = GetNumber(s, "crewTick");
    state_.shiplets = GetNumber(s, "shiplets");
    state_.payTick = GetNumber(s, "payTick");
    state_.docked = GetBool(s, "docked");
    state_.fuelCells = GetNumber(s, "fuelCells");
    state_.repairKits = GetNumber(s, "repairKits");
    state_.lotSerial = GetNumber(s, "lotSerial");
    state_.gameOver = GetString(s, "gameOver");

    state_.navTarget.reset();
    if (auto it = s.find("navTarget"); it != s.end() && it->is_string()) {
      state_.navTarget = it->get<std::string>();
    }

    auto readBoolMap = [](const Json& object, std::map<std::string, bool>* target) {
      target->clear();
      if (!object.is_object()) { return; }
      for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().is_boolean()) { (*target)[it.key()] = it.value().get<bool>(); }
      }
    };
    readBoolMap(s.at("scanned"), &state_.scanned);
    readBoolMap(s.at("pendingOrders"), &state_.pendingOrders);
    readBoolMap(s.at("upgrades"), &state_.upgrades);
    readBoolMap(s.at("hired"), &state_.hired);
    // Optional: a save written before the station bar existed has no tally,
    // and an absent tally simply means nobody has been spoken to yet.
    state_.talked.clear();
    if (auto talkedIt = s.find("talked"); talkedIt != s.end()) {
      readBoolMap(*talkedIt, &state_.talked);
    }
    // Optional: a save written before the stations kept a roll of visits has
    // none, and an absent roll simply means the voyage has just begun.
    state_.visitedPorts.clear();
    if (auto it = s.find("visitedPorts"); it != s.end() && it->is_object()) {
      readBoolMap(*it, &state_.visitedPorts);
    }
    state_.trades.clear();
    if (auto it = s.find("trades"); it != s.end() && it->is_array()) {
      for (const Json& entry : *it) {
        if (!entry.is_object()) { continue; }
        TradeContract trade;
        trade.mode = GetString(entry, "mode");
        trade.from = GetString(entry, "from");
        trade.to = GetString(entry, "to");
        trade.good = GetString(entry, "good");
        trade.crates = static_cast<int>(GetNumber(entry, "crates"));
        trade.fee = GetNumber(entry, "fee");
        trade.freight = GetNumber(entry, "freight");
        trade.label = GetString(entry, "label");
        if (!trade.mode.empty() && !trade.to.empty()) { state_.trades.push_back(trade); }
      }
    }
    state_.spentTrades.clear();
    if (auto it = s.find("spentTrades"); it != s.end() && it->is_object()) {
      readBoolMap(*it, &state_.spentTrades);
    }
    state_.freelanceRejects.clear();
    if (auto it = s.find("freelanceRejects"); it != s.end() && it->is_object()) {
      for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (entry.value().is_number()) {
          state_.freelanceRejects[entry.key()] = static_cast<int>(entry.value().get<double>());
        }
      }
    }
    stowed_.clear();
    stowOrder_.clear();
    cargoMixGen_.clear();
    justStowedLot_.clear();
    mixClicks_ = 0;
    mixThreshold_ = 0;
    readBoolMap(s.at("spOpen"), &state_.spOpen);
    readBoolMap(s.at("freshTabs"), &state_.freshTabs);

    state_.contracts.clear();
    if (auto it = s.find("contracts"); it != s.end() && it->is_object()) {
      for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (entry.value().is_string()) { state_.contracts[entry.key()] = entry.value().get<std::string>(); }
      }
    }

    state_.passengers.clear();
    for (const Json& passenger : s.at("passengers")) {
      state_.passengers.push_back(Passenger{GetString(passenger, "id"), GetString(passenger, "name")});
    }

    state_.charges.clear();
    if (auto it = s.find("charges"); it != s.end() && it->is_object()) {
      for (auto entry = it->begin(); entry != it->end(); ++entry) {
        Charge charge;
        charge.left = GetNumber(entry.value(), "left");
        charge.volume = GetNumber(entry.value(), "volume");
        state_.charges[entry.key()] = charge;
      }
    }

    if (auto it = s.find("options"); it != s.end() && it->is_object()) {
      state_.options.largeText = GetBool(*it, "largeText");
      state_.options.confirmRisk = GetBool(*it, "confirmRisk");
      state_.options.hideCrewLabels = GetBool(*it, "hideCrewLabels");
      state_.options.theme = GetString(*it, "theme", "classic");
      state_.options.soundEffects = GetBool(*it, "soundEffects", true);
      state_.options.music = GetBool(*it, "music", true);
      state_.options.audioLevel = GetNumber(*it, "audioLevel", 0.4);
      const double savedGamma = GetNumber(*it, "displayGamma", 1.0);
      // Old saves used the shader exponent directly. Preserve deliberate
      // non-default choices, while the old default adopts the new baseline.
      if (GetString(*it, "displayGammaScale") == "relative-1.6" || savedGamma == 1.0) {
        state_.options.displayGamma = DisplayGamma(savedGamma);
      } else {
        state_.options.displayGamma = DisplayGamma(
            std::clamp(savedGamma, 0.7, 1.6) / kDisplayGammaBaseline);
      }
      state_.options.displayBrightness = DisplayBrightness(GetNumber(*it, "displayBrightness", 1.0));
      const double step = GetNumber(*it, "fontStep", 0.0);
      state_.options.fontStep = step < 0 ? 0 : (step > 3 ? 3 : static_cast<int>(step));
      state_.options.fullscreen = GetBool(*it, "fullscreen", false);
    }

    state_.chronicle = Chronicle();
    if (auto it = s.find("chronicle"); it != s.end() && it->is_object()) {
      state_.chronicle.banks = GetNumber(*it, "banks");
      state_.chronicle.projects = GetNumber(*it, "projects");
      state_.chronicle.meals = GetNumber(*it, "meals");
      state_.chronicle.incidents = GetNumber(*it, "incidents");
      if (auto systems = it->find("systems"); systems != it->end()) { readBoolMap(*systems, &state_.chronicle.systems); }
      if (auto regions = it->find("regions"); regions != it->end()) { readBoolMap(*regions, &state_.chronicle.regions); }
      if (auto earned = it->find("earned"); earned != it->end()) { readBoolMap(*earned, &state_.chronicle.earned); }
      if (auto claimed = it->find("claimed"); claimed != it->end()) { readBoolMap(*claimed, &state_.chronicle.claimed); }
    }

    state_.systemSerials.clear();
    if (auto it = s.find("systemSerials"); it != s.end() && it->is_object()) {
      for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (entry.value().is_number()) {
          state_.systemSerials[entry.key()] = static_cast<int>(entry.value().get<double>());
        }
      }
    }
    state_.nextSystemSerial = static_cast<int>(GetNumber(s, "nextSystemSerial", 1.0));
    if (state_.nextSystemSerial < 1) { state_.nextSystemSerial = 1; }
    // A save written before this existed still knows which systems were visited:
    // number them in the order the chronicle holds them, so the tag is never empty
    // on a voyage that has arrived somewhere.
    for (const auto& entry : state_.chronicle.systems) {
      if (!entry.second) { continue; }
      if (state_.systemSerials.find(entry.first) == state_.systemSerials.end()) {
        state_.systemSerials[entry.first] = state_.nextSystemSerial++;
      }
    }

    state_.commissions = Commissions();
    if (auto it = s.find("commissions"); it != s.end() && it->is_object()) {
      state_.commissions.serial = GetNumber(*it, "serial");
      state_.commissions.completed = GetNumber(*it, "completed");
      if (auto active = it->find("active"); active != it->end() && active->is_object()) {
        Commission commission;
        commission.kind = GetString(*active, "kind");
        commission.origin = GetString(*active, "origin");
        commission.target = GetString(*active, "target");
        commission.progress = GetNumber(*active, "progress");
        commission.reward = GetNumber(*active, "reward");
        state_.commissions.active = commission;
      }
    }

    state_.mapMarks.clear();
    if (auto it = s.find("mapMarks"); it != s.end()) { readBoolMap(*it, &state_.mapMarks); }

    state_.ongoing.clear();
    for (const Json& task : s.at("ongoing")) {
      if (GetString(task, "type") == "incident") { continue; }  // removed; see the check above
      state_.ongoing.push_back(ReadTask(task));
    }

    ship_.fields.clear();
    for (auto it = d.at("ship").begin(); it != d.at("ship").end(); ++it) {
      if (it.value().is_string()) { ship_.fields[it.key()] = it.value().get<std::string>(); }
    }

    rooms_.clear();
    for (const Json& room : d.at("rooms")) {
      Room entry;
      entry.id = GetString(room, "id");
      entry.name = GetString(room, "name");
      entry.systems = GetString(room, "systems");
      entry.tag = GetString(room, "tag");
      entry.tone = GetString(room, "tone");
      entry.box.x = GetNumber(room, "x");
      entry.box.y = GetNumber(room, "y");
      entry.box.w = GetNumber(room, "w");
      entry.box.h = GetNumber(room, "h");
      entry.small = GetBool(room, "small");
      rooms_.push_back(entry);
    }

    crew_.clear();
    for (const Json& member : d.at("crew")) { crew_.push_back(ReadCrewMember(member)); }

    stats_.clear();
    for (const Json& stat : d.at("stats")) {
      Stat entry;
      entry.id = GetString(stat, "id");
      entry.label = GetString(stat, "label");
      entry.value = GetNumber(stat, "value");
      entry.unit = GetString(stat, "unit");
      entry.colour = GetString(stat, "colour");
      stats_.push_back(entry);
    }

    infos_.clear();
    for (const Json& info : d.at("infos")) {
      infos_.push_back(InfoRow{GetString(info, "id"), GetString(info, "label"), GetString(info, "value")});
    }

    life_.canisters = GetNumber(d.at("life"), "canisters");
    life_.capacity = GetNumber(d.at("life"), "capacity");
    life_.slots.clear();
    for (const Json& slot : d.at("life").at("slots")) {
      life_.slots.push_back(CanisterSlot{GetString(slot, "id"), GetString(slot, "name"), GetString(slot, "state")});
    }

    mess_.rack = GetNumber(d.at("mess"), "rack");
    mess_.capacity = GetNumber(d.at("mess"), "capacity");
    // Older voyages carry no larder: they restore with an empty store, which
    // is exactly the situation their save describes - they never cooked one.
    const Json& galley = d.at("mess");
    mess_.rations = At(galley, "rations") != nullptr ? GetNumber(galley, "rations") : 0.0;
    if (At(galley, "rationCapacity") != nullptr) {
      mess_.rationCapacity = GetNumber(galley, "rationCapacity");
    }
    cookRestock_ = At(galley, "cookRestock") != nullptr && GetBool(galley, "cookRestock");
    cargo_.capacity = GetNumber(d.at("cargo"), "capacity");

    goods_.clear();
    for (const Json& lot : d.at("goods")) {
      GoodsLot entry;
      entry.id = GetString(lot, "id");
      entry.name = GetString(lot, "name");
      entry.kind = GetString(lot, "kind");
      entry.vol = GetNumber(lot, "vol");
      entry.value = GetNumber(lot, "value");
      entry.risk = GetBool(lot, "risk");
      entry.contractId = GetString(lot, "contractId");
      entry.triggerId = GetString(lot, "trigger_id");
      if (entry.contractId.empty()) {
        const std::string bind = LotBindId(lot);
        if (bind.compare(0, 6, "trade:") == 0) { entry.contractId = bind; }
        else { entry.contractId = bind; }
      }
      goods_.push_back(entry);
    }
    for (const GoodsLot& lot : goods_) {
      if (lot.contractId.compare(0, 6, "trade:") != 0) { continue; }
      const std::string key = lot.contractId.substr(6);
      bool have = false;
      for (const TradeContract& trade : state_.trades) {
        if (TradeKey(trade.mode, trade.to, trade.good) == key) { have = true; break; }
      }
      if (have) { continue; }
      TradeContract trade;
      const std::size_t a = key.find('|');
      const std::size_t b = a == std::string::npos ? std::string::npos : key.find('|', a + 1);
      if (a == std::string::npos || b == std::string::npos) { continue; }
      trade.mode = key.substr(0, a);
      trade.to = key.substr(a + 1, b - a - 1);
      trade.good = key.substr(b + 1);
      trade.label = lot.name;
      state_.trades.push_back(std::move(trade));
    }

    survey_.clear();
    for (const Json& record : d.at("survey")) {
      SurveyRecord entry;
      entry.id = GetString(record, "id");
      entry.name = GetString(record, "name");
      entry.src = GetString(record, "src");
      entry.value = GetNumber(record, "value");
      entry.state = GetString(record, "state");
      survey_.push_back(entry);
    }

    const Json& map = d.at("map");
    map_.w = GetNumber(map, "w", 638);
    map_.h = GetNumber(map, "h", 278);
    map_.cx = GetNumber(map, "cx", 319);
    map_.cy = GetNumber(map, "cy", 139);
    map_.vw = GetNumber(map, "vw", 638);
    map_.vh = GetNumber(map, "vh", 278);
    map_.zoom = GetNumber(map, "zoom", 1);
    map_.zmin = GetNumber(map, "zmin", 0.6);
    map_.zmax = GetNumber(map, "zmax", 2.0);
    map_.zstep = GetNumber(map, "zstep", 0.2);
    map_.here = GetString(map, "here", "kessler");
    map_.layer = GetString(map, "layer", "system");
    map_.sector = GetString(map, "sector");
    map_.grid = GetNumber(map, "grid", 45);
    map_.vpy = GetNumber(map, "vpy", 0.35);
    map_.rungs = GetNumber(map, "rungs", 9);
    map_.rungPow = GetNumber(map, "rungPow", 2.1);
    map_.pxPerAction = GetNumber(map, "pxPerAction", 16);
    map_.lyPerAction = GetNumber(map, "lyPerAction", 0.42);
    if (map_.sector.empty()) {
      const Body* system = CurrentSystem();
      if (system != nullptr) { map_.sector = system->region; }
    }

    views_.clear();
    for (const std::string& layer : catalog_.layerOrder) {
      LayerView view;
      if (auto it = d.at("views").find(layer); it != d.at("views").end() && it->is_object()) {
        view.zoom = GetNumber(*it, "zoom", 1);
        view.px = GetNumber(*it, "px");
        view.py = GetNumber(*it, "py");
        view.sel = GetString(*it, "sel");
      }
      views_[layer] = view;
    }
    // The board is a property of the catalogue, not of the file: a save written
    // before the sector chart widened still loads onto the board the chart is
    // drawn on now. ApplyChartBoard() leaves the saved zoom alone.
    ApplyChartBoard();

    events_.clear();
    for (const Json& event : d.at("events")) {
      Incident entry;
      entry.id = GetString(event, "id");
      entry.type = GetString(event, "type");
      entry.stage = static_cast<int>(GetNumber(event, "stage"));
      entry.left = GetNumber(event, "left");
      entry.a = GetString(event, "a");
      entry.b = GetString(event, "b");
      if (const IncidentType* type = IncidentSpec(entry)) {
        entry.kind = type->kind;
        entry.source = type->source;
      }
      events_.push_back(entry);
    }

    ApplySnapshotDefaults();
    ++feedRevision_; // A restored log is rendered newest-first, like renderFeed().
    return true;
  } catch (const std::exception& failure) {
    *this = before;
    if (error != nullptr) { *error = std::string("The save could not be applied: ") + failure.what(); }
    return false;
  }
}

// ---------------------------------------------------------------------------
// Files, and the two encodings
// ---------------------------------------------------------------------------

namespace {

std::string Utf8ToUtf16Le(const std::string& utf8, bool withBom) {
  std::string out;
  if (withBom) { out.push_back(static_cast<char>(0xFF)); out.push_back(static_cast<char>(0xFE)); }
  std::size_t index = 0;
  while (index < utf8.size()) {
    unsigned int code = 0;
    const unsigned char lead = static_cast<unsigned char>(utf8[index]);
    int extra = 0;
    if (lead < 0x80) { code = lead; }
    else if ((lead >> 5) == 0x6) { code = lead & 0x1F; extra = 1; }
    else if ((lead >> 4) == 0xE) { code = lead & 0x0F; extra = 2; }
    else { code = lead & 0x07; extra = 3; }
    if (index + static_cast<std::size_t>(extra) >= utf8.size() + 0 && extra > 0) {
      // Truncated sequence: stop rather than invent characters.
      break;
    }
    for (int k = 1; k <= extra; ++k) {
      code = (code << 6) | (static_cast<unsigned char>(utf8[index + static_cast<std::size_t>(k)]) & 0x3F);
    }
    index += static_cast<std::size_t>(extra) + 1;
    if (code < 0x10000) {
      out.push_back(static_cast<char>(code & 0xFF));
      out.push_back(static_cast<char>((code >> 8) & 0xFF));
    } else {
      code -= 0x10000;
      const unsigned int high = 0xD800 + (code >> 10);
      const unsigned int low = 0xDC00 + (code & 0x3FF);
      out.push_back(static_cast<char>(high & 0xFF));
      out.push_back(static_cast<char>((high >> 8) & 0xFF));
      out.push_back(static_cast<char>(low & 0xFF));
      out.push_back(static_cast<char>((low >> 8) & 0xFF));
    }
  }
  return out;
}

std::string Utf16LeToUtf8(const std::string& utf16, std::size_t start) {
  std::string out;
  std::vector<unsigned int> units;
  for (std::size_t index = start; index + 1 < utf16.size(); index += 2) {
    units.push_back(static_cast<unsigned char>(utf16[index]) |
                    (static_cast<unsigned int>(static_cast<unsigned char>(utf16[index + 1])) << 8));
  }
  for (std::size_t index = 0; index < units.size(); ++index) {
    unsigned int code = units[index];
    if (code >= 0xD800 && code <= 0xDBFF && index + 1 < units.size()) {
      const unsigned int low = units[index + 1];
      if (low >= 0xDC00 && low <= 0xDFFF) {
        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        index += 1;
      }
    }
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }
  return out;
}

}  // namespace

std::string Simulation::SlotFilename(int slot) {
  if (slot == 2) { return "Spaceship_save_2.txt"; }
  if (slot == 3) { return "Spaceship_save_3.txt"; }
  return "Spaceship_save.txt";
}

bool Simulation::SlotIsUtf16(int slot) { return slot != 2 && slot != 3; }

bool Simulation::SaveSlot(int slot, const std::string& directory, std::string* error) const {
  const std::string path = directory.empty() ? SlotFilename(slot) : directory + "/" + SlotFilename(slot);
  return SaveToFile(path, SlotIsUtf16(slot), error);
}

bool Simulation::LoadSlot(int slot, const std::string& directory, std::string* error) {
  const std::string path = directory.empty() ? SlotFilename(slot) : directory + "/" + SlotFilename(slot);
  return LoadFromFile(path, error);
}

bool Simulation::SaveToFile(const std::string& path, bool utf16le, std::string* error) const {
  const std::filesystem::path savePath = std::filesystem::u8path(path);
  const std::filesystem::path parent = savePath.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      if (error != nullptr) {
        *error = "could not create save folder " + parent.u8string() + ": " + ec.message() +
                 ". Extract the game to a writable folder and try again.";
      }
      return false;
    }
  }
  const std::string text = SaveJson();
  std::ofstream file(savePath, std::ios::binary | std::ios::trunc);
  if (!file) {
    if (error != nullptr) {
      *error = "could not open " + path +
               ". Check folder permissions and extract the game before running it.";
    }
    return false;
  }
  const std::string bytes = utf16le ? Utf8ToUtf16Le(text, true) : text;
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  file.close();  // surface buffered write/close failures instead of claiming a save succeeded
  if (!file) {
    if (error != nullptr) { *error = "could not write " + path; }
    return false;
  }
  return true;
}

bool Simulation::LoadFromFile(const std::string& path, std::string* error) {
  std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
  if (!file) {
    if (error != nullptr) { *error = "No saved game found at " + path; }
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  std::string bytes = buffer.str();

  std::string text = bytes;
  if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
      static_cast<unsigned char>(bytes[1]) == 0xFE) {
    text = Utf16LeToUtf8(bytes, 2);
  } else if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
             static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
    text = bytes.substr(3);
  }
  return LoadJson(text, error);
}

}  // namespace ship

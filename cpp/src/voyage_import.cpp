#include <algorithm>
#include <stdexcept>

#include "voyage.hpp"

namespace spaceship {
namespace {
const Json* find(const Json& list, const std::string& id) {
  for (const auto& item : list)
    if (item.value("id", std::string{}) == id) return &item;
  return nullptr;
}
std::string text(const Json& j, const char* key) { return j.value(key, std::string{}); }
void defaults(Json& value, const Json& model) {
  if (!value.is_object() || !model.is_object()) return;
  for (auto it = model.begin(); it != model.end(); ++it)
    if (!value.contains(it.key()))
      value[it.key()] = it.value();
    else
      defaults(value[it.key()], it.value());
}
}  // namespace
Json Voyage::importLegacy(const Json& original) const {
  if (original.value("format", std::string{}) != "spaceship-command-1")
    throw std::runtime_error("Unsupported save format.");
  Json d = original;
  for (auto field : {"state", "crew", "stats", "life", "mess", "cargo", "goods", "survey", "map",
                     "rooms", "events"})
    if (!d.contains(field))
      throw std::runtime_error(std::string("Legacy save is missing ") + field);
  auto& s = d.at("state");
  if (s.contains("nativeTasks")) throw std::runtime_error("Native save incorrectly marked as HTA.");
  const bool preClass = !s.contains("shipClass");
  defaults(d, catalog()["initial"]);
  if (preClass) {
    s["shipClass"] = "kestrel";
    d["ship"] = catalog()["largeShip"];
  }
  if (text(s, "shipClass") != "wren" && text(s, "shipClass") != "kestrel")
    throw std::runtime_error("Unknown legacy ship class.");
  // Layout is authored, not trusted executable/UI geometry from the save.
  d["rooms"] =
      text(s, "shipClass") == "wren" ? catalog()["initial"]["rooms"] : catalog()["largeRooms"];
  s["nativeTasks"] = Json::array();
  s["nativeIncidents"] = Json::array();
  s["nativeVisited"] = Json::object();
  s["nativeVisited"][text(d["map"], "here")] = true;
  s["nativeDreamSelection"] = Json::array();
  if (!s["feed"].is_array()) throw std::runtime_error("Invalid legacy log.");
  for (auto& line : s["feed"]) {
    if (line.is_string()) continue;
    line = text(line, "t") + " | " + text(line, "s");
  }
  for (auto& slot : d["life"]["slots"])
    if (text(slot, "state") == "installed") slot["state"] = "seated";
  for (const auto& ev : d["events"]) {
    auto type = text(ev, "type");
    if (!find(catalog()["incidents"], type)) throw std::runtime_error("Unknown legacy incident.");
    Json converted = ev;
    converted["id"] = type;
    s["nativeIncidents"].push_back(converted);
  }
  for (const auto& t : s["ongoing"]) {
    const auto type = text(t, "type");
    std::string id, target = t.value("target", Json{}).is_string() ? text(t, "target") : "";
    Json payload = nullptr;
    if (type == "travel") {
      id = "travel";
      if (const auto* sys = find(catalog()["systems"], target)) target = text(*sys, "entry");
    } else if (type == "scan")
      id = "scan";
    else if (type == "maint")
      id = "maintain";
    else if (type == "cook") {
      id = "cook";
      payload = {{"rate", t.value("rate", 1)}};
    } else if (type == "o2slot") {
      id = "install-" + target;
      const auto index = t.at("slot").get<size_t>();
      if (index >= 3) throw std::runtime_error("Invalid canister slot.");
      if (text(t, "state") == "running") d["life"]["slots"][index]["state"] = "installing";
    } else if (type == "crewOrder") {
      id = "crew-sheet";
      payload = t.value("sheet", Json::object());
    } else if (type == "crewJob") {
      target = text(t, "who");
      id = "orders-" + target;
      payload = t.at("orders");
    } else if (type == "crewDoc") {
      target = text(t, "who");
      id = "doctor-" + target;
    } else if (type == "surveyPackage") {
      id = "package-" + target;
    } else if (type == "salvage") {
      id = text(t, "id");
      payload = {{"vol", t.at("vol")}};
    } else if (type == "incident") {
      const auto* event = find(d["events"], target);
      if (!event) throw std::runtime_error("Legacy incident work has no active report.");
      target = text(*event, "type");
      id = "incident-" + target;
    } else if (type == "shipProject") {
      if (target == "medical") {
        target = text(t, "who");
        id = "medical-" + target;
      } else if (target == "social") {
        id = "social";
        payload = Json::array({text(t, "who"), text(t, "other")});
        target = "";
      } else {
        id = "project-" + target;
        if (target == "observe")
          target = t.at("payload").get<std::string>();
        else
          target = "";
        payload = t.value("payload", Json{});
      }
      if (id == "project-dream")
        for (auto& part : payload) {
          if (part.is_string()) part = {{"kind", part}, {"text", part}, {"who", ""}, {"other", ""}};
        }
    } else
      throw std::runtime_error("Legacy task cannot be safely imported: " + type);
    Json task = {{"id", id},
                 {"target", target},
                 {"left", t.at("left")},
                 {"total", t.at("total")},
                 {"payload", payload}};
    if (text(t, "state") == "done") {
      if (type != "travel" && type != "scan")
        throw std::runtime_error("Unexpected completed legacy task: " + type);
      task["state"] = "done";
    }
    if (t.contains("by")) {
      const Json* worker = nullptr;
      for (const auto& p : d["crew"])
        if (text(p, "name") == text(t, "by")) worker = &p;
      if (worker) task["by"] = text(*worker, "id");
    }
    s["nativeTasks"].push_back(task);
  }
  Json cooldowns = Json::object();
  for (auto it = s["actionCooldowns"].begin(); it != s["actionCooldowns"].end(); ++it) {
    auto category = text(it.value(), "category");
    int tier = category == "routine"    ? 5
               : category == "systems"  ? 10
               : category == "space"    ? 15
               : category == "critical" ? 20
                                        : 0;
    if (!tier) throw std::runtime_error("Invalid legacy cooldown category.");
    const auto source = it.key();
    std::string key;
    auto pos = source.find("progress:");
    std::string action = pos == std::string::npos ? "" : source.substr(pos + 9);
    if (action == "travel" || action == "scan")
      key = "nav:" + action;
    else if (action == "maint" || action == "maint-eng")
      key = "maintain:";
    else if (action == "cook")
      key = "cook:";
    else if (action == "crew-assign")
      key = "issue-orders:";
    else if (action.rfind("o2s", 0) == 0)
      key = "install:s" + action.substr(3);
    else if (action.rfind("project-", 0) == 0)
      key = "project:" + action.substr(8);
    else if (action.rfind("salvage-", 0) == 0)
      key = "salvage:" + action.substr(8);
    else if (action.rfind("pkg-", 0) == 0)
      key = "package:" + action.substr(4);
    else
      throw std::runtime_error(
          "Unmapped legacy button cooldown. Let it expire in the reference game before "
          "importing: " +
          source);
    const int remaining = it.value().at("remaining").get<int>();
    if (remaining < 1 || remaining > tier)
      throw std::runtime_error("Invalid legacy cooldown range.");
    if (!cooldowns.contains(key) || cooldowns[key]["remaining"].get<int>() < remaining)
      cooldowns[key] = {{"tier", tier}, {"remaining", remaining}};
  }
  s["actionCooldowns"] = cooldowns;
  auto& commission = s["commissions"]["active"];
  if (!commission.is_null()) {
    auto kind = text(commission, "kind");
    commission["done"] = commission.at("progress");
    commission["goal"] = kind == "salvage" ? 2 : 1;
  }
  // Preserve source-only UI/progression records for inspection and future migrations.
  d["nativeLegacyArchive"] = original;
  d["format"] = "spaceship-native-1";
  return d;
}
}  // namespace spaceship

#include "voyage.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <codecvt>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>

#include "native_data.hpp"

namespace spaceship {
namespace {
const Json* find(const Json& list, const std::string& id) {
  for (const auto& item : list)
    if (item.value("id", "") == id) return &item;
  return nullptr;
}
std::string text(const Json& j, const char* key) { return j.value(key, std::string{}); }
int number(const Json& j, const char* key) { return j.value(key, 0); }
void add(Json& j, const char* key, int amount) { j[key] = number(j, key) + amount; }
std::string bodySystem(const Json& b) { return b.value("system", std::string("sec-kessler")); }
std::string keyFor(const std::string& c, const std::string& a) {
  if (c == "jump") return "nav:travel";
  if (c == "salvage") return "salvage:" + (a.empty() ? std::string("0") : a);
  if (c == "travel" || c == "scan") return "nav:" + c;
  for (auto global :
       {"watch",        "fuel",     "recharge",   "refuel",  "splitfuel", "service-shield",
        "repair",       "maintain", "filters",    "cook",    "dock",      "undock",
        "issue-orders", "social",   "collect",    "abandon", "ship",      "sell-data",
        "broadcast",    "preserve", "sell-signal"})
    if (c == global) return c + ":";
  return c + ":" + a;
}
std::string controlName(const std::string& c, const std::string& a) {
  std::string name = c == "watch" ? "Stand watch" : c == "issue-orders" ? "Issue crew orders" : c;
  std::replace(name.begin(), name.end(), '-', ' ');
  if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  if (!a.empty() && keyFor(c, a) != c + ":") name += " / " + a;
  return name;
}

int tier(const std::string& c) {
  if (c == "claim") return 0;
  if (c == "watch" || c == "broadcast") return 15;
  if (c == "travel" || c == "jump" || c == "scan" || c == "salvage" || c == "dock" || c == "undock")
    return 15;
  if (c == "ship" || c == "jettison" || c == "preserve" || c == "sell-signal" || c == "abandon")
    return 20;
  if (c == "project" || c == "repair" || c == "maintain" || c == "install" || c == "filters" ||
      c == "fuel" || c == "transfer" || c == "medical" || c == "social" || c == "resolve" ||
      c == "issue-orders" || c == "cook" || c == "package" || c == "upgrade" || c == "recharge" ||
      c == "refuel" || c == "splitfuel" || c == "service-shield")
    return 10;
  return 5;
}
}  // namespace
const Json& Voyage::catalog() {
  static const Json data = [] {
    std::string source;
    for (const char* part : nativeDataParts) source += part;
    return Json::parse(source);
  }();
  return data;
}
Voyage::Voyage() : save_(catalog()["initial"]) {
  save_["format"] = "spaceship-native-1";
  save_["state"]["nativeTasks"] = Json::array();
  save_["state"]["nativeIncidents"] = Json::array();
  save_["state"]["nativeVisited"] = Json::object();
  save_["state"]["sim"]["memories"] = Json::array();
  save_["state"]["sim"]["serial"] = 0;
  remember("Watch", "Native C++ voyage begins at Kessler Drift.");
}
int Voyage::actions() const { return number(save_, "actions"); }
int Voyage::stat(const std::string& id) const {
  const auto* s = find(save_["stats"], id);
  return s ? number(*s, "value") : 0;
}
void Voyage::change(const std::string& id, int amount) {
  for (auto& s : save_["stats"])
    if (text(s, "id") == id) s["value"] = std::clamp(number(s, "value") + amount, 0, 100);
}
int Voyage::cargoUsed() const {
  int n = 0;
  for (const auto& g : save_["goods"]) n += number(g, "vol");
  return n;
}
int Voyage::cooldown(const std::string& key) const {
  const auto& c = save_["state"]["actionCooldowns"];
  return c.contains(key) ? number(c[key], "remaining") : 0;
}
int Voyage::controlCooldown(const std::string& c, const std::string& a) const {
  return cooldown(keyFor(c, a));
}
std::string Voyage::cooldownReport() const {
  std::ostringstream out;
  out << "CONTROL COOLDOWNS (other action clicks)\n";
  const auto& cds = save_["state"]["actionCooldowns"];
  if (cds.empty()) out << "All controls ready.\n";
  for (auto it = cds.begin(); it != cds.end(); ++it) {
    const auto split = it.key().find(':');
    auto name = it.value().value(
        "label", controlName(it.key().substr(0, split),
                             split == std::string::npos ? "" : it.key().substr(split + 1)));
    out << name << ": " << number(it.value(), "remaining") << " / " << number(it.value(), "tier")
        << " left\n";
  }
  return out.str();
}

std::string Voyage::location() const {
  auto* b = find(catalog()["bodies"], text(save_["map"], "here"));
  return b ? text(*b, "name") : "Unknown";
}
bool Voyage::busy(const std::string& id) const {
  if (std::find(claims_.begin(), claims_.end(), id) != claims_.end()) return true;
  for (const auto& t : save_["state"]["nativeTasks"]) {
    if (text(t, "id") == id || text(t, "target") == id || t.value("by", std::string{}) == id)
      return true;
    if (text(t, "id") == "social" && t["payload"].is_array())
      for (const auto& member : t["payload"])
        if (member == id) return true;
  }
  return false;
}
bool Voyage::reject(const std::string& why) {
  message_ = why;
  return false;
}
void Voyage::task(const std::string& id, const std::string& target, int work, const Json& payload) {
  save_["state"]["nativeTasks"].push_back(
      {{"id", id}, {"target", target}, {"left", work}, {"total", work}, {"payload", payload}});
}
void Voyage::remember(const std::string& kind, const std::string& line, const std::string& who,
                      const std::string& other) {
  auto& sim = save_["state"]["sim"];
  add(sim, "serial", 1);
  sim["memories"].push_back({{"id", sim["serial"]},
                             {"kind", kind},
                             {"text", line},
                             {"at", actions()},
                             {"who", who},
                             {"other", other},
                             {"processed", false}});
  if (sim["memories"].size() > 32) sim["memories"].erase(sim["memories"].begin());
  auto& feed = save_["state"]["feed"];
  feed.push_back(std::to_string(actions()) + " | " + line);
  if (feed.size() > 80) feed.erase(feed.begin());
}
bool Voyage::act(const std::string& c, const std::string& a) {
  // Invalid and rejected commands cannot leave partial resource mutations behind.
  Json before = save_;
  claims_.clear();
  try {
    if (execute(c, a)) {
      claims_.clear();
      return true;
    }
  } catch (const std::exception& e) {
    message_ = std::string("Command rejected: ") + e.what();
  }
  save_ = std::move(before);
  claims_.clear();
  return false;
}
bool Voyage::execute(const std::string& c, const std::string& a) {
  auto& s = save_["state"];
  auto& sim = s["sim"];
  if (c == "toggle-music" || c == "toggle-sounds") {
    auto k = c == "toggle-music" ? "music" : "soundEffects";
    s["options"][k] = !s["options"].value(k, true);
    message_ = "Audio preference updated (free).";
    return true;
  }
  if (c == "audio-level") {
    if (a != "0" && a != "25" && a != "50" && a != "75" && a != "100")
      return reject("Choose 0, 25, 50, 75 or 100 percent.");
    s["options"]["audioLevel"] = std::stoi(a) / 100.0;
    message_ = "Audio level updated (free).";
    return true;
  }
  if (!text(s, "gameOver").empty())
    return reject(text(s, "gameOver") + " Load a save or start a new voyage.");
  if (c == "acknowledge") {
    auto& tasks = s["nativeTasks"];
    auto it = std::find_if(tasks.begin(), tasks.end(), [&](const Json& t) {
      return text(t, "id") == a && t.value("state", std::string("running")) == "done";
    });
    if (it == tasks.end()) return reject("No completed readout waiting.");
    Json done = *it;
    tasks.erase(it);
    finish(done);
    progress();
    message_ = "Readout acknowledged (free).";
    return true;
  }
  if (c == "select-memory") {
    int id = 0;
    try {
      id = std::stoi(a);
    } catch (...) {
      return reject("Choose a memory.");
    }
    bool available = false;
    for (const auto& m : sim["memories"])
      if (number(m, "id") == id && !m.value("processed", false)) available = true;
    if (!available) return reject("Memory is already processed or missing.");
    if (!s.contains("nativeDreamSelection")) s["nativeDreamSelection"] = Json::array();
    auto& selection = s["nativeDreamSelection"];
    auto it = std::find(selection.begin(), selection.end(), Json(id));
    if (it != selection.end())
      selection.erase(it);
    else {
      if (selection.size() >= 2) selection.erase(selection.begin());
      selection.push_back(id);
    }
    message_ = "Dream selection updated (free).";
    return true;
  }
  if (c.rfind("stage-", 0) == 0) {
    if (busy("crew-sheet") || busy(a)) return reject("Order sheet or crew member is busy.");
    const auto* person = find(save_["crew"], a);
    if (!person) return reject("Choose a crew member.");
    std::string order = c.substr(6);
    auto& staged = s["pendingOrders"];
    if (!staged.contains(a)) staged[a] = Json::object();
    if (order.rfind("job=", 0) == 0) {
      auto job = order.substr(4);
      if (!find(catalog()["jobs"], job)) return reject("Unknown job.");
      staged[a]["job"] = job;
    } else {
      if (!find(catalog()["orders"], order)) return reject("Unknown order.");
      if (order == "learnSt" && !s.value("docked", false))
        return reject("Station learning requires a dock.");
      if (staged[a].value(order, false))
        staged[a].erase(order);
      else {
        if (order == "rest" && number(*person, "fatigue") < 25 && number(*person, "morale") >= 70)
          return reject("Crew member does not need rest.");
        staged[a][order] = true;
      }
    }
    if (staged[a].empty()) staged.erase(a);
    message_ = "Order sheet updated (free).";
    return true;
  }
  if (c == "clear-orders") {
    if (busy("crew-sheet")) return reject("Sheet already issued.");
    s["pendingOrders"] = Json::object();
    message_ = "Order sheet cleared (free).";
    return true;
  }
  const auto key = keyFor(c, a);
  const int wait = cooldown(key);
  if (wait) return reject("Cooldown: " + std::to_string(wait) + " other committed actions.");
  std::string pending, target;
  int work = 0;
  Json payload;
  const bool docked = s.value("docked", false);
  int reservedCargo = 0;
  for (const auto& t : s["nativeTasks"])
    if (text(t, "id") == "salvage" || text(t, "id").rfind("salvage-", 0) == 0)
      reservedCargo += t["payload"].is_object() ? t["payload"].value("vol", 3) : 5;
  if (c == "issue-orders") {
    const auto quote = orderQuote();
    if (number(quote, "work") < 1 || busy("crew-sheet"))
      return reject("Stage orders first; only one sheet can run.");
    for (auto it = s["pendingOrders"].begin(); it != s["pendingOrders"].end(); ++it) {
      if (busy(it.key())) return reject("A selected crew member is unavailable.");
      if (it.value().value("learnSt", false) && !docked)
        return reject("Station training requires docking.");
      claims_.push_back(it.key());
    }
    if (number(s, "shiplets") < number(quote, "fee")) return reject("Insufficient training fees.");
    add(s, "shiplets", -number(quote, "fee"));
    pending = "crew-sheet";
    work = number(quote, "work");
    payload = s["pendingOrders"];
    s["pendingOrders"] = Json::object();
  } else if (c == "watch") { /* A space-operation click, subject to its own cooldown. */
  } else if (c == "travel" || c == "jump") {
    const auto* b = find(catalog()[c == "jump" ? "systems" : "bodies"], a);
    const auto* here = find(catalog()["bodies"], text(save_["map"], "here"));
    if (!b || !here || docked || busy("travel"))
      return reject("Undock and finish the current crossing first.");
    if ((c == "travel" && (a == text(*here, "id") || bodySystem(*b) != bodySystem(*here))) ||
        (c == "jump" && a == bodySystem(*here)))
      return reject("Choose another reachable destination.");
    const auto metrics = navigationMetrics(a);
    int cost = metrics.at("fuel");
    if (stat("fuel") < cost || stat("energy") < 1 || number(s, "engineCondition") <= 0)
      return reject("Insufficient fuel, power or working drive.");
    change("fuel", -cost);
    pending = "travel";
    target = c == "jump" ? text(*b, "entry") : a;
    work = metrics.at("dist");
  } else if (c == "scan") {
    const auto* b = find(catalog()["bodies"], a);
    const auto* here = find(catalog()["bodies"], text(save_["map"], "here"));
    if (!b || !here || bodySystem(*b) != bodySystem(*here) || busy("scan") || stat("energy") <= 0)
      return reject("Choose an unscanned local body; needs 3 energy and idle sensors.");
    pending = "scan";
    target = a;
    work = std::max(1, number(save_, "scanCost") - (number(sim, "sensor") > 0 ? 2 : 0));
    sim["sensor"] = 0;
  } else if (c == "dock" || c == "undock") {
    if (c == "undock") {
      if (!docked) return reject("Not docked.");
      s["docked"] = false;
    } else {
      if (docked || busy("travel") || !catalog()["ports"].contains(text(save_["map"], "here")))
        return reject("Reach a station first.");
      s["docked"] = true;
    }
  } else if (c == "recharge" || c == "refuel" || c == "splitfuel" || c == "service-shield") {
    const bool shields = c == "service-shield";
    const auto resource = shields ? "repairKits" : "fuelCells";
    if (number(s, resource) < 1 || (c == "recharge" && stat("energy") == 100) ||
        (c == "refuel" && stat("fuel") == 100) || (shields && stat("shield") == 100) ||
        (c == "splitfuel" && stat("fuel") == 100 && stat("energy") == 100))
      return reject("Needs one supply item and room in the selected reserve.");
    add(s, resource, -1);
    if (shields) change("shield", 25);
    if (c == "recharge") change("energy", 20);
    if (c == "refuel") change("fuel", 15);
    if (c == "splitfuel") {
      change("energy", 10);
      change("fuel", 7);
    }
  } else if (c == "fuel") {
    if (number(s, "fuelCells") < 1 || (stat("fuel") == 100 && stat("energy") == 100))
      return reject("Needs a fuel cell and room in the reserves.");
    add(s, "fuelCells", -1);
    change("fuel", 20);
    change("energy", 20);
  } else if (c == "repair" || c == "maintain") {
    if ((c == "repair" && number(s, "repairKits") < 1) || busy(c) ||
        (c == "maintain" && busy("project-cooling")) ||
        (c == "repair" ? stat("hull") : number(s, "engineCondition")) >= 100)
      return reject("Needs damage, a repair kit and no matching work in progress.");
    if (c == "repair") add(s, "repairKits", -1);
    if (c == "repair")
      change("hull", 25);
    else
      pending = c;
    work = c == "maintain"
               ? static_cast<int>(std::ceil((100 - number(s, "engineCondition")) / 10.0))
               : 4;
  } else if (c == "install") {
    int i = -1;
    for (int n = 0; n < 3; ++n)
      if (a == "s" + std::to_string(n)) i = n;
    if (i < 0 || text(save_["life"]["slots"][i], "state") != "empty" ||
        number(save_["life"], "canisters") < 1)
      return reject("Needs an empty slot and an O2 canister.");
    add(save_["life"], "canisters", -1);
    save_["life"]["slots"][i]["state"] = "installing";
    pending = "install-" + a;
    target = a;
    work = 2;
  } else if (c == "filters") {
    for (const auto& slot : save_["life"]["slots"])
      if (text(slot, "state") != "seated") return reject("Seat all three canisters first.");
    if (stat("energy") < 1) return reject("Needs power.");
    // Switch-on happens after the initiating world tick, like new work.
    pending = "start-filters";
  } else if (c == "cook") {
    if (busy("cook") || busy("project-food") ||
        number(save_["mess"], "rack") >= number(save_["mess"], "capacity"))
      return reject("Galley is occupied or ration rack is full.");
    work = crewWork(number(save_["mess"], "capacity") - number(save_["mess"], "rack"));
    pending = "cook";
    payload = {{"rate", 1}};
  } else if (c == "package") {
    const auto* record = find(save_["survey"], a);
    if (!record || text(*record, "state") == "packaged" || busy("package-" + a))
      return reject("Choose an unpackaged survey record.");
    pending = "package-" + a;
    target = a;
    work = 2;
  } else if (c == "salvage") {
    std::string bay = a.empty() ? "0" : a;
    if (bay != "0" && bay != "1" && bay != "2") return reject("Choose bay 0, 1 or 2.");
    const auto* crossing = find(s["nativeTasks"], "travel");
    if (!crossing || crossing->value("state", std::string("running")) != "running" ||
        busy("salvage-" + bay) ||
        cargoUsed() + reservedCargo + 3 > number(save_["cargo"], "capacity") || stat("energy") <= 4)
      return reject(
          "Recover during a running crossing; needs 3 free cargo space and more than 4 energy.");
    change("energy", -4);
    pending = "salvage-" + bay;
    work = 2;
    payload = {{"vol", 3}};
  } else if (c == "buy" || c == "sell") {
    if (!docked) return reject("Trade at a station.");
    const auto& port = catalog()["ports"][text(save_["map"], "here")];
    if (c == "buy" &&
        (a == "fuel" || a == "kit" || a == "med" || a == "oxygen" || a == "rations")) {
      const int price = a == "fuel"     ? 45
                        : a == "kit"    ? 70
                        : a == "med"    ? 50
                        : a == "oxygen" ? 25
                                        : 20;
      if (number(s, "shiplets") < price) return reject("Insufficient shiplets.");
      if (a == "oxygen" && number(save_["life"], "canisters") + (busy("project-oxygen") ? 1 : 0) >=
                               number(save_["life"], "capacity"))
        return reject("Canister rack full.");
      const int reservedRations = (busy("cook") ? 6 : 0) + (busy("project-food") ? 8 : 0);
      if (a == "rations" &&
          number(save_["mess"], "rack") + reservedRations > number(save_["mess"], "capacity") - 6)
        return reject("Needs room for six rations.");
      add(s, "shiplets", -price);
      if (a == "fuel")
        add(s, "fuelCells", 1);
      else if (a == "kit")
        add(s, "repairKits", 1);
      else if (a == "med")
        add(s, "medkits", 1);
      else if (a == "oxygen")
        add(save_["life"], "canisters", 1);
      else
        add(save_["mess"], "rack", 6);
    } else if (c == "buy") {
      const auto* good = find(catalog()["market"], a);
      if (!good) return reject("Unknown cargo.");
      int price = static_cast<int>(std::ceil(
          std::floor(number(*good, "value") * port["prices"].value(text(*good, "kind"), 1.0) +
                     0.5) *
          1.2));
      if (number(s, "shiplets") < price ||
          cargoUsed() + reservedCargo + number(*good, "vol") > number(save_["cargo"], "capacity"))
        return reject("Insufficient money or cargo space.");
      add(s, "shiplets", -price);
      Json lot = *good;
      add(s, "lotSerial", 1);
      lot["id"] = "lot-" + std::to_string(number(s, "lotSerial"));
      save_["goods"].push_back(lot);
    } else {
      auto& goods = save_["goods"];
      auto it = std::find_if(goods.begin(), goods.end(),
                             [&](const Json& g) { return text(g, "id") == a; });
      if (it == goods.end() || it->contains("contractId"))
        return reject("Choose uncommitted cargo.");
      add(s, "shiplets",
          static_cast<int>(std::floor(
              number(*it, "value") * port["prices"].value(text(*it, "kind"), 1.0) + 0.5)));
      goods.erase(it);
    }
  } else if (c == "sell-data") {
    if (!docked || save_["survey"].empty()) return reject("Bring survey records to a station.");
    int value = 0;
    for (const auto& record : save_["survey"]) {
      if (busy("package-" + text(record, "id")))
        return reject("Wait until survey packaging is finished.");
      value += number(record, "value");
    }
    add(s, "shiplets", value);
    save_["survey"] = Json::array();
  } else if (c == "jettison") {
    auto& goods = save_["goods"];
    auto it =
        std::find_if(goods.begin(), goods.end(), [&](const Json& g) { return text(g, "id") == a; });
    if (it == goods.end()) return reject("Choose uncommitted cargo.");
    if (it->contains("contractId")) s["contracts"][text(*it, "contractId")] = "failed";
    goods.erase(it);
  } else if (c == "project") {
    if (!catalog()["projects"].contains(a)) return reject("Unknown project.");
    const auto& p = catalog()["projects"][a];
    if (busy("project-" + a) || stat("energy") <= number(p, "energy"))
      return reject("Project is running or needs more power.");
    if (a == "oxygen" && number(save_["life"], "canisters") + (busy("project-oxygen") ? 1 : 0) >=
                             number(save_["life"], "capacity"))
      return reject("Canister rack full.");
    if (a == "food" &&
        (busy("cook") || number(save_["mess"], "rack") > number(save_["mess"], "capacity") - 8))
      return reject("Needs room for eight rations and an idle galley.");
    if (a == "cooling" && number(s, "engineCondition") >= 100)
      return reject("Drive is already sound.");
    if (a == "brace" && stat("hull") == 100) return reject("Hull is already sound.");
    if (a == "observe" &&
        (busy("travel") || sim["observed"].value(text(save_["map"], "here"), false)))
      return reject("Stop at a location not already recorded.");
    if (a == "sensors" && (number(sim, "insight") < 1 || number(sim, "sensor") > 0 || busy("scan")))
      return reject("Needs one insight and idle uncalibrated sensors.");
    if (a == "decode" && (text(sim, "signal") != "intercepted" || number(sim, "insight") < 1))
      return reject("Needs the intercepted signal and one insight.");
    if (a == "dream") {
      Json memories = Json::array();
      const auto selected = s.value("nativeDreamSelection", Json::array());
      if (!selected.empty() && selected.size() != 2)
        return reject("Select two different memories, or clear the selection.");
      for (auto& m : sim["memories"])
        if (!m.value("processed", false) && memories.size() < 2 &&
            (selected.empty() ||
             std::find(selected.begin(), selected.end(), m["id"]) != selected.end())) {
          m["processed"] = true;
          memories.push_back(m);
        }
      if (memories.size() != 2 || number(sim, "insight") >= 5)
        return reject("Needs two unprocessed memories and room for an insight.");
      payload = memories;
      s["nativeDreamSelection"] = Json::array();
    }
    if (a == "sensors" || a == "decode") add(sim, "insight", -1);
    if (a == "decode") sim["signal"] = "decoding";
    if (a == "observe") {
      target = text(save_["map"], "here");
      sim["observed"][target] = true;
    }
    if (p.contains("lot")) {
      auto& goods = save_["goods"];
      auto it = std::find_if(goods.begin(), goods.end(), [&](const Json& g) {
        return text(g, "kind") == text(p, "lot") && !g.value("risk", false) &&
               !g.contains("contractId");
      });
      if (it == goods.end()) return reject("Needs an uncommitted " + text(p, "lot") + " lot.");
      goods.erase(it);
    }
    change("energy", -number(p, "energy"));
    pending = "project-" + a;
    work = number(p, "work");
  } else if (c == "transfer") {
    const bool shield = a == "shield";
    if (stat(shield ? "energy" : "shield") <= 20 || stat(shield ? "shield" : "energy") >= 100)
      return reject("Needs more than 20 reserve points and room at the destination.");
    change(shield ? "energy" : "shield", -20);
    change(shield ? "shield" : "energy", 15);
  } else if (c == "rest" || c == "medical" || c == "talk" || c == "train" || c == "job") {
    Json* crew = nullptr;
    for (auto& person : save_["crew"])
      if (text(person, "id") == a) crew = &person;
    if (!crew || busy(a) || number(*crew, "health") == 0)
      return reject("Choose an available living crew member.");
    claims_.push_back(a);
    if (c == "medical") {
      if (number(s, "medkits") < 1 || number(*crew, "health") == 100 || stat("energy") <= 2)
        return reject("Needs injury, a medkit and 3 power.");
      add(s, "medkits", -1);
      change("energy", -2);
      pending = "medical-" + a;
      target = a;
      work = 3;
    } else if (c == "rest") {
      pending = "rest-" + a;
      target = a;
      work = 4;
    } else if (c == "talk") {
      (*crew)["morale"] = std::min(100, number(*crew, "morale") + 3);
      remember("Care", "The captain listened to " + text(*crew, "name") + ".");
    } else if (c == "train") {
      if (!docked || number(s, "shiplets") < 50 || text(*crew, "job").empty())
        return reject("Training requires a job, a station and 50 shiplets.");
      add(s, "shiplets", -50);
      (*crew)["skill"] = "Trained";
      (*crew)["jobAt"] = text(*crew, "job") == "maintenance"
                             ? std::min(75, number(*crew, "jobAt") + 15)
                             : std::min(5, number(*crew, "jobAt") + 1);
    } else {
      return reject("Use a specific assignment command.");
    }
  } else if (c.rfind("assign-", 0) == 0) {
    std::string job = c.substr(7);
    const auto* spec = find(catalog()["jobs"], job);
    if (!spec) return reject("Unknown assignment.");
    Json* crew = nullptr;
    for (auto& p : save_["crew"])
      if (text(p, "id") == a) crew = &p;
    if (!crew || busy(a)) return reject("Crew member is busy.");
    if (spec->value("unique", false))
      for (auto& p : save_["crew"])
        if (text(p, "job") == job) p["job"] = "";
    (*crew)["job"] = job;
    (*crew)["skill"] = "Untrained";
    (*crew)["jobAt"] = job == "maintenance" ? 30 : 1;
  } else if (c == "social") {
    if (save_["crew"].size() < 2 || busy("social") || number(save_["mess"], "rack") < 2)
      return reject("Needs two crew, two rations and an idle recreation room.");
    Json pair = Json::array();
    for (const auto& p : save_["crew"])
      if (!busy(text(p, "id")) && number(p, "health") > 0 && pair.size() < 2)
        pair.push_back(text(p, "id"));
    if (pair.size() < 2) return reject("Two available crew needed.");
    add(save_["mess"], "rack", -2);
    pending = "social";
    payload = pair;
    for (const auto& member : pair) claims_.push_back(member.get<std::string>());
    work = 3;
  } else if (c == "hire") {
    const auto* p = find(catalog()["candidates"], a);
    if (!docked || !p || find(save_["crew"], a) ||
        (p->contains("port") && text(*p, "port") != text(save_["map"], "here")) ||
        save_["crew"].size() >= static_cast<size_t>(std::stoi(text(save_["ship"], "berths"))) ||
        number(s, "shiplets") < number(*p, "fee"))
      return reject("Candidate unavailable, no berth, or insufficient money.");
    add(s, "shiplets", -number(*p, "fee"));
    save_["crew"].push_back(*p);
  } else if (c == "ship" || c == "upgrade") {
    const int cost = c == "ship"                   ? 4000
                     : a == "drive" || a == "bay2" ? 900
                     : a == "rack"                 ? 400
                     : a == "scanner"              ? 600
                     : a == "hull"                 ? 120
                                                   : 500;
    if (!docked || number(s, "shiplets") < cost)
      return reject("Requires a station and " + std::to_string(cost) + " shiplets.");
    if (c == "ship") {
      if (text(s, "shipClass") == "kestrel") return reject("Already aboard the larger ship.");
      s["shipClass"] = "kestrel";
      save_["ship"] = catalog()["largeShip"];
      save_["rooms"] = catalog()["largeRooms"];
    } else {
      if ((a != "bay" && a != "bay2" && a != "drive" && a != "rack" && a != "scanner" &&
           a != "hull") ||
          s["upgrades"].value(a, false) || (a == "bay2" && !s["upgrades"].value("bay", false)))
        return reject("Refit unavailable or already fitted.");
      if (a == "hull") {
        if (stat("hull") == 100) return reject("Hull already sound.");
        change("hull", 50);
      } else
        s["upgrades"][a] = true;
      if (a == "rack") save_["life"]["capacity"] = 15;
      if (a == "scanner") save_["scanCost"] = 3;
    }
    add(s, "shiplets", -cost);
    int level = s["upgrades"].value("bay2", false) ? 2 : s["upgrades"].value("bay", false) ? 1 : 0;
    save_["cargo"]["capacity"] = text(s, "shipClass") == "wren" ? (level == 0   ? 40
                                                                   : level == 1 ? 60
                                                                                : 90)
                                                                : (level == 0   ? 60
                                                                   : level == 1 ? 90
                                                                                : 120);
  } else if (c == "contract") {
    const auto* contract = find(catalog()["contracts"], a);
    if (!docked || !contract) return reject("Use a station mission board.");
    auto status = s["contracts"].value(a, std::string{});
    if (status == "ready") {
      add(s, "shiplets", number(*contract, "reward"));
      s["contracts"][a] = "complete";
    } else if (status.empty() || status == "failed") {
      if (contract->contains("origin") && text(*contract, "origin") != text(save_["map"], "here"))
        return reject("Accept at the contract's origin port.");
      if (text(*contract, "goal") == "freight") {
        if (cargoUsed() + reservedCargo + number(*contract, "vol") >
            number(save_["cargo"], "capacity"))
          return reject("Not enough cargo space.");
        save_["goods"].push_back({{"id", "contract-" + a},
                                  {"name", text(*contract, "title")},
                                  {"vol", number(*contract, "vol")},
                                  {"value", 0},
                                  {"kind", "Contract freight"},
                                  {"contractId", a}});
      }
      if (text(*contract, "goal") == "passenger") {
        if (save_["crew"].size() + save_["state"]["passengers"].size() >=
            static_cast<size_t>(std::stoi(text(save_["ship"], "berths"))))
          return reject("No free passenger berth.");
        s["passengers"].push_back({{"id", a}, {"name", "Station passenger"}});
      }
      s["contracts"][a] = "active";
      if (contract->value("recognizePrior", false)) {
        const auto* here = find(catalog()["bodies"], text(save_["map"], "here"));
        if ((text(*contract, "goal") == "scan" &&
             s["scanned"].value(text(*contract, "target"), false)) ||
            (text(*contract, "goal") == "visit" &&
             (text(*contract, "target") == text(save_["map"], "here") ||
              (here && text(*contract, "target") == bodySystem(*here)))))
          s["contracts"][a] = "ready";
      }
    } else
      return reject("Contract is already active or collected.");
  } else if (c == "commission") {
    if (!docked || !s["commissions"]["active"].is_null())
      return reject("Only one commission can be active; accept at a station.");
    if (a != "visit" && a != "scan" && a != "salvage") return reject("Unknown commission.");
    const auto offer = commissionOffer(a);
    if (offer.is_null()) return reject("No eligible destination.");
    s["commissions"]["active"] = offer;
  } else if (c == "collect" || c == "abandon") {
    auto& active = s["commissions"]["active"];
    if (active.is_null()) return reject("No active commission.");
    if (!docked) return reject("Collect or abandon at a station.");
    if (c == "collect") {
      if (!docked || number(active, "done") < number(active, "goal"))
        return reject("Finish the commission and collect at a station.");
      add(s, "shiplets", number(active, "reward"));
      add(s["commissions"], "completed", 1);
    }
    add(s["commissions"], "serial", 1);
    active = nullptr;
  } else if (c == "resolve") {
    auto& incidents = s["nativeIncidents"];
    auto it = std::find_if(incidents.begin(), incidents.end(),
                           [&](const Json& i) { return text(i, "id") == a; });
    if (it == incidents.end() || busy("incident-" + a))
      return reject("Choose an unresolved report.");
    const auto* spec = find(catalog()["incidents"], a);
    const Json* worker = nullptr;
    int best = -1;
    for (const auto& person : save_["crew"]) {
      const auto id = text(person, "id");
      if (busy(id) || s["pendingOrders"].contains(id)) continue;
      int score = number(person, "morale");
      const auto kind = text(*spec, "kind");
      if ((kind == "Mechanical" || kind == "Electrical") && text(person, "job") == "maintenance")
        score += 100;
      if (kind == "Environmental" && text(person, "job") == "filters") score += 100;
      if (kind == "Crew" && id != it->value("a", std::string{}) &&
          id != it->value("b", std::string{}))
        score += 100;
      if (score > best) {
        best = score;
        worker = &person;
      }
    }
    if (!worker) return reject("No available crew member can resolve this report.");
    claims_.push_back(text(*worker, "id"));
    pending = "incident-" + a;
    target = a;
    work = 6;
    payload = {{"by", text(*worker, "id")}};
  } else if (c == "broadcast") {
    const auto* body = find(catalog()["bodies"], text(save_["map"], "here"));
    const auto* system = body ? find(catalog()["systems"], bodySystem(*body)) : nullptr;
    if (!system || sim["broadcasts"].value(text(*system, "id"), false) || stat("energy") <= 2)
      return reject("Local system report already collected, or needs more than two energy.");
    change("energy", -2);
    sim["broadcasts"][text(*system, "id")] = true;
    remember("World", text(*system, "name") + ": " +
                          catalog()["history"].at(text(*system, "region"))[1].get<std::string>());
  } else if (c == "preserve" || c == "sell-signal") {
    if (text(sim, "signal") != "request") return reject("Follow the Silicon Marrow signal first.");
    if (c == "preserve") {
      if (text(save_["map"], "here") != "palimpsest-site" || number(s, "repairKits") < 1 ||
          stat("energy") <= 5)
        return reject("Reach the Archive Hull with a repair kit and more than five energy.");
      add(s, "repairKits", -1);
      change("energy", -5);
      sim["signal"] = "preserved";
      for (auto& p : save_["crew"]) p["morale"] = std::min(100, number(p, "morale") + 5);
    } else {
      if (!docked) return reject("Sell at a station.");
      add(s, "shiplets", 900);
      sim["signal"] = "sold";
      for (auto& p : save_["crew"]) p["morale"] = std::max(0, number(p, "morale") - 8);
    }
    remember("Choice", c == "preserve" ? "The crew sheltered Silicon Marrow's archive."
                                       : "The signal was sold despite its request.");
  } else if (c == "claim") {
    const auto* award = find(catalog()["awards"], a);
    if (!award || !s["chronicle"]["earned"].value(a, false) ||
        s["chronicle"]["claimed"].value(a, false))
      return reject("Reward not earned or already claimed.");
    const auto& r = (*award)["reward"];
    add(s, "shiplets", r.value("cash", 0));
    add(s, "fuelCells", r.value("fuel", 0));
    add(s, "repairKits", r.value("kits", 0));
    add(s, "medkits", r.value("med", 0));
    s["chronicle"]["claimed"][a] = true;
    message_ = "Reward claimed.";
    return true;
  } else
    return reject("Unknown native command.");
  add(save_, "actions", 1);
  auto& cds = s["actionCooldowns"];
  for (auto it = cds.begin(); it != cds.end();) {
    add(it.value(), "remaining", -1);
    if (number(it.value(), "remaining") <= 0)
      it = cds.erase(it);
    else
      ++it;
  }
  tick(pending);
  if (text(s, "gameOver").empty() && pending == "start-filters") {
    startBank();
  } else if (text(s, "gameOver").empty() && !pending.empty())
    task(pending, target, work, payload);
  if (pending.rfind("incident-", 0) == 0 && !s["nativeTasks"].empty() &&
      text(s, "gameOver").empty())
    s["nativeTasks"].back()["by"] = payload.at("by");
  if (pending == "crew-sheet" && text(s, "gameOver").empty()) {
    for (auto it = payload.begin(); it != payload.end(); ++it) {
      const auto* person = find(save_["crew"], it.key());
      if (person) task("orders-" + it.key(), it.key(), sheetCost(*person, it.value()), it.value());
    }
  }
  if (tier(c)) cds[key] = {{"remaining", tier(c)}, {"tier", tier(c)}, {"label", controlName(c, a)}};

  progress();
  message_ = "Committed: " + c + (a.empty() ? "" : " / " + a);
  return true;
}

void Voyage::commissionProgress(const std::string& kind, const std::string& target) {
  auto& c = save_["state"]["commissions"]["active"];
  if (!c.is_null() && text(c, "kind") == kind && (kind == "salvage" || text(c, "target") == target))
    c["done"] = std::min(number(c, "goal"), number(c, "done") + 1);
}
void Voyage::arrive(const std::string& target) {
  auto& s = save_["state"];
  save_["map"]["here"] = target;
  s["nativeVisited"][target] = true;
  s["docked"] = catalog()["ports"].contains(target);
  const auto* body = find(catalog()["bodies"], target);
  if (body) {
    const auto sys = bodySystem(*body);
    s["chronicle"]["systems"][sys] = true;
    const auto* system = find(catalog()["systems"], sys);
    if (system) s["chronicle"]["regions"][text(*system, "region")] = true;
  }
  contractProgress("visit", target);
  commissionProgress("visit", target);
  remember("Arrival", "Arrived at " + location() + ".");
}
void Voyage::finish(const Json& t) {
  auto& s = save_["state"];
  auto& sim = s["sim"];
  const auto id = text(t, "id"), target = text(t, "target");
  if (id == "travel")
    arrive(target);
  else if (id == "scan") {
    const bool first = !s["scanned"].value(target, false);
    s["scanned"][target] = true;
    const auto* b = find(catalog()["bodies"], target);
    if (!find(save_["survey"], "sv-" + target))
      save_["survey"].push_back(
          {{"id", "sv-" + target},
           {"name", b ? text(*b, "name") : target},
           {"src", "Deep scan"},
           {"value", b ? catalog()["surveyWorth"].value(text(*b, "type"), 60) : 60},
           {"state", "filed"}});
    contractProgress("scan", target);
    commissionProgress("scan", target);
    if (first) remember("Discovery", "Scanned " + (b ? text(*b, "name") : target) + ".");
    if (target == "halvan" && text(sim, "signal") == "dormant") sim["signal"] = "intercepted";
    if (target == "echo-site" && text(sim, "signal") == "coordinates") sim["signal"] = "request";
  } else if (id == "repair")
    change("hull", 25);
  else if (id == "maintain") {
    s["engineCondition"] = 100;
    s["wearTick"] = 0;
  } else if (id == "cook" ||
             id == "crew-sheet") { /* Effects delivered while cooking / by individual orders. */
  } else if (id.rfind("orders-", 0) == 0)
    applyOrders(target, t["payload"]);
  else if (id.rfind("doctor-", 0) == 0) {
    for (auto& person : save_["crew"])
      if (text(person, "id") == target) {
        person["health"] = std::min(100, number(person, "health") + 12);
        person["activity"] = "Back on station";
        remember("Care", text(person, "name") + " recovered in the medical bay.", target);
      }
  } else if (id.rfind("install-", 0) == 0) {
    for (auto& slot : save_["life"]["slots"])
      if (text(slot, "id") == target) slot["state"] = "seated";
  } else if (id.rfind("package-", 0) == 0) {
    for (auto& record : save_["survey"])
      if (text(record, "id") == target && text(record, "state") != "packaged") {
        record["state"] = "packaged";
        record["value"] = static_cast<int>(std::ceil(number(record, "value") * 1.15));
      }
  } else if (id == "salvage" || id.rfind("salvage-", 0) == 0) {
    int volume = t["payload"].is_object() ? t["payload"].value("vol", 3) : 5;
    if (cargoUsed() + volume <= number(save_["cargo"], "capacity")) {
      add(s, "lotSerial", 1);
      save_["goods"].push_back({{"id", "salvage-" + std::to_string(number(s, "lotSerial"))},
                                {"name", "Recovered salvage"},
                                {"vol", volume},
                                {"value", 30},
                                {"kind", "Salvage"},
                                {"risk", false}});
      commissionProgress("salvage", "");
    }
  } else if (id.rfind("project-", 0) == 0) {
    const auto p = id.substr(8);
    add(s["chronicle"], "projects", 1);
    if (p == "kit") add(s, "repairKits", 1);
    if (p == "oxygen")
      save_["life"]["canisters"] =
          std::min(number(save_["life"], "capacity"), number(save_["life"], "canisters") + 1);
    if (p == "fuel") add(s, "fuelCells", 1);
    if (p == "food")
      save_["mess"]["rack"] =
          std::min(number(save_["mess"], "capacity"), number(save_["mess"], "rack") + 8);
    if (p == "cooling") s["engineCondition"] = std::min(100, number(s, "engineCondition") + 15);
    if (p == "brace") change("hull", 15);
    if (p == "sensors") sim["sensor"] = 1;
    if (p == "decode")
      sim["signal"] = s["scanned"].value("echo-site", false) ? "request" : "coordinates";
    if (p == "observe")
      save_["survey"].push_back({{"id", "observation-" + target},
                                 {"name", "Observation: " + target},
                                 {"value", 65},
                                 {"state", "packaged"}});
    if (p == "dream") {
      const auto& first = t["payload"][0];
      const auto& second = t["payload"][1];
      auto exploration = [](const Json& m) {
        auto k = text(m, "kind");
        return k == "Discovery" || k == "World" || k == "Arrival";
      };
      sim["insight"] =
          std::min(5, number(sim, "insight") + (exploration(first) && exploration(second) ? 2 : 1));
      auto has = [](const Json& m, const std::string& id) {
        return text(m, "who") == id || text(m, "other") == id;
      };
      for (auto& person : save_["crew"])
        if (has(first, text(person, "id")) && has(second, text(person, "id")))
          person["morale"] = std::min(100, number(person, "morale") + 2);
      auto pair = [](const Json& m) {
        auto a = text(m, "who"), b = text(m, "other");
        return a < b ? a + "~" + b : b + "~" + a;
      };
      const auto k1 = text(first, "kind"), k2 = text(second, "kind");
      if (((k1 == "Conflict" && k2 == "Connection") || (k2 == "Conflict" && k1 == "Connection")) &&
          !text(first, "who").empty() && !text(first, "other").empty() &&
          !text(second, "who").empty() && !text(second, "other").empty() &&
          pair(first) == pair(second))
        sim["bonds"][pair(first)] = std::min(100, sim["bonds"].value(pair(first), 0) + 4);
      add(s["chronicle"], "dreams", 1);
      sim["dreams"].push_back({{"at", actions()},
                               {"text", "A dream joins " + k1 + " with " + k2 + ". " +
                                            text(first, "text").substr(0, 48) + " / " +
                                            text(second, "text").substr(0, 48)}});
      while (sim["dreams"].size() > 6) sim["dreams"].erase(sim["dreams"].begin());
    }
    remember("Work", "Completed " + p + ".");
  } else if (id.rfind("rest-", 0) == 0 || id.rfind("medical-", 0) == 0) {
    for (auto& p : save_["crew"])
      if (text(p, "id") == target) {
        if (id.rfind("rest-", 0) == 0)
          p["fatigue"] = std::max(0, number(p, "fatigue") - 35);
        else
          p["health"] = std::min(100, number(p, "health") + 25);
      }
    add(s["chronicle"], "projects", 1);
    remember("Care", "Completed care for " + target + ".");
  } else if (id == "social") {
    auto first = t["payload"][0].get<std::string>(), second = t["payload"][1].get<std::string>();
    const auto* a = find(save_["crew"], first);
    const auto* b = find(save_["crew"], second);
    int bonus = a && b && anchor(*a) == anchor(*b) ? 2 : 0;
    for (auto& person : save_["crew"])
      if (text(person, "id") == first || text(person, "id") == second)
        person["morale"] = std::min(100, number(person, "morale") + 4);
    if (first > second) std::swap(first, second);
    auto key = first + "~" + second;
    sim["bonds"][key] = std::min(100, sim["bonds"].value(key, 0) + 12 + bonus);
    add(s["chronicle"], "meals", 1);
    add(s["chronicle"], "projects", 1);
    remember("Connection", "The crew shared a meal.", first, second);
  } else if (id.rfind("incident-", 0) == 0) {
    auto& incidents = s["nativeIncidents"];
    const auto* ev = find(incidents, target);
    if (ev) {
      if (target == "crew" && ev->contains("a") && ev->contains("b")) {
        auto a = text(*ev, "a"), b = text(*ev, "b");
        if (a > b) std::swap(a, b);
        auto key = a + "~" + b;
        sim["bonds"][key] = std::min(100, sim["bonds"].value(key, 0) + 12);
        remember("Connection", "The crew settled their dispute.", a, b);
      }
      incidents.erase(std::remove_if(incidents.begin(), incidents.end(),
                                     [&](const Json& i) { return text(i, "id") == target; }),
                      incidents.end());
      add(s["chronicle"], "incidents", 1);
      remember("Care", "Incident resolved: " + target);
    }
  }
}
void Voyage::advanceWork(int amount) {
  if (!text(save_["state"], "gameOver").empty()) return;
  auto& tasks = save_["state"]["nativeTasks"];
  Json completed = Json::array();
  // Snapshot processing: newly created follow-up medical jobs never consume this tick.
  for (auto it = tasks.begin(); it != tasks.end();) {
    const auto id = text(*it, "id");
    if (it->value("state", std::string("running")) == "done") {
      ++it;
      continue;
    }
    const bool powered = id == "travel" || id == "scan" || id == "salvage" ||
                         id.rfind("salvage-", 0) == 0 || id.rfind("project-", 0) == 0 ||
                         id.rfind("medical-", 0) == 0;
    if ((powered && stat("energy") <= 0) ||
        (id == "travel" && number(save_["state"], "engineCondition") <= 0)) {
      ++it;
      continue;
    }
    const int used = std::min(amount, number(*it, "left"));
    if (id == "cook") {
      const int rate = it->at("payload").is_object() ? it->at("payload").value("rate", 1) : 1;
      save_["mess"]["rack"] =
          std::min(number(save_["mess"], "capacity"), number(save_["mess"], "rack") + used * rate);
    }
    add(*it, "left", -used);
    if (number(*it, "left") == 0) {
      if (id == "travel" || id == "scan") {
        (*it)["state"] = "done";
        ++it;
      } else {
        completed.push_back(*it);
        it = tasks.erase(it);
      }
    } else
      ++it;
  }
  for (const auto& t : completed) finish(t);
}
void Voyage::tick(const std::string& initiating) {
  auto& s = save_["state"];
  add(s, "wearTick", 1);
  if (number(s, "wearTick") >= 4) {
    s["wearTick"] = 0;
    s["engineCondition"] = std::max(0, number(s, "engineCondition") - 1);
  }
  if (actions() % 20 == 0) change("energy", -1);
  if (number(s, "filterLife") > 0 && stat("energy") > 0) {
    s["filterLife"] = std::max(0, number(s, "filterLife") - number(save_["life"], "decay"));
    if (number(s, "filterLife") == 0) {
      for (auto& slot : save_["life"]["slots"]) slot["state"] = "empty";
      s["airHold"] = 20;
    }
  } else
    s["airHold"] = std::max(0, number(s, "airHold") - 1);
  add(s, "crewTick", 1);
  bool living = false;
  for (auto& p : save_["crew"]) {
    const int harm = number(catalog()["air"][airStage()], "harm");
    if (harm > 0 && number(s, "crewTick") % harm == 0)
      p["health"] = std::max(0, number(p, "health") - 1);
    add(p, "hungerTick", 1);
    if (number(p, "hungerTick") >= 25) {
      p["hungerTick"] = 0;
      if (number(save_["mess"], "rack") > 0) {
        add(save_["mess"], "rack", -1);
        p["hunger"] = std::max(0, number(p, "hunger") - 40);
      } else
        p["hunger"] = std::min(100, number(p, "hunger") + 15);
    }
    if (number(p, "hunger") >= number(catalog()["constants"], "starve") &&
        number(s, "crewTick") % number(catalog()["constants"], "starveEvery") == 0)
      p["health"] = std::max(0, number(p, "health") - 1);
    if (number(s, "crewTick") % 10 == 0) {
      p["fatigue"] = std::clamp(number(p, "fatigue") + (text(p, "job").empty() ? -2 : 1), 0, 100);
      if (!text(p, "job").empty() && text(p, "skill") == "Untrained")
        p["morale"] = std::max(0, number(p, "morale") - 1);
    }
    living = living || number(p, "health") > 0;
  }
  // Only the best available cleaner applies; multiple hires cannot multiply morale.
  int cleaner = 100000;
  for (const auto& p : save_["crew"])
    if (text(p, "job") == "cleaner" && !busy(text(p, "id")))
      cleaner = std::min(cleaner, std::max(20, 40 - 4 * std::min(5, number(p, "jobAt"))));
  if (cleaner != 100000 && actions() % cleaner == 0)
    for (auto& p : save_["crew"]) p["morale"] = std::min(100, number(p, "morale") + 1);
  if (checkLoss()) return;
  add(s, "payTick", 1);
  if (number(s, "payTick") >= 100) {
    s["payTick"] = 0;
    std::vector<size_t> list;
    for (size_t i = 0; i < save_["crew"].size(); ++i) list.push_back(i);
    auto wage = [&](size_t i) {
      auto* spec = find(catalog()["jobs"], text(save_["crew"][i], "job"));
      return spec ? number(*spec, "wage") : 0;
    };
    std::stable_sort(list.begin(), list.end(),
                     [&](size_t a, size_t b) { return wage(a) > wage(b); });
    for (size_t i : list) {
      auto& p = save_["crew"][i];
      const int cost = wage(i);
      if (number(s, "shiplets") >= cost) {
        add(s, "shiplets", -cost);
        add(p, "wallet", cost);
      } else {
        p["job"] = "";
        p["morale"] = std::max(0, number(p, "morale") - 12);
        p["activity"] = "Idle in the corridor";
        remember("Work", text(p, "name") + " could not be paid.", text(p, "id"));
      }
    }
  }
  if (stat("hull") == 0 || !living) {
    s["gameOver"] = stat("hull") == 0 ? "The hull failed." : "All crew were lost.";
    s["nativeTasks"] = Json::array();
    return;
  }
  advanceWork(1);
  automate(initiating);
  auto& incidents = s["nativeIncidents"];
  for (auto it = incidents.begin(); it != incidents.end();) {
    const auto* spec = find(catalog()["incidents"], text(*it, "id"));
    add(*it, "left", -1);
    if (number(*it, "left") <= 0 && spec) {
      add(*it, "stage", 1);
      int stage = number(*it, "stage");
      if (stage >= static_cast<int>((*spec)["stages"].size())) {
        it = incidents.erase(it);
        continue;
      }
      const auto& effect = (*spec)["stages"][stage];
      for (auto name : {"hull", "shield", "energy"}) change(name, effect.value(name, 0));
      for (auto& p : save_["crew"]) {
        p["morale"] = std::clamp(number(p, "morale") + effect.value("morale", 0), 0, 100);
        p["health"] = std::clamp(number(p, "health") + effect.value("health", 0), 0, 100);
      }
      if (text(*it, "id") == "crew" && it->contains("a") && it->contains("b")) {
        auto a = text(*it, "a"), b = text(*it, "b");
        if (a > b) std::swap(a, b);
        auto key = a + "~" + b;
        s["sim"]["bonds"][key] = std::max(-100, s["sim"]["bonds"].value(key, 0) - 8);
        remember("Conflict", "The crew remain in conflict.", a, b);
      }
      if (number(effect, "ticks") == 0) {
        const auto id = "incident-" + text(*it, "id");
        auto& tasks = s["nativeTasks"];
        tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                                   [&](const Json& t) { return text(t, "id") == id; }),
                    tasks.end());
        it = incidents.erase(it);
        continue;
      }
      (*it)["left"] = number(effect, "ticks");
    }
    ++it;
  }
  for (const auto& spec : catalog()["incidents"]) {
    const auto id = text(spec, "id");
    const int first = number(spec, "first"), every = number(spec, "every");
    if (actions() >= first && (actions() - first) % every == 0 && !find(s["nativeIncidents"], id) &&
        (id != "crew" || save_["crew"].size() >= 2)) {
      s["nativeIncidents"].push_back(
          {{"id", id}, {"stage", 0}, {"left", number(spec["stages"][0], "ticks")}});
      if (id == "crew") {
        auto& event = s["nativeIncidents"].back();
        int worst = 101;
        for (size_t i = 0; i < save_["crew"].size(); ++i)
          for (size_t j = i + 1; j < save_["crew"].size(); ++j) {
            auto a = text(save_["crew"][i], "id"), b = text(save_["crew"][j], "id");
            auto key = a < b ? a + "~" + b : b + "~" + a;
            int bond = s["sim"]["bonds"].value(key, 0);
            if (bond < worst) {
              worst = bond;
              event["a"] = a;
              event["b"] = b;
            }
          }
      }
      remember("Incident", text(spec, "title"));
    }
  }
  tickRelationships();
  checkLoss();
}
int Voyage::metric(const std::string& name) const {
  const auto& s = save_["state"];
  const auto& j = s["chronicle"];
  if (name == "systems" || name == "regions") return static_cast<int>(j[name].size());
  if (name == "actions") return actions();
  if (name == "scans") return static_cast<int>(s["scanned"].size());
  if (name == "commissions") return number(s["commissions"], "completed");
  if (name == "contracts") {
    int n = 0;
    for (const auto& c : s["contracts"])
      if (c == "complete") ++n;
    return n;
  }
  if (name == "berth") return s["nativeVisited"].value("tolme", false) ? 1 : 0;
  if (name == "signal")
    return text(s["sim"], "signal") == "preserved" || text(s["sim"], "signal") == "sold" ? 1 : 0;
  return j.value(name, 0);
}
void Voyage::progress() {
  auto& s = save_["state"];
  for (const auto& award : catalog()["awards"])
    if (metric(text(award, "metric")) >= number(award, "goal"))
      s["chronicle"]["earned"][text(award, "id")] = true;
}
std::string Voyage::summary() const {
  const auto& s = save_["state"];
  std::ostringstream o;
  o << text(save_["ship"], "name") << "  |  " << location()
    << (s.value("docked", false) ? " [DOCKED]" : " [IN SPACE]") << "\nAction " << actions()
    << "   Shiplets " << number(s, "shiplets") << "\n";
  for (const auto& st : save_["stats"])
    o << text(st, "label") << " " << number(st, "value") << "%   ";
  o << "\nDrive " << number(s, "engineCondition") << "%   Air "
    << (number(s, "filterLife") > 0 ? number(s, "filterLife") / 4 : number(s, "airHold"))
    << " actions\nFuel cells " << number(s, "fuelCells") << "   Kits " << number(s, "repairKits")
    << "   Medkits " << number(s, "medkits") << "\nRations " << number(save_["mess"], "rack")
    << "   O2 canisters " << number(save_["life"], "canisters") << "   Cargo " << cargoUsed() << "/"
    << number(save_["cargo"], "capacity") << " m3\n";
  o << "\nONGOING WORK (actions to completion)";
  if (s["nativeTasks"].empty()) o << "\nNo work in progress.";
  for (const auto& t : s["nativeTasks"])
    o << "\n"
      << text(t, "id") << " / " << text(t, "target") << ": " << number(t, "left")
      << (t.value("state", std::string("running")) == "done"
              ? " / readout waiting (acknowledge free)"
              : " actions left");
  o << "\n\n" << cooldownReport();
  if (!text(s, "gameOver").empty()) o << "\nVOYAGE LOST: " << text(s, "gameOver");
  return o.str();
}

void Voyage::validate(const Json& d) const {
  const auto shape = [](const auto& self, const Json& actual, const Json& model) -> void {
    if (model.is_object()) {
      if (!actual.is_object()) throw std::runtime_error("Invalid save object.");
      for (auto it = model.begin(); it != model.end(); ++it)
        self(self, actual.at(it.key()), it.value());
    } else if (model.is_array()) {
      if (!actual.is_array()) throw std::runtime_error("Invalid save array.");
    } else if (model.is_number_integer()) {
      if (!actual.is_number_integer()) throw std::runtime_error("Invalid integer field.");
    } else if (model.is_number()) {
      if (!actual.is_number()) throw std::runtime_error("Invalid numeric field.");
    } else if (model.is_string() && !actual.is_string())
      throw std::runtime_error("Invalid text field.");
    else if (model.is_boolean() && !actual.is_boolean())
      throw std::runtime_error("Invalid boolean field.");
  };
  shape(shape, d, catalog()["initial"]);
  if (d.value("format", std::string{}) != "spaceship-native-1")
    throw std::runtime_error(
        "Not a native save. HTA saves are kept separate and are not overwritten.");
  if (!d.at("actions").is_number_integer() || d.at("actions").get<int>() < 0 ||
      d.at("actions").get<int>() > 10000000)
    throw std::runtime_error("Invalid action counter.");
  for (auto key : {"crew", "stats", "goods", "survey", "rooms"})
    if (!d.at(key).is_array()) throw std::runtime_error("Invalid save array.");
  if (!find(catalog()["bodies"], text(d.at("map"), "here")))
    throw std::runtime_error("Unknown location.");
  const auto& s = d.at("state");
  if (s["options"].at("audioLevel").get<double>() < 0 ||
      s["options"]["audioLevel"].get<double>() > 1)
    throw std::runtime_error("Invalid audio level.");

  if (text(s, "shipClass") != "wren" && text(s, "shipClass") != "kestrel")
    throw std::runtime_error("Unknown ship class.");
  for (auto key : {"shiplets", "fuelCells", "repairKits", "medkits", "engineCondition",
                   "filterLife", "airHold", "crewTick", "wearTick", "lotSerial"})
    if (!s.at(key).is_number_integer() || s.at(key).get<int>() < 0 ||
        s.at(key).get<int>() > 100000000)
      throw std::runtime_error("Invalid resource.");
  if ((d["crew"].empty() && text(s, "gameOver").empty()) || d["crew"].size() > 14)
    throw std::runtime_error("Invalid crew count.");
  for (const auto& p : d["crew"])
    for (auto key : {"health", "morale", "fatigue", "hunger"})
      if (!p.at(key).is_number_integer() || number(p, key) < 0 || number(p, key) > 100)
        throw std::runtime_error("Invalid crew condition.");
  auto validateOrder = [&](const Json& order) {
    if (!order.is_object()) throw std::runtime_error("Invalid crew order.");
    for (auto it = order.begin(); it != order.end(); ++it) {
      if (it.key() == "job") {
        if (!it.value().is_string() || (!it.value().get<std::string>().empty() &&
                                        !find(catalog()["jobs"], it.value().get<std::string>())))
          throw std::runtime_error("Unknown staged job.");
      } else if ((it.key() != "doc" && it.key() != "learnOn" && it.key() != "learnSt" &&
                  it.key() != "fire" && it.key() != "rest") ||
                 !it.value().is_boolean())
        throw std::runtime_error("Invalid staged order flag.");
    }
  };
  for (const auto& p : d["crew"]) {
    for (auto k : {"name", "role", "room"}) p.at(k).get<std::string>();
    for (auto k : {"skill", "job"})
      if (p.contains(k)) p.at(k).get<std::string>();
    if (!text(p, "job").empty() && !find(catalog()["jobs"], text(p, "job")))
      throw std::runtime_error("Unknown crew job.");
    for (auto k : {"jobAt", "wallet", "hungerTick", "idleTick"})
      if (p.contains(k) &&
          (!p[k].is_number_integer() || number(p, k) < 0 || number(p, k) > 100000000))
        throw std::runtime_error("Invalid crew counter.");
  }
  for (auto it = s["pendingOrders"].begin(); it != s["pendingOrders"].end(); ++it) {
    if (!find(d["crew"], it.key())) throw std::runtime_error("Order refers to missing crew.");
    validateOrder(it.value());
  }
  for (auto id : {"hull", "shield", "energy", "fuel"}) {
    const auto* st = find(d["stats"], id);
    if (!st || !st->at("value").is_number_integer() || number(*st, "value") < 0 ||
        number(*st, "value") > 100)
      throw std::runtime_error("Invalid ship condition.");
  }
  if (!s.at("nativeTasks").is_array() || !s.at("nativeIncidents").is_array() ||
      !s.at("nativeVisited").is_object() || !s.at("actionCooldowns").is_object())
    throw std::runtime_error("Invalid task state.");
  if (s["nativeTasks"].size() > 128 || s["nativeIncidents"].size() > catalog()["incidents"].size())
    throw std::runtime_error("Too many tasks or incidents.");
  auto unique = [](const Json& list) {
    std::vector<std::string> seen;
    for (const auto& item : list) {
      auto id = item.at("id").get<std::string>();
      if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end())
        throw std::runtime_error("Duplicate or empty ID.");
      seen.push_back(id);
    }
  };
  for (auto key : {"crew", "stats", "goods", "rooms"}) unique(d.at(key));
  unique(s["nativeTasks"]);
  unique(s["nativeIncidents"]);
  unique(d["life"]["slots"]);
  for (const auto& ev : s["nativeIncidents"]) {
    const auto* spec = find(catalog()["incidents"], text(ev, "id"));
    if (!spec || number(ev, "stage") < 0 ||
        number(ev, "stage") >= static_cast<int>((*spec)["stages"].size()) ||
        number(ev, "left") < 1 || number(ev, "left") > 1000)
      throw std::runtime_error("Invalid incident stage.");
  }
  if (d["rooms"].size() != 21 || d["stats"].size() != 4)
    throw std::runtime_error("Invalid ship structure.");
  const auto& rooms =
      text(s, "shipClass") == "wren" ? catalog()["initial"]["rooms"] : catalog()["largeRooms"];
  for (const auto& r : d["rooms"]) {
    const auto* authored = find(rooms, text(r, "id"));
    if (!authored) throw std::runtime_error("Unknown compartment.");
    for (auto field : {"x", "y", "w", "h"})
      if (r.at(field) != authored->at(field))
        throw std::runtime_error("Invalid compartment geometry.");
  }
  if (number(s, "engineCondition") > 100 || number(s, "airHold") > 20 ||
      number(s, "filterLife") > 200 || number(s, "wearTick") > 3)
    throw std::runtime_error("Invalid ship clock/condition.");
  if ((number(d["life"], "capacity") != 10 && number(d["life"], "capacity") != 15) ||
      number(d["life"], "canisters") > number(d["life"], "capacity") ||
      number(d["life"], "lifeMax") != 200 || number(d["life"], "decay") != 4)
    throw std::runtime_error("Invalid life support capacity.");
  if (number(d["mess"], "capacity") != 24 || number(d["mess"], "rack") < 0 ||
      number(d["mess"], "rack") > 24)
    throw std::runtime_error("Invalid ration rack.");
  const auto& sim = s["sim"];
  if (number(sim, "insight") < 0 || number(sim, "insight") > 5 || number(sim, "sensor") < 0 ||
      number(sim, "sensor") > 2)
    throw std::runtime_error("Invalid insight or calibration.");
  for (auto key : {"scanned", "nativeVisited"})
    for (const auto& value : s[key])
      if (!value.is_boolean()) throw std::runtime_error("Invalid progress flag.");
  for (auto key : {"observed", "broadcasts"})
    for (const auto& value : sim[key])
      if (!value.is_boolean()) throw std::runtime_error("Invalid simulation flag.");
  for (auto key : {"systems", "regions", "earned", "claimed"})
    for (const auto& value : s["chronicle"][key])
      if (!value.is_boolean()) throw std::runtime_error("Invalid chronicle flag.");
  for (auto key : {"banks", "projects", "dreams", "meals", "incidents"})
    if (number(s["chronicle"], key) < 0) throw std::runtime_error("Invalid chronicle count.");
  for (const auto& item : s["contracts"])
    if (!item.is_string() ||
        (item != "active" && item != "ready" && item != "complete" && item != "failed"))
      throw std::runtime_error("Invalid contract state.");
  for (const auto& m : sim["memories"]) {
    m.at("id").get<int>();
    m.at("processed").get<bool>();
    m.at("text").get<std::string>();
    m.at("kind").get<std::string>();
  }
  if (!s["commissions"]["active"].is_null()) {
    const auto& c = s["commissions"]["active"];
    auto kind = text(c, "kind");
    if ((kind != "visit" && kind != "scan" && kind != "salvage") || number(c, "done") < 0 ||
        number(c, "done") > number(c, "goal") || number(c, "goal") != (kind == "salvage" ? 2 : 1) ||
        (kind != "salvage" && !find(catalog()["bodies"], text(c, "target"))))
      throw std::runtime_error("Invalid commission.");
  }
  if (s.contains("nativeDreamSelection")) {
    if (!s["nativeDreamSelection"].is_array() || s["nativeDreamSelection"].size() > 2)
      throw std::runtime_error("Invalid dream selection.");
    for (const auto& selected : s["nativeDreamSelection"])
      if (!selected.is_number_integer()) throw std::runtime_error("Invalid memory ID.");
  }
  for (const auto& t : s["nativeTasks"]) {
    const auto id = text(t, "id");
    if (t.contains("by") && (!t["by"].is_string() || !find(d["crew"], text(t, "by"))))
      throw std::runtime_error("Unknown assigned worker.");
    if (id.rfind("orders-", 0) == 0) validateOrder(t.at("payload"));
    if (id == "crew-sheet") {
      if (!t.at("payload").is_object()) throw std::runtime_error("Invalid order sheet.");
      for (const auto& order : t["payload"]) validateOrder(order);
    }
    if (id == "project-dream") {
      if (!t.at("payload").is_array() || t["payload"].size() != 2)
        throw std::runtime_error("Invalid dream payload.");
      for (const auto& m : t["payload"]) {
        m.at("kind").get<std::string>();
        m.at("text").get<std::string>();
      }
    }
    bool known = id == "travel" || id == "scan" || id == "repair" || id == "maintain" ||
                 id == "cook" || id == "salvage" || id == "social" || id == "crew-sheet" ||
                 id.rfind("salvage-", 0) == 0;
    if (id.rfind("package-", 0) == 0) known = find(d["survey"], text(t, "target")) != nullptr;
    if (id.rfind("project-", 0) == 0) known = catalog()["projects"].contains(id.substr(8));
    if (id.rfind("install-", 0) == 0)
      known = find(d["life"]["slots"], text(t, "target")) != nullptr;
    if (id.rfind("orders-", 0) == 0 || id.rfind("doctor-", 0) == 0)
      known = find(d["crew"], text(t, "target")) != nullptr;
    if (id.rfind("medical-", 0) == 0 || id.rfind("rest-", 0) == 0)
      known = find(d["crew"], text(t, "target")) != nullptr;
    if (id.rfind("incident-", 0) == 0)
      known = find(catalog()["incidents"], text(t, "target")) != nullptr;
    if (id == "social" && (!t.at("payload").is_array() || t["payload"].size() != 2 ||
                           t["payload"][0] == t["payload"][1]))
      throw std::runtime_error("Invalid social assignment.");
    if (id == "social")
      for (const auto& member : t["payload"])
        if (!member.is_string() || !find(d["crew"], member.get<std::string>()))
          throw std::runtime_error("Unknown social participant.");
    if (!known || !t.at("left").is_number_integer() || number(t, "left") < 0 ||
        (number(t, "left") == 0 &&
         ((id != "travel" && id != "scan") || t.value("state", std::string{}) != "done")) ||
        number(t, "left") > 10000 || number(t, "total") < number(t, "left"))
      throw std::runtime_error("Invalid ongoing work.");
    if ((id == "travel" || id == "scan") && !find(catalog()["bodies"], text(t, "target")))
      throw std::runtime_error("Invalid work destination.");
  }
  for (const auto& cd : s["actionCooldowns"]) {
    int t = number(cd, "tier");
    if ((t != 5 && t != 10 && t != 15 && t != 20) || number(cd, "remaining") < 1 ||
        number(cd, "remaining") > t)
      throw std::runtime_error("Invalid cooldown.");
  }
  if (!s.at("sim").is_object() || !s["sim"].at("memories").is_array() ||
      s["sim"]["memories"].size() > 32)
    throw std::runtime_error("Invalid memory archive.");
  for (auto key : {"canisters", "capacity", "lifeMax", "decay"})
    if (number(d.at("life"), key) < 0) throw std::runtime_error("Invalid life support.");
  if (!d.at("life").at("slots").is_array() || d["life"]["slots"].size() != 3)
    throw std::runtime_error("Invalid filter rack.");
  for (const auto& slot : d["life"]["slots"]) {
    auto state = text(slot, "state");
    if (state != "empty" && state != "installing" && state != "seated" && state != "active")
      throw std::runtime_error("Invalid filter slot.");
  }
  for (const auto& g : d["goods"])
    if (number(g, "vol") < 0 || number(g, "value") < 0) throw std::runtime_error("Invalid cargo.");
}
bool Voyage::save(const std::filesystem::path& path) const {
  try {
    validate(save_);
    auto temporary = path;
    temporary += ".tmp";
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      out << save_.dump(2);
      out.flush();
      if (!out) return false;
    }
    // Preserve an existing slot until the replacement has been fully written.
    auto backup = path;
    backup += ".bak";
    std::error_code ec;
    if (std::filesystem::exists(path)) {
      std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing,
                                 ec);
      if (ec) return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
      std::filesystem::remove(path, ec);
      ec.clear();
      std::filesystem::rename(temporary, path, ec);
    }
    return !ec;
  } catch (...) {
    return false;
  }
}
bool Voyage::load(const std::filesystem::path& path) {
  try {
    if (std::filesystem::file_size(path) > 8 * 1024 * 1024) return reject("Save is too large.");
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe) {
      if (bytes.size() % 2) return reject("Truncated UTF-16 save.");
      std::u16string utf16;
      for (size_t i = 2; i < bytes.size(); i += 2)
        utf16.push_back(static_cast<char16_t>(static_cast<unsigned char>(bytes[i]) |
                                              (static_cast<unsigned char>(bytes[i + 1]) << 8)));
      std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
      bytes = convert.to_bytes(utf16);
    }
    Json value = Json::parse(bytes);
    const bool legacy = value.value("format", std::string{}) == "spaceship-command-1";
    if (legacy) value = importLegacy(value);
    validate(value);
    // Exercise read-only views before replacement to reject incomplete state.
    Voyage candidate;
    candidate.save_ = value;
    candidate.summary();
    candidate.controls("station", "");
    save_ = std::move(value);
    message_ = legacy ? "HTA voyage imported. Source file is unchanged; save into a native slot."
                      : "Voyage loaded.";
    return true;
  } catch (const std::exception& e) {
    return reject(std::string("Load failed: ") + e.what());
  }
}
}  // namespace spaceship

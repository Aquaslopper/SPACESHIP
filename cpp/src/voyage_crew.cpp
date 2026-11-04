#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "voyage.hpp"

namespace spaceship {
namespace {
const Json* find(const Json& list, const std::string& id) {
  for (const auto& item : list)
    if (item.value("id", std::string{}) == id) return &item;
  return nullptr;
}
std::string text(const Json& j, const char* key) { return j.value(key, std::string{}); }
int n(const Json& j, const char* key) { return j.value(key, 0); }
std::string system(const Json& b) { return b.value("system", std::string("sec-kessler")); }
std::string bondKey(std::string a, std::string b) {
  if (a > b) std::swap(a, b);
  return a + "~" + b;
}
}  // namespace
int Voyage::airStage() const {
  const auto& s = save_["state"];
  if (n(s, "filterLife") > 0 && stat("energy") > 0) return 4;
  return std::clamp(static_cast<int>(std::ceil(n(s, "airHold") / 5.0)), 0, 4);
}
int Voyage::crewWork(int base) const {
  return std::max(
      1, static_cast<int>(std::ceil(base * catalog()["air"][airStage()].at("slow").get<double>())));
}
Json Voyage::navigationMetrics(const std::string& id) const {
  const auto& d = catalog();
  const auto* here = find(d["bodies"], text(save_["map"], "here"));
  const auto* b = find(d["bodies"], id);
  const auto* targetSystem = find(d["systems"], id);
  if (!here || (!b && !targetSystem)) return {{"dist", 0}, {"fuel", 0}};
  double distance = 0;
  int work = 0;
  if (targetSystem) {
    const auto* origin = find(d["systems"], system(*here));
    if (text(*origin, "region") == text(*targetSystem, "region"))
      distance = std::hypot(n(*origin, "x") - n(*targetSystem, "x"),
                            n(*origin, "y") - n(*targetSystem, "y"));
    else {
      const auto* from = find(d["regions"], text(*origin, "region"));
      const auto* to = find(d["regions"], text(*targetSystem, "region"));
      distance =
          std::hypot((n(*to, "x") - n(*from, "x")) * 1.3, (n(*to, "y") - n(*from, "y")) * 1.3) +
          150;
    }
    work = std::max(6, static_cast<int>(std::floor(distance / 24 + 0.5)) + 5);
    if (text(*targetSystem, "id") == text(*origin, "id")) work = 0;
  } else {
    distance = std::hypot(n(*here, "x") - n(*b, "x"), n(*here, "y") - n(*b, "y"));
    work = std::max(
        3, static_cast<int>(std::floor(distance / save_["map"].value("pxPerAction", 16) + 0.5)));
    if (text(*b, "id") == text(*here, "id")) work = 0;
  }
  int navigator = 100;
  for (const auto& p : save_["crew"])
    if (text(p, "job") == "navigation" && !busy(text(p, "id"))) {
      navigator = 100 - 4 * std::clamp(n(p, "jobAt"), 1, 5);
      break;
    }
  const int drive = save_["state"]["upgrades"].value("drive", false) ? 80 : 100;
  return {{"dist", work},
          {"fuel", static_cast<int>(std::ceil(work * drive * navigator / 10000.0))},
          {"total", work + 1}};
}
Json Voyage::commissionOffer(const std::string& kind) const {
  const auto& d = catalog();
  const auto& s = save_["state"];
  const auto origin = text(save_["map"], "here");
  const auto* here = find(d["bodies"], origin);
  const auto* home = here ? find(d["systems"], system(*here)) : nullptr;
  if (!home || !s.value("docked", false) || !d["ports"].contains(origin) ||
      (kind != "visit" && kind != "scan" && kind != "salvage"))
    return nullptr;
  if (kind == "salvage")
    return {{"kind", kind}, {"origin", origin}, {"target", ""},
            {"done", 0},    {"goal", 2},        {"reward", 240}};
  std::vector<const Json*> all, local, fresh;
  for (const auto& b : d["bodies"]) {
    const auto* sys = find(d["systems"], system(b));
    if (!sys ||
        (kind == "visit" && (!d["ports"].contains(text(b, "id")) || text(b, "id") == origin)) ||
        (kind == "scan" && (text(*sys, "id") == text(*home, "id") || text(b, "type") == "origin")))
      continue;
    all.push_back(&b);
    if (text(*sys, "region") == text(*home, "region")) {
      local.push_back(&b);
      if (kind == "scan" && !s["scanned"].value(text(b, "id"), false)) fresh.push_back(&b);
    }
  }
  if (kind == "visit" && local.size() < 2) {
    const auto* originRegion = find(d["regions"], text(*home, "region"));
    while (local.size() < 2 && local.size() < all.size()) {
      const Json* best = nullptr;
      double bestDistance = 1e30;
      for (auto* b : all) {
        if (std::find(local.begin(), local.end(), b) != local.end()) continue;
        const auto* sys = find(d["systems"], system(*b));
        const auto* region = find(d["regions"], text(*sys, "region"));
        const double distance = std::pow(n(*region, "x") - n(*originRegion, "x"), 2) +
                                std::pow(n(*region, "y") - n(*originRegion, "y"), 2);
        if (distance < bestDistance) {
          best = b;
          bestDistance = distance;
        }
      }
      if (!best) break;
      local.push_back(best);
    }
  }
  const auto& pool = !fresh.empty() ? fresh : !local.empty() ? local : all;
  if (pool.empty()) return nullptr;
  size_t hash = 0;
  for (size_t i = 0; i < origin.size(); ++i)
    hash += static_cast<unsigned char>(origin[i]) * (i + 1);
  const auto* target =
      pool[(hash + static_cast<size_t>(n(s["commissions"], "serial"))) % pool.size()];
  return {{"kind", kind}, {"origin", origin}, {"target", text(*target, "id")},
          {"done", 0},    {"goal", 1},        {"reward", kind == "visit" ? 450 : 550}};
}
int Voyage::sheetCost(const Json& person, const Json& orders) const {
  auto job = orders.value("job", person.value("job", std::string{}));
  const auto* spec = find(catalog()["jobs"], job);
  int work = orders.contains("job") ? (spec ? n(*spec, "cost") : 1) : 0;
  for (auto id : {"doc", "learnOn", "learnSt", "fire", "rest"})
    if (orders.value(id, false)) {
      work +=
          std::string(id) == "fire"      ? 5
          : std::string(id) == "learnOn" ? (spec && n(*spec, "learnOn") ? n(*spec, "learnOn") : 1)
          : std::string(id) == "learnSt" ? (spec && n(*spec, "learnSt") ? n(*spec, "learnSt") : 2)
                                         : 1;
    }
  return std::max(1, work);
}
Json Voyage::orderQuote() const {
  int work = 0, fee = 0;
  const auto& sheet = save_["state"]["pendingOrders"];
  for (auto it = sheet.begin(); it != sheet.end(); ++it) {
    const auto* p = find(save_["crew"], it.key());
    if (!p) continue;
    work += sheetCost(*p, it.value());
    auto job = it.value().value("job", p->value("job", std::string{}));
    const auto* j = find(catalog()["jobs"], job);
    if (j && it.value().value("learnOn", false)) fee += n(*j, "feeOn");
    if (j && it.value().value("learnSt", false)) fee += n(*j, "feeSt");
  }
  return {{"work", work}, {"fee", fee}};
}
void Voyage::train(Json& p, int steps) {
  const auto* job = find(catalog()["jobs"], text(p, "job"));
  p["skill"] =
      text(p, "skill").empty() || text(p, "skill") == "Untrained" ? "Trained" : "Practised";
  if (job && n(*job, "trainStep") > 0)
    p["jobAt"] = std::min(n(*job, "trainCap"), n(p, "jobAt") + steps * n(*job, "trainStep"));
}
void Voyage::applyOrders(const std::string& id, const Json& orders) {
  Json* p = nullptr;
  for (auto& person : save_["crew"])
    if (text(person, "id") == id) p = &person;
  if (!p) return;
  if (orders.value("fire", false)) {
    if (!text(*p, "job").empty()) {
      (*p)["job"] = "";
      (*p)["morale"] = std::max(0, n(*p, "morale") - 12);
      (*p)["activity"] = "Idle in the corridor";
      remember("Work", text(*p, "name") + " was laid off.", id);
    }
    return;
  }
  if (orders.value("rest", false)) {
    (*p)["fatigue"] = std::max(0, n(*p, "fatigue") - 25);
    (*p)["morale"] = std::min(100, n(*p, "morale") + 5);
    remember("Rest", text(*p, "name") + " took a restorative break.", id);
  }
  if (orders.contains("job")) {
    auto job = text(orders, "job");
    const auto* spec = find(catalog()["jobs"], job);
    if (spec && spec->value("unique", false))
      for (auto& other : save_["crew"])
        if (text(other, "id") != id && text(other, "job") == job) other["job"] = "";
    (*p)["job"] = job;
    (*p)["jobAt"] = job == "maintenance" ? 30 : job == "cleaner" || job.empty() ? 0 : 1;
    if (!p->contains("skill")) (*p)["skill"] = "Untrained";
    if (job == "maintenance")
      (*p)["room"] = "engine";
    else if (job == "filters")
      (*p)["room"] = "o2";
    else if (job == "kitchen")
      (*p)["room"] = "mess";
    else if (job == "navigation")
      (*p)["room"] = "bridge";
    else if (job == "cleaner")
      (*p)["room"] = "quarters";
  }
  if (orders.value("doc", false)) {
    task("doctor-" + id, id, 7);
    (*p)["activity"] = "In the medical bay";
  }
  if (orders.value("learnOn", false)) train(*p, 1);
  if (orders.value("learnSt", false) && save_["state"].value("docked", false)) train(*p, 2);
  if (!orders.value("doc", false)) (*p)["activity"] = "Back on station";
}
std::string Voyage::anchor(const Json& p) {
  std::string bio = text(p, "role") + " " + text(p, "traits") + " " + text(p, "hobby");
  std::transform(bio.begin(), bio.end(), bio.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto has = [&](std::initializer_list<const char*> words) {
    for (const auto* word : words)
      if (bio.find(word) != std::string::npos) return true;
    return false;
  };
  if (has({"pilot", "navigation", "chart", "route"})) return "Wayfinder";
  if (has({"engineer", "mechanic", "repair", "wrench"})) return "Maker";
  if (has({"support", "spare", "suppl", "methodical"})) return "Keeper";
  if (has({"story", "stories", "notices", "flower", "recording"})) return "Witness";
  return "Companion";
}
std::string Voyage::desire(const Json& p) const {
  if (n(p, "health") < 75 || airStage() <= 1) return "Safety - care and breathable air";
  if (n(p, "fatigue") > 70) return "Rest - a break from the watch";
  if (n(p, "hunger") > 70) return "Nourishment - a reliable meal";
  if (n(p, "morale") < 60) return "Belonging - a change of pace or company";
  if (text(p, "job").empty() || text(p, "skill").empty() || text(p, "skill") == "Untrained")
    return "Mastery - useful work and training";
  auto name = anchor(p);
  if (name == "Wayfinder") return "Discovery - a new place or a quiet watch";
  if (name == "Maker") return "Mastery - something worth making";
  if (name == "Keeper") return "Safety - keeping the ship supplied";
  return "Belonging - stories and company";
}
void Voyage::startBank() {
  auto& s = save_["state"];
  s["filterLife"] = save_["life"]["lifeMax"];
  for (auto& slot : save_["life"]["slots"]) slot["state"] = "active";
  s["chronicle"]["banks"] = n(s["chronicle"], "banks") + 1;
  advanceWork(10);
}
void Voyage::automate(const std::string& initiating) {
  auto& s = save_["state"];
  if (actions() % 30 == 0) {
    const Json* chosen = nullptr;
    for (const auto& p : save_["crew"])
      if (n(p, "fatigue") >= 75 && desire(p).rfind("Rest", 0) == 0 && !busy(text(p, "id")) &&
          !s["pendingOrders"].contains(text(p, "id")) &&
          (!chosen || n(p, "fatigue") > n(*chosen, "fatigue")))
        chosen = &p;
    if (chosen) {
      auto id = text(*chosen, "id");
      task("orders-" + id, id, 3, {{"rest", true}});
      remember("Need", text(*chosen, "name") + " stepped away to rest.", id);
    }
  }
  for (const auto& p : save_["crew"])
    if (n(p, "health") > 0 && !busy(text(p, "id"))) {
      const auto job = text(p, "job"), id = text(p, "id");
      if (job == "maintenance" && initiating != "maintain" && initiating != "project-cooling" &&
          !busy("maintain") && !busy("project-cooling") && n(s, "engineCondition") < 100 &&
          n(s, "engineCondition") <=
              std::max(5, n(p, "jobAt") - n(catalog()["air"][airStage()], "jobDrop"))) {
        task("maintain", "engine",
             crewWork(static_cast<int>(std::ceil((100 - n(s, "engineCondition")) / 10.0))));
        s["nativeTasks"].back()["by"] = id;
      }
      if (job == "filters" && initiating.rfind("install-", 0) != 0 &&
          initiating != "start-filters" && n(s, "filterLife") == 0 && stat("energy") > 0 &&
          airStage() <= n(p, "jobAt")) {
        bool ready = true, installing = false;
        for (const auto& slot : save_["life"]["slots"]) {
          ready = ready && text(slot, "state") == "seated";
          installing = installing || busy("install-" + text(slot, "id"));
        }
        if (ready) {
          startBank();
          continue;
        }
        if (!installing && n(save_["life"], "canisters") > 0)
          for (auto& slot : save_["life"]["slots"])
            if (text(slot, "state") == "empty") {
              save_["life"]["canisters"] = n(save_["life"], "canisters") - 1;
              slot["state"] = "installing";
              task("install-" + text(slot, "id"), text(slot, "id"), crewWork(2));
              s["nativeTasks"].back()["by"] = id;
              break;
            }
      }
      if (job == "kitchen" && initiating != "cook" && initiating != "project-food" &&
          !busy("cook") && !busy("project-food") &&
          n(save_["mess"], "rack") <= n(save_["mess"], "capacity") / 2) {
        int rate = std::max(1, n(p, "jobAt"));
        task("cook", "mess",
             crewWork(static_cast<int>(
                 std::ceil((n(save_["mess"], "capacity") - n(save_["mess"], "rack")) /
                           static_cast<double>(rate)))),
             {{"rate", rate}});
        s["nativeTasks"].back()["by"] = id;
      }
    }
}
void Voyage::tickRelationships() {
  if (actions() % 25 || save_["crew"].size() < 2) return;
  std::vector<std::pair<size_t, size_t>> pairs;
  for (size_t i = 0; i < save_["crew"].size(); ++i)
    for (size_t j = i + 1; j < save_["crew"].size(); ++j) pairs.push_back({i, j});
  auto pair = pairs[(static_cast<size_t>(actions() / 25) - 1) % pairs.size()];
  auto& a = save_["crew"][pair.first];
  auto& b = save_["crew"][pair.second];
  const auto key = bondKey(text(a, "id"), text(b, "id"));
  auto& bonds = save_["state"]["sim"]["bonds"];
  int bond = bonds.value(key, 0);
  if (n(a, "fatigue") >= 75 && n(b, "fatigue") >= 75 && n(a, "morale") < 55 &&
      n(b, "morale") < 55) {
    bond = std::max(-100, bond - 3);
    bonds[key] = bond;
  }
  if (bond >= 20 || bond <= -20) {
    a["morale"] = std::clamp(n(a, "morale") + (bond > 0 ? 1 : -1), 0, 100);
    b["morale"] = std::clamp(n(b, "morale") + (bond > 0 ? 1 : -1), 0, 100);
  }
}
void Voyage::contractProgress(const std::string& kind, const std::string& target) {
  auto& s = save_["state"];
  for (const auto& c : catalog()["contracts"]) {
    auto id = text(c, "id"), goal = text(c, "goal");
    if (s["contracts"].value(id, std::string{}) != "active" || text(c, "target") != target ||
        (goal != kind && !(kind == "visit" && (goal == "freight" || goal == "passenger"))))
      continue;
    if (goal == "freight" && !find(save_["goods"], "contract-" + id)) continue;
    if (goal == "passenger" && !find(s["passengers"], id)) continue;
    s["contracts"][id] = "ready";
    auto& goods = save_["goods"];
    goods.erase(
        std::remove_if(goods.begin(), goods.end(),
                       [&](const Json& g) { return g.value("contractId", std::string{}) == id; }),
        goods.end());
    auto& passengers = s["passengers"];
    passengers.erase(std::remove_if(passengers.begin(), passengers.end(),
                                    [&](const Json& p) { return text(p, "id") == id; }),
                     passengers.end());
  }
}
bool Voyage::checkLoss() {
  auto& s = save_["state"];
  auto& crew = save_["crew"];
  for (auto it = crew.begin(); it != crew.end();) {
    if (n(*it, "health") > 0) {
      ++it;
      continue;
    }
    const auto id = text(*it, "id");
    s["pendingOrders"].erase(id);
    auto& tasks = s["nativeTasks"];
    for (auto job = tasks.begin(); job != tasks.end();) {
      bool assigned = text(*job, "by") == id || text(*job, "target") == id;
      if (text(*job, "id") == "social")
        for (const auto& person : job->at("payload")) assigned = assigned || person == id;
      if (assigned) {
        if (text(*job, "id").rfind("install-", 0) == 0) {
          save_["life"]["canisters"] =
              std::min(n(save_["life"], "capacity"), n(save_["life"], "canisters") + 1);
          for (auto& slot : save_["life"]["slots"])
            if (text(slot, "id") == text(*job, "target")) slot["state"] = "empty";
        }
        job = tasks.erase(job);
      } else
        ++job;
    }
    remember("Loss", text(*it, "name") + " has died.", id);
    it = crew.erase(it);
  }
  if (stat("hull") == 0 || crew.empty()) {
    s["gameOver"] = stat("hull") == 0 ? "The hull failed." : "All crew were lost.";
    s["nativeTasks"] = Json::array();
    return true;
  }
  return false;
}
}  // namespace spaceship

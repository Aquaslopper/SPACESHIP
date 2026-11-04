#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "voyage.hpp"

using spaceship::Json;
using spaceship::Voyage;
namespace {
int failures = 0;
std::filesystem::path directory;
void require(bool condition, const std::string& line) {
  if (!condition) throw std::runtime_error(line);
}
void test(const char* name, const std::function<void()>& body) {
  try {
    body();
    std::cout << "PASS: " << name << '\n';
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << name << ": " << e.what() << '\n';
  }
}
void write(const Json& d, const std::string& name = "fixture.json") {
  std::ofstream out(directory / name);
  out << d.dump();
}
Voyage fixture(const std::function<void(Json&)>& change) {
  Voyage g;
  Json d = g.snapshot();
  change(d);
  write(d);
  require(g.load(directory / "fixture.json"), g.message());
  return g;
}
void watch(Voyage& g, int count) {
  for (int i = 0; i < count; ++i) require(g.act("watch"), g.message());
}
const Json* task(const Voyage& g, const std::string& id) {
  for (const auto& t : g.snapshot()["state"]["nativeTasks"])
    if (t["id"] == id) return &t;
  return nullptr;
}
void stat(Json& d, const std::string& id, int value) {
  for (auto& st : d["stats"])
    if (st["id"] == id) st["value"] = value;
}
}  // namespace
int main() {
  directory = std::filesystem::temp_directory_path() /
              ("spaceship-native-tests-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  test("compiled authored catalog preserves 48 systems, 243 bodies and 21 compartments", [] {
    Voyage g;
    require(Voyage::catalog()["systems"].size() == 48, "systems");
    require(Voyage::catalog()["bodies"].size() == 243, "bodies");
    require(g.snapshot()["rooms"].size() == 21, "rooms");
    require(g.cargoUsed() == 32, "starting cargo");
    require(g.actions() == 0, "reading is free");
    for (const auto& s : Voyage::catalog()["systems"]) {
      bool entry = false;
      for (const auto& b : Voyage::catalog()["bodies"])
        if (b["id"] == s["entry"]) entry = true;
      require(entry, "system entry missing");
    }
  });
  test("native views and availability probes never mutate the voyage", [] {
    Voyage g;
    auto before = g.snapshot();
    for (auto page :
         {"home",       "navigation", "crew",    "quarters", "cargo",  "station", "science",
          "simulacron", "events",     "rewards", "guide",    "engine", "o2",      "mess",
          "r1",         "r2",         "r3",      "r4",       "r5",     "r6",      "r7",
          "r8",         "r9",         "r10",     "r11",      "r12",    "r13"}) {
      const auto entries = g.choices(page);
      auto selected = entries.empty() ? "" : entries.front().id;
      g.describe(page, selected);
      g.controls(page, selected);
    }
    require(g.snapshot() == before, "free UI mutated state");
  });
  test("cooldowns start after source action, independent controls tick them, repeats do not", [] {
    Voyage g;
    require(g.act("fuel"), g.message());
    require(g.cooldown("fuel:") == 10, "fresh tier");
    int n = g.actions();
    auto before = g.snapshot();
    require(!g.act("fuel"), "repeat committed");
    require(g.snapshot() == before && g.actions() == n, "blocked repeat mutated");
    require(g.act("talk", "rell"), g.message());
    require(g.cooldown("fuel:") == 9, "other action did not tick");
    watch(g, 9);
    require(g.cooldown("fuel:") == 0, "cooldown did not expire");
  });
  test("invalid commands roll back every mutation and do not advance time", [] {
    Voyage g;
    auto before = g.snapshot();
    require(!g.act("travel", "not-a-body"), "unknown travel accepted");
    require(!g.act("project", "dream"), "one memory dream accepted");
    require(!g.act("buy", "fuel"), "trade in space accepted");
    require(!g.act("install", "not-a-slot"), "unknown slot");
    require(g.snapshot() == before, "invalid command left changes");
  });
  test("filter installation uses three independent controls and powers a running bank", [] {
    Voyage g;
    require(g.act("install", "s0"), g.message());
    require(task(g, "install-s0")->at("left") == 3, "initiating tick counted");
    require(g.act("install", "s1"), g.message());
    require(task(g, "install-s0")->at("left") == 2, "old work did not tick");
    require(g.act("install", "s2"), g.message());
    watch(g, 3);
    for (const auto& slot : g.snapshot()["life"]["slots"])
      require(slot["state"] == "seated", "not seated");
    require(g.act("filters"), g.message());
    require(g.snapshot()["state"]["chronicle"]["banks"] == 1, "bank metric");
    require(g.snapshot()["state"]["filterLife"] == 200,
            "initiating action consumed new bank charge");
    require(g.act("claim", "breath"), g.message());
    int n = g.actions();
    require(!g.act("claim", "breath"), "claimed twice");
    require(g.actions() == n, "claim spent action");
  });
  test("player-facing watch prevents the original console deadlock and advances real work", [] {
    Voyage g;
    require(g.act("travel", "halvan"), g.message());
    require(g.act("scan", "halvan"), g.message());
    require(g.act("watch"), g.message());
    require(g.act("fuel"), g.message());
    watch(g, 20);
    require(g.location() == "Halvan's Reach", "crossing failed");
    require(g.snapshot()["state"]["scanned"].value("halvan", false), "scan failed");
    require(g.snapshot()["state"]["sim"]["signal"] == "intercepted", "signal not intercepted");
    require(g.act("travel", "tolme"), g.message());
  });
  test("blackout pauses powered work and fuel restores it", [] {
    Voyage g;
    require(g.act("scan", "halvan"), g.message());
    Json d = g.snapshot();
    stat(d, "energy", 0);
    write(d);
    require(g.load(directory / "fixture.json"), g.message());
    int left = task(g, "scan")->at("left");
    watch(g, 3);
    require(task(g, "scan")->at("left") == left, "blackout progressed work");
    require(g.act("fuel"), g.message());
    require(task(g, "scan")->at("left") == left - 1, "restored power did not tick old work");
  });
  test("cargo trading uses regional prices and station restrictions", [] {
    auto g = fixture([](Json& d) {
      d["map"]["here"] = "tolme";
      d["state"]["docked"] = true;
    });
    int cash = g.snapshot()["state"]["shiplets"], used = g.cargoUsed();
    require(g.act("sell", "ore"), g.message());
    require(g.snapshot()["state"]["shiplets"].get<int>() == cash + 336, "Tolme price");
    require(g.cargoUsed() == used - 14, "lot not removed");
    require(g.act("buy", "fuel"), g.message());
    require(g.snapshot()["state"]["fuelCells"] == 13, "buyfuel did not buy a cell");
    require(g.act("undock"), g.message());
    auto before = g.snapshot();
    require(!g.act("buy", "fuel"), "buy outside port");
    require(g.snapshot() == before, "failed buy mutated");
  });
  test("crossings record visits and ready contracts; collection pays once", [] {
    Voyage g;
    require(g.act("travel", "tolme"), g.message());
    watch(g, 15);
    require(g.snapshot()["state"]["docked"] == true, "not docked on arrival");
    require(g.snapshot()["state"]["contracts"]["arrival"] == "ready", "arrival not ready");
    int cash = g.snapshot()["state"]["shiplets"];
    require(g.act("contract", "arrival"), g.message());
    require(g.snapshot()["state"]["shiplets"].get<int>() == cash + 120, "reward");
    require(!g.act("contract", "arrival"), "paid twice");
  });
  test("repeatable salvage commission counts only new completed work", [] {
    auto g = fixture([](Json& d) {
      d["map"]["here"] = "tolme";
      d["state"]["docked"] = true;
      d["goods"] = Json::array();
    });
    require(g.act("commission", "salvage"), g.message());
    require(g.act("undock"), g.message());
    require(g.act("travel", "relay"), g.message());
    require(g.act("salvage"), g.message());
    watch(g, 15);
    require(g.snapshot()["state"]["commissions"]["active"]["done"] == 1, "salvage commission");
    require(g.snapshot()["goods"].size() == 1, "salvage delivered");
  });
  test("ship projects reserve inputs, complete exactly once, and survive loading", [] {
    auto g = fixture([](Json& d) {
      d["goods"].push_back({{"id", "test-salvage"},
                            {"name", "Salvage"},
                            {"kind", "Salvage"},
                            {"risk", false},
                            {"vol", 5},
                            {"value", 120}});
    });
    int kits = g.snapshot()["state"]["repairKits"];
    require(g.act("project", "kit"), g.message());
    require(g.snapshot()["goods"].size() == 4, "input not reserved");
    require(g.save(directory / "slot1.json"), "save failed");
    Voyage loaded;
    require(loaded.load(directory / "slot1.json"), loaded.message());
    watch(loaded, 4);
    require(loaded.snapshot()["state"]["repairKits"].get<int>() == kits + 1, "output");
    watch(loaded, 3);
    require(loaded.snapshot()["state"]["repairKits"].get<int>() == kits + 1, "duplicated output");
  });
  test("crew care reserves members and social work excludes overlapping orders", [] {
    Voyage g;
    require(g.act("medical", "jax"), g.message());
    require(!g.act("rest", "jax"), "busy crew rested");
    watch(g, 4);
    require(g.snapshot()["crew"][1]["health"] == 100, "healing");
    require(g.act("social"), g.message());
    require(!g.act("rest", "rell"), "social participant reused");
    watch(g, 3);
    require(g.snapshot()["state"]["chronicle"]["meals"] == 1, "meal not recorded");
    require(!g.snapshot()["state"]["sim"]["bonds"].empty(), "no bond");
  });
  test("hiring, assignments and refits preserve the current voyage", [] {
    auto g = fixture([](Json& d) {
      d["map"]["here"] = "tolme";
      d["state"]["docked"] = true;
      d["state"]["shiplets"] = 10000;
    });
    require(g.act("hire", "nemi"), g.message());
    require(g.snapshot()["crew"].size() == 5, "hire");
    require(g.act("assign-navigation", "nemi"), g.message());
    require(g.act("train", "nemi"), g.message());
    require(g.act("upgrade", "bay"), g.message());
    require(g.snapshot()["cargo"]["capacity"] == 60, "small cargo refit");
    require(g.act("ship"), g.message());
    require(g.snapshot()["cargo"]["capacity"] == 90, "larger refit transfer");
    require(g.snapshot()["crew"].size() == 5 && g.cargoUsed() == 32, "voyage lost on transfer");
    require(!g.act("ship"), "bought same ship twice");
  });
  test("Silicon Marrow decoding, remote scans and both permanent endings", [] {
    auto start = [](Json& d) {
      d["state"]["sim"]["signal"] = "intercepted";
      d["state"]["sim"]["insight"] = 1;
    };
    auto g = fixture(start);
    require(g.act("project", "decode"), g.message());
    watch(g, 4);
    require(g.snapshot()["state"]["sim"]["signal"] == "coordinates", "decode");
    Json state = g.snapshot();
    state["map"]["here"] = "echo-star";
    write(state);
    require(g.load(directory / "fixture.json"), g.message());
    require(g.act("scan", "echo-site"), g.message());
    watch(g, 5);
    require(g.snapshot()["state"]["sim"]["signal"] == "request", "request");
    state = g.snapshot();
    state["map"]["here"] = "palimpsest-site";
    write(state);
    require(g.load(directory / "fixture.json"), g.message());
    require(g.act("preserve"), g.message());
    require(g.snapshot()["state"]["sim"]["signal"] == "preserved", "preserved");
    require(!g.act("sell-signal"), "ending reversed");
    auto sold = fixture([](Json& d) {
      d["map"]["here"] = "tolme";
      d["state"]["docked"] = true;
      d["state"]["sim"]["signal"] = "request";
    });
    require(sold.act("sell-signal"), sold.message());
    require(sold.snapshot()["state"]["shiplets"] == 1240, "sale reward");
  });
  test("memory consolidation consumes two memories and produces insight on completion", [] {
    Voyage g;
    require(g.act("talk", "rell"), g.message());
    require(g.act("project", "dream"), g.message());
    watch(g, 4);
    require(g.snapshot()["state"]["sim"]["insight"] == 1, "insight");
    require(g.snapshot()["state"]["chronicle"]["dreams"] == 1, "dream metric");
  });
  test("incident work resolves while ignored incidents escalate on actions only", [] {
    Voyage g;
    watch(g, 45);
    require(g.snapshot()["state"]["nativeIncidents"].size() == 1, "incident missing");
    int n = g.actions();
    g.describe("events", "fracture");
    require(g.actions() == n, "reading ticked");
    require(g.act("resolve", "fracture"), g.message());
    watch(g, 4);
    require(g.snapshot()["state"]["nativeIncidents"].empty(), "incident not resolved");
    require(g.snapshot()["state"]["chronicle"]["incidents"] == 1, "incident metric");
  });
  test("save slots are independent and malformed loads preserve live state", [] {
    Voyage g;
    require(g.save(directory / "slot1.json"), "save one");
    watch(g, 3);
    require(g.save(directory / "slot2.json"), "save two");
    Voyage a, b;
    require(a.load(directory / "slot1.json"), a.message());
    require(b.load(directory / "slot2.json"), b.message());
    require(a.actions() == 0 && b.actions() == 3, "slots mixed");
    auto before = g.snapshot();
    for (int i = 0; i < 13; ++i) {
      Json bad = before;
      if (i == 0) bad["format"] = "spaceship-command-1";
      if (i == 1) bad["actions"] = -1;
      if (i == 2) bad["crew"][0]["health"] = 101;
      if (i == 3) bad["state"]["actionCooldowns"]["fake"] = {{"tier", 15}, {"remaining", 16}};
      if (i == 4) bad["map"]["here"] = "unknown";
      if (i == 5)
        bad["state"]["nativeTasks"].push_back(
            {{"id", "alien"}, {"target", ""}, {"left", 1}, {"total", 1}, {"payload", nullptr}});
      if (i == 6) bad["stats"][0]["value"] = 1.5;
      if (i == 7) bad["rooms"][0]["x"] = 2000000000;
      if (i == 8) bad["state"]["sim"]["insight"] = 6;
      if (i == 9)
        bad["state"]["nativeIncidents"].push_back(
            {{"id", "fracture"}, {"stage", 999}, {"left", 1}});
      if (i == 10) bad["crew"].push_back(bad["crew"][0]);
      if (i == 11) bad["state"]["chronicle"]["earned"]["fake"] = "not-a-flag";
      if (i == 12) bad["state"].erase("sim");
      write(bad, "bad.json");
      require(!g.load(directory / "bad.json"), "malformed accepted " + std::to_string(i));
      require(g.snapshot() == before, "malformed load replaced game");
    }
    require(!g.load(directory / "missing.json"), "missing accepted");
    require(g.snapshot() == before, "missing mutated");
  });
  test("game over cancels work, blocks actions, and save/load retains loss", [] {
    auto g = fixture([](Json& d) {
      for (auto& p : d["crew"]) p["health"] = 0;
    });
    require(g.act("scan", "halvan"), g.message());
    require(!g.snapshot()["state"]["gameOver"].get<std::string>().empty(), "loss not detected");
    require(g.snapshot()["state"]["nativeTasks"].empty(), "posthumous work");
    auto before = g.snapshot();
    require(!g.act("watch"), "lost voyage ticked");
    require(g.snapshot() == before, "lost state changed");
    require(g.save(directory / "lost.json"), "loss save");
  });
  test("native save round-trip retains cooldowns and backs up the previous slot", [] {
    Voyage g;
    require(g.act("fuel"), g.message());
    require(g.save(directory / "backup.json"), "first save");
    auto original = g.snapshot();
    watch(g, 3);
    require(g.save(directory / "backup.json"), "replacement save");
    Voyage loaded, backup;
    require(loaded.load(directory / "backup.json"), loaded.message());
    require(loaded.snapshot() == g.snapshot(), "snapshot changed during serialization");
    require(loaded.cooldown("fuel:") == 7, "cooldown reset on load");
    require(backup.load(directory / "backup.json.bak"), backup.message());
    require(backup.snapshot() == original, "backup did not retain original slot");
    const auto unicodePath = directory / std::filesystem::u8path("voyage space ü.json");
    require(g.save(unicodePath), "Unicode save path");
    require(loaded.load(unicodePath), loaded.message());
  });
  test("a longer player-command voyage remains serializable without hidden test actions", [] {
    Voyage g;
    for (int n = 0; n < 120; ++n) {
      if (n % 18 == 0) g.act("fuel");
      if (n % 13 == 0) g.act("talk", "rell");
      if (n % 17 == 0) g.act("rest", "suri");
      if (n % 19 == 0) g.act("cook");
      require(g.act("watch"), g.message());
      require(g.save(directory / "long-voyage.json"), "long voyage save validation");
      Voyage loaded;
      require(loaded.load(directory / "long-voyage.json"), loaded.message());
      require(loaded.snapshot() == g.snapshot(), "long voyage round-trip");
      for (const auto& state : g.snapshot()["stats"]) {
        int value = state["value"];
        require(value >= 0 && value <= 100, "condition escaped range");
      }
    }
  });
  std::filesystem::remove_all(directory);
  std::cout << failures << " failing test groups\n";
  return failures ? 1 : 0;
}

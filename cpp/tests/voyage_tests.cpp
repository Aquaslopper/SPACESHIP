#include <chrono>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
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
    require(task(g, "install-s0")->at("left") == 2, "initiating tick counted");
    require(g.act("install", "s1"), g.message());
    require(task(g, "install-s0")->at("left") == 1, "old work did not tick");
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
    require(g.location() == "Kessler Drift", "crossing arrived before acknowledgement");
    require(g.act("acknowledge", "travel"), g.message());
    require(g.act("acknowledge", "scan"), g.message());
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
    watch(g, 30);
    require(g.act("acknowledge", "travel"), g.message());
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
    require(g.act("acknowledge", "scan"), g.message());
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
    require(!g.act("rest", "jax"), "assigned engineer not reserved");
    watch(g, 6);
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
  test("reference parity: all 2547 local and system route metrics", [] {
    std::ifstream file("cpp/tests/reference_parity.json");
    Json reference;
    file >> reference;
    Voyage g;
    std::string origin;
    for (const auto& c : reference["navigation"]) {
      if (c["origin"] != origin) {
        origin = c["origin"];
        g = fixture([&](Json& d) { d["map"]["here"] = origin; });
      }
      auto actual = g.navigationMetrics(c["target"]);
      require(actual["dist"] == c["dist"] && actual["fuel"] == c["fuel"],
              "route mismatch " + c.dump() + " actual=" + actual.dump());
    }
  });
  test("reference parity: deterministic and rotating commission offers at every port", [] {
    std::ifstream file("cpp/tests/reference_parity.json");
    Json reference;
    file >> reference;
    for (const auto& c : reference["commissions"]) {
      auto g = fixture([&](Json& d) {
        d["map"]["here"] = c["origin"];
        d["state"]["docked"] = true;
        d["state"]["commissions"]["serial"] = c["serial"];
      });
      auto offer = g.commissionOffer(c["kind"]);
      for (auto key : {"kind", "origin", "target", "reward"})
        require(offer[key] == c["offer"][key],
                "offer mismatch " + c.dump() + " actual=" + offer.dump());
    }
  });
  test(
      "reference parity: 250 reference world ticks including incidents, hunger, relationships and "
      "automation",
      [] {
        std::ifstream file("cpp/tests/reference_parity.json");
        Json reference;
        file >> reference;
        Voyage g;
        for (const auto& expected : reference["watch"]) {
          require(g.act("watch"), g.message());
          const auto& d = g.snapshot();
          const auto& s = d["state"];
          Json actual = {{"actions", g.actions()},
                         {"engine", s["engineCondition"]},
                         {"wear", s["wearTick"]},
                         {"pay", s["payTick"]},
                         {"air", s["airHold"]},
                         {"filter", s["filterLife"]},
                         {"canisters", d["life"]["canisters"]},
                         {"rations", d["mess"]["rack"]},
                         {"crew", Json::array()},
                         {"stats", Json::array()},
                         {"events", Json::array()}};
          for (const auto& st : d["stats"])
            actual["stats"].push_back({{"id", st["id"]}, {"value", st["value"]}});
          for (const auto& ev : s["nativeIncidents"])
            actual["events"].push_back(
                {{"id", ev["id"]}, {"stage", ev["stage"]}, {"left", ev["left"]}});
          for (const auto& p : d["crew"]) {
            Json person;
            for (auto k : {"id", "health", "morale", "fatigue", "hunger", "hungerTick", "wallet"})
              person[k] = p[k];
            actual["crew"].push_back(person);
          }
          require(actual == expected, "tick mismatch at " + std::to_string(g.actions()) +
                                          " expected=" + expected.dump() +
                                          " actual=" + actual.dump());
        }
      });
  test("staging is free, sheet pricing uses the staged job, and each person releases independently",
       [] {
         auto g = fixture([](Json& d) {
           d["state"]["shiplets"] = 1000;
           d["state"]["docked"] = true;
           d["map"]["here"] = "tolme";
         });
         require(g.act("stage-job=maintenance", "rell"), g.message());
         require(g.act("stage-learnSt", "rell"), g.message());
         require(g.act("stage-rest", "suri"), g.message());
         require(g.actions() == 0, "staging ticked");
         require(g.orderQuote() == Json({{"work", 8}, {"fee", 50}}),
                 "wrong staged-job fee or work");
         require(g.act("issue-orders"), g.message());
         require(g.actions() == 1, "issuing costs one action");
         require(task(g, "orders-rell")->at("left") == 7 && task(g, "orders-suri")->at("left") == 1,
                 "personal task cost");
         require(!g.act("rest", "rell"), "orders did not reserve crew");
         watch(g, 1);
         require(!task(g, "orders-suri"), "short errand not released");
         require(!g.act("stage-rest", "suri"), "new sheet accepted before old sheet discharged");
         watch(g, 6);
         const auto& rell = g.snapshot()["crew"][0];
         require(rell["job"] == "maintenance" && rell["jobAt"] == 60,
                 "two station training steps not applied");
         watch(g, 1);
         require(!task(g, "crew-sheet"), "sheet did not discharge");
       });
  test("orders doctor follow-up is not advanced by its own creation tick or a pro-action burst",
       [] {
         Voyage g;
         require(g.act("stage-doc", "jax"), g.message());
         require(g.act("issue-orders"), g.message());
         watch(g, 1);
         require(task(g, "doctor-jax") && task(g, "doctor-jax")->at("left") == 7,
                 "follow-up consumed own tick");
         int health = g.snapshot()["crew"][1]["health"];
         watch(g, 7);
         require(g.snapshot()["crew"][1]["health"].get<int>() == std::min(100, health + 12),
                 "doctor care result");
       });
  test("native cooking yields rations while working and unpowered crew work continues", [] {
    auto g = fixture([](Json& d) {
      d["mess"]["rack"] = 5;
      stat(d, "energy", 0);
      for (auto& p : d["crew"]) p["job"] = "";
    });
    require(g.act("cook"), g.message());
    int start = g.snapshot()["mess"]["rack"];
    watch(g, 1);
    require(g.snapshot()["mess"]["rack"].get<int>() == start + 1,
            "galley did not produce during blackout");
  });
  test("selected exploration dreams yield extra insight and shared-person reconciliation rewards",
       [] {
         auto g = fixture([](Json& d) {
           d["state"]["sim"]["memories"] = Json::array({{{"id", 1},
                                                         {"kind", "Discovery"},
                                                         {"text", "A discovery"},
                                                         {"who", "rell"},
                                                         {"other", ""},
                                                         {"processed", false}},
                                                        {{"id", 2},
                                                         {"kind", "Arrival"},
                                                         {"text", "An arrival"},
                                                         {"who", "rell"},
                                                         {"other", ""},
                                                         {"processed", false}}});
           d["state"]["sim"]["serial"] = 2;
         });
         require(g.act("select-memory", "2"), g.message());
         require(g.act("select-memory", "1"), g.message());
         require(g.actions() == 0, "dream selection ticked");
         require(g.act("project", "dream"), g.message());
         watch(g, 4);
         require(g.snapshot()["state"]["sim"]["insight"] == 2, "exploration combination reward");
         require(g.snapshot()["crew"][0]["morale"] == 73, "shared-person dream reward");
       });
  test("HTA UTF-8 and UTF-16 saves import live tasks without changing the source", [] {
    Json original = Voyage::catalog()["initial"];
    original["crew"][0]["name"] = "Rell \xc3\xbc \xf0\x9f\x9a\x80";
    original["state"]["ongoing"].push_back({{"id", "travel"},
                                            {"type", "travel"},
                                            {"target", "tolme"},
                                            {"state", "done"},
                                            {"total", 21},
                                            {"left", 0}});
    write(original, "legacy.txt");
    Voyage g;
    require(g.load(directory / "legacy.txt"), g.message());
    require(g.actions() == 0 && g.location() == "Kessler Drift", "import ticked or arrived early");
    require(g.act("acknowledge", "travel"), g.message());
    require(g.location() == "Tolme Anchorage", "imported readout target");
    std::ifstream source(directory / "legacy.txt");
    Json unchanged;
    source >> unchanged;
    require(unchanged == original, "import changed source");
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    auto bytes = convert.from_bytes(original.dump());
    std::ofstream utf16(directory / "legacy16.txt", std::ios::binary);
    utf16.put(static_cast<char>(0xff));
    utf16.put(static_cast<char>(0xfe));
    for (char16_t c : bytes) {
      utf16.put(static_cast<char>(c & 255));
      utf16.put(static_cast<char>(c >> 8));
    }
    utf16.close();
    require(g.load(directory / "legacy16.txt"), g.message());
    require(task(g, "travel")->at("left") == 0, "UTF16 live work");
    require(g.snapshot()["crew"][0]["name"] == original["crew"][0]["name"], "UTF16 Unicode name");
    original["state"]["ongoing"][0]["type"] = "unimplemented";
    write(original, "unsupported.txt");
    auto before = g.snapshot();
    require(!g.load(directory / "unsupported.txt"), "unknown task silently discarded");
    require(g.snapshot() == before, "failed migration replaced voyage");
  });
  test("reference air penalties and crew anchors match at every pressure level", [] {
    std::ifstream file("cpp/tests/reference_parity.json");
    Json ref;
    file >> ref;
    for (const auto& sample : ref["air"]) {
      auto g = fixture([&](Json& d) {
        d["state"]["filterLife"] = 0;
        d["state"]["airHold"] = sample["hold"];
      });
      require(g.airStage() == sample["stage"] && g.crewWork(7) == sample["work"],
              "air penalty mismatch");
    }
    Voyage g;
    for (const auto& item : ref["anchors"])
      for (const auto& person : g.snapshot()["crew"])
        if (person["id"] == item["id"])
          require(Voyage::anchor(person) == item["anchor"], "crew anchor mismatch");
  });
  test("lost crew cancel shared work, medical reservations and pending orders", [] {
    auto g = fixture([](Json& d) {
      d["crew"][1]["health"] = 0;
      d["state"]["pendingOrders"]["jax"] = {{"rest", true}};
      d["state"]["nativeTasks"].push_back({{"id", "social"},
                                           {"target", ""},
                                           {"left", 3},
                                           {"total", 3},
                                           {"payload", Json::array({"rell", "jax"})}});
    });
    require(g.act("watch"), g.message());
    require(g.snapshot()["crew"].size() == 3, "dead crew retained");
    require(!task(g, "social"), "posthumous shared work retained");
    require(!g.snapshot()["state"]["pendingOrders"].contains("jax"),
            "dead person's staged orders retained");
    require(g.save(directory / "loss-cleanup.json"), "loss save");
    Voyage restored;
    require(restored.load(directory / "loss-cleanup.json"), restored.message());
  });
  test("malformed crew sheets reject loading transactionally", [] {
    Voyage g;
    auto before = g.snapshot();
    auto d = before;
    d["state"]["pendingOrders"]["rell"] = {{"job", 42}};
    write(d, "bad-orders.json");
    require(!g.load(directory / "bad-orders.json"), "bad job type accepted");
    require(g.snapshot() == before, "bad load changed live voyage");
    d = before;
    d["state"]["pendingOrders"]["rell"] = {{"doc", "yes"}};
    write(d, "bad-orders.json");
    require(!g.load(directory / "bad-orders.json"), "bad flag type accepted");
    require(g.snapshot() == before, "bad load changed live voyage");
  });
  test("reference HTA snapshots preserve crew work, incident workers and mapped cooldowns", [] {
    std::ifstream file("cpp/tests/reference_parity.json");
    Json ref;
    file >> ref;
    for (const auto& item : ref["legacy"]) {
      write(item["save"], "reference-import.json");
      Voyage g;
      require(g.load(directory / "reference-import.json"),
              item["name"].get<std::string>() + ": " + g.message());
      require(g.actions() == item["save"]["actions"], "import spent action");
      if (item["name"] == "crew-sheet") {
        require(task(g, "orders-rell") && task(g, "orders-jax"), "per-person work lost");
        watch(g, 3);
        require(task(g, "doctor-rell"), "imported doctor follow-up lost");
      }
      if (item["name"] == "incident-worker") {
        auto t = task(g, "incident-fracture");
        require(t && t->at("by") == "jax", "incident worker lost");
        watch(g, 6);
        require(g.snapshot()["state"]["chronicle"]["incidents"] == 1,
                "imported incident did not resolve");
      }
      if (item["name"] == "known-cooldown")
        require(g.cooldown("nav:travel") == 11, "mapped cooldown lost");
      if (item["name"] == "pre-ship-class")
        require(g.snapshot()["state"]["shipClass"] == "kestrel",
                "pre-class migration chose wrong ship");
      require(g.save(directory / "reference-import-native.json"),
              "imported voyage not serializable");
    }
  });
  std::filesystem::remove_all(directory);
  std::cout << failures << " failing test groups\n";
  return failures ? 1 : 0;
}

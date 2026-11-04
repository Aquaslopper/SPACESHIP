#include <sstream>

#include "voyage.hpp"

namespace spaceship {
namespace {
std::string str(const Json& j, const char* key) { return j.value(key, std::string{}); }
const Json* lookup(const Json& list, const std::string& id) {
  for (const auto& item : list)
    if (str(item, "id") == id) return &item;
  return nullptr;
}
std::string systemOf(const Json& b) { return b.value("system", std::string("sec-kessler")); }
std::string clean(std::string s) {
  // Authored handbook text is plain text except occasional HTML formatting.
  bool tag = false;
  std::string out;
  for (char c : s) {
    if (c == '<')
      tag = true;
    else if (c == '>')
      tag = false;
    else if (!tag)
      out += c;
  }
  return out;
}
}  // namespace
std::vector<Choice> Voyage::choices(const std::string& page) const {
  std::vector<Choice> out;
  const auto& d = catalog();
  const auto& s = save_["state"];
  auto append = [&](const Json& list, const std::string& prefix = "") {
    for (const auto& item : list)
      out.push_back({prefix + str(item, "id"),
                     str(item, "name").empty() ? str(item, "title") : str(item, "name")});
  };
  if (page == "home") append(save_["rooms"]);
  if (page == "navigation" || page == "bridge") {
    const auto* here = lookup(d["bodies"], str(save_["map"], "here"));
    for (const auto& b : d["bodies"])
      if (here && systemOf(b) == systemOf(*here))
        out.push_back({str(b, "id"), "LOCAL / " + str(b, "name")});
    for (const auto& b : d["systems"]) {
      const auto* r = lookup(d["regions"], str(b, "region"));
      out.push_back({str(b, "id"), (r ? str(*r, "name") : "System") + " / " + str(b, "name")});
    }
  } else if (page == "crew" || page == "quarters" || page == "r1")
    append(save_["crew"]);
  else if (page == "cargo")
    append(save_["goods"]);
  else if (page == "science")
    append(save_["survey"]);
  else if (page == "station") {
    append(d["market"], "buy:");
    append(save_["goods"], "sell:");
    append(d["contracts"], "contract:");
    append(d["candidates"], "hire:");
  } else if (page == "simulacron") {
    for (const auto& m : s["sim"]["memories"])
      out.push_back({std::to_string(m["id"].get<int>()), str(m, "kind") + " / " + str(m, "text")});
  } else if (page == "events") {
    for (const auto& ev : s["nativeIncidents"]) {
      const auto* spec = lookup(d["incidents"], str(ev, "id"));
      out.push_back({str(ev, "id"), spec ? str(*spec, "title") : str(ev, "id")});
    }
  } else if (page == "rewards") {
    for (const auto& award : d["awards"]) {
      auto id = str(award, "id");
      out.push_back({id, (s["chronicle"]["claimed"].value(id, false)  ? "CLAIMED / "
                          : s["chronicle"]["earned"].value(id, false) ? "READY / "
                                                                      : "LOCKED / ") +
                             str(award, "title")});
    }
  } else if (page == "guide")
    append(d["wiki"]);
  else if (page == "o2")
    append(save_["life"]["slots"]);
  return out;
}
std::vector<Control> Voyage::controls(const std::string& page, const std::string& selection) const {
  std::vector<Control> out;
  auto button = [&](const std::string& cmd, const std::string& arg, const std::string& label) {
    Voyage trial = *this;
    bool allowed = trial.act(cmd, arg);
    auto shown = label;
    const int remaining = controlCooldown(cmd, arg);
    if (remaining) shown += " [CD: " + std::to_string(remaining) + " clicks]";
    out.push_back({cmd, arg, shown, allowed ? "" : trial.message(), allowed});
  };
  if (page == "home" || page == "guide") {
    const auto& options = save_["state"]["options"];
    button("toggle-music", "",
           std::string("Music: ") + (options.value("music", true) ? "On" : "Off") + " (free)");
    button("toggle-sounds", "",
           std::string("Sound effects: ") + (options.value("soundEffects", true) ? "On" : "Off") +
               " (free)");
    for (auto level : {"0", "25", "50", "75", "100"})
      button("audio-level", level, std::string("Audio level ") + level + "% (free)");
  }
  if (page == "navigation" || page == "bridge") {
    const auto* system = lookup(catalog()["systems"], selection);
    button("acknowledge", "travel", "Acknowledge arrival (free)");
    button("acknowledge", "scan", "Read completed scan (free)");
    button(system ? "jump" : "travel", selection, system ? "Jump to system" : "Start crossing");
    button("scan", selection, "Deep scan");
    button(save_["state"].value("docked", false) ? "undock" : "dock", "",
           save_["state"].value("docked", false) ? "Undock" : "Dock at station");
  }
  if (page == "home" || page == "engine" || page == "bridge" || page == "r10") {
    button("recharge", "", "Recharge energy (+20, 1 cell)");
    button("refuel", "", "Transfer fuel (+15, 1 cell)");
  }
  if (page == "bridge") button("splitfuel", "", "Split fuel cell (+10 energy / +7 fuel)");
  if (page == "engine") {
    button("maintain", "", "Service drive (work = missing condition / 10)");
    {
      button("repair", "", "Patch hull (+25, 1 kit)");
      button("service-shield", "", "Service shields (+25, 1 kit)");
    }
  }
  if (page == "o2") {
    button("install", selection, "Seat selected canister (2 work)");
    button("filters", "", "Start filter bank (+10 work)");
  }
  if (page == "mess") button("cook", "", "Cook to fill rack (1 ration/work)");
  if (page == "cargo") {
    for (auto bay : {"0", "1", "2"})
      button("salvage", bay, std::string("Recover salvage / bay ") + bay + " (3 m3)");
    button("sell", selection, "Sell selected lot");
    button("jettison", selection, "Jettison selected lot");
  }
  if (page == "science") {
    button("package", selection, "Package survey (2 work, +15% value)");
    button("sell-data", "", "Sell survey archive at station");
  }
  if (page == "crew" || page == "quarters" || page == "r1") {
    button("medical", selection, "Treat (1 medkit, 3 work, +25 health)");
    button("talk", selection, "Listen to crew member");
    for (const auto& order : catalog()["orders"])
      button("stage-" + str(order, "id"), selection,
             "Stage / unstage: " + str(order, "label") + " (free)");
    for (const auto& job : catalog()["jobs"])
      button("stage-job=" + str(job, "id"), selection,
             "Stage job: " + str(job, "label") + " (free)");
    button("issue-orders", "", "Issue complete order sheet (1 action)");
    button("clear-orders", "", "Clear staged orders (free)");
  }
  if (page == "station") {
    auto pos = selection.find(':');
    if (pos != std::string::npos)
      button(selection.substr(0, pos), selection.substr(pos + 1),
             "Trade / accept / collect / hire selection");
    for (auto supply : {"fuel", "kit", "med", "oxygen", "rations"})
      button("buy", supply,
             std::string("Buy ") + supply +
                 (std::string(supply) == "fuel"     ? " (45)"
                  : std::string(supply) == "kit"    ? " (70)"
                  : std::string(supply) == "med"    ? " (50)"
                  : std::string(supply) == "oxygen" ? " (25)"
                                                    : " (20 / six)"));
    button("sell-data", "", "Sell survey archive");
    button("ship", "", "Buy SC Tessellate (4000)");
    button("upgrade", "bay", "Cargo refit I (500)");
    button("upgrade", "bay2", "Cargo refit II (900)");
    button("upgrade", "drive", "Efficient drive (900)");
    button("upgrade", "rack", "O2 rack to 15 (400)");
    button("upgrade", "scanner", "Faster sensors (600)");
    button("upgrade", "hull", "Station hull service +50 (120)");
    for (auto kind : {"visit", "scan", "salvage"})
      button("commission", kind, std::string("Commission: ") + kind);
    button("collect", "", "Collect commission");
    button("abandon", "", "Abandon commission");
  }
  if (page == "r6") button("transfer", "energy", "Shields -> energy (20 -> 15)");
  if (page == "r7") button("transfer", "shield", "Energy -> shields (20 -> 15)");
  if (page == "simulacron")
    button("select-memory", selection, "Select / deselect dream memory (free)");
  if (page == "r13") button("social", "", "Shared meal (2 rations, 3 work)");
  if (page == "r4") {
    button("broadcast", "", "Collect local report");
    button("preserve", "", "Shelter archive (permanent)");
    button("sell-signal", "", "Sell signal (permanent)");
  }
  for (auto it = catalog()["projects"].begin(); it != catalog()["projects"].end(); ++it)
    if (str(it.value(), "room") == page) button("project", it.key(), str(it.value(), "label"));
  if (page == "events") button("resolve", selection, "Assign crew to report (6 work)");
  if (page == "rewards") button("claim", selection, "Claim earned reward (free)");
  return out;
}
std::string Voyage::describe(const std::string& page, const std::string& selection) const {
  std::ostringstream o;
  const auto& d = catalog();
  const auto& s = save_["state"];
  if (page == "home") {
    o << "SPACESHIP COMMAND / NATIVE C++\n\nSelect a compartment on the deck plan or in the "
         "list.\nRead, browse and save freely. Only committed actions advance the world.\n\nThis "
         "native port runs without HTA, JavaScript or a web browser.\nIt is still under migration; "
         "see NATIVE-PORT.md for differences.\n";
    o << "\nFirst watch: seat all three canisters in the O2 Filter Room,\nstart the bank, then "
         "cross to Tolme Anchorage.\n";
  }
  if (const auto* room = lookup(save_["rooms"], page)) {
    o << str(*room, "name") << "\n" << str(*room, "systems") << "\n\n";
    if (d["scenes"].contains(page)) o << str(d["scenes"][page], "description") << "\n\n";
  }
  if (page == "navigation" || page == "bridge" || page == "science") {
    const auto* b = lookup(d["bodies"], selection);
    if (!b) b = lookup(d["systems"], selection);
    if (b) {
      o << str(*b, "name") << "\n" << str(*b, "klass") << "\n";
      for (const auto& pair : (*b)["details"])
        o << pair[0].get<std::string>() << ": " << pair[1].get<std::string>() << "\n";
      if (s["scanned"].value(selection, false)) {
        o << "\nDEEP SCAN\n";
        for (const auto& line : (*b)["scan"]) o << line.get<std::string>() << "\n";
      } else
        o << "\nDeep scan not recorded.\n";
    }
    if (!selection.empty()) {
      auto metrics = navigationMetrics(selection);
      o << "\nDistance: " << metrics["dist"] << " work actions / " << metrics["fuel"] << " fuel\n";
    }
    o << "\nCrossings and scans advance only when you commit actions.\nLocal travel uses "
         "distance-based fuel; system jumps use region/system distance.\nNavigation and drive "
         "refits "
         "reduce that cost. Acknowledge the completed crossing to arrive.\n";
  }
  if (page == "crew" || page == "quarters" || page == "r1") {
    const auto* p = lookup(save_["crew"], selection);
    if (p) {
      o << str(*p, "name") << " / " << str(*p, "role") << "\n";
      for (auto k : {"health", "morale", "fatigue", "hunger", "wallet"})
        o << k << ": " << p->value(k, 0) << "   ";
      o << "\nJob: " << str(*p, "job") << " / " << str(*p, "skill")
        << (busy(selection) ? " [BUSY]" : "") << "\n\n";
      o << "Anchor: " << anchor(*p) << "\nDesire: " << desire(*p) << "\n";
      o << "\nSTAGED ORDERS\n"
        << s["pendingOrders"].dump(2) << "\nWork / fees: " << orderQuote().dump() << "\n";
      for (auto k : {"traits", "homeworld", "hobby", "religion"})
        o << k << ": " << str(*p, k) << "\n";
    }
  }
  if (page == "cargo") {
    o << "Cargo " << cargoUsed() << " / " << save_["cargo"]["capacity"] << " m3\n";
    const auto* g = lookup(save_["goods"], selection);
    if (g)
      o << str(*g, "name") << "\n"
        << str(*g, "kind") << " / " << (*g)["vol"] << " m3 / base value " << (*g)["value"] << "\n";
  }
  if (page == "station") {
    o << (s.value("docked", false) ? "STATION SERVICES" : "NOT DOCKED - reach a port to trade")
      << "\n\n";
    auto pos = selection.find(':');
    if (pos != std::string::npos) {
      auto kind = selection.substr(0, pos), id = selection.substr(pos + 1);
      const auto* item = lookup(d[kind == "buy"        ? "market"
                                  : kind == "contract" ? "contracts"
                                                       : "candidates"],
                                id);
      if (kind == "sell") item = lookup(save_["goods"], id);
      if (item) {
        o << item->dump(2) << "\n";
        if (kind == "contract")
          o << "Status: " << s["contracts"].value(id, std::string("not accepted")) << "\n";
      }
    }
    if (!s["commissions"]["active"].is_null())
      o << "\nCOMMISSION\n" << s["commissions"]["active"].dump(2) << "\n";
  }
  if (page == "o2") {
    o << "Canisters " << save_["life"]["canisters"] << " / " << save_["life"]["capacity"] << "\n";
    for (const auto& slot : save_["life"]["slots"])
      o << str(slot, "name") << ": " << str(slot, "state") << "\n";
  }
  if (page == "simulacron" || page == "r4") {
    o << "Insight " << s["sim"]["insight"] << " / 5\nSilicon Marrow: " << str(s["sim"], "signal")
      << "\n\n";
    if (page == "simulacron")
      o << "Selected memory IDs: " << s.value("nativeDreamSelection", Json::array()).dump() << "\n";
    if (page == "simulacron") {
      o << "MEMORY MOSAIC\n";
      for (const auto& m : s["sim"]["memories"])
        if (std::to_string(m["id"].get<int>()) == selection)
          o << str(m, "text")
            << (m.value("processed", false) ? " [consolidated]" : " [unprocessed]") << "\n";
      o << "\nSelect two memories; otherwise the oldest two are used.\n";
    }
  }
  if (page == "events") {
    const auto* ev = lookup(s["nativeIncidents"], selection);
    const auto* spec = lookup(d["incidents"], selection);
    if (ev && spec)
      o << str(*spec, "title") << "\n"
        << str((*spec)["stages"][ev->at("stage").get<int>()], "text") << "\n"
        << ev->at("left") << " actions before escalation\n";
    else
      o << "No active incidents.\n";
  }
  if (page == "rewards") {
    const auto* a = lookup(d["awards"], selection);
    if (a)
      o << str(*a, "title") << "\n"
        << str(*a, "detail") << "\nProgress: " << metric(str(*a, "metric")) << " / "
        << a->at("goal") << "\nReward: " << a->at("reward").dump() << "\n";
  }
  if (page == "guide") {
    o << "NATIVE CONTROLS\nSelect a page, select a target, then choose a command.\nStand watch is "
         "limited by its own 15-action cooldown. Other successful gameplay clicks advance it; "
         "reading and blocked repeats do not.\nSave slots are native-only; HTA "
         "saves are not overwritten.\n\nREFERENCE HANDBOOK (some original UI instructions "
         "differ)\n\n";
    const auto* entry = lookup(d["wiki"], selection);
    if (entry) o << str(*entry, "title") << "\n\n" << clean(str(*entry, "text")) << "\n";
  }
  o << "\nCOMMAND AVAILABILITY\n";
  for (const auto& c : controls(page, selection))
    if (!c.enabled) o << c.label << ": " << c.reason << "\n";
  return o.str();
}
}  // namespace spaceship

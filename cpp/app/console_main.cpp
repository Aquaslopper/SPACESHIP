// ============================================================================
//  console_main.cpp — spaceship_console, the scriptable harness.
//  ---------------------------------------------------------------------------
//  Runs a command script against the headless simulation and prints the
//  canonical trace at every checkpoint. The same script, run through
//  tools/trace-voyage.cjs against the reference edition, must produce the same
//  stream; tests/parity/differential.test.cjs does that comparison.
//
//  Script syntax, one command per line, '#' starts a comment:
//
//    seed <words>         a new voyage on that sky — the player's own seed
//    action [text]        performAction(text)      — spend one action
//    seat <0|1|2>         startInstall(slot)       — carry a canister over
//    bank                 turnOnFilter()           — throw the filter switch
//    job <crewId> <jobId> applyJob()             — scenario setup
//    purse <n>            set STATE.shiplets     — scenario setup
//    stat <id> <n>        set a gauge             — scenario setup
//    target <body>        select a chart body (free)
//    travel               startTravel()          — plot a crossing
//    scan                 startScan()            — run the sensors
//    watch                standWatch()           — one action, nothing else
//    ack <taskId>         press a finished task's button (arrival, scan readout)
//    dock | undock        the station berth
//    layer <key>          switch chart scale (free)
//    sector <region>      browse a region (free)
//    buy <item>           supply id, or a market good id
//    sellgood <lotId> | sellsurvey <recordId> | jettison <lotId>
//    package <recordId>   file a survey record for sale
//    salvage <bay>        recover route salvage while under way
//    contract accept|collect <id>
//    commission accept <kind> | commission collect | commission abandon
//    hire <candidateId> | upgrade <id> | shipyard
//    project <id> | medical <crewId> | social <a> <b>
//    save <file> [utf16]  |  load <file>
//    saveslot <1|2|3> | loadslot <1|2|3>   the HTA's own three slots
//    crew-set <id> <numeric field> <value>   survival boundary test setup
//    survival-set <field> <value>            survival boundary test setup
//    active <tab>         select cooldown scope (UI test setup)
//    progress[-click] <id> <0|1 enabled> <idle label>
//                         inspect/click the real progress-button model
//    trace                print the canonical snapshot
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/Popups.h"
#include "ui/Widgets.h"
#include "core/JsNumber.h"

namespace {

// The save root every script uses. Defaults to the game's own runtime/saves,
// but a test can point it anywhere with SPACESHIP_SAVE_DIR - which is what
// keeps parallel CTest runs from fighting over one Spaceship_save.txt.
std::string SaveRootPath() {
  const char* override = std::getenv("SPACESHIP_SAVE_DIR");
  if (override != nullptr && override[0] != '\0') { return std::string(override); }
  return std::string("runtime/saves");
}

}  // namespace

namespace ship {
// Exposed to the command loop below; the definition lives with the harness's
// other helpers so the default stays in one place.
std::string SaveRoot() { return SaveRootPath(); }
}  // namespace ship

namespace {


std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> parts;
  std::istringstream stream(line);
  std::string word;
  while (stream >> word) { parts.push_back(word); }
  return parts;
}

std::string RestOfLine(const std::string& line, const std::string& firstWord) {
  const std::size_t at = line.find(firstWord);
  if (at == std::string::npos) { return std::string(); }
  std::string rest = line.substr(at + firstWord.size());
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) { rest.erase(rest.begin()); }
  while (!rest.empty() && (rest.back() == '\r' || rest.back() == '\n' || rest.back() == ' ')) {
    rest.pop_back();
  }
  return rest;
}

}  // namespace

int main(int argc, char** argv) {
  std::string scriptPath;
  std::string assetsPath;
  bool referenceFit = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--reference-fit") == 0) {
      referenceFit = true;
    } else if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
      assetsPath = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::printf("usage: spaceship_console [script] [--assets <dir>] [--reference-fit]\n");
      return 0;
    } else {
      scriptPath = argv[i];
    }
  }

  ship::Catalog catalog;
  try {
    catalog = ship::Catalog::LoadFromAssets(
        assetsPath.empty() ? ship::Catalog::DefaultAssetDir() : std::filesystem::path(assetsPath));
  } catch (const std::exception& error) {
    std::fprintf(stderr, "spaceship_console: %s\n", error.what());
    return 1;
  }

  std::istream* input = &std::cin;
  std::ifstream file;
  if (!scriptPath.empty()) {
    file.open(scriptPath);
    if (!file) {
      std::fprintf(stderr, "spaceship_console: cannot open %s\n", scriptPath.c_str());
      return 1;
    }
    input = &file;
  }

  ship::Simulation simulation = referenceFit ? ship::Simulation::ReferenceVoyage(catalog)
                                              : ship::Simulation::NewVoyage(catalog);

  std::string line;
  int lineNumber = 0;
  while (std::getline(*input, line)) {
    ++lineNumber;
    if (line.empty() || line[0] == '#') { continue; }

    const std::vector<std::string> parts = Split(line);
    if (parts.empty()) { continue; }
    const std::string& command = parts[0];

    if (command == "seed" && parts.size() >= 2) {
      // A voyage on the player's own sky: the same words draw the same stars.
      simulation = ship::Simulation::NewVoyage(catalog, parts[1]);
      std::fprintf(stderr, "seed '%s': %zu systems, %zu docks\n", parts[1].c_str(),
                   simulation.World().systems.size(), simulation.World().ports.size());
    } else if (command == "crew-set" && parts.size() == 4) {
      auto* crew = simulation.FindCrew(parts[1]);
      double* field = nullptr;
      if (crew != nullptr) {
        if (parts[2] == "health") field = &crew->health;
        if (parts[2] == "hunger") field = &crew->hunger;
        if (parts[2] == "hungerTick") field = &crew->hungerTick;
        if (parts[2] == "morale") field = &crew->morale;
        if (parts[2] == "fatigue") field = &crew->fatigue;
        if (parts[2] == "jobAt") field = &crew->jobAt;
      }
      if (field == nullptr) { std::fprintf(stderr, "Invalid crew fixture\n"); return 2; }
      *field = std::stod(parts[3]);
    } else if (command == "survival-set" && parts.size() == 3) {
      double* field = nullptr;
      if (parts[1] == "airHold") field = &simulation.MutableState().airHold;
      if (parts[1] == "crewTick") field = &simulation.MutableState().crewTick;
      if (parts[1] == "filterLife") field = &simulation.MutableState().filterLife;
      if (parts[1] == "engineCondition") field = &simulation.MutableState().engineCondition;
      if (parts[1] == "rack") field = &simulation.MutableMess().rack;
      if (parts[1] == "canisters") field = &simulation.MutableLife().canisters;
      if (field == nullptr) { std::fprintf(stderr, "Invalid survival fixture\n"); return 2; }
      *field = std::stod(parts[2]);
    } else if (command == "active" && parts.size() >= 2) {
      simulation.MutableState().activeTab = parts[1];
    } else if ((command == "progress" || command == "progress-click") && parts.size() >= 4) {
      const auto label = RestOfLine(RestOfLine(RestOfLine(line, command), parts[1]), parts[2]);
      const auto button = ship::BuildProgressButton(simulation, parts[1], label, parts[2] == "1");
      if (command == "progress-click") { ship::PressProgressButton(simulation, button); }
      else {
        const char* state = "idle";
        switch (button.state) {
          case ship::ButtonState::Off: state = "off"; break;
          case ship::ButtonState::Running: state = "run"; break;
          case ship::ButtonState::Done: state = "done"; break;
          case ship::ButtonState::Cooldown: state = "cooldown"; break;
          case ship::ButtonState::Idle: break;
        }
        const auto cd = simulation.State().actionCooldowns.find(button.cooldownKey);
        const double remaining = cd == simulation.State().actionCooldowns.end() ? 0 : cd->second.remaining;
        std::printf("PROGRESS %s %s %s %s %s %s\n", button.taskId.c_str(), button.category.c_str(),
            ship::js::NumToString(button.fillPercent).c_str(), state,
            ship::js::NumToString(remaining).c_str(), button.text.c_str());
      }
    } else if (command == "action") {
      std::string detail = RestOfLine(line, "action");
      if (detail.empty()) { detail = "Scripted click"; }
      simulation.PerformAction(detail);
    } else if (command == "maint") {
      simulation.StartMaintenanceSelf();
    } else if (command == "cook") {
      simulation.StartCookingSelf();
    } else if (command == "seat" && parts.size() >= 2) {
      simulation.StartCanisterInstall(std::atoi(parts[1].c_str()));
    } else if (command == "bank") {
      simulation.StartFilterBank();
    } else if (command == "job" && parts.size() >= 3) {
      simulation.ApplyJob(parts[1], parts[2]);
    } else if (command == "stat" && parts.size() >= 3) {
      // Setup only: a parity script may need a voyage in a particular condition
      // (a dead hull, say) without spending a hundred actions to get there.
      ship::Stat* stat = simulation.FindStat(parts[1]);
      if (stat == nullptr) {
        std::fprintf(stderr, "spaceship_console: no such stat '%s'\n", parts[1].c_str());
        return 1;
      }
      stat->value = std::stod(parts[2]);
    } else if (command == "purse" && parts.size() >= 2) {
      simulation.MutableState().shiplets = std::atof(parts[1].c_str());
    } else if (command == "target" && parts.size() >= 2) {
      simulation.SelectTarget(parts[1]);
    } else if (command == "travel") {
      simulation.StartTravel();
    } else if (command == "scan") {
      simulation.StartScan();
    } else if (command == "watch") {
      simulation.StandWatch();
    } else if (command == "ack" && parts.size() >= 2) {
      simulation.Acknowledge(parts[1]);
    } else if (command == "dock") {
      simulation.Dock();
    } else if (command == "undock") {
      simulation.Undock();
    } else if (command == "jobassign" && parts.size() >= 3) {
      simulation.StageJob(parts[1], parts[2]);
    } else if (command == "stage" && parts.size() >= 3) {
      simulation.ToggleOrder(parts[1], parts[2]);
    } else if (command == "orders") {
      simulation.CommitOrders();
    } else if (command == "talk" && parts.size() >= 2) {
      for (ship::CrewMember& crew : simulation.MutableCrew()) {
        if (crew.id != parts[1]) { continue; }
        const std::string said = simulation.AskCrew(crew);
        simulation.LogLine(crew.name + ": \"" + said + "\"");
      }
    } else if (command == "option" && parts.size() >= 2) {
      // (the settings rows never touch the save root; the directory is passed
      // through because the press path wants one)
      // The settings popup's own free paths, so a script drives the same rows
      // a reader would press.
      if (parts[1] == "theme" || parts[1] == "audio") {
        if (parts.size() < 3) { continue; }
        std::string message;
        ship::PressPopupAction(simulation, "settings",
                               parts[1] == "theme" ? "theme" : "audio", parts[2],
                               ship::SaveRoot(), &message);
      } else {
        std::string message;
        ship::PressPopupAction(simulation, "settings", "toggle", parts[1],
                               ship::SaveRoot(), &message);
      }
    } else if (command == "layer" && parts.size() >= 2) {
      simulation.SetLayer(parts[1]);
    } else if (command == "sector" && parts.size() >= 2) {
      simulation.ViewSector(parts[1]);
    } else if (command == "buy" && parts.size() >= 2) {
      if (!simulation.BuySupply(parts[1])) { simulation.BuyGood(parts[1]); }
    } else if (command == "sellgood" && parts.size() >= 2) {
      simulation.SellGood(parts[1]);
    } else if (command == "sellsurvey" && parts.size() >= 2) {
      simulation.SellSurvey(parts[1]);
    } else if (command == "jettison" && parts.size() >= 2) {
      simulation.Jettison(parts[1]);
    } else if (command == "package" && parts.size() >= 2) {
      simulation.PackageSurvey(parts[1]);
    } else if (command == "salvage" && parts.size() >= 2) {
      simulation.StartSalvage(std::atoi(parts[1].c_str()));
    } else if (command == "contract" && parts.size() >= 3) {
      if (parts[1] == "accept") { simulation.AcceptContract(parts[2]); }
      else if (parts[1] == "collect") { simulation.CollectContract(parts[2]); }
    } else if (command == "commission" && parts.size() >= 2) {
      if (parts[1] == "accept" && parts.size() >= 3) { simulation.AcceptCommission(parts[2]); }
      else if (parts[1] == "collect") { simulation.CollectCommission(); }
      else if (parts[1] == "abandon") { simulation.AbandonCommission(); }
    } else if (command == "hire" && parts.size() >= 2) {
      simulation.HireCrew(parts[1]);
    } else if (command == "upgrade" && parts.size() >= 2) {
      simulation.BuyUpgrade(parts[1]);
    } else if (command == "shipyard") {
      simulation.BuyLargerShip();
    } else if (command == "project" && parts.size() >= 2) {
      simulation.StartProject(parts[1]);
    } else if (command == "dream" && parts.size() >= 3) {
      simulation.StartDream(parts[1], parts[2]);
    } else if (command == "signalstate" && parts.size() >= 2) {
      // Setup only, like `purse`: put the story where a script needs it.
      simulation.MutableState().sim.signal = parts[1];
    } else if (command == "broadcast") {
      simulation.BroadcastLocal();
    } else if (command == "signal" && parts.size() >= 2) {
      simulation.FinishSignal(parts[1]);
    } else if (command == "claim" && parts.size() >= 2) {
      simulation.ClaimCommendation(parts[1]);
    } else if (command == "remedy" && parts.size() >= 2) {
      simulation.RemedyIncident(parts[1]);
    } else if (command == "assign" && parts.size() >= 2) {
      simulation.AssignIncidentCrew(parts[1]);
    } else if (command == "medical" && parts.size() >= 2) {
      simulation.StartMedical(parts[1]);
    } else if (command == "social" && parts.size() >= 3) {
      simulation.StartSocial(parts[1], parts[2]);
    } else if (command == "save" && parts.size() >= 2) {
      const bool utf16 = parts.size() >= 3 && parts[2] == "utf16";
      std::string error;
      if (!simulation.SaveToFile(parts[1], utf16, &error)) {
        std::fprintf(stderr, "spaceship_console: %s\n", error.c_str());
        return 1;
      }
    } else if (command == "saveslot" && parts.size() >= 2) {
      std::string error;
      if (!simulation.SaveSlot(std::atoi(parts[1].c_str()), ship::SaveRoot(), &error)) {
        std::fprintf(stderr, "spaceship_console: %s\n", error.c_str());
        return 1;
      }
    } else if (command == "loadslot" && parts.size() >= 2) {
      std::string error;
      if (!simulation.LoadSlot(std::atoi(parts[1].c_str()), ship::SaveRoot(), &error)) {
        std::fprintf(stderr, "spaceship_console: %s\n", error.c_str());
        return 1;
      }
    } else if (command == "load" && parts.size() >= 2) {
      std::string error;
      if (!simulation.LoadFromFile(parts[1], &error)) {
        std::fprintf(stderr, "spaceship_console: %s\n", error.c_str());
        return 1;
      }
    } else if (command == "trace") {
      std::printf("TRACE %s\n", simulation.TraceJson().c_str());
    } else {
      std::fprintf(stderr, "spaceship_console: line %d: unknown command '%s'\n", lineNumber,
                   command.c_str());
      return 2;
    }
  }

  std::printf("END %s\n", simulation.TraceJson().c_str());
  return 0;
}

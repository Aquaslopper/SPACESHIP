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
//    action [text]        performAction(text)      — spend one action
//    seat <0|1|2>         startInstall(slot)       — carry a canister over
//    bank                 turnOnFilter()           — throw the filter switch
//    job <crewId> <jobId> applyJob()             — scenario setup
//    purse <n>            set STATE.shiplets     — scenario setup
//    trace                print the canonical snapshot
// ============================================================================

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"

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
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
      assetsPath = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::printf("usage: spaceship_console [script] [--assets <dir>]\n");
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

  ship::Simulation simulation = ship::Simulation::NewVoyage(catalog);

  std::string line;
  int lineNumber = 0;
  while (std::getline(*input, line)) {
    ++lineNumber;
    if (line.empty() || line[0] == '#') { continue; }

    const std::vector<std::string> parts = Split(line);
    if (parts.empty()) { continue; }
    const std::string& command = parts[0];

    if (command == "action") {
      std::string detail = RestOfLine(line, "action");
      if (detail.empty()) { detail = "Scripted action"; }
      simulation.PerformAction(detail);
    } else if (command == "seat" && parts.size() >= 2) {
      simulation.StartCanisterInstall(std::atoi(parts[1].c_str()));
    } else if (command == "bank") {
      simulation.StartFilterBank();
    } else if (command == "job" && parts.size() >= 3) {
      simulation.ApplyJob(parts[1], parts[2]);
    } else if (command == "purse" && parts.size() >= 2) {
      simulation.MutableState().shiplets = std::atof(parts[1].c_str());
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

#include "game.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {
using spaceship::ActionResult;
using spaceship::CooldownTier;
using spaceship::Game;

void printHelp() {
  std::cout << "Commands (reads are free; committed actions advance work):\n"
            << "  status                  show ship state and cooldowns\n"
            << "  watch                   advance ongoing work (repeatable)\n"
            << "  travel <destination>    start a three-action crossing (undock first)\n"
            << "  scan <target>           start a three-action deep scan\n"
            << "  service                 restore 25 hull using a repair kit (docked)\n"
            << "  dock | undock           berth or depart at a port\n"
            << "  buyfuel                 buy a fuel cell for 45 shiplets (docked)\n"
            << "  splitfuel               convert a fuel cell into energy and fuel\n"
            << "  help | quit\n"
            << "Destinations: Kessler, Tolme Anchorage, Coldwell Relay, Halvan's Reach,\n"
            << "              Echo Shoal, Choir Array, Palimpsest Site\n";
}

void printStatus(const Game& game) {
  const auto& s = game.state();
  std::cout << "\nSC Wren | " << s.location << (s.docked ? " (docked)" : " (in space)")
            << " | actions " << s.actionCount << "\n"
            << "Hull " << s.hull << "%  Drive " << s.engine << "%  Energy " << s.energy
            << "%  Fuel " << s.fuel << "%  Repair kits " << s.repairKits
            << "  Fuel cells " << s.fuelCells << "  Shiplets " << s.shiplets << "\n";
  if (s.travelActions > 0)
    std::cout << "Crossing to " << s.travelTarget << ": " << s.travelActions << " work actions left\n";
  if (s.scanActions > 0)
    std::cout << "Scanning " << s.scanTarget << ": " << s.scanActions << " work actions left\n";
  if (!s.lastScan.empty()) std::cout << "Latest scan: " << s.lastScan << "\n";
  if (!s.actionCooldowns.empty()) {
    std::cout << "Cooldowns (advance only with other committed actions):\n";
    for (const auto& item : s.actionCooldowns) {
      std::cout << "  " << item.first << " — " << item.second.remainingActions
                << " actions (" << Game::tierName(item.second.tier) << ")\n";
    }
  }
  std::cout << "\n";
}

void report(const Game& game, ActionResult result, const std::string& controlId) {
  if (result == ActionResult::committed) {
    std::cout << "Action committed. " << game.state().actionCount << " total.\n";
  } else if (result == ActionResult::blockedCooldown) {
    std::cout << "That button is cooling down for " << game.cooldownRemaining(controlId)
              << " more committed actions.\n";
  } else {
    std::cout << "Action " << spaceship::resultName(result) << "; no clock tick.\n";
  }
}
} // namespace

int main() {
  Game game;
  std::cout << "SPACESHIP COMMAND — native C++ voyage\nType help for commands. Idle time is free.\n";
  printStatus(game);
  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command.empty()) continue;
    std::string argument;
    std::getline(input, argument);
    const auto first = argument.find_first_not_of(" \t");
    if (first != std::string::npos) argument.erase(0, first); else argument.clear();

    if (command == "quit" || command == "exit") break;
    if (command == "help") { printHelp(); continue; }
    if (command == "status") { printStatus(game); continue; }

    ActionResult result = ActionResult::invalid;
    std::string controlId;
    if (command == "watch") { controlId = "nav:stand-watch"; result = game.standWatch(); }
    else if (command == "travel") { controlId = "nav:travel"; result = game.startTravel(argument); }
    else if (command == "scan") { controlId = "nav:scan"; result = game.startScan(argument); }
    else if (command == "service") { controlId = "engine:service-hull"; result = game.serviceHull(); }
    else if (command == "dock") { controlId = "station:dock"; result = game.dock(); }
    else if (command == "undock") { controlId = "station:undock"; result = game.undock(); }
    else if (command == "buyfuel") { controlId = "market:buy-fuel-cell"; result = game.buyFuelCell(); }
    else if (command == "splitfuel") { controlId = "bridge:split-fuel-cell"; result = game.splitFuelCell(); }
    else { std::cout << "Unknown command. Type help.\n"; continue; }

    report(game, result, controlId);
    if (result == ActionResult::committed) printStatus(game);
  }
  return 0;
}

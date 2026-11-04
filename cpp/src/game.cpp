#include "game.hpp"

#include <algorithm>
#include <utility>

namespace spaceship {

int Game::cooldownLength(CooldownTier tier) noexcept {
  switch (tier) {
    case CooldownTier::routine: return 5;
    case CooldownTier::systems: return 10;
    case CooldownTier::space: return 15;
    case CooldownTier::critical: return 20;
  }
  return 5;
}

const char* Game::tierName(CooldownTier tier) noexcept {
  switch (tier) {
    case CooldownTier::routine: return "Routine";
    case CooldownTier::systems: return "Ship systems";
    case CooldownTier::space: return "Space operations";
    case CooldownTier::critical: return "Critical";
  }
  return "Routine";
}

std::vector<std::string> Game::destinations() {
  return {"Kessler", "Tolme Anchorage", "Coldwell Relay", "Halvan's Reach",
          "Echo Shoal", "Choir Array", "Palimpsest Site"};
}

bool Game::isKnownLocation(const std::string& location) {
  const auto all = destinations();
  return std::find(all.begin(), all.end(), location) != all.end();
}

bool Game::isPort(const std::string& location) {
  return location == "Kessler" || location == "Tolme Anchorage" ||
         location == "Coldwell Relay";
}

int Game::cooldownRemaining(const std::string& controlId) const noexcept {
  const auto found = state_.actionCooldowns.find(controlId);
  return found == state_.actionCooldowns.end() ? 0 : found->second.remainingActions;
}

ActionResult Game::performAction(const std::string& controlId, CooldownTier tier) {
  return commit(controlId, tier, [] { return true; });
}

ActionResult Game::standWatch() {
  return performAction("nav:stand-watch", CooldownTier::space);
}

ActionResult Game::startTravel(const std::string& destination) {
  if (state_.gameOver) return ActionResult::gameOver;
  if (state_.docked || state_.travelActions > 0 || state_.fuel < 10 ||
      !isKnownLocation(destination) || destination == state_.location) {
    return ActionResult::invalid;
  }
  const auto result = commit("nav:travel", CooldownTier::space, [this] {
    state_.fuel -= 10;
    return true;
  });
  if (result == ActionResult::committed) {
    // The initiating action ticks old work first; this new crossing starts afterward.
    state_.travelTarget = destination;
    state_.travelActions = 3;
  }
  return result;
}

ActionResult Game::startScan(const std::string& target) {
  if (state_.gameOver) return ActionResult::gameOver;
  if (target.empty() || state_.scanActions > 0 || state_.energy < 2) {
    return ActionResult::invalid;
  }
  const auto result = commit("nav:scan", CooldownTier::space, [this] {
    state_.energy -= 2;
    return true;
  });
  if (result == ActionResult::committed) {
    state_.scanTarget = target;
    state_.scanActions = 3;
  }
  return result;
}

ActionResult Game::serviceHull() {
  if (state_.gameOver) return ActionResult::gameOver;
  if (state_.hull >= 100 || state_.repairKits <= 0 || !state_.docked) {
    return ActionResult::invalid;
  }
  return commit("engine:service-hull", CooldownTier::systems, [this] {
    --state_.repairKits;
    state_.hull = std::min(100, state_.hull + 25);
    return true;
  });
}

ActionResult Game::dock() {
  if (state_.gameOver) return ActionResult::gameOver;
  if (state_.docked || !isPort(state_.location) || state_.travelActions > 0) {
    return ActionResult::invalid;
  }
  return commit("station:dock", CooldownTier::space, [this] {
    state_.docked = true;
    return true;
  });
}

ActionResult Game::undock() {
  if (state_.gameOver) return ActionResult::gameOver;
  if (!state_.docked) return ActionResult::invalid;
  return commit("station:undock", CooldownTier::space, [this] {
    state_.docked = false;
    return true;
  });
}

ActionResult Game::buyFuelCell() {
  if (state_.gameOver) return ActionResult::gameOver;
  if (!state_.docked || state_.shiplets < 45 || state_.fuel >= 100) {
    return ActionResult::invalid;
  }
  return commit("market:buy-fuel-cell", CooldownTier::routine, [this] {
    state_.shiplets -= 45;
    state_.fuel = std::min(100, state_.fuel + 20);
    return true;
  });
}

ActionResult Game::splitFuelCell() {
  if (state_.gameOver) return ActionResult::gameOver;
  if (state_.fuelCells <= 0 || (state_.energy >= 100 && state_.fuel >= 100)) {
    return ActionResult::invalid;
  }
  return commit("bridge:split-fuel-cell", CooldownTier::systems, [this] {
    --state_.fuelCells;
    state_.energy = std::min(100, state_.energy + 10);
    state_.fuel = std::min(100, state_.fuel + 7);
    return true;
  });
}

ActionResult Game::commit(const std::string& controlId, CooldownTier tier,
                          const std::function<bool()>& effect) {
  if (state_.gameOver) return ActionResult::gameOver;
  if (cooldownRemaining(controlId) > 0) return ActionResult::blockedCooldown;
  if (!effect()) return ActionResult::invalid;

  ++state_.actionCount;
  tickActionCooldowns();
  tickWorld();
  // Start the source button's cooldown after its action has ticked all others.
  state_.actionCooldowns[controlId] = {tier, cooldownLength(tier)};
  return ActionResult::committed;
}

void Game::tickActionCooldowns() {
  for (auto it = state_.actionCooldowns.begin(); it != state_.actionCooldowns.end();) {
    if (--it->second.remainingActions <= 0) it = state_.actionCooldowns.erase(it);
    else ++it;
  }
}

void Game::tickWorld() {
  if (state_.actionCount % 4 == 0 && state_.engine > 0) --state_.engine;
  if (state_.actionCount % 20 == 0 && state_.energy > 0) --state_.energy;
  if (state_.energy <= 0) return; // powered work pauses on blackout

  if (state_.travelActions > 0 && --state_.travelActions == 0) {
    state_.location = state_.travelTarget;
    state_.travelTarget.clear();
    state_.docked = isPort(state_.location);
  }
  if (state_.scanActions > 0 && --state_.scanActions == 0) {
    state_.lastScan = state_.scanTarget;
    state_.scanTarget.clear();
  }
}

const char* resultName(ActionResult result) noexcept {
  switch (result) {
    case ActionResult::committed: return "committed";
    case ActionResult::blockedCooldown: return "blocked by cooldown";
    case ActionResult::invalid: return "unavailable";
    case ActionResult::gameOver: return "voyage ended";
  }
  return "unavailable";
}

} // namespace spaceship

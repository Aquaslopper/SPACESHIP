#include "game.hpp"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
  else { std::cout << "PASS: " << message << '\n'; }
}
}

int main() {
  using namespace spaceship;
  Game game;
  check(Game::cooldownLength(CooldownTier::routine) == 5 &&
        Game::cooldownLength(CooldownTier::systems) == 10 &&
        Game::cooldownLength(CooldownTier::space) == 15 &&
        Game::cooldownLength(CooldownTier::critical) == 20,
        "cooldown tiers use 5/10/15/20 action counts");

  check(game.performAction("button:a", CooldownTier::routine) == ActionResult::committed,
        "first button action commits");
  check(game.cooldownRemaining("button:a") == 5,
        "source action starts its cooldown after its own tick");
  check(game.performAction("button:b", CooldownTier::systems) == ActionResult::committed,
        "a different button can commit independently");
  check(game.cooldownRemaining("button:a") == 4 && game.cooldownRemaining("button:b") == 10,
        "another action advances the first cooldown and starts its own");
  const int beforeRepeat = game.state().actionCount;
  check(game.performAction("button:a", CooldownTier::routine) == ActionResult::blockedCooldown,
        "same-button repeat is blocked");
  check(game.state().actionCount == beforeRepeat && game.cooldownRemaining("button:b") == 10,
        "blocked repeats do not spend actions or tick other cooldowns");
  for (int i = 0; i < 4; ++i)
    game.performAction("other:" + std::to_string(i), CooldownTier::routine);
  check(game.cooldownRemaining("button:a") == 0 && game.cooldownRemaining("button:b") == 6,
        "cooldowns expire only after the required other committed actions");

  Game voyage;
  check(voyage.startTravel("Halvan's Reach") == ActionResult::invalid &&
        voyage.state().actionCount == 0, "invalid travel from dock does not tick the clock");
  check(voyage.undock() == ActionResult::committed, "undocking commits one action");
  check(voyage.startTravel("Halvan's Reach") == ActionResult::committed &&
        voyage.state().travelActions == 3, "new crossing starts after its initiating action tick");
  voyage.performAction("other:1", CooldownTier::routine);
  check(voyage.state().travelActions == 2, "other actions advance crossing work");
  voyage.performAction("other:2", CooldownTier::routine);
  voyage.performAction("other:3", CooldownTier::routine);
  check(voyage.state().location == "Halvan's Reach" && voyage.state().travelActions == 0,
        "crossing completes on the action clock");

  Game watch;
  check(watch.standWatch() == ActionResult::committed &&
        watch.cooldownRemaining("nav:stand-watch") == 15,
        "Stand watch is an action with an independent space cooldown");
  const int watchCount = watch.state().actionCount;
  check(watch.standWatch() == ActionResult::blockedCooldown && watch.state().actionCount == watchCount,
        "Stand watch cannot be repeated during its cooldown");

  return failures == 0 ? 0 : 1;
}

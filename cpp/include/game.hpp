#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace spaceship {

enum class CooldownTier { routine, systems, space, critical };
enum class ActionResult { committed, blockedCooldown, invalid, gameOver };

struct Cooldown {
  CooldownTier tier;
  int remainingActions;
};

struct GameState {
  int actionCount = 0;
  int hull = 100;
  int engine = 100;
  int energy = 100;
  int fuel = 60;
  int repairKits = 2;
  int fuelCells = 1;
  int shiplets = 200;
  bool docked = true;
  bool gameOver = false;
  std::string location = "Tolme Anchorage";
  std::string travelTarget;
  int travelActions = 0;
  std::string scanTarget;
  int scanActions = 0;
  std::string lastScan;
  std::map<std::string, Cooldown> actionCooldowns;
};

class Game {
public:
  Game() = default;
  const GameState& state() const noexcept { return state_; }

  // Free reads never call this. Use it for an action with no direct item effect.
  ActionResult performAction(const std::string& controlId, CooldownTier tier);
  ActionResult standWatch();
  ActionResult startTravel(const std::string& destination);
  ActionResult startScan(const std::string& target);
  ActionResult serviceHull();
  ActionResult dock();
  ActionResult undock();
  ActionResult buyFuelCell();
  ActionResult splitFuelCell();

  int cooldownRemaining(const std::string& controlId) const noexcept;
  static int cooldownLength(CooldownTier tier) noexcept;
  static const char* tierName(CooldownTier tier) noexcept;
  static std::vector<std::string> destinations();

private:
  ActionResult commit(const std::string& controlId, CooldownTier tier,
                      const std::function<bool()>& effect);
  void tickActionCooldowns();
  void tickWorld();
  static bool isKnownLocation(const std::string& location);
  static bool isPort(const std::string& location);

  GameState state_;
};

const char* resultName(ActionResult result) noexcept;

} // namespace spaceship

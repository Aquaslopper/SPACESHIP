// ============================================================================
//  Survival.cpp — the ship wearing out and the air running down.
//  ---------------------------------------------------------------------------
//  Both decay with ATTENTION, never with time: the drive loses a percent every
//  four actions spent aboard, and the cabin air spends itself while no filter
//  bank is running. A full bank holds the top air stage for two hundred actions
//  and then hands the player a twenty-action countdown, in five visible steps.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>

namespace ship {

// One maintenance press restores 10% of the drive. The commander
// spends the click through EngineClick; the engineer works inside an existing
// player click and uses the same maintenance amount without advancing time again.
void Simulation::ServiceDrive(const std::string& by) {
  const std::string who = by.empty() ? "The commander" : by;
  state_.engineClickAt = ActionCount();
  LogLine(who + " pressed the maintenance button");
  if (state_.gameOver.empty()) {
    state_.engineCondition = std::min(100.0, state_.engineCondition + 10.0);
  }
}

bool Simulation::EngineClick(const std::string& by) {
  if (!state_.gameOver.empty()) { return false; }
  if (engineClicking_) { return false; }
  // Nothing to maintain is nothing to click: a drive at full condition refuses
  // the press instead of spending a click on an empty job.
  if (state_.engineCondition >= 100.0) { return false; }
  // A maintenance job already in hand owns the drive; the button waits for it.
  if (MaintRunning()) { return false; }

  const std::string who = by.empty() ? "The commander" : by;
  engineClicking_ = true;
  engineClickAtPending_ = true;
  PerformAction(who + " pressed the maintenance button");
  engineClicking_ = false;
  if (engineClickAtPending_) { ServiceDrive(who); }
  engineClickAtPending_ = false;
  return true;
}

void Simulation::WearEngine() {
  state_.wearTick += 1;
  if (state_.wearTick < rules_.engine.wearEvery) { return; }

  state_.wearTick = 0;
  state_.engineCondition -= rules_.engine.wearStep;
  if (state_.engineCondition < 0) { state_.engineCondition = 0; }
}

void Simulation::TickFilters() {
  // --- the bank is running: burn the charge -------------------------------
  if (BankRunning()) {
    state_.filterLife -= rules_.life.decay;
    if (state_.filterLife > 0) { return; }

    state_.filterLife = 0;
    for (CanisterSlot& slot : life_.slots) { slot.state = "empty"; }
    // The room is still full of good air: it holds, then it goes. This action
    // does not also spend a step of it.
    state_.airHold = rules_.air.holdMax;
    LogLine("Filter bank spent. The cabin air will hold for " +
            js::NumToString(rules_.air.holdMax) + " clicks.");
    return;
  }

  // --- no bank: the air in the room is what is left -----------------------
  if (state_.airHold <= 0) { return; }

  const int before = AirStageIndex();
  state_.airHold -= 1;
  if (AirStageIndex() != before) {
    // Only the bottom is announced; the steps above are a colour on a bar.
    if (AirStageIndex() == 0) {
      LogLine("The cabin air has gone foul. The crew are labouring.");
    }
  }
}

bool Simulation::BankRunning() const {
  return state_.filterLife > 0 && StatValue("energy") > 0;
}

bool Simulation::BankReady() const {
  for (const CanisterSlot& slot : life_.slots) {
    if (slot.state != "installed") { return false; }
  }
  return !life_.slots.empty();
}

int Simulation::AirStageIndex() const {
  if (BankRunning()) { return 4; }
  int stage = static_cast<int>(js::Ceil(state_.airHold / rules_.air.step));
  if (stage > 4) { stage = 4; }
  if (stage < 0) { stage = 0; }
  return stage;
}

const struct AirStage& Simulation::AirSpec() const {
  const std::vector<AirStage>& stages = rules_.air.stages;
  const int index = AirStageIndex();
  if (stages.empty()) {
    static const AirStage fallback{"OPTIMAL", "#7f9161", 1.0, 0.0, 0.0};
    return fallback;
  }
  const std::size_t clamped = static_cast<std::size_t>(
      std::max(0, std::min(index, static_cast<int>(stages.size()) - 1)));
  return stages[clamped];
}

double Simulation::AirPercent() const {
  if (BankRunning()) { return 100.0; }
  const double percent = js::Round((state_.airHold / rules_.air.holdMax) * 100.0);
  return percent < 0 ? 0.0 : percent;
}

double Simulation::CrewSlow() const { return AirSpec().slow; }

double Simulation::CrewCost(double n) const {
  double cost = js::Ceil(n * CrewSlow());
  if (cost < 1) { cost = 1; }
  return cost;
}

void Simulation::RunBank(const std::string& line) {
  for (CanisterSlot& slot : life_.slots) { slot.state = "active"; }
  state_.filterLife = rules_.life.lifeMax;
  state_.chronicle.banks += 1;

  ProAction(10);
  CheckCommendations();
  LogLine(line);
}

bool Simulation::ServiceSystem(const std::string& statId) {
  if (!state_.gameOver.empty()) { return false; }
  Stat* stat = FindStat(statId);
  if (stat == nullptr || stat->value >= 100.0) { return false; }

  std::string label;
  bool useKit = false;
  double delta = 0.0;
  if (statId == "hull") {
    label = "Patch hull (+25%)";
    useKit = true;
    delta = 25.0;
  } else if (statId == "shield") {
    label = "Service shields (+25%)";
    useKit = true;
    delta = 25.0;
  } else if (statId == "energy") {
    label = "Recharge energy (+20%)";
    useKit = false;
    delta = 20.0;
  } else if (statId == "fuel") {
    label = "Transfer fuel (+15%)";
    useKit = false;
    delta = 15.0;
  } else {
    return false;
  }

  double& stock = useKit ? state_.repairKits : state_.fuelCells;
  if (stock <= 0.0) { return false; }
  stock -= 1.0;
  ClampStat(statId, delta);
  PerformAction(label);
  return true;
}

}  // namespace ship

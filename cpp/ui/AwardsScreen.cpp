// ============================================================================
//  AwardsScreen.cpp — every commendation, its condition and its pay.
// ============================================================================
#include "ui/AwardsScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

double Owned(const std::map<std::string, bool>& map) {
  double count = 0;
  for (const auto& entry : map) {
    if (entry.second) { count += 1; }
  }
  return count;
}

}  // namespace

AwardsScreenModel BuildAwardsScreen(const Simulation& sim) {
  AwardsScreenModel model;
  const GameState& state = sim.State();

  model.head.kind = "LOGBOOK";
  model.head.title = "Commendations & Rewards";
  model.head.sub =
      "Progress is earned through play; claiming a reward is free and can be done once.";

  model.summary.id = "captain-log";
  model.summary.title = "Captain's log";
  model.summary.Field("Rank", sim.CaptainRank() + " (cosmetic)");
  model.summary.Field("Commendations", Num(sim.EarnedCommendations()) + " / " +
                                           Num(static_cast<double>(sim.World().commendations.size())));
  model.summary.Field("Unclaimed rewards", Num(sim.ReadyCommendations()));
  model.summary.Field("Systems / regions visited",
                      Num(Owned(state.chronicle.systems)) + " / " +
                          Num(Owned(state.chronicle.regions)));
  model.summary.Field("Unique deep scans", Num(sim.ChronicleValueFor("scans")));
  model.summary.Field("Completed commissions", Num(state.commissions.completed));

  for (const Commendation& award : sim.World().commendations) {
    const double value = sim.ChronicleValueFor(award.metric);
    const double amount = std::min(award.goal, value);
    const bool earned = [&] {
      auto it = state.chronicle.earned.find(award.id);
      return it != state.chronicle.earned.end() && it->second;
    }();
    const bool claimed = [&] {
      auto it = state.chronicle.claimed.find(award.id);
      return it != state.chronicle.claimed.end() && it->second;
    }();

    ActionRowView row;
    row.id = "claim:" + award.id;
    std::string status;
    if (claimed) {
      status = "Claimed";
    } else if (earned) {
      status = "Ready to claim";
    } else {
      status = Num(amount) + " / " + Num(award.goal);
    }
    row.label = award.title + " - " + status;
    row.note = award.detail + " Pays " + sim.RewardTextOf(award) + ".";
    row.cost = claimed ? "done" : "free";
    row.enabled = earned && !claimed && state.gameOver.empty();
    model.rows.push_back(row);
  }
  return model;
}

namespace {
bool PressAwardsActionRaw(Simulation& sim, const std::string& action, const std::string& arg) {
  (void)arg;
  if (action.compare(0, 6, "claim:") == 0) { return sim.ClaimCommendation(action.substr(6)); }
  return false;
}

}  // namespace

// ============================================================================
//  The press, with the anti-spam lock around it (Phase 9).
//  ---------------------------------------------------------------------------
//  One control is one control. The first press spends the action and starts the
//  tier's lock in ACTION_COOLDOWN_RULES; a second press of the same button
//  before another action resolves is refused, whether it comes from the window
//  or from this harness. Free reads spend nothing and therefore lock nothing.
// ============================================================================
bool PressAwardsAction(Simulation& sim, const std::string& action, const std::string& arg) {
  return CommitActionButton(
      sim, ActionCooldownKey(sim, "press:awards:" + action + ":" + arg),
      ActionCooldownCategory(action + " " + arg),
      [&]() { return PressAwardsActionRaw(sim, action, arg); });
}

}  // namespace ship

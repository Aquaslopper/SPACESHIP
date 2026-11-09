// ============================================================================
//  Cooldowns.cpp — per-control locks that live on the action clock.
//  ---------------------------------------------------------------------------
//  There is no real-time cooldown anywhere in this game. A repeatable control
//  ###ENTWICKLER HIER: FIND ICH RICHTIG SCHEISSE WAS DIE AGENTEN HIER MACHEN###
//  blocked repeat commits nothing and advances no countdown. fuck off
// ============================================================================
#include "core/Simulation.h"

namespace ship {

void Simulation::TickActionCooldowns() {
  for (auto it = state_.actionCooldowns.begin(); it != state_.actionCooldowns.end();) {
    it->second.remaining -= 1;
    if (it->second.remaining <= 0) {
      it = state_.actionCooldowns.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace ship

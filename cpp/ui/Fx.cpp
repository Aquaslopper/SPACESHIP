// ============================================================================
//  Fx.cpp — the player's action mark.
// ============================================================================
#include "ui/Fx.h"

#include <algorithm>
#include <cmath>

namespace ship {

bool SpawnMark(std::vector<ActionMark>& marks, float x, float y, double now,
               bool reducedMotion) {
  if (reducedMotion) { return false; }
  const float half = std::floor(FxTiming::kMarkSize / 2.0f);
  marks.push_back(ActionMark{x - half, y - half, now});
  while (static_cast<int>(marks.size()) > FxTiming::kMaxMarks) {
    marks.erase(marks.begin());
  }
  return true;
}

void UpdateMarks(std::vector<ActionMark>& marks, double now) {
  const double life = FxTiming::kMarkHold + FxTiming::kMarkDuration;
  marks.erase(std::remove_if(marks.begin(), marks.end(),
                             [now, life](const ActionMark& mark) {
                               return now - mark.born >= life;
                             }),
              marks.end());
}

MarkStyle MarkAt(const ActionMark& mark, double now) {
  MarkStyle style;
  const double elapsed = now - mark.born;
  if (elapsed < 0.0) { return style; }
  const double t = (elapsed - FxTiming::kMarkHold) / FxTiming::kMarkDuration;
  if (t >= 1.0) { return style; }
  if (t < 0.0) {
    // Phase 1: land and stay put, so the player registers where their hand was.
    style.top = mark.y;
    style.opacity = 1.0f;
    style.alive = true;
    return style;
  }
  const double ease = t * t * (3.0 - 2.0 * t);
  style.top = static_cast<float>(mark.y - FxTiming::kMarkRise * ease);
  style.opacity = static_cast<float>(1.0 - t);
  style.alive = true;
  return style;
}

}  // namespace ship

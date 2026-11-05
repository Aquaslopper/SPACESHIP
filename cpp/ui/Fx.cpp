// ============================================================================
//  Fx.cpp — half a pixel at a time, and a cross that rises.
// ============================================================================
#include "ui/Fx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ship {

std::uint32_t FxRng::Next() {
  // xorshift32: four instructions, no allocation, and reproducible.
  std::uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

float FxRng::Unit() {
  return static_cast<float>(Next() >> 8) / 16777216.0f;
}

int FxRng::Range(int span) {
  if (span <= 0) { return 0; }
  return static_cast<int>(Unit() * static_cast<float>(span));
}

float DotSpot(const Area& box, bool vertical, bool compact, FxRng& rng) {
  float lo = 0.0f;
  float span = 1.0f;
  if (!vertical) {
    lo = box.x + 22.0f;
    span = box.w - 54.0f;
  } else if (compact && box.h >= 50.0f) {
    lo = box.y + 34.0f;
    span = box.h - 52.0f;
  } else if (box.h > 70.0f) {
    lo = box.y + 44.0f;
    span = box.h - 66.0f;
  } else if (box.h > 34.0f) {
    lo = box.y + 16.0f;
    span = box.h - 34.0f;
  } else {
    // The corridor is 26px of headroom - centre the dot in it rather than
    // hanging it out of the bottom.
    lo = box.y + std::round((box.h - 15.0f) / 2.0f);
    span = 1.0f;
  }
  if (span < 1.0f) { span = 1.0f; }
  return lo + rng.Unit() * span;
}

void PickTarget(DriftDot& dot, const Area& box, bool compact, FxRng& rng) {
  dot.tx = DotSpot(box, false, compact, rng);
  dot.ty = DotSpot(box, true, compact, rng);
}

void DriftStep(DriftDot& dot, const Area& box, bool compact, FxRng& rng) {
  if (dot.wait > 0) {
    --dot.wait;
    return;
  }
  const float dx = dot.tx - dot.x;
  const float dy = dot.ty - dot.y;
  const float dist = std::sqrt(dx * dx + dy * dy);
  if (dist < FxTiming::kArriveAt) {
    dot.wait = FxTiming::kWaitMin + rng.Range(FxTiming::kWaitSpan);
    PickTarget(dot, box, compact, rng);
    return;
  }
  dot.x += (dx / dist) * FxTiming::kDriftStep;
  dot.y += (dy / dist) * FxTiming::kDriftStep;
}

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

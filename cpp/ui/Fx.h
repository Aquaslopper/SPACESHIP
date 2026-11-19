#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/Layout.h"

namespace ship {

struct FxTiming {
  static constexpr int kMarkSize = 11;     // FX.size
  static constexpr int kMarkHold = 220;    // FX.hold
  static constexpr int kMarkRise = 26;     // FX.rise
  static constexpr int kMarkDuration = 950;  // FX.duration
  static constexpr int kMarkFrame = 16;      // FX.frame
  static constexpr int kMaxMarks = 5;        // FX.maxMarks
};

// One action mark. `born` is the spawn time in milliseconds.
struct ActionMark {
  float x = 0.0f;
  float y = 0.0f;
  double born = 0.0;
};

// spawnActionMark(): returns false when motion is reduced, so the caller can
// skip the whole effect rather than draw a mark that never moves.
bool SpawnMark(std::vector<ActionMark>& marks, float x, float y, double now,
               bool reducedMotion);

// Retires marks older than hold + duration; the oldest is retired early rather
// than letting a hammered mouse stack a dozen of them.
void UpdateMarks(std::vector<ActionMark>& marks, double now);

struct MarkStyle {
  float top = 0.0f;
  float opacity = 1.0f;
  bool alive = false;
};

// The reference's two-phase animation, as arithmetic: still for the hold, then
// an ease-in-out rise with a linear fade.
MarkStyle MarkAt(const ActionMark& mark, double now);

}  // namespace ship

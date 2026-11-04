// ============================================================================
//  Fx.h — the ship's breathing, and the player's hand.
//  ---------------------------------------------------------------------------
//  Two cosmetic effects live here and nothing else may:
//
//    dot drift      a crew dot moves inside its own room, so the diagram never
//                   contradicts the crew window. 55 ms a tick, half a pixel a
//                   tick, a 20..109 tick pause on arrival.
//    action marks   every click that costs something leaves a small cross where
//                   the hand was: 220 ms still, then 26 px of slow rise while
//                   fading over 950 ms, at most five alive.
//
//  Both are gated on STATE.reducedMotion, and neither touches a number the
//  voyage is scored on. Fx holds no Simulation reference at all: it is handed
//  anchors and time, so a headless test can run a thousand ticks and prove the
//  voyage's trace is unchanged.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/Layout.h"

namespace ship {

struct FxTiming {
  static constexpr int kDriftMs = 55;      // driftTick()'s interval
  static constexpr float kDriftStep = 0.5f;  // pixels per tick
  static constexpr float kArriveAt = 1.2f;   // closer than this = arrived
  static constexpr int kWaitMin = 20;
  static constexpr int kWaitSpan = 90;
  static constexpr int kMarkSize = 11;     // FX.size
  static constexpr int kMarkHold = 220;    // FX.hold
  static constexpr int kMarkRise = 26;     // FX.rise
  static constexpr int kMarkDuration = 950;  // FX.duration
  static constexpr int kMarkFrame = 16;      // FX.frame
  static constexpr int kMaxMarks = 5;        // FX.maxMarks
};

// A tiny deterministic generator. The reference uses Math.random(); the port
// uses a seeded one so a test can replay a drift exactly. Never used for a game
// number - only for where a dot stands and how long it waits.
struct FxRng {
  std::uint32_t state = 0x9e3779b9u;
  std::uint32_t Next();
  float Unit();               // [0,1)
  int Range(int span);        // floor(Unit() * span)
};

// One drifting hand. `roomId` is the compartment it is confined to.
struct DriftDot {
  std::string id;
  std::string roomId;
  float x = 0.0f;
  float y = 0.0f;
  float tx = 0.0f;
  float ty = 0.0f;
  int wait = 0;
};

// dotSpot(): where inside a box a dot may stand, on one axis.
float DotSpot(const Area& box, bool vertical, bool compact, FxRng& rng);

// pickTarget(): aim at the room the hand is in now (re-read every time, so a
// hand laid off mid-voyage walks out of the workshop on the next drift).
void PickTarget(DriftDot& dot, const Area& box, bool compact, FxRng& rng);

// driftTick(): one tick for one dot. Arrived dots pause and re-aim.
void DriftStep(DriftDot& dot, const Area& box, bool compact, FxRng& rng);

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

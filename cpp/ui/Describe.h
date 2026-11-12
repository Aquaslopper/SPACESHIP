// ============================================================================
//  Describe.h — what a thing IS, in words.
//  ---------------------------------------------------------------------------
//  The reference's briefing text lives in one function, describeTarget(), and
//  it is the only place the game explains itself: every left-click in the
//  cockpit, every task window header and the GONE guard that keeps a stale tab
//  from taking the screen down all come from here. The strings are therefore
//  part of the contract, not decoration - so they live in a pure function that
//  takes the voyage and a (kind, id) pair, and returns data. CTest can compare
//  them word for word without a window.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"

namespace ship {

struct DescribeRow {
  std::string key;
  std::string value;
};

// An action descriptor. `live` is false for every briefing row in the reference
// on purpose: a briefing says what the window holds, the window does the work.
struct DescribeAct {
  std::string label;
  std::string cost;
  bool live = false;
};

struct Description {
  std::string kind;
  std::string id;
  std::string kindLabel;
  std::string title;
  std::string sub;
  std::vector<DescribeRow> rows;
  std::vector<DescribeAct> acts;
  bool gone = false;
};

// targetEntity(): does the record a tab is watching still exist? `nav` and the
// two reference windows carry no record and always answer true.
bool DescribeTargetExists(const Simulation& sim, const std::string& kind,
                          const std::string& id);

// describeTarget(). Never throws, never mutates, always returns a full record -
// including the GONE shape when the watched thing has left the ship.
Description DescribeTarget(const Simulation& sim, const std::string& kind,
                           const std::string& id);

// The one place that knows what "acts at" means for a crew member's job.
std::string JobThreshold(const Simulation& sim, const CrewMember& crew);

}  // namespace ship

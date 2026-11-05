// ============================================================================
//  IdleScreen.h — Home: the ship at rest, and everything readable from it.
//  ---------------------------------------------------------------------------
//  Section E's build order is the model's order: stat rail, stage, stage note,
//  nav strip, dock strip (only while docked), info rail, feed. The feed is the
//  only part that is measured rather than authored - it takes what the rest of
//  the column leaves, inside the 62..132 band - so the layout is asked for the
//  feed height and this file lays the column out around it.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Fx.h"
#include "ui/Layout.h"
#include "ui/ShipDiagram.h"
#include "ui/Theme.h"

namespace ship {

// One cell of either rail. The reference floors the share to 1/100 of a percent
// so a browser's rounding can never push the last cell down.
struct RailCell {
  std::string id;
  std::string label;
  std::string value;
  std::string unit;
  double reading = 0.0;
  std::string colour;
  float widthPercent = 0.0f;
  Area box;
};

struct Strip {
  std::string id;
  std::string label;
  std::string main;
  std::string right = "Left-click to read - right-click opens it";
  Area box;
};

struct IdleScreenModel {
  Area content;
  std::vector<RailCell> stats;
  std::vector<RailCell> infos;
  Area stageArea;
  std::string stageNote;
  Strip nav;
  bool docked = false;
  Strip dock;
  DiagramModel diagram;
  std::vector<std::string> feed;
  std::string feedHead = "Activity Feed";
  Area feedBox;
  float feedHeight = Metrics::kFeedMin;
};

// The four readouts across the top: one cell per STATS row, equal shares.
std::vector<RailCell> StatRail(const Simulation& sim, const Area& rail);

// The lower rail: location, contacts and life support, from INFOS.
std::vector<RailCell> InfoRail(const Simulation& sim, const Area& rail);

// buildNavStrip(): "Holding at <body> - no course plotted", or the crossing's
// own label while one is running or waiting to be acknowledged.
Strip NavStrip(const Simulation& sim, const Area& box);

// dockStrip(): only drawn while the ship is at a berth.
Strip DockStrip(const Simulation& sim, const Area& box);

// The whole screen. `layout` is what Layout.cpp measured; `others` is the
// height the column needs before the feed, which the shell knows and the tests
// can state outright.
IdleScreenModel BuildIdleScreen(const Simulation& sim, const ShellLayout& layout,
                                const Theme& theme, bool hideCrewLabels,
                                float othersHeight, FxRng& rng);

// Production layout measures its complete built content instead of accepting
// a guessed non-feed allowance. The explicit-height overload remains available
// for callers that already measured their surrounding host.
IdleScreenModel BuildIdleScreen(const Simulation& sim, const ShellLayout& layout,
                                const Theme& theme, bool hideCrewLabels, FxRng& rng);

}  // namespace ship

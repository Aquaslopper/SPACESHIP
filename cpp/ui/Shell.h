// ============================================================================
//  Shell.h — the frame the game is played inside.
//  ---------------------------------------------------------------------------
//  The reference's chrome is four pieces of furniture and one rule:
//
//    taskbar   Home, plus one tab per open task, plus < > and the home slot
//    sidebar   five fold blocks that cost nothing to open, and one strip that
//              is always visible because it is the glance
//    foot      four ship functions, Wiki and Rewards
//    status    one line, visible from every screen
//
//  The rule is that opening a tab is free and drawing the sidebar is free -
//  nothing in this file spends an action, and nothing here is saved except the
//  state the reference itself saves (tabs, activeTab, freshTabs, spOpen).
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Layout.h"

namespace ship {

// One tab, as the taskbar draws it.
struct TabView {
  std::string key;        // "home", or "<kind>:<id>"
  std::string title;
  std::string kindLabel;
  bool home = false;
  bool active = false;
  bool fresh = false;     // the cream pulse, cleared the moment it is drawn
  bool closable = false;
};

// §H: the tab system. `openTask` never focuses unless asked; `switchTab`
// re-describes the window it is about to build; `closeTab` falls back home.
std::string TaskKey(const std::string& kind, const std::string& id);
const Tab* FindTab(const GameState& state, const std::string& key);
bool OpenTask(Simulation& sim, const std::string& kind, const std::string& id,
              bool focus);
bool SwitchTab(Simulation& sim, const std::string& key);
bool CloseTab(Simulation& sim, const std::string& key);
bool RefreshTab(Simulation& sim, const std::string& key);

// The whole bar, Home first. Drawing a fresh tab consumes its pulse, exactly
// as makeTab does - so the caller should keep this call once per paint.
std::vector<TabView> TabBar(Simulation& sim);
std::string HomeTabTitle(const Simulation& sim);

// §L: the one line visible from every screen.
std::string StatusText(const Simulation& sim);

// §D: the activity feed, newest first, capped at what the reference shows.
std::vector<std::string> FeedLinesNewestFirst(const Simulation& sim,
                                              std::size_t limit = 200);

// §J: the sidebar. A block is a title, a fold state and rows; the indicator
// strip is its own block and never folds.
struct SideRow {
  std::string key;
  std::string value;
  bool dim = true;      // sp-val-dim vs sp-val
  bool num = false;
  std::string kind;     // "pair", "long", "event", "dim"
  std::string id;       // event id, for the Open button
};

struct SideBlock {
  std::string key;      // vessel / resources / crew / contracts / events
  std::string title;
  bool open = false;
  bool indicators = false;
  std::string empty;    // "No active contracts." / "Nothing reported."
  std::vector<SideRow> rows;
};

std::vector<SideBlock> SideBlocks(const Simulation& sim);

// indicatorLines(): the engine warning, the air, every running task, every
// report. Leftmost is always the thing most likely to hurt.
std::vector<IndicatorLine> IndicatorLines(const Simulation& sim);

// The pinned foot.
struct FootButton {
  std::string key;      // manifest / log / files / settings
  std::string title;    // the tooltip; the only label they get
};
std::vector<FootButton> FootButtons();
std::string RewardsLabel(const Simulation& sim);
int ReadyCommendations(const Simulation& sim);
std::string CaptainRank(const Simulation& sim);

}  // namespace ship

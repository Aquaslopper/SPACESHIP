// ============================================================================
//  WikiScreen.h — the ship's handbook (Section N).
//  ---------------------------------------------------------------------------
//  Eighteen entries, grouped by subject, with a search box that
//  matches title, group and body the way `wikiMatches` does. Reading and
//  navigating are free: nothing here can reach PerformAction.
//
//  The one addition to the reference is the spoiler gate: an entry marked as a
//  spoiler keeps its body behind a reveal row, so a first watch is not spoiled
//  by accident. The reveal itself is free and local.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"
#include "ui/WikiView.h"

namespace ship {

// WIKI_VIEW lives in WikiView.h: the router needs it, the screen fills it.

// wikiMatches(): case-insensitive substring over title, group and text.
bool WikiMatches(const WikiEntry& entry, const std::string& query);

// The index: matches in catalogue order, plus a group heading before each run.
struct WikiIndexEntry {
  const WikiEntry* entry = nullptr;
  std::string group;      // set only when the group changes
  bool selected = false;
};

struct WikiModel {
  TaskHead head;
  WikiView view;
  std::vector<WikiIndexEntry> index;
  const WikiEntry* selected = nullptr;
  std::string group;
  std::string title;
  std::string body;        // withheld behind the gate when spoilers are hidden
  std::string emptyNote;   // "No matching entries."
  std::vector<ActionRowView> rows;   // the link, then the gate
  bool gated = false;      // the selection is a spoiler and has not been revealed
};

WikiModel BuildWikiScreen(const Simulation& sim, const WikiView& view);

// Free actions: "entry:<id>" selects, "search:<text>" filters, "reveal" opens
// the gate, "link" opens the entry's tab. Returns true when the view changed.
bool PressWikiAction(Simulation& sim, const std::string& action, const std::string& arg,
                     WikiView* view);

}  // namespace ship

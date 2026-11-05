// ============================================================================
//  WikiScreen.cpp — the handbook, its index and its search.
// ============================================================================
#include "ui/WikiScreen.h"

#include "core/JsNumber.h"
#include "ui/Shell.h"

#include <algorithm>

namespace ship {

namespace {

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

const WikiEntry* Find(const Simulation& sim, const std::string& id) {
  for (const WikiEntry& entry : sim.World().wiki) {
    if (entry.id == id) { return &entry; }
  }
  return nullptr;
}

}  // namespace

bool WikiMatches(const WikiEntry& entry, const std::string& query) {
  const std::string needle = Lower(query);
  const std::string haystack = Lower(entry.title + " " + entry.group + " " + entry.text);
  return haystack.find(needle) != std::string::npos;
}

WikiModel BuildWikiScreen(const Simulation& sim, const WikiView& view) {
  WikiModel model;
  model.view = view;
  model.head.kind = "WIKI";
  model.head.title = "Ship's Handbook";
  model.head.sub = "Search the offline handbook. Reading and navigation are free.";

  std::string group;
  const WikiEntry* selected = nullptr;
  for (const WikiEntry& entry : sim.World().wiki) {
    if (!WikiMatches(entry, view.query)) { continue; }
    WikiIndexEntry row;
    row.entry = &entry;
    if (entry.group != group) {
      group = entry.group;
      row.group = group;
    }
    model.index.push_back(row);
  }

  // The selection is resolved against the matches: a search that filters the
  // selected entry away falls back to the first match, as `draw()` does.
  if (!model.index.empty()) {
    selected = model.index.front().entry;
    for (const WikiIndexEntry& row : model.index) {
      if (row.entry->id == view.selected) { selected = row.entry; }
    }
  }
  if (selected == nullptr) {
    model.emptyNote = "No matching entries.";
    return model;
  }

  model.selected = selected;
  model.group = selected->group;
  model.title = selected->title;
  model.gated = selected->spoiler && !view.revealed;
  model.body = model.gated ? "Spoilers hidden. Reveal the entry to read the story guide."
                           : selected->text;
  for (WikiIndexEntry& row : model.index) { row.selected = row.entry == selected; }

  if (model.gated) {
    ActionRowView reveal;
    reveal.id = "reveal";
    reveal.label = "Reveal the spoiler entry";
    reveal.cost = "free";
    reveal.enabled = true;
    model.rows.push_back(reveal);
  }
  if (!selected->link.empty()) {
    ActionRowView link;
    link.id = "link";
    link.label = selected->linkLabel;
    link.cost = "free";
    link.enabled = true;
    model.rows.push_back(link);
  }
  return model;
}

bool PressWikiAction(Simulation& sim, const std::string& action, const std::string& arg,
                     WikiView* view) {
  if (view == nullptr) { return false; }
  if (action == "entry") {
    if (Find(sim, arg) == nullptr) { return false; }
    view->selected = arg;
    return true;
  }
  if (action == "search") {
    view->query = arg;
    return true;
  }
  if (action == "reveal") {
    view->revealed = true;
    return true;
  }
  if (action == "hide-spoilers") {
    view->revealed = false;
    return true;
  }
  if (action == "link") {
    const WikiEntry* entry = Find(sim, view->selected);
    if (entry == nullptr || entry->link.empty()) { return false; }
    // Opening the named tab is free, like every other read in the reference.
    if (entry->link == "nav") { return OpenTask(sim, "nav", "main", true); }
    return OpenTask(sim, "room", entry->link, true);
  }
  return false;
}

}  // namespace ship

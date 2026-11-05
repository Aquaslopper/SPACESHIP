// ============================================================================
//  TitleScreen.cpp — the card, the dialog and the overlay a loss leaves.
// ============================================================================
#include "ui/TitleScreen.h"

#include "core/JsNumber.h"
#include "ui/Shell.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

const char* kGuide =
    "Start aboard a compact Wren courier. Every committed action advances the voyage; "
    "reading, opening tabs and plotting targets are free.";

const char* kGuideMore =
    "Right-click Navigation and travel to Tolme Anchorage. Stand watch there to advance "
    "work when needed. Seat three O2 canisters and start the filter bank. At a station, "
    "use the Docked indicator to trade, accept work and visit the shipyard.";

const char* kGuideShip =
    "The larger Kestrel ship costs 4,000 shiplets. Your crew, cargo, equipment, tasks and "
    "records transfer when you buy it. Open Wiki on the ship for the full handbook.";

double Owned(const std::map<std::string, bool>& map) {
  double count = 0;
  for (const auto& entry : map) {
    if (entry.second) { count += 1; }
  }
  return count;
}

}  // namespace

std::string TitleButtonLabel(bool fromVoyage) { return fromVoyage ? "RESUME" : "EXIT"; }

bool AnySlotWritten(const Simulation& sim, const std::string& saveDir) {
  for (int slot = 1; slot <= 3; ++slot) {
    bool written = false;
    SaveSlotSummary(sim, slot, saveDir, &written);
    if (written) { return true; }
  }
  return false;
}

TitleModel BuildTitleScreen(const Simulation& sim, bool fromVoyage, TitlePanel panel,
                            const std::string& saveDir, int slot,
                            const std::string& message) {
  TitleModel model;
  model.fromVoyage = fromVoyage;
  model.panel = panel;
  model.slot = slot;
  model.message = message;
  model.artPath = "assets/art/title.jpg";

  // The five commands, in the reference's order. NEW VOYAGE and LOAD are the
  // only two that can change the voyage, and both ask first while a voyage is
  // live (the caller owns the question; the press owns the answer).
  struct Command {
    const char* id;
    std::string label;
    bool enabled;
  };
  const std::vector<Command> commands = {
      {"new-voyage", "NEW VOYAGE", true},
      {"load", "LOAD VOYAGE", AnySlotWritten(sim, saveDir) || !sim.State().tabs.empty()},
      {"options", "OPTIONS", true},
      {"guide", "HOW TO PLAY", true},
      {"resume", TitleButtonLabel(fromVoyage), true},
  };
  for (const Command& command : commands) {
    ActionRowView row;
    row.id = command.id;
    row.label = command.label;
    row.cost = "free";
    row.enabled = command.enabled;
    model.actions.push_back(row);
  }

  if (panel == TitlePanel::Load) {
    model.panelTitle = "Load a voyage";
    model.panelNote =
        "Choose one of your three independent save slots. Slot 1 also reads the original "
        "save.";
    for (int index = 1; index <= 3; ++index) {
      PopupSlot entry;
      entry.slot = index;
      entry.label = index == 1 ? "Slot 1 (original)" : "Slot " + Num(index);
      entry.summary = SaveSlotSummary(sim, index, saveDir, &entry.written);
      model.slots.push_back(entry);

      ActionRowView row;
      row.id = "slot:" + Num(index);
      row.label = entry.label + " - " + entry.summary;
      row.cost = entry.written ? "free" : "empty";
      row.enabled = entry.written;
      row.reading = !entry.written;
      model.panelRows.push_back(row);
    }
    ActionRowView load;
    load.id = "load-slot";
    load.label = "LOAD VOYAGE from slot " + Num(slot);
    load.cost = "free";
    load.enabled = slot >= 1 && slot <= 3;
    model.panelRows.push_back(load);
  } else if (panel == TitlePanel::Settings) {
    model.panelTitle = "Options";
    model.panelNote = "Preferences are free to change and travel with the voyage.";
    model.settings = BuildPopup(sim, "settings", saveDir);
    for (const PopupToggle& toggle : model.settings.toggles) {
      ActionRowView row;
      row.id = "toggle:" + toggle.id;
      row.label = toggle.label;
      row.cost = "free";
      row.enabled = true;
      model.panelRows.push_back(row);
    }
    for (std::size_t i = 0; i < model.settings.themes.size(); ++i) {
      ActionRowView row;
      row.id = "theme:" + model.settings.themes[i];
      row.label = "Palette: " + (i < model.settings.themeLabels.size()
                                     ? model.settings.themeLabels[i]
                                     : model.settings.themes[i]);
      row.cost = "free";
      row.enabled = true;
      model.panelRows.push_back(row);
    }
  } else if (panel == TitlePanel::Guide) {
    model.panelTitle = "Your first watch";
    model.panelNote = std::string(kGuide) + "\n\n" + kGuideMore + "\n\n" + kGuideShip;
  } else if (panel == TitlePanel::Quit) {
    model.panelTitle = "Leaving the game";
    model.panelNote = "Close this window to exit the preview.";
  }

  if (panel != TitlePanel::None) {
    ActionRowView back;
    back.id = "back";
    back.label = "BACK";
    back.cost = "free";
    back.enabled = true;
    model.panelRows.push_back(back);
  }
  return model;
}

bool PressTitleAction(Simulation& sim, const std::string& action, const std::string& arg,
                      bool* fromVoyage, TitlePanel* panel, int* slot,
                      const std::string& saveDir, std::string* message) {
  (void)arg;
  if (message != nullptr) { message->clear(); }
  if (fromVoyage == nullptr || panel == nullptr || slot == nullptr) { return false; }

  if (action == "new-voyage") {
    sim.RestartVoyage();
    *fromVoyage = false;
    *panel = TitlePanel::None;
    if (message != nullptr) { *message = "A new voyage has begun."; }
    return true;
  }
  if (action == "load") { *panel = TitlePanel::Load; return true; }
  if (action == "options") { *panel = TitlePanel::Settings; return true; }
  if (action == "guide") { *panel = TitlePanel::Guide; return true; }
  if (action == "resume") {
    if (!*fromVoyage) { *panel = TitlePanel::Quit; return true; }
    return true;  // the caller hides the menu
  }
  if (action == "back") { *panel = TitlePanel::None; return true; }
  if (action.compare(0, 5, "slot:") == 0) {
    const int index = std::stoi(action.substr(5));
    if (index < 1 || index > 3) { return false; }
    *slot = index;
    return true;
  }
  if (action == "load-slot") {
    std::string error;
    const std::string directory = saveDir.empty() ? std::string(".") : saveDir;
    if (!sim.LoadSlot(*slot, directory, &error)) {
      if (message != nullptr) { *message = "Could not load: " + error; }
      return false;
    }
    *fromVoyage = true;
    *panel = TitlePanel::None;
    if (message != nullptr) { *message = "Saved voyage restored."; }
    return true;
  }
  if (action.compare(0, 7, "toggle:") == 0) {
    return PressPopupAction(sim, "settings", "toggle", action.substr(7), saveDir, message);
  }
  if (action.compare(0, 6, "theme:") == 0) {
    return PressPopupAction(sim, "settings", "theme", action.substr(6), saveDir, message);
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Loss
// ---------------------------------------------------------------------------
LossModel BuildLossScreen(const Simulation& sim, int slot, const std::string& message) {
  LossModel model;
  model.slot = slot;
  model.message = message;
  model.cause = sim.State().gameOver;
  model.lost = !model.cause.empty();
  if (!model.lost) { return model; }

  const GameState& state = sim.State();
  model.summary.push_back(PopupRow{"Actions", Num(sim.ActionCount())});
  model.summary.push_back(PopupRow{"Shiplets", Num(state.shiplets)});
  model.summary.push_back(PopupRow{"Captain's rank", sim.CaptainRank()});
  model.summary.push_back(
      PopupRow{"Commendations", Num(sim.EarnedCommendations()) + " / " +
                                   Num(static_cast<double>(sim.World().commendations.size()))});
  model.summary.push_back(PopupRow{"Systems visited", Num(Owned(state.chronicle.systems))});
  model.summary.push_back(PopupRow{"Regions visited", Num(Owned(state.chronicle.regions))});
  model.summary.push_back(PopupRow{"Crew remaining", Num(static_cast<double>(sim.Crew().size()))});

  for (int index = 1; index <= 3; ++index) {
    ActionRowView row;
    row.id = "slot:" + Num(index);
    row.label = "Load slot " + Num(index);
    row.cost = "free";
    row.enabled = true;
    model.actions.push_back(row);
  }
  ActionRowView load;
  load.id = "load";
  load.label = "Load saved game";
  load.cost = "free";
  load.enabled = true;
  model.actions.push_back(load);

  ActionRowView fresh;
  fresh.id = "new-voyage";
  fresh.label = "Start a new voyage";
  fresh.cost = "free";
  fresh.enabled = true;
  model.actions.push_back(fresh);

  ActionRowView menu;
  menu.id = "main-menu";
  menu.label = "Return to main menu";
  menu.cost = "free";
  menu.enabled = true;
  model.actions.push_back(menu);
  return model;
}

bool PressLossAction(Simulation& sim, const std::string& action, const std::string& arg,
                     int* slot, bool* toTitle, const std::string& saveDir,
                     std::string* message) {
  (void)arg;
  if (message != nullptr) { message->clear(); }
  if (slot == nullptr || toTitle == nullptr) { return false; }

  if (action.compare(0, 5, "slot:") == 0) {
    const int index = std::stoi(action.substr(5));
    if (index < 1 || index > 3) { return false; }
    *slot = index;
    return true;
  }
  if (action == "load") {
    std::string error;
    const std::string directory = saveDir.empty() ? std::string(".") : saveDir;
    if (!sim.LoadSlot(*slot, directory, &error)) {
      if (message != nullptr) { *message = "Could not load: " + error; }
      return false;
    }
    if (message != nullptr) { *message = "Saved voyage restored."; }
    return true;
  }
  if (action == "new-voyage") {
    sim.RestartVoyage();
    if (message != nullptr) { *message = "A new voyage has begun."; }
    return true;
  }
  if (action == "main-menu") {
    *toTitle = true;
    return true;
  }
  return false;
}

}  // namespace ship

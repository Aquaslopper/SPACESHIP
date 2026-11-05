// ============================================================================
//  TitleScreen.h — the main menu and the loss screen (Section N).
//  ---------------------------------------------------------------------------
//  The menu pauses nothing because the game only advances on actions. It is
//  four things: the title card and its art, five free commands, one dialog that
//  can hold the three save slots, the settings sheet or the guide, and the
//  overlay a lost voyage leaves behind.
//
//  Every model here is data. The menu's own actions are free — hiding the menu,
//  starting again and loading a slot are the reference's free verbs — and the
//  loss overlay adds a voyage summary the HTA does not print, so a player can
//  see what the run amounted to before choosing.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Popups.h"
#include "ui/Widgets.h"

namespace ship {

// The dialog the menu can have open, in the order the reference offers it.
enum class TitlePanel { None, Load, Settings, Guide, Quit };

struct TitleModel {
  std::string heading = "SPACESHIP";
  std::string subtitle = "Command deck of the Wren courier";
  std::string artPath;            // assets/art/title.jpg
  float artWidth = 840.0f;        // the card's authored 3:2 crop of the 1672x941 art
  float artHeight = 560.0f;
  bool fromVoyage = false;        // RESUME when true, EXIT when not
  std::vector<ActionRowView> actions;
  TitlePanel panel = TitlePanel::None;
  std::string panelTitle;
  std::string panelNote;
  std::vector<ActionRowView> panelRows;   // slots, settings, back
  std::vector<PopupSlot> slots;
  PopupModel settings;                    // the same sheet the footer opens
  int slot = 1;
  std::string message;
};

std::string TitleButtonLabel(bool fromVoyage);
bool AnySlotWritten(const Simulation& sim, const std::string& saveDir);

TitleModel BuildTitleScreen(const Simulation& sim, bool fromVoyage, TitlePanel panel,
                            const std::string& saveDir, int slot = 1,
                            const std::string& message = std::string());

// Free menu verbs. `fromVoyage`/`panel` are the menu's own state; the press
// returns true when something changed and the caller should repaint.
bool PressTitleAction(Simulation& sim, const std::string& action, const std::string& arg,
                      bool* fromVoyage, TitlePanel* panel, int* slot,
                      const std::string& saveDir, std::string* message);

// ---------------------------------------------------------------------------
//  Loss
// ---------------------------------------------------------------------------
struct LossModel {
  bool lost = false;
  std::string heading = "Ship lost";
  std::string cause;      // STATE.gameOver, verbatim
  std::string note = "Load a saved game or start a new voyage.";
  std::vector<PopupRow> summary;    // the voyage, as it ended
  std::vector<ActionRowView> actions;
  int slot = 1;
  std::string message;
};

LossModel BuildLossScreen(const Simulation& sim, int slot = 1,
                          const std::string& message = std::string());

// "load" (the selected slot), "slot:<n>", "new-voyage", "main-menu".
bool PressLossAction(Simulation& sim, const std::string& action, const std::string& arg,
                     int* slot, bool* toTitle, const std::string& saveDir,
                     std::string* message);

}  // namespace ship

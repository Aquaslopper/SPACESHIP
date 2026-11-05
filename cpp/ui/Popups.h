// ============================================================================
//  Popups.h — the four floating panels the pinned foot opens (Section K).
//  ---------------------------------------------------------------------------
//  A popup never replaces the main area: the idle screen stays readable behind
//  it. Everything in one is FREE - opening it, reading it, toggling a
//  preference, saving or loading - so none of them can reach PerformAction.
//
//  The bodies are built the way the reference builds them: a manifest of crew
//  and compartments, the log newest-first, a settings sheet whose button labels
//  say what the click will do, and three independent save slots.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/Simulation.h"

namespace ship {

struct PopupRow {
  std::string key;
  std::string value;
};

struct PopupSection {
  std::string title;
  std::vector<PopupRow> rows;
};

// A setting: its id, and the label the button carries right now.
struct PopupToggle {
  std::string id;
  std::string label;
  bool on = false;
};

struct PopupSlot {
  int slot = 1;
  std::string label;    // "Slot 1 (original)"
  std::string summary;  // "12 actions, 340 sh" or "Empty"
  bool written = false;
};

struct PopupModel {
  std::string key;     // manifest / log / files / settings
  std::string title;   // "Ship Manifest", "Activity Log", ...
  std::string notice;
  std::vector<PopupSection> sections;
  std::vector<std::string> lines;   // the log, newest first
  std::vector<PopupToggle> toggles;
  std::vector<std::string> themes;
  std::vector<std::string> themeLabels;
  std::string theme;
  std::string audioLevel;
  std::vector<PopupSlot> slots;
  std::string message;
  bool free = true;
};

// The four keys, in the order the foot lists them.
const std::vector<std::string>& PopupKeys();
std::string PopupTitle(const std::string& key);

// slotSummary(): a label, not a machine — read by loading the file into a
// throw-away voyage, so a corrupt slot reports Empty rather than lying.
std::string SaveSlotSummary(const Simulation& sim, int slot, const std::string& saveDir,
                            bool* written);

PopupModel BuildPopup(const Simulation& sim, const std::string& key,
                      const std::string& saveDir = std::string());

// What a click in a popup does. Every action here is free.
bool PressPopupAction(Simulation& sim, const std::string& key,
                      const std::string& action, const std::string& arg,
                      const std::string& saveDir, std::string* message);

}  // namespace ship

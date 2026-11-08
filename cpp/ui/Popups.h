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
#include "ui/Describe.h"

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
  std::string heading = {};
  std::string help = {};
};

struct PopupChoice {
  std::string value;
  std::string label;
};

struct PopupSlot {
  int slot = 1;
  std::string label;    // "Slot 1 (original)"
  std::string summary;  // "12 clicks, 340 sh" or "Empty"
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
  float displayGamma = 1.0f;
  float displayBrightness = 1.0f;
  std::vector<PopupChoice> audioChoices;
  // Phase 9: the reading-size ladder (18/19/20/21 px) and the borderless
  // window switch. Both are preferences; both are drawn as their own rows.
  int fontStep = 0;
  std::vector<std::string> fontStepLabels;
  std::string fontStepLine;    // "Reading size: +5% (19 px)"
  bool fullscreen = false;
  std::string fullscreenLabel; // "Window: Borderless fullscreen"
  // The sky rows: the words that draw the chart, how many stars it holds and how
  // many of those systems carry a dock. Each choice value is what a press hands
  // back; changing one starts a new voyage on the new chart.
  std::vector<PopupChoice> skyChoices;
  std::vector<PopupChoice> starChoices;
  std::vector<PopupChoice> stationChoices;
  std::vector<std::string> skyHelp;   // help line per station density
  std::string universeSeed;
  std::string universeSystems;
  std::string stationDensity;
  std::string universeSeedLine;      // "Sky: kessler"
  std::string universeSystemsLine;   // "Stars: 1200"
  std::vector<PopupSlot> slots;
  std::string message;
  std::vector<DescribeAct> acts;
  std::vector<std::string> footer;
  bool free = true;
};

// The four keys, in the order the foot lists them.
const std::vector<std::string>& PopupKeys();
std::string PopupTitle(const std::string& key);

// slotSummary(): a label, not a machine — read by loading the file into a
// throw-away voyage, so a corrupt slot reports Empty rather than lying.
std::string SaveSlotSummary(const Simulation& sim, int slot, const std::string& saveDir,
                            bool* written);

PopupModel BuildBriefPopup(const Simulation& sim, const std::string& kind,
                           const std::string& id);

PopupModel BuildPopup(const Simulation& sim, const std::string& key,
                      const std::string& saveDir = std::string());

// What a click in a popup does. Every action here is free.
bool PressPopupAction(Simulation& sim, const std::string& key,
                      const std::string& action, const std::string& arg,
                      const std::string& saveDir, std::string* message);

}  // namespace ship

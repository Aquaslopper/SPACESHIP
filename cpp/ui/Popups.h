// ============================================================================
//  Popups.h — the four floating panels the pinned foot opens (Section K).
//  ---------------------------------------------------------------------------
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
  std::string label;    
  std::string summary;  
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
  float displayGamma = 1.3f;
  float displayBrightness = 1.0f;
  float radiationWave = 1.1f;
  float colorShift = 0.0f;
  float glow = 1.1f;
  float lensFlare = 0.7f;
  std::vector<PopupChoice> audioChoices;
  int fontStep = 2;
  std::vector<std::string> fontStepLabels;
  std::string fontStepLine;    // "Reading size: +5% (19 px)"
  bool fullscreen = true;
  std::string fullscreenLabel; // "Window: Borderless fullscreen"
  std::vector<PopupSlot> slots;
  std::string message;
  std::vector<DescribeAct> acts;
  std::vector<std::string> footer;
  bool free = true;
};


const std::vector<std::string>& PopupKeys();
std::string PopupTitle(const std::string& key);


std::string SaveSlotSummary(const Simulation& sim, int slot, const std::string& saveDir,
                            bool* written);

PopupModel BuildBriefPopup(const Simulation& sim, const std::string& kind,
                           const std::string& id);

PopupModel BuildPopup(const Simulation& sim, const std::string& key,
                      const std::string& saveDir = std::string());


bool PressPopupAction(Simulation& sim, const std::string& key,
                      const std::string& action, const std::string& arg,
                      const std::string& saveDir, std::string* message);

}  // namespace ship

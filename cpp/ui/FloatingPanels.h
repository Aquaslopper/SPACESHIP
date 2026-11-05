#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <utility>

#include "ui/Popups.h"

namespace ship {

// Ephemeral presentation state only: not part of the voyage or save format.
// HTA showPopup cascades every new panel; settings/files replace only their
// own predecessor. Briefings and read-only panels can coexist independently.
struct FloatingPanel {
  int serial = 0;
  float x = 300;
  float y = 90;
  std::string key; // empty for a snapshot briefing
  PopupModel brief;
  int slot = 1;
  std::string message;
  int pendingSlot = 1;
  std::string pendingAction;
  std::string question;
};

class FloatingPanels {
 public:
  FloatingPanel& Open(const std::string& key, PopupModel brief = {}) {
    if (key == "settings" || key == "files") {
      panels_.erase(std::remove_if(panels_.begin(), panels_.end(),
          [&](const auto& p) { return p.key == key; }), panels_.end());
    }
    FloatingPanel panel;
    const int offset = (opened_ % 5) * 22;
    panel.serial = ++opened_;
    panel.x += offset;
    panel.y += offset;
    panel.key = key;
    panel.brief = std::move(brief);
    panels_.push_back(std::move(panel));
    return panels_.back();
  }
  void Close(int serial) {
    panels_.erase(std::remove_if(panels_.begin(), panels_.end(),
        [&](const auto& p) { return p.serial == serial; }), panels_.end());
  }
  void Clear() { panels_.clear(); }
  int OpenedCount() const { return opened_; }
  bool HasConfirmation() const {
    return std::any_of(panels_.begin(), panels_.end(), [](const auto& p) { return !p.pendingAction.empty(); });
  }
  bool HasBriefings() const {
    return std::any_of(panels_.begin(), panels_.end(), [](const auto& p) { return p.key.empty(); });
  }
  std::vector<FloatingPanel>& Panels() { return panels_; }
  const std::vector<FloatingPanel>& Panels() const { return panels_; }
 private:
  int opened_ = 0;
  std::vector<FloatingPanel> panels_;
};

} // namespace ship

// ============================================================================
//  RoomsScreen.h — the fitted compartments (Sections I and M).
//  ---------------------------------------------------------------------------
//  Fourteen bays, one bridge and the Simulacron are real rooms rather than
//  store cupboards: each opens on its own authored vignette, its own fields and
//  the work that belongs to it — a project, a treatment, a transfer, a meal, a
//  traffic report, or the two permanent answers to a signal.
//
//  Every row is data, so the layout, the sentences and the press ids can be
//  checked without a window.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Widgets.h"

namespace ship {

struct RoomScreenModel {
  TaskHead head;
  std::string roomId;
  std::string glyph;      // the vignette the paint layer draws
  std::string code;       // "PROPULSION / 01"
  std::string caption;    // the scene's own label
  std::string description;
  std::string accent;     // authored hex
  NavPanel body;          // "Ship systems": the room's fields and notices
  std::vector<ActionRowView> rows;
  std::vector<ProgressButton> buttons;
};

// ROOM_FITS: the fitted bays (r1..r13) and the bridge.
bool RoomHasScreen(const Simulation& sim, const std::string& roomId);

RoomScreenModel BuildRoomScreen(const Simulation& sim, const std::string& roomId);

// The press path. Free verbs (opening the chart) return true and spend nothing;
// committed ones spend exactly one action.
std::string RoomActionCategory(const std::string& action);

bool PressRoomAction(Simulation& sim, const std::string& action,
                     const std::string& arg = std::string());

}  // namespace ship

// ============================================================================
//  Ships.cpp — the hull the voyage is happening in.
//  ---------------------------------------------------------------------------
//  Two classes, one deck plan each. Buying the Kestrel is not a new ship: it is
//  the same voyage in a larger hull. The crew, the hold, the equipment, the work
//  in progress and every record transfer; only the deck plan and the berths
//  change.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <string>

namespace ship {

double Simulation::ShipCargoCapacity(const std::string& kind, int level) const {
  const int index = std::max(0, std::min(2, level));
  auto it = catalog_.cargoByClass.find(kind);
  if (it == catalog_.cargoByClass.end() || it->second.empty()) {
    const int values[3] = {kind == "wren" ? 40 : 60, kind == "wren" ? 60 : 90, kind == "wren" ? 90 : 120};
    return values[index];
  }
  return it->second[static_cast<std::size_t>(index)];
}

int Simulation::CargoRefitLevel() const {
  auto has = [this](const char* id) {
    auto it = state_.upgrades.find(id);
    return it != state_.upgrades.end() && it->second;
  };
  return has("bay2") ? 2 : (has("bay") ? 1 : 0);
}

void Simulation::ApplyShipLayout() {
  // The reference keeps ROOM_BLUEPRINTS as the large plan and SMALL_ROOMS as the
  // compact one, and rewrites every room's box from the plan for the class the
  // voyage is flying. Unrecognised legacy rooms are preserved, not dropped.
  const DeckPlan& plan = state_.shipClass == "wren" ? catalog_.wren : catalog_.kestrel;
  for (Room& room : rooms_) {
    const Room* authored = plan.FindRoom(room.id);
    if (authored == nullptr) { continue; }
    room.box = authored->box;
    room.tag = authored->tag;
    room.small = authored->small;
    room.tone = authored->tone;
    room.systems = authored->systems;
    room.name = authored->name;
  }
}

bool Simulation::BuyLargerShip() {
  const double price = 4000;
  if (!state_.gameOver.empty() || !state_.docked) { return false; }
  const Port* port = HerePort();
  if (port == nullptr || state_.shipClass != "wren" || state_.shiplets < price) { return false; }

  const std::string portName = port->name;
  state_.shiplets -= price;
  state_.shipClass = "kestrel";
  auto fields = catalog_.shipFields.find("kestrel");
  if (fields != catalog_.shipFields.end()) { ship_.fields = fields->second; }
  cargo_.capacity = std::max(cargo_.capacity, ShipCargoCapacity("kestrel", CargoRefitLevel()));
  ApplyShipLayout();

  SimRemember("Work", "The crew transferred from SC Wren to SC Tessellate at " + portName + ".");
  PerformAction("Commissioned SC Tessellate");
  return true;
}

}  // namespace ship

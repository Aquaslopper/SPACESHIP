// ============================================================================
//  Ships.cpp — the hull the voyage is happening in.
//  ---------------------------------------------------------------------------
//  Wren, Kestrel and two cargo haulers share the existing deck plans. Buying a hull is
//  the same voyage in another hull. The crew, the hold, the equipment, the work
//  in progress and every record transfer; only the deck plan and the berths
//  change.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <string>

namespace ship {

double Simulation::ShipCargoCapacity(const std::string& kind, int level) const {
  if (kind == "explorer-needle" || kind == "explorer-manta") {
    return ShipCargoCapacity("wren", level);
  }
  const int index = std::max(0, std::min(2, level));
  if (kind == "wren" && index == 0 && !referenceFit_) return 20;
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

bool Simulation::StandardRoom(const std::string& id) {
  return id == "engine" || id == "o2" || id == "quarters" || id == "mess" ||
         id == "cargo" || id == "bridge";
}

bool Simulation::HasRoom(const std::string& id) const {
  return std::any_of(rooms_.begin(), rooms_.end(), [&](const Room& room) { return room.id == id; });
}

bool Simulation::BuyRoom(const std::string& id) {
  if (!state_.gameOver.empty() || !state_.docked || HerePort() == nullptr ||
      state_.shiplets < RoomPrice() || StandardRoom(id) || HasRoom(id)) return false;
  const DeckPlan& plan = (state_.shipClass == "wren" || state_.shipClass == "explorer-needle" || state_.shipClass == "explorer-manta") ? catalog_.wren : catalog_.kestrel;
  const Room* room = plan.FindRoom(id);
  if (room == nullptr) return false;
  const std::string name = room->name;
  rooms_.push_back(*room);
  state_.shiplets -= RoomPrice();
  FreeCommit("Installed " + name);
  return true;
}

void Simulation::ApplyShipLayout() {
  // The reference keeps ROOM_BLUEPRINTS as the large plan and SMALL_ROOMS as the
  // compact one, and rewrites every room's box from the plan for the class the
  // voyage is flying. Unrecognised legacy rooms are preserved, not dropped.
  const DeckPlan& plan = (state_.shipClass == "wren" || state_.shipClass == "explorer-needle" || state_.shipClass == "explorer-manta") ? catalog_.wren : catalog_.kestrel;
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

bool Simulation::BuyLargerShip(const std::string& kind) {
  const double price = kind == "kestrel" ? 4000 : 1;
  if (!state_.gameOver.empty() || !state_.docked) { return false; }
  const Port* port = HerePort();
  if (port == nullptr || (kind == "kestrel" && state_.shipClass != "wren" &&
                          state_.shipClass != "explorer-needle" && state_.shipClass != "explorer-manta") ||
      state_.shipClass == kind || state_.shiplets < price) { return false; }

  state_.shiplets -= price;
  state_.shipClass = kind;
  auto fields = catalog_.shipFields.find(kind);
  if (fields != catalog_.shipFields.end()) { ship_.fields = fields->second; }
  cargo_.capacity = kind == "kestrel"
      ? std::max(cargo_.capacity, ShipCargoCapacity(kind, CargoRefitLevel()))
      : ShipCargoCapacity(kind, CargoRefitLevel());
  ApplyShipLayout();

  FreeCommit("Commissioned " + ship_.fields["name"]);
  return true;
}

}  // namespace ship

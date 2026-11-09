// ============================================================================
//  ShipDiagram.h — the cross-section, as numbers.
//  ---------------------------------------------------------------------------
//  §E builds the stage once and leaves it alone: ten hull pieces with their CSS
//  border radii, intake bars on each nacelle, three nozzles aft of the main
//  body, the spine corridor with its label, a power conduit along the deck, the
//  twenty-one compartments from the deck plan, two cosmetic bay dividers in the
//  hold, and one dot per hand. The compact Wren is the same recipe at 600x365
//  with two nozzles and a smaller hull.
//
//  The rectangles here are STAGE-LOCAL, exactly as. The renderer adds
//  the stage origin; that keeps the reference's pixel numbers checkable.
// ============================================================================
#pragma once

#include <string>
#include <map>
#include <vector>

#include "core/Simulation.h"
#include "ui/Fx.h"
#include "ui/Layout.h"

namespace ship {

// The CSS border radius of each silhouette class, in pixels. The hull is drawn
// with rounded corners, not a rectangle; this is the one number a port is most
// likely to lose.
float HullRadius(const std::string& cls);

struct DiagramCrew {
  std::string id;
  std::string firstName;
  std::string colour;
  std::string roomId;   // compartment the dot is confined to
  Area dot;             // 9x9 at stage-local coordinates
  Area label;
  bool labelled = false;
};

struct DiagramModel {
  Area stage;              // the fixed 1160x460 host
  bool compact = false;
  std::vector<HullPart> hull;
  std::vector<Area> nacBars;
  std::vector<Area> nozzles;
  Area corridor;
  std::string corridorLabel;
  Area corridorLabelBox;
  Area conduit;
  std::vector<Room> rooms;
  std::map<std::string, std::string> roomAccents;
  std::vector<Area> dividers;
  std::vector<DiagramCrew> crew;
};

// The deck plan the voyage is currently flying. A Wren is compact; anything
// else is drawn at the large hull.
const DeckPlan& ActivePlan(const Simulation& sim);

// Build the cross-section. `rng` places each dot inside its compartment; pass
// the same seed twice and the diagram is identical, which is what the layout
// tests rely on.
DiagramModel BuildDiagram(const Simulation& sim, const Area& stage,
                          bool hideCrewLabels, FxRng& rng);

// crewBox(): the compartment a hand is drawn in. Laid off hands drift up and
// down the corridor, which is how the payroll is read off the cross-section.
const Room* CrewRoom(const Simulation& sim, const CrewMember& crew);

// The compact hull's nozzle count differs (two, not three).
int NozzleCount(bool compact);

}  // namespace ship

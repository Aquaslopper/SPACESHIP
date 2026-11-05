// ============================================================================
//  CrewSprites.h — the quarters figure, as rectangles.
//  ---------------------------------------------------------------------------
//  The reference draws a crew member out of character maps: rows of letters,
//  five pixels a cell, no image file and no canvas. This header turns those
//  maps into positioned rectangles with resolved colours, so a test can read
//  the exact pixel grid a window would paint, and Draw can blit it with nothing
//  but DrawRectangle.
//
//  Posture follows what the sheet is about to do to the hand - not what they
//  are doing now. A body being sent to the doc lies on a cot; someone being
//  schooled stands at a bench; a trade change puts a crate at their feet; and a
//  hand nobody wants sits on the deck with zzz over their head.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Layout.h"
#include "data/Types.h"

namespace ship {

// One cell of a sprite: where it sits in hallway coordinates, and its colour.
struct SpritePixel {
  Area box;
  std::string colour;
};

struct CrewLook {
  std::string pose = "stand";   // stand / lie / sit
  std::string cloth;
  std::string dark;
  std::string btn;
  std::string hatKind;
  std::string hat;
};

// spriteLook(): what the figure is wearing. The STAGED job, not the saved one,
// so the player sees the change before paying for it. A patient wears the gown,
// and even the buckle goes quiet.
CrewLook LookForCrew(const Simulation& sim, const CrewMember& crew);

// The colour behind one letter of a sprite row.
std::string PixelColour(const Simulation& sim, const CrewMember& crew,
                        const CrewLook& look, char key);

// figureRows(): the haircut applied over the standing sprite, the top four rows
// blanked by a hat, and the female eye row spliced in.
std::vector<std::string> FigureRows(const Simulation& sim, const CrewMember& crew,
                                    bool hatted);

// One grid of cells, placed in hallway coordinates.
std::vector<SpritePixel> Rasterise(const Simulation& sim, const CrewMember& crew,
                                   const CrewLook& look, const SpriteFrame& frame,
                                   double ox, double oy);

struct CrewScene {
  std::string pose = "stand";
  std::string signature;              // what the layer would look different by
  std::vector<SpritePixel> pixels;    // props, then the figure, then the hat
  std::vector<std::string> propNames;
  double figureX = 244;
  double figureY = 0;
};

CrewScene BuildCrewScene(const Simulation& sim, const CrewMember& crew);

// onDeck(rows): the top of something that rests on the deck line.
double OnDeck(const Simulation& sim, std::size_t rows);

}  // namespace ship

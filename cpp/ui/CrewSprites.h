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
//
//  A crew member may also have pictures under assets/art/crew: a figure for
//  the stage and a portrait for the card beside it. The scene only names them.
//  The painter uses a picture whose file was loaded and the rectangles when
//  there is none, so a hand without pictures still has a figure.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Layout.h"
#include "data/Types.h"

namespace ship {

// The quarters stage in its own units: the portrait card on the left, the
// corridor on the right. The figure stands at kCrewFigureX on the deck line,
// before a door kCrewDoorW wide. Either side of the door the wall is bare for
// kCrewBareWall: the sign goes on the left of it, the tool and the speech
// bubble on the right.
inline constexpr float kCrewStageW = 560.0f;
inline constexpr float kCrewStageH = 330.0f;
inline constexpr float kCrewStageTypePx = 18.0f;  // the reading size it is laid out for
inline constexpr float kCrewFigureX = 401.0f;
inline constexpr float kCrewDeckY = 278.0f;
inline constexpr float kCrewDoorW = 96.0f;
inline constexpr float kCrewBareWall = 52.0f;

// One cell of a sprite: where it sits in stage coordinates, and its colour.
struct SpritePixel {
  Area box;
  std::string colour;
};

// A picture from assets/art/crew, named by its file without ".png". It is
// painted at half its pixel size with the middle of its lower edge at
// (x, base). `cloth` colours the layer "<name>-cloth" painted underneath it,
// where the picture has holes in place of the uniform.
struct ScenePicture {
  std::string name;
  double x = 0;
  double base = 0;
  std::string cloth;
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

// The side of one sprite cell on the stage.
double SpriteCell(const Simulation& sim);

// One grid of cells, placed in stage coordinates. `cell` is the side of one
// cell; zero takes SpriteCell().
std::vector<SpritePixel> Rasterise(const Simulation& sim, const CrewMember& crew,
                                   const CrewLook& look, const SpriteFrame& frame,
                                   double ox, double oy, double cell = 0.0);

struct CrewScene {
  std::string pose = "stand";
  std::string signature;              // what the layer would look different by
  std::vector<SpritePixel> pixels;    // props, then the figure, then the hat
  // pixels[figureFrom] onwards are what a figure picture stands in for: the
  // figure and its hat, and the cot under a patient.
  std::size_t figureFrom = 0;
  std::vector<std::string> propNames;
  double figureX = 0;
  double figureY = 0;
  // The same figure as a picture, best first. The first one on file is painted.
  std::vector<ScenePicture> figureArt;
  std::string portrait;               // the picture for the card
};

CrewScene BuildCrewScene(const Simulation& sim, const CrewMember& crew);

// onDeck(rows): the top of something that rests on the deck line.
double OnDeck(const Simulation& sim, std::size_t rows);

}  // namespace ship

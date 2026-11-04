// ============================================================================
//  Draw.h — the only file that knows a pixel has a colour.
//  ---------------------------------------------------------------------------
//  Everything the shell needs to decide is in Theme, Layout, Shell, Describe,
//  IdleScreen, ShipDiagram and Fx, and all of it is testable headless. This
//  class is the thin skin over that: it measures the window, asks Layout for
//  the rectangles, asks the view-models for the content, and paints.
//
//  It is compiled only when the GUI target is built (SPACESHIP_GUI); on a
//  machine without X11 or OpenGL the CMake gate skips it entirely and the
//  tests still run. In this sandbox it is compile-checked with -fsyntax-only.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Describe.h"
#include "ui/Fx.h"
#include "ui/IdleScreen.h"
#include "ui/Layout.h"
#include "ui/Shell.h"
#include "ui/Widgets.h"
#include "ui/Theme.h"

namespace ship {

// What the pointer did, independent of which library delivered it.
enum class HitButton { None, Left, Right };

class ShellView {
 public:
  ShellView();

  // Window size and options, before anything is drawn.
  void Configure(float width, float height, const Theme& theme);

  // The idle screen's non-feed height, measured once per paint. Reported to
  // Layout::GiveFeedHeight, exactly as `other` is reported in the reference.
  void SetOthersHeight(float others) { others_ = others; }

  // One paint. `nowMs` drives the cosmetic effects only.
  void Draw(Simulation& sim, double nowMs);

  // The two verbs, from a hit test in window coordinates.
  void Click(Simulation& sim, float x, float y, HitButton button, double nowMs);

  // What the last left-click selected, if anything. Empty when the click
  // landed on chrome rather than on the ship.
  const Description& Selection() const { return selection_; }
  bool HasSelection() const { return hasSelection_; }

  const ShellLayout& LayoutNow() const { return layout_; }
  const Theme& CurrentTheme() const { return theme_; }
  void SetTheme(const Theme& theme) { theme_ = theme; }

 private:
  Theme theme_;
  ShellLayout layout_;
  FxRng rng_;
  std::vector<ActionMark> marks_;
  std::vector<DriftDot> dots_;
  IdleScreenModel idle_;
  Description selection_;
  bool hasSelection_ = false;
  float width_ = 1280.0f;
  float height_ = 800.0f;
  float others_ = 300.0f;
  int tabScroll_ = 0;

  TaskScreenModel taskScreen_;
  std::vector<std::pair<Area, std::string>> taskButtons_;
  std::string taskTabKey_;

  void PaintChrome(Simulation& sim);
  void PaintTaskScreen(Simulation& sim);
  void PaintPanel(const NavPanel& panel, const Area& box);
  void PaintStage(const TaskStage& stage, const Area& origin);
  void PaintIdle(Simulation& sim);
  void PaintDiagram(const DiagramModel& diagram);
};

}  // namespace ship

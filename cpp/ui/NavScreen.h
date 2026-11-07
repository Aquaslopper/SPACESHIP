// ============================================================================
//  NavScreen.h — the star chart: three scales, one projection, and the two
//  things on it that cost anything.
//  ---------------------------------------------------------------------------
//  Section I-2 of the reference draws the chart out of absolutely positioned
//  dots, and every dot goes through the same pair of functions:
//
//      mapX(wx) = (cx + (wx - cx) * zoom) * (vw / w)  + panX
//      mapY(wy) = (cy + (wy - cy) * zoom) * (vh / h)  + panY
//
//  ...so the chart can be any size at all and nothing comes loose from
//  anything else. That pair, the perspective raster behind it, the pan clamp
//  and the zoom ladder all live here as plain functions, which is why the
//  geometry can be checked character-for-character without a window.
//
//  Nothing in this file touches a renderer, and nothing in the chart model
//  touches game state. Looking is free: selecting, zooming, panning and
//  reading panels never reach PerformAction. Two controls do - Start Travel
//  and Run Deep Scan - and they do it through the same PressNavAction() the
//  tests drive.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"
#include "ui/Layout.h"
#include "ui/Widgets.h"

namespace ship {

// The reference's exact words for a contact the ship cannot measure.
const char* NavOutOfRange();

// ---------------------------------------------------------------------------
//  The perspective raster (Section D, applyRaster)
//  ---------------------------------------------------------------------------
struct NavRasterRung {
  double y = 0;      // where the rung crosses the box, in chart pixels
  double alpha = 0;  // far faint, near solid
};

struct NavRasterLine {
  double angle = 0;  // degrees, as the CSS gradient was angled
  double t = 0;      // where the hairline crosses its own gradient axis
};

struct NavRaster {
  double width = 0;
  double height = 0;
  double vx = 0;  // the vanishing point, held above the top edge
  double vy = 0;
  std::vector<NavRasterRung> rungs;
  std::vector<NavRasterLine> lines;
};

NavRaster BuildNavRaster(const Simulation& sim);

// The one hairline through (vx,vy) and (bx,by), as the reference computes it.
NavRasterLine NavRasterThrough(double w, double h, double vx, double vy,
                               double bx, double by);

// ---------------------------------------------------------------------------
//  Projection, zoom and pan — all free, all pure
// ---------------------------------------------------------------------------
struct NavPoint {
  double x = 0;
  double y = 0;
};

// mapX / mapY: zoom about the centre of the world, then stretch the world onto
// the live size of the map box.
NavPoint NavProject(const MapView& map, double wx, double wy);

// The same, with the layer's pan applied: what the player actually sees.
NavPoint NavProjectPanned(const MapView& map, const LayerView& view,
                          double wx, double wy);

// zoomMap(dir): one step on the ladder, clamped to [zmin, zmax], rounded to a
// tenth, exactly as the reference rounds it.
double NavZoomStep(double zoom, int dir, double zmin, double zmax, double zstep);

// clampPan(): the chart may be pulled half a screen out of place, no further.
double NavClampPan(double value, double extent);

// ---------------------------------------------------------------------------
//  The chart's contents
// ---------------------------------------------------------------------------
struct NavBodyView {
  std::string id;
  std::string name;
  std::string type;     // origin/star/planet/moon/station/belt/site/system/region
  std::string subtype;  // gas/ice/ocean/rock, planets only
  std::string klass;
  std::string region;   // systems and regions only; the browsed sector is read from Map()

  double worldX = 0;
  double worldY = 0;
  NavPoint at;  // projected, pan included

  double size = 16;        // the hit target, four larger than the dot
  std::string classes;     // "mapdot mapdot-station mapdot-sel"
  double dotX = 0;         // at - size / 2
  double dotY = 0;
  double labelX = 0;       // the label's anchor, flipped near the right edge
  double labelY = 0;
  bool labelRight = false;

  bool selected = false;
  bool here = false;       // this is where the ship is
  bool atTarget = false;
  bool inRange = false;
  bool scanned = false;
};

struct NavDot {
  double x = 0;
  double y = 0;
  std::string cls;  // rdot / rdot-sel / rdot-faint / cdot / cdot-far ...
};

struct NavChartView {
  std::string layer;  // system/sector/galaxy
  double zoom = 1;
  double panX = 0;
  double panY = 0;

  std::vector<NavBodyView> bodies;
  std::vector<NavDot> routes;
  std::vector<NavDot> clusters;

  NavPoint marker;          // the 26px green ring
  bool hasMarker = false;

  std::string scaleText;    // "Kessler - System - zoom x1.0"
};

NavChartView BuildNavChart(const Simulation& sim);

// routeDots(): a dot trail from a to b, not a line.
std::vector<NavDot> NavRouteDots(const Simulation& sim, double ax, double ay,
                                 double bx, double by, bool selected,
                                 bool faint);

// ---------------------------------------------------------------------------
//  NAVFX — the ambient motion. Cosmetic only, and read-only with respect to
//  game state: it is computed from a clock, never from the action counter.
// ---------------------------------------------------------------------------
struct NavFxRock {
  double worldX = 0;
  double worldY = 0;
  double vx = 0;
  int size = 2;
  NavPoint at;
};

struct NavFxFrame {
  std::vector<NavFxRock> rocks;
  bool hasStation = false;
  NavPoint satellite;
  NavPoint beacon;
  double beaconOpacity = 1;
  std::string stationId;
};

// `tick` is the ambient clock in sixtieths of a second (the reference's
// 60ms interval). Reduced motion places the static details and then freezes.
NavFxFrame BuildNavFx(const Simulation& sim, double tick, bool reducedMotion);

// ---------------------------------------------------------------------------
//  The screen
// ---------------------------------------------------------------------------
struct NavScreenModel {
  TaskScreenModel screen;      // head, panels, band, buttons, command rows
  NavChartView chart;
  NavFxFrame fx;
  std::string courseNote;      // the action bar's single line
  std::vector<std::string> notes;
  bool hasTarget = false;
};

NavScreenModel BuildNavScreen(const Simulation& sim);

// What a click on the chart, a layer button, the zoom pair or an action does.
// Every "free" verb is refused nothing and spends nothing; the two commitments
// go through the simulation's own StartTravel/StartScan, so a test drives the
// same path a window does.
bool PressNavAction(Simulation& sim, const std::string& action,
                    const std::string& arg = std::string());

// The layer keys in the reference's order, and the words it prints for them.
const std::vector<std::string>& NavLayerOrder();
std::string NavLayerLabel(const std::string& key);  // SYSTEM / SECTOR / GALAXY
std::string NavLayerName(const std::string& key);   // System / Sector / Galaxy

}  // namespace ship

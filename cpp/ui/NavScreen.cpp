// ============================================================================
//  NavScreen.cpp — the chart, its raster, its panels and its two commitments.
// ============================================================================
#include "ui/NavScreen.h"

#include "core/JsNumber.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace ship {

namespace {

std::string Plur(double n, const std::string& word) {
  return js::NumToString(n) + " " + word + (n == 1 ? "" : "s");
}

std::string Num(double value) { return js::NumToString(value); }

std::string OneDecimal(double value) { return js::ToFixed(value, 1); }

const Body* FindIn(const std::vector<Body>& list, const std::string& id) {
  for (const Body& body : list) {
    if (body.id == id) { return &body; }
  }
  return nullptr;
}

// seeded(seed) from the reference: the same seed draws the same sky forever.
struct Seeded {
  double s = 0;
  explicit Seeded(double seed) : s(seed * 9301 + 49297) {}
  double operator()() {
    s = std::fmod(s * 9301 + 49297, 233280.0);
    return s / 233280.0;
  }
};

std::string BodyLayer(const Simulation& sim) { return sim.Map().layer; }

// The layer's remembered pan. NewVoyage seeds all three, but a caller that
// built a bare MapView still gets a sane zero.
const LayerView& ViewOf(const Simulation& sim) {
  static const LayerView kZero{};
  auto it = sim.Views().find(sim.Map().layer);
  return it == sim.Views().end() ? kZero : it->second;
}

// curBodies(), as pointers so the panels can ask each body its own questions.
std::vector<const Body*> LayerBodies(const Simulation& sim) {
  std::vector<const Body*> out;
  const Catalog& catalog = sim.World();
  const std::string layer = BodyLayer(sim);
  if (layer == "galaxy") {
    for (const Body& body : catalog.regions) { out.push_back(&body); }
    return out;
  }
  if (layer == "system") {
    const std::string sid = sim.CurrentSystemId();
    for (const Body& body : catalog.bodies) {
      if (catalog.BodySystem(body) == sid) { out.push_back(&body); }
    }
    return out;
  }
  for (const Body& body : catalog.systems) {
    if (body.region == sim.Map().sector) { out.push_back(&body); }
  }
  return out;
}

// The layer's authored array, in catalogue order and unfiltered. The
// reference's cluster pass reads this, not curBodies() - see the note in
// BuildNavChart where the clusters are scattered.
const std::vector<Body>& LayerArray(const Simulation& sim) {
  const Catalog& catalog = sim.World();
  const std::string layer = BodyLayer(sim);
  if (layer == "galaxy") { return catalog.regions; }
  if (layer == "system") { return catalog.bodies; }
  return catalog.systems;
}

double BodySize(const Body& body) {
  double size = 16;
  if (body.type == "station") { size = 18; }
  if (body.type == "origin") { size = 14; }
  if (body.type == "moon") { size = 12; }
  if (body.subtype == "gas") { size = 20; }
  if (body.type == "system") { size = 18; }
  if (body.type == "region") { size = 22; }
  return size;
}

std::string BodyClasses(const Body& body, bool selected) {
  std::string cls = "mapdot";
  if (body.type == "station") { cls += " mapdot-station"; }
  if (body.type == "star") { cls += " mapdot-star"; }
  if (body.type == "planet") { cls += " mapdot-planet"; }
  if (body.type == "moon") { cls += " mapdot-moon"; }
  if (!body.subtype.empty()) { cls += " mapdot-" + body.subtype; }
  if (body.type == "belt") { cls += " mapdot-belt"; }
  if (body.type == "site") { cls += " mapdot-site"; }
  if (body.type == "system") { cls += " mapdot-sys"; }
  if (body.type == "region") { cls += " mapdot-region"; }
  if (selected) { cls += " mapdot-sel"; }
  return cls;
}

// travelMarker(): where the green ring sits, part way along the course in
// proportion to the work the player has actually paid for.
NavPoint TravelMarker(const Simulation& sim) {
  const Body* here = sim.FindBodyById(sim.Map().here);
  NavPoint at{here == nullptr ? 0.0 : here->x, here == nullptr ? 0.0 : here->y};
  const Task* travel = sim.FindOngoing("travel");
  if (travel == nullptr || travel->target.empty()) { return at; }
  const Body* body = sim.FindBodyById(travel->target);
  if (body == nullptr) { return at; }
  const double p = OngoingPercent(*travel) / 100.0;
  return NavPoint{at.x + (body->x - at.x) * p, at.y + (body->y - at.y) * p};
}

const CrewMember* NavigatorOf(const Simulation& sim) {
  for (const CrewMember& crew : sim.Crew()) {
    if (crew.job == "navigation" && sim.CrewBusy(crew) == nullptr) { return &crew; }
  }
  return nullptr;
}

double NavigatorDiscount(const CrewMember& navigator) {
  const double at = navigator.jobAt == 0 ? 1.0 : navigator.jobAt;
  return 4.0 * std::max(1.0, std::min(5.0, at));
}

double SimScanCost(const Simulation& sim) {
  return std::max(1.0, sim.ScanCost() - (sim.State().sim.sensor ? 2.0 : 0.0));
}

// scanReadout(): a reward, not a description. Blank until a deep scan has been
// run AND the player has gone back to read it.
std::vector<PanelLine> ScanReadout(const Simulation& sim, const Body& body) {
  std::vector<PanelLine> lines;
  if (!sim.InRange(body)) {
    lines.push_back(PanelLine{PanelLine::Kind::Dim, "", NavOutOfRange(), "", 0.0});
    return lines;
  }
  auto scannedIt = sim.State().scanned.find(body.id);
  const bool scanned = scannedIt != sim.State().scanned.end() && scannedIt->second;
  const Task* scan = sim.FindOngoing("scan");
  if (!scanned) {
    if (scan != nullptr && scan->target == body.id) {
      lines.push_back(PanelLine{PanelLine::Kind::Dim, "",
                                scan->state == "done"
                                    ? "Deep scan complete. Readout waiting."
                                    : "Deep scan under way.",
                                "", 0.0});
    } else {
      lines.push_back(PanelLine{PanelLine::Kind::Dim, "", "No detailed data. Sensors idle.", "", 0.0});
    }
    return lines;
  }
  for (const std::string& line : body.scan) {
    lines.push_back(PanelLine{PanelLine::Kind::Note, "", line, "", 0.0});
  }
  for (const std::string& line : body.scanLocked) {
    lines.push_back(PanelLine{PanelLine::Kind::Dim, "", line + " - sensors insufficient", "", 0.0});
  }
  return lines;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Words
// ---------------------------------------------------------------------------
const char* NavOutOfRange() { return "Distance too great. Target out of sensor range."; }

const std::vector<std::string>& NavLayerOrder() {
  static const std::vector<std::string> order = {"system", "sector", "galaxy"};
  return order;
}

std::string NavLayerLabel(const std::string& key) {
  if (key == "system") { return "SYSTEM"; }
  if (key == "sector") { return "SECTOR"; }
  return "GALAXY";
}

std::string NavLayerName(const std::string& key) {
  if (key == "system") { return "System"; }
  if (key == "sector") { return "Sector"; }
  return "Galaxy";
}

// ---------------------------------------------------------------------------
//  The raster
// ---------------------------------------------------------------------------
NavRasterLine NavRasterThrough(double w, double h, double vx, double vy,
                               double bx, double by) {
  NavRasterLine line;
  const double ux = bx - vx;
  const double uy = by - vy;
  const double len = std::sqrt(ux * ux + uy * uy);
  if (len < 0.01) { return line; }

  double dx = uy / len;  // the perpendicular of the line
  double dy = -ux / len;

  // Two decimals, not whole degrees: on a 900px chart a rounding of half a
  // degree walks the far end of the line about 6px off its anchor.
  const double pi = 3.14159265358979323846;
  const double ang = js::Round(std::atan2(dx, -dy) * 18000 / pi) / 100;
  const double rad = ang * pi / 180;
  dx = std::sin(rad);
  dy = -std::cos(rad);

  const double L = std::abs(w * dx) + std::abs(h * dy);
  line.angle = ang;
  line.t = (vx - w / 2) * dx + (vy - h / 2) * dy + L / 2;
  return line;
}

NavRaster BuildNavRaster(const Simulation& sim) {
  const MapView& map = sim.Map();
  NavRaster raster;
  raster.width = map.vw;
  raster.height = map.vh;

  const double w = map.vw;
  const double h = map.vh;
  const double vx = w / 2;
  const double vy = -h * map.vpy;  // the vanishing point, off the top
  raster.vx = vx;
  raster.vy = vy;

  // Depth rungs: one stop pair per line, bunching toward the far edge.
  double n = js::Round(map.rungs / map.zoom);
  if (n < 4) { n = 4; }
  if (n > 14) { n = 14; }
  for (int j = 1; j <= static_cast<int>(n); ++j) {
    const double f = std::pow(j / (n + 1), map.rungPow);
    NavRasterRung rung;
    rung.y = js::Round(f * h);
    rung.alpha = 0.06 + 0.12 * f;
    raster.rungs.push_back(rung);
  }

  // The fan. Widening the pitch on a wide chart keeps the number of gradient
  // layers bounded whatever the window does.
  double pitch = map.grid * map.zoom;
  if (pitch < w / 13) { pitch = w / 13; }
  double k = js::Ceil((w * 0.62) / pitch) + 1;
  if (k > 7) { k = 7; }
  for (int i = -static_cast<int>(k); i <= static_cast<int>(k); ++i) {
    const NavRasterLine line = NavRasterThrough(w, h, vx, vy, vx + i * pitch, h);
    if (line.t != 0 || line.angle != 0) { raster.lines.push_back(line); }
  }
  return raster;
}

// ---------------------------------------------------------------------------
//  Projection, zoom and pan
// ---------------------------------------------------------------------------
NavPoint NavProject(const MapView& map, double wx, double wy) {
  NavPoint point;
  const double sw = map.w == 0 ? 1.0 : map.w;
  const double sh = map.h == 0 ? 1.0 : map.h;
  point.x = (map.cx + (wx - map.cx) * map.zoom) * (map.vw / sw);
  point.y = (map.cy + (wy - map.cy) * map.zoom) * (map.vh / sh);
  return point;
}

NavPoint NavProjectPanned(const MapView& map, const LayerView& view, double wx,
                          double wy) {
  NavPoint point = NavProject(map, wx, wy);
  point.x += view.px;
  point.y += view.py;
  return point;
}

double NavZoomStep(double zoom, int dir, double zmin, double zmax, double zstep) {
  double next = zoom + static_cast<double>(dir) * zstep;
  if (next < zmin) { next = zmin; }
  if (next > zmax) { next = zmax; }
  return js::Round(next * 10) / 10;
}

double NavClampPan(double value, double extent) {
  const double limit = js::Round(extent * 0.5);
  if (value > limit) { return limit; }
  if (value < -limit) { return -limit; }
  return value;
}

// ---------------------------------------------------------------------------
//  Routes
// ---------------------------------------------------------------------------
std::vector<NavDot> NavRouteDots(const Simulation& sim, double ax, double ay,
                                 double bx, double by, bool selected, bool faint) {
  const MapView& map = sim.Map();
  const LayerView& view = ViewOf(sim);
  const NavPoint a = NavProjectPanned(map, view, ax, ay);
  const NavPoint b = NavProjectPanned(map, view, bx, by);

  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len = std::sqrt(dx * dx + dy * dy);
  const double gap = selected ? 8 : (faint ? 15 : 11);
  double n = std::floor(len / gap);
  if (n > 90) { n = 90; }

  std::vector<NavDot> dots;
  for (int i = 1; i < static_cast<int>(n); ++i) {
    const double t = i / n;
    NavDot dot;
    dot.cls = selected ? "rdot rdot-sel" : (faint ? "rdot rdot-faint" : "rdot");
    dot.x = js::Round(a.x + dx * t);
    dot.y = js::Round(a.y + dy * t);
    dots.push_back(dot);
  }
  return dots;
}

// ---------------------------------------------------------------------------
//  The chart
// ---------------------------------------------------------------------------
NavChartView BuildNavChart(const Simulation& sim) {
  NavChartView chart;
  const MapView& map = sim.Map();
  chart.layer = map.layer;
  chart.zoom = map.zoom;
  const LayerView& view = ViewOf(sim);
  chart.panX = view.px;
  chart.panY = view.py;

  const std::string target = sim.State().navTarget.value_or(std::string());

  // --- bodies -------------------------------------------------------------
  const std::vector<const Body*> bodies = LayerBodies(sim);
  for (const Body* body : bodies) {
    NavBodyView bodyView;
    bodyView.id = body->id;
    bodyView.name = body->name;
    bodyView.type = body->type;
    bodyView.subtype = body->subtype;
    bodyView.klass = body->klass;
    bodyView.worldX = body->x;
    bodyView.worldY = body->y;
    bodyView.at = NavProjectPanned(map, view, body->x, body->y);
    bodyView.size = BodySize(*body);
    bodyView.selected = !target.empty() && target == body->id;
    bodyView.classes = BodyClasses(*body, bodyView.selected);
    bodyView.dotX = bodyView.at.x - bodyView.size / 2;
    bodyView.dotY = bodyView.at.y - bodyView.size / 2;
    bodyView.here = sim.AtTarget(*body);
    bodyView.atTarget = sim.AtTarget(*body);
    bodyView.inRange = sim.InRange(*body);
    auto scannedIt = sim.State().scanned.find(body->id);
    bodyView.scanned = scannedIt != sim.State().scanned.end() && scannedIt->second;

    // Flip the label inward near the right edge or the map's overflow clips it.
    bodyView.labelY = bodyView.at.y - 6;
    const double labelW = 92;
    const bool flip =
        bodyView.at.x > map.vw * 0.78 ||
        (bodyView.at.y < 150 &&
         bodyView.at.x + 10 + std::min(labelW, body->name.size() * 5.5) > map.vw - 76);
    bodyView.labelRight = flip;
    bodyView.labelX = flip ? bodyView.at.x - 100 : bodyView.at.x + 10;
    chart.bodies.push_back(bodyView);
  }

  // --- routes -------------------------------------------------------------
  const auto append = [&chart](std::vector<NavDot> dots) {
    chart.routes.insert(chart.routes.end(), dots.begin(), dots.end());
  };

  if (chart.layer == "system") {
    // Routes are derived: they radiate from wherever the ship actually is, so
    // they stay honest after it moves.
    const Body* here = sim.FindBodyById(map.here);
    for (const Body* body : bodies) {
      if (body->id == map.here || here == nullptr) { continue; }
      append(NavRouteDots(sim, here->x, here->y, body->x, body->y,
                          target == body->id, false));
    }
  } else {
    // Wide-layer lanes are authored context, fainter than the selected course.
    const std::vector<Lane>& lanes =
        chart.layer == "sector" ? sim.World().sectorLanes : sim.World().galaxyLanes;
    const std::vector<Body>& list =
        chart.layer == "sector" ? sim.World().systems : sim.World().regions;
    for (const Lane& lane : lanes) {
      const Body* a = FindIn(list, lane.from);
      const Body* b = FindIn(list, lane.to);
      if (a == nullptr || b == nullptr || a == nullptr) { continue; }
      if (chart.layer == "sector" &&
          (a->region != map.sector || b->region != map.sector)) {
        continue;
      }
      append(NavRouteDots(sim, a->x, a->y, b->x, b->y, false, true));
    }
    if (chart.layer == "sector") {
      const Body* home = sim.HomeBody();
      const Body* targetBody = FindIn(sim.World().systems, target);
      if (home != nullptr && targetBody != nullptr && targetBody->region == map.sector &&
          targetBody->id != home->id) {
        append(NavRouteDots(sim, home->x, home->y, targetBody->x, targetBody->y,
                            true, false));
      }
    } else {
      const Task* trip = sim.FindOngoing("travel");
      const Body* dest = trip == nullptr ? nullptr : FindIn(sim.World().systems, trip->target);
      const Body* home = sim.HomeBody();
      const Body* region = dest == nullptr ? nullptr : FindIn(sim.World().regions, dest->region);
      if (home != nullptr && region != nullptr && home->id != region->id) {
        append(NavRouteDots(sim, home->x, home->y, region->x, region->y, true, false));
      }
    }
  }

  // --- clusters -----------------------------------------------------------
  if (chart.layer == "galaxy" || chart.layer == "sector") {
    double seed = 8721;
    const double count = chart.layer == "galaxy" ? 260 : 108;
    if (chart.layer == "sector") {
      for (std::size_t i = 0; i < sim.World().regions.size(); ++i) {
        if (sim.World().regions[i].id == map.sector) {
          seed += static_cast<double>(i) + 1;
        }
      }
    }
    Seeded rnd(seed);
    for (int i = 0; i < static_cast<int>(count); ++i) {
      const NavPoint point =
          NavProjectPanned(map, view, rnd() * map.w, rnd() * map.h);
      NavDot dot;
      dot.cls = std::string("cdot cdot-far") + (i % 17 == 0 ? " cdot-bright" : "");
      dot.x = js::Round(point.x);
      dot.y = js::Round(point.y);
      chart.clusters.push_back(dot);
    }
  }
  // drawClusters() in the reference walks lay.bodies, NOT curBodies(): a body
  // keeps its cosmetic stars even when the layer's filter hides the body
  // itself, and the scatter is seeded from the index into that array, so the
  // sky does not shift when the selection changes. In the shipped chart both
  // readings agree - only the 7 regions carry a cluster, and on the galaxy
  // layer the filter returns the array untouched - so this is invisible today.
  // It stops being invisible the moment a generated body carries one, which
  // is exactly what the procedural chart will do, so follow the reference here.
  const std::vector<Body>& layer = LayerArray(sim);
  for (std::size_t i = 0; i < layer.size(); ++i) {
    const Body& body = layer[i];
    if (body.cluster == 0) { continue; }
    Seeded rnd(static_cast<double>(i) + 7);
    for (int j = 0; j < body.cluster; ++j) {
      const double a = rnd() * 6.283;
      const double r = 16 + rnd() * 34;
      const NavPoint point = NavProjectPanned(
          map, view, body.x + std::cos(a) * r, body.y + std::sin(a) * r * 0.62);
      NavDot dot;
      dot.cls = "cdot";
      dot.x = js::Round(point.x);
      dot.y = js::Round(point.y);
      chart.clusters.push_back(dot);
    }
  }

  // --- the ship's ring ----------------------------------------------------
  const Body* home = sim.HomeBody();
  if (home != nullptr) {
    NavPoint at{home->x, home->y};
    if (chart.layer == "system") {
      at = TravelMarker(sim);
    } else {
      const Task* trip = sim.FindOngoing("travel");
      const Body* dest = trip == nullptr ? nullptr : FindIn(sim.World().systems, trip->target);
      const Body* markerTarget = nullptr;
      if (chart.layer == "galaxy" && dest != nullptr) {
        markerTarget = FindIn(sim.World().regions, dest->region);
      } else {
        markerTarget = dest;
      }
      if (markerTarget != nullptr &&
          (chart.layer == "galaxy" || dest->region == home->region)) {
        const double part = OngoingPercent(*trip) / 100.0;
        at.x = home->x + (markerTarget->x - home->x) * part;
        at.y = home->y + (markerTarget->y - home->y) * part;
      }
    }
    chart.marker = NavProjectPanned(map, view, at.x, at.y);
    chart.hasMarker = true;
  }

  // --- the scale caption --------------------------------------------------
  std::string where;
  if (chart.layer == "system") {
    const Body* system = sim.CurrentSystem();
    where = std::string(system == nullptr ? "Kessler" : system->name) + " - System";
  } else if (chart.layer == "sector") {
    const Body* region = FindIn(sim.World().regions, map.sector);
    where = std::string(region == nullptr ? "Sector" : region->name) + " - Sector";
  } else {
    where = "Galaxy";
  }
  chart.scaleText = where + " - zoom x" + OneDecimal(chart.zoom);
  return chart;
}

// ---------------------------------------------------------------------------
//  NAVFX
// ---------------------------------------------------------------------------
NavFxFrame BuildNavFx(const Simulation& sim, double tick, bool reducedMotion) {
  NavFxFrame frame;
  if (sim.Map().layer != "system") { return frame; }

  const MapView& map = sim.Map();
  const LayerView& view = ViewOf(sim);
  const std::vector<const Body*> bodies = LayerBodies(sim);

  // Nine drifting rocks, authored from a fixed seed rather than Math.random so
  // the same sky greys out in the same places every time.
  Seeded rnd(4242);
  const double t = reducedMotion ? 0.0 : tick;
  for (int i = 0; i < 9; ++i) {
    NavFxRock rock;
    rock.worldX = rnd() * map.w;
    rock.worldY = 96 + rnd() * 120;
    rock.vx = 0.10 + rnd() * 0.22;
    rock.size = 2 + static_cast<int>(std::floor(rnd() * 3));
    double x = rock.worldX + rock.vx * t;
    if (NavProject(map, x, rock.worldY).x > map.vw + 12) {
      x = map.cx - (map.cx + 20) / map.zoom;
    }
    rock.at = NavProjectPanned(map, view, x, rock.worldY);
    frame.rocks.push_back(rock);
  }

  // The satellite orbits the first station on the chart; the beacon blinks at
  // its masthead.
  const Body* station = nullptr;
  for (const Body* body : bodies) {
    if (body->type == "station") { station = body; break; }
  }
  if (station != nullptr) {
    frame.hasStation = true;
    frame.stationId = station->id;
    const double ang = reducedMotion ? 0.0 : tick * 0.022;
    const double sx = station->x + std::cos(ang) * 17;
    const double sy = station->y + std::sin(ang) * (17 * 0.55);
    frame.satellite = NavProjectPanned(map, view, sx, sy);
    frame.beacon = NavProjectPanned(map, view, station->x, station->y);
    frame.beacon.x -= 1;
    frame.beacon.y -= 9;
    const int phase = static_cast<int>(js::Round(tick));
    frame.beaconOpacity = reducedMotion ? 1.0 : ((phase % 22) < 11 ? 1.0 : 0.15);
  }
  return frame;
}

// ---------------------------------------------------------------------------
//  The screen
// ---------------------------------------------------------------------------
NavScreenModel BuildNavScreen(const Simulation& sim) {
  NavScreenModel model;
  model.chart = BuildNavChart(sim);
  model.fx = BuildNavFx(sim, 0, sim.State().reducedMotion);

  TaskScreenModel& screen = model.screen;
  screen.head.kind = "NAV";
  screen.head.title = "Navigation";
  screen.head.sub = "Select a destination to read it. Reading costs nothing.";

  const MapView& map = sim.Map();
  const Body* here = sim.FindBodyById(map.here);
  const std::string hereName = here == nullptr ? std::string("Kessler") : here->name;
  const Body* system = sim.CurrentSystem();

  std::optional<std::string> targetId = sim.State().navTarget;
  const std::string targetKey = targetId.value_or(std::string());
  const Body* target = targetKey.empty() ? nullptr : sim.FindBodyById(targetKey);
  model.hasTarget = target != nullptr;

  NavPanel name;
  name.id = "nav-name";
  name.title = "Target";
  NavPanel klass;
  klass.id = "nav-class";
  klass.title = "Classification";
  NavPanel status;
  status.id = "nav-status";
  status.title = "Status";
  status.placement = "np-last";
  NavPanel details;
  details.id = "nav-details";
  details.title = "Target Details";
  details.placement = "np-half";
  NavPanel distance;
  distance.id = "nav-dist";
  distance.title = "Distance";
  distance.placement = "np-half-last";
  NavPanel scan;
  scan.id = "nav-scan";
  scan.title = "Scan Data";
  scan.placement = "np-last";

  const Task* running = sim.FindOngoing("travel");
  const Task* scanTask = sim.FindOngoing("scan");

  if (target == nullptr) {
    name.Dim("No target selected");
    klass.Dim("Select a body on the chart.");
    status.Dim("Idle");
    details.Dim("No target.");
    scan.Dim("No scan data.");
    distance.Dim("No route plotted.");
  } else if (target->type == "region") {
    int sectorCount = 0;
    for (const Body& body : sim.World().systems) {
      if (body.region == target->id) { ++sectorCount; }
    }
    name.Name(target->name);
    name.Sub("Galactic region");
    klass.Note(target->klass);
    status.Note(system != nullptr && system->region == target->id ? "Current region"
                                                                 : "Chart available");
    for (const auto& field : target->details) { details.Field(field.first, field.second); }
    details.Field("Systems", Num(sectorCount) + " navigable");
    scan.Note("Open the sector chart to choose a system. Local bodies can be scanned "
              "after arrival.");
    distance.Note("Select a system on the sector chart to plot a jump.");
  } else if (!sim.InRange(*target)) {
    // Looking is always free, even across a galaxy. What the player gets for
    // free is the chart entry - a name and a classification.
    name.Name(target->name);
    name.Sub(std::string("Long range contact - ") + NavLayerName(map.layer) + " chart");
    klass.Note(target->klass);
    status.Note(sim.AtTarget(*target) || target->id == map.here ? "Current position"
                                                                : "Out of sensor range");
    details.Dim(NavOutOfRange());
    scan.Dim(NavOutOfRange());
    distance.Dim(NavOutOfRange());
  } else {
    const Simulation::NavMetrics metrics = sim.MetricsFor(*target);
    name.Name(target->name);
    name.Sub(sim.AtTarget(*target)
                 ? std::string("Current position")
                 : "From " + (target->type == "system"
                                  ? (system == nullptr ? hereName : system->name)
                                  : hereName));
    klass.Note(target->klass);
    status.Note(sim.AtTarget(*target)
                    ? (target->type == "system" ? "Current system" : "Current position")
                    : (running != nullptr ? "Crossing under way" : "Reachable"));

    for (const auto& field : target->details) { details.Field(field.first, field.second); }
    details.Field("Origin", target->type == "system"
                               ? (system == nullptr ? hereName : system->name)
                               : hereName);

    scan.Note(target->type == "system"
                  ? "Jump to this system, then use the System chart to scan its stars "
                    "and other destinations."
                  : std::string());
    if (target->type != "system") { scan.lines = ScanReadout(sim, *target); }

    if (sim.AtTarget(*target)) {
      distance.Dim("You are already here.");
    } else {
      const CrewMember* navigator = NavigatorOf(sim);
      distance.Field("Distance", metrics.ly);
      distance.Field("", metrics.km);
      distance.Field("Transit", metrics.transit);
      distance.Field("Action cost", Plur(metrics.total, "action"));
      distance.Field("Fuel required", Num(metrics.fuel) + "%");
      distance.Field("Navigator",
                     navigator == nullptr
                         ? std::string("None available")
                         : navigator->name + " (-" +
                               Num(NavigatorDiscount(*navigator)) +
                               "% fuel at departure)");
      distance.Field("", "1 to commit, " + Plur(metrics.dist, "action") + " under way");
    }
  }

  screen.left = {name, klass, status};
  screen.under = {details, distance};
  screen.right = {scan};

  // --- the action band ----------------------------------------------------
  const bool reach = target != nullptr && sim.InRange(*target);
  const bool hereTarget = target != nullptr && sim.AtTarget(*target);
  const double energy = sim.StatValue("energy");
  const double fuel = sim.StatValue("fuel");

  screen.bandNote.clear();
  if (reach && !hereTarget) {
    model.courseNote = "Course from " +
                       std::string(target->type == "system"
                                       ? (system == nullptr ? hereName : system->name)
                                       : hereName) +
                       " to " + target->name + ".";
  } else if (target != nullptr && !reach) {
    model.courseNote = target->type == "region"
                           ? "Open this sector chart to plot a jump."
                           : "This contact is outside the local sensor range.";
  } else {
    model.courseNote = "Plot a course by selecting a destination.";
  }

  NavPanel actions;
  actions.id = "nav-actions";
  actions.title = "Target Actions";
  actions.placement = "np-full";
  actions.Note(model.courseNote);

  const bool travelEnabled =
      reach && !hereTarget && !sim.State().docked && sim.State().engineCondition > 0 &&
      energy > 0 && fuel >= (target == nullptr ? 0.0 : sim.MetricsFor(*target).fuel);
  screen.progress.push_back(BuildProgressButton(
      sim, "travel", target != nullptr && target->type == "system" ? "Start Jump"
                                                                  : "Start Travel",
      travelEnabled, "Complete", "space"));

  if (scanTask != nullptr && scanTask->state == "running" && target != nullptr &&
      scanTask->target != target->id) {
    // A scan running on a DIFFERENT body must not look like it belongs to this
    // one. One set of sensors, one scan at a time.
    screen.progress.push_back(
        BuildProgressButton(sim, "scan-busy", "Sensors busy", false, "Scan Complete", "space"));
  } else {
    screen.progress.push_back(BuildProgressButton(
        sim, "scan", "Run Deep Scan",
        reach && target != nullptr && target->type != "system" && energy > 0,
        "Scan Complete", "space"));
  }

  if (target != nullptr && target->type == "region") {
    screen.commands.push_back(BuildCommandRow(
        sim, "View " + target->name + " sector chart", true, "free", std::string(),
        0));
  }
  screen.commands.push_back(BuildCommandRow(
      sim, "Stand watch - advance ongoing work", sim.State().gameOver.empty(),
      std::string(), "space", 0));

  const Port* port = sim.World().FindPort(sim.Map().here);
  if (port != nullptr && !sim.State().docked && running == nullptr) {
    screen.commands.push_back(BuildCommandRow(
        sim, "Dock at " + port->name, true, std::string(), "space", 0));
  }

  // The band's two notes: what standing watch costs, then why a button may be
  // off.
  model.notes.push_back(
      "Standing watch spends one action and needs no item; work and survival "
      "clocks advance, with normal resource use.");
  if (energy <= 0) {
    model.notes.push_back("Restore energy to use navigation and sensors.");
  } else if (sim.State().docked) {
    model.notes.push_back("Undock at the station before plotting a crossing.");
  } else if (sim.State().engineCondition <= 0) {
    model.notes.push_back("The drive needs maintenance before travel.");
  } else if (reach && !hereTarget && target != nullptr &&
             fuel < sim.MetricsFor(*target).fuel) {
    model.notes.push_back("Insufficient fuel. Right-click the fuel gauge to refuel.");
  } else if (running != nullptr) {
    model.notes.push_back("A crossing is already under way.");
  } else {
    model.notes.push_back(
        "Committing costs one action now, then the distance in actions spent "
        "elsewhere aboard the ship. A deep scan costs one action now and " +
        Num(SimScanCost(sim)) + " spent elsewhere.");
  }
  for (const std::string& note : model.notes) { actions.Dim(note); }
  screen.band = {actions};

  screen.hasStage = true;
  screen.stage.id = "map";
  screen.stage.width = static_cast<float>(map.vw);
  screen.stage.height = static_cast<float>(map.vh);
  return model;
}

// ---------------------------------------------------------------------------
//  Clicks
// ---------------------------------------------------------------------------
namespace {
bool PressNavActionRaw(Simulation& sim, const std::string& action, const std::string& arg) {
  if (action == "travel") { return PressProgressButton(sim, "travel"); }
  if (action == "scan") { return PressProgressButton(sim, "scan"); }
  if (action == "stand-watch") { return sim.StandWatch(); }
  if (action == "dock") { return sim.Dock(); }
  if (action == "view-sector") { return sim.ViewSector(arg); }
  if (action == "layer") {
    if (arg != "system" && arg != "sector" && arg != "galaxy") { return false; }
    sim.SetLayer(arg);
    return true;
  }
  if (action == "select") { return sim.SelectTarget(arg); }
  if (action == "zoom-in") { return sim.ZoomMap(1); }
  if (action == "zoom-out") { return sim.ZoomMap(-1); }
  if (action == "pan") {
    // "pan:<dx>,<dy>" — free, and clamped per layer.
    const std::size_t comma = arg.find(',');
    if (comma == std::string::npos) { return false; }
    sim.PanMap(std::stod(arg.substr(0, comma)), std::stod(arg.substr(comma + 1)));
    return true;
  }
  return false;
}

}  // namespace

// ============================================================================
//  The press, with the anti-spam lock around it (Phase 9).
//  ---------------------------------------------------------------------------
//  One control is one control. The first press spends the action and starts the
//  tier's lock in ACTION_COOLDOWN_RULES; a second press of the same button
//  before another action resolves is refused, whether it comes from the window
//  or from this harness. Free reads spend nothing and therefore lock nothing.
// ============================================================================
bool PressNavAction(Simulation& sim, const std::string& action, const std::string& arg) {
  // Progress controls acknowledge before consulting cooldowns (HTA
  // onProgressClick). Do not add a second lock around that dispatcher.
  const auto screen = BuildNavScreen(sim).screen;
  if (action == "travel" || action == "scan") {
    for (const auto& button : screen.progress) {
      if (button.taskId == action) { return PressProgressButton(sim, button); }
    }
    return false;
  }
  for (const auto& row : screen.commands) {
    const bool matches =
        (action == "stand-watch" && row.label.find("Stand watch") == 0) ||
        (action == "dock" && row.label.find("Dock at ") == 0) ||
        (action == "view-sector" && row.label.find("View ") == 0);
    if (!matches) { continue; }
    if (!row.enabled || row.coolingDown) { return false; }
    return CommitActionButton(sim, row.cooldownKey, row.category,
        [&]() { return PressNavActionRaw(sim, action, arg); });
  }
  if (action == "stand-watch" || action == "dock" || action == "view-sector") {
    return false;
  }
  return PressNavActionRaw(sim, action, arg);
}

}  // namespace ship

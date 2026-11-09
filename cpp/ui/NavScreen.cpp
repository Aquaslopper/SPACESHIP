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
//
// Every body of the scale is here - nothing is held back by a region or a
// sensor filter. The sector chart used to show only the systems of the region
// being read, which is seven of forty-eight; with the galaxy generated, that
// filter would hide a hundred and fifty stars behind a button. What a body can
// be *used for* is a separate question, and InRange() still answers it.
//
// The system chart is the one place a filter is not a filter: every system's
// local bodies live in one coordinate space, so "this system's bodies" is the
// only set that is spatially meaningful at that scale.
std::vector<const Body*> LayerBodies(const Simulation& sim) {
  std::vector<const Body*> out;
  const Catalog& catalog = sim.World();
  const std::string layer = BodyLayer(sim);
  if (layer == "galaxy") {
    // The regions, and then every star inside them. The galaxy scale used to
    // carry seven named blobs; the systems have positions of their own now
    // (Catalog::galaxyPositions), so the third layer shows the same two hundred
    // stars the other two layers are drawing.
    for (const Body& body : catalog.regions) { out.push_back(&body); }
    for (const Body& body : catalog.systems) {
      if (catalog.galaxyPositions.count(body.id) != 0) { out.push_back(&body); }
    }
    return out;
  }
  if (layer == "system") {
    const auto& indices = catalog.BodiesInSystem(sim.CurrentSystemId());
    out.reserve(indices.size());
    for (std::size_t idx : indices) {
      if (idx < catalog.bodies.size()) { out.push_back(&catalog.bodies[idx]); }
    }
    return out;
  }
  out.reserve(catalog.systems.size());
  for (const Body& body : catalog.systems) { out.push_back(&body); }
  return out;
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
  if (!sim.CanScan(body)) {
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
  const int tier = sim.SensorTier();
  for (std::size_t i = 0; i < body.scanLocked.size(); ++i) {
    const std::string& line = body.scanLocked[i];
    if (tier >= 2 || (tier == 1 && i == 0)) {
      lines.push_back(PanelLine{PanelLine::Kind::Note, "",
                                line + (tier >= 2 ? " - deep-array verified"
                                                  : " - upgraded sensor lock"),
                                "", 0.0});
    } else {
      lines.push_back(PanelLine{PanelLine::Kind::Dim, "",
                                line + (tier == 1 ? " - deep-array sensors required"
                                                  : " - sensors insufficient"),
                                "", 0.0});
    }
  }
  if (sim.CanExtractBody(body)) {
    const bool depleted = sim.ExtractionPassesUsed(body) >= sim.ExtractionMaxPasses(body);
    lines.push_back(PanelLine{depleted ? PanelLine::Kind::Dim : PanelLine::Kind::Note,
                              "", sim.ExtractionScanSummary(body), "", 0.0});
  }
  if (body.type == "system") {
    const auto& localIdx = sim.World().BodiesInSystem(body.id);
    int ports = 0;
    int deposits = 0;
    for (std::size_t idx : localIdx) {
      if (idx >= sim.World().bodies.size()) { continue; }
      const Body& local = sim.World().bodies[idx];
      if (sim.World().FindPort(local.id) != nullptr) { ++ports; }
      if (sim.CanExtractBody(local)) { ++deposits; }
    }
    lines.push_back(PanelLine{
        PanelLine::Kind::Note, "",
        "Remote survey: " + Num(localIdx.size()) + " charted bodies, " +
            Num(ports) + (ports == 1 ? " port, " : " ports, ") +
            Num(deposits) + " extractable deposits.",
        "", 0.0});
  } else if (body.type == "region") {
    const auto& sysIdx = sim.World().SystemsInRegion(body.id);
    lines.push_back(PanelLine{
        PanelLine::Kind::Note, "",
        "Deep-region cluster telemetry: " + Num(sysIdx.size()) +
            " navigable star systems catalogued.",
        "", 0.0});
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

  // Skip faint lane segments whose bounding box lies completely outside the viewport.
  if (faint && !selected) {
    const double pad = 48.0;
    const double minX = std::min(a.x, b.x);
    const double maxX = std::max(a.x, b.x);
    const double minY = std::min(a.y, b.y);
    const double maxY = std::max(a.y, b.y);
    if (maxX < -pad || minX > map.vw + pad || maxY < -pad || minY > map.vh + pad) {
      return {};
    }
  }

  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len = std::sqrt(dx * dx + dy * dy);
  const double boardScale = std::max(1.0, map.vw / 638.0);
  const double gap = (selected ? 8.0 : (faint ? 15.0 : 11.0)) * boardScale;
  double n = std::floor(len / gap);
  if (n > 90) { n = 90; }

  std::vector<NavDot> dots;
  if (n > 1) { dots.reserve(static_cast<std::size_t>(n) - 1); }
  const char* cls = selected ? "rdot rdot-sel" : (faint ? "rdot rdot-faint" : "rdot");
  for (int i = 1; i < static_cast<int>(n); ++i) {
    const double t = i / n;
    const double px = js::Round(a.x + dx * t);
    const double py = js::Round(a.y + dy * t);
    if (faint && (px < -16.0 || px > map.vw + 16.0 || py < -16.0 || py > map.vh + 16.0)) {
      continue;
    }
    dots.push_back(NavDot{px, py, cls});
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
  // A system has two positions: its own on the sector board, and its place in
  // its region's cluster on the galaxy board. Everything that projects a body
  // asks this, so the two wide scales cannot disagree about where a star is.
  const auto layerPoint = [&sim, &chart](const Body& body) -> std::pair<double, double> {
    if (chart.layer == "galaxy" && body.type == "system") {
      const auto it = sim.World().galaxyPositions.find(body.id);
      if (it != sim.World().galaxyPositions.end()) { return it->second; }
    }
    return {body.x, body.y};
  };
  for (const Body* body : bodies) {
    NavBodyView bodyView;
    bodyView.id = body->id;
    bodyView.name = body->name;
    bodyView.type = body->type;
    bodyView.subtype = body->subtype;
    bodyView.klass = body->klass;
    bodyView.region = body->region;
    const std::pair<double, double> at = layerPoint(*body);
    // Only what the window can show is built. At two thousand systems the chart
    // used to project and label every one of them every frame, most of them far
    // outside the board.
    const NavPoint projected = NavProjectPanned(map, view, at.first, at.second);
    const double pad = 180.0;
    if (projected.x < -pad || projected.x > map.vw + pad || projected.y < -pad ||
        projected.y > map.vh + pad) {
      continue;
    }
    bodyView.worldX = at.first;
    bodyView.worldY = at.second;
    bodyView.at = projected;
    bodyView.size = BodySize(*body);
    // A star is a star on its own chart; on the galaxy scale it is one point of
    // light in a cluster of thirty, so it is drawn at the size that reads.
    if (chart.layer == "galaxy" && body->type == "system") { bodyView.size = 10; }
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

    // The trade pathfinder: every trade taken with a destination in this system
    // gets a green trail from wherever the ship is, so the board and the chart
    // say the same thing without the player digging through a list.
    if (here != nullptr) {
      const std::string systemId = sim.CurrentSystemId();
      for (const TradeContract& trade : sim.State().trades) {
        if (trade.mode == "station") { continue; }  // its route is a sector one
        const Body* destination = sim.FindBodyById(trade.to);
        if (destination == nullptr || destination->id == here->id) { continue; }
        if (sim.World().BodySystem(*destination) != systemId) { continue; }
        std::vector<NavDot> trail =
            NavRouteDots(sim, here->x, here->y, destination->x, destination->y, false, false);
        for (NavDot& dot : trail) { dot.cls = "rdot rdot-trade"; }
        append(std::move(trail));
      }
    }
  } else {
    const Catalog& world = sim.World();
    const bool isSector = (chart.layer == "sector");
    const std::vector<Lane>& lanes = isSector ? world.sectorLanes : world.galaxyLanes;
    for (const Lane& lane : lanes) {
      const Body* a = isSector ? world.FindSystem(lane.from) : world.FindRegion(lane.from);
      const Body* b = isSector ? world.FindSystem(lane.to) : world.FindRegion(lane.to);
      if (a == nullptr || b == nullptr) { continue; }
      append(NavRouteDots(sim, a->x, a->y, b->x, b->y, false, true));
    }
    if (isSector) {
      const Body* home = sim.HomeBody();
      const Body* targetBody = world.FindSystem(target);
      if (home != nullptr && targetBody != nullptr && targetBody->id != home->id) {
        append(NavRouteDots(sim, home->x, home->y, targetBody->x, targetBody->y,
                            true, false));
      }
    } else {
      const Task* trip = sim.FindOngoing("travel");
      const Body* dest = trip == nullptr ? nullptr : world.FindSystem(trip->target);
      const Body* home = sim.HomeBody();
      const Body* region = dest == nullptr ? nullptr : world.FindRegion(dest->region);
      const Body* selRegion = world.FindRegion(target);
      if (home != nullptr && selRegion != nullptr && home->id != selRegion->id) {
        append(NavRouteDots(sim, home->x, home->y, selRegion->x, selRegion->y, true, false));
      } else if (home != nullptr && region != nullptr && home->id != region->id) {
        append(NavRouteDots(sim, home->x, home->y, region->x, region->y, true, false));
      }
    }
  }

  // --- clusters -----------------------------------------------------------
  {
    double seed = 8721;
    int count = 420;
    if (chart.layer == "galaxy") { count = 720; }
    if (chart.layer == "sector") {
      count = 960;
      for (std::size_t i = 0; i < sim.World().regions.size(); ++i) {
        if (sim.World().regions[i].id == map.sector) {
          seed += static_cast<double>(i) + 1;
        }
      }
    }
    chart.clusters.reserve(static_cast<std::size_t>(count) + 256);
    Seeded rnd(seed);
    for (int i = 0; i < count; ++i) {
      const NavPoint point =
          NavProjectPanned(map, view, rnd() * map.w, rnd() * map.h);
      const double px = js::Round(point.x);
      const double py = js::Round(point.y);
      if (px < -24.0 || px > map.vw + 24.0 || py < -24.0 || py > map.vh + 24.0) { continue; }
      chart.clusters.push_back(NavDot{
          px, py,
          i % 17 == 0 ? "cdot cdot-far cdot-bright" : "cdot cdot-far"});
    }
  }
  for (std::size_t i = 0; i < bodies.size(); ++i) {
    const Body* body = bodies[i];
    if (body->cluster == 0) { continue; }
    Seeded rnd(static_cast<double>(i) + 7);
    for (int j = 0; j < body->cluster; ++j) {
      const double a = rnd() * 6.283;
      const double r = 16 + rnd() * 34;
      const NavPoint point = NavProjectPanned(
          map, view, body->x + std::cos(a) * r, body->y + std::sin(a) * r * 0.62);
      const double px = js::Round(point.x);
      const double py = js::Round(point.y);
      if (px < -24.0 || px > map.vw + 24.0 || py < -24.0 || py > map.vh + 24.0) { continue; }
      chart.clusters.push_back(NavDot{px, py, "cdot"});
    }
  }

  // --- the ship's ring ----------------------------------------------------
  const Body* home = sim.HomeBody();
  if (home != nullptr) {
    const std::pair<double, double> homeAt = layerPoint(*home);
    NavPoint at{homeAt.first, homeAt.second};
    if (chart.layer == "system") {
      at = TravelMarker(sim);
    } else {
      const Task* trip = sim.FindOngoing("travel");
      const Body* dest = trip == nullptr ? nullptr : sim.World().FindSystem(trip->target);
      const Body* markerTarget = nullptr;
      if (chart.layer == "galaxy" && dest != nullptr) {
        markerTarget = sim.World().FindRegion(dest->region);
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
    const Body* region = sim.World().FindRegion(map.sector);
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
  const Task* extractTask = sim.FindOngoing("extract");

  const auto appendDepositsField = [&sim](NavPanel& panel, const Body& body) {
    if (!sim.CanExtractBody(body)) { return; }
    const int used = sim.ExtractionPassesUsed(body);
    const int maxPasses = sim.ExtractionMaxPasses(body);
    panel.Field("Deposits", used >= maxPasses ? std::string("Depleted") : std::string("Available"));
  };

  if (target == nullptr) {
    name.Dim("No target selected");
    klass.Dim("Select a body on the chart.");
    status.Dim("Idle");
    details.Dim("No target.");
    scan.Dim("No scan data.");
    distance.Dim("No route plotted.");
  } else if (target->type == "region") {
    const int sectorCount =
        static_cast<int>(sim.World().SystemsInRegion(target->id).size());
    name.Name(target->name);
    name.Sub("Galactic region");
    klass.Note(target->klass);
    status.Note(system != nullptr && system->region == target->id ? "Current region"
                                                                 : "Chart available");
    for (const auto& field : target->details) { details.Field(field.first, field.second); }
    details.Field("Systems", Num(sectorCount) + " navigable");
    if (sim.CanScan(*target)) {
      scan.lines = ScanReadout(sim, *target);
    } else {
      scan.Note("Open the sector chart to choose a system. Local bodies can be scanned "
                "after arrival.");
    }
    distance.Note("Select a system on the sector chart to plot a jump.");
  } else if (!sim.InRange(*target)) {
    name.Name(target->name);
    name.Sub(std::string("Long range contact - ") + NavLayerName(map.layer) + " chart");
    klass.Note(target->klass);
    if (sim.CanScan(*target)) {
      status.Note("Long-range sensor lock");
      for (const auto& field : target->details) { details.Field(field.first, field.second); }
      appendDepositsField(details, *target);
      scan.lines = ScanReadout(sim, *target);
      distance.Note("Jump to its parent system before plotting local approach.");
    } else {
      status.Note(sim.AtTarget(*target) || target->id == map.here ? "Current position"
                                                                  : "Out of sensor range");
      details.Dim(NavOutOfRange());
      scan.Dim(NavOutOfRange());
      distance.Dim(NavOutOfRange());
    }
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
    appendDepositsField(details, *target);

    if (target->type == "system" && !sim.CanScan(*target)) {
      scan.Note("Jump to this system, then use the System chart to scan its stars "
                "and other destinations.");
    } else {
      scan.lines = ScanReadout(sim, *target);
    }

    if (sim.AtTarget(*target)) {
      distance.Dim("You are already here.");
    } else {
      const CrewMember* navigator = NavigatorOf(sim);
      distance.Field("Distance", metrics.ly);
      distance.Field("", metrics.km);
      distance.Field("Transit", metrics.transit);
      distance.Field("Click cost", Plur(metrics.total, "action"));
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
  const bool canScanTarget = target != nullptr && sim.CanScan(*target);
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
                           : (canScanTarget
                                  ? "Long-range sensor lock acquired. Jump to parent system to visit."
                                  : "This contact is outside the local sensor range.");
  } else {
    model.courseNote = "Plot a course by selecting a destination.";
  }

  NavPanel actions;
  actions.id = "nav-actions";
  actions.title = "Target Actions";
  actions.placement = "np-full";
  actions.Note(model.courseNote);

  const bool travelEnabled =
      reach && !hereTarget && sim.State().engineCondition > 0 &&
      extractTask == nullptr && energy > 0 &&
      fuel >= (target == nullptr ? 0.0 : sim.MetricsFor(*target).fuel);
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
    const bool remoteScan = target != nullptr &&
                            (!reach || target->type == "system" || target->type == "region");
    screen.progress.push_back(BuildProgressButton(
        sim, "scan", remoteScan ? "Run Long-Range Scan" : "Run Deep Scan",
        canScanTarget && energy > 0,
        "Scan Complete", "space"));
  }

  const bool showExtract =
      (target != nullptr && sim.CanExtractBody(*target)) || extractTask != nullptr;
  if (showExtract) {
    if (extractTask != nullptr && target != nullptr && extractTask->target != target->id) {
      screen.progress.push_back(BuildProgressButton(
          sim, "extract-busy", "Extraction rig busy", false, "Haul Yield", "routine"));
    } else {
      const Body* extBody =
          (extractTask != nullptr) ? sim.FindBodyById(extractTask->target) : target;
      const std::string idleLabel =
          extBody != nullptr ? sim.ExtractionActionLabel(*extBody) : "Extract resources";
      const std::string doneLabel =
          extBody != nullptr ? sim.ExtractionDoneLabel(*extBody) : "Haul Yield";
      const double needVol = extBody != nullptr ? sim.ExtractionReserveVol(*extBody) : 0.0;
      const bool holdFits =
          needVol <= 0.0 ||
          (sim.CargoUsed() + sim.CargoReserved() + needVol <= sim.CargoStore().capacity);
      const bool extractEnabled =
          target != nullptr && sim.CanExtractBody(*target) && hereTarget &&
          !sim.State().docked && running == nullptr && energy > 0 && holdFits &&
          sim.ExtractionPassesUsed(*target) < sim.ExtractionMaxPasses(*target);
      screen.progress.push_back(BuildProgressButton(
          sim, "extract", idleLabel, extractEnabled, doneLabel, "routine"));
    }
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
      "Standing watch spends one click and needs no item; work and survival "
      "clocks advance, with normal resource use.");
  if (energy <= 0) {
    model.notes.push_back("Restore energy to use navigation and sensors.");
  } else if (sim.State().engineCondition <= 0) {
    model.notes.push_back("The drive needs maintenance before travel.");
  } else if (reach && !hereTarget && target != nullptr &&
             fuel < sim.MetricsFor(*target).fuel) {
    model.notes.push_back("Insufficient fuel. Right-click the fuel gauge to refuel.");
  } else if (running != nullptr) {
    model.notes.push_back("A crossing is already under way.");
  } else {
    model.notes.push_back(
        "Committing costs one click now, then the distance in clicks spent "
        "elsewhere aboard the ship. A deep scan costs one click now and " +
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
  if (action == "extract") { return PressProgressButton(sim, "extract"); }
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
  if (action == "travel" || action == "scan" || action == "extract") {
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

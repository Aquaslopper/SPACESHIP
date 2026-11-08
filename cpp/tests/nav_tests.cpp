// ============================================================================
//  nav_tests.cpp — Phase 4: the chart, its raster, its panels, its two
//  commitments.
//  ---------------------------------------------------------------------------
//  Everything on the Navigation screen that can be checked without a window is
//  checked here: the projection pair, the zoom ladder and its clamps, the depth
//  raster, the body sizes and label flips, the scanned/unscanned readouts, and
//  the exact panel strings the reference prints. The last case drives a whole
//  voyage through the same PressNavAction() a click reaches.
// ============================================================================
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/Simulation.h"
#include "data/Catalog.h"
#include "core/JsNumber.h"
#include "data/ChartExpansion.h"
#include "ui/NavScreen.h"
#include "ui/Shell.h"
#include "ui/StationScreen.h"
#include "ui/Widgets.h"

namespace {

ship::Catalog& World() {
  static ship::Catalog catalog =
      ship::Catalog::LoadFromAssets(ship::Catalog::DefaultAssetDir());
  return catalog;
}

ship::Simulation Fresh() { return ship::Simulation::ReferenceVoyage(World()); }

const ship::NavPanel* Panel(const std::vector<ship::NavPanel>& panels,
                            const std::string& id) {
  for (const ship::NavPanel& panel : panels) {
    if (panel.id == id) { return &panel; }
  }
  return nullptr;
}

const ship::PanelLine* Line(const ship::NavPanel& panel, const std::string& key) {
  for (const ship::PanelLine& line : panel.lines) {
    if (line.key == key) { return &line; }
  }
  return nullptr;
}

bool HasText(const ship::NavPanel& panel, const std::string& text) {
  for (const ship::PanelLine& line : panel.lines) {
    if (line.value == text) { return true; }
  }
  return false;
}

const ship::NavBodyView* Body(const ship::NavChartView& chart,
                              const std::string& id) {
  for (const ship::NavBodyView& body : chart.bodies) {
    if (body.id == id) { return &body; }
  }
  return nullptr;
}

const ship::ProgressButton* Button(const ship::TaskScreenModel& screen,
                                   const std::string& taskId) {
  for (const ship::ProgressButton& button : screen.progress) {
    if (button.taskId == taskId) { return &button; }
  }
  return nullptr;
}

bool HasCommand(const ship::TaskScreenModel& screen, const std::string& label) {
  for (const ship::CommandRowView& row : screen.commands) {
    if (row.label == label) { return true; }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------
TEST_CASE("the chart projects through the reference's mapX and mapY", "[nav]") {
  ship::Simulation sim = Fresh();
  const ship::MapView& map = sim.Map();

  // The authored world and the live box agree at zoom 1, so projection is the
  // identity: the dot lands exactly on its coordinate.
  ship::NavPoint kessler = ship::NavProject(map, 210, 150);
  CHECK(kessler.x == 210);
  CHECK(kessler.y == 150);

  // Zoom about the centre of the world (319, 139), then stretch onto the box.
  ship::MapView twice = map;
  twice.zoom = 2;
  const ship::NavPoint zoomed = ship::NavProject(twice, 210, 150);
  CHECK(zoomed.x == 319 + (210 - 319) * 2);
  CHECK(zoomed.y == 139 + (150 - 139) * 2);

  // ...and the stretch is independent of the zoom.
  ship::MapView wide = map;
  wide.vw = 1276;
  const ship::NavPoint stretched = ship::NavProject(wide, 210, 150);
  CHECK(stretched.x == 420);
  CHECK(stretched.y == 150);

  // Pan is a plain offset on top, and each layer keeps its own.
  ship::LayerView view;
  view.px = 40;
  view.py = -12;
  const ship::NavPoint panned = ship::NavProjectPanned(map, view, 210, 150);
  CHECK(panned.x == 250);
  CHECK(panned.y == 138);
}

TEST_CASE("the zoom ladder is clamped to a tenth and to its ends", "[nav]") {
  CHECK(ship::NavZoomStep(1.0, 1, 0.6, 2.0, 0.2) == 1.2);
  CHECK(ship::NavZoomStep(1.0, -1, 0.6, 2.0, 0.2) == 0.8);
  CHECK(ship::NavZoomStep(1.9, 1, 0.6, 2.0, 0.2) == 2.0);
  CHECK(ship::NavZoomStep(0.7, -1, 0.6, 2.0, 0.2) == 0.6);

  // The 3828x1668 wide sector and galaxy boards span 24 clusters and zoom up
  // to 10.0x so individual sectors open cleanly without label overlap.
  CHECK(ship::kZoomMin == 0.3);
  CHECK(ship::kZoomMax == 10.0);

  ship::Simulation sim = Fresh();
  CHECK(sim.Map().zmin == ship::kSystemZoomMin);
  CHECK(sim.Map().zmax == ship::kSystemZoomMax);
  for (int i = 0; i < 30; ++i) { sim.ZoomMap(1); }
  CHECK(sim.Map().zoom == ship::kSystemZoomMax);
  for (int i = 0; i < 40; ++i) { sim.ZoomMap(-1); }
  CHECK(sim.Map().zoom == ship::kSystemZoomMin);

  // The sector and galaxy scales go all the way out and all the way in.
  sim.SetLayer("sector");
  CHECK(sim.Map().zmin == ship::kZoomMin);
  CHECK(sim.Map().zmax == ship::kZoomMax);
  for (int i = 0; i < 60; ++i) { sim.ZoomMap(1); }
  CHECK(sim.Map().zoom == ship::kZoomMax);

  // Free: looking never costs an action.
  CHECK(sim.ActionCount() == 0);
}

TEST_CASE("pan is clamped to half a screen and per layer", "[nav]") {
  CHECK(ship::NavClampPan(500, 638) == 319);
  CHECK(ship::NavClampPan(-500, 638) == -319);
  CHECK(ship::NavClampPan(12, 638) == 12);

  ship::Simulation sim = Fresh();
  sim.PanMap(1000, 1000);
  CHECK(sim.Views().at("system").px == 319);
  CHECK(sim.Views().at("system").py == 139);

  sim.PanMap(-5000, -5000);
  CHECK(sim.Views().at("system").px == -319);
  CHECK(sim.Views().at("system").py == -139);

  // The sector layer was not dragged and is exactly where it was left.
  CHECK(sim.Views().at("sector").px == 0);
  CHECK(sim.ActionCount() == 0);
}

TEST_CASE("the perspective raster bunches toward the far edge", "[nav]") {
  ship::Simulation sim = Fresh();
  const ship::NavRaster raster = ship::BuildNavRaster(sim);

  CHECK(raster.width == 638);
  CHECK(raster.height == 278);
  CHECK(raster.vx == 319);
  CHECK(raster.vy == -97.3);  // held above the top edge, out of sight

  // rungs = round(9 / zoom), clamped to 4..14.
  REQUIRE(raster.rungs.size() == 9);
  const double f1 = std::pow(1.0 / 10.0, 2.1);
  CHECK(raster.rungs[0].y == std::round(f1 * 278));
  CHECK(std::abs(raster.rungs[0].alpha - (0.06 + 0.12 * f1)) < 1e-9);
  const double f9 = std::pow(9.0 / 10.0, 2.1);
  CHECK(raster.rungs[8].y == std::round(f9 * 278));
  CHECK(raster.rungs[8].alpha > raster.rungs[0].alpha);  // far faint, near solid

  // The fan: pitch never narrower than a thirteenth of the width, and the
  // number of gradient layers is bounded at fifteen.
  REQUIRE(raster.lines.size() == 15);
  const ship::NavRasterLine apex = ship::NavRasterThrough(638, 278, 319, -97.3, 319, 278);
  CHECK(apex.angle == 90);  // a vertical hairline: atan2(1, 0)

  // Zoomed in, the rungs thin out and the fan widens with them.
  while (sim.Map().zoom < 2.0) { sim.ZoomMap(1); }
  const ship::NavRaster near = ship::BuildNavRaster(sim);
  CHECK(near.rungs.size() == 5);  // round(4.5) is 5, as Math.round is
  CHECK(near.lines.size() == 13);
}

// ---------------------------------------------------------------------------
//  Layers and bodies
// ---------------------------------------------------------------------------
TEST_CASE("three layers, each remembering its own view", "[nav]") {
  CHECK(ship::NavLayerOrder().size() == 3);
  CHECK(ship::NavLayerLabel("system") == "SYSTEM");
  CHECK(ship::NavLayerLabel("sector") == "SECTOR");
  CHECK(ship::NavLayerLabel("galaxy") == "GALAXY");
  CHECK(ship::NavLayerName("sector") == "Sector");

  ship::Simulation sim = Fresh();
  CHECK(sim.Map().layer == "system");

  std::size_t kesslerBodies = 0;
  for (const ship::Body& body : sim.World().bodies) {
    if (sim.World().BodySystem(body) == sim.CurrentSystemId()) { ++kesslerBodies; }
  }
  const ship::NavChartView system = ship::BuildNavChart(sim);
  CHECK(system.layer == "system");
  CHECK(system.bodies.size() == kesslerBodies);
  CHECK(system.scaleText == "Kessler System - System - zoom x1.0");

  // Looking is free, and a layer keeps its own selection.
  sim.SetLayer("sector");
  CHECK(sim.Map().layer == "sector");
  CHECK_FALSE(sim.State().navTarget.has_value());
  CHECK(sim.SelectTarget("sec-vance"));
  sim.SetLayer("galaxy");
  CHECK(sim.Map().layer == "galaxy");
  CHECK_FALSE(sim.State().navTarget.has_value());
  sim.SetLayer("sector");
  CHECK(sim.State().navTarget.value_or("") == "sec-vance");
  CHECK(sim.ActionCount() == 0);

  // Viewing a region is the same free switch, and it prefills the selection
  // when the ship is inside that sector.
  sim.SetLayer("galaxy");
  CHECK(sim.ViewSector("gal-inner"));
  CHECK(sim.Map().layer == "sector");
  CHECK(sim.State().navTarget.value_or("") == "sec-kessler");
  CHECK(sim.ActionCount() == 0);
}

TEST_CASE("every body is drawn at its own size, with its label on the right side",
          "[nav]") {
  ship::Simulation sim = Fresh();
  const ship::NavChartView chart = ship::BuildNavChart(sim);
  std::size_t localBodies = 0;
  for (const ship::Body& body : sim.World().bodies) {
    if (sim.World().BodySystem(body) == sim.CurrentSystemId()) { ++localBodies; }
  }
  REQUIRE(chart.bodies.size() == localBodies);

  const ship::NavBodyView* origin = Body(chart, "kessler");
  REQUIRE(origin != nullptr);
  CHECK(origin->size == 14);
  CHECK(origin->dotX == 210 - 7);
  CHECK(origin->dotY == 150 - 7);
  CHECK(origin->labelX == 220);  // parked, so the label runs inward from the right
  CHECK_FALSE(origin->labelRight);
  CHECK(origin->atTarget);
  CHECK(origin->classes == "mapdot");

  const ship::NavBodyView* gas = Body(chart, "kessler-outer");
  REQUIRE(gas != nullptr);
  CHECK(gas->size == 20);
  CHECK(gas->classes == "mapdot mapdot-planet mapdot-gas");

  const ship::NavBodyView* moon = Body(chart, "kessler-moon");
  REQUIRE(moon != nullptr);
  CHECK(moon->size == 12);
  CHECK(moon->classes == "mapdot mapdot-moon");

  const ship::NavBodyView* station = Body(chart, "tolme");
  REQUIRE(station != nullptr);
  CHECK(station->size == 18);
  CHECK(station->classes == "mapdot mapdot-station");
  // Right of 0.78 of the width: the label flips inward so the overflow
  // cannot clip it. 545 > 638 * 0.78 = 497.6
  CHECK(station->labelRight);
  CHECK(station->labelX == 445);
  CHECK(station->labelY == 199);

  // System and region sizes, on their own scales.
  // The sector chart holds every charted system: no region filter, no sensor
  // filter. A filter here is what hid most of the galaxy behind a button.
  sim.SetLayer("sector");
  const ship::NavChartView sector = ship::BuildNavChart(sim);
  CHECK(sector.bodies.size() == sim.World().systems.size());
  CHECK(sector.bodies.size() > 150u);
  const ship::NavBodyView* kesslerSystem = Body(sector, "sec-kessler");
  REQUIRE(kesslerSystem != nullptr);
  CHECK(kesslerSystem->size == 18);
  CHECK(kesslerSystem->classes == "mapdot mapdot-sys");
  CHECK(sector.scaleText == "Inner Rim - Sector - zoom x1.0");

  sim.SetLayer("galaxy");
  const ship::NavChartView galaxy = ship::BuildNavChart(sim);
  // Seven regions, and every star inside them: the third scale draws the same
  // systems the other two do, at their galaxy positions.
  CHECK(galaxy.bodies.size() == sim.World().regions.size() + sim.World().systems.size());
  const ship::NavBodyView* core = Body(galaxy, "gal-core");
  REQUIRE(core != nullptr);
  CHECK(core->size == 22);
  const ship::NavBodyView* starInGalaxy = Body(galaxy, "sec-vance");
  REQUIRE(starInGalaxy != nullptr);
  CHECK(starInGalaxy->size == 10);  // one point of light, not a system-sized orb
  CHECK(core->classes == "mapdot mapdot-region");
  CHECK(galaxy.scaleText == "Galaxy - zoom x1.0");
}

TEST_CASE("routes are dot trails that radiate from where the ship is", "[nav]") {
  ship::Simulation sim = Fresh();
  ship::NavChartView chart = ship::BuildNavChart(sim);
  CHECK_FALSE(chart.routes.empty());
  for (const ship::NavDot& dot : chart.routes) { CHECK(dot.cls == "rdot"); }

  // Selecting a target recolours exactly one course.
  REQUIRE(sim.SelectTarget("tolme"));
  chart = ship::BuildNavChart(sim);
  int selected = 0;
  for (const ship::NavDot& dot : chart.routes) {
    if (dot.cls == "rdot rdot-sel") { ++selected; }
  }
  CHECK(selected > 0);
  const ship::NavBodyView* station = Body(chart, "tolme");
  REQUIRE(station != nullptr);
  CHECK(station->selected);
  CHECK(station->classes.find("mapdot-sel") != std::string::npos);

  // The wide layers draw authored lanes faintly, and the ship's ring still
  // marks where the ship is inside them.
  sim.SetLayer("sector");
  const ship::NavChartView sector = ship::BuildNavChart(sim);
  CHECK(sector.hasMarker);
  bool faint = false;
  for (const ship::NavDot& dot : sector.routes) {
    if (dot.cls == "rdot rdot-faint") { faint = true; }
  }
  CHECK(faint);

  // The galaxy draws star clusters from a fixed seed: the same sky forever.
  sim.SetLayer("galaxy");
  const ship::NavChartView galaxy = ship::BuildNavChart(sim);
  CHECK(galaxy.clusters.size() > 260);
  const ship::NavChartView again = ship::BuildNavChart(sim);
  REQUIRE(again.clusters.size() == galaxy.clusters.size());
  CHECK(again.clusters[0].x == galaxy.clusters[0].x);
}

// ---------------------------------------------------------------------------
//  Panels
// ---------------------------------------------------------------------------
TEST_CASE("an empty chart says what to do, in the reference's words", "[nav]") {
  ship::Simulation sim = Fresh();
  const ship::NavScreenModel nav = ship::BuildNavScreen(sim);
  CHECK(nav.screen.head.kind == "NAV");
  CHECK(nav.screen.head.title == "Navigation");
  CHECK(nav.screen.head.sub ==
        "Select a destination to read it. Reading costs nothing.");

  REQUIRE(nav.screen.left.size() == 3);
  CHECK(HasText(*Panel(nav.screen.left, "nav-name"), "No target selected"));
  CHECK(HasText(*Panel(nav.screen.left, "nav-class"), "Select a body on the chart."));
  CHECK(HasText(*Panel(nav.screen.left, "nav-status"), "Idle"));
  CHECK(HasText(*Panel(nav.screen.under, "nav-details"), "No target."));
  CHECK(HasText(*Panel(nav.screen.under, "nav-dist"), "No route plotted."));
  CHECK(HasText(*Panel(nav.screen.right, "nav-scan"), "No scan data."));

  CHECK(nav.courseNote == "Plot a course by selecting a destination.");
  REQUIRE(nav.screen.progress.size() == 2);
  CHECK(nav.screen.progress[0].idleLabel == "Start Travel");
  CHECK(nav.screen.progress[0].state == ship::ButtonState::Off);
  CHECK(nav.screen.progress[1].idleLabel == "Run Deep Scan");
  CHECK(HasCommand(nav.screen, "Stand watch - advance ongoing work"));
}

TEST_CASE("a reachable target reads out distance, transit and cost", "[nav]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.SelectTarget("tolme"));
  const ship::NavScreenModel nav = ship::BuildNavScreen(sim);

  const ship::NavPanel& name = *Panel(nav.screen.left, "nav-name");
  REQUIRE(name.lines.size() == 2);
  CHECK(name.lines[0].kind == ship::PanelLine::Kind::Name);
  CHECK(name.lines[0].value == "Tolme Anchorage");
  CHECK(name.lines[1].value == "From Kessler Drift");

  CHECK(HasText(*Panel(nav.screen.left, "nav-class"),
                "Independent station, class C"));
  CHECK(HasText(*Panel(nav.screen.left, "nav-status"), "Reachable"));

  // Details are the authored fields, with the origin appended.
  const ship::NavPanel& details = *Panel(nav.screen.under, "nav-details");
  REQUIRE(Line(details, "Origin") != nullptr);
  CHECK(Line(details, "Origin")->value == "Kessler Drift");

  // navMetrics(): 339.5px / 16px per action = 21 actions, and everything the
  // panel prints is derived from that number.
  const ship::NavPanel& distance = *Panel(nav.screen.under, "nav-dist");
  CHECK(Line(distance, "Distance")->value == "8.8 ly");
  CHECK(Line(distance, "")->value == "8.3 x 10^13 km");
  CHECK(Line(distance, "Transit")->value == "~13 months, 9 days");
  CHECK(Line(distance, "Action cost")->value == "22 actions");

  CHECK(nav.courseNote == "Course from Kessler Drift to Tolme Anchorage.");
  REQUIRE(Button(nav.screen, "travel") != nullptr);
  CHECK(Button(nav.screen, "travel")->state == ship::ButtonState::Idle);
  CHECK(Button(nav.screen, "travel")->category == "space");

  // An out-of-range contact is named for free and refused its numbers.
  sim.SetLayer("system");
  REQUIRE(sim.SelectTarget("vance-star"));  // a local body in another system
  const ship::NavScreenModel far = ship::BuildNavScreen(sim);
  CHECK(HasText(*Panel(far.screen.left, "nav-status"), "Out of sensor range"));
  CHECK(HasText(*Panel(far.screen.under, "nav-dist"), ship::NavOutOfRange()));
  CHECK(far.courseNote == "This contact is outside the local sensor range.");

  // A region is a chart container, not a destination: it offers the sector.
  sim.SetLayer("galaxy");
  REQUIRE(sim.SelectTarget("gal-inner"));
  const ship::NavScreenModel region = ship::BuildNavScreen(sim);
  CHECK(HasText(*Panel(region.screen.left, "nav-status"), "Current region"));
  // Inner Rim keeps its authored eight and gains the generated systems of its
  // own region, so the count is read off the voyage's chart rather than fixed.
  std::size_t innerSystems = 0;
  for (const ship::Body& system : sim.World().systems) {
    if (system.region == "gal-inner") { ++innerSystems; }
  }
  CHECK(Line(*Panel(region.screen.under, "nav-details"), "Systems")->value ==
        std::to_string(innerSystems) + " navigable");
  CHECK(HasCommand(region.screen, "View Inner Rim sector chart"));
}

TEST_CASE("a scanned body hands over its readout, and only once", "[nav]") {
  ship::Simulation sim = Fresh();
  REQUIRE(sim.SelectTarget("halvan"));
  const ship::NavScreenModel before = ship::BuildNavScreen(sim);
  CHECK(HasText(*Panel(before.screen.right, "nav-scan"),
                "No detailed data. Sensors idle."));

  CHECK(ship::PressNavAction(sim, "scan"));
  const ship::NavScreenModel running = ship::BuildNavScreen(sim);
  CHECK(HasText(*Panel(running.screen.right, "nav-scan"), "Deep scan under way."));

  while (sim.FindOngoing("scan")->state == "running") { sim.PerformAction(); }
  const ship::NavScreenModel done = ship::BuildNavScreen(sim);
  CHECK(HasText(*Panel(done.screen.right, "nav-scan"),
                "Deep scan complete. Readout waiting."));

  // The reveal is the acknowledgement, exactly as a click on the finished
  // button does it.
  CHECK(ship::PressProgressButton(sim, "scan"));
  const ship::NavScreenModel read = ship::BuildNavScreen(sim);
  const ship::NavPanel& scan = *Panel(read.screen.right, "nav-scan");
  const ship::Body* halvan = sim.FindBodyById("halvan");
  REQUIRE(halvan != nullptr);
  for (const std::string& line : halvan->scan) { CHECK(HasText(scan, line)); }
  for (const std::string& line : halvan->scanLocked) {
    CHECK(HasText(scan, line + " - sensors insufficient"));
  }
}

// ---------------------------------------------------------------------------
//  NAVFX
// ---------------------------------------------------------------------------
TEST_CASE("the ambient motion is cosmetic, seeded and still under reduced motion",
          "[nav]") {
  ship::Simulation sim = Fresh();
  const ship::NavFxFrame first = ship::BuildNavFx(sim, 0, false);
  CHECK(first.rocks.size() == 9);
  CHECK(first.hasStation);
  CHECK(first.stationId == "tolme");
  CHECK(first.beaconOpacity == 1.0);
  CHECK(first.beacon.x == 545 - 1);
  CHECK(first.beacon.y == 205 - 9);

  // The beacon blinks on a 22-tick cycle and the satellite orbits at 0.022
  // radians a tick - both from the clock, never from the action counter.
  const ship::NavFxFrame blink = ship::BuildNavFx(sim, 11, false);
  CHECK(blink.beaconOpacity == 0.15);
  const ship::NavFxFrame orbit = ship::BuildNavFx(sim, 100, false);
  CHECK(orbit.satellite.x != first.satellite.x);

  const double before = sim.ActionCount();
  const ship::NavFxFrame frozen = ship::BuildNavFx(sim, 100, true);
  const ship::NavFxFrame frozenAgain = ship::BuildNavFx(sim, 999, true);
  CHECK(frozen.satellite.x == frozenAgain.satellite.x);
  CHECK(frozen.rocks[0].at.x == frozenAgain.rocks[0].at.x);
  CHECK(sim.ActionCount() == before);

  // The wider layers carry no ship-scale cosmetics at all.
  sim.SetLayer("galaxy");
  CHECK(ship::BuildNavFx(sim, 40, false).rocks.empty());
}

TEST_CASE("Navigation opens from the taskbar and routes to the chart", "[nav]") {
  ship::Simulation sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "nav", "main", true));
  CHECK(sim.State().activeTab == "nav:main");

  const ship::Tab* tab = ship::FindTab(sim.State(), "nav:main");
  REQUIRE(tab != nullptr);
  CHECK(tab->title == "Navigation");
  CHECK(ship::ScreenKindFor(*tab) == ship::ScreenKind::Nav);

  const ship::TaskScreenModel routed = ship::BuildTabScreen(sim, *tab);
  const ship::NavScreenModel direct = ship::BuildNavScreen(sim);
  REQUIRE(routed.left.size() == direct.screen.left.size());
  CHECK(routed.head.kind == "NAV");
  CHECK(routed.stage.id == "map");
  CHECK(routed.stage.width == 638.0f);
  CHECK(routed.stage.height == 278.0f);
}

// ---------------------------------------------------------------------------
//  The gate: a whole voyage, driven through the screen's own press path
// ---------------------------------------------------------------------------
TEST_CASE("a docked ship plots and starts its crossing without undocking first",
          "[nav][travel]") {
  ship::Simulation sim = Fresh();
  REQUIRE(ship::PressNavAction(sim, "select", "tolme"));
  REQUIRE(ship::PressNavAction(sim, "travel"));       // the crossing to the port
  while (sim.FindOngoing("travel")->state == "running") {
    sim.PerformAction("Stood watch");   // the same action, taken elsewhere
  }
  REQUIRE(ship::PressProgressButton(sim, "travel"));  // acknowledge: the ship arrives
  CHECK(sim.Map().here == "tolme");
  CHECK(sim.State().docked);   // arriving at a port berths the ship
  // The owner's rule: the berth is never a cage. Selecting the way back and
  // committing leaves the berth inside the same departure.
  REQUIRE(ship::PressNavAction(sim, "select", "kessler"));
  const ship::NavScreenModel plotted = ship::BuildNavScreen(sim);
  REQUIRE(Button(plotted.screen, "travel") != nullptr);
  CHECK(Button(plotted.screen, "travel")->state == ship::ButtonState::Idle);
  const double actions = sim.ActionCount();
  REQUIRE(ship::PressNavAction(sim, "travel"));
  CHECK_FALSE(sim.State().docked);
  CHECK(sim.ActionCount() == actions + 1);   // one departure, one action
  CHECK(sim.FindOngoing("travel") != nullptr);
}

TEST_CASE("a voyage crosses, docks and scans through the navigation screen",
          "[nav][opening]") {
  ship::Simulation sim = Fresh();
  CHECK(sim.Map().here == "kessler");
  const double start = sim.ActionCount();

  // Looking: three scales, a zoom, a pan and a selection - all free.
  CHECK(ship::PressNavAction(sim, "layer", "sector"));
  CHECK(ship::PressNavAction(sim, "layer", "galaxy"));
  CHECK(ship::PressNavAction(sim, "zoom-in"));
  CHECK(ship::PressNavAction(sim, "zoom-out"));
  CHECK(ship::PressNavAction(sim, "pan", "30,-20"));
  CHECK(ship::PressNavAction(sim, "layer", "system"));
  CHECK(sim.ActionCount() == start);

  // Plot the crossing to the anchorage over the water: select it on the chart,
  // then commit. The button says "Start Travel" because it is a local body.
  CHECK(ship::PressNavAction(sim, "select", "tolme"));
  const ship::NavScreenModel plotted = ship::BuildNavScreen(sim);
  REQUIRE(Button(plotted.screen, "travel") != nullptr);
  CHECK(Button(plotted.screen, "travel")->idleLabel == "Start Travel");

  const double fuelBefore = sim.StatValue("fuel");
  CHECK(ship::PressNavAction(sim, "travel"));
  CHECK(sim.ActionCount() == start + 1);
  const ship::Task* travel = sim.FindOngoing("travel");
  REQUIRE(travel != nullptr);
  CHECK(travel->label == "Under way to Tolme Anchorage");
  CHECK(travel->left == 21);
  CHECK(sim.StatValue("fuel") == fuelBefore - 21);

  // Standing watch advances the crossing, one action at a time. Phase 9: the
  // button carries the space tier's 15-action lock, so a second press of the
  // same control is refused until another action has resolved; the rest of the
  // crossing is walked with that pattern, exactly as a player would.
  const double watchFirst = sim.ActionCount();
  CHECK(ship::PressNavAction(sim, "stand-watch"));
  CHECK(sim.ActionCount() == watchFirst + 1);
  CHECK_FALSE(ship::PressNavAction(sim, "stand-watch"));   // locked: no second click
  CHECK(sim.ActionCount() == watchFirst + 1);
  while (sim.FindOngoing("travel")->state == "running") {
    const double actions = sim.ActionCount();
    sim.PerformAction("Stood watch");   // the same action, taken elsewhere
    CHECK(sim.ActionCount() == actions + 1);
  }
  CHECK(sim.FindOngoing("travel")->state == "done");

  // A finished crossing is acknowledged, and the ship genuinely relocates.
  const ship::NavScreenModel arrivedModel = ship::BuildNavScreen(sim);
  REQUIRE(Button(arrivedModel.screen, "travel") != nullptr);
  CHECK(Button(arrivedModel.screen, "travel")->state == ship::ButtonState::Done);
  CHECK(ship::PressProgressButton(sim, "travel"));
  CHECK(sim.Map().here == "tolme");
  CHECK(sim.FindOngoing("travel") == nullptr);

  // Tolme happens to be a quay; step off it and dock again through the
  // command row to prove the control itself.
  CHECK(sim.State().docked);
  CHECK(sim.Undock());
  CHECK_FALSE(sim.State().docked);
  const ship::NavScreenModel moored = ship::BuildNavScreen(sim);
  CHECK(HasCommand(moored.screen, "Dock at Tolme Anchorage"));
  CHECK(ship::PressNavAction(sim, "dock"));
  CHECK(sim.State().docked);

  // Scan a body that is still in range and read the readout back off the
  // screen: one action to start, the rest spent elsewhere.
  CHECK(ship::PressNavAction(sim, "select", "halvan"));
  const double beforeScan = sim.ActionCount();
  CHECK(ship::PressNavAction(sim, "scan"));
  CHECK(sim.ActionCount() == beforeScan + 1);
  while (sim.FindOngoing("scan")->state == "running") { sim.PerformAction(); }
  CHECK(ship::PressProgressButton(sim, "scan"));
  CHECK(sim.State().scanned.at("halvan"));

  // The whole voyage moved the clock and nothing else did.
  CHECK(sim.ActionCount() > start);
  CHECK(sim.State().gameOver.empty());
}

TEST_CASE("navigation completion remains clickable during its start cooldown", "[ui][nav][cooldown]") {
  auto sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "room", "nav", true));
  REQUIRE(sim.SelectTarget("tolme"));
  REQUIRE(ship::PressNavAction(sim, "travel"));
  const auto key = ship::ActionCooldownKey(sim, "progress:travel");
  REQUIRE(ship::CooldownRemaining(sim, key) == 15);
  // Explicit finished-work fixture: acknowledgment is a free read even when
  // this control still has a start lock (HTA onProgressClick ordering).
  auto* task = sim.FindOngoing("travel");
  REQUIRE(task != nullptr);
  task->state = "done";
  task->left = 0;
  const auto before = sim.ActionCount();
  CHECK(ship::PressNavAction(sim, "travel"));
  CHECK(sim.FindOngoing("travel") == nullptr);
  CHECK(sim.ActionCount() == before);
  CHECK(ship::CooldownRemaining(sim, key) == 15);
  CHECK(sim.State().actionCooldowns.size() == 1);
}

TEST_CASE("navigation uses the displayed command lock and disabled progress guard", "[ui][nav][cooldown]") {
  auto sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "room", "nav", true));
  const auto before = sim.ActionCount();
  CHECK_FALSE(ship::PressNavAction(sim, "travel")); // no destination
  CHECK(sim.ActionCount() == before);
  REQUIRE(ship::PressNavAction(sim, "stand-watch"));
  const auto screen = ship::BuildNavScreen(sim).screen;
  const auto& row = screen.commands.front();
  CHECK(row.coolingDown);
  CHECK(row.remaining == 15);
  CHECK(sim.State().actionCooldowns.size() == 1);
  CHECK_FALSE(ship::PressNavAction(sim, "stand-watch"));
  CHECK(sim.ActionCount() == before + 1);
  sim.PerformAction("another control");
  CHECK(ship::BuildNavScreen(sim).screen.commands.front().remaining == 14);
}

TEST_CASE("an opening crossing progresses through actual task controls", "[ui][nav][logic-audit]") {
  auto sim = Fresh();
  REQUIRE(ship::OpenTask(sim, "room", "nav", true));
  REQUIRE(ship::PressNavAction(sim, "select", "tolme"));
  const auto before = sim.ActionCount();
  REQUIRE(ship::PressNavAction(sim, "travel"));
  REQUIRE(sim.FindOngoing("travel") != nullptr);
  CHECK(sim.ActionCount() == before + 1);
  CHECK(sim.FindOngoing("travel")->left == 21);

  REQUIRE(ship::OpenTask(sim, "room", "o2", true));
  for (int i = 0; i < 3; ++i) {
    REQUIRE(ship::PressProgressButton(sim, "o2s" + std::to_string(i)));
  }
  REQUIRE(ship::OpenTask(sim, "room", "nav", true));
  REQUIRE(ship::PressNavAction(sim, "stand-watch"));
  const auto lockedAt = sim.ActionCount();
  CHECK_FALSE(ship::PressNavAction(sim, "stand-watch"));
  CHECK(sim.ActionCount() == lockedAt);

  REQUIRE(ship::OpenTask(sim, "room", "cargo", true));
  for (int i = 0; i < 2; ++i) {
    REQUIRE(ship::PressProgressButton(sim, "salvage-" + std::to_string(i)));
  }
  // The starting hold has room for two lots, not three; refusal costs nothing.
  const auto beforeFullHold = sim.ActionCount();
  CHECK_FALSE(ship::PressProgressButton(sim, "salvage-2"));
  CHECK(sim.ActionCount() == beforeFullHold);
  REQUIRE(sim.BankReady());
  const double priorBurst = sim.FindOngoing("travel")->left;
  REQUIRE(ship::OpenTask(sim, "room", "o2", true));
  const auto beforeBank = sim.ActionCount();
  REQUIRE(ship::PressProgressButton(sim, "o2-run"));
  CHECK(sim.ActionCount() == beforeBank + 1);
  REQUIRE(sim.FindOngoing("travel") != nullptr);
  CHECK(sim.FindOngoing("travel")->left == priorBurst - 11);
  CHECK(sim.BankRunning());
  CHECK(sim.State().gameOver.empty());

  REQUIRE(ship::OpenTask(sim, "system", "fuel", true));
  REQUIRE(ship::PressSystemAction(sim, "fuel"));
  REQUIRE(ship::OpenTask(sim, "room", "engine", true));
  REQUIRE(ship::PressProgressButton(sim, "maint"));
  REQUIRE(ship::OpenTask(sim, "system", "energy", true));
  REQUIRE(ship::PressSystemAction(sim, "energy"));
  REQUIRE(ship::OpenTask(sim, "system", "shield", true));
  REQUIRE(ship::PressSystemAction(sim, "shield"));
  const auto* trip = sim.FindOngoing("travel");
  REQUIRE(trip != nullptr);
  CHECK(trip->state == "done");
  REQUIRE(ship::OpenTask(sim, "room", "nav", true));
  const auto beforeAck = sim.ActionCount();
  REQUIRE(ship::PressNavAction(sim, "travel"));
  CHECK(sim.ActionCount() == beforeAck);
  CHECK(sim.Map().here == "tolme");
  CHECK(sim.State().docked);
  CHECK(sim.State().gameOver.empty());
}

TEST_CASE("powered work pauses without energy while other work and the action clock continue", "[ui][nav][logic-audit]") {
  auto sim = Fresh();
  sim.FindStat("energy")->value = 0;
  sim.MutableState().engineCondition = 70;
  REQUIRE(sim.AddOngoing("travel", "Crossing", 4, "travel", "tolme") != nullptr);
  REQUIRE(sim.AddOngoing("scan", "Scanning", 4, "scan", "halvan") != nullptr);
  REQUIRE(sim.AddOngoing("maint", "Maintenance", 4, "maint", "engine") != nullptr);
  REQUIRE(ship::OpenTask(sim, "room", "nav", true));
  const auto before = sim.ActionCount();
  REQUIRE(ship::PressNavAction(sim, "stand-watch"));
  CHECK(sim.ActionCount() == before + 1);
  CHECK(sim.FindOngoing("travel")->left == 4);
  CHECK(sim.FindOngoing("scan")->left == 4);
  CHECK(sim.FindOngoing("maint")->left == 3);

  REQUIRE(ship::OpenTask(sim, "system", "energy", true));
  REQUIRE(ship::PressSystemAction(sim, "energy"));
  CHECK(sim.ActionCount() == before + 2);
  CHECK(sim.StatValue("energy") > 0);
  CHECK(sim.FindOngoing("travel")->left == 3);
  CHECK(sim.FindOngoing("scan")->left == 3);
  CHECK(sim.FindOngoing("maint")->left == 2);
}

// ---------------------------------------------------------------------------
//  The grown chart
//  ---------------------------------------------------------------------------
//  Owner report, 2026-11-07: "the nav screen and the planets are fake - I want
//  to see them and not let a filter hide them; more stars and more systems."
//  The authored catalogue packs 48 systems into seven shared positions (41 of
//  them stacked six deep), and both the sector and the galaxy scale filtered
//  their bodies by region. These cases hold the grown chart to what it promises.
TEST_CASE("the charted galaxy is grown once, deterministically, and every system owns its dot",
          "[nav][chart][expansion]") {
  const ship::Catalog& authored = World();

  ship::Simulation first = ship::Simulation::NewVoyage(authored);
  const ship::Catalog& grown = first.World();

  // Every region is expanded, outer galactic regions are added, and nothing authored is lost.
  CHECK(grown.regions.size() ==
        authored.regions.size() + static_cast<std::size_t>(ship::kNewGalaxies));
  CHECK(grown.systems.size() ==
        authored.systems.size() +
            static_cast<std::size_t>(ship::kNewSystemsPerRegion) * grown.regions.size());
  CHECK(grown.bodies.size() > authored.bodies.size());
  CHECK(grown.ports.size() > authored.ports.size());

  // The authored rows survive byte for byte by id and name...
  for (const ship::Body& body : authored.bodies) {
    const ship::Body* kept = grown.FindBody(body.id);
    REQUIRE(kept != nullptr);
    CHECK(kept->name == body.name);
    CHECK(kept->type == body.type);
  }

  // ...and the reference hangs together after the growth.
  CHECK(grown.Validate().empty());

  // The bug this replaces: six systems on one dot, five of them unreachable by
  // the mouse. No two systems may share a position now.
  {
    std::map<std::string, std::string> dots;  // "x,y" -> the system that owns it
    std::string clash;
    for (const ship::Body& system : grown.systems) {
      const std::string key = ship::js::NumToString(system.x) + "," + ship::js::NumToString(system.y);
      auto it = dots.find(key);
      if (it != dots.end()) { clash = it->second + " and " + system.id; break; }
      dots[key] = system.id;
    }
    INFO(clash);
    CHECK(clash.empty());
  }

  // A generated system is a real destination: its own star, at least two local
  // bodies, a lane to a neighbour in its own region.
  std::size_t generated = 0;
  for (const ship::Body& system : grown.systems) {
    if (system.id.rfind("sec-gen-", 0) != 0) { continue; }
    ++generated;
    CHECK(grown.FindRegion(system.region) != nullptr);
    REQUIRE_FALSE(system.entry.empty());
    const ship::Body* entry = grown.FindBody(system.entry);
    REQUIRE(entry != nullptr);
    CHECK(entry->type == "star");
    CHECK(grown.BodySystem(*entry) == system.id);
    const std::size_t local = grown.BodiesInSystem(system.id).size();
    CHECK(local >= 2);
    bool linked = false;
    for (const ship::Lane& lane : grown.sectorLanes) {
      if (lane.from != system.id && lane.to != system.id) { continue; }
      const std::string other = lane.from == system.id ? lane.to : lane.from;
      const ship::Body* peer = grown.FindSystem(other);
      if (peer != nullptr && peer->region == system.region) { linked = true; }
    }
    CHECK(linked);
  }
  CHECK(generated > 0u);

  // Deterministic: a second voyage grows the same sky, and growing a grown
  // catalogue is a no-op (a restart must not multiply the galaxy).
  ship::Simulation second = ship::Simulation::NewVoyage(authored);
  REQUIRE(second.World().systems.size() == grown.systems.size());
  REQUIRE(second.World().bodies.size() == grown.bodies.size());
  for (std::size_t i = 0; i < grown.systems.size(); ++i) {
    CHECK(second.World().systems[i].id == grown.systems[i].id);
    CHECK(second.World().systems[i].x == grown.systems[i].x);
    CHECK(second.World().systems[i].y == grown.systems[i].y);
  }
  ship::Simulation again = ship::Simulation::NewVoyage(first.World());
  CHECK(again.World().systems.size() == grown.systems.size());
  CHECK(again.World().bodies.size() == grown.bodies.size());

  // The system chart keeps the reference's board; the sector chart is the wide
  // one the galaxy is laid out on, and the authored crossings still cost the
  // same because the authored systems only moved.
  CHECK(first.Map().w == 638);
  CHECK(first.Map().h == 278);
  first.SetLayer("sector");
  CHECK(first.Map().w > 638);
  CHECK(first.Map().h > 278);
  const ship::Body* kesslerAuthored = authored.FindSystem("sec-kessler");
  const ship::Body* kesslerGrown = grown.FindSystem("sec-kessler");
  REQUIRE(kesslerAuthored != nullptr);
  REQUIRE(kesslerGrown != nullptr);
  CHECK((kesslerGrown->x - kesslerAuthored->x) ==
        (ship::kSectorBoardScale - 1.0) * 0.5 * authored.map.w);
  CHECK((kesslerGrown->y - kesslerAuthored->y) ==
        (ship::kSectorBoardScale - 1.0) * 0.5 * authored.map.h);

  // ---- every system is a system, not a diagram of one --------------------
  std::size_t thinnest = 999;
  for (const ship::Body& system : grown.systems) {
    const std::size_t local = grown.BodiesInSystem(system.id).size();
    thinnest = std::min(thinnest, local);
    bool claimed = false;
    for (const auto& detail : system.details) {
      if (detail.first == "Charted bodies" && detail.second == std::to_string(local)) {
        claimed = true;
      }
    }
    INFO(system.id);
    CHECK(claimed);
  }
  CHECK(thinnest >= 15u);

  // ---- the galaxy scale has stars on it ----------------------------------
  const ship::MapGeometry& galaxyBoard = grown.layerBoards.at("galaxy");
  CHECK(galaxyBoard.w == authored.map.w * ship::kGalaxyBoardScale);
  CHECK(galaxyBoard.h == authored.map.h * ship::kGalaxyBoardScale);
  CHECK(grown.galaxyPositions.size() == grown.systems.size());
  {
    std::set<std::string> dots;
    std::string clash;
    for (const ship::Body& system : grown.systems) {
      const auto it = grown.galaxyPositions.find(system.id);
      REQUIRE(it != grown.galaxyPositions.end());
      CHECK(it->second.first >= 0.0);
      CHECK(it->second.first <= galaxyBoard.w);
      CHECK(it->second.second >= 0.0);
      CHECK(it->second.second <= galaxyBoard.h);
      const std::string key = ship::js::NumToString(it->second.first) + "," +
                              ship::js::NumToString(it->second.second);
      if (!dots.insert(key).second) { clash = system.id; }
    }
    INFO(clash);
    CHECK(clash.empty());
  }
}

// The rule the station pass answers: every system keeps a space station, in the
// authored core and the generated sky alike, with no quota and no exceptions.
TEST_CASE("every system keeps a space station", "[nav][chart][expansion]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  std::set<std::string> portIds;
  for (const ship::Port& port : world.ports) { portIds.insert(port.id); }

  std::set<std::string> systemsWithPort;
  for (const ship::Body& body : world.bodies) {
    if (body.type == "station" && portIds.count(body.id) != 0) {
      systemsWithPort.insert(world.BodySystem(body));
    }
  }

  std::size_t systems = 0;
  for (const ship::Body& system : world.systems) {
    INFO(system.id);
    ++systems;
    CHECK(systemsWithPort.count(system.id) == 1u);
  }
  CHECK(systems > 0u);
  CHECK(systems == world.systems.size());

  // Nothing dangles: every port names a station body, and the chart validates.
  for (const ship::Port& port : world.ports) {
    const ship::Body* body = world.FindBody(port.id);
    REQUIRE(body != nullptr);
    CHECK(body->type == "station");
    CHECK(body->name == port.name);
  }
  CHECK(world.Validate().empty());
}

TEST_CASE("no filter hides a body, and a body out of sensor range is still on the chart",
          "[nav][chart][expansion]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  // The system scale shows every local body of the system the ship is in - the
  // ones it cannot scan included.
  std::size_t local = 0;
  for (const ship::Body& body : world.bodies) {
    if (world.BodySystem(body) == sim.CurrentSystemId()) { ++local; }
  }
  const ship::NavChartView system = ship::BuildNavChart(sim);
  CHECK(system.bodies.size() == local);
  std::size_t systemOutOfRange = 0;
  for (const ship::NavBodyView& body : system.bodies) {
    if (!body.inRange) { ++systemOutOfRange; }
  }
  CHECK(systemOutOfRange == 0u);  // this scale is the ship's own system

  // The sector scale shows the whole galaxy: every system, in every region,
  // with its own position - not the seven of the region being read.
  sim.SetLayer("sector");
  const ship::NavChartView sector = ship::BuildNavChart(sim);
  CHECK(sector.bodies.size() == world.systems.size());
  std::size_t regions = 0;
  std::vector<std::string> seenRegions;
  for (const ship::NavBodyView& body : sector.bodies) {
    if (std::find(seenRegions.begin(), seenRegions.end(), body.region) == seenRegions.end()) {
      seenRegions.push_back(body.region);
    }
  }
  regions = seenRegions.size();
  CHECK(regions == world.regions.size());
  // ...and there is sky behind it, on every scale.
  CHECK(sector.clusters.size() > 500u);

  // A local body in another system is on the system chart of *that* system, and
  // answers "out of sensor range" when asked from here - visible, still refused
  // its numbers.
  sim.SetLayer("system");
  const ship::NavChartView home = ship::BuildNavChart(sim);
  for (const ship::NavBodyView& body : home.bodies) { CHECK(body.id != "vance-star"); }
  REQUIRE(sim.SelectTarget("vance-star"));
  const ship::NavScreenModel far = ship::BuildNavScreen(sim);
  CHECK(HasText(*Panel(far.screen.left, "nav-status"), "Out of sensor range"));
  CHECK(far.screen.progress.size() >= 1);
  CHECK(sim.ActionCount() == 0);
}

TEST_CASE("procedural galaxies carry histories, lanes, ports, and camera centering",
          "[nav][chart][expansion]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  std::size_t genGalaxies = 0;
  for (const ship::Body& region : world.regions) {
    if (region.id.rfind("gal-gen-", 0) != 0) { continue; }
    ++genGalaxies;
    const auto& history = sim.WorldHistory(region.id);
    CHECK(history.size() == 2u);
    CHECK_FALSE(world.SystemsInRegion(region.id).empty());

    bool hasGalaxyLane = false;
    for (const ship::Lane& lane : world.galaxyLanes) {
      if (lane.from == region.id || lane.to == region.id) { hasGalaxyLane = true; break; }
    }
    CHECK(hasGalaxyLane);
  }
  CHECK(genGalaxies == static_cast<std::size_t>(ship::kNewGalaxies));

  // Selecting an outer procedural galaxy and viewing its sector centres the
  // sector camera over its star cluster so those systems project near the middle.
  sim.SetLayer("galaxy");
  REQUIRE(sim.ViewSector("gal-gen-1"));
  CHECK(sim.Map().layer == "sector");
  const auto& indices = world.SystemsInRegion("gal-gen-1");
  REQUIRE_FALSE(indices.empty());
  const ship::Body& sampleSystem = world.systems[indices.front()];
  const ship::NavPoint projected =
      ship::NavProjectPanned(sim.Map(), sim.Views().at("sector"), sampleSystem.x, sampleSystem.y);
  CHECK(projected.x >= 0.0);
  CHECK(projected.x <= sim.Map().vw);
  CHECK(projected.y >= 0.0);
  CHECK(projected.y <= sim.Map().vh);
}

TEST_CASE("procedural map events fire on exploration, resolve cleanly, and round-trip saves",
          "[nav][events][expansion]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();

  // Locate a procedural body tagged with a salvage cache and another with an
  // ancient nav buoy so both scan discovery paths can be exercised directly.
  const ship::Body* salvageBody = nullptr;
  const ship::Body* buoyBody = nullptr;
  const ship::Body* depotBody = nullptr;
  for (const ship::Body& body : world.bodies) {
    for (const auto& d : body.details) {
      if (d.first != "Map feature") { continue; }
      if (d.second == "Salvage cache" && salvageBody == nullptr) { salvageBody = &body; }
      if (d.second == "Ancient nav buoy" && buoyBody == nullptr) { buoyBody = &body; }
      if (d.second == "Fuel skimmer depot" && depotBody == nullptr) { depotBody = &body; }
    }
  }
  REQUIRE(salvageBody != nullptr);
  REQUIRE(buoyBody != nullptr);
  REQUIRE(depotBody != nullptr);

  // Move into the salvage body's system, select it, run a deep scan, and acknowledge.
  const ship::Body* salvageSys = world.FindSystem(world.BodySystem(*salvageBody));
  REQUIRE(salvageSys != nullptr);
  sim.MutableMap().here = salvageSys->entry;
  REQUIRE(sim.SelectTarget(salvageBody->id));
  const std::size_t goodsBefore = sim.Goods().size();
  REQUIRE(sim.StartScan());
  while (sim.FindOngoing("scan") != nullptr && sim.FindOngoing("scan")->state != "done") {
    REQUIRE(sim.StandWatch());
  }
  REQUIRE(sim.Acknowledge("scan"));
  CHECK(sim.Goods().size() == goodsBefore + 1);

  // Move into the nav buoy's system and scan the buoy: insight rises and local
  // peer bodies are revealed.
  const ship::Body* buoySys = world.FindSystem(world.BodySystem(*buoyBody));
  REQUIRE(buoySys != nullptr);
  sim.MutableMap().here = buoySys->entry;
  const double insightBefore = sim.State().sim.insight;
  REQUIRE(sim.SelectTarget(buoyBody->id));
  REQUIRE(sim.StartScan());
  while (sim.FindOngoing("scan") != nullptr && sim.FindOngoing("scan")->state != "done") {
    REQUIRE(sim.StandWatch());
  }
  REQUIRE(sim.Acknowledge("scan"));
  CHECK(sim.State().sim.insight >= insightBefore + 1.0);

  // Arrive at an orbital fuel skimmer depot and confirm fuel cell recovery.
  const ship::Body* depotSys = world.FindSystem(world.BodySystem(*depotBody));
  REQUIRE(depotSys != nullptr);
  sim.MutableMap().here = depotSys->entry;
  sim.FindStat("fuel")->value = 60.0;
  const double cellsBefore = sim.State().fuelCells;
  REQUIRE(sim.SelectTarget(depotBody->id));
  REQUIRE(sim.StartTravel());
  while (sim.FindOngoing("travel") != nullptr && sim.FindOngoing("travel")->state != "done") {
    sim.FindStat("energy")->value = 100.0;
    sim.MutableLife().canisters = 6;
    sim.MutableMess().rack = 10;
    REQUIRE(sim.StandWatch());
  }
  REQUIRE(sim.Acknowledge("travel"));
  CHECK(sim.State().fuelCells == cellsBefore + 1.0);

  // Spawn a map-tied incident, verify save validation against the unexpanded
  // authored catalogue, load it back, and resolve it with a repair kit.
  REQUIRE(sim.SpawnMapIncident("map-ion-squall", "Entered charged filament."));
  std::string why;
  const std::string saved = sim.SaveJson();
  INFO(why);
  REQUIRE(ship::Simulation::ValidSave(saved, World(), &why));

  ship::Simulation restored = Fresh();
  REQUIRE(restored.LoadJson(saved, &why));
  REQUIRE_FALSE(restored.Events().empty());
  const std::string incId = restored.Events().back().id;
  restored.MutableState().repairKits = 3;
  REQUIRE(restored.RemedyIncident(incId));
}

TEST_CASE("huge procedural charts build within interactive frame budgets",
          "[nav][perf][expansion]") {
  ship::Simulation sim = Fresh();
  const auto t0 = std::chrono::steady_clock::now();
  for (const char* layer : {"system", "sector", "galaxy"}) {
    sim.SetLayer(layer);
    const ship::NavScreenModel screen = ship::BuildNavScreen(sim);
    CHECK_FALSE(screen.chart.bodies.empty());
    sim.ZoomMap(4);
    const ship::NavChartView zoomed = ship::BuildNavChart(sim);
    CHECK_FALSE(zoomed.bodies.empty());
  }
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
  INFO("6 full chart builds across all layers took " << elapsedMs << " ms");
  CHECK(elapsedMs < 250);
}

// The trade card: three buttons in the middle column, and the list each one
// opens on the right. The station-to-station list is the roll of stations the
// player has docked at, less the station they are standing in.
TEST_CASE("the trade board lists visited stations and greys a contract once taken",
          "[station][trade]") {
  ship::Simulation sim = Fresh();
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = false;
  REQUIRE(sim.Dock());
  sim.MutableState().docked = false;
  sim.MutableMap().here = "relay";
  REQUIRE(sim.Dock());

  const ship::StationScreenModel model = ship::BuildStationScreen(sim, "relay");
  // The card sits under the starmap, above the sales counter.
  REQUIRE(model.columns.size() >= 3u);
  CHECK(model.columns[0].id == "market");
  CHECK(model.columns[1].id == "starmap");
  CHECK(model.columns[2].id == "trade");

  // Three modes, in the order the middle column shows them.
  REQUIRE(model.tradeModes.size() == 3u);
  CHECK(model.tradeModes[0].id == "delivery");
  CHECK(model.tradeModes[1].id == "freelance");
  CHECK(model.tradeModes[2].id == "station");
  // A mode with nothing behind it says so rather than inventing a consignment.
  CHECK_FALSE(model.tradeModes[0].empty.empty());
  CHECK_FALSE(model.tradeModes[1].empty.empty());

  // Station-to-station is every station visited so far, less this one.
  const ship::TradeMode& station = model.tradeModes[2];
  REQUIRE(station.offers.size() == 1u);
  CHECK(station.offers[0].to == "tolme");
  CHECK_FALSE(station.offers[0].accepted);
  CHECK(station.offers[0].label.find("Tolme Anchorage") != std::string::npos);

  // The card's own rows are the three mode buttons, all free to open.
  const ship::StationColumn* card = ship::FindStationCategory(model, "trade");
  REQUIRE(card != nullptr);
  REQUIRE(card->rows.size() == 3u);
  CHECK(card->rows[0].id == "trade-mode:delivery");
  CHECK(card->rows[1].id == "trade-mode:freelance");
  CHECK(card->rows[2].id == "trade-mode:station");
  for (const ship::StationRow& row : card->rows) { CHECK(row.free); }

  // Accepting costs one action, and the row greys from then on.
  const double before = sim.ActionCount();
  REQUIRE(ship::PressStationAction(sim, "trade-accept:station:tolme"));
  CHECK(sim.ActionCount() == before + 1);
  CHECK(sim.TradeAccepted("station", "tolme"));
  CHECK_FALSE(ship::PressStationAction(sim, "trade-accept:station:tolme"));
  CHECK(sim.ActionCount() == before + 1);

  const ship::StationScreenModel after = ship::BuildStationScreen(sim, "relay");
  REQUIRE(after.tradeModes.size() == 3u);
  REQUIRE(after.tradeModes[2].offers.size() == 1u);
  CHECK(after.tradeModes[2].offers[0].accepted);

  // A station that was never visited cannot be the far end of the trade.
  CHECK_FALSE(sim.AcceptTrade("station", "auriga-port"));
  CHECK(sim.State().trades.size() == 1u);

  // The roll of visits and the accepted trade both survive a save.
  std::string error;
  ship::Simulation restored = ship::Simulation::NewVoyage(sim.World());
  REQUIRE(restored.LoadJson(sim.SaveJson(), &error));
  CHECK(restored.State().visitedPorts.count("tolme") == 1u);
  CHECK(restored.State().visitedPorts.count("relay") == 1u);
  CHECK(restored.TradeAccepted("station", "tolme"));
}

// The delivery and freelance lists are a draw, not a table: one seeded board per
// system per epoch, read off the same chart the starmap prints, so every run
// names a body that is really there and the two berths of one system cannot
// disagree. Nothing about the draw is stored; what a contract promised is.
TEST_CASE("each system draws its own trade board out of its own chart", "[station][trade]") {
  ship::Simulation sim = Fresh();
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = false;
  REQUIRE(sim.Dock());

  const ship::Catalog& world = sim.World();
  const ship::Body* berth = world.FindBody("tolme");
  REQUIRE(berth != nullptr);
  const std::string systemId = world.BodySystem(*berth);

  // Every berth of the system, and the runs the system draws between them.
  std::vector<std::string> berths;
  for (const ship::Port& port : world.ports) {
    const ship::Body* body = world.FindBody(port.id);
    if (body != nullptr && world.BodySystem(*body) == systemId) { berths.push_back(port.id); }
  }
  REQUIRE(berths.size() == 2u);  // Kessler keeps Tolme and the relay

  std::size_t runs = 0;
  std::set<std::string> drawn;
  for (const std::string& id : berths) {
    for (const ship::TradeRun& run : sim.TradeBoard(id)) {
      ++runs;
      CHECK(run.from == id);
      CHECK(run.mode == "delivery" || run.mode == "freelance");
      CHECK(run.crates >= 1);
      CHECK(run.fee > 0);
      // The run names a body of this system, and a commodity the market lists.
      const ship::Body* destination = world.FindBody(run.to);
      REQUIRE(destination != nullptr);
      CHECK(world.BodySystem(*destination) == systemId);
      const ship::Good* good = nullptr;
      for (const ship::Good& candidate : world.goods) {
        if (candidate.id == run.good) { good = &candidate; }
      }
      REQUIRE(good != nullptr);
      // The lot fits the hold a voyage starts with, and stays a shipment.
      CHECK(static_cast<double>(run.crates) * good->vol <= 20.0);
      CHECK(good->value * run.crates <= 1200.0 + good->value);
      // A run belongs to one berth: the berths of a system never repeat one.
      CHECK(drawn.insert(run.to + "|" + run.good).second);
    }
  }
  // Three to five runs a system, shared out across its berths.
  CHECK(runs >= 3u);
  CHECK(runs <= 5u);

  // The same call draws the same board: nothing is stored, everything is seeded.
  const std::vector<ship::TradeRun> first = sim.TradeBoard("tolme");
  const std::vector<ship::TradeRun> again = sim.TradeBoard("tolme");
  REQUIRE(first.size() == again.size());
  for (std::size_t index = 0; index < first.size(); ++index) {
    CHECK(first[index].mode == again[index].mode);
    CHECK(first[index].to == again[index].to);
    CHECK(first[index].good == again[index].good);
    CHECK(first[index].crates == again[index].crates);
    CHECK(first[index].fee == again[index].fee);
  }

  // Taking a run costs one action, and it keeps the numbers it was signed for.
  REQUIRE_FALSE(first.empty());
  const ship::TradeRun run = first.front();
  const std::string id = "trade-accept:" + run.mode + ":" + run.to + ":" + run.good;
  const double before = sim.ActionCount();
  REQUIRE(ship::PressStationAction(sim, id));
  CHECK(sim.ActionCount() == before + 1);
  CHECK(sim.TradeAccepted(run.mode, run.to, run.good));
  CHECK_FALSE(ship::PressStationAction(sim, id));
  bool greyed = false;
  for (const ship::TradeRun& row : sim.TradeBoard("tolme")) {
    if (row.mode == run.mode && row.to == run.to && row.good == run.good) {
      greyed = row.accepted && row.crates == run.crates && row.fee == run.fee;
    }
  }
  CHECK(greyed);

  // A run the board is not showing cannot be signed, and a mode that does not
  // exist cannot either.
  CHECK_FALSE(sim.AcceptTrade("delivery", "no-such-body", "ore"));
  CHECK_FALSE(sim.AcceptTrade("nonsense", run.to, run.good));

  // The window is built from the same draw: the two lists are the runs, and a
  // run already taken is grey when the panel is rebuilt.
  const ship::StationScreenModel model = ship::BuildStationScreen(sim, "tolme");
  REQUIRE(model.tradeModes.size() == 3u);
  CHECK(model.tradeModes[0].offers.size() + model.tradeModes[1].offers.size() == first.size());
  for (const ship::TradeMode& mode : model.tradeModes) {
    if (mode.id == "station") { continue; }
    for (const ship::TradeOffer& offer : mode.offers) {
      CHECK(offer.mode == mode.id);
      CHECK_FALSE(offer.good.empty());
      CHECK(offer.accepted == sim.TradeAccepted(offer.mode, offer.to, offer.good));
      CHECK(offer.label.find("crate") != std::string::npos);
    }
  }

  // The numbers survive the save: the draw does not, so the record carries them.
  std::string error;
  ship::Simulation restored = ship::Simulation::NewVoyage(sim.World());
  REQUIRE(restored.LoadJson(sim.SaveJson(), &error));
  REQUIRE(restored.State().trades.size() == 1u);
  CHECK(restored.State().trades[0].good == run.good);
  CHECK(restored.State().trades[0].crates == run.crates);
  CHECK(restored.State().trades[0].fee == run.fee);
}

// Every station writes its own starmap of the system it sits in, out of the
// chart: the system's reading, then its bodies, planets first. Nothing is
// authored per port, so a station that exists cannot be missing its page.
TEST_CASE("every station writes its own starmap of its own system", "[station][starmap]") {
  ship::Simulation sim = Fresh();
  const ship::Catalog& world = sim.World();
  REQUIRE_FALSE(world.ports.empty());

  for (const ship::Port& port : world.ports) {
    INFO(port.id);
    const ship::Body* berth = sim.FindBodyById(port.id);
    REQUIRE(berth != nullptr);
    const std::string systemId = world.BodySystem(*berth);

    const ship::StarmapPage page = ship::BuildStarmapPage(sim, port.id);
    CHECK(page.systemId == systemId);
    CHECK_FALSE(page.systemName.empty());
    CHECK(page.planetCount > 0u);
    CHECK_FALSE(page.entries.empty());
    CHECK_FALSE(page.systemReading.empty());

    // Every planet of that system is on the page, with its name, kind and
    // reading - and the station itself is not.
    for (std::size_t index : world.BodiesInSystem(systemId)) {
      const ship::Body& body = world.bodies[index];
      if (body.id == port.id) {
        for (const ship::StarmapEntry& entry : page.entries) { CHECK(entry.id != port.id); }
        continue;
      }
      if (body.type != "planet") { continue; }
      const auto entry = std::find_if(page.entries.begin(), page.entries.end(),
                                      [&](const ship::StarmapEntry& e) { return e.id == body.id; });
      REQUIRE(entry != page.entries.end());
      CHECK(entry->name == body.name);
      CHECK_FALSE(entry->kind.empty());
      CHECK_FALSE(entry->reading.empty());
    }
  }

  // The page is a card in the station window, directly under the shop, and
  // reading it never spends an action or asks for shiplets.
  sim.MutableMap().here = "tolme";
  sim.MutableState().docked = false;
  REQUIRE(sim.Dock());
  const ship::StationScreenModel model = ship::BuildStationScreen(sim, "tolme");
  const ship::StationColumn* map = ship::FindStationCategory(model, "starmap");
  REQUIRE(map != nullptr);
  REQUIRE(model.columns.size() >= 2u);
  CHECK(model.columns[0].id == "market");
  CHECK(model.columns[1].id == "starmap");

  const ship::StarmapPage tolme = ship::BuildStarmapPage(sim, "tolme");
  std::size_t entries = 0;
  std::size_t headings = 0;
  for (const ship::StationRow& row : map->rows) {
    if (row.reading) {
      ++headings;   // a group heading, not a body
      continue;
    }
    ++entries;
    CHECK(row.page);
    CHECK(row.free);
    REQUIRE(row.id.rfind("starmap:", 0) == 0);
    CHECK(sim.FindBodyById(row.id.substr(8)) != nullptr);
  }
  CHECK(entries == tolme.entries.size());
  CHECK(headings > 0u);

  const double actions = sim.ActionCount();
  for (const ship::StationRow& row : map->rows) {
    if (row.reading) { continue; }
    CHECK_FALSE(ship::PressStationAction(sim, row.id));
  }
  CHECK(sim.ActionCount() == actions);
}

TEST_CASE("staged sensor upgrades unlock scanLocked tiers and deep-region scanning",
          "[nav][sensors][expansion]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().shiplets = 5000;

  // Park and dock at the first authored port to inspect Shipyard staging first.
  REQUIRE_FALSE(sim.World().ports.empty());
  sim.MutableMap().here = sim.World().ports.front().id;
  sim.MutableState().docked = false;
  REQUIRE(sim.Dock());
  REQUIRE(sim.State().docked);

  const auto stationBefore = ship::BuildStationScreen(sim, sim.Map().here);
  const ship::StationColumn* yardBeforePtr = ship::FindStationCategory(stationBefore, "yard");
  REQUIRE(yardBeforePtr != nullptr);
  const ship::StationColumn& yardBefore = *yardBeforePtr;
  auto findYardRow = [](const ship::StationColumn& col, const std::string& id) -> const ship::StationRow* {
    for (const auto& r : col.rows) {
      if (r.id == id) { return &r; }
    }
    return nullptr;
  };
  REQUIRE(findYardRow(yardBefore, "upgrade:scanner") != nullptr);
  REQUIRE(findYardRow(yardBefore, "upgrade:scanner2") != nullptr);
  CHECK(findYardRow(yardBefore, "upgrade:scanner")->enabled);
  CHECK_FALSE(findYardRow(yardBefore, "upgrade:scanner2")->enabled);

  // Pick a local body with scanLocked entries in Kessler, a same-region system,
  // and a deep-region outer-galaxy system.
  std::string localWithLocked;
  for (std::size_t idx : sim.World().BodiesInSystem("sec-kessler")) {
    const ship::Body& b = sim.World().bodies[idx];
    if (b.scanLocked.size() >= 2) { localWithLocked = b.id; break; }
  }
  if (localWithLocked.empty()) {
    for (std::size_t idx : sim.World().BodiesInSystem("sec-kessler")) {
      const ship::Body& b = sim.World().bodies[idx];
      if (!b.scanLocked.empty()) { localWithLocked = b.id; break; }
    }
  }
  REQUIRE_FALSE(localWithLocked.empty());

  std::string outerSysId;
  std::string outerRegId;
  for (const ship::Body& sys : sim.World().systems) {
    if (sys.region != "gal-inner") {
      outerSysId = sys.id;
      outerRegId = sys.region;
      break;
    }
  }
  REQUIRE_FALSE(outerSysId.empty());

  // Tier 0: local scan leaves scanLocked marked "sensors insufficient", and
  // neither systems nor cross-system contacts can be scanned.
  CHECK(sim.SensorTier() == 0);
  CHECK_FALSE(sim.CanScan(*sim.FindBodyById("sec-vance")));
  CHECK_FALSE(sim.CanScan(*sim.FindBodyById("vance-star")));
  CHECK_FALSE(sim.CanScan(*sim.FindBodyById(outerSysId)));

  sim.MutableState().scanned[localWithLocked] = true;
  REQUIRE(sim.SelectTarget(localWithLocked));
  {
    const auto nav0 = ship::BuildNavScreen(sim);
    bool sawInsufficient = false;
    for (const auto& line : nav0.screen.right[0].lines) {
      if (line.value.find("sensors insufficient") != std::string::npos) {
        sawInsufficient = true;
      }
    }
    CHECK(sawInsufficient);
  }

  // Buy Tier 1 ("scanner"): scanCost drops to 3, first scanLocked entry unlocks,
  // and same-region systems/bodies become scannable while outer galaxies stay locked.
  REQUIRE(sim.BuyUpgrade("scanner"));
  CHECK(sim.SensorTier() == 1);
  CHECK(sim.ScanCost() == 3.0);
  CHECK(sim.CanScan(*sim.FindBodyById("sec-vance")));
  CHECK(sim.CanScan(*sim.FindBodyById("vance-star")));
  CHECK_FALSE(sim.CanScan(*sim.FindBodyById(outerSysId)));
  CHECK_FALSE(sim.CanScan(*sim.FindBodyById(outerRegId)));

  {
    const auto stationMid = ship::BuildStationScreen(sim, sim.Map().here);
    const ship::StationColumn* yardMidPtr = ship::FindStationCategory(stationMid, "yard");
    REQUIRE(yardMidPtr != nullptr);
    const ship::StationColumn& yardMid = *yardMidPtr;
    CHECK_FALSE(findYardRow(yardMid, "upgrade:scanner")->enabled);
    CHECK(findYardRow(yardMid, "upgrade:scanner2")->enabled);

    const auto nav1 = ship::BuildNavScreen(sim);
    bool sawTier1Unlock = false;
    for (const auto& line : nav1.screen.right[0].lines) {
      if (line.value.find("upgraded sensor lock") != std::string::npos) {
        sawTier1Unlock = true;
      }
    }
    CHECK(sawTier1Unlock);
  }

  // Run a remote regional scan on sec-vance from Kessler!
  REQUIRE(sim.SelectTarget("sec-vance"));
  REQUIRE(sim.StartScan());
  while (sim.FindOngoing("scan")->state != "done") {
    sim.MutableLife().canisters = 6;
    sim.MutableMess().rack = 10;
    REQUIRE(sim.StandWatch());
  }
  REQUIRE(sim.Acknowledge("scan"));
  CHECK(sim.State().scanned.at("sec-vance"));

  // Buy Tier 2 ("scanner2"): scanCost drops to 2, ALL scanLocked entries unlock,
  // and deep-region systems & outer-galaxy clusters can be scanned remotely + saved mid-scan.
  REQUIRE(sim.BuyUpgrade("scanner2"));
  CHECK(sim.SensorTier() == 2);
  CHECK(sim.ScanCost() == 2.0);
  CHECK(sim.CanScan(*sim.FindBodyById(outerSysId)));
  CHECK(sim.CanScan(*sim.FindBodyById(outerRegId)));

  REQUIRE(sim.SelectTarget(localWithLocked));
  {
    const auto nav2 = ship::BuildNavScreen(sim);
    for (const auto& line : nav2.screen.right[0].lines) {
      CHECK(line.value.find("sensors insufficient") == std::string::npos);
      CHECK(line.value.find("deep-array sensors required") == std::string::npos);
    }
  }

  // Start a deep-region scan on an outer galaxy cluster and round-trip save/load mid-scan.
  REQUIRE(sim.SelectTarget(outerRegId));
  REQUIRE(sim.StartScan());
  std::string why;
  const std::string saved = sim.SaveJson();
  INFO(why);
  REQUIRE(ship::Simulation::ValidSave(saved, World(), &why));
  ship::Simulation restored = Fresh();
  REQUIRE(restored.LoadJson(saved, &why));
  while (restored.FindOngoing("scan")->state != "done") {
    restored.MutableLife().canisters = 6;
    restored.MutableMess().rack = 10;
    REQUIRE(restored.StandWatch());
  }
  const std::size_t surveysBefore = restored.Survey().size();
  REQUIRE(restored.Acknowledge("scan"));
  CHECK(restored.State().scanned.at(outerRegId));
  REQUIRE(restored.Survey().size() == surveysBefore + 1);
  CHECK(restored.Survey().back().value >= 180.0);  // 145 * 1.25 deep-array bonus
}

TEST_CASE("planet ore extraction, moon mining, gas skimming, and debris looting work end-to-end",
          "[nav][extract][expansion]") {
  ship::Simulation sim = Fresh();
  sim.MutableState().docked = false;

  // Find a rocky planet, a moon, a gas giant, a belt, and a wreck/anomaly site.
  const ship::Body* rockyPlanet = nullptr;
  const ship::Body* moonBody = nullptr;
  const ship::Body* gasPlanet = nullptr;
  const ship::Body* beltBody = nullptr;
  const ship::Body* siteBody = nullptr;

  for (const ship::Body& b : sim.World().bodies) {
    if (sim.World().FindPort(b.id) != nullptr) { continue; }
    const bool isGas = b.subtype == "gas" || b.klass.find("Gas") != std::string::npos ||
                       b.klass.find("gas") != std::string::npos;
    const bool isIce = b.subtype == "ice" || b.subtype == "ocean" ||
                       b.klass.find("Ice") != std::string::npos ||
                       b.klass.find("ice") != std::string::npos;
    if (b.type == "planet" && !isGas && !isIce && rockyPlanet == nullptr) { rockyPlanet = &b; }
    if (b.type == "moon" && moonBody == nullptr) { moonBody = &b; }
    if (b.type == "planet" && isGas && gasPlanet == nullptr) { gasPlanet = &b; }
    if (b.type == "belt" && beltBody == nullptr) { beltBody = &b; }
    if (b.type == "site" && siteBody == nullptr) { siteBody = &b; }
  }
  REQUIRE(rockyPlanet != nullptr);
  REQUIRE(moonBody != nullptr);
  REQUIRE(gasPlanet != nullptr);
  REQUIRE(beltBody != nullptr);
  REQUIRE(siteBody != nullptr);

  auto tickUntilDone = [](ship::Simulation& s, const std::string& id) {
    while (s.FindOngoing(id) != nullptr && s.FindOngoing(id)->state != "done") {
      s.FindStat("energy")->value = 100.0;
      s.FindStat("hull")->value = 100.0;
      s.MutableState().filterLife = 100.0;
      s.MutableState().airHold = 20.0;
      s.MutableLife().canisters = 6;
      s.MutableMess().rack = 10;
      REQUIRE(s.StandWatch());
    }
  };

  auto parkAt = [&](const ship::Body& target) {
    sim.FindStat("energy")->value = 100.0;
    sim.FindStat("hull")->value = 100.0;
    sim.MutableState().filterLife = 100.0;
    sim.MutableState().airHold = 20.0;
    sim.MutableMap().here = target.id;
    sim.MutableState().docked = false;
    REQUIRE(sim.SelectTarget(target.id));
  };

  // 1. Rocky planet ore extraction: scanned data reveals lootable yield,
  //    extraction reserves 4 m3 while running/done, enforces a 5-action cooldown,
  //    and is only possible once per body!
  sim.MutableGoods().clear();
  parkAt(*rockyPlanet);
  CHECK(sim.ExtractionMaxPasses(*rockyPlanet) == 1);
  CHECK(sim.ExtractionActionLabel(*rockyPlanet) == "Extract planetary ore");

  // Scanned data includes what can be looted.
  sim.MutableState().scanned[rockyPlanet->id] = true;
  {
    const auto navScanned = ship::BuildNavScreen(sim);
    bool sawLootable = false;
    for (const auto& line : navScanned.screen.right[0].lines) {
      if (line.value.find("Lootable:") != std::string::npos &&
          line.value.find("Depleted") == std::string::npos) {
        sawLootable = true;
      }
    }
    CHECK(sawLootable);
  }

  const double usedBefore = sim.CargoUsed();
  REQUIRE(ship::PressNavAction(sim, "extract"));
  CHECK(ship::CooldownRemaining(sim, ship::ActionCooldownKey(sim, "progress:extract")) == 5.0);
  REQUIRE(sim.FindOngoing("extract") != nullptr);
  CHECK(sim.CargoReserved() == 4.0);
  CHECK_FALSE(sim.StartTravel());  // cannot jump away with surface drills deployed

  std::string why;
  const std::string savedMidExtract = sim.SaveJson();
  INFO(why);
  REQUIRE(ship::Simulation::ValidSave(savedMidExtract, World(), &why));
  REQUIRE(sim.LoadJson(savedMidExtract, &why));
  CHECK(sim.CargoReserved() == 4.0);

  tickUntilDone(sim, "extract");
  CHECK(sim.CargoReserved() == 4.0);  // still reserved while waiting to haul aboard
  REQUIRE(ship::PressNavAction(sim, "extract"));  // acknowledges & hauls ore aboard
  CHECK(sim.FindOngoing("extract") == nullptr);
  CHECK(sim.CargoUsed() > usedBefore);
  CHECK(sim.ExtractionPassesUsed(*rockyPlanet) == 1);
  CHECK(ship::CooldownRemaining(sim, ship::ActionCooldownKey(sim, "progress:extract")) == 5.0);

  // Even after cooldown clears, looting the same body a second time is refused,
  // and Scan Data reports it depleted.
  sim.MutableState().actionCooldowns.clear();
  CHECK_FALSE(sim.StartExtraction());
  {
    const auto navAfter = ship::BuildNavScreen(sim);
    bool sawDepleted = false;
    for (const auto& line : navAfter.screen.right[0].lines) {
      if (line.value.find("Lootable: Depleted") != std::string::npos) {
        sawDepleted = true;
      }
    }
    CHECK(sawDepleted);
  }

  // 2. Gas giant atmospheric skimming needs 0 m3 cargo hold and condenses fuel + cells.
  sim.MutableGoods().clear();
  parkAt(*gasPlanet);
  sim.FindStat("fuel")->value = 40.0;
  const double cells0 = sim.State().fuelCells;
  REQUIRE(sim.StartExtraction());
  CHECK(sim.CargoReserved() == 0.0);
  tickUntilDone(sim, "extract");
  REQUIRE(sim.Acknowledge("extract"));
  CHECK(sim.StatValue("fuel") > 40.0);
  CHECK(sim.State().fuelCells >= cells0 + 1.0);

  // 3. Moon mining & Debris/Site looting produce cargo lots and can trigger live hazards.
  parkAt(*moonBody);
  const std::size_t lotsBeforeMoon = sim.Goods().size();
  REQUIRE(sim.StartExtraction());
  tickUntilDone(sim, "extract");
  REQUIRE(sim.Acknowledge("extract"));
  CHECK(sim.Goods().size() == lotsBeforeMoon + 1);

  parkAt(*beltBody);
  const std::size_t lotsBeforeBelt = sim.Goods().size();
  REQUIRE(sim.StartExtraction());
  tickUntilDone(sim, "extract");
  REQUIRE(sim.Acknowledge("extract"));
  CHECK(sim.Goods().size() == lotsBeforeBelt + 1);
  CHECK(sim.Goods().back().kind == "Salvage");

  parkAt(*siteBody);
  REQUIRE(sim.StartExtraction());
  tickUntilDone(sim, "extract");
  REQUIRE(sim.Acknowledge("extract"));
  CHECK(sim.ExtractionPassesUsed(*siteBody) == 1);
}

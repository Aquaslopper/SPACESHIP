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

  ship::Simulation sim = Fresh();
  for (int i = 0; i < 12; ++i) { sim.ZoomMap(1); }
  CHECK(sim.Map().zoom == 2.0);
  for (int i = 0; i < 20; ++i) { sim.ZoomMap(-1); }
  CHECK(sim.Map().zoom == 0.6);

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

  // Every region is expanded, and nothing authored is lost.
  CHECK(grown.regions.size() == authored.regions.size());
  CHECK(grown.systems.size() ==
        authored.systems.size() +
            static_cast<std::size_t>(ship::kNewSystemsPerRegion) * authored.regions.size());
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
    std::size_t local = 0;
    for (const ship::Body& body : grown.bodies) {
      if (grown.BodySystem(body) == system.id) { ++local; }
    }
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
  CHECK((kesslerGrown->x - kesslerAuthored->x) == 638.0);
  CHECK((kesslerGrown->y - kesslerAuthored->y) == 278.0);

  // ---- every system is a system, not a diagram of one --------------------
  // The reference gives a system five local bodies; the growth gives each one
  // worlds of its own, on rings, so the local chart has something to show.
  std::size_t thinnest = 999;
  for (const ship::Body& system : grown.systems) {
    std::size_t local = 0;
    for (const ship::Body& body : grown.bodies) {
      if (grown.BodySystem(body) == system.id) { ++local; }
    }
    thinnest = std::min(thinnest, local);
    // ...and the summary says what the chart holds.
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
  // Every system is placed inside its region's cluster, so the third layer is
  // the same two hundred stars the other two layers draw.
  CHECK(grown.galaxyPositions.size() == grown.systems.size());
  {
    std::set<std::string> dots;
    std::string clash;
    for (const ship::Body& system : grown.systems) {
      const auto it = grown.galaxyPositions.find(system.id);
      REQUIRE(it != grown.galaxyPositions.end());
      CHECK(it->second.first >= 0.0);
      CHECK(it->second.first <= grown.map.w);
      CHECK(it->second.second >= 0.0);
      CHECK(it->second.second <= grown.map.h);
      const std::string key = ship::js::NumToString(it->second.first) + "," +
                              ship::js::NumToString(it->second.second);
      if (!dots.insert(key).second) { clash = system.id; }
    }
    INFO(clash);
    CHECK(clash.empty());
  }
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

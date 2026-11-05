// ============================================================================
//  Navigation.cpp — the chart, the crossing and the sensors.
//  ---------------------------------------------------------------------------
//  Looking is free. Every scale of the chart, every pan, every selection and
//  every readout of a body's name, class and distance costs nothing. Two things
//  cost: plotting a crossing (one action now, then the distance in actions spent
//  elsewhere) and asking the sensors for more than the chart already shows.
//
//  Distances are DERIVED, never authored. They are measured from MAP.here at the
//  moment of the question, which is why the ship can relocate on arrival without
//  invalidating a single route.
// ============================================================================
#include "core/JsNumber.h"
#include "core/Simulation.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ship {

namespace {

std::string Plur(double n, const std::string& word) {
  return js::NumToString(n) + " " + word + (n == 1 ? "" : "s");
}

}  // namespace

// ---------------------------------------------------------------------------
// The chart's own questions
// ---------------------------------------------------------------------------

const Body* Simulation::FindBodyById(const std::string& id) const {
  // ids are unique across every layer, so a lookup does not need to know which
  // map the player is looking at.
  for (const std::string& layer : catalog_.layerOrder) {
    const std::vector<Body>* list = nullptr;
    if (layer == "system") { list = &catalog_.bodies; }
    if (layer == "sector") { list = &catalog_.systems; }
    if (layer == "galaxy") { list = &catalog_.regions; }
    if (list == nullptr) { continue; }
    for (const Body& body : *list) {
      if (body.id == id) { return &body; }
    }
  }
  return nullptr;
}

std::string Simulation::CurrentSystemId() const {
  const Body* here = FindBodyById(map_.here);
  return here == nullptr ? std::string("sec-kessler") : catalog_.BodySystem(*here);
}

const Body* Simulation::CurrentSystem() const { return FindBodyById(CurrentSystemId()); }

bool Simulation::InRange(const Body& body) const {
  if (body.type == "region") { return false; }
  if (body.type == "system") { return !body.entry.empty(); }
  return catalog_.BodySystem(body) == CurrentSystemId();
}

bool Simulation::AtTarget(const Body& body) const {
  if (body.type == "system") { return body.id == CurrentSystemId(); }
  return body.id == map_.here;
}

const Body* Simulation::HomeBody() const {
  // Position at the current map scale, not the region being browsed.
  const Body* system = CurrentSystem();
  if (map_.layer == "system") { return FindBodyById(map_.here); }
  if (map_.layer == "sector") {
    if (system != nullptr && system->region == map_.sector) { return system; }
    return nullptr;
  }
  return system == nullptr ? nullptr : FindBodyById(system->region);
}

std::vector<const Body*> Simulation::CurBodies() const {
  std::vector<const Body*> out;
  if (map_.layer == "galaxy") {
    for (const Body& body : catalog_.regions) { out.push_back(&body); }
    return out;
  }
  if (map_.layer == "system") {
    const std::string sid = CurrentSystemId();
    for (const Body& body : catalog_.bodies) {
      if (catalog_.BodySystem(body) == sid) { out.push_back(&body); }
    }
    return out;
  }
  for (const Body& body : catalog_.systems) {
    if (body.region == map_.sector) { out.push_back(&body); }
  }
  return out;
}

void Simulation::SetLayer(const std::string& key) {
  if (key == map_.layer) { return; }
  if (key != "system" && key != "sector" && key != "galaxy") { return; }

  // Each layer keeps its own zoom, pan and selection, so coming back finds the
  // map where it was left.
  LayerView& old = views_[map_.layer];
  old.zoom = map_.zoom;
  old.sel = state_.navTarget.value_or(std::string());

  map_.layer = key;
  LayerView& fresh = views_[key];
  map_.zoom = fresh.zoom;
  state_.navTarget = fresh.sel.empty() ? std::optional<std::string>() : std::optional<std::string>(fresh.sel);
  // The drifting rocks and the station beacon belong to the system view; the
  // client starts and stops that animation from here.
}

bool Simulation::ZoomMap(int dir) {
  // Free. One step on the ladder, clamped, then rounded to a tenth exactly as
  // the reference rounds it, so the caption and the raster always agree.
  double zoom = map_.zoom + static_cast<double>(dir) * map_.zstep;
  if (zoom < map_.zmin) { zoom = map_.zmin; }
  if (zoom > map_.zmax) { zoom = map_.zmax; }
  map_.zoom = js::Round(zoom * 10) / 10;
  return true;
}

void Simulation::PanMap(double dx, double dy) {
  // The chart may be pulled half a screen out of place, no further, or it can
  // be lost off the edge and look broken. Panning is free.
  LayerView& view = views_[map_.layer];
  const double limitX = js::Round(map_.vw * 0.5);
  const double limitY = js::Round(map_.vh * 0.5);
  view.px = std::max(-limitX, std::min(limitX, view.px + dx));
  view.py = std::max(-limitY, std::min(limitY, view.py + dy));
}

bool Simulation::ViewSector(const std::string& regionId) {
  if (FindBodyById(regionId) == nullptr) { return false; }
  const Body* region = nullptr;
  for (const Body& body : catalog_.regions) {
    if (body.id == regionId) { region = &body; }
  }
  if (region == nullptr) { return false; }

  const Body* system = CurrentSystem();
  map_.sector = regionId;
  views_["sector"].sel = (system != nullptr && system->region == regionId) ? system->id : std::string();
  SetLayer("sector");
  return true;
}

bool Simulation::SelectTarget(const std::string& bodyId) {
  if (FindBodyById(bodyId) == nullptr) { return false; }
  state_.navTarget = bodyId;
  views_[map_.layer].sel = bodyId;
  return true;
}

// ---------------------------------------------------------------------------
// navMetrics(): what a crossing would cost
// ---------------------------------------------------------------------------

CrewMember* Simulation::AssignedNavigator() {
  for (CrewMember& crew : crew_) {
    if (crew.job == "navigation" && CrewBusy(crew) == nullptr) { return &crew; }
  }
  return nullptr;
}

Simulation::NavMetrics Simulation::MetricsFor(const Body& body) const {
  const Body* here = FindBodyById(map_.here);
  double distance = 0;

  if (body.type == "system") {
    const Body* origin = CurrentSystem();
    double px = 0;
    if (origin != nullptr && body.region == origin->region) {
      const double dx = body.x - origin->x;
      const double dy = body.y - origin->y;
      px = std::sqrt(dx * dx + dy * dy);
    } else if (origin != nullptr) {
      const Body* fromRegion = nullptr;
      const Body* toRegion = nullptr;
      for (const Body& region : catalog_.regions) {
        if (region.id == origin->region) { fromRegion = &region; }
        if (region.id == body.region) { toRegion = &region; }
      }
      double dx = 0;
      double dy = 0;
      if (fromRegion != nullptr && toRegion != nullptr) {
        dx = (toRegion->x - fromRegion->x) * 1.3;
        dy = (toRegion->y - fromRegion->y) * 1.3;
      }
      px = std::sqrt(dx * dx + dy * dy) + 150;
    }
    distance = std::max(6.0, js::Round(px / 24.0) + 5.0);
  } else {
    const double dx = body.x - (here == nullptr ? 0.0 : here->x);
    const double dy = body.y - (here == nullptr ? 0.0 : here->y);
    const double px = std::sqrt(dx * dx + dy * dy);
    distance = std::max(3.0, js::Round(px / map_.pxPerAction));
  }
  if (AtTarget(body)) { distance = 0; }

  // The navigator reduces FUEL, never travel time: a busy navigator cannot plot
  // a more efficient course, and training raises the saving.
  Simulation* self = const_cast<Simulation*>(this);
  const CrewMember* navigator = nullptr;
  for (const CrewMember& crew : crew_) {
    if (crew.job == "navigation" && CrewBusy(crew) == nullptr) { navigator = &crew; break; }
  }
  (void)self;
  const double discount = navigator == nullptr
                              ? 0.0
                              : 4.0 * std::max(1.0, std::min(5.0, navigator->jobAt == 0 ? 1.0 : navigator->jobAt));
  const double drivePercent = state_.upgrades.count("drive") != 0 && state_.upgrades.at("drive") ? 80.0 : 100.0;
  const double navPercent = 100.0 - discount;

  NavMetrics metrics;
  metrics.dist = distance;
  metrics.fuel = js::Ceil(distance * drivePercent * navPercent / 10000.0);
  metrics.total = distance + 1;

  const double ly = distance * map_.lyPerAction;
  const double e13 = (ly * 9.46) / 10.0;
  const double days = distance * 19.0;
  const double months = js::Floor(days / 30.0);
  const double remainder = days - months * 30.0;

  metrics.ly = js::ToFixed(ly, 1) + " ly";
  metrics.km = js::ToFixed(e13, 1) + " x 10^13 km";
  metrics.transit = months > 0 ? "~" + Plur(months, "month") + ", " + Plur(remainder, "day")
                               : "~" + Plur(days, "day");
  return metrics;
}

// ---------------------------------------------------------------------------
// Commitments
// ---------------------------------------------------------------------------

bool Simulation::StartTravel() {
  if (!state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !InRange(*body) || AtTarget(*body)) { return false; }
  if (FindOngoing("travel") != nullptr) { return false; }
  if (state_.docked || state_.engineCondition <= 0 || StatValue("energy") <= 0) { return false; }

  const NavMetrics metrics = MetricsFor(*body);
  if (StatValue("fuel") < metrics.fuel) { return false; }
  Stat* fuel = FindStat("fuel");
  if (fuel != nullptr) { fuel->value -= metrics.fuel; }

  // Order matters. The initiating action is spent FIRST, so the tick it causes
  // cannot eat a step of the journey it starts.
  PerformAction("Course locked - " + body->name);
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("travel", "Under way to " + body->name, metrics.dist, "travel", body->id);
  return true;
}

bool Simulation::StartScan() {
  if (!state_.navTarget.has_value()) { return false; }
  const Body* body = FindBodyById(*state_.navTarget);
  if (body == nullptr || !InRange(*body) || body->type == "system") { return false; }
  if (FindOngoing("scan") != nullptr || StatValue("energy") <= 0) { return false; }

  // Same ordering rule as a crossing: the initiating action first.
  const double cost = std::max(1.0, scanCost_ - (state_.sim.sensor ? 2.0 : 0.0));
  state_.sim.sensor = 0;
  PerformAction("Deep scan started - " + body->name);
  if (!state_.gameOver.empty()) { return true; }

  AddOngoing("scan", "Deep scan - " + body->name, cost, "scan", body->id);
  return true;
}

bool Simulation::StandWatch() {
  if (!state_.gameOver.empty()) { return false; }
  PerformAction("Stood watch");
  return true;
}

bool Simulation::Acknowledge(const std::string& taskId) {
  Task* task = FindOngoing(taskId);
  if (task == nullptr || task->state != "done") { return false; }

  // The hook runs BEFORE the task is cleared: the arrival happens while the
  // crossing is still on the books, exactly as the reference does it.
  const std::string type = task->type;
  const std::string target = task->target;

  bool handled = false;
  if (type == "travel" && !target.empty()) { ArriveAt(target); handled = true; }
  else if (type == "scan") { RevealScan(target); handled = true; }

  ClearOngoing(taskId);
  LogLine("Process acknowledged and cleared.");
  return handled;
}

// ---------------------------------------------------------------------------
// Arrival and revelation
// ---------------------------------------------------------------------------

void Simulation::RevealScan(const std::string& bodyId) {
  const Body* body = FindBodyById(bodyId);
  if (body == nullptr) { return; }

  const bool first = state_.scanned.count(body->id) == 0 || !state_.scanned.at(body->id);
  state_.scanned[body->id] = true;

  // simOnScan(): the two authored signal beats.
  if (first) {
    SimRemember("Discovery", "The crew scanned " + body->name + ".");
  }
  if (body->id == "halvan" && state_.sim.signal == "dormant") {
    state_.sim.signal = "intercepted";
    SimRemember("Signal", "An old relay at Halvan's Reach repeats a name.");
    LogLine("An unusual signal was intercepted. Open the Comms Relay.");
  }
  if (body->id == "echo-site" && state_.sim.signal == "coordinates") {
    state_.sim.signal = "request";
    SimRemember("Signal", "The Choir Array carries a request from Silicon Marrow.");
    LogLine("The Choir Array answered. Open the Comms Relay.");
  }

  FileSurvey(*body);
  ContractProgress("scan", body->id);
  CommissionProgress("scan", body->id);
  CheckCommendations();

  if (catalog_.BodySystem(*body) == CurrentSystemId()) {
    views_["system"].sel = body->id;
    if (map_.layer == "system") { state_.navTarget = body->id; }
  }
  LogLine("Deep scan readout filed: " + body->name + ". Survey record stored in the Science Room.");
}

void Simulation::FileSurvey(const Body& body) {
  for (const SurveyRecord& record : survey_) {
    if (record.id == "sv-" + body.id) { return; }  // already held
  }
  double worth = 60;
  auto it = catalog_.surveyWorth.find(body.type);
  if (it != catalog_.surveyWorth.end()) { worth = it->second; }

  SurveyRecord record;
  record.id = "sv-" + body.id;
  record.name = body.name;
  record.src = "Deep scan";
  record.value = worth;
  record.state = "filed";
  survey_.push_back(record);
}

void Simulation::ChronicleVisit(const Body* system) {
  if (system == nullptr) { return; }
  state_.chronicle.systems[system->id] = true;
  state_.chronicle.regions[system->region] = true;
}

void Simulation::ArriveAt(const std::string& bodyId) {
  const Body* system = nullptr;
  for (const Body& body : catalog_.systems) {
    if (body.id == bodyId) { system = &body; }
  }

  const std::string localId = system != nullptr ? system->entry : bodyId;
  const Body* body = FindBodyById(localId);
  if (body == nullptr) { return; }

  map_.here = body->id;
  views_["system"].sel = body->id;

  if (system != nullptr) {
    map_.sector = system->region;
    views_["sector"].sel = system->id;
    views_["galaxy"].sel = system->region;
    if (map_.layer == "sector") { state_.navTarget = system->id; }
    else if (map_.layer == "galaxy") { state_.navTarget = system->region; }
    ContractProgress("visit", system->id);
  }
  if (map_.layer == "system") { state_.navTarget = body->id; }

  state_.docked = HerePort() != nullptr && HerePort()->id == body->id;
  RefreshLocationInfo();
  ContractProgress("visit", body->id);

  LogLine("Arrived at " + body->name +
          (state_.docked ? " - docked. Right-click the docked indicator to open the station" : "") + ".");
  SimRemember("Arrival", "The ship arrived at " + body->name + ".");
  ChronicleVisit(CurrentSystem());
  CommissionProgress("visit", body->id);
  CheckCommendations();
}

void Simulation::RefreshLocationInfo() {
  const Body* here = FindBodyById(map_.here);
  const std::string name = here == nullptr ? std::string("Unknown") : here->name;
  const bool docked = HerePort() != nullptr;
  for (InfoRow& row : infos_) {
    if (row.id == "location") {
      row.value = name + (docked ? " - docked" : "");
    } else if (row.id == "contacts") {
      const Body* system = CurrentSystem();
      row.value = docked ? name + " - station contact"
                         : "Charted objects in " + (system == nullptr ? std::string("unknown space") : system->name);
    }
  }
}

bool Simulation::Dock() {
  const Port* port = HerePort();
  if (port == nullptr || state_.docked || !state_.gameOver.empty()) { return false; }
  const std::string name = port->name;
  state_.docked = true;
  ContractProgress("visit", map_.here);
  for (InfoRow& row : infos_) {
    if (row.id == "location") { row.value = name + " - docked"; }
  }
  PerformAction("Docked at " + name);
  return true;
}

bool Simulation::Undock() {
  const Port* port = HerePort();
  if (port == nullptr || !state_.docked || !state_.gameOver.empty()) { return false; }
  const std::string name = port->name;
  state_.docked = false;
  state_.activeTab = "home";
  for (InfoRow& row : infos_) {
    if (row.id == "location") { row.value = name + " - undocked"; }
  }
  PerformAction("Undocked from " + name);
  return true;
}

}  // namespace ship

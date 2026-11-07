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
#include <cstdint>
#include <string>

namespace ship {

namespace {

std::string Plur(double n, const std::string& word) {
  return js::NumToString(n) + " " + word + (n == 1 ? "" : "s");
}

std::uint64_t MapEventHash(const std::string& key) {
  std::uint64_t h = 0xcbf29ce484222325ULL;
  for (const char c : key) {
    h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::string DetailValue(const Body& body, const std::string& label) {
  for (const auto& entry : body.details) {
    if (entry.first == label) { return entry.second; }
  }
  return std::string();
}

bool IsProceduralId(const std::string& id) {
  return id.rfind("sec-gen-", 0) == 0 || id.rfind("gal-gen-", 0) == 0 ||
         id.rfind("gen-", 0) == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The chart's own questions
// ---------------------------------------------------------------------------

const Body* Simulation::FindBodyById(const std::string& id) const {
  if (const Body* body = catalog_.FindBody(id)) { return body; }
  if (const Body* system = catalog_.FindSystem(id)) { return system; }
  if (const Body* region = catalog_.FindRegion(id)) { return region; }
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
  // Position at the current map scale, not the region being browsed. The
  // sector chart paints every system, so the ship's own system is always on it
  // whether or not it belongs to the region being read.
  const Body* system = CurrentSystem();
  if (map_.layer == "system") { return FindBodyById(map_.here); }
  if (map_.layer == "sector") { return system; }
  return system == nullptr ? nullptr : FindBodyById(system->region);
}

void Simulation::ApplyChartBoard() {
  const MapGeometry& authored = catalog_.map;
  auto it = catalog_.layerBoards.find(map_.layer);
  const MapGeometry& board = it == catalog_.layerBoards.end() ? authored : it->second;
  map_.w = board.w;
  map_.h = board.h;
  map_.cx = board.cx;
  map_.cy = board.cy;
  map_.vw = board.w;
  map_.vh = board.h;
  map_.zmin = board.zmin;
  map_.zmax = board.zmax;
  map_.zstep = board.zstep;
  map_.grid = board.grid;
  map_.vpy = board.vpy;
  map_.rungs = board.rungs;
  map_.rungPow = board.rungPow;
  map_.pxPerAction = board.pxPerAction;
  map_.lyPerAction = board.lyPerAction;
}

std::vector<const Body*> Simulation::CurBodies() const {
  std::vector<const Body*> out;
  if (map_.layer == "galaxy") {
    out.reserve(catalog_.regions.size());
    for (const Body& body : catalog_.regions) { out.push_back(&body); }
    return out;
  }
  if (map_.layer == "system") {
    const auto& indices = catalog_.BodiesInSystem(CurrentSystemId());
    out.reserve(indices.size());
    for (std::size_t idx : indices) {
      if (idx < catalog_.bodies.size()) { out.push_back(&catalog_.bodies[idx]); }
    }
    return out;
  }
  out.reserve(catalog_.systems.size());
  for (const Body& body : catalog_.systems) { out.push_back(&body); }
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
  ApplyChartBoard();
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
  LayerView& view = views_[map_.layer];
  const double spanX = std::max(map_.vw, map_.w);
  const double spanY = std::max(map_.vh, map_.h);
  const double limitX = js::Round(spanX * 0.5 * map_.zoom);
  const double limitY = js::Round(spanY * 0.5 * map_.zoom);
  view.px = std::max(-limitX, std::min(limitX, view.px + dx));
  view.py = std::max(-limitY, std::min(limitY, view.py + dy));
}

bool Simulation::ViewSector(const std::string& regionId) {
  const Body* region = catalog_.FindRegion(regionId);
  if (region == nullptr) { return false; }

  const Body* system = CurrentSystem();
  map_.sector = regionId;
  LayerView& secView = views_["sector"];
  secView.sel = (system != nullptr && system->region == regionId) ? system->id : std::string();

  // Centre the sector camera over the chosen galactic region's star cluster so
  // clicking any galaxy on Layer 3 frames its systems immediately.
  const auto& sysIndices = catalog_.SystemsInRegion(regionId);
  auto boardIt = catalog_.layerBoards.find("sector");
  const MapGeometry& secBoard = boardIt == catalog_.layerBoards.end() ? catalog_.map : boardIt->second;
  if (!sysIndices.empty()) {
    double sumX = 0.0;
    double sumY = 0.0;
    for (std::size_t idx : sysIndices) {
      sumX += catalog_.systems[idx].x;
      sumY += catalog_.systems[idx].y;
    }
    const double meanX = sumX / static_cast<double>(sysIndices.size());
    const double meanY = sumY / static_cast<double>(sysIndices.size());
    const double z = secView.zoom > 0.0 ? secView.zoom : 1.0;
    const double limX = js::Round(secBoard.w * 0.5 * z);
    const double limY = js::Round(secBoard.h * 0.5 * z);
    secView.px = std::clamp(js::Round((secBoard.cx - meanX) * z), -limX, limX);
    secView.py = std::clamp(js::Round((secBoard.cy - meanY) * z), -limY, limY);
  }

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
  static const std::string kDefaultSystem = "sec-kessler";
  const Body* here = catalog_.FindBody(map_.here);
  const std::string& curSysId =
      (here == nullptr || here->system.empty()) ? kDefaultSystem : here->system;
  double distance = 0;

  if (body.type == "system") {
    const Body* origin = catalog_.FindSystem(curSysId);
    double px = 0;
    if (origin != nullptr && body.region == origin->region) {
      const double dx = body.x - origin->x;
      const double dy = body.y - origin->y;
      px = std::sqrt(dx * dx + dy * dy);
    } else if (origin != nullptr) {
      const Body* fromRegion = catalog_.FindRegion(origin->region);
      const Body* toRegion = catalog_.FindRegion(body.region);
      double dx = 0;
      double dy = 0;
      if (fromRegion != nullptr && toRegion != nullptr) {
        dx = (toRegion->x - fromRegion->x) * 1.3;
        dy = (toRegion->y - fromRegion->y) * 1.3;
      }
      px = std::sqrt(dx * dx + dy * dy) + 150;
      if (px > 1050.0) {
        const double excess = px - 1050.0;
        px = 1050.0 + 1150.0 * (excess / (excess + 1400.0));
      }
    }
    distance = std::max(6.0, js::Round(px / 24.0) + 5.0);
    if (body.id == curSysId) { distance = 0; }
  } else {
    const double dx = body.x - (here == nullptr ? 0.0 : here->x);
    const double dy = body.y - (here == nullptr ? 0.0 : here->y);
    const double px = std::sqrt(dx * dx + dy * dy);
    distance = std::max(3.0, js::Round(px / map_.pxPerAction));
    if (body.id == map_.here) { distance = 0; }
  }

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
  ResolveMapScanEvents(*body, first);
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
  const Body* prevSystem = CurrentSystem();
  const std::string prevRegion = prevSystem == nullptr ? std::string() : prevSystem->region;
  const Body* system = catalog_.FindSystem(bodyId);

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
  ResolveMapArrivalEvents(*body, system, prevRegion);
  CommissionProgress("visit", body->id);
  CheckCommendations();
}

void Simulation::ResolveMapArrivalEvents(const Body& localBody, const Body* jumpedSystem,
                                         const std::string& prevRegion) {
  // 1. Interstellar jumps into procedural systems/galaxies can trigger regional
  //    navigation hazards or relay telemetry bursts on first entry.
  if (jumpedSystem != nullptr && IsProceduralId(jumpedSystem->id)) {
    const std::string visitKey = "map-jump:" + jumpedSystem->id;
    if (!state_.sim.observed[visitKey]) {
      state_.sim.observed[visitKey] = true;
      const Body* region = catalog_.FindRegion(jumpedSystem->region);
      const std::string hazard = region == nullptr ? std::string() : DetailValue(*region, "Dominant hazard");
      const std::string auth = region == nullptr ? std::string() : DetailValue(*region, "Authority");
      const bool crossGalaxy = !prevRegion.empty() && prevRegion != jumpedSystem->region;
      const std::uint64_t roll = MapEventHash(jumpedSystem->id) % 100ULL;

      if (crossGalaxy && IsProceduralId(jumpedSystem->region)) {
        const std::string galKey = "map-galaxy:" + jumpedSystem->region;
        if (!state_.sim.observed[galKey]) {
          state_.sim.observed[galKey] = true;
          const std::string rname = region == nullptr ? jumpedSystem->region : region->name;
          SimRemember("World", "Crossed into outer cluster " + rname + ".");
          LogLine("Chart milestone: entered " + rname + ". Regional beacons logged.");
        }
      }

      if (roll < (crossGalaxy ? 52ULL : 34ULL)) {
        if (hazard == "Ion squalls") {
          SpawnMapIncident("map-ion-squall",
                           "Charged filaments in " + jumpedSystem->name + " wash across the hull.");
        } else if (hazard == "Subspace shear") {
          SpawnMapIncident("map-grav-shear",
                           "Jump wake turbulence in " + jumpedSystem->name + " shakes the drive spine.");
        } else if (hazard == "Cryo-dust plumes") {
          SpawnMapIncident("map-cryo-spore",
                           "Brine-ice plumes in " + jumpedSystem->name + " frost the outer seals.");
        } else if (hazard == "Ghost telemetry") {
          SpawnMapIncident("map-beacon-ghost",
                           "Silicate veil echoes in " + jumpedSystem->name + " flood the comms bus.");
        } else if (hazard == "Drifting hulks") {
          SpawnMapIncident("map-derelict-ping",
                           "Graveyard debris in " + jumpedSystem->name + " crosses the arrival cone.");
        } else if (hazard == "Lane blockades" || auth == "None" || auth == "Free Captains") {
          SpawnMapIncident("map-pirate-toll",
                           "An unflagged picket sweeps the approach into " + jumpedSystem->name + ".");
        }
      } else if (roll >= 82ULL) {
        // Calm survey beacon packet on arrival.
        state_.sim.insight += 1;
        LogLine("Arrival telemetry: an automated picket buoy in " + jumpedSystem->name +
                " transmitted +1 Simulacron insight.");
      }
    }
  }

  // 2. Local arrival at a procedural anomaly/wreck/depot resolves proximity
  //    encounters once per body.
  const std::string feature = DetailValue(localBody, "Map feature");
  if (feature.empty()) { return; }
  const std::string arriveKey = "map-arr:" + localBody.id;
  if (state_.sim.observed[arriveKey]) { return; }
  state_.sim.observed[arriveKey] = true;

  if (feature == "Fuel skimmer depot") {
    ClampStat("fuel", 10.0);
    state_.fuelCells += 1;
    LogLine("Orbital skimmer at " + localBody.name +
            " transferred +10% fuel and 1 condensed fuel cell.");
    SimRemember("Discovery", "Recovered condensed fuel from an automated depot at " + localBody.name + ".");
  } else if (feature == "Ion pocket") {
    SpawnMapIncident("map-ion-squall",
                     "Local magnetosphere pocket around " + localBody.name + " surged.");
  } else if (feature == "Unstable hulk") {
    state_.shiplets += 45;
    LogLine("Recovered a 45-shiplet registry transponder from " + localBody.name + ".");
    SpawnMapIncident("map-derelict-ping",
                     "Breakaway plating from " + localBody.name + " drifted across the bow.");
  }
}

void Simulation::ResolveMapScanEvents(const Body& body, bool firstScan) {
  if (!firstScan) { return; }
  const std::string feature = DetailValue(body, "Map feature");
  if (feature.empty()) { return; }
  const std::string scanKey = "map-scan:" + body.id;
  if (state_.sim.observed[scanKey]) { return; }
  state_.sim.observed[scanKey] = true;

  if (feature == "Salvage cache") {
    state_.lotSerial += 1;
    if (CargoUsed() + 3.0 <= cargo_.capacity) {
      GoodsLot lot;
      lot.id = "salvage-map-" + std::to_string(static_cast<long long>(state_.lotSerial));
      lot.name = "Recovered Salvage (" + body.name + ")";
      lot.kind = "Salvage";
      lot.vol = 3.0;
      lot.value = 75.0;
      lot.risk = false;
      goods_.push_back(std::move(lot));
      CommissionProgress("salvage", body.id);
      LogLine("Map discovery at " + body.name + ": 3 m3 salvage cache winched into the hold.");
    } else {
      state_.repairKits += 1;
      LogLine("Map discovery at " + body.name +
              ": hold full, stripped 1 field repair kit from the cache.");
    }
    SimRemember("Discovery", "Recovered a charted salvage cache at " + body.name + ".");
  } else if (feature == "Ancient nav buoy") {
    state_.sim.insight += 1;
    const auto& peers = catalog_.BodiesInSystem(catalog_.BodySystem(body));
    int revealed = 0;
    for (std::size_t idx : peers) {
      if (revealed >= 2 || idx >= catalog_.bodies.size()) { break; }
      const Body& peer = catalog_.bodies[idx];
      if (peer.id == body.id || state_.scanned[peer.id]) { continue; }
      state_.scanned[peer.id] = true;
      FileSurvey(peer);
      ++revealed;
    }
    LogLine("Ancient nav buoy at " + body.name + " unlocked " + std::to_string(revealed) +
            " local telemetry chart" + (revealed == 1 ? "" : "s") + " and +1 insight.");
    SimRemember("Discovery", "Decoded an ancient navigation buoy near " + body.name + ".");
  } else if (feature == "Rich mineral seam") {
    const std::string bonusId = "sv-seam-" + body.id;
    bool exists = false;
    for (const SurveyRecord& rec : survey_) {
      if (rec.id == bonusId) { exists = true; break; }
    }
    if (!exists) {
      SurveyRecord bonus;
      bonus.id = bonusId;
      bonus.name = body.name + " Core Assay";
      bonus.src = "Anomaly scan";
      bonus.value = 110.0;
      bonus.state = "filed";
      survey_.push_back(std::move(bonus));
      LogLine("Rich mineral assay filed at " + body.name + " (worth 110 sh in Science).");
    }
  } else if (feature == "Ion pocket") {
    state_.sim.insight += 1;
    state_.sim.sensor = 1;
    LogLine("Magnetospheric pocket at " + body.name +
            " calibrated the sensor array (+1 insight, next scan -2 work).");
  }
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
